#pragma once

#include <Arduino.h>
#include <Wire.h>

// === C L A S S  G L A N C E W A K E ===
//
// - "Raise to look" deep-sleep wake. The ESP32-S3's ULP-FSM coprocessor keeps running
//   while the CPU sleeps. It bit-bangs the shared I2C bus (GPIO14/15 are RTC pins) about
//   five times a second, reads the QMI8658 accelerometer and wakes the CPU only once the
//   gravity vector has stayed inside a pose window for DWELL consecutive samples. Motion
//   alone (arm swings, a rolling boat) never wakes the watch. The pose has to be held.
// - The ULP program is chipguyhere's, vendored unchanged in amoled206_orient_ulp.c (MIT).
//   This class is our own wrapper around it, modelled on his AMOLED206_OrientationWatch
//   but using our Wire bus and PmuRails for the IMU set-up.
// - Exit-first: a pose only counts after the watch has been seen OUTSIDE it since arming,
//   so a watch put down face-up does not wake, time out and wake again.
// - Cost: +80 uA over the floor at 5 Hz (chipguyhere, PPK2). That is below what our
//   gauge can resolve. What matters for the budget is how often it wakes for nothing.
// - Mechanism only. PowerManager decides whether to arm it (PowerManager::GLANCE_WAKE).
// - Contract: arm() is the LAST thing before esp_deep_sleep_start(). From there on the
//   ULP owns the I2C pins. reclaimBus() must run FIRST on every boot, before anything
//   touches the bus.

class GlanceWake {

public:

    // Stop the ULP (timer off, program overwritten with HALT so a run spinning on the
    // wake handshake ends too), hand GPIO14/15 back to the digital side and clock out a
    // slave left holding SDA. A no-op unless the previous sleep was armed. Also captures
    // the ULP's last sample and the bus state found, for log().
    static void reclaimBus();

    // Build and load the ULP program, take the bus pins from Wire, start the ULP and
    // enable it as a wake source. The IMU must already be in accel-only low-power mode
    // (PmuRails::imuGlanceMode()). Returns false (and leaves the bus with Wire) if the
    // program did not fit or load.
    static bool arm(TwoWire& wire);

    // True if this boot is a glance wake
    static bool wokeByGlance();

    // One Serial line: program size, ULP runs and last sample of the previous sleep
    static void log();

private:

    // Fixed pose for now (no teach UI yet): face 45 degrees up from flat toward +X,
    // +-350 mg on every axis. chipguyhere's README-orientation default; a pose taught
    // by eye on his unit came out at X +0.73, Y +0.05, Z -0.70 g. Face-up is -Z.
    static constexpr int POSE_X_MG = 707;
    static constexpr int POSE_Y_MG = 0;
    static constexpr int POSE_Z_MG = -707;
    static constexpr int TOLERANCE_MG = 350;
    static constexpr int DWELL = 2;               // consecutive in-window samples
    static constexpr uint32_t PERIOD_MS = 200;    // ULP sample period (~0.4 s hold)

    static constexpr uint8_t PIN_SCL = 14;
    static constexpr uint8_t PIN_SDA = 15;

    static void recoverBus();
    static int mgToOffsetBinary(int mg);
    static int mgFromWord(int word);

};
