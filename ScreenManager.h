#pragma once

#include <Arduino.h>
#include <lvgl.h>
#include "IScreenUI.h"

// ===  C L A S S  S C R E E N M A N A G E R ===
//
// - Class ScreenManager - switches between pages of the watch UI
// - IMPORTANT: unlike the verrokki (which loads separate LVGL screens), the watch
//   UI is a SINGLE LVGL screen (ui_ScreenMain) with several sibling CONTAINERS.
//   A "page" here is a container; switching = show target container, hide the rest
//   via LV_OBJ_FLAG_HIDDEN. Each IScreenUI::getLvglScreen() returns its container.
// - Pages are registered via addScreen() before begin()
// - Init: addScreen() for each page, then screenMgr.begin()
// - Carousel order matches registration order (modulo index arithmetic)
// - Owned by: CrowPanelApplication

class ScreenManager {

public:

    static constexpr uint8_t MAX_SCREENS = 8;

    explicit ScreenManager() = default;

    void addScreen(IScreenUI* screen);
    void begin();
    void switchNext();
    void switchPrevious();
    IScreenUI* getCurrentScreen() const;

    // Re-assert the current page's visibility (used after a wake re-render)
    void showCurrent();

private:

    enum class Direction { CW, CCW };

    void switchTo(uint8_t index, Direction dir);
    void hideAll();

    uint8_t nextIdx() const { return (_current + 1) % _screen_count; }
    uint8_t previousIdx() const { return (_current + _screen_count - 1) % _screen_count; }

    IScreenUI* _screens[MAX_SCREENS] = {};
    uint8_t _screen_count = 0;
    uint8_t _current = 0;
    bool _initialized = false;

};
