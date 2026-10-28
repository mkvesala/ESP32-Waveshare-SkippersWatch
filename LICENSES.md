# Third-Party Licenses

This project uses the following open-source libraries:

## Espressif ESP32 Arduino Core
- **Version:** 3.3.8
- **Author:** Espressif Systems
- **License:** GNU Lesser General Public License v2.1
- **Repository:** https://github.com/espressif/arduino-esp32

## LVGL
- **Version:** 9.6.0
- **Author:** LVGL
- **License:** MIT
- **Repository:** https://github.com/lvgl/lvgl

## Arduino GFX Library
- **Version:** 1.6.9 (mainline; contains `Arduino_CO5300`. The v1.0.0 CrowPanel build needed 1.6.7 patched with Elecrow's `Arduino_GC9309` driver)
- **Author:** Moon On Our Nation (moononournation)
- **License:** BSD 3-Clause
- **Repository:** https://github.com/moononournation/Arduino_GFX

## Astronomy Engine
- **Version:** 2.1.19 (C version, `astronomy.c` / `astronomy.h` copied into the sketch folder)
- **Author:** Don Cross
- **License:** MIT
- **Repository:** https://github.com/cosinekitty/astronomy
- **Local change:** `ASTRONOMY_ENGINE_NO_CURRENT_TIME` defined at the top of `astronomy.h`

## Star data
- **Source:** Hipparcos, the New Reduction (van Leeuwen 2007, VizieR I/311) and the Hipparcos Main Catalogue (ESA 1997, VizieR I/239), retrieved via CDS VizieR; names checked against CDS SIMBAD
- **Used in:** `NavStars.cpp`

## AXS5106L touch driver (local)
- **Source:** adapted from Elecrow's Lesson03 example for this board
- **Author:** Elecrow (example code)
- **File:** `axs5106l_touch.h/.cpp` in this repository

---

**Note:** Please verify license details from each library's repository. This file is for informational purposes.
