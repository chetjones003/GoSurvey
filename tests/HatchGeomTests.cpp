// Regression tests for filled-region (solid hatch) geometry helpers (commands/HatchGeom.hpp).
// These back REQ-042: clicking inside a hatch selects it (point-in-polygon, holes excluded),
// box-select uses the outer bounds, and MOVE translates every loop vertex. Pure compute — no
// UI/GL, so they run in the GoSurveyTests target without linking the command layer (ADR-002/016).

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <vector>

#include "commands/HatchGeom.hpp"
#include "commands/HatchBoundary.hpp"
#include "commands/HatchPat.hpp"
#include "commands/HatchPattern.hpp"

using hatchboundary::Seg;

namespace {

// A 10×10 outer square (0,0)-(10,10) with a 4×4 square hole (3,3)-(7,7).
CadFilledRegion SquareWithHole() {
  CadFilledRegion fr;
  fr.verts = {0, 0, 10, 0, 10, 10, 0, 10,   // outer loop (loop 0)
              3, 3, 7, 3, 7, 7, 3, 7};       // hole loop  (loop 1)
  fr.loopStart = {0, 4};
  return fr;
}

}  // namespace

TEST_CASE("ContainsPoint: inside outer and outside holes", "[hatch]") {
  const CadFilledRegion fr = SquareWithHole();
  // Inside the solid ring (outer, not in the hole).
  REQUIRE(hatchgeom::ContainsPoint(fr, 1.0, 5.0));
  REQUIRE(hatchgeom::ContainsPoint(fr, 5.0, 1.0));
  // Inside the hole → NOT contained (REQ-042: clicking a hole does not select the fill).
  REQUIRE_FALSE(hatchgeom::ContainsPoint(fr, 5.0, 5.0));
  // Outside the outer boundary.
  REQUIRE_FALSE(hatchgeom::ContainsPoint(fr, 11.0, 5.0));
  REQUIRE_FALSE(hatchgeom::ContainsPoint(fr, -1.0, -1.0));
}

TEST_CASE("OuterAreaAbs ignores winding and holes", "[hatch]") {
  const CadFilledRegion fr = SquareWithHole();
  REQUIRE(hatchgeom::OuterAreaAbs(fr) == 100.0);  // outer loop area, hole not subtracted

  // Reverse the outer winding — area magnitude is unchanged.
  CadFilledRegion cw;
  cw.verts = {0, 0, 0, 10, 10, 10, 10, 0};
  cw.loopStart = {0};
  REQUIRE(hatchgeom::OuterAreaAbs(cw) == 100.0);
}

TEST_CASE("Smallest enclosing region wins on overlap (pick priority)", "[hatch]") {
  // The pick logic in PickFilledRegionAt prefers the smallest-area region that contains the point.
  std::vector<CadFilledRegion> regions;
  CadFilledRegion big;
  big.verts = {0, 0, 20, 0, 20, 20, 0, 20};
  big.loopStart = {0};
  CadFilledRegion small;
  small.verts = {5, 5, 9, 5, 9, 9, 5, 9};
  small.loopStart = {0};
  regions.push_back(big);
  regions.push_back(small);

  int best = -1;
  double bestArea = 0.0;
  for (size_t i = 0; i < regions.size(); ++i) {
    if (!hatchgeom::ContainsPoint(regions[i], 7.0, 7.0))
      continue;
    const double area = hatchgeom::OuterAreaAbs(regions[i]);
    if (best < 0 || area < bestArea) {
      best = static_cast<int>(i);
      bestArea = area;
    }
  }
  REQUIRE(best == 1);  // the small region, though both contain (7,7)
}

TEST_CASE("OuterBounds reports the outer-loop AABB", "[hatch]") {
  const CadFilledRegion fr = SquareWithHole();
  float mnX = 0, mnY = 0, mxX = 0, mxY = 0;
  REQUIRE(hatchgeom::OuterBounds(fr, &mnX, &mnY, &mxX, &mxY));
  REQUIRE(mnX == 0.f);
  REQUIRE(mnY == 0.f);
  REQUIRE(mxX == 10.f);
  REQUIRE(mxY == 10.f);

  CadFilledRegion degenerate;  // fewer than 3 verts → no bounds
  degenerate.verts = {1, 1, 2, 2};
  degenerate.loopStart = {0};
  REQUIRE_FALSE(hatchgeom::OuterBounds(degenerate, &mnX, &mnY, &mxX, &mxY));
}

TEST_CASE("Translate moves every loop vertex and preserves containment", "[hatch]") {
  CadFilledRegion fr = SquareWithHole();
  hatchgeom::Translate(fr, 100.f, -50.f);

  // Outer first vertex moved by the delta.
  REQUIRE(fr.verts[0] == 100.f);
  REQUIRE(fr.verts[1] == -50.f);
  // Hole vertex moved too (loop 1 starts at pair index 4 → floats 8,9).
  REQUIRE(fr.verts[8] == 103.f);
  REQUIRE(fr.verts[9] == -47.f);

  // A point that was inside the ring before is inside the translated ring at the shifted location.
  REQUIRE(hatchgeom::ContainsPoint(fr, 101.0, -45.0));
  REQUIRE_FALSE(hatchgeom::ContainsPoint(fr, 1.0, 5.0));  // old location no longer covered
}

namespace {

// Four line segments forming the rectangle (x0,y0)-(x1,y1).
std::vector<Seg> Rect(float x0, float y0, float x1, float y1) {
  return {{x0, y0, x1, y0}, {x1, y0, x1, y1}, {x1, y1, x0, y1}, {x0, y1, x0, y0}};
}

}  // namespace

TEST_CASE("Boundary trace: closed rectangle around the seed", "[hatch][boundary]") {
  const std::vector<Seg> segs = Rect(0, 0, 10, 10);
  std::vector<float> loop;
  REQUIRE(hatchboundary::TraceEnclosingLoop(segs, 5.0, 5.0, &loop));
  REQUIRE(loop.size() == 8);  // four corners
  REQUIRE(hatchboundary::detail::LoopContains(loop, 5.0, 5.0));
  REQUIRE(hatchboundary::detail::LoopAreaAbs(loop) == Catch::Approx(100.0));
}

TEST_CASE("Boundary trace: open boundary (gap) finds no region", "[hatch][boundary]") {
  // Rectangle missing its top edge — no closed loop encloses the seed (REQ-201: place nothing).
  std::vector<Seg> segs = {{0, 0, 10, 0}, {10, 0, 10, 10}, {0, 10, 0, 0}};
  std::vector<float> loop;
  REQUIRE_FALSE(hatchboundary::TraceEnclosingLoop(segs, 5.0, 5.0, &loop));
}

TEST_CASE("Boundary trace: seed outside all geometry finds nothing", "[hatch][boundary]") {
  const std::vector<Seg> segs = Rect(0, 0, 10, 10);
  std::vector<float> loop;
  REQUIRE_FALSE(hatchboundary::TraceEnclosingLoop(segs, 50.0, 50.0, &loop));
}

TEST_CASE("Boundary trace: nested squares pick the smallest enclosing loop", "[hatch][boundary]") {
  std::vector<Seg> segs = Rect(0, 0, 10, 10);
  const std::vector<Seg> inner = Rect(3, 3, 7, 7);
  segs.insert(segs.end(), inner.begin(), inner.end());

  // Seed inside the inner square → traces the inner square (area 16).
  std::vector<float> loop;
  REQUIRE(hatchboundary::TraceEnclosingLoop(segs, 5.0, 5.0, &loop));
  REQUIRE(hatchboundary::detail::LoopAreaAbs(loop) == Catch::Approx(16.0));

  // Seed between the squares → the inner square does not contain it, so the outer square wins (area 100).
  std::vector<float> loop2;
  REQUIRE(hatchboundary::TraceEnclosingLoop(segs, 1.0, 5.0, &loop2));
  REQUIRE(hatchboundary::detail::LoopAreaAbs(loop2) == Catch::Approx(100.0));
}

namespace {
// A minimal ANSI31-like definition: one solid family at 45°, 3-unit perpendicular spacing.
hatchpat::Def Ansi31Def() {
  hatchpat::Def d;
  d.name = "ANSI31";
  hatchpat::Line ln;
  ln.angleDeg = 45.0;
  ln.dy = 3.0;
  d.lines.push_back(ln);
  return d;
}
}  // namespace

TEST_CASE("Pattern generation: solid region yields no segments", "[hatch][pattern]") {
  CadFilledRegion fr;
  fr.verts = {0, 0, 10, 0, 10, 10, 0, 10};
  fr.loopStart = {0};
  // default patternName empty → solid
  std::vector<float> segs;
  REQUIRE(hatchpattern::BuildSegments(fr, Ansi31Def(), &segs) == 0);
  REQUIRE(segs.empty());
}

TEST_CASE("Pattern generation: line family is clipped inside the region", "[hatch][pattern]") {
  CadFilledRegion fr;
  fr.verts = {0, 0, 10, 0, 10, 10, 0, 10};
  fr.loopStart = {0};
  fr.patternName = "ANSI31";  // single 45° family
  fr.patternScale = 1.f;
  std::vector<float> segs;
  const int n = hatchpattern::BuildSegments(fr, Ansi31Def(), &segs);
  REQUIRE(n > 0);
  REQUIRE(segs.size() == static_cast<size_t>(n) * 4);
  // Every emitted endpoint lies within the region bounds (clipped), with a small tolerance.
  for (size_t i = 0; i + 1 < segs.size(); i += 2) {
    REQUIRE(segs[i] >= -1e-3f);
    REQUIRE(segs[i] <= 10.f + 1e-3f);
    REQUIRE(segs[i + 1] >= -1e-3f);
    REQUIRE(segs[i + 1] <= 10.f + 1e-3f);
  }
  // Each emitted segment midpoint is inside the solid region (true for a convex square with no holes).
  for (size_t s = 0; s + 3 < segs.size(); s += 4) {
    const double mx = 0.5 * (segs[s] + segs[s + 2]);
    const double my = 0.5 * (segs[s + 1] + segs[s + 3]);
    REQUIRE(hatchgeom::ContainsPoint(fr, mx, my));
  }
}

TEST_CASE("Pattern generation: a hole carves gaps (midpoints avoid the hole)", "[hatch][pattern]") {
  CadFilledRegion fr;
  fr.verts = {0, 0, 20, 0, 20, 20, 0, 20,    // outer
              7, 7, 13, 7, 13, 13, 7, 13};   // hole
  fr.loopStart = {0, 4};
  fr.patternName = "ANSI31";
  std::vector<float> segs;
  REQUIRE(hatchpattern::BuildSegments(fr, Ansi31Def(), &segs) > 0);
  // No emitted segment midpoint falls inside the hole (even-odd clipping respects islands).
  for (size_t s = 0; s + 3 < segs.size(); s += 4) {
    const double mx = 0.5 * (segs[s] + segs[s + 2]);
    const double my = 0.5 * (segs[s + 1] + segs[s + 3]);
    const bool inHole = (mx > 7.0 && mx < 13.0 && my > 7.0 && my < 13.0);
    REQUIRE_FALSE(inHole);
  }
}

TEST_CASE("Pattern generation: larger scale produces fewer lines", "[hatch][pattern]") {
  CadFilledRegion fr;
  fr.verts = {0, 0, 40, 0, 40, 40, 0, 40};
  fr.loopStart = {0};
  fr.patternName = "ANSI31";
  fr.patternScale = 1.f;
  std::vector<float> dense;
  const int nDense = hatchpattern::BuildSegments(fr, Ansi31Def(), &dense);
  fr.patternScale = 3.f;
  std::vector<float> sparse;
  const int nSparse = hatchpattern::BuildSegments(fr, Ansi31Def(), &sparse);
  REQUIRE(nSparse < nDense);
}

TEST_CASE("PAT parser: names, descriptions, family lines, dashes", "[hatch][patparse]") {
  const std::string text =
      ";; a comment\n"
      "*SOLID, Solid fill\n"
      "45, 0,0, 0,.125\n"
      "*ANSI31,ANSI Iron, Brick, Stone masonry\n"
      "45, 0, 0, 0, 3.175 \n"
      "*DASHED, dashed demo\n"
      "0, 0,0, 0,5, 6.35,-3.175\n";
  std::vector<hatchpat::Def> defs;
  REQUIRE(hatchpat::Parse(text, &defs) == 3);

  const hatchpat::Def* solid = hatchpat::Find(defs, "solid");  // case-insensitive
  REQUIRE(solid != nullptr);
  REQUIRE(solid->description == "Solid fill");
  REQUIRE(solid->lines.size() == 1);

  const hatchpat::Def* ansi = hatchpat::Find(defs, "ANSI31");
  REQUIRE(ansi != nullptr);
  REQUIRE(ansi->description == "ANSI Iron, Brick, Stone masonry");  // commas in description preserved
  REQUIRE(ansi->lines.size() == 1);
  REQUIRE(ansi->lines[0].angleDeg == Catch::Approx(45.0));
  REQUIRE(ansi->lines[0].dy == Catch::Approx(3.175));
  REQUIRE(ansi->lines[0].dashes.empty());

  const hatchpat::Def* dash = hatchpat::Find(defs, "DASHED");
  REQUIRE(dash != nullptr);
  REQUIRE(dash->lines[0].dashes.size() == 2);
  REQUIRE(dash->lines[0].dashes[0] == Catch::Approx(6.35));
  REQUIRE(dash->lines[0].dashes[1] == Catch::Approx(-3.175));
}
