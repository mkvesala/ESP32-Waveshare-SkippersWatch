#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "ChartFormat.h"
#include "ChartStore.h"

// === C L A S S  C H A R T E N G I N E ===
//
// - Composes chart views off the UI loop, so touch and the page keep running during a read.
//   FreeRTOS task "Chart", core 0, priority 1 (main loop and LVGL on core 1). Created and its
//   memory allocated on the first request of a wake: a wake that never opens the chart page
//   neither reads the card while awake nor spends the memory.
// - Two RGB565 frames in PSRAM. The task writes the back frame while the page shows the
//   front one, and takeResult() swaps them on the loop task, so a half-written view can
//   never reach the display.
// - Bow-up (a request with a rotation): the task reads a SOURCE_PX square of palette indices
//   around the boat into a third PSRAM buffer, allocated on the first such request, and
//   rotates it into the back frame. While the next request's rotated view still fits in that
//   square, only the rotation runs: a turn without much way on reads nothing from the card.
//   North-up requests take the original path and never touch the square.
// - The newest request wins: requests that arrive during a compose collapse into one.
// - shutdown() on the way into deep sleep waits for a read in flight and leaves the card idle.
//   It mounts and closes the card if that has not happened on this wake: a card never
//   initialised since it got power draws far more asleep (see leaveCardIdle).
// - Requests and results are exchanged under a spinlock; the card is only touched by the task.
// - Uses: ChartStore, ChartFormat, ChartGeometry
// - Owned by: ChartUI

class ChartEngine {

public:

    enum class Status : uint8_t {
        OK,          // the frame shows chart data, possibly with black holes
        NO_DATA,     // the frame is valid but entirely black: this area is not on the card
        NO_CARD,     // no card, an unreadable one, or no valid chart on it. No new frame.
        NO_MEMORY,   // PSRAM allocation failed. No new frame.
    };

    struct Result {
        Status             status = Status::NO_CARD;
        bool               rotated = false;    // a bow-up frame: grid bearing rot_deg is up
        double             rot_deg = 0.0;
        const uint16_t*    pixels = nullptr;   // null unless a new frame came with this result
        ChartFormat::View  view = {};
        double             m_per_px = 0.0;
        double             boat_px = 0.0;      // the request's position, in chart pixels
        double             boat_py = 0.0;
        double             lat_deg = 0.0;      // the request's position
        double             lon_deg = 0.0;
    };

    ChartEngine(uint16_t view_w, uint16_t view_h) : _view_w(view_w), _view_h(view_h) {}

    // Compose a view centered on this position. Starts the task on the first call.
    void request(double lat_deg, double lon_deg, bool night);

    // The same, rotated so that grid bearing rot_deg points up
    void requestRotated(double lat_deg, double lon_deg, bool night, double rot_deg);

    // The Chart task's unused stack, in bytes; 0 before the task has started
    uint32_t stackFreeBytes() const;

    // Loop task. True when a new result is ready; a new frame in it replaces the old one,
    // whose memory the task may then reuse.
    bool takeResult(Result& out);

    bool isBusy() const;

    // Waits up to timeout_ms for a read in flight, then leaves the card idle. If the task was
    // never started, the mount and close run on the caller without a timeout.
    void shutdown(uint32_t timeout_ms);

private:

    struct Pending {
        double lat_deg;
        double lon_deg;
        bool   night;
        bool   rotate;
        double rot_deg;
    };

    // The bow-up source square: 768 covers the 648 px diagonal of the view with about 60 px
    // to spare on each side, so small moves rotate the square already read. 590 kB of PSRAM.
    static constexpr uint16_t SOURCE_PX = 768;

    // Generous until the high-water mark has been measured on the watch
    static constexpr uint32_t TASK_STACK_BYTES = 8192;
    static constexpr UBaseType_t TASK_PRIORITY = 1;
    static constexpr BaseType_t TASK_CORE = 0;

    const uint16_t _view_w;
    const uint16_t _view_h;

    ChartStore _store;
    TaskHandle_t _task = nullptr;
    mutable portMUX_TYPE _lock = portMUX_INITIALIZER_UNLOCKED;

    // Under _lock
    Pending _pending = {};
    bool _has_pending = false;
    bool _composing = false;
    bool _stop = false;
    bool _stopped = false;
    bool _result_ready = false;
    Result _result;
    uint8_t _front = 0;

    // Task only, after allocation
    uint16_t* _frames[2] = { nullptr, nullptr };
    uint8_t* _tile_buffer = nullptr;
    ChartFormat::Meta* _meta = nullptr;
    bool _meta_valid = false;
    uint8_t* _source = nullptr;          // bow-up only, allocated on its first request
    bool _source_valid = false;
    ChartFormat::View _source_view = {};

    static void taskEntry(void* param);
    void run();
    void leaveCardIdle();
    bool allocate();
    void enqueue(const Pending& p);
    bool prepareCard(Result& r);
    void unmountCard();
    void compose(const Pending& p);
    void composeRotated(const Pending& p);
    void publish(const Result& r);

};
