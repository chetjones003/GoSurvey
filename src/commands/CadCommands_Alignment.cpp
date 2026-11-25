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
#include <GoSurvey/Survey/AlignmentStylePublic.hpp>
#include <GoSurvey/Survey/AlignmentLabelSetPublic.hpp>
#include "CadColor.hpp"
#include "TextStyle.hpp"
#include "GoSurvey/Survey/AlignmentReportModel.hpp"
#include "GoSurvey/Survey/AlignmentGroundModel.hpp"
#include <GoSurvey/Terrain/FeatureLineStations.hpp>
#include <memory>
#include "GoSurvey/Survey/AlignmentTables.hpp"
#include "GoSurvey/Survey/AlignmentLabels.hpp"
#include "GoSurvey/Survey/AlignmentDesign.hpp"  // REQ-502
#include "GoSurvey/Survey/AlignmentViewFrames.hpp"  // REQ-517
#include "PaperSpace.hpp"  // REQ-517: paper sizes for sheets
#include "ViewportZoomScale.hpp"  // REQ-517: named scales
#include "GoSurvey/Document/cadtable.hpp"
#include "GoSurvey/Document/cadtablestyle.hpp"
#include "SpreadsheetIo.hpp"
#include <GoSurvey/UI/FileDialogs.hpp>
#include <GoSurvey/Core/StringUtil.hpp>
#include <GoSurvey/Core/NumFormat.hpp>

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
#include "io/src/DwgIo.hpp"  // REQ-518.8: the title block DWG
#include <GoSurvey/Renderer/ViewportProjection.hpp>  // REQ-518.9: the north arrow follows the view
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "alignment/CadAlignmentInternal.hpp"


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
  st.drawing.alignments[static_cast<size_t>(ix)] = std::move(edited);
  const alignment::Alignment& r = st.drawing.alignments[static_cast<size_t>(ix)];
  log.push_back("ALIGNMENTADD - \"" + r.name + "\" now has " + std::to_string(r.Entities().size()) +
                " entities, length " + Lin(st, r.Length()) + ", ending at station " + Sta(st, r, r.EndStation()) + ".");
  RefreshAlignmentOffsets(st, log);  // REQ-488 linked offsets and REQ-493.2 dynamic tables follow the new entity
}

// ALIGNMENTLIST [<name>]  (REQ-501.1 properties, REQ-494 entity list)
void RunAlignmentList(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::string name = Trim(args);
  if (st.drawing.alignments.empty()) {
    log.push_back("ALIGNMENTLIST - this drawing has no alignments.");
    return;
  }
  for (const alignment::Alignment& a : st.drawing.alignments) {
    if (!name.empty() && Lower(a.name) != Lower(name)) continue;
    const std::string criteriaName = AlignmentStyles::EffectiveCheckSetName(st.drawing.alignmentStyles, a);
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
      if (const alignment::AlignmentCheckSet* checkSet = alignment::FindCheckSet(st.drawing.alignmentCheckSets, criteriaName)) {
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
  st.drawing.alignments.erase(st.drawing.alignments.begin() + ix);
  RefreshViewFrameGroups(st, log);  // REQ-517.9: the alignment's view frame groups go with it, in the same step
  BumpCadGpuCache(st);  // the plan drops the alignment's drawing at once, not at the next redraw
  log.push_back("ALIGNMENTDELETE - removed \"" + name + "\".");
}

// Keeps view frame groups in step with the alignments (REQ-517.8, REQ-517.9). Called from RefreshAlignmentOffsetsOnly,
// which every geometry edit and the alignment erase path already call, so no caller has to remember it.
void RefreshViewFrameGroups(AppCommandState& st, std::vector<std::string>& log) {
  std::vector<std::string> notes;
  alignment::SyncViewFrameGroups(st.drawing.alignments, st.drawing.viewFrameGroups, &notes);
  for (const std::string& n : notes) log.push_back(n);
  alignment::SyncSampleLines(st.drawing.alignments, st.drawing.sampleLines);  // REQ-519.13: a deleted alignment takes its lines
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
  const std::uint64_t id = st.drawing.alignments[static_cast<size_t>(ix)].id;
  size_t count = 0;
  for (const alignment::ViewFrameGroup& g : st.drawing.viewFrameGroups) count += g.alignmentId == id ? 1 : 0;
  if (count == 0) {
    log.push_back("VIEWFRAMEDELETE - \"" + name + "\" has no view frame groups.");
    return;
  }
  PushUndoSnapshot(st, "Delete view frames");
  st.drawing.viewFrameGroups.erase(std::remove_if(st.drawing.viewFrameGroups.begin(), st.drawing.viewFrameGroups.end(),
                                          [id](const alignment::ViewFrameGroup& g) { return g.alignmentId == id; }),
                           st.drawing.viewFrameGroups.end());
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
  EnsureStandardAnnotationScales(st.drawing.viewportZoomScales, st.drawing.drawingInsUnits);
  const bool autoScale = NormalizeName(f[4]) == "AUTO";
  const CadAnnotationScale* scale = nullptr;
  for (const CadAnnotationScale& candidate : st.drawing.viewportZoomScales)
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
    if (!ChooseBestFitScale(*a, s, st.drawing.viewportZoomScales, whyScale)) {
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
  st.drawing.viewFrameGroups.push_back(std::move(g));
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
  for (size_t i = 0; i < tb.drawing.lineCount(); ++i) {
    add(tb.drawing.line(i).x0, tb.drawing.line(i).y0);
    add(tb.drawing.line(i).x1, tb.drawing.line(i).y1);
  }
  for (size_t v = 0; v < tb.drawing.vertexCount(); ++v) add(tb.drawing.vertex(v).x, tb.drawing.vertex(v).y);
  return any;
}

// The title block's main border: the four-vertex polyline with the largest area.
bool TitleBlockBorder(const AppCommandState& tb, Box* out) {
  bool found = false;
  double bestArea = -1.0;
  for (size_t p = 0; p + 1 < tb.drawing.userPolylineOffsets.size(); ++p) {
    const int v0 = tb.drawing.userPolylineOffsets[p], v1 = tb.drawing.userPolylineOffsets[p + 1];
    if (v1 - v0 != 4) continue;
    Box b;
    bool first = true;
    for (int v = v0; v < v1; ++v) {
      const double x = tb.drawing.vertex(static_cast<size_t>(v)).x;
      const double y = tb.drawing.vertex(static_cast<size_t>(v)).y;
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
  for (size_t i = 0; i < tb.drawing.lineCount(); ++i) {
    const CadLineRecord ln = tb.drawing.line(i);
    sheet.paperLines.insert(sheet.paperLines.end(), {static_cast<float>(ln.x0 + dx), static_cast<float>(ln.y0 + dy), 0.f,
                                                     static_cast<float>(ln.x1 + dx), static_cast<float>(ln.y1 + dy), 0.f});
    EntityAttributes attr = i < tb.drawing.userLineAttrs.size() ? tb.drawing.userLineAttrs[i] : EntityAttributes{};
    attr.id = 0;  // the sheet's own copy takes a fresh identity when it is saved
    sheet.paperLineAttrs.push_back(attr);
  }
  if (sheet.paperPolyOffsets.empty()) sheet.paperPolyOffsets.push_back(0);
  for (size_t p = 0; p + 1 < tb.drawing.userPolylineOffsets.size(); ++p) {
    for (int v = tb.drawing.userPolylineOffsets[p]; v < tb.drawing.userPolylineOffsets[p + 1]; ++v) {
      const size_t vi = static_cast<size_t>(v);
      sheet.paperPolyVerts.push_back(static_cast<float>(tb.drawing.vertex(vi).x + dx));
      sheet.paperPolyVerts.push_back(static_cast<float>(tb.drawing.vertex(vi).y + dy));
      sheet.paperPolyVerts.push_back(0.f);
      sheet.paperPolyVertsBulge.push_back(vi < tb.drawing.userPolylineVertsBulge.size() ? tb.drawing.userPolylineVertsBulge[vi] : 0.f);
    }
    sheet.paperPolyClosed.push_back(p < tb.drawing.userPolylineClosed.size() ? tb.drawing.userPolylineClosed[p] : 0);
    EntityAttributes attr = p < tb.drawing.userPolylineAttrs.size() ? tb.drawing.userPolylineAttrs[p] : EntityAttributes{};
    attr.id = 0;
    sheet.paperPolyAttrs.push_back(attr);
    sheet.paperPolyOffsets.push_back(static_cast<int>(sheet.paperPolyVerts.size() / 3));
  }
  // REQ-518.8: the block's own text (labels and notes) comes across too. Styles and annotative scales name entries
  // the sheet's drawing does not have, so they are dropped; the baked height, font and style values are kept.
  for (const CadAnnotation& src : tb.drawing.cadAnnotations) {
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
  for (size_t i = 0; i < tb.state.drawing.lineCount(); ++i) {
    const CadLineRecord ln = tb.state.drawing.line(i);
    const double x0 = ln.x0, y0 = ln.y0, y1 = ln.y1;
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
  for (const alignment::ViewFrameGroup& g : st.drawing.viewFrameGroups)
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
  for (size_t i = 0; i < titleBlock.drawing.lineCount(); ++i) {
    const CadLineRecord ln = titleBlock.drawing.line(i);
    const double x0 = ln.x0, y0 = ln.y0, y1 = ln.y1;
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
    for (size_t i = 0; i < titleBlock.drawing.lineCount(); ++i) {
      const CadLineRecord ln = titleBlock.drawing.line(i);
      const double x0 = ln.x0 + dx, x1 = ln.x1 + dx, y0 = ln.y0 + dy, y1 = ln.y1 + dy;
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
  for (const PaperLayout& layout : st.drawing.paperLayouts) takenNames.push_back(Lower(layout.name));
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
    AddSheetFields(sheet, *a, fr, sized.settings, st.drawing.alignmentStationFormat, columnLeft, fieldRight, rowMids);
    AddNorthArrow(sheet, sheet.viewports[0], arrowX, arrowY);
    sheet.viewInit = false;  // fit the new sheet to the window on first entry
    sheets.push_back(std::move(sheet));
  }

  PushUndoSnapshot(st, "Create sheets");  // REQ-518.5: one undo step removes every sheet and the frame resize
  const size_t framesBefore = g.frames.size();
  for (alignment::ViewFrameGroup& stored : st.drawing.viewFrameGroups)
    if (stored.id == g.id) stored = sized;
  // The title block's layers come into this drawing where they are not already here.
  for (const CadLayerRow& row : titleBlock.drawing.drawingLayerTable) {
    const bool present = std::any_of(st.drawing.drawingLayerTable.begin(), st.drawing.drawingLayerTable.end(),
                                     [&row](const CadLayerRow& have) { return have.name == row.name; });
    if (!present) st.drawing.drawingLayerTable.push_back(row);
  }
  std::string names;
  for (PaperLayout& sheet : sheets) {
    if (!names.empty()) names += ", ";
    names += sheet.name;
    st.drawing.paperLayouts.push_back(std::move(sheet));
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
    const CadSurface& surface = st.drawing.cadSurfaces[static_cast<size_t>(surfaceIndex)];
    if (surface.kind != SurfaceKind::Tin || !surface.tin) {
      log.push_back("CREATESAMPLELINES - \"" + surfaceText +
                    "\" is not a built TIN surface; sections need a TIN. No sample lines were created.");
      return;
    }
  }

  std::vector<alignment::SampleLine> lines;
  std::string why;
  if (!alignment::ComputeSampleLines(*a, s, st.drawing.alignmentStationFormat, &lines, &why)) {
    log.push_back("CREATESAMPLELINES - refused: " + why + ". No sample lines were created.");
    return;
  }
  // A name already on this alignment would leave two lines the command cannot tell apart.
  for (const alignment::SampleLine& line : lines) {
    for (const alignment::SampleLine& have : st.drawing.sampleLines) {
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
    const CadSurface& surface = st.drawing.cadSurfaces[static_cast<size_t>(surfaceIndex)];
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
    st.drawing.sampleLines.push_back(std::move(line));
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
  for (const alignment::SampleLine& line : st.drawing.sampleLines)
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
  for (const PaperLayout& layout : st.drawing.paperLayouts) takenNames.push_back(Lower(layout.name));
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
  for (const CadLayerRow& row : tb.state.drawing.drawingLayerTable) {
    const bool present = std::any_of(st.drawing.drawingLayerTable.begin(), st.drawing.drawingLayerTable.end(),
                                     [&row](const CadLayerRow& have) { return have.name == row.name; });
    if (!present) st.drawing.drawingLayerTable.push_back(row);
  }
  std::string names;
  for (PaperLayout& sheet : sheets) {
    if (!names.empty()) names += ", ";
    names += sheet.name;
    st.drawing.paperLayouts.push_back(std::move(sheet));
  }
  BumpCadGpuCache(st);
  log.push_back("CREATESECTIONSHEETS - " + std::to_string(chosen.size()) + " section(s) on " + std::to_string(sheetCount) +
                " sheet(s) from \"" + a->name + "\" on \"" + kTitleBlockFile + "\": " + names + ".");
}

// ---- REQ-521: design vertical profiles along an alignment (GitHub issue #1163) ----
