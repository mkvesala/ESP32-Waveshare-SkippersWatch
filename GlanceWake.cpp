#include "GlanceWake.h"
#include "amoled206_orient_ulp.h"
#include "ulp_common_defs.h"   // RTC_SLOW_MEM
#include "ulp.h"               // I_HALT
#include <string.h>
#include <driver/rtc_io.h>
#include <driver/gpio.h>
#include <esp_sleep.h>

// Set while the ULP owns the bus. RTC_DATA_ATTR: survives deep sleep, so the next boot
// knows it has to take the pins back. Any other reset reloads false, and that is right,
// because arm() runs only immediately before esp_deep_sleep_start().
RTC_DATA_ATTR static bool s_armed = false;
RTC_DATA_ATTR static int16_t s_words = 0;

// Captured by reclaimBus() for log(): the boot-time Serial lines are lost before the
// native USB port enumerates.
static bool s_reclaimed = false;
static uint16_t s_runs = 0;
static int s_x = 0, s_y = 0, s_z = 0;
static bool s_sda_was_low = false;     // bus state found by recoverBus()
static bool s_scl_was_low = false;
static bool s_sda_after_low = false;
static uint8_t s_clocks = 0;

// === P U B L I C ===

void GlanceWake::reclaimBus() {
    if (!s_armed) return;
    amoled206_orient_ulp_stop();   // no new runs
    delay(2);                      // a run in flight finishes (well under 1 ms)

    // Capture the evidence before anything else can change it
    s_reclaimed = true;
    s_runs = (uint16_t)(RTC_SLOW_MEM[AMOLED206_ORIENT_RUNS] & 0xFFFF);
    s_x = mgFromWord(AMOLED206_ORIENT_AX);
    s_y = mgFromWord(AMOLED206_ORIENT_AY);
    s_z = mgFromWord(AMOLED206_ORIENT_AZ);

    // Stopping the timer does not stop a program that is already executing. A run that
    // reached the dwell again while the CPU was booting (the watch is still held in the
    // pose) waits for RTC_CNTL_RDY_FOR_WAKEUP, which never comes while awake, so the ULP
    // spins there indefinitely. The ULP fetches every instruction from RTC slow memory:
    // overwriting the program with HALT ends any such loop on its next fetch. arm()
    // rebuilds and reloads the program before the next sleep.
    const ulp_insn_t halt = I_HALT();
    uint32_t halt_word;
    memcpy(&halt_word, &halt, sizeof(halt_word));
    for (int w = 0; w < AMOLED206_ORIENT_DATA_BASE; w++) RTC_SLOW_MEM[w] = halt_word;
    delayMicroseconds(50);

    rtc_gpio_deinit((gpio_num_t)PIN_SCL);
    rtc_gpio_deinit((gpio_num_t)PIN_SDA);
    s_armed = false;

    recoverBus();
}

bool GlanceWake::arm(TwoWire& wire) {
    amoled206_orient_params_t prm = {};
    const int pose[3] = { POSE_X_MG, POSE_Y_MG, POSE_Z_MG };
    for (int i = 0; i < 3; i++) {
        prm.lo[i] = mgToOffsetBinary(pose[i] - TOLERANCE_MG);
        prm.hi[i] = mgToOffsetBinary(pose[i] + TOLERANCE_MG);
    }
    prm.dwell = DWELL;
    prm.delay_cycles = 0;
    prm.wake_enabled = 1;
    prm.require_exit = 1;

    for (int w = AMOLED206_ORIENT_DATA_BASE; w < 128; w++) RTC_SLOW_MEM[w] = 0;
    RTC_SLOW_MEM[AMOLED206_ORIENT_EXIT] = 1;   // exit-first

    const int words = amoled206_orient_ulp_build(&prm);
    s_words = (int16_t)words;
    if (words <= 0) return false;
    if (amoled206_orient_ulp_load() != 0) { s_words = -1; return false; }

    // Hand GPIO14/15 to the RTC domain: OUT = 0, the ULP toggles output enable (open
    // drain), the board's 2.2k pull-ups hold the released level. gpio_sleep_sel_dis()
    // keeps IDF from switching the pads to their sleep configuration.
    wire.end();
    const gpio_num_t pins[2] = { (gpio_num_t)PIN_SCL, (gpio_num_t)PIN_SDA };
    for (gpio_num_t p : pins) {
        rtc_gpio_init(p);
        rtc_gpio_set_level(p, 0);
        rtc_gpio_set_direction(p, RTC_GPIO_MODE_INPUT_ONLY);
        rtc_gpio_pullup_dis(p);
        rtc_gpio_pulldown_dis(p);
        gpio_sleep_sel_dis(p);
    }
    s_armed = true;
    if (amoled206_orient_ulp_start(PERIOD_MS * 1000u) != 0) {
        reclaimBus();
        wire.begin(PIN_SDA, PIN_SCL);
        return false;
    }
    esp_sleep_enable_ulp_wakeup();
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);   // the ULP's RTC GPIOs
    return true;
}

bool GlanceWake::wokeByGlance() {
    return esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_ULP;
}

void GlanceWake::log() {
    if (!s_reclaimed) {
        Serial.printf("[glance] not armed last sleep (program %d words)\n", s_words);
        return;
    }
    Serial.printf("[glance] %s; program %d words, %u ULP runs, last sample X %d Y %d Z %d mg\n",
                  wokeByGlance() ? "woke on the pose" : "armed, woke otherwise",
                  s_words, s_runs, s_x, s_y, s_z);
    Serial.printf("[glance] bus at reclaim: SDA %s, SCL %s, %u recovery clocks, SDA after STOP %s\n",
                  s_sda_was_low ? "LOW" : "high", s_scl_was_low ? "LOW" : "high",
                  s_clocks, s_sda_after_low ? "LOW" : "high");
}

// === P R I V A T E ===

// Standard I2C bus recovery on the digital side, before Wire.begin(): if a slave (the
// QMI8658, mid-byte when the ULP was cut off) holds SDA low, clock SCL until it lets go
// (at most nine clocks), then send STOP. The ULP does the same at the start of every run;
// this is the awake-side twin. Records the SDA level found and the clocks needed.
void GlanceWake::recoverBus() {
    pinMode(PIN_SDA, INPUT_PULLUP);
    pinMode(PIN_SCL, OUTPUT_OPEN_DRAIN);
    digitalWrite(PIN_SCL, HIGH);
    delayMicroseconds(10);
    s_sda_was_low = (digitalRead(PIN_SDA) == LOW);
    s_scl_was_low = (digitalRead(PIN_SCL) == LOW);
    s_clocks = 0;
    while (digitalRead(PIN_SDA) == LOW && s_clocks < 9) {
        digitalWrite(PIN_SCL, LOW);  delayMicroseconds(5);
        digitalWrite(PIN_SCL, HIGH); delayMicroseconds(5);
        s_clocks++;
    }
    // STOP: SDA low -> high while SCL is high
    digitalWrite(PIN_SCL, LOW);  delayMicroseconds(5);
    pinMode(PIN_SDA, OUTPUT_OPEN_DRAIN);
    digitalWrite(PIN_SDA, LOW);  delayMicroseconds(5);
    digitalWrite(PIN_SCL, HIGH); delayMicroseconds(5);
    digitalWrite(PIN_SDA, HIGH); delayMicroseconds(5);
    s_sda_after_low = (digitalRead(PIN_SDA) == LOW);
    pinMode(PIN_SDA, INPUT);
    pinMode(PIN_SCL, INPUT);
}

// milli-g -> offset-binary high byte at +-4 g (32 counts per g), as the ULP compares it
int GlanceWake::mgToOffsetBinary(int mg) {
    const int hb = (mg >= 0) ? (mg * 32 + 500) / 1000 : -((-mg * 32 + 500) / 1000);
    return constrain(hb + 128, 0, 255);
}

// One ULP data word (raw two's-complement high byte) -> milli-g
int GlanceWake::mgFromWord(int word) {
    return (int)(int8_t)(RTC_SLOW_MEM[word] & 0xFF) * 1000 / 32;
}
