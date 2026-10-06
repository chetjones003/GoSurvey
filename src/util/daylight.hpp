#pragma once

/// REQ-371 — side slope grading: project a designed edge outward at a stated slope until it meets
/// existing ground. Where it meets is a **daylight point**; the chain of them is the daylight line,
/// which is what says how much land the earthwork disturbs.
///
/// Pure util over \ref ISurfaceQuery, with no document types, which is the shape ADR-028's first
/// consequence already prescribes for surface work and what REQ-300 / REQ-301 ask for. Everything
/// here is `double`: REQ-101 is +/-0.002 ft and `float` resolves only ~0.25 ft at the survey
/// magnitudes REQ-371's last acceptance condition names, so a float here would miss by 125x.
///
/// Slopes are **run:rise** — 3:1 is three horizontal per one vertical. Cut and fill are separate
/// values because a cut face stands steeper than placed fill, so one slope for both would have no
/// engineering meaning (D-2026-10-05-b).

#include "surfacequery.hpp"

#include <vector>

/// Why a baseline point produced no daylight point. Each is reported rather than papered over —
/// REQ-074 forbids extrapolating past a surface edge, so "I could not" is a real answer here.
enum class DaylightFailure {
  None,
  /// The baseline point itself is not over the surface. REQ-074: report outside, never extrapolate.
  BaselineOutsideSurface,
  /// The projection ran past the surface's edge before reaching ground. Same rule, further out.
  SlopeLeftSurface,
  /// Ground falls away at least as fast as the slope descends, so the two never converge.
  NeverMeets,
  /// A run that is zero, negative or not finite — a slope that cannot be projected.
  DegenerateSlope,
};

struct DaylightPoint {
  bool daylighted = false;
  double offset = 0.0;  ///< horizontal distance from the baseline point, in feet
  double x = 0.0, y = 0.0, z = 0.0;
  bool inCut = false;  ///< the design sat BELOW ground here, so the slope climbs rather than falls
  DaylightFailure why = DaylightFailure::None;
};

/// The two slopes, as the run of run:rise.
struct SideSlopes {
  double cutRun = 2.0;   ///< used where the design sits BELOW existing ground
  double fillRun = 3.0;  ///< used where the design sits ABOVE it
};

/// How far out to look, and how finely to step before bisecting the bracketed crossing.
///
/// The step is REQ-371's stated known limit: ground detail finer than one step can hide a second
/// crossing, and the nearest one is reported. Callers pass a step scaled to the surface's own
/// triangle size rather than a constant, so the limit tracks the data.
struct DaylightSearch {
  double maxOffsetFt = 500.0;
  double stepFt = 1.0;
};

/// Solve one baseline point. \p dirX / \p dirY must be a unit horizontal vector pointing outward.
///
/// \p bz is the DESIGN elevation at (bx, by) — the feature line's own elevation there, not the
/// ground's. Whether this is cut or fill is decided from the two, not asked.
[[nodiscard]] DaylightPoint SolveDaylightPoint(const ISurfaceQuery& ground, double bx, double by,
                                               double bz, double dirX, double dirY,
                                               const SideSlopes& slopes, const DaylightSearch& search);

/// Percent grade for a run:rise run, so a caller reports REQ-074's two conventions from one source
/// and cannot describe one slope two ways (the drift REQ-105 was amended to stop).
[[nodiscard]] double SlopePercentFromRun(double run);

/// Outward unit normals for a baseline, one per vertex, as interleaved x,y pairs.
///
/// \p xy is interleaved plan coordinates. For a **closed** baseline, outward is away from the
/// enclosed interior, decided from the ring's own signed area rather than from vertex order — so a
/// ring drawn either way grades outward. For an **open** one there is no intrinsic outward, so
/// \p leftSide picks the side, which is why REQ-371 requires the caller to have asked.
///
/// Returns false and leaves the outputs empty when the baseline has fewer than two distinct points.
[[nodiscard]] bool BaselineOutwardNormals(const std::vector<double>& xy, bool closed, bool leftSide,
                                          std::vector<double>* outNxy);
