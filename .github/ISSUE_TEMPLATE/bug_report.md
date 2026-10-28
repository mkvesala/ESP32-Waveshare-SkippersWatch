---
name: Bug report
about: Create a report to help us improve
title: '[BUG] '
labels: bug
---

**Describe the bug**
A clear and concise description of what the bug is.

**To Reproduce**
Steps to reproduce the behavior:
1. Configure '...'
2. Run '...'
3. Observe '...'

**Expected behavior**
A clear and concise description of what you expected to happen.

**Environment:**
- Arduino IDE version: [e.g. 2.3.x]
- ESP32 board package: [e.g. 3.3.8]
- LVGL version: [e.g. 9.6.0]
- Arduino_GFX_Library: [e.g. 1.6.9]
- SquareLine Studio: [e.g. 1.6.0]

**Logs**
Paste Serial monitor output here (remember `USB CDC On Boot = Disabled` so the CH34x bridge shows the log):
```
(paste here)
```

**Screenshots**
If applicable, add screenshots of the CrowPanel display.

**Which screen is affected?**
- [ ] Navigation
- [ ] Batteries
- [ ] Weather
- [ ] Engine
- [ ] Attitude
- [ ] Clock
- [ ] Setup (brightness)
- [ ] All screens / startup / wake
- [ ] Deep sleep / wake

**Hardware setup**
- [ ] CrowPanel 2.01" HMI ESP32 Watch (240×296, GC9309 SPI) connected via USB-C
- [ ] WCH CH34x driver installed, serial port visible
- [ ] Running on battery (USB unplugged) vs. on USB power
- [ ] Relevant ESP-NOW sender(s) running and broadcasting
- [ ] WiFi router on fixed channel 6

**Additional context**
Add any other context about the problem here.
