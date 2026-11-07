// Host unit tests: gesture classification, the low-charge ladder and display timeout,
// the night palette and the demo value generator.
// Minimal self-contained harness (no framework dependency).

#include <cstdio>
#include "GestureClassifier.h"
#include "BatteryLevel.h"
#include "NightPalette.h"
#include "DemoValues.h"
#include "StatusFormat.h"
#include "ChartGeometry.h"
#include "ChartFormat.h"
#include <cstring>
#include <cmath>

static int g_failed = 0;
static int g_run = 0;

#define CHECK_EQ(actual, expected)                                                   \
    do {                                                                             \
        ++g_run;                                                                     \
        if ((actual) != (expected)) {                                                \
            ++g_failed;                                                              \
            std::printf("FAIL %s:%d  %s != %s\n", __FILE__, __LINE__, #actual, #expected); \
        }                                                                            \
    } while (0)

using G = GestureClassifier;

// Peak per-axis travel the way TouchInput tracks it for a straight-line touch
static int16_t peak(int16_t dx, int16_t dy) {
    const int16_t ax = dx < 0 ? -dx : dx;
    const int16_t ay = dy < 0 ? -dy : dy;
    return ax > ay ? ax : ay;
}

static TouchGesture swipe(int16_t dx, int16_t dy, uint32_t held = 200) {
    const int16_t x0 = 200, y0 = 250;
    return G::classify(x0, y0, x0 + dx, y0 + dy, peak(dx, dy), held);
}

static void testTapAndLongPress() {
    CHECK_EQ(G::classify(100, 100, 100, 100, 0, 80), TouchGesture::TAP);
    CHECK_EQ(G::classify(100, 100, 110, 105, 10, 150), TouchGesture::TAP);
    // Jitter right at the tap limit is still a tap
    CHECK_EQ(G::classify(100, 100, 127, 100, G::TAP_MAX_MOVE_PX, 150), TouchGesture::TAP);
    // Held still long enough → long press (unbound, but must not become a tap)
    CHECK_EQ(G::classify(100, 100, 100, 100, 0, G::LONG_PRESS_MS), TouchGesture::LONG_PRESS);
    CHECK_EQ(G::classify(100, 100, 100, 100, 0, G::LONG_PRESS_MS - 1), TouchGesture::TAP);
}

static void testFourSwipeDirections() {
    CHECK_EQ(swipe(-120, 0), TouchGesture::SWIPE_LEFT);    // next page
    CHECK_EQ(swipe(120, 0), TouchGesture::SWIPE_RIGHT);    // previous page
    CHECK_EQ(swipe(0, 150), TouchGesture::SWIPE_DOWN);     // open menu (y grows down)
    CHECK_EQ(swipe(0, -150), TouchGesture::SWIPE_UP);      // close menu
}

static void testDominantAxis() {
    CHECK_EQ(swipe(-100, 40), TouchGesture::SWIPE_LEFT);
    CHECK_EQ(swipe(40, -100), TouchGesture::SWIPE_UP);
    // Exact diagonal: vertical wins (menu pull-down over a page step)
    CHECK_EQ(swipe(80, 80), TouchGesture::SWIPE_DOWN);
}

static void testSwipeThreshold() {
    CHECK_EQ(swipe(-G::SWIPE_MIN_MOVE_PX, 0), TouchGesture::SWIPE_LEFT);
    CHECK_EQ(swipe(-(G::SWIPE_MIN_MOVE_PX - 1), 0), TouchGesture::NONE);
    CHECK_EQ(swipe(0, G::SWIPE_MIN_MOVE_PX), TouchGesture::SWIPE_DOWN);
    CHECK_EQ(swipe(0, G::SWIPE_MIN_MOVE_PX - 1), TouchGesture::NONE);
    // Moved far but came back: big peak, small net travel → no gesture
    CHECK_EQ(G::classify(200, 250, 205, 250, 120, 400), TouchGesture::NONE);
    // A long, slow swipe is still a swipe (travel decides, not duration)
    CHECK_EQ(swipe(-150, 0, 2000), TouchGesture::SWIPE_LEFT);
}

static void testBatteryLadder() {
    using L = BatteryLevel;
    CHECK_EQ(BatteryLadder::classify(80, 3950), L::OK);
    CHECK_EQ(BatteryLadder::classify(21, 3760), L::OK);
    CHECK_EQ(BatteryLadder::classify(20, 3740), L::WARN);
    CHECK_EQ(BatteryLadder::classify(11, 3700), L::WARN);
    CHECK_EQ(BatteryLadder::classify(10, 3680), L::CRITICAL);
    CHECK_EQ(BatteryLadder::classify(6, 3500), L::CRITICAL);
    CHECK_EQ(BatteryLadder::classify(5, 3500), L::CUTOFF);
    CHECK_EQ(BatteryLadder::classify(0, 3300), L::CUTOFF);
    // Voltage floor trips CUTOFF even if the gauge still claims charge
    CHECK_EQ(BatteryLadder::classify(60, BatteryLadder::CUTOFF_CELL_MV), L::CUTOFF);
    CHECK_EQ(BatteryLadder::classify(60, BatteryLadder::CUTOFF_CELL_MV + 1), L::OK);
    // Ordering is what PowerManager compares against (preventSleepActive: < CRITICAL)
    CHECK_EQ(L::OK < L::WARN && L::WARN < L::CRITICAL && L::CRITICAL < L::CUTOFF, true);
}

static void testDisplayTimeout() {
    using L = BatteryLevel;
    // Healthy cell: the user's wake time as is, across the whole slider range
    CHECK_EQ(BatteryLadder::displayTimeoutMs(L::OK, 2000), 2000u);
    CHECK_EQ(BatteryLadder::displayTimeoutMs(L::OK, 10000), 10000u);
    CHECK_EQ(BatteryLadder::displayTimeoutMs(L::OK, 20000), 20000u);
    // WARN caps at 5 s, but never lengthens a shorter user choice
    CHECK_EQ(BatteryLadder::displayTimeoutMs(L::WARN, 20000), 5000u);
    CHECK_EQ(BatteryLadder::displayTimeoutMs(L::WARN, 2000), 2000u);
    // CRITICAL (and CUTOFF) cap at 3 s
    CHECK_EQ(BatteryLadder::displayTimeoutMs(L::CRITICAL, 10000), 3000u);
    CHECK_EQ(BatteryLadder::displayTimeoutMs(L::CRITICAL, 2000), 2000u);
    CHECK_EQ(BatteryLadder::displayTimeoutMs(L::CUTOFF, 20000), 3000u);
}

static void testNightPalette() {
    using P = NightPalette;
    CHECK_EQ(P::map(0x000000), 0u);
    CHECK_EQ(P::map(0x15171A), 0u);              // LVGL dark theme screen → true black
    CHECK_EQ(P::map(0xFFFFFF), 0xFF0000u);       // theme text → full red
    // Red only: no green or blue ever survives the mapping
    const uint32_t samples[] = { 0x666666, 0xCCCCCC, 0xFF9800, 0x4CAF50, 0x2196F3, 0x444444 };
    for (uint32_t c : samples) CHECK_EQ(P::map(c) & 0x00FFFF, 0u);
    // Brightness order is kept (slider: track < rest knob)
    CHECK_EQ(P::map(0x666666) < P::map(0xCCCCCC), true);
    // Dim but visible colors are lifted, not lost
    CHECK_EQ(P::map(0x303030) >= ((uint32_t)P::NIGHT_MIN_RED << 16), true);
    CHECK_EQ(P::pick(0x4CAF50, false), 0x4CAF50u);
}

static bool inRange(float v, float lo, float hi) { return v == v && v >= lo && v <= hi; }

static void testDemoValues() {
    bool ok = true;
    // Every row: each value stays plausible, finite and never NaN, so no page ever drops
    // to its placeholder in demo mode.
    for (uint32_t ms = 0; ms < DemoTrack::ROWS * 1000u; ms += 1000u) {
        const DemoSnapshot d = DemoValues::at(ms);
        ok = ok && inRange(d.heading_true_deg, 0, 360) && inRange(d.cog_true_deg, 0, 360)
                && inRange(d.sog_kn, 6, 8) && inRange(d.pitch_deg, -10, 10)
                && inRange(d.roll_deg, -15, 15)
                && inRange(d.lat_deg, 60.25f, 60.30f) && inRange(d.lon_deg, 25.95f, 26.10f)
                && inRange(d.depth_below_surface_m, 2, 30)
                && inRange(d.house_v, 24, 30) && inRange(d.house_a, -10, 10)
                && inRange(d.house_soc_pct, 0, 100) && inRange(d.start_v, 24, 30)
                && inRange(d.temperature_c, 0, 30) && inRange(d.pressure_hpa, 980, 1050)
                && inRange(d.humidity_pct, 0, 100) && inRange(d.exhaust_k, 300, 400)
                && inRange(d.fuel_ratio, 0, 1) && inRange(d.water_ratio, 0, 1);
    }
    CHECK_EQ(ok, true);
    // START_LAT/LON_DEG name the track's first row
    const DemoSnapshot first = DemoValues::at(0);
    CHECK_EQ(fabsf(first.lat_deg - DemoValues::START_LAT_DEG) < 0.001f, true);
    CHECK_EQ(fabsf(first.lon_deg - DemoValues::START_LON_DEG) < 0.001f, true);
    // The track loops: the position is back at the start after TRACK_LOOP_S
    const DemoSnapshot a = DemoValues::at(0);
    const DemoSnapshot b = DemoValues::at((uint32_t)(DemoValues::TRACK_LOOP_S * 1000));
    CHECK_EQ(a.lat_deg == b.lat_deg && a.lon_deg == b.lon_deg, true);
}

static bool fmtDuration(uint64_t s, const char* expected) {
    char buf[24];
    StatusFormat::duration(s, buf, sizeof(buf));
    return std::strcmp(buf, expected) == 0;
}

static bool fmtKb(uint32_t bytes, const char* expected) {
    char buf[16];
    StatusFormat::kb(bytes, buf, sizeof(buf));
    return std::strcmp(buf, expected) == 0;
}

static bool fmtDrain(bool valid, uint8_t base, uint8_t now, uint64_t s, const char* expected) {
    char buf[24];
    StatusFormat::drain(valid, base, now, s, 400, buf, sizeof(buf));
    return std::strcmp(buf, expected) == 0;
}

static void testStatusFormat() {
    using F = StatusFormat;
    CHECK_EQ(fmtDuration(0, "00h 00m 00s"), true);
    CHECK_EQ(fmtDuration(59, "00h 00m 59s"), true);
    CHECK_EQ(fmtDuration(3600, "01h 00m 00s"), true);
    CHECK_EQ(fmtDuration(24 * 3600, "24h 00m 00s"), true);
    CHECK_EQ(fmtDuration(54 * 3600 + 2 * 60 + 30, "54h 02m 30s"), true);
    CHECK_EQ(fmtDuration(123ull * 3600 + 5, "123h 00m 05s"), true);   // past 99 h

    CHECK_EQ(fmtKb(196 * 1024, "196 KB"), true);
    CHECK_EQ(fmtKb(5325, "5.2 KB"), true);
    CHECK_EQ(fmtKb(0, "0.0 KB"), true);

    CHECK_EQ(std::strcmp(F::resetReason(F::RST_POWERON), "POWERON"), 0);
    CHECK_EQ(std::strcmp(F::resetReason(F::RST_DEEPSLEEP), "DEEPSLEEP"), 0);
    CHECK_EQ(std::strcmp(F::resetReason(F::RST_TASK_WDT), "WDT"), 0);
    CHECK_EQ(std::strcmp(F::resetReason(99), "OTHER"), 0);

    CHECK_EQ(F::isUnexpected(F::RST_PANIC), true);
    CHECK_EQ(F::isUnexpected(F::RST_BROWNOUT), true);
    CHECK_EQ(F::isUnexpected(F::RST_DEEPSLEEP), false);
    CHECK_EQ(F::isUnexpected(F::RST_SW), false);

    // Counters start over on a true power-on or with untrusted RTC memory, nowhere else
    CHECK_EQ(F::shouldReset(true, F::RST_POWERON), true);
    CHECK_EQ(F::shouldReset(false, F::RST_DEEPSLEEP), true);
    CHECK_EQ(F::shouldReset(true, F::RST_DEEPSLEEP), false);
    CHECK_EQ(F::shouldReset(true, F::RST_TASK_WDT), false);

    // Drain: the field test (100 -> 54 % of 400 mAh in 25 h) reads as 7.4 mA
    CHECK_EQ(fmtDrain(true, 100, 54, 25ull * 3600, "7.4 mA/25h"), true);
    CHECK_EQ(fmtDrain(true, 100, 92, 16ull * 3600, "2.0 mA/16h"), true);   // the target
    CHECK_EQ(fmtDrain(true, 80, 80, 10ull * 3600, "0.0 mA/10h"), true);
    CHECK_EQ(fmtDrain(true, 80, 81, 10ull * 3600, "0.0 mA/10h"), true);    // gauge crept up
    CHECK_EQ(fmtDrain(true, 80, 70, 3599, "-- mA/<1h"), true);
    CHECK_EQ(fmtDrain(false, 80, 70, 99999, "-- (USB)"), true);
}

#define CHECK_NEAR(actual, expected, tol)                                            \
    do {                                                                             \
        ++g_run;                                                                     \
        if (std::fabs((double)(actual) - (double)(expected)) > (tol)) {              \
            ++g_failed;                                                              \
            std::printf("FAIL %s:%d  %s = %.6f, expected %.6f\n", __FILE__, __LINE__, \
                        #actual, (double)(actual), (double)(expected));              \
        }                                                                            \
    } while (0)

// Reference values: pyproj 3.7.2, Proj("EPSG:3067") forward and get_factors()
static void testChartProjection() {
    struct Ref { double lat, lon, e, n, conv; };
    const Ref refs[] = {
        { 60.0833, 24.8333, 379464.874, 6662663.930, -1.87821 },   // the demo position
        { 59.82,   22.95,   272967.956, 6638305.962, -3.50250 },   // off Hanko
        { 60.5,    27.0,    500000.000, 6707097.168,  0.00000 },   // on the central meridian
        { 60.45,   27.9,    549515.561, 6701866.700,  0.78295 },   // east of it
        { 60.15,   19.9,    106283.130, 6689315.183, -6.16589 },   // Åland, far from it
        { 65.0,    25.0,    405698.988, 7209946.446, -1.81275 },
    };
    for (const Ref& r : refs) {
        const ChartGeometry::GridPoint p = ChartGeometry::wgs84ToTm35(r.lat, r.lon);
        CHECK_NEAR(p.e, r.e, 0.01);
        CHECK_NEAR(p.n, r.n, 0.01);
        CHECK_NEAR(ChartGeometry::convergenceDeg(r.lat, r.lon), r.conv, 0.005);
    }

    // West of 27°E true north points right of grid north, and the result wraps to 0..360
    CHECK_NEAR(ChartGeometry::gridBearingDeg(0.0, -3.5), 3.5, 1e-9);
    CHECK_NEAR(ChartGeometry::gridBearingDeg(359.0, -3.5), 2.5, 1e-9);
    CHECK_NEAR(ChartGeometry::gridBearingDeg(1.0, 3.5), 357.5, 1e-9);
}

static void testChartSymbols() {
    // 0.25 NM at 4 m/px, and 3 min at 5 kn is the same distance
    CHECK_NEAR(ChartGeometry::ringRadiusPx(4.0), 115.75, 0.01);
    CHECK_NEAR(ChartGeometry::cogVectorPx(5.0, 4.0), 115.75, 0.01);
    CHECK_NEAR(ChartGeometry::cogVectorPx(1.0, 4.0), 23.15, 0.01);

    ChartGeometry::ScreenPoint p = ChartGeometry::lineEnd(100.0f, 100.0f, 0.0, 50.0f);   // up
    CHECK_NEAR(p.x, 100.0, 0.001);
    CHECK_NEAR(p.y, 50.0, 0.001);
    p = ChartGeometry::lineEnd(100.0f, 100.0f, 90.0, 50.0f);                             // right
    CHECK_NEAR(p.x, 150.0, 0.001);
    CHECK_NEAR(p.y, 100.0, 0.001);
    p = ChartGeometry::lineEnd(100.0f, 100.0f, 180.0, 50.0f);                            // down
    CHECK_NEAR(p.x, 100.0, 0.001);
    CHECK_NEAR(p.y, 150.0, 0.001);
}

static void testChartAddressing() {
    using C = ChartFormat;
    CHECK_EQ(C::TILE_BYTES, 65536u);
    CHECK_EQ(C::BLOCK_BYTES, 16777216u);

    CHECK_EQ(C::floorDiv(511, 256), 1);
    CHECK_EQ(C::floorDiv(512, 256), 2);
    CHECK_EQ(C::floorDiv(-1, 256), -1);
    CHECK_EQ(C::floorDiv(-256, 256), -1);
    CHECK_EQ(C::floorDiv(-257, 256), -2);

    char path[C::BLOCK_PATH_LEN];
    CHECK_EQ(C::blockPath(1000, 1650, path, sizeof(path)), true);
    CHECK_EQ(std::strcmp(path, "/chart/blocks/c0062r0103.bin"), 0);
    CHECK_EQ(C::blockPath(-1, 1650, path, sizeof(path)), false);
    CHECK_EQ(C::blockPath(1000, -1, path, sizeof(path)), false);

    CHECK_EQ(C::blockOffset(32, 48, 0), 0u);                     // first tile of its block
    CHECK_EQ(C::blockOffset(33, 48, 0), 65536u);                 // next column
    CHECK_EQ(C::blockOffset(32, 49, 0), 16u * 65536u);           // next row
    CHECK_EQ(C::blockOffset(47, 63, 255), C::BLOCK_BYTES - 256u);   // last row of the last tile

    // The demo position through the whole chain: tile 11 of the TM35FIN grid at 4 m/px
    C::Meta m = {};
    m.m_per_px = 4.0;
    m.origin_e = -548576.0;
    m.origin_n = 8388608.0;
    const ChartGeometry::GridPoint g = ChartGeometry::wgs84ToTm35(60.0833, 24.8333);
    double px, py;
    C::toPixel(m, g.e, g.n, px, py);
    CHECK_NEAR(px, (379464.874 + 548576.0) / 4.0, 0.01);
    CHECK_NEAR(py, (8388608.0 - 6662663.930) / 4.0, 0.01);
}

// A view must be covered exactly once, whichever way it falls on the tiles
static void checkViewCover(int32_t x0, int32_t y0, uint16_t w, uint16_t h, int expected_spans) {
    using C = ChartFormat;
    static uint8_t hits[1024 * 1024];
    std::memset(hits, 0, sizeof(hits));
    int spans = 0;
    bool in_tile = true;
    const C::View v = { x0, y0, w, h };
    C::forEachSpan(v, [&](const C::Span& s) {
        ++spans;
        if (s.tile_x + s.w > C::TILE_PX || s.tile_y + s.h > C::TILE_PX) in_tile = false;
        // The span's chart pixel, reached from both ends, must be the same one
        if (s.col * C::TILE_PX + s.tile_x != x0 + s.view_x) in_tile = false;
        if (s.row * C::TILE_PX + s.tile_y != y0 + s.view_y) in_tile = false;
        for (uint16_t y = 0; y < s.h; ++y) {
            for (uint16_t x = 0; x < s.w; ++x) hits[(s.view_y + y) * 1024u + s.view_x + x]++;
        }
    });
    int wrong = 0;
    for (uint16_t y = 0; y < h; ++y) {
        for (uint16_t x = 0; x < w; ++x) {
            if (hits[y * 1024u + x] != 1) ++wrong;
        }
    }
    CHECK_EQ(spans, expected_spans);
    CHECK_EQ(in_tile, true);
    CHECK_EQ(wrong, 0);
}

static void testChartSpans() {
    checkViewCover(0, 0, 256, 256, 1);           // exactly one tile
    checkViewCover(255, 255, 2, 2, 4);           // a tile corner
    checkViewCover(1024, 1024, 410, 502, 4);     // the watch view at its best: 2 x 2 tiles
    checkViewCover(1000, 1000, 410, 502, 9);     // the watch view, usual case: 3 x 3 tiles
    checkViewCover(-100, -100, 410, 502, 9);     // off the grid's corner
    checkViewCover(10, 10, 650, 650, 9);         // a future bow-up source square
    checkViewCover(250, 250, 650, 650, 16);      // ...which 3 x 3 tiles cannot always cover

    // The view is centered on the boat's pixel
    const ChartFormat::View v = ChartFormat::centeredView(1000.7, 2000.2, 410, 502);
    CHECK_EQ(v.x0, 1000 - 205);
    CHECK_EQ(v.y0, 2000 - 251);
}

// Index = tile_y * 16 + tile_x in a small test tile area, so each pixel names where it came from
static void testChartExpand() {
    using C = ChartFormat;
    static uint8_t tile[C::TILE_BYTES];
    static uint16_t palette[C::PALETTE_COUNT];
    for (size_t i = 0; i < C::PALETTE_COUNT; ++i) palette[i] = (uint16_t)(0x1000 + i);
    for (int y = 0; y < C::TILE_PX; ++y) {
        for (int x = 0; x < C::TILE_PX; ++x) tile[y * C::TILE_PX + x] = (uint8_t)((y % 16) * 16 + (x % 16));
    }

    uint16_t view[6 * 10];
    std::memset(view, 0xAB, sizeof(view));
    const C::Span s = { 0, 0, /*tile_x*/ 3, /*tile_y*/ 2, /*view_x*/ 1, /*view_y*/ 4, /*w*/ 2, /*h*/ 2 };
    // src starts at the span's first row, as the store reads it
    C::expandSpan(s, tile + 2 * C::TILE_PX, palette, view, 10);
    CHECK_EQ(view[4 * 10 + 1], 0x1000 + 2 * 16 + 3);
    CHECK_EQ(view[4 * 10 + 2], 0x1000 + 2 * 16 + 4);
    CHECK_EQ(view[5 * 10 + 1], 0x1000 + 3 * 16 + 3);
    CHECK_EQ(view[5 * 10 + 2], 0x1000 + 3 * 16 + 4);
    CHECK_EQ(view[4 * 10 + 0], 0xABAB);   // nothing outside the span is touched
    CHECK_EQ(view[4 * 10 + 3], 0xABAB);
    CHECK_EQ(view[3 * 10 + 1], 0xABAB);

    C::blankSpan(s, view, 10);
    CHECK_EQ(view[4 * 10 + 1], 0);
    CHECK_EQ(view[5 * 10 + 2], 0);
    CHECK_EQ(view[4 * 10 + 3], 0xABAB);
}

static void testBowUpGeometry() {
    using CG = ChartGeometry;
    CHECK_NEAR(CG::headingDelta(10.0, 350.0), 20.0, 1e-9);
    CHECK_NEAR(CG::headingDelta(350.0, 10.0), -20.0, 1e-9);
    CHECK_NEAR(CG::headingDelta(180.0, 0.0), 180.0, 1e-9);
    CHECK_NEAR(CG::headingDelta(0.0, 180.0), 180.0, 1e-9);
    CHECK_NEAR(CG::headingDelta(725.0, 5.0), 0.0, 1e-9);

    CHECK_NEAR(CG::screenBearing(0.0, 90.0), 270.0, 1e-9);   // north is left when heading east
    CHECK_NEAR(CG::screenBearing(90.0, 90.0), 0.0, 1e-9);    // the heading is up
    CHECK_NEAR(CG::screenBearing(45.0, 0.0), 45.0, 1e-9);    // north up: unchanged

    // Heading east: a point east of the boat is above it on the screen
    CG::ScreenPoint p = CG::rotateOffset(10.0, 0.0, 90.0);
    CHECK_NEAR(p.x, 0.0, 1e-5);
    CHECK_NEAR(p.y, -10.0, 1e-5);
    p = CG::rotateOffset(3.0, -4.0, 0.0);                     // north up: unchanged
    CHECK_NEAR(p.x, 3.0, 1e-6);
    CHECK_NEAR(p.y, -4.0, 1e-6);

    // An offset and a line at the same bearing turn the same way
    const CG::ScreenPoint grid = CG::lineEnd(0.0f, 0.0f, 70.0, 100.0f);
    p = CG::rotateOffset(grid.x, grid.y, 25.0);
    const CG::ScreenPoint line = CG::lineEnd(0.0f, 0.0f, CG::screenBearing(70.0, 25.0), 100.0f);
    CHECK_NEAR(p.x, line.x, 1e-3);
    CHECK_NEAR(p.y, line.y, 1e-3);
}

// Source index = y * 8 + x, palette value = index + 1, so each view pixel names its source
static void testBowUpRotate() {
    using C = ChartFormat;
    static uint16_t palette[C::PALETTE_COUNT];
    for (size_t i = 0; i < C::PALETTE_COUNT; ++i) palette[i] = (uint16_t)(i + 1);
    palette[C::NO_DATA_INDEX] = 0;
    uint8_t src[8 * 8];
    for (int i = 0; i < 64; ++i) src[i] = (uint8_t)i;
    uint16_t out[12 * 12];

    const auto at = [&](int sx, int sy) { return (uint16_t)(sy * 8 + sx + 1); };
    bool ok = true;

    CHECK_EQ(C::rotateIndexed(src, 8, 8, 4.0, 4.0, 0.0, palette, out, 8, 8), true);
    for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; ++x) ok &= out[y * 8 + x] == at(x, y);
    CHECK_EQ(ok, true);

    // Heading east: the east edge of the source is the view's top row
    C::rotateIndexed(src, 8, 8, 4.0, 4.0, 90.0, palette, out, 8, 8);
    ok = true;
    for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; ++x) ok &= out[y * 8 + x] == at(7 - y, x);
    CHECK_EQ(ok, true);

    C::rotateIndexed(src, 8, 8, 4.0, 4.0, 180.0, palette, out, 8, 8);
    ok = true;
    for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; ++x) ok &= out[y * 8 + x] == at(7 - x, 7 - y);
    CHECK_EQ(ok, true);

    // A view larger than the source: black outside it
    C::rotateIndexed(src, 8, 8, 4.0, 4.0, 0.0, palette, out, 12, 12);
    CHECK_EQ(out[0], 0);
    CHECK_EQ(out[2 * 12 + 2], at(0, 0));
    CHECK_EQ(out[11 * 12 + 11], 0);

    // The four pixels around the view's center come from around the source point, whatever
    // the angle
    C::rotateIndexed(src, 8, 8, 4.0, 4.0, 37.0, palette, out, 8, 8);
    ok = true;
    for (int y = 3; y <= 4; ++y) {
        for (int x = 3; x <= 4; ++x) {
            const int v = out[y * 8 + x] - 1;
            ok &= (v % 8 == 3 || v % 8 == 4) && (v / 8 == 3 || v / 8 == 4);
        }
    }
    CHECK_EQ(ok, true);

    // No chart data on view → false
    std::memset(src, C::NO_DATA_INDEX, sizeof(src));
    CHECK_EQ(C::rotateIndexed(src, 8, 8, 4.0, 4.0, 45.0, palette, out, 8, 8), false);
}

static void testBowUpIndexedSpans() {
    using C = ChartFormat;
    static uint8_t tile[C::TILE_BYTES];
    for (int y = 0; y < C::TILE_PX; ++y) for (int x = 0; x < C::TILE_PX; ++x) tile[y * C::TILE_PX + x] = (uint8_t)(y * 16 + x);
    uint8_t out[6 * 10];
    std::memset(out, 0xAB, sizeof(out));
    const C::Span s = { 0, 0, 3, 2, 1, 4, 2, 2 };
    C::copySpanIndexed(s, tile + 2 * C::TILE_PX, out, 10);
    CHECK_EQ(out[4 * 10 + 1], 2 * 16 + 3);
    CHECK_EQ(out[5 * 10 + 2], 3 * 16 + 4);
    CHECK_EQ(out[4 * 10 + 3], 0xAB);
    C::blankSpanIndexed(s, out, 10);
    CHECK_EQ(out[4 * 10 + 1], C::NO_DATA_INDEX);
    CHECK_EQ(out[4 * 10 + 0], 0xAB);
}

static void testDoubleTap() {
    CHECK_EQ(G::isDoubleTap(1000, 200, 250, 1300, 210, 240), true);
    CHECK_EQ(G::isDoubleTap(1000, 200, 250, 1000 + G::DOUBLE_TAP_MS, 200, 250), true);
    CHECK_EQ(G::isDoubleTap(1000, 200, 250, 1001 + G::DOUBLE_TAP_MS, 200, 250), false);   // too slow
    CHECK_EQ(G::isDoubleTap(1000, 200, 250, 1200, 300, 250), false);                      // too far
    CHECK_EQ(G::isDoubleTap(0xFFFFFF00u, 200, 250, 0x40, 200, 250), true);                // millis wrap
}

int main() {
    testChartExpand();
    testChartProjection();
    testChartSymbols();
    testChartAddressing();
    testChartSpans();
    testBowUpGeometry();
    testBowUpRotate();
    testBowUpIndexedSpans();
    testDoubleTap();
    testTapAndLongPress();
    testFourSwipeDirections();
    testDominantAxis();
    testSwipeThreshold();
    testBatteryLadder();
    testDisplayTimeout();
    testNightPalette();
    testDemoValues();
    testStatusFormat();
    std::printf("%d checks, %d failed\n", g_run, g_failed);
    return g_failed == 0 ? 0 : 1;
}
