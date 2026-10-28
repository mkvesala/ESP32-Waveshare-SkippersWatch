#include "CelestialSelector.h"
#include <math.h>

// === P U B L I C ===

// Find the best set, then keep the current one unless the new one is clearly better
const CelestialSelector::Selection& CelestialSelector::update(const Candidate* planets, uint8_t planet_count,
                                                              const Candidate* stars, uint8_t star_count,
                                                              const Candidate* moon) {
    // Prefilter stars to the hard window (typically 20–30 of the 57)
    int8_t usable[MAX_STAR_CANDIDATES];
    uint8_t n_usable = 0;
    for (uint8_t i = 0; i < star_count && n_usable < MAX_STAR_CANDIDATES; i++) {
        if (inHardWindow(stars[i])) usable[n_usable++] = (int8_t)i;
    }

    // Per-body penalties once per pass, not once per combination (thousands of them)
    float planet_pen[MAX_PLANET_CANDIDATES];
    float star_pen[MAX_STAR_CANDIDATES];
    for (uint8_t i = 0; i < planet_count && i < MAX_PLANET_CANDIDATES; i++) planet_pen[i] = memberPenalty(planets[i], moon);
    for (uint8_t i = 0; i < star_count && i < MAX_STAR_CANDIDATES; i++)     star_pen[i]   = memberPenalty(stars[i], moon);

    const float* pp = (planet_count <= MAX_PLANET_CANDIDATES) ? planet_pen : nullptr;
    const float* sp = (star_count <= MAX_STAR_CANDIDATES) ? star_pen : nullptr;

    Selection best;
    float best_score = score(best, planets, planet_count, stars, star_count, moon);

    auto consider = [&](const Selection& sel) {
        float s = scoreWith(sel, planets, planet_count, stars, star_count, pp, sp, moon);
        if (s < best_score) {
            best_score = s;
            best = sel;
        }
    };

    // planet: NONE or any usable planet; then every star combination that fills the set.
    // Partial sets are scored too (MISSING_MEMBER_PENALTY), for skies with few usable bodies.
    const int n = n_usable;
    for (int p = NONE; p < (int)planet_count; p++) {
        if (p != NONE && !inHardWindow(planets[p])) continue;
        const int max_stars = (p == NONE) ? SET_SIZE : SET_SIZE - 1;

        Selection sel;
        sel.planet = (int8_t)p;
        if (p != NONE) consider(sel);
        for (int a = 0; a < n; a++) {
            sel.star[0] = usable[a];
            sel.star[1] = NONE;
            sel.star[2] = NONE;
            consider(sel);
            for (int b = a + 1; b < n; b++) {
                sel.star[1] = usable[b];
                sel.star[2] = NONE;
                consider(sel);
                if (max_stars < 3) continue;
                for (int c = b + 1; c < n; c++) {
                    sel.star[2] = usable[c];
                    consider(sel);
                }
            }
        }
    }

    // Hysteresis
    if (_current.isEmpty() || best == _current) {
        _current = best;
        return _current;
    }
    float current_score = score(_current, planets, planet_count, stars, star_count, moon);
    if (best_score < current_score - SWITCH_MARGIN) _current = best;

    return _current;
}

// Every current member still computed and inside the hard window
bool CelestialSelector::isStillValid(const Candidate* planets, uint8_t planet_count,
                                     const Candidate* stars, uint8_t star_count) const {
    if (_current.planet != NONE && (_current.planet >= planet_count || !inHardWindow(planets[_current.planet]))) return false;
    for (int8_t s : _current.star) {
        if (s != NONE && (s >= star_count || !inHardWindow(stars[s]))) return false;
    }
    return true;
}

// Lower is better. Missing members cost MISSING_MEMBER_PENALTY each, so a complete set always
// beats a partial one when one is available; a set with a planet gets PLANET_PREFERENCE off.
float CelestialSelector::score(const Selection& sel,
                               const Candidate* planets, uint8_t planet_count,
                               const Candidate* stars, uint8_t star_count,
                               const Candidate* moon) {
    return scoreWith(sel, planets, planet_count, stars, star_count, nullptr, nullptr, moon);
}

bool CelestialSelector::inHardWindow(const Candidate& c) {
    return c.valid && c.hc_deg >= HARD_MIN_HC_DEG && c.hc_deg <= HARD_MAX_HC_DEG;
}

// === P R I V A T E ===

// score() with optional precomputed per-body penalties (nullptr = compute on the fly)
float CelestialSelector::scoreWith(const Selection& sel,
                                   const Candidate* planets, uint8_t planet_count,
                                   const Candidate* stars, uint8_t star_count,
                                   const float* planet_pen, const float* star_pen,
                                   const Candidate* moon) {
    const Candidate* members[1 + MAX_STARS];
    float penalties[1 + MAX_STARS];
    uint8_t n = 0;

    if (sel.planet != NONE) {
        if (sel.planet >= planet_count) return INVALID_SCORE;
        members[n] = &planets[sel.planet];
        penalties[n++] = planet_pen ? planet_pen[sel.planet] : memberPenalty(planets[sel.planet], moon);
    }
    for (uint8_t i = 0; i < MAX_STARS; i++) {
        int8_t s = sel.star[i];
        if (s == NONE) continue;
        if (s >= star_count) return INVALID_SCORE;
        for (uint8_t j = 0; j < i; j++) if (sel.star[j] == s) return INVALID_SCORE;
        members[n] = &stars[s];
        penalties[n++] = star_pen ? star_pen[s] : memberPenalty(stars[s], moon);
    }
    if (n > SET_SIZE) return INVALID_SCORE;

    float total = MISSING_MEMBER_PENALTY * (float)(SET_SIZE - n);
    if (sel.planet != NONE) total -= PLANET_PREFERENCE;
    float zn[SET_SIZE];
    for (uint8_t i = 0; i < n; i++) {
        if (!inHardWindow(*members[i])) return INVALID_SCORE;
        zn[i] = members[i]->zn_deg;
        total += penalties[i];
    }
    total += spreadError(zn, n);
    return total;
}

// 0 = ideal spread. Three bodies: worst gap deviation from 120°. Two bodies: deviation of
// their separation from 90°. Normalised so a fully bunched set scores ~1.
float CelestialSelector::spreadError(const float* zn, uint8_t n) {
    if (n == 3) {
        float z[3] = { zn[0], zn[1], zn[2] };
        for (uint8_t i = 0; i < 2; i++) {
            for (uint8_t j = 0; j < 2 - i; j++) {
                if (z[j] > z[j + 1]) { float t = z[j]; z[j] = z[j + 1]; z[j + 1] = t; }
            }
        }
        float gaps[3] = { z[1] - z[0], z[2] - z[1], 360.0f - (z[2] - z[0]) };
        float worst = 0.0f;
        for (float g : gaps) {
            float e = fabsf(g - IDEAL_GAP_3_DEG);
            if (e > worst) worst = e;
        }
        return worst / IDEAL_GAP_3_DEG;
    }
    if (n == 2) {
        float sep = fmodf(fabsf(zn[0] - zn[1]), 360.0f);
        if (sep > 180.0f) sep = 360.0f - sep;
        return fabsf(sep - IDEAL_SEPARATION_2_DEG) / IDEAL_SEPARATION_2_DEG;
    }
    return 0.0f;
}

// Great-circle separation of two bodies from their Hc/Zn
float CelestialSelector::angularSeparationDeg(const Candidate& a, const Candidate& b) {
    constexpr float D2R = (float)M_PI / 180.0f;
    float c = sinf(a.hc_deg * D2R) * sinf(b.hc_deg * D2R)
            + cosf(a.hc_deg * D2R) * cosf(b.hc_deg * D2R) * cosf((a.zn_deg - b.zn_deg) * D2R);
    if (c > 1.0f) c = 1.0f;
    if (c < -1.0f) c = -1.0f;
    return acosf(c) / D2R;
}

// Altitude outside the preferred window, brightness, closeness to the Moon
float CelestialSelector::memberPenalty(const Candidate& c, const Candidate* moon) {
    float p = 0.0f;
    if (c.hc_deg < PREFERRED_MIN_HC_DEG) p += (PREFERRED_MIN_HC_DEG - c.hc_deg) * ALTITUDE_PENALTY_PER_DEG;
    if (c.hc_deg > PREFERRED_MAX_HC_DEG) p += (c.hc_deg - PREFERRED_MAX_HC_DEG) * ALTITUDE_PENALTY_PER_DEG;
    p += c.vmag * MAGNITUDE_WEIGHT;
    if (moon && moon->valid && moon->hc_deg > 0.0f && angularSeparationDeg(c, *moon) < MOON_CLEARANCE_DEG) p += MOON_PENALTY;
    return p;
}
