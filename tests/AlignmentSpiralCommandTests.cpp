#include "alignment/Alignment.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "CadCommands.hpp"
#include "GsIo.hpp"

using alignment::Alignment;
using Catch::Approx;

namespace {

void Submit(AppCommandState& st, const std::string& text, std::vector<std::string>& log) {
  char buf[512];
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
  return st;
}

}  // namespace

TEST_CASE("ALIGNMENTADD and ALIGNMENTINSERT build clothoid spirals", "[req481][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  Submit(st, "ALIGNMENT S1", log);
  Submit(st, "ALIGNMENTADD S1, TANGENT, 0, 0, 0, 100", log);
  Submit(st, "ALIGNMENTADD S1, SPIRAL, 100, 500, RIGHT", log);
  Submit(st, "ALIGNMENTADD S1, CURVE, 500, 10, RIGHT", log);
  REQUIRE(st.alignments.size() == 1);
  const alignment::Alignment& a = st.alignments[0];
  REQUIRE(a.Entities().size() == 3);
  CHECK(a.SpiralCount() == 1);
  CHECK(a.Entities()[1].kind == alignment::EntityKind::Spiral);
  CHECK(a.Entities()[1].length == Approx(100.0));
  CHECK(a.Entities()[1].radius == Approx(500.0));

  log.clear();
  Submit(st, "ALIGNMENTLIST S1", log);
  CHECK(LogHas(log, "Spiral"));
  CHECK(LogHas(log, "Spirals: 1"));

  log.clear();
  Submit(st, "ALIGNMENTINSERT S1, 1, SPIRAL, 50, 0, 300, LEFT", log);
  REQUIRE(st.alignments[0].Entities().size() == 4);
  CHECK(st.alignments[0].SpiralCount() == 2);
}

TEST_CASE("ALIGNMENTEDIT changes spiral length and end radius", "[req481][req486][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  Submit(st, "ALIGNMENT E1", log);
  Submit(st, "ALIGNMENTADD E1, TANGENT, 0, 0, 0, 50", log);
  Submit(st, "ALIGNMENTADD E1, SPIRAL, 100, 500, RIGHT", log);
  const double len0 = st.alignments[0].Length();
  log.clear();
  Submit(st, "ALIGNMENTEDIT E1, 2, LENGTH, 120", log);
  CHECK(st.alignments[0].Entities()[1].length == Approx(120.0));
  CHECK(st.alignments[0].Length() == Approx(len0 + 20.0));
  log.clear();
  Submit(st, "ALIGNMENTEDIT E1, 2, RADIUS, 400", log);
  CHECK(st.alignments[0].Entities()[1].radius == Approx(400.0));
}

TEST_CASE("Spiral alignments round-trip through document JSON", "[req496][req481][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  Submit(st, "ALIGNMENT Spirals", log);
  Submit(st, "ALIGNMENTADD Spirals, TANGENT, 0, 0, 90, 200", log);
  Submit(st, "ALIGNMENTADD Spirals, SPIRAL, 100, 0, 500, RIGHT", log);
  const alignment::Alignment& a = st.alignments[0];
  const std::string json = SerializeGoSurveyJson(st);
  AppCommandState back = Drawing();
  std::vector<std::string> loadLog;
  REQUIRE(LoadGoSurveyFromJsonUtf8(back, json, loadLog));
  REQUIRE(back.alignments.size() == 1);
  REQUIRE(back.alignments[0].Entities().size() == a.Entities().size());
  CHECK(back.alignments[0].Entities()[1].kind == alignment::EntityKind::Spiral);
  CHECK(back.alignments[0].Entities()[1].startRadius == a.Entities()[1].startRadius);
  CHECK(back.alignments[0].Entities()[1].radius == a.Entities()[1].radius);
  CHECK(back.alignments[0].EndPoint().x == Approx(a.EndPoint().x));
  CHECK(back.alignments[0].EndPoint().y == Approx(a.EndPoint().y));
}

TEST_CASE("ALIGNMENTCONTINUITY sets the check level and persists", "[req482][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  Submit(st, "ALIGNMENT C1", log);
  Submit(st, "ALIGNMENTADD C1, TANGENT, 0, 0, 0, 50", log);
  Submit(st, "ALIGNMENTADD C1, SPIRAL, 100, 500, RIGHT", log);
  Submit(st, "ALIGNMENTADD C1, CURVE, 400, 40, RIGHT", log);
  log.clear();
  Submit(st, "ALIGNMENTCONTINUITY C1, C2", log);
  CHECK(st.alignments[0].continuityRequirement == alignment::ContinuityRequirement::C2);
  CHECK(LogHas(log, "C2"));
  log.clear();
  Submit(st, "ALIGNMENTCONTINUITY C1, LIST", log);
  CHECK((LogHas(log, "curvature mismatch") || LogHas(log, "WARNING")));
  const std::string json = SerializeGoSurveyJson(st);
  AppCommandState back = Drawing();
  std::vector<std::string> loadLog;
  REQUIRE(LoadGoSurveyFromJsonUtf8(back, json, loadLog));
  CHECK(back.alignments[0].continuityRequirement == alignment::ContinuityRequirement::C2);
}

TEST_CASE("Spiral end grip adjusts length", "[req487][req481][alignment]") {
  Alignment a;
  REQUIRE(a.AppendTangent({0, 0}, 0.0, 50.0));
  REQUIRE(a.AppendSpiral(100.0, 0.0, 500.0, true));
  const auto grips = alignment::GripPoints(a);
  REQUIRE(grips.size() >= 2);
  Alignment out;
  const alignment::Point2 spStart = a.Entities()[1].start;
  int spiralEnd = -1;
  for (const alignment::Grip& g : grips)
    if (g.kind == alignment::GripKind::SpiralEnd && g.entity == 1) spiralEnd = g.which;
  REQUIRE(spiralEnd > 0);
  REQUIRE(alignment::DragGrip(a, spiralEnd, {spStart.x, spStart.y + 119.9}, &out));
  CHECK(out.Entities()[1].length == Approx(119.9).margin(0.05));
  CHECK(out.Entities()[1].length > a.Entities()[1].length);
}

TEST_CASE("Spiral mid grip adjusts end radius", "[req487][req481][alignment]") {
  Alignment a;
  REQUIRE(a.AppendTangent({0, 0}, 0.0, 50.0));
  REQUIRE(a.AppendSpiral(100.0, 0.0, 500.0, true));
  const double r0 = a.Entities()[1].radius;
  const double staMid = a.EntityStartStation(1) + 50.0;
  const alignment::Point2 pMid = a.GetPointAtStation(staMid);
  Alignment out;
  int spiralMid = -1;
  for (const alignment::Grip& g : alignment::GripPoints(a))
    if (g.kind == alignment::GripKind::SpiralMid && g.entity == 1) spiralMid = g.which;
  REQUIRE(spiralMid > 0);
  REQUIRE(alignment::DragGrip(a, spiralMid, {pMid.x + 15.0, pMid.y + 5.0}, &out));
  CHECK(out.Entities()[1].radius != Approx(r0).margin(1e-9));
  CHECK(out.Entities()[1].radius > 0.0);
}
