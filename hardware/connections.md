# Wiring reference

Use this alongside the megaTinyCore ATtiny1616 pinout diagram to draw the schematic.
Arduino pin numbers (from `config.h`) need to be mapped to physical chip pins using:
https://github.com/SpenceKonde/megaTinyCore/blob/master/megaavr/extras/ATtiny_x16.md

## ATtiny1616 connections

| Arduino pin | Port pin | Physical pin (SOIC-20) | Connected to        | Notes                        |
|-------------|----------|------------------------|---------------------|------------------------------|
| 0           | PA4      | 2                      | (unused)            | Debug LED on breadboard only |
| 1           | PA5      | 3                      | EPD DIN (MOSI)      | Bit-banged SPI data          |
| 2           | PA6      | 4                      | Reed switch → GND   | INPUT_PULLUP, RISING edge wake |
| 3           | PA7      | 5                      | EPD CLK (SCK)       | Bit-banged SPI clock         |
| 4           | PB5      | 6                      | EPD CS              | Active LOW chip select       |
| 5           | PB4      | 7                      | EPD DC              | Data/Command select          |
| 6           | PB3      | 8                      | EPD RST             | Active LOW reset             |
| 7           | PB2      | 9                      | EPD BUSY            | HIGH = panel busy            |
| —           | PB1      | 10                     | (unused)            | No-connect                   |
| —           | PB0      | 11                     | (unused)            | No-connect                   |
| VDD         | —        | 1                      | CR2032 +            | 3V supply                    |
| GND         | —        | 20                     | CR2032 −            | Common ground                |

> **Note:** pins 1/3 are used as bit-banged SPI, NOT hardware SPI.
> Hardware SPI on ATtiny1616 lives on PA1/PA3 (Arduino pins 14/16) — those are unused.

## EPD (Waveshare 1.54" SSD1681) connector

| EPD pin | Signal | Connected to       |
|---------|--------|--------------------|
| VCC     | 3.3V   | ATtiny1616 VDD     |
| GND     | GND    | Common ground      |
| DIN     | MOSI   | Arduino pin 1      |
| CLK     | SCK    | Arduino pin 3      |
| CS      | CS     | Arduino pin 4      |
| DC      | DC     | Arduino pin 5      |
| RST     | RST    | Arduino pin 6      |
| BUSY    | BUSY   | Arduino pin 7      |

## Reed switch

N/O (normally open) glass reed switch:
- One leg → Arduino pin 2 (INPUT_PULLUP)
- Other leg → GND
- Neodymium magnet mounted on lid body, switch on lid edge

When the magnet is close (lid closed): switch closes, pin pulled LOW.
When the lid opens: magnet leaves, pin floats HIGH via internal pull-up → RISING edge wakes MCU.

## Power

- CR2032 coin cell (3V, ~225mAh)
- 100nF decoupling capacitor between VDD and GND, placed close to MCU
- No regulator needed — ATtiny1616 runs 1.8–5.5V, display runs 3.3V
