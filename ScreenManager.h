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
// - Init: addScreen() for each page, then screenMgr.begin(start_index) — the start
//   page defaults to 0 (Navigation) but a button-wake restores the last-active page
//   (see WatchApplication::resolveStartPage / RTC_NOINIT_ATTR s_last_page_index + NVS)
// - Carousel order matches registration order (modulo index arithmetic)
// - Owned by: WatchApplication

class ScreenManager {

public:

    static constexpr uint8_t MAX_SCREENS = 8;

    explicit ScreenManager() = default;

    void addScreen(IScreenUI* screen);
    void begin(uint8_t start_index = 0);
    void switchNext();
    void switchPrevious();

    // Jump straight to a page (the page menu's image buttons). Out-of-range is ignored.
    void switchToIndex(uint8_t index);
    IScreenUI* getCurrentScreen() const;

    // Zero-based index of the active page — persisted to RTC memory so a wake can
    // restore the last-active page instead of always starting on Navigation
    uint8_t currentIndex() const { return _current; }

    // Re-assert the current page's visibility (after a wake re-render, or when the
    // page menu is dismissed and the last-active page comes back)
    void showCurrent();

    // Hide the current page container without leaving it (the page menu covers it)
    void hideCurrent();

    uint8_t screenCount() const { return _screen_count; }

    // Hide every registered page container. Usable before begin() — the startup screen
    // needs a clean screen, and the SquareLine export may leave a page visible.
    void hideAll();

private:

    void switchTo(uint8_t index);

    uint8_t nextIdx() const { return (_current + 1) % _screen_count; }
    uint8_t previousIdx() const { return (_current + _screen_count - 1) % _screen_count; }

    IScreenUI* _screens[MAX_SCREENS] = {};
    uint8_t _screen_count = 0;
    uint8_t _current = 0;
    bool _initialized = false;

};
