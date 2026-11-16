// Vendored unchanged from chipguyhere/ESP32-S3-Touch-AMOLED-2.06_board
// src/amoled206_orient_ulp.h @ 2f3fd06 (read 2026-11-16).
// Copyright (c) 2026 chipguyhere, MIT License.
#pragma once
// Shared between the ULP program builder (orient_ulp.c) and the sketch.
#ifdef __cplusplus
extern "C" {
#endif

#define AMOLED206_ORIENT_IMU_ADDR   0x6B
// Word addresses in RTC slow memory (the ULP's reserved 512 bytes = 128 words;
// the program must fit below AMOLED206_ORIENT_DATA_BASE).
#define AMOLED206_ORIENT_DATA_BASE  121
#define AMOLED206_ORIENT_EXIT       121   // nonzero: ignore the pose until a sample is OUTSIDE it
#define AMOLED206_ORIENT_RUNS       122   // ULP runs completed (16 bits, wraps)
#define AMOLED206_ORIENT_IDLE       123   // (unused now; was the idle-bus-level diagnostic)
#define AMOLED206_ORIENT_AX         124   // last high byte of AX/AY/AZ (raw, two's complement)
#define AMOLED206_ORIENT_AY         125
#define AMOLED206_ORIENT_AZ         126
#define AMOLED206_ORIENT_COUNT      127   // consecutive in-window samples

typedef struct {
    // Window per axis on the offset-binary high byte (raw + 128), 0..255;
    // lo <= 0 and hi >= 255 means "don't care".  At +/-4 g, 32 counts = 1 g.
    int lo[3], hi[3];
    int dwell;            // consecutive in-window samples needed
    int delay_cycles;     // I2C half-period padding (ULP clock cycles)
    int wake_enabled;     // 0 = just measure (for testing while awake)
    int require_exit;     // 1 = compile in the "leave the pose first" check
} amoled206_orient_params_t;

int amoled206_orient_ulp_build(const amoled206_orient_params_t *p);   // returns size in words, < 0 on error
int amoled206_orient_ulp_load(void);                        // 0 (ESP_OK) on success
int amoled206_orient_ulp_start(unsigned period_us);         // 0 on success
void amoled206_orient_ulp_stop(void);

#ifdef __cplusplus
}
#endif
