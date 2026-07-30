#include <catch2/catch_test_macros.hpp>

#include "ui/MtextToolbar.hpp"

// REQ-051: pure logic behind the MTEXT "Text Formatting" panel.

TEST_CASE("An in-bounds panel anchor is left where the user dropped it (REQ-051)", "[mtexttoolbar]") {
  float x = 0.f, y = 0.f;
  mtexttoolbar::ClampPanelAnchor(300.f, 200.f, 480.f, 90.f, 0.f, 0.f, 1600.f, 900.f, &x, &y);
  REQUIRE(x == 300.f);
  REQUIRE(y == 200.f);
}

TEST_CASE("An off-screen panel anchor is pulled fully back inside (REQ-051)", "[mtexttoolbar]") {
  float x = 0.f, y = 0.f;
  // Dragged past the right/bottom edge: the far edge lands exactly on the boundary.
  mtexttoolbar::ClampPanelAnchor(5000.f, 5000.f, 480.f, 90.f, 10.f, 20.f, 1610.f, 920.f, &x, &y);
  REQUIRE(x == 1610.f - 480.f);
  REQUIRE(y == 920.f - 90.f);

  // Dragged past the top/left edge (negative): clamped to the region minimum, not to zero.
  mtexttoolbar::ClampPanelAnchor(-900.f, -50.f, 480.f, 90.f, 10.f, 20.f, 1610.f, 920.f, &x, &y);
  REQUIRE(x == 10.f);
  REQUIRE(y == 20.f);
}

TEST_CASE("A panel wider or taller than the viewport pins to the origin (REQ-051)", "[mtexttoolbar]") {
  float x = 0.f, y = 0.f;
  // Failure mode: a tiny viewport. Naive clamping would give max-panel < min and push the title bar
  // off-screen; the panel must stay reachable at the region minimum instead.
  mtexttoolbar::ClampPanelAnchor(200.f, 200.f, 480.f, 90.f, 4.f, 6.f, 100.f, 40.f, &x, &y);
  REQUIRE(x == 4.f);
  REQUIRE(y == 6.f);
}

TEST_CASE("Font run tags wrap a selection; an empty family applies nothing (REQ-051)", "[mtexttoolbar]") {
  const auto arial = mtexttoolbar::FontRunTags("Arial");
  REQUIRE(arial.open == "[[font:Arial]]");
  REQUIRE(arial.close == "[[/font]]");

  const auto shx = mtexttoolbar::FontRunTags("romans.shx");
  REQUIRE(shx.open == "[[font:romans.shx]]");

  // Failure mode: "(default)" must not emit [[font:]], which the parser would read as an empty-named font.
  const auto none = mtexttoolbar::FontRunTags("");
  REQUIRE(none.open.empty());
  REQUIRE(none.close.empty());
}

TEST_CASE("Colour run tags match the wire format's %06X emission (REQ-051)", "[mtexttoolbar]") {
  const auto red = mtexttoolbar::ColorRunTags(0xFF0000u);
  REQUIRE(red.open == "[[color:FF0000]]");
  REQUIRE(red.close == "[[/color]]");

  // Low components keep their leading zeros (a 6-digit field, not a truncated one).
  REQUIRE(mtexttoolbar::ColorRunTags(0x0000FFu).open == "[[color:0000FF]]");
  REQUIRE(mtexttoolbar::ColorRunTags(0u).open == "[[color:000000]]");

  // An alpha byte riding along in the high bits is dropped rather than widening the field.
  REQUIRE(mtexttoolbar::ColorRunTags(0xFF12ABCDu).open == "[[color:12ABCD]]");
}

TEST_CASE("Ruler ticks are evenly spaced with every Nth major (REQ-051)", "[mtexttoolbar]") {
  const auto ticks = mtexttoolbar::RulerTicks(100.f, 10.f, 5);
  REQUIRE(ticks.size() == 11);  // 0,10,…,100 inclusive
  REQUIRE(ticks.front().offsetPx == 0.f);
  REQUIRE(ticks.front().isMajor);
  REQUIRE(ticks.back().offsetPx == 100.f);
  REQUIRE(ticks[5].offsetPx == 50.f);
  REQUIRE(ticks[5].isMajor);
  REQUIRE_FALSE(ticks[1].isMajor);
  REQUIRE_FALSE(ticks[4].isMajor);

  // No tick may sit past the ruler's right edge.
  const auto ragged = mtexttoolbar::RulerTicks(95.f, 10.f, 5);
  REQUIRE(ragged.size() == 10);
  REQUIRE(ragged.back().offsetPx == 90.f);
}

TEST_CASE("A collapsed or degenerate ruler yields no ticks (REQ-051)", "[mtexttoolbar]") {
  // Failure modes: a zero/negative width (panel not yet laid out) and a zero/negative spacing must not
  // divide by zero, loop forever, or allocate.
  REQUIRE(mtexttoolbar::RulerTicks(0.f, 10.f, 5).empty());
  REQUIRE(mtexttoolbar::RulerTicks(-40.f, 10.f, 5).empty());
  REQUIRE(mtexttoolbar::RulerTicks(100.f, 0.f, 5).empty());
  REQUIRE(mtexttoolbar::RulerTicks(100.f, -2.f, 5).empty());

  // A nonsense majorEvery degrades to "every tick is major" instead of a modulo by zero.
  const auto all = mtexttoolbar::RulerTicks(20.f, 10.f, 0);
  REQUIRE(all.size() == 3);
  for (const auto& t : all)
    REQUIRE(t.isMajor);

  // A pathological spacing is capped rather than allocating without bound.
  REQUIRE(mtexttoolbar::RulerTicks(1.e9f, 0.001f, 5).size() == 4097);
}

TEST_CASE("Attachment points name themselves, out-of-range falls back (REQ-051)", "[mtexttoolbar]") {
  REQUIRE(std::string(mtexttoolbar::AttachLabel(1)) == "Top Left");
  REQUIRE(std::string(mtexttoolbar::AttachLabel(5)) == "Middle Center");
  REQUIRE(std::string(mtexttoolbar::AttachLabel(9)) == "Bottom Right");

  // Failure mode: a DXF may carry a group-71 value outside 1–9; read it as the default, never past
  // the end of the table.
  REQUIRE(std::string(mtexttoolbar::AttachLabel(0)) == "Top Left");
  REQUIRE(std::string(mtexttoolbar::AttachLabel(10)) == "Top Left");
  REQUIRE(std::string(mtexttoolbar::AttachLabel(-3)) == "Top Left");
}
