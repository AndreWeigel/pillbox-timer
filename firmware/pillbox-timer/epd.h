// Low-level SSD1681 e-paper driver (Waveshare 1.54" V2, 200x200), bit-banged SPI.
// This layer is pure mechanism: it knows nothing about fonts, text, or layout.
// The caller supplies pixel rows through an EpdRowProvider, and the driver
// handles the 180-degree rotation this panel needs (flex cable at the bottom).
//
// Two update paths:
//   full    - clean, but flashes black/white several times (clears ghosting)
//   partial - no flash, fast; diffs new image (0x24) against old image (0x26)
//
// Wiring (megaTinyCore pin): DIN 1, CLK 3, CS 4, DC 5, RST 6, BUSY 7.
#pragma once
#include <Arduino.h>

// Fills dst[0..24] (25 bytes = 200 px; MSB = leftmost; bit 1 = white, 0 = black)
// for the given upright row 0..199. The driver rotates as it streams.
typedef void (*EpdRowProvider)(int row, uint8_t* dst);

void epdSetup(void);          // configure the panel's pins (call once, in setup)

void epdInitFull(void);       // wake + init for a full (flashing) refresh
void epdInitPartial(void);    // wake + init for a partial (no-flash) refresh

void epdWriteRam(uint8_t ramCmd, EpdRowProvider rows);  // 0x24 = new, 0x26 = old

void epdRefreshFull(void);    // run the full update sequence (visible flash)
void epdRefreshPartial(void); // run the partial update sequence (no flash)

void epdDeepSleep(void);      // panel deep sleep; image is held with no power
