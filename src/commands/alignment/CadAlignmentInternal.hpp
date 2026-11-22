#include "CadCommands.hpp"
#include "CadCommandsInternal.hpp"
#include <GoSurvey/Core/NumFormat.hpp>
#include <GoSurvey/Core/StringUtil.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

constexpr double kDegToRad = alignment::kPi / 180.0;

std::string Lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

std::string Trim(const std::string& s) {
  size_t b = 0, e = s.size();
  while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
  while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
  return s.substr(b, e - b);
}

std::vector<std::string> SplitFields(const std::string& s) {
  std::vector<std::string> out;
  size_t start = 0;
  while (true) {
    const size_t comma = s.find(',', start);
    out.push_back(Trim(s.substr(start, comma == std::string::npos ? std::string::npos : comma - start)));
    if (comma == std::string::npos) break;
    start = comma + 1;
  }
  return out;
}

bool ParseNum(const std::string& s, double* v) {
  if (s.empty()) return false;
  char* end = nullptr;
  const double d = std::strtod(s.c_str(), &end);
  if (end == s.c_str() || *end != '\0' || !std::isfinite(d)) return false;
  *v = d;
  return true;
}

int FindAlignment(const AppCommandState& st, const std::string& name) {
  const std::string want = Lower(name);
  for (size_t i = 0; i < st.drawing.alignments.size(); ++i) {
    if (Lower(st.drawing.alignments[i].name) == want) return static_cast<int>(i);
  }
  return -1;
}

const char* TypeName(alignment::AlignmentType t) {
  switch (t) {
    case alignment::AlignmentType::Centerline: return "Centerline";
    case alignment::AlignmentType::Offset: return "Offset";
    case alignment::AlignmentType::CurbReturn: return "Curb Return";
    case alignment::AlignmentType::Miscellaneous: return "Miscellaneous";
    case alignment::AlignmentType::Rail: return "Rail";
  }
  return "?";
}

std::string StaFmt(const AppCommandState& st, double displayed) {
  return alignment::FormatStation(displayed, st.drawing.alignmentStationFormat);  // REQ-483.4: the drawing's station format
}

bool ParseSta(const AppCommandState& st, const std::string& text, double* displayed) {
  return alignment::ParseStation(text, displayed, st.drawing.alignmentStationFormat);
}

std::string Sta(const AppCommandState& st, const alignment::Alignment& a, double internalStation) {
  return StaFmt(st, a.DisplayedStation(internalStation, true));
}

std::string Lin(const AppCommandState& st, double v) { return FormatLinear(v, st.displayLinearPrecision); }

std::string Brg(const AppCommandState& st, double azimuthRad) {
  return FormatBearing(azimuthRad / kDegToRad, CadAngleDisplaySettings(st));
}

/// Resolves the alignment named in \p field, or reports and returns nullptr.
alignment::Alignment* Resolve(AppCommandState& st, const std::string& cmd, const std::string& field,
                              std::vector<std::string>& log) {
  if (field.empty()) {
    log.push_back(cmd + " - an alignment name is required.");
    return nullptr;
  }
  const int ix = FindAlignment(st, field);
  if (ix < 0) {
    log.push_back(cmd + " - no alignment named \"" + field + "\".");
    return nullptr;
  }
  return &st.drawing.alignments[static_cast<size_t>(ix)];
}

/// Continuity warnings at the alignment's requirement level, and reverse-curve notes (REQ-482).
void ReportContinuity(const AppCommandState& st, const alignment::Alignment& r, std::vector<std::string>& log);


void ReportContinuity(const AppCommandState& st, const alignment::Alignment& r, std::vector<std::string>& log) {
  assert(r.Entities().size() < std::numeric_limits<int>::max());
  for (const alignment::ContinuityIssue& c : r.CheckContinuity()) {
    if (c.curvatureSignChange && c.c0 && c.c1)
      log.push_back("  NOTE: reverse curve after entity " + std::to_string(c.afterEntity + 1) +
                    ": the curvature changes sign (informational).");
    if (!c.FailsRequirement(r.continuityRequirement)) continue;
    if (!c.c0)
      log.push_back("  WARNING: gap of " + Lin(st, c.gap) + " after entity " + std::to_string(c.afterEntity + 1) + " (C0).");
    else if (!c.c1)
      log.push_back("  WARNING: direction changes after entity " + std::to_string(c.afterEntity + 1) + " (C1).");
    else if (!c.c2)
      log.push_back("  WARNING: curvature mismatch of " + Lin(st, c.curvatureGap) + " after entity " +
                    std::to_string(c.afterEntity + 1) + " (C2).");
  }
}

}  // namespace
