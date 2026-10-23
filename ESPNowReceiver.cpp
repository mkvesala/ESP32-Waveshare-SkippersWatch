#include "ESPNowReceiver.h"

// === P U B L I C ===

// Constructor
ESPNowReceiver::ESPNowReceiver() {}

// Initialization
bool ESPNowReceiver::begin(uint8_t channel) {
    if (_initialized) return true;

    _channel = channel;

    // Init and disconnect WiFi
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    // Set channel manually
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);

    // Init ESP-NOW
    if (esp_now_init() != ESP_OK) return false;

    // Register callback
    esp_now_register_recv_cb(onDataRecv);

    _initialized = true;
    _last_stats_millis = millis();

    return true;
}

// Returns true if new packet available, otherwise false
bool ESPNowReceiver::hasNewData() const {
    bool result;
    portENTER_CRITICAL(&s_spinlock);
    result = s_has_new_data;
    portEXIT_CRITICAL(&s_spinlock);
    return result;
}

// Returns the latest data packet received via ESP-NOW
HeadingData ESPNowReceiver::getData() {
    HeadingData data;
    portENTER_CRITICAL(&s_spinlock);
    data = s_latest_data;
    s_has_new_data = false;
    portEXIT_CRITICAL(&s_spinlock);
    return data;
}

// Returns true if timeout not reached, otherwise false
bool ESPNowReceiver::isConnected(uint32_t timeout_ms) const {
    uint32_t last_rx;
    portENTER_CRITICAL(&s_spinlock);
    last_rx = s_last_rx_millis;
    portEXIT_CRITICAL(&s_spinlock);

    if (last_rx == 0) return false;

    return (millis() - last_rx) < timeout_ms;
}

// Update the PPS statistics
void ESPNowReceiver::updateStats() {
    uint32_t now = millis();
    uint32_t elapsed = now - _last_stats_millis;
    if (elapsed < 1000) return;

    uint32_t count;
    portENTER_CRITICAL(&s_spinlock);
    count = s_packet_count;
    portEXIT_CRITICAL(&s_spinlock);

    uint32_t delta_count = count - _last_packet_count;
    _packets_per_second = (float)delta_count * 1000.0f / (float)elapsed;

    _last_packet_count = count;
    _last_stats_millis = now;
}

// Demo mode switch. Both directions wipe every displayed source (timestamps and has-new
// flags), so neither a real value can linger among the demo values nor a demo value pass
// for a real one: the pages show their placeholders until the new source delivers. The
// cached payloads themselves are left as they are — with a zero timestamp no page shows
// them, and the next stored frame overwrites them.
void ESPNowReceiver::setDemoMode(bool on) {
    portENTER_CRITICAL(&s_spinlock);
    if (s_demo_mode != on) {
        s_demo_mode = on;
        s_has_new_data = false;    s_last_rx_millis = 0;
        s_has_new_weather = false; s_last_weather_millis = 0;
        s_has_new_battery = false; s_last_battery_millis = 0;
        s_has_new_gnss = false;    s_last_gnss_millis = 0;
        s_has_new_engine = false;  s_last_engine_millis = 0;
        s_has_new_tank = false;    s_last_tank_millis = 0;
        s_has_new_water = false;   s_last_water_millis = 0;
        s_has_new_depth = false;   s_last_depth_millis = 0;
    }
    portEXIT_CRITICAL(&s_spinlock);
}

uint32_t ESPNowReceiver::radioFrameCount() {
    portENTER_CRITICAL(&s_spinlock);
    const uint32_t n = s_radio_frames;
    portEXIT_CRITICAL(&s_spinlock);
    return n;
}

// A demo frame enters through the very same parser and slots as a radio frame
void ESPNowReceiver::injectFrame(const uint8_t* data, int data_len) {
    dispatch(data, data_len, true);
}

// === P R I V A T E ===

// Callback for data receive of ESP-NOW (WiFi task). Radio frames always go through the
// same parser, demo mode or not, so the radio's cost is identical in both.
void ESPNowReceiver::onDataRecv(const esp_now_recv_info_t* recv_info, const uint8_t* data, int data_len) {
    dispatch(data, data_len, false);
}

// Store a frame's payload in its message type's single slot. No heap, no queue: each type
// has exactly one fixed slot that every new frame overwrites, so nothing received ever
// accumulates — a frame rejected here simply ceases to exist with the caller's buffer.
//
// Source gate (checked under the lock, so a mode switch cannot interleave with a store):
// in demo mode only demo frames are stored; otherwise only radio frames. DATETIME_DELTA
// is exempt — it only disciplines the system clock and is never shown as a field, so the
// real time keeps flowing in demo mode too.
void ESPNowReceiver::dispatch(const uint8_t* data, int data_len, bool from_demo) {

    // Minimum frame size check
    if (data_len < (int)sizeof(ESPNowHeader)) return;

    // Extract and validate header
    ESPNowHeader hdr;
    memcpy(&hdr, data, sizeof(ESPNowHeader));

    if (hdr.magic != ESPNOW_MAGIC) return;

    // Frame integrity: buffer must hold header + declared payload
    if (data_len < (int)(sizeof(ESPNowHeader) + hdr.payload_len)) return;

    // Status screen statistic: every valid radio frame, stored or not (demo mode drops
    // them below, but the radio still received them). Demo frames are not counted.
    if (!from_demo) {
        portENTER_CRITICAL(&s_spinlock);
        s_radio_frames++;
        portEXIT_CRITICAL(&s_spinlock);
    }

    const uint8_t* payload = data + sizeof(ESPNowHeader);

    // Actual handling of the payload content, based on the message type
    switch (static_cast<ESPNowMsgType>(hdr.msg_type)) {

        case ESPNowMsgType::HEADING_DELTA: {
            if (hdr.payload_len != sizeof(HeadingDelta)) return;
            HeadingDelta delta;
            memcpy(&delta, payload, sizeof(HeadingDelta));
            HeadingData converted = convertDeltaToData(delta);
            portENTER_CRITICAL(&s_spinlock);
            if (acceptLocked(from_demo)) {
                s_latest_data    = converted;
                s_has_new_data   = true;
                s_last_rx_millis = millis();
                s_packet_count++;
            }
            portEXIT_CRITICAL(&s_spinlock);
            break;
        }

        case ESPNowMsgType::WEATHER_DELTA: {
            if (hdr.payload_len != sizeof(WeatherDelta)) return;
            WeatherDelta weather;
            memcpy(&weather, payload, sizeof(WeatherDelta));
            portENTER_CRITICAL(&s_spinlock);
            if (acceptLocked(from_demo)) {
                s_latest_weather  = weather;
                s_has_new_weather = true;
                s_last_weather_millis = millis();
            }
            portEXIT_CRITICAL(&s_spinlock);
            break;
        }

        case ESPNowMsgType::BATTERY_DELTA: {
            if (hdr.payload_len != sizeof(BatteryDelta)) return;
            BatteryDelta battery;
            memcpy(&battery, payload, sizeof(BatteryDelta));
            portENTER_CRITICAL(&s_spinlock);
            if (acceptLocked(from_demo)) {
                s_latest_battery  = battery;
                s_has_new_battery = true;
                s_last_battery_millis = millis();
            }
            portEXIT_CRITICAL(&s_spinlock);
            break;
        }

        case ESPNowMsgType::GNSS_DELTA: {
            if (hdr.payload_len != sizeof(GnssDelta)) return;
            GnssDelta gnss;
            memcpy(&gnss, payload, sizeof(GnssDelta));
            GnssData converted = convertGnssDeltaToData(gnss);
            portENTER_CRITICAL(&s_spinlock);
            if (acceptLocked(from_demo)) {
                s_latest_gnss  = converted;
                s_has_new_gnss = true;
                s_last_gnss_millis = millis();
            }
            portEXIT_CRITICAL(&s_spinlock);
            break;
        }

        case ESPNowMsgType::HALMET_ENGINE_DELTA: {
            if (hdr.payload_len != sizeof(HALMETEngineDelta)) return;
            HALMETEngineDelta eng;
            memcpy(&eng, payload, sizeof(HALMETEngineDelta));
            portENTER_CRITICAL(&s_spinlock);
            if (acceptLocked(from_demo)) {
                s_latest_engine  = eng;
                s_has_new_engine = true;
                s_last_engine_millis = millis();
            }
            portEXIT_CRITICAL(&s_spinlock);
            break;
        }

        case ESPNowMsgType::HALMET_TANK_DELTA: {
            if (hdr.payload_len != sizeof(HALMETTankDelta)) return;
            HALMETTankDelta tank;
            memcpy(&tank, payload, sizeof(HALMETTankDelta));
            portENTER_CRITICAL(&s_spinlock);
            if (acceptLocked(from_demo)) {
                s_latest_tank  = tank;
                s_has_new_tank = true;
                s_last_tank_millis = millis();
            }
            portEXIT_CRITICAL(&s_spinlock);
            break;
        }

        case ESPNowMsgType::HALMET_WATER_DELTA: {
            if (hdr.payload_len != sizeof(HALMETWaterDelta)) return;
            HALMETWaterDelta water;
            memcpy(&water, payload, sizeof(HALMETWaterDelta));
            portENTER_CRITICAL(&s_spinlock);
            if (acceptLocked(from_demo)) {
                s_latest_water  = water;
                s_has_new_water = true;
                s_last_water_millis = millis();
            }
            portEXIT_CRITICAL(&s_spinlock);
            break;
        }

        case ESPNowMsgType::DEPTH_DELTA: {
            if (hdr.payload_len != sizeof(DepthDelta)) return;
            DepthDelta depth;
            memcpy(&depth, payload, sizeof(DepthDelta));
            portENTER_CRITICAL(&s_spinlock);
            if (acceptLocked(from_demo)) {
                s_latest_depth  = depth;
                s_has_new_depth = true;
                s_last_depth_millis = millis();
            }
            portEXIT_CRITICAL(&s_spinlock);
            break;
        }

        case ESPNowMsgType::DATETIME_DELTA: {
            if (hdr.payload_len != sizeof(DateTimeDelta)) return;
            DateTimeDelta dt;
            memcpy(&dt, payload, sizeof(DateTimeDelta));
            portENTER_CRITICAL(&s_spinlock);
            s_latest_datetime  = dt;
            s_has_new_datetime = true;
            s_last_datetime_millis = millis();
            portEXIT_CRITICAL(&s_spinlock);
            break;
        }

        default:
            // Unknown msg_type — ignore
            break;
    }
}

// Returns true if new weather data packet available, otherwise false
bool ESPNowReceiver::hasNewWeatherData() const {
    bool result;
    portENTER_CRITICAL(&s_spinlock);
    result = s_has_new_weather;
    portEXIT_CRITICAL(&s_spinlock);
    return result;
}

// Returns the latest weather data packet received via ESP-NOW
WeatherDelta ESPNowReceiver::getWeatherData() {
    WeatherDelta data;
    portENTER_CRITICAL(&s_spinlock);
    data = s_latest_weather;
    s_has_new_weather = false;
    portEXIT_CRITICAL(&s_spinlock);
    return data;
}

// Returns true if new battery data packet available, otherwise false
bool ESPNowReceiver::hasNewBatteryData() const {
    bool result;
    portENTER_CRITICAL(&s_spinlock);
    result = s_has_new_battery;
    portEXIT_CRITICAL(&s_spinlock);
    return result;
}

// Returns the latest battery data packet received via ESP-NOW
BatteryDelta ESPNowReceiver::getBatteryData() {
    BatteryDelta data;
    portENTER_CRITICAL(&s_spinlock);
    data = s_latest_battery;
    s_has_new_battery = false;
    portEXIT_CRITICAL(&s_spinlock);
    return data;
}

// Returns true if new GNSS data packet available, otherwise false
bool ESPNowReceiver::hasNewGnssData() const {
    bool result;
    portENTER_CRITICAL(&s_spinlock);
    result = s_has_new_gnss;
    portEXIT_CRITICAL(&s_spinlock);
    return result;
}

// Returns the latest GNSS data packet received via ESP-NOW
GnssData ESPNowReceiver::getGnssData() {
    GnssData data;
    portENTER_CRITICAL(&s_spinlock);
    data = s_latest_gnss;
    s_has_new_gnss = false;
    portEXIT_CRITICAL(&s_spinlock);
    return data;
}

// Returns true if new HALMET engine data packet available, otherwise false
bool ESPNowReceiver::hasNewEngineData() const {
    bool result;
    portENTER_CRITICAL(&s_spinlock);
    result = s_has_new_engine;
    portEXIT_CRITICAL(&s_spinlock);
    return result;
}

// Returns the latest HALMET engine data packet received via ESP-NOW
HALMETEngineDelta ESPNowReceiver::getEngineData() {
    HALMETEngineDelta data;
    portENTER_CRITICAL(&s_spinlock);
    data = s_latest_engine;
    s_has_new_engine = false;
    portEXIT_CRITICAL(&s_spinlock);
    return data;
}

// Returns true if new HALMET tank data packet available, otherwise false
bool ESPNowReceiver::hasNewTankData() const {
    bool result;
    portENTER_CRITICAL(&s_spinlock);
    result = s_has_new_tank;
    portEXIT_CRITICAL(&s_spinlock);
    return result;
}

// Returns the latest HALMET tank data packet received via ESP-NOW
HALMETTankDelta ESPNowReceiver::getTankData() {
    HALMETTankDelta data;
    portENTER_CRITICAL(&s_spinlock);
    data = s_latest_tank;
    s_has_new_tank = false;
    portEXIT_CRITICAL(&s_spinlock);
    return data;
}

// Returns true if new HALMET fresh water data packet available, otherwise false
bool ESPNowReceiver::hasNewWaterData() const {
    bool result;
    portENTER_CRITICAL(&s_spinlock);
    result = s_has_new_water;
    portEXIT_CRITICAL(&s_spinlock);
    return result;
}

// Returns the latest HALMET fresh water data packet received via ESP-NOW
HALMETWaterDelta ESPNowReceiver::getWaterData() {
    HALMETWaterDelta data;
    portENTER_CRITICAL(&s_spinlock);
    data = s_latest_water;
    s_has_new_water = false;
    portEXIT_CRITICAL(&s_spinlock);
    return data;
}

// Returns true if new depth data packet available, otherwise false
bool ESPNowReceiver::hasNewDepthData() const {
    bool result;
    portENTER_CRITICAL(&s_spinlock);
    result = s_has_new_depth;
    portEXIT_CRITICAL(&s_spinlock);
    return result;
}

// Returns the latest depth data packet received via ESP-NOW
DepthDelta ESPNowReceiver::getDepthData() {
    DepthDelta data;
    portENTER_CRITICAL(&s_spinlock);
    data = s_latest_depth;
    s_has_new_depth = false;
    portEXIT_CRITICAL(&s_spinlock);
    return data;
}

// Returns true if new GNSS datetime packet available, otherwise false
bool ESPNowReceiver::hasNewDatetimeData() const {
    bool result;
    portENTER_CRITICAL(&s_spinlock);
    result = s_has_new_datetime;
    portEXIT_CRITICAL(&s_spinlock);
    return result;
}

// Returns the latest GNSS datetime packet received via ESP-NOW
DateTimeDelta ESPNowReceiver::getDatetimeData() {
    DateTimeDelta data;
    portENTER_CRITICAL(&s_spinlock);
    data = s_latest_datetime;
    s_has_new_datetime = false;
    portEXIT_CRITICAL(&s_spinlock);
    return data;
}

// Per-source last-receive timestamps (millis, 0 = never). Read under spinlock.
uint32_t ESPNowReceiver::lastHeadingRxMillis() const {
    uint32_t v; portENTER_CRITICAL(&s_spinlock); v = s_last_rx_millis;      portEXIT_CRITICAL(&s_spinlock); return v;
}
uint32_t ESPNowReceiver::lastWeatherRxMillis() const {
    uint32_t v; portENTER_CRITICAL(&s_spinlock); v = s_last_weather_millis; portEXIT_CRITICAL(&s_spinlock); return v;
}
uint32_t ESPNowReceiver::lastBatteryRxMillis() const {
    uint32_t v; portENTER_CRITICAL(&s_spinlock); v = s_last_battery_millis; portEXIT_CRITICAL(&s_spinlock); return v;
}
uint32_t ESPNowReceiver::lastGnssRxMillis() const {
    uint32_t v; portENTER_CRITICAL(&s_spinlock); v = s_last_gnss_millis;    portEXIT_CRITICAL(&s_spinlock); return v;
}
uint32_t ESPNowReceiver::lastEngineRxMillis() const {
    uint32_t v; portENTER_CRITICAL(&s_spinlock); v = s_last_engine_millis;  portEXIT_CRITICAL(&s_spinlock); return v;
}
uint32_t ESPNowReceiver::lastTankRxMillis() const {
    uint32_t v; portENTER_CRITICAL(&s_spinlock); v = s_last_tank_millis;    portEXIT_CRITICAL(&s_spinlock); return v;
}
uint32_t ESPNowReceiver::lastWaterRxMillis() const {
    uint32_t v; portENTER_CRITICAL(&s_spinlock); v = s_last_water_millis;   portEXIT_CRITICAL(&s_spinlock); return v;
}
uint32_t ESPNowReceiver::lastDepthRxMillis() const {
    uint32_t v; portENTER_CRITICAL(&s_spinlock); v = s_last_depth_millis;   portEXIT_CRITICAL(&s_spinlock); return v;
}
uint32_t ESPNowReceiver::lastDatetimeRxMillis() const {
    uint32_t v; portENTER_CRITICAL(&s_spinlock); v = s_last_datetime_millis; portEXIT_CRITICAL(&s_spinlock); return v;
}

