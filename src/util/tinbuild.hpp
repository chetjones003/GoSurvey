#pragma once

/// Delaunay triangulation for TIN surfaces (REQ-068 / ADR-028 (c)).
///
/// Pure and dependency-free — only `<cstdint>`, `<string>`, `<vector>` — so `GoSurveyTests` links it
/// without a GL context or the GUI stack, which is the standing lesson of TASK-035 §11 and the same
/// reason `curveintersect`, `gltfimport` and `benchscene` live here.
///
/// **Predicates run in `double`, storage stays `float`** (ADR-028 (d)). Orientation and in-circle
/// are the classic float-instability case: a sign flip does not produce a slightly wrong triangle,
/// it produces a wrong *topology* — a visibly bad face, or an edge-flip loop that never terminates.
/// REQ-101's ±0.01 ft leaves no margin to absorb that, so coordinates are widened at the predicate
/// rather than in the store (architecture §11.8 is unchanged).
///
/// Constraints (breaklines) are **not** here: they belong to REQ-069, and building their machinery
/// before that requirement is in flight would be scaffolding (REQ-301). The output addresses
/// vertices by index, which is what constraint insertion will need to operate on.

#include <cstdint>
#include <string>
#include <vector>

/// A point going into the triangulation. X/Y decide the topology; Z is carried through untouched.
struct TinInputPoint {
  double x = 0.0;
  double y = 0.0;
  float  z = 0.f;
};

/// Why a build produced no surface. Every one of these is reported, never absorbed (REQ-201).
enum class TinBuildStatus : std::uint8_t {
  Ok = 0,
  TooFewPoints,   ///< Fewer than 3 distinct plan positions.
  AllCollinear,   ///< 3+ distinct points, but no three of them form a triangle.
};

struct TinBuildResult {
  TinBuildStatus status = TinBuildStatus::TooFewPoints;

  /// Interleaved x,y,z (architecture §11.8), one triplet per surviving vertex. De-duplicated: see
  /// \ref duplicatesDropped.
  std::vector<float> vertsXyz;
  /// Triangle list, 3 indices per triangle into \ref vertsXyz. Every triangle is counter-clockwise.
  std::vector<std::uint32_t> indices;

  /// Points discarded because another point already occupied that plan position within
  /// \ref kTinPlanEpsilon. The FIRST occurrence wins. Reported so a user can see that two shots
  /// disagreed rather than wondering why a spike vanished (REQ-201).
  int duplicatesDropped = 0;

  /// Human-readable explanation, always set when status != Ok, and set when duplicates were
  /// dropped. Empty otherwise.
  std::string message;

  [[nodiscard]] bool ok() const { return status == TinBuildStatus::Ok; }
  [[nodiscard]] int vertexCount() const { return static_cast<int>(vertsXyz.size() / 3); }
  [[nodiscard]] int triangleCount() const { return static_cast<int>(indices.size() / 3); }
};

/// Plan-distance below which two points are the same site. Matches REQ-101's ±0.01 ft: two shots
/// closer than this in plan cannot be distinguished by the tolerance the rest of the system works to,
/// and feeding both to Delaunay is undefined.
inline constexpr double kTinPlanEpsilon = 0.01;

/// Triangulate \p points in plan (X/Y), carrying Z through to the output vertices.
///
/// Returns a result whose `status` is `Ok` only when there is a surface; on any other status the
/// vertex and index arrays are **empty** — there is no partial surface (REQ-001: reject, never
/// absorb).
[[nodiscard]] TinBuildResult BuildTin(const std::vector<TinInputPoint>& points);

/// Interpolated surface elevation at plan position (\p x, \p y) — REQ-074's spot elevation.
///
/// Finds the triangle covering the point and evaluates that triangle's plane there. Takes the raw
/// arrays rather than a `CadTin` so it stays GUI-free and testable without a GL context, like
/// everything else in this header; callers pass `tin->vertsXyz` and `tin->indices`.
///
/// **Containment is decided per triangle, never against the hull.** A convex-hull test would call a
/// point in a concave notch "inside" and then hand back an extrapolated elevation for ground that
/// was never surveyed — which REQ-074 forbids in as many words ("it never extrapolates"). Deciding
/// per triangle makes concavities, and later REQ-069's boundary voids, outside by construction:
/// there is simply no triangle there.
///
/// The arithmetic is done in `double` even though the vertices are `float` (architecture §11.8).
/// At state-plane magnitudes a barycentric solve in float loses far more than REQ-101's ±0.01 ft to
/// cancellation — the same failure BUG-001 hit in picking and ADR-028 (d) recorded for the
/// triangulation predicates.
///
/// A point exactly on an edge or vertex is inside: shared edges must not have a gap between the two
/// triangles that meet there, and either triangle's plane gives the same elevation on the edge they
/// share.
///
/// \param outZ receives the elevation; untouched when the point is not on the surface.
/// \returns true when a triangle covers the point.
[[nodiscard]] bool TinElevationAt(const std::vector<float>& vertsXyz, const std::vector<std::uint32_t>& indices,
                                  double x, double y, double* outZ);

// --- Predicates, exposed for testing -----------------------------------------------------------
// These are the two functions the whole triangulation rests on, and a sign error in either is
// invisible on most inputs. They are public so they can be pinned directly rather than only through
// the triangles they happen to produce.

/// > 0 if c is left of a→b (counter-clockwise), < 0 if right, 0 if collinear.
[[nodiscard]] double TinOrient2D(double ax, double ay, double bx, double by, double cx, double cy);

/// > 0 if d is strictly inside the circumcircle of the counter-clockwise triangle a,b,c.
[[nodiscard]] double TinInCircle(double ax, double ay, double bx, double by, double cx, double cy,
                                 double dx, double dy);
