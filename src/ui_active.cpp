#include "ui.h"
#include "hw.h"
#include "appstate.h"
#include <string.h>
#include <stdio.h>

static const uint16_t BG     = TFT_BLACK;
static const uint16_t FG     = TFT_WHITE;
static const uint16_t DIM    = 0x8410;   // mid-gray
static const uint16_t ATTN   = 0xFA20;   // red-orange — matches buddy's "HOT"

// Redraw only what changed, to avoid a full-screen flicker every tick —
// there's no double buffering on this bus.
static AppState lastState        = APP_NO_DESKTOP;   // forces a first full draw
static char     lastHeader[80]   = "";
static bool     lastAttn         = false;
static uint8_t  lastTotal = 255, lastRunning = 255, lastWaiting = 255;

void uiActiveReset() { lastState = APP_NO_DESKTOP; }

void uiRenderActive() {
  const Heartbeat& hb = appstateHeartbeat();
  AppState st = appstateCurrent();
  bool attention = (st == APP_ATTENTION);

  bool firstDraw = (lastState != APP_ACTIVE && lastState != APP_ATTENTION);
  if (firstDraw) {
    tft.fillScreen(BG);
    tft.drawFastHLine(8, 36, 304, DIM);
  }

  char header[80];
  if (attention && hb.promptTool[0]) {
    snprintf(header, sizeof(header), "%s: %s", hb.promptTool, hb.promptHint);
  } else {
    snprintf(header, sizeof(header), "%s", hb.msg[0] ? hb.msg : "working...");
  }

  if (firstDraw || strcmp(header, lastHeader) != 0 || attention != lastAttn) {
    tft.fillRect(8, 8, 304, 22, BG);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(attention ? ATTN : FG, BG);
    tft.drawString(header, 8, 12, 2);
    if (attention) {
      tft.setTextColor(ATTN, BG);
      tft.setTextDatum(TR_DATUM);
      tft.drawString("[!]", 312, 12, 2);
      tft.setTextDatum(TL_DATUM);
    }
    strncpy(lastHeader, header, sizeof(lastHeader) - 1);
    lastHeader[sizeof(lastHeader) - 1] = 0;
    lastAttn = attention;
  }

  if (firstDraw || hb.total != lastTotal || hb.running != lastRunning || hb.waiting != lastWaiting) {
    char counts[48];
    snprintf(counts, sizeof(counts), "%d total    %d running    %d waiting",
             hb.total, hb.running, hb.waiting);
    tft.fillRect(8, 46, 304, 22, BG);
    tft.setTextColor(FG, BG);
    tft.drawString(counts, 8, 50, 2);
    lastTotal = hb.total; lastRunning = hb.running; lastWaiting = hb.waiting;
  }

  lastState = st;
}

void uiRenderPairing(uint32_t passkey) {
  tft.fillScreen(BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(FG, BG);
  tft.drawString("Enter this code on Claude Desktop", 160, 60, 2);

  char code[16];
  // Space the digits out — a plain "%06lu" reads as one dense blob at
  // large font sizes.
  snprintf(code, sizeof(code), "%06lu", (unsigned long)passkey);
  char spaced[24]; int j = 0;
  for (int i = 0; code[i]; i++) { spaced[j++] = code[i]; if (i < 5) spaced[j++] = ' '; }
  spaced[j] = 0;

  tft.setTextColor(TFT_CYAN, BG);
  tft.drawString(spaced, 160, 100, 4);
  tft.setTextDatum(TL_DATUM);
}
