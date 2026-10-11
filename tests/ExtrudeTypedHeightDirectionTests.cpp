// GitHub issue #396 — a typed EXTRUDE height must take its sign from whichever direction the live
// preview is currently showing (the same rule LINE follows under ORTHO/polar), not from the raw
// sign of the typed literal. Exercises HandleExtrudeTextInput directly with a hand-built profile
// and a hand-set extrudeHeightPick, so no viewport/GL context is needed.

#include <catch2/catch_test_macros.hpp>

#include "commands/CadCommands.hpp"
#include "util/brep.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace {

/// A 10x10 rectangle in the world XY plane, the way EXTRUDE gathers a selected closed polyline.
brep::Profile RectProfile() {
  brep::Profile pr;
  pr.plane = ucs::Ucs{};
  const std::vector<ucs::Point2D> pts2 = {{0.0, 0.0}, {10.0, 0.0}, {10.0, 10.0}, {0.0, 10.0}};
  for (const ucs::Point2D& q : pts2)
    pr.vertices.push_back(ucs::PlaneToWorld(pr.plane, q));
  pr.edges.assign(pts2.size(), brep::ProfileEdge{});
  return pr;
}

/// Puts the command in WaitHeight with one profile queued, as if the user had already selected the
/// rectangle and pressed Enter.
AppCommandState MakeWaitHeightState() {
  AppCommandState st;
  st.active = AppCommandState::Kind::Extrude;
  st.extrudePhase = AppCommandState::ExtrudePhase::WaitHeight;
  st.extrudeProfiles = {RectProfile()};
  return st;
}

/// The signed extent of a solid's bounding box along +Z, i.e. which side of z=0 it grew into.
double SolidZExtent(const brep::Solid& s) {
  double zmin = 0.0, zmax = 0.0;
  bool first = true;
  for (const brep::Vertex& v : s.vertices) {
    if (first) {
      zmin = zmax = v.p.z;
      first = false;
    } else {
      zmin = std::min(zmin, v.p.z);
      zmax = std::max(zmax, v.p.z);
    }
  }
  return zmax + zmin;  // one bound sits at z=0, so this recovers the signed height
}

}  // namespace

TEST_CASE("EXTRUDE: typed magnitude follows the live preview's direction, not its own sign",
          "[extrude][issue396]") {
  std::vector<std::string> log;

  SECTION("preview pointing -Z, typed positive magnitude extrudes -Z") {
    AppCommandState st = MakeWaitHeightState();
    st.extrudeHeightPickValid = true;
    st.extrudeHeightPick = -5.0;

    REQUIRE(HandleExtrudeTextInput("5", st, log));
    REQUIRE(st.cadSolids.size() == 1);
    CHECK(SolidZExtent(*st.cadSolids[0]) < 0.0);
  }

  SECTION("preview pointing +Z, typed negative magnitude still extrudes +Z") {
    AppCommandState st = MakeWaitHeightState();
    st.extrudeHeightPickValid = true;
    st.extrudeHeightPick = 5.0;

    REQUIRE(HandleExtrudeTextInput("-5", st, log));
    REQUIRE(st.cadSolids.size() == 1);
    CHECK(SolidZExtent(*st.cadSolids[0]) > 0.0);
  }

  SECTION("no valid preview direction: the typed sign is used as-is") {
    AppCommandState st = MakeWaitHeightState();
    st.extrudeHeightPickValid = false;

    REQUIRE(HandleExtrudeTextInput("-5", st, log));
    REQUIRE(st.cadSolids.size() == 1);
    CHECK(SolidZExtent(*st.cadSolids[0]) < 0.0);
  }
}
