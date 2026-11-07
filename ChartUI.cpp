#include "ChartUI.h"
#include "ChartGeometry.h"
#include "CelestialFormat.h"
#include "NightPalette.h"
#include "ui.h"
#include <math.h>
#include <sys/time.h>
#include <time.h>

// === P U B L I C ===

ChartUI::ChartUI(ESPNowReceiver& receiver, LastPosition& last_position)
    : _receiver(receiver), _last_position(last_position), _engine(VIEW_W, VIEW_H) {}

lv_obj_t* ChartUI::getLvglScreen() const {
    return ui_ContainerChart;
}

void ChartUI::begin() {
    if (_initialized) return;

    // Black behind everything: where there is no chart, the page must not look like one
    lv_obj_set_style_bg_color(ui_ContainerChart, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ui_ContainerChart, LV_OPA_COVER, LV_PART_MAIN);

    _dsc.header.magic  = LV_IMAGE_HEADER_MAGIC;
    _dsc.header.cf     = LV_COLOR_FORMAT_RGB565;
    _dsc.header.w      = VIEW_W;
    _dsc.header.h      = VIEW_H;
    _dsc.header.stride = VIEW_W * sizeof(uint16_t);
    _dsc.data_size     = (uint32_t)VIEW_W * VIEW_H * sizeof(uint16_t);

    _image = lv_image_create(ui_ContainerChart);
    lv_obj_remove_flag(_image, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(_image, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(_image, 0, 0);

    _symbol_layer = lv_obj_create(ui_ContainerChart);
    lv_obj_remove_style_all(_symbol_layer);
    lv_obj_set_size(_symbol_layer, VIEW_W, VIEW_H);
    lv_obj_set_pos(_symbol_layer, 0, 0);
    lv_obj_remove_flag(_symbol_layer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(_symbol_layer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(_symbol_layer, &ChartUI::drawEvent, LV_EVENT_DRAW_MAIN, this);

    // Under the SquareLine labels: image first, then the symbols
    lv_obj_move_to_index(_image, 0);
    lv_obj_move_to_index(_symbol_layer, 1);

    // Orbitron has no copyright sign
    lv_label_set_text(ui_LabelChartAttribution, "(c) Traficom");
    lv_obj_add_flag(ui_LabelChartStatus, LV_OBJ_FLAG_HIDDEN);
    this->applyNightStyles();
    this->showWaiting();

    _top_bar_opa = lv_obj_get_style_bg_opa(ui_ContainerTopBar, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ui_ContainerTopBar, LV_OPA_TRANSP, LV_PART_MAIN);

    _initialized = true;
}

void ChartUI::onEnter() {
    _need_center = true;
    _last_update_ms = millis();
    // Bow-up: the map takes the current or the newest heading at once, without the confirm wait
    _reorient_now = true;
    _bow_up_since_ms = _last_update_ms;
    lv_obj_set_style_bg_opa(ui_ContainerTopBar, _top_bar_opa, LV_PART_MAIN);
    this->showWaiting();
}

void ChartUI::onLeave() {
    lv_obj_set_style_bg_opa(ui_ContainerTopBar, LV_OPA_TRANSP, LV_PART_MAIN);
}

void ChartUI::update() {
    if (!_initialized) return;
    const uint32_t now = millis();

    // update() does not run while the menu or Status covers the page, so a gap means that
    // one was just closed: back on the page, the boat goes to the center again
    if (now - _last_update_ms > UPDATE_GAP_MS) _need_center = true;
    _last_update_ms = now;

    this->takeEngineResult();

    const GnssData gnss = _receiver.getGnssData();
    const uint32_t rx = _receiver.lastGnssRxMillis();
    const bool fresh = rx > 0 && (now - rx) < GNSS_TIMEOUT_MS && gnss.hasFix();

    this->trackHeading(now);
    this->updateOrientation(gnss, fresh, now);
    this->applyPosition(gnss, fresh, now);
    this->refreshSymbols(gnss, fresh, now);
}

// On: bow-up, also after a fallback to north up in this wake. Off: north up, and stays so.
void ChartUI::onDoubleTap() {
    if (!_initialized) return;
    if (_bow_up_active) {
        _bow_up_selected = false;
        _bow_up_active = false;
        _orient_valid = false;
        _rotate_pending = false;
        _need_center = true;
    } else {
        _bow_up_selected = true;
        _bow_up_active = true;
        _reorient_now = true;
        _rotate_pending = false;
        _bow_up_since_ms = millis();
    }
    Serial.printf("[chart] %s\n", _bow_up_active ? "bow up" : "north up");
}

void ChartUI::setBowUpSelected(bool on) {
    _bow_up_selected = on;
    _bow_up_active = on;
    _reorient_now = on;
    _orient_valid = false;
    _bow_up_since_ms = millis();
}

bool ChartUI::readyForReveal() const {
    return !_awaiting_result;
}

void ChartUI::setDemo(bool on) {
    if (on == _demo) return;
    _demo = on;
    _need_center = true;
}

void ChartUI::setNightMode(bool on) {
    if (on == _night) return;
    _night = on;
    if (!_initialized) return;
    this->applyNightStyles();
    lv_obj_invalidate(_symbol_layer);
    // Same place, other palette: no need to wait for the next fix
    if (_have_request) {
        this->sendRequest();
        _awaiting_result = true;
    }
}

void ChartUI::shutdown() {
    _engine.shutdown(SHUTDOWN_WAIT_MS);
}

// === P R I V A T E ===

void ChartUI::takeEngineResult() {
    ChartEngine::Result r;
    if (!_engine.takeResult(r)) return;
    if (!_engine.isBusy()) _awaiting_result = false;

    // No memory for the bow-up source square: north up still works
    if (r.rotated && r.status == ChartEngine::Status::NO_MEMORY) {
        this->fallBackToNorthUp();
        return;
    }

    _status = r.status;
    if (r.pixels == nullptr) return;

    const bool first = !_have_frame;
    _have_frame = true;
    _frame_rotated = r.rotated;
    _frame_rot_deg = r.rot_deg;
    _view = r.view;
    _boat_px = r.boat_px;
    _boat_py = r.boat_py;
    _m_per_px = r.m_per_px;
    const ChartGeometry::GridPoint g = ChartGeometry::wgs84ToTm35(r.lat_deg, r.lon_deg);
    _frame_e = g.e;
    _frame_n = g.n;
    _ring_px = (int32_t)lroundf(ChartGeometry::ringRadiusPx(_m_per_px));

    _dsc.data = reinterpret_cast<const uint8_t*>(r.pixels);
    if (first) lv_image_set_src(_image, &_dsc);
    lv_obj_invalidate(_image);
    _last_symbol_ms = 0;   // the symbols move to the new frame at once
}

void ChartUI::center(double lat_deg, double lon_deg, uint32_t now) {
    _request_lat = lat_deg;
    _request_lon = lon_deg;
    _have_request = true;
    _need_center = false;
    _last_center_ms = now;
    _awaiting_result = true;
    _request_rotated = _bow_up_active && _orient_valid;
    _request_rot_deg = _orient_deg;
    this->sendRequest();
}

void ChartUI::sendRequest() {
    if (_request_rotated) _engine.requestRotated(_request_lat, _request_lon, _night, _request_rot_deg);
    else                  _engine.request(_request_lat, _request_lon, _night);
}

// The newest fresh heading of this wake, kept through short gaps for bow-up
void ChartUI::trackHeading(uint32_t now) {
    const uint32_t rx = _receiver.lastHeadingRxMillis();
    if (rx > 0 && (now - rx) < HEADING_TIMEOUT_MS) {
        _last_hdg_true = _receiver.getData().heading_true_x10 / 10.0;
        _last_hdg_ms = rx;
    }
}

// Decides when a bow-up map turns. A turn only sets the angle and asks for a recenter, which
// applyPosition() then sends with it.
void ChartUI::updateOrientation(const GnssData& gnss, bool fresh, uint32_t now) {
    if (!_bow_up_active) return;

    const bool recent = _last_hdg_ms != 0 && (now - _last_hdg_ms) <= HEADING_LOST_FALLBACK_MS;
    if (!recent) {
        _rotate_pending = false;
        if (now - _bow_up_since_ms > HEADING_LOST_FALLBACK_MS) this->fallBackToNorthUp();
        return;   // a short gap keeps the last angle; a new wake waits for its first heading
    }

    // The convergence needs a position: the fix, or the one the map is centered on
    double lat, lon;
    if (fresh) {
        lat = gnss.lat_deg;
        lon = gnss.lon_deg;
    } else if (_have_request) {
        lat = _request_lat;
        lon = _request_lon;
    } else {
        return;
    }
    const double grid = ChartGeometry::gridBearingDeg(_last_hdg_true, ChartGeometry::convergenceDeg(lat, lon));

    if (_reorient_now || !_orient_valid) {
        _reorient_now = false;
        _orient_valid = true;
        _orient_deg = grid;
        _last_rotate_ms = now;
        _rotate_pending = false;
        _need_center = true;
        return;
    }

    // Only a live heading turns the map; in a gap the angle holds
    if ((now - _last_hdg_ms) >= HEADING_TIMEOUT_MS ||
        fabs(ChartGeometry::headingDelta(grid, _orient_deg)) < ROTATE_THRESHOLD_DEG) {
        _rotate_pending = false;
        return;
    }
    if (!_rotate_pending) {
        _rotate_pending = true;
        _rotate_off_since_ms = now;
    }
    if ((now - _rotate_off_since_ms) >= ROTATE_CONFIRM_MS && (now - _last_rotate_ms) >= ROTATE_MIN_INTERVAL_MS) {
        _orient_deg = grid;
        _last_rotate_ms = now;
        _rotate_pending = false;
        _need_center = true;
    }
}

// For the rest of the wake: only a double tap brings bow-up back
void ChartUI::fallBackToNorthUp() {
    if (!_bow_up_active) return;
    _bow_up_active = false;
    _orient_valid = false;
    _rotate_pending = false;
    _need_center = true;
    Serial.println("[chart] no heading: north up");
}

// Decides when the map moves. A fresh fix always wins; the last known position only serves
// to put a map on the screen until one arrives.
void ChartUI::applyPosition(const GnssData& gnss, bool fresh, uint32_t now) {
    const bool due = (now - _last_center_ms) >= RECENTER_MS;
    const bool card_failed = _status == ChartEngine::Status::NO_CARD ||
                             _status == ChartEngine::Status::NO_MEMORY;

    if (fresh) {
        const double lat = gnss.lat_deg;
        const double lon = gnss.lon_deg;
        if (_need_center || _fix_was_lost || !_have_frame) {
            // !_have_frame would re-request on every tick: only once the last one is done
            if (_need_center || _fix_was_lost || (due && !_awaiting_result)) this->center(lat, lon, now);
            _fix_was_lost = false;
        } else {
            const ChartGeometry::GridPoint g = ChartGeometry::wgs84ToTm35(lat, lon);
            double dx = (g.e - _frame_e) / _m_per_px;
            double dy = (g.n - _frame_n) / _m_per_px;
            if (_frame_rotated) {
                // Screen axes of the turned frame (dy is the northing here, so south is -dy)
                const ChartGeometry::ScreenPoint o = ChartGeometry::rotateOffset(dx, -dy, _frame_rot_deg);
                dx = o.x;
                dy = o.y;
            }
            // A boat off the view does not wait for its turn (the demo track's loop jumps)
            const bool off_view = !_awaiting_result && (fabs(dx) >= VIEW_W / 2.0 || fabs(dy) >= VIEW_H / 2.0);
            if (due || off_view) {
                if (card_failed || fabs(dx) >= RECENTER_MIN_MOVE_PX || fabs(dy) >= RECENTER_MIN_MOVE_PX) {
                    this->center(lat, lon, now);
                } else {
                    _last_center_ms = now;   // at anchor: no read, look again in a while
                }
            }
        }
        return;
    }

    _fix_was_lost = true;
    LastPosition::Fix fix;
    if (!this->lastKnown(now, fix)) return;
    // A card put in later is found: retry at the same slow pace as a recenter
    if (_need_center || (card_failed && due)) this->center(fix.lat_deg, fix.lon_deg, now);
}

void ChartUI::refreshSymbols(const GnssData& gnss, bool fresh, uint32_t now) {
    if (now - _last_symbol_ms < SYMBOL_REFRESH_MS) return;
    _last_symbol_ms = now;

    this->refreshValues(gnss, fresh, now);

    const bool card_failed = _status == ChartEngine::Status::NO_CARD;
    const bool no_memory = _status == ChartEngine::Status::NO_MEMORY;

    // Map and message
    LastPosition::Fix fix;
    const bool known = !fresh && this->lastKnown(now, fix);
    const bool any_position = fresh || known;

    char text[sizeof(_label_text)] = "";
    if (card_failed) {
        strlcpy(text, "No chart card", sizeof(text));
    } else if (no_memory) {
        strlcpy(text, "No chart memory", sizeof(text));
    } else if (!any_position) {
        strlcpy(text, "No fix", sizeof(text));
    } else if (!fresh) {
        const char* marker = CelestialFormat::storedPositionMarker(fix.age_s);
        snprintf(text, sizeof(text), "No fix %s", marker != nullptr ? marker : "");
    } else if (_have_frame && _status == ChartEngine::Status::NO_DATA) {
        strlcpy(text, "No chart here", sizeof(text));
    }
    this->showLabel(text);

    const bool show_map = _have_frame && any_position && !card_failed && !no_memory;
    if (show_map) lv_obj_remove_flag(_image, LV_OBJ_FLAG_HIDDEN);
    else          lv_obj_add_flag(_image, LV_OBJ_FLAG_HIDDEN);
    this->refreshNorthArrow(show_map);

    // Symbols: a fresh fix, and a frame to place them on
    Symbols next;
    if (fresh && show_map && _m_per_px > 0.0) {
        const double lat = gnss.lat_deg;
        const double lon = gnss.lon_deg;
        const ChartGeometry::GridPoint g = ChartGeometry::wgs84ToTm35(lat, lon);
        const double conv = ChartGeometry::convergenceDeg(lat, lon);
        next.visible = true;
        if (_frame_rotated) {
            // A turned frame has the request's position at the view's center
            const ChartGeometry::ScreenPoint o = ChartGeometry::rotateOffset(
                (g.e - _frame_e) / _m_per_px, -(g.n - _frame_n) / _m_per_px, _frame_rot_deg);
            next.x = (int32_t)lround(VIEW_W / 2 + o.x);
            next.y = (int32_t)lround(VIEW_H / 2 + o.y);
        } else {
            next.x = (int32_t)lround(_boat_px - _view.x0 + (g.e - _frame_e) / _m_per_px);
            next.y = (int32_t)lround(_boat_py - _view.y0 - (g.n - _frame_n) / _m_per_px);
        }
        // Bearings as drawn: north up unchanged, bow-up relative to the frame's angle
        const auto onScreen = [&](double grid_deg) {
            const double b = _frame_rotated ? ChartGeometry::screenBearing(grid_deg, _frame_rot_deg) : grid_deg;
            return (int32_t)lround(b);
        };

        const HeadingData hdg = _receiver.getData();
        const uint32_t hdg_rx = _receiver.lastHeadingRxMillis();
        if (hdg_rx > 0 && (now - hdg_rx) < HEADING_TIMEOUT_MS) {
            next.hdg_valid = true;
            next.hdg_deg = onScreen(ChartGeometry::gridBearingDeg(hdg.heading_true_x10 / 10.0, conv));
        }
        if (gnss.hasCog() && gnss.getSogKnots() >= COG_MIN_SOG_KNOTS) {
            next.cog_valid = true;
            next.cog_deg = onScreen(ChartGeometry::gridBearingDeg(gnss.cog_true_x10 / 10.0, conv));
            next.cog_len = (int32_t)lroundf(ChartGeometry::cogVectorPx(gnss.getSogKnots(), _m_per_px));
        }
    }

    const bool changed = next.visible != _drawn.visible || next.x != _drawn.x || next.y != _drawn.y ||
                         next.hdg_valid != _drawn.hdg_valid || next.hdg_deg != _drawn.hdg_deg ||
                         next.cog_valid != _drawn.cog_valid || next.cog_deg != _drawn.cog_deg ||
                         next.cog_len != _drawn.cog_len;
    if (!changed) return;

    if (_drawn.visible) {
        const lv_area_t old_area = this->symbolArea(_drawn);
        lv_obj_invalidate_area(_symbol_layer, &old_area);
    }
    if (next.visible) {
        const lv_area_t new_area = this->symbolArea(next);
        lv_obj_invalidate_area(_symbol_layer, &new_area);
    }
    _drawn = next;
}

// The same readings and freshness rules as NavigationUI
void ChartUI::refreshValues(const GnssData& gnss, bool fresh, uint32_t now) {
    char buf[8];

    const HeadingData hdg = _receiver.getData();
    const uint32_t hdg_rx = _receiver.lastHeadingRxMillis();
    if (hdg_rx > 0 && (now - hdg_rx) < HEADING_TIMEOUT_MS) {
        snprintf(buf, sizeof(buf), "%03u", hdg.getHeadingTrueDeg());
        setValue(ui_LabelHDGValue2, buf);
    } else {
        setValue(ui_LabelHDGValue2, "---");
    }

    if (fresh && gnss.hasCog()) {
        snprintf(buf, sizeof(buf), "%03u", gnss.getCogDeg());
        setValue(ui_LabelCOGValue2, buf);
    } else {
        setValue(ui_LabelCOGValue2, "---");
    }

    if (fresh) {
        snprintf(buf, sizeof(buf), "%.1f", gnss.getSogKnots());
        setValue(ui_LabelSOGValue2, buf);
    } else {
        setValue(ui_LabelSOGValue2, "--.-");
    }

    const DepthDelta depth = _receiver.getDepthData();
    const uint32_t depth_rx = _receiver.lastDepthRxMillis();
    const bool depth_link_ok = depth_rx > 0 && (now - depth_rx) < DEPTH_TIMEOUT_MS;
    const bool depth_feed_ok = depth.age_ms != UINT32_MAX && depth.age_ms < DEPTH_FEED_MAX_AGE_MS;
    if (depth_link_ok && depth_feed_ok && !isnan(depth.below_surface_m)) {
        snprintf(buf, sizeof(buf), "%.1f", depth.below_surface_m);
        setValue(ui_LabelDPTValue2, buf);
    } else {
        setValue(ui_LabelDPTValue2, "--.-");
    }
}

void ChartUI::showWaiting() {
    setValue(ui_LabelHDGValue2, "---");
    setValue(ui_LabelCOGValue2, "---");
    setValue(ui_LabelSOGValue2, "--.-");
    setValue(ui_LabelDPTValue2, "--.-");
}

// Setting a label's text redraws it even when nothing changed, and here each redraw blends
// over the chart image in PSRAM
void ChartUI::setValue(lv_obj_t* label, const char* text) {
    if (strcmp(lv_label_get_text(label), text) != 0) lv_label_set_text(label, text);
}

void ChartUI::showLabel(const char* text) {
    if (strcmp(text, _label_text) == 0) return;
    strlcpy(_label_text, text, sizeof(_label_text));
    if (text[0] == '\0') {
        lv_obj_add_flag(ui_LabelChartStatus, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_label_set_text(ui_LabelChartStatus, text);
        lv_obj_remove_flag(ui_LabelChartStatus, LV_OBJ_FLAG_HIDDEN);
    }
}

void ChartUI::applyNightStyles() {
    lv_obj_set_style_text_color(ui_LabelChartAttribution,
                                lv_color_hex(_night ? NightPalette::RED_MID : 0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_color(ui_LabelChartAttribution,
                              lv_color_hex(_night ? 0x000000 : 0xFFFFFF), LV_PART_MAIN);

    // Red text would sink into the red night chart. By day these inherit the screen's color.
    lv_obj_t* const corners[] = {
        ui_LabelCOGTitle2, ui_LabelCOGValue2, ui_LabelHDGTitle2, ui_LabelHDGValue2,
        ui_LabelSOGTitle2, ui_LabelSOGValue2, ui_LabelDPTTitle2, ui_LabelDPTValue2,
    };
    for (lv_obj_t* label : corners) {
        if (_night) lv_obj_set_style_text_color(label, lv_color_hex(CORNER_NIGHT_RGB), LV_PART_MAIN);
        else lv_obj_remove_local_style_prop(label, LV_STYLE_TEXT_COLOR, LV_PART_MAIN);
    }
}

// The newest usable position other than a live fix. Demo positions are never stored.
bool ChartUI::lastKnown(uint32_t now, LastPosition::Fix& out) {
    if (_demo) return false;
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    struct tm utc_tm;
    const time_t t = tv.tv_sec;
    gmtime_r(&t, &utc_tm);
    if (utc_tm.tm_year + 1900 < MIN_VALID_YEAR) return false;
    return _last_position.lookup((double)tv.tv_sec, now, out);
}

lv_area_t ChartUI::symbolArea(const Symbols& s) const {
    int32_t x1 = s.x - _ring_px;
    int32_t x2 = s.x + _ring_px;
    int32_t y1 = s.y - _ring_px;
    int32_t y2 = s.y + _ring_px;

    const auto include = [&](int32_t bearing, int32_t length) {
        const ChartGeometry::ScreenPoint p = ChartGeometry::lineEnd((float)s.x, (float)s.y, bearing, (float)length);
        x1 = min(x1, (int32_t)floorf(p.x));
        x2 = max(x2, (int32_t)ceilf(p.x));
        y1 = min(y1, (int32_t)floorf(p.y));
        y2 = max(y2, (int32_t)ceilf(p.y));
    };
    if (s.hdg_valid) include(s.hdg_deg, _ring_px);
    if (s.cog_valid) include(s.cog_deg, s.cog_len);

    lv_area_t a;
    a.x1 = x1 - REDRAW_PAD_PX;
    a.y1 = y1 - REDRAW_PAD_PX;
    a.x2 = x2 + REDRAW_PAD_PX;
    a.y2 = y2 + REDRAW_PAD_PX;
    return a;
}

// Fixed place and size, so one area covers the arrow at any angle
lv_area_t ChartUI::northArea() const {
    const int32_t cx = VIEW_W - NORTH_MARGIN_PX;
    const int32_t cy = VIEW_H / 2;
    const int32_t r = NORTH_TIP_PX + LINE_W_PX + REDRAW_PAD_PX;
    lv_area_t a;
    a.x1 = cx - r;
    a.y1 = cy - r;
    a.x2 = cx + r;
    a.y2 = cy + r;
    return a;
}

// Only on a turned map, fix or not: it tells how the map lies, not where the boat is
void ChartUI::refreshNorthArrow(bool show_map) {
    const int32_t next = (show_map && _frame_rotated)
        ? (int32_t)lround(ChartGeometry::screenBearing(0.0, _frame_rot_deg)) % 360 : -1;
    if (next == _drawn_north) return;
    _drawn_north = next;
    const lv_area_t a = this->northArea();
    lv_obj_invalidate_area(_symbol_layer, &a);
}

void ChartUI::drawNorthArrow(lv_layer_t* layer) const {
    if (_drawn_north < 0) return;
    const float cx = (float)(VIEW_W - NORTH_MARGIN_PX);
    const float cy = (float)(VIEW_H / 2);
    const double b = (double)_drawn_north;
    const lv_color_t color = lv_color_hex(_night ? SYMBOL_NIGHT_RGB : SYMBOL_DAY_RGB);

    lv_draw_line_dsc_t tail;
    lv_draw_line_dsc_init(&tail);
    tail.color = color;
    tail.width = LINE_W_PX;
    const ChartGeometry::ScreenPoint end = ChartGeometry::lineEnd(cx, cy, b + 180.0, (float)NORTH_TAIL_PX);
    tail.p1.x = (lv_value_precise_t)cx;
    tail.p1.y = (lv_value_precise_t)cy;
    tail.p2.x = (lv_value_precise_t)end.x;
    tail.p2.y = (lv_value_precise_t)end.y;
    lv_draw_line(layer, &tail);

    lv_draw_triangle_dsc_t head;
    lv_draw_triangle_dsc_init(&head);
    head.color = color;
    head.opa = LV_OPA_COVER;
    const ChartGeometry::ScreenPoint tip = ChartGeometry::lineEnd(cx, cy, b, (float)NORTH_TIP_PX);
    const ChartGeometry::ScreenPoint left = ChartGeometry::lineEnd(cx, cy, b - NORTH_BASE_DEG, (float)NORTH_BASE_PX);
    const ChartGeometry::ScreenPoint right = ChartGeometry::lineEnd(cx, cy, b + NORTH_BASE_DEG, (float)NORTH_BASE_PX);
    head.p[0].x = (lv_value_precise_t)tip.x;
    head.p[0].y = (lv_value_precise_t)tip.y;
    head.p[1].x = (lv_value_precise_t)left.x;
    head.p[1].y = (lv_value_precise_t)left.y;
    head.p[2].x = (lv_value_precise_t)right.x;
    head.p[2].y = (lv_value_precise_t)right.y;
    lv_draw_triangle(layer, &head);
}

void ChartUI::drawSymbols(lv_layer_t* layer) const {
    this->drawNorthArrow(layer);
    if (!_drawn.visible) return;
    const Symbols& s = _drawn;
    const lv_color_t color = lv_color_hex(_night ? SYMBOL_NIGHT_RGB : SYMBOL_DAY_RGB);

    lv_draw_arc_dsc_t ring;
    lv_draw_arc_dsc_init(&ring);
    ring.color = color;
    ring.width = LINE_W_PX;
    ring.center.x = s.x;
    ring.center.y = s.y;
    ring.radius = (uint16_t)_ring_px;
    ring.start_angle = 0;
    ring.end_angle = 360;
    lv_draw_arc(layer, &ring);

    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.color = color;
    line.width = LINE_W_PX;

    if (s.hdg_valid) {
        const ChartGeometry::ScreenPoint end = ChartGeometry::lineEnd((float)s.x, (float)s.y, s.hdg_deg, (float)_ring_px);
        line.p1.x = s.x;
        line.p1.y = s.y;
        line.p2.x = (lv_value_precise_t)end.x;
        line.p2.y = (lv_value_precise_t)end.y;
        lv_draw_line(layer, &line);
    }

    // Segment by segment: LVGL's own dash pattern works only on horizontal and vertical lines
    if (s.cog_valid) {
        for (int32_t from = 0; from < s.cog_len; from += DASH_PX + DASH_GAP_PX) {
            const int32_t to = min(from + DASH_PX, s.cog_len);
            const ChartGeometry::ScreenPoint a = ChartGeometry::lineEnd((float)s.x, (float)s.y, s.cog_deg, (float)from);
            const ChartGeometry::ScreenPoint b = ChartGeometry::lineEnd((float)s.x, (float)s.y, s.cog_deg, (float)to);
            line.p1.x = (lv_value_precise_t)a.x;
            line.p1.y = (lv_value_precise_t)a.y;
            line.p2.x = (lv_value_precise_t)b.x;
            line.p2.y = (lv_value_precise_t)b.y;
            lv_draw_line(layer, &line);
        }
    }

    lv_draw_rect_dsc_t boat;
    lv_draw_rect_dsc_init(&boat);
    boat.bg_color = color;
    boat.bg_opa = LV_OPA_COVER;
    boat.radius = LV_RADIUS_CIRCLE;
    lv_area_t boat_area;
    boat_area.x1 = s.x - BOAT_R_PX;
    boat_area.y1 = s.y - BOAT_R_PX;
    boat_area.x2 = s.x + BOAT_R_PX;
    boat_area.y2 = s.y + BOAT_R_PX;
    lv_draw_rect(layer, &boat, &boat_area);
}

void ChartUI::drawEvent(lv_event_t* e) {
    const ChartUI* self = static_cast<const ChartUI*>(lv_event_get_user_data(e));
    if (self != nullptr) self->drawSymbols(lv_event_get_layer(e));
}
