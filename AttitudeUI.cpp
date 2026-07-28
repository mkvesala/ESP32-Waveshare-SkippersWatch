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
    this->updateMarkers();
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

    this->updateMarkers();
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

// Session extremes → max/min marker labels (independent of live-connection state)
void AttitudeUI::updateMarkers() {
    if (!ESPNowReceiver::hasMinMaxData()) {
        lv_label_set_text(ui_LabelBowUpMaxValue, "---");
        lv_label_set_text(ui_LabelBowDownMinValue, "---");
        lv_label_set_text(ui_LabelStbdMaxValue, "---");
        lv_label_set_text(ui_LabelPortMinValue, "---");
        return;
    }

    int max_bow_up   = ESPNowReceiver::getMaxPitch_x10() / 10;   // most positive pitch
    int max_bow_down = -(ESPNowReceiver::getMinPitch_x10() / 10); // magnitude of most negative pitch
    int max_stbd     = ESPNowReceiver::getMaxRoll_x10() / 10;    // most positive roll
    int max_port     = -(ESPNowReceiver::getMinRoll_x10() / 10);  // magnitude of most negative roll

    if (max_bow_up   < 0) max_bow_up   = 0;
    if (max_bow_down < 0) max_bow_down = 0;
    if (max_stbd     < 0) max_stbd     = 0;
    if (max_port     < 0) max_port     = 0;

    char buf[8];
    snprintf(buf, sizeof(buf), "%d", max_bow_up);   lv_label_set_text(ui_LabelBowUpMaxValue, buf);
    snprintf(buf, sizeof(buf), "%d", max_bow_down); lv_label_set_text(ui_LabelBowDownMinValue, buf);
    snprintf(buf, sizeof(buf), "%d", max_stbd);     lv_label_set_text(ui_LabelStbdMaxValue, buf);
    snprintf(buf, sizeof(buf), "%d", max_port);     lv_label_set_text(ui_LabelPortMinValue, buf);
}
