#include "ScreenManager.h"

// === P U B L I C ===

// Register a page (container-backed) — must be called before begin()
void ScreenManager::addScreen(IScreenUI* screen) {
    if (_screen_count >= MAX_SCREENS) return;
    _screens[_screen_count++] = screen;
}

// Initialize: hide every page container, show start_index, mark ready.
// start_index defaults to 0 (Navigation); a button-wake passes the restored
// last-active page. Clamped so a corrupted RTC value falls back to page 0.
void ScreenManager::begin(uint8_t start_index) {
    if (_initialized || _screen_count == 0) return;
    _initialized = true;
    if (start_index >= _screen_count) start_index = 0;
    _current = start_index;
    this->hideAll();
    lv_obj_remove_flag(_screens[_current]->getLvglScreen(), LV_OBJ_FLAG_HIDDEN);
    _screens[_current]->onEnter();
}

// CW carousel step
void ScreenManager::switchNext() {
    if (_screen_count <= 1) return;
    this->switchTo(this->nextIdx(), Direction::CW);
}

// CCW carousel step
void ScreenManager::switchPrevious() {
    if (_screen_count <= 1) return;
    this->switchTo(this->previousIdx(), Direction::CCW);
}

// Return pointer to the active page
IScreenUI* ScreenManager::getCurrentScreen() const {
    if (_screen_count == 0) return nullptr;
    return _screens[_current];
}

// Re-assert current page visibility (e.g. after a wake redraw)
void ScreenManager::showCurrent() {
    if (!_initialized) return;
    this->hideAll();
    lv_obj_remove_flag(_screens[_current]->getLvglScreen(), LV_OBJ_FLAG_HIDDEN);
}

// === P R I V A T E ===

// Hide every registered page container
void ScreenManager::hideAll() {
    for (uint8_t i = 0; i < _screen_count; ++i) {
        lv_obj_add_flag(_screens[i]->getLvglScreen(), LV_OBJ_FLAG_HIDDEN);
    }
}

// Unified page switch: cleanup current, show target container, notify new
void ScreenManager::switchTo(uint8_t index, Direction dir) {
    if (!_initialized) return;
    if (index == _current) return;

    _screens[_current]->onLeave();
    lv_obj_add_flag(_screens[_current]->getLvglScreen(), LV_OBJ_FLAG_HIDDEN);

    _current = index;

    lv_obj_remove_flag(_screens[index]->getLvglScreen(), LV_OBJ_FLAG_HIDDEN);
    _screens[index]->onEnter();
}
