#include "CelestialEngine.h"

// === P U B L I C ===

// Constructor
CelestialEngine::CelestialEngine() {}

// Start the task (idle until setActive(true))
void CelestialEngine::begin() {
    if (_task) return;
    xTaskCreatePinnedToCore(
        taskEntry,           // Callback
        "Celestial",         // Name
        TASK_STACK_BYTES,    // Stack size
        this,                // Param (this)
        TASK_PRIORITY,       // Priority
        &_task,              // Handle
        TASK_CORE            // Core
    );
}

// Screen entered / left. Entering wakes the task for an immediate computation.
void CelestialEngine::setActive(bool active) {
    portENTER_CRITICAL(&_spinlock);
    _active = active;
    portEXIT_CRITICAL(&_spinlock);
    if (active && _task) xTaskNotifyGive(_task);
}

// Latest time anchor and position from the UI. A switched position source computes at once.
void CelestialEngine::setInputs(const Inputs& inputs) {
    portENTER_CRITICAL(&_spinlock);
    const bool source_changed = inputs.source_tag != _inputs.source_tag;
    _inputs = inputs;
    _inputs_millis = millis();
    portEXIT_CRITICAL(&_spinlock);
    if (source_changed && _task) xTaskNotifyGive(_task);
}

// Copy of the latest result
CelestialEngine::Result CelestialEngine::getResult() const {
    Result r;
    portENTER_CRITICAL(&_spinlock);
    r = _result;
    portEXIT_CRITICAL(&_spinlock);
    return r;
}

uint32_t CelestialEngine::getMaxComputeMicros() const {
    uint32_t v;
    portENTER_CRITICAL(&_spinlock);
    v = _max_compute_us;
    portEXIT_CRITICAL(&_spinlock);
    return v;
}

UBaseType_t CelestialEngine::getStackHighWaterMark() const {
    return _task ? uxTaskGetStackHighWaterMark(_task) : 0;
}

// === P R I V A T E ===

void CelestialEngine::taskEntry(void* param) {
    static_cast<CelestialEngine*>(param)->run();
}

// Task loop: compute at 1 Hz while active and fed, otherwise sleep until notified
void CelestialEngine::run() {
    for (;;) {
        bool active;
        Inputs in;
        uint32_t inputs_ms;
        portENTER_CRITICAL(&_spinlock);
        active = _active;
        in = _inputs;
        inputs_ms = _inputs_millis;
        portEXIT_CRITICAL(&_spinlock);

        if (!active) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            _selection_due = true;   // re-evaluate the set on (re)entry, with hysteresis
            continue;
        }

        // Page covered by an overlay (update() not running): no inputs, no computation.
        // Checked once per interval; the next setInputs() resumes within a second.
        if (inputs_ms == 0 || (uint32_t)(millis() - inputs_ms) >= INPUT_HEARTBEAT_MS) {
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(COMPUTE_INTERVAL_MS));
            continue;
        }

        uint32_t start_ms = millis();
        uint32_t t0 = micros();

        Result r;
        if (in.valid) this->compute(in, start_ms, r);
        r.seq = ++_seq;
        r.computed_millis = start_ms;
        r.valid = in.valid;
        r.source_tag = in.source_tag;
        r.compute_us = micros() - t0;

        portENTER_CRITICAL(&_spinlock);
        _result = r;
        if (r.compute_us > _max_compute_us) _max_compute_us = r.compute_us;
        portEXIT_CRITICAL(&_spinlock);

        uint32_t elapsed = millis() - start_ms;
        uint32_t wait_ms = (elapsed < COMPUTE_INTERVAL_MS) ? (COMPUTE_INTERVAL_MS - elapsed) : 1;
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(wait_ms));
    }
}

// One computation for the UTC at start_ms
void CelestialEngine::compute(const Inputs& in, uint32_t start_ms, Result& r) {
    double unix_utc = in.unix_utc + (double)(int32_t)(start_ms - in.anchor_millis) / 1000.0;
    _calc.set(unix_utc, in.lat_deg, in.lon_deg);

    CelestialCalculator::Position sun = _calc.sun();
    CelestialCalculator::Position moon = _calc.moon();
    r.sun  = toBody(sun,  sun.valid  && _calc.isSunUp());    // between sunrise and sunset
    r.moon = toBody(moon, moon.valid && _calc.isMoonUp());   // between moonrise and moonset
    r.twilight = sun.valid && CelestialCalculator::isTwilight(sun.hc_deg);

    if (!r.twilight) {
        _selector.reset();
        _selection_due = true;   // first twilight second selects at once
        return;
    }

    bool pass_done = false;
    if (_selection_due || (start_ms - _last_selection_ms) >= SELECTION_INTERVAL_MS) {
        this->selectionPass(r.moon);
        _last_selection_ms = start_ms;
        _selection_due = false;
        pass_done = true;
    }

    // Selected bodies for this second (a pass has just computed them for this very time)
    const CelestialSelector::Selection& sel = _selector.current();
    bool still_valid = true;

    if (sel.planet != CelestialSelector::NONE) {
        CelestialCalculator::Planet p = static_cast<CelestialCalculator::Planet>(sel.planet);
        if (!pass_done) _planet_cand[sel.planet] = toCandidate(_calc.planet(p), CelestialCalculator::planetMagnitude(p));
        still_valid &= CelestialSelector::inHardWindow(_planet_cand[sel.planet]);
    }
    for (int8_t s : sel.star) {
        if (s == CelestialSelector::NONE) continue;
        if (!pass_done) _star_cand[s] = toCandidate(_calc.star(s), NavStars::TABLE[s].vmag);
        still_valid &= CelestialSelector::inHardWindow(_star_cand[s]);
    }

    // A member left the hard window: reselect now rather than show it for up to a minute
    if (!still_valid && !pass_done) {
        this->selectionPass(r.moon);
        _last_selection_ms = start_ms;
    }

    const CelestialSelector::Selection& out = _selector.current();
    if (out.planet != CelestialSelector::NONE) {
        const CelestialSelector::Candidate& c = _planet_cand[out.planet];
        r.planet = out.planet;
        r.planet_pos.visible = true;
        r.planet_pos.hc_deg = c.hc_deg;
        r.planet_pos.zn_deg = c.zn_deg;
    }
    for (uint8_t i = 0; i < CelestialSelector::MAX_STARS; i++) {
        if (out.star[i] == CelestialSelector::NONE) continue;
        const CelestialSelector::Candidate& c = _star_cand[out.star[i]];
        r.star[i] = out.star[i];
        r.star_pos[i].visible = true;
        r.star_pos[i].hc_deg = c.hc_deg;
        r.star_pos[i].zn_deg = c.zn_deg;
    }
}

// All planets and stars for the current time, then CelestialSelector (with hysteresis)
void CelestialEngine::selectionPass(const Body& moon) {
    for (uint8_t i = 0; i < CelestialCalculator::PLANET_COUNT; i++) {
        CelestialCalculator::Planet p = static_cast<CelestialCalculator::Planet>(i);
        _planet_cand[i] = toCandidate(_calc.planet(p), CelestialCalculator::planetMagnitude(p));
    }
    for (uint8_t i = 0; i < NavStars::COUNT; i++) {
        _star_cand[i] = toCandidate(_calc.star(i), NavStars::TABLE[i].vmag);
    }

    CelestialSelector::Candidate moon_cand;
    moon_cand.hc_deg = (float)moon.hc_deg;
    moon_cand.zn_deg = (float)moon.zn_deg;
    moon_cand.valid  = moon.visible;

    _selector.update(_planet_cand, CelestialCalculator::PLANET_COUNT,
                     _star_cand, NavStars::COUNT, &moon_cand);
}

CelestialEngine::Body CelestialEngine::toBody(const CelestialCalculator::Position& pos, bool visible) {
    Body b;
    b.visible = visible;
    b.hc_deg = pos.hc_deg;
    b.zn_deg = pos.zn_deg;
    return b;
}

CelestialSelector::Candidate CelestialEngine::toCandidate(const CelestialCalculator::Position& pos, float vmag) {
    CelestialSelector::Candidate c;
    c.hc_deg = (float)pos.hc_deg;
    c.zn_deg = (float)pos.zn_deg;
    c.vmag = vmag;
    c.valid = pos.valid;
    return c;
}
