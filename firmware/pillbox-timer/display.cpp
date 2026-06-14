#include "display.h"
#include "epd.h"
#include <Arduino.h>
#include <string.h>

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

#define PILL_W   84
#define PILL_H   34
#define PILL_TOP 86                // rows 86..119, centered around y=103

static uint8_t bigBand[BIG_H * 25];   // 700 bytes; 0 = black, 1 = white
static uint8_t subBand[SUB_H * 25];   // 350 bytes

// Render a centered, scaled string into a band buffer (bandH rows tall).
static void renderLine(uint8_t* buf, int bandH, const char* s, int scale) {
  memset(buf, 0xFF, bandH * 25);                   // white background
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

// Is (x,y) inside a horizontal capsule of size W x H, shrunk by `inset`?
static bool inCapsule(int x, int y, int W, int H, int inset) {
  int r = H / 2, cy = H / 2;
  int left = r, right = W - 1 - r;
  int rr = r - inset;
  if (rr < 0) return false;
  if (x < left)  { int dx = x - left,  dy = y - cy; return dx * dx + dy * dy <= rr * rr; }
  if (x > right) { int dx = x - right, dy = y - cy; return dx * dx + dy * dy <= rr * rr; }
  return (y >= cy - rr && y <= cy + rr);
}

// Render one row of the two-tone capsule pill: black outline, left half filled.
static void pillRow(int y, uint8_t* dst) {
  memset(dst, 0xFF, 25);
  const int x0 = (200 - PILL_W) / 2;
  for (int px = 0; px < PILL_W; px++) {
    if (!inCapsule(px, y, PILL_W, PILL_H, 0)) continue;
    bool inner = inCapsule(px, y, PILL_W, PILL_H, 3);
    bool black = !inner || (px < PILL_W / 2);
    if (black) {
      int x = x0 + px;
      dst[x >> 3] &= ~(0x80 >> (x & 7));
    }
  }
}

// Row provider for the driver: fills a 25-byte row from whichever element
// covers it (headline band, pill, subtitle band), else white.
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

static void renderInto(const char* big, const char* sub) {
  renderLine(bigBand, BIG_H, big, BIG_SCALE);
  renderLine(subBand, SUB_H, sub, SUB_SCALE);
}

// ---------------------------------------------------------------------------
// Refresh policy: partial (no-flash) updates, with a full refresh every Nth
// change to clear accumulated ghosting. Partial diffs the new image (0x24)
// against the previously shown one (0x26), which we re-render from lastBig/
// lastSub — so we never need to keep a full framebuffer around.
// ---------------------------------------------------------------------------

#define FULL_REFRESH_EVERY 20  // 1 full (flashing) refresh per this many changes;
                               // higher = rarer flash, but ghosting builds up more

static char lastBig[16] = "";
static char lastSub[16] = "";
static uint8_t updateCount = 0;
static bool primed = false;    // has anything been shown yet?

void displaySetup() {
  epdSetup();
}

void displayShow(const char* big, const char* sub) {
  if (primed && strcmp(big, lastBig) == 0 && strcmp(sub, lastSub) == 0)
    return;                                        // nothing changed

  bool full = (updateCount % FULL_REFRESH_EVERY == 0);  // includes the first show

  if (full) {
    renderInto(big, sub);
    epdInitFull();
    epdWriteRam(0x24, srcRowBytes);                // new image
    epdRefreshFull();
  } else {
    epdInitPartial();
    renderInto(lastBig, lastSub);
    epdWriteRam(0x26, srcRowBytes);                // old image (for the diff)
    renderInto(big, sub);
    epdWriteRam(0x24, srcRowBytes);                // new image
    epdRefreshPartial();
  }
  epdDeepSleep();

  strcpy(lastBig, big);
  strcpy(lastSub, sub);
  updateCount++;
  primed = true;
}
