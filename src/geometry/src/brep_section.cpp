#include <GoSurvey/Geometry/brep.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <initializer_list>
#include <limits>
#include <map>
#include <unordered_map>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>
#include "brep_internal.hpp"

namespace brep {

namespace {

// ---------------------------------------------------------------------------------------------
// Section — the cross-section of a solid by a plane, without cutting it (REQ-335, GitHub #149
// acceptance 5).
// ---------------------------------------------------------------------------------------------


/// Drop the vertices that sit on a straight run, in place (GitHub #522).
///
/// A `UNION` leaves its operands' faces split into fragments along each other's planes, and coplanar
/// fragments are not merged back, so a cut crosses those internal boundaries and keeps a vertex at
/// each — a section of two unioned boxes came out with 12 vertices where the shape has 8. The shape
/// was right; the outline carried vertices nobody drew and had to be cleaned up by hand.
///
/// A vertex goes only when **both** of its segments are straight and it lies on the line between its
/// neighbours to within 1e-9 of the outline's own size — far inside REQ-101's 0.002 ft, so a real
/// corner, however slight, is kept. An arc's endpoint is never dropped: it carries the sweep.
void DropCollinearPathVertices(Path* p) {
  if (!p || !p->closed || p->segs.size() < 3)
    return;
  // The ring of points: `start`, then each segment's end. Segment i runs from point i to point i+1,
  // and the last closes back onto `start`.
  const std::size_t n = p->segs.size();
  std::vector<ucs::Point2D> pt;
  pt.reserve(n);
  pt.push_back(p->start);
  for (std::size_t i = 0; i + 1 < n; ++i)
    pt.push_back(p->segs[i].end);

  double size = 0.0;
  for (std::size_t i = 0; i < n; ++i)
    size = std::max(size, std::hypot(pt[i].x - pt[0].x, pt[i].y - pt[0].y));
  const double tol = std::max(1e-9 * std::max(size, 1.0), 1e-12);

  std::vector<bool> keep(n, true);
  for (std::size_t i = 0; i < n; ++i) {
    // Point i is the junction between segment i-1 (arriving) and segment i (leaving).
    const std::size_t prevSeg = (i + n - 1) % n;
    if (p->segs[prevSeg].sweep != 0.0 || p->segs[i].sweep != 0.0)
      continue;  // an arc's endpoint carries its sweep
    // Its neighbours on the ring, skipping any already dropped, so a run of several goes in one pass.
    std::size_t a = (i + n - 1) % n;
    while (a != i && !keep[a])
      a = (a + n - 1) % n;
    std::size_t b = (i + 1) % n;
    while (b != i && !keep[b])
      b = (b + 1) % n;
    if (a == i || b == i || a == b)
      continue;
    const double ax = pt[b].x - pt[a].x, ay = pt[b].y - pt[a].y;
    const double len = std::hypot(ax, ay);
    if (!(len > tol))
      continue;
    const double cross = std::fabs((pt[i].x - pt[a].x) * ay - (pt[i].y - pt[a].y) * ax) / len;
    if (cross > tol)
      continue;  // a real corner
    // And it must lie BETWEEN them, not beyond an end: a spike back along the same line is a corner.
    const double t = ((pt[i].x - pt[a].x) * ax + (pt[i].y - pt[a].y) * ay) / (len * len);
    if (!(t > 0.0 && t < 1.0))
      continue;
    keep[i] = false;
  }

  std::size_t kept = 0;
  for (bool k : keep)
    kept += k ? 1u : 0u;
  if (kept == n || kept < 3)
    return;  // nothing to drop, or dropping would leave no outline

  Path out;
  out.closed = true;
  std::size_t first = 0;
  while (first < n && !keep[first])
    ++first;
  out.start = pt[first];
  for (std::size_t k = 1; k <= n; ++k) {
    const std::size_t i = (first + k) % n;
    if (!keep[i] && k != n)
      continue;  // this point is gone; the straight run simply continues
    PathSeg seg;
    seg.end = pt[i];
    // The segment arriving at a kept point is the one that left the previous kept point. Every
    // segment in a dropped run is straight by the test above, so the merged span is straight too.
    seg.sweep = p->segs[(i + n - 1) % n].sweep;
    out.segs.push_back(seg);
    if (k == n)
      break;
  }
  *p = std::move(out);
}

/// One loop of a cut face, as a \ref Path in the section frame — the shared body of
/// \ref SectionOutlines and \ref SectionLoop.
///
/// \p flip is whether the face's own normal opposes the section normal, in which case the loop is
/// walked backwards so the outline a caller receives winds counter-clockwise about the normal it
/// asked for. A hole loop, wound opposite to its face's outer loop, therefore comes back wound
/// clockwise — which is what tells the two apart without a second flag.
[[nodiscard]] bool SectionLoopToPath(const Solid& piece, const Face& face, const Loop& loop,
                                     const ucs::Ucs& frame, bool flip, Path* out, Problem* outWhy) {
  const auto fail = [&](Problem p) {
    if (outWhy)
      *outWhy = p;
    return false;
  };
  const std::vector<EdgeUse>& uses = loop.uses;
  if (uses.size() < 2)
    return fail(Problem::SliceResultInvalid);
  // Every edge must be expressible as a straight or circular segment, because that is what a
  // \ref Path is. An `Ellipse` — what an OBLIQUE cut of a cylinder produces — and an `Intersection`
  // curve are refused BY NAME rather than approximated by a chord or a nearby arc: a section is a
  // measured figure, and one that is quietly the wrong shape is the failure REQ-201 exists to stop.
  for (const EdgeUse& u : uses) {
    const CurveKind k = piece.edges[static_cast<std::size_t>(u.edge)].kind;
    if (k == CurveKind::Ellipse)
      return fail(Problem::SectionEllipse);  // named for what it is, not "flat faces only" (#516)
    if (k != CurveKind::Line && k != CurveKind::Arc)
      return fail(Problem::SectionCurve);
  }
  (void)face;

  const std::size_t n = uses.size();
  std::vector<const EdgeUse*> order;
  order.reserve(n);
  for (std::size_t i = 0; i < n; ++i)
    order.push_back(&uses[flip ? (n - 1 - i) : i]);

  Path path;
  path.closed = true;
  path.segs.clear();
  for (std::size_t i = 0; i < n; ++i) {
    const EdgeUse& u = *order[i];
    const Edge& e = piece.edges[static_cast<std::size_t>(u.edge)];
    // Reversing the loop's ORDER also reverses each edge's own direction of travel.
    const bool rev = flip ? !u.reversed : u.reversed;
    const Vec3 aW = piece.vertices[static_cast<std::size_t>(rev ? e.v1 : e.v0)].p;
    const Vec3 bW = piece.vertices[static_cast<std::size_t>(rev ? e.v0 : e.v1)].p;
    if (i == 0)
      path.start = ucs::WorldToPlane(frame, aW);

    PathSeg seg;
    seg.end = ucs::WorldToPlane(frame, bW);
    if (e.kind == CurveKind::Arc) {
      // `PathSeg::sweep` is signed CCW about the PATH frame's +Z; `Edge::sweep` is signed about the
      // EDGE frame's own +Z. Two independent sign flips: whether the arc's normal agrees with the
      // section plane's, and whether this traversal runs the edge backwards.
      const double align = ray3d::Dot(e.frame.zAxis, frame.zAxis) < 0.0 ? -1.0 : 1.0;
      seg.sweep = e.sweep * align * (rev ? -1.0 : 1.0);
    }
    path.segs.push_back(seg);
  }
  DropCollinearPathVertices(&path);
  *out = std::move(path);
  return true;
}

/// Every closed outline of the section, plus how many separate cut faces they came from.
[[nodiscard]] bool SectionAllOutlines(const Solid& solid, const Vec3& planePoint, const Vec3& planeNormal,
                                      ucs::Ucs* outPlane, std::vector<Path>* outLoops, int* outFaces,
                                      Problem* outWhy) {
  const auto fail = [&](Problem p) {
    if (outWhy)
      *outWhy = p;
    return false;
  };
  ucs::Ucs frame{};
  if (!ucs::FromNormal(planePoint, planeNormal, &frame))
    return fail(Problem::SliceDegeneratePlane);

  // The cut itself is \ref Slice's, unchanged and un-extended: sectioning is the same geometry
  // question asked without keeping the answer, so it inherits Slice's accepted set exactly.
  //
  // Non-destructiveness is not a property this has to implement. `Slice` takes its input by const
  // reference and writes to out-parameters, so the source solid is untouched by construction, and
  // the two pieces are simply dropped when this returns.
  Solid above, below;
  Problem why = Problem::Ok;
  if (!Slice(solid, planePoint, planeNormal, SliceKeep::Both, &above, &below, &why))
    return fail(why);

  // The section is carried by the faces of the ABOVE piece that lie ON the cut plane. Taking them
  // from the result rather than intersecting the faces by hand is the whole reason this is cheap:
  // Slice has already done the classification, the ordering and the seam handling.
  std::vector<const Face*> cuts;
  for (const Face& f : above.faces) {
    if (f.surface.kind != SurfaceKind::Plane)
      continue;
    if (std::fabs(std::fabs(ray3d::Dot(f.surface.frame.zAxis, frame.zAxis)) - 1.0) > 1e-9)
      continue;
    if (std::fabs(ray3d::Dot(ray3d::Sub(f.surface.frame.origin, planePoint), frame.zAxis)) > 1e-9)
      continue;
    cuts.push_back(&f);
  }
  if (cuts.empty())
    return fail(Problem::SlicePlaneMissesSolid);

  std::vector<Path> loops;
  for (const Face* f : cuts) {
    // The `above` piece's cut face looks DOWN — its outward normal points away from the material
    // above it, so it is anti-parallel to the section normal, and its loops are walked backwards.
    const bool flip = ray3d::Dot(f->surface.frame.zAxis, frame.zAxis) < 0.0;
    for (const Loop& lp : f->loops) {
      Path p;
      if (!SectionLoopToPath(above, *f, lp, frame, flip, &p, outWhy))
        return false;
      loops.push_back(std::move(p));
    }
  }
  *outPlane = frame;
  *outLoops = std::move(loops);
  if (outFaces)
    *outFaces = static_cast<int>(cuts.size());
  return Succeed(outWhy);
}

} // namespace


/// The elliptical ARC plus chord a tilted cut exposes when it runs off the end of a cylinder or cone
/// (GitHub #520 follow-up, D-2026-09-23-b). The ellipse is the one the cut would make on the endless
/// side surface; the cap cuts a piece out of it, and the chord closes what is left across the cap.
///
/// Section-only, and computed from the primitive's own geometry rather than from a cut: `Slice` does
/// not build the pieces for this cut yet (it refuses with \ref Problem::SliceCutCrossesCurvedEnd), so
/// there is no cut face to read back. That is the one place sectioning does not inherit Slice's
/// accepted set, decided with the user and recorded; the follow-up is to teach the cutter the same
/// cut, after which this can be read from the pieces like every other section.
struct SectionEllipseArcX {
  bool valid = false;
  Vec3 centre{};    ///< world, the full ellipse's centre
  Vec3 normal{};    ///< world, the caller's plane normal
  Vec3 majorDir{};  ///< world, unit
  double majorSemi = 0.0;
  double minorSemi = 0.0;
  double startParam = 0.0;  ///< the arc's own parametrisation, measured from `majorDir`
  double sweep = 0.0;       ///< signed, CCW about `normal`
  Vec3 chordA{};            ///< world; the arc runs from here...
  Vec3 chordB{};            ///< ...to here, and the chord closes it across the cap
};

/// The two parameters at which the ellipse `centre + a cos t * major + b sin t * minor` reaches the
/// axial height \p capZ, measured along \p axis from \p axisOrigin. False when it never does, or
/// only grazes it — neither is an arc plus a chord.
[[nodiscard]] bool EllipseParamsAtAxialHeight(const Vec3& centre, const Vec3& majorDir, const Vec3& minorDir,
                                              double a, double b, const Vec3& axis, const Vec3& axisOrigin,
                                              double capZ, double eps, double* outT0, double* outT1) {
  // z(t) = zc + A cos t + B sin t = capZ  ->  R cos(t - phi) = capZ - zc.
  const double zc = ray3d::Dot(ray3d::Sub(centre, axisOrigin), axis);
  const double A = a * ray3d::Dot(majorDir, axis);
  const double B = b * ray3d::Dot(minorDir, axis);
  const double R = std::sqrt(A * A + B * B);
  if (!(R > eps))
    return false;  // the ellipse is level with the cap: it never crosses it
  const double c = (capZ - zc) / R;
  if (std::fabs(c) >= 1.0 - 1e-12)
    return false;  // tangent or clear of the cap
  const double phi = std::atan2(B, A);
  const double d = std::acos(std::clamp(c, -1.0, 1.0));
  *outT0 = phi - d;
  *outT1 = phi + d;
  return true;
}

[[nodiscard]] bool SectionEllipseArcOutline(const Solid& solid, const Vec3& planePoint, const Vec3& planeNormal,
                                            ucs::Ucs* outPlane, SectionEllipseArc* outArc,
                                            Problem* outWhy) {
  if (!outPlane || !outArc)
    return Fail(Problem::IndexOutOfRange, outWhy);
  const Recipe& rc = solid.recipe;
  if (rc.kind != PrimitiveKind::Cylinder && rc.kind != PrimitiveKind::Cone)
    return Fail(Problem::SliceCurvedFace, outWhy);
  ucs::Ucs frame{};
  if (!ucs::FromNormal(planePoint, planeNormal, &frame))
    return Fail(Problem::SliceDegeneratePlane, outWhy);

  const ucs::Ucs& fr = rc.frame;
  const Vec3 axis = ray3d::Normalize(fr.zAxis);
  const Vec3 pn = ray3d::Normalize(planeNormal);
  const double dotNZ = ray3d::Dot(pn, axis);
  if (std::fabs(dotNZ) < 1e-6 || std::fabs(dotNZ) > 1.0 - 1e-9)
    return Fail(Problem::SliceCutCrossesCurvedEnd, outWhy);  // not a tilted cut at all
  const double h = rc.height;
  const double scale = std::max(h, std::max(rc.radius, rc.radius2));
  const double eps = 1e-7 * std::max(scale, 1.0);

  // The full ellipse the plane makes on the side surface, taken from the same geometry the oblique
  // cutters use: closed form for a cylinder, the quadric solve for a cone.
  Vec3 centre{}, majorDir{}, minorDir{}, eN{};
  double ea = 0.0, eb = 0.0;
  if (rc.kind == PrimitiveKind::Cylinder) {
    const double r = rc.radius;
    const Vec3 pl = ucs::WorldToUcs(fr, planePoint);
    const Vec3 nl{ray3d::Dot(pn, fr.xAxis), ray3d::Dot(pn, fr.yAxis), dotNZ};
    const double a0 = (nl.x * pl.x + nl.y * pl.y + nl.z * pl.z) / nl.z;
    centre = ray3d::Add(fr.origin, ray3d::Scale(axis, a0));
    minorDir = ray3d::Normalize(ray3d::Cross(pn, axis));
    majorDir = ray3d::Normalize(ray3d::Cross(pn, minorDir));
    ea = r / std::fabs(dotNZ);
    eb = r;
    eN = dotNZ > 0.0 ? pn : ray3d::Scale(pn, -1.0);
  } else {
    ConeObliqueEllipse ce{};
    if (!ComputeConeObliqueEllipse(fr, rc.radius, rc.radius2, h, planePoint, pn, eps, &ce))
      return Fail(Problem::SliceCutTooSteepForCone, outWhy);
    centre = ce.centre;
    majorDir = ce.majorDir;
    ea = ce.majorSemi;
    eb = ce.minorSemi;
    eN = ce.normal;
    minorDir = ray3d::Cross(eN, majorDir);
  }
  if (rc.kind == PrimitiveKind::Cylinder)
    minorDir = ray3d::Cross(eN, majorDir);  // the frame AddEllipse itself builds

  // Which cap does it run off? Exactly one, or this is not the shape this answers.
  const double zLoCap = 0.0;
  const double zHiCap = h;
  double t0 = 0.0, t1 = 0.0;
  int crossings = 0;
  double capZ = 0.0;
  for (const double cz : {zLoCap, zHiCap}) {
    double u0 = 0.0, u1 = 0.0;
    if (EllipseParamsAtAxialHeight(centre, majorDir, minorDir, ea, eb, axis, fr.origin, cz, eps, &u0, &u1)) {
      ++crossings;
      t0 = u0;
      t1 = u1;
      capZ = cz;
    }
  }
  if (crossings == 0)
    return Fail(Problem::SectionEllipse, outWhy);  // it stays on the side: a whole ellipse, not this
  if (crossings > 1)
    return Fail(Problem::SliceCutCrossesCurvedEnd, outWhy);  // off BOTH ends: two arcs and two chords

  // Of the two spans between the crossings, the section is the one whose points are INSIDE the
  // solid — on the far side of the cap from the piece the cut removed.
  const auto pointAt = [&](double t) {
    return ray3d::Add(centre, ray3d::Add(ray3d::Scale(majorDir, ea * std::cos(t)),
                                         ray3d::Scale(minorDir, eb * std::sin(t))));
  };
  const auto axialOf = [&](const Vec3& p) { return ray3d::Dot(ray3d::Sub(p, fr.origin), axis); };
  const double mid = axialOf(pointAt(0.5 * (t0 + t1)));
  const bool keepDirect = capZ == zLoCap ? mid > capZ : mid < capZ;
  double start = keepDirect ? t0 : t1;
  double sweep = keepDirect ? (t1 - t0) : (t0 + kTwoPi - t1);
  while (sweep < 0.0)
    sweep += kTwoPi;
  if (!(sweep > eps) || sweep >= kTwoPi - eps)
    return Fail(Problem::SliceCutCrossesCurvedEnd, outWhy);

  // The caller's plane normal may oppose the ellipse's own, in which case its parametrisation runs
  // the other way round — so the span is restated in the frame the caller (and the entity) will use.
  if (ray3d::Dot(frame.zAxis, eN) < 0.0) {
    start = -start;
    sweep = -sweep;
  }

  SectionEllipseArc out;
  out.valid = true;
  out.centre = centre;
  out.normal = frame.zAxis;
  out.majorDir = majorDir;
  out.majorSemi = ea;
  out.minorSemi = eb;
  out.startParam = start;
  out.sweep = sweep;
  out.chordA = pointAt(keepDirect ? t0 : t1);
  out.chordB = pointAt((keepDirect ? t0 : t1) + (keepDirect ? (t1 - t0) : (t0 + kTwoPi - t1)));
  *outPlane = frame;
  *outArc = out;
  return Succeed(outWhy);
}

bool SectionEllipseOutline(const Solid& solid, const Vec3& planePoint, const Vec3& planeNormal,
                           ucs::Ucs* outPlane, SectionEllipse* outEllipse, Problem* outWhy) {
  if (!outPlane || !outEllipse)
    return Fail(Problem::IndexOutOfRange, outWhy);

  ucs::Ucs frame{};
  if (!ucs::FromNormal(planePoint, planeNormal, &frame))
    return Fail(Problem::SliceDegeneratePlane, outWhy);

  Solid above, below;
  Problem why = Problem::Ok;
  if (!Slice(solid, planePoint, planeNormal, SliceKeep::Both, &above, &below, &why))
    return Fail(why, outWhy);

  // The same cut face \ref SectionOutlines takes, asked a narrower question: is this one closed
  // ellipse? A tilted cut of a cylinder or cone is built as two half-sweeps of one ellipse
  // (`AddEllipse`), so the face's loop is exactly two `Ellipse` edges sharing a frame. Anything
  // else — a cut that also crosses an end cap, a marched curve — is not this, and says so.
  const Face* cut = nullptr;
  for (const Face& f : above.faces) {
    if (f.surface.kind != SurfaceKind::Plane)
      continue;
    if (std::fabs(std::fabs(ray3d::Dot(f.surface.frame.zAxis, frame.zAxis)) - 1.0) > 1e-9)
      continue;
    if (std::fabs(ray3d::Dot(ray3d::Sub(f.surface.frame.origin, planePoint), frame.zAxis)) > 1e-9)
      continue;
    if (cut)
      return Fail(Problem::SliceCutSeveralOutlines, outWhy);
    cut = &f;
  }
  if (!cut)
    return Fail(Problem::SlicePlaneMissesSolid, outWhy);
  if (cut->loops.size() != 1)
    return Fail(Problem::SectionHasHole, outWhy);

  const std::vector<EdgeUse>& uses = cut->loops.front().uses;
  double sweep = 0.0;
  const Edge* first = nullptr;
  for (const EdgeUse& u : uses) {
    const Edge& e = above.edges[static_cast<std::size_t>(u.edge)];
    if (e.kind != CurveKind::Ellipse)
      return Fail(Problem::SectionCurve, outWhy);
    if (!first)
      first = &e;
    else if (ray3d::Length(ray3d::Sub(e.frame.origin, first->frame.origin)) > 1e-9 * std::max(1.0, e.radius) ||
             std::fabs(e.radius - first->radius) > 1e-9 * std::max(1.0, e.radius) ||
             std::fabs(e.radius2 - first->radius2) > 1e-9 * std::max(1.0, e.radius))
      return Fail(Problem::SliceCutSeveralOutlines, outWhy);  // two different ellipses, not one
    sweep += std::fabs(e.sweep);
  }
  if (!first)
    return Fail(Problem::SliceResultInvalid, outWhy);
  // A closed ellipse, not an arc of one: the sweeps must add to a full turn. A cut that clips an end
  // cap has a chord as well, and is refused by name where it always was (#516).
  if (std::fabs(sweep - kTwoPi) > 1e-9)
    return Fail(Problem::SliceCutCrossesCurvedEnd, outWhy);

  SectionEllipse out;
  out.valid = true;
  out.centre = first->frame.origin;
  out.normal = frame.zAxis;  // the caller's own plane, as \ref SectionOutlines promises
  out.majorDir = first->frame.xAxis;
  out.majorSemi = first->radius;
  out.minorSemi = first->radius2;
  *outPlane = frame;
  *outEllipse = out;
  return Succeed(outWhy);
}

bool SectionOutlines(const Solid& solid, const Vec3& planePoint, const Vec3& planeNormal,
                     ucs::Ucs* outPlane, std::vector<Path>* outLoops, Problem* outWhy) {
  if (!outPlane || !outLoops)
    return Fail(Problem::IndexOutOfRange, outWhy);
  return SectionAllOutlines(solid, planePoint, planeNormal, outPlane, outLoops, nullptr, outWhy);
}

bool SectionLoop(const Solid& solid, const Vec3& planePoint, const Vec3& planeNormal,
                 ucs::Ucs* outPlane, Path* outLoop, Problem* outWhy) {
  if (!outPlane || !outLoop)
    return Fail(Problem::IndexOutOfRange, outWhy);
  std::vector<Path> loops;
  int faces = 0;
  if (!SectionAllOutlines(solid, planePoint, planeNormal, outPlane, &loops, &faces, outWhy))
    return false;
  // One outline is what this entry point promises. Which of the two ways it can be several matters
  // to the caller, so they are named apart: separate pieces of solid on the plane, or one piece with
  // a hole in it. \ref SectionOutlines returns both without refusing.
  if (faces > 1)
    return Fail(Problem::SliceCutSeveralOutlines, outWhy);
  if (loops.size() != 1)
    return Fail(Problem::SectionHasHole, outWhy);
  *outLoop = std::move(loops.front());
  return Succeed(outWhy);
}

} // namespace brep
