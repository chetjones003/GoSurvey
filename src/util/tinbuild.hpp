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
/// Constrained edges (breaklines, boundary rings — REQ-069) are enforced by flip-based insertion
/// (Anglada/Sloan): after the unconstrained Delaunay triangulation is built, each constraint edge not
/// already present is exposed by repeatedly flipping the triangle-edge it crosses whenever the local
/// quad is convex, deferring the rest until a later pass makes them so. This is the standard method —
/// it needs no segment-intersection retriangulation and it composes with the existing incremental
/// Bowyer–Watson build rather than replacing it.

#include <cstdint>
#include <string>
#include <utility>
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

  /// Duplicates whose Z disagreed by more than \ref kTinPlanEpsilon at the same plan position — a
  /// subset of \ref duplicatesDropped, called out because it means two shots of the same ground
  /// disagree, not that the same shot was entered twice (REQ-069, REQ-201).
  int conflictingDuplicates = 0;

  /// Constraint edges (breaklines/boundary rings) whose exposure did not converge within the flip
  /// budget — reported, not silently left crossed (REQ-069, REQ-201).
  int constraintsUnresolved = 0;

  [[nodiscard]] bool ok() const { return status == TinBuildStatus::Ok; }
  [[nodiscard]] int vertexCount() const { return static_cast<int>(vertsXyz.size() / 3); }
  [[nodiscard]] int triangleCount() const { return static_cast<int>(indices.size() / 3); }
};

/// One edge of a breakline or boundary ring, in the caller's own coordinates — REQ-069. Endpoints are
/// **not** required to already be in \p points passed to \ref BuildTin: a constraint's own vertices
/// are inserted into the triangulation as ordinary points (deduplicated exactly like any other),
/// carrying their own elevation. A boundary ring of N vertices is N \c TinConstraint edges, one per
/// side, closing back to the first vertex.
struct TinConstraint {
  double ax = 0.0, ay = 0.0;
  float  az = 0.f;
  double bx = 0.0, by = 0.0;
  float  bz = 0.f;
};

/// Two constraint segments that cross in plan but disagree on elevation at the crossing point, by
/// more than \ref kTinPlanEpsilon — reported rather than silently triangulated one way or the other
/// (REQ-069's named diagnostic for this case, REQ-201).
struct TinCrossingIssue {
  size_t constraintIndexA = 0;
  size_t constraintIndexB = 0;
  double x = 0.0, y = 0.0;
  float  zFromA = 0.f, zFromB = 0.f;
};

/// Finds every pair of \p constraints that cross in plan with a Z disagreement at the crossing point.
/// Pure geometry over the input segments — independent of whether they were ever built into a
/// triangulation, so it can run before \ref BuildTin is even called and be reported up front.
[[nodiscard]] std::vector<TinCrossingIssue> TinFindCrossingConflicts(const std::vector<TinConstraint>& constraints);

/// Plan-distance below which two points are the same site. Matches REQ-101's ±0.01 ft: two shots
/// closer than this in plan cannot be distinguished by the tolerance the rest of the system works to,
/// and feeding both to Delaunay is undefined.
inline constexpr double kTinPlanEpsilon = 0.01;

/// Triangulate \p points in plan (X/Y), carrying Z through to the output vertices, honouring every
/// edge in \p constraints (REQ-069): no output triangle edge crosses one. Constraint vertices are
/// folded into the point set (see \ref TinConstraint); \p constraints defaults to empty, which is
/// exactly REQ-068's unconstrained build — existing callers are unaffected.
///
/// Returns a result whose `status` is `Ok` only when there is a surface; on any other status the
/// vertex and index arrays are **empty** — there is no partial surface (REQ-001: reject, never
/// absorb).
[[nodiscard]] TinBuildResult BuildTin(const std::vector<TinInputPoint>& points,
                                      const std::vector<TinConstraint>& constraints = {});

/// How a boundary ring (REQ-069) affects the surface it is applied to.
enum class TinBoundaryKind : std::uint8_t {
  Outer,  ///< Clips the surface to inside this ring.
  Hide,   ///< Removes surface inside this ring, leaving a void.
  Show,   ///< Restores surface inside this ring — meaningful inside a Hide.
};

/// A boundary ring: an ordered, implicitly-closed polygon in plan (X/Y). Vertex order does not need
/// to be a particular winding — containment is tested by ray casting, which is winding-independent.
struct TinBoundaryLoop {
  TinBoundaryKind kind = TinBoundaryKind::Outer;
  std::vector<std::pair<double, double>> ring;
};

/// Culls triangles from an already-built (\p vertsXyz, \p indices) by a sequence of boundary rings,
/// applied in \p loops order (REQ-069: "boundaries apply in definition order"). A triangle's
/// inclusion is decided by its **centroid**: exact when the ring's edges were also passed to
/// \ref BuildTin as constraints (no triangle straddles a constrained boundary edge), an approximation
/// otherwise. No \c Outer loop present means "no clip" (every triangle starts included); \c Hide
/// removes centroids inside its ring; \c Show restores centroids inside its ring, which only has a
/// visible effect where an earlier \c Hide removed them.
///
/// \p indices is filtered in place; \p vertsXyz is left untouched (culled vertices simply go
/// unreferenced, exactly as convex-hull exclusion already works in \ref BuildTin).
void TinCullByBoundaries(std::vector<std::uint32_t>& indices, const std::vector<float>& vertsXyz,
                         const std::vector<TinBoundaryLoop>& loops);

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
