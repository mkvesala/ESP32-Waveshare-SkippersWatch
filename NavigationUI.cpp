#include "NavigationUI.h"
#include "ui.h"
#include <math.h>

// === P U B L I C ===

NavigationUI::NavigationUI(ESPNowReceiver& receiver)
    : _receiver(receiver) {}

lv_obj_t* NavigationUI::getLvglScreen() const {
    return ui_ContainerNavigation;
}

void NavigationUI::begin() {
    if (_initialized) return;
    this->showWaiting();
    _initialized = true;
}

// On entering the page, treat everything as stale until a fresh packet arrives
void NavigationUI::onEnter() {
    this->showWaiting();
}

void NavigationUI::update() {
    if (!_initialized) return;

    const uint32_t now = millis();

    // --- Heading (HDG) ---
    // Freshness gates the display; getData() just returns the latest cached value.
    HeadingData hdg = _receiver.getData();
    uint32_t hdg_rx = _receiver.lastHeadingRxMillis();
    bool hdg_ok = (hdg_rx > 0) && ((now - hdg_rx) < HEADING_TIMEOUT_MS);
    if (hdg_ok) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%03u", hdg.getHeadingMagDeg());
        lv_label_set_text(ui_LabelHDGValue, buf);
    } else {
        lv_label_set_text(ui_LabelHDGValue, "---");
    }

    // --- GNSS (COG / SOG / POS) ---
    GnssData gnss = _receiver.getGnssData();
    uint32_t gnss_rx = _receiver.lastGnssRxMillis();
    bool gnss_ok = (gnss_rx > 0) && ((now - gnss_rx) < GNSS_TIMEOUT_MS);

    if (gnss_ok && gnss.hasFix()) {
        char buf[16];
        // COG is undefined when stationary (at anchor) — dashes there, but SOG and position
        // still show. hasFix() gates position/SOG; hasCog() gates course, independently.
        if (gnss.hasCog()) {
            snprintf(buf, sizeof(buf), "%03u", gnss.getCogDeg());
            lv_label_set_text(ui_LabelCOGValue, buf);
        } else {
            lv_label_set_text(ui_LabelCOGValue, "---");
        }

        snprintf(buf, sizeof(buf), "%.1f", gnss.getSogKnots());
        lv_label_set_text(ui_LabelSOGValue, buf);

        char pos[40];
        this->formatPosition(gnss.lat_deg, gnss.lon_deg, pos, sizeof(pos));
        lv_label_set_text(ui_LabelPOSValue, pos);
    } else {
        // Connected but no fix still shows placeholders — never a stale position
        lv_label_set_text(ui_LabelCOGValue, "---");
        lv_label_set_text(ui_LabelSOGValue, "--.-");
        lv_label_set_text(ui_LabelPOSValue, "--°--.-'\n--°--.-'");
    }

    // --- Depth (DPT, below surface) ---
    // Two-fold freshness: gateway alive (RX timestamp) AND the relayed feed alive (age_ms).
    // Either stale, or a NAN value, shows "---" — a frozen depth is worse than none.
    DepthDelta depth = _receiver.getDepthData();
    uint32_t depth_rx = _receiver.lastDepthRxMillis();
    bool depth_link_ok = (depth_rx > 0) && ((now - depth_rx) < DEPTH_TIMEOUT_MS);
    bool depth_feed_ok = (depth.age_ms != UINT32_MAX) && (depth.age_ms < DEPTH_FEED_MAX_AGE_MS);
    if (depth_link_ok && depth_feed_ok && !isnan(depth.below_surface_m)) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%.1f", depth.below_surface_m);
        lv_label_set_text(ui_LabelDPTValue, buf);
    } else {
        lv_label_set_text(ui_LabelDPTValue, "--.-");
    }
}

// === P R I V A T E ===

void NavigationUI::showWaiting() {
    lv_label_set_text(ui_LabelHDGValue, "---");
    lv_label_set_text(ui_LabelCOGValue, "---");
    lv_label_set_text(ui_LabelSOGValue, "--.-");
    lv_label_set_text(ui_LabelDPTValue, "--.-");
    lv_label_set_text(ui_LabelPOSValue, "--°--.-'\n--°--.-'");
}

// Decimal degrees → "DD°MM.m'H" per line (font charset is ASCII + ° only)
void NavigationUI::formatPosition(float lat_deg, float lon_deg, char* out, size_t out_len) {
    if (isnan(lat_deg) || isnan(lon_deg)) {
        snprintf(out, out_len, "--°--.-'\n--°--.-'");
        return;
    }
    char ns = (lat_deg >= 0.0f) ? 'N' : 'S';
    char ew = (lon_deg >= 0.0f) ? 'E' : 'W';
    float alat = fabsf(lat_deg);
    float alon = fabsf(lon_deg);
    uint16_t lat_d = (uint16_t)alat;
    uint16_t lon_d = (uint16_t)alon;
    float lat_m = (alat - lat_d) * 60.0f;
    float lon_m = (alon - lon_d) * 60.0f;
    snprintf(out, out_len, "%02u°%04.1f'%c\n%03u°%04.1f'%c",
             lat_d, lat_m, ns, lon_d, lon_m, ew);
}
