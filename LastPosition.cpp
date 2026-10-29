#include "LastPosition.h"
#include "CelestialFormat.h"
#include <Preferences.h>
#include <sys/time.h>
#include <time.h>

// === P U B L I C ===

// Demo feeds the receiver with demo positions: forget the session fix, record nothing
void LastPosition::setDemo(bool on) {
    _demo = on;
    if (on) _have_session = false;
}

void LastPosition::observe(const GnssData& gnss, uint32_t rx_ms, uint32_t now) {
    if (_demo) return;
    if (!gnss.fix_ok || isnan(gnss.lat_deg) || isnan(gnss.lon_deg)) return;
    if (!CelestialFormat::isFresh(now, rx_ms, FIX_TIMEOUT_MS)) return;
    _have_session  = true;
    _session_lat   = gnss.lat_deg;
    _session_lon   = gnss.lon_deg;
    _session_rx_ms = rx_ms;
}

// The session fix is always the newer one: the stored fix was written before this wake
bool LastPosition::lookup(double utc_now, uint32_t now, Fix& out) {
    if (_demo) return false;

    if (_have_session) {
        out.lat_deg = _session_lat;
        out.lon_deg = _session_lon;
        out.age_s   = (int64_t)((uint32_t)(now - _session_rx_ms) / 1000);
    } else {
        this->load();
        if (!_have_stored) return false;
        out.lat_deg = _stored.lat_deg;
        out.lon_deg = _stored.lon_deg;
        out.age_s   = (int64_t)floor(utc_now) - _stored.utc_s;
    }
    return CelestialFormat::storedPositionMarker(out.age_s) != nullptr;
}

// Runs once, from WatchApplication::preSleepFlush(). Needs the system clock for the UTC
// stamp; without it nothing is written (an unstamped fix could never be aged).
void LastPosition::saveOnSleep() {
    if (_demo) return;
    double utc;
    if (!systemUtc(utc)) {
        Serial.println("[lastpos] skip: no clock");
        return;
    }

    if (_have_session) {
        const uint32_t age_ms = millis() - _session_rx_ms;
        if (age_ms / 1000 <= (uint32_t)CelestialFormat::STORED_POS_MAX_AGE_S) {
            Record r = { RECORD_MAGIC, _session_lat, _session_lon,
                         (int64_t)floor(utc) - (int64_t)(age_ms / 1000) };
            Preferences prefs;
            prefs.begin(NVS_NAMESPACE, false);
            prefs.putBytes(NVS_KEY, &r, sizeof(r));
            prefs.end();
            _stored = r;
            _have_stored = true;
            _loaded = true;
            Serial.printf("[lastpos] saved age=%lus\n", (unsigned long)(age_ms / 1000));
            return;
        }
    }

    // No usable session fix: drop the stored one once it has expired
    this->load();
    if (!_have_stored) return;
    if ((int64_t)floor(utc) - _stored.utc_s <= CelestialFormat::STORED_POS_MAX_AGE_S) return;
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.remove(NVS_KEY);
    prefs.end();
    _have_stored = false;
    Serial.println("[lastpos] removed (expired)");
}

// === P R I V A T E ===

// Read the stored fix once per wake. A missing key or a record of another size/version
// leaves _have_stored false.
void LastPosition::load() {
    if (_loaded) return;
    _loaded = true;
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, true)) return;   // namespace absent on a fresh device
    Record r;
    const size_t n = prefs.isKey(NVS_KEY) ? prefs.getBytes(NVS_KEY, &r, sizeof(r)) : 0;
    prefs.end();
    _have_stored = (n == sizeof(r) && r.magic == RECORD_MAGIC &&
                    !isnan(r.lat_deg) && !isnan(r.lon_deg));
    if (_have_stored) _stored = r;
}

bool LastPosition::systemUtc(double& utc) {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    struct tm ut;
    time_t t = tv.tv_sec;
    gmtime_r(&t, &ut);
    if ((ut.tm_year + 1900) < MIN_VALID_YEAR) return false;
    utc = (double)tv.tv_sec + (double)tv.tv_usec / 1e6;
    return true;
}
