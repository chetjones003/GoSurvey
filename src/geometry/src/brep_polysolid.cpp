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
// REQ-317 POLYSOLID: a wall swept along a path (ADR-050).
//
// The whole of the difficulty is the corners. Offsetting each segment to each side is easy and
// gives a run of disconnected pieces; making one wall out of them means INTERSECTING adjacent
// offsets so the corner is mitred and counted once. A box per straight run would be far easier and
// is wrong three ways at once — the runs overlap, the drawing holds N objects where the user drew
// one, and the volume double-counts every bend (ADR-050 (b)).
// ---------------------------------------------------------------------------------------------

/// A 2D point in the path frame's plane. `ucs::Point2D` with arithmetic, kept local because it
/// exists only for the two hundred lines below.
struct P2 {
  double x = 0.0;
  double y = 0.0;
};

[[nodiscard]] P2 Sub2(const P2& a, const P2& b) { return P2{a.x - b.x, a.y - b.y}; }
[[nodiscard]] P2 Add2(const P2& a, const P2& b) { return P2{a.x + b.x, a.y + b.y}; }
[[nodiscard]] P2 Mul2(const P2& a, double s) { return P2{a.x * s, a.y * s}; }
[[nodiscard]] double Dot2(const P2& a, const P2& b) { return a.x * b.x + a.y * b.y; }
[[nodiscard]] double Cross2v(const P2& a, const P2& b) { return a.x * b.y - a.y * b.x; }
[[nodiscard]] double Len2(const P2& a) { return std::sqrt(a.x * a.x + a.y * a.y); }
/// Rotate 90 degrees counter-clockwise: the LEFT of a direction of travel.
[[nodiscard]] P2 Left2(const P2& a) { return P2{-a.y, a.x}; }

/// One segment of the centreline, resolved into geometry.
///
/// A straight segment carries its direction; a curved one its centre, radius and signed sweep. Both
/// carry the tangent at each end, which is what the corner code actually asks for — it never needs
/// to know which kind it is holding to decide whether two segments meet smoothly.
struct Seg {
  bool arc = false;
  P2 a{};        ///< start point
  P2 b{};        ///< end point
  P2 dir{};      ///< straight only: unit direction
  P2 centre{};   ///< arc only
  double radius = 0.0;
  double sweep = 0.0;  ///< arc only, signed, CCW positive
  P2 tanA{};     ///< unit tangent at `a`, pointing along travel
  P2 tanB{};     ///< unit tangent at `b`, pointing along travel
};

/// The offset of a \ref Seg to one side. A straight segment offsets to a parallel line; a curved one
/// to a concentric arc about the same centre — which is why an offset keeps the centre rather than
/// recomputing one.
struct OffsetSeg {
  bool arc = false;
  P2 a{};
  P2 b{};
  P2 dir{};
  P2 centre{};
  double radius = 0.0;
  double sweep = 0.0;
};

/// Angle of \p p about \p centre, in [-pi, pi].
[[nodiscard]] double AngleAt(const P2& centre, const P2& p) {
  return std::atan2(p.y - centre.y, p.x - centre.x);
}

/// \p a advanced to \p b in the direction \p sign, as a value in (0, 2*pi) times that sign.
[[nodiscard]] double SweepBetween(double a, double b, double sign) {
  double d = b - a;
  while (d <= 0.0)
    d += kTwoPi;
  while (d > kTwoPi)
    d -= kTwoPi;
  return sign >= 0.0 ? d : d - kTwoPi;
}

/// Resolve one path segment into geometry, or say why it is not one.
[[nodiscard]] bool ResolveSeg(const P2& a, const P2& b, double sweep, double eps, Seg* out) {
  const P2 chord = Sub2(b, a);
  const double c = Len2(chord);
  if (!(c > eps))
    return false;  // a repeated point; a full circle in ONE segment lands here too, by design
  out->a = a;
  out->b = b;
  if (std::fabs(sweep) <= 1e-12) {
    out->arc = false;
    out->dir = Mul2(chord, 1.0 / c);
    out->tanA = out->dir;
    out->tanB = out->dir;
    return true;
  }
  if (std::fabs(sweep) >= kTwoPi)
    return false;  // a segment cannot go all the way round: its own endpoints would coincide
  // Centre from the chord and the included angle. Positive (counter-clockwise) sweep puts the centre
  // to the LEFT of a->b, which is the same convention `AddArc` and every other curve here use.
  const double half = sweep * 0.5;
  const P2 mid = Mul2(Add2(a, b), 0.5);
  const P2 n = Left2(Mul2(chord, 1.0 / c));
  out->arc = true;
  out->centre = Add2(mid, Mul2(n, (c * 0.5) * (std::cos(half) / std::sin(half))));
  out->radius = c / (2.0 * std::fabs(std::sin(half)));
  out->sweep = sweep;
  const double s = sweep > 0.0 ? 1.0 : -1.0;
  // The tangent of a counter-clockwise arc is the outward radius turned a further quarter turn.
  out->tanA = Mul2(Left2(Mul2(Sub2(a, out->centre), 1.0 / out->radius)), s);
  out->tanB = Mul2(Left2(Mul2(Sub2(b, out->centre), 1.0 / out->radius)), s);
  return true;
}

/// \p seg offset by the signed distance \p t along its own left normal.
///
/// \return false when a curve is too tight for the offset — the inner radius reaching zero, where
/// the wall would turn inside out around the bend.
[[nodiscard]] bool OffsetOf(const Seg& seg, double t, double eps, OffsetSeg* out) {
  out->arc = seg.arc;
  if (!seg.arc) {
    const P2 n = Left2(seg.dir);
    out->a = Add2(seg.a, Mul2(n, t));
    out->b = Add2(seg.b, Mul2(n, t));
    out->dir = seg.dir;
    return true;
  }
  // The centre is on the left of a counter-clockwise arc, so moving left means moving toward it.
  const double r = seg.radius - (seg.sweep > 0.0 ? t : -t);
  if (!(r > eps))
    return false;
  const double scale = r / seg.radius;
  out->centre = seg.centre;
  out->radius = r;
  out->sweep = seg.sweep;
  out->a = Add2(seg.centre, Mul2(Sub2(seg.a, seg.centre), scale));
  out->b = Add2(seg.centre, Mul2(Sub2(seg.b, seg.centre), scale));
  return true;
}

/// Where two offset carriers meet, taking the root nearest \p near.
///
/// Three cases and no fourth: line/line solves two lines, line/circle a quadratic, circle/circle the
/// radical line. All closed form; nothing here iterates. Tangent joins — every arc drawn by the
/// command, which is tangent to the segment before it by construction — are handled by the caller
/// before this is reached, so the near-zero discriminant they produce is never relied on.
[[nodiscard]] bool IntersectCarriers(const OffsetSeg& p, const OffsetSeg& q, const P2& near,
                                     double eps, P2* out) {
  auto pick = [&](const P2& r0, const P2& r1, bool two) {
    if (!two) {
      *out = r0;
      return;
    }
    *out = Len2(Sub2(r0, near)) <= Len2(Sub2(r1, near)) ? r0 : r1;
  };

  if (!p.arc && !q.arc) {
    const double d = Cross2v(p.dir, q.dir);
    if (std::fabs(d) <= 1e-12) {
      // Parallel. Continuing straight needs no mitre and the two offsets already coincide; a
      // reversal has no corner point at all and is refused by the caller's collapse check.
      *out = p.b;
      return Dot2(p.dir, q.dir) > 0.0;
    }
    const P2 w = Sub2(q.a, p.a);
    *out = Add2(p.a, Mul2(p.dir, Cross2v(w, q.dir) / d));
    return true;
  }

  if (p.arc != q.arc) {
    const OffsetSeg& line = p.arc ? q : p;
    const OffsetSeg& circ = p.arc ? p : q;
    const P2 f = Sub2(line.a, circ.centre);
    const double b = 2.0 * Dot2(f, line.dir);
    const double c = Dot2(f, f) - circ.radius * circ.radius;
    double disc = b * b - 4.0 * c;
    if (disc < 0.0) {
      if (disc < -eps)
        return false;
      disc = 0.0;  // a tangent join, arriving here only from a path the command did not build
    }
    const double sq = std::sqrt(disc);
    pick(Add2(line.a, Mul2(line.dir, (-b - sq) * 0.5)),
         Add2(line.a, Mul2(line.dir, (-b + sq) * 0.5)), sq > 0.0);
    return true;
  }

  const P2 d = Sub2(q.centre, p.centre);
  const double dist = Len2(d);
  if (!(dist > 1e-12))
    return false;  // concentric: no corner
  const double a = (p.radius * p.radius - q.radius * q.radius + dist * dist) / (2.0 * dist);
  double h2 = p.radius * p.radius - a * a;
  if (h2 < 0.0) {
    if (h2 < -eps)
      return false;
    h2 = 0.0;
  }
  const P2 base = Add2(p.centre, Mul2(d, a / dist));
  const P2 perp = Mul2(Left2(Mul2(d, 1.0 / dist)), std::sqrt(h2));
  pick(Add2(base, perp), Sub2(base, perp), h2 > 0.0);
  return true;
}

/// Do the two segments \p a0-a1 and \p b0-b1 properly cross?
[[nodiscard]] bool SegsCross2D(const P2& a0, const P2& a1, const P2& b0, const P2& b1, double eps) {
  const double d1 = Cross2v(Sub2(a1, a0), Sub2(b0, a0));
  const double d2 = Cross2v(Sub2(a1, a0), Sub2(b1, a0));
  const double d3 = Cross2v(Sub2(b1, b0), Sub2(a0, b0));
  const double d4 = Cross2v(Sub2(b1, b0), Sub2(a1, b0));
  return ((d1 > eps && d2 < -eps) || (d1 < -eps && d2 > eps)) &&
         ((d3 > eps && d4 < -eps) || (d3 < -eps && d4 > eps));
}

} // namespace

bool MakePolysolid(const ucs::Ucs& frame, const Path& path, double width, double height,
                   Justify justify, Solid* out, Problem* outWhy) {
  if (!out)
    return false;  // a null output is a caller bug, not a user-facing reason: outWhy is left alone
  if (!AllFinite({width, height, path.start.x, path.start.y}))
    return Fail(Problem::NonFiniteParameter, outWhy);
  if (!(width > 0.0))
    return Fail(Problem::NonPositiveWidth, outWhy);
  if (!(height > 0.0))
    return Fail(Problem::NonPositiveHeight, outWhy);
  if (!FrameOk(frame))
    return Fail(Problem::DegenerateFrame, outWhy);
  const std::size_t n = path.segs.size();
  if (n < 1 || (path.closed && n < 2))
    return Fail(Problem::PathTooShort, outWhy);
  for (const PathSeg& ps : path.segs) {
    if (!AllFinite({ps.end.x, ps.end.y, ps.sweep}))
      return Fail(Problem::NonFiniteParameter, outWhy);
  }

  // Tolerances scale with the drawing, so a 1 ft wall and a 1000 ft wall are judged on the same
  // relative terms — the argument `ModelScale` already makes for the validity checks.
  double extent = 0.0;
  {
    P2 mn{path.start.x, path.start.y};
    P2 mx = mn;
    for (const PathSeg& ps : path.segs) {
      mn.x = std::min(mn.x, ps.end.x);
      mn.y = std::min(mn.y, ps.end.y);
      mx.x = std::max(mx.x, ps.end.x);
      mx.y = std::max(mx.y, ps.end.y);
    }
    extent = std::max({mx.x - mn.x, mx.y - mn.y, width, 1e-9});
  }
  const double lenEps = 1e-9 * extent;
  const double areaEps = lenEps * extent;

  // --- The centreline ---------------------------------------------------------------------------
  std::vector<Seg> segs(n);
  {
    P2 prev{path.start.x, path.start.y};
    for (std::size_t i = 0; i < n; ++i) {
      const P2 next{path.segs[i].end.x, path.segs[i].end.y};
      if (!ResolveSeg(prev, next, path.segs[i].sweep, lenEps, &segs[i]))
        return Fail(Problem::PathSegmentDegenerate, outWhy);
      prev = next;
    }
    if (path.closed && Len2(Sub2(prev, P2{path.start.x, path.start.y})) > lenEps)
      return Fail(Problem::PathSegmentDegenerate, outWhy);
  }

  // Which way the closed ring winds decides which of the two rails is the OUTER boundary of the cap
  // faces and which is the hole. Shoelace over the stations plus each arc's own bulge — the same
  // decomposition `PlaneLoopSignedArea` uses on a face, for the same reason.
  double ringArea2 = 0.0;
  for (const Seg& sg : segs) {
    ringArea2 += sg.a.x * sg.b.y - sg.b.x * sg.a.y;
    if (sg.arc)
      ringArea2 += sg.radius * sg.radius * (sg.sweep - std::sin(sg.sweep));
  }
  const bool pathCcw = ringArea2 > 0.0;

  // --- The two offset rails ---------------------------------------------------------------------
  double tLeft = 0.0;
  double tRight = 0.0;
  switch (justify) {
  case Justify::Left:   tLeft = 0.0;          tRight = -width;      break;
  case Justify::Center: tLeft = width * 0.5;  tRight = -width * 0.5; break;
  case Justify::Right:  tLeft = width;        tRight = 0.0;         break;
  }

  const std::size_t stations = path.closed ? n : n + 1;
  // One rail: the offset of every segment, mitred at every station it shares with its neighbour.
  auto buildRail = [&](double t, std::vector<OffsetSeg>* rail, std::vector<P2>* pts) -> Problem {
    rail->assign(n, OffsetSeg{});
    for (std::size_t i = 0; i < n; ++i) {
      if (!OffsetOf(segs[i], t, lenEps, &(*rail)[i]))
        return Problem::PolysolidCurveTooTight;
    }
    pts->assign(stations, P2{});
    for (std::size_t j = 0; j < stations; ++j) {
      const bool interior = path.closed || (j > 0 && j < stations - 1);
      if (!interior) {
        (*pts)[j] = j == 0 ? (*rail)[0].a : (*rail)[n - 1].b;
        continue;
      }
      const std::size_t prev = (j + n - 1) % n;
      const std::size_t next = j % n;
      // A SMOOTH join needs no mitre and must not be given one: its two offsets are tangent, so the
      // intersection is a double root and solving for it would trade an exact answer for a
      // near-singular one. Every arc the command draws is tangent to the segment before it, so this
      // is the common path and not the exception.
      const P2& tOut = segs[prev].tanB;
      const P2& tIn = segs[next].tanA;
      if (std::fabs(Cross2v(tOut, tIn)) <= 1e-9 && Dot2(tOut, tIn) > 0.0) {
        (*pts)[j] = Add2(segs[next].a, Mul2(Left2(tIn), t));
        continue;
      }
      if (Dot2(tOut, tIn) <= -1.0 + 1e-9)
        return Problem::PolysolidCornerCollapsed;  // a full reversal has no corner point at all
      if (!IntersectCarriers((*rail)[prev], (*rail)[next], segs[next].a, areaEps, &(*pts)[j]))
        return Problem::PolysolidCornerCollapsed;
    }
    // Re-anchor each offset segment onto the mitred stations, then check it still describes the
    // piece of wall it started as. A corner too sharp, or a segment shorter than the mitre its
    // neighbours demand, shows up here as a piece that has reversed or wrapped right round.
    for (std::size_t i = 0; i < n; ++i) {
      OffsetSeg& os = (*rail)[i];
      os.a = (*pts)[i];
      os.b = (*pts)[(i + 1) % stations];
      if (!os.arc) {
        const P2 d = Sub2(os.b, os.a);
        if (!(Len2(d) > lenEps) || Dot2(d, os.dir) <= 0.0)
          return Problem::PolysolidCornerCollapsed;
        continue;
      }
      const double sign = os.sweep > 0.0 ? 1.0 : -1.0;
      const double sweep = SweepBetween(AngleAt(os.centre, os.a), AngleAt(os.centre, os.b), sign);
      if (!(std::fabs(sweep) > 1e-9) || std::fabs(std::fabs(sweep) - std::fabs(segs[i].sweep)) > kPi)
        return Problem::PolysolidCornerCollapsed;
      os.sweep = sweep;
    }
    return Problem::Ok;
  };

  std::vector<OffsetSeg> railL;
  std::vector<OffsetSeg> railR;
  std::vector<P2> ptsL;
  std::vector<P2> ptsR;
  if (const Problem why = buildRail(tLeft, &railL, &ptsL); why != Problem::Ok)
    return Fail(why, outWhy);
  if (const Problem why = buildRail(tRight, &railR, &ptsR); why != Problem::Ok)
    return Fail(why, outWhy);

  // A path that crosses its own run sweeps a wall enclosing part of the ground twice, and its volume
  // would count that part twice — the silent wrong answer REQ-201 forbids. ADR-045 (f) lets a TORUS
  // pass through itself because that is a shape people draw on purpose and only its mass properties
  // are withheld; a wall crossing itself is an authoring mistake, so it is refused outright.
  //
  // The test runs on paths made ENTIRELY of straight segments, where a rail is a polygon and the
  // answer is exact. With an arc in the path a rail is not a polygon, and testing its chords instead
  // would refuse walls that are perfectly fine — a false refusal being strictly worse than the
  // absence of a check. The general case is the same Phase 4 self-intersection test ADR-045 already
  // defers, and it is stated as a boundary rather than approximated here.
  {
    bool anyArc = false;
    for (const Seg& sg : segs)
      anyArc = anyArc || sg.arc;
    auto crosses = [&](const std::vector<P2>& pts) {
      const std::size_t m = pts.size();
      const std::size_t chords = path.closed ? m : m - 1;
      for (std::size_t i = 0; i < chords; ++i) {
        for (std::size_t j = i + 2; j < chords; ++j) {
          if (path.closed && i == 0 && j == chords - 1)
            continue;  // the ring's first and last chords legitimately share a station
          if (SegsCross2D(pts[i], pts[(i + 1) % m], pts[j], pts[(j + 1) % m], areaEps))
            return true;
        }
      }
      return false;
    };
    if (!anyArc && (crosses(ptsL) || crosses(ptsR)))
      return Fail(Problem::PolysolidPathSelfIntersects, outWhy);
  }

  // --- Topology ---------------------------------------------------------------------------------
  Solid s;
  std::vector<int> bl(stations);
  std::vector<int> br(stations);
  std::vector<int> tl(stations);
  std::vector<int> tr(stations);
  for (std::size_t j = 0; j < stations; ++j) {
    bl[j] = AddVertex(&s, Vec3{ptsL[j].x, ptsL[j].y, 0.0});
    br[j] = AddVertex(&s, Vec3{ptsR[j].x, ptsR[j].y, 0.0});
    tl[j] = AddVertex(&s, Vec3{ptsL[j].x, ptsL[j].y, height});
    tr[j] = AddVertex(&s, Vec3{ptsR[j].x, ptsR[j].y, height});
  }

  const Vec3 up{0.0, 0.0, 1.0};
  auto rail = [&](const OffsetSeg& os, int v0, int v1, double z) {
    return os.arc ? AddArc(&s, v0, v1, Vec3{os.centre.x, os.centre.y, z}, up, os.sweep)
                  : AddLine(&s, v0, v1);
  };
  std::vector<int> eBL(n), eBR(n), eTL(n), eTR(n);
  for (std::size_t i = 0; i < n; ++i) {
    const std::size_t k = (i + 1) % stations;
    eBL[i] = rail(railL[i], bl[i], bl[k], 0.0);
    eBR[i] = rail(railR[i], br[i], br[k], 0.0);
    eTL[i] = rail(railL[i], tl[i], tl[k], height);
    eTR[i] = rail(railR[i], tr[i], tr[k], height);
  }
  std::vector<int> eVL(stations), eVR(stations);
  for (std::size_t j = 0; j < stations; ++j) {
    eVL[j] = AddLine(&s, bl[j], tl[j]);
    eVR[j] = AddLine(&s, br[j], tr[j]);
  }

  // --- Side faces -------------------------------------------------------------------------------
  //
  // The left rail is the wall's left extreme whatever the justification, so its face looks LEFT;
  // the right one looks right. A curved run gives a cylinder rather than a torus — a polysolid
  // extrudes a flat profile straight up, and extruding a planar arc perpendicular to its own plane
  // sweeps a cylinder (ADR-048 (e)).
  auto sideFace = [&](const OffsetSeg& os, const Seg& seg, bool leftSide, std::vector<EdgeUse> uses) {
    Face f;
    if (!os.arc) {
      const P2 nrm = leftSide ? Left2(os.dir) : Mul2(Left2(os.dir), -1.0);
      f.surface = PlaneSurface(Vec3{os.a.x, os.a.y, 0.0}, Vec3{nrm.x, nrm.y, 0.0});
    } else {
      f.surface.kind = SurfaceKind::Cylinder;
      f.surface.frame.origin = Vec3{os.centre.x, os.centre.y, 0.0};
      f.surface.frame.xAxis = Vec3{1.0, 0.0, 0.0};
      f.surface.frame.yAxis = Vec3{0.0, 1.0, 0.0};
      f.surface.frame.zAxis = up;
      f.surface.radius = os.radius;
      f.surface.height = height;
      // Which side of the cylinder the material is on. For a counter-clockwise bend the centre is to
      // the left, so the LEFT rail is the inner one and its face looks INWARD - which is exactly the
      // `Surface::inward` flag REQ-314 B2a added for the wall of a bore, seen from the other side.
      const bool inner = (seg.sweep > 0.0) == leftSide;
      f.surface.inward = inner;
      const double a0 = AngleAt(os.centre, os.a);
      const double a1 = a0 + os.sweep;
      f.uStart = std::min(a0, a1);
      f.uEnd = std::max(a0, a1);
    }
    Loop lp;
    lp.uses = std::move(uses);
    f.loops.push_back(std::move(lp));
    s.faces.push_back(std::move(f));
  };
  for (std::size_t i = 0; i < n; ++i) {
    const std::size_t k = (i + 1) % stations;
    sideFace(railL[i], segs[i], true,
             {{eVL[i], false}, {eTL[i], false}, {eVL[k], true}, {eBL[i], true}});
    sideFace(railR[i], segs[i], false,
             {{eBR[i], false}, {eVR[k], false}, {eTR[i], true}, {eVR[i], true}});
  }

  // --- Caps and the two flat faces --------------------------------------------------------------
  auto railLoop = [&](const std::vector<int>& e, bool forward) {
    std::vector<EdgeUse> uses;
    uses.reserve(n);
    for (std::size_t i = 0; i < n; ++i)
      uses.push_back(EdgeUse{e[forward ? i : n - 1 - i], !forward});
    return uses;
  };

  if (path.closed) {
    // No end caps, and each flat face has a hole: the ring's inner rail. Which rail that is depends
    // on which way the ring winds, so it is decided once, here, from the centreline's own area.
    std::vector<EdgeUse> bottomL = railLoop(eBL, true);
    std::vector<EdgeUse> bottomR = railLoop(eBR, false);
    std::vector<EdgeUse> topR = railLoop(eTR, true);
    std::vector<EdgeUse> topL = railLoop(eTL, false);

    Face bottom;
    bottom.surface = PlaneSurface(Vec3{ptsL[0].x, ptsL[0].y, 0.0}, Vec3{0.0, 0.0, -1.0});
    bottom.loops.push_back(Loop{pathCcw ? std::move(bottomR) : std::move(bottomL)});
    bottom.loops.push_back(Loop{pathCcw ? std::move(bottomL) : std::move(bottomR)});
    s.faces.push_back(std::move(bottom));

    Face top;
    top.surface = PlaneSurface(Vec3{ptsL[0].x, ptsL[0].y, height}, up);
    top.loops.push_back(Loop{pathCcw ? std::move(topR) : std::move(topL)});
    top.loops.push_back(Loop{pathCcw ? std::move(topL) : std::move(topR)});
    s.faces.push_back(std::move(top));
  } else {
    const std::size_t last = stations - 1;
    const int cb0 = AddLine(&s, bl[0], br[0]);
    const int cbN = AddLine(&s, bl[last], br[last]);
    const int ct0 = AddLine(&s, tl[0], tr[0]);
    const int ctN = AddLine(&s, tl[last], tr[last]);

    std::vector<EdgeUse> bottom = railLoop(eBL, true);
    bottom.push_back(EdgeUse{cbN, false});
    for (EdgeUse& u : railLoop(eBR, false))
      bottom.push_back(u);
    bottom.push_back(EdgeUse{cb0, true});
    s.faces.push_back(
        MakePlaneFace(Vec3{ptsL[0].x, ptsL[0].y, 0.0}, Vec3{0.0, 0.0, -1.0}, std::move(bottom)));

    std::vector<EdgeUse> top = railLoop(eTR, true);
    top.push_back(EdgeUse{ctN, true});
    for (EdgeUse& u : railLoop(eTL, false))
      top.push_back(u);
    top.push_back(EdgeUse{ct0, false});
    s.faces.push_back(MakePlaneFace(Vec3{ptsL[0].x, ptsL[0].y, height}, up, std::move(top)));

    const P2 startN = Mul2(segs[0].tanA, -1.0);
    const P2 endN = segs[n - 1].tanB;
    s.faces.push_back(MakePlaneFace(Vec3{ptsL[0].x, ptsL[0].y, 0.0}, Vec3{startN.x, startN.y, 0.0},
                                    {{cb0, false}, {eVR[0], false}, {ct0, true}, {eVL[0], true}}));
    s.faces.push_back(MakePlaneFace(Vec3{ptsL[last].x, ptsL[last].y, 0.0}, Vec3{endN.x, endN.y, 0.0},
                                    {{cbN, true}, {eVL[last], false}, {ctN, false}, {eVR[last], true}}));
  }

  AddSingleShell(&s);
  s.recipe.kind = PrimitiveKind::Polysolid;
  s.recipe.width = width;
  s.recipe.height = height;
  s.recipe.path = path;
  s.recipe.justify = justify;
  PlaceInFrame(&s, frame);

  const Problem why = Validate(s);
  if (why != Problem::Ok)
    return Fail(why, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

} // namespace brep
