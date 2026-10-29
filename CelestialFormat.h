#pragma once

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

// === C E L E S T I A L  F O R M A T ===
//
// - Pure helpers for CelestialUI: label formatting, data freshness and stored-position age
// - No Arduino or LVGL dependencies — unit tested on the host (tests/celestial)
// - Angles are rounded once to tenths of an arcminute, so 59.95' carries into the next degree
// - Decimal point, not comma, as on the rest of the watch (NavigationUI POS)

namespace CelestialFormat {

    static constexpr uint8_t NAME_MAX_CHARS = 6;     // longer names are shortened to 6 chars + "."

    // Stored position (not live GNSS): up to 15 min "*", up to 1 h "**", older is not used
    static constexpr int64_t STORED_POS_MARK1_S   = 15 * 60;
    static constexpr int64_t STORED_POS_MAX_AGE_S = 60 * 60;

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

    // UTC date as "dd.mm.yyyy"
    inline void formatDate(char* buf, size_t len, double unix_utc) {
        time_t t = (time_t)floor(unix_utc);
        struct tm ut;
        gmtime_r(&t, &ut);
        snprintf(buf, len, "%02d.%02d.%04d", ut.tm_mday, ut.tm_mon + 1, ut.tm_year + 1900);
    }

    // Lat/lon suffix for a stored position of the given age: "*" / "**", or nullptr when it
    // is too old (or from the future, i.e. the clock is off) to be used at all
    inline const char* storedPositionMarker(int64_t age_s) {
        if (age_s < 0 || age_s > STORED_POS_MAX_AGE_S) return nullptr;
        return age_s <= STORED_POS_MARK1_S ? "*" : "**";
    }

    // True if a timestamp is set and younger than timeout (millis() wraparound safe)
    inline bool isFresh(uint32_t now_ms, uint32_t last_ms, uint32_t timeout_ms) {
        return last_ms != 0 && (uint32_t)(now_ms - last_ms) < timeout_ms;
    }

} // namespace CelestialFormat
