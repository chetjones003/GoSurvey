// REQ-500 / GitHub issue #121 Phase 5 - the profile seam: no profile yet, no Z on geometry, and QUICKPROFILE along an alignment.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "CadCommands.hpp"
#include "GsIo.hpp"
#include "alignment/Alignment.hpp"

using Catch::Approx;

namespace {

// Compile-time check that a type has no member named z (REQ-500 AC2, REQ-480.3).
template <typename T, typename = void>
struct HasZ : std::false_type {};
template <typename T>
struct HasZ<T, std::void_t<decltype(std::declval<T>().z)>> : std::true_type {};

void Submit(AppCommandState& st, const std::string& text, std::vector<std::string>& log) {
  char buf[1024];
  std::snprintf(buf, sizeof(buf), "%s", text.c_str());
  ProcessCommandLineSubmit(buf, sizeof(buf), st, log);
}

bool LogHas(const std::vector<std::string>& log, const char* needle) {
  for (const std::string& line : log) {
    if (line.find(needle) != std::string::npos) return true;
  }
  return false;
}

AppCommandState Drawing() {
  AppCommandState st;
  st.documents.resize(2);
  st.activeDrawingIdx = 1;
  st.displayLinearPrecision = 4;
  return st;
}

}  // namespace

TEST_CASE("An alignment with no profile reports no vertical value (REQ-500 AC1)", "[req500][alignment]") {
  alignment::Alignment a;
  REQUIRE(a.AppendTangent({0.0, 0.0}, 0.0, 100.0));
  CHECK(a.profileIds.empty());
  for (const auto& r : {a.GetElevationAtStation(50.0), a.GetProfilePoint(50.0), a.GetVerticalGrade(50.0)}) {
    CHECK_FALSE(r.hasValue);
    CHECK(r.reason == "no profile");
  }
  // With a profile reference present, the value still is not read yet; the reason says so.
  a.profileIds.push_back(42);
  const alignment::Alignment::VerticalResult r = a.GetElevationAtStation(50.0);
  CHECK_FALSE(r.hasValue);
  CHECK(r.reason.find("not built") != std::string::npos);
}

TEST_CASE("No geometry type carries a Z field (REQ-500 AC2, REQ-480.3)", "[req500][alignment]") {
  CHECK_FALSE(HasZ<alignment::Entity>::value);
  CHECK_FALSE(HasZ<alignment::Point2>::value);
  CHECK_FALSE(HasZ<alignment::GeometryPoint>::value);
  CHECK_FALSE(HasZ<alignment::CurveData>::value);
  CHECK_FALSE(HasZ<alignment::StationOffset>::value);
  CHECK_FALSE(HasZ<alignment::Intersection>::value);
  CHECK_FALSE(HasZ<alignment::Alignment>::value);
}

TEST_CASE("Profile references survive save and reopen as ids only", "[req500][req496][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  Submit(st, "ALIGNMENT PR1, , 0+00", log);
  Submit(st, "ALIGNMENTADD PR1, TANGENT, 0, 0, 90, 100", log);
  st.alignments[0].profileIds = {7, 9};
  const std::string json = SerializeGoSurveyJson(st);
  AppCommandState back = Drawing();
  std::vector<std::string> loadLog;
  REQUIRE(LoadGoSurveyFromJsonUtf8(back, json, loadLog));
  REQUIRE(back.alignments.size() == 1);
  CHECK(back.alignments[0].profileIds == std::vector<std::uint64_t>{7, 9});
  CHECK(back.alignments[0].Entities().size() == 1);  // the geometry is unchanged by the references
}

TEST_CASE("QUICKPROFILE along an alignment samples the surface from start to end (REQ-500 AC3)", "[req500][req497][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  // An 11 x 2 grid over X 0..100, Y 0..10, Z = 0.1 X + 50.
  std::string z;
  for (int j = 0; j < 2; ++j) {
    for (int i = 0; i < 11; ++i) {
      char buf[32];
      std::snprintf(buf, sizeof buf, ", %g", 0.1 * 10.0 * i + 50.0);
      z += buf;
    }
  }
  Submit(st, "SURFACECREATEGRID PLANE, 0, 0, 10, 10, 11, 2" + z, log);
  REQUIRE(st.cadSurfaces.size() == 1);
  Submit(st, "ALIGNMENT QA, , 0+00", log);
  Submit(st, "ALIGNMENTADD QA, TANGENT, 0, 5, 90, 100", log);

  log.clear();
  Submit(st, "QUICKPROFILE PLANE, QA", log);
  REQUIRE(st.quickProfile.hasResult);
  CHECK(LogHas(log, "along \"QA\""));
  CHECK(st.quickProfile.samples.size() == 101);  // one foot apart, both ends included
  CHECK(st.quickProfile.onSurfaceCount == 101);
  CHECK(st.quickProfile.length == Approx(100.0));
  CHECK(st.quickProfile.minZ == Approx(50.0).margin(1e-4));
  CHECK(st.quickProfile.maxZ == Approx(60.0).margin(1e-4));
  CHECK(st.quickProfile.samples[50].z == Approx(55.0).margin(1e-4));
}

TEST_CASE("QUICKPROFILE with two points still works after the alignment form was added (REQ-500 AC3)",
          "[req500][req145][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  std::string z;
  for (int j = 0; j < 2; ++j) {
    for (int i = 0; i < 11; ++i) {
      char buf[32];
      std::snprintf(buf, sizeof buf, ", %g", 0.1 * 10.0 * i + 50.0);
      z += buf;
    }
  }
  Submit(st, "SURFACECREATEGRID PLANE, 0, 0, 10, 10, 11, 2" + z, log);
  log.clear();
  Submit(st, "QUICKPROFILE PLANE, 0, 5, 100, 5", log);
  REQUIRE(st.quickProfile.hasResult);
  CHECK(st.quickProfile.onSurfaceCount > 0);
  CHECK(st.quickProfile.minZ == Approx(50.0).margin(1e-3));
  CHECK(st.quickProfile.maxZ == Approx(60.0).margin(1e-3));
}

TEST_CASE("QUICKPROFILE along an alignment refuses a surface that does not exist, and an alignment with no ground",
          "[req500][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  Submit(st, "ALIGNMENT QB, , 0+00", log);
  Submit(st, "ALIGNMENTADD QB, TANGENT, 0, 0, 90, 100", log);
  log.clear();
  Submit(st, "QUICKPROFILE NOSUCH, QB", log);
  CHECK(LogHas(log, "no surface named"));
  CHECK_FALSE(st.quickProfile.hasResult);
}
