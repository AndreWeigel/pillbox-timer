# Hardware Notes

## Adafruit ATtiny1616 Breakout (5690)

- Ships with **seesaw firmware** — needs to be overwritten by your own sketch on first upload.
- **UPDI pad is on the bottom** of the board, not labeled on top. The `16` pin on the top
  silkscreen is GPIO (PA3), not UPDI.
- The top silkscreen uses seesaw pin numbers (0–16), which are NOT megaTinyCore pin numbers
  and NOT physical chip pin numbers. Always consult the megaTinyCore ATtiny1616 pinout diagram.

## Adafruit UPDI Friend (5879)

- Has a **3V/5V slide switch**. Chip works at either voltage (rated 1.8–5.5V).
  Port showed up in Arduino IDE after switching to 5V — worth toggling if port doesn't appear.
- Programs reliably at **230400 baud** with `SerialUPDI` programmer in megaTinyCore.

## FT232RL — why it failed

The generic FT232RL USB-C adapter has activity LEDs on its RX line. The ATtiny1616's
UPDI/reset pin has an extremely weak output driver (doubles as HV programming pin).
The LED + resistor loads the line enough that the chip can't pull it low, so UPDI
communication fails entirely.

**Symptom:** loopback test passes, but UPDI gives `Can't read CS register. likely wiring error.`

**Reference:** https://github.com/SpenceKonde/megaTinyCore/discussions/713

Fix options: desolder the RX LED, or use a programmer without RX LEDs (UPDI Friend).

### FT232RL pin map (for reference)
| Pin | What it actually is |
|---|---|
| TXD | Real TX data output — this is what you connect to UPDI |
| RXD | Real RX data input |
| TXL / RXL | LED activity indicators ONLY — not data |
| DTR, RTS, CTS, etc. | Handshaking signals — not data |
