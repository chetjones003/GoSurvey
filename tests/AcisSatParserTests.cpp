#include "util/AcisSatParser.hpp"

#include "util/brep.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <sstream>
#include <string>

/// REQ-320 / ADR-051 (GitHub issue #299): the ACIS SAT parser. No real vendor SAT corpus is
/// available, so every fixture here is hand-authored against the field layout AcisSatParser.cpp
/// documents at its top — the same approach the ADR records as deliberate, not a shortcut.

namespace {

bool Contains(const std::string& haystack, const std::string& needle) {
  return haystack.find(needle) != std::string::npos;
}

/// The mandatory 3 ACIS header lines this parser skips verbatim.
const std::string kHeader =
    "700 0 1 0\n"
    "17 GoSurveyTest 7 32.0.2 NT 24 today\n"
    "1 9.9999999999999995e-07 1e-10\n";

/// A plain cylinder, radius 2, height 5, base at the world origin, axis +Z: 2 planar caps + 1
/// full-revolve cylindrical wall. See AcisSatParser.cpp's field-layout comment for what each
/// record's fields mean; see the ADR-051 (b-1) doc comment on BuildConeFace for the seam synthesis
/// this exercises.
const std::string kCylinderSat = kHeader + R"(
point $-1 2 0 0 #
point $-1 2 0 5 #
vertex $-1 $-1 $0 #
vertex $-1 $-1 $1 #
ellipse-curve $-1 0 0 0 0 0 -1 2 0 0 1 #
ellipse-curve $-1 0 0 5 0 0 1 2 0 0 1 #
edge $-1 $2 $2 $4 forward #
edge $-1 $3 $3 $5 forward #
plane-surface $-1 0 0 0 0 0 -1 1 0 0 #
plane-surface $-1 0 0 5 0 0 1 1 0 0 #
cone-surface $-1 0 0 0 0 0 1 1 0 0 0 1 2 1 #
loop $-1 $-1 $12 $18 #
coedge $-1 $12 $12 $16 $6 forward $11 #
loop $-1 $-1 $14 $19 #
coedge $-1 $14 $14 $17 $7 forward $13 #
loop $-1 $-1 $16 $20 #
coedge $-1 $17 $17 $12 $6 reversed $15 #
coedge $-1 $16 $16 $14 $7 reversed $15 #
face $-1 $19 $11 $21 $8 forward single #
face $-1 $20 $13 $21 $9 forward single #
face $-1 $-1 $15 $21 $10 forward single #
shell $-1 $-1 $-1 $18 $-1 $22 #
lump $-1 $-1 $21 $23 #
body $-1 $22 $-1 $-1 #
End-of-ACIS-data
)";

/// A unit cube (see `brep::MakeBox`'s vertex/edge/loop layout, reused verbatim here so the topology is
/// known-good) whose top face is declared as a `spline-surface` (a flat, degree-1x1 bilinear patch
/// spanning exactly the same 4 corners a `plane-surface` top face would) instead of a `plane-surface`
/// — GitHub issue #300.
const std::string kCubeSplineTopSat = kHeader + R"(
point $-1 -0.5 -0.5 0 #
point $-1 0.5 -0.5 0 #
point $-1 0.5 0.5 0 #
point $-1 -0.5 0.5 0 #
point $-1 -0.5 -0.5 1 #
point $-1 0.5 -0.5 1 #
point $-1 0.5 0.5 1 #
point $-1 -0.5 0.5 1 #
vertex $-1 $-1 $0 #
vertex $-1 $-1 $1 #
vertex $-1 $-1 $2 #
vertex $-1 $-1 $3 #
vertex $-1 $-1 $4 #
vertex $-1 $-1 $5 #
vertex $-1 $-1 $6 #
vertex $-1 $-1 $7 #
straight-curve $-1 0 0 0 1 0 0 #
edge $-1 $8 $9 $16 forward #
edge $-1 $9 $10 $16 forward #
edge $-1 $10 $11 $16 forward #
edge $-1 $11 $8 $16 forward #
edge $-1 $12 $13 $16 forward #
edge $-1 $13 $14 $16 forward #
edge $-1 $14 $15 $16 forward #
edge $-1 $15 $12 $16 forward #
edge $-1 $8 $12 $16 forward #
edge $-1 $9 $13 $16 forward #
edge $-1 $10 $14 $16 forward #
edge $-1 $11 $15 $16 forward #
plane-surface $-1 0 0 0 0 0 -1 1 0 0 #
plane-surface $-1 0 -0.5 0 0 -1 0 1 0 0 #
plane-surface $-1 0.5 0 0 1 0 0 0 1 0 #
plane-surface $-1 0 0.5 0 0 1 0 1 0 0 #
plane-surface $-1 -0.5 0 0 -1 0 0 0 1 0 #
spline-surface $-1 1 1 2 2 0 0 0 1 1 0 0 1 1 -0.5 -0.5 1 0.5 -0.5 1 -0.5 0.5 1 0.5 0.5 1 #
loop $-1 $-1 $36 $65 #
coedge $-1 $37 $-1 $-1 $20 reversed $35 #
coedge $-1 $38 $-1 $-1 $19 reversed $35 #
coedge $-1 $39 $-1 $-1 $18 reversed $35 #
coedge $-1 $36 $-1 $-1 $17 reversed $35 #
loop $-1 $-1 $41 $66 #
coedge $-1 $42 $-1 $-1 $21 forward $40 #
coedge $-1 $43 $-1 $-1 $22 forward $40 #
coedge $-1 $44 $-1 $-1 $23 forward $40 #
coedge $-1 $41 $-1 $-1 $24 forward $40 #
loop $-1 $-1 $46 $67 #
coedge $-1 $47 $-1 $-1 $17 forward $45 #
coedge $-1 $48 $-1 $-1 $26 forward $45 #
coedge $-1 $49 $-1 $-1 $21 reversed $45 #
coedge $-1 $46 $-1 $-1 $25 reversed $45 #
loop $-1 $-1 $51 $68 #
coedge $-1 $52 $-1 $-1 $18 forward $50 #
coedge $-1 $53 $-1 $-1 $27 forward $50 #
coedge $-1 $54 $-1 $-1 $22 reversed $50 #
coedge $-1 $51 $-1 $-1 $26 reversed $50 #
loop $-1 $-1 $56 $69 #
coedge $-1 $57 $-1 $-1 $19 forward $55 #
coedge $-1 $58 $-1 $-1 $28 forward $55 #
coedge $-1 $59 $-1 $-1 $23 reversed $55 #
coedge $-1 $56 $-1 $-1 $27 reversed $55 #
loop $-1 $-1 $61 $70 #
coedge $-1 $62 $-1 $-1 $20 forward $60 #
coedge $-1 $63 $-1 $-1 $25 forward $60 #
coedge $-1 $64 $-1 $-1 $24 reversed $60 #
coedge $-1 $61 $-1 $-1 $28 reversed $60 #
face $-1 $66 $35 $71 $29 forward single #
face $-1 $67 $40 $71 $34 forward single #
face $-1 $68 $45 $71 $30 forward single #
face $-1 $69 $50 $71 $31 forward single #
face $-1 $70 $55 $71 $32 forward single #
face $-1 $-1 $60 $71 $33 forward single #
shell $-1 $-1 $-1 $65 $-1 $72 #
lump $-1 $-1 $71 $73 #
body $-1 $72 $-1 $-1 #
End-of-ACIS-data
)";

/// A quarter-cylinder wedge (issue #310): radius 2, height 5, standing on the origin over the
/// quadrant `0<=x, 0<=y` — a straight extrusion (flat bottom and top quarter-disk caps, two flat
/// radial side faces, one curved wall) whose wall face's loop is an arc/line/arc/line quadrilateral
/// spanning less than the full revolve (`u: 0..pi/2`, not `0..2*pi`). `BuildConeFace`'s old
/// two-full-circle-rim recognizer refused this loop shape outright (see the ADR-051 (b-1) comment on
/// `BuildConeFace` in AcisSatParser.cpp: "a partial revolve...is deliberately NOT accepted"). Issue
/// #306's `Face::paramLoops` general trim loop is what lets this loop shape in now, via
/// `BuildConeGeneralTrim`. Volume = quarter-disk area * height = `(pi*2*2/4) * 5 = 5*pi`.
const std::string kQuarterCylinderSat = kHeader + R"(
point $-1 0 0 0 #
point $-1 2 0 0 #
point $-1 0 2 0 #
point $-1 0 0 5 #
point $-1 2 0 5 #
point $-1 0 2 5 #
vertex $-1 $-1 $0 #
vertex $-1 $-1 $1 #
vertex $-1 $-1 $2 #
vertex $-1 $-1 $3 #
vertex $-1 $-1 $4 #
vertex $-1 $-1 $5 #
straight-curve $-1 0 0 0 1 0 0 #
ellipse-curve $-1 0 0 0 0 0 1 2 0 0 1 #
ellipse-curve $-1 0 0 5 0 0 1 2 0 0 1 #
edge $-1 $6 $7 $12 forward #
edge $-1 $6 $8 $12 forward #
edge $-1 $9 $10 $12 forward #
edge $-1 $9 $11 $12 forward #
edge $-1 $6 $9 $12 forward #
edge $-1 $7 $10 $12 forward #
edge $-1 $8 $11 $12 forward #
edge $-1 $7 $8 $13 forward #
edge $-1 $10 $11 $14 forward #
plane-surface $-1 0 0 0 0 0 -1 1 0 0 #
plane-surface $-1 0 0 5 0 0 1 1 0 0 #
plane-surface $-1 0 0 0 0 -1 0 1 0 0 #
plane-surface $-1 0 0 0 -1 0 0 0 1 0 #
cone-surface $-1 0 0 0 0 0 1 1 0 0 0 1 2 1 #
loop $-1 $-1 $30 $-1 #
coedge $-1 $31 $-1 $-1 $16 forward $-1 #
coedge $-1 $32 $-1 $-1 $22 reversed $-1 #
coedge $-1 $30 $-1 $-1 $15 reversed $-1 #
loop $-1 $-1 $34 $-1 #
coedge $-1 $35 $-1 $-1 $17 forward $-1 #
coedge $-1 $36 $-1 $-1 $23 forward $-1 #
coedge $-1 $34 $-1 $-1 $18 reversed $-1 #
loop $-1 $-1 $38 $-1 #
coedge $-1 $39 $-1 $-1 $15 forward $-1 #
coedge $-1 $40 $-1 $-1 $20 forward $-1 #
coedge $-1 $41 $-1 $-1 $17 reversed $-1 #
coedge $-1 $38 $-1 $-1 $19 reversed $-1 #
loop $-1 $-1 $43 $-1 #
coedge $-1 $44 $-1 $-1 $19 forward $-1 #
coedge $-1 $45 $-1 $-1 $18 forward $-1 #
coedge $-1 $46 $-1 $-1 $21 reversed $-1 #
coedge $-1 $43 $-1 $-1 $16 reversed $-1 #
loop $-1 $-1 $48 $-1 #
coedge $-1 $49 $-1 $-1 $22 forward $-1 #
coedge $-1 $50 $-1 $-1 $21 forward $-1 #
coedge $-1 $51 $-1 $-1 $23 reversed $-1 #
coedge $-1 $48 $-1 $-1 $20 reversed $-1 #
face $-1 $53 $29 $-1 $24 forward single #
face $-1 $54 $33 $-1 $25 forward single #
face $-1 $55 $37 $-1 $26 forward single #
face $-1 $56 $42 $-1 $27 forward single #
face $-1 $-1 $47 $-1 $28 forward single #
shell $-1 $-1 $-1 $52 $-1 $58 #
lump $-1 $-1 $57 $59 #
body $-1 $58 $-1 $-1 #
End-of-ACIS-data
)";

}  // namespace

TEST_CASE("ACIS SAT import: plain cylinder builds a valid solid", "[acissat]") {
  const acissat::ImportResult r = acissat::ImportSatSolid(kCylinderSat, "TestEntity");
  INFO(r.error);
  REQUIRE(r.ok);
  CHECK(r.solid.faces.size() == 3);
  CHECK(r.solid.shells.size() == 1);

  int planeCount = 0, cylCount = 0;
  for (const brep::Face& f : r.solid.faces) {
    if (f.surface.kind == brep::SurfaceKind::Plane)
      ++planeCount;
    else if (f.surface.kind == brep::SurfaceKind::Cylinder)
      ++cylCount;
  }
  CHECK(planeCount == 2);
  CHECK(cylCount == 1);
  CHECK(brep::Validate(r.solid) == brep::Problem::Ok);

  const auto mp = brep::ComputeMassProperties(r.solid);
  const double expectedVolume = 3.14159265358979323846 * 2.0 * 2.0 * 5.0;
  CHECK(mp.volume == Catch::Approx(expectedVolume).epsilon(1e-9));
}

TEST_CASE("ACIS SAT import: empty stream is refused with a message", "[acissat]") {
  const acissat::ImportResult r = acissat::ImportSatSolid("", "E1");
  CHECK_FALSE(r.ok);
  CHECK_FALSE(r.error.empty());
}

TEST_CASE("ACIS SAT import: unsupported surface kind is refused by name, not silently dropped",
          "[acissat]") {
  // Same cylinder fixture, but the side face's surface record is swapped for a genuinely
  // unrecognized kind (not one of #300's spline/blend/sweep additions either).
  std::string sat = kCylinderSat;
  const std::string from = "cone-surface $-1 0 0 0 0 0 1 1 0 0 0 1 2 1 #";
  const std::string to = "helix-surface $-1 #";
  const size_t pos = sat.find(from);
  REQUIRE(pos != std::string::npos);
  sat.replace(pos, from.size(), to);

  const acissat::ImportResult r = acissat::ImportSatSolid(sat, "E2");
  CHECK_FALSE(r.ok);
  CHECK(Contains(r.error, "helix-surface"));
  CHECK(Contains(r.error, "E2"));
}

TEST_CASE("ACIS SAT import: spline-surface face maps onto a SurfaceKind::Nurbs patch (issue #300)",
          "[acissat]") {
  const acissat::ImportResult r = acissat::ImportSatSolid(kCubeSplineTopSat, "TestEntity");
  INFO(r.error);
  REQUIRE(r.ok);
  CHECK(r.solid.faces.size() == 6);
  CHECK(brep::Validate(r.solid) == brep::Problem::Ok);

  int nurbsCount = 0, planeCount = 0;
  for (const brep::Face& f : r.solid.faces) {
    if (f.surface.kind == brep::SurfaceKind::Nurbs)
      ++nurbsCount;
    else if (f.surface.kind == brep::SurfaceKind::Plane)
      ++planeCount;
  }
  CHECK(nurbsCount == 1);
  CHECK(planeCount == 5);

  const auto mp = brep::ComputeMassProperties(r.solid);
  CHECK(mp.volume == Catch::Approx(1.0).epsilon(1e-6));  // the flat spline top makes this exactly a cube
}

TEST_CASE("ACIS SAT import: a degree above the NURBS patch limit is refused", "[acissat]") {
  std::string sat = kCubeSplineTopSat;
  const std::string from = "spline-surface $-1 1 1 2 2 0";
  const std::string to = "spline-surface $-1 4 1 2 2 0";
  const size_t pos = sat.find(from);
  REQUIRE(pos != std::string::npos);
  sat.replace(pos, from.size(), to);

  const acissat::ImportResult r = acissat::ImportSatSolid(sat, "");
  CHECK_FALSE(r.ok);
  CHECK(Contains(r.error, "degree"));
}

TEST_CASE("ACIS SAT import: a trimmed spline-surface (loop not the full patch corners) is refused, "
          "not approximated",
          "[acissat]") {
  // Nudge one control point away from the loop's actual corner vertex, so the loop no longer bounds
  // the whole patch rectangle — this importer has no way to represent a genuine trim (ADR-048 (b)).
  std::string sat = kCubeSplineTopSat;
  const std::string from = "0.5 0.5 1 #";
  const std::string to = "0.5 0.5 1.5 #";
  const size_t pos = sat.rfind(from);
  REQUIRE(pos != std::string::npos);
  sat.replace(pos, from.size(), to);

  const acissat::ImportResult r = acissat::ImportSatSolid(sat, "");
  CHECK_FALSE(r.ok);
  CHECK(Contains(r.error, "does not bound the whole parametric patch"));
}

TEST_CASE("ACIS SAT import: blend-surface reduces to its representable underlying surface",
          "[acissat]") {
  // Appends a new `blend-surface $-1 $8 #` record (landing at index 24, right after kCylinderSat's
  // existing 24 records) that names the fixture's existing bottom plane-surface (record 8) as its
  // representable reduction, then repoints the bottom cap face at it instead of $8 directly — the
  // shape this importer builds must be unchanged.
  std::string sat = kCylinderSat;
  const size_t endPos = sat.find("End-of-ACIS-data");
  REQUIRE(endPos != std::string::npos);
  sat.insert(endPos, "blend-surface $-1 $8 #\n");
  const std::string faceFrom = "face $-1 $19 $11 $21 $8 forward single #";
  const std::string faceTo = "face $-1 $19 $11 $21 $24 forward single #";
  const size_t facePos = sat.find(faceFrom);
  REQUIRE(facePos != std::string::npos);
  sat.replace(facePos, faceFrom.size(), faceTo);

  const acissat::ImportResult r = acissat::ImportSatSolid(sat, "");
  INFO(r.error);
  REQUIRE(r.ok);
  CHECK(brep::Validate(r.solid) == brep::Problem::Ok);

  int planeCount = 0;
  for (const brep::Face& f : r.solid.faces)
    if (f.surface.kind == brep::SurfaceKind::Plane)
      ++planeCount;
  CHECK(planeCount == 2);
}

TEST_CASE("ACIS SAT import: blend-surface with no representable reduction is refused", "[acissat]") {
  std::string sat = kCylinderSat;
  const std::string from = "cone-surface $-1 0 0 0 0 0 1 1 0 0 0 1 2 1 #";
  const std::string to = "blend-surface $-1 $-1 #";
  const size_t pos = sat.find(from);
  REQUIRE(pos != std::string::npos);
  sat.replace(pos, from.size(), to);

  const acissat::ImportResult r = acissat::ImportSatSolid(sat, "");
  CHECK_FALSE(r.ok);
  CHECK(Contains(r.error, "blend-surface"));
  CHECK(Contains(r.error, "does not reduce"));
}

TEST_CASE("ACIS SAT import: sphere-surface is recognized but refused as a fast-follow", "[acissat]") {
  std::string sat = kCylinderSat;
  const std::string from = "cone-surface $-1 0 0 0 0 0 1 1 0 0 0 1 2 1 #";
  const std::string to = "sphere-surface $-1 0 0 0 0 0 1 1 0 0 2 #";
  const size_t pos = sat.find(from);
  REQUIRE(pos != std::string::npos);
  sat.replace(pos, from.size(), to);

  const acissat::ImportResult r = acissat::ImportSatSolid(sat, "");
  CHECK_FALSE(r.ok);
  CHECK(Contains(r.error, "sphere-surface"));
}

TEST_CASE("ACIS SAT import: a wire body (no lump) is refused, not silently empty", "[acissat]") {
  const std::string sat = kHeader + std::string(R"(
body $-1 $-1 $1 $-1 #
wire $-1 #
End-of-ACIS-data
)");
  const acissat::ImportResult r = acissat::ImportSatSolid(sat, "");
  CHECK_FALSE(r.ok);
  CHECK(Contains(r.error, "wire"));
}

TEST_CASE("ACIS SAT import: a malformed record is refused, never crashes", "[acissat]") {
  const std::string sat = kHeader + std::string("body garbage-not-a-pointer #\nEnd-of-ACIS-data\n");
  const acissat::ImportResult r = acissat::ImportSatSolid(sat, "");
  CHECK_FALSE(r.ok);
  CHECK_FALSE(r.error.empty());
}

TEST_CASE("ACIS SAT import: a non-rectangular trimmed cylindrical face imports via the general trim "
          "loop instead of being refused (issue #310)",
          "[acissat]") {
  const acissat::ImportResult r = acissat::ImportSatSolid(kQuarterCylinderSat, "QuarterCylinder");
  INFO(r.error);
  REQUIRE(r.ok);
  CHECK(r.solid.faces.size() == 5);
  CHECK(r.solid.shells.size() == 1);
  CHECK(brep::Validate(r.solid) == brep::Problem::Ok);

  const brep::Face* wall = nullptr;
  for (const brep::Face& f : r.solid.faces)
    if (f.surface.kind == brep::SurfaceKind::Cylinder)
      wall = &f;
  REQUIRE(wall != nullptr);
  CHECK_FALSE(wall->paramLoops.empty());

  const double kPi = 3.14159265358979323846;
  const auto mp = brep::ComputeMassProperties(r.solid);
  CHECK(mp.volume == Catch::Approx(5.0 * kPi).epsilon(1e-6));
  // Total surface area: two quarter-disk caps (pi*r^2/4 each) + two flat radial sides (r*h each) +
  // the curved wall (r*(pi/2)*h) = 2*pi + 20 + 5*pi = 7*pi + 20.
  CHECK(mp.surfaceArea == Catch::Approx(7.0 * kPi + 20.0).epsilon(1e-6));

  brep::Tessellation tess;
  brep::Problem tessWhy = brep::Problem::Ok;
  REQUIRE(brep::Tessellate(r.solid, 0.01, &tess, &tessWhy));
  CHECK(tess.indices.size() % 3 == 0);
  CHECK_FALSE(tess.indices.empty());
}

TEST_CASE("ACIS SAT import: a cylindrical face with a hole loop is still refused by name even "
          "though general trim loops are now accepted (issue #310)",
          "[acissat]") {
  // Gives the wall face a second loop (record index 60, appended below, reusing the same coedge
  // chain — its content doesn't matter, only that `loops.size()` becomes 2) by pointing the wall's
  // own loop (index 47) at it via `loop.next`. `BuildFaceForSurface`'s cone-surface branch refuses
  // any loop count other than exactly 1 before general-trim building ever runs, so this must still
  // be refused by name rather than silently misimported as, say, an annular general trim.
  std::string sat = kQuarterCylinderSat;
  const std::string from = "loop $-1 $-1 $48 $-1 #\ncoedge $-1 $49";
  const std::string to = "loop $-1 $60 $48 $-1 #\ncoedge $-1 $49";
  const size_t pos = sat.find(from);
  REQUIRE(pos != std::string::npos);
  sat.replace(pos, from.size(), to);
  const std::string endMarker = "End-of-ACIS-data";
  const size_t endPos = sat.find(endMarker);
  REQUIRE(endPos != std::string::npos);
  sat.insert(endPos, "loop $-1 $-1 $48 $-1 #\n");

  const acissat::ImportResult r = acissat::ImportSatSolid(sat, "");
  CHECK_FALSE(r.ok);
  CHECK(Contains(r.error, "hole loop"));
}

// GitHub issue #473 — a REAL ACIS SAT file, exported by Civil 3D's ACISOUT from a 4" weld-neck
// flange (samples/CJ_4in_WELD_NECK_FLANGE.sat). This is the first real ASM-authored SAT corpus
// this parser has: it exercises the real record schema (a `$attrib -1 $pattern` prefix on every
// record, edge parameter ranges, `I` bound markers, `@n` strings, interleaved `color-adesk-attrib`
// records) and a `body` `transform`. All 16 faces are plane/cone (a turned part), so it is within
// REQ-320's analytic-primitive scope.
TEST_CASE("ACIS SAT import: a real Civil 3D flange (.sat, ACISOUT) imports as a valid solid",
          "[acissat][issue473]") {
  const std::string path = std::string(GOSURVEY_TEST_DATA_DIR) + "/CJ_4in_WELD_NECK_FLANGE.sat";
  std::ifstream f(path, std::ios::binary);
  REQUIRE(f.is_open());
  std::ostringstream ss;
  ss << f.rdbuf();
  const std::string sat = ss.str();
  REQUIRE(sat.size() > 1000);

  const acissat::ImportResult r = acissat::ImportSatSolid(sat, "CJ_4in_WELD_NECK_FLANGE.sat");
  INFO("import error: " << r.error);
  REQUIRE(r.ok);
  CHECK(r.mmPerUnit == Catch::Approx(25.4));
  CHECK(r.solid.faces.size() == 16);
  CHECK(r.solid.shells.size() == 1);
  CHECK(brep::Validate(r.solid) == brep::Problem::Ok);

  int planeCount = 0, coneOrCylCount = 0;
  for (const brep::Face& fc : r.solid.faces) {
    if (fc.surface.kind == brep::SurfaceKind::Plane)
      ++planeCount;
    else if (fc.surface.kind == brep::SurfaceKind::Cylinder ||
             fc.surface.kind == brep::SurfaceKind::Cone)
      ++coneOrCylCount;
  }
  CHECK(planeCount == 4);
  CHECK(coneOrCylCount == 12);

  const brep::MassProperties mp = brep::ComputeMassProperties(r.solid);
  CHECK(mp.volume > 0.0);

  // It also tessellates — the multi-hole planar faces (bolt circle) go through the ADR-052
  // general-loop mesher, so this is what proves the renderer has triangles to draw.
  brep::Tessellation tess;
  brep::Problem tessWhy = brep::Problem::Ok;
  REQUIRE(brep::Tessellate(r.solid, 0.001, &tess, &tessWhy));
  CHECK(tess.indices.size() % 3 == 0);
  CHECK(tess.indices.size() > 60);   // 16 faces, several with holes

  // The body transform places the part near (4998.96, 4998.90) — its bounds must be there, not at
  // the origin where the raw ACIS geometry lives.
  const brep::Bounds b = brep::ComputeBounds(r.solid);
  REQUIRE(b.valid);
  CHECK(b.mn.x == Catch::Approx(4998.59457).margin(0.01));   // the body transform placed the part
  CHECK(b.mn.y == Catch::Approx(4998.52899).margin(0.01));
  CHECK(b.mx.z == Catch::Approx(0.25).margin(0.01));
}
