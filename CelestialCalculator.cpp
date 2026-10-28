#include "CelestialCalculator.h"
#include <math.h>

// === P U B L I C ===

// Set observation time and observer, cache the J2000 → equator-of-date rotation
void CelestialCalculator::set(double unix_utc, double lat_deg, double lon_deg) {
    double ut_days = (unix_utc - UNIX_J2000) / 86400.0;
    _time = Astronomy_TimeFromDays(ut_days);
    _eqj_to_eqd = Astronomy_Rotation_EQJ_EQD(&_time);
    _observer.latitude = lat_deg;
    _observer.longitude = lon_deg;
    _observer.height = 0.0;
    _years_since_j2000 = ut_days / DAYS_PER_JULIAN_YEAR;
    _ready = (_eqj_to_eqd.status == ASTRO_SUCCESS);
}

CelestialCalculator::Position CelestialCalculator::sun() {
    return this->geocentric(BODY_SUN);
}

CelestialCalculator::Position CelestialCalculator::moon() {
    return this->geocentric(BODY_MOON);
}

CelestialCalculator::Position CelestialCalculator::planet(Planet planet) {
    if (planet >= Planet::COUNT) return Position{};
    return this->geocentric(planetBody(planet));
}

// Star: catalogue place moved to the current epoch, then the same apparent-place chain as the
// planets via a user-defined star slot. Only BODY_STAR1 is used — it is redefined per call.
CelestialCalculator::Position CelestialCalculator::star(uint8_t star_index) {
    if (star_index >= NavStars::COUNT) return Position{};

    double ra_hours, dec_deg;
    this->starPositionAtEpoch(star_index, ra_hours, dec_deg);
    if (Astronomy_DefineStar(BODY_STAR1, ra_hours, dec_deg, STAR_DISTANCE_LY) != ASTRO_SUCCESS) return Position{};

    return this->geocentric(BODY_STAR1);
}

bool CelestialCalculator::isSunUp() {
    return this->isUp(BODY_SUN, SUN_RADIUS_KM);
}

bool CelestialCalculator::isMoonUp() {
    return this->isUp(BODY_MOON, MOON_EQUATORIAL_RADIUS_KM);
}

// Horizon transform of an apparent place of date (no refraction)
CelestialCalculator::Position CelestialCalculator::fromEquatorOfDate(double ra_hours, double dec_deg) {
    Position pos;
    if (!_ready) return pos;

    astro_horizon_t hor = Astronomy_Horizon(&_time, _observer, ra_hours, dec_deg, REFRACTION_NONE);
    pos.hc_deg = hor.altitude;
    pos.zn_deg = hor.azimuth;
    pos.valid  = isfinite(hor.altitude) && isfinite(hor.azimuth);
    return pos;
}

// Linear proper motion from J2000.0 to the current epoch (adequate for decades; the
// navigational stars are all well away from the celestial poles)
void CelestialCalculator::starPositionAtEpoch(uint8_t star_index, double& ra_hours, double& dec_deg) const {
    const NavStar& s = NavStars::TABLE[star_index];
    double dec0 = s.dec_deg;
    double cos_dec = cos(dec0 * M_PI / 180.0);

    double ra = s.ra_deg + (double)s.pm_ra_mas / cos_dec * _years_since_j2000 / MAS_PER_DEG;
    dec_deg = dec0 + (double)s.pm_dec_mas * _years_since_j2000 / MAS_PER_DEG;

    ra = fmod(ra, 360.0);
    if (ra < 0.0) ra += 360.0;
    ra_hours = ra / 15.0;
    if (ra_hours >= 24.0) ra_hours = 0.0;
}

bool CelestialCalculator::isTwilight(double sun_hc_deg) {
    return sun_hc_deg <= TWILIGHT_UPPER_DEG && sun_hc_deg >= TWILIGHT_LOWER_DEG;
}

const char* CelestialCalculator::planetName(Planet planet) {
    switch (planet) {
        case Planet::VENUS:   return "Venus";
        case Planet::MARS:    return "Mars";
        case Planet::JUPITER: return "Jupiter";
        case Planet::SATURN:  return "Saturn";
        default:              return "";
    }
}

float CelestialCalculator::planetMagnitude(Planet planet) {
    switch (planet) {
        case Planet::VENUS:   return -4.2f;
        case Planet::MARS:    return  0.7f;
        case Planet::JUPITER: return -2.3f;
        case Planet::SATURN:  return  0.7f;
        default:              return  9.9f;
    }
}

// === P R I V A T E ===

// Geocentric apparent place of date → Hc/Zn.
// GeoVector (EQJ, light time + aberration) → precession/nutation rotation → RA/Dec of date.
CelestialCalculator::Position CelestialCalculator::geocentric(astro_body_t body) {
    if (!_ready) return Position{};

    astro_vector_t eqj = Astronomy_GeoVector(body, _time, ABERRATION);
    if (eqj.status != ASTRO_SUCCESS) return Position{};

    astro_vector_t eqd = Astronomy_RotateVector(_eqj_to_eqd, eqj);
    if (eqd.status != ASTRO_SUCCESS) return Position{};

    astro_equatorial_t equ = Astronomy_EquatorFromVector(eqd);
    if (equ.status != ASTRO_SUCCESS) return Position{};

    return this->fromEquatorOfDate(equ.ra, equ.dec);
}

// Topocentric apparent altitude of the centre + angular radius ≥ -34' (standard refraction at the
// horizon). Rise/set are exactly the moments this condition changes.
bool CelestialCalculator::isUp(astro_body_t body, double radius_km) {
    if (!_ready) return false;

    astro_equatorial_t equ = Astronomy_Equator(body, &_time, _observer, EQUATOR_OF_DATE, ABERRATION);
    if (equ.status != ASTRO_SUCCESS || !(equ.dist > 0.0)) return false;

    astro_horizon_t hor = Astronomy_Horizon(&_time, _observer, equ.ra, equ.dec, REFRACTION_NONE);
    double radius_deg = asin(radius_km / (equ.dist * KM_PER_AU)) * 180.0 / M_PI;
    return isfinite(hor.altitude) && (hor.altitude + radius_deg) >= -RISE_SET_REFRACTION_DEG;
}

astro_body_t CelestialCalculator::planetBody(Planet planet) {
    switch (planet) {
        case Planet::VENUS:   return BODY_VENUS;
        case Planet::MARS:    return BODY_MARS;
        case Planet::JUPITER: return BODY_JUPITER;
        case Planet::SATURN:  return BODY_SATURN;
        default:              return BODY_INVALID;
    }
}
