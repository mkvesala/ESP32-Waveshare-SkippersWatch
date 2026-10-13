#include "MenuUI.h"
#include "ui.h"

// === P U B L I C ===

void MenuUI::begin() {
    if (_initialized) return;
    lv_obj_add_flag(ui_ContainerMenu, LV_OBJ_FLAG_HIDDEN);
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

int8_t MenuUI::pageAt(int16_t x, int16_t y) const {
    if (!_initialized || !_visible) return -1;

    struct Button { lv_obj_t* obj; int8_t page; };
    const Button buttons[] = {
        { ui_ImgButtonNavigation, PAGE_NAVIGATION },
        { ui_ImgButtonBatteries,  PAGE_BATTERIES  },
        { ui_ImgButtonWeather,    PAGE_WEATHER    },
        { ui_ImgButtonEngine,     PAGE_ENGINE     },
        { ui_ImgButtonAttitude,   PAGE_ATTITUDE   },
        { ui_ImgButtonClock,      PAGE_CLOCK      },
        { ui_ImgButtonSetup,      PAGE_SETUP      },
    };

    for (const Button& b : buttons) {
        if (b.obj == nullptr) continue;
        lv_area_t a;
        lv_obj_get_coords(b.obj, &a);
        if (x >= (a.x1 - TOUCH_PAD_PX) && x <= (a.x2 + TOUCH_PAD_PX) &&
            y >= (a.y1 - TOUCH_PAD_PX) && y <= (a.y2 + TOUCH_PAD_PX)) {
            return b.page;
        }
    }
    return -1;
}
