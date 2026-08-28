// REQ-073 surface-to-surface volumes (TASK-095). Pure spatial-index + grid-sample integrator: the
// sign convention (ASSUMPTION-3) and the bbox-disjoint early-out are both invisible on a single hand
// example and wrong on exactly the cases a real grading comparison needs — no overlap, self vs self,
// partial overlap.

#include "util/surfacevolume.hpp"
#include "util/tinbuild.hpp"
#include "util/benchscene.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <chrono>
#include <vector>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

/// A flat, axis-aligned rectangle [x0,x1] x [y0,y1] at constant elevation \p z, as two CCW triangles
/// — full control over the exact hand-computable area/volume the acceptance conditions ask for.
struct FlatRect {
  std::vector<float> vertsXyz;
  std::vector<std::uint32_t> indices;
};

FlatRect MakeFlatRect(double x0, double y0, double x1, double y1, float z) {
  FlatRect r;
  r.vertsXyz = {
      static_cast<float>(x0), static_cast<float>(y0), z,
      static_cast<float>(x1), static_cast<float>(y0), z,
      static_cast<float>(x1), static_cast<float>(y1), z,
      static_cast<float>(x0), static_cast<float>(y1), z,
  };
  r.indices = {0, 1, 2, 0, 2, 3};
  return r;
}

} // namespace

TEST_CASE("Planar surfaces offset by a known constant report the hand-computed volume", "[volume]") {
  // Base at z=110, Comparison at z=100, both over [0,100]x[0,100]: Base sits 10 ft above Comparison
  // everywhere, so cut = 10 * 10,000 ft^2 = 100,000 ft^3 exactly, fill = 0, net = -100,000.
  const FlatRect base = MakeFlatRect(0, 0, 100, 100, 110.f);
  const FlatRect comp = MakeFlatRect(0, 0, 100, 100, 100.f);

  const SurfaceVolumeResult r =
      ComputeSurfaceVolume(base.vertsXyz, base.indices, comp.vertsXyz, comp.indices);

  REQUIRE(r.overlapped);
  CHECK_THAT(r.commonAreaFt2, WithinRel(10000.0, 1e-6));
  CHECK_THAT(r.cutFt3, WithinRel(100000.0, 1e-6));
  CHECK_THAT(r.fillFt3, WithinAbs(0.0, 1e-6));
  CHECK_THAT(r.netFt3, WithinRel(-100000.0, 1e-6));
}

TEST_CASE("The sign convention is Base-above-Comparison is CUT, the reverse is FILL", "[volume]") {
  // ASSUMPTION-3 (TASK-095): reversing which surface is on top reverses which bucket the volume
  // lands in, not just the sign of net — cut and fill are separately reported quantities.
  const FlatRect base = MakeFlatRect(0, 0, 50, 50, 50.f);
  const FlatRect comp = MakeFlatRect(0, 0, 50, 50, 60.f);  // Comparison ABOVE Base this time

  const SurfaceVolumeResult r =
      ComputeSurfaceVolume(base.vertsXyz, base.indices, comp.vertsXyz, comp.indices);

  REQUIRE(r.overlapped);
  const double expectedVol = 10.0 * (50.0 * 50.0);  // 25,000 ft^3
  CHECK_THAT(r.fillFt3, WithinRel(expectedVol, 1e-6));
  CHECK_THAT(r.cutFt3, WithinAbs(0.0, 1e-6));
  CHECK_THAT(r.netFt3, WithinRel(expectedVol, 1e-6));
}

TEST_CASE("The cut/fill map emits triangles only on its own side, and none for a self-comparison",
         "[volume]") {
  // Base above Comparison everywhere: the map must be ALL cut, NO fill triangles — the geometry
  // twin of the "sign convention" test above, checked on the OPTIONAL output rather than the totals.
  const FlatRect base = MakeFlatRect(0, 0, 20, 20, 60.f);
  const FlatRect comp = MakeFlatRect(0, 0, 20, 20, 50.f);

  std::vector<float> cutTris, fillTris;
  const SurfaceVolumeResult r = ComputeSurfaceVolume(base.vertsXyz, base.indices, comp.vertsXyz,
                                                      comp.indices, &cutTris, &fillTris);
  REQUIRE(r.overlapped);
  CHECK(cutTris.empty() == false);
  CHECK(fillTris.empty());
  REQUIRE(cutTris.size() % 9 == 0);  // whole triangles: 9 floats (3 verts x xyz) each

  // Every emitted cut vertex is at the BASE surface's own elevation (60), and lies within the
  // rectangle's own extent — REQ-073's "the cut/fill map... shows nothing outside the common area".
  for (size_t i = 0; i < cutTris.size(); i += 3) {
    CHECK(cutTris[i] >= 0.f);
    CHECK(cutTris[i] <= 20.f);
    CHECK(cutTris[i + 1] >= 0.f);
    CHECK(cutTris[i + 1] <= 20.f);
    CHECK_THAT(static_cast<double>(cutTris[i + 2]), WithinAbs(60.0, 1e-4));
  }

  // A caller that does not ask for the map gets none, at no extra cost — the two-argument overload
  // used everywhere else in this file must still work unchanged.
  const SurfaceVolumeResult r2 =
      ComputeSurfaceVolume(base.vertsXyz, base.indices, comp.vertsXyz, comp.indices);
  CHECK_THAT(r2.cutFt3, WithinRel(r.cutFt3, 1e-9));

  // Self-comparison: diff is exactly 0 everywhere, so NEITHER bucket gets a triangle (TASK-095 §6:
  // "diff > 0.0" / "diff < 0.0" are both false at diff == 0.0 — no cell is ambiguously double-mapped).
  std::vector<float> selfCut, selfFill;
  const SurfaceVolumeResult selfR =
      ComputeSurfaceVolume(base.vertsXyz, base.indices, base.vertsXyz, base.indices, &selfCut, &selfFill);
  REQUIRE(selfR.overlapped);
  CHECK(selfCut.empty());
  CHECK(selfFill.empty());
}

TEST_CASE("Surfaces with no common extent report zero volume and say so", "[volume]") {
  const FlatRect base = MakeFlatRect(0, 0, 10, 10, 100.f);
  const FlatRect comp = MakeFlatRect(1000, 1000, 1010, 1010, 50.f);

  const SurfaceVolumeResult r =
      ComputeSurfaceVolume(base.vertsXyz, base.indices, comp.vertsXyz, comp.indices);

  CHECK_FALSE(r.overlapped);
  CHECK(r.cutFt3 == 0.0);
  CHECK(r.fillFt3 == 0.0);
  CHECK(r.netFt3 == 0.0);
  CHECK(r.commonAreaFt2 == 0.0);
}

TEST_CASE("Partial overlap reports volume and area over the overlap only", "[volume]") {
  // Base [0,100]x[0,100] @110, Comparison [50,150]x[50,150] @100. Their common footprint is exactly
  // [50,100]x[50,100] = 2,500 ft^2, so cut = 10 * 2,500 = 25,000 ft^3 — NOT the 100,000 the full
  // Base rectangle would give, which is exactly the defect a bbox-only (not per-sample-covered)
  // common-area accounting would produce.
  const FlatRect base = MakeFlatRect(0, 0, 100, 100, 110.f);
  const FlatRect comp = MakeFlatRect(50, 50, 150, 150, 100.f);

  const SurfaceVolumeResult r =
      ComputeSurfaceVolume(base.vertsXyz, base.indices, comp.vertsXyz, comp.indices);

  REQUIRE(r.overlapped);
  CHECK_THAT(r.commonAreaFt2, WithinRel(2500.0, 1e-6));
  CHECK_THAT(r.cutFt3, WithinRel(25000.0, 1e-6));
  CHECK_THAT(r.fillFt3, WithinAbs(0.0, 1e-6));
}

TEST_CASE("Comparing a surface with itself reports zero net within tolerance", "[volume]") {
  const FlatRect s = MakeFlatRect(0, 0, 73, 41, 103.5f);  // an irregular size on purpose

  const SurfaceVolumeResult r = ComputeSurfaceVolume(s.vertsXyz, s.indices, s.vertsXyz, s.indices);

  REQUIRE(r.overlapped);
  // Exactly zero, not merely "close": both sides sample the identical float data at the identical
  // points, so there is no discretisation error to be within a tolerance OF.
  CHECK(r.cutFt3 == 0.0);
  CHECK(r.fillFt3 == 0.0);
  CHECK(r.netFt3 == 0.0);
}

TEST_CASE("A degenerate (too-few-triangle) input reports no overlap rather than crashing", "[volume]") {
  const std::vector<float> empty;
  const std::vector<std::uint32_t> emptyIdx;
  const FlatRect base = MakeFlatRect(0, 0, 10, 10, 100.f);

  const SurfaceVolumeResult r = ComputeSurfaceVolume(base.vertsXyz, base.indices, empty, emptyIdx);
  CHECK_FALSE(r.overlapped);
  CHECK(r.commonAreaFt2 == 0.0);
}

// ---------------------------------------------------------------- spatial index

TEST_CASE("The indexed elevation query agrees with the full-scan query everywhere", "[volume][tin]") {
  // A small, irregular (non-axis-aligned-friendly) triangulation so the index has more than one
  // occupied cell and a query can plausibly land in the wrong one if the bucketing were wrong.
  const std::vector<float> verts = {
      0.f, 0.f, 10.f,     10.f, 0.f, 12.f,   20.f, 0.f, 9.f,
      0.f, 10.f, 11.f,    10.f, 10.f, 14.f,  20.f, 10.f, 8.f,
  };
  const std::vector<std::uint32_t> idx = {
      0, 1, 4,  0, 4, 3,
      1, 2, 5,  1, 5, 4,
  };

  const TinSpatialIndex spIdx = BuildTinSpatialIndex(verts, idx);
  REQUIRE_FALSE(spIdx.empty());

  const double samples[][2] = {{5.0, 5.0}, {15.0, 5.0}, {2.0, 8.0}, {18.0, 2.0}, {10.0, 5.0}};
  for (const auto& s : samples) {
    double zFull = 0.0, zIdx = 0.0;
    const bool okFull = TinElevationAt(verts, idx, s[0], s[1], &zFull);
    const bool okIdx = TinElevationAtIndexed(verts, idx, spIdx, s[0], s[1], &zIdx);
    REQUIRE(okFull == okIdx);
    if (okFull)
      CHECK_THAT(zIdx, WithinAbs(zFull, 1e-9));
  }

  // Well outside the triangulation's extent: both must agree it is off the surface.
  double zOut = 0.0;
  CHECK_FALSE(TinElevationAtIndexed(verts, idx, spIdx, 1000.0, 1000.0, &zOut));
}

TEST_CASE("An empty triangulation yields an empty spatial index, not a crash", "[volume][tin]") {
  const std::vector<float> empty;
  const std::vector<std::uint32_t> emptyIdx;
  const TinSpatialIndex spIdx = BuildTinSpatialIndex(empty, emptyIdx);
  CHECK(spIdx.empty());
  double z = 0.0;
  CHECK_FALSE(TinElevationAtIndexed(empty, emptyIdx, spIdx, 0.0, 0.0, &z));
}

TEST_CASE("A 5 ft pad clipped to 66x66 ft reports 21780 ft3 within 1 percent", "[volume][req131]") {
  // Acceptance names 21,780 ft3 (806.67 yd3) = 5 ft x 4,356 ft2 (66 x 66), not 1 acre x 5 ft.
  const FlatRect base = MakeFlatRect(0, 0, 200, 200, 105.f);
  const FlatRect comp = MakeFlatRect(0, 0, 200, 200, 100.f);
  const std::vector<std::pair<double, double>> clip{{0.0, 0.0}, {66.0, 0.0}, {66.0, 66.0}, {0.0, 66.0}};

  const SurfaceVolumeResult r = ComputeSurfaceVolume(base.vertsXyz, base.indices, comp.vertsXyz,
                                                     comp.indices, nullptr, nullptr, &clip);

  REQUIRE(r.overlapped);
  CHECK_THAT(r.cutFt3, WithinRel(21780.0, 0.01));
  CHECK_THAT(r.fillFt3, WithinAbs(0.0, 1.0));
  CHECK_THAT(r.commonAreaFt2, WithinRel(4356.0, 0.01));
}

TEST_CASE("A clip that misses both surfaces reports no overlap", "[volume][req131]") {
  const FlatRect base = MakeFlatRect(0, 0, 50, 50, 10.f);
  const FlatRect comp = MakeFlatRect(0, 0, 50, 50, 0.f);
  const std::vector<std::pair<double, double>> clip{{1000.0, 1000.0}, {1010.0, 1000.0}, {1010.0, 1010.0},
                                                    {1000.0, 1010.0}};

  const SurfaceVolumeResult r = ComputeSurfaceVolume(base.vertsXyz, base.indices, comp.vertsXyz,
                                                     comp.indices, nullptr, nullptr, &clip);
  CHECK_FALSE(r.overlapped);
  CHECK_THAT(r.cutFt3, WithinAbs(0.0, 1e-12));
  CHECK_THAT(r.fillFt3, WithinAbs(0.0, 1e-12));
}

TEST_CASE("Omitting the clip preserves full-overlap volume", "[volume][req131]") {
  const FlatRect base = MakeFlatRect(0, 0, 100, 100, 110.f);
  const FlatRect comp = MakeFlatRect(0, 0, 100, 100, 100.f);
  const SurfaceVolumeResult full =
      ComputeSurfaceVolume(base.vertsXyz, base.indices, comp.vertsXyz, comp.indices);
  const SurfaceVolumeResult also =
      ComputeSurfaceVolume(base.vertsXyz, base.indices, comp.vertsXyz, comp.indices, nullptr, nullptr,
                           nullptr);
  REQUIRE(full.overlapped);
  CHECK_THAT(also.cutFt3, WithinAbs(full.cutFt3, 1e-9));
  CHECK_THAT(also.commonAreaFt2, WithinAbs(full.commonAreaFt2, 1e-9));
}

// ---------------------------------------------------------------- performance guard

TEST_CASE("Volume computation over a REQ-100-density surface completes in near-linear time", "[volume][perf]") {
  // A complexity guard, not a benchmark (the same shape TinBuildTests's own perf case uses): if the
  // spatial index were ever bypassed or built wrong, this degrades from ~O(samples) to
  // O(samples * triangles) — at 250,000 samples x ~200,000 triangles that is not a slow test, it is
  // one that never finishes. Self-comparison is used so the expected volume (net 0) is still checked
  // even in a stress case, rather than timing something whose correctness this test does not verify.
  std::vector<float> ptsXyz;
  const int n = benchscene::BuildSurfacePointScene(100000, &ptsXyz);
  std::vector<TinInputPoint> pts;
  pts.reserve(static_cast<size_t>(n));
  for (int i = 0; i < n; ++i)
    pts.push_back({static_cast<double>(ptsXyz[static_cast<size_t>(i) * 3 + 0]),
                   static_cast<double>(ptsXyz[static_cast<size_t>(i) * 3 + 1]),
                   ptsXyz[static_cast<size_t>(i) * 3 + 2]});
  const TinBuildResult tr = BuildTin(pts);
  REQUIRE(tr.ok());
  REQUIRE(tr.triangleCount() > 150000);

  const auto t0 = std::chrono::steady_clock::now();
  const SurfaceVolumeResult r = ComputeSurfaceVolume(tr.vertsXyz, tr.indices, tr.vertsXyz, tr.indices);
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();

  REQUIRE(r.overlapped);
  CHECK(r.netFt3 == 0.0);
  // Generously loose (measure-and-record territory, not a tuned budget): REQ-100's own frame budget
  // does not apply here (this runs off the UI thread), so the bound only needs to catch a return to
  // quadratic behaviour, not to prove interactivity.
  CHECK(ms < 5000.0);
}
