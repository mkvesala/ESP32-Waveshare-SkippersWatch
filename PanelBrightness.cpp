#include "PanelBrightness.h"

// === P U B L I C ===

void PanelBrightness::set(uint8_t level) {
    if (_panel == nullptr) return;
    if (_sent && level == _level) return;
    _panel->setBrightness(level);
    _level = level;
    _sent = true;
}
