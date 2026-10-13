# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [2.0.0] - Unreleased

Port to the **Waveshare ESP32-S3-Touch-AMOLED-2.06** (SKU 31957): 410×502 AMOLED, larger
battery, factory case with an integrated strap. Breaking: the firmware no longer runs on the
Elecrow CrowPanel 2.01". The CrowPanel build stays available as `v1.0.0` and on the
`legacy/crowpanel` branch. Phase 1 is an "as-is" port: same screens, same data, same power
policy. The only new UI feature is the page menu.

### Added

- **Page menu** (`MenuUI` over SquareLine's `ui_ContainerMenu`): swipe down on any screen
  to open a 3×3 grid of image buttons, one per screen; tap an icon to jump straight to that
  screen, or swipe up to close the menu and return to the screen you were on. The menu is
  an overlay, not a carousel page, so it is never stored as the last-active screen and a
  wake always opens a screen.
- `PanelBrightness`: AMOLED brightness as a panel command (CO5300 `0x51`, 0..255), shared
  by `SetupUI` (ramp up, slider, ceiling) and `PowerManager` (ramp down).
- `ft3168_touch`: small local FT3168 driver (I2C 0x38, RST=GPIO9), polled on the main loop;
  hibernated before deep sleep.
- AXP2101 PMIC support via **XPowersLib** in `BatteryMonitor`: battery voltage, the PMIC's
  fuel-gauge percentage (LiPo resting curve as fallback, both logged for comparison), VBUS
  presence for the USB stay-awake rule.
- `GestureClassifier.h` and `BatteryLevel.h`: the gesture rules and the low-charge ladder
  as pure, Arduino-free code, with host unit tests in `tests/host` (`make -C tests/host`).

### Changed

- **Hardware platform.** Display stack is `Arduino_ESP32QSPI` + `Arduino_CO5300`
  (410×502, column offset 22) with the panel's even/odd area rounder; LVGL partial render
  with a 40-line internal-RAM buffer (32 800 B). No tearing-effect sync in this phase.
- **Navigation is horizontal.** Swipe left = next screen, swipe right = previous screen
  (was swipe up / swipe down). Swipe down / up now open / close the page menu. Gesture
  thresholds scaled to the new resolution (tap ≤ 27 px, swipe ≥ 48 px).
- **Wake button is the PWR key** (GPIO10 = SYS_OUT, **active-high**, `ext0` level 1), was
  the encoder button (GPIO18, active-low). The stuck-wake-button guard follows the new
  polarity. Touch cannot wake from deep sleep on this board (FT3168 INT is GPIO38, not an
  RTC pin).
- **Sleep entry** puts the panel to sleep (display off + sleep-in) and the touch controller
  into hibernate, holds the panel/touch reset lines high and the speaker amp (GPIO46) off.
  The AXP2101 rails are deliberately left untouched until the rail-to-load map is traced.
- `CrowPanelApplication` renamed to **`WatchApplication`**. The repository, sketch folder
  and `.ino` keep their names.
- `IScreenUI` is touch-only: `onRotation()` and `interceptsRotation()` removed.
- `ScreenManager` gained `switchToIndex()`, `hideCurrent()` and `screenCount()`; the
  CW/CCW direction parameter is gone.
- `LV_MEM_SIZE` 64 kB → 96 kB.
- Arduino IDE settings: **USB CDC On Boot: Enabled** (native USB, was a CH34x bridge),
  **Flash Size: 32MB**, Partition Scheme Huge APP. `Serial.setTxTimeoutMs(0)` so a missing
  USB host never blocks the wake path.
- Power **policy** unchanged (10 s display timeout, 120 s awake ceiling, 30 s task watchdog,
  stuck-button guard, low-charge ladder, USB stay-awake, prevent sleep). Its constants were
  tuned on CrowPanel measurements; this board's sleep floor and active current are still to
  be measured.

### Removed

- `RotaryEncoder` and the AXS5106L driver (`axs5106l_touch`): the board has no knob and a
  different touch controller.
- CrowPanel power mechanisms: LEDC backlight PWM (GPIO13), LCD rail (GPIO40), peripheral
  rail (GPIO47), battery ADC (GPIO1), USB-detect (GPIO17) and charge-status (GPIO15) pins.

## [1.0.0] - 2026-10-04

First release, on the **Elecrow CrowPanel 2.01" Watch Display**. Running on hardware on M/V Frida, with the power budget measured and closed. Maintained on the `legacy/crowpanel` branch.

### Added

**Application and architecture**

- `CrowPanelApplication` orchestrator owning the Arduino_GFX SPI display stack (GC9309), the ESP-NOW receiver, seven screen adapters, both input devices, the screen carousel, the power owner and the battery monitor.
- `IScreenUI` abstraction for screen adapters, deliberately input-source-agnostic so the knob and the touch panel drive the same seam.
- `ScreenManager` carousel: all screens are containers on one LVGL screen, toggled via `LV_OBJ_FLAG_HIDDEN`.
- Architecture ported from the always-on companion project [ESP32-Crowpanel-compass](https://github.com/mkvesala/ESP32-Crowpanel-compass), with the sleep/wake layer added.

**Screens**

- **Navigation** — position, true heading (HDG), COG, SOG and depth below surface. HDG is the true heading so it is directly comparable with the true COG beside it. Position-fix validity and course validity are kept distinct, so position and SOG stay visible at anchor where COG is undefined.
- **Batteries** — house bank voltage, current and state-of-charge, plus starter battery voltage.
- **Weather** — temperature, air pressure, relative humidity.
- **Engine** — exhaust temperature, fuel and fresh-water tank litres (M/V Frida: fuel 400 L, water 80 L).
- **Attitude** — artificial horizon with pitch and roll.
- **Clock** — local date and weekday, local and UTC time, with a previous/next-day indicator when the UTC calendar day differs.
- **Setup** — touch-only: a backlight brightness slider and a **prevent-sleep switch**, both persisted to NVS. Prevent sleep keeps the watch awake on battery like it does on USB (no display timeout, dimmed to 5 % after 60 s idle), but the 120 s awake ceiling stays armed and the low-charge ladder overrides it from `CRITICAL` down without rewriting the stored setting.
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
- **The last-active screen survives every reset**, not only a deep-sleep wake. Two tiers: `RTC_NOINIT_ATTR` plus a magic word (free; survives deep sleep, software reset, watchdog and panic) with an NVS backstop written once at sleep entry and only when the page changed (survives power-on, a flat cell or a removed battery). Boot logs the reset reason and which tier supplied the page; that log showed the CrowPanel reset button power-cycles the RTC domain, making the NVS tier load-bearing.

**Power management (Model B — deep sleep, wake-to-fetch)**

- `PowerManager` owning the display-on timeout (10 s), peripheral gating with GPIO holds on sleep entry, and ext0 button wake arming.
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

[2.0.0]: https://github.com/mkvesala/ESP32-Crowpanel-SkippersWatch/compare/v1.0.0...v2.0.0
[1.0.0]: https://github.com/mkvesala/ESP32-Crowpanel-SkippersWatch/releases/tag/v1.0.0
