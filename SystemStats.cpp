#include "SystemStats.h"
#include "StatusFormat.h"
#include <esp_rtc_time.h>

// Power-on counters. .rtc.noinit for the same reason as the last-page store in
// WatchApplication: .rtc.data is reloaded from flash on every boot except a deep-sleep
// wake, so it would be wiped by a watchdog or panic reset — exactly the events the
// "Resets" counter has to survive. Trusted only when the magic word matches.
namespace {
    struct PowerOnStats {
        uint32_t magic;
        uint32_t wakeups;
        uint64_t awake_ms;        // completed wakes only; the current one is millis()
        uint64_t radio_frames;    // completed wakes only
        uint32_t unexpected_resets;
        // Drain baseline: gauge % and uptime when the current battery-powered stretch
        // began (first loop off USB). Cleared while on USB.
        uint32_t drain_valid;
        uint32_t drain_base_pct;
        uint64_t drain_base_s;
    };
    RTC_NOINIT_ATTR PowerOnStats s_stats;
    constexpr uint32_t STATS_MAGIC = 0x57A75C02;
}

// === P U B L I C ===

void SystemStats::onBoot(esp_sleep_wakeup_cause_t cause, esp_reset_reason_t reason) {
    _reason = reason;
    if (StatusFormat::shouldReset(s_stats.magic == STATS_MAGIC, (int)reason)) {
        s_stats = {};
        s_stats.magic = STATS_MAGIC;
    }
    // A user wake is the PWR button; the stuck-button guard's timer wakes are not
    if (cause == ESP_SLEEP_WAKEUP_EXT0 || cause == ESP_SLEEP_WAKEUP_EXT1) s_stats.wakeups++;
    if (StatusFormat::isUnexpected((int)reason)) s_stats.unexpected_resets++;
}

void SystemStats::onLoop(uint32_t loop_us) {
    if (!_loop_seeded) {
        _loop_avg_us = (float)loop_us;
        _loop_seeded = true;
    } else {
        _loop_avg_us += LOOP_EMA_ALPHA * ((float)loop_us - _loop_avg_us);
    }
    if (loop_us > _loop_max_us) _loop_max_us = loop_us;
}

void SystemStats::onSleepEntry(uint32_t radio_frames_this_wake) {
    if (s_stats.magic != STATS_MAGIC) return;
    s_stats.awake_ms += millis();
    s_stats.radio_frames += radio_frames_this_wake;
}

void SystemStats::trackDrain(bool on_usb, uint8_t soc) {
    if (s_stats.magic != STATS_MAGIC) return;
    if (on_usb) {
        s_stats.drain_valid = 0;
        return;
    }
    if (s_stats.drain_valid) return;
    s_stats.drain_valid    = 1;
    s_stats.drain_base_pct = soc;
    s_stats.drain_base_s   = this->uptimeS();
}

bool SystemStats::drainValid() const {
    return s_stats.magic == STATS_MAGIC && s_stats.drain_valid != 0;
}

uint8_t SystemStats::drainBasePct() const {
    return (uint8_t)s_stats.drain_base_pct;
}

uint64_t SystemStats::drainElapsedS() const {
    const uint64_t up = this->uptimeS();
    return up > s_stats.drain_base_s ? up - s_stats.drain_base_s : 0;
}

uint64_t SystemStats::uptimeS() const {
    return esp_rtc_get_time_us() / 1000000ULL;
}

uint64_t SystemStats::awakeS() const {
    return (s_stats.awake_ms + millis()) / 1000ULL;
}

// The RTC timer and millis() come from different clocks; clamp so a little drift can
// never show a negative sleep time.
uint64_t SystemStats::deepSleepS() const {
    const uint64_t up = this->uptimeS();
    const uint64_t awake = this->awakeS();
    return up > awake ? up - awake : 0;
}

uint32_t SystemStats::wakeups() const {
    return s_stats.wakeups;
}

uint32_t SystemStats::unexpectedResets() const {
    return s_stats.unexpected_resets;
}

uint32_t SystemStats::radioFramesTotal(uint32_t this_wake) const {
    return (uint32_t)(s_stats.radio_frames + this_wake);
}
