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

/// Recognise \p s as one right circular cylinder: two cylinder half-faces at a seam plus two disk
/// caps. Read from the faces, not the recipe, so an extruded circle qualifies too.
[[nodiscard]] bool ClassifyCylinder(const Solid& s, CylinderShape* out) {
  if (s.faces.size() != 4 || s.vertices.size() != 4 || s.edges.size() != 6)
    return false;
  int firstCyl = -1;
  int nCyl = 0;
  int nPlane = 0;
  for (int i = 0; i < 4; ++i) {
    const SurfaceKind k = s.faces[static_cast<std::size_t>(i)].surface.kind;
    if (k == SurfaceKind::Cylinder) {
      ++nCyl;
      if (firstCyl < 0)
        firstCyl = i;
    } else if (k == SurfaceKind::Plane) {
      ++nPlane;
    } else {
      return false;
    }
  }
  if (nCyl != 2 || nPlane != 2)
    return false;
  const Surface& sf = s.faces[static_cast<std::size_t>(firstCyl)].surface;
  if (!(sf.radius > 0.0) || !(sf.height > 0.0))
    return false;
  const double sc = sf.radius + sf.height;
  for (int i = 0; i < 4; ++i) {
    const Surface& g = s.faces[static_cast<std::size_t>(i)].surface;
    if (g.kind != SurfaceKind::Cylinder)
      continue;
    if (std::fabs(g.radius - sf.radius) > 1e-9 * sc || std::fabs(g.height - sf.height) > 1e-9 * sc ||
        ray3d::Length(ray3d::Sub(g.frame.origin, sf.frame.origin)) > 1e-9 * sc ||
        std::fabs(std::fabs(ray3d::Dot(g.frame.zAxis, sf.frame.zAxis)) - 1.0) > 1e-9)
      return false;
  }
  ucs::Ucs ax;
  if (!ucs::FromNormal(sf.frame.origin, sf.frame.zAxis, &ax))
    return false;
  out->axis = ax;
  out->radius = sf.radius;
  out->length = sf.height;
  return true;
}

[[nodiscard]] bool AllFacesPlanar(const Solid& s) {
  for (const Face& f : s.faces)
    if (f.surface.kind != SurfaceKind::Plane)
      return false;
  for (const Edge& e : s.edges)
    if (e.kind != CurveKind::Line)
      return false;
  return true;
}

/// A coaxial stack of cylindrical bands: `z` holds `rOut.size()+1` ascending breakpoints along the
/// axis; band `i` (between `z[i]` and `z[i+1]`) is the annulus `[rIn[i], rOut[i]]`. `rIn` may be
/// empty (a solid stack) or parallel to `rOut` with a 0 meaning "solid". An `rIn[i] > 0` band gets
/// an **inward** inner wall — a bore. Built canonically and placed into \p frame (origin at `z[0]`).
// \p rOutLo / \p rOutHi are the outer radius at each band's z0 / z1 — equal for a cylindrical band,
// different for a conical/tapered one (338d-1). Inner bore walls (\p rIn) stay cylindrical only:
// no case in this kernel yet needs a tapered bore.
[[nodiscard]] bool BuildCoaxialStack(const ucs::Ucs& frame, const std::vector<double>& z,
                                     const std::vector<double>& rOutLo,
                                     const std::vector<double>& rOutHi,
                                     const std::vector<double>& rIn, Solid* out, Problem* outWhy) {
  const int n = static_cast<int>(rOutLo.size());
  if (n < 1 || static_cast<int>(rOutHi.size()) != n || static_cast<int>(z.size()) != n + 1 ||
      (!rIn.empty() && static_cast<int>(rIn.size()) != n))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  auto inAt = [&](int i) { return rIn.empty() ? 0.0 : rIn[static_cast<std::size_t>(i)]; };
  Solid s;
  const Vec3 up{0.0, 0.0, 1.0};
  const double z0 = z.front();
  std::map<std::pair<long long, long long>, std::pair<int, int>> vc;
  std::map<std::pair<long long, long long>, std::pair<int, int>> ec;
  auto key = [](double a, double b) {
    return std::make_pair(static_cast<long long>(std::llround(a * 1e7)),
                          static_cast<long long>(std::llround(b * 1e7)));
  };
  auto verts = [&](double zz, double rr) -> std::pair<int, int> {
    const auto k = key(zz, rr);
    const auto it = vc.find(k);
    if (it != vc.end())
      return it->second;
    const int p = AddVertex(&s, Vec3{rr, 0.0, zz - z0});
    const int m = AddVertex(&s, Vec3{-rr, 0.0, zz - z0});
    return vc[k] = {p, m};
  };
  auto circle = [&](double zz, double rr) -> std::pair<int, int> {
    const auto k = key(zz, rr);
    const auto it = ec.find(k);
    if (it != ec.end())
      return it->second;
    const auto v = verts(zz, rr);
    const Vec3 c{0.0, 0.0, zz - z0};
    const int e0 = AddArc(&s, v.first, v.second, c, up, kPi);   // +x -> -x, CCW about +z
    const int e1 = AddArc(&s, v.second, v.first, c, up, kPi);   // -x -> +x
    return ec[k] = {e0, e1};
  };
  // A cylinder wall (rLo == rHi) or cone wall (rLo != rHi, straight taper) when outer/+radial, or a
  // bore wall (inner, −radial/inward, always cylindrical — rLo == rHi there), z in [zl,zh].
  // `inwardWall` picks which.
  auto addWall = [&](double zl, double zh, double rLo, double rHi, bool inwardWall) {
    const auto cb = circle(zl, rLo);
    const auto ct = circle(zh, rHi);
    const auto vb = verts(zl, rLo);
    const auto vt = verts(zh, rHi);
    const int sm0 = AddLine(&s, vb.first, vt.first);
    const int sm1 = AddLine(&s, vb.second, vt.second);
    auto wall = [&](double u0, double u1, std::vector<EdgeUse> uses) {
      Face f;
      f.surface.kind = std::fabs(rLo - rHi) <= 1e-9 ? SurfaceKind::Cylinder : SurfaceKind::Cone;
      f.surface.frame = ucs::Ucs{};
      f.surface.frame.origin = Vec3{0.0, 0.0, zl - z0};
      f.surface.radius = rLo;
      f.surface.radius2 = rHi;
      f.surface.height = zh - zl;
      f.surface.inward = inwardWall;
      f.uStart = u0;
      f.uEnd = u1;
      Loop lp;
      lp.uses = std::move(uses);
      f.loops.push_back(std::move(lp));
      s.faces.push_back(std::move(f));
    };
    if (inwardWall) {  // the boundary loop runs the opposite way, so every rim edge is used once each way
      wall(0.0, kPi, {{sm0, false}, {ct.first, false}, {sm1, true}, {cb.first, true}});
      wall(kPi, kTwoPi, {{sm1, false}, {ct.second, false}, {sm0, true}, {cb.second, true}});
    } else {
      wall(0.0, kPi, {{cb.first, false}, {sm1, false}, {ct.first, true}, {sm0, true}});
      wall(kPi, kTwoPi, {{cb.second, false}, {sm0, false}, {ct.second, true}, {sm1, true}});
    }
  };
  for (int i = 0; i < n; ++i) {
    addWall(z[static_cast<std::size_t>(i)], z[static_cast<std::size_t>(i + 1)],
            rOutLo[static_cast<std::size_t>(i)], rOutHi[static_cast<std::size_t>(i)],
            /*inwardWall=*/false);
    if (inAt(i) > 0.0)
      addWall(z[static_cast<std::size_t>(i)], z[static_cast<std::size_t>(i + 1)], inAt(i), inAt(i),
              /*inwardWall=*/true);
  }
  // A horizontal annular face between radii [inner, outer] at height zz; `matBelow` true when the
  // material is on the −z side (so the face's outward normal is +z), false for +z-side material.
  auto addRing = [&](double zz, double inner, double outer, bool matBelow) {
    if (!(outer - inner > 1e-9))
      return;
    const Vec3 nrm = matBelow ? up : Vec3{0.0, 0.0, -1.0};
    const auto co = circle(zz, outer);
    Face f = MakePlaneFace(Vec3{0.0, 0.0, zz - z0}, nrm, {});
    f.loops.clear();
    Loop outerL;
    if (matBelow)
      outerL.uses = {{co.first, false}, {co.second, false}};
    else
      outerL.uses = {{co.second, true}, {co.first, true}};
    f.loops.push_back(std::move(outerL));
    if (inner > 1e-9) {
      const auto ci = circle(zz, inner);
      Loop innerL;
      if (matBelow)
        innerL.uses = {{ci.second, true}, {ci.first, true}};
      else
        innerL.uses = {{ci.first, false}, {ci.second, false}};
      f.loops.push_back(std::move(innerL));
    }
    s.faces.push_back(std::move(f));
  };
  for (int k = 0; k <= n; ++k) {
    const double zz = z[static_cast<std::size_t>(k)];
    // The radius each neighbouring band actually reaches AT this boundary — its own r1 for the band
    // below, its own r0 for the band above (equal to the flat mid-band radius for a cylinder, and
    // the taper's near-end radius for a cone), so a step is detected even when one side is tapering.
    const double outL = (k > 0) ? rOutHi[static_cast<std::size_t>(k - 1)] : 0.0;
    const double outH = (k < n) ? rOutLo[static_cast<std::size_t>(k)] : 0.0;
    const double inL = (k > 0) ? inAt(k - 1) : 0.0;
    const double inH = (k < n) ? inAt(k) : 0.0;
    if (k == 0) {
      addRing(zz, inH, outH, /*matBelow=*/false);  // bottom cap, material above
    } else if (k == n) {
      addRing(zz, inL, outL, /*matBelow=*/true);  // top cap, material below
    } else {
      // Outer transition: the wider band's material overhangs the thinner one.
      if (std::fabs(outL - outH) > 1e-9)
        addRing(zz, std::min(outL, outH), std::max(outL, outH), /*matBelow=*/outL > outH);
      // Inner transition: the band with the smaller bore has material where the other is void.
      if (std::fabs(inL - inH) > 1e-9)
        addRing(zz, std::min(inL, inH), std::max(inL, inH), /*matBelow=*/inL < inH);
    }
  }
  AddSingleShell(&s);
  PlaceInFrame(&s, frame);
  if (Validate(s) != Problem::Ok)
    return Fail(Problem::BooleanResultInvalid, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

namespace {

/// One stub of a boss: the planar face to bore, the bore centre on it, the outward direction, and
/// the stub length.
struct BossStub {
  int face = 0;
  Vec3 centre;
  Vec3 dir;
  double length = 0.0;
};

/// \p planar with a cylindrical \p radius boss added at each stub: the face is bored open (an inner
/// circular loop) and a cylinder + end cap carry the material outward.
[[nodiscard]] bool BuildBoss(const Solid& planar, const std::vector<BossStub>& stubs, double radius,
                             Solid* out, Problem* outWhy) {
  Solid s = planar;
  s.recipe = Recipe{};  // a Boolean result carries no recipe (REQ-314)
  for (const BossStub& stub : stubs) {
    const Vec3 dir = ray3d::Normalize(stub.dir);
    Vec3 xa = s.faces[static_cast<std::size_t>(stub.face)].surface.frame.xAxis;
    xa = ray3d::Sub(xa, ray3d::Scale(dir, ray3d::Dot(xa, dir)));
    if (!(ray3d::Length(xa) > 1e-9))
      xa = s.faces[static_cast<std::size_t>(stub.face)].surface.frame.yAxis;
    xa = ray3d::Normalize(xa);
    const Vec3 ya = ray3d::Normalize(ray3d::Cross(dir, xa));
    const Vec3 capC = ray3d::Add(stub.centre, ray3d::Scale(dir, stub.length));
    const int b0 = AddVertex(&s, ray3d::Add(stub.centre, ray3d::Scale(xa, radius)));
    const int b1 = AddVertex(&s, ray3d::Add(stub.centre, ray3d::Scale(xa, -radius)));
    const int t0 = AddVertex(&s, ray3d::Add(capC, ray3d::Scale(xa, radius)));
    const int t1 = AddVertex(&s, ray3d::Add(capC, ray3d::Scale(xa, -radius)));
    const int rb0 = AddArc(&s, b0, b1, stub.centre, dir, kPi);
    const int rb1 = AddArc(&s, b1, b0, stub.centre, dir, kPi);
    const int rt0 = AddArc(&s, t0, t1, capC, dir, kPi);
    const int rt1 = AddArc(&s, t1, t0, capC, dir, kPi);
    const int sm0 = AddLine(&s, b0, t0);
    const int sm1 = AddLine(&s, b1, t1);
    s.faces.push_back(MakePlaneFace(capC, dir, {{rt0, false}, {rt1, false}}));
    auto wall = [&](double u0, double u1, std::vector<EdgeUse> uses) {
      Face f;
      f.surface.kind = SurfaceKind::Cylinder;
      f.surface.frame.origin = stub.centre;
      f.surface.frame.xAxis = xa;
      f.surface.frame.yAxis = ya;
      f.surface.frame.zAxis = dir;
      f.surface.radius = radius;
      f.surface.radius2 = radius;
      f.surface.height = stub.length;
      f.uStart = u0;
      f.uEnd = u1;
      Loop lp;
      lp.uses = std::move(uses);
      f.loops.push_back(std::move(lp));
      s.faces.push_back(std::move(f));
    };
    wall(0.0, kPi, {{rb0, false}, {sm1, false}, {rt0, true}, {sm0, true}});
    wall(kPi, kTwoPi, {{rb1, false}, {sm0, false}, {rt1, true}, {sm1, true}});
    // Bore the face: an inner loop wound opposite the outer one (the missing near-end cap's loop).
    s.faces[static_cast<std::size_t>(stub.face)].loops.push_back(Loop{{{rb1, true}, {rb0, true}}});
  }
  for (int i = static_cast<int>(planar.faces.size()); i < static_cast<int>(s.faces.size()); ++i)
    s.shells[0].faces.push_back(i);
  if (Validate(s) != Problem::Ok || SelfIntersects(s))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}
} // namespace

/// \p planar with a cylindrical bore of \p radius removed (REQ-314 B2a — a curved SUBTRACT). The
/// entry face is bored open at \p entryC; if \p through, the exit face is too and an inward cylinder
/// wall spans the two; otherwise the bore is blind, \p blindDepth deep, closed by a planar floor.
/// \p nEntry is the entry face's outward normal; the bore runs along `-nEntry` into the solid.
[[nodiscard]] bool BuildBore(const Solid& planar, int entryFace, const Vec3& entryC, const Vec3& nEntry,
                             double radius, bool through, int exitFace, const Vec3& exitC,
                             double blindDepth, Solid* out, Problem* outWhy) {
  const Vec3 nE = ray3d::Normalize(nEntry);
  const Vec3 dir = ray3d::Scale(nE, -1.0);  // into the solid
  Solid s = planar;
  s.recipe = Recipe{};
  Vec3 xa = s.faces[static_cast<std::size_t>(entryFace)].surface.frame.xAxis;
  xa = ray3d::Sub(xa, ray3d::Scale(nE, ray3d::Dot(xa, nE)));
  if (!(ray3d::Length(xa) > 1e-9))
    xa = s.faces[static_cast<std::size_t>(entryFace)].surface.frame.yAxis;
  xa = ray3d::Normalize(xa);
  const Vec3 ya = ray3d::Normalize(ray3d::Cross(dir, xa));
  const double h = through ? ray3d::Length(ray3d::Sub(exitC, entryC)) : blindDepth;
  if (!(h > 1e-9) || !(radius > 0.0))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  const Vec3 farC = ray3d::Add(entryC, ray3d::Scale(dir, h));
  const int e0 = AddVertex(&s, ray3d::Add(entryC, ray3d::Scale(xa, radius)));
  const int e1 = AddVertex(&s, ray3d::Add(entryC, ray3d::Scale(xa, -radius)));
  const int f0 = AddVertex(&s, ray3d::Add(farC, ray3d::Scale(xa, radius)));
  const int f1 = AddVertex(&s, ray3d::Add(farC, ray3d::Scale(xa, -radius)));
  // All four rim arcs are built about the ENTRY face's outward normal, so "forward" means the same
  // angular sense at both ends — the BuildBoss convention.
  const int re0 = AddArc(&s, e0, e1, entryC, nE, kPi);
  const int re1 = AddArc(&s, e1, e0, entryC, nE, kPi);
  const int rf0 = AddArc(&s, f0, f1, farC, nE, kPi);
  const int rf1 = AddArc(&s, f1, f0, farC, nE, kPi);
  const int sm0 = AddLine(&s, e0, f0);
  const int sm1 = AddLine(&s, e1, f1);
  auto wall = [&](double u0, double u1, std::vector<EdgeUse> uses) {
    Face fc;
    fc.surface.kind = SurfaceKind::Cylinder;
    fc.surface.frame.origin = entryC;
    fc.surface.frame.xAxis = xa;
    fc.surface.frame.yAxis = ya;
    fc.surface.frame.zAxis = dir;
    fc.surface.radius = radius;
    fc.surface.radius2 = radius;
    fc.surface.height = h;
    fc.surface.inward = true;
    fc.uStart = u0;
    fc.uEnd = u1;
    Loop lp;
    lp.uses = std::move(uses);
    fc.loops.push_back(std::move(lp));
    s.faces.push_back(std::move(fc));
  };
  wall(0.0, kPi, {{re0, false}, {sm1, false}, {rf0, true}, {sm0, true}});
  wall(kPi, kTwoPi, {{re1, false}, {sm0, false}, {rf1, true}, {sm1, true}});
  s.faces[static_cast<std::size_t>(entryFace)].loops.push_back(Loop{{{re1, true}, {re0, true}}});
  if (through)
    s.faces[static_cast<std::size_t>(exitFace)].loops.push_back(Loop{{{rf0, false}, {rf1, false}}});
  else
    s.faces.push_back(MakePlaneFace(farC, nE, {{rf0, false}, {rf1, false}}));  // floor faces back out
  for (int i = static_cast<int>(planar.faces.size()); i < static_cast<int>(s.faces.size()); ++i)
    s.shells[0].faces.push_back(i);
  if (Validate(s) != Problem::Ok || SelfIntersects(s))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

/// \p planar with a **stepped, coaxial, through** bore removed in one pass — a shouldered/counter-
/// bored hole (issue #493 / REQ-337), generalizing \ref BuildBore's single-radius tunnel to an
/// ordered list of radius/length segments along one axis. Scoped to a **non-increasing** radius
/// sequence from entry to exit (the wide end always faces the entry, narrowing or holding steady
/// deeper in — a real counterbore/shouldered-pin shape) so every internal transition's shoulder
/// ring has one, unambiguous, always-correct winding; a widening sequence is the caller's job to
/// refuse before calling this (REQ-337's own scope boundary, not a bug here).
///
/// `BuildBore`'s sequential-cut alternative — subtracting each segment's own cylinder one at a time
/// into the solid the previous cut already produced — was tried first and found to be unreliable:
/// the general Boolean classifier that \ref SubtractCircle ultimately calls refuses once the target
/// already carries an inward cylindrical face from a prior cut nearby, which is exactly every
/// internal segment boundary. Building the whole stepped tunnel's topology directly, the same way
/// \ref BuildBore already does for one radius, sidesteps that composability gap entirely.
/// \p radii has N entries (one per segment); \p internalShoulders has N-1 **absolute** positions,
/// entry-to-exit order — the true world point of each radius change, independent of where the
/// cutter's OWN geometry started or ended (the entry/exit segments are clipped to \p entryC / the
/// found exit point, which will generally sit inside the cutter's own overhanging extent — a
/// through-cutter is deliberately longer than the target it bores, exactly like every other bore
/// builder here already assumes). Passing lengths instead of absolute shoulder positions was the
/// first attempt and was wrong for exactly this reason: it silently used the cutter's OWN full
/// segment length as the entry segment's cut depth, which is longer than the target actually needs
/// wherever the cutter overhangs the entry face — REQ-337.
[[nodiscard]] bool BuildSteppedBore(const Solid& planar, int entryFace, const Vec3& entryC,
                                    const Vec3& nEntry, const std::vector<double>& radii,
                                    const std::vector<Vec3>& internalShoulders, int exitFace,
                                    const Vec3& exitC, Solid* out, Problem* outWhy) {
  if (radii.empty() || internalShoulders.size() + 1 != radii.size())
    return Fail(Problem::BooleanResultInvalid, outWhy);
  for (size_t i = 1; i < radii.size(); ++i) {
    if (radii[i] > radii[i - 1] + 1e-9)
      return Fail(Problem::BooleanCurvedFace, outWhy);  // widening — outside this builder's scope
  }
  const Vec3 nE = ray3d::Normalize(nEntry);
  const Vec3 dir = ray3d::Scale(nE, -1.0);  // into the solid, entry toward exit
  Solid s = planar;
  s.recipe = Recipe{};
  Vec3 xa = s.faces[static_cast<std::size_t>(entryFace)].surface.frame.xAxis;
  xa = ray3d::Sub(xa, ray3d::Scale(nE, ray3d::Dot(xa, nE)));
  if (!(ray3d::Length(xa) > 1e-9))
    xa = s.faces[static_cast<std::size_t>(entryFace)].surface.frame.yAxis;
  xa = ray3d::Normalize(xa);

  const size_t N = radii.size();
  std::vector<Vec3> boundary(N + 1);
  boundary[0] = entryC;
  for (size_t i = 0; i + 1 < N; ++i)
    boundary[i + 1] = internalShoulders[i];
  boundary[N] = exitC;
  std::vector<std::pair<double, double>> segs(N);
  for (size_t i = 0; i < N; ++i) {
    segs[i].first = radii[i];
    segs[i].second = ray3d::Length(ray3d::Sub(boundary[i + 1], boundary[i]));
    if (!(segs[i].first > 0.0) || !(segs[i].second > 1e-9))
      return Fail(Problem::BooleanResultInvalid, outWhy);
  }

  struct Rim {
    int v0 = -1, v1 = -1, arcFwd = -1, arcBack = -1;
  };
  auto makeRim = [&](const Vec3& c, double r) -> Rim {
    Rim rim;
    rim.v0 = AddVertex(&s, ray3d::Add(c, ray3d::Scale(xa, r)));
    rim.v1 = AddVertex(&s, ray3d::Add(c, ray3d::Scale(xa, -r)));
    rim.arcFwd = AddArc(&s, rim.v0, rim.v1, c, nE, kPi);
    rim.arcBack = AddArc(&s, rim.v1, rim.v0, c, nE, kPi);
    return rim;
  };

  const Rim entryRim = makeRim(boundary[0], segs[0].first);
  s.faces[static_cast<std::size_t>(entryFace)].loops.push_back(
      Loop{{{entryRim.arcBack, true}, {entryRim.arcFwd, true}}});
  const Rim exitRim = makeRim(boundary[N], segs[N - 1].first);
  s.faces[static_cast<std::size_t>(exitFace)].loops.push_back(
      Loop{{{exitRim.arcFwd, false}, {exitRim.arcBack, false}}});

  Rim prevRim = entryRim;
  double prevRadius = segs[0].first;
  for (size_t i = 0; i < N; ++i) {
    const double r = segs[i].first;
    Rim nearRim;
    if (i == 0) {
      nearRim = prevRim;
    } else if (std::fabs(prevRadius - r) < 1e-9) {
      nearRim = prevRim;
    } else {
      nearRim = makeRim(boundary[i], r);
      // Shoulder ring at boundary[i]: outer boundary at the WIDER (previous) radius, a hole at
      // this (narrower) radius. True outward normal is nE — the same "back toward entry" side as
      // the entry face itself, so the outer loop takes the OPPOSITE winding from a hole loop on a
      // same-normal face (entry's own hole above is reversed, so a brand-new face's own outer
      // boundary here is forward).
      Face shoulder = MakePlaneFace(boundary[i], nE, {{prevRim.arcFwd, false}, {prevRim.arcBack, false}});
      shoulder.loops.push_back(Loop{{{nearRim.arcBack, true}, {nearRim.arcFwd, true}}});
      s.faces.push_back(std::move(shoulder));
    }
    const Rim farRim = (i + 1 == N) ? exitRim : makeRim(boundary[i + 1], r);
    const int sm0 = AddLine(&s, nearRim.v0, farRim.v0);
    const int sm1 = AddLine(&s, nearRim.v1, farRim.v1);
    auto wallFace = [&](double u0, double u1, std::vector<EdgeUse> uses) {
      Face fc;
      fc.surface.kind = SurfaceKind::Cylinder;
      fc.surface.frame.origin = boundary[i];
      fc.surface.frame.xAxis = xa;
      fc.surface.frame.yAxis = ray3d::Normalize(ray3d::Cross(dir, xa));
      fc.surface.frame.zAxis = dir;
      fc.surface.radius = r;
      fc.surface.radius2 = r;
      fc.surface.height = segs[i].second;
      fc.surface.inward = true;
      fc.uStart = u0;
      fc.uEnd = u1;
      Loop lp;
      lp.uses = std::move(uses);
      fc.loops.push_back(std::move(lp));
      s.faces.push_back(std::move(fc));
    };
    wallFace(0.0, kPi, {{nearRim.arcFwd, false}, {sm1, false}, {farRim.arcFwd, true}, {sm0, true}});
    wallFace(kPi, kTwoPi, {{nearRim.arcBack, false}, {sm0, false}, {farRim.arcBack, true}, {sm1, true}});
    prevRim = farRim;
    prevRadius = r;
  }

  for (int i = static_cast<int>(planar.faces.size()); i < static_cast<int>(s.faces.size()); ++i)
    s.shells[0].faces.push_back(i);
  if (Validate(s) != Problem::Ok || SelfIntersects(s))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

namespace {

/// The ellipse where a plane (through \p planePt, unit normal \p pn) cuts the cylinder of radius
/// \p r about \p axisFrame. Fills the world centre / major direction / oriented normal / semi-axes.
struct CylEllipse {
  Vec3 centre;
  Vec3 majorDir;
  Vec3 normal;  // oriented so normal·axis > 0
  double a = 0.0;
  double b = 0.0;
  double alpha = 0.0;  // z_plane on the axis, in the axis frame
};
[[nodiscard]] bool EllipseFromPlane(const ucs::Ucs& axisFrame, double r, const Vec3& planePt,
                                    const Vec3& pn, CylEllipse* out) {
  const Vec3 Z = axisFrame.zAxis;
  const double dotNZ = ray3d::Dot(pn, Z);
  if (std::fabs(dotNZ) < 1e-6)
    return false;
  const double tAxis = ray3d::Dot(ray3d::Sub(planePt, axisFrame.origin), pn) / dotNZ;
  out->centre = ray3d::Add(axisFrame.origin, ray3d::Scale(Z, tAxis));
  out->alpha = tAxis;
  const Vec3 minorDir = ray3d::Normalize(ray3d::Cross(pn, Z));
  out->majorDir = ray3d::Normalize(ray3d::Cross(pn, minorDir));
  out->normal = dotNZ > 0.0 ? pn : ray3d::Scale(pn, -1.0);
  out->a = r / std::fabs(dotNZ);
  out->b = r;
  return true;
}

/// A cylinder segment of radius \p r about \p axisFrame bounded by two oblique planes — the plug of
/// an INTERSECT of a tilted cylinder with a planar-faced solid (REQ-314 B2b-1). Both ends are
/// elliptical; no flat cap.
[[nodiscard]] bool BuildObliqueCylinderPlug(const ucs::Ucs& axisFrame, double r, const Vec3& p1,
                                            const Vec3& n1, const Vec3& p2, const Vec3& n2, Solid* out,
                                            Problem* outWhy) {
  CylEllipse eA;
  CylEllipse eB;
  if (!EllipseFromPlane(axisFrame, r, p1, n1, &eA) || !EllipseFromPlane(axisFrame, r, p2, n2, &eB))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  const CylEllipse& lo = eA.alpha <= eB.alpha ? eA : eB;
  const CylEllipse& hi = eA.alpha <= eB.alpha ? eB : eA;
  if (hi.alpha - lo.alpha < 1e-9)
    return Fail(Problem::BooleanEmptyResult, outWhy);
  const Vec3 Z = axisFrame.zAxis;
  auto W = [&](double x, double y, double z) { return ucs::UcsToWorld(axisFrame, Vec3{x, y, z}); };
  // z_plane(u=0) = alpha + beta ; here beta = -r * (n·X)/(n·Z). Recover it for the seam heights.
  auto zAtSeam = [&](const CylEllipse& e, double sx) {
    const Vec3 nl{ray3d::Dot(e.normal, axisFrame.xAxis), ray3d::Dot(e.normal, axisFrame.yAxis),
                  ray3d::Dot(e.normal, Z)};
    return e.alpha - r * nl.x / nl.z * sx;  // sx = cos u  (±1 at the seam)
  };
  Solid s;
  const int lo0 = AddVertex(&s, W(r, 0.0, zAtSeam(lo, 1.0)));
  const int lo1 = AddVertex(&s, W(-r, 0.0, zAtSeam(lo, -1.0)));
  const int hi0 = AddVertex(&s, W(r, 0.0, zAtSeam(hi, 1.0)));
  const int hi1 = AddVertex(&s, W(-r, 0.0, zAtSeam(hi, -1.0)));
  const int elLo0 = AddEllipse(&s, lo0, lo1, lo.centre, lo.normal, lo.majorDir, lo.a, lo.b, kPi);
  const int elLo1 = AddEllipse(&s, lo1, lo0, lo.centre, lo.normal, lo.majorDir, lo.a, lo.b, kPi);
  const int elHi0 = AddEllipse(&s, hi0, hi1, hi.centre, hi.normal, hi.majorDir, hi.a, hi.b, kPi);
  const int elHi1 = AddEllipse(&s, hi1, hi0, hi.centre, hi.normal, hi.majorDir, hi.a, hi.b, kPi);
  const int sm0 = AddLine(&s, lo0, hi0);
  const int sm1 = AddLine(&s, lo1, hi1);
  s.faces.push_back(
      MakePlaneFace(lo.centre, ray3d::Scale(lo.normal, -1.0), {{elLo1, true}, {elLo0, true}}));
  s.faces.push_back(MakePlaneFace(hi.centre, hi.normal, {{elHi0, false}, {elHi1, false}}));
  auto side = [&](double u0, double u1, std::vector<EdgeUse> uses) {
    Face f;
    f.surface.kind = SurfaceKind::Cylinder;
    f.surface.frame = axisFrame;
    f.surface.frame.origin = lo.centre;
    f.surface.radius = r;
    f.surface.radius2 = r;
    f.surface.height = hi.alpha - lo.alpha;
    f.uStart = u0;
    f.uEnd = u1;
    Loop lp;
    lp.uses = std::move(uses);
    f.loops.push_back(std::move(lp));
    s.faces.push_back(std::move(f));
  };
  side(0.0, kPi, {{elLo0, false}, {sm1, false}, {elHi0, true}, {sm0, true}});
  side(kPi, kTwoPi, {{elLo1, false}, {sm0, false}, {elHi1, true}, {sm1, true}});
  AddSingleShell(&s);
  if (Validate(s) != Problem::Ok || SelfIntersects(s))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

/// \p planar with a **tilted** cylindrical bore of radius \p r removed along \p axisFrame — the
/// elliptical-mouthed SUBTRACT of a tilted cylinder that crosses faces \p fA and \p fB
/// (REQ-314 B2b-1). Each face is bored with an ellipse; an inward cylinder wall spans the two.
[[nodiscard]] bool BuildTiltedBore(const Solid& planar, int fA, const Vec3& nA, int fB, const Vec3& nB,
                                   const ucs::Ucs& axisFrame, double r, Solid* out, Problem* outWhy) {
  Solid s = planar;
  s.recipe = Recipe{};
  const Vec3 Z = axisFrame.zAxis;
  auto W = [&](double x, double y, double z) { return ucs::UcsToWorld(axisFrame, Vec3{x, y, z}); };

  struct FaceEll {
    int face;
    CylEllipse e;
    Vec3 s0;  // world ellipse ∩ seam at cyl u = 0
    Vec3 s1;  // ... at u = pi
    int e0 = -1;
    int e1 = -1;  // half-ellipse arcs, s0->s1 and s1->s0
    double sweepSign = 1.0;
  };
  auto prep = [&](int face, const Vec3& faceN) -> FaceEll {
    FaceEll fe{face, {}, {}, {}, -1, -1, 1.0};
    const Vec3 pt = s.vertices[static_cast<std::size_t>(
                                   s.edges[static_cast<std::size_t>(s.faces[static_cast<std::size_t>(face)]
                                                                        .loops[0]
                                                                        .uses[0]
                                                                        .edge)]
                                       .v0)]
                        .p;
    (void)EllipseFromPlane(axisFrame, r, pt, faceN, &fe.e);
    // seam heights in the axis frame: z_plane(u) = alpha + beta cos u ; beta = -r*(n·X)/(n·Z).
    const Vec3 nl{ray3d::Dot(fe.e.normal, axisFrame.xAxis), ray3d::Dot(fe.e.normal, axisFrame.yAxis),
                  ray3d::Dot(fe.e.normal, Z)};
    const double beta = -r * nl.x / nl.z;
    const double gamma = -r * nl.y / nl.z;
    fe.s0 = W(r, 0.0, fe.e.alpha + beta);
    fe.s1 = W(-r, 0.0, fe.e.alpha - beta);
    (void)gamma;
    // the ellipse edge is built about the FACE outward normal, so the bored-face hole winds right.
    fe.sweepSign = ray3d::Dot(faceN, Z) > 0.0 ? 1.0 : -1.0;
    return fe;
  };
  FaceEll a = prep(fA, nA);
  FaceEll b = prep(fB, nB);
  // order lo/hi by axis param so the inward wall's two ellipse ends match CylinderCutZExtent.
  if (a.e.alpha > b.e.alpha)
    std::swap(a, b);

  auto addArcs = [&](FaceEll& fe, const Vec3& faceN) {
    const int v0 = AddVertex(&s, fe.s0);
    const int v1 = AddVertex(&s, fe.s1);
    fe.e0 = AddEllipse(&s, v0, v1, fe.e.centre, faceN, fe.e.majorDir, fe.e.a, fe.e.b,
                       fe.sweepSign * kPi);
    fe.e1 = AddEllipse(&s, v1, v0, fe.e.centre, faceN, fe.e.majorDir, fe.e.a, fe.e.b,
                       fe.sweepSign * kPi);
  };
  addArcs(a, a.face == fA ? nA : nB);
  addArcs(b, b.face == fA ? nA : nB);

  const int sm0 = AddLine(&s, s.edges[static_cast<std::size_t>(a.e0)].v0,
                          s.edges[static_cast<std::size_t>(b.e0)].v0);  // +x seam, lo→hi
  const int sm1 = AddLine(&s, s.edges[static_cast<std::size_t>(a.e1)].v0,
                          s.edges[static_cast<std::size_t>(b.e1)].v0);  // -x seam

  auto wall = [&](double u0, double u1, std::vector<EdgeUse> uses) {
    Face f;
    f.surface.kind = SurfaceKind::Cylinder;
    f.surface.frame = axisFrame;
    f.surface.frame.origin = a.e.centre;
    f.surface.radius = r;
    f.surface.radius2 = r;
    f.surface.height = b.e.alpha - a.e.alpha;
    f.surface.inward = true;
    f.uStart = u0;
    f.uEnd = u1;
    Loop lp;
    lp.uses = std::move(uses);
    f.loops.push_back(std::move(lp));
    s.faces.push_back(std::move(f));
  };
  // Same edge-use pattern as BuildBore's inward wall.
  wall(0.0, kPi, {{sm0, false}, {b.e0, false}, {sm1, true}, {a.e0, true}});
  wall(kPi, kTwoPi, {{sm1, false}, {b.e1, false}, {sm0, true}, {a.e1, true}});
  // Bore each face with the ellipse, wound opposite the wall's use of that arc.
  s.faces[static_cast<std::size_t>(a.face)].loops.push_back(Loop{{{a.e0, false}, {a.e1, false}}});
  s.faces[static_cast<std::size_t>(b.face)].loops.push_back(Loop{{{b.e1, true}, {b.e0, true}}});
  for (int i = static_cast<int>(planar.faces.size()); i < static_cast<int>(s.faces.size()); ++i)
    s.shells[0].faces.push_back(i);
  if (Validate(s) != Problem::Ok || SelfIntersects(s))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

/// Append one boss stub for a tilted cylinder poking through planar face \p faceIdx: the piece of
/// the cylinder between its own circular cap and the elliptical mouth on the face — a faithful port
/// of `SliceCylinderOblique`'s piece, minus the ellipse cap face (the face's inner loop replaces it).
/// \p isEntry true = the stub runs from the cylinder base to the face; false = the face to the top.
[[nodiscard]] bool AddTiltedStub(Solid* s, const Solid& planar, int faceIdx, const Vec3& faceN,
                                 const CylinderShape& C, bool isEntry, Problem* outWhy) {
  const ucs::Ucs& fr = C.axis;
  const Vec3 Z = fr.zAxis;
  const double r = C.radius;
  const double h = C.length;  // used only as a fallback; the real bounds come from CylinderCutZExtent
  const Vec3 pn = faceN;
  const double dotNZ = ray3d::Dot(pn, Z);
  if (std::fabs(dotNZ) < 1e-6)
    return Fail(Problem::BooleanResultInvalid, outWhy);
  const Vec3 facePt =
      planar.vertices[static_cast<std::size_t>(
                          planar.edges[static_cast<std::size_t>(
                                           planar.faces[static_cast<std::size_t>(faceIdx)].loops[0].uses[0].edge)]
                              .v0)]
          .p;
  const Vec3 pl = ucs::WorldToUcs(fr, facePt);
  const Vec3 nl{ray3d::Dot(pn, fr.xAxis), ray3d::Dot(pn, fr.yAxis), dotNZ};
  const double a0 = (nl.x * pl.x + nl.y * pl.y + nl.z * pl.z) / nl.z;
  const double a1 = -r * nl.x / nl.z;
  const double a2 = -r * nl.y / nl.z;
  const double amp = std::sqrt(a1 * a1 + a2 * a2);
  const double capZ = isEntry ? 0.0 : h;
  // The elliptical mouth must lie strictly between the cap and infinity on the stub side.
  if (isEntry ? (a0 - amp <= 1e-9) : (a0 + amp >= h - 1e-9))
    return Fail(Problem::BooleanResultInvalid, outWhy);

  const Vec3 ec = ray3d::Add(fr.origin, ray3d::Scale(Z, a0));
  const Vec3 minorDir = ray3d::Normalize(ray3d::Cross(pn, Z));
  const Vec3 majorDir = ray3d::Normalize(ray3d::Cross(pn, minorDir));
  const double ea = r / std::fabs(dotNZ);
  const double eb = r;
  const Vec3 eN = dotNZ > 0.0 ? pn : ray3d::Scale(pn, -1.0);
  auto W = [&](double x, double y, double z) { return ucs::UcsToWorld(fr, Vec3{x, y, z}); };
  const double zp0 = a0 + a1;
  const double zpP = a0 - a1;
  const bool upper = !isEntry;  // "upper" (rim at z = h) mirrors SliceCylinderOblique's exit piece

  const int se0 = AddVertex(s, W(r, 0.0, zp0));
  const int se1 = AddVertex(s, W(-r, 0.0, zpP));
  const int rimZ0 = AddVertex(s, W(r, 0.0, capZ));
  const int rimZ1 = AddVertex(s, W(-r, 0.0, capZ));
  const Vec3 rimC = W(0.0, 0.0, capZ);
  const int rr0 = AddArc(s, rimZ0, rimZ1, rimC, Z, kPi);
  const int rr1 = AddArc(s, rimZ1, rimZ0, rimC, Z, kPi);
  const int el0 = AddEllipse(s, se0, se1, ec, eN, majorDir, ea, eb, kPi);
  const int el1 = AddEllipse(s, se1, se0, ec, eN, majorDir, ea, eb, kPi);
  const int sm0 = AddLine(s, upper ? se0 : rimZ0, upper ? rimZ0 : se0);
  const int sm1 = AddLine(s, upper ? se1 : rimZ1, upper ? rimZ1 : se1);

  if (upper)
    s->faces.push_back(MakePlaneFace(rimC, Z, {{rr0, false}, {rr1, false}}));
  else
    s->faces.push_back(MakePlaneFace(rimC, ray3d::Scale(Z, -1.0), {{rr1, true}, {rr0, true}}));

  auto side = [&](double u0, double u1, std::vector<EdgeUse> uses) {
    Face f;
    f.surface.kind = SurfaceKind::Cylinder;
    f.surface.frame = fr;
    f.surface.radius = r;
    f.surface.radius2 = r;
    f.surface.height = h;
    f.uStart = u0;
    f.uEnd = u1;
    Loop lp;
    lp.uses = std::move(uses);
    f.loops.push_back(std::move(lp));
    s->faces.push_back(std::move(f));
  };
  if (upper) {
    side(0.0, kPi, {{el0, false}, {sm1, false}, {rr0, true}, {sm0, true}});
    side(kPi, kTwoPi, {{el1, false}, {sm0, false}, {rr1, true}, {sm1, true}});
    s->faces[static_cast<std::size_t>(faceIdx)].loops.push_back(Loop{{{el1, true}, {el0, true}}});
  } else {
    side(0.0, kPi, {{rr0, false}, {sm1, false}, {el0, true}, {sm0, true}});
    side(kPi, kTwoPi, {{rr1, false}, {sm0, false}, {el1, true}, {sm1, true}});
    s->faces[static_cast<std::size_t>(faceIdx)].loops.push_back(Loop{{{el0, false}, {el1, false}}});
  }
  return Succeed(outWhy);
}

/// \p planar UNION a tilted cylinder that crosses faces \p fA / \p fB at params \p tA / \p tB: each
/// face bored with an ellipse, the cylinder stubs that stick out past each face added (REQ-314 B2b-1).
[[nodiscard]] bool BuildTiltedBoss(const Solid& planar, int fA, const Vec3& nA, double tA, int fB,
                                   const Vec3& nB, double tB, const CylinderShape& C,
                                   std::vector<Solid>* out, Problem* outWhy) {
  Solid s = planar;
  s.recipe = Recipe{};
  const double slack = 1e-7 * (C.radius + C.length);
  const bool aFirst = tA <= tB;
  const int f1 = aFirst ? fA : fB;
  const Vec3 n1 = aFirst ? nA : nB;
  const double t1 = aFirst ? tA : tB;
  const int f2 = aFirst ? fB : fA;
  const Vec3 n2 = aFirst ? nB : nA;
  const double t2 = aFirst ? tB : tA;
  int added = 0;
  if (t1 > slack) {  // a stub from the cylinder base up to face f1
    if (!AddTiltedStub(&s, planar, f1, n1, C, /*isEntry=*/true, outWhy))
      return false;
    ++added;
  }
  if (C.length - t2 > slack) {  // a stub from face f2 out to the cylinder top
    if (!AddTiltedStub(&s, planar, f2, n2, C, /*isEntry=*/false, outWhy))
      return false;
    ++added;
  }
  if (added == 0) {
    out->push_back(planar);
    return Succeed(outWhy);
  }
  for (int i = static_cast<int>(planar.faces.size()); i < static_cast<int>(s.faces.size()); ++i)
    s.shells[0].faces.push_back(i);
  if (Validate(s) != Problem::Ok || SelfIntersects(s))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  out->push_back(std::move(s));
  return Succeed(outWhy);
}
} // namespace

/// Recognise \p s as N>=1 coaxial cylindrical-or-conical segments sharing one axis line, contiguous
/// end to end (a stepped/shouldered/tapered stack, or — the N==1 case — a single plain cylinder or
/// cone): the general-purpose counterpart of REQ-337's `TryDecomposeCoaxialCylinderStack`
/// (`CadCommands.cpp`), which requires N>=2, cylindrical bands only, and a non-increasing radius
/// sequence (its own SUBTRACT-cutter scope). This one places no such restriction — 338c/338d-1's
/// UNION-folding use needs any contiguous coaxial run, monotonic or not, and N==1 so a bare
/// cylinder/cone and a stack can be merged through the same code path. Only Plane, Cylinder, and (as
/// of 338d-1) Cone faces are tolerated (a Sphere/Torus/Nurbs face anywhere still refuses); \p segs
/// comes back sorted by \p CoaxialSeg::z0, \p axisPoint/\p axisDir describe the shared line.
[[nodiscard]] bool ExtractCoaxialStack(const Solid& s, Vec3* axisPoint, Vec3* axisDir,
                                       std::vector<CoaxialSeg>* segs) {
  if (!axisPoint || !axisDir || !segs)
    return false;
  constexpr double kAxisAngTol = 1e-6;
  constexpr double kAxisDistTol = 1e-6;

  struct RawSpan { double z0, z1, r0, r1; };
  std::vector<RawSpan> raw;
  Vec3 dirAxis{}, ptAxis{};
  bool haveAxis = false;
  int planeCount = 0;

  for (const Face& f : s.faces) {
    if (f.surface.kind == SurfaceKind::Plane) {
      ++planeCount;
      continue;
    }
    if (f.surface.kind != SurfaceKind::Cylinder && f.surface.kind != SurfaceKind::Cone)
      return false;  // a sphere/torus/nurbs face — not this shape
    Vec3 dir = f.surface.frame.zAxis;
    const double dlen = ray3d::Length(dir);
    if (!(dlen > 1e-12))
      return false;
    dir = ray3d::Scale(dir, 1.0 / dlen);
    double d = 1.0;
    if (!haveAxis) {
      dirAxis = dir;
      ptAxis = f.surface.frame.origin;
      haveAxis = true;
    } else {
      d = ray3d::Dot(dir, dirAxis);
      if (std::fabs(d) < 1.0 - kAxisAngTol)
        return false;  // not parallel to the running axis
      const Vec3 toP = ray3d::Sub(f.surface.frame.origin, ptAxis);
      const double along = ray3d::Dot(toP, dirAxis);
      const Vec3 perp = ray3d::Sub(toP, ray3d::Scale(dirAxis, along));
      if (ray3d::Length(perp) > kAxisDistTol)
        return false;  // parallel but not the same line
    }
    // Cone: radius at the face's own local z=0 (frame.origin) vs. local z=height (ADR-045's
    // BuildConical convention — `radius`/`radius2`). Cylinder: both equal by construction.
    double r0 = f.surface.radius;
    double r1 = f.surface.radius2;
    if (!(r0 > 0.0) || r1 < 0.0 || !(f.surface.height > 0.0))
      return false;
    if (d < 0.0)  // this face's local +z runs opposite the running shared axis: its low/high ends flip
      std::swap(r0, r1);
    const double z0 = ray3d::Dot(ray3d::Sub(f.surface.frame.origin, ptAxis), dirAxis);
    raw.push_back({z0, z0 + f.surface.height, r0, r1});
  }
  if (!haveAxis || raw.empty() || planeCount < 2)
    return false;

  std::vector<RawSpan> uniqSpans;
  for (const RawSpan& r : raw) {
    bool merged = false;
    for (RawSpan& g : uniqSpans) {
      if (std::fabs(g.z0 - r.z0) < kAxisDistTol && std::fabs(g.z1 - r.z1) < kAxisDistTol &&
          std::fabs(g.r0 - r.r0) < kAxisDistTol && std::fabs(g.r1 - r.r1) < kAxisDistTol) {
        merged = true;
        break;
      }
    }
    if (!merged)
      uniqSpans.push_back(r);
  }

  std::sort(uniqSpans.begin(), uniqSpans.end(),
           [](const RawSpan& a, const RawSpan& b) { return a.z0 < b.z0; });
  for (size_t i = 0; i + 1 < uniqSpans.size(); ++i) {
    if (std::fabs(uniqSpans[i].z1 - uniqSpans[i + 1].z0) > kAxisDistTol)
      return false;  // a gap or an overlap — not one contiguous run
  }

  *axisPoint = ptAxis;
  *axisDir = dirAxis;
  segs->clear();
  segs->reserve(uniqSpans.size());
  for (const RawSpan& g : uniqSpans)
    segs->push_back({g.z0, g.z1, g.r0, g.r1});
  return true;
}

/// UNION of two coaxial stacks (issue #495 / REQ-338 338c) — the N-segment generalisation of
/// `TryBooleanCoaxialCylinders`'s own single-interval UNION merge below: every breakpoint from
/// either side's segment list becomes a candidate stack boundary, and each resulting band's radius
/// is the max of whichever side(s) cover it there (0 where neither does — a gap, kept disjoint by
/// returning both solids unchanged if the two runs don't overlap or touch at all). This is the
/// closed-form fix for "extend an existing coaxial stack with one more coaxial piece" — reuses
/// `BuildCoaxialStack` exactly as the two-operand case already does, never a general stitch of two
/// independently-built solids.
[[nodiscard]] bool TryBooleanCoaxialStackUnion(const Solid& a, const Solid& b, const Vec3& axisPoint,
                                               const Vec3& axisDir, const std::vector<CoaxialSeg>& segsA,
                                               const std::vector<CoaxialSeg>& segsBraw,
                                               std::vector<Solid>* out, Problem* outWhy) {
  double sc = 1.0;
  for (const CoaxialSeg& s : segsA)
    sc = std::max({sc, std::fabs(s.z0), std::fabs(s.z1), s.r0, s.r1});
  for (const CoaxialSeg& s : segsBraw)
    sc = std::max({sc, std::fabs(s.z0), std::fabs(s.z1), s.r0, s.r1});
  const double eps = 1e-7 * sc;

  // Both extraction runs are individually contiguous (ExtractCoaxialStack guarantees it); the two
  // combined can only be merged into ONE stack if their overall extents overlap or touch. Otherwise
  // they are two physically separate pieces on the same axis line — the ordinary disjoint-UNION
  // result, same convention as BooleanPlanar's own `if (!overlap) { push a; push b; }`.
  const double aLo = segsA.front().z0, aHi = segsA.back().z1;
  const double bLo = segsBraw.front().z0, bHi = segsBraw.back().z1;
  if (std::max(aLo, bLo) > std::min(aHi, bHi) + eps) {
    out->push_back(a);
    out->push_back(b);
    return Succeed(outWhy);
  }

  std::vector<double> brk;
  for (const CoaxialSeg& s : segsA) { brk.push_back(s.z0); brk.push_back(s.z1); }
  for (const CoaxialSeg& s : segsBraw) { brk.push_back(s.z0); brk.push_back(s.z1); }
  std::sort(brk.begin(), brk.end());
  std::vector<double> uniq;
  for (double v : brk)
    if (uniq.empty() || v - uniq.back() > eps)
      uniq.push_back(v);
  if (uniq.size() < 2)
    return Fail(Problem::BooleanResultInvalid, outWhy);

  // 338d-1: a covering segment's radius is no longer necessarily constant across it (a Cone band
  // tapers linearly from r0 at z0 to r1 at z1). `profileAt` returns the segment's own radius,
  // linearly interpolated, at each end of a candidate output band [z0,z1] — valid only when that one
  // segment covers the whole band (ExtractCoaxialStack's own per-operand contiguity means a band
  // never needs a THIRD segment from the same side).
  auto profileAt = [&](const std::vector<CoaxialSeg>& segs, double z0, double z1, double* rAt0,
                       double* rAt1) {
    for (const CoaxialSeg& s : segs) {
      if (z0 > s.z0 - eps && z1 < s.z1 + eps) {
        const double span = s.z1 - s.z0;
        auto interp = [&](double z) {
          return span > 1e-12 ? s.r0 + (z - s.z0) / span * (s.r1 - s.r0) : s.r0;
        };
        *rAt0 = interp(z0);
        *rAt1 = interp(z1);
        return true;
      }
    }
    return false;
  };

  std::vector<double> zs{uniq.front()};
  std::vector<double> rLo, rHi;
  for (size_t i = 0; i + 1 < uniq.size(); ++i) {
    const double z0 = uniq[i], z1 = uniq[i + 1];
    double aR0 = 0.0, aR1 = 0.0, bR0 = 0.0, bR1 = 0.0;
    const bool aHas = profileAt(segsA, z0, z1, &aR0, &aR1);
    const bool bHas = profileAt(segsBraw, z0, z1, &bR0, &bR1);
    double bandR0, bandR1;
    if (aHas && bHas) {
      // Both sides cover this band. Only combine when one side's radius profile dominates the
      // other's at BOTH ends (sufficient for two linear profiles never to cross in between) — the
      // ordinary "wider piece wins" UNION rule, generalised from a constant radius to a taper.
      // Two profiles that genuinely cross inside the band would need a real surface-pair
      // intersection curve to represent correctly (338d's later, unscoped general case) — refused
      // by name here rather than approximated.
      if (aR0 >= bR0 - eps && aR1 >= bR1 - eps) {
        bandR0 = aR0;
        bandR1 = aR1;
      } else if (bR0 >= aR0 - eps && bR1 >= aR1 - eps) {
        bandR0 = bR0;
        bandR1 = bR1;
      } else {
        return Fail(Problem::BooleanCurvedFace, outWhy);
      }
    } else if (aHas) {
      bandR0 = aR0;
      bandR1 = aR1;
    } else if (bHas) {
      bandR0 = bR0;
      bandR1 = bR1;
    } else {
      continue;  // a gap between the two runs
    }
    if (!(bandR0 > 0.0) && !(bandR1 > 0.0))
      continue;  // a gap between the two runs
    rLo.push_back(bandR0);
    rHi.push_back(bandR1);
    zs.push_back(z1);
  }
  if (rLo.empty())
    return Fail(Problem::BooleanResultInvalid, outWhy);
  if (rLo.size() == 1 && std::fabs(rLo.front() - rHi.front()) <= eps) {
    ucs::Ucs fr;
    if (!ucs::FromNormal(ray3d::Add(axisPoint, ray3d::Scale(axisDir, zs.front())), axisDir, &fr))
      return Fail(Problem::DegenerateFrame, outWhy);
    Solid r;
    if (!MakeCylinder(fr, rLo.front(), zs.back() - zs.front(), &r, outWhy))
      return false;
    out->push_back(std::move(r));
    return Succeed(outWhy);
  }
  ucs::Ucs fr;
  if (!ucs::FromNormal(ray3d::Add(axisPoint, ray3d::Scale(axisDir, zs.front())), axisDir, &fr))
    return Fail(Problem::DegenerateFrame, outWhy);
  Solid r;
  if (!BuildCoaxialStack(fr, zs, rLo, rHi, /*rIn=*/{}, &r, outWhy))
    return false;
  out->push_back(std::move(r));
  return Succeed(outWhy);
}

[[nodiscard]] bool TryBooleanCoaxialCylinders(const Solid& a, const Solid& b, const CylinderShape& A,
                                              const CylinderShape& B, BoolOp op, std::vector<Solid>* out,
                                              bool* handled, Problem* outWhy) {
  const Vec3 az = A.axis.zAxis;
  const Vec3 d = ray3d::Sub(B.axis.origin, A.axis.origin);
  const double sc = A.radius + A.length + B.length;
  const double perp = ray3d::Length(ray3d::Sub(d, ray3d::Scale(az, ray3d::Dot(d, az))));
  if (std::fabs(std::fabs(ray3d::Dot(az, B.axis.zAxis)) - 1.0) > 1e-7 || perp > 1e-6 * sc) {
    *handled = true;  // two cylinders we cannot combine analytically in B1
    return Fail(Problem::BooleanCurvedFace, outWhy);
  }
  *handled = true;
  const double eps = 1e-7 * sc;
  const double a0 = 0.0;
  const double a1 = A.length;
  double b0 = ray3d::Dot(d, az);
  double b1 = b0 + (ray3d::Dot(B.axis.zAxis, az) > 0.0 ? B.length : -B.length);
  if (b0 > b1)
    std::swap(b0, b1);

  if (op == BoolOp::Subtract) {
    // A − B, coaxial. Overlap along the axis:
    const double ov0 = std::max(a0, b0);
    const double ov1 = std::min(a1, b1);
    if (ov1 - ov0 <= eps) {  // no shared length — A is untouched
      out->push_back(a);
      return Succeed(outWhy);
    }
    auto oneCyl = [&](double zlo, double zhi, double rr, std::vector<Solid>* dst) {
      ucs::Ucs fr = A.axis;
      fr.origin = ray3d::Add(A.axis.origin, ray3d::Scale(az, zlo));
      Solid r;
      Problem w = Problem::Ok;
      if (!MakeCylinder(fr, rr, zhi - zlo, &r, &w))
        return false;
      dst->push_back(std::move(r));
      return true;
    };
    if (B.radius >= A.radius - eps) {
      // B is at least as wide as A: it removes a slab. A shrinks, splits, or vanishes.
      const bool keepLow = ov0 - a0 > eps;
      const bool keepHigh = a1 - ov1 > eps;
      if (!keepLow && !keepHigh)
        return Fail(Problem::BooleanEmptyResult, outWhy);
      if (keepLow && !oneCyl(a0, ov0, A.radius, out))
        return Fail(Problem::BooleanResultInvalid, outWhy);
      if (keepHigh && !oneCyl(ov1, a1, A.radius, out))
        return Fail(Problem::BooleanResultInvalid, outWhy);
      return Succeed(outWhy);
    }
    // B is narrower: a bore. It must open at an end of A, else it leaves a sealed cavity.
    const bool openLow = b0 <= a0 + eps;
    const bool openHigh = b1 >= a1 - eps;
    if (!openLow && !openHigh)
      return Fail(Problem::BooleanCurvedFace, outWhy);  // a floating internal cavity — its own slice
    std::vector<double> zs;
    std::vector<double> rOut;
    std::vector<double> rIn;
    zs.push_back(a0);
    if (ov0 - a0 > eps) {  // solid band below the bore
      rOut.push_back(A.radius);
      rIn.push_back(0.0);
      zs.push_back(ov0);
    }
    rOut.push_back(A.radius);  // the bored band
    rIn.push_back(B.radius);
    zs.push_back(ov1);
    if (a1 - ov1 > eps) {  // solid band above the bore
      rOut.push_back(A.radius);
      rIn.push_back(0.0);
      zs.push_back(a1);
    }
    ucs::Ucs fr = A.axis;
    fr.origin = ray3d::Add(A.axis.origin, ray3d::Scale(az, zs.front()));
    Solid r;
    if (!BuildCoaxialStack(fr, zs, rOut, rOut, rIn, &r, outWhy))
      return false;
    out->push_back(std::move(r));
    return Succeed(outWhy);
  }

  if (op == BoolOp::Intersect) {
    const double lo = std::max(a0, b0);
    const double hi = std::min(a1, b1);
    if (hi - lo <= eps)
      return Fail(Problem::BooleanEmptyResult, outWhy);
    ucs::Ucs fr = A.axis;
    fr.origin = ray3d::Add(A.axis.origin, ray3d::Scale(az, lo));
    Solid r;
    Problem w = Problem::Ok;
    if (!MakeCylinder(fr, std::min(A.radius, B.radius), hi - lo, &r, &w))
      return Fail(Problem::BooleanResultInvalid, outWhy);
    out->push_back(std::move(r));
    return Succeed(outWhy);
  }

  // UNION.
  if (std::max(a0, b0) > std::min(a1, b1) + eps) {
    out->push_back(a);
    out->push_back(b);
    return Succeed(outWhy);
  }
  const double lo = std::min(a0, b0);
  const double hi = std::max(a1, b1);
  auto oneCylinder = [&](double zlo, double zhi, double rr) {
    ucs::Ucs fr = A.axis;
    fr.origin = ray3d::Add(A.axis.origin, ray3d::Scale(az, zlo));
    Solid r;
    Problem w = Problem::Ok;
    if (!MakeCylinder(fr, rr, zhi - zlo, &r, &w))
      return Fail(Problem::BooleanResultInvalid, outWhy);
    out->push_back(std::move(r));
    return Succeed(outWhy);
  };
  if (std::fabs(A.radius - B.radius) <= 1e-7 * sc)
    return oneCylinder(lo, hi, A.radius);

  std::vector<double> brk{a0, a1, b0, b1};
  std::sort(brk.begin(), brk.end());
  std::vector<double> uniq;
  for (double v : brk)
    if (uniq.empty() || v - uniq.back() > eps)
      uniq.push_back(v);
  std::vector<double> zs{uniq.front()};
  std::vector<double> rs;
  for (std::size_t i = 0; i + 1 < uniq.size(); ++i) {
    const double m = 0.5 * (uniq[i] + uniq[i + 1]);
    double rr = 0.0;
    if (m > a0 - eps && m < a1 + eps)
      rr = std::max(rr, A.radius);
    if (m > b0 - eps && m < b1 + eps)
      rr = std::max(rr, B.radius);
    if (!(rr > 0.0))
      continue;
    if (!rs.empty() && std::fabs(rs.back() - rr) <= 1e-7 * sc)
      zs.back() = uniq[i + 1];
    else {
      rs.push_back(rr);
      zs.push_back(uniq[i + 1]);
    }
  }
  if (rs.size() == 1)
    return oneCylinder(zs.front(), zs.back(), rs.front());
  ucs::Ucs fr = A.axis;
  fr.origin = ray3d::Add(A.axis.origin, ray3d::Scale(az, zs.front()));
  Solid r;
  if (!BuildCoaxialStack(fr, zs, rs, rs, /*rIn=*/{}, &r, outWhy))
    return false;
  out->push_back(std::move(r));
  return Succeed(outWhy);
}

namespace {

/// Weld two solids at one exactly congruent, flat, coplanar shared face (REQ-339/ADR-057): \p faceA
/// in \p a and \p faceB in \p b are known — by construction, not discovered — to be the same disk or
/// annulus in world space, material on opposite sides. The weld removes both copies of that face and
/// merges everything else into one manifold solid, identifying every vertex/edge \p b shares with
/// \p a by world position/geometry (to \p a and \p b's own model scale) rather than recomputing any
/// geometry — no surface-pair intersection, no trim, nothing beyond topology. Scoped exactly to
/// REQ-339's own use: a bare flat coaxial-stack boundary, never a general "find where two arbitrary
/// solids touch" primitive — the caller supplies the two face indices, this never searches for them.
[[nodiscard]] bool WeldAtSharedFace(const Solid& a, int faceA, const Solid& b, int faceB, Solid* out,
                                    Problem* outWhy) {
  if (faceA < 0 || static_cast<std::size_t>(faceA) >= a.faces.size() || faceB < 0 ||
      static_cast<std::size_t>(faceB) >= b.faces.size())
    return Fail(Problem::BooleanResultInvalid, outWhy);
  const Face& fa = a.faces[static_cast<std::size_t>(faceA)];
  const Face& fb = b.faces[static_cast<std::size_t>(faceB)];
  if (fa.surface.kind != SurfaceKind::Plane || fb.surface.kind != SurfaceKind::Plane)
    return Fail(Problem::BooleanResultInvalid, outWhy);
  const double sc = std::max({1.0, ModelScale(a), ModelScale(b)});
  const double eps = 1e-6 * sc;
  if (ray3d::Length(ray3d::Sub(fa.surface.frame.origin, fb.surface.frame.origin)) > eps ||
      ray3d::Dot(fa.surface.frame.zAxis, fb.surface.frame.zAxis) > -1.0 + 1e-9)
    return Fail(Problem::BooleanResultInvalid, outWhy);  // not the same face, opposite-facing

  Solid s = a;
  std::vector<int> vmap(b.vertices.size(), -1);
  auto sameV = [&](const Vec3& p, const Vec3& q) { return ray3d::Length(ray3d::Sub(p, q)) <= eps; };
  const std::size_t aVertN = a.vertices.size();
  for (std::size_t i = 0; i < b.vertices.size(); ++i) {
    for (std::size_t j = 0; j < aVertN; ++j) {
      if (sameV(b.vertices[i].p, s.vertices[j].p)) {
        vmap[i] = static_cast<int>(j);
        break;
      }
    }
    if (vmap[i] < 0)
      vmap[i] = AddVertex(&s, b.vertices[i].p);
  }
  auto sameEdgeGeom = [&](const Edge& x, const Edge& y) {
    if (x.kind != y.kind)
      return false;
    if (x.kind == CurveKind::Arc || x.kind == CurveKind::Ellipse)
      return ray3d::Length(ray3d::Sub(x.frame.origin, y.frame.origin)) <= eps &&
             std::fabs(std::fabs(ray3d::Dot(x.frame.zAxis, y.frame.zAxis)) - 1.0) <= 1e-9 &&
             std::fabs(x.radius - y.radius) <= eps && std::fabs(x.radius2 - y.radius2) <= eps;
    return x.kind == CurveKind::Line;  // fully described by its (already-matched) endpoints
  };
  std::vector<int> emap(b.edges.size(), -1);
  std::vector<bool> eflip(b.edges.size(), false);
  const std::size_t aEdgeN = a.edges.size();
  for (std::size_t i = 0; i < b.edges.size(); ++i) {
    const Edge& be = b.edges[i];
    const int nv0 = vmap[static_cast<std::size_t>(be.v0)];
    const int nv1 = vmap[static_cast<std::size_t>(be.v1)];
    if (static_cast<std::size_t>(nv0) < aVertN && static_cast<std::size_t>(nv1) < aVertN) {
      for (std::size_t j = 0; j < aEdgeN; ++j) {
        const Edge& ae = s.edges[j];
        const bool fwd = ae.v0 == nv0 && ae.v1 == nv1;
        const bool rev = ae.v0 == nv1 && ae.v1 == nv0;
        if ((fwd || rev) && be.kind != CurveKind::Intersection && sameEdgeGeom(ae, be)) {
          emap[i] = static_cast<int>(j);
          eflip[i] = rev;
          break;
        }
      }
    }
    if (emap[i] < 0) {
      Edge ne = be;
      ne.v0 = nv0;
      ne.v1 = nv1;
      s.edges.push_back(std::move(ne));
      emap[i] = static_cast<int>(s.edges.size()) - 1;
    }
  }
  for (std::size_t i = 0; i < b.faces.size(); ++i) {
    if (static_cast<int>(i) == faceB)
      continue;
    Face nf = b.faces[i];
    for (Loop& lp : nf.loops)
      for (EdgeUse& u : lp.uses) {
        const std::size_t bi = static_cast<std::size_t>(u.edge);
        u.edge = emap[bi];
        if (eflip[bi])
          u.reversed = !u.reversed;
      }
    s.faces.push_back(std::move(nf));
  }
  s.faces.erase(s.faces.begin() + faceA);
  s.shells.clear();
  s.recipe = Recipe{};
  AddSingleShell(&s);
  if (Validate(s) != Problem::Ok || SelfIntersects(s))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

/// The radius of a circular cap's rim, read from either of its (assumed exactly two) boundary arcs.
/// Returns 0 if the face is not a plain circular cap in this shape.
[[nodiscard]] double CapRimRadius(const Solid& s, const Face& f) {
  if (f.loops.size() != 1)
    return 0.0;
  for (const EdgeUse& u : f.loops.front().uses) {
    const Edge& e = s.edges[static_cast<std::size_t>(u.edge)];
    if (e.kind == CurveKind::Arc)
      return e.radius;
  }
  return 0.0;
}
} // namespace

/// Join two coaxial solids at a pair of circular caps that may have DIFFERENT radii (a genuine
/// shoulder, not just a flat splice) — the general case `WeldAtSharedFace` does not cover, needed
/// because \p a and \p b are independently-built neighbours of a drilled piece and generally do not
/// share a radius at the join (issue #497 / REQ-339). \p a's cap is the LOWER one (the solid whose
/// material sits below the join, matching \ref BuildCoaxialStack's own `matBelow` convention for a
/// top cap); \p b's is the upper. Equal radii fall through to \ref WeldAtSharedFace unchanged (no
/// ring would have any width). Unequal radii drop both caps and connect the two solids' now-open rims
/// with one new annular ring face — reusing each cap's own two rim edges with their **own original
/// winding** verbatim: a solid's top cap and bottom cap are already wound so that whichever role
/// (inner or outer boundary of the new ring) a given radius turns out to play, the existing winding
/// is already the one the ring's own outward-normal convention (away from the wider side, exactly
/// `BuildCoaxialStack`'s `addRing`) needs — verified by cases, not asserted.
[[nodiscard]] bool WeldCoaxialCap(const Solid& a, int faceA, const Solid& b, int faceB, Solid* out,
                                  Problem* outWhy) {
  if (faceA < 0 || static_cast<std::size_t>(faceA) >= a.faces.size() || faceB < 0 ||
      static_cast<std::size_t>(faceB) >= b.faces.size())
    return Fail(Problem::BooleanResultInvalid, outWhy);
  const Face& fa = a.faces[static_cast<std::size_t>(faceA)];
  const Face& fb = b.faces[static_cast<std::size_t>(faceB)];
  const double ra = CapRimRadius(a, fa);
  const double rb = CapRimRadius(b, fb);
  const double sc = std::max({1.0, ModelScale(a), ModelScale(b)});
  if (!(ra > 0.0) || !(rb > 0.0))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  if (std::fabs(ra - rb) <= 1e-6 * sc)
    return WeldAtSharedFace(a, faceA, b, faceB, out, outWhy);

  Solid s = a;
  const int vOff = static_cast<int>(s.vertices.size());
  for (const Vertex& v : b.vertices)
    s.vertices.push_back(v);
  const int eOff = static_cast<int>(s.edges.size());
  for (const Edge& e : b.edges) {
    Edge ne = e;
    ne.v0 += vOff;
    ne.v1 += vOff;
    s.edges.push_back(ne);
  }
  Loop ringLoopA = fa.loops.front();  // copy before erasing fa below
  Loop ringLoopB = fb.loops.front();
  for (EdgeUse& u : ringLoopB.uses)
    u.edge += eOff;
  for (std::size_t i = 0; i < b.faces.size(); ++i) {
    if (static_cast<int>(i) == faceB)
      continue;
    Face nf = b.faces[i];
    for (Loop& lp : nf.loops)
      for (EdgeUse& u : lp.uses)
        u.edge += eOff;
    s.faces.push_back(std::move(nf));
  }
  s.faces.erase(s.faces.begin() + faceA);
  const Vec3 ringNormal = ra > rb ? fa.surface.frame.zAxis : ray3d::Scale(fa.surface.frame.zAxis, -1.0);
  Face ring;
  ring.surface = PlaneSurface(fa.surface.frame.origin, ringNormal);
  ring.loops = ra > rb ? std::vector<Loop>{std::move(ringLoopA), std::move(ringLoopB)}
                       : std::vector<Loop>{std::move(ringLoopB), std::move(ringLoopA)};
  s.faces.push_back(std::move(ring));
  s.shells.clear();
  s.recipe = Recipe{};
  AddSingleShell(&s);
  if (Validate(s) != Problem::Ok || SelfIntersects(s))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

namespace {


/// A right circular cylinder (base at \p axisFrame.origin, height \p h, radius \p r) with one planar
/// flat milled the full length parallel to the axis: the cut plane sits at local x = \p px
/// (`-r < px < r`), the material with x > px removed, the flat's outward normal along local +x. The
/// result is 4v / 6e / 4f, χ = 2 — two D-shaped end caps, one rectangular flat, one partial
/// cylindrical wall (u ∈ [φ, 2π−φ], φ = acos(px/r)). Used by `cylinder − box` (GitHub issue #242).
[[nodiscard]] bool BuildCylinderLongitudinalFlat(const ucs::Ucs& axisFrame, double r, double h,
                                                 double px, Solid* out, Problem* outWhy) {
  const double q = std::sqrt(std::max(0.0, r * r - px * px));
  const double phi = std::acos(std::clamp(px / r, -1.0, 1.0));
  const double sweep = kTwoPi - 2.0 * phi;
  if (q <= 1e-12 || sweep <= 1e-9 || sweep >= kTwoPi - 1e-9)
    return Fail(Problem::BooleanResultInvalid, outWhy);
  auto W = [&](double x, double y, double z) { return ucs::UcsToWorld(axisFrame, Vec3{x, y, z}); };
  Solid s;
  const int v0 = AddVertex(&s, W(px, -q, 0.0));
  const int v1 = AddVertex(&s, W(px, q, 0.0));
  const int v2 = AddVertex(&s, W(px, q, h));
  const int v3 = AddVertex(&s, W(px, -q, h));
  const Vec3 Z = axisFrame.zAxis;
  const Vec3 botC = W(0.0, 0.0, 0.0);
  const Vec3 topC = W(0.0, 0.0, h);
  const int eb = AddArc(&s, v1, v0, botC, Z, sweep);  // bottom major arc, u: φ → 2π−φ
  const int et = AddArc(&s, v2, v3, topC, Z, sweep);  // top major arc, same sense
  const int chordBot = AddLine(&s, v0, v1);
  const int chordTop = AddLine(&s, v2, v3);
  const int seamPos = AddLine(&s, v1, v2);  // u = φ seam, base → top
  const int seamNeg = AddLine(&s, v0, v3);  // u = 2π−φ seam, base → top

  s.faces.push_back(MakePlaneFace(botC, ray3d::Scale(Z, -1.0), {{chordBot, true}, {eb, true}}));
  s.faces.push_back(MakePlaneFace(topC, Z, {{chordTop, true}, {et, false}}));
  s.faces.push_back(MakePlaneFace(
      W(px, 0.0, 0.5 * h), axisFrame.xAxis,
      {{chordBot, false}, {seamPos, false}, {chordTop, false}, {seamNeg, true}}));
  {
    Face f;
    f.surface.kind = SurfaceKind::Cylinder;
    f.surface.frame = axisFrame;
    f.surface.radius = r;
    f.surface.radius2 = r;
    f.surface.height = h;
    f.uStart = phi;
    f.uEnd = kTwoPi - phi;
    Loop lp;
    lp.uses = {{seamNeg, false}, {et, true}, {seamPos, true}, {eb, false}};
    f.loops.push_back(std::move(lp));
    s.faces.push_back(std::move(f));
  }
  AddSingleShell(&s);
  if (Validate(s) != Problem::Ok || SelfIntersects(s))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

/// A right circular cylinder (base at \p axisFrame.origin, height \p h, radius \p r) with a
/// rectangular **pocket** milled into one side that does not reach either end: the flat at local
/// x = \p px (`-r < px < r`) removes material x > px only for z ∈ [\p za, \p zb]
/// (`0 < za < zb < h`). The pocket floor and ceiling are the circular segment `x > px` at each
/// level; the caps and the wall above/below the pocket are untouched. 8 vertices, 16 edges,
/// 10 faces, χ = 2. Arcs and lines only — closed form, no numeric integration (GitHub issue #242).
[[nodiscard]] bool BuildCylinderPocket(const ucs::Ucs& axisFrame, double r, double h, double px,
                                       double za, double zb, Solid* out, Problem* outWhy) {
  const double q = std::sqrt(std::max(0.0, r * r - px * px));
  const double phi = std::acos(std::clamp(px / r, -1.0, 1.0));
  const double minorSweep = 2.0 * phi;
  const double majorSweep = kTwoPi - 2.0 * phi;
  const double eps = 1e-9 * std::max(h, r);
  if (q <= 1e-12 || minorSweep <= 1e-9 || majorSweep <= 1e-9 || !(za > eps) || !(zb > za + eps) ||
      !(h - zb > eps))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  auto W = [&](double x, double y, double z) { return ucs::UcsToWorld(axisFrame, Vec3{x, y, z}); };
  const Vec3 Z = axisFrame.zAxis;

  Solid s;
  const int v0Lo = AddVertex(&s, W(px, -q, 0.0));
  const int v0Hi = AddVertex(&s, W(px, q, 0.0));
  const int vALo = AddVertex(&s, W(px, -q, za));
  const int vAHi = AddVertex(&s, W(px, q, za));
  const int vBLo = AddVertex(&s, W(px, -q, zb));
  const int vBHi = AddVertex(&s, W(px, q, zb));
  const int vLLo = AddVertex(&s, W(px, -q, h));
  const int vLHi = AddVertex(&s, W(px, q, h));

  // At each level, the "minor" arc runs the short way through u = 0 (the +x point, r,0,z) and the
  // "major" arc the long way through u = π (the −x point) — the same pair used everywhere the
  // kernel needs a full circle split to match a chord elsewhere (issue #242).
  auto level = [&](double z, int lo, int hi, int* mn, int* mj) {
    const Vec3 c = W(0.0, 0.0, z);
    *mn = AddArc(&s, lo, hi, c, Z, minorSweep);
    *mj = AddArc(&s, hi, lo, c, Z, majorSweep);
  };
  int mn0 = -1, mj0 = -1, mnA = -1, mjA = -1, mnB = -1, mjB = -1, mnL = -1, mjL = -1;
  level(0.0, v0Lo, v0Hi, &mn0, &mj0);
  level(za, vALo, vAHi, &mnA, &mjA);
  level(zb, vBLo, vBHi, &mnB, &mjB);
  level(h, vLLo, vLHi, &mnL, &mjL);

  const int sHi0A = AddLine(&s, v0Hi, vAHi);
  const int sLo0A = AddLine(&s, v0Lo, vALo);
  const int sHiAB = AddLine(&s, vAHi, vBHi);
  const int sLoAB = AddLine(&s, vALo, vBLo);
  const int sHiBL = AddLine(&s, vBHi, vLHi);
  const int sLoBL = AddLine(&s, vBLo, vLLo);
  const int chordA = AddLine(&s, vALo, vAHi);
  const int chordB = AddLine(&s, vBLo, vBHi);

  // Each partial band gets its OWN frame origin (shifted to its own local z = 0) and its own
  // `height` — `CylinderCutZExtent` only recognises a cut against an `Ellipse` edge, so a face
  // bounded by plain rim arcs falls back to `ConicalFaceIntegrals`, which trusts `sf.height` as
  // the face's actual span. Sharing the full-cylinder frame/height across every sub-band here
  // would integrate each one as if it ran the whole way from the base to the top.
  auto cyl = [&](double zBase, double zSpan, double u0, double u1, std::vector<EdgeUse> uses) {
    Face f;
    f.surface.kind = SurfaceKind::Cylinder;
    f.surface.frame = axisFrame;
    f.surface.frame.origin = W(0.0, 0.0, zBase);
    f.surface.radius = r;
    f.surface.radius2 = r;
    f.surface.height = zSpan;
    f.uStart = u0;
    f.uEnd = u1;
    f.loops.push_back(Loop{std::move(uses)});
    s.faces.push_back(std::move(f));
  };

  s.faces.push_back(MakePlaneFace(W(0.0, 0.0, 0.0), ray3d::Scale(Z, -1.0), {{mn0, true}, {mj0, true}}));
  s.faces.push_back(MakePlaneFace(W(0.0, 0.0, h), Z, {{mnL, false}, {mjL, false}}));
  cyl(0.0, za, -phi, phi, {{mn0, false}, {sHi0A, false}, {mnA, true}, {sLo0A, true}});
  cyl(0.0, za, phi, kTwoPi - phi, {{mj0, false}, {sLo0A, false}, {mjA, true}, {sHi0A, true}});
  cyl(zb, h - zb, -phi, phi, {{mnB, false}, {sHiBL, false}, {mnL, true}, {sLoBL, true}});
  cyl(zb, h - zb, phi, kTwoPi - phi, {{mjB, false}, {sLoBL, false}, {mjL, true}, {sHiBL, true}});
  cyl(za, zb - za, phi, kTwoPi - phi, {{mjA, false}, {sLoAB, false}, {mjB, true}, {sHiAB, true}});
  s.faces.push_back(MakePlaneFace(W(px, 0.0, za), Z, {{mnA, false}, {chordA, true}}));
  s.faces.push_back(
      MakePlaneFace(W(px, 0.0, zb), ray3d::Scale(Z, -1.0), {{mnB, true}, {chordB, false}}));
  s.faces.push_back(MakePlaneFace(
      W(px, 0.0, 0.5 * (za + zb)), axisFrame.xAxis,
      {{chordA, false}, {sHiAB, false}, {chordB, true}, {sLoAB, true}}));

  AddSingleShell(&s);
  if (Validate(s) != Problem::Ok || SelfIntersects(s))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}
} // namespace

[[nodiscard]] bool TryBooleanCylinderThroughPlanar(const Solid& planar, const Solid& cyl,
                                                   const CylinderShape& C, BoolOp op, bool cylIsMinuend,
                                                   std::vector<Solid>* out, bool* handled,
                                                   Problem* outWhy) {
  *handled = true;
  if (op == BoolOp::Subtract && cylIsMinuend) {
    // cylinder − box: the single-flat notch. Recognise exactly one box face parallel to the axis
    // that cuts within the radius, the box otherwise square to the axis and engulfing the removed
    // slab; then the subtraction is the cylinder minus that one half-space. A partial-length pocket,
    // a slot bounded by two faces, or a tilted box falls through to the refusal (GitHub issue #242).
    const Vec3 az2 = C.axis.zAxis;
    const double sc2 = std::max(ModelScale(planar), C.radius + C.length);
    const double e2 = 1e-7 * sc2;
    if (!AabbsOverlap(planar, cyl, e2)) {
      out->push_back(cyl);  // disjoint — the minuend is unchanged
      return Succeed(outWhy);
    }
    std::vector<int> cutFaces;
    bool boxSquareToAxis = true;
    for (int fi = 0; fi < static_cast<int>(planar.faces.size()); ++fi) {
      const Face& f = planar.faces[static_cast<std::size_t>(fi)];
      if (f.surface.kind != SurfaceKind::Plane)
        continue;
      const std::vector<Vec3> ring = FaceRing(planar, f);
      if (ring.size() < 3)
        continue;
      const Vec3 n = f.surface.frame.zAxis;
      const double dn = std::fabs(ray3d::Dot(n, az2));
      if (dn > 1e-6 && dn < 1.0 - 1e-6)
        boxSquareToAxis = false;  // a face neither parallel nor perpendicular to the axis
      if (dn > 1e-6)
        continue;  // not parallel to the axis — cannot be the lengthwise flat
      const double px = ray3d::Dot(ray3d::Sub(C.axis.origin, ring[0]), n);  // axis offset, +n out of box
      if (px > -(C.radius - e2) && px < C.radius - e2)
        cutFaces.push_back(fi);
    }
    const int cutCount = static_cast<int>(cutFaces.size());
    if (boxSquareToAxis && cutCount == 1) {
      const Face& f = planar.faces[static_cast<std::size_t>(cutFaces[0])];
      const std::vector<Vec3> ring = FaceRing(planar, f);
      const Vec3 n = f.surface.frame.zAxis;
      const double px = ray3d::Dot(ray3d::Sub(C.axis.origin, ring[0]), n);
      const double q = std::sqrt(std::max(0.0, C.radius * C.radius - px * px));
      // Local frame: base at the cylinder base, +x into the removed slab (−n), +z the axis.
      ucs::Ucs lf = C.axis;
      const Vec3 wx0 = ray3d::Scale(n, -1.0);
      lf.xAxis = ray3d::Normalize(ray3d::Sub(wx0, ray3d::Scale(lf.zAxis, ray3d::Dot(wx0, lf.zAxis))));
      lf.yAxis = ray3d::Normalize(ray3d::Cross(lf.zAxis, lf.xAxis));
      auto WL = [&](double x, double y, double z) { return ucs::UcsToWorld(lf, Vec3{x, y, z}); };
      auto insideEveryBoxFace = [&](const std::vector<Vec3>& probe) {
        for (const Face& bf : planar.faces) {
          if (bf.surface.kind != SurfaceKind::Plane)
            continue;
          const std::vector<Vec3> br = FaceRing(planar, bf);
          if (br.size() < 3)
            continue;
          const Vec3 bn = bf.surface.frame.zAxis;
          for (const Vec3& p : probe)
            if (ray3d::Dot(ray3d::Sub(p, br[0]), bn) > e2)
              return false;
        }
        return true;
      };
      // Every extreme point of the removed segment must lie inside the box, so that
      // cylinder − box == cylinder − (this one half-space).
      const std::vector<Vec3> fullProbe = {WL(px, -q, 0.0),       WL(px, q, 0.0),
                                           WL(px, q, C.length),   WL(px, -q, C.length),
                                           WL(C.radius, 0.0, 0.0), WL(C.radius, 0.0, C.length)};
      if (insideEveryBoxFace(fullProbe)) {
        Solid r;
        if (!BuildCylinderLongitudinalFlat(lf, C.radius, C.length, px, &r, outWhy))
          return false;
        out->push_back(std::move(r));
        return Succeed(outWhy);
      }
      // Not a full-span flat — try a partial-length pocket: the box's two axis-perpendicular faces,
      // both strictly inside the cylinder's length, bound a rectangular bite in the middle
      // (GitHub issue #242).
      std::vector<double> perpHeights;
      bool perpOk = true;
      for (const Face& bf : planar.faces) {
        if (bf.surface.kind != SurfaceKind::Plane)
          continue;
        const std::vector<Vec3> br = FaceRing(planar, bf);
        if (br.size() < 3)
          continue;
        const Vec3 bn = bf.surface.frame.zAxis;
        if (std::fabs(std::fabs(ray3d::Dot(bn, az2)) - 1.0) > 1e-6)
          continue;
        const double t = ray3d::Dot(ray3d::Sub(br[0], C.axis.origin), az2);
        if (t <= e2 || t >= C.length - e2) {
          perpOk = false;  // a perpendicular face at or beyond a cap — not this shape
          continue;
        }
        perpHeights.push_back(t);
      }
      if (perpOk && perpHeights.size() == 2) {
        const double za = std::min(perpHeights[0], perpHeights[1]);
        const double zb = std::max(perpHeights[0], perpHeights[1]);
        const std::vector<Vec3> pocketProbe = {WL(px, -q, za),          WL(px, q, za),
                                               WL(px, -q, zb),          WL(px, q, zb),
                                               WL(C.radius, 0.0, za),   WL(C.radius, 0.0, zb)};
        if (insideEveryBoxFace(pocketProbe)) {
          Solid r;
          if (!BuildCylinderPocket(lf, C.radius, C.length, px, za, zb, &r, outWhy))
            return false;
          out->push_back(std::move(r));
          return Succeed(outWhy);
        }
      }
    }
    if (boxSquareToAxis && cutCount == 2) {
      // A slot: two parallel cutting faces bound a full-length slab between them, leaving two
      // disjoint "wing" pieces (GitHub issue #242). Each wing has exactly the single-flat notch's
      // shape, so it reuses BuildCylinderLongitudinalFlat; the second wing is built in a frame with
      // its +x mirrored (still right-handed) so "kept x ≤ px" reads as the far side of the slot.
      const Face& f0 = planar.faces[static_cast<std::size_t>(cutFaces[0])];
      const std::vector<Vec3> ring0 = FaceRing(planar, f0);
      const Vec3 n0 = f0.surface.frame.zAxis;
      const double px0 = ray3d::Dot(ray3d::Sub(C.axis.origin, ring0[0]), n0);
      ucs::Ucs lf0 = C.axis;
      const Vec3 wx00 = ray3d::Scale(n0, -1.0);
      lf0.xAxis =
          ray3d::Normalize(ray3d::Sub(wx00, ray3d::Scale(lf0.zAxis, ray3d::Dot(wx00, lf0.zAxis))));
      lf0.yAxis = ray3d::Normalize(ray3d::Cross(lf0.zAxis, lf0.xAxis));
      // The other face's threshold, expressed in lf0's coordinates. Its own local frame has
      // xAxis' = -n1 = n0 = -lf0.xAxis (n1 is antiparallel to n0 for a box's two opposing faces), so
      // its "kept x' <= px1" becomes "lf0-x >= -px1" — the far wing is kept where lf0-x >= qx1Signed.
      const Face& f1 = planar.faces[static_cast<std::size_t>(cutFaces[1])];
      const std::vector<Vec3> ring1 = FaceRing(planar, f1);
      const Vec3 n1 = f1.surface.frame.zAxis;
      if (ray3d::Dot(n0, n1) > -1.0 + 1e-6)
        return Fail(Problem::BooleanCurvedFace, outWhy);  // not a simple opposing-face slot
      const double px1 = ray3d::Dot(ray3d::Sub(C.axis.origin, ring1[0]), n1);
      const double qx1Signed = -px1;
      if (px0 < qx1Signed - e2) {
        auto WL0 = [&](double x, double y, double z) { return ucs::UcsToWorld(lf0, Vec3{x, y, z}); };
        const double q0 = std::sqrt(std::max(0.0, C.radius * C.radius - px0 * px0));
        const double q1 = std::sqrt(std::max(0.0, C.radius * C.radius - qx1Signed * qx1Signed));
        auto insideEveryBoxFace = [&](const std::vector<Vec3>& probe) {
          for (const Face& bf : planar.faces) {
            if (bf.surface.kind != SurfaceKind::Plane)
              continue;
            const std::vector<Vec3> br = FaceRing(planar, bf);
            if (br.size() < 3)
              continue;
            const Vec3 bn = bf.surface.frame.zAxis;
            for (const Vec3& p : probe)
              if (ray3d::Dot(ray3d::Sub(p, br[0]), bn) > e2)
                return false;
          }
          return true;
        };
        const std::vector<Vec3> slotProbe = {
            WL0(px0, -q0, 0.0),       WL0(px0, q0, 0.0),       WL0(px0, -q0, C.length),
            WL0(px0, q0, C.length),   WL0(qx1Signed, -q1, 0.0), WL0(qx1Signed, q1, 0.0),
            WL0(qx1Signed, -q1, C.length), WL0(qx1Signed, q1, C.length)};
        if (insideEveryBoxFace(slotProbe)) {
          ucs::Ucs mf = lf0;
          mf.xAxis = ray3d::Scale(lf0.xAxis, -1.0);
          mf.yAxis = ray3d::Scale(lf0.yAxis, -1.0);
          Solid wingLo;
          Solid wingHi;
          if (!BuildCylinderLongitudinalFlat(lf0, C.radius, C.length, px0, &wingLo, outWhy) ||
              !BuildCylinderLongitudinalFlat(mf, C.radius, C.length, -qx1Signed, &wingHi, outWhy))
            return false;
          out->push_back(std::move(wingLo));
          out->push_back(std::move(wingHi));
          return Succeed(outWhy);
        }
      }
    }
    if (boxSquareToAxis && cutCount == 0 && PointInPlanarSolid(C.axis.origin, planar, sc2) &&
        PointInPlanarSolid(ray3d::Add(C.axis.origin, ray3d::Scale(az2, C.length)), planar, sc2))
      return Fail(Problem::BooleanEmptyResult, outWhy);  // box swallows the whole cylinder
    return Fail(Problem::BooleanCurvedFace, outWhy);  // cylinder − box: other cases, their own slice
  }
  const bool bore = op == BoolOp::Subtract;  // box − cylinder: drill a hole (B2a)
  const Vec3 az = C.axis.zAxis;
  const double scale = std::max(ModelScale(planar), C.radius + C.length);
  const double eps = 1e-7 * scale;

  struct Hit {
    int face = -1;
    double t = 0.0;
    Vec3 point;
  };
  Hit entry;
  Hit exitH;
  bool anyPerp = false;
  for (int fi = 0; fi < static_cast<int>(planar.faces.size()); ++fi) {
    const Face& f = planar.faces[static_cast<std::size_t>(fi)];
    if (f.surface.kind != SurfaceKind::Plane)
      continue;
    const Vec3 n = f.surface.frame.zAxis;
    const double dn = ray3d::Dot(n, az);
    if (std::fabs(std::fabs(dn) - 1.0) > 1e-6)
      continue;
    const std::vector<Vec3> ring = FaceRing(planar, f);
    // Disk cap (cylinder top/bottom) has 2 arc edges, not a polygon. Handle separately.
    bool isDisk = false;
    Vec3 diskCentre{};
    double diskRadius = 0;
    if (ring.size() < 3) {
      if (f.loops.size() == 1 && f.loops[0].uses.size() == 2) {
        const Edge& e0 = planar.edges[static_cast<size_t>(f.loops[0].uses[0].edge)];
        const Edge& e1 = planar.edges[static_cast<size_t>(f.loops[0].uses[1].edge)];
        if (e0.kind == CurveKind::Arc && e1.kind == CurveKind::Arc) {
          if (std::fabs(e0.radius - e1.radius) < 1e-9 * std::max(e0.radius, 1.0) &&
              ray3d::Length(ray3d::Sub(e0.frame.origin, e1.frame.origin)) < 1e-9 * std::max(e0.radius, 1.0)) {
            isDisk = true;
            diskCentre = e0.frame.origin;
            diskRadius = e0.radius;
          }
        }
      }
      if (!isDisk) continue;
    }
    const double t = isDisk ? ray3d::Dot(ray3d::Sub(diskCentre, C.axis.origin), n) / dn : ray3d::Dot(ray3d::Sub(ring[0], C.axis.origin), n) / dn;
    const Vec3 hp = ray3d::Add(C.axis.origin, ray3d::Scale(az, t));
    bool onEdge = false;
    if (isDisk) {
      double d = ray3d::Length(ray3d::Sub(hp, diskCentre));
      if (d > diskRadius + eps) continue;
      onEdge = std::fabs(d - diskRadius) <= eps;
    } else {
      if (!PointInPolygon3D(hp, ring, n, eps, &onEdge))
        continue;
    }
    anyPerp = true;
    double clr = std::numeric_limits<double>::max();
    if (isDisk) {
      double d = ray3d::Length(ray3d::Sub(hp, diskCentre));
      clr = diskRadius - d;
    } else {
      for (std::size_t i = 0; i < ring.size(); ++i) {
        const Vec3& p0 = ring[i];
        const Vec3& p1 = ring[(i + 1) % ring.size()];
        const Vec3 e = ray3d::Sub(p1, p0);
        const double len2 = ray3d::Dot(e, e);
        double u = len2 > 1e-24 ? ray3d::Dot(ray3d::Sub(hp, p0), e) / len2 : 0.0;
        u = std::clamp(u, 0.0, 1.0);
        clr = std::min(clr, ray3d::Length(ray3d::Sub(hp, ray3d::Add(p0, ray3d::Scale(e, u)))));
      }
    }
    if (clr < C.radius + eps)
      continue;  // the footprint crosses this face's edge — a mixed arc/line intersection (B2)
    Hit& slot = dn < 0.0 ? entry : exitH;
    if (slot.face < 0)
      slot = Hit{fi, t, hp};
  }

  const bool havePair = entry.face >= 0 && exitH.face >= 0 && exitH.t > entry.t + eps;
  if (havePair) {
    const double lo = std::max(entry.t, 0.0);
    const double hi = std::min(exitH.t, C.length);
    if (hi - lo <= eps) {  // the cylinder does not actually reach the solid
      if (op == BoolOp::Intersect)
        return Fail(Problem::BooleanEmptyResult, outWhy);
      out->push_back(planar);
      out->push_back(cyl);
      return Succeed(outWhy);
    }
    if (op == BoolOp::Intersect) {
      ucs::Ucs fr = C.axis;
      fr.origin = ray3d::Add(C.axis.origin, ray3d::Scale(az, lo));
      Solid r;
      Problem w = Problem::Ok;
      if (!MakeCylinder(fr, C.radius, hi - lo, &r, &w))
        return Fail(Problem::BooleanResultInvalid, outWhy);
      out->push_back(std::move(r));
      return Succeed(outWhy);
    }
    if (bore) {
      if (entry.t < -eps || entry.t > C.length + eps)
        return Fail(Problem::BooleanCurvedFace, outWhy);  // cylinder base inside — a floating pocket, defer
      const Vec3 nE = planar.faces[static_cast<std::size_t>(entry.face)].surface.frame.zAxis;
      const Vec3 entryC = entry.point;
      const bool through = exitH.t <= C.length + eps;
      Solid r;
      if (through) {
        if (!BuildBore(planar, entry.face, entryC, nE, C.radius, /*through=*/true, exitH.face,
                       exitH.point, 0.0, &r, outWhy))
          return false;
      } else {
        const double depth = C.length - std::max(entry.t, 0.0);
        if (depth <= eps)
          return Fail(Problem::BooleanCurvedFace, outWhy);
        if (!BuildBore(planar, entry.face, entryC, nE, C.radius, /*through=*/false, -1, Vec3{}, depth,
                       &r, outWhy))
          return false;
      }
      out->push_back(std::move(r));
      return Succeed(outWhy);
    }
    std::vector<BossStub> stubs;
    if (entry.t > eps)
      stubs.push_back(BossStub{entry.face, entry.point,
                               planar.faces[static_cast<std::size_t>(entry.face)].surface.frame.zAxis,
                               entry.t});
    if (C.length - exitH.t > eps)
      stubs.push_back(BossStub{exitH.face, exitH.point,
                               planar.faces[static_cast<std::size_t>(exitH.face)].surface.frame.zAxis,
                               C.length - exitH.t});
    if (stubs.empty()) {
      out->push_back(planar);
      return Succeed(outWhy);
    }
    Solid r;
    if (!BuildBoss(planar, stubs, C.radius, &r, outWhy))
      return false;
    out->push_back(std::move(r));
    return Succeed(outWhy);
  }

  if (!AabbsOverlap(planar, cyl, eps)) {
    if (op == BoolOp::Intersect)
      return Fail(Problem::BooleanEmptyResult, outWhy);
    out->push_back(planar);
    out->push_back(cyl);
    return Succeed(outWhy);
  }

  // A TILTED cylinder (B2b-1): the axis crosses two planar faces at an angle, cutting an ellipse on
  // each. INTERSECT -> a plug with two oblique elliptical ends; SUBTRACT -> an elliptical-mouthed bore.
  {
    struct EHit {
      int face = -1;
      double t = 0.0;
      Vec3 n;
      Vec3 pt;
    };
    std::vector<EHit> hits;
    bool messy = false;
    for (int fi = 0; fi < static_cast<int>(planar.faces.size()); ++fi) {
      const Face& f = planar.faces[static_cast<std::size_t>(fi)];
      if (f.surface.kind != SurfaceKind::Plane)
        continue;
      const Vec3 n = f.surface.frame.zAxis;
      const double dn = ray3d::Dot(n, az);
      if (std::fabs(dn) < 1e-6)
        continue;  // axis parallel to the face — no crossing
      const std::vector<Vec3> ring = FaceRing(planar, f);
      if (ring.size() < 3)
        continue;
      const double t = ray3d::Dot(ray3d::Sub(ring[0], C.axis.origin), n) / dn;
      const Vec3 hp = ray3d::Add(C.axis.origin, ray3d::Scale(az, t));
      bool onEdge = false;
      if (!PointInPolygon3D(hp, ring, n, eps, &onEdge))
        continue;
      double clr = std::numeric_limits<double>::max();
      for (std::size_t i = 0; i < ring.size(); ++i) {
        const Vec3& p0 = ring[i];
        const Vec3& p1 = ring[(i + 1) % ring.size()];
        const Vec3 e = ray3d::Sub(p1, p0);
        const double l2 = ray3d::Dot(e, e);
        double u = l2 > 1e-24 ? ray3d::Dot(ray3d::Sub(hp, p0), e) / l2 : 0.0;
        u = std::clamp(u, 0.0, 1.0);
        clr = std::min(clr, ray3d::Length(ray3d::Sub(hp, ray3d::Add(p0, ray3d::Scale(e, u)))));
      }
      if (clr < C.radius / std::fabs(dn) + eps) {  // the elliptical footprint runs over a face edge
        messy = true;
        continue;
      }
      hits.push_back(EHit{fi, t, n, hp});
    }
    if (!messy && hits.size() == 2) {
      double t0 = hits[0].t;
      double t1 = hits[1].t;
      if (t0 > t1) {
        std::swap(t0, t1);
        std::swap(hits[0], hits[1]);
      }
      if (t1 - t0 <= eps || t0 < -eps || t1 > C.length + eps)
        return Fail(Problem::BooleanCurvedFace, outWhy);  // partial penetration — a later slice
      Solid r;
      if (op == BoolOp::Intersect) {
        if (!BuildObliqueCylinderPlug(C.axis, C.radius, hits[0].pt, hits[0].n, hits[1].pt, hits[1].n,
                                      &r, outWhy))
          return false;
      } else if (op == BoolOp::Subtract && !cylIsMinuend) {
        if (!BuildTiltedBore(planar, hits[0].face, hits[0].n, hits[1].face, hits[1].n, C.axis,
                             C.radius, &r, outWhy))
          return false;
      } else if (op == BoolOp::Union) {
        return BuildTiltedBoss(planar, hits[0].face, hits[0].n, t0, hits[1].face, hits[1].n, t1, C,
                               out, outWhy)
                   ? Succeed(outWhy)
                   : false;
      } else {
        return Fail(Problem::BooleanObliqueCylinder, outWhy);  // cyl − box tilted — later
      }
      out->push_back(std::move(r));
      return Succeed(outWhy);
    }
  }

  if (!anyPerp)
    return Fail(Problem::BooleanObliqueCylinder, outWhy);
  return Fail(Problem::BooleanCurvedFace, outWhy);  // partial penetration / a footprint over an edge
}

} // namespace brep
