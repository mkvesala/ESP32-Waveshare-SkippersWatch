#include "ClockUI.h"
#include "ui.h"
#include <time.h>

// === P U B L I C ===

lv_obj_t* ClockUI::getLvglScreen() const {
    return ui_ContainerClock;
}

void ClockUI::begin() {
    if (_initialized) return;
    this->showWaiting();
    _initialized = true;
}

// On entering the page, blank the cache and render immediately so the current time
// shows in the first frame rather than after the next update tick.
void ClockUI::onEnter() {
    _last_shown_sec = -1;
    this->update();
}

void ClockUI::onLeave() {
    _last_shown_sec = -1;
}

void ClockUI::update() {
    if (!_initialized) return;

    struct timeval tv;
    gettimeofday(&tv, nullptr);

    // Not yet disciplined by GNSS (system clock still ~1970) → never show a made-up time,
    // except in demo mode, which says so on the top bar
    time_t t = tv.tv_sec;
    struct tm lt;
    localtime_r(&t, &lt);
    if (lt.tm_year + 1900 < MIN_VALID_YEAR) {
        if (!_demo) {
            if (_last_shown_sec != -1) { this->showWaiting(); _last_shown_sec = -1; }
            return;
        }
        t = DEMO_EPOCH_UTC + (time_t)(millis() / 1000);
        localtime_r(&t, &lt);
    }

    // Tick the display at 1 Hz — skip redundant LVGL writes within the same second
    if ((long)t == _last_shown_sec) return;
    _last_shown_sec = (long)t;

    struct tm ut;
    gmtime_r(&t, &ut);

    char buf[16];

    // Local date (DD.MM.YYYY, Finnish convention) + weekday abbreviation
    static const char* const WD[7] = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };
    snprintf(buf, sizeof(buf), "%02d.%02d.%04d", lt.tm_mday, lt.tm_mon + 1, lt.tm_year + 1900);
    lv_label_set_text(ui_LabelDate, buf);
    lv_label_set_text(ui_LabelDay, WD[lt.tm_wday % 7]);

    // Local + UTC time, HH:MM:SS (24h)
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", lt.tm_hour, lt.tm_min, lt.tm_sec);
    lv_label_set_text(ui_LabelTimeLocal, buf);
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", ut.tm_hour, ut.tm_min, ut.tm_sec);
    lv_label_set_text(ui_LabelTimeUTC, buf);

    // PREV./NEXT under the UTC time when its calendar day differs from local.
    // Local and UTC differ by at most one day, so comparing (year, day-of-year) is enough.
    // In Finland (UTC+2/+3) UTC is behind local → PREV. or blank, never NEXT.
    const char* rel = "";
    if (ut.tm_year != lt.tm_year) {
        rel = (ut.tm_year < lt.tm_year) ? "PREV." : "NEXT.";
    } else if (ut.tm_yday != lt.tm_yday) {
        rel = (ut.tm_yday < lt.tm_yday) ? "PREV." : "NEXT.";
    }
    lv_label_set_text(ui_LabelDateUTC, rel);
}

// === P R I V A T E ===

void ClockUI::showWaiting() {
    lv_label_set_text(ui_LabelDate, "--.--.----");
    lv_label_set_text(ui_LabelDay, "---");
    lv_label_set_text(ui_LabelTimeLocal, "--:--:--");
    lv_label_set_text(ui_LabelTimeUTC, "--:--:--");
    lv_label_set_text(ui_LabelDateUTC, "");
}
