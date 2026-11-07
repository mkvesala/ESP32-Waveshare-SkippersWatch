#pragma once

#include <Arduino.h>
#include <lvgl.h>
#include "IScreenUI.h"
#include "ESPNowReceiver.h"
#include "LastPosition.h"
#include "ChartEngine.h"
#include "ChartGeometry.h"

// === C L A S S  C H A R T U I ===
//
// - Sea chart page: a Traficom chart from the microSD card, grid north up, about 1 NM across the
//   display's height, with the boat, its heading, its course and a range ring on top.
// - Realizes IScreenUI over container ui_ContainerChart. The chart image and the symbol layer are
//   made here, the labels come from SquareLine. ui_ContainerTopBar shows its SquareLine background
//   only on this page, because the status bar's white text would vanish on a white chart.
// - COG, HDG, SOG and DPT in the corners, under the same freshness rules as NavigationUI. At
//   night they turn light grey, like the symbols, because red text sinks into the red chart.
// - The map is recentered on the boat when the page is entered (wake and boot included), when an
//   overlay is closed, as soon as a fresh fix returns, when the boat is off the view, and every
//   RECENTER_MS while the display stays on, if the boat has moved. In between the map stands
//   still and the boat moves on it.
// - Symbols, updated once per second:
//   - range ring, always 0.25 NM, so it also shows the scale
//   - solid heading line out to the ring, 0.25 NM (true heading, needs fresh compass data)
//   - dashed COG vector, the distance covered in 3 min at SOG (needs a defined COG and SOG)
//   True bearings are turned to the chart's grid north with the meridian convergence.
// - Never stale: without a fresh fix all symbols are hidden. The map may still be centered on
//   the last known position (LastPosition, up to 1 h old), with "No fix *" or "No fix **" in
//   the same meaning as on the Celestial page. No such position: black, "No fix". A fresh fix
//   always takes over at once.
// - Bow-up, toggled by a double tap on the page: grid bearing of the heading up. The map turns
//   in steps, not continuously. It turns, and is recentered with it, when the heading has been
//   ROTATE_THRESHOLD_DEG or more off the map's for ROTATE_CONFIRM_MS, at most every
//   ROTATE_MIN_INTERVAL_MS. In between the symbols move and turn on it once per second as in
//   north up. A heading gap up to HEADING_LOST_FALLBACK_MS keeps the last angle; a longer one
//   turns the map north up for the rest of the wake, until the next double tap. The choice
//   itself (bowUpSelected) is kept by WatchApplication across sleep. A wake has no heading at
//   first (never stale), so its first map is north up and turns when the first heading lands.
//   A small north arrow on the right edge shows where north is on a turned map.
// - Card reads run on ChartEngine's task on core 0, started by the first visit of a wake, so a
//   wake that stays off this page reads nothing and leaves the PSRAM frames unallocated. Every
//   sleep still mounts and closes the card once (ChartEngine::shutdown).
// - Owns: ChartEngine
// - Owned by: WatchApplication

class ChartUI : public IScreenUI {

public:

    ChartUI(ESPNowReceiver& receiver, LastPosition& last_position);

    void begin() override;
    lv_obj_t* getLvglScreen() const override;
    void onEnter() override;
    void onLeave() override;
    void update() override;
    bool readyForReveal() const override;

    void onDoubleTap() override;

    void setDemo(bool on);
    void setNightMode(bool on);

    // The skipper's choice, kept across sleep by WatchApplication. Set before the first update.
    void setBowUpSelected(bool on);
    bool bowUpSelected() const { return _bow_up_selected; }

    uint32_t chartTaskStackFreeBytes() const { return _engine.stackFreeBytes(); }

    // On the way into deep sleep: finish a read in flight and leave the card idle
    void shutdown();

private:

    static constexpr uint16_t VIEW_W = 410;
    static constexpr uint16_t VIEW_H = 502;

    static constexpr uint32_t RECENTER_MS        = 21000;
    static constexpr uint32_t SYMBOL_REFRESH_MS  = 1000;
    static constexpr uint32_t UPDATE_GAP_MS      = 500;    // update() stops while an overlay covers the page
    static constexpr uint32_t GNSS_TIMEOUT_MS    = 6000;   // as NavigationUI
    static constexpr uint32_t HEADING_TIMEOUT_MS = 5000;   // as NavigationUI
    static constexpr uint32_t DEPTH_TIMEOUT_MS      = 6000;   // as NavigationUI
    static constexpr uint32_t DEPTH_FEED_MAX_AGE_MS = 5000;   // as NavigationUI
    static constexpr uint32_t SHUTDOWN_WAIT_MS   = 1000;
    static constexpr int      MIN_VALID_YEAR     = 2020;   // system clock not yet GNSS-set reads ~1970

    // Bow-up. To be tuned on the water.
    static constexpr uint32_t ROTATE_MIN_INTERVAL_MS   = 3000;    // between two turns of the map
    static constexpr double   ROTATE_THRESHOLD_DEG     = 10.0;    // heading off the map's by this much...
    static constexpr uint32_t ROTATE_CONFIRM_MS        = 2000;    // ...without a break for this long
    static constexpr uint32_t HEADING_LOST_FALLBACK_MS = 21000;   // then north up until a double tap

    static constexpr int32_t NORTH_TIP_PX    = 20;   // north arrow, from its center
    static constexpr int32_t NORTH_BASE_PX   = 12;
    static constexpr double  NORTH_BASE_DEG  = 150.0;
    static constexpr int32_t NORTH_TAIL_PX   = 14;
    static constexpr int32_t NORTH_MARGIN_PX = 30;   // its center from the right edge

    static constexpr double RECENTER_MIN_MOVE_PX = 2.0;
    static constexpr float  COG_MIN_SOG_KNOTS    = 0.5f;
    static constexpr int32_t DASH_PX      = 10;
    static constexpr int32_t DASH_GAP_PX  = 6;
    static constexpr int32_t BOAT_R_PX    = 8;
    static constexpr int32_t LINE_W_PX    = 4;
    static constexpr int32_t REDRAW_PAD_PX = 4;

    static constexpr uint32_t SYMBOL_DAY_RGB  = 0xEB0000;
    static constexpr uint32_t SYMBOL_NIGHT_RGB = 0xBEBEBE;   // stands out from the red night chart
    static constexpr uint32_t CORNER_NIGHT_RGB = 0xA0A0A0;   // a little darker than the symbols

    // What the symbol layer shows, in whole pixels and degrees: nothing is redrawn until
    // one of these changes
    struct Symbols {
        bool    visible   = false;
        int32_t x         = 0;
        int32_t y         = 0;
        bool    hdg_valid = false;
        int32_t hdg_deg   = 0;
        bool    cog_valid = false;
        int32_t cog_deg   = 0;
        int32_t cog_len   = 0;
    };

    ESPNowReceiver& _receiver;
    LastPosition& _last_position;
    ChartEngine _engine;

    bool _initialized = false;
    bool _demo = false;
    bool _night = false;

    lv_obj_t* _image = nullptr;
    lv_obj_t* _symbol_layer = nullptr;
    lv_image_dsc_t _dsc = {};

    // The frame on display
    bool _have_frame = false;
    bool _frame_rotated = false;
    double _frame_rot_deg = 0.0;
    ChartFormat::View _view = {};
    double _boat_px = 0.0;
    double _boat_py = 0.0;
    double _frame_e = 0.0;
    double _frame_n = 0.0;
    double _m_per_px = 0.0;
    ChartEngine::Status _status = ChartEngine::Status::OK;

    // Recentering
    bool _need_center = true;
    bool _fix_was_lost = true;
    bool _have_request = false;
    bool _awaiting_result = false;
    double _request_lat = 0.0;
    double _request_lon = 0.0;
    uint32_t _last_center_ms = 0;
    uint32_t _last_update_ms = 0;
    bool _request_rotated = false;
    double _request_rot_deg = 0.0;

    // Bow-up
    bool _bow_up_selected = false;   // the skipper's choice
    bool _bow_up_active = false;     // this wake: off after a long heading gap
    bool _orient_valid = false;      // _orient_deg comes from a heading of this wake
    double _orient_deg = 0.0;        // grid bearing up on the maps requested from now on
    bool _reorient_now = false;      // page entered or bow-up turned on: take the heading at once
    uint32_t _bow_up_since_ms = 0;   // the heading gap is counted from here at the earliest
    uint32_t _last_rotate_ms = 0;
    bool _rotate_pending = false;    // heading off by the threshold, since _rotate_off_since_ms
    uint32_t _rotate_off_since_ms = 0;
    double _last_hdg_true = 0.0;     // newest fresh heading of this wake
    uint32_t _last_hdg_ms = 0;       // 0: none yet

    uint32_t _last_symbol_ms = 0;
    Symbols _drawn;
    int32_t _ring_px = 0;
    int32_t _drawn_north = -1;       // north arrow's screen bearing, -1: hidden

    char _label_text[24] = "";
    lv_opa_t _top_bar_opa = LV_OPA_TRANSP;   // the SquareLine value, used on this page only

    void takeEngineResult();
    void center(double lat_deg, double lon_deg, uint32_t now);
    void sendRequest();
    void trackHeading(uint32_t now);
    void updateOrientation(const GnssData& gnss, bool fresh, uint32_t now);
    void fallBackToNorthUp();
    void applyPosition(const GnssData& gnss, bool fresh, uint32_t now);
    void refreshSymbols(const GnssData& gnss, bool fresh, uint32_t now);
    void refreshValues(const GnssData& gnss, bool fresh, uint32_t now);
    void showWaiting();
    void showLabel(const char* text);
    void applyNightStyles();
    bool lastKnown(uint32_t now, LastPosition::Fix& out);

    lv_area_t symbolArea(const Symbols& s) const;
    lv_area_t northArea() const;
    void refreshNorthArrow(bool show_map);
    void drawNorthArrow(lv_layer_t* layer) const;
    void drawSymbols(lv_layer_t* layer) const;
    static void drawEvent(lv_event_t* e);
    static void setValue(lv_obj_t* label, const char* text);

};
