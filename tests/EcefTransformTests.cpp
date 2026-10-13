// WGS84 ECEF <-> geodetic + local ENU rotation (GitHub issue #847). Every case
// checks a value a hand calculation (or a known reference point) can confirm.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "geo/EcefTransform.hpp"

#include <cmath>

using Catch::Approx;

TEST_CASE("EcefToGeodeticWgs84: the equator/prime-meridian surface point", "[ecef][REQ-416]") {
    // At lat=0, lon=0, h=0 the ECEF point is exactly (a, 0, 0).
    double lat = 0.0, lon = 0.0, h = 0.0;
    geo::EcefToGeodeticWgs84(geo::kWgs84SemiMajorM, 0.0, 0.0, &lat, &lon, &h);
    CHECK(lat == Approx(0.0).margin(1e-9));
    CHECK(lon == Approx(0.0).margin(1e-9));
    CHECK(h == Approx(0.0).margin(1e-6));
}

TEST_CASE("EcefToGeodeticWgs84: the north pole", "[ecef][REQ-416]") {
    const double b = geo::kWgs84SemiMajorM * (1.0 - geo::kWgs84Flattening);
    double lat = 0.0, lon = 0.0, h = 0.0;
    geo::EcefToGeodeticWgs84(0.0, 0.0, b, &lat, &lon, &h);
    CHECK(lat == Approx(90.0).margin(1e-6));
    CHECK(h == Approx(0.0).margin(1e-5));
}

TEST_CASE("GeodeticToEcefWgs84 / EcefToGeodeticWgs84 round-trip", "[ecef][REQ-416]") {
    const double latIn = 37.421;
    const double lonIn = -122.084;
    const double hIn = 123.45;
    double x = 0.0, y = 0.0, z = 0.0;
    geo::GeodeticToEcefWgs84(latIn, lonIn, hIn, &x, &y, &z);

    double latOut = 0.0, lonOut = 0.0, hOut = 0.0;
    geo::EcefToGeodeticWgs84(x, y, z, &latOut, &lonOut, &hOut);
    CHECK(latOut == Approx(latIn).margin(1e-9));
    CHECK(lonOut == Approx(lonIn).margin(1e-9));
    CHECK(hOut == Approx(hIn).margin(1e-6));
}

TEST_CASE("GeodeticToEcefWgs84: a known reference point", "[ecef][REQ-416]") {
    // WGS84, lat=45, lon=0, h=0 — a simple symmetric case: y=0 by symmetry,
    // and x/z follow directly from the ellipsoid's radius-of-curvature formula.
    double x = 0.0, y = 0.0, z = 0.0;
    geo::GeodeticToEcefWgs84(45.0, 0.0, 0.0, &x, &y, &z);
    CHECK(y == Approx(0.0).margin(1e-6));
    CHECK(x > 0.0);
    CHECK(z > 0.0);
    // x and z are not equal at lat=45 on an oblate ellipsoid (z is slightly
    // smaller than x), unlike on a sphere.
    CHECK(z < x);
}

TEST_CASE("EcefDeltaToEnu: at the equator/prime meridian, ENU axes align with ECEF Y/Z/X",
          "[ecef][REQ-416]") {
    // At lat=0, lon=0: East = ECEF +Y, North = ECEF +Z, Up = ECEF +X.
    double e = 0.0, n = 0.0, u = 0.0;
    geo::EcefDeltaToEnu(10.0, 20.0, 30.0, 0.0, 0.0, &e, &n, &u);
    CHECK(e == Approx(20.0).margin(1e-9));
    CHECK(n == Approx(30.0).margin(1e-9));
    CHECK(u == Approx(10.0).margin(1e-9));
}

TEST_CASE("EcefDeltaToEnu: a zero vector stays zero", "[ecef][REQ-416]") {
    double e = 1.0, n = 1.0, u = 1.0;
    geo::EcefDeltaToEnu(0.0, 0.0, 0.0, 37.0, -122.0, &e, &n, &u);
    CHECK(e == Approx(0.0).margin(1e-12));
    CHECK(n == Approx(0.0).margin(1e-12));
    CHECK(u == Approx(0.0).margin(1e-12));
}

TEST_CASE("EcefDeltaToEnu: magnitude is preserved (pure rotation)", "[ecef][REQ-416]") {
    const double dx = 53.86, dy = 24.88, dz = 80.50;
    const double mag = std::sqrt(dx * dx + dy * dy + dz * dz);
    double e = 0.0, n = 0.0, u = 0.0;
    geo::EcefDeltaToEnu(dx, dy, dz, 38.5, -104.8, &e, &n, &u);
    const double outMag = std::sqrt(e * e + n * n + u * u);
    CHECK(outMag == Approx(mag).margin(1e-9));
}
