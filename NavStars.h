#pragma once

#include <stdint.h>

// === N A V I G A T I O N A L  S T A R S ===
//
// - The 57 selected stars of the Nautical Almanac, in almanac order (index 0 = star No. 1)
// - Source: Hipparcos new reduction (van Leeuwen 2007, VizieR I/311/hip2), positions propagated
//   by VizieR to epoch J2000.0 in the ICRS/J2000 frame; V magnitudes from VizieR I/239/hip_main
// - Proper motion is applied at runtime (CelestialCalculator::starPositionAtEpoch) — over
//   26 years Arcturus moves ~1' and Rigil Kentaurus ~1.6', more than the 1' display tolerance
// - Lives in flash (const), ~2.5 KB

struct NavStar {
    const char* name;        // Nautical Almanac name
    uint32_t    hip;         // Hipparcos catalogue number
    double      ra_deg;      // Right ascension, J2000 frame, epoch J2000.0 (degrees)
    double      dec_deg;     // Declination, J2000 frame, epoch J2000.0 (degrees)
    float       pm_ra_mas;   // Proper motion in RA × cos(Dec), mas/year
    float       pm_dec_mas;  // Proper motion in Dec, mas/year
    float       vmag;        // Visual magnitude
};

namespace NavStars {

    static constexpr uint8_t COUNT = 57;

    extern const NavStar TABLE[COUNT];

} // namespace NavStars
