#include "BatteryMonitor.h"

// LiPo single-cell resting-curve lookup: cell millivolts → State-of-Charge %.
// Coarse but honest (no fuel-gauge IC on this board); the light ~0.6C load means
// modest sag, so the resting curve is adequate. Points are strictly descending in
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

void BatteryMonitor::begin() {
    // 11 dB attenuation → ~0..3.1 V ADC range; the divided cell (1.73..2.43 V for a
    // 3.0..4.2 V LiPo) sits comfortably inside it. analogReadMilliVolts() applies the
    // per-chip eFuse calibration, so no manual reference math is needed.
    analogSetPinAttenuation(BATTERY_ADC_PIN, ADC_11db);

    // USB-present (GPIO17, HIGH) + charge-status (GPIO15) — match Lesson11 (plain INPUT,
    // no assumed pull). GPIO15 is only logged; its polarity is undocumented.
    pinMode(USB_DETECT_PIN, INPUT);
    pinMode(CHARGE_STATUS_PIN, INPUT);

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
            Serial.printf("[battery] USB present (charge-pin=%d) -> charging\n",
                          digitalRead(CHARGE_STATUS_PIN));
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
    Serial.printf("[battery] cell %u mV (ema %u) -> SoC %u%%\n",
                  cell_mv, (uint16_t)(_ema_mv + 0.5f), _soc);
    this->render();
}

// === P R I V A T E ===

// Average ADC_SAMPLES calibrated reads of the divider node, then scale back up to the
// true cell voltage. Averaging beats ADC noise; sampling is cheap and only runs awake.
uint16_t BatteryMonitor::sampleCellMillivolts() const {
    uint32_t acc = 0;
    for (uint8_t i = 0; i < ADC_SAMPLES; ++i) {
        acc += analogReadMilliVolts(BATTERY_ADC_PIN);
    }
    const uint32_t adc_mv = acc / ADC_SAMPLES;
    return (uint16_t)((adc_mv * DIVIDER_NUM) / DIVIDER_DEN);
}

// Linear interpolation over the LiPo resting curve, clamped to 0..100.
uint8_t BatteryMonitor::estimateSoc(uint16_t cell_mv) const {
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

// Classify the current (smoothed) reading into a ladder level. No hysteresis here —
// escalateLevel() provides that by refusing to go back down within an awake session.
// CUTOFF trips on either a near-empty SoC or the hard voltage floor, whichever first.
BatteryMonitor::Level BatteryMonitor::classify() const {
    if (_charging || !_seeded) return Level::OK;

    const uint16_t mv = this->cellMillivolts();
    if (_soc <= CUTOFF_AT_OR_BELOW   || mv <= CUTOFF_CELL_MV) return Level::CUTOFF;
    if (_soc <= CRITICAL_AT_OR_BELOW)                         return Level::CRITICAL;
    if (_soc <= WARN_AT_OR_BELOW)                             return Level::WARN;
    return Level::OK;
}

// One-way escalation: the level can only get worse while awake. The ~145 mA active load
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

// Push the current state to the bar, and recolor the indicator only when the bucket
// changes — avoids redundant style invalidations every sample. Buckets:
//   3 = charging (blue, forced 100 %), 2 = green > 40 %, 1 = amber > 20 %, 0 = red.
void BatteryMonitor::render() {
    if (ui_BarBatteryIndicator == nullptr) return;

    const int8_t bucket = _charging ? 3
                        : (_soc > GREEN_ABOVE) ? 2
                        : (_soc > AMBER_ABOVE) ? 1
                                               : 0;
    const int32_t value = _charging ? 100 : _soc;
    lv_bar_set_value(ui_BarBatteryIndicator, value, LV_ANIM_OFF);

    if (bucket == _last_bucket) return;
    _last_bucket = bucket;

    const uint32_t color = (bucket == 3) ? COLOR_BLUE
                         : (bucket == 2) ? COLOR_GREEN
                         : (bucket == 1) ? COLOR_AMBER
                                         : COLOR_RED;
    lv_obj_set_style_bg_color(ui_BarBatteryIndicator, lv_color_hex(color),
                              LV_PART_INDICATOR | LV_STATE_DEFAULT);
}
