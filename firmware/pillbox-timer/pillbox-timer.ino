// Pillbox Timer — integration firmware.
// Tracks time since the lid was last opened (magnet leaves the reed switch) and
// shows one of two displays, selected by DEFAULT_MODE below:
//   MODE_DOSE_STATUS   DONE / TAKE / OVERDUE headline + subtitle — interval-based
//   MODE_LAST_OPENED   "JUST NOW" / "7H AGO"                     — passive log
// The RTC's periodic interrupt wakes the chip every 32 s to keep counting, and
// the display is refreshed only when the shown text would actually change.
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

// --- display mode -----------------------------------------------------------
#define MODE_LAST_OPENED 0
#define MODE_DOSE_STATUS 1
#define DEFAULT_MODE     MODE_DOSE_STATUS   // change to switch modes (button later)

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

uint8_t displayMode = DEFAULT_MODE;   // runtime var so a button can flip it later

// ---------------------------------------------------------------------------
// 5x7 font. Each glyph is 7 rows; the low 5 bits of each row are pixels,
// bit4 = leftmost. FONT_CHARS gives the character at each glyph index.
// ---------------------------------------------------------------------------

static const char FONT_CHARS[] = " 0123456789ADGHJMNOSTUWERVXIKLP";

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
  {0b11111,0b10000,0b10000,0b11110,0b10000,0b10000,0b11111}, // E
  {0b11110,0b10001,0b10001,0b11110,0b10100,0b10010,0b10001}, // R
  {0b10001,0b10001,0b10001,0b10001,0b10001,0b01010,0b00100}, // V
  {0b10001,0b10001,0b01010,0b00100,0b01010,0b10001,0b10001}, // X
  {0b01110,0b00100,0b00100,0b00100,0b00100,0b00100,0b01110}, // I
  {0b10001,0b10010,0b10100,0b11000,0b10100,0b10010,0b10001}, // K
  {0b10000,0b10000,0b10000,0b10000,0b10000,0b10000,0b11111}, // L
  {0b11110,0b10001,0b10001,0b11110,0b10000,0b10000,0b10000}, // P
};

static int glyphIndex(char c) {
  const char* p = strchr(FONT_CHARS, c);
  return p ? (int)(p - FONT_CHARS) : 0;   // unknown chars render as space
}

// ---------------------------------------------------------------------------
// Screen layout (top to bottom): big headline, capsule pill icon, small
// subtitle. We can't hold a full 200x200 frame (5000 bytes > 2KB SRAM), so the
// two text lines are rendered into band buffers and the pill is drawn
// procedurally during streaming; rows outside all three stream as white.
// ---------------------------------------------------------------------------

#define BIG_SCALE 4
#define BIG_H     (7 * BIG_SCALE)   // 28 px
#define BIG_TOP   28               // headline near the top
#define SUB_SCALE 2
#define SUB_H     (7 * SUB_SCALE)   // 14 px
#define SUB_TOP   160              // subtitle near the bottom

// Capsule pill icon in the middle (drawn procedurally, no buffer).
#define PILL_W   84
#define PILL_H   34
#define PILL_TOP 86                // rows 86..119, centered around y=103

static uint8_t bigBand[BIG_H * 25];   // 700 bytes; 0 = black, 1 = white
static uint8_t subBand[SUB_H * 25];   // 350 bytes

// Render a centered, scaled string into a band buffer (bandH rows tall).
static void renderLine(uint8_t* buf, int bandH, const char* s, int scale) {
  memset(buf, 0xFF, bandH * 25);                 // white background
  int width = (int)strlen(s) * 6 * scale - scale;  // ink width
  int penX = (200 - width) / 2;
  if (penX < 0) penX = 0;
  for (int i = 0; s[i]; i++) {
    int gi = glyphIndex(s[i]);
    for (int gy = 0; gy < 7; gy++) {
      uint8_t bits = FONT[gi][gy];
      for (int gx = 0; gx < 5; gx++) {
        if (bits & (1 << (4 - gx))) {
          for (int dy = 0; dy < scale; dy++)
            for (int dx = 0; dx < scale; dx++) {
              int x = penX + gx * scale + dx;
              int y = gy * scale + dy;
              if (x >= 0 && x < 200 && y >= 0 && y < bandH)
                buf[y * 25 + (x >> 3)] &= ~(0x80 >> (x & 7));
            }
        }
      }
    }
    penX += 6 * scale;
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

// Is (x,y) inside a horizontal capsule of size W x H, shrunk by `inset`?
// The capsule is a rectangle with semicircular left/right ends.
static bool inCapsule(int x, int y, int W, int H, int inset) {
  int r = H / 2, cy = H / 2;
  int left = r, right = W - 1 - r;
  int rr = r - inset;
  if (rr < 0) return false;
  if (x < left)  { int dx = x - left,  dy = y - cy; return dx * dx + dy * dy <= rr * rr; }
  if (x > right) { int dx = x - right, dy = y - cy; return dx * dx + dy * dy <= rr * rr; }
  return (y >= cy - rr && y <= cy + rr);
}

// Render one row of the two-tone capsule pill into a 25-byte (200 px) row:
// black outline, left half filled black, right half empty — reads as a pill.
static void pillRow(int y, uint8_t* dst) {
  memset(dst, 0xFF, 25);
  const int x0 = (200 - PILL_W) / 2;        // centered horizontally
  for (int px = 0; px < PILL_W; px++) {
    if (!inCapsule(px, y, PILL_W, PILL_H, 0)) continue;
    bool inner = inCapsule(px, y, PILL_W, PILL_H, 3);
    bool black = !inner || (px < PILL_W / 2);   // border, or left half fill
    if (black) {
      int x = x0 + px;
      dst[x >> 3] &= ~(0x80 >> (x & 7));
    }
  }
}

// Fill a 25-byte row from whichever element covers it, else white.
static void srcRowBytes(int row, uint8_t* dst) {
  if (row >= BIG_TOP && row < BIG_TOP + BIG_H) {
    memcpy(dst, &bigBand[(row - BIG_TOP) * 25], 25);
  } else if (row >= PILL_TOP && row < PILL_TOP + PILL_H) {
    pillRow(row - PILL_TOP, dst);
  } else if (row >= SUB_TOP && row < SUB_TOP + SUB_H) {
    memcpy(dst, &subBand[(row - SUB_TOP) * 25], 25);
  } else {
    memset(dst, 0xFF, 25);                 // white
  }
}

// Streams the frame rotated 180° so text reads upright relative to the flex
// cable: rows bottom-to-top, and each row mirrored horizontally (byte order
// reversed + bits within each byte reversed). 200 px == exactly 25 bytes, so
// the horizontal mirror is exact with no pixel offset.
static void epd_draw_frame() {
  epd_cmd(0x24);                         // write B/W RAM
  digitalWrite(DC_PIN, HIGH);
  digitalWrite(CS_PIN, LOW);
  for (int outRow = 0; outRow < 200; outRow++) {
    uint8_t rowBytes[25];
    srcRowBytes(199 - outRow, rowBytes);
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

// Show a big headline plus a subtitle. Pass "" for sub to leave it blank.
static void showText(const char* big, const char* sub) {
  renderLine(bigBand, BIG_H, big, BIG_SCALE);
  renderLine(subBand, SUB_H, sub, SUB_SCALE);
  epd_init();        // panel was deep-asleep; reset + re-init to wake it
  epd_draw_frame();
  epd_refresh();
  epd_deep_sleep();
}

// ---------------------------------------------------------------------------
// Timekeeping
// ---------------------------------------------------------------------------

// The PIT wakes the chip on this period. 32 s is the longest single PIT period
// the hardware can do (32768 cycles of the 1.024 kHz oscillator), which means
// ~2700 wakes/day instead of 86400 at 1 s — much less wake energy. Time is
// tracked in 32 s steps, which is plenty for dose state and minute display.
#define PIT_TICK_SEC 32

volatile uint32_t elapsedSec = 0;
volatile bool tickFlag = false;
volatile bool lidFlag  = false;
char lastBig[16] = "";
char lastSub[16] = "";

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
// Resolution: minutes under an hour, then hours, then days.
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
// Headline stays <= 8 chars to fit one line at the big scale; subtitle is small.
//
// NOTE: in last-opened mode the minute resolution means a full refresh (with
// its ~2s flash) every minute for the first hour — fine for the bench, but
// coarsen for production (see ROADMAP). Dose mode changes at most hourly.
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

void goToSleep() {
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  sleep_enable();
  sleep_cpu();          // wakes on PIT (every 32 s) or reed edge (lid open)
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
  formatStatus(0, lastBig, lastSub);
  showText(lastBig, lastSub);
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
  if (strcmp(big, lastBig) != 0 || strcmp(sub, lastSub) != 0) {  // refresh on change
    strcpy(lastBig, big);
    strcpy(lastSub, sub);
    showText(big, sub);
  }
}
