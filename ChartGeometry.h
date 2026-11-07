#pragma once

#include <cmath>

// === C H A R T  G E O M E T R Y ===
//
// - Pure geometry of the sea chart page, no Arduino or LVGL, so host unit tests can run it
//   against pyproj reference values
// - WGS84 → ETRS-TM35FIN (EPSG:3067): Krüger series, GRS80, central meridian 27°E, k0 0.9996,
//   false easting 500 000 m. ETRS89 is taken as WGS84, the difference is well below a pixel.
// - The chart is grid north up, not true north up. convergenceDeg() gives the difference
//   (about -3.5° at Hanko), and gridBearingDeg() turns a true HDG or COG into the angle
//   that lines up with the chart.
// - Double precision is software emulated on the ESP32-S3. Fine here: one conversion per
//   position update.

struct ChartGeometry {

    static constexpr double DEG_TO_RAD_D = 0.017453292519943295;
    static constexpr double RAD_TO_DEG_D = 57.29577951308232;

    static constexpr double METERS_PER_NM   = 1852.0;
    static constexpr double RING_RADIUS_NM  = 0.25;   // fixed range ring, doubles as the scale
    static constexpr double COG_VECTOR_MIN  = 3.0;    // COG vector length in minutes of travel

    struct GridPoint {
        double e;
        double n;
    };

    struct ScreenPoint {
        float x;
        float y;
    };

    static GridPoint wgs84ToTm35(double lat_deg, double lon_deg) {
        constexpr double A_AXIS = 6378137.0;
        constexpr double F      = 1.0 / 298.257222101;   // GRS80
        constexpr double K0     = 0.9996;
        constexpr double FE     = 500000.0;
        constexpr double LON0   = 27.0;

        constexpr double N  = F / (2.0 - F);
        constexpr double N2 = N * N;
        constexpr double N3 = N2 * N;
        constexpr double N4 = N3 * N;
        constexpr double RECT = A_AXIS / (1.0 + N) * (1.0 + N2 / 4.0 + N4 / 64.0);
        constexpr double ALPHA[4] = {
            N / 2.0 - 2.0 * N2 / 3.0 + 5.0 * N3 / 16.0 + 41.0 * N4 / 180.0,
            13.0 * N2 / 48.0 - 3.0 * N3 / 5.0 + 557.0 * N4 / 1440.0,
            61.0 * N3 / 240.0 - 103.0 * N4 / 140.0,
            49561.0 * N4 / 161280.0,
        };

        const double lat   = lat_deg * DEG_TO_RAD_D;
        const double dlon  = (lon_deg - LON0) * DEG_TO_RAD_D;
        const double e2n   = 2.0 * std::sqrt(N) / (1.0 + N);
        const double sin_lat = std::sin(lat);
        const double t = std::sinh(std::atanh(sin_lat) - e2n * std::atanh(e2n * sin_lat));
        const double xi0  = std::atan2(t, std::cos(dlon));
        const double eta0 = std::atanh(std::sin(dlon) / std::sqrt(1.0 + t * t));

        double xi = xi0;
        double eta = eta0;
        for (int j = 1; j <= 4; ++j) {
            const double a = 2.0 * j;
            xi  += ALPHA[j - 1] * std::sin(a * xi0) * std::cosh(a * eta0);
            eta += ALPHA[j - 1] * std::cos(a * xi0) * std::sinh(a * eta0);
        }
        return { FE + K0 * RECT * eta, K0 * RECT * xi };
    }

    // Meridian convergence in degrees: negative west of 27°E
    static double convergenceDeg(double lat_deg, double lon_deg) {
        const double dlon = (lon_deg - 27.0) * DEG_TO_RAD_D;
        return std::atan(std::tan(dlon) * std::sin(lat_deg * DEG_TO_RAD_D)) * RAD_TO_DEG_D;
    }

    // True bearing → bearing on the chart, 0..360
    static double gridBearingDeg(double true_deg, double convergence_deg) {
        const double b = std::fmod(true_deg - convergence_deg, 360.0);
        return b < 0.0 ? b + 360.0 : b;
    }

    static float ringRadiusPx(double m_per_px) {
        return (float)(RING_RADIUS_NM * METERS_PER_NM / m_per_px);
    }

    static float cogVectorPx(double sog_knots, double m_per_px) {
        return (float)(sog_knots * METERS_PER_NM * COG_VECTOR_MIN / 60.0 / m_per_px);
    }

    // End point of a line from (x, y): bearing 0 is up on the screen, 90 is right
    static ScreenPoint lineEnd(float x, float y, double bearing_deg, float length_px) {
        const double b = bearing_deg * DEG_TO_RAD_D;
        return { x + length_px * (float)std::sin(b), y - length_px * (float)std::cos(b) };
    }

    // --- Bow-up ---
    // A rotated view has grid bearing rot_deg pointing up on the screen. Offsets are in
    // pixels, x to the right and y down, from the point the view is rotated about.

    // Shortest signed difference a - b, in -180..180
    static double headingDelta(double a_deg, double b_deg) {
        double d = std::fmod(a_deg - b_deg, 360.0);
        if (d > 180.0) d -= 360.0;
        if (d <= -180.0) d += 360.0;
        return d;
    }

    // A grid bearing as seen on a view rotated by rot_deg, 0..360
    static double screenBearing(double grid_deg, double rot_deg) {
        const double b = std::fmod(grid_deg - rot_deg, 360.0);
        return b < 0.0 ? b + 360.0 : b;
    }

    // A chart pixel offset (east right, south down) → its offset on a view rotated by rot_deg
    static ScreenPoint rotateOffset(double dx, double dy, double rot_deg) {
        const double r = rot_deg * DEG_TO_RAD_D;
        const double c = std::cos(r);
        const double s = std::sin(r);
        return { (float)(dx * c + dy * s), (float)(-dx * s + dy * c) };
    }

};
