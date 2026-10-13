// Host unit tests: gesture classification and the low-charge ladder.
// Minimal self-contained harness (no framework dependency).

#include <cstdio>
#include "GestureClassifier.h"
#include "BatteryLevel.h"

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

int main() {
    testTapAndLongPress();
    testFourSwipeDirections();
    testDominantAxis();
    testSwipeThreshold();
    testBatteryLadder();
    std::printf("%d checks, %d failed\n", g_run, g_failed);
    return g_failed == 0 ? 0 : 1;
}
