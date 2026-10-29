#pragma once

#include <Arduino.h>
#include "ESPNowReceiver.h"

// === C L A S S  L A S T P O S I T I O N ===
//
// - Last known GNSS position for the Celestial page, used only when no live fix is available.
//   Never shown as the boat's position elsewhere (Navigation keeps showing "---").
// - Two sources, the newer one wins:
//   - session: the latest fresh fix seen during this awake session (RAM, millis based)
//   - stored:  the fix persisted to NVS on the way into the previous deep sleep (UTC based)
// - Usable up to CelestialFormat::STORED_POS_MAX_AGE_S (1 h). An expired stored fix is removed
//   from NVS on the next sleep entry.
// - Flash is touched only lazily (first lookup() of a wake reads) and in the pre-sleep flush
//   (the only writer), as with the last-page store.
// - Demo positions are never recorded or stored.
// - Owned by: WatchApplication; used by: CelestialUI

class LastPosition {

public:

    struct Fix {
        double  lat_deg = 0.0;
        double  lon_deg = 0.0;
        int64_t age_s   = 0;
    };

    void setDemo(bool on);

    // Remember the fix if it is fresh. Every UI tick on any page — RAM only.
    void observe(const GnssData& gnss, uint32_t rx_ms, uint32_t now);

    // Newest usable fix at utc_now (UTC s since 1970, trusted), or false if none is ≤ 1 h old
    bool lookup(double utc_now, uint32_t now, Fix& out);

    // Pre-sleep flush: persist the session fix, or remove an expired stored one
    void saveOnSleep();

private:

    struct Record {
        uint32_t magic;
        double   lat_deg;
        double   lon_deg;
        int64_t  utc_s;
    };

    static constexpr uint32_t RECORD_MAGIC  = 0x4C504F31;   // "LPO1"
    static constexpr const char* NVS_NAMESPACE = "watch";   // shared with WatchApplication's last page
    static constexpr const char* NVS_KEY       = "lastpos";
    static constexpr uint32_t FIX_TIMEOUT_MS   = 6000;      // as CelestialUI / NavigationUI
    static constexpr int MIN_VALID_YEAR        = 2020;      // system clock not yet GNSS-set reads ~1970

    bool _demo = false;

    bool     _have_session = false;
    double   _session_lat = 0.0;
    double   _session_lon = 0.0;
    uint32_t _session_rx_ms = 0;

    bool   _loaded = false;
    bool   _have_stored = false;
    Record _stored = {};

    void load();
    static bool systemUtc(double& utc);

};
