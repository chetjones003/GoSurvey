// REQ-370 (D-2026-10-05-a) — the live preview of what an open selection box would select must be
// exactly what releasing the click selects, and must never touch the real selection.

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <string>
#include <vector>

#include "CadCommands.hpp"

namespace {

void Submit(AppCommandState& st, const std::string& text, std::vector<std::string>& log) {
  char buf[256];
  std::snprintf(buf, sizeof(buf), "%s", text.c_str());
  ProcessCommandLineSubmit(buf, sizeof(buf), st, log);
}

/// Two lines, y = 0 and y = 20, both x = 0..10.
AppCommandState TwoLines() {
  AppCommandState st;
  std::vector<std::string> log;
  for (const char* y : {"0", "20"}) {
    Submit(st, "LINE", log);
    Submit(st, std::string("0,") + y, log);
    Submit(st, std::string("10,") + y, log);
    CancelActiveCommand(st, log);
  }
  EnsureAttrCounts(st);
  REQUIRE(st.userLineAttrs.size() == 2);
  st.selection.clear();
  return st;
}

void OpenBox(AppCommandState& st, float x, float y) {
  st.selBoxWaitingSecond = true;
  st.selBoxAnchorX = x;
  st.selBoxAnchorY = y;
  st.selBoxAnchorZ = 0.f;
  st.uiCursorWorldZ = 0.f;
}

}  // namespace

TEST_CASE("box preview equals the window click result and leaves the selection alone (REQ-370)",
          "[req370][boxselect]") {
  AppCommandState st = TwoLines();
  std::vector<std::string> log;
  OpenBox(st, -1.f, -1.f);
  st.selection.push_back(SelectedEntity{SelectedEntity::Type::LineSeg, 1});  // pre-existing pick

  UpdateSelectionBoxPreview(st, 11.f, 5.f, /*windowMode=*/true, nullptr, 0.f, 0.f);
  REQUIRE(st.selBoxPreview.size() == 1);
  CHECK(st.selBoxPreview[0].index == 0);
  REQUIRE(st.selection.size() == 1);  // untouched
  CHECK(st.selection[0].index == 1);

  // The click on the same corner selects the same thing (additively).
  st.selection.clear();
  SubmitViewportPick(st, 11.0, 5.0, log, false, /*windowMode=*/true);
  REQUIRE(st.selection.size() == 1);
  CHECK(st.selection[0].index == st.selBoxPreview[0].index);
}

TEST_CASE("crossing preview picks what the box merely touches; window does not (REQ-370)",
          "[req370][boxselect]") {
  AppCommandState st = TwoLines();
  OpenBox(st, 5.f, -1.f);
  UpdateSelectionBoxPreview(st, 6.f, 25.f, /*windowMode=*/false, nullptr, 0.f, 0.f);
  CHECK(st.selBoxPreview.size() == 2);
  UpdateSelectionBoxPreview(st, 6.f, 25.f, /*windowMode=*/true, nullptr, 0.f, 0.f);
  CHECK(st.selBoxPreview.empty());
}

TEST_CASE("empty box previews nothing and closing the box clears the preview (REQ-370)",
          "[req370][boxselect]") {
  AppCommandState st = TwoLines();
  OpenBox(st, 100.f, 100.f);
  UpdateSelectionBoxPreview(st, 105.f, 105.f, false, nullptr, 0.f, 0.f);
  CHECK(st.selBoxPreview.empty());

  OpenBox(st, -1.f, -1.f);
  UpdateSelectionBoxPreview(st, 11.f, 5.f, true, nullptr, 0.f, 0.f);
  REQUIRE_FALSE(st.selBoxPreview.empty());
  st.selBoxWaitingSecond = false;
  UpdateSelectionBoxPreview(st, 0.f, 0.f, true, nullptr, 0.f, 0.f);
  CHECK(st.selBoxPreview.empty());
}
