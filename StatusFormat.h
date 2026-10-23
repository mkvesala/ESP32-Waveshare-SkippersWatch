#pragma once

#include <stdint.h>
#include <stdio.h>

// === S T A T U S  F O R M A T ===
//
// - Pure text formatting and the counter-reset rule for the Status screen. No Arduino,
//   LVGL or ESP-IDF types (reset reasons are passed as the plain esp_reset_reason_t
//   numbers), so host unit tests can exercise them.
// - Owners: StatusUI (formatting), SystemStats (shouldReset).

struct StatusFormat {

    // esp_reset_reason_t values used here (ESP-IDF esp_system.h, stable across 4.x/5.x)
    enum Reason : int {
        RST_UNKNOWN   = 0,
        RST_POWERON   = 1,
        RST_EXT       = 2,
        RST_SW        = 3,
        RST_PANIC     = 4,
        RST_INT_WDT   = 5,
        RST_TASK_WDT  = 6,
        RST_WDT       = 7,
        RST_DEEPSLEEP = 8,
        RST_BROWNOUT  = 9,
        RST_SDIO      = 10,
    };

    // "54h 02m 30s" — hours keep counting past 24, a day count would only add a unit
    static void duration(uint64_t seconds, char* out, size_t len) {
        const uint64_t h = seconds / 3600;
        const unsigned m = (unsigned)((seconds / 60) % 60);
        const unsigned s = (unsigned)(seconds % 60);
        snprintf(out, len, "%02lluh %02um %02us", (unsigned long long)h, m, s);
    }

    // Whole KB below 10 KB gets one decimal ("5.2 KB"), larger values none ("196 KB")
    static void kb(uint32_t bytes, char* out, size_t len) {
        if (bytes < 10u * 1024u) {
            const unsigned tenths = (unsigned)((bytes * 10u + 512u) / 1024u);
            snprintf(out, len, "%u.%u KB", tenths / 10u, tenths % 10u);
        } else {
            snprintf(out, len, "%u KB", (unsigned)((bytes + 512u) / 1024u));
        }
    }

    static const char* resetReason(int reason) {
        switch (reason) {
            case RST_POWERON:   return "POWERON";
            case RST_DEEPSLEEP: return "DEEPSLEEP";
            case RST_SW:        return "SW";
            case RST_PANIC:     return "PANIC";
            case RST_INT_WDT:
            case RST_TASK_WDT:
            case RST_WDT:       return "WDT";
            case RST_BROWNOUT:  return "BROWNOUT";
            case RST_EXT:       return "EXT";
            default:            return "OTHER";
        }
    }

    // The resets nobody asked for: they count towards "Resets" on the Status screen
    static bool isUnexpected(int reason) {
        return reason == RST_PANIC || reason == RST_INT_WDT || reason == RST_TASK_WDT ||
               reason == RST_WDT || reason == RST_BROWNOUT;
    }

    // The power-on counters start over when their RTC memory cannot be trusted (magic
    // missing) or on a true power-on — the same moment the RTC timer behind "Uptime"
    // restarts from zero, so all the counters always describe the same span.
    static bool shouldReset(bool magic_ok, int reason) {
        return !magic_ok || reason == RST_POWERON;
    }

};
