// Property-panel edit services and the shared right-click classifier (issue #891; REQ-024,
// REQ-057, REQ-101, REQ-054, REQ-084). Both frontends call these, so they are asserted once here.
#include "CadCommands.hpp"
#include "CadPropertyEdit.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <string>
#include <vector>

using Catch::Approx;
using propedit::Result;

namespace {

AppCommandState DrawingWithLineCircleText() {
  AppCommandState st;
  st.userLinesFlat = {0, 0, 0, 10, 0, 0};
  st.userCirclesCxCyZR = {5, 5, 0, 2};
  CadAnnotation text;
  text.kind = CadAnnotation::Kind::Text;
  text.text = "MH-1";
  text.insX = 1.f;
  text.insY = 2.f;
  st.cadAnnotations.push_back(text);
  CadAnnotation mtext;
  mtext.kind = CadAnnotation::Kind::Mtext;
  mtext.text = "NOTE";
  mtext.insX = 0.f;
  mtext.insY = 0.f;
  mtext.boxMinX = 0.f;
  mtext.boxMinY = 0.f;
  mtext.boxMaxX = 4.f;
  mtext.boxMaxY = 2.f;
  st.cadAnnotations.push_back(mtext);
  EnsureAttrCounts(st);
  return st;
}

}  // namespace

TEST_CASE("A line coordinate is typed in world space and stored local", "[issue891][properties]") {
  AppCommandState st = DrawingWithLineCircleText();
  st.worldDocumentOriginX = 1000.0;
  st.worldDocumentOriginY = 2000.0;
  const auto x = propedit::SetLineField(st, 0, propedit::LineField::EndX, 1015.0);
  CHECK(x.result == Result::Applied);
  CHECK(st.userLinesFlat[3] == Approx(15.0));
  const auto z = propedit::SetLineField(st, 0, propedit::LineField::StartZ, 7.5);
  CHECK(z.result == Result::Applied);
  CHECK(st.userLinesFlat[2] == Approx(7.5));  // Z carries no document origin (ADR-025 D2)
}

TEST_CASE("A property edit is one undo step and Unchanged pushes none", "[issue891][properties][undo]") {
  AppCommandState st = DrawingWithLineCircleText();
  std::vector<std::string> log;
  const bool couldUndoBefore = CanUndo(st);
  CHECK(propedit::SetLineField(st, 0, propedit::LineField::EndY, 0.0).result == Result::Unchanged);
  CHECK(CanUndo(st) == couldUndoBefore);
  REQUIRE(propedit::SetLineField(st, 0, propedit::LineField::EndY, 4.0).result == Result::Applied);
  CHECK(st.userLinesFlat[4] == Approx(4.0));
  REQUIRE(DoUndo(st, log));
  CHECK(st.userLinesFlat[4] == Approx(0.0));
}

TEST_CASE("A non-finite or stale-index edit is rejected and leaves the drawing alone", "[issue891][properties][validation]") {
  AppCommandState st = DrawingWithLineCircleText();
  const auto nan = propedit::SetLineField(st, 0, propedit::LineField::StartX, std::numeric_limits<double>::quiet_NaN());
  CHECK(nan.result == Result::Rejected);
  CHECK_FALSE(nan.message.empty());
  CHECK(propedit::SetLineField(st, 9, propedit::LineField::StartX, 1.0).result == Result::Rejected);
  CHECK(propedit::SetCircleField(st, 3, propedit::CircleField::Radius, 1.0).result == Result::Rejected);
  CHECK(propedit::SetTextNumber(st, 7, propedit::TextField::InsertionZ, 1.0).result == Result::Rejected);
  CHECK(st.userLinesFlat[0] == 0.0);
  CHECK_FALSE(CanUndo(st));
}

TEST_CASE("A non-positive radius is raised to the minimum and says so", "[issue891][properties][validation]") {
  AppCommandState st = DrawingWithLineCircleText();
  const auto r = propedit::SetCircleField(st, 0, propedit::CircleField::Radius, -3.0);
  CHECK(r.result == Result::Applied);
  CHECK_FALSE(r.message.empty());
  CHECK(st.userCirclesCxCyZR[3] == Approx(propedit::kMinCircleRadius));
  CHECK(propedit::SetCircleField(st, 0, propedit::CircleField::Radius, 6.0).result == Result::Applied);
  CHECK(st.userCirclesCxCyZR[3] == Approx(6.0));
}

TEST_CASE("MTEXT insertion carries its box; a box edit keeps min <= max", "[issue891][properties][text]") {
  AppCommandState st = DrawingWithLineCircleText();
  REQUIRE(propedit::SetTextNumber(st, 1, propedit::TextField::InsertionX, 10.0).result == Result::Applied);
  CHECK(st.cadAnnotations[1].boxMinX == Approx(10.f));
  CHECK(st.cadAnnotations[1].boxMaxX == Approx(14.f));
  REQUIRE(propedit::SetTextNumber(st, 1, propedit::TextField::BoxMinX, 20.0).result == Result::Applied);
  CHECK(st.cadAnnotations[1].boxMinX == Approx(14.f));  // swapped, not left inverted
  CHECK(st.cadAnnotations[1].boxMaxX == Approx(20.f));
  CHECK(st.cadAnnotations[1].insX == Approx(14.f));
}

TEST_CASE("Text fields that do not apply to the kind are refused", "[issue891][properties][text]") {
  AppCommandState st = DrawingWithLineCircleText();
  CHECK(propedit::SetTextNumber(st, 1, propedit::TextField::RotationBearingDeg, 45.0).result == Result::Rejected);
  CHECK(propedit::SetTextNumber(st, 0, propedit::TextField::BoxMaxX, 9.0).result == Result::Rejected);
  CHECK(propedit::SetTextNumber(st, 0, propedit::TextField::RotationBearingDeg, 90.0).result == Result::Applied);
  CHECK(BearingCwNorthDegFromMathAngleRad(st.cadAnnotations[0].rotationRad) == Approx(90.f).margin(1e-3));
}

TEST_CASE("A non-positive text height resets to the default and says so", "[issue891][properties][text]") {
  AppCommandState st = DrawingWithLineCircleText();
  const auto r = propedit::SetTextNumber(st, 0, propedit::TextField::PlottedHeightInches, 0.0);
  CHECK(r.result == Result::Applied);
  CHECK_FALSE(r.message.empty());
  CHECK(st.cadAnnotations[0].plottedHeightInches == Approx(propedit::kDefaultPlottedTextHeightInches));
}

TEST_CASE("Text content edits are undoable", "[issue891][properties][text][undo]") {
  AppCommandState st = DrawingWithLineCircleText();
  std::vector<std::string> log;
  REQUIRE(propedit::SetTextContent(st, 0, "MH-2").result == Result::Applied);
  CHECK(st.cadAnnotations[0].text == "MH-2");
  CHECK(propedit::SetTextContent(st, 0, "MH-2").result == Result::Unchanged);
  REQUIRE(DoUndo(st, log));
  CHECK(st.cadAnnotations[0].text == "MH-1");
}

TEST_CASE("General attributes report agreement and mixing across the selection", "[issue891][properties][general]") {
  AppCommandState st = DrawingWithLineCircleText();
  st.selection = {{SelectedEntity::Type::LineSeg, 0}, {SelectedEntity::Type::Circle, 0}};
  st.userLineAttrs[0].layer = "PARCEL";
  st.userCircleAttrs[0].layer = "PARCEL";
  auto g = propedit::ReadGeneralAttrs(st);
  CHECK(g.count == 2);
  CHECK(g.layer == "PARCEL");
  CHECK_FALSE(g.layerMixed);
  st.userCircleAttrs[0].layer = "CURB";
  st.userCircleAttrs[0].lineweightMm = 0.5f;
  g = propedit::ReadGeneralAttrs(st);
  CHECK(g.layerMixed);
  CHECK(g.lineweightMixed);
  CHECK_FALSE(g.linetypeMixed);
}

TEST_CASE("Transparency is one undo step over the selection", "[issue891][properties][general][undo]") {
  AppCommandState st = DrawingWithLineCircleText();
  std::vector<std::string> log;
  st.selection = {{SelectedEntity::Type::LineSeg, 0}, {SelectedEntity::Type::Circle, 0}};
  CHECK(CadApplyTransparencyToSelection(st, 0.5f) == 2);
  CHECK(st.userLineAttrs[0].transparency == Approx(0.5f));
  CHECK(st.userCircleAttrs[0].transparency == Approx(0.5f));
  CHECK(CadApplyTransparencyToSelection(st, 0.5f) == 0);  // nothing changed, nothing pushed
  CHECK(CadApplyTransparencyToSelection(st, 7.f) == 2);   // clamped to 1
  CHECK(st.userLineAttrs[0].transparency == Approx(1.f));
  REQUIRE(DoUndo(st, log));
  CHECK(st.userLineAttrs[0].transparency == Approx(0.5f));
}

TEST_CASE("Right-click preferences classify the press the same way for every frontend", "[issue891][rightclick]") {
  using RC = RightClickAction;
  AppCommandState st;
  st.lastCommand = AppCommandState::Kind::None;
  // Idle, nothing selected: Default Mode. "Repeat" with nothing to repeat opens the menu.
  st.rightClickDefaultMode = AppCommandState::RightClickDefaultMode::RepeatLastCommand;
  CHECK(CadClassifyRightClick(st, false) == RC::ShortcutMenu);
  st.lastCommand = AppCommandState::Kind::Line;
  CHECK(CadClassifyRightClick(st, false) == RC::RepeatLast);
  st.rightClickDefaultMode = AppCommandState::RightClickDefaultMode::ShortcutMenu;
  CHECK(CadClassifyRightClick(st, false) == RC::ShortcutMenu);
  // A selection uses Edit Mode, independent of Default Mode.
  st.rightClickEditMode = AppCommandState::RightClickEditMode::RepeatLastCommand;
  CHECK(CadClassifyRightClick(st, true) == RC::RepeatLast);
  st.rightClickEditMode = AppCommandState::RightClickEditMode::ShortcutMenu;
  CHECK(CadClassifyRightClick(st, true) == RC::ShortcutMenu);
  // A running command uses Command Mode.
  st.active = AppCommandState::Kind::Line;
  st.rightClickCommandMode = AppCommandState::RightClickCommandMode::Enter;
  CHECK(CadClassifyRightClick(st, true) == RC::Enter);
  st.rightClickCommandMode = AppCommandState::RightClickCommandMode::ShortcutMenuAlways;
  CHECK(CadClassifyRightClick(st, false) == RC::ShortcutMenu);
}

TEST_CASE("Time-sensitive right-click governs only the no-selection case", "[issue891][rightclick]") {
  AppCommandState st;
  st.rightClickTimeSensitive = false;
  CHECK_FALSE(CadRightClickIsTimed(st, false));
  st.rightClickTimeSensitive = true;
  CHECK(CadRightClickIsTimed(st, false));
  CHECK_FALSE(CadRightClickIsTimed(st, true));
  CHECK(CadRightClickHoldIsMenu(250.0, 250));
  CHECK_FALSE(CadRightClickHoldIsMenu(249.0, 250));
}
