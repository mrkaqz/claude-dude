#pragma once
#include <stdint.h>

// Screen state machine — see docs/PROTOCOL.md. Driven entirely by the
// Desktop heartbeat and the daemon's usage snapshot; there is no button
// input, so every state is re-derived from elapsed time on every tick
// rather than latched. That's what makes the "no state can wedge" watchdog
// requirement in PLAN.md automatic: if the heartbeat goes stale, the state
// falls out of ATTENTION/ACTIVE on the next tick with no separate timer to
// get stuck.
enum AppState : uint8_t { APP_ATTENTION, APP_ACTIVE, APP_IDLE, APP_NO_DESKTOP };

struct Heartbeat {
  uint8_t  total = 0, running = 0, waiting = 0;
  char     msg[24] = "";
  uint32_t tokensToday = 0;
  char     promptId[40]   = "";
  char     promptTool[20] = "";
  char     promptHint[44] = "";
  uint32_t lastSeenMs = 0;   // millis() of last heartbeat; 0 = never seen
};

void appstateInit();

// Feeds one line of NUS RX, from either BLE central, through the
// dispatcher: cmd:"usage" -> usage.cpp, cmd:"name"/"owner"/"unpair"/
// "status"/char_* -> xfer.h, {"time":[epoch,tzOffset]} -> settimeofday(),
// anything else that parses as JSON -> heartbeat fields.
void appstateFeedLine(const char* line);

// Re-derives the current state from elapsed time. Call every loop
// iteration, cheap.
void appstateTick();

AppState appstateCurrent();
const Heartbeat& appstateHeartbeat();
bool appstateDesktopConnected();

// Milliseconds since Claude was last actively running or needed attention.
// Meaningful only while appstateCurrent() == APP_IDLE; drives the idle
// screen's "idle Nm" line.
uint32_t appstateIdleMs();
