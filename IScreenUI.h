#pragma once

#include <Arduino.h>
#include <lvgl.h>

// === I N T E R F A C E  I S C R E E N U I ===
//
// - Abstract base class for all UI screen adapters
// - Defines the interface used by ScreenManager and CrowPanelApplication
// - Inherited by: CompassUI, AttitudeUI, BrightnessUI, WeatherUI, ...
//
// Lifecycle (managed by CrowPanelApplication + ScreenManager):
//   begin()              — called by CrowPanelApplication before addScreen()
//   getLvglScreen()      — returns the LVGL lv_obj_t* for this screen
//   onEnter()            — called by ScreenManager when screen becomes active
//   onLeave()            — called by ScreenManager before switching away (cleanup)
//   update()             — called by CrowPanelApplication at UI_UPDATE_INTERVAL_MS
//   onButtonPress()      — called by CrowPanelApplication on knob button press
//                          (a touch TAP is routed here too — tap == button press)
//   onRotation(dir)      — called by CrowPanelApplication on knob rotation
//                          only if interceptsRotation() returns true
//   interceptsRotation() — return true to consume rotation events instead of
//                          switching screens. NO page currently does: SetupUI was the
//                          last one and is now touch-only, so rotation always switches
//                          screens. The seam is kept for a future page that needs it.
//
// Touch input (parallel to the knob, dispatched by CrowPanelApplication::handleTouch;
// coordinates are in the panel's pixel space). A page opts into raw-touch handling
// only for absolute-position gestures the knob can't produce (e.g. a slider drag);
// swipe-navigation and tap are handled at the app level and need no page hooks:
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
    virtual void onRotation(int8_t dir){}

    virtual bool interceptsRotation() const { return false; }

    virtual bool interceptsTouchAt(int16_t x, int16_t y) { return false; }
    virtual void onTouchDrag(int16_t x, int16_t y){}
    virtual void onTouchEnd(){}

};
