#include "ThemeController.h"
#include "ui.h"

// === P U B L I C ===

void ThemeController::begin() {
    if (_slot_count != 0) return;
    this->capture(ui_ScreenMain, LV_PART_MAIN, LV_STYLE_BG_COLOR);
    this->capture(ui_ScreenMain, LV_PART_MAIN, LV_STYLE_TEXT_COLOR);   // inherited by labels
    this->capture(ui_BarBatteryIndicator, LV_PART_MAIN, LV_STYLE_BG_COLOR);
    this->capture(ui_BarBatteryIndicator, LV_PART_MAIN, LV_STYLE_OUTLINE_COLOR);
}

// No-op when nothing changes after the first call, so the app may call it every loop.
void ThemeController::apply(bool night) {
    if (_applied && night == _night) return;
    _night = night;
    _applied = true;

    for (size_t i = 0; i < _slot_count; ++i) {
        const Slot& s = _slots[i];
        lv_style_value_t v;
        v.color = lv_color_hex(NightPalette::pick(s.day_rgb, night));
        lv_obj_set_local_style_prop(s.obj, s.prop, v, s.part | LV_STATE_DEFAULT);
    }

    _battery.setNightMode(night);
    _menu.setNightMode(night);
    lv_obj_invalidate(ui_ScreenMain);
    Serial.printf("[theme] %s palette\n", night ? "night" : "day");
}

// === P R I V A T E ===

void ThemeController::capture(lv_obj_t* obj, lv_part_t part, lv_style_prop_t prop) {
    if (obj == nullptr || _slot_count >= SLOT_COUNT) return;
    const lv_style_value_t v = lv_obj_get_style_prop(obj, part, prop);
    _slots[_slot_count++] = { obj, part, prop, lv_color_to_int(v.color) };
}
