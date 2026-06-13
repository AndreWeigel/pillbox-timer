// Pillbox Timer — integration firmware.
// Shows "last opened X ago" on the e-paper. Opening the lid (magnet leaves the
// reed switch) resets the timer to "JUST NOW". The RTC's periodic interrupt
// wakes the chip once per second to keep counting, and the display is refreshed
// only when the shown text would actually change.
//
// Power: chip sleeps in POWER-DOWN (~1µA). The PIT runs off the internal 32kHz
// oscillator and still ticks in power-down; the reed switch is on a fully
// asynchronous pin (PA6 = pin 2) so its edge also wakes from power-down.
//
// Dev indicator: two slow blinks at boot (alive), then a short blink on each
// lid-open. The LED is breadboard-only — the final PCB has no LED.
//
// SOFTWARE (bit-banged) SPI on the exact pins below — the hardware SPI peripheral
// lives on PA1/PA3 (pins 14/16), which is NOT where the display is wired.
//
// Wiring (display module -> megaTinyCore pin):
//   DIN  -> pin 1      CLK  -> pin 3
//   CS   -> pin 4      DC   -> pin 5
//   RST  -> pin 6      BUSY -> pin 7
//   Reed switch: pin 2 -> GND     LED: pin 0 + 220-470 ohm -> GND
//   Display VCC -> 3.3V,  GND -> GND

#include <avr/sleep.h>
#include <avr/interrupt.h>
#include <stdlib.h>
#include <string.h>

#define LED_PIN  0
#define REED_PIN 2
#define DIN_PIN  1
#define CLK_PIN  3
#define CS_PIN   4
#define DC_PIN   5
#define RST_PIN  6
#define BUSY_PIN 7

// ---------------------------------------------------------------------------
// 5x7 font. Each glyph is 7 rows; the low 5 bits of each row are pixels,
// bit4 = leftmost. FONT_CHARS gives the character at each glyph index.
// ---------------------------------------------------------------------------

static const char FONT_CHARS[] = " 0123456789ADGHJMNOSTUW";

static const uint8_t FONT[][7] = {
  {0b00000,0b00000,0b00000,0b00000,0b00000,0b00000,0b00000}, // ' '
  {0b01110,0b10001,0b10011,0b10101,0b11001,0b10001,0b01110}, // 0
  {0b00100,0b01100,0b00100,0b00100,0b00100,0b00100,0b01110}, // 1
  {0b01110,0b10001,0b00001,0b00010,0b00100,0b01000,0b11111}, // 2
  {0b11111,0b00010,0b00100,0b00010,0b00001,0b10001,0b01110}, // 3
  {0b00010,0b00110,0b01010,0b10010,0b11111,0b00010,0b00010}, // 4
  {0b11111,0b10000,0b11110,0b00001,0b00001,0b10001,0b01110}, // 5
  {0b00110,0b01000,0b10000,0b11110,0b10001,0b10001,0b01110}, // 6
  {0b11111,0b00001,0b00010,0b00100,0b01000,0b01000,0b01000}, // 7
  {0b01110,0b10001,0b10001,0b01110,0b10001,0b10001,0b01110}, // 8
  {0b01110,0b10001,0b10001,0b01111,0b00001,0b00010,0b01100}, // 9
  {0b01110,0b10001,0b10001,0b11111,0b10001,0b10001,0b10001}, // A
  {0b11110,0b10001,0b10001,0b10001,0b10001,0b10001,0b11110}, // D
  {0b01110,0b10001,0b10000,0b10111,0b10001,0b10001,0b01111}, // G
  {0b10001,0b10001,0b10001,0b11111,0b10001,0b10001,0b10001}, // H
  {0b00111,0b00010,0b00010,0b00010,0b00010,0b10010,0b01100}, // J
  {0b10001,0b11011,0b10101,0b10101,0b10001,0b10001,0b10001}, // M
  {0b10001,0b11001,0b10101,0b10011,0b10001,0b10001,0b10001}, // N
  {0b01110,0b10001,0b10001,0b10001,0b10001,0b10001,0b01110}, // O
  {0b01111,0b10000,0b10000,0b01110,0b00001,0b00001,0b11110}, // S
  {0b11111,0b00100,0b00100,0b00100,0b00100,0b00100,0b00100}, // T
  {0b10001,0b10001,0b10001,0b10001,0b10001,0b10001,0b01110}, // U
  {0b10001,0b10001,0b10001,0b10101,0b10101,0b11011,0b10001}, // W
};

static int glyphIndex(char c) {
  const char* p = strchr(FONT_CHARS, c);
  return p ? (int)(p - FONT_CHARS) : 0;   // unknown chars render as space
}

// ---------------------------------------------------------------------------
// Band buffer: one horizontal strip tall enough for scaled text. We can't hold
// a full 200x200 frame (5000 bytes > 2KB SRAM), so only the text band is
// buffered; everything above/below streams as white.
// ---------------------------------------------------------------------------

#define SCALE    4
#define BAND_H   (7 * SCALE)            // 28 px
#define BAND_TOP ((200 - BAND_H) / 2)   // vertically centered
static uint8_t band[BAND_H * 25];       // 700 bytes; 0 = black, 1 = white

static void bandSetBlack(int x, int y) {
  if (x < 0 || x >= 200 || y < 0 || y >= BAND_H) return;
  band[y * 25 + (x >> 3)] &= ~(0x80 >> (x & 7));
}

static void renderText(const char* s) {
  memset(band, 0xFF, sizeof(band));     // white background
  int len = strlen(s);
  int width = len * 6 * SCALE - SCALE;  // ink width (5 px glyph + 1 px gap)
  int penX = (200 - width) / 2;
  if (penX < 0) penX = 0;
  for (int i = 0; s[i]; i++) {
    int gi = glyphIndex(s[i]);
    for (int gy = 0; gy < 7; gy++) {
      uint8_t bits = FONT[gi][gy];
      for (int gx = 0; gx < 5; gx++) {
        if (bits & (1 << (4 - gx))) {
          for (int dy = 0; dy < SCALE; dy++)
            for (int dx = 0; dx < SCALE; dx++)
              bandSetBlack(penX + gx * SCALE + dx, gy * SCALE + dy);
        }
      }
    }
    penX += 6 * SCALE;
  }
}

// ---------------------------------------------------------------------------
// SSD1681 e-paper driver (bit-banged SPI, mode 0, MSB first)
// ---------------------------------------------------------------------------

static void spiByte(uint8_t b) {
  for (uint8_t i = 0; i < 8; i++) {
    digitalWrite(DIN_PIN, (b & 0x80) ? HIGH : LOW);
    b <<= 1;
    digitalWrite(CLK_PIN, HIGH);
    digitalWrite(CLK_PIN, LOW);
  }
}

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
  while (digitalRead(BUSY_PIN) == HIGH) {   // BUSY is HIGH while busy
    if (millis() - start > 5000) break;     // safety timeout
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

  epd_cmd(0x44); epd_dat(0x00); epd_dat(0x18);   // RAM X: bytes 0-24

  epd_cmd(0x45);                         // RAM Y: start=199, end=0
  epd_dat(0xC7); epd_dat(0x00);
  epd_dat(0x00); epd_dat(0x00);

  epd_cmd(0x3C); epd_dat(0x01);          // border waveform
  epd_cmd(0x18); epd_dat(0x80);          // temperature: internal sensor

  epd_cmd(0x22); epd_dat(0xB1);          // load temperature + waveform
  epd_cmd(0x20); epd_wait();

  epd_cmd(0x4E); epd_dat(0x00);          // RAM X cursor = 0
  epd_cmd(0x4F); epd_dat(0xC7); epd_dat(0x00);   // RAM Y cursor = 199
  epd_wait();
}

static uint8_t reverseBits(uint8_t b) {
  b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
  b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
  b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
  return b;
}

// Streams the frame rotated 180° so text reads upright relative to the flex
// cable: rows bottom-to-top, and each row mirrored horizontally (byte order
// reversed + bits within each byte reversed). 200 px == exactly 25 bytes, so
// the horizontal mirror is exact with no pixel offset.
static void epd_draw_band() {
  epd_cmd(0x24);                         // write B/W RAM
  digitalWrite(DC_PIN, HIGH);
  digitalWrite(CS_PIN, LOW);
  for (int outRow = 0; outRow < 200; outRow++) {
    int srcRow = 199 - outRow;
    uint8_t rowBytes[25];
    if (srcRow >= BAND_TOP && srcRow < BAND_TOP + BAND_H) {
      uint8_t* br = &band[(srcRow - BAND_TOP) * 25];
      for (int col = 0; col < 25; col++) rowBytes[col] = br[col];
    } else {
      for (int col = 0; col < 25; col++) rowBytes[col] = 0xFF;  // white
    }
    for (int ob = 0; ob < 25; ob++) spiByte(reverseBits(rowBytes[24 - ob]));
  }
  digitalWrite(CS_PIN, HIGH);
}

static void epd_refresh() {
  epd_cmd(0x22); epd_dat(0xF7);          // full update sequence
  epd_cmd(0x20);
  epd_wait();
}

static void epd_deep_sleep() {
  epd_cmd(0x10); epd_dat(0x01);          // panel deep sleep — holds image at 0 power
}

static void showText(const char* s) {
  renderText(s);
  epd_init();        // panel was deep-asleep; reset + re-init to wake it
  epd_draw_band();
  epd_refresh();
  epd_deep_sleep();
}

// ---------------------------------------------------------------------------
// Timekeeping
// ---------------------------------------------------------------------------

volatile uint32_t elapsedSec = 0;
volatile bool tickFlag = false;
volatile bool lidFlag  = false;
char lastShown[16] = "";

ISR(RTC_PIT_vect) {
  RTC.PITINTFLAGS = RTC_PI_bm;   // clear the interrupt
  elapsedSec++;
  tickFlag = true;
}

void reedISR() { lidFlag = true; }   // lid opened (magnet left the switch)

static void rtcPitInit() {
  RTC.CLKSEL = RTC_CLKSEL_INT32K_gc;                       // 32.768 kHz internal
  RTC.PITINTCTRL = RTC_PI_bm;                              // enable PIT interrupt
  while (RTC.PITSTATUS & RTC_CTRLBUSY_bm) ;                // wait for sync
  RTC.PITCTRLA = RTC_PERIOD_CYC32768_gc | RTC_PITEN_bm;    // 32768/32768 Hz = 1 s
}

// Build "last opened" text. Shows minutes for the first hour, then hours,
// then days. NOTE: minute-resolution means a full refresh (with its ~2s flash)
// every minute for the first hour — fine for the bench, but coarsen this for
// production to protect battery life and avoid constant flashing.
static void formatElapsed(uint32_t s, char* out) {
  if (s < 60) { strcpy(out, "JUST NOW"); return; }
  unsigned long v;
  char unit;
  if (s < 3600UL)       { v = s / 60UL;    unit = 'M'; }
  else if (s < 86400UL) { v = s / 3600UL;  unit = 'H'; }
  else                  { v = s / 86400UL; unit = 'D'; }
  char* p = out;
  char num[12];
  ultoa(v, num, 10);
  for (char* q = num; *q; q++) *p++ = *q;
  *p++ = unit;
  *p++ = ' '; *p++ = 'A'; *p++ = 'G'; *p++ = 'O';
  *p = '\0';
}

// ---------------------------------------------------------------------------

void goToSleep() {
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  sleep_enable();
  sleep_cpu();          // wakes on PIT (1/s) or reed edge (lid open)
  sleep_disable();
}

void setup() {
  pinMode(LED_PIN,  OUTPUT);
  pinMode(REED_PIN, INPUT_PULLUP);
  pinMode(DIN_PIN,  OUTPUT);
  pinMode(CLK_PIN,  OUTPUT); digitalWrite(CLK_PIN, LOW);
  pinMode(CS_PIN,   OUTPUT); digitalWrite(CS_PIN,  HIGH);
  pinMode(DC_PIN,   OUTPUT);
  pinMode(RST_PIN,  OUTPUT); digitalWrite(RST_PIN, HIGH);
  pinMode(BUSY_PIN, INPUT);

  // Lid opens when the magnet LEAVES the reed switch -> RISING edge.
  attachInterrupt(digitalPinToInterrupt(REED_PIN), reedISR, RISING);
  rtcPitInit();

  // Two slow blinks = alive
  for (uint8_t i = 0; i < 2; i++) {
    digitalWrite(LED_PIN, HIGH); delay(500);
    digitalWrite(LED_PIN, LOW);  delay(500);
  }

  elapsedSec = 0;
  strcpy(lastShown, "JUST NOW");
  showText(lastShown);
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

  char buf[16];
  formatElapsed(sec, buf);
  if (strcmp(buf, lastShown) != 0) {  // only refresh when the text changes
    strcpy(lastShown, buf);
    showText(buf);
  }
}
