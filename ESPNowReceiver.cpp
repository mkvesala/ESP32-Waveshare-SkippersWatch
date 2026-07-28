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

// === P R I V A T E ===

// Callback for data receive of ESP-NOW
void ESPNowReceiver::onDataRecv(const esp_now_recv_info_t* recv_info, const uint8_t* data, int data_len) {

    // Minimum frame size check
    if (data_len < (int)sizeof(ESPNowHeader)) return;

    // Extract and validate header
    ESPNowHeader hdr;
    memcpy(&hdr, data, sizeof(ESPNowHeader));

    if (hdr.magic != ESPNOW_MAGIC) return;

    // Frame integrity: buffer must hold header + declared payload
    if (data_len < (int)(sizeof(ESPNowHeader) + hdr.payload_len)) return;

    const uint8_t* payload = data + sizeof(ESPNowHeader);

    // Actual handling of the payload content, based on the message type
    switch (static_cast<ESPNowMsgType>(hdr.msg_type)) {

        case ESPNowMsgType::HEADING_DELTA: {
            if (hdr.payload_len != sizeof(HeadingDelta)) return;
            HeadingDelta delta;
            memcpy(&delta, payload, sizeof(HeadingDelta));
            HeadingData converted = convertDeltaToData(delta);
            portENTER_CRITICAL(&s_spinlock);
            s_latest_data    = converted;
            s_has_new_data   = true;
            s_last_rx_millis = millis();
            s_packet_count++;
            if (s_min_pitch_x10 == MINMAX_SENTINEL || converted.pitch_x10 < s_min_pitch_x10) s_min_pitch_x10 = converted.pitch_x10;
            if (s_max_pitch_x10 == MINMAX_SENTINEL || converted.pitch_x10 > s_max_pitch_x10) s_max_pitch_x10 = converted.pitch_x10;
            if (s_min_roll_x10  == MINMAX_SENTINEL || converted.roll_x10  < s_min_roll_x10)  s_min_roll_x10  = converted.roll_x10;
            if (s_max_roll_x10  == MINMAX_SENTINEL || converted.roll_x10  > s_max_roll_x10)  s_max_roll_x10  = converted.roll_x10;
            portEXIT_CRITICAL(&s_spinlock);
            break;
        }

        case ESPNowMsgType::WEATHER_DELTA: {
            if (hdr.payload_len != sizeof(WeatherDelta)) return;
            WeatherDelta weather;
            memcpy(&weather, payload, sizeof(WeatherDelta));
            portENTER_CRITICAL(&s_spinlock);
            s_latest_weather  = weather;
            s_has_new_weather = true;
            s_last_weather_millis = millis();
            portEXIT_CRITICAL(&s_spinlock);
            break;
        }

        case ESPNowMsgType::BATTERY_DELTA: {
            if (hdr.payload_len != sizeof(BatteryDelta)) return;
            BatteryDelta battery;
            memcpy(&battery, payload, sizeof(BatteryDelta));
            portENTER_CRITICAL(&s_spinlock);
            s_latest_battery  = battery;
            s_has_new_battery = true;
            s_last_battery_millis = millis();
            portEXIT_CRITICAL(&s_spinlock);
            break;
        }

        case ESPNowMsgType::GNSS_DELTA: {
            if (hdr.payload_len != sizeof(GnssDelta)) return;
            GnssDelta gnss;
            memcpy(&gnss, payload, sizeof(GnssDelta));
            GnssData converted = convertGnssDeltaToData(gnss);
            portENTER_CRITICAL(&s_spinlock);
            s_latest_gnss  = converted;
            s_has_new_gnss = true;
            s_last_gnss_millis = millis();
            portEXIT_CRITICAL(&s_spinlock);
            break;
        }

        case ESPNowMsgType::HALMET_ENGINE_DELTA: {
            if (hdr.payload_len != sizeof(HALMETEngineDelta)) return;
            HALMETEngineDelta eng;
            memcpy(&eng, payload, sizeof(HALMETEngineDelta));
            portENTER_CRITICAL(&s_spinlock);
            s_latest_engine  = eng;
            s_has_new_engine = true;
            s_last_engine_millis = millis();
            portEXIT_CRITICAL(&s_spinlock);
            break;
        }

        case ESPNowMsgType::HALMET_TANK_DELTA: {
            if (hdr.payload_len != sizeof(HALMETTankDelta)) return;
            HALMETTankDelta tank;
            memcpy(&tank, payload, sizeof(HALMETTankDelta));
            portENTER_CRITICAL(&s_spinlock);
            s_latest_tank  = tank;
            s_has_new_tank = true;
            s_last_tank_millis = millis();
            portEXIT_CRITICAL(&s_spinlock);
            break;
        }

        case ESPNowMsgType::HALMET_WATER_DELTA: {
            if (hdr.payload_len != sizeof(HALMETWaterDelta)) return;
            HALMETWaterDelta water;
            memcpy(&water, payload, sizeof(HALMETWaterDelta));
            portENTER_CRITICAL(&s_spinlock);
            s_latest_water  = water;
            s_has_new_water = true;
            s_last_water_millis = millis();
            portEXIT_CRITICAL(&s_spinlock);
            break;
        }

        case ESPNowMsgType::DEPTH_DELTA: {
            if (hdr.payload_len != sizeof(DepthDelta)) return;
            DepthDelta depth;
            memcpy(&depth, payload, sizeof(DepthDelta));
            portENTER_CRITICAL(&s_spinlock);
            s_latest_depth  = depth;
            s_has_new_depth = true;
            s_last_depth_millis = millis();
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

// Lifetime min/max pitch/roll getters — tracked across all screens since boot
int16_t ESPNowReceiver::getMinPitch_x10() {
    int16_t v; portENTER_CRITICAL(&s_spinlock); v = s_min_pitch_x10; portEXIT_CRITICAL(&s_spinlock); return v;
}
int16_t ESPNowReceiver::getMaxPitch_x10() {
    int16_t v; portENTER_CRITICAL(&s_spinlock); v = s_max_pitch_x10; portEXIT_CRITICAL(&s_spinlock); return v;
}
int16_t ESPNowReceiver::getMinRoll_x10() {
    int16_t v; portENTER_CRITICAL(&s_spinlock); v = s_min_roll_x10; portEXIT_CRITICAL(&s_spinlock); return v;
}
int16_t ESPNowReceiver::getMaxRoll_x10() {
    int16_t v; portENTER_CRITICAL(&s_spinlock); v = s_max_roll_x10; portEXIT_CRITICAL(&s_spinlock); return v;
}
bool ESPNowReceiver::hasMinMaxData() {
    bool v; portENTER_CRITICAL(&s_spinlock); v = (s_min_pitch_x10 != MINMAX_SENTINEL); portEXIT_CRITICAL(&s_spinlock); return v;
}
void ESPNowReceiver::resetMinMax() {
    portENTER_CRITICAL(&s_spinlock);
    s_min_pitch_x10 = MINMAX_SENTINEL;
    s_max_pitch_x10 = MINMAX_SENTINEL;
    s_min_roll_x10  = MINMAX_SENTINEL;
    s_max_roll_x10  = MINMAX_SENTINEL;
    portEXIT_CRITICAL(&s_spinlock);
}
