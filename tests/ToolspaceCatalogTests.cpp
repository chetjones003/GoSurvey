#include <catch2/catch_test_macros.hpp>

#include "ToolspaceCatalog.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace {

bool HasLine(const std::vector<std::string>& lines, const std::string& want) {
  return std::find(lines.begin(), lines.end(), want) != lines.end();
}

bool AnyForbidden(const std::vector<std::string>& lines) {
  for (const std::string& line : lines) {
    if (ToolspaceLineIsForbiddenCivil3d(line))
      return true;
  }
  return false;
}

}  // namespace

TEST_CASE("empty drawing prospector lists implemented collections only", "[req142][toolspace]") {
  AppCommandState st;
  std::vector<std::string> lines;
  AppendToolspaceProspectorLines(st, &lines);
  REQUIRE(HasLine(lines, "Drawing 1"));
  REQUIRE(HasLine(lines, "Points"));
  REQUIRE(HasLine(lines, "Point Groups"));
  REQUIRE(HasLine(lines, "Surfaces"));
  REQUIRE(HasLine(lines, "Feature Lines"));
  REQUIRE_FALSE(AnyForbidden(lines));
}

TEST_CASE("named groups and surfaces appear in prospector", "[req142][toolspace]") {
  AppCommandState st;
  PointGroup g;
  g.name = "Existing Ground";
  st.pointGroups.push_back(g);
  CadSurface s;
  s.name = "EG TIN";
  st.cadSurfaces.push_back(s);
  std::vector<std::string> lines;
  AppendToolspaceProspectorLines(st, &lines);
  REQUIRE(HasLine(lines, "Existing Ground"));
  REQUIRE(HasLine(lines, "EG TIN"));
  REQUIRE(HasLine(lines, "Definition"));
  REQUIRE(HasLine(lines, "Masks"));
  REQUIRE(HasLine(lines, "Watersheds"));
  REQUIRE(HasLine(lines, "Boundaries"));
  REQUIRE(HasLine(lines, "Breaklines"));
  REQUIRE_FALSE(HasLine(lines, "DEM Files"));
  REQUIRE_FALSE(AnyForbidden(lines));
}

TEST_CASE("settings lists text and surface styles not parcel", "[req142][toolspace]") {
  AppCommandState st;
  std::vector<std::string> lines;
  AppendToolspaceSettingsLines(st, &lines);
  REQUIRE(HasLine(lines, "General"));
  REQUIRE(HasLine(lines, "Text Styles"));
  REQUIRE(HasLine(lines, "Surface"));
  REQUIRE(HasLine(lines, "Surface Styles"));
  REQUIRE(HasLine(lines, "Standard"));
  REQUIRE_FALSE(HasLine(lines, "Parcel"));
  REQUIRE_FALSE(HasLine(lines, "Grading"));
  REQUIRE_FALSE(AnyForbidden(lines));
}

TEST_CASE("null catalog out is a no-op", "[req142][toolspace]") {
  AppCommandState st;
  AppendToolspaceProspectorLines(st, nullptr);
  AppendToolspaceSettingsLines(st, nullptr);
  REQUIRE(ToolspaceActiveDrawingName(st) == "Drawing 1");
}
