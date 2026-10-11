// OFFSET flow + precision regression (user report, 2026-09-15).
//
// 1. Flow: typing OFFSET should ask for distance FIRST, then select object, then side; after a
//    successful offset it should loop back to "select object" with the SAME distance, not end the
//    command. This mirrors REQ-103's documented TRIM/OFFSET per-target loop pattern.
// 2. Precision: OFFSET's internal math used to read the `double` geometry storage into `float`
//    locals before computing the perpendicular offset, which at state-plane/UTM coordinate
//    magnitudes (~1e6+) lost ~0.1-0.2 units of precision on every offset, compounding across a
//    chain of repeated offsets on a non-axis-aligned line. Fixed by doing that math in `double`.
//
// Exercised through the command-line/viewport-pick entry points, like DistCommandTests. Test
// geometry is pushed directly into userLinesFlat/userLineAttrs rather than driven through the LINE
// command — LINE stays active after a blank Enter (ready for the next chain; only ESC ends it),
// which is orthogonal to what this file is testing.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "CadCommands.hpp"

using Catch::Approx;

namespace {

void Submit(AppCommandState& st, const std::string& text, std::vector<std::string>& log) {
  char buf[256];
  std::snprintf(buf, sizeof(buf), "%s", text.c_str());
  ProcessCommandLineSubmit(buf, sizeof(buf), st, log);
}

void PushLine(AppCommandState& st, double x0, double y0, double x1, double y1) {
  st.userLinesFlat.push_back(x0);
  st.userLinesFlat.push_back(y0);
  st.userLinesFlat.push_back(0.0);
  st.userLinesFlat.push_back(x1);
  st.userLinesFlat.push_back(y1);
  st.userLinesFlat.push_back(0.0);
  st.userLineAttrs.push_back(EntityAttributes{});
}

double PerpDistanceToLine(double ax, double ay, double bx, double by, double px, double py) {
  const double vx = bx - ax, vy = by - ay;
  const double len = std::hypot(vx, vy);
  const double nx = -vy / len, ny = vx / len;
  return std::fabs((px - ax) * nx + (py - ay) * ny);
}

} // namespace

TEST_CASE("OFFSET asks for distance before selecting an object, then loops back to select", "[commands][offset]") {
  AppCommandState st;
  std::vector<std::string> log;
  using K = AppCommandState::Kind;
  using OP = AppCommandState::OffsetPhase;

  PushLine(st, 0.0, 0.0, 10.0, 0.0);
  REQUIRE(st.userLinesFlat.size() == 6);

  Submit(st, "offset", log);
  REQUIRE(st.active == K::Offset);
  // Distance is the FIRST thing asked — before any object is selected.
  REQUIRE(st.offsetPhase == OP::WaitDistanceOrThrough);
  REQUIRE_FALSE(st.offsetEntityValid);

  Submit(st, "2", log);
  REQUIRE(st.offsetPhase == OP::WaitSelectEntity);
  REQUIRE(st.offsetTypedDistance == Approx(2.0));

  // Pick the line, then pick a side.
  SubmitViewportPick(st, 5.0, 0.0, log, false, false, nullptr);
  REQUIRE(st.offsetPhase == OP::WaitSidePick);
  SubmitViewportPick(st, 5.0, 1.0, log, false, false, nullptr);

  // OFFSET must still be active, looped back to select-object, with the SAME distance retained.
  REQUIRE(st.active == K::Offset);
  REQUIRE(st.offsetPhase == OP::WaitSelectEntity);
  REQUIRE(st.offsetTypedDistance == Approx(2.0));
  REQUIRE(st.userLinesFlat.size() == 12);  // one new offset line committed

  // Enter with nothing selected ends the command (matches TRIM's own per-target loop).
  Submit(st, "", log);
  REQUIRE(st.active == K::None);
}

TEST_CASE("OFFSET keeps exact distance across repeated offsets on a non-axis-aligned line", "[commands][offset]") {
  AppCommandState st;
  std::vector<std::string> log;

  // A diagonal line at survey/state-plane magnitude (~1.88e6), matching the user's report.
  PushLine(st, 1887700.0, 303400.0, 1887760.0, 303460.0);
  REQUIRE(st.userLinesFlat.size() == 6);

  Submit(st, "offset", log);
  Submit(st, "3.5", log);

  double midX = 0.5 * (st.userLinesFlat[0] + st.userLinesFlat[3]) + st.worldDocumentOriginX;
  double midY = 0.5 * (st.userLinesFlat[1] + st.userLinesFlat[4]) + st.worldDocumentOriginY;
  // Unit normal toward the (+1,-1) side, the same side every side-pick below targets, so the select
  // pick below tracks the newest offset line (not the original) on each iteration.
  const double nx = 1.0 / std::sqrt(2.0), ny = -1.0 / std::sqrt(2.0);

  // Offset the newest line 5 times in a row, same distance, same side each time.
  for (int i = 0; i < 5; ++i) {
    SubmitViewportPick(st, midX, midY, log, false, false, nullptr);  // select nearest line
    SubmitViewportPick(st, midX + 100.0 * nx, midY + 100.0 * ny, log, false, false, nullptr);  // consistent side
    midX += 3.5 * nx;
    midY += 3.5 * ny;
  }
  REQUIRE(st.userLinesFlat.size() == 6 * 6);  // original + 5 offsets

  // Every consecutive pair of parallel lines must be EXACTLY 3.5 apart (within double precision),
  // not drifting further from 3.5 as the chain grows (the reported "skew").
  for (size_t i = 0; i + 1 < 6; ++i) {
    const size_t kA = i * 6, kB = (i + 1) * 6;
    const double ax = st.userLinesFlat[kA] + st.worldDocumentOriginX;
    const double ay = st.userLinesFlat[kA + 1] + st.worldDocumentOriginY;
    const double bx = st.userLinesFlat[kA + 3] + st.worldDocumentOriginX;
    const double by = st.userLinesFlat[kA + 4] + st.worldDocumentOriginY;
    const double px = st.userLinesFlat[kB] + st.worldDocumentOriginX;
    const double py = st.userLinesFlat[kB + 1] + st.worldDocumentOriginY;
    const double d = PerpDistanceToLine(ax, ay, bx, by, px, py);
    INFO("offset pair " << i << " -> " << (i + 1) << " perpendicular distance = " << d);
    CHECK(d == Approx(3.5).margin(1e-6));
  }
}
