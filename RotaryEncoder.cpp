#include "RotaryEncoder.h"

// Full-step quadrature decoder (Ben Buxton's table). Reads both A and B every poll
// and only emits one CW/CCW event per completed detent, walking a state machine that
// rejects contact bounce and half-steps. Replaces the old rising-edge-of-A count,
// which fired on every bounce edge → multiple page jumps + wrong direction per click.
namespace {
    constexpr uint8_t R_START     = 0x0;
    constexpr uint8_t R_CW_FINAL  = 0x1;
    constexpr uint8_t R_CW_BEGIN  = 0x2;
    constexpr uint8_t R_CW_NEXT   = 0x3;
    constexpr uint8_t R_CCW_BEGIN = 0x4;
    constexpr uint8_t R_CCW_FINAL = 0x5;
    constexpr uint8_t R_CCW_NEXT  = 0x6;
    constexpr uint8_t DIR_CW      = 0x10;   // emitted in the state's high bits
    constexpr uint8_t DIR_CCW     = 0x20;

    // Indexed [current state][pin state = (A<<1)|B]. Low nibble = next state.
    constexpr uint8_t kTable[7][4] = {
        {R_START,    R_CW_BEGIN,  R_CCW_BEGIN, R_START},              // R_START
        {R_CW_NEXT,  R_START,     R_CW_FINAL,  R_START | DIR_CW},     // R_CW_FINAL
        {R_CW_NEXT,  R_CW_BEGIN,  R_START,     R_START},              // R_CW_BEGIN
        {R_CW_NEXT,  R_CW_BEGIN,  R_CW_FINAL,  R_START},              // R_CW_NEXT
        {R_CCW_NEXT, R_START,     R_CCW_BEGIN, R_START},              // R_CCW_BEGIN
        {R_CCW_NEXT, R_CCW_FINAL, R_START,     R_START | DIR_CCW},    // R_CCW_FINAL
        {R_CCW_NEXT, R_CCW_FINAL, R_CCW_BEGIN, R_START},              // R_CCW_NEXT
    };
}

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

    // Initial states (quadrature decoder starts in R_START)
    _enc_state = R_START;
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

// Process the rotary knob rotation via the full-step state table (bounce-immune).
// One completed detent → exactly one _direction event; partial/bouncing transitions
// keep walking the table without emitting.
void RotaryEncoder::processEncoder() {
    uint8_t pinstate = (digitalRead(ENCODER_A_PIN) << 1) | digitalRead(ENCODER_B_PIN);
    _enc_state = kTable[_enc_state & 0x0f][pinstate];
    uint8_t emit = _enc_state & 0x30;

    if (emit) {
        portENTER_CRITICAL(&_spinlock);
        if (emit == DIR_CW) {
            _direction = 1;
            _counter++;
        } else {
            _direction = -1;
            _counter--;
        }
        portEXIT_CRITICAL(&_spinlock);
    }
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
