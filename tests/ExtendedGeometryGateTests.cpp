// REQ-087 — the gate that decides the viewport has committed geometry to draw.
//
// This exists because of a specific bug and would have caught it. `hasExt` in ViewportRenderer.cpp
// was a hand-written list of "does the extended input hold anything?" — arcs, ellipses, polylines —
// and feature lines were not in it. A drawing holding ONLY a feature line therefore skipped the
// entire committed-geometry block and rendered NOTHING, while still hovering, selecting and zooming
// to extents, because each of those is a different path. The object was plainly there and plainly
// invisible.
//
// That was the fifth list a feature line had been left out of. Rendering itself is not testable
// here — it needs a GL context the test target has no business creating — but the PREDICATE is pure,
// and the predicate is where the defect lived.

#include <catch2/catch_test_macros.hpp>

#include "CadCommands.hpp"

#include <vector>

TEST_CASE("A drawing holding only a feature line has drawable geometry", "[render][featureline][req087]") {
  // Exactly what main.cpp hands the renderer: every pointer wired, most stores empty. Pointers being
  // non-null is NOT the question — having entities is.
  std::vector<float> noVerts;
  std::vector<int> noOffsets;
  std::vector<uint8_t> noClosed;
  std::vector<EntityAttributes> noAttrs;
  std::vector<CadArc> noArcs;
  std::vector<CadEllipse> noEllipses;

  // One feature line, two vertices: CSR offsets {0, 2}.
  std::vector<float> flVerts = {0.f, 0.f, 10.f, 100.f, 0.f, 12.f};
  std::vector<int> flOffsets = {0, 2};
  std::vector<uint8_t> flClosed = {0};
  std::vector<EntityAttributes> flAttrs(1);

  CadExtendedGeometryInput e;
  e.arcs = &noArcs;
  e.ellipses = &noEllipses;
  e.polylineVerts = &noVerts;
  e.polylineOffsets = &noOffsets;
  e.polylineClosed = &noClosed;
  e.polylineAttrs = &noAttrs;
  e.featureLineVerts = &flVerts;
  e.featureLineOffsets = &flOffsets;
  e.featureLineClosed = &flClosed;
  e.featureLineAttrs = &flAttrs;

  REQUIRE(CadExtendedHasDrawableGeometry(e));
}

TEST_CASE("An input wired but holding nothing has no drawable geometry", "[render][req087]") {
  // The complement, and the reason the predicate cannot simply test the pointers: main.cpp wires
  // every store on every frame, so a null check would report "yes, draw" for an empty drawing.
  std::vector<float> noVerts;
  std::vector<int> noOffsets;
  std::vector<CadArc> noArcs;
  std::vector<CadEllipse> noEllipses;

  CadExtendedGeometryInput e;
  e.arcs = &noArcs;
  e.ellipses = &noEllipses;
  e.polylineVerts = &noVerts;
  e.polylineOffsets = &noOffsets;
  e.featureLineVerts = &noVerts;
  e.featureLineOffsets = &noOffsets;

  REQUIRE_FALSE(CadExtendedHasDrawableGeometry(e));
}

TEST_CASE("A one-entry CSR offsets table describes zero entities", "[render][req087][issue60]") {
  // An "empty" CSR store is legitimately either {} or {0} — issue #60's lesson. One offset claims a
  // table exists while describing no entities, and reading it as one entity walks off the end of the
  // vertex array.
  std::vector<float> noVerts;
  std::vector<int> oneOffset = {0};
  REQUIRE_FALSE(CadChainHasEntities(&noVerts, &oneOffset));

  std::vector<float> verts = {0.f, 0.f, 0.f, 1.f, 1.f, 0.f};
  std::vector<int> twoOffsets = {0, 2};
  REQUIRE(CadChainHasEntities(&verts, &twoOffsets));
}

TEST_CASE("Polylines still gate the draw on their own, unchanged", "[render][req087]") {
  // The feature-line append used to be NESTED inside the polyline block, so it inherited whether
  // polylines existed. Un-nesting them must not have made polylines depend on feature lines either.
  std::vector<float> pvVerts = {0.f, 0.f, 0.f, 5.f, 5.f, 0.f};
  std::vector<int> pvOffsets = {0, 2};
  std::vector<float> noVerts;
  std::vector<int> noOffsets;

  CadExtendedGeometryInput e;
  e.polylineVerts = &pvVerts;
  e.polylineOffsets = &pvOffsets;
  e.featureLineVerts = &noVerts;
  e.featureLineOffsets = &noOffsets;

  REQUIRE(CadExtendedHasDrawableGeometry(e));
}
