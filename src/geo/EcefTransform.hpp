#pragma once

// WGS84 ECEF <-> geodetic conversion and local ENU (East-North-Up) tangent-plane
// rotation (GitHub issue #847). Pure closed-form ellipsoid math, no CS-MAP and no
// UI dependency — kept separate from geo::CoordinateSystems so the traverse
// module's GPS-vector resolution (TraverseCalc.cpp) does not need to link CS-MAP;
// grid (UTM/State Plane/etc.) re-projection of the resulting lat/long is a
// further, optional step via the existing geo::Wgs84GridConverter.

namespace geo {

/// WGS84 ellipsoid semi-major axis, meters.
inline constexpr double kWgs84SemiMajorM = 6378137.0;
/// WGS84 ellipsoid flattening.
inline constexpr double kWgs84Flattening = 1.0 / 298.257223563;

/// ECEF (x, y, z, meters) -> geodetic WGS84 latitude/longitude (degrees) and
/// ellipsoid height (meters). Iterative (converges in a handful of steps for
/// any point near the earth's surface).
void EcefToGeodeticWgs84(double x, double y, double z,
                          double* latDegOut, double* lonDegOut, double* heightMOut);

/// Geodetic WGS84 latitude/longitude (degrees) and ellipsoid height (meters) ->
/// ECEF (x, y, z, meters). Inverse of \ref EcefToGeodeticWgs84.
void GeodeticToEcefWgs84(double latDeg, double lonDeg, double heightM,
                          double* xOut, double* yOut, double* zOut);

/// Rotates an ECEF delta vector (dx, dy, dz, meters — e.g. a GPS baseline vector)
/// into local East/North/Up components (meters) at the tangent plane anchored at
/// geodetic (originLatDeg, originLonDeg). Exact for the ENU frame at that origin
/// (no map-grid distortion) — this is the rotation GPS processing software uses
/// to turn a baseline vector into local coordinates.
void EcefDeltaToEnu(double dx, double dy, double dz,
                     double originLatDeg, double originLonDeg,
                     double* eastMOut, double* northMOut, double* upMOut);

}  // namespace geo
