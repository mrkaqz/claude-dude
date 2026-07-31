#pragma once
#include <stdint.h>

struct Palette {
  uint16_t body, bg, text, textDim, ink;
};

// Call after hwInit() and spr.createSprite(). Mounts LittleFS, reads
// /characters/<name>/manifest.json, parses colors, caches GIF paths.
bool characterInit(const char* name);
bool characterLoaded();

// Sets where the non-peek ("home") GIF centers within the sprite. Upstream
// always centered in the full sprite width because portrait had only one
// screen region for the pet; the landscape pet column is a sub-region of
// a wider sprite, so this needs to be explicit. Call once from setup().
void characterSetHomeArea(int centerX, int centerY);

// 0..6: sleep, idle, busy, attention, celebrate, dizzy, heart.
// Closes current GIF, opens the one for this state. No-op if same state.
void characterSetState(uint8_t state);

// Advances timing; if it's time for the next frame, decodes it into the
// sprite. Call every loop iteration. Does nothing if not loaded.
void characterTick();
void characterInvalidate();
void characterClose();   // close GIF + clear loaded flag; FS stays mounted   // full clear + reopen current — call when an overlay closes

// Peek mode renders the GIF at half scale, centered in the info-panel
// header strip; off renders full-size centered in the upper home area.
// Adaptive to actual canvas height — no padding required in source art.
void characterSetPeek(bool peek);
class TFT_eSPI;
void characterRenderTo(TFT_eSPI* tgt, int cx, int cy);

const Palette& characterPalette();
