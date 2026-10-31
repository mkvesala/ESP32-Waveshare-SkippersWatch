#include "StatusUI.h"
#include "StatusFormat.h"
#include "ESPNowReceiver.h"
#include "ui.h"
#include <esp_heap_caps.h>

// Row titles, in display order. render() emits exactly one value line per entry.
static const char* const kTitles =
    "Uptime\n"
    "Wake-ups\n"
    "Wake time\n"
    "Deep sleep\n"
    "Free heap\n"
    "Min free heap\n"
    "Loop stack free\n"
    "Loop avg / max\n"
    "ESP-NOW packets\n"
    "Resets\n"
    "Last reset\n"
    "Chip temp\n"
    "Battery voltage\n"
    "Avg drain\n"
    "PMU rails";

// === P U B L I C ===

void StatusUI::begin() {
    if (_initialized) return;
    lv_obj_add_flag(ui_ContainerStatus, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(ui_LabelInfo, kTitles);
    _visible = false;
    _initialized = true;
}

void StatusUI::show() {
    if (!_initialized || _visible) return;
    this->render();   // current values in the first frame, not the SquareLine hints
    _last_refresh_ms = millis();
    lv_obj_remove_flag(ui_ContainerStatus, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(ui_ContainerStatus);
    _visible = true;
}

void StatusUI::hide() {
    if (!_initialized || !_visible) return;
    lv_obj_add_flag(ui_ContainerStatus, LV_OBJ_FLAG_HIDDEN);
    _visible = false;
}

void StatusUI::update(uint32_t now) {
    if (!_visible || (now - _last_refresh_ms) < REFRESH_MS) return;
    _last_refresh_ms = now;
    this->render();
}

// === P R I V A T E ===

void StatusUI::render() {
    char up[24], awake[24], sleep[24], heap[16], heap_min[16], stack[16];
    StatusFormat::duration(_stats.uptimeS(), up, sizeof(up));
    StatusFormat::duration(_stats.awakeS(), awake, sizeof(awake));
    StatusFormat::duration(_stats.deepSleepS(), sleep, sizeof(sleep));
    StatusFormat::kb(heap_caps_get_free_size(MALLOC_CAP_INTERNAL), heap, sizeof(heap));
    StatusFormat::kb(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL), heap_min, sizeof(heap_min));
    // ESP-IDF reports the high-water mark in bytes. render() runs on the loop task, so
    // NULL is the loop task's own stack.
    StatusFormat::kb(uxTaskGetStackHighWaterMark(NULL), stack, sizeof(stack));

    const uint16_t mv = _battery.cellMillivolts();

    // Average drain since the battery-powered stretch began (baseline in SystemStats)
    char drain[24], rails[24];
    StatusFormat::drain(_stats.drainValid() && !_battery.charging(), _stats.drainBasePct(),
                        _battery.soc(), _stats.drainElapsedS(), BATTERY_CAPACITY_MAH,
                        drain, sizeof(drain));
    _rails.summary(rails, sizeof(rails));

    char buf[448];
    snprintf(buf, sizeof(buf),
             "%s\n%lu\n%s\n%s\n%s\n%s\n%s\n%.1f / %.0f ms\n%lu\n%lu\n%s\n%.0f °C\n%u.%03u V\n%s\n%s",
             up,
             (unsigned long)_stats.wakeups(),
             awake,
             sleep,
             heap,
             heap_min,
             stack,
             _stats.loopAvgMs(), _stats.loopMaxMs(),
             (unsigned long)_stats.radioFramesTotal(ESPNowReceiver::radioFrameCount()),
             (unsigned long)_stats.unexpectedResets(),
             StatusFormat::resetReason(_stats.lastResetReason()),
             temperatureRead(),
             mv / 1000u, mv % 1000u,
             drain,
             rails);
    lv_label_set_text(ui_LabelInfo1, buf);
}
