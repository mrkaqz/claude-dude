#include "ui.h"
#include "hw.h"
#include "appstate.h"
#include "usage.h"
#include <stdio.h>
#include <string.h>

static const uint16_t BG    = TFT_BLACK;
static const uint16_t FG    = TFT_WHITE;
static const uint16_t DIM   = 0x8410;   // mid-gray
static const uint16_t BAR_BG = 0x2104;  // matches buddy's "PANEL" overlay color
static const uint16_t ACCENT = 0x07FF;  // cyan
static const uint16_t WARN   = 0xFA20;  // red-orange — matches buddy's "HOT"

void uiFormatDuration(uint32_t seconds, char* buf, size_t bufLen) {
  if (seconds >= 86400) {
    snprintf(buf, bufLen, "%lud%02lu", (unsigned long)(seconds / 86400), (unsigned long)((seconds % 86400) / 3600));
  } else if (seconds >= 3600) {
    snprintf(buf, bufLen, "%luh%02lu", (unsigned long)(seconds / 3600), (unsigned long)((seconds % 3600) / 60));
  } else {
    snprintf(buf, bufLen, "%lum%02lu", (unsigned long)(seconds / 60), (unsigned long)(seconds % 60));
  }
}

static void drawBar(int x, int y, int w, int h, uint8_t pct, uint16_t color) {
  if (pct > 100) pct = 100;
  tft.drawRect(x, y, w, h, DIM);
  int fillW = ((w - 2) * pct) / 100;
  tft.fillRect(x + 1, y + 1, w - 2, h - 2, BAR_BG);
  if (fillW > 0) tft.fillRect(x + 1, y + 1, fillW, h - 2, color);
}

// Full redraw every call — the idle screen only refreshes at ~1Hz (driven
// from main.cpp's loop), so flicker isn't a concern the way it is for the
// higher-frequency active screen.
void uiRenderIdle() {
  const UsageSnapshot& u = usageCurrent();
  bool offline = !appstateDesktopConnected();
  bool stale   = usageStale();

  tft.fillScreen(BG);
  tft.setTextDatum(TL_DATUM);

  char pctBuf[8], resetBuf[12];

  // Session row
  snprintf(pctBuf, sizeof(pctBuf), "%d%%", u.sessionPct);
  tft.setTextColor(FG, BG);
  tft.drawString("session", 8, 12, 2);
  tft.drawString(pctBuf, 70, 12, 2);
  drawBar(110, 10, 140, 16, u.sessionPct, u.sessionPct >= 90 ? WARN : ACCENT);
  uiFormatDuration(usageSessionResetSecondsNow(), resetBuf, sizeof(resetBuf));
  tft.drawString(resetBuf, 258, 12, 2);

  // Weekly row
  snprintf(pctBuf, sizeof(pctBuf), "%d%%", u.weekPct);
  tft.drawString("weekly", 8, 40, 2);
  tft.drawString(pctBuf, 70, 40, 2);
  drawBar(110, 38, 140, 16, u.weekPct, u.weekPct >= 90 ? WARN : ACCENT);
  uiFormatDuration(usageWeekResetSecondsNow(), resetBuf, sizeof(resetBuf));
  tft.drawString(resetBuf, 258, 40, 2);

  // Bottom row: idle time + tokens today
  char idleBuf[16];
  uint32_t idleS = appstateIdleMs() / 1000;
  if (idleS < 60) snprintf(idleBuf, sizeof(idleBuf), "idle <1m");
  else snprintf(idleBuf, sizeof(idleBuf), "idle %lum", (unsigned long)(idleS / 60));
  tft.setTextColor(DIM, BG);
  tft.drawString(idleBuf, 8, 148, 2);

  char tokBuf[24];
  const Heartbeat& hb = appstateHeartbeat();
  if (hb.tokensToday >= 1000) {
    snprintf(tokBuf, sizeof(tokBuf), "%.1fk tok today", hb.tokensToday / 1000.0f);
  } else {
    snprintf(tokBuf, sizeof(tokBuf), "%lu tok today", (unsigned long)hb.tokensToday);
  }
  tft.setTextDatum(TR_DATUM);
  tft.drawString(tokBuf, 312, 148, 2);
  tft.setTextDatum(TL_DATUM);

  // Status marker — offline takes priority over a stale usage poll as the
  // more actionable thing to tell the user.
  if (offline) {
    tft.setTextColor(WARN, BG);
    tft.drawString("desktop offline", 8, 126, 2);
  } else if (stale) {
    tft.setTextColor(DIM, BG);
    char staleBuf[64];
    snprintf(staleBuf, sizeof(staleBuf), "usage stale%s%s", u.err[0] ? ": " : "", u.err);
    tft.drawString(staleBuf, 8, 126, 2);
  }
}
