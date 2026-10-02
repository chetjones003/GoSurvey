#include "DxfIo.hpp"
#include "DxfColors.hpp"
#include "DwgIo.hpp"
#include "GsIo.hpp"
#include "LibreDwg.hpp"
#include "LibreDwgCad.hpp"

#include "CadCommands.hpp"
#include "CadCoordinateFrame.hpp"
#include "CadField.hpp"
#include "CadDimStroke.hpp"
#include "SurveyPoints.hpp"
#include "io/SurveyCsv.hpp"
#include "util/ucs.hpp"
#include "util/brep.hpp"
#include "util/cadpiperun.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <imgui.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#if defined(__cplusplus) && !defined(restrict)
#define restrict
#endif
extern "C" {
#include <dwg.h>
#include <dwg_api.h>
}

namespace {

struct ScratchDir {
  std::filesystem::path path;
  explicit ScratchDir(const char* tag) {
    static int counter = 0;
    path = std::filesystem::temp_directory_path() /
           ("gosurvey-cadio-" + std::string(tag) + "-" + std::to_string(++counter));
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
    std::filesystem::create_directories(path, ec);
  }
  ~ScratchDir() {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
  }
};

void OneLine(AppCommandState& st) {
  st.userLinesFlat = {0.f, 0.f, 0.f, 10.f, 0.f, 0.f};
  st.userLineAttrs = {EntityAttributes{}};
}

bool LogContains(const std::vector<std::string>& log, std::string_view needle) {
  for (const std::string& line : log) {
    if (line.find(needle) != std::string::npos)
      return true;
  }
  return false;
}

bool EedCode0Equals(const Dwg_Eed_Data* data, const char* literal) {
  if (data == nullptr || data->code != 0 || literal == nullptr)
    return false;
  std::string s;
  if (data->u.eed_0.is_tu != 0)
    s = libredwgcad_detail::DecodeDwgString(data->u.eed_0.string, true);
  else {
    const unsigned short len = data->u.eed_0.length;
    if (len == 0)
      return false;
    s.assign(reinterpret_cast<const char*>(data->u.eed_0.string), len);
  }
  return s == literal;
}

void StripGosurveyAnnotativeEed(Dwg_Data& dwg) {
  const BITCODE_H app = dwg_find_tablehandle(&dwg, "GOSURVEY", "APPID");
  if (app == nullptr)
    return;
  const BITCODE_RLL gosRef = app->absolute_ref;
  for (unsigned oi = 0; oi < dwg.num_objects; ++oi) {
    Dwg_Object& obj = dwg.object[oi];
    if (obj.supertype != DWG_SUPERTYPE_ENTITY || obj.tio.entity == nullptr)
      continue;
    Dwg_Object_Entity* ent = obj.tio.entity;
    if (ent->eed == nullptr || ent->num_eed == 0)
      continue;
    if (ent->eed[0].handle.value != gosRef)
      continue;
    if (!EedCode0Equals(ent->eed[0].data, "annotative"))
      continue;
    free(ent->eed[0].data);
    ent->eed[0].data = nullptr;
    if (ent->num_eed == 1) {
      free(ent->eed);
      ent->eed = nullptr;
      ent->num_eed = 0;
      continue;
    }
    for (BITCODE_BL i = 1; i < ent->num_eed; ++i)
      ent->eed[i - 1] = ent->eed[i];
    ent->num_eed -= 1;
  }
}

bool DwgHasAcadAnnotativeDataEed(const Dwg_Data& dwg) {
  for (unsigned oi = 0; oi < dwg.num_objects; ++oi) {
    const Dwg_Object& obj = dwg.object[oi];
    if (obj.supertype != DWG_SUPERTYPE_ENTITY || obj.tio.entity == nullptr)
      continue;
    const Dwg_Object_Entity* ent = obj.tio.entity;
    if (ent->eed == nullptr)
      continue;
    for (BITCODE_BL i = 0; i < ent->num_eed; ++i) {
      if (EedCode0Equals(ent->eed[i].data, "AnnotativeData"))
        return true;
    }
  }
  return false;
}

// Survey-point labels are measured through ImGui::GetFont() while a point is placed/imported
// (EnsureSurveyPointLabelMtext) — same fixture as GsMigrateLegacyBreaklineTests.cpp (ADR-031 (c')).
struct HeadlessImGuiScope {
  HeadlessImGuiScope() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1920.f, 1080.f);
    io.DeltaTime = 1.0f / 60.0f;
    io.IniFilename = nullptr;
    io.Fonts->AddFontDefault();
    unsigned char* pixels = nullptr;
    int w = 0, h = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    ImGui::NewFrame();
  }
  ~HeadlessImGuiScope() {
    ImGui::EndFrame();
    ImGui::DestroyContext();
  }
};

}  // namespace

TEST_CASE("LibreDWG DXF round-trips a model-space LINE", "[dxf][libredwg]") {
  ScratchDir dir("dxf");
  const auto p = (dir.path / "line.dxf").string();
  AppCommandState st;
  OneLine(st);
  std::vector<std::string> log;
  REQUIRE(ExportDxfFile(st, p.c_str(), log));
  AppCommandState in;
  REQUIRE(ImportDxfFile(in, p.c_str(), log));
  REQUIRE(in.userLinesFlat.size() == 6);
  REQUIRE(in.userLinesFlat[3] == Catch::Approx(10.f).margin(0.05f));
}

TEST_CASE("LibreDWG DWG round-trips a model-space LINE", "[dwg][libredwg]") {
  ScratchDir dir("dwg");
  const auto p = (dir.path / "line.dwg").string();
  AppCommandState st;
  OneLine(st);
  std::vector<std::string> log;
  REQUIRE(ExportDwgFile(st, p.c_str(), log));
  REQUIRE(DwgVersionName(p.c_str()) == "AutoCAD 2000");
  AppCommandState in;
  REQUIRE(ImportDwgFile(in, p.c_str(), log));
  REQUIRE(in.userLinesFlat.size() == 6);
  REQUIRE(in.userLinesFlat[3] == Catch::Approx(10.f).margin(0.05f));
}

// Issue #600: full export path respects dwgExportVersion (LibreDWG body only — no trailer).
namespace {

int CountDwg3DSolids(const char* pathUtf8) {
  Dwg_Data dwg{};
  if (dwg_read_file(pathUtf8, &dwg) != 0) {
    dwg_free(&dwg);
    return -1;
  }
  int n = 0;
  for (unsigned i = 0; i < dwg.num_objects; ++i) {
    if (dwg.object[i].type == DWG_TYPE__3DSOLID)
      ++n;
  }
  dwg_free(&dwg);
  return n;
}

int CountDwgFixedType(const char* pathUtf8, enum DWG_OBJECT_TYPE ty) {
  Dwg_Data dwg{};
  if (dwg_read_file(pathUtf8, &dwg) >= DWG_ERR_CRITICAL) {
    dwg_free(&dwg);
    return -1;
  }
  int n = 0;
  for (unsigned i = 0; i < dwg.num_objects; ++i) {
    if (dwg.object[i].fixedtype == ty)
      ++n;
  }
  dwg_free(&dwg);
  return n;
}

int CountDwgMultileaders(const char* pathUtf8) {
  return CountDwgFixedType(pathUtf8, DWG_TYPE_MULTILEADER);
}

}  // namespace

TEST_CASE("ExportDwgFile writes a CYLINDER recipe cadSolid as 3DSOLID (issue #612)",
          "[dwg][libredwg][issue612]") {
  ScratchDir dir("dwg-solid-cylinder");
  const auto p = (dir.path / "cyl.dwg").string();
  AppCommandState st;
  brep::Solid s;
  brep::Problem why = brep::Problem::Ok;
  REQUIRE(brep::MakeCylinder(ucs::Ucs{}, 2.0, 8.0, &s, &why));
  st.cadSolids.push_back(std::make_shared<const brep::Solid>(std::move(s)));
  st.cadSolidAttrs.push_back(EntityAttributes{});
  std::vector<std::string> log;
  REQUIRE(ExportDwgFile(st, p.c_str(), log));
  REQUIRE(CountDwg3DSolids(p.c_str()) == 1);
}

TEST_CASE("ExportDwgFile writes a BOX cadSolid as 3DSOLID (issue #612)", "[dwg][libredwg][issue612]") {
  ScratchDir dir("dwg-solid-box");
  const auto p = (dir.path / "box.dwg").string();
  AppCommandState st;
  brep::Solid s;
  brep::Problem why = brep::Problem::Ok;
  REQUIRE(brep::MakeBox(ucs::Ucs{}, 4.0, 5.0, 6.0, &s, &why));
  st.cadSolids.push_back(std::make_shared<const brep::Solid>(std::move(s)));
  st.cadSolidAttrs.push_back(EntityAttributes{});
  std::vector<std::string> log;
  REQUIRE(ExportDwgFile(st, p.c_str(), log));
  REQUIRE(CountDwg3DSolids(p.c_str()) == 1);
}

TEST_CASE("ExportDwgFile writes a straight pipe run as 3DSOLID (issue #612)", "[dwg][libredwg][issue612]") {
  ScratchDir dir("dwg-pipe-run");
  const auto p = (dir.path / "pipe.dwg").string();
  AppCommandState st;
  CadPipeRun run;
  run.nominalSize = "4in";
  run.vertsXyz = {0.f, 0.f, 0.f, 20.f, 0.f, 0.f};
  st.cadPipeRuns.push_back(std::move(run));
  st.cadPipeRunAttrs.push_back(EntityAttributes{});
  std::vector<std::string> log;
  REQUIRE(ExportDwgFile(st, p.c_str(), log));
  REQUIRE(CountDwg3DSolids(p.c_str()) >= 1);
}

namespace {

std::shared_ptr<const CadMesh> MakeUnitSquareMesh() {
  auto m = std::make_shared<CadMesh>();
  m->sourceName = "test";
  m->vertsXyz = {0.f, 0.f, 0.f, 10.f, 0.f, 0.f, 10.f, 10.f, 0.f, 0.f, 10.f, 0.f};
  m->indices = {0, 1, 2, 0, 2, 3};
  CadMeshPart part;
  part.indexBegin = 0;
  part.indexCount = 6;
  m->parts.push_back(part);
  return m;
}

CadSurface MakeUnitSquareSurface() {
  CadSurface s;
  s.name = "T1";
  auto tin = std::make_shared<CadTin>();
  tin->vertsXyz = {0.0, 0.0, 0.0, 10.0, 0.0, 0.0, 10.0, 10.0, 0.0, 0.0, 10.0, 0.0};
  tin->indices = {0, 1, 2, 0, 2, 3};
  s.tin = std::move(tin);
  return s;
}

int CountDwgPolylinePface(const char* path) {
  Dwg_Data dwg;
  std::memset(&dwg, 0, sizeof(dwg));
  if (dwg_read_file(path, &dwg) >= DWG_ERR_CRITICAL) {
    dwg_free(&dwg);
    return -1;
  }
  int n = 0;
  for (unsigned i = 0; i < dwg.num_objects; ++i) {
    if (dwg.object[i].type == DWG_TYPE_POLYLINE_PFACE)
      ++n;
  }
  dwg_free(&dwg);
  return n;
}

int CountDwgEntities(Dwg_Object_Type ty, const char* path) {
  Dwg_Data dwg;
  std::memset(&dwg, 0, sizeof(dwg));
  if (dwg_read_file(path, &dwg) >= DWG_ERR_CRITICAL) {
    dwg_free(&dwg);
    return -1;
  }
  int n = 0;
  for (unsigned i = 0; i < dwg.num_objects; ++i) {
    if (dwg.object[i].fixedtype == ty)
      ++n;
  }
  dwg_free(&dwg);
  return n;
}

}  // namespace

TEST_CASE("ExportDwgFile writes a CadMesh as POLYLINE_PFACE (issue #611)", "[dwg][libredwg][issue611]") {
  ScratchDir dir("dwg-mesh-export");
  const auto p = (dir.path / "mesh.dwg").string();
  AppCommandState st;
  st.cadMeshes.push_back(MakeUnitSquareMesh());
  st.cadMeshAttrs.push_back(EntityAttributes{});
  std::vector<std::string> log;
  REQUIRE(ExportDwgFile(st, p.c_str(), log));
  REQUIRE(CountDwgPolylinePface(p.c_str()) == 1);
}

TEST_CASE("ExportDwgFile writes a built TIN surface as POLYLINE_PFACE (issue #611)",
          "[dwg][libredwg][issue611]") {
  ScratchDir dir("dwg-tin-export");
  const auto p = (dir.path / "tin.dwg").string();
  AppCommandState st;
  st.cadSurfaces.push_back(MakeUnitSquareSurface());
  st.cadSurfaceAttrs.push_back(EntityAttributes{});
  std::vector<std::string> log;
  REQUIRE(ExportDwgFile(st, p.c_str(), log));
  REQUIRE(CountDwgPolylinePface(p.c_str()) == 1);
}

TEST_CASE("DWG export loss summary omits exportable mesh and TIN (issue #611 / #614)",
          "[dwg][libredwg][issue611][issue614]") {
  AppCommandState st;
  OneLine(st);
  st.cadMeshes.push_back(MakeUnitSquareMesh());
  st.cadSurfaces.push_back(MakeUnitSquareSurface());
  const std::vector<DwgExportLoss> losses = ComputeDwgExportLosses(st);
  for (const DwgExportLoss& l : losses) {
    CHECK(l.label.find("mesh") == std::string::npos);
    CHECK(l.label.find("TIN surface") == std::string::npos);
  }
}

TEST_CASE("ExportDwgFile mesh round-trips through import (issue #611)", "[dwg][libredwg][issue611]") {
  ScratchDir dir("dwg-mesh-roundtrip");
  const auto p = (dir.path / "mesh.dwg").string();
  AppCommandState st;
  st.cadMeshes.push_back(MakeUnitSquareMesh());
  st.cadMeshAttrs.push_back(EntityAttributes{});
  std::vector<std::string> log;
  REQUIRE(ExportDwgFile(st, p.c_str(), log));
  AppCommandState loaded;
  REQUIRE(ImportDwgFile(loaded, p.c_str(), log));
  REQUIRE(loaded.cadMeshes.size() >= 1);
  CHECK(loaded.cadMeshes[0]->triangleCount() == 2);
  CHECK(loaded.cadMeshes[0]->vertexCount() == 4);
}

TEST_CASE("ExportDwgFile writes an L-shaped pipe run as 3DSOLID (issue #612)", "[dwg][libredwg][issue612]") {
  ScratchDir dir("dwg-pipe-elbow");
  const auto p = (dir.path / "elbow.dwg").string();
  AppCommandState st;
  CadPipeRun run;
  run.nominalSize = "4in";
  run.vertsXyz = {0.f, 0.f, 0.f, 20.f, 0.f, 0.f, 20.f, 15.f, 0.f};
  st.cadPipeRuns.push_back(std::move(run));
  st.cadPipeRunAttrs.push_back(EntityAttributes{});
  std::vector<std::string> log;
  REQUIRE(ExportDwgFile(st, p.c_str(), log));
  REQUIRE(CountDwg3DSolids(p.c_str()) >= 1);
}

TEST_CASE("R2004 DWG export writes native FIELD and FIELDLIST for GoSurvey area field (issue #617)",
          "[dwg][libredwg][issue617][req368]") {
  ScratchDir dir("dwg-field-native");
  const auto p = (dir.path / "area-field.dwg").string();
  AppCommandState st;
  st.userPolylineOffsets = {0, 4};
  st.userPolylineClosed = {1};
  st.userPolylineVerts = {0, 0, 0, 10, 0, 0, 10, 10, 0, 0, 10, 0};
  st.userPolylineAttrs = {EntityAttributes{}};
  st.userPolylineAttrs[0].id = 99;
  CadAnnotation ann;
  ann.kind = CadAnnotation::Kind::Mtext;
  ann.boxMinX = 0.f;
  ann.boxMaxX = 5.f;
  ann.boxMinY = 0.f;
  ann.boxMaxY = 2.f;
  ann.mtextAttach = 1;
  ann.text = CadFieldMakeGoSurveyWire(99, "Area", ".2f");
  st.cadAnnotations.push_back(ann);
  st.cadAnnotationAttrs.push_back(EntityAttributes{});
  st.dwgExportVersion = DwgSaveVersion::R2004;
  std::vector<std::string> log;
  REQUIRE(ExportLibreCadFile(st, p.c_str(), log, /*asDxf=*/false));
  CHECK(CountDwgFixedType(p.c_str(), DWG_TYPE_FIELD) >= 2);
  CHECK(CountDwgFixedType(p.c_str(), DWG_TYPE_FIELDLIST) >= 1);
  CHECK(LogContains(log, "FIELD object(s)"));
}

TEST_CASE("ExportLibreCadFile writes R2004 when dwgExportVersion is R2004 (issue #600)",
          "[dwg][libredwg][issue600]") {
  ScratchDir dir("dwg-r2004-export");
  const auto p = (dir.path / "line.dwg").string();
  AppCommandState st;
  OneLine(st);
  st.dwgExportVersion = DwgSaveVersion::R2004;
  std::vector<std::string> log;
  REQUIRE(ExportLibreCadFile(st, p.c_str(), log, /*asDxf=*/false));
  REQUIRE(DwgVersionName(p.c_str()) == "AutoCAD 2004");
}

TEST_CASE("ExportLibreCadFile writes R2018 when dwgExportVersion is R2018", "[dwg][libredwg][r2018]") {
  ScratchDir dir("dwg-r2018-export");
  const auto p = (dir.path / "line.dwg").string();
  AppCommandState st;
  OneLine(st);
  st.dwgExportVersion = DwgSaveVersion::R2018;
  std::vector<std::string> log;
  REQUIRE(ExportLibreCadFile(st, p.c_str(), log, /*asDxf=*/false));
  REQUIRE(DwgVersionName(p.c_str()) == "AutoCAD 2018");
  AppCommandState loaded;
  REQUIRE(ImportDwgFile(loaded, p.c_str(), log));
  REQUIRE(loaded.userLinesFlat.size() == 6);
}

TEST_CASE("R2004 DWG round-trips 24-bit entity and layer colours (issue #615)",
          "[dwg][libredwg][issue615]") {
  ScratchDir dir("dwg-truecolor-r2004");
  const auto p = (dir.path / "colors.dwg").string();
  AppCommandState st;
  OneLine(st);
  st.dwgExportVersion = DwgSaveVersion::R2004;
  st.userLineAttrs[0].color = "#1E90FF";
  st.userLineAttrs[0].layer = "CustomBrown";
  CadLayerRow lyr;
  lyr.name = "CustomBrown";
  lyr.color = "#8B4513";
  st.drawingLayerTable.push_back(lyr);
  std::vector<std::string> log;
  REQUIRE(ExportLibreCadFile(st, p.c_str(), log, /*asDxf=*/false));
  AppCommandState in;
  REQUIRE(ImportDwgFile(in, p.c_str(), log));
  REQUIRE(in.userLineAttrs.size() == 1);
  CHECK(in.userLineAttrs[0].color == "#1E90FF");
  const CadLayerRow* brown = nullptr;
  for (const CadLayerRow& row : in.drawingLayerTable) {
    if (row.name == "CustomBrown")
      brown = &row;
  }
  REQUIRE(brown != nullptr);
  CHECK(brown->color == "#8B4513");
}

TEST_CASE("R2000 DWG still rounds non-palette colours to ACI (issue #615)",
          "[dwg][libredwg][issue615]") {
  ScratchDir dir("dwg-aci-round-r2000");
  const auto p = (dir.path / "aci.dwg").string();
  AppCommandState st;
  OneLine(st);
  st.dwgExportVersion = DwgSaveVersion::R2000;
  st.userLineAttrs[0].color = "#1E90FF";
  std::vector<std::string> log;
  REQUIRE(ExportLibreCadFile(st, p.c_str(), log, /*asDxf=*/false));
  uint32_t rgb = 0;
  REQUIRE(DxfColorStringToRgbPacked("#1E90FF", &rgb));
  const int aci = DxfNearestAciFromRgbPacked(rgb);
  char expected[16];
  DxfRgbPackedToHex(DxfRgbPackedFromAci(aci), expected, sizeof(expected));
  AppCommandState in;
  REQUIRE(ImportDwgFile(in, p.c_str(), log));
  REQUIRE(in.userLineAttrs.size() == 1);
  CHECK(in.userLineAttrs[0].color == expected);
  CHECK(in.userLineAttrs[0].color != "#1E90FF");
}

TEST_CASE("DWG export loss omits colour rounding when saving R2004 (issue #615 / #614)",
          "[dwg][libredwg][issue615][issue614]") {
  AppCommandState st;
  OneLine(st);
  st.dwgExportVersion = DwgSaveVersion::R2004;
  st.userLineAttrs[0].color = "#1E90FF";
  const std::vector<DwgExportLoss> losses = ComputeDwgExportLosses(st);
  for (const DwgExportLoss& l : losses)
    CHECK(l.label.find("colour") == std::string::npos);
}

TEST_CASE("R2004 DWG round-trips entity transparency (issue #620)", "[dwg][libredwg][issue620]") {
  ScratchDir dir("dwg-transparency");
  const auto p = (dir.path / "tr.dwg").string();
  AppCommandState st;
  OneLine(st);
  st.dwgExportVersion = DwgSaveVersion::R2004;
  st.userLineAttrs[0].transparency = 0.6f;
  std::vector<std::string> log;
  REQUIRE(ExportLibreCadFile(st, p.c_str(), log, /*asDxf=*/false));
  AppCommandState in;
  REQUIRE(ImportDwgFile(in, p.c_str(), log));
  REQUIRE(in.userLineAttrs.size() == 1);
  CHECK(in.userLineAttrs[0].transparency == Catch::Approx(0.6f).margin(0.02f));
}

TEST_CASE("R2018 DWG writes ByLayer transparency from layer table (issue #620)",
          "[dwg][libredwg][issue620]") {
  ScratchDir dir("dwg-layer-transparency");
  const auto p = (dir.path / "layer-tr.dwg").string();
  AppCommandState st;
  OneLine(st);
  st.dwgExportVersion = DwgSaveVersion::R2018;
  CadLayerRow row{};
  row.name = "C-TOPO";
  row.transparency = 0.3f;
  st.drawingLayerTable.push_back(row);
  st.userLineAttrs[0].layer = "C-TOPO";
  st.userLineAttrs[0].color = "ByLayer";
  std::vector<std::string> log;
  REQUIRE(ExportLibreCadFile(st, p.c_str(), log, /*asDxf=*/false));
  AppCommandState in;
  REQUIRE(ImportLibreCadFile(in, p.c_str(), log, /*asDxf=*/false));
  REQUIRE(in.userLineAttrs.size() == 1);
  CHECK(in.userLineAttrs[0].transparency == Catch::Approx(0.3f).margin(0.02f));
}

TEST_CASE("DWG export loss lists transparency only for R2000 (issue #620 / #614)",
          "[dwg][libredwg][issue620][issue614]") {
  AppCommandState st;
  OneLine(st);
  st.userLineAttrs[0].transparency = 0.5f;
  st.dwgExportVersion = DwgSaveVersion::R2000;
  bool sawTransparency = false;
  for (const DwgExportLoss& l : ComputeDwgExportLosses(st)) {
    if (l.label.find("transparency") != std::string::npos)
      sawTransparency = true;
  }
  CHECK(sawTransparency);
  st.dwgExportVersion = DwgSaveVersion::R2004;
  for (const DwgExportLoss& l : ComputeDwgExportLosses(st)) {
    CHECK(l.label.find("transparency") == std::string::npos);
  }
}

TEST_CASE("LibreDWG dwg_add_MULTILEADER encodes and decodes (issue #619)",
          "[dwg][libredwg][issue619]") {
  ScratchDir dir("dwg-api-mleader");
  const auto p = (dir.path / "api-mleader.dwg").string();
  Dwg_Data* dwg = dwg_new_Document(R_2018, 0, 0);
  REQUIRE(dwg != nullptr);
  Dwg_Object* m = dwg_model_space_object(dwg);
  REQUIRE(m != nullptr);
  Dwg_Object_BLOCK_HEADER* hdr = m->tio.object->tio.BLOCK_HEADER;
  REQUIRE(hdr != nullptr);
  const dwg_point_3d lpts[3] = {{0.0, 0.0, 0.0}, {10.0, 0.0, 0.0}, {12.0, 2.0, 0.0}};
  const dwg_point_3d textPt{12.0, 2.0, 0.0};
  const dwg_point_3d textDir{1.0, 0.0, 0.0};
  REQUIRE(dwg_add_MULTILEADER(hdr, 3, lpts, "Monument A", &textPt, &textDir, 0.18, 8.0) != nullptr);
  int inDoc = 0;
  for (unsigned i = 0; i < dwg->num_objects; ++i) {
    if (dwg->object[i].fixedtype == DWG_TYPE_MULTILEADER)
      ++inDoc;
  }
  CHECK(inDoc == 1);
  CHECK(hdr->num_owned >= 1);
  LibreDwgLinkBlockEntities(dwg);
  REQUIRE(dwg_write_file(p.c_str(), dwg) == 0);
  dwg_free(dwg);
  std::free(dwg);
  CHECK(CountDwgMultileaders(p.c_str()) == 1);
}

TEST_CASE("CadMultileader round-trips through native MULTILEADER DWG export (issue #619)",
          "[dwg][libredwg][issue619]") {
  ScratchDir dir("dwg-mleader");
  const auto p = (dir.path / "ml.dwg").string();
  AppCommandState st;
  st.dwgExportVersion = DwgSaveVersion::R2018;
  CadMultileader ml{};
  ml.pathXyz = {0.f, 0.f, 0.f, 10.f, 0.f, 0.f, 12.f, 2.f, 0.f};
  ml.label.kind = CadAnnotation::Kind::Mtext;
  ml.label.insX = 12.f;
  ml.label.insY = 2.f;
  ml.label.text = "Monument A";
  ml.label.boxMinX = 12.f;
  ml.label.boxMinY = 1.f;
  ml.label.boxMaxX = 20.f;
  ml.label.boxMaxY = 2.f;
  st.cadMultileaders.push_back(ml);
  st.cadMultileaderAttrs.push_back(EntityAttributes{});
  std::vector<std::string> log;
  REQUIRE(ExportLibreCadFile(st, p.c_str(), log, /*asDxf=*/false));
  CHECK(CountDwgMultileaders(p.c_str()) == 1);
  CHECK(CountDwgFixedType(p.c_str(), DWG_TYPE_MLEADERSTYLE) >= 1);
  CHECK(CountDwgFixedType(p.c_str(), DWG_TYPE_LEADER) == 0);
  AppCommandState in;
  REQUIRE(ImportLibreCadFile(in, p.c_str(), log, /*asDxf=*/false));
  REQUIRE(in.cadMultileaders.size() == 1);
  CHECK(in.cadMultileaders[0].pathXyz.size() == 9);
  CHECK(in.cadMultileaders[0].label.text.find("Monument") != std::string::npos);
}

TEST_CASE("Block-content MULTILEADER import logs skip reason (REQ-367, issue #619)",
          "[dwg][libredwg][issue619]") {
  ScratchDir dir("dwg-mleader-block-skip");
  const auto p = (dir.path / "block-ml.dwg").string();
  Dwg_Data* dwg = dwg_new_Document(R_2018, 0, 0);
  REQUIRE(dwg != nullptr);
  Dwg_Object* m = dwg_model_space_object(dwg);
  REQUIRE(m != nullptr);
  Dwg_Object_BLOCK_HEADER* hdr = m->tio.object->tio.BLOCK_HEADER;
  REQUIRE(hdr != nullptr);

  Dwg_Object_BLOCK_HEADER* blkHdr = dwg_add_BLOCK_HEADER(dwg, "MLBLK");
  REQUIRE(blkHdr != nullptr);
  dwg_add_BLOCK(blkHdr, "MLBLK");
  const dwg_point_3d circleCenter{0.0, 0.0, 0.0};
  dwg_add_CIRCLE(blkHdr, &circleCenter, 0.1);
  dwg_add_ENDBLK(blkHdr);

  const dwg_point_3d lpts[2] = {{0.0, 0.0, 0.0}, {10.0, 0.0, 0.0}};
  const dwg_point_3d textPt{10.0, 0.0, 0.0};
  const dwg_point_3d textDir{1.0, 0.0, 0.0};
  Dwg_Entity_MULTILEADER* ml =
      dwg_add_MULTILEADER(hdr, 2, lpts, "unused", &textPt, &textDir, 0.18, 8.0);
  REQUIRE(ml != nullptr);
  Dwg_Object* mlObj = nullptr;
  for (unsigned i = 0; i < dwg->num_objects; ++i) {
    if (dwg->object[i].fixedtype == DWG_TYPE_MULTILEADER) {
      mlObj = &dwg->object[i];
      break;
    }
  }
  REQUIRE(mlObj != nullptr);
  BITCODE_H blkRef = dwg_find_tablehandle(dwg, "MLBLK", "BLOCK");
  REQUIRE(blkRef != nullptr);
  ml->ctx.has_content_txt = 0;
  ml->ctx.has_content_blk = 1;
  ml->ctx.content.blk.location.x = textPt.x;
  ml->ctx.content.blk.location.y = textPt.y;
  ml->ctx.content.blk.location.z = textPt.z;
  ml->ctx.content.blk.normal.x = 0.0;
  ml->ctx.content.blk.normal.y = 0.0;
  ml->ctx.content.blk.normal.z = 1.0;
  ml->ctx.content.blk.scale.x = ml->ctx.content.blk.scale.y = ml->ctx.content.blk.scale.z = 1.0;
  ml->ctx.content.blk.block_table =
      dwg_add_handleref(dwg, 4, blkRef->absolute_ref, mlObj);
  ml->ctx.content.blk.transform = static_cast<BITCODE_BD*>(std::calloc(16, sizeof(BITCODE_BD)));
  REQUIRE(ml->ctx.content.blk.transform != nullptr);
  ml->ctx.content.blk.transform[0] = 1.0;
  ml->ctx.content.blk.transform[5] = 1.0;
  ml->ctx.content.blk.transform[10] = 1.0;
  ml->ctx.content.blk.transform[15] = 1.0;
  LibreDwgLinkBlockEntities(dwg);
  REQUIRE(dwg_write_file(p.c_str(), dwg) == 0);
  dwg_free(dwg);
  std::free(dwg);

  Dwg_Data reread{};
  std::memset(&reread, 0, sizeof(reread));
  const int err = dwg_read_file(p.c_str(), &reread);
  REQUIRE(err < DWG_ERR_CRITICAL);
  bool sawBlockOnly = false;
  for (unsigned i = 0; i < reread.num_objects; ++i) {
    if (reread.object[i].fixedtype != DWG_TYPE_MULTILEADER ||
        reread.object[i].tio.entity == nullptr ||
        reread.object[i].tio.entity->tio.MULTILEADER == nullptr)
      continue;
    const Dwg_Entity_MULTILEADER* got = reread.object[i].tio.entity->tio.MULTILEADER;
    if (got->ctx.has_content_blk && !got->ctx.has_content_txt)
      sawBlockOnly = true;
  }
  dwg_free(&reread);
  REQUIRE(sawBlockOnly);

  AppCommandState in;
  std::vector<std::string> log;
  REQUIRE(ImportLibreCadFile(in, p.c_str(), log, /*asDxf=*/false));
  CHECK(in.cadMultileaders.empty());
  CHECK(LogContains(log, "MULTILEADER(block content, issue #619)"));
}

TEST_CASE("CadMultileader extra branches round-trip native MULTILEADER export (issue #619)",
          "[dwg][libredwg][issue619]") {
  ScratchDir dir("dwg-mleader-branches");
  const auto p = (dir.path / "ml-branches.dwg").string();
  AppCommandState st;
  st.dwgExportVersion = DwgSaveVersion::R2018;
  CadMultileader ml{};
  ml.pathXyz = {0.f, 0.f, 0.f, 10.f, 0.f, 0.f};
  ml.extraLeaderPaths.push_back({5.f, 8.f, 0.f, 10.f, 0.f, 0.f});
  ml.label.kind = CadAnnotation::Kind::Mtext;
  ml.label.insX = 10.f;
  ml.label.insY = 0.f;
  ml.label.text = "Two branches";
  ml.label.boxMinX = 10.f;
  ml.label.boxMinY = -1.f;
  ml.label.boxMaxX = 24.f;
  ml.label.boxMaxY = 1.f;
  st.cadMultileaders.push_back(std::move(ml));
  st.cadMultileaderAttrs.push_back(EntityAttributes{});
  std::vector<std::string> log;
  REQUIRE(ExportLibreCadFile(st, p.c_str(), log, /*asDxf=*/false));
  Dwg_Data dwg{};
  REQUIRE(dwg_read_file(p.c_str(), &dwg) < DWG_ERR_CRITICAL);
  int leaderNodes = 0;
  for (unsigned i = 0; i < dwg.num_objects; ++i) {
    if (dwg.object[i].fixedtype != DWG_TYPE_MULTILEADER || dwg.object[i].tio.entity == nullptr ||
        dwg.object[i].tio.entity->tio.MULTILEADER == nullptr)
      continue;
    leaderNodes = static_cast<int>(dwg.object[i].tio.entity->tio.MULTILEADER->ctx.num_leaders);
  }
  dwg_free(&dwg);
  REQUIRE(leaderNodes == 2);
  AppCommandState in;
  REQUIRE(ImportLibreCadFile(in, p.c_str(), log, /*asDxf=*/false));
  REQUIRE(in.cadMultileaders.size() == 1);
  CHECK(in.cadMultileaders[0].extraLeaderPaths.size() == 1);
}

TEST_CASE("Annotative multileader round-trips is_annotative in DWG (issue #622)",
          "[dwg][libredwg][issue622]") {
  ScratchDir dir("dwg-mleader-annotative");
  const auto p = (dir.path / "ml-anno.dwg").string();
  AppCommandState st;
  st.dwgExportVersion = DwgSaveVersion::R2018;
  CadMultileader ml{};
  ml.annotative = true;
  ml.pathXyz = {0.f, 0.f, 0.f, 10.f, 0.f, 0.f};
  ml.label.kind = CadAnnotation::Kind::Mtext;
  ml.label.insX = 10.f;
  ml.label.insY = 0.f;
  ml.label.text = "Annotative callout";
  ml.label.boxMinX = 10.f;
  ml.label.boxMinY = -1.f;
  ml.label.boxMaxX = 26.f;
  ml.label.boxMaxY = 1.f;
  st.cadMultileaders.push_back(std::move(ml));
  st.cadMultileaderAttrs.push_back(EntityAttributes{});
  std::vector<std::string> log;
  REQUIRE(ExportLibreCadFile(st, p.c_str(), log, /*asDxf=*/false));
  Dwg_Data dwg{};
  REQUIRE(dwg_read_file(p.c_str(), &dwg) < DWG_ERR_CRITICAL);
  bool sawAnno = false;
  for (unsigned i = 0; i < dwg.num_objects; ++i) {
    if (dwg.object[i].fixedtype != DWG_TYPE_MULTILEADER || dwg.object[i].tio.entity == nullptr ||
        dwg.object[i].tio.entity->tio.MULTILEADER == nullptr)
      continue;
    if (dwg.object[i].tio.entity->tio.MULTILEADER->is_annotative != 0)
      sawAnno = true;
  }
  dwg_free(&dwg);
  REQUIRE(sawAnno);
  AppCommandState in;
  REQUIRE(ImportLibreCadFile(in, p.c_str(), log, /*asDxf=*/false));
  REQUIRE(in.cadMultileaders.size() == 1);
  CHECK(in.cadMultileaders[0].annotative);
}

TEST_CASE("Annotative MTEXT round-trips is_not_annotative in DWG (issue #622)", "[dwg][libredwg][issue622]") {
  ScratchDir dir("dwg-mtext-annotative");
  const auto p = (dir.path / "mt-anno.dwg").string();
  AppCommandState st;
  st.dwgExportVersion = DwgSaveVersion::R2018;
  CadAnnotation m{};
  m.kind = CadAnnotation::Kind::Mtext;
  m.annotative = true;
  m.insX = 0.f;
  m.insY = 0.f;
  m.text = "Annotative note";
  m.boxMinX = 0.f;
  m.boxMinY = -1.f;
  m.boxMaxX = 20.f;
  m.boxMaxY = 1.f;
  st.cadAnnotations.push_back(std::move(m));
  st.cadAnnotationAttrs.push_back(EntityAttributes{});
  std::vector<std::string> log;
  REQUIRE(ExportLibreCadFile(st, p.c_str(), log, /*asDxf=*/false));
  Dwg_Data dwg{};
  REQUIRE(dwg_read_file(p.c_str(), &dwg) < DWG_ERR_CRITICAL);
  bool sawAnno = false;
  for (unsigned i = 0; i < dwg.num_objects; ++i) {
    if (dwg.object[i].fixedtype != DWG_TYPE_MTEXT || dwg.object[i].tio.entity == nullptr ||
        dwg.object[i].tio.entity->tio.MTEXT == nullptr)
      continue;
    if (dwg.object[i].tio.entity->tio.MTEXT->is_not_annotative == 0)
      sawAnno = true;
  }
  dwg_free(&dwg);
  REQUIRE(sawAnno);
  AppCommandState in;
  REQUIRE(ImportLibreCadFile(in, p.c_str(), log, /*asDxf=*/false));
  REQUIRE(in.cadAnnotations.size() == 1);
  CHECK(in.cadAnnotations[0].annotative);
}

TEST_CASE("Annotative TEXT round-trips via GOSURVEY XDATA (issue #622)", "[dwg][libredwg][issue622]") {
  ScratchDir dir("dwg-text-annotative");
  const auto p = (dir.path / "txt-anno.dwg").string();
  AppCommandState st;
  st.dwgExportVersion = DwgSaveVersion::R2018;
  CadAnnotation t{};
  t.kind = CadAnnotation::Kind::Text;
  t.annotative = true;
  t.insX = 2.f;
  t.insY = 3.f;
  t.plottedHeightInches = 0.125f;
  t.text = "Annotative label";
  st.cadAnnotations.push_back(std::move(t));
  st.cadAnnotationAttrs.push_back(EntityAttributes{});
  std::vector<std::string> log;
  REQUIRE(ExportLibreCadFile(st, p.c_str(), log, /*asDxf=*/false));
  AppCommandState in;
  REQUIRE(ImportLibreCadFile(in, p.c_str(), log, /*asDxf=*/false));
  REQUIRE(in.cadAnnotations.size() == 1);
  CHECK(in.cadAnnotations[0].annotative);
  CHECK(in.cadAnnotations[0].text == "Annotative label");
}

TEST_CASE("AcadAnnotative EED imports when GOSURVEY marker stripped (issue #622)", "[dwg][libredwg][issue622]") {
  ScratchDir dir("dwg-acad-annotative-eed");
  const auto exported = (dir.path / "both.dwg").string();
  const auto acadOnly = (dir.path / "acad-only.dwg").string();
  AppCommandState st;
  st.dwgExportVersion = DwgSaveVersion::R2018;
  CadBlockDefinition def;
  def.name = "SYM";
  def.content.lines = {0.f, 0.f, 0.f, 1.f, 0.f, 0.f};
  def.content.lineAttrs.push_back(EntityAttributes{});
  st.blockDefs.push_back(std::move(def));
  CadBlockRef ref;
  ref.defName = "SYM";
  ref.annotative = true;
  ref.xf.x = 1.f;
  ref.xf.y = 2.f;
  st.cadBlockRefs.push_back(std::move(ref));
  st.cadBlockRefAttrs.push_back(EntityAttributes{});
  std::vector<std::string> log;
  REQUIRE(ExportLibreCadFile(st, exported.c_str(), log, /*asDxf=*/false));
  Dwg_Data dwg{};
  REQUIRE(dwg_read_file(exported.c_str(), &dwg) < DWG_ERR_CRITICAL);
  REQUIRE(DwgHasAcadAnnotativeDataEed(dwg));
  StripGosurveyAnnotativeEed(dwg);
  REQUIRE(DwgHasAcadAnnotativeDataEed(dwg));
  REQUIRE(dwg_write_file(acadOnly.c_str(), &dwg) == 0);
  dwg_free(&dwg);
  AppCommandState in;
  REQUIRE(ImportLibreCadFile(in, acadOnly.c_str(), log, /*asDxf=*/false));
  REQUIRE(in.cadBlockRefs.size() == 1);
  CHECK(in.cadBlockRefs[0].annotative);
}

TEST_CASE("Annotation scale list round-trips through DWG (issue #622)", "[dwg][libredwg][issue622]") {
  ScratchDir dir("dwg-scale-list");
  const auto p = (dir.path / "scales.dwg").string();
  AppCommandState st;
  st.dwgExportVersion = DwgSaveVersion::R2018;
  CadAnnotationScale s;
  s.name = "1:25";
  s.paperUnits = 1.f;
  s.drawingUnits = 25.f;
  st.annotationScales.push_back(std::move(s));
  st.currentAnnotationScaleIndex = 0;
  std::vector<std::string> log;
  REQUIRE(ExportLibreCadFile(st, p.c_str(), log, /*asDxf=*/false));
  AppCommandState in;
  REQUIRE(ImportLibreCadFile(in, p.c_str(), log, /*asDxf=*/false));
  REQUIRE(in.annotationScales.size() == 1);
  CHECK(in.annotationScales[0].name == "1:25");
  CHECK(in.annotationScales[0].drawingUnits == Catch::Approx(25.f));
}

TEST_CASE("Two annotation scales round-trip through DWG (issue #622)", "[dwg][libredwg][issue622]") {
  ScratchDir dir("dwg-two-scales");
  const auto p = (dir.path / "two-scales.dwg").string();
  AppCommandState st;
  st.dwgExportVersion = DwgSaveVersion::R2018;
  CadAnnotationScale a;
  a.name = "1:10";
  a.paperUnits = 1.f;
  a.drawingUnits = 10.f;
  CadAnnotationScale b;
  b.name = "1:50";
  b.paperUnits = 1.f;
  b.drawingUnits = 50.f;
  st.annotationScales.push_back(a);
  st.annotationScales.push_back(b);
  st.currentAnnotationScaleIndex = 1;
  std::vector<std::string> log;
  REQUIRE(ExportLibreCadFile(st, p.c_str(), log, /*asDxf=*/false));
  Dwg_Data probe{};
  REQUIRE(dwg_read_file(p.c_str(), &probe) < DWG_ERR_CRITICAL);
  int scaleObjCount = 0;
  for (unsigned i = 0; i < probe.num_objects; ++i) {
    if (probe.object[i].fixedtype == DWG_TYPE_SCALE)
      ++scaleObjCount;
  }
  dwg_free(&probe);
  REQUIRE(scaleObjCount == 2);
  AppCommandState in;
  REQUIRE(ImportLibreCadFile(in, p.c_str(), log, /*asDxf=*/false));
  REQUIRE(in.annotationScales.size() == 2);
  CHECK(in.annotationScales[0].name == "1:10");
  CHECK(in.annotationScales[1].name == "1:50");
  CHECK(in.currentAnnotationScaleIndex == 1);
}

TEST_CASE("GOSURVEY CANNOSCALE EED round-trips on model space (issue #622)", "[dwg][libredwg][issue622]") {
  ScratchDir dir("dwg-cannoscale-eed");
  const auto p = (dir.path / "cannoscale.dwg").string();
  AppCommandState st;
  st.dwgExportVersion = DwgSaveVersion::R2018;
  CadAnnotationScale s;
  s.name = "1:50";
  s.paperUnits = 1.f;
  s.drawingUnits = 50.f;
  st.annotationScales.push_back(std::move(s));
  st.currentAnnotationScaleIndex = 0;
  std::vector<std::string> log;
  REQUIRE(ExportLibreCadFile(st, p.c_str(), log, /*asDxf=*/false));
  AppCommandState in;
  in.modelUnitsPerPlottedInch = 10.f;
  REQUIRE(ImportLibreCadFile(in, p.c_str(), log, /*asDxf=*/false));
  REQUIRE(in.annotationScales.size() == 1);
  CHECK(in.currentAnnotationScaleIndex == 0);
  CHECK(in.annotationScales[0].name == "1:50");
}

TEST_CASE("Multileader annotative flag persists through GsIo (issue #622)", "[issue622][gsio]") {
  AppCommandState src;
  CadMultileader ml{};
  ml.annotative = true;
  ml.pathXyz = {0.f, 0.f, 0.f, 5.f, 0.f, 0.f};
  ml.label.kind = CadAnnotation::Kind::Mtext;
  ml.label.text = "A";
  src.cadMultileaders.push_back(std::move(ml));
  src.cadMultileaderAttrs.push_back(EntityAttributes{});
  std::vector<std::string> log;
  AppCommandState back;
  REQUIRE(LoadGoSurveyFromJsonUtf8(back, SerializeGoSurveyJson(src), log));
  REQUIRE(back.cadMultileaders.size() == 1);
  CHECK(back.cadMultileaders[0].annotative);
}

TEST_CASE("DWG annotation scale list persists through GsIo (issue #622)", "[issue622][gsio]") {
  AppCommandState src;
  CadAnnotationScale s;
  s.name = "1:20";
  s.paperUnits = 1.f;
  s.drawingUnits = 20.f;
  src.annotationScales.push_back(std::move(s));
  src.currentAnnotationScaleIndex = 0;
  std::vector<std::string> log;
  AppCommandState back;
  REQUIRE(LoadGoSurveyFromJsonUtf8(back, SerializeGoSurveyJson(src), log));
  REQUIRE(back.annotationScales.size() == 1);
  CHECK(back.annotationScales[0].name == "1:20");
  CHECK(back.annotationScales[0].drawingUnits == Catch::Approx(20.f));
  CHECK(back.currentAnnotationScaleIndex == 0);
}

TEST_CASE("SyncCurrentAnnotationScaleIndex picks scale closest to plot scale (issue #622)", "[issue622][gsio]") {
  AppCommandState st;
  st.modelUnitsPerPlottedInch = 20.f;
  CadAnnotationScale a;
  a.name = "1:10";
  a.paperUnits = 1.f;
  a.drawingUnits = 10.f;
  CadAnnotationScale b;
  b.name = "1:20";
  b.paperUnits = 1.f;
  b.drawingUnits = 20.f;
  st.annotationScales.push_back(a);
  st.annotationScales.push_back(b);
  st.currentAnnotationScaleIndex = -1;
  SyncCurrentAnnotationScaleIndex(st);
  REQUIRE(st.currentAnnotationScaleIndex == 1);
}

TEST_CASE("Dynamic anonymous *U INSERT round-trips as cadBlockRef (issue #618)", "[dwg][libredwg][issue618]") {
  ScratchDir dir("dwg-dynamic-u-block");
  const auto p = (dir.path / "dynu.dwg").string();
  AppCommandState st;
  CadBlockDefinition def;
  def.name = "*U42";
  def.dynamicAnonymous = true;
  def.content.lines = {0.f, 0.f, 0.f, 2.f, 0.f, 0.f};
  def.content.lineAttrs.push_back(EntityAttributes{});
  st.blockDefs.push_back(std::move(def));
  CadBlockRef ref;
  ref.defName = "*U42";
  ref.dynamicCanonicalName = "DOOR";
  ref.xf.x = 3.f;
  ref.xf.y = 4.f;
  st.cadBlockRefs.push_back(std::move(ref));
  st.cadBlockRefAttrs.push_back(EntityAttributes{});
  std::vector<std::string> log;
  REQUIRE(ExportLibreCadFile(st, p.c_str(), log, /*asDxf=*/false));
  AppCommandState in;
  REQUIRE(ImportLibreCadFile(in, p.c_str(), log, /*asDxf=*/false));
  REQUIRE(in.blockDefs.size() == 1);
  CHECK(in.blockDefs[0].name == "*U42");
  CHECK(in.blockDefs[0].dynamicAnonymous);
  REQUIRE(in.cadBlockRefs.size() == 1);
  CHECK(in.cadBlockRefs[0].defName == "*U42");
  CHECK(in.cadBlockRefs[0].xf.x == Catch::Approx(3.f));
  CHECK(in.cadBlockRefs[0].xf.y == Catch::Approx(4.f));
  CHECK(in.userLinesFlat.empty());
}

TEST_CASE("Foreign DWG with *U INSERT imports as block ref not exploded geometry (issue #618)",
          "[dwg][libredwg][issue618]") {
  ScratchDir dir("dwg-foreign-dynamic-u");
  const auto p = (dir.path / "foreign.dwg").string();
  Dwg_Data* dwg = dwg_new_Document(R_2000, 0, 0);
  REQUIRE(dwg != nullptr);
  Dwg_Object* m = dwg_model_space_object(dwg);
  REQUIRE(m != nullptr);
  auto* ms = m->tio.object->tio.BLOCK_HEADER;
  REQUIRE(ms != nullptr);

  Dwg_Object_BLOCK_HEADER* ublk = dwg_add_BLOCK_HEADER(dwg, "*U99");
  REQUIRE(ublk != nullptr);
  ublk->anonymous = 1;
  dwg_add_BLOCK(ublk, "*U99");
  dwg_point_3d a{0.0, 0.0, 0.0};
  dwg_point_3d b{5.0, 0.0, 0.0};
  dwg_add_LINE(ublk, &a, &b);
  dwg_add_ENDBLK(ublk);

  dwg_point_3d ins{10.0, 20.0, 0.0};
  REQUIRE(dwg_add_INSERT(ms, &ins, "*U99", 1.0, 1.0, 1.0, 0.0) != nullptr);
  REQUIRE(dwg_write_file(p.c_str(), dwg) == 0);
  dwg_free(dwg);
  std::free(dwg);

  AppCommandState in;
  std::vector<std::string> log;
  REQUIRE(ImportLibreCadFile(in, p.c_str(), log, /*asDxf=*/false));
  REQUIRE(in.cadBlockRefs.size() == 1);
  CHECK(in.cadBlockRefs[0].defName == "*U99");
  CHECK(in.cadBlockRefs[0].xf.x == Catch::Approx(10.f));
  CHECK(in.cadBlockRefs[0].xf.y == Catch::Approx(20.f));
  CHECK(in.userLinesFlat.empty());
  REQUIRE(in.blockDefs.size() == 1);
  CHECK(in.blockDefs[0].dynamicAnonymous);
}

TEST_CASE("GoSurvey linear dynamic block exports evaluation graph at R2004 (issue #618 inc3)",
          "[dwg][libredwg][issue618][inc3]") {
  ScratchDir dir("dwg-gosurvey-dynamic-export");
  const auto p = (dir.path / "dynexport.dwg").string();
  AppCommandState st;
  st.dwgExportVersion = DwgSaveVersion::R2004;
  CadBlockDefinition def;
  def.name = "STRETCH_DOOR";
  def.content.lines = {0.f, 0.f, 0.f, 2.f, 0.f, 0.f};
  def.content.lineAttrs.push_back(EntityAttributes{});
  CadBlockParameter len;
  len.name = "Width";
  len.kind = CadBlockParamKind::Linear;
  len.value = 2.f;
  len.minValue = 0.f;
  len.maxValue = 10.f;
  def.parameters.push_back(len);
  CadBlockAction stretch;
  stretch.kind = CadBlockActionKind::Stretch;
  stretch.paramName = "Width";
  stretch.originX = 0.f;
  stretch.originY = 0.f;
  stretch.dirX = 1.f;
  stretch.dirY = 0.f;
  stretch.threshold = 0.f;
  def.actions.push_back(stretch);
  st.blockDefs.push_back(std::move(def));
  CadBlockRef ref;
  ref.defName = "STRETCH_DOOR";
  st.cadBlockRefs.push_back(std::move(ref));
  st.cadBlockRefAttrs.push_back(EntityAttributes{});
  std::vector<std::string> log;
  REQUIRE(ExportLibreCadFile(st, p.c_str(), log, /*asDxf=*/false));

  Dwg_Data rd;
  std::memset(&rd, 0, sizeof(rd));
  REQUIRE(dwg_read_file(p.c_str(), &rd) < DWG_ERR_CRITICAL);
  int nGraph = 0;
  int nLinear = 0;
  int nStretch = 0;
  for (BITCODE_BL i = 0; i < rd.num_objects; ++i) {
    const Dwg_Object* o = &rd.object[i];
    if (o->fixedtype == DWG_TYPE_EVALUATION_GRAPH)
      ++nGraph;
    if (o->fixedtype == DWG_TYPE_BLOCKLINEARPARAMETER)
      ++nLinear;
    if (o->fixedtype == DWG_TYPE_BLOCKSTRETCHACTION)
      ++nStretch;
  }
  CHECK(nGraph >= 1);
  CHECK(nLinear >= 1);
  CHECK(nStretch >= 1);
  dwg_free(&rd);
}

TEST_CASE("GoSurvey dynamic block DWG import restores parameters and actions (issue #618 inc4)",
          "[dwg][libredwg][issue618][inc4]") {
  ScratchDir dir("dwg-gosurvey-dynamic-import");
  const auto p = (dir.path / "dynimport.dwg").string();
  AppCommandState st;
  st.dwgExportVersion = DwgSaveVersion::R2004;
  CadBlockDefinition def;
  def.name = "STRETCH_DOOR";
  def.content.lines = {0.f, 0.f, 0.f, 2.f, 0.f, 0.f};
  def.content.lineAttrs.push_back(EntityAttributes{});
  CadBlockParameter len;
  len.name = "Width";
  len.kind = CadBlockParamKind::Linear;
  len.value = 2.f;
  len.minValue = 0.f;
  len.maxValue = 10.f;
  def.parameters.push_back(len);
  CadBlockAction stretch;
  stretch.kind = CadBlockActionKind::Stretch;
  stretch.paramName = "Width";
  stretch.originX = 0.f;
  stretch.originY = 0.f;
  stretch.dirX = 1.f;
  stretch.dirY = 0.f;
  def.actions.push_back(stretch);
  st.blockDefs.push_back(std::move(def));
  CadBlockRef ref;
  ref.defName = "STRETCH_DOOR";
  st.cadBlockRefs.push_back(std::move(ref));
  st.cadBlockRefAttrs.push_back(EntityAttributes{});
  std::vector<std::string> log;
  REQUIRE(ExportLibreCadFile(st, p.c_str(), log, /*asDxf=*/false));

  AppCommandState in;
  REQUIRE(ImportLibreCadFile(in, p.c_str(), log, /*asDxf=*/false));
  REQUIRE(in.blockDefs.size() == 1);
  CHECK(in.blockDefs[0].name == "STRETCH_DOOR");
  REQUIRE(in.blockDefs[0].parameters.size() >= 1);
  CHECK(in.blockDefs[0].parameters[0].name == "Width");
  CHECK(in.blockDefs[0].parameters[0].value == Catch::Approx(2.f).margin(0.01f));
  REQUIRE(in.blockDefs[0].actions.size() >= 1);
  CHECK(in.blockDefs[0].actions[0].kind == CadBlockActionKind::Stretch);
  CHECK(in.blockDefs[0].actions[0].paramName == "Width");
  REQUIRE(in.cadBlockRefs.size() == 1);
  REQUIRE(in.cadBlockRefs[0].paramState.size() >= 1);
  CHECK(in.cadBlockRefs[0].paramState[0].name == "Width");
}

TEST_CASE("Foreign dynamic insert draws evaluated *U geometry not default size (issue #618 inc2)",
          "[dwg][libredwg][issue618][inc2]") {
  ScratchDir dir("dwg-dynamic-golden-display");
  const auto p = (dir.path / "golden.dwg").string();
  Dwg_Data* dwg = dwg_new_Document(R_2000, 0, 0);
  REQUIRE(dwg != nullptr);
  Dwg_Object* m = dwg_model_space_object(dwg);
  REQUIRE(m != nullptr);
  auto* ms = m->tio.object->tio.BLOCK_HEADER;
  REQUIRE(ms != nullptr);

  Dwg_Object_BLOCK_HEADER* door = dwg_add_BLOCK_HEADER(dwg, "DOOR");
  REQUIRE(door != nullptr);
  dwg_add_BLOCK(door, "DOOR");
  dwg_point_3d d0{0.0, 0.0, 0.0};
  dwg_point_3d d1{1.0, 0.0, 0.0};
  dwg_add_LINE(door, &d0, &d1);
  dwg_add_ENDBLK(door);

  Dwg_Object_BLOCK_HEADER* ublk = dwg_add_BLOCK_HEADER(dwg, "*U1");
  REQUIRE(ublk != nullptr);
  ublk->anonymous = 1;
  dwg_add_BLOCK(ublk, "*U1");
  dwg_point_3d u1{0.0, 0.0, 0.0};
  dwg_point_3d u2{5.0, 0.0, 0.0};
  dwg_add_LINE(ublk, &u1, &u2);
  dwg_add_ENDBLK(ublk);

  dwg_point_3d ins{0.0, 0.0, 0.0};
  REQUIRE(dwg_add_INSERT(ms, &ins, "*U1", 1.0, 1.0, 1.0, 0.0) != nullptr);
  REQUIRE(dwg_write_file(p.c_str(), dwg) == 0);
  dwg_free(dwg);
  std::free(dwg);

  AppCommandState st;
  std::vector<std::string> log;
  REQUIRE(ImportLibreCadFile(st, p.c_str(), log, /*asDxf=*/false));
  REQUIRE(st.cadBlockRefs.size() == 1);
  CHECK(st.cadBlockRefs[0].defName == "*U1");
  REQUIRE(st.blockDefs.size() == 1);
  CHECK(st.blockDefs[0].name == "*U1");
  std::vector<CadBlockWorldSeg> segs;
  CadBlockCollectWorldLines(st.blockDefs, st.cadBlockRefs[0], EntityAttributes{}, &segs);
  REQUIRE(segs.size() == 1);
  const float span =
      std::hypot(segs[0].x1 - segs[0].x0, segs[0].y1 - segs[0].y0);
  CHECK(span == Catch::Approx(5.f).margin(0.01f));
  CHECK(st.userLinesFlat.empty());
}

TEST_CASE("Named block INSERT re-imports as cadBlockRef without trailer (issue #622)", "[dwg][libredwg][issue622]") {
  ScratchDir dir("dwg-block-insert-ref");
  const auto p = (dir.path / "blk.dwg").string();
  AppCommandState st;
  CadBlockDefinition def;
  def.name = "ANNO_SYM";
  def.content.lines = {0.f, 0.f, 0.f, 1.f, 0.f, 0.f};
  def.content.lineAttrs.push_back(EntityAttributes{});
  st.blockDefs.push_back(std::move(def));
  CadBlockRef ref;
  ref.defName = "ANNO_SYM";
  ref.annotative = true;
  ref.xf.x = 5.f;
  ref.xf.y = 10.f;
  st.cadBlockRefs.push_back(std::move(ref));
  st.cadBlockRefAttrs.push_back(EntityAttributes{});
  std::vector<std::string> log;
  REQUIRE(ExportLibreCadFile(st, p.c_str(), log, /*asDxf=*/false));
  AppCommandState in;
  REQUIRE(ImportLibreCadFile(in, p.c_str(), log, /*asDxf=*/false));
  REQUIRE(in.blockDefs.size() == 1);
  CHECK(in.blockDefs[0].name == "ANNO_SYM");
  REQUIRE(in.cadBlockRefs.size() == 1);
  CHECK(in.cadBlockRefs[0].defName == "ANNO_SYM");
  CHECK(in.cadBlockRefs[0].xf.x == Catch::Approx(5.f));
  CHECK(in.cadBlockRefs[0].xf.y == Catch::Approx(10.f));
  CHECK(in.cadBlockRefs[0].annotative);
  CHECK(in.userLinesFlat.empty());
}

TEST_CASE("Paper-space named INSERT round-trips as paperBlockRef (issue #622)", "[dwg][libredwg][issue622]") {
  ScratchDir dir("dwg-paper-block-insert");
  const auto p = (dir.path / "paper-blk.dwg").string();
  AppCommandState st;
  CadBlockDefinition def;
  def.name = "TITLE_BLK";
  def.content.lines = {0.f, 0.f, 0.f, 2.f, 0.f, 0.f};
  def.content.lineAttrs.push_back(EntityAttributes{});
  st.blockDefs.push_back(std::move(def));
  PaperLayout sheet;
  sheet.name = "Sheet1";
  CadBlockRef ref;
  ref.defName = "TITLE_BLK";
  ref.annotative = true;
  ref.xf.x = 1.25f;
  ref.xf.y = 0.5f;
  ref.xf.rotZ = 0.25f;
  sheet.paperBlockRefs.push_back(std::move(ref));
  sheet.paperBlockRefAttrs.push_back(EntityAttributes{});
  st.paperLayouts.push_back(std::move(sheet));
  std::vector<std::string> log;
  REQUIRE(ExportLibreCadFile(st, p.c_str(), log, /*asDxf=*/false));
  AppCommandState in;
  REQUIRE(ImportLibreCadFile(in, p.c_str(), log, /*asDxf=*/false));
  REQUIRE(in.paperLayouts.size() == 1);
  REQUIRE(in.paperLayouts[0].paperBlockRefs.size() == 1);
  CHECK(in.paperLayouts[0].paperBlockRefs[0].defName == "TITLE_BLK");
  CHECK(in.paperLayouts[0].paperBlockRefs[0].xf.x == Catch::Approx(1.25f));
  CHECK(in.paperLayouts[0].paperBlockRefs[0].xf.y == Catch::Approx(0.5f));
  CHECK(in.paperLayouts[0].paperBlockRefs[0].xf.rotZ == Catch::Approx(0.25f).margin(0.001f));
  CHECK(in.paperLayouts[0].paperBlockRefs[0].annotative);
  CHECK(in.cadBlockRefs.empty());
}

TEST_CASE("Nested block INSERT in definition round-trips through DWG (issue #622)", "[dwg][libredwg][issue622]") {
  ScratchDir dir("dwg-nested-block-insert");
  const auto p = (dir.path / "nested-blk.dwg").string();
  AppCommandState st;
  CadBlockDefinition leaf;
  leaf.name = "NEST_LEAF";
  leaf.content.lines = {0.f, 0.f, 0.f, 1.f, 0.f, 0.f};
  leaf.content.lineAttrs.push_back(EntityAttributes{});
  CadBlockDefinition parent;
  parent.name = "NEST_PARENT";
  parent.content.lines = {0.f, 0.f, 0.f, 0.f, 1.f, 0.f};
  parent.content.lineAttrs.push_back(EntityAttributes{});
  CadBlockNested nested;
  nested.defName = "NEST_LEAF";
  nested.xf.x = 4.f;
  nested.xf.y = 2.f;
  nested.xf.rotZ = 0.5f;
  parent.content.nested.push_back(std::move(nested));
  st.blockDefs.push_back(std::move(leaf));
  st.blockDefs.push_back(std::move(parent));
  CadBlockRef top;
  top.defName = "NEST_PARENT";
  top.xf.x = 10.f;
  top.xf.y = 20.f;
  st.cadBlockRefs.push_back(std::move(top));
  st.cadBlockRefAttrs.push_back(EntityAttributes{});
  std::vector<std::string> log;
  REQUIRE(ExportLibreCadFile(st, p.c_str(), log, /*asDxf=*/false));
  Dwg_Data exported;
  std::memset(&exported, 0, sizeof(exported));
  REQUIRE(dwg_read_file(p.c_str(), &exported) < DWG_ERR_CRITICAL);
  Dwg_Object* parentBlkObj = nullptr;
  int nestedLeafInserts = 0;
  for (BITCODE_BL i = 0; i < exported.num_objects; ++i) {
    const Dwg_Object* o = &exported.object[i];
    if (o->fixedtype == DWG_TYPE_BLOCK_HEADER && o->tio.object != nullptr &&
        o->tio.object->tio.BLOCK_HEADER != nullptr) {
      const std::string nm =
          libredwgcad_detail::DecodeDwgString(o->tio.object->tio.BLOCK_HEADER->name, false);
      if (nm == "NEST_PARENT")
        parentBlkObj = const_cast<Dwg_Object*>(o);
    }
    if (o->fixedtype != DWG_TYPE_INSERT || o->tio.entity == nullptr || o->tio.entity->tio.INSERT == nullptr)
      continue;
    const Dwg_Entity_INSERT* ins = o->tio.entity->tio.INSERT;
    if (ins->block_header == nullptr)
      continue;
    Dwg_Object* blk = dwg_resolve_handle_silent(&exported, ins->block_header->absolute_ref);
    if (blk == nullptr || blk->tio.object == nullptr || blk->tio.object->tio.BLOCK_HEADER == nullptr)
      continue;
    const std::string refName =
        libredwgcad_detail::DecodeDwgString(blk->tio.object->tio.BLOCK_HEADER->name, false);
    if (refName == "NEST_LEAF")
      ++nestedLeafInserts;
  }
  int ownedNestedInserts = 0;
  if (parentBlkObj != nullptr) {
    for (Dwg_Object* e = get_first_owned_entity(parentBlkObj); e != nullptr;
         e = get_next_owned_entity(parentBlkObj, e)) {
      if (e->fixedtype == DWG_TYPE_INSERT)
        ++ownedNestedInserts;
    }
  }
  dwg_free(&exported);
  REQUIRE(nestedLeafInserts >= 1);
  REQUIRE(ownedNestedInserts == 1);

  AppCommandState in;
  REQUIRE(ImportLibreCadFile(in, p.c_str(), log, /*asDxf=*/false));
  REQUIRE(in.blockDefs.size() == 2);
  const CadBlockDefinition* parentIn = nullptr;
  for (const CadBlockDefinition& d : in.blockDefs) {
    if (d.name == "NEST_PARENT")
      parentIn = &d;
  }
  REQUIRE(parentIn != nullptr);
  REQUIRE(parentIn->content.nested.size() == 1);
  CHECK(parentIn->content.nested[0].defName == "NEST_LEAF");
  CHECK(parentIn->content.nested[0].xf.x == Catch::Approx(4.f));
  CHECK(parentIn->content.nested[0].xf.y == Catch::Approx(2.f));
  CHECK(parentIn->content.nested[0].xf.rotZ == Catch::Approx(0.5f).margin(0.001f));
  REQUIRE(in.cadBlockRefs.size() == 1);
  CHECK(in.cadBlockRefs[0].defName == "NEST_PARENT");
}

TEST_CASE("Native DWG nested INSERT in block definition imports as nested (issue #622)",
          "[dwg][libredwg][issue622]") {
  ScratchDir dir("dwg-native-nested");
  const auto p = (dir.path / "native-nested.dwg").string();
  Dwg_Data* dwg = dwg_new_Document(R_2000, 0, 0);
  REQUIRE(dwg != nullptr);
  Dwg_Object* m = dwg_model_space_object(dwg);
  REQUIRE(m != nullptr);
  auto* mhdr = m->tio.object->tio.BLOCK_HEADER;
  REQUIRE(mhdr != nullptr);

  Dwg_Object_BLOCK_HEADER* leafHdr = dwg_add_BLOCK_HEADER(dwg, "CHILD_BLK");
  REQUIRE(leafHdr != nullptr);
  dwg_add_BLOCK(leafHdr, "CHILD_BLK");
  dwg_point_3d a{0.0, 0.0, 0.0};
  dwg_point_3d b{1.0, 0.0, 0.0};
  dwg_add_LINE(leafHdr, &a, &b);
  dwg_add_ENDBLK(leafHdr);

  Dwg_Object_BLOCK_HEADER* parentHdr = dwg_add_BLOCK_HEADER(dwg, "PARENT_BLK");
  REQUIRE(parentHdr != nullptr);
  dwg_add_BLOCK(parentHdr, "PARENT_BLK");
  dwg_point_3d insNested{3.0, 4.0, 0.0};
  REQUIRE(dwg_add_INSERT(parentHdr, &insNested, "CHILD_BLK", 1.0, 1.0, 1.0, 0.0) != nullptr);
  dwg_add_ENDBLK(parentHdr);

  dwg_point_3d insTop{10.0, 20.0, 0.0};
  REQUIRE(dwg_add_INSERT(mhdr, &insTop, "PARENT_BLK", 1.0, 1.0, 1.0, 0.0) != nullptr);

  REQUIRE(dwg_write_file(p.c_str(), dwg) == 0);
  dwg_free(dwg);
  std::free(dwg);

  AppCommandState in;
  std::vector<std::string> log;
  REQUIRE(ImportLibreCadFile(in, p.c_str(), log, /*asDxf=*/false));
  const CadBlockDefinition* parentIn = nullptr;
  for (const CadBlockDefinition& d : in.blockDefs) {
    if (d.name == "PARENT_BLK")
      parentIn = &d;
  }
  REQUIRE(parentIn != nullptr);
  REQUIRE(parentIn->content.nested.size() == 1);
  CHECK(parentIn->content.nested[0].defName == "CHILD_BLK");
  CHECK(parentIn->content.nested[0].xf.x == Catch::Approx(3.f));
  CHECK(parentIn->content.nested[0].xf.y == Catch::Approx(4.f));
  REQUIRE(in.cadBlockRefs.size() == 1);
  CHECK(in.cadBlockRefs[0].defName == "PARENT_BLK");
}

TEST_CASE("GsIo syncs missing currentAnnotationScaleIndex on load (issue #622)", "[issue622][gsio]") {
  AppCommandState src;
  src.modelUnitsPerPlottedInch = 25.f;
  CadAnnotationScale s;
  s.name = "1:25";
  s.paperUnits = 1.f;
  s.drawingUnits = 25.f;
  src.annotationScales.push_back(s);
  src.currentAnnotationScaleIndex = 0;
  std::vector<std::string> log;
  std::string json = SerializeGoSurveyJson(src);
  const auto pos = json.find("\"currentAnnotationScaleIndex\"");
  if (pos != std::string::npos) {
    const auto lineEnd = json.find('\n', pos);
    json.erase(pos, lineEnd == std::string::npos ? std::string::npos : lineEnd - pos + 1);
  }
  AppCommandState back;
  REQUIRE(LoadGoSurveyFromJsonUtf8(back, json, log));
  REQUIRE(back.annotationScales.size() == 1);
  REQUIRE(back.currentAnnotationScaleIndex == 0);
}

TEST_CASE("MultileaderStyle persists through GsIo JSON (issue #619)", "[issue619][gsio]") {
  AppCommandState src;
  src.activeMultileaderStyle.textSizeInches = 0.14f;
  src.activeMultileaderStyle.arrowSizeInches = 0.12f;
  std::vector<std::string> log;
  AppCommandState back;
  REQUIRE(LoadGoSurveyFromJsonUtf8(back, SerializeGoSurveyJson(src), log));
  CHECK(back.activeMultileaderStyle.textSizeInches == Catch::Approx(0.14f));
  CHECK(back.activeMultileaderStyle.arrowSizeInches == Catch::Approx(0.12f));
}

TEST_CASE("CadMultileader extra leader paths persist through GsIo (issue #619)",
          "[issue619][gsio]") {
  AppCommandState src;
  CadMultileader ml{};
  ml.pathXyz = {0.f, 0.f, 0.f, 10.f, 0.f, 0.f};
  ml.extraLeaderPaths.push_back({5.f, 8.f, 0.f, 10.f, 0.f, 0.f});
  ml.label.kind = CadAnnotation::Kind::Mtext;
  ml.label.insX = 10.f;
  ml.label.insY = 0.f;
  ml.label.text = "Callout";
  src.cadMultileaders.push_back(std::move(ml));
  src.cadMultileaderAttrs.push_back(EntityAttributes{});
  std::vector<std::string> log;
  AppCommandState back;
  REQUIRE(LoadGoSurveyFromJsonUtf8(back, SerializeGoSurveyJson(src), log));
  REQUIRE(back.cadMultileaders.size() == 1);
  REQUIRE(back.cadMultileaders[0].extraLeaderPaths.size() == 1);
  CHECK(back.cadMultileaders[0].extraLeaderPaths[0].size() == 6);
}

TEST_CASE("CadMultileader persists through GsIo JSON round trip (REQ-367, issue #619)",
          "[issue619][gsio]") {
  AppCommandState src;
  CadMultileader ml{};
  ml.pathXyz = {1.f, 2.f, 0.f, 11.f, 2.f, 0.f};
  ml.label.kind = CadAnnotation::Kind::Mtext;
  ml.label.insX = 11.f;
  ml.label.insY = 2.f;
  ml.label.text = "Easement callout";
  ml.label.boxMinX = 11.f;
  ml.label.boxMinY = 1.f;
  ml.label.boxMaxX = 22.f;
  ml.label.boxMaxY = 2.5f;
  src.cadMultileaders.push_back(std::move(ml));
  EntityAttributes at{};
  at.layer = "C-ANNO-TEXT";
  src.cadMultileaderAttrs.push_back(at);
  std::vector<std::string> log;
  AppCommandState back;
  REQUIRE(LoadGoSurveyFromJsonUtf8(back, SerializeGoSurveyJson(src), log));
  REQUIRE(back.cadMultileaders.size() == 1);
  REQUIRE(back.cadMultileaderAttrs.size() == 1);
  CHECK(back.cadMultileaders[0].pathXyz.size() == 6);
  CHECK(back.cadMultileaders[0].label.text == "Easement callout");
  CHECK(back.cadMultileaderAttrs[0].layer == "C-ANNO-TEXT");
}

TEST_CASE("ExportDwgFile writes a CadTable as grid lines and MTEXT (issue #616)",
          "[dwg][libredwg][issue616]") {
  ScratchDir dir("dwg-table-export");
  const auto p = (dir.path / "table.dwg").string();
  AppCommandState st;
  CadTable table;
  table.cols = 2;
  table.width = 20.f;
  table.height = 8.f;
  table.cells = {"A1", "B1", "A2", "B2"};
  st.cadTables.push_back(table);
  st.cadTableAttrs.push_back(EntityAttributes{});
  std::vector<std::string> log;
  REQUIRE(ExportLibreCadFile(st, p.c_str(), log, /*asDxf=*/false));
  REQUIRE(CountDwgEntities(DWG_TYPE_LINE, p.c_str()) >= 3);
  REQUIRE(CountDwgEntities(DWG_TYPE_MTEXT, p.c_str()) >= 4);
}

TEST_CASE("DWG export loss omits exportable tables (issue #616 / #614)", "[dwg][libredwg][issue616][issue614]") {
  AppCommandState st;
  CadTable table;
  table.cols = 2;
  table.cells = {"x", "y"};
  st.cadTables.push_back(table);
  for (const DwgExportLoss& l : ComputeDwgExportLosses(st)) {
    CHECK(l.label.find("table") == std::string::npos);
  }
}

TEST_CASE("ExportDwgFile writes a point-cloud extent box (issue #621)", "[dwg][libredwg][issue621]") {
  ScratchDir dir("dwg-pc-box");
  const auto p = (dir.path / "pc.dwg").string();
  AppCommandState st;
  auto pc = std::make_shared<CadPointCloud>();
  pc->sourcePath = "C:/surveys/site.e57";
  pc->pointsXyz = {0.0, 0.0, 0.0, 10.0, 5.0, 3.0};
  st.cadPointClouds.push_back(pc);
  st.cadPointCloudAttrs.push_back(EntityAttributes{});
  std::vector<std::string> log;
  REQUIRE(ExportLibreCadFile(st, p.c_str(), log, /*asDxf=*/false));
  CHECK(CountDwgEntities(DWG_TYPE_LINE, p.c_str()) == 12);
  for (const DwgExportLoss& l : ComputeDwgExportLosses(st))
    CHECK(l.label.find("point cloud") == std::string::npos);
}

// REQ-101 (D-2026-09-08-i) / ADR-054 Phase B (#441): the DWG-trailer document is the same `double`
// GsIo JSON tree as `.gst` (Phase A widened the stores it reads/writes) — a state-plane-magnitude
// coordinate must survive the trailer round trip within ±0.002 ft, not the pre-migration ±0.01 ft.
TEST_CASE("DWG trailer round-trips a state-plane coordinate within REQ-101 tolerance",
          "[dwg][libredwg][req101]") {
  ScratchDir dir("dwg-req101");
  const auto p = (dir.path / "stateplane.dwg").string();
  AppCommandState st;
  st.userLinesFlat = {2034567.891234, 891234.567891, 0.0, 2034577.891234, 891234.567891, 0.0};
  st.userLineAttrs = {EntityAttributes{}};
  std::vector<std::string> log;
  REQUIRE(ExportDwgFile(st, p.c_str(), log));
  AppCommandState in;
  REQUIRE(ImportDwgFile(in, p.c_str(), log));
  REQUIRE(in.userLinesFlat.size() == 6);
  // Local storage invariant: world = local + worldDocumentOrigin (a state-plane-magnitude
  // coordinate rebases on load, per CadCoordinateFrame — that is unrelated to REQ-101's precision
  // guarantee, which this test checks in world space).
  CHECK(in.userLinesFlat[0] + in.worldDocumentOriginX == Catch::Approx(2034567.891234).margin(0.002));
  CHECK(in.userLinesFlat[1] + in.worldDocumentOriginY == Catch::Approx(891234.567891).margin(0.002));
  CHECK(in.userLinesFlat[3] + in.worldDocumentOriginX == Catch::Approx(2034577.891234).margin(0.002));
}

// A DWG saved by a pre-migration build stored coordinates as `float` before writing the trailer
// JSON, so its text already carries only `float` resolution (~0.008 ft at state-plane magnitude).
// Loading such a file today must not error and must still land within the old, documented ±0.01 ft
// — the trailer JSON shape did not change, so there is no format-version gate to fail open on.
TEST_CASE("A legacy float-precision DWG trailer still loads within the old REQ-101 tolerance",
          "[dwg][libredwg][req101]") {
  ScratchDir dir("dwg-legacy");
  const auto p = (dir.path / "legacy.dwg").string();

  const double trueX = 2034567.891234;
  const double trueY = 891234.567891;
  const double legacyX = static_cast<double>(static_cast<float>(trueX));
  const double legacyY = static_cast<double>(static_cast<float>(trueY));

  AppCommandState legacy;
  legacy.userLinesFlat = {legacyX, legacyY, 0.0, legacyX + 10.0, legacyY, 0.0};
  legacy.userLineAttrs = {EntityAttributes{}};
  const std::string json = SerializeGoSurveyJson(legacy);

  // Mirrors DwgIo.cpp's private trailer layout (REQ-175 / ADR-044): a placeholder "DWG" prefix +
  // JSON document + 8-byte little-endian length + the 16-byte magic. TryGoSurveyDwgPayloadFromBytes
  // only inspects the trailer, so the prefix need not be real LibreDWG bytes.
  static constexpr unsigned char kMagic[16] = {'G', 'O', 'S', 'U', 'R', 'V', 'E', 'Y',
                                                '_', 'D', 'O', 'C', 'v', '1', '\n', '\0'};
  {
    std::ofstream f(p, std::ios::binary);
    f << "not-a-real-dwg-prefix";
    f.write(json.data(), static_cast<std::streamsize>(json.size()));
    std::uint64_t n = static_cast<std::uint64_t>(json.size());
    unsigned char b[8];
    for (int i = 0; i < 8; ++i)
      b[static_cast<size_t>(i)] = static_cast<unsigned char>((n >> (8 * i)) & 0xFFu);
    f.write(reinterpret_cast<const char*>(b), 8);
    f.write(reinterpret_cast<const char*>(kMagic), sizeof(kMagic));
  }

  AppCommandState in;
  std::vector<std::string> log;
  REQUIRE(ImportDwgFile(in, p.c_str(), log));
  REQUIRE(in.userLinesFlat.size() == 6);
  // Local storage invariant: world = local + worldDocumentOrigin.
  CHECK(in.userLinesFlat[0] + in.worldDocumentOriginX == Catch::Approx(trueX).margin(0.01));
  CHECK(in.userLinesFlat[1] + in.worldDocumentOriginY == Catch::Approx(trueY).margin(0.01));
}

// REQ-101 Phase F (#447): SurveyPoint::easting/northing/elevation widened `float` -> `double`. A
// survey point round-tripped through DXF POINT + GOSURVEY XDATA (1071 id / 1070 labelStyle / 1000
// desc, the format ADR-005/REQ-023 use) must land within +/-0.002 ft of a state-plane-magnitude
// value — the same guarantee Phase B already proved for plain LINE/CIRCLE geometry. Before this
// phase, DxfIo.cpp's reader narrowed `wx - worldDocumentOriginX` through `static_cast<float>` when
// rebuilding the point (float resolves ~0.008 ft at this magnitude), so this assertion would have
// failed at the old code with a margin tighter than 0.008.
TEST_CASE("DXF survey point XDATA round-trips a state-plane coordinate within REQ-101 tolerance",
          "[dxf][libredwg][req101][survey]") {
  ScratchDir dir("dxf-survey-req101");
  const auto p = (dir.path / "surveypoint.dxf").string();

  AppCommandState st;
  SurveyPoint sp;
  sp.id = 501;
  sp.easting = 2034567.891234;
  sp.northing = 891234.567891;
  sp.elevation = 456.789123;
  sp.description = "IPF";
  sp.rawDescription = "IPF";
  sp.layer = "0";
  sp.labelStyle = SurveyPointLabelStyle::None;  // no MTEXT needed for this round trip
  st.surveyPoints.push_back(sp);

  std::vector<std::string> log;
  REQUIRE(ExportDxfFile(st, p.c_str(), log));

  AppCommandState in;
  REQUIRE(ImportDxfFile(in, p.c_str(), log));
  REQUIRE(in.surveyPoints.size() == 1);

  // Local storage invariant: world = local + worldDocumentOrigin (a state-plane-magnitude
  // coordinate rebases on import) — REQ-101's guarantee is checked in world space, as the DWG-trailer
  // test above does for plain geometry.
  // `epsilon(0.0)` disables Catch2's default RELATIVE tolerance (~1.2e-3 of the larger operand) —
  // at a ~2e6 magnitude that alone is +/-2000+ ft, which would swallow the 0.002 ft absolute margin
  // entirely and let the test pass regardless of the actual error.
  const SurveyPoint& got = in.surveyPoints[0];
  CHECK(got.easting + in.worldDocumentOriginX == Catch::Approx(sp.easting).margin(0.002).epsilon(0.0));
  CHECK(got.northing + in.worldDocumentOriginY == Catch::Approx(sp.northing).margin(0.002).epsilon(0.0));
  CHECK(got.elevation == Catch::Approx(sp.elevation).margin(0.002).epsilon(0.0));
  CHECK(got.id == sp.id);
}

// REQ-101 Phase F (#447): SurveyPoint::easting/northing widened `float` -> `double`. The CSV
// importer computes each point's LOCAL coordinate as `worldE - worldDocumentOriginX` and stores it
// BEFORE the post-import rebase (`MaybeRebaseLargeCoordinates`) runs — so on a fresh document
// (origin still (0,0)) a state-plane-magnitude easting is assigned to `SurveyPoint::easting` at its
// FULL magnitude, then rebased afterward. With `easting` as `float`, that first assignment alone
// quantizes at ~0.008 ft (float spacing at 2e6), and the later rebase only rearranges an already
//-quantized value — it cannot recover the lost precision. This is the same "narrow-before-origin"
// hazard `regression-req101-origin-at-entry` pins for typed LINE points; this test pins the CSV
// import path for survey points and would have failed at the pre-Phase-F `float` field (error
// ~0.008-0.025 ft, outside +/-0.002 ft) — proven by reverting the field to `float` locally and
// re-running, then restored.
TEST_CASE("CSV import stores a state-plane survey point within REQ-101 tolerance", "[csv][survey][req101]") {
  HeadlessImGuiScope imguiScope;
  ScratchDir dir("csv-survey-req101");
  const auto p = (dir.path / "points.csv").string();
  {
    std::ofstream f(p);
    // ENZ layout (no point-id column): E,N,Z. Same state-plane easting/northing
    // `regression-req101-origin-at-entry` uses for LINE — documented there to quantize to
    // 2000000.125 through a bare `float` cast (error 0.025 ft), so this value is known to expose
    // the hazard rather than happening to land within tolerance by luck.
    f << "2000000.10,500000.03,456.789123\n";
  }

  AppCommandState st;
  std::snprintf(st.surveyImportCsvPath, sizeof(st.surveyImportCsvPath), "%s", p.c_str());
  st.surveyImportCsvLayoutIdx = 3;  // ENZ (SurveyCsvLayoutFromUiIndex)
  st.surveyImportCsvSkipFirstRow = false;

  std::vector<std::string> log;
  REQUIRE(SurveyCsvImportFile(st, log));
  REQUIRE(st.surveyPoints.size() == 1);

  // Local storage invariant: world = local + worldDocumentOrigin — the point's magnitude triggers
  // the post-import rebase, so this checks the guarantee in world space, as the DWG-trailer test
  // above does for plain geometry.
  // `epsilon(0.0)`: see the DXF survey-point test above — without it Catch2's default RELATIVE
  // tolerance at this magnitude (~2000+ ft) would swallow the 0.002 ft margin entirely.
  const SurveyPoint& got = st.surveyPoints[0];
  CHECK(got.easting + st.worldDocumentOriginX == Catch::Approx(2000000.10).margin(0.002).epsilon(0.0));
  CHECK(got.northing + st.worldDocumentOriginY == Catch::Approx(500000.03).margin(0.002).epsilon(0.0));
  CHECK(got.elevation == Catch::Approx(456.789123).margin(0.002).epsilon(0.0));
}

TEST_CASE("DWG import refuses a non-DWG path", "[dwg][libredwg]") {
  ScratchDir dir("nodwg");
  const auto p = dir.path / "fake.dwg";
  {
    std::ofstream f(p, std::ios::binary);
    f << "not a dwg";
  }
  AppCommandState st;
  std::vector<std::string> log;
  REQUIRE_FALSE(ImportDwgFile(st, p.string().c_str(), log));
}

TEST_CASE("GoSurvey DWG preserves a survey point (REQ-175)", "[dwg][libredwg][req175]") {
  ScratchDir dir("svy");
  const auto p = (dir.path / "svy.dwg").string();
  AppCommandState st;
  OneLine(st);
  SurveyPoint pt;
  pt.id = 42;
  pt.easting = 100.f;
  pt.northing = 200.f;
  pt.elevation = 12.5f;
  pt.description = "IPF";
  pt.labelStyle = SurveyPointLabelStyle::None;
  st.surveyPoints.push_back(pt);
  std::vector<std::string> log;
  REQUIRE(ExportDwgFile(st, p.c_str(), log));
  AppCommandState in;
  REQUIRE(ImportDwgFile(in, p.c_str(), log));
  REQUIRE(in.surveyPoints.size() == 1);
  CHECK(in.surveyPoints[0].id == 42);
  CHECK(in.surveyPoints[0].easting == Catch::Approx(100.f).margin(0.05f));
  CHECK(in.surveyPoints[0].northing == Catch::Approx(200.f).margin(0.05f));
  CHECK(in.surveyPoints[0].elevation == Catch::Approx(12.5f).margin(0.05f));
  CHECK(in.surveyPoints[0].description == "IPF");
  REQUIRE(in.userLinesFlat.size() == 6);
}

// issue #167 — save staged the file beside the target then replaced it; the reopen-append race on
// the final (possibly sync-locked) path is gone, and no staging file is left behind.
TEST_CASE("DWG save overwrites an existing file with a full GoSurvey document (issue #167)",
          "[dwg][libredwg][issue167]") {
  ScratchDir dir("resave");
  const auto p = (dir.path / "resave.dwg").string();
  AppCommandState st;
  OneLine(st);
  SurveyPoint pt;
  pt.id = 7;
  pt.description = "REBAR";
  pt.labelStyle = SurveyPointLabelStyle::None;
  st.surveyPoints.push_back(pt);
  std::vector<std::string> log;

  REQUIRE(ExportDwgFile(st, p.c_str(), log));
  st.surveyPoints[0].description = "IPF";
  REQUIRE(ExportDwgFile(st, p.c_str(), log));  // overwrite the existing target

  CHECK_FALSE(std::filesystem::exists(p + ".gosurvey-save.tmp"));
  AppCommandState in;
  REQUIRE(ImportDwgFile(in, p.c_str(), log));
  REQUIRE(in.surveyPoints.size() == 1);
  CHECK(in.surveyPoints[0].description == "IPF");
}

#if defined(_WIN32)
// issue #167 follow-up — re-opening the same std::ofstream after a failed append (clear() only)
// could leave MSVC's stream locale null and crash on write. Hold the staged DWG exclusively,
// release after the first retry sleep, and assert the trailer still lands.
TEST_CASE("DWG trailer append retries through a briefly locked staged file", "[dwg][io][issue167]") {
  ScratchDir dir("append-lock");
  const std::string stagedUtf8 = (dir.path / "staged.dwg").u8string();
  AppCommandState st;
  OneLine(st);
  std::vector<std::string> log;
  REQUIRE(ExportLibreCadFile(st, stagedUtf8.c_str(), log, false));

  const std::wstring wstaged = std::filesystem::u8path(stagedUtf8).wstring();
  const HANDLE lock =
      CreateFileW(wstaged.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                  FILE_ATTRIBUTE_NORMAL, nullptr);
  REQUIRE(lock != INVALID_HANDLE_VALUE);

  std::thread releaser([lock] {
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    CloseHandle(lock);
  });

  REQUIRE(AppendGoSurveyPayloadToDwgFile(stagedUtf8.c_str(), st, log));
  releaser.join();

  AppCommandState loaded;
  REQUIRE(ImportDwgFile(loaded, stagedUtf8.c_str(), log));
  REQUIRE(loaded.userLinesFlat.size() == 6);
}
#endif

// issue #140 — the DWG layer table imported with garbled names / wrong colours / no linetypes.
TEST_CASE("LibreDWG decodes UTF-16LE (R2007+) table strings", "[dwg][libredwg][issue140]") {
  // The pre-fix code did std::string((char*)buf), which truncates a TU buffer at the first
  // NUL byte -> "P" instead of "Parcel Line", then the empty-name guard dropped the layer.
  const std::uint16_t wide[] = {'P', 'a', 'r', 'c', 'e', 'l', ' ', 'L', 'i', 'n', 'e', 0};
  CHECK(libredwgcad_detail::DecodeDwgString(wide, /*utf16le=*/true) == "Parcel Line");
  CHECK(libredwgcad_detail::DecodeDwgString("Parcel Line", /*utf16le=*/false) == "Parcel Line");
  CHECK(libredwgcad_detail::DecodeDwgString(nullptr, true).empty());
}

TEST_CASE("LibreDWG layer colour: negative ACI keeps its colour", "[dwg][libredwg][issue140]") {
  // method 0xc2 = entity/table default colour, index = signed ACI. A layer that is OFF stores a
  // negative ACI; the pre-fix code collapsed idx < 0 to "ByLayer", losing the colour.
  CHECK(libredwgcad_detail::ColorToStorage(3, 0xc2, 0) == "#00FF00");       // ACI 3 = green
  CHECK(libredwgcad_detail::ColorToStorage(-3, 0xc2, 0) == "#00FF00");      // off layer, same green
  CHECK(libredwgcad_detail::ColorToStorage(256, 0xc0, 0) == "ByLayer");
  CHECK(libredwgcad_detail::ColorToStorage(0, 0xc1, 0) == "ByBlock");
  CHECK(libredwgcad_detail::ColorToStorage(0, 0xc3, 0x1E90FFu) == "#1E90FF"); // true colour
  CHECK(libredwgcad_detail::ColorToStorage(0, 0xc3, 0) == "ByBlock");         // 0xc3 sentinel
  CHECK(libredwgcad_detail::ColorToStorage(256, 0xc3, 0x100u) == "ByLayer");  // 0xc3 sentinel
}

// issue #369 / D-2026-09-10-b — a Civil 3D parts-catalog file (pressure pipe / fitting /
// structure) stores no portable 3D geometry; its 3DSOLID is an empty placeholder and its class
// table is full of AECC_* custom classes. The importer names that skip for what it is rather than
// the ambiguous "3DSOLID(empty)".
TEST_CASE("Civil3D parts-catalog class signature is detected from the class table",
          "[dwg][libredwg][issue369]") {
  Dwg_Class classes[3] = {};
  classes[0].dxfname = const_cast<char*>("ACDBDICTIONARYWDFLT");
  classes[1].dxfname = const_cast<char*>("AECC_PRESSURE_PIPE");
  classes[2].dxfname = const_cast<char*>("AECC_FITTING_STYLE");

  Dwg_Data dwg = {};
  dwg.num_classes = 3;
  dwg.dwg_class = classes;
  CHECK(libredwgcad_detail::DwgHasCivil3dCatalogClasses(&dwg));

  // A plain drawing (no AECC_* classes) is not flagged.
  Dwg_Class plain[2] = {};
  plain[0].dxfname = const_cast<char*>("ACDBDICTIONARYWDFLT");
  plain[1].dxfname = const_cast<char*>("LWPOLYLINE");
  Dwg_Data ordinary = {};
  ordinary.num_classes = 2;
  ordinary.dwg_class = plain;
  CHECK_FALSE(libredwgcad_detail::DwgHasCivil3dCatalogClasses(&ordinary));

  // No class table, and a null drawing, are both safe.
  Dwg_Data empty = {};
  CHECK_FALSE(libredwgcad_detail::DwgHasCivil3dCatalogClasses(&empty));
  CHECK_FALSE(libredwgcad_detail::DwgHasCivil3dCatalogClasses(nullptr));
}

// issue #140 / DEBT-151-a — end-to-end against a real LibreDWG-decoded file: a multi-layer table
// must import names, colours (incl. off-layer negative ACI), assigned linetypes and freeze/lock
// flags intact. Fixture is R2000 because LibreDWG 0.13.4's own encoder does not round-trip
// R2004+ (a real R2018 fixture is the remaining half of DEBT-151-a); the UTF-16LE name path is
// covered by the DecodeDwgString case above.
TEST_CASE("LibreDWG imports a multi-layer table end to end", "[dwg][libredwg][issue140]") {
  ScratchDir dir("layers");
  const auto p = (dir.path / "layers.dwg").string();

  {
    Dwg_Data* dwg = dwg_new_Document(R_2000, /*imperial=*/0, /*loglevel=*/0);
    REQUIRE(dwg != nullptr);

    Dwg_Object_LTYPE* dashed = dwg_add_LTYPE(dwg, "DASHED");
    REQUIRE(dashed != nullptr);
    Dwg_Object* dashedObj = &dwg->object[dashed->parent->objid];

    struct Spec {
      const char* name;
      int16_t aci;      // signed: negative == layer off
      bool on;
      bool frozen;
      bool locked;
      bool dashed;
    };
    const Spec specs[] = {
        {"Parcel Line", 3, true, false, false, true},
        {"EXISTING-CONTOUR", 8, true, false, true, false},
        {"Utilities", -1, false, false, false, false},  // off -> negative ACI
        {"FROZEN LAYER", 5, true, true, false, false},
    };
    for (const Spec& s : specs) {
      Dwg_Object_LAYER* ly = dwg_add_LAYER(dwg, s.name);
      REQUIRE(ly != nullptr);
      ly->color.index = s.aci;
      ly->color.method = DWG_COLOR_METHOD_ACI;
      ly->off = s.on ? 0 : 1;
      ly->frozen = s.frozen ? 1 : 0;
      ly->locked = s.locked ? 1 : 0;
      // R2000 encode serialises the packed flag0 bits, not the decoded booleans.
      ly->flag0 = static_cast<BITCODE_BS>((s.frozen ? 1 : 0) | (s.on ? 2 : 0) | (s.locked ? 8 : 0) | 16);
      if (s.dashed)
        ly->ltype = dwg_add_handleref(dwg, 5, dashedObj->handle.value, dashedObj);
    }

    const int werr = dwg_write_file(p.c_str(), dwg);
    dwg_free(dwg);
    std::free(dwg);
    REQUIRE(werr == 0);
  }

  AppCommandState in;
  std::vector<std::string> log;
  const bool ok = ImportDwgFile(in, p.c_str(), log);
  for (const std::string& l : log)
    UNSCOPED_INFO(l);
  REQUIRE(ok);

  auto row = [&](const char* n) -> const CadLayerRow* {
    for (const CadLayerRow& r : in.drawingLayerTable)
      if (r.name == n)
        return &r;
    return nullptr;
  };

  REQUIRE(row("Parcel Line") != nullptr);        // TU name decoded, not truncated to "P"
  REQUIRE(row("EXISTING-CONTOUR") != nullptr);
  REQUIRE(row("Utilities") != nullptr);
  REQUIRE(row("FROZEN LAYER") != nullptr);

  CHECK(row("Parcel Line")->color == "#00FF00");
  CHECK(row("Parcel Line")->linetype == "DASHED");   // resolved from the LTYPE handle
  CHECK(row("EXISTING-CONTOUR")->locked);
  CHECK(row("EXISTING-CONTOUR")->linetype == "Continuous");
  CHECK(row("Utilities")->color == "#FF0000");        // ACI 1, recovered from the negative index
  CHECK(row("FROZEN LAYER")->frozen);
  CHECK_FALSE(row("FROZEN LAYER")->locked);
}

// DEBT-151-b — the DWG writer must emit the layer table and wire each entity to its layer /
// colour / linetype, not just the geometry.
TEST_CASE("DWG export writes the layer table and per-entity layer (DEBT-151-b)",
          "[dwg][libredwg][issue140]") {
  ScratchDir dir("layerexport");
  const auto p = (dir.path / "out.dwg").string();

  AppCommandState st;
  CadLayerRow contour;
  contour.name = "V-CONTOUR";
  contour.color = "#00FF00";
  contour.linetype = "DASHED";
  contour.locked = true;
  st.drawingLayerTable.push_back(contour);
  CadLayerRow border;
  border.name = "BORDER";
  border.color = "Red";
  st.drawingLayerTable.push_back(border);

  st.userLinesFlat = {0.f, 0.f, 0.f, 10.f, 5.f, 0.f};
  EntityAttributes at;
  at.layer = "V-CONTOUR";
  at.color = "ByLayer";
  at.linetype = "ByLayer";
  st.userLineAttrs = {at};

  std::vector<std::string> log;
  // Bypass the GoSurvey payload so this exercises the CAD-level layer writer (the path a foreign
  // reader / a non-GoSurvey DWG hits).
  REQUIRE(ExportLibreCadFile(st, p.c_str(), log, /*asDxf=*/false));

  AppCommandState in;
  REQUIRE(ImportDwgFile(in, p.c_str(), log));

  auto row = [&](const char* n) -> const CadLayerRow* {
    for (const CadLayerRow& r : in.drawingLayerTable)
      if (r.name == n)
        return &r;
    return nullptr;
  };
  REQUIRE(row("V-CONTOUR") != nullptr);
  REQUIRE(row("BORDER") != nullptr);
  CHECK(row("V-CONTOUR")->color == "#00FF00");
  CHECK(row("V-CONTOUR")->linetype == "DASHED");
  CHECK(row("V-CONTOUR")->locked);
  CHECK(row("BORDER")->color == "#FF0000");

  REQUIRE(in.userLineAttrs.size() == 1);
  CHECK(in.userLineAttrs[0].layer == "V-CONTOUR");
}

// Save-As crash (issue #167 follow-up): TableWriter cached Dwg_Object* into dwg->object[], which
// LibreDWG can reallocate while later dwg_add_ARC calls run. Re-resolve by objid instead.
TEST_CASE("DWG export keeps layer handles valid after many entities (issue #167)",
          "[dwg][libredwg][issue140]") {
  ScratchDir dir("layerexport-realloc");
  const auto p = (dir.path / "many-arcs.dwg").string();

  AppCommandState st;
  CadLayerRow contour;
  contour.name = "V-CONTOUR";
  contour.color = "#00FF00";
  contour.linetype = "DASHED";
  st.drawingLayerTable.push_back(contour);
  CadLayerRow border;
  border.name = "BORDER";
  border.color = "Red";
  border.linetype = "CENTER";
  st.drawingLayerTable.push_back(border);

  EntityAttributes at;
  at.layer = "V-CONTOUR";
  at.color = "ByLayer";
  at.linetype = "DASHED";

  for (int i = 0; i < 200; ++i) {
    CadArc arc;
    arc.cx = static_cast<double>(i);
    arc.cy = 0.0;
    arc.r = 1.0;
    arc.startRad = 0.f;
    arc.sweepRad = 3.14159265f;
    st.userArcs.push_back(arc);
    st.userArcAttrs.push_back(at);
  }

  std::vector<std::string> log;
  REQUIRE(ExportLibreCadFile(st, p.c_str(), log, /*asDxf=*/false));

  AppCommandState in;
  REQUIRE(ImportDwgFile(in, p.c_str(), log));
  REQUIRE(in.userArcs.size() == 200);
  REQUIRE(in.userArcAttrs.size() == 200);
  CHECK(in.userArcAttrs[0].layer == "V-CONTOUR");
  CHECK(in.userArcAttrs[0].linetype == "DASHED");
}

// issue #160 / DEBT-151-a — end-to-end against a genuine AutoCAD 2018 (AC1032, from_version
// R_2018) file, committed as samples/duke-main-clean-r2018.dwg (a real survey/topo drawing:
// 154 named layers, ~2300 lines, ~1500 polylines). LibreDWG 0.13.4's own encoder cannot produce
// an R2004+ file its decoder can re-read, so a real committed fixture is the only way to run the
// UTF-16LE (BITCODE_TU) name-decode path (IS_FROM_TU_DWG, from_version >= R_2007) end to end
// rather than via the hand-built uint16_t buffer in the DecodeDwgString unit case above.
//
// Fixture path: GOSURVEY_SAMPLES_DIR is defined on this target by CMakeLists.txt.
// Note: this drawing has no off / frozen / locked layers — those flags stay covered by the
// R2000 "multi-layer table end to end" case. AutoCAD-visual verification of the colours is out
// of scope here (no AutoCAD/ODA in the build env; REQ-170).
namespace {
std::string SamplePath(const char* name) {
  return std::string(GOSURVEY_SAMPLES_DIR) + "/" + name;
}
}  // namespace

TEST_CASE("LibreDWG imports the full layer table of a real R2018 DWG (issue #160)",
          "[dwg][libredwg][issue140][issue160]") {
  const std::string p = SamplePath("duke-main-clean-r2018.dwg");
  REQUIRE(std::filesystem::exists(p));

  // Criterion 4: the fixture really is R2007+, so the BITCODE_TU decode branch runs.
  CHECK(DwgVersionName(p.c_str()) == "AutoCAD 2018");
  {
    Dwg_Data dwg;
    std::memset(&dwg, 0, sizeof(dwg));
    REQUIRE(dwg_read_file(p.c_str(), &dwg) < DWG_ERR_CRITICAL);
    CHECK(dwg.header.from_version >= R_2007);
    CHECK(dwg.header.from_version == R_2018);
    dwg_free(&dwg);
  }

  AppCommandState in;
  std::vector<std::string> log;
  const bool ok = ImportDwgFile(in, p.c_str(), log);
  for (const std::string& l : log) UNSCOPED_INFO(l);
  REQUIRE(ok);

  bool sawVersion = false, sawCount = false;
  for (const std::string& l : log) {
    if (l.find("AutoCAD 2018") != std::string::npos) sawVersion = true;
    if (l.find("154 layer(s)") != std::string::npos) sawCount = true;
  }
  CHECK(sawVersion);
  CHECK(sawCount);

  // 154 imported + the always-present default layer "0".
  CHECK(in.drawingLayerTable.size() == 155);

  auto row = [&](const char* n) -> const CadLayerRow* {
    for (const CadLayerRow& r : in.drawingLayerTable)
      if (r.name == n) return &r;
    return nullptr;
  };

  // Names: multi-word and punctuated names decode intact (not truncated at the first UTF-16 NUL).
  REQUIRE(row("1 Node") != nullptr);
  REQUIRE(row("0 Center Lines") != nullptr);
  REQUIRE(row("JM- Ehouse 4") != nullptr);
  REQUIRE(row("CJ-TOPO-DRAINLINE") != nullptr);
  REQUIRE(row("C-FIRE-PIPE-12IN") != nullptr);

  // Colours: 0xc3-encoded indexed colours resolve through the ACI palette (regression: they used
  // to import as near-black #0000NN); 0xc2 colours keep their true-colour RGB.
  CHECK(row("1 Node")->color == "#FF0000");             // ACI 10
  CHECK(row("0 Center Lines")->color == "#BFFF00");     // ACI 60
  CHECK(row("CJ-TOPO-DRAINLINE")->color == "#FF007F");  // ACI 230
  CHECK(row("C-FIRE-PIPE-12IN")->color == "#00BFFF");   // ACI 140
  CHECK(row("C-TOPO-MAJR")->color == "#0000FF");        // 0xc2 true colour, unchanged
  CHECK(row("V-SSWR-PIPE")->color == "#BF00FF");        // 0xc2 true colour
  CHECK(row("C-PRKG-STRP")->color == "ByLayer");

  // Linetypes: the layer's assigned LTYPE name is imported, not forced to Continuous.
  CHECK(row("0 Center Lines")->linetype == "CENTER2");
  CHECK(row("CJ-TOPO-DRAINLINE")->linetype == "PHANTOM2");
  CHECK(row("C-FIRE-PIPE-12IN")->linetype == "UT-FIRE12''");
  CHECK(row("C-FENC")->linetype == "Fence");
  CHECK(row("1 Node")->linetype == "Continuous");

  // Flags: every layer in this drawing is on / thawed / unlocked.
  for (const CadLayerRow& r : in.drawingLayerTable) {
    CHECK(r.on);
    CHECK_FALSE(r.frozen);
    CHECK_FALSE(r.locked);
  }

  // A sample entity resolves to the correct (named, non-"0") layer.
  int firePipe12 = 0;
  for (const EntityAttributes& a : in.userLineAttrs)
    if (a.layer == "C-FIRE-PIPE-12IN") ++firePipe12;
  for (const EntityAttributes& a : in.userPolylineAttrs)
    if (a.layer == "C-FIRE-PIPE-12IN") ++firePipe12;
  CHECK(firePipe12 > 100);
}

// GitHub issue #391 / REQ-312 — the DWG ARC writer never set `extrusion`, so a tilted arc
// (`CadArc::nx/ny/nz` not world +Z) exported flat and silently. It must now write the normal as the
// ARC's extrusion and the centre in the OCS frame that normal implies, matching how DxfIo.cpp's
// `ocsPointOf` writes DXF groups 210/220/230. A flat arc must be byte-for-byte unchanged.
TEST_CASE("DWG export writes a tilted ARC's extrusion and OCS centre (issue #391)",
          "[dwg][libredwg][req312][issue391]") {
  ScratchDir dir("tiltedarc");
  const auto p = (dir.path / "arc.dwg").string();

  AppCommandState st;
  // A flat arc — extrusion +Z, centre unchanged.
  CadArc flat{};
  flat.cx = 5.f; flat.cy = 5.f; flat.z = 0.f; flat.r = 3.f;
  flat.startRad = 0.f; flat.sweepRad = 1.2f;
  flat.nx = 0.f; flat.ny = 0.f; flat.nz = 1.f;
  // A tilted arc — normal pointing world +Y.
  CadArc tilt{};
  tilt.cx = 10.f; tilt.cy = 0.f; tilt.z = 4.f; tilt.r = 2.f;
  tilt.startRad = 0.f; tilt.sweepRad = 1.5f;
  tilt.nx = 0.f; tilt.ny = 1.f; tilt.nz = 0.f;
  st.userArcs = {flat, tilt};
  st.userArcAttrs = {EntityAttributes{}, EntityAttributes{}};

  std::vector<std::string> log;
  REQUIRE(ExportLibreCadFile(st, p.c_str(), log, /*asDxf=*/false));

  // The OCS centre a reader reconstructs for the tilted arc — same frame the writer used.
  ucs::Ucs frame;
  REQUIRE(ucs::FromNormal({0.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, &frame));
  const ray3d::Vec3 tiltOcs = ucs::WorldToUcs(frame, {10.0, 0.0, 4.0});

  Dwg_Data dwg;
  std::memset(&dwg, 0, sizeof(dwg));
  REQUIRE(dwg_read_file(p.c_str(), &dwg) < DWG_ERR_CRITICAL);

  const Dwg_Entity_ARC* flatArc = nullptr;
  const Dwg_Entity_ARC* tiltArc = nullptr;
  for (BITCODE_BL i = 0; i < dwg.num_objects; ++i) {
    const Dwg_Object* o = &dwg.object[i];
    if (o->fixedtype != DWG_TYPE_ARC || o->tio.entity == nullptr || o->tio.entity->tio.ARC == nullptr)
      continue;
    const Dwg_Entity_ARC* e = o->tio.entity->tio.ARC;
    if (e->radius == Catch::Approx(3.0).margin(1e-6))
      flatArc = e;
    else if (e->radius == Catch::Approx(2.0).margin(1e-6))
      tiltArc = e;
  }
  REQUIRE(flatArc != nullptr);
  REQUIRE(tiltArc != nullptr);

  // Flat arc: extrusion is the default +Z and the centre is the world centre, unchanged.
  CHECK(flatArc->extrusion.x == Catch::Approx(0.0).margin(1e-9));
  CHECK(flatArc->extrusion.y == Catch::Approx(0.0).margin(1e-9));
  CHECK(flatArc->extrusion.z == Catch::Approx(1.0).margin(1e-9));
  CHECK(flatArc->center.x == Catch::Approx(5.0).margin(1e-6));
  CHECK(flatArc->center.y == Catch::Approx(5.0).margin(1e-6));
  CHECK(flatArc->center.z == Catch::Approx(0.0).margin(1e-6));

  // Tilted arc: extrusion carries the normal, centre is the OCS point.
  CHECK(tiltArc->extrusion.x == Catch::Approx(0.0).margin(1e-9));
  CHECK(tiltArc->extrusion.y == Catch::Approx(1.0).margin(1e-9));
  CHECK(tiltArc->extrusion.z == Catch::Approx(0.0).margin(1e-9));
  CHECK(tiltArc->center.x == Catch::Approx(tiltOcs.x).margin(1e-6));
  CHECK(tiltArc->center.y == Catch::Approx(tiltOcs.y).margin(1e-6));
  CHECK(tiltArc->center.z == Catch::Approx(tiltOcs.z).margin(1e-6));
  // The OCS centre is genuinely different from the world centre (not a no-op path).
  CHECK(std::fabs(tiltArc->center.z - 4.0) > 0.5);

  dwg_free(&dwg);
}

// GitHub issue #435 / REQ-312 — the DWG importer never read an ARC's or CIRCLE's
// extrusion, so a tilted curve landed flat and misplaced with no message. Import must
// convert the OCS centre via ucs::FromNormal + UcsToWorld and store the normal.
TEST_CASE("DWG import reads a tilted ARC's extrusion and OCS centre (issue #435)",
          "[dwg][libredwg][req312][issue435]") {
  ScratchDir dir("tiltedarc-import");
  const auto p = (dir.path / "arc-import.dwg").string();

  // Build a DWG directly via LibreDWG with a tilted ARC: normal (0,1,0), centre in OCS.
  ucs::Ucs frame;
  REQUIRE(ucs::FromNormal({0.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, &frame));
  const ray3d::Vec3 worldC{10.0, 0.0, 4.0};
  const ray3d::Vec3 ocs = ucs::WorldToUcs(frame, worldC);

  {
    Dwg_Data* dwg = dwg_new_Document(R_2000, 0, 0);
    REQUIRE(dwg != nullptr);
    Dwg_Object_BLOCK_HEADER* hdr = nullptr;
    {
      Dwg_Object* m = dwg_model_space_object(dwg);
      REQUIRE(m != nullptr);
      hdr = m->tio.object->tio.BLOCK_HEADER;
      REQUIRE(hdr != nullptr);
    }
    dwg_point_3d c{ocs.x, ocs.y, ocs.z};
    Dwg_Entity_ARC* e = dwg_add_ARC(hdr, &c, 2.0, 0.0, 1.5);
    REQUIRE(e != nullptr);
    e->extrusion.x = 0.0;
    e->extrusion.y = 1.0;
    e->extrusion.z = 0.0;
    REQUIRE(dwg_write_file(p.c_str(), dwg) == 0);
    dwg_free(dwg);
    std::free(dwg);
  }

  AppCommandState in;
  std::vector<std::string> log;
  REQUIRE(ImportDwgFile(in, p.c_str(), log));
  REQUIRE(in.userArcs.size() == 1);
  const CadArc& a = in.userArcs[0];
  // Centre recovered to world, normal stored, angles untouched.
  CHECK(a.cx + in.worldDocumentOriginX == Catch::Approx(10.0).margin(1e-4));
  CHECK(a.cy + in.worldDocumentOriginY == Catch::Approx(0.0).margin(1e-4));
  CHECK(a.z == Catch::Approx(4.0).margin(1e-4));
  CHECK(a.nx == Catch::Approx(0.f).margin(1e-6));
  CHECK(a.ny == Catch::Approx(1.f).margin(1e-6));
  CHECK(a.nz == Catch::Approx(0.f).margin(1e-6));
  CHECK(a.startRad == Catch::Approx(0.f).margin(1e-6));
  CHECK(a.sweepRad == Catch::Approx(1.5f).margin(1e-6));
  // Log must not contain a degenerate refusal.
  for (const auto& l : log)
    CHECK(l.find("zero-length") == std::string::npos);
}

TEST_CASE("DWG import reads a tilted CIRCLE's extrusion and OCS centre (issue #435)",
          "[dwg][libredwg][req312][issue435]") {
  ScratchDir dir("tiltedcircle-import");
  const auto p = (dir.path / "circle-import.dwg").string();

  ucs::Ucs frame;
  REQUIRE(ucs::FromNormal({0.0, 0.0, 0.0}, {0.6, 0.0, 0.8}, &frame));
  const ray3d::Vec3 worldC{7.0, -3.0, 12.0};
  const ray3d::Vec3 ocs = ucs::WorldToUcs(frame, worldC);

  {
    Dwg_Data* dwg = dwg_new_Document(R_2000, 0, 0);
    REQUIRE(dwg != nullptr);
    Dwg_Object* m = dwg_model_space_object(dwg);
    REQUIRE(m != nullptr);
    auto* hdr = m->tio.object->tio.BLOCK_HEADER;
    REQUIRE(hdr != nullptr);
    dwg_point_3d c{ocs.x, ocs.y, ocs.z};
    Dwg_Entity_CIRCLE* e = dwg_add_CIRCLE(hdr, &c, 5.0);
    REQUIRE(e != nullptr);
    e->extrusion.x = 0.6;
    e->extrusion.y = 0.0;
    e->extrusion.z = 0.8;
    REQUIRE(dwg_write_file(p.c_str(), dwg) == 0);
    dwg_free(dwg);
    std::free(dwg);
  }

  AppCommandState in;
  std::vector<std::string> log;
  REQUIRE(ImportDwgFile(in, p.c_str(), log));
  REQUIRE(in.userCirclesCxCyZR.size() == 4);
  CHECK(in.userCirclesCxCyZR[0] + in.worldDocumentOriginX == Catch::Approx(7.0).margin(1e-4));
  CHECK(in.userCirclesCxCyZR[1] + in.worldDocumentOriginY == Catch::Approx(-3.0).margin(1e-4));
  CHECK(in.userCirclesCxCyZR[2] == Catch::Approx(12.0).margin(1e-4));
  float nx = 0, ny = 0, nz = 0;
  CircleNormalAt(in.userCircleNormals, 0, &nx, &ny, &nz);
  CHECK(nx == Catch::Approx(0.6f).margin(1e-6));
  CHECK(ny == Catch::Approx(0.0f).margin(1e-6));
  CHECK(nz == Catch::Approx(0.8f).margin(1e-6));
}

TEST_CASE("DWG import refuses a zero-length extrusion (issue #435, REQ-201)",
          "[dwg][libredwg][req312][issue435][req201]") {
  ScratchDir dir("degenerate");
  const auto p = (dir.path / "deg.dwg").string();
  {
    Dwg_Data* dwg = dwg_new_Document(R_2000, 0, 0);
    REQUIRE(dwg != nullptr);
    Dwg_Object* m = dwg_model_space_object(dwg);
    REQUIRE(m != nullptr);
    auto* hdr = m->tio.object->tio.BLOCK_HEADER;
    REQUIRE(hdr != nullptr);
    dwg_point_3d c{1.0, 2.0, 3.0};
    // LibreDWG's own bit_write_BE/bit_read_BE (third_party/libredwg/src/bits.c) normalizes any
    // extrusion with x==0 && y==0 to a unit +-Z vector on both write and read, so a literal
    // (0,0,0) never survives a round trip through dwg_write_file/dwg_read_file. A vector whose
    // x component underflows to 0 only on squaring (1e-300) skips that normalization (x != 0.0
    // bit-for-bit) yet is still degenerate once ucs::FromNormal normalizes it, so it still
    // exercises the reader's zero-length guard.
    Dwg_Entity_ARC* e = dwg_add_ARC(hdr, &c, 2.0, 0.0, 1.0);
    REQUIRE(e != nullptr);
    e->extrusion.x = 1e-300;
    e->extrusion.y = 0.0;
    e->extrusion.z = 0.0;
    Dwg_Entity_CIRCLE* ce = dwg_add_CIRCLE(hdr, &c, 1.0);
    REQUIRE(ce != nullptr);
    ce->extrusion.x = 1e-300;
    ce->extrusion.y = 0.0;
    ce->extrusion.z = 0.0;
    REQUIRE(dwg_write_file(p.c_str(), dwg) == 0);
    dwg_free(dwg);
    std::free(dwg);
  }
  AppCommandState in;
  std::vector<std::string> log;
  REQUIRE(ImportDwgFile(in, p.c_str(), log));
  CHECK(in.userArcs.empty());
  CHECK(in.userCirclesCxCyZR.empty());
  bool found = false;
  for (const auto& l : log)
    if (l.find("zero-length") != std::string::npos)
      found = true;
  CHECK(found);
}

TEST_CASE("DWG round-trips a tilted ARC via GoSurvey export/import (issue #435)",
          "[dwg][libredwg][req312][issue435]") {
  ScratchDir dir("roundtrip-arc");
  const auto p = (dir.path / "rt.dwg").string();
  AppCommandState st;
  CadArc a{};
  a.cx = 100.f;
  a.cy = -50.f;
  a.z = 25.f;
  a.r = 10.f;
  a.startRad = 0.3f;
  a.sweepRad = 2.1f;
  a.nx = 0.f;
  a.ny = 1.f;
  a.nz = 0.f;
  st.userArcs.push_back(a);
  st.userArcAttrs.push_back(EntityAttributes{});
  std::vector<std::string> log;
  REQUIRE(ExportDwgFile(st, p.c_str(), log));
  AppCommandState in;
  REQUIRE(ImportDwgFile(in, p.c_str(), log));
  REQUIRE(in.userArcs.size() == 1);
  const CadArc& b = in.userArcs[0];
  CHECK(b.cx + in.worldDocumentOriginX == Catch::Approx(a.cx + st.worldDocumentOriginX).margin(0.01));
  CHECK(b.cy + in.worldDocumentOriginY == Catch::Approx(a.cy + st.worldDocumentOriginY).margin(0.01));
  CHECK(b.z == Catch::Approx(a.z).margin(0.01));
  CHECK(b.nx == Catch::Approx(a.nx).margin(1e-6));
  CHECK(b.ny == Catch::Approx(a.ny).margin(1e-6));
  CHECK(b.nz == Catch::Approx(a.nz).margin(1e-6));
  CHECK(b.startRad == Catch::Approx(a.startRad).margin(1e-4));
  CHECK(b.sweepRad == Catch::Approx(a.sweepRad).margin(1e-4));
}

TEST_CASE("DWG round-trips a tilted CIRCLE via GoSurvey export/import (issue #435)",
          "[dwg][libredwg][req312][issue435]") {
  ScratchDir dir("roundtrip-circle");
  const auto p = (dir.path / "rt-circle.dwg").string();
  AppCommandState st;
  st.userCirclesCxCyZR = {20.f, 30.f, 15.f, 8.f};
  st.userCircleAttrs = {EntityAttributes{}};
  st.userCircleNormals.clear();
  PushCircleNormal(st.userCircleNormals, 0.6f, 0.f, 0.8f);
  std::vector<std::string> log;
  REQUIRE(ExportDwgFile(st, p.c_str(), log));
  AppCommandState in;
  REQUIRE(ImportDwgFile(in, p.c_str(), log));
  REQUIRE(in.userCirclesCxCyZR.size() == 4);
  CHECK(in.userCirclesCxCyZR[0] + in.worldDocumentOriginX == Catch::Approx(20.f).margin(0.01));
  CHECK(in.userCirclesCxCyZR[1] + in.worldDocumentOriginY == Catch::Approx(30.f).margin(0.01));
  CHECK(in.userCirclesCxCyZR[2] == Catch::Approx(15.f).margin(0.01));
  float nx = 0, ny = 0, nz = 0;
  CircleNormalAt(in.userCircleNormals, 0, &nx, &ny, &nz);
  CHECK(nx == Catch::Approx(0.6f).margin(1e-5));
  CHECK(ny == Catch::Approx(0.0f).margin(1e-5));
  CHECK(nz == Catch::Approx(0.8f).margin(1e-5));
}

TEST_CASE("Foreign DWG without payload still imports a LINE (REQ-175)", "[dwg][libredwg][req175]") {
  ScratchDir dir("foreign");
  const auto p = (dir.path / "cad-only.dwg").string();
  AppCommandState st;
  OneLine(st);
  std::vector<std::string> log;
  REQUIRE(ExportLibreCadFile(st, p.c_str(), log, /*asDxf=*/false));
  AppCommandState in;
  REQUIRE(ImportDwgFile(in, p.c_str(), log));
  REQUIRE(in.surveyPoints.empty());
  REQUIRE(in.userLinesFlat.size() == 6);
  REQUIRE(in.userLinesFlat[3] == Catch::Approx(10.f).margin(0.05f));
}

// REQ-357: DWG export writes the drawing unit into INSUNITS always, and LUNITS / AUNITS from the
// Drawing Settings when "Set drawing variables to match" is on; import adopts Millimeters (4).
TEST_CASE("DWG export writes INSUNITS, LUNITS and AUNITS from the Drawing Settings (REQ-357)",
          "[dwg][libredwg][req357]") {
  ScratchDir dir("req357");
  const auto p = (dir.path / "units.dwg").string();
  AppCommandState st;
  OneLine(st);
  st.drawingInsUnits = 4;
  st.drawingSettings.angularUnits = DrawingSettings::AngularUnits::Grads;
  std::vector<std::string> log;
  REQUIRE(ExportLibreCadFile(st, p.c_str(), log, /*asDxf=*/false));
  {
    Dwg_Data dwg;
    std::memset(&dwg, 0, sizeof(dwg));
    REQUIRE(dwg_read_file(p.c_str(), &dwg) < DWG_ERR_CRITICAL);
    CHECK(dwg.header_vars.INSUNITS == 4);
    CHECK(dwg.header_vars.LUNITS == 2);
    CHECK(dwg.header_vars.AUNITS == 2);
    dwg_free(&dwg);
  }
  AppCommandState in;
  REQUIRE(ImportLibreCadFile(in, p.c_str(), log, /*asDxf=*/false));
  CHECK(in.drawingInsUnits == 4);
}

// REQ-358: the drawing's zone (CS-MAP code) survives DWG save → close → reopen, through the
// ADR-044 trailer — including a code the installed dictionary does not know.
TEST_CASE("The coordinate-system zone survives a DWG round trip (REQ-358)", "[dwg][libredwg][req358]") {
  ScratchDir dir("req358");
  for (const char* code : {"HARN/TX.TX-CF", "SOME.FUTURE-ZONE"}) {
    const auto p = (dir.path / "zone.dwg").string();
    AppCommandState st;
    OneLine(st);
    st.drawingSettings.zoneCode = code;
    std::vector<std::string> log;
    REQUIRE(ExportDwgFile(st, p.c_str(), log));
    AppCommandState in;
    REQUIRE(ImportDwgFile(in, p.c_str(), log));
    CHECK(in.drawingSettings.zoneCode == code);
    CHECK(in.userLinesFlat.size() == 6);
  }
}

// REQ-360: every Transformation setting survives DWG save → close → reopen (ADR-044 trailer).
TEST_CASE("The Transformation settings survive a DWG round trip (REQ-360)", "[dwg][libredwg][req360]") {
  ScratchDir dir("req360");
  const auto p = (dir.path / "transform.dwg").string();
  AppCommandState st;
  OneLine(st);
  st.drawingSettings.zoneCode = "HARN/TX.TX-CF";
  DrawingSettings::Transform& t = st.drawingSettings.transform;
  t.apply = true;
  t.applySeaLevel = true;
  t.elevation = 594.0;
  t.spheroidRadiusM = 6372000.0;
  t.computation = DrawingSettings::Transform::Computation::UserDefined;
  t.userScaleFactor = 0.99995905;
  t.refLocalX = 3115243.14;
  t.refLocalY = 10077391.26;
  t.refGridE = 3115243.14;
  t.refGridN = 10077391.26;
  t.refPointNumber = 7;
  t.rotation = DrawingSettings::Transform::Rotation::RotationPoint;
  t.rotLocalX = 3116243.14;
  t.rotLocalY = 10077391.26;
  t.rotGridE = 3116243.0;
  t.rotGridN = 10077408.71;
  t.rotPointNumber = 8;
  t.toNorthDeg = 1.336;
  t.localAzimuthDeg = 91.0;
  t.gridAzimuthDeg = 90.0;
  std::vector<std::string> log;
  REQUIRE(ExportDwgFile(st, p.c_str(), log));
  AppCommandState in;
  REQUIRE(ImportDwgFile(in, p.c_str(), log));
  CHECK(in.drawingSettings.transform == t);
  CHECK(in.userLinesFlat.size() == 6);
}

// REQ-361: the Object Layers table survives DWG save → close → reopen (ADR-044 trailer).
TEST_CASE("The Object Layers table survives a DWG round trip (REQ-361)", "[dwg][libredwg][req361]") {
  ScratchDir dir("req361");
  const auto p = (dir.path / "objectlayers.dwg").string();
  AppCommandState st;
  OneLine(st);
  ObjectLayerRow& surf = st.drawingSettings.objectLayers[static_cast<size_t>(ObjectLayerKind::Surface)];
  surf.layer = "C-TOPO-SURF";
  surf.modifier = ObjectLayerRow::Modifier::Suffix;
  surf.value = "-*";
  surf.locked = true;
  st.drawingSettings.objectLayers[static_cast<size_t>(ObjectLayerKind::Table)].layer = "G-ANNO-TABL";
  std::vector<std::string> log;
  REQUIRE(ExportDwgFile(st, p.c_str(), log));
  AppCommandState in;
  REQUIRE(ImportDwgFile(in, p.c_str(), log));
  CHECK(in.drawingSettings.objectLayers == st.drawingSettings.objectLayers);
}

TEST_CASE("The online map choice survives a DWG round trip (REQ-363)", "[dwg][libredwg][req363]") {
  ScratchDir dir("req363");
  const auto p = (dir.path / "onlinemap.dwg").string();
  AppCommandState st;
  OneLine(st);
  st.drawingSettings.zoneCode = "HARN/TX.TX-CF";
  st.drawingSettings.onlineMap = DrawingSettings::OnlineMap::UsgsImageryTopo;
  std::vector<std::string> log;
  REQUIRE(ExportDwgFile(st, p.c_str(), log));
  AppCommandState in;
  REQUIRE(ImportDwgFile(in, p.c_str(), log));
  CHECK(in.drawingSettings.onlineMap == DrawingSettings::OnlineMap::UsgsImageryTopo);
}

TEST_CASE("A captured map area survives a DWG round trip, bytes intact (REQ-364)", "[dwg][libredwg][req364]") {
  ScratchDir dir("req364");
  const auto p = (dir.path / "captured.dwg").string();
  AppCommandState st;
  OneLine(st);
  st.drawingSettings.zoneCode = "HARN/TX.TX-CF";
  DrawingSettings::CapturedArea a;
  a.map = DrawingSettings::OnlineMap::UsgsImagery;
  a.level = 16;
  std::string bytes;
  for (int i = 0; i < 1000; ++i)
    bytes.push_back(static_cast<char>(i * 37));  // every byte value, including NUL
  a.tiles.push_back({14977, 26984, std::make_shared<const std::string>(bytes)});
  a.tiles.push_back({14978, 26984, std::make_shared<const std::string>(bytes.substr(1))});
  st.drawingSettings.capturedAreas.push_back(a);
  std::vector<std::string> log;
  REQUIRE(ExportDwgFile(st, p.c_str(), log));
  AppCommandState in;
  REQUIRE(ImportDwgFile(in, p.c_str(), log));
  CHECK(in.drawingSettings.capturedAreas == st.drawingSettings.capturedAreas);
}

// REQ-359 item 3 / D-2026-09-29-e: a Position Marker reopens ONCE from a GoSurvey DWG (the trailer),
// while the DWG / DXF body carries a CIRCLE, two LINEs and an MTEXT for other programs.
TEST_CASE("A Position Marker survives DWG save and is written as circle, lines and MTEXT (REQ-359)",
          "[dwg][libredwg][req359]") {
  ScratchDir dir("req359");
  const auto p = (dir.path / "marker.dwg").string();
  AppCommandState st;
  OneLine(st);
  st.drawingSettings.zoneCode = "HARN/TX.TX-CF";
  st.worldDocumentOriginX = 3115000.0;
  st.worldDocumentOriginY = 10077000.0;
  CadPositionMarker m;
  m.x = 243.14;
  m.y = 391.26;
  m.latitudeDeg = 30.28625;
  m.longitudeDeg = -97.7394;
  m.label.kind = CadAnnotation::Kind::Mtext;
  m.label.text = "UT Tower\nAG9976";
  m.label.boxMinX = 250.f;
  m.label.boxMaxX = 300.f;
  m.label.boxMinY = 395.f;
  m.label.boxMaxY = 400.f;
  st.cadPositionMarkers.push_back(m);
  st.cadPositionMarkerAttrs.push_back(EntityAttributes{});
  std::vector<std::string> log;
  REQUIRE(ExportDwgFile(st, p.c_str(), log));

  AppCommandState in;
  REQUIRE(ImportDwgFile(in, p.c_str(), log));
  REQUIRE(in.cadPositionMarkers.size() == 1);  // once: not also rebuilt from the loose pieces
  CHECK(in.cadPositionMarkers[0].x == m.x);
  CHECK(in.cadPositionMarkers[0].label.text == m.label.text);
  CHECK(in.userLinesFlat.size() == 6);          // the one real line, no cross arms
  CHECK(in.userCirclesCxCyZR.empty());
  CHECK(in.cadAnnotations.empty());

  // The CAD body alone (what another program reads): circle + 2 lines + MTEXT at WORLD coordinates.
  AppCommandState body;
  REQUIRE(ImportLibreCadFile(body, p.c_str(), log, /*asDxf=*/false));
  REQUIRE(body.userCirclesCxCyZR.size() == 4);
  CHECK(body.userCirclesCxCyZR[0] + body.worldDocumentOriginX == Catch::Approx(3115243.14).margin(0.01));
  CHECK(body.userCirclesCxCyZR[1] + body.worldDocumentOriginY == Catch::Approx(10077391.26).margin(0.01));
  CHECK(body.userLinesFlat.size() == 3 * 6);
  bool labelFound = false;
  for (const CadAnnotation& a : body.cadAnnotations)
    labelFound = labelFound || a.text.find("AG9976") != std::string::npos;
  CHECK(labelFound);

  // DXF export writes the same pieces.
  const auto dxf = (dir.path / "marker.dxf").string();
  REQUIRE(ExportDxfFile(st, dxf.c_str(), log));
  std::ifstream f(dxf);
  const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  CHECK(text.find("AcDbCircle") != std::string::npos);
  CHECK(text.find("UT Tower\\PAG9976") != std::string::npos);
  CHECK(text.find("3115243.14") != std::string::npos);
}

// Issue #590: AutoCAD refused every GoSurvey DWG (eDwgCRCDoesNotMatch) because LibreDWG left
// `nolinks` = 1 on the last entity of model space — "my next entity is handle + 1", which does not
// exist. Every entity must carry explicit links: first prev null, last next null.
namespace {
struct EntityLink {
  BITCODE_HV handle = 0;
  BITCODE_HV prev = 0;
  BITCODE_HV next = 0;
  int nolinks = -1;
};

std::vector<EntityLink> ModelSpaceLinks(const std::string& path) {
  std::vector<EntityLink> links;
  Dwg_Data dwg;
  std::memset(&dwg, 0, sizeof(dwg));
  if (dwg_read_file(path.c_str(), &dwg) >= DWG_ERR_CRITICAL) {
    dwg_free(&dwg);
    return links;
  }
  for (BITCODE_BL i = 0; i < dwg.num_objects; ++i) {
    const Dwg_Object& obj = dwg.object[i];
    // R2000 stores no owner handle for model-space entities: entmode 2 says "model space".
    // The block's own BLOCK / ENDBLK markers are not in its entity chain.
    if (obj.supertype != DWG_SUPERTYPE_ENTITY || obj.tio.entity->entmode != 2 ||
        obj.fixedtype == DWG_TYPE_BLOCK || obj.fixedtype == DWG_TYPE_ENDBLK)
      continue;
    const Dwg_Object_Entity* ent = obj.tio.entity;
    EntityLink link;
    link.handle = obj.handle.value;
    link.prev = ent->prev_entity != nullptr ? ent->prev_entity->absolute_ref : 0;
    link.next = ent->next_entity != nullptr ? ent->next_entity->absolute_ref : 0;
    link.nolinks = ent->nolinks;
    links.push_back(link);
  }
  dwg_free(&dwg);
  return links;
}

void CheckExplicitChain(const std::vector<EntityLink>& links) {
  for (size_t i = 0; i < links.size(); ++i) {
    INFO("entity " << i << " handle " << links[i].handle);
    CHECK(links[i].nolinks == 0);
    CHECK(links[i].prev == (i > 0 ? links[i - 1].handle : 0));
    CHECK(links[i].next == (i + 1 < links.size() ? links[i + 1].handle : 0));
  }
}
}  // namespace

TEST_CASE("The minimal LibreDWG R2000 file links its only entity explicitly (issue #590)",
          "[dwg][libredwg][issue590]") {
  ScratchDir dir("links-minimal");
  const auto p = (dir.path / "line.dwg").string();
  REQUIRE(LibreDwgWriteMinimalR2000(p.c_str()));

  const std::vector<EntityLink> links = ModelSpaceLinks(p);
  REQUIRE(links.size() == 1);
  CheckExplicitChain(links);
}

TEST_CASE("DWG export links every model-space entity explicitly (issue #590)", "[dwg][libredwg][issue590]") {
  ScratchDir dir("links-export");
  const auto p = (dir.path / "mixed.dwg").string();

  AppCommandState st;
  st.userLinesFlat = {0.f, 0.f, 0.f, 10.f, 0.f, 0.f, 10.f, 0.f, 0.f, 10.f, 10.f, 0.f};
  st.userLineAttrs = {EntityAttributes{}, EntityAttributes{}};
  st.userCirclesCxCyZR = {5.f, 5.f, 0.f, 2.f};
  st.userCircleAttrs = {EntityAttributes{}};
  CadArc arc{};
  arc.cx = 20.f; arc.cy = 0.f; arc.r = 3.f;
  arc.startRad = 0.f; arc.sweepRad = 1.5f;
  arc.nz = 1.f;
  st.userArcs = {arc};
  st.userArcAttrs = {EntityAttributes{}};
  st.userPolylineVerts = {0.f, 20.f, 0.f, 10.f, 20.f, 0.f, 10.f, 30.f, 0.f};
  st.userPolylineOffsets = {0, 3};
  st.userPolylineClosed = {0};
  st.userPolylineAttrs = {EntityAttributes{}};

  std::vector<std::string> log;
  REQUIRE(ExportLibreCadFile(st, p.c_str(), log, /*asDxf=*/false));

  const std::vector<EntityLink> links = ModelSpaceLinks(p);
  REQUIRE(links.size() == 5);
  CheckExplicitChain(links);

  // The explicit links still read back as the same drawing.
  AppCommandState in;
  REQUIRE(ImportDwgFile(in, p.c_str(), log));
  CHECK(in.userLinesFlat.size() == 12);
  CHECK(in.userCirclesCxCyZR.size() == 4);
  CHECK(in.userArcs.size() == 1);
  CHECK(in.userPolylineOffsets.size() == 2);
}

// REQ-325 / ADR-053 increment 4 (GitHub issue #437) — DWG export splits a
// tilted curved polyline segment onto its own ARC, the DWG mirror of
// DxfIo.cpp's split. A flat-only polyline stays one LWPOLYLINE unchanged.
TEST_CASE("DWG export splits a tilted curved polyline segment onto its own ARC (issue #437)",
          "[dwg][libredwg][req325][issue437]") {
  ScratchDir dir("dwg-tilted-poly-split");
  const auto p = (dir.path / "tilted-poly.dwg").string();

  auto makeTiltedPolyline = []() -> AppCommandState {
    AppCommandState st;
    st.worldDocumentOriginX = 0.0;
    st.worldDocumentOriginY = 0.0;
    // 3-vertex open polyline: v0(0,0,0) -> v1(10,0,0) -> v2(10,0,10)
    // Segment v1->v2 is tilted (plane y=0, normal (0,1,0)) and curved.
    st.userPolylineOffsets = {0, 3};
    st.userPolylineVerts = {0.f, 0.f, 0.f, 10.f, 0.f, 0.f, 10.f, 0.f, 10.f};
    st.userPolylineVertsBulge = {0.f, 0.4f, 0.f};
    st.userPolylineVertsNormal = {0.f, 0.f, 1.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f};
    st.userPolylineClosed = {0};
    st.userPolylineAttrs = {EntityAttributes{}};
    return st;
  };
  AppCommandState st = makeTiltedPolyline();
  std::vector<std::string> log;
  REQUIRE(ExportDwgFile(st, p.c_str(), log));

  Dwg_Data dwg;
  std::memset(&dwg, 0, sizeof(dwg));
  REQUIRE(dwg_read_file(p.c_str(), &dwg) < DWG_ERR_CRITICAL);
  int nLw = 0, nArc = 0;
  const Dwg_Entity_LWPOLYLINE* lw = nullptr;
  const Dwg_Entity_ARC* arc = nullptr;
  for (BITCODE_BL i = 0; i < dwg.num_objects; ++i) {
    const Dwg_Object* o = &dwg.object[i];
    if (o->fixedtype == DWG_TYPE_LWPOLYLINE && o->tio.entity && o->tio.entity->tio.LWPOLYLINE)
      { ++nLw; lw = o->tio.entity->tio.LWPOLYLINE; }
    if (o->fixedtype == DWG_TYPE_ARC && o->tio.entity && o->tio.entity->tio.ARC)
      { ++nArc; arc = o->tio.entity->tio.ARC; }
  }
  CHECK(nLw == 1);
  CHECK(nArc == 1);
  if (lw) {
    CHECK(lw->num_points == 2);
    // The flat run must not carry the tilted bulge.
    if (lw->bulges == nullptr)
      CHECK(lw->num_bulges == 0);
    else {
      bool anyTiltedBulge = false;
      for (BITCODE_BL i = 0; i < lw->num_bulges; ++i)
        if (lw->bulges[i] != 0.0) anyTiltedBulge = true;
      CHECK(!anyTiltedBulge);
    }
  }
  if (arc) {
    CHECK(arc->extrusion.x == Catch::Approx(0.0).margin(1e-9));
    CHECK(arc->extrusion.y == Catch::Approx(1.0).margin(1e-9));
    CHECK(arc->extrusion.z == Catch::Approx(0.0).margin(1e-9));
    // The ARC's centre is stored in OCS, so it differs from world (10,0,10) etc.
    // Verify it is not the naive world centre taken as flat.
    CHECK((std::fabs(arc->center.x - 10.0) > 0.5 || std::fabs(arc->center.z - 10.0) > 0.5));
  }
  dwg_free(&dwg);

  // ExportDwgFile also embeds the GoSurvey document as an ADR-044 trailer, and ImportDwgFile
  // prefers that trailer over the LibreDWG entities when it is present (DwgIo.cpp's
  // TryGoSurveyDwgPayloadFromBytes check) — so GoSurvey reopening its OWN file recovers the
  // original, unsplit polyline exactly, not the flat-run-plus-ARC a foreign reader sees above.
  AppCommandState in;
  REQUIRE(ImportDwgFile(in, p.c_str(), log));
  CHECK(in.userPolylineOffsets.size() == 2);
  if (in.userPolylineOffsets.size() == 2) {
    const int nv = in.userPolylineOffsets[1] - in.userPolylineOffsets[0];
    CHECK(nv == 3);
  }
  CHECK(in.userArcs.empty());
}

TEST_CASE("DWG export keeps a flat-only polyline as one LWPOLYLINE (issue #437)",
          "[dwg][libredwg][req325][issue437]") {
  ScratchDir dir("dwg-flat-poly");
  const auto p = (dir.path / "flat-poly.dwg").string();
  AppCommandState st;
  st.worldDocumentOriginX = 0.0;
  st.worldDocumentOriginY = 0.0;
  st.userPolylineOffsets = {0, 4};
  st.userPolylineVerts = {0.f, 0.f, 0.f, 10.f, 0.f, 0.f, 10.f, 10.f, 0.f, 0.f, 10.f, 0.f};
  st.userPolylineVertsBulge = {0.f, 0.5f, 0.f, 0.f};
  st.userPolylineVertsNormal = {0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f};
  st.userPolylineClosed = {1};
  st.userPolylineAttrs = {EntityAttributes{}};
  std::vector<std::string> log;
  REQUIRE(ExportDwgFile(st, p.c_str(), log));
  Dwg_Data dwg;
  std::memset(&dwg, 0, sizeof(dwg));
  REQUIRE(dwg_read_file(p.c_str(), &dwg) < DWG_ERR_CRITICAL);
  int nLw = 0, nArc = 0;
  const Dwg_Entity_LWPOLYLINE* lw = nullptr;
  for (BITCODE_BL i = 0; i < dwg.num_objects; ++i) {
    const Dwg_Object* o = &dwg.object[i];
    if (o->fixedtype == DWG_TYPE_LWPOLYLINE && o->tio.entity && o->tio.entity->tio.LWPOLYLINE)
      { ++nLw; lw = o->tio.entity->tio.LWPOLYLINE; }
    if (o->fixedtype == DWG_TYPE_ARC && o->tio.entity && o->tio.entity->tio.ARC)
      ++nArc;
  }
  CHECK(nLw == 1);
  CHECK(nArc == 0);
  if (lw) {
    CHECK(lw->flag & 512); // closed preserved
    CHECK(lw->num_points == 4);
    CHECK(lw->num_bulges == 4);
  }
  dwg_free(&dwg);
}

// REQ-170 / REQ-201, issue #614: the "Export DWG" warning and save log are built from what the
// drawing actually contains, not a fixed list.
TEST_CASE("DWG export loss summary is empty for a drawing of only lines and circles (issue #614)",
          "[dwg][libredwg][req170][req201][issue614]") {
  AppCommandState st;
  OneLine(st);
  st.userCirclesCxCyZR = {5.f, 5.f, 0.f, 2.f};
  st.userCircleAttrs = {EntityAttributes{}};
  const std::vector<DwgExportLoss> losses = ComputeDwgExportLosses(st);
  CHECK(losses.empty());
}

TEST_CASE("DWG export loss summary omits exportable solids and pipe runs (issue #612 / #614)",
          "[dwg][libredwg][issue612][req170][req201][issue614]") {
  AppCommandState st;
  OneLine(st);
  brep::Solid box;
  brep::Problem why = brep::Problem::Ok;
  REQUIRE(brep::MakeBox(ucs::Ucs{}, 1.0, 1.0, 1.0, &box, &why));
  st.cadSolids.push_back(std::make_shared<const brep::Solid>(std::move(box)));
  CadPipeRun run;
  run.nominalSize = "4in";
  run.vertsXyz = {0.f, 0.f, 0.f, 10.f, 0.f, 0.f};
  st.cadPipeRuns.push_back(std::move(run));
  const std::vector<DwgExportLoss> losses = ComputeDwgExportLosses(st);
  for (const DwgExportLoss& l : losses) {
    CHECK(l.label.find("solid") == std::string::npos);
    CHECK(l.label.find("pipe run") == std::string::npos);
  }
}

TEST_CASE("DWG export loss summary lists exactly 5 skipped pipe runs (issue #614 / #616)",
          "[dwg][libredwg][req170][req201][issue614]") {
  AppCommandState st;
  OneLine(st);
  for (int i = 0; i < 5; ++i)
    st.cadPipeRuns.push_back(CadPipeRun{});
  CadTable t;
  t.cols = 2;
  t.cells = {"A", "B"};
  st.cadTables.push_back(t);

  const std::vector<DwgExportLoss> losses = ComputeDwgExportLosses(st);
  REQUIRE(losses.size() == 1);
  CHECK(losses[0].label.find("pipe run") != std::string::npos);
  CHECK(losses[0].count == 5);
  for (const DwgExportLoss& l : losses)
    CHECK(l.label.find("table") == std::string::npos);
}

TEST_CASE("DWG export logs every loss the summary names (issue #614)",
          "[dwg][libredwg][req170][req201][issue614]") {
  ScratchDir dir("dwg-loss-log");
  const auto p = (dir.path / "loss.dwg").string();
  AppCommandState st;
  OneLine(st);
  CadTable t;
  t.cols = 2;
  t.cells = {"A", "B"};
  st.cadTables.push_back(t);

  std::vector<std::string> log;
  REQUIRE(ExportDwgFile(st, p.c_str(), log));
  bool found = false;
  for (const auto& l : log)
    if (l.find("table") != std::string::npos) found = true;
  CHECK(found);
}

// REQ-365 / D-2026-09-30-f / issue #605: survey points ARE written — a GOSURVEY_POINT block
// INSERT per point with visible NUMBER/DESCRIPTION attributes, so AutoCAD and Civil 3D show them
// (the user's choice: a Civil 3D/Carlson-style point block with attributes, over a plain POINT +
// XDATA + MTEXT label). Elevation is not a third attribute — see LibreDwgCad.cpp's
// EnsureSurveyPointBlockDef for why — but is not lost: it is the INSERT's own Z coordinate.
TEST_CASE("DWG export writes each survey point as a GOSURVEY_POINT block insert (issue #605)",
          "[dwg][libredwg][req365][issue605]") {
  ScratchDir dir("dwg-survey-points");
  const auto p = (dir.path / "points.dwg").string();
  AppCommandState st;
  st.worldDocumentOriginX = 0.0;
  st.worldDocumentOriginY = 0.0;

  SurveyPoint sp1;
  sp1.id = 501;
  sp1.easting = 1000.25;
  sp1.northing = 2000.75;
  sp1.elevation = 456.125;
  sp1.description = "IPF";
  sp1.rawDescription = "IPF";
  sp1.labelStyle = SurveyPointLabelStyle::None;
  st.surveyPoints.push_back(sp1);

  SurveyPoint sp2;
  sp2.id = 502;
  sp2.easting = 1010.0;
  sp2.northing = 2010.0;
  sp2.elevation = 460.0;
  sp2.description = "PK NAIL";
  sp2.rawDescription = "PK NAIL";
  sp2.labelStyle = SurveyPointLabelStyle::None;
  st.surveyPoints.push_back(sp2);

  std::vector<std::string> log;
  REQUIRE(ExportDwgFile(st, p.c_str(), log));

  // No loss line for survey points, since they are now written.
  for (const auto& l : log)
    CHECK(l.find("survey point") == std::string::npos);

  Dwg_Data dwg;
  std::memset(&dwg, 0, sizeof(dwg));
  REQUIRE(dwg_read_file(p.c_str(), &dwg) < DWG_ERR_CRITICAL);

  int nInsert = 0;
  std::vector<std::pair<double, double>> insertXy;
  std::vector<std::string> attrNumbers, attrDescs;
  for (BITCODE_BL i = 0; i < dwg.num_objects; ++i) {
    const Dwg_Object* o = &dwg.object[i];
    if (o->fixedtype != DWG_TYPE_INSERT || o->tio.entity == nullptr || o->tio.entity->tio.INSERT == nullptr)
      continue;
    const Dwg_Entity_INSERT* ins = o->tio.entity->tio.INSERT;
    if (ins->block_header == nullptr)
      continue;
    Dwg_Object* blk = dwg_resolve_handle_silent(&dwg, ins->block_header->absolute_ref);
    if (blk == nullptr || blk->tio.object == nullptr)
      continue;
    const Dwg_Object_BLOCK_HEADER* hdr2 = blk->tio.object->tio.BLOCK_HEADER;
    if (hdr2 == nullptr ||
        libredwgcad_detail::DecodeDwgString(hdr2->name, false) != "GOSURVEY_POINT")
      continue;
    ++nInsert;
    insertXy.emplace_back(ins->ins_pt.x, ins->ins_pt.y);
  }
  // Walk every ATTRIB directly rather than through INSERT::attribs[] (round-trip fidelity of that
  // array is not the point of this test).
  for (BITCODE_BL i = 0; i < dwg.num_objects; ++i) {
    const Dwg_Object* o = &dwg.object[i];
    if (o->fixedtype != DWG_TYPE_ATTRIB || o->tio.entity == nullptr || o->tio.entity->tio.ATTRIB == nullptr)
      continue;
    const Dwg_Entity_ATTRIB* att = o->tio.entity->tio.ATTRIB;
    const std::string tag = libredwgcad_detail::DecodeDwgString(att->tag, false);
    const std::string val = libredwgcad_detail::DecodeDwgString(att->text_value, false);
    if (tag == "NUMBER") attrNumbers.push_back(val);
    if (tag == "DESCRIPTION") attrDescs.push_back(val);
  }
  CHECK(nInsert == 2);
  REQUIRE(insertXy.size() == 2);
  CHECK(insertXy[0].first == Catch::Approx(1000.25).margin(1e-6));
  CHECK(insertXy[0].second == Catch::Approx(2000.75).margin(1e-6));
  REQUIRE(attrNumbers.size() == 2);
  CHECK((attrNumbers[0] == "501" || attrNumbers[1] == "501"));
  REQUIRE(attrDescs.size() == 2);
  CHECK((attrDescs[0] == "IPF" || attrDescs[1] == "IPF"));
  dwg_free(&dwg);
}

// REQ-057 / D-2026-10-01-a, issue #603: a flat polyline at a non-zero elevation round-trips that
// elevation (LWPOLYLINE's own `elevation` field was never set before this fix — always 0).
TEST_CASE("DWG export writes a flat polyline's elevation (issue #603)",
          "[dwg][libredwg][req057][issue603]") {
  ScratchDir dir("dwg-poly-elevation");
  const auto p = (dir.path / "elev.dwg").string();
  AppCommandState st;
  st.worldDocumentOriginX = 0.0;
  st.worldDocumentOriginY = 0.0;
  st.userPolylineOffsets = {0, 3};
  st.userPolylineVerts = {0.f, 0.f, 100.f, 10.f, 0.f, 100.f, 10.f, 10.f, 100.f};
  st.userPolylineVertsBulge = {0.f, 0.f, 0.f};
  st.userPolylineVertsNormal = {0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f};
  st.userPolylineClosed = {0};
  st.userPolylineAttrs = {EntityAttributes{}};
  std::vector<std::string> log;
  REQUIRE(ExportDwgFile(st, p.c_str(), log));

  Dwg_Data dwg;
  std::memset(&dwg, 0, sizeof(dwg));
  REQUIRE(dwg_read_file(p.c_str(), &dwg) < DWG_ERR_CRITICAL);
  int nLw = 0;
  double elevation = -1.0;
  for (BITCODE_BL i = 0; i < dwg.num_objects; ++i) {
    const Dwg_Object* o = &dwg.object[i];
    if (o->fixedtype == DWG_TYPE_LWPOLYLINE && o->tio.entity && o->tio.entity->tio.LWPOLYLINE) {
      ++nLw;
      elevation = o->tio.entity->tio.LWPOLYLINE->elevation;
    }
  }
  CHECK(nLw == 1);
  CHECK(elevation == Catch::Approx(100.0).margin(1e-6));
  dwg_free(&dwg);
}

// REQ-057 / D-2026-10-01-a, issue #603: a polyline whose vertices have different Z values writes
// as POLYLINE_3D (real per-vertex Z) instead of a flattened LWPOLYLINE.
TEST_CASE("DWG export writes a varying-Z polyline as POLYLINE_3D (issue #603)",
          "[dwg][libredwg][req057][issue603]") {
  ScratchDir dir("dwg-poly-3d");
  const auto p = (dir.path / "p3d.dwg").string();
  AppCommandState st;
  st.worldDocumentOriginX = 0.0;
  st.worldDocumentOriginY = 0.0;
  st.userPolylineOffsets = {0, 4};
  st.userPolylineVerts = {0.f, 0.f, 0.f, 10.f, 0.f, 5.f, 10.f, 10.f, 10.f, 0.f, 10.f, 2.f};
  st.userPolylineVertsBulge = {0.f, 0.f, 0.f, 0.f};
  st.userPolylineVertsNormal = {0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f};
  st.userPolylineClosed = {0};
  st.userPolylineAttrs = {EntityAttributes{}};
  std::vector<std::string> log;
  REQUIRE(ExportDwgFile(st, p.c_str(), log));

  // No "flattened" loss line for this drawing (no bulge, so nothing degrades).
  for (const auto& l : log)
    CHECK(l.find("flattened") == std::string::npos);

  Dwg_Data dwg;
  std::memset(&dwg, 0, sizeof(dwg));
  REQUIRE(dwg_read_file(p.c_str(), &dwg) < DWG_ERR_CRITICAL);
  int nLw = 0, nPoly3d = 0;
  std::vector<double> vertZ;
  for (BITCODE_BL i = 0; i < dwg.num_objects; ++i) {
    const Dwg_Object* o = &dwg.object[i];
    if (o->fixedtype == DWG_TYPE_LWPOLYLINE)
      ++nLw;
    if (o->fixedtype == DWG_TYPE_POLYLINE_3D)
      ++nPoly3d;
    if (o->fixedtype == DWG_TYPE_VERTEX_3D && o->tio.entity && o->tio.entity->tio.VERTEX_3D)
      vertZ.push_back(o->tio.entity->tio.VERTEX_3D->point.z);
  }
  CHECK(nLw == 0);
  CHECK(nPoly3d == 1);
  REQUIRE(vertZ.size() == 4);
  CHECK(vertZ[0] == Catch::Approx(0.0).margin(1e-6));
  CHECK(vertZ[1] == Catch::Approx(5.0).margin(1e-6));
  CHECK(vertZ[2] == Catch::Approx(10.0).margin(1e-6));
  CHECK(vertZ[3] == Catch::Approx(2.0).margin(1e-6));
  dwg_free(&dwg);
}

// REQ-057 / D-2026-10-01-a, issue #603: a feature line writes as a POLYLINE_3D with its name and
// description preserved as GOSURVEY-appid XDATA (the user's choice over a plain polyline with no
// identity data at all).
TEST_CASE("DWG export writes a feature line as POLYLINE_3D with XDATA identity (issue #603)",
          "[dwg][libredwg][req057][issue603]") {
  ScratchDir dir("dwg-featureline");
  const auto p = (dir.path / "fl.dwg").string();
  AppCommandState st;
  st.worldDocumentOriginX = 0.0;
  st.worldDocumentOriginY = 0.0;
  st.featureLineOffsets = {0, 3};
  st.featureLineVerts = {0.0, 0.0, 100.0, 10.0, 0.0, 101.5, 20.0, 0.0, 103.0};
  st.featureLineClosed = {0};
  CadFeatureLineInfo info;
  info.name = "Top of Curb";
  info.description = "North run";
  st.featureLineInfo = {info};
  st.featureLineAttrs = {EntityAttributes{}};

  std::vector<std::string> log;
  REQUIRE(ExportDwgFile(st, p.c_str(), log));
  for (const auto& l : log)
    CHECK(l.find("feature line") == std::string::npos);

  Dwg_Data dwg;
  std::memset(&dwg, 0, sizeof(dwg));
  REQUIRE(dwg_read_file(p.c_str(), &dwg) < DWG_ERR_CRITICAL);
  int nPoly3d = 0;
  std::vector<double> vertZ;
  std::vector<std::string> eedStrings;
  for (BITCODE_BL i = 0; i < dwg.num_objects; ++i) {
    const Dwg_Object* o = &dwg.object[i];
    if (o->fixedtype == DWG_TYPE_POLYLINE_3D && o->tio.entity) {
      ++nPoly3d;
      Dwg_Object_Entity* ent = o->tio.entity;
      for (BITCODE_BL j = 0; j < ent->num_eed; ++j) {
        if (ent->eed[j].data != nullptr && ent->eed[j].data->code == 0)
          eedStrings.emplace_back(ent->eed[j].data->u.eed_0.string, ent->eed[j].data->u.eed_0.length);
      }
    }
    if (o->fixedtype == DWG_TYPE_VERTEX_3D && o->tio.entity && o->tio.entity->tio.VERTEX_3D)
      vertZ.push_back(o->tio.entity->tio.VERTEX_3D->point.z);
  }
  CHECK(nPoly3d == 1);
  REQUIRE(vertZ.size() == 3);
  CHECK(vertZ[1] == Catch::Approx(101.5).margin(1e-6));
  REQUIRE(eedStrings.size() == 2);
  CHECK(eedStrings[0] == "Top of Curb");
  CHECK(eedStrings[1] == "North run");
  dwg_free(&dwg);
}

// REQ-170 / REQ-044, issue #604: TEXT keeps its rotation and its named style, and the degree
// sign (the character issue #604 calls out — "almost every survey bearing") round-trips via
// AutoCAD's codepage-independent %%d control code rather than a raw UTF-8 byte ANSI misreads.
TEST_CASE("DWG export writes TEXT rotation, style, and the degree sign (issue #604)",
          "[dwg][libredwg][req170][req044][issue604]") {
  ScratchDir dir("dwg-text-rotation");
  const auto p = (dir.path / "text.dwg").string();
  AppCommandState st;
  st.worldDocumentOriginX = 0.0;
  st.worldDocumentOriginY = 0.0;
  TextStyle ts;
  ts.name = "Survey";
  ts.fontFamily = "Arial";
  ts.heightInches = 0.1f;
  st.textStyles.push_back(ts);

  CadAnnotation an;
  an.kind = CadAnnotation::Kind::Text;
  an.insX = 5.f;
  an.insY = 10.f;
  an.insZ = 0.f;
  an.plottedHeightInches = 0.1f;
  an.rotationRad = 0.5f;
  an.text = "N 45\xC2\xB0 30' E";  // UTF-8 degree sign
  an.styleName = "Survey";
  st.cadAnnotations.push_back(an);
  st.cadAnnotationAttrs.push_back(EntityAttributes{});

  std::vector<std::string> log;
  REQUIRE(ExportDwgFile(st, p.c_str(), log));

  Dwg_Data dwg;
  std::memset(&dwg, 0, sizeof(dwg));
  REQUIRE(dwg_read_file(p.c_str(), &dwg) < DWG_ERR_CRITICAL);
  int nText = 0;
  double rotation = -1.0;
  std::string text, styleName;
  for (BITCODE_BL i = 0; i < dwg.num_objects; ++i) {
    const Dwg_Object* o = &dwg.object[i];
    if (o->fixedtype != DWG_TYPE_TEXT || o->tio.entity == nullptr || o->tio.entity->tio.TEXT == nullptr)
      continue;
    ++nText;
    const Dwg_Entity_TEXT* t = o->tio.entity->tio.TEXT;
    rotation = t->rotation;
    text = libredwgcad_detail::DecodeDwgString(t->text_value, false);
    if (t->style != nullptr) {
      Dwg_Object* sObj = dwg_resolve_handle_silent(&dwg, t->style->absolute_ref);
      if (sObj != nullptr && sObj->tio.object != nullptr && sObj->tio.object->tio.STYLE != nullptr)
        styleName = libredwgcad_detail::DecodeDwgString(sObj->tio.object->tio.STYLE->name, false);
    }
  }
  CHECK(nText == 1);
  CHECK(rotation == Catch::Approx(0.5).margin(1e-6));
  CHECK(text.find("%%d") != std::string::npos);
  CHECK(styleName == "Survey");
  dwg_free(&dwg);
}

// REQ-170 / REQ-044, issue #604: MTEXT keeps its rotation, attachment, height and style.
TEST_CASE("DWG export writes MTEXT rotation, attachment, height, and style (issue #604)",
          "[dwg][libredwg][req170][req044][issue604]") {
  ScratchDir dir("dwg-mtext-attach");
  const auto p = (dir.path / "mtext.dwg").string();
  AppCommandState st;
  st.worldDocumentOriginX = 0.0;
  st.worldDocumentOriginY = 0.0;
  TextStyle ts;
  ts.name = "Survey";
  ts.fontFamily = "Arial";
  ts.heightInches = 0.1f;
  st.textStyles.push_back(ts);

  CadAnnotation an;
  an.kind = CadAnnotation::Kind::Mtext;
  an.insX = 0.f;
  an.insY = 0.f;
  an.insZ = 0.f;
  an.plottedHeightInches = 0.1f;
  an.rotationRad = 1.5707963267948966f;  // 90 degrees
  an.text = "Middle Center";
  an.boxMinX = 0.f;
  an.boxMinY = 0.f;
  an.boxMaxX = 10.f;
  an.boxMaxY = 4.f;
  an.mtextAttach = 5;  // middle-center
  an.styleName = "Survey";
  st.cadAnnotations.push_back(an);
  st.cadAnnotationAttrs.push_back(EntityAttributes{});

  std::vector<std::string> log;
  REQUIRE(ExportDwgFile(st, p.c_str(), log));

  Dwg_Data dwg;
  std::memset(&dwg, 0, sizeof(dwg));
  REQUIRE(dwg_read_file(p.c_str(), &dwg) < DWG_ERR_CRITICAL);
  int nMtext = 0;
  double insX = -1.0, insY = -1.0, textHeight = -1.0;
  int attachment = -1;
  double axX = 0.0, axY = 0.0;
  std::string styleName;
  for (BITCODE_BL i = 0; i < dwg.num_objects; ++i) {
    const Dwg_Object* o = &dwg.object[i];
    if (o->fixedtype != DWG_TYPE_MTEXT || o->tio.entity == nullptr || o->tio.entity->tio.MTEXT == nullptr)
      continue;
    ++nMtext;
    const Dwg_Entity_MTEXT* m = o->tio.entity->tio.MTEXT;
    insX = m->ins_pt.x;
    insY = m->ins_pt.y;
    textHeight = m->text_height;
    attachment = m->attachment;
    axX = m->x_axis_dir.x;
    axY = m->x_axis_dir.y;
    if (m->style != nullptr) {
      Dwg_Object* sObj = dwg_resolve_handle_silent(&dwg, m->style->absolute_ref);
      if (sObj != nullptr && sObj->tio.object != nullptr && sObj->tio.object->tio.STYLE != nullptr)
        styleName = libredwgcad_detail::DecodeDwgString(sObj->tio.object->tio.STYLE->name, false);
    }
  }
  CHECK(nMtext == 1);
  CHECK(attachment == 5);
  // Middle-center of a 10x4 box: (5, 2).
  CHECK(insX == Catch::Approx(5.0).margin(1e-6));
  CHECK(insY == Catch::Approx(2.0).margin(1e-6));
  CHECK(textHeight > 0.0);
  CHECK(axX == Catch::Approx(0.0).margin(1e-6));
  CHECK(axY == Catch::Approx(1.0).margin(1e-6));
  CHECK(styleName == "Survey");
  dwg_free(&dwg);
}

// REQ-107, issue #606 (regression for the root cause of D-2026-09-30-f's #605 crash): LibreDWG
// 0.13.4's in_postprocess_SEQEND (encode.c) rebuilt INSERT::first_attrib/last_attrib from
// owned[i]->handleref.value — an OFFSET once dwg_add_handle's own offset-encoding optimization has
// rewritten that ref's handleref.code (which it does whenever two handles are close together,
// i.e. almost always for entities created back-to-back), not the absolute handle. That silently
// corrupted the INSERT's attribute chain on every attribute after the first, with only sometimes
// an immediate crash (page-heap-verified; plain-heap runs could succeed while still corrupted).
// Fixed (vendored copy) to read ->absolute_ref, which dwg_add_handleref always keeps correct
// regardless of that rewriting. Five attributes, not three, so this cannot regress back to "two is
// the safe ceiling" without being caught.
TEST_CASE("DWG export writes an INSERT with five attributes without corruption (issue #606)",
          "[dwg][libredwg][req107][issue606]") {
  ScratchDir dir("dwg-multi-attrib");
  const auto p = (dir.path / "multiattrib.dwg").string();
  Dwg_Data* dwg = dwg_new_Document(R_2000, 0, 0);
  REQUIRE(dwg != nullptr);
  Dwg_Object* m = dwg_model_space_object(dwg);
  REQUIRE(m != nullptr);
  auto* hdr = m->tio.object->tio.BLOCK_HEADER;
  REQUIRE(hdr != nullptr);

  Dwg_Object_BLOCK_HEADER* blk = dwg_add_BLOCK_HEADER(dwg, "MULTIATTRIB");
  REQUIRE(blk != nullptr);
  dwg_add_BLOCK(blk, "MULTIATTRIB");
  dwg_point_3d c{0.0, 0.0, 0.0};
  dwg_add_CIRCLE(blk, &c, 1.0);
  dwg_add_ENDBLK(blk);

  dwg_point_3d ins{0.0, 0.0, 0.0};
  Dwg_Entity_INSERT* e = dwg_add_INSERT(hdr, &ins, "MULTIATTRIB", 1.0, 1.0, 1.0, 0.0);
  REQUIRE(e != nullptr);
  const char* tags[5] = {"TAG", "SIZE", "MATERIAL", "SPEC", "NOTE"};
  const char* vals[5] = {"T1", "2in", "Steel", "ASTM-A53", "field-verify"};
  for (int i = 0; i < 5; ++i)
    REQUIRE(dwg_add_ATTRIB(e, 0.5, 0, &ins, tags[i], vals[i]) != nullptr);

  REQUIRE(dwg_write_file(p.c_str(), dwg) == 0);
  dwg_free(dwg);
  std::free(dwg);

  Dwg_Data rd;
  std::memset(&rd, 0, sizeof(rd));
  REQUIRE(dwg_read_file(p.c_str(), &rd) < DWG_ERR_CRITICAL);
  std::vector<std::pair<std::string, std::string>> attrs;
  for (BITCODE_BL i = 0; i < rd.num_objects; ++i) {
    const Dwg_Object* o = &rd.object[i];
    if (o->fixedtype != DWG_TYPE_ATTRIB || o->tio.entity == nullptr || o->tio.entity->tio.ATTRIB == nullptr)
      continue;
    const Dwg_Entity_ATTRIB* a = o->tio.entity->tio.ATTRIB;
    attrs.emplace_back(libredwgcad_detail::DecodeDwgString(a->tag, false),
                       libredwgcad_detail::DecodeDwgString(a->text_value, false));
  }
  REQUIRE(attrs.size() == 5);
  for (int i = 0; i < 5; ++i) {
    bool found = false;
    for (const auto& kv : attrs)
      if (kv.first == tags[i] && kv.second == vals[i]) found = true;
    CHECK(found);
  }
  dwg_free(&rd);
}

// REQ-107, issue #606: DWG export writes real BLOCK/INSERT/ATTRIB records — 2 definitions, 5
// inserts (1 with attributes), matching the issue's proposed acceptance.
TEST_CASE("DWG export writes real BLOCK definitions, INSERTs, and attributes (issue #606)",
          "[dwg][libredwg][req107][issue606]") {
  ScratchDir dir("dwg-blocks");
  const auto p = (dir.path / "blocks.dwg").string();
  AppCommandState st;
  st.worldDocumentOriginX = 0.0;
  st.worldDocumentOriginY = 0.0;

  CadBlockDefinition defA;
  defA.name = "MARKER";
  defA.content.circles = {0.0, 0.0, 0.0, 1.0};
  defA.content.circleAttrs = {EntityAttributes{}};
  st.blockDefs.push_back(defA);

  CadBlockDefinition defB;
  defB.name = "VALVE";
  defB.content.lines = {-1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
  defB.content.lineAttrs = {EntityAttributes{}};
  CadBlockAttrDef ad;
  ad.tag = "TAG";
  ad.height = 0.1f;
  defB.attrDefs.push_back(ad);
  st.blockDefs.push_back(defB);

  for (int i = 0; i < 4; ++i) {
    CadBlockRef ref;
    ref.defName = "MARKER";
    ref.xf.x = static_cast<float>(i) * 10.f;
    ref.xf.y = 0.f;
    st.cadBlockRefs.push_back(ref);
    st.cadBlockRefAttrs.push_back(EntityAttributes{});
  }
  CadBlockRef valveRef;
  valveRef.defName = "VALVE";
  valveRef.xf.x = 100.f;
  valveRef.xf.y = 50.f;
  valveRef.xf.rotZ = 0.5f;
  CadBlockAttrValue av;
  av.tag = "TAG";
  av.value = "V-101";
  valveRef.attributes.push_back(av);
  st.cadBlockRefs.push_back(valveRef);
  st.cadBlockRefAttrs.push_back(EntityAttributes{});

  std::vector<std::string> log;
  REQUIRE(ExportDwgFile(st, p.c_str(), log));
  for (const auto& l : log)
    CHECK(l.find("block reference") == std::string::npos);

  Dwg_Data dwg;
  std::memset(&dwg, 0, sizeof(dwg));
  REQUIRE(dwg_read_file(p.c_str(), &dwg) < DWG_ERR_CRITICAL);

  int nBlockDefs = 0, nInserts = 0, nAttribs = 0;
  std::vector<std::string> insertBlockNames;
  for (BITCODE_BL i = 0; i < dwg.num_objects; ++i) {
    const Dwg_Object* o = &dwg.object[i];
    if (o->fixedtype == DWG_TYPE_BLOCK_HEADER && o->tio.object != nullptr &&
        o->tio.object->tio.BLOCK_HEADER != nullptr) {
      const std::string nm = libredwgcad_detail::DecodeDwgString(o->tio.object->tio.BLOCK_HEADER->name, false);
      if (nm == "MARKER" || nm == "VALVE")
        ++nBlockDefs;
    }
    if (o->fixedtype == DWG_TYPE_INSERT && o->tio.entity != nullptr && o->tio.entity->tio.INSERT != nullptr) {
      ++nInserts;
      const Dwg_Entity_INSERT* ins = o->tio.entity->tio.INSERT;
      if (ins->block_header != nullptr) {
        Dwg_Object* blk = dwg_resolve_handle_silent(&dwg, ins->block_header->absolute_ref);
        if (blk != nullptr && blk->tio.object != nullptr && blk->tio.object->tio.BLOCK_HEADER != nullptr)
          insertBlockNames.push_back(
              libredwgcad_detail::DecodeDwgString(blk->tio.object->tio.BLOCK_HEADER->name, false));
      }
    }
    if (o->fixedtype == DWG_TYPE_ATTRIB && o->tio.entity != nullptr && o->tio.entity->tio.ATTRIB != nullptr)
      ++nAttribs;
  }
  CHECK(nBlockDefs == 2);
  CHECK(nInserts == 5);
  CHECK(nAttribs == 1);
  int nMarker = 0, nValve = 0;
  for (const auto& nm : insertBlockNames) {
    if (nm == "MARKER") ++nMarker;
    if (nm == "VALVE") ++nValve;
  }
  CHECK(nMarker == 4);
  CHECK(nValve == 1);
  dwg_free(&dwg);
}

// REQ-366, issue #607: one DimAligned, one DimLinear, and one DimAngular dimension round-trip
// through DWG save and GoSurvey's own DWG reopen — kind, definition geometry, and measured value
// (via the regenerated label text) all survive.
TEST_CASE("DWG round-trips Aligned, Linear and Angular dimensions (REQ-366, issue #607)",
          "[dwg][libredwg][req366][issue607]") {
  ScratchDir dir("roundtrip-dims");
  const auto p = (dir.path / "rt-dims.dwg").string();
  AppCommandState st;
  st.activeDimensionStyle = DimensionStyles::Default();
  st.activeDimensionStyle.name = "GSDIM";
  st.activeDimensionStyle.textSizeInches = 0.12f;
  st.activeDimensionStyle.arrowSizeInches = 0.09f;
  st.activeDimensionStyle.unitPrecision = 3;
  st.activeDimensionStyle.arrowType = DimArrowType::ClosedBlank;
  st.activeDimensionStyle.dimLineColor = "ACI:1";
  st.activeDimensionStyle.extLineColor = "ACI:2";
  st.activeDimensionStyle.textColor = "ACI:3";

  CadAnnotation aligned{};
  aligned.kind = CadAnnotation::Kind::DimAligned;
  aligned.dimExt1X = 0.f;   aligned.dimExt1Y = 0.f;
  aligned.dimExt2X = 30.f;  aligned.dimExt2Y = 20.f;
  aligned.dimSignedOffset = 5.f;
  aligned.insZ = 1.f;
  st.cadAnnotations.push_back(aligned);
  st.cadAnnotationAttrs.push_back(EntityAttributes{});

  CadAnnotation linear{};
  linear.kind = CadAnnotation::Kind::DimLinear;
  linear.dimExt1X = 0.f;   linear.dimExt1Y = 0.f;
  linear.dimExt2X = 40.f;  linear.dimExt2Y = 7.f;
  linear.dimSignedOffset = 8.f;
  linear.dimLinearVertical = false;
  linear.insZ = 2.f;
  st.cadAnnotations.push_back(linear);
  st.cadAnnotationAttrs.push_back(EntityAttributes{});

  CadAnnotation vlinear{};
  vlinear.kind = CadAnnotation::Kind::DimLinear;
  vlinear.dimExt1X = 0.f;   vlinear.dimExt1Y = 0.f;
  vlinear.dimExt2X = 3.f;   vlinear.dimExt2Y = 25.f;
  vlinear.dimSignedOffset = 6.f;
  vlinear.dimLinearVertical = true;
  vlinear.insZ = 0.f;
  st.cadAnnotations.push_back(vlinear);
  st.cadAnnotationAttrs.push_back(EntityAttributes{});

  CadAnnotation angular{};
  angular.kind = CadAnnotation::Kind::DimAngular;
  angular.dimAngVertexX = 10.f; angular.dimAngVertexY = 10.f;
  angular.dimExt1X = 20.f; angular.dimExt1Y = 10.f;   // 0 rad from vertex
  angular.dimExt2X = 10.f; angular.dimExt2Y = 20.f;   // pi/2 rad from vertex
  angular.dimSignedOffset = 12.f;                     // arc radius
  st.cadAnnotations.push_back(angular);
  st.cadAnnotationAttrs.push_back(EntityAttributes{});

  for (CadAnnotation& a : st.cadAnnotations)
    DimensionStyles::BakeTextOntoDimension(a, st.activeDimensionStyle);
  {
    AngleDisplaySettings angleSet{};
    CadDimRefreshMeasurementText(&st.cadAnnotations[0], st.activeDimensionStyle.unitPrecision, angleSet);
    CadDimRefreshMeasurementText(&st.cadAnnotations[1], st.activeDimensionStyle.unitPrecision, angleSet);
    CadDimRefreshMeasurementText(&st.cadAnnotations[2], st.activeDimensionStyle.unitPrecision, angleSet);
    CadDimRefreshMeasurementText(&st.cadAnnotations[3], st.activeDimensionStyle.unitPrecision, angleSet);
  }

  std::vector<std::string> log;
  REQUIRE(ExportDwgFile(st, p.c_str(), log));
  bool loggedDims = false;
  for (const auto& l : log)
    if (l.find("REQ-366") != std::string::npos && l.find("export") != std::string::npos)
      loggedDims = true;
  CHECK(loggedDims);

  // Strip the GoSurvey JSON trailer (ADR-044) so the import below is FORCED through the real DWG
  // DIMENSION entities this test exists to verify, not GoSurvey's own lossless trailer shortcut
  // (ImportDwgFile prefers the trailer when present — see DwgIo.cpp / LibreDwgCad.cpp's comments
  // on it). Footer layout: [LibreDWG bytes][JSON][8-byte LE length][16-byte magic] at EOF.
  {
    std::ifstream rf(std::filesystem::u8path(p), std::ios::binary);
    REQUIRE(rf.good());
    std::vector<char> bytes((std::istreambuf_iterator<char>(rf)), std::istreambuf_iterator<char>());
    rf.close();
    constexpr size_t kMagicLen = 16, kLenLen = 8;
    REQUIRE(bytes.size() > kMagicLen + kLenLen);
    uint64_t jsonLen = 0;
    for (int i = 0; i < 8; ++i)
      jsonLen |= static_cast<uint64_t>(static_cast<unsigned char>(bytes[bytes.size() - kMagicLen - kLenLen + i]))
                 << (8 * i);
    const size_t dwgOnlyLen = bytes.size() - kMagicLen - kLenLen - static_cast<size_t>(jsonLen);
    REQUIRE(dwgOnlyLen > 0);
    REQUIRE(dwgOnlyLen < bytes.size());
    std::filesystem::resize_file(std::filesystem::u8path(p), dwgOnlyLen);
  }

  AppCommandState in;
  REQUIRE(ImportDwgFile(in, p.c_str(), log));

  std::vector<const CadAnnotation*> dims;
  for (const CadAnnotation& a : in.cadAnnotations)
    if (CadAnnotationIsDimension(a))
      dims.push_back(&a);
  REQUIRE(dims.size() == 4);

  auto findKind = [&](CadAnnotation::Kind k, bool vertical) -> const CadAnnotation* {
    for (const CadAnnotation* a : dims)
      if (a->kind == k && (k != CadAnnotation::Kind::DimLinear || a->dimLinearVertical == vertical))
        return a;
    return nullptr;
  };

  const CadAnnotation* ba = findKind(CadAnnotation::Kind::DimAligned, false);
  REQUIRE(ba != nullptr);
  CHECK(ba->dimExt1X + in.worldDocumentOriginX == Catch::Approx(0.0).margin(0.02));
  CHECK(ba->dimExt1Y + in.worldDocumentOriginY == Catch::Approx(0.0).margin(0.02));
  CHECK(ba->dimExt2X + in.worldDocumentOriginX == Catch::Approx(30.0).margin(0.02));
  CHECK(ba->dimExt2Y + in.worldDocumentOriginY == Catch::Approx(20.0).margin(0.02));
  CHECK(!ba->text.empty());

  const CadAnnotation* bl = findKind(CadAnnotation::Kind::DimLinear, false);
  REQUIRE(bl != nullptr);
  CHECK(bl->dimExt1X + in.worldDocumentOriginX == Catch::Approx(0.0).margin(0.02));
  CHECK(bl->dimExt2X + in.worldDocumentOriginX == Catch::Approx(40.0).margin(0.02));
  CHECK_FALSE(bl->dimLinearVertical);

  const CadAnnotation* bv = findKind(CadAnnotation::Kind::DimLinear, true);
  REQUIRE(bv != nullptr);
  CHECK(bv->dimLinearVertical);
  CHECK(bv->dimExt2Y + in.worldDocumentOriginY == Catch::Approx(25.0).margin(0.02));

  const CadAnnotation* bg = findKind(CadAnnotation::Kind::DimAngular, false);
  REQUIRE(bg != nullptr);
  CHECK(bg->dimAngVertexX + in.worldDocumentOriginX == Catch::Approx(10.0).margin(0.02));
  CHECK(bg->dimAngVertexY + in.worldDocumentOriginY == Catch::Approx(10.0).margin(0.02));
  CHECK(bg->dimExt1X + in.worldDocumentOriginX == Catch::Approx(20.0).margin(0.02));
  CHECK(bg->dimExt2Y + in.worldDocumentOriginY == Catch::Approx(20.0).margin(0.02));
  CHECK(bg->dimSignedOffset == Catch::Approx(12.0).margin(0.05));
  CHECK(!bg->text.empty());

  bool loggedImportDims = false;
  for (const auto& l : log)
    if (l.find("REQ-366") != std::string::npos && l.find("import") != std::string::npos)
      loggedImportDims = true;
  CHECK(loggedImportDims);
}

// REQ-366 statement 3: a DIMENSION subtype GoSurvey does not model (Radius here, hand-built
// directly with LibreDWG's own API so the test does not depend on any GoSurvey writer for it)
// opens without crashing or dropping data: the test only requires that import succeeds and the
// skipped subtype is named exactly once in the log, not once per instance.
TEST_CASE("DWG import falls back an unsupported DIMENSION subtype to a skip log, no crash (REQ-366)",
          "[dwg][libredwg][req366][issue607]") {
  ScratchDir dir("dim-radius-fallback");
  const auto p = (dir.path / "radius.dwg").string();

  Dwg_Data* nd = dwg_new_Document(R_2000, /*imperial=*/0, /*loglevel=*/0);
  REQUIRE(nd != nullptr);
  Dwg_Object* mso = dwg_model_space_object(nd);
  REQUIRE(mso != nullptr);
  REQUIRE(mso->tio.object != nullptr);
  Dwg_Object_BLOCK_HEADER* hdr = mso->tio.object->tio.BLOCK_HEADER;
  dwg_point_3d center{5.0, 5.0, 0.0};
  dwg_point_3d chord{10.0, 5.0, 0.0};
  for (int i = 0; i < 2; ++i)
    dwg_add_DIMENSION_RADIUS(hdr, &center, &chord, 2.0);  // two instances -> one aggregated log line
  REQUIRE(dwg_write_file(p.c_str(), nd) == 0);
  dwg_free(nd);
  std::free(nd);

  AppCommandState in;
  std::vector<std::string> log;
  REQUIRE(ImportDwgFile(in, p.c_str(), log));  // must not crash

  int radiusLines = 0;
  for (const auto& l : log)
    if (l.find("Radius") != std::string::npos)
      ++radiusLines;
  CHECK(radiusLines == 1);  // aggregated, not one line per instance
  for (const CadAnnotation& a : in.cadAnnotations)
    CHECK_FALSE(CadAnnotationIsDimension(a));  // never mapped to a GoSurvey dimension kind
}

static void StripGosurveyDwgTrailer(const std::string& pathUtf8) {
  std::ifstream rf(std::filesystem::u8path(pathUtf8), std::ios::binary);
  REQUIRE(rf.good());
  std::vector<char> bytes((std::istreambuf_iterator<char>(rf)), std::istreambuf_iterator<char>());
  rf.close();
  constexpr size_t kMagicLen = 16, kLenLen = 8;
  REQUIRE(bytes.size() > kMagicLen + kLenLen);
  uint64_t jsonLen = 0;
  for (int i = 0; i < 8; ++i)
    jsonLen |= static_cast<uint64_t>(static_cast<unsigned char>(bytes[bytes.size() - kMagicLen - kLenLen + i]))
               << (8 * i);
  const size_t dwgOnlyLen = bytes.size() - kMagicLen - kLenLen - static_cast<size_t>(jsonLen);
  REQUIRE(dwgOnlyLen > 0);
  REQUIRE(dwgOnlyLen < bytes.size());
  std::filesystem::resize_file(std::filesystem::u8path(pathUtf8), dwgOnlyLen);
}

static CadFilledRegion SquareHatchRegion(float x0, float y0, float size) {
  CadFilledRegion fr;
  fr.loopStart = {0};
  fr.vertsXyz = {x0, y0, 0.0, x0 + size, y0, 0.0, x0 + size, y0 + size, 0.0, x0, y0 + size, 0.0};
  return fr;
}

// REQ-170 / issue #608: solid and pattern hatches round-trip through native DWG entities.
TEST_CASE("DWG round-trips solid and pattern HATCH fills (REQ-170, issue #608)",
          "[dwg][libredwg][req170][issue608]") {
  ScratchDir dir("roundtrip-hatch");
  const auto p = (dir.path / "rt-hatch.dwg").string();
  AppCommandState st;
  st.cadFilledRegions.push_back(SquareHatchRegion(0.f, 0.f, 10.f));
  st.cadFilledRegionAttrs.push_back(EntityAttributes{});

  CadFilledRegion pat = SquareHatchRegion(20.f, 0.f, 8.f);
  pat.patternName = "ANSI31";
  pat.patternAngleDeg = 45.f;
  pat.patternScale = 2.f;
  st.cadFilledRegions.push_back(std::move(pat));
  st.cadFilledRegionAttrs.push_back(EntityAttributes{});

  std::vector<std::string> log;
  REQUIRE(ExportDwgFile(st, p.c_str(), log));
  bool loggedHatch = false;
  for (const auto& l : log)
    if (l.find("HATCH") != std::string::npos && l.find("608") != std::string::npos)
      loggedHatch = true;
  CHECK(loggedHatch);
  CHECK(ComputeDwgExportLosses(st).empty());

  StripGosurveyDwgTrailer(p);

  AppCommandState in;
  REQUIRE(ImportDwgFile(in, p.c_str(), log));
  REQUIRE(in.cadFilledRegions.size() == 2);

  int nSolid = 0;
  int nPattern = 0;
  for (const CadFilledRegion& fr : in.cadFilledRegions) {
    if (fr.isSolid())
      ++nSolid;
    else if (fr.patternName == "ANSI31") {
      ++nPattern;
      CHECK(fr.patternAngleDeg == Catch::Approx(45.f).margin(0.05f));
      CHECK(fr.patternScale == Catch::Approx(2.f).margin(0.05f));
    }
    REQUIRE(fr.loopStart.size() == 1);
    CHECK(fr.loopCount(0) == 4);
  }
  CHECK(nSolid == 1);
  CHECK(nPattern == 1);
}

TEST_CASE("Annotative HATCH path flag round-trips on R2018 DWG (issue #622)", "[dwg][libredwg][issue622]") {
  ScratchDir dir("dwg-hatch-annotative");
  const auto p = (dir.path / "hatch-anno.dwg").string();
  AppCommandState st;
  st.dwgExportVersion = DwgSaveVersion::R2018;
  CadFilledRegion pat = SquareHatchRegion(0.f, 0.f, 12.f);
  pat.patternName = "ANSI31";
  pat.patternScale = 1.5f;
  pat.annotative = true;
  st.cadFilledRegions.push_back(std::move(pat));
  st.cadFilledRegionAttrs.push_back(EntityAttributes{});
  std::vector<std::string> log;
  REQUIRE(ExportLibreCadFile(st, p.c_str(), log, /*asDxf=*/false));
  Dwg_Data dwg{};
  REQUIRE(dwg_read_file(p.c_str(), &dwg) < DWG_ERR_CRITICAL);
  bool sawAnnoPath = false;
  for (unsigned i = 0; i < dwg.num_objects; ++i) {
    if (dwg.object[i].fixedtype != DWG_TYPE_HATCH || dwg.object[i].tio.entity == nullptr ||
        dwg.object[i].tio.entity->tio.HATCH == nullptr)
      continue;
    const Dwg_Entity_HATCH* h = dwg.object[i].tio.entity->tio.HATCH;
    if (h->paths != nullptr) {
      for (BITCODE_BL pi = 0; pi < h->num_paths; ++pi) {
        if ((h->paths[pi].flag & 0x200) != 0)
          sawAnnoPath = true;
      }
    }
  }
  dwg_free(&dwg);
  REQUIRE(sawAnnoPath);
  AppCommandState in;
  REQUIRE(ImportLibreCadFile(in, p.c_str(), log, /*asDxf=*/false));
  REQUIRE(in.cadFilledRegions.size() == 1);
  CHECK(in.cadFilledRegions[0].annotative);
}

// REQ-170, issue #609: explicit entity lineweight and layer-table lineweight survive DWG save/open.
TEST_CASE("DWG round-trips entity and layer lineweight (REQ-170, issue #609)",
          "[dwg][libredwg][req170][issue609]") {
  ScratchDir dir("roundtrip-lw");
  const auto p = (dir.path / "rt-lw.dwg").string();
  AppCommandState st;
  CadLayerRow lyr;
  lyr.name = "Heavy";
  lyr.lineweightMm = 0.35f;
  st.drawingLayerTable.push_back(lyr);

  EntityAttributes explicitLw;
  explicitLw.lineweightMm = 0.50f;
  st.userLinesFlat = {0.f, 0.f, 0.f, 10.f, 0.f, 0.f, 20.f, 0.f, 0.f, 30.f, 0.f, 0.f};
  st.userLineAttrs.push_back(explicitLw);

  EntityAttributes byLayerLw;
  byLayerLw.layer = "Heavy";
  byLayerLw.lineweightMm = -1.f;
  st.userLineAttrs.push_back(byLayerLw);

  std::vector<std::string> log;
  REQUIRE(ExportDwgFile(st, p.c_str(), log));
  StripGosurveyDwgTrailer(p);

  AppCommandState in;
  REQUIRE(ImportDwgFile(in, p.c_str(), log));
  REQUIRE(in.userLineAttrs.size() == 2);
  CHECK(in.userLineAttrs[0].lineweightMm == Catch::Approx(0.50f).margin(0.02f));
  CHECK(in.userLineAttrs[1].lineweightMm < 0.f);
  CHECK(in.userLineAttrs[1].layer == "Heavy");

  const CadLayerRow* heavy = nullptr;
  for (const CadLayerRow& row : in.drawingLayerTable) {
    if (row.name == "Heavy")
      heavy = &row;
  }
  REQUIRE(heavy != nullptr);
  CHECK(heavy->lineweightMm == Catch::Approx(0.35f).margin(0.02f));
}

// REQ-037 / REQ-170, issue #610: paper layouts, sheet lines, and viewport scales survive DWG save/open.
TEST_CASE("DWG round-trips paper layouts and viewport scales (REQ-170, issue #610)",
          "[dwg][libredwg][req170][issue610]") {
  ScratchDir dir("roundtrip-paper");
  const auto p = (dir.path / "rt-paper.dwg").string();
  AppCommandState st;

  PaperLayout sheetA;
  sheetA.name = "Plot A";
  sheetA.paperLines = {0.5f, 0.5f, 0.f, 10.f, 0.5f, 0.f};
  Viewport vp20;
  vp20.paperXIn = 0.5f;
  vp20.paperYIn = 1.f;
  vp20.paperWIn = 4.f;
  vp20.paperHIn = 3.f;
  vp20.scaleModelPerPaperIn = 240.f;
  vp20.modelCenterX = 100.0;
  vp20.modelCenterY = 50.0;
  Viewport vp50;
  vp50.paperXIn = 5.f;
  vp50.paperYIn = 1.f;
  vp50.paperWIn = 4.f;
  vp50.paperHIn = 3.f;
  vp50.scaleModelPerPaperIn = 600.f;
  vp50.modelCenterX = 200.0;
  vp50.modelCenterY = 75.0;
  sheetA.viewports.push_back(vp20);
  sheetA.viewports.push_back(vp50);

  PaperLayout sheetB;
  sheetB.name = "Plot B";
  sheetB.paperLines = {1.f, 1.f, 0.f, 1.f, 10.f, 0.f};

  st.paperLayouts.push_back(sheetA);
  st.paperLayouts.push_back(sheetB);

  std::vector<std::string> log;
  REQUIRE(ExportDwgFile(st, p.c_str(), log));
  StripGosurveyDwgTrailer(p);

  AppCommandState in;
  REQUIRE(ImportDwgFile(in, p.c_str(), log));
  REQUIRE(in.paperLayouts.size() == 2);
  const PaperLayout* plotA = nullptr;
  const PaperLayout* plotB = nullptr;
  for (const PaperLayout& L : in.paperLayouts) {
    if (L.name == "Plot A")
      plotA = &L;
    if (L.name == "Plot B")
      plotB = &L;
  }
  REQUIRE(plotA != nullptr);
  REQUIRE(plotB != nullptr);
  REQUIRE(plotA->viewports.size() == 2);
  CHECK(plotA->paperLines.size() == 6);
  CHECK(plotA->viewports[0].scaleModelPerPaperIn == Catch::Approx(240.f).margin(1.f));
  CHECK(plotA->viewports[1].scaleModelPerPaperIn == Catch::Approx(600.f).margin(1.f));
  CHECK(plotA->viewports[0].modelCenterX == Catch::Approx(100.0).margin(0.01));
  CHECK(plotB->paperLines.size() == 6);
}

// REQ-170, issue #613: SPLINE and trimmed ELLIPSE import as polylines instead of being skipped.
TEST_CASE("DWG import maps SPLINE and trimmed ELLIPSE to polylines (REQ-170, issue #613)",
          "[dwg][libredwg][req170][issue613]") {
  ScratchDir dir("open-spline-ellipse");
  const auto p = (dir.path / "spline-ellipse.dwg").string();
  Dwg_Data* dwg = dwg_new_Document(R_2000, 0, 0);
  REQUIRE(dwg != nullptr);
  Dwg_Object* m = dwg_model_space_object(dwg);
  REQUIRE(m != nullptr);
  Dwg_Object_BLOCK_HEADER* hdr = m->tio.object->tio.BLOCK_HEADER;
  REQUIRE(hdr != nullptr);

  const dwg_point_3d fitPts[3] = {{0.0, 0.0, 0.0}, {5.0, 5.0, 0.0}, {10.0, 0.0, 0.0}};
  const dwg_point_3d tan0{1.0, 0.0, 0.0};
  const dwg_point_3d tan1{1.0, 0.0, 0.0};
  REQUIRE(dwg_add_SPLINE(hdr, 3, fitPts, &tan0, &tan1) != nullptr);

  const dwg_point_3d center{20.0, 0.0, 0.0};
  Dwg_Entity_ELLIPSE* ell = dwg_add_ELLIPSE(hdr, &center, 5.0, 0.5);
  REQUIRE(ell != nullptr);
  ell->start_angle = 0.0;
  ell->end_angle = 1.5707963267948966;

  LibreDwgLinkBlockEntities(dwg);
  REQUIRE(dwg_write_file(p.c_str(), dwg) == 0);
  dwg_free(dwg);
  std::free(dwg);

  AppCommandState st;
  std::vector<std::string> log;
  REQUIRE(ImportDwgFile(st, p.c_str(), log));
  REQUIRE(st.userPolylineOffsets.size() >= 2);
  CHECK(st.userPolylineVerts.size() >= 12);
}

TEST_CASE("DWG import maps POINT LEADER SOLID 3DFACE and mesh (REQ-170, issue #613)",
          "[dwg][libredwg][req170][issue613]") {
  ScratchDir dir("open-misc-entities");
  const auto p = (dir.path / "misc.dwg").string();
  Dwg_Data* dwg = dwg_new_Document(R_2000, 0, 0);
  REQUIRE(dwg != nullptr);
  Dwg_Object* m = dwg_model_space_object(dwg);
  REQUIRE(m != nullptr);
  Dwg_Object_BLOCK_HEADER* hdr = m->tio.object->tio.BLOCK_HEADER;
  REQUIRE(hdr != nullptr);

  const dwg_point_3d pt{1.0, 2.0, 0.0};
  REQUIRE(dwg_add_POINT(hdr, &pt) != nullptr);

  const dwg_point_3d mtextPt{8.0, 8.0, 0.0};
  Dwg_Entity_MTEXT* mt = dwg_add_MTEXT(hdr, &mtextPt, 12.0, "Leader note");
  REQUIRE(mt != nullptr);
  const dwg_point_3d lpts[2] = {{0.0, 0.0, 0.0}, {8.0, 8.0, 0.0}};
  REQUIRE(dwg_add_LEADER(hdr, 2, lpts, mt, 0) != nullptr);

  const dwg_point_3d s1{10.0, 0.0, 0.0};
  const dwg_point_2d s2{15.0, 0.0};
  const dwg_point_2d s3{15.0, 5.0};
  const dwg_point_2d s4{10.0, 5.0};
  REQUIRE(dwg_add_SOLID(hdr, &s1, &s2, &s3, &s4) != nullptr);

  const dwg_point_3d f1{0.0, 10.0, 0.0};
  const dwg_point_3d f2{5.0, 10.0, 0.0};
  const dwg_point_3d f3{5.0, 15.0, 0.0};
  const dwg_point_3d f4{0.0, 15.0, 0.0};
  REQUIRE(dwg_add_3DFACE(hdr, &f1, &f2, &f3, &f4) != nullptr);

  LibreDwgLinkBlockEntities(dwg);
  REQUIRE(dwg_write_file(p.c_str(), dwg) == 0);
  dwg_free(dwg);
  std::free(dwg);

  AppCommandState st;
  std::vector<std::string> log;
  REQUIRE(ImportDwgFile(st, p.c_str(), log));
  CHECK(st.cadPositionMarkers.size() == 1);
  CHECK(st.cadPositionMarkers[0].x == Catch::Approx(1.0).margin(0.01));
  CHECK(st.cadFilledRegions.size() >= 1);
  REQUIRE(st.cadMeshes.size() >= 1);
  CHECK(st.cadMeshes[0]->triangleCount() >= 2);
  REQUIRE(st.cadMultileaders.size() >= 1);
  CHECK(st.cadMultileaders[0].label.text.find("Leader note") != std::string::npos);
  CHECK(st.cadMultileaders[0].pathXyz.size() >= 6);
}

TEST_CASE("DWG import maps POLYLINE_PFACE to CadMesh (REQ-170, issue #613)",
          "[dwg][libredwg][req170][issue613]") {
  ScratchDir dir("open-pface");
  const auto p = (dir.path / "pface.dwg").string();
  Dwg_Data* dwg = dwg_new_Document(R_2000, 0, 0);
  REQUIRE(dwg != nullptr);
  Dwg_Object* m = dwg_model_space_object(dwg);
  REQUIRE(m != nullptr);
  Dwg_Object_BLOCK_HEADER* hdr = m->tio.object->tio.BLOCK_HEADER;
  REQUIRE(hdr != nullptr);

  const dwg_point_3d verts[4] = {{0.0, 0.0, 0.0}, {10.0, 0.0, 0.0}, {10.0, 10.0, 0.0}, {0.0, 10.0, 0.0}};
  const dwg_face faces[2] = {{1, 2, 3, 0}, {1, 3, 4, 0}};
  REQUIRE(dwg_add_POLYLINE_PFACE(hdr, 4, 2, verts, faces) != nullptr);

  LibreDwgLinkBlockEntities(dwg);
  REQUIRE(dwg_write_file(p.c_str(), dwg) == 0);
  dwg_free(dwg);
  std::free(dwg);

  Dwg_Data chk;
  std::memset(&chk, 0, sizeof(chk));
  REQUIRE(dwg_read_file(p.c_str(), &chk) < DWG_ERR_CRITICAL);
  int pfaceCount = 0;
  for (BITCODE_BL i = 0; i < chk.num_objects; ++i) {
    if (chk.object[i].fixedtype == DWG_TYPE_POLYLINE_PFACE)
      ++pfaceCount;
  }
  dwg_free(&chk);
  REQUIRE(pfaceCount >= 1);

  AppCommandState st;
  std::vector<std::string> log;
  REQUIRE(ImportDwgFile(st, p.c_str(), log));
  REQUIRE(st.cadMeshes.size() == 1);
  CHECK(st.cadMeshes[0]->triangleCount() == 2);
  CHECK(st.cadMeshes[0]->vertexCount() == 4);
}
