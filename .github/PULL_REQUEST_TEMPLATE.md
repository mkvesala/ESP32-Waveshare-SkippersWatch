## Description

Please include a summary of the change and which issue is fixed.

Fixes # (issue)

## Type of change

- [ ] Bug fix (non-breaking change which fixes an issue)
- [ ] New feature (non-breaking change which adds functionality)
- [ ] Breaking change (fix or feature that would cause existing functionality to not work as expected)
- [ ] Documentation update

## How Has This Been Tested?

Please describe the tests that you ran to verify your changes.

- [ ] Tested on actual hardware
- [ ] ESP32 boots/wakes without errors
- [ ] Display turns on, screens start in the placeholder (`---` / `--.-`) state
- [ ] ESP-NOW data received; the affected screen's values update correctly
- [ ] Placeholders persist until a fresh broadcast (stale data never shown as current)
- [ ] Screen carousel works both directions (swipe up/down AND knob CW/CCW)
- [ ] Touch gestures work (tap / swipe / slider drag)
- [ ] Setup brightness adjusts (touch drag and knob) and persists after reboot (NVS)
- [ ] Deep sleep entry (8 s timeout) and button wake (GPIO18) work
- [ ] Battery bar reflects SoC (green/amber/red) and charging (blue) correctly
- [ ] No memory leaks / no unexpected bottlenecks in Serial output

**Test Configuration**:
- Arduino IDE: [e.g. 2.3.x]
- ESP32 board package: [e.g. 3.3.8]
- LVGL: [e.g. 9.5.0]
- Arduino_GFX_Library: [e.g. 1.6.7 + GC9309 patch]
- SquareLine Studio: [e.g. 1.6.0]

## Checklist:

- [ ] My code follows the style guidelines of this project
- [ ] I have performed a self-review of my own code
- [ ] I have commented my code, particularly in hard-to-understand areas
- [ ] I have made corresponding changes to the documentation
- [ ] My changes generate no new warnings
- [ ] I have tested on actual hardware if possible
- [ ] I have acknowledged any AI assistance in code comments/PR description

## AI Assistance

- [ ] No AI tools used
- [ ] ChatGPT used for code generation/review
- [ ] Claude used for code generation/review
- [ ] GitHub Copilot used
- [ ] Other AI tools (specify): _____________

If AI was used, describe the prompts and how you verified the code:
```
(describe here)
```

