#pragma once

#include <Arduino.h>
#include <Arduino_GFX_Library.h>

// === C L A S S  P A N E L B R I G H T N E S S ===
//
// - The AMOLED's "backlight": there is no LED and no PWM pin on this board. Brightness
//   is a panel command (CO5300 0x51, 0..255) sent over QSPI by Arduino_OLED::setBrightness.
// - Replaces the CrowPanel LEDC write/read pair: remembers the last level sent, because
//   the panel register cannot be read back cheaply.
// - Shared by SetupUI (ramp up, slider, ceiling) and PowerManager (ramp down at sleep).
// - Owned by: WatchApplication (attach() after the panel has been begun).

class PanelBrightness {

public:

    void attach(Arduino_OLED* panel) { _panel = panel; }

    // Send a level 0..255 to the panel. No-op when nothing changed or no panel attached.
    void set(uint8_t level);

    // Last level sent (0 = dark)
    uint8_t current() const { return _level; }

private:

    Arduino_OLED* _panel = nullptr;
    uint8_t _level = 0;
    bool _sent = false;   // false until the first write, so set(0) at boot is not elided

};
