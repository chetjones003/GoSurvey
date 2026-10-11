#include "CadField.hpp"
#include "CadCommands.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

namespace {

AppCommandState MakePolylineSquare(AppCommandState st, std::uint64_t id, double side) {
  st.userPolylineOffsets = {0, 4};
  st.userPolylineClosed = {1};
  st.userPolylineVerts = {0, 0, 0, side, 0, 0, side, side, 0, 0, side, 0};
  st.userPolylineAttrs = {EntityAttributes{}};
  st.userPolylineAttrs[0].id = id;
  return st;
}

}  // namespace

TEST_CASE("CadField evaluates GoSurvey polyline area", "[cadfield][req368]") {
  AppCommandState st;
  st = MakePolylineSquare(std::move(st), 42, 100.0);
  const std::string wire = CadFieldMakeGoSurveyWire(42, "Area", ".2f");
  CadFieldContext ctx;
  ctx.drawingPath = "C:/jobs/lot17.gs";
  ctx.activeLayoutTabName = "Model";
  const std::string out = CadFieldEvaluateWire(st, "Area: " + wire, ctx);
  REQUIRE(out.find("10000") != std::string::npos);
}

TEST_CASE("CadField evaluates document filename var", "[cadfield][req368]") {
  AppCommandState st;
  const std::string wire = CadFieldMakeAcVarWire("Filename", "tc1");
  CadFieldContext ctx;
  ctx.drawingPath = "D:\\drawings\\sheet-A1.gs";
  const std::string out = CadFieldEvaluateWire(st, wire, ctx);
  REQUIRE(out == "sheet-A1.gs");
}

TEST_CASE("CadField R2000 export substitutes evaluated text", "[cadfield][req368]") {
  AppCommandState st;
  st = MakePolylineSquare(std::move(st), 7, 10.0);
  const std::string wire = CadFieldMakeGoSurveyWire(7, "Area", ".1f");
  CadFieldContext ctx;
  const std::string r2000 = CadFieldTextForDwgExport(st, wire, ctx, false);
  REQUIRE_FALSE(CadTextContainsFieldCodes(r2000));
  REQUIRE(r2000.find("100") != std::string::npos);
  const std::string r2004 = CadFieldTextForDwgExport(st, wire, ctx, true);
  REQUIRE(CadTextContainsFieldCodes(r2004));
}
