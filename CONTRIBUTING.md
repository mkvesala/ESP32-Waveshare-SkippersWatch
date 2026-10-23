# Contributing to ESP32-Waveshare-SkippersWatch

Thank you for considering contributing to this project! This document provides guidelines for contributing.

## How Can I Contribute?

### About the project

This is an individual *learning project* by me. Please refer to [README.md](README.md) for the purpose of the project. While new feature suggestions and bug fixes are welcome, the most precious are any advice that might help the me improving my knowledge in ESP32 programming.

### Reporting Bugs

Before creating bug reports, please check existing issues to avoid duplicates. When creating a bug report, include:

- Clear description of the issue
- Detailed steps to reproduce the behavior
- What you expected to happen
- What actually happened
- Environment:
  - Arduino IDE version
  - ESP32 board package version
  - Hardware used
  - Library versions
- Output (Serial monitor, CrowPanel display)
- Screenshots, if applicable

### Suggesting Features

Feature requests are welcome! Please:

- Use a clear and descriptive title
- Provide detailed description of the proposed feature
- Explain why this feature would be useful
- Include code examples or mockups if applicable

### Pull Requests

1. **Fork the repository** and create your branch from `main`
   ```
   git checkout -b feature/your-feature-name
   ```
2. Follow the coding style used in the project:
   - Use descriptive variable and function names in English
   - Add comments for complex logic in English
   - Keep class responsibilities clear and focused
   - Follow existing naming conventions
3. Test your changes:
   - Compiles without errors or warnings
   - Test on actual hardware, out there if possible
   - Check that existing features work
4. Update documentation:
   - Update README.md if you add new features
5. Commit your changes:
   ```
   git commit -m "Add brief description of your changes"
   ```
   - Use clear commit messages
6. Push to your fork and submit a PR:
   ```
   git push origin feature/your-feature-name
   ```
7. Wait for maintainer's review and feedback

## Coding guidelines

1. Use consistent indentation
2. Opening brace on the same line for functions and control structures:
  ```
   void myFunction() {
      if (condition) {
         // code
      }
   }
   ```
3. No braces if one line of code like in:
   ```
   if (condition) return;
   ```
4. Naming:
   - Classes, structs, enum classes `PascalCase`
   - Functions/methods `camelCase`
   - Variables `snake_case`
   - Private members `_snake_case``
   - Static variables `s_snake_case`
   - Constants `UPPER_SNAKE_CASE`
   - Timers: use `_ms`, `_MS`, `_us` to indicate the units for example `NEXT_TRY_MS`
5. Use `//` in single line comments
6. Avoid `#DEFINE` macros, use compile time `static constexpr` constants when possible
7. Avoid String variables
8. Use `delay()` in loop task *only* when absolutely necessary
9. Minimize dynamic memory allocation - prefer stack allocation
12. Respect ESP32 limited resources and core 0/core 1 task responsibilities if such applied

## Testing checklist

* [ ] ESP32 boots/wakes without errors
* [ ] Display turns on promptly; all screens start in the placeholder (`---` / `--.-`) state
* [ ] ESP-NOW data received; each screen updates from its source:
  * [ ] Navigation — POS / COG (blank at anchor) / SOG / DPT
  * [ ] Batteries — house V / A / SoC, starter V
  * [ ] Weather — temperature / pressure / humidity
  * [ ] Engine — exhaust temp, fuel and fresh-water litres
  * [ ] Attitude — pitch, roll and horizon line
  * [ ] Clock — local date/weekday, local + UTC time (GNSS-disciplined)
* [ ] Stale data never shown as current; placeholders persist until a fresh broadcast
* [ ] Screen carousel works both directions: swipe up/down AND knob CW/CCW
* [ ] Touch gestures work: tap, swipe, Setup slider drag (absolute) and Setup switch tap
* [ ] Setup brightness shows the NVS value on boot, adjusts by touch drag, persists after reboot
* [ ] Setup prevent-sleep switch: red off / green on, keeps the watch awake on battery, still sleeps at the 120 s ceiling, persists after reboot
* [ ] Deep sleep entry after the 10 s timeout; button wake (GPIO18) restores the last screen
* [ ] Battery bar: SoC color green/amber/red, and blue/100 % while on USB power
* [ ] No memory leaks (heap free stable over time)
* [ ] Serial output reports no unexpected bottlenecks

## Development environment

### Software requirements

[README.md](README.md) for details.

### Hardware requirements

[README.md](README.md) for details.

## AI-assisted development

### Policy

1. **Using AI tools is allowed and encouraged** - ChatGPT, Claude Code, Github Copilot or IDE AI copilots, whatever works for you
2. **Transparency is important**  - be open and mention that AI has assisted you in development
3. **Understanding first** - you are responsible for not only the code itself but also being able to explain it

### Best practises

1. **Always test** 
2. **Check security** 
3. **Match coding style** 
4. **Share learnings** 
5. **Put effort on prompting and building skills**
6. **Document skills or other assets in use**

### Copyright

AI tools are assistive tools. You are responsible for the code. Make your best to ensure that the code does not violate 3rd party licenses.

## Code of conduct

1. Check existing issues
2. Create a new issue with your question
3. Be patient - I'm maintaining this on my freetime among other boat projects
4. Be respectful and constructive
5. Welcome beginners
6. No harassment, no inappropriate behaviour, no crimes




