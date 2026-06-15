# Pillbox Timer

A battery-powered pillbox that answers one question at a glance:
**"Have I taken my meds — and am I due?"**

No phone. No app. No network. Just look at the lid and the answer is there, drawn on a 1.54" e-ink display in the lid, running on a CR2032 coin cell for an estimated 3–5 years.

---

## The idea

The obvious approach is a clock: timestamp every lid-open, compare to the current time, show the result. But a real-time clock needs either a crystal + battery backup (more parts, more power, more to fail) or network sync (way out of scope for a thing you put pills in). So we dropped it entirely.

Instead the device uses **interval-based scheduling**: it doesn't know what time it is, only how many seconds have passed since the lid was last opened. That's enough. If you take medication every 24 hours, "is it due?" is just `elapsed > 20h`. No clock required.

The three dose states are:

| State | Condition | Meaning |
|---|---|---|
| **DONE** | `elapsed < 20h` | Taken, nothing to do yet |
| **TAKE** | `20h ≤ elapsed < 28h` | Due — go take them |
| **OVERDUE** | `elapsed ≥ 28h` | Late — how many hours late is shown |

---

## Hardware

| Part | Why |
|---|---|
| ATtiny1616 (SOIC-20) | Tiny, cheap, deep-sleep at ~1µA, megaTinyCore gives Arduino compatibility |
| Waveshare 1.54" e-paper (SSD1681, 200×200) | Visible in any light with zero power draw when idle — the image stays without current |
| Reed switch + neodymium magnet | Passive lid detection. No button, no moving contacts exposed to the user |
| CR2032 + holder | Small, common, 225mAh capacity — achievable 3–5 year life at these duty cycles |
| Adafruit UPDI Friend | One-wire programming over USB — no bootloader needed |

---

## How it got built — phase by phase

### Phase 1 — Does the toolchain work?

First commit: blink an LED. Boring but necessary — megaTinyCore has quirks (pin numbers ≠ port numbers), UPDI programming needs specific settings, and the 5 MHz internal oscillator needed to be configured before the chip could reliably talk to the programmer. Once the LED blinked, the chain was proven: editor → compile → flash → chip.

See `firmware/01-blink`.

---

### Phase 2 — Reed switch input

A simple test: LED on when the magnet is close (switch closed, pin LOW), off when the lid opens. Confirmed that the ATtiny1616's internal pull-up is enough — no external resistor needed.

The switch is **N/O (normally open)**. Lid closed = magnet present = switch closed = pin LOW. Lid open = magnet leaves = pin floats HIGH. The RISING edge is the wake signal.

See `firmware/02-reed-switch`.

---

### Phase 3 — Deep sleep + wake on reed

The ATtiny1616 has a POWER-DOWN mode that draws ~1µA. The reed switch is wired to pin PA6 (pin 2 in megaTinyCore numbering) — one of the fully asynchronous pins that can wake the chip from POWER-DOWN on a pin-change interrupt, without any clocks running.

Result: the chip sleeps indefinitely, wakes the instant the lid opens, does its work, goes back to sleep.

See `firmware/03-sleep-wake`.

---

### Phase 4 — E-ink display driver

**The first big gotcha:** hardware SPI on the ATtiny1616 lives on PA1/PA3 — which are pins 14 and 16 in megaTinyCore. The display was wired to pins 1 and 3 (which are PB3/PB0 — not hardware SPI at all). Telling Arduino to use SPI delivered nothing.

**Fix:** bit-banged SPI. A simple `spiByte()` function toggles the clock and data pins manually. Slower, but the SSD1681 doesn't need speed — it runs at 2 MHz max and only receives during setup and frame writes.

The SSD1681 init sequence also had several subtle gotchas (wrong data entry mode, wrong RAM Y range order, wrong cursor position) that produced a blank display until cross-referenced against Waveshare's reference driver.

See `firmware/04-eink`.

---

### Phase 5 — Integration: time tracking + display

The chip can't keep time in POWER-DOWN (the CPU is off). The solution is the **RTC Periodic Interrupt Timer (PIT)**: a hardware counter clocked from the internal 1.024 kHz oscillator that fires an interrupt even in POWER-DOWN. We use the maximum hardware period — 32,768 cycles = **32 seconds** — which means ~2,700 wakes per day instead of 86,400 at 1-second resolution. That's a ~32× reduction in wake energy, and 32-second resolution is plenty for dose state and "X hours ago" display.

Two display modes were added behind a flag:

- **Dose status** — the three-state model described above
- **Last opened** — passive log, shows JUST NOW / 7H AGO / 2D AGO

---

### Phase 6 — Layout: big text, pill icon, subtitle

The display layout became: large headline at top, a two-tone capsule pill icon in the middle, small subtitle near the bottom. The pill is drawn **procedurally** — a point-in-capsule test per pixel, per row, during streaming. No lookup table, no stored shape.

**The SRAM problem:** a full 200×200 framebuffer is 5,000 bytes. The ATtiny1616 has 2KB of SRAM. Storing the whole frame isn't possible.

**The solution:** band buffers. The headline band (28px tall × 200px wide = 700 bytes) and subtitle band (14px tall × 200px wide = 350 bytes) are pre-rendered into RAM. The pill is drawn on-the-fly. Everything else streams as white. Peak RAM for a single render pass: ~1,050 bytes — well within budget.

The 180° rotation (display mounted upside-down) is handled during streaming: rows are emitted bottom-to-top, and within each row the byte order is reversed and the bits within each byte are mirrored.

---

### Phase 7 — Partial refresh (no more flicker)

The default SSD1681 full refresh cycles the display through several black/white flashes before settling — visible and ugly on a device meant to sit quietly in a pillbox lid.

**Partial refresh** skips the erase cycles: it diffs the new image against the previous one and only updates changed pixels. The result is a near-instant, flash-free update — the same mechanism that makes Kindle page turns smooth.

The catch: the controller needs the *previous* image in RAM (`0x26`) to diff against. We can't store a full framebuffer. **The fix:** we store the two text strings and the dose state from the last render. When a partial update is needed, we re-run the row provider for the old content into `0x26`, then the new content into `0x24`, then trigger the partial refresh sequence with a 159-byte LUT from Waveshare's reference driver.

A **full refresh runs every 20 updates** to clear accumulated ghosting. The flash is brief and infrequent enough to not be jarring.

The firmware was split into three layers at this point:
- `epd.*` — SSD1681 hardware driver (SPI, init, RAM write, refresh commands)
- `display.*` — layout, font, refresh policy
- `pillbox-timer.ino` — dose logic, timekeeping, sleep/wake

---

### Phase 8 — Three display styles

A single `DISPLAY_STYLE` flag in `config.h` selects the look:

| Style | Description |
|---|---|
| `STYLE_PLAIN` | Big caps headline + pill icon + subtitle |
| `STYLE_FUN` | Same layout, cheeky wording (CHILL / PILL TIME / UGHHH) |
| `STYLE_PIXEL` | Pixel-art face (happy / worried) or pill, + subtitle |

The pixel-art faces are drawn **procedurally** — two square eyes at fixed positions, and a parabolic mouth curve computed with integer arithmetic per row. Happy = smile, overdue = frown. No bitmaps stored, no extra RAM used.

The app layer (`pillbox-timer.ino`) picks the wording; the display layer picks the visual. A `DoseState` enum (`STATE_DONE`, `STATE_TAKE`, `STATE_OVERDUE`, `STATE_INFO`) bridges the two without either layer knowing the other's internals.

---

### Phase 9 — Config file

All tunable constants moved to `config.h` — a single file to open when you want to change anything:

```c
// Pins
#define LED_PIN  0
#define REED_PIN 2
#define EPD_DIN_PIN  1   // ... and 5 more EPD pins

// Display
#define DISPLAY_STYLE    STYLE_PIXEL
#define FULL_REFRESH_EVERY 20

// App
#define DEFAULT_MODE MODE_DOSE_STATUS
#define TEST_MODE 1        // shrinks thresholds to seconds for bench testing

// Dose schedule
#define DOSE_DONE_SEC    72000UL   // 20 h
#define DOSE_OVERDUE_SEC 100800UL  // 28 h
#define PIT_TICK_SEC 32
```

---

## Power budget (estimated)

| State | Current | Time/day |
|---|---|---|
| POWER-DOWN sleep | ~1 µA | ~23h 59m |
| PIT wake (compute + display) | ~5 mA | ~2,700 × ~10ms |
| Reed wake (lid open) | ~5 mA | negligible |

Rough average: ~2–3 µA → CR2032 (225 mAh) → **3–5 years**.

---

## Repo layout

```
firmware/
  01-blink/           Phase 1: toolchain validation
  02-reed-switch/     Phase 2: reed switch input test
  03-sleep-wake/      Phase 3: deep sleep + wake on reed interrupt
  04-eink/            Phase 4: SSD1681 driver + wiring debug
  pillbox-timer/      Phase 5+: integrated firmware (active)
    config.h          ← all tunable constants live here
    style.h           display style defines + DoseState enum
    epd.*             SSD1681 hardware driver
    display.*         layout, font, refresh policy
    pillbox-timer.ino app: dose logic, timekeeping, sleep/wake
hardware/             schematics (Phase 2, in progress)
docs/                 setup guides, decision notes
```

---

## Development setup

See `docs/arduino-ide-setup.md` for Arduino IDE / megaTinyCore settings and UPDI programmer configuration.

---

## License

MIT — see [LICENSE](LICENSE).
