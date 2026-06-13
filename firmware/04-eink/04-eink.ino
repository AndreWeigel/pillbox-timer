// Pillbox Timer — Step 4: Waveshare 1.54" SSD1681 e-paper test.
// Chip sleeps in power-down. Bringing the magnet close wakes it and redraws the
// display with a toggling pattern (black-top <-> white-top).
//
// Startup indicator: two SLOW blinks, then the LED stays solid ON once setup()
// finishes. If the LED ends up solid, the code ran all the way through (no hang).
//
// IMPORTANT: this uses SOFTWARE (bit-banged) SPI, so DIN/CLK are driven on the
// exact megaTinyCore pins below — no dependency on the hardware-SPI pin mux.
// (The hardware SPI peripheral is on PA1/PA3 = pins 14/16, which is NOT where
//  the display is wired. That mismatch is why earlier versions did nothing.)
//
// Wiring (display module -> megaTinyCore pin):
//   DIN  -> pin 1
//   CLK  -> pin 3
//   CS   -> pin 4
//   DC   -> pin 5
//   RST  -> pin 6
//   BUSY -> pin 7
//   Reed switch: pin 2 -> GND
//   LED  -> pin 0  with 220-470 ohm to GND
//   VCC  -> 3.3V,  GND -> GND

#include <avr/sleep.h>

#define LED_PIN  0
#define REED_PIN 2
#define DIN_PIN  1
#define CLK_PIN  3
#define CS_PIN   4
#define DC_PIN   5
#define RST_PIN  6
#define BUSY_PIN 7

volatile bool woke = false;
static uint8_t frame = 0;

void reedISR() { woke = true; }

void blinkSlow() {
  digitalWrite(LED_PIN, HIGH); delay(500);
  digitalWrite(LED_PIN, LOW);  delay(500);
}

// --- bit-banged SPI (mode 0: clock idle low, MSB first) ---

static void spiByte(uint8_t b) {
  for (uint8_t i = 0; i < 8; i++) {
    digitalWrite(DIN_PIN, (b & 0x80) ? HIGH : LOW);
    b <<= 1;
    digitalWrite(CLK_PIN, HIGH);
    digitalWrite(CLK_PIN, LOW);
  }
}

// --- SSD1681 helpers ---

static void epd_cmd(uint8_t c) {
  digitalWrite(DC_PIN, LOW);
  digitalWrite(CS_PIN, LOW);
  spiByte(c);
  digitalWrite(CS_PIN, HIGH);
}

static void epd_dat(uint8_t d) {
  digitalWrite(DC_PIN, HIGH);
  digitalWrite(CS_PIN, LOW);
  spiByte(d);
  digitalWrite(CS_PIN, HIGH);
}

static void epd_wait() {
  uint32_t start = millis();
  while (digitalRead(BUSY_PIN) == HIGH) {     // SSD1681 BUSY is HIGH while busy
    if (millis() - start > 5000) break;        // safety timeout — remove once confirmed
  }
}

static void epd_reset() {
  digitalWrite(RST_PIN, HIGH); delay(20);
  digitalWrite(RST_PIN, LOW);  delay(5);
  digitalWrite(RST_PIN, HIGH); delay(20);
  epd_wait();
}

static void epd_init() {
  epd_reset();
  epd_cmd(0x12); epd_wait();             // software reset

  epd_cmd(0x01);                         // driver output: 200 gates
  epd_dat(0xC7); epd_dat(0x00); epd_dat(0x01);

  epd_cmd(0x11); epd_dat(0x01);          // data entry: Y decrement, X increment

  epd_cmd(0x44); epd_dat(0x00); epd_dat(0x18);   // RAM X: bytes 0-24 (200 px)

  epd_cmd(0x45);                         // RAM Y: start=199, end=0
  epd_dat(0xC7); epd_dat(0x00);
  epd_dat(0x00); epd_dat(0x00);

  epd_cmd(0x3C); epd_dat(0x01);          // border waveform
  epd_cmd(0x18); epd_dat(0x80);          // temperature: internal sensor

  epd_cmd(0x22); epd_dat(0xB1);          // load temperature + waveform from OTP
  epd_cmd(0x20); epd_wait();

  epd_cmd(0x4E); epd_dat(0x00);          // RAM X cursor = 0
  epd_cmd(0x4F); epd_dat(0xC7); epd_dat(0x00);   // RAM Y cursor = 199
  epd_wait();
}

static void epd_draw(uint8_t frameIndex) {
  epd_cmd(0x24);                         // write B/W RAM
  digitalWrite(DC_PIN, HIGH);
  digitalWrite(CS_PIN, LOW);
  for (uint16_t row = 0; row < 200; row++) {
    bool topHalf = (row < 100);
    uint8_t px = (topHalf == (frameIndex % 2 == 0)) ? 0x00 : 0xFF;
    for (uint8_t col = 0; col < 25; col++) spiByte(px);
  }
  digitalWrite(CS_PIN, HIGH);
}

static void epd_refresh() {
  epd_cmd(0x22); epd_dat(0xF7);          // full update sequence
  epd_cmd(0x20);
  epd_wait();
}

static void epd_deep_sleep() {
  epd_cmd(0x10); epd_dat(0x01);
}

// --- main ---

void updateDisplay() {
  epd_init();
  epd_draw(frame++);
  epd_refresh();
  epd_deep_sleep();
}

void goToSleep() {
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  sleep_enable();
  sleep_cpu();
  sleep_disable();
}

void setup() {
  pinMode(LED_PIN,  OUTPUT);
  pinMode(REED_PIN, INPUT_PULLUP);
  pinMode(DIN_PIN,  OUTPUT);
  pinMode(CLK_PIN,  OUTPUT); digitalWrite(CLK_PIN, LOW);   // clock idle low
  pinMode(CS_PIN,   OUTPUT); digitalWrite(CS_PIN,  HIGH);
  pinMode(DC_PIN,   OUTPUT);
  pinMode(RST_PIN,  OUTPUT); digitalWrite(RST_PIN, HIGH);
  pinMode(BUSY_PIN, INPUT);

  attachInterrupt(digitalPinToInterrupt(REED_PIN), reedISR, FALLING);

  blinkSlow(); blinkSlow();    // two slow blinks
  updateDisplay();
  digitalWrite(LED_PIN, HIGH); // ...then LED stays solid ON = setup finished
}

void loop() {
  goToSleep();
  if (woke) {
    woke = false;
    delay(20);
    digitalWrite(LED_PIN, LOW);   // brief off-pulse = "magnet woke me"
    delay(150);
    digitalWrite(LED_PIN, HIGH);
    updateDisplay();
  }
}
