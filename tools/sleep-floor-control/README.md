# sleep-floor-control

A measurement tool, not part of the watch. It runs chipguyhere's own deep-sleep path
for this board on our board, and shows the AXP2101 gauge on every PWR wake. The drain
is computed with the same formula as the watch's Status page (Δ% × 400 mAh / 100 / h).

The question it answers: is our ~6 mA floor caused by our firmware, or by our board
and our gauge-based method? chipguyhere measured 2.2–2.4 mA on the same board with a
PPK2 on the cell
([power library notes](https://github.com/chipguyhere/ESP32-S3-Touch-AMOLED-2.06_power/blob/main/README-engineering.md#preparing-for-sleep)).

## Libraries

These are read-only clones next to the display repo. Nothing is installed into
`~/Documents/Arduino/libraries`.

```bash
cd ~/Documents/Arduino/examples && git clone https://github.com/chipguyhere/ESP32-S3-Touch-AMOLED-2.06_board.git && git clone https://github.com/chipguyhere/ESP32-S3-Touch-AMOLED-2.06_power.git
```

`ESP32-S3-Touch-AMOLED-2.06_display` is cloned in the same place. The sketch uses neither LVGL nor the rtc libraries.

## Build and flash

Arduino compiles only the sketch root and `src/`, so this folder never ends up in the watch build.

```bash
cd tools/sleep-floor-control && "/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli" compile --upload -p /dev/cu.usbmodem14101 --fqbn esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=32M,PartitionScheme=huge_app,PSRAM=opi --library ~/Documents/Arduino/examples/ESP32-S3-Touch-AMOLED-2.06_board --library ~/Documents/Arduino/examples/ESP32-S3-Touch-AMOLED-2.06_power --library ~/Documents/Arduino/examples/ESP32-S3-Touch-AMOLED-2.06_display .
```

If the port is missing because the board is in deep sleep, hold BOOT while you plug in USB.

## Screen

On each wake the screen stays on for 10 s, then the board goes back to deep sleep. PWR wakes it.

| Line | Content |
|---|---|
| 1 | gauge %; `USB` in front means VBUS is present and the baseline is cleared |
| 2 | VBAT mV |
| 3 | hours since the baseline |
| 4 | Avg drain mA (`-` for the first 0.5 h) |
| 5 | wake count since power-on |

On USB, Serial prints the same values plus a raw AXP2101 dump of registers 0x10..0x9F.
Diff it against the watch's `[rails] reg` lines.

## Procedure

The procedure is the same for the watch firmware tests in `hardware_notes_waveshare.md`.

1. Charge to 100 % with USB connected.
2. With USB still connected, hold PWR about 6 s until the board turns off, then press PWR.
   This PMIC power cycle returns both codecs to their power-up defaults. Their register
   state otherwise survives everything, because ALDO1 never switches off.
3. Wait until the gauge reads 100 % again on USB.
4. Unplug USB. Don't power-cycle on battery, because the gauge re-estimates upwards for an hour or more afterwards.
5. Press PWR once to take the baseline (line 3 shows `0.0 H`). Leave the slot empty.
6. After at least 10 h, press PWR and read lines 1–4.
