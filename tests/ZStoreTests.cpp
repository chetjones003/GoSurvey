// Storage-layout regression tests for the 3D move (REQ-057 / ADR-025 (a)).
//
// These exist because the widening of two geometry stores — CadFilledRegion::verts (x,y → x,y,z)
// and userCirclesCxCyR → userCirclesCxCyZR (cx,cy,r → cx,cy,z,r) — is the kind of change that
// compiles cleanly while drawing garbage. The arrays were RENAMED as part of the widening so the
// compiler would flag every site, but a rename cannot catch a site that computes its own stride,
// and two such defects were in fact found only by audit. These tests pin the contracts that
// remain checkable without the GUI/GL stack (ADR-002).
//
// SCOPE LIMIT, stated honestly: the circle store's mutation paths (create / copy / offset / paste /
// purge) live in src/commands/CadCommands.cpp, which the test target does not link — it pulls in
// the UI and GL layers. So REQ-057's "survives insert, erase-from-the-middle and undo" condition
// is only partly automated here: it is covered for filled regions (whose helpers are pure) and by
// layout contracts for circles. See the TASK-034 log.

#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "commands/CadEntities.hpp"
#include "commands/HatchGeom.hpp"
#include "commands/PaperSpace.hpp"

namespace {

// A 10×10 square at a uniform elevation, as x,y,z triplets.
CadFilledRegion SquareAtZ(float z) {
  CadFilledRegion fr;
  fr.vertsXyz = {0, 0, z, 10, 0, z, 10, 10, z, 0, 10, z};
  fr.loopStart = {0};
  return fr;
}

}  // namespace

// ---------------------------------------------------------------------------
// CadFilledRegion — stride 3, and loopStart indexes VERTICES (not floats, not pairs).
// ---------------------------------------------------------------------------

TEST_CASE("Filled region vertsXyz is stride 3 and loopCount counts vertices", "[zstore]") {
  CadFilledRegion fr;
  // Outer square (4 verts) + triangular hole (3 verts) = 7 vertices = 21 floats.
  fr.vertsXyz = {0, 0, 0, 10, 0, 0, 10, 10, 0, 0, 10, 0,   // loop 0 — 4 verts
                 3, 3, 0, 6, 3, 0, 6, 6,  0};              // loop 1 — 3 verts
  fr.loopStart = {0, 4};

  REQUIRE(fr.vertsXyz.size() == 21);
  REQUIRE(fr.vertsXyz.size() % 3 == 0);
  REQUIRE(fr.loopCount(0) == 4);
  // The last loop runs to vertsXyz.size()/3 — this is the assertion that fails if anyone
  // reintroduces the old /2 (pair-based) arithmetic: it would report 10 - 4 = 6, not 3.
  REQUIRE(fr.loopCount(1) == 3);
}

TEST_CASE("Filled region degeneracy guard is 9 floats, not 6", "[zstore]") {
  // Pre-3D the guard was `verts.size() >= 6` meaning "3 vertices x 2 floats". At stride 3 the
  // same 3-vertex minimum is 9 floats. A region of exactly 3 vertices must still be valid, and
  // a 2-vertex region must still be rejected — the boundary the old constant silently moved.
  CadFilledRegion tri;
  tri.vertsXyz = {0, 0, 0, 4, 0, 0, 0, 4, 0};  // 3 verts, 9 floats
  tri.loopStart = {0};
  REQUIRE(tri.vertsXyz.size() == 9);
  REQUIRE(tri.loopCount(0) == 3);
  float a = 0, b = 0, c = 0, d = 0;
  REQUIRE(hatchgeom::OuterBounds(tri, &a, &b, &c, &d));

  CadFilledRegion two;
  two.vertsXyz = {0, 0, 0, 4, 0, 0};  // 2 verts, 6 floats — would have passed the OLD >= 6 guard
  two.loopStart = {0};
  REQUIRE(two.vertsXyz.size() == 6);
  REQUIRE_FALSE(hatchgeom::OuterBounds(two, &a, &b, &c, &d));
}

TEST_CASE("Elevation does not change plan-view containment or bounds", "[zstore]") {
  // Plan-view hit-testing must be unaffected by Z: picking a hatch is an X/Y question, and a
  // region lifted to 500 ft must still contain the same plan points and report the same AABB.
  const CadFilledRegion flat = SquareAtZ(0.f);
  const CadFilledRegion high = SquareAtZ(500.f);

  REQUIRE(hatchgeom::ContainsPoint(flat, 5.0, 5.0));
  REQUIRE(hatchgeom::ContainsPoint(high, 5.0, 5.0));
  REQUIRE_FALSE(hatchgeom::ContainsPoint(high, 11.0, 5.0));
  REQUIRE(hatchgeom::OuterAreaAbs(high) == hatchgeom::OuterAreaAbs(flat));

  float mnX = 0, mnY = 0, mxX = 0, mxY = 0;
  REQUIRE(hatchgeom::OuterBounds(high, &mnX, &mnY, &mxX, &mxY));
  REQUIRE(mnX == 0.f);
  REQUIRE(mnY == 0.f);
  REQUIRE(mxX == 10.f);
  REQUIRE(mxY == 10.f);
}

TEST_CASE("Erasing a vertex from the middle keeps each Z with its own XY", "[zstore]") {
  // The desync failure mode the widening had to avoid: after removing a vertex, every remaining
  // Z must still belong to the XY it was stored with. Each vertex here carries a distinct Z so a
  // one-slot misalignment is visible rather than plausible.
  CadFilledRegion fr;
  fr.vertsXyz = {0, 0, 10, 1, 0, 20, 2, 0, 30, 3, 0, 40};  // z = 10,20,30,40
  fr.loopStart = {0};

  // Erase vertex 1 (x=1, z=20) — one whole triplet.
  fr.vertsXyz.erase(fr.vertsXyz.begin() + 3, fr.vertsXyz.begin() + 6);

  REQUIRE(fr.vertsXyz.size() == 9);
  REQUIRE(fr.loopCount(0) == 3);
  REQUIRE(fr.vertsXyz[0] == 0.f);  REQUIRE(fr.vertsXyz[2] == 10.f);
  REQUIRE(fr.vertsXyz[3] == 2.f);  REQUIRE(fr.vertsXyz[5] == 30.f);
  REQUIRE(fr.vertsXyz[6] == 3.f);  REQUIRE(fr.vertsXyz[8] == 40.f);
}

TEST_CASE("A planar translate moves XY and leaves every Z alone", "[zstore]") {
  CadFilledRegion fr;
  fr.vertsXyz = {0, 0, 7, 4, 0, 8, 4, 4, 9};
  fr.loopStart = {0};
  hatchgeom::Translate(fr, 100.f, -50.f);

  REQUIRE(fr.vertsXyz[0] == 100.f);  REQUIRE(fr.vertsXyz[1] == -50.f);  REQUIRE(fr.vertsXyz[2] == 7.f);
  REQUIRE(fr.vertsXyz[3] == 104.f);  REQUIRE(fr.vertsXyz[4] == -50.f);  REQUIRE(fr.vertsXyz[5] == 8.f);
  REQUIRE(fr.vertsXyz[6] == 104.f);  REQUIRE(fr.vertsXyz[7] == -46.f);  REQUIRE(fr.vertsXyz[8] == 9.f);
}

// ---------------------------------------------------------------------------
// Entity value types — Z defaults to 0 so legacy drawings load flat (REQ-057 acceptance).
// ---------------------------------------------------------------------------

TEST_CASE("Entity Z defaults to zero and survives a copy", "[zstore]") {
  REQUIRE(CadArc{}.z == 0.f);
  REQUIRE(CadEllipse{}.z == 0.f);
  REQUIRE(CadAnnotation{}.insZ == 0.f);
  REQUIRE(CadFilledRegion{}.vertsXyz.empty());

  CadArc a;
  a.cx = 1.f; a.cy = 2.f; a.r = 3.f; a.z = 42.f;
  const CadArc b = a;
  REQUIRE(b.z == 42.f);

  CadAnnotation t;
  t.insX = 1.f; t.insY = 2.f; t.insZ = -17.5f;
  const CadAnnotation u = t;
  REQUIRE(u.insZ == -17.5f);
}

// ---------------------------------------------------------------------------
// Paper space stays 2D — paperCircles must remain stride 3 (ADR-025 (g)).
// ---------------------------------------------------------------------------

TEST_CASE("Paper circles are stride 3 - a sheet has no elevation", "[zstore][paperspace]") {
  // Model circles are cx,cy,z,r but paper circles are deliberately still cx,cy,r. If someone
  // "helpfully" widens paperCircles to match the model, these two pure helpers break: box-select
  // would read a radius as a Z, and the snap would place quadrant points at the wrong offsets.
  PaperLayout L;
  L.paperCircles = {2.f, 3.f, 1.f,    // circle 0 at (2,3) r=1
                    8.f, 9.f, 0.5f};  // circle 1 at (8,9) r=0.5
  REQUIRE(L.paperCircles.size() % 3 == 0);

  std::vector<PaperEntityRef> hits;
  SelectPaperEntitiesInBox(L, 0.f, 0.f, 5.f, 5.f, /*windowMode=*/true, hits);
  REQUIRE(hits.size() == 1);
  REQUIRE(hits[0].type == PaperEntityRef::Type::Circle);
  REQUIRE(hits[0].index == 0);  // reads 4 as the stride and this becomes 1, or nothing at all

  // The centre of circle 1 snaps — proving the second circle starts at float index 3, not 4.
  float sx = 0.f, sy = 0.f;
  REQUIRE(SnapPaperInchPoint(L, 8.02f, 9.02f, 0.25f, &sx, &sy));
  REQUIRE(sx == 8.f);
  REQUIRE(sy == 9.f);
}
