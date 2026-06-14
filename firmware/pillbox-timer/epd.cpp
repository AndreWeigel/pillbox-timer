#include "epd.h"

// Pins are driven directly here so the rest of the firmware never touches them.
#define DIN_PIN  1
#define CLK_PIN  3
#define CS_PIN   4
#define DC_PIN   5
#define RST_PIN  6
#define BUSY_PIN 7

// Partial-refresh waveform for the SSD1681 1.54" V2, from Waveshare's reference
// driver (WF_PARTIAL_1IN54_0). Bytes 0..152 are the LUT loaded via 0x32; bytes
// 153..158 are the gate/source/VCOM voltage settings applied afterward.
static const uint8_t WF_PARTIAL[159] = {
  0x0,0x40,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
  0x80,0x80,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
  0x40,0x40,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
  0x0,0x80,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
  0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
  0xF,0x0,0x0,0x0,0x0,0x0,0x0,
  0x1,0x1,0x0,0x0,0x0,0x0,0x0,
  0x0,0x0,0x0,0x0,0x0,0x0,0x0,
  0x0,0x0,0x0,0x0,0x0,0x0,0x0,
  0x0,0x0,0x0,0x0,0x0,0x0,0x0,
  0x0,0x0,0x0,0x0,0x0,0x0,0x0,
  0x0,0x0,0x0,0x0,0x0,0x0,0x0,
  0x0,0x0,0x0,0x0,0x0,0x0,0x0,
  0x0,0x0,0x0,0x0,0x0,0x0,0x0,
  0x0,0x0,0x0,0x0,0x0,0x0,0x0,
  0x0,0x0,0x0,0x0,0x0,0x0,0x0,
  0x0,0x0,0x0,0x0,0x0,0x0,0x0,
  0x22,0x22,0x22,0x22,0x22,0x22,0x0,0x0,0x0,
  0x02,0x17,0x41,0xB0,0x32,0x28,
};

// --- bit-banged SPI (mode 0: clock idle low, MSB first) ---------------------

static void spiByte(uint8_t b) {
  for (uint8_t i = 0; i < 8; i++) {
    digitalWrite(DIN_PIN, (b & 0x80) ? HIGH : LOW);
    b <<= 1;
    digitalWrite(CLK_PIN, HIGH);
    digitalWrite(CLK_PIN, LOW);
  }
}

static void epdCmd(uint8_t c) {
  digitalWrite(DC_PIN, LOW);
  digitalWrite(CS_PIN, LOW);
  spiByte(c);
  digitalWrite(CS_PIN, HIGH);
}

static void epdDat(uint8_t d) {
  digitalWrite(DC_PIN, HIGH);
  digitalWrite(CS_PIN, LOW);
  spiByte(d);
  digitalWrite(CS_PIN, HIGH);
}

static void epdWait() {
  uint32_t start = millis();
  while (digitalRead(BUSY_PIN) == HIGH) {   // BUSY is HIGH while busy
    if (millis() - start > 5000) break;     // safety timeout
  }
}

// Reverse the 8 bits of a byte (horizontal mirror within a byte, for rotation).
static uint8_t reverseBits(uint8_t b) {
  b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
  b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
  b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
  return b;
}

// --- init building blocks ---------------------------------------------------

static void epdHwReset() {
  digitalWrite(RST_PIN, HIGH); delay(20);
  digitalWrite(RST_PIN, LOW);  delay(5);
  digitalWrite(RST_PIN, HIGH); delay(20);
  epdWait();
}

// Common config after a hardware reset: gates, scan direction, RAM window.
static void epdBasicConfig() {
  epdCmd(0x12); epdWait();                  // software reset
  epdCmd(0x01); epdDat(0xC7); epdDat(0x00); epdDat(0x01);  // driver output: 200 gates
  epdCmd(0x11); epdDat(0x01);               // data entry: Y decrement, X increment
  epdCmd(0x44); epdDat(0x00); epdDat(0x18); // RAM X range: bytes 0..24
  epdCmd(0x45); epdDat(0xC7); epdDat(0x00); // RAM Y range: 199..0
  epdDat(0x00); epdDat(0x00);
}

static void epdSetCursor() {
  epdCmd(0x4E); epdDat(0x00);               // RAM X cursor = 0
  epdCmd(0x4F); epdDat(0xC7); epdDat(0x00); // RAM Y cursor = 199
}

static void epdLoadPartialLut() {
  epdCmd(0x32);
  for (int i = 0; i < 153; i++) epdDat(WF_PARTIAL[i]);
  epdWait();
  epdCmd(0x3F); epdDat(WF_PARTIAL[153]);
  epdCmd(0x03); epdDat(WF_PARTIAL[154]);                          // gate voltage
  epdCmd(0x04); epdDat(WF_PARTIAL[155]); epdDat(WF_PARTIAL[156]); // source voltage
  epdDat(WF_PARTIAL[157]);
  epdCmd(0x2C); epdDat(WF_PARTIAL[158]);                          // VCOM
}

// --- public API -------------------------------------------------------------

void epdSetup() {
  pinMode(DIN_PIN, OUTPUT);
  pinMode(CLK_PIN, OUTPUT); digitalWrite(CLK_PIN, LOW);   // clock idle low
  pinMode(CS_PIN,  OUTPUT); digitalWrite(CS_PIN,  HIGH);
  pinMode(DC_PIN,  OUTPUT);
  pinMode(RST_PIN, OUTPUT); digitalWrite(RST_PIN, HIGH);
  pinMode(BUSY_PIN, INPUT);
}

void epdInitFull() {
  epdHwReset();
  epdBasicConfig();
  epdCmd(0x3C); epdDat(0x01);               // border waveform (full)
  epdCmd(0x18); epdDat(0x80);               // temperature: internal sensor
  epdCmd(0x22); epdDat(0xB1);               // load temperature + full waveform from OTP
  epdCmd(0x20); epdWait();
}

void epdInitPartial() {
  epdHwReset();
  epdBasicConfig();
  epdLoadPartialLut();
  epdCmd(0x37);                             // gate/source config for partial
  epdDat(0x00); epdDat(0x00); epdDat(0x00); epdDat(0x00); epdDat(0x00);
  epdDat(0x40); epdDat(0x00); epdDat(0x00); epdDat(0x00); epdDat(0x00);
  epdCmd(0x3C); epdDat(0x80);               // border waveform (partial)
  epdCmd(0x22); epdDat(0xC0);               // power on analog/clock for partial
  epdCmd(0x20); epdWait();
}

// Stream a full 200x200 image into one RAM bank, rotated 180 degrees so text
// reads upright: rows bottom-to-top, each row mirrored horizontally (byte order
// reversed + bits within each byte reversed). 200 px == exactly 25 bytes, so
// the mirror is exact with no pixel offset.
void epdWriteRam(uint8_t ramCmd, EpdRowProvider rows) {
  epdSetCursor();                           // reset to window origin first
  epdCmd(ramCmd);
  digitalWrite(DC_PIN, HIGH);
  digitalWrite(CS_PIN, LOW);
  for (int outRow = 0; outRow < 200; outRow++) {
    uint8_t rowBytes[25];
    rows(199 - outRow, rowBytes);
    for (int ob = 0; ob < 25; ob++) spiByte(reverseBits(rowBytes[24 - ob]));
  }
  digitalWrite(CS_PIN, HIGH);
}

void epdRefreshFull() {
  epdCmd(0x22); epdDat(0xF7);               // full update sequence
  epdCmd(0x20); epdWait();
}

void epdRefreshPartial() {
  epdCmd(0x22); epdDat(0xCF);               // partial update sequence
  epdCmd(0x20); epdWait();
}

void epdDeepSleep() {
  epdCmd(0x10); epdDat(0x01);               // deep sleep; image held at 0 power
}
