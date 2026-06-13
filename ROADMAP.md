# Roadmap

## Vision

A battery-powered pillbox that helps someone answer one question at a glance:
**"have I taken my meds, and am I due?"** — not just "when did I last open the box."
The device should do the interpreting, not the person.

Constraint that shapes everything: the hardware has no wall-clock time reference
(no RTC sync, no buttons, no network — see `docs/decisions.md`). So scheduling is
**interval-based** ("take every N hours"), computed from elapsed time since the
last lid-open, which the firmware already tracks.

## Display modes

Two modes, selected by a flag today (`DEFAULT_MODE` in `firmware/pillbox-timer`),
by a physical button later:

- **Dose status** — three states from time since last open: `DONE` (within the
  done window), `TAKE` (due), `OVERDUE` (well past due), each a big headline plus
  a small subtitle. Interval-based, so no wall-clock time is needed.
- **Last opened** — passive log; shows `JUST NOW` / `<t> AGO`.

## Status

- [x] Phase 1: toolchain, blink (`firmware/01-blink`)
- [x] Reed switch input (`firmware/02-reed-switch`)
- [x] Deep sleep + wake on reed interrupt (`firmware/03-sleep-wake`)
- [x] SSD1681 e-paper driver, bit-banged SPI (`firmware/04-eink`)
- [x] Integration: reed wake + RTC PIT timekeeping + 5x7 text (`firmware/pillbox-timer`)
- [x] Fix display orientation (180° rotation)
- [x] Dual display mode (dose status + last opened) behind a flag
- [x] Dose status as DONE / TAKE / OVERDUE states with headline + subtitle

## Backlog

### Firmware
- [ ] Subtitle in lowercase instead of small caps, if the softer look is wanted
- [ ] Coarsen refresh cadence for battery — last-opened mode currently refreshes
      every minute for the first hour (~10x over the 0.041 mAh/day budget).
      Dose mode is naturally coarse (changes hourly).
- [ ] Mode-select button: toggle dose/last-opened at runtime (needs input pin)
- [ ] Set dosing interval without reflashing (EEPROM + button, or UPDI config)
- [ ] Low-battery indication on the display
- [ ] Debounce / ignore spurious reed events (e.g. lid jiggle)

### Hardware (Phase 2)
- [ ] KiCad schematic: bare ATtiny1616, CR2032 holder, reed switch, e-paper FPC
- [ ] Board layout + fab
- [ ] Measure real deep-sleep current on bare chip (impossible on the Adafruit
      breakout — its power LED swamps the ~1µA sleep draw)
- [ ] Validate 3–5 year battery life estimate

### Mechanical
- [ ] Mount display in the pillbox lid
- [ ] Position magnet + reed switch for reliable lid-open detection
- [ ] Enclosure / battery access

## Open questions

- **Who reads the screen** — the patient, or a caregiver checking on someone?
  The latter pushes toward connectivity (big scope change).
- **"Opened" ≠ "took the dose"** — with one reed switch this proxy is the best we
  have. Acceptable, or worth more sensing?
- **How is the dosing interval set** — hardcoded at flash time is trivial;
  user-adjustable needs input hardware we don't have yet.
