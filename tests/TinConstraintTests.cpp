// REQ-069 — surface breaklines, boundary rings, and their diagnostics.
//
// The breakline test proves the constrained-edge insertion actually FORCES a diagonal Delaunay would
// not have chosen on its own — not merely that it agrees with what Delaunay already does — by first
// building unconstrained to see which diagonal is "natural," then constraining the OTHER one and
// checking it is honoured. The boundary tests build a flat grid (so elevation cannot mask a wrong
// answer) and check triangle survival by centroid, matching TinCullByBoundaries' own documented rule.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <cstdint>
#include <vector>

#include "util/tinbuild.hpp"

using Catch::Approx;

namespace {

/// BuildTin sorts its de-duplicated points by (x,y) before assigning indices (step 1), so an output
/// vertex index does NOT match the caller's input order. Every test below must look vertices up by
/// coordinate rather than assume index i came from pts[i].
std::uint32_t FindVertexIndex(const TinBuildResult& r, double x, double y) {
  for (int i = 0; i < r.vertexCount(); ++i) {
    const float vx = r.vertsXyz[static_cast<size_t>(i) * 3], vy = r.vertsXyz[static_cast<size_t>(i) * 3 + 1];
    if (std::fabs(vx - x) < 1e-6 && std::fabs(vy - y) < 1e-6)
      return static_cast<std::uint32_t>(i);
  }
  return 0xFFFFFFFFu;
}

bool HasEdge(const TinBuildResult& r, std::uint32_t a, std::uint32_t b) {
  for (size_t i = 0; i + 2 < r.indices.size(); i += 3) {
    const std::uint32_t v[3] = {r.indices[i], r.indices[i + 1], r.indices[i + 2]};
    for (int k = 0; k < 3; ++k)
      if ((v[k] == a && v[(k + 1) % 3] == b) || (v[k] == b && v[(k + 1) % 3] == a))
        return true;
  }
  return false;
}

/// Strict interior crossing, same formula the implementation itself uses — kept independent here
/// (not a call into tinbuild.cpp internals) so the test does not just re-check its own assumptions.
bool SegmentsProperlyCross(double ax, double ay, double bx, double by, double cx, double cy, double dx,
                           double dy) {
  const double rx = bx - ax, ry = by - ay;
  const double sx = dx - cx, sy = dy - cy;
  const double rxs = rx * sy - ry * sx;
  if (std::fabs(rxs) < 1e-12)
    return false;
  const double qx = cx - ax, qy = cy - ay;
  const double t = (qx * sy - qy * sx) / rxs;
  const double u = (qx * ry - qy * rx) / rxs;
  return t > 0.0 && t < 1.0 && u > 0.0 && u < 1.0;
}

/// No triangle edge in \p r (other than the constraint edge itself) crosses (ax,ay)-(bx,by).
bool NoTriangleEdgeCrosses(const TinBuildResult& r, std::uint32_t fa, std::uint32_t fb, double ax,
                          double ay, double bx, double by) {
  for (size_t i = 0; i + 2 < r.indices.size(); i += 3) {
    const std::uint32_t v[3] = {r.indices[i], r.indices[i + 1], r.indices[i + 2]};
    for (int k = 0; k < 3; ++k) {
      const std::uint32_t u = v[k], w = v[(k + 1) % 3];
      if ((u == fa && w == fb) || (u == fb && w == fa))
        continue;  // the constraint edge itself, not a crossing
      const double ux = r.vertsXyz[u * 3], uy = r.vertsXyz[u * 3 + 1];
      const double wx = r.vertsXyz[w * 3], wy = r.vertsXyz[w * 3 + 1];
      if (SegmentsProperlyCross(ax, ay, bx, by, ux, uy, wx, wy))
        return false;
    }
  }
  return true;
}

/// A flat 5x5 grid over [0,size]x[0,size] — elevation is irrelevant to boundary culling, so it is
/// held at a constant to keep the fixture legible.
TinBuildResult FlatGrid(double size = 100.0, int n = 5) {
  std::vector<TinInputPoint> pts;
  for (int j = 0; j < n; ++j)
    for (int i = 0; i < n; ++i)
      pts.push_back({size * i / (n - 1), size * j / (n - 1), 50.f});
  return BuildTin(pts);
}

} // namespace

TEST_CASE("A breakline forces the diagonal Delaunay did not pick, and nothing crosses it", "[tin][req069]") {
  // An asymmetric quad — a rectangle or square is exactly co-circular on both diagonals, which
  // would make the "natural" choice ambiguous and this test meaningless.
  const std::vector<TinInputPoint> pts = {
      {0.0, 0.0, 0.f}, {4.0, 0.0, 0.f}, {4.0, 4.0, 10.f}, {0.0, 3.0, 0.f}};

  const TinBuildResult unconstrained = BuildTin(pts);
  REQUIRE(unconstrained.ok());
  REQUIRE(unconstrained.triangleCount() == 2);
  const std::uint32_t a = FindVertexIndex(unconstrained, 0.0, 0.0), b = FindVertexIndex(unconstrained, 4.0, 0.0),
                      c = FindVertexIndex(unconstrained, 4.0, 4.0), d = FindVertexIndex(unconstrained, 0.0, 3.0);
  const bool naturalIsAC = HasEdge(unconstrained, a, c);
  const bool naturalIsBD = HasEdge(unconstrained, b, d);
  REQUIRE(naturalIsAC != naturalIsBD);  // exactly one diagonal in a 2-triangle quad

  // Force whichever diagonal Delaunay did NOT choose.
  const TinInputPoint& p1 = naturalIsAC ? pts[1] : pts[0];
  const TinInputPoint& p2 = naturalIsAC ? pts[3] : pts[2];
  TinConstraint forced;
  forced.ax = p1.x; forced.ay = p1.y; forced.az = p1.z;
  forced.bx = p2.x; forced.by = p2.y; forced.bz = p2.z;

  const TinBuildResult constrained = BuildTin(pts, {forced});
  REQUIRE(constrained.ok());
  CHECK(constrained.constraintsUnresolved == 0);
  const std::uint32_t ia = FindVertexIndex(constrained, p1.x, p1.y);
  const std::uint32_t ib = FindVertexIndex(constrained, p2.x, p2.y);
  CHECK(HasEdge(constrained, ia, ib));
  CHECK(NoTriangleEdgeCrosses(constrained, ia, ib, forced.ax, forced.ay, forced.bx, forced.by));
}

TEST_CASE("A constraint edge already present in the unconstrained build is a no-op", "[tin][req069]") {
  const std::vector<TinInputPoint> pts = {
      {0.0, 0.0, 0.f}, {4.0, 0.0, 0.f}, {4.0, 4.0, 10.f}, {0.0, 3.0, 0.f}};
  const TinBuildResult unconstrained = BuildTin(pts);
  REQUIRE(unconstrained.ok());
  const std::uint32_t a = FindVertexIndex(unconstrained, 0.0, 0.0), c = FindVertexIndex(unconstrained, 4.0, 4.0);
  const bool naturalIsAC = HasEdge(unconstrained, a, c);
  const TinInputPoint& p1 = naturalIsAC ? pts[0] : pts[1];
  const TinInputPoint& p2 = naturalIsAC ? pts[2] : pts[3];

  TinConstraint already;
  already.ax = p1.x; already.ay = p1.y; already.az = p1.z;
  already.bx = p2.x; already.by = p2.y; already.bz = p2.z;

  const TinBuildResult r = BuildTin(pts, {already});
  REQUIRE(r.ok());
  CHECK(r.constraintsUnresolved == 0);
  CHECK(r.triangleCount() == 2);
  CHECK(HasEdge(r, FindVertexIndex(r, p1.x, p1.y), FindVertexIndex(r, p2.x, p2.y)));
}

TEST_CASE("A breakline vertex not among the surface points is folded in with its own elevation",
         "[tin][req069]") {
  // Four survey points around a breakline whose two endpoints are NOT survey points — the ordinary
  // case of an independently-drawn breakline polyline.
  const std::vector<TinInputPoint> pts = {
      {0.0, 0.0, 0.f}, {10.0, 0.0, 0.f}, {10.0, 10.0, 0.f}, {0.0, 10.0, 0.f}};
  TinConstraint c;
  c.ax = 2.0; c.ay = 5.0; c.az = 7.5f;
  c.bx = 8.0; c.by = 5.0; c.bz = 7.5f;

  const TinBuildResult r = BuildTin(pts, {c});
  REQUIRE(r.ok());
  CHECK(r.constraintsUnresolved == 0);
  REQUIRE(r.vertexCount() == 6);  // 4 survey points + 2 breakline vertices, none coincident

  // The two breakline vertices ended up in the output with the elevation the constraint carried,
  // not some interpolated or zeroed value.
  int foundA = 0, foundB = 0;
  for (int i = 0; i < r.vertexCount(); ++i) {
    const float x = r.vertsXyz[static_cast<size_t>(i) * 3], y = r.vertsXyz[static_cast<size_t>(i) * 3 + 1],
                z = r.vertsXyz[static_cast<size_t>(i) * 3 + 2];
    if (std::fabs(x - 2.0f) < 1e-6f && std::fabs(y - 5.0f) < 1e-6f) { CHECK(z == Approx(7.5)); ++foundA; }
    if (std::fabs(x - 8.0f) < 1e-6f && std::fabs(y - 5.0f) < 1e-6f) { CHECK(z == Approx(7.5)); ++foundB; }
  }
  CHECK(foundA == 1);
  CHECK(foundB == 1);
}

TEST_CASE("Crossing breaklines at different elevations are reported, sharing an endpoint is not",
         "[tin][req069]") {
  // A and B cross at (5,5) — A is flat at z=1, B is flat at z=9. A real conflict.
  TinConstraint a{0.0, 5.0, 1.f, 10.0, 5.0, 1.f};
  TinConstraint b{5.0, 0.0, 9.f, 5.0, 10.0, 9.f};
  const auto issues = TinFindCrossingConflicts({a, b});
  REQUIRE(issues.size() == 1);
  CHECK(issues[0].x == Approx(5.0));
  CHECK(issues[0].y == Approx(5.0));
  CHECK(issues[0].zFromA == Approx(1.0));
  CHECK(issues[0].zFromB == Approx(9.0));

  // Two breaklines meeting at a shared endpoint, both at the same elevation there, are the ordinary
  // case of a breakline chain — not a crossing conflict.
  TinConstraint c{0.0, 0.0, 3.f, 5.0, 5.0, 3.f};
  TinConstraint d{5.0, 5.0, 3.f, 10.0, 0.0, 3.f};
  CHECK(TinFindCrossingConflicts({c, d}).empty());
}

TEST_CASE("A duplicate point with conflicting elevation is counted separately from an ordinary duplicate",
         "[tin][req069]") {
  const std::vector<TinInputPoint> pts = {
      {0.0, 0.0, 5.f}, {10.0, 0.0, 5.f}, {10.0, 10.0, 5.f}, {0.0, 10.0, 5.f},
      {0.005, 0.005, 5.f},   // same site as (0,0), same elevation — ordinary duplicate
      {5.0, 5.005, 12.f},    // a NEW distinct site otherwise, so on its own would not dedupe...
  };
  // ...so pair it with a near-coincident conflicting shot to actually exercise the conflict path.
  std::vector<TinInputPoint> pts2 = pts;
  pts2.push_back({5.0, 5.0, 20.f});  // within kTinPlanEpsilon of the previous point, disagreeing Z

  const TinBuildResult r = BuildTin(pts2);
  REQUIRE(r.ok());
  CHECK(r.duplicatesDropped >= 2);
  CHECK(r.conflictingDuplicates == 1);
}

TEST_CASE("An outer boundary clips a flat grid to inside itself", "[tin][req069]") {
  const TinBuildResult tin = FlatGrid();
  REQUIRE(tin.ok());
  std::vector<std::uint32_t> indices = tin.indices;

  TinBoundaryLoop outer;
  outer.kind = TinBoundaryKind::Outer;
  outer.ring = {{20, 20}, {80, 20}, {80, 80}, {20, 80}};
  TinCullByBoundaries(indices, tin.vertsXyz, {outer});

  REQUIRE(!indices.empty());
  for (size_t t = 0; t + 2 < indices.size(); t += 3) {
    const std::uint32_t a = indices[t], b = indices[t + 1], c = indices[t + 2];
    const double cx = (tin.vertsXyz[a * 3] + tin.vertsXyz[b * 3] + tin.vertsXyz[c * 3]) / 3.0;
    const double cy =
        (tin.vertsXyz[a * 3 + 1] + tin.vertsXyz[b * 3 + 1] + tin.vertsXyz[c * 3 + 1]) / 3.0;
    CHECK(cx >= 20.0);
    CHECK(cx <= 80.0);
    CHECK(cy >= 20.0);
    CHECK(cy <= 80.0);
  }
}

TEST_CASE("A hide boundary leaves a void, and a show boundary inside it restores surface there",
         "[tin][req069]") {
  const TinBuildResult tin = FlatGrid();
  REQUIRE(tin.ok());

  auto centroidInRing = [&](std::uint32_t a, std::uint32_t b, std::uint32_t c, double x0, double y0,
                            double x1, double y1) {
    const double cx = (tin.vertsXyz[a * 3] + tin.vertsXyz[b * 3] + tin.vertsXyz[c * 3]) / 3.0;
    const double cy =
        (tin.vertsXyz[a * 3 + 1] + tin.vertsXyz[b * 3 + 1] + tin.vertsXyz[c * 3 + 1]) / 3.0;
    return cx > x0 && cx < x1 && cy > y0 && cy < y1;
  };
  auto anyTriangleIn = [&](const std::vector<std::uint32_t>& idx, double x0, double y0, double x1,
                           double y1) {
    for (size_t t = 0; t + 2 < idx.size(); t += 3)
      if (centroidInRing(idx[t], idx[t + 1], idx[t + 2], x0, y0, x1, y1))
        return true;
    return false;
  };

  // Grid spacing is 25 units (5 points over [0,100]), so a triangle's centroid sits well away from
  // its own vertices — rings need enough margin around 50,50 to actually enclose a centroid.
  REQUIRE(anyTriangleIn(tin.indices, 25, 25, 75, 75));  // sanity: the void region has surface to begin with

  // Hide alone: the whole inner region loses every triangle.
  std::vector<std::uint32_t> hidden = tin.indices;
  TinBoundaryLoop hide;
  hide.kind = TinBoundaryKind::Hide;
  hide.ring = {{20, 20}, {80, 20}, {80, 80}, {20, 80}};
  TinCullByBoundaries(hidden, tin.vertsXyz, {hide});
  CHECK_FALSE(anyTriangleIn(hidden, 25, 25, 75, 75));
  CHECK(anyTriangleIn(hidden, 0, 0, 15, 15));  // untouched elsewhere, near the corner outside the hide ring

  // Hide then Show (in that definition order) restores a smaller island inside the void.
  std::vector<std::uint32_t> restored = tin.indices;
  TinBoundaryLoop show;
  show.kind = TinBoundaryKind::Show;
  show.ring = {{40, 40}, {60, 40}, {60, 60}, {40, 60}};
  TinCullByBoundaries(restored, tin.vertsXyz, {hide, show});
  CHECK(anyTriangleIn(restored, 40, 40, 60, 60));   // the show island is back
  CHECK_FALSE(anyTriangleIn(restored, 20, 20, 30, 30));  // still hidden outside the island
}

TEST_CASE("A show boundary cannot restore surface an outer boundary clipped away", "[tin][req069]") {
  // Regression. Show and Outer originally shared ONE inclusion mask, so `included[t] = 1` could not
  // tell which of the two had removed a triangle — and a Show ring lying outside an Outer ring
  // pulled the clipped-away surface back, producing surface beyond the surface's own boundary. On a
  // 5x5 flat grid an Outer ring over the left half kept 24 of 32 triangles; adding a Show ring in
  // the right half restored all 32. TinCullByBoundaries now keeps the extent mask (Outer) and the
  // void mask (Hide/Show) separate, which is what its header has always documented: a Show "only
  // has a visible effect where an earlier Hide removed them".
  const TinBuildResult tin = FlatGrid();
  REQUIRE(tin.ok());

  auto cull = [&](const std::vector<TinBoundaryLoop>& loops) {
    std::vector<std::uint32_t> idx = tin.indices;
    TinCullByBoundaries(idx, tin.vertsXyz, loops);
    return idx.size() / 3;
  };

  TinBoundaryLoop outer;
  outer.kind = TinBoundaryKind::Outer;
  outer.ring = {{-10, -10}, {50, -10}, {50, 110}, {-10, 110}};  // keeps the left half only

  TinBoundaryLoop showRight;
  showRight.kind = TinBoundaryKind::Show;
  showRight.ring = {{60, -10}, {110, -10}, {110, 110}, {60, 110}};  // wholly OUTSIDE the outer ring

  const size_t all = tin.indices.size() / 3;
  const size_t clipped = cull({outer});
  REQUIRE(clipped > 0);
  REQUIRE(clipped < all);  // the outer ring really did clip something, so the check below can fail

  CHECK(cull({outer, showRight}) == clipped);  // the show ring must change nothing at all

  // The order the two are declared in must not matter either: an extent is an intersection.
  CHECK(cull({showRight, outer}) == clipped);

  // And the feature Show exists for still works within the extent: a Hide inside the outer ring,
  // then a Show over the same area, comes back to the outer-clipped count.
  TinBoundaryLoop hideLeft;
  hideLeft.kind = TinBoundaryKind::Hide;
  hideLeft.ring = {{10, 10}, {40, 10}, {40, 90}, {10, 90}};
  TinBoundaryLoop showLeft = hideLeft;
  showLeft.kind = TinBoundaryKind::Show;
  REQUIRE(cull({outer, hideLeft}) < clipped);
  CHECK(cull({outer, hideLeft, showLeft}) == clipped);
}

TEST_CASE("A breakline through an intervening vertex is honoured as a chain, not reported failed",
         "[tin][req069]") {
  // Regression. Constraint insertion terminated on edgeExists(ia, ib) — the constraint as a SINGLE
  // edge — which no triangulation can ever contain when a third vertex lies between the endpoints.
  // Every such constraint was therefore counted unresolved, and that is the ORDINARY surveying case:
  // a breakline drawn along a ridge through the shots that define it. The constraint is now split at
  // the vertices lying on it and each link is enforced.
  std::vector<TinInputPoint> pts = {
      {0, 0, 10.f}, {10, 0, 10.f},  // the breakline's own endpoints
      {5, 0, 10.f},                 // a shot sitting exactly ON it, strictly between them
      {0, 10, 20.f}, {10, 10, 20.f}, {5, -10, 0.f},
  };
  std::vector<TinConstraint> cons(1);
  cons[0].ax = 0;  cons[0].ay = 0; cons[0].az = 10.f;
  cons[0].bx = 10; cons[0].by = 0; cons[0].bz = 10.f;

  const TinBuildResult r = BuildTin(pts, cons);
  REQUIRE(r.ok());
  CHECK(r.constraintsUnresolved == 0);  // this was 1 before the split

  // The breakline is present as the chain (0,0)-(5,0)-(10,0). That IS the breakline: both links
  // lie along it, and no edge crosses it.
  const std::uint32_t a = FindVertexIndex(r, 0, 0);
  const std::uint32_t m = FindVertexIndex(r, 5, 0);
  const std::uint32_t b = FindVertexIndex(r, 10, 0);
  REQUIRE(a != 0xFFFFFFFFu);
  REQUIRE(m != 0xFFFFFFFFu);
  REQUIRE(b != 0xFFFFFFFFu);
  CHECK(HasEdge(r, a, m));
  CHECK(HasEdge(r, m, b));
  CHECK(NoTriangleEdgeCrosses(r, a, m, 0, 0, 5, 0));
  CHECK(NoTriangleEdgeCrosses(r, m, b, 5, 0, 10, 0));
}

TEST_CASE("Breaklines crossing at a shared vertex are both honoured and neither is reported",
         "[tin][req069]") {
  // The same defect seen from the other side: two breaklines meeting at a grid vertex reported TWO
  // unresolved constraints while both were in fact fully present in the mesh, edge for edge. Their
  // elevations agree at the crossing, so TinFindCrossingConflicts correctly says nothing — which is
  // precisely why a spurious count here was the user's only (and misleading) signal.
  std::vector<TinInputPoint> pts;
  for (int i = 0; i <= 4; ++i)
    for (int j = 0; j <= 4; ++j)
      pts.push_back({i * 10.0, j * 10.0, 5.f});
  std::vector<TinConstraint> cons(2);
  cons[0].ax = 0;  cons[0].ay = 20; cons[0].bx = 40; cons[0].by = 20;  // horizontal through (20,20)
  cons[1].ax = 20; cons[1].ay = 0;  cons[1].bx = 20; cons[1].by = 40;  // vertical   through (20,20)

  CHECK(TinFindCrossingConflicts(cons).empty());  // same elevation: not a conflict

  const TinBuildResult r = BuildTin(pts, cons);
  REQUIRE(r.ok());
  CHECK(r.constraintsUnresolved == 0);  // this was 2 before the split

  // Every link of both chains is present.
  for (int s = 0; s < 4; ++s) {
    const std::uint32_t h0 = FindVertexIndex(r, s * 10.0, 20);
    const std::uint32_t h1 = FindVertexIndex(r, (s + 1) * 10.0, 20);
    const std::uint32_t v0 = FindVertexIndex(r, 20, s * 10.0);
    const std::uint32_t v1 = FindVertexIndex(r, 20, (s + 1) * 10.0);
    REQUIRE(h0 != 0xFFFFFFFFu);
    REQUIRE(h1 != 0xFFFFFFFFu);
    REQUIRE(v0 != 0xFFFFFFFFu);
    REQUIRE(v1 != 0xFFFFFFFFu);
    CHECK(HasEdge(r, h0, h1));
    CHECK(HasEdge(r, v0, v1));
  }
}

TEST_CASE("A constraint that genuinely cannot be enforced is still reported", "[tin][req069]") {
  // The counterweight to the two cases above, and the reason they are safe: silencing a false alarm
  // must not silence the real one. These two breaklines cross at (15,15), which is NOT a vertex —
  // there is nothing to split at, and no triangulation can hold both edges. The count must still
  // move, or `constraintsUnresolved` would have become a field that is always zero.
  std::vector<TinInputPoint> pts;
  for (int i = 0; i <= 4; ++i)
    for (int j = 0; j <= 4; ++j)
      pts.push_back({i * 10.0, j * 10.0, 5.f});
  std::vector<TinConstraint> cons(2);
  cons[0].ax = 0;  cons[0].ay = 15; cons[0].bx = 40; cons[0].by = 15;
  cons[1].ax = 15; cons[1].ay = 0;  cons[1].bx = 15; cons[1].by = 40;

  const TinBuildResult r = BuildTin(pts, cons);
  REQUIRE(r.ok());
  CHECK(r.constraintsUnresolved > 0);
  CHECK(r.message.find("could not be enforced") != std::string::npos);
}

TEST_CASE("A vertex near a breakline but not on it is crossed, not treated as a split point",
         "[tin][req069]") {
  // The split tolerance is kTinPlanEpsilon perpendicular — the same distance that decides two points
  // are the same site. A vertex further off than that is ordinary surrounding ground: the breakline
  // must still be forced straight through as ONE edge, with that vertex's edges flipped out of the
  // way, rather than the breakline bending to visit it.
  std::vector<TinInputPoint> pts = {
      {0, 0, 10.f}, {10, 0, 10.f},
      {5, 0.5, 12.f},  // 0.5 off the segment — fifty times kTinPlanEpsilon
      {0, 10, 20.f}, {10, 10, 20.f}, {5, -10, 0.f},
  };
  std::vector<TinConstraint> cons(1);
  cons[0].ax = 0;  cons[0].ay = 0; cons[0].az = 10.f;
  cons[0].bx = 10; cons[0].by = 0; cons[0].bz = 10.f;

  const TinBuildResult r = BuildTin(pts, cons);
  REQUIRE(r.ok());
  CHECK(r.constraintsUnresolved == 0);
  const std::uint32_t a = FindVertexIndex(r, 0, 0);
  const std::uint32_t b = FindVertexIndex(r, 10, 0);
  REQUIRE(a != 0xFFFFFFFFu);
  REQUIRE(b != 0xFFFFFFFFu);
  CHECK(HasEdge(r, a, b));  // one straight edge, not a chain via (5,0.5)
  CHECK(NoTriangleEdgeCrosses(r, a, b, 0, 0, 10, 0));
}
