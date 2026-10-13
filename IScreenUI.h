#pragma once

#include <Arduino.h>
#include <lvgl.h>

// === I N T E R F A C E  I S C R E E N U I ===
//
// - Abstract base class for all UI screen adapters
// - Defines the interface used by ScreenManager and WatchApplication
// - Inherited by: CompassUI, AttitudeUI, BrightnessUI, WeatherUI, ...
//
// Lifecycle (managed by WatchApplication + ScreenManager):
//   begin()              — called by WatchApplication before addScreen()
//   getLvglScreen()      — returns the LVGL lv_obj_t* for this screen
//   onEnter()            — called by ScreenManager when screen becomes active
//   onLeave()            — called by ScreenManager before switching away (cleanup)
//   update()             — called by WatchApplication at UI_UPDATE_INTERVAL_MS
//   onButtonPress()      — called by WatchApplication on a touch TAP on the page
//
// Touch input (dispatched by WatchApplication::handleTouch; coordinates are in the
// panel's pixel space). A page opts into raw-touch handling only for absolute-position
// gestures (e.g. a slider drag); swipe navigation, the page menu and tap are handled at
// the app level and need no page hooks:
//   interceptsTouchAt(x,y) — return true on touch-DOWN to claim this touch as a drag
//                            (e.g. the point is on the page's slider). When claimed,
//                            the app forwards onTouchDrag/onTouchEnd and suppresses
//                            the swipe/tap classification for that touch. Deliberately
//                            NOT const: a page with several controls also records which
//                            one was claimed, since the drag/end pair that follows
//                            cannot always identify it (SetupUI's switch).
//   onTouchDrag(x,y)       — live drag position while the claimed touch is held
//   onTouchEnd()           — the claimed touch was released (persist here)

class IScreenUI {

public:

    virtual ~IScreenUI() = default;

    virtual void begin() = 0;
    virtual lv_obj_t* getLvglScreen() const = 0;

    virtual void onEnter(){}
    virtual void onLeave(){}
    virtual void update(){}
    virtual void onButtonPress(){}

    virtual bool interceptsTouchAt(int16_t x, int16_t y) { return false; }
    virtual void onTouchDrag(int16_t x, int16_t y){}
    virtual void onTouchEnd(){}

};
