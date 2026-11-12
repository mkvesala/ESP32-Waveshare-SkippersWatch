# Waveshare ESP32-S3-Touch-AMOLED-2.06 — Hardware Notes (port target)

Pin map, part list and build notes for the Waveshare ESP32-S3-Touch-AMOLED-2.06
watch board (SKU 31957), the port target for SkippersWatch. Companion to
`hardware_notes.md` (CrowPanel 2.01", the v1.0.0 board).

Written 2026-10-04 from vendor documentation and example code, **before the board
was on the bench**. Nothing here has been measured or run on hardware yet. Each fact
is marked:

- **[V]** verified against a vendor source (listed under Sources)
- **[D]** derived from a verified fact plus ESP32-S3 behaviour; not yet tried
- **[M]** must be measured or checked on the board

Update the marks as the port progresses.

## SoC / memory
- **ESP32-S3R8**, dual LX7, **8 MB OPI PSRAM**, **32 MB flash**. [V]
- RTC-capable GPIOs = **GPIO0–21** (same SoC as CrowPanel; matters for deep-sleep wake). [D]
- Wi-Fi 2.4 GHz + Bluetooth LE; ESP-NOW rides the Wi-Fi PHY as before. [V]

## Display — CO5300 AMOLED, QSPI (Arduino_GFX)
| Signal | GPIO | Notes |
|---|---|---|
| QSPI SIO0 | 4 | |
| QSPI SIO1 | 5 | |
| QSPI SIO2 | 6 | |
| QSPI SIO3 | 7 | |
| QSPI SCL | 11 | |
| CS | 12 | |
| RESET | 8 | |
| TE (tearing) | 13 | |

- Resolution **410×502** portrait, 16.7M colours, RGB565 from LVGL. [V]
- **No backlight pin, no LCD power-enable GPIO.** Brightness is a panel command. [V]
- Driver stack (from Waveshare `01_HelloWorld` and LVGL examples) [V]:
  ```cpp
  Arduino_DataBus *bus = new Arduino_ESP32QSPI(
      12 /*CS*/, 11 /*SCK*/, 4 /*SIO0*/, 5 /*SIO1*/, 6 /*SIO2*/, 7 /*SIO3*/);
  Arduino_GFX *gfx = new Arduino_CO5300(bus, 8 /*RST*/, 0 /*rotation*/,
                                        410, 502,
                                        22 /*col_offset1*/, 0, 0, 0);
  ```
  The **column offset 22** is part of the vendor init.
- `Arduino_CO5300` provides `setBrightness(uint8_t)`, `displayOn()`, `displayOff()`. [V]
  It is included in the GFX library Waveshare ships (1.6.0) — no vendor patch as with
  GC9309. Confirm the locally installed GFX (1.6.7, GC9309-patched) contains
  `display/Arduino_CO5300.h`. [M]
- Vendor LVGL examples (LVGL 9.3.0) register a **rounder** on
  `LV_EVENT_INVALIDATE_AREA` that aligns invalidated areas to even start / odd end
  coordinates. Keep it. [V]
- Vendor examples run `LV_DISPLAY_RENDER_MODE_DIRECT` with a full-screen buffer
  (410×502×2 = 411 640 B, PSRAM). A 40-line partial buffer as on CrowPanel is
  410×40×2 = 32 800 B. Which is faster over QSPI: [M]
- AMOLED: per-pixel emission, so draw depends on lit pixel area and brightness, and
  black is near zero. Use true black backgrounds. Current vs. brightness: [M]

## I2C bus (shared) — SCL=14, SDA=15 [V]
| Device | Function | Address | Extra lines |
|---|---|---|---|
| FT3168 | capacitive touch | 0x38 [V] | INT=**38**, RST=**9** |
| AXP2101 | PMIC, charger, gauge, power key | [M] | — |
| QMI8658 | 6-axis IMU | [M] | INT1=**21** |
| PCF85063 | RTC | [M] | INT=**39** |
| ES8311 | audio codec | [M] | I2S |
| ES7210 | mic ADC | [M] | I2S |

- Touch driver in vendor examples: `Arduino_FT3x68` from `Arduino_DriveBus`, with
  power modes ACTIVE / MONITOR / STANDBY / HIBERNATE. [V]
- IMU and RTC drivers in vendor examples: `SensorQMI8658`, `SensorPCF85063`
  (SensorLib). [V]
- Addresses marked [M] are defined inside the vendor libraries; read them from the
  library constants or run an I2C scan rather than typing them from memory.

## Input
- **PWR button**: state readable on **GPIO10** (`SYS_OUT`), **high = pressed**. Also
  wired to the AXP2101 power key: press powers the board on when off; long press
  (about 6 s) powers it off in hardware. [V]
- **BOOT button**: **GPIO0**, low = pressed, strapping pin. [V]
- **No rotary encoder.** No vibration motor is listed. [V]

## Power / battery — AXP2101 (XPowersLib)
- Battery: 3.7 V LiPo on an **MX1.25** connector. [V] Fitted cell capacity: [M]
- Battery, USB and charge state come from the PMIC over I2C — there is no battery
  ADC pin, no USB-detect GPIO, no charge-status GPIO. [V]
- XPowersLib functions present in the shipped library [V]: `getBattVoltage()`,
  `getBatteryPercent()`, `isBatteryConnect()`, `isCharging()`, `isVbusIn()`,
  `getVbusVoltage()`, `getSystemVoltage()`, `enableALDO1..4()/disableALDO1..4()`,
  `enableBLDO1..2()/disableBLDO1..2()`, `enableDC1..5()/disableDC1..5()`,
  `enableSleep()`, `shutdown()`, `setPowerKeyPressOffTime()`,
  `setLowBatShutdownThreshold()`, `setSysPowerDownVoltage()`,
  `isPekeyShortPressIrq()`, `isPekeyLongPressIrq()`, `clearIrqStatus()`.
- Rail table printed on the schematic [V]:

  | Rail | Label |
  |---|---|
  | DCDC1 | VCC3V3 (main 3.3 V) |
  | DCDC2 | 0.9 V |
  | DCDC3 | 1.2 V |
  | DCDC4 | 1.8 V |
  | DCDC5 | NC |
  | ALDO1 | VL1 3.3 V |
  | ALDO2 | VL2 3.3 V |
  | ALDO3 | VCC3V |
  | ALDO4 | VL3 1.8 V |
  | BLDO2 | VL 2.8 V |
  | CPUSLDO | 1.2 V |

- **Rail-to-load map** traced on the schematic V1.0 (2026-10-31) [V]:

  | Rail | Loads | Firmware (`PmuRails`) |
  |---|---|---|
  | DCDC1 = VCC3V3 | ESP32-S3, flash, AMOLED VCI/VDDIO (J3 pins 23/25/29/30), touch, QMI8658, ES8311 PVDD/DVDD, NS4150B, SD card + 10k pull-ups, I2C 2.2k pull-ups, BOOT/PWR pull-ups | never touched |
  | ALDO1 = A3V3 | ES7210 (all supplies), ES8311 AVDD | left on; codecs powered down by register |
  | ALDO2 | only `DSI_PWR_EN` (R10 10k pull-up) = the AMOLED module's own supply enable | stays on: cutting it in sleep measured +30–40 mA |
  | ALDO3 | vibration-motor pad P1 (0R); no motor fitted | off |
  | ALDO4, BLDO1/2, CPUSLDO, DLDO1/2 | no load (BLDOIN, CPUSLDO, DLDO pins unconnected) | off |
  | DCDC2–4 | VIN/LX pins unconnected; on after reset (0x80 = 0F) | off |
  | DCDC5 | shares the `Vsys/DCDC5` node | not touched |
  | RTCLDO = VCC-RTC | PCF85063 | — |

  Cutting DCDC1 would cut the SoC and the I2C pull-ups, leaving no way back short of a
  PMIC power cycle (PWR held ~6 s).
- `AXP_IRQ` goes to VCC-RTC through a 10k pull-up and to an "EXIO5" label, **not** to an
  ESP32 GPIO. It cannot wake the SoC. [V]
- **PWR key circuit** [V]: PWRON (AXP2101) → 510R → key to GND; PWRON also drives the
  gate of T1 (BSS138) through 1k. At rest T1 conducts and pulls SYS_OUT (GPIO10) low
  against R11 (10k to VCC3V3), so R11 draws **~0.33 mA continuously** — a fixed part of
  the sleep floor that firmware cannot remove. On a press T1 opens and R11 pulls GPIO10
  high. The line is actively driven both ways, so no internal pull is needed for wake.
- Battery TS pin has a 10k thermistor to GND (RP2).

## Audio (unused — default off)
- ES8311 codec, ES7210 dual-mic ADC, NS4150B amplifier. [V]
- I2S: MCLK=16, SCLK=41, LRCK=45, DSDIN=40 (to ES8311), ASDOUT=42 (from ES7210). [V]
- Amplifier enable `PA_CTRL` = **GPIO46**. Keep low. [V]

## Other pins [V]
- Native USB: D−=19, D+=20.
- UART0: TX=43, RX=44.

## TF card
- Pins [V]: GPIO1 = MOSI / CMD, GPIO2 = SCK / CLK, GPIO3 = MISO / D0, GPIO17 = CS / D3.
  D3 only stays pulled up in 1-bit SD mode. Used by the Chart screen (`ChartStore`).
- **Bus** [measured, 2026-11-04]: one 376 KB chart view from a 32 GB SDHC, mean of 3 rounds,
  USB on, demo mode. The firmware uses SD_MMC 1-bit at 40 MHz, with 20 MHz as the fallback if
  the mount fails, and reads directly to PSRAM. A 32 KB DMA buffer would save only 11 ms.

  | Mode | Mount | Via 32 KB DMA buffer | Direct to PSRAM |
  |---|---|---|---|
  | SD_MMC 1-bit 40 MHz | 47 ms | 141 ms | 152 ms |
  | SD_MMC 1-bit 20 MHz | 42 ms | 232 ms | 243 ms |
  | SPI3 20 MHz | 146 ms | 297 ms | 308 ms |
  | SPI3 40 MHz | 46 ms | 297 ms | 308 ms |

  SPI "40 MHz" timed exactly like 20 MHz, so the clock apparently did not rise. Its mount is
  faster because the card was already in SPI mode. A card left in SPI mode needs re-inserting
  before SD_MMC works again. The display holds SPI2, so SPI would have needed SPI3.
- **Filesystem** [V]: FAT32 with an MBR. The core's FatFs is built with `FF_FS_EXFAT 0`.
- **Power:** the card and its 10k pull-ups are on DCDC1, so firmware cannot cut them. Drain
  from the gauge, deep sleep, with the same card:

  | Card state | Drain |
  |---|---|
  | Fitted, never initialised since it got power | **42.1 mA** / 8 h (98 → 11 %, 3.500 V) |
  | One mount and close, then sleep | 8.4 mA / 3 h (100 → 92 %, 4.013 V) |
  | Mount and close before sleep, chart page not visited | 9.4 mA / 4 h (100 → 90 %, 3.995 V) |

  The fitted, uninitialised card is the main suspect for the 42 mA, but there was no run of the
  same firmware without a card. The two short readings include the gauge's fast first hour and
  ±1 mA of gauge step, so the card's own share against the ~6 mA floor needs a reading of 10 h
  or more.
- **Before every sleep** `ChartEngine` mounts and closes the card, also on a wake that never
  opened the chart, because a card can be put in unnoticed. Measured time: 58 ms without a card,
  47 ms with an unopened card, 1 ms when the chart page already mounted it. Afterwards the four
  pins are left as inputs with no hold.

## Deep-sleep wake summary (Model B)
| Source | GPIO | RTC-capable | Polarity | Use |
|---|---|---|---|---|
| PWR button | 10 | yes [D] | high = pressed | **in use** (ext1 ANY_HIGH), replaces encoder button GPIO18 |
| BOOT button | 0 | yes [D] | low = pressed | secondary |
| IMU INT1 | 21 | yes [D] | set in QMI8658 | wrist-raise, if ever wanted |
| Timer | — | — | — | stuck-button recheck |
| Touch INT | 38 | **no** | — | light sleep only |
| RTC INT | 39 | **no** | — | light sleep only |

- GPIO10 is **active-high**, the opposite of CrowPanel's GPIO18. `armWakeSource()`,
  the stuck-button pre-read and `wokeFromButton()` all change polarity. [D]
- BOOT and PWR have opposite polarity; deep-sleep `ext1` applies one level to all its
  pins. Using both means `ext0` for one and `ext1` for the other. [M]
- Both buttons are level-triggered wake sources, so the stuck-wake guard still
  applies. [D]
- Alternative resting state: AXP2101 `shutdown()` (full power-off, PWR button cold
  boots). No timer wake, RTC memory lost. Floor current vs. deep sleep: [M]

## Libraries (as shipped in the vendor repo, `examples/arduino/libraries`) [V]
| Library | Version in repo |
|---|---|
| GFX Library for Arduino | 1.6.0 |
| lvgl | 9.3.0 |
| XPowersLib | 0.3.0 (Waveshare's docs page says 0.2.6) |
| SensorLib | 0.3.1 |
| Arduino_DriveBus | 1.0.1 |

`pin_config.h` (in `libraries/Mylibrary/`) defines the display, touch, I2C and SD pins
used above and `XPOWERS_CHIP_AXP2101`.

## Build requirements (Arduino IDE / arduino-cli) [V]
- Arduino core `esp32` by Espressif **3.2.0 or newer**; board **ESP32S3 Dev Module**.
- **USB CDC On Boot: Enabled** (native USB; vendor examples use `HWCDC USBSerial`).
  This is the opposite of the CrowPanel setting.
- **PSRAM: OPI PSRAM**, **Flash Size: 32MB**, partition scheme with a large app slot.
- Download mode fallback: hold **BOOT**, reconnect USB, upload.
- Native USB serial port **drops while the SoC is in deep sleep** (unlike the CH34x
  bridge on CrowPanel). Flashing a sleeping watch needs the BOOT fallback; early
  boot logs may be lost — UART0 (GPIO43/44) is the fallback log channel. [D]
- Waveshare warns not to mix in LVGL 8.4.0 from its other boards' examples.

### As used by this project (port branch, 2026-10-13) [V: compiles]
| Setting | Value |
|---|---|
| Board | ESP32S3 Dev Module, core esp32 3.3.12 |
| USB CDC On Boot | Enabled |
| USB Mode | Hardware CDC and JTAG |
| Flash Size | 32MB (256Mb) |
| Partition Scheme | Huge APP (3MB No OTA/1MB SPIFFS) |
| PSRAM | OPI PSRAM (not used on the wake path) |
| Flash Mode | QIO 80MHz |

Libraries: GFX Library for Arduino 1.6.9 (mainline, contains `Arduino_CO5300`; 1.6.7
GC9309-patched also worked), LVGL 9.6.0 with the project-local `lv_conf.h` (v9.6.0 template),
**XPowersLib 0.3.3** (added for the port). No Arduino_DriveBus: the FT3168 is driven by
the project's own `ft3168_touch.*`. Sketch: ~1.98 MB of the 3 MB app slot (v2.2.0).

CLI equivalent (the arduino-cli bundled with the IDE):
`arduino-cli compile --fqbn esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=32M,PartitionScheme=huge_app,PSRAM=opi .`

## Deep-sleep floor

First field test (2026-10-29…31, firmware before `PmuRails`): fully charged 400 mAh
cell, demo mode, 1–2 wakes, < 1 min awake. Gauge 100 → 54 % (3.750 V) in 25 h and
→ 23 % (3.683 V) in 36 h: **~7–8.5 mA average deep-sleep draw** [measured, from the
gauge]. Target ≤ 2 mA (~200 h on 400 mAh).

Measures added in v2.1.0, bisected with temporary switches (removed for the release).
Results from the Status screen's **Avg drain** row on battery (2026-10-31…11-03):

| Test | Switches | Drain |
|---|---|---|
| all on | ALDO2 cut + pads + rails + codecs + ext1 | **36.0 mA** / 10 h |
| A | rails + codecs + ext1 | 7.9 mA / 8 h — no gain over baseline |
| B | A + ALDO2 cut | **49.6 mA** / 4 h |
| C | A + pads held | ~8.4 mA / 14 h — no gain |
| D | C + CO5300 deep standby | **6.3 mA** / 10 h (≈5.3 mA after the first hour), 3.951 V — gain ≈2–3 mA |
| E | D + ALDO2 cut | 6.7 mA / 8 h (100 → 86 %, 3.962 V) — no gain over D |
| F | D + QMI8658 oscillator/sensors off + DCDC2–4 off | 6.0 mA / 13 h (100 → 80 %, 3.919 V) — no gain over D beyond the ±0.3 mA resolution |

- **CO5300 deep standby helps** [measured]: the first measure with a clear gain. The
  first hour after unplugging a full cell drops the gauge fast (100 → 96 %), which inflates
  the whole-night figure; from the 1 h reading the drain is ≈5.3 mA. Still above the
  2 mA target.
- **Sleep lines** [measured, 2026-11-03, with a temporary probe]: at sleep entry reads `HH HFF L`
  (SCL SDA | TP_INT RTC_INT QMI_INT1 | TE). No line sits low against a pull-up, so the
  I2C and interrupt lines are not the remaining ~5 mA. RTC_INT and QMI_INT1 float (no
  pull-up; the ESP32 pads are disabled in sleep, so no input leakage). TE low is the
  panel's own output.
- **IMU and DCDC2–4** [measured, 2026-11-03]: bisect F reads `6B*` on the I2C row (all
  QMI8658 writes acknowledged) and 6.0 mA, the same as D. Both stay off as default-off
  hygiene. Every peripheral the firmware can reach is now asleep or unpowered, and ~5–6 mA
  remains. Not yet separated: the ESP32-S3 itself, the AMOLED module in deep standby, the
  AXP2101's own draw, and the gauge's accuracy on this cell (capacity [M]).
- **Board off** [measured, test G, 2026-11-03→04]: PWR hold off for 9 h 29 min, no firmware
  change. 4.038 V / 95 % → 4.013 V / 94 %, read ~20 s after power-on. 1 % = 4 mAh, so about
  0.4 mA at most by the gauge (±1 % resolution), and part of the 25 mV is relaxation after
  charging. The same span in deep sleep would cost about 14 %. The ~5–6 mA is therefore a
  real load while the board is in deep sleep, not gauge error or cell self-discharge. The
  gauge did not read low after this power-on, unlike the earlier case; not yet watched
  over the following hours.
- **I2C scan** [measured, 2026-11-03, with a temporary Status row]: `18* 34 38 40* 51 6B` — both codecs answer at
  their schematic addresses and take their whole power-down sequence (`*`). An earlier
  "not found" serial line was a one-off boot. The codecs are not the remaining load.
- **Cutting ALDO2 (`DSI_PWR_EN`) in sleep is harmful** [measured]: +30–40 mA. The AMOLED
  module draws more with its supply enable low while its logic stays on VCC3V3.
- The unused LDOs, codec power-down, ext1 wake and held pads are each below the gauge's
  resolution over one night. They stay on as harmless.
- **Accepted for v2.1.0** (2026-11-04): v2.1.0 ships with a deep-sleep floor of about 6 mA,
  about 66 h on the nominal 400 mAh with no wakes. The 2 mA target stays open. For long
  storage, power the board off with PWR (~6 s). The release build drops the temporary
  sleep-line probe, so its pads are as in test D (6.3 mA); it has not had its own overnight
  test.
- **Gauge caveat** [measured]: after a USB unplug or a PWR power cycle the AXP2101 gauge
  re-estimates *upwards* for an hour or more (57 → 62 %, 50 → 80 %). The drain baseline
  therefore restarts whenever the gauge reads above it. Start a test by unplugging USB
  only.

### Comparison with chipguyhere's libraries (2026-11-12)

chipguyhere reports **2.2–2.4 mA** on this board, measured with a PPK2 on the cell. Sources:

- [power library README-engineering.md](https://github.com/chipguyhere/ESP32-S3-Touch-AMOLED-2.06_power/blob/main/README-engineering.md#preparing-for-sleep)
- the display library's README-engineering.md

His sleep path is in `prepareForSleep()` and `deepSleep()`:

- It switches the unused rails off. These are the same ones we switch off.
- It sets QMI8658 `CTRL7` = 0.
- It drives LCD_RST and TP_RST high and holds them.
- It calls `gpio_deep_sleep_hold_en()`.
- It arms ext0 on BOOT and ext1 on GPIO10.
- The panel goes through DISPOFF, SLPIN, 120 ms, then `4F` = 01. This is the same as ours.

His measured differences that matter to us:

- **Codecs.** He writes nothing to them. The esp_codec_dev stop sequences measured **+1.85 mA**, ES7210 power-down +0.55 mA and ES8311 standby +0.27 mA. The codecs draw least at their power-up defaults. Until v2.3.0 we wrote exactly those stop sequences (`PmuRails::codecsPowerDown`).
- **Pads.** He holds no QSPI or I2S pads. We held all of them (`definePads`).
- **Measured as no effect:** ADCs and gauge off, CLKOUT off, FT3168 hibernate.
- **Measured as harmful:**
  - ALDO2 off: +13 mA.
  - ALDO1 off: +3 mA, and the bus is dead.
  - Panel held in reset: +0.35 mA.
  - Touch held in reset: +0.28 mA.

Switches for the next tests:

- `PmuRails::SLEEP_CODEC_POWERDOWN`
- `PmuRails::SLEEP_IMU_CTRL1_DISABLE`
- `PowerManager::SLEEP_DEFINE_PADS`

All three now default to his behaviour (`false`). The codec state survives everything except a PMIC power cycle, so every test starts with the procedure in [tools/sleep-floor-control/README.md](tools/sleep-floor-control/README.md): a PWR 6 s power cycle on USB, back to 100 %, then unplug USB.

| Test | Firmware | Drain | Decision |
|---|---|---|---|
| H | `tools/sleep-floor-control` (his libraries, unmodified) | — | ≤ 3.5 mA: the extra load is in our firmware. ≥ 5 mA: board or gauge method, stop. |
| I | watch, all three switches `false` | — | < 3 mA: target met |
| J | I + `SLEEP_DEFINE_PADS = true` | — | optional: do the pads matter |

## To measure first on the bench [M]
1. Active current at several brightness levels, on a typical (mostly black) screen.
2. Deep-sleep floor and board-off draw with a real meter (so far gauge-based only).
3. AXP2101 gauge percentage vs. cell voltage over one full discharge.
4. Reset reason and RTC-memory survival for each button (as done on CrowPanel).
5. Fitted cell's real capacity (400 mAh is the assumed figure behind every mA above).
6. Deep-sleep drain with the card fitted over 10 h or more, against the ~6 mA floor.
7. Chart screen's awake drain, since a light map lights far more of the AMOLED than a black page.
8. `ChartEngine` task's stack high-water mark (8 KB chosen generously, not measured).

## Sources
- Waveshare docs: <https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-2.06> (pin table,
  buttons) and <https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-2.06/Arduino> (board
  settings, libraries, examples). Read 2026-10-04.
- Waveshare repo: <https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-2.06> —
  `examples/arduino/01_HelloWorld`, `05_LVGL_AXP2101_ADC_Data`, `06_LVGL_Arduino_v9`,
  `libraries/Mylibrary/pin_config.h`, `libraries/*/library.properties`,
  `libraries/Arduino_GFX/src/display/Arduino_CO5300.h`,
  `libraries/XPowersLib/src/XPowersAXP2101.tpp`,
  `Schematic/ESP32-S3-Touch-AMOLED-2.06-Schematic-V1.0.pdf`. Read 2026-10-04.
- Product page: <https://www.waveshare.com/esp32-s3-touch-amoled-2.06.htm>
