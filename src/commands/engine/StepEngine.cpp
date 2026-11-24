#include "engine/StepEngine.hpp"
#include "engine/CommandRegistry.hpp"
#include "CadCommands.hpp"
#include "CadCommandsInternal.hpp"
#include "CadCoordinateFrame.hpp"
#include "OrthoConstrain.hpp"
#include "paperspace/CadPaperSpaceInternal.hpp"
#include <GoSurvey/Core/StringUtil.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace stepengine {

namespace {
constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
constexpr double kRadToDeg = 180.0 / 3.14159265358979323846;
} // namespace

// ---------------------------------------------------------------------------
// CommandSession default methods
// ---------------------------------------------------------------------------

StepSubmitResult CommandSession::onBlankEnter(AppCommandState& st, std::vector<std::string>& log) {
  const StepDescriptor step = currentStep(st);
  if (step.blankEnter == BlankEnterPolicy::Finish)
    return StepSubmitResult::Finished;
  (void)log;
  return StepSubmitResult::Handled;
}

void CommandSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                    std::vector<std::string>& log) {
  const StepDescriptor step = currentStep(st);
  if (step.kind == StepKind::SelectObjects && st.selBoxWaitingSecond) {
    FinishSelectionBox(st, pick.localX, pick.localY, pick.windowSelectionSubtract,
                       pick.fenceLeftToRightWindowMode, step.includeSurveyInSelectionBox, log);
  }
}

// ---------------------------------------------------------------------------
// Session lifecycle
// ---------------------------------------------------------------------------

void BeginSession(AppCommandState& st, std::unique_ptr<CommandSession> session,
                  bool recordAsLastCommand) {
  ResetAllCadDraftTools(st);
  ClearPendingOneShotObjectSnap(st);
  st.selBoxWaitingSecond = false;
  if (!session) {
    st.active = CadCommandKind::None;
    return;
  }
  const CadCommandKind k = session->kind();
  st.active = k;
  if (recordAsLastCommand && k != CadCommandKind::None)
    st.lastCommand = k;
  st.activeSession = std::move(session);
}

void EndSession(AppCommandState& st) {
  st.activeSession.reset();
  st.active = CadCommandKind::None;
  st.selBoxWaitingSecond = false;
  st.resolvedPointZValid = false;
  st.resolvedPointZ = 0.f;
  st.resolvedPointZTyped = false;
}

void ResetActiveSession(AppCommandState& st) {
  if (st.activeSession) {
    auto session = std::move(st.activeSession);
    session->onReset(st);
  }
  st.active = CadCommandKind::None;
  st.selBoxWaitingSecond = false;
  st.resolvedPointZValid = false;
  st.resolvedPointZ = 0.f;
  st.resolvedPointZTyped = false;
}

bool CancelActiveSession(AppCommandState& st, std::vector<std::string>& log) {
  if (!st.activeSession)
    return false;
  ClearPendingOneShotObjectSnap(st);
  auto session = std::move(st.activeSession);
  session->onCancel(st, log);
  st.active = CadCommandKind::None;
  ResetSegmentAngleLock(st);
  st.selBoxWaitingSecond = false;
  st.resolvedPointZValid = false;
  st.resolvedPointZ = 0.f;
  st.resolvedPointZTyped = false;
  st.copySurveyDupModalOpen = false;
  st.copySurveyDupModalOpenRequested = false;
  ClearEntityGripInteraction(st);
  ClearDimGripInteraction(st);
  return true;
}

// ---------------------------------------------------------------------------
// Shared selection box completion
// ---------------------------------------------------------------------------

void FinishSelectionBox(AppCommandState& st, double wx, double wy, bool windowSelectionSubtract,
                        bool fenceLeftToRightWindowMode, bool includeSurvey,
                        std::vector<std::string>& log) {
  if (PaperLayout* paper = ActivePaperGeometryTarget(st)) {
    const float mnX = std::min(st.selBoxAnchorX, static_cast<float>(wx));
    const float mxX = std::max(st.selBoxAnchorX, static_cast<float>(wx));
    const float mnY = std::min(st.selBoxAnchorY, static_cast<float>(wy));
    const float mxY = std::max(st.selBoxAnchorY, static_cast<float>(wy));
    std::vector<PaperEntityRef> hits;
    SelectPaperEntitiesInBox(*paper, mnX, mnY, mxX, mxY, fenceLeftToRightWindowMode, hits,
                             &st.drawing.blockDefs);
    if (windowSelectionSubtract) {
      st.selectedPaperEntities.erase(
          std::remove_if(st.selectedPaperEntities.begin(), st.selectedPaperEntities.end(),
                         [&](const PaperEntityRef& selected) {
                           return std::find_if(hits.begin(), hits.end(), [&](const PaperEntityRef& hit) {
                                    return hit.type == selected.type && hit.index == selected.index;
                                  }) != hits.end();
                         }),
          st.selectedPaperEntities.end());
    } else {
      for (const PaperEntityRef& hit : hits) {
        const bool alreadySelected = std::any_of(
            st.selectedPaperEntities.begin(), st.selectedPaperEntities.end(),
            [&](const PaperEntityRef& selected) {
              return selected.type == hit.type && selected.index == hit.index;
            });
        if (!alreadySelected)
          st.selectedPaperEntities.push_back(hit);
      }
    }
    st.paperSelBoxLastValid = true;
    st.paperSelBoxLastMnXIn = mnX;
    st.paperSelBoxLastMxXIn = mxX;
    st.paperSelBoxLastMnYIn = mnY;
    st.paperSelBoxLastMxYIn = mxY;
    st.selBoxWaitingSecond = false;
    log.push_back(std::string(fenceLeftToRightWindowMode ? "Window" : "Fence") + " — CAD " +
                  std::to_string(hits.size()) + ", survey 0 (" +
                  (fenceLeftToRightWindowMode ? "window" : "crossing") + ").");
    return;
  }

  const Camera boxSelCamValue = CadViewCamera(st);
  const Camera* const boxSelCam = CadViewIsPlan(st) ? nullptr : &boxSelCamValue;
  ComputeSelectionFromRect(st, st.selBoxAnchorX, st.selBoxAnchorY, st.selBoxAnchorZ,
                           static_cast<float>(wx), static_cast<float>(wy), st.uiCursorWorldZ,
                           windowSelectionSubtract, fenceLeftToRightWindowMode, includeSurvey,
                           boxSelCam, st.uiViewportWidthPx, st.uiViewportHeightPx);
  st.selBoxWaitingSecond = false;
  st.subObjectSelection.clear();
  log.push_back("Fence — CAD " + std::to_string(st.selection.size()) + ", survey " +
                std::to_string(st.selectedSurveyPointIndices.size()) +
                (fenceLeftToRightWindowMode ? " (window)." : " (crossing)."));
}

// ---------------------------------------------------------------------------
// Queries driven from the active session's current step
// ---------------------------------------------------------------------------

StepClickRoute ActiveClickRoute(const AppCommandState& st) {
  if (!st.activeSession)
    return StepClickRoute::IdleSelection;
  return st.activeSession->currentStep(st).clickRoute;
}

bool ActiveUseRawWorldForSelectionRect(const AppCommandState& st) {
  if (!st.activeSession)
    return st.active == CadCommandKind::None;
  return st.activeSession->currentStep(st).rawWorldForSelectionRect;
}

bool ActiveIsZoomRegionBox(const AppCommandState& st) {
  if (!st.activeSession)
    return st.active == CadCommandKind::Zoom;
  return st.activeSession->currentStep(st).isZoomRegionBox;
}

bool ActiveExpectsPointEntry(const AppCommandState& st) {
  if (!st.activeSession)
    return false;
  return st.activeSession->currentStep(st).dynInput.pointEntry;
}

bool ActiveUcsPolarPromptBase(const AppCommandState& st, ray3d::Vec3* baseWorld) {
  if (!st.activeSession || !baseWorld)
    return false;
  const StepDynInputSpec dyn = st.activeSession->currentStep(st).dynInput;
  if (!dyn.ucsPolar)
    return false;
  *baseWorld = dyn.ucsPolarBase;
  return true;
}

CadDynInputPrompt ActiveDynInputPromptFor(const AppCommandState& st) {
  CadDynInputPrompt p;
  if (!st.activeSession)
    return p;
  const StepDynInputSpec dyn = st.activeSession->currentStep(st).dynInput;
  p.pointEntry = dyn.pointEntry;
  if (!p.pointEntry)
    return p;
  if (dyn.ucsPolar) {
    p.ucsPolar = true;
    p.ucsPolarBase = dyn.ucsPolarBase;
    return p;
  }
  const bool modelSpace = st.activeSpaceIndex < 0 || InFloatingModelSpace(st);
  p.showZ = modelSpace && !CadViewIsPlanToActiveUcs(st);
  if (dyn.haveBase) {
    p.haveBase = true;
    if (dyn.baseIsWorld) {
      p.baseWorld = dyn.basePoint;
    } else {
      double wx = 0.0, wy = 0.0;
      CadCoord::WorldFromLocal(st, static_cast<float>(dyn.basePoint.x),
                               static_cast<float>(dyn.basePoint.y), &wx, &wy);
      p.baseWorld = ray3d::Vec3{wx, wy, dyn.basePoint.z};
    }
  }
  if (dyn.initialRelativePolar)
    p.initialMode = dyninput::Mode::RelativePolar;
  p.directDistance = dyn.directDistance;
  return p;
}

std::string ActivePromptHint(const AppCommandState& st) {
  if (!st.activeSession)
    return {};
  return st.activeSession->currentStep(st).prompt;
}

void ReportUnparsedInput(const AppCommandState& st, const std::string& line,
                         const std::string& fallback, std::vector<std::string>& log,
                         std::size_t markBeforeHandler) {
  const std::string trimmed = StringUtil::trimCopy(line);
  std::string wanted;
  if (!trimmed.empty()) {
    const std::string low = StringUtil::toLowerAsciiCopy(trimmed);
    if (const CommandDef* entry = CommandRegistry::Instance().FindListed(low)) {
      wanted = entry->primary;
      for (char& c : wanted)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
  }
  const char* running = AppCommandState::KindName(st.active);
  if (!wanted.empty() && running && running[0] != '\0') {
    log.push_back(std::string(running) + " is still running, so \"" + trimmed +
                  "\" was read as input to it rather than as a command. Press Esc to end " + running +
                  ", then type " + wanted + ".");
    return;
  }
  if (log.size() > markBeforeHandler)
    return;
  if (!fallback.empty())
    log.push_back(fallback);
}

// ---------------------------------------------------------------------------
// Shared step-kind parsers
// ---------------------------------------------------------------------------

bool ParseDistanceToken(const std::string& raw, bool allowZero, bool allowNegative,
                        double* outDistance) {
  if (!outDistance)
    return false;
  const std::string s = StringUtil::trimCopy(raw);
  if (s.empty())
    return false;
  char* end = nullptr;
  const double v = std::strtod(s.c_str(), &end);
  if (!end || *end != '\0' || !std::isfinite(v))
    return false;
  if (!allowNegative && v < 0.0)
    return false;
  if (!allowZero && std::fabs(v) < 1e-12)
    return false;
  *outDistance = v;
  return true;
}

bool ParseAngleToken(const std::string& raw, double* outDegrees, double* outRadians) {
  const std::string s = StringUtil::trimCopy(raw);
  if (s.empty())
    return false;
  float deg = 0.f;
  if (!ParseAngleDegreesInternal(s, &deg) || !std::isfinite(deg))
    return false;
  if (outDegrees)
    *outDegrees = static_cast<double>(deg);
  if (outRadians)
    *outRadians = static_cast<double>(deg) * kDegToRad;
  return true;
}

const KeywordOption* MatchKeywordOption(const std::string& raw,
                                        const std::vector<KeywordOption>& options) {
  const std::string low = StringUtil::toLowerAsciiCopy(StringUtil::trimCopy(raw));
  if (low.empty())
    return nullptr;
  for (const KeywordOption& opt : options) {
    if (low == StringUtil::toLowerAsciiCopy(opt.canonical))
      return &opt;
    for (const std::string& tok : opt.tokens) {
      if (low == StringUtil::toLowerAsciiCopy(tok))
        return &opt;
    }
  }
  return nullptr;
}

// ---------------------------------------------------------------------------
// Active-session submit & pick dispatch
// ---------------------------------------------------------------------------

void SubmitCommandLine(AppCommandState& st, const std::string& rawLine,
                       std::vector<std::string>& log) {
  const std::string line = StringUtil::trimCopy(rawLine);
  if (!st.activeSession)
    return;
  // Hold a raw pointer only until after the call; the session may replace or end itself.
  CommandSession* session = st.activeSession.get();
  const std::size_t markBefore = log.size();
  const char* fallback = session->unparsedFallback();
  StepSubmitResult res = StepSubmitResult::Handled;
  if (line.empty()) {
    res = session->onBlankEnter(st, log);
  } else {
    res = session->onText(st, line, log);
  }
  if (res == StepSubmitResult::Finished) {
    // Only drop if the session didn't already replace itself with a different session.
    if (st.activeSession.get() == session)
      EndSession(st);
  } else if (res == StepSubmitResult::Unparsed) {
    if (st.activeSession.get() == session) {
      const std::string fb = (fallback && fallback[0] != '\0')
                                 ? std::string(fallback)
                                 : (std::string("Could not parse ") +
                                    AppCommandState::KindName(st.active) + " input — see command hints.");
      ReportUnparsedInput(st, line, fb, log, markBefore);
    }
  }
}

void SubmitViewportPick(AppCommandState& st, const StepPickInput& pick,
                        std::vector<std::string>& log) {
  if (!st.activeSession)
    return;
  CommandSession* session = st.activeSession.get();
  session->onViewportPick(st, pick, log);
}

// ---------------------------------------------------------------------------
// ScriptedStepSession — reusable implementation of all 7 ADR-0012 step kinds
// ---------------------------------------------------------------------------

ScriptedStepSession::ScriptedStepSession(
    CadCommandKind k, std::string cancelMessage, std::vector<ScriptedStepSpec> steps,
    std::function<void(AppCommandState&, std::vector<std::string>&)> onComplete,
    std::function<void(AppCommandState&, std::vector<std::string>&)> onCancelFn)
    : kind_(k),
      cancelMessage_(std::move(cancelMessage)),
      steps_(std::move(steps)),
      onComplete_(std::move(onComplete)),
      onCancelFn_(std::move(onCancelFn)) {}

StepDescriptor ScriptedStepSession::currentStep(const AppCommandState&) const {
  if (index_ >= steps_.size())
    return StepDescriptor::Custom("", StepClickRoute::Ignore, BlankEnterPolicy::Finish);
  const ScriptedStepSpec& s = steps_[index_];
  switch (s.kind) {
  case StepKind::SelectObjects:
    return s.boxOnlySelection
               ? StepDescriptor::SelectionBox(s.prompt, s.blankEnter, s.includeSurveyInSelectionBox)
               : StepDescriptor::SelectObjects(s.prompt, s.blankEnter, s.includeSurveyInSelectionBox);
  case StepKind::Point: {
    StepDynInputSpec dyn;
    dyn.pointEntry = true;
    dyn.haveBase = s.hasPointBase;
    dyn.basePoint = s.pointBaseLocal;
    return StepDescriptor::Point(s.prompt, dyn, s.blankEnter);
  }
  case StepKind::Distance:
    return StepDescriptor::Distance(
        s.prompt, s.blankEnter,
        s.allowPointPickForDistance ? StepClickRoute::SnappedPointPick : StepClickRoute::Ignore);
  case StepKind::Angle:
    return StepDescriptor::Angle(
        s.prompt, s.blankEnter,
        s.allowPointPickForAngle ? StepClickRoute::SnappedPointPick : StepClickRoute::Ignore);
  case StepKind::KeywordOptions:
    return StepDescriptor::KeywordOptions(s.prompt, s.blankEnter);
  case StepKind::Text:
    return StepDescriptor::Text(s.prompt, s.blankEnter);
  case StepKind::EntityPick:
    return StepDescriptor::EntityPick(s.prompt, s.blankEnter);
  case StepKind::Custom:
    return StepDescriptor::Custom(s.prompt, StepClickRoute::Ignore, s.blankEnter);
  }
  return StepDescriptor::Custom(s.prompt, StepClickRoute::Ignore, s.blankEnter);
}

StepSubmitResult ScriptedStepSession::AcceptAndAdvance(AppCommandState& st, StepValue value,
                                                       std::vector<std::string>& log) {
  if (index_ >= steps_.size())
    return StepSubmitResult::Finished;
  const ScriptedStepSpec& s = steps_[index_];
  if (s.onAccept && !s.onAccept(st, value, log))
    return StepSubmitResult::Handled;

  // Persist last values for BlankEnterPolicy::RepeatLast
  if (value.kind == StepKind::Distance && s.lastDistanceSlot)
    *s.lastDistanceSlot = value.scalar;
  else if (value.kind == StepKind::Angle && s.lastAngleDegSlot)
    *s.lastAngleDegSlot = value.scalar;
  else if (value.kind == StepKind::KeywordOptions && s.lastKeywordSlot)
    *s.lastKeywordSlot = value.text;
  else if (value.kind == StepKind::Text && s.lastTextSlot)
    *s.lastTextSlot = value.text;

  lastAccepted_ = value;
  accepted_.push_back(std::move(value));
  ++index_;
  if (index_ >= steps_.size()) {
    completed_ = true;
    if (onComplete_)
      onComplete_(st, log);
    return StepSubmitResult::Finished;
  }
  return StepSubmitResult::Handled;
}

StepSubmitResult ScriptedStepSession::onBlankEnter(AppCommandState& st,
                                                   std::vector<std::string>& log) {
  if (index_ >= steps_.size())
    return StepSubmitResult::Finished;
  const ScriptedStepSpec& s = steps_[index_];

  if (s.kind == StepKind::SelectObjects) {
    if (s.requireNonEmptySelection && st.selection.empty() && st.selectedSurveyPointIndices.empty()) {
      log.push_back("Nothing selected — click objects or drag a selection window, then press Enter.");
      return StepSubmitResult::Handled;
    }
    StepValue v;
    v.kind = StepKind::SelectObjects;
    v.fromBlankEnter = true;
    return AcceptAndAdvance(st, std::move(v), log);
  }

  switch (s.blankEnter) {
  case BlankEnterPolicy::Finish: {
    completed_ = true;
    if (onComplete_)
      onComplete_(st, log);
    return StepSubmitResult::Finished;
  }
  case BlankEnterPolicy::TakeDefault: {
    StepValue v;
    v.kind = s.kind;
    v.fromBlankEnter = true;
    switch (s.kind) {
    case StepKind::Point:
      if (!s.defaultPoint.has_value())
        return StepSubmitResult::Handled;
      v.point = *s.defaultPoint;
      return AcceptAndAdvance(st, std::move(v), log);
    case StepKind::Distance:
      if (!s.defaultDistance.has_value())
        return StepSubmitResult::Handled;
      v.scalar = *s.defaultDistance;
      return AcceptAndAdvance(st, std::move(v), log);
    case StepKind::Angle:
      if (!s.defaultAngleDeg.has_value())
        return StepSubmitResult::Handled;
      v.scalar = *s.defaultAngleDeg;
      v.angleRad = *s.defaultAngleDeg * kDegToRad;
      return AcceptAndAdvance(st, std::move(v), log);
    case StepKind::KeywordOptions:
      if (s.defaultKeyword.empty())
        return StepSubmitResult::Handled;
      v.text = s.defaultKeyword;
      return AcceptAndAdvance(st, std::move(v), log);
    case StepKind::Text:
      v.text = s.defaultText;
      return AcceptAndAdvance(st, std::move(v), log);
    default:
      return StepSubmitResult::Handled;
    }
  }
  case BlankEnterPolicy::RepeatLast: {
    StepValue v;
    v.kind = s.kind;
    v.fromBlankEnter = true;
    if (s.kind == StepKind::Distance && s.lastDistanceSlot && std::isfinite(*s.lastDistanceSlot)) {
      v.scalar = *s.lastDistanceSlot;
      return AcceptAndAdvance(st, std::move(v), log);
    }
    if (s.kind == StepKind::Angle && s.lastAngleDegSlot && std::isfinite(*s.lastAngleDegSlot)) {
      v.scalar = *s.lastAngleDegSlot;
      v.angleRad = *s.lastAngleDegSlot * kDegToRad;
      return AcceptAndAdvance(st, std::move(v), log);
    }
    if (s.kind == StepKind::KeywordOptions && s.lastKeywordSlot && !s.lastKeywordSlot->empty()) {
      v.text = *s.lastKeywordSlot;
      return AcceptAndAdvance(st, std::move(v), log);
    }
    if (s.kind == StepKind::Text && s.lastTextSlot && !s.lastTextSlot->empty()) {
      v.text = *s.lastTextSlot;
      return AcceptAndAdvance(st, std::move(v), log);
    }
    if (lastAccepted_.has_value() && lastAccepted_->kind == s.kind) {
      v = *lastAccepted_;
      v.fromBlankEnter = true;
      return AcceptAndAdvance(st, std::move(v), log);
    }
    log.push_back(s.verbUpper + " — no previous value to repeat.");
    return StepSubmitResult::Handled;
  }
  case BlankEnterPolicy::Custom:
    return StepSubmitResult::Handled;
  }
  return StepSubmitResult::Handled;
}

StepSubmitResult ScriptedStepSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                             std::vector<std::string>& log) {
  if (index_ >= steps_.size())
    return StepSubmitResult::Finished;
  const ScriptedStepSpec& s = steps_[index_];
  StepValue v;
  v.kind = s.kind;

  switch (s.kind) {
  case StepKind::SelectObjects:
    log.push_back(s.verbUpper + " — select objects in the viewport, then press Enter.");
    return StepSubmitResult::Handled;

  case StepKind::Point: {
    float lx = 0.f, ly = 0.f, lz = 0.f;
    bool consumed = false;
    if (!ResolveTypedModifyPoint(st, trimmedLine, s.allowRelativePoint && s.hasPointBase,
                                 static_cast<float>(s.pointBaseLocal.x),
                                 static_cast<float>(s.pointBaseLocal.y),
                                 static_cast<float>(s.pointBaseLocal.z), s.verbUpper.c_str(), &lx,
                                 &ly, &lz, &consumed, log)) {
      return consumed ? StepSubmitResult::Handled : StepSubmitResult::Unparsed;
    }
    v.point = ray3d::Vec3{static_cast<double>(lx), static_cast<double>(ly), static_cast<double>(lz)};
    return AcceptAndAdvance(st, std::move(v), log);
  }

  case StepKind::Distance: {
    double dist = 0.0;
    if (!ParseDistanceToken(trimmedLine, s.allowZeroDistance, s.allowNegativeDistance, &dist)) {
      log.push_back(s.verbUpper + " — enter a valid distance.");
      return StepSubmitResult::Unparsed;
    }
    v.scalar = dist;
    return AcceptAndAdvance(st, std::move(v), log);
  }

  case StepKind::Angle: {
    double deg = 0.0, rad = 0.0;
    if (!ParseAngleToken(trimmedLine, &deg, &rad)) {
      log.push_back(s.verbUpper + " — enter a valid angle (decimal degrees or DMS).");
      return StepSubmitResult::Unparsed;
    }
    v.scalar = deg;
    v.angleRad = rad;
    return AcceptAndAdvance(st, std::move(v), log);
  }

  case StepKind::KeywordOptions: {
    const KeywordOption* match = MatchKeywordOption(trimmedLine, s.keywords);
    if (!match) {
      log.push_back(s.verbUpper + " — unrecognized option \"" + trimmedLine + "\".");
      return StepSubmitResult::Unparsed;
    }
    v.text = match->canonical;
    return AcceptAndAdvance(st, std::move(v), log);
  }

  case StepKind::Text: {
    v.text = trimmedLine;
    return AcceptAndAdvance(st, std::move(v), log);
  }

  case StepKind::EntityPick: {
    if (!s.keywords.empty()) {
      if (const KeywordOption* match = MatchKeywordOption(trimmedLine, s.keywords)) {
        v.text = match->canonical;
        return AcceptAndAdvance(st, std::move(v), log);
      }
    }
    return StepSubmitResult::Unparsed;
  }

  case StepKind::Custom:
    return StepSubmitResult::Unparsed;
  }
  return StepSubmitResult::Unparsed;
}

void ScriptedStepSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                         std::vector<std::string>& log) {
  if (index_ >= steps_.size())
    return;
  const ScriptedStepSpec& s = steps_[index_];
  if (s.kind == StepKind::SelectObjects) {
    if (st.selBoxWaitingSecond) {
      FinishSelectionBox(st, pick.localX, pick.localY, pick.windowSelectionSubtract,
                         pick.fenceLeftToRightWindowMode, s.includeSurveyInSelectionBox, log);
      if (s.boxOnlySelection) {
        if (!s.requireNonEmptySelection || !st.selection.empty() ||
            !st.selectedSurveyPointIndices.empty()) {
          StepValue v;
          v.kind = StepKind::SelectObjects;
          if (AcceptAndAdvance(st, std::move(v), log) == StepSubmitResult::Finished)
            EndSession(st);
        }
      }
    }
    return;
  }

  if (s.kind == StepKind::Point || s.kind == StepKind::EntityPick) {
    StepValue v;
    v.kind = s.kind;
    v.point = ray3d::Vec3{pick.localX, pick.localY, static_cast<double>(CadCommitElevation(st))};
    if (AcceptAndAdvance(st, std::move(v), log) == StepSubmitResult::Finished)
      EndSession(st);
    return;
  }

  if (s.kind == StepKind::Distance && s.allowPointPickForDistance) {
    const double dx = pick.localX - s.pointBaseLocal.x;
    const double dy = pick.localY - s.pointBaseLocal.y;
    const double d = std::hypot(dx, dy);
    if (!s.allowZeroDistance && d < 1e-12) {
      log.push_back(s.verbUpper + " — distance must be positive.");
      return;
    }
    StepValue v;
    v.kind = StepKind::Distance;
    v.scalar = d;
    v.point = ray3d::Vec3{pick.localX, pick.localY, static_cast<double>(CadCommitElevation(st))};
    if (AcceptAndAdvance(st, std::move(v), log) == StepSubmitResult::Finished)
      EndSession(st);
    return;
  }

  if (s.kind == StepKind::Angle && s.allowPointPickForAngle) {
    const double dx = pick.localX - s.pointBaseLocal.x;
    const double dy = pick.localY - s.pointBaseLocal.y;
    const double rad = std::atan2(dy, dx);
    StepValue v;
    v.kind = StepKind::Angle;
    v.angleRad = rad;
    v.scalar = rad * kRadToDeg;
    v.point = ray3d::Vec3{pick.localX, pick.localY, static_cast<double>(CadCommitElevation(st))};
    if (AcceptAndAdvance(st, std::move(v), log) == StepSubmitResult::Finished)
      EndSession(st);
  }
}

void ScriptedStepSession::onCancel(AppCommandState& st, std::vector<std::string>& log) {
  cancelled_ = true;
  if (onCancelFn_)
    onCancelFn_(st, log);
  if (!cancelMessage_.empty())
    log.push_back(cancelMessage_);
}

} // namespace stepengine

std::string ActiveCommandPromptHint(const AppCommandState& st) {
  return stepengine::ActivePromptHint(st);
}

void CancelActiveCommand(AppCommandState& st, std::vector<std::string>& log) {
  if (HasPaperModifyPhase(st)) {
    if (st.paperLengthenPhase)
      log.push_back("LENGTHEN canceled.");
    else if (st.paperExtendPhase)
      log.push_back("EXTEND canceled.");
    else if (st.paperBreakPhase)
      log.push_back("BREAK canceled.");
    else if (st.paperStretchPhase)
      log.push_back("STRETCH canceled.");
    else if (st.paperFilletPhase)
      log.push_back("FILLET canceled.");
    else if (st.paperChamferPhase)
      log.push_back("CHAMFER canceled.");
    ResetPaperModifyDrafts(st);
    return;
  }
  if (st.active == AppCommandState::Kind::None)
    return;
  if (stepengine::CancelActiveSession(st, log))
    return;
  ClearPendingOneShotObjectSnap(st);
  st.active = AppCommandState::Kind::None;
  ResetSegmentAngleLock(st);
  ResetAllCadDraftTools(st);
  st.selBoxWaitingSecond = false;
  st.copySurveyDupModalOpen = false;
  st.copySurveyDupModalOpenRequested = false;
  ClearEntityGripInteraction(st);
  ClearDimGripInteraction(st);
}

void SubmitViewportPick(AppCommandState& st, double localX, double localY, std::vector<std::string>& log,
                        bool windowSelectionSubtract, bool fenceLeftToRightWindowMode,
                        const ray3d::Ray* pickRay) {
  ClearPendingOneShotObjectSnap(st);
  if (st.activeSession) {
    stepengine::StepPickInput pick;
    pick.localX = localX;
    pick.localY = localY;
    pick.windowSelectionSubtract = windowSelectionSubtract;
    pick.fenceLeftToRightWindowMode = fenceLeftToRightWindowMode;
    pick.pickRay = pickRay;
    stepengine::SubmitViewportPick(st, pick, log);
    return;
  }
  if (st.active == AppCommandState::Kind::None && st.activeSpaceIndex != kModelSpaceIndex &&
      !InFloatingModelSpace(st) &&
      HandlePaperModifyPick(st, static_cast<float>(localX), static_cast<float>(localY), log)) {
    return;
  }
  if (st.active == AppCommandState::Kind::None && st.selBoxWaitingSecond) {
    stepengine::FinishSelectionBox(st, localX, localY, windowSelectionSubtract,
                                   fenceLeftToRightWindowMode, /*includeSurvey=*/true, log);
  }
}

namespace {
void MaybeEstablishDocumentOriginFromTypedPoint(AppCommandState& st, const std::string& line,
                                                std::vector<std::string>& log) {
  if (st.drawing.worldDocumentOriginX != 0.0 || st.drawing.worldDocumentOriginY != 0.0)
    return;
  double wx = 0.;
  double wy = 0.;
  if (!ParseWorldPointD(line, &wx, &wy, /*allowRelative=*/false, 0., 0.))
    return;
  const double mag = std::max(std::fabs(wx), std::fabs(wy));
  if (mag < CadCoord::kLargeCoordinateRebaseThreshold)
    return;
  if (mag > CadCoord::kMaxEstablishableOriginMagnitude)
    return;
  CadCoord::ApplyDocumentOriginRebase(st, wx, wy, &log);
}
} // namespace

void ProcessCommandLineSubmit(char* cmdBuf, int cmdBufSize, AppCommandState& st, std::vector<std::string>& log) {
  [&]() {
    (void)cmdBufSize;
    std::string line = StringUtil::trimCopy(std::string(cmdBuf));

    MaybeEstablishDocumentOriginFromTypedPoint(st, line, log);

    if (!line.empty() && (st.cmdEnteredHistory.empty() || st.cmdEnteredHistory.back() != line)) {
      st.cmdEnteredHistory.push_back(line);
      if (st.cmdEnteredHistory.size() > 20)
        st.cmdEnteredHistory.erase(st.cmdEnteredHistory.begin());
    }

    // GIZMO MOVE | ROTATE | SCALE | OFF is a one-line setting accepted ahead of active dispatch.
    {
      std::istringstream gz(StringUtil::toLowerAsciiCopy(line));
      std::string gzCmd;
      if ((gz >> gzCmd) && gzCmd == "gizmo") {
        std::string rest;
        std::getline(gz, rest);
        stepengine::ExecuteGizmoSettingCommand(st, StringUtil::trimCopy(rest), log);
        return;
      }
    }

    // Grip stretch, direct-distance entry (REQ-047).
    if (st.entityGripMoveActive) {
      float gripDist = 0.f;
      if (ParseSingleFloatToken(line, &gripDist)) {
        if (std::fabs(gripDist) < 1e-20f) {
          log.push_back("Grip — distance must be non-zero.");
          return;
        }
        float ux = 0.f;
        float uy = 0.f;
        if (st.entityGripLengthenMode) {
          ux = st.entityGripLengthenDirX;
          uy = st.entityGripLengthenDirY;
        } else {
          float cursorLocalX = 0.f;
          float cursorLocalY = 0.f;
          CadCoord::LocalFromWorld(st, static_cast<double>(st.uiCursorWorldX),
                                   static_cast<double>(st.uiCursorWorldY), &cursorLocalX, &cursorLocalY);
          if (!OrthoUnitTowardPoint(st.entityGripAnchorX, st.entityGripAnchorY, cursorLocalX,
                                    cursorLocalY, &ux, &uy)) {
            log.push_back(
                "Grip distance needs a direction — move the crosshair off the grip, then enter a distance.");
            return;
          }
        }
        st.entityGripTypedX = st.entityGripAnchorX + ux * gripDist;
        st.entityGripTypedY = st.entityGripAnchorY + uy * gripDist;
        st.entityGripTypedDistanceValid = true;
        ApplyEntityGripPoint(st, st.entityGripTypedX, st.entityGripTypedY, CadCommitElevation(st));
        char gripMsg[128];
        if (st.entityGripLengthenMode)
          std::snprintf(gripMsg, sizeof(gripMsg), "Grip lengthened %.6g along the line's angle.",
                        static_cast<double>(gripDist));
        else
          std::snprintf(gripMsg, sizeof(gripMsg), "Grip stretched %.6g %s.",
                        static_cast<double>(std::fabs(gripDist)),
                        ux > 0.f ? "right" : (ux < 0.f ? "left" : (uy > 0.f ? "up" : "down")));
        log.push_back(gripMsg);
        ClearEntityGripInteraction(st);
        BumpCadGpuCache(st);
        return;
      }
    }

    if (st.activeSession) {
      stepengine::SubmitCommandLine(st, line, log);
      return;
    }

    // #764: typed points for the click-driven paper MOVE/COPY/ROTATE/MIRROR/SCALE gestures.
    if (HandlePaperPhaseText(st, line, log))
      return;

    if (line.empty()) {
      if (st.paperMoveWaitingSelection) {
        ProcessPaperMoveWaitingSelectionEnter(st, log);
        return;
      }
      if (st.paperDeleteWaitingSelection) {
        ProcessPaperDeleteWaitingSelectionEnter(st, log);
        return;
      }
      return;
    }

    if (st.active == AppCommandState::Kind::None) {
      std::istringstream issIdle(StringUtil::trimCopy(std::string(cmdBuf)));
      std::string plotTok;
      issIdle >> plotTok;
      plotTok = StringUtil::toLowerAsciiCopy(plotTok);
      if (const stepengine::CommandDef* regDef = stepengine::CommandRegistry::Instance().FindAny(plotTok);
          regDef && regDef->execute) {
        std::string restOfLine;
        std::getline(issIdle, restOfLine);
        regDef->execute(st, StringUtil::trimCopy(restOfLine), log);
        return;
      }
      const std::vector<std::string> sug = FuzzyCommandMatches(line, 6);
      if (!sug.empty()) {
        std::string msg = "Unknown command. Did you mean: ";
        for (size_t i = 0; i < sug.size(); ++i) {
          if (i)
            msg += ", ";
          msg += sug[i];
        }
        msg += "?";
        log.push_back(msg);
      } else {
        log.push_back("Unknown command. Type HELP.");
      }
      return;
    }
  }();
  cmdBuf[0] = '\0';
}

void RepeatLastCommand(AppCommandState& st, std::vector<std::string>& log) {
  if (const stepengine::CommandDef* def =
          stepengine::CommandRegistry::Instance().FindRepeatableByKind(st.lastCommand);
      def && def->execute) {
    def->execute(st, {}, log);
  }
}
