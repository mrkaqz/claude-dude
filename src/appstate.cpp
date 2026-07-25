#include "appstate.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <sys/time.h>
#include "xfer.h"     // xferCommand() — name/owner/unpair/status/char_* + hw.h/stats.h
#include "usage.h"     // usageApply() — cmd:"usage" from the daemon

static Heartbeat hb;
static AppState  sticky      = APP_IDLE;
static uint32_t  calmSinceMs = 0;   // 0 = not currently calm
static uint32_t  lastActiveMs = 0;

static const uint32_t DESKTOP_TIMEOUT_MS = 30000;
static const uint32_t CALM_TO_IDLE_MS    = 30000;

void appstateInit() {
  hb = Heartbeat();
  sticky = APP_IDLE;
  calmSinceMs = 0;
  lastActiveMs = millis();
}

static void applyHeartbeatFields(JsonDocument& doc) {
  hb.total   = doc["total"]   | hb.total;
  hb.running = doc["running"] | hb.running;
  hb.waiting = doc["waiting"] | hb.waiting;
  hb.tokensToday = doc["tokens_today"] | hb.tokensToday;

  const char* m = doc["msg"];
  if (m) { strncpy(hb.msg, m, sizeof(hb.msg) - 1); hb.msg[sizeof(hb.msg) - 1] = 0; }

  JsonObject pr = doc["prompt"];
  if (!pr.isNull()) {
    const char* pid = pr["id"];
    const char* pt  = pr["tool"];
    const char* ph  = pr["hint"];
    strncpy(hb.promptId,   pid ? pid : "", sizeof(hb.promptId) - 1);   hb.promptId[sizeof(hb.promptId) - 1] = 0;
    strncpy(hb.promptTool, pt  ? pt  : "", sizeof(hb.promptTool) - 1); hb.promptTool[sizeof(hb.promptTool) - 1] = 0;
    strncpy(hb.promptHint, ph  ? ph  : "", sizeof(hb.promptHint) - 1); hb.promptHint[sizeof(hb.promptHint) - 1] = 0;
  } else {
    hb.promptId[0] = 0; hb.promptTool[0] = 0; hb.promptHint[0] = 0;
  }

  hb.lastSeenMs = millis();
}

void appstateFeedLine(const char* line) {
  JsonDocument doc;
  if (deserializeJson(doc, line)) return;

  const char* cmd = doc["cmd"];
  if (cmd && strcmp(cmd, "usage") == 0) { usageApply(doc); return; }

  // xferCommand() handles every other "cmd" value (name/owner/unpair/
  // status/char_begin/file/chunk/file_end/char_end) and returns false when
  // there's no "cmd" key at all, which is exactly the heartbeat/time-sync
  // case below.
  if (xferCommand(doc)) return;

  JsonArray t = doc["time"];
  if (!t.isNull() && t.size() == 2) {
    time_t epoch = (time_t)t[0].as<uint32_t>() + (int32_t)t[1].as<int32_t>();
    struct timeval tv = { epoch, 0 };
    settimeofday(&tv, nullptr);
    return;
  }

  applyHeartbeatFields(doc);
}

void appstateTick() {
  uint32_t now = millis();
  bool connected = hb.lastSeenMs != 0 && (now - hb.lastSeenMs <= DESKTOP_TIMEOUT_MS);

  if (!connected) {
    sticky = APP_NO_DESKTOP;
    calmSinceMs = 0;
    return;
  }

  bool attention = hb.promptId[0] != 0 || hb.waiting > 0;
  bool active    = hb.running > 0;

  if (attention) { sticky = APP_ATTENTION; calmSinceMs = 0; lastActiveMs = now; return; }
  if (active)    { sticky = APP_ACTIVE;    calmSinceMs = 0; lastActiveMs = now; return; }

  // Calm: neither attention- nor activity-worthy right now. Hold whatever
  // state was showing for a grace window so a brief gap between two tasks
  // doesn't flicker the screen to the usage layout and back.
  if (calmSinceMs == 0) calmSinceMs = now;
  if (now - calmSinceMs > CALM_TO_IDLE_MS) sticky = APP_IDLE;
}

AppState appstateCurrent() { return sticky; }
const Heartbeat& appstateHeartbeat() { return hb; }
bool appstateDesktopConnected() {
  return hb.lastSeenMs != 0 && (millis() - hb.lastSeenMs <= DESKTOP_TIMEOUT_MS);
}
uint32_t appstateIdleMs() { return millis() - lastActiveMs; }
