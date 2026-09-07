# CrowPanel 2.01" Watch — Hardware Notes (V1.0)

Authoritative pin map + part list for the Elecrow CrowPanel 2.01" HMI ESP32-S3
Watch Display (240×296). Confirmed from Elecrow's official repo
`Elecrow-RD/CrowPanel_2.01_inch-HMI_ESP32_Watch_Display_240_296`
(`example/V1.0/Arduino/…`, `Datasheet/`, `Eagle_SCH&PCB/`). This is the hardware
half of the power/architecture plan — the display init concern is fully resolved
because Arduino_GFX ships a built-in `Arduino_GC9309` class (no manual init
table, unlike the verrokki's RGB ST7701).

## SoC / memory
- **ESP32-S3R8**, dual LX7 @ up to 240 MHz, **8 MB PSRAM**, **16 MB NOR flash**
  (ZD25Q128). RTC-capable GPIOs = **GPIO0–21** (matters for deep-sleep wake).

## Display — GC9309, SPI (Arduino_GFX)
| Signal | GPIO | Notes |
|---|---|---|
| CS | 9 | |
| DC | 10 | |
| SCK | 7 | |
| MOSI | 8 | |
| MISO | -1 | write-only |
| RST | 6 | |
| Backlight | **13** | LEDC PWM (verrokki used GPIO6 — different here) |
| TE (tearing) | 21 | |
| LCD power enable | **40** | **ACTIVE-LOW**: `digitalWrite(40, LOW)` = rail ON |
| Display power-state sense | 14 | input |
- Resolution **240×296** portrait, **RGB565**, SPI **80 MHz**.
- Driver stack (from Lesson03):
  ```cpp
  displayBus = new Arduino_ESP32SPIDMA(PIN_LCD_DC/*10*/, PIN_LCD_CS/*9*/,
                                       PIN_LCD_SCK/*7*/, PIN_LCD_MOSI/*8*/, PIN_LCD_MISO/*-1*/);
  displayGfx = new Arduino_GC9309(displayBus, PIN_LCD_RST/*6*/, /*rotation*/0,
                                  /*IPS*/false, 240, 296, 0,0,0,0);
  displayGfx->begin(80000000);   // 80 MHz
  ```
- LVGL 9.1, `LV_DISPLAY_RENDER_MODE_PARTIAL`, example uses a **40-line** draw
  buffer (`240*40*2 = 19.2 KB`) in internal RAM. Good small-buffer starting point
  for the watch (render latency = power).

## I2C bus (shared) — SCL=3, SDA=4
- **Touch AXS5106L**: INT=**2**, RST=**5**. Local driver `axs5106l_touch.*`.
- **Accelerometer SC7A20H** (LIS2DH-class): I2C addr **0x19**. Local driver
  `sc7a20h.*`. Lesson09 **polls** magnitude; **no INT GPIO wired in the example**
  — for Model C wrist-raise wake, confirm the INT routing from
  `Eagle_SCH&PCB/V1.0/ESP32 WATCH_V1.0.pdf` before relying on accel ext-wake.

## Input
- **Rotary encoder** (ALPS EC11): A=**20**, B=**19**, **button=18** (active-low,
  internal pull-up). Example uses `EncoderTool` + `OneButton`.
  → **GPIO18 is RTC-capable → valid ext0/ext1 / GPIO deep-sleep wake** (this is
  the Model B wake source).
- **Boot button**: GPIO**0** (active-low). RTC-capable (secondary wake option).

## Power / battery
- **Peripheral power rail enable: GPIO47** — must be driven **HIGH** to power the
  battery divider, vibration motor, and shared peripheral circuits. Gate LOW on
  deep sleep for anything on this rail you don't need.
- **Battery sense ADC: GPIO1**. Divider **100 kΩ (upper) / 137 kΩ (lower)** →
  ratio `137/(100+137) = 0.578`; `V_batt = V_adc / 0.578`. Read with
  `analogReadMilliVolts()`, 12-bit, `ADC_11db`. Sample away from spikes; needs
  GPIO47 HIGH.
- **Charge status: GPIO15** (digital input).
- **USB power detect: GPIO17** (HIGH = USB present) — suspends the endurance logic
  while charging. Read by `BatteryMonitor::usbPresent()`; `PowerManager` uses it to
  keep the watch awake on the charger (`STAY_AWAKE_ON_USB`) and to disable both the
  display timeout and the awake ceiling. GPIO15 (charge status) is logged only —
  its polarity is undocumented in Lesson11.
- **Vibration motor: GPIO45** (needs GPIO47 rail HIGH). Optional haptic.

## Audio (deferred — default off)
- MEMS mic **LMD3526** (I2S). Speaker + codec present. Keep unpowered by default.

## Deep-sleep wake summary (Model B)
- Primary wake = **encoder button GPIO18**, active-low → wake on LOW. GPIO18 is in
  the RTC domain, so `ext1`/`esp_deep_sleep_enable_gpio_wakeup` both work.
- Note Lesson15 demonstrates **light sleep + `gpio_wakeup_enable`** (touch INT
  GPIO2, USB GPIO17, display-state GPIO14) — useful reference for the Model C
  light-listen path, but Model B uses deep sleep + button.
- On sleep entry: LCD power (40) OFF = drive HIGH (active-low), backlight (13) 0,
  peripheral rail (47) LOW, then GPIO-hold the output levels.
- **ext0 is LEVEL-triggered.** If GPIO18 is already LOW at `esp_deep_sleep_start()`
  (a button held down in a pocket), the SoC wakes immediately and loops at ~145 mA —
  the 250 mAh cell would be flat in under two hours. `PowerManager::armWakeSource()`
  therefore reads the pin first and, when it is held, arms a **30 s timer** instead;
  `handleTimerRecheck()` then re-reads it at the very top of `app.begin()` and sleeps
  straight back with no rail/panel/UI/radio bring-up (~0.003 mAh per check).

## Libraries (from example/V1.0)
`Arduino_GFX_Library` (GC9309), `EncoderTool`, `OneButton`, `Bounce2`, local
`axs5106l_touch`, local `sc7a20h`, `lvgl` 9.1.

## Build requirements (Arduino IDE / arduino-cli)
- **Board:** ESP32-S3 (Arduino-ESP32 core 3.x — needs `ledcAttach`/`ledcWrite`,
  `ledcRead`, `ledcDetach`). Enable **PSRAM** (OPI), Flash 16 MB, QIO 80 MHz.
- **USB / serial: this board uses a CH34x USB-UART bridge on UART0, NOT the S3
  native USB.** The USB-C connector enumerates as WCH VID `0x1a86` →
  `/dev/cu.wchusbserial*` (macOS needs the WCH CH34x driver installed — without
  it NO port appears even though the S3 is running). Consequences:
  - **`USB CDC On Boot` = Disabled** — so `Serial` maps to UART0→CH34x and the
    Serial Monitor works. (Enabled would route `Serial` to the unused native USB
    and the monitor stays blank.)
  - Upload uses the bridge's DTR/RTS auto-reset → **no manual BOOT+RESET needed**;
    Arduino IDE auto-enters download mode. Fallback on "Failed to connect": hold
    **BOOT (GPIO0)** → tap **RESET** → release BOOT → Upload.
  - The CH34x port **persists regardless of firmware deep-sleep** (bridge is
    independent of the S3), unlike a native-USB-CDC board whose port drops on
    sleep. The factory firmware sleeps ~3 s after boot (LED+screen off) — that is
    normal, not a fault.
- **Arduino_GFX_Library** ("GFX Library for Arduino") with the **`Arduino_GC9309`**
  driver class. Mainline releases (e.g. 1.6.7) do NOT include GC9309 — Elecrow
  added it. This install was patched by dropping Elecrow's
  `src/display/Arduino_GC9309.{h,cpp}` into the installed library and adding
  `#include "display/Arduino_GC9309.h"` to `src/Arduino_GFX_Library.h`. **If the
  GFX library is ever updated/reinstalled, re-apply this patch** (source:
  Elecrow repo `example/V1.0/Arduino/libraries/GFX_Library_for_Arduino`). The
  driver derives from `Arduino_TFT` and is signature-compatible with 1.6.7. The
  verrokki shares this library but doesn't use GC9309, so it is unaffected.
- **LVGL 9.x** with an `lv_conf.h` where `LV_COLOR_DEPTH 16`. The app calls
  `lv_tick_set_cb(millis)` (no `LV_TICK_CUSTOM` needed). If colors look
  byte-swapped, enable the `lv_draw_sw_rgb565_swap` line noted in
  `CrowPanelApplication.cpp::lvglFlushCb`.
- SquareLine export (`ui_*.c/.h`, fonts) is compiled as-is; do not hand-edit
  (regeneration overwrites).

## Runtime behavior (Model B, M1)
- On boot/wake the UI comes up with every value `---`, fills in as ESP-NOW
  packets arrive, and after `PowerManager::DISPLAY_TIMEOUT_MS` (10 s) of no knob
  activity the watch soft-ramps the backlight down and enters **deep sleep**.
- **Press the encoder button (GPIO18) to wake.** Rotating the knob switches pages
  on every page including Setup; the knob no longer has any Setup-specific mode.
- **The Setup page is touch-only:** drag `ui_SliderBrightness` for backlight, tap
  `ui_SwitchPreventSleep` to keep the watch awake on battery. Both persist to NVS
  (`"display"` / `"brightness"`, `"nosleep"`).

### RTC memory across reset causes
`RTC_DATA_ATTR` (`.rtc.data`) is **not** enough to carry state across a reset: the
bootloader reloads that section from the flash image on every boot **except** a
deep-sleep wake, where it deliberately skips the initialisation. Use
`RTC_NOINIT_ATTR` (`.rtc.noinit`, not in the image, never initialised) plus a magic
word for anything that must also survive the reset button, a watchdog trip or a panic
— see `CrowPanelApplication::resolveStartPage()`.

**Open (measure on HW):** the reset button is presumed wired to **EN (CHIP_PU)**, which
power-cycles the chip internally; whether the RTC domain keeps its contents across that
is board-specific and not documented by Elecrow. The boot log
`[page] reset=<esp_reset_reason> src=rtc|nvs` answers it: tap RESET and read the source.
`src=rtc` → EN reset preserves RTC RAM here; `src=nvs` → it wipes it, and the NVS tier is
load-bearing. Either way the page is restored — this is diagnostics, not a dependency.

### Power guards (M1 hardening)
The measured budget is comfortable (2.16 mA sleep floor, ~145 mA active, 250 mAh cell,
24 h + nightly charge), so what remains is failure modes rather than averages. All four
guards live in `PowerManager`; the numbers come from the M1 meter session.

| Guard | Constant | Value | Protects against |
|---|---|---|---|
| Awake ceiling | `MAX_AWAKE_MS` | 120 s | A stuck input source calling `notifyActivity()` forever (~145 mA until flat) |
| Task watchdog | `WDT_TIMEOUT_MS` | 30 s | A hung `loop()`, which the ceiling cannot catch; a trip resets → cold boot |
| Stuck wake button | `STUCK_BUTTON_RECHECK_US` | 30 s | Level-triggered `ext0` re-waking instantly on a held button |
| Low-charge ladder | see below | — | Running the cell down to its own protection cutoff |

Low-charge ladder — `BatteryMonitor::Level` classifies (one-way escalation within an
awake session; forced to `OK` on USB), `PowerManager` acts:

| Level | Trigger | Display timeout | Backlight ceiling |
|---|---|---|---|
| `OK` | SoC > 20 % | 10 s | 100 % |
| `WARN` | SoC ≤ 20 % | 5 s | 100 % (bar is already red) |
| `CRITICAL` | SoC ≤ 10 % | 3 s | 10 % |
| `CUTOFF` | SoC ≤ 5 % **or** cell ≤ 3400 mV | — | 5 %, `BATTERY EMPTY / CHARGE NOW` for 2 s → protective sleep |

The **timeout** is the lever, not the clamp: active baseline is ~145 mA and the backlight
adder at 20 % is only ~10 mA, so 10 s → 3 s saves far more than any brightness cap. The
cutoff path deliberately runs *before* the radio is brought up (RF bring-up is the
sharpest current edge on the board) and keeps the button wake armed, so a press only
repeats the short prompt cycle and plugging in USB restores a normal boot.

On USB (GPIO17 HIGH) the watch **does not sleep**: it stays up as a docked display and
dims the panel to `STAYAWAKE_DIM_PERCENT` (5 %) after `STAYAWAKE_DIM_IDLE_MS` (60 s) of no
input. Unplugging grants a fresh 10 s window and restarts the awake ceiling. The clamp is
applied through `SetupUI::setBrightnessCeiling()`, which never overwrites the user's stored
brightness — only what reaches the LEDC pin.

The Setup **prevent-sleep** switch (`SetupUI::preventSleep()` → `PowerManager::setPreventSleep()`)
is the same stay-awake behaviour on battery, with two deliberate limits: `MAX_AWAKE_MS`
stays armed (only USB lifts the ceiling), and `preventSleepActive()` returns false from
`Level::CRITICAL` down, so the ladder wins on a low cell. The stored setting is never
rewritten — it resumes on its own once the cell recovers. Constants are shared with the USB
path, which is why they are named `STAYAWAKE_*` and not `USB_*`.

## Sources
Elecrow official repo `CrowPanel_2.01_inch-HMI_ESP32_Watch_Display_240_296`:
`example/V1.0/Arduino/Lesson03_Display_Touch_LVGL/config.h` (display/touch pins),
`Lesson04` (encoder), `Lesson11` (battery), `Lesson15` (sleep), `Lesson09`
(accel), `Lesson10` (motor); `Datasheet/GC9309NA Datasheet V1.0.pdf`.
