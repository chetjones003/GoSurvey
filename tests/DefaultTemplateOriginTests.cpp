// The bundled default-template.gst is what EVERY new drawing starts from (main.cpp's startup
// path). Its worldDocumentOriginX/Y must be exactly (0,0): REQ-101's precision-rebase mechanism
// (MaybeEstablishDocumentOriginFromTypedPoint) only fires once per drawing, guarded by "origin ==
// (0,0)" — a template shipped with any other value permanently disables that safety net for every
// new drawing, silently forcing every typed/imported large (state-plane / UTM scale) coordinate to
// be stored at full magnitude instead of rebased near zero, which then loses precision at every one
// of the many `float` locals throughout the app (found via user report: a circle typed at
// 1887731.488,303429.767 was stored ~13.6 units off in X because the shipped template carried a
// stray worldDocumentOriginX of ~13.607 from whoever authored it, baked in by mistake).

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "CadCommands.hpp"
#include "GsIo.hpp"

TEST_CASE("Bundled default-template.gst has a zero document origin", "[io][template][req101]") {
  std::ifstream f("resources/default-template.gst", std::ios::binary);
  REQUIRE(f.good());
  std::ostringstream ss;
  ss << f.rdbuf();
  const std::string json = ss.str();

  AppCommandState st;
  std::vector<std::string> log;
  REQUIRE(LoadGoSurveyFromJsonUtf8(st, json, log));

  CHECK(st.worldDocumentOriginX == 0.0);
  CHECK(st.worldDocumentOriginY == 0.0);
}
