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

// --- The opt-in link (ADR-062 (b)-(e)) ----------------------------------------------------------

namespace {

/// The same ground, raised by `lift` — what a surface edit looks like to a linked drape.
std::shared_ptr<CadTin> SlopingGroundRaised(double lift) {
  auto tin = SlopingGround();
  for (size_t i = 2; i < tin->vertsXyz.size(); i += 3)
    tin->vertsXyz[i] += lift;
  return tin;
}

std::uint64_t SurfaceIdOf(AppCommandState& st, size_t si) {
  EnsureEntityIds(st);
  return st.cadSurfaceAttrs[si].id;
}

}  // namespace

TEST_CASE("A baked drape stores no link; LINK stores the surface's stable id",
          "[drape][issue150][phase7][drapelink]") {
  AppCommandState st;
  AddGround(st, "EG");
  AddFlatPolylineAcross(st, {10.0, 20.0});
  st.selection.push_back({SelectedEntity::Type::Polyline, 0});
  std::vector<std::string> log;

  SECTION("baked leaves nothing behind") {
    Type(st, "DRAPE EG", log);
    CHECK(st.userPolylineAttrs[0].drapedOnSurfaceId == 0u);
  }
  SECTION("LINK stores the id, not the index and not the name") {
    Type(st, "DRAPE EG, LINK", log);
    const std::uint64_t id = SurfaceIdOf(st, 0);
    CHECK(id != 0u);
    CHECK(st.userPolylineAttrs[0].drapedOnSurfaceId == id);
    CHECK(LogHas(log, "linked"));
  }
  SECTION("re-draping without LINK clears the link") {
    Type(st, "DRAPE EG, LINK", log);
    REQUIRE(st.userPolylineAttrs[0].drapedOnSurfaceId != 0u);
    Type(st, "DRAPE EG", log);
    CHECK(st.userPolylineAttrs[0].drapedOnSurfaceId == 0u);
  }
  SECTION("an unknown option is refused rather than ignored") {
    Type(st, "DRAPE EG, WOBBLE", log);
    CHECK(LogHas(log, "Unknown option"));
    CHECK(st.userPolylineVerts[2] == Approx(0.0).margin(1e-9));  // and nothing moved
  }
}

TEST_CASE("Linked geometry follows a rebuilt surface; baked geometry does not",
          "[drape][issue150][phase7][drapelink]") {
  AppCommandState st;
  AddGround(st, "EG");
  AddFlatPolylineAcross(st, {10.0, 20.0});   // polyline 0 - will be LINKED
  AddFlatPolylineAcross(st, {30.0, 40.0});   // polyline 1 - will be BAKED
  std::vector<std::string> log;

  st.selection.clear();
  st.selection.push_back({SelectedEntity::Type::Polyline, 0});
  Type(st, "DRAPE EG, LINK", log);
  st.selection.clear();
  st.selection.push_back({SelectedEntity::Type::Polyline, 1});
  Type(st, "DRAPE EG", log);

  REQUIRE(st.userPolylineVerts[2] == Approx(GroundZ(10.0)).margin(0.002));
  REQUIRE(st.userPolylineVerts[8] == Approx(GroundZ(30.0)).margin(0.002));

  // The ground rises by 5 and the surface is rebuilt.
  st.cadSurfaces[0].tin = SlopingGroundRaised(5.0);
  log.clear();
  ReDrapeLinkedToSurface(st, 0, log);

  // The linked one followed...
  CHECK(st.userPolylineVerts[2] == Approx(GroundZ(10.0) + 5.0).margin(0.002));
  CHECK(st.userPolylineVerts[5] == Approx(GroundZ(20.0) + 5.0).margin(0.002));
  // ...and the baked one did not move at all. This is the whole point of the default.
  CHECK(st.userPolylineVerts[8] == Approx(GroundZ(30.0)).margin(0.002));
  CHECK(st.userPolylineVerts[11] == Approx(GroundZ(40.0)).margin(0.002));
  CHECK(LogHas(log, "1 linked object(s) re-draped"));
}

TEST_CASE("SURFACEREBUILD re-drapes what is linked to it", "[drape][issue150][phase7][drapelink]") {
  // The hook, through the command a user actually types, rather than the helper directly.
  AppCommandState st;
  AddGround(st, "EG");
  AddFlatPolylineAcross(st, {10.0, 20.0});
  st.selection.push_back({SelectedEntity::Type::Polyline, 0});
  std::vector<std::string> log;
  Type(st, "DRAPE EG, LINK", log);
  REQUIRE(st.userPolylineVerts[2] == Approx(GroundZ(10.0)).margin(0.002));

  st.cadSurfaces[0].tin = SlopingGroundRaised(3.0);
  log.clear();
  Type(st, "SURFACEREBUILD EG", log);
  // The rebuild has no sources to build from, so the TIN it finds is the one just set; what is
  // asserted here is that the re-drape ran at all off the command.
  CHECK(LogHas(log, "re-draped"));
}

TEST_CASE("A link to an erased surface resolves to nothing and the geometry stays put",
          "[drape][issue150][phase7][drapelink]") {
  // ADR-062 (e). The reference resolving to nothing is REQ-076's own rule; what the GEOMETRY does is
  // the decision - it is not deleted and not moved. Destroying drawn geometry because a surface was
  // erased would be far worse than a stale shape, and the stale shape is what a bake gives anyway.
  AppCommandState st;
  AddGround(st, "EG");
  AddFlatPolylineAcross(st, {10.0, 20.0});
  st.selection.push_back({SelectedEntity::Type::Polyline, 0});
  std::vector<std::string> log;
  Type(st, "DRAPE EG, LINK", log);
  const std::uint64_t id = SurfaceIdOf(st, 0);
  const double zBefore = st.userPolylineVerts[2];
  REQUIRE(zBefore == Approx(GroundZ(10.0)).margin(0.002));

  st.cadSurfaces.clear();
  st.cadSurfaceAttrs.clear();

  // The id is still on the entity, and resolves to nothing rather than to whatever takes the slot.
  CHECK(st.userPolylineAttrs[0].drapedOnSurfaceId == id);
  CHECK(FindSurfaceIndexById(st, id) == -1);
  // And the shape is exactly where it was.
  CHECK(st.userPolylineVerts[2] == Approx(zBefore).margin(1e-12));
}

TEST_CASE("A linked entity that leaves the ground is left where it is, and said so",
          "[drape][issue150][phase7][drapelink]") {
  AppCommandState st;
  AddGround(st, "EG");
  AddFlatPolylineAcross(st, {10.0, 20.0});
  st.selection.push_back({SelectedEntity::Type::Polyline, 0});
  std::vector<std::string> log;
  Type(st, "DRAPE EG, LINK", log);
  const double z0 = st.userPolylineVerts[2];
  const double z1 = st.userPolylineVerts[5];

  // The surface shrinks to a corner that no longer covers the polyline.
  auto small = std::make_shared<CadTin>();
  small->vertsXyz = {60.0, 60.0, 9.0, 70.0, 60.0, 9.0, 70.0, 70.0, 9.0};
  small->indices = {0, 1, 2};
  st.cadSurfaces[0].tin = small;

  log.clear();
  ReDrapeLinkedToSurface(st, 0, log);

  CHECK(LogHas(log, "left where they are"));
  CHECK(st.userPolylineVerts[2] == Approx(z0).margin(1e-12));
  CHECK(st.userPolylineVerts[5] == Approx(z1).margin(1e-12));
  // The link is KEPT, so it re-drapes on its own once the ground covers it again.
  CHECK(st.userPolylineAttrs[0].drapedOnSurfaceId != 0u);
}

TEST_CASE("A refused drape pushes no undo step", "[drape][issue150][phase7][drapelink]") {
  // An undo after a drape that moved nothing must take back whatever the user did BEFORE it.
  AppCommandState st;
  AddGround(st, "EG");
  AddFlatPolylineAcross(st, {10.0, 5000.0});  // the second vertex is off the ground
  st.selection.push_back({SelectedEntity::Type::Polyline, 0});
  std::vector<std::string> log;

  // Something undoable first, so there is a step to come back to.
  PushUndoSnapshot(st, "Before");
  st.userPolylineVerts[2] = 42.0;

  Type(st, "DRAPE EG", log);
  REQUIRE(LogHas(log, "not draped"));

  DoUndo(st, log);
  CHECK(st.userPolylineVerts[2] == Approx(0.0).margin(1e-9));  // the "Before" step, not a no-op drape
}

// --- ADR-062 (d): the link is visible, not a hidden attribute -----------------------------------

TEST_CASE("DrapedOnSurfaceName reports what an object follows", "[drape][issue150][phase7][drapemark]") {
  AppCommandState st;
  AddGround(st, "EG");
  AddFlatPolylineAcross(st, {10.0, 20.0});   // 0 - linked
  AddFlatPolylineAcross(st, {30.0, 40.0});   // 1 - baked
  std::vector<std::string> log;

  st.selection.clear();
  st.selection.push_back({SelectedEntity::Type::Polyline, 0});
  Type(st, "DRAPE EG, LINK", log);
  st.selection.clear();
  st.selection.push_back({SelectedEntity::Type::Polyline, 1});
  Type(st, "DRAPE EG", log);

  CHECK(DrapedOnSurfaceName(st, {SelectedEntity::Type::Polyline, 0}) == "EG");
  CHECK(DrapedOnSurfaceName(st, {SelectedEntity::Type::Polyline, 1}).empty());
  // A type that cannot be draped, and an index past the end, both answer "follows nothing" rather
  // than reading off the end of a store.
  CHECK(DrapedOnSurfaceName(st, {SelectedEntity::Type::Circle, 0}).empty());
  CHECK(DrapedOnSurfaceName(st, {SelectedEntity::Type::Polyline, 99}).empty());
}

TEST_CASE("An object whose surface was erased reports following nothing",
          "[drape][issue150][phase7][drapemark]") {
  // ADR-062 (e) again, but from the USER's side: the geometry stays put, and the panel must not go
  // on claiming it follows something. "Never linked", "baked since" and "its surface is gone" are
  // the same answer to the person looking at it.
  AppCommandState st;
  AddGround(st, "EG");
  AddFlatPolylineAcross(st, {10.0, 20.0});
  st.selection.push_back({SelectedEntity::Type::Polyline, 0});
  std::vector<std::string> log;
  Type(st, "DRAPE EG, LINK", log);
  REQUIRE(DrapedOnSurfaceName(st, {SelectedEntity::Type::Polyline, 0}) == "EG");

  st.cadSurfaces.clear();
  st.cadSurfaceAttrs.clear();
  CHECK(DrapedOnSurfaceName(st, {SelectedEntity::Type::Polyline, 0}).empty());
  // ...while the id itself is untouched, so undoing the erase brings the link back.
  CHECK(st.userPolylineAttrs[0].drapedOnSurfaceId != 0u);
}

TEST_CASE("DRAPELINKS answers what moves when a surface is rebuilt",
          "[drape][issue150][phase7][drapemark]") {
  AppCommandState st;
  std::vector<std::string> log;

  SECTION("a drawing where nothing follows a surface says so") {
    AddGround(st, "EG");
    AddFlatPolylineAcross(st, {10.0, 20.0});
    Type(st, "DRAPELINKS", log);
    CHECK(LogHas(log, "nothing in the drawing follows a surface"));
  }

  SECTION("it names each object and its surface") {
    AddGround(st, "EG");
    AddFlatPolylineAcross(st, {10.0, 20.0});
    AddFlatPolylineAcross(st, {30.0, 40.0});
    st.selection.push_back({SelectedEntity::Type::Polyline, 0});
    Type(st, "DRAPE EG, LINK", log);
    log.clear();
    Type(st, "DRAPELINKS", log);
    CHECK(LogHas(log, "polyline 0 follows \"EG\""));
    CHECK(LogHas(log, "1 object(s) follow a surface"));
    CHECK_FALSE(LogHas(log, "polyline 1 follows"));  // the baked one is not listed
  }

  SECTION("an object naming a surface that is gone is reported apart, not as following it") {
    AddGround(st, "EG");
    AddFlatPolylineAcross(st, {10.0, 20.0});
    st.selection.push_back({SelectedEntity::Type::Polyline, 0});
    Type(st, "DRAPE EG, LINK", log);
    st.cadSurfaces.clear();
    st.cadSurfaceAttrs.clear();
    log.clear();
    Type(st, "DRAPELINKS", log);
    CHECK(LogHas(log, "name a surface that is gone"));
    CHECK(LogHas(log, "stay where they are"));
    CHECK_FALSE(LogHas(log, "follows \""));
  }
}
