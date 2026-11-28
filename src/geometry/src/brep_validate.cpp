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
// General trim loops (ADR-052, issue #306): validation of `Face::paramLoops`, a straight-line
// (u,v) polygon used only for inside/outside classification. Local to this translation unit —
// #307/#308/#309 build their own consumers of the same field, not of these helpers.
// ---------------------------------------------------------------------------------------------


/// Shoelace signed area: positive for a CCW polygon.
[[nodiscard]] double PolygonSignedArea2D(const std::vector<curveisect::Vec2>& poly) {
  double acc = 0.0;
  const std::size_t n = poly.size();
  for (std::size_t i = 0; i < n; ++i) {
    const curveisect::Vec2& a = poly[i];
    const curveisect::Vec2& b = poly[(i + 1) % n];
    acc += a.x * b.y - b.x * a.y;
  }
  return 0.5 * acc;
}

/// True when segments p0-p1 and p2-p3 cross at an interior point of both (shared endpoints
/// between CONSECUTIVE polygon edges are not tested — see caller).
[[nodiscard]] bool SegmentsCross(const curveisect::Vec2& p0, const curveisect::Vec2& p1,
                                  const curveisect::Vec2& p2, const curveisect::Vec2& p3) {
  const auto cross = [](const curveisect::Vec2& o, const curveisect::Vec2& a, const curveisect::Vec2& b) {
    return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
  };
  const double d1 = cross(p2, p3, p0);
  const double d2 = cross(p2, p3, p1);
  const double d3 = cross(p0, p1, p2);
  const double d4 = cross(p0, p1, p3);
  // A full-turn trim closes across the periodic seam. A vertex that only touches that seam is not
  // an interior crossing; a real crossing separates the endpoints by a visible gap.
  const double len01 = std::hypot(p1.x - p0.x, p1.y - p0.y);
  const double len23 = std::hypot(p3.x - p2.x, p3.y - p2.y);
  const double touch = 1e-6;
  if (std::fabs(d1) <= touch * std::max(len23, 1e-12) || std::fabs(d2) <= touch * std::max(len23, 1e-12) ||
      std::fabs(d3) <= touch * std::max(len01, 1e-12) || std::fabs(d4) <= touch * std::max(len01, 1e-12))
    return false;
  return ((d1 > 0.0) != (d2 > 0.0)) && ((d3 > 0.0) != (d4 > 0.0));
}

/// A closed polygon (implicitly closed by wrapping the last vertex back to the first) crosses
/// itself when any two NON-ADJACENT edges intersect.
[[nodiscard]] bool PolygonSelfIntersects2D(const std::vector<curveisect::Vec2>& poly) {
  const std::size_t n = poly.size();
  for (std::size_t i = 0; i < n; ++i) {
    const curveisect::Vec2& a0 = poly[i];
    const curveisect::Vec2& a1 = poly[(i + 1) % n];
    for (std::size_t j = i + 1; j < n; ++j) {
      if (j == i || j == (i + 1) % n || (j + 1) % n == i)
        continue;  // shares a vertex with edge i - not a self-intersection
      if (SegmentsCross(a0, a1, poly[j], poly[(j + 1) % n]))
        return true;
    }
  }
  return false;
}

/// Even-odd ray cast: is \p p inside \p poly (closed by wrap-around)?
[[nodiscard]] bool PointInPolygon2D(const curveisect::Vec2& p, const std::vector<curveisect::Vec2>& poly) {
  const std::size_t nEdge = poly.size();
  for (std::size_t i = 0, j = nEdge - 1; i < nEdge; j = i++) {
    const curveisect::Vec2& a = poly[j];
    const curveisect::Vec2& b = poly[i];
    const double abx = b.x - a.x;
    const double aby = b.y - a.y;
    const double ab2 = abx * abx + aby * aby;
    if (!(ab2 > 1e-24))
      continue;
    double t = ((p.x - a.x) * abx + (p.y - a.y) * aby) / ab2;
    t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
    const double dx = p.x - (a.x + abx * t);
    const double dy = p.y - (a.y + aby * t);
    if (dx * dx + dy * dy < 1e-8)
      return true;
  }
  bool inside = false;
  const std::size_t n = poly.size();
  for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
    const curveisect::Vec2& a = poly[i];
    const curveisect::Vec2& b = poly[j];
    if (((a.y > p.y) != (b.y > p.y)) &&
        (p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x))
      inside = !inside;
  }
  return inside;
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// Validity.
// ---------------------------------------------------------------------------------------------

int EulerCharacteristic(const Solid& s) {
  return static_cast<int>(s.vertices.size()) - static_cast<int>(s.edges.size()) +
         static_cast<int>(s.faces.size());
}

Problem Validate(const Solid& s) {
  const int vn = static_cast<int>(s.vertices.size());
  const int en = static_cast<int>(s.edges.size());
  const int fn = static_cast<int>(s.faces.size());

  if (s.shells.empty())
    return Problem::NoShell;
  if (vn == 0 || en == 0 || fn == 0)
    return Problem::EmptyShell;

  for (const Vertex& v : s.vertices) {
    if (!FinitePoint(v.p))
      return Problem::NonFiniteCoordinate;
  }

  const double scale = ModelScale(s);
  const double lenEps = 1e-9 * scale;
  const double areaEps = lenEps * lenEps;

  for (const Edge& e : s.edges) {
    if (e.v0 < 0 || e.v0 >= vn || e.v1 < 0 || e.v1 >= vn)
      return Problem::IndexOutOfRange;
    if (e.kind == CurveKind::Line) {
      if (ray3d::Length(ray3d::Sub(s.vertices[static_cast<std::size_t>(e.v1)].p,
                                   s.vertices[static_cast<std::size_t>(e.v0)].p)) <= lenEps)
        return Problem::DegenerateEdge;
    } else if (e.kind == CurveKind::Intersection) {
      // The curve is defined entirely by its two surfaces and its endpoints — check them here, not a
      // radius or sweep (an Intersection edge carries neither).
      if (e.isectSurfaces.size() != 2)
        return Problem::DegenerateEdge;
      const double onEps = 1e-7 * scale;
      for (const Surface& sf : e.isectSurfaces) {
        if (!AllFinite({sf.radius, sf.radius2, sf.height}) || !FinitePoint(sf.frame.origin))
          return Problem::NonFiniteCoordinate;
        for (const Vec3& p : {s.vertices[static_cast<std::size_t>(e.v0)].p,
                              s.vertices[static_cast<std::size_t>(e.v1)].p, e.frame.origin}) {
          if (!FinitePoint(p))
            return Problem::NonFiniteCoordinate;
          if (ray3d::Length(ray3d::Sub(p, ClosestPointOnSurface(sf, p))) > onEps)
            return Problem::DegenerateEdge;
        }
      }
      const Vec3& a = s.vertices[static_cast<std::size_t>(e.v0)].p;
      const Vec3& b = s.vertices[static_cast<std::size_t>(e.v1)].p;
      // A closed fillet starts and ends on one vertex. It still has length when the witness sits
      // off that vertex.
      if (ray3d::Length(ray3d::Sub(b, a)) <= lenEps &&
          ray3d::Length(ray3d::Sub(e.frame.origin, a)) <= lenEps)
        return Problem::DegenerateEdge;
    } else {
      if (!AllFinite({e.radius, e.radius2, e.sweep}) || !FinitePoint(e.frame.origin))
        return Problem::NonFiniteCoordinate;
      if (!(e.radius > lenEps) || !(std::fabs(e.sweep) > 1e-9))
        return Problem::DegenerateEdge;
      if (e.kind == CurveKind::Ellipse && !(e.radius2 > lenEps))
        return Problem::DegenerateEdge;
    }
  }

  // Every face belongs to exactly one shell, and every shell has faces. A face nobody owns would
  // never be drawn and would still contribute to the volume, which is exactly the sort of quiet
  // disagreement this check exists to prevent.
  std::vector<int> faceShellCount(static_cast<std::size_t>(fn), 0);
  for (const Shell& sh : s.shells) {
    if (sh.faces.empty())
      return Problem::EmptyShell;
    for (int fi : sh.faces) {
      if (fi < 0 || fi >= fn)
        return Problem::IndexOutOfRange;
      ++faceShellCount[static_cast<std::size_t>(fi)];
    }
  }
  for (int c : faceShellCount) {
    if (c != 1)
      return Problem::IndexOutOfRange;
  }

  // Edge-use tally: manifold (twice) and orientable (once each way).
  std::vector<int> forwardUses(static_cast<std::size_t>(en), 0);
  std::vector<int> reverseUses(static_cast<std::size_t>(en), 0);
  std::vector<char> vertexUsed(static_cast<std::size_t>(vn), 0);

  for (const Face& f : s.faces) {
    if (f.loops.empty())
      return Problem::FaceHasNoLoop;
    for (const Loop& lp : f.loops) {
      if (lp.uses.empty())
        return Problem::EmptyLoop;
      for (const EdgeUse& u : lp.uses) {
        if (u.edge < 0 || u.edge >= en)
          return Problem::IndexOutOfRange;
        if (u.reversed)
          ++reverseUses[static_cast<std::size_t>(u.edge)];
        else
          ++forwardUses[static_cast<std::size_t>(u.edge)];
      }
      // Ring closure: each use ends where the next begins, and the last closes onto the first.
      const std::size_t n = lp.uses.size();
      for (std::size_t i = 0; i < n; ++i) {
        const Edge& a = s.edges[static_cast<std::size_t>(lp.uses[i].edge)];
        const Edge& b = s.edges[static_cast<std::size_t>(lp.uses[(i + 1) % n].edge)];
        const int aEnd = lp.uses[i].reversed ? a.v0 : a.v1;
        const int bStart = lp.uses[(i + 1) % n].reversed ? b.v1 : b.v0;
        if (aEnd != bStart)
          return Problem::LoopNotClosed;
      }
    }
    if (f.surface.kind == SurfaceKind::Plane) {
      if (std::fabs(PlaneFaceArea(s, f)) <= areaEps)
        return Problem::DegenerateFace;
    } else if (f.surface.kind == SurfaceKind::Nurbs) {
      // A freeform face is judged on its patch (the kernel's own validator is the truth) and on
      // having a non-empty parameter rectangle in both directions.
      if (nurbs::ValidatePatch(f.surface.patch) != nurbs::PatchProblem::Ok)
        return Problem::DegenerateFace;
      if (!(std::fabs(f.uEnd - f.uStart) > 1e-12) || !(std::fabs(f.vEnd - f.vStart) > 1e-12))
        return Problem::DegenerateFace;
    } else {
      if (!(std::fabs(f.uEnd - f.uStart) > 1e-12))
        return Problem::DegenerateFace;
      if (!(f.surface.radius > lenEps))
        return Problem::DegenerateFace;
      if ((f.surface.kind == SurfaceKind::Sphere || f.surface.kind == SurfaceKind::Torus) &&
          !(std::fabs(f.vEnd - f.vStart) > 1e-12))
        return Problem::DegenerateFace;
    }

    // General trim loop (ADR-052, issue #306). Empty `paramLoops` is the rectangle form checked
    // above and is untouched by any of this - byte-identical to pre-ADR-052 validation.
    if (!f.paramLoops.empty()) {
      if (f.paramLoops.size() != f.loops.size())
        return Problem::GeneralLoopCountMismatch;
      for (std::size_t li = 0; li < f.paramLoops.size(); ++li) {
        const std::vector<curveisect::Vec2>& poly = f.paramLoops[li];
        if (poly.size() < 3)
          return Problem::GeneralLoopOpen;
        if (PolygonSelfIntersects2D(poly))
          return Problem::GeneralLoopSelfIntersects;
        const double area = PolygonSignedArea2D(poly);
        const bool isHole = li != 0;
        if ((isHole && area >= 0.0) || (!isHole && area <= 0.0))
          return Problem::GeneralLoopWrongWinding;
      }
      const std::vector<curveisect::Vec2>& outer = f.paramLoops[0];
      for (std::size_t li = 1; li < f.paramLoops.size(); ++li) {
        for (const curveisect::Vec2& p : f.paramLoops[li]) {
          if (!PointInPolygon2D(p, outer))
            return Problem::GeneralLoopHoleNotNested;
        }
      }
    }
  }

  for (int i = 0; i < en; ++i) {
    const int total = forwardUses[static_cast<std::size_t>(i)] + reverseUses[static_cast<std::size_t>(i)];
    // Fewer than two and more than two are DIFFERENT faults, and #149 acceptance 7 asks to be told
    // them apart: fewer means the shell has a hole, more means the surface is not a manifold.
    // `EdgeNotUsedTwice` reported both under one value whose text says "not closed", which is
    // simply wrong for the three-face case — and the collision hid behind a test fixture that was
    // tripping an earlier check, so it survived a slice written to look for exactly this.
    if (total < 2)
      return Problem::ShellOpenAtEdge;
    if (total > 2)
      return Problem::EdgeNonManifold;
    if (forwardUses[static_cast<std::size_t>(i)] != 1)
      return Problem::EdgeOrientationInconsistent;
    vertexUsed[static_cast<std::size_t>(s.edges[static_cast<std::size_t>(i)].v0)] = 1;
    vertexUsed[static_cast<std::size_t>(s.edges[static_cast<std::size_t>(i)].v1)] = 1;
  }
  for (char c : vertexUsed) {
    if (!c)
      return Problem::UnusedVertex;
  }

  // Finally, the two geometric questions the topology cannot answer.
  //
  // (1) Does the surface close *geometrically*? Everything above checks that the faces are stitched
  //     together correctly, but a face's parametric span is carried alongside its loop, and nothing
  //     so far compares the two: a cylinder face spanning a quarter turn while its boundary runs a
  //     half turn is topologically flawless and geometrically a hole. The test is that the volume
  //     integral is independent of the point it is taken about, which holds for a closed surface
  //     (the integral of n dA over it is zero) and fails for anything else. The offset below is
  //     deliberately generic — nonzero along all three axes, and irrational relative to the model —
  //     so no face's own frame can happen to cancel it.
  // (2) Does it enclose a positive volume, i.e. do the faces point outward rather than inward?
  // `honorGeneralLoops=false`: a `Face::paramLoops` (ADR-052, issue #306) is a classification aid
  // that need not trace the face's real 3D boundary, so this closure probe must judge the shell by
  // its actual edges, not by a general trim shape that could be a placeholder (issue #307).
  const Vec3 q = ReferencePoint(s);
  bool finite = true;
  const double volume = VolumeAbout(s, q, &finite, /*honorGeneralLoops=*/false);
  if (!finite)
    return Problem::NonFiniteCoordinate;

  // A solid with a procedural `Intersection` edge has a face (or two) whose integral is numerical,
  // not closed form (ADR-045 (b) amendment, D-2026-09-02-i), so the point-invariance residual is a
  // quadrature error rather than zero. Relaxed to `1e-5 * scale^3` for those solids — still three
  // orders inside REQ-101's ±0.01 ft on a model-scale part; every analytic solid keeps `1e-8`.
  bool hasNumericFace = false;
  for (const Edge& e : s.edges)
    if (e.kind == CurveKind::Intersection)
      hasNumericFace = true;
  for (const Face& f : s.faces) {
    if (f.surface.kind == SurfaceKind::Nurbs)  // REQ-315: a NURBS face is integrated by quadrature too
      hasNumericFace = true;
    // A SliceConeOblique cut face (issue #283, TASK-204) is also integrated numerically — see
    // IntegrateConeCutFaceNumeric — for the same reason: a cone's non-constant radius means its
    // plane-cut face's u-integral has no tractable closed form (ADR-045 (b), extended).
    if (f.surface.kind == SurfaceKind::Cone && FaceLoopHasEllipseEdge(s, f))
      hasNumericFace = true;
    if ((f.surface.kind == SurfaceKind::Cone || f.surface.kind == SurfaceKind::Cylinder) &&
        RevolutionFaceNeedsBoundaryIntegral(s, f))
      hasNumericFace = true;
  }
  const double closeTol = (hasNumericFace ? 1e-5 : 1e-8) * scale * scale * scale;

  const Vec3 probe = ray3d::Add(q, Vec3{scale, 0.7 * scale, -1.3 * scale});
  const double probeVolume = VolumeAbout(s, probe, nullptr, /*honorGeneralLoops=*/false);
  const bool edgeClosed = std::isfinite(probeVolume) && std::fabs(volume - probeVolume) <= closeTol &&
                          (volume > areaEps * lenEps);
  if (!edgeClosed) {
    // A full-turn cone trimmed by a true ellipse is closed in its (u, v) loops. The edge integral
    // (which ignores those loops) does not see that trim, so it reports a gap the mesh does not have.
    bool finiteTrim = true;
    const double trimVolume = VolumeAbout(s, q, &finiteTrim, /*honorGeneralLoops=*/true);
    const double trimProbe = VolumeAbout(s, probe, nullptr, /*honorGeneralLoops=*/true);
    // Quadrature of a periodic trim is not origin-invariant to 1e-5; half a percent of the volume
    // still rejects a missing face, whose gap is on the order of the part itself.
    const double trimTol = std::max(closeTol, 0.005 * std::max(std::fabs(trimVolume), 1.0));
    if (!finiteTrim || !std::isfinite(trimProbe) || std::fabs(trimVolume - trimProbe) > trimTol ||
        !(trimVolume > areaEps * lenEps))
      return Problem::NotClosed;
  }

  return Problem::Ok;
}

// ---------------------------------------------------------------------------------------------
// Mass properties.
// ---------------------------------------------------------------------------------------------

bool SelfIntersects(const Solid& s) {
  for (const Face& f : s.faces) {
    if (f.surface.kind == SurfaceKind::Torus && f.surface.radius2 >= f.surface.radius)
      return true;
  }
  return false;
}

} // namespace brep
