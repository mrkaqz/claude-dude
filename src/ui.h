#pragma once
#include <stdint.h>
#include <stddef.h>

// ACTIVE and ATTENTION share this layout (header line + counts row);
// appstate's current state decides which variant is drawn. Diffs against
// its own last draw internally, so call uiActiveReset() first whenever the
// idle screen may have overwritten the display since the last call.
void uiRenderActive();
void uiActiveReset();

// IDLE and NO_DESKTOP share this layout (usage bars); appstate's current
// state decides whether the "desktop offline" marker shows.
void uiRenderIdle();

// Full-screen passkey display for LE Secure Connections passkey-entry
// pairing — this device is DisplayOnly, the central is KeyboardOnly, so
// the user reads this code and types it into Claude Desktop. Takes
// priority over every other screen; call uiActiveReset() (or just let the
// next active/idle draw notice the screen changed) once it's done.
void uiRenderPairing(uint32_t passkey);

// Formats a seconds count as "2h00" / "5d00" / "12m00", matching
// PLAN.md's reset-countdown mockups. buf must be at least 12 bytes.
void uiFormatDuration(uint32_t seconds, char* buf, size_t bufLen);
