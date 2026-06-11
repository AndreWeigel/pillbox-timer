# Pillbox Timer

A battery-powered pillbox that records when you last opened it on a small
e-ink display in the lid. Runs on a CR2032 coin cell for 3–5 years.

## Status
Phase 1 (breadboard): toolchain proven, blink working. Reed switch & e-paper next.

## Hardware
- Microchip ATtiny1616 (SOIC-20)
- Waveshare 1.54" e-Paper, 200×200, SSD1681
- Reed switch + neodymium magnet
- CR2032 + holder
- Adafruit UPDI Friend (programming)

## Development
See `docs/arduino-ide-setup.md` for required Arduino IDE settings.
See `docs/01-phase1-toolchain.md` for what was learned getting the toolchain working.
