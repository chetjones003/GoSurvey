// REQ-356 (D-2026-09-29-a, GitHub issue #575) — CHPROP, MATCHPROP, LAYMCUR, the current colour and
// the ribbon colour combo.
//
// The commands are driven the way the command line drives them (`ProcessCommandLineSubmit`), and a
// pick is what the viewport does after an accumulate click: the entity joins `st.selection`, then
// `CadPropCommandSelectionChanged` runs. A window goes through `SubmitViewportPick`, as the headless
// CLICK verb's second corner does.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "CadColor.hpp"
#include "CadCommands.hpp"
#include "GsIo.hpp"
#include "ViewportPickPolicy.hpp"

namespace {

void Submit(AppCommandState& st, const std::string& text, std::vector<std::string>& log) {
  char buf[256];
  std::snprintf(buf, sizeof(buf), "%s", text.c_str());
  ProcessCommandLineSubmit(buf, sizeof(buf), st, log);
}

SelectedEntity Sel(SelectedEntity::Type t, int index) {
  SelectedEntity e;
  e.type = t;
  e.index = index;
  return e;
}

void AddLayer(AppCommandState& st, const std::string& name, const std::string& color) {
  CadLayerRow row;
  row.name = name;
  row.color = color;
  st.drawingLayerTable.push_back(row);
}

void AddLine(AppCommandState& st, float y, std::vector<std::string>& log) {
  Submit(st, "LINE", log);
  Submit(st, "0," + std::to_string(y), log);
  Submit(st, "10," + std::to_string(y), log);
  CancelActiveCommand(st, log);  // Esc ends LINE (Enter starts a new chain)
}

/// Two lines (y = 0 and y = 20), a 20x10x8 box solid at y 40..50 and a 4in pipe run at y = 80.
AppCommandState Drawing() {
  AppCommandState st;
  std::vector<std::string> log;
  AddLine(st, 0.f, log);
  AddLine(st, 20.f, log);
  Submit(st, "BOX 0,40 20 10 8", log);
  CadPipeRun run;
  run.name = "L-101";
  run.vertsXyz = {0.0, 80.0, 0.0, 10.0, 80.0, 0.0};
  run.nominalSize = "4in";
  run.wallThicknessIn = 0.237;
  st.cadPipeRuns.push_back(run);
  st.cadPipeRunAttrs.push_back(EntityAttributes{});
  EnsureAttrCounts(st);
  REQUIRE(st.userLineAttrs.size() == 2);
  REQUIRE(st.cadSolids.size() == 1);
  AddLayer(st, "WALLS", "#00FF00");
  AddLayer(st, "PIPE", "#0000FF");
  st.selection.clear();
  return st;
}

/// A viewport click that picked \p e during a property command.
void Pick(AppCommandState& st, SelectedEntity e, std::vector<std::string>& log) {
  st.selection.push_back(e);
  CadPropCommandSelectionChanged(st, log);
}

bool LogHas(const std::vector<std::string>& log, const std::string& text) {
  return std::any_of(log.begin(), log.end(),
                     [&](const std::string& s) { return s.find(text) != std::string::npos; });
}

}  // namespace

TEST_CASE("Typed colours become colour storage, and bad ones are refused (REQ-356)", "[req356]") {
  std::string out;
  CHECK((CadColorStorageFromTyped("bylayer", &out) && out == "ByLayer"));
  CHECK((CadColorStorageFromTyped("BYBLOCK", &out) && out == "ByBlock"));
  CHECK((CadColorStorageFromTyped("1", &out) && out == CadColorStorageFromAci(1)));
  CHECK((CadColorStorageFromTyped("255", &out) && out == CadColorStorageFromAci(255)));
  CHECK((CadColorStorageFromTyped("red", &out) && out == CadColorStorageFromAci(1)));
  CHECK((CadColorStorageFromTyped("Blue", &out) && out == CadColorStorageFromAci(5)));
  CHECK((CadColorStorageFromTyped("#12AbEf", &out) && CadColorStorageMatches(out, "#12ABEF")));
  out = "untouched";
  CHECK_FALSE(CadColorStorageFromTyped("0", &out));
  CHECK_FALSE(CadColorStorageFromTyped("256", &out));
  CHECK_FALSE(CadColorStorageFromTyped("12a", &out));
  CHECK_FALSE(CadColorStorageFromTyped("#12345", &out));
  CHECK_FALSE(CadColorStorageFromTyped("#12345G", &out));
  CHECK_FALSE(CadColorStorageFromTyped("purple", &out));
  CHECK(out == "untouched");
}

TEST_CASE("The three commands are typeable, route clicks to selection and cancel cleanly (REQ-356)",
          "[req356]") {
  using K = AppCommandState::Kind;
  AppCommandState st = Drawing();
  std::vector<std::string> log;

  Submit(st, "CHPROP", log);
  CHECK(st.active == K::ChProp);
  CHECK(ViewportClickRouteFor(st) == ViewportClickRoute::SelectionAccumulate);
  CancelActiveCommand(st, log);
  CHECK(st.active == K::None);

  Submit(st, "MA", log);
  CHECK(st.active == K::MatchProp);
  CHECK(ViewportClickRouteFor(st) == ViewportClickRoute::SelectionAccumulate);
  CancelActiveCommand(st, log);

  Submit(st, "LAYMCUR", log);
  CHECK(st.active == K::LayMCur);
  CHECK(ViewportClickRouteFor(st) == ViewportClickRoute::SelectionAccumulate);
  CancelActiveCommand(st, log);
  CHECK(st.active == K::None);
  CHECK(st.selection.empty());

  // Past its selection, CHPROP's steps are typed, so a click means nothing to it.
  st.selection = {Sel(SelectedEntity::Type::LineSeg, 0)};
  Submit(st, "CHPROP", log);
  CHECK(st.propCmdPhase == AppCommandState::PropCmdPhase::WaitProperty);
  CHECK(ViewportClickRouteFor(st) == ViewportClickRoute::Ignore);
}

TEST_CASE("CHPROP changes colour, layer, linetype and lineweight, one undo step each (REQ-356)",
          "[req356][chprop]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  st.selection = {Sel(SelectedEntity::Type::LineSeg, 0), Sel(SelectedEntity::Type::LineSeg, 1)};
  const size_t undo0 = CadActiveUndoStackSize(st);

  Submit(st, "CHPROP", log);  // the held selection is the pick
  REQUIRE(st.propCmdPhase == AppCommandState::PropCmdPhase::WaitProperty);
  Submit(st, "C", log);
  REQUIRE(st.propCmdPhase == AppCommandState::PropCmdPhase::WaitValue);
  Submit(st, "red", log);
  CHECK(st.userLineAttrs[0].color == CadColorStorageFromAci(1));
  CHECK(st.userLineAttrs[1].color == CadColorStorageFromAci(1));
  CHECK(LogHas(log, "set on 2 object(s)"));
  CHECK(CadActiveUndoStackSize(st) == undo0 + 1);

  Submit(st, "LA", log);
  Submit(st, "walls", log);  // case-insensitive, stored as the table spells it
  CHECK(st.userLineAttrs[0].layer == "WALLS");
  CHECK(CadActiveUndoStackSize(st) == undo0 + 2);

  Submit(st, "LTYPE dashed", log);  // property and value on one line
  CHECK(st.userLineAttrs[0].linetype == "DASHED");
  CHECK(st.userLineAttrs[1].linetype == "DASHED");
  CHECK(CadActiveUndoStackSize(st) == undo0 + 3);

  Submit(st, "LW", log);
  Submit(st, "0.35", log);
  CHECK(st.userLineAttrs[0].lineweightMm == 0.35f);
  CHECK(CadActiveUndoStackSize(st) == undo0 + 4);

  Submit(st, "", log);  // Enter at the property prompt ends it
  CHECK(st.active == AppCommandState::Kind::None);

  REQUIRE(DoUndo(st, log));
  CHECK(st.userLineAttrs[0].lineweightMm == -1.f);
  CHECK(st.userLineAttrs[0].linetype == "DASHED");  // one step back is one property back
  REQUIRE(DoUndo(st, log));
  CHECK(st.userLineAttrs[0].linetype == "ByLayer");
}

TEST_CASE("CHPROP with nothing held collects a window selection first (REQ-356)", "[req356][chprop]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  Submit(st, "CHPROP", log);
  REQUIRE(st.propCmdPhase == AppCommandState::PropCmdPhase::SelectObjects);

  Submit(st, "", log);  // Enter with nothing picked: asks again rather than moving on
  CHECK(st.propCmdPhase == AppCommandState::PropCmdPhase::SelectObjects);
  CHECK(LogHas(log, "Nothing selected"));

  // A window around the first line only, as the viewport's second corner submits it.
  st.selBoxWaitingSecond = true;
  st.selBoxAnchorX = -1.0;
  st.selBoxAnchorY = -1.0;
  SubmitViewportPick(st, 11.0, 1.0, log);
  REQUIRE(st.selection.size() == 1);
  CHECK(st.active == AppCommandState::Kind::ChProp);  // CHPROP waits for Enter, as MOVE does

  Submit(st, "", log);
  REQUIRE(st.propCmdPhase == AppCommandState::PropCmdPhase::WaitProperty);
  Submit(st, "COLOR 3", log);
  CHECK(st.userLineAttrs[0].color == CadColorStorageFromAci(3));
  CHECK(st.userLineAttrs[1].color == "ByLayer");
}

TEST_CASE("CHPROP colours a solid and a pipe run and skips them for linetype (REQ-356)",
          "[req356][chprop][solid][piperun]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  st.selection = {Sel(SelectedEntity::Type::Solid, 0), Sel(SelectedEntity::Type::PipeRun, 0),
                  Sel(SelectedEntity::Type::LineSeg, 0)};
  Submit(st, "CHPROP", log);
  Submit(st, "C #112233", log);
  CHECK(CadColorStorageMatches(st.cadSolidAttrs[0].color, "#112233"));
  CHECK(CadColorStorageMatches(st.cadPipeRunAttrs[0].color, "#112233"));
  CHECK(CadColorStorageMatches(st.userLineAttrs[0].color, "#112233"));

  const size_t undo0 = CadActiveUndoStackSize(st);
  Submit(st, "LT HIDDEN", log);
  CHECK(st.userLineAttrs[0].linetype == "HIDDEN");
  CHECK(st.cadSolidAttrs[0].linetype == "ByLayer");
  CHECK(st.cadPipeRunAttrs[0].linetype == "ByLayer");
  CHECK(LogHas(log, "set on 1 object(s); 2 skipped"));
  CHECK(CadActiveUndoStackSize(st) == undo0 + 1);

  Submit(st, "LW 0.5", log);
  CHECK(st.cadSolidAttrs[0].lineweightMm == -1.f);
  CHECK(st.userLineAttrs[0].lineweightMm == 0.5f);
}

TEST_CASE("CHPROP refuses bad values and changes nothing (REQ-356)", "[req356][chprop]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  st.selection = {Sel(SelectedEntity::Type::LineSeg, 0)};
  const EntityAttributes before = st.userLineAttrs[0];
  const size_t undo0 = CadActiveUndoStackSize(st);
  const size_t layers0 = st.drawingLayerTable.size();
  Submit(st, "CHPROP", log);

  Submit(st, "LA NO-SUCH-LAYER", log);
  CHECK(LogHas(log, "does not exist"));
  Submit(st, "C 256", log);
  CHECK(LogHas(log, "is not a color"));
  Submit(st, "LT DOTTED", log);
  CHECK(LogHas(log, "is not one GoSurvey offers"));
  Submit(st, "LW 0.33", log);
  CHECK(LogHas(log, "is not a lineweight"));
  Submit(st, "THICKNESS", log);
  CHECK(LogHas(log, "is not a property"));

  // A refused value at the value prompt asks for the same property again.
  Submit(st, "C", log);
  Submit(st, "chartreuse", log);
  CHECK(st.propCmdPhase == AppCommandState::PropCmdPhase::WaitValue);

  CHECK(st.userLineAttrs[0].layer == before.layer);
  CHECK(st.userLineAttrs[0].color == before.color);
  CHECK(st.userLineAttrs[0].linetype == before.linetype);
  CHECK(st.userLineAttrs[0].lineweightMm == before.lineweightMm);
  CHECK(CadActiveUndoStackSize(st) == undo0);
  CHECK(st.drawingLayerTable.size() == layers0);  // no typo layer was created
}

TEST_CASE("MATCHPROP copies a line's properties per pick, and layer and colour onto a solid (REQ-356)",
          "[req356][matchprop]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  EntityAttributes& src = st.userLineAttrs[0];
  src.layer = "WALLS";
  src.color = CadColorStorageFromAci(1);
  src.linetype = "CENTER";
  src.lineweightMm = 0.5f;

  Submit(st, "MATCHPROP", log);
  Pick(st, Sel(SelectedEntity::Type::LineSeg, 0), log);  // the source
  REQUIRE(st.propCmdPhase == AppCommandState::PropCmdPhase::SelectTargets);
  CHECK(st.selection.empty());

  const size_t undo0 = CadActiveUndoStackSize(st);
  Pick(st, Sel(SelectedEntity::Type::LineSeg, 1), log);
  CHECK(st.userLineAttrs[1].layer == "WALLS");
  CHECK(st.userLineAttrs[1].color == CadColorStorageFromAci(1));
  CHECK(st.userLineAttrs[1].linetype == "CENTER");
  CHECK(st.userLineAttrs[1].lineweightMm == 0.5f);
  CHECK(CadActiveUndoStackSize(st) == undo0 + 1);

  Pick(st, Sel(SelectedEntity::Type::Solid, 0), log);
  CHECK(st.cadSolidAttrs[0].layer == "WALLS");
  CHECK(st.cadSolidAttrs[0].color == CadColorStorageFromAci(1));
  CHECK(st.cadSolidAttrs[0].linetype == "ByLayer");  // no linetype on a solid body
  CHECK(st.cadSolidAttrs[0].lineweightMm == -1.f);
  CHECK(CadActiveUndoStackSize(st) == undo0 + 2);  // one step per destination pick

  Submit(st, "", log);
  CHECK(st.active == AppCommandState::Kind::None);

  REQUIRE(DoUndo(st, log));
  CHECK(st.cadSolidAttrs[0].layer == "C-SOLID");  // where BOX created it (REQ-361)
  CHECK(st.userLineAttrs[1].layer == "WALLS");
}

TEST_CASE("MATCHPROP takes a held single object as the source; a solid source copies no linetype (REQ-356)",
          "[req356][matchprop]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  st.cadSolidAttrs[0].layer = "PIPE";
  st.cadSolidAttrs[0].color = CadColorStorageFromAci(4);
  st.userLineAttrs[1].linetype = "HIDDEN";
  st.selection = {Sel(SelectedEntity::Type::Solid, 0)};

  Submit(st, "MATCHPROP", log);
  REQUIRE(st.propCmdPhase == AppCommandState::PropCmdPhase::SelectTargets);
  Pick(st, Sel(SelectedEntity::Type::LineSeg, 1), log);
  CHECK(st.userLineAttrs[1].layer == "PIPE");
  CHECK(st.userLineAttrs[1].color == CadColorStorageFromAci(4));
  CHECK(st.userLineAttrs[1].linetype == "HIDDEN");  // the source had none to give

  // A window of destinations applies at once, too.
  st.selBoxWaitingSecond = true;
  st.selBoxAnchorX = -1.0;
  st.selBoxAnchorY = -1.0;
  SubmitViewportPick(st, 11.0, 1.0, log);
  CHECK(st.userLineAttrs[0].layer == "PIPE");
  CHECK(st.selection.empty());
  CHECK(st.active == AppCommandState::Kind::MatchProp);
}

TEST_CASE("LAYMCUR makes a picked solid's layer current and refuses two layers (REQ-356)",
          "[req356][laymcur]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  st.cadSolidAttrs[0].layer = "PIPE";
  st.userLineAttrs[0].layer = "WALLS";

  Submit(st, "LAYMCUR", log);
  Pick(st, Sel(SelectedEntity::Type::Solid, 0), log);
  CHECK(st.currentLayer == "PIPE");
  CHECK(st.active == AppCommandState::Kind::None);

  // Held selection on two layers: refused, current layer unchanged, still asking.
  st.selection = {Sel(SelectedEntity::Type::Solid, 0), Sel(SelectedEntity::Type::LineSeg, 0)};
  Submit(st, "LAYMCUR", log);
  CHECK(st.currentLayer == "PIPE");
  CHECK(LogHas(log, "more than one layer"));
  CHECK(st.active == AppCommandState::Kind::LayMCur);
  Pick(st, Sel(SelectedEntity::Type::LineSeg, 0), log);
  CHECK(st.currentLayer == "WALLS");
}

TEST_CASE("The current colour colours new objects; a pipe run keeps its size colour (REQ-356)",
          "[req356][currentcolor]") {
  AppCommandState st;
  std::vector<std::string> log;
  REQUIRE(st.currentColor == "ByLayer");
  CadRibbonPickColor(st, CadColorStorageFromAci(1), log);  // nothing selected: sets the current colour
  CHECK(st.currentColor == CadColorStorageFromAci(1));

  AddLine(st, 0.f, log);
  Submit(st, "BOX 0,40 20 10 8", log);
  EnsureAttrCounts(st);
  REQUIRE(st.userLineAttrs.size() == 1);
  REQUIRE(st.cadSolidAttrs.size() == 1);
  CHECK(st.userLineAttrs[0].color == CadColorStorageFromAci(1));
  CHECK(st.cadSolidAttrs[0].color == CadColorStorageFromAci(1));

  StartPipeRunCommand(st, log);
  REQUIRE(HandlePipeRunTextInput("4in", st, log));
  REQUIRE(HandlePipeRunTextInput("", st, log));
  SubmitPipeRunViewportPick(st, 0.f, 80.f, log);
  SubmitPipeRunViewportPick(st, 20.f, 80.f, log);
  REQUIRE(HandlePipeRunTextInput("end", st, log));
  REQUIRE(st.cadPipeRunAttrs.size() == 1);
  CHECK(CadColorStorageMatches(st.cadPipeRunAttrs[0].color, "#2D6CDF"));  // REQ-353's 4in blue

  const std::string json = SerializeGoSurveyJson(st);
  AppCommandState back;
  REQUIRE(LoadGoSurveyFromJsonUtf8(back, json, log));
  CHECK(back.currentColor == CadColorStorageFromAci(1));

  AppCommandState old;  // a drawing saved before the field existed opens ByLayer
  old.currentColor = CadColorStorageFromAci(3);
  std::string legacy = json;
  const size_t at = legacy.find("\"currentColor\"");
  REQUIRE(at != std::string::npos);
  legacy.replace(at, std::string("\"currentColor\"").size(), "\"unusedColor\"");
  REQUIRE(LoadGoSurveyFromJsonUtf8(old, legacy, log));
  CHECK(old.currentColor == "ByLayer");
}

TEST_CASE("The ribbon colour pick recolours a selection without touching the current colour (REQ-356)",
          "[req356][ribbon]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  st.selection = {Sel(SelectedEntity::Type::LineSeg, 0), Sel(SelectedEntity::Type::Solid, 0)};
  CHECK(CadSelectionColor(st) == "ByLayer");
  const size_t undo0 = CadActiveUndoStackSize(st);

  CadRibbonPickColor(st, CadColorStorageFromAci(2), log);
  CHECK(st.userLineAttrs[0].color == CadColorStorageFromAci(2));
  CHECK(st.cadSolidAttrs[0].color == CadColorStorageFromAci(2));
  CHECK(st.currentColor == "ByLayer");
  CHECK(CadActiveUndoStackSize(st) == undo0 + 1);

  st.userLineAttrs[0].color = CadColorStorageFromAci(5);
  CHECK(std::string(CadSelectionColor(st)) == kCadSelectionColorVaries);

  st.selection.clear();
  CHECK(CadSelectionColor(st).empty());
}

TEST_CASE("A Properties-panel linetype or lineweight edit is one undo step (REQ-356)", "[req356]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  st.selection = {Sel(SelectedEntity::Type::LineSeg, 0), Sel(SelectedEntity::Type::LineSeg, 1)};
  const size_t undo0 = CadActiveUndoStackSize(st);
  CHECK(CadApplyLinetypeToSelection(st, "PHANTOM") == 2);
  CHECK(CadActiveUndoStackSize(st) == undo0 + 1);
  CHECK(CadApplyLinetypeToSelection(st, "PHANTOM") == 0);  // no change, no step
  CHECK(CadActiveUndoStackSize(st) == undo0 + 1);
  CHECK(CadApplyLineweightToSelection(st, 0.7f) == 2);
  REQUIRE(DoUndo(st, log));
  CHECK(st.userLineAttrs[0].lineweightMm == -1.f);
  REQUIRE(DoUndo(st, log));
  CHECK(st.userLineAttrs[0].linetype == "ByLayer");
  CHECK(st.userLineAttrs[1].linetype == "ByLayer");
}
