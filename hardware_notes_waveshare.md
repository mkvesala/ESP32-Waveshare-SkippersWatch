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

  **Which load is on which rail is [M].** Trace it on the schematic before disabling
  any rail: cutting the rail that feeds the SoC or the I2C pull-ups leaves no way to
  re-enable it from firmware.
- The schematic has an `AXP_IRQ` net; it is **not** in Waveshare's GPIO table, so do
  not assume the PMIC interrupt reaches the ESP32. [M]

## Audio (unused — default off)
- ES8311 codec, ES7210 dual-mic ADC, NS4150B amplifier. [V]
- I2S: MCLK=16, SCLK=41, LRCK=45, DSDIN=40 (to ES8311), ASDOUT=42 (from ES7210). [V]
- Amplifier enable `PA_CTRL` = **GPIO46**. Keep low. [V]

## Other pins [V]
- TF card (SPI): MOSI=1, SCK=2, MISO=3, CS=17.
- Native USB: D−=19, D+=20.
- UART0: TX=43, RX=44.

## Deep-sleep wake summary (Model B)
| Source | GPIO | RTC-capable | Polarity | Use |
|---|---|---|---|---|
| PWR button | 10 | yes [D] | high = pressed | **primary candidate**, replaces encoder button GPIO18 |
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
| Board | ESP32S3 Dev Module, core esp32 3.3.11 |
| USB CDC On Boot | Enabled |
| USB Mode | Hardware CDC and JTAG |
| Flash Size | 32MB (256Mb) |
| Partition Scheme | Huge APP (3MB No OTA/1MB SPIFFS) |
| PSRAM | OPI PSRAM (not used on the wake path) |
| Flash Mode | QIO 80MHz |

Libraries: GFX Library for Arduino 1.6.9 (mainline, contains `Arduino_CO5300`; 1.6.7
GC9309-patched also worked), LVGL 9.6.0 with the project-local `lv_conf.h` (v9.6.0 template),
**XPowersLib 0.3.3** (added for the port). No Arduino_DriveBus: the FT3168 is driven by
the project's own `ft3168_touch.*`. Sketch: ~1.91 MB of the 3 MB app slot.

CLI equivalent (the arduino-cli bundled with the IDE):
`arduino-cli compile --fqbn esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=32M,PartitionScheme=huge_app,PSRAM=opi .`

## To measure first on the bench [M]
1. Deep-sleep floor on battery only, with display off and everything default-off.
2. Active current at several brightness levels, on a typical (mostly black) screen.
3. AXP2101 `shutdown()` floor, for comparison with 1.
4. AXP2101 gauge percentage vs. cell voltage over one full discharge.
5. Reset reason and RTC-memory survival for each button (as done on CrowPanel).

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
