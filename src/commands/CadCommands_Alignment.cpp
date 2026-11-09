// CadCommands_Alignment.cpp - the alignment commands (REQ-480, 484, 485, 501; issue #121 Phase 1).
//
// Same TU conventions as CadCommands_Geo.cpp: entry points are declared in CadCommands.hpp and the
// geometry itself lives in alignment/Alignment.cpp (pure, no session state). This file only parses
// typed arguments, converts WORLD input to the drawing-local doubles alignments are stored in
// (world = local + worldDocumentOrigin, always computed in double - REQ-499), and reports.
//
// Geometry is built from typed arguments, from selected objects, and interactively by layout (ALIGNMENTLAYOUT) and
// by picking objects (REQ-485). The dialogs and the layout toolbar live in the Qt editor and submit these commands.

#include "GsIo.hpp"  // REQ-502 criteria files
#include "CadCommands.hpp"
#include "CadCommandsInternal.hpp"
#include "CadCoordinateFrame.hpp"
#include "AlignmentStyle.hpp"
#include "AlignmentLabelSet.hpp"
#include "CadColor.hpp"
#include "TextStyle.hpp"
#include "alignment/AlignmentReport.hpp"
#include "alignment/AlignmentGround.hpp"
#include "alignment/AlignmentFeatureLine.hpp"
#include <memory>
#include "alignment/AlignmentTables.hpp"
#include "alignment/AlignmentLabels.hpp"
#include "alignment/AlignmentDesign.hpp"  // REQ-502
#include "alignment/AlignmentViewFrames.hpp"  // REQ-517
#include "PaperSpace.hpp"  // REQ-517: paper sizes for sheets
#include "ViewportZoomScale.hpp"  // REQ-517: named scales
#include "util/cadtable.hpp"
#include "util/cadtablestyle.hpp"
#include "SpreadsheetIo.hpp"
#include "WinFileDialogs.hpp"
#include "StringUtil.hpp"
#include "util/NumFormat.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <functional>
#include <filesystem>
#include "io/DwgIo.hpp"  // REQ-518.8: the title block DWG
#include "render/ViewportProjection.hpp"  // REQ-518.9: the north arrow follows the view
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <iomanip>
#include <sstream>
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
  for (size_t i = 0; i < st.alignments.size(); ++i) {
    if (Lower(st.alignments[i].name) == want) return static_cast<int>(i);
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
  return alignment::FormatStation(displayed, st.alignmentStationFormat);  // REQ-483.4: the drawing's station format
}

bool ParseSta(const AppCommandState& st, const std::string& text, double* displayed) {
  return alignment::ParseStation(text, displayed, st.alignmentStationFormat);
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
  return &st.alignments[static_cast<size_t>(ix)];
}

/// Continuity warnings at the alignment's requirement level, and reverse-curve notes (REQ-482).
void ReportContinuity(const AppCommandState& st, const alignment::Alignment& r, std::vector<std::string>& log);

}  // namespace

// ALIGNMENTADD <name>, TANGENT, <x>, <y>, <azimuth deg>, <length>     first tangent, WORLD x/y
//              <name>, TANGENT, <length>                              continues tangent-continuous
//              <name>, TANGENTTO, <x>, <y>                            tangent to a WORLD point
//              <name>, CURVE, <radius>, <delta deg>, LEFT|RIGHT       circular curve by central angle
//              <name>, CURVELEN, <radius>, <arc length>, LEFT|RIGHT   circular curve by arc length
//              <name>, SPIRAL, <length>, <end radius>, LEFT|RIGHT     clothoid from tangent (start R = infinite)
//              <name>, SPIRAL, <length>, <start radius>, <end radius>, LEFT|RIGHT
void RunAlignmentAdd(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitFields(args);
  const char* usage =
      "ALIGNMENTADD - usage: ALIGNMENTADD <name>, TANGENT, <x>, <y>, <azimuth deg>, <length> | <name>, TANGENT, "
      "<length> | <name>, TANGENTTO, <x>, <y> | <name>, CURVE, <radius>, <delta deg>, LEFT|RIGHT | <name>, CURVELEN, "
      "<radius>, <arc length>, LEFT|RIGHT | <name>, SPIRAL, <length>, <end radius>, LEFT|RIGHT | <name>, SPIRAL, "
      "<length>, <start radius>, <end radius>, LEFT|RIGHT (0 radius = tangent / infinite).";
  if (f.size() < 3) {
    log.push_back(usage);
    return;
  }
  alignment::Alignment* a = Resolve(st, "ALIGNMENTADD", f[0], log);
  if (a == nullptr) return;
  const std::string kind = Lower(f[1]);
  alignment::Alignment edited = *a;
  bool ok = false;
  std::string what;
  double v[4] = {};
  auto nums = [&](size_t from, size_t count) {
    if (f.size() < from + count) return false;
    for (size_t i = 0; i < count; ++i) {
      if (!ParseNum(f[from + i], &v[i])) return false;
    }
    return true;
  };
  if (kind == "tangent" && f.size() == 6) {
    if (!edited.Empty()) {
      log.push_back("ALIGNMENTADD - \"" + a->name + "\" already has geometry; use TANGENT, <length> to continue it.");
      return;
    }
    if (!nums(2, 4)) {
      log.push_back(usage);
      return;
    }
    double lx = 0.0, ly = 0.0;
    CadCoord::LocalFromWorld(st, v[0], v[1], &lx, &ly);
    ok = edited.AppendTangent({lx, ly}, v[2] * kDegToRad, v[3]);
    what = "tangent";
  } else if (kind == "tangent" && f.size() == 3) {
    if (!nums(2, 1)) {
      log.push_back(usage);
      return;
    }
    if (edited.Empty()) {
      log.push_back("ALIGNMENTADD - the first tangent needs a start point and azimuth: <name>, TANGENT, <x>, <y>, <azimuth deg>, <length>.");
      return;
    }
    ok = edited.AppendTangent(v[0]);
    what = "tangent";
  } else if (kind == "tangentto" && f.size() == 4) {
    if (!nums(2, 2)) {
      log.push_back(usage);
      return;
    }
    if (edited.Empty()) {
      log.push_back("ALIGNMENTADD - the first tangent needs a start point and azimuth, not TANGENTTO.");
      return;
    }
    double lx = 0.0, ly = 0.0;
    CadCoord::LocalFromWorld(st, v[0], v[1], &lx, &ly);
    ok = edited.AppendTangentTo({lx, ly});
    what = "tangent";
  } else if ((kind == "curve" || kind == "curvelen") && f.size() == 5) {
    if (!nums(2, 2)) {
      log.push_back(usage);
      return;
    }
    const std::string side = Lower(f[4]);
    if (side != "left" && side != "right") {
      log.push_back("ALIGNMENTADD - the curve direction must be LEFT or RIGHT.");
      return;
    }
    if (edited.Empty()) {
      log.push_back("ALIGNMENTADD - a curve needs a tangent before it; add the first tangent first.");
      return;
    }
    ok = kind == "curve" ? edited.AppendCurveByDelta(v[0], v[1] * kDegToRad, side == "right")
                         : edited.AppendCurve(v[0], v[1], side == "right");
    what = "curve";
  } else if (kind == "spiral" && (f.size() == 5 || f.size() == 6)) {
    if (edited.Empty()) {
      log.push_back("ALIGNMENTADD - a spiral needs geometry before it; add a tangent first.");
      return;
    }
    const std::string side = Lower(f.back());
    if (side != "left" && side != "right") {
      log.push_back("ALIGNMENTADD - the spiral direction must be LEFT or RIGHT.");
      return;
    }
    double ls = 0.0, rs = 0.0, re = 0.0;
    if (f.size() == 5) {
      if (!ParseNum(f[2], &ls) || !ParseNum(f[3], &re)) {
        log.push_back(usage);
        return;
      }
      rs = 0.0;
    } else {
      if (!ParseNum(f[2], &ls) || !ParseNum(f[3], &rs) || !ParseNum(f[4], &re)) {
        log.push_back(usage);
        return;
      }
    }
    ok = edited.AppendSpiral(ls, rs, re, side == "right");
    what = "spiral";
  } else {
    log.push_back(usage);
    return;
  }
  if (!ok) {
    log.push_back("ALIGNMENTADD - the " + what +
                  " was refused: lengths and radii must be positive, a curve must sweep less than 360 degrees, "
                  "and a tangent must have a non-zero length.");
    return;
  }
  PushUndoSnapshot(st, "Add alignment geometry");
  // PushUndoSnapshot copied the pre-edit state; re-resolve in case it reallocated nothing but be exact.
  const int ix = FindAlignment(st, f[0]);
  st.alignments[static_cast<size_t>(ix)] = std::move(edited);
  const alignment::Alignment& r = st.alignments[static_cast<size_t>(ix)];
  log.push_back("ALIGNMENTADD - \"" + r.name + "\" now has " + std::to_string(r.Entities().size()) +
                " entities, length " + Lin(st, r.Length()) + ", ending at station " + Sta(st, r, r.EndStation()) + ".");
  RefreshAlignmentOffsets(st, log);  // REQ-488 linked offsets and REQ-493.2 dynamic tables follow the new entity
}

// ALIGNMENTLIST [<name>]  (REQ-501.1 properties, REQ-494 entity list)
void RunAlignmentList(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::string name = Trim(args);
  if (st.alignments.empty()) {
    log.push_back("ALIGNMENTLIST - this drawing has no alignments.");
    return;
  }
  for (const alignment::Alignment& a : st.alignments) {
    if (!name.empty() && Lower(a.name) != Lower(name)) continue;
    const std::string criteriaName = AlignmentStyles::EffectiveCheckSetName(st.alignmentStyles, a);
    log.push_back("ALIGNMENT: " + a.name + "  [" + TypeName(a.type) + ", layer " + a.layer + ", style " + a.style + "]");
    if (!a.description.empty()) log.push_back("  Description:   " + a.description);
    for (const auto& [key, value] : a.metadata) log.push_back("  " + key + ": " + value);  // REQ-480.1
    if (a.Empty()) {
      log.push_back("  Start station: " + StaFmt(st, a.startingStation) + "   (no geometry yet)");
      continue;
    }
    double wsx = 0.0, wsy = 0.0, wex = 0.0, wey = 0.0;
    wsx = a.StartPoint().x + CadCoord::WorldOriginX(st);
    wsy = a.StartPoint().y + CadCoord::WorldOriginY(st);
    wex = a.EndPoint().x + CadCoord::WorldOriginX(st);
    wey = a.EndPoint().y + CadCoord::WorldOriginY(st);
    log.push_back("  Start station: " + Sta(st, a, a.StartStation()) + "   End station: " +
                  StaFmt(st, a.EndStationWithEquations()) + "   Length: " + Lin(st, a.Length()));
    if (a.hasReference)  // REQ-483.1
      log.push_back("  Reference point: " + Lin(st, a.referenceDistance) + " along, station " +
                    StaFmt(st, a.startingStation + a.referenceDistance));
    log.push_back("  Start: " + Lin(st, wsx) + ", " + Lin(st, wsy) + "  bearing " + Brg(st, a.StartAzimuth()));
    log.push_back("  End:   " + Lin(st, wex) + ", " + Lin(st, wey) + "  bearing " + Brg(st, a.EndAzimuth()));
    log.push_back("  Entities: " + std::to_string(a.Entities().size()) + "   Curves: " + std::to_string(a.CurveCount()) +
                  "   Spirals: " + std::to_string(a.SpiralCount()) + "   Station equations: " +
                  std::to_string(a.equations.size()) + "   Continuity check: " +
                  alignment::ContinuityRequirementLabel(a.continuityRequirement));
    for (size_t i = 0; i < a.Entities().size(); ++i) {
      const alignment::Entity& e = a.Entities()[i];
      const int ii = static_cast<int>(i);
      std::string row = "  ENTITY " + std::to_string(i + 1) + "  " + Sta(st, a, a.EntityStartStation(ii)) + " - " +
                        Sta(st, a, a.EntityEndStation(ii)) + "  ";
      if (e.kind == alignment::EntityKind::Line) {
        row += "Tangent  bearing " + Brg(st, e.startAzimuth) + "  length " + Lin(st, e.length);
      } else if (e.kind == alignment::EntityKind::Arc) {
        alignment::CurveData cd;
        (void)a.CurveDataFor(ii, &cd);
        row += std::string("Curve ") + (e.right ? "right" : "left") + "  R " + Lin(st, cd.radius) + "  delta " +
               FormatSweptAngle(cd.delta / kDegToRad, CadAngleDisplaySettings(st)) + "  L " + Lin(st, cd.arcLength) +
               "  T " + Lin(st, cd.tangentLength) + "  C " + Lin(st, cd.chord);
      } else {
        alignment::SpiralData sd;
        (void)alignment::SpiralDataFor(ii, a, &sd);
        row += std::string("Spiral ") + (e.right ? "right" : "left") + "  Ls " + Lin(st, sd.length) + "  R " +
               Lin(st, sd.endRadius) + "  theta " + FormatSweptAngle(sd.deflection / kDegToRad, CadAngleDisplaySettings(st));
        if (sd.parameterA > 0.0) row += "  A " + Lin(st, sd.parameterA);
        if (sd.tangentEnd)  // REQ-481.5a
          row += "  X " + Lin(st, sd.totalX) + "  Y " + Lin(st, sd.totalY) + "  P " + Lin(st, sd.p) + "  K " +
                 Lin(st, sd.k) + "  LT " + Lin(st, sd.longTangent) + "  ST " + Lin(st, sd.shortTangent);
      }
      log.push_back(row);
    }
    if (!criteriaName.empty()) {  // REQ-502: design warnings in the list, as the spec asks
      if (const alignment::AlignmentCheckSet* checkSet = alignment::FindCheckSet(st.alignmentCheckSets, criteriaName)) {
        for (const alignment::DesignFinding& fd : alignment::CheckDesign(a, *checkSet, st.displayLinearPrecision))
          log.push_back("  WARNING: design check, entity " + std::to_string(fd.entity) + ": " + fd.text);
      }
    }
    ReportContinuity(st, a, log);
  }
}

// ALIGNMENTDELETE <name>
void RunAlignmentDelete(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::string name = Trim(args);
  const int ix = FindAlignment(st, name);
  if (ix < 0) {
    log.push_back("ALIGNMENTDELETE - no alignment named \"" + name + "\".");
    return;
  }
  PushUndoSnapshot(st, "Delete alignment");
  st.alignments.erase(st.alignments.begin() + ix);
  RefreshViewFrameGroups(st, log);  // REQ-517.9: the alignment's view frame groups go with it, in the same step
  BumpCadGpuCache(st);  // the plan drops the alignment's drawing at once, not at the next redraw
  log.push_back("ALIGNMENTDELETE - removed \"" + name + "\".");
}

// Keeps view frame groups in step with the alignments (REQ-517.8, REQ-517.9). Called from RefreshAlignmentOffsetsOnly,
// which every geometry edit and the alignment erase path already call, so no caller has to remember it.
void RefreshViewFrameGroups(AppCommandState& st, std::vector<std::string>& log) {
  std::vector<std::string> notes;
  alignment::SyncViewFrameGroups(st.alignments, st.viewFrameGroups, &notes);
  for (const std::string& n : notes) log.push_back(n);
  alignment::SyncSampleLines(st.alignments, st.sampleLines);  // REQ-519.13: a deleted alignment takes its lines
}

namespace {

std::string NormalizeName(const std::string& text) {
  std::string out;
  for (unsigned char ch : text)
    if (!std::isspace(ch)) out.push_back(static_cast<char>(std::toupper(ch)));
  return out;
}

const PaperSizePreset* FindSheetSize(const std::string& text) {
  const std::string want = NormalizeName(text);
  if (want.empty()) return nullptr;
  for (int i = 0; i < kPaperSizePresetCount; ++i) {
    const std::string have = NormalizeName(kPaperSizePresets[i].name);
    if (have == want || have.compare(0, want.size(), want) == 0) return &kPaperSizePresets[i];
  }
  return nullptr;
}

}  // namespace

// VIEWFRAMEDELETE <alignment> (REQ-517.7): deletes every view frame group that follows the alignment, as one undo step.
void RunViewFrameDelete(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::string name = Trim(args);
  if (name.empty()) {
    log.push_back("VIEWFRAMEDELETE - usage: VIEWFRAMEDELETE <alignment>");
    return;
  }
  if (ActiveProjectPointsReadOnly(st)) {  // REQ-517.6: refused before any snapshot or change
    log.push_back("VIEWFRAMEDELETE - the project is read-only; no view frames were deleted.");
    return;
  }
  const int ix = FindAlignment(st, name);
  if (ix < 0) {
    log.push_back("VIEWFRAMEDELETE - no alignment named \"" + name + "\".");
    return;
  }
  const std::uint64_t id = st.alignments[static_cast<size_t>(ix)].id;
  size_t count = 0;
  for (const alignment::ViewFrameGroup& g : st.viewFrameGroups) count += g.alignmentId == id ? 1 : 0;
  if (count == 0) {
    log.push_back("VIEWFRAMEDELETE - \"" + name + "\" has no view frame groups.");
    return;
  }
  PushUndoSnapshot(st, "Delete view frames");
  st.viewFrameGroups.erase(std::remove_if(st.viewFrameGroups.begin(), st.viewFrameGroups.end(),
                                          [id](const alignment::ViewFrameGroup& g) { return g.alignmentId == id; }),
                           st.viewFrameGroups.end());
  BumpCadGpuCache(st);
  log.push_back("VIEWFRAMEDELETE - removed " + std::to_string(count) + " view frame group(s) from \"" + name + "\".");
}

namespace {

// REQ-517.8 (AUTO scale): the title block and margins take this much of an ARCH D sheet across and down, so the
// usable area is 29.75 x 19.5 in, the same area CREATESHEETS gives the viewport.
constexpr double kSheetAllowanceWidthIn = 6.25;
constexpr double kSheetAllowanceHeightIn = 4.5;

// REQ-517.8 (D-2026-11-08-h, amended): AUTO picks the most detailed named scale on which the whole alignment fits one
// sheet, when that scale is 1" = 50' (1:50 in a metric drawing) or finer. A longer alignment gets the plan scale itself
// and as many sheets as it needs, so no sheet is drawn at a coarse scale just to hold a long run.
// Returns false (with a reason) when the scale list has no plan scale to fall back to.
bool ChooseBestFitScale(const alignment::Alignment& a, alignment::ViewFrameSettings& s,
                        const std::vector<CadAnnotationScale>& scales, std::string& why) {
  std::vector<const CadAnnotationScale*> byDetail;
  for (const CadAnnotationScale& candidate : scales)
    if (candidate.paperUnits > 0.f && candidate.drawingUnits > 0.f) byDetail.push_back(&candidate);
  auto groundPerInch = [](const CadAnnotationScale* c) { return static_cast<double>(c->drawingUnits) / c->paperUnits; };
  std::sort(byDetail.begin(), byDetail.end(), [&groundPerInch](const CadAnnotationScale* p, const CadAnnotationScale* q) {
    return groundPerInch(p) < groundPerInch(q);  // most detailed (smallest ground per inch) first
  });
  const CadAnnotationScale* plan = nullptr;
  for (const CadAnnotationScale* c : byDetail)
    if (NormalizeName(c->name) == NormalizeName("1\" = 50'") || NormalizeName(c->name) == "1:50") plan = c;
  if (plan == nullptr) {
    why = "this drawing's scale list has no 1\" = 50' (or 1:50) plan scale for AUTO to use";
    return false;
  }
  const CadAnnotationScale* chosen = plan;
  for (const CadAnnotationScale* candidate : byDetail) {
    if (groundPerInch(candidate) > groundPerInch(plan)) break;  // coarser than the plan scale: not wanted
    s.groundPerInch = groundPerInch(candidate);
    s.scaleName = candidate->name;
    if (!alignment::ViewFrameSettingsRefusal(a, s).empty()) continue;
    if (alignment::ComputeViewFrames(a, s).size() == 1) {
      chosen = candidate;
      break;
    }
  }
  s.groundPerInch = groundPerInch(chosen);
  s.scaleName = chosen->name;
  return true;
}

}  // namespace

// CREATEVIEWFRAMES <alignment>, <start station>, <end station>, <sheet>, <scale>, <overlap>[, CENTRED|LEFT|RIGHT]
// REQ-517: frames along the alignment, sized by the sheet (landscape, so the long side runs along the alignment) and
// the named scale. The group follows the alignment from then on. <scale> AUTO (REQ-517.8) picks the best-fit scale.
void RunCreateViewFrames(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const char* usage =
      "CREATEVIEWFRAMES - usage: CREATEVIEWFRAMES <alignment>, <start station>, <end station>, <sheet>, <scale or AUTO>, "
      "<overlap>[, CENTRED|LEFT|RIGHT]";
  const std::vector<std::string> f = SplitFields(args);
  if (f.size() < 6 || f.size() > 7) {
    log.push_back(usage);
    return;
  }
  if (ActiveProjectPointsReadOnly(st)) {  // REQ-517.6: a read-only project refuses before any change
    log.push_back("CREATEVIEWFRAMES - the project is read-only; no view frames were created.");
    return;
  }
  alignment::Alignment* a = Resolve(st, "CREATEVIEWFRAMES", f[0], log);
  if (a == nullptr) return;

  double startDisplayed = 0.0;
  double endDisplayed = 0.0;
  if (!ParseSta(st, f[1], &startDisplayed) || !ParseSta(st, f[2], &endDisplayed)) {
    log.push_back("CREATEVIEWFRAMES - a station is not understood: \"" + f[1] + "\" and \"" + f[2] + "\".");
    return;
  }
  const PaperSizePreset* sheet = FindSheetSize(f[3]);
  if (sheet == nullptr) {
    log.push_back("CREATEVIEWFRAMES - no sheet size \"" + f[3] + "\". Use a name from Page Setup, such as ARCH D.");
    return;
  }
  EnsureStandardAnnotationScales(st.viewportZoomScales, st.drawingInsUnits);
  const bool autoScale = NormalizeName(f[4]) == "AUTO";
  const CadAnnotationScale* scale = nullptr;
  for (const CadAnnotationScale& candidate : st.viewportZoomScales)
    if (!autoScale && NormalizeName(candidate.name) == NormalizeName(f[4])) scale = &candidate;
  if (!autoScale && scale == nullptr) {  // REQ-517.3: named scales only
    log.push_back("CREATEVIEWFRAMES - \"" + f[4] + "\" is not a named scale in this drawing's scale list.");
    return;
  }
  double overlap = 0.0;
  if (!ParseNum(f[5], &overlap)) {
    log.push_back("CREATEVIEWFRAMES - the overlap \"" + f[5] + "\" is not a number.");
    return;
  }
  alignment::ViewFramePlacement placement = alignment::ViewFramePlacement::Centred;
  if (f.size() == 7) {
    const std::string p = Lower(f[6]);
    if (p == "centred" || p == "centered" || p == "center" || p == "centre") placement = alignment::ViewFramePlacement::Centred;
    else if (p == "left") placement = alignment::ViewFramePlacement::Left;
    else if (p == "right") placement = alignment::ViewFramePlacement::Right;
    else {
      log.push_back("CREATEVIEWFRAMES - placement must be CENTRED, LEFT or RIGHT.");
      return;
    }
  }

  alignment::ViewFrameSettings s;
  // REQ-517.6a: typed stations are displayed stations; each must name exactly one internal station.
  std::string whyStart, whyEnd;
  if (!alignment::ResolveDisplayedStation(*a, startDisplayed, &s.startStation, &whyStart) ||
      !alignment::ResolveDisplayedStation(*a, endDisplayed, &s.endStation, &whyEnd)) {
    log.push_back("CREATEVIEWFRAMES - refused: " + (whyStart.empty() ? whyEnd : whyStart) + ". No view frames were created.");
    return;
  }
  // Landscape: the longer side of the sheet runs along the alignment (a plan sheet's usual orientation).
  s.sheetWidthIn = std::max(sheet->widthIn, sheet->heightIn);
  s.sheetHeightIn = std::min(sheet->widthIn, sheet->heightIn);
  s.groundPerInch = scale != nullptr && scale->paperUnits > 0.f ? static_cast<double>(scale->drawingUnits) / scale->paperUnits : 0.0;
  s.overlap = overlap;
  s.placement = placement;
  s.sheetName = sheet->name;
  s.scaleName = scale != nullptr ? scale->name : std::string();
  if (autoScale) {
    // REQ-517.8: frames are sized to the usable area inside the title block, so AUTO's one-sheet test uses that area.
    s.sheetWidthIn = std::max(s.sheetWidthIn - kSheetAllowanceWidthIn, 0.0);
    s.sheetHeightIn = std::max(s.sheetHeightIn - kSheetAllowanceHeightIn, 0.0);
    std::string whyScale;
    if (!ChooseBestFitScale(*a, s, st.viewportZoomScales, whyScale)) {
      log.push_back("CREATEVIEWFRAMES - refused: " + whyScale + ". No view frames were created.");
      return;
    }
  }

  const std::string why = alignment::ViewFrameSettingsRefusal(*a, s);
  if (!why.empty()) {
    log.push_back("CREATEVIEWFRAMES - refused: " + why + ". No view frames were created.");
    return;
  }

  PushUndoSnapshot(st, "Create view frames");
  alignment::ViewFrameGroup g;
  g.id = st.nextEntityId++;
  g.alignmentId = a->id;
  g.name = a->name;
  g.settings = s;
  g.frames = alignment::ComputeViewFrames(*a, s);
  const size_t count = g.frames.size();
  st.viewFrameGroups.push_back(std::move(g));
  BumpCadGpuCache(st);
  log.push_back("CREATEVIEWFRAMES - " + std::to_string(count) + " view frame(s) along \"" + a->name + "\" (" +
                s.sheetName + ", " + s.scaleName + (autoScale ? " chosen automatically" : "") + ", overlap " +
                Lin(st, overlap) + ").");
}

namespace {

// REQ-518.10: the plan viewport sits this far inside the title block's main border.
constexpr double kSheetMarginIn = 0.5;
// REQ-518.8: the title block every sheet is built on. Its paper geometry is copied onto each new sheet.
constexpr const char* kTitleBlockFile = "Generic 24in x 36in Title Block.dwg";

// The title block DWG on this machine: the AutoCAD template folder of the Civil 3D install.
std::string DefaultTitleBlockPath() {
  const char* local = std::getenv("LOCALAPPDATA");
  if (local == nullptr || *local == '\0') return {};
  return std::string(local) + "\\Autodesk\\C3D 2026\\enu\\Template\\AutoCAD Template\\" + kTitleBlockFile;
}

struct Box {
  double minX = 0.0, minY = 0.0, maxX = 0.0, maxY = 0.0;
  double width() const { return maxX - minX; }
  double height() const { return maxY - minY; }
};

// Bounding box of a title block's lines and polylines, in its own DWG coordinates.
bool TitleBlockBounds(const AppCommandState& tb, Box* out) {
  bool any = false;
  auto add = [&](double x, double y) {
    if (!any) {
      out->minX = out->maxX = x;
      out->minY = out->maxY = y;
      any = true;
      return;
    }
    out->minX = std::min(out->minX, x);
    out->maxX = std::max(out->maxX, x);
    out->minY = std::min(out->minY, y);
    out->maxY = std::max(out->maxY, y);
  };
  for (size_t i = 0; i + 5 < tb.userLinesFlat.size(); i += 6) {
    add(tb.userLinesFlat[i], tb.userLinesFlat[i + 1]);
    add(tb.userLinesFlat[i + 3], tb.userLinesFlat[i + 4]);
  }
  for (size_t v = 0; v + 2 < tb.userPolylineVerts.size(); v += 3) add(tb.userPolylineVerts[v], tb.userPolylineVerts[v + 1]);
  return any;
}

// The title block's main border: the four-vertex polyline with the largest area.
bool TitleBlockBorder(const AppCommandState& tb, Box* out) {
  bool found = false;
  double bestArea = -1.0;
  for (size_t p = 0; p + 1 < tb.userPolylineOffsets.size(); ++p) {
    const int v0 = tb.userPolylineOffsets[p], v1 = tb.userPolylineOffsets[p + 1];
    if (v1 - v0 != 4) continue;
    Box b;
    bool first = true;
    for (int v = v0; v < v1; ++v) {
      const double x = tb.userPolylineVerts[3 * static_cast<size_t>(v)];
      const double y = tb.userPolylineVerts[3 * static_cast<size_t>(v) + 1];
      if (first) {
        b.minX = b.maxX = x;
        b.minY = b.maxY = y;
        first = false;
        continue;
      }
      b.minX = std::min(b.minX, x);
      b.maxX = std::max(b.maxX, x);
      b.minY = std::min(b.minY, y);
      b.maxY = std::max(b.maxY, y);
    }
    const double area = b.width() * b.height();
    if (area > bestArea) {
      bestArea = area;
      *out = b;
      found = true;
    }
  }
  return found;
}

// REQ-518.8: copies the title block's lines and polylines onto `sheet`, moved by (dx, dy) paper inches.
void CopyTitleBlockOntoSheet(PaperLayout& sheet, const AppCommandState& tb, double dx, double dy) {
  for (size_t i = 0; i + 5 < tb.userLinesFlat.size(); i += 6) {
    sheet.paperLines.push_back(static_cast<float>(tb.userLinesFlat[i] + dx));
    sheet.paperLines.push_back(static_cast<float>(tb.userLinesFlat[i + 1] + dy));
    sheet.paperLines.push_back(0.f);
    sheet.paperLines.push_back(static_cast<float>(tb.userLinesFlat[i + 3] + dx));
    sheet.paperLines.push_back(static_cast<float>(tb.userLinesFlat[i + 4] + dy));
    sheet.paperLines.push_back(0.f);
    EntityAttributes attr = i / 6 < tb.userLineAttrs.size() ? tb.userLineAttrs[i / 6] : EntityAttributes{};
    attr.id = 0;  // the sheet's own copy takes a fresh identity when it is saved
    sheet.paperLineAttrs.push_back(attr);
  }
  if (sheet.paperPolyOffsets.empty()) sheet.paperPolyOffsets.push_back(0);
  for (size_t p = 0; p + 1 < tb.userPolylineOffsets.size(); ++p) {
    for (int v = tb.userPolylineOffsets[p]; v < tb.userPolylineOffsets[p + 1]; ++v) {
      const size_t vi = static_cast<size_t>(v);
      sheet.paperPolyVerts.push_back(static_cast<float>(tb.userPolylineVerts[3 * vi] + dx));
      sheet.paperPolyVerts.push_back(static_cast<float>(tb.userPolylineVerts[3 * vi + 1] + dy));
      sheet.paperPolyVerts.push_back(0.f);
      sheet.paperPolyVertsBulge.push_back(vi < tb.userPolylineVertsBulge.size() ? tb.userPolylineVertsBulge[vi] : 0.f);
    }
    sheet.paperPolyClosed.push_back(p < tb.userPolylineClosed.size() ? tb.userPolylineClosed[p] : 0);
    EntityAttributes attr = p < tb.userPolylineAttrs.size() ? tb.userPolylineAttrs[p] : EntityAttributes{};
    attr.id = 0;
    sheet.paperPolyAttrs.push_back(attr);
    sheet.paperPolyOffsets.push_back(static_cast<int>(sheet.paperPolyVerts.size() / 3));
  }
  // REQ-518.8: the block's own text (labels and notes) comes across too. Styles and annotative scales name entries
  // the sheet's drawing does not have, so they are dropped; the baked height, font and style values are kept.
  for (const CadAnnotation& src : tb.cadAnnotations) {
    if (src.kind != CadAnnotation::Kind::Text && src.kind != CadAnnotation::Kind::Mtext) continue;
    CadAnnotation a = src;
    a.insX = static_cast<float>(src.insX + dx);
    a.insY = static_cast<float>(src.insY + dy);
    a.insZ = 0.f;
    if (src.kind == CadAnnotation::Kind::Mtext) {
      a.boxMinX = static_cast<float>(src.boxMinX + dx);
      a.boxMaxX = static_cast<float>(src.boxMaxX + dx);
      a.boxMinY = static_cast<float>(src.boxMinY + dy);
      a.boxMaxY = static_cast<float>(src.boxMaxY + dy);
    }
    a.styleName.clear();
    a.dimensionStyleName.clear();
    a.dimensionTextOverride.clear();
    a.annotative = false;
    a.annotativeVisibleScaleNames.clear();
    sheet.paperTexts.push_back(std::move(a));
    sheet.paperTextAttrs.push_back(EntityAttributes{});
  }
}

// REQ-518.8 / REQ-519.12: an ARCH D sheet is 36 x 24 in, landscape.
constexpr double kSheetW = 36.0, kSheetH = 24.0;

// The title block read for a sheet command, placed on the ARCH D sheet (REQ-518.8, .10).
struct SheetTitleBlock {
  AppCommandState state;
  Box bounds;                 // the block's own extent, in its DWG coordinates
  Box border;                 // its main border, in its DWG coordinates
  Box borderOnSheet;          // the border once the block is centred on the sheet
  Box inner;                  // the usable area inside the 0.5 in margin
  double dx = 0.0, dy = 0.0;  // the move from the block's coordinates to the sheet's
};

// Reads the title block DWG and centres it on the sheet. `cmd` names the command in the log. Refuses (with the reason in
// `log`, and nothing created) when the file is missing or unreadable, has no border, or its border is bigger than the sheet.
bool LoadSheetTitleBlock(const char* cmd, const std::string& path, SheetTitleBlock& out, std::vector<std::string>& log) {
  const std::string prefix = std::string(cmd) + " - ";
  if (path.empty() || !std::filesystem::exists(std::filesystem::u8path(path))) {
    log.push_back(prefix + "title block not found: \"" + path + "\". No sheets were created.");
    return false;
  }
  if (!ImportDwgFile(out.state, path.c_str(), log)) {
    log.push_back(prefix + "the title block could not be read. No sheets were created.");
    return false;
  }
  if (!TitleBlockBounds(out.state, &out.bounds) || !TitleBlockBorder(out.state, &out.border)) {
    log.push_back(prefix + "the title block has no border to fit. No sheets were created.");
    return false;
  }
  // REQ-518.8: the sheets are fixed at ARCH D, so a title block whose border is bigger would be clipped on the sheet.
  if (out.border.width() > kSheetW + 1e-6 || out.border.height() > kSheetH + 1e-6) {
    std::ostringstream size;
    size << std::fixed << std::setprecision(2) << out.border.width() << " x " << out.border.height();
    log.push_back(prefix + "the title block's border is " + size.str() +
                  " in, larger than the ARCH D sheet (36 x 24 in). No sheets were created.");
    return false;
  }
  // Centre the title block on the sheet, so its border lies in the same place on every sheet.
  out.dx = (kSheetW - out.bounds.width()) * 0.5 - out.bounds.minX;
  out.dy = (kSheetH - out.bounds.height()) * 0.5 - out.bounds.minY;
  out.borderOnSheet = Box{out.border.minX + out.dx, out.border.minY + out.dy, out.border.maxX + out.dx,
                          out.border.maxY + out.dy};
  out.inner = Box{out.borderOnSheet.minX + kSheetMarginIn, out.borderOnSheet.minY + kSheetMarginIn,
                  out.borderOnSheet.maxX - kSheetMarginIn, out.borderOnSheet.maxY - kSheetMarginIn};
  return true;
}

// The ARCH D entry in the paper size list, or -1 when the list has none.
int ArchDPresetIndex() {
  for (int i = 0; i < kPaperSizePresetCount; ++i)
    if (std::string(kPaperSizePresets[i].name).rfind("ARCH D", 0) == 0) return i;
  return -1;
}

// REQ-519.12: the sheet x where the title block's right-hand field column starts, found the way CREATESHEETS finds it (the
// horizontal field rules beside the main border). Sections stay to its left. Returns the inner edge when there is none.
double FieldColumnLeftOnSheet(const SheetTitleBlock& tb) {
  double left = tb.inner.maxX;
  bool found = false;
  for (size_t i = 0; i + 5 < tb.state.userLinesFlat.size(); i += 6) {
    const double x0 = tb.state.userLinesFlat[i], y0 = tb.state.userLinesFlat[i + 1];
    const double y1 = tb.state.userLinesFlat[i + 4];
    if (std::fabs(y0 - y1) > 1e-6) continue;                                // horizontal lines only
    if (x0 < tb.border.maxX - 0.05) continue;                               // the column beside the main border
    if (y0 < (tb.border.minY + tb.border.maxY) * 0.5) continue;             // the field cells above the middle
    left = found ? std::min(left, x0 + tb.dx) : x0 + tb.dx;
    found = true;
  }
  return found ? left : tb.inner.maxX;
}

// One straight line of native paper-space geometry (REQ-037), in sheet inches.
void AddSheetLine(PaperLayout& sheet, double ax, double ay, double bx, double by) {
  sheet.paperLines.insert(sheet.paperLines.end(), {static_cast<float>(ax), static_cast<float>(ay), 0.f,
                                                    static_cast<float>(bx), static_cast<float>(by), 0.f});
  sheet.paperLineAttrs.push_back(EntityAttributes{});
}

// REQ-518.9: one line of text on a sheet, in paper inches from its lower-left corner, with no style of its own.
void AddSheetText(PaperLayout& sheet, const std::string& text, double insX, double insY, double heightIn) {
  CadAnnotation a;
  a.kind = CadAnnotation::Kind::Text;
  a.insX = static_cast<float>(insX);
  a.insY = static_cast<float>(insY);
  a.plottedHeightInches = static_cast<float>(heightIn);
  a.text = text;
  sheet.paperTexts.push_back(std::move(a));
  sheet.paperTextAttrs.push_back(EntityAttributes{});
}

// REQ-518.2 / .10 / .11: the sheet's viewport. Its rectangle is the inner area of the border; it shows the frame's centre
// at the frame's scale, turned by the view roll that puts the alignment tangent across the sheet, and it is locked.
Viewport SheetViewportForFrame(const alignment::Alignment& a, const alignment::ViewFrame& f,
                               const alignment::ViewFrameSettings& s, const Box& inner) {
  const alignment::Point2 t = a.GetTangentAtStation(f.centreStation);
  Viewport vp;
  vp.paperXIn = static_cast<float>(inner.minX);
  vp.paperYIn = static_cast<float>(inner.minY);
  vp.paperWIn = static_cast<float>(inner.width());
  vp.paperHIn = static_cast<float>(inner.height());
  vp.modelCenterX = (f.corner[0].x + f.corner[2].x) * 0.5;
  vp.modelCenterY = (f.corner[0].y + f.corner[2].y) * 0.5;
  vp.scaleModelPerPaperIn = static_cast<float>(s.groundPerInch);
  vp.annotationScaleName = s.scaleName;
  vp.camRollDeg = Camera::RollToPlaceUp(0.f, 90.f, ray3d::Vec3{-t.y, t.x, 0.0});
  vp.locked = true;  // REQ-518.11: a locked sheet viewport keeps its model view when the sheet is navigated
  return vp;
}

// REQ-518.9: the fields of the title block, filled from the view frame, down the right-hand column of the border.
void AddSheetFields(PaperLayout& sheet, const alignment::Alignment& a, const alignment::ViewFrame& f,
                    const alignment::ViewFrameSettings& s, const alignment::StationFormat& fmt, double columnLeft,
                    double fieldRight, const std::vector<double>& rowMids) {
  alignment::StationFormat whole = fmt;  // whole stations: "17+00", so the field fits its column
  whole.decimals = 0;
  const std::string from = alignment::FormatStation(a.DisplayedStation(f.startStation, true), whole);
  const std::string to = alignment::FormatStation(a.DisplayedStation(f.endStation, true), whole);
  const std::time_t now = std::time(nullptr);
  std::tm local{};
  localtime_s(&local, &now);
  char date[16] = {};
  std::strftime(date, sizeof(date), "%m/%d/%Y", &local);
  const std::string size = s.sheetName.substr(0, s.sheetName.find(" ("));  // "ARCH D"
  const std::string rows[] = {
      "ALIGNMENT  " + a.name,
      "SHEET  " + f.name,
      "SCALE  " + s.scaleName,
      "SIZE  " + size,
      "DATE  " + std::string(date),
      "STA  " + from + " to " + to,
  };
  // One row per cell of the column: the row sits on the cell's middle, so no text runs along a box line. The height is
  // 0.1 in unless the longest row would run past the field's right-hand line, then it shrinks to fit (REQ-518.9).
  // A character takes about 0.75 of the height across, which is generous for the capitals in these rows.
  size_t longest = 0;
  for (const std::string& row : rows) longest = std::max(longest, row.size());
  const double insetIn = 0.08, rightPadIn = 0.05;
  const double availableIn = fieldRight - (columnLeft + insetIn) - rightPadIn;
  const double textIn = std::max(0.04, std::min(0.1, availableIn / (0.75 * static_cast<double>(longest))));
  for (size_t i = 0; i < std::size(rows) && i < rowMids.size(); ++i)
    AddSheetText(sheet, rows[i], columnLeft + insetIn, rowMids[i] - textIn * 0.4, textIn);
}

// REQ-518.9: a north arrow in the title block's empty right-hand column, centred at (cx, cy) in sheet inches, turned by
// the view roll so it points north on the sheet. The arrow is paper lines and its N is paper text.
void AddNorthArrow(PaperLayout& sheet, const Viewport& vp, double cx, double cy) {
  const double r = 0.4;
  float x0 = 0.f, y0 = 0.f, x1 = 0.f, y1 = 0.f;
  ModelToPaperInThroughCamera(vp, vp.modelCenterX, vp.modelCenterY, 0.0, &x0, &y0);
  ModelToPaperInThroughCamera(vp, vp.modelCenterX, vp.modelCenterY + 1.0, 0.0, &x1, &y1);
  double dx = x1 - x0, dy = y1 - y0;
  const double len = std::hypot(dx, dy);
  if (len < 1e-9) {
    dx = 0.0;
    dy = 1.0;
  } else {
    dx /= len;
    dy /= len;
  }
  auto segment = [&sheet](double ax, double ay, double bx, double by) {
    sheet.paperLines.insert(sheet.paperLines.end(), {static_cast<float>(ax), static_cast<float>(ay), 0.f,
                                                      static_cast<float>(bx), static_cast<float>(by), 0.f});
    sheet.paperLineAttrs.push_back(EntityAttributes{});
  };
  const double tipX = cx + dx * r, tipY = cy + dy * r;
  segment(cx - dx * r, cy - dy * r, tipX, tipY);
  // Head: two short barbs back from the tip, 30 degrees either side of the shaft.
  for (double side : {-1.0, 1.0}) {
    const double ang = side * 30.0 * 3.14159265358979323846 / 180.0;
    const double bx = -(dx * std::cos(ang) - dy * std::sin(ang));
    const double by = -(dx * std::sin(ang) + dy * std::cos(ang));
    segment(tipX, tipY, tipX + bx * 0.18, tipY + by * 0.18);
  }
  AddSheetText(sheet, "N", tipX + dx * 0.12, tipY + dy * 0.12, 0.12);
}

}  // namespace

// CREATESHEETS <alignment>[, <frames>[, <title block DWG>]]  (REQ-518). One paper layout per chosen frame of the
// alignment's single view frame group, built on the title block DWG (REQ-518.8). One undo step for all of them.
void RunCreateSheets(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitFields(args);
  if (f.empty() || f.size() > 3) {
    log.push_back("CREATESHEETS - usage: CREATESHEETS <alignment>[, <frames>[, <title block DWG>]]");
    return;
  }
  if (ActiveProjectPointsReadOnly(st)) {  // REQ-518.4: a read-only project refuses before any change
    log.push_back("CREATESHEETS - the project is read-only; no sheets were created.");
    return;
  }
  const alignment::Alignment* a = Resolve(st, "CREATESHEETS", f[0], log);
  if (a == nullptr) return;

  // REQ-518.4: exactly one view frame group follows the alignment; with two, the sheets could not tell which to use.
  std::vector<const alignment::ViewFrameGroup*> groups;
  for (const alignment::ViewFrameGroup& g : st.viewFrameGroups)
    if (g.alignmentId == a->id) groups.push_back(&g);
  if (groups.empty()) {
    log.push_back("CREATESHEETS - \"" + a->name + "\" has no view frame group. No sheets were created.");
    return;
  }
  if (groups.size() > 1) {
    log.push_back("CREATESHEETS - \"" + a->name + "\" has " + std::to_string(groups.size()) +
                  " view frame groups; sheets need one. Delete the extra groups with VIEWFRAMEDELETE first. "
                  "No sheets were created.");
    return;
  }
  const alignment::ViewFrameGroup& g = *groups[0];
  if (g.frames.empty()) {
    log.push_back("CREATESHEETS - \"" + a->name + "\" has no frames to make sheets from. No sheets were created.");
    return;
  }

  // REQ-518.8: the title block DWG. The default is the Generic 24in x 36in block in the AutoCAD template folder.
  const std::string titleBlockPath = f.size() == 3 ? Trim(f[2]) : DefaultTitleBlockPath();
  SheetTitleBlock tb;
  if (!LoadSheetTitleBlock("CREATESHEETS", titleBlockPath, tb, log)) return;
  const AppCommandState& titleBlock = tb.state;
  const Box& tbBounds = tb.bounds;
  const Box& border = tb.border;
  const Box& borderOnSheet = tb.borderOnSheet;
  const Box& inner = tb.inner;
  const double dx = tb.dx, dy = tb.dy;

  // REQ-518.2 / .10: the frames are sized to the usable area inside the margin, at the same scale, so each sheet's
  // viewport covers its whole frame. The stored group changes only when the sheets are made (one undo step).
  alignment::ViewFrameGroup sized = g;
  sized.settings.sheetWidthIn = inner.width();
  sized.settings.sheetHeightIn = inner.height();
  sized.settings.sheetName = "ARCH D";  // the sheets are ARCH D; the SIZE field and reports read this
  if (const std::string refusal = alignment::RegenerateViewFrameGroup(*a, sized); !refusal.empty()) {
    log.push_back("CREATESHEETS - " + refusal + " No sheets were created.");
    return;
  }
  if (sized.frames.empty()) {
    log.push_back("CREATESHEETS - no frames fit the title block's usable area. No sheets were created.");
    return;
  }

  // Which frames: ALL (or none given) is every frame; otherwise named frames, each of which must exist.
  std::vector<size_t> chosen;
  const std::string framesText = f.size() >= 2 ? Trim(f[1]) : std::string();
  if (framesText.empty() || Lower(framesText) == "all") {
    for (size_t i = 0; i < sized.frames.size(); ++i) chosen.push_back(i);
  } else {
    std::istringstream words(framesText);
    std::string word;
    while (words >> word) {
      size_t found = sized.frames.size();
      for (size_t i = 0; i < sized.frames.size(); ++i)
        if (Lower(sized.frames[i].name) == Lower(word)) found = i;
      if (found == sized.frames.size()) {
        log.push_back("CREATESHEETS - no frame named \"" + word + "\" in \"" + a->name +
                      "\" at the title block's size. No sheets were created.");
        return;
      }
      if (std::find(chosen.begin(), chosen.end(), found) == chosen.end()) chosen.push_back(found);
    }
  }

  const int archD = ArchDPresetIndex();

  // REQ-518.9: the right-hand column's horizontal box lines (in sheet coordinates) give the field rows: one per cell.
  double columnLeft = borderOnSheet.maxX;
  std::vector<double> lineYs;
  for (size_t i = 0; i + 5 < titleBlock.userLinesFlat.size(); i += 6) {
    const double x0 = titleBlock.userLinesFlat[i], y0 = titleBlock.userLinesFlat[i + 1];
    const double y1 = titleBlock.userLinesFlat[i + 4];
    if (std::fabs(y0 - y1) > 1e-6) continue;                                      // horizontal lines only
    if (x0 < border.maxX - 0.05) continue;                                        // the column beside the main border
    if (y0 < (border.minY + border.maxY) * 0.5) continue;                         // the cells above the middle of the block
    lineYs.push_back(y0 + dy);
    columnLeft = lineYs.size() == 1 ? x0 + dx : std::min(columnLeft, x0 + dx);
  }
  std::sort(lineYs.begin(), lineYs.end(), std::greater<double>());
  lineYs.erase(std::unique(lineYs.begin(), lineYs.end(), [](double p, double q) { return std::fabs(p - q) < 1e-6; }),
               lineYs.end());
  std::vector<double> rowMids;
  for (size_t i = 0; i + 1 < lineYs.size(); ++i) rowMids.push_back((lineYs[i] + lineYs[i + 1]) * 0.5);
  // REQ-518.9: a field's text stops at the first vertical line inside the column, so it stays in its own cell.
  const double columnRight = tbBounds.maxX + dx;
  double fieldRight = columnRight;
  if (lineYs.size() >= 2) {
    const double fieldsLow = lineYs.back(), fieldsHigh = lineYs.front();
    for (size_t i = 0; i + 5 < titleBlock.userLinesFlat.size(); i += 6) {
      const double x0 = titleBlock.userLinesFlat[i] + dx, x1 = titleBlock.userLinesFlat[i + 3] + dx;
      const double y0 = titleBlock.userLinesFlat[i + 1] + dy, y1 = titleBlock.userLinesFlat[i + 4] + dy;
      if (std::fabs(x0 - x1) > 1e-6) continue;                                  // vertical lines only
      if (std::max(y0, y1) < fieldsLow || std::min(y0, y1) > fieldsHigh) continue;  // beside the fields
      if (x0 > columnLeft + 0.05 && x0 < fieldRight - 0.01) fieldRight = x0;
    }
  }
  // REQ-518.9: the north arrow sits in the empty part of the column, below the fields and centred across the column.
  const double arrowX = (columnLeft + columnRight) * 0.5;
  const double arrowY = lineYs.size() >= 2 ? lineYs.back() - 1.3 : borderOnSheet.maxY - 3.0;

  // Build every sheet before the undo snapshot, so a refusal above leaves history untouched.
  std::vector<std::string> takenNames;
  for (const PaperLayout& layout : st.paperLayouts) takenNames.push_back(Lower(layout.name));
  auto nameTaken = [&takenNames](const std::string& name) {
    return std::find(takenNames.begin(), takenNames.end(), Lower(name)) != takenNames.end();
  };
  std::vector<PaperLayout> sheets;
  for (size_t i : chosen) {
    const alignment::ViewFrame& fr = sized.frames[i];
    std::string name = fr.name;
    for (int suffix = 2; nameTaken(name); ++suffix) name = fr.name + " (" + std::to_string(suffix) + ")";
    takenNames.push_back(Lower(name));

    PaperLayout sheet;
    sheet.name = name;
    sheet.landscape = true;
    sheet.portraitWidthIn = static_cast<float>(kSheetH);  // landscape: width = portrait height
    sheet.portraitHeightIn = static_cast<float>(kSheetW);
    sheet.presetIdx = archD;
    CopyTitleBlockOntoSheet(sheet, titleBlock, dx, dy);
    sheet.viewports.assign(1, SheetViewportForFrame(*a, fr, sized.settings, inner));
    AddSheetFields(sheet, *a, fr, sized.settings, st.alignmentStationFormat, columnLeft, fieldRight, rowMids);
    AddNorthArrow(sheet, sheet.viewports[0], arrowX, arrowY);
    sheet.viewInit = false;  // fit the new sheet to the window on first entry
    sheets.push_back(std::move(sheet));
  }

  PushUndoSnapshot(st, "Create sheets");  // REQ-518.5: one undo step removes every sheet and the frame resize
  const size_t framesBefore = g.frames.size();
  for (alignment::ViewFrameGroup& stored : st.viewFrameGroups)
    if (stored.id == g.id) stored = sized;
  // The title block's layers come into this drawing where they are not already here.
  for (const CadLayerRow& row : titleBlock.drawingLayerTable) {
    const bool present = std::any_of(st.drawingLayerTable.begin(), st.drawingLayerTable.end(),
                                     [&row](const CadLayerRow& have) { return have.name == row.name; });
    if (!present) st.drawingLayerTable.push_back(row);
  }
  std::string names;
  for (PaperLayout& sheet : sheets) {
    if (!names.empty()) names += ", ";
    names += sheet.name;
    st.paperLayouts.push_back(std::move(sheet));
  }
  BumpCadGpuCache(st);
  char resize[320];
  std::snprintf(resize, sizeof(resize),
                " The view frames of \"%s\" were resized to the usable area inside the margin, %.2f x %.2f in at the "
                "same scale: %zu frame(s), was %zu (REQ-518, D-2026-11-08-f).",
                a->name.c_str(), inner.width(), inner.height(), sized.frames.size(), framesBefore);
  log.push_back("CREATESHEETS - " + std::to_string(chosen.size()) + " sheet(s) from \"" + a->name + "\" on \"" +
                kTitleBlockFile + "\": " + names + "." + resize);
}

// REQ-519.1, .2, .9: CREATESAMPLELINES <alignment>, <start station>, <end station>, <interval>, <left swath>, <right
// swath>[, <surface>]. One sample line at each interval station in the range, named by its station. A named TIN surface
// samples every line's section at once. One undo step for all the lines.
void RunCreateSampleLines(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitFields(args);
  if (f.size() < 6 || f.size() > 7) {
    log.push_back("CREATESAMPLELINES - usage: CREATESAMPLELINES <alignment>, <start station>, <end station>, <interval>, "
                  "<left swath>, <right swath>[, <surface>]");
    return;
  }
  if (ActiveProjectPointsReadOnly(st)) {  // REQ-519.4: a read-only project refuses before any change
    log.push_back("CREATESAMPLELINES - the project is read-only; no sample lines were created.");
    return;
  }
  const alignment::Alignment* a = Resolve(st, "CREATESAMPLELINES", f[0], log);
  if (a == nullptr) return;

  alignment::SampleLineSettings s;
  if (!ParseSta(st, f[1], &s.startStation) || !ParseSta(st, f[2], &s.endStation) || !ParseNum(Trim(f[3]), &s.interval) ||
      !ParseNum(Trim(f[4]), &s.leftWidth) || !ParseNum(Trim(f[5]), &s.rightWidth)) {
    log.push_back("CREATESAMPLELINES - a station, the interval or a swath is not understood. No sample lines were created.");
    return;
  }

  // REQ-519.9: the optional surface. Leaving it out, or NONE, makes lines with no section.
  const std::string surfaceText = f.size() == 7 ? Trim(f[6]) : std::string();
  int surfaceIndex = -1;
  if (!surfaceText.empty() && Lower(surfaceText) != "none") {
    surfaceIndex = FindSurfaceIndex(st, surfaceText);
    if (surfaceIndex < 0) {
      log.push_back("CREATESAMPLELINES - no surface named \"" + surfaceText + "\". No sample lines were created.");
      return;
    }
    const CadSurface& surface = st.cadSurfaces[static_cast<size_t>(surfaceIndex)];
    if (surface.kind != SurfaceKind::Tin || !surface.tin) {
      log.push_back("CREATESAMPLELINES - \"" + surfaceText +
                    "\" is not a built TIN surface; sections need a TIN. No sample lines were created.");
      return;
    }
  }

  std::vector<alignment::SampleLine> lines;
  std::string why;
  if (!alignment::ComputeSampleLines(*a, s, st.alignmentStationFormat, &lines, &why)) {
    log.push_back("CREATESAMPLELINES - refused: " + why + ". No sample lines were created.");
    return;
  }
  // A name already on this alignment would leave two lines the command cannot tell apart.
  for (const alignment::SampleLine& line : lines) {
    for (const alignment::SampleLine& have : st.sampleLines) {
      if (have.alignmentId == a->id && Lower(have.name) == Lower(line.name)) {
        log.push_back("CREATESAMPLELINES - refused: a sample line named \"" + line.name + "\" already exists on \"" +
                      a->name + "\". No sample lines were created.");
        return;
      }
    }
  }

  // REQ-519.2: sample each line against the surface. A line the surface does not cover keeps an empty section.
  const std::uint64_t alignmentId = a->id;
  const std::string alignmentName = a->name;
  std::string surfaceName;
  std::string noData;
  size_t withGround = 0;
  if (surfaceIndex >= 0) {
    const CadSurface& surface = st.cadSurfaces[static_cast<size_t>(surfaceIndex)];
    const std::unique_ptr<ISurfaceQuery> query = MakeSurfaceQueryForCommands(st, surface);
    if (!query) {
      log.push_back("CREATESAMPLELINES - \"" + surfaceText + "\" is not a built surface. No sample lines were created.");
      return;
    }
    surfaceName = surface.name;
    // REQ-519.2: the index is built once for the run, so each station looks only at the triangles near its swath.
    const TinSpatialIndex index = BuildTinSpatialIndex(surface.tin->vertsXyz, surface.tin->indices);
    for (alignment::SampleLine& line : lines) {
      line.hasSection = true;
      line.surfaceName = surfaceName;
      alignment::SampleSection(surface.tin->vertsXyz, surface.tin->indices, index, *query, line);
      if (line.section.empty()) {
        noData += (noData.empty() ? "" : ", ") + line.name;
      } else {
        ++withGround;
      }
    }
  }

  PushUndoSnapshot(st, "Create sample lines");  // REQ-519.5: one undo step removes every line
  const std::string first = lines.front().name, last = lines.back().name;
  const size_t count = lines.size();
  for (alignment::SampleLine& line : lines) {
    line.id = st.nextEntityId++;
    line.alignmentId = alignmentId;
    st.sampleLines.push_back(std::move(line));
  }
  BumpCadGpuCache(st);
  std::string message = "CREATESAMPLELINES - " + std::to_string(count) + " sample line(s) on \"" + alignmentName + "\", " +
                        first + " to " + last + ".";
  if (surfaceIndex >= 0) {
    message += " Sections sampled from \"" + surfaceName + "\": " + std::to_string(withGround) + " with ground, " +
               std::to_string(count - withGround) + " with no data.";
    if (!noData.empty()) message += " No data (no surface coverage) at " + noData + ".";
  } else {
    message += " No surface was named, so the lines have no sections.";
  }
  log.push_back(message);
}

// REQ-519.3, .4, .11, .12: CREATESECTIONSHEETS <alignment>, <sample lines>, <sections per sheet>, <axis elevation>. Each
// ARCH D sheet holds the chosen sections, in station order, from left to right, on the title block (REQ-518.8). One
// undo step for all the sheets.
void RunCreateSectionSheets(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  constexpr int kMaxSectionsPerSheet = 10;
  constexpr double kPlotMarginIn = 1.0;  // the bands above and below the plot hold the scale text and station labels
  constexpr double kColumnPadIn = 0.1;   // clear space between a section and its column's edge
  const std::vector<std::string> f = SplitFields(args);
  if (f.size() != 4) {
    log.push_back("CREATESECTIONSHEETS - usage: CREATESECTIONSHEETS <alignment>, <sample lines>, <sections per sheet>, "
                  "<axis elevation>");
    return;
  }
  if (ActiveProjectPointsReadOnly(st)) {  // REQ-519.4: a read-only project refuses before any change
    log.push_back("CREATESECTIONSHEETS - the project is read-only; no sheets were created.");
    return;
  }
  const alignment::Alignment* a = Resolve(st, "CREATESECTIONSHEETS", f[0], log);
  if (a == nullptr) return;

  double perSheetValue = 0.0, axisElevation = 0.0;
  if (!ParseNum(Trim(f[2]), &perSheetValue) || perSheetValue != std::floor(perSheetValue) || perSheetValue < 1.0 ||
      perSheetValue > kMaxSectionsPerSheet) {
    log.push_back("CREATESECTIONSHEETS - sections per sheet must be a whole number from 1 to " +
                  std::to_string(kMaxSectionsPerSheet) + ". No sheets were created.");
    return;
  }
  if (!ParseNum(Trim(f[3]), &axisElevation) || !std::isfinite(axisElevation)) {
    log.push_back("CREATESECTIONSHEETS - the axis elevation \"" + f[3] + "\" is not a number. No sheets were created.");
    return;
  }
  const size_t perSheet = static_cast<size_t>(perSheetValue);

  // REQ-519.4: the chosen lines must belong to this alignment. ALL (or none given) is every line on it.
  std::vector<const alignment::SampleLine*> pool;
  for (const alignment::SampleLine& line : st.sampleLines)
    if (line.alignmentId == a->id) pool.push_back(&line);
  if (pool.empty()) {
    log.push_back("CREATESECTIONSHEETS - \"" + a->name + "\" has no sample lines. Make them with CREATESAMPLELINES first. "
                  "No sheets were created.");
    return;
  }
  std::vector<const alignment::SampleLine*> chosen;
  const std::string linesText = Trim(f[1]);
  if (linesText.empty()) {
    log.push_back("CREATESECTIONSHEETS - give ALL or the sample line names. No sheets were created.");
    return;
  }
  if (Lower(linesText) == "all") {
    chosen = pool;
  } else {
    std::istringstream words(linesText);
    std::string word;
    while (words >> word) {
      const alignment::SampleLine* found = nullptr;
      for (const alignment::SampleLine* line : pool)
        if (Lower(line->name) == Lower(word)) found = line;
      if (found == nullptr) {
        log.push_back("CREATESECTIONSHEETS - no sample line named \"" + word + "\" on \"" + a->name +
                      "\". No sheets were created.");
        return;
      }
      if (std::find(chosen.begin(), chosen.end(), found) == chosen.end()) chosen.push_back(found);
    }
  }
  // REQ-519.4: with no surface behind any chosen line there is nothing to draw, so the command refuses.
  const bool anySection = std::any_of(chosen.begin(), chosen.end(),
                                      [](const alignment::SampleLine* line) { return line->hasSection; });
  if (!anySection) {
    log.push_back("CREATESECTIONSHEETS - none of the chosen sample lines has a section (no surface was named when they "
                  "were made). No sheets were created.");
    return;
  }
  std::stable_sort(chosen.begin(), chosen.end(), [](const alignment::SampleLine* p, const alignment::SampleLine* q) {
    return p->internalStation < q->internalStation;  // REQ-519.3: left to right in station order
  });

  // REQ-519.12: the same title block and margin as CREATESHEETS (REQ-518.8, .10).
  const std::string titleBlockPath = DefaultTitleBlockPath();
  SheetTitleBlock tb;
  if (!LoadSheetTitleBlock("CREATESECTIONSHEETS", titleBlockPath, tb, log)) return;
  const Box& inner = tb.inner;
  // REQ-519.12: the sections stop short of the title block's field column, so they never cross the fields.
  const double plotMinX = inner.minX;
  const double plotMaxX = std::min(inner.maxX, FieldColumnLeftOnSheet(tb) - kColumnPadIn);
  const double plotMinY = inner.minY + kPlotMarginIn, plotMaxY = inner.maxY - kPlotMarginIn;
  const double plotHeight = plotMaxY - plotMinY;
  const double axisY = (plotMinY + plotMaxY) * 0.5;  // every section's axis sits at the middle of the plot
  const double columnWidth = (plotMaxX - plotMinX) / static_cast<double>(perSheet);

  std::vector<std::string> takenNames;
  for (const PaperLayout& layout : st.paperLayouts) takenNames.push_back(Lower(layout.name));
  auto nameTaken = [&takenNames](const std::string& name) {
    return std::find(takenNames.begin(), takenNames.end(), Lower(name)) != takenNames.end();
  };

  std::vector<PaperLayout> sheets;
  const size_t sheetCount = (chosen.size() + perSheet - 1) / perSheet;
  for (size_t sheetIndex = 0; sheetIndex < sheetCount; ++sheetIndex) {
    const size_t first = sheetIndex * perSheet;
    const size_t last = std::min(chosen.size(), first + perSheet);

    // REQ-519.11: one horizontal and one vertical scale for every section on the sheet, the finest step at which all fit.
    bool anyPoint = false;
    double maxOffset = 0.0, maxDeviation = 0.0;
    for (size_t i = first; i < last; ++i) {
      for (const alignment::SectionPoint& p : chosen[i]->section) {
        anyPoint = true;
        maxOffset = std::max(maxOffset, std::fabs(p.offset));
        maxDeviation = std::max(maxDeviation, std::fabs(p.ground - axisElevation));
      }
    }
    const alignment::SectionScale fit = alignment::FitSectionScale(
        anyPoint, maxOffset, maxDeviation, columnWidth * 0.5 - kColumnPadIn, plotHeight * 0.5 - kColumnPadIn);
    const double feetPerInchAcross = fit.feetPerInchAcross;
    const double feetPerInchDown = fit.feetPerInchDown;

    const std::string base = "SEC-" + std::to_string(sheetIndex + 1);
    std::string name = base;
    for (int suffix = 2; nameTaken(name); ++suffix) name = base + " (" + std::to_string(suffix) + ")";
    takenNames.push_back(Lower(name));

    PaperLayout sheet;
    sheet.name = name;
    sheet.landscape = true;
    sheet.portraitWidthIn = static_cast<float>(kSheetH);  // landscape: width = portrait height
    sheet.portraitHeightIn = static_cast<float>(kSheetW);
    sheet.presetIdx = ArchDPresetIndex();
    CopyTitleBlockOntoSheet(sheet, tb.state, tb.dx, tb.dy);

    std::ostringstream scale;
    scale << std::fixed << std::setprecision(0) << "Horizontal 1\" = " << feetPerInchAcross << "'    Vertical 1\" = "
          << feetPerInchDown << "'";
    AddSheetText(sheet, scale.str(), inner.minX + 0.3, inner.maxY - 0.45, 0.14);

    for (size_t i = first; i < last; ++i) {
      const alignment::SampleLine& line = *chosen[i];
      const double columnLeft = plotMinX + static_cast<double>(i - first) * columnWidth;
      const double centre = columnLeft + columnWidth * 0.5;
      AddSheetLine(sheet, columnLeft + kColumnPadIn, axisY, columnLeft + columnWidth - kColumnPadIn, axisY);
      // The ground line: each section point at its offset across and its elevation above or below the axis (REQ-519.3).
      for (size_t p = 0; p + 1 < line.section.size(); ++p) {
        const alignment::SectionPoint& p0 = line.section[p];
        const alignment::SectionPoint& p1 = line.section[p + 1];
        if (p0.gapAfter) continue;  // REQ-519.2: no ground is drawn across an uncovered stretch
        AddSheetLine(sheet, centre + p0.offset / feetPerInchAcross,
                     axisY + (p0.ground - axisElevation) / feetPerInchDown, centre + p1.offset / feetPerInchAcross,
                     axisY + (p1.ground - axisElevation) / feetPerInchDown);
      }
      AddSheetText(sheet, line.name, centre - 0.3, plotMinY - 0.35, 0.12);
      if (line.section.empty()) AddSheetText(sheet, "no data", centre - 0.3, axisY + 0.15, 0.12);
    }
    sheet.viewInit = false;  // fit the new sheet to the window on first entry
    sheets.push_back(std::move(sheet));
  }

  PushUndoSnapshot(st, "Create section sheets");  // REQ-519.5: one undo step removes every sheet
  // The title block's layers come into this drawing where they are not already here (as CREATESHEETS).
  for (const CadLayerRow& row : tb.state.drawingLayerTable) {
    const bool present = std::any_of(st.drawingLayerTable.begin(), st.drawingLayerTable.end(),
                                     [&row](const CadLayerRow& have) { return have.name == row.name; });
    if (!present) st.drawingLayerTable.push_back(row);
  }
  std::string names;
  for (PaperLayout& sheet : sheets) {
    if (!names.empty()) names += ", ";
    names += sheet.name;
    st.paperLayouts.push_back(std::move(sheet));
  }
  BumpCadGpuCache(st);
  log.push_back("CREATESECTIONSHEETS - " + std::to_string(chosen.size()) + " section(s) on " + std::to_string(sheetCount) +
                " sheet(s) from \"" + a->name + "\" on \"" + kTitleBlockFile + "\": " + names + ".");
}

// ---- REQ-521: design vertical profiles along an alignment (GitHub issue #1163) ----

namespace {

constexpr double kProfileStationTol = 1e-6;

// Index of the profile named \p name (case-insensitive), or -1.
int FindProfileIndex(const AppCommandState& st, const std::string& name) {
  const std::string key = Lower(Trim(name));
  for (size_t i = 0; i < st.profiles.size(); ++i)
    if (Lower(st.profiles[i].name) == key) return static_cast<int>(i);
  return -1;
}

// The alignment with stable id \p id, or nullptr when it has been deleted (REQ-521.7).
const alignment::Alignment* AlignmentById(const AppCommandState& st, std::uint64_t id) {
  for (const alignment::Alignment& a : st.alignments)
    if (a.id == id) return &a;
  return nullptr;
}

// The internal station for the displayed station \p displayed on \p a (REQ-517.6a), which must lie on the alignment.
// False with `why` when it is not exactly one station or is outside the alignment.
bool InternalStationOnAlignment(const AppCommandState& st, const alignment::Alignment& a, double displayed,
                                double* internal, std::string* why) {
  if (!alignment::ResolveDisplayedStation(a, displayed, internal, why)) return false;
  if (*internal < a.StartStation() - kProfileStationTol || *internal > a.EndStation() + kProfileStationTol) {
    *why = "the station " + StaFmt(st, displayed) + " is outside the alignment (" + Sta(st, a, a.StartStation()) +
           " to " + Sta(st, a, a.EndStation()) + ")";
    return false;
  }
  return true;
}

// The grade as a percent, two decimals (rise over run x 100).
std::string GradePercent(double grade) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.2f%%", grade * 100.0);
  return buf;
}

}  // namespace

// CREATEDESIGNPROFILE <alignment>, <profile name> (REQ-521.6): an empty profile on the alignment. One undo step.
void RunCreateDesignProfile(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitFields(args);
  if (f.size() != 2) {
    log.push_back("CREATEDESIGNPROFILE - usage: CREATEDESIGNPROFILE <alignment>, <profile name>");
    return;
  }
  if (ActiveProjectPointsReadOnly(st)) {  // REQ-521.6: a read-only project refuses before any change
    log.push_back("CREATEDESIGNPROFILE - the project is read-only; no profile was created.");
    return;
  }
  const alignment::Alignment* a = Resolve(st, "CREATEDESIGNPROFILE", f[0], log);
  if (a == nullptr) return;
  const std::string name = Trim(f[1]);
  if (name.empty()) {
    log.push_back("CREATEDESIGNPROFILE - a profile name is required. No profile was created.");
    return;
  }
  if (FindProfileIndex(st, name) >= 0) {
    log.push_back("CREATEDESIGNPROFILE - a profile named \"" + name + "\" already exists. No profile was created.");
    return;
  }
  const std::uint64_t alignmentId = a->id;
  const std::string alignmentName = a->name;
  PushUndoSnapshot(st, "Create design profile");
  alignment::VerticalProfile p;
  p.id = st.nextEntityId++;
  p.name = name;
  p.alignmentId = alignmentId;
  st.profiles.push_back(std::move(p));
  log.push_back("CREATEDESIGNPROFILE - profile \"" + name + "\" on \"" + alignmentName + "\" created with no PVIs.");
}

// PROFILEPVI <profile>, <station>, <elevation>[, <curve length>] (REQ-521.6): adds the PVI at that station, or changes the
// elevation and curve length of the PVI already there. Leaving out the curve length means no curve. One undo step.
void RunProfilePvi(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitFields(args);
  if (f.size() < 3 || f.size() > 4) {
    log.push_back("PROFILEPVI - usage: PROFILEPVI <profile>, <station>, <elevation>[, <curve length>]");
    return;
  }
  if (ActiveProjectPointsReadOnly(st)) {
    log.push_back("PROFILEPVI - the project is read-only; no PVI was changed.");
    return;
  }
  const int pi = FindProfileIndex(st, f[0]);
  if (pi < 0) {
    log.push_back("PROFILEPVI - no profile named \"" + Trim(f[0]) + "\". No PVI was changed.");
    return;
  }
  const std::string profileName = st.profiles[static_cast<size_t>(pi)].name;
  const alignment::Alignment* a = AlignmentById(st, st.profiles[static_cast<size_t>(pi)].alignmentId);
  if (a == nullptr) {
    log.push_back("PROFILEPVI - profile \"" + profileName + "\" has no alignment (alignment missing). No PVI was changed.");
    return;
  }
  double displayed = 0.0, elevation = 0.0, curveLength = 0.0;
  if (!ParseSta(st, f[1], &displayed) || !ParseNum(Trim(f[2]), &elevation) ||
      (f.size() == 4 && !ParseNum(Trim(f[3]), &curveLength))) {
    log.push_back("PROFILEPVI - a station, elevation or curve length is not understood. No PVI was changed.");
    return;
  }
  double station = 0.0;
  std::string why;
  if (!InternalStationOnAlignment(st, *a, displayed, &station, &why)) {
    log.push_back("PROFILEPVI - " + why + ". No PVI was changed.");
    return;
  }

  alignment::VerticalProfile candidate = st.profiles[static_cast<size_t>(pi)];
  bool replaced = false;
  for (alignment::VerticalPoint& q : candidate.points) {
    if (std::abs(q.station - station) <= kProfileStationTol) {  // edit: keep the PVI's station
      q.elevation = elevation;
      q.curveLength = curveLength;
      replaced = true;
      break;
    }
  }
  if (!replaced) {
    const auto pos = std::find_if(candidate.points.begin(), candidate.points.end(),
                                  [station](const alignment::VerticalPoint& q) { return q.station > station; });
    candidate.points.insert(pos, alignment::VerticalPoint{station, elevation, curveLength});
  }
  const std::string refusal = alignment::ProfileGeometryRefusal(candidate);
  if (!refusal.empty()) {  // REQ-521.3: refused input changes nothing
    log.push_back("PROFILEPVI - refused: " + refusal + ". No PVI was changed.");
    return;
  }

  const std::string staText = Sta(st, *a, station);
  PushUndoSnapshot(st, replaced ? "Edit profile PVI" : "Add profile PVI");
  st.profiles[static_cast<size_t>(pi)].points = std::move(candidate.points);
  log.push_back("PROFILEPVI - profile \"" + profileName + "\": PVI at " + staText + (replaced ? " changed" : " added") +
                " (elevation " + Lin(st, elevation) +
                (curveLength > 0.0 ? ", curve length " + Lin(st, curveLength) : std::string(", no curve")) + ").");
}

// PROFILEDELPVI <profile>, <station> (REQ-521.6): removes the PVI at that station. Refused when the rest is not valid.
void RunProfileDelPvi(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitFields(args);
  if (f.size() != 2) {
    log.push_back("PROFILEDELPVI - usage: PROFILEDELPVI <profile>, <station>");
    return;
  }
  if (ActiveProjectPointsReadOnly(st)) {
    log.push_back("PROFILEDELPVI - the project is read-only; no PVI was removed.");
    return;
  }
  const int pi = FindProfileIndex(st, f[0]);
  if (pi < 0) {
    log.push_back("PROFILEDELPVI - no profile named \"" + Trim(f[0]) + "\". No PVI was removed.");
    return;
  }
  const std::string profileName = st.profiles[static_cast<size_t>(pi)].name;
  const alignment::Alignment* a = AlignmentById(st, st.profiles[static_cast<size_t>(pi)].alignmentId);
  if (a == nullptr) {
    log.push_back("PROFILEDELPVI - profile \"" + profileName + "\" has no alignment (alignment missing). No PVI was removed.");
    return;
  }
  double displayed = 0.0, station = 0.0;
  std::string why;
  // Removal matches the PVI by its station, so a PVI left beyond a shortened alignment can still be removed (REQ-521.6),
  // unless the alignment has station equations: then the gap reason refuses it (REQ-521 item 6).
  if (!ParseSta(st, f[1], &displayed) || !alignment::ResolveDisplayedStation(*a, displayed, &station, &why)) {
    log.push_back("PROFILEDELPVI - " + (why.empty() ? std::string("the station is not understood") : why) +
                  ". No PVI was removed.");
    return;
  }
  alignment::VerticalProfile candidate = st.profiles[static_cast<size_t>(pi)];
  const auto found = std::find_if(candidate.points.begin(), candidate.points.end(), [station](const alignment::VerticalPoint& q) {
    return std::abs(q.station - station) <= kProfileStationTol;
  });
  if (found == candidate.points.end()) {
    log.push_back("PROFILEDELPVI - profile \"" + profileName + "\" has no PVI at " + Sta(st, *a, station) +
                  ". No PVI was removed.");
    return;
  }
  candidate.points.erase(found);
  const std::string refusal = alignment::ProfileGeometryRefusal(candidate);
  if (!refusal.empty()) {
    log.push_back("PROFILEDELPVI - refused: " + refusal + ". No PVI was removed.");
    return;
  }
  const std::string staText = Sta(st, *a, station);
  PushUndoSnapshot(st, "Delete profile PVI");
  st.profiles[static_cast<size_t>(pi)].points = std::move(candidate.points);
  log.push_back("PROFILEDELPVI - profile \"" + profileName + "\": PVI at " + staText + " removed.");
}

// PROFILEQUERY <profile>, <station> (REQ-521.4, .5): elevation, grade and the derived 3D design point at a displayed
// station. Refused, with the reason, when the profile's alignment is missing.
void RunProfileQuery(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitFields(args);
  if (f.size() != 2) {
    log.push_back("PROFILEQUERY - usage: PROFILEQUERY <profile>, <station>");
    return;
  }
  const int pi = FindProfileIndex(st, f[0]);
  if (pi < 0) {
    log.push_back("PROFILEQUERY - no profile named \"" + Trim(f[0]) + "\".");
    return;
  }
  const alignment::VerticalProfile& p = st.profiles[static_cast<size_t>(pi)];
  const alignment::Alignment* a = AlignmentById(st, p.alignmentId);
  if (a == nullptr) {
    log.push_back("PROFILEQUERY - profile \"" + p.name + "\" has no alignment (alignment missing), so it has no design path.");
    return;
  }
  double displayed = 0.0, station = 0.0;
  std::string why;
  // REQ-521.4: the profile's range answers first ("outside the profile"). A displayed station the alignment cannot resolve
  // (a station-equation gap) is refused by ResolveDisplayedStation before that; DesignPathPoint checks the alignment range.
  if (!ParseSta(st, f[1], &displayed) || !alignment::ResolveDisplayedStation(*a, displayed, &station, &why)) {
    log.push_back("PROFILEQUERY - " + (why.empty() ? std::string("the station is not understood") : why) + ".");
    return;
  }
  const alignment::ProfileSample sample = alignment::ProfileAtStation(p, station);
  if (!sample.hasValue) {
    log.push_back("PROFILEQUERY - profile \"" + p.name + "\" at " + Sta(st, *a, station) + ": no value (" + sample.reason + ").");
    return;
  }
  alignment::DesignPoint pt;
  if (!alignment::DesignPathPoint(*a, p, station, &pt, &why)) {
    log.push_back("PROFILEQUERY - profile \"" + p.name + "\" at " + Sta(st, *a, station) + ": " + why + ".");
    return;
  }
  log.push_back("PROFILEQUERY - profile \"" + p.name + "\" at " + Sta(st, *a, station) + ": elevation " +
                Lin(st, sample.elevation) + ", grade " + GradePercent(sample.grade) + ". Design point (" + Lin(st, pt.x) +
                ", " + Lin(st, pt.y) + ", " + Lin(st, pt.z) + ").");
}

// PROFILELIST (REQ-521.7): every profile, its alignment and its PVI count. A profile whose alignment is gone says so.
void RunProfileList(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  if (!Trim(args).empty()) {
    log.push_back("PROFILELIST - usage: PROFILELIST");
    return;
  }
  if (st.profiles.empty()) {
    log.push_back("PROFILELIST - no profiles in this drawing.");
    return;
  }
  for (const alignment::VerticalProfile& p : st.profiles) {
    const alignment::Alignment* a = AlignmentById(st, p.alignmentId);
    std::string line = "PROFILELIST - \"" + p.name + "\" on " +
                       (a != nullptr ? "\"" + a->name + "\"" : std::string("alignment missing")) + ", " +
                       std::to_string(p.points.size()) + " PVI(s)";
    const std::string refusal = alignment::ProfileGeometryRefusal(p);
    if (!refusal.empty()) line += "; geometry not valid: " + refusal;
    log.push_back(line + ".");
  }
}

// PROFILEDELETE <profile> (REQ-521.6): deletes the profile. One undo step.
void RunProfileDelete(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitFields(args);
  if (f.size() != 1) {
    log.push_back("PROFILEDELETE - usage: PROFILEDELETE <profile>");
    return;
  }
  if (ActiveProjectPointsReadOnly(st)) {
    log.push_back("PROFILEDELETE - the project is read-only; no profile was deleted.");
    return;
  }
  const int pi = FindProfileIndex(st, f[0]);
  if (pi < 0) {
    log.push_back("PROFILEDELETE - no profile named \"" + Trim(f[0]) + "\".");
    return;
  }
  const std::string name = st.profiles[static_cast<size_t>(pi)].name;
  PushUndoSnapshot(st, "Delete design profile");
  st.profiles.erase(st.profiles.begin() + pi);
  log.push_back("PROFILEDELETE - profile \"" + name + "\" deleted.");
}

// GETSTATIONOFFSET <alignment>, <x>, <y>[, SURFACE=<surface>]  (WORLD x/y) - REQ-484.4
void RunGetStationOffset(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  std::vector<std::string> f = SplitFields(args);
  std::string surfaceName;  // REQ-484.4 / REQ-497: the ground elevation at the point, from a chosen surface
  if (!f.empty() && Lower(f.back()).rfind("surface=", 0) == 0) {
    surfaceName = Trim(f.back().substr(8));
    f.pop_back();
  }
  double x = 0.0, y = 0.0;
  if (f.size() == 1 && !f[0].empty()) {  // REQ-484.4 pick: each click reports, until ESC
    const alignment::Alignment* a = Resolve(st, "GETSTATIONOFFSET", f[0], log);
    if (a == nullptr) return;
    if (st.active != AppCommandState::Kind::None) {
      log.push_back("GETSTATIONOFFSET - finish or cancel the active command first.");
      return;
    }
    st.active = AppCommandState::Kind::StationOffsetPick;
    st.stationOffsetPickArgs = a->name + (surfaceName.empty() ? std::string{} : ", SURFACE=" + surfaceName);
    log.push_back("GETSTATIONOFFSET - pick a point (or type X,Y) to report its station and offset on \"" + a->name +
                  "\". ESC ends.");
    return;
  }
  if (f.size() == 3 && Lower(f[1]) == "point") {  // REQ-501.3: a survey point, by its point number
    double number = 0.0;
    if (!ParseNum(f[2], &number)) {
      log.push_back("GETSTATIONOFFSET - the point number must be a number.");
      return;
    }
    const auto point = std::find_if(st.surveyPoints.begin(), st.surveyPoints.end(),
                                    [&](const SurveyPoint& p) { return p.id == static_cast<int>(number); });
    if (point == st.surveyPoints.end()) {
      log.push_back("GETSTATIONOFFSET - no survey point numbered " + f[2] + ".");
      return;
    }
    x = point->easting;  // the survey point's coordinates are world, and so is the query
    y = point->northing;
  } else if (f.size() != 3 || !ParseNum(f[1], &x) || !ParseNum(f[2], &y)) {
    log.push_back("GETSTATIONOFFSET - usage: GETSTATIONOFFSET <alignment>[, <x>, <y> | , POINT, <number>]"
                  " [, SURFACE=<surface>] (the alignment alone picks points until ESC).");
    return;
  }

  alignment::Alignment* a = Resolve(st, "GETSTATIONOFFSET", f[0], log);
  if (a == nullptr) return;
  if (a->Empty()) {
    log.push_back("GETSTATIONOFFSET - \"" + a->name + "\" has no geometry.");
    return;
  }
  double lx = 0.0, ly = 0.0;
  CadCoord::LocalFromWorld(st, x, y, &lx, &ly);
  const alignment::StationOffset so = a->GetStationOffset(lx, ly);
  std::string line = "GETSTATIONOFFSET - \"" + a->name + "\": Station = " + Sta(st, *a, so.internalStation) +
                     "  Offset = " + Lin(st, so.offset) + (so.offset < 0.0 ? " (left)" : so.offset > 0.0 ? " (right)" : "") +
                     "  Bearing = " + Brg(st, so.azimuth) + ".";
  if (so.outOfRange) line += " The point lies beyond the end of the alignment; the nearest end is reported.";
  if (so.ambiguous) line += " Another location is equally close; the lowest station is reported.";
  if (surfaceName.empty()) {
    line += " Elevation: no profile.";  // REQ-500: profiles are a separate feature
  } else {
    const int si = FindSurfaceIndex(st, surfaceName);
    std::unique_ptr<ISurfaceQuery> ground =
        si < 0 ? nullptr : MakeSurfaceQueryForCommands(st, st.cadSurfaces[static_cast<size_t>(si)]);
    double z = 0.0;
    if (si < 0) line += " There is no surface named \"" + surfaceName + "\".";
    else if (!ground) line += " \"" + surfaceName + "\" is not a built surface.";
    else if (ground->elevationAt(lx, ly, &z)) line += " Elevation = " + Lin(st, z) + " (surface \"" + surfaceName + "\").";
    else line += " Elevation: the point is off the surface \"" + surfaceName + "\".";
  }
  log.push_back(line);
}

void CommitStationOffsetPick(AppCommandState& st, double lx, double ly, std::vector<std::string>& log) {
  assert(st.active == AppCommandState::Kind::StationOffsetPick);
  assert(std::isfinite(lx) && std::isfinite(ly));
  double wx = 0.0, wy = 0.0;
  CadCoord::WorldFromLocal(st, static_cast<float>(lx), static_cast<float>(ly), &wx, &wy);
  std::array<char, 64> sx{}, sy{};
  std::snprintf(sx.data(), sx.size(), "%.17g", wx);
  std::snprintf(sy.data(), sy.size(), "%.17g", wy);
  const std::vector<std::string> f = SplitFields(st.stationOffsetPickArgs);
  std::string args = f.empty() ? std::string{} : f[0];
  args += std::string(", ") + sx.data() + ", " + sy.data();
  for (size_t i = 1; i < f.size(); ++i) args += ", " + f[i];
  RunGetStationOffset(st, args, log);  // the command stays active for the next pick
}

// GETPOINTFROMSTATIONOFFSET <alignment>, <station>, <offset>  - REQ-484.4
void RunGetPointFromStationOffset(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitFields(args);
  double displayed = 0.0, offset = 0.0;
  if (f.size() != 3 || !ParseSta(st, f[1], &displayed) || !ParseNum(f[2], &offset)) {
    log.push_back("GETPOINTFROMSTATIONOFFSET - usage: GETPOINTFROMSTATIONOFFSET <alignment>, <station>, <offset> (station like 12+45.32).");
    return;
  }
  alignment::Alignment* a = Resolve(st, "GETPOINTFROMSTATIONOFFSET", f[0], log);
  if (a == nullptr) return;
  if (a->Empty()) {
    log.push_back("GETPOINTFROMSTATIONOFFSET - \"" + a->name + "\" has no geometry.");
    return;
  }
  const std::vector<double> raws = a->RawStationsForDisplayed(displayed);
  if (raws.empty()) {
    log.push_back("GETPOINTFROMSTATIONOFFSET - station " + StaFmt(st, displayed) + " is outside \"" + a->name +
                  "\" (" + Sta(st, *a, a->StartStation()) + " to " + StaFmt(st, a->EndStationWithEquations()) + ").");
    return;
  }
  for (const double raw : raws) {
    double az = 0.0;
    const alignment::Point2 p = a->GetPointFromStationOffset(raw, offset, &az);
    log.push_back("GETPOINTFROMSTATIONOFFSET - \"" + a->name + "\": X = " + Lin(st, p.x + CadCoord::WorldOriginX(st)) +
                  "  Y = " + Lin(st, p.y + CadCoord::WorldOriginY(st)) + "  Bearing = " + Brg(st, az) +
                  (raws.size() > 1 ? "  (station occurs " + std::to_string(raws.size()) + " times; this is the one at internal station " + Lin(st, raw) + ")" : ""));
  }
}

namespace {

/// "<name>  STA 25+42.35  OFF -12.40  BRG <bearing>": the cursor's place on one alignment (REQ-501.2).
std::string ReadoutText(const AppCommandState& st, const alignment::Alignment& a, const alignment::StationOffset& so) {
  assert(!a.Empty());
  assert(std::isfinite(so.internalStation));
  return a.name + "  STA " + StaFmt(st, a.DisplayedStation(so.internalStation, true)) + "  OFF " +
         FormatLinear(so.offset, st.displayLinearPrecision) + "  BRG " + Brg(st, so.azimuth);
}

}  // namespace

std::string AlignmentCursorReadout(const AppCommandState& st, double lx, double ly, double maxOffset) {
  // REQ-487.1: while a grip drags an alignment, the readout follows the cursor on that alignment as it was grabbed,
  // however far away, so the station, offset and bearing say where the grip is being taken.
  if (st.entityGripMoveActive && st.entityGripType == SelectedEntity::Type::Alignment &&
      !st.entityGripOrigAlignment.Empty()) {
    const alignment::Alignment& g = st.entityGripOrigAlignment;
    return ReadoutText(st, g, g.GetStationOffset(lx, ly));
  }
  const alignment::Alignment* best = nullptr;
  alignment::StationOffset bestSo;
  double bestAbs = maxOffset;
  for (const alignment::Alignment& a : st.alignments) {
    if (a.Empty()) continue;
    const alignment::StationOffset so = a.GetStationOffset(lx, ly);
    if (std::fabs(so.offset) <= bestAbs) {
      bestAbs = std::fabs(so.offset);
      best = &a;
      bestSo = so;
    }
  }
  if (best == nullptr) return {};
  return ReadoutText(st, *best, bestSo);
}

bool ResolveAlignmentStationSnapPoint(const AppCommandState& st, double displayed, float preferLocalX,
                                      float preferLocalY, float* outLx, float* outLy) {
  if (outLx == nullptr || outLy == nullptr)
    return false;
  const alignment::Alignment* bestAl = nullptr;
  alignment::Point2 bestPt{};
  double bestDist2 = std::numeric_limits<double>::infinity();
  for (const alignment::Alignment& a : st.alignments) {
    if (a.Empty())
      continue;
    const std::vector<double> raws = a.RawStationsForDisplayed(displayed);
    for (const double raw : raws) {
      const alignment::Point2 p = a.GetPointAtStation(raw);
      float lx = 0.f;
      float ly = 0.f;
      double wx = 0.0;
      double wy = 0.0;
      CadCoord::WorldFromLocal(st, p.x, p.y, &wx, &wy);
      CadCoord::LocalFromWorld(st, wx, wy, &lx, &ly);
      const double dx = static_cast<double>(lx) - static_cast<double>(preferLocalX);
      const double dy = static_cast<double>(ly) - static_cast<double>(preferLocalY);
      const double d2 = dx * dx + dy * dy;
      if (bestAl == nullptr || d2 < bestDist2) {
        bestAl = &a;
        bestPt = p;
        bestDist2 = d2;
      }
    }
  }
  if (bestAl == nullptr)
    return false;
  double wx = 0.0;
  double wy = 0.0;
  CadCoord::WorldFromLocal(st, bestPt.x, bestPt.y, &wx, &wy);
  CadCoord::LocalFromWorld(st, wx, wy, outLx, outLy);
  return std::isfinite(*outLx) && std::isfinite(*outLy);
}

bool LooksLikeAlignmentStationSnapInput(const std::string& raw) {
  const std::string trimmed = StringUtil::trimCopy(raw);
  if (trimmed.size() < 7)
    return false;
  const std::string low = Lower(trimmed);
  return low.rfind("station", 0) == 0;
}

bool TryParseAlignmentStationSnapStoragePoint(AppCommandState& st, const std::string& raw, float* lx, float* ly,
                                              double* outWorldZ, bool /*allowRelative*/, float baseLocalX,
                                              float baseLocalY) {
  st.alignmentStationSnapRejectReason.clear();
  if (!LooksLikeAlignmentStationSnapInput(raw))
    return false;
  if (lx == nullptr || ly == nullptr) {
    st.alignmentStationSnapRejectReason = "STATION snap â€” internal error (null output).";
    return false;
  }
  std::string trimmed = StringUtil::trimCopy(raw);
  const std::string low = Lower(trimmed);
  if (low.size() < 7) {
    st.alignmentStationSnapRejectReason = "STATION snap â€” usage: STATION 12+45.32 (displayed station on an alignment).";
    return false;
  }
  size_t i = 7;
  while (i < trimmed.size() && std::isspace(static_cast<unsigned char>(trimmed[i])) != 0)
    ++i;
  if (i < trimmed.size() && (trimmed[i] == ',' || trimmed[i] == '='))
    ++i;
  while (i < trimmed.size() && std::isspace(static_cast<unsigned char>(trimmed[i])) != 0)
    ++i;
  const std::string staText = StringUtil::trimCopy(trimmed.substr(i));
  if (staText.empty()) {
    st.alignmentStationSnapRejectReason = "STATION snap â€” missing station value (try 12+45.32).";
    return false;
  }
  double displayed = 0.0;
  if (!ParseSta(st, staText, &displayed)) {
    st.alignmentStationSnapRejectReason = "STATION snap â€” \"" + staText + "\" is not a station.";
    return false;
  }
  if (!ResolveAlignmentStationSnapPoint(st, displayed, baseLocalX, baseLocalY, lx, ly)) {
    st.alignmentStationSnapRejectReason =
        "STATION snap â€” station " + StaFmt(st, displayed) + " is not on any alignment.";
    return false;
  }
  st.objectSnapStationDisplayedValid = true;
  st.objectSnapStationDisplayed = displayed;
  if (outWorldZ != nullptr)
    *outWorldZ = static_cast<double>(CadWorkPlaneElevation(st));
  st.resolvedPointZValid = true;
  st.resolvedPointZ = static_cast<float>(CadWorkPlaneElevation(st));
  st.resolvedPointZTyped = false;
  st.viewportSnapPickValid = false;
  return true;
}

// ======================================================================================================
// Phase 2 (REQ-486..489): editing, constraints, station control, reverse, offsets, intersections.
// ======================================================================================================

namespace {

alignment::Alignment* FindById(AppCommandState& st, std::uint64_t id) {
  for (alignment::Alignment& a : st.alignments) {
    if (a.id == id) return &a;
  }
  return nullptr;
}

/// REQ-488.3: a linked offset follows its parent. Called after anything that changes a parent. A parent that no
/// longer exists detaches the offset (it keeps its geometry); an offset that can no longer be built keeps its last
/// geometry and says why.
void RefreshLinkedOffsetsImpl(AppCommandState& st, std::vector<std::string>& log) {
  for (alignment::Alignment& off : st.alignments) {
    if (off.parentId == 0) continue;
    alignment::Alignment* parent = FindById(st, off.parentId);
    if (parent == nullptr) {
      off.parentId = 0;
      log.push_back("  Offset \"" + off.name + "\" lost its parent and is now independent.");
      continue;
    }
    alignment::Alignment rebuilt = off;
    std::string why;
    alignment::Alignment source = *parent;
    const bool ranged = off.HasOffsetRange();
    if ((!ranged || parent->ExtractRange(off.offsetFromStation, off.offsetToStation, &source, &why)) &&
        alignment::MakeOffset(source, off.offsetDistance, &rebuilt, &why)) {
      rebuilt.startingStation = off.startingStation;
      rebuilt.parentId = off.parentId;  // an extracted range carries no id of its own
      rebuilt.offsetFromStation = off.offsetFromStation;
      rebuilt.offsetToStation = off.offsetToStation;
      off = std::move(rebuilt);
    } else {
      log.push_back("  Offset \"" + off.name + "\" could not follow \"" + parent->name + "\": " + why + ".");
    }
  }
}

/// Parses "<entity#>" (1-based) into a 0-based index within `a`, or reports.
bool ParseEntityIndex(const alignment::Alignment& a, const std::string& cmd, const std::string& text, int* out,
                      std::vector<std::string>& log) {
  double v = 0.0;
  if (!ParseNum(text, &v) || v != std::floor(v) || v < 1.0 || v > static_cast<double>(a.Entities().size())) {
    log.push_back(cmd + " - \"" + text + "\" is not an entity number (1 to " + std::to_string(a.Entities().size()) + ").");
    return false;
  }
  *out = static_cast<int>(v) - 1;
  return true;
}

bool IsLinkedOffset(const alignment::Alignment& a, const std::string& cmd, std::vector<std::string>& log) {
  if (a.parentId == 0) return false;
  log.push_back(cmd + " - \"" + a.name + "\" is an offset linked to its parent; edit the parent, or ALIGNMENTDETACH it first.");
  return true;
}

/// REQ-483.5: equations a geometry edit left beyond the new end are listed by name; UNDO brings them back.
void ReportDroppedEquations(const AppCommandState& st, const alignment::Alignment& before, const alignment::Alignment& after,
                            std::vector<std::string>& log) {
  assert(after.equations.size() <= before.equations.size() + after.equations.size());
  assert(before.equations.size() < 100000);
  for (const alignment::StationEquation& q : before.equations) {
    const bool kept = std::any_of(after.equations.begin(), after.equations.end(),
                                  [&](const alignment::StationEquation& o) { return o.rawBack == q.rawBack; });
    if (!kept)
      log.push_back("  Station equation at raw station " + StaFmt(st, q.rawBack) + " (ahead " + StaFmt(st, q.ahead) +
                    ") now lies beyond the end of \"" + after.name + "\" and was removed; UNDO restores it.");
  }
}

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

// ======================================================================================================
// Phase 4 (REQ-492.3, REQ-493): segment labelling and drawing tables.
// ======================================================================================================

namespace {

const char* TableKindLabel(alignment::AlignmentTableKind k) {
  switch (k) {
    case alignment::AlignmentTableKind::Line: return "Line";
    case alignment::AlignmentTableKind::Curve: return "Curve";
    case alignment::AlignmentTableKind::Spiral: return "Spiral";
    case alignment::AlignmentTableKind::Segment: return "Segment";
    case alignment::AlignmentTableKind::StationEquation: return "Station equation";
  }
  return "?";
}

bool ParseTableKind(const std::string& word, alignment::AlignmentTableKind* k) {
  if (word == "line") *k = alignment::AlignmentTableKind::Line;
  else if (word == "curve") *k = alignment::AlignmentTableKind::Curve;
  else if (word == "spiral") *k = alignment::AlignmentTableKind::Spiral;
  else if (word == "segment" || word == "segments") *k = alignment::AlignmentTableKind::Segment;
  else if (word == "equation" || word == "equations") *k = alignment::AlignmentTableKind::StationEquation;
  else return false;
  return true;
}

/// REQ-493.2: rewrites every DYNAMIC table from its alignment's current geometry. A static table is a frozen snapshot and
/// is never touched. A table whose alignment can no longer be tabulated (labels removed) keeps its last text and says so.
void RefreshDynamicTables(AppCommandState& st, std::vector<std::string>& log) {
  bool any = false;
  for (const alignment::Alignment& a : st.alignments) {
    for (const alignment::AlignmentTableLink& link : a.tables) {
      if (!link.dynamic) continue;
      const int ti = FindEntityIndexById(st.cadTableAttrs, link.tableId);
      if (ti < 0 || static_cast<size_t>(ti) >= st.cadTables.size()) continue;
      alignment::AlignmentTableText text;
      std::string why;
      if (!alignment::BuildAlignmentTableText(a, link.kind, st.displayLinearPrecision, CadAngleDisplaySettings(st), &text, &why, st.alignmentStationFormat)) {
        log.push_back("  Table for \"" + a.name + "\" was not updated: " + why + ".");
        continue;
      }
      if (link.firstRow != 0 || link.rowCount != 0) text = alignment::SliceTableRows(text, link.firstRow, link.rowCount);
      CadTable& tbl = st.cadTables[static_cast<size_t>(ti)];
      tbl.cols = text.cols;
      tbl.cells = text.cells;
      CadTableFitToContent(&tbl, st.modelUnitsPerPlottedInch);
      any = true;
    }
  }
  if (any) BumpCadGpuCache(st);
}

}  // namespace

namespace {

// ALIGNMENTLABEL <name>, SEGMENT, <entity#>: REQ-492.3 Single Segment label on one picked segment.
void RunAlignmentSingleSegmentLabel(AppCommandState& st, const std::vector<std::string>& f, std::vector<std::string>& log) {
  assert(f.size() == 3);
  assert(st.alignments.size() < 100000000);
  alignment::Alignment* a = Resolve(st, "ALIGNMENTLABEL", f[0], log);
  if (a == nullptr) return;
  double n = 0.0;
  const int count = static_cast<int>(a->Entities().size());
  if (!ParseNum(f[2], &n) || n != std::floor(n) || n < 1.0 || n > static_cast<double>(count)) {
    log.push_back("ALIGNMENTLABEL - \"" + a->name + "\" has entities 1 to " + std::to_string(count) + "; \"" + f[2] +
                  "\" is not one of them.");
    return;
  }
  const int i = static_cast<int>(n) - 1;
  if (alignment::SegmentIsLabelled(*a, i)) {
    log.push_back("ALIGNMENTLABEL - entity " + std::to_string(i + 1) + " of \"" + a->name + "\" is already labelled.");
    return;
  }
  PushUndoSnapshot(st, "Label alignment segment");
  a = Resolve(st, "ALIGNMENTLABEL", f[0], log);
  a->segmentLabelStations.push_back(0.5 * (a->EntityStartStation(i) + a->EntityEndStation(i)));
  BumpCadGpuCache(st);
  log.push_back("ALIGNMENTLABEL - entity " + std::to_string(i + 1) + " of \"" + a->name + "\" labelled.");
}

}  // namespace

// ALIGNMENTLABEL <name>, SEGMENTS | SEGMENT, <entity#> | CLEAR   (REQ-492.3 Multiple and Single Segment labels;
// REQ-493.3: a line, curve, spiral or segment table lists the labelled segments)
void RunAlignmentStationLabel(AppCommandState& st, const std::string& args, std::vector<std::string>& log);

void RunAlignmentLabel(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitFields(args);
  if (f.size() >= 2 && Lower(f[1]) == "stationlabel") {  // REQ-492.4
    RunAlignmentStationLabel(st, args, log);
    return;
  }
  const char* usage = "ALIGNMENTLABEL - usage: ALIGNMENTLABEL <name>, SEGMENTS | SEGMENT, <entity#> | CLEAR.";
  const std::string what = f.size() >= 2 ? Lower(f[1]) : std::string{};
  if (what == "segment" && f.size() == 3) {
    RunAlignmentSingleSegmentLabel(st, f, log);
    return;
  }
  if (f.size() != 2 || (what != "segments" && what != "clear")) {
    log.push_back(usage);
    return;
  }
  alignment::Alignment* a = Resolve(st, "ALIGNMENTLABEL", f[0], log);
  if (a == nullptr) return;
  const bool on = what == "segments";
  if (on && a->Empty()) {
    log.push_back("ALIGNMENTLABEL - \"" + a->name + "\" has no geometry to label.");
    return;
  }
  if (a->segmentLabels == on && (on || a->segmentLabelStations.empty())) {
    log.push_back("ALIGNMENTLABEL - the segments of \"" + a->name + "\" are already " + (on ? "labelled." : "unlabelled."));
    return;
  }
  PushUndoSnapshot(st, on ? "Label alignment segments" : "Clear alignment segment labels");
  a = Resolve(st, "ALIGNMENTLABEL", f[0], log);
  a->segmentLabels = on;
  a->segmentLabelStations.clear();  // Multiple Segment covers every segment; CLEAR removes the single ones too
  log.push_back(on ? "ALIGNMENTLABEL - segments of \"" + a->name + "\" labelled; line, curve and segment tables can now be made."
                   : "ALIGNMENTLABEL - segment labels of \"" + a->name + "\" cleared. Existing line and curve tables stop updating.");
}

namespace {

struct AlignmentTableOptions {
  bool dynamic = true;
  int splitRows = 0;  ///< 0 = one table
  std::string style;
  bool hasPoint = false;
  double wx = 0.0, wy = 0.0;
};

/// Reads `[STATIC|DYNAMIC][, SPLIT=<rows>][, STYLE=<name>][, <x>, <y>]` in any order. False (with a message) on bad input.
bool ParseAlignmentTableOptions(const std::vector<std::string>& f, AlignmentTableOptions* o, std::vector<std::string>& log) {
  assert(o != nullptr);
  assert(f.size() >= 2);
  std::vector<std::string> numbers;
  for (size_t i = 2; i < f.size(); ++i) {
    const std::string w = Lower(f[i]);
    if (w == "static" || w == "dynamic") {
      o->dynamic = w == "dynamic";
    } else if (w.rfind("split=", 0) == 0) {
      double rows = 0.0;
      if (!ParseNum(f[i].substr(6), &rows) || rows < 1.0 || rows > 10000.0 || rows != std::floor(rows)) {
        log.push_back("ALIGNMENTTABLE - SPLIT= needs a whole number of data rows per table, 1 or more.");
        return false;
      }
      o->splitRows = static_cast<int>(rows);
    } else if (w.rfind("style=", 0) == 0) {
      o->style = f[i].substr(6);
    } else {
      numbers.push_back(f[i]);
    }
  }
  if (numbers.empty()) return true;
  if (numbers.size() != 2 || !ParseNum(numbers[0], &o->wx) || !ParseNum(numbers[1], &o->wy)) {
    log.push_back("ALIGNMENTTABLE - the insertion point must be two numbers.");
    return false;
  }
  o->hasPoint = true;
  return true;
}

/// The data table style a new alignment table uses: STYLE= when given, else the current table style, else Standard
/// (REQ-493.1: the table style is a REQ-478 table style). Null when STYLE= names no data table style.
const CadTableStyle* AlignmentTableStyle(const AppCommandState& st, const std::string& requested) {
  assert(st.tableStyles.size() < 100000);
  assert(requested.size() < 4096);
  if (!requested.empty()) {
    const CadTableStyle* s = CadTableFindStyle(st.tableStyles, requested);
    return s != nullptr && !s->point ? s : nullptr;
  }
  const CadTableStyle* current = CadTableFindStyle(st.tableStyles, st.tablePendingStyle);
  return current != nullptr && !current->point ? current : CadTableFindStyle(st.tableStyles, "Standard");
}

/// Adds one drawing TABLE holding `text` with its top-left at drawing-local (lx, ly); returns its stable id.
std::uint64_t InsertAlignmentCadTable(AppCommandState& st, const alignment::AlignmentTableText& text,
                                      const CadTableStyle* style, double lx, double ly, float* width) {
  assert(text.cols > 0);
  assert(width != nullptr);
  CadTable tbl;
  tbl.cols = text.cols;
  tbl.cells = text.cells;
  if (const TextStyle* ts = ActiveTextStyle(st)) tbl.fontFamily = ts->fontFamily;
  tbl.plottedHeightInches = st.defaultPlottedTextHeightInches;
  if (style != nullptr) {
    tbl.styleName = style->name;
    tbl.plottedHeightInches = style->textHeightInches;
    if (const TextStyle* ts = TextStyles::Find(st.textStyles, style->textStyleName)) tbl.fontFamily = ts->fontFamily;
  }
  CadTableFitToContent(&tbl, st.modelUnitsPerPlottedInch);
  tbl.insX = static_cast<float>(lx);
  tbl.insY = static_cast<float>(ly) + tbl.height;  // top-left, body below it, as VOLTABLE places its table
  tbl.insZ = CadCommitElevation(st);
  *width = tbl.width;
  st.cadTables.push_back(std::move(tbl));
  AppendNewObjectAttrs(st, st.cadTableAttrs, st.cadTables.size(), ObjectLayerKind::Table, {});  // REQ-361
  EnsureAttrCounts(st);
  st.cadTableAttrs.back().id = st.nextEntityId++;  // the link needs a stable id now, not at the next sweep (REQ-076)
  return st.cadTableAttrs.back().id;
}

}  // namespace

// ALIGNMENTTABLE <name>, LINE|CURVE|SPIRAL|SEGMENT|EQUATIONS[, STATIC|DYNAMIC][, SPLIT=<rows>][, STYLE=<name>][, <x>, <y>]
// (REQ-493.1 / .2). Inserts a drawing TABLE at the world point (top-left), default the drawing origin. DYNAMIC (the
// default) rebuilds the table whenever the alignment changes; STATIC is a frozen snapshot. SPLIT= puts at most that many
// data rows in each table and sets the parts side by side, each with the header, as Civil 3D's Split Table does.
void RunAlignmentTable(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitFields(args);
  const char* usage = "ALIGNMENTTABLE - usage: ALIGNMENTTABLE <name>, LINE|CURVE|SPIRAL|SEGMENT|EQUATIONS[, STATIC|DYNAMIC]"
                      "[, SPLIT=<rows>][, STYLE=<name>][, <x>, <y>].";
  alignment::AlignmentTableKind kind{};
  if (f.size() < 2 || !ParseTableKind(Lower(f[1]), &kind)) {
    log.push_back(usage);
    return;
  }
  alignment::Alignment* a = Resolve(st, "ALIGNMENTTABLE", f[0], log);
  AlignmentTableOptions opt;
  if (a == nullptr || !ParseAlignmentTableOptions(f, &opt, log)) return;
  const CadTableStyle* style = AlignmentTableStyle(st, opt.style);
  if (!opt.style.empty() && style == nullptr) {
    log.push_back("ALIGNMENTTABLE - no data table style named \"" + opt.style + "\". No table was made.");
    return;
  }
  alignment::AlignmentTableText text;
  std::string why;
  if (!alignment::BuildAlignmentTableText(*a, kind, st.displayLinearPrecision, CadAngleDisplaySettings(st), &text, &why, st.alignmentStationFormat)) {
    log.push_back("ALIGNMENTTABLE - " + why + ". No table was made.");
    return;
  }
  const int rows = static_cast<int>(text.cells.size() / static_cast<size_t>(text.cols)) - 1;
  const int parts = opt.splitRows > 0 ? std::max(1, (rows + opt.splitRows - 1) / opt.splitRows) : 1;
  PushUndoSnapshot(st, "Insert alignment table");
  a = Resolve(st, "ALIGNMENTTABLE", f[0], log);
  double lx = 0.0, ly = 0.0;
  if (opt.hasPoint) CadCoord::LocalFromWorld(st, opt.wx, opt.wy, &lx, &ly);
  for (int part = 0; part < parts && part < 10000; ++part) {
    const int first = part * opt.splitRows;
    const int count = part + 1 < parts ? opt.splitRows : 0;  // the last part takes the rest, so it can grow
    float width = 0.0f;
    const alignment::AlignmentTableText slice = parts > 1 ? alignment::SliceTableRows(text, first, count) : text;
    const std::uint64_t id = InsertAlignmentCadTable(st, slice, style, lx, ly, &width);
    a->tables.push_back({id, kind, opt.dynamic, parts > 1 ? first : 0, parts > 1 ? count : 0});
    lx += static_cast<double>(width) * 1.05;  // Civil 3D tiles split parts across, a small gap apart
  }
  BumpCadGpuCache(st);
  log.push_back("ALIGNMENTTABLE - " + std::string(TableKindLabel(kind)) + " table for \"" + a->name + "\" inserted (" +
                (opt.dynamic ? "dynamic" : "static") + ", " + std::to_string(rows) + " row(s)" +
                (parts > 1 ? " in " + std::to_string(parts) + " tables" : std::string()) + ", style " +
                (style != nullptr ? style->name : std::string("Standard")) + ").");
}

void RefreshAlignmentOffsetsOnly(AppCommandState& st, std::vector<std::string>& log) {
  RefreshLinkedOffsetsImpl(st, log);
  RefreshDynamicTables(st, log);  // REQ-493.2: dynamic tables follow the geometry
  RefreshViewFrameGroups(st, log);  // REQ-517.8: view frame groups regenerate with the alignment
}

void RefreshAlignmentOffsets(AppCommandState& st, std::vector<std::string>& log) {
  RefreshAlignmentOffsetsOnly(st, log);
  RefreshFeatureLineLinks(st, log);  // REQ-498.2: feature lines made from an alignment follow it
  for (const alignment::Alignment& alignment : st.alignments)
    RefreshSurfaceProfilesForAlignment(st, alignment.id, log);
}

// ALIGNMENTEDIT <name>, <entity#>, BEARING|LENGTH|RADIUS|DELTA|DIRECTION|CONSTRAINT, <value>
void RunAlignmentEdit(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitFields(args);
  const char* usage =
      "ALIGNMENTEDIT - usage: ALIGNMENTEDIT <name>, <entity#>, BEARING|LENGTH|RADIUS|STARTRADIUS|DELTA|DIRECTION|CONSTRAINT|"
      "DEGREE|TANGENTLENGTH|CHORD|EXTERNAL|MIDORDINATE|A, <value> | <name>, <entity#>, THROUGH, <x>, <y> "
      "(DIRECTION LEFT|RIGHT; CONSTRAINT FIXED|FLOATPREV|FLOATNEXT|FREE; spiral RADIUS/STARTRADIUS 0 = tangent).";
  const bool through = f.size() == 5 && Lower(f[2]) == "through";
  if (f.size() != 4 && !through) {
    log.push_back(usage);
    return;
  }
  alignment::Alignment* a = Resolve(st, "ALIGNMENTEDIT", f[0], log);
  if (a == nullptr || IsLinkedOffset(*a, "ALIGNMENTEDIT", log)) return;
  int idx = 0;
  if (!ParseEntityIndex(*a, "ALIGNMENTEDIT", f[1], &idx, log)) return;
  if (through) {  // REQ-481.5a Curve through a point
    double wx = 0.0, wy = 0.0, lx = 0.0, ly = 0.0;
    if (!ParseNum(f[3], &wx) || !ParseNum(f[4], &wy)) {
      log.push_back("ALIGNMENTEDIT - THROUGH needs the point as two numbers, <x>, <y>.");
      return;
    }
    CadCoord::LocalFromWorld(st, wx, wy, &lx, &ly);
    alignment::Alignment trial = *a;
    std::string why;
    if (!trial.EditCurveThroughPoint(idx, {lx, ly}, &why)) {
      log.push_back("ALIGNMENTEDIT - refused: " + why + ". Nothing changed.");
      return;
    }
    PushUndoSnapshot(st, "Edit alignment");
    *Resolve(st, "ALIGNMENTEDIT", f[0], log) = std::move(trial);
    log.push_back("ALIGNMENTEDIT - entity " + f[1] + " of \"" + f[0] + "\" now passes through (" + f[3] + ", " + f[4] + ").");
    RefreshAlignmentOffsets(st, log);
    return;
  }
  const alignment::EntityKind kind = a->Entities()[static_cast<size_t>(idx)].kind;
  const bool isArc = kind == alignment::EntityKind::Arc;
  const bool isSpiral = kind == alignment::EntityKind::Spiral;
  const std::string what = Lower(f[2]);
  alignment::EditParam param{};
  double v = 0.0;
  if (what == "bearing" || what == "azimuth") {
    if (!ParseNum(f[3], &v)) { log.push_back("ALIGNMENTEDIT - the bearing must be a number of degrees (azimuth from north)."); return; }
    param = alignment::EditParam::LineAzimuth;
    v *= kDegToRad;
  } else if (what == "length") {
    if (!ParseNum(f[3], &v)) { log.push_back("ALIGNMENTEDIT - the length must be a number."); return; }
    param = (isArc || isSpiral) ? alignment::EditParam::CurveLength : alignment::EditParam::LineLength;
  } else if (what == "radius") {
    if (!ParseNum(f[3], &v)) { log.push_back("ALIGNMENTEDIT - the radius must be a number."); return; }
    param = alignment::EditParam::CurveRadius;
  } else if (what == "startradius" || what == "rs") {
    if (!ParseNum(f[3], &v)) { log.push_back("ALIGNMENTEDIT - the start radius must be a number (0 = tangent)."); return; }
    param = alignment::EditParam::SpiralStartRadius;
  } else if (what == "delta") {
    if (isSpiral) {
      log.push_back("ALIGNMENTEDIT - a spiral has no delta; edit LENGTH or RADIUS instead.");
      return;
    }
    if (!ParseNum(f[3], &v)) { log.push_back("ALIGNMENTEDIT - the delta must be a number of degrees."); return; }
    param = alignment::EditParam::CurveDelta;
    v *= kDegToRad;
  } else if (what == "direction") {
    const std::string d = Lower(f[3]);
    if (d != "left" && d != "right") { log.push_back("ALIGNMENTEDIT - the direction must be LEFT or RIGHT."); return; }
    param = alignment::EditParam::CurveDirection;
    v = d == "right" ? 1.0 : 0.0;
  } else if (what == "constraint") {
    const std::string c = Lower(f[3]);
    param = alignment::EditParam::Constraint;
    if (c == "fixed") v = 0.0;
    else if (c == "floatprev" || c == "floatonprev") v = 1.0;
    else if (c == "floatnext" || c == "floatonnext") v = 2.0;
    else if (c == "free") v = 3.0;
    else { log.push_back("ALIGNMENTEDIT - the constraint must be FIXED, FLOATPREV, FLOATNEXT or FREE."); return; }
  } else if (what == "degree" || what == "tangentlength" || what == "chord" || what == "external" ||
             what == "midordinate" || what == "middleordinate" || what == "a") {  // REQ-481.5a definitions
    if (!ParseNum(f[3], &v)) { log.push_back("ALIGNMENTEDIT - the " + f[2] + " must be a number."); return; }
    param = what == "degree"          ? alignment::EditParam::CurveDegree
            : what == "tangentlength" ? alignment::EditParam::CurveTangent
            : what == "chord"         ? alignment::EditParam::CurveChord
            : what == "external"      ? alignment::EditParam::CurveExternal
            : what == "a"             ? alignment::EditParam::SpiralA
                                      : alignment::EditParam::CurveMiddleOrdinate;
  } else {
    log.push_back(usage);
    return;
  }
  alignment::Alignment trial = *a;
  std::string why;
  if (!trial.EditEntity(idx, param, v, &why)) {
    log.push_back("ALIGNMENTEDIT - refused: " + why + ". Nothing changed.");
    return;
  }
  PushUndoSnapshot(st, "Edit alignment");
  ReportDroppedEquations(st, *Resolve(st, "ALIGNMENTEDIT", f[0], log), trial, log);
  alignment::Alignment* target = Resolve(st, "ALIGNMENTEDIT", f[0], log);
  *target = std::move(trial);
  log.push_back("ALIGNMENTEDIT - \"" + target->name + "\" entity " + std::to_string(idx + 1) + " updated; length now " + Lin(st, target->Length()) +
                ", ending at station " + Sta(st, *target, target->EndStation()) + ".");
  ReportContinuity(st, *target, log);
  RefreshAlignmentOffsets(st, log);
}

// ALIGNMENTLOCK <name>, <entity#>, RADIUS|LENGTH|BEARING|STATION, ON|OFF
void RunAlignmentLock(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitFields(args);
  if (f.size() != 4) {
    log.push_back("ALIGNMENTLOCK - usage: ALIGNMENTLOCK <name>, <entity#>, RADIUS|LENGTH|BEARING|STATION, ON|OFF.");
    return;
  }
  alignment::Alignment* a = Resolve(st, "ALIGNMENTLOCK", f[0], log);
  if (a == nullptr) return;
  int idx = 0;
  if (!ParseEntityIndex(*a, "ALIGNMENTLOCK", f[1], &idx, log)) return;
  const std::string what = Lower(f[2]), onoff = Lower(f[3]);
  unsigned char bit = 0;
  if (what == "radius") bit = alignment::Entity::kLockRadius;
  else if (what == "length") bit = alignment::Entity::kLockLength;
  else if (what == "bearing") bit = alignment::Entity::kLockBearing;
  else if (what == "station") bit = alignment::Entity::kLockStation;  // REQ-486.3: its start station stays put
  if (bit == 0 || (onoff != "on" && onoff != "off")) {
    log.push_back("ALIGNMENTLOCK - usage: ALIGNMENTLOCK <name>, <entity#>, RADIUS|LENGTH|BEARING|STATION, ON|OFF.");
    return;
  }
  unsigned char mask = a->Entities()[static_cast<size_t>(idx)].locks;
  mask = onoff == "on" ? static_cast<unsigned char>(mask | bit) : static_cast<unsigned char>(mask & ~bit);
  PushUndoSnapshot(st, "Lock alignment parameter");
  a = Resolve(st, "ALIGNMENTLOCK", f[0], log);
  std::string why;
  (void)a->EditEntity(idx, alignment::EditParam::Locks, static_cast<double>(mask), &why);
  log.push_back("ALIGNMENTLOCK - entity " + std::to_string(idx + 1) + " of \"" + a->name + "\": " + what + (onoff == "on" ? " locked." : " unlocked."));
}

// ALIGNMENTINSERT <name>, <after entity#>, TANGENT, <length> | CURVE, <radius>, <delta deg>, LEFT|RIGHT
//                 | SPIRAL, <length>, <end radius>, LEFT|RIGHT | SPIRAL, <length>, <start R>, <end R>, LEFT|RIGHT
void RunAlignmentInsert(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitFields(args);
  const char* usage =
      "ALIGNMENTINSERT - usage: ALIGNMENTINSERT <name>, <after entity#>, TANGENT, <length> | CURVE, <radius>, <delta deg>, "
      "LEFT|RIGHT | SPIRAL, <length>, <end radius>, LEFT|RIGHT | SPIRAL, <length>, <start radius>, <end radius>, LEFT|RIGHT.";
  if (f.size() < 4) {
    log.push_back(usage);
    return;
  }
  alignment::Alignment* a = Resolve(st, "ALIGNMENTINSERT", f[0], log);
  if (a == nullptr || IsLinkedOffset(*a, "ALIGNMENTINSERT", log)) return;
  int idx = 0;
  if (!ParseEntityIndex(*a, "ALIGNMENTINSERT", f[1], &idx, log)) return;
  alignment::Alignment trial = *a;
  std::string why;
  bool ok = false;
  double v1 = 0.0, v2 = 0.0;
  const std::string kind = Lower(f[2]);
  if (kind == "tangent" && f.size() == 4 && ParseNum(f[3], &v1)) {
    ok = trial.InsertTangentAfter(idx, v1, &why);
  } else if (kind == "curve" && f.size() == 6 && ParseNum(f[3], &v1) && ParseNum(f[4], &v2) &&
             (Lower(f[5]) == "left" || Lower(f[5]) == "right")) {
    ok = trial.InsertCurveAfter(idx, v1, v1 * v2 * kDegToRad, Lower(f[5]) == "right", &why);
  } else if (kind == "spiral" && (f.size() == 5 || f.size() == 7)) {
    const std::string side = Lower(f.back());
    if (side != "left" && side != "right") {
      log.push_back(usage);
      return;
    }
    double ls = 0.0, rs = 0.0, re = 0.0;
    if (f.size() == 5) {
      if (!ParseNum(f[3], &ls) || !ParseNum(f[4], &re)) {
        log.push_back(usage);
        return;
      }
    } else {
      if (!ParseNum(f[3], &ls) || !ParseNum(f[4], &rs) || !ParseNum(f[5], &re)) {
        log.push_back(usage);
        return;
      }
    }
    ok = trial.InsertSpiralAfter(idx, ls, rs, re, side == "right", &why);
  } else {
    log.push_back(usage);
    return;
  }
  if (!ok) {
    log.push_back("ALIGNMENTINSERT - refused: " + why + ". Nothing changed.");
    return;
  }
  PushUndoSnapshot(st, "Insert alignment entity");
  ReportDroppedEquations(st, *Resolve(st, "ALIGNMENTINSERT", f[0], log), trial, log);
  alignment::Alignment* target = Resolve(st, "ALIGNMENTINSERT", f[0], log);
  *target = std::move(trial);
  log.push_back("ALIGNMENTINSERT - \"" + target->name + "\" now has " + std::to_string(target->Entities().size()) + " entities, length " +
                Lin(st, target->Length()) + ".");
  RefreshAlignmentOffsets(st, log);
}

// ALIGNMENTREMOVE <name>, <entity#>
void RunAlignmentRemove(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitFields(args);
  if (f.size() != 2) {
    log.push_back("ALIGNMENTREMOVE - usage: ALIGNMENTREMOVE <name>, <entity#>.");
    return;
  }
  alignment::Alignment* a = Resolve(st, "ALIGNMENTREMOVE", f[0], log);
  if (a == nullptr || IsLinkedOffset(*a, "ALIGNMENTREMOVE", log)) return;
  int idx = 0;
  if (!ParseEntityIndex(*a, "ALIGNMENTREMOVE", f[1], &idx, log)) return;
  alignment::Alignment trial = *a;
  std::string why;
  if (!trial.DeleteEntity(idx, &why)) {
    log.push_back("ALIGNMENTREMOVE - refused: " + why + ". Nothing changed.");
    return;
  }
  PushUndoSnapshot(st, "Remove alignment entity");
  ReportDroppedEquations(st, *Resolve(st, "ALIGNMENTREMOVE", f[0], log), trial, log);
  alignment::Alignment* target = Resolve(st, "ALIGNMENTREMOVE", f[0], log);
  *target = std::move(trial);
  log.push_back("ALIGNMENTREMOVE - entity " + std::to_string(idx + 1) + " removed; \"" + target->name + "\" has " +
                std::to_string(target->Entities().size()) + " entities.");
  ReportContinuity(st, *target, log);
  RefreshAlignmentOffsets(st, log);
}

// ALIGNMENTCONTINUITY <name>, C0|C1|C2 | <name>, LIST   - REQ-482
void RunAlignmentContinuity(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitFields(args);
  if (f.size() != 2) {
    log.push_back("ALIGNMENTCONTINUITY - usage: ALIGNMENTCONTINUITY <name>, C0|C1|C2 | <name>, LIST.");
    return;
  }
  alignment::Alignment* a = Resolve(st, "ALIGNMENTCONTINUITY", f[0], log);
  if (a == nullptr) return;
  const std::string sub = Lower(f[1]);
  if (sub == "list") {
    log.push_back("ALIGNMENTCONTINUITY - \"" + a->name + "\" requires " +
                  std::string(alignment::ContinuityRequirementLabel(a->continuityRequirement)) + " at junctions.");
    int warnings = 0;
    for (const alignment::ContinuityIssue& c : a->CheckContinuity()) {
      if (c.FailsRequirement(a->continuityRequirement)) ++warnings;
    }
    ReportContinuity(st, *a, log);
    if (warnings == 0) log.push_back("  No continuity warnings at this level.");
    return;
  }
  alignment::ContinuityRequirement level = alignment::ContinuityRequirement::C1;
  if (!alignment::ParseContinuityRequirement(sub, &level)) {
    log.push_back("ALIGNMENTCONTINUITY - the level must be C0, C1 or C2.");
    return;
  }
  PushUndoSnapshot(st, "Set alignment continuity requirement");
  a = Resolve(st, "ALIGNMENTCONTINUITY", f[0], log);
  a->continuityRequirement = level;
  log.push_back("ALIGNMENTCONTINUITY - \"" + a->name + "\" now checks " +
                std::string(alignment::ContinuityRequirementLabel(level)) + " at junctions.");
  ReportContinuity(st, *a, log);
}

// REVERSEALIGNMENT <name>  (also REVERSEALIGNDIRECTION) - REQ-487.2
void RunReverseAlignment(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::string name = Trim(args);
  alignment::Alignment* a = Resolve(st, "REVERSEALIGNMENT", name, log);
  if (a == nullptr || IsLinkedOffset(*a, "REVERSEALIGNMENT", log)) return;
  alignment::Alignment trial = *a;
  std::string note;
  if (!trial.Reverse(&note)) {
    log.push_back("REVERSEALIGNMENT - refused: the alignment is empty or has a gap (it must be continuous). Nothing changed.");
    return;
  }
  PushUndoSnapshot(st, "Reverse alignment");
  alignment::Alignment* target = Resolve(st, "REVERSEALIGNMENT", name, log);
  *target = std::move(trial);
  const std::uint64_t parentId = target->id;
  log.push_back("REVERSEALIGNMENT - \"" + target->name + "\" now runs from " + Sta(st, *target, target->StartStation()) + " (was the end) to " +
                Sta(st, *target, target->EndStation()) + "." + (note.empty() ? "" : " " + note + "."));
  // REQ-487.2: a linked offset keeps its side on the ground; facing the other way, right becomes left.
  int followed = 0;
  for (alignment::Alignment& child : st.alignments) {
    if (child.parentId != parentId || parentId == 0) continue;
    child.offsetDistance = -child.offsetDistance;
    ++followed;
  }
  if (followed > 0) log.push_back("REVERSEALIGNMENT - " + std::to_string(followed) + " offset alignment(s) reversed with it.");
  RefreshAlignmentOffsets(st, log);
}

// ALIGNMENTSTATION <name>, START, <station>[, CLEAR]
//                  <name>, EQ, ADD, <raw station>, <ahead station>, INC|DEC
//                  <name>, EQ, REMOVE, <n>   |   <name>, EQ, LIST        - REQ-483, REQ-487.3
void RunAlignmentStation(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitFields(args);
  const char* usage =
      "ALIGNMENTSTATION - usage: ALIGNMENTSTATION <name>, START, <station>[, CLEAR] | <name>, EQ, ADD, <raw station>, <ahead station>, INC|DEC | "
      "<name>, EQ, EDIT, <n>, <raw station>, <ahead station>, INC|DEC | <name>, EQ, REMOVE, <n> | <name>, EQ, LIST | "
      "<name>, REFERENCE, <x>, <y>, <station>[, CLEAR].";
  if (f.size() < 3) {
    log.push_back(usage);
    return;
  }
  alignment::Alignment* a = Resolve(st, "ALIGNMENTSTATION", f[0], log);
  if (a == nullptr) return;
  const std::string sub = Lower(f[1]);
  if (sub == "start") {
    double sta = 0.0;
    if (!ParseSta(st, f[2], &sta)) {
      log.push_back("ALIGNMENTSTATION - \"" + f[2] + "\" is not a station.");
      return;
    }
    const bool clear = f.size() == 4 && Lower(f[3]) == "clear";
    if ((!a->equations.empty() || !a->designSpeeds.empty()) && !clear) {  // REQ-483.1: equations and design speeds
      log.push_back("ALIGNMENTSTATION - changing the starting station removes this alignment's " + std::to_string(a->equations.size()) +
                    " station equation(s) and " + std::to_string(a->designSpeeds.size()) +
                    " design speed(s); repeat with , CLEAR to confirm.");
      return;
    }
    PushUndoSnapshot(st, "Set starting station");
    a = Resolve(st, "ALIGNMENTSTATION", f[0], log);
    a->equations.clear();
    a->designSpeeds.clear();
    a->startingStation = sta;
    a->hasReference = false;  // the start is the reference point again
    a->referenceDistance = 0.0;
    log.push_back("ALIGNMENTSTATION - \"" + a->name + "\" starts at " + StaFmt(st, sta) + " and ends at " + Sta(st, *a, a->EndStation()) + ".");
    return;
  }
  if (sub == "reference" && (f.size() == 5 || f.size() == 6)) {  // REQ-483.1: <x>, <y>, <station>[, CLEAR]
    double wx = 0.0, wy = 0.0, sta = 0.0;
    if (!ParseNum(f[2], &wx) || !ParseNum(f[3], &wy) || !ParseSta(st, f[4], &sta)) {
      log.push_back(usage);
      return;
    }
    if (a->Empty()) {
      log.push_back("ALIGNMENTSTATION - \"" + a->name + "\" has no geometry to put a reference point on.");
      return;
    }
    double lx = 0.0, ly = 0.0;
    CadCoord::LocalFromWorld(st, wx, wy, &lx, &ly);
    const alignment::StationOffset so = a->GetStationOffset(lx, ly);
    if (so.outOfRange) {
      log.push_back("ALIGNMENTSTATION - the reference point must lie along \"" + a->name + "\", not beyond its ends.");
      return;
    }
    const bool clear = f.size() == 6 && Lower(f[5]) == "clear";
    if ((!a->equations.empty() || !a->designSpeeds.empty()) && !clear) {
      log.push_back("ALIGNMENTSTATION - changing the reference point removes this alignment's " +
                    std::to_string(a->equations.size()) + " station equation(s) and " +
                    std::to_string(a->designSpeeds.size()) + " design speed(s); repeat with , CLEAR to confirm.");
      return;
    }
    const double along = so.internalStation - a->startingStation;
    PushUndoSnapshot(st, "Set reference point");
    a = Resolve(st, "ALIGNMENTSTATION", f[0], log);
    a->equations.clear();
    a->designSpeeds.clear();
    a->hasReference = true;
    a->referenceDistance = along;
    a->startingStation = sta - along;
    log.push_back("ALIGNMENTSTATION - the point " + Lin(st, along) + " along \"" + a->name + "\" is station " +
                  StaFmt(st, sta) + "; it now starts at " + StaFmt(st, a->startingStation) + ".");
    return;
  }
  if (sub != "eq" && sub != "equation") {
    log.push_back(usage);
    return;
  }
  const std::string verb = Lower(f[2]);
  if (verb == "list") {
    if (a->equations.empty()) log.push_back("ALIGNMENTSTATION - \"" + a->name + "\" has no station equations.");
    for (size_t i = 0; i < a->equations.size(); ++i) {
      const alignment::StationEquation& q = a->equations[i];
      log.push_back("  EQUATION " + std::to_string(i + 1) + "  back " + StaFmt(st, a->DisplayedStation(q.rawBack, false)) + "  ahead " +
                    StaFmt(st, q.ahead) + "  " + (q.increasing ? "increasing" : "decreasing") + "  (raw station " +
                    StaFmt(st, q.rawBack) + ")");
    }
    return;
  }
  if (verb == "add" && f.size() == 6) {
    alignment::StationEquation q;
    const std::string dir = Lower(f[5]);
    if (!ParseSta(st, f[3], &q.rawBack) || !ParseSta(st, f[4], &q.ahead) || (dir != "inc" && dir != "dec")) {
      log.push_back(usage);
      return;
    }
    q.increasing = dir == "inc";
    alignment::Alignment trial = *a;
    std::string why;
    if (!trial.AddStationEquation(q, &why)) {
      log.push_back("ALIGNMENTSTATION - refused: " + why + ".");
      return;
    }
    PushUndoSnapshot(st, "Add station equation");
    alignment::Alignment* target = Resolve(st, "ALIGNMENTSTATION", f[0], log);
    *target = std::move(trial);
    log.push_back("ALIGNMENTSTATION - equation added; \"" + target->name + "\" now ends at station " +
                  StaFmt(st, target->EndStationWithEquations()) + ".");
    return;
  }
  if (verb == "edit" && f.size() == 7) {  // REQ-487.3: replace equation n, refused as a whole if the new one does not fit
    double n = 0.0;
    alignment::StationEquation q;
    const std::string dir = Lower(f[6]);
    if (!ParseNum(f[3], &n) || n < 1.0 || n > static_cast<double>(a->equations.size()) || n != std::floor(n)) {
      log.push_back("ALIGNMENTSTATION - \"" + f[3] + "\" is not an equation number (1 to " + std::to_string(a->equations.size()) + ").");
      return;
    }
    if (!ParseSta(st, f[4], &q.rawBack) || !ParseSta(st, f[5], &q.ahead) || (dir != "inc" && dir != "dec")) {
      log.push_back(usage);
      return;
    }
    q.increasing = dir == "inc";
    alignment::Alignment trial = *a;
    trial.equations.erase(trial.equations.begin() + static_cast<std::ptrdiff_t>(n) - 1);
    std::string why;
    if (!trial.AddStationEquation(q, &why)) {
      log.push_back("ALIGNMENTSTATION - refused: " + why + "; equation " + f[3] + " is unchanged.");
      return;
    }
    PushUndoSnapshot(st, "Edit station equation");
    alignment::Alignment* target = Resolve(st, "ALIGNMENTSTATION", f[0], log);
    *target = std::move(trial);
    log.push_back("ALIGNMENTSTATION - equation " + f[3] + " changed; \"" + target->name + "\" now ends at station " +
                  StaFmt(st, target->EndStationWithEquations()) + ".");
    return;
  }
  if (verb == "remove" && f.size() == 4) {
    double n = 0.0;
    if (!ParseNum(f[3], &n) || n < 1.0 || n > static_cast<double>(a->equations.size()) || n != std::floor(n)) {
      log.push_back("ALIGNMENTSTATION - \"" + f[3] + "\" is not an equation number (1 to " + std::to_string(a->equations.size()) + ").");
      return;
    }
    PushUndoSnapshot(st, "Remove station equation");
    a = Resolve(st, "ALIGNMENTSTATION", f[0], log);
    a->equations.erase(a->equations.begin() + static_cast<std::ptrdiff_t>(n) - 1);
    log.push_back("ALIGNMENTSTATION - equation " + std::to_string(static_cast<int>(n)) + " removed.");
    return;
  }
  log.push_back(usage);
}

namespace {

struct OffsetRequest {
  std::vector<double> distances;  ///< Signed, right positive.
  std::vector<int> increment;     ///< 1-based number of each distance on its side (the template's {n}).
  std::string nameTemplate;       ///< Empty: "<parent> - L12".
  std::string from, to;           ///< Station range on the parent; empty = its whole length.
};

/// "3x12" (three offsets 12 apart) or "12" (one) for one side; false for anything else.
bool ParseOffsetSide(const std::string& text, double sign, OffsetRequest* r) {
  assert(r != nullptr);
  assert(sign == 1.0 || sign == -1.0);
  const size_t x = Lower(text).find('x');
  double count = 1.0, step = 0.0;
  if (x == std::string::npos ? !ParseNum(text, &step) : !ParseNum(text.substr(0, x), &count) || !ParseNum(text.substr(x + 1), &step))
    return false;
  constexpr double kMaxPerSide = 100.0;
  if (!(step > 0.0) || count < 1.0 || count > kMaxPerSide || count != std::floor(count)) return false;
  for (int n = 1; n <= static_cast<int>(count); ++n) {
    r->distances.push_back(sign * step * n);
    r->increment.push_back(n);
  }
  return true;
}

/// Reads the distance list (12 / R12 right, -12 / L12 left) and the LEFT= / RIGHT= / NAME= / FROM= / TO= fields.
bool ParseOffsetRequest(const std::vector<std::string>& f, OffsetRequest* r, std::string* bad) {
  assert(r != nullptr && bad != nullptr);
  assert(!f.empty());
  for (size_t i = 1; i < f.size(); ++i) {
    const size_t eq = f[i].find('=');
    const std::string key = eq == std::string::npos ? std::string() : Lower(Trim(f[i].substr(0, eq)));
    const std::string value = eq == std::string::npos ? Trim(f[i]) : Trim(f[i].substr(eq + 1));
    *bad = f[i];
    if (key == "left" || key == "right") {
      if (!ParseOffsetSide(value, key == "left" ? -1.0 : 1.0, r)) return false;
    } else if (key == "name") {
      r->nameTemplate = value;
    } else if (key == "from" || key == "to") {
      (key == "from" ? r->from : r->to) = value;
    } else if (key.empty()) {
      std::string t = value;
      double sign = 1.0, d = 0.0;
      if (!t.empty() && (t[0] == 'L' || t[0] == 'l')) { sign = -1.0; t.erase(0, 1); }
      else if (!t.empty() && (t[0] == 'R' || t[0] == 'r')) { t.erase(0, 1); }
      if (!ParseNum(t, &d) || d == 0.0) return false;
      r->distances.push_back(sign * d);
      r->increment.push_back(1 + static_cast<int>(std::count_if(r->distances.begin(), r->distances.end() - 1,
                                                                [&](double o) { return (o < 0.0) == (sign * d < 0.0); })));
    } else {
      return false;
    }
  }
  bad->clear();
  return !r->distances.empty();
}

/// The template with {parent}, {side} (Left / Right), {offset} and {n} filled in; or "<parent> - L12".
std::string OffsetName(const std::string& tmpl, const std::string& parent, double d, int n) {
  assert(std::isfinite(d) && d != 0.0);
  assert(n >= 1);
  std::array<char, 48> num{};
  std::snprintf(num.data(), num.size(), "%g", std::fabs(d));
  if (tmpl.empty()) return parent + " - " + (d < 0.0 ? "L" : "R") + num.data();
  std::string out = tmpl;
  const std::array<std::pair<const char*, std::string>, 4> tokens = {{{"{parent}", parent},
                                                                      {"{side}", d < 0.0 ? "Left" : "Right"},
                                                                      {"{offset}", num.data()},
                                                                      {"{n}", std::to_string(n)}}};
  for (const auto& [token, value] : tokens) {
    for (size_t at = out.find(token), guard = 0; at != std::string::npos && guard < 64; at = out.find(token), ++guard)
      out.replace(at, std::strlen(token), value);
  }
  return out;
}

}  // namespace

// OFFSETALIGNMENT <parent>, <distance>[, <distance> ...]    distance: 12 / R12 = right, -12 / L12 = left
//   [, LEFT=<count>x<increment>][, RIGHT=<count>x<increment>][, NAME=<template>][, FROM=<station>][, TO=<station>]
//   REQ-488.1: template tokens {parent} {side} {offset} {n}; the range limits the offsets to part of the parent.
void RunOffsetAlignment(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitFields(args);
  const char* usage = "OFFSETALIGNMENT - usage: OFFSETALIGNMENT <parent>, <distance>[, <distance>...] (R12 or 12 = right, "
                      "L12 or -12 = left) [, LEFT=<count>x<increment>][, RIGHT=<count>x<increment>][, NAME=<template>]"
                      "[, FROM=<station>][, TO=<station>].";
  if (f.size() < 2) {
    log.push_back(usage);
    return;
  }
  alignment::Alignment* parent = Resolve(st, "OFFSETALIGNMENT", f[0], log);
  if (parent == nullptr) return;
  const std::uint64_t parentId = parent->id;
  const alignment::Alignment parentCopy = *parent;
  OffsetRequest req;
  std::string bad;
  if (!ParseOffsetRequest(f, &req, &bad)) {
    log.push_back(bad.empty() ? std::string(usage) : "OFFSETALIGNMENT - \"" + bad + "\" is not a non-zero offset distance or field.");
    return;
  }
  alignment::Alignment source = parentCopy;  // the parent, or the part of it in the station range
  double from = 0.0, to = 0.0;
  if (!req.from.empty() || !req.to.empty()) {
    double shownFrom = 0.0, shownTo = 0.0;
    const bool fromOk = req.from.empty() || ParseSta(st, req.from, &shownFrom);
    const bool toOk = req.to.empty() || ParseSta(st, req.to, &shownTo);
    const std::vector<double> rawFrom = req.from.empty() ? std::vector<double>{parentCopy.StartStation()}
                                                         : parentCopy.RawStationsForDisplayed(shownFrom);
    const std::vector<double> rawTo = req.to.empty() ? std::vector<double>{parentCopy.EndStation()}
                                                     : parentCopy.RawStationsForDisplayed(shownTo);
    std::string why = "a station is outside \"" + parentCopy.name + "\"";
    if (!fromOk || !toOk || rawFrom.empty() || rawTo.empty() ||
        !parentCopy.ExtractRange(rawFrom.front(), rawTo.back(), &source, &why)) {
      log.push_back("OFFSETALIGNMENT - the station range is not usable: " + why + ".");
      return;
    }
    source.id = parentCopy.id;
    from = rawFrom.front();
    to = rawTo.back();
  }
  // Build everything first so one bad distance creates nothing.
  std::vector<alignment::Alignment> made;
  for (size_t k = 0; k < req.distances.size(); ++k) {
    const double d = req.distances[k];
    alignment::Alignment off;
    off.name = OffsetName(req.nameTemplate, parentCopy.name, d, req.increment[k]);
    const bool duplicate = std::any_of(made.begin(), made.end(), [&](const alignment::Alignment& m) {
      return Lower(m.name) == Lower(off.name);
    });
    if (off.name.empty() || duplicate || FindAlignment(st, off.name) >= 0) {
      log.push_back("OFFSETALIGNMENT - an alignment named \"" + off.name + "\" already exists; nothing was created.");
      return;
    }
    off.layer = parentCopy.layer;
    off.style = parentCopy.style;
    off.labelSet = parentCopy.labelSet;
    std::string why;
    if (!alignment::MakeOffset(source, d, &off, &why)) {
      log.push_back("OFFSETALIGNMENT - refused for " + off.name + ": " + why + ". Nothing was created.");
      return;
    }
    off.startingStation = source.startingStation;
    off.offsetFromStation = from;
    off.offsetToStation = to;
    made.push_back(std::move(off));
  }
  PushUndoSnapshot(st, "Offset alignment");
  for (alignment::Alignment& off : made) {
    off.id = st.nextEntityId++;
    off.parentId = parentId;
    log.push_back("OFFSETALIGNMENT - created \"" + off.name + "\" (" + Lin(st, std::fabs(off.offsetDistance)) +
                  (off.offsetDistance < 0.0 ? " left" : " right") + "), length " + Lin(st, off.Length()) + ".");
    st.alignments.push_back(std::move(off));
  }
}

// ALIGNMENTDETACH <name> - REQ-488.3
void RunAlignmentDetach(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::string name = Trim(args);
  alignment::Alignment* a = Resolve(st, "ALIGNMENTDETACH", name, log);
  if (a == nullptr) return;
  if (a->parentId == 0) {
    log.push_back("ALIGNMENTDETACH - \"" + a->name + "\" is not linked to a parent.");
    return;
  }
  PushUndoSnapshot(st, "Detach alignment");
  a = Resolve(st, "ALIGNMENTDETACH", name, log);
  a->parentId = 0;
  log.push_back("ALIGNMENTDETACH - \"" + a->name + "\" is now independent and can be edited.");
}

// ALIGNMENTINTERSECT <a>, <b> | <a>, LINE, x1, y1, x2, y2 | <a>, CIRCLE, cx, cy, r | <a>, ARC, cx, cy, r, start, end
//   [, POINT]   (WORLD coordinates; arc angles in degrees counter-clockwise from east) - REQ-489
void RunAlignmentIntersect(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  std::vector<std::string> f = SplitFields(args);
  const char* usage =
      "ALIGNMENTINTERSECT - usage: ALIGNMENTINTERSECT <alignment>, <alignment> | <alignment>, LINE, <x1>, <y1>, <x2>, <y2> | "
      "<alignment>, CIRCLE, <cx>, <cy>, <r> | <alignment>, ARC, <cx>, <cy>, <r>, <start angle>, <end angle> [, POINT].";
  const bool placePoints = !f.empty() && Lower(f.back()) == "point";  // REQ-489: place a point at each intersection
  if (placePoints) f.pop_back();
  if (f.size() < 2) {
    log.push_back(usage);
    return;
  }
  alignment::Alignment* a = Resolve(st, "ALIGNMENTINTERSECT", f[0], log);
  if (a == nullptr) return;
  alignment::Alignment other;
  std::string otherName;
  const std::string kind = Lower(f[1]);
  double v[5] = {};
  auto num = [&](size_t from, size_t n) {
    if (f.size() != from + n) return false;
    for (size_t i = 0; i < n; ++i) {
      if (!ParseNum(f[from + i], &v[i])) return false;
    }
    return true;
  };
  if (kind == "line" && num(2, 4)) {
    double ax = 0, ay = 0, bx = 0, by = 0;
    CadCoord::LocalFromWorld(st, v[0], v[1], &ax, &ay);
    CadCoord::LocalFromWorld(st, v[2], v[3], &bx, &by);
    other = alignment::MakeLineObject({ax, ay}, {bx, by});
    otherName = "the line";
  } else if (kind == "circle" && num(2, 3) && v[2] > 0.0) {
    double cx = 0, cy = 0;
    CadCoord::LocalFromWorld(st, v[0], v[1], &cx, &cy);
    other = alignment::MakeCircleObject({cx, cy}, v[2], 0.0, alignment::kTwoPi - 1e-9);
    otherName = "the circle";
  } else if (kind == "arc" && num(2, 5) && v[2] > 0.0) {
    double cx = 0, cy = 0;
    CadCoord::LocalFromWorld(st, v[0], v[1], &cx, &cy);
    double sweep = std::fmod(v[4] - v[3], 360.0);
    if (sweep <= 0.0) sweep += 360.0;
    other = alignment::MakeCircleObject({cx, cy}, v[2], v[3] * kDegToRad, std::min(sweep * kDegToRad, alignment::kTwoPi - 1e-9));
    otherName = "the arc";
  } else if (f.size() == 2) {
    alignment::Alignment* b = Resolve(st, "ALIGNMENTINTERSECT", f[1], log);
    if (b == nullptr) return;
    other = *b;
    otherName = "\"" + b->name + "\"";
  } else {
    log.push_back(usage);
    return;
  }
  if (a->Empty() || other.Empty()) {
    log.push_back("ALIGNMENTINTERSECT - both objects need geometry.");
    return;
  }
  std::vector<alignment::IntersectionOverlap> overlaps;
  const std::vector<alignment::Intersection> hits = alignment::IntersectAlignments(*a, other, &overlaps);
  log.push_back("ALIGNMENTINTERSECT - \"" + a->name + "\" and " + otherName + ": " + std::to_string(hits.size()) + " intersection(s).");
  const bool otherIsAlignment = f.size() == 2;
  for (size_t i = 0; i < hits.size(); ++i) {
    const alignment::Intersection& h = hits[i];
    // The point lies on both, so its offset from each is zero; the station is given on each alignment (REQ-489).
    log.push_back("  " + std::to_string(i + 1) + ": X = " + Lin(st, h.point.x + CadCoord::WorldOriginX(st)) + "  Y = " +
                  Lin(st, h.point.y + CadCoord::WorldOriginY(st)) + "  Station = " + Sta(st, *a, h.stationA) + "  Offset = " +
                  Lin(st, 0.0) + (otherIsAlignment ? "  Station on " + otherName + " = " + Sta(st, other, h.stationB) : "") +
                  "  Angle = " + FormatSweptAngle(h.angle / kDegToRad, CadAngleDisplaySettings(st)) +
                  (h.tangential ? "  (tangential)" : ""));
  }
  for (const alignment::IntersectionOverlap& o : overlaps)
    log.push_back("  Coincident from station " + Sta(st, *a, o.fromStationA) + " to " + Sta(st, *a, o.toStationA) + ".");
  if (placePoints && !hits.empty()) {
    PushUndoSnapshot(st, "Alignment intersection points");
    for (const alignment::Intersection& h : hits) {
      CadPoint p{};
      p.x = h.point.x;
      p.y = h.point.y;
      st.cadPoints.push_back(p);
      st.cadPointAttrs.push_back(MakeNewEntityAttrs(st));
    }
    BumpCadGpuCache(st);
    log.push_back("ALIGNMENTINTERSECT - " + std::to_string(hits.size()) + " point(s) placed.");
  }
}

// EDITALIGNMENT [<name>] - REQ-486.1. Opens the Alignment Layout table in the Qt editor; headless it lists the rows.
void RunEditAlignment(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  std::string name = Trim(args);
  if (name.empty()) {
    for (const SelectedEntity& e : st.selection) {
      if (e.type == SelectedEntity::Type::Alignment && e.index >= 0 && static_cast<size_t>(e.index) < st.alignments.size()) {
        name = st.alignments[static_cast<size_t>(e.index)].name;
        break;
      }
    }
  }
  if (name.empty() && !st.alignments.empty()) name = st.alignments.front().name;
  if (name.empty()) {
    log.push_back("EDITALIGNMENT - this drawing has no alignments.");
    return;
  }
  if (FindAlignment(st, name) < 0) {
    log.push_back("EDITALIGNMENT - no alignment named \"" + name + "\".");
    return;
  }
  st.alignmentLayoutRequested = true;
  st.alignmentLayoutName = name;
  // As in Civil 3D, the alignment being edited is the selection, so Properties and the Alignment ribbon tab show it.
  st.selection = {{SelectedEntity::Type::Alignment, FindAlignment(st, name)}};
  RunAlignmentList(st, name, log);  // the same rows, for a window-less run
}

SurfaceComponentStyle* AlignmentComponentByName(AlignmentStyle& s, const std::string& word, std::string* why) {
  const std::string w = Lower(word);
  if (w == "tangent" || w == "line") return &s.tangent;
  if (w == "curve" || w == "arc") return &s.curve;
  if (w == "spiral") return &s.spiral;
  if (why != nullptr)
    *why = "component must be TANGENT, CURVE or SPIRAL, not \"" + word + "\".";
  return nullptr;
}

namespace {

// Reads space-separated geometry point codes as the labels write them (BP, PI, TS, Mid, ...). False on an unknown one.
bool ParseGeometryCodes(const std::string& text, std::vector<std::string>* codes, std::string* bad) {
  assert(codes != nullptr && bad != nullptr);
  assert(text.size() < 100000);
  static const std::array<const char*, 14> kCodes{"BP", "EP", "PI", "PC", "PT", "PCC", "PRC",
                                                  "TS", "ST", "CS", "SC", "SS", "RS", "Mid"};
  std::istringstream words(text);
  std::string w;
  for (int guard = 0; guard < 1000 && (words >> w); ++guard) {
    const auto it = std::find_if(kCodes.begin(), kCodes.end(), [&](const char* c) { return Lower(c) == Lower(w); });
    if (it == kCodes.end()) {
      *bad = w;
      return false;
    }
    if (std::find(codes->begin(), codes->end(), *it) == codes->end()) codes->push_back(*it);
  }
  return !codes->empty();
}

// ALIGNMENTSTYLE MARKER <style>, <code>, CROSS|X|CIRCLE|SQUARE|NONE|DEFAULT[, <size>[, <color>]]  (REQ-491 Markers tab:
// shape, size and colour per geometry point code; DEFAULT returns the code to the style's cross)
void RunAlignmentStyleMarker(AppCommandState& st, const std::vector<std::string>& f, std::vector<std::string>& log) {
  assert(!f.empty());
  assert(!st.alignmentStyles.empty());
  const AlignmentStyle* current = f.size() >= 4 && f.size() <= 6 ? AlignmentStyles::Find(st.alignmentStyles, f[1]) : nullptr;
  std::vector<std::string> codes;
  std::string bad;
  if (current == nullptr || !ParseGeometryCodes(f.size() >= 3 ? f[2] : std::string{}, &codes, &bad) || codes.size() != 1) {
    log.push_back("ALIGNMENTSTYLE - usage: ALIGNMENTSTYLE MARKER <style>, <code>, CROSS|X|CIRCLE|SQUARE|NONE|DEFAULT"
                  "[, <size>[, <color>]] (one code: BP EP PI PC PT PCC PRC TS ST CS SC SS RS Mid).");
    return;
  }
  static const std::array<std::pair<const char*, AlignmentMarkerShape>, 5> kShapes{{{"cross", AlignmentMarkerShape::Cross},
      {"x", AlignmentMarkerShape::X}, {"circle", AlignmentMarkerShape::Circle}, {"square", AlignmentMarkerShape::Square},
      {"none", AlignmentMarkerShape::None}}};
  AlignmentStyle edited = *current;
  auto& markers = edited.codeMarkers;
  markers.erase(std::remove_if(markers.begin(), markers.end(), [&](const AlignmentMarkerSpec& m) { return m.code == codes[0]; }),
                markers.end());
  if (Lower(f[3]) != "default") {
    const auto shape = std::find_if(kShapes.begin(), kShapes.end(), [&](const auto& s) { return Lower(f[3]) == s.first; });
    AlignmentMarkerSpec spec{codes[0], AlignmentMarkerShape::Cross, edited.markerHalfSize, "ByLayer"};
    double size = 0.0;
    std::string color = "ByLayer";
    if (shape == kShapes.end() || (f.size() >= 5 && (!ParseNum(f[4], &size) || !(size > 0.0))) ||
        (f.size() == 6 && !CadColorStorageFromTyped(f[5], &color))) {
      log.push_back("ALIGNMENTSTYLE - MARKER needs a shape (CROSS, X, CIRCLE, SQUARE, NONE or DEFAULT), a positive size "
                    "and a colour.");
      return;
    }
    spec.shape = shape->second;
    if (f.size() >= 5) spec.halfSize = size;
    spec.color = color;
    markers.push_back(std::move(spec));
  }
  PushUndoSnapshot(st, "Alignment style");
  *AlignmentStyles::Find(st.alignmentStyles, f[1]) = std::move(edited);
  BumpCadGpuCache(st);
  log.push_back("ALIGNMENTSTYLE - \"" + f[1] + "\" " + codes[0] + " marker set to " + f[3] + ".");
}

}  // namespace

void RunAlignmentStyle(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  AlignmentStyles::EnsureStandard(st.alignmentStyles);
  const auto usage = [&]() {
    log.push_back(
        "ALIGNMENTSTYLE - usage: ALIGNMENTSTYLE LIST | NEW <style> | DELETE <style> | ASSIGN <alignment>, <style> | "
        "SHOW|HIDE <style>, <tangent|curve|spiral> | COLOR <style>, <tangent|curve|spiral>, <color> | "
        "SET <style>, TICKS|MARKERS|ARROWS|LABELS ON|OFF | SET <style>, TICKSIDE BOTH|LEFT|RIGHT | "
        "SET <style>, MAJORTICK|MINORTICK|MARKERSIZE|ARROWLENGTH <n> | SET <style>, CHECKSET <set>|NONE | "
        "CURRENT <style> | MARKER <style>, <code>, CROSS|X|CIRCLE|SQUARE|NONE|DEFAULT[, <size>[, <color>]].");
  };
  const std::string trimmed = Trim(args);
  if (trimmed.empty()) {
    usage();
    return;
  }
  const std::vector<std::string> f = SplitFields(trimmed);
  const std::string verb = Lower(f[0]);
  if (verb == "list") {
    if (st.alignmentStyles.empty()) {
      log.push_back("ALIGNMENTSTYLE - no alignment styles.");
      return;
    }
    for (const AlignmentStyle& s : st.alignmentStyles)
      log.push_back("  STYLE \"" + s.name + "\"  (used by " +
                    std::to_string(AlignmentStyles::CountAlignmentsResolvingTo(st.alignments, st.alignmentStyles, s.name)) +
                    " alignment(s))");
    return;
  }
  if (verb == "new") {
    if (f.size() < 2 || f[1].empty()) {
      usage();
      return;
    }
    const std::string name = f[1];
    if (AlignmentStyles::Find(st.alignmentStyles, name)) {
      log.push_back("ALIGNMENTSTYLE - a style named \"" + name + "\" already exists.");
      return;
    }
    PushUndoSnapshot(st, "Alignment style");
    AlignmentStyle s = AlignmentStyles::StandardAlignmentStyle();
    s.name = name;
    st.alignmentStyles.push_back(std::move(s));
    BumpCadGpuCache(st);
    log.push_back("ALIGNMENTSTYLE - created style \"" + name + "\" (copied from Standard).");
    return;
  }
  if (verb == "delete") {
    if (f.size() < 2 || f[1].empty()) {
      usage();
      return;
    }
    const std::string name = f[1];
    if (name == AlignmentStyles::kStandardName) {
      log.push_back("ALIGNMENTSTYLE - \"Standard\" cannot be deleted.");
      return;
    }
    const int users = AlignmentStyles::CountAlignmentsResolvingTo(st.alignments, st.alignmentStyles, name);
    if (users > 0) {
      log.push_back("ALIGNMENTSTYLE - \"" + name + "\" is used by " + std::to_string(users) +
                    " alignment(s) and cannot be deleted.");
      return;
    }
    if (name == st.alignmentStyleCurrent) {
      log.push_back("ALIGNMENTSTYLE - \"" + name + "\" is the current style and cannot be deleted; make another current first.");
      return;
    }
    const auto it = std::find_if(st.alignmentStyles.begin(), st.alignmentStyles.end(),
                                 [&](const AlignmentStyle& s) { return s.name == name; });
    if (it == st.alignmentStyles.end()) {
      log.push_back("ALIGNMENTSTYLE - no style named \"" + name + "\".");
      return;
    }
    PushUndoSnapshot(st, "Alignment style");
    st.alignmentStyles.erase(it);
    BumpCadGpuCache(st);
    log.push_back("ALIGNMENTSTYLE - deleted style \"" + name + "\".");
    return;
  }
  if (verb == "set") {  // REQ-491: the appearance items the Markers, Display and Design tabs edit
    if (f.size() != 4) {
      usage();
      return;
    }
    const AlignmentStyle* current = AlignmentStyles::Find(st.alignmentStyles, f[1]);
    if (current == nullptr) {
      log.push_back("ALIGNMENTSTYLE - no style named \"" + f[1] + "\".");
      return;
    }
    AlignmentStyle edited = *current;  // validate on a copy, so a refused item changes nothing and takes no undo step
    const std::string item = Lower(f[2]), value = Lower(f[3]);
    double v = 0.0;
    const bool numberOk = ParseNum(f[3], &v) && v >= 0.0;
    bool ok = false;
    const auto setOnOff = [&](bool* target) {
      if (value != "on" && value != "off") return false;
      *target = value == "on";
      return true;
    };
    if (item == "checkset") {  // REQ-502 / D-2026-11-04-b: the design-check set the style applies by default
      const std::string want = Trim(f[3]);
      if (Lower(want) == "none") {
        edited.checkSetName.clear();
        ok = true;
      } else if (const alignment::AlignmentCheckSet* set = alignment::FindCheckSet(st.alignmentCheckSets, want)) {
        edited.checkSetName = set->name;
        ok = true;
      }
    } else if (item == "ticks") ok = setOnOff(&edited.ticks.visible);
    else if (item == "markers") ok = setOnOff(&edited.markers.visible);
    else if (item == "arrows") ok = setOnOff(&edited.arrows.visible);
    else if (item == "labels") ok = setOnOff(&edited.labelsVisible);
    else if (item == "tickside") {
      ok = true;
      if (value == "both") edited.tickSide = 0;
      else if (value == "left") edited.tickSide = 1;
      else if (value == "right") edited.tickSide = 2;
      else ok = false;
    } else if (item == "majortick" && numberOk) { edited.majorTickLength = v; ok = true; }
    else if (item == "minortick" && numberOk) { edited.minorTickLength = v; ok = true; }
    else if (item == "markersize" && numberOk) { edited.markerHalfSize = v; ok = true; }
    else if (item == "arrowlength" && numberOk) { edited.arrowLength = v; ok = true; }
    if (!ok) {
      log.push_back("ALIGNMENTSTYLE - SET item \"" + f[2] + "\" with value \"" + f[3] + "\" is not valid.");
      return;
    }
    if (edited == *current) {
      log.push_back("ALIGNMENTSTYLE - \"" + edited.name + "\" already has " + f[2] + " set to " + f[3] + ".");
      return;
    }
    PushUndoSnapshot(st, "Alignment style");
    *AlignmentStyles::Find(st.alignmentStyles, f[1]) = std::move(edited);
    BumpCadGpuCache(st);
    log.push_back("ALIGNMENTSTYLE - style \"" + f[1] + "\": " + f[2] + " set to " + f[3] + ".");
    return;
  }
  if (verb == "assign") {
    if (f.size() < 3 || f[1].empty() || f[2].empty()) {
      usage();
      return;
    }
    alignment::Alignment* a = Resolve(st, "ALIGNMENTSTYLE", f[1], log);
    if (a == nullptr) return;
    if (!AlignmentStyles::Find(st.alignmentStyles, f[2])) {
      log.push_back("ALIGNMENTSTYLE - no style named \"" + f[2] + "\".");
      return;
    }
    PushUndoSnapshot(st, "Alignment style");
    a = Resolve(st, "ALIGNMENTSTYLE", f[1], log);
    a->style = f[2];
    BumpCadGpuCache(st);
    log.push_back("ALIGNMENTSTYLE - alignment \"" + a->name + "\" now uses style \"" + f[2] + "\".");
    return;
  }
  if (verb == "show" || verb == "hide") {
    if (f.size() < 3 || f[1].empty() || f[2].empty()) {
      usage();
      return;
    }
    AlignmentStyle* s = AlignmentStyles::Find(st.alignmentStyles, f[1]);
    if (s == nullptr) {
      log.push_back("ALIGNMENTSTYLE - no style named \"" + f[1] + "\".");
      return;
    }
    std::string why;
    SurfaceComponentStyle* comp = AlignmentComponentByName(*s, f[2], &why);
    if (comp == nullptr) {
      log.push_back("ALIGNMENTSTYLE - " + why);
      return;
    }
    PushUndoSnapshot(st, "Alignment style");
    comp->visible = (verb == "show");
    BumpCadGpuCache(st);
    log.push_back("ALIGNMENTSTYLE - \"" + s->name + "\" " + f[2] + " " + (comp->visible ? "shown." : "hidden."));
    return;
  }
  if (verb == "color") {
    if (f.size() < 4 || f[1].empty() || f[2].empty() || f[3].empty()) {
      usage();
      return;
    }
    AlignmentStyle* s = AlignmentStyles::Find(st.alignmentStyles, f[1]);
    if (s == nullptr) {
      log.push_back("ALIGNMENTSTYLE - no style named \"" + f[1] + "\".");
      return;
    }
    std::string why;
    SurfaceComponentStyle* comp = AlignmentComponentByName(*s, f[2], &why);
    if (comp == nullptr) {
      log.push_back("ALIGNMENTSTYLE - " + why);
      return;
    }
    PushUndoSnapshot(st, "Alignment style");
    comp->color = f[3];
    BumpCadGpuCache(st);
    log.push_back("ALIGNMENTSTYLE - \"" + s->name + "\" " + f[2] + " color set to " + f[3] + ".");
    return;
  }
  if (verb == "current" && f.size() == 2) {  // REQ-491 / REQ-478 current style
    const AlignmentStyle* s = AlignmentStyles::Find(st.alignmentStyles, f[1]);
    if (s == nullptr) {
      log.push_back("ALIGNMENTSTYLE - no style named \"" + f[1] + "\".");
      return;
    }
    PushUndoSnapshot(st, "Alignment style");
    st.alignmentStyleCurrent = f[1];
    log.push_back("ALIGNMENTSTYLE - \"" + f[1] + "\" is now the current style; new alignments use it.");
    return;
  }
  if (verb == "marker") {
    RunAlignmentStyleMarker(st, f, log);
    return;
  }
  usage();
}

namespace {

// The label-set field an item name selects (REQ-492.5), or nullptr for an unknown item.
std::string* LabelSetItemStyle(AlignmentLabelSet& s, const std::string& item) {
  const std::string which = Lower(item);
  if (which == "major") return &s.majorStyle;
  if (which == "geometry") return &s.geometryStyle;
  if (which == "speeds" || which == "speed") return &s.speedStyle;
  if (which == "equations" || which == "equation") return &s.equationStyle;
  if (which == "segments" || which == "segment") return &s.segmentStyle;
  if (which == "offsets" || which == "offset") return &s.offsetStyle;
  return nullptr;
}

// ALIGNMENTLABELSET STYLE, <set>, MAJOR|GEOMETRY|SPEEDS|EQUATIONS|SEGMENTS|OFFSETS, <label style>
void RunAlignmentLabelSetStyle(AppCommandState& st, const std::vector<std::string>& f, std::vector<std::string>& log) {
  assert(!f.empty());
  assert(!st.alignmentLabelSets.empty());
  if (f.size() < 4 || f[1].empty() || f[2].empty() || f[3].empty()) {
    log.push_back("ALIGNMENTLABELSET - usage: ALIGNMENTLABELSET STYLE, <set>, "
                  "MAJOR|GEOMETRY|SPEEDS|EQUATIONS|SEGMENTS|OFFSETS, <label style>.");
    return;
  }
  AlignmentLabelSet* s = AlignmentLabelSets::Find(st.alignmentLabelSets, f[1]);
  if (s == nullptr) {
    log.push_back("ALIGNMENTLABELSET - no label set named \"" + f[1] + "\".");
    return;
  }
  if (LabelSetItemStyle(*s, f[2]) == nullptr) {
    log.push_back("ALIGNMENTLABELSET - item must be MAJOR, GEOMETRY, SPEEDS, EQUATIONS, SEGMENTS or OFFSETS, not \"" +
                  f[2] + "\".");
    return;
  }
  if (AlignmentLabelStyles::Find(st.alignmentLabelStyles, f[3]) == nullptr) {
    log.push_back("ALIGNMENTLABELSET - no alignment label style named \"" + f[3] + "\" (see ALIGNMENTLABELSTYLE LIST).");
    return;
  }
  PushUndoSnapshot(st, "Alignment label set");
  s = AlignmentLabelSets::Find(st.alignmentLabelSets, f[1]);
  *LabelSetItemStyle(*s, f[2]) = f[3];
  BumpCadGpuCache(st);
  log.push_back("ALIGNMENTLABELSET - \"" + s->name + "\" " + f[2] + " labels now use label style \"" + f[3] + "\".");
}

// ALIGNMENTLABELSET GEOMETRY, <set>, CODES, <codes>|ALL | ADD, <label style>, <codes> | REMOVE, <n>   (REQ-492.1: a
// Geometry Points item may repeat with different styles, e.g. BP and EP in one and the PIs in another)
void RunAlignmentLabelSetGeometry(AppCommandState& st, const std::vector<std::string>& f, std::vector<std::string>& log) {
  assert(!f.empty());
  assert(!st.alignmentLabelSets.empty());
  const char* usage = "ALIGNMENTLABELSET - usage: ALIGNMENTLABELSET GEOMETRY, <set>, CODES, <codes>|ALL | "
                      "ADD, <label style>, <codes> | REMOVE, <item#> (codes: BP EP PI PC PT PCC PRC TS ST CS SC SS RS Mid).";
  const std::string op = f.size() >= 3 ? Lower(f[2]) : std::string{};
  const bool shapeOk = (op == "codes" && f.size() == 4) || (op == "add" && f.size() == 5) || (op == "remove" && f.size() == 4);
  const AlignmentLabelSet* current = shapeOk ? AlignmentLabelSets::Find(st.alignmentLabelSets, f[1]) : nullptr;
  if (!shapeOk || current == nullptr) {
    log.push_back(shapeOk ? "ALIGNMENTLABELSET - no label set named \"" + f[1] + "\"." : std::string(usage));
    return;
  }
  AlignmentLabelSet edited = *current;
  std::vector<std::string> codes;
  std::string bad;
  if (op == "codes" && Lower(f[3]) == "all") {
    edited.geometryCodes.clear();
  } else if (op == "codes" || op == "add") {
    if (!ParseGeometryCodes(op == "add" ? f[4] : f[3], &codes, &bad)) {
      log.push_back("ALIGNMENTLABELSET - \"" + bad + "\" is not a geometry point code. " + usage);
      return;
    }
    if (op == "codes") edited.geometryCodes = codes;
    else if (AlignmentLabelStyles::Find(st.alignmentLabelStyles, f[3]) == nullptr) {
      log.push_back("ALIGNMENTLABELSET - no alignment label style named \"" + f[3] + "\".");
      return;
    } else edited.extraGeometryItems.push_back({f[3], codes});
  } else {
    double n = 0.0;
    if (!ParseNum(f[3], &n) || n < 1.0 || n > static_cast<double>(edited.extraGeometryItems.size()) || n != std::floor(n)) {
      log.push_back("ALIGNMENTLABELSET - \"" + edited.name + "\" has " + std::to_string(edited.extraGeometryItems.size()) +
                    " further geometry item(s); \"" + f[3] + "\" is not one of them.");
      return;
    }
    edited.extraGeometryItems.erase(edited.extraGeometryItems.begin() + static_cast<std::ptrdiff_t>(n) - 1);
  }
  PushUndoSnapshot(st, "Alignment label set");
  *AlignmentLabelSets::Find(st.alignmentLabelSets, f[1]) = std::move(edited);
  BumpCadGpuCache(st);
  log.push_back("ALIGNMENTLABELSET - \"" + f[1] + "\" geometry point items updated.");
}

}  // namespace

void RunAlignmentLabelSet(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  AlignmentLabelSets::EnsureStandard(st.alignmentLabelSets);
  AlignmentLabelStyles::EnsureStandard(st.alignmentLabelStyles);
  const auto usage = [&]() {
    log.push_back(
        "ALIGNMENTLABELSET - usage: ALIGNMENTLABELSET LIST | NEW <set> | DELETE <set> | ASSIGN <alignment>, <set> | "
        "MAJORINTERVAL <set>, <interval> | MINORINTERVAL <set>, <interval> | "
        "SHOW|HIDE <set>, MAJOR|MINOR|EQUATIONS|GEOMETRY|SPEEDS|PROFILE|SUPERELEVATION | "
        "STYLE <set>, MAJOR|GEOMETRY|SPEEDS|EQUATIONS|SEGMENTS|OFFSETS, <label style> | "
        "GEOMETRY <set>, CODES, <codes>|ALL | GEOMETRY <set>, ADD, <label style>, <codes> | GEOMETRY <set>, REMOVE, <n>.");
  };
  const std::string trimmed = Trim(args);
  if (trimmed.empty()) {
    usage();
    return;
  }
  const std::vector<std::string> f = SplitFields(trimmed);
  const std::string verb = Lower(f[0]);
  if (verb == "list") {
    const auto joined = [](const std::vector<std::string>& codes) {
      std::string text;
      for (const std::string& c : codes) text += (text.empty() ? "" : " ") + c;
      return text.empty() ? std::string("all") : text;
    };
    for (const AlignmentLabelSet& s : st.alignmentLabelSets) {
      log.push_back("  LABEL SET \"" + s.name + "\"  major=" + Lin(st, s.majorInterval) + " minor=" +
                    Lin(st, s.minorInterval) +
                    "  (used by " +
                    std::to_string(AlignmentLabelSets::CountAlignmentsResolvingTo(st.alignments, st.alignmentLabelSets,
                                                                                  s.name)) +
                    " alignment(s))");
      log.push_back("    Geometry points 1: " + joined(s.geometryCodes) + " in \"" + s.geometryStyle + "\"");
      for (size_t k = 0; k < s.extraGeometryItems.size(); ++k)
        log.push_back("    Geometry points " + std::to_string(k + 2) + ": " + joined(s.extraGeometryItems[k].codes) +
                      " in \"" + s.extraGeometryItems[k].style + "\"");
    }
    return;
  }
  if (verb == "new") {
    if (f.size() < 2 || f[1].empty()) {
      usage();
      return;
    }
    if (AlignmentLabelSets::Find(st.alignmentLabelSets, f[1])) {
      log.push_back("ALIGNMENTLABELSET - a label set named \"" + f[1] + "\" already exists.");
      return;
    }
    PushUndoSnapshot(st, "Alignment label set");
    AlignmentLabelSet s = AlignmentLabelSets::StandardLabelSet();
    s.name = f[1];
    const bool metric = st.drawingInsUnits == 4 || st.drawingInsUnits == 6;
    if (metric) {  // REQ-492.2: 20 m major stations in a metric drawing; minors keep the imperial 1:5 ratio
      s.majorInterval = 20.0;
      s.minorInterval = 4.0;
    }
    st.alignmentLabelSets.push_back(std::move(s));
    BumpCadGpuCache(st);
    log.push_back("ALIGNMENTLABELSET - created label set \"" + f[1] + "\" (copied from Standard" +
                  (metric ? ", major 20 m, minor 4 m)." : ")."));
    return;
  }
  if (verb == "delete") {
    if (f.size() < 2 || f[1].empty()) {
      usage();
      return;
    }
    if (f[1] == AlignmentLabelSets::kStandardName) {
      log.push_back("ALIGNMENTLABELSET - \"Standard\" cannot be deleted.");
      return;
    }
    const int users = AlignmentLabelSets::CountAlignmentsResolvingTo(st.alignments, st.alignmentLabelSets, f[1]);
    if (users > 0) {
      log.push_back("ALIGNMENTLABELSET - \"" + f[1] + "\" is used by " + std::to_string(users) +
                    " alignment(s) and cannot be deleted.");
      return;
    }
    const auto it = std::find_if(st.alignmentLabelSets.begin(), st.alignmentLabelSets.end(),
                                 [&](const AlignmentLabelSet& s) { return s.name == f[1]; });
    if (it == st.alignmentLabelSets.end()) {
      log.push_back("ALIGNMENTLABELSET - no label set named \"" + f[1] + "\".");
      return;
    }
    PushUndoSnapshot(st, "Alignment label set");
    st.alignmentLabelSets.erase(it);
    BumpCadGpuCache(st);
    log.push_back("ALIGNMENTLABELSET - deleted label set \"" + f[1] + "\".");
    return;
  }
  if (verb == "assign") {
    if (f.size() < 3 || f[1].empty() || f[2].empty()) {
      usage();
      return;
    }
    alignment::Alignment* a = Resolve(st, "ALIGNMENTLABELSET", f[1], log);
    if (a == nullptr) return;
    if (!AlignmentLabelSets::Find(st.alignmentLabelSets, f[2])) {
      log.push_back("ALIGNMENTLABELSET - no label set named \"" + f[2] + "\".");
      return;
    }
    PushUndoSnapshot(st, "Alignment label set");
    a = Resolve(st, "ALIGNMENTLABELSET", f[1], log);
    a->labelSet = f[2];
    BumpCadGpuCache(st);
    log.push_back("ALIGNMENTLABELSET - alignment \"" + a->name + "\" now uses label set \"" + f[2] + "\".");
    return;
  }
  if (verb == "majorinterval") {
    if (f.size() < 3 || f[1].empty() || f[2].empty()) {
      usage();
      return;
    }
    AlignmentLabelSet* s = AlignmentLabelSets::Find(st.alignmentLabelSets, f[1]);
    if (s == nullptr) {
      log.push_back("ALIGNMENTLABELSET - no label set named \"" + f[1] + "\".");
      return;
    }
    const double v = std::strtod(f[2].c_str(), nullptr);
    if (!(v > 0.0)) {
      log.push_back("ALIGNMENTLABELSET - major interval must be positive.");
      return;
    }
    PushUndoSnapshot(st, "Alignment label set");
    s->majorInterval = v;
    BumpCadGpuCache(st);
    log.push_back("ALIGNMENTLABELSET - \"" + s->name + "\" major interval set to " + Lin(st, v) + ".");
    return;
  }
  if (verb == "minorinterval") {
    if (f.size() < 3 || f[1].empty() || f[2].empty()) {
      usage();
      return;
    }
    AlignmentLabelSet* s = AlignmentLabelSets::Find(st.alignmentLabelSets, f[1]);
    if (s == nullptr) {
      log.push_back("ALIGNMENTLABELSET - no label set named \"" + f[1] + "\".");
      return;
    }
    const double v = std::strtod(f[2].c_str(), nullptr);
    if (!(v > 0.0)) {
      log.push_back("ALIGNMENTLABELSET - minor interval must be positive.");
      return;
    }
    PushUndoSnapshot(st, "Alignment label set");
    s->minorInterval = v;
    BumpCadGpuCache(st);
    log.push_back("ALIGNMENTLABELSET - \"" + s->name + "\" minor interval set to " + Lin(st, v) + ".");
    return;
  }
  if (verb == "show" || verb == "hide") {
    if (f.size() < 3 || f[1].empty() || f[2].empty()) {
      usage();
      return;
    }
    AlignmentLabelSet* s = AlignmentLabelSets::Find(st.alignmentLabelSets, f[1]);
    if (s == nullptr) {
      log.push_back("ALIGNMENTLABELSET - no label set named \"" + f[1] + "\".");
      return;
    }
    const std::string which = Lower(f[2]);
    const bool show = verb == "show";
    // REQ-492.2 / Civil 3D: minor stations are placed from major stations, so the two cannot be split apart.
    if (which == "major" && !show && s->minorStationsOn) {
      log.push_back("ALIGNMENTLABELSET - refused: \"" + s->name + "\" has minor stations shown, and they need the major stations. Hide MINOR first.");
      return;
    }
    if (which == "minor" && show && !s->majorStationsOn) {
      log.push_back("ALIGNMENTLABELSET - refused: minor stations need the major stations. Show MAJOR first.");
      return;
    }
    PushUndoSnapshot(st, "Alignment label set");
    if (which == "major")
      s->majorStationsOn = show;
    else if (which == "minor")
      s->minorStationsOn = show;
    else if (which == "equations" || which == "equation")  // REQ-492.1 Station Equations item
      s->equationsOn = show;
    else if (which == "geometry")  // REQ-492.1 Geometry Points item (D-2026-11-04-c)
      s->geometryPointsOn = show;
    else if (which == "speeds" || which == "speed")  // REQ-492.1 Design Speeds item
      s->designSpeedsOn = show;
    else if (which == "profile")  // REQ-492.1 placeholder items
      s->profileGeometryPointsOn = show;
    else if (which == "superelevation")
      s->superelevationPointsOn = show;
    else {
      log.push_back("ALIGNMENTLABELSET - component must be MAJOR, MINOR, EQUATIONS, GEOMETRY, SPEEDS, PROFILE or "
                    "SUPERELEVATION, not \"" + f[2] + "\".");
      return;
    }
    BumpCadGpuCache(st);
    log.push_back("ALIGNMENTLABELSET - \"" + s->name + "\" " + f[2] + " " + (verb == "show" ? "shown." : "hidden."));
    if (show && (which == "profile" || which == "superelevation"))
      log.push_back("  NOTE: this item is a placeholder; it labels nothing until alignments have " +
                    std::string(which == "profile" ? "profiles." : "superelevation."));
    return;
  }
  if (verb == "style") {
    RunAlignmentLabelSetStyle(st, f, log);
    return;
  }
  if (verb == "geometry") {
    RunAlignmentLabelSetGeometry(st, f, log);
    return;
  }
  usage();
}

namespace {

// ALIGNMENTLABELSTYLE SET, <style>, PRECISION|PREFIX|SUFFIX|ORIENTATION, <value>: REQ-492.5 formatting. `which` is
// lower case. Returns false with the reason logged.
bool EditAlignmentLabelFormat(AlignmentLabelStyle& s, const std::string& which, const std::string& value,
                              std::vector<std::string>& log) {
  assert(!which.empty());
  assert(!s.name.empty());
  const std::string v = Lower(value);
  if (which == "precision") {
    double d = 0.0;
    if (v == "drawing") {
      s.precision = -1;
      return true;
    }
    if (!ParseNum(value, &d) || d < 0.0 || d > 8.0 || d != std::floor(d)) {
      log.push_back("ALIGNMENTLABELSTYLE - precision must be 0 to 8 decimals, or DRAWING, not \"" + value + "\".");
      return false;
    }
    s.precision = static_cast<int>(d);
    return true;
  }
  if (which == "prefix" || which == "suffix") {
    (which == "prefix" ? s.prefix : s.suffix) = v == "none" ? std::string{} : value;
    return true;
  }
  if (which == "orientation" && (v == "object" || v == "view")) {
    s.viewOriented = v == "view";
    return true;
  }
  log.push_back("ALIGNMENTLABELSTYLE - property must be HEIGHT, COLOR, TEXTSTYLE, PRECISION, PREFIX, SUFFIX or "
                "ORIENTATION OBJECT|VIEW, not \"" + which + "\" = \"" + value + "\".");
  return false;
}

// ALIGNMENTLABELSTYLE SET, <style>, HEIGHT|COLOR|TEXTSTYLE, <value> (REQ-492.5). Returns false with the reason logged.
bool EditAlignmentLabelStyle(const AppCommandState& st, AlignmentLabelStyle& s, const std::string& item,
                             const std::string& value, std::vector<std::string>& log) {
  assert(!item.empty());
  assert(!s.name.empty());
  const std::string which = Lower(item);
  if (which == "height") {
    const double v = std::strtod(value.c_str(), nullptr);
    if (!(v > 0.0) || !std::isfinite(v)) {
      log.push_back("ALIGNMENTLABELSTYLE - height must be a positive plotted height in inches, not \"" + value + "\".");
      return false;
    }
    s.heightInches = v;
    return true;
  }
  if (which == "color" || which == "colour") {
    std::string storage;
    if (!CadColorStorageFromTyped(value, &storage)) {
      log.push_back("ALIGNMENTLABELSTYLE - \"" + value + "\" is not a colour (ByLayer, an index 1-255, a name such as "
                    "red, or #RRGGBB).");
      return false;
    }
    s.color = storage;
    return true;
  }
  if (which == "textstyle") {
    if (TextStyles::Find(st.textStyles, value) == nullptr) {
      log.push_back("ALIGNMENTLABELSTYLE - no text style named \"" + value + "\" (see STYLE).");
      return false;
    }
    s.textStyle = value;
    return true;
  }
  return EditAlignmentLabelFormat(s, which, value, log);
}

// REQ-478 pattern: Standard and a style a label set still uses cannot be deleted.
void RunAlignmentLabelStyleDelete(AppCommandState& st, const std::string& name, std::vector<std::string>& log) {
  assert(!name.empty());
  assert(!st.alignmentLabelStyles.empty());
  if (name == AlignmentLabelStyles::kStandardName) {
    log.push_back("ALIGNMENTLABELSTYLE - \"Standard\" cannot be deleted.");
    return;
  }
  const auto it = std::find_if(st.alignmentLabelStyles.begin(), st.alignmentLabelStyles.end(),
                               [&](const AlignmentLabelStyle& s) { return s.name == name; });
  if (it == st.alignmentLabelStyles.end()) {
    log.push_back("ALIGNMENTLABELSTYLE - no label style named \"" + name + "\".");
    return;
  }
  const int users = AlignmentLabelStyles::CountLabelSetsUsing(st.alignmentLabelSets, name);
  if (users > 0) {
    log.push_back("ALIGNMENTLABELSTYLE - \"" + name + "\" is used by " + std::to_string(users) +
                  " label set(s) and cannot be deleted; assign another style first (ALIGNMENTLABELSET STYLE).");
    return;
  }
  PushUndoSnapshot(st, "Alignment label style");
  st.alignmentLabelStyles.erase(std::find_if(st.alignmentLabelStyles.begin(), st.alignmentLabelStyles.end(),
                                             [&](const AlignmentLabelStyle& s) { return s.name == name; }));
  log.push_back("ALIGNMENTLABELSTYLE - deleted label style \"" + name + "\".");
}

}  // namespace

// ALIGNMENTLABELSTYLE LIST | NEW <style> | DELETE <style> | SET <style>, HEIGHT|COLOR|TEXTSTYLE, <value>  (REQ-492.5)
void RunAlignmentLabelStyle(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  AlignmentLabelStyles::EnsureStandard(st.alignmentLabelStyles);
  const std::vector<std::string> f = SplitFields(Trim(args));
  const std::string verb = f.empty() ? std::string{} : Lower(f[0]);
  const auto usage = [&] {
    log.push_back("ALIGNMENTLABELSTYLE - usage: ALIGNMENTLABELSTYLE LIST | NEW, <style> | DELETE, <style> | "
                  "SET, <style>, HEIGHT|COLOR|TEXTSTYLE|PRECISION|PREFIX|SUFFIX|ORIENTATION, <value>.");
  };
  if (verb == "list") {
    for (const AlignmentLabelStyle& s : st.alignmentLabelStyles)
      log.push_back("  LABEL STYLE \"" + s.name + "\"  height=" + FormatLinear(s.heightInches, 3) + "in  colour=" +
                    CadColorDisplayLabel(s.color) + "  text style=" + s.textStyle + "  precision=" +
                    (s.precision < 0 ? std::string("drawing") : std::to_string(s.precision)) +
                    (s.prefix.empty() ? "" : "  prefix=\"" + s.prefix + "\"") +
                    (s.suffix.empty() ? "" : "  suffix=\"" + s.suffix + "\"") +
                    "  orientation=" + (s.viewOriented ? "view" : "object") + "  (used by " +
                    std::to_string(AlignmentLabelStyles::CountLabelSetsUsing(st.alignmentLabelSets, s.name)) +
                    " label set(s))");
    return;
  }
  if (f.size() < 2 || f[1].empty()) {
    usage();
    return;
  }
  if (verb == "new") {
    if (AlignmentLabelStyles::Find(st.alignmentLabelStyles, f[1]) != nullptr) {
      log.push_back("ALIGNMENTLABELSTYLE - a label style named \"" + f[1] + "\" already exists.");
      return;
    }
    PushUndoSnapshot(st, "Alignment label style");
    AlignmentLabelStyle s = AlignmentLabelStyles::StandardLabelStyle();
    s.name = f[1];
    st.alignmentLabelStyles.push_back(std::move(s));
    log.push_back("ALIGNMENTLABELSTYLE - created label style \"" + f[1] + "\" (copied from Standard).");
    return;
  }
  if (verb == "delete") {
    RunAlignmentLabelStyleDelete(st, f[1], log);
    return;
  }
  if (verb == "set" && f.size() >= 4) {
    const AlignmentLabelStyle* current = AlignmentLabelStyles::Find(st.alignmentLabelStyles, f[1]);
    if (current == nullptr) {
      log.push_back("ALIGNMENTLABELSTYLE - no label style named \"" + f[1] + "\".");
      return;
    }
    AlignmentLabelStyle edited = *current;
    if (!EditAlignmentLabelStyle(st, edited, f[2], Trim(f[3]), log)) return;
    PushUndoSnapshot(st, "Alignment label style");
    *AlignmentLabelStyles::Find(st.alignmentLabelStyles, f[1]) = std::move(edited);
    BumpCadGpuCache(st);
    log.push_back("ALIGNMENTLABELSTYLE - \"" + f[1] + "\" " + f[2] + " set to " + Trim(f[3]) + ".");
    return;
  }
  usage();
}

// ALIGNMENTREPORT [<name>][, TXT|CSV[, <path>]]  (REQ-494)
namespace {

// REQ-502: the report names the alignment's own check set and its findings.
alignment::AlignmentReportFormat ReportFormatFor(const AppCommandState& st, const alignment::Alignment& a,
                                                 alignment::AlignmentReportFormat fmt) {
  const std::string criteriaName = AlignmentStyles::EffectiveCheckSetName(st.alignmentStyles, a);
  if (!criteriaName.empty()) fmt.checkSet = alignment::FindCheckSet(st.alignmentCheckSets, criteriaName);
  return fmt;
}

}  // namespace

void RunAlignmentReport(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitFields(args);
  std::string filterName;
  std::string exportKind;
  std::string path;
  if (f.size() >= 2) {
    const std::string verb = Lower(f[1]);
    if (verb == "txt" || verb == "csv") {
      exportKind = verb;
      filterName = f[0];
      if (f.size() >= 3) path = Trim(f[2]);
    }
  }
  if (exportKind.empty() && !f.empty()) filterName = Trim(f[0]);

  if (st.alignments.empty()) {
    log.push_back("ALIGNMENTREPORT - this drawing has no alignments.");
    return;
  }

  std::vector<const alignment::Alignment*> targets;
  for (const alignment::Alignment& a : st.alignments) {
    if (!filterName.empty() && Lower(a.name) != Lower(filterName)) continue;
    targets.push_back(&a);
  }
  if (!filterName.empty() && targets.empty()) {
    log.push_back("ALIGNMENTREPORT - no alignment named \"" + filterName + "\".");
    return;
  }

  alignment::AlignmentReportFormat fmt;
  fmt.linearPrecision = st.displayLinearPrecision;
  fmt.angles = CadAngleDisplaySettings(st);
  fmt.station = st.alignmentStationFormat;

  if (exportKind.empty()) {
    for (size_t i = 0; i < targets.size(); ++i) {
      std::vector<std::string> lines;
      alignment::BuildAlignmentReportLines(*targets[i], ReportFormatFor(st, *targets[i], fmt), &lines);
      for (const std::string& line : lines) log.push_back(line);
      if (targets.size() > 1 && i + 1 < targets.size()) log.push_back("---");
    }
    log.push_back("ALIGNMENTREPORT - " + std::to_string(targets.size()) + " alignment(s) reported.");
    return;
  }

  if (path.empty()) {
    char buf[1024] = {};
    const char* defaultName = exportKind == "csv" ? "alignment-report.csv" : "alignment-report.txt";
    const bool picked = exportKind == "csv" ? BrowseSaveFileCsvUtf8(buf, sizeof(buf), defaultName)
                                              : BrowseSaveFileTxtUtf8(buf, sizeof(buf), defaultName);
    if (!picked || buf[0] == '\0') {
      log.push_back("ALIGNMENTREPORT - cancelled.");
      return;
    }
    path = buf;
  }

  if (exportKind == "csv") {
    SheetGrid::Grid grid;
    bool header = false;
    for (const alignment::Alignment* a : targets) {
      SheetGrid::Grid part = alignment::BuildAlignmentReportCsv(*a, ReportFormatFor(st, *a, fmt));
      if (part.empty()) continue;
      if (!header) {
        grid.push_back(part.front());
        header = true;
      }
      for (size_t r = 1; r < part.size(); ++r) grid.push_back(std::move(part[r]));
    }
    std::string err;
    if (!SpreadsheetIo::WriteCsv(path, grid, &err)) {
      log.push_back("ALIGNMENTREPORT - could not write CSV \"" + path + "\": " + err + ".");
      return;
    }
    log.push_back("ALIGNMENTREPORT - CSV written to \"" + path + "\" (" + std::to_string(grid.size() - 1) +
                  " entity row(s)).");
    return;
  }

  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    log.push_back("ALIGNMENTREPORT - could not write \"" + path + "\".");
    return;
  }
  for (size_t i = 0; i < targets.size(); ++i) {
    std::vector<std::string> lines;
    alignment::BuildAlignmentReportLines(*targets[i], ReportFormatFor(st, *targets[i], fmt), &lines);
    for (size_t li = 0; li < lines.size(); ++li) {
      out << lines[li];
      if (li + 1 < lines.size()) out << '\n';
    }
    if (targets.size() > 1 && i + 1 < targets.size()) out << "\n---\n";
  }
  log.push_back("ALIGNMENTREPORT - report written to \"" + path + "\".");
}

// ALIGNMENTSURFACE <alignment>, <surface>, STATION=<station>[, OFFSET=<offset>]   one point      - REQ-497.2
//                  <alignment>, <surface>, INTERVAL=<interval>                    sampled table   - REQ-497.3
// The ground is read from the named surface. Elevations are session data: nothing is stored on the alignment.
void RunAlignmentSurface(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitFields(args);
  const char* usage =
      "ALIGNMENTSURFACE - usage: ALIGNMENTSURFACE <alignment>, <surface>, STATION=<station>[, OFFSET=<offset>] | "
      "<alignment>, <surface>, INTERVAL=<interval>.";
  if (f.size() < 3) {
    log.push_back(usage);
    return;
  }
  alignment::Alignment* a = Resolve(st, "ALIGNMENTSURFACE", f[0], log);
  if (a == nullptr) return;
  const int si = FindSurfaceIndex(st, f[1]);
  if (si < 0) {
    log.push_back("ALIGNMENTSURFACE - no surface named \"" + f[1] + "\".");
    return;
  }
  std::unique_ptr<ISurfaceQuery> ground = MakeSurfaceQueryForCommands(st, st.cadSurfaces[static_cast<size_t>(si)]);
  if (!ground) {
    log.push_back("ALIGNMENTSURFACE - \"" + f[1] + "\" is not a built surface.");
    return;
  }
  std::string station, offsetText, intervalText;
  for (size_t i = 2; i < f.size(); ++i) {
    const std::string key = Lower(f[i].substr(0, f[i].find('=')));
    const std::string value = f[i].find('=') == std::string::npos ? "" : Trim(f[i].substr(f[i].find('=') + 1));
    if (key == "station") station = value;
    else if (key == "offset") offsetText = value;
    else if (key == "interval") intervalText = value;
    else {
      log.push_back(usage);
      return;
    }
  }
  const std::string unit = " (surface \"" + f[1] + "\")";
  if (!intervalText.empty()) {
    double interval = 0.0;
    if (!ParseNum(intervalText, &interval) || !(interval > 0.0)) {
      log.push_back("ALIGNMENTSURFACE - the interval must be a positive number.");
      return;
    }
    std::vector<alignment::GroundPoint> samples;
    std::string err;
    if (!alignment::SampleGround(*a, *ground, interval, 100000, &samples, &err)) {
      log.push_back("ALIGNMENTSURFACE - " + err + ".");
      return;
    }
    int withGround = 0;
    for (const alignment::GroundPoint& g : samples) {
      if (g.hasGround) ++withGround;
    }
    log.push_back("ALIGNMENTSURFACE - \"" + a->name + "\": " + std::to_string(samples.size()) + " sample(s), " +
                  std::to_string(withGround) + " with ground" + unit + ".");
    for (const alignment::GroundPoint& g : samples) {
      const std::string sta = Sta(st, *a, g.internalStation);
      log.push_back("  STA " + sta + "  " + (g.hasGround ? "EG = " + Lin(st, g.ground) : std::string("no ground")));
    }
    return;
  }
  if (station.empty()) {
    log.push_back(usage);
    return;
  }
  double displayed = 0.0, offset = 0.0;
  if (!ParseSta(st, station, &displayed)) {
    log.push_back("ALIGNMENTSURFACE - \"" + station + "\" is not a station.");
    return;
  }
  if (!offsetText.empty() && !ParseNum(offsetText, &offset)) {
    log.push_back("ALIGNMENTSURFACE - the offset must be a number.");
    return;
  }
  const std::vector<double> raws = a->RawStationsForDisplayed(displayed);
  if (raws.empty()) {
    log.push_back("ALIGNMENTSURFACE - station " + StaFmt(st, displayed) + " is outside \"" + a->name + "\".");
    return;
  }
  for (const double raw : raws) {
    const alignment::GroundPoint g = alignment::GroundAt(*a, *ground, raw, offset);
    const std::string head = "ALIGNMENTSURFACE - STA " + StaFmt(st, displayed) + "  OFF " + Lin(st, offset);
    if (g.hasGround) {
      log.push_back(head + "  EG = " + Lin(st, g.ground) + unit + ".");
    } else {
      log.push_back(head + "  no ground: the point is off the surface" + unit + ".");
    }
  }
}

// ALIGNMENTLABEL <name>, STATIONLABEL, FIXED, <x>, <y>        a point that stays put (REQ-492.4)
//                  <name>, STATIONLABEL, RELATIVE, <station>, <offset>   a station/offset that moves with the alignment
// The new label's text is reported straight away. Labels belong to the alignment: deleting it removes them (REQ-492.6).
void RunAlignmentStationLabel(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitFields(args);
  const char* usage = "ALIGNMENTLABEL - usage: ALIGNMENTLABEL <name>, STATIONLABEL, FIXED, <x>, <y> | <name>, STATIONLABEL, RELATIVE, <station>, <offset> | <name>, STATIONLABEL, LIST | <name>, STATIONLABEL, MOVE, <number>, <x>, <y>.";
  if (f.size() < 3) {
    log.push_back(usage);
    return;
  }
  alignment::Alignment* a = Resolve(st, "ALIGNMENTLABEL", f[0], log);
  if (a == nullptr) return;
  const std::string sub = Lower(f[2]);
  if (sub == "list" && f.size() == 3) {  // REQ-492.4: the numbers MOVE takes
    if (a->stationLabels.empty()) log.push_back("ALIGNMENTLABEL - \"" + a->name + "\" has no station labels.");
    for (size_t i = 0; i < a->stationLabels.size(); ++i) {
      const alignment::StationOffsetLabelPlacement p = alignment::PlaceStationOffsetLabel(*a, a->stationLabels[i], st.displayLinearPrecision);
      log.push_back("  LABEL " + std::to_string(i + 1) + "  " + (a->stationLabels[i].fixed ? "fixed" : "relative") + "  " + p.text +
                    (p.onAlignment ? "" : " (off the alignment)"));
    }
    return;
  }
  if (sub == "move" && f.size() == 6) {  // REQ-492.5: drag a label to a new point
    double number = 0.0, x = 0.0, y = 0.0;
    if (!ParseNum(f[3], &number) || number != std::floor(number) || number < 1.0 ||
        number > static_cast<double>(a->stationLabels.size()) || !ParseNum(f[4], &x) || !ParseNum(f[5], &y)) {
      log.push_back("ALIGNMENTLABEL - MOVE needs a label number (1 to " + std::to_string(a->stationLabels.size()) + ") and a point.");
      return;
    }
    double lx = 0.0, ly = 0.0;
    CadCoord::LocalFromWorld(st, x, y, &lx, &ly);
    alignment::StationOffsetLabel moved = a->stationLabels[static_cast<size_t>(number) - 1];
    if (!alignment::MoveStationOffsetLabelTo(*a, &moved, {lx, ly})) {
      log.push_back("ALIGNMENTLABEL - \"" + a->name + "\" has no geometry to label.");
      return;
    }
    PushUndoSnapshot(st, "Move alignment station label");
    a = Resolve(st, "ALIGNMENTLABEL", f[0], log);
    a->stationLabels[static_cast<size_t>(number) - 1] = moved;
    const alignment::StationOffsetLabelPlacement p = alignment::PlaceStationOffsetLabel(*a, moved, st.displayLinearPrecision);
    log.push_back("ALIGNMENTLABEL - label " + std::to_string(static_cast<int>(number)) + " moved: " + p.text + ".");
    return;
  }
  if (f.size() != 5) {
    log.push_back(usage);
    return;
  }
  if (a->Empty()) {
    log.push_back("ALIGNMENTLABEL - \"" + a->name + "\" has no geometry to label.");
    return;
  }
  alignment::StationOffsetLabel label;
  const std::string mode = sub;
  if (mode == "fixed") {
    double x = 0.0, y = 0.0;
    if (!ParseNum(f[3], &x) || !ParseNum(f[4], &y)) {
      log.push_back("ALIGNMENTLABEL - the point must be two numbers.");
      return;
    }
    label.fixed = true;
    double lx = 0.0, ly = 0.0;
    CadCoord::LocalFromWorld(st, x, y, &lx, &ly);
    label.fixedPoint = {lx, ly};
  } else if (mode == "relative") {
    double station = 0.0, offset = 0.0;
    if (!ParseSta(st, f[3], &station) || !ParseNum(f[4], &offset)) {
      log.push_back("ALIGNMENTLABEL - the station must be a station (12+45.32) and the offset a number.");
      return;
    }
    const std::vector<double> raws = a->RawStationsForDisplayed(station);
    if (raws.empty()) {
      log.push_back("ALIGNMENTLABEL - station " + StaFmt(st, station) + " is outside \"" + a->name + "\".");
      return;
    }
    label.internalStation = raws.front();  // a station that occurs twice (REQ-483.3) takes the first place
    label.offset = offset;
  } else {
    log.push_back(usage);
    return;
  }
  PushUndoSnapshot(st, "Add alignment station label");
  a = Resolve(st, "ALIGNMENTLABEL", f[0], log);
  a->stationLabels.push_back(label);
  const alignment::StationOffsetLabelPlacement p = alignment::PlaceStationOffsetLabel(*a, label, st.displayLinearPrecision);
  log.push_back("ALIGNMENTLABEL - " + std::string(label.fixed ? "fixed" : "relative") + " station label on \"" + a->name + "\": " + p.text +
                (p.onAlignment ? "" : " (off the alignment)") + ".");
}

// ======================================================================================================
// REQ-492.5: dragging a station label in the viewport. Same shape as the entity grip drag: press arms it, move updates
// it live, release commits one undo step, Esc or a cancel puts it back.
// ======================================================================================================

bool TryBeginAlignmentLabelDrag(AppCommandState& st, float lx, float ly, float tolWorld) {
  float bestD2 = tolWorld * tolWorld;
  int bestA = -1, bestL = -1;
  for (size_t ai = 0; ai < st.alignments.size(); ++ai) {
    const alignment::Alignment& a = st.alignments[ai];
    for (size_t li = 0; li < a.stationLabels.size(); ++li) {
      const alignment::StationOffsetLabelPlacement p =
          alignment::PlaceStationOffsetLabel(a, a.stationLabels[li], st.displayLinearPrecision);
      if (!p.onAlignment) continue;
      const double dx = lx - p.point.x, dy = ly - p.point.y;
      const float d2 = static_cast<float>(dx * dx + dy * dy);
      if (d2 <= bestD2) {
        bestD2 = d2;
        bestA = static_cast<int>(ai);
        bestL = static_cast<int>(li);
      }
    }
  }
  if (bestA < 0) return false;
  st.alignLabelDragActive = true;
  st.alignLabelDragMoved = false;
  st.alignLabelDragAlignment = bestA;
  st.alignLabelDragIndex = bestL;
  st.alignLabelDragOriginal = st.alignments[static_cast<size_t>(bestA)].stationLabels[static_cast<size_t>(bestL)];
  return true;
}

void ApplyAlignmentLabelDrag(AppCommandState& st, float lx, float ly) {
  if (!st.alignLabelDragActive) return;
  if (st.alignLabelDragAlignment < 0 || static_cast<size_t>(st.alignLabelDragAlignment) >= st.alignments.size()) return;
  alignment::Alignment& a = st.alignments[static_cast<size_t>(st.alignLabelDragAlignment)];
  if (st.alignLabelDragIndex < 0 || static_cast<size_t>(st.alignLabelDragIndex) >= a.stationLabels.size()) return;
  alignment::StationOffsetLabel& label = a.stationLabels[static_cast<size_t>(st.alignLabelDragIndex)];
  if (alignment::MoveStationOffsetLabelTo(a, &label, {static_cast<double>(lx), static_cast<double>(ly)})) {
    st.alignLabelDragMoved = true;
    BumpCadGpuCache(st);
  }
}

void FinishAlignmentLabelDrag(AppCommandState& st, bool commit, std::vector<std::string>& log) {
  if (!st.alignLabelDragActive) return;
  const int ai = st.alignLabelDragAlignment, li = st.alignLabelDragIndex;
  const bool valid = ai >= 0 && static_cast<size_t>(ai) < st.alignments.size() && li >= 0 &&
                     static_cast<size_t>(li) < st.alignments[static_cast<size_t>(ai)].stationLabels.size();
  st.alignLabelDragActive = false;
  if (!valid) return;
  alignment::Alignment& a = st.alignments[static_cast<size_t>(ai)];
  auto& label = a.stationLabels[static_cast<size_t>(li)];
  if (!commit || !st.alignLabelDragMoved) {  // cancel, or a click that did not move: nothing to record
    label = st.alignLabelDragOriginal;
    BumpCadGpuCache(st);
    return;
  }
  // One undo step for the whole drag: restore the original, take the snapshot, then put the moved label back.
  const alignment::StationOffsetLabel moved = label;
  label = st.alignLabelDragOriginal;
  PushUndoSnapshot(st, "Move alignment station label");
  st.alignments[static_cast<size_t>(ai)].stationLabels[static_cast<size_t>(li)] = moved;
  BumpCadGpuCache(st);
  const alignment::StationOffsetLabelPlacement p = alignment::PlaceStationOffsetLabel(
      st.alignments[static_cast<size_t>(ai)], moved, st.displayLinearPrecision);
  log.push_back("ALIGNMENTLABEL - label " + std::to_string(li + 1) + " moved: " + p.text + ".");
}

// EDITALIGNMENTSTYLE [<style>] - REQ-491. Opens the Alignment Style editor in the Qt editor; headless it lists the style.
void RunEditAlignmentStyle(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  AlignmentStyles::EnsureStandard(st.alignmentStyles);
  std::string name = Trim(args);
  if (name.empty() && !st.alignmentStyles.empty()) name = st.alignmentStyles.front().name;
  if (AlignmentStyles::Find(st.alignmentStyles, name) == nullptr) {
    log.push_back("EDITALIGNMENTSTYLE - no style named \"" + name + "\".");
    return;
  }
  st.alignmentStyleEditorRequested = true;
  st.alignmentStyleEditorName = name;
  RunAlignmentStyle(st, "LIST", log);  // the same list, for a window-less run
}

// ======================================================================================================
// REQ-498: feature lines made from an alignment (issue #121 Phase 5).
// ======================================================================================================

namespace {

int AlignmentIndexById(const AppCommandState& st, std::uint64_t id) {
  for (size_t i = 0; i < st.alignments.size(); ++i) {
    if (st.alignments[i].id == id) return static_cast<int>(i);
  }
  return -1;
}

// Plan vertices for one feature line, with Z from its surface or its constant (REQ-498.1). Fails with the reason when a
// station has no ground on the surface, rather than writing a zero elevation (REQ-497.1).
bool BuildFeatureLineVerts(const AppCommandState& st, const alignment::Alignment& a, const CadFeatureLineInfo& info,
                           std::vector<double>* xyz, std::string* why) {
  const std::vector<double> stations = alignment::FeatureLineStations(a, info.sampleInterval, info.chordTolerance);
  if (stations.size() < 2) {
    if (why) *why = "the alignment has too little geometry to sample";
    return false;
  }
  std::unique_ptr<ISurfaceQuery> ground;
  if (info.hasSurface) {
    const int si = FindSurfaceIndex(st, info.surfaceName);
    if (si < 0) {
      if (why) *why = "the surface \"" + info.surfaceName + "\" is not in the drawing";
      return false;
    }
    ground = MakeSurfaceQueryForCommands(st, st.cadSurfaces[static_cast<size_t>(si)]);
    if (!ground) {
      if (why) *why = "the surface \"" + info.surfaceName + "\" is not a built surface";
      return false;
    }
  }
  xyz->clear();
  for (const double s : stations) {
    const double internal = a.StartStation() + s;
    const alignment::Point2 p = a.GetPointAtStation(internal);
    double z = info.constantZ;
    if (ground) {
      const alignment::GroundPoint g = alignment::GroundAt(a, *ground, internal, 0.0);
      if (!g.hasGround) {
        if (why) {
          *why = "there is no ground on \"" + info.surfaceName + "\" at station " +
                 StaFmt(st, a.DisplayedStation(internal, true));
        }
        return false;
      }
      z = g.ground;
    }
    xyz->push_back(p.x);
    xyz->push_back(p.y);
    xyz->push_back(z);
  }
  return true;
}

// Replaces the vertices of feature line `index` in place, shifting every later line's offsets by the change in count.
void ReplaceFeatureLineVerts(AppCommandState& st, size_t index, const std::vector<double>& xyz) {
  if (index + 1 >= st.featureLineOffsets.size()) return;
  const int a = st.featureLineOffsets[index], b = st.featureLineOffsets[index + 1];
  const int oldCount = b - a, newCount = static_cast<int>(xyz.size() / 3);
  const bool flagsMatch = st.featureLineElevPt.size() == st.featureLineVerts.size() / 3;
  st.featureLineVerts.erase(st.featureLineVerts.begin() + static_cast<std::ptrdiff_t>(a) * 3,
                            st.featureLineVerts.begin() + static_cast<std::ptrdiff_t>(b) * 3);
  st.featureLineVerts.insert(st.featureLineVerts.begin() + static_cast<std::ptrdiff_t>(a) * 3, xyz.begin(), xyz.end());
  if (flagsMatch) {  // the vertices are elevation points, the flag FEATURELINE gives a typed elevation
    st.featureLineElevPt.erase(st.featureLineElevPt.begin() + a, st.featureLineElevPt.begin() + b);
    st.featureLineElevPt.insert(st.featureLineElevPt.begin() + a, static_cast<size_t>(newCount), static_cast<uint8_t>(1));
  }
  const int delta = newCount - oldCount;
  for (size_t j = index + 1; j < st.featureLineOffsets.size(); ++j) st.featureLineOffsets[j] += delta;
}

}  // namespace

// REQ-498.2: rebuilds every feature line that names an alignment, and drops the link (with a log line) when that alignment
// is gone. A line whose surface or ground can no longer be read keeps its last vertices and says why.
void RefreshFeatureLineLinks(AppCommandState& st, std::vector<std::string>& log) {
  bool changed = false;
  for (size_t i = 0; i < st.featureLineInfo.size(); ++i) {
    CadFeatureLineInfo& info = st.featureLineInfo[i];
    if (info.sourceAlignmentId == 0) continue;
    const int ai = AlignmentIndexById(st, info.sourceAlignmentId);
    if (ai < 0) {
      log.push_back("  Feature line \"" + info.name + "\" lost its alignment and is now an ordinary feature line.");
      info.sourceAlignmentId = 0;
      continue;
    }
    std::vector<double> xyz;
    std::string why;
    if (!BuildFeatureLineVerts(st, st.alignments[static_cast<size_t>(ai)], info, &xyz, &why)) {
      log.push_back("  Feature line \"" + info.name + "\" was not updated: " + why + ".");
      continue;
    }
    ReplaceFeatureLineVerts(st, i, xyz);
    changed = true;
  }
  if (changed) BumpCadGpuCache(st);
}

// FEATURELINEFROMALIGNMENT <alignment>[, INTERVAL=<n>][, TOL=<n>][, SURFACE=<name> | Z=<value>]   (REQ-498.1)
void RunFeatureLineFromAlignment(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitFields(args);
  const char* usage =
      "FEATURELINEFROMALIGNMENT - usage: FEATURELINEFROMALIGNMENT <alignment>[, INTERVAL=<n>][, TOL=<n>][, SURFACE=<name> | Z=<value>].";
  if (f.empty() || f[0].empty()) {
    log.push_back(usage);
    return;
  }
  alignment::Alignment* a = Resolve(st, "FEATURELINEFROMALIGNMENT", f[0], log);
  if (a == nullptr) return;
  if (a->Empty()) {
    log.push_back("FEATURELINEFROMALIGNMENT - \"" + a->name + "\" has no geometry.");
    return;
  }
  CadFeatureLineInfo info;
  info.name = a->name;
  info.description = "From alignment " + a->name;
  info.sourceAlignmentId = a->id;
  for (size_t i = 1; i < f.size(); ++i) {
    const size_t eq = f[i].find('=');
    const std::string key = Lower(f[i].substr(0, eq));
    const std::string value = eq == std::string::npos ? "" : Trim(f[i].substr(eq + 1));
    double v = 0.0;
    if (key == "interval" && ParseNum(value, &v) && v > 0.0) {
      info.sampleInterval = v;
    } else if (key == "tol" && ParseNum(value, &v) && v > 0.0) {
      info.chordTolerance = v;
    } else if (key == "surface" && !value.empty()) {
      if (FindSurfaceIndex(st, value) < 0) {
        log.push_back("FEATURELINEFROMALIGNMENT - no surface named \"" + value + "\".");
        return;
      }
      info.hasSurface = true;
      info.surfaceName = value;
    } else if (key == "z" && ParseNum(value, &v)) {
      info.hasSurface = false;
      info.constantZ = v;
    } else {
      log.push_back(usage);
      return;
    }
  }
  std::vector<double> xyz;
  std::string why;
  if (!BuildFeatureLineVerts(st, *a, info, &xyz, &why)) {
    log.push_back("FEATURELINEFROMALIGNMENT - " + why + ". No feature line was made.");
    return;
  }
  PushUndoSnapshot(st, "Feature line from alignment");
  if (st.featureLineOffsets.empty()) st.featureLineOffsets.push_back(0);
  const int base = st.featureLineOffsets.back();
  st.featureLineVerts.insert(st.featureLineVerts.end(), xyz.begin(), xyz.end());
  st.featureLineElevPt.resize(st.featureLineVerts.size() / 3, static_cast<uint8_t>(1));
  st.featureLineOffsets.push_back(base + static_cast<int>(xyz.size() / 3));
  st.featureLineClosed.push_back(0);
  const std::string zText = info.hasSurface ? "Z from \"" + info.surfaceName + "\"" : std::string("constant Z");
  const size_t count = xyz.size() / 3;
  st.featureLineInfo.push_back(std::move(info));
  AppendNewObjectAttrs(st, st.featureLineAttrs, st.featureLineInfo.size(), ObjectLayerKind::FeatureLine,
                       st.featureLineInfo.back().name);  // REQ-361
  EnsureAttrCounts(st);
  BumpCadGpuCache(st);
  log.push_back("FEATURELINEFROMALIGNMENT - feature line \"" + a->name + "\" made from the alignment: " +
                std::to_string(count) + " vertices, " + zText + ".");
}

// ======================================================================================================
// REQ-500: the profile seam, and QUICKPROFILE along an alignment (issue #121 Phase 5).
// ======================================================================================================

// QUICKPROFILE <surface>, <alignment>   (REQ-500 AC3). Samples the surface along the alignment from its start to its end,
// one foot at a time (REQ-497.3), and stores the result in the same session graph the two-point command uses. Returns false
// only when no alignment has that name, so the caller can report its usage line instead.
bool RunQuickProfileAlongAlignment(AppCommandState& st, const std::string& surfaceName, const std::string& alignmentName,
                                   std::vector<std::string>& log) {
  const int ai = [&]() {
    const std::string want = Lower(alignmentName);
    for (size_t i = 0; i < st.alignments.size(); ++i) {
      if (Lower(st.alignments[i].name) == want) return static_cast<int>(i);
    }
    return -1;
  }();
  if (ai < 0) return false;
  const alignment::Alignment& a = st.alignments[static_cast<size_t>(ai)];
  const int si = FindSurfaceIndex(st, surfaceName);
  if (si < 0) {
    log.push_back("QUICKPROFILE - no surface named \"" + surfaceName + "\".");
    return true;
  }
  if (a.Empty()) {
    log.push_back("QUICKPROFILE - \"" + a.name + "\" has no geometry to sample.");
    return true;
  }
  std::unique_ptr<ISurfaceQuery> ground = MakeSurfaceQueryForCommands(st, st.cadSurfaces[static_cast<size_t>(si)]);
  if (!ground) {
    log.push_back("QUICKPROFILE - \"" + surfaceName + "\" is not a built surface.");
    return true;
  }
  std::vector<alignment::GroundPoint> groundSamples;
  std::string err;
  if (!alignment::SampleGround(a, *ground, kQuickProfileStepFt, kQuickProfileMaxSamples, &groundSamples, &err)) {
    log.push_back("QUICKPROFILE - " + err + ".");
    return true;
  }
  std::vector<SurfaceProfileSample> samples;
  samples.reserve(groundSamples.size());
  int hits = 0;
  double minZ = 0.0, maxZ = 0.0;
  for (const alignment::GroundPoint& g : groundSamples) {
    SurfaceProfileSample p;
    p.station = g.internalStation - a.StartStation();
    p.x = g.point.x;
    p.y = g.point.y;
    p.z = g.ground;
    p.onSurface = g.hasGround;
    if (p.onSurface) {
      minZ = hits == 0 ? p.z : std::min(minZ, p.z);
      maxZ = hits == 0 ? p.z : std::max(maxZ, p.z);
      ++hits;
    }
    samples.push_back(p);
  }
  if (hits == 0) {
    log.push_back("QUICKPROFILE - no samples on the surface (outside).");
    return true;
  }
  const int p = st.displayLinearPrecision;
  st.quickProfile.open = true;
  st.quickProfile.hasResult = true;
  st.quickProfile.surfaceName = surfaceName;
  st.quickProfile.length = samples.back().station;
  st.quickProfile.onSurfaceCount = hits;
  st.quickProfile.minZ = minZ;
  st.quickProfile.maxZ = maxZ;
  st.quickProfile.samples = std::move(samples);
  log.push_back("QUICKPROFILE - \"" + surfaceName + "\" along \"" + a.name + "\": length " + FormatLinear(st.quickProfile.length, p) +
                ", " + std::to_string(st.quickProfile.samples.size()) + " sample(s), " + std::to_string(hits) +
                " on surface, elev " + FormatLinear(minZ, p) + " to " + FormatLinear(maxZ, p) + ".");
  return true;
}

// ======================================================================================================
// REQ-501: alignment properties and COGO (issue #121 Phase 5).
// ======================================================================================================

// ALIGNMENTPROPS <alignment>, NAME|DESCRIPTION|STYLE|LAYER|START, <value>   (REQ-501.1)
// The editable Properties fields. Names stay unique; a style must exist; the start station cannot change while the
// alignment has station equations, because the equations are stated against the old stations (REQ-483.1).
void RunAlignmentProps(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitFields(args);
  if (f.size() != 3) {
    log.push_back("ALIGNMENTPROPS - usage: ALIGNMENTPROPS <alignment>, NAME|DESCRIPTION|STYLE|LAYER|START, <value>.");
    return;
  }
  alignment::Alignment* a = Resolve(st, "ALIGNMENTPROPS", f[0], log);
  if (a == nullptr) return;
  const std::string field = Lower(f[1]);
  const std::string value = Trim(f[2]);
  alignment::Alignment edited = *a;
  if (field == "name") {
    if (value.empty()) {
      log.push_back("ALIGNMENTPROPS - the name cannot be empty.");
      return;
    }
    if (FindAlignment(st, value) >= 0 && Lower(value) != Lower(a->name)) {
      log.push_back("ALIGNMENTPROPS - an alignment named \"" + value + "\" already exists; names must be unique.");
      return;
    }
    edited.name = value;
  } else if (field == "description") {
    edited.description = value;
  } else if (field == "style") {
    if (AlignmentStyles::Find(st.alignmentStyles, value) == nullptr) {
      log.push_back("ALIGNMENTPROPS - no style named \"" + value + "\".");
      return;
    }
    edited.style = value;
  } else if (field == "layer") {
    if (value.empty()) {
      log.push_back("ALIGNMENTPROPS - the layer cannot be empty.");
      return;
    }
    edited.layer = value;
  } else if (field == "start") {
    double sta = 0.0;
    if (!ParseSta(st, value, &sta)) {
      log.push_back("ALIGNMENTPROPS - \"" + value + "\" is not a station.");
      return;
    }
    if (!edited.equations.empty()) {
      log.push_back("ALIGNMENTPROPS - \"" + a->name + "\" has " + std::to_string(edited.equations.size()) +
                    " station equation(s), stated against the current stations. Remove them with ALIGNMENTSTATION first.");
      return;
    }
    edited.startingStation = sta;
  } else {
    log.push_back("ALIGNMENTPROPS - field must be NAME, DESCRIPTION, STYLE, LAYER or START, not \"" + f[1] + "\".");
    return;
  }
  const bool same = edited.name == a->name && edited.description == a->description && edited.style == a->style &&
                    edited.layer == a->layer && edited.startingStation == a->startingStation;
  if (same) {
    log.push_back("ALIGNMENTPROPS - " + f[1] + " is already \"" + value + "\".");
    return;
  }
  PushUndoSnapshot(st, "Alignment properties");
  *Resolve(st, "ALIGNMENTPROPS", f[0], log) = std::move(edited);
  BumpCadGpuCache(st);
  log.push_back("ALIGNMENTPROPS - \"" + value + "\" set as the " + f[1] + ".");
}

// REQ-502: design criteria. ALIGNMENTCHECKSET NEW|DELETE|LIST|SET manages drawing-owned check sets; ALIGNMENTCRITERIA applies
// one to an alignment; ALIGNMENTSPEED sets the design speed from a station; ALIGNMENTCHECK lists the failed checks.
namespace {

int FindCheckSetIndex(const AppCommandState& st, const std::string& name) {
  for (size_t i = 0; i < st.alignmentCheckSets.size(); ++i) {
    if (Lower(st.alignmentCheckSets[i].name) == Lower(name)) return static_cast<int>(i);
  }
  return -1;
}

}  // namespace

// ALIGNMENTCHECKSET NEW, <name>[, ROAD|RAIL]
// ALIGNMENTCHECKSET DELETE, <name>
// ALIGNMENTCHECKSET LIST
// ALIGNMENTCHECKSET SET, <name>, RADIUS, <speed>, <minRadius>   (minRadius 0 removes the row for that speed)
// ALIGNMENTCHECKSET SET, <name>, MINTANGENT|MINSPIRAL|MINA|MAXA|MINLINE|MAXDEFLECTION|SUPERELEVATION|FRICTION|SIGHT|CLEARANCE, <value>
//   (0 switches the check off; SUPERELEVATION and FRICTION are REQ-522 rates, SIGHT and CLEARANCE are REQ-522 feet)
void RunAlignmentCheckSet(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitFields(args);
  const char* usage =
      "ALIGNMENTCHECKSET - usage: ALIGNMENTCHECKSET NEW, <name>[, ROAD|RAIL] | DELETE, <name> | LIST | "
      "SET, <name>, RADIUS, <speed>, <minRadius> | SET, <name>, MINTANGENT|MINSPIRAL|MINA|MAXA|MINLINE|MAXDEFLECTION|"
      "SUPERELEVATION|FRICTION|SIGHT|CLEARANCE, <value> | LOAD, <file> | SAVE, <name>, <file>.";
  if (f.empty()) {
    log.push_back(usage);
    return;
  }
  const std::string verb = Lower(f[0]);
  if (verb == "list") {
    if (st.alignmentCheckSets.empty()) {
      log.push_back("ALIGNMENTCHECKSET - no check sets. Make one with ALIGNMENTCHECKSET NEW, <name>.");
      return;
    }
    for (const alignment::AlignmentCheckSet& s : st.alignmentCheckSets) {
      log.push_back("ALIGNMENTCHECKSET: " + s.name + "  [" + (s.kind == alignment::CheckSetKind::Rail ? "rail" : "road") +
                    ", " + std::to_string(s.radiusTable.size()) + " radius row(s), min tangent " +
                    FormatLinear(s.minTangent, st.displayLinearPrecision) + ", min spiral " +
                    FormatLinear(s.minSpiralLength, st.displayLinearPrecision) + ", A " +
                    FormatLinear(s.minA, st.displayLinearPrecision) + " to " +
                    FormatLinear(s.maxA, st.displayLinearPrecision) + "]");
    }
    return;
  }
  if (verb == "new") {
    if (f.size() < 2 || Trim(f[1]).empty()) {
      log.push_back("ALIGNMENTCHECKSET - the new set needs a name.");
      return;
    }
    const std::string name = Trim(f[1]);
    if (FindCheckSetIndex(st, name) >= 0) {
      log.push_back("ALIGNMENTCHECKSET - a check set named \"" + name + "\" already exists.");
      return;
    }
    alignment::AlignmentCheckSet made;
    made.name = name;
    if (f.size() >= 3) {
      const std::string kind = Lower(Trim(f[2]));
      if (kind == "rail") {
        made.kind = alignment::CheckSetKind::Rail;
      } else if (kind != "road") {
        log.push_back("ALIGNMENTCHECKSET - kind must be ROAD or RAIL, not \"" + Trim(f[2]) + "\".");
        return;
      }
    }
    PushUndoSnapshot(st, "New check set");
    st.alignmentCheckSets.push_back(made);
    log.push_back("ALIGNMENTCHECKSET - created \"" + name + "\" (" +
                  (made.kind == alignment::CheckSetKind::Rail ? "rail" : "road") + ").");
    return;
  }
  if (verb == "delete") {
    if (f.size() != 2) {
      log.push_back("ALIGNMENTCHECKSET - usage: ALIGNMENTCHECKSET DELETE, <name>.");
      return;
    }
    const int idx = FindCheckSetIndex(st, f[1]);
    if (idx < 0) {
      log.push_back("ALIGNMENTCHECKSET - no check set named \"" + Trim(f[1]) + "\".");
      return;
    }
    const std::string name = st.alignmentCheckSets[static_cast<size_t>(idx)].name;
    PushUndoSnapshot(st, "Delete check set");
    st.alignmentCheckSets.erase(st.alignmentCheckSets.begin() + idx);
    int cleared = 0;
    for (AlignmentStyle& style : st.alignmentStyles) {
      if (Lower(style.checkSetName) == Lower(name)) style.checkSetName.clear();
    }
    for (alignment::Alignment& a : st.alignments) {
      if (Lower(a.checkSetName) == Lower(name)) {
        a.checkSetName.clear();
        ++cleared;
      }
    }
    log.push_back("ALIGNMENTCHECKSET - deleted \"" + name + "\"" +
                  (cleared > 0 ? "; " + std::to_string(cleared) + " alignment(s) no longer have criteria." : "."));
    return;
  }
  if (verb == "set") {
    if (f.size() < 4) {
      log.push_back(usage);
      return;
    }
    const int idx = FindCheckSetIndex(st, f[1]);
    if (idx < 0) {
      log.push_back("ALIGNMENTCHECKSET - no check set named \"" + Trim(f[1]) + "\".");
      return;
    }
    alignment::AlignmentCheckSet edited = st.alignmentCheckSets[static_cast<size_t>(idx)];
    const std::string what = Lower(Trim(f[2]));
    if (what == "radius") {
      double speed = 0.0, minRadius = 0.0;
      if (f.size() != 5 || !ParseNum(f[3], &speed) || !ParseNum(f[4], &minRadius) || speed < 0.0 || minRadius < 0.0) {
        log.push_back("ALIGNMENTCHECKSET - usage: SET, <name>, RADIUS, <speed>, <minRadius> (both zero or more).");
        return;
      }
      auto& table = edited.radiusTable;
      table.erase(std::remove_if(table.begin(), table.end(),
                                 [speed](const alignment::RadiusRequirement& r) { return std::fabs(r.speed - speed) < 1e-9; }),
                  table.end());
      if (minRadius > 0.0) table.push_back({speed, minRadius});
      std::sort(table.begin(), table.end(),
                [](const alignment::RadiusRequirement& x, const alignment::RadiusRequirement& y) { return x.speed < y.speed; });
    } else if (what == "mintangent" || what == "minspiral" || what == "mina" || what == "maxa" ||
               what == "minline" || what == "maxdeflection" || what == "superelevation" || what == "friction" ||
               what == "sight" || what == "clearance") {
      double value = 0.0;
      if (f.size() != 4 || !ParseNum(f[3], &value) || value < 0.0) {
        log.push_back("ALIGNMENTCHECKSET - " + what + " takes one value, zero or more.");
        return;
      }
      if (what == "mintangent") edited.minTangent = value;
      else if (what == "minspiral") edited.minSpiralLength = value;
      else if (what == "mina") edited.minA = value;
      else if (what == "maxa") edited.maxA = value;
      else if (what == "minline") edited.minLineLength = value;
      else if (what == "maxdeflection") edited.maxTangentDeflection = value;  // degrees, at a curveless PI
      else if (what == "superelevation") edited.maxSuperelevation = value;  // REQ-522: maximum rate, ft/ft
      else if (what == "friction") edited.sideFriction = value;              // REQ-522: side friction factor f
      else if (what == "clearance") edited.sightClearance = value;           // REQ-522: lateral sight clearance, ft
      else edited.minSightDistance = value;  // REQ-522: minimum sight distance, ft
    } else {
      log.push_back(usage);
      return;
    }
    if (edited == st.alignmentCheckSets[static_cast<size_t>(idx)]) {
      log.push_back("ALIGNMENTCHECKSET - nothing changed.");
      return;
    }
    PushUndoSnapshot(st, "Edit check set");
    st.alignmentCheckSets[static_cast<size_t>(idx)] = std::move(edited);
    log.push_back("ALIGNMENTCHECKSET - \"" + st.alignmentCheckSets[static_cast<size_t>(idx)].name + "\" updated.");
    return;
  }
  if (verb == "load" && f.size() == 2) {  // REQ-502: reference values loaded from a user file
    std::ifstream in(Trim(f[1]), std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<alignment::AlignmentCheckSet> loaded;
    std::string err = "the file could not be read";
    if (!in.good() && text.empty()) {
      log.push_back("ALIGNMENTCHECKSET - \"" + Trim(f[1]) + "\": " + err + ".");
      return;
    }
    if (!AlignmentCheckSetsFromJsonText(text, &loaded, &err) || loaded.empty()) {
      log.push_back("ALIGNMENTCHECKSET - \"" + Trim(f[1]) + "\": " + (loaded.empty() && err.empty() ? "no check sets" : err) + ".");
      return;
    }
    PushUndoSnapshot(st, "Load check sets");
    int added = 0, replaced = 0;
    for (alignment::AlignmentCheckSet& s : loaded) {
      const int at = FindCheckSetIndex(st, s.name);
      if (at >= 0) {
        st.alignmentCheckSets[static_cast<size_t>(at)] = std::move(s);
        ++replaced;
      } else {
        st.alignmentCheckSets.push_back(std::move(s));
        ++added;
      }
    }
    log.push_back("ALIGNMENTCHECKSET - loaded " + std::to_string(added) + " new and " + std::to_string(replaced) +
                  " replaced check set(s) from \"" + Trim(f[1]) + "\".");
    return;
  }
  if (verb == "save" && f.size() == 3) {
    const int idx = FindCheckSetIndex(st, f[1]);
    if (idx < 0) {
      log.push_back("ALIGNMENTCHECKSET - no check set named \"" + Trim(f[1]) + "\".");
      return;
    }
    std::ofstream out(Trim(f[2]), std::ios::binary | std::ios::trunc);
    out << AlignmentCheckSetsToJsonText({st.alignmentCheckSets[static_cast<size_t>(idx)]});
    if (!out.good()) {
      log.push_back("ALIGNMENTCHECKSET - could not write \"" + Trim(f[2]) + "\".");
      return;
    }
    log.push_back("ALIGNMENTCHECKSET - \"" + st.alignmentCheckSets[static_cast<size_t>(idx)].name + "\" saved to \"" +
                  Trim(f[2]) + "\".");
    return;
  }
  log.push_back(usage);
}

// ALIGNMENTCRITERIA <alignment>, <set>|NONE
void RunAlignmentCriteria(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitFields(args);
  if (f.size() != 2) {
    log.push_back("ALIGNMENTCRITERIA - usage: ALIGNMENTCRITERIA <alignment>, <set>|NONE.");
    return;
  }
  const alignment::Alignment* a = Resolve(st, "ALIGNMENTCRITERIA", f[0], log);
  if (a == nullptr) return;
  const size_t ai = static_cast<size_t>(FindAlignment(st, a->name));
  const std::string want = Trim(f[1]);
  std::string setName;
  if (Lower(want) != "none") {
    const int idx = FindCheckSetIndex(st, want);
    if (idx < 0) {
      log.push_back("ALIGNMENTCRITERIA - no check set named \"" + want + "\".");
      return;
    }
    const alignment::AlignmentCheckSet& set = st.alignmentCheckSets[static_cast<size_t>(idx)];
    std::string why;
    if (!alignment::CriteriaAllowed(a->type, set.kind, &why)) {
      log.push_back("ALIGNMENTCRITERIA - " + why + ".");
      return;
    }
    setName = set.name;
  }
  if (setName == st.alignments[ai].checkSetName) {
    log.push_back("ALIGNMENTCRITERIA - \"" + a->name + "\" already uses " +
                  (setName.empty() ? std::string("no criteria.") : "\"" + setName + "\"."));
    return;
  }
  PushUndoSnapshot(st, "Alignment criteria");
  st.alignments[ai].checkSetName = setName;
  log.push_back("ALIGNMENTCRITERIA - \"" + st.alignments[ai].name + "\" " +
                (setName.empty() ? std::string("has no design criteria.") : "is checked against \"" + setName + "\"."));
}

// ALIGNMENTSPEED <alignment>, <station>, <speed>|CLEAR
void RunAlignmentSpeed(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitFields(args);
  if (f.size() != 3) {
    log.push_back("ALIGNMENTSPEED - usage: ALIGNMENTSPEED <alignment>, <station>, <speed>|CLEAR.");
    return;
  }
  const alignment::Alignment* a = Resolve(st, "ALIGNMENTSPEED", f[0], log);
  if (a == nullptr) return;
  double displayed = 0.0;
  if (!ParseSta(st, f[1], &displayed)) {
    log.push_back("ALIGNMENTSPEED - \"" + f[1] + "\" is not a station.");
    return;
  }
  const std::string value = Trim(f[2]);
  const bool clear = Lower(value) == "clear";
  double speed = 0.0;
  if (!clear && (!ParseNum(value, &speed) || speed <= 0.0)) {
    log.push_back("ALIGNMENTSPEED - the speed must be a number above zero, or CLEAR.");
    return;
  }
  // Design speeds are stored at internal stations, like every other station on the alignment.
  const std::vector<double> raws = a->RawStationsForDisplayed(displayed);
  if (raws.empty()) {
    log.push_back("ALIGNMENTSPEED - station " + StaFmt(st, displayed) + " is outside \"" + a->name + "\".");
    return;
  }
  const double internal = raws.front();
  const size_t ai = static_cast<size_t>(FindAlignment(st, a->name));
  const auto sameStation = [internal](const alignment::DesignSpeed& d) { return std::fabs(d.internalStation - internal) < 1e-6; };
  const auto& current = st.alignments[ai].designSpeeds;
  const auto found = std::find_if(current.begin(), current.end(), sameStation);
  if (clear) {
    if (found == current.end()) {
      log.push_back("ALIGNMENTSPEED - no design speed is set at " + StaFmt(st, displayed) + ".");
      return;
    }
    PushUndoSnapshot(st, "Clear design speed");
    auto& after = st.alignments[ai].designSpeeds;
    after.erase(std::remove_if(after.begin(), after.end(), sameStation), after.end());
    log.push_back("ALIGNMENTSPEED - design speed cleared at " + StaFmt(st, displayed) + ".");
    return;
  }
  if (found != current.end() && found->speed == speed) {
    log.push_back("ALIGNMENTSPEED - the design speed at " + StaFmt(st, displayed) + " is already " +
                  FormatLinear(speed, 2) + ".");
    return;
  }
  PushUndoSnapshot(st, "Set design speed");
  auto& after = st.alignments[ai].designSpeeds;
  const auto again = std::find_if(after.begin(), after.end(), sameStation);
  if (again != after.end()) {
    again->speed = speed;
  } else {
    after.push_back({internal, speed});
  }
  std::sort(after.begin(), after.end(), [](const alignment::DesignSpeed& x, const alignment::DesignSpeed& y) {
    return x.internalStation < y.internalStation;
  });
  log.push_back("ALIGNMENTSPEED - design speed " + FormatLinear(speed, 2) + " from station " +
                StaFmt(st, displayed) + ".");
}

// ALIGNMENTCHECK <alignment>: every failed check against the alignment's criteria, in travel order.
void RunAlignmentCheck(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitFields(args);
  if (f.size() != 1) {
    log.push_back("ALIGNMENTCHECK - usage: ALIGNMENTCHECK <alignment>.");
    return;
  }
  const alignment::Alignment* a = Resolve(st, "ALIGNMENTCHECK", f[0], log);
  if (a == nullptr) return;
  const std::string criteriaName = AlignmentStyles::EffectiveCheckSetName(st.alignmentStyles, *a);
  if (criteriaName.empty()) {
    log.push_back("ALIGNMENTCHECK - \"" + a->name + "\" has no design criteria. Apply a set with ALIGNMENTCRITERIA.");
    return;
  }
  const int idx = FindCheckSetIndex(st, criteriaName);
  if (idx < 0) {
    log.push_back("ALIGNMENTCHECK - the check set \"" + criteriaName + "\" is missing.");
    return;
  }
  const alignment::AlignmentCheckSet& set = st.alignmentCheckSets[static_cast<size_t>(idx)];
  const std::vector<alignment::DesignFinding> findings = alignment::CheckDesign(*a, set, st.displayLinearPrecision);
  if (findings.empty()) {
    log.push_back("ALIGNMENTCHECK - \"" + a->name + "\" meets \"" + set.name + "\".");
    return;
  }
  log.push_back("ALIGNMENTCHECK - " + std::to_string(findings.size()) + " finding(s) for \"" + a->name + "\" against \"" +
                set.name + "\":");
  for (const alignment::DesignFinding& fd : findings) {
    log.push_back("  Entity " + std::to_string(fd.entity) + ": " + fd.text);
  }
}

// ALIGNMENTINVERSE <alignment>, STA, <station1>, <station2>[, CHORD]     stations as displayed (12+45.32)
//                  <alignment>, PT, <x1>, <y1>, <x2>, <y2>[, CHORD]     points, projected onto the alignment (REQ-501.3)
// Reports the distance along the alignment between the two stations (the arc length for a curve). CHORD also gives the
// straight distance between the two points, which the spec's option asks for.
void RunAlignmentInverse(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitFields(args);
  const char* usage =
      "ALIGNMENTINVERSE - usage: ALIGNMENTINVERSE <alignment>, STA, <station1>, <station2>[, CHORD] | "
      "<alignment>, PT, <x1>, <y1>, <x2>, <y2>[, CHORD].";
  if (f.size() < 4) {
    log.push_back(usage);
    return;
  }
  alignment::Alignment* a = Resolve(st, "ALIGNMENTINVERSE", f[0], log);
  if (a == nullptr) return;
  if (a->Empty()) {
    log.push_back("ALIGNMENTINVERSE - \"" + a->name + "\" has no geometry.");
    return;
  }
  const std::string mode = Lower(f[1]);
  const bool chordWanted = Lower(f.back()) == "chord";
  double internal1 = 0.0, internal2 = 0.0;
  double px1 = 0.0, py1 = 0.0, px2 = 0.0, py2 = 0.0;
  if (mode == "sta" && (f.size() == 4 || (f.size() == 5 && chordWanted))) {
    double s1 = 0.0, s2 = 0.0;
    if (!ParseSta(st, f[2], &s1) || !ParseSta(st, f[3], &s2)) {
      log.push_back("ALIGNMENTINVERSE - the stations must be stations (12+45.32).");
      return;
    }
    const std::vector<double> r1 = a->RawStationsForDisplayed(s1), r2 = a->RawStationsForDisplayed(s2);
    if (r1.empty() || r2.empty()) {
      log.push_back("ALIGNMENTINVERSE - a station is outside \"" + a->name + "\".");
      return;
    }
    internal1 = r1.front();
    internal2 = r2.front();
    const alignment::Point2 p1 = a->GetPointAtStation(internal1), p2 = a->GetPointAtStation(internal2);
    px1 = p1.x; py1 = p1.y; px2 = p2.x; py2 = p2.y;
  } else if (mode == "pt" && (f.size() == 6 || (f.size() == 7 && chordWanted))) {
    double x1 = 0.0, y1 = 0.0, x2 = 0.0, y2 = 0.0;
    if (!ParseNum(f[2], &x1) || !ParseNum(f[3], &y1) || !ParseNum(f[4], &x2) || !ParseNum(f[5], &y2)) {
      log.push_back("ALIGNMENTINVERSE - the points must be two numbers each.");
      return;
    }
    double lx1 = 0.0, ly1 = 0.0, lx2 = 0.0, ly2 = 0.0;
    CadCoord::LocalFromWorld(st, x1, y1, &lx1, &ly1);
    CadCoord::LocalFromWorld(st, x2, y2, &lx2, &ly2);
    internal1 = a->GetStationOffset(lx1, ly1).internalStation;
    internal2 = a->GetStationOffset(lx2, ly2).internalStation;
    const alignment::Point2 p1 = a->GetPointAtStation(internal1), p2 = a->GetPointAtStation(internal2);
    px1 = p1.x; py1 = p1.y; px2 = p2.x; py2 = p2.y;
  } else {
    log.push_back(usage);
    return;
  }
  const double arc = std::fabs(internal2 - internal1);
  const double chord = std::hypot(px2 - px1, py2 - py1);
  const int p = st.displayLinearPrecision;
  std::string line = "ALIGNMENTINVERSE - \"" + a->name + "\" from station " +
                     StaFmt(st, a->DisplayedStation(internal1, true)) + " to " +
                     StaFmt(st, a->DisplayedStation(internal2, true)) + ": distance along " + FormatLinear(arc, p);
  if (chordWanted) line += ", chord " + FormatLinear(chord, p);
  log.push_back(line + ".");
}

// ALIGNMENTBEARING <alignment>, <station>   the direction of travel at a station (REQ-501.3)
void RunAlignmentBearing(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitFields(args);
  if (f.size() != 2) {
    log.push_back("ALIGNMENTBEARING - usage: ALIGNMENTBEARING <alignment>, <station>.");
    return;
  }
  alignment::Alignment* a = Resolve(st, "ALIGNMENTBEARING", f[0], log);
  if (a == nullptr) return;
  double displayed = 0.0;
  if (!ParseSta(st, f[1], &displayed)) {
    log.push_back("ALIGNMENTBEARING - \"" + f[1] + "\" is not a station.");
    return;
  }
  const std::vector<double> raws = a->RawStationsForDisplayed(displayed);
  if (raws.empty()) {
    log.push_back("ALIGNMENTBEARING - station " + StaFmt(st, displayed) + " is outside \"" + a->name + "\".");
    return;
  }
  const double az = a->GetBearingAtStation(raws.front());
  log.push_back("ALIGNMENTBEARING - \"" + a->name + "\" at " + StaFmt(st, displayed) + ": " +
                FormatBearing(az * 180.0 / alignment::kPi, CadAngleDisplaySettings(st)) + ".");
}

// ALIGNMENTDISTANCE <alignment>, <x>, <y>   the distance along the alignment to the point nearest (x, y) (REQ-501.3)
void RunAlignmentDistance(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitFields(args);
  double x = 0.0, y = 0.0;
  if (f.size() != 3 || !ParseNum(f[1], &x) || !ParseNum(f[2], &y)) {
    log.push_back("ALIGNMENTDISTANCE - usage: ALIGNMENTDISTANCE <alignment>, <x>, <y>.");
    return;
  }
  alignment::Alignment* a = Resolve(st, "ALIGNMENTDISTANCE", f[0], log);
  if (a == nullptr) return;
  if (a->Empty()) {
    log.push_back("ALIGNMENTDISTANCE - \"" + a->name + "\" has no geometry.");
    return;
  }
  double lx = 0.0, ly = 0.0;
  CadCoord::LocalFromWorld(st, x, y, &lx, &ly);
  const alignment::StationOffset so = a->GetStationOffset(lx, ly);
  log.push_back("ALIGNMENTDISTANCE - \"" + a->name + "\": " + FormatLinear(a->GetDistanceAlongAlignment(lx, ly), st.displayLinearPrecision) +
                " along from the start, nearest at station " + StaFmt(st, a->DisplayedStation(so.internalStation, true)) +
                (so.outOfRange ? " (beyond an end)" : "") + ".");
}

// ----------------------------------------------------------------------------------------------------------------
// REQ-485: create alignments. The Create Alignment dialogs submit everything they collect as KEY=value fields, so the
// dialog, the command line and a transcript all go through one typed, undoable command.
// ----------------------------------------------------------------------------------------------------------------
namespace {

bool ParseAlignmentType(const std::string& text, alignment::AlignmentType* out) {
  assert(out != nullptr);
  std::string t;
  for (const char c : Lower(text)) {
    if (c != ' ' && c != '_' && c != '-') t.push_back(c);
  }
  assert(t.size() <= text.size());
  using T = alignment::AlignmentType;
  if (t == "centerline" || t == "centreline") *out = T::Centerline;
  else if (t == "offset") *out = T::Offset;
  else if (t == "curbreturn") *out = T::CurbReturn;
  else if (t == "miscellaneous" || t == "misc") *out = T::Miscellaneous;
  else if (t == "rail") *out = T::Rail;
  else return false;
  return true;
}

struct CreateOptions {
  std::optional<alignment::AlignmentType> type;
  std::optional<std::string> description, layer, style, labelSet, criteria;
  std::optional<double> start, speed, fillet, tolerance;
  std::optional<alignment::Point2> pick;  ///< WORLD coordinates of the first pick.
  bool erase = false;
  bool reverse = false;
};

enum class OptionParse { Positional, Taken, Bad };

/// "x y" (WORLD), the form a picked point takes inside one comma-separated field.
bool ParseSpacedPoint(const std::string& text, alignment::Point2* out) {
  assert(out != nullptr);
  std::istringstream in(text);
  std::string xs, ys, extra;
  in >> xs >> ys;
  const bool ok = !(in >> extra) && ParseNum(xs, &out->x) && ParseNum(ys, &out->y);
  assert(!ok || (std::isfinite(out->x) && std::isfinite(out->y)));
  return ok;
}

OptionParse ParseNumberOption(const std::string& key, const std::string& val, bool fromObjects, CreateOptions* o,
                              std::string* why) {
  assert(o != nullptr && why != nullptr);
  assert(!key.empty());
  double d = 0.0;
  if (key == "start") {
    if (!alignment::ParseStation(val, &d)) *why = "\"" + val + "\" is not a station (try 5+00 or 500)";
    else o->start = d;
  } else if (key == "speed") {
    if (!ParseNum(val, &d) || d <= 0.0) *why = "the design speed must be a number above zero";
    else o->speed = d;
  } else if (fromObjects && key == "fillet") {
    if (!ParseNum(val, &d) || d <= 0.0) *why = "the curve radius must be a number above zero";
    else o->fillet = d;
  } else if (fromObjects && key == "tol") {
    if (!ParseNum(val, &d) || d <= 0.0 || d > 1.0) *why = "the gap tolerance must be above 0 and at most 1.0";
    else o->tolerance = d;
  } else if (fromObjects && key == "pick") {
    alignment::Point2 p;
    if (!ParseSpacedPoint(val, &p)) *why = "PICK takes a point as \"<x> <y>\"";
    else o->pick = p;
  } else {
    return OptionParse::Positional;
  }
  return why->empty() ? OptionParse::Taken : OptionParse::Bad;
}

OptionParse ParseCreateOption(const std::string& field, bool fromObjects, CreateOptions* o, std::string* why) {
  assert(o != nullptr && why != nullptr);
  why->clear();
  const size_t eq = field.find('=');
  const std::string key = Lower(Trim(field.substr(0, eq)));
  if (eq == std::string::npos) {
    if (fromObjects && (key == "erase" || key == "reverse" || key == "r")) {
      (key == "erase" ? o->erase : o->reverse) = true;
      return OptionParse::Taken;
    }
    return OptionParse::Positional;
  }
  const std::string val = Trim(field.substr(eq + 1));
  if (key == "type") {
    alignment::AlignmentType t = alignment::AlignmentType::Centerline;
    if (!ParseAlignmentType(val, &t)) {
      *why = "\"" + val + "\" is not an alignment type (Centerline, Offset, Curb Return, Miscellaneous or Rail)";
      return OptionParse::Bad;
    }
    o->type = t;
  } else if (key == "description") o->description = val;
  else if (key == "layer") o->layer = val;
  else if (key == "style") o->style = val;
  else if (key == "labelset") o->labelSet = val;
  else if (key == "criteria") o->criteria = val;
  else if (ParseNumberOption(key, val, fromObjects, o, why) == OptionParse::Positional) {
    *why = "unrecognised option \"" + field + "\"";
    return OptionParse::Bad;
  }
  return why->empty() ? OptionParse::Taken : OptionParse::Bad;
}

/// Splits the fields of a create command into positional values and KEY=value options.
bool SplitCreateFields(const std::string& args, bool fromObjects, std::vector<std::string>* positional,
                       CreateOptions* o, std::string* why) {
  assert(positional != nullptr && o != nullptr && why != nullptr);
  const std::vector<std::string> fields = SplitFields(args);
  for (const std::string& f : fields) {
    const OptionParse r = ParseCreateOption(f, fromObjects, o, why);
    if (r == OptionParse::Bad) return false;
    if (r == OptionParse::Positional) positional->push_back(f);
  }
  assert(positional->size() <= fields.size());
  return true;
}

/// Applies the dialog's fields to a new alignment; false names the first one that cannot be used.
bool ApplyCreateOptions(AppCommandState& st, const CreateOptions& o, alignment::Alignment* a, std::string* why) {
  assert(a != nullptr && why != nullptr);
  assert(!a->name.empty());
  if (o.type) a->type = *o.type;
  if (o.description) a->description = *o.description;
  if (o.layer && !o.layer->empty()) a->layer = *o.layer;
  if (o.style && !o.style->empty()) {
    if (AlignmentStyles::Find(st.alignmentStyles, *o.style) == nullptr) {
      *why = "no alignment style named \"" + *o.style + "\"";
      return false;
    }
    a->style = *o.style;
  }
  if (o.labelSet && !o.labelSet->empty()) {
    if (AlignmentLabelSets::Find(st.alignmentLabelSets, *o.labelSet) == nullptr) {
      *why = "no alignment label set named \"" + *o.labelSet + "\"";
      return false;
    }
    a->labelSet = *o.labelSet;
  }
  if (o.start) a->startingStation = *o.start;
  if (o.criteria && !o.criteria->empty() && Lower(*o.criteria) != "none") {
    const int idx = FindCheckSetIndex(st, *o.criteria);
    if (idx < 0) {
      *why = "no check set named \"" + *o.criteria + "\"";
      return false;
    }
    const alignment::AlignmentCheckSet& set = st.alignmentCheckSets[static_cast<size_t>(idx)];
    if (!alignment::CriteriaAllowed(a->type, set.kind, why)) return false;
    a->checkSetName = set.name;
  }
  if (o.speed) {
    alignment::DesignSpeed speed;
    speed.internalStation = a->startingStation;
    speed.speed = *o.speed;
    a->designSpeeds = {speed};
  }
  return true;
}

alignment::Alignment NewAlignment(const AppCommandState& st, const std::string& name) {
  assert(!name.empty());
  alignment::Alignment a;
  a.name = name;
  a.layer = MakeNewEntityAttrs(st).layer;
  if (a.layer.empty()) a.layer = "0";
  if (AlignmentStyles::Find(st.alignmentStyles, st.alignmentStyleCurrent) != nullptr) a.style = st.alignmentStyleCurrent;
  assert(!a.layer.empty());
  return a;
}

}  // namespace

void RunAlignmentMeta(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  assert(st.alignments.size() < st.alignments.max_size());
  assert(args.size() < 65536);
  const std::vector<std::string> f = SplitFields(args);
  const char* usage = "ALIGNMENTMETA - usage: ALIGNMENTMETA <alignment>, SET, <name>, <value> | <alignment>, REMOVE, <name> | "
                      "<alignment>, LIST.";
  if (f.size() < 2) {
    log.push_back(usage);
    return;
  }
  alignment::Alignment* a = Resolve(st, "ALIGNMENTMETA", f[0], log);
  if (a == nullptr) return;
  const std::string verb = Lower(f[1]);
  auto find = [&](const std::string& key) {
    return std::find_if(a->metadata.begin(), a->metadata.end(), [&](const auto& kv) { return Lower(kv.first) == Lower(key); });
  };
  if (verb == "list" && f.size() == 2) {
    if (a->metadata.empty()) log.push_back("ALIGNMENTMETA - \"" + a->name + "\" has no metadata.");
    for (const auto& [key, value] : a->metadata) log.push_back("  " + key + " = " + value);
    return;
  }
  if (verb == "set" && f.size() == 4 && !Trim(f[2]).empty()) {
    PushUndoSnapshot(st, "Alignment metadata");
    a = Resolve(st, "ALIGNMENTMETA", f[0], log);
    const auto it = find(Trim(f[2]));
    if (it != a->metadata.end()) it->second = f[3];
    else a->metadata.emplace_back(Trim(f[2]), f[3]);
    log.push_back("ALIGNMENTMETA - \"" + a->name + "\": " + Trim(f[2]) + " = " + f[3] + ".");
    return;
  }
  if (verb == "remove" && f.size() == 3) {
    if (find(Trim(f[2])) == a->metadata.end()) {
      log.push_back("ALIGNMENTMETA - \"" + a->name + "\" has no \"" + Trim(f[2]) + "\".");
      return;
    }
    PushUndoSnapshot(st, "Alignment metadata");
    a = Resolve(st, "ALIGNMENTMETA", f[0], log);
    a->metadata.erase(find(Trim(f[2])));
    log.push_back("ALIGNMENTMETA - \"" + Trim(f[2]) + "\" removed from \"" + a->name + "\".");
    return;
  }
  log.push_back(usage);
}

void RunStationFormat(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  assert(st.alignmentStationFormat.decimals >= 0);
  assert(args.size() < 4096);
  const std::vector<std::string> f = SplitFields(args);
  alignment::StationFormat fmt = st.alignmentStationFormat;
  const char* usage = "STATIONFORMAT - usage: STATIONFORMAT [DELIMITER=+|-|_|/|C|K|NONE][, DIGITS=1..6][, DECIMALS=0..6]"
                      "[, PREFIX=<text>][, SUFFIX=<text>] | DEFAULT.";
  if (f.size() == 1 && Lower(Trim(f[0])) == "default") fmt = alignment::StationFormat{};
  for (const std::string& field : f) {
    if (Trim(field).empty() || Lower(Trim(field)) == "default") continue;
    const size_t eq = field.find('=');
    if (eq == std::string::npos) {
      log.push_back(usage);
      return;
    }
    const std::string key = Lower(Trim(field.substr(0, eq)));
    const std::string value = field.substr(eq + 1);  // prefix and suffix keep their spaces ("STA ")
    double n = 0.0;
    if (key == "delimiter" || key == "delim") {
      const std::string v = Trim(value);
      if (Lower(v) == "none") fmt.delimiter = '\0';
      else if (v.size() == 1 && std::string("+-_/CcKk").find(v[0]) != std::string::npos)
        fmt.delimiter = static_cast<char>(std::toupper(static_cast<unsigned char>(v[0])));
      else {
        log.push_back("STATIONFORMAT - the delimiter must be +, -, _, /, C, K or NONE.");
        return;
      }
    } else if (key == "digits" && ParseNum(value, &n) && n >= 1 && n <= 6 && n == std::floor(n)) {
      fmt.digitsRight = static_cast<int>(n);
    } else if (key == "decimals" && ParseNum(value, &n) && n >= 0 && n <= 6 && n == std::floor(n)) {
      fmt.decimals = static_cast<int>(n);
    } else if (key == "prefix") {
      fmt.prefix = value;
    } else if (key == "suffix") {
      fmt.suffix = value;
    } else {
      log.push_back(usage);
      return;
    }
  }
  if (!f.empty() && !(f.size() == 1 && Trim(f[0]).empty())) {
    PushUndoSnapshot(st, "Station format");
    st.alignmentStationFormat = fmt;
  }
  log.push_back("STATIONFORMAT - stations are written like " + StaFmt(st, 1245.32) + " (delimiter " +
                (st.alignmentStationFormat.delimiter == '\0' ? std::string("none")
                                                             : std::string(1, st.alignmentStationFormat.delimiter)) +
                ", " + std::to_string(st.alignmentStationFormat.digitsRight) + " digit(s) after it, " +
                std::to_string(st.alignmentStationFormat.decimals) + " decimal(s)).");
}

std::string UniqueAlignmentName(const AppCommandState& st, const std::string& base) {
  assert(!base.empty());
  assert(st.alignments.size() < st.alignments.max_size());
  if (FindAlignment(st, base) < 0) return base;
  for (size_t n = 2; n <= st.alignments.size() + 2; ++n) {  // one of these is free: there are only so many names
    const std::string candidate = base + " (" + std::to_string(n) + ")";
    if (FindAlignment(st, candidate) < 0) return candidate;
  }
  return base + " (" + std::to_string(st.alignments.size() + 3) + ")";
}

std::string NextAlignmentName(const AppCommandState& st) {
  assert(st.alignments.size() < st.alignments.max_size());
  for (size_t n = 1; n <= st.alignments.size() + 1; ++n) {
    const std::string name = "Alignment - (" + std::to_string(n) + ")";
    if (FindAlignment(st, name) < 0) return name;
  }
  assert(false && "a free counter always exists among n+1 candidates");
  return "Alignment";
}

double EffectiveAlignmentCurveRadius(const AppCommandState& st) {
  assert(st.alignmentCurveSettings.radius >= 0.0);
  if (st.alignmentCurveSettings.radius > 0.0) return st.alignmentCurveSettings.radius;
  // REQ-485.2: 100 ft, or 30 m in a metric drawing ($INSUNITS 6 metres, 4 millimetres).
  const double r = st.drawingInsUnits == 6 ? 30.0 : st.drawingInsUnits == 4 ? 30000.0 : 100.0;
  assert(r > 0.0);
  return r;
}

// ALIGNMENT <name>[, <description>[, <starting station>[, <layer>[, <style>]]]][, TYPE=][, LABELSET=][, CRITERIA=]
//   [, SPEED=]  (REQ-480, REQ-485.1). With no arguments the Qt editor opens the Create Alignment dialog.
void RunAlignmentCreate(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  assert(st.alignments.size() < st.alignments.max_size());
  if (Trim(args).empty() && st.createAlignmentDialogAvailable) {
    st.createAlignmentDialogRequested = true;
    log.push_back("ALIGNMENT - opening the Create Alignment dialog.");
    return;
  }
  CreateOptions o;
  std::vector<std::string> pos;
  std::string why;
  if (!SplitCreateFields(args, false, &pos, &o, &why)) {
    log.push_back("ALIGNMENT - " + why + ".");
    return;
  }
  if (pos.empty() || pos[0].empty() || pos.size() > 5) {
    log.push_back("ALIGNMENT - usage: ALIGNMENT <name>[, <description>[, <starting station>[, <layer>[, <style>]]]]"
                  "[, TYPE=<type>][, LABELSET=<set>][, CRITERIA=<check set>][, SPEED=<design speed>].");
    return;
  }
  if (FindAlignment(st, pos[0]) >= 0) {
    log.push_back("ALIGNMENT - an alignment named \"" + pos[0] + "\" already exists; names must be unique.");
    return;
  }
  if (pos.size() > 1 && !o.description) o.description = pos[1];
  if (pos.size() > 2 && !pos[2].empty() && ParseNumberOption("start", pos[2], false, &o, &why) != OptionParse::Taken) {
    log.push_back("ALIGNMENT - " + why + ".");
    return;
  }
  if (pos.size() > 3 && !pos[3].empty()) o.layer = pos[3];
  if (pos.size() > 4 && !pos[4].empty()) o.style = pos[4];
  alignment::Alignment a = NewAlignment(st, pos[0]);
  if (!ApplyCreateOptions(st, o, &a, &why)) {
    log.push_back("ALIGNMENT - " + why + ". No alignment was created.");
    return;
  }
  PushUndoSnapshot(st, "Create alignment");
  a.id = st.nextEntityId++;
  st.alignments.push_back(std::move(a));
  const alignment::Alignment& made = st.alignments.back();
  log.push_back("ALIGNMENT - created \"" + made.name + "\" (" + TypeName(made.type) + ", starting station " +
                StaFmt(st, made.startingStation) + "). Lay it out with ALIGNMENTLAYOUT or ALIGNMENTADD.");
}

namespace {

/// What an unusable source is, for the refusal (REQ-485.3 / REQ-201).
const char* SourceKindName(SelectedEntity::Type t) {
  using T = SelectedEntity::Type;
  switch (t) {
    case T::Circle: return "a circle";
    case T::Ellipse: return "an ellipse";
    case T::Spline: return "a spline";
    case T::Annotation: return "text or a dimension";
    case T::BlockRef: return "a block reference";
    case T::Point: return "a point";
    case T::Alignment: return "an alignment";
    case T::Solid: return "a solid";
    default: return "not a line, arc, polyline or feature line";
  }
}

/// Plan segments of one selected source. Z is ignored (an alignment is plan geometry, REQ-480.3) but `zSeen` reports
/// it; a vertical line, a tilted arc, a 3D polyline and anything that is not linework are refused by name.
bool AddSourceSegments(const AppCommandState& st, const SelectedEntity& se, int srcNo,
                       std::vector<alignment::SourceSegment>* segs, bool* zSeen, std::string* why) {
  assert(segs != nullptr && zSeen != nullptr && why != nullptr);
  assert(srcNo > 0);
  using T = SelectedEntity::Type;
  const size_t ix = static_cast<size_t>(se.index);
  const std::string obj = "object " + std::to_string(srcNo);
  if (se.type == T::LineSeg && se.index >= 0 && ix * 6 + 5 < st.userLinesFlat.size()) {
    const double* v = &st.userLinesFlat[ix * 6];
    if (std::hypot(v[3] - v[0], v[4] - v[1]) <= 1e-9 && std::fabs(v[5] - v[2]) > 1e-9) {
      *why = obj + " is a vertical line; it has no length in plan";
      return false;
    }
    alignment::SourceSegment s;
    s.a = {v[0], v[1]};
    s.b = {v[3], v[4]};
    s.sourceIndex = srcNo;
    segs->push_back(s);
    *zSeen = *zSeen || v[2] != 0.0 || v[5] != 0.0;
    return true;
  }
  if (se.type == T::Arc && se.index >= 0 && ix < st.userArcs.size()) {
    const CadArc& ar = st.userArcs[ix];
    if (std::fabs(ar.nx) > 1e-6 || std::fabs(ar.ny) > 1e-6) {
      *why = obj + " is an arc that is not in plan (its plane is tilted)";
      return false;
    }
    alignment::SourceSegment s;
    s.isArc = true;
    s.center = {ar.cx, ar.cy};
    s.radius = ar.r;
    const double a0 = ar.startRad, sw = ar.sweepRad;
    s.startAngle = sw >= 0.0 ? a0 : a0 + sw;
    s.endAngle = s.startAngle + std::fabs(sw);
    s.sourceIndex = srcNo;
    segs->push_back(s);
    *zSeen = *zSeen || ar.z != 0.0;
    return true;
  }
  *why = obj + " is " + SourceKindName(se.type);
  return false;
}

/// Polyline and feature-line sources: one segment per span (a bulge span becomes a true arc).
bool AddVertexSourceSegments(const AppCommandState& st, const SelectedEntity& se, int srcNo,
                             std::vector<alignment::SourceSegment>* segs, bool* zSeen, std::string* why) {
  assert(segs != nullptr && zSeen != nullptr && why != nullptr);
  assert(se.type == SelectedEntity::Type::Polyline || se.type == SelectedEntity::Type::FeatureLine);
  const bool feature = se.type == SelectedEntity::Type::FeatureLine;
  const std::vector<int>& offsets = feature ? st.featureLineOffsets : st.userPolylineOffsets;
  const size_t ix = static_cast<size_t>(se.index);
  if (se.index < 0 || ix + 1 >= offsets.size()) {
    *why = "object " + std::to_string(srcNo) + " no longer exists";
    return false;
  }
  const int v0 = offsets[ix], cnt = offsets[ix + 1] - offsets[ix];
  const bool closed = feature ? (ix < st.featureLineClosed.size() && st.featureLineClosed[ix] != 0)
                              : (ix < st.userPolylineClosed.size() && st.userPolylineClosed[ix] != 0);
  auto vert = [&](int k, int c) {
    const size_t i = static_cast<size_t>(v0 + k) * 3 + static_cast<size_t>(c);
    return feature ? st.featureLineVerts[i] : static_cast<double>(st.userPolylineVerts[i]);
  };
  double zMin = std::numeric_limits<double>::infinity(), zMax = -zMin;
  for (int k = 0; k < cnt; ++k) {
    zMin = std::min(zMin, vert(k, 2));
    zMax = std::max(zMax, vert(k, 2));
  }
  if (!feature && cnt > 0 && zMax - zMin > 1e-9) {  // REQ-485.3: a 3D polyline is non-planar; a feature line is not
    *why = "object " + std::to_string(srcNo) + " is a 3D polyline (its vertices are at different elevations)";
    return false;
  }
  *zSeen = *zSeen || (cnt > 0 && (zMin != 0.0 || zMax != 0.0));
  for (int k = 0; k + 1 < cnt + (closed ? 1 : 0); ++k) {
    const int k1 = (k + 1) % cnt;
    const alignment::Point2 p0{vert(k, 0), vert(k, 1)}, p1{vert(k1, 0), vert(k1, 1)};
    const size_t bi = static_cast<size_t>(v0 + k);
    const double bulge = !feature && bi < st.userPolylineVertsBulge.size() ? st.userPolylineVertsBulge[bi] : 0.0;
    segs->push_back(alignment::SegmentFromBulge(p0, p1, bulge, srcNo));
  }
  return true;
}

/// The selection as source segments (REQ-485.2). False names the first object that cannot be used.
bool GatherSourceSegments(const AppCommandState& st, std::vector<alignment::SourceSegment>* segs, bool* zSeen,
                          std::string* why) {
  assert(segs != nullptr && zSeen != nullptr && why != nullptr);
  assert(segs->empty());
  int n = 0;
  for (const SelectedEntity& se : st.selection) {
    const int srcNo = ++n;
    const bool vertexSource = se.type == SelectedEntity::Type::Polyline || se.type == SelectedEntity::Type::FeatureLine;
    const bool ok = vertexSource ? AddVertexSourceSegments(st, se, srcNo, segs, zSeen, why)
                                 : AddSourceSegments(st, se, srcNo, segs, zSeen, why);
    if (!ok) return false;
  }
  return true;
}

/// Adds the alignment in one undo step. With ERASE the sources are deleted first; the delete's snapshot is the step,
/// so one UNDO restores them and removes the alignment together (REQ-485 acceptance 6).
void CommitFromObjects(AppCommandState& st, alignment::Alignment made, bool erase) {
  assert(!made.name.empty());
  assert(!made.Empty());
  if (erase && !st.selection.empty()) {
    std::vector<std::string> deleteLog;  // this command reports the result, not the delete
    ExecuteDeleteSelection(st, deleteLog);
    const int idx = st.activeDrawingIdx;
    if (idx >= 0 && static_cast<size_t>(idx) < st.documents.size() && !st.documents[static_cast<size_t>(idx)].undoStack.empty())
      st.documents[static_cast<size_t>(idx)].undoStack.back().description = "Create alignment from objects";
  } else {
    PushUndoSnapshot(st, "Create alignment from objects");
  }
  made.id = st.nextEntityId++;
  st.alignments.push_back(std::move(made));
  st.selection.clear();
  BumpCadGpuCache(st);
}

void StartAlignmentFromObjects(AppCommandState& st, std::vector<std::string>& log);

}  // namespace

// CREATEALIGNMENTFROMOBJECTS <name>[, START=<station>][, FILLET=<radius>][, REVERSE][, TOL=<gap>][, ERASE]
//   [, PICK=<x> <y>][, TYPE=][, DESCRIPTION=][, LAYER=][, STYLE=][, LABELSET=][, CRITERIA=][, SPEED=]
// Converts the SELECTED lines, arcs, 2D polylines and feature lines (REQ-485.2). Line -> Tangent, Arc -> Curve, a
// polyline bulge segment -> Curve; never a vertex approximation. With no arguments the objects are picked first.
void RunCreateAlignmentFromObjects(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  assert(st.alignments.size() < st.alignments.max_size());
  if (Trim(args).empty()) {
    StartAlignmentFromObjects(st, log);
    return;
  }
  const std::string cmd = "CREATEALIGNMENTFROMOBJECTS - ";
  CreateOptions o;
  std::vector<std::string> pos;
  std::string why;
  if (!SplitCreateFields(args, true, &pos, &o, &why)) {
    log.push_back(cmd + why + ".");
    return;
  }
  if (pos.size() != 1 || pos[0].empty()) {
    log.push_back(cmd + "usage: CREATEALIGNMENTFROMOBJECTS <name>[, START=<station>][, FILLET=<radius>][, REVERSE]"
                        "[, TOL=<gap>][, ERASE][, PICK=<x> <y>] (select the objects first), or no arguments to pick them.");
    return;
  }
  if (FindAlignment(st, pos[0]) >= 0) {
    log.push_back(cmd + "an alignment named \"" + pos[0] + "\" already exists; names must be unique.");
    return;
  }
  if (st.selection.empty()) {
    log.push_back(cmd + "select the lines, arcs or polylines to convert first.");
    return;
  }
  std::vector<alignment::SourceSegment> segs;
  bool zSeen = false;
  if (!GatherSourceSegments(st, &segs, &zSeen, &why)) {
    log.push_back(cmd + why + ". No alignment was created.");
    return;
  }
  alignment::FromObjectsOptions opt;
  opt.reverse = o.reverse;
  opt.addFillets = o.fillet.has_value();
  opt.filletRadius = o.fillet.value_or(opt.filletRadius);
  opt.gapTolerance = o.tolerance.value_or(opt.gapTolerance);
  if (o.pick) {
    opt.hasPick = true;
    CadCoord::LocalFromWorld(st, o.pick->x, o.pick->y, &opt.pick.x, &opt.pick.y);
  }
  alignment::Alignment made;
  std::vector<std::string> warn;
  if (!alignment::BuildFromSegments(segs, opt, &made, &why, &warn)) {
    log.push_back(cmd + why + ". No alignment was created.");
    return;
  }
  const std::vector<alignment::Entity> geometry = made.Entities();
  made = NewAlignment(st, pos[0]);
  for (const alignment::Entity& e : geometry) (void)made.AppendRaw(e);
  if (!ApplyCreateOptions(st, o, &made, &why)) {
    log.push_back(cmd + why + ". No alignment was created.");
    return;
  }
  const size_t sources = st.selection.size();
  CommitFromObjects(st, std::move(made), o.erase);
  const alignment::Alignment& r = st.alignments.back();
  if (zSeen) log.push_back(cmd + "elevations were ignored; an alignment is plan geometry.");
  for (const std::string& w : warn) log.push_back(cmd + w + ".");
  log.push_back(cmd + "created \"" + r.name + "\": " + std::to_string(r.Entities().size()) + " entities (" +
                std::to_string(r.CurveCount()) + " curves), length " + Lin(st, r.Length()) + ", starting at station " +
                StaFmt(st, r.startingStation) + "." +
                (o.erase ? " " + std::to_string(sources) + " source object(s) erased." : ""));
  for (const alignment::ContinuityIssue& c : r.CheckContinuity()) {
    if (!c.c1 && c.c0) log.push_back("  Note: direction changes after entity " + std::to_string(c.afterEntity + 1) + " (not tangent).");
  }
}

// ----------------------------------------------------------------------------------------------------------------
// REQ-485 interactive: ALIGNMENTLAYOUT (Civil 3D's Tangent-Tangent tools) and CREATEALIGNMENTFROMOBJECTS picking.
// ----------------------------------------------------------------------------------------------------------------
namespace {

std::string FormatWorldNumber(double v) {
  std::array<char, 40> b{};
  std::snprintf(b.data(), b.size(), "%.10f", v);
  assert(std::isfinite(v));
  assert(b[0] != '\0');
  return b.data();
}

bool BeginInteractive(AppCommandState& st, AppCommandState::Kind k, const char* name, std::vector<std::string>& log) {
  assert(k != AppCommandState::Kind::None);
  assert(name != nullptr);
  if (st.active != AppCommandState::Kind::None) {
    log.push_back(std::string(name) + " - finish or cancel the active command first.");
    return false;
  }
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  st.selBoxWaitingSecond = false;
  st.active = k;
  st.lastCommand = k;
  return true;
}

/// Rebuilds the alignment being laid out from the session's base and PIs (REQ-485.1). The first change takes the
/// session's one undo snapshot. False (alignment unchanged) when the PIs cannot form an alignment.
bool RebuildLayout(AppCommandState& st, std::vector<std::string>& log) {
  AppCommandState::AlignmentLayoutSession& s = st.alignmentLayout;
  assert(!s.name.empty());
  const int ix = FindAlignment(st, s.name);
  if (ix < 0) return false;
  alignment::Alignment result = s.base;
  if (s.pis.size() >= 2) {
    alignment::CurveSettings cs = st.alignmentCurveSettings;
    cs.radius = EffectiveAlignmentCurveRadius(st);
    std::vector<alignment::Entity> keep = s.base.Entities();
    std::vector<alignment::Point2> pis = s.pis;
    // With curves, the base's last tangent becomes the first leg so the joining PI gets a curve too.
    if (cs.addCurves && !keep.empty() && keep.back().kind == alignment::EntityKind::Line) {
      pis.insert(pis.begin(), keep.back().start);
      keep.pop_back();
    }
    alignment::Alignment tail;
    std::string err;
    std::vector<std::string> warn;
    if (!alignment::BuildFromPis(pis, cs, &tail, &err, &warn)) {
      log.push_back("ALIGNMENTLAYOUT - " + err + ".");
      return false;
    }
    result.Clear();
    for (const alignment::Entity& e : keep) (void)result.AppendRaw(e);
    for (const alignment::Entity& e : tail.Entities()) (void)result.AppendRaw(e);
    for (size_t w = s.warningsShown; w < warn.size(); ++w) log.push_back("ALIGNMENTLAYOUT - " + warn[w] + ".");
    s.warningsShown = warn.size();
  }
  if (!s.snapshotTaken) {
    PushUndoSnapshot(st, "Alignment layout");
    s.snapshotTaken = true;
  }
  st.alignments[static_cast<size_t>(ix)] = std::move(result);
  BumpCadGpuCache(st);
  assert(st.alignments[static_cast<size_t>(ix)].name == s.name);
  return true;
}

void FinishLayout(AppCommandState& st, std::vector<std::string>& log) {
  assert(st.active == AppCommandState::Kind::AlignmentLayout);
  const int ix = FindAlignment(st, st.alignmentLayout.name);
  st.active = AppCommandState::Kind::None;
  if (ix < 0) return;
  const alignment::Alignment& a = st.alignments[static_cast<size_t>(ix)];
  assert(a.name == st.alignmentLayout.name);
  if (a.Empty()) {
    log.push_back("ALIGNMENTLAYOUT - \"" + a.name + "\" has no geometry yet.");
    return;
  }
  log.push_back("ALIGNMENTLAYOUT - \"" + a.name + "\": " + std::to_string(a.Entities().size()) + " entities (" +
                std::to_string(a.CurveCount()) + " curves, " + std::to_string(a.SpiralCount()) + " spirals), length " +
                Lin(st, a.Length()) + ", ending at station " + Sta(st, a, a.EndStation()) + ".");
}

bool SubmitLayoutPoint(AppCommandState& st, alignment::Point2 p, std::vector<std::string>& log) {
  AppCommandState::AlignmentLayoutSession& s = st.alignmentLayout;
  assert(st.active == AppCommandState::Kind::AlignmentLayout);
  assert(std::isfinite(p.x) && std::isfinite(p.y));
  if (s.fixedDirection && s.pis.size() == 1) {  // the dialog's starting direction holds the first tangent
    const alignment::Point2 dir{std::sin(s.directionAzimuth), std::cos(s.directionAzimuth)};
    const double t = (p.x - s.pis[0].x) * dir.x + (p.y - s.pis[0].y) * dir.y;
    if (!(t > 1e-9)) {
      log.push_back("ALIGNMENTLAYOUT - the point is behind the starting direction; pick ahead of the start point.");
      return false;
    }
    p = {s.pis[0].x + dir.x * t, s.pis[0].y + dir.y * t};
  }
  if (!s.pis.empty() && std::hypot(p.x - s.pis.back().x, p.y - s.pis.back().y) <= 1e-9) {
    log.push_back("ALIGNMENTLAYOUT - that is the previous point; pick a different one.");
    return false;
  }
  s.pis.push_back(p);
  if (!RebuildLayout(st, log)) {
    s.pis.pop_back();
    return false;
  }
  return true;
}

/// The from-objects result for the current picks, for the direction prompt. False carries the refusal.
bool PreviewFromObjects(const AppCommandState& st, alignment::Alignment* out, std::string* why) {
  assert(out != nullptr && why != nullptr);
  const AppCommandState::AlignmentFromObjectsPick& p = st.alignmentFromObjects;
  assert(st.active == AppCommandState::Kind::AlignmentFromObjects);
  std::vector<alignment::SourceSegment> segs;
  bool zSeen = false;
  if (!GatherSourceSegments(st, &segs, &zSeen, why)) return false;
  alignment::FromObjectsOptions opt;
  opt.hasPick = p.hasPick;
  opt.pick = {p.pickX, p.pickY};
  opt.reverse = p.reverse;
  return alignment::BuildFromSegments(segs, opt, out, why);
}

void PromptDirection(AppCommandState& st, std::vector<std::string>& log) {
  assert(st.active == AppCommandState::Kind::AlignmentFromObjects);
  st.alignmentFromObjects.phase = AppCommandState::AlignmentFromObjectsPick::Phase::WaitDirection;
  alignment::Alignment preview;
  std::string why;
  if (!PreviewFromObjects(st, &preview, &why)) {
    log.push_back("CREATEALIGNMENTFROMOBJECTS - " + why + ". No alignment was created.");
    st.active = AppCommandState::Kind::None;
    st.selection.clear();
    return;
  }
  const alignment::Point2 s = preview.StartPoint();
  assert(!preview.Empty());
  log.push_back("CREATEALIGNMENTFROMOBJECTS - the alignment starts at " + Lin(st, s.x + CadCoord::WorldOriginX(st)) +
                ", " + Lin(st, s.y + CadCoord::WorldOriginY(st)) + " heading " + Brg(st, preview.StartAzimuth()) +
                ". Press Enter to accept the direction or [Reverse]:");
}

void FinishFromObjectsPicks(AppCommandState& st, std::vector<std::string>& log) {
  assert(st.active == AppCommandState::Kind::AlignmentFromObjects);
  assert(!st.selection.empty());
  st.active = AppCommandState::Kind::None;
  if (st.createAlignmentDialogAvailable) {  // the dialog collects the name and options, then submits the command
    st.alignmentFromObjectsDialogRequested = true;
    return;
  }
  RunCreateAlignmentFromObjects(st, NextAlignmentName(st) + AlignmentFromObjectsPickArgs(st), log);
}

bool HandleFromObjectsText(AppCommandState& st, const std::string& line, std::vector<std::string>& log) {
  using P = AppCommandState::AlignmentFromObjectsPick::Phase;
  AppCommandState::AlignmentFromObjectsPick& p = st.alignmentFromObjects;
  assert(st.active == AppCommandState::Kind::AlignmentFromObjects);
  const std::string t = Lower(Trim(line));
  assert(t.size() <= line.size());
  if (p.phase == P::WaitDirection) {
    if (t.empty()) {
      FinishFromObjectsPicks(st, log);
    } else if (t == "r" || t == "reverse") {
      p.reverse = !p.reverse;
      PromptDirection(st, log);
    } else {
      log.push_back("CREATEALIGNMENTFROMOBJECTS - press Enter to accept the direction, or R to reverse it.");
    }
    return true;
  }
  if (!t.empty()) {
    log.push_back("CREATEALIGNMENTFROMOBJECTS - pick lines, arcs, polylines or feature lines in the drawing; Enter when done.");
  } else if (st.selection.empty()) {
    log.push_back("CREATEALIGNMENTFROMOBJECTS - nothing is selected yet; pick the first line, arc or polyline, or ESC.");
  } else {
    PromptDirection(st, log);
  }
  return true;
}

/// A typed point for the layout: "x,y" (WORLD) or "@dx,dy" from the previous PI.
bool ParseLayoutPoint(const AppCommandState& st, const std::string& text, alignment::Point2* local) {
  assert(local != nullptr);
  const std::string t = Trim(text);
  const bool relative = !t.empty() && t[0] == '@';
  const std::vector<std::string> f = SplitFields(relative ? t.substr(1) : t);
  double x = 0.0, y = 0.0;
  if (f.size() != 2 || !ParseNum(f[0], &x) || !ParseNum(f[1], &y)) return false;
  if (relative) {
    if (st.alignmentLayout.pis.empty()) return false;
    *local = {st.alignmentLayout.pis.back().x + x, st.alignmentLayout.pis.back().y + y};
  } else {
    CadCoord::LocalFromWorld(st, x, y, &local->x, &local->y);
  }
  assert(std::isfinite(local->x) && std::isfinite(local->y));
  return true;
}

bool HandleLayoutText(AppCommandState& st, const std::string& line, std::vector<std::string>& log) {
  AppCommandState::AlignmentLayoutSession& s = st.alignmentLayout;
  assert(st.active == AppCommandState::Kind::AlignmentLayout);
  const std::string t = Lower(Trim(line));
  assert(t.size() <= line.size());
  if (t.empty()) {
    FinishLayout(st, log);
    return true;
  }
  if (t == "u" || t == "undo") {
    const size_t keep = s.base.Empty() ? 0 : 1;  // a continued alignment keeps its end point as the first PI
    if (s.pis.size() <= keep) {
      log.push_back("ALIGNMENTLAYOUT - nothing to undo.");
      return true;
    }
    s.pis.pop_back();
    (void)RebuildLayout(st, log);
    log.push_back(s.pis.empty() ? "ALIGNMENTLAYOUT - specify the start point:" : "ALIGNMENTLAYOUT - specify the next point or [Undo]:");
    return true;
  }
  alignment::Point2 p;
  if (!ParseLayoutPoint(st, line, &p)) {
    log.push_back("ALIGNMENTLAYOUT - pick a point or type X,Y or @dx,dy; U undoes the last point, Enter finishes.");
    return true;
  }
  (void)SubmitLayoutPoint(st, p, log);
  return true;
}

void StartAlignmentFromObjects(AppCommandState& st, std::vector<std::string>& log) {
  assert(st.alignments.size() < st.alignments.max_size());
  const std::vector<SelectedEntity> preselected = st.selection;
  if (!BeginInteractive(st, AppCommandState::Kind::AlignmentFromObjects, "CREATEALIGNMENTFROMOBJECTS", log)) return;
  st.alignmentFromObjects = {};
  st.selection = preselected;
  assert(st.active == AppCommandState::Kind::AlignmentFromObjects);
  if (!st.selection.empty()) {  // pick-first: the selection is the source; its first free end starts the alignment
    PromptDirection(st, log);
    return;
  }
  log.push_back("CREATEALIGNMENTFROMOBJECTS - select the first line/arc/polyline (the alignment starts at the end "
                "nearest the pick):");
}

bool SubmitFromObjectsPick(AppCommandState& st, double lx, double ly, std::vector<std::string>& log) {
  using P = AppCommandState::AlignmentFromObjectsPick::Phase;
  AppCommandState::AlignmentFromObjectsPick& p = st.alignmentFromObjects;
  assert(st.active == AppCommandState::Kind::AlignmentFromObjects);
  if (p.phase == P::WaitDirection) {
    log.push_back("CREATEALIGNMENTFROMOBJECTS - press Enter to accept the direction, or R to reverse it.");
    return false;
  }
  SelectedEntity hit{};
  float d2 = 0.f;
  if (!PickClosestCadEntity(st, lx, ly, CadOffsetEntityPickTolWorld(st), &hit, &d2)) {
    log.push_back("CREATEALIGNMENTFROMOBJECTS - nothing under the cursor; pick a line, arc, polyline or feature line.");
    return false;
  }
  using T = SelectedEntity::Type;
  if (hit.type != T::LineSeg && hit.type != T::Arc && hit.type != T::Polyline && hit.type != T::FeatureLine) {
    log.push_back(std::string("CREATEALIGNMENTFROMOBJECTS - that is ") + SourceKindName(hit.type) +
                  "; an alignment is made from lines, arcs, polylines and feature lines.");
    return false;
  }
  for (const SelectedEntity& e : st.selection) {
    if (e.type == hit.type && e.index == hit.index) {
      log.push_back("CREATEALIGNMENTFROMOBJECTS - already selected; pick another or press Enter.");
      return false;
    }
  }
  st.selection.push_back(hit);
  if (p.phase == P::WaitFirst) {
    p.hasPick = true;
    p.pickX = lx;
    p.pickY = ly;
    p.phase = P::WaitMore;
  }
  assert(!st.selection.empty());
  log.push_back("CREATEALIGNMENTFROMOBJECTS - " + std::to_string(st.selection.size()) +
                " selected. Select lines/arcs or polylines to create the alignment, Enter when done:");
  return true;
}

}  // namespace

bool AlignmentFromObjectsPreview(const AppCommandState& st, alignment::Alignment* out) {
  assert(out != nullptr);
  if (st.active != AppCommandState::Kind::AlignmentFromObjects ||
      st.alignmentFromObjects.phase != AppCommandState::AlignmentFromObjectsPick::Phase::WaitDirection)
    return false;
  std::string why;
  const bool ok = PreviewFromObjects(st, out, &why);
  assert(!ok || !out->Empty());
  return ok;
}

std::string AlignmentFromObjectsPickArgs(const AppCommandState& st) {
  const AppCommandState::AlignmentFromObjectsPick& p = st.alignmentFromObjects;
  std::string out;
  if (p.hasPick)
    out += ", PICK=" + FormatWorldNumber(p.pickX + CadCoord::WorldOriginX(st)) + " " +
           FormatWorldNumber(p.pickY + CadCoord::WorldOriginY(st));
  if (p.reverse) out += ", REVERSE";
  assert(out.size() < 200);
  assert(!p.reverse || !out.empty());
  return out;
}

// ALIGNMENTLAYOUT <name>[, CURVES|NOCURVES][, FROM=<x> <y>][, DIRECTION=<azimuth deg>]   (REQ-485.1)
// Tangent-Tangent layout: each click or typed point is a PI; with curves every interior PI gets the Curve and Spiral
// Settings curve. An alignment that already has geometry is continued from its end. U takes back a PI; Enter or ESC
// finishes and keeps what was built; one UNDO afterwards removes the whole session.
void RunAlignmentLayout(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitFields(args);
  assert(!f.empty());
  alignment::Alignment* a = Resolve(st, "ALIGNMENTLAYOUT", f[0], log);
  if (a == nullptr) return;
  std::optional<alignment::Point2> from;
  std::optional<double> direction;
  std::optional<bool> curves;
  for (size_t i = 1; i < f.size(); ++i) {
    const std::string key = Lower(Trim(f[i].substr(0, f[i].find('='))));
    const std::string val = f[i].find('=') == std::string::npos ? std::string() : Trim(f[i].substr(f[i].find('=') + 1));
    alignment::Point2 p;
    double d = 0.0;
    if (key == "curves" || key == "nocurves") curves = key == "curves";
    else if (key == "from" && ParseSpacedPoint(val, &p)) from = p;
    else if (key == "direction" && ParseNum(val, &d)) direction = d * kDegToRad;
    else {
      log.push_back("ALIGNMENTLAYOUT - usage: ALIGNMENTLAYOUT <name>[, CURVES|NOCURVES][, FROM=<x> <y>][, DIRECTION=<azimuth deg>].");
      return;
    }
  }
  const std::string name = a->name;
  const alignment::Alignment base = *a;
  if (!BeginInteractive(st, AppCommandState::Kind::AlignmentLayout, "ALIGNMENTLAYOUT", log)) return;
  st.alignmentLayout = {};
  st.alignmentLayout.name = name;
  st.alignmentLayout.base = base;
  st.alignmentLayoutToolsName = name;
  if (curves) st.alignmentCurveSettings.addCurves = *curves;
  const char* mode = st.alignmentCurveSettings.addCurves ? "with curves" : "no curves";
  if (!base.Empty()) {
    st.alignmentLayout.pis = {base.EndPoint()};
    log.push_back("ALIGNMENTLAYOUT - Tangent-Tangent (" + std::string(mode) + ") continues \"" + name + "\" from station " +
                  Sta(st, base, base.EndStation()) + ". Specify the next point or [Undo]:");
    return;
  }
  if (from) {
    alignment::Point2 lp;
    CadCoord::LocalFromWorld(st, from->x, from->y, &lp.x, &lp.y);
    st.alignmentLayout.pis = {lp};
    st.alignmentLayout.fixedDirection = direction.has_value();
    st.alignmentLayout.directionAzimuth = direction.value_or(0.0);
  }
  log.push_back("ALIGNMENTLAYOUT - Tangent-Tangent (" + std::string(mode) + ", radius " +
                Lin(st, EffectiveAlignmentCurveRadius(st)) + ") for \"" + name + "\". " +
                (from ? "Specify the next point or [Undo]:" : "Specify the start point:"));
}

// ALIGNMENTCURVESETTINGS [RADIUS=<r>|DEFAULT][, SPIRALIN=<length>|OFF][, SPIRALOUT=<length>|OFF]   (REQ-485.1)
void RunAlignmentCurveSettings(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  alignment::CurveSettings cs = st.alignmentCurveSettings;
  assert(cs.radius >= 0.0);
  for (const std::string& field : SplitFields(args)) {
    if (field.empty()) continue;
    const size_t eq = field.find('=');
    const std::string key = Lower(Trim(field.substr(0, eq)));
    const std::string val = eq == std::string::npos ? std::string() : Lower(Trim(field.substr(eq + 1)));
    double d = 0.0;
    const bool off = val == "off" || val == "0";
    if (key == "radius" && (val == "default" || (ParseNum(val, &d) && d > 0.0))) cs.radius = val == "default" ? 0.0 : d;
    else if (key == "spiralin" && (off || (ParseNum(val, &d) && d > 0.0))) cs.spiralInLength = off ? 0.0 : d;
    else if (key == "spiralout" && (off || (ParseNum(val, &d) && d > 0.0))) cs.spiralOutLength = off ? 0.0 : d;
    else {
      log.push_back("ALIGNMENTCURVESETTINGS - usage: ALIGNMENTCURVESETTINGS [RADIUS=<r>|DEFAULT][, SPIRALIN=<length>|OFF]"
                    "[, SPIRALOUT=<length>|OFF].");
      return;
    }
  }
  st.alignmentCurveSettings = cs;
  const double r = EffectiveAlignmentCurveRadius(st);
  assert(r > 0.0);
  auto spiral = [&](double len) { return len > 0.0 ? Lin(st, len) + " (A " + Lin(st, std::sqrt(r * len)) + ")" : std::string("off"); };
  log.push_back("ALIGNMENTCURVESETTINGS - clothoid; curve radius " + Lin(st, r) + (cs.radius > 0.0 ? "" : " (default)") +
                ", spiral in " + spiral(cs.spiralInLength) + ", spiral out " + spiral(cs.spiralOutLength) + ".");
}

bool SubmitAlignmentCommandPoint(AppCommandState& st, double localX, double localY, std::vector<std::string>& log) {
  assert(std::isfinite(localX) && std::isfinite(localY));
  if (st.active == AppCommandState::Kind::AlignmentLayout) return SubmitLayoutPoint(st, {localX, localY}, log);
  if (st.active == AppCommandState::Kind::AlignmentFromObjects) return SubmitFromObjectsPick(st, localX, localY, log);
  assert(st.active != AppCommandState::Kind::AlignmentLayout);
  return false;
}

bool HandleAlignmentCommandText(AppCommandState& st, const std::string& line, std::vector<std::string>& log) {
  assert(line.size() < line.max_size());
  if (st.active == AppCommandState::Kind::AlignmentLayout) return HandleLayoutText(st, line, log);
  if (st.active == AppCommandState::Kind::AlignmentFromObjects) return HandleFromObjectsText(st, line, log);
  assert(st.active != AppCommandState::Kind::AlignmentFromObjects);
  return false;
}

void CancelAlignmentCommand(AppCommandState& st, std::vector<std::string>& log) {
  assert(st.active == AppCommandState::Kind::AlignmentLayout || st.active == AppCommandState::Kind::AlignmentFromObjects);
  if (st.active == AppCommandState::Kind::AlignmentLayout) {
    FinishLayout(st, log);  // Civil 3D keeps what was laid out
    return;
  }
  st.active = AppCommandState::Kind::None;
  st.selection.clear();
  st.alignmentFromObjects = {};
  assert(st.active == AppCommandState::Kind::None);
  log.push_back("CREATEALIGNMENTFROMOBJECTS canceled - nothing was created.");
}

const char* AlignmentCommandPrompt(const AppCommandState& st) {
  using P = AppCommandState::AlignmentFromObjectsPick::Phase;
  if (st.active == AppCommandState::Kind::AlignmentLayout)
    return st.alignmentLayout.pis.empty()
               ? "ALIGNMENTLAYOUT: Specify start point (click or X,Y) | ESC ends"
               : "ALIGNMENTLAYOUT: Specify next point or [Undo] (click, X,Y or @dx,dy) | Enter or ESC ends";
  if (st.active != AppCommandState::Kind::AlignmentFromObjects) return nullptr;
  switch (st.alignmentFromObjects.phase) {
    case P::WaitFirst: return "CREATEALIGNMENTFROMOBJECTS: Select the first line/arc/polyline | ESC cancels";
    case P::WaitMore: return "CREATEALIGNMENTFROMOBJECTS: Select lines/arcs or polylines to create alignment | Enter when done";
    case P::WaitDirection: return "CREATEALIGNMENTFROMOBJECTS: Press Enter to accept alignment direction or [Reverse]";
  }
  return nullptr;
}
