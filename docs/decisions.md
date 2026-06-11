# Design Decisions

## Why ATtiny1616?
- Deep sleep current ~1µA with RTC running — essential for CR2032 longevity.
- Built-in 32kHz internal oscillator for the RTC (no external crystal needed).
- Hardware SPI peripheral for e-paper communication.
- Pin-change interrupts on all pins (needed for reed switch wake).
- 16KB Flash — enough for e-paper driver + font rendering.
- Small SOIC-20 package fits a compact PCB.

## Why no external RTC chip (e.g. DS3231)?
- The ATtiny1616's internal RTC is accurate enough for a "last opened" display.
  Exact time doesn't need to be perfect — ±1 minute/day is fine.
- Eliminating the RTC chip saves board space, cost, and quiescent current.
- Tradeoff: time drifts if battery dies and needs to be re-synced manually.

## Why CR2032?
- ~230mAh capacity, 3V nominal — matches the chip's operating range directly (no regulator).
- Extremely common, cheap, available everywhere.
- Small enough to fit inside the pillbox lid/base.
- At ~0.041mAh/day estimated draw, gives 3–5 year life.

## Why e-ink (not OLED/LCD)?
- Zero power draw to hold an image — the display only draws current during refresh.
- Visible in all lighting conditions including direct sunlight.
- Tradeoff: slow refresh (~0.3s partial, ~2s full), and full refreshes cause visible flash.

## Why reed switch (not button)?
- Zero power consumption when closed (magnet holds it).
- No mechanical wear.
- Naturally triggered by lid motion without any user action required.
- Tradeoff: requires placing a magnet in the lid at the right position.
