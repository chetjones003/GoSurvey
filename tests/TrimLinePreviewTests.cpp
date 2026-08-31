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
