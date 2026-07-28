#include "EngineUI.h"
#include "ui.h"

// === P U B L I C ===

EngineUI::EngineUI(ESPNowReceiver& receiver)
    : _receiver(receiver) {}

lv_obj_t* EngineUI::getLvglScreen() const {
    return ui_ContainerEngineroom;
}

void EngineUI::begin() {
    if (_initialized) return;
    this->showWaiting();
    _initialized = true;
}

void EngineUI::onEnter() {
    this->showWaiting();
}

void EngineUI::update() {
    if (!_initialized) return;

    const uint32_t now = millis();

    // --- Exhaust temperature (Kelvin → °C) ---
    HALMETEngineDelta eng = _receiver.getEngineData();
    uint32_t eng_rx = _receiver.lastEngineRxMillis();
    bool eng_ok = (eng_rx > 0) && ((now - eng_rx) < ENGINE_TIMEOUT_MS);
    if (eng_ok) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%.0f", eng.exhaust_temp_k - 273.15f);
        lv_label_set_text(ui_LabelExhaustCValue, buf);
    } else {
        lv_label_set_text(ui_LabelExhaustCValue, "---");
    }

    // --- Fuel (ratio 0..1 → liters) ---
    HALMETTankDelta tank = _receiver.getTankData();
    uint32_t tank_rx = _receiver.lastTankRxMillis();
    bool tank_ok = (tank_rx > 0) && ((now - tank_rx) < TANK_TIMEOUT_MS);
    if (tank_ok) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%.0f", tank.fuel_level_ratio * FUEL_TANK_CAPACITY_L);
        lv_label_set_text(ui_LabelFuelLValue, buf);
    } else {
        lv_label_set_text(ui_LabelFuelLValue, "---");
    }

    // --- Fresh water (ratio 0..1 → liters) ---
    HALMETWaterDelta water = _receiver.getWaterData();
    uint32_t water_rx = _receiver.lastWaterRxMillis();
    bool water_ok = (water_rx > 0) && ((now - water_rx) < WATER_TIMEOUT_MS);
    if (water_ok) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%.0f", water.water_level_ratio * WATER_TANK_CAPACITY_L);
        lv_label_set_text(ui_LabelFreshwaterLValue, buf);
    } else {
        lv_label_set_text(ui_LabelFreshwaterLValue, "---");
    }
}

// === P R I V A T E ===

void EngineUI::showWaiting() {
    lv_label_set_text(ui_LabelExhaustCValue, "---");
    lv_label_set_text(ui_LabelFuelLValue, "---");
    lv_label_set_text(ui_LabelFreshwaterLValue, "---");
}
