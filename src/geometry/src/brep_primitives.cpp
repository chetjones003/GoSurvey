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

// ---------------------------------------------------------------------------------------------
// The seven primitives.
// ---------------------------------------------------------------------------------------------

bool MakeBox(const ucs::Ucs& frame, double length, double width, double height, Solid* out, Problem* outWhy) {
  if (!out)
    return false;  // a null output is a caller bug, not a user-facing reason: outWhy is left alone
  if (!AllFinite({length, width, height}))
    return Fail(Problem::NonFiniteParameter, outWhy);
  if (!(length > 0.0))
    return Fail(Problem::NonPositiveLength, outWhy);
  if (!(width > 0.0))
    return Fail(Problem::NonPositiveWidth, outWhy);
  if (!(height > 0.0))
    return Fail(Problem::NonPositiveHeight, outWhy);
  if (!FrameOk(frame))
    return Fail(Problem::DegenerateFrame, outWhy);

  const double hx = length * 0.5;
  const double hy = width * 0.5;
  const double h = height;

  Solid s;
  const int v0 = AddVertex(&s, Vec3{-hx, -hy, 0.0});
  const int v1 = AddVertex(&s, Vec3{hx, -hy, 0.0});
  const int v2 = AddVertex(&s, Vec3{hx, hy, 0.0});
  const int v3 = AddVertex(&s, Vec3{-hx, hy, 0.0});
  const int v4 = AddVertex(&s, Vec3{-hx, -hy, h});
  const int v5 = AddVertex(&s, Vec3{hx, -hy, h});
  const int v6 = AddVertex(&s, Vec3{hx, hy, h});
  const int v7 = AddVertex(&s, Vec3{-hx, hy, h});

  const int b0 = AddLine(&s, v0, v1);
  const int b1 = AddLine(&s, v1, v2);
  const int b2 = AddLine(&s, v2, v3);
  const int b3 = AddLine(&s, v3, v0);
  const int t0 = AddLine(&s, v4, v5);
  const int t1 = AddLine(&s, v5, v6);
  const int t2 = AddLine(&s, v6, v7);
  const int t3 = AddLine(&s, v7, v4);
  const int p0 = AddLine(&s, v0, v4);
  const int p1 = AddLine(&s, v1, v5);
  const int p2 = AddLine(&s, v2, v6);
  const int p3 = AddLine(&s, v3, v7);

  s.faces.push_back(MakePlaneFace(Vec3{0.0, 0.0, 0.0}, Vec3{0.0, 0.0, -1.0},
                                  {{b3, true}, {b2, true}, {b1, true}, {b0, true}}));
  s.faces.push_back(
      MakePlaneFace(Vec3{0.0, 0.0, h}, Vec3{0.0, 0.0, 1.0}, {{t0, false}, {t1, false}, {t2, false}, {t3, false}}));
  s.faces.push_back(MakePlaneFace(Vec3{0.0, -hy, 0.0}, Vec3{0.0, -1.0, 0.0},
                                  {{b0, false}, {p1, false}, {t0, true}, {p0, true}}));
  s.faces.push_back(MakePlaneFace(Vec3{hx, 0.0, 0.0}, Vec3{1.0, 0.0, 0.0},
                                  {{b1, false}, {p2, false}, {t1, true}, {p1, true}}));
  s.faces.push_back(MakePlaneFace(Vec3{0.0, hy, 0.0}, Vec3{0.0, 1.0, 0.0},
                                  {{b2, false}, {p3, false}, {t2, true}, {p2, true}}));
  s.faces.push_back(MakePlaneFace(Vec3{-hx, 0.0, 0.0}, Vec3{-1.0, 0.0, 0.0},
                                  {{b3, false}, {p0, false}, {t3, true}, {p3, true}}));

  AddSingleShell(&s);
  s.recipe.kind = PrimitiveKind::Box;
  s.recipe.length = length;
  s.recipe.width = width;
  s.recipe.height = height;
  PlaceInFrame(&s, frame);

  const Problem why = Validate(s);
  if (why != Problem::Ok)
    return Fail(why, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

bool MakeWedge(const ucs::Ucs& frame, double length, double width, double height, Solid* out,
               Problem* outWhy) {
  if (!out)
    return false;  // a null output is a caller bug, not a user-facing reason: outWhy is left alone
  if (!AllFinite({length, width, height}))
    return Fail(Problem::NonFiniteParameter, outWhy);
  if (!(length > 0.0))
    return Fail(Problem::NonPositiveLength, outWhy);
  if (!(width > 0.0))
    return Fail(Problem::NonPositiveWidth, outWhy);
  if (!(height > 0.0))
    return Fail(Problem::NonPositiveHeight, outWhy);
  if (!FrameOk(frame))
    return Fail(Problem::DegenerateFrame, outWhy);

  const double hx = length * 0.5;
  const double hy = width * 0.5;
  const double h = height;

  Solid s;
  const int v0 = AddVertex(&s, Vec3{-hx, -hy, 0.0});
  const int v1 = AddVertex(&s, Vec3{hx, -hy, 0.0});
  const int v2 = AddVertex(&s, Vec3{hx, hy, 0.0});
  const int v3 = AddVertex(&s, Vec3{-hx, hy, 0.0});
  const int v4 = AddVertex(&s, Vec3{-hx, -hy, h});
  const int v5 = AddVertex(&s, Vec3{-hx, hy, h});

  const int b0 = AddLine(&s, v0, v1);
  const int b1 = AddLine(&s, v1, v2);
  const int b2 = AddLine(&s, v2, v3);
  const int b3 = AddLine(&s, v3, v0);
  const int ridge = AddLine(&s, v4, v5);
  const int u0 = AddLine(&s, v0, v4);  // vertical, y = -hy
  const int u1 = AddLine(&s, v3, v5);  // vertical, y = +hy
  const int g0 = AddLine(&s, v1, v4);  // slant, y = -hy
  const int g1 = AddLine(&s, v2, v5);  // slant, y = +hy

  s.faces.push_back(MakePlaneFace(Vec3{0.0, 0.0, 0.0}, Vec3{0.0, 0.0, -1.0},
                                  {{b3, true}, {b2, true}, {b1, true}, {b0, true}}));
  // The sloping face. Its outward normal leans out and up, away from the ridge.
  {
    const Vec3 n = ray3d::Normalize(Vec3{h, 0.0, 2.0 * hx});
    s.faces.push_back(
        MakePlaneFace(Vec3{hx, 0.0, 0.0}, n, {{b1, false}, {g1, false}, {ridge, true}, {g0, true}}));
  }
  s.faces.push_back(MakePlaneFace(Vec3{-hx, 0.0, 0.0}, Vec3{-1.0, 0.0, 0.0},
                                  {{b3, false}, {u0, false}, {ridge, false}, {u1, true}}));
  s.faces.push_back(
      MakePlaneFace(Vec3{0.0, -hy, 0.0}, Vec3{0.0, -1.0, 0.0}, {{b0, false}, {g0, false}, {u0, true}}));
  s.faces.push_back(
      MakePlaneFace(Vec3{0.0, hy, 0.0}, Vec3{0.0, 1.0, 0.0}, {{b2, false}, {u1, false}, {g1, true}}));

  AddSingleShell(&s);
  s.recipe.kind = PrimitiveKind::Wedge;
  s.recipe.length = length;
  s.recipe.width = width;
  s.recipe.height = height;
  PlaceInFrame(&s, frame);

  const Problem why = Validate(s);
  if (why != Problem::Ok)
    return Fail(why, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

bool MakePyramid(const ucs::Ucs& frame, int sides, double baseRadius, double topRadius, double height,
                 Solid* out, Problem* outWhy) {
  if (!out)
    return false;  // a null output is a caller bug, not a user-facing reason: outWhy is left alone
  if (!AllFinite({baseRadius, topRadius, height}))
    return Fail(Problem::NonFiniteParameter, outWhy);
  if (sides < 3 || sides > kMaxPyramidSides)
    return Fail(Problem::SideCountOutOfRange, outWhy);
  if (!(baseRadius > 0.0))
    return Fail(Problem::NonPositiveRadius, outWhy);
  if (topRadius < 0.0)
    return Fail(Problem::NegativeTopRadius, outWhy);
  if (topRadius >= baseRadius)
    return Fail(Problem::TopRadiusNotBelowBase, outWhy);
  if (!(height > 0.0))
    return Fail(Problem::NonPositiveHeight, outWhy);
  if (!FrameOk(frame))
    return Fail(Problem::DegenerateFrame, outWhy);

  const bool apex = !(topRadius > 0.0);
  const int n = sides;

  Solid s;
  std::vector<int> base(static_cast<std::size_t>(n));
  for (int i = 0; i < n; ++i) {
    const double a = kTwoPi * static_cast<double>(i) / static_cast<double>(n);
    base[static_cast<std::size_t>(i)] =
        AddVertex(&s, Vec3{baseRadius * std::cos(a), baseRadius * std::sin(a), 0.0});
  }
  std::vector<int> top;
  int apexV = -1;
  if (apex) {
    apexV = AddVertex(&s, Vec3{0.0, 0.0, height});
  } else {
    top.resize(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
      const double a = kTwoPi * static_cast<double>(i) / static_cast<double>(n);
      top[static_cast<std::size_t>(i)] =
          AddVertex(&s, Vec3{topRadius * std::cos(a), topRadius * std::sin(a), height});
    }
  }

  std::vector<int> baseEdge(static_cast<std::size_t>(n));
  for (int i = 0; i < n; ++i)
    baseEdge[static_cast<std::size_t>(i)] =
        AddLine(&s, base[static_cast<std::size_t>(i)], base[static_cast<std::size_t>((i + 1) % n)]);

  std::vector<int> topEdge;
  std::vector<int> sideEdge(static_cast<std::size_t>(n));
  if (apex) {
    for (int i = 0; i < n; ++i)
      sideEdge[static_cast<std::size_t>(i)] = AddLine(&s, base[static_cast<std::size_t>(i)], apexV);
  } else {
    topEdge.resize(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i)
      topEdge[static_cast<std::size_t>(i)] =
          AddLine(&s, top[static_cast<std::size_t>(i)], top[static_cast<std::size_t>((i + 1) % n)]);
    for (int i = 0; i < n; ++i)
      sideEdge[static_cast<std::size_t>(i)] =
          AddLine(&s, base[static_cast<std::size_t>(i)], top[static_cast<std::size_t>(i)]);
  }

  // Base, wound clockwise as seen from +Z so that it is counter-clockwise about its own -Z normal.
  {
    std::vector<EdgeUse> uses;
    uses.reserve(static_cast<std::size_t>(n));
    for (int i = n - 1; i >= 0; --i)
      uses.push_back(EdgeUse{baseEdge[static_cast<std::size_t>(i)], true});
    s.faces.push_back(MakePlaneFace(Vec3{0.0, 0.0, 0.0}, Vec3{0.0, 0.0, -1.0}, std::move(uses)));
  }

  if (!apex) {
    std::vector<EdgeUse> uses;
    uses.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i)
      uses.push_back(EdgeUse{topEdge[static_cast<std::size_t>(i)], false});
    s.faces.push_back(MakePlaneFace(Vec3{0.0, 0.0, height}, Vec3{0.0, 0.0, 1.0}, std::move(uses)));
  }

  for (int i = 0; i < n; ++i) {
    const int j = (i + 1) % n;
    const Vec3& a = s.vertices[static_cast<std::size_t>(base[static_cast<std::size_t>(i)])].p;
    const Vec3& b = s.vertices[static_cast<std::size_t>(base[static_cast<std::size_t>(j)])].p;
    const Vec3& c = apex ? s.vertices[static_cast<std::size_t>(apexV)].p
                         : s.vertices[static_cast<std::size_t>(top[static_cast<std::size_t>(j)])].p;
    const Vec3 nrm = ray3d::Normalize(ray3d::Cross(ray3d::Sub(b, a), ray3d::Sub(c, a)));
    std::vector<EdgeUse> uses;
    if (apex) {
      uses = {EdgeUse{baseEdge[static_cast<std::size_t>(i)], false},
              EdgeUse{sideEdge[static_cast<std::size_t>(j)], false},
              EdgeUse{sideEdge[static_cast<std::size_t>(i)], true}};
    } else {
      uses = {EdgeUse{baseEdge[static_cast<std::size_t>(i)], false},
              EdgeUse{sideEdge[static_cast<std::size_t>(j)], false},
              EdgeUse{topEdge[static_cast<std::size_t>(i)], true},
              EdgeUse{sideEdge[static_cast<std::size_t>(i)], true}};
    }
    s.faces.push_back(MakePlaneFace(a, nrm, std::move(uses)));
  }

  AddSingleShell(&s);
  s.recipe.kind = PrimitiveKind::Pyramid;
  s.recipe.sides = sides;
  s.recipe.radius = baseRadius;
  s.recipe.radius2 = topRadius;
  s.recipe.height = height;
  PlaceInFrame(&s, frame);

  const Problem why = Validate(s);
  if (why != Problem::Ok)
    return Fail(why, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

namespace {


/// Cylinder and cone differ only in their top radius and in which `SurfaceKind` the side carries,
/// so one builder serves both. Splitting the rims at a seam (rather than leaving a full-circle
/// edge) is what makes every edge bound exactly two faces, which is the invariant `Validate` leans
/// on hardest.
[[nodiscard]] bool BuildConical(const ucs::Ucs& frame, double r0, double r1, double h, bool asCylinder,
                                Solid* out, Problem* outWhy) {
  const bool apex = !(r1 > 0.0);

  Solid s;
  const int b0 = AddVertex(&s, Vec3{r0, 0.0, 0.0});
  const int b1 = AddVertex(&s, Vec3{-r0, 0.0, 0.0});
  int t0 = -1;
  int t1 = -1;
  int apexV = -1;
  if (apex) {
    apexV = AddVertex(&s, Vec3{0.0, 0.0, h});
  } else {
    t0 = AddVertex(&s, Vec3{r1, 0.0, h});
    t1 = AddVertex(&s, Vec3{-r1, 0.0, h});
  }

  const Vec3 up{0.0, 0.0, 1.0};
  const Vec3 baseCentre{0.0, 0.0, 0.0};
  const int rb0 = AddArc(&s, b0, b1, baseCentre, up, kPi);
  const int rb1 = AddArc(&s, b1, b0, baseCentre, up, kPi);

  int rt0 = -1;
  int rt1 = -1;
  int sm0 = -1;
  int sm1 = -1;
  if (apex) {
    sm0 = AddLine(&s, b0, apexV);
    sm1 = AddLine(&s, b1, apexV);
  } else {
    const Vec3 topCentre{0.0, 0.0, h};
    rt0 = AddArc(&s, t0, t1, topCentre, up, kPi);
    rt1 = AddArc(&s, t1, t0, topCentre, up, kPi);
    sm0 = AddLine(&s, b0, t0);
    sm1 = AddLine(&s, b1, t1);
  }

  // Base cap: counter-clockwise about -Z means clockwise about +Z, so both rim arcs run reversed.
  s.faces.push_back(
      MakePlaneFace(baseCentre, Vec3{0.0, 0.0, -1.0}, {{rb1, true}, {rb0, true}}));
  if (!apex)
    s.faces.push_back(
        MakePlaneFace(Vec3{0.0, 0.0, h}, Vec3{0.0, 0.0, 1.0}, {{rt0, false}, {rt1, false}}));

  auto sideFace = [&](double u0, double u1, std::vector<EdgeUse> uses) {
    Face f;
    f.surface.kind = asCylinder ? SurfaceKind::Cylinder : SurfaceKind::Cone;
    f.surface.frame = ucs::Ucs{};  // the canonical local frame; PlaceInFrame maps it
    f.surface.radius = r0;
    f.surface.radius2 = r1;
    f.surface.height = h;
    f.uStart = u0;
    f.uEnd = u1;
    Loop lp;
    lp.uses = std::move(uses);
    f.loops.push_back(std::move(lp));
    s.faces.push_back(std::move(f));
  };

  if (apex) {
    sideFace(0.0, kPi, {{rb0, false}, {sm1, false}, {sm0, true}});
    sideFace(kPi, kTwoPi, {{rb1, false}, {sm0, false}, {sm1, true}});
  } else {
    sideFace(0.0, kPi, {{rb0, false}, {sm1, false}, {rt0, true}, {sm0, true}});
    sideFace(kPi, kTwoPi, {{rb1, false}, {sm0, false}, {rt1, true}, {sm1, true}});
  }

  AddSingleShell(&s);
  s.recipe.kind = asCylinder ? PrimitiveKind::Cylinder : PrimitiveKind::Cone;
  s.recipe.radius = r0;
  s.recipe.radius2 = r1;
  s.recipe.height = h;
  PlaceInFrame(&s, frame);

  const Problem why = Validate(s);
  if (why != Problem::Ok)
    return Fail(why, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

} // namespace

bool MakeCylinder(const ucs::Ucs& frame, double radius, double height, Solid* out, Problem* outWhy) {
  if (!out)
    return false;  // a null output is a caller bug, not a user-facing reason: outWhy is left alone
  if (!AllFinite({radius, height}))
    return Fail(Problem::NonFiniteParameter, outWhy);
  if (!(radius > 0.0))
    return Fail(Problem::NonPositiveRadius, outWhy);
  if (!(height > 0.0))
    return Fail(Problem::NonPositiveHeight, outWhy);
  if (!FrameOk(frame))
    return Fail(Problem::DegenerateFrame, outWhy);
  return BuildConical(frame, radius, radius, height, /*asCylinder=*/true, out, outWhy);
}

bool MakeCone(const ucs::Ucs& frame, double baseRadius, double topRadius, double height, Solid* out,
              Problem* outWhy) {
  if (!out)
    return false;  // a null output is a caller bug, not a user-facing reason: outWhy is left alone
  if (!AllFinite({baseRadius, topRadius, height}))
    return Fail(Problem::NonFiniteParameter, outWhy);
  if (!(baseRadius > 0.0))
    return Fail(Problem::NonPositiveRadius, outWhy);
  if (topRadius < 0.0)
    return Fail(Problem::NegativeTopRadius, outWhy);
  if (topRadius >= baseRadius)
    return Fail(Problem::TopRadiusNotBelowBase, outWhy);
  if (!(height > 0.0))
    return Fail(Problem::NonPositiveHeight, outWhy);
  if (!FrameOk(frame))
    return Fail(Problem::DegenerateFrame, outWhy);
  return BuildConical(frame, baseRadius, topRadius, height, /*asCylinder=*/false, out, outWhy);
}


// ---------------------------------------------------------------------------
// A solid between a sampled height field and a flat plane — the earthwork pad (GitHub #150,
// 3D Phase 7).
// ---------------------------------------------------------------------------

bool MakeHeightFieldSolid(const HeightField& hf, Solid* out, Problem* outWhy) {
  if (!out)
    return false;  // a null output is a caller bug, not a user-facing reason
  if (hf.cols < 1 || hf.rows < 1)
    return Fail(Problem::IndexOutOfRange, outWhy);
  const int nx = hf.cols + 1;
  const int ny = hf.rows + 1;
  if (static_cast<int>(hf.nodeZ.size()) != nx * ny ||
      static_cast<int>(hf.cellIn.size()) != hf.cols * hf.rows)
    return Fail(Problem::IndexOutOfRange, outWhy);
  if (!AllFinite({hf.originX, hf.originY, hf.cellW, hf.cellH, hf.flatZ}))
    return Fail(Problem::NonFiniteParameter, outWhy);
  if (!(hf.cellW > 0.0) || !(hf.cellH > 0.0))
    return Fail(Problem::NonPositiveWidth, outWhy);
  for (double z : hf.nodeZ)
    if (!std::isfinite(z))
      return Fail(Problem::NonFiniteParameter, outWhy);

  const auto nodeIx = [nx](int i, int j) { return static_cast<std::size_t>(j) * static_cast<std::size_t>(nx) +
                                                  static_cast<std::size_t>(i); };
  const auto cellIx = [&hf](int ci, int cj) { return static_cast<std::size_t>(cj) *
                                                     static_cast<std::size_t>(hf.cols) +
                                                     static_cast<std::size_t>(ci); };
  // The two surfaces the solid lies between. For a CUT the ground is on top and the pad is the
  // floor; for a FILL the pad is the top and the ground is what it rests on. One construction,
  // because the only difference is which of the two is the upper surface.
  const auto upperZ = [&hf](double groundZ) { return hf.flatIsBottom ? groundZ : hf.flatZ; };
  const auto lowerZ = [&hf](double groundZ) { return hf.flatIsBottom ? hf.flatZ : groundZ; };
  const double eps = 1e-9 * std::max({std::fabs(hf.cellW), std::fabs(hf.cellH), 1.0});

  // A cell is only built where it has REAL thickness at all four corners. A cell that tapers to
  // nothing — which is what happens along the line where the ground crosses the pad elevation —
  // would contribute zero-area faces and an edge used more than twice, and the result would not be
  // a solid at all. Those cells are counted and left out; the caller reports the shortfall rather
  // than the kernel pretending the shape is exact.
  std::vector<std::uint8_t> build(static_cast<std::size_t>(hf.cols) * static_cast<std::size_t>(hf.rows), 0);
  int tapered = 0;
  for (int cj = 0; cj < hf.rows; ++cj) {
    for (int ci = 0; ci < hf.cols; ++ci) {
      if (!hf.cellIn[cellIx(ci, cj)])
        continue;
      bool thick = true;
      for (int dj = 0; dj <= 1 && thick; ++dj)
        for (int di = 0; di <= 1 && thick; ++di) {
          const double g = hf.nodeZ[nodeIx(ci + di, cj + dj)];
          if (!(upperZ(g) - lowerZ(g) > eps))
            thick = false;
        }
      if (thick)
        build[cellIx(ci, cj)] = 1;
      else
        ++tapered;
    }
  }
  if (std::find(build.begin(), build.end(), 1) == build.end())
    return Fail(Problem::SlicePlaneMissesSolid, outWhy);  // nothing of the pad has any depth

  Solid s;
  // One vertex per (node, upper/lower), created on first use so an unused corner of the grid costs
  // nothing.
  std::vector<int> vUp(static_cast<std::size_t>(nx) * static_cast<std::size_t>(ny), -1);
  std::vector<int> vLo(vUp.size(), -1);
  const auto nodeXY = [&hf](int i, int j) {
    return std::pair<double, double>{hf.originX + hf.cellW * static_cast<double>(i),
                                     hf.originY + hf.cellH * static_cast<double>(j)};
  };
  const auto upVert = [&](int i, int j) {
    int& v = vUp[nodeIx(i, j)];
    if (v < 0) {
      const auto xy = nodeXY(i, j);
      v = AddVertex(&s, Vec3{xy.first, xy.second, upperZ(hf.nodeZ[nodeIx(i, j)])});
    }
    return v;
  };
  const auto loVert = [&](int i, int j) {
    int& v = vLo[nodeIx(i, j)];
    if (v < 0) {
      const auto xy = nodeXY(i, j);
      v = AddVertex(&s, Vec3{xy.first, xy.second, lowerZ(hf.nodeZ[nodeIx(i, j)])});
    }
    return v;
  };
  // Every edge is shared by exactly two faces, which is what makes this a solid rather than a pile
  // of triangles — so edges are created once and looked up by their vertex pair.
  std::map<std::pair<int, int>, int> edgeOf;
  const auto edgeBetween = [&](int a, int b, bool* reversed) {
    const std::pair<int, int> key = a < b ? std::make_pair(a, b) : std::make_pair(b, a);
    *reversed = !(a < b);
    const auto it = edgeOf.find(key);
    if (it != edgeOf.end())
      return it->second;
    const int e = AddLine(&s, key.first, key.second);
    edgeOf.emplace(key, e);
    return e;
  };
  // The face's plane is derived from the ring itself (Newell), not passed in: a sampled ground
  // surface is not level, so a top triangle's normal is only +Z when the ground happens to be flat,
  // and a plane face whose vertices do not lie on its own plane is not a face -- Validate refuses it.
  const auto faceFrom = [&](const std::vector<int>& ring) {
    Vec3 nrm{0.0, 0.0, 0.0};
    for (std::size_t k = 0; k < ring.size(); ++k) {
      const Vec3& p = s.vertices[static_cast<std::size_t>(ring[k])].p;
      const Vec3& q = s.vertices[static_cast<std::size_t>(ring[(k + 1) % ring.size()])].p;
      nrm.x += (p.y - q.y) * (p.z + q.z);
      nrm.y += (p.z - q.z) * (p.x + q.x);
      nrm.z += (p.x - q.x) * (p.y + q.y);
    }
    const Vec3 normal = ray3d::Normalize(nrm);
    std::vector<EdgeUse> uses;
    uses.reserve(ring.size());
    for (std::size_t k = 0; k < ring.size(); ++k) {
      bool rev = false;
      const int e = edgeBetween(ring[k], ring[(k + 1) % ring.size()], &rev);
      uses.push_back(EdgeUse{e, rev});
    }
    s.faces.push_back(MakePlaneFace(s.vertices[static_cast<std::size_t>(ring[0])].p, normal, std::move(uses)));
  };

  for (int cj = 0; cj < hf.rows; ++cj) {
    for (int ci = 0; ci < hf.cols; ++ci) {
      if (!build[cellIx(ci, cj)])
        continue;
      // The cell's four corners, counter-clockwise seen from above.
      const int u00 = upVert(ci, cj), u10 = upVert(ci + 1, cj);
      const int u11 = upVert(ci + 1, cj + 1), u01 = upVert(ci, cj + 1);
      const int l00 = loVert(ci, cj), l10 = loVert(ci + 1, cj);
      const int l11 = loVert(ci + 1, cj + 1), l01 = loVert(ci, cj + 1);
      // The upper surface is two triangles, not one quad: four sampled ground elevations are not
      // coplanar in general, and a plane face through four non-coplanar points is not a face.
      faceFrom({u00, u10, u11});
      faceFrom({u00, u11, u01});
      // The lower surface, wound the other way so its normal points out of the solid.
      faceFrom({l00, l11, l10});
      faceFrom({l00, l01, l11});
      // A wall wherever the neighbour is not part of the solid. Each is a vertical quad, and all
      // four of its corners lie in one vertical plane however the ground slopes, so it stays planar.
      struct Side { int di, dj, a, b, la, lb; };
      const Side sides[4] = {
          {0, -1, u00, u10, l00, l10},   // south
          {1, 0, u10, u11, l10, l11},    // east
          {0, 1, u11, u01, l11, l01},    // north
          {-1, 0, u01, u00, l01, l00},   // west
      };
      for (const Side& sd : sides) {
        const int ni = ci + sd.di;
        const int nj = cj + sd.dj;
        const bool neighbourBuilt = ni >= 0 && nj >= 0 && ni < hf.cols && nj < hf.rows &&
                                    build[cellIx(ni, nj)] != 0;
        if (neighbourBuilt)
          continue;
        const Vec3 a = s.vertices[static_cast<std::size_t>(sd.la)].p;
        const Vec3 b = s.vertices[static_cast<std::size_t>(sd.lb)].p;
        (void)a;
        (void)b;
        faceFrom({sd.la, sd.lb, sd.b, sd.a});
      }
    }
  }

  AddSingleShell(&s);
  s.recipe.kind = PrimitiveKind::None;
  const Problem why = Validate(s);
  if (why != Problem::Ok)
    return Fail(why, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

bool MakeSphere(const ucs::Ucs& frame, double radius, Solid* out, Problem* outWhy) {
  if (!out)
    return false;  // a null output is a caller bug, not a user-facing reason: outWhy is left alone
  if (!AllFinite({radius}))
    return Fail(Problem::NonFiniteParameter, outWhy);
  if (!(radius > 0.0))
    return Fail(Problem::NonPositiveRadius, outWhy);
  if (!FrameOk(frame))
    return Fail(Problem::DegenerateFrame, outWhy);

  Solid s;
  const int south = AddVertex(&s, Vec3{0.0, 0.0, -radius});
  const int north = AddVertex(&s, Vec3{0.0, 0.0, radius});

  // The two half-meridians that seam the sphere. Their normals are chosen so that sweeping +pi
  // from the south pole runs through the equator at longitude 0 and pi respectively.
  const Vec3 centre{0.0, 0.0, 0.0};
  const int m0 = AddArc(&s, south, north, centre, Vec3{0.0, -1.0, 0.0}, kPi);
  const int m1 = AddArc(&s, south, north, centre, Vec3{0.0, 1.0, 0.0}, kPi);

  auto half = [&](double u0, double u1, std::vector<EdgeUse> uses) {
    Face f;
    f.surface.kind = SurfaceKind::Sphere;
    f.surface.radius = radius;
    f.uStart = u0;
    f.uEnd = u1;
    f.vStart = -kHalfPi;
    f.vEnd = kHalfPi;
    Loop lp;
    lp.uses = std::move(uses);
    f.loops.push_back(std::move(lp));
    s.faces.push_back(std::move(f));
  };
  half(0.0, kPi, {{m1, false}, {m0, true}});
  half(kPi, kTwoPi, {{m0, false}, {m1, true}});

  AddSingleShell(&s);
  s.recipe.kind = PrimitiveKind::Sphere;
  s.recipe.radius = radius;
  PlaceInFrame(&s, frame);

  const Problem why = Validate(s);
  if (why != Problem::Ok)
    return Fail(why, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

bool MakeTorus(const ucs::Ucs& frame, double majorRadius, double minorRadius, Solid* out, Problem* outWhy) {
  if (!out)
    return false;  // a null output is a caller bug, not a user-facing reason: outWhy is left alone
  if (!AllFinite({majorRadius, minorRadius}))
    return Fail(Problem::NonFiniteParameter, outWhy);
  if (!(majorRadius > 0.0) || !(minorRadius > 0.0))
    return Fail(Problem::NonPositiveRadius, outWhy);
  // A tube LARGER than the ring is allowed, and self-intersects — the shape AutoCAD builds and that
  // users draw deliberately (ADR-045 (f) as amended). Only the EXACTLY equal case is refused: there
  // the inner equator collapses to a point, both inner rim edges have zero radius, and the result is
  // not a solid at all. `Validate` would reject it a moment later as a degenerate edge, so it is
  // refused here by name instead of by a symptom.
  if (minorRadius == majorRadius)
    return Fail(Problem::MinorRadiusEqualsMajor, outWhy);
  if (!FrameOk(frame))
    return Fail(Problem::DegenerateFrame, outWhy);

  const double R = majorRadius;
  const double r = minorRadius;

  Solid s;
  // Vertices at the four corners of the (t, v) cut pattern: t in {0, pi}, v in {0, pi}.
  const int v00 = AddVertex(&s, Vec3{R + r, 0.0, 0.0});
  const int v0p = AddVertex(&s, Vec3{R - r, 0.0, 0.0});
  const int vp0 = AddVertex(&s, Vec3{-(R + r), 0.0, 0.0});
  const int vpp = AddVertex(&s, Vec3{-(R - r), 0.0, 0.0});

  const Vec3 up{0.0, 0.0, 1.0};
  const Vec3 origin{0.0, 0.0, 0.0};
  // Rings around the axis, at the outer (v = 0) and inner (v = pi) equators.
  const int e1 = AddArc(&s, v00, vp0, origin, up, kPi);
  const int e2 = AddArc(&s, vp0, v00, origin, up, kPi);
  const int e3 = AddArc(&s, v0p, vpp, origin, up, kPi);
  const int e4 = AddArc(&s, vpp, v0p, origin, up, kPi);
  // Rings around the tube, at t = 0 and t = pi. The normals are the ones for which increasing v
  // sweeps counter-clockwise, which is what keeps the tube ring's winding and the surface's own
  // (t, v) parametrisation in agreement.
  const Vec3 tube0Centre{R, 0.0, 0.0};
  const Vec3 tubePCentre{-R, 0.0, 0.0};
  const int e5 = AddArc(&s, v00, v0p, tube0Centre, Vec3{0.0, -1.0, 0.0}, kPi);
  const int e6 = AddArc(&s, v0p, v00, tube0Centre, Vec3{0.0, -1.0, 0.0}, kPi);
  const int e7 = AddArc(&s, vp0, vpp, tubePCentre, Vec3{0.0, 1.0, 0.0}, kPi);
  const int e8 = AddArc(&s, vpp, vp0, tubePCentre, Vec3{0.0, 1.0, 0.0}, kPi);

  auto patch = [&](double u0, double u1, double vs, double ve, std::vector<EdgeUse> uses) {
    Face f;
    f.surface.kind = SurfaceKind::Torus;
    f.surface.radius = R;
    f.surface.radius2 = r;
    f.uStart = u0;
    f.uEnd = u1;
    f.vStart = vs;
    f.vEnd = ve;
    Loop lp;
    lp.uses = std::move(uses);
    f.loops.push_back(std::move(lp));
    s.faces.push_back(std::move(f));
  };
  patch(0.0, kPi, 0.0, kPi, {{e1, false}, {e7, false}, {e3, true}, {e5, true}});
  patch(kPi, kTwoPi, 0.0, kPi, {{e2, false}, {e5, false}, {e4, true}, {e7, true}});
  patch(0.0, kPi, kPi, kTwoPi, {{e3, false}, {e8, false}, {e1, true}, {e6, true}});
  patch(kPi, kTwoPi, kPi, kTwoPi, {{e4, false}, {e6, false}, {e2, true}, {e8, true}});

  AddSingleShell(&s);
  s.recipe.kind = PrimitiveKind::Torus;
  s.recipe.radius = R;
  s.recipe.radius2 = r;
  PlaceInFrame(&s, frame);

  const Problem why = Validate(s);
  if (why != Problem::Ok)
    return Fail(why, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

} // namespace brep
