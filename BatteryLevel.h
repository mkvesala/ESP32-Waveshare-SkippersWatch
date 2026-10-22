#pragma once

#include <stdint.h>

// === B A T T E R Y  L E V E L ===
//
// - The watch cell's coarse charge state and its pure classification rule. No Arduino
//   dependencies, so host unit tests (tests/host) can exercise the thresholds directly.
// - BatteryMonitor exposes this as BatteryMonitor::Level; PowerManager decides what
//   each level means (display timeout, brightness ceiling, protective sleep).
// - CUTOFF trips on either a near-empty SoC or the hard voltage floor, whichever first.
//   No hysteresis here: BatteryMonitor escalates one way within an awake session.

enum class BatteryLevel : uint8_t { OK = 0, WARN = 1, CRITICAL = 2, CUTOFF = 3 };

struct BatteryLadder {

    static constexpr uint8_t  WARN_AT_OR_BELOW     = 20;
    static constexpr uint8_t  CRITICAL_AT_OR_BELOW = 10;
    static constexpr uint8_t  CUTOFF_AT_OR_BELOW   = 5;
    static constexpr uint16_t CUTOFF_CELL_MV       = 3400;  // well above the cell's own PCM cutoff

    static BatteryLevel classify(uint8_t soc, uint16_t cell_mv) {
        if (soc <= CUTOFF_AT_OR_BELOW || cell_mv <= CUTOFF_CELL_MV) return BatteryLevel::CUTOFF;
        if (soc <= CRITICAL_AT_OR_BELOW)                            return BatteryLevel::CRITICAL;
        if (soc <= WARN_AT_OR_BELOW)                                return BatteryLevel::WARN;
        return BatteryLevel::OK;
    }

    // Display-on window for a level. The user's wake time (Setup slider) applies as is on
    // a healthy cell; from WARN down the ladder's shorter window wins, but never extends a
    // user choice that is already shorter.
    static constexpr uint32_t TIMEOUT_WARN_MS     = 5000;
    static constexpr uint32_t TIMEOUT_CRITICAL_MS = 3000;

    static uint32_t displayTimeoutMs(BatteryLevel level, uint32_t user_ms) {
        uint32_t cap = UINT32_MAX;
        if (level == BatteryLevel::WARN)          cap = TIMEOUT_WARN_MS;
        else if (level >= BatteryLevel::CRITICAL) cap = TIMEOUT_CRITICAL_MS;
        return user_ms < cap ? user_ms : cap;
    }

};
