// GitHub issue #396 (PRESSPULL addendum) — PRESSPULL was brought in line with EXTRUDE's own
// select-target / wait-distance flow: a typed distance must take its sign from whichever direction
// the live preview is currently showing, not from the raw sign of the typed literal (the same rule
// ExtrudeTypedHeightDirectionTests.cpp asserts for EXTRUDE). This exercises both of PRESSPULL's
// targets — an existing solid FACE, and a closed 2D shape PRESSPULL turns into a new solid — by
// driving HandlePressPullTextInput directly with hand-set command state, so no viewport/GL context
// is needed.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "commands/CadCommands.hpp"
#include "util/brep.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace {

/// The +Z (top) face of a 20 x 10 x 8 box centred on the origin (the same box the req319 headless
/// transcript uses), as a target for PRESSPULL's face mode.
AppCommandState MakeFaceWaitDistanceState() {
  AppCommandState st;
  brep::Solid box;
  brep::Problem why{};
  REQUIRE(brep::MakeBox(ucs::Ucs{}, 20.0, 10.0, 8.0, &box, &why));
  const auto sp = std::make_shared<const brep::Solid>(std::move(box));
  st.cadSolids.push_back(sp);
  st.cadSolidAttrs.push_back(EntityAttributes{});
  RefreshSolidDisplayGeometry(st);

  int top = -1;
  for (size_t i = 0; i < sp->faces.size(); ++i) {
    const brep::Face& f = sp->faces[i];
    if (f.surface.kind == brep::SurfaceKind::Plane && f.surface.frame.zAxis.z > 0.5 &&
        std::fabs(f.surface.frame.origin.z - 8.0) < 1e-9)
      top = static_cast<int>(i);
  }
  REQUIRE(top >= 0);

  SelectedSubObject ref;
  ref.solidIndex = 0;
  ref.kind = solidpick::Kind::Face;
  ref.index = top;
  ref.owner = sp;

  st.active = AppCommandState::Kind::PressPull;
  st.pressPullPhase = AppCommandState::PressPullPhase::WaitDistance;
  st.pressPullOnFace = true;
  st.pressPullFace = ref;
  return st;
}

double BoxVolume(const AppCommandState& st) {
  REQUIRE(st.cadSolids.size() == 1);
  return brep::ComputeMassProperties(*st.cadSolids[0]).volume;
}

/// A 10x10 rectangle in the world XY plane, as a target for PRESSPULL's profile mode (a closed
/// polyline/circle PRESSPULL extrudes into a new solid, GitHub issue #396's widened scope).
AppCommandState MakeProfileWaitDistanceState() {
  AppCommandState st;
  brep::Profile pr;
  pr.plane = ucs::Ucs{};
  const std::vector<ucs::Point2D> pts2 = {{0.0, 0.0}, {10.0, 0.0}, {10.0, 10.0}, {0.0, 10.0}};
  for (const ucs::Point2D& q : pts2)
    pr.vertices.push_back(ucs::PlaneToWorld(pr.plane, q));
  pr.edges.assign(pts2.size(), brep::ProfileEdge{});

  st.active = AppCommandState::Kind::PressPull;
  st.pressPullPhase = AppCommandState::PressPullPhase::WaitDistance;
  st.pressPullOnFace = false;
  st.pressPullProfile = pr;
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
  return zmax + zmin;
}

}  // namespace

TEST_CASE("PRESSPULL face mode: typed magnitude follows the live preview's direction",
          "[presspull][issue396]") {
  std::vector<std::string> log;

  SECTION("preview pulling inward (-3), typed positive magnitude pulls inward") {
    AppCommandState st = MakeFaceWaitDistanceState();
    st.pressPullDistPickValid = true;
    st.pressPullDistPick = -3.0;

    REQUIRE(HandlePressPullTextInput("3", st, log));
    CHECK(BoxVolume(st) == Catch::Approx(1000.0));  // 1600 - 20*10*3
  }

  SECTION("preview pushing outward (+3), typed negative magnitude still pushes outward") {
    AppCommandState st = MakeFaceWaitDistanceState();
    st.pressPullDistPickValid = true;
    st.pressPullDistPick = 3.0;

    REQUIRE(HandlePressPullTextInput("-3", st, log));
    CHECK(BoxVolume(st) == Catch::Approx(2200.0));  // 1600 + 20*10*3
  }

  SECTION("no valid preview direction: the typed sign is used as-is") {
    AppCommandState st = MakeFaceWaitDistanceState();
    st.pressPullDistPickValid = false;

    REQUIRE(HandlePressPullTextInput("-3", st, log));
    CHECK(BoxVolume(st) == Catch::Approx(1000.0));
  }
}

TEST_CASE("PRESSPULL profile mode: typed magnitude follows the live preview's direction",
          "[presspull][issue396]") {
  std::vector<std::string> log;

  SECTION("preview pointing -Z, typed positive magnitude extrudes -Z") {
    AppCommandState st = MakeProfileWaitDistanceState();
    st.pressPullDistPickValid = true;
    st.pressPullDistPick = -5.0;

    REQUIRE(HandlePressPullTextInput("5", st, log));
    REQUIRE(st.cadSolids.size() == 1);
    CHECK(SolidZExtent(*st.cadSolids[0]) < 0.0);
  }

  SECTION("preview pointing +Z, typed negative magnitude still extrudes +Z") {
    AppCommandState st = MakeProfileWaitDistanceState();
    st.pressPullDistPickValid = true;
    st.pressPullDistPick = 5.0;

    REQUIRE(HandlePressPullTextInput("-5", st, log));
    REQUIRE(st.cadSolids.size() == 1);
    CHECK(SolidZExtent(*st.cadSolids[0]) > 0.0);
  }

  SECTION("no valid preview direction: the typed sign is used as-is") {
    AppCommandState st = MakeProfileWaitDistanceState();
    st.pressPullDistPickValid = false;

    REQUIRE(HandlePressPullTextInput("-5", st, log));
    REQUIRE(st.cadSolids.size() == 1);
    CHECK(SolidZExtent(*st.cadSolids[0]) < 0.0);
  }
}

TEST_CASE("StartPressPullCommand mirrors StartExtrudeCommand's select-then-distance shape",
          "[presspull][issue396]") {
  std::vector<std::string> log;

  SECTION("nothing selected: opens the SelectTarget prompt rather than refusing outright") {
    AppCommandState st;
    StartPressPullCommand(st, log);
    CHECK(st.active == AppCommandState::Kind::PressPull);
    CHECK(st.pressPullPhase == AppCommandState::PressPullPhase::SelectTarget);
  }

  SECTION("a face already named by Ctrl+click enters WaitDistance directly") {
    AppCommandState st = MakeFaceWaitDistanceState();
    st.pressPullPhase = AppCommandState::PressPullPhase::SelectTarget;  // as if freshly typed
    st.subObjectSelection.push_back(st.pressPullFace);
    st.active = AppCommandState::Kind::None;

    StartPressPullCommand(st, log);
    CHECK(st.active == AppCommandState::Kind::PressPull);
    CHECK(st.pressPullPhase == AppCommandState::PressPullPhase::WaitDistance);
    CHECK(st.pressPullOnFace);
  }
}
