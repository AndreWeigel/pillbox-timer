// Pillbox Timer — application layer.
// Tracks time since the lid was last opened (magnet leaves the reed switch) and
// decides WHAT to show; the display module renders it and the epd module drives
// the panel. Two modes, selected by DEFAULT_MODE:
//   MODE_DOSE_STATUS   DONE / TAKE / OVERDUE headline + subtitle (interval-based)
//   MODE_LAST_OPENED   "JUST NOW" / "7H AGO"                     (passive log)
//
// Power: the chip sleeps in POWER-DOWN (~1µA). The RTC PIT wakes it every 32 s
// to keep counting (it still ticks in power-down); the reed switch is on a fully
// asynchronous pin (PA6 = pin 2) so its edge also wakes from power-down. The
// screen is only redrawn when the shown text actually changes.
//
// Modules: display.* (font, layout, refresh policy), epd.* (SSD1681 driver).
// Breadboard LED on pin 0; reed switch pin 2 -> GND.

#include <avr/sleep.h>
#include <avr/interrupt.h>
#include <stdlib.h>
#include <string.h>
#include "display.h"

#define LED_PIN  0
#define REED_PIN 2

// --- display mode -----------------------------------------------------------
#define MODE_LAST_OPENED 0
#define MODE_DOSE_STATUS 1
#define DEFAULT_MODE     MODE_DOSE_STATUS   // change to switch modes (button later)

uint8_t displayMode = DEFAULT_MODE;         // runtime var so a button can flip it

// Set to 1 for fast bench testing (states change in tens of seconds), 0 for the
// real medication schedule. Remember time advances in PIT_TICK_SEC (32 s) steps.
#define TEST_MODE 1

// Dose schedule (dose-status mode only), measured from the last lid-open.
#if TEST_MODE
  #define DOSE_DONE_SEC     32UL    // ~1 tick: "DONE"
  #define DOSE_OVERDUE_SEC  96UL    // ~3 ticks: "OVERDUE"
#else
  #define DOSE_DONE_SEC     72000UL // 20 h: show "DONE" within this window
  #define DOSE_OVERDUE_SEC 100800UL // 28 h: show "OVERDUE" past this (8 h late)
#endif

// ---------------------------------------------------------------------------
// Timekeeping. The PIT wakes the chip every 32 s — the longest single PIT
// period the hardware allows (32768 cycles of the 1.024 kHz oscillator), which
// means ~2700 wakes/day instead of 86400 at 1 s. 32 s resolution is plenty for
// dose state and minute display, and lid-open response is unaffected (the reed
// switch is a separate, instant wake source).
// ---------------------------------------------------------------------------

#define PIT_TICK_SEC 32

volatile uint32_t elapsedSec = 0;
volatile bool tickFlag = false;
volatile bool lidFlag  = false;

ISR(RTC_PIT_vect) {
  RTC.PITINTFLAGS = RTC_PI_bm;   // clear the interrupt
  elapsedSec += PIT_TICK_SEC;
  tickFlag = true;
}

void reedISR() { lidFlag = true; }   // lid opened (magnet left the switch)

static void rtcPitInit() {
  RTC.CLKSEL = RTC_CLKSEL_INT1K_gc;                        // 1.024 kHz internal
  RTC.PITINTCTRL = RTC_PI_bm;                              // enable PIT interrupt
  while (RTC.PITSTATUS & RTC_CTRLBUSY_bm) ;                // wait for sync
  RTC.PITCTRLA = RTC_PERIOD_CYC32768_gc | RTC_PITEN_bm;    // 32768/1024 Hz = 32 s
}

// Append a compact duration ("45M", "5H", "3D") at the start of buf.
static void fmtDur(uint32_t s, char* buf) {
  unsigned long v;
  char unit;
  if (s < 3600UL)       { v = s / 60UL;    unit = 'M'; }
  else if (s < 86400UL) { v = s / 3600UL;  unit = 'H'; }
  else                  { v = s / 86400UL; unit = 'D'; }
  ultoa(v, buf, 10);
  char* p = buf + strlen(buf);
  *p++ = unit;
  *p = '\0';
}

// Build the headline (big) and subtitle (small) for the active mode.
static void formatStatus(uint32_t sec, char* big, char* sub) {
  if (displayMode == MODE_DOSE_STATUS) {
    if (sec < DOSE_DONE_SEC) {              // still within the done window
      strcpy(big, "DONE");
      strcpy(sub, "PILLS TAKEN");
    } else if (sec < DOSE_OVERDUE_SEC) {    // dose is due
      strcpy(big, "TAKE");
      strcpy(sub, "PILLS DUE NOW");
    } else {                                // well past due
      strcpy(big, "OVERDUE");
      fmtDur(sec - DOSE_DONE_SEC, sub);     // how long it has been due
      strcat(sub, " LATE");                 // e.g. "9H LATE"
    }
  } else {  // MODE_LAST_OPENED
    sub[0] = '\0';                          // no subtitle
    if (sec < 60) { strcpy(big, "JUST NOW"); return; }
    fmtDur(sec, big);
    strcat(big, " AGO");                    // "7H AGO"
  }
}

// ---------------------------------------------------------------------------

static void goToSleep() {
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  sleep_enable();
  sleep_cpu();          // wakes on PIT (every 32 s) or reed edge (lid open)
  sleep_disable();
}

void setup() {
  pinMode(LED_PIN,  OUTPUT);
  pinMode(REED_PIN, INPUT_PULLUP);
  displaySetup();

  // Lid opens when the magnet LEAVES the reed switch -> RISING edge.
  attachInterrupt(digitalPinToInterrupt(REED_PIN), reedISR, RISING);
  rtcPitInit();

  // Two slow blinks = alive
  for (uint8_t i = 0; i < 2; i++) {
    digitalWrite(LED_PIN, HIGH); delay(500);
    digitalWrite(LED_PIN, LOW);  delay(500);
  }

  elapsedSec = 0;
  char big[16], sub[16];
  formatStatus(0, big, sub);
  displayShow(big, sub);
}

void loop() {
  goToSleep();

  bool opened;
  uint32_t sec;
  cli();
  opened = lidFlag; lidFlag = false;
  tickFlag = false;
  if (opened) elapsedSec = 0;
  sec = elapsedSec;
  sei();

  if (opened) {                       // dev feedback for a lid-open event
    digitalWrite(LED_PIN, HIGH); delay(100); digitalWrite(LED_PIN, LOW);
  }

  char big[16], sub[16];
  formatStatus(sec, big, sub);
  displayShow(big, sub);              // no-ops if unchanged
}
