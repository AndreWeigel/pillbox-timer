// Pillbox Timer — Step 3: deep sleep + wake on pin-change interrupt.
// Chip sleeps in power-down mode. Bringing the magnet near (FALLING: pin goes LOW)
// wakes the chip, blinks the LED once to confirm, then returns to sleep.
//
// NOTE: final firmware uses RISING (lid opens = magnet leaves = pin goes HIGH).
// FALLING is used here so testing is easy: bring magnet close → see LED blink.
//
// Wiring: same as Step 2 — no changes needed.

#include <avr/sleep.h>

#define LED_PIN  0
#define REED_PIN 2

volatile bool woke = false;

void reedISR() {
  woke = true;
}

void blinkOnce() {
  digitalWrite(LED_PIN, HIGH);
  delay(150);
  digitalWrite(LED_PIN, LOW);
}

void goToSleep() {
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  sleep_enable();
  sleep_cpu();      // halts here until interrupt fires
  sleep_disable();
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  pinMode(REED_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(REED_PIN), reedISR, FALLING);
  // Two quick blinks confirm the chip booted and is about to sleep
  blinkOnce();
  delay(150);
  blinkOnce();
}

void loop() {
  goToSleep();
  if (woke) {
    woke = false;
    delay(20);        // debounce: ignore any bounce after the first edge
    blinkOnce();
  }
}
