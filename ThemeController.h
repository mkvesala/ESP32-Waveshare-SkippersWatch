#pragma once

#include <Arduino.h>
#include <lvgl.h>
#include "NightPalette.h"
#include "BatteryMonitor.h"
#include "MenuUI.h"

// === C L A S S  T H E M E C O N T R O L L E R ===
//
// - Applies the day or the night palette to everything on ui_ScreenMain that no page
//   adapter owns: the LVGL theme's screen colors (background, inherited text color, so
//   every label follows) and the battery bar's frame. Then it tells the color owners to
//   repaint: the battery bar buckets (BatteryMonitor) and the menu icons (MenuUI). The
//   Setup page paints its own sliders and switches from the same stored choice.
// - Theme colors are captured once from LVGL on begin() and mapped through NightPalette,
//   so the day look is exactly what the theme + SquareLine produce, never a copy of it.
// - Call begin() after ui_init() and apply() before the first rendered frame, so a night
//   wake is red from its very first lit frame. apply() again whenever the switch flips.
// - Owned by: WatchApplication.

class ThemeController {

public:

    ThemeController(BatteryMonitor& battery, MenuUI& menu) : _battery(battery), _menu(menu) {}

    void begin();
    void apply(bool night);
    bool night() const { return _night; }

private:

    // One theme-owned color: which object/part/property, and its captured day value
    struct Slot {
        lv_obj_t*       obj;
        lv_part_t       part;
        lv_style_prop_t prop;
        uint32_t        day_rgb;
    };

    static constexpr size_t SLOT_COUNT = 4;
    Slot _slots[SLOT_COUNT] = {};
    size_t _slot_count = 0;

    BatteryMonitor& _battery;
    MenuUI& _menu;
    bool _night = false;
    bool _applied = false;

    void capture(lv_obj_t* obj, lv_part_t part, lv_style_prop_t prop);

};
