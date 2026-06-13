// Pillbox Timer — Step 2: validate reed switch wiring and input.
// LED turns on while the magnet is close (switch closed), off when removed.
//
// Wiring:
//   Reed switch: one leg → REED_PIN, other leg → GND  (no external resistor needed)
//   LED: LED_PIN → LED long leg → 220–470Ω → GND  (same as Phase 1)

#define LED_PIN  0
#define REED_PIN 2  // PA2 — any free GPIO works; change if needed

void setup() {
  pinMode(LED_PIN, OUTPUT);
  pinMode(REED_PIN, INPUT_PULLUP);
}

void loop() {
  // N/O reed switch: internal pull-up holds pin HIGH when open (no magnet).
  // Magnet closes the switch, pulling the pin LOW.
  bool magnetPresent = (digitalRead(REED_PIN) == LOW);
  digitalWrite(LED_PIN, magnetPresent);
}
