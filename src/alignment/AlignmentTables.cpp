#include "alignment/AlignmentTables.hpp"

#include "util/NumFormat.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <string>

namespace alignment {
namespace {

constexpr double kDegToRad = kPi / 180.0;

std::string Lin(int precision, double v) { return FormatLinear(v, precision); }

std::string Brg(const AngleDisplaySettings& angles, double azimuthRad) {
  return FormatBearing(azimuthRad / kDegToRad, angles);
}

std::string Sta(const Alignment& a, double internalStation, const StationFormat& fmt) {
  return FormatStation(a.DisplayedStation(internalStation, true), fmt);
}

std::string Dir(const Entity& e) { return e.right ? "Right" : "Left"; }

/// A spiral's zero radius is its tangent end (Alignment.hpp); \xE2\x88\x9E is the infinity sign in UTF-8.
std::string SpiralRadius(int precision, double r) { return r > 0.0 ? Lin(precision, r) : std::string("\xE2\x88\x9E"); }

/// Names the entity kinds a table would need labelled, so the refusal can say what is missing (REQ-493.3).
std::string MissingLabelsMessage(const Alignment& a) {
  return "the segments of \"" + a.name + "\" are not labelled yet; label them first with ALIGNMENTLABEL " + a.name +
         ", SEGMENTS (missing labels: " + std::to_string(a.Entities().size()) + " segment label(s))";
}

struct TableInput {
  const Alignment& a;
  int precision;
  const AngleDisplaySettings& angles;
  const StationFormat& station;
  std::vector<std::string> tags;
};

void Row(AlignmentTableText* out, std::initializer_list<std::string> cells) {
  out->cells.insert(out->cells.end(), cells.begin(), cells.end());
}

void LineRows(const TableInput& in, AlignmentTableText* out) {
  assert(out != nullptr);
  assert(in.tags.size() == in.a.Entities().size());
  out->cols = 5;
  Row(out, {"Line", "Start station", "End station", "Bearing", "Length"});
  for (int i = 0; i < static_cast<int>(in.tags.size()); ++i) {
    const Entity& e = in.a.Entities()[static_cast<size_t>(i)];
    if (e.kind != EntityKind::Line) continue;
    Row(out, {in.tags[static_cast<size_t>(i)], Sta(in.a, in.a.EntityStartStation(i), in.station),
              Sta(in.a, in.a.EntityEndStation(i), in.station), Brg(in.angles, e.startAzimuth), Lin(in.precision, e.length)});
  }
}

void CurveRows(const TableInput& in, AlignmentTableText* out) {
  assert(out != nullptr);
  assert(in.tags.size() == in.a.Entities().size());
  out->cols = 9;
  Row(out, {"Curve", "Start station", "End station", "Radius", "Delta", "Length", "Tangent", "Chord", "Direction"});
  for (int i = 0; i < static_cast<int>(in.tags.size()); ++i) {
    CurveData cd;
    if (!in.a.CurveDataFor(i, &cd)) continue;
    Row(out, {in.tags[static_cast<size_t>(i)], Sta(in.a, in.a.EntityStartStation(i), in.station),
              Sta(in.a, in.a.EntityEndStation(i), in.station), Lin(in.precision, cd.radius),
              FormatSweptAngle(cd.delta / kDegToRad, in.angles), Lin(in.precision, cd.arcLength),
              Lin(in.precision, cd.tangentLength), Lin(in.precision, cd.chord), Dir(in.a.Entities()[static_cast<size_t>(i)])});
  }
}

void SpiralRows(const TableInput& in, AlignmentTableText* out) {
  assert(out != nullptr);
  assert(in.tags.size() == in.a.Entities().size());
  out->cols = 9;
  Row(out, {"Spiral", "Start station", "End station", "Length", "A", "Start radius", "End radius", "Deflection", "Direction"});
  for (int i = 0; i < static_cast<int>(in.tags.size()); ++i) {
    SpiralData sd;
    if (in.a.Entities()[static_cast<size_t>(i)].kind != EntityKind::Spiral || !SpiralDataFor(i, in.a, &sd)) continue;
    Row(out, {in.tags[static_cast<size_t>(i)], Sta(in.a, in.a.EntityStartStation(i), in.station),
              Sta(in.a, in.a.EntityEndStation(i), in.station), Lin(in.precision, sd.length), Lin(in.precision, sd.parameterA),
              SpiralRadius(in.precision, sd.startRadius), SpiralRadius(in.precision, sd.endRadius),
              FormatSweptAngle(sd.deflection / kDegToRad, in.angles), sd.right ? "Right" : "Left"});
  }
}

void SegmentRows(const TableInput& in, AlignmentTableText* out) {
  assert(out != nullptr);
  assert(in.tags.size() == in.a.Entities().size());
  out->cols = 9;
  Row(out, {"Entity", "Type", "Start station", "End station", "Length", "Bearing", "Radius", "Delta", "Direction"});
  for (int i = 0; i < static_cast<int>(in.tags.size()); ++i) {
    const Entity& e = in.a.Entities()[static_cast<size_t>(i)];
    const std::string& tag = in.tags[static_cast<size_t>(i)];
    const std::string start = Sta(in.a, in.a.EntityStartStation(i), in.station);
    const std::string end = Sta(in.a, in.a.EntityEndStation(i), in.station);
    CurveData cd;
    SpiralData sd;
    if (e.kind == EntityKind::Line)
      Row(out, {tag, "Tangent", start, end, Lin(in.precision, e.length), Brg(in.angles, e.startAzimuth), "", "", ""});
    else if (e.kind == EntityKind::Arc && in.a.CurveDataFor(i, &cd))
      Row(out, {tag, "Curve", start, end, Lin(in.precision, cd.arcLength), "", Lin(in.precision, cd.radius),
                FormatSweptAngle(cd.delta / kDegToRad, in.angles), Dir(e)});
    else if (e.kind == EntityKind::Spiral && SpiralDataFor(i, in.a, &sd))
      Row(out, {tag, "Spiral", start, end, Lin(in.precision, sd.length), "",
                SpiralRadius(in.precision, sd.startRadius) + " - " + SpiralRadius(in.precision, sd.endRadius),
                FormatSweptAngle(sd.deflection / kDegToRad, in.angles), Dir(e)});
  }
}

void EquationRows(const TableInput& in, AlignmentTableText* out) {
  assert(out != nullptr);
  assert(in.a.equations.size() < 1000000);
  out->cols = 4;
  Row(out, {"Back station", "Ahead station", "Type", "Raw station"});
  for (const StationEquation& q : in.a.equations) {
    Row(out, {FormatStation(in.a.DisplayedStation(q.rawBack, false), in.station), FormatStation(q.ahead, in.station),
              q.increasing ? "Increasing" : "Decreasing", FormatStation(q.rawBack, in.station)});
  }
}

}  // namespace

std::vector<std::string> SegmentTags(const Alignment& a) {
  assert(a.Entities().size() < 100000000);
  std::vector<std::string> tags;
  tags.reserve(a.Entities().size());
  int lines = 0, curves = 0, spirals = 0;
  for (const Entity& e : a.Entities()) {
    if (e.kind == EntityKind::Line) tags.push_back("L" + std::to_string(++lines));
    else if (e.kind == EntityKind::Arc) tags.push_back("C" + std::to_string(++curves));
    else tags.push_back("S" + std::to_string(++spirals));
  }
  assert(tags.size() == a.Entities().size());
  return tags;
}

bool ShowsTagLabels(const Alignment& a) {
  assert(a.tables.size() < 1000000);
  if (!a.segmentLabels) return false;
  for (const AlignmentTableLink& link : a.tables)
    if (link.kind != AlignmentTableKind::StationEquation) return true;
  return false;
}

bool BuildAlignmentTableText(const Alignment& a, AlignmentTableKind kind, int linearPrecision,
                             const AngleDisplaySettings& angles, AlignmentTableText* out, std::string* whyNot,
                             const StationFormat& station) {
  auto fail = [&](const std::string& why) {
    if (whyNot != nullptr) *whyNot = why;
    return false;
  };
  if (out == nullptr) return false;
  *out = AlignmentTableText{};
  if (a.Empty()) return fail("\"" + a.name + "\" has no geometry to tabulate");
  const bool needsLabels = kind != AlignmentTableKind::StationEquation;
  if (needsLabels && !a.segmentLabels) return fail(MissingLabelsMessage(a));
  const TableInput in{a, linearPrecision, angles, station, SegmentTags(a)};
  switch (kind) {
    case AlignmentTableKind::Line: LineRows(in, out); return true;
    case AlignmentTableKind::Curve: CurveRows(in, out); return true;
    case AlignmentTableKind::Spiral: SpiralRows(in, out); return true;
    case AlignmentTableKind::Segment: SegmentRows(in, out); return true;
    case AlignmentTableKind::StationEquation: EquationRows(in, out); return true;
  }
  return fail("unknown table kind");
}

AlignmentTableText SliceTableRows(const AlignmentTableText& full, int firstRow, int rowCount) {
  assert(full.cols > 0);
  assert(firstRow >= 0 && rowCount >= 0);
  AlignmentTableText part;
  part.cols = full.cols;
  if (full.cols <= 0 || full.cells.size() < static_cast<size_t>(full.cols)) return part;
  const size_t cols = static_cast<size_t>(full.cols);
  const size_t dataRows = full.cells.size() / cols - 1;
  const size_t from = std::min(dataRows, static_cast<size_t>(std::max(0, firstRow)));
  const size_t to = rowCount <= 0 ? dataRows : std::min(dataRows, from + static_cast<size_t>(rowCount));
  part.cells.assign(full.cells.begin(), full.cells.begin() + static_cast<std::ptrdiff_t>(cols));  // header
  part.cells.insert(part.cells.end(), full.cells.begin() + static_cast<std::ptrdiff_t>(cols * (from + 1)),
                    full.cells.begin() + static_cast<std::ptrdiff_t>(cols * (to + 1)));
  return part;
}

std::vector<std::string> CurveLabelLines(const CurveData& c, int linearPrecision, const AngleDisplaySettings& angles) {
  // \xCE\x94 is the Greek capital delta in UTF-8, as the spec writes it. Feet, as the spec example reads (REQ-492.3);
  // metric label units are a later format setting, not this slice.
  return {"R = " + Lin(linearPrecision, c.radius) + "'", "\xCE\x94 = " + FormatSweptAngle(c.delta / kDegToRad, angles),
          "L = " + Lin(linearPrecision, c.arcLength) + "'", "T = " + Lin(linearPrecision, c.tangentLength) + "'"};
}

std::string CurveLabelText(const CurveData& c, int linearPrecision, const AngleDisplaySettings& angles) {
  std::string text;
  for (const std::string& line : CurveLabelLines(c, linearPrecision, angles)) text += (text.empty() ? "" : " ") + line;
  return text;
}

}  // namespace alignment
