// Visual-style naming and parsing (REQ-064).
//
// The renderer's GL state machine is not linkable by this target (TASK-035 §11), so what is tested
// here is the piece that is pure and has a real bug class behind it: the command's input handling.
// A mistyped alias that silently selects the wrong style is invisible in review and obvious only to
// whoever types it.

#include <catch2/catch_test_macros.hpp>

#include <string>

#include "commands/CadEntities.hpp"

namespace {
VisualStyle Parsed(const std::string& s) {
  VisualStyle v = VisualStyle::Hidden;  // a sentinel that is none of the expected answers below
  REQUIRE(VisualStyleFromName(s, &v));
  return v;
}
} // namespace

TEST_CASE("Every accepted spelling maps to the right style", "[visualstyle]") {
  for (const char* s : {"2D", "2d", "2dwireframe", "2D Wireframe", "wireframe", "W", "0"})
    CHECK(Parsed(s) == VisualStyle::Wireframe2D);
  for (const char* s : {"HIDDEN", "hidden", "Hidden", "h", "1"})
    CHECK(Parsed(s) == VisualStyle::Hidden);
  for (const char* s : {"SHADED", "shaded", "s", "2"})
    CHECK(Parsed(s) == VisualStyle::Shaded);
}

TEST_CASE("An unrecognised style leaves the current one untouched", "[visualstyle]") {
  // REQ-201: a bad command reports and changes nothing. If this returned true, or wrote to `out`
  // before failing, a typo would silently change how the drawing renders.
  VisualStyle v = VisualStyle::Shaded;
  for (const char* s : {"", "3", "-1", "realistic", "conceptual", "shade", "hid", "2dd", "x"}) {
    INFO("input = '" << s << "'");
    CHECK_FALSE(VisualStyleFromName(s, &v));
    CHECK(v == VisualStyle::Shaded);  // unchanged
  }
}

TEST_CASE("A null destination is refused rather than dereferenced", "[visualstyle]") {
  CHECK_FALSE(VisualStyleFromName("shaded", nullptr));
}

TEST_CASE("Style names round-trip through parsing", "[visualstyle]") {
  // The canonical name the UI and `.gs` show must be one the command accepts — otherwise a user
  // reading "2D Wireframe" off the ribbon and typing it back gets an error.
  for (VisualStyle s : {VisualStyle::Wireframe2D, VisualStyle::Hidden, VisualStyle::Shaded}) {
    VisualStyle back = VisualStyle::Hidden;
    INFO("name = " << VisualStyleName(s));
    REQUIRE(VisualStyleFromName(VisualStyleName(s), &back));
    CHECK(back == s);
  }
}

TEST_CASE("Wireframe2D is the zero value", "[visualstyle]") {
  // Load-bearing for persistence and for RenderTuning's default: a `.gs` or prefs file written
  // before REQ-064 has no style key at all, and a zero-initialised field must mean "the classic
  // view", not "Shaded".
  CHECK(static_cast<int>(VisualStyle::Wireframe2D) == 0);
}
