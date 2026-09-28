// REQ-353 (D-2026-09-28-i, GitHub issue #564 section 7) — pipe colour by nominal size.
//
// The NPS table carries all 21 listed sizes with their OD, schedule-40 wall and default colour; a new run
// is stamped with its size's colour; every fitting spliced into a run takes that run's layer and
// colour (a reducer included, issue #564 Q4); a split keeps the line's colour; and a user's
// override wins and survives a save and reload.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "CadBlocks.hpp"
#include "CadCommands.hpp"
#include "GsIo.hpp"
#include "util/cadpiperun.hpp"

namespace {

constexpr const char* kBlue4in = "#2D6CDF";
constexpr const char* kGreen2in = "#2ECC40";

/// Routes a straight two-point run at \p size (schedule-40 wall) through the real PIPERUN command.
void RouteRun(AppCommandState& st, const std::string& size, float y, std::vector<std::string>& log) {
  StartPipeRunCommand(st, log);
  REQUIRE(HandlePipeRunTextInput(size, st, log));
  REQUIRE(HandlePipeRunTextInput("", st, log));
  SubmitPipeRunViewportPick(st, 0.f, y, log);
  SubmitPipeRunViewportPick(st, 20.f, y, log);
  REQUIRE(HandlePipeRunTextInput("end", st, log));
}

/// A two-port inline test double (both ports at the local origin, Inlet -X / Outlet +X) — the same
/// simplification CadPipeRunCommandTests' `MakeValveDef` uses.
CadBlockDefinition MakeInlineDef(const std::string& name, CadPipePartType type) {
  CadBlockDefinition def;
  def.name = name;
  def.partType = type;
  def.nominalSize = "4in";
  CadBlockConnection in;
  in.name = "IN"; in.nx = -1.f; in.ny = 0.f; in.nz = 0.f;
  in.role = CadBlockConnectionRole::Inlet;
  CadBlockConnection out;
  out.name = "OUT"; out.nx = 1.f; out.ny = 0.f; out.nz = 0.f;
  out.role = CadBlockConnectionRole::Outlet;
  def.connections = {in, out};
  return def;
}

void SelectRun(AppCommandState& st, int index) {
  SelectedEntity e{};
  e.type = SelectedEntity::Type::PipeRun;
  e.index = index;
  st.selection = {e};
}

bool DisplaysHex(AppCommandState& st, const char* hex) {
  unsigned r = 0, g = 0, b = 0;
  REQUIRE(std::sscanf(hex, "#%02x%02x%02x", &r, &g, &b) == 3);
  RefreshSolidDisplayGeometry(st);
  for (const auto& batch : st.solidDisplayGeometry.solids)
    if (std::fabs(batch.rgba[0] - r / 255.f) < 1e-3f && std::fabs(batch.rgba[1] - g / 255.f) < 1e-3f &&
        std::fabs(batch.rgba[2] - b / 255.f) < 1e-3f)
      return true;
  return false;
}

}  // namespace

TEST_CASE("The NPS table carries all 21 sizes with OD, wall and colour (REQ-353)", "[req353][piperun]") {
  // The issue says "20 sizes", but its own lists name 21 (13 existing + 8 added); the list governs.
  CHECK(std::size(kCadPipeNpsTable) == 21);
  struct Want {
    const char* label;
    double odIn;
    double wallIn;
  };
  // The eight sizes issue #564 section 7 adds (ASME B36.10M; 22in is STD, D-2026-09-28-i).
  const Want added[] = {{"3.5in", 4.000, 0.226}, {"5in", 5.563, 0.258},   {"14in", 14.0, 0.438},
                        {"16in", 16.0, 0.500},   {"18in", 18.0, 0.562},   {"20in", 20.0, 0.594},
                        {"22in", 22.0, 0.375},   {"24in", 24.0, 0.688}};
  for (const Want& w : added) {
    INFO(w.label);
    double odFeet = 0.0, wallIn = 0.0;
    REQUIRE(CadPipeNominalOdFeet(w.label, &odFeet));
    CHECK(odFeet == Catch::Approx(w.odIn / 12.0));
    REQUIRE(CadPipeStandardWallThicknessInches(w.label, &wallIn));
    CHECK(wallIn == Catch::Approx(w.wallIn));
  }

  // One colour per size, each a real #RRGGBB, no two alike; sizes strictly ascending.
  std::set<std::string> colours;
  double prev = 0.0;
  for (const CadPipeNpsEntry& e : kCadPipeNpsTable) {
    const std::string hex = e.colorHex;
    INFO(e.nps);
    CHECK(hex.size() == 7);
    CHECK(hex[0] == '#');
    CHECK(hex.find_first_not_of("0123456789ABCDEF", 1) == std::string::npos);
    colours.insert(hex);
    CHECK(e.nps > prev);
    prev = e.nps;
  }
  CHECK(colours.size() == std::size(kCadPipeNpsTable));

  std::string hex;
  REQUIRE(CadPipeNominalSizeColor("4in", &hex));
  CHECK(hex == kBlue4in);
  REQUIRE(CadPipeNominalSizeColor("0.5in", &hex));
  CHECK(hex == "#FF3B30");
  REQUIRE(CadPipeNominalSizeColor("24in", &hex));
  CHECK(hex == "#B0B7C3");
  CHECK_FALSE(CadPipeNominalSizeColor("7in", &hex));
}

TEST_CASE("PIPERUN accepts a new size and lists every size when refusing one (REQ-353)",
          "[req353][piperun][command]") {
  AppCommandState st;
  std::vector<std::string> log;
  StartPipeRunCommand(st, log);
  REQUIRE(HandlePipeRunTextInput("7in", st, log));
  CHECK(st.pipeRunNominalSize.empty());
  REQUIRE_FALSE(log.empty());
  CHECK(log.back().find("3.5in") != std::string::npos);
  CHECK(log.back().find("24in") != std::string::npos);

  REQUIRE(HandlePipeRunTextInput("22in", st, log));
  CHECK(st.pipeRunNominalSize == "22in");
  REQUIRE(HandlePipeRunTextInput("", st, log));
  CHECK(st.pipeRunWallThicknessIn == Catch::Approx(0.375));  // STD: 22in has no schedule 40
}

TEST_CASE("A 4in run draws blue and a 2in run beside it draws green, with no user action (REQ-353)",
          "[req353][piperun]") {
  AppCommandState st;
  std::vector<std::string> log;
  RouteRun(st, "4in", 0.f, log);
  RouteRun(st, "2in", 10.f, log);
  REQUIRE(st.cadPipeRuns.size() == 2);
  CHECK(st.cadPipeRunAttrs[0].color == kBlue4in);
  CHECK(st.cadPipeRunAttrs[1].color == kGreen2in);
  CHECK(st.cadPipeRunAttrs[0].layer == "0");  // the current layer, as for any new entity
  CHECK(DisplaysHex(st, kBlue4in));
  CHECK(DisplaysHex(st, kGreen2in));
}

TEST_CASE("An auto-inserted elbow takes its run's colour and layer (REQ-353)", "[req353][piperun][autofit]") {
  AppCommandState st;
  CadBlockDefinition elbow = MakeInlineDef("ELBOW90-4IN", CadPipePartType::Elbow90);
  elbow.connections[1].nx = 0.f;
  elbow.connections[1].ny = 1.f;  // outlet +Y: 90 degrees from the inlet
  st.blockDefs.push_back(elbow);
  st.currentLayer = "PIPE-STEEL";

  std::vector<std::string> log;
  StartPipeRunCommand(st, log);
  REQUIRE(HandlePipeRunTextInput("4in", st, log));
  REQUIRE(HandlePipeRunTextInput("", st, log));
  SubmitPipeRunViewportPick(st, 0.f, 0.f, log);
  SubmitPipeRunViewportPick(st, 10.f, 0.f, log);
  SubmitPipeRunViewportPick(st, 10.f, 10.f, log);
  REQUIRE(HandlePipeRunTextInput("end", st, log));

  REQUIRE(st.cadPipeRuns.size() == 2);
  REQUIRE(st.cadBlockRefAttrs.size() == 1);
  for (const EntityAttributes& a : st.cadPipeRunAttrs) {
    CHECK(a.color == kBlue4in);
    CHECK(a.layer == "PIPE-STEEL");
  }
  CHECK(st.cadBlockRefAttrs[0].color == kBlue4in);
  CHECK(st.cadBlockRefAttrs[0].layer == "PIPE-STEEL");
}

TEST_CASE("A spliced fitting and the far piece keep the run's own layer and override colour (REQ-353)",
          "[req353][pipefit]") {
  for (const CadPipePartType type : {CadPipePartType::Valve, CadPipePartType::Reducer}) {
    INFO(static_cast<int>(type));
    AppCommandState st;
    std::vector<std::string> log;
    st.currentLayer = "PIPE-STEEL";
    RouteRun(st, "4in", 0.f, log);
    st.cadPipeRunAttrs[0].color = "#123456";  // the user's override
    st.currentLayer = "0";                    // drawing elsewhere since
    st.blockDefs.push_back(MakeInlineDef("PART-4IN", type));

    SelectRun(st, 0);
    StartPipeFitCommand(st, std::string(CadPipePartTypeTag(type)), log);
    REQUIRE(st.active == AppCommandState::Kind::PipeFit);
    SubmitPipeFitViewportPick(st, 10.f, 0.f, log);

    REQUIRE(st.cadPipeRuns.size() == 2);
    REQUIRE(st.cadBlockRefAttrs.size() == 1);
    // A reducer reads as the run it was spliced into (issue #564 Q4), as any other fitting does.
    CHECK(st.cadBlockRefAttrs[0].color == "#123456");
    CHECK(st.cadBlockRefAttrs[0].layer == "PIPE-STEEL");
    CHECK(st.cadPipeRunAttrs[1].color == "#123456");
    CHECK(st.cadPipeRunAttrs[1].layer == "PIPE-STEEL");
  }
}

TEST_CASE("PIPESPLIT keeps the line's colour on both pieces (REQ-353)", "[req353][pipesplit]") {
  AppCommandState st;
  std::vector<std::string> log;
  RouteRun(st, "2in", 0.f, log);
  st.cadPipeRunAttrs[0].color = "ByLayer";  // an override to ByLayer is an override too
  SelectRun(st, 0);
  StartPipeSplitCommand(st, log);
  SubmitPipeSplitViewportPick(st, 10.f, 0.f, log);
  REQUIRE(st.cadPipeRuns.size() == 2);
  CHECK(st.cadPipeRunAttrs[0].color == "ByLayer");
  CHECK(st.cadPipeRunAttrs[1].color == "ByLayer");
}

TEST_CASE("Overriding a run's colour wins over the palette and survives save and reload (REQ-353)",
          "[req353][piperun][io]") {
  AppCommandState st;
  std::vector<std::string> log;
  RouteRun(st, "4in", 0.f, log);
  RouteRun(st, "2in", 10.f, log);
  SelectRun(st, 0);
  REQUIRE(CadApplyColorToSelection(st, "#FF00FF") == 1);
  CHECK(DisplaysHex(st, "#FF00FF"));
  CHECK_FALSE(DisplaysHex(st, kBlue4in));

  const std::string json = SerializeGoSurveyJson(st);
  AppCommandState back;
  REQUIRE(LoadGoSurveyFromJsonUtf8(back, json, log));
  REQUIRE(back.cadPipeRunAttrs.size() == 2);
  CHECK(back.cadPipeRunAttrs[0].color == "#FF00FF");
  CHECK(back.cadPipeRunAttrs[1].color == kGreen2in);  // the palette colour is saved as the run's own
}

TEST_CASE("A flange snapped onto a run's end takes the run's layer and colour (REQ-353)",
          "[req353][insert][connector]") {
  AppCommandState st;
  std::vector<std::string> log;
  st.currentLayer = "PIPE-STEEL";
  RouteRun(st, "2in", 0.f, log);  // ends at (20,0,0)
  st.currentLayer = "0";

  CadBlockDefinition flange;
  flange.name = "FLANGE";
  CadBlockConnection fc;
  fc.name = "P1";
  fc.nx = 0.f; fc.ny = 0.f; fc.nz = 1.f;
  flange.connections.push_back(fc);
  st.blockDefs.push_back(flange);

  auto snapFlangeAt = [&](float x, float y) {
    StartInsertBlockCommand(st, log);
    std::snprintf(st.insertBlockName, sizeof(st.insertBlockName), "FLANGE");
    st.insertBlockSpecifyConnectorSnap = true;
    st.insertBlockSpecifyPoint = false;
    st.insertBlockSpecifyRot = false;
    st.insertBlockSpecifyScale = false;
    st.insertBlockDialogOpen = false;
    st.insertBlockPhase = AppCommandState::InsertBlockPhase::WaitConnectorTarget;
    REQUIRE(SubmitInsertBlockConnectorPick(st, x, y, 0.f, log));
  };

  snapFlangeAt(19.99f, 0.f);
  REQUIRE(st.cadBlockRefAttrs.size() == 1);
  CHECK(st.cadBlockRefAttrs[0].color == kGreen2in);
  CHECK(st.cadBlockRefAttrs[0].layer == "PIPE-STEEL");
  CHECK(st.insertBlockSnappedPipeRun == -1);  // consumed by the placement

  // A bare line's end is not a run: the part is an ordinary insert on the current layer.
  st.userLinesFlat.insert(st.userLinesFlat.end(), {0.0, 50.0, 0.0, 10.0, 50.0, 0.0});
  st.userLineAttrs.push_back(EntityAttributes{});
  snapFlangeAt(9.99f, 50.f);
  REQUIRE(st.cadBlockRefAttrs.size() == 2);
  CHECK(st.cadBlockRefAttrs[1].color == "ByLayer");
  CHECK(st.cadBlockRefAttrs[1].layer == "0");
}
