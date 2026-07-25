#pragma once
#include <ArduinoJson.h>
#include <stdint.h>

// Parses the daemon's cmd:"usage" line (docs/PROTOCOL.md) and holds the
// last good snapshot with a staleness timestamp, counting down the reset
// timers locally between the daemon's ~60s polls.
struct UsageSnapshot {
  uint8_t  sessionPct    = 0;
  uint32_t sessionResetS = 0;   // seconds remaining as of lastGoodMs
  uint8_t  weekPct       = 0;
  uint32_t weekResetS    = 0;   // seconds remaining as of lastGoodMs
  char     plan[16]      = "";
  bool     everReceived  = false;   // true once any ok:true snapshot arrived
  uint32_t lastGoodMs     = 0;      // millis() at the last ok:true apply
  bool     lastPollOk     = true;   // reflects the most recent poll's "ok"
  char     err[48]        = "";     // reason from the most recent ok:false poll
};

void usageApply(JsonDocument& doc);
const UsageSnapshot& usageCurrent();

// Seconds remaining right now, ticking down in real time from the last
// good snapshot — never frozen, even while polls are failing.
uint32_t usageSessionResetSecondsNow();
uint32_t usageWeekResetSecondsNow();

// True when the daemon hasn't delivered a good snapshot recently (either
// the last poll reported ok:false, or it's been too long since the last
// good one — 1.5x the daemon's ~60s poll interval).
bool usageStale();
