# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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
- **Attitude** — artificial horizon with pitch and roll, plus bow-up/down and port/starboard extremes (which reset on each wake, since a wake is a full reboot).
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
