#include "CelestialUI.h"
#include "NavStars.h"
#include "DemoValues.h"
#include "ui.h"
#include <sys/time.h>
#include <time.h>

// === P U B L I C ===

CelestialUI::CelestialUI(ESPNowReceiver& receiver, LastPosition& last_position)
    : _receiver(receiver), _last_position(last_position) {}

lv_obj_t* CelestialUI::getLvglScreen() const {
    return ui_ContainerCelestial;
}

// No task yet: most wakes never open this page, so the engine's 16 KB stack is
// allocated only on the first onEnter()
void CelestialUI::begin() {
    if (_initialized) return;
    this->showNote("");
    lv_label_set_text(ui_LabelVariables, "--.--.----\n--:--:-- UTC\n---- mbar --°C\n--°--.-'\n---°--.-'");
    _initialized = true;
}

// Start computing (creating the task on first entry); redraw on the next update()
void CelestialUI::onEnter() {
    if (!_initialized) return;
    _engine.begin();   // once per wake; no-op afterwards
    _engine.setActive(true);
    _refresh_key = INT64_MIN;
}

void CelestialUI::onLeave() {
    if (!_initialized) return;
    _engine.setActive(false);
    _refresh_key = INT64_MIN;
}

// ~17 Hz: feed the engine (its heartbeat), redraw when the UTC second changes
void CelestialUI::update() {
    if (!_initialized) return;

    const uint32_t now = millis();
    const double utc = this->utcNow(now);
    const bool time_ok = !isnan(utc);
    const Position pos = this->choosePosition(now, utc);

    // Live GNSS <-> stored position: recompute and redraw at once, not on the next second
    const bool stored = pos.ok && pos.marker[0] != '\0';
    if (stored != _using_stored) {
        _using_stored = stored;
        _source_tag++;
        _refresh_key = INT64_MIN;
    }

    CelestialEngine::Inputs in;
    in.unix_utc      = time_ok ? utc : 0.0;
    in.anchor_millis = now;
    in.lat_deg       = pos.lat_deg;
    in.lon_deg       = pos.lon_deg;
    in.valid         = time_ok && pos.ok;
    in.source_tag    = _source_tag;
    _engine.setInputs(in);

    const int64_t key = time_ok ? (int64_t)floor(utc)
                                : -1 - (int64_t)(now / REFRESH_INTERVAL_MS);
    if (key == _refresh_key) return;
    _refresh_key = key;
    this->refresh(now, utc, pos);
}

// === P R I V A T E ===

// The system clock is disciplined by GNSS (WatchApplication::handleTimeSync). It is trusted for
// a sight only while DATETIME_DELTA keeps arriving: the RTC drifts during deep sleep. Demo mode
// uses the Clock page's time instead — demo epoch until a real sync, then the system clock.
double CelestialUI::utcNow(uint32_t now) const {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    struct tm ut;
    time_t t = tv.tv_sec;
    gmtime_r(&t, &ut);
    const bool clock_set = (ut.tm_year + 1900) >= MIN_VALID_YEAR;
    const double sys = (double)tv.tv_sec + (double)tv.tv_usec / 1e6;

    if (_demo) {
        return clock_set ? sys : (double)DemoValues::DEMO_EPOCH_UTC + (double)now / 1000.0;
    }
    if (!clock_set) return NAN;
    if (!CelestialFormat::isFresh(now, _receiver.lastDatetimeRxMillis(), DATETIME_TIMEOUT_MS)) return NAN;
    return sys;
}

bool CelestialUI::positionFresh(const GnssData& gnss, uint32_t now) const {
    return gnss.fix_ok && !isnan(gnss.lat_deg) && !isnan(gnss.lon_deg) &&
           CelestialFormat::isFresh(now, _receiver.lastGnssRxMillis(), GNSS_TIMEOUT_MS);
}

// Live GNSS whenever it is fresh. Otherwise the last known position (this session's last fix,
// else the one stored at the previous sleep) while it is at most 1 h old — never in demo mode,
// and only with trusted time, since its age is measured in UTC.
CelestialUI::Position CelestialUI::choosePosition(uint32_t now, double utc) {
    Position pos;
    const GnssData gnss = _receiver.getGnssData();
    if (this->positionFresh(gnss, now)) {
        pos.ok      = true;
        pos.lat_deg = gnss.lat_deg;
        pos.lon_deg = gnss.lon_deg;
        pos.marker  = "";
        return pos;
    }
    LastPosition::Fix fix;
    if (_demo || isnan(utc) || !_last_position.lookup(utc, now, fix)) return pos;
    pos.ok      = true;
    pos.lat_deg = fix.lat_deg;
    pos.lon_deg = fix.lon_deg;
    pos.marker  = CelestialFormat::storedPositionMarker(fix.age_s);
    return pos;
}

bool CelestialUI::weatherFresh(uint32_t now) const {
    return CelestialFormat::isFresh(now, _receiver.lastWeatherRxMillis(), WEATHER_TIMEOUT_MS);
}

void CelestialUI::refresh(uint32_t now, double utc, const Position& pos) {
    const bool time_ok = !isnan(utc);
    this->updateTable(now, time_ok, pos.ok);
    this->updateVariables(utc, pos, this->weatherFresh(now));
}

// Bodies of the latest engine result. Without fresh inputs (or a fresh result) the table shows
// only a note — never positions computed for an old time or place, nor for the position
// source (live / stored) that was in use before the latest switch.
void CelestialUI::updateTable(uint32_t now, bool time_ok, bool position_ok) {
    if (!time_ok)     { this->showNote("No\ntime"); return; }
    if (!position_ok) { this->showNote("No\nfix"); return; }

    const CelestialEngine::Result r = _engine.getResult();
    if (r.seq == 0 || !r.valid || r.source_tag != _source_tag ||
        (uint32_t)(now - r.computed_millis) >= RESULT_MAX_AGE_MS) {
        this->showNote("Wait");
        return;
    }

    struct Row { const char* name; const CelestialEngine::Body* body; };
    Row rows[MAX_ROWS];
    uint8_t n = 0;
    if (r.sun.visible)  rows[n++] = { "Sun",  &r.sun };
    if (r.moon.visible) rows[n++] = { "Moon", &r.moon };
    if (r.planet != CelestialSelector::NONE && r.planet_pos.visible) {
        rows[n++] = { CelestialCalculator::planetName(static_cast<CelestialCalculator::Planet>(r.planet)),
                      &r.planet_pos };
    }
    for (uint8_t i = 0; i < CelestialSelector::MAX_STARS && n < MAX_ROWS; i++) {
        if (r.star[i] == CelestialSelector::NONE || !r.star_pos[i].visible) continue;
        rows[n++] = { NavStars::TABLE[r.star[i]].name, &r.star_pos[i] };
    }

    if (n == 0) { this->showNote("No\nbodies"); return; }

    // Header rows as in the SquareLine layout: "" / "Hc" / "Zn", then a blank line
    char names[96] = "\n\n";
    char hcs[96]   = "Hc\n\n";
    char zns[64]   = "Zn\n\n";
    for (uint8_t i = 0; i < n; i++) {
        char name[16], hc[16], zn[8];
        CelestialFormat::abbreviateName(name, sizeof(name), rows[i].name);
        CelestialFormat::formatAltitude(hc, sizeof(hc), rows[i].body->hc_deg);
        CelestialFormat::formatAzimuth(zn, sizeof(zn), rows[i].body->zn_deg);
        const char* sep = (i + 1 < n) ? "\n" : "";
        strlcat(names, name, sizeof(names)); strlcat(names, sep, sizeof(names));
        strlcat(hcs, hc, sizeof(hcs));       strlcat(hcs, sep, sizeof(hcs));
        strlcat(zns, zn, sizeof(zns));       strlcat(zns, sep, sizeof(zns));
    }
    lv_label_set_text(ui_LabelBodies, names);
    lv_label_set_text(ui_LabelHc, hcs);
    lv_label_set_text(ui_LabelZn, zns);
}

// "dd.mm.yyyy" / "HH:MM:SS UTC" / "PPPP mbar TT°C" / latitude / longitude, each dashed on its
// own when stale. A stored position carries "*" (≤ 15 min old) or "**" (≤ 1 h) after N/S, E/W.
void CelestialUI::updateVariables(double utc, const Position& pos, bool weather_ok) {
    char date_s[16], time_s[16], wx_s[24], lat_s[20], lon_s[20];

    if (!isnan(utc)) {
        CelestialFormat::formatDate(date_s, sizeof(date_s), utc);
        const uint32_t sod = (uint32_t)((int64_t)floor(utc) % 86400);
        snprintf(time_s, sizeof(time_s), "%02lu:%02lu:%02lu",
                 (unsigned long)(sod / 3600), (unsigned long)((sod / 60) % 60), (unsigned long)(sod % 60));
    } else {
        snprintf(date_s, sizeof(date_s), "--.--.----");
        snprintf(time_s, sizeof(time_s), "--:--:--");
    }

    const WeatherDelta w = _receiver.getWeatherData();
    char pres[8], temp[8];
    if (weather_ok && !isnan(w.pressure_hpa))  snprintf(pres, sizeof(pres), "%ld", lround(w.pressure_hpa));
    else                                       snprintf(pres, sizeof(pres), "----");
    if (weather_ok && !isnan(w.temperature_c)) snprintf(temp, sizeof(temp), "%ld", lround(w.temperature_c));
    else                                       snprintf(temp, sizeof(temp), "--");
    snprintf(wx_s, sizeof(wx_s), "%s mbar %s°C", pres, temp);

    if (pos.ok) {
        CelestialFormat::formatLatitude(lat_s, sizeof(lat_s), pos.lat_deg);
        CelestialFormat::formatLongitude(lon_s, sizeof(lon_s), pos.lon_deg);
        strlcat(lat_s, pos.marker, sizeof(lat_s));
        strlcat(lon_s, pos.marker, sizeof(lon_s));
    } else {
        snprintf(lat_s, sizeof(lat_s), "--°--.-'");
        snprintf(lon_s, sizeof(lon_s), "---°--.-'");
    }

    char buf[112];
    snprintf(buf, sizeof(buf), "%s\n%s UTC\n%s\n%s\n%s", date_s, time_s, wx_s, lat_s, lon_s);
    lv_label_set_text(ui_LabelVariables, buf);
}

// Table without bodies: the note in the name column, Hc/Zn headers only
void CelestialUI::showNote(const char* note) {
    char buf[32];
    snprintf(buf, sizeof(buf), "\n\n%s", note);
    lv_label_set_text(ui_LabelBodies, buf);
    lv_label_set_text(ui_LabelHc, "Hc\n\n");
    lv_label_set_text(ui_LabelZn, "Zn\n\n");
}
