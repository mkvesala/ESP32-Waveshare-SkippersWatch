#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include "espnow_protocol.h"

using namespace ESPNow;

// === C L A S S  E S P N O W R E C E I V E R ===
//
// - Class ESPNowReceiver - responsible for ESP-NOW inbound/outbound communication
//
// - Provides public API to manage incoming instrument data (receive-only)
// - Receives: HEADING_DELTA (compass/attitude), WEATHER_DELTA (weather sensor), BATTERY_DELTA,
//   GNSS_DELTA, HALMET_ENGINE_DELTA, HALMET_TANK_DELTA, HALMET_WATER_DELTA, DEPTH_DELTA
// - Init: _receiver.begin(channel)
// - Owned by: WatchApplication

class ESPNowReceiver {

public:
    
    explicit ESPNowReceiver();

    bool begin(uint8_t channel = 6);
    bool hasNewData() const;
    HeadingData getData();
    bool isConnected(uint32_t timeout_ms = 500) const;
    void updateStats();
    bool hasNewWeatherData() const;
    bool hasNewBatteryData() const;
    WeatherDelta getWeatherData();
    BatteryDelta getBatteryData();
    bool hasNewGnssData() const;
    GnssData getGnssData();

    bool hasNewEngineData() const;
    HALMETEngineDelta getEngineData();
    bool hasNewTankData() const;
    HALMETTankDelta getTankData();
    bool hasNewWaterData() const;
    HALMETWaterDelta getWaterData();
    bool hasNewDepthData() const;
    DepthDelta getDepthData();
    bool hasNewDatetimeData() const;
    DateTimeDelta getDatetimeData();

    // Per-source last-receive timestamps (millis). 0 = never received.
    // Authoritative freshness — lets the UI (and PowerManager) age out a value even
    // when a stale has-new flag is still pending, upholding the never-show-stale rule.
    uint32_t lastHeadingRxMillis() const;
    uint32_t lastWeatherRxMillis() const;
    uint32_t lastBatteryRxMillis() const;
    uint32_t lastGnssRxMillis() const;
    uint32_t lastEngineRxMillis() const;
    uint32_t lastTankRxMillis() const;
    uint32_t lastWaterRxMillis() const;
    uint32_t lastDepthRxMillis() const;
    uint32_t lastDatetimeRxMillis() const;

    float getPacketsPerSecond() const { return _packets_per_second; }

    // Demo mode: the radio keeps listening and parsing exactly as normal (same power
    // profile), but radio frames are no longer stored — only frames handed in through
    // injectFrame() are. Switching either way clears every source, so pages fall back to
    // their placeholders until the new source delivers.
    void setDemoMode(bool on);
    void injectFrame(const uint8_t* data, int data_len);

    // Valid radio frames received during this wake (demo frames excluded). Static: the
    // receiver's state is static, and PowerManager reads it at sleep entry.
    static uint32_t radioFrameCount();

private:

    // Static callback for ESP-NOW
    static void onDataRecv(const esp_now_recv_info_t* recv_info, const uint8_t* data, int data_len);

    // Shared frame parser for radio and demo frames (see the source gate in the .cpp)
    static void dispatch(const uint8_t* data, int data_len, bool from_demo);

    // Caller holds s_spinlock. True if a frame from this source may be stored now.
    static bool acceptLocked(bool from_demo) { return from_demo == s_demo_mode; }
    inline static volatile bool s_demo_mode = false;
    inline static uint32_t s_radio_frames = 0;

    // Static data storage for ESP-NOW
    inline static portMUX_TYPE s_spinlock = portMUX_INITIALIZER_UNLOCKED;
    inline static HeadingData s_latest_data = {};
    inline static volatile bool s_has_new_data = false;
    inline static volatile uint32_t s_last_rx_millis = 0;
    inline static volatile uint32_t s_packet_count = 0;

    // Static variables for weather data handling
    inline static WeatherDelta s_latest_weather = {};
    inline static volatile bool s_has_new_weather = false;
    inline static volatile uint32_t s_last_weather_millis = 0;

    // Static variables for battery data handling
    inline static BatteryDelta s_latest_battery = {};
    inline static volatile bool s_has_new_battery = false;
    inline static volatile uint32_t s_last_battery_millis = 0;

    // Static variables for GNSS data handling
    inline static GnssData s_latest_gnss = {};
    inline static volatile bool s_has_new_gnss = false;
    inline static volatile uint32_t s_last_gnss_millis = 0;

    // Static variables for HALMET engine data handling
    inline static HALMETEngineDelta s_latest_engine = {};
    inline static volatile bool     s_has_new_engine = false;
    inline static volatile uint32_t  s_last_engine_millis = 0;

    // Static variables for HALMET tank data handling
    inline static HALMETTankDelta s_latest_tank = {};
    inline static volatile bool   s_has_new_tank = false;
    inline static volatile uint32_t s_last_tank_millis = 0;

    // Static variables for HALMET fresh water tank data handling
    inline static HALMETWaterDelta s_latest_water = {};
    inline static volatile bool     s_has_new_water = false;
    inline static volatile uint32_t  s_last_water_millis = 0;

    // Static variables for depth data handling. NOTE: depth is relayed, so freshness is
    // two-fold — s_last_depth_millis is ESP-NOW gateway liveness; DepthDelta.age_ms inside
    // the payload is the underlying N2K sounder feed liveness. The UI must gate on both.
    inline static DepthDelta s_latest_depth = {};
    inline static volatile bool     s_has_new_depth = false;
    inline static volatile uint32_t  s_last_depth_millis = 0;

    // Static variables for GNSS UTC date/time handling (drives the system clock)
    inline static DateTimeDelta s_latest_datetime = {};
    inline static volatile bool     s_has_new_datetime = false;
    inline static volatile uint32_t  s_last_datetime_millis = 0;

    // Instance data
    uint8_t _channel = 6;
    float _packets_per_second = 0.0f;
    uint32_t _last_stats_millis = 0;
    uint32_t _last_packet_count = 0;
    bool _initialized = false;
    
};

