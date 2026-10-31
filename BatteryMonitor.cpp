#include "BatteryMonitor.h"
#define XPOWERS_CHIP_AXP2101
#include <XPowersLib.h>

// LiPo single-cell resting-curve lookup: cell millivolts → State-of-Charge %.
// Fallback only, for when the AXP2101 gauge reports no percentage. Also logged next to
// the gauge value so a full discharge shows how far the two agree [M]. Points are strictly descending in
// both columns; estimateSoc() interpolates linearly between neighbours.
namespace {
    struct SocPoint { uint16_t mv; uint8_t soc; };
    constexpr SocPoint kSocCurve[] = {
        { 4200, 100 }, { 4100, 90 }, { 4000, 80 }, { 3930, 70 },
        { 3870,  60 }, { 3820, 50 }, { 3790, 40 }, { 3770, 30 },
        { 3740,  20 }, { 3680, 10 }, { 3450,  5 }, { 3000,  0 },
    };
    constexpr size_t kSocCurveLen = sizeof(kSocCurve) / sizeof(kSocCurve[0]);
}

// === P U B L I C ===

bool BatteryMonitor::beginPmu(TwoWire& wire, int sda, int scl) {
    if (_pmu != nullptr) return true;
    XPowersAXP2101* pmu = new XPowersAXP2101();
    if (!pmu->begin(wire, AXP2101_SLAVE_ADDRESS, sda, scl)) {
        Serial.println("WARN: AXP2101 not detected — battery bar unavailable");
        delete pmu;
        return false;
    }
    // As in Waveshare's 05_LVGL_AXP2101_ADC_Data: IRQs off and cleared, 4.2 V charge
    // target. Rails and the ADC channels are PmuRails' job (beginAwake() runs next).
    pmu->disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
    pmu->clearIrqStatus();
    pmu->setChargeTargetVoltage(XPOWERS_AXP2101_CHG_VOL_4V2);
    _pmu = pmu;
    return true;
}

void BatteryMonitor::begin() {
    _last_sample_ms = millis();

    // On USB power the cell voltage is charger-elevated and meaningless for SoC, so
    // show the charging visual straight away and skip seeding (unplug re-seeds fresh).
    if (usbPresent()) {
        _charging = true;
        _seeded = false;
        _level = Level::OK;      // charger-elevated voltage says nothing about SoC
    } else {
        // Seed immediately so the bar shows a real value within the short awake window
        // on every wake — no 75 % dummy flash, no ramp-up from zero.
        _ema_mv = (float)sampleCellMillivolts();
        _seeded = true;
        _soc = estimateSoc((uint16_t)(_ema_mv + 0.5f));
        // Classify from that very first sample: the wake path needs a level BEFORE it
        // ramps the backlight or brings the radio up on a possibly dying cell.
        _level = Level::OK;
        this->escalateLevel();
    }
    this->render();
}

void BatteryMonitor::update(uint32_t now) {
    if (now - _last_sample_ms < SAMPLE_INTERVAL_MS) return;
    _last_sample_ms = now;

    // On external power: force the charging visual (blue/100), don't trust the elevated
    // cell voltage for SoC. Keep _seeded=false so the EMA re-seeds from a fresh resting
    // sample on unplug instead of lingering at the charge voltage.
    if (usbPresent()) {
        _charging = true;
        _seeded = false;
        _level = Level::OK;   // nothing to protect on external power
        // Log on CHANGE only: the watch now stays awake indefinitely on USB
        // (PowerManager STAY_AWAKE_ON_USB), so a per-sample line would never stop.
        if (!_charge_logged) {
            Serial.printf("[battery] USB present (charging=%d, batt %u mV) -> external power\n",
                          (_pmu != nullptr) ? (int)_pmu->isCharging() : -1,
                          this->sampleCellMillivolts());
            _charge_logged = true;
        }
        this->render();
        return;
    }
    if (_charge_logged) {
        Serial.println("[battery] USB removed -> on battery");
        _charge_logged = false;
    }
    _charging = false;

    const uint16_t cell_mv = sampleCellMillivolts();
    if (_seeded) {
        _ema_mv += EMA_ALPHA * ((float)cell_mv - _ema_mv);
    } else {
        _ema_mv = (float)cell_mv;   // fresh seed after unplug (or if begin() was on USB)
        _seeded = true;
    }

    _soc = estimateSoc((uint16_t)(_ema_mv + 0.5f));
    this->escalateLevel();
    Serial.printf("[battery] cell %u mV (ema %u) -> SoC %u%% (gauge %d%%, curve %u%%)\n",
                  cell_mv, (uint16_t)(_ema_mv + 0.5f), _soc,
                  (_pmu != nullptr) ? _pmu->getBatteryPercent() : -1,
                  this->curveSoc((uint16_t)(_ema_mv + 0.5f)));
    this->render();
}

void BatteryMonitor::setDemoSuffix(bool on) {
    if (on == _demo_suffix) return;
    _demo_suffix = on;
    _label_dirty = true;
    this->render();
}

void BatteryMonitor::setNightMode(bool on) {
    if (on == _night) return;
    _night = on;
    _last_bucket = -1;   // force the recolor
    this->render();
}

// === P R I V A T E ===

// No PMIC → report external power: the bar shows charging and the ladder stays at OK,
// so a missing gauge can never trigger a protective sleep on a healthy cell.
bool BatteryMonitor::usbPresent() const {
    if (_pmu == nullptr) return true;
    return _pmu->isVbusIn();
}

uint16_t BatteryMonitor::sampleCellMillivolts() const {
    if (_pmu == nullptr) return 0;
    return _pmu->getBattVoltage();
}

// Prefer the AXP2101 fuel gauge; fall back to the resting curve when it reports nothing
// (no battery detected, or a value outside 0..100).
uint8_t BatteryMonitor::estimateSoc(uint16_t cell_mv) const {
    if (_pmu != nullptr && _pmu->isBatteryConnect()) {
        const int pct = _pmu->getBatteryPercent();
        if (pct >= 0 && pct <= 100) return (uint8_t)pct;
    }
    return this->curveSoc(cell_mv);
}

// Linear interpolation over the LiPo resting curve, clamped to 0..100.
uint8_t BatteryMonitor::curveSoc(uint16_t cell_mv) const {
    if (cell_mv >= kSocCurve[0].mv)                return kSocCurve[0].soc;            // full
    if (cell_mv <= kSocCurve[kSocCurveLen - 1].mv) return kSocCurve[kSocCurveLen - 1].soc; // empty

    for (size_t i = 1; i < kSocCurveLen; ++i) {
        if (cell_mv >= kSocCurve[i].mv) {
            const SocPoint& hi = kSocCurve[i - 1];  // higher mV / higher SoC
            const SocPoint& lo = kSocCurve[i];      // lower  mV / lower  SoC
            const uint16_t span_mv  = hi.mv  - lo.mv;
            const uint8_t  span_soc = hi.soc - lo.soc;
            return lo.soc + (uint8_t)(((uint32_t)(cell_mv - lo.mv) * span_soc + span_mv / 2) / span_mv);
        }
    }
    return 0;  // unreachable (guarded above)
}

// Classify the current (smoothed) reading into a ladder level (rules in BatteryLevel.h).
// No hysteresis here — escalateLevel() refuses to go back down within an awake session.
BatteryMonitor::Level BatteryMonitor::classify() const {
    if (_charging || !_seeded) return Level::OK;
    return BatteryLadder::classify(_soc, this->cellMillivolts());
}

// One-way escalation: the level can only get worse while awake. The active load
// sags the cell enough that a reading can hop back over a threshold; a ladder that
// flapped would flip the display timeout and backlight ceiling back and forth.
void BatteryMonitor::escalateLevel() {
    const Level now_level = this->classify();
    if ((uint8_t)now_level <= (uint8_t)_level) return;
    _level = now_level;
    Serial.printf("[battery] level -> %s\n",
                  _level == Level::CUTOFF   ? "CUTOFF"
                : _level == Level::CRITICAL ? "CRITICAL"
                : _level == Level::WARN     ? "WARN"
                                            : "OK");
}

// On battery: the same SoC the bar shows. On USB the bar is forced to a full blue, but
// the label shows the gauge's live percentage so charging progress is visible (the
// gauge counts charge; it is not fooled by the charger-elevated voltage like the curve).
int16_t BatteryMonitor::labelPercent() const {
    if (!_charging) return _soc;
    if (_pmu == nullptr || !_pmu->isBatteryConnect()) return -1;
    const int pct = _pmu->getBatteryPercent();
    return (pct >= 0 && pct <= 100) ? (int16_t)pct : -1;
}

// Push the current state to the bar, and recolor the indicator only when the bucket
// changes — avoids redundant style invalidations every sample. Buckets:
//   3 = charging (blue, forced 100 %), 2 = green > 40 %, 1 = amber > 20 %, 0 = red.
// The label is rewritten only when its text would change.
void BatteryMonitor::render() {
    if (ui_LabelSoC != nullptr) {
        const int16_t pct = this->labelPercent();
        if (_label_dirty || pct != _label_pct) {
            _label_pct = pct;
            _label_dirty = false;
            const char* suffix = _demo_suffix ? " DEMO" : "";
            char buf[16];
            if (pct < 0) snprintf(buf, sizeof(buf), "-- %%%s", suffix);
            else         snprintf(buf, sizeof(buf), "%d %%%s", pct, suffix);
            lv_label_set_text(ui_LabelSoC, buf);
        }
    }

    if (ui_BarBatteryIndicator == nullptr) return;

    const int8_t bucket = _charging ? 3
                        : (_soc > GREEN_ABOVE) ? 2
                        : (_soc > AMBER_ABOVE) ? 1
                                               : 0;
    const int32_t value = _charging ? 100 : _soc;
    lv_bar_set_value(ui_BarBatteryIndicator, value, LV_ANIM_OFF);

    if (bucket == _last_bucket) return;
    _last_bucket = bucket;

    const uint32_t color = (bucket == 3) ? (_night ? NIGHT_BLUE  : COLOR_BLUE)
                         : (bucket == 2) ? (_night ? NIGHT_GREEN : COLOR_GREEN)
                         : (bucket == 1) ? (_night ? NIGHT_AMBER : COLOR_AMBER)
                                         : (_night ? NIGHT_RED   : COLOR_RED);
    lv_obj_set_style_bg_color(ui_BarBatteryIndicator, lv_color_hex(color),
                              LV_PART_INDICATOR | LV_STATE_DEFAULT);
}
