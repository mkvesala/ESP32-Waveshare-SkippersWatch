#pragma once

#include <Arduino.h>

// === C L A S S  R O T A R Y E N C O D E R ===
//
// - Class RotaryEncoder - manages the CrowPanel 2.01" Watch rotary encoder (ALPS EC11)
// - Init: _encoder.begin();
// - FreeRTOS Tasks:
//  - encoderTask, "RotaryEnc", core 0 - reads knob rotation
//  - buttonTask, "RotaryBtn", core 0 - reads knob press
//  - (main loop runs on core 1)
// - Provides public API to get rotation events, button presses, and task handles
// - Owned by: CrowPanelApplication
// - Hardware (from Elecrow example config, see hardware_notes.md):
//  - Encoder A:  GPIO20
//  - Encoder B:  GPIO19
//  - Switch:     GPIO18 (active-low, internal pull-up) — RTC-capable → deep-sleep wake source
//
// Ported from the ESP32-Crowpanel-compass verrokki: same read-and-clear API and
// two-task delivery, but the button is a direct GPIO (no PCF8574 I/O expander).

class RotaryEncoder {

public:

    explicit RotaryEncoder();

    void begin();
    int8_t getDirection();
    bool getButtonPressed();

    TaskHandle_t getEncoderTaskHandle() const { return _encoder_task_handle; }
    TaskHandle_t getButtonTaskHandle() const { return _button_task_handle; }

    // Encoder button GPIO — exposed so PowerManager can arm it as a deep-sleep wake source
    static constexpr uint8_t ENCODER_BTN_PIN = 18;

private:

    static void encoderTask(void* param);
    static void buttonTask(void* param);
    void processEncoder();
    void processButton();

    // Encoder pins A and B
    static constexpr uint8_t ENCODER_A_PIN = 20;
    static constexpr uint8_t ENCODER_B_PIN = 19;

    // Debounce
    static constexpr uint32_t DEBOUNCE_MS = 50;

    // Rotary state
    volatile int8_t _direction = 0;
    volatile int32_t _counter = 0;
    volatile uint8_t _enc_state = 0;   // quadrature decoder state (R_START); see .cpp

    // Button state
    volatile bool _button_pressed = false;
    volatile bool _last_button_state;
    volatile uint32_t _last_button_time = 0;

    // Static pointer for FreeRTOS tasks
    inline static RotaryEncoder *s_instance = nullptr;

    // FreeRTOS
    TaskHandle_t _encoder_task_handle = nullptr;
    TaskHandle_t _button_task_handle = nullptr;
    portMUX_TYPE _spinlock;

    bool _initialized = false;

};
