#include "alignment/AlignmentTables.hpp"

#include "util/NumFormat.hpp"

#include <string>

namespace alignment {
namespace {

constexpr double kDegToRad = kPi / 180.0;

std::string Lin(int precision, double v) { return FormatLinear(v, precision); }

std::string Brg(const AngleDisplaySettings& angles, double azimuthRad) {
  return FormatBearing(azimuthRad / kDegToRad, angles);
}

std::string Sta(const Alignment& a, double internalStation) {
  return FormatStation(a.DisplayedStation(internalStation, true));
}

std::string Dir(const Entity& e) { return e.right ? "Right" : "Left"; }

/// Names the entity kinds a table would need labelled, so the refusal can say what is missing (REQ-493.3).
std::string MissingLabelsMessage(const Alignment& a) {
  return "the segments of \"" + a.name + "\" are not labelled yet; label them first with ALIGNMENTLABEL " + a.name +
         ", SEGMENTS (missing labels: " + std::to_string(a.Entities().size()) + " segment label(s))";
}

}  // namespace

bool BuildAlignmentTableText(const Alignment& a, AlignmentTableKind kind, int linearPrecision,
                             const AngleDisplaySettings& angles, AlignmentTableText* out, std::string* whyNot) {
  auto fail = [&](const std::string& why) {
    if (whyNot != nullptr) *whyNot = why;
    return false;
  };
  if (out == nullptr) return false;
  *out = AlignmentTableText{};
  if (a.Empty()) return fail("\"" + a.name + "\" has no geometry to tabulate");
  const bool needsLabels = kind != AlignmentTableKind::StationEquation;
  if (needsLabels && !a.segmentLabels) return fail(MissingLabelsMessage(a));

  auto row = [&](std::initializer_list<std::string> cells) {
    out->cells.insert(out->cells.end(), cells.begin(), cells.end());
  };
  const int n = static_cast<int>(a.Entities().size());
  switch (kind) {
    case AlignmentTableKind::Line: {
      out->cols = 5;
      row({"Line", "Start station", "End station", "Bearing", "Length"});
      for (int i = 0; i < n; ++i) {
        const Entity& e = a.Entities()[static_cast<size_t>(i)];
        if (e.kind != EntityKind::Line) continue;
        row({std::to_string(i + 1), Sta(a, a.EntityStartStation(i)), Sta(a, a.EntityEndStation(i)),
             Brg(angles, e.startAzimuth), Lin(linearPrecision, e.length)});
      }
      return true;
    }
    case AlignmentTableKind::Curve: {
      out->cols = 9;
      row({"Curve", "Start station", "End station", "Radius", "Delta", "Length", "Tangent", "Chord", "Direction"});
      for (int i = 0; i < n; ++i) {
        CurveData cd;
        if (!a.CurveDataFor(i, &cd)) continue;
        row({std::to_string(i + 1), Sta(a, a.EntityStartStation(i)), Sta(a, a.EntityEndStation(i)),
             Lin(linearPrecision, cd.radius), FormatSweptAngle(cd.delta / kDegToRad, angles),
             Lin(linearPrecision, cd.arcLength), Lin(linearPrecision, cd.tangentLength),
             Lin(linearPrecision, cd.chord), Dir(a.Entities()[static_cast<size_t>(i)])});
      }
      return true;
    }
    case AlignmentTableKind::Segment: {
      out->cols = 9;
      row({"Entity", "Type", "Start station", "End station", "Length", "Bearing", "Radius", "Delta", "Direction"});
      for (int i = 0; i < n; ++i) {
        const Entity& e = a.Entities()[static_cast<size_t>(i)];
        const std::string num = std::to_string(i + 1);
        const std::string start = Sta(a, a.EntityStartStation(i)), end = Sta(a, a.EntityEndStation(i));
        if (e.kind == EntityKind::Line) {
          row({num, "Tangent", start, end, Lin(linearPrecision, e.length), Brg(angles, e.startAzimuth), "", "", ""});
        } else {
          CurveData cd;
          (void)a.CurveDataFor(i, &cd);
          row({num, "Curve", start, end, Lin(linearPrecision, cd.arcLength), "", Lin(linearPrecision, cd.radius),
               FormatSweptAngle(cd.delta / kDegToRad, angles), Dir(e)});
        }
      }
      return true;
    }
    case AlignmentTableKind::StationEquation: {
      out->cols = 4;
      row({"Back station", "Ahead station", "Type", "Raw station"});
      for (const StationEquation& q : a.equations) {
        row({FormatStation(a.DisplayedStation(q.rawBack, false)), FormatStation(q.ahead),
             q.increasing ? "Increasing" : "Decreasing", FormatStation(q.rawBack)});
      }
      return true;
    }
  }
  return fail("unknown table kind");
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
