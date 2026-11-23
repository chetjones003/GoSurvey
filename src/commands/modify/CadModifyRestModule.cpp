// ADR-0012 / Issue #1578 (Track B Batch 1): Step-engine command modules for
// ARRAY, LENGTHEN, EXTEND, BREAK, FILLET, CHAMFER, TRIM, TRIMSTATE, OFFSET,
// DELETE, JOIN, OVERKILL, and ALIGN.

#include "CadCommandsInternal.hpp"
#include "modify/CadModifyInternal.hpp"
#include "modify/CadModifyRestSessions.hpp"
#include "engine/CommandRegistry.hpp"
#include "engine/StepEngine.hpp"
#include "CadCoordinateFrame.hpp"

#include <GoSurvey/Core/StringUtil.hpp>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

void SubmitOffsetViewportPick(AppCommandState& st, double wx, double wy, std::vector<std::string>& log);

namespace stepengine {

// ============================================================================
// ARRAY
// ============================================================================

StepDescriptor ArraySession::currentStep(const AppCommandState&) const {
  switch (phase) {
  case ArrayPhase::PickSelection:
    return StepDescriptor::SelectObjects(kSelectObjectsPrompt, BlankEnterPolicy::Custom, true);
  case ArrayPhase::Rect_WaitColumnSpacing:
  case ArrayPhase::Rect_WaitRowSpacing:
  case ArrayPhase::Polar_WaitCenter:
  case ArrayPhase::Polar_WaitAngle:
    return StepDescriptor::Custom("", StepClickRoute::SnappedPointPick);
  case ArrayPhase::WaitType:
  case ArrayPhase::Rect_WaitColumns:
  case ArrayPhase::Rect_WaitRows:
  case ArrayPhase::Rect_WaitLevels:
  case ArrayPhase::Rect_WaitLevelSpacing:
  case ArrayPhase::Polar_WaitItemCount:
  case ArrayPhase::Polar_WaitRotateAnswer:
    return StepDescriptor::Custom("", StepClickRoute::Ignore);
  }
  return StepDescriptor::Custom("", StepClickRoute::Ignore);
}

StepSubmitResult ArraySession::onText(AppCommandState& st, const std::string& trimmedLine,
                                      std::vector<std::string>& log) {
  return HandleArrayText(st, trimmedLine, log) ? StepSubmitResult::Handled : StepSubmitResult::Unparsed;
}

StepSubmitResult ArraySession::onBlankEnter(AppCommandState& st, std::vector<std::string>& log) {
  if (phase == ArrayPhase::PickSelection) {
    if (onPaper) {
      if (st.selectedPaperEntities.empty()) {
        log.push_back("Nothing selected — click paper objects or drag a selection window, then press Enter.");
      } else {
        phase = ArrayPhase::WaitType;
        SetArrayAnchorFromPaperSelection(st);
        log.push_back("ARRAY — select array type: [R]ectangular / [P]olar:");
      }
    } else {
      DropArrayUnsupportedFromSelection(st, log);
      if (st.selection.empty()) {
        log.push_back("Nothing selected — click objects or drag a selection window, then press Enter.");
      } else {
        phase = ArrayPhase::WaitType;
        SetArrayAnchorFromSelection(st);
        log.push_back("ARRAY — select array type: [R]ectangular / [P]olar:");
      }
    }
    return StepSubmitResult::Handled;
  }
  if (phase == ArrayPhase::Rect_WaitLevels) {
    levels = 1;
    CommitArrayRectangular(st, log);
    return StepSubmitResult::Finished;
  }
  return StepSubmitResult::Handled;
}

void ArraySession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                  std::vector<std::string>& log) {
  const double wx = pick.localX;
  const double wy = pick.localY;
  if (phase == ArrayPhase::PickSelection) {
    if (onPaper) {
      PaperLayout* layout = ActivePaperGeometryTarget(st);
      PaperRef hit{};
      if (!layout || !PickPaperEntityAt(*layout, static_cast<float>(wx), static_cast<float>(wy),
                                        std::max(0.12f, CadOffsetEntityPickTolWorld(st)), &hit,
                                        &st.drawing.blockDefs))
        return;
      const auto selected = std::find_if(st.selectedPaperEntities.begin(), st.selectedPaperEntities.end(),
                                         [&](const PaperEntityRef& item) {
                                           return item.type == hit.type && item.index == hit.index;
                                         });
      if (pick.windowSelectionSubtract) {
        if (selected != st.selectedPaperEntities.end())
          st.selectedPaperEntities.erase(selected);
      } else if (selected == st.selectedPaperEntities.end()) {
        st.selectedPaperEntities.push_back(hit);
      }
      log.push_back("ARRAY — paper selection: " + std::to_string(st.selectedPaperEntities.size()) + " object(s).");
      return;
    }
    if (st.selBoxWaitingSecond)
      FinishSelectionBox(st, pick, true, log);
    return;
  }
  if (phase == ArrayPhase::Rect_WaitColumnSpacing) {
    float px = 0.f, py = 0.f, pz = 0.f;
    CadResolvePickOnWorkPlaneAnchored(st, static_cast<float>(wx), static_cast<float>(wy), pick.pickRay,
                                      anchorX, anchorY, anchorZ, &px, &py, &pz);
    const ucs::Ucs frame = CadWorkPlaneAnchoredAt(st, anchorX, anchorY, anchorZ);
    const ucs::Point2D local = ucs::WorldToPlane(frame, {px, py, static_cast<double>(pz)});
    colSpacing = static_cast<float>(local.x);
    phase = ArrayPhase::Rect_WaitRows;
    log.push_back("ARRAY Rectangular — number of rows:");
    return;
  }
  if (phase == ArrayPhase::Rect_WaitRowSpacing) {
    float px = 0.f, py = 0.f, pz = 0.f;
    CadResolvePickOnWorkPlaneAnchored(st, static_cast<float>(wx), static_cast<float>(wy), pick.pickRay,
                                      anchorX, anchorY, anchorZ, &px, &py, &pz);
    const ucs::Ucs frame = CadWorkPlaneAnchoredAt(st, anchorX, anchorY, anchorZ);
    const ucs::Point2D local = ucs::WorldToPlane(frame, {px, py, static_cast<double>(pz)});
    rowSpacing = static_cast<float>(local.y);
    if (onPaper) {
      levels = 1;
      CommitArrayRectangular(st, log);
      return;
    }
    phase = ArrayPhase::Rect_WaitLevels;
    log.push_back("ARRAY Rectangular — number of levels <1 = 2D>:");
    return;
  }
  if (phase == ArrayPhase::Polar_WaitCenter) {
    float pz = 0.f;
    CadResolvePickOnWorkPlaneAnchored(st, static_cast<float>(wx), static_cast<float>(wy), pick.pickRay,
                                      anchorX, anchorY, anchorZ, &centerX, &centerY, &pz);
    centerZ = pz;
    phase = ArrayPhase::Polar_WaitItemCount;
    log.push_back("ARRAY Polar — number of items (total, including the original):");
    return;
  }
  if (phase == ArrayPhase::Polar_WaitAngle) {
    float px = 0.f, py = 0.f, pz = 0.f;
    CadResolvePickOnWorkPlaneAnchored(st, static_cast<float>(wx), static_cast<float>(wy), pick.pickRay,
                                      centerX, centerY, centerZ, &px, &py, &pz);
    const ucs::Ucs angleFrame = CadWorkPlaneAnchoredAt(st, centerX, centerY, centerZ);
    const ucs::Point2D angleLocal = ucs::WorldToPlane(angleFrame, {px, py, static_cast<double>(pz)});
    float deg = std::atan2(static_cast<float>(angleLocal.y), static_cast<float>(angleLocal.x)) *
                (180.f / 3.14159265358979323846f);
    if (deg < 0.f)
      deg += 360.f;
    fillAngleDeg = deg;
    phase = ArrayPhase::Polar_WaitRotateAnswer;
    log.push_back("ARRAY Polar — rotate items? [Y]es / [N]o:");
    return;
  }
}

void ArraySession::onCancel(AppCommandState&, std::vector<std::string>& log) {
  log.push_back("ARRAY canceled.");
}

// ============================================================================
// LENGTHEN
// ============================================================================

StepDescriptor LengthenSession::currentStep(const AppCommandState&) const {
  return StepDescriptor::Custom("", phase == LengthenPhase::WaitDynamicTarget
                                        ? StepClickRoute::SnappedPointPick
                                        : StepClickRoute::RawEntityPick);
}

StepSubmitResult LengthenSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                         std::vector<std::string>& log) {
  return HandleLengthenText(st, trimmedLine, log) ? StepSubmitResult::Handled : StepSubmitResult::Unparsed;
}

StepSubmitResult LengthenSession::onBlankEnter(AppCommandState& st, std::vector<std::string>& log) {
  if (phase == LengthenPhase::WaitSelectOrMode) {
    EndSession(st);
    log.push_back("LENGTHEN — finished.");
    return StepSubmitResult::Finished;
  }
  HandleLengthenText(st, "", log);
  return StepSubmitResult::Handled;
}

void LengthenSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                     std::vector<std::string>& log) {
  HandleLengthenViewportPick(st, static_cast<float>(pick.localX), static_cast<float>(pick.localY), log);
}

void LengthenSession::onCancel(AppCommandState&, std::vector<std::string>& log) {
  log.push_back("LENGTHEN canceled.");
}

// ============================================================================
// EXTEND
// ============================================================================

StepDescriptor ExtendSession::currentStep(const AppCommandState&) const {
  return StepDescriptor::EntityPick("", BlankEnterPolicy::Custom);
}

StepSubmitResult ExtendSession::onText(AppCommandState&, const std::string&, std::vector<std::string>&) {
  return StepSubmitResult::Unparsed;
}

StepSubmitResult ExtendSession::onBlankEnter(AppCommandState& st, std::vector<std::string>& log) {
  if (phase == ExtendPhase::SelectBoundaries) {
    if (boundaries.empty()) {
      log.push_back("EXTEND — pick at least one boundary edge before pressing Enter.");
    } else {
      phase = ExtendPhase::SelectTargets;
      log.push_back("EXTEND — click objects to extend (near the end to stretch). Enter when done.");
    }
    return StepSubmitResult::Handled;
  }
  EndSession(st);
  log.push_back("EXTEND — finished.");
  return StepSubmitResult::Finished;
}

void ExtendSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                   std::vector<std::string>& log) {
  HandleExtendViewportPick(st, static_cast<float>(pick.localX), static_cast<float>(pick.localY), log);
}

void ExtendSession::onCancel(AppCommandState&, std::vector<std::string>& log) {
  log.push_back("EXTEND canceled.");
}

// ============================================================================
// BREAK
// ============================================================================

StepDescriptor BreakSession::currentStep(const AppCommandState&) const {
  return StepDescriptor::Custom("", phase == BreakPhase::SelectFirstPoint
                                        ? StepClickRoute::RawEntityPick
                                        : StepClickRoute::SnappedPointPick);
}

StepSubmitResult BreakSession::onText(AppCommandState&, const std::string&, std::vector<std::string>&) {
  return StepSubmitResult::Unparsed;
}

StepSubmitResult BreakSession::onBlankEnter(AppCommandState& st, std::vector<std::string>& log) {
  if (phase == BreakPhase::SelectFirstPoint) {
    EndSession(st);
    log.push_back("BREAK — finished.");
    return StepSubmitResult::Finished;
  }
  log.push_back("BREAK — specify second break point in the viewport.");
  return StepSubmitResult::Handled;
}

void BreakSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                  std::vector<std::string>& log) {
  HandleBreakViewportPick(st, static_cast<float>(pick.localX), static_cast<float>(pick.localY), log);
}

void BreakSession::onCancel(AppCommandState&, std::vector<std::string>& log) {
  log.push_back("BREAK canceled.");
}

// ============================================================================
// FILLET
// ============================================================================

StepDescriptor FilletSession::currentStep(const AppCommandState& st) const {
  char buf[160];
  if (CadSubObjectSelectionIsAllEdges(st)) {
    std::snprintf(buf, sizeof(buf),
                  "FILLET: %d solid edge(s) | Ctrl+click more, radius <%.4f> | ESC cancel",
                  static_cast<int>(st.subObjectSelection.size()),
                  static_cast<double>(st.filletRadius));
    return StepDescriptor::EntityPick(buf, BlankEnterPolicy::Custom);
  }
  if (solidAwaitingRadius) {
    std::snprintf(buf, sizeof(buf), "FILLET: radius for %d solid edge(s) <%.4f> | ESC cancel",
                  static_cast<int>(st.subObjectSelection.size()),
                  static_cast<double>(st.filletRadius));
    return StepDescriptor::EntityPick(buf, BlankEnterPolicy::Custom);
  }
  if (textAwaitingRadius) {
    std::snprintf(buf, sizeof(buf), "FILLET: New radius <%.3f> | ESC cancel",
                  static_cast<double>(st.filletRadius));
    return StepDescriptor::EntityPick(buf, BlankEnterPolicy::Custom);
  }
  if (textAwaitingTrim) {
    std::snprintf(buf, sizeof(buf), "FILLET: Trim mode [Trim/No trim] <%s> | ESC cancel",
                  st.cornerTrimMode ? "Trim" : "No trim");
    return StepDescriptor::EntityPick(buf, BlankEnterPolicy::Custom);
  }
  if (phase == FilletPhase::WaitSecondEntity)
    return StepDescriptor::EntityPick("FILLET: Select second object | ESC cancel", BlankEnterPolicy::Custom);
  std::snprintf(buf, sizeof(buf), "FILLET: Select first object or [Radius/Trim] <R=%.3f, %s> | ESC cancel",
                static_cast<double>(st.filletRadius), st.cornerTrimMode ? "Trim" : "No trim");
  return StepDescriptor::EntityPick(buf, BlankEnterPolicy::Custom);
}

StepSubmitResult FilletSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                       std::vector<std::string>& log) {
  return HandleFilletText(st, trimmedLine, log) ? StepSubmitResult::Handled : StepSubmitResult::Unparsed;
}

StepSubmitResult FilletSession::onBlankEnter(AppCommandState& st, std::vector<std::string>& log) {
  if (phase == FilletPhase::WaitFirstEntity && !textAwaitingRadius && !textAwaitingTrim &&
      !solidAwaitingRadius && !CadSubObjectSelectionIsAllEdges(st)) {
    EndSession(st);
    log.push_back("FILLET — finished.");
    return StepSubmitResult::Finished;
  }
  if (textAwaitingRadius || textAwaitingTrim || solidAwaitingRadius ||
      CadSubObjectSelectionIsAllEdges(st)) {
    HandleFilletText(st, "", log);
    return StepSubmitResult::Handled;
  }
  log.push_back("FILLET — specify second object in the viewport.");
  return StepSubmitResult::Handled;
}

void FilletSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                   std::vector<std::string>& log) {
  HandleFilletViewportPick(st, static_cast<float>(pick.localX), static_cast<float>(pick.localY), log, pick.pickRay);
}

void FilletSession::onCancel(AppCommandState&, std::vector<std::string>&) {
  // Historically FILLET canceled silently without an extra log line.
}

// ============================================================================
// CHAMFER
// ============================================================================

StepDescriptor ChamferSession::currentStep(const AppCommandState& st) const {
  char buf[160];
  if (CadSubObjectSelectionIsAllEdges(st)) {
    std::snprintf(buf, sizeof(buf),
                  "CHAMFER: %d solid edge(s) | Ctrl+click more, distance <%.4f> | ESC cancel",
                  static_cast<int>(st.subObjectSelection.size()),
                  static_cast<double>(st.chamferDist1));
    return StepDescriptor::EntityPick(buf, BlankEnterPolicy::Custom);
  }
  if (solidAwaitingDistance) {
    std::snprintf(buf, sizeof(buf), "CHAMFER: distance for %d solid edge(s) <%.4f> | ESC cancel",
                  static_cast<int>(st.subObjectSelection.size()),
                  static_cast<double>(st.chamferDist1));
    return StepDescriptor::EntityPick(buf, BlankEnterPolicy::Custom);
  }
  if (textAwaitingFirstValue) {
    if (st.chamferMode == 0)
      std::snprintf(buf, sizeof(buf), "CHAMFER Distance: First distance <%.3f> | ESC cancel",
                    static_cast<double>(st.chamferDist1));
    else
      std::snprintf(buf, sizeof(buf), "CHAMFER Angle: Distance <%.3f> | ESC cancel",
                    static_cast<double>(st.chamferDist1));
    return StepDescriptor::EntityPick(buf, BlankEnterPolicy::Custom);
  }
  if (textAwaitingSecondDist) {
    std::snprintf(buf, sizeof(buf), "CHAMFER: Second distance <%.3f> | ESC cancel",
                  static_cast<double>(st.chamferDist2));
    return StepDescriptor::EntityPick(buf, BlankEnterPolicy::Custom);
  }
  if (textAwaitingAngle) {
    std::snprintf(buf, sizeof(buf), "CHAMFER: Angle in degrees <%.1f> | ESC cancel",
                  static_cast<double>(st.chamferAngle));
    return StepDescriptor::EntityPick(buf, BlankEnterPolicy::Custom);
  }
  if (textAwaitingTrim) {
    std::snprintf(buf, sizeof(buf), "CHAMFER: Trim mode [Trim/No trim] <%s> | ESC cancel",
                  st.cornerTrimMode ? "Trim" : "No trim");
    return StepDescriptor::EntityPick(buf, BlankEnterPolicy::Custom);
  }
  if (phase == ChamferPhase::WaitSecondEntity)
    return StepDescriptor::EntityPick("CHAMFER: Select second object | ESC cancel", BlankEnterPolicy::Custom);
  if (st.chamferMode == 0)
    std::snprintf(buf, sizeof(buf),
                  "CHAMFER: Select first object or [Distance/Angle/Trim] <D1=%.3f, D2=%.3f, %s> | ESC cancel",
                  static_cast<double>(st.chamferDist1), static_cast<double>(st.chamferDist2),
                  st.cornerTrimMode ? "Trim" : "No trim");
  else
    std::snprintf(buf, sizeof(buf),
                  "CHAMFER: Select first object or [Distance/Angle/Trim] <D=%.3f, A=%.1f, %s> | ESC cancel",
                  static_cast<double>(st.chamferDist1), static_cast<double>(st.chamferAngle),
                  st.cornerTrimMode ? "Trim" : "No trim");
  return StepDescriptor::EntityPick(buf, BlankEnterPolicy::Custom);
}

StepSubmitResult ChamferSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                        std::vector<std::string>& log) {
  return HandleChamferText(st, trimmedLine, log) ? StepSubmitResult::Handled : StepSubmitResult::Unparsed;
}

StepSubmitResult ChamferSession::onBlankEnter(AppCommandState& st, std::vector<std::string>& log) {
  const bool awaitingValue = textAwaitingFirstValue || textAwaitingSecondDist || textAwaitingAngle ||
                             textAwaitingTrim || solidAwaitingDistance ||
                             CadSubObjectSelectionIsAllEdges(st);
  if (phase == ChamferPhase::WaitFirstEntity && !awaitingValue) {
    EndSession(st);
    log.push_back("CHAMFER — finished.");
    return StepSubmitResult::Finished;
  }
  if (awaitingValue) {
    HandleChamferText(st, "", log);
    return StepSubmitResult::Handled;
  }
  log.push_back("CHAMFER — specify second object in the viewport.");
  return StepSubmitResult::Handled;
}

void ChamferSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                    std::vector<std::string>& log) {
  HandleChamferViewportPick(st, static_cast<float>(pick.localX), static_cast<float>(pick.localY), log);
}

void ChamferSession::onCancel(AppCommandState&, std::vector<std::string>&) {
  // Historically CHAMFER canceled silently without an extra log line.
}

// ============================================================================
// TRIM
// ============================================================================

StepDescriptor TrimSession::currentStep(const AppCommandState&) const {
  StepDynInputSpec dyn;
  dyn.pointEntry = (phase == TrimPhase::CuttingLine_WaitP1 || phase == TrimPhase::CuttingLine_WaitP2);
  const char* prompt = "";
  switch (phase) {
  case TrimPhase::SelectCuttingEdges:
    prompt = "TRIM: Pick cutting edges (hover highlights) | Enter | [L] — draw the trim line | ESC cancel";
    break;
  case TrimPhase::CuttingLine_WaitP1:
    prompt = "TRIM: First point of the trim line | [T] — pick cutting edges instead | ESC cancel";
    break;
  case TrimPhase::CuttingLine_WaitP2:
    prompt = "TRIM: Second point — dashed = removed part (midpoint picks side) | Ortho | ESC";
    break;
  case TrimPhase::SelectTrimTargets:
    prompt = "TRIM: Click segment near end to remove | Enter done | ESC cancel";
    break;
  }
  return StepDescriptor::Custom(prompt, StepClickRoute::TrimPick, BlankEnterPolicy::Custom,
                                /*rawWorldSelRect=*/true, /*includeSurvey=*/false, dyn);
}

StepSubmitResult TrimSession::onText(AppCommandState&, const std::string& trimmedLine,
                                     std::vector<std::string>& log) {
  const std::string low = StringUtil::toLowerAsciiCopy(trimmedLine);
  if (low == "l" || low == "line") {
    if (phase == TrimPhase::CuttingLine_WaitP1 || phase == TrimPhase::CuttingLine_WaitP2) {
      log.push_back("TRIM — already drawing the trim line; pick its points in the viewport.");
    } else {
      cutters.clear();
      paperCutters.clear();
      phase = TrimPhase::CuttingLine_WaitP1;
      log.push_back("TRIM — draw along the segment to trim: first point (rubber band shows dashed preview).");
    }
    return StepSubmitResult::Handled;
  }
  if (low == "t" || low == "cutting" || low == "edges") {
    if (phase == TrimPhase::SelectCuttingEdges || phase == TrimPhase::SelectTrimTargets) {
      log.push_back("TRIM — already picking cutting edges.");
    } else {
      cutters.clear();
      paperCutters.clear();
      phase = TrimPhase::SelectCuttingEdges;
      log.push_back("TRIM — pick cutting edges (hover highlights), Enter when done, then click pieces to trim.");
    }
    return StepSubmitResult::Handled;
  }
  log.push_back("TRIM — viewport picks; L draws the trim line, T picks cutting edges; ESC cancels.");
  return StepSubmitResult::Handled;
}

StepSubmitResult TrimSession::onBlankEnter(AppCommandState& st, std::vector<std::string>& log) {
  const bool paperTrim = st.activeSpaceIndex != kModelSpaceIndex && !InFloatingModelSpace(st);
  if (phase == TrimPhase::SelectCuttingEdges) {
    if (paperTrim ? paperCutters.empty() : cutters.empty()) {
      log.push_back("TRIM — pick at least one cutting edge before pressing Enter.");
    } else {
      phase = TrimPhase::SelectTrimTargets;
      log.push_back("TRIM — click segments to trim (near the piece to remove). Enter when done.");
    }
    return StepSubmitResult::Handled;
  }
  if (phase == TrimPhase::CuttingLine_WaitP1 || phase == TrimPhase::CuttingLine_WaitP2) {
    log.push_back("TRIM — specify cutting-line points in the viewport.");
    return StepSubmitResult::Handled;
  }
  EndSession(st);
  log.push_back("TRIM — finished.");
  return StepSubmitResult::Finished;
}

void TrimSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                 std::vector<std::string>& log) {
  (void)SubmitTrimViewportPick(st, static_cast<float>(pick.localX), static_cast<float>(pick.localY),
                               CadOffsetEntityPickTolWorld(st), log, pick.pickRay);
}

void TrimSession::onCancel(AppCommandState&, std::vector<std::string>& log) {
  log.push_back("TRIM canceled.");
}

// ============================================================================
// TRIMSTATE
// ============================================================================

StepDescriptor TrimStateSession::currentStep(const AppCommandState&) const {
  return StepDescriptor::Text("", BlankEnterPolicy::Custom);
}

StepSubmitResult TrimStateSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                          std::vector<std::string>& log) {
  int tv = 0;
  std::istringstream iss(trimmedLine);
  if (!(iss >> tv) || !(iss >> std::ws).eof()) {
    log.push_back("TRIMSTATE — enter 0 or 1 (blank Enter keeps the current value).");
    return StepSubmitResult::Handled;
  }
  if (ApplyTrimStateValue(st, tv, log)) {
    EndSession(st);
    return StepSubmitResult::Finished;
  }
  return StepSubmitResult::Handled;
}

StepSubmitResult TrimStateSession::onBlankEnter(AppCommandState& st, std::vector<std::string>& log) {
  log.push_back("TRIMSTATE unchanged (" + std::to_string(st.trimState) + ").");
  EndSession(st);
  return StepSubmitResult::Finished;
}

void TrimStateSession::onCancel(AppCommandState& st, std::vector<std::string>& log) {
  log.push_back("TRIMSTATE unchanged (" + std::to_string(st.trimState) + ").");
}

// ============================================================================
// OFFSET
// ============================================================================

StepDescriptor OffsetSession::currentStep(const AppCommandState&) const {
  const char* prompt = "";
  switch (phase) {
  case OffsetPhase::WaitDistanceOrThrough:
    prompt = "OFFSET: Type offset distance, or T for through-point mode | ESC cancel";
    break;
  case OffsetPhase::WaitSelectEntity:
    prompt = "OFFSET: Pick line, circle, arc, ellipse, or polyline | Enter to finish | ESC cancel";
    break;
  case OffsetPhase::WaitSidePick:
    prompt = "OFFSET: Pick side of object (polyline/ellipse use closest edge) | ESC cancel";
    break;
  case OffsetPhase::WaitThroughPick:
    prompt = "OFFSET: Click the through point (line / circle / arc) | ESC cancel";
    break;
  }
  return StepDescriptor::EntityPick(prompt, BlankEnterPolicy::Custom);
}

StepSubmitResult OffsetSession::onText(AppCommandState&, const std::string& trimmedLine,
                                       std::vector<std::string>& log) {
  if (phase != OffsetPhase::WaitDistanceOrThrough) {
    log.push_back("OFFSET — use viewport picks to select the object and its side.");
    return StepSubmitResult::Handled;
  }
  const std::string low = StringUtil::toLowerAsciiCopy(trimmedLine);
  if (low == "t" || low == "through") {
    throughMode = true;
    typedDistance = 0.f;
    phase = OffsetPhase::WaitSelectEntity;
    log.push_back("OFFSET — through-point mode. Select line, circle, arc, ellipse, or polyline.");
    return StepSubmitResult::Handled;
  }
  float d = 0.f;
  if (!ParseOneFloat(trimmedLine, &d)) {
    log.push_back("OFFSET — type a positive offset distance (model units), or T for through-point mode.");
    return StepSubmitResult::Handled;
  }
  if (d <= 0.f) {
    log.push_back("OFFSET — distance must be positive.");
    return StepSubmitResult::Handled;
  }
  throughMode = false;
  typedDistance = d;
  phase = OffsetPhase::WaitSelectEntity;
  log.push_back("OFFSET — select line, circle, arc, ellipse, or polyline.");
  return StepSubmitResult::Handled;
}

StepSubmitResult OffsetSession::onBlankEnter(AppCommandState& st, std::vector<std::string>& log) {
  if (phase == OffsetPhase::WaitSelectEntity) {
    log.push_back("OFFSET complete.");
    st.offsetHoverHighlightValid = false;
    st.offsetHoverEntity = {};
    EndSession(st);
    return StepSubmitResult::Finished;
  }
  if (phase == OffsetPhase::WaitDistanceOrThrough)
    log.push_back("OFFSET — type a positive distance, or T for through-point mode.");
  else if (phase == OffsetPhase::WaitSidePick)
    log.push_back("OFFSET — pick which side to offset.");
  else
    log.push_back("OFFSET — click the through point.");
  return StepSubmitResult::Handled;
}

void OffsetSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                   std::vector<std::string>& log) {
  SubmitOffsetViewportPick(st, pick.localX, pick.localY, log);
}

void OffsetSession::onCancel(AppCommandState& st, std::vector<std::string>& log) {
  st.offsetHoverHighlightValid = false;
  st.offsetHoverEntity = {};
  log.push_back("OFFSET canceled.");
}

void OffsetSession::onReset(AppCommandState& st) {
  st.offsetHoverHighlightValid = false;
  st.offsetHoverEntity = {};
}

// ============================================================================
// DELETE
// ============================================================================

StepDescriptor DeleteSession::currentStep(const AppCommandState&) const {
  return StepDescriptor::SelectObjects(kSelectObjectsPrompt, BlankEnterPolicy::Custom, false);
}

StepSubmitResult DeleteSession::onText(AppCommandState&, const std::string&,
                                       std::vector<std::string>& log) {
  log.push_back("DELETE — click objects or drag a selection window, then press Enter. ESC cancels.");
  return StepSubmitResult::Handled;
}

StepSubmitResult DeleteSession::onBlankEnter(AppCommandState& st, std::vector<std::string>& log) {
  if (st.selection.empty() && st.selectedSurveyPointIndices.empty()) {
    log.push_back("Nothing selected — click objects or drag a selection window, then press Enter.");
    return StepSubmitResult::Handled;
  }
  if (!st.selectedSurveyPointIndices.empty())
    DeleteSelectedSurveyPoints(st, log);
  if (!st.selection.empty())
    ExecuteDeleteSelection(st, log);
  EndSession(st);
  return StepSubmitResult::Finished;
}

void DeleteSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                   std::vector<std::string>& log) {
  if (st.selBoxWaitingSecond)
    FinishSelectionBox(st, pick, false, log);
}

void DeleteSession::onCancel(AppCommandState&, std::vector<std::string>& log) {
  log.push_back("DELETE canceled.");
}

// ============================================================================
// JOIN
// ============================================================================

StepDescriptor JoinSession::currentStep(const AppCommandState&) const {
  return StepDescriptor::SelectObjects(kSelectObjectsPrompt, BlankEnterPolicy::Custom, false);
}

StepSubmitResult JoinSession::onText(AppCommandState&, const std::string&,
                                     std::vector<std::string>& log) {
  log.push_back("JOIN — click objects or drag a selection window, then press Enter. ESC cancels.");
  return StepSubmitResult::Handled;
}

StepSubmitResult JoinSession::onBlankEnter(AppCommandState& st, std::vector<std::string>& log) {
  const bool paperActive = ActivePaperGeometryTarget(st) != nullptr;
  if (paperActive ? st.selectedPaperEntities.empty() : st.selection.empty()) {
    log.push_back("Nothing selected — click objects or drag a selection window, then press Enter.");
    return StepSubmitResult::Handled;
  }
  ExecuteJoinSelection(st, log);
  EndSession(st);
  return StepSubmitResult::Finished;
}

void JoinSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                 std::vector<std::string>& log) {
  if (st.selBoxWaitingSecond)
    FinishSelectionBox(st, pick, false, log);
}

void JoinSession::onCancel(AppCommandState&, std::vector<std::string>& log) {
  log.push_back("JOIN canceled.");
}

// ============================================================================
// ALIGN
// ============================================================================

StepDescriptor AlignSession::currentStep(const AppCommandState&) const {
  if (phase == AlignPhase::PickSelection)
    return StepDescriptor::SelectObjects(kSelectObjectsPrompt, BlankEnterPolicy::Custom, true);
  if (phase == AlignPhase::PickSrc) {
    return StepDescriptor::Custom(
        "ALIGN: pick SOURCE survey point in drawing (snap to it) — Enter to solve",
        StepClickRoute::SnappedPointPick);
  }
  return StepDescriptor::Custom(
      "ALIGN: pick/type DESTINATION real-world X,Y for this source point",
      StepClickRoute::SnappedPointPick);
}

StepSubmitResult AlignSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                      std::vector<std::string>& log) {
  if (phase == AlignPhase::PickSelection) {
    log.push_back("ALIGN — click objects or drag a selection window in the drawing, then press Enter.");
    return StepSubmitResult::Handled;
  }
  float px = 0.f, py = 0.f;
  if (!ParseStoragePoint(st, trimmedLine, &px, &py, false, 0.f, 0.f)) {
    log.push_back("ALIGN — type X,Y for the point, or press Enter to apply with current pairs.");
    return StepSubmitResult::Handled;
  }
  if (phase == AlignPhase::PickSrc) {
    AppCommandState::AlignControlPt cp{};
    cp.srcX = px;
    cp.srcY = py;
    st.alignControlPts.push_back(cp);
    phase = AlignPhase::PickDst;
    log.push_back("ALIGN — destination for pair " + std::to_string(st.alignControlPts.size()) +
                  " (pick or type real-world X,Y):");
  } else {
    st.alignControlPts.back().dstX = px;
    st.alignControlPts.back().dstY = py;
    phase = AlignPhase::PickSrc;
    const size_t n = st.alignControlPts.size();
    log.push_back("ALIGN — pair " + std::to_string(n) + " added.  Pick next source, or Enter to apply (" +
                  std::to_string(n) + " pair" + (n == 1 ? "" : "s") + " ready).");
  }
  return StepSubmitResult::Handled;
}

StepSubmitResult AlignSession::onBlankEnter(AppCommandState& st, std::vector<std::string>& log) {
  if (phase == AlignPhase::PickSelection) {
    st.alignSelectionSnapshot = st.selection;
    st.alignSurveySnapshot = st.selectedSurveyPointIndices;
    st.alignHasSelection = !st.alignSelectionSnapshot.empty() || !st.alignSurveySnapshot.empty();
    phase = AlignPhase::PickSrc;
    const int nCad = static_cast<int>(st.alignSelectionSnapshot.size());
    const int nSrv = static_cast<int>(st.alignSurveySnapshot.size());
    if (st.alignHasSelection) {
      log.push_back("ALIGN — " + std::to_string(nCad) + " CAD, " + std::to_string(nSrv) +
                    " survey point(s). Pick SOURCE survey point 1 in drawing (snap to it).");
    } else {
      log.push_back("ALIGN — no selection; all geometry will be transformed. "
                    "Pick SOURCE survey point 1 in drawing (snap to it).");
    }
    return StepSubmitResult::Handled;
  }
  ExecuteAlignCommand(st, log);
  return st.active == CadCommandKind::None ? StepSubmitResult::Finished : StepSubmitResult::Handled;
}

void AlignSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                  std::vector<std::string>& log) {
  if (phase == AlignPhase::PickSelection) {
    if (st.selBoxWaitingSecond)
      FinishSelectionBox(st, pick, true, log);
    return;
  }
  if (phase == AlignPhase::PickSrc) {
    AppCommandState::AlignControlPt cp{};
    cp.srcX = static_cast<float>(pick.localX);
    cp.srcY = static_cast<float>(pick.localY);
    st.alignControlPts.push_back(cp);
    phase = AlignPhase::PickDst;
    log.push_back("ALIGN — destination for pair " + std::to_string(st.alignControlPts.size()) +
                  " (pick or type real-world X,Y):");
  } else {
    st.alignControlPts.back().dstX = static_cast<float>(pick.localX);
    st.alignControlPts.back().dstY = static_cast<float>(pick.localY);
    phase = AlignPhase::PickSrc;
    const size_t n = st.alignControlPts.size();
    log.push_back("ALIGN — pair " + std::to_string(n) + " added.  Pick next source, or Enter to apply (" +
                  std::to_string(n) + " pair" + (n == 1 ? "" : "s") + " ready).");
  }
}

void AlignSession::onCancel(AppCommandState& st, std::vector<std::string>& log) {
  st.alignControlPts.clear();
  st.alignSelectionSnapshot.clear();
  st.alignSurveySnapshot.clear();
  st.alignHasSelection = false;
  st.selBoxWaitingSecond = false;
  log.push_back("ALIGN canceled.");
}

// ============================================================================
// Registry contributor
// ============================================================================

void RegisterModifyRestCommands(CommandRegistry& reg) {
  reg.Add({"array", "ar", "Rectangular or polar array of copies",
           CadCommandKind::Array, false, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartArrayCommand(st, log);
           }});
  reg.Add({"lengthen", "len", "Change an object's length (DElta/Percent/Total/DYnamic)",
           CadCommandKind::Lengthen, true, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartLengthenCommand(st, log);
           }});
  reg.Add({"extend", "ex", "Extend objects to a boundary edge",
           CadCommandKind::Extend, true, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartExtendCommand(st, log);
           }});
  reg.Add({"break", "br", "Split an object at one or two picked points",
           CadCommandKind::Break, true, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartBreakCommand(st, log);
           }});
  reg.Add({"fillet", "f", "Round a corner between two curves with a tangent arc (Radius/Trim)",
           CadCommandKind::Fillet, true, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             if (CadSubObjectSelectionIsAllEdges(st))
               CadFilletSolidEdges(st, args, log);
             else
               StartFilletCommand(st, log);
           }});
  reg.Add({"chamfer", "cha", "Connect two curves with a straight bevel (Distance/Angle/Trim)",
           CadCommandKind::Chamfer, true, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             if (CadSubObjectSelectionIsAllEdges(st))
               CadChamferSolidEdges(st, args, log);
             else
               StartChamferCommand(st, log);
           }});
  reg.Add({"delete", "del, erase", "Erase objects",
           CadCommandKind::Delete, true, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartDeleteCommand(st, log);
           }});
  reg.Add({"join", "j", "Join collinear objects",
           CadCommandKind::Join, true, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartJoinCommand(st, log);
           }});
  reg.Add({"trim", "tr", "Trim objects to an edge",
           CadCommandKind::Trim, true, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartTrimCommand(st, log);
           }});
  reg.Add({"trimstate", "", "TRIM mode: 0 = draw a line to trim (default), 1 = pick cutting edges",
           CadCommandKind::TrimState, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             if (!args.empty()) {
               std::istringstream iss(args);
               int tv = 0;
               if (iss >> tv) {
                 ApplyTrimStateValue(st, tv, log);
                 return;
               }
             }
             StartTrimStateCommand(st, log);
           }});
  reg.Add({"offset", "o", "Offset at a distance",
           CadCommandKind::Offset, true, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartOffsetCommand(st, log);
           }});
  reg.Add({"overkill", "ok", "Remove duplicate geometry",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             ExecuteOverkill(st, log);
           }});
  reg.Add({"align", "al", "Align objects to others",
           CadCommandKind::Align, false, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartAlignCommand(st, log);
           }});
}

} // namespace stepengine
