#include "CadBlocks.hpp"
#include "CadCommands.hpp"
#include "CadRubberPreview.hpp"
#include "HeadlessFileDialogs.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

TEST_CASE("DXF model-space drawing imports as a named block definition", "[issue124][blockimport]") {
  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() / "gosurvey-blockimport";
  fs::create_directories(dir);
  const fs::path dxf = dir / "MATCH_E.dxf";
  {
    std::ofstream f(dxf, std::ios::binary);
    f << "0\nSECTION\n2\nHEADER\n9\n$INSUNITS\n70\n2\n0\nENDSEC\n"
         "0\nSECTION\n2\nBLOCKS\n0\nENDSEC\n"
         "0\nSECTION\n2\nENTITIES\n"
         "0\nLINE\n8\n0\n10\n0.0\n20\n0.0\n30\n0.0\n11\n1.0\n21\n0.0\n31\n0.0\n"
         "0\nATTDEF\n8\n0\n10\n0.0\n20\n0.5\n30\n0.0\n40\n0.25\n1\n0.00\n2\nEASTING\n3\nEasting\n"
         "0\nENDSEC\n0\nEOF\n";
  }
  AppCommandState st;
  std::vector<std::string> log;
  REQUIRE(ImportCadBlocksFromPath(st, dxf.u8string().c_str(), log));
  REQUIRE(CadBlockFindDef(st.blockDefs, "MATCH_E") >= 0);
  const CadBlockDefinition& d = st.blockDefs[static_cast<size_t>(CadBlockFindDef(st.blockDefs, "MATCH_E"))];
  CHECK(d.content.lines.size() >= 6);
  REQUIRE_FALSE(d.attrDefs.empty());
  CHECK(d.attrDefs[0].tag == "EASTING");
  CHECK(st.cadBlockRefs.empty());
}

TEST_CASE("WBLOCK writes a definition to .dwg and BLOCKIMPORT reads it back", "[issue284][wblock][blockimport]") {
  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() / "gosurvey-wblock";
  fs::create_directories(dir);
  const fs::path dwg = dir / "HYDRANT.dwg";
  std::error_code rmEc;
  fs::remove(dwg, rmEc);

  AppCommandState st;
  CadBlockDefinition def;
  def.name = "HYDRANT";
  def.units = CadDrawingInsUnitsName(st.drawingInsUnits);
  def.content.lines = {0.f, 0.f, 0.f, 1.f, 0.f, 0.f};
  def.content.lineAttrs.resize(1);
  def.content.lineVis.resize(1);
  CadBlockAttrDef ad;
  ad.tag = "ID";
  ad.prompt = "Point ID";
  ad.defaultValue = "A";
  def.attrDefs.push_back(ad);
  st.blockDefs.push_back(def);

  std::vector<std::string> log;
  std::istringstream wblockArgs(std::string("HYDRANT, ") + dwg.u8string());
  REQUIRE(CadBlocksTryIdleCommand(st, "wblock", wblockArgs, log));
  REQUIRE(fs::exists(dwg));

  AppCommandState dest;
  std::vector<std::string> importLog;
  REQUIRE(ImportCadBlocksFromPath(dest, dwg.u8string().c_str(), importLog));
  const int di = CadBlockFindDef(dest.blockDefs, "HYDRANT");
  REQUIRE(di >= 0);
  const CadBlockDefinition& d = dest.blockDefs[static_cast<size_t>(di)];
  CHECK(d.content.lines.size() == 6);
  REQUIRE(d.attrDefs.size() == 1);
  CHECK(d.attrDefs[0].tag == "ID");
}

TEST_CASE("WBLOCK refuses a missing block name", "[issue284][wblock]") {
  AppCommandState st;
  std::vector<std::string> log;
  std::istringstream args("MISSING, C:/does/not/matter.dwg");
  REQUIRE(CadBlocksTryIdleCommand(st, "wblock", args, log));
  bool refused = false;
  for (const std::string& line : log) {
    if (line.find("no block named") != std::string::npos)
      refused = true;
  }
  CHECK(refused);
}

TEST_CASE("bare BLOCKIMPORT opens the file picker", "[issue124][blockimport]") {
  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() / "gosurvey-blockimport";
  fs::create_directories(dir);
  const fs::path dxf = dir / "PICKED_BLK.dxf";
  {
    std::ofstream f(dxf, std::ios::binary);
    f << "0\nSECTION\n2\nHEADER\n9\n$INSUNITS\n70\n2\n0\nENDSEC\n"
         "0\nSECTION\n2\nBLOCKS\n0\nENDSEC\n"
         "0\nSECTION\n2\nENTITIES\n"
         "0\nLINE\n8\n0\n10\n0.0\n20\n0.0\n30\n0.0\n11\n1.0\n21\n0.0\n31\n0.0\n"
         "0\nENDSEC\n0\nEOF\n";
  }
  AppCommandState st;
  std::vector<std::string> log;
  headless::ClearDialogAnswers();
  headless::QueueDialogAnswer(dxf.u8string());
  std::istringstream args("");
  REQUIRE(CadBlocksTryIdleCommand(st, "blockimport", args, log));
  REQUIRE(CadBlockFindDef(st.blockDefs, "PICKED_BLK") >= 0);
  CHECK(headless::PendingDialogAnswers() == 0);
}

TEST_CASE("cancelled BLOCKIMPORT picker leaves the library unchanged", "[issue124][blockimport]") {
  AppCommandState st;
  std::vector<std::string> log;
  headless::ClearDialogAnswers();
  headless::QueueDialogCancel();
  std::istringstream args("");
  REQUIRE(CadBlocksTryIdleCommand(st, "blockimport", args, log));
  CHECK(st.blockDefs.empty());
  bool cancelled = false;
  for (const std::string& line : log) {
    if (line.find("BLOCKIMPORT — cancelled") != std::string::npos)
      cancelled = true;
  }
  CHECK(cancelled);
}

TEST_CASE("CadBlockPlaceInsert refuses a missing name", "[issue124][block][insert]") {
  AppCommandState st;
  CadBlockXform xf;
  std::vector<std::string> log;
  REQUIRE_FALSE(CadBlockPlaceInsert(st, "TREE", xf, false, log));
  CHECK(st.cadBlockRefs.empty());
}

TEST_CASE("CadBlockPlaceInsert places a BlockRef", "[issue124][block][insert]") {
  AppCommandState st;
  CadBlockDefinition def;
  def.name = "TREE";
  def.units = CadDrawingInsUnitsName(st.drawingInsUnits);  // isolate from unit rescale
  def.content.lines = {0.f, 0.f, 0.f, 1.f, 0.f, 0.f};
  st.blockDefs.push_back(def);
  CadBlockXform xf;
  xf.x = 10.f;
  xf.y = 20.f;
  std::vector<std::string> log;
  REQUIRE(CadBlockPlaceInsert(st, "TREE", xf, false, log));
  REQUIRE(st.cadBlockRefs.size() == 1);
  CHECK(st.cadBlockRefs[0].xf.x == Catch::Approx(10.f));
  CHECK(st.cadBlockRefs[0].xf.y == Catch::Approx(20.f));
}

TEST_CASE("CadBlockPlaceInsert explode writes primitives", "[issue124][block][insert]") {
  AppCommandState st;
  CadBlockDefinition def;
  def.name = "TREE";
  def.units = CadDrawingInsUnitsName(st.drawingInsUnits);  // isolate from unit rescale
  def.content.lines = {0.f, 0.f, 0.f, 1.f, 0.f, 0.f};
  st.blockDefs.push_back(def);
  CadBlockXform xf;
  std::vector<std::string> log;
  REQUIRE(CadBlockPlaceInsert(st, "TREE", xf, true, log));
  CHECK(st.cadBlockRefs.empty());
  CHECK(st.userLinesFlat.size() >= 6);
}

TEST_CASE("bare INSERT opens dialog state without placing", "[issue124][block][insert]") {
  AppCommandState st;
  CadBlockDefinition def;
  def.name = "TREE";
  st.blockDefs.push_back(def);
  std::vector<std::string> log;
  StartInsertBlockCommand(st, log);
  CHECK(st.active == AppCommandState::Kind::InsertBlock);
  CHECK(st.insertBlockDialogOpen);
  CHECK(st.insertBlockPhase == AppCommandState::InsertBlockPhase::WaitDialog);
  CHECK(st.cadBlockRefs.empty());
}

TEST_CASE("Insert dialog OK places when Specify On-screen is off", "[issue124][block][insert]") {
  AppCommandState st;
  CadBlockDefinition def;
  def.name = "TREE";
  def.units = CadDrawingInsUnitsName(st.drawingInsUnits);  // isolate from unit rescale
  def.content.lines = {0.f, 0.f, 0.f, 1.f, 0.f, 0.f};
  st.blockDefs.push_back(def);
  std::vector<std::string> log;
  StartInsertBlockCommand(st, log);
  st.insertBlockSpecifyPoint = false;
  st.insertBlockSpecifyScale = false;
  st.insertBlockSpecifyRot = false;
  st.insertBlockX = 5.f;
  st.insertBlockY = 6.f;
  std::snprintf(st.insertBlockName, sizeof(st.insertBlockName), "TREE");
  CadBlocksCommitInsertDialog(st, log);
  REQUIRE(st.cadBlockRefs.size() == 1);
  CHECK(st.cadBlockRefs[0].xf.x == Catch::Approx(5.f));
  CHECK(st.active == AppCommandState::Kind::None);
}

TEST_CASE("INSERT with attributes opens Edit Attributes after place", "[issue124][block][insert]") {
  AppCommandState st;
  CadBlockDefinition def;
  def.name = "MH";
  def.content.lines = {0.f, 0.f, 0.f, 1.f, 0.f, 0.f};
  CadBlockAttrDef ad;
  ad.tag = "ID";
  ad.prompt = "Point ID";
  ad.defaultValue = "A";
  def.attrDefs.push_back(ad);
  st.blockDefs.push_back(def);
  std::vector<std::string> log;
  StartInsertBlockCommand(st, log);
  st.insertBlockSpecifyPoint = false;
  st.insertBlockSpecifyScale = false;
  st.insertBlockSpecifyRot = false;
  std::snprintf(st.insertBlockName, sizeof(st.insertBlockName), "MH");
  CadBlocksCommitInsertDialog(st, log);
  REQUIRE(st.cadBlockRefs.size() == 1);
  CHECK(st.active == AppCommandState::Kind::InsertBlock);
  CHECK(st.insertBlockPhase == AppCommandState::InsertBlockPhase::WaitAttributes);
  CHECK(st.insertBlockAttrDialogOpen);
  std::snprintf(st.insertBlockAttrBuf[0], sizeof(st.insertBlockAttrBuf[0]), "B9");
  CadBlocksCommitInsertAttrDialog(st, log);
  CHECK(st.active == AppCommandState::Kind::None);
  CHECK(CadBlockAttrGet(st.cadBlockRefs[0], def, "ID") == "B9");
}

TEST_CASE("bundled _matchline_NORTHING DXF carries ATTDEF and labels", "[issue124][blockimport]") {
  namespace fs = std::filesystem;
  const fs::path dxf = fs::exists(fs::path("resources") / "blocks" / "_matchline_NORTHING.dxf")
                           ? fs::path("resources") / "blocks" / "_matchline_NORTHING.dxf"
                           : fs::path("..") / "resources" / "blocks" / "_matchline_NORTHING.dxf";
  REQUIRE(fs::exists(dxf));
  AppCommandState st;
  std::vector<std::string> log;
  REQUIRE(ImportCadBlocksFromPath(st, dxf.u8string().c_str(), log));
  const int di = CadBlockFindDef(st.blockDefs, "_matchline_NORTHING");
  REQUIRE(di >= 0);
  const CadBlockDefinition& d = st.blockDefs[static_cast<size_t>(di)];
  CHECK(d.attrDefs.size() >= 2);
  CHECK(d.content.texts.size() >= 2);
  CHECK(d.content.lines.size() >= 12);
  bool sawRotatedMatch = false;
  for (const CadAnnotation& t : d.content.texts) {
    if (t.kind == CadAnnotation::Kind::Mtext && t.text.find("MATCH") != std::string::npos) {
      CHECK(t.rotationRad == Catch::Approx(1.5707963f).margin(0.05f));
      sawRotatedMatch = true;
    }
  }
  CHECK(sawRotatedMatch);
  bool sawDashed = false;
  for (const EntityAttributes& a : d.content.lineAttrs) {
    if (a.linetype == "DASHED")
      sawDashed = true;
  }
  CHECK(sawDashed);
  bool sawRomansMatch = false;
  for (const CadAnnotation& t : d.content.texts) {
    if (t.kind == CadAnnotation::Kind::Mtext && t.text.find("MATCH") != std::string::npos) {
      CHECK(t.fontFamily == "romans.shx");
      sawRomansMatch = true;
    }
  }
  CHECK(sawRomansMatch);
  CadBlockRef r;
  r.defName = "_matchline_NORTHING";
  std::vector<CadBlockWorldSeg> segs;
  CadBlockCollectWorldLines(st.blockDefs, r, EntityAttributes{}, &segs);
  bool worldDashed = false;
  for (const CadBlockWorldSeg& s : segs) {
    if (s.attr.linetype == "DASHED")
      worldDashed = true;
  }
  CHECK(worldDashed);
}

TEST_CASE("matchline INSERT world annotations follow the insert, not the origin",
          "[issue124][blockimport]") {
  namespace fs = std::filesystem;
  const fs::path dxf = fs::exists(fs::path("resources") / "blocks" / "_matchline_NORTHING.dxf")
                           ? fs::path("resources") / "blocks" / "_matchline_NORTHING.dxf"
                           : fs::path("..") / "resources" / "blocks" / "_matchline_NORTHING.dxf";
  REQUIRE(fs::exists(dxf));
  AppCommandState st;
  std::vector<std::string> log;
  REQUIRE(ImportCadBlocksFromPath(st, dxf.u8string().c_str(), log));
  const int di = CadBlockFindDef(st.blockDefs, "_matchline_NORTHING");
  REQUIRE(di >= 0);

  CadBlockRef r;
  r.defName = "_matchline_NORTHING";
  r.xf.x = 10.f;
  r.xf.y = 20.f;
  std::vector<CadAnnotation> anns;
  CadBlockCollectWorldAnnotations(st.blockDefs, r, &anns);
  REQUIRE(anns.size() >= 2);
  for (const CadAnnotation& a : anns) {
    const float cx = (a.kind == CadAnnotation::Kind::Mtext) ? 0.5f * (a.boxMinX + a.boxMaxX) : a.insX;
    const float cy = (a.kind == CadAnnotation::Kind::Mtext) ? 0.5f * (a.boxMinY + a.boxMaxY) : a.insY;
    const float dIns = std::hypot(cx - 10.f, cy - 20.f);
    const float dOrig = std::hypot(cx, cy);
    CHECK(dIns < 5.f);
    CHECK(dOrig > 15.f);
  }
}

TEST_CASE("importing _matchline_ DXF authors DistNeg dynamics", "[issue124][blockimport]") {
  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() / "gosurvey-blockimport";
  fs::create_directories(dir);
  const fs::path dxf = dir / "_matchline_NORTHING.dxf";
  {
    std::ofstream f(dxf, std::ios::binary);
    f << "0\nSECTION\n2\nHEADER\n9\n$INSUNITS\n70\n2\n0\nENDSEC\n"
         "0\nSECTION\n2\nBLOCKS\n0\nENDSEC\n"
         "0\nSECTION\n2\nENTITIES\n"
         "0\nLINE\n8\n0\n10\n0.0\n20\n-2.0\n30\n0.0\n11\n0.0\n21\n2.0\n31\n0.0\n"
         "0\nENDSEC\n0\nEOF\n";
  }
  AppCommandState st;
  std::vector<std::string> log;
  REQUIRE(ImportCadBlocksFromPath(st, dxf.u8string().c_str(), log));
  const int di = CadBlockFindDef(st.blockDefs, "_matchline_NORTHING");
  REQUIRE(di >= 0);
  CHECK(CadBlockHasMatchlineDyn(st.blockDefs[static_cast<size_t>(di)]));
}

TEST_CASE("bundled matchline import upgrades a definition that has no attributes", "[issue124][blockimport]") {
  AppCommandState st;
  CadBlockDefinition stub;
  stub.name = "_matchline_NORTHING";
  stub.content.lines = {0.f, -2.f, 0.f, 0.f, 2.f, 0.f};
  st.blockDefs.push_back(stub);
  namespace fs = std::filesystem;
  const fs::path dxf = fs::exists(fs::path("resources") / "blocks" / "_matchline_NORTHING.dxf")
                           ? fs::path("resources") / "blocks" / "_matchline_NORTHING.dxf"
                           : fs::path("..") / "resources" / "blocks" / "_matchline_NORTHING.dxf";
  REQUIRE(fs::exists(dxf));
  std::vector<std::string> log;
  REQUIRE(ImportCadBlocksFromPath(st, dxf.u8string().c_str(), log));
  const int di = CadBlockFindDef(st.blockDefs, "_matchline_NORTHING");
  REQUIRE(di >= 0);
  CHECK(st.blockDefs[static_cast<size_t>(di)].attrDefs.size() >= 2);
}

TEST_CASE("INSERT matchline at 90 degrees opens attributes and lays the line horizontal",
          "[issue124][block][insert]") {
  AppCommandState st;
  std::vector<std::string> log;
  LoadBundledBlockLibrary(st, log);
  const int di = CadBlockFindDef(st.blockDefs, "_matchline_NORTHING");
  REQUIRE(di >= 0);
  REQUIRE_FALSE(st.blockDefs[static_cast<size_t>(di)].attrDefs.empty());

  StartInsertBlockCommand(st, log);
  std::snprintf(st.insertBlockName, sizeof(st.insertBlockName), "_matchline_NORTHING");
  CadBlocksApplyInsertNameDefaults(st);
  st.insertBlockSpecifyPoint = true;
  st.insertBlockSpecifyScale = false;
  st.insertBlockSpecifyRot = true;
  CadBlocksCommitInsertDialog(st, log);
  REQUIRE(st.insertBlockPhase == AppCommandState::InsertBlockPhase::WaitInsertPoint);
  SubmitInsertBlockPick(st, 10.f, 20.f, log);
  REQUIRE(st.insertBlockPhase == AppCommandState::InsertBlockPhase::WaitRotation);

  char ang[32] = "90";
  ProcessCommandLineSubmit(ang, static_cast<int>(sizeof(ang)), st, log);
  REQUIRE(st.cadBlockRefs.size() == 1);
  // 90 = 90° clockwise from north (the app convention); the block frame is CCW-from-east, so rotZ
  // is −π/2. A north-authored matchline rotated 90° CW lands east — horizontal, asserted below.
  CHECK(st.cadBlockRefs[0].xf.rotZ == Catch::Approx(-1.5707963f).margin(0.01f));
  CHECK(st.insertBlockPhase == AppCommandState::InsertBlockPhase::WaitAttributes);
  CHECK(st.insertBlockAttrDialogOpen);

  std::vector<CadBlockWorldSeg> segs;
  CadBlockCollectWorldLines(st.blockDefs, st.cadBlockRefs[0], EntityAttributes{}, &segs);
  REQUIRE_FALSE(segs.empty());
  CHECK(segs[0].y0 == Catch::Approx(segs[0].y1).margin(0.05f));
}

TEST_CASE("BEDIT with no name opens the definition picker", "[issue124][block][bedit]") {
  AppCommandState st;
  std::istringstream args("");
  std::vector<std::string> log;
  REQUIRE(CadBlocksTryIdleCommand(st, "bedit", args, log));
  CHECK(st.blockEditPickerOpen);
  CHECK(st.blockEditorName.empty());
}

TEST_CASE("BEDIT picker OK starts the editor", "[issue124][block][bedit]") {
  AppCommandState st;
  CadBlockDefinition def;
  def.name = "HYDRANT";
  st.blockDefs.push_back(def);
  std::snprintf(st.blockEditPickerName, sizeof(st.blockEditPickerName), "HYDRANT");
  std::vector<std::string> log;
  CadBlocksCommitEditPicker(st, log);
  CHECK(st.blockEditorName == "HYDRANT");
  CHECK_FALSE(st.blockEditPickerOpen);
}

TEST_CASE("BEDIT with a name skips the picker", "[issue124][block][bedit]") {
  AppCommandState st;
  CadBlockDefinition def;
  def.name = "HYDRANT";
  st.blockDefs.push_back(def);
  std::istringstream args("HYDRANT");
  std::vector<std::string> log;
  REQUIRE(CadBlocksTryIdleCommand(st, "bedit", args, log));
  CHECK(st.blockEditorName == "HYDRANT");
  CHECK_FALSE(st.blockEditPickerOpen);
}

// Bug: INSERT rotation must follow the app-wide angle convention (0deg = north, clockwise
// positive -- CadCommands.hpp), the same as ROTATE. A block whose geometry points north
// must come in pointing north at rotation 0, and pointing east at rotation 90.
TEST_CASE("INSERT rotation is clockwise from north", "[issue124][block][rotation]") {
  AppCommandState st;
  st.blockDefs.emplace_back();
  st.blockDefs[0].name = "ML";
  st.blockDefs[0].units = CadDrawingInsUnitsName(st.drawingInsUnits);  // isolate rotation from unit scaling
  st.blockDefs[0].content.lines = {0.f, 0.f, 0.f, 0.f, 2.f, 0.f};  // points NORTH (+Y)
  st.blockDefs[0].content.lineAttrs.resize(1);
  st.blockDefs[0].content.lineVis.resize(1);

  auto dirAt = [&](const char* deg) {
    st.cadBlockRefs.clear();
    std::istringstream args(std::string("ML, 10, 10, 1, 1, ") + deg);
    std::vector<std::string> log;
    REQUIRE(CadBlocksTryIdleCommand(st, "insert", args, log));
    REQUIRE(st.cadBlockRefs.size() == 1);
    std::vector<CadBlockWorldSeg> segs;
    CadBlockCollectWorldLines(st.blockDefs, st.cadBlockRefs[0], EntityAttributes{}, &segs);
    REQUIRE(segs.size() == 1);
    return std::pair<float, float>{segs[0].x1 - segs[0].x0, segs[0].y1 - segs[0].y0};
  };

  const auto d0 = dirAt("0");
  CHECK(d0.first == Catch::Approx(0.f).margin(2e-3));
  CHECK(d0.second == Catch::Approx(2.f).margin(2e-3));  // rotation 0 -> unchanged (north)

  const auto d90 = dirAt("90");
  CHECK(d90.first == Catch::Approx(2.f).margin(2e-3));   // 90 clockwise from north -> east
  CHECK(d90.second == Catch::Approx(0.f).margin(2e-3));

  const auto d180 = dirAt("180");
  CHECK(d180.first == Catch::Approx(0.f).margin(2e-3));
  CHECK(d180.second == Catch::Approx(-2.f).margin(2e-3));  // 180 -> south
}

// REQ-107 (D-2026-08-29-i): the live INSERT preview transform must match what the pick commits.
TEST_CASE("INSERT rotation preview transform matches the committed insert", "[issue124][block][insert][preview]") {
  AppCommandState st;
  CadBlockDefinition def;
  def.name = "TREE";
  def.units = CadDrawingInsUnitsName(st.drawingInsUnits);  // isolate from unit rescale
  def.content.lines = {0.f, 0.f, 0.f, 1.f, 0.f, 0.f};
  st.blockDefs.push_back(def);
  std::vector<std::string> log;

  StartInsertBlockCommand(st, log);
  std::snprintf(st.insertBlockName, sizeof(st.insertBlockName), "TREE");
  st.insertBlockSpecifyPoint = true;
  st.insertBlockSpecifyScale = false;
  st.insertBlockSpecifyRot = true;
  st.insertBlockDialogOpen = false;
  st.insertBlockPhase = AppCommandState::InsertBlockPhase::WaitInsertPoint;

  SubmitInsertBlockPick(st, 5.f, 6.f, log);  // insertion point
  REQUIRE(st.insertBlockPhase == AppCommandState::InsertBlockPhase::WaitRotation);

  CadBlockXform pv;
  REQUIRE(CadBlockInsertPreviewXform(st, 15.f, 6.f, &pv));  // cursor due east -> 90 deg cw-from-north

  SubmitInsertBlockPick(st, 15.f, 6.f, log);  // commit rotation
  REQUIRE(st.cadBlockRefs.size() == 1);
  CHECK(st.cadBlockRefs[0].xf.rotZ == Catch::Approx(pv.rotZ).margin(1e-4));
  CHECK(pv.rotZ == Catch::Approx(-1.57079633f).margin(1e-4));
}

TEST_CASE("INSERT scale preview transform matches the committed insert", "[issue124][block][insert][preview]") {
  AppCommandState st;
  CadBlockDefinition def;
  def.name = "TREE";
  def.units = CadDrawingInsUnitsName(st.drawingInsUnits);  // isolate from unit rescale
  def.content.lines = {0.f, 0.f, 0.f, 1.f, 0.f, 0.f};
  st.blockDefs.push_back(def);
  std::vector<std::string> log;

  StartInsertBlockCommand(st, log);
  std::snprintf(st.insertBlockName, sizeof(st.insertBlockName), "TREE");
  st.insertBlockSpecifyPoint = true;
  st.insertBlockSpecifyScale = true;
  st.insertBlockSpecifyRot = false;
  st.insertBlockUniformScale = true;
  st.insertBlockDialogOpen = false;
  st.insertBlockPhase = AppCommandState::InsertBlockPhase::WaitInsertPoint;

  SubmitInsertBlockPick(st, 0.f, 0.f, log);  // insertion point
  REQUIRE(st.insertBlockPhase == AppCommandState::InsertBlockPhase::WaitScale);

  CadBlockXform pv;
  REQUIRE(CadBlockInsertPreviewXform(st, 3.f, 4.f, &pv));  // distance 5
  CHECK(pv.sx == Catch::Approx(5.f));

  SubmitInsertBlockPick(st, 3.f, 4.f, log);  // commit scale (rotation off -> places)
  REQUIRE(st.cadBlockRefs.size() == 1);
  CHECK(st.cadBlockRefs[0].xf.sx == Catch::Approx(pv.sx));
}

TEST_CASE("INSERT preview emits a rotated block ghost into the rubber lines", "[issue124][block][insert][preview]") {
  AppCommandState st;
  CadBlockDefinition def;
  def.name = "TREE";
  def.units = CadDrawingInsUnitsName(st.drawingInsUnits);  // isolate from unit rescale
  def.content.lines = {0.f, 0.f, 0.f, 0.f, 2.f, 0.f};  // one segment pointing north
  st.blockDefs.push_back(def);
  std::vector<std::string> log;

  StartInsertBlockCommand(st, log);
  std::snprintf(st.insertBlockName, sizeof(st.insertBlockName), "TREE");
  st.insertBlockSpecifyPoint = true;
  st.insertBlockSpecifyRot = true;
  st.insertBlockSpecifyScale = false;
  st.insertBlockDialogOpen = false;
  st.insertBlockPhase = AppCommandState::InsertBlockPhase::WaitInsertPoint;
  SubmitInsertBlockPick(st, 10.f, 10.f, log);
  REQUIRE(st.insertBlockPhase == AppCommandState::InsertBlockPhase::WaitRotation);

  std::vector<float> rubber;
  // Cursor due east of the insertion point -> 90 deg cw-from-north -> ghost segment points east.
  AppendCadDraftRubberLines(st, 20.0, 10.0, /*orthoEnabled=*/false, 0.0, 0.0, 50.f, 800, rubber);
  REQUIRE(rubber.size() >= 12u);  // drag indicator (6) + at least one ghost segment (6)
  // Find the ghost segment anchored at the insertion point (10,10).
  bool foundEastGhost = false;
  for (size_t i = 0; i + 5 < rubber.size(); i += 6) {
    if (std::fabs(rubber[i] - 10.f) < 1e-2f && std::fabs(rubber[i + 1] - 10.f) < 1e-2f &&
        std::fabs(rubber[i + 3] - 12.f) < 1e-2f && std::fabs(rubber[i + 4] - 10.f) < 1e-2f)
      foundEastGhost = true;
  }
  CHECK(foundEastGhost);
}

TEST_CASE("INSERT preview is inert with no definition selected", "[issue124][block][insert][preview]") {
  AppCommandState st;
  st.insertBlockPhase = AppCommandState::InsertBlockPhase::WaitRotation;
  std::snprintf(st.insertBlockName, sizeof(st.insertBlockName), "MISSING");
  CadBlockXform pv;
  CHECK_FALSE(CadBlockInsertPreviewXform(st, 1.f, 1.f, &pv));
}
