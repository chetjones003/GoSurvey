// GitHub issue #150 (3D Phase 7) — DRAPE lays geometry on the ground.
//
// Driven through ProcessCommandLineSubmit, the way a user types it, so the dispatch wiring is under
// test alongside the geometry. The surface is a TIN whose elevation is an exact plane, z = 2 + x/10,
// so every draped vertex has a hand-computable answer and REQ-101's +/-0.002 ft is a real assertion
// rather than a comparison against the same code that produced it.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <memory>
#include <string>
#include <vector>

#include "CadCommands.hpp"

using Catch::Approx;

namespace {

/// A square of ground from (0,0) to (100,100), two triangles, tilted so that z = 2 + x/10.
/// Flat in Y, so a vertex's elevation depends only on its X — easy to state and easy to check.
std::shared_ptr<CadTin> SlopingGround() {
  auto tin = std::make_shared<CadTin>();
  const double z0 = 2.0;
  tin->vertsXyz = {
      0.0,   0.0,   z0 + 0.0,    // 0
      100.0, 0.0,   z0 + 10.0,   // 1
      100.0, 100.0, z0 + 10.0,   // 2
      0.0,   100.0, z0 + 0.0,    // 3
  };
  tin->indices = {0, 1, 2, 0, 2, 3};
  return tin;
}

/// The elevation the plane above gives at \p x — what a correct drape must land on.
double GroundZ(double x) { return 2.0 + x / 10.0; }

void AddGround(AppCommandState& st, const std::string& name) {
  CadSurface s;
  s.name = name;
  s.tin = SlopingGround();
  st.cadSurfaces.push_back(std::move(s));
  st.cadSurfaceAttrs.push_back(EntityAttributes{});
}

/// A polyline of \p n vertices along y = 50, all at elevation 0, spaced across the ground.
void AddFlatPolylineAcross(AppCommandState& st, const std::vector<double>& xs) {
  if (st.userPolylineOffsets.empty())
    st.userPolylineOffsets.push_back(0);
  for (double x : xs) {
    st.userPolylineVerts.push_back(x);
    st.userPolylineVerts.push_back(50.0);
    st.userPolylineVerts.push_back(0.0);
  }
  st.userPolylineOffsets.push_back(static_cast<int>(st.userPolylineVerts.size() / 3));
  st.userPolylineClosed.push_back(0);
  st.userPolylineAttrs.push_back(EntityAttributes{});
}

void Type(AppCommandState& st, const std::string& line, std::vector<std::string>& log) {
  std::vector<char> buf(line.begin(), line.end());
  buf.push_back('\0');
  ProcessCommandLineSubmit(buf.data(), static_cast<int>(buf.size()), st, log);
}

bool LogHas(const std::vector<std::string>& log, const std::string& needle) {
  for (const std::string& l : log)
    if (l.find(needle) != std::string::npos)
      return true;
  return false;
}

}  // namespace

TEST_CASE("DRAPE puts every vertex on the ground under it", "[drape][issue150][phase7]") {
  AppCommandState st;
  AddGround(st, "EG");
  const std::vector<double> xs = {0.0, 10.0, 25.0, 60.0, 100.0};
  AddFlatPolylineAcross(st, xs);
  st.selection.push_back({SelectedEntity::Type::Polyline, 0});
  std::vector<std::string> log;

  Type(st, "DRAPE EG", log);

  INFO("last log line: " << (log.empty() ? std::string("(none)") : log.back()));
  REQUIRE(st.userPolylineVerts.size() == xs.size() * 3);
  for (size_t i = 0; i < xs.size(); ++i) {
    INFO("vertex " << i << " at x = " << xs[i]);
    // REQ-101: +/-0.002 ft against the plane's own arithmetic, not against the drape's.
    CHECK(st.userPolylineVerts[i * 3 + 2] == Approx(GroundZ(xs[i])).margin(0.002));
    CHECK(st.userPolylineVerts[i * 3] == Approx(xs[i]));      // X unmoved
    CHECK(st.userPolylineVerts[i * 3 + 1] == Approx(50.0));   // Y unmoved
  }
  CHECK(LogHas(log, "draped onto \"EG\""));
}

TEST_CASE("DRAPE is one undo step", "[drape][issue150][phase7]") {
  AppCommandState st;
  AddGround(st, "EG");
  AddFlatPolylineAcross(st, {10.0, 20.0, 30.0});
  st.selection.push_back({SelectedEntity::Type::Polyline, 0});
  std::vector<std::string> log;

  Type(st, "DRAPE EG", log);
  REQUIRE(st.userPolylineVerts[2] == Approx(GroundZ(10.0)).margin(0.002));

  DoUndo(st, log);
  for (int i = 0; i < 3; ++i) {
    INFO("vertex " << i);
    CHECK(st.userPolylineVerts[static_cast<size_t>(i) * 3 + 2] == Approx(0.0).margin(1e-9));
  }
}

TEST_CASE("A vertex off the surface refuses its whole entity, by name and count",
          "[drape][issue150][phase7]") {
  // ADR-062 (f). TinElevationAt never extrapolates (REQ-074), so there is no elevation for a vertex
  // beyond the ground. Draping the rest would leave a shape that is neither the original nor the
  // ground — the plausible-looking wrong answer REQ-201 forbids.
  AppCommandState st;
  AddGround(st, "EG");
  AddFlatPolylineAcross(st, {10.0, 50.0, 500.0});  // the last one is well off the square
  st.selection.push_back({SelectedEntity::Type::Polyline, 0});
  std::vector<std::string> log;

  Type(st, "DRAPE EG", log);

  CHECK(LogHas(log, "not draped"));
  CHECK(LogHas(log, "1 of 3 vertices are off"));
  // Nothing moved — not even the two vertices that WERE over the ground.
  for (int i = 0; i < 3; ++i) {
    INFO("vertex " << i);
    CHECK(st.userPolylineVerts[static_cast<size_t>(i) * 3 + 2] == Approx(0.0).margin(1e-9));
  }
}

TEST_CASE("DRAPE names what it cannot drape, and still drapes the rest", "[drape][issue150][phase7]") {
  AppCommandState st;
  AddGround(st, "EG");
  AddFlatPolylineAcross(st, {10.0, 20.0});
  // A circle is not a list of vertices: draping sampled points would leave something that is no
  // longer a circle, so it is refused by name rather than silently skipped (REQ-201).
  st.userCirclesCxCyZR = {50.0, 50.0, 0.0, 5.0};
  st.userCircleAttrs.push_back(EntityAttributes{});
  st.selection.push_back({SelectedEntity::Type::Polyline, 0});
  st.selection.push_back({SelectedEntity::Type::Circle, 0});
  std::vector<std::string> log;

  Type(st, "DRAPE EG", log);

  CHECK(LogHas(log, "could not be draped"));
  CHECK(LogHas(log, "circle"));
  CHECK(st.userPolylineVerts[2] == Approx(GroundZ(10.0)).margin(0.002));  // the polyline still went
  CHECK(st.userCirclesCxCyZR[2] == Approx(0.0).margin(1e-9));             // the circle did not move
}

TEST_CASE("DRAPE refuses clearly when it has nothing to work with", "[drape][issue150][phase7]") {
  SECTION("no surfaces at all") {
    AppCommandState st;
    std::vector<std::string> log;
    Type(st, "DRAPE", log);
    CHECK(LogHas(log, "no surfaces"));
  }
  SECTION("a name that is not a surface") {
    AppCommandState st;
    AddGround(st, "EG");
    std::vector<std::string> log;
    Type(st, "DRAPE Nope", log);
    CHECK(LogHas(log, "no surface named"));
  }
  SECTION("nothing selected") {
    AppCommandState st;
    AddGround(st, "EG");
    std::vector<std::string> log;
    Type(st, "DRAPE EG", log);
    CHECK(LogHas(log, "select the objects"));
  }
  SECTION("several surfaces are listed rather than guessed") {
    AppCommandState st;
    AddGround(st, "EG");
    AddGround(st, "FG");
    AddFlatPolylineAcross(st, {10.0});
    st.selection.push_back({SelectedEntity::Type::Polyline, 0});
    std::vector<std::string> log;
    Type(st, "DRAPE", log);
    CHECK(LogHas(log, "Surfaces: EG, FG"));
    CHECK(st.userPolylineVerts[2] == Approx(0.0).margin(1e-9));  // and nothing was touched
  }
}

TEST_CASE("DRAPE holds REQ-101 at survey coordinate magnitudes", "[drape][issue150][phase7]") {
  // Phase 7 acceptance: "everything remains stable at survey coordinate magnitudes." The ground and
  // the geometry both sit out at state-plane easting, where a float would have lost a quarter of a
  // foot before the drape even started.
  AppCommandState st;
  const double e0 = 2196000.0;
  const double n0 = 1400000.0;
  CadSurface s;
  s.name = "EG";
  auto tin = std::make_shared<CadTin>();
  tin->vertsXyz = {
      e0,         n0,         250.0,
      e0 + 100.0, n0,         260.0,
      e0 + 100.0, n0 + 100.0, 260.0,
      e0,         n0 + 100.0, 250.0,
  };
  tin->indices = {0, 1, 2, 0, 2, 3};
  s.tin = tin;
  st.cadSurfaces.push_back(std::move(s));
  st.cadSurfaceAttrs.push_back(EntityAttributes{});

  st.userPolylineOffsets.push_back(0);
  const std::vector<double> xs = {e0 + 10.0, e0 + 37.5, e0 + 90.0};
  for (double x : xs) {
    st.userPolylineVerts.push_back(x);
    st.userPolylineVerts.push_back(n0 + 50.0);
    st.userPolylineVerts.push_back(0.0);
  }
  st.userPolylineOffsets.push_back(3);
  st.userPolylineClosed.push_back(0);
  st.userPolylineAttrs.push_back(EntityAttributes{});
  st.selection.push_back({SelectedEntity::Type::Polyline, 0});

  std::vector<std::string> log;
  Type(st, "DRAPE EG", log);

  for (size_t i = 0; i < xs.size(); ++i) {
    const double want = 250.0 + (xs[i] - e0) / 10.0;
    INFO("vertex " << i);
    CHECK(st.userPolylineVerts[i * 3 + 2] == Approx(want).margin(0.002));
  }
}
