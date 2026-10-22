#pragma once

#include <Arduino.h>
#include "ESPNowReceiver.h"
#include "DemoValues.h"

// === C L A S S  D E M O S O U R C E ===
//
// - Demo mode's data source: turns a DemoValues snapshot into real ESP-NOW frames
//   (header + wire payload) and hands them to ESPNowReceiver::injectFrame(), so they go
//   through the same parser, slots and freshness timestamps as radio frames. The page
//   adapters cannot tell the difference and need no demo code of their own.
// - Frames are built on the stack and passed by pointer; nothing is allocated or kept.
// - Every source is sent, except DATETIME_DELTA: the system clock stays real (ClockUI
//   has its own demo fallback for an unsynced clock).
// - Owned by: WatchApplication. update(now) each loop while demo mode is on; it
//   self-gates to SEND_INTERVAL_MS, well inside every page's freshness timeout.

class DemoSource {

public:

    explicit DemoSource(ESPNowReceiver& receiver) : _receiver(receiver) {}

    void update(uint32_t now);

    // Send one full set right away (wake path: the first lit frame shows demo values)
    void sendAll(uint32_t now);

private:

    static constexpr uint32_t SEND_INTERVAL_MS = 1000;

    ESPNowReceiver& _receiver;
    uint32_t _last_send_ms = 0;
    bool _sent_once = false;

    template <typename T>
    void send(ESPNowMsgType type, const T& payload);

};
