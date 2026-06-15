#pragma once
#include "style.h"

// ---------------------------------------------------------------------------
// Hardware pins
// ---------------------------------------------------------------------------
#define LED_PIN  0
#define REED_PIN 2

#define EPD_DIN_PIN  1
#define EPD_CLK_PIN  3
#define EPD_CS_PIN   4
#define EPD_DC_PIN   5
#define EPD_RST_PIN  6
#define EPD_BUSY_PIN 7

// ---------------------------------------------------------------------------
// Display
// ---------------------------------------------------------------------------
#define DISPLAY_STYLE    STYLE_PLAIN   // STYLE_PLAIN / STYLE_FUN / STYLE_PIXEL
#define FULL_REFRESH_EVERY 20         // 1 full (flashing) refresh per N changes

// ---------------------------------------------------------------------------
// App mode
// ---------------------------------------------------------------------------
#define MODE_LAST_OPENED 0
#define MODE_DOSE_STATUS 1
#define DEFAULT_MODE MODE_DOSE_STATUS // change to MODE_LAST_OPENED to switch

// ---------------------------------------------------------------------------
// Bench-testing shortcut: shrinks dose windows to seconds so states cycle fast.
// Set to 0 for the real medication schedule.
// ---------------------------------------------------------------------------
#define TEST_MODE 0

#if TEST_MODE
  #define DOSE_DONE_SEC     32UL   // ~1 PIT tick: "DONE" window
  #define DOSE_OVERDUE_SEC  96UL   // ~3 PIT ticks: "OVERDUE" threshold
#else
  #define DOSE_DONE_SEC    72000UL  // 20 h
  #define DOSE_OVERDUE_SEC 100800UL // 28 h (8 h late)
#endif

#define PIT_TICK_SEC 32             // RTC PIT period in seconds (hardware max)
