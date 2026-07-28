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
        int16_t pitch = h.getPitchDeg();   // + = bow up
        int16_t roll  = h.getRollDeg();    // + = starboard heel

        int bow_up   = pitch > 0 ?  pitch : 0;
        int bow_down = pitch < 0 ? -pitch : 0;
        int stbd     = roll  > 0 ?  roll  : 0;
        int port     = roll  < 0 ? -roll  : 0;

        char buf[8];
        snprintf(buf, sizeof(buf), "%d", bow_up);   lv_label_set_text(ui_LabelBowUpValue, buf);
        snprintf(buf, sizeof(buf), "%d", bow_down); lv_label_set_text(ui_LabelBowDownValue, buf);
        snprintf(buf, sizeof(buf), "%d", stbd);     lv_label_set_text(ui_LabelStbdValue, buf);
        snprintf(buf, sizeof(buf), "%d", port);     lv_label_set_text(ui_LabelPortValue, buf);
    } else {
        lv_label_set_text(ui_LabelBowUpValue, "---");
        lv_label_set_text(ui_LabelBowDownValue, "---");
        lv_label_set_text(ui_LabelStbdValue, "---");
        lv_label_set_text(ui_LabelPortValue, "---");
    }

    this->updateMarkers();
}

// === P R I V A T E ===

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
