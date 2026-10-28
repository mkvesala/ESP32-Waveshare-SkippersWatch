#include "MenuUI.h"
#include "ui.h"

// === P U B L I C ===

void MenuUI::begin() {
    if (_initialized) return;
    lv_obj_add_flag(ui_ContainerMenu, LV_OBJ_FLAG_HIDDEN);
    this->setNightMode(false);   // the export leaves both icon sets visible
    _visible = false;
    _initialized = true;
}

void MenuUI::show() {
    if (!_initialized || _visible) return;
    lv_obj_remove_flag(ui_ContainerMenu, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(ui_ContainerMenu);
    _visible = true;
}

void MenuUI::hide() {
    if (!_initialized || !_visible) return;
    lv_obj_add_flag(ui_ContainerMenu, LV_OBJ_FLAG_HIDDEN);
    _visible = false;
}

void MenuUI::setNightMode(bool on) {
    _night = on;
    Button bs[BUTTON_COUNT];
    buttons(bs);
    for (const Button& b : bs) {
        if (b.day != nullptr) {
            if (on) lv_obj_add_flag(b.day, LV_OBJ_FLAG_HIDDEN);
            else    lv_obj_remove_flag(b.day, LV_OBJ_FLAG_HIDDEN);
        }
        if (b.night != nullptr) {
            if (on) lv_obj_remove_flag(b.night, LV_OBJ_FLAG_HIDDEN);
            else    lv_obj_add_flag(b.night, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

// Hit-test against the visible set's boxes (both sets share positions in the export,
// but the visible one is the one the user is aiming at).
int8_t MenuUI::pageAt(int16_t x, int16_t y) const {
    if (!_initialized || !_visible) return -1;

    Button bs[BUTTON_COUNT];
    buttons(bs);
    for (const Button& b : bs) {
        lv_obj_t* obj = _night ? b.night : b.day;
        if (obj == nullptr) continue;
        lv_area_t a;
        lv_obj_get_coords(obj, &a);
        if (x >= (a.x1 - TOUCH_PAD_PX) && x <= (a.x2 + TOUCH_PAD_PX) &&
            y >= (a.y1 - TOUCH_PAD_PX) && y <= (a.y2 + TOUCH_PAD_PX)) {
            return b.page;
        }
    }
    return -1;
}

// === P R I V A T E ===

void MenuUI::buttons(Button (&out)[BUTTON_COUNT]) {
    const Button table[BUTTON_COUNT] = {
        { ui_ImgButtonNavigation, ui_ImgButtonNavigationNight, PAGE_NAVIGATION },
        { ui_ImgButtonBatteries,  ui_ImgButtonBatteriesNight,  PAGE_BATTERIES  },
        { ui_ImgButtonWeather,    ui_ImgButtonWeatherNight,    PAGE_WEATHER    },
        { ui_ImgButtonEngine,     ui_ImgButtonEngineNight,     PAGE_ENGINE     },
        { ui_ImgButtonAttitude,   ui_ImgButtonAttitudeNight,   PAGE_ATTITUDE   },
        { ui_ImgButtonClock,      ui_ImgButtonClockNight,      PAGE_CLOCK      },
        { ui_ImgButtonSetup,      ui_ImgButtonSetupNight,      PAGE_SETUP      },
        { ui_ImgButtonCelestial,  ui_ImgButtonCelestialNight,  PAGE_CELESTIAL  },
    };
    for (size_t i = 0; i < BUTTON_COUNT; ++i) out[i] = table[i];
}
