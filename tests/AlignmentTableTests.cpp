// REQ-492.3 / REQ-493 / GitHub issue #121 Phase 4 - curve label text, segment labelling and drawing tables.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <string>
#include <vector>

#include "CadCommands.hpp"
#include "GsIo.hpp"
#include "alignment/Alignment.hpp"
#include "alignment/AlignmentTables.hpp"

using alignment::Alignment;
using alignment::AlignmentTableKind;
using Catch::Approx;

namespace {

constexpr double kDeg = alignment::kPi / 180.0;

/// Tangent 250 N45E -> right curve R500 delta 15 -> tangent 200: two tangents and one curve (REQ-493 acceptance 1).
Alignment Example() {
  Alignment a;
  a.name = "MAIN";
  REQUIRE(a.AppendTangent({0.0, 0.0}, 45.0 * kDeg, 250.0));
  REQUIRE(a.AppendCurveByDelta(500.0, 15.0 * kDeg, true));
  REQUIRE(a.AppendTangent(200.0));
  return a;
}

int DataRows(const alignment::AlignmentTableText& t) {
  return static_cast<int>(t.cells.size() / static_cast<size_t>(t.cols)) - 1;  // minus the header row
}

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
  st.displayLinearPrecision = 2;  // the expected strings below are two-decimal; the drawing default is four
  return st;
}

/// The first drawing table of the state whose cells contain `needle`, or null.
const CadTable* TableWith(const AppCommandState& st, const std::string& needle) {
  for (const CadTable& t : st.cadTables) {
    for (const std::string& c : t.cells) {
      if (c.find(needle) != std::string::npos) return &t;
    }
  }
  return nullptr;
}

}  // namespace

TEST_CASE("Curve label reads in the spec's layout", "[req492][alignment]") {
  alignment::CurveData cd;
  REQUIRE(Example().CurveDataFor(1, &cd));
  const std::string label = alignment::CurveLabelText(cd, 2, AngleDisplaySettings{});
  CHECK(label.find("R = 500.00'") != std::string::npos);
  CHECK(label.find("L = 130.90'") != std::string::npos);
  CHECK(label.find("T = 65.83'") != std::string::npos);
  CHECK(label.find("\xCE\x94 = ") != std::string::npos);  // Delta
}

TEST_CASE("Line and curve tables need labelled segments, and equation tables do not", "[req493][alignment]") {
  const Alignment a = Example();
  alignment::AlignmentTableText t;
  std::string why;
  CHECK_FALSE(alignment::BuildAlignmentTableText(a, AlignmentTableKind::Line, 2, AngleDisplaySettings{}, &t, &why));
  CHECK(why.find("ALIGNMENTLABEL") != std::string::npos);  // REQ-493.3: the refusal names what to do
  CHECK(why.find("segment label") != std::string::npos);
  CHECK(t.cells.empty());
  CHECK(alignment::BuildAlignmentTableText(a, AlignmentTableKind::StationEquation, 2, AngleDisplaySettings{}, &t, &why));
}

TEST_CASE("A labelled three-entity alignment gives a 2-row line table and a 1-row curve table", "[req493][alignment]") {
  Alignment a = Example();
  a.segmentLabels = true;
  alignment::AlignmentTableText line, curve;
  std::string why;
  REQUIRE(alignment::BuildAlignmentTableText(a, AlignmentTableKind::Line, 2, AngleDisplaySettings{}, &line, &why));
  REQUIRE(alignment::BuildAlignmentTableText(a, AlignmentTableKind::Curve, 2, AngleDisplaySettings{}, &curve, &why));
  CHECK(line.cols == 5);
  CHECK(DataRows(line) == 2);
  CHECK(curve.cols == 9);
  CHECK(DataRows(curve) == 1);
  CHECK(line.cells[0] == "Line");
  CHECK(line.cells[14] == "200.00");      // second tangent's length: row 2 starts at cell 10, length is its 5th cell
  CHECK(curve.cells[12] == "500.00");     // radius: row 1 starts at cell 9, radius is its 4th cell
  CHECK(curve.cells[14] == "130.90");     // arc length
  CHECK(curve.cells[15] == "65.83");      // tangent length
}

TEST_CASE("Segment table lists every entity in travel order", "[req493][alignment]") {
  Alignment a = Example();
  a.segmentLabels = true;
  alignment::AlignmentTableText seg;
  std::string why;
  REQUIRE(alignment::BuildAlignmentTableText(a, AlignmentTableKind::Segment, 2, AngleDisplaySettings{}, &seg, &why));
  CHECK(DataRows(seg) == 3);
  CHECK(seg.cells[9] == "L1");           // rows are named by their tags, as Civil 3D's tables are
  CHECK(seg.cells[10] == "Tangent");
  CHECK(seg.cells[18] == "C1");          // row 2 starts at cell 18
  CHECK(seg.cells[19] == "Curve");
  CHECK(seg.cells[27] == "L2");
}

TEST_CASE("Tags number each entity kind in travel order", "[req493][alignment]") {
  Alignment a;
  REQUIRE(a.AppendTangent({0.0, 0.0}, 0.0, 100.0));
  REQUIRE(a.AppendSpiral(50.0, 0.0, 400.0, true));
  REQUIRE(a.AppendCurveByDelta(400.0, 10.0 * kDeg, true));
  REQUIRE(a.AppendSpiral(50.0, 400.0, 0.0, true));
  REQUIRE(a.AppendTangent(100.0));
  REQUIRE(a.AppendCurveByDelta(300.0, 8.0 * kDeg, false));
  CHECK(alignment::SegmentTags(a) == std::vector<std::string>{"L1", "S1", "C1", "S2", "L2", "C2"});
  CHECK(alignment::SegmentTags(Alignment{}).empty());
}

TEST_CASE("A spiral table lists each spiral's length, A, radii and deflection", "[req493][alignment]") {
  Alignment a;
  a.segmentLabels = true;
  REQUIRE(a.AppendTangent({0.0, 0.0}, 0.0, 100.0));
  REQUIRE(a.AppendSpiral(50.0, 0.0, 400.0, true));
  REQUIRE(a.AppendCurveByDelta(400.0, 10.0 * kDeg, true));
  alignment::AlignmentTableText t, seg;
  std::string why;
  REQUIRE(alignment::BuildAlignmentTableText(a, AlignmentTableKind::Spiral, 2, AngleDisplaySettings{}, &t, &why));
  REQUIRE(t.cols == 9);
  REQUIRE(DataRows(t) == 1);
  CHECK(t.cells[0] == "Spiral");
  CHECK(t.cells[9] == "S1");
  CHECK(t.cells[12] == "50.00");                              // length
  CHECK(t.cells[13] == "141.42");                             // A = sqrt(400 * 50)
  CHECK(t.cells[14] == "\xE2\x88\x9E");                       // tangent end: infinite radius
  CHECK(t.cells[15] == "400.00");
  CHECK(t.cells[17] == "Right");
  REQUIRE(alignment::BuildAlignmentTableText(a, AlignmentTableKind::Segment, 2, AngleDisplaySettings{}, &seg, &why));
  CHECK(seg.cells[19] == "Spiral");  // a spiral is not misread as a curve
  CHECK(seg.cells[22] == "50.00");
}

TEST_CASE("SPLIT= divides a table into parts side by side; the last part takes rows an edit adds",
          "[req493][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  Submit(st, "ALIGNMENT SP", log);
  Submit(st, "ALIGNMENTADD SP, TANGENT, 0, 0, 90, 100", log);
  for (int i = 0; i < 4; ++i) {
    Submit(st, "ALIGNMENTADD SP, CURVE, 500, 5, RIGHT", log);
    Submit(st, "ALIGNMENTADD SP, TANGENT, 100", log);
  }
  Submit(st, "ALIGNMENTLABEL SP, SEGMENTS", log);
  log.clear();
  Submit(st, "ALIGNMENTTABLE SP, LINE, SPLIT=2, 0, 0", log);  // 5 lines: parts of 2, 2 and 1 rows
  CHECK(LogHas(log, "in 3 tables"));
  REQUIRE(st.cadTables.size() == 3);
  REQUIRE(st.alignments[0].tables.size() == 3);
  CHECK(st.cadTables[0].cells.size() == 15);
  CHECK(st.cadTables[1].cells.size() == 15);
  CHECK(st.cadTables[2].cells.size() == 10);
  CHECK(st.cadTables[1].cells[0] == "Line");  // each part repeats the header
  CHECK(st.cadTables[1].cells[5] == "L3");
  CHECK(st.cadTables[2].cells[5] == "L5");
  CHECK(st.cadTables[1].insX > st.cadTables[0].insX + st.cadTables[0].width);  // parts sit side by side
  CHECK(st.cadTables[1].insY == st.cadTables[0].insY);
  Submit(st, "ALIGNMENTADD SP, CURVE, 500, 5, RIGHT", log);
  Submit(st, "ALIGNMENTADD SP, TANGENT, 100", log);
  CHECK(st.cadTables[2].cells.size() == 15);  // L5 and the new L6
  CHECK(st.cadTables[0].cells.size() == 15);

  const std::string json = SerializeGoSurveyJson(st);
  AppCommandState back = Drawing();
  std::vector<std::string> loadLog;
  REQUIRE(LoadGoSurveyFromJsonUtf8(back, json, loadLog));
  REQUIRE(back.alignments[0].tables.size() == 3);
  CHECK(back.alignments[0].tables[1].firstRow == 2);
  CHECK(back.alignments[0].tables[1].rowCount == 2);
  CHECK(back.alignments[0].tables[2].rowCount == 0);

  log.clear();
  Submit(st, "ALIGNMENTTABLE SP, LINE, SPLIT=0", log);
  CHECK(LogHas(log, "SPLIT= needs a whole number"));
  CHECK(st.cadTables.size() == 3);
}

TEST_CASE("Alignment tables use the current or named data table style", "[req493][alignment]") {
  AppCommandState st = Drawing();
  CadTableStyle big = CadTableBuiltinStyles().front();
  big.name = "Big";
  big.textHeightInches = 0.25f;
  st.tableStyles = CadTableBuiltinStyles();
  st.tableStyles.push_back(big);
  std::vector<std::string> log;
  Submit(st, "ALIGNMENT TS", log);
  Submit(st, "ALIGNMENTADD TS, TANGENT, 0, 0, 90, 100", log);
  Submit(st, "ALIGNMENTLABEL TS, SEGMENTS", log);
  Submit(st, "ALIGNMENTTABLE TS, LINE", log);
  REQUIRE(st.cadTables.size() == 1);
  CHECK(st.cadTables[0].styleName == "Standard");
  st.tablePendingStyle = "Big";
  Submit(st, "ALIGNMENTTABLE TS, LINE", log);
  REQUIRE(st.cadTables.size() == 2);
  CHECK(st.cadTables[1].styleName == "Big");
  CHECK(st.cadTables[1].plottedHeightInches == 0.25f);
  Submit(st, "ALIGNMENTTABLE TS, LINE, STYLE=Standard", log);
  REQUIRE(st.cadTables.size() == 3);
  CHECK(st.cadTables[2].styleName == "Standard");
  log.clear();
  Submit(st, "ALIGNMENTTABLE TS, LINE, STYLE=NameNEZD", log);  // a point table style cannot hold alignment rows
  CHECK(LogHas(log, "no data table style named"));
  CHECK(st.cadTables.size() == 3);
}

TEST_CASE("Segment labels become tags once a segment table lists them", "[req493][alignment]") {
  Alignment a = Example();
  a.segmentLabels = true;
  CHECK_FALSE(alignment::ShowsTagLabels(a));
  a.tables.push_back({7, AlignmentTableKind::StationEquation, true});
  CHECK_FALSE(alignment::ShowsTagLabels(a));  // an equation table lists no segments
  a.tables.push_back({8, AlignmentTableKind::Curve, false});
  CHECK(alignment::ShowsTagLabels(a));
  a.segmentLabels = false;
  CHECK_FALSE(alignment::ShowsTagLabels(a));
}

TEST_CASE("ALIGNMENTTABLE refuses an unlabelled alignment and names the label command", "[req493][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  Submit(st, "ALIGNMENT T1", log);
  Submit(st, "ALIGNMENTADD T1, TANGENT, 0, 0, 90, 100", log);
  log.clear();
  Submit(st, "ALIGNMENTTABLE T1, LINE", log);
  CHECK(st.cadTables.empty());
  CHECK(LogHas(log, "ALIGNMENTLABEL T1, SEGMENTS"));
  CHECK(LogHas(log, "No table was made"));
}

TEST_CASE("Dynamic tables follow an edit and static tables do not", "[req493][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  Submit(st, "ALIGNMENT T2", log);
  Submit(st, "ALIGNMENTADD T2, TANGENT, 0, 0, 45, 250", log);
  Submit(st, "ALIGNMENTADD T2, CURVE, 500, 15, RIGHT", log);
  Submit(st, "ALIGNMENTADD T2, TANGENT, 200", log);
  Submit(st, "ALIGNMENTLABEL T2, SEGMENTS", log);
  Submit(st, "ALIGNMENTTABLE T2, CURVE, DYNAMIC, 0, 0", log);
  Submit(st, "ALIGNMENTTABLE T2, CURVE, STATIC, 0, -200", log);
  REQUIRE(st.cadTables.size() == 2);
  REQUIRE(st.alignments[0].tables.size() == 2);
  CHECK(st.alignments[0].tables[0].dynamic);
  CHECK_FALSE(st.alignments[0].tables[1].dynamic);
  REQUIRE(TableWith(st, "500.00"));

  Submit(st, "ALIGNMENTEDIT T2, 2, RADIUS, 400", log);
  const CadTable* dyn = TableWith(st, "400.00");
  REQUIRE(dyn != nullptr);  // the dynamic table now shows the new radius
  CHECK(dyn->cells[12] == "400.00");
  // The static table still shows the radius it was made with.
  int staticWith500 = 0;
  for (const CadTable& t : st.cadTables) {
    if (t.cells.size() > 12 && t.cells[12] == "500.00") ++staticWith500;
  }
  CHECK(staticWith500 == 1);
}

TEST_CASE("Segment labels and table links survive save and reopen", "[req493][req496][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  Submit(st, "ALIGNMENT P2", log);
  Submit(st, "ALIGNMENTADD P2, TANGENT, 0, 0, 90, 100", log);
  Submit(st, "ALIGNMENTLABEL P2, SEGMENTS", log);
  Submit(st, "ALIGNMENTTABLE P2, LINE, DYNAMIC", log);
  REQUIRE(st.alignments[0].tables.size() == 1);
  const std::uint64_t linkedId = st.alignments[0].tables[0].tableId;
  REQUIRE(FindEntityIndexById(st.cadTableAttrs, linkedId) >= 0);

  const std::string json = SerializeGoSurveyJson(st);
  AppCommandState back = Drawing();
  std::vector<std::string> loadLog;
  REQUIRE(LoadGoSurveyFromJsonUtf8(back, json, loadLog));
  REQUIRE(back.alignments.size() == 1);
  CHECK(back.alignments[0].segmentLabels);
  REQUIRE(back.alignments[0].tables.size() == 1);
  CHECK(back.alignments[0].tables[0].tableId == linkedId);
  CHECK(back.alignments[0].tables[0].dynamic);
  CHECK(back.alignments[0].tables[0].kind == AlignmentTableKind::Line);
  CHECK(FindEntityIndexById(back.cadTableAttrs, linkedId) >= 0);
}

TEST_CASE("Clearing the segment labels stops a line table updating and says so", "[req493][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  Submit(st, "ALIGNMENT P3", log);
  Submit(st, "ALIGNMENTADD P3, TANGENT, 0, 0, 90, 100", log);
  Submit(st, "ALIGNMENTLABEL P3, SEGMENTS", log);
  Submit(st, "ALIGNMENTTABLE P3, LINE, DYNAMIC", log);
  log.clear();
  Submit(st, "ALIGNMENTLABEL P3, CLEAR", log);
  CHECK(LogHas(log, "cleared"));
  log.clear();
  Submit(st, "ALIGNMENTEDIT P3, 1, LENGTH, 150", log);
  CHECK(LogHas(log, "not updated"));
}
