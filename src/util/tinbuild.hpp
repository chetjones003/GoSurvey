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

// --- Predicates, exposed for testing -----------------------------------------------------------
// These are the two functions the whole triangulation rests on, and a sign error in either is
// invisible on most inputs. They are public so they can be pinned directly rather than only through
// the triangles they happen to produce.

/// > 0 if c is left of a→b (counter-clockwise), < 0 if right, 0 if collinear.
[[nodiscard]] double TinOrient2D(double ax, double ay, double bx, double by, double cx, double cy);

/// > 0 if d is strictly inside the circumcircle of the counter-clockwise triangle a,b,c.
[[nodiscard]] double TinInCircle(double ax, double ay, double bx, double by, double cx, double cy,
                                 double dx, double dy);
