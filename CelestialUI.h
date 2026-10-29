#pragma once

#include <Arduino.h>
#include <lvgl.h>
#include "IScreenUI.h"
#include "ESPNowReceiver.h"
#include "CelestialEngine.h"
#include "CelestialFormat.h"
#include "LastPosition.h"

// === C L A S S  C E L E S T I A L U I ===
//
// - Celestial page adapter: the data a skipper needs for a sight reduction, as a table
//   - Bodies / Hc / Zn columns (ui_LabelBodies, ui_LabelHc, ui_LabelZn), max 5 rows:
//     Sun and Moon between their rise and set; in twilight (Sun Hc -3°..-12°) also one
//     planet + two stars, or three stars, picked by CelestialSelector
//   - ui_LabelVariables: UTC date, UTC time, pressure + temperature, latitude, longitude
// - Hc and Zn are GEOCENTRIC (Nautical Almanac convention), not related to the boat's heading.
//   The navigator applies dip, refraction, semi-diameter and parallax to Hs as usual.
// - Astronomy runs on CelestialEngine's own task (core 0), only while this page is active;
//   this class samples inputs, formats labels once per UTC second
// - Sources: GNSS_DELTA (position), DATETIME_DELTA (freshness of the GNSS-disciplined system
//   clock, see WatchApplication::handleTimeSync), WEATHER_DELTA (pressure, temperature).
//   Stale data is never shown as fresh: no time or no position → no bodies, only a note.
// - No live fix: the last known position (LastPosition: this session's last fix, else the one
//   stored at the previous sleep) is used for up to 1 h, marked "*" (≤ 15 min) or "**" after
//   the lat/lon hemisphere letters. A live fix takes over the moment it arrives.
// - Demo mode: the same real computation on demo inputs — DemoSource's position and
//   weather through the receiver, and the Clock page's demo time (DemoValues::DEMO_EPOCH_UTC).
//   The variables show those inputs as if they were real, so table and inputs agree.
// - Realizes IScreenUI over container ui_ContainerCelestial
// - Owns: CelestialEngine
// - Owned by: WatchApplication

class CelestialUI : public IScreenUI {

public:

    CelestialUI(ESPNowReceiver& receiver, LastPosition& last_position);

    void begin() override;
    lv_obj_t* getLvglScreen() const override;
    void onEnter() override;
    void onLeave() override;
    void update() override;

    void setDemo(bool on) { if (on != _demo) { _demo = on; _refresh_key = INT64_MIN; } }

    const CelestialEngine& getEngine() const { return _engine; }

private:

    // Position fed to the engine and shown; marker "" = live GNSS, "*" / "**" = stored
    struct Position {
        bool        ok      = false;
        double      lat_deg = 0.0;
        double      lon_deg = 0.0;
        const char* marker  = "";
    };

    ESPNowReceiver& _receiver;
    LastPosition& _last_position;
    CelestialEngine _engine;
    bool _initialized = false;
    bool _demo = false;

    // Redraw once per displayed UTC second (or per second of millis() without time)
    int64_t _refresh_key = INT64_MIN;

    // Position source in use, and a tag bumped on every switch (results are matched to it)
    bool _using_stored = false;
    uint32_t _source_tag = 0;

    // UTC (s since 1970, with fraction) now, or NAN when no trustworthy time
    double utcNow(uint32_t now) const;
    bool positionFresh(const GnssData& gnss, uint32_t now) const;
    Position choosePosition(uint32_t now, double utc);
    bool weatherFresh(uint32_t now) const;

    void refresh(uint32_t now, double utc, const Position& pos);
    void updateTable(uint32_t now, bool time_ok, bool position_ok);
    void updateVariables(double utc, const Position& pos, bool weather_ok);
    void showNote(const char* note);

    // Freshness limits (sender cadence in brackets)
    static constexpr uint32_t GNSS_TIMEOUT_MS     = 6000;    // UBLOX position (499 ms), as NavigationUI
    static constexpr uint32_t DATETIME_TIMEOUT_MS = 10000;   // UBLOX DATETIME_DELTA (2 s)
    static constexpr uint32_t WEATHER_TIMEOUT_MS  = 6000;    // BME280 (2 s), as WeatherUI
    static constexpr uint32_t RESULT_MAX_AGE_MS   = 3000;    // engine computes at 1 Hz

    static constexpr uint32_t REFRESH_INTERVAL_MS = 1000;

    // System clock not yet GNSS-set reads ~1970 (same test as ClockUI)
    static constexpr int MIN_VALID_YEAR = 2020;

    // Table rows: Sun, Moon, planet or star, star, star
    static constexpr uint8_t MAX_ROWS = 5;

};
