#pragma once

#include "ray3d.hpp"

#include <cstdint>
#include <vector>

/// Rational tensor-product B-spline (NURBS) patch — the kernel's freeform surface (REQ-315 / ADR-048,
/// GitHub issue #147, Phase 4 of #120).
///
/// This is the **minimal subset** ADR-048 (b) allows: degree at most 3 per direction, an untrimmed
/// rectangular patch, rational weights carried so a lofted or swept circular-arc profile edge is
/// exact. Trimmed patches, higher degree, and surface–surface intersection against a patch are out
/// of scope and are not built here.
///
/// Pure and dependency-free — only `ray3d` for `Vec3` — the same ADR-002 layering that governs the
/// rest of `util/`. Everything is `double`; a caller narrows to `float` local storage above this
/// layer once the document origin is known (REQ-101). `brep` builds a \ref Patch for a
/// `SurfaceKind::Nurbs` face and integrates it numerically; this module owns only the maths of the
/// patch itself — evaluation, derivatives, the surface normal, validation, and the two builders loft
/// needs.
namespace nurbs {

using ray3d::Vec3;

/// The most smoothness a loft or a sweep between line-and-arc profiles ever needs (ADR-048 (b)).
inline constexpr int kMaxDegree = 3;

/// A rational B-spline surface patch.
///
/// Control points and their weights are a `nu * nv` grid stored **row-major**: control point
/// `(i, j)` — `i` along U, `j` along V — is at `ctrl[j * nu + i]` with weight `wts[j * nu + i]`.
/// The knot vectors are clamped (the first `degU + 1` and last `degU + 1` U-knots are equal, and
/// likewise for V), so the patch passes through its four corner control points and the parameter
/// domain is `[knotsU[degU], knotsU[nu]] x [knotsV[degV], knotsV[nv]]`.
struct Patch {
  int degU = 1;
  int degV = 1;
  int nu = 0;  ///< Number of control points along U.
  int nv = 0;  ///< Number of control points along V.
  std::vector<double> knotsU;  ///< Size `nu + degU + 1`, non-decreasing.
  std::vector<double> knotsV;  ///< Size `nv + degV + 1`, non-decreasing.
  std::vector<Vec3> ctrl;      ///< Size `nu * nv`, row-major (see above).
  std::vector<double> wts;     ///< Size `nu * nv`, each `> 0`. Row-major, parallel to \ref ctrl.
};

/// Why a \ref Patch is not a usable surface. Nothing is repaired silently (REQ-201): a patch is
/// valid or it is refused with a reason a caller can surface to the user.
enum class PatchProblem : std::uint8_t {
  Ok = 0,
  DegreeOutOfRange,        ///< A degree below 1 or above \ref kMaxDegree.
  TooFewControlPoints,     ///< Fewer than `deg + 1` control points in a direction.
  ControlCountMismatch,    ///< `ctrl.size()` or `wts.size()` is not `nu * nv`.
  KnotVectorWrongLength,   ///< A knot vector whose length is not `n + deg + 1`.
  KnotsNotNondecreasing,   ///< A knot vector that steps backward.
  KnotVectorNotClamped,    ///< The end knots are not repeated `deg + 1` times.
  DegenerateKnotDomain,    ///< The first and last distinct knots are equal — no parameter span.
  NonFiniteControlPoint,   ///< A NaN or infinity in a control point.
  NonPositiveWeight,       ///< A weight that is zero, negative, or not finite.
};

/// A short, user-facing sentence for \p p. Never returns null.
[[nodiscard]] const char* PatchProblemText(PatchProblem p);

/// Full structural check of \p patch — degrees, control counts, knot-vector lengths, monotonic and
/// clamped knots, finite control points, positive weights.
[[nodiscard]] PatchProblem ValidatePatch(const Patch& patch);

/// Convenience predicate over \ref ValidatePatch.
[[nodiscard]] inline bool IsValidPatch(const Patch& patch) {
  return ValidatePatch(patch) == PatchProblem::Ok;
}

/// The parameter domain of a valid patch: `[uMin, uMax] x [vMin, vMax]`.
[[nodiscard]] double UMin(const Patch& patch);
[[nodiscard]] double UMax(const Patch& patch);
[[nodiscard]] double VMin(const Patch& patch);
[[nodiscard]] double VMax(const Patch& patch);

/// The point on \p patch at parameter `(u, v)`. `u` and `v` are clamped to the patch domain, so a
/// caller that walks a face's parametric span never falls off the end of a knot vector.
[[nodiscard]] Vec3 Evaluate(const Patch& patch, double u, double v);

/// A point on the patch together with its first partial derivatives and the unit surface normal.
struct SurfacePoint {
  Vec3 p;       ///< The surface point.
  Vec3 du;      ///< dS/du.
  Vec3 dv;      ///< dS/dv.
  Vec3 normal;  ///< Unit `du x dv`. Zero only where the patch is degenerate at `(u, v)` (a collapsed
                ///< edge — a pole), where a caller should fall back to a neighbouring sample.
};

/// \ref Evaluate plus the first derivatives and the normal, from one basis-function pass.
[[nodiscard]] SurfacePoint EvaluateWithDerivs(const Patch& patch, double u, double v);

/// \p patch with every control point moved by \p delta. Weights, knots and degrees are unchanged.
/// The first caller is `brep::Translate` (the document-origin rebase, REQ-101).
[[nodiscard]] Patch Translate(const Patch& patch, const Vec3& delta);

// ---------------------------------------------------------------------------------------------
// Builders — the two shapes a loft between line-and-arc profiles produces (ADR-048 (f)).
// A sweep (increment 2) will add path-frame builders here.
// ---------------------------------------------------------------------------------------------

/// A **ruled** patch spanning straight (degree 1) from polyline \p row0 to polyline \p row1 in V,
/// interpolating each polyline's points along U at degree 1. The two polylines must have the same
/// point count (>= 2). Every weight is 1 — a straight span carries no rational data.
///
/// This is the patch a loft raises over one pair of corresponding **straight** profile edges, and,
/// chained, over a pair of whole polygonal profiles. `row0[k]` maps to `(u_k, vMin)` and `row1[k]`
/// to `(u_k, vMax)` with `u_k` the chord-length parameter along `row0`.
[[nodiscard]] Patch RuledLinear(const std::vector<Vec3>& row0, const std::vector<Vec3>& row1);

/// The degree-2 rational control points and weights of a **circular arc** of \p sweepRad radians
/// (`0 < |sweepRad| <= 2*pi`) about \p axis (unit) through \p centre, starting at \p start. Written
/// into \p outPts and \p outWts (cleared first); the arc is split into `ceil(|sweep| / (pi/2))`
/// span segments so no segment exceeds a quarter turn, giving `2 * segments + 1` control points.
/// The interior weights are `cos(halfSegmentAngle)`; the rest are 1. Exact — an evaluated point is
/// on the circle to rounding.
void RationalArc(const Vec3& centre, const Vec3& start, const Vec3& axis, double sweepRad,
                 std::vector<Vec3>* outPts, std::vector<double>* outWts);

/// A **ribbon** patch spanning straight (degree 1) in V between two circular arcs that share a sweep
/// and an axis direction — arc 0 about \p centre0 from \p start0, arc 1 about \p centre1 from
/// \p start1 — both of \p sweepRad about \p axis. Degree 2 and rational in U. This is the patch a
/// loft raises over one pair of corresponding **arc** profile edges.
[[nodiscard]] Patch ArcRibbon(const Vec3& centre0, const Vec3& start0, const Vec3& centre1,
                              const Vec3& start1, const Vec3& axis, double sweepRad);

}  // namespace nurbs
