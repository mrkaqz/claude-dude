#include "hw.h"
#include <Arduino.h>
#include <esp_sleep.h>

TFT_eSPI tft = TFT_eSPI();

static const uint8_t  PIN_POWER_ON = 15;
static const uint8_t  PIN_BAT_VOLT = 4;
static const uint8_t  PIN_BL       = 38;
static const uint32_t BL_FREQ_HZ   = 5000;
static const uint8_t  BL_RES_BITS  = 8;

void hwInit() {
  // #1 cause of a black screen on this board: the panel and backlight are
  // both dead until this rail is switched on.
  pinMode(PIN_POWER_ON, OUTPUT);
  digitalWrite(PIN_POWER_ON, HIGH);
  delay(10);

  tft.init();
  tft.setRotation(1);   // 320x170 landscape, USB-side conventions per PLAN.md
  tft.fillScreen(TFT_BLACK);

  // Pin-based LEDC API (this core's ledcAttach/ledcWrite take a pin, not a
  // channel — the older channel-based ledcSetup/ledcAttachPin is gone).
  ledcAttach(PIN_BL, BL_FREQ_HZ, BL_RES_BITS);
  hwSetBacklight(100);
}

void hwSetBacklight(uint8_t pct) {
  if (pct > 100) pct = 100;
  uint32_t duty = ((uint32_t)pct * ((1 << BL_RES_BITS) - 1)) / 100;
  ledcWrite(PIN_BL, duty);
}

float hwBatteryVoltage() {
  // 2:1 divider on GPIO4 — see PLAN.md hardware table.
  return analogReadMilliVolts(PIN_BAT_VOLT) * 2.0f / 1000.0f;
}

void hwPowerOff() {
  esp_deep_sleep_start();
}
