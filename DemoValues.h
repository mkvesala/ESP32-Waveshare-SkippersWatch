#pragma once

#include <stdint.h>
#include <math.h>

// === D E M O  V A L U E S ===
//
// - Made-up but plausible instrument values for demo mode, as a pure function of time.
//   No Arduino, LVGL or protocol types, so host unit tests can check that every value
//   stays in a range the pages accept (no NaN, no placeholder-triggering value).
// - Values drift slowly (sines with different periods) so the pages visibly live, the
//   way they do on the boat: under way on a westerly course in the Gulf of Finland.
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

    // Start of the demo track and how long before it loops back (keeps the position
    // inside a sensible box no matter how long demo mode runs)
    static constexpr float START_LAT_DEG = 60.0833f;   // 60°05.0'N
    static constexpr float START_LON_DEG = 24.8333f;   // 024°50.0'E
    static constexpr float TRACK_LOOP_S  = 3600.0f;

    static DemoSnapshot at(uint32_t ms) {
        const float t = ms / 1000.0f;
        DemoSnapshot d{};

        d.heading_true_deg = wrap360(245.0f + 5.0f * sinf(t / 20.0f));
        d.mag_var_deg      = 11.0f;
        d.pitch_deg        = 3.0f * sinf(t / 4.0f);
        d.roll_deg         = 8.0f * sinf(t / 6.0f + 1.0f);

        d.cog_true_deg     = wrap360(247.0f + 2.0f * sinf(t / 30.0f));
        d.sog_kn           = 6.2f + 0.3f * sinf(t / 15.0f);

        // Dead-reckon along the mean course at the mean speed, looping every TRACK_LOOP_S
        const float leg_s   = fmodf(t, TRACK_LOOP_S);
        const float dist_nm = 6.2f * leg_s / 3600.0f;
        const float crs     = 247.0f * (float)M_PI / 180.0f;
        d.lat_deg = START_LAT_DEG + dist_nm * cosf(crs) / 60.0f;
        d.lon_deg = START_LON_DEG + dist_nm * sinf(crs) / (60.0f * cosf(START_LAT_DEG * (float)M_PI / 180.0f));

        d.depth_below_surface_m = 10.0f + 2.0f * sinf(t / 25.0f);

        d.house_v       = 12.9f + 0.05f * sinf(t / 40.0f);
        d.house_a       = -3.4f + 0.4f * sinf(t / 10.0f);
        d.house_soc_pct = 87.0f;
        d.start_v       = 12.7f;

        d.temperature_c = 18.0f + 0.3f * sinf(t / 60.0f);
        d.pressure_hpa  = 1013.2f + 0.4f * sinf(t / 90.0f);
        d.humidity_pct  = 62.0f + 2.0f * sinf(t / 45.0f);

        d.exhaust_k   = 330.0f + 5.0f * sinf(t / 35.0f);
        d.fuel_ratio  = 0.65f;
        d.water_ratio = 0.40f;
        return d;
    }

    static float wrap360(float deg) {
        float r = fmodf(deg, 360.0f);
        return r < 0.0f ? r + 360.0f : r;
    }

};
