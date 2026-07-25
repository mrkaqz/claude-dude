#include <Arduino.h>
#include <esp_mac.h>
#include <LittleFS.h>
#include "hw.h"
#include "ble_bridge.h"
#include "appstate.h"
#include "usage.h"
#include "ui.h"
#include "stats.h"

// character.cpp's AnimatedGIF rendering path targets this sprite. Not
// wired into either screen yet — that's Phase 4, once there's real art —
// but defining it here keeps character.cpp/character.h compiling and
// linking cleanly against the new board shim in the meantime.
TFT_eSprite spr = TFT_eSprite(&tft);

static char btName[16] = "Claude";

static void startBle() {
  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_BT);
  snprintf(btName, sizeof(btName), "Claude-%02X%02X", mac[4], mac[5]);
  bleInit(btName);
}

// Line-buffers one BLE central's byte stream into complete JSON lines and
// feeds each through appstate's dispatcher. RX is shared by both the
// Desktop connection and the daemon connection — NUS doesn't distinguish
// which wrote a given byte, so both are parsed through the same path (see
// docs/PROTOCOL.md).
template<size_t N>
struct LineBuf {
  char buf[N];
  uint16_t len = 0;
  void feedByte(char c) {
    if (c == '\n' || c == '\r') {
      if (len > 0) { buf[len] = 0; if (buf[0] == '{') appstateFeedLine(buf); len = 0; }
    } else if (len < N - 1) {
      buf[len++] = c;
    }
  }
};
static LineBuf<1024> bleLine;

static bool     lastWasActiveFamily = false;
static uint32_t lastIdleRenderMs    = 0;
static uint32_t lastDrawnPasskey    = 0xFFFFFFFF;   // sentinel: never matches a real 6-digit code

void setup() {
  Serial.begin(115200);
  hwInit();
  LittleFS.begin(true);   // format on first boot; art (Phase 4) and xfer.h's status ack both need it mounted
  petNameLoad();
  appstateInit();
  startBle();
}

void loop() {
  while (bleAvailable()) {
    int c = bleRead();
    if (c < 0) break;
    bleLine.feedByte((char)c);
  }

  appstateTick();

  // Passkey pairing takes priority over everything else — the link isn't
  // usable yet, and the code needs to stay legible and unobstructed until
  // it's entered on the Desktop side.
  uint32_t passkey = blePasskey();
  if (passkey != 0) {
    if (passkey != lastDrawnPasskey) {
      uiRenderPairing(passkey);
      lastDrawnPasskey = passkey;
    }
    return;
  }
  lastDrawnPasskey = 0xFFFFFFFF;

  AppState st = appstateCurrent();
  bool isActiveFamily = (st == APP_ACTIVE || st == APP_ATTENTION);

  if (isActiveFamily) {
    if (!lastWasActiveFamily) uiActiveReset();
    uiRenderActive();
  } else {
    // Idle/no-desktop redraws at ~1Hz — enough to keep the reset
    // countdowns visibly ticking without repainting on every loop().
    uint32_t now = millis();
    if (lastWasActiveFamily || now - lastIdleRenderMs >= 1000) {
      uiRenderIdle();
      lastIdleRenderMs = now;
    }
  }
  lastWasActiveFamily = isActiveFamily;
}
