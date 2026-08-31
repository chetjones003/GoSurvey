#include "DxfIo.hpp"
#include "DwgIo.hpp"
#include "LibreDwgCad.hpp"

#include "CadCommands.hpp"
#include "SurveyPoints.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

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

// issue #140 / DEBT-151-a — end-to-end against a real LibreDWG-decoded file: a multi-layer table
// must import names, colours (incl. off-layer negative ACI), assigned linetypes and freeze/lock
// flags intact. Fixture is R2000 because LibreDWG 0.13.3's own encoder does not round-trip
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
      ly->color.method = 0xc2;
      ly->on = s.on ? 1 : 0;
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
