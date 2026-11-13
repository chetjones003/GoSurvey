#include "AlignmentReport.hpp"

#include "Alignment.hpp"
#include "AlignmentDesign.hpp"  // REQ-502

#include <GoSurvey/Core/NumFormat.hpp>

#include <array>
#include <cassert>
#include <cstdio>
#include <string>

namespace alignment {
namespace {

constexpr double kDegToRad = kPi / 180.0;

std::string FormatFixed4(double v) {
  std::array<char, 32> text{};
  std::snprintf(text.data(), text.size(), "%.4f", v);
  return text.data();
}

const char* AlignmentTypeLabel(AlignmentType type) {
  switch (type) {
    case AlignmentType::Centerline:
      return "Centerline";
    case AlignmentType::Offset:
      return "Offset";
    case AlignmentType::CurbReturn:
      return "Curb Return";
    case AlignmentType::Miscellaneous:
      return "Miscellaneous";
    case AlignmentType::Rail:
      return "Rail";
  }
  return "?";
}

std::string Lin(int precision, double v) { return FormatLinear(v, precision) + "'"; }

std::string Brg(const AlignmentReportFormat& fmt, double azimuthRad) {
  return FormatBearing(azimuthRad / kDegToRad, fmt.angles);
}

std::string Sta(const Alignment& a, double internalStation, const StationFormat& fmt) {
  return FormatStation(a.DisplayedStation(internalStation, true), fmt);
}

std::string KindLabel(EntityKind kind) {
  switch (kind) {
    case EntityKind::Line:
      return "Tangent";
    case EntityKind::Arc:
      return "Curve";
    case EntityKind::Spiral:
      return "Spiral";
  }
  return "?";
}

void AppendContinuityFindings(const Alignment& a, const AlignmentReportFormat& fmt, std::vector<std::string>* lines) {
  assert(lines != nullptr);
  bool any = false;
  for (const ContinuityIssue& c : a.CheckContinuity()) {
    if (!c.FailsRequirement(a.continuityRequirement)) continue;
    any = true;
    if (!c.c0)
      lines->push_back("  WARNING: gap of " + FormatLinear(c.gap, fmt.linearPrecision) +
                       "' after entity " + std::to_string(c.afterEntity + 1) + " (C0).");
    else if (!c.c1)
      lines->push_back("  WARNING: direction changes after entity " + std::to_string(c.afterEntity + 1) + " (C1).");
    else if (!c.c2)
      lines->push_back("  WARNING: curvature mismatch of " + FormatLinear(c.curvatureGap, fmt.linearPrecision) +
                       "' after entity " + std::to_string(c.afterEntity + 1) + " (C2).");
  }
  if (!any) lines->push_back("  (none at " + std::string(ContinuityRequirementLabel(a.continuityRequirement)) + " level)");
}

/// REQ-494: "meets" or "violates: <findings>" for one entity against the check set; empty without criteria.
std::string CriteriaMark(const AlignmentReportFormat& fmt, const std::vector<DesignFinding>& findings, int entityIndex) {
  assert(entityIndex >= 0);
  assert(findings.size() < findings.max_size());
  if (fmt.checkSet == nullptr) return {};
  std::string broken;
  for (const DesignFinding& f : findings)
    if (f.entity == entityIndex + 1) broken += (broken.empty() ? "" : "; ") + f.text;
  return broken.empty() ? std::string("meets") : "violates: " + broken;
}

void AppendEntityTextBlock(const Alignment& a, int entityIndex, const AlignmentReportFormat& fmt,
                           const std::vector<DesignFinding>& findings, std::vector<std::string>* lines) {
  assert(lines != nullptr);
  assert(entityIndex >= 0);
  assert(static_cast<size_t>(entityIndex) < a.Entities().size());
  const Entity& e = a.Entities()[static_cast<size_t>(entityIndex)];
  lines->push_back("");
  lines->push_back("ENTITY " + std::to_string(entityIndex + 1));
  lines->push_back(KindLabel(e.kind));
  if (const std::string mark = CriteriaMark(fmt, findings, entityIndex); !mark.empty())
    lines->push_back("Design criteria: " + mark);
  if (e.kind == EntityKind::Line) {
    lines->push_back("Bearing: " + Brg(fmt, e.startAzimuth));
    lines->push_back("Length: " + Lin(fmt.linearPrecision, e.length));
    return;
  }
  if (e.kind == EntityKind::Arc) {
    CurveData cd;
    (void)a.CurveDataFor(entityIndex, &cd);
    lines->push_back("Radius: " + Lin(fmt.linearPrecision, cd.radius));
    lines->push_back("Delta: " + FormatSweptAngle(cd.delta / kDegToRad, fmt.angles));
    lines->push_back("Length: " + Lin(fmt.linearPrecision, cd.arcLength));
    lines->push_back("Tangent length: " + Lin(fmt.linearPrecision, cd.tangentLength));
    lines->push_back("Chord: " + Lin(fmt.linearPrecision, cd.chord));
    lines->push_back("Direction: " + std::string(e.right ? "Right" : "Left"));
    return;
  }
  SpiralData sd;
  (void)SpiralDataFor(entityIndex, a, &sd);
  lines->push_back("Spiral length: " + Lin(fmt.linearPrecision, sd.length));
  if (sd.startRadius > 0.0) lines->push_back("Start radius: " + Lin(fmt.linearPrecision, sd.startRadius));
  if (sd.endRadius > 0.0) lines->push_back("End radius: " + Lin(fmt.linearPrecision, sd.endRadius));
  lines->push_back("Deflection: " + FormatSweptAngle(sd.deflection / kDegToRad, fmt.angles));
  if (sd.parameterA > 0.0) lines->push_back("A: " + Lin(fmt.linearPrecision, sd.parameterA));
  lines->push_back("Direction: " + std::string(e.right ? "Right" : "Left"));
}

SheetGrid::Row CsvHeaderRow() {
  return {std::string("alignment"), std::string("type"),       std::string("entity"), std::string("kind"),
          std::string("start_station"), std::string("end_station"), std::string("bearing"), std::string("length"),
          std::string("radius"), std::string("delta"), std::string("arc_length"), std::string("tangent_length"),
          std::string("chord"), std::string("spiral_a"), std::string("spiral_ls"), std::string("start_radius"),
          std::string("end_radius"), std::string("direction"), std::string("criteria")};
}

void PushCsvNumber(SheetGrid::Row* row, double v) {
  assert(row != nullptr);
  row->push_back(v);
}

/// Decimals of an angle in degrees at the display precision (DMS / surveyor show seconds: four more places).
void PushCsvText(SheetGrid::Row* row, std::string v) {
  assert(row != nullptr);
  row->push_back(std::move(v));
}

void PushCsvEmpty(SheetGrid::Row* row) {
  assert(row != nullptr);
  row->emplace_back(std::monostate{});
}

}  // namespace

void BuildAlignmentReportLines(const Alignment& a, const AlignmentReportFormat& format, std::vector<std::string>* lines) {
  assert(lines != nullptr);
  lines->clear();
  lines->push_back("ALIGNMENT: " + a.name);
  lines->push_back("");
  lines->push_back("Type: " + std::string(AlignmentTypeLabel(a.type)));
  if (!a.description.empty()) lines->push_back("Description: " + a.description);
  lines->push_back("Start Station: " + Sta(a, a.StartStation(), format.station));
  lines->push_back("End Station:   " + FormatStation(a.EndStationWithEquations(), format.station));
  lines->push_back("Length:        " + Lin(format.linearPrecision, a.Length()));
  if (!a.Empty()) {
    lines->push_back("Start bearing: " + Brg(format, a.StartAzimuth()));
    lines->push_back("End bearing:   " + Brg(format, a.EndAzimuth()));
    lines->push_back("Entity count:  " + std::to_string(a.Entities().size()));
    lines->push_back("Curve count:   " + std::to_string(a.CurveCount()));
    lines->push_back("Spiral count:  " + std::to_string(a.SpiralCount()));
  }
  lines->push_back("");
  lines->push_back("STATION EQUATIONS:");
  if (a.equations.empty()) {
    lines->push_back("  (none)");
  } else {
    for (const StationEquation& eq : a.equations) {
      lines->push_back("  At raw " + FormatLinear(eq.rawBack, format.linearPrecision) + " -> ahead " +
                       FormatStation(eq.ahead, format.station) + (eq.increasing ? " (increasing)" : " (decreasing)"));
    }
  }
  lines->push_back("");
  lines->push_back("CONTINUITY CHECK: " + std::string(ContinuityRequirementLabel(a.continuityRequirement)));
  lines->push_back("FINDINGS:");
  if (a.Empty()) {
    lines->push_back("  (no geometry)");
  } else {
    AppendContinuityFindings(a, format, lines);
  }
  lines->push_back("");
  if (format.checkSet == nullptr) {
    lines->push_back("Design criteria: none");
  } else {
    lines->push_back("Design criteria: " + format.checkSet->name +
                     (format.checkSet->kind == CheckSetKind::Rail ? " (rail)" : " (road)"));
    const std::vector<DesignFinding> findings = CheckDesign(a, *format.checkSet, format.linearPrecision);
    if (findings.empty()) lines->push_back("  No design check findings.");
    for (const DesignFinding& f : findings)
      lines->push_back("  Entity " + std::to_string(f.entity) + ": " + f.text);
  }
  // REQ-522: the limits and assumptions behind the superelevation and sight checks, or why each is off.
  if (format.checkSet == nullptr || format.checkSet->maxSuperelevation <= 0.0) {
    lines->push_back("Superelevation: not checked (no maximum rate).");
  } else {
    lines->push_back("Superelevation: maximum " + FormatFixed4(format.checkSet->maxSuperelevation) + ", side friction " +
                     FormatFixed4(format.checkSet->sideFriction) +
                     ", circular curves only; a curve with no design speed is not checked.");
  }
  if (format.checkSet == nullptr || format.checkSet->minSightDistance <= 0.0 || format.checkSet->sightClearance <= 0.0) {
    lines->push_back("Sight distance: not checked (no minimum sight distance or no clearance).");
  } else {
    lines->push_back("Sight distance: minimum " + FormatLinear(format.checkSet->minSightDistance, format.linearPrecision) +
                     "', clearance " + FormatLinear(format.checkSet->sightClearance, format.linearPrecision) +
                     "', circular curves only, sight beyond the curve not counted.");
  }
  if (a.Empty()) {
    lines->push_back("");
    lines->push_back("(no entities)");
    return;
  }
  const std::vector<DesignFinding> findings =
      format.checkSet == nullptr ? std::vector<DesignFinding>{} : CheckDesign(a, *format.checkSet, format.linearPrecision);
  for (size_t i = 0; i < a.Entities().size(); ++i) AppendEntityTextBlock(a, static_cast<int>(i), format, findings, lines);
}

SheetGrid::Grid BuildAlignmentReportCsv(const Alignment& a, const AlignmentReportFormat& format) {
  SheetGrid::Grid grid;
  grid.push_back(CsvHeaderRow());
  if (a.Empty()) return grid;
  const std::vector<DesignFinding> findings =
      format.checkSet == nullptr ? std::vector<DesignFinding>{} : CheckDesign(a, *format.checkSet, format.linearPrecision);
  for (size_t i = 0; i < a.Entities().size(); ++i) {
    const int ii = static_cast<int>(i);
    const Entity& e = a.Entities()[i];
    SheetGrid::Row row;
    PushCsvText(&row, a.name);
    PushCsvText(&row, AlignmentTypeLabel(a.type));
    PushCsvNumber(&row, static_cast<double>(i + 1));
    PushCsvText(&row, KindLabel(e.kind));
    PushCsvText(&row, Sta(a, a.EntityStartStation(ii), format.station));
    PushCsvText(&row, Sta(a, a.EntityEndStation(ii), format.station));
    PushCsvText(&row, Brg(format, e.startAzimuth));
    if (e.kind == EntityKind::Line) {
      PushCsvNumber(&row, e.length);
      for (int n = 0; n < 9; ++n) PushCsvEmpty(&row);  // radius .. end_radius (9 columns)
      PushCsvText(&row, e.right ? "Right" : "Left");
    } else if (e.kind == EntityKind::Arc) {
      CurveData cd;
      (void)a.CurveDataFor(ii, &cd);
      PushCsvEmpty(&row);
      PushCsvNumber(&row, cd.radius);
      PushCsvNumber(&row, cd.delta / kDegToRad);
      PushCsvNumber(&row, cd.arcLength);
      PushCsvNumber(&row, cd.tangentLength);
      PushCsvNumber(&row, cd.chord);
      for (int n = 0; n < 4; ++n) PushCsvEmpty(&row);  // spiral_a .. end_radius (4 columns)
      PushCsvText(&row, e.right ? "Right" : "Left");
    } else {
      SpiralData sd;
      (void)SpiralDataFor(ii, a, &sd);
      PushCsvEmpty(&row);
      PushCsvNumber(&row, sd.endRadius);
      PushCsvNumber(&row, sd.deflection / kDegToRad);
      PushCsvEmpty(&row);
      PushCsvEmpty(&row);
      PushCsvEmpty(&row);
      PushCsvNumber(&row, sd.parameterA);
      PushCsvNumber(&row, sd.length);
      PushCsvNumber(&row, sd.startRadius);
      PushCsvNumber(&row, sd.endRadius);
      PushCsvText(&row, e.right ? "Right" : "Left");
    }
    PushCsvText(&row, CriteriaMark(format, findings, ii));
    grid.push_back(std::move(row));
  }
  return grid;
}

}  // namespace alignment
