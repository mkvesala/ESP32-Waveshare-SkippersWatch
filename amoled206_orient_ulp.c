// Vendored from chipguyhere/ESP32-S3-Touch-AMOLED-2.06_board
// src/amoled206_orient_ulp.c @ 2f3fd06 (read 2026-11-16).
// Copyright (c) 2026 chipguyhere, MIT License. Our wrapper: GlanceWake.
//
// LOCAL CHANGE (2026-11-17, Skipper's Watch): the 9-clock bus recovery moved from the
// START of each run to the END, just before HALT, on every exit path. On our board the
// QMI8658 holds SDA low after every run (GlanceWake::selfTest(): SDA low in 499 of 500
// samples between runs, 7-8 clocks to free it), so SDA sat low for the whole 200 ms idle
// gap, ~1.5 mA through the 2.2k pull-up all night (overnight test: 7.4 mA against ~5.3 mA
// without the glance wake). Recovering at the end leaves the bus released between runs;
// a run cut short by sleep entry is still cleaned up by the next run's end. Costs one
// instruction (R2 must now be zeroed explicitly). Search "LOCAL CHANGE" for the diff.
//
// amoled206_orient_ulp.c -- the ULP-FSM program behind
// AMOLED206_OrientationWatch: watch the QMI8658 accelerometer while the
// ESP32-S3 is in deep sleep, and wake the CPU only when the board has been
// held in a chosen orientation for N consecutive samples.
//
// Copyright (c) 2026 chipguyhere
// MIT License
//
// ---------------------------------------------------------------------------
// Why a ULP program at all
// ---------------------------------------------------------------------------
// The QMI8658 can interrupt on motion (wake-on-motion, any-motion, tap...) but
// not on ORIENTATION, and a watch wants "wake when I look at it", not "wake
// on every arm swing".  The S3's ULP coprocessor can run while the main CPU is
// in deep sleep, and GPIO14/15 -- the board's I2C bus -- are RTC GPIOs it can
// drive.  So the ULP reads the accelerometer itself and decides.
//
// The Arduino core ships the FSM-type ULP (CONFIG_ULP_COPROC_TYPE_FSM, 512
// bytes reserved) and the ULP macro "assembler" (esp32s3/ulp.h), but no ULP
// toolchain, so the program is built at RUN TIME from those C macros.
//
// ---------------------------------------------------------------------------
// What one run does (the ULP timer restarts it every period)
// ---------------------------------------------------------------------------
//   1. (LOCAL CHANGE: the I2C bus recovery runs at the END now, see the top)
//   2. count the run (ORIENT_RUNS)
//   3. START, 0x6B+W, register 0x35 (AX_L), repeated START, 0x6B+R
//   4. read 6 bytes (AX..AZ, little endian), ACK 5 / NACK the last, STOP;
//      keep only the HIGH byte of each axis (1/32 g at +/-4 g -- plenty for
//      orientation, and it saves instructions and data words)
//   5. (the bench build also stored the released bus levels here -- the
//      diagnostic that found the stuck-SDA problem; removed to make room)
//   6. per axis: inside [lo, hi]?  (one modular compare: (raw - start) & 0xFF
//      <= width, which also handles windows that straddle zero)
//   7. inside on all axes: (if "exit first" is still pending, nothing)
//      otherwise count++, and at `dwell` wait for the sleep controller's
//      RDY_FOR_WAKEUP and WAKE; outside: count = 0 and "exit first" cleared
//   8. HALT
//
// Software I2C, open drain: both pins' OUT bits are 0; "low" = output enable
// set (W1TS), "released" = output enable cleared (W1TC), and the board's
// 2.2k pull-ups make the high level.  No delays are inserted: the ULP's own
// instruction time gives SCL ~250 kHz, inside the QMI8658's 400 kHz.
//
// ---------------------------------------------------------------------------
// Size
// ---------------------------------------------------------------------------
// The FSM ULP has 4 registers (R3 holds subroutine return addresses), a stage
// counter, and the 512-byte reservation = 128 words for program AND data.
// Data lives in the last 7 words (121..127); the program must fit in 121.
// With all three axes in the window it is ~117 words.  Things done to fit:
// ACK/NACK folded into the READ subroutine (stage counter counts the bytes),
// high bytes only, one-compare axis test, no NACK error counter.
//
// The S3 macro set has no M_MOVL (load a label's address into a register), so
// subroutine calls and branches are emitted with placeholder targets and
// patched by the small label resolver below (FX_BX = absolute branch address,
// FX_MOVI = return address, FX_REL = relative compare-branch offset).
//
// ---------------------------------------------------------------------------
// Bench findings that shaped it (2026-10-05/06, one board, documented in
// README-orientation.md and README-engineering.md)
// ---------------------------------------------------------------------------
//  - First sleep tests "worked" only because the dwell count was reached in
//    the instant BEFORE deep sleep began and the wake was latched.  Genuine
//    sleep-time reads were broken.  Lesson: stop the ULP as the very first
//    thing after a wake (AMOLED206_Board::begin() does), or post-wake runs
//    overwrite the evidence.
//  - SDA was found stuck LOW from the first sleep-time run, reads ~0, while
//    the same sequence run awake was fine.  Cause: a ULP transaction cut short
//    by deep-sleep entry leaves the QMI8658 mid-byte holding SDA.  Fix: the
//    9-clock bus recovery at the start of every run (step 2).  After it: 10/10
//    sleep/wake cycles correct.
//  - WAKE is only issued once RTC_CNTL_RDY_FOR_WAKEUP is set, as Espressif's
//    ULP-FSM examples do.
//  - gpio_sleep_sel_dis() on both pins keeps ESP-IDF from switching them to
//    their sleep pad configuration (done in AMOLED206_OrientationWatch::arm()).
//  - Cost: +80 uA over the board's deep-sleep floor at 5 Hz (PPK2).

#include <string.h>
#include "ulp.h"
#include "ulp_common.h"
#include "soc/rtc_io_reg.h"
#include "soc/rtc_cntl_reg.h"
#include "amoled206_orient_ulp.h"

#define SCL_BIT (RTC_GPIO_ENABLE_W1TS_S + 14)    // RTC GPIO14
#define SDA_BIT (RTC_GPIO_ENABLE_W1TS_S + 15)    // RTC GPIO15
// Open drain: OUT is 0 for both pins; "low" = output enabled, "high" = released.
#define SCL_LO  I_WR_REG(RTC_GPIO_ENABLE_W1TS_REG, SCL_BIT, SCL_BIT, 1)
#define SCL_HI  I_WR_REG(RTC_GPIO_ENABLE_W1TC_REG, SCL_BIT, SCL_BIT, 1)
#define SDA_LO  I_WR_REG(RTC_GPIO_ENABLE_W1TS_REG, SDA_BIT, SDA_BIT, 1)
#define SDA_HI  I_WR_REG(RTC_GPIO_ENABLE_W1TC_REG, SDA_BIT, SDA_BIT, 1)
#define SDA_RD  I_RD_REG(RTC_GPIO_IN_REG, RTC_GPIO_IN_NEXT_S + 15, RTC_GPIO_IN_NEXT_S + 15)

#define MAXP AMOLED206_ORIENT_DATA_BASE
static ulp_insn_t P[MAXP];
static int n;

enum { L_SEND, L_READ, L_ERR, L_FAIL, L_DONE, L_SL, L_SZ, L_SC, L_SD, L_RL, L_RD,
       L_R1, L_R2, L_R3, L_R4, L_R5, L_R6, L_A1, L_A2, L_A3, L_NACK, L_RDY, L_CLR, L_CLRDONE, NLABELS };
static int lab[NLABELS];
enum { FX_BX, FX_MOVI, FX_REL };
static struct { int at, label, type; } fx[64];
static int nfx;

static void E(ulp_insn_t i) { if (n < MAXP) P[n] = i; n++; }
static void LABEL(int l) { lab[l] = n; }
static void BX(int l)   { fx[nfx++] = (typeof(fx[0])){ n, l, FX_BX };   E((ulp_insn_t)I_BXI(0)); }
static void BXZ(int l)  { fx[nfx++] = (typeof(fx[0])){ n, l, FX_BX };   E((ulp_insn_t)I_BXZI(0)); }
static void MOVL(int r, int l) { fx[nfx++] = (typeof(fx[0])){ n, l, FX_MOVI }; E((ulp_insn_t)I_MOVI(r, 0)); }
static void BL(int l, int imm) { fx[nfx++] = (typeof(fx[0])){ n, l, FX_REL }; E((ulp_insn_t)I_BL(0, imm)); }
static void BG(int l, int imm) { fx[nfx++] = (typeof(fx[0])){ n, l, FX_REL }; E((ulp_insn_t)I_BG(0, imm)); }
static void BSGE(int l, int imm) { fx[nfx++] = (typeof(fx[0])){ n, l, FX_REL }; E((ulp_insn_t)I_BSGE(0, imm)); }
static void BE(int l, int imm) { fx[nfx++] = (typeof(fx[0])){ n, l, FX_REL }; E((ulp_insn_t)I_BE(0, imm)); }
static void CALL(int sub, int ret) { MOVL(R3, ret); BX(sub); LABEL(ret); }
static void DLY(int c) { if (c > 0) E((ulp_insn_t)I_DELAY(c)); }

// One axis test on a high byte stored at word `off`: offset-binary, then
// branch to FAIL if outside [lo, hi].  R2 must be 0.
static void axis(int off, int lo, int hi) {
    if (lo <= 0 && hi >= 255) return;            // don't care
    // lo/hi are offset-binary (raw + 128).  In raw two's-complement terms the
    // window starts at lo-128; (raw - start) mod 256 <= width is "inside".
    int start = (lo - 128) & 0xFF, width = hi - lo;
    E((ulp_insn_t)I_LD(R0, R2, off));
    E((ulp_insn_t)I_SUBI(R0, R0, start));
    E((ulp_insn_t)I_ANDI(R0, R0, 0xFF));
    BG(L_FAIL, width);
}

int amoled206_orient_ulp_build(const amoled206_orient_params_t *p) {
    n = 0; nfx = 0;
    for (int i = 0; i < NLABELS; i++) lab[i] = -1;
    const int d = p->delay_cycles;

    // Bus recovery: nine SCL clocks with SDA released, then STOP.  Frees a
    // slave left mid-byte -- which happens when deep-sleep entry cuts a ULP
    // transaction short (seen on the bench: SDA stuck low, every later read
    // ~0).  Harmless when nothing is stuck.
    // LOCAL CHANGE: the recovery block itself moved to L_DONE below.
    E((ulp_insn_t)I_MOVI(R2, 0));

    // Count the run.  R2 is 0 here (LOCAL CHANGE: zeroed above, no longer by
    // the recovery loop), and stays 0 as the base register for every data word below.
    E((ulp_insn_t)I_LD(R0, R2, AMOLED206_ORIENT_RUNS));
    E((ulp_insn_t)I_ADDI(R0, R0, 1));
    E((ulp_insn_t)I_ST(R0, R2, AMOLED206_ORIENT_RUNS));

    // START, address+W, register 0x35 (AX_L)
    E((ulp_insn_t)SDA_LO); DLY(d); E((ulp_insn_t)SCL_LO);
    E((ulp_insn_t)I_MOVI(R1, (AMOLED206_ORIENT_IMU_ADDR << 1) | 0));
    CALL(L_SEND, L_A1);
    BE(L_ERR, 1);                                  // NACK on address: give up
    E((ulp_insn_t)I_MOVI(R1, 0x35));
    CALL(L_SEND, L_A2);
    // repeated START, address+R
    E((ulp_insn_t)SDA_HI); DLY(d); E((ulp_insn_t)SCL_HI); DLY(d);
    E((ulp_insn_t)SDA_LO); DLY(d); E((ulp_insn_t)SCL_LO);
    E((ulp_insn_t)I_MOVI(R1, (AMOLED206_ORIENT_IMU_ADDR << 1) | 1));
    CALL(L_SEND, L_A3);

    // six data bytes; keep the high bytes (1, 3, 5).  READ counts bytes in
    // stage_cnt and ACKs all but the sixth.
    E((ulp_insn_t)I_STAGE_RST(0, 0, 0));
    static const int rl[6] = { L_R1, L_R2, L_R3, L_R4, L_R5, L_R6 };
    for (int b = 0; b < 6; b++) {
        CALL(L_READ, rl[b]);
        if (b & 1) E((ulp_insn_t)I_ST(R1, R2, AMOLED206_ORIENT_AX + b / 2));   // R2 == 0 after READ
    }
    // STOP
    E((ulp_insn_t)SDA_LO); DLY(d); E((ulp_insn_t)SCL_HI); DLY(d); E((ulp_insn_t)SDA_HI);

    // ---- the window test --------------------------------------------------
    axis(AMOLED206_ORIENT_AX, p->lo[0], p->hi[0]);
    axis(AMOLED206_ORIENT_AY, p->lo[1], p->hi[1]);
    axis(AMOLED206_ORIENT_AZ, p->lo[2], p->hi[2]);
    // pass.  With require_exit, a match only counts once the board has been
    // seen outside the pose since arming (the EXIT word is cleared on FAIL),
    // so a watch left lying in the pose doesn't wake again and again.
    if (p->require_exit) {
        E((ulp_insn_t)I_LD(R0, R2, AMOLED206_ORIENT_EXIT));
        BG(L_DONE, 0);
    }
    // count++, wake when count reaches dwell
    E((ulp_insn_t)I_LD(R0, R2, AMOLED206_ORIENT_COUNT));
    E((ulp_insn_t)I_ADDI(R0, R0, 1));
    E((ulp_insn_t)I_ST(R0, R2, AMOLED206_ORIENT_COUNT));
    BL(L_DONE, p->dwell);
    E((ulp_insn_t)I_MOVI(R0, 0));
    E((ulp_insn_t)I_ST(R0, R2, AMOLED206_ORIENT_COUNT));
    if (p->wake_enabled) {
        // Only signal the wake once the sleep controller says it can take one
        // (RTC_CNTL_RDY_FOR_WAKEUP), as Espressif's ULP-FSM examples do.  A
        // WAKE issued before that was seen on the bench to leave the chip
        // asleep with the I2C bus disturbed (SDA stuck low).
        LABEL(L_RDY);
        E((ulp_insn_t)I_RD_REG(RTC_CNTL_LOW_POWER_ST_REG, RTC_CNTL_RDY_FOR_WAKEUP_S, RTC_CNTL_RDY_FOR_WAKEUP_S));
        BE(L_RDY, 0);
        E((ulp_insn_t)I_WAKE());
    }
    BX(L_DONE);
    LABEL(L_FAIL);
    E((ulp_insn_t)I_MOVI(R0, 0));
    E((ulp_insn_t)I_ST(R0, R2, AMOLED206_ORIENT_COUNT));
    if (p->require_exit) E((ulp_insn_t)I_ST(R0, R2, AMOLED206_ORIENT_EXIT));
    BX(L_DONE);
    LABEL(L_ERR);                                  // NACK: STOP, count the error
    E((ulp_insn_t)SDA_LO); E((ulp_insn_t)SCL_HI); DLY(d); E((ulp_insn_t)SDA_HI);
    LABEL(L_DONE);
    // LOCAL CHANGE: bus recovery at the end of every run (was at the start)
    E((ulp_insn_t)SDA_HI);
    E((ulp_insn_t)I_MOVI(R2, 9));
    LABEL(L_CLR);
    E((ulp_insn_t)SCL_LO); DLY(d); E((ulp_insn_t)SCL_HI); DLY(d);
    E((ulp_insn_t)I_SUBI(R2, R2, 1));
    BXZ(L_CLRDONE);
    BX(L_CLR);
    LABEL(L_CLRDONE);
    E((ulp_insn_t)SCL_LO); E((ulp_insn_t)SDA_LO); E((ulp_insn_t)SCL_HI); E((ulp_insn_t)SDA_HI);
    E((ulp_insn_t)I_HALT());

    // ---- SEND: byte in R1, MSB first; returns ACK bit in R0 (0 = ACK) -------
    LABEL(L_SEND);
    E((ulp_insn_t)I_MOVI(R2, 8));
    LABEL(L_SL);
    E((ulp_insn_t)I_ANDI(R0, R1, 0x80));
    BXZ(L_SZ);
    E((ulp_insn_t)SDA_HI);
    BX(L_SC);
    LABEL(L_SZ);
    E((ulp_insn_t)SDA_LO);
    LABEL(L_SC);
    DLY(d); E((ulp_insn_t)SCL_HI); DLY(d); E((ulp_insn_t)SCL_LO);
    E((ulp_insn_t)I_LSHI(R1, R1, 1));
    E((ulp_insn_t)I_SUBI(R2, R2, 1));
    BXZ(L_SD);
    BX(L_SL);
    LABEL(L_SD);
    E((ulp_insn_t)SDA_HI); DLY(d); E((ulp_insn_t)SCL_HI); DLY(d);
    E((ulp_insn_t)SDA_RD);
    E((ulp_insn_t)SCL_LO);
    E((ulp_insn_t)I_BXR(R3));

    // ---- READ: 8 bits into R1, MSB first; leaves R2 = 0 ----------------------
    LABEL(L_READ);
    E((ulp_insn_t)I_MOVI(R1, 0));
    E((ulp_insn_t)I_MOVI(R2, 8));
    E((ulp_insn_t)SDA_HI);
    LABEL(L_RL);
    DLY(d); E((ulp_insn_t)SCL_HI); DLY(d);
    E((ulp_insn_t)SDA_RD);
    E((ulp_insn_t)SCL_LO);
    E((ulp_insn_t)I_LSHI(R1, R1, 1));
    E((ulp_insn_t)I_ORR(R1, R1, R0));
    E((ulp_insn_t)I_SUBI(R2, R2, 1));
    BXZ(L_RD);
    BX(L_RL);
    LABEL(L_RD);
    E((ulp_insn_t)I_STAGE_INC(0, 0, 1));
    BSGE(L_NACK, 6);                               // sixth byte: NACK
    E((ulp_insn_t)SDA_LO); E((ulp_insn_t)SCL_HI); DLY(d); E((ulp_insn_t)SCL_LO); E((ulp_insn_t)SDA_HI);
    E((ulp_insn_t)I_BXR(R3));
    LABEL(L_NACK);
    E((ulp_insn_t)SCL_HI); DLY(d); E((ulp_insn_t)SCL_LO);
    E((ulp_insn_t)I_BXR(R3));

    if (n > MAXP) return -n;                      // too big for the space before the data
    for (int i = 0; i < nfx; i++) {
        int at = fx[i].at, t = lab[fx[i].label];
        if (t < 0) return -1000 - fx[i].label;
        switch (fx[i].type) {
            case FX_BX:   P[at].bx.addr = t; break;
            case FX_MOVI: P[at].alu_imm.imm = t; break;
            case FX_REL: {
                int off = t - at;
                P[at].b.offset = off < 0 ? -off : off;
                P[at].b.sign = off < 0 ? 1 : 0;
                break; }
        }
    }
    return n;
}

int amoled206_orient_ulp_load(void) {
    size_t sz = n;
    return ulp_process_macros_and_load(0, P, &sz);
}

int amoled206_orient_ulp_start(unsigned period_us) {
    ulp_set_wakeup_period(0, period_us);
    return ulp_run(0);
}

void amoled206_orient_ulp_stop(void) {
    ulp_timer_stop();
}
