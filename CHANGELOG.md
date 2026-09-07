# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Fixed

- **Navigation HDG showed magnetic heading instead of true.** `NavigationUI` read
  `getHeadingMagDeg()` while COG on the same screen is true, so the two differed by the
  local variation (~11 deg easterly) and read as a crab angle that was not there. The
  `HeadingDelta` payload carries both fields; `getHeadingTrueDeg()` existed but was unused
  anywhere in the tree. The README's Navigation section was also missing HDG entirely.

### Added

- **Prevent-sleep switch on the Setup screen** (`ui_SwitchPreventSleep`). Keeps the watch
  awake on battery the same way it stays awake while docked on USB — no display timeout,
  panel dimmed to 5 % after 60 s of no input — with two deliberate limits: the 120 s awake
  ceiling stays armed (only USB lifts it), and the low-charge ladder overrides the switch
  from `CRITICAL` down without rewriting the stored setting, so it resumes by itself once
  the cell recovers. Persisted to NVS (`"display"` / `"nosleep"`) the instant it is flipped,
  since `onLeave()` never runs on the way into deep sleep. Red knob = off, green = on.
  Costs ~4.7 mAh per wake (~11x a normal glance) while on, so it is a "keep it awake while
  I am using it" control rather than a set-and-forget setting.

### Changed

- **The Setup screen is now touch-only.** The knob's ADJUSTING capture mode is gone with the
  state machine behind it (`SetupState`, `onButtonPress`, `onRotation`, `interceptsRotation`,
  the 3 s autosave): brightness is set by dragging the slider and prevent sleep by tapping the
  switch. Rotating the knob now switches pages on the Setup screen like on every other screen
  instead of being captured, and a knob press does nothing there. The hidden SquareLine value
  labels are no longer written — the slider position and the switch colour show the state.
- `IScreenUI::interceptsTouchAt()` is no longer `const`: a page with several controls also
  records which one a touch claimed, since the drag/end pair that follows cannot identify it.
- `USB_DIM_IDLE_MS` / `USB_DIM_PERCENT` renamed to `STAYAWAKE_DIM_IDLE_MS` /
  `STAYAWAKE_DIM_PERCENT` — they now govern the battery-side prevent-sleep mode too, and the
  `USB_` prefix would mislead. `STAY_AWAKE_ON_USB` keeps its name, being genuinely USB-specific.
- **Display-on window lengthened from 8 s to 10 s** (`DISPLAY_TIMEOUT_MS`). Costs ~0.08 mAh
  per glance; endurance on the 250 mAh cell goes from ~4.0/3.5/2.9 days to ~3.9/3.2/2.7 days
  at 30/60/100 glances per day, i.e. still 2.7-4.8x the 24 h + nightly-charge requirement.
  The low-charge ladder's shortened windows (5 s / 3 s) are unchanged.
- **The last-active screen now survives every reset, not just a deep-sleep wake.** Two
  independent causes were fixed: the restore was gated on the wake cause being
  `ESP_SLEEP_WAKEUP_EXT0` (a reset reports `UNDEFINED`), and the index lived in
  `RTC_DATA_ATTR` i.e. `.rtc.data`, which the bootloader reloads from the flash image on
  every boot except a deep-sleep wake. The store is now two-tier: `RTC_NOINIT_ATTR` plus a
  magic word (free; survives deep sleep, software reset, watchdog and panic) with an NVS
  backstop (survives a power-on, a flat cell or a disconnected battery). NVS is read only
  when the RTC tier is gone and written only when the page changed, once per sleep entry —
  so the common deep-sleep wake path touches no flash and the wake sequence is unchanged.
  `PowerManager` gained a pre-sleep hook for the flush, placed before the backlight ramp
  where the radio is idle and no render is in flight. Boot now logs `esp_reset_reason()`
  and which tier supplied the page.

## [1.0.0] - 2026-09-04

First release. Running on hardware on M/V Frida, with the power budget measured and closed.

### Added

**Application and architecture**

- `CrowPanelApplication` orchestrator owning the Arduino_GFX SPI display stack (GC9309), the ESP-NOW receiver, seven screen adapters, both input devices, the screen carousel, the power owner and the battery monitor.
- `IScreenUI` abstraction for screen adapters, deliberately input-source-agnostic so the knob and the touch panel drive the same seam.
- `ScreenManager` carousel: all screens are containers on one LVGL screen, toggled via `LV_OBJ_FLAG_HIDDEN`.
- Architecture ported from the always-on companion project [ESP32-Crowpanel-compass](https://github.com/mkvesala/ESP32-Crowpanel-compass), with the sleep/wake layer added.

**Screens**

- **Navigation** — position, COG, SOG and depth below surface. Position-fix validity and course validity are kept distinct, so position and SOG stay visible at anchor where COG is undefined.
- **Batteries** — house bank voltage, current and state-of-charge, plus starter battery voltage.
- **Weather** — temperature, air pressure, relative humidity.
- **Engine** — exhaust temperature, fuel and fresh-water tank litres (M/V Frida: fuel 400 L, water 80 L).
- **Attitude** — artificial horizon with pitch and roll.
- **Clock** — local date and weekday, local and UTC time, with a previous/next-day indicator when the UTC calendar day differs.
- **Setup** — backlight brightness with NVS persistence.
- Always-visible top status bar showing the watch's own LiPo charge.

**ESP-NOW reception**

- Receive-only `ESPNowReceiver` for the shared fleet protocol (`espnow_protocol.h`), handling `HeadingDelta`, `BatteryDelta`, `WeatherDelta`, `GnssDelta`, `HALMETEngineDelta`, `HALMETTankDelta`, `HALMETWaterDelta`, `DepthDelta` and `DateTimeDelta`.
- Per-source RX timestamps so each value's freshness is tracked independently.
- **Freshness rule: stale data is never shown as if current.** Every wake starts with placeholders (`---` / `--.-`) that are replaced only when a fresh broadcast lands; depth additionally honours the payload's own `age_ms`, so a dead sounder feed cannot freeze a depth reading on screen.

**Timekeeping**

- ESP32-internal POSIX time disciplined by GNSS UTC over ESP-NOW (`DateTimeDelta` → `settimeofday()`), Finland timezone with DST. Survives deep sleep on the RTC timer.
- Placeholders until synced — a fabricated time is never displayed. No external RTC chip and no NTP (there is no internet at anchor).

**Input**

- `TouchInput` over a local AXS5106L driver: software tap / swipe-up / swipe-down recognition, plus absolute slider dragging. The interrupt-driven driver's event stream is debounced into a stable "held" state so swipes and drags do not break up.
- `RotaryEncoder` with a full-step Buxton quadrature decoder (one event per detent, contact-bounce immune) and a push button.
- Both devices are fully parallel; the encoder's rotation is redundant and the encoder can be removed later, leaving only its push button as the wake source.
- The last-active screen is stored in RTC memory and restored on a button-wake.

**Power management (Model B — deep sleep, wake-to-fetch)**

- `PowerManager` owning the display-on timeout, peripheral gating with GPIO holds on sleep entry, and ext0 button wake arming.
- Staggered wake sequence that spreads the inrush in time — holds release, rails and panel init with the panel dark, radio up first into the dark low-load moment, ~200 ms dark ESP-NOW listen window, render, and only then a PWM soft-start of the backlight. The brownout detector stays enabled.
- `BatteryMonitor` reading the GPIO1 battery ADC, EMA-smoothing it, estimating state-of-charge from a LiPo resting curve, and driving the status-bar indicator green / amber / red, or blue while on USB power.
- **Absolute awake ceiling (120 s)** that user activity cannot extend, so a stuck input source cannot hold the board at ~145 mA until the cell is flat.
- **Task watchdog (30 s)** on the Arduino loop task, covering a hung loop that the awake ceiling cannot catch.
- **Stuck wake-button guard.** Deep-sleep `ext0` is level-triggered, so a button held down at sleep entry would wake the watch instantly and repeatedly. The wake pin is read first and, when held, a 30 s timer is armed instead; the recheck runs before any rail, panel, UI or radio bring-up.
- **Low-charge ladder** shortening the display timeout and clamping the backlight as the cell empties, ending in a protective deep sleep behind a `BATTERY EMPTY / CHARGE NOW` prompt — evaluated before the radio is brought up on a dying cell, and self-recovering on charge.
- **Stays awake while on USB power**, dimming the panel after a minute of no input instead of sleeping, so the watch works as a docked display.

**Documentation**

- `README.md` with architecture, per-screen documentation, the measured consumption and endurance tables, and the ESP-NOW protocol.
- `hardware_notes.md` as the authoritative pin map, build/toolchain notes and power-guard reference.
- Issue and pull-request templates, contribution guide, and third-party licence notes.

[1.0.0]: https://github.com/mkvesala/ESP32-Crowpanel-SkippersWatch/releases/tag/v1.0.0
