#include "ScreenManager.h"

// === P U B L I C ===

// Register a page (container-backed) — must be called before begin()
void ScreenManager::addScreen(IScreenUI* screen) {
    if (_screen_count >= MAX_SCREENS) return;
    _screens[_screen_count++] = screen;
}

// Initialize: hide every page container, show page 0, mark ready
void ScreenManager::begin() {
    if (_initialized || _screen_count == 0) return;
    _initialized = true;
    _current = 0;
    this->hideAll();
    lv_obj_remove_flag(_screens[0]->getLvglScreen(), LV_OBJ_FLAG_HIDDEN);
    _screens[0]->onEnter();
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
