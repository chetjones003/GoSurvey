// REQ-070 — contour generation over a TIN (`util/contourgen`, ADR-036 (f)).
//
// A wrong contour is never a crash. It is a plan that looks plausible and states the wrong ground:
// a line at 102 drawn where 104 belongs, a contour broken in half across a shared edge, a closed
// ring reported open. Every one of those reads as a triangulation defect and is not one, so every
// expectation below is HAND-COMPUTED from the plane the vertices describe — never read back from
// the generator's own output.
//
// Three things are load-bearing and each gets its own case:
//   * levels are multiples of the interval measured from elevation ZERO, so a contour does not move
//     when one low shot joins the surface;
//   * chaining is topological — two triangles sharing an edge yield ONE contour, not two segments;
//   * the ASSUMPTION-1 tie rule (a vertex exactly at a level counts as above, uniformly) is what
//     keeps a contour continuous where it runs through a vertex instead of half-open.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include "util/contourgen.hpp"

namespace {

/// REQ-101's tolerance, in feet. Contour vertices are an interpolation along a triangle edge, so
/// this is the band every asserted coordinate is checked inside.
constexpr double kTolFt = 0.01;

/// Contour \p i as (x,y,z) triples, so a case reads as geometry rather than as offset arithmetic.
std::vector<std::array<double, 3>> ContourAt(const ContourResult& r, int i) {
  std::vector<std::array<double, 3>> pts;
  for (int v = r.offsets[static_cast<size_t>(i)]; v < r.offsets[static_cast<size_t>(i) + 1]; ++v)
    pts.push_back({static_cast<double>(r.vertsXyz[static_cast<size_t>(v) * 3]),
                   static_cast<double>(r.vertsXyz[static_cast<size_t>(v) * 3 + 1]),
                   static_cast<double>(r.vertsXyz[static_cast<size_t>(v) * 3 + 2])});
  return pts;
}

bool NearFt(double got, double want) { return std::abs(got - want) <= kTolFt; }

/// The square [0,10]² as two triangles, with each corner's Z supplied — the smallest mesh that has
/// an interior shared edge, which is where chaining either works or does not.
struct Square {
  std::vector<float> verts;
  std::vector<std::uint32_t> tris;
};

Square MakeSquare(double z00, double z10, double z11, double z01) {
  Square s;
  s.verts = {0.f,  0.f,  static_cast<float>(z00), 10.f, 0.f,  static_cast<float>(z10),
             10.f, 10.f, static_cast<float>(z11), 0.f,  10.f, static_cast<float>(z01)};
  s.tris = {0, 1, 2, 0, 2, 3};
  return s;
}

} // namespace

// ---------------------------------------------------------------------------------------------
// ContourLevels — which elevations a contour interval means
// ---------------------------------------------------------------------------------------------

TEST_CASE("Levels are whole multiples of the interval measured from zero",
          "[surface][req070][contour]") {
  // The point of the case: 101..109 at a 2 ft interval must give 102,104,106,108 — the elevations a
  // plan reader expects — and NOT 101,103,105,107,109, which is what measuring from the surface's
  // own low point produces. Measuring from the low point also moves every contour on the sheet the
  // day a single lower shot is added, which is the defect this rule exists to prevent.
  std::vector<double> levels;
  ContourLevels(101.0, 109.0, 2.0, &levels);

  REQUIRE(levels.size() == 4);
  CHECK(levels[0] == Catch::Approx(102.0));
  CHECK(levels[1] == Catch::Approx(104.0));
  CHECK(levels[2] == Catch::Approx(106.0));
  CHECK(levels[3] == Catch::Approx(108.0));
}

TEST_CASE("A level exactly on the range's endpoints is included", "[surface][req070][contour]") {
  std::vector<double> levels;
  ContourLevels(100.0, 110.0, 5.0, &levels);

  REQUIRE(levels.size() == 3);
  CHECK(levels[0] == Catch::Approx(100.0));
  CHECK(levels[1] == Catch::Approx(105.0));
  CHECK(levels[2] == Catch::Approx(110.0));
}

TEST_CASE("A range holding no multiple of the interval yields no levels",
          "[surface][req070][contour]") {
  // A 2 ft interval over a surface sitting entirely between two contours. Zero levels is the
  // correct answer, not an error and not a nearest-contour consolation.
  std::vector<double> levels;
  ContourLevels(100.2, 101.4, 2.0, &levels);
  CHECK(levels.empty());
}

TEST_CASE("A non-positive or non-finite interval yields no levels", "[surface][req070][contour]") {
  // The caller owns REQ-070's rejection message, because the caller is where the value was typed.
  // Here there is simply nothing to generate — and in particular no hang and no infinite list.
  std::vector<double> levels{1.0, 2.0};  // Seeded, to prove the clear() happens.

  ContourLevels(0.0, 100.0, 0.0, &levels);
  CHECK(levels.empty());

  levels = {1.0};
  ContourLevels(0.0, 100.0, -2.0, &levels);
  CHECK(levels.empty());

  levels = {1.0};
  ContourLevels(0.0, 100.0, std::numeric_limits<double>::quiet_NaN(), &levels);
  CHECK(levels.empty());

  levels = {1.0};
  ContourLevels(0.0, 100.0, std::numeric_limits<double>::infinity(), &levels);
  CHECK(levels.empty());

  levels = {1.0};
  ContourLevels(std::numeric_limits<double>::quiet_NaN(), 100.0, 2.0, &levels);
  CHECK(levels.empty());
}

TEST_CASE("An inverted range yields no levels", "[surface][req070][contour]") {
  std::vector<double> levels;
  ContourLevels(110.0, 100.0, 2.0, &levels);
  CHECK(levels.empty());
}

TEST_CASE("ContourLevels tolerates a null out", "[surface][req070][contour]") {
  ContourLevels(0.0, 10.0, 1.0, nullptr);  // Must not crash; nothing else to assert.
  SUCCEED();
}

// ---------------------------------------------------------------------------------------------
// GenerateContours — the geometry
// ---------------------------------------------------------------------------------------------

TEST_CASE("A tilted plane's contours land where the plane says they do",
          "[surface][req070][contour]") {
  // One triangle on the plane z = y: A(0,0,0), B(10,0,0), C(0,10,10). At level L the contour is the
  // line y = L, running from the AC edge at (0,L) to the BC edge at (10-L, L) — both hand-computed
  // from the edge interpolation, not from the generator.
  const std::vector<float> verts{0.f, 0.f, 0.f, 10.f, 0.f, 0.f, 0.f, 10.f, 10.f};
  const std::vector<std::uint32_t> tris{0, 1, 2};
  const std::vector<double> levels{2.0, 4.0, 6.0, 8.0};

  ContourResult r;
  GenerateContours(verts, tris, levels, &r);

  REQUIRE(r.contourCount() == 4);
  for (int i = 0; i < 4; ++i) {
    const double L = 2.0 * (i + 1);
    const auto pts = ContourAt(r, i);

    INFO("level " << L);
    CHECK(r.levels[static_cast<size_t>(i)] == Catch::Approx(L));
    // A contour that stops at the triangle's border is open. Reporting it closed would make the
    // renderer join its two ends straight across the surface.
    CHECK(r.closed[static_cast<size_t>(i)] == 0);

    REQUIRE(pts.size() == 2);
    CHECK(NearFt(pts[0][0], 0.0));
    CHECK(NearFt(pts[0][1], L));
    CHECK(NearFt(pts[1][0], 10.0 - L));
    CHECK(NearFt(pts[1][1], L));
    // Z is the level itself — a contour vertex is AT its elevation by definition, and one that
    // merely interpolated to within rounding of it would label wrong at the boundary.
    CHECK(pts[0][2] == Catch::Approx(L));
    CHECK(pts[1][2] == Catch::Approx(L));
  }
}

TEST_CASE("Two triangles sharing an edge produce one contour, not two segments",
          "[surface][req070][contour]") {
  // The square z = y, split on the 0-2 diagonal. The level-5 contour crosses that diagonal, so the
  // two triangles each contribute a segment and the two MUST come back joined. If chaining ever
  // regresses to comparing endpoint floats, this is the case that breaks: the two crossings agree
  // only to within rounding and then fail to match, leaving a contour visibly split at the diagonal
  // for no reason a user could diagnose.
  const Square sq = MakeSquare(0.0, 0.0, 10.0, 10.0);

  ContourResult r;
  GenerateContours(sq.verts, sq.tris, {5.0}, &r);

  REQUIRE(r.contourCount() == 1);
  const auto pts = ContourAt(r, 0);
  REQUIRE(pts.size() == 3);  // border, diagonal, border

  // Emitted from a dangling end inward, so the run is monotone across the square either way.
  CHECK(NearFt(pts[1][0], 5.0));
  CHECK(NearFt(pts[0][0] + pts[2][0], 10.0));
  CHECK(NearFt(std::abs(pts[0][0] - pts[2][0]), 10.0));
  for (const auto& p : pts) {
    CHECK(NearFt(p[1], 5.0));
    CHECK(p[2] == Catch::Approx(5.0));
  }
  CHECK(r.closed[0] == 0);
}

TEST_CASE("A contour that rings a peak is reported closed", "[surface][req070][contour]") {
  // A pyramid: a 10x10 base at elevation 0 and an apex at (5,5,10). The level-5 contour is the ring
  // of apex-edge midpoints — (2.5,2.5), (7.5,2.5), (7.5,7.5), (2.5,7.5) — and it closes. Four
  // vertices, not five: the closing point IS the start point, and emitting it twice would put a
  // duplicate at the seam of every closed contour on the drawing.
  const std::vector<float> verts{0.f,  0.f, 0.f, 10.f, 0.f,  0.f,  10.f, 10.f,
                                 0.f,  0.f, 10.f, 0.f, 5.f,  5.f,  10.f};
  const std::vector<std::uint32_t> tris{0, 1, 4, 1, 2, 4, 2, 3, 4, 3, 0, 4};

  ContourResult r;
  GenerateContours(verts, tris, {5.0}, &r);

  REQUIRE(r.contourCount() == 1);
  CHECK(r.closed[0] == 1);

  const auto pts = ContourAt(r, 0);
  REQUIRE(pts.size() == 4);

  // Order depends on which triangle the walk starts from, so the ring is asserted as a SET of
  // corners. Pinning the traversal order would be pinning an implementation detail.
  const std::array<std::array<double, 2>, 4> want{
      {{2.5, 2.5}, {7.5, 2.5}, {7.5, 7.5}, {2.5, 7.5}}};
  for (const auto& w : want) {
    int hits = 0;
    for (const auto& p : pts)
      if (NearFt(p[0], w[0]) && NearFt(p[1], w[1]))
        ++hits;
    INFO("corner " << w[0] << "," << w[1]);
    CHECK(hits == 1);
  }
  for (const auto& p : pts)
    CHECK(p[2] == Catch::Approx(5.0));
}

TEST_CASE("A level exactly on a vertex keeps the contour continuous",
          "[surface][req070][contour]") {
  // ASSUMPTION-1. Four triangles fanned around an interior vertex V(5,5,5), with two neighbours
  // below it and two above. The level-5 contour runs straight through V.
  //
  // This is the case the tie rule exists for. Decided per-edge instead of once, the contour passes
  // through V on one triangle and misses it on the neighbour, and what comes out is TWO contours
  // stopping either side of a vertex — a half-open contour that reads as a triangulation bug and is
  // not one. The assertion is therefore on the COUNT (one, not two) and on continuity through V.
  const std::vector<float> verts{0.f,  0.f,  0.f,   // A, below
                                 10.f, 0.f,  0.f,   // B, below
                                 10.f, 10.f, 10.f,  // C, above
                                 0.f,  10.f, 10.f,  // D, above
                                 5.f,  5.f,  5.f};  // V, exactly on the level
  const std::vector<std::uint32_t> tris{0, 1, 4, 1, 2, 4, 2, 3, 4, 3, 0, 4};

  ContourResult r;
  GenerateContours(verts, tris, {5.0}, &r);

  REQUIRE(r.contourCount() == 1);

  const auto pts = ContourAt(r, 0);
  REQUIRE(pts.size() >= 3);

  // It spans the surface: it enters on one border edge at x = 10 and leaves on the other at x = 0.
  CHECK(NearFt(pts.front()[0] + pts.back()[0], 10.0));
  CHECK(NearFt(std::abs(pts.front()[0] - pts.back()[0]), 10.0));
  CHECK(NearFt(pts.front()[1], 5.0));
  CHECK(NearFt(pts.back()[1], 5.0));

  // And it goes through V rather than stopping either side of it.
  int throughV = 0;
  for (const auto& p : pts)
    if (NearFt(p[0], 5.0) && NearFt(p[1], 5.0))
      ++throughV;
  CHECK(throughV >= 1);

  for (const auto& p : pts)
    CHECK(p[2] == Catch::Approx(5.0));
}

TEST_CASE("A saddle yields two contours at the level through it", "[surface][req070][contour]") {
  // Corners (0,0,10), (10,0,0), (10,10,10), (0,10,0) — high on one diagonal, low on the other. The
  // level-5 contour is two separate arcs, and a generator that welded segment ends by proximity
  // rather than by shared edge would return one. Hand-checked: the 0-2 split diagonal has BOTH ends
  // at elevation 10, so it is never crossed at 5, which is exactly why the two arcs cannot join.
  const Square sq = MakeSquare(10.0, 0.0, 10.0, 0.0);

  ContourResult r;
  GenerateContours(sq.verts, sq.tris, {5.0}, &r);

  CHECK(r.contourCount() == 2);
  for (int i = 0; i < r.contourCount(); ++i) {
    CHECK(r.levels[static_cast<size_t>(i)] == Catch::Approx(5.0));
    CHECK(r.closed[static_cast<size_t>(i)] == 0);
    for (const auto& p : ContourAt(r, i))
      CHECK(p[2] == Catch::Approx(5.0));
  }
}

TEST_CASE("Every contour vertex sits on its edge's linear interpolation within REQ-101",
          "[surface][req070][contour][req101]") {
  // The acceptance condition stated as a property rather than as a table: on the plane
  // z = 0.3x + 0.7y + 2, every generated vertex must satisfy that equation to within 0.01 ft. The
  // grid spacing is deliberately un-round, so a bug that happens to be exact on integers still
  // shows up here.
  constexpr int kN = 7;
  std::vector<float> verts;
  for (int j = 0; j < kN; ++j) {
    for (int i = 0; i < kN; ++i) {
      const double x = i * 3.7, y = j * 4.3;
      verts.push_back(static_cast<float>(x));
      verts.push_back(static_cast<float>(y));
      verts.push_back(static_cast<float>(0.3 * x + 0.7 * y + 2.0));
    }
  }
  std::vector<std::uint32_t> tris;
  for (int j = 0; j + 1 < kN; ++j) {
    for (int i = 0; i + 1 < kN; ++i) {
      const std::uint32_t a = static_cast<std::uint32_t>(j * kN + i), b = a + 1;
      const std::uint32_t c = b + kN, d = a + kN;
      tris.insert(tris.end(), {a, b, c, a, c, d});
    }
  }

  std::vector<double> levels;
  ContourLevels(2.0, 0.3 * 3.7 * (kN - 1) + 0.7 * 4.3 * (kN - 1) + 2.0, 2.0, &levels);
  REQUIRE(levels.size() > 3);

  ContourResult r;
  GenerateContours(verts, tris, levels, &r);
  REQUIRE(r.contourCount() > 0);

  for (int c = 0; c < r.contourCount(); ++c) {
    const double L = r.levels[static_cast<size_t>(c)];
    for (const auto& p : ContourAt(r, c)) {
      INFO("contour " << c << " at " << L << " vertex " << p[0] << "," << p[1] << "," << p[2]);
      CHECK(NearFt(0.3 * p[0] + 0.7 * p[1] + 2.0, L));  // on the plane
      CHECK(NearFt(p[2], L));                           // and at its own level
    }
  }
}

TEST_CASE("The caller may list levels in any order", "[surface][req070][contour]") {
  // Normalised internally, so a style that hands over its major and minor levels concatenated —
  // which is exactly what a major/minor pair produces, duplicates and all — gets the same contours
  // in the same order as a caller that sorted them first.
  const Square sq = MakeSquare(0.0, 0.0, 10.0, 10.0);

  ContourResult sortedIn, jumbledIn;
  GenerateContours(sq.verts, sq.tris, {2.0, 4.0, 6.0, 8.0}, &sortedIn);
  GenerateContours(sq.verts, sq.tris, {6.0, 2.0, 8.0, 4.0, 6.0}, &jumbledIn);

  CHECK(sortedIn.contourCount() == jumbledIn.contourCount());
  CHECK(sortedIn.vertsXyz == jumbledIn.vertsXyz);
  CHECK(sortedIn.offsets == jumbledIn.offsets);
  CHECK(sortedIn.levels == jumbledIn.levels);
  CHECK(sortedIn.closed == jumbledIn.closed);
}

TEST_CASE("A flat surface has no contours", "[surface][req070][contour]") {
  // Not an error: a level surface genuinely has no contour lines on it. The interesting half is
  // that the tie rule does not turn the whole plateau into a contour at its own elevation.
  const Square sq = MakeSquare(100.0, 100.0, 100.0, 100.0);

  ContourResult r;
  GenerateContours(sq.verts, sq.tris, {100.0}, &r);
  CHECK(r.contourCount() == 0);
  CHECK(r.vertsXyz.empty());
}

TEST_CASE("Levels outside the surface's range produce nothing", "[surface][req070][contour]") {
  const Square sq = MakeSquare(0.0, 0.0, 10.0, 10.0);

  ContourResult r;
  GenerateContours(sq.verts, sq.tris, {-50.0, 500.0}, &r);
  CHECK(r.contourCount() == 0);
}

TEST_CASE("An empty triangulation or an empty level list produces an empty result",
          "[surface][req070][contour]") {
  // A surface that has never been built has no contours, and that must not be an error — the
  // Surface Manager lists unbuilt surfaces and the display pass runs over them like any other.
  ContourResult r;
  r.vertsXyz = {1.f};  // Seeded, to prove the clear() happens on every path.
  r.offsets = {0, 1};

  GenerateContours({}, {}, {5.0}, &r);
  CHECK(r.contourCount() == 0);
  CHECK(r.vertsXyz.empty());
  CHECK(r.offsets.empty());

  const Square sq = MakeSquare(0.0, 0.0, 10.0, 10.0);
  GenerateContours(sq.verts, sq.tris, {}, &r);
  CHECK(r.contourCount() == 0);
}

TEST_CASE("A malformed index is skipped rather than trusted", "[surface][req070][contour]") {
  // Defensive in the same shape as TinBorderEdges next door: an out-of-range index would read past
  // the vertex array, and a contour generator is not the layer that gets to decide a build was
  // corrupt. The valid triangle beside it still contours.
  const std::vector<float> verts{0.f, 0.f, 0.f, 10.f, 0.f, 0.f, 0.f, 10.f, 10.f};
  const std::vector<std::uint32_t> tris{0, 1, 2, 0, 1, 99};

  ContourResult r;
  GenerateContours(verts, tris, {5.0}, &r);
  REQUIRE(r.contourCount() == 1);
  CHECK(ContourAt(r, 0).size() == 2);
}

TEST_CASE("GenerateContours tolerates a null out", "[surface][req070][contour]") {
  const Square sq = MakeSquare(0.0, 0.0, 10.0, 10.0);
  GenerateContours(sq.verts, sq.tris, {5.0}, nullptr);  // Must not crash.
  SUCCEED();
}
