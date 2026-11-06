// REQ-497 / GitHub issue #121 Phase 5 - ground elevation along an alignment.
// Expected values come from a known plane, Z = 0.1 X + 50, which a bilinear grid reproduces exactly.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "CadCommands.hpp"
#include "alignment/Alignment.hpp"
#include "alignment/AlignmentGround.hpp"
#include "util/surfacequery.hpp"

using alignment::Alignment;
using alignment::GroundPoint;
using Catch::Approx;

namespace {

constexpr double kDeg = alignment::kPi / 180.0;

/// A grid covering X 0..xMax (spacing 10) and Y 0..10, with Z = 0.1 X + 50 at every node.
GridSurfaceQuery PlaneGrid(int cols, std::vector<float>* storage) {
  storage->clear();
  for (int j = 0; j < 2; ++j) {
    for (int i = 0; i < cols; ++i) storage->push_back(static_cast<float>(0.1 * (10.0 * i) + 50.0));
  }
  return GridSurfaceQuery(0.0, 0.0, 10.0, 10.0, cols, 2, *storage);
}

Alignment EastLine(double length) {
  Alignment a;
  a.name = "EAST";
  REQUIRE(a.AppendTangent({0.0, 5.0}, 90.0 * kDeg, length));
  return a;
}

void Submit(AppCommandState& st, const std::string& text, std::vector<std::string>& log) {
  char buf[512];
  std::snprintf(buf, sizeof(buf), "%s", text.c_str());
  ProcessCommandLineSubmit(buf, sizeof(buf), st, log);
}

bool LogHas(const std::vector<std::string>& log, const char* needle) {
  for (const std::string& line : log) {
    if (line.find(needle) != std::string::npos) return true;
  }
  return false;
}

}  // namespace

TEST_CASE("Ground at a station equals the plane value on the centreline and across the offset", "[req497][alignment]") {
  std::vector<float> z;
  const GridSurfaceQuery grid = PlaneGrid(11, &z);  // X 0..100
  const Alignment a = EastLine(100.0);
  const GroundPoint on = alignment::GroundAt(a, grid, 50.0, 0.0);
  REQUIRE(on.hasGround);
  CHECK(on.ground == Approx(55.0).margin(1e-4));  // 0.1 * 50 + 50
  CHECK(on.point.x == Approx(50.0).margin(1e-9));

  // 2 units to the right of an eastbound alignment is south (lower Y); the plane does not depend on Y.
  const GroundPoint off = alignment::GroundAt(a, grid, 50.0, 2.0);
  REQUIRE(off.hasGround);
  CHECK(off.ground == Approx(55.0).margin(1e-4));
  CHECK(off.point.y == Approx(3.0).margin(1e-9));
}

TEST_CASE("A station off the alignment or off the surface has no ground, never zero", "[req497][alignment]") {
  std::vector<float> z;
  const GridSurfaceQuery grid = PlaneGrid(11, &z);  // covers X 0..100 only
  const Alignment a = EastLine(100.0);
  CHECK_FALSE(alignment::GroundAt(a, grid, 150.0, 0.0).hasGround);  // past the end of the alignment
  CHECK_FALSE(alignment::GroundAt(a, grid, -5.0, 0.0).hasGround);   // before the start

  const Alignment longer = EastLine(150.0);  // the alignment runs past the grid's X 100
  CHECK(alignment::GroundAt(longer, grid, 90.0, 0.0).hasGround);
  CHECK_FALSE(alignment::GroundAt(longer, grid, 120.0, 0.0).hasGround);
}

TEST_CASE("Sampling every 10 units over 2500 gives 251 samples including both ends", "[req497][alignment]") {
  std::vector<float> z;
  const GridSurfaceQuery grid = PlaneGrid(251, &z);  // X 0..2500
  const Alignment a = EastLine(2500.0);
  std::vector<GroundPoint> samples;
  std::string err;
  REQUIRE(alignment::SampleGround(a, grid, 10.0, 100000, &samples, &err));
  REQUIRE(samples.size() == 251);
  CHECK(samples.front().internalStation == Approx(0.0));
  CHECK(samples.back().internalStation == Approx(2500.0));
  CHECK(samples[100].ground == Approx(0.1 * 1000.0 + 50.0).margin(1e-3));
  for (const GroundPoint& g : samples) CHECK(g.hasGround);
}

TEST_CASE("Sampling includes every geometry point on the alignment", "[req497][alignment]") {
  std::vector<float> z;
  const GridSurfaceQuery grid = PlaneGrid(51, &z);  // X 0..500
  Alignment a;
  REQUIRE(a.AppendTangent({0.0, 5.0}, 90.0 * kDeg, 100.0));
  REQUIRE(a.AppendCurveByDelta(60.0, 40.0 * kDeg, true));
  REQUIRE(a.AppendTangent(100.0));
  std::vector<GroundPoint> samples;
  std::string err;
  REQUIRE(alignment::SampleGround(a, grid, 33.0, 100000, &samples, &err));
  bool hasPc = false, hasPt = false;
  for (const GroundPoint& g : samples) {
    if (std::fabs(g.internalStation - 100.0) < 1e-6) hasPc = true;
    if (std::fabs(g.internalStation - (100.0 + 60.0 * 40.0 * kDeg)) < 1e-6) hasPt = true;
  }
  CHECK(hasPc);
  CHECK(hasPt);
  for (size_t i = 1; i < samples.size(); ++i) CHECK(samples[i].internalStation > samples[i - 1].internalStation);
}

TEST_CASE("Sampling refuses an empty alignment, a bad interval and too many samples", "[req497][alignment]") {
  std::vector<float> z;
  const GridSurfaceQuery grid = PlaneGrid(11, &z);
  std::vector<GroundPoint> out;
  std::string err;
  CHECK_FALSE(alignment::SampleGround(Alignment{}, grid, 10.0, 100000, &out, &err));
  CHECK_FALSE(err.empty());
  const Alignment a = EastLine(100.0);
  CHECK_FALSE(alignment::SampleGround(a, grid, 0.0, 100000, &out, &err));
  CHECK_FALSE(alignment::SampleGround(a, grid, -3.0, 100000, &out, &err));
  CHECK_FALSE(alignment::SampleGround(a, grid, 0.01, 1000, &out, &err));  // 10000 steps, cap 1000
  CHECK(err.find("larger interval") != std::string::npos);
}

TEST_CASE("ALIGNMENTSURFACE reads the ground of a named surface, at a point and along a sampled line", "[req497][alignment]") {
  AppCommandState st;
  st.documents.resize(2);
  st.activeDrawingIdx = 1;
  std::vector<std::string> log;
  Submit(st, "ALIGNMENT G1, , 0+00", log);
  Submit(st, "ALIGNMENTADD G1, TANGENT, 0, 5, 90, 100", log);
  // A 11 x 2 grid over X 0..100, Y 0..10, Z = 0.1 X + 50 (row by row).
  std::string z;
  for (int j = 0; j < 2; ++j) {
    for (int i = 0; i < 11; ++i) {
      char buf[32];
      std::snprintf(buf, sizeof buf, ", %g", 0.1 * 10.0 * i + 50.0);
      z += buf;
    }
  }
  Submit(st, "SURFACECREATEGRID PLANE, 0, 0, 10, 10, 11, 2" + z, log);
  REQUIRE(st.cadSurfaces.size() == 1);

  log.clear();
  Submit(st, "ALIGNMENTSURFACE G1, PLANE, STATION=0+50, OFFSET=0", log);
  CHECK(LogHas(log, "EG = 55"));

  log.clear();
  Submit(st, "ALIGNMENTSURFACE G1, PLANE, STATION=1+50", log);  // 150 along a 100-long alignment
  CHECK(LogHas(log, "outside"));

  log.clear();
  Submit(st, "ALIGNMENTSURFACE G1, PLANE, INTERVAL=25", log);
  CHECK(LogHas(log, "5 sample(s), 5 with ground"));
  CHECK(LogHas(log, "EG = 50"));

  log.clear();
  Submit(st, "ALIGNMENTSURFACE G1, NOSUCH, STATION=0+50", log);
  CHECK(LogHas(log, "no surface named"));
  log.clear();
  Submit(st, "ALIGNMENTSURFACE G1, PLANE, BOGUS=1", log);
  CHECK(LogHas(log, "usage"));
}
