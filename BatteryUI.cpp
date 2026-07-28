#include "BatteryUI.h"
#include "ui.h"

// === P U B L I C ===

BatteryUI::BatteryUI(ESPNowReceiver& receiver)
    : _receiver(receiver) {}

lv_obj_t* BatteryUI::getLvglScreen() const {
    return ui_ContainerBatteries;
}

void BatteryUI::begin() {
    if (_initialized) return;
    this->showWaiting();
    _initialized = true;
}

void BatteryUI::onEnter() {
    this->showWaiting();
}

void BatteryUI::update() {
    if (!_initialized) return;

    BatteryDelta b = _receiver.getBatteryData();
    uint32_t rx = _receiver.lastBatteryRxMillis();
    bool ok = (rx > 0) && ((millis() - rx) < BATTERY_TIMEOUT_MS);

    if (!ok) {
        this->showWaiting();
        return;
    }

    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f", b.house_voltage);
    lv_label_set_text(ui_LabelHouseVValue, buf);

    snprintf(buf, sizeof(buf), "%.1f", b.start_voltage);
    lv_label_set_text(ui_LabelStartVValue, buf);

    snprintf(buf, sizeof(buf), "%.0f", b.house_soc);
    lv_label_set_text(ui_LabelHouseSOCValue, buf);

    snprintf(buf, sizeof(buf), "%+.1f", b.house_current);
    lv_label_set_text(ui_LabelHouseAValue, buf);
}

// === P R I V A T E ===

void BatteryUI::showWaiting() {
    lv_label_set_text(ui_LabelHouseVValue, "---");
    lv_label_set_text(ui_LabelStartVValue, "---");
    lv_label_set_text(ui_LabelHouseSOCValue, "---");
    lv_label_set_text(ui_LabelHouseAValue, "---");
}
