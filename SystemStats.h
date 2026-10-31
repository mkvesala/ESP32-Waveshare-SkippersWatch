#pragma once

#include <Arduino.h>
#include <esp_sleep.h>
#include <esp_system.h>

// === C L A S S  S Y S T E M S T A T S ===
//
// - Collects the Status screen's data. Two lifetimes:
//     * since power-on: uptime (the RTC timer, which runs through deep sleep), wake-ups,
//       total awake time, ESP-NOW frames and unexpected resets. The counters live in
//       RTC_NOINIT memory, so they survive deep sleep, watchdog, panic and software
//       resets, and start over on a true power-on — exactly when the RTC timer does.
//     * this wake: loop runtime (average and max).
// - Deep sleep time is derived: uptime − awake time.
// - Battery drain estimate: a baseline (gauge % + uptime) taken when a battery-powered
//   stretch begins; the Status screen turns the drop since then into average mA. With
//   no meter on the battery lead, this is how the sleep floor gets measured.
// - onSleepEntry() is static and writes RTC RAM only (no flash), so PowerManager calls
//   it on every sleep path, the stuck-button timer path included.
// - Owned by: WatchApplication.

class SystemStats {

public:

    // Call once per boot, before the stuck-button recheck (which may sleep at once)
    void onBoot(esp_sleep_wakeup_cause_t cause, esp_reset_reason_t reason);

    // Time one pass of the main loop
    void onLoop(uint32_t loop_us);

    // Fold this wake's awake time and radio frame count into the power-on totals
    static void onSleepEntry(uint32_t radio_frames_this_wake);

    // Call each loop after BatteryMonitor::update(). On USB the baseline is dropped; the
    // first call on battery takes a new one, so it always starts at an unplug or a
    // power-on on battery.
    void trackDrain(bool on_usb, uint8_t soc);
    bool     drainValid() const;
    uint8_t  drainBasePct() const;
    uint64_t drainElapsedS() const;

    uint64_t uptimeS() const;
    uint64_t awakeS() const;                 // power-on total incl. the current wake
    uint64_t deepSleepS() const;
    uint32_t wakeups() const;
    uint32_t unexpectedResets() const;
    uint32_t radioFramesTotal(uint32_t this_wake) const;
    int      lastResetReason() const { return (int)_reason; }

    float    loopAvgMs() const { return _loop_avg_us / 1000.0f; }
    float    loopMaxMs() const { return _loop_max_us / 1000.0f; }

private:

    esp_reset_reason_t _reason = ESP_RST_UNKNOWN;
    float    _loop_avg_us = 0.0f;
    uint32_t _loop_max_us = 0;
    bool     _loop_seeded = false;

    static constexpr float LOOP_EMA_ALPHA = 0.05f;

};
