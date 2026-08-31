#include "util/cadblock.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

TEST_CASE("Block geometry is stored relative to a baked base point", "[issue124][block]") {
  CadBlockDefinition def;
  def.name = "TREE";
  def.baseX = 100.f;
  def.baseY = 200.f;
  def.content.lines = {98.f, 200.f, 0.f, 102.f, 200.f, 0.f};
  CadBlockBakeBasePoint(&def);
  REQUIRE(def.baseX == 0.f);
  REQUIRE(def.baseY == 0.f);
  REQUIRE(def.content.lines.size() >= 6);
  CHECK(def.content.lines[0] == Catch::Approx(-2.f));
  CHECK(def.content.lines[1] == Catch::Approx(0.f));
  CHECK(def.content.lines[3] == Catch::Approx(2.f));
}

TEST_CASE("Two references share one definition and keep independent transforms", "[issue124][block]") {
  std::vector<CadBlockDefinition> defs(1);
  defs[0].name = "TREE";
  defs[0].content.lines = {-1.f, 0.f, 0.f, 1.f, 0.f, 0.f};
  CadBlockRef a;
  a.defName = "TREE";
  a.xf.x = 10.f;
  a.xf.y = 20.f;
  CadBlockRef b = a;
  b.xf.x = 50.f;
  b.xf.sx = 2.f;
  std::vector<CadBlockWorldSeg> sa;
  std::vector<CadBlockWorldSeg> sb;
  CadBlockCollectWorldLines(defs, a, EntityAttributes{}, &sa);
  CadBlockCollectWorldLines(defs, b, EntityAttributes{}, &sb);
  REQUIRE_FALSE(sa.empty());
  REQUIRE_FALSE(sb.empty());
  CHECK(sa[0].x0 == Catch::Approx(9.f));
  CHECK(sb[0].x0 == Catch::Approx(48.f));
}

TEST_CASE("Circular nested blocks are refused", "[issue124][block]") {
  std::vector<CadBlockDefinition> defs(2);
  defs[0].name = "A";
  defs[1].name = "B";
  CadBlockNested n;
  n.defName = "A";
  defs[1].content.nested.push_back(n);
  CHECK(CadBlockWouldCycle(defs, "A", "B"));
  CHECK(CadBlockWouldCycle(defs, "A", "A"));
  CHECK_FALSE(CadBlockWouldCycle(defs, "B", "A"));
}

TEST_CASE("Inch-to-foot insert scale is 1/12", "[issue124][block]") {
  CHECK(CadBlockUnitsScale("inches", "feet") == Catch::Approx(1.f / 12.f));
}

TEST_CASE("ByBlock color resolves from the insert", "[issue124][block]") {
  EntityAttributes prim;
  prim.color = "ByBlock";
  EntityAttributes insert;
  insert.color = "1";
  const EntityAttributes r = CadBlockResolveAttr(prim, insert);
  CHECK(r.color == "1");
}

TEST_CASE("Mirror of a reference flips insertion across the axis", "[issue124][block]") {
  CadBlockRef r;
  r.xf.x = 4.f;
  r.xf.y = 1.f;
  CadBlockMirror(&r, 0.f, 0.f, 0.f, 10.f);
  CHECK(r.xf.x == Catch::Approx(-4.f));
  CHECK(r.xf.sx < 0.f);
}

TEST_CASE("XYZ insertion and 3D scale apply to collected segments", "[issue124][block]") {
  std::vector<CadBlockDefinition> defs(1);
  defs[0].name = "POST";
  defs[0].content.lines = {0.f, 0.f, 0.f, 0.f, 0.f, 2.f};
  CadBlockRef r;
  r.defName = "POST";
  r.xf.x = 1.f;
  r.xf.y = 2.f;
  r.xf.z = 10.f;
  r.xf.sz = 3.f;
  std::vector<CadBlockWorldSeg> segs;
  CadBlockCollectWorldLines(defs, r, EntityAttributes{}, &segs);
  REQUIRE_FALSE(segs.empty());
  CHECK(segs[0].z0 == Catch::Approx(10.f));
  CHECK(segs[0].z1 == Catch::Approx(16.f));
}

TEST_CASE("Attribute values differ per reference", "[issue124][block]") {
  CadBlockDefinition def;
  def.name = "MH";
  CadBlockAttrDef ad;
  ad.tag = "ID";
  ad.defaultValue = "0";
  def.attrDefs.push_back(ad);
  CadBlockRef a;
  a.defName = "MH";
  CadBlockRef b = a;
  CadBlockAttrSet(&a, "ID", "A1");
  CadBlockAttrSet(&b, "ID", "B2");
  CHECK(CadBlockAttrGet(a, def, "ID") == "A1");
  CHECK(CadBlockAttrGet(b, def, "ID") == "B2");
}

TEST_CASE("Visibility state hides unmatched primitives", "[issue124][block]") {
  std::vector<CadBlockDefinition> defs(1);
  defs[0].name = "GATE";
  defs[0].content.lines = {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f};
  defs[0].content.lineVis = {"OPEN", "SHUT"};
  CadBlockRef r;
  r.defName = "GATE";
  r.visState = "OPEN";
  std::vector<CadBlockWorldSeg> segs;
  CadBlockCollectWorldLines(defs, r, EntityAttributes{}, &segs);
  REQUIRE(segs.size() == 1);
}

TEST_CASE("Matchline dynamics stretch the negative end and flip labels", "[issue124][block]") {
  CadBlockDefinition def;
  def.name = "_matchline_NORTHING";
  def.content.lines = {0.f, -2.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 2.f, 0.f};
  CadBlockAttrDef sheet;
  sheet.tag = "N#";
  sheet.localX = 0.2f;
  sheet.localY = -0.5f;
  def.attrDefs.push_back(sheet);
  CadBlockAttrDef north;
  north.tag = "NORTHING";
  north.localX = 0.2f;
  north.localY = 0.4f;
  def.attrDefs.push_back(north);
  CadBlockAuthorMatchlineDynamics(&def);
  REQUIRE(CadBlockHasMatchlineDyn(def));
  CHECK(CadBlockDynGripCount(def) == kCadBlockDynGripCount);
  CHECK(CadBlockDynGripShapeOf(1) == CadBlockDynGripShape::StretchArrow);
  CHECK(CadBlockDynGripShapeOf(4) == CadBlockDynGripShape::OffsetTriangle);
  CHECK(CadBlockDynGripShapeOf(3) == CadBlockDynGripShape::FlipArrow);
  CHECK(CadBlockDynGripShownOnInsert(0));
  CHECK(CadBlockDynGripShownOnInsert(3));

  CadBlockRef r;
  r.defName = def.name;
  std::vector<CadBlockDefinition> defs;
  defs.push_back(def);
  std::vector<CadBlockWorldSeg> segs;
  CadBlockCollectWorldLines(defs, r, EntityAttributes{}, &segs);
  REQUIRE(segs.size() == 2);
  float yMin = 0.f, yMax = 0.f;
  CadBlockLineYExtent(def, &yMin, &yMax);
  CHECK(yMin == Catch::Approx(-2.f));

  CadBlockParamSet(&r, "DistNeg", 1.f);
  segs.clear();
  CadBlockCollectWorldLines(defs, r, EntityAttributes{}, &segs);
  float minY = 1.e9f;
  for (const CadBlockWorldSeg& s : segs)
    minY = std::min(minY, std::min(s.y0, s.y1));
  CHECK(minY == Catch::Approx(-3.f));

  std::vector<CadAnnotation> annsAtStretch;
  CadBlockCollectWorldAnnotations(defs, r, &annsAtStretch);
  bool sheetStayed = false;
  for (const CadAnnotation& a : annsAtStretch) {
    if (std::fabs(a.insY + 0.5f) < 0.02f)
      sheetStayed = true;
  }
  CHECK(sheetStayed);

  CadBlockParamSet(&r, "Flip", 1.f);
  std::vector<CadAnnotation> anns;
  CadBlockCollectWorldAnnotations(defs, r, &anns);
  REQUIRE(anns.size() >= 2);
  bool sawFlippedSheet = false;
  for (const CadAnnotation& a : anns) {
    if (a.insX < 0.f)
      sawFlippedSheet = true;
  }
  CHECK(sawFlippedSheet);
}

TEST_CASE("annotation overlay is active for a drawing that only has block INSERTs", "[issue124][block]") {
  CHECK_FALSE(CadNeedsAnnotationOverlay(0, 0, 0, false, false));
  CHECK(CadNeedsAnnotationOverlay(0, 0, 1, false, false));
  CHECK(CadNeedsAnnotationOverlay(1, 0, 0, false, false));
}
