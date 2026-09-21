#include "CadBlocks.hpp"
#include "CadCommands.hpp"
#include "CadRubberPreview.hpp"
#include "HeadlessFileDialogs.hpp"
#include "io/GsIo.hpp"
#include "util/brep.hpp"
#include "util/ucs.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
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

TEST_CASE("LIBEXPORT writes a tagged fitting to .dwg plus a metadata sidecar",
          "[issue486][libexport][blockimport]") {
  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() / "gosurvey-libexport";
  fs::create_directories(dir);
  const fs::path dwg = dir / "ELBOW90-4IN.dwg";
  const fs::path sidecar = dir / "ELBOW90-4IN.json";
  std::error_code rmEc;
  fs::remove(dwg, rmEc);
  fs::remove(sidecar, rmEc);

  AppCommandState st;
  CadBlockDefinition def;
  def.name = "ELBOW90-4IN";
  def.units = CadDrawingInsUnitsName(st.drawingInsUnits);
  def.partType = CadPipePartType::Elbow90;
  def.nominalSize = "4in";
  def.pressureClass = CadPipePressureClass::CS150;
  def.partNumber = "ACME-E90-4";
  CadBlockConnection a;
  a.name = "P1";
  a.nominalSize = "4in";
  a.role = CadBlockConnectionRole::Inlet;
  def.connections.push_back(a);
  CadBlockConnection b;
  b.name = "P2";
  b.nominalSize = "4in";
  b.role = CadBlockConnectionRole::Outlet;
  def.connections.push_back(b);
  st.blockDefs.push_back(def);

  std::vector<std::string> log;
  std::istringstream args(std::string("ELBOW90-4IN, ") + dwg.u8string());
  REQUIRE(CadBlocksTryIdleCommand(st, "libexport", args, log));
  REQUIRE(fs::exists(dwg));
  REQUIRE(fs::exists(sidecar));

  // The DWG trailer round-trips every A1/A2 field, same mechanism as WBLOCK.
  AppCommandState dest;
  std::vector<std::string> importLog;
  REQUIRE(ImportCadBlocksFromPath(dest, dwg.u8string().c_str(), importLog));
  const int di = CadBlockFindDef(dest.blockDefs, "ELBOW90-4IN");
  REQUIRE(di >= 0);
  const CadBlockDefinition& out = dest.blockDefs[static_cast<size_t>(di)];
  CHECK(out.partType == CadPipePartType::Elbow90);
  CHECK(out.pressureClass == CadPipePressureClass::CS150);
  CHECK(out.partNumber == "ACME-E90-4");
  REQUIRE(out.connections.size() == 2);
  CHECK(out.connections[1].role == CadBlockConnectionRole::Outlet);

  // Sidecar is a plain-text catalog index carrying the same headline fields.
  std::ifstream sideIn(sidecar);
  std::string sideJson((std::istreambuf_iterator<char>(sideIn)), std::istreambuf_iterator<char>());
  CHECK(sideJson.find("\"elbow-90\"") != std::string::npos);
  CHECK(sideJson.find("\"CS150\"") != std::string::npos);
  CHECK(sideJson.find("ACME-E90-4") != std::string::npos);
  CHECK(sideJson.find("\"outlet\"") != std::string::npos);
}

TEST_CASE("LIBEXPORT refuses a block with no fitting metadata", "[issue486][libexport]") {
  AppCommandState st;
  CadBlockDefinition def;
  def.name = "PLAIN";
  st.blockDefs.push_back(def);

  std::vector<std::string> log;
  std::istringstream args("PLAIN, C:/does/not/matter.dwg");
  REQUIRE(CadBlocksTryIdleCommand(st, "libexport", args, log));
  bool refused = false;
  for (const std::string& line : log) {
    if (line.find("no fitting metadata") != std::string::npos)
      refused = true;
  }
  CHECK(refused);
}

TEST_CASE("LIBEXPORT refuses a missing block name", "[issue486][libexport]") {
  AppCommandState st;
  std::vector<std::string> log;
  std::istringstream args("MISSING, C:/does/not/matter.dwg");
  REQUIRE(CadBlocksTryIdleCommand(st, "libexport", args, log));
  bool refused = false;
  for (const std::string& line : log) {
    if (line.find("no block named") != std::string::npos)
      refused = true;
  }
  CHECK(refused);
}

TEST_CASE("LIBEXPORT default path lands in CadFittingLibraryExportDir and lists with sidecar metadata",
          "[issue486][libexport][library]") {
  namespace fs = std::filesystem;
  const fs::path dir = CadFittingLibraryExportDir();
  if (dir.empty())
    return; // No %APPDATA% (or equivalent) in this environment — nothing to verify.
  const std::string name = "GS_TEST_LIBEXPORT_A5_ELBOW";
  const fs::path dwg = dir / (name + ".dwg");
  const fs::path sidecar = dir / (name + ".json");
  std::error_code rmEc;
  fs::remove(dwg, rmEc);
  fs::remove(sidecar, rmEc);

  AppCommandState st;
  CadBlockDefinition def;
  def.name = name;
  def.units = CadDrawingInsUnitsName(st.drawingInsUnits);
  def.partType = CadPipePartType::Tee;
  def.nominalSize = "6in";
  def.pressureClass = CadPipePressureClass::CS300;
  st.blockDefs.push_back(def);

  std::vector<std::string> log;
  std::istringstream args(name);
  const bool ok = CadBlocksTryIdleCommand(st, "libexport", args, log);
  const bool wrote = fs::exists(dwg) && fs::exists(sidecar);
  if (wrote) {
    AppCommandState fresh; // Empty blockDefs — the entry must come from disk, not memory.
    std::vector<CadBlockLibraryEntry> lib;
    CadBlocksCollectLibraryEntries(fresh, &lib);
    const auto it = std::find_if(lib.begin(), lib.end(),
                                 [&](const CadBlockLibraryEntry& e) { return e.name == name; });
    REQUIRE(it != lib.end());
    CHECK(it->isFitting);
    CHECK_FALSE(it->imported);
    CHECK(it->partType == CadPipePartType::Tee);
    CHECK(it->nominalSize == "6in");
    CHECK(it->pressureClass == CadPipePressureClass::CS300);
  }

  fs::remove(dwg, rmEc);
  fs::remove(sidecar, rmEc);
  REQUIRE(ok);
  REQUIRE(wrote);
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

TEST_CASE("BEDIT loads and BSAVE harvests a block solid", "[issue475][block][bedit][solid]") {
  ucs::Ucs frame;
  brep::Solid box;
  brep::Problem why = brep::Problem::Ok;
  REQUIRE(brep::MakeBox(frame, 1.0, 1.0, 2.0, &box, &why));

  AppCommandState st;
  CadBlockDefinition def;
  def.name = "SOLFIT";
  def.content.solids.push_back(std::make_shared<const brep::Solid>(std::move(box)));
  def.content.solidAttrs.push_back(EntityAttributes{});
  st.blockDefs.push_back(def);

  std::vector<std::string> log;
  std::istringstream beditArgs("SOLFIT");
  REQUIRE(CadBlocksTryIdleCommand(st, "bedit", beditArgs, log));
  REQUIRE(st.blockEditActive);
  REQUIRE(st.cadSolids.size() == 1);
  REQUIRE(st.cadSolids[0]);
  CHECK(st.cadSolids[0]->faces.size() == 6);

  std::istringstream bsaveArgs("");
  REQUIRE(CadBlocksTryIdleCommand(st, "bsave", bsaveArgs, log));
  const int di = CadBlockFindDef(st.blockDefs, "SOLFIT");
  REQUIRE(di >= 0);
  REQUIRE(st.blockDefs[static_cast<size_t>(di)].content.solids.size() == 1);

  std::istringstream bcloseArgs("save");
  REQUIRE(CadBlocksTryIdleCommand(st, "bclose", bcloseArgs, log));
  CHECK_FALSE(st.blockEditActive);

  CadBlockXform xf;
  xf.x = 5.f;
  xf.y = 6.f;
  xf.z = 7.f;
  st.cadBlockRefs.clear();
  REQUIRE(CadBlockPlaceInsert(st, "SOLFIT", xf, false, log));
  REQUIRE(st.cadBlockRefs.size() == 1);
  std::vector<CadBlockWorldSolid> ws;
  CadBlockCollectWorldSolids(st.blockDefs, st.cadBlockRefs[0], EntityAttributes{}, &ws);
  REQUIRE(ws.size() == 1);
  REQUIRE(ws[0].solid);
  const brep::Bounds bb = brep::ComputeBounds(*ws[0].solid);
  REQUIRE(bb.valid);
  CHECK(bb.mn.z == Catch::Approx(7.0).margin(0.05));
}

TEST_CASE("Bundled fittings library imports SAT as block defs only", "[issue475][block][library][fitting]") {
  namespace fs = std::filesystem;
  const fs::path sat =
      fs::exists(fs::path("resources") / "blocks" / "fittings" / "CJ_4in_WELD_NECK_FLANGE.sat")
          ? fs::path("resources") / "blocks" / "fittings" / "CJ_4in_WELD_NECK_FLANGE.sat"
          : fs::path("..") / "resources" / "blocks" / "fittings" / "CJ_4in_WELD_NECK_FLANGE.sat";
  if (!fs::exists(sat)) {
    WARN("Bundled flange SAT not present — skip.");
    return;
  }
  AppCommandState st;
  std::vector<std::string> log;
  CadBlockLibraryEntry entry;
  entry.name = "CJ_4in_WELD_NECK_FLANGE";
  entry.path = sat.u8string();
  entry.isFitting = true;
  REQUIRE(CadBlocksImportLibraryEntry(st, entry, log));
  const int di = CadBlockFindDef(st.blockDefs, "CJ_4in_WELD_NECK_FLANGE");
  REQUIRE(di >= 0);
  REQUIRE_FALSE(st.blockDefs[static_cast<size_t>(di)].content.solids.empty());
  CHECK(st.cadSolids.empty());
}

TEST_CASE("CadBlockInsertUnitsScale honours the INSERT dialog unit override", "[issue475][block][units]") {
  AppCommandState st;
  st.drawingInsUnits = 2;
  CadBlockDefinition def;
  def.name = "FIT";
  def.units = "inches";
  std::snprintf(st.insertBlockUnitsBuf, sizeof(st.insertBlockUnitsBuf), "unitless");
  CHECK(CadBlockInsertUnitsScale(st, def) == Catch::Approx(1.f));
  std::snprintf(st.insertBlockUnitsBuf, sizeof(st.insertBlockUnitsBuf), "inches");
  CHECK(CadBlockInsertUnitsScale(st, def) == Catch::Approx(1.f / 12.f));
}

TEST_CASE("INSERT connector snap places the fitting port on the target", "[issue475][block][connector][insert]") {
  AppCommandState st;

  CadBlockDefinition host;
  host.name = "HOST";
  CadBlockConnection hc;
  hc.name = "P1";
  hc.nominalSize = "4in";
  host.connections.push_back(hc);
  st.blockDefs.push_back(host);

  CadBlockDefinition tail;
  tail.name = "TAIL";
  CadBlockConnection tc;
  tc.name = "P1";
  tc.x = 0.f;
  tc.y = 0.f;
  tc.z = 2.f;
  tc.nx = 0.f;
  tc.ny = 0.f;
  tc.nz = 1.f;
  tail.connections.push_back(tc);
  st.blockDefs.push_back(tail);

  std::vector<std::string> log;
  CadBlockXform hostXf;
  REQUIRE(CadBlockPlaceInsert(st, "HOST", hostXf, false, log));
  REQUIRE(st.cadBlockRefs.size() == 1);

  StartInsertBlockCommand(st, log);
  std::snprintf(st.insertBlockName, sizeof(st.insertBlockName), "TAIL");
  st.insertBlockSpecifyConnectorSnap = true;
  st.insertBlockSpecifyPoint = false;
  st.insertBlockSpecifyRot = false;
  st.insertBlockSpecifyScale = false;
  st.insertBlockDialogOpen = false;
  st.insertBlockPhase = AppCommandState::InsertBlockPhase::WaitConnectorTarget;

  REQUIRE(SubmitInsertBlockConnectorPick(st, 0.f, 0.f, 0.f, log));
  REQUIRE(st.cadBlockRefs.size() == 2);

  std::vector<CadBlockWorldConnection> world;
  CadBlockCollectWorldConnections(st.blockDefs, st.cadBlockRefs[1], 1, &world);
  REQUIRE(world.size() == 1);
  CHECK(world[0].x == Catch::Approx(0.f).margin(0.002));
  CHECK(world[0].y == Catch::Approx(0.f).margin(0.002));
  CHECK(world[0].z == Catch::Approx(0.f).margin(0.002));
  const float dot = world[0].nx * 0.f + world[0].ny * 0.f + world[0].nz * 1.f;
  CHECK(dot == Catch::Approx(-1.f).margin(0.02));
}

TEST_CASE("INSERT connector snap reaches a CadPipeRun's own end (issue #486)",
          "[issue475][issue486][block][connector][insert][piperun]") {
  AppCommandState st;

  CadPipeRun run;
  run.vertsXyz = {0.0, 0.0, 0.0, 10.0, 0.0, 0.0};
  run.nominalSize = "4in";
  st.cadPipeRuns.push_back(run);
  st.cadPipeRunAttrs.push_back(EntityAttributes{});

  CadBlockDefinition flange;
  flange.name = "FLANGE";
  CadBlockConnection fc;
  fc.name = "P1";
  fc.x = 0.f;
  fc.y = 0.f;
  fc.z = 0.f;
  fc.nx = 0.f;
  fc.ny = 0.f;
  fc.nz = 1.f;
  flange.connections.push_back(fc);
  st.blockDefs.push_back(flange);

  std::vector<std::string> log;
  StartInsertBlockCommand(st, log);
  std::snprintf(st.insertBlockName, sizeof(st.insertBlockName), "FLANGE");
  st.insertBlockSpecifyConnectorSnap = true;
  st.insertBlockSpecifyPoint = false;
  st.insertBlockSpecifyRot = false;
  st.insertBlockSpecifyScale = false;
  st.insertBlockDialogOpen = false;
  st.insertBlockPhase = AppCommandState::InsertBlockPhase::WaitConnectorTarget;

  // Click near the run's END (10,0,0), not exactly on it — the snap is a nearest-within-2ft search.
  REQUIRE(SubmitInsertBlockConnectorPick(st, 9.99f, 0.f, 0.f, log));
  REQUIRE(st.cadBlockRefs.size() == 1);
  const bool sawPipeEnd =
      std::any_of(log.begin(), log.end(), [](const std::string& s) { return s.find("pipe end") != std::string::npos; });
  CHECK(sawPipeEnd);

  std::vector<CadBlockWorldConnection> world;
  CadBlockCollectWorldConnections(st.blockDefs, st.cadBlockRefs[0], 0, &world);
  REQUIRE(world.size() == 1);
  CHECK(world[0].x == Catch::Approx(10.f).margin(0.002));
  CHECK(world[0].y == Catch::Approx(0.f).margin(0.002));
}

TEST_CASE("A connection point configured only for pipe end ignores a CLOSER but incompatible port "
          "(user request 2026-09-17)",
          "[issue486][issue496][block][connector][insert]") {
  AppCommandState st;

  // A pipe run end at (10,0,0) — farther from the click point than the host port below.
  CadPipeRun run;
  run.vertsXyz = {0.0, 0.0, 0.0, 10.0, 0.0, 0.0};
  run.nominalSize = "4in";
  st.cadPipeRuns.push_back(run);
  st.cadPipeRunAttrs.push_back(EntityAttributes{});

  // A real block connection port (generic — no part type) at (9,0,0), CLOSER to the click point.
  CadBlockDefinition host;
  host.name = "HOST";
  CadBlockConnection hc;
  hc.name = "P1";
  hc.x = 9.f;
  hc.y = 0.f;
  hc.z = 0.f;
  hc.nz = 1.f;
  host.connections.push_back(hc);
  st.blockDefs.push_back(host);
  CadBlockXform hostXf;
  std::vector<std::string> setupLog;
  REQUIRE(CadBlockPlaceInsert(st, "HOST", hostXf, false, setupLog));
  REQUIRE(st.cadBlockRefs.size() == 1);

  // The fitting being inserted has a port configured ONLY for a pipe end — no FlangeFace/GenericPort
  // mode and no isDefault fallback, so a generic block port must never be an acceptable target for it.
  CadBlockDefinition flange;
  flange.name = "FLANGE";
  CadBlockConnection fc;
  fc.name = "P1";
  fc.nz = 1.f;
  CadBlockConnectionMode pipeMode;
  pipeMode.name = "ToPipe";
  pipeMode.target = CadConnectionModeTarget::PipeEnd;
  pipeMode.isDefault = false;
  fc.modes.push_back(pipeMode);
  flange.connections.push_back(fc);
  st.blockDefs.push_back(flange);

  std::vector<std::string> log;
  StartInsertBlockCommand(st, log);
  std::snprintf(st.insertBlockName, sizeof(st.insertBlockName), "FLANGE");
  st.insertBlockSpecifyConnectorSnap = true;
  st.insertBlockSpecifyPoint = false;
  st.insertBlockSpecifyRot = false;
  st.insertBlockSpecifyScale = false;
  st.insertBlockDialogOpen = false;
  st.insertBlockPhase = AppCommandState::InsertBlockPhase::WaitConnectorTarget;

  // Click at (9.3,0,0): 0.3 ft from the host port, 0.7 ft from the pipe end — the host port is
  // geometrically nearer, but incompatible with this connection point's configured mode.
  REQUIRE(SubmitInsertBlockConnectorPick(st, 9.3f, 0.f, 0.f, log));
  REQUIRE(st.cadBlockRefs.size() == 2);  // HOST (setup) + the placed FLANGE

  const bool sawPipeEnd =
      std::any_of(log.begin(), log.end(), [](const std::string& s) { return s.find("pipe end") != std::string::npos; });
  CHECK(sawPipeEnd);

  std::vector<CadBlockWorldConnection> world;
  CadBlockCollectWorldConnections(st.blockDefs, st.cadBlockRefs[1], 1, &world);
  REQUIRE(world.size() == 1);
  CHECK(world[0].x == Catch::Approx(10.f).margin(0.002));  // landed on the FARTHER but compatible pipe end
  CHECK(world[0].y == Catch::Approx(0.f).margin(0.002));
}

TEST_CASE("INSERT auto-picks the block's WELD-NECK port (not the FIRST-defined gasket-face port) "
          "when snapping to a pipe end (issue #486 user bug report)",
          "[issue486][issue496][block][connector][insert]") {
  AppCommandState st;

  CadPipeRun run;
  run.vertsXyz = {0.0, 0.0, 0.0, 10.0, 0.0, 0.0};
  run.nominalSize = "4in";
  st.cadPipeRuns.push_back(run);
  st.cadPipeRunAttrs.push_back(EntityAttributes{});

  // A flange with TWO connection points, gasketFace defined FIRST (reproducing the reported bug:
  // InsertSourceConnection used to always default to connections.front()), each with exactly ONE
  // mode aimed at a different target — exactly the setup in the user's screenshots.
  CadBlockDefinition flange;
  flange.name = "WELD_NECK_FLANGE";
  CadBlockConnection gasketFace;
  gasketFace.name = "gasketFace";
  gasketFace.nz = 1.f;
  CadBlockConnectionMode gasketMode;
  gasketMode.name = "Mode 1";
  gasketMode.target = CadConnectionModeTarget::FlangeFace;
  gasketMode.isDefault = true;  // matches the reported scenario: the UI's natural single-mode state
  gasketFace.modes.push_back(gasketMode);
  flange.connections.push_back(gasketFace);

  CadBlockConnection weldNeckFace;
  weldNeckFace.name = "weldNeckFace";
  weldNeckFace.nz = 1.f;
  CadBlockConnectionMode weldMode;
  weldMode.name = "Mode 1";
  weldMode.target = CadConnectionModeTarget::PipeEnd;
  weldMode.isDefault = true;    // both flagged default is exactly what made the wrong port "match"
  weldNeckFace.modes.push_back(weldMode);
  flange.connections.push_back(weldNeckFace);
  st.blockDefs.push_back(flange);

  std::vector<std::string> log;
  StartInsertBlockCommand(st, log);
  std::snprintf(st.insertBlockName, sizeof(st.insertBlockName), "WELD_NECK_FLANGE");
  st.insertBlockConnectorName[0] = '\0';  // no explicit choice — auto-detect, the reported scenario
  st.insertBlockSpecifyConnectorSnap = true;
  st.insertBlockSpecifyPoint = false;
  st.insertBlockSpecifyRot = false;
  st.insertBlockSpecifyScale = false;
  st.insertBlockDialogOpen = false;
  st.insertBlockPhase = AppCommandState::InsertBlockPhase::WaitConnectorTarget;

  REQUIRE(SubmitInsertBlockConnectorPick(st, 9.99f, 0.f, 0.f, log));
  REQUIRE(st.cadBlockRefs.size() == 1);

  const bool usedWeldNeck = std::any_of(log.begin(), log.end(), [](const std::string& s) {
    return s.find("pipe end") != std::string::npos;
  });
  CHECK(usedWeldNeck);

  // weldNeckFace was authored at the block's local origin (0,0,0), same as gasketFace — but the
  // POINT that mattered is which port's world connection actually lands on the pipe end (10,0,0).
  std::vector<CadBlockWorldConnection> world;
  CadBlockCollectWorldConnections(st.blockDefs, st.cadBlockRefs[0], 0, &world);
  REQUIRE(world.size() == 2);
  const auto weldNeckWorld = std::find_if(world.begin(), world.end(),
                                          [](const CadBlockWorldConnection& c) { return c.name == "weldNeckFace"; });
  REQUIRE(weldNeckWorld != world.end());
  CHECK(weldNeckWorld->x == Catch::Approx(10.f).margin(0.002));
  CHECK(weldNeckWorld->y == Catch::Approx(0.f).margin(0.002));
}

TEST_CASE("A connection point configured only for a flange face ignores a nearby pipe end",
          "[issue486][issue496][block][connector][insert]") {
  AppCommandState st;

  CadPipeRun run;
  run.vertsXyz = {0.0, 0.0, 0.0, 10.0, 0.0, 0.0};
  run.nominalSize = "4in";
  st.cadPipeRuns.push_back(run);
  st.cadPipeRunAttrs.push_back(EntityAttributes{});

  CadBlockDefinition flange;
  flange.name = "FLANGE";
  CadBlockConnection fc;
  fc.name = "P1";
  fc.nz = 1.f;
  CadBlockConnectionMode faceMode;
  faceMode.name = "ToFace";
  faceMode.target = CadConnectionModeTarget::FlangeFace;
  faceMode.isDefault = false;
  fc.modes.push_back(faceMode);
  flange.connections.push_back(fc);
  st.blockDefs.push_back(flange);

  std::vector<std::string> log;
  StartInsertBlockCommand(st, log);
  std::snprintf(st.insertBlockName, sizeof(st.insertBlockName), "FLANGE");
  st.insertBlockSpecifyConnectorSnap = true;
  st.insertBlockSpecifyPoint = false;
  st.insertBlockSpecifyRot = false;
  st.insertBlockSpecifyScale = false;
  st.insertBlockDialogOpen = false;
  st.insertBlockPhase = AppCommandState::InsertBlockPhase::WaitConnectorTarget;

  // Only a pipe end is nearby (no flange face anywhere) — this port must refuse rather than snap
  // to a target kind it was never configured to mate with.
  REQUIRE_FALSE(SubmitInsertBlockConnectorPick(st, 9.99f, 0.f, 0.f, log));
  CHECK(st.cadBlockRefs.empty());
  const bool sawRefusal = std::any_of(log.begin(), log.end(), [](const std::string& s) {
    return s.find("configured mode") != std::string::npos;
  });
  CHECK(sawRefusal);
}

TEST_CASE("BEDIT BCONNECT wizard: typed coords persist on the definition",
          "[issue475][issue496][block][connector][bedit]") {
  AppCommandState st;
  CadBlockDefinition def;
  def.name = "FIT";
  st.blockDefs.push_back(def);

  std::vector<std::string> log;
  std::istringstream beditArgs("FIT");
  REQUIRE(CadBlocksTryIdleCommand(st, "bedit", beditArgs, log));

  std::istringstream bconnArgs("P1");
  REQUIRE(CadBlocksTryIdleCommand(st, "bconnect", bconnArgs, log));
  REQUIRE(st.active == AppCommandState::Kind::BConnect);
  BConnectSubmitLine(st, "4in", log); // nominal size
  BConnectSubmitLine(st, "", log);    // role: keep inlet
  BConnectSubmitLine(st, "", log);    // engagement: keep 0
  BConnectSubmitLine(st, "", log);    // compat tag: skip
  BConnectSubmitLine(st, "0, 0, 0, 0, 0, 1", log); // typed point instead of a face pick
  CHECK(st.active == AppCommandState::Kind::None);

  const int di = CadBlockFindDef(st.blockDefs, "FIT");
  REQUIRE(di >= 0);
  REQUIRE(st.blockDefs[static_cast<size_t>(di)].connections.size() == 1);
  CHECK(st.blockDefs[static_cast<size_t>(di)].connections[0].name == "P1");
  CHECK(st.blockDefs[static_cast<size_t>(di)].connections[0].nz == Catch::Approx(1.f));
}

TEST_CASE("BEDIT BCONNECT wizard: role, engagement, and compatibility tag prompts are honored",
          "[issue486][issue496][block][connector][bedit]") {
  AppCommandState st;
  CadBlockDefinition def;
  def.name = "FIT";
  st.blockDefs.push_back(def);

  std::vector<std::string> log;
  std::istringstream beditArgs("FIT");
  REQUIRE(CadBlocksTryIdleCommand(st, "bedit", beditArgs, log));

  std::istringstream bconnArgs("P1");
  REQUIRE(CadBlocksTryIdleCommand(st, "bconnect", bconnArgs, log));
  BConnectSubmitLine(st, "4in", log);
  BConnectSubmitLine(st, "branch", log);
  BConnectSubmitLine(st, "0.25", log);
  BConnectSubmitLine(st, "class150", log);
  BConnectSubmitLine(st, "0, 0, 0, 0, 0, 1", log);

  const int di = CadBlockFindDef(st.blockDefs, "FIT");
  REQUIRE(di >= 0);
  const CadBlockConnection& c = st.blockDefs[static_cast<size_t>(di)].connections[0];
  CHECK(c.role == CadBlockConnectionRole::Branch);
  CHECK(c.engagementLength == Catch::Approx(0.25f));
  CHECK(c.compatibilityTag == "class150");
}

TEST_CASE("BCONNECTEDIT wizard updates role, engagement, and compatibility tag on an existing port",
          "[issue486][issue496][block][connector][bedit]") {
  AppCommandState st;
  CadBlockDefinition def;
  def.name = "FIT";
  CadBlockConnection c;
  c.name = "P1";
  c.nominalSize = "4in";
  def.connections.push_back(c);
  st.blockDefs.push_back(def);

  std::vector<std::string> log;
  std::istringstream beditArgs("FIT");
  REQUIRE(CadBlocksTryIdleCommand(st, "bedit", beditArgs, log));

  std::istringstream editArgs("P1");
  REQUIRE(CadBlocksTryIdleCommand(st, "bconnectedit", editArgs, log));
  REQUIRE(st.active == AppCommandState::Kind::BConnectEdit);
  BConnectEditSubmitLine(st, "n", log);        // don't remove
  BConnectEditSubmitLine(st, "4in", log);      // nominal size unchanged
  BConnectEditSubmitLine(st, "outlet", log);   // role
  BConnectEditSubmitLine(st, "0.5", log);      // engagement
  BConnectEditSubmitLine(st, "ansi150", log);  // compat tag
  CHECK(st.active == AppCommandState::Kind::None);

  const int di = CadBlockFindDef(st.blockDefs, "FIT");
  REQUIRE(di >= 0);
  const CadBlockConnection& out = st.blockDefs[static_cast<size_t>(di)].connections[0];
  CHECK(out.role == CadBlockConnectionRole::Outlet);
  CHECK(out.engagementLength == Catch::Approx(0.5f));
  CHECK(out.compatibilityTag == "ansi150");
}

TEST_CASE("BCONNECTEDIT wizard removes a port when the user confirms",
          "[issue486][issue496][block][connector][bedit]") {
  AppCommandState st;
  CadBlockDefinition def;
  def.name = "FIT";
  CadBlockConnection c;
  c.name = "P1";
  def.connections.push_back(c);
  st.blockDefs.push_back(def);

  std::vector<std::string> log;
  std::istringstream beditArgs("FIT");
  REQUIRE(CadBlocksTryIdleCommand(st, "bedit", beditArgs, log));
  std::istringstream editArgs("P1");
  REQUIRE(CadBlocksTryIdleCommand(st, "bconnectedit", editArgs, log));
  BConnectEditSubmitLine(st, "y", log);

  const int di = CadBlockFindDef(st.blockDefs, "FIT");
  REQUIRE(di >= 0);
  CHECK(st.blockDefs[static_cast<size_t>(di)].connections.empty());
}

TEST_CASE("BCONNECTMODE adds two modes with a single default on a connection point",
          "[issue496][block][connector][bedit]") {
  AppCommandState st;
  CadBlockDefinition def;
  def.name = "FIT";
  CadBlockConnection c;
  c.name = "P1";
  def.connections.push_back(c);
  st.blockDefs.push_back(def);

  std::vector<std::string> log;
  std::istringstream beditArgs("FIT");
  REQUIRE(CadBlocksTryIdleCommand(st, "bedit", beditArgs, log));

  const int di = CadBlockFindDef(st.blockDefs, "FIT");
  REQUIRE(di >= 0);

  // Add mode "pipe": connection name, mode name, target, role, engagement, compat tag, default.
  BConnectModeStart(st, "P1", log);
  REQUIRE(st.active == AppCommandState::Kind::BConnectMode);
  BConnectModeSubmitLine(st, "pipe", log);
  BConnectModeSubmitLine(st, "pipe-end", log);
  BConnectModeSubmitLine(st, "inlet", log);
  BConnectModeSubmitLine(st, "0.25", log);
  BConnectModeSubmitLine(st, "", log);   // no compatibility tag
  BConnectModeSubmitLine(st, "y", log);  // make default
  CHECK(st.active == AppCommandState::Kind::None);

  // Add mode "flange", not default.
  BConnectModeStart(st, "P1", log);
  BConnectModeSubmitLine(st, "flange", log);
  BConnectModeSubmitLine(st, "flange-face", log);
  BConnectModeSubmitLine(st, "inlet", log);
  BConnectModeSubmitLine(st, "0", log);
  BConnectModeSubmitLine(st, "class150", log);
  BConnectModeSubmitLine(st, "n", log);

  const CadBlockConnection& out = st.blockDefs[static_cast<size_t>(di)].connections[0];
  REQUIRE(out.modes.size() == 2);
  CHECK(out.modes[0].name == "pipe");
  CHECK(out.modes[0].target == CadConnectionModeTarget::PipeEnd);
  CHECK(out.modes[0].isDefault);
  CHECK(out.modes[1].name == "flange");
  CHECK(out.modes[1].target == CadConnectionModeTarget::FlangeFace);
  CHECK(out.modes[1].compatibilityTag == "class150");
  CHECK_FALSE(out.modes[1].isDefault);

  // Editing an existing mode name walks the same prompts, prefilled with its current values;
  // making it the new default clears the previous default so exactly one mode stays default.
  BConnectModeStart(st, "P1", log);
  BConnectModeSubmitLine(st, "flange", log);
  BConnectModeSubmitLine(st, "n", log); // don't remove — proceed to edit
  BConnectModeSubmitLine(st, "", log);  // keep target
  BConnectModeSubmitLine(st, "", log);  // keep role
  BConnectModeSubmitLine(st, "", log);  // keep engagement
  BConnectModeSubmitLine(st, "", log);  // keep compat tag
  BConnectModeSubmitLine(st, "y", log); // now default
  const CadBlockConnection& out2 = st.blockDefs[static_cast<size_t>(di)].connections[0];
  REQUIRE(out2.modes.size() == 2);
  CHECK_FALSE(out2.modes[0].isDefault);
  CHECK(out2.modes[1].isDefault);

  // Typing an existing mode name and confirming removal deletes just that mode.
  BConnectModeStart(st, "P1", log);
  BConnectModeSubmitLine(st, "pipe", log);
  BConnectModeSubmitLine(st, "y", log);
  const CadBlockConnection& out3 = st.blockDefs[static_cast<size_t>(di)].connections[0];
  REQUIRE(out3.modes.size() == 1);
  CHECK(out3.modes[0].name == "flange");
}

TEST_CASE("BCONNECTMODE cancels cleanly on an unknown connection or ESC", "[issue496][block][connector][bedit]") {
  AppCommandState st;
  CadBlockDefinition def;
  def.name = "FIT";
  st.blockDefs.push_back(def);
  std::vector<std::string> log;
  std::istringstream beditArgs("FIT");
  REQUIRE(CadBlocksTryIdleCommand(st, "bedit", beditArgs, log));

  BConnectModeStart(st, "", log);
  REQUIRE(st.active == AppCommandState::Kind::BConnectMode);
  BConnectModeSubmitLine(st, "NOPE", log);
  CHECK(st.active == AppCommandState::Kind::None);

  CancelActiveCommand(st, log);
  BConnectModeStart(st, "", log);
  CancelActiveCommand(st, log);
  CHECK(st.active == AppCommandState::Kind::None);
}

TEST_CASE("BLOCKFITTING wizard tags a block definition with piping metadata",
          "[issue486][issue496][block][fitting][bedit]") {
  AppCommandState st;
  CadBlockDefinition def;
  def.name = "ELBOW90-4IN";
  st.blockDefs.push_back(def);

  std::vector<std::string> log;
  std::istringstream beditArgs("ELBOW90-4IN");
  REQUIRE(CadBlocksTryIdleCommand(st, "bedit", beditArgs, log));
  std::istringstream fittingArgs("");
  REQUIRE(CadBlocksTryIdleCommand(st, "blockfitting", fittingArgs, log));
  REQUIRE(st.active == AppCommandState::Kind::BlockFitting);
  BlockFittingSubmitLine(st, "elbow-90", log);
  BlockFittingSubmitLine(st, "4in", log);
  BlockFittingSubmitLine(st, "CS150", log);
  BlockFittingSubmitLine(st, "ACME-E90-4", log);
  CHECK(st.active == AppCommandState::Kind::None);

  const int di = CadBlockFindDef(st.blockDefs, "ELBOW90-4IN");
  REQUIRE(di >= 0);
  const CadBlockDefinition& out = st.blockDefs[static_cast<size_t>(di)];
  CHECK(out.partType == CadPipePartType::Elbow90);
  CHECK(out.nominalSize == "4in");
  CHECK(out.pressureClass == CadPipePressureClass::CS150);
  CHECK(out.partNumber == "ACME-E90-4");
}

TEST_CASE("BLOCKFITTING refuses without an open block editor", "[issue486][block][fitting][bedit]") {
  AppCommandState st;
  std::vector<std::string> log;
  std::istringstream fittingArgs("elbow-90");
  REQUIRE(CadBlocksTryIdleCommand(st, "blockfitting", fittingArgs, log));
  CHECK_FALSE(log.empty());
  CHECK(log.back().find("BEDIT") != std::string::npos);
}

TEST_CASE("Fitting metadata and connection role/engagement/compat round-trip through .gs JSON",
          "[issue486][block][fitting][connector][gsio]") {
  AppCommandState st;
  CadBlockDefinition def;
  def.name = "ELBOW90-4IN";
  def.partType = CadPipePartType::Elbow90;
  def.nominalSize = "4in";
  def.pressureClass = CadPipePressureClass::CS150;
  def.partNumber = "ACME-E90-4";
  CadBlockConnection a;
  a.name = "P1";
  a.nominalSize = "4in";
  a.role = CadBlockConnectionRole::Inlet;
  a.engagementLength = 0.2f;
  a.compatibilityTag = "class150";
  def.connections.push_back(a);
  CadBlockConnection b;
  b.name = "P2";
  b.role = CadBlockConnectionRole::Branch;
  def.connections.push_back(b);
  st.blockDefs.push_back(def);

  const std::string json = SerializeGoSurveyJson(st);
  AppCommandState loaded;
  std::vector<std::string> log;
  REQUIRE(LoadGoSurveyFromJsonUtf8(loaded, json, log));
  const int di = CadBlockFindDef(loaded.blockDefs, "ELBOW90-4IN");
  REQUIRE(di >= 0);
  const CadBlockDefinition& out = loaded.blockDefs[static_cast<size_t>(di)];
  CHECK(out.partType == CadPipePartType::Elbow90);
  CHECK(out.nominalSize == "4in");
  CHECK(out.pressureClass == CadPipePressureClass::CS150);
  CHECK(out.partNumber == "ACME-E90-4");
  REQUIRE(out.connections.size() == 2);
  CHECK(out.connections[0].role == CadBlockConnectionRole::Inlet);
  CHECK(out.connections[0].engagementLength == Catch::Approx(0.2f));
  CHECK(out.connections[0].compatibilityTag == "class150");
  CHECK(out.connections[1].role == CadBlockConnectionRole::Branch);
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

// GitHub issue #473 — a standalone ACIS .sat file (Civil 3D / AutoCAD ACISOUT) imports through
// BLOCKIMPORT: the solid drops straight into the drawing, re-based onto the origin (the .sat
// carries its absolute position — ~4999 units — from the source drawing), and a block definition
// is kept. INSERT cannot place a 3D solid (it is a 2D command). Fixture is the real 4" weld-neck
// flange (samples/CJ_4in_WELD_NECK_FLANGE.sat).
TEST_CASE("BLOCKIMPORT of a standalone ACIS .sat drops the solid on the origin",
          "[issue473][blockimport][sat]") {
  const std::string sat = std::string(GOSURVEY_SAMPLES_DIR) + "/CJ_4in_WELD_NECK_FLANGE.sat";
  REQUIRE(std::filesystem::exists(sat));

  AppCommandState st;
  std::vector<std::string> log;
  REQUIRE(ImportCadBlocksFromPath(st, sat.c_str(), log));

  // The solid is in the drawing, re-based: centred in X/Y, its lowest point at Z 0 — NOT ~4999
  // units out where the .sat put it.
  REQUIRE(st.cadSolids.size() == 1);
  REQUIRE(st.cadSolids[0]);
  CHECK(st.cadSolids[0]->faces.size() == 16);
  CHECK(brep::Validate(*st.cadSolids[0]) == brep::Problem::Ok);
  const brep::Bounds ib = brep::ComputeBounds(*st.cadSolids[0]);
  REQUIRE(ib.valid);
  CHECK(std::fabs(ib.mn.x + ib.mx.x) < 1e-6);   // centred in X
  CHECK(std::fabs(ib.mn.y + ib.mx.y) < 1e-6);   // centred in Y
  CHECK(ib.mn.z == Catch::Approx(0.0).margin(1e-6));  // sits on Z 0
  CHECK(st.cadBlockRefs.empty());

  // The block definition is kept (for a future 3D INSERT), carrying the same re-based solid.
  const int di = CadBlockFindDef(st.blockDefs, "CJ_4in_WELD_NECK_FLANGE");
  REQUIRE(di >= 0);
  REQUIRE(st.blockDefs[static_cast<size_t>(di)].content.solids.size() == 1);
  CHECK(st.blockDefs[static_cast<size_t>(di)].units == CadDrawingInsUnitsName(st.drawingInsUnits));
}

// issue #486 increment A4 — importing a standalone .sat while BEDIT is open used to also spawn a
// second, unrelated block definition (the same wrap-and-drop behavior as the plain-drawing case
// above), even though the solid already lands directly in the block being authored (ADR-043
// aliases the model arrays to its content during the session). That was surprising duplication,
// not something the author asked for.
TEST_CASE("BLOCKIMPORT of a standalone ACIS .sat during BEDIT lands in the block being edited, no stray definition",
          "[issue486][blockimport][sat][bedit]") {
  namespace fs = std::filesystem;
  const std::string satSrc = std::string(GOSURVEY_SAMPLES_DIR) + "/CJ_4in_WELD_NECK_FLANGE.sat";
  REQUIRE(fs::exists(satSrc));
  // A renamed copy, not present in the bundled fittings library BEDIT preloads on entry, so its
  // absence/presence in blockDefs unambiguously reflects what THIS BLOCKIMPORT call did.
  const fs::path dir = fs::temp_directory_path() / "gosurvey-libexport-a4";
  fs::create_directories(dir);
  const fs::path sat = dir / "GS_TEST_A4_UNIQUE_SAT.sat";
  std::error_code cpEc;
  fs::copy_file(satSrc, sat, fs::copy_options::overwrite_existing, cpEc);
  REQUIRE_FALSE(cpEc);

  AppCommandState st;
  std::vector<std::string> log;
  std::istringstream beditArgs("FIT");
  REQUIRE(CadBlocksTryIdleCommand(st, "bedit", beditArgs, log));
  REQUIRE(st.blockEditActive);
  const size_t defCountBefore = st.blockDefs.size();
  REQUIRE(CadBlockFindDef(st.blockDefs, "GS_TEST_A4_UNIQUE_SAT") < 0);

  REQUIRE(ImportCadBlocksFromPath(st, sat.u8string().c_str(), log));

  // The solid landed directly in the block being edited (the model arrays ARE its content while
  // BEDIT is open), not floating loose in some other drawing space.
  REQUIRE(st.cadSolids.size() == 1);
  CHECK(brep::Validate(*st.cadSolids[0]) == brep::Problem::Ok);

  // No new "GS_TEST_A4_UNIQUE_SAT" definition was spawned alongside it, and the definition count
  // did not grow at all.
  CHECK(CadBlockFindDef(st.blockDefs, "GS_TEST_A4_UNIQUE_SAT") < 0);
  CHECK(st.blockDefs.size() == defCountBefore);

  // BSAVE harvests the solid into the definition actually being edited.
  std::istringstream bsaveArgs("");
  REQUIRE(CadBlocksTryIdleCommand(st, "bsave", bsaveArgs, log));
  const int di = CadBlockFindDef(st.blockDefs, "FIT");
  REQUIRE(di >= 0);
  CHECK(st.blockDefs[static_cast<size_t>(di)].content.solids.size() == 1);
}

TEST_CASE("INSERT of a SAT block in a feet drawing keeps native scale", "[issue475][block][insert][units]") {
  const std::string sat = std::string(GOSURVEY_SAMPLES_DIR) + "/CJ_4in_WELD_NECK_FLANGE.sat";
  REQUIRE(std::filesystem::exists(sat));

  AppCommandState st;
  st.drawingInsUnits = 2;  // feet
  std::vector<std::string> log;
  REQUIRE(ImportCadBlocksFromPath(st, sat.c_str(), log));
  st.cadSolids.clear();
  st.cadSolidAttrs.clear();

  CadBlockXform xf;
  xf.x = 10.f;
  xf.y = 20.f;
  xf.z = 0.f;
  xf.sx = xf.sy = xf.sz = 1.f;
  REQUIRE(CadBlockPlaceInsert(st, "CJ_4in_WELD_NECK_FLANGE", xf, false, log));
  REQUIRE(st.cadBlockRefs.size() == 1);
  CHECK(st.cadBlockRefs[0].xf.sx == Catch::Approx(1.f));

  std::vector<CadBlockWorldSolid> ws;
  CadBlockCollectWorldSolids(st.blockDefs, st.cadBlockRefs[0], EntityAttributes{}, &ws);
  REQUIRE(ws.size() == 1);
  REQUIRE(ws[0].solid);
  const brep::Bounds bb = brep::ComputeBounds(*ws[0].solid);
  REQUIRE(bb.valid);
  CHECK(bb.mx.x - bb.mn.x == Catch::Approx(0.75006).margin(0.02));
  CHECK(bb.mx.y - bb.mn.y == Catch::Approx(0.75006).margin(0.02));
  CHECK(bb.mx.z - bb.mn.z == Catch::Approx(0.25).margin(0.02));
}

TEST_CASE("BLOCKIMPORT rejects a malformed .sat file with a message, no crash",
          "[issue473][blockimport][sat]") {
  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() / "gosurvey-satbad";
  fs::create_directories(dir);
  const fs::path bad = dir / "bad.sat";
  {
    std::ofstream f(bad, std::ios::binary);
    f << "700 0 1 0\nnot really 20 an ASM header\n1 1e-6 1e-10\nbody $-1 -1 $-1 $99 $-1 $-1 #\n";
  }
  AppCommandState st;
  std::vector<std::string> log;
  CHECK_FALSE(ImportCadBlocksFromPath(st, bad.u8string().c_str(), log));
  CHECK(st.blockDefs.empty());
  bool named = false;
  for (const std::string& l : log)
    if (l.find("BLOCKIMPORT") != std::string::npos)
      named = true;
  CHECK(named);
}

// Issue #475 increment 1 — INSERT insertion point is 3D, with Z from snap / typed X,Y,Z / ghost preview.
TEST_CASE("INSERT 3D insertion point stores Z and preview matches commit", "[issue475][block][insert][3d]") {
  AppCommandState st;
  CadBlockDefinition def;
  def.name = "TEST3D";
  def.units = CadDrawingInsUnitsName(st.drawingInsUnits);
  def.content.lines = {0.f, 0.f, 0.f, 1.f, 0.f, 0.f};
  st.blockDefs.push_back(def);
  std::vector<std::string> log;

  // --- 2D legacy path still yields Z=0 via the 2-arg overload ---
  {
    AppCommandState s2;
    s2.blockDefs = st.blockDefs;
    s2.drawingInsUnits = st.drawingInsUnits;
    StartInsertBlockCommand(s2, log);
    std::snprintf(s2.insertBlockName, sizeof(s2.insertBlockName), "TEST3D");
    s2.insertBlockSpecifyPoint = true;
    s2.insertBlockSpecifyScale = false;
    s2.insertBlockSpecifyRot = false;
    s2.insertBlockDialogOpen = false;
    s2.insertBlockPhase = AppCommandState::InsertBlockPhase::WaitInsertPoint;
    SubmitInsertBlockPick(s2, 10.f, 20.f, log);
    REQUIRE(s2.cadBlockRefs.size() == 1);
    CHECK(s2.cadBlockRefs[0].xf.z == Catch::Approx(0.f));
  }

  // --- 3D pick via the 4-arg overload ---
  StartInsertBlockCommand(st, log);
  std::snprintf(st.insertBlockName, sizeof(st.insertBlockName), "TEST3D");
  st.insertBlockSpecifyPoint = true;
  st.insertBlockSpecifyScale = false;
  st.insertBlockSpecifyRot = false;
  st.insertBlockDialogOpen = false;
  st.insertBlockPhase = AppCommandState::InsertBlockPhase::WaitInsertPoint;

  // Preview during WaitInsertPoint must be at the cursor Z, not at stored 0.
  CadBlockXform pv{};
  REQUIRE(CadBlockInsertPreviewXform(st, 10.f, 20.f, 5.f, &pv));
  CHECK(pv.x == Catch::Approx(10.f));
  CHECK(pv.y == Catch::Approx(20.f));
  CHECK(pv.z == Catch::Approx(5.f));

  SubmitInsertBlockPick(st, 10.f, 20.f, 5.f, log);
  REQUIRE(st.cadBlockRefs.size() == 1);
  CHECK(st.cadBlockRefs[0].xf.x == Catch::Approx(10.f));
  CHECK(st.cadBlockRefs[0].xf.y == Catch::Approx(20.f));
  CHECK(st.cadBlockRefs[0].xf.z == Catch::Approx(5.f));
  CHECK(st.cadBlockRefs[0].xf.z == Catch::Approx(pv.z));

  // --- Typed X,Y,Z via the command line (WCS) ---
  {
    AppCommandState s3;
    s3.blockDefs = st.blockDefs;
    s3.drawingInsUnits = st.drawingInsUnits;
    s3.cadBlockRefs.clear();
    StartInsertBlockCommand(s3, log);
    std::snprintf(s3.insertBlockName, sizeof(s3.insertBlockName), "TEST3D");
    s3.insertBlockSpecifyPoint = true;
    s3.insertBlockSpecifyScale = false;
    s3.insertBlockSpecifyRot = false;
    s3.insertBlockDialogOpen = false;
    s3.insertBlockPhase = AppCommandState::InsertBlockPhase::WaitInsertPoint;
    s3.active = AppCommandState::Kind::InsertBlock;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "30,40,7.5");
    ProcessCommandLineSubmit(buf, static_cast<int>(sizeof(buf)), s3, log);
    REQUIRE(s3.cadBlockRefs.size() == 1);
    CHECK(s3.cadBlockRefs[0].xf.z == Catch::Approx(7.5f).margin(1e-4));
  }

  // --- Relative @X,Y,Z from origin (WCS) ---
  {
    AppCommandState s4;
    s4.blockDefs = st.blockDefs;
    s4.drawingInsUnits = st.drawingInsUnits;
    s4.cadBlockRefs.clear();
    StartInsertBlockCommand(s4, log);
    std::snprintf(s4.insertBlockName, sizeof(s4.insertBlockName), "TEST3D");
    s4.insertBlockSpecifyPoint = true;
    s4.insertBlockSpecifyScale = false;
    s4.insertBlockSpecifyRot = false;
    s4.insertBlockDialogOpen = false;
    s4.insertBlockPhase = AppCommandState::InsertBlockPhase::WaitInsertPoint;
    s4.active = AppCommandState::Kind::InsertBlock;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "@5,0,3");
    ProcessCommandLineSubmit(buf, static_cast<int>(sizeof(buf)), s4, log);
    REQUIRE(s4.cadBlockRefs.size() == 1);
    CHECK(s4.cadBlockRefs[0].xf.x == Catch::Approx(5.f).margin(1e-4));
    CHECK(s4.cadBlockRefs[0].xf.y == Catch::Approx(0.f).margin(1e-4));
    CHECK(s4.cadBlockRefs[0].xf.z == Catch::Approx(3.f).margin(1e-4));
  }
}

TEST_CASE("INSERT 3D scale preview uses true 3D distance", "[issue475][block][insert][3d][scale]") {
  AppCommandState st;
  CadBlockDefinition def;
  def.name = "POST";
  def.units = CadDrawingInsUnitsName(st.drawingInsUnits);
  def.content.lines = {0.f, 0.f, 0.f, 0.f, 0.f, 1.f};
  st.blockDefs.push_back(def);
  std::vector<std::string> log;

  StartInsertBlockCommand(st, log);
  std::snprintf(st.insertBlockName, sizeof(st.insertBlockName), "POST");
  st.insertBlockSpecifyPoint = true;
  st.insertBlockSpecifyScale = true;
  st.insertBlockSpecifyRot = false;
  st.insertBlockUniformScale = true;
  st.insertBlockDialogOpen = false;
  st.insertBlockPhase = AppCommandState::InsertBlockPhase::WaitInsertPoint;

  SubmitInsertBlockPick(st, 0.f, 0.f, 0.f, log);
  REQUIRE(st.insertBlockPhase == AppCommandState::InsertBlockPhase::WaitScale);

  CadBlockXform pv;
  REQUIRE(CadBlockInsertPreviewXform(st, 0.f, 0.f, 3.f, &pv));
  CHECK(pv.sx == Catch::Approx(3.f));

  SubmitInsertBlockPick(st, 0.f, 0.f, 3.f, log);
  REQUIRE(st.cadBlockRefs.size() == 1);
  CHECK(st.cadBlockRefs[0].xf.sx == Catch::Approx(3.f));
}

TEST_CASE("INSERT dialog rotX and rotY apply on place", "[issue475][block][insert][orient]") {
  AppCommandState st;
  CadBlockDefinition def;
  def.name = "FIT";
  def.units = CadDrawingInsUnitsName(st.drawingInsUnits);
  def.content.lines = {0.f, 0.f, 0.f, 1.f, 0.f, 0.f};
  st.blockDefs.push_back(def);
  std::vector<std::string> log;

  StartInsertBlockCommand(st, log);
  std::snprintf(st.insertBlockName, sizeof(st.insertBlockName), "FIT");
  st.insertBlockSpecifyPoint = false;
  st.insertBlockSpecifyScale = false;
  st.insertBlockSpecifyRot = false;
  st.insertBlockSpecifyAlignFace = false;
  st.insertBlockRotXDeg = 90.f;
  st.insertBlockRotYDeg = 0.f;
  std::snprintf(st.insertBlockRotXBuf, sizeof(st.insertBlockRotXBuf), "90");
  CadBlocksCommitInsertDialog(st, log);
  REQUIRE(st.cadBlockRefs.size() == 1);
  CHECK(st.cadBlockRefs[0].xf.rotX == Catch::Approx(CadBlockRotDegToRad(90.f)).margin(1e-4));
}

TEST_CASE("INSERT ghost rubber includes solid block wireframe edges", "[issue475][block][insert][preview][solid]") {
  ucs::Ucs frame;
  brep::Solid box;
  brep::Problem why = brep::Problem::Ok;
  REQUIRE(brep::MakeBox(frame, 1.0, 1.0, 2.0, &box, &why));

  AppCommandState st;
  CadBlockDefinition def;
  def.name = "SOLIDGHOST";
  def.units = CadDrawingInsUnitsName(st.drawingInsUnits);
  def.content.solids.push_back(std::make_shared<const brep::Solid>(std::move(box)));
  st.blockDefs.push_back(def);
  std::snprintf(st.insertBlockName, sizeof(st.insertBlockName), "SOLIDGHOST");

  CadBlockXform xf;
  xf.x = 5.f;
  xf.y = 5.f;
  xf.z = 2.f;
  std::vector<float> rubber;
  AppendInsertBlockGhostRubber(st, xf, rubber);
  REQUIRE(rubber.size() >= 12u);
  bool sawElevatedZ = false;
  for (size_t i = 0; i + 2 < rubber.size(); i += 6) {
    if (rubber[i + 2] > 1.5f || rubber[i + 5] > 1.5f)
      sawElevatedZ = true;
  }
  CHECK(sawElevatedZ);
}

TEST_CASE("INSERT explode copies block solid into cadSolids", "[issue475][block][solid][explode]") {
  ucs::Ucs frame;
  brep::Solid box;
  brep::Problem why = brep::Problem::Ok;
  REQUIRE(brep::MakeBox(frame, 1.0, 1.0, 2.0, &box, &why));

  AppCommandState st;
  CadBlockDefinition def;
  def.name = "SOLIDBLK";
  def.content.solids.push_back(std::make_shared<const brep::Solid>(std::move(box)));
  st.blockDefs.push_back(def);

  CadBlockXform xf;
  xf.x = 3.f;
  xf.y = 4.f;
  xf.z = 5.f;
  std::vector<std::string> log;
  REQUIRE(CadBlockPlaceInsert(st, "SOLIDBLK", xf, true, log));
  CHECK(st.cadBlockRefs.empty());
  REQUIRE(st.cadSolids.size() == 1);
  const brep::Bounds bb = brep::ComputeBounds(*st.cadSolids[0]);
  REQUIRE(bb.valid);
  CHECK(bb.mn.z == Catch::Approx(5.0).margin(0.05));
}

// issue #493 / REQ-337 — drives the real two-phase SUBTRACT command flow (StartBooleanCommand /
// HandleBooleanTextInput), the same path a user's click-select-Enter-select-Enter takes, since the
// retry-on-refusal wiring itself lives in an internal-linkage CommitBoolean inside CadCommands.cpp
// and isn't reachable directly from another translation unit.
void RunSubtractCommand(AppCommandState& st, int minuendSolidIdx, int subtrahendSolidIdx,
                        std::vector<std::string>& log) {
  StartBooleanCommand(st, CadBooleanOp::Subtract, log);
  SelectedEntity minuend;
  minuend.type = SelectedEntity::Type::Solid;
  minuend.index = minuendSolidIdx;
  st.selection = {minuend};
  REQUIRE(HandleBooleanTextInput("", st, log));
  SelectedEntity subtrahend;
  subtrahend.type = SelectedEntity::Type::Solid;
  subtrahend.index = subtrahendSolidIdx;
  st.selection = {subtrahend};
  REQUIRE(HandleBooleanTextInput("", st, log));
}

TEST_CASE("SUBTRACT bores a stepped coaxial cylinder stack through a flange", "[issue493][boolean][brep]") {
  // A 10x10x4 flange (z 0..4) and a two-step shaft coaxial on Z: the wide step (r 1.5) spans
  // z[-1,2], the narrow step (r 1.0) continues z[2,6] — so the flange sees r 1.5 for z[0,2] and
  // r 1.0 for z[2,4], a genuine counterbore-shaped compound cutter, not a single primitive.
  AppCommandState st;
  brep::Problem why = brep::Problem::Ok;
  brep::Solid flange;
  REQUIRE(brep::MakeBox(ucs::Ucs{}, 10.0, 10.0, 4.0, &flange, &why));

  ucs::Ucs wideFrame;
  wideFrame.origin = brep::Vec3{0.0, 0.0, -1.0};
  brep::Solid wide;
  REQUIRE(brep::MakeCylinder(wideFrame, 1.5, 3.0, &wide, &why));  // z[-1,2]

  ucs::Ucs narrowFrame;
  narrowFrame.origin = brep::Vec3{0.0, 0.0, 2.0};
  brep::Solid narrow;
  REQUIRE(brep::MakeCylinder(narrowFrame, 1.0, 4.0, &narrow, &why));  // z[2,6]

  std::vector<brep::Solid> unioned;
  REQUIRE(brep::BooleanUnion(wide, narrow, &unioned, &why));
  REQUIRE(unioned.size() == 1);
  brep::Solid shaft = std::move(unioned[0]);

  // A single-cylinder cutter (TryGetCylinderInfo's own case) still fails the direct kernel call
  // the same way — confirms the fixture setup, not the new decomposition, produced this refusal.
  std::vector<brep::Solid> direct;
  REQUIRE_FALSE(brep::BooleanSubtract(flange, shaft, &direct, &why));
  CHECK(why == brep::Problem::BooleanCurvedFace);

  st.cadSolids.push_back(std::make_shared<const brep::Solid>(std::move(flange)));
  st.cadSolids.push_back(std::make_shared<const brep::Solid>(std::move(shaft)));

  std::vector<std::string> log;
  RunSubtractCommand(st, 0, 1, log);

  REQUIRE(st.cadSolids.size() == 1);
  REQUIRE(st.cadSolids[0]);
  CHECK(brep::Validate(*st.cadSolids[0]) == brep::Problem::Ok);
  // Removed: r1.5 across z[0,2] (2 ft) + r1.0 across z[2,4] (2 ft).
  const double removed = 3.141592653589793 * (1.5 * 1.5 * 2.0 + 1.0 * 1.0 * 2.0);
  const double vol = brep::ComputeMassProperties(*st.cadSolids[0]).volume;
  CHECK(vol == Catch::Approx(400.0 - removed).epsilon(1e-6));
}

TEST_CASE("SUBTRACT still refuses a widening coaxial composite cutter by name", "[issue493][boolean][brep]") {
  // Same coaxial two-cylinder union as the success case above, but with the radii swapped — narrow
  // (r 1.0) at the entry end, wide (r 1.5) continuing deeper. REQ-337's own scope boundary is a
  // NON-increasing radius sequence (the wide end faces the entry, a real counterbore); a widening
  // stack must still refuse by name, not silently misbehave or produce a wrong result.
  AppCommandState st;
  brep::Problem why = brep::Problem::Ok;
  brep::Solid flange;
  REQUIRE(brep::MakeBox(ucs::Ucs{}, 10.0, 10.0, 4.0, &flange, &why));

  ucs::Ucs aFrame;
  aFrame.origin = brep::Vec3{0.0, 0.0, -1.0};
  brep::Solid a;
  REQUIRE(brep::MakeCylinder(aFrame, 1.0, 3.0, &a, &why));  // z[-1,2], narrow

  ucs::Ucs bFrame;
  bFrame.origin = brep::Vec3{0.0, 0.0, 2.0};
  brep::Solid b;
  REQUIRE(brep::MakeCylinder(bFrame, 1.5, 4.0, &b, &why));  // z[2,6], wide — widening from a

  std::vector<brep::Solid> unioned;
  REQUIRE(brep::BooleanUnion(a, b, &unioned, &why));
  REQUIRE(unioned.size() == 1);

  st.cadSolids.push_back(std::make_shared<const brep::Solid>(std::move(flange)));
  st.cadSolids.push_back(std::make_shared<const brep::Solid>(std::move(unioned[0])));

  std::vector<std::string> log;
  RunSubtractCommand(st, 0, 1, log);

  // Refused: both solids are untouched (still 2 in the array, geometry unchanged).
  REQUIRE(st.cadSolids.size() == 2);
  bool refused = false;
  for (const std::string& line : log) {
    if (line.find("cannot combine these curved solids") != std::string::npos)
      refused = true;
  }
  CHECK(refused);
}


// issue #495 / REQ-338 increment 338a — the mirror image of REQ-337: the TARGET (not the cutter)
// is a coaxial stepped-cylinder stack, and the cutter is a single plain cylinder. Verified during
// #495 that `SubtractCircleThrough`/`TryBoreThroughDirect` already handles this correctly today —
// it doesn't special-case the two end faces at all, it just adds tunnel geometry between whichever
// two planar faces it finds, so an unrelated step boundary in between is never even consulted.
// These are regression tests locking that behaviour in, not a new code path.
TEST_CASE("SUBTRACT bores axially through a composite coaxial target (issue #495 338a)", "[issue495][boolean][brep]") {
  AppCommandState st;
  brep::Problem why = brep::Problem::Ok;

  ucs::Ucs wideFrame;
  wideFrame.origin = brep::Vec3{0.0, 0.0, 0.0};
  brep::Solid wide;
  REQUIRE(brep::MakeCylinder(wideFrame, 2.0, 4.0, &wide, &why));  // z[0,4] r2

  ucs::Ucs narrowFrame;
  narrowFrame.origin = brep::Vec3{0.0, 0.0, 4.0};
  brep::Solid narrow;
  REQUIRE(brep::MakeCylinder(narrowFrame, 1.0, 4.0, &narrow, &why));  // z[4,8] r1, narrower

  std::vector<brep::Solid> unioned;
  REQUIRE(brep::BooleanUnion(wide, narrow, &unioned, &why));
  REQUIRE(unioned.size() == 1);
  brep::Solid target = std::move(unioned[0]);
  REQUIRE(brep::Validate(target) == brep::Problem::Ok);

  // A short PRESSPULL-style nub cylinder near the middle, radius well under the narrow section's
  // r1.0 — this is the same short-cylinder-through-hole-fallback shape TryGetCylinderInfo already
  // recognises (see the "SUBTRACT short PRESSPULL cylinder" test in BrepTests.cpp).
  ucs::Ucs nubFrame;
  ucs::FromNormal(brep::Vec3{0.0, 0.0, 3.5}, brep::Vec3{0.0, 0.0, 1.0}, &nubFrame);
  brep::Solid nub;
  REQUIRE(brep::MakeCylinder(nubFrame, 0.5, 1.0, &nub, &why));

  st.cadSolids.push_back(std::make_shared<const brep::Solid>(std::move(target)));
  st.cadSolids.push_back(std::make_shared<const brep::Solid>(std::move(nub)));

  std::vector<std::string> log;
  RunSubtractCommand(st, 0, 1, log);

  REQUIRE(st.cadSolids.size() == 1);
  REQUIRE(st.cadSolids[0]);
  CHECK(brep::Validate(*st.cadSolids[0]) == brep::Problem::Ok);
  // Removed: a clean r0.5 through-bore, the full z[0,8] length of the composite target.
  const double removed = 3.141592653589793 * 0.5 * 0.5 * 8.0;
  const double wideVol = 3.141592653589793 * 2.0 * 2.0 * 4.0;
  const double narrowVol = 3.141592653589793 * 1.0 * 1.0 * 4.0;
  const double vol = brep::ComputeMassProperties(*st.cadSolids[0]).volume;
  CHECK(vol == Catch::Approx(wideVol + narrowVol - removed).epsilon(1e-6));
}

TEST_CASE("SUBTRACT still refuses when the bore is wider than the narrow section (issue #495 338a)",
         "[issue495][boolean][brep]") {
  // Same composite target as above, but the cutter radius (1.3) fits the wide section (r2) and
  // does NOT fit the narrow one (r1) — REQ-201: refused by name, never a silently wrong result.
  AppCommandState st;
  brep::Problem why = brep::Problem::Ok;

  ucs::Ucs wideFrame;
  wideFrame.origin = brep::Vec3{0.0, 0.0, 0.0};
  brep::Solid wide;
  REQUIRE(brep::MakeCylinder(wideFrame, 2.0, 4.0, &wide, &why));  // z[0,4] r2

  ucs::Ucs narrowFrame;
  narrowFrame.origin = brep::Vec3{0.0, 0.0, 4.0};
  brep::Solid narrow;
  REQUIRE(brep::MakeCylinder(narrowFrame, 1.0, 4.0, &narrow, &why));  // z[4,8] r1

  std::vector<brep::Solid> unioned;
  REQUIRE(brep::BooleanUnion(wide, narrow, &unioned, &why));
  REQUIRE(unioned.size() == 1);
  brep::Solid target = std::move(unioned[0]);

  ucs::Ucs nubFrame;
  ucs::FromNormal(brep::Vec3{0.0, 0.0, 3.5}, brep::Vec3{0.0, 0.0, 1.0}, &nubFrame);
  brep::Solid nub;
  REQUIRE(brep::MakeCylinder(nubFrame, 1.3, 1.0, &nub, &why));

  st.cadSolids.push_back(std::make_shared<const brep::Solid>(std::move(target)));
  st.cadSolids.push_back(std::make_shared<const brep::Solid>(std::move(nub)));

  std::vector<std::string> log;
  RunSubtractCommand(st, 0, 1, log);

  // Refused: both solids untouched.
  REQUIRE(st.cadSolids.size() == 2);
  bool refused = false;
  for (const std::string& line : log) {
    if (line.find("cannot combine these curved solids") != std::string::npos)
      refused = true;
  }
  CHECK(refused);
}

// issue #495 / REQ-338 increment 338b — sequential same-command cutters. Investigation found
// CommitBoolean's existing per-piece SUBTRACT loop (each subtrahend applied to the result of the
// previous one, with the TryGetCylinderInfo/SubtractCircleThrough short-cylinder fallback already
// in place from REQ-337) already handles this correctly: a later cutter lands fine in a target that
// already carries an earlier cut nearby, including touching and overlapping holes. These are
// regression tests locking that behaviour in, not a new code path.
TEST_CASE("SUBTRACT cuts a bolt-circle of holes in one command (issue #495 338b)", "[issue495][boolean][brep]") {
  AppCommandState st;
  brep::Problem why = brep::Problem::Ok;
  brep::Solid flange;
  REQUIRE(brep::MakeBox(ucs::Ucs{}, 10.0, 10.0, 2.0, &flange, &why));
  st.cadSolids.push_back(std::make_shared<const brep::Solid>(std::move(flange)));

  std::vector<int> subIdx;
  const double pi = 3.141592653589793;
  const int N = 8;
  for (int i = 0; i < N; ++i) {
    const double ang = 2.0 * pi * static_cast<double>(i) / N;
    const double cx = 3.5 * std::cos(ang);
    const double cy = 3.5 * std::sin(ang);
    ucs::Ucs cf;
    ucs::FromNormal(brep::Vec3{cx, cy, 1.0}, brep::Vec3{0, 0, 1}, &cf);
    brep::Solid hole;
    REQUIRE(brep::MakeCylinder(cf, 0.4, 1.0, &hole, &why));  // short PRESSPULL-style nub
    st.cadSolids.push_back(std::make_shared<const brep::Solid>(std::move(hole)));
    subIdx.push_back(static_cast<int>(st.cadSolids.size()) - 1);
  }

  std::vector<std::string> log;
  StartBooleanCommand(st, CadBooleanOp::Subtract, log);
  SelectedEntity minuend;
  minuend.type = SelectedEntity::Type::Solid;
  minuend.index = 0;
  st.selection = {minuend};
  REQUIRE(HandleBooleanTextInput("", st, log));
  st.selection.clear();
  for (int idx : subIdx) {
    SelectedEntity e;
    e.type = SelectedEntity::Type::Solid;
    e.index = idx;
    st.selection.push_back(e);
  }
  REQUIRE(HandleBooleanTextInput("", st, log));

  REQUIRE(st.cadSolids.size() == 1);
  REQUIRE(st.cadSolids[0]);
  CHECK(brep::Validate(*st.cadSolids[0]) == brep::Problem::Ok);
  const double removed = pi * 0.4 * 0.4 * 2.0 * N;
  const double vol = brep::ComputeMassProperties(*st.cadSolids[0]).volume;
  CHECK(vol == Catch::Approx(200.0 - removed).epsilon(1e-6));
}

TEST_CASE("SUBTRACT handles touching and overlapping holes in one command (issue #495 338b)",
         "[issue495][boolean][brep]") {
  brep::Problem why = brep::Problem::Ok;
  brep::Solid flange;
  REQUIRE(brep::MakeBox(ucs::Ucs{}, 10.0, 10.0, 2.0, &flange, &why));

  // Hole 1, then hole 2 whose disk overlaps hole 1's (0.6 apart, radius 0.5 each) — the merged-
  // cavity case, landing the second cut directly against the first cut's own inward face.
  brep::Solid afterHole1;
  REQUIRE(brep::SubtractCircleThrough(flange, brep::Vec3{-0.3, 0, 1.0}, brep::Vec3{0, 0, 1}, 0.5,
                                     &afterHole1, &why));
  brep::Solid afterHole2;
  const bool ok2 = brep::SubtractCircleThrough(afterHole1, brep::Vec3{0.3, 0, 1.0}, brep::Vec3{0, 0, 1},
                                              0.5, &afterHole2, &why);
  INFO("why=" << brep::ProblemText(why));
  REQUIRE(ok2);
  CHECK(brep::Validate(afterHole2) == brep::Problem::Ok);
}

TEST_CASE("SUBTRACT with one unresolvable cutter refuses the whole command (issue #495 338b)",
         "[issue495][boolean][brep]") {
  AppCommandState st;
  brep::Problem why = brep::Problem::Ok;
  brep::Solid flange;
  REQUIRE(brep::MakeBox(ucs::Ucs{}, 10.0, 10.0, 2.0, &flange, &why));
  st.cadSolids.push_back(std::make_shared<const brep::Solid>(std::move(flange)));
  std::vector<int> subIdx;

  ucs::Ucs cf1;
  ucs::FromNormal(brep::Vec3{-2.5, -2.5, 1.0}, brep::Vec3{0, 0, 1}, &cf1);
  brep::Solid hole1;
  REQUIRE(brep::MakeCylinder(cf1, 0.4, 1.0, &hole1, &why));
  st.cadSolids.push_back(std::make_shared<const brep::Solid>(std::move(hole1)));
  subIdx.push_back(static_cast<int>(st.cadSolids.size()) - 1);

  ucs::Ucs cf2;
  ucs::FromNormal(brep::Vec3{2.5, -2.5, 1.0}, brep::Vec3{0, 0, 1}, &cf2);
  brep::Solid hole2;
  REQUIRE(brep::MakeCylinder(cf2, 0.4, 1.0, &hole2, &why));
  st.cadSolids.push_back(std::make_shared<const brep::Solid>(std::move(hole2)));
  subIdx.push_back(static_cast<int>(st.cadSolids.size()) - 1);

  // Third cutter: a WIDENING coaxial 2-cylinder stack — REQ-337's own scope boundary, genuinely
  // unresolvable. The whole multi-cutter SUBTRACT must refuse, not apply the first two and choke
  // on the third (REQ-201: named refusal, document untouched).
  ucs::Ucs aFrame;
  aFrame.origin = brep::Vec3{0.0, 2.5, -1.0};
  brep::Solid a;
  REQUIRE(brep::MakeCylinder(aFrame, 1.0, 3.0, &a, &why));
  ucs::Ucs bFrame;
  bFrame.origin = brep::Vec3{0.0, 2.5, 2.0};
  brep::Solid b;
  REQUIRE(brep::MakeCylinder(bFrame, 1.5, 4.0, &b, &why));
  std::vector<brep::Solid> unioned;
  REQUIRE(brep::BooleanUnion(a, b, &unioned, &why));
  st.cadSolids.push_back(std::make_shared<const brep::Solid>(std::move(unioned[0])));
  subIdx.push_back(static_cast<int>(st.cadSolids.size()) - 1);

  const size_t before = st.cadSolids.size();
  std::vector<std::string> log;
  StartBooleanCommand(st, CadBooleanOp::Subtract, log);
  SelectedEntity minuend;
  minuend.type = SelectedEntity::Type::Solid;
  minuend.index = 0;
  st.selection = {minuend};
  REQUIRE(HandleBooleanTextInput("", st, log));
  st.selection.clear();
  for (int idx : subIdx) {
    SelectedEntity e;
    e.type = SelectedEntity::Type::Solid;
    e.index = idx;
    st.selection.push_back(e);
  }
  REQUIRE(HandleBooleanTextInput("", st, log));

  CHECK(st.cadSolids.size() == before);
  bool refused = false;
  for (const std::string& line : log)
    if (line.find("cannot combine these curved solids") != std::string::npos)
      refused = true;
  CHECK(refused);
}

// issue #495 / REQ-338 increment 338c — general multi-solid folding. The concrete gap: `FoldBoolean`
// (`CadCommands.cpp`) already retries a multi-piece UNION selection against every accumulated piece,
// but each retry called the kernel's `BooleanUnion` pairwise, and the kernel itself only recognised
// TWO bare single-primitive cylinders as "coaxial" — extending an already-built multi-segment stack
// with one more coaxial piece fell straight through to `Problem::BooleanCurvedFace`, even though
// each individual piece was, on its own, a shape REQ-314 already handles. Fixed in `brep.cpp` by
// generalising `TryBooleanCoaxialCylinders`'s own single-interval UNION merge (`ExtractCoaxialStack`
// + `TryBooleanCoaxialStackUnion`, reusing the existing `BuildCoaxialStack`) to N segments per side —
// closed-form 1D interval arithmetic along the shared axis, never a general solid-to-solid stitch.
TEST_CASE("UNION extends an existing coaxial stack with a third cylinder (issue #495 338c)",
         "[issue495][boolean][brep]") {
  brep::Problem why = brep::Problem::Ok;
  ucs::Ucs fA;
  fA.origin = brep::Vec3{0, 0, 0};
  brep::Solid a;
  REQUIRE(brep::MakeCylinder(fA, 2.0, 4.0, &a, &why));  // z[0,4] r2
  ucs::Ucs fB;
  fB.origin = brep::Vec3{0, 0, 4};
  brep::Solid b;
  REQUIRE(brep::MakeCylinder(fB, 1.0, 4.0, &b, &why));  // z[4,8] r1

  std::vector<brep::Solid> u1;
  REQUIRE(brep::BooleanUnion(a, b, &u1, &why));
  REQUIRE(u1.size() == 1);
  brep::Solid ab = std::move(u1[0]);  // a genuine 2-segment stack, no longer a single primitive

  ucs::Ucs fC;
  fC.origin = brep::Vec3{0, 0, 8};
  brep::Solid c;
  REQUIRE(brep::MakeCylinder(fC, 2.0, 2.0, &c, &why));  // z[8,10] r2 — widens back out

  std::vector<brep::Solid> u2;
  const bool ok = brep::BooleanUnion(ab, c, &u2, &why);
  INFO("why=" << brep::ProblemText(why));
  REQUIRE(ok);
  REQUIRE(u2.size() == 1);
  CHECK(brep::Validate(u2[0]) == brep::Problem::Ok);
  const double pi = 3.141592653589793;
  const double vol = brep::ComputeMassProperties(u2[0]).volume;
  const double expected = pi * 2 * 2 * 4 + pi * 1 * 1 * 4 + pi * 2 * 2 * 2;
  CHECK(vol == Catch::Approx(expected).epsilon(1e-6));
}

TEST_CASE("UNION of a coaxial stack with a genuinely disjoint cylinder stays two pieces "
         "(issue #495 338c)",
         "[issue495][boolean][brep]") {
  brep::Problem why = brep::Problem::Ok;
  ucs::Ucs fA;
  fA.origin = brep::Vec3{0, 0, 0};
  brep::Solid a;
  REQUIRE(brep::MakeCylinder(fA, 2.0, 4.0, &a, &why));
  ucs::Ucs fB;
  fB.origin = brep::Vec3{0, 0, 4};
  brep::Solid b;
  REQUIRE(brep::MakeCylinder(fB, 1.0, 4.0, &b, &why));
  std::vector<brep::Solid> u1;
  REQUIRE(brep::BooleanUnion(a, b, &u1, &why));
  brep::Solid stack = std::move(u1[0]);

  ucs::Ucs fC;
  fC.origin = brep::Vec3{0, 0, 20};  // far away, no touch
  brep::Solid c;
  REQUIRE(brep::MakeCylinder(fC, 2.0, 2.0, &c, &why));

  std::vector<brep::Solid> u2;
  const bool ok = brep::BooleanUnion(stack, c, &u2, &why);
  INFO("why=" << brep::ProblemText(why));
  REQUIRE(ok);
  CHECK(u2.size() == 2);  // disjoint, not silently merged or dropped (REQ-201)
}

TEST_CASE("UNION command folds a three-piece coaxial selection into one solid (issue #495 338c)",
         "[issue495][boolean][brep]") {
  AppCommandState st;
  brep::Problem why = brep::Problem::Ok;
  ucs::Ucs fA;
  fA.origin = brep::Vec3{0, 0, 0};
  brep::Solid a;
  REQUIRE(brep::MakeCylinder(fA, 2.0, 4.0, &a, &why));
  ucs::Ucs fB;
  fB.origin = brep::Vec3{0, 0, 4};
  brep::Solid b;
  REQUIRE(brep::MakeCylinder(fB, 1.0, 4.0, &b, &why));
  ucs::Ucs fC;
  fC.origin = brep::Vec3{0, 0, 8};
  brep::Solid c;
  REQUIRE(brep::MakeCylinder(fC, 2.0, 2.0, &c, &why));

  st.cadSolids.push_back(std::make_shared<const brep::Solid>(std::move(a)));
  st.cadSolids.push_back(std::make_shared<const brep::Solid>(std::move(b)));
  st.cadSolids.push_back(std::make_shared<const brep::Solid>(std::move(c)));
  SelectedEntity e0, e1, e2;
  e0.type = e1.type = e2.type = SelectedEntity::Type::Solid;
  e0.index = 0;
  e1.index = 1;
  e2.index = 2;
  st.selection = {e0, e1, e2};

  std::vector<std::string> log;
  StartBooleanCommand(st, CadBooleanOp::Union, log);  // pre-selected 3 solids commits immediately
  for (auto& l : log) UNSCOPED_INFO(l);

  REQUIRE(st.cadSolids.size() == 1);
  REQUIRE(st.cadSolids[0]);
  CHECK(brep::Validate(*st.cadSolids[0]) == brep::Problem::Ok);
}

// issue #495 / REQ-338 increment 338d-1 — coaxial composite stacks may include a conical (tapered)
// segment. Found live testing 338c: a shaft with a straight-taper transition (a LOFT-built cone
// unioned into an otherwise-cylindrical stack) still refused with Problem::BooleanCurvedFace because
// ExtractCoaxialStack tolerated only Plane/Cylinder faces. Fixed by widening ExtractCoaxialStack /
// BuildCoaxialStack / TryBooleanCoaxialStackUnion to carry a per-band (radius-at-z0, radius-at-z1)
// pair instead of one constant radius, building a Cone wall when the pair differs (Cylinder,
// unchanged, when it doesn't) — no new surface or curve type, ADR-045's kernel already has an exact
// closed-form Cone.
TEST_CASE("UNION merges a coaxial cylinder with a coaxial cone into one tapered stack "
         "(issue #495 338d-1)",
         "[issue495][boolean][brep]") {
  brep::Problem why = brep::Problem::Ok;
  ucs::Ucs fA;
  fA.origin = brep::Vec3{0, 0, 0};
  brep::Solid a;
  REQUIRE(brep::MakeCylinder(fA, 2.0, 4.0, &a, &why));  // z[0,4] r2

  ucs::Ucs fB;
  fB.origin = brep::Vec3{0, 0, 4};
  brep::Solid b;
  REQUIRE(brep::MakeCone(fB, 2.0, 1.0, 3.0, &b, &why));  // z[4,7] tapers r2 -> r1

  std::vector<brep::Solid> u;
  const bool ok = brep::BooleanUnion(a, b, &u, &why);
  INFO("why=" << brep::ProblemText(why));
  REQUIRE(ok);
  REQUIRE(u.size() == 1);
  CHECK(brep::Validate(u[0]) == brep::Problem::Ok);

  const double pi = 3.141592653589793;
  const double cylVol = pi * 2.0 * 2.0 * 4.0;
  const double coneVol = (pi * 3.0 / 3.0) * (2.0 * 2.0 + 2.0 * 1.0 + 1.0 * 1.0);  // frustum
  const double vol = brep::ComputeMassProperties(u[0]).volume;
  CHECK(vol == Catch::Approx(cylVol + coneVol).epsilon(1e-6));
}

TEST_CASE("UNION extends a cylinder+cone stack with a further coaxial cylinder "
         "(issue #495 338d-1)",
         "[issue495][boolean][brep]") {
  brep::Problem why = brep::Problem::Ok;
  ucs::Ucs fA;
  fA.origin = brep::Vec3{0, 0, 0};
  brep::Solid a;
  REQUIRE(brep::MakeCylinder(fA, 2.0, 4.0, &a, &why));  // z[0,4] r2
  ucs::Ucs fB;
  fB.origin = brep::Vec3{0, 0, 4};
  brep::Solid b;
  REQUIRE(brep::MakeCone(fB, 2.0, 1.0, 3.0, &b, &why));  // z[4,7] r2 -> r1

  std::vector<brep::Solid> u1;
  REQUIRE(brep::BooleanUnion(a, b, &u1, &why));
  REQUIRE(u1.size() == 1);
  brep::Solid stack = std::move(u1[0]);  // cylinder + cone, no longer a single primitive

  ucs::Ucs fC;
  fC.origin = brep::Vec3{0, 0, 7};
  brep::Solid c;
  REQUIRE(brep::MakeCylinder(fC, 1.0, 2.0, &c, &why));  // z[7,9] r1 — matches the cone's narrow end

  std::vector<brep::Solid> u2;
  const bool ok = brep::BooleanUnion(stack, c, &u2, &why);
  INFO("why=" << brep::ProblemText(why));
  REQUIRE(ok);
  REQUIRE(u2.size() == 1);
  CHECK(brep::Validate(u2[0]) == brep::Problem::Ok);

  const double pi = 3.141592653589793;
  const double cylVol = pi * 2.0 * 2.0 * 4.0;
  const double coneVol = (pi * 3.0 / 3.0) * (2.0 * 2.0 + 2.0 * 1.0 + 1.0 * 1.0);
  const double tailVol = pi * 1.0 * 1.0 * 2.0;
  const double vol = brep::ComputeMassProperties(u2[0]).volume;
  CHECK(vol == Catch::Approx(cylVol + coneVol + tailVol).epsilon(1e-6));
}

TEST_CASE("SUBTRACT bores axially through a composite target that includes a tapered segment "
         "(issue #495 338d-1)",
         "[issue495][boolean][brep]") {
  // 338a's own reasoning (SubtractCircleThrough/TryBoreThroughDirect never special-cases anything
  // but the two outer planar faces) should already cover a target with a cone segment in the middle,
  // once that target can even be BUILT via UNION (338d-1's own fix) — this locks that in.
  AppCommandState st;
  brep::Problem why = brep::Problem::Ok;

  ucs::Ucs fA;
  fA.origin = brep::Vec3{0.0, 0.0, 0.0};
  brep::Solid a;
  REQUIRE(brep::MakeCylinder(fA, 2.0, 4.0, &a, &why));  // z[0,4] r2
  ucs::Ucs fB;
  fB.origin = brep::Vec3{0.0, 0.0, 4.0};
  brep::Solid b;
  REQUIRE(brep::MakeCone(fB, 2.0, 1.0, 3.0, &b, &why));  // z[4,7] r2 -> r1

  std::vector<brep::Solid> unioned;
  REQUIRE(brep::BooleanUnion(a, b, &unioned, &why));
  REQUIRE(unioned.size() == 1);
  brep::Solid target = std::move(unioned[0]);
  REQUIRE(brep::Validate(target) == brep::Problem::Ok);

  // A drill well under the narrowest radius anywhere on the taper (r1 at the cone's top).
  ucs::Ucs nubFrame;
  ucs::FromNormal(brep::Vec3{0.0, 0.0, 3.5}, brep::Vec3{0.0, 0.0, 1.0}, &nubFrame);
  brep::Solid nub;
  REQUIRE(brep::MakeCylinder(nubFrame, 0.5, 1.0, &nub, &why));

  st.cadSolids.push_back(std::make_shared<const brep::Solid>(std::move(target)));
  st.cadSolids.push_back(std::make_shared<const brep::Solid>(std::move(nub)));

  std::vector<std::string> log;
  RunSubtractCommand(st, 0, 1, log);
  for (auto& l : log) UNSCOPED_INFO(l);

  REQUIRE(st.cadSolids.size() == 1);
  REQUIRE(st.cadSolids[0]);
  CHECK(brep::Validate(*st.cadSolids[0]) == brep::Problem::Ok);
}

TEST_CASE("SUBTRACT still refuses when the bore is wider than the cone's narrow end "
         "(issue #495 338d-1)",
         "[issue495][boolean][brep]") {
  // Same tapered target as above, but the cutter (r1.3) fits the wide cylinder (r2) and does NOT
  // fit the cone's narrow end (r1) — REQ-201: refused by name, never a silently wrong result.
  AppCommandState st;
  brep::Problem why = brep::Problem::Ok;

  ucs::Ucs fA;
  fA.origin = brep::Vec3{0.0, 0.0, 0.0};
  brep::Solid a;
  REQUIRE(brep::MakeCylinder(fA, 2.0, 4.0, &a, &why));  // z[0,4] r2
  ucs::Ucs fB;
  fB.origin = brep::Vec3{0.0, 0.0, 4.0};
  brep::Solid b;
  REQUIRE(brep::MakeCone(fB, 2.0, 1.0, 3.0, &b, &why));  // z[4,7] r2 -> r1

  std::vector<brep::Solid> unioned;
  REQUIRE(brep::BooleanUnion(a, b, &unioned, &why));
  REQUIRE(unioned.size() == 1);
  brep::Solid target = std::move(unioned[0]);

  ucs::Ucs nubFrame;
  ucs::FromNormal(brep::Vec3{0.0, 0.0, 3.5}, brep::Vec3{0.0, 0.0, 1.0}, &nubFrame);
  brep::Solid nub;
  REQUIRE(brep::MakeCylinder(nubFrame, 1.3, 1.0, &nub, &why));

  st.cadSolids.push_back(std::make_shared<const brep::Solid>(std::move(target)));
  st.cadSolids.push_back(std::make_shared<const brep::Solid>(std::move(nub)));

  std::vector<std::string> log;
  RunSubtractCommand(st, 0, 1, log);

  // Refused: both solids untouched.
  REQUIRE(st.cadSolids.size() == 2);
  bool refused = false;
  for (const std::string& line : log) {
    if (line.find("cannot combine these curved solids") != std::string::npos)
      refused = true;
  }
  CHECK(refused);
}

TEST_CASE("PickClosestCadEntity finds a block reference made entirely of solid geometry",
          "[issue496][block][solid][pick]") {
  // A piping fitting block whose content is a 3D solid and nothing else (no lines/circles) — the
  // shape most of the real fitting library actually has. Selection previously fell back to testing
  // only the block's own insertion-origin point, so the block was unclickable anywhere across its
  // visible body.
  AppCommandState st;
  ucs::Ucs frame;
  brep::Solid box;
  brep::Problem why = brep::Problem::Ok;
  REQUIRE(brep::MakeBox(frame, 2.0, 2.0, 4.0, &box, &why));  // x,y in [-1,1], z in [0,4]

  CadBlockDefinition def;
  def.name = "FIT";
  def.content.solids.push_back(std::make_shared<const brep::Solid>(std::move(box)));
  st.blockDefs.push_back(def);

  CadBlockRef r;
  r.defName = "FIT";
  st.cadBlockRefs.push_back(r);
  st.cadBlockRefAttrs.push_back(EntityAttributes{});

  // On the box's top-face edge (y=1, x in [-1,1], z=4) but far from the insertion origin (0,0) —
  // exactly the point the old origin-only fallback would miss by a wide margin.
  SelectedEntity hit{};
  float distSq = 0.f;
  REQUIRE(PickClosestCadEntity(st, 0.0, 1.0, 0.01f, &hit, &distSq));
  CHECK(hit.type == SelectedEntity::Type::BlockRef);
  CHECK(hit.index == 0);
}

// --- PIPECATALOG catalog lookup (issue #486 increment B4 / REQ-345) ----------------------------

namespace {
CadBlockDefinition MakeFittingDef(const std::string& name, CadPipePartType partType,
                                  const std::string& nominalSize, CadPipePressureClass pressureClass) {
  CadBlockDefinition def;
  def.name = name;
  def.partType = partType;
  def.nominalSize = nominalSize;
  def.pressureClass = pressureClass;
  return def;
}
} // namespace

TEST_CASE("CadPipeCatalogFind matches an already-imported fitting by size, class and type",
          "[issue486][pipecatalog]") {
  AppCommandState st;
  st.blockDefs.push_back(
      MakeFittingDef("ELBOW90-4IN-CS150", CadPipePartType::Elbow90, "4in", CadPipePressureClass::CS150));

  std::string name;
  std::vector<std::string> log;
  REQUIRE(CadPipeCatalogFind(st, CadPipePartType::Elbow90, "4in", CadPipePressureClass::CS150, &name, log));
  CHECK(name == "ELBOW90-4IN-CS150");
}

TEST_CASE("CadPipeCatalogFind refuses with a named reason when no part matches",
          "[issue486][pipecatalog]") {
  AppCommandState st;
  st.blockDefs.push_back(
      MakeFittingDef("ELBOW90-4IN-CS150", CadPipePartType::Elbow90, "4in", CadPipePressureClass::CS150));

  std::string name;
  std::vector<std::string> log;
  CHECK_FALSE(CadPipeCatalogFind(st, CadPipePartType::Tee, "4in", CadPipePressureClass::CS150, &name, log));
  CHECK(name.empty());
  REQUIRE_FALSE(log.empty());
  CHECK(log.back().find("no tee found") != std::string::npos);
}

TEST_CASE("CadPipeCatalogFind requires a part type and a nominal size", "[issue486][pipecatalog]") {
  AppCommandState st;
  std::string name;
  std::vector<std::string> log;
  CHECK_FALSE(CadPipeCatalogFind(st, CadPipePartType::None, "4in", CadPipePressureClass::None, &name, log));
  CHECK_FALSE(CadPipeCatalogFind(st, CadPipePartType::Elbow90, "", CadPipePressureClass::None, &name, log));
}

TEST_CASE("A requested class falls back to a class-agnostic part when no exact-classed part exists",
          "[issue486][pipecatalog]") {
  AppCommandState st;
  st.blockDefs.push_back(
      MakeFittingDef("VALVE-2IN", CadPipePartType::Valve, "2in", CadPipePressureClass::None));

  std::string name;
  std::vector<std::string> log;
  REQUIRE(CadPipeCatalogFind(st, CadPipePartType::Valve, "2in", CadPipePressureClass::CS300, &name, log));
  CHECK(name == "VALVE-2IN");
}

TEST_CASE("An exact-classed part is preferred over a class-agnostic one", "[issue486][pipecatalog]") {
  AppCommandState st;
  st.blockDefs.push_back(
      MakeFittingDef("VALVE-2IN-ANY", CadPipePartType::Valve, "2in", CadPipePressureClass::None));
  st.blockDefs.push_back(
      MakeFittingDef("VALVE-2IN-CS300", CadPipePartType::Valve, "2in", CadPipePressureClass::CS300));

  std::string name;
  std::vector<std::string> log;
  REQUIRE(CadPipeCatalogFind(st, CadPipePartType::Valve, "2in", CadPipePressureClass::CS300, &name, log));
  CHECK(name == "VALVE-2IN-CS300");
}

TEST_CASE("A requested class with no restriction (None) matches any classed part, refusing only "
          "on ambiguity",
          "[issue486][pipecatalog]") {
  AppCommandState st;
  st.blockDefs.push_back(
      MakeFittingDef("FLANGE-6IN-CS150", CadPipePartType::Flange, "6in", CadPipePressureClass::CS150));

  std::string name;
  std::vector<std::string> log;
  REQUIRE(CadPipeCatalogFind(st, CadPipePartType::Flange, "6in", CadPipePressureClass::None, &name, log));
  CHECK(name == "FLANGE-6IN-CS150");
}

TEST_CASE("CadPipeCatalogFind refuses as ambiguous when two parts tie on size/class/type",
          "[issue486][pipecatalog]") {
  AppCommandState st;
  st.blockDefs.push_back(
      MakeFittingDef("ELBOW90-4IN-A", CadPipePartType::Elbow90, "4in", CadPipePressureClass::CS150));
  st.blockDefs.push_back(
      MakeFittingDef("ELBOW90-4IN-B", CadPipePartType::Elbow90, "4in", CadPipePressureClass::CS150));

  std::string name;
  std::vector<std::string> log;
  CHECK_FALSE(CadPipeCatalogFind(st, CadPipePartType::Elbow90, "4in", CadPipePressureClass::CS150, &name, log));
  CHECK(name.empty());
  REQUIRE_FALSE(log.empty());
  CHECK(log.back().find("ambiguous") != std::string::npos);
}

TEST_CASE("A non-fitting block definition (partType None) is never offered as a catalog match",
          "[issue486][pipecatalog]") {
  AppCommandState st;
  CadBlockDefinition ordinary;
  ordinary.name = "4in";  // adversarial: name collides with the nominal size string itself
  st.blockDefs.push_back(ordinary);

  std::string name;
  std::vector<std::string> log;
  CHECK_FALSE(CadPipeCatalogFind(st, CadPipePartType::Elbow90, "4in", CadPipePressureClass::None, &name, log));
}

TEST_CASE("PIPECATALOG command parses part type, size and optional class", "[issue486][pipecatalog][command]") {
  AppCommandState st;
  st.blockDefs.push_back(
      MakeFittingDef("TEE-3IN-CS300", CadPipePartType::Tee, "3in", CadPipePressureClass::CS300));

  std::vector<std::string> log;
  std::istringstream args("tee 3in CS300");
  REQUIRE(CadBlocksTryIdleCommand(st, "pipecatalog", args, log));
  REQUIRE_FALSE(log.empty());
  CHECK(log.back().find("TEE-3IN-CS300") != std::string::npos);
}

TEST_CASE("PIPECATALOG command refuses an unknown part type or pressure class",
          "[issue486][pipecatalog][command]") {
  AppCommandState st;
  std::vector<std::string> log;
  std::istringstream badType("bogus 4in");
  REQUIRE(CadBlocksTryIdleCommand(st, "pipecatalog", badType, log));
  CHECK(log.back().find("unknown part type") != std::string::npos);

  log.clear();
  std::istringstream badClass("elbow-90 4in CS999");
  REQUIRE(CadBlocksTryIdleCommand(st, "pipecatalog", badClass, log));
  CHECK(log.back().find("unknown pressure class") != std::string::npos);
}
