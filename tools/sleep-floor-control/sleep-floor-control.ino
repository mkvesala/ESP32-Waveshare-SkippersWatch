// sleep-floor-control — a measurement tool, not part of the watch.
//
// Runs chipguyhere's own sleep path for the Waveshare ESP32-S3-Touch-AMOLED-2.06
// (board + power + display libraries, unmodified) on our board, and shows the AXP2101
// gauge on each PWR wake so the deep-sleep floor can be estimated with the SAME method
// the watch's Status page uses: Avg drain = Δ% × 400 mAh / 100 / hours.
//
// Purpose: separate "our firmware carries extra load" from "our board / our gauge
// method reads higher". chipguyhere measured 2.2–2.4 mA with a PPK2 on the cell.
//
// Libraries (read-only clones, NOT installed into ~/Documents/Arduino/libraries):
//   github.com/chipguyhere/ESP32-S3-Touch-AMOLED-2.06_board
//   github.com/chipguyhere/ESP32-S3-Touch-AMOLED-2.06_power
//   github.com/chipguyhere/ESP32-S3-Touch-AMOLED-2.06_display
// See README.md in this folder for the build command and the test procedure.
//
// Screen on each wake (10 s), one value per line:
//   gauge %     | VBAT mV | hours since baseline | Avg drain mA | wake count
// "USB" on the first line means VBUS is present: the baseline is cleared and the
// drain lines show "-". The baseline is taken on the first battery boot after that.

#include <AMOLED206_Board.h>
#include <chipguy_AMOLED206_power.h>
#include <chipguy_AMOLED206_display.h>
#include <sys/time.h>

static AMOLED206_Board board;
// `power` is the library's own inline global (chipguy_AMOLED206_power.h)
static chipguy_AMOLED206_display display(board);

static constexpr float    BATTERY_CAPACITY_MAH = 400.0f;   // same nominal as the watch
static constexpr uint32_t SHOW_MS              = 10000;
static constexpr float    MIN_HOURS_FOR_DRAIN  = 0.5f;

// Survive deep sleep; cleared by a power-on reset (PMIC power cycle).
RTC_DATA_ATTR static int      s_base_pct = -1;
RTC_DATA_ATTR static int64_t  s_base_s   = 0;
RTC_DATA_ATTR static uint32_t s_wakes    = 0;

// ── Tiny 5x7 font, scaled ───────────────────────────────────────────────────

struct Glyph { char c; uint8_t rows[7]; };
static const Glyph FONT[] = {
    {'0', {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}}, {'1', {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}},
    {'2', {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}}, {'3', {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E}},
    {'4', {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}}, {'5', {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}},
    {'6', {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}}, {'7', {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}},
    {'8', {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}}, {'9', {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}},
    {'.', {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C}}, {'-', {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}},
    {'%', {0x18,0x19,0x02,0x04,0x08,0x13,0x03}}, {'V', {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}},
    {'H', {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}}, {'M', {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}},
    {'A', {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}}, {'N', {0x11,0x11,0x19,0x15,0x13,0x11,0x11}},
    {'U', {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}}, {'S', {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}},
    {'B', {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}},
};

static constexpr int SCALE   = 6;
static constexpr int PITCH   = 6 * SCALE;              // 5 px glyph + 1 px gap
static constexpr int LINE_H  = 48;                     // even; 410 x 48 fits one DMA buffer
static constexpr int TOP     = 60;                     // even
static constexpr int LEFT    = 40;
static constexpr int NATIVE_W = 410;
static uint16_t s_line[NATIVE_W * LINE_H];

static const uint8_t* glyphRows(char c) {
    for (const Glyph& g : FONT) if (g.c == c) return g.rows;
    return nullptr;   // unknown characters render as a space
}

// Full-width, even-aligned strip: avoids the CO5300 partial-width line.
static void drawLine(int index, const char* text) {
    memset(s_line, 0, sizeof(s_line));
    int x0 = LEFT;
    for (const char* p = text; *p && x0 + 5 * SCALE <= NATIVE_W; ++p, x0 += PITCH) {
        const uint8_t* rows = glyphRows(*p);
        if (rows == nullptr) continue;
        for (int r = 0; r < 7; ++r)
            for (int c = 0; c < 5; ++c)
                if (rows[r] & (0x10 >> c))
                    for (int dy = 0; dy < SCALE; ++dy)
                        for (int dx = 0; dx < SCALE; ++dx)
                            s_line[(r * SCALE + dy) * NATIVE_W + x0 + c * SCALE + dx] = 0xFFFF;
    }
    const int y1 = TOP + index * (LINE_H + 8);
    display.flushRegion(0, y1, NATIVE_W - 1, y1 + LINE_H - 1, s_line);
    display.waitFlush();
}

static int64_t nowSeconds() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);   // RTC timer: keeps counting through deep sleep
    return (int64_t)tv.tv_sec;
}

static void dumpPmic() {
    Serial.println("[control] AXP2101 0x10..0x9F");
    for (int base = 0x10; base <= 0x90; base += 0x10) {
        Serial.printf("[control] %02X:", base);
        for (int r = base; r < base + 0x10; ++r) {
            uint8_t v = 0;
            if (power.readReg((uint8_t)r, v)) Serial.printf(" %02X", v);
            else                              Serial.print(" --");
        }
        Serial.println();
    }
}

void setup() {
    Serial.begin(115200);
    Serial.setTxTimeoutMs(0);
    board.begin();
    power.begin(board);
    display.begin();
    display.setBrightness(50);
    ++s_wakes;

    const bool usb = power.usbPowerPresent();
    const int  pct = power.getBatteryPercent();
    const int  mv  = (int)(power.getBatteryVoltage() * 1000.0f + 0.5f);
    const int64_t now = nowSeconds();

    if (usb) {
        s_base_pct = -1;
    } else if (pct >= 0 && (s_base_pct < 0 || pct > s_base_pct)) {
        // Same rule as SystemStats: a gauge reading above the baseline means the gauge
        // re-estimated upwards after an unplug, so the baseline restarts there.
        s_base_pct = pct;
        s_base_s   = now;
    }

    char l0[16], l1[16], l2[16], l3[16], l4[16];
    if (usb) snprintf(l0, sizeof(l0), "USB %d%%", pct);
    else     snprintf(l0, sizeof(l0), "%d%%", pct);
    snprintf(l1, sizeof(l1), "%d MV", mv);

    float hours = -1.0f;
    if (!usb && s_base_pct >= 0) hours = (float)(now - s_base_s) / 3600.0f;
    if (hours >= 0.0f) snprintf(l2, sizeof(l2), "%.1f H", hours);
    else               snprintf(l2, sizeof(l2), "- H");
    if (hours >= MIN_HOURS_FOR_DRAIN) {
        const float ma = (float)(s_base_pct - pct) * BATTERY_CAPACITY_MAH / 100.0f / hours;
        snprintf(l3, sizeof(l3), "%.2f MA", ma);
    } else {
        snprintf(l3, sizeof(l3), "- MA");
    }
    snprintf(l4, sizeof(l4), "N %lu", (unsigned long)s_wakes);

    drawLine(0, l0);
    drawLine(1, l1);
    drawLine(2, l2);
    drawLine(3, l3);
    drawLine(4, l4);
    Serial.printf("[control] %s | %s | %s | %s | %s | base %d%%\n", l0, l1, l2, l3, l4, s_base_pct);

    delay(SHOW_MS);
    if (usb) dumpPmic();

    // A held PWR would re-fire the level-triggered ext1 wake at once.
    while (board.pwrButtonPressed() || board.bootButtonPressed()) delay(10);

    display.sleep();          // DISPOFF, SLPIN, 120 ms, deep standby
    power.prepareForSleep();  // unused rails off, IMU off, LCD/TP reset held high
    power.deepSleep(0);       // ext0 BOOT + ext1 PWR
}

void loop() {}
