#pragma once

#include <stdint.h>
#include "astronomy.h"
#include "NavStars.h"

// === C L A S S  C E L E S T I A L C A L C U L A T O R ===
//
// - Class CelestialCalculator - computed altitude Hc and azimuth Zn of navigational bodies
// - Hc is GEOCENTRIC, as in the Nautical Almanac and sight reduction: apparent place of date
//   (light time, aberration, precession, nutation) from the Earth's centre, no refraction,
//   no parallax. The navigator applies those corrections to Hs → Ho as usual.
// - All astronomy in double (the ESP32-S3 FPU is single precision only, which is too coarse
//   for Julian dates) — slow in software, so CelestialEngine runs this on its own task
// - Not thread safe: Astronomy Engine keeps the user-defined star table in a global, so all
//   calls must come from one task
// - No Arduino or LVGL dependencies — unit tested on the host (tests/celestial)
// - Owned by: CelestialEngine

class CelestialCalculator {

public:

    enum class Planet : uint8_t {
        VENUS   = 0,
        MARS    = 1,
        JUPITER = 2,
        SATURN  = 3,
        COUNT   = 4
    };

    static constexpr uint8_t PLANET_COUNT = static_cast<uint8_t>(Planet::COUNT);

    struct Position {
        double hc_deg = 0.0;   // computed altitude, degrees (negative = below the horizon)
        double zn_deg = 0.0;   // true azimuth 0..360, degrees
        bool   valid  = false;
    };

    // Twilight for sextant work: Sun's Hc from -12°00,0' to -3°00,0', both inclusive — mid civil
    // twilight through the whole of nautical twilight, while both horizon and stars are visible
    static constexpr double TWILIGHT_UPPER_DEG = -3.0;
    static constexpr double TWILIGHT_LOWER_DEG = -12.0;

    // Set observation time (UTC seconds since 1970) and observer position. Must be called
    // before any position query; caches the time-dependent rotation matrix.
    void set(double unix_utc, double lat_deg, double lon_deg);

    Position sun();
    Position moon();
    Position planet(Planet planet);

    // Between rise and set: upper limb at or above the sea horizon with standard refraction.
    // Same definition as Astronomy_SearchRiseSet (topocentric place, 34' refraction, body
    // radius), evaluated for the current time instead of searched. For the Sun this is
    // geocentric Hc ≈ -50'; the Moon's ~57' parallax makes it about Hc +7' instead.
    bool isSunUp();
    bool isMoonUp();
    Position star(uint8_t star_index);   // index into NavStars::TABLE

    // Position for arbitrary apparent RA (hours) / Dec (degrees) of date — the horizon transform
    // used by all of the above. Public for tests.
    Position fromEquatorOfDate(double ra_hours, double dec_deg);

    // Catalogue position of a star at the current epoch: J2000 frame with proper motion applied
    void starPositionAtEpoch(uint8_t star_index, double& ra_hours, double& dec_deg) const;

    static bool isTwilight(double sun_hc_deg);
    static const char* planetName(Planet planet);
    static float planetMagnitude(Planet planet);   // typical visual magnitude, for ranking only

    // Unix time of the J2000.0 epoch, 2000-01-01 12:00:00 UTC
    static constexpr double UNIX_J2000 = 946728000.0;

private:

    astro_time_t     _time = {};
    astro_rotation_t _eqj_to_eqd = {};
    astro_observer_t _observer = {};
    double           _years_since_j2000 = 0.0;
    bool             _ready = false;

    Position geocentric(astro_body_t body);
    bool isUp(astro_body_t body, double radius_km);

    static astro_body_t planetBody(Planet planet);

    // Distance passed to Astronomy_DefineStar: far enough that annual parallax is negligible
    static constexpr double STAR_DISTANCE_LY = 1000.0;
    static constexpr double RISE_SET_REFRACTION_DEG = 34.0 / 60.0;   // as Astronomy_SearchRiseSet
    static constexpr double DAYS_PER_JULIAN_YEAR = 365.25;
    static constexpr double MAS_PER_DEG = 3600000.0;

};
