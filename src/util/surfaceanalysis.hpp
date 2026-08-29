#pragma once

/// Surface analysis: elevation banding, slope banding, and slope arrows (REQ-072 / ADR-036 (g)).
///
/// Pure and dependency-free — `<vector>` only — so `GoSurveyTests` links it without a GL context or
/// the GUI stack, beside `contourgen` and `tinbuild` and for the same reason (ADR-028 (c)).
///
/// **One representative value per triangle.** A triangle is banded by a single number — its centroid
/// elevation, or its plane's grade — and takes one colour for the whole of it. It is never
/// subdivided where a band boundary crosses it (ASSUMPTION-1). REQ-072 asks that "a triangle of known
/// elevation and of known slope each take the colour their band prescribes", which presumes exactly
/// one colour per triangle; Civil 3D does the same.
///
/// **A flat triangle is a distinguishable answer, not a zero vector.** \ref TriangleDownhillDirection
/// reports validity separately rather than returning `(0,0)` for a triangle that has no downhill, so
/// "there is no direction here" cannot be mistaken for "the direction is zero". A caller that draws
/// the zero vector draws an arrow pointing east.
///
/// The arithmetic is `double` over the TIN's `float` storage, per architecture §11.8 — the same
/// widening `contourgen` and `TinElevationAt` document, and for the same reason.

#include <vector>

/// The three corners of one triangle, widened from the TIN's float storage by the caller (§11.8).
struct AnalysisTriangle {
  double x0 = 0.0, y0 = 0.0, z0 = 0.0;
  double x1 = 0.0, y1 = 0.0, z1 = 0.0;
  double x2 = 0.0, y2 = 0.0, z2 = 0.0;
};

/// The grade AT OR BELOW which a triangle is treated as flat and given no arrow, as a PERCENT.
///
/// ASSUMPTION-2: the flatness test is a minimum grade, not a raw normal-vector magnitude, because a
/// grade is a number a surveyor can read and argue with — 0.1% is one foot of fall in a thousand,
/// which no survey drawing means to show a direction for. A bitwise-equal-Z test instead would emit
/// an arrow of arbitrary direction for a triangle flat to any practical measure, which is the exact
/// failure REQ-072 names.
constexpr double kFlatGradePctDefault = 0.1;

/// The triangle's centroid elevation — the representative value for ELEVATION banding.
double TriangleCentroidZ(const AnalysisTriangle& t);

/// The grade of the triangle's plane as a PERCENT (100 × rise ÷ run), the representative value for
/// SLOPE banding. Percent rather than degrees because that is what a grading plan is dimensioned in.
///
/// A degenerate triangle — three collinear or coincident corners, so there is no plane — has no grade
/// and returns 0. It is drawn as flat rather than assigned an arbitrary steepness, and
/// \ref TriangleDownhillDirection likewise refuses it. A truly vertical face returns infinity, which
/// bands into the topmost band; a Delaunay TIN of distinct XY points cannot produce one, so it is
/// defensive rather than expected.
double TrianglePlaneSlopePct(const AnalysisTriangle& t);

/// The downhill direction of the triangle's plane as a UNIT vector in XY, written to \p outDx and
/// \p outDy.
///
/// Returns false — and leaves the outputs untouched — when the triangle's grade is at or below
/// \p flatGradePct, or is degenerate. False is the answer "this triangle has no downhill", and is
/// what REQ-072's "a perfectly flat triangle produces no arrow direction" is asserted on.
///
/// The result does not depend on winding: a triangle and the same triangle wound the other way give
/// the same downhill, because the plane's fall direction is a property of the surface and not of the
/// order its corners happen to be listed in.
bool TriangleDownhillDirection(const AnalysisTriangle& t, double flatGradePct, double* outDx,
                               double* outDy);

/// Downhill azimuth in degrees: 0 = +Y (northing), increasing toward +X (easting), in [0, 360).
/// False (outputs untouched) when \ref TriangleDownhillDirection refuses the triangle.
bool TriangleDownhillAspectDeg(const AnalysisTriangle& t, double flatGradePct, double* outDeg);

/// The index of the band \p value falls in, given the bands' \p upperBounds — or -1 when it falls in
/// none.
///
/// **Bands are half-open, `[lo, hi)`** (Q2). A value exactly on a breakpoint therefore falls in the
/// band ABOVE it, uniformly, so REQ-072's "including at an exact breakpoint, where the band a value
/// falls into is defined and tested rather than left to float comparison" is answered by a rule
/// rather than by whichever way a comparison happened to round.
///
/// Two consequences, both deliberate:
///   * the lowest band is open at the bottom, so every value at or below the table's top has a band;
///   * **the topmost band is closed at its top**, so the surface's maximum elevation has a band
///     instead of falling off the end of the table that was built to span it.
///
/// A value strictly above the topmost bound belongs to no band and returns -1; so does any value when
/// \p upperBounds is empty. The caller draws such a triangle unbanded rather than clamping it into a
/// band it is not in.
///
/// \p upperBounds must be **strictly ascending**. It is not sorted here: the returned index selects
/// the caller's colour for that band, so reordering the bounds would silently repaint the surface.
int AssignBand(double value, const std::vector<double>& upperBounds);
