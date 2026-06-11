# Phase 1 Toolchain Prove-Out

## What was accomplished
- Arduino IDE + megaTinyCore + avr-gcc compiles for ATtiny1616
- UPDI Friend programs the chip reliably at 230400 baud
- GPIO output works: LED on pin 0 blinks at 1Hz

## Board settings that work

| Setting | Value |
|---|---|
| Board | megaTinyCore → `ATtiny3216/1616/1606/816/806/416/406` (no-Optiboot) |
| Chip | `ATtiny1616` |
| Clock | `5 MHz internal tuned` |
| Programmer | `SerialUPDI - 230400 baud` |
| Port | UPDI Friend's port (`/dev/cu.usbserial-XXXX`) |

**FQBN:** `megaTinyCore:megaavr:atxy6:chip=1616,clock=5internaltuned`

## Hard-won learnings

See `hardware-notes.md` for UPDI Friend quirks and the FT232 LED-on-RX failure.

1. **Apple Silicon needs Rosetta 2** — megaTinyCore's avr-gcc is x86_64 only.
   `softwareupdate --install-rosetta --agree-to-license`

2. **Two board variants in the menu** — pick the one WITHOUT "w/Optiboot".
   The Optiboot variant fails on a blank chip with `stk500_getsync() not in sync`.

3. **Chip selection resets when you change Board** — always re-verify `Tools → Chip`
   is set to `ATtiny1616` after switching anything in the Board menu.

4. **UPDI pad on the Adafruit 5690 is on the BACK of the board** — not labeled on top.
   The `16` pin on the top silkscreen is PA3 (GPIO), not UPDI.
