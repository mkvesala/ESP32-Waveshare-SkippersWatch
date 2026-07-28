#include "WeatherUI.h"
#include "ui.h"

// === P U B L I C ===

WeatherUI::WeatherUI(ESPNowReceiver& receiver)
    : _receiver(receiver) {}

lv_obj_t* WeatherUI::getLvglScreen() const {
    return ui_ContainerWeather;
}

void WeatherUI::begin() {
    if (_initialized) return;
    this->showWaiting();
    _initialized = true;
}

void WeatherUI::onEnter() {
    this->showWaiting();
}

void WeatherUI::update() {
    if (!_initialized) return;

    WeatherDelta w = _receiver.getWeatherData();
    uint32_t rx = _receiver.lastWeatherRxMillis();
    bool ok = (rx > 0) && ((millis() - rx) < WEATHER_TIMEOUT_MS);

    if (!ok) {
        this->showWaiting();
        return;
    }

    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f", w.temperature_c);
    lv_label_set_text(ui_LabelTemperatureValue, buf);

    snprintf(buf, sizeof(buf), "%.0f", w.humidity_p);
    lv_label_set_text(ui_LabelHumidityValue, buf);

    snprintf(buf, sizeof(buf), "%.0f", w.pressure_hpa);
    lv_label_set_text(ui_LabelPressureValue, buf);
}

// === P R I V A T E ===

void WeatherUI::showWaiting() {
    lv_label_set_text(ui_LabelTemperatureValue, "---");
    lv_label_set_text(ui_LabelHumidityValue, "---");
    lv_label_set_text(ui_LabelPressureValue, "---");
}
