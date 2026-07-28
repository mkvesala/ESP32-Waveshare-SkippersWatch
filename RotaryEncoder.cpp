#include "RotaryEncoder.h"

// === P U B L I C ===

// Constructor
RotaryEncoder::RotaryEncoder()
    : _last_button_state(true)
    , _spinlock(portMUX_INITIALIZER_UNLOCKED) {}

// Initialize
void RotaryEncoder::begin() {
    if (_initialized) return;

    // Static pointer to 'this'
    s_instance = this;

    // Init encoder + button pins (button active-low with internal pull-up)
    pinMode(ENCODER_A_PIN, INPUT);
    pinMode(ENCODER_B_PIN, INPUT);
    pinMode(ENCODER_BTN_PIN, INPUT_PULLUP);

    // Initial states
    _last_state_clk = digitalRead(ENCODER_A_PIN);
    _last_button_state = digitalRead(ENCODER_BTN_PIN);

    // FreeRTOS tasks on core 0 (core 1: main loop)
    xTaskCreatePinnedToCore(
        encoderTask,           // Callback
        "RotaryEnc",           // Name
        2048,                  // Stack size
        this,                  // Param (this)
        1,                     // Priority
        &_encoder_task_handle, // Handle
        0                      // Core
    );

    xTaskCreatePinnedToCore(
        buttonTask,            // Callback
        "RotaryBtn",           // Name
        2048,                  // Stack size
        this,                  // Param (this)
        1,                     // Priority
        &_button_task_handle,  // Handle
        0                      // Core
    );

    _initialized = true;
}

// Return rotation direction -1 = CCW, 0 = neutral, +1 = CW and reset to 0
int8_t RotaryEncoder::getDirection() {
    int8_t dir;
    portENTER_CRITICAL(&_spinlock);
    dir = _direction;
    _direction = 0;
    portEXIT_CRITICAL(&_spinlock);
    return dir;
}

// Return true when knob button pressed and reset to false
bool RotaryEncoder::getButtonPressed() {
    bool pressed;
    portENTER_CRITICAL(&_spinlock);
    pressed = _button_pressed;
    _button_pressed = false;
    portEXIT_CRITICAL(&_spinlock);
    return pressed;
}

// Rotary knob task, 500 Hz
void RotaryEncoder::encoderTask(void* param) {
    RotaryEncoder* self = static_cast<RotaryEncoder*>(param);

    while (true) {
        self->processEncoder();
        vTaskDelay(pdMS_TO_TICKS(2));  // 2ms polling
    }
}

// Knob button task, 200 Hz
void RotaryEncoder::buttonTask(void* param) {
    RotaryEncoder* self = static_cast<RotaryEncoder*>(param);

    while (true) {
        self->processButton();
        vTaskDelay(pdMS_TO_TICKS(5));  // 5ms polling
    }
}

// Process the rotary knob rotation
void RotaryEncoder::processEncoder() {
    // Read current state of CLK
    uint8_t current_state_clk = digitalRead(ENCODER_A_PIN);

    // Capture rising edge
    if (current_state_clk != _last_state_clk && current_state_clk == 1) {
        // Read DT to determine direction
        uint8_t state_dt = digitalRead(ENCODER_B_PIN);

        portENTER_CRITICAL(&_spinlock);
        if (state_dt != current_state_clk) {
            // CCW
            _direction = -1;
            _counter--;
        } else {
            // CW
            _direction = 1;
            _counter++;
        }
        portEXIT_CRITICAL(&_spinlock);
    }

    _last_state_clk = current_state_clk;
}

// Process the knob button press
void RotaryEncoder::processButton() {

    // Read knob button state (LOW = pressed, INPUT_PULLUP)
    bool current_state = digitalRead(ENCODER_BTN_PIN);
    uint32_t now = millis();

    if (current_state == _last_button_state) return;
    if (now - _last_button_time < DEBOUNCE_MS) return;

    if (current_state == LOW && _last_button_state == HIGH) {
        portENTER_CRITICAL(&_spinlock);
        _button_pressed = true;
        portEXIT_CRITICAL(&_spinlock);
    }

    _last_button_state = current_state;
    _last_button_time = now;

}
