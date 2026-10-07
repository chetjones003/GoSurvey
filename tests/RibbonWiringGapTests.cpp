// GitHub issue #762 — ribbon "wiring gaps": commands that exist elsewhere must stay wired on
// Home/Insert. Static guard on CadUi.cpp so the inventory does not drift back to NYI placeholders.

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

std::string ReadCadUiSource() {
  const std::filesystem::path path =
      std::filesystem::path("..") / "src" / "ui" / "CadUi.cpp";
  std::ifstream in(path, std::ios::binary);
  REQUIRE(in.good());
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

bool Contains(const std::string& hay, const std::string& needle) {
  return hay.find(needle) != std::string::npos;
}

std::string FirstLineContaining(const std::string& src, const std::string& needle) {
  const size_t pos = src.find(needle);
  REQUIRE(pos != std::string::npos);
  const size_t lineStart = src.rfind('\n', pos);
  const size_t begin = lineStart == std::string::npos ? 0 : lineStart + 1;
  const size_t lineEnd = src.find('\n', pos);
  return src.substr(begin, lineEnd == std::string::npos ? std::string::npos : lineEnd - begin);
}

} // namespace

TEST_CASE("Issue #762 wiring gaps stay connected in CadUi.cpp", "[ribbon][issue762]") {
  const std::string src = ReadCadUiSource();

  CHECK(FirstLineContaining(src, "iconBtn(\"##RibbonArray\"").find("not implemented yet") ==
        std::string::npos);
  REQUIRE(Contains(src, "else if (id == \"##RibbonArray\") StartArrayCommand"));

  CHECK(FirstLineContaining(src, "iconBtn2Row(\"##PalProps\"").find("not implemented yet") ==
        std::string::npos);
  REQUIRE(Contains(src, "else if (id == \"##PalProps\") cmd.pendingPropertiesFocus = true"));

  CHECK(FirstLineContaining(src, "rowBtn(\"##RibbonInsPointsFile\"").find("not implemented yet") ==
        std::string::npos);
  REQUIRE(Contains(src, "else if (id == \"##RibbonInsPointsFile\")"));
  REQUIRE(Contains(src, "StartImportPointsCommand(cmd, log);  // issue #762"));
}
