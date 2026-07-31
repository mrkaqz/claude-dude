// LittleFS.h must come before hw.h's TFT_eSPI.h — see the comment on that
// include in character.cpp for why.
#include <LittleFS.h>
#include "hw.h"
#include <stdarg.h>
#include <esp_mac.h>   // esp_read_mac() / ESP_MAC_BT — was pulled in transitively via M5StickCPlus.h
#include "ble_bridge.h"
#include "data.h"
#include "buddy.h"

TFT_eSprite spr = TFT_eSprite(&tft);

// Advertise as "Claude-XXXX" (last two BT MAC bytes) so multiple sticks
// in one room are distinguishable in the desktop picker. Name persists in
// btName for reference.
static char btName[16] = "Claude";
static void startBt() {
  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_BT);
  snprintf(btName, sizeof(btName), "Claude-%02X%02X", mac[4], mac[5]);
  bleInit(btName);
}

#include "character.h"
#include "stats.h"

// Landscape 320x170, split pet-left / stats-right (PLAN.md Phase 2).
// Upstream hardcoded portrait 135x240 with magic numbers throughout —
// this is the direct replacement, not an adaptation of those numbers.
const int W = 320, H = 170;
const int PET_W      = 156;   // pet column: x 0..PET_W
const int DIVIDER_X  = 158;
const int STATS_X    = 166;   // stats column: x STATS_X..W
const int STATS_W    = W - STATS_X - 6;

// Colors used across multiple UI surfaces
const uint16_t HOT = 0xFA20;   // red-orange: warnings, impatience, deny

enum PersonaState { P_SLEEP, P_IDLE, P_BUSY, P_ATTENTION, P_CELEBRATE, P_DIZZY, P_HEART };
const char* stateNames[] = { "sleep", "idle", "busy", "attention", "celebrate", "dizzy", "heart" };

TamaState    tama;
PersonaState baseState   = P_SLEEP;
PersonaState activeState = P_SLEEP;
uint32_t     oneShotUntil = 0;
unsigned long t = 0;

bool    buddyMode = false;
bool    gifAvailable = false;
const uint8_t SPECIES_GIF = 0xFF;   // species NVS sentinel: use the installed GIF

char     lastPromptId[40] = "";
uint32_t promptArrivedMs = 0;

void triggerOneShot(uint8_t s, uint32_t durMs) {
  activeState = (PersonaState)s;
  oneShotUntil = millis() + durMs;
}

PersonaState derive(const TamaState& s) {
  if (!s.connected)            return P_IDLE;
  if (s.sessionsWaiting > 0)   return P_ATTENTION;
  if (s.recentlyCompleted)     return P_CELEBRATE;
  if (s.sessionsRunning >= 3)  return P_BUSY;
  return P_IDLE;   // connected, 0+ sessions, nothing urgent — hang out
}

// --- Stats column -----------------------------------------------------

static void drawStatsHeader() {
  const Palette& p = characterPalette();
  spr.setTextSize(1);
  spr.setTextColor(p.text, p.bg);
  spr.setCursor(STATS_X, 4);
  if (ownerName()[0]) spr.printf("%s's %s", ownerName(), petName());
  else spr.print(petName());

  uint16_t stateCol = (activeState == P_ATTENTION) ? HOT
                     : (activeState == P_SLEEP)     ? p.textDim
                     : p.body;
  spr.setTextColor(stateCol, p.bg);
  spr.setCursor(STATS_X, 16);
  spr.print(stateNames[activeState]);
}

// Prompt pending: tool + hint + how long it's been waiting. No approve/deny
// affordance — there's no button to act with; the prompt is actioned on
// the desktop, this is display-only (PLAN.md's button-relocation table).
static void drawApprovalPanel() {
  const Palette& p = characterPalette();
  const int TOP = 30;
  spr.fillRect(STATS_X, TOP, STATS_W, H - TOP, p.bg);

  spr.setTextSize(1);
  spr.setTextColor(p.textDim, p.bg);
  spr.setCursor(STATS_X, TOP);
  uint32_t waited = (millis() - promptArrivedMs) / 1000;
  if (waited >= 10) spr.setTextColor(HOT, p.bg);
  spr.printf("waiting %lus", (unsigned long)waited);

  int toolLen = strlen(tama.promptTool);
  spr.setTextColor(p.text, p.bg);
  spr.setTextSize(toolLen <= 12 ? 2 : 1);
  spr.setCursor(STATS_X, TOP + 12);
  spr.print(tama.promptTool);
  spr.setTextSize(1);

  spr.setTextColor(p.textDim, p.bg);
  int wcols = (STATS_W - 4) / 6;   // ~6px/char at text size 1
  if (wcols > 30) wcols = 30;
  int hlen = strlen(tama.promptHint);
  spr.setCursor(STATS_X, TOP + 34);
  spr.printf("%.*s", wcols, tama.promptHint);
  if (hlen > wcols) {
    spr.setCursor(STATS_X, TOP + 44);
    spr.printf("%.*s", wcols, tama.promptHint + wcols);
  }

  spr.setTextColor(p.body, p.bg);
  spr.setCursor(STATS_X, H - 14);
  spr.print("approve on desktop");
}

static void tinyHeart(int x, int y, bool filled, uint16_t col) {
  if (filled) {
    spr.fillCircle(x - 2, y, 2, col);
    spr.fillCircle(x + 2, y, 2, col);
    spr.fillTriangle(x - 4, y + 1, x + 4, y + 1, x, y + 5, col);
  } else {
    spr.drawCircle(x - 2, y, 2, col);
    spr.drawCircle(x + 2, y, 2, col);
    spr.drawLine(x - 4, y + 1, x, y + 5, col);
    spr.drawLine(x + 4, y + 1, x, y + 5, col);
  }
}

// Normal (no prompt pending) stats: connection message, mood/fed/energy
// (stats.h's token-driven tamagotchi mechanics — still fully functional
// without buttons or an IMU, see stats.h), level, token counts.
static void drawStatsPanel() {
  const Palette& p = characterPalette();
  const int TOP = 30;
  spr.fillRect(STATS_X, TOP, STATS_W, H - TOP, p.bg);
  int y = TOP;

  spr.setTextSize(1);
  spr.setTextColor(p.textDim, p.bg);
  spr.setCursor(STATS_X, y);
  spr.print(tama.msg[0] ? tama.msg : (tama.connected ? "connected" : "no claude connected"));
  y += 16;

  spr.setCursor(STATS_X, y); spr.print("mood");
  uint8_t mood = statsMoodTier();
  uint16_t moodCol = (mood >= 3) ? TFT_RED : (mood >= 2) ? HOT : p.textDim;
  for (int i = 0; i < 4; i++) tinyHeart(STATS_X + 48 + i * 16, y + 3, i < mood, moodCol);
  y += 18;

  spr.setCursor(STATS_X, y); spr.print("fed");
  uint8_t fed = statsFedProgress();
  for (int i = 0; i < 10; i++) {
    int px = STATS_X + 30 + i * 11;
    if (i < fed) spr.fillCircle(px, y + 3, 2, p.body);
    else spr.drawCircle(px, y + 3, 2, p.textDim);
  }
  y += 18;

  spr.setCursor(STATS_X, y); spr.print("energy");
  uint8_t en = statsEnergyTier();
  uint16_t enCol = (en >= 4) ? 0x07FF : (en >= 2) ? 0xFFE0 : HOT;
  for (int i = 0; i < 5; i++) {
    int px = STATS_X + 48 + i * 16;
    if (i < en) spr.fillRect(px, y - 2, 11, 7, enCol);
    else spr.drawRect(px, y - 2, 11, 7, p.textDim);
  }
  y += 20;

  spr.fillRoundRect(STATS_X, y - 2, 46, 15, 3, p.body);
  spr.setTextColor(p.bg, p.body);
  spr.setCursor(STATS_X + 5, y + 1);
  spr.printf("Lv %u", stats().level);
  y += 22;

  spr.setTextColor(p.textDim, p.bg);
  auto tokFmt = [&](const char* label, uint32_t v, int yPx) {
    spr.setCursor(STATS_X, yPx);
    if (v >= 1000000)   spr.printf("%s%lu.%luM", label, v / 1000000, (v / 100000) % 10);
    else if (v >= 1000) spr.printf("%s%lu.%luK", label, v / 1000, (v / 100) % 10);
    else                spr.printf("%s%lu", label, v);
  };
  tokFmt("tokens ", stats().tokens, y);
  y += 10;
  tokFmt("today  ", tama.tokensToday, y);
  y += 14;
  spr.setCursor(STATS_X, y);
  spr.printf("appr %u  deny %u", stats().approvals, stats().denials);
}

void drawPasskey() {
  const Palette& p = characterPalette();
  spr.fillSprite(p.bg);
  spr.setTextDatum(MC_DATUM);
  spr.setTextSize(1);
  spr.setTextColor(p.textDim, p.bg);
  spr.drawString("BLUETOOTH PAIRING", W / 2, 50);
  spr.drawString("enter on desktop:", W / 2, 110);
  spr.setTextSize(3);
  spr.setTextColor(p.text, p.bg);
  char b[8]; snprintf(b, sizeof(b), "%06lu", (unsigned long)blePasskey());
  spr.drawString(b, W / 2, 80);
  spr.setTextDatum(TL_DATUM);
  spr.setTextSize(1);
}

void setup() {
  Serial.begin(115200);
  hwInit();
  startBt();
  statsLoad();
  settingsLoad();
  petNameLoad();
  buddyInit();

  spr.createSprite(W, H);
  characterSetHomeArea(PET_W / 2, H / 2);
  characterInit(nullptr);   // scan /characters/ for whatever is installed
  gifAvailable = characterLoaded();
  // species NVS: 0..N-1 = ASCII species, 0xFF = use GIF (also the default,
  // so a fresh install lands on the GIF). With no GIF installed, 0xFF falls
  // through to buddyInit()'s clamped default.
  buddyMode = !(gifAvailable && speciesIdxLoad() == SPECIES_GIF);

  // Start with a full energy bar rather than the stale value
  // statsEnergyTier() would otherwise derive from a never-set nap clock —
  // see stats.h's statsOnWake()/statsEnergyTier() and NOTICE for context
  // on why the original face-down nap trigger doesn't apply here.
  statsOnWake();

  {
    const Palette& p = characterPalette();
    spr.fillSprite(p.bg);
    spr.setTextDatum(MC_DATUM);
    spr.setTextSize(2);
    if (ownerName()[0]) {
      char line[40];
      snprintf(line, sizeof(line), "%s's", ownerName());
      spr.setTextColor(p.text, p.bg);   spr.drawString(line, W / 2, H / 2 - 12);
      spr.setTextColor(p.body, p.bg);   spr.drawString(petName(), W / 2, H / 2 + 12);
    } else {
      // First boot, no owner pushed yet — say hi.
      spr.setTextColor(p.body, p.bg);   spr.drawString("Hello!", W / 2, H / 2 - 12);
      spr.setTextSize(1);
      spr.setTextColor(p.textDim, p.bg);
      spr.drawString("a buddy appears", W / 2, H / 2 + 12);
    }
    spr.setTextDatum(TL_DATUM); spr.setTextSize(1);
    spr.pushSprite(0, 0);
    delay(1800);
  }

  Serial.printf("buddy: %s\n", buddyMode ? "ASCII mode" : "GIF character loaded");
}

void loop() {
  t++;
  uint32_t now = millis();

  dataPoll(&tama);
  if (statsPollLevelUp()) triggerOneShot(P_CELEBRATE, 3000);
  baseState = derive(tama);
  if ((int32_t)(now - oneShotUntil) >= 0) activeState = baseState;

  // Prompt arrival: edge-detect a new id so "waited Ns" starts from the
  // right moment. No response is ever sent from the device — see the
  // button-relocation table in PLAN.md.
  if (strcmp(tama.promptId, lastPromptId) != 0) {
    strncpy(lastPromptId, tama.promptId, sizeof(lastPromptId) - 1);
    lastPromptId[sizeof(lastPromptId) - 1] = 0;
    if (tama.promptId[0]) promptArrivedMs = millis();
  }
  bool inPrompt = tama.promptId[0] != 0;

  // Backlight: dim during sleep, full otherwise — driven purely by
  // persona state, no interaction timer (there's no interaction to time).
  static bool wasSleepy = true;   // forces the first apply in setup()
  bool sleepy = (activeState == P_SLEEP);
  if (sleepy != wasSleepy) {
    hwSetBacklight(sleepy ? 15 : 100);
    wasSleepy = sleepy;
  }

  // Pet column
  if (buddyMode) {
    buddyTick(activeState);
  } else if (characterLoaded()) {
    characterSetState(activeState);
    characterTick();
  } else {
    const Palette& p = characterPalette();
    spr.fillRect(0, 0, PET_W, H, p.bg);
    spr.setTextColor(p.textDim, p.bg);
    spr.setTextSize(1);
    if (xferActive()) {
      uint32_t done = xferProgress(), total = xferTotal();
      spr.setCursor(8, 70);
      spr.print("installing");
      spr.setCursor(8, 82);
      spr.printf("%luK / %luK", done / 1024, total / 1024);
      int barW = PET_W - 16;
      spr.drawRect(8, 96, barW, 8, p.textDim);
      if (total > 0) {
        int fill = (int)((uint64_t)barW * done / total);
        if (fill > 1) spr.fillRect(9, 97, fill - 1, 6, p.body);
      }
    } else {
      spr.setCursor(8, 80);
      spr.print("no character");
    }
  }

  // Stats column
  drawStatsHeader();
  if (inPrompt) drawApprovalPanel();
  else drawStatsPanel();

  spr.drawFastVLine(DIVIDER_X, 0, H, characterPalette().textDim);

  // Passkey pairing takes over the whole screen — needs to stay legible
  // and unobstructed until entered on the desktop.
  if (blePasskey()) drawPasskey();

  spr.pushSprite(0, 0);

  delay(16);
}
