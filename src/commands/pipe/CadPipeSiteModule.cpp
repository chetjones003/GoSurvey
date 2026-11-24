// ADR-0012 / Issue #1578: Pipe, Revcloud, FeatureLine, Grading, and PadSolid module (Batch 3).

#include "CadCommandsInternal.hpp"
#include "CadBlocks.hpp"
#include "surface/CadSurfaceInternal.hpp"
#include "engine/CommandRegistry.hpp"
#include "engine/StepEngine.hpp"
#include <GoSurvey/Core/StringUtil.hpp>

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

namespace stepengine {

// --- RevcloudSession ---

StepDescriptor RevcloudSession::currentStep(const AppCommandState& st) const {
  if (phase == RevcloudPhase::WaitObject)
    return StepDescriptor::EntityPick(CadRevcloudPromptText(st), BlankEnterPolicy::Custom);
  return StepDescriptor::Custom(CadRevcloudPromptText(st), StepClickRoute::SnappedPointPick,
                                BlankEnterPolicy::Custom);
}

StepSubmitResult RevcloudSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                         std::vector<std::string>& log) {
  if (!HandleRevcloudTextInput(trimmedLine, st, log))
    log.push_back(CadRevcloudPromptText(st));
  return StepSubmitResult::Handled;
}

StepSubmitResult RevcloudSession::onBlankEnter(AppCommandState& st, std::vector<std::string>& log) {
  (void)HandleRevcloudTextInput("", st, log);
  return StepSubmitResult::Handled;
}

void RevcloudSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                     std::vector<std::string>& log) {
  SubmitRevcloudViewportPick(st, static_cast<float>(pick.localX), static_cast<float>(pick.localY), log);
}

void RevcloudSession::onCancel(AppCommandState&, std::vector<std::string>& log) {
  log.push_back("REVCLOUD canceled.");
}

// --- PipeRunSession ---

StepDescriptor PipeRunSession::currentStep(const AppCommandState& st) const {
  if (phase == PipeRunPhase::WaitFirstPoint)
    return StepDescriptor::Point(CadPipeRunPromptText(st), {}, BlankEnterPolicy::Custom);
  if (phase == PipeRunPhase::WaitNextPoint) {
    StepDynInputSpec dyn;
    if (draftVerts.size() >= 3) {
      const size_t n = draftVerts.size();
      dyn.haveBase = true;
      dyn.baseIsWorld = false;
      dyn.basePoint = ray3d::Vec3{draftVerts[n - 3], draftVerts[n - 2], draftVerts[n - 1]};
      dyn.initialRelativePolar = true;
      dyn.directDistance = st.pipeRunCompassOn;
    }
    return StepDescriptor::Point(CadPipeRunPromptText(st), dyn, BlankEnterPolicy::Finish);
  }
  // WaitNominalSize / WaitWallThickness: viewport click still routes to SnappedPointPick so
  // SubmitPipeRunViewportPick can report the refusal rather than silently ignoring the click.
  return StepDescriptor::Custom(CadPipeRunPromptText(st), StepClickRoute::SnappedPointPick,
                                BlankEnterPolicy::TakeDefault);
}

StepSubmitResult PipeRunSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                        std::vector<std::string>& log) {
  if (!HandlePipeRunTextInput(trimmedLine, st, log))
    log.push_back(CadPipeRunPromptText(st));
  return StepSubmitResult::Handled;
}

StepSubmitResult PipeRunSession::onBlankEnter(AppCommandState& st, std::vector<std::string>& log) {
  (void)HandlePipeRunTextInput("", st, log);
  return StepSubmitResult::Handled;
}

void PipeRunSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                    std::vector<std::string>& log) {
  SubmitPipeRunViewportPick(st, static_cast<float>(pick.localX), static_cast<float>(pick.localY), log);
}

void PipeRunSession::onCancel(AppCommandState& st, std::vector<std::string>& log) {
  log.push_back("PIPERUN canceled.");
  CancelPipeRunCommand(st);
}

void PipeRunSession::onReset(AppCommandState& st) {
  CancelPipeRunCommand(st);
}

// --- PipeFitSession ---

StepDescriptor PipeFitSession::currentStep(const AppCommandState&) const {
  return StepDescriptor::Custom(
      "PIPEFIT - pick a point on the run, or type X,Y,Z. ESC to cancel.",
      StepClickRoute::SnappedPointPick, BlankEnterPolicy::Custom);
}

StepSubmitResult PipeFitSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                        std::vector<std::string>& log) {
  if (!HandlePipeFitTextInput(trimmedLine, st, log))
    log.push_back("PIPEFIT - pick a point on the run, or type X,Y,Z. ESC to cancel.");
  return StepSubmitResult::Handled;
}

void PipeFitSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                    std::vector<std::string>& log) {
  SubmitPipeFitViewportPick(st, static_cast<float>(pick.localX), static_cast<float>(pick.localY), log);
}

void PipeFitSession::onCancel(AppCommandState&, std::vector<std::string>& log) {
  log.push_back("PIPEFIT canceled.");
}

// --- PipeSplitSession ---

StepDescriptor PipeSplitSession::currentStep(const AppCommandState&) const {
  return StepDescriptor::Custom(
      "PIPESPLIT - pick a point on the run, or type X,Y,Z. ESC to cancel.",
      StepClickRoute::SnappedPointPick, BlankEnterPolicy::Custom);
}

StepSubmitResult PipeSplitSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                          std::vector<std::string>& log) {
  if (!HandlePipeSplitTextInput(trimmedLine, st, log))
    log.push_back("PIPESPLIT - pick a point on the run, or type X,Y,Z. ESC to cancel.");
  return StepSubmitResult::Handled;
}

void PipeSplitSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                      std::vector<std::string>& log) {
  SubmitPipeSplitViewportPick(st, static_cast<float>(pick.localX), static_cast<float>(pick.localY), log);
}

void PipeSplitSession::onCancel(AppCommandState&, std::vector<std::string>& log) {
  log.push_back("PIPESPLIT canceled.");
}

// --- FeatureLineSession ---

StepDescriptor FeatureLineSession::currentStep(const AppCommandState&) const {
  if (pendingPoint) {
    char buf[192];
    std::snprintf(buf, sizeof(buf), "FEATURELINE: Elevation for %s %zu <%.3f> | ESC cancel",
                  nextIsElevPoint ? "elevation point" : "point",
                  draftElevPt.size() + 1,
                  static_cast<double>(pendingDefaultZ));
    return StepDescriptor::Custom(buf, StepClickRoute::SnappedPointPick, BlankEnterPolicy::TakeDefault);
  }
  const char* prompt =
      draftVerts.empty()
          ? "FEATURELINE: First point — click or X,Y/X,Y,Z | [E] = elevation point | [CLOSE]/[END] | ESC cancel"
          : "FEATURELINE: Next point — click or X,Y/X,Y,Z | [E] = elevation point | [CLOSE]/[END] | ESC cancel";
  return StepDescriptor::Custom(prompt, StepClickRoute::SnappedPointPick, BlankEnterPolicy::Custom);
}

StepSubmitResult FeatureLineSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                            std::vector<std::string>& log) {
  const std::string lowFl = StringUtil::toLowerAsciiCopy(trimmedLine);

  if (pendingPoint) {
    char* zEnd = nullptr;
    const double zv = std::strtod(trimmedLine.c_str(), &zEnd);
    if (!zEnd || *zEnd != '\0' || !std::isfinite(zv)) {
      char buf[192];
      std::snprintf(buf, sizeof(buf),
                    "FEATURELINE — elevation must be a number. Type one, or Enter for <%.3f>:",
                    static_cast<double>(pendingDefaultZ));
      log.push_back(buf);
      return StepSubmitResult::Handled;
    }
    CommitFeatureLinePendingPoint(st, static_cast<float>(zv), log);
    return StepSubmitResult::Handled;
  }

  if (lowFl == "close" || lowFl == "cl") {
    CommitFeatureLineDraft(st, true, log);
    return StepSubmitResult::Handled;
  }
  if (lowFl == "end") {
    CommitFeatureLineDraft(st, false, log);
    return StepSubmitResult::Handled;
  }

  bool isElevPt = false;
  std::string flText = trimmedLine;
  if (!flText.empty() && (flText[0] == 'e' || flText[0] == 'E') &&
      (flText.size() == 1 || flText[1] == ' ' || flText[1] == '\t')) {
    isElevPt = true;
    flText = StringUtil::trimCopy(flText.substr(1));
    if (flText.empty()) {
      nextIsElevPoint = true;
      log.push_back("FEATURELINE — next point will be an elevation point. Click it, or type X,Y "
                    "/ X,Y,Z.");
      return StepSubmitResult::Handled;
    }
  }
  if (nextIsElevPoint)
    isElevPt = true;

  {
    const size_t c1 = flText.find(',');
    const size_t c2 = (c1 == std::string::npos) ? std::string::npos : flText.find(',', c1 + 1);
    if (c2 != std::string::npos) {
      if (flText.find(',', c2 + 1) != std::string::npos) {
        log.push_back("FEATURELINE — too many coordinates: X,Y,Z or @dx,dy,dz.");
        return StepSubmitResult::Handled;
      }
      const std::string zText = StringUtil::trimCopy(flText.substr(c2 + 1));
      char* zEnd = nullptr;
      const double zv = std::strtod(zText.c_str(), &zEnd);
      if (zText.empty() || !zEnd || *zEnd != '\0' || !std::isfinite(zv)) {
        log.push_back("FEATURELINE — elevation must be a number: X,Y,Z or @dx,dy,dz.");
        return StepSubmitResult::Handled;
      }
      st.polylineTypedZ = static_cast<float>(zv);
      st.polylineTypedZRelative = !flText.empty() && flText[0] == '@';
      st.polylineTypedZValid = true;
      flText = flText.substr(0, c2);
    }
  }

  float fx = 0.f, fy = 0.f;
  const bool flRel = !draftVerts.empty();
  if (ParseStoragePoint(st, flText, &fx, &fy, flRel, st.anchorX, st.anchorY)) {
    if (st.polylineTypedZValid) {
      nextIsElevPoint = false;
      SubmitFeatureLineVertex(st, fx, fy, isElevPt, log);
    } else {
      nextIsElevPoint = isElevPt;
      SubmitFeatureLinePoint(st, fx, fy, log);
    }
    return StepSubmitResult::Handled;
  }
  st.polylineTypedZValid = false;
  log.push_back("FEATURELINE — click a point, or type X,Y / X,Y,Z (or @dx,dy,dz), E for an "
                "elevation point, CLOSE, END, or ESC.");
  return StepSubmitResult::Handled;
}

StepSubmitResult FeatureLineSession::onBlankEnter(AppCommandState& st,
                                                  std::vector<std::string>& log) {
  if (pendingPoint) {
    CommitFeatureLinePendingPoint(st, pendingDefaultZ, log);
    return StepSubmitResult::Handled;
  }
  return StepSubmitResult::Handled;
}

void FeatureLineSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                        std::vector<std::string>& log) {
  if (pendingPoint) {
    pendingX = static_cast<float>(pick.localX);
    pendingY = static_cast<float>(pick.localY);
    char buf[192];
    std::snprintf(buf, sizeof(buf),
                  "FEATURELINE — point moved. Elevation for %s %zu <%.3f>:",
                  nextIsElevPoint ? "elevation point" : "point",
                  draftElevPt.size() + 1,
                  static_cast<double>(pendingDefaultZ));
    log.push_back(buf);
    return;
  }
  SubmitFeatureLinePoint(st, static_cast<float>(pick.localX), static_cast<float>(pick.localY), log);
}

void FeatureLineSession::onCancel(AppCommandState&, std::vector<std::string>& log) {
  log.push_back("FEATURELINE canceled.");
}

// --- GradingSession ---

StepDescriptor GradingSession::currentStep(const AppCommandState&) const {
  if (phase == GradingPhase::WaitBaseline)
    return StepDescriptor::EntityPick("GRADING: Select baseline feature line | ESC cancel",
                                      BlankEnterPolicy::Custom);
  return StepDescriptor::KeywordOptions(
      "GRADING: Enter to grade, or Cut / Fill / Surface / Target / Side | ESC cancel",
      BlankEnterPolicy::Finish, StepClickRoute::Ignore);
}

StepSubmitResult GradingSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                        std::vector<std::string>& log) {
  ProcessGradingCommandLine(st, trimmedLine, log);
  return StepSubmitResult::Handled;
}

StepSubmitResult GradingSession::onBlankEnter(AppCommandState& st, std::vector<std::string>& log) {
  ProcessGradingCommandLine(st, "", log);
  return StepSubmitResult::Handled;
}

void GradingSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                    std::vector<std::string>& log) {
  if (phase == GradingPhase::WaitBaseline)
    HandleGradingPick(st, pick.localX, pick.localY, log);
}

void GradingSession::onCancel(AppCommandState&, std::vector<std::string>&) {}

// --- PadSolidSession ---

StepDescriptor PadSolidSession::currentStep(const AppCommandState&) const {
  if (phase == PadSolidPhase::WaitBoundary)
    return StepDescriptor::EntityPick(
        "PADSOLID: Select closed boundary polyline or feature line | ESC cancel",
        BlankEnterPolicy::Custom);
  return StepDescriptor::KeywordOptions(
      "PADSOLID: Specify pad elevation or Surface | ESC cancel",
      BlankEnterPolicy::Custom, StepClickRoute::Ignore);
}

StepSubmitResult PadSolidSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                         std::vector<std::string>& log) {
  HandlePadSolidText(st, trimmedLine, log);
  return StepSubmitResult::Handled;
}

StepSubmitResult PadSolidSession::onBlankEnter(AppCommandState& st, std::vector<std::string>& log) {
  HandlePadSolidText(st, "", log);
  return StepSubmitResult::Handled;
}

void PadSolidSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                     std::vector<std::string>& log) {
  if (phase == PadSolidPhase::WaitBoundary)
    HandlePadSolidPick(st, pick.localX, pick.localY, log);
}

void PadSolidSession::onCancel(AppCommandState&, std::vector<std::string>&) {}

// --- Registry registration ---

namespace {

void RunPipeCatalog(AppCommandState& st, const std::string& argsStr,
                    std::vector<std::string>& log) {
  std::istringstream args(argsStr);
  std::string partTypeTok, sizeTok, classTok;
  args >> partTypeTok >> sizeTok;
  if (partTypeTok.empty() || sizeTok.empty()) {
    log.push_back("PIPECATALOG - usage: PIPECATALOG <part type> <nominal size> [pressure class]. Part "
                  "types: elbow-90, elbow-45, tee, cross, reducer, flange, valve, coupling, cap, "
                  "nozzle, other.");
    return;
  }
  const CadPipePartType partType = ParseCadPipePartType(StringUtil::toLowerAsciiCopy(partTypeTok));
  if (partType == CadPipePartType::None) {
    log.push_back("PIPECATALOG - unknown part type \"" + partTypeTok + "\".");
    return;
  }
  CadPipePressureClass pressureClass = CadPipePressureClass::None;
  if (args >> classTok) {
    pressureClass = ParseCadPipePressureClass(classTok);
    if (pressureClass == CadPipePressureClass::None) {
      log.push_back("PIPECATALOG - unknown pressure class \"" + classTok + "\". Use CS150 or CS300.");
      return;
    }
  }
  std::string blockName;
  (void)CadPipeCatalogFind(st, partType, sizeTok, pressureClass, &blockName, log);
}

void RunFeatureLineList(AppCommandState& st, const std::string&, std::vector<std::string>& log) {
  const size_t n =
      st.drawing.featureLineOffsets.empty() ? 0 : st.drawing.featureLineOffsets.size() - 1;
  if (n == 0) {
    log.push_back("FEATURELINELIST — no feature lines in the drawing.");
    return;
  }
  for (size_t i = 0; i < n; ++i) {
    const int b = st.drawing.featureLineOffsets[i], e = st.drawing.featureLineOffsets[i + 1];
    const std::string nm =
        i < st.drawing.featureLineInfo.size() ? st.drawing.featureLineInfo[i].name : std::string();
    const bool closed =
        i < st.drawing.featureLineClosed.size() && st.drawing.featureLineClosed[i] != 0;
    int elevPts = 0;
    for (int v = b; v < e; ++v)
      if (static_cast<size_t>(v) < st.drawing.featureLineElevPt.size() &&
          st.drawing.featureLineElevPt[v])
        ++elevPts;
    log.push_back("Feature line " + std::to_string(i + 1) + ": \"" + nm + "\" — " +
                  std::to_string(e - b) + " vertices (" + std::to_string((e - b) - elevPts) +
                  " PI, " + std::to_string(elevPts) + " elevation point(s))" +
                  (closed ? ", closed" : "") + ", entity id " +
                  std::to_string(i < st.drawing.featureLineAttrs.size()
                                     ? st.drawing.featureLineAttrs[i].id
                                     : 0));
    for (int v = b; v < e; ++v) {
      const size_t base = static_cast<size_t>(v) * 3;
      if (base + 2 >= st.drawing.featureLineVerts.size())
        break;
      char buf[160];
      std::snprintf(buf, sizeof(buf), "  %s %d: %.3f, %.3f, elevation %.3f",
                    (static_cast<size_t>(v) < st.drawing.featureLineElevPt.size() &&
                     st.drawing.featureLineElevPt[v])
                        ? "elev pt"
                        : "PI     ",
                    v - b + 1, static_cast<double>(st.drawing.featureLineVerts[base]),
                    static_cast<double>(st.drawing.featureLineVerts[base + 1]),
                    static_cast<double>(st.drawing.featureLineVerts[base + 2]));
      log.push_back(buf);
    }
  }
}

void RunFlElevEdit(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const int flCount = st.drawing.featureLineOffsets.empty()
                          ? 0
                          : static_cast<int>(st.drawing.featureLineOffsets.size()) - 1;
  if (flCount <= 0) {
    log.push_back("FLELEVEDIT — no feature lines in the drawing. Draw one with FEATURELINE first.");
    return;
  }
  std::istringstream iss(args);
  int which = 0;
  if (iss >> which) {
    if (which < 1 || which > flCount) {
      log.push_back("FLELEVEDIT — there is no feature line " + std::to_string(which) + "; the "
                    "drawing has " + std::to_string(flCount) + ".");
      return;
    }
    st.featureLineElevIndex = which - 1;
  }
  st.showFeatureLineElevWindow = true;
  log.push_back("FLELEVEDIT — elevation editor opened on feature line " +
                std::to_string(st.featureLineElevIndex + 1) + ".");
}

void RunFlElev(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  std::istringstream fe(args);
  int flNum = 0;
  if (!(fe >> flNum)) {
    log.push_back("FLELEV — usage: FLELEV <feature line #> [SET|GRADEAHEAD|GRADEBACK|RAISE|"
                  "INSERT|DELETE ...]. FLELEV <n> alone lists the table.");
    return;
  }
  std::string sub;
  if (!(fe >> sub)) {
    std::vector<FeatureLineElevRow> rows;
    if (!BuildFeatureLineElevTable(st, flNum - 1, &rows)) {
      log.push_back("FLELEV — no feature line " + std::to_string(flNum) + ".");
      return;
    }
    const size_t ii = static_cast<size_t>(flNum) - 1;
    const std::string nm =
        ii < st.drawing.featureLineInfo.size() ? st.drawing.featureLineInfo[ii].name : std::string();
    log.push_back("FLELEV — feature line " + std::to_string(flNum) + " \"" + nm + "\": " +
                  std::to_string(rows.size()) + " points.");
    log.push_back("   #  type     station    elevation   length ahead   grade back   grade ahead");
    for (const FeatureLineElevRow& r : rows) {
      const auto gradeText = [](double pct, char* buf, size_t cap) -> const char* {
        if (std::isnan(pct))
          return "-";
        std::snprintf(buf, cap, "%.2f%%", pct);
        return buf;
      };
      char gbBuf[16], gaBuf[16];
      const char* gb = gradeText(r.gradeBackPct, gbBuf, sizeof(gbBuf));
      const char* ga = gradeText(r.gradeAheadPct, gaBuf, sizeof(gaBuf));
      char buf[256];
      std::snprintf(buf, sizeof(buf), "  %2d  %-7s  %9.3f  %11.3f  %13.3f  %11s  %12s",
                    r.vertexIndex + 1, r.isElevationPoint ? "elev pt" : "PI", r.station,
                    static_cast<double>(r.elevation), r.lengthAhead, gb, ga);
      log.push_back(buf);
    }
    return;
  }
  for (char& c : sub)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (sub == "set" || sub == "gradeahead" || sub == "gradeback" || sub == "delete") {
    int pt = 0;
    if (!(fe >> pt)) {
      log.push_back("FLELEV — " + sub + " needs a point number.");
      return;
    }
    if (sub == "delete") {
      DeleteFeatureLineElevationPoint(st, flNum, pt, log);
      return;
    }
    double val = 0.0;
    if (!(fe >> val)) {
      log.push_back("FLELEV — " + sub + " needs a " +
                    (sub == "set" ? "elevation." : "grade in percent."));
      return;
    }
    if (sub == "set")
      SetFeatureLinePointElevation(st, flNum, pt, static_cast<float>(val), log);
    else if (sub == "gradeahead")
      SetFeatureLineGradeAhead(st, flNum, pt, val, log);
    else
      SetFeatureLineGradeBack(st, flNum, pt, val, log);
    return;
  }
  if (sub == "raise") {
    double d = 0.0;
    if (!(fe >> d)) {
      log.push_back("FLELEV — RAISE needs a delta (negative lowers).");
      return;
    }
    RaiseFeatureLineElevations(st, flNum, static_cast<float>(d), log);
    return;
  }
  if (sub == "insert") {
    double station = 0.0, elev = 0.0;
    if (!(fe >> station) || !(fe >> elev)) {
      log.push_back("FLELEV — INSERT needs a station and an elevation.");
      return;
    }
    InsertFeatureLineElevationPoint(st, flNum, station, static_cast<float>(elev), log);
    return;
  }
  log.push_back("FLELEV — unknown option \"" + sub +
                "\". Use SET, GRADEAHEAD, GRADEBACK, RAISE, INSERT, or DELETE.");
}

} // namespace

void RegisterPipeSiteCommands(CommandRegistry& reg) {
  reg.Add({"revcloud", "",
           "Revision cloud: click corners round an area, or O to convert a closed shape (A arc length, R reverse, D delete original)",
           CadCommandKind::Revcloud, false, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartRevcloudCommand(st, log);
           }});
  reg.Add({"piperun", "pipe",
           "Route a pipe run: PIPERUN, nominal size [class], wall thickness (Enter = schedule 40), then points (U undo, END/Enter finishes)",
           CadCommandKind::PipeRun, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             StartPipeRunCommand(st, log);
             // "PIPERUN 4in" answers the size prompt inline, as the command line usage promises.
             if (!StringUtil::trimCopy(args).empty())
               HandlePipeRunTextInput(StringUtil::trimCopy(args), st, log);
           }});
  reg.Add({"pipesys", "pipenet, pipingsystem",
           "Named piping networks: PIPESYS NEW/ADD/REMOVE/RENAME/DELETE/LIST <name> (ADD/REMOVE use the current pipe-run selection)",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             HandlePipingSystemCommand(args, st, log);
           }});
  reg.Add({"pipecatalog", "pcat",
           "Look up a fitting in the library by size/class/type: PIPECATALOG <part type> <size> [class]",
           CadCommandKind::None, false, true, &RunPipeCatalog});
  reg.Add({"pipepalette", "pipepal",
           "Toggle the Pipe Fittings palette (REQ-350)",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             CadPipePaletteSetOpen(st, !st.pipeFittingPaletteOpen, log);
           }});
  reg.Add({"pipefit", "pfit",
           "Place a catalog fitting on the selected pipe run: PIPEFIT <part type>",
           CadCommandKind::PipeFit, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             std::istringstream iss(args);
             std::string partTypeTok;
             iss >> partTypeTok;
             StartPipeFitCommand(st, partTypeTok, log);
           }});
  reg.Add({"pipesplit", "psplit",
           "Split the selected pipe run at a picked station",
           CadCommandKind::PipeSplit, false, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartPipeSplitCommand(st, log);
           }});
  reg.Add({"pipejoin", "pjoin",
           "Join two selected collinear/coincident pipe runs of the same size and class",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             HandlePipeJoinCommand(st, log);
           }});
  reg.Add({"pipeprop", "pprop",
           "Change nominal size and optional pressure class on selected pipe runs: PIPEPROP <size> [class]",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             HandlePipePropCommand(args, st, log);
           }});
  reg.Add({"featureline", "fl",
           "Draw a feature line: named 3D linework with per-vertex elevations (REQ-087)",
           CadCommandKind::FeatureLine, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             StartFeatureLineCommand(st, args, log);
           }});
  reg.Add({"featurelinelist", "fllist", "List every feature line and its vertices",
           CadCommandKind::None, false, true, &RunFeatureLineList});
  reg.Add({"flelevedit", "featurelineelevedit",
           "Open the feature line elevation editor: FLELEVEDIT [<n>]",
           CadCommandKind::None, false, true, &RunFlElevEdit});
  reg.Add({"flelev", "featurelineelev",
           "Inspect or edit feature line elevations: FLELEV <n> [SET|GRADEAHEAD|GRADEBACK|RAISE|INSERT|DELETE ...]",
           CadCommandKind::None, false, true, &RunFlElev});
  reg.Add({"grading", "grd",
           "Side slopes from a feature line out to a surface, giving the daylight line (REQ-398)",
           CadCommandKind::Grading, false, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartGradingCommand(st, log);
           }});
  reg.Add({"gradinggroup", "",
           "Grading group: GRADINGGROUP <name>[, Add/Remove/List] with selected grading(s)",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             ExecuteGradingGroupCommand(st, args, log);
           }});
  reg.Add({"gradinginfill", "",
           "Create a grading infill from a selected closed loop",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             ExecuteGradingInfillCommand(st, args, log);
           }});
  reg.Add({"padsolid", "pad",
           "Build cut and fill solids for a building pad: PADSOLID [<surface>, <pad elevation>]",
           CadCommandKind::PadSolid, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             ExecutePadSolidCommand(st, args, log);
           }});
}

} // namespace stepengine
