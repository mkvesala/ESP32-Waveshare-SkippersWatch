#pragma once

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// === C E L E S T I A L  F O R M A T ===
//
// - Pure helpers for CelestialUI: label formatting and data freshness
// - No Arduino or LVGL dependencies — unit tested on the host (tests/celestial)
// - Angles are rounded once to tenths of an arcminute, so 59.95' carries into the next degree
// - Decimal point, not comma, as on the rest of the watch (NavigationUI POS)

namespace CelestialFormat {

    static constexpr uint8_t NAME_MAX_CHARS = 6;     // longer names are shortened to 6 chars + "."

    // Split |deg| into whole degrees, whole minutes and tenths of a minute
    inline void splitDegMin(double deg, long& d, long& m, long& tenths) {
        long t = lround(fabs(deg) * 600.0);  // tenths of an arcminute
        d = t / 600;
        m = (t % 600) / 10;
        tenths = t % 10;
    }

    // Hc as "dd°mm.m'" (e.g. "45°07.3'"), negative values prefixed with "-"
    inline void formatAltitude(char* buf, size_t len, double hc_deg) {
        long d, m, tenths;
        splitDegMin(hc_deg, d, m, tenths);
        bool negative = hc_deg < 0.0 && (d | m | tenths) != 0;
        snprintf(buf, len, "%s%02ld°%02ld.%ld'", negative ? "-" : "", d, m, tenths);
    }

    // Zn as "ddd°" (e.g. "092°"), rounded to whole degrees, 359.5° → "000°"
    inline void formatAzimuth(char* buf, size_t len, double zn_deg) {
        long zn = lround(zn_deg) % 360;
        if (zn < 0) zn += 360;
        snprintf(buf, len, "%03ld°", zn);
    }

    // Latitude as "dd°mm.m'N" / "dd°mm.m'S"
    inline void formatLatitude(char* buf, size_t len, double lat_deg) {
        long d, m, tenths;
        splitDegMin(lat_deg, d, m, tenths);
        snprintf(buf, len, "%02ld°%02ld.%ld'%c", d, m, tenths, lat_deg < 0.0 ? 'S' : 'N');
    }

    // Longitude as "ddd°mm.m'E" / "ddd°mm.m'W"
    inline void formatLongitude(char* buf, size_t len, double lon_deg) {
        long d, m, tenths;
        splitDegMin(lon_deg, d, m, tenths);
        snprintf(buf, len, "%03ld°%02ld.%ld'%c", d, m, tenths, lon_deg < 0.0 ? 'W' : 'E');
    }

    // Body name: up to 6 chars as is, longer ones as 6 chars + "." without a trailing space
    // ("Betelgeuse" → "Betelg.", "Rigil Kentaurus" → "Rigil.")
    inline void abbreviateName(char* buf, size_t len, const char* name) {
        if (len == 0) return;
        size_t n = strlen(name);
        if (n <= NAME_MAX_CHARS) {
            snprintf(buf, len, "%s", name);
            return;
        }
        size_t keep = NAME_MAX_CHARS;
        while (keep > 0 && name[keep - 1] == ' ') keep--;
        snprintf(buf, len, "%.*s.", (int)keep, name);
    }

    // True if a timestamp is set and younger than timeout (millis() wraparound safe)
    inline bool isFresh(uint32_t now_ms, uint32_t last_ms, uint32_t timeout_ms) {
        return last_ms != 0 && (uint32_t)(now_ms - last_ms) < timeout_ms;
    }

} // namespace CelestialFormat
