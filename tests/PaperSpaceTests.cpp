#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>

#include "commands/PaperSpace.hpp"

// REQ-026: a layout's orientation maps its portrait dimensions onto the sheet.
TEST_CASE("PaperLayout orientation maps portrait dims to the sheet", "[paperspace]") {
  PaperLayout L;
  L.portraitWidthIn = 24.f;
  L.portraitHeightIn = 36.f;

  L.landscape = false;
  REQUIRE(L.sheetWidthIn() == 24.f);
  REQUIRE(L.sheetHeightIn() == 36.f);

  L.landscape = true;
  REQUIRE(L.sheetWidthIn() == 36.f);
  REQUIRE(L.sheetHeightIn() == 24.f);
}

// REQ-026: the preset table is well-formed (portrait orientation: width <= height) and the
// default index is in range.
TEST_CASE("Paper size presets are well-formed", "[paperspace]") {
  REQUIRE(kPaperSizePresetCount > 0);
  for (int i = 0; i < kPaperSizePresetCount; ++i) {
    REQUIRE(kPaperSizePresets[i].name != nullptr);
    REQUIRE(kPaperSizePresets[i].widthIn > 0.f);
    REQUIRE(kPaperSizePresets[i].heightIn >= kPaperSizePresets[i].widthIn);
  }
  REQUIRE(kDefaultPaperPresetIdx >= 0);
  REQUIRE(kDefaultPaperPresetIdx < kPaperSizePresetCount);
}

// REQ-027: the model→paper transform maps the viewport's model center to the rect center, and a
// model point one scale-unit away maps one paper inch away.
TEST_CASE("Viewport model->paper transform", "[paperspace]") {
  Viewport vp;
  vp.paperXIn = 2.f;
  vp.paperYIn = 1.f;
  vp.paperWIn = 10.f;
  vp.paperHIn = 6.f;
  vp.modelCenterX = 1000.0;
  vp.modelCenterY = 500.0;
  vp.scaleModelPerPaperIn = 50.f;  // 50 model units per paper inch

  float px = 0.f, py = 0.f;
  ModelToPaperIn(vp, 1000.0, 500.0, &px, &py);  // model center → rect center
  REQUIRE(px == 7.f);   // 2 + 10/2
  REQUIRE(py == 4.f);   // 1 + 6/2

  ModelToPaperIn(vp, 1050.0, 500.0, &px, &py);  // +50 model units (= +1 paper inch) in X
  REQUIRE(px == 8.f);
  REQUIRE(py == 4.f);
}

// Zero/invalid scale must not divide-by-zero; safeScale clamps it.
TEST_CASE("Viewport safeScale clamps non-positive scale", "[paperspace]") {
  Viewport vp;
  vp.scaleModelPerPaperIn = 0.f;
  REQUIRE(vp.safeScale() > 0.f);
  float px = 0.f, py = 0.f;
  ModelToPaperIn(vp, 5.0, 5.0, &px, &py);  // must be finite (no div-by-zero)
  REQUIRE(std::isfinite(px));
  REQUIRE(std::isfinite(py));
}

// REQ-028: per-viewport frozen layers default empty and can be toggled.
TEST_CASE("Viewport frozen layers toggle", "[paperspace]") {
  Viewport vp;
  REQUIRE(vp.frozenLayers.empty());

  // Initially no layers frozen
  REQUIRE_FALSE(IsLayerFrozenInViewport(vp, "0"));
  REQUIRE_FALSE(IsLayerFrozenInViewport(vp, "Layer1"));

  // Toggle a layer on
  ToggleFrozenLayerInViewport(vp, "Layer1");
  REQUIRE(vp.frozenLayers.size() == 1);
  REQUIRE(IsLayerFrozenInViewport(vp, "Layer1"));
  REQUIRE_FALSE(IsLayerFrozenInViewport(vp, "0"));

  // Add another layer
  ToggleFrozenLayerInViewport(vp, "Layer2");
  REQUIRE(vp.frozenLayers.size() == 2);
  REQUIRE(IsLayerFrozenInViewport(vp, "Layer1"));
  REQUIRE(IsLayerFrozenInViewport(vp, "Layer2"));

  // Toggle a frozen layer off
  ToggleFrozenLayerInViewport(vp, "Layer1");
  REQUIRE(vp.frozenLayers.size() == 1);
  REQUIRE_FALSE(IsLayerFrozenInViewport(vp, "Layer1"));
  REQUIRE(IsLayerFrozenInViewport(vp, "Layer2"));

  // Toggle off the last layer
  ToggleFrozenLayerInViewport(vp, "Layer2");
  REQUIRE(vp.frozenLayers.empty());
  REQUIRE_FALSE(IsLayerFrozenInViewport(vp, "Layer2"));
}

TEST_CASE("Paper-space object snap finds endpoints, midpoints, text (REQ-037)", "[paperspace]") {
  PaperLayout L;
  // One paper line from (0,0) to (10,0): endpoints (0,0),(10,0) and midpoint (5,0).
  const float seg[6] = {0.f, 0.f, 0.f, 10.f, 0.f, 0.f};
  L.paperLines.assign(seg, seg + 6);
  CadAnnotation t;
  t.insX = 3.f;
  t.insY = 4.f;
  t.text = "TB";
  L.paperTexts.push_back(t);

  float sx = 0.f, sy = 0.f;

  // Near an endpoint → snaps to it.
  REQUIRE(SnapPaperInchPoint(L, 0.1f, 0.05f, 0.5f, &sx, &sy));
  REQUIRE(sx == Catch::Approx(0.f));
  REQUIRE(sy == Catch::Approx(0.f));

  // Near the midpoint → snaps to (5,0).
  REQUIRE(SnapPaperInchPoint(L, 4.9f, 0.2f, 0.5f, &sx, &sy));
  REQUIRE(sx == Catch::Approx(5.f));
  REQUIRE(sy == Catch::Approx(0.f));

  // Near the text insertion point → snaps to (3,4).
  REQUIRE(SnapPaperInchPoint(L, 3.2f, 3.9f, 0.5f, &sx, &sy));
  REQUIRE(sx == Catch::Approx(3.f));
  REQUIRE(sy == Catch::Approx(4.f));

  // Far from everything (tol too small) → no snap, outputs untouched.
  sx = -99.f;
  sy = -99.f;
  REQUIRE_FALSE(SnapPaperInchPoint(L, 100.f, 100.f, 0.5f, &sx, &sy));
  REQUIRE(sx == Catch::Approx(-99.f));
  REQUIRE(sy == Catch::Approx(-99.f));
}

// REQ-038 / ADR-013: paper space now stores circles/arcs/ellipses/polylines; snapping finds their key points.
TEST_CASE("Paper-space object snap finds new primitive key points (REQ-038)", "[paperspace]") {
  PaperLayout L;
  // Circle center (20,20), r=5 → center + 4 quadrants.
  const float circ[3] = {20.f, 20.f, 5.f};
  L.paperCircles.assign(circ, circ + 3);
  // Arc center (0,0), r=10, start 0, sweep +90° → start point (10,0), end point (0,10), center (0,0).
  CadArc a;
  a.cx = 0.f; a.cy = 0.f; a.r = 10.f; a.startRad = 0.f; a.sweepRad = 1.57079633f;
  L.paperArcs.push_back(a);
  // Polyline with a vertex at (40,5).
  L.paperPolyOffsets = {0, 2};
  L.paperPolyVerts = {40.f, 5.f, 0.f, 45.f, 5.f, 0.f};
  L.paperPolyClosed = {0};

  float sx = 0.f, sy = 0.f;
  // Circle east quadrant (25,20).
  REQUIRE(SnapPaperInchPoint(L, 24.9f, 20.1f, 0.5f, &sx, &sy));
  REQUIRE(sx == Catch::Approx(25.f));
  REQUIRE(sy == Catch::Approx(20.f));
  // Arc end point (0,10) — cos(90°) leaves a tiny float residue in x, so use a margin.
  REQUIRE(SnapPaperInchPoint(L, 0.1f, 9.9f, 0.5f, &sx, &sy));
  REQUIRE(sx == Catch::Approx(0.f).margin(1e-4));
  REQUIRE(sy == Catch::Approx(10.f));
  // Polyline vertex (40,5).
  REQUIRE(SnapPaperInchPoint(L, 40.2f, 4.8f, 0.5f, &sx, &sy));
  REQUIRE(sx == Catch::Approx(40.f));
  REQUIRE(sy == Catch::Approx(5.f));
}

// REQ-039 (bug #2): the renderer anchors paper text at its insertion = TOP-LEFT, so the bounds occupy
// [insY - h, insY] in Y and [insX, insX + w] in X. Earlier (bottom-left) bounds put the glyphs above the
// insertion, mis-picking text by ~one line height.
TEST_CASE("Paper text bounds anchor at the top-left insertion (REQ-039)", "[paperspace]") {
  CadAnnotation t;
  t.insX = 3.f;
  t.insY = 4.f;
  t.plottedHeightInches = 0.5f;
  t.text = "AB";  // two glyphs → width = 0.6*h*2 = 0.6
  float x0, y0, x1, y1;
  PaperTextBoundsIn(t, &x0, &y0, &x1, &y1);
  REQUIRE(x0 == Catch::Approx(3.f));
  REQUIRE(y1 == Catch::Approx(4.f));        // top edge at the insertion Y
  REQUIRE(y0 == Catch::Approx(4.f - 0.5f)); // descends one line height below it
  REQUIRE(x1 == Catch::Approx(3.f + 0.6f));
  REQUIRE(y0 < y1);
}

// REQ-039 (bug #1): box-select hits each paper object type. Window (L→R) needs the whole extent inside;
// crossing (R→L) needs only an overlap.
TEST_CASE("Paper box-select selects each type by window/crossing rules (REQ-039)", "[paperspace]") {
  PaperLayout L;
  // Line from (1,1) to (3,3) — extent [1,3]x[1,3].
  const float seg[6] = {1.f, 1.f, 0.f, 3.f, 3.f, 0.f};
  L.paperLines.assign(seg, seg + 6);
  // Text at top-left (5,6), h=0.5 → bounds [5,5.3]x[5.5,6].
  CadAnnotation t; t.insX = 5.f; t.insY = 6.f; t.plottedHeightInches = 0.5f; t.text = "T";
  L.paperTexts.push_back(t);
  // Circle center (10,10) r=2 → bbox [8,12]x[8,12].
  const float circ[3] = {10.f, 10.f, 2.f};
  L.paperCircles.assign(circ, circ + 3);
  // Arc center (20,20) r=3 → bbox [17,23]x[17,23].
  CadArc a; a.cx = 20.f; a.cy = 20.f; a.r = 3.f; a.startRad = 0.f; a.sweepRad = 1.f;
  L.paperArcs.push_back(a);
  // Ellipse center (30,30) major (4,0) → bbox [26,34]x[26,34].
  CadEllipse e; e.cx = 30.f; e.cy = 30.f; e.majVx = 4.f; e.majVy = 0.f; e.ratio = 0.5f;
  L.paperEllipses.push_back(e);
  // Polyline verts (40,40)-(44,42) → bbox [40,44]x[40,42].
  L.paperPolyOffsets = {0, 2};
  L.paperPolyVerts = {40.f, 40.f, 0.f, 44.f, 42.f, 0.f};
  L.paperPolyClosed = {0};

  auto countType = [](const std::vector<PaperEntityRef>& v, PaperEntityRef::Type t) {
    int n = 0;
    for (const auto& r : v) if (r.type == t) ++n;
    return n;
  };

  // A window box [0,0]-[50,50] fully contains every object → all six selected.
  {
    std::vector<PaperEntityRef> out;
    SelectPaperEntitiesInBox(L, 0.f, 0.f, 50.f, 50.f, /*windowMode=*/true, out);
    REQUIRE(out.size() == 6);
    REQUIRE(countType(out, PaperEntityRef::Type::Line) == 1);
    REQUIRE(countType(out, PaperEntityRef::Type::Text) == 1);
    REQUIRE(countType(out, PaperEntityRef::Type::Circle) == 1);
    REQUIRE(countType(out, PaperEntityRef::Type::Arc) == 1);
    REQUIRE(countType(out, PaperEntityRef::Type::Ellipse) == 1);
    REQUIRE(countType(out, PaperEntityRef::Type::Polyline) == 1);
  }

  // A window box [9,9]-[11,11] cuts through the circle but does not contain it → window selects nothing.
  {
    std::vector<PaperEntityRef> out;
    SelectPaperEntitiesInBox(L, 9.f, 9.f, 11.f, 11.f, /*windowMode=*/true, out);
    REQUIRE(out.empty());
  }

  // The same box as a crossing selection (R→L) overlaps only the circle → exactly the circle.
  {
    std::vector<PaperEntityRef> out;
    SelectPaperEntitiesInBox(L, 9.f, 9.f, 11.f, 11.f, /*windowMode=*/false, out);
    REQUIRE(out.size() == 1);
    REQUIRE(out.front().type == PaperEntityRef::Type::Circle);
    REQUIRE(out.front().index == 0);
  }

  // A box far from everything selects nothing in either mode.
  {
    std::vector<PaperEntityRef> out;
    SelectPaperEntitiesInBox(L, 100.f, 100.f, 110.f, 110.f, /*windowMode=*/false, out);
    REQUIRE(out.empty());
    SelectPaperEntitiesInBox(L, 100.f, 100.f, 110.f, 110.f, /*windowMode=*/true, out);
    REQUIRE(out.empty());
  }
}
