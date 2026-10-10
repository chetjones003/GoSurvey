// GitHub issue #166 — the TRIM "draw a line to trim" (TRIMSTATE 0) rubber-band preview.
//
// CadTrimAppendCutLineRemovedPreview runs every frame while the cut line is being dragged. The
// old implementation previewed a hypothetical removal for EVERY line and polyline edge in the
// drawing and re-tessellated the whole drawing's cutting geometry for each one — O(edges x
// drawing), seconds per frame on a real survey, and it dashed edges the commit would never touch.
//
// It now mirrors ExecuteDrawnSegmentTrimOnce: the fence line trims the ONE nearest edge, so the
// preview shows exactly that edge's removed stub and nothing else. These tests pin both halves —
// the stub is right, and an edge the fence is nowhere near is not previewed.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "CadCommands.hpp"
#include "viewport/TransformPreview.hpp"

#include <cmath>
#include <vector>

using Catch::Approx;

namespace {

// A horizontal target line (0,0)->(100,0) as entity 0, crossed at x=50 by a vertical line.
AppCommandState TwoCrossingLines() {
  AppCommandState st;
  st.userLinesFlat = {
      0.f,  0.f,   0.f, 100.f, 0.f,  0.f,  // entity 0: the target
      50.f, -20.f, 0.f, 50.f,  20.f, 0.f,  // entity 1: the cutter, crosses at (50,0)
  };
  return st;
}

} // namespace

TEST_CASE("Cut-line preview shows the stub on the pick side of the crossing", "[trim][issue166]") {
  const AppCommandState st = TwoCrossingLines();

  // Fence drawn across the target at x=20 — left of the crossing. Pick preview = fence midpoint.
  std::vector<float> preview;
  CadTrimAppendCutLineRemovedPreview(st, 20.f, -5.f, 20.f, 5.f, 20.f, 0.f, &preview);

  // Exactly one segment (6 floats): from the target's left end to the crossing at (50,0).
  REQUIRE(preview.size() == 6);
  CHECK(preview[0] == Approx(0.f));
  CHECK(preview[1] == Approx(0.f));
  CHECK(preview[3] == Approx(50.f));
  CHECK(preview[4] == Approx(0.f));
}

TEST_CASE("Cut-line preview shows the other stub when the pick is on the other side", "[trim][issue166]") {
  const AppCommandState st = TwoCrossingLines();

  // Fence at x=80 — right of the crossing.
  std::vector<float> preview;
  CadTrimAppendCutLineRemovedPreview(st, 80.f, -5.f, 80.f, 5.f, 80.f, 0.f, &preview);

  REQUIRE(preview.size() == 6);
  // The removed stub is now crossing -> right end.
  const float x0 = preview[0], x1 = preview[3];
  CHECK(std::min(x0, x1) == Approx(50.f));
  CHECK(std::max(x0, x1) == Approx(100.f));
}

TEST_CASE("Cut-line preview is empty when the fence is near no edge", "[trim][issue166]") {
  const AppCommandState st = TwoCrossingLines();

  // Both target and cutter cross each other, so the OLD code would still have previewed removals
  // for them here. The fence is 200 units away from everything.
  std::vector<float> preview;
  CadTrimAppendCutLineRemovedPreview(st, 200.f, 200.f, 200.f, 210.f, 200.f, 205.f, &preview);

  CHECK(preview.empty());
}

TEST_CASE("Cut-line preview never previews more than the single nearest edge", "[trim][issue166]") {
  // A ladder of horizontal lines all crossed by one vertical cutter. Dragging a fence across the
  // middle rung must preview that rung only — not all five.
  AppCommandState st;
  st.userLinesFlat.clear();
  for (int i = 0; i < 5; ++i) {
    const float y = static_cast<float>(i) * 40.f;
    st.userLinesFlat.insert(st.userLinesFlat.end(), {0.f, y, 0.f, 100.f, y, 0.f});
  }
  st.userLinesFlat.insert(st.userLinesFlat.end(), {50.f, -20.f, 0.f, 50.f, 220.f, 0.f});  // the cutter

  std::vector<float> preview;
  // Fence across the middle rung (y = 80).
  CadTrimAppendCutLineRemovedPreview(st, 20.f, 75.f, 20.f, 85.f, 20.f, 80.f, &preview);

  REQUIRE(preview.size() == 6);           // one segment, not five
  CHECK(preview[1] == Approx(80.f));      // and it is the y=80 rung
  CHECK(preview[4] == Approx(80.f));
}

TEST_CASE("Cut-line preview tolerates a null output pointer", "[trim][issue166]") {
  const AppCommandState st = TwoCrossingLines();
  CadTrimAppendCutLineRemovedPreview(st, 20.f, -5.f, 20.f, 5.f, 20.f, 0.f, nullptr);  // must not crash
}

// REQ-325 / ADR-053 follow-up (real report): a polyline JOIN gave a tilted curved segment (issue
// #373's 3D FILLET), and the selected polyline's YELLOW highlight never lit up over that segment —
// it stayed the entity's own base colour, because BuildSelectionHighlight's own arc tracer
// (TransformPreview.cpp's appendCommittedPolylineStrip) was still flat-only, missed by increment 2
// (which only fixed the RENDERER's own tessellation and PickClosestCadEntity, a separate function).
TEST_CASE("A selected polyline's tilted curved segment highlights on the true arc, not flat",
         "[TransformPreview][req325]") {
  AppCommandState st;
  // Same wall-standing half circle as the CadSnapTests req325 cases: vertex A=(0,0,0), B=(20,0,0),
  // bulge=1, normal (0,-1,0) -- the true apex sits at world (10, 0, -10), not the chord's (10,0,0).
  st.userPolylineVerts = {0.f, 0.f, 0.f, 20.f, 0.f, 0.f};
  st.userPolylineOffsets = {0, 2};
  st.userPolylineClosed = {0};
  st.userPolylineAttrs.emplace_back();
  st.userPolylineVertsBulge = {1.f, 0.f};
  st.userPolylineVertsNormal = {0.f, -1.f, 0.f, 0.f, 0.f, 1.f};
  SelectedEntity se{};
  se.type = SelectedEntity::Type::Polyline;
  se.index = 0;
  st.selection.push_back(se);

  std::vector<float> hlLines;
  std::vector<float> hlCircles;
  BuildSelectionHighlight(st, &hlLines, &hlCircles);
  REQUIRE(hlLines.size() % 6 == 0);
  REQUIRE(hlLines.size() >= 6);

  // Every highlighted point must lie on the true circle (radius 10 about world (10,0,0)) and in the
  // wall's own plane (y = 0) -- the old flat-chord code instead drew a single straight segment
  // sitting at z = 0 the whole way, which both checks below would catch (a chord point off the
  // circle, or every z pinned to 0 instead of dipping to -10 at the apex).
  bool sawTrueDepth = false;
  for (size_t i = 0; i + 5 < hlLines.size(); i += 6) {
    for (int p = 0; p < 2; ++p) {
      const float x = hlLines[i + static_cast<size_t>(p) * 3 + 0];
      const float y = hlLines[i + static_cast<size_t>(p) * 3 + 1];
      const float z = hlLines[i + static_cast<size_t>(p) * 3 + 2];
      CHECK(y == Approx(0.f).margin(1e-3));
      const float d = std::sqrt((x - 10.f) * (x - 10.f) + z * z);
      CHECK(d == Approx(10.f).margin(0.05f));
      if (z < -5.f)
        sawTrueDepth = true;
    }
  }
  CHECK(sawTrueDepth);  // at least one sampled point is genuinely near the apex, not chord-flat
}
