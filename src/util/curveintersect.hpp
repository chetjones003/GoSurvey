#pragma once

#include <vector>

/// Exact 2D intersections between the shapes GoSurvey draws (REQ-062).
///
/// Dependency-free by design — only `<vector>` here and `<cmath>`/`<algorithm>` in the .cpp — so
/// the test target links it directly, the way it links `DwgProbe.cpp` and `CadLinetype.cpp`. That
/// matters more here than usual: intersection math is the kind of code that is wrong in ways no
/// screenshot reveals, and REQ-101 gives it a ±0.01 ft budget to stay inside.
///
/// **Why not tessellate.** Approximating an arc by chords and running segment×segment over the
/// result is the cheap implementation, and it does not meet REQ-101: a 24-chord arc of radius 100
/// deviates from the true arc by r·(1 − cos(π/24)) ≈ 0.86 ft — 86× the tolerance. Everything below
/// is analytic except conic×conic with a non-circular member, which is bracketed and then refined
/// by Newton to well inside tolerance.
namespace curveisect {

struct Vec2 {
  double x = 0.0;
  double y = 0.0;
};

/// A line segment A→B.
struct Seg {
  Vec2 a;
  Vec2 b;
};

/// A circle, arc or ellipse in one parametric form: `P(t) = c + u·cos t + v·sin t`.
///
/// One form for all three because it is what the math wants, not for tidiness:
///   - **circle**  u = (r, 0), v = (0, r)
///   - **arc**     the same, with tStart/tSweep restricting the range
///   - **ellipse** u = the major-axis vector, v = its perpendicular scaled by the axis ratio
///
/// The decisive property is that this form is **closed under linear maps**: projecting a conic into
/// a view basis is just projecting c, u and v. That is what lets apparent intersection (REQ-062)
/// reuse every routine below in screen space rather than needing a second implementation — and an
/// orbited circle really does project to an ellipse, so a screen-space path that only understood
/// circles would be wrong exactly when it is needed.
///
/// u and v need not be perpendicular or equal in length; a projected circle's rarely are.
struct Conic {
  Vec2 c;
  Vec2 u;
  Vec2 v;
  double tStart = 0.0;               ///< Range start, radians in this parametrization.
  double tSweep = 6.283185307179586; ///< Signed sweep; a full circle/ellipse is ±2π.

  [[nodiscard]] bool isFullTurn() const;
  /// True when \p t lies within [tStart, tStart+tSweep], handling negative sweep and wrap.
  [[nodiscard]] bool containsAngle(double t) const;
  [[nodiscard]] Vec2 point(double t) const;
  [[nodiscard]] Vec2 tangent(double t) const;
};

[[nodiscard]] Conic MakeCircle(double cx, double cy, double r);
[[nodiscard]] Conic MakeArc(double cx, double cy, double r, double startRad, double sweepRad);
[[nodiscard]] Conic MakeEllipse(double cx, double cy, double majVx, double majVy, double ratio);

/// One intersection, carrying the parameter on each input so callers can interpolate whatever the
/// shapes carry alongside their XY — elevation, in our case.
///
/// \c tA is the segment parameter in [0,1] for a Seg, or the conic angle for a Conic; likewise tB.
struct Hit2 {
  Vec2 p;
  double tA = 0.0;
  double tB = 0.0;
};

/// Segment × segment. Parallel and collinear pairs report nothing: a collinear overlap is a shared
/// *interval*, not a point, and inventing one of its endpoints would be a snap to a place the user
/// cannot see any crossing.
void IntersectSegSeg(const Seg& a, const Seg& b, std::vector<Hit2>* out);

/// Segment × conic — analytic for every conic including ellipses, with no special case for the
/// circle. Substituting the segment into the conic and eliminating the segment parameter leaves
/// `α·cos t + β·sin t = γ`, which solves in closed form; results outside the segment's [0,1] or the
/// conic's sweep are dropped.
void IntersectSegConic(const Seg& s, const Conic& k, std::vector<Hit2>* out);

/// Conic × conic. Circle×circle (both `u ⟂ v`, |u| = |v|) is solved analytically via the radical
/// line; any other pair is bracketed by a coarse walk and refined with a 2-D Newton step on
/// `P(u) − Q(v) = 0` until it is inside \p tol. Arc sweeps are applied after solving, so an
/// intersection off the drawn part of the arc is dropped rather than never found.
void IntersectConicConic(const Conic& a, const Conic& b, std::vector<Hit2>* out, double tol = 1.e-7);

/// Project a point / segment / conic through the linear map with the given basis rows.
///
/// \p rx,\p ry map world XY(Z) onto the horizontal screen axis and \p ux,\p uy,\p uz onto the
/// vertical one. Used to run every routine above in the camera's plane for apparent intersection.
[[nodiscard]] Vec2 ProjectPoint(double wx, double wy, double wz, const double right[3], const double up[3]);
[[nodiscard]] Seg ProjectSeg(double x0, double y0, double z0, double x1, double y1, double z1, const double right[3],
                             const double up[3]);
/// Projects a conic that lies in a plane of constant \p z (every arc/circle/ellipse we store).
[[nodiscard]] Conic ProjectConic(const Conic& k, double z, const double right[3], const double up[3]);

} // namespace curveisect
