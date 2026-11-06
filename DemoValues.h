#pragma once

#include <stdint.h>
#include <math.h>
#include "DemoTrack.h"

// === D E M O  V A L U E S ===
//
// - Instrument values for demo mode, as a pure function of time: DemoTrack's recorded rows,
//   one per second, looping back to the start. No Arduino, LVGL or protocol types, so host
//   unit tests can check that every value stays in a range the pages accept (no NaN, no
//   placeholder-triggering value).
// - DemoSource turns a snapshot into ESP-NOW wire structs.

struct DemoSnapshot {
    float heading_true_deg;   // 0..360
    float mag_var_deg;        // easterly variation, + = E
    float pitch_deg;
    float roll_deg;
    float cog_true_deg;       // 0..360
    float sog_kn;
    float lat_deg;
    float lon_deg;
    float depth_below_surface_m;
    float house_v;
    float house_a;            // negative = charging
    float house_soc_pct;
    float start_v;
    float temperature_c;
    float pressure_hpa;
    float humidity_pct;
    float exhaust_k;
    float fuel_ratio;         // 0..1
    float water_ratio;        // 0..1
};

struct DemoValues {

    // Where the demo track starts and how long before it loops back
    static constexpr float START_LAT_DEG = 60.2708f;   // 60°16.2'N
    static constexpr float START_LON_DEG = 26.0097f;   // 026°00.6'E
    static constexpr float TRACK_LOOP_S  = (float)DemoTrack::ROWS;

    // Demo clock while the system clock has no GNSS time: this epoch plus uptime. The time of
    // the track's first row, shared by the Clock page and the Celestial page, which computes
    // real Hc/Zn for this time and the demo position.
    static constexpr int64_t DEMO_EPOCH_UTC = DemoTrack::START_UTC;

    static DemoSnapshot at(uint32_t ms) {
        const DemoTrack::Row& r = DemoTrack::TRACK[(ms / 1000u) % DemoTrack::ROWS];
        DemoSnapshot d{};

        d.heading_true_deg = wrap360(r.hdg_true_e4 / 1e4f * RAD_TO_DEG_F);
        d.mag_var_deg      = 11.0f;
        d.pitch_deg        = r.pitch_e4 / 1e4f * RAD_TO_DEG_F;
        d.roll_deg         = r.roll_e4 / 1e4f * RAD_TO_DEG_F;

        d.cog_true_deg     = wrap360(r.cog_e4 / 1e4f * RAD_TO_DEG_F);
        d.sog_kn           = r.sog_mms / 1000.0f * MS_TO_KN;

        d.lat_deg = (float)(r.lat_e7 / 1e7);
        d.lon_deg = (float)(r.lon_e7 / 1e7);

        d.depth_below_surface_m = r.depth_surf_cm / 100.0f;

        d.house_v       = r.house_v_mv / 1000.0f;
        d.house_a       = r.house_i_ca / 100.0f;
        d.house_soc_pct = r.soc_e2 / 100.0f;
        d.start_v       = r.start_v_mv / 1000.0f;

        d.temperature_c = r.temp_c_e2 / 100.0f;
        d.pressure_hpa  = (float)((int32_t)r.press_pa_o + DemoTrack::PRESS_OFFSET) / 100.0f;
        d.humidity_pct  = r.hum_e2 / 100.0f;

        d.exhaust_k   = r.exhaust_k_e2 / 100.0f;
        d.fuel_ratio  = r.fuel_e4 / 1e4f;
        d.water_ratio = r.water_e4 / 1e4f;
        return d;
    }

    static float wrap360(float deg) {
        float r = fmodf(deg, 360.0f);
        return r < 0.0f ? r + 360.0f : r;
    }

private:

    static constexpr float RAD_TO_DEG_F = 180.0f / (float)M_PI;
    static constexpr float MS_TO_KN     = 1.94384f;

};
