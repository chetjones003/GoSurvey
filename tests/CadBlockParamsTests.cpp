// GitHub issue #808 stage 1 — REQ-523 (Test Block), REQ-524 (Parameters Manager), REQ-525 (Block
// Properties Table). The pure edit functions are tested directly; the commands are driven through
// ProcessCommandLineSubmit, the same path a typed command uses.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "CadBlocks.hpp"
#include "CadCommands.hpp"
#include "GsIoTestSupport.hpp"
#include "util/cadblockparams.hpp"

using Catch::Approx;

namespace {

void Submit(AppCommandState& st, const std::string& text, std::vector<std::string>& log) {
  char buf[512];
  std::snprintf(buf, sizeof(buf), "%s", text.c_str());
  ProcessCommandLineSubmit(buf, sizeof(buf), st, log);
}

bool LogHas(const std::vector<std::string>& log, const char* needle) {
  for (const std::string& line : log) {
    if (line.find(needle) != std::string::npos)
      return true;
  }
  return false;
}

/// The last line a command wrote, so a check reads the reply to that command and not an older one.
std::string Last(const std::vector<std::string>& log) { return log.empty() ? std::string() : log.back(); }

std::size_t UndoDepth(const AppCommandState& st) {
  return st.documents[static_cast<std::size_t>(st.activeDrawingIdx)].undoStack.size();
}

CadBlockParameter Param(const char* name, CadBlockParamKind kind, float value) {
  CadBlockParameter p;
  p.name = name;
  p.kind = kind;
  p.value = value;
  return p;
}

/// A block with a width (0..10, value 2), a visibility parameter with two states, and one stretch
/// action bound to the width.
CadBlockDefinition DoorDef() {
  CadBlockDefinition def;
  def.name = "DOOR";
  def.content.lines = {0., 0., 0., 2., 0., 0.};
  def.content.lineAttrs.push_back(EntityAttributes{});
  def.content.lineVis.push_back("");
  CadBlockParameter width = Param("Width", CadBlockParamKind::Linear, 2.f);
  width.maxValue = 10.f;
  def.parameters.push_back(width);
  def.parameters.push_back(Param("Look", CadBlockParamKind::Visibility, 0.f));
  def.visibilityStates = {"Open", "Closed"};
  CadBlockAction stretch;
  stretch.paramName = "Width";
  def.actions.push_back(stretch);
  return def;
}

/// A drawing with DOOR defined and open in the block editor.
AppCommandState EditingDoor(std::vector<std::string>& log) {
  AppCommandState st;
  st.documents.resize(2);
  st.activeDrawingIdx = 1;
  st.blockDefs.push_back(DoorDef());
  Submit(st, "BEDIT DOOR", log);
  REQUIRE(st.blockEditActive);
  return st;
}

const CadBlockDefinition& Door(const AppCommandState& st) {
  const int di = CadBlockFindDef(st.blockDefs, "DOOR");
  REQUIRE(di >= 0);
  return st.blockDefs[static_cast<std::size_t>(di)];
}

}  // namespace

// --- pure functions -------------------------------------------------------------------------------

TEST_CASE("block parameter numbers parse whole tokens only", "[block][req524][issue808]") {
  float v = 0.f;
  CHECK(CadBlockParseNumber(" 2.5 ", &v));
  CHECK(v == Approx(2.5f));
  CHECK(CadBlockParseNumber("+3", &v));
  CHECK(CadBlockParseNumber("-1e2", &v));
  CHECK(v == Approx(-100.f));
  CHECK_FALSE(CadBlockParseNumber("", &v));
  CHECK_FALSE(CadBlockParseNumber("2,5", &v));
  CHECK_FALSE(CadBlockParseNumber("2.5x", &v));
  CHECK_FALSE(CadBlockParseNumber("nan", &v));
  CHECK_FALSE(CadBlockParseNumber("inf", &v));
  CHECK_FALSE(CadBlockParseNumber("1e60", &v));  // finite as a double, not as the float that is stored
  int n = 0;
  CHECK(CadBlockParseListNumber("12", &n));
  CHECK(n == 12);
  CHECK_FALSE(CadBlockParseListNumber("0", &n));
  CHECK_FALSE(CadBlockParseListNumber("-1", &n));
  CHECK_FALSE(CadBlockParseListNumber("1.0", &n));
  CHECK_FALSE(CadBlockParseListNumber("99999999999", &n));
}

TEST_CASE("a formatted table value round-trips at the upper float limit", "[block][req525][issue808]") {
  CadBlockDefinition def = DoorDef();
  CadBlockParameter& width = def.parameters[0];
  width.minValue = 0.f;
  width.value = width.maxValue = 1.2345678f;
  REQUIRE(CadBlockTableAddColumn(def, 0).empty());
  std::string stored;
  CHECK(CadBlockTableCanonCell(def, width, "1.2345678", &stored).empty());
  float roundTrip = 0.f;
  REQUIRE(CadBlockParseNumber(stored, &roundTrip));
  CHECK(roundTrip <= width.maxValue);
  CHECK(roundTrip >= width.minValue);
}

TEST_CASE("a parameter is addressed by list number or unique name", "[block][req524][issue808]") {
  CadBlockDefinition def = DoorDef();
  std::string err;
  CHECK(CadBlockResolveParameter(def, "1", &err) == 0);
  CHECK(CadBlockResolveParameter(def, "look", &err) == 1);
  CHECK(CadBlockResolveParameter(def, "3", &err) == -1);
  CHECK(err.find("there is no parameter 3") != std::string::npos);
  CHECK(CadBlockResolveParameter(def, "0", &err) == -1);
  CHECK(CadBlockResolveParameter(def, "Height", &err) == -1);
  CHECK(err.find("no parameter named") != std::string::npos);
  def.parameters.push_back(Param("WIDTH", CadBlockParamKind::Linear, 1.f));
  CHECK(CadBlockResolveParameter(def, "Width", &err) == -1);
  CHECK(err.find("use the list number") != std::string::npos);
  CHECK(CadBlockResolveParameter(def, "3", &err) == 2);
}

TEST_CASE("renaming a parameter carries its actions and table column", "[block][req524][issue808]") {
  CadBlockDefinition def = DoorDef();
  REQUIRE(CadBlockTableAddColumn(def, 0).empty());
  CHECK(CadBlockParamRename(def, 0, "  Leaf  ").empty());
  CHECK(def.parameters[0].name == "Leaf");
  CHECK(def.actions[0].paramName == "Leaf");
  CHECK(def.propertyTable.columns[0] == "Leaf");

  const CadBlockDefinition before = def;
  CHECK(CadBlockParamRename(def, 0, "   ").find("needs a name") != std::string::npos);
  CHECK(CadBlockParamRename(def, 0, "a,b").find("comma") != std::string::npos);
  CHECK(CadBlockParamRename(def, 0, "123").find("only digits") != std::string::npos);
  CHECK(CadBlockParamRename(def, 0, "LOOK").find("already used") != std::string::npos);
  CHECK(CadBlockParamRename(def, 9, "X").find("no longer exists") != std::string::npos);
  CHECK(CadBlockAuthoringEqual(def, before));
  CHECK(CadBlockParamRename(def, 0, "LEAF").empty());  // a change of case only is the same parameter
  CHECK(def.parameters[0].name == "LEAF");
}

TEST_CASE("a shared parameter name keeps its actions when one is renamed or deleted", "[block][req524][issue808]") {
  CadBlockDefinition def = DoorDef();
  def.parameters.push_back(Param("Width", CadBlockParamKind::Linear, 1.f));
  CHECK(CadBlockParamRename(def, 2, "Other").empty());
  CHECK(def.actions[0].paramName == "Width");

  def.parameters[2].name = "Width";
  int removed = -1;
  CHECK(CadBlockParamDelete(def, 2, &removed).empty());
  CHECK(removed == 0);
  CHECK(def.actions.size() == 1);
}

TEST_CASE("a table owner cannot leave its column bound to a duplicate parameter", "[block][req524][req525][issue808]") {
  CadBlockDefinition def;
  def.parameters.push_back(Param("D", CadBlockParamKind::Linear, 50.f));
  def.parameters.push_back(Param("D", CadBlockParamKind::Linear, 5.f));
  def.parameters[0].minValue = 0.f;
  def.parameters[0].maxValue = 100.f;
  def.parameters[1].minValue = 0.f;
  def.parameters[1].maxValue = 10.f;
  def.propertyTable.columns.push_back("D");
  def.propertyTable.rows.push_back({"50"});

  const CadBlockDefinition before = def;
  CHECK(CadBlockParamRename(def, 0, "Distance").find("remove the \"D\" table column") != std::string::npos);
  CHECK(CadBlockAuthoringEqual(def, before));
  int removed = 0;
  CHECK(CadBlockParamDelete(def, 0, &removed).find("remove the \"D\" table column") != std::string::npos);
  CHECK(CadBlockAuthoringEqual(def, before));
}

TEST_CASE("parameter value and limits are checked against each other", "[block][req524][issue808]") {
  CadBlockDefinition def = DoorDef();  // Width: value 2, 0..10
  using F = CadBlockParamField;
  CHECK(CadBlockParamSetNumber(def, 0, F::Value, "10").empty());  // the maximum itself is in range
  CHECK(def.parameters[0].value == Approx(10.f));
  CHECK(CadBlockParamSetNumber(def, 0, F::Value, "0").empty());   // and the minimum
  CHECK(CadBlockParamSetNumber(def, 0, F::Value, "10.5").find("from 0 to 10") != std::string::npos);
  CHECK(CadBlockParamSetNumber(def, 0, F::Value, "-0.1").find("from 0 to 10") != std::string::npos);
  CHECK(CadBlockParamSetNumber(def, 0, F::Value, "wide").find("must be a number") != std::string::npos);
  CHECK(def.parameters[0].value == Approx(0.f));

  REQUIRE(CadBlockParamSetNumber(def, 0, F::Value, "4").empty());
  CHECK(CadBlockParamSetNumber(def, 0, F::Min, "4").empty());   // equal to the value: allowed
  CHECK(CadBlockParamSetNumber(def, 0, F::Min, "4.5").find("current value (4)") != std::string::npos);
  CHECK(CadBlockParamSetNumber(def, 0, F::Min, "11").find("above the maximum (10)") != std::string::npos);
  CHECK(CadBlockParamSetNumber(def, 0, F::Max, "4").empty());
  CHECK(CadBlockParamSetNumber(def, 0, F::Max, "3").find("below the minimum (4)") != std::string::npos);
  CHECK(CadBlockParamSetNumber(def, 0, F::Max, "x").find("maximum must be a number") != std::string::npos);
  CHECK(def.parameters[0].minValue == Approx(4.f));
  CHECK(def.parameters[0].maxValue == Approx(4.f));
}

TEST_CASE("a limit may not strand a block-table cell", "[block][req524][req525][issue808]") {
  CadBlockDefinition def = DoorDef();
  REQUIRE(CadBlockTableAddColumn(def, 0).empty());
  REQUIRE(CadBlockTableAddRow(def, {"8"}).empty());
  REQUIRE(CadBlockTableAddRow(def, {"1"}).empty());
  using F = CadBlockParamField;
  CHECK(CadBlockParamSetNumber(def, 0, F::Max, "7").find("row 1 of the block table holds 8") != std::string::npos);
  CHECK(CadBlockParamSetNumber(def, 0, F::Min, "1.5").find("row 2 of the block table holds 1") != std::string::npos);
  CHECK(CadBlockParamSetNumber(def, 0, F::Max, "8").empty());  // a cell on the limit stays valid
  CHECK(CadBlockParamSetNumber(def, 0, F::Min, "1").empty());
}

TEST_CASE("deleting a parameter removes its actions and table column", "[block][req524][issue808]") {
  CadBlockDefinition def = DoorDef();
  REQUIRE(CadBlockTableAddColumn(def, 0).empty());
  REQUIRE(CadBlockTableAddColumn(def, 1).empty());
  REQUIRE(CadBlockTableAddRow(def, {"3", "closed"}).empty());
  int removed = 0;
  CHECK(CadBlockParamDelete(def, 0, &removed).empty());
  CHECK(removed == 1);
  CHECK(def.actions.empty());
  REQUIRE(def.propertyTable.columns.size() == 1);
  CHECK(def.propertyTable.columns[0] == "Look");
  CHECK(def.propertyTable.rows[0] == std::vector<std::string>{"Closed"});
  CHECK(CadBlockParamDelete(def, 0, &removed).empty());  // the last column takes the rows with it
  CHECK(def.propertyTable.columns.empty());
  CHECK(def.propertyTable.rows.empty());
  CHECK(CadBlockParamDelete(def, 0, &removed).find("no longer exists") != std::string::npos);
}

TEST_CASE("block table columns", "[block][req525][issue808]") {
  CadBlockDefinition def = DoorDef();
  CHECK(CadBlockTableAddRow(def, {}).find("add a parameter to the table first") != std::string::npos);
  CHECK(CadBlockTableAddColumn(def, 0).empty());
  CHECK(CadBlockTableAddColumn(def, 0).find("already in the table") != std::string::npos);
  CHECK(CadBlockTableAddColumn(def, 5).find("no longer exists") != std::string::npos);
  REQUIRE(CadBlockTableAddRow(def, {"5"}).empty());
  // A later column gives existing rows the parameter's current value: the first visibility state.
  CHECK(CadBlockTableAddColumn(def, 1).empty());
  CHECK(def.propertyTable.rows[0] == std::vector<std::string>{"5", "Open"});
  CHECK(CadBlockTableRemoveColumn(def, "Height").find("not in the table") != std::string::npos);
  CHECK(CadBlockTableRemoveColumn(def, "width").empty());
  CHECK(def.propertyTable.rows[0] == std::vector<std::string>{"Open"});
  CHECK(CadBlockTableRemoveColumn(def, "Look").empty());
  CHECK(def.propertyTable.rows.empty());
  CHECK(CadBlockTableClear(def).find("no table") != std::string::npos);

  def.parameters.push_back(Param("Width", CadBlockParamKind::Linear, 1.f));
  CHECK(CadBlockTableAddColumn(def, 0).find("more than one parameter is named") != std::string::npos);
}

TEST_CASE("block table rows and values", "[block][req525][issue808]") {
  CadBlockDefinition def = DoorDef();
  REQUIRE(CadBlockTableAddColumn(def, 0).empty());
  REQUIRE(CadBlockTableAddColumn(def, 1).empty());
  CHECK(CadBlockTableAddRow(def, {}).empty());  // defaults: the value 2 and the first state
  CHECK(def.propertyTable.rows[0] == std::vector<std::string>{"2", "Open"});
  CHECK(CadBlockTableAddRow(def, {}).find("row 1 already holds those values") != std::string::npos);
  CHECK(CadBlockTableAddRow(def, {"3"}).find("2 column(s) but 1 value(s)") != std::string::npos);
  CHECK(CadBlockTableAddRow(def, {"10", "CLOSED"}).empty());  // the maximum; state spelling stored
  CHECK(def.propertyTable.rows[1] == std::vector<std::string>{"10", "Closed"});
  CHECK(CadBlockTableAddRow(def, {"0", "Open"}).empty());     // the minimum
  CHECK(CadBlockTableAddRow(def, {"10.01", "Open"}).find("must be from 0 to 10") != std::string::npos);
  CHECK(CadBlockTableAddRow(def, {"wide", "Open"}).find("\"Width\" must be a number") != std::string::npos);
  CHECK(CadBlockTableAddRow(def, {"4", "Ajar"}).find("visibility states (Open, Closed)") != std::string::npos);
  CHECK(CadBlockTableAddRow(def, {"4", ""}).find("visibility states") != std::string::npos);
  CHECK(def.propertyTable.rows.size() == 3);

  CHECK(CadBlockTableSetCell(def, 1, "width", "6").empty());
  CHECK(def.propertyTable.rows[0][0] == "6");
  CHECK(CadBlockTableSetCell(def, 1, "Width", "11").find("must be from 0 to 10") != std::string::npos);
  CHECK(CadBlockTableSetCell(def, 4, "Width", "1").find("there is no row 4 (the table has 3)") != std::string::npos);
  CHECK(CadBlockTableSetCell(def, 0, "Width", "1").find("there is no row 0") != std::string::npos);
  CHECK(CadBlockTableSetCell(def, 1, "Height", "1").find("not in the table") != std::string::npos);
  // Row 1 is (6, Open); making it (0, Open) would copy row 3.
  CHECK(CadBlockTableSetCell(def, 1, "Width", "0").find("row 3 already holds those values") != std::string::npos);
  CHECK(CadBlockTableSetCell(def, 1, "Width", "6").empty());  // a cell set to itself is not a duplicate
  CHECK(CadBlockTableRemoveRow(def, 4).find("there is no row 4") != std::string::npos);
  CHECK(CadBlockTableRemoveRow(def, 2).empty());
  CHECK(def.propertyTable.rows.size() == 2);
  CHECK(CadBlockTableClear(def).empty());
  CHECK(def.propertyTable.columns.empty());
}

TEST_CASE("block table default cells obey parameter limits", "[block][req525][issue808]") {
  CadBlockDefinition def;
  def.parameters.push_back(Param("Distance", CadBlockParamKind::Linear, -45.f));
  CHECK(CadBlockTableAddColumn(def, 0).find("must be from") != std::string::npos);
  def.propertyTable.columns.push_back("Distance");
  CHECK(CadBlockTableAddRow(def, {}).find("must be from") != std::string::npos);
  CHECK(def.propertyTable.rows.empty());
}

TEST_CASE("a visibility column of a block with no states stays empty", "[block][req525][issue808]") {
  CadBlockDefinition def = DoorDef();
  def.visibilityStates.clear();
  REQUIRE(CadBlockTableAddColumn(def, 1).empty());
  CHECK(CadBlockTableAddRow(def, {"Open"}).find("must be left empty") != std::string::npos);
  CHECK(CadBlockTableAddRow(def, {""}).empty());
}

TEST_CASE("adding the first visibility state preserves empty table cells on reload", "[block][req525][issue808][io]") {
  AppCommandState state;
  CadBlockDefinition definition;
  definition.name = "VISIBILITY";
  definition.parameters.push_back(Param("Visibility", CadBlockParamKind::Visibility, 0.f));
  REQUIRE(CadBlockTableAddColumn(definition, 0).empty());
  REQUIRE(CadBlockTableAddRow(definition, {}).empty());
  state.blockDefs.push_back(definition);
  std::vector<std::string> log;
  Submit(state, "BEDIT VISIBILITY", log);
  REQUIRE(state.blockEditActive);
  Submit(state, "BVISIBILITY Open", log);
  CHECK(state.blockDefs[0].propertyTable.rows[0][0] == "Open");

  AppCommandState reopened;
  REQUIRE(LoadGoSurveyFromJsonUtf8(reopened, SerializeGoSurveyJson(state), log));
  const int reopenedIndex = CadBlockFindDef(reopened.blockDefs, "VISIBILITY");
  REQUIRE(reopenedIndex >= 0);
  REQUIRE(reopened.blockDefs[static_cast<size_t>(reopenedIndex)].propertyTable.rows.size() == 1);
  CHECK(reopened.blockDefs[static_cast<size_t>(reopenedIndex)].propertyTable.rows[0][0] == "Open");
}

TEST_CASE("a table row sets a reference's parameters and visibility state", "[block][req525][issue808]") {
  CadBlockDefinition def = DoorDef();
  REQUIRE(CadBlockTableAddColumn(def, 0).empty());
  REQUIRE(CadBlockTableAddColumn(def, 1).empty());
  REQUIRE(CadBlockTableAddRow(def, {"7", "Closed"}).empty());
  CadBlockRef ref;
  ref.defName = "DOOR";
  CHECK_FALSE(CadBlockTableApplyRow(def, 2, &ref));
  CHECK_FALSE(CadBlockTableApplyRow(def, 0, &ref));
  CHECK(ref.paramState.empty());
  CHECK(CadBlockTableApplyRow(def, 1, &ref));
  CHECK(CadBlockParamValue(ref, def, "Width") == Approx(7.f));
  CHECK(ref.visState == "Closed");
}

// --- PARAMETERS (REQ-524) -------------------------------------------------------------------------

TEST_CASE("PARAMETERS lists, and refuses outside the block editor", "[block][req524][issue808]") {
  AppCommandState plain;
  plain.documents.resize(2);
  plain.activeDrawingIdx = 1;
  std::vector<std::string> log;
  Submit(plain, "PARAMETERS", log);
  CHECK(Last(log) == "PARAMETERS — open a block with BEDIT first.");
  CHECK_FALSE(plain.showBlockParametersWindow);

  AppCommandState st = EditingDoor(log);
  Submit(st, "ATTDEF TAG, Prompt, 12", log);
  log.clear();
  Submit(st, "PARAMETERS", log);
  CHECK(LogHas(log, "PARAMETERS — 1. Width (Linear) value 2, minimum 0, maximum 10, actions 1."));
  CHECK(LogHas(log, "PARAMETERS — 2. Look (Visibility)"));
  CHECK(LogHas(log, "PARAMETERS — attribute TAG = 12."));
  CHECK(st.showBlockParametersWindow);

  AppCommandState empty;
  empty.documents.resize(2);
  empty.activeDrawingIdx = 1;
  CadBlockDefinition bare;
  bare.name = "BARE";
  empty.blockDefs.push_back(bare);
  Submit(empty, "BEDIT BARE", log);
  Submit(empty, "PARAMETERS", log);
  CHECK(Last(log) == "PARAMETERS — \"BARE\" has no parameters.");
}

TEST_CASE("PARAMETERS edits are one undo step each and mark the session unsaved", "[block][req524][issue808]") {
  std::vector<std::string> log;
  AppCommandState st = EditingDoor(log);
  const std::size_t depth = UndoDepth(st);
  REQUIRE(st.cadGpuRevision == st.blockEditCleanRevision);

  Submit(st, "PARAMETERS Width, VALUE, 6", log);
  CHECK(Last(log) == "PARAMETERS — Width value 6.");
  CHECK(Door(st).parameters[0].value == Approx(6.f));
  CHECK(UndoDepth(st) == depth + 1);
  CHECK(st.cadGpuRevision != st.blockEditCleanRevision);

  Submit(st, "PARAMETERS 1, MAX, 8", log);
  CHECK(Door(st).parameters[0].maxValue == Approx(8.f));
  Submit(st, "PARAMETERS 1, MIN, 1", log);
  CHECK(Door(st).parameters[0].minValue == Approx(1.f));
  Submit(st, "PARAMETERS width, NAME, Leaf", log);
  CHECK(Last(log) == "PARAMETERS — Width renamed Leaf.");
  CHECK(Door(st).actions[0].paramName == "Leaf");
  CHECK(UndoDepth(st) == depth + 4);

  REQUIRE(DoUndo(st, log));
  CHECK(Door(st).parameters[0].name == "Width");
  CHECK(Door(st).actions[0].paramName == "Width");
  REQUIRE(DoUndo(st, log));
  CHECK(Door(st).parameters[0].minValue == Approx(0.f));

  Submit(st, "PARAMETERS Width, DELETE", log);
  CHECK(Last(log) == "PARAMETERS — deleted Width and 1 action(s) bound to it.");
  CHECK(Door(st).parameters.size() == 1);
  CHECK(Door(st).actions.empty());
  REQUIRE(DoUndo(st, log));
  CHECK(Door(st).parameters.size() == 2);
  CHECK(Door(st).actions.size() == 1);

  // A changed block prompts on close.
  Submit(st, "BCLOSE", log);
  CHECK(LogHas(log, "unsaved changes"));
  CHECK(st.blockEditActive);
}

TEST_CASE("a refused PARAMETERS edit changes nothing and adds no undo step", "[block][req524][issue808]") {
  std::vector<std::string> log;
  AppCommandState st = EditingDoor(log);
  const std::size_t depth = UndoDepth(st);
  const CadBlockDefinition before = Door(st);
  const struct { const char* command; const char* reply; } cases[] = {
      {"PARAMETERS Width, VALUE, 11", "the value must be from 0 to 10"},
      {"PARAMETERS Width, VALUE, wide", "the value must be a number"},
      {"PARAMETERS Width, MIN, 3", "the minimum cannot be above the current value (2)"},
      {"PARAMETERS Width, MAX, 1", "the maximum cannot be below the current value (2)"},
      {"PARAMETERS Width, NAME, Look", "already used by another parameter"},
      {"PARAMETERS Width, NAME, ", "a parameter needs a name"},
      {"PARAMETERS Height, VALUE, 1", "no parameter named \"Height\""},
      {"PARAMETERS 9, DELETE", "there is no parameter 9 (the block has 2)"},
      {"PARAMETERS Width, COLOUR, 1", "PARAMETERS — usage:"},
      {"PARAMETERS Width", "PARAMETERS — usage:"},
      {"PARAMETERS Width, DELETE, now", "PARAMETERS — usage:"},
      {"PARAMETERS Width, VALUE", "PARAMETERS — usage:"},
  };
  for (const auto& c : cases) {
    INFO(c.command);
    Submit(st, c.command, log);
    CHECK(Last(log).find(c.reply) != std::string::npos);
    CHECK(CadBlockAuthoringEqual(Door(st), before));
    CHECK(UndoDepth(st) == depth);
    CHECK(st.cadGpuRevision == st.blockEditCleanRevision);
  }
  // A value set to what it already is: accepted, but nothing to undo and still clean.
  Submit(st, "PARAMETERS Width, VALUE, 2", log);
  CHECK(Last(log) == "PARAMETERS — Width value 2.");
  CHECK(UndoDepth(st) == depth);
  CHECK(st.cadGpuRevision == st.blockEditCleanRevision);
}

TEST_CASE("parameter limits and the block table survive save and load", "[block][req524][req525][issue808][io]") {
  AppCommandState st;
  st.blockDefs.push_back(DoorDef());
  CadBlockDefinition plain;
  plain.name = "PLAIN";
  plain.parameters.push_back(Param("Dist", CadBlockParamKind::Linear, 1.f));
  plain.parameters[0].value = -45.f;
  st.blockDefs.push_back(plain);
  CadBlockDefinition& door = st.blockDefs[0];
  door.parameters[0].minValue = 1.5f;
  REQUIRE(CadBlockTableAddColumn(door, 0).empty());
  REQUIRE(CadBlockTableAddColumn(door, 1).empty());
  REQUIRE(CadBlockTableAddRow(door, {"3", "Closed"}).empty());
  REQUIRE(CadBlockTableAddRow(door, {"4.25", "Open"}).empty());

  const std::string json = SerializeGoSurveyJson(st);
  // Defaults write nothing new: PLAIN has default limits and no table.
  const std::size_t plainAt = json.find("\"PLAIN\"");
  REQUIRE(plainAt != std::string::npos);
  CHECK(json.find("propertyTable", plainAt) == std::string::npos);
  CHECK(json.find("minValue", plainAt) == std::string::npos);
  CHECK(json.find("maxValue", plainAt) == std::string::npos);

  AppCommandState in;
  std::vector<std::string> log;
  REQUIRE(LoadGoSurveyFromJsonUtf8(in, json, log));
  const CadBlockDefinition& back = Door(in);
  CHECK(back.parameters[0].minValue == Approx(1.5f));
  CHECK(back.parameters[0].maxValue == Approx(10.f));
  CHECK(back.parameters[1].maxValue == Approx(CadBlockParameter{}.maxValue));
  CHECK(back.propertyTable.columns == std::vector<std::string>{"Width", "Look"});
  REQUIRE(back.propertyTable.rows.size() == 2);
  CHECK(back.propertyTable.rows[1] == std::vector<std::string>{"4.25", "Open"});
  const int pi = CadBlockFindDef(in.blockDefs, "PLAIN");
  REQUIRE(pi >= 0);
  CHECK(in.blockDefs[static_cast<std::size_t>(pi)].propertyTable.columns.empty());
  CHECK(in.blockDefs[static_cast<std::size_t>(pi)].parameters[0].value == Approx(-45.f));
}

TEST_CASE("a saved table still loads when parameter names repeat", "[block][req525][issue808][io]") {
  AppCommandState state;
  CadBlockDefinition definition;
  definition.name = "DUPLICATE";
  definition.parameters.push_back(Param("Width", CadBlockParamKind::Linear, 5.f));
  REQUIRE(CadBlockTableAddColumn(definition, 0).empty());
  REQUIRE(CadBlockTableAddRow(definition, {"4"}).empty());
  state.blockDefs.push_back(definition);
  nlohmann::json root = nlohmann::json::parse(SerializeGoSurveyJson(state));
  const std::function<void(nlohmann::json&)> patch = [&](nlohmann::json& node) {
    if (node.is_object() && node.contains("name") && node["name"] == "DUPLICATE" && node.contains("parameters"))
      node["parameters"].push_back(node["parameters"][0]);
    if (node.is_structured())
      for (auto& child : node)
        patch(child);
  };
  patch(root);
  AppCommandState reopened;
  std::vector<std::string> log;
  REQUIRE(LoadGoSurveyFromJsonUtf8(reopened, root.dump(), log));
  const int index = CadBlockFindDef(reopened.blockDefs, "DUPLICATE");
  REQUIRE(index >= 0);
  CHECK(reopened.blockDefs[static_cast<size_t>(index)].propertyTable.columns == std::vector<std::string>{"Width"});
  REQUIRE(reopened.blockDefs[static_cast<size_t>(index)].propertyTable.rows.size() == 1);
  CHECK(reopened.blockDefs[static_cast<size_t>(index)].propertyTable.rows[0] == std::vector<std::string>{"4"});
}

TEST_CASE("a malformed saved block table loads without the bad rows", "[block][req525][issue808][io]") {
  AppCommandState st;
  st.blockDefs.push_back(DoorDef());
  REQUIRE(CadBlockTableAddColumn(st.blockDefs[0], 0).empty());
  REQUIRE(CadBlockTableAddRow(st.blockDefs[0], {"3"}).empty());
  nlohmann::json root = nlohmann::json::parse(SerializeGoSurveyJson(st));
  // A row of the wrong length, a row holding a number, something that is not a row, and a good row.
  bool patched = false;
  const std::function<void(nlohmann::json&)> patch = [&](nlohmann::json& node) {
    if (node.is_object() && node.contains("propertyTable")) {
      node["propertyTable"]["rows"] = nlohmann::json::parse(R"([["3","4"],[7],"x",["5"]])");
      patched = true;
    }
    if (node.is_structured()) {
      for (auto& child : node)
        patch(child);
    }
  };
  patch(root);
  REQUIRE(patched);
  AppCommandState in;
  std::vector<std::string> log;
  REQUIRE(LoadGoSurveyFromJsonUtf8(in, root.dump(), log));
  const CadBlockPropertyTable& t = Door(in).propertyTable;
  REQUIRE(t.rows.size() == 1);
  CHECK(t.rows[0] == std::vector<std::string>{"5"});
  CHECK(LogHas(log, "row 1 has the wrong number of cells"));
  CHECK(LogHas(log, "contains a non-text cell"));
  CHECK(LogHas(log, "row 3 has the wrong number of cells"));
}

TEST_CASE("malformed saved table columns and parameter limits are reported and repaired",
          "[block][req524][req525][issue808][io]") {
  AppCommandState st;
  st.blockDefs.push_back(DoorDef());
  REQUIRE(CadBlockTableAddColumn(st.blockDefs[0], 0).empty());
  nlohmann::json root = nlohmann::json::parse(SerializeGoSurveyJson(st));
  const std::function<void(nlohmann::json&)> patch = [&](nlohmann::json& node) {
    if (node.is_object() && node.contains("name") && node["name"].is_string() &&
        node["name"] == "DOOR" && node.contains("propertyTable")) {
      node["propertyTable"]["columns"] = {"Width", "Width"};
      node["parameters"][0]["minValue"] = 12.f;
      node["parameters"][0]["maxValue"] = 10.f;
    }
    if (node.is_structured())
      for (auto& child : node)
        patch(child);
  };
  patch(root);
  AppCommandState in;
  std::vector<std::string> log;
  REQUIRE(LoadGoSurveyFromJsonUtf8(in, root.dump(), log));
  const CadBlockDefinition& loaded = Door(in);
  CHECK(loaded.propertyTable.columns.empty());
  const CadBlockParameter defaults;
  CHECK(loaded.parameters[0].value == Approx(defaults.value));
  CHECK(loaded.parameters[0].minValue == Approx(defaults.minValue));
  CHECK(loaded.parameters[0].maxValue == Approx(defaults.maxValue));
  CHECK(LogHas(log, "duplicate column \"Width\""));
  CHECK(LogHas(log, "has invalid limits or value"));
}

// --- BTABLE / BSETROW (REQ-525) -------------------------------------------------------------------

TEST_CASE("BTABLE builds a table, one undo step per change", "[block][req525][issue808]") {
  AppCommandState plain;
  plain.documents.resize(2);
  plain.activeDrawingIdx = 1;
  std::vector<std::string> log;
  for (const char* command : {"BTABLE", "BTABLE ADDROW", "BTABLE CLEAR"}) {
    Submit(plain, command, log);
    CHECK(Last(log) == "BTABLE — open a block with BEDIT first.");
  }

  AppCommandState st = EditingDoor(log);
  const std::size_t depth = UndoDepth(st);
  Submit(st, "BTABLE", log);
  CHECK(Last(log) == "BTABLE — \"DOOR\" has no block table.");
  CHECK(st.showBlockTableWindow);
  Submit(st, "BTABLE ADDCOLUMN, Width", log);
  CHECK(Last(log) == "BTABLE — added column Width.");
  Submit(st, "BTABLE ADDCOLUMN, 2", log);
  CHECK(Last(log) == "BTABLE — added column Look.");
  Submit(st, "BTABLE ADDROW, 3, closed", log);
  CHECK(Last(log) == "BTABLE — added row 1.");
  Submit(st, "BTABLE ADDROW", log);
  CHECK(Last(log) == "BTABLE — added row 2.");
  Submit(st, "BTABLE SET, 2, Width, 9", log);
  CHECK(Last(log) == "BTABLE — row 2 Width set.");
  CHECK(UndoDepth(st) == depth + 5);
  CHECK(st.cadGpuRevision != st.blockEditCleanRevision);
  log.clear();
  Submit(st, "BTABLE", log);
  CHECK(LogHas(log, "BTABLE — columns: Width, Look; 2 row(s)."));
  CHECK(LogHas(log, "BTABLE — 1. 3, Closed"));
  CHECK(LogHas(log, "BTABLE — 2. 9, Open"));

  REQUIRE(DoUndo(st, log));
  CHECK(Door(st).propertyTable.rows[1][0] == "2");
  Submit(st, "BTABLE REMOVEROW, 1", log);
  CHECK(Door(st).propertyTable.rows.size() == 1);
  Submit(st, "BTABLE REMOVECOLUMN, 1", log);
  CHECK(Last(log) == "BTABLE — removed column Width.");
  Submit(st, "BTABLE CLEAR", log);
  CHECK(Last(log) == "BTABLE — table removed.");
  CHECK(Door(st).propertyTable.columns.empty());
  REQUIRE(DoUndo(st, log));
  CHECK(Door(st).propertyTable.columns == std::vector<std::string>{"Look"});
}

TEST_CASE("a refused BTABLE change alters nothing and adds no undo step", "[block][req525][issue808]") {
  std::vector<std::string> log;
  AppCommandState st = EditingDoor(log);
  Submit(st, "BTABLE ADDCOLUMN, Width", log);
  Submit(st, "BTABLE ADDROW, 3", log);
  const std::size_t depth = UndoDepth(st);
  const CadBlockDefinition before = Door(st);
  const struct { const char* command; const char* reply; } cases[] = {
      {"BTABLE ADDCOLUMN, Width", "already in the table"},
      {"BTABLE ADDCOLUMN, Height", "no parameter named"},
      {"BTABLE ADDCOLUMN", "BTABLE — usage:"},
      {"BTABLE REMOVECOLUMN, Look", "is not in the table"},
      {"BTABLE REMOVECOLUMN, 7", "there is no parameter 7"},
      {"BTABLE ADDROW, 3", "row 1 already holds those values"},
      {"BTABLE ADDROW, 3, 4", "1 column(s) but 2 value(s)"},
      {"BTABLE ADDROW, 12", "must be from 0 to 10"},
      {"BTABLE SET, 1, Width, x", "must be a number"},
      {"BTABLE SET, 2, Width, 1", "there is no row 2 (the table has 1)"},
      {"BTABLE SET, one, Width, 1", "the row must be a whole number"},
      {"BTABLE SET, 1, Width", "BTABLE — usage:"},
      {"BTABLE REMOVEROW, 0", "the row must be a whole number"},
      {"BTABLE REMOVEROW, 5", "there is no row 5"},
      {"BTABLE SHUFFLE", "BTABLE — usage:"},
  };
  for (const auto& c : cases) {
    INFO(c.command);
    Submit(st, c.command, log);
    CHECK(Last(log).find(c.reply) != std::string::npos);
    CHECK(CadBlockAuthoringEqual(Door(st), before));
    CHECK(UndoDepth(st) == depth);
  }
}

TEST_CASE("BSETROW sets selected references from a table row in one undo step", "[block][req525][issue808]") {
  AppCommandState st;
  st.documents.resize(2);
  st.activeDrawingIdx = 1;
  CadBlockDefinition door = DoorDef();
  REQUIRE(CadBlockTableAddColumn(door, 0).empty());
  REQUIRE(CadBlockTableAddColumn(door, 1).empty());
  REQUIRE(CadBlockTableAddRow(door, {"7", "Closed"}).empty());
  st.blockDefs.push_back(door);
  CadBlockDefinition plain;
  plain.name = "PLAIN";
  plain.content.lines = {0., 0., 0., 1., 0., 0.};
  plain.content.lineAttrs.push_back(EntityAttributes{});
  plain.content.lineVis.push_back("");
  st.blockDefs.push_back(plain);
  std::vector<std::string> log;
  Submit(st, "INSERT DOOR, 0, 0", log);
  Submit(st, "INSERT DOOR, 5, 0", log);
  Submit(st, "INSERT PLAIN, 9, 0", log);
  REQUIRE(st.cadBlockRefs.size() == 3);

  st.selection.clear();
  Submit(st, "BSETROW 1", log);
  CHECK(Last(log) == "BSETROW — select a block reference first.");
  Submit(st, "BSETROW", log);
  CHECK(Last(log).find("BSETROW — usage:") != std::string::npos);

  for (int i = 0; i < 3; ++i)
    st.selection.push_back({SelectedEntity::Type::BlockRef, i});
  const std::size_t depth = UndoDepth(st);
  Submit(st, "BSETROW 2", log);
  CHECK(Last(log) == "BSETROW — none of the 3 selected reference(s) has a block table with row 2.");
  CHECK(UndoDepth(st) == depth);
  Submit(st, "BSETROW 0", log);
  CHECK(Last(log).find("BSETROW — usage:") != std::string::npos);

  Submit(st, "BSETROW 1", log);
  CHECK(Last(log) == "BSETROW — row 1 set on 2 reference(s), 1 skipped (no such row).");
  CHECK(UndoDepth(st) == depth + 1);
  for (int i = 0; i < 2; ++i) {
    CHECK(CadBlockParamValue(st.cadBlockRefs[static_cast<std::size_t>(i)], Door(st), "Width") == Approx(7.f));
    CHECK(st.cadBlockRefs[static_cast<std::size_t>(i)].visState == "Closed");
  }
  CHECK(st.cadBlockRefs[2].paramState.empty());
  REQUIRE(DoUndo(st, log));
  CHECK(st.cadBlockRefs[0].paramState.empty());
  CHECK(st.cadBlockRefs[0].visState.empty());
}

// --- BTESTBLOCK (REQ-523) -------------------------------------------------------------------------

TEST_CASE("BTESTBLOCK refuses outside the block editor and on an empty block", "[block][req523][issue808]") {
  AppCommandState st;
  st.documents.resize(2);
  st.activeDrawingIdx = 1;
  std::vector<std::string> log;
  Submit(st, "MKLINE 0,0, 4,0", log);
  Submit(st, "BTESTBLOCK", log);
  CHECK(Last(log) == "BTESTBLOCK — open a block with BEDIT first.");
  CHECK_FALSE(st.blockTestActive);
  CHECK(st.userLinesFlat.size() == 6);

  CadBlockDefinition bare;
  bare.name = "BARE";
  st.blockDefs.push_back(bare);
  Submit(st, "BEDIT BARE", log);
  REQUIRE(st.blockEditActive);
  st.importedDxfAttrDefs.push_back(CadBlockAttrDef{});
  Submit(st, "BTESTBLOCK", log);
  CHECK(Last(log) == "BTESTBLOCK — the block has no objects to test.");
  CHECK_FALSE(st.blockTestActive);
  CHECK(st.blockEditActive);
  CHECK(st.cadBlockRefs.empty());
}

TEST_CASE("BTESTBLOCK shows the unsaved block as one reference and BCLOSE restores the editor",
          "[block][req523][issue808]") {
  std::vector<std::string> log;
  AppCommandState st = EditingDoor(log);
  Submit(st, "MKLINE 0,1, 2,1", log);  // an unsaved edit
  REQUIRE(st.userLinesFlat.size() == 12);
  const std::vector<double> editorLines = st.userLinesFlat;
  const CadBlockDefinition before = Door(st);
  const std::size_t depth = UndoDepth(st);
  st.viewportZoom = 3.25f;
  st.blockAuthoringPaletteOpen = true;
  st.showBlockParametersWindow = true;

  Submit(st, "BTESTBLOCK", log);
  REQUIRE(st.blockTestActive);
  CHECK(LogHas(log, "BTESTBLOCK — testing \"DOOR\""));
  CHECK(st.userLinesFlat.empty());  // no loose lines
  REQUIRE(st.cadBlockRefs.size() == 1);
  CHECK(st.cadBlockRefs[0].defName == "DOOR");
  CHECK(st.cadBlockRefs[0].xf.x == Approx(0.f));
  CHECK(st.cadBlockRefs[0].xf.y == Approx(0.f));
  CHECK(st.cadBlockRefs[0].xf.sx == Approx(1.f));
  CHECK(st.cadBlockRefs[0].xf.rotZ == Approx(0.f));
  CHECK(st.cadBlockRefs[0].paramState.empty());
  CHECK(Door(st).content.lines.size() == 12);  // the reference draws the unsaved line too
  CHECK(st.selection.empty());
  CHECK_FALSE(st.blockAuthoringPaletteOpen);    // item 7: authoring windows are hidden
  CHECK_FALSE(st.showBlockParametersWindow);

  // Item 8: nothing to undo at the start of a test, and the view is untouched.
  CHECK_FALSE(DoUndo(st, log));
  CHECK(Last(log) == "Nothing to undo.");
  CHECK(st.cadBlockRefs.size() == 1);

  // Item 4: the reference behaves as a placed one.
  st.selection.push_back({SelectedEntity::Type::BlockRef, 0});
  Submit(st, "BSETPARAM Width, 5", log);
  CHECK(CadBlockParamValue(st.cadBlockRefs[0], Door(st), "Width") == Approx(5.f));
  Submit(st, "BSETVIS Closed", log);
  CHECK(st.cadBlockRefs[0].visState == "Closed");
  Submit(st, "MKLINE 7,7, 8,8", log);  // anything drawn in a test is thrown away

  // Item 6: a second BTESTBLOCK starts over with default values.
  Submit(st, "BTESTBLOCK", log);
  REQUIRE(st.blockTestActive);
  REQUIRE(st.cadBlockRefs.size() == 1);
  CHECK(st.cadBlockRefs[0].paramState.empty());
  CHECK(st.cadBlockRefs[0].visState.empty());
  CHECK(st.userLinesFlat.empty());
  CHECK_FALSE(DoUndo(st, log));
  CHECK(Last(log) == "Nothing to undo.");
  CHECK_FALSE(DoRedo(st, log));
  CHECK(Last(log) == "Nothing to redo.");

  Submit(st, "BCLOSE SAVE", log);  // SAVE is ignored: this closes the test, not the editor
  CHECK(Last(log) == "BCLOSE — test block closed; back in the block editor for \"DOOR\".");
  CHECK_FALSE(st.blockTestActive);
  CHECK(st.blockEditActive);
  CHECK(st.blockEditorName == "DOOR");
  CHECK(st.userLinesFlat == editorLines);
  CHECK(st.cadBlockRefs.empty());
  CHECK(Door(st).content.lines.size() == before.content.lines.size());  // still unsaved in the definition
  CHECK(CadBlockAuthoringEqual(Door(st), before));
  CHECK(UndoDepth(st) == depth);
  CHECK(st.viewportZoom == Approx(3.25f));
  CHECK(st.blockAuthoringPaletteOpen);
  CHECK(st.showBlockParametersWindow);

  // The session had unsaved changes before the test, so it still prompts.
  Submit(st, "BCLOSE", log);
  CHECK(LogHas(log, "unsaved changes"));
  CHECK(st.blockEditActive);
  // And the editor's own undo history still works: undo the unsaved line.
  REQUIRE(DoUndo(st, log));
  CHECK(st.userLinesFlat.size() == 6);
}

TEST_CASE("a test of a clean session leaves it clean, and redo history survives", "[block][req523][issue808]") {
  std::vector<std::string> log;
  AppCommandState st = EditingDoor(log);
  Submit(st, "MKLINE 0,1, 2,1", log);
  Submit(st, "BSAVE", log);
  Submit(st, "MKLINE 0,2, 2,2", log);
  REQUIRE(DoUndo(st, log));  // back to the saved state, with one redo frame
  Submit(st, "BSAVE", log);
  REQUIRE(st.cadGpuRevision == st.blockEditCleanRevision);
  REQUIRE(CanRedo(st));

  Submit(st, "BTESTBLOCK", log);
  REQUIRE(st.blockTestActive);
  CHECK_FALSE(CanRedo(st));  // a test cannot redo into an editor frame
  Submit(st, "BCLOSE", log);
  REQUIRE_FALSE(st.blockTestActive);
  CHECK(st.cadGpuRevision == st.blockEditCleanRevision);
  CHECK(CanRedo(st));

  log.clear();
  Submit(st, "BCLOSE", log);  // clean: closes with no prompt
  CHECK_FALSE(LogHas(log, "unsaved changes"));
  CHECK_FALSE(st.blockEditActive);
}

TEST_CASE("block authoring commands are refused during a test", "[block][req523][issue808]") {
  std::vector<std::string> log;
  AppCommandState st = EditingDoor(log);
  Submit(st, "BTESTBLOCK", log);
  REQUIRE(st.blockTestActive);
  const CadBlockDefinition before = Door(st);
  const std::size_t depth = UndoDepth(st);
  const std::size_t defs = st.blockDefs.size();
  const struct { const char* command; const char* verb; } cases[] = {
      {"BEDIT DOOR", "BEDIT"},
      {"BSAVE", "BSAVE"},
      {"BSAVEAS DOOR2", "BSAVEAS"},
      {"BPARAM Extra, linear", "BPARAM"},
      {"BACTION stretch, Width", "BACTION"},
      {"BVISIBILITY Ajar", "BVISIBILITY"},
      {"ATTDEF TAG, Prompt, 1", "ATTDEF"},
      {"BEDITADD LINE, 0, 0, 1, 1", "BEDITADD"},
      {"BCONNECT", "BCONNECT"},
      {"BCONNECTEDIT", "BCONNECTEDIT"},
      {"BCONNECTMODE", "BCONNECTMODE"},
      {"BLOCKFITTING", "BLOCKFITTING"},
      {"PARAMETERS", "PARAMETERS"},
      {"PARAMETERS Width, VALUE, 4", "PARAMETERS"},
      {"BTABLE ADDCOLUMN, Width", "BTABLE"},
  };
  for (const auto& c : cases) {
    INFO(c.command);
    Submit(st, c.command, log);
    CHECK(Last(log) == std::string(c.verb) + " — close the test block first (BCLOSE).");
    CHECK(st.blockTestActive);
    CHECK(CadBlockAuthoringEqual(Door(st), before));
    CHECK(Door(st).visibilityStates == before.visibilityStates);
    CHECK(Door(st).attrDefs.size() == before.attrDefs.size());
    CHECK(st.blockDefs.size() == defs);
    CHECK(UndoDepth(st) == depth);
    CHECK(st.userLinesFlat.empty());
  }
  // The shared edit path refuses too, for a dialog that calls it directly.
  CHECK(CadBlocksApplyDefinitionEdit(st, "x", [](CadBlockDefinition& d) { return CadBlockParamDelete(d, 0, nullptr); }) ==
        "close the test block first (BCLOSE).");
}
