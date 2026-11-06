#include "ChartEngine.h"
#include "ChartGeometry.h"
#include <esp_heap_caps.h>

// === P U B L I C ===

void ChartEngine::request(double lat_deg, double lon_deg, bool night) {
    portENTER_CRITICAL(&_lock);
    _pending = { lat_deg, lon_deg, night };
    _has_pending = true;
    _stop = false;
    _stopped = false;
    portEXIT_CRITICAL(&_lock);

    if (_task == nullptr) {
        xTaskCreatePinnedToCore(&ChartEngine::taskEntry, "Chart", TASK_STACK_BYTES, this,
                                TASK_PRIORITY, &_task, TASK_CORE);
        return;   // the task looks for pending work before it first blocks
    }
    xTaskNotifyGive(_task);
}

bool ChartEngine::takeResult(Result& out) {
    portENTER_CRITICAL(&_lock);
    const bool ready = _result_ready;
    if (ready) {
        out = _result;
        _result_ready = false;
        // Swap here, on the loop task: the page draws from the new front frame from now on
        if (out.pixels != nullptr) _front = (uint8_t)(1 - _front);
    }
    portEXIT_CRITICAL(&_lock);
    return ready;
}

bool ChartEngine::isBusy() const {
    portENTER_CRITICAL(&_lock);
    const bool busy = _composing || _has_pending;
    portEXIT_CRITICAL(&_lock);
    return busy;
}

void ChartEngine::shutdown(uint32_t timeout_ms) {
    if (_task == nullptr) {
        this->leaveCardIdle();   // no task, so the card is free to use from this one
        return;
    }

    portENTER_CRITICAL(&_lock);
    _has_pending = false;
    _stop = true;
    portEXIT_CRITICAL(&_lock);
    xTaskNotifyGive(_task);

    const uint32_t start = millis();
    while (millis() - start < timeout_ms) {
        portENTER_CRITICAL(&_lock);
        const bool stopped = _stopped;
        portEXIT_CRITICAL(&_lock);
        if (stopped) return;
        delay(5);
    }
    Serial.println("[chart] card still busy at sleep entry");
}

// === P R I V A T E ===

void ChartEngine::taskEntry(void* param) {
    static_cast<ChartEngine*>(param)->run();
}

void ChartEngine::run() {
    const bool allocated = this->allocate();

    for (;;) {
        portENTER_CRITICAL(&_lock);
        const bool stop = _stop && !_stopped;
        const bool has_work = _has_pending;
        const Pending work = _pending;
        _has_pending = false;
        _composing = has_work;
        portEXIT_CRITICAL(&_lock);

        if (stop) {
            this->leaveCardIdle();
            _meta_valid = false;
            portENTER_CRITICAL(&_lock);
            _stopped = true;
            portEXIT_CRITICAL(&_lock);
        } else if (has_work) {
            if (allocated) {
                this->compose(work);
            } else {
                Result r;
                r.status = Status::NO_MEMORY;
                this->publish(r);
            }
            portENTER_CRITICAL(&_lock);
            _composing = false;
            portEXIT_CRITICAL(&_lock);
            continue;   // another request may have come in during the read
        }
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }
}

// Measured on the watch: 42 mA asleep with a card never initialised since it got power, about
// 6-8 mA after one mount and close. A card can be put in at any time unnoticed, so this runs
// before every sleep, also when no card is expected.
void ChartEngine::leaveCardIdle() {
    _store.mount();
    _store.unmount();
}

bool ChartEngine::allocate() {
    const size_t frame_bytes = (size_t)_view_w * _view_h * sizeof(uint16_t);
    _frames[0] = (uint16_t*)heap_caps_malloc(frame_bytes, MALLOC_CAP_SPIRAM);
    _frames[1] = (uint16_t*)heap_caps_malloc(frame_bytes, MALLOC_CAP_SPIRAM);
    _tile_buffer = (uint8_t*)heap_caps_malloc(ChartFormat::TILE_BYTES, MALLOC_CAP_SPIRAM);
    _meta = (ChartFormat::Meta*)heap_caps_malloc(sizeof(ChartFormat::Meta), MALLOC_CAP_SPIRAM);
    return _frames[0] != nullptr && _frames[1] != nullptr && _tile_buffer != nullptr && _meta != nullptr;
}

void ChartEngine::compose(const Pending& p) {
    Result r;
    r.lat_deg = p.lat_deg;
    r.lon_deg = p.lon_deg;

    // Mounted on demand and kept mounted until sleep. A card put in later is found by the
    // next request, because a failed read or mount leaves the store unmounted.
    if (!_store.isMounted()) {
        _meta_valid = false;
        if (!_store.mount()) {
            r.status = Status::NO_CARD;
            this->publish(r);
            return;
        }
    }
    if (!_meta_valid) _meta_valid = _store.readMeta(*_meta);
    if (!_meta_valid) {
        _store.unmount();
        r.status = Status::NO_CARD;
        this->publish(r);
        return;
    }

    const ChartGeometry::GridPoint g = ChartGeometry::wgs84ToTm35(p.lat_deg, p.lon_deg);
    ChartFormat::toPixel(*_meta, g.e, g.n, r.boat_px, r.boat_py);
    r.view = ChartFormat::centeredView(r.boat_px, r.boat_py, _view_w, _view_h);
    r.m_per_px = _meta->m_per_px;

    portENTER_CRITICAL(&_lock);
    const uint8_t back = (uint8_t)(1 - _front);
    // An unclaimed frame in the back buffer is being overwritten, so its result goes too
    _result_ready = false;
    portEXIT_CRITICAL(&_lock);

    uint16_t* frame = _frames[back];
    const uint16_t* palette = p.night ? _meta->palette_night : _meta->palette_day;
    bool any_data = false;
    bool failed = false;

    ChartFormat::forEachSpan(r.view, [&](const ChartFormat::Span& s) {
        if (failed) return;
        switch (_store.readSpan(s, _tile_buffer)) {
            case ChartStore::ReadResult::OK:
                ChartFormat::expandSpan(s, _tile_buffer, palette, frame, _view_w);
                any_data = true;
                break;
            case ChartStore::ReadResult::NO_BLOCK:
                ChartFormat::blankSpan(s, frame, _view_w);
                break;
            case ChartStore::ReadResult::FAILED:
                failed = true;
                break;
        }
    });

    if (failed) {
        _store.unmount();
        r.status = Status::NO_CARD;
        this->publish(r);
        return;
    }

    r.status = any_data ? Status::OK : Status::NO_DATA;
    r.pixels = frame;
    this->publish(r);
}

void ChartEngine::publish(const Result& r) {
    portENTER_CRITICAL(&_lock);
    _result = r;
    _result_ready = true;
    portEXIT_CRITICAL(&_lock);
}
