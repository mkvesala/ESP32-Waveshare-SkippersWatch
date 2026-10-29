#pragma once

#include <Arduino.h>
#include "CelestialCalculator.h"
#include "CelestialSelector.h"
#include "NavStars.h"

// === C L A S S  C E L E S T I A L E N G I N E ===
//
// - Class CelestialEngine - runs the celestial computations off the UI loop
// - FreeRTOS task "Celestial", core 0, priority 1 (main loop and LVGL on core 1). Double precision
//   astronomy is software emulated on the ESP32-S3: tens of ms per body, a few hundred ms for a
//   full selection pass — far too long for the 59 ms UI tick.
// - Computes once per second, and only while the Celestial page is active (setActive()) AND the
//   page keeps feeding inputs (setInputs() at least every INPUT_HEARTBEAT_MS). CelestialUI calls
//   setInputs() from update(), which the app skips while the Menu/Status overlay covers the page,
//   so computation pauses there too. Inactive: blocked on a task notification, no CPU used.
// - Every second: Sun, Moon (visible between their rise and set), the selected planet and stars.
//   In twilight, every 60 s (or at once when a selected body leaves the hard window): all 4
//   planets + 57 stars → CelestialSelector (planet + 2 stars, or 3 stars).
// - Inputs (time anchor + position) and the result are exchanged under a spinlock; the UI never
//   touches Astronomy Engine, so the library is only ever used from this task.
// - A new Inputs::source_tag (live GNSS <-> stored position) wakes the task for an immediate
//   computation; the result echoes the tag so the UI can drop results of the old source.
// - Uses: CelestialCalculator, CelestialSelector, NavStars
// - Owned by: CelestialUI (Skipper's Watch; ported from ESP32-Crowpanel-compass)

class CelestialEngine {

public:

    struct Inputs {
        double   unix_utc      = 0.0;   // UTC (s since 1970) at anchor_millis
        uint32_t anchor_millis = 0;     // millis() at which unix_utc was true
        double   lat_deg       = 0.0;
        double   lon_deg       = 0.0;
        bool     valid         = false; // position AND time usable when the UI sampled them
        uint32_t source_tag    = 0;     // changed by the UI when the position source switches
    };

    struct Body {
        bool   visible = false;
        double hc_deg  = 0.0;
        double zn_deg  = 0.0;
    };

    struct Result {
        uint32_t seq             = 0;      // increments with every computation, 0 = none yet
        uint32_t computed_millis = 0;      // millis() at which the positions are valid
        bool     valid           = false;  // false: inputs were stale, positions meaningless
        bool     twilight        = false;
        Body     sun;
        Body     moon;
        int8_t   planet          = CelestialSelector::NONE;  // CelestialCalculator::Planet index
        Body     planet_pos;
        int8_t   star[CelestialSelector::MAX_STARS] = {   // NavStars index, filled from [0]
            CelestialSelector::NONE, CelestialSelector::NONE, CelestialSelector::NONE };
        Body     star_pos[CelestialSelector::MAX_STARS];
        uint32_t compute_us      = 0;
        uint32_t source_tag      = 0;      // Inputs::source_tag this result was computed from
    };

    explicit CelestialEngine();

    void begin();
    void setActive(bool active);
    void setInputs(const Inputs& inputs);
    Result getResult() const;

    // Diagnostics
    uint32_t getMaxComputeMicros() const;
    UBaseType_t getStackHighWaterMark() const;

private:

    static void taskEntry(void* param);
    void run();
    void compute(const Inputs& in, uint32_t start_ms, Result& r);
    void selectionPass(const Body& moon);
    static Body toBody(const CelestialCalculator::Position& pos, bool visible);
    static CelestialSelector::Candidate toCandidate(const CelestialCalculator::Position& pos, float vmag);

    static constexpr uint32_t COMPUTE_INTERVAL_MS   = 1000;
    static constexpr uint32_t SELECTION_INTERVAL_MS = 60000;
    static constexpr uint32_t INPUT_HEARTBEAT_MS    = 2000;
    static constexpr uint32_t TASK_STACK_BYTES      = 16384;
    static constexpr UBaseType_t TASK_PRIORITY      = 1;
    static constexpr BaseType_t TASK_CORE           = 0;

    // Shared with the UI (core 1), guarded by _spinlock
    mutable portMUX_TYPE _spinlock = portMUX_INITIALIZER_UNLOCKED;
    Inputs   _inputs;
    uint32_t _inputs_millis = 0;   // millis() of the last setInputs(), 0 = none
    Result   _result;
    bool     _active = false;
    uint32_t _max_compute_us = 0;

    // Task-only state
    TaskHandle_t        _task = nullptr;
    CelestialCalculator _calc;
    CelestialSelector   _selector;
    CelestialSelector::Candidate _planet_cand[CelestialCalculator::PLANET_COUNT];
    CelestialSelector::Candidate _star_cand[NavStars::COUNT];
    uint32_t _last_selection_ms = 0;
    bool     _selection_due = true;
    uint32_t _seq = 0;

};
