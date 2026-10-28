#pragma once

#include <stdint.h>

// === C L A S S  C E L E S T I A L S E L E C T O R ===
//
// - Class CelestialSelector - picks the twilight set for sextant practice: three bodies whose
//   azimuths are spread ~120° apart — one planet and two stars when a planet scores well,
//   otherwise three stars. A planet set gets a PLANET_PREFERENCE bonus, so three stars win only
//   when they spread clearly better (or no planet is inside the hard window).
// - Hard window: Hc 10°..75°. Preferred window: Hc 15°..65°, linear penalty outside it.
// - Score (lower = better) = azimuth spread error + altitude penalty + brightness term
//   + penalty for bodies close to the Moon (its glare makes them hard to shoot)
// - Hysteresis: a new set replaces the current one only if it scores SWITCH_MARGIN better,
//   or if a current member has left the hard window. The caller decides how often to
//   re-evaluate (CelestialEngine: every 60 s, or at once when a member becomes invalid).
// - Float math on precomputed Hc/Zn; no Arduino or LVGL dependencies — unit tested on the host
// - Owned by: CelestialEngine

class CelestialSelector {

public:

    struct Candidate {
        float hc_deg = 0.0f;
        float zn_deg = 0.0f;
        float vmag   = 0.0f;
        bool  valid  = false;   // false = not computed / unusable
    };

    static constexpr int8_t  NONE      = -1;
    static constexpr uint8_t MAX_STARS = 3;    // three star rows in the table
    static constexpr uint8_t SET_SIZE  = 3;    // bodies in a full set (planet counts as one)

    // Stars fill star[] from index 0; a set with a planet holds at most SET_SIZE - 1 stars
    struct Selection {
        int8_t planet = NONE;
        int8_t star[MAX_STARS] = { NONE, NONE, NONE };

        bool isEmpty() const {
            if (planet != NONE) return false;
            for (int8_t s : star) if (s != NONE) return false;
            return true;
        }
        bool operator==(const Selection& o) const {
            if (planet != o.planet) return false;
            for (uint8_t i = 0; i < MAX_STARS; i++) if (star[i] != o.star[i]) return false;
            return true;
        }
    };

    // Re-evaluate from scratch, then apply hysteresis against the current selection.
    // moon may be nullptr or invalid when the Moon is below the horizon.
    const Selection& update(const Candidate* planets, uint8_t planet_count,
                            const Candidate* stars, uint8_t star_count,
                            const Candidate* moon);

    // True when every member of the current selection is still inside the hard window
    bool isStillValid(const Candidate* planets, uint8_t planet_count,
                      const Candidate* stars, uint8_t star_count) const;

    const Selection& current() const { return _current; }
    void reset() { _current = Selection{}; }

    // Score of a given selection (lower = better, INVALID_SCORE if a member is unusable)
    static float score(const Selection& sel,
                       const Candidate* planets, uint8_t planet_count,
                       const Candidate* stars, uint8_t star_count,
                       const Candidate* moon);

    static bool inHardWindow(const Candidate& c);

    static constexpr float HARD_MIN_HC_DEG      = 10.0f;
    static constexpr float HARD_MAX_HC_DEG      = 75.0f;
    static constexpr float PREFERRED_MIN_HC_DEG = 15.0f;
    static constexpr float PREFERRED_MAX_HC_DEG = 65.0f;
    static constexpr float SWITCH_MARGIN        = 0.15f;   // ≈ 18° of spread error
    static constexpr float PLANET_PREFERENCE    = 0.25f;   // bonus for a planet set ≈ 30° of spread error
    static constexpr float INVALID_SCORE        = 1.0e9f;

private:

    Selection _current;

    static constexpr float IDEAL_GAP_3_DEG          = 120.0f;  // three bodies
    static constexpr float IDEAL_SEPARATION_2_DEG   = 90.0f;   // two bodies (LOP crossing angle)
    static constexpr float ALTITUDE_PENALTY_PER_DEG = 0.1f;    // 10° outside preferred = 1.0
    static constexpr float MAGNITUDE_WEIGHT         = 0.05f;   // per magnitude, per member
    static constexpr float MOON_CLEARANCE_DEG       = 15.0f;
    static constexpr float MOON_PENALTY             = 0.5f;
    static constexpr float MISSING_MEMBER_PENALTY   = 10.0f;   // a full set always wins
    static constexpr uint8_t MAX_STAR_CANDIDATES    = 64;
    static constexpr uint8_t MAX_PLANET_CANDIDATES  = 8;

    static float scoreWith(const Selection& sel,
                           const Candidate* planets, uint8_t planet_count,
                           const Candidate* stars, uint8_t star_count,
                           const float* planet_pen, const float* star_pen,
                           const Candidate* moon);
    static float spreadError(const float* zn, uint8_t n);
    static float angularSeparationDeg(const Candidate& a, const Candidate& b);
    static float memberPenalty(const Candidate& c, const Candidate* moon);

};
