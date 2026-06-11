// Pillbox Timer — Step 1: validate the programming pipeline.
// Blinks an LED on pin 0 once per second.
// Wiring: pin 0 → LED long leg, LED short leg → 220-470Ω resistor → GND.

#define LED_PIN 0

void setup() {
  pinMode(LED_PIN, OUTPUT);
}

void loop() {
  digitalWrite(LED_PIN, HIGH);
  delay(500);
  digitalWrite(LED_PIN, LOW);
  delay(500);
}
