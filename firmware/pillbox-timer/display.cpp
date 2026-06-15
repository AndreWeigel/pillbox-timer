#include "display.h"
#include "epd.h"
#include "config.h"
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

// Helpers to set/clear one black pixel in a 25-byte row (0 = black).
static void rowBlack(uint8_t* dst, int x) {
  if (x >= 0 && x < 200) dst[x >> 3] &= ~(0x80 >> (x & 7));
}

// Render one absolute row of a two-tone capsule pill (outline + left half
// filled), centered horizontally, occupying rows [top, top+H). White elsewhere.
static void capsuleRowAbs(int absRow, int top, int W, int H, uint8_t* dst) {
  memset(dst, 0xFF, 25);
  int y = absRow - top;
  if (y < 0 || y >= H) return;
  int x0 = (200 - W) / 2;
  for (int px = 0; px < W; px++) {
    if (!inCapsule(px, y, W, H, 0)) continue;
    bool inner = inCapsule(px, y, W, H, 3);
    if (!inner || px < W / 2) rowBlack(dst, x0 + px);   // border, or left half
  }
}

// --- pixel-art face (eyes + mouth, no outline) ------------------------------
#define EYE_W      18
#define EYE_H      18
#define EYE_TOP    58
#define EYE_LX     62              // left eye:  x 62..79
#define EYE_RX     120             // right eye: x 120..137
#define MOUTH_CX   100
#define MOUTH_HALF 34              // mouth spans x 66..134
#define MOUTH_THICK 2
#define SMILE_BASE 124             // happy: curve centre (lowest point)
#define FROWN_TOP  100             // worried: curve centre (highest point)
#define MOUTH_DEPTH 24

// Render one absolute row of a face: square eyes plus a parabolic mouth that
// smiles (happy) or frowns (worried). White where the row misses both.
static void faceRow(int absRow, bool happy, uint8_t* dst) {
  memset(dst, 0xFF, 25);
  if (absRow >= EYE_TOP && absRow < EYE_TOP + EYE_H) {
    for (int x = EYE_LX; x < EYE_LX + EYE_W; x++) rowBlack(dst, x);
    for (int x = EYE_RX; x < EYE_RX + EYE_W; x++) rowBlack(dst, x);
  }
  for (int x = MOUTH_CX - MOUTH_HALF; x <= MOUTH_CX + MOUTH_HALF; x++) {
    long dxn = x - MOUTH_CX;
    long off = (MOUTH_DEPTH * dxn * dxn) / ((long)MOUTH_HALF * MOUTH_HALF);
    int cy = happy ? (SMILE_BASE - (int)off) : (FROWN_TOP + (int)off);
    if (absRow >= cy - MOUTH_THICK && absRow <= cy + MOUTH_THICK) rowBlack(dst, x);
  }
}

// Active layout/icon for the row provider; set by setupRender() before each
// epdWriteRam() call (which calls srcRowBytes synchronously).
enum Layout { LAYOUT_TEXT, LAYOUT_PIXEL };
enum Icon   { ICON_PILL, ICON_HAPPY, ICON_WORRIED };
static Layout gLayout = LAYOUT_TEXT;
static Icon   gIcon   = ICON_PILL;

#define PIXEL_PILL_TOP 72          // bigger centered pill for pixel layout
#define PIXEL_PILL_W   120
#define PIXEL_PILL_H   48

// Row provider for the driver: fills a 25-byte row for the active layout.
static void srcRowBytes(int row, uint8_t* dst) {
  if (gLayout == LAYOUT_TEXT) {
    if (row >= BIG_TOP && row < BIG_TOP + BIG_H)
      memcpy(dst, &bigBand[(row - BIG_TOP) * 25], 25);
    else if (row >= PILL_TOP && row < PILL_TOP + PILL_H)
      capsuleRowAbs(row, PILL_TOP, PILL_W, PILL_H, dst);
    else if (row >= SUB_TOP && row < SUB_TOP + SUB_H)
      memcpy(dst, &subBand[(row - SUB_TOP) * 25], 25);
    else
      memset(dst, 0xFF, 25);
  } else {  // LAYOUT_PIXEL: big face (or pill) + subtitle
    if (row >= SUB_TOP && row < SUB_TOP + SUB_H)
      memcpy(dst, &subBand[(row - SUB_TOP) * 25], 25);
    else if (gIcon == ICON_PILL)
      capsuleRowAbs(row, PIXEL_PILL_TOP, PIXEL_PILL_W, PIXEL_PILL_H, dst);
    else
      faceRow(row, gIcon == ICON_HAPPY, dst);
  }
}

static void renderInto(const char* big, const char* sub) {
  renderLine(bigBand, BIG_H, big, BIG_SCALE);
  renderLine(subBand, SUB_H, sub, SUB_SCALE);
}

// Set the layout, icon, and band contents for one screen. Called for both the
// new screen and (during a partial refresh) the previous one.
static void setupRender(DoseState state, const char* big, const char* sub) {
#if DISPLAY_STYLE == STYLE_PIXEL
  if (state != STATE_INFO) {
    gLayout = LAYOUT_PIXEL;
    gIcon = (state == STATE_DONE) ? ICON_HAPPY
          : (state == STATE_TAKE) ? ICON_PILL
                                  : ICON_WORRIED;
    renderLine(subBand, SUB_H, sub, SUB_SCALE);
    return;
  }
#endif
  gLayout = LAYOUT_TEXT;
  gIcon = ICON_PILL;
  renderInto(big, sub);
}

// ---------------------------------------------------------------------------
// Refresh policy: partial (no-flash) updates, with a full refresh every Nth
// change to clear accumulated ghosting. Partial diffs the new image (0x24)
// against the previously shown one (0x26), which we re-render from lastBig/
// lastSub — so we never need to keep a full framebuffer around.
// ---------------------------------------------------------------------------


static char lastBig[16] = "";
static char lastSub[16] = "";
static DoseState lastState = STATE_INFO;
static uint8_t updateCount = 0;
static bool primed = false;    // has anything been shown yet?

void displaySetup() {
  epdSetup();
}

void displayShow(DoseState state, const char* big, const char* sub) {
  if (primed && state == lastState &&
      strcmp(big, lastBig) == 0 && strcmp(sub, lastSub) == 0)
    return;                                        // nothing changed

  bool full = (updateCount % FULL_REFRESH_EVERY == 0);  // includes the first show

  if (full) {
    setupRender(state, big, sub);
    epdInitFull();
    epdWriteRam(0x24, srcRowBytes);                // new image
    epdRefreshFull();
  } else {
    epdInitPartial();
    setupRender(lastState, lastBig, lastSub);
    epdWriteRam(0x26, srcRowBytes);                // old image (for the diff)
    setupRender(state, big, sub);
    epdWriteRam(0x24, srcRowBytes);                // new image
    epdRefreshPartial();
  }
  epdDeepSleep();

  strcpy(lastBig, big);
  strcpy(lastSub, sub);
  lastState = state;
  updateCount++;
  primed = true;
}
