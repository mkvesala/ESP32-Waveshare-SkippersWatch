# Porting SkippersWatch: CrowPanel 2.01" → Waveshare ESP32-S3-Touch-AMOLED-2.06

Working plan for the port. Written 2026-10-04, before any code was changed and before
the new board was on the bench. Hardware facts and their verification status are in
`hardware_notes_waveshare.md`; the CrowPanel baseline is in `hardware_notes.md`.

This file is a planning aid for the port branch. Delete or archive it when the port
lands on `main`.

## Branches and tags

| Ref | Purpose |
|---|---|
| tag `v1.0-crowpanel` | Frozen last CrowPanel state |
| branch `crowpanel-2.01` | CrowPanel maintenance (fixes only) |
| branch `waveshare-amoled-2.06` | Port work |
| `main` | Stays CrowPanel until the port works, then fast-forwards to the port |

Create the tag and both branches from the current `main` **before** the first port
commit. Fixes that apply to both boards: commit once, `git cherry-pick` to the other.

## Claude skills for this work

| Skill | Use for |
|---|---|
| `skippers-watch-power` | Power model, staggered wake, guards, freshness, budget method. Board-independent. Read first. |
| `waveshare-amoled-watch` | Waveshare pins, parts, library calls, what replaces what |
| `crowpanel-watch-power` | The CrowPanel "before" picture; `crowpanel-2.01` branch fixes |

## What does not change

- `espnow_protocol.h`, `ESPNowReceiver` (receive-only, per-source freshness)
- `ScreenManager`, `IScreenUI` touch seam, page order
- Data logic of `NavigationUI`, `BatteryUI`, `WeatherUI`, `EngineUI`, `AttitudeUI`,
  `ClockUI`, `SetupUI`
- `TouchInput`'s software gesture recognition (tap, swipe up/down)
- `PowerManager` **policy**: Model B, display timeout, 120 s awake ceiling, 30 s
  watchdog, stuck-wake guard, low-charge ladder, stay-awake on external power
- Never-stale rule, placeholders, ~200 ms dark listen window
- Two-tier last-page restore (RTC noinit + NVS)

## What changes, class by class

| File / class | CrowPanel | Waveshare | Notes |
|---|---|---|---|
| `CrowPanelApplication` | pin map, `Arduino_ESP32SPIDMA` + `Arduino_GC9309`, 240×296 | `Arduino_ESP32QSPI` + `Arduino_CO5300`, 410×502, col offset 22 | Class and repo name contain "CrowPanel" — decide on a board-neutral name |
| `initPowerRails()` | GPIO40 (active-low LCD rail), GPIO47 (peripheral rail) | AXP2101 rails via XPowersLib | Rail-to-load map must be traced first |
| `initBacklight()` + LEDC | PWM on GPIO13 | `gfx->setBrightness()` | No LEDC; ramp is brightness steps |
| `initLvgl()` | 40-line partial buffer, 19.2 kB | 40 lines = 32.8 kB, plus the even-coordinate rounder | Direct mode is the vendor default; compare |
| `BatteryMonitor` | ADC GPIO1 + divider + LiPo curve, USB GPIO17, charge GPIO15 | AXP2101 voltage / percent / VBUS / charging | Keep interface and `Level` classification |
| `PowerManager` mechanism | GPIO holds, ext0 on GPIO18 (active-low), backlight ramp | Rail gating via PMIC, wake on GPIO10 (active-high) and/or GPIO0, brightness ramp | Policy constants unchanged until measured |
| `RotaryEncoder` | EC11 on GPIO20/19/18 | **removed** — no knob on the board | Touch already covers all UI actions |
| `TouchInput` | `axs5106l_touch` driver, INT=2 RST=5 | FT3168 at 0x38, INT=38 RST=9 | Swap the chip driver; keep gesture layer |
| `axs5106l_touch.*` | local driver | removed | Replaced by an FT3168 driver |
| `SetupUI` | slider drives LEDC through a ceiling | slider drives `setBrightness()` through the same ceiling | Knob adjust state machine becomes dead code |
| `IScreenUI` | knob + touch methods | touch methods | Knob methods can stay unused or be removed |
| SquareLine project | 240×296 | new project at 410×502 | Scale ≈1.7× in both axes; regenerate fonts |
| `lv_conf.h` | LVGL 9.1 era, `LV_COLOR_DEPTH 16` | check against LVGL version in use | Keep project-local |
| `Serial` | UART0 via CH34x, CDC on boot disabled | native USB, CDC on boot enabled | Port drops in deep sleep |
| `README.md`, `hardware_notes.md`, `CHANGELOG.md` | describe CrowPanel | rewrite for Waveshare on the port branch | CrowPanel docs stay on `crowpanel-2.01` |

## Suggested order of work

Each step ends with something observable. Do not start the next step on top of an
unverified one.

1. **Branch and tag.** Tag, create both branches, switch to the port branch.
2. **Vendor examples on the bare board.** Flash Waveshare's `01_HelloWorld` and
   `05_LVGL_AXP2101_ADC_Data` unchanged. Confirms toolchain, USB CDC, display,
   touch and PMIC before any project code is involved.
3. **Bench measurements, round 1.** Deep-sleep floor and active current with a minimal
   sketch (see `hardware_notes_waveshare.md`, "To measure first"). These decide whether
   Model B still closes the budget and which lever matters on AMOLED.
4. **Display + LVGL (M0, no sleep).** New display stack, rounder, draw buffer; a
   placeholder screen at 410×502.
5. **SquareLine UI at 410×502.** New project, export, wire the seven adapters.
6. **Touch.** FT3168 driver under `TouchInput`; gestures unchanged.
7. **ESP-NOW.** Should run as is; confirm all sources and freshness placeholders.
8. **BatteryMonitor on AXP2101.** Percent, voltage, VBUS, charging; status bar.
9. **PowerManager (M1).** Rail gating, wake on PWR button, stuck-wake guard with the
   new polarity, brightness ramp, staggered wake order.
10. **Bench measurements, round 2.** Floor, active, endurance table; retune timeout
    and ladder constants only if the numbers call for it.
11. **Docs and merge.** README, hardware notes, CHANGELOG; fast-forward `main`.

## Open items

Decisions for Masa:
- [ ] Board-neutral names for the repo, the sketch folder / `.ino` and
      `CrowPanelApplication`
- [ ] Wake button: PWR only, or PWR + BOOT
- [ ] Remove the knob methods from `IScreenUI` / `SetupUI`, or leave them unused
- [ ] Resting state: ESP32 deep sleep, or AXP2101 `shutdown()` (decide after measuring)
- [ ] Battery to use (the board's own cell, or another capacity)

To verify on hardware (all marked [M] in `hardware_notes_waveshare.md`):
- [ ] Rail-to-load map of the AXP2101 from the schematic
- [ ] Installed Arduino_GFX contains `Arduino_CO5300`
- [ ] `ext0` + `ext1` combination for opposite-polarity buttons
- [ ] Deep-sleep floor, active current, `shutdown()` floor
- [ ] AXP2101 gauge accuracy over a full discharge
- [ ] Reset reason and RTC-memory survival per button
- [ ] Partial vs. direct LVGL render mode over QSPI
- [ ] I2C addresses of AXP2101, QMI8658, PCF85063 (from library constants or a scan)

## Carry-over lessons from the CrowPanel build

- ext0/ext1 are level-triggered: read the wake pin before arming, or a held button
  loops the watch awake at active current.
- The reset button power-cycled the RTC domain there. Test, don't assume.
- Measure on battery only; a handheld DMM's mA ranges brown the board out — use a
  series shunt for the sleep floor and the 10 A jack for active current.
- SquareLine clears its export directory. Commit first, export to a temp directory.
- Keep `lv_conf.h` project-local and check for a second copy under
  `Arduino/libraries/`.
- The app partition was 99 % full with the default scheme; choose a large app slot.
