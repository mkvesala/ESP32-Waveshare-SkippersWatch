#include "DemoSource.h"

namespace {
    constexpr float DEG_TO_RAD_F = (float)M_PI / 180.0f;
    constexpr float KN_TO_MS     = 1.0f / 1.94384f;
}

// === P U B L I C ===

void DemoSource::update(uint32_t now) {
    if (_sent_once && (now - _last_send_ms) < SEND_INTERVAL_MS) return;
    this->sendAll(now);
}

void DemoSource::sendAll(uint32_t now) {
    _last_send_ms = now;
    _sent_once = true;
    const DemoSnapshot d = DemoValues::at(now);

    HeadingDelta hdg;
    hdg.heading_true_rad = d.heading_true_deg * DEG_TO_RAD_F;
    hdg.heading_rad      = DemoValues::wrap360(d.heading_true_deg - d.mag_var_deg) * DEG_TO_RAD_F;
    hdg.pitch_rad        = d.pitch_deg * DEG_TO_RAD_F;
    hdg.roll_rad         = d.roll_deg * DEG_TO_RAD_F;
    this->send(ESPNowMsgType::HEADING_DELTA, hdg);

    GnssDelta gnss;
    gnss.lat_deg      = d.lat_deg;
    gnss.lon_deg      = d.lon_deg;
    gnss.sog_ms       = d.sog_kn * KN_TO_MS;
    gnss.cog_true_rad = d.cog_true_deg * DEG_TO_RAD_F;
    gnss.mag_var_rad  = d.mag_var_deg * DEG_TO_RAD_F;
    gnss.satellites   = 12;
    gnss.fix_type     = 3;
    gnss.fix_ok       = 1;
    gnss.reserved     = 0;
    this->send(ESPNowMsgType::GNSS_DELTA, gnss);

    DepthDelta depth;
    depth.below_surface_m    = d.depth_below_surface_m;
    depth.below_transducer_m = d.depth_below_surface_m - 0.5f;
    depth.below_keel_m       = d.depth_below_surface_m - 1.7f;
    depth.age_ms             = 0;
    this->send(ESPNowMsgType::DEPTH_DELTA, depth);

    BatteryDelta bat;
    bat.house_voltage = d.house_v;
    bat.house_current = d.house_a;
    bat.house_power   = d.house_v * d.house_a;
    bat.house_soc     = d.house_soc_pct;
    bat.start_voltage = d.start_v;
    this->send(ESPNowMsgType::BATTERY_DELTA, bat);

    WeatherDelta wx;
    wx.temperature_c = d.temperature_c;
    wx.humidity_p    = d.humidity_pct;
    wx.pressure_hpa  = d.pressure_hpa;
    this->send(ESPNowMsgType::WEATHER_DELTA, wx);

    HALMETEngineDelta eng;
    eng.exhaust_temp_k = d.exhaust_k;
    this->send(ESPNowMsgType::HALMET_ENGINE_DELTA, eng);

    HALMETTankDelta tank;
    tank.fuel_level_ratio = d.fuel_ratio;
    this->send(ESPNowMsgType::HALMET_TANK_DELTA, tank);

    HALMETWaterDelta water;
    water.water_level_ratio = d.water_ratio;
    this->send(ESPNowMsgType::HALMET_WATER_DELTA, water);
}

// === P R I V A T E ===

// One frame on the stack, exactly as a sender lays it out on the air
template <typename T>
void DemoSource::send(ESPNowMsgType type, const T& payload) {
    ESPNowPacket<T> pkt;
    initHeader(pkt.hdr, type, sizeof(T));
    pkt.payload = payload;
    _receiver.injectFrame(reinterpret_cast<const uint8_t*>(&pkt), sizeof(pkt));
}
