// REQ-355 (D-2026-09-28-k, GitHub issue #564 section 8) — the Modeling ribbon tab. The tab draws
// its buttons from modelingribbon::kSections and submits each button's command text through the
// command line, so these tests run that same text through the same entry point.
#include "CadCommands.hpp"
#include "ModelingRibbon.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string>
#include <vector>

namespace {

void Submit(AppCommandState& st, const std::string& line, std::vector<std::string>& log) {
  std::vector<char> buf(line.begin(), line.end());
  buf.push_back('\0');
  ProcessCommandLineSubmit(buf.data(), static_cast<int>(buf.size()), st, log);
}

bool LogSaysUnknown(const std::vector<std::string>& log) {
  for (const std::string& l : log)
    if (l.find("Unknown command") != std::string::npos)
      return true;
  return false;
}

} // namespace

TEST_CASE("The Modeling tab holds the six sections and the commands section 8 lists (REQ-355)",
          "[req355][ribbon]") {
  std::vector<std::string> titles;
  std::set<std::string> commands;
  std::set<std::string> ids;
  for (const modelingribbon::Section& sec : modelingribbon::kSections) {
    titles.push_back(sec.title);
    REQUIRE(sec.count > 0);
    for (int k = 0; k < sec.count; ++k) {
      commands.insert(sec.buttons[k].command);
      CHECK(ids.insert(sec.buttons[k].id).second);  // ids are unique across the tab
    }
  }
  CHECK(titles == std::vector<std::string>{"Primitives", "Create", "Booleans", "Edit", "3D Modify", "Piping"});
  for (const char* c : {"BOX", "WEDGE", "CONE", "CYLINDER", "SPHERE", "TORUS", "PYRAMID", "POLYSOLID",
                        "EXTRUDE", "REVOLVE", "SWEEP", "LOFT", "UNION", "SUBTRACT", "INTERSECT", "SLICE",
                        "FILLET", "CHAMFER", "SECTION", "SECTIONPLANE", "3DMOVE", "3DROTATE", "3DSCALE", "MOVE",
                        "COPY", "ARRAY", "PIPEFIT", "PIPESPLIT", "PIPEJOIN"})
    CHECK(commands.count(c) == 1);
  CHECK(std::string(modelingribbon::kPipeRunCommand) == "PIPERUN");
}

TEST_CASE("Every Modeling button's command is one the command line accepts (REQ-355)", "[req355][ribbon]") {
  std::vector<std::string> lines;
  for (const modelingribbon::Section& sec : modelingribbon::kSections)
    for (int k = 0; k < sec.count; ++k)
      lines.push_back(sec.buttons[k].command);
  for (const char* part : modelingribbon::kPipeFitPartTypes)
    lines.push_back(std::string("PIPEFIT ") + part);
  lines.push_back(modelingribbon::kPipeRunCommand);
  for (const std::string& line : lines) {
    INFO(line);
    AppCommandState st;
    std::vector<std::string> log;
    Submit(st, line, log);
    CHECK_FALSE(log.empty());  // it said something: a prompt, or its own refusal
    CHECK_FALSE(LogSaysUnknown(log));
  }
}

TEST_CASE("PIPEFIT's menu offers only part types the command accepts (REQ-355)", "[req355][ribbon]") {
  for (const char* part : modelingribbon::kPipeFitPartTypes) {
    INFO(part);
    AppCommandState st;
    std::vector<std::string> log;
    Submit(st, std::string("PIPEFIT ") + part, log);
    REQUIRE_FALSE(log.empty());
    // With no run selected the command stops at the selection check, which comes first; a bad part
    // type is its own message, checked through the command's own parser here.
    CHECK(log.back().find("unknown part type") == std::string::npos);
    CHECK(ParseCadPipePartType(part) != CadPipePartType::None);
  }
}

TEST_CASE("A ribbon command cancels the running one, then starts as typed (REQ-355)", "[req355][ribbon]") {
  AppCommandState typed;
  std::vector<std::string> log;
  Submit(typed, "BOX", log);
  REQUIRE(typed.active == AppCommandState::Kind::Solid);

  // What RunRibbonTypedCommand does: cancel, then the same typed text.
  AppCommandState ribbon;
  Submit(ribbon, "LINE", log);
  REQUIRE(ribbon.active == AppCommandState::Kind::Line);
  CancelActiveCommand(ribbon, log);
  Submit(ribbon, "BOX", log);
  CHECK(ribbon.active == typed.active);
}

TEST_CASE("The ribbon's PIPERUN asks neither question and starts at the dropdown's size (REQ-355)",
          "[req355][piperun]") {
  AppCommandState st;
  std::vector<std::string> log;
  CHECK(st.pipeRunNominalSize == "4in");  // D-2026-09-28-k: a new session has a size to show

  StartPipeRunAtCurrentSize(st, log);
  CHECK(st.active == AppCommandState::Kind::PipeRun);
  CHECK(st.pipeRunPhase == AppCommandState::PipeRunPhase::WaitFirstPoint);
  CHECK(st.pipeRunWallThicknessIn == Catch::Approx(0.237));  // schedule 40 at 4in
  REQUIRE_FALSE(log.empty());
  CHECK(log.back().find("start point") != std::string::npos);

  // The dropdown writes the remembered size; the next ribbon run takes it and its standard wall.
  CancelActiveCommand(st, log);
  st.pipeRunNominalSize = "22in";
  StartPipeRunAtCurrentSize(st, log);
  CHECK(st.pipeRunPhase == AppCommandState::PipeRunPhase::WaitFirstPoint);
  CHECK(st.pipeRunWallThicknessIn == Catch::Approx(0.375));  // STD: 22in has no schedule 40
}

TEST_CASE("Choosing a dropdown size is typing it alone: the pressure class is cleared (REQ-355)",
          "[req355][piperun]") {
  AppCommandState st;
  std::vector<std::string> log;
  Submit(st, "PIPERUN", log);
  REQUIRE(HandlePipeRunTextInput("2in CS300", st, log));
  REQUIRE(st.pipeRunPressureClassTag == "CS300");
  CancelActiveCommand(st, log);

  ChoosePipeRunNominalSize(st, "6in");
  CHECK(st.pipeRunNominalSize == "6in");
  CHECK(st.pipeRunPressureClassTag.empty());  // as typing "6in" alone would leave it

  AppCommandState typed;
  Submit(typed, "PIPERUN", log);
  REQUIRE(HandlePipeRunTextInput("2in CS300", typed, log));
  CancelActiveCommand(typed, log);
  Submit(typed, "PIPERUN", log);
  REQUIRE(HandlePipeRunTextInput("6in", typed, log));
  CHECK(typed.pipeRunNominalSize == st.pipeRunNominalSize);
  CHECK(typed.pipeRunPressureClassTag == st.pipeRunPressureClassTag);
}

TEST_CASE("The ribbon's PIPERUN falls back to the prompts for a size with no standard wall (REQ-355)",
          "[req355][piperun]") {
  AppCommandState st;
  std::vector<std::string> log;
  st.pipeRunNominalSize = "7in";  // not in the NPS table (a hand-set value; the dropdown cannot offer it)
  StartPipeRunAtCurrentSize(st, log);
  CHECK(st.active == AppCommandState::Kind::PipeRun);
  CHECK(st.pipeRunPhase == AppCommandState::PipeRunPhase::WaitNominalSize);
}

TEST_CASE("Typed PIPERUN still offers the current size, and a typed size is the dropdown's (REQ-355)",
          "[req355][piperun]") {
  AppCommandState st;
  std::vector<std::string> log;
  Submit(st, "PIPERUN", log);
  CHECK(st.pipeRunPhase == AppCommandState::PipeRunPhase::WaitNominalSize);
  REQUIRE_FALSE(log.empty());
  CHECK(log.back().find("[4in]") != std::string::npos);

  // A size typed at the prompt is the one value the dropdown shows.
  REQUIRE(HandlePipeRunTextInput("6in", st, log));
  CHECK(st.pipeRunNominalSize == "6in");
  CancelActiveCommand(st, log);

  // ...and the next ribbon run takes it without asking.
  StartPipeRunAtCurrentSize(st, log);
  CHECK(st.pipeRunPhase == AppCommandState::PipeRunPhase::WaitFirstPoint);
  CHECK(st.pipeRunWallThicknessIn == Catch::Approx(0.280));  // schedule 40 at 6in
}
