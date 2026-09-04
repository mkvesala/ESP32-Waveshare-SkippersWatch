#include "AttitudeUI.h"
#include "ui.h"

// === P U B L I C ===

AttitudeUI::AttitudeUI(ESPNowReceiver& receiver)
    : _receiver(receiver) {}

lv_obj_t* AttitudeUI::getLvglScreen() const {
    return ui_ContainerAttitude;
}

void AttitudeUI::begin() {
    if (_initialized) return;
    this->showWaiting();
    _initialized = true;
}

void AttitudeUI::onEnter() {
    this->showWaiting();
}

void AttitudeUI::update() {
    if (!_initialized) return;

    HeadingData h = _receiver.getData();
    uint32_t rx = _receiver.lastHeadingRxMillis();
    bool ok = (rx > 0) && ((millis() - rx) < HEADING_TIMEOUT_MS);

    if (ok) {
        int16_t pitch = h.getPitchDeg();   // + = bow up,        - = bow down
        int16_t roll  = h.getRollDeg();    // + = starboard heel, - = port heel

        // Show the angle only on the side it belongs to; the opposite side is
        // blank (not even dashes). Exactly level (0° to whole-degree) shows "0"
        // on BOTH sides. Magnitude only — the side is conveyed by the label.
        this->showAxis(ui_LabelBowUpValue, ui_LabelBowDownValue, pitch);  // + = up (BowUp)
        this->showAxis(ui_LabelStbdValue,  ui_LabelPortValue,   roll);    // + = stbd, - = port
    } else {
        lv_label_set_text(ui_LabelBowUpValue, "---");
        lv_label_set_text(ui_LabelBowDownValue, "---");
        lv_label_set_text(ui_LabelStbdValue, "---");
        lv_label_set_text(ui_LabelPortValue, "---");
    }
}

// === P R I V A T E ===

// One heel/pitch axis: value on its own side, opposite side blank; 0 → both "0"
void AttitudeUI::showAxis(lv_obj_t* positive_label, lv_obj_t* negative_label, int16_t value) {
    char buf[8];
    if (value == 0) {
        lv_label_set_text(positive_label, "0");
        lv_label_set_text(negative_label, "0");
    } else if (value > 0) {
        snprintf(buf, sizeof(buf), "%d", value);
        lv_label_set_text(positive_label, buf);
        lv_label_set_text(negative_label, "");
    } else {
        snprintf(buf, sizeof(buf), "%d", -value);
        lv_label_set_text(positive_label, "");
        lv_label_set_text(negative_label, buf);
    }
}

void AttitudeUI::showWaiting() {
    lv_label_set_text(ui_LabelBowUpValue, "---");
    lv_label_set_text(ui_LabelBowDownValue, "---");
    lv_label_set_text(ui_LabelStbdValue, "---");
    lv_label_set_text(ui_LabelPortValue, "---");
}
