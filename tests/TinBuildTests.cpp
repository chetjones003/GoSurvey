// Delaunay triangulation for TIN surfaces (REQ-068 / ADR-028).
//
// The predicates are pinned directly, not only through the triangles they produce. A sign error in
// TinOrient2D or TinInCircle is invisible on most inputs and shows up as one bad face on a real
// survey — exactly the class of bug that "it looked right on screen" does not catch.
//
// The structural properties (CCW winding, empty circumcircle, Euler triangle count) matter more than
// any single hand-computed triangulation: they hold for EVERY valid Delaunay result, so they catch
// wrong output on inputs nobody thought to enumerate.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

#include "commands/CadEntities.hpp"
#include "util/tinbuild.hpp"

namespace {

std::vector<TinInputPoint> Pts(std::initializer_list<TinInputPoint> l) { return l; }

/// Every triangle counter-clockwise? The renderer and any later normal calculation depend on it.
bool AllCounterClockwise(const TinBuildResult& r) {
  for (size_t i = 0; i + 2 < r.indices.size(); i += 3) {
    const auto a = r.indices[i], b = r.indices[i + 1], c = r.indices[i + 2];
    const double o = TinOrient2D(r.vertsXyz[a * 3], r.vertsXyz[a * 3 + 1], r.vertsXyz[b * 3],
                                 r.vertsXyz[b * 3 + 1], r.vertsXyz[c * 3], r.vertsXyz[c * 3 + 1]);
    if (o <= 0.0)
      return false;
  }
  return true;
}

/// The defining Delaunay property: no vertex lies inside any triangle's circumcircle.
bool DelaunayPropertyHolds(const TinBuildResult& r) {
  const int nv = r.vertexCount();
  for (size_t i = 0; i + 2 < r.indices.size(); i += 3) {
    const auto a = r.indices[i], b = r.indices[i + 1], c = r.indices[i + 2];
    for (int v = 0; v < nv; ++v) {
      const auto uv = static_cast<std::uint32_t>(v);
      if (uv == a || uv == b || uv == c)
        continue;
      const double d = TinInCircle(r.vertsXyz[a * 3], r.vertsXyz[a * 3 + 1], r.vertsXyz[b * 3],
                                   r.vertsXyz[b * 3 + 1], r.vertsXyz[c * 3], r.vertsXyz[c * 3 + 1],
                                   r.vertsXyz[uv * 3], r.vertsXyz[uv * 3 + 1]);
      if (d > 1e-6)  // tolerance: cocircular points are legitimately ~0
        return false;
    }
  }
  return true;
}

} // namespace

// ---------------------------------------------------------------- predicates

TEST_CASE("Orient2D reports left, right and collinear", "[tin]") {
  REQUIRE(TinOrient2D(0, 0, 1, 0, 0, 1) > 0);   // (0,1) is left of +X → counter-clockwise
  REQUIRE(TinOrient2D(0, 0, 1, 0, 0, -1) < 0);  // right → clockwise
  REQUIRE(TinOrient2D(0, 0, 1, 0, 2, 0) == 0);  // collinear
}

TEST_CASE("InCircle reports inside, outside and on the circle", "[tin]") {
  // Unit circle through (±1,0) and (0,1), taken counter-clockwise.
  const double ax = -1, ay = 0, bx = 1, by = 0, cx = 0, cy = 1;
  REQUIRE(TinInCircle(ax, ay, bx, by, cx, cy, 0.0, 0.0) > 0);    // centre — inside
  REQUIRE(TinInCircle(ax, ay, bx, by, cx, cy, 0.0, 5.0) < 0);    // far above — outside
  REQUIRE(TinInCircle(ax, ay, bx, by, cx, cy, 0.0, -1.0) == 0);  // on the circle
}

TEST_CASE("Predicates stay correct at state-plane magnitudes", "[tin][req101]") {
  // The reason ADR-028 (d) computes in double over float storage. At 6-figure easting/northing a
  // float determinant loses the sign entirely; this must not.
  const double ox = 2145678.0, oy = 745678.0;
  REQUIRE(TinOrient2D(ox, oy, ox + 100.0, oy, ox, oy + 100.0) > 0);
  // A point 0.02 ft off the line — twice REQ-101's tolerance — must still resolve as off it.
  REQUIRE(TinOrient2D(ox, oy, ox + 100.0, oy, ox + 50.0, oy + 0.02) > 0);
  REQUIRE(TinOrient2D(ox, oy, ox + 100.0, oy, ox + 50.0, oy - 0.02) < 0);
}

// ---------------------------------------------------------------- happy path

TEST_CASE("A unit square triangulates into two triangles covering it", "[tin]") {
  const TinBuildResult r = BuildTin(Pts({{0, 0, 10.f}, {10, 0, 11.f}, {10, 10, 12.f}, {0, 10, 13.f}}));
  REQUIRE(r.ok());
  REQUIRE(r.vertexCount() == 4);
  REQUIRE(r.triangleCount() == 2);  // a convex quad is exactly two triangles
  REQUIRE(AllCounterClockwise(r));
  REQUIRE(DelaunayPropertyHolds(r));
}

TEST_CASE("Vertex elevations are carried through untouched", "[tin][req101]") {
  const TinBuildResult r = BuildTin(Pts({{0, 0, 100.25f}, {10, 0, 101.5f}, {5, 10, 99.75f}}));
  REQUIRE(r.ok());
  REQUIRE(r.triangleCount() == 1);
  // Z is data, not geometry the triangulator may adjust: exact equality is the right assertion.
  std::vector<float> zs = {r.vertsXyz[2], r.vertsXyz[5], r.vertsXyz[8]};
  std::sort(zs.begin(), zs.end());
  REQUIRE(zs[0] == 99.75f);
  REQUIRE(zs[1] == 100.25f);
  REQUIRE(zs[2] == 101.5f);
}

TEST_CASE("A grid triangulates to the Euler-predicted triangle count", "[tin]") {
  // For a point set in general position with h hull points, a Delaunay triangulation has exactly
  // 2n - h - 2 triangles. A 5x5 grid: n = 25, hull = 16 → 32.
  std::vector<TinInputPoint> pts;
  for (int i = 0; i < 5; ++i)
    for (int j = 0; j < 5; ++j)
      pts.push_back({static_cast<double>(i) * 10.0, static_cast<double>(j) * 10.0,
                     static_cast<float>(i + j)});
  const TinBuildResult r = BuildTin(pts);
  REQUIRE(r.ok());
  REQUIRE(r.vertexCount() == 25);
  REQUIRE(r.triangleCount() == 2 * 25 - 16 - 2);
  REQUIRE(AllCounterClockwise(r));
  REQUIRE(DelaunayPropertyHolds(r));
}

TEST_CASE("A scattered set satisfies the Delaunay property", "[tin]") {
  // Deterministic pseudo-random scatter — no <random>, so the case is identical on every platform
  // and a failure is reproducible.
  std::vector<TinInputPoint> pts;
  std::uint32_t seed = 12345u;
  auto next = [&seed]() {
    seed = seed * 1664525u + 1013904223u;
    return static_cast<double>((seed >> 8) & 0xFFFF) / 65535.0;
  };
  for (int i = 0; i < 120; ++i)
    pts.push_back({next() * 500.0, next() * 500.0, static_cast<float>(next() * 20.0)});

  const TinBuildResult r = BuildTin(pts);
  REQUIRE(r.ok());
  REQUIRE(r.triangleCount() > 100);
  REQUIRE(AllCounterClockwise(r));
  REQUIRE(DelaunayPropertyHolds(r));
}

TEST_CASE("Triangulation works at state-plane coordinates", "[tin][req101]") {
  const double ox = 2145678.0, oy = 745678.0;
  const TinBuildResult r = BuildTin(Pts({{ox, oy, 100.f},
                                         {ox + 100, oy, 101.f},
                                         {ox + 100, oy + 100, 102.f},
                                         {ox, oy + 100, 103.f}}));
  REQUIRE(r.ok());
  REQUIRE(r.triangleCount() == 2);
  REQUIRE(AllCounterClockwise(r));
}

TEST_CASE("A large point set triangulates in near-linear time", "[tin][perf]") {
  // A complexity guard, not a benchmark. The first implementation scanned every triangle per
  // inserted point — correct, and O(n²): 16k points took 1.8 s and 100k extrapolated to ~70 s,
  // which cannot build the surface REQ-100 specifies. Adjacency + a walk made 100k points 89 ms.
  //
  // The bound is deliberately ~100x looser than the real figure so a slow or loaded machine cannot
  // make it flaky, while a return to quadratic behaviour (which would be ~18 s here) still fails.
  std::vector<TinInputPoint> pts;
  std::uint32_t seed = 99u;
  auto next = [&seed]() {
    seed = seed * 1664525u + 1013904223u;
    return static_cast<double>((seed >> 8) & 0xFFFF) / 65535.0;
  };
  constexpr int kN = 50000;
  pts.reserve(kN);
  for (int i = 0; i < kN; ++i)
    pts.push_back({next() * 5000.0, next() * 5000.0, static_cast<float>(next() * 50.0)});

  const auto t0 = std::chrono::steady_clock::now();
  const TinBuildResult r = BuildTin(pts);
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();

  REQUIRE(r.ok());
  REQUIRE(r.triangleCount() > kN);  // ~2n for a set in general position
  REQUIRE(ms < 5000.0);
}

// ---------------------------------------------------------------- undo sharing

TEST_CASE("Copying a surface shares its triangulation instead of duplicating it", "[tin][undo]") {
  // REQ-068's acceptance: "an edit unrelated to the surface — drawing a line — does not copy the
  // triangulation ... asserted on the shared pointer rather than by inspection".
  //
  // DrawingGeometrySnapshot copies `std::vector<CadSurface>` by value, and 50 undo frames are kept.
  // If CadTin lived in the surface by value, a 200k-triangle surface would cost ~7 MB per frame —
  // ~350 MB of undo stack, re-paid by every unrelated edit, because drawing a line snapshots the
  // whole model. That is exactly the trap TASK-041 hit with meshes; here the shape is asserted.
  auto tin = std::make_shared<CadTin>();
  tin->vertsXyz = {0, 0, 1, 10, 0, 2, 5, 10, 3};
  tin->indices = {0, 1, 2};

  std::vector<CadSurface> live(1);
  live[0].name = "EG";
  live[0].tin = tin;
  const CadTin* payload = live[0].tin.get();
  REQUIRE(tin.use_count() == 2);  // the local handle + the one in `live`

  // What an undo snapshot does: copy the whole vector.
  const std::vector<CadSurface> snapshot = live;
  REQUIRE(tin.use_count() == 3);              // a refcount bump...
  REQUIRE(snapshot[0].tin.get() == payload);  // ...pointing at the SAME triangulation, not a copy
  REQUIRE(snapshot[0].tin->vertsXyz.data() == payload->vertsXyz.data());

  // Fifty frames of unrelated edits: still one payload.
  std::vector<std::vector<CadSurface>> frames;
  for (int i = 0; i < 50; ++i)
    frames.push_back(live);
  REQUIRE(tin.use_count() == 53);
  for (const auto& f : frames)
    REQUIRE(f[0].tin.get() == payload);
}

TEST_CASE("Rebuilding a surface replaces the pointer and leaves snapshots untouched", "[tin][undo]") {
  // The other half of the architecture §11.5 contract: "editing" means replacing the pointer, never
  // writing through it. An undo frame taken before a rebuild must still see the old triangulation.
  std::vector<CadSurface> live(1);
  live[0].tin = std::make_shared<CadTin>();
  const_cast<CadTin&>(*live[0].tin).indices = {0, 1, 2};
  const CadTin* before = live[0].tin.get();

  const std::vector<CadSurface> snapshot = live;

  auto rebuilt = std::make_shared<CadTin>();
  rebuilt->indices = {0, 1, 2, 0, 2, 3};
  live[0].tin = std::move(rebuilt);  // replace, never write through

  REQUIRE(live[0].tin.get() != before);
  REQUIRE(snapshot[0].tin.get() == before);      // the old frame still holds the old surface
  REQUIRE(snapshot[0].tin->triangleCount() == 1);
  REQUIRE(live[0].tin->triangleCount() == 2);
}

// ---------------------------------------------------------------- failure modes

TEST_CASE("Fewer than three points fails with a specific message and no partial surface", "[tin][req201]") {
  for (const auto& pts : {Pts({}), Pts({{0, 0, 1.f}}), Pts({{0, 0, 1.f}, {10, 0, 2.f}})}) {
    const TinBuildResult r = BuildTin(pts);
    REQUIRE_FALSE(r.ok());
    REQUIRE(r.status == TinBuildStatus::TooFewPoints);
    REQUIRE_FALSE(r.message.empty());
    REQUIRE(r.vertsXyz.empty());  // REQ-001: no partial result
    REQUIRE(r.indices.empty());
  }
}

TEST_CASE("Collinear points fail with a specific message, not an empty success", "[tin][req201]") {
  const TinBuildResult r = BuildTin(Pts({{0, 0, 1.f}, {10, 0, 2.f}, {20, 0, 3.f}, {30, 0, 4.f}}));
  REQUIRE_FALSE(r.ok());
  REQUIRE(r.status == TinBuildStatus::AllCollinear);
  REQUIRE_FALSE(r.message.empty());
  REQUIRE(r.indices.empty());
  REQUIRE(r.vertsXyz.empty());
}

TEST_CASE("Coincident plan positions are dropped, reported, and first-wins", "[tin][req201]") {
  // Two shots at the same spot with different elevations. Delaunay is undefined for coincident
  // sites, so one must go — but silently picking one is exactly the absorbed failure REQ-201 bans.
  const TinBuildResult r = BuildTin(Pts({{0, 0, 100.f},
                                         {0, 0, 999.f},  // duplicate of the first
                                         {10, 0, 101.f},
                                         {5, 10, 102.f}}));
  REQUIRE(r.ok());
  REQUIRE(r.duplicatesDropped == 1);
  REQUIRE_FALSE(r.message.empty());
  REQUIRE(r.vertexCount() == 3);
  // First occurrence wins: 100 survives, 999 does not appear anywhere.
  bool has100 = false, has999 = false;
  for (int i = 0; i < r.vertexCount(); ++i) {
    if (r.vertsXyz[i * 3 + 2] == 100.f) has100 = true;
    if (r.vertsXyz[i * 3 + 2] == 999.f) has999 = true;
  }
  REQUIRE(has100);
  REQUIRE_FALSE(has999);
}

TEST_CASE("Near-coincident points inside REQ-101 tolerance count as one site", "[tin][req101]") {
  // Closer than 0.01 ft in plan is indistinguishable at the tolerance the rest of the system works
  // to, so feeding both to Delaunay would be feeding it a duplicate.
  const TinBuildResult r = BuildTin(Pts({{0, 0, 100.f},
                                         {0.005, 0.005, 200.f},  // within kTinPlanEpsilon
                                         {10, 0, 101.f},
                                         {5, 10, 102.f}}));
  REQUIRE(r.ok());
  REQUIRE(r.duplicatesDropped == 1);
  REQUIRE(r.vertexCount() == 3);
}

TEST_CASE("Points just outside the tolerance are kept as distinct sites", "[tin][req101]") {
  const TinBuildResult r = BuildTin(Pts({{0, 0, 100.f},
                                         {0.5, 0.02, 200.f},  // well outside kTinPlanEpsilon
                                         {10, 0, 101.f},
                                         {5, 10, 102.f}}));
  REQUIRE(r.ok());
  REQUIRE(r.duplicatesDropped == 0);
  REQUIRE(r.vertexCount() == 4);
}

TEST_CASE("A duplicate-heavy set that collapses below three sites fails cleanly", "[tin][req201]") {
  const TinBuildResult r = BuildTin(Pts({{0, 0, 1.f}, {0, 0, 2.f}, {0, 0, 3.f}, {10, 0, 4.f}}));
  REQUIRE_FALSE(r.ok());
  REQUIRE(r.status == TinBuildStatus::TooFewPoints);
  REQUIRE(r.indices.empty());
}
