#pragma once

// Local ↔ grid transformation (REQ-360, GitHub issue #582 increment 4): the Transformation tab's
// maths. Pure: no CS-MAP, no drawing state, every value in double (REQ-101).
//
//   grid = G_ref + k · R(θ) · (L − L_ref)        L = local = θ-rotated, k-scaled ground coordinates
//
// Local and grid coordinates are in the SAME unit (the zone's); the caller converts the drawing unit
// first. θ is counter-clockwise (the maths convention); the helpers below turn the tab's
// clockwise-from-north angles into it.

namespace geo {

struct LocalGridTransform {
  double refLocalX = 0.0;  ///< L_ref, easting (zone unit)
  double refLocalY = 0.0;  ///< L_ref, northing
  double refGridX = 0.0;   ///< G_ref, easting (zone unit)
  double refGridY = 0.0;   ///< G_ref, northing
  double scale = 1.0;      ///< k = k_grid · k_sea; must be > 0
  double rotationRad = 0.0;  ///< θ, counter-clockwise
};

struct XY {
  double x = 0.0;
  double y = 0.0;
};

[[nodiscard]] XY LocalToGrid(const LocalGridTransform& t, double localX, double localY);
/// The exact inverse of \ref LocalToGrid.
[[nodiscard]] XY GridToLocal(const LocalGridTransform& t, double gridX, double gridY);

/// Sea level scale factor R / (R + h): \p radius and \p elevation in the same unit. 0 when R + h ≤ 0.
[[nodiscard]] double SeaLevelScaleFactor(double radius, double elevation);

/// θ (counter-clockwise, radians) that turns the local direction ref→rot into the grid direction
/// ref→rot. False when either pair is coincident (no direction), which a caller must refuse.
[[nodiscard]] bool RotationFromPoints(double refLocalX, double refLocalY, double rotLocalX, double rotLocalY,
                                      double refGridX, double refGridY, double rotGridX, double rotGridY,
                                      double* rotationRad);

/// θ for "To north": \p toNorthDeg is the angle from local north to grid north, clockwise (REQ-021's
/// entry convention). A local line at that azimuth becomes grid north.
[[nodiscard]] double RotationFromToNorthDeg(double toNorthDeg);
/// θ for "Azimuth": the local azimuth \p localAzimuthDeg becomes the grid azimuth \p gridAzimuthDeg
/// (both clockwise from north).
[[nodiscard]] double RotationFromAzimuthsDeg(double localAzimuthDeg, double gridAzimuthDeg);

/// Azimuth (degrees clockwise from north, [0, 360)) of the direction (dx, dy).
[[nodiscard]] double AzimuthDeg(double dx, double dy);

}  // namespace geo
