#pragma once

#include "ray3d.hpp"

#include <functional>
#include <string>
#include <vector>

/// The SPLINE entity's mathematics (REQ-440, GitHub issue #803): a non-uniform rational B-spline
/// CURVE in 3D, with the two ways a user makes one and the DXF/DWG shape it round-trips through.
///
/// Pure and dependency-free — only `ray3d` — the same ADR-002 layering the rest of `util/` keeps, so
/// it is testable with no window and no GPU. Everything is `double`; the caller narrows nothing.
///
/// **One representation for every spline**, because a SPLINE read from another CAD tool can be any
/// degree, rational, clamped or periodic, and the evaluator must not care which:
/// `degree`, a `knots` vector of length `ctrl.size() + degree + 1`, `ctrl` control points and an
/// optional `weights`. The curve's parameter domain is `[knots[degree], knots[ctrl.size()]]`.
///
/// **Two creation methods** (REQ-440 item 2), both degree 3:
///  - *Fit*: the user's points are kept in \ref Spline::fit and the curve passes exactly through
///    every one. `ctrl` and `knots` are DERIVED from `fit` by \ref MakeFit / \ref Rebuild and are what
///    DXF stores; editing a fit point re-derives them.
///  - *CV* (control vertices): `fit` is empty, `ctrl` is what the user clicked, and the curve is
///    pulled toward but does not touch the interior points.
///
/// **Closed splines are periodic** and stored *expanded*: the first `degree` control points are
/// repeated at the end and the knots are uniform, so the one evaluator handles them and DXF's
/// periodic form is written and read directly. \ref UniqueControlCount says how many are distinct.
namespace spline {

using ray3d::Vec3;

struct Spline {
  int degree = 3;
  bool periodic = false;          ///< Closed and smooth; `ctrl` is the expanded form (see above).
  std::vector<double> knots;      ///< Non-decreasing, `ctrl.size() + degree + 1` entries.
  std::vector<Vec3> ctrl;
  std::vector<double> weights;    ///< Empty = every weight 1; otherwise one positive weight per point.
  std::vector<Vec3> fit;          ///< Non-empty = a Fit spline (ctrl derived from these).
  [[nodiscard]] bool isFit() const { return !fit.empty(); }
};

/// The curve through \p points (at least two, no two consecutive ones coincident), open or closed.
/// An open fit spline is a natural cubic (zero curvature at both ends) with chord-length
/// parameters; a closed one is a periodic cubic with uniform parameters, so it is smooth at the join.
/// False, with the reason in \p why, if the points cannot define a curve.
[[nodiscard]] bool MakeFit(const std::vector<Vec3>& points, bool closed, Spline* out, std::string* why);

/// The curve controlled by \p ctrl (at least two; at least three if \p closed), open or closed. An
/// open one is a clamped uniform B-spline of degree `min(3, n - 1)` — three clicks give a quadratic,
/// two a straight line — and a closed one is a periodic cubic.
[[nodiscard]] bool MakeControl(const std::vector<Vec3>& ctrl, bool closed, Spline* out, std::string* why);

/// Re-derive \p s's control points and knots from its fit points, after a fit point was edited.
/// A CV spline is left untouched. False, with the reason, if the edited points no longer define a curve.
[[nodiscard]] bool Rebuild(Spline* s, std::string* why);

/// Why \p s is not a usable curve, or an empty string if it is. Nothing is repaired silently (REQ-201).
[[nodiscard]] std::string Validate(const Spline& s);

[[nodiscard]] double DomainMin(const Spline& s);
[[nodiscard]] double DomainMax(const Spline& s);

/// The point at parameter \p t (clamped to the domain).
[[nodiscard]] Vec3 Evaluate(const Spline& s, double t);

/// The curve as a polyline whose chords stay within \p tolerance of it (REQ-101). The first point is
/// the curve's start and the last its end; a closed spline's last point equals its first. A degenerate
/// spline yields an empty polyline.
void Tessellate(const Spline& s, double tolerance, std::vector<Vec3>* out);

/// Length of the curve, from a tessellation fine enough that it is exact to well inside REQ-101.
[[nodiscard]] double Length(const Spline& s);

/// The point of the curve nearest \p p, its parameter in \p tOut, and the distance in \p distOut.
void ClosestPoint(const Spline& s, const Vec3& p, double* tOut, Vec3* pointOut, double* distOut);

/// Distinct control points: all of them for an open spline, `ctrl.size() - degree` for a periodic one.
[[nodiscard]] std::size_t UniqueControlCount(const Spline& s);

/// Move the \p i'th DISTINCT control point (keeping a periodic spline's repeated tail in step).
void SetControlPoint(Spline* s, std::size_t i, const Vec3& p);

/// Replace every stored point (control and fit) with `f(point)`. Exact for any affine map — a move, a
/// rotation, a uniform scale, a mirror — because a B-spline is an affine combination of its control
/// points and the weights are ratios that carry no length. Knots and weights are untouched.
void MapPoints(Spline* s, const std::function<Vec3(const Vec3&)>& f);

}  // namespace spline
