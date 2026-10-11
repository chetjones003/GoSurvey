// REQ-103 STRETCH (step 5, TASK-098) — RecomputeArcFromEndpoints, the one genuinely new piece of
// geometry this task adds: true AutoCAD-parity arc partial stretch (center/radius recomputed so
// the arc passes through a moved endpoint and a fixed endpoint while preserving the original
// included angle). Header-only/inline (CadCommands.hpp), so this is testable without linking the
// whole command layer — the same reasoning SurfaceRebuildLifetimeTests/ExtendedGeometryGateTests
// already follow.
//
// Every expected value below was hand-derived from the closed form itself (spec/project.md
// D-2026-08-24-d) before being written here — this file is the automated pin of that derivation,
// not a second independent check of it.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "CadCommands.hpp"

using Catch::Approx;

namespace {
CadArc MakeArc(float cx, float cy, float r, float startRad, float sweepRad) {
  CadArc a{};
  a.cx = cx;
  a.cy = cy;
  a.r = r;
  a.startRad = startRad;
  a.sweepRad = sweepRad;
  return a;
}
}  // namespace

TEST_CASE("RecomputeArcFromEndpoints: quarter circle, one endpoint moved", "[stretch][req103]") {
  // r=10, center (0,0), start at angle 0 -> (10,0); sweep +90 deg -> end at angle 90 -> (0,10).
  // Move ONLY the start point by (+5,0): new start (15,0), end fixed at (0,10).
  // Hand-derived (D-2026-08-24-d): V'=(-15,10), M'=(7.5,5), cot(45deg)=1, Rot90CW(V')=(10,15),
  // C' = M' - 0.5*(10,15) = (2.5,-2.5); r' = |(15,0)-(2.5,-2.5)| = sqrt(12.5^2+2.5^2) = sqrt(162.5).
  constexpr float kPi = 3.14159265358979323846f;
  CadArc a = MakeArc(0.f, 0.f, 10.f, 0.f, kPi * 0.5f);
  std::vector<std::string> log;
  const bool ok = RecomputeArcFromEndpoints(a, 15.f, 0.f, 0.f, 10.f, &log);
  REQUIRE(ok);
  CHECK(a.cx == Approx(2.5).margin(1e-3));
  CHECK(a.cy == Approx(-2.5).margin(1e-3));
  CHECK(a.r == Approx(std::sqrt(162.5)).margin(1e-3));
  // sweepRad preserved exactly.
  CHECK(a.sweepRad == Approx(kPi * 0.5f).margin(1e-6));
  // The recomputed arc must actually pass through both endpoints, at the preserved included angle.
  const float sx = a.cx + a.r * std::cos(a.startRad);
  const float sy = a.cy + a.r * std::sin(a.startRad);
  CHECK(sx == Approx(15.0).margin(1e-3));
  CHECK(sy == Approx(0.0).margin(1e-3));
  const float ex = a.cx + a.r * std::cos(a.startRad + a.sweepRad);
  const float ey = a.cy + a.r * std::sin(a.startRad + a.sweepRad);
  CHECK(ex == Approx(0.0).margin(1e-3));
  CHECK(ey == Approx(10.0).margin(1e-3));
}

TEST_CASE("RecomputeArcFromEndpoints: semicircle, cot(sweep/2)=0 branch", "[stretch][req103]") {
  // r=5, center (0,0), start at angle 0 -> (5,0); sweep = 180 deg -> end at (-5,0). A semicircle's
  // center is always the midpoint of its two endpoints (cot(90 deg)=0), an independent hand check
  // of the closed form's second term. Move the start point by (+2,0): new start (7,0), end fixed.
  constexpr float kPi = 3.14159265358979323846f;
  CadArc a = MakeArc(0.f, 0.f, 5.f, 0.f, kPi);
  std::vector<std::string> log;
  const bool ok = RecomputeArcFromEndpoints(a, 7.f, 0.f, -5.f, 0.f, &log);
  REQUIRE(ok);
  CHECK(a.cx == Approx(1.0).margin(1e-4));  // midpoint of (7,0) and (-5,0)
  CHECK(a.cy == Approx(0.0).margin(1e-4));
  CHECK(a.r == Approx(6.0).margin(1e-4));  // half the new chord length
  CHECK(a.startRad == Approx(0.0).margin(1e-4));
  CHECK(a.sweepRad == Approx(kPi).margin(1e-6));
}

TEST_CASE("RecomputeArcFromEndpoints: both endpoints shifted equally degenerates to a pure translation",
         "[stretch][req103]") {
  // Whole-arc translate (both endpoints in the crossing box) must fall out of the SAME formula with
  // no special-casing — this is the case StretchOneArc relies on to avoid a separate code path.
  constexpr float kPi = 3.14159265358979323846f;
  const CadArc orig = MakeArc(100.f, -50.f, 7.5f, 0.3f, 1.1f);
  const float startX = orig.cx + orig.r * std::cos(orig.startRad);
  const float startY = orig.cy + orig.r * std::sin(orig.startRad);
  const float endAng = orig.startRad + orig.sweepRad;
  const float endX = orig.cx + orig.r * std::cos(endAng);
  const float endY = orig.cy + orig.r * std::sin(endAng);
  CadArc a = orig;
  std::vector<std::string> log;
  const bool ok = RecomputeArcFromEndpoints(a, startX + 20.f, startY - 8.f, endX + 20.f, endY - 8.f, &log);
  REQUIRE(ok);
  CHECK(a.cx == Approx(orig.cx + 20.f).margin(1e-2));
  CHECK(a.cy == Approx(orig.cy - 8.f).margin(1e-2));
  CHECK(a.r == Approx(orig.r).margin(1e-2));
  CHECK(a.startRad == Approx(orig.startRad).margin(1e-4));
  CHECK(a.sweepRad == Approx(orig.sweepRad).margin(1e-6));
  (void)kPi;
}

TEST_CASE("RecomputeArcFromEndpoints: collapsing chord is refused, arc left unchanged", "[stretch][req103]") {
  constexpr float kPi = 3.14159265358979323846f;
  CadArc a = MakeArc(0.f, 0.f, 10.f, 0.f, kPi * 0.5f);
  const CadArc before = a;
  std::vector<std::string> log;
  // Both new endpoints dragged onto the same point: no valid arc can pass through one point twice.
  const bool ok = RecomputeArcFromEndpoints(a, 3.f, 4.f, 3.f, 4.f, &log);
  CHECK_FALSE(ok);
  CHECK(a.cx == before.cx);
  CHECK(a.cy == before.cy);
  CHECK(a.r == before.r);
  CHECK(a.startRad == before.startRad);
  CHECK_FALSE(log.empty());
}
