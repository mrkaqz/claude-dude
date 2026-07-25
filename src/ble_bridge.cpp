#include "ble_bridge.h"
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLESecurity.h>
#include <BLE2902.h>
#include <Arduino.h>
#include <string.h>
// PLAN.md's Phase 1 file layout says this file is kept "verbatim, UNCHANGED"
// from claude-desktop-buddy — that held until the BLE pairing fix pulled in
// a newer Arduino-ESP32 core (see PLATFORM comment in platformio.ini) whose
// bundled BLEDevice/BLEServer/BLESecurity classes now wrap NimBLE instead of
// Bluedroid. Same class names, but callback signatures, getValue()'s return
// type, and bond storage are all different underneath, so this file is a
// straight port to that new surface — not a redesign.
#include <host/ble_store.h>   // ble_store_util_delete_all() — bond wipe

// Nordic UART Service UUIDs — every BLE serial example uses these, so
// existing tools (nRF Connect, bluefy, Web Bluetooth examples) can talk to
// us without custom UUIDs.
#define NUS_SERVICE_UUID "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define NUS_RX_UUID      "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
#define NUS_TX_UUID      "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

// Incoming bytes are buffered in a simple ring for bleRead()/bleAvailable().
// Sized to hold a transcript snapshot JSON plus headroom; the GATT layer
// will flow-control if we fall behind.
static const size_t RX_CAP = 2048;
static uint8_t  rxBuf[RX_CAP];
static volatile size_t rxHead = 0;
static volatile size_t rxTail = 0;

static BLEServer*         server = nullptr;
static BLECharacteristic* txChar = nullptr;
static BLECharacteristic* rxChar = nullptr;
static volatile bool      connected = false;
static volatile int       connectionCount = 0;   // supports two concurrent centrals: Desktop + daemon
static volatile bool      secure = false;
static volatile uint32_t  passkey = 0;
static volatile uint16_t  mtu = 23;

static void rxPush(const uint8_t* p, size_t n) {
  for (size_t i = 0; i < n; i++) {
    size_t next = (rxHead + 1) % RX_CAP;
    if (next == rxTail) return;  // full — drop (upstream should keep up)
    rxBuf[rxHead] = p[i];
    rxHead = next;
  }
}

class RxCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    String v = c->getValue();
    if (v.length() > 0) rxPush((const uint8_t*)v.c_str(), v.length());
  }
};

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* s) override {
    connectionCount++;
    connected = true;
    Serial.printf("[ble] connected (n=%d)\n", connectionCount);
    // Keep advertising so a second central (the usage daemon, alongside
    // Claude Desktop) can also find and connect to us — CONFIG_BT_NIMBLE_
    // MAX_CONNECTIONS is 3, plenty of headroom for our two. A peripheral
    // stops advertising on its own once any one central connects, so this
    // has to be requested again explicitly on every connect, not just
    // after a disconnect.
    BLEDevice::startAdvertising();
  }
  void onDisconnect(BLEServer* s) override {
    if (connectionCount > 0) connectionCount--;
    connected = (connectionCount > 0);
    if (!connected) { secure = false; passkey = 0; mtu = 23; }
    Serial.printf("[ble] disconnected (n=%d)\n", connectionCount);
    BLEDevice::startAdvertising();
  }
  void onMtuChanged(BLEServer*, ble_gap_conn_desc*, uint16_t newMtu) override {
    mtu = newMtu;
    Serial.printf("[ble] mtu=%u\n", mtu);
  }
};

// LE Secure Connections, passkey-entry: we are DisplayOnly, the central
// is KeyboardOnly. The stack picks a random 6-digit passkey, calls
// onPassKeyNotify here, and the user types it on the desktop. main.cpp
// polls blePasskey() to render it.
class SecCallbacks : public BLESecurityCallbacks {
  uint32_t onPassKeyRequest() override { return 0; }
  bool onConfirmPIN(uint32_t) override { return false; }
  bool onSecurityRequest() override { return true; }
  void onPassKeyNotify(uint32_t pk) override {
    passkey = pk;
    Serial.printf("[ble] passkey %06lu\n", (unsigned long)pk);
  }
  // NimBLE always delivers a valid desc here (on BLE_GAP_EVENT_ENC_CHANGE,
  // for both success and failure) — sec_state.encrypted is what
  // distinguishes them, there's no separate "did it fail" signal.
  void onAuthenticationComplete(ble_gap_conn_desc* desc) override {
    passkey = 0;
    secure = desc && desc->sec_state.encrypted;
    Serial.printf("[ble] auth %s\n", secure ? "ok" : "FAIL");
    if (!secure && server) server->disconnect(server->getConnId());
  }
};

void bleInit(const char* deviceName) {
  BLEDevice::init(deviceName);
  // Request the biggest MTU we can get. macOS negotiates to 185 typically.
  BLEDevice::setMTU(517);

  // setEncryptionLevel() was Bluedroid-only; under NimBLE the
  // ESP_LE_AUTH_REQ_SC_MITM_BOND passed to setAuthenticationMode() below
  // already fully specifies the required security level.
  BLEDevice::setSecurityCallbacks(new SecCallbacks());

  server = BLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());

  BLEService* svc = server->createService(NUS_SERVICE_UUID);

  txChar = svc->createCharacteristic(
    NUS_TX_UUID,
    BLECharacteristic::PROPERTY_NOTIFY
  );
  txChar->setAccessPermissions(ESP_GATT_PERM_READ_ENCRYPTED);
  BLE2902* cccd = new BLE2902();
  cccd->setAccessPermissions(ESP_GATT_PERM_READ_ENCRYPTED | ESP_GATT_PERM_WRITE_ENCRYPTED);
  txChar->addDescriptor(cccd);

  rxChar = svc->createCharacteristic(
    NUS_RX_UUID,
    BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
  );
  rxChar->setAccessPermissions(ESP_GATT_PERM_WRITE_ENCRYPTED);
  rxChar->setCallbacks(new RxCallbacks());

  svc->start();

  BLESecurity* sec = new BLESecurity();
  sec->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM_BOND);
  sec->setCapability(ESP_IO_CAP_OUT);
  sec->setKeySize(16);
  sec->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  sec->setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);

  BLEAdvertising* adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(NUS_SERVICE_UUID);
  adv->setScanResponse(true);
  adv->setMinPreferred(0x06);   // iOS-friendly connection interval
  adv->setMaxPreferred(0x12);
  BLEDevice::startAdvertising();
  Serial.printf("[ble] advertising as '%s'\n", deviceName);
}

bool bleConnected() { return connected; }
bool bleSecure()    { return secure; }
uint32_t blePasskey() { return passkey; }

void bleClearBonds() {
  // NimBLE splits bond state across three store object types rather than
  // Bluedroid's single bond-device list; wipe all three.
  ble_store_util_delete_all(BLE_STORE_OBJ_TYPE_PEER_SEC, nullptr);
  ble_store_util_delete_all(BLE_STORE_OBJ_TYPE_OUR_SEC, nullptr);
  ble_store_util_delete_all(BLE_STORE_OBJ_TYPE_CCCD, nullptr);
  Serial.println("[ble] cleared all bonds");
}

size_t bleAvailable() {
  return (rxHead + RX_CAP - rxTail) % RX_CAP;
}

int bleRead() {
  if (rxHead == rxTail) return -1;
  int b = rxBuf[rxTail];
  rxTail = (rxTail + 1) % RX_CAP;
  return b;
}

size_t bleWrite(const uint8_t* data, size_t len) {
  if (!connected || !txChar) return 0;
  // ATT notify payload is limited to (MTU - 3). macOS negotiates 185, so
  // the 182-byte chunk works there; use the live mtu so a peer that caps
  // at the 23-byte default doesn't get truncated notifies.
  size_t chunk = mtu > 3 ? mtu - 3 : 20;
  if (chunk > 180) chunk = 180;
  size_t sent = 0;
  while (sent < len) {
    size_t n = len - sent;
    if (n > chunk) n = chunk;
    txChar->setValue((uint8_t*)(data + sent), n);
    txChar->notify();
    sent += n;
    // Small yield so the BLE stack flushes before the next chunk.
    delay(4);
  }
  return sent;
}
