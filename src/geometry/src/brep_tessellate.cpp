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

/// Segment counts are clamped here rather than left to the tolerance alone: a chord tolerance of
/// 1e-12 on a survey-scale cylinder would otherwise ask for tens of millions of triangles and take
/// the frame budget with it (REQ-100).
constexpr int kMinArcSegments = 2;
constexpr int kMaxArcSegments = 512;

/// A full turn gets a much higher segment BUDGET than a small arc sliver needs, independent of
/// `tol/radius` (issue #486 GUI pass): the app's chord tolerance is a fixed absolute distance
/// (`kSolidChordToleranceFt`), so `tol/radius` — and with it the segment count — shrinks for any
/// small-to-moderate-radius circle, not just tiny ones; even an ordinary few-foot cylinder read as
/// visibly faceted. This is the ONE place a circle or an arc's parametrisation is turned into a
/// segment count (`SegmentsForArc`/`SegmentsForEdge`), so raising the floor here reaches every
/// curved solid face, every curved B-rep edge, AND the rubber-band edge preview (`TessellateEdges`,
/// which every EXTRUDE/PRESSPULL/CYLINDER/REVOLVE/LOFT/SWEEP ghost in CadRubberPreview.cpp draws
/// through) in one place, so none of them can drift back out of sync with each other. The budget is
/// PER FULL TURN, scaled by span in `SegmentsForArc` itself — not a step function on span — because
/// a circle is not always one edge: a cylinder's wall is built as two half-turn faces (see
/// BuildConical's `sideFace(0, kPi, ...)` / `sideFace(kPi, kTwoPi, ...)`, needed so a boolean/slice
/// always has an edge to cut at), while elsewhere the same circle can be one full 2*pi rim edge. A
/// step function keyed on "is this span nearly a full turn" gives EACH half-turn piece the same
/// absolute floor as the whole circle, doubling the total versus the unsplit case; scaling by span
/// keeps the per-turn budget constant regardless of how many pieces the circle is cut into.
///
/// 256, not 128: every ordinary EXTRUDE/PRESSPULL/CYLINDER wall is the two-half-turn-face case above,
/// so issue #486's tuning pass (done before the scaled floor existed) was implicitly tuned against
/// TWO applications of this constant — 256 segments per turn, not 128 — and a user-visible regression
/// confirmed 128 alone reads as faceted again at ordinary working zoom on a small (2 in radius) part.
/// The public default, aliased so the two cannot drift apart.
constexpr int kMinFullCircleSegments = kFullCircleSegments;

/// `[vLo, vHi]` at longitude \p u, or false where the meridian never enters the other surface (past
/// an end of the lens-shaped patch). Scans latitude for the two crossings and bisects each.
[[nodiscard]] bool SphereStripAt(const SphereIsectStrip& st, double u, double* vLo, double* vHi) {
  const double R = st.sf->radius;
  auto g = [&](double v) {
    return SignedDistToSurface(
        *st.other, ucs::UcsToWorld(st.sf->frame, Vec3{R * std::cos(v) * std::cos(u),
                                                      R * std::cos(v) * std::sin(u), R * std::sin(v)}));
  };
  double first = 0.0;
  double last = 0.0;
  int roots = 0;
  const int scan = 96;
  double vp = st.vSearchLo;
  double gp = g(vp);
  for (int i = 1; i <= scan; ++i) {
    const double vc = st.vSearchLo + (st.vSearchHi - st.vSearchLo) * i / scan;
    const double gc = g(vc);
    if ((gp <= 0.0) != (gc <= 0.0)) {
      double lo = vp;
      double hi = vc;
      double glo = gp;
      for (int k = 0; k < 46; ++k) {
        const double m = 0.5 * (lo + hi);
        const double gm = g(m);
        if ((glo <= 0.0) != (gm <= 0.0))
          hi = m;
        else {
          lo = m;
          glo = gm;
        }
      }
      const double root = 0.5 * (lo + hi);
      if (roots == 0)
        first = root;
      last = root;
      ++roots;
    }
    vp = vc;
    gp = gc;
  }
  if (roots == 1) {
    // The bite runs off one end of the search range — a polar cap (issue #242, the d < r sub-case).
    // `inside` above the lone root ⟹ the cap reaches the top of the range (a pole), and vice versa.
    if (g(st.vSearchHi) <= 0.0) {
      *vLo = first;
      *vHi = st.vSearchHi;
      return true;
    }
    if (g(st.vSearchLo) <= 0.0) {
      *vLo = st.vSearchLo;
      *vHi = first;
      return true;
    }
  }
  if (roots < 2)
    return false;
  *vLo = first;
  *vHi = last;
  return true;
}

// ---------------------------------------------------------------------------------------------
// Tessellation helpers.
// ---------------------------------------------------------------------------------------------

/// Segments needed so the sagitta of each chord stays within \p tol on a circle of \p radius.
[[nodiscard]] int SegmentsForArc(double radius, double spanRad, double tol,
                                 int fullCircleSegments = kMinFullCircleSegments) {
  const double span = std::fabs(spanRad);
  if (!(radius > 0.0) || !(span > 0.0))
    return 1;
  // The floor scales with span rather than switching on a near-full-turn threshold (issue: torn
  // bolt holes). A circle is not always one edge: a primitive's cylindrical wall is built as TWO
  // half-turn faces (the seamed pattern noted above), while a Phase 4 boolean can just as easily
  // leave the SAME circle as a single full 2*pi rim edge. Under the old step function both a
  // half-turn piece and a full-turn piece hit the SAME absolute floor (kMinFullCircleSegments), so
  // splitting a circle in two doubled its total vertex budget relative to the unsplit case — and
  // the two disagreed wherever a face tessellated one representation next to a loop that walked the
  // other (a bore wall next to its own hole's rim, cap vs. wall). Scaling the floor by span keeps
  // the total segment budget for one full turn constant (kMinFullCircleSegments) no matter how many
  // pieces it is cut into, so two neighbouring faces sampling the same circle always agree.
  const int minSegs = std::max(
      kMinArcSegments, static_cast<int>(std::llround(fullCircleSegments * span / kTwoPi)));
  if (tol >= radius)
    return minSegs;
  const double maxStep = 2.0 * std::acos(1.0 - tol / radius);
  if (!(maxStep > 0.0))
    return kMaxArcSegments;
  const int n = static_cast<int>(std::ceil(span / maxStep));
  return std::clamp(n, minSegs, kMaxArcSegments);
}

/// Segment count to walk a curved edge (Arc / Ellipse / Intersection) within \p tol; 1 for a line.
[[nodiscard]] int SegmentsForEdge(const Edge& e, double tol,
                                  int fullCircleSegments = kMinFullCircleSegments) {
  if (e.kind == CurveKind::Line)
    return 1;
  if (e.kind == CurveKind::Intersection) {
    // A quartic has no closed segment count. Use the tighter of the two surfaces' curvatures as a
    // proxy radius over a quarter turn — generous, and the marched walk itself is what stays on it.
    double rr = 1.0;
    for (const Surface& sf : e.isectSurfaces)
      if (sf.radius > 1e-9)
        rr = std::min(rr == 1.0 ? sf.radius : rr, sf.radius);
    return SegmentsForArc(rr, kHalfPi, tol, fullCircleSegments);
  }
  const double r = e.kind == CurveKind::Ellipse ? std::max(e.radius, e.radius2) : e.radius;
  return SegmentsForArc(r, e.sweep, tol, fullCircleSegments);
}

/// Curvature-only segment count for a NURBS patch's (u, v) grid (TASK-272 §5) — extracted so the
/// pre-pass below can fold this into the same shared-edge relaxation as the loop-edge-derived
/// counts, instead of a Nurbs face silently outrunning whatever count its neighbour settled on.
/// How finely one PARAMETRIC DIRECTION of \p patch has to be divided to stay within
/// \p chordTolerance — U when \p alongU, otherwise V.
///
/// **Per direction, and that is the whole point.** This used to take the largest gap between
/// consecutive control points over the WHOLE net and apply the resulting count to both directions.
/// The net is row-major (`ctrl[j * nu + i]`), so walking it linearly also strides across row
/// boundaries, and that stride is the distance between successive profiles — the length of the
/// SWEEP. A pipe tube swept along a 20 ft leg therefore measured 20 ft of "curvature", asked for the
/// 128-segment ceiling, and applied it to the ruled sweep direction as well as to the small circular
/// cross-section: ~128x128 quads per patch where ~12x1 is exact. Measured on a four-vertex 2in run:
/// 1,182,720 triangles and 16-34 SECONDS per solid in `Tessellate` (PIPEPERF, 2026-09-24).
///
/// A direction of degree 1 is RULED — the surface runs along straight lines in that parameter, even
/// rationally weighted — so one division is exact and no sampling can improve it. That is what this
/// function's own call site already claimed ("a ruled straight span is exact at one quad"); it was
/// simply never able to say so per direction.
///
/// The flatness fallback is kept for the one case degree alone does not settle: a patch that is
/// degree 1 in BOTH directions can still be a warped bilinear quad, whose middle bulges away from
/// the two triangles a single quad would emit. That is tested against the chord tolerance exactly as
/// before, and when it fails both directions are divided.
[[nodiscard]] int NurbsPatchDirectionSegs(const nurbs::Patch& patch, double chordTolerance, bool alongU,
                                          int fullCircleSegments = kMinFullCircleSegments) {
  const int nu = patch.nu;
  const int nv = patch.nv;
  if (nu < 2 || nv < 2 || patch.ctrl.size() != static_cast<std::size_t>(nu) * static_cast<std::size_t>(nv))
    return 1;
  const auto ctrlAt = [&](int i, int j) -> const Vec3& {
    return patch.ctrl[static_cast<std::size_t>(j) * static_cast<std::size_t>(nu) + static_cast<std::size_t>(i)];
  };
  // The largest control-net step IN THIS DIRECTION: along a row for U, down a column for V. Never
  // across a row boundary, which is the step that belongs to the other direction entirely.
  double netStep = 0.0;
  if (alongU) {
    for (int j = 0; j < nv; ++j)
      for (int i = 0; i + 1 < nu; ++i)
        netStep = std::max(netStep, ray3d::Length(ray3d::Sub(ctrlAt(i + 1, j), ctrlAt(i, j))));
  } else {
    for (int i = 0; i < nu; ++i)
      for (int j = 0; j + 1 < nv; ++j)
        netStep = std::max(netStep, ray3d::Length(ray3d::Sub(ctrlAt(i, j + 1), ctrlAt(i, j))));
  }

  bool curved = alongU ? patch.degU > 1 : patch.degV > 1;
  if (!curved && patch.degU == 1 && patch.degV == 1) {
    // Both directions ruled: the patch is a bilinear quad, exact at one division UNLESS it is
    // warped out of plane by more than the tolerance.
    const Vec3 e1 = ray3d::Sub(ctrlAt(1, 0), ctrlAt(0, 0));
    const Vec3 e2 = ray3d::Sub(ctrlAt(0, 1), ctrlAt(0, 0));
    Vec3 nrm = ray3d::Cross(e1, e2);
    const double nl = ray3d::Length(nrm);
    if (nl > 1e-12) {
      nrm = ray3d::Scale(nrm, 1.0 / nl);
      for (const Vec3& c : patch.ctrl)
        if (std::fabs(ray3d::Dot(ray3d::Sub(c, ctrlAt(0, 0)), nrm)) > chordTolerance) {
          curved = true;
          break;
        }
    }
  }
  return curved ? std::clamp(SegmentsForArc(std::max(netStep, 1e-9), kHalfPi, chordTolerance,
                                            fullCircleSegments),
                             std::min(8, fullCircleSegments), 128)
                : 1;
}

/// The U-direction division count — the one the face's RIM edges run along, and the count their
/// shared-edge relaxation (TASK-272 §5) has to agree with.
[[nodiscard]] int NurbsPatchCurvatureSegs(const nurbs::Patch& patch, double chordTolerance,
                                          int fullCircleSegments = kMinFullCircleSegments) {
  return NurbsPatchDirectionSegs(patch, chordTolerance, /*alongU=*/true, fullCircleSegments);
}

struct MeshBuilder {
  Tessellation* out = nullptr;
  /// Which face the triangles being emitted belong to. Set once per face by the loop below, so no
  /// emit site has to remember to pass it and none can pass the wrong one.
  int face = -1;

  std::uint32_t Push(const Vec3& p, const Vec3& n) {
    out->vertsXyz.push_back(p.x);
    out->vertsXyz.push_back(p.y);
    out->vertsXyz.push_back(p.z);
    out->normalsXyz.push_back(n.x);
    out->normalsXyz.push_back(n.y);
    out->normalsXyz.push_back(n.z);
    return static_cast<std::uint32_t>(out->vertsXyz.size() / 3) - 1;
  }

  void Tri(std::uint32_t a, std::uint32_t b, std::uint32_t c) {
    if (a == b || b == c || a == c)
      return;  // a pole fan's degenerate sliver; emitting it would only cost the GPU work
    out->indices.push_back(a);
    out->indices.push_back(b);
    out->indices.push_back(c);
    out->triFace.push_back(face);
  }
};

/// The outward unit normal of a cone/cylinder side at longitude \p t, in world. `Surface::inward`
/// (REQ-314 B2a) flips it: the face then bounds a bore, material on the −radial side.
[[nodiscard]] Vec3 ConicalNormal(const Surface& sf, double r0, double r1, double t) {
  const double k = (r0 - r1) / sf.height;
  const double inv = 1.0 / std::sqrt(1.0 + k * k);
  const double s = sf.inward ? -1.0 : 1.0;
  const Vec3 local{s * std::cos(t) * inv, s * std::sin(t) * inv, s * k * inv};
  return ucs::UcsVectorToWorld(sf.frame, local);
}

[[nodiscard]] Vec3 SphericalNormal(const Surface& sf, double t, double v) {
  const double cv = std::cos(v);
  const double s = sf.inward ? -1.0 : 1.0;
  return ucs::UcsVectorToWorld(sf.frame, Vec3{s * cv * std::cos(t), s * cv * std::sin(t), s * std::sin(v)});
}

[[nodiscard]] Vec3 ToroidalNormal(const Surface& sf, double t, double v) {
  const double cv = std::cos(v);
  const double s = sf.inward ? -1.0 : 1.0;
  return ucs::UcsVectorToWorld(sf.frame, Vec3{s * cv * std::cos(t), s * cv * std::sin(t), s * std::sin(v)});
}

// ---------------------------------------------------------------------------------------------
// Tessellation.
// ---------------------------------------------------------------------------------------------


/// Signed area of a 2D ring (CCW positive).
[[nodiscard]] double SignedArea2D(const std::vector<ucs::Point2D>& p) {
  double a = 0.0;
  const std::size_t n = p.size();
  for (std::size_t i = 0; i < n; ++i) {
    const ucs::Point2D& u = p[i];
    const ucs::Point2D& v = p[(i + 1) % n];
    a += u.x * v.y - v.x * u.y;
  }
  return 0.5 * a;
}

/// True when \p p turns the same way at every corner — a convex polygon, for which the centroid fan
/// below is exact. A straight-through vertex does not count against it.
[[nodiscard]] bool Polygon2DIsConvex(const std::vector<ucs::Point2D>& p) {
  const std::size_t n = p.size();
  if (n < 3)
    return false;
  double sign = 0.0;
  for (std::size_t i = 0; i < n; ++i) {
    const ucs::Point2D& a = p[i];
    const ucs::Point2D& b = p[(i + 1) % n];
    const ucs::Point2D& c = p[(i + 2) % n];
    const double cross = (b.x - a.x) * (c.y - b.y) - (b.y - a.y) * (c.x - b.x);
    if (std::fabs(cross) < 1e-14)
      continue;
    if (sign == 0.0)
      sign = cross;
    else if ((cross > 0.0) != (sign > 0.0))
      return false;
  }
  return true;
}

[[nodiscard]] bool PointInTriangle2D(const ucs::Point2D& p, const ucs::Point2D& a, const ucs::Point2D& b,
                                     const ucs::Point2D& c) {
  const double d1 = (p.x - b.x) * (a.y - b.y) - (a.x - b.x) * (p.y - b.y);
  const double d2 = (p.x - c.x) * (b.y - c.y) - (b.x - c.x) * (p.y - c.y);
  const double d3 = (p.x - a.x) * (c.y - a.y) - (c.x - a.x) * (p.y - a.y);
  const bool hasNeg = d1 < 0.0 || d2 < 0.0 || d3 < 0.0;
  const bool hasPos = d1 > 0.0 || d2 > 0.0 || d3 > 0.0;
  return !(hasNeg && hasPos);
}

/// Ear-clip a **CCW** 2D ring into triangles, each a triple of indices into \p ring. O(n^2), which
/// is ample for a profile cap. A profile is the first thing to hand \ref Tessellate a non-convex
/// plane face — ADR-045 named that "Phase 4's problem, when a boolean first produces a face that
/// needs one", and an extruded L-shape needs it now.
///
/// Returns false if a full pass finds no clippable ear before the ring is down to a triangle. A
/// valid simple polygon always has at least two ears (the standard "two ears theorem"), so that can
/// only happen from a genuinely malformed ring OR — this used to be the unhandled case — from the
/// exact-zero convexity/containment comparisons below spuriously rejecting an ear that numerically
/// exists but is very thin (a nearly-collinear triple, e.g. along a hole-to-outer-boundary bridge
/// slit). The old code treated "no ear found" as "stop early and fan whatever is left from one
/// point" — which for a non-star-shaped remainder produces self-overlapping triangles. That defect
/// was invisible to a total-signed-area check (the shoelace identity gives the same total for a
/// self-overlapping decomposition as for a valid one) and only showed up as a wrong VOLUME much
/// later, once the triangles were integrated in 3D — see issue "torn bolt holes" / TASK-272. Failing
/// loudly here instead turns that into an immediate, attributable refusal.
[[nodiscard]] bool EarClip(const std::vector<ucs::Point2D>& ring, std::vector<std::array<int, 3>>* tris) {
  const int n = static_cast<int>(ring.size());
  if (n < 3)
    return false;
  double minX = ring[0].x, maxX = ring[0].x, minY = ring[0].y, maxY = ring[0].y;
  for (const ucs::Point2D& p : ring) {
    minX = std::min(minX, p.x);
    maxX = std::max(maxX, p.x);
    minY = std::min(minY, p.y);
    maxY = std::max(maxY, p.y);
  }
  // A relative tolerance, scaled to the ring's own extent: an absolute epsilon would be meaningless
  // across the wide range of magnitudes this kernel tessellates at (a bolt-hole flange vs. a
  // survey-scale part), and too tight a tolerance is exactly what let a valid-but-thin ear near a
  // bridge slit get spuriously rejected in the first place.
  const double scale = std::max({maxX - minX, maxY - minY, 1.0});
  double areaEps = 1e-9 * scale * scale;
  double containEps = 1e-9 * scale;

  std::vector<int> idx(static_cast<std::size_t>(n));
  for (int i = 0; i < n; ++i)
    idx[static_cast<std::size_t>(i)] = i;

  int guard = 0;
  int stallEscalations = 0;
  // TASK-272 §9h: which ear gets clipped each pass used to always scan starting at index 0 — on a
  // large, mostly-convex boundary (a bridged multi-hole ring easily has 1000+ points), index 0 (or
  // whatever is now next to it) is very often IMMEDIATELY valid, so the scan kept re-clipping from
  // the same end every single pass instead of walking around the ring. That is a VALID triangulation
  // (every "two ears theorem" guarantee still holds — this was never a correctness bug, which is why
  // it passed watertightness, per-point radius, and self-intersection checks) but a geometrically
  // terrible one: it peels into a fan of thousands of long, thin sliver triangles converging on one
  // or two points, rather than a well-shaped mesh hugging the boundary. Confirmed directly by
  // rendering the actual dumped mesh for a real multi-hole flange face offline (see TASK-272 §9g) —
  // exactly this fan pattern, not a crack. Thin slivers are exactly what a GPU rasterizer can drop
  // sub-pixel coverage on at a grazing/foreshortened view angle, which is what read as "torn" edges
  // in Shaded view. Fix: resume scanning from where the LAST clip happened instead of restarting at
  // 0 every pass — the standard "walk the boundary" ear-clip technique — so clips distribute evenly
  // around the ring instead of always favouring whichever region sits near index 0.
  int scan = 0;
  while (idx.size() > 3 && guard++ < 4 * n) {
    const int m = static_cast<int>(idx.size());
    bool clipped = false;
    // TASK-272 §9m: taking the FIRST valid ear found (§9h's walk-the-ring fix) still lets a genuinely
    // thin ear win whenever it happens to be first in the scan window — confirmed directly on the
    // real flange: 6007 individual triangles with aspect ratio > 50, one at 4.4 MILLION, even with
    // §9h's fix already active, all still well-formed (watertight, correctly wound — never a
    // correctness bug) but thin enough that a GPU rasterizer can drop sub-pixel coverage on them,
    // leaving a visible gap at exactly the hole/outer rim a bridge slit runs along — the actual,
    // finally-confirmed cause of "torn" Shaded-view edges this whole task has been chasing. Fix:
    // look at up to `kQualityWindow` candidate ears from the current scan position (bounded, so the
    // overall algorithm stays close to its prior cost) and clip the BEST-shaped one (by min interior
    // angle, a standard sliver-quality proxy) instead of the first, falling back to first-found if
    // none of the window's candidates are notably better than "any valid ear" — this can only ever
    // improve triangle shape, never change which polygon gets triangulated or reject a valid ring.
    constexpr int kQualityWindow = 24;
    int bestI = -1, bestI0 = -1, bestI1 = -1, bestI2 = -1;
    double bestQuality = -1.0;
    for (int k = 0, tried = 0; k < m && tried < kQualityWindow; ++k) {
      const int i = (scan + k) % m;
      const int i0 = idx[static_cast<std::size_t>((i + m - 1) % m)];
      const int i1 = idx[static_cast<std::size_t>(i)];
      const int i2 = idx[static_cast<std::size_t>((i + 1) % m)];
      const ucs::Point2D& a = ring[static_cast<std::size_t>(i0)];
      const ucs::Point2D& b = ring[static_cast<std::size_t>(i1)];
      const ucs::Point2D& c = ring[static_cast<std::size_t>(i2)];
      const double area2 = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
      if (area2 <= areaEps)
        continue;  // reflex or (numerically) straight — not an ear tip
      bool contains = false;
      for (int j = 0; j < m && !contains; ++j) {
        const int ij = idx[static_cast<std::size_t>(j)];
        if (ij == i0 || ij == i1 || ij == i2)
          continue;
        // A point exactly ON the ear's boundary (common right at a bridge slit, where a duplicated
        // vertex from the other side of the slit can sit precisely on this edge) must NOT count as
        // "inside" — that is what made a genuinely valid ear look blocked. `PointInTriangle2D`'s
        // strict signs already treat an exact zero as neither positive nor negative, which is
        // correct; padding the SIGN test itself in `containEps` would instead misclassify a
        // truly-inside point near the edge as outside, so only the containment point's PROXIMITY to
        // each of the triangle's own edges is padded, not the sign test.
        const ucs::Point2D& q = ring[static_cast<std::size_t>(ij)];
        const bool onA = ray3d::Length(ray3d::Sub(Vec3{q.x, q.y, 0}, Vec3{a.x, a.y, 0})) < containEps;
        const bool onB = ray3d::Length(ray3d::Sub(Vec3{q.x, q.y, 0}, Vec3{b.x, b.y, 0})) < containEps;
        const bool onC = ray3d::Length(ray3d::Sub(Vec3{q.x, q.y, 0}, Vec3{c.x, c.y, 0})) < containEps;
        if (onA || onB || onC)
          continue;  // coincides with a triangle corner (a bridge duplicate) — not "inside"
        contains = PointInTriangle2D(q, a, b, c);
      }
      if (contains)
        continue;
      ++tried;
      // Quality proxy: area / (perimeter^2), scale-free and maximized by an equilateral triangle,
      // near zero for a sliver — cheap (no acos/atan2) and monotonic with "thinness" for this purpose.
      const double ab = ray3d::Length(ray3d::Sub(Vec3{b.x, b.y, 0}, Vec3{a.x, a.y, 0}));
      const double bc = ray3d::Length(ray3d::Sub(Vec3{c.x, c.y, 0}, Vec3{b.x, b.y, 0}));
      const double ca = ray3d::Length(ray3d::Sub(Vec3{a.x, a.y, 0}, Vec3{c.x, c.y, 0}));
      const double perim = ab + bc + ca;
      const double quality = perim > 1e-300 ? area2 / (perim * perim) : 0.0;
      if (bestI < 0 || quality > bestQuality) {
        bestQuality = quality;
        bestI = i;
        bestI0 = i0;
        bestI1 = i1;
        bestI2 = i2;
      }
    }
    if (bestI >= 0) {
      tris->push_back({bestI0, bestI1, bestI2});
      idx.erase(idx.begin() + bestI);
      // Resume next pass's scan right where this clip happened (mod the now-one-shorter ring),
      // instead of jumping back to index 0 — this is what keeps clips walking around the boundary.
      scan = (m > 1) ? bestI % (m - 1) : 0;
      clipped = true;
    }
    if (!clipped) {
      // By the "two ears theorem" a valid simple polygon always has at least two clippable ears, so
      // a full pass finding none — on a polygon this large (a multi-hole bridge easily reaches into
      // the thousands of vertices) — is far more often float precision defeating the strict tests
      // above than a genuinely malformed ring. Loosen the tolerance by 10x and retry the SAME
      // remaining ring before concluding it is actually malformed; capped, so a truly bad ring still
      // gets refused rather than silently accepted at an absurdly coarse tolerance.
      if (stallEscalations < 6) {
        areaEps *= 10.0;
        containEps *= 10.0;
        ++stallEscalations;
        continue;
      }
      return false;  // a genuinely malformed ring (self-intersecting or degenerate) — refuse
    }
    stallEscalations = 0;  // progress was made; the next stall gets its own fresh escalation budget
  }
  for (std::size_t i = 1; i + 1 < idx.size(); ++i)
    tris->push_back({idx[0], idx[i], idx[i + 1]});
  // The final `idx.size() == 3` fan above (and, rarely, a triangle clipped just before it) can be
  // degenerate when two of the three remaining points are a bridge's exact-duplicate pair (`M` or
  // `target`, re-emitted to close a hole's slit — see `BridgeHoleIntoOuter`) that never got
  // consumed as an ear along the way. A zero-area triangle contributes nothing to area or volume,
  // so dropping it changes nothing about the triangulation except removing a spurious duplicate
  // triangle edge — this is what left the very last handful of cracked mesh edges after the
  // robustness fix above (verified via a `triFace`-tagged dump: the crack was one degenerate
  // triangle, with two coincident vertices, double-booking one real edge).
  tris->erase(std::remove_if(tris->begin(), tris->end(),
                             [&](const std::array<int, 3>& t3) {
                               const ucs::Point2D& a = ring[static_cast<std::size_t>(t3[0])];
                               const ucs::Point2D& b = ring[static_cast<std::size_t>(t3[1])];
                               const ucs::Point2D& c = ring[static_cast<std::size_t>(t3[2])];
                               const double area2 =
                                   (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
                               return std::fabs(area2) <= areaEps;
                             }),
              tris->end());
  return true;
}

/// TASK-272 §9p: local Delaunay-style edge-flip pass over an `EarClip` triangulation.
///
/// Ear-clipping a SIMPLE POLYGON necessarily removes 3 CONSECUTIVE boundary vertices at a time — for
/// a finely-sampled near-circular boundary (any real bolt hole or bore rim at a tight chord
/// tolerance), that is structurally a thin sliver almost every time (three consecutive points on a
/// fine circle are close to collinear by construction), independent of which valid ear the clipper
/// happens to pick. Confirmed directly on the real reported flange: even scanning the ENTIRE
/// remaining ring each pass for the best-quality available ear (the `kQualityWindow` widening tried
/// first) left the worst offender completely unchanged — proof the problem is not "which ear got
/// picked" but the one-consecutive-triple-at-a-time nature of ear-clipping itself. These slivers are
/// never a CORRECTNESS bug (watertight, correctly wound — confirmed independently), but a triangle
/// thin enough interpolates its vertex normals across a huge, near-degenerate footprint, which is
/// what actually read as "torn" Shaded-view edges right at hole/bore rims specifically (confirmed:
/// raising the Shaded shader's ambient floor smoothed the plain OUTER rim — ordinary curvature
/// shading — but left the bolt holes and bore exactly as torn, since those are the only boundaries
/// this bridge+EarClip path ever triangulates).
///
/// The standard fix for "valid mesh, bad triangle shapes" is a LOCAL edge flip: for every interior
/// edge shared by two triangles (forming a quadrilateral), replace it with the OTHER diagonal when
/// that diagonal gives a better-shaped pair — this changes triangle SHAPE only, never the boundary,
/// the vertex set, the winding, or the covered area, so it cannot affect watertightness, volume, or
/// which triangle belongs to which face. Bounded to a fixed number of sweeps (a full Delaunay
/// convergence is not needed — only removing the SEVERE outliers is), and gated by convexity (a flip
/// across a reflex quad would leave the polygon self-intersecting) and a real quality improvement
/// (never flips a good pair into a worse one).
void ImproveTriangulationQuality(const std::vector<ucs::Point2D>& ring,
                                  std::vector<std::array<int, 3>>* tris) {
  auto triQuality = [&](int a, int b, int c) {
    const ucs::Point2D& pa = ring[static_cast<std::size_t>(a)];
    const ucs::Point2D& pb = ring[static_cast<std::size_t>(b)];
    const ucs::Point2D& pc = ring[static_cast<std::size_t>(c)];
    const double area2 = (pb.x - pa.x) * (pc.y - pa.y) - (pb.y - pa.y) * (pc.x - pa.x);
    const double ab = std::hypot(pb.x - pa.x, pb.y - pa.y);
    const double bc = std::hypot(pc.x - pb.x, pc.y - pb.y);
    const double ca = std::hypot(pa.x - pc.x, pa.y - pc.y);
    const double perim = ab + bc + ca;
    return perim > 1e-300 ? area2 / (perim * perim) : 0.0;  // area2 keeps the CCW-positive sign
  };
  // Map an undirected edge (lo, hi) to the up-to-two triangle indices that use it, and which corner
  // (opposite vertex) each one contributes — enough to reconstruct both candidate diagonals.
  std::map<std::pair<int, int>, std::array<int, 2>> edgeTris;  // {-1,-1} default = unseen
  auto edgeKey = [](int u, int v) { return u < v ? std::make_pair(u, v) : std::make_pair(v, u); };
  for (std::size_t ti = 0; ti < tris->size(); ++ti) {
    const std::array<int, 3>& t3 = (*tris)[ti];
    for (int e = 0; e < 3; ++e) {
      const int u = t3[static_cast<std::size_t>(e)];
      const int v = t3[static_cast<std::size_t>((e + 1) % 3)];
      std::array<int, 2>& slot = edgeTris[edgeKey(u, v)];
      if (slot[0] < 0)
        slot[0] = static_cast<int>(ti);
      else
        slot[1] = static_cast<int>(ti);
    }
  }
  for (int pass = 0; pass < 6; ++pass) {
    bool changedAny = false;
    for (auto& [key, slot] : edgeTris) {
      if (slot[0] < 0 || slot[1] < 0)
        continue;  // a boundary edge (or a bridge slit edge) — only one triangle uses it, never flip
      std::array<int, 3>& t0 = (*tris)[static_cast<std::size_t>(slot[0])];
      std::array<int, 3>& t1 = (*tris)[static_cast<std::size_t>(slot[1])];
      // Find the two "opposite" corners (the quad's other diagonal) and confirm the shared edge is
      // really (key.first, key.second) oriented oppositely in each triangle, as a consistent
      // 2-manifold triangulation guarantees.
      auto oppositeOf = [&](const std::array<int, 3>& t, int lo, int hi) {
        for (int e = 0; e < 3; ++e) {
          const int u = t[static_cast<std::size_t>(e)];
          const int v = t[static_cast<std::size_t>((e + 1) % 3)];
          if ((u == lo && v == hi) || (u == hi && v == lo))
            return t[static_cast<std::size_t>((e + 2) % 3)];
        }
        return -1;
      };
      const int p = oppositeOf(t0, key.first, key.second);
      const int q = oppositeOf(t1, key.first, key.second);
      if (p < 0 || q < 0 || p == q)
        continue;
      // Only ever flip a CONVEX quad — a reflex one flipped would self-intersect. Convexity here
      // means p and q lie on opposite sides of edge (key.first,key.second) (guaranteed by the two
      // triangles sharing it with opposite winding) AND key.first/key.second lie on opposite sides
      // of the candidate new diagonal (p, q).
      const ucs::Point2D& P = ring[static_cast<std::size_t>(p)];
      const ucs::Point2D& Q = ring[static_cast<std::size_t>(q)];
      const ucs::Point2D& Lo = ring[static_cast<std::size_t>(key.first)];
      const ucs::Point2D& Hi = ring[static_cast<std::size_t>(key.second)];
      const double sideLo = (Q.x - P.x) * (Lo.y - P.y) - (Q.y - P.y) * (Lo.x - P.x);
      const double sideHi = (Q.x - P.x) * (Hi.y - P.y) - (Q.y - P.y) * (Hi.x - P.x);
      if (sideLo * sideHi >= 0.0)
        continue;  // not a convex quad — flipping would self-intersect
      // Current pairing's worst quality vs. the flipped pairing's worst quality; flip only on a
      // real improvement, never sideways or for a worse split (this can only ever raise quality).
      const double q0a = std::fabs(triQuality(key.first, p, key.second));
      const double q0b = std::fabs(triQuality(key.second, q, key.first));
      const double q1a = std::fabs(triQuality(p, q, key.second));
      const double q1b = std::fabs(triQuality(q, p, key.first));
      const double before = std::min(q0a, q0b);
      const double after = std::min(q1a, q1b);
      if (after <= before * 1.05)
        continue;  // require a clear win (5% margin), not just noise-level — never flip sideways
      // Rebuild both triangles around the new diagonal (p, q). Don't assume which of the two
      // orderings is CCW (that depends on which side of the original edge p fell on, which this
      // function does not track) — derive it from the signed area directly, the same quantity
      // `triQuality` already computes, so a flip can never silently introduce an inverted normal.
      std::array<int, 3> newT0 = {p, key.second, q};
      std::array<int, 3> newT1 = {q, key.first, p};
      if (triQuality(newT0[0], newT0[1], newT0[2]) < 0.0)
        std::swap(newT0[1], newT0[2]);
      if (triQuality(newT1[0], newT1[1], newT1[2]) < 0.0)
        std::swap(newT1[1], newT1[2]);
      t0 = newT0;
      t1 = newT1;
      changedAny = true;
    }
    if (!changedAny)
      break;
  }
}

/// Splices \p hole into \p outer as a zero-width slit (Held's bridging technique), so the two rings
/// become one simple polygon `EarClip` can triangulate exactly. \p hole must already be wound
/// opposite \p outer (CW inside a CCW outer, the usual even-odd hole convention) — the caller is
/// responsible for that, since it can cheaply check once per loop rather than this being re-derived
/// per bridge. \p awayFrom is a point well inside the shape (the ORIGINAL outer ring's own centroid,
/// computed once by the caller before any bridging, not recomputed per hole) that the bridge
/// direction points away from.
///
/// Finds the hole's own extremal vertex in the direction `hole centroid - awayFrom` — "outward",
/// radially — and ray-casts further in that SAME direction to the nearest edge of the CURRENT
/// `outer` (so a second, third, ... hole bridged afterward sees earlier bridges as part of the
/// boundary and cannot cross them), connecting to that edge's farther-along-the-ray endpoint. A
/// fixed cast direction (this used to always be +X) works for exactly one hole, but for several
/// holes arranged around a shape — a bolt circle, the actual case that motivated this function — a
/// hole on the far side casting toward the SAME fixed direction has to cross the shape's interior,
/// and can cross another hole or an earlier bridge on the way, which `EarClip` then correctly (if
/// unhelpfully) refuses as a self-intersecting input. Cast radially instead, and every hole's bridge
/// points a different way, like spokes — for a bolt-circle-shaped arrangement (the common case)
/// spokes from distinct holes do not cross each other or pass through a third hole in between.
///
/// This still assumes the target vertex is visible along the ray from the hole's own extremal point
/// — true whenever the outer boundary has no reflex vertex in the way, which holds for every convex
/// or near-convex outer boundary this kernel's callers produce (a flange, a plate, an extruded
/// profile); a pathological concave outer boundary, or holes clustered close enough together that
/// their own radial spokes cross, could still defeat it — the caller is expected to treat a `false`
/// return as "fall back to a different tessellation strategy", never as fatal.
///
/// The bridge is left an EXACT zero-width slit (the closing pair duplicates the hole's own extremal
/// point and the target precisely) — `EarClip`'s own robustness (proximity-to-corner skip in its
/// containment test) is what makes that safe, rather than perturbing the duplicate points, which was
/// tried first and did not address the actual defect (see TASK-272).
[[nodiscard]] bool BridgeHoleIntoOuter(std::vector<ucs::Point2D>* outer,
                                       const std::vector<ucs::Point2D>& hole,
                                       const ucs::Point2D& awayFrom) {
  if (hole.size() < 3 || outer->size() < 3)
    return false;

  ucs::Point2D holeC{0.0, 0.0};
  double holeExtent = 0.0;
  for (const ucs::Point2D& p : hole) {
    holeC.x += p.x / static_cast<double>(hole.size());
    holeC.y += p.y / static_cast<double>(hole.size());
  }
  for (const ucs::Point2D& p : hole)
    holeExtent = std::max(holeExtent, std::hypot(p.x - holeC.x, p.y - holeC.y));
  double dx = holeC.x - awayFrom.x;
  double dy = holeC.y - awayFrom.y;
  double dlen = std::sqrt(dx * dx + dy * dy);
  // TASK-272 §9i: a CONCENTRIC hole (a flange's own central bore, sharing the outer ring's axis) has
  // `holeC` and `awayFrom` both landing on — or, after only floating-point summation noise, a hair's
  // breadth from — the same true centre point. The old check only caught an EXACT (or near-exact,
  // 1e-12 absolute) zero; a "concentric but not bit-identical" case (near-guaranteed: `holeC` and
  // `awayFrom` are each an independent average over a DIFFERENT sample count/spacing of two
  // DIFFERENT circles, so they only agree to summation-noise precision, not exactly) left `dlen` a
  // tiny but nonzero number — and normalizing (dx, dy) by a near-zero `dlen` amplifies THAT noise
  // into an effectively arbitrary, numerically unstable ray direction, rather than the safe, fixed
  // fallback direction the exact-zero case already had. Confirmed as the actual cause of a torn bore
  // rim on a real flange (RenderDoc frame capture of the live GPU output — see TASK-272 §9g/§9h/§9i
  // — the 4 well-separated bolt holes rendered perfectly clean, only the CONCENTRIC bore tore, which
  // is exactly what a direction-instability bug predicts and a general triangulation-quality bug
  // does not). Compare against the hole's own SIZE, not an absolute constant, so this catches
  // "concentric to within noise" robustly regardless of the part's overall scale.
  if (dlen < 1e-6 * std::max(holeExtent, 1e-9)) {
    dx = 1.0;
    dy = 0.0;
    dlen = 1.0;
  }
  dx /= dlen;
  dy /= dlen;

  std::size_t mi = 0;
  double bestDot = hole[0].x * dx + hole[0].y * dy;
  for (std::size_t i = 1; i < hole.size(); ++i) {
    const double d = hole[i].x * dx + hole[i].y * dy;
    if (d > bestDot) {
      bestDot = d;
      mi = i;
    }
  }
  const ucs::Point2D M = hole[mi];

  // Ray-cast M + t*(dx,dy), t > 0, against every edge of the current outer ring; keep the nearest.
  double bestT = std::numeric_limits<double>::infinity();
  std::size_t bestEdge = SIZE_MAX;
  const std::size_t n = outer->size();
  for (std::size_t i = 0; i < n; ++i) {
    const ucs::Point2D& a = (*outer)[i];
    const ucs::Point2D& b = (*outer)[(i + 1) % n];
    const double ex = b.x - a.x;
    const double ey = b.y - a.y;
    const double den = dx * ey - dy * ex;
    if (std::fabs(den) < 1e-15)
      continue;  // parallel to the ray
    const double t = ((a.x - M.x) * ey - (a.y - M.y) * ex) / den;
    const double s = ((a.x - M.x) * dy - (a.y - M.y) * dx) / den;
    if (t > 1e-12 && s >= -1e-9 && s <= 1.0 + 1e-9 && t < bestT) {
      bestT = t;
      bestEdge = i;
    }
  }
  if (bestEdge == SIZE_MAX)
    return false;  // M's radial ray never left the shape (shouldn't happen for a validated solid)

  const ucs::Point2D& a = (*outer)[bestEdge];
  const ucs::Point2D& b = (*outer)[(bestEdge + 1) % n];
  const double aDot = a.x * dx + a.y * dy;
  const double bDot = b.x * dx + b.y * dy;
  const std::size_t targetIdx = (aDot >= bDot) ? bestEdge : (bestEdge + 1) % n;
  const ucs::Point2D target = (*outer)[targetIdx];

  // TASK-272 §9q: a RenderDoc capture of the real reported file confirmed the "torn" Shaded pixels
  // are genuine sub-pixel rasterization dropout — a real, gap-free, but extremely thin (aspect ratio
  // in the millions) triangle at a bridge slit that a GPU's fixed MSAA sample positions can miss
  // entirely, letting the black background bleed through. A fix was attempted here (thickening the
  // slit into a tiny real-width corridor) but it changed which side of the bridge is "inside" the
  // polygon for at least one real hole arrangement, measurably CRACKING the real flange import (120
  // of 18540 edges, was 0) — worse than the rasterization artifact it targeted. Reverted rather than
  // ship a regression; see TASK-272 §9.6/§9q for the root cause and what a correct fix needs to
  // preserve (the polygon's covered AREA, not just its vertex count).
  //
  // TASK-272 §9r (2026-09-23): a second attempt — keep `M`/`target` exactly in place and instead bow
  // the corridor's two RAILS apart only in the middle (tapering to zero width at both ends, so the
  // shared boundary points stay bit-exact) — was tried and is ALSO wrong, for a reason that's
  // structural, not a tuning mistake: confirmed by testing two corridor widths 100x apart and getting
  // the identical cracked-edge count (8720 of 30456) and identical worst aspect ratio (97784.1) both
  // times, which only makes sense if the width value never mattered. It doesn't, because of how this
  // whole scheme's watertightness actually works: the ORIGINAL zero-width slit passes watertightness
  // only because the outbound edge (target -> M) and the return edge (M -> target) are POSITIONALLY
  // IDENTICAL, so the test's quantize-by-position edge-count happens to see "2 uses of the same
  // edge" even though it's really one degenerate fold-back, not a real second triangle. The instant
  // the two rails stop being positionally identical (any nonzero bow, in any direction, any width),
  // every one of those newly-distinct edges becomes a real boundary edge used only ONCE — nothing
  // else in the mesh was ever built to be its twin, since a bridge is internal to one face's own
  // EarClip ring and no neighbouring face's geometry has any reason to sample this corridor at all.
  // A single simple-polygon ring literally cannot represent a real-width, still-watertight corridor
  // this way: doing so needs FOUR distinct rail points forming a true quadrilateral loop with an
  // internal diagonal of its own, not two rails that only work by coinciding. Reverted; do not retry
  // this shape of fix again without addressing that structural point first. The likely correct place
  // for this fix is downstream of `Tessellate`'s topology entirely: a render-only vertex nudge (or a
  // screen-space "minimum triangle coverage" technique) applied to the GPU vertex buffer, which does
  // not need to preserve the shared-edge-count-of-2 invariant the CSG/volume-facing mesh does.
  std::vector<ucs::Point2D> result;
  result.reserve(n + hole.size() + 2);
  for (std::size_t i = 0; i < n; ++i) {
    result.push_back((*outer)[i]);
    if (i == targetIdx) {
      // The hole loop below already starts AT `mi` (j == 0 gives `hole[mi] == M`), so pushing `M`
      // again here first would insert it three times total (this one, the loop's own first point,
      // and the closing M below) with a zero-length edge between the first two — confirmed as the
      // actual cause of an EarClip stall on a multi-hole face: not a self-intersection (checked
      // directly, none), just a redundant degenerate point compounding once per bridged hole.
      for (std::size_t j = 0; j < hole.size(); ++j)
        result.push_back(hole[(mi + j) % hole.size()]);
      result.push_back(M);
      result.push_back(target);
    }
  }
  *outer = std::move(result);
  return true;
}

/// World-space position of \p sf at parameter `(u, v)`, via the same `LocalSurfaceDerivs` the
/// general-loop mass-property integral (issue #307) uses — so the tessellator's notion of the
/// surface matches the numeric integral's exactly, which is what lets #308's area test compare
/// the two.
[[nodiscard]] Vec3 GeneralLoopWorldPoint(const Surface& sf, double u, double v) {
  Vec3 p, su, sv;
  LocalSurfaceDerivs(sf, u, v, &p, &su, &sv);
  return ucs::UcsToWorld(sf.frame, p);
}

/// Recursive chord-deviation half of \ref GeneralLoopChordSegments: `depth` bounds it to `2^10`
/// segments per call even on a pathological span (e.g. a paramLoop edge that runs a full 2*pi).
int GeneralLoopChordDepth(const Surface& sf, curveisect::Vec2 pa, curveisect::Vec2 pb, double tol,
                          int depth) {
  if (depth >= 10)
    return 1;
  const curveisect::Vec2 pm{0.5 * (pa.x + pb.x), 0.5 * (pa.y + pb.y)};
  const Vec3 a = GeneralLoopWorldPoint(sf, pa.x, pa.y);
  const Vec3 b = GeneralLoopWorldPoint(sf, pb.x, pb.y);
  const Vec3 m = GeneralLoopWorldPoint(sf, pm.x, pm.y);
  const Vec3 mid = ray3d::Scale(ray3d::Add(a, b), 0.5);
  if (ray3d::Length(ray3d::Sub(m, mid)) <= tol)
    return 1;
  return GeneralLoopChordDepth(sf, pa, pm, tol, depth + 1) + GeneralLoopChordDepth(sf, pm, pb, tol, depth + 1);
}

/// How many straight segments the parameter-space chord `pa`-`pb`, evaluated through \p sf, needs
/// so no sub-chord deviates from the true surface by more than \p tol — the general-loop analogue
/// of `SegmentsForArc`'s role in the rectangle grid path, but adaptive rather than closed-form so
/// it works for every `SurfaceKind` including `Nurbs`.
[[nodiscard]] int GeneralLoopChordSegments(const Surface& sf, curveisect::Vec2 pa, curveisect::Vec2 pb,
                                           double tol) {
  return GeneralLoopChordDepth(sf, pa, pb, tol, 0);
}

/// Tessellates a face carrying a general trim loop (`Face::paramLoops`, ADR-052, issue #306), for
/// any `SurfaceKind` — the triangulation counterpart to issue #307's numeric integral. It reuses
/// that integral's exact decomposition: break `v` at every loop vertex (so the number of
/// even-odd "inside" intervals from \ref GeneralLoopScanline is constant within a band), which
/// turns each interval, per band, into a trapezoid whose four corners are real loop points and
/// whose sides are the loop's own straight edges — an exact fit to the trim boundary, holes
/// excluded automatically by the even-odd rule. Each trapezoid is then filled with a
/// chord-tolerance grid exactly like the rectangle path's, so a curved surface still tessellates
/// finely inside the loop, not just accurately along its edge.
void TessellateGeneralLoopFace(const Face& f, double chordTolerance, MeshBuilder* mb, const Solid* s = nullptr,
                                const std::map<int, int>* edgeSegs = nullptr) {
  const Surface& sf = f.surface;
  if (f.paramLoops.empty())
    return;

  // TASK-272 §9l: an extremal band row (v == the face's own overall v0/v1, i.e. a real rim, not an
  // interior hole pinch) that corresponds to one whole rim edge is raised to that edge's UNIFIED
  // `segsForEdge` count — the same count `Tessellate`'s `reconstructParamLoop2D` now uses (§9.4,
  // §9l) for the neighbouring Plane cap's own hole/outer boundary on the SAME shared edge. Confirmed
  // by direct measurement that doing only one side or the other makes cracking WORSE, not better
  // (§9.4's reverted attempt, and this section's own first try): the two sides must land on the same
  // final count, not just a higher one. Only ever raises (never lowers) `nCols` — the same monotonic
  // safety rule as every other resolution fix in this task.
  auto localV = [&](const Vec3& w) {
    const Vec3 l = ucs::WorldToUcs(sf.frame, w);
    return sf.kind == SurfaceKind::Plane ? l.y : l.z;
  };
  auto segsForEdgeLocal = [&](int edgeIdx, const Edge& e) {
    if (edgeSegs) {
      const auto it = edgeSegs->find(edgeIdx);
      if (it != edgeSegs->end())
        return it->second;
    }
    return SegmentsForEdge(e, chordTolerance);
  };
  std::vector<std::pair<double, int>> rimEdges;
  if (s && edgeSegs && !f.loops.empty()) {
    const double vTol = 1e-6 * std::max(1.0, std::fabs(f.vStart) + std::fabs(f.vEnd));
    for (const EdgeUse& u : f.loops[0].uses) {
      const Edge& e = s->edges[static_cast<std::size_t>(u.edge)];
      if (e.kind == CurveKind::Line)
        continue;
      const double v0v = localV(EdgePointAt(*s, e, 0.0));
      const double v1v = localV(EdgePointAt(*s, e, 1.0));
      const double vMid = localV(EdgePointAt(*s, e, 0.5));
      if (std::fabs(v0v - v1v) < vTol && std::fabs(v0v - vMid) < vTol)
        rimEdges.emplace_back(v0v, u.edge);
    }
  }
  auto findRimEdgeAt = [&](double v) -> int {
    const double vTol = 1e-6 * std::max(1.0, std::fabs(f.vStart) + std::fabs(f.vEnd));
    for (const auto& [rv, edgeIdx] : rimEdges)
      if (std::fabs(rv - v) < vTol)
        return edgeIdx;
    return -1;
  };

  std::vector<double> vBreaks;
  for (const std::vector<curveisect::Vec2>& poly : f.paramLoops)
    for (const curveisect::Vec2& p : poly)
      vBreaks.push_back(p.y);
  if (vBreaks.size() < 2)
    return;
  std::sort(vBreaks.begin(), vBreaks.end());
  vBreaks.erase(std::unique(vBreaks.begin(), vBreaks.end(),
                            [](double a, double b) { return std::fabs(a - b) < 1e-12 * (1.0 + std::fabs(a)); }),
                vBreaks.end());
  if (vBreaks.size() < 2)
    return;

  const double normalSign = sf.inward ? -1.0 : 1.0;
  auto pushVertex = [&](double u, double v) {
    Vec3 p, su, sv;
    LocalSurfaceDerivs(sf, u, v, &p, &su, &sv);
    const Vec3 n = ray3d::Scale(ray3d::Normalize(ray3d::Cross(su, sv)), normalSign);
    return mb->Push(ucs::UcsToWorld(sf.frame, p), ucs::UcsVectorToWorld(sf.frame, n));
  };

  // `nCols` for a band used to be chosen from that band's OWN two horizontal rows alone — nothing
  // tied it to the row a neighbouring band computes for the SAME shared boundary (band i's `v1` row
  // is band i+1's `v0` row). Each band's count was `max(its own bottom need, its own top need)`, and
  // those "own" needs are generally different between two bands that only share ONE of their two
  // rows, so the shared row could legitimately get a different column count on each side of it — a
  // T-junction crack inside a single face's own triangulation (confirmed on a box-minus-cylinder
  // bore: the cracked mesh edges all had only one triangle touching them, i.e. the crack was WITHIN
  // one face, not between two faces as first suspected). Fixed with a small pre-pass: gather every
  // band's raw per-row needs first, resolve each SHARED boundary to the max of what either side
  // independently asked for (only where the two sides' interval topology actually matches — same
  // count of inside/outside crossings — since a topology change at a v-break, e.g. a hole starting,
  // means the two sides legitimately don't correspond row-for-row), then build geometry using those
  // resolved counts. This only ever RAISES a band's resolution versus the old per-band-only formula,
  // never lowers it, so it cannot make an already-fine face coarser.
  struct BandInfo {
    double v0 = 0.0, v1 = 0.0;
    std::vector<double> xs0, xs1;
  };
  std::vector<BandInfo> bands;
  bands.reserve(vBreaks.size());
  for (std::size_t bi = 0; bi + 1 < vBreaks.size(); ++bi) {
    const double v0 = vBreaks[bi];
    const double v1 = vBreaks[bi + 1];
    // `GeneralLoopScanline`'s half-open `a.y <= v` convention correctly avoids double-counting an
    // edge shared by two bands, but it means a scan taken exactly AT a band's own v0/v1 (rather
    // than strictly between them) can miss a crossing that lies exactly on the scanline — most
    // visibly the outermost band's own outer edge. Sampling a hair inside the band sidesteps that
    // without changing which edges bound this band (guaranteed constant within it by `vBreaks`).
    const double eps = 1e-9 * std::max(1.0, v1 - v0);
    BandInfo bandInfo;
    bandInfo.v0 = v0;
    bandInfo.v1 = v1;
    bandInfo.xs0 = GeneralLoopScanline(f.paramLoops, v0 + eps);
    bandInfo.xs1 = GeneralLoopScanline(f.paramLoops, v1 - eps);
    bands.push_back(std::move(bandInfo));
  }

  // Per band, per interval k: the column count that band would use on its own (its old, purely
  // local `max(bottom need, top need)`). `nCols` is ONE number applied to a band's WHOLE grid — both
  // its rows — so raising just the row-level "need" at a shared boundary is not enough: if band i's
  // resolution is pinned high by its OWN unrelated bottom row, that higher count applies to band i's
  // top row too, and band i+1 must match THAT (not just band i's raw top-row need). Two relaxation
  // sweeps (forward then backward) propagate the max through every run of bands whose interval
  // topology matches its neighbour, so the whole run converges on one shared count — like a
  // union-find over a simple chain.
  std::vector<std::vector<int>> nColsFor(bands.size());
  for (std::size_t bi = 0; bi < bands.size(); ++bi) {
    const BandInfo& b = bands[bi];
    if (b.xs0.size() != b.xs1.size() || b.xs0.size() < 2)
      continue;
    nColsFor[bi].assign(b.xs0.size() / 2, 1);
    for (std::size_t k = 0; k + 1 < b.xs0.size(); k += 2) {
      if (b.xs0[k + 1] <= b.xs0[k] && b.xs1[k + 1] <= b.xs1[k])
        continue;
      const int botNeed = GeneralLoopChordSegments(sf, {b.xs0[k], b.v0}, {b.xs0[k + 1], b.v0}, chordTolerance);
      const int topNeed = GeneralLoopChordSegments(sf, {b.xs1[k], b.v1}, {b.xs1[k + 1], b.v1}, chordTolerance);
      nColsFor[bi][k / 2] = std::max(1, std::max(botNeed, topNeed));
    }
  }
  auto topologyMatches = [&](std::size_t bi) {
    return bi + 1 < bands.size() && nColsFor[bi].size() == nColsFor[bi + 1].size() &&
           !nColsFor[bi].empty();
  };
  for (std::size_t bi = 0; bi + 1 < bands.size(); ++bi)
    if (topologyMatches(bi))
      for (std::size_t k = 0; k < nColsFor[bi].size(); ++k)
        nColsFor[bi + 1][k] = std::max(nColsFor[bi + 1][k], nColsFor[bi][k]);
  for (std::size_t bi = bands.size(); bi-- > 1;)
    if (topologyMatches(bi - 1))
      for (std::size_t k = 0; k < nColsFor[bi].size(); ++k)
        nColsFor[bi - 1][k] = std::max(nColsFor[bi - 1][k], nColsFor[bi][k]);

  for (std::size_t bi = 0; bi < bands.size(); ++bi) {
    const BandInfo& band = bands[bi];
    const double v0 = band.v0;
    const double v1 = band.v1;
    const std::vector<double>& xs0 = band.xs0;
    const std::vector<double>& xs1 = band.xs1;
    if (xs0.size() != xs1.size() || xs0.size() < 2)
      continue;  // a numerically-degenerate band (a break landing exactly on a vertex pair) — skip
    for (std::size_t k = 0; k + 1 < xs0.size(); k += 2) {
      const double u0L = xs0[k];
      const double u0R = xs0[k + 1];
      const double u1L = xs1[k];
      const double u1R = xs1[k + 1];
      if (u0R <= u0L && u1R <= u1L)
        continue;

      const int nRows = std::max(1, std::max(GeneralLoopChordSegments(sf, {u0L, v0}, {u1L, v1}, chordTolerance),
                                             GeneralLoopChordSegments(sf, {u0R, v0}, {u1R, v1}, chordTolerance)));
      int nCols = nColsFor[bi][k / 2];
      if (xs0.size() == 2) {
        if (bi == 0) {
          const int edgeIdx = findRimEdgeAt(v0);
          if (edgeIdx >= 0)
            nCols = std::max(nCols, segsForEdgeLocal(edgeIdx, s->edges[static_cast<std::size_t>(edgeIdx)]));
        }
        if (bi + 1 == bands.size()) {
          const int edgeIdx = findRimEdgeAt(v1);
          if (edgeIdx >= 0)
            nCols = std::max(nCols, segsForEdgeLocal(edgeIdx, s->edges[static_cast<std::size_t>(edgeIdx)]));
        }
      }

      std::vector<std::vector<std::uint32_t>> grid(
          static_cast<std::size_t>(nRows) + 1,
          std::vector<std::uint32_t>(static_cast<std::size_t>(nCols) + 1));
      for (int r = 0; r <= nRows; ++r) {
        const double t = static_cast<double>(r) / nRows;
        const double v = v0 + t * (v1 - v0);
        const double uL = u0L + t * (u1L - u0L);
        const double uR = u0R + t * (u1R - u0R);
        for (int c = 0; c <= nCols; ++c) {
          const double s = static_cast<double>(c) / nCols;
          grid[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)] =
              pushVertex(uL + s * (uR - uL), v);
        }
      }
      for (int r = 0; r < nRows; ++r)
        for (int c = 0; c < nCols; ++c) {
          const std::uint32_t a = grid[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)];
          const std::uint32_t b = grid[static_cast<std::size_t>(r)][static_cast<std::size_t>(c) + 1];
          const std::uint32_t cc = grid[static_cast<std::size_t>(r) + 1][static_cast<std::size_t>(c)];
          const std::uint32_t d = grid[static_cast<std::size_t>(r) + 1][static_cast<std::size_t>(c) + 1];
          mb->Tri(a, b, d);
          mb->Tri(a, d, cc);
        }
    }
  }
}

} // namespace

bool Tessellate(const Solid& s, double chordTolerance, Tessellation* out, Problem* outWhy,
                int fullCircleSegments) {
  if (!out)
    return false;  // a null output is a caller bug, not a user-facing reason: outWhy is left alone
  if (!std::isfinite(chordTolerance) || !(chordTolerance > 0.0))
    return Fail(Problem::NonPositiveTolerance, outWhy);
  const Problem why = Validate(s);
  if (why != Problem::Ok)
    return Fail(why, outWhy);

  Tessellation mesh;
  MeshBuilder mb{&mesh};

  // Shared-edge resolution unification (TASK-272 §5): a NURBS band's two v-boundary edges (its rim
  // at v=vStart and v=vEnd — see the `case SurfaceKind::Nurbs` block below) are now sampled DIRECTLY
  // from those loop edges, so that boundary is bit-identical to whatever neighbouring face (a flat
  // cap, or the next band in a multi-band loft/sweep) also walks the same edge — the same
  // "derive-the-shared-boundary-from-the-edge" fix that resolved §1's planar-hole crack.
  //
  // A uniform (u, v) grid needs ONE resolution for its whole u-direction, but its two v-boundary
  // edges can independently want different counts (different radii, e.g. a frustum band between two
  // differently-sized circles) — and a neighbouring face may separately want a HIGHER count still
  // (its own curvature need, or a further edge it shares elsewhere). Force every v-boundary edge to
  // the MAX segment count that any face touching it needs, propagated by repeated relaxation until
  // stable. This is monotonic (only ever raises a count), so it can only add resolution to an
  // already-correct neighbour, never invalidate it — the same safety argument the (now superseded)
  // two-pass relaxation in TASK-272 §3 relied on, this time paired with edge-exact sampling rather
  // than grid-only counts, which is what that earlier attempt was missing (see §5.2's failed retry).
  // Which loop edge is the v=vLo / v=vHi rim is found GEOMETRICALLY (by matching each edge-use's
  // world endpoints against the patch's own (u,v) corner points), not by assuming a fixed ordinal
  // position (`uses[0]`/`uses[2]`) or a fixed edge-kind pattern (Arc/Line/Arc/Line) — a real
  // imported/generated NURBS band does not reliably take that shape (confirmed: on a real pipe-
  // fitting transition piece, some bands' "side" edges are themselves arcs, and the ordinal
  // convention that held for a synthetic `brep::Loft` repro did not hold for the real geometry,
  // so the fix silently no-op'd despite "engaging"). `flip` records whether the edge's own natural
  // t=0 lands at the uHi corner instead of uLo, so the grid loop below knows which way to walk it.
  struct NurbsBoundaryEdge { int edge = -1; bool flip = false; };
  std::vector<std::array<NurbsBoundaryEdge, 2>> nurbsBoundaryEdges(s.faces.size());
  std::map<int, int> edgeSegs;
  for (std::size_t fi2 = 0; fi2 < s.faces.size(); ++fi2) {
    const Face& f2 = s.faces[fi2];
    if (f2.surface.kind != SurfaceKind::Nurbs)
      continue;
    if (!f2.paramLoops.empty() || f2.loops.size() != 1 || f2.loops[0].uses.size() != 4)
      continue;
    const Loop& lp = f2.loops[0];
    const nurbs::Patch& patch = f2.surface.patch;
    const double uLo = std::min(f2.uStart, f2.uEnd), uHi = std::max(f2.uStart, f2.uEnd);
    const double vLo = std::min(f2.vStart, f2.vEnd), vHi = std::max(f2.vStart, f2.vEnd);
    const Vec3 c00 = nurbs::Evaluate(patch, uLo, vLo);
    const Vec3 c10 = nurbs::Evaluate(patch, uHi, vLo);
    const Vec3 c01 = nurbs::Evaluate(patch, uLo, vHi);
    const Vec3 c11 = nurbs::Evaluate(patch, uHi, vHi);
    const double diag = std::max({ray3d::Length(ray3d::Sub(c11, c00)), ray3d::Length(ray3d::Sub(c10, c00)),
                                   ray3d::Length(ray3d::Sub(c01, c00)), 1e-6});
    const double tol = diag * 1e-4;
    auto near = [&](const Vec3& a, const Vec3& b) { return ray3d::Length(ray3d::Sub(a, b)) < tol; };
    int loEdge = -1, hiEdge = -1;
    bool loFlip = false, hiFlip = false;
    for (const EdgeUse& u : lp.uses) {
      const Edge& e = s.edges[static_cast<std::size_t>(u.edge)];
      const Vec3 p0 = EdgePointAt(s, e, 0.0);
      const Vec3 p1 = EdgePointAt(s, e, 1.0);
      if ((near(p0, c00) && near(p1, c10)) || (near(p0, c10) && near(p1, c00))) {
        loEdge = u.edge;
        loFlip = near(p0, c10);
      } else if ((near(p0, c01) && near(p1, c11)) || (near(p0, c11) && near(p1, c01))) {
        hiEdge = u.edge;
        hiFlip = near(p0, c11);
      }
    }
    // No corner match means this face's loop is not the standard 4-edge rectangle this fix knows
    // how to read (a future Sweep mitred corner, say) — leave it on the curvature-only grid.
    if (loEdge < 0 || hiEdge < 0)
      continue;
    const int curvatureSegs = NurbsPatchCurvatureSegs(patch, chordTolerance, fullCircleSegments);
    nurbsBoundaryEdges[fi2] = {NurbsBoundaryEdge{loEdge, loFlip}, NurbsBoundaryEdge{hiEdge, hiFlip}};
    for (const int edgeIdx : {loEdge, hiEdge}) {
      const Edge& be = s.edges[static_cast<std::size_t>(edgeIdx)];
      const int natural = std::max(SegmentsForEdge(be, chordTolerance, fullCircleSegments), curvatureSegs);
      auto [it, inserted] = edgeSegs.try_emplace(edgeIdx, natural);
      if (!inserted)
        it->second = std::max(it->second, natural);
    }
  }
  for (int pass = 0; pass < 8; ++pass) {
    bool changed = false;
    for (const std::array<NurbsBoundaryEdge, 2>& be : nurbsBoundaryEdges) {
      if (be[0].edge < 0)
        continue;
      const int shared = std::max(edgeSegs[be[0].edge], edgeSegs[be[1].edge]);
      if (edgeSegs[be[0].edge] != shared) { edgeSegs[be[0].edge] = shared; changed = true; }
      if (edgeSegs[be[1].edge] != shared) { edgeSegs[be[1].edge] = shared; changed = true; }
    }
    if (!changed)
      break;
  }
  // Every other consumer of `SegmentsForEdge` (flat-cap loop sampling, the planar-hole bridging
  // path, `TessellateGeneralLoopFace`'s fallback) must use the unified count too, or a cap will
  // still sample its shared rim at its own smaller natural count while the NURBS band next to it
  // samples the same edge at the relaxed, larger one.
  //
  // TASK-272 §9j: this same unification is what a paramLoops-bearing Plane/Cylinder/Cone face's
  // boundary needs too, and here the unification is trivial — `Loop::uses` already names the real
  // shared `Edge` by INDEX (no geometric corner-matching needed, unlike the NURBS case above, which
  // had no such back-reference), so every face that walks a given edge just needs to register its
  // own natural need against that one shared key.
  for (const Face& f2 : s.faces) {
    if (f2.paramLoops.empty())
      continue;
    for (const Loop& lp : f2.loops) {
      for (const EdgeUse& u : lp.uses) {
        const Edge& e = s.edges[static_cast<std::size_t>(u.edge)];
        if (e.kind == CurveKind::Line)
          continue;
        const int natural = SegmentsForEdge(e, chordTolerance, fullCircleSegments);
        auto [it, inserted] = edgeSegs.try_emplace(u.edge, natural);
        if (!inserted)
          it->second = std::max(it->second, natural);
      }
    }
  }
  auto segsForEdge = [&](int edgeIdx, const Edge& e) {
    const auto it = edgeSegs.find(edgeIdx);
    return it != edgeSegs.end() ? it->second : SegmentsForEdge(e, chordTolerance, fullCircleSegments);
  };

  // TASK-272 §9j: a paramLoops-bearing face's boundary as the IMPORTER sampled it (a fixed low
  // count — `kArcSamples` in `AcisSatParser.cpp`, 32 for a Plane, 8 for a Cylinder/Cone — chosen
  // only well enough to classify inside/outside, per `Face::paramLoops`' own docs) is not what a
  // NEIGHBOURING face's boundary is sampled at, even when both walk the same real `Edge`: a Plane
  // cap used its own points as-is (§9.2b) while `TessellateGeneralLoopFace` re-derived an
  // independent, chord-tolerance-driven count for a Cylinder/Cone wall — two different points for
  // the one physical rim, which is exactly what the real flange import's 1728-cracked-edge count
  // (evenly divisible into per-loop chunks matching each loop's own fixed sample count) turned out
  // to be. Fix: reconstruct a loop's 2D boundary directly from its real edges at the UNIFIED
  // `segsForEdge` count instead of trusting the raw `paramLoops` points, but only when that
  // reconstruction is confirmed (by enclosed-area agreement) to be the SAME loop the raw points
  // describe — hand-built test fixtures (`BrepTests.cpp`) set `paramLoops` on a face whose `loops`
  // still holds a differently-shaped primitive loop (ADR-052 (c) explicitly does not require them to
  // match), and reconstructing from THOSE edges would silently substitute the wrong boundary. A real
  // importer's `paramLoops` and `loops` always describe the same edges by construction, so the areas
  // agree closely there and the substitution safely engages.
  auto reconstructParamLoop2D = [&](const Surface& sf2, const Loop& lp,
                                     const std::vector<curveisect::Vec2>& raw)
      -> std::optional<std::vector<curveisect::Vec2>> {
    if (lp.uses.empty() || raw.size() < 3)
      return std::nullopt;
    std::vector<curveisect::Vec2> rebuilt;
    double contU = 0.0, prevRaw = 0.0;
    bool haveRaw = false;
    for (const EdgeUse& u : lp.uses) {
      const Edge& e = s.edges[static_cast<std::size_t>(u.edge)];
      const int segs = (e.kind == CurveKind::Line) ? 1 : segsForEdge(u.edge, e);
      for (int i = 0; i < segs; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(segs);
        const Vec3 w = EdgePointAt(s, e, u.reversed ? 1.0 - t : t);
        if (sf2.kind == SurfaceKind::Plane) {
          const ucs::Point2D p2 = ucs::WorldToPlane(sf2.frame, w);
          rebuilt.push_back({p2.x, p2.y});
        } else {
          // Cylinder/Cone general-trim convention (`BuildConeGeneralTrim`, AcisSatParser.cpp):
          // u = atan2(y, x) in the face frame, continuously unwrapped; v = frame-local z.
          const Vec3 local = ucs::WorldToUcs(sf2.frame, w);
          const double rawU = std::atan2(local.y, local.x);
          if (!haveRaw) {
            contU = rawU;
            haveRaw = true;
          } else {
            double delta = rawU - prevRaw;
            while (delta > kPi)
              delta -= kTwoPi;
            while (delta <= -kPi)
              delta += kTwoPi;
            contU += delta;
          }
          prevRaw = rawU;
          rebuilt.push_back({contU, local.z});
        }
      }
    }
    if (rebuilt.size() < 3)
      return std::nullopt;
    auto signedArea = [](const std::vector<curveisect::Vec2>& p) {
      double a = 0.0;
      for (std::size_t i = 0, n = p.size(); i < n; ++i)
        a += p[i].x * p[(i + 1) % n].y - p[(i + 1) % n].x * p[i].y;
      return 0.5 * a;
    };
    const double aRaw = std::fabs(signedArea(raw));
    const double aRebuilt = std::fabs(signedArea(rebuilt));
    // TASK-272 §9l: raw's area and the rebuild's area are each an INSCRIBED-polygon approximation of
    // the same true curve, at two different point counts (`raw` at the importer's fixed
    // `kArcSamples`, `rebuilt` at the unified, usually much higher, `segsForEdge` count) — so they
    // legitimately differ by the polygon-discretization error itself, not just by whether they are
    // "the same loop". Measured directly on the real flange import: a genuinely-matching loop (same
    // edges, 32 vs 256 points) differs by 0.64%, comfortably past the original, too-tight 0.1% gate
    // (confirmed via a temporary print: EVERY real loop in the flange was being rejected by it, which
    // is why §9.4's `reconstructParamLoop2D` measured as a no-op — it was silently declining to
    // engage on every real loop, not succeeding and failing to help). 2% comfortably covers a 32-gon's
    // worst-case discretization error against a much finer reconstruction while still rejecting a
    // hand-built test fixture's genuinely different shape (an unrelated box edge loop differs by
    // orders of magnitude, not a couple of percent).
    if (aRaw < 1e-12 || std::fabs(aRaw - aRebuilt) > 0.02 * aRaw)
      return std::nullopt;  // not confirmed to be the same loop — keep the raw points, don't misfire
    return rebuilt;
  };

  for (std::size_t fi = 0; fi < s.faces.size(); ++fi) {
    const Face& f = s.faces[fi];
    mb.face = static_cast<int>(fi);
    const Surface& sf = f.surface;
    // TASK-272 §9: a real imported solid (ACIS/DWG conversion) sets `paramLoops` on EVERY face, not
    // just the general-trim cases ADR-052 introduced it for — which used to route every such face
    // through `TessellateGeneralLoopFace` unconditionally, before it ever reached the bridge+EarClip
    // fix below. That fix works just as well from `paramLoops`' own 2D points (a Plane's param-loop
    // IS already its own (u, v) = plane-local 2D polygon, and using it AS-IS — not resampling it via
    // `SegmentsForEdge`/`EdgePointAt` — keeps this face's boundary bit-identical to whatever a
    // neighbouring face's paramLoops-derived boundary already agrees it should be, since an importer
    // that populates paramLoops populates it consistently for both sides of a shared edge). So a
    // Plane face with a hole is exempted from the early exit below and still gets the exact
    // triangulation; every OTHER paramLoops face (no holes, or curved) is unaffected by this task.
    const bool planeWithHoles = sf.kind == SurfaceKind::Plane && f.loops.size() > 1;
    if (!f.paramLoops.empty() && !planeWithHoles) {
      // A general trim loop (ADR-052, issue #306) overrides the rectangle-span grid below for
      // every `SurfaceKind` — issue #308's counterpart to issue #307's numeric-integral branch.
      // TASK-272 §9j: reconstruct each loop's boundary from its real edges at the unified
      // `segsForEdge` count (see `reconstructParamLoop2D` above) wherever that is confirmed safe,
      // so a Cylinder/Cone wall's rim is bit-identical to whatever a neighbouring cap or NURBS band
      // samples the same shared edge at, instead of this face re-deriving its own independent count.
      Face fFixed = f;
      for (std::size_t li = 0; li < f.paramLoops.size() && li < f.loops.size(); ++li) {
        std::optional<std::vector<curveisect::Vec2>> rebuilt =
            reconstructParamLoop2D(sf, f.loops[li], f.paramLoops[li]);
        if (rebuilt)
          fFixed.paramLoops[li] = std::move(*rebuilt);
      }
      TessellateGeneralLoopFace(fFixed, chordTolerance, &mb, &s, &edgeSegs);
      continue;
    }
    switch (sf.kind) {
    case SurfaceKind::Plane: {
      if (f.loops.size() > 1) {
        // A face with at least one hole (a single bolt hole is loops.size() == 2; a flange with
        // several is more). A plane has no curvature, so — unlike the general-loop path below,
        // which grids each face in chord-tolerance bands for a possibly-curved surface — the 2D
        // polygon these loops trace can be triangulated EXACTLY: bridge each hole into the outer
        // ring as a zero-width slit (`BridgeHoleIntoOuter`), which turns the whole face into one
        // simple polygon, then ear-clip it (`EarClip`, made robust to a bridge's thin/degenerate
        // geometry — see its own docs and TASK-272). This sidesteps a real bug in the old approach
        // (routing through `TessellateGeneralLoopFace`'s per-band grid): at a hole's own top/bottom
        // extremum, the scanline's interval count genuinely changes between adjacent bands, which
        // is not a resolution-matching problem no per-band column count can reconcile, and cracked
        // every hole at exactly those two points.
        //
        // `BridgeHoleIntoOuter`'s bridging assumes visibility with no reflex-vertex check (see its
        // own docs), which is not guaranteed for every real hole ARRANGEMENT (several holes around
        // a circular outer boundary, not just one hole in a rectangle) — confirmed on an actual
        // multi-hole flange, where it left a face untessellated entirely (the part vanished from
        // the render) instead of merely cracked. Rather than trust it unconditionally, this whole
        // attempt is speculative: on ANY failure (a hole this kernel could not bridge, or a bridged
        // polygon `EarClip` refuses), fall back to the always-available `TessellateGeneralLoopFace`
        // band grid below — worse (the pinch crack this fix targets), but never worse than not
        // rendering the face at all, which is the one regression this must never reintroduce.
        // TASK-272 §9: when this face came from a general-trim-loop import, `paramLoops` already
        // carries the outer/hole boundaries as plane-local 2D points — for a Plane, that IS the same
        // 2D space `WorldToPlane`/`PlaneToWorld` use (see `TessellateGeneralLoopFace`'s own
        // `pushVertex`, which treats a param-loop point's (u, v) as literal plane-local coordinates).
        // Use those points AS-IS instead of resampling from `s.edges` — an importer that populates
        // `paramLoops` does so consistently for every face that shares a boundary, so using its exact
        // points (not re-deriving new ones) is what keeps this face's boundary bit-identical to a
        // neighbour's, the same "derive-from-the-authoritative-source, don't resample" principle as
        // the edge-based path below uses when there is no param loop to read.
        const bool useParamLoops = !f.paramLoops.empty() && f.paramLoops.size() == f.loops.size();
        bool bridgedOk = true;
        std::vector<ucs::Point2D> outer2;
        if (useParamLoops) {
          // TASK-272 §9j: prefer a real-edge reconstruction at the unified segment count over the
          // raw (fixed, importer-chosen) paramLoops points, when confirmed to be the same loop —
          // see `reconstructParamLoop2D` above; this is what keeps this outer rim bit-identical to
          // a neighbouring Cylinder/Cone wall's own edge-derived boundary.
          std::optional<std::vector<curveisect::Vec2>> rebuilt =
              reconstructParamLoop2D(sf, f.loops[0], f.paramLoops[0]);
          const std::vector<curveisect::Vec2>& src = rebuilt ? *rebuilt : f.paramLoops[0];
          outer2.reserve(src.size());
          for (const curveisect::Vec2& p : src)
            outer2.push_back(ucs::Point2D{p.x, p.y});
        } else {
          for (const EdgeUse& u : f.loops[0].uses) {
            const Edge& e = s.edges[static_cast<std::size_t>(u.edge)];
            const int segs = segsForEdge(u.edge, e);
            for (int i = 0; i < segs; ++i) {
              const double t = static_cast<double>(i) / static_cast<double>(segs);
              const Vec3 p = EdgePointAt(s, e, u.reversed ? 1.0 - t : t);
              outer2.push_back(ucs::WorldToPlane(sf.frame, p));
            }
          }
        }
        if (outer2.size() < 3) {
          bridgedOk = false;
        } else {
          if (SignedArea2D(outer2) < 0.0)
            std::reverse(outer2.begin(), outer2.end());
          ucs::Point2D outerCentroid{0.0, 0.0};
          for (const ucs::Point2D& p : outer2) {
            outerCentroid.x += p.x / static_cast<double>(outer2.size());
            outerCentroid.y += p.y / static_cast<double>(outer2.size());
          }
          for (std::size_t li = 1; bridgedOk && li < f.loops.size(); ++li) {
            std::vector<ucs::Point2D> hole2;
            if (useParamLoops) {
              std::optional<std::vector<curveisect::Vec2>> rebuilt =
                  reconstructParamLoop2D(sf, f.loops[li], f.paramLoops[li]);
              const std::vector<curveisect::Vec2>& src = rebuilt ? *rebuilt : f.paramLoops[li];
              hole2.reserve(src.size());
              for (const curveisect::Vec2& p : src)
                hole2.push_back(ucs::Point2D{p.x, p.y});
            } else {
              for (const EdgeUse& u : f.loops[li].uses) {
                const Edge& e = s.edges[static_cast<std::size_t>(u.edge)];
                const int segs = segsForEdge(u.edge, e);
                for (int i = 0; i < segs; ++i) {
                  const double t = static_cast<double>(i) / static_cast<double>(segs);
                  const Vec3 p = EdgePointAt(s, e, u.reversed ? 1.0 - t : t);
                  hole2.push_back(ucs::WorldToPlane(sf.frame, p));
                }
              }
            }
            if (hole2.size() < 3)
              continue;  // a degenerate hole loop contributes nothing to bridge
            if (SignedArea2D(hole2) > 0.0)
              std::reverse(hole2.begin(), hole2.end());  // holes wind opposite the outer ring
            if (!BridgeHoleIntoOuter(&outer2, hole2, outerCentroid)) {
              bridgedOk = false;
            }
          }
        }
        std::vector<std::array<int, 3>> tris;
        if (bridgedOk && !EarClip(outer2, &tris)) {
          bridgedOk = false;
        }
        if (bridgedOk)
          ImproveTriangulationQuality(outer2, &tris);
        if (bridgedOk) {
          const Vec3 planeN = sf.frame.zAxis;
          std::vector<Vec3> world(outer2.size());
          for (std::size_t i = 0; i < outer2.size(); ++i)
            world[i] = ucs::PlaneToWorld(sf.frame, outer2[i]);
          // Every triangle on a flat face must wind to match the SAME single normal, `planeN` —
          // unlike a curved surface (where winding can only sensibly be checked against a
          // neighbour, since the "expected" direction varies per point), a flat face's correct
          // winding is unambiguous per triangle, so each is checked and, if needed, flipped
          // individually rather than deciding once.
          std::vector<std::uint32_t> verts(outer2.size());
          for (std::size_t i = 0; i < outer2.size(); ++i)
            verts[i] = mb.Push(world[i], planeN);
          for (const std::array<int, 3>& t3 : tris) {
            const Vec3& a3 = world[static_cast<std::size_t>(t3[0])];
            const Vec3& b3 = world[static_cast<std::size_t>(t3[1])];
            const Vec3& c3 = world[static_cast<std::size_t>(t3[2])];
            const Vec3 g = ray3d::Cross(ray3d::Sub(b3, a3), ray3d::Sub(c3, a3));
            const bool flip = ray3d::Length(g) > 1e-15 && ray3d::Dot(g, planeN) < 0.0;
            if (!flip)
              mb.Tri(verts[static_cast<std::size_t>(t3[0])], verts[static_cast<std::size_t>(t3[1])],
                     verts[static_cast<std::size_t>(t3[2])]);
            else
              mb.Tri(verts[static_cast<std::size_t>(t3[0])], verts[static_cast<std::size_t>(t3[2])],
                     verts[static_cast<std::size_t>(t3[1])]);
          }
          break;
        }
        // Fallback: the pre-existing band-grid tessellator, unchanged.
        Face tmp = f;
        tmp.paramLoops.clear();
        tmp.paramLoops.reserve(f.loops.size());
        for (const Loop& lp : f.loops) {
          std::vector<curveisect::Vec2> poly;
          for (const EdgeUse& u : lp.uses) {
            const Edge& e = s.edges[static_cast<std::size_t>(u.edge)];
            int segs = segsForEdge(u.edge, e);
            for (int i = 0; i < segs; ++i) {
              double t = double(i) / double(segs);
              Vec3 p = EdgePointAt(s, e, u.reversed ? 1.0 - t : t);
              ucs::Point2D q = ucs::WorldToPlane(sf.frame, p);
              poly.push_back(curveisect::Vec2{q.x, q.y});
            }
          }
          tmp.paramLoops.push_back(std::move(poly));
        }
        TessellateGeneralLoopFace(tmp, chordTolerance, &mb, &s, &edgeSegs);
        break;
      }
      // Walk each loop into a polyline. Arc edges are subdivided by the same chord rule the curved
      // faces use, so the cap and the wall it meets do not disagree.
      auto sampleLoop = [&](const Loop& lp) {
        std::vector<Vec3> r;
        for (const EdgeUse& u : lp.uses) {
          const Edge& e = s.edges[static_cast<std::size_t>(u.edge)];
          const int segs = segsForEdge(u.edge, e);
          for (int i = 0; i < segs; ++i) {
            const double t = static_cast<double>(i) / static_cast<double>(segs);
            r.push_back(EdgePointAt(s, e, u.reversed ? 1.0 - t : t));
          }
        }
        return r;
      };
      const Vec3 n = sf.frame.zAxis;

      std::vector<Vec3> ring = sampleLoop(f.loops[0]);
      if (ring.size() < 3)
        return Fail(Problem::DegenerateFace, outWhy);
      std::vector<ucs::Point2D> ring2;
      ring2.reserve(ring.size());
      for (const Vec3& p : ring)
        ring2.push_back(ucs::WorldToPlane(sf.frame, p));

      if (Polygon2DIsConvex(ring2)) {
        // The centroid fan — unchanged from REQ-313, so every primitive tessellates as it did.
        Vec3 centroid{};
        for (const Vec3& p : ring)
          centroid = ray3d::Add(centroid, p);
        centroid = ray3d::Scale(centroid, 1.0 / static_cast<double>(ring.size()));
        const std::uint32_t c = mb.Push(centroid, n);
        std::vector<std::uint32_t> idx;
        idx.reserve(ring.size());
        for (const Vec3& p : ring)
          idx.push_back(mb.Push(p, n));
        for (std::size_t i = 0; i < idx.size(); ++i)
          mb.Tri(c, idx[i], idx[(i + 1) % idx.size()]);
      } else {
        // Non-convex cap (REQ-314): ear-clip. The clipper wants a CCW ring; the 2D points are the
        // same either way, so a triangle it returns is still CCW about `n` once mapped back.
        const bool ccw = SignedArea2D(ring2) >= 0.0;
        std::vector<int> order(ring.size());
        for (std::size_t i = 0; i < ring.size(); ++i)
          order[i] = static_cast<int>(ccw ? i : ring.size() - 1 - i);
        std::vector<ucs::Point2D> ccwRing;
        ccwRing.reserve(ring.size());
        for (int o : order)
          ccwRing.push_back(ring2[static_cast<std::size_t>(o)]);
        std::vector<std::array<int, 3>> tris;
        if (!EarClip(ccwRing, &tris))
          return Fail(Problem::DegenerateFace, outWhy);
        ImproveTriangulationQuality(ccwRing, &tris);
        std::vector<std::uint32_t> idx;
        idx.reserve(ring.size());
        for (const Vec3& p : ring)
          idx.push_back(mb.Push(p, n));
        for (const std::array<int, 3>& t : tris)
          mb.Tri(idx[static_cast<std::size_t>(order[static_cast<std::size_t>(t[0])])],
                 idx[static_cast<std::size_t>(order[static_cast<std::size_t>(t[1])])],
                 idx[static_cast<std::size_t>(order[static_cast<std::size_t>(t[2])])]);
      }
      break;
    }
    case SurfaceKind::Cylinder:
    case SurfaceKind::Cone: {
      const double r0 = sf.radius;
      const double r1 = sf.kind == SurfaceKind::Cylinder ? sf.radius : sf.radius2;
      // A holed band (a branch mouth bored out of an otherwise-full cylinder wall — REQ-314 B2b-2):
      // the Intersection edge is in an INNER loop, so the face is `[zFull0, zFull1]` minus a bite
      // `[bz0(u), bz1(u)]`. Drawn as a lower and an upper sub-strip (both collapse where there is no
      // bite).
      if (sf.kind == SurfaceKind::Cylinder && f.loops.size() > 1 &&
          !LoopHasIntersectionEdge(s, f.loops.front()) && FaceLoopHasIntersectionEdge(s, f)) {
        const IsectStrip hStrip = MakeIsectStrip(s, f);
        double zF0 = 1e300;
        double zF1 = -1e300;
        for (const EdgeUse& u : f.loops.front().uses)
          for (const int vi : {s.edges[static_cast<std::size_t>(u.edge)].v0,
                               s.edges[static_cast<std::size_t>(u.edge)].v1}) {
            const double z = ucs::WorldToUcs(sf.frame, s.vertices[static_cast<std::size_t>(vi)].p).z;
            zF0 = std::min(zF0, z);
            zF1 = std::max(zF1, z);
          }
        const int hnu = std::clamp(
            SegmentsForArc(r0, f.uEnd - f.uStart, 0.25 * chordTolerance), 32, 320);
        std::vector<std::uint32_t> rA(static_cast<std::size_t>(hnu) + 1);
        std::vector<std::uint32_t> rB(static_cast<std::size_t>(hnu) + 1);
        std::vector<std::uint32_t> rC(static_cast<std::size_t>(hnu) + 1);
        std::vector<std::uint32_t> rD(static_cast<std::size_t>(hnu) + 1);
        for (int i = 0; i <= hnu; ++i) {
          const double t = f.uStart + (f.uEnd - f.uStart) * i / static_cast<double>(hnu);
          const Vec3 nn = ConicalNormal(sf, r0, r0, t);
          double b0 = zF1;
          double b1 = zF1;
          if (hStrip.valid())
            (void)IsectStripAt(hStrip, t, &b0, &b1);
          b0 = std::clamp(b0, zF0, zF1);
          b1 = std::clamp(b1, zF0, zF1);
          rA[static_cast<std::size_t>(i)] = mb.Push(ConicalPoint(sf, r0, r0, t, zF0), nn);
          rB[static_cast<std::size_t>(i)] = mb.Push(ConicalPoint(sf, r0, r0, t, b0), nn);
          rC[static_cast<std::size_t>(i)] = mb.Push(ConicalPoint(sf, r0, r0, t, b1), nn);
          rD[static_cast<std::size_t>(i)] = mb.Push(ConicalPoint(sf, r0, r0, t, zF1), nn);
        }
        for (int i = 0; i < hnu; ++i) {
          const auto a = static_cast<std::size_t>(i);
          const auto b = static_cast<std::size_t>(i + 1);
          mb.Tri(rA[a], rA[b], rB[b]);
          mb.Tri(rA[a], rB[b], rB[a]);
          mb.Tri(rC[a], rC[b], rD[b]);
          mb.Tri(rC[a], rD[b], rD[a]);
        }
        break;
      }
      const bool isect = sf.kind == SurfaceKind::Cylinder && FaceLoopHasIntersectionEdge(s, f);
      const bool coneCut = sf.kind == SurfaceKind::Cone &&
                          (FaceLoopHasEllipseEdge(s, f) || FaceLoopHasIntersectionEdge(s, f));
      // The plain unclipped cylinder/cone wall — EXTRUDE or PRESSPULL of an ordinary circle, the
      // common case — is a (near-)full turn, so `SegmentsForArc`'s own `kMinFullCircleSegments`
      // floor already keeps it smooth; no separate clamp needed here (issue #486 GUI pass). The
      // isect/coneCut case additionally floors at 24, since its face is CUT to a partial sweep by
      // an intersection or ellipse edge and so would not otherwise reach the full-turn floor above.
      const int nu = (isect || coneCut)
                        ? std::clamp(SegmentsForArc(std::max(r0, r1), f.uEnd - f.uStart, 0.25 * chordTolerance), 24, 256)
                        : SegmentsForArc(std::max(r0, r1), f.uEnd - f.uStart, chordTolerance);
      CylinderCut cc;
      const bool cut = sf.kind == SurfaceKind::Cylinder && !isect && CylinderCutZExtent(s, f, &cc);
      const IsectStrip strip = isect ? MakeIsectStrip(s, f) : IsectStrip{};
      const ConeCutStrip coneStrip = coneCut ? MakeConeCutStrip(s, f) : ConeCutStrip{};
      auto bound = [](const double c[3], double u) {
        return c[0] + c[1] * std::cos(u) + c[2] * std::sin(u);
      };
      std::vector<std::uint32_t> lower(static_cast<std::size_t>(nu) + 1);
      std::vector<std::uint32_t> upper(static_cast<std::size_t>(nu) + 1);
      std::vector<Vec3> lowerP(static_cast<std::size_t>(nu) + 1);
      std::vector<Vec3> upperP(static_cast<std::size_t>(nu) + 1);
      for (int i = 0; i <= nu; ++i) {
        const double t = f.uStart + (f.uEnd - f.uStart) * static_cast<double>(i) / static_cast<double>(nu);
        const Vec3 n = ConicalNormal(sf, r0, r1, t);
        double zA = cut ? bound(cc.lo, t) : 0.0;
        double zB = cut ? bound(cc.hi, t) : sf.height;
        if (isect && strip.valid()) {
          if (!IsectStripAt(strip, t, &zA, &zB)) {
            const double zc = 0.5 * (strip.zSearchLo + strip.zSearchHi);
            zA = zB = zc;  // the strip has pinched — a degenerate sliver at this end of the lens
          }
        }
        if (coneCut && coneStrip.valid())
          (void)ConeCutStripAt(coneStrip, t, &zA, &zB);  // exact — never pinches (SliceConeOblique's
                                                          // own guard keeps the cut off both caps
        lowerP[static_cast<std::size_t>(i)] = ConicalPoint(sf, r0, r1, t, zA);
        upperP[static_cast<std::size_t>(i)] = ConicalPoint(sf, r0, r1, t, zB);
        lower[static_cast<std::size_t>(i)] = mb.Push(lowerP[static_cast<std::size_t>(i)], n);
        upper[static_cast<std::size_t>(i)] = mb.Push(upperP[static_cast<std::size_t>(i)], n);
      }
      // Where a band's ring collapses to a point — the APEX of a sharp cone, whose whole upper ring
      // is one vertex — a quad is a triangle, and emitting it as two triangles makes the second one
      // a zero-area sliver with two coincident corners. Such a sliver draws nothing, so it looked
      // harmless, but its two "different" edges are the SAME rim-to-apex edge, which counts that
      // edge twice: every apex edge was used 4 times instead of 2, and the mesh read as non-manifold
      // (`RequireMeshWatertight`). A sharp cone was HALF degenerate triangles — 128 of 256 per side
      // half-face. Collapse-aware emission fixes the manifold count and halves the cone's side mesh.
      const double ringEps = 1e-12 * std::max({std::fabs(r0), std::fabs(r1), std::fabs(sf.height), 1.0});
      auto samePoint = [&](const Vec3& p, const Vec3& q) {
        return ray3d::Length(ray3d::Sub(p, q)) <= ringEps;
      };
      for (int i = 0; i < nu; ++i) {
        const std::size_t a = static_cast<std::size_t>(i);
        const std::size_t b = static_cast<std::size_t>(i + 1);
        const bool upperPinched = samePoint(upperP[a], upperP[b]);
        const bool lowerPinched = samePoint(lowerP[a], lowerP[b]);
        if (upperPinched && lowerPinched)
          continue;  // the whole segment is a point: nothing to draw
        // Emission ORDER is unchanged from before the collapse test, so a band with no collapsed
        // ring — every cylinder, every frustum — produces the identical index buffer it always has.
        if (sf.inward) {  // REQ-314 B2a: a bore wall — reverse winding to match the flipped normal
          if (!lowerPinched)
            mb.Tri(lower[a], upper[b], lower[b]);
          if (!upperPinched)
            mb.Tri(lower[a], upper[a], upper[b]);
        } else {
          if (!lowerPinched)
            mb.Tri(lower[a], lower[b], upper[b]);
          if (!upperPinched)
            mb.Tri(lower[a], upper[b], upper[a]);
        }
      }
      break;
    }
    case SurfaceKind::Sphere:
    case SurfaceKind::Torus: {
      const bool sphere = sf.kind == SurfaceKind::Sphere;
      const double uRadius = sphere ? sf.radius : sf.radius + sf.radius2;
      const double vRadius = sphere ? sf.radius : sf.radius2;
      // A sphere patch bounded by a procedural Intersection edge (REQ-314 B2b-2, issue #242): the
      // latitude band is a function of longitude, found the same way IntegrateSphereFaceNumeric does.
      const bool isectSphere = sphere && FaceLoopHasIntersectionEdge(s, f);
      const SphereIsectStrip sStrip = isectSphere ? MakeSphereIsectStrip(s, f) : SphereIsectStrip{};
      // A hemisphere (full pole-to-pole v span) carrying a procedural edge is the SUBTRACT / UNION
      // kept sphere (issue #242): draw its three kept latitude bands — below the lower lens bite,
      // between the two bites, above the upper bite — the complement of what SphereStripsAt returns.
      const bool sphereComplement = isectSphere && sStrip.valid() &&
                                    f.vStart <= -kHalfPi + 1e-9 && f.vEnd >= kHalfPi - 1e-9;
      if (sphereComplement) {
        // Draw the full hemisphere grid and drop any quad whose centre falls inside a lens bite.
        // The lens boundary lands ragged at grid resolution — well inside REQ-101 on a model part —
        // but every emitted quad keeps the clean outward grid winding (no pinch slivers).
        const int cnu =
            std::clamp(SegmentsForArc(uRadius, std::fabs(f.uEnd - f.uStart), 0.25 * chordTolerance),
                       48, 320);
        const int cnv = std::clamp(SegmentsForArc(vRadius, kPi, 0.5 * chordTolerance), 24, 200);
        std::vector<std::pair<double, double>> strips;
        auto inBite = [&](double u, double v) {
          SphereStripsAt(sStrip, u, &strips);
          for (const auto& [a, b] : strips)
            if (v > a && v < b)
              return true;
          return false;
        };
        std::vector<std::uint32_t> row0(static_cast<std::size_t>(cnv) + 1);
        std::vector<std::uint32_t> row1(static_cast<std::size_t>(cnv) + 1);
        auto fillRow = [&](double u, std::vector<std::uint32_t>& row) {
          for (int j = 0; j <= cnv; ++j) {
            const double v = f.vStart + (f.vEnd - f.vStart) * static_cast<double>(j) / cnv;
            row[static_cast<std::size_t>(j)] = mb.Push(SphericalPoint(sf, u, v), SphericalNormal(sf, u, v));
          }
        };
        double uPrev = f.uStart;
        fillRow(uPrev, row0);
        for (int i = 1; i <= cnu; ++i) {
          const double uCur = f.uStart + (f.uEnd - f.uStart) * static_cast<double>(i) / cnu;
          fillRow(uCur, row1);
          const double uMid = 0.5 * (uPrev + uCur);
          for (int j = 0; j < cnv; ++j) {
            const double vMid =
                f.vStart + (f.vEnd - f.vStart) * (static_cast<double>(j) + 0.5) / cnv;
            if (inBite(uMid, vMid))
              continue;
            const auto a = static_cast<std::size_t>(j);
            mb.Tri(row0[a], row1[a], row1[a + 1]);
            mb.Tri(row0[a], row1[a + 1], row0[a + 1]);
          }
          row0.swap(row1);
          uPrev = uCur;
        }
        break;
      }
      const int nu = isectSphere
                         ? std::clamp(SegmentsForArc(uRadius, f.uEnd - f.uStart, 0.25 * chordTolerance),
                                      24, 256)
                         : SegmentsForArc(uRadius, f.uEnd - f.uStart, chordTolerance);
      const int nv = SegmentsForArc(vRadius, f.vEnd - f.vStart, chordTolerance);
      std::vector<std::uint32_t> grid(static_cast<std::size_t>(nu + 1) * static_cast<std::size_t>(nv + 1));
      std::vector<Vec3> gridP(static_cast<std::size_t>(nu + 1) * static_cast<std::size_t>(nv + 1));
      for (int i = 0; i <= nu; ++i) {
        const double t = f.uStart + (f.uEnd - f.uStart) * static_cast<double>(i) / static_cast<double>(nu);
        double vA = f.vStart;
        double vB = f.vEnd;
        if (isectSphere && sStrip.valid() && !SphereStripAt(sStrip, t, &vA, &vB))
          vA = vB = 0.5 * (sStrip.vSearchLo + sStrip.vSearchHi);  // strip pinched — a sliver
        for (int j = 0; j <= nv; ++j) {
          const double v = vA + (vB - vA) * static_cast<double>(j) / static_cast<double>(nv);
          const Vec3 p = sphere ? SphericalPoint(sf, t, v) : ToroidalPoint(sf, t, v);
          const Vec3 n = sphere ? SphericalNormal(sf, t, v) : ToroidalNormal(sf, t, v);
          const std::size_t gi = static_cast<std::size_t>(i) * static_cast<std::size_t>(nv + 1) +
                                 static_cast<std::size_t>(j);
          gridP[gi] = p;
          grid[gi] = mb.Push(p, n);
        }
      }
      // The same collapse the conical band has, at a SPHERE'S POLES: every longitude meets at one
      // point, so the pole row of the grid is a single position repeated. Emitting each cell as two
      // triangles makes one of them a zero-area sliver whose two "different" edges are the same
      // pole-to-ring edge, counting it twice — 512 edges of a sphere read as non-manifold. A torus
      // has no pole and is unaffected.
      const double gridEps =
          1e-12 * std::max({std::fabs(uRadius), std::fabs(vRadius), 1.0});
      auto sameGridPoint = [&](std::size_t p, std::size_t q) {
        return ray3d::Length(ray3d::Sub(gridP[p], gridP[q])) <= gridEps;
      };
      const std::size_t stride = static_cast<std::size_t>(nv + 1);
      for (int i = 0; i < nu; ++i) {
        for (int j = 0; j < nv; ++j) {
          const std::size_t a = static_cast<std::size_t>(i) * stride + static_cast<std::size_t>(j);
          const std::size_t b = a + stride;
          // Each cell's two v-edges: the pair at this j, and the pair at j+1. Either may be a pole.
          const bool pinchedLo = sameGridPoint(a, b);
          const bool pinchedHi = sameGridPoint(a + 1, b + 1);
          if (pinchedLo && pinchedHi)
            continue;  // the whole cell is a point
          // Emission order is unchanged, so a patch with no collapsed row — every torus, every
          // sphere band away from the poles — produces the index buffer it always has.
          if (sf.inward) {  // REQ-314 B2a: reverse winding to match the flipped normal
            if (!pinchedLo)
              mb.Tri(grid[a], grid[b + 1], grid[b]);
            if (!pinchedHi)
              mb.Tri(grid[a], grid[a + 1], grid[b + 1]);
          } else {
            if (!pinchedLo)
              mb.Tri(grid[a], grid[b], grid[b + 1]);
            if (!pinchedHi)
              mb.Tri(grid[a], grid[b + 1], grid[a + 1]);
          }
        }
      }
      break;
    }
    case SurfaceKind::Nurbs: {
      // A uniform (u, v) grid over the face's parameter rectangle, with the analytic patch normal at
      // every node so the shading matches the isolines the same evaluator draws. A ruled straight
      // span is exact at one quad; a rational (arc) span gets a curvature-driven division count.
      const nurbs::Patch& patch = sf.patch;
      const double uLo = std::min(f.uStart, f.uEnd);
      const double uHi = std::max(f.uStart, f.uEnd);
      const double vLo = std::min(f.vStart, f.vEnd);
      const double vHi = std::max(f.vStart, f.vEnd);
      // TASK-272 §5: when the pre-pass above identified this face's two v-boundary (rim) edges, use
      // the unified, shared-edge-relaxed segment count instead of this face's own independent
      // curvature estimate, so this boundary lands on exactly the same resolution as whatever
      // neighbouring face (cap or band) also walks those edges.
      const std::array<NurbsBoundaryEdge, 2>& boundaryEdges = nurbsBoundaryEdges[fi];
      const int n = boundaryEdges[0].edge >= 0
                        ? edgeSegs.at(boundaryEdges[0].edge)
                        : NurbsPatchCurvatureSegs(patch, chordTolerance, fullCircleSegments);
      // V is divided on its OWN curvature, which for a ruled sweep direction is one division and
      // exactly right. U keeps the shared, edge-relaxed count above because the rim edges run along
      // it and neighbouring faces must land on the same resolution (TASK-272 §5) — that is why this
      // is two numbers and not one. See NurbsPatchDirectionSegs for what using one cost.
      const int nvSegs =
          std::max(1, NurbsPatchDirectionSegs(patch, chordTolerance, /*alongU=*/false, fullCircleSegments));
      std::vector<std::uint32_t> grid(static_cast<std::size_t>(n + 1) * static_cast<std::size_t>(nvSegs + 1));
      // The two v-boundary rows (j=0 at v=vLo, j=n at v=vHi) are sampled DIRECTLY from their loop
      // edges rather than `nurbs::EvaluateWithDerivs`, so they are bit-identical to the same edge's
      // sampling on the neighbouring face (a flat cap's `sampleLoop`, or the next band's own
      // boundary row) — the phase-matching half of the fix that §5.2's resolution-only floor was
      // missing. `flip` (found geometrically by the pre-pass above, by matching this edge's own
      // endpoints against the patch's own (u, v) corners) says whether the edge's own t=0 lands at
      // the uHi end instead of uLo, so u-index i maps to edge-native t = flip ? 1-i/n : i/n.
      const Edge* loEdge = boundaryEdges[0].edge >= 0 ? &s.edges[static_cast<std::size_t>(boundaryEdges[0].edge)] : nullptr;
      const Edge* hiEdge = boundaryEdges[1].edge >= 0 ? &s.edges[static_cast<std::size_t>(boundaryEdges[1].edge)] : nullptr;
      for (int i = 0; i <= n; ++i)
        for (int j = 0; j <= nvSegs; ++j) {
          const double u = uLo + (uHi - uLo) * static_cast<double>(i) / static_cast<double>(n);
          const double v = vLo + (vHi - vLo) * static_cast<double>(j) / static_cast<double>(nvSegs);
          Vec3 p;
          Vec3 nrm;
          const Edge* boundaryEdge = (j == 0) ? loEdge : (j == nvSegs) ? hiEdge : nullptr;
          if (boundaryEdge) {
            const bool flip = (j == 0) ? boundaryEdges[0].flip : boundaryEdges[1].flip;
            const double s_ = static_cast<double>(i) / static_cast<double>(n);
            const double t = flip ? 1.0 - s_ : s_;
            p = EdgePointAt(s, *boundaryEdge, t);
            // The analytic patch normal at the matching (u, v) still gives correct shading — only the
            // POSITION needs to come from the edge; a boundary curve has no separate "edge normal".
            nrm = nurbs::EvaluateWithDerivs(patch, u, v).normal;
          } else {
            const nurbs::SurfacePoint sp = nurbs::EvaluateWithDerivs(patch, u, v);
            p = sp.p;
            nrm = sp.normal;
          }
          if (!(ray3d::Dot(nrm, nrm) > 0.25))
            nrm = Vec3{0.0, 0.0, 1.0};  // a collapsed edge (a pole) — a loft patch has none
          if (sf.inward)
            nrm = ray3d::Scale(nrm, -1.0);
          grid[static_cast<std::size_t>(i) * static_cast<std::size_t>(nvSegs + 1) +
               static_cast<std::size_t>(j)] = mb.Push(p, nrm);
        }
      const std::size_t stride = static_cast<std::size_t>(nvSegs + 1);
      for (int i = 0; i < n; ++i)
        for (int j = 0; j < nvSegs; ++j) {
          const std::size_t a = static_cast<std::size_t>(i) * stride + static_cast<std::size_t>(j);
          const std::size_t b = a + stride;
          if (sf.inward) {
            mb.Tri(grid[a], grid[b + 1], grid[b]);
            mb.Tri(grid[a], grid[a + 1], grid[b + 1]);
          } else {
            mb.Tri(grid[a], grid[b], grid[b + 1]);
            mb.Tri(grid[a], grid[b + 1], grid[a + 1]);
          }
        }
      break;
    }
    }
  }

  // Weld duplicate vertices that share position AND normal (smooth seams, e.g. cylinder halves).
  // Keep hard edges (cap ↔ wall, 90°) separate by requiring normal match, so wall/cap seams stay hard.
  //
  // Through a SPATIAL HASH, not a linear scan. This was a plain double loop — every vertex compared
  // against every unique vertex kept so far — which is O(n^2) and, on the meshes this actually
  // produces, catastrophic: a ten-vertex 2in pipe run tessellates to roughly 400,000 vertices, and
  // welding them took **26 seconds** while the application appeared to hang (measured 2026-09-24,
  // user-reported freeze on finishing a long run). It is the dominant cost of Tessellate for any
  // dense mesh and it got worse as the square of the size.
  //
  // The rule is unchanged, and so is the result: a candidate within `posEps` of a vertex must lie in
  // that vertex's own cell or one of the 26 around it, because the cell size IS `posEps`. So the 27
  // cells are probed, and within them the same position-and-normal comparison decides, in the same
  // order (ascending kept-index), which means the winning match — and therefore the remap and the
  // final vertex numbering — is identical to what the linear scan produced.
  {
    const double posEps = 1e-9;
    const double nrmEps = 1e-6;
    const std::size_t vertCount = mesh.vertsXyz.size() / 3;
    std::vector<uint32_t> remap(vertCount, 0xFFFFFFFFu);
    std::vector<double> newVerts, newNorms;
    newVerts.reserve(mesh.vertsXyz.size());
    newNorms.reserve(mesh.normalsXyz.size());

    // Cell index of a coordinate, at cell size == posEps. Storage coordinates reach ~1e5 ft and the
    // cell is 1e-9, so the quotient needs the full 64-bit range.
    const auto cellOf = [&](double v) {
      return static_cast<std::int64_t>(std::floor(v / posEps));
    };
    struct CellKey {
      std::int64_t x, y, z;
      bool operator==(const CellKey& o) const { return x == o.x && y == o.y && z == o.z; }
    };
    struct CellHash {
      std::size_t operator()(const CellKey& k) const {
        std::uint64_t h = 1469598103934665603ull;
        const auto mix = [&h](std::uint64_t v) { h = (h ^ v) * 1099511628211ull; };
        mix(static_cast<std::uint64_t>(k.x));
        mix(static_cast<std::uint64_t>(k.y));
        mix(static_cast<std::uint64_t>(k.z));
        return static_cast<std::size_t>(h);
      }
    };
    std::unordered_map<CellKey, std::vector<uint32_t>, CellHash> cells;
    cells.reserve(vertCount * 2);

    for (std::size_t i = 0; i < vertCount; ++i) {
      const double x = mesh.vertsXyz[i * 3], y = mesh.vertsXyz[i * 3 + 1], z = mesh.vertsXyz[i * 3 + 2];
      const double nx = mesh.normalsXyz[i * 3], ny = mesh.normalsXyz[i * 3 + 1],
                   nz = mesh.normalsXyz[i * 3 + 2];
      const std::int64_t cx = cellOf(x), cy = cellOf(y), cz = cellOf(z);
      uint32_t found = 0xFFFFFFFFu;
      for (std::int64_t dx = -1; dx <= 1 && found == 0xFFFFFFFFu; ++dx)
        for (std::int64_t dy = -1; dy <= 1 && found == 0xFFFFFFFFu; ++dy)
          for (std::int64_t dz = -1; dz <= 1 && found == 0xFFFFFFFFu; ++dz) {
            const auto it = cells.find(CellKey{cx + dx, cy + dy, cz + dz});
            if (it == cells.end())
              continue;
            // Ascending kept-index, so the FIRST acceptable match is the same one the linear scan
            // would have found.
            for (const uint32_t j : it->second) {
              const double ddx = newVerts[j * 3] - x, ddy = newVerts[j * 3 + 1] - y,
                           ddz = newVerts[j * 3 + 2] - z;
              const double dnx = newNorms[j * 3] - nx, dny = newNorms[j * 3 + 1] - ny,
                           dnz = newNorms[j * 3 + 2] - nz;
              if (ddx * ddx + ddy * ddy + ddz * ddz < posEps * posEps &&
                  dnx * dnx + dny * dny + dnz * dnz < nrmEps * nrmEps) {
                found = j;
                break;
              }
            }
          }
      if (found == 0xFFFFFFFFu) {
        found = static_cast<uint32_t>(newVerts.size() / 3);
        newVerts.push_back(x);
        newVerts.push_back(y);
        newVerts.push_back(z);
        newNorms.push_back(nx);
        newNorms.push_back(ny);
        newNorms.push_back(nz);
        cells[CellKey{cx, cy, cz}].push_back(found);
      }
      remap[i] = found;
    }
    for (auto& idx : mesh.indices)
      idx = remap[idx];
    mesh.vertsXyz.swap(newVerts);
    mesh.normalsXyz.swap(newNorms);
  }

  *out = std::move(mesh);
  return Succeed(outWhy);
}

namespace {



/// A point on a curved face at parameters (u, v), in world.
///
/// One evaluator for every surface kind, so an isoline and the shaded triangles beside it cannot
/// disagree about where the surface is. `v` is ignored for the ruled kinds, where the second
/// parameter is a height rather than an angle.
[[nodiscard]] Vec3 SurfacePointAt(const Surface& sf, double u, double v) {
  switch (sf.kind) {
  case SurfaceKind::Plane:
    return sf.frame.origin;
  case SurfaceKind::Cylinder:
    return CylinderPoint(sf, u, v);
  case SurfaceKind::Cone:
    return ConicalPoint(sf, sf.radius, sf.radius2, u, v);
  case SurfaceKind::Sphere:
    return SphericalPoint(sf, u, v);
  case SurfaceKind::Torus:
    return ToroidalPoint(sf, u, v);
  case SurfaceKind::Nurbs:
    return nurbs::Evaluate(sf.patch, u, v);
  }
  return sf.frame.origin;
}

/// Walk one iso-curve and emit it as `GL_LINES` segments.
///
/// \p fixedIsU says which parameter is held constant: the curve runs along the other one.
void AppendIsoCurve(const Surface& sf, bool fixedIsU, double fixed, double from, double to, int steps,
                    std::vector<double>* out) {
  if (steps < 1)
    return;
  Vec3 prev = fixedIsU ? SurfacePointAt(sf, fixed, from) : SurfacePointAt(sf, from, fixed);
  for (int i = 1; i <= steps; ++i) {
    const double t = from + (to - from) * static_cast<double>(i) / static_cast<double>(steps);
    const Vec3 cur = fixedIsU ? SurfacePointAt(sf, fixed, t) : SurfacePointAt(sf, t, fixed);
    out->push_back(prev.x);
    out->push_back(prev.y);
    out->push_back(prev.z);
    out->push_back(cur.x);
    out->push_back(cur.y);
    out->push_back(cur.z);
    prev = cur;
  }
}

/// The angles of a global grid of \p count divisions of a full turn that fall STRICTLY inside
/// (\p lo, \p hi).
///
/// Strictly, so an isoline never lands on a seam and doubles an edge that is already drawn. Global,
/// so the lines are evenly spaced around the whole solid rather than around each face — the
/// difference is visible the moment a solid is split into two half-faces, which every curved
/// primitive here is.
void GridAnglesInside(int count, double lo, double hi, std::vector<double>* out) {
  out->clear();
  if (count < 1)
    return;
  const double step = kTwoPi / static_cast<double>(count);
  // Walk a window wide enough to cover any face span, including a full turn.
  const int first = static_cast<int>(std::floor(lo / step)) - 1;
  const int last = static_cast<int>(std::ceil(hi / step)) + 1;
  const double eps = 1e-9;
  for (int k = first; k <= last; ++k) {
    const double a = static_cast<double>(k) * step;
    if (a > lo + eps && a < hi - eps)
      out->push_back(a);
  }
}

} // namespace

bool TessellateIsolines(const Solid& s, int isolineCount, double chordTolerance, std::vector<double>* out,
                        Problem* outWhy) {
  if (!out)
    return false;
  if (!std::isfinite(chordTolerance) || !(chordTolerance > 0.0))
    return Fail(Problem::NonPositiveTolerance, outWhy);
  const Problem why = Validate(s);
  if (why != Problem::Ok)
    return Fail(why, outWhy);

  std::vector<double> segs;
  if (isolineCount < 1) {
    *out = std::move(segs);  // zero is a legal setting: it means "edges only"
    return Succeed(outWhy);
  }

  std::vector<double> angles;
  for (const Face& f : s.faces) {
    const Surface& sf = f.surface;
    if (sf.kind == SurfaceKind::Plane)
      continue;  // flat: its boundary already says everything

    if (sf.kind == SurfaceKind::Nurbs) {
      const nurbs::Patch& patch = sf.patch;
      if (patch.degU == 1 && patch.degV == 1 && patch.ctrl.size() >= 4) {
        // A planar bilinear span is flat — its boundary already says everything. A twisted one is not.
        const Vec3 e1 = ray3d::Sub(patch.ctrl[1], patch.ctrl[0]);
        const Vec3 e2 = ray3d::Sub(patch.ctrl[static_cast<std::size_t>(patch.nu)], patch.ctrl[0]);
        Vec3 nrm = ray3d::Cross(e1, e2);
        const double nl = ray3d::Length(nrm);
        bool planar = true;
        if (nl > 1e-12) {
          nrm = ray3d::Scale(nrm, 1.0 / nl);
          for (const Vec3& c : patch.ctrl)
            if (std::fabs(ray3d::Dot(ray3d::Sub(c, patch.ctrl[0]), nrm)) > chordTolerance) {
              planar = false;
              break;
            }
        }
        if (planar)
          continue;
      }
      // Evenly spaced interior parameter lines both ways — the patch domain is not a full turn, so
      // the global-angle grid the analytic kinds use does not apply. Same evaluator as the shaded
      // triangles (SurfacePointAt), so the wireframe cannot float off the shading.
      const double uA = std::min(f.uStart, f.uEnd);
      const double uB = std::max(f.uStart, f.uEnd);
      const double vA = std::min(f.vStart, f.vEnd);
      const double vB = std::max(f.vStart, f.vEnd);
      double netStep = 0.0;
      for (std::size_t k = 0; k + 1 < patch.ctrl.size(); ++k)
        netStep = std::max(netStep, ray3d::Length(ray3d::Sub(patch.ctrl[k + 1], patch.ctrl[k])));
      const int steps = std::clamp(SegmentsForArc(std::max(netStep, 1e-9), kHalfPi, chordTolerance),
                                   8, 128);
      for (int k = 1; k <= isolineCount; ++k) {
        const double fr = static_cast<double>(k) / static_cast<double>(isolineCount + 1);
        AppendIsoCurve(sf, /*fixedIsU=*/true, uA + (uB - uA) * fr, vA, vB, steps, &segs);
        AppendIsoCurve(sf, /*fixedIsU=*/false, vA + (vB - vA) * fr, uA, uB, steps, &segs);
      }
      continue;
    }

    const double uLo = std::min(f.uStart, f.uEnd);
    const double uHi = std::max(f.uStart, f.uEnd);

    // --- Lines along the face, at constant u -----------------------------------------------------
    GridAnglesInside(isolineCount, uLo, uHi, &angles);
    for (double u : angles) {
      if (sf.kind == SurfaceKind::Cylinder || sf.kind == SurfaceKind::Cone) {
        // A straight ruling from base to top: one segment is exact, since the surface is ruled.
        AppendIsoCurve(sf, /*fixedIsU=*/true, u, 0.0, sf.height, 1, &segs);
      } else {
        const double vLo = std::min(f.vStart, f.vEnd);
        const double vHi = std::max(f.vStart, f.vEnd);
        const double r = sf.kind == SurfaceKind::Sphere ? sf.radius : sf.radius2;
        AppendIsoCurve(sf, true, u, vLo, vHi, SegmentsForArc(r, vHi - vLo, chordTolerance), &segs);
      }
    }

    // --- Rings across the face, at constant v ----------------------------------------------------
    //
    // Skipped for the ruled kinds on purpose. A horizontal ring part way up a cylinder is not
    // something AutoCAD draws, and it reads as an edge that is not there — a seam, or the join of
    // two stacked solids.
    if (sf.kind == SurfaceKind::Cylinder || sf.kind == SurfaceKind::Cone)
      continue;

    const double vLo = std::min(f.vStart, f.vEnd);
    const double vHi = std::max(f.vStart, f.vEnd);
    if (sf.kind == SurfaceKind::Torus) {
      GridAnglesInside(isolineCount, vLo, vHi, &angles);
    } else {
      // A sphere's v is a LATITUDE over [-pi/2, pi/2], not a full turn, so the global-grid rule does
      // not apply: half of that grid's lines would fall outside the surface entirely. Evenly spaced
      // interior latitudes instead, at half the count — a sphere with four meridians and four
      // latitude circles reads as a net rather than as a ball.
      angles.clear();
      const int nv = std::max(1, isolineCount / 2);
      for (int i = 1; i <= nv; ++i)
        angles.push_back(vLo + (vHi - vLo) * static_cast<double>(i) / static_cast<double>(nv + 1));
    }
    for (double v : angles) {
      const double ringR = sf.kind == SurfaceKind::Sphere
                               ? sf.radius * std::cos(v)
                               : sf.radius + sf.radius2 * std::cos(v);
      AppendIsoCurve(sf, /*fixedIsU=*/false, v, uLo, uHi,
                     SegmentsForArc(std::fabs(ringR), uHi - uLo, chordTolerance), &segs);
    }
  }

  *out = std::move(segs);
  return Succeed(outWhy);
}

bool TessellateEdges(const Solid& s, double chordTolerance, std::vector<double>* out, Problem* outWhy) {
  if (!out)
    return false;
  if (!std::isfinite(chordTolerance) || !(chordTolerance > 0.0))
    return Fail(Problem::NonPositiveTolerance, outWhy);
  const Problem why = Validate(s);
  if (why != Problem::Ok)
    return Fail(why, outWhy);

  std::vector<double> segs;
  for (const Edge& e : s.edges) {
    const int n = SegmentsForEdge(e, chordTolerance);
    Vec3 prev = EdgePointAt(s, e, 0.0);
    for (int i = 1; i <= n; ++i) {
      const Vec3 next = EdgePointAt(s, e, static_cast<double>(i) / static_cast<double>(n));
      segs.push_back(prev.x);
      segs.push_back(prev.y);
      segs.push_back(prev.z);
      segs.push_back(next.x);
      segs.push_back(next.y);
      segs.push_back(next.z);
      prev = next;
    }
  }
  *out = std::move(segs);
  return Succeed(outWhy);
}


} // namespace brep
