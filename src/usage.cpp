#include "usage.h"
#include <Arduino.h>
#include <string.h>

static UsageSnapshot snap;
static const uint32_t STALE_AFTER_MS = 90000;   // 1.5x the daemon's ~60s poll

void usageApply(JsonDocument& doc) {
  bool ok = doc["ok"] | true;
  snap.lastPollOk = ok;

  if (ok) {
    snap.sessionPct    = doc["session_pct"]    | snap.sessionPct;
    snap.sessionResetS = doc["session_reset_s"] | snap.sessionResetS;
    snap.weekPct        = doc["week_pct"]        | snap.weekPct;
    snap.weekResetS      = doc["week_reset_s"]     | snap.weekResetS;
    const char* plan = doc["plan"];
    if (plan) { strncpy(snap.plan, plan, sizeof(snap.plan) - 1); snap.plan[sizeof(snap.plan) - 1] = 0; }
    snap.err[0] = 0;
    snap.lastGoodMs = millis();
    snap.everReceived = true;
  } else {
    const char* err = doc["err"];
    strncpy(snap.err, err ? err : "unknown error", sizeof(snap.err) - 1);
    snap.err[sizeof(snap.err) - 1] = 0;
    // pct/reset values intentionally untouched — last good numbers keep
    // showing, just marked stale via usageStale().
  }
}

const UsageSnapshot& usageCurrent() { return snap; }

static uint32_t countdownNow(uint32_t resetSAtReceipt) {
  if (!snap.everReceived) return 0;
  uint32_t elapsedS = (millis() - snap.lastGoodMs) / 1000;
  if (elapsedS >= resetSAtReceipt) return 0;
  return resetSAtReceipt - elapsedS;
}

uint32_t usageSessionResetSecondsNow() { return countdownNow(snap.sessionResetS); }
uint32_t usageWeekResetSecondsNow()    { return countdownNow(snap.weekResetS); }

bool usageStale() {
  if (!snap.everReceived) return true;
  if (!snap.lastPollOk) return true;
  return (millis() - snap.lastGoodMs) > STALE_AFTER_MS;
}
