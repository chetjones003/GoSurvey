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

// REQ-028 acceptance: a layer frozen in one viewport is hidden ONLY in that viewport — it stays
// visible in other viewports (and in model space, which never consults a viewport's frozen set).
// This is the predicate both the on-screen viewport render (CadUi) and the PDF plot (PdfPlot) apply.
TEST_CASE("Frozen layer is hidden only in its own viewport (REQ-028)", "[paperspace]") {
  Viewport vpA;  // freeze "Utilities" here
  Viewport vpB;  // leave everything thawed here
  ToggleFrozenLayerInViewport(vpA, "Utilities");

  // Hidden in vpA, visible in vpB.
  REQUIRE(IsLayerFrozenInViewport(vpA, "Utilities"));
  REQUIRE_FALSE(IsLayerFrozenInViewport(vpB, "Utilities"));

  // A different layer is untouched in both viewports.
  REQUIRE_FALSE(IsLayerFrozenInViewport(vpA, "Boundary"));
  REQUIRE_FALSE(IsLayerFrozenInViewport(vpB, "Boundary"));

  // Freezing the same layer in vpB is independent of vpA (thawing one leaves the other frozen).
  ToggleFrozenLayerInViewport(vpB, "Utilities");
  REQUIRE(IsLayerFrozenInViewport(vpB, "Utilities"));
  ToggleFrozenLayerInViewport(vpA, "Utilities");  // thaw in A only
  REQUIRE_FALSE(IsLayerFrozenInViewport(vpA, "Utilities"));
  REQUIRE(IsLayerFrozenInViewport(vpB, "Utilities"));
}

// REQ-046: VPFREEZE freezes (idempotent add) and VPTHAW thaws (remove) a layer in a viewport.
TEST_CASE("VPFREEZE freezes and VPTHAW thaws a layer in a viewport (REQ-046)", "[paperspace]") {
  Viewport vp;
  FreezeLayerInViewport(vp, "Grid");
  REQUIRE(IsLayerFrozenInViewport(vp, "Grid"));
  REQUIRE(vp.frozenLayers.size() == 1);
  FreezeLayerInViewport(vp, "Grid");  // idempotent — no duplicate
  REQUIRE(vp.frozenLayers.size() == 1);
  ThawLayerInViewport(vp, "Grid");
  REQUIRE_FALSE(IsLayerFrozenInViewport(vp, "Grid"));
  ThawLayerInViewport(vp, "Grid");  // thawing an already-thawed layer is a no-op
  REQUIRE(vp.frozenLayers.empty());
}

// REQ-046: per-viewport layer COLOR override — set/replace, query, clear, keeping the arrays parallel.
TEST_CASE("Per-viewport layer color override set/replace/clear (REQ-046)", "[paperspace]") {
  Viewport vp;
  REQUIRE(ViewportLayerColorOverride(vp, "Roads") == nullptr);

  SetViewportLayerColor(vp, "Roads", "Red");
  const std::string* c = ViewportLayerColorOverride(vp, "Roads");
  REQUIRE(c != nullptr);
  REQUIRE(*c == "Red");
  REQUIRE(vp.vpColorLayers.size() == 1);
  REQUIRE(vp.vpColorValues.size() == 1);

  // Setting the same layer again replaces the color, not appends.
  SetViewportLayerColor(vp, "Roads", "Blue");
  REQUIRE(vp.vpColorLayers.size() == 1);
  REQUIRE(*ViewportLayerColorOverride(vp, "Roads") == "Blue");

  // A second layer is independent.
  SetViewportLayerColor(vp, "Water", "Cyan");
  REQUIRE(vp.vpColorLayers.size() == 2);
  REQUIRE(*ViewportLayerColorOverride(vp, "Water") == "Cyan");
  REQUIRE(*ViewportLayerColorOverride(vp, "Roads") == "Blue");

  // Clearing removes one entry and keeps the parallel arrays in step.
  ClearViewportLayerColor(vp, "Roads");
  REQUIRE(ViewportLayerColorOverride(vp, "Roads") == nullptr);
  REQUIRE(vp.vpColorLayers.size() == 1);
  REQUIRE(vp.vpColorValues.size() == 1);
  REQUIRE(*ViewportLayerColorOverride(vp, "Water") == "Cyan");
}

TEST_CASE("Per-viewport color override is independent per viewport (REQ-046)", "[paperspace]") {
  Viewport a, b;
  SetViewportLayerColor(a, "Contours", "Green");
  REQUIRE(*ViewportLayerColorOverride(a, "Contours") == "Green");
  REQUIRE(ViewportLayerColorOverride(b, "Contours") == nullptr);  // untouched in the other viewport
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
  // Two glyphs → width = kCadTextAdvanceFactor * h * 2 = 0.55 * 0.5 * 2 = 0.55. Paper used its own
  // 0.6 until TASK-198; model and paper now share one factor, so a string of a given height reports
  // the same width on a sheet as it does in the model — REQ-039's parity, in the one place it was
  // measurably absent.
  t.text = "AB";
  float x0, y0, x1, y1;
  PaperTextBoundsIn(t, &x0, &y0, &x1, &y1);
  REQUIRE(x0 == Catch::Approx(3.f));
  REQUIRE(y1 == Catch::Approx(4.f));        // top edge at the insertion Y
  REQUIRE(y0 == Catch::Approx(4.f - 0.5f)); // descends one line height below it
  REQUIRE(x1 == Catch::Approx(3.f + 0.55f));
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

  // The same box as a crossing selection (R→L). It lies ENTIRELY INSIDE the circle — centre (10,10),
  // r=2, and the box's farthest corner is sqrt(2) = 1.41 away — so it touches no drawn geometry and
  // selects nothing. Until 2026-09-03 this required the circle instead: the box test was the circle's
  // bounding SQUARE, which is solid, so a fence in the hollow middle hit. See the "against actual
  // geometry" case below, and TASK-197.
  {
    std::vector<PaperEntityRef> out;
    SelectPaperEntitiesInBox(L, 9.f, 9.f, 11.f, 11.f, /*windowMode=*/false, out);
    REQUIRE(out.empty());
  }

  // A crossing box that genuinely straddles the rim — the same intent the case above used to carry.
  // Centre (10,10) r=2: (11,11) is 1.41 from the centre (inside) and (13,13) is 4.24 (outside).
  {
    std::vector<PaperEntityRef> out;
    SelectPaperEntitiesInBox(L, 11.f, 11.f, 13.f, 13.f, /*windowMode=*/false, out);
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

// TASK-197 — REQ-039 acceptance (1): a crossing box selects a paper object it TOUCHES, and a window
// box one it ENCLOSES. Every type used to be tested by its bounding box, which answers a different
// question for anything that is not a filled rectangle. Each miss below failed before the fix.
//
// Every MISS is paired with a HIT on the same entity, because a box test that selected nothing would
// pass every miss on its own.
TEST_CASE("Paper box-select tests the object, not a box round it (REQ-039, TASK-197)", "[paperspace]") {
  auto n = [](const std::vector<PaperEntityRef>& v) { return static_cast<int>(v.size()); };

  SECTION("line: a diagonal is not selected from the corner of its bounding square") {
    PaperLayout L;
    const float seg[6] = {0.f, 0.f, 0.f, 10.f, 10.f, 0.f};  // (0,0)-(10,10)
    L.paperLines.assign(seg, seg + 6);
    std::vector<PaperEntityRef> out;
    // Inside the bbox [0,10]^2, ~5 in clear of the line y = x.
    SelectPaperEntitiesInBox(L, 8.f, 1.f, 9.f, 2.f, /*windowMode=*/false, out);
    REQUIRE(n(out) == 0);
    // Straddling the line itself.
    SelectPaperEntitiesInBox(L, 4.f, 5.f, 6.f, 7.f, /*windowMode=*/false, out);
    REQUIRE(n(out) == 1);
    REQUIRE(out.front().type == PaperEntityRef::Type::Line);
    // Window still needs the whole segment inside - unchanged by this fix, asserted so it stays so.
    out.clear();
    SelectPaperEntitiesInBox(L, 4.f, 5.f, 6.f, 7.f, /*windowMode=*/true, out);
    REQUIRE(n(out) == 0);
    SelectPaperEntitiesInBox(L, -1.f, -1.f, 11.f, 11.f, /*windowMode=*/true, out);
    REQUIRE(n(out) == 1);
  }

  SECTION("circle: not from the enclosing square's corner, not from the hollow middle") {
    PaperLayout L;
    const float circ[3] = {0.f, 0.f, 10.f};
    L.paperCircles.assign(circ, circ + 3);
    std::vector<PaperEntityRef> out;
    // |(7.5,7.5)| = 10.6 > 10 - the whole box is outside the circle.
    SelectPaperEntitiesInBox(L, 7.5f, 7.5f, 8.5f, 8.5f, /*windowMode=*/false, out);
    REQUIRE(n(out) == 0);
    // Floating in the middle: a circle is a curve, not a disc.
    SelectPaperEntitiesInBox(L, -1.f, -1.f, 1.f, 1.f, /*windowMode=*/false, out);
    REQUIRE(n(out) == 0);
    // Straddling the rim at (10,0).
    SelectPaperEntitiesInBox(L, 9.f, -1.f, 11.f, 1.f, /*windowMode=*/false, out);
    REQUIRE(n(out) == 1);
    REQUIRE(out.front().type == PaperEntityRef::Type::Circle);
  }

  SECTION("arc: the sweep decides, not the full circle's square") {
    PaperLayout L;
    CadArc a;  // quarter circle, centre (0,0) r=10, 0 to 90 deg - the upper-RIGHT quadrant only
    a.cx = 0.f; a.cy = 0.f; a.r = 10.f; a.startRad = 0.f; a.sweepRad = 1.5707963f;
    L.paperArcs.push_back(a);
    std::vector<PaperEntityRef> out;
    // On the full circle, three quadrants away from this arc.
    SelectPaperEntitiesInBox(L, -8.f, -8.f, -6.f, -6.f, /*windowMode=*/false, out);
    REQUIRE(n(out) == 0);
    // Under the arc's own span, in the empty region its bounding box covers.
    SelectPaperEntitiesInBox(L, 1.f, 1.f, 3.f, 3.f, /*windowMode=*/false, out);
    REQUIRE(n(out) == 0);
    // Straddling the crown at (0,10).
    SelectPaperEntitiesInBox(L, -1.f, 9.f, 1.f, 11.f, /*windowMode=*/false, out);
    REQUIRE(n(out) == 1);
    REQUIRE(out.front().type == PaperEntityRef::Type::Arc);
    // A window box round the ARC - not round the whole circle - now selects it. The old test wanted
    // the full circle's square inside, so a box this size selected nothing.
    out.clear();
    SelectPaperEntitiesInBox(L, -1.f, -1.f, 11.f, 11.f, /*windowMode=*/true, out);
    REQUIRE(n(out) == 1);
  }

  SECTION("ellipse: the ratio counts") {
    PaperLayout L;
    CadEllipse e;  // semi-major 10 along X, ratio 0.1 -> semi-minor 1. Real extent [-10,10]x[-1,1].
    e.cx = 0.f; e.cy = 0.f; e.majVx = 10.f; e.majVy = 0.f; e.ratio = 0.1f;
    L.paperEllipses.push_back(e);
    std::vector<PaperEntityRef> out;
    // Five times the ellipse's own half-height above it - inside the square the old test used.
    SelectPaperEntitiesInBox(L, -1.f, 5.f, 1.f, 6.f, /*windowMode=*/false, out);
    REQUIRE(n(out) == 0);
    // The hollow middle.
    SelectPaperEntitiesInBox(L, -1.f, -0.2f, 1.f, 0.2f, /*windowMode=*/false, out);
    REQUIRE(n(out) == 0);
    // Straddling the minor-axis end at (0,1).
    SelectPaperEntitiesInBox(L, -1.f, 0.5f, 1.f, 1.5f, /*windowMode=*/false, out);
    REQUIRE(n(out) == 1);
    REQUIRE(out.front().type == PaperEntityRef::Type::Ellipse);
    // A window box round the true extent selects it; a square of side 2*major does not fit here.
    out.clear();
    SelectPaperEntitiesInBox(L, -11.f, -2.f, 11.f, 2.f, /*windowMode=*/true, out);
    REQUIRE(n(out) == 1);
  }

  SECTION("polyline: the empty inside of an L is empty") {
    PaperLayout L;
    // (0,0) -> (10,0) -> (10,10): an L. Its bbox is [0,10]^2; the whole upper-left is empty.
    L.paperPolyOffsets = {0, 3};
    L.paperPolyVerts = {0.f, 0.f, 0.f, 10.f, 0.f, 0.f, 10.f, 10.f, 0.f};
    L.paperPolyClosed = {0};
    std::vector<PaperEntityRef> out;
    SelectPaperEntitiesInBox(L, 1.f, 8.f, 3.f, 9.f, /*windowMode=*/false, out);
    REQUIRE(n(out) == 0);
    // Straddling the horizontal leg.
    SelectPaperEntitiesInBox(L, 4.f, -1.f, 6.f, 1.f, /*windowMode=*/false, out);
    REQUIRE(n(out) == 1);
    REQUIRE(out.front().type == PaperEntityRef::Type::Polyline);
  }

  SECTION("polyline: a CLOSED chain's closing segment selects too") {
    PaperLayout L;
    // A triangle (0,0)-(10,0)-(10,10), closed: the closing leg runs (10,10) back to (0,0).
    L.paperPolyOffsets = {0, 3};
    L.paperPolyVerts = {0.f, 0.f, 0.f, 10.f, 0.f, 0.f, 10.f, 10.f, 0.f};
    L.paperPolyClosed = {1};
    std::vector<PaperEntityRef> out;
    // Straddling the hypotenuse at its midpoint (5,5) - a segment that exists only because it closes.
    SelectPaperEntitiesInBox(L, 4.f, 4.f, 6.f, 6.f, /*windowMode=*/false, out);
    REQUIRE(n(out) == 1);
    REQUIRE(out.front().type == PaperEntityRef::Type::Polyline);
    // The open version of the same chain is NOT selected there - proof the closing leg is what hit.
    L.paperPolyClosed = {0};
    out.clear();
    SelectPaperEntitiesInBox(L, 4.f, 4.f, 6.f, 6.f, /*windowMode=*/false, out);
    REQUIRE(n(out) == 0);
  }

  SECTION("block: its real geometry, not just the insertion point") {
    std::vector<CadBlockDefinition> defs;
    CadBlockDefinition d;
    d.name = "B";
    // One segment from (2,2) to (6,6), well away from the insertion point at the origin.
    d.content.lines = {2.f, 2.f, 0.f, 6.f, 6.f, 0.f};
    d.content.lineAttrs.resize(1);
    defs.push_back(d);

    PaperLayout L;
    CadBlockRef br;
    br.defName = "B";
    br.xf.x = 0.f; br.xf.y = 0.f;
    L.paperBlockRefs.push_back(br);

    std::vector<PaperEntityRef> out;
    // A crossing box over what the block DRAWS, nowhere near its insertion point. This missed before.
    SelectPaperEntitiesInBox(L, 3.f, 3.f, 5.f, 5.f, /*windowMode=*/false, out, &defs);
    REQUIRE(n(out) == 1);
    REQUIRE(out.front().type == PaperEntityRef::Type::Block);
    // Far from both the geometry and the insertion point: still nothing.
    out.clear();
    SelectPaperEntitiesInBox(L, 50.f, 50.f, 60.f, 60.f, /*windowMode=*/false, out, &defs);
    REQUIRE(n(out) == 0);
    // Window now needs the whole extent inside, insertion point included.
    SelectPaperEntitiesInBox(L, 3.f, 3.f, 5.f, 5.f, /*windowMode=*/true, out, &defs);
    REQUIRE(n(out) == 0);
    SelectPaperEntitiesInBox(L, -1.f, -1.f, 7.f, 7.f, /*windowMode=*/true, out, &defs);
    REQUIRE(n(out) == 1);
    // With no definitions there is nothing to measure, so the historical insertion-point test stands.
    out.clear();
    SelectPaperEntitiesInBox(L, 3.f, 3.f, 5.f, 5.f, /*windowMode=*/false, out);
    REQUIRE(n(out) == 0);
    SelectPaperEntitiesInBox(L, -1.f, -1.f, 1.f, 1.f, /*windowMode=*/false, out);
    REQUIRE(n(out) == 1);
  }
}

// TASK-198 — paper text is bounded by the rectangle it OCCUPIES. Paper space carried its own copy of
// the text-bounds rule and it differed from model space's in three ways, each of which put the pick
// and the fence somewhere the glyphs are not. Every case below failed before the fix.
TEST_CASE("Paper text bounds: the box it occupies, not a guess at the insertion point (TASK-198)",
          "[paperspace]") {
  SECTION("MTEXT is bounded by the box the user dragged, not by a one-line estimate") {
    CadAnnotation a;
    a.kind = CadAnnotation::Kind::Mtext;
    a.insX = 0.f;
    a.insY = 0.f;
    a.plottedHeightInches = 0.25f;
    a.text = "a paragraph of wrapped text";
    a.boxMinX = 10.f; a.boxMaxX = 14.f;   // the rectangle the renderer wraps, anchors and clips to
    a.boxMinY = 20.f; a.boxMaxY = 23.f;
    float x0, y0, x1, y1;
    PaperTextBoundsIn(a, &x0, &y0, &x1, &y1);
    REQUIRE(x0 == Catch::Approx(10.f));
    REQUIRE(y0 == Catch::Approx(20.f));
    REQUIRE(x1 == Catch::Approx(14.f));
    REQUIRE(y1 == Catch::Approx(23.f));
  }

  SECTION("a Table is bounded by its box too") {
    CadAnnotation a;
    a.kind = CadAnnotation::Kind::Table;
    a.plottedHeightInches = 0.25f;
    a.text = "cells";
    a.boxMinX = 1.f; a.boxMaxX = 6.f;
    a.boxMinY = 2.f; a.boxMaxY = 5.f;
    float x0, y0, x1, y1;
    PaperTextBoundsIn(a, &x0, &y0, &x1, &y1);
    REQUIRE(x0 == Catch::Approx(1.f));
    REQUIRE(y0 == Catch::Approx(2.f));
    REQUIRE(x1 == Catch::Approx(6.f));
    REQUIRE(y1 == Catch::Approx(5.f));
  }

  SECTION("a stored box is normalized, so a box dragged right-to-left is still a box") {
    CadAnnotation a;
    a.kind = CadAnnotation::Kind::Mtext;
    a.text = "x";
    a.boxMinX = 14.f; a.boxMaxX = 10.f;   // reversed corners
    a.boxMinY = 23.f; a.boxMaxY = 20.f;
    float x0, y0, x1, y1;
    PaperTextBoundsIn(a, &x0, &y0, &x1, &y1);
    REQUIRE(x0 == Catch::Approx(10.f));
    REQUIRE(y0 == Catch::Approx(20.f));
    REQUIRE(x1 == Catch::Approx(14.f));
    REQUIRE(y1 == Catch::Approx(23.f));
  }

  SECTION("rotation turns the box with the glyphs") {
    CadAnnotation a;
    a.kind = CadAnnotation::Kind::Text;
    a.insX = 0.f;
    a.insY = 0.f;
    a.plottedHeightInches = 1.f;
    a.text = "ABCD";                       // 4 glyphs -> w = 0.55 * 1 * 4 = 2.2
    a.rotationRad = 1.5707963267948966f;   // 90 deg CCW
    float x0, y0, x1, y1;
    PaperTextBoundsIn(a, &x0, &y0, &x1, &y1);
    // Unrotated the box is [0,2.2] x [-1,0]. Turned a quarter turn about the insertion point it
    // becomes [0,1] x [0,2.2] — the string now runs UP the sheet, and the box has to run up with it.
    REQUIRE(x0 == Catch::Approx(0.f).margin(1.e-6));  // margin: Approx(0) rejects a signed zero
    REQUIRE(x1 == Catch::Approx(1.f));
    REQUIRE(y0 == Catch::Approx(0.f).margin(1.e-6));
    REQUIRE(y1 == Catch::Approx(2.2f));
  }

  SECTION("width counts characters, not bytes") {
    CadAnnotation a;
    a.kind = CadAnnotation::Kind::Text;
    a.insX = 0.f;
    a.insY = 0.f;
    a.plottedHeightInches = 1.f;
    a.text = "\xC3\xA9\xC3\xA9\xC3\xA9";   // "eee" with acute accents: 3 characters, 6 BYTES
    float x0, y0, x1, y1;
    PaperTextBoundsIn(a, &x0, &y0, &x1, &y1);
    REQUIRE(x1 - x0 == Catch::Approx(0.55f * 3.f));   // not 6 characters' worth
    // The same three characters written in ASCII must measure the same.
    CadAnnotation b = a;
    b.text = "eee";
    float bx0, by0, bx1, by1;
    PaperTextBoundsIn(b, &bx0, &by0, &bx1, &by1);
    REQUIRE(x1 - x0 == Catch::Approx(bx1 - bx0));
  }

  SECTION("a short label reports the width it draws, with no minimum padding") {
    CadAnnotation a;
    a.kind = CadAnnotation::Kind::Text;
    a.insX = 0.f;
    a.insY = 0.f;
    a.plottedHeightInches = 1.f;
    a.text = "A";
    float x0, y0, x1, y1;
    PaperTextBoundsIn(a, &x0, &y0, &x1, &y1);
    // Was floored at 2*h in model space, which reported an extent three times the glyph. A pick
    // aperture belongs to the pick (PickPaperEntityAt already expands by tolIn), not to the extent —
    // applied here it also inflated the box FENCE, which is what TASK-197 spent its length removing.
    REQUIRE(x1 - x0 == Catch::Approx(0.55f));
  }
}

// The bounds above are what the box fence consumes, so the fix has to be visible through it too.
TEST_CASE("Paper box-select follows an MTEXT's real box (TASK-198)", "[paperspace]") {
  PaperLayout L;
  CadAnnotation a;
  a.kind = CadAnnotation::Kind::Mtext;
  a.insX = 0.f;                       // insertion point at the origin ...
  a.insY = 0.f;
  a.plottedHeightInches = 0.25f;
  a.text = "wrapped paragraph text";
  a.boxMinX = 10.f; a.boxMaxX = 14.f;  // ... the box it occupies is ten inches away
  a.boxMinY = 20.f; a.boxMaxY = 23.f;
  L.paperTexts.push_back(a);

  std::vector<PaperEntityRef> out;
  // A crossing box over the text as drawn selects it.
  SelectPaperEntitiesInBox(L, 11.f, 21.f, 13.f, 22.f, /*windowMode=*/false, out);
  REQUIRE(out.size() == 1);
  REQUIRE(out.front().type == PaperEntityRef::Type::Text);

  // A crossing box over the INSERTION POINT, where the old estimate put the rectangle, selects
  // nothing — there is no text drawn there.
  out.clear();
  SelectPaperEntitiesInBox(L, -1.f, -1.f, 1.f, 1.f, /*windowMode=*/false, out);
  REQUIRE(out.empty());

  // Window mode needs the real box inside, and is not satisfied by enclosing the insertion point.
  SelectPaperEntitiesInBox(L, -1.f, -1.f, 5.f, 5.f, /*windowMode=*/true, out);
  REQUIRE(out.empty());
  SelectPaperEntitiesInBox(L, 9.f, 19.f, 15.f, 24.f, /*windowMode=*/true, out);
  REQUIRE(out.size() == 1);
}
