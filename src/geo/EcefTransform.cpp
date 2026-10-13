#include "EcefTransform.hpp"

#include <cmath>

namespace geo {

namespace {
constexpr double kDeg2Rad = 3.14159265358979323846 / 180.0;
constexpr double kRad2Deg = 180.0 / 3.14159265358979323846;
}  // namespace

void EcefToGeodeticWgs84(double x, double y, double z,
                          double* latDegOut, double* lonDegOut, double* heightMOut) {
    const double a = kWgs84SemiMajorM;
    const double f = kWgs84Flattening;
    const double e2 = f * (2.0 - f);

    const double lon = std::atan2(y, x);
    const double p = std::sqrt(x * x + y * y);

    // Iterative solution (Bowring-style initial guess, then Newton refinement).
    double lat = std::atan2(z, p * (1.0 - e2));
    for (int i = 0; i < 8; ++i) {
        const double sinLat = std::sin(lat);
        const double n = a / std::sqrt(1.0 - e2 * sinLat * sinLat);
        const double h = (p > 1e-12) ? (p / std::cos(lat) - n) : (std::abs(z) - n * (1.0 - e2));
        const double newLat = std::atan2(z, p * (1.0 - e2 * n / (n + h)));
        if (std::abs(newLat - lat) < 1e-14) { lat = newLat; break; }
        lat = newLat;
    }

    const double sinLat = std::sin(lat);
    const double n = a / std::sqrt(1.0 - e2 * sinLat * sinLat);
    const double height = (p > 1e-12) ? (p / std::cos(lat) - n) : (std::abs(z) - n * (1.0 - e2));

    *latDegOut = lat * kRad2Deg;
    *lonDegOut = lon * kRad2Deg;
    *heightMOut = height;
}

void GeodeticToEcefWgs84(double latDeg, double lonDeg, double heightM,
                          double* xOut, double* yOut, double* zOut) {
    const double a = kWgs84SemiMajorM;
    const double f = kWgs84Flattening;
    const double e2 = f * (2.0 - f);

    const double lat = latDeg * kDeg2Rad;
    const double lon = lonDeg * kDeg2Rad;
    const double sinLat = std::sin(lat);
    const double cosLat = std::cos(lat);
    const double n = a / std::sqrt(1.0 - e2 * sinLat * sinLat);

    *xOut = (n + heightM) * cosLat * std::cos(lon);
    *yOut = (n + heightM) * cosLat * std::sin(lon);
    *zOut = (n * (1.0 - e2) + heightM) * sinLat;
}

void EcefDeltaToEnu(double dx, double dy, double dz,
                     double originLatDeg, double originLonDeg,
                     double* eastMOut, double* northMOut, double* upMOut) {
    const double lat = originLatDeg * kDeg2Rad;
    const double lon = originLonDeg * kDeg2Rad;
    const double sinLat = std::sin(lat), cosLat = std::cos(lat);
    const double sinLon = std::sin(lon), cosLon = std::cos(lon);

    *eastMOut  = -sinLon * dx + cosLon * dy;
    *northMOut = -sinLat * cosLon * dx - sinLat * sinLon * dy + cosLat * dz;
    *upMOut    =  cosLat * cosLon * dx + cosLat * sinLon * dy + sinLat * dz;
}

}  // namespace geo
