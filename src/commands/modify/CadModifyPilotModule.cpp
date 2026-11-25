// ADR-0012 / Issue #1578 (Track B1 Pilot): Step-engine command modules for
// MOVE, COPY, ROTATE, SCALE, MIRROR, STRETCH, and PASTE.

#include "modify/CadModifySessions.hpp"
#include "engine/CommandRegistry.hpp"
#include "engine/StepEngine.hpp"
#include "CadCommandsInternal.hpp"
#include "modify/CadModifyInternal.hpp"
#include "CadCoordinateFrame.hpp"

#include <GoSurvey/Core/StringUtil.hpp>
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace stepengine {

namespace {

void FinishScaleTransform(AppCommandState& st, float bx, float by, float bz, float scaleFactor,
                          std::vector<std::string>& log) {
  PushUndoSnapshot(st, "Scale");
  const float s = std::max(scaleFactor, 1e-6f);
  ApplyUniformScaleAboutBase(st, bx, by, bz, s, log);
  EndSession(st);
  log.push_back("SCALE complete.");
}

void FinishRotateTransform(AppCommandState& st, float bx, float by, float bz, float rad, bool copyMode,
                           std::vector<std::string>& log) {
  PushUndoSnapshot(st, copyMode ? "Rotate-copy" : "Rotate");
  const bool tilted = !CadWorkPlaneIsWorldXy(st);
  if (copyMode) {
    if (tilted) {
      DropSurfacesFromSelectionForTransform(st, "ROTATE", log);
      DropPositionMarkersFromSelection(st, "ROTATE", log);
      DropMultileadersFromSelection(st, "ROTATE", log);
      const ucs::Ucs u = CadActiveUcsStorage(st);
      const ray3d::Vec3 axisUnit =
          ray3d::Normalize(ray3d::Vec3{u.zAxis.x, u.zAxis.y, u.zAxis.z});
      RotateSelectionAboutAxis(st, {static_cast<double>(bx), static_cast<double>(by), static_cast<double>(bz)},
                               axisUnit, rad, log, "ROTATE");
      if (!st.selectedSurveyPointIndices.empty())
        log.push_back("ROTATE COPY — " + std::to_string(st.selectedSurveyPointIndices.size()) +
                      " survey point(s) not duplicated: rotation about a tilted axis is not"
                      " supported yet (REQ-328).");
    } else {
      DuplicateCadSelectionRotated(st, bx, by, rad, log);
    }
    EndSession(st);
    if (!tilted && !st.selectedSurveyPointIndices.empty()) {
      st.pendingSurveyDupIsRotate = true;
      st.pendingRotateCopyBx = bx;
      st.pendingRotateCopyBy = by;
      st.pendingRotateCopyRad = rad;
      st.copySurveyDupModalOpen = true;
      st.copySurveyDupModalOpenRequested = true;
      log.push_back("ROTATE COPY — CAD duplicated; choose survey ID policy.");
    } else {
      log.push_back("ROTATE COPY complete.");
    }
  } else {
    ApplyRotationAboutUcsZ(st, bx, by, bz, rad, log);
    EndSession(st);
    log.push_back("ROTATE complete.");
  }
}

bool TryToggleRotateCopy(RotateSession& s, const std::string& lineIn, std::vector<std::string>& log) {
  const std::string low = StringUtil::toLowerAsciiCopy(StringUtil::trimCopy(lineIn));
  if (low != "c" && low != "copy")
    return false;
  s.copyMode = !s.copyMode;
  log.push_back(s.copyMode ? "ROTATE — copy mode on (original kept)." : "ROTATE — copy mode off.");
  return true;
}

} // namespace

// ---------------------------------------------------------------------------
// MoveCopySession
// ---------------------------------------------------------------------------

StepDescriptor MoveCopySession::currentStep(const AppCommandState&) const {
  switch (phase) {
  case ModifyPhase::PickSelection:
    return StepDescriptor::SelectObjects(kSelectObjectsPrompt, BlankEnterPolicy::Custom, true);
  case ModifyPhase::NeedBase:
    return StepDescriptor::Point("MOVE/COPY: Base point — click or X,Y | ESC cancel");
  case ModifyPhase::NeedDestination: {
    StepDynInputSpec dyn;
    dyn.pointEntry = true;
    dyn.haveBase = true;
    dyn.basePoint = ray3d::Vec3{baseX, baseY, baseZ};
    return StepDescriptor::Point("MOVE/COPY: Second point — click or X,Y or @dx,dy from base | ESC cancel", dyn);
  }
  }
  return StepDescriptor::Custom("", StepClickRoute::Ignore);
}

StepSubmitResult MoveCopySession::onBlankEnter(AppCommandState& st, std::vector<std::string>& log) {
  if (phase == ModifyPhase::PickSelection) {
    if (st.selection.empty() && st.selectedSurveyPointIndices.empty()) {
      log.push_back("Nothing selected — click objects or drag a selection window, then press Enter.");
    } else {
      phase = ModifyPhase::NeedBase;
      log.push_back(isCopy ? "COPY — base point:" : "MOVE — base point:");
    }
  }
  return StepSubmitResult::Handled;
}

StepSubmitResult MoveCopySession::onText(AppCommandState& st, const std::string& trimmedLine,
                                         std::vector<std::string>& log) {
  const char* verb = isCopy ? "COPY" : "MOVE";
  if (phase == ModifyPhase::NeedBase) {
    float bx = 0.f, by = 0.f, bwz = 0.f;
    bool consumed = false;
    if (!ResolveTypedModifyPoint(st, trimmedLine, false, 0.f, 0.f, 0.f, verb, &bx, &by, &bwz, &consumed, log))
      return consumed ? StepSubmitResult::Handled : StepSubmitResult::Unparsed;
    baseX = bx;
    baseY = by;
    baseZ = bwz;
    phase = ModifyPhase::NeedDestination;
    log.push_back(isCopy ? "COPY — specify second point (destination)."
                         : "MOVE — specify second point (destination).");
    return StepSubmitResult::Handled;
  }
  if (phase == ModifyPhase::NeedDestination) {
    float px = 0.f, py = 0.f, dwz = 0.f;
    bool consumed = false;
    if (!ResolveTypedModifyPoint(st, trimmedLine, true, baseX, baseY, baseZ, verb, &px, &py, &dwz, &consumed, log))
      return consumed ? StepSubmitResult::Handled : StepSubmitResult::Unparsed;
    const float dx = px - baseX;
    const float dy = py - baseY;
    const float dz = dwz - baseZ;
    PushUndoSnapshot(st, isCopy ? "Copy" : "Move");
    if (isCopy) {
      FinalizeCopyTranslation(st, dx, dy, dz, log);
      phase = ModifyPhase::NeedDestination;
    } else {
      ApplyTranslationToSelection(st, dx, dy, dz, log);
      phase = ModifyPhase::NeedBase;
      log.push_back("MOVE complete — base point (ESC to exit):");
    }
    return StepSubmitResult::Handled;
  }
  return StepSubmitResult::Unparsed;
}

void MoveCopySession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                     std::vector<std::string>& log) {
  if (phase == ModifyPhase::PickSelection) {
    if (st.selBoxWaitingSecond)
      FinishSelectionBox(st, pick.localX, pick.localY, pick.windowSelectionSubtract,
                         pick.fenceLeftToRightWindowMode, true, log);
    return;
  }
  if (phase == ModifyPhase::NeedBase) {
    baseX = static_cast<float>(pick.localX);
    baseY = static_cast<float>(pick.localY);
    baseZ = CadCommitElevation(st);
    phase = ModifyPhase::NeedDestination;
    log.push_back(isCopy ? "COPY — destination:" : "MOVE — destination:");
    return;
  }
  if (phase == ModifyPhase::NeedDestination) {
    const float dx = static_cast<float>(pick.localX - baseX);
    const float dy = static_cast<float>(pick.localY - baseY);
    const float dz = CadCommitElevation(st) - baseZ;
    PushUndoSnapshot(st, isCopy ? "Copy" : "Move");
    if (isCopy) {
      FinalizeCopyTranslation(st, dx, dy, dz, log);
      phase = ModifyPhase::NeedDestination;
    } else {
      ApplyTranslationToSelection(st, dx, dy, dz, log);
      phase = ModifyPhase::NeedBase;
      log.push_back("MOVE complete — base point (ESC to exit):");
    }
  }
}

void MoveCopySession::onCancel(AppCommandState&, std::vector<std::string>& log) {
  log.push_back(isCopy ? "COPY canceled." : "MOVE canceled.");
}

// ---------------------------------------------------------------------------
// StretchSession
// ---------------------------------------------------------------------------

StepDescriptor StretchSession::currentStep(const AppCommandState&) const {
  switch (phase) {
  case ModifyPhase::PickSelection:
    return StepDescriptor::SelectionBox(
        "STRETCH: Crossing/window select objects | ESC cancel", BlankEnterPolicy::Custom, true);
  case ModifyPhase::NeedBase:
    return StepDescriptor::Point("STRETCH: Base point — click or X,Y | ESC cancel");
  case ModifyPhase::NeedDestination: {
    StepDynInputSpec dyn;
    dyn.pointEntry = true;
    dyn.haveBase = true;
    dyn.basePoint = ray3d::Vec3{baseX, baseY, baseZ};
    return StepDescriptor::Point("STRETCH: Second point — click or X,Y or @dx,dy from base | ESC cancel", dyn);
  }
  }
  return StepDescriptor::Custom("", StepClickRoute::Ignore);
}

StepSubmitResult StretchSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                        std::vector<std::string>& log) {
  if (phase == ModifyPhase::NeedBase) {
    float bx = 0.f, by = 0.f, bwz = 0.f;
    bool consumed = false;
    if (!ResolveTypedModifyPoint(st, trimmedLine, false, 0.f, 0.f, 0.f, "STRETCH", &bx, &by, &bwz, &consumed, log))
      return consumed ? StepSubmitResult::Handled : StepSubmitResult::Unparsed;
    baseX = bx;
    baseY = by;
    baseZ = bwz;
    phase = ModifyPhase::NeedDestination;
    log.push_back("STRETCH — specify second point (destination).");
    return StepSubmitResult::Handled;
  }
  if (phase == ModifyPhase::NeedDestination) {
    float px = 0.f, py = 0.f, dwz = 0.f;
    bool consumed = false;
    if (!ResolveTypedModifyPoint(st, trimmedLine, true, baseX, baseY, baseZ, "STRETCH", &px, &py, &dwz, &consumed,
                                 log))
      return consumed ? StepSubmitResult::Handled : StepSubmitResult::Unparsed;
    const float dx = px - baseX;
    const float dy = py - baseY;
    const float dz = dwz - baseZ;
    PushUndoSnapshot(st, "Stretch");
    ApplyStretchToSelection(st, dx, dy, dz, rectMnX, rectMxX, rectMnY, rectMxY, rectInUcsPlane, log);
    phase = ModifyPhase::NeedBase;
    log.push_back("STRETCH complete — base point (ESC to exit):");
    return StepSubmitResult::Handled;
  }
  return StepSubmitResult::Unparsed;
}

void StretchSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                    std::vector<std::string>& log) {
  if (phase == ModifyPhase::PickSelection) {
    if (st.selBoxWaitingSecond) {
      const float ax = st.selBoxAnchorX;
      const float ay = st.selBoxAnchorY;
      const float az = st.selBoxAnchorZ;
      FinishSelectionBox(st, pick.localX, pick.localY, pick.windowSelectionSubtract,
                         pick.fenceLeftToRightWindowMode, true, log);
      if (CadWorkPlaneIsWorldXy(st)) {
        rectMnX = static_cast<float>(std::min<double>(ax, pick.localX));
        rectMxX = static_cast<float>(std::max<double>(ax, pick.localX));
        rectMnY = static_cast<float>(std::min<double>(ay, pick.localY));
        rectMxY = static_cast<float>(std::max<double>(ay, pick.localY));
        rectInUcsPlane = false;
      } else {
        const ucs::Ucs frame = CadActiveUcsStorage(st);
        const ucs::Point2D c1 = ucs::WorldToPlane(
            frame, {static_cast<double>(ax), static_cast<double>(ay), static_cast<double>(az)});
        const ucs::Point2D c2 = ucs::WorldToPlane(
            frame, {pick.localX, pick.localY, static_cast<double>(st.uiCursorWorldZ)});
        rectMnX = static_cast<float>(std::min(c1.x, c2.x));
        rectMxX = static_cast<float>(std::max(c1.x, c2.x));
        rectMnY = static_cast<float>(std::min(c1.y, c2.y));
        rectMxY = static_cast<float>(std::max(c1.y, c2.y));
        rectInUcsPlane = true;
      }
      if (st.selection.empty() && st.selectedSurveyPointIndices.empty()) {
        log.push_back("Nothing selected — pick two corners again.");
      } else {
        phase = ModifyPhase::NeedBase;
        log.push_back("STRETCH — base point:");
      }
    }
    return;
  }
  if (phase == ModifyPhase::NeedBase) {
    baseX = static_cast<float>(pick.localX);
    baseY = static_cast<float>(pick.localY);
    baseZ = CadCommitElevation(st);
    phase = ModifyPhase::NeedDestination;
    log.push_back("STRETCH — destination:");
    return;
  }
  if (phase == ModifyPhase::NeedDestination) {
    const float dx = static_cast<float>(pick.localX - baseX);
    const float dy = static_cast<float>(pick.localY - baseY);
    const float dz = CadCommitElevation(st) - baseZ;
    PushUndoSnapshot(st, "Stretch");
    ApplyStretchToSelection(st, dx, dy, dz, rectMnX, rectMxX, rectMnY, rectMxY, rectInUcsPlane, log);
    phase = ModifyPhase::NeedBase;
    log.push_back("STRETCH complete — base point (ESC to exit):");
  }
}

void StretchSession::onCancel(AppCommandState&, std::vector<std::string>& log) {
  log.push_back("STRETCH canceled.");
}

// ---------------------------------------------------------------------------
// PasteSession
// ---------------------------------------------------------------------------

StepDescriptor PasteSession::currentStep(const AppCommandState&) const {
  return StepDescriptor::Custom("PASTE: Click destination point | ESC cancel",
                                StepClickRoute::SnappedPointPick, BlankEnterPolicy::Custom);
}

StepSubmitResult PasteSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                      std::vector<std::string>& log) {
  float px = 0.f, py = 0.f;
  if (ParseStoragePoint(st, trimmedLine, &px, &py, false, 0.f, 0.f)) {
    CommitClipboardPasteAt(st, px, py, log);
    return StepSubmitResult::Handled;
  }
  return StepSubmitResult::Unparsed;
}

void PasteSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                  std::vector<std::string>& log) {
  CommitClipboardPasteAt(st, pick.localX, pick.localY, log);
}

void PasteSession::onCancel(AppCommandState&, std::vector<std::string>& log) {
  log.push_back("PASTE canceled.");
}

// ---------------------------------------------------------------------------
// ScaleSession
// ---------------------------------------------------------------------------

StepDescriptor ScaleSession::currentStep(const AppCommandState&) const {
  if (phase == ModifyPhase::PickSelection)
    return StepDescriptor::SelectObjects(kSelectObjectsPrompt, BlankEnterPolicy::Custom, true);
  if (phase == ModifyPhase::NeedBase)
    return StepDescriptor::Point("SCALE: Base point — click or X,Y | ESC cancel");
  switch (scalePhase) {
  case ScalePhase::FactorPick:
    return StepDescriptor::Distance(
        "SCALE: Second point or type factor (>0) — dist/base-ref | [R]eference = two-point ref length | ESC",
        BlankEnterPolicy::Custom, StepClickRoute::SnappedPointPick);
  case ScalePhase::Ref_WaitP1:
    return StepDescriptor::Point("SCALE ref: First point of reference length | ESC cancel");
  case ScalePhase::Ref_WaitP2:
    return StepDescriptor::Point("SCALE ref: Second point (reference length) | ESC cancel");
  case ScalePhase::NewLength_WaitTypedOrP1:
    return StepDescriptor::Distance(
        "SCALE ref: Type new length (model units) or pick first point of new length | ESC",
        BlankEnterPolicy::Custom, StepClickRoute::SnappedPointPick);
  case ScalePhase::NewLength_WaitP2:
    return StepDescriptor::Point("SCALE ref: Second point of new length (preview) | ESC cancel");
  }
  return StepDescriptor::Custom("", StepClickRoute::Ignore);
}

StepSubmitResult ScaleSession::onBlankEnter(AppCommandState& st, std::vector<std::string>& log) {
  if (phase == ModifyPhase::PickSelection) {
    if (st.selection.empty() && st.selectedSurveyPointIndices.empty()) {
      log.push_back("Nothing selected — click objects or drag a selection window, then press Enter.");
    } else {
      phase = ModifyPhase::NeedBase;
      log.push_back("SCALE — base point:");
    }
  }
  return StepSubmitResult::Handled;
}

StepSubmitResult ScaleSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                      std::vector<std::string>& log) {
  if (phase == ModifyPhase::NeedBase) {
    float px = 0.f, py = 0.f;
    if (!ParseStoragePoint(st, trimmedLine, &px, &py, false, 0.f, 0.f))
      return StepSubmitResult::Unparsed;
    baseX = px;
    baseY = py;
    baseZ = CadCommitElevation(st);
    refDist = ComputeScaleReferenceDistance(st, px, py);
    scalePhase = ScalePhase::FactorPick;
    phase = ModifyPhase::NeedDestination;
    log.push_back(
        "SCALE — pick second point or type factor (>0), or R / REFERENCE for two-point reference length then new "
        "length.");
    return StepSubmitResult::Handled;
  }
  if (phase != ModifyPhase::NeedDestination)
    return StepSubmitResult::Unparsed;

  switch (scalePhase) {
  case ScalePhase::FactorPick: {
    const std::string low = StringUtil::toLowerAsciiCopy(trimmedLine);
    if (low == "r" || low == "ref" || low == "reference") {
      scalePhase = ScalePhase::Ref_WaitP1;
      log.push_back("SCALE ref — first point of reference length:");
      return StepSubmitResult::Handled;
    }
    float sf = 0.f;
    if (ParseOneFloat(trimmedLine, &sf)) {
      if (!(sf > 0.f) || !std::isfinite(sf)) {
        log.push_back("SCALE — scale factor must be a positive finite number.");
        return StepSubmitResult::Unparsed;
      }
      FinishScaleTransform(st, baseX, baseY, baseZ, sf, log);
      return StepSubmitResult::Handled;
    }
    float px = 0.f, py = 0.f;
    if (!ParseStoragePoint(st, trimmedLine, &px, &py, true, baseX, baseY))
      return StepSubmitResult::Unparsed;
    const float d = std::hypot(px - baseX, py - baseY);
    FinishScaleTransform(st, baseX, baseY, baseZ, d / std::max(refDist, 1e-20f), log);
    return StepSubmitResult::Handled;
  }
  case ScalePhase::Ref_WaitP1: {
    float px = 0.f, py = 0.f;
    if (!ParseStoragePoint(st, trimmedLine, &px, &py, false, 0.f, 0.f))
      return StepSubmitResult::Unparsed;
    refP1X = px;
    refP1Y = py;
    scalePhase = ScalePhase::Ref_WaitP2;
    log.push_back("SCALE ref — second point of reference length:");
    return StepSubmitResult::Handled;
  }
  case ScalePhase::Ref_WaitP2: {
    float px = 0.f, py = 0.f;
    if (!ParseStoragePoint(st, trimmedLine, &px, &py, false, 0.f, 0.f))
      return StepSubmitResult::Unparsed;
    const float refLen = std::hypot(px - refP1X, py - refP1Y);
    if (!(refLen > 1e-8f) || !std::isfinite(refLen)) {
      log.push_back("SCALE ref — reference length is too small; pick two distinct points.");
      return StepSubmitResult::Unparsed;
    }
    refDist = refLen;
    scalePhase = ScalePhase::NewLength_WaitTypedOrP1;
    log.push_back("SCALE ref — type new length (model units) or pick first point of new length segment.");
    return StepSubmitResult::Handled;
  }
  case ScalePhase::NewLength_WaitTypedOrP1: {
    float L = 0.f;
    if (ParseOneFloat(trimmedLine, &L)) {
      if (!(L > 0.f) || !std::isfinite(L)) {
        log.push_back("SCALE ref — new length must be a positive finite number.");
        return StepSubmitResult::Unparsed;
      }
      FinishScaleTransform(st, baseX, baseY, baseZ, L / std::max(refDist, 1e-20f), log);
      return StepSubmitResult::Handled;
    }
    float px = 0.f, py = 0.f;
    if (!ParseStoragePoint(st, trimmedLine, &px, &py, false, 0.f, 0.f))
      return StepSubmitResult::Unparsed;
    newLenP1X = px;
    newLenP1Y = py;
    scalePhase = ScalePhase::NewLength_WaitP2;
    log.push_back("SCALE ref — second point of new length segment:");
    return StepSubmitResult::Handled;
  }
  case ScalePhase::NewLength_WaitP2: {
    float px = 0.f, py = 0.f;
    if (!ParseStoragePoint(st, trimmedLine, &px, &py, false, 0.f, 0.f))
      return StepSubmitResult::Unparsed;
    const float newLen = std::hypot(px - newLenP1X, py - newLenP1Y);
    if (!(newLen > 1e-8f) || !std::isfinite(newLen)) {
      log.push_back("SCALE ref — new length is too small; pick two distinct points.");
      return StepSubmitResult::Unparsed;
    }
    FinishScaleTransform(st, baseX, baseY, baseZ, newLen / std::max(refDist, 1e-20f), log);
    return StepSubmitResult::Handled;
  }
  }
  return StepSubmitResult::Unparsed;
}

void ScaleSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                  std::vector<std::string>& log) {
  if (phase == ModifyPhase::PickSelection) {
    if (st.selBoxWaitingSecond)
      FinishSelectionBox(st, pick.localX, pick.localY, pick.windowSelectionSubtract,
                         pick.fenceLeftToRightWindowMode, true, log);
    return;
  }
  if (phase == ModifyPhase::NeedBase) {
    baseX = static_cast<float>(pick.localX);
    baseY = static_cast<float>(pick.localY);
    baseZ = CadCommitElevation(st);
    refDist = ComputeScaleReferenceDistance(st, baseX, baseY);
    scalePhase = ScalePhase::FactorPick;
    phase = ModifyPhase::NeedDestination;
    log.push_back(
        "SCALE — pick second point or type factor (>0), or R / REFERENCE on command line for two-point reference "
        "length.");
    return;
  }
  if (phase == ModifyPhase::NeedDestination) {
    switch (scalePhase) {
    case ScalePhase::FactorPick: {
      const float d = static_cast<float>(std::hypot(pick.localX - baseX, pick.localY - baseY));
      const float s = std::max(d / std::max(refDist, 1e-20f), 1e-6f);
      FinishScaleTransform(st, baseX, baseY, baseZ, s, log);
      return;
    }
    case ScalePhase::Ref_WaitP1:
      refP1X = static_cast<float>(pick.localX);
      refP1Y = static_cast<float>(pick.localY);
      scalePhase = ScalePhase::Ref_WaitP2;
      log.push_back("SCALE ref — second point of reference length:");
      return;
    case ScalePhase::Ref_WaitP2: {
      const float refLen = static_cast<float>(std::hypot(pick.localX - refP1X, pick.localY - refP1Y));
      if (!(refLen > 1e-8f) || !std::isfinite(refLen)) {
        log.push_back("SCALE ref — reference length is too small; pick two distinct points.");
        return;
      }
      refDist = refLen;
      scalePhase = ScalePhase::NewLength_WaitTypedOrP1;
      log.push_back("SCALE ref — type new length (model units) or pick first point of new length segment.");
      return;
    }
    case ScalePhase::NewLength_WaitTypedOrP1:
      newLenP1X = static_cast<float>(pick.localX);
      newLenP1Y = static_cast<float>(pick.localY);
      scalePhase = ScalePhase::NewLength_WaitP2;
      log.push_back("SCALE ref — second point of new length segment:");
      return;
    case ScalePhase::NewLength_WaitP2: {
      const float newLen = static_cast<float>(std::hypot(pick.localX - newLenP1X, pick.localY - newLenP1Y));
      if (!(newLen > 1e-8f) || !std::isfinite(newLen)) {
        log.push_back("SCALE ref — new length is too small; pick two distinct points.");
        return;
      }
      FinishScaleTransform(st, baseX, baseY, baseZ, newLen / std::max(refDist, 1e-20f), log);
      return;
    }
    }
  }
}

void ScaleSession::onCancel(AppCommandState&, std::vector<std::string>& log) {
  log.push_back("SCALE canceled.");
}

// ---------------------------------------------------------------------------
// RotateSession
// ---------------------------------------------------------------------------

StepDescriptor RotateSession::currentStep(const AppCommandState&) const {
  switch (phase) {
  case RotatePhase::PickSelection:
    return StepDescriptor::SelectObjects(kSelectObjectsPrompt, BlankEnterPolicy::Custom, true);
  case RotatePhase::NeedBase:
    return StepDescriptor::Point("ROTATE: Base point — click or X,Y | ESC cancel");
  case RotatePhase::NeedAngleOrReference:
    return StepDescriptor::Angle(
        "ROTATE: ° clockwise / DMS | [R]eference | [C]opy | ESC (north=0° CW)",
        BlankEnterPolicy::Custom, StepClickRoute::SnappedPointPick);
  case RotatePhase::Ref_WaitP1:
    return StepDescriptor::Point("ROTATE ref: First point | [C] toggles copy | ESC cancel");
  case RotatePhase::Ref_WaitP2:
    return StepDescriptor::Point("ROTATE ref: Second point | [C] toggles copy | ESC cancel");
  case RotatePhase::AfterReference_WaitAngleOrP:
    return StepDescriptor::Angle(
        "ROTATE ref: New bearing ° from north (like props) | [P] two pts | [C]opy | ESC",
        BlankEnterPolicy::Custom, StepClickRoute::SnappedPointPick);
  case RotatePhase::AnglePoints_WaitP1:
    return StepDescriptor::Point("ROTATE angle pts: First point | [C]opy | ESC cancel");
  case RotatePhase::AnglePoints_WaitP2:
    return StepDescriptor::Point("ROTATE angle pts: Second point | [C]opy | ESC cancel");
  }
  return StepDescriptor::Custom("", StepClickRoute::Ignore);
}

StepSubmitResult RotateSession::onBlankEnter(AppCommandState& st, std::vector<std::string>& log) {
  if (phase == RotatePhase::PickSelection) {
    if (st.selection.empty() && st.selectedSurveyPointIndices.empty()) {
      log.push_back("Nothing selected — click objects or drag a selection window, then press Enter.");
    } else {
      phase = RotatePhase::NeedBase;
      log.push_back("ROTATE — base point:");
    }
  }
  return StepSubmitResult::Handled;
}

StepSubmitResult RotateSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                       std::vector<std::string>& log) {
  constexpr float kDegToRad = 0.01745329251994329577f;
  if (phase == RotatePhase::NeedBase) {
    float px = 0.f, py = 0.f;
    if (!ParseStoragePoint(st, trimmedLine, &px, &py, false, 0.f, 0.f))
      return StepSubmitResult::Unparsed;
    baseX = px;
    baseY = py;
    baseZ = CadCommitElevation(st);
    phase = RotatePhase::NeedAngleOrReference;
    log.push_back("ROTATE — ° clockwise from north (decimal/DMS), R reference, C copy — click-drag preview.");
    return StepSubmitResult::Handled;
  }
  if (phase == RotatePhase::NeedAngleOrReference) {
    if (TryToggleRotateCopy(*this, trimmedLine, log))
      return StepSubmitResult::Handled;
    const std::string low = StringUtil::toLowerAsciiCopy(trimmedLine);
    if (low == "r" || low == "ref" || low == "reference") {
      phase = RotatePhase::Ref_WaitP1;
      log.push_back("Reference — first point:");
      return StepSubmitResult::Handled;
    }
    float deg = 0.f;
    if (!ParseAngleDegreesInternal(trimmedLine, &deg))
      return StepSubmitResult::Unparsed;
    FinishRotateTransform(st, baseX, baseY, baseZ, -deg * kDegToRad, copyMode, log);
    return StepSubmitResult::Handled;
  }
  if (phase == RotatePhase::Ref_WaitP1) {
    if (TryToggleRotateCopy(*this, trimmedLine, log))
      return StepSubmitResult::Handled;
    float px = 0.f, py = 0.f;
    if (!ParseStoragePoint(st, trimmedLine, &px, &py, false, 0.f, 0.f))
      return StepSubmitResult::Unparsed;
    refX1 = px;
    refY1 = py;
    phase = RotatePhase::Ref_WaitP2;
    log.push_back("Reference — second point:");
    return StepSubmitResult::Handled;
  }
  if (phase == RotatePhase::Ref_WaitP2) {
    if (TryToggleRotateCopy(*this, trimmedLine, log))
      return StepSubmitResult::Handled;
    float px = 0.f, py = 0.f;
    if (!ParseStoragePoint(st, trimmedLine, &px, &py, false, 0.f, 0.f))
      return StepSubmitResult::Unparsed;
    refX2 = px;
    refY2 = py;
    phase = RotatePhase::AfterReference_WaitAngleOrP;
    log.push_back(
        "Enter new bearing from north ° (decimal/DMS — matches properties), or P for two-point line (C toggles copy).");
    return StepSubmitResult::Handled;
  }
  if (phase == RotatePhase::AfterReference_WaitAngleOrP) {
    const std::string low = StringUtil::toLowerAsciiCopy(trimmedLine);
    if (low == "p") {
      phase = RotatePhase::AnglePoints_WaitP1;
      log.push_back("Angle — first point:");
      return StepSubmitResult::Handled;
    }
    if (TryToggleRotateCopy(*this, trimmedLine, log))
      return StepSubmitResult::Handled;
    float deg = 0.f;
    if (!ParseAngleDegreesInternal(trimmedLine, &deg))
      return StepSubmitResult::Unparsed;
    const float thetaRef = std::atan2(refY2 - refY1, refX2 - refX1);
    const float targetMath = MathAngleRadFromBearingCwNorthDeg(deg);
    const float delta = NormalizeAngleRadMinusPiToPi(targetMath - thetaRef);
    FinishRotateTransform(st, baseX, baseY, baseZ, delta, copyMode, log);
    return StepSubmitResult::Handled;
  }
  if (phase == RotatePhase::AnglePoints_WaitP1) {
    if (TryToggleRotateCopy(*this, trimmedLine, log))
      return StepSubmitResult::Handled;
    float px = 0.f, py = 0.f;
    if (!ParseStoragePoint(st, trimmedLine, &px, &py, false, 0.f, 0.f))
      return StepSubmitResult::Unparsed;
    anglePt1X = px;
    anglePt1Y = py;
    phase = RotatePhase::AnglePoints_WaitP2;
    log.push_back("Angle — second point:");
    return StepSubmitResult::Handled;
  }
  if (phase == RotatePhase::AnglePoints_WaitP2) {
    if (TryToggleRotateCopy(*this, trimmedLine, log))
      return StepSubmitResult::Handled;
    float px = 0.f, py = 0.f;
    if (!ParseStoragePoint(st, trimmedLine, &px, &py, false, 0.f, 0.f))
      return StepSubmitResult::Unparsed;
    const float delta = RotateDeltaFromReferenceAndNewSegment(refX1, refY1, refX2, refY2, anglePt1X, anglePt1Y, px, py);
    FinishRotateTransform(st, baseX, baseY, baseZ, delta, copyMode, log);
    return StepSubmitResult::Handled;
  }
  return StepSubmitResult::Unparsed;
}

void RotateSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                   std::vector<std::string>& log) {
  if (phase == RotatePhase::PickSelection) {
    if (st.selBoxWaitingSecond)
      FinishSelectionBox(st, pick.localX, pick.localY, pick.windowSelectionSubtract,
                         pick.fenceLeftToRightWindowMode, true, log);
    return;
  }
  if (phase == RotatePhase::NeedBase) {
    baseX = static_cast<float>(pick.localX);
    baseY = static_cast<float>(pick.localY);
    baseZ = CadCommitElevation(st);
    phase = RotatePhase::NeedAngleOrReference;
    log.push_back(
        "ROTATE — ° clockwise from north or R reference or C copy; decimal/DMS or click-drag preview.");
    return;
  }
  if (phase == RotatePhase::NeedAngleOrReference) {
    float rad = 0.f;
    if (CadWorkPlaneIsWorldXy(st)) {
      rad = static_cast<float>(-std::atan2(pick.localX - baseX, pick.localY - baseY));
    } else {
      float ax = 0.f, ay = 0.f, az = 0.f;
      CadResolvePickOnWorkPlaneAnchored(st, static_cast<float>(pick.localX), static_cast<float>(pick.localY),
                                        pick.pickRay, baseX, baseY, baseZ, &ax, &ay, &az);
      const ucs::Point2D loc = ucs::WorldToPlane(
          CadWorkPlaneAnchoredAt(st, baseX, baseY, baseZ), {ax, ay, static_cast<double>(az)});
      rad = -std::atan2(static_cast<float>(loc.x), static_cast<float>(loc.y));
    }
    FinishRotateTransform(st, baseX, baseY, baseZ, rad, copyMode, log);
    return;
  }
  if (phase == RotatePhase::AfterReference_WaitAngleOrP) {
    const float thetaRef = std::atan2(refY2 - refY1, refX2 - refX1);
    const float delta = static_cast<float>(std::atan2(pick.localY - baseY, pick.localX - baseX) - thetaRef);
    FinishRotateTransform(st, baseX, baseY, baseZ, delta, copyMode, log);
    return;
  }
  if (phase == RotatePhase::Ref_WaitP1) {
    refX1 = static_cast<float>(pick.localX);
    refY1 = static_cast<float>(pick.localY);
    phase = RotatePhase::Ref_WaitP2;
    log.push_back("Reference — second point:");
    return;
  }
  if (phase == RotatePhase::Ref_WaitP2) {
    refX2 = static_cast<float>(pick.localX);
    refY2 = static_cast<float>(pick.localY);
    phase = RotatePhase::AfterReference_WaitAngleOrP;
    log.push_back("Enter new bearing from north ° (matches properties), or P for two-point line.");
    return;
  }
  if (phase == RotatePhase::AnglePoints_WaitP1) {
    anglePt1X = static_cast<float>(pick.localX);
    anglePt1Y = static_cast<float>(pick.localY);
    phase = RotatePhase::AnglePoints_WaitP2;
    log.push_back("Angle — second point:");
    return;
  }
  if (phase == RotatePhase::AnglePoints_WaitP2) {
    const float delta = RotateDeltaFromReferenceAndNewSegment(
        refX1, refY1, refX2, refY2, anglePt1X, anglePt1Y, static_cast<float>(pick.localX),
        static_cast<float>(pick.localY));
    FinishRotateTransform(st, baseX, baseY, baseZ, delta, copyMode, log);
  }
}

void RotateSession::onCancel(AppCommandState&, std::vector<std::string>& log) {
  log.push_back("ROTATE canceled.");
}

// ---------------------------------------------------------------------------
// MirrorSession
// ---------------------------------------------------------------------------

StepDescriptor MirrorSession::currentStep(const AppCommandState&) const {
  switch (phase) {
  case MirrorPhase::PickSelection:
    return StepDescriptor::SelectObjects(kSelectObjectsPrompt, BlankEnterPolicy::Custom, true);
  case MirrorPhase::NeedP1:
    return StepDescriptor::Point("MIRROR: First point of mirror line — click or X,Y | ESC cancel");
  case MirrorPhase::NeedP2: {
    StepDynInputSpec dyn;
    dyn.pointEntry = true;
    dyn.haveBase = true;
    dyn.basePoint = ray3d::Vec3{p1X, p1Y, p1Z};
    return StepDescriptor::Point("MIRROR: Second point of mirror line — click or X,Y or @dx,dy | ESC cancel", dyn);
  }
  case MirrorPhase::NeedEraseAnswer:
    return StepDescriptor::KeywordOptions("MIRROR: Erase source objects? [Yes/No] <N> | ESC cancel",
                                          BlankEnterPolicy::TakeDefault, StepClickRoute::Ignore);
  }
  return StepDescriptor::Custom("", StepClickRoute::Ignore);
}

StepSubmitResult MirrorSession::onBlankEnter(AppCommandState& st, std::vector<std::string>& log) {
  if (phase == MirrorPhase::PickSelection) {
    if (st.selection.empty() && st.selectedSurveyPointIndices.empty()) {
      log.push_back("Nothing selected — click objects or drag a selection window, then press Enter.");
    } else {
      phase = MirrorPhase::NeedP1;
      log.push_back("MIRROR — specify first point of mirror line:");
    }
    return StepSubmitResult::Handled;
  }
  if (phase == MirrorPhase::NeedEraseAnswer) {
    FinishMirrorTransform(st, p1X, p1Y, p1Z, p2X, p2Y, p2Z, false, log);
    return StepSubmitResult::Handled;
  }
  return StepSubmitResult::Handled;
}

StepSubmitResult MirrorSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                       std::vector<std::string>& log) {
  if (phase == MirrorPhase::NeedP1) {
    float px = 0.f, py = 0.f;
    if (!ParseStoragePoint(st, trimmedLine, &px, &py, false, 0.f, 0.f))
      return StepSubmitResult::Unparsed;
    p1X = px;
    p1Y = py;
    p1Z = CadCommitElevation(st);
    phase = MirrorPhase::NeedP2;
    log.push_back("MIRROR — specify second point of mirror line:");
    return StepSubmitResult::Handled;
  }
  if (phase == MirrorPhase::NeedP2) {
    float px = 0.f, py = 0.f;
    if (!ParseStoragePoint(st, trimmedLine, &px, &py, true, p1X, p1Y))
      return StepSubmitResult::Unparsed;
    if (std::hypot(px - p1X, py - p1Y) < 1e-8f) {
      log.push_back("MIRROR — mirror line needs two distinct points; try again.");
      return StepSubmitResult::Unparsed;
    }
    p2X = px;
    p2Y = py;
    p2Z = CadCommitElevation(st);
    phase = MirrorPhase::NeedEraseAnswer;
    log.push_back("Erase source objects? [Yes/No] <N>:");
    return StepSubmitResult::Handled;
  }
  if (phase == MirrorPhase::NeedEraseAnswer) {
    const std::string low = StringUtil::toLowerAsciiCopy(trimmedLine);
    bool eraseSource = false;
    if (low.empty() || low == "n" || low == "no")
      eraseSource = false;
    else if (low == "y" || low == "yes")
      eraseSource = true;
    else {
      log.push_back("MIRROR — answer Yes or No (Enter defaults to No).");
      return StepSubmitResult::Unparsed;
    }
    FinishMirrorTransform(st, p1X, p1Y, p1Z, p2X, p2Y, p2Z, eraseSource, log);
    return StepSubmitResult::Handled;
  }
  return StepSubmitResult::Unparsed;
}

void MirrorSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                   std::vector<std::string>& log) {
  if (phase == MirrorPhase::PickSelection) {
    if (st.selBoxWaitingSecond)
      FinishSelectionBox(st, pick.localX, pick.localY, pick.windowSelectionSubtract,
                         pick.fenceLeftToRightWindowMode, true, log);
    return;
  }
  if (phase == MirrorPhase::NeedP1) {
    p1X = static_cast<float>(pick.localX);
    p1Y = static_cast<float>(pick.localY);
    p1Z = CadCommitElevation(st);
    phase = MirrorPhase::NeedP2;
    log.push_back("MIRROR — specify second point of mirror line:");
    return;
  }
  if (phase == MirrorPhase::NeedP2) {
    // ORTHO locks the mirror line, as it does in the preview.
    float px = static_cast<float>(pick.localX), py = static_cast<float>(pick.localY), pz = CadCommitElevation(st);
    ApplyOrthoConstrainFromAnchor(st, p1X, p1Y, &px, &py, st.orthoMode, p1Z, pz, &pz);
    if (std::hypot(px - p1X, py - p1Y) < 1e-8f) {
      log.push_back("MIRROR — mirror line needs two distinct points; try again.");
      return;
    }
    p2X = px;
    p2Y = py;
    p2Z = pz;
    phase = MirrorPhase::NeedEraseAnswer;
    log.push_back("Erase source objects? [Yes/No] <N>:");
  }
}

void MirrorSession::onCancel(AppCommandState&, std::vector<std::string>& log) {
  log.push_back("MIRROR canceled.");
}

// ---------------------------------------------------------------------------
// Pilot command registration
// ---------------------------------------------------------------------------

void RegisterModifyPilotCommands(CommandRegistry& reg) {
  reg.Add({"move", "m", "Move selected objects (base point, then destination)", CadCommandKind::Move, true, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartMoveCommand(st, log);
           }});
  reg.Add({"copy", "co,cp", "Copy selected objects (keeps originals)", CadCommandKind::Copy, true, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartCopyCommand(st, log);
           }});
  reg.Add({"rotate", "ro", "Rotate selected objects (bearing CW from north or Reference)", CadCommandKind::Rotate,
           true, true, [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartRotateCommand(st, log);
           }});
  reg.Add({"scale", "sc", "Scale selected objects by factor or reference length", CadCommandKind::Scale, true, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartScaleCommand(st, log);
           }});
  reg.Add({"mirror", "mi", "Mirror selected objects across a two-point line (optional erase source)",
           CadCommandKind::Mirror, true, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartMirrorCommand(st, log);
           }});
  reg.Add({"stretch", "s", "Stretch vertices inside a crossing/window box by base point and destination",
           CadCommandKind::Stretch, true, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartStretchCommand(st, log);
           }});
  reg.Add({"paste", "pasteclip", "Paste clipboard objects at a picked point (Ctrl+V)", CadCommandKind::Paste, false,
           true, [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartPasteCommand(st, log);
           }});
}

} // namespace stepengine
