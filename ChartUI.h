#pragma once

#include <Arduino.h>
#include <lvgl.h>
#include "IScreenUI.h"
#include "ESPNowReceiver.h"
#include "LastPosition.h"
#include "ChartEngine.h"

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

    void setDemo(bool on);
    void setNightMode(bool on);

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

    uint32_t _last_symbol_ms = 0;
    Symbols _drawn;
    int32_t _ring_px = 0;

    char _label_text[24] = "";
    lv_opa_t _top_bar_opa = LV_OPA_TRANSP;   // the SquareLine value, used on this page only

    void takeEngineResult();
    void center(double lat_deg, double lon_deg, uint32_t now);
    void applyPosition(const GnssData& gnss, bool fresh, uint32_t now);
    void refreshSymbols(const GnssData& gnss, bool fresh, uint32_t now);
    void refreshValues(const GnssData& gnss, bool fresh, uint32_t now);
    void showWaiting();
    void showLabel(const char* text);
    void applyNightStyles();
    bool lastKnown(uint32_t now, LastPosition::Fix& out);

    lv_area_t symbolArea(const Symbols& s) const;
    void drawSymbols(lv_layer_t* layer) const;
    static void drawEvent(lv_event_t* e);
    static void setValue(lv_obj_t* label, const char* text);

};
