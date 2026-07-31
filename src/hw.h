#pragma once
#include <TFT_eSPI.h>
#include <stdint.h>

// T-Display-S3 board shim. buddy has no HAL — ~50 M5.* call sites
// scattered through its firmware, all originally targeting the M5StickC
// Plus. This is the board-specific replacement surface for the
// T-Display-S3: everything that talked to an M5StickC Plus now talks to
// this instead.

extern TFT_eSPI tft;

// Pin power domain on, init the ST7789 at 320x170 landscape (rotation 1),
// and bring the backlight PWM channel up. Call once from setup(), before
// any drawing.
void hwInit();

// 0..100. Backed by ledcWrite on GPIO38 (TFT_BL).
void hwSetBacklight(uint8_t pct);

// Battery voltage in volts, via the 2:1 divider on GPIO4. No PMU on this
// board, so this is the only battery signal available — no current, no
// USB-bus voltage, no temperature.
float hwBatteryVoltage();

// Same shape as the old M5.Axp.PowerOff() call site.
void hwPowerOff();
