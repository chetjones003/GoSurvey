// Local ↔ grid transformation (REQ-360). See LocalGridTransform.hpp.

#include "LocalGridTransform.hpp"

#include <cmath>

namespace geo {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDegToRad = kPi / 180.0;
/// Shorter than any surveyed distance; two points closer than this have no direction between them.
constexpr double kCoincident = 1e-9;

}  // namespace

XY LocalToGrid(const LocalGridTransform& t, double localX, double localY) {
  const double dx = localX - t.refLocalX;
  const double dy = localY - t.refLocalY;
  const double c = std::cos(t.rotationRad);
  const double s = std::sin(t.rotationRad);
  return {t.refGridX + t.scale * (c * dx - s * dy), t.refGridY + t.scale * (s * dx + c * dy)};
}

XY GridToLocal(const LocalGridTransform& t, double gridX, double gridY) {
  const double dx = (gridX - t.refGridX) / t.scale;
  const double dy = (gridY - t.refGridY) / t.scale;
  const double c = std::cos(t.rotationRad);
  const double s = std::sin(t.rotationRad);
  return {t.refLocalX + c * dx + s * dy, t.refLocalY - s * dx + c * dy};
}

double SeaLevelScaleFactor(double radius, double elevation) {
  const double d = radius + elevation;
  return d > 0.0 ? radius / d : 0.0;
}

bool RotationFromPoints(double refLocalX, double refLocalY, double rotLocalX, double rotLocalY, double refGridX,
                        double refGridY, double rotGridX, double rotGridY, double* rotationRad) {
  const double lx = rotLocalX - refLocalX;
  const double ly = rotLocalY - refLocalY;
  const double gx = rotGridX - refGridX;
  const double gy = rotGridY - refGridY;
  if (std::hypot(lx, ly) < kCoincident || std::hypot(gx, gy) < kCoincident)
    return false;
  *rotationRad = std::atan2(gy, gx) - std::atan2(ly, lx);
  return true;
}

double RotationFromToNorthDeg(double toNorthDeg) {
  // A local line at azimuth α must come out at grid azimuth 0: azimuth decreases by α, which is a
  // counter-clockwise turn of α.
  return toNorthDeg * kDegToRad;
}

double RotationFromAzimuthsDeg(double localAzimuthDeg, double gridAzimuthDeg) {
  return (localAzimuthDeg - gridAzimuthDeg) * kDegToRad;
}

double AzimuthDeg(double dx, double dy) {
  double az = std::atan2(dx, dy) / kDegToRad;
  if (az < 0.0)
    az += 360.0;
  return az >= 360.0 ? 0.0 : az;
}

}  // namespace geo
