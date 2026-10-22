// Host unit tests: gesture classification, the low-charge ladder and display timeout,
// the night palette and the demo value generator.
// Minimal self-contained harness (no framework dependency).

#include <cstdio>
#include "GestureClassifier.h"
#include "BatteryLevel.h"
#include "NightPalette.h"
#include "DemoValues.h"

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
    // Two hours in 7 s steps: every value stays plausible, finite and never NaN, so no
    // page ever drops to its placeholder in demo mode.
    for (uint32_t ms = 0; ms < 2u * 3600u * 1000u; ms += 7000u) {
        const DemoSnapshot d = DemoValues::at(ms);
        ok = ok && inRange(d.heading_true_deg, 0, 360) && inRange(d.cog_true_deg, 0, 360)
                && inRange(d.sog_kn, 5, 7) && inRange(d.pitch_deg, -5, 5)
                && inRange(d.roll_deg, -10, 10)
                && inRange(d.lat_deg, 59.9f, 60.2f) && inRange(d.lon_deg, 24.4f, 25.0f)
                && inRange(d.depth_below_surface_m, 7, 13)
                && inRange(d.house_v, 12, 14) && inRange(d.house_a, -5, 0)
                && inRange(d.house_soc_pct, 0, 100) && inRange(d.start_v, 12, 14)
                && inRange(d.temperature_c, 10, 30) && inRange(d.pressure_hpa, 1000, 1030)
                && inRange(d.humidity_pct, 0, 100) && inRange(d.exhaust_k, 300, 400)
                && inRange(d.fuel_ratio, 0, 1) && inRange(d.water_ratio, 0, 1);
    }
    CHECK_EQ(ok, true);
    // The track loops: the position is back at the start after TRACK_LOOP_S
    const DemoSnapshot a = DemoValues::at(0);
    const DemoSnapshot b = DemoValues::at((uint32_t)(DemoValues::TRACK_LOOP_S * 1000));
    CHECK_EQ(a.lat_deg == b.lat_deg && a.lon_deg == b.lon_deg, true);
}

int main() {
    testTapAndLongPress();
    testFourSwipeDirections();
    testDominantAxis();
    testSwipeThreshold();
    testBatteryLadder();
    testDisplayTimeout();
    testNightPalette();
    testDemoValues();
    std::printf("%d checks, %d failed\n", g_run, g_failed);
    return g_failed == 0 ? 0 : 1;
}
