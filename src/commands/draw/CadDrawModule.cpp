// ADR-0012 / Issue #1578 (Track B Batch 2): Step-engine command modules for the
// Draw, Annotation, Inquiry, and DrawInquiry command families.

#include "CadCommandsInternal.hpp"
#include "draw/CadDrawInternal.hpp"
#include "annotation/CadAnnotationInternal.hpp"
#include "inquiry/CadInquiryInternal.hpp"
#include "input/CadInputInternal.hpp"
#include "engine/CommandRegistry.hpp"
#include "engine/StepEngine.hpp"
#include "CadCoordinateFrame.hpp"

#include <GoSurvey/Core/StringUtil.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

bool SubmitLineVertex(AppCommandState& st, float x, float y, std::vector<std::string>& log);
bool SubmitPolylineVertex(AppCommandState& st, float x, float y, std::vector<std::string>& log);
void CommitRectangle(AppCommandState& st, float x1, float y1, float x2, float y2,
                     std::vector<std::string>& log);
void CommitMleaderLandingAt(AppCommandState& st, float landX, float landY, std::vector<std::string>& log);
void CommitMleaderAddLeaderAt(AppCommandState& st, float tipX, float tipY, std::vector<std::string>& log);
void AbandonJustPlacedMultileader(AppCommandState& st);
bool HandleCadDrawInquiryTextInput(const std::string& lineIn, AppCommandState& st, std::vector<std::string>& log);
bool SubmitCadDrawInquiryViewportPick(AppCommandState& st, double wx, double wy, std::vector<std::string>& log);
const char* CadDrawInquiryCommandInputHint(const AppCommandState& st);

namespace stepengine {

namespace {

bool ParseSingleFloatTokenLocal(const std::string& raw, float* out) {
  const std::string s = StringUtil::trimCopy(raw);
  if (s.empty())
    return false;
  char* end = nullptr;
  const double v = std::strtod(s.c_str(), &end);
  if (!end || *end != '\0' || !std::isfinite(v))
    return false;
  *out = static_cast<float>(v);
  return true;
}

bool HandleSegmentAnglePreText(AppCommandState& st, SegmentAngleState& ang, bool inNextPt,
                               const std::string& line, std::vector<std::string>& log) {
  if (!inNextPt)
    return false;
  if (ang.pickPhase == SegmentAnglePickPhase::WaitAdjustOrCommit) {
    if (line.empty()) {
      CommitSegmentAnglePickLock(st, log);
      return true;
    }
    const std::string t = StringUtil::trimCopy(line);
    if (!t.empty() && (t[0] == '+' || t[0] == '-')) {
      float dlt = 0.f;
      if (!ParseAngleDegreesInternal(t, &dlt)) {
        log.push_back("Bearing pick — invalid adjustment (decimal/DMS). Blank Enter locks; +/- adds turn.");
        return true;
      }
      ang.pickDraftBearingDeg = NormalizeBearingDegreesCwNorth(ang.pickDraftBearingDeg + dlt);
      CommitSegmentAnglePickLock(st, log);
      return true;
    }
    log.push_back("Bearing pick — blank Enter to lock, or +90 / -45 first (° clockwise from north).");
    return true;
  }
  if (ang.keyboardAwaitBearing) {
    if (line.empty()) {
      ang.keyboardAwaitBearing = false;
      log.push_back("Bearing entry canceled.");
      return true;
    }
    float combined = 0.f;
    if (!ParseBearingCwNorthStringWithOptionalDelta(line, &combined, log))
      return true;
    const float theta = MathAngleRadFromBearingCwNorthDeg(combined);
    ang.lockUx = std::cos(theta);
    ang.lockUy = std::sin(theta);
    ang.lockActive = true;
    ang.pickPhase = SegmentAnglePickPhase::Idle;
    ang.keyboardAwaitBearing = false;
    char bufKb[144];
    std::snprintf(bufKb, sizeof(bufKb),
                  "Bearing lock %.6g° clockwise from north — distance (+/- along ray) or click (A clears).",
                  static_cast<double>(combined));
    log.push_back(bufKb);
    return true;
  }
  return false;
}

bool ApplySegmentAnglePick(AppCommandState& st, SegmentAngleState& ang, double& wx, double& wy,
                           bool inNextPtPhase, std::vector<std::string>& log) {
  if (!inNextPtPhase)
    return false;
  if (ang.keyboardAwaitBearing) {
    log.push_back("Finish bearing entry on the command line (blank Enter cancels) before viewport picks.");
    return true;
  }
  if (ang.pickPhase == SegmentAnglePickPhase::WaitP1) {
    ang.pickRefX1 = static_cast<float>(wx);
    ang.pickRefY1 = static_cast<float>(wy);
    ang.pickPhase = SegmentAnglePickPhase::WaitP2;
    log.push_back("Bearing pick — second reference point:");
    return true;
  }
  if (ang.pickPhase == SegmentAnglePickPhase::WaitP2) {
    const float dx = static_cast<float>(wx - ang.pickRefX1);
    const float dy = static_cast<float>(wy - ang.pickRefY1);
    if (std::hypot(dx, dy) < 1e-8f) {
      log.push_back("Bearing pick — points coincide; pick again.");
    } else {
      ang.pickDraftBearingDeg = BearingCwNorthDegFromMathAngleRad(std::atan2(dy, dx));
      ang.pickPhase = SegmentAnglePickPhase::WaitAdjustOrCommit;
      log.push_back("Bearing from picks — Enter locks as-is; type +90 / -45 (° CW from N) to adjust and lock (one line).");
    }
    return true;
  }
  if (ang.pickPhase == SegmentAnglePickPhase::WaitAdjustOrCommit) {
    log.push_back("Bearing pick — press Enter to lock (or type +90 / -45); viewport click ignored in this step.");
    return true;
  }
  if (ang.lockActive) {
    float lwx = static_cast<float>(wx), lwy = static_cast<float>(wy);
    ApplySegmentAngleLockToWorldPick(st.anchorX, st.anchorY, ang.lockUx, ang.lockUy, &lwx, &lwy, false);
    wx = lwx;
    wy = lwy;
  } else {
    float owx = static_cast<float>(wx), owy = static_cast<float>(wy);
    ApplyOrthoConstrainFromAnchor(st, st.anchorX, st.anchorY, &owx, &owy, st.orthoMode, st.anchorZ,
                                  st.resolvedPointZ, &st.resolvedPointZ);
    wx = owx;
    wy = owy;
    st.resolvedPointZValid = true;
  }
  return false;
}

} // namespace

// ============================================================================
// LINE
// ============================================================================

StepDescriptor LineSession::currentStep(const AppCommandState& st) const {
  StepDynInputSpec dyn;
  const char* prompt = "";
  if (phase == LinePhase::NeedFirstPoint) {
    dyn.pointEntry = true;
    prompt = "LINE: First point — click or X,Y | ESC ends command";
  } else {
    dyn.pointEntry = !(angle.keyboardAwaitBearing || angle.lockActive ||
                       angle.pickPhase != SegmentAnglePickPhase::Idle);
    dyn.haveBase = true;
    dyn.basePoint = ray3d::Vec3{st.anchorX, st.anchorY, st.anchorZ};
    dyn.initialRelativePolar = true;
    dyn.directDistance = st.orthoMode;
    if (angle.keyboardAwaitBearing)
      prompt = "LINE: Type bearing ° CW from N (decimal/DMS) | blank Enter cancels | ESC ends command";
    else if (angle.pickPhase == SegmentAnglePickPhase::WaitP1)
      prompt = "LINE bearing pick: First direction point — click | 2P started | ESC cancels pick";
    else if (angle.pickPhase == SegmentAnglePickPhase::WaitP2)
      prompt = "LINE bearing pick: Second point — direction | ESC cancels pick";
    else if (angle.pickPhase == SegmentAnglePickPhase::WaitAdjustOrCommit)
      prompt = "LINE bearing pick: Enter locks | +90/-45 adjust+lock | ESC cancels pick";
    else if (angle.lockActive)
      prompt = "LINE (bearing lock): distance ± along ray | click on line | X,Y | @dx,dy | [A] clears";
    else
      prompt = "Next: click; X, Y; @dx,dy; [A]zimuth, [2P];";
  }
  return StepDescriptor::Point(prompt, dyn, BlankEnterPolicy::Custom);
}

StepSubmitResult LineSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                     std::vector<std::string>& log) {
  const bool allowRel = (phase == LinePhase::NeedNextPoint);
  if (HandleSegmentAnglePreText(st, angle, allowRel, trimmedLine, log))
    return StepSubmitResult::Handled;

  if (allowRel && TryParseSegmentAngleLockCommand(st, trimmedLine, log))
    return StepSubmitResult::Handled;

  float px = 0.f, py = 0.f;
  if (ParseStoragePoint(st, trimmedLine, &px, &py, allowRel, st.anchorX, st.anchorY, &st.anchorZ)) {
    SubmitLineVertex(st, px, py, log);
    return StepSubmitResult::Handled;
  }
  if (!st.alignmentStationSnapRejectReason.empty()) {
    log.push_back(st.alignmentStationSnapRejectReason);
    st.alignmentStationSnapRejectReason.clear();
    return StepSubmitResult::Handled;
  }

  if (allowRel && angle.lockActive) {
    float dist = 0.f;
    if (ParseSingleFloatTokenLocal(trimmedLine, &dist)) {
      if (std::fabs(dist) < 1e-20f)
        log.push_back("LINE — distance must be non-zero.");
      else
        SubmitLineVertex(st, st.anchorX + angle.lockUx * dist, st.anchorY + angle.lockUy * dist, log);
      return StepSubmitResult::Handled;
    }
  }

  if (allowRel && st.orthoMode) {
    float dist = 0.f;
    if (ParseSingleFloatTokenLocal(trimmedLine, &dist)) {
      if (CadUcsIsWorld(st)) {
        float ux = 0.f, uy = 0.f;
        if (!OrthoUnitTowardUiCursorFromAnchor(st, &ux, &uy))
          log.push_back("Ortho distance needs cursor direction — move crosshair away from anchor, then enter distance.");
        else
          SubmitLineVertex(st, st.anchorX + ux * dist, st.anchorY + uy * dist, log);
      } else {
        if (!OrthoUcsDirectDistancePoint(st, dist, &px, &py))
          log.push_back("Ortho distance needs cursor direction — move crosshair away from anchor, then enter distance.");
        else
          SubmitLineVertex(st, px, py, log);
      }
      return StepSubmitResult::Handled;
    }
  }

  ReportUnparsedInput(
      st, trimmedLine,
      std::string("Could not parse point. Use X,Y or X Y") +
          (allowRel ? "; @dx,dy; A / 2P (two picks); A 45 +90; ortho distance toward cursor." : "."),
      log, log.size());
  return StepSubmitResult::Handled;
}

StepSubmitResult LineSession::onBlankEnter(AppCommandState& st, std::vector<std::string>& log) {
  const bool nextPt = (phase == LinePhase::NeedNextPoint);
  if (HandleSegmentAnglePreText(st, angle, nextPt, "", log))
    return StepSubmitResult::Handled;
  if (nextPt) {
    angle.reset();
    phase = LinePhase::NeedFirstPoint;
    draftSegments = 0;
    log.push_back("LINE — specify first point (click or type X,Y / X Y). ESC to cancel.");
  }
  return StepSubmitResult::Handled;
}

void LineSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                 std::vector<std::string>& log) {
  double wx = pick.localX;
  double wy = pick.localY;
  const bool nextPt = (phase == LinePhase::NeedNextPoint);
  if (!ApplySegmentAnglePick(st, angle, wx, wy, nextPt, log))
    SubmitLineVertex(st, static_cast<float>(wx), static_cast<float>(wy), log);
}

void LineSession::onCancel(AppCommandState&, std::vector<std::string>& log) {
  log.push_back("LINE canceled.");
}

// ============================================================================
// POLYLINE / 3DPOLY
// ============================================================================

StepDescriptor PolylineSession::currentStep(const AppCommandState& st) const {
  StepDynInputSpec dyn;
  const char* prompt = "";
  if (phase == PolylinePhase::NeedFirstPoint) {
    dyn.pointEntry = true;
    prompt = "POLYLINE: First point — click or X,Y | [CLOSE] closes | ESC cancel";
  } else {
    dyn.pointEntry = !(angle.keyboardAwaitBearing || angle.lockActive ||
                       angle.pickPhase != SegmentAnglePickPhase::Idle);
    dyn.haveBase = true;
    dyn.basePoint = ray3d::Vec3{st.anchorX, st.anchorY, st.anchorZ};
    dyn.initialRelativePolar = true;
    dyn.directDistance = st.orthoMode;
    if (angle.keyboardAwaitBearing)
      prompt = "POLYLINE: Type bearing ° CW from N | blank Enter cancels | ESC cancel";
    else if (angle.pickPhase == SegmentAnglePickPhase::WaitP1)
      prompt = "POLYLINE bearing pick: First direction click | ESC cancels pick";
    else if (angle.pickPhase == SegmentAnglePickPhase::WaitP2)
      prompt = "POLYLINE bearing pick: Second point | ESC cancels pick";
    else if (angle.pickPhase == SegmentAnglePickPhase::WaitAdjustOrCommit)
      prompt = "POLYLINE bearing pick: Enter locks | +90/-45 adjust+lock | ESC cancels pick";
    else if (angle.lockActive)
      prompt = "POLYLINE (bearing lock): distance ± | click on ray | X,Y | [A] clears | [CLOSE] / [END]";
    else
      prompt = "POLYLINE: Next — click | X,Y | @dx,dy | [A]/[2P] | [CLOSE] / [END] | ESC";
  }
  return StepDescriptor::Point(prompt, dyn, BlankEnterPolicy::Custom);
}

StepSubmitResult PolylineSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                         std::vector<std::string>& log) {
  const bool allowRel = (phase == PolylinePhase::NeedNextPoint);
  if (HandleSegmentAnglePreText(st, angle, allowRel, trimmedLine, log))
    return StepSubmitResult::Handled;

  const std::string low = StringUtil::toLowerAsciiCopy(trimmedLine);

  std::string pointText = trimmedLine;
  if (draft3d) {
    const size_t c1 = trimmedLine.find(',');
    const size_t c2 = (c1 == std::string::npos) ? std::string::npos : trimmedLine.find(',', c1 + 1);
    if (c2 != std::string::npos) {
      if (trimmedLine.find(',', c2 + 1) != std::string::npos) {
        log.push_back("3DPOLY — too many coordinates: X,Y,Z or @dx,dy,dz.");
        return StepSubmitResult::Handled;
      }
      const std::string zText = StringUtil::trimCopy(trimmedLine.substr(c2 + 1));
      char* zEnd = nullptr;
      const double zv = std::strtod(zText.c_str(), &zEnd);
      if (!zText.empty() && zEnd && *zEnd == '\0' && std::isfinite(zv)) {
        st.polylineTypedZ = static_cast<float>(zv);
        st.polylineTypedZRelative = !trimmedLine.empty() && trimmedLine[0] == '@';
        st.polylineTypedZValid = true;
        pointText = trimmedLine.substr(0, c2);
      } else {
        log.push_back("3DPOLY — elevation must be a number: X,Y,Z or @dx,dy,dz.");
        return StepSubmitResult::Handled;
      }
    }
  }

  if (!draft3d) {
    if (low == "arc") {
      arcMode = true;
      log.push_back("POLYLINE — arc mode. Next segment is a circular arc (LINE to switch back).");
      return StepSubmitResult::Handled;
    }
    if (low == "line") {
      arcMode = false;
      arcRadius = 0.f;
      arcAngleValid = false;
      log.push_back("POLYLINE — line mode.");
      return StepSubmitResult::Handled;
    }
    if (arcMode && (low.rfind("radius", 0) == 0 || low.rfind("rad ", 0) == 0 ||
                    (low.size() > 1 && low[0] == 'r' &&
                     (std::isdigit(static_cast<unsigned char>(low[1])) || low[1] == '-' || low[1] == '.')))) {
      const size_t sp = low.find_first_of(" 0123456789-.");
      float rv = 0.f;
      if (sp != std::string::npos && ParseSingleFloatTokenLocal(StringUtil::trimCopy(trimmedLine.substr(sp)), &rv) &&
          rv > 1e-9f) {
        arcRadius = rv;
        arcAngleValid = false;
        log.push_back("POLYLINE arc — radius set; pick the arc end point.");
      } else {
        log.push_back("POLYLINE arc — RADIUS <value>.");
      }
      return StepSubmitResult::Handled;
    }
    if (arcMode && (low.rfind("cangle", 0) == 0 || low.rfind("ca ", 0) == 0)) {
      const size_t sp = low.find_first_of(" 0123456789-.");
      float av = 0.f;
      if (sp != std::string::npos && ParseSingleFloatTokenLocal(StringUtil::trimCopy(trimmedLine.substr(sp)), &av) &&
          std::fabs(av) > 1e-6f) {
        arcAngleDeg = av;
        arcAngleValid = true;
        arcRadius = 0.f;
        log.push_back("POLYLINE arc — included angle set; pick the arc end point.");
      } else {
        log.push_back("POLYLINE arc — CANGLE <degrees>.");
      }
      return StepSubmitResult::Handled;
    }
  }

  if (low == "undo" || low == "u") {
    if (phase != PolylinePhase::NeedNextPoint || draftVerts.size() < 6) {
      log.push_back("POLYLINE — nothing to undo.");
      return StepSubmitResult::Handled;
    }
    draftVerts.resize(draftVerts.size() - 3);
    if (!draftBulge.empty())
      draftBulge.pop_back();
    if (!draftBulge.empty())
      draftBulge.back() = 0.f;
    if (draftSegments > 0)
      --draftSegments;
    const size_t n = draftVerts.size();
    st.anchorX = draftVerts[n - 3];
    st.anchorY = draftVerts[n - 2];
    st.anchorZ = draftVerts[n - 1];
    log.push_back("POLYLINE — last vertex removed.");
    return StepSubmitResult::Handled;
  }

  if (low == "close" || low == "cl") {
    CancelSegmentAnglePick(st, nullptr);
    if (phase != PolylinePhase::NeedNextPoint || draftSegments == 0) {
      log.push_back("POLYLINE CLOSE — need at least one segment after the start point.");
      return StepSubmitResult::Handled;
    }
    if (arcMode && !draftBulge.empty())
      draftBulge.back() = CadPolylineDraftBulgeForNextPoint(st, firstX, firstY);
    CommitPolylineDraft(st, true, log);
    return st.active == CadCommandKind::None ? StepSubmitResult::Finished : StepSubmitResult::Handled;
  }

  if (low == "end") {
    CancelSegmentAnglePick(st, nullptr);
    if (phase != PolylinePhase::NeedNextPoint || draftSegments == 0) {
      log.push_back("POLYLINE END — need at least one segment after the start point.");
      return StepSubmitResult::Handled;
    }
    CommitPolylineDraft(st, false, log);
    return st.active == CadCommandKind::None ? StepSubmitResult::Finished : StepSubmitResult::Handled;
  }

  if (allowRel && TryParseSegmentAngleLockCommand(st, trimmedLine, log))
    return StepSubmitResult::Handled;

  float px = 0.f, py = 0.f;
  if (ParseStoragePoint(st, pointText, &px, &py, allowRel, st.anchorX, st.anchorY, &st.anchorZ)) {
    SubmitPolylineVertex(st, px, py, log);
    return st.active == CadCommandKind::None ? StepSubmitResult::Finished : StepSubmitResult::Handled;
  }

  if (allowRel && angle.lockActive) {
    float dist = 0.f;
    if (ParseSingleFloatTokenLocal(trimmedLine, &dist)) {
      if (std::fabs(dist) < 1e-20f)
        log.push_back("POLYLINE — distance must be non-zero.");
      else
        SubmitPolylineVertex(st, st.anchorX + angle.lockUx * dist, st.anchorY + angle.lockUy * dist, log);
      return st.active == CadCommandKind::None ? StepSubmitResult::Finished : StepSubmitResult::Handled;
    }
  }

  if (allowRel && st.orthoMode) {
    float dist = 0.f;
    if (ParseSingleFloatTokenLocal(trimmedLine, &dist)) {
      if (CadUcsIsWorld(st)) {
        float ux = 0.f, uy = 0.f;
        if (!OrthoUnitTowardUiCursorFromAnchor(st, &ux, &uy))
          log.push_back("Ortho distance needs cursor direction — move crosshair away from anchor, then enter distance.");
        else
          SubmitPolylineVertex(st, st.anchorX + ux * dist, st.anchorY + uy * dist, log);
      } else {
        if (!OrthoUcsDirectDistancePoint(st, dist, &px, &py))
          log.push_back("Ortho distance needs cursor direction — move crosshair away from anchor, then enter distance.");
        else
          SubmitPolylineVertex(st, px, py, log);
      }
      return st.active == CadCommandKind::None ? StepSubmitResult::Finished : StepSubmitResult::Handled;
    }
  }

  st.polylineTypedZValid = false;
  log.push_back("POLYLINE — X,Y / @dx,dy / A or 2P bearing / ARC / LINE / UNDO / CLOSE / END / ortho distance.");
  return StepSubmitResult::Handled;
}

StepSubmitResult PolylineSession::onBlankEnter(AppCommandState& st, std::vector<std::string>& log) {
  const bool nextPt = (phase == PolylinePhase::NeedNextPoint);
  if (HandleSegmentAnglePreText(st, angle, nextPt, "", log))
    return StepSubmitResult::Handled;
  if (nextPt) {
    CancelSegmentAnglePick(st, nullptr);
    angle.reset();
    if (draftSegments == 0) {
      log.push_back("POLYLINE END — need at least one segment after the start point.");
      return StepSubmitResult::Handled;
    }
    CommitPolylineDraft(st, /*closed=*/false, log);
    return st.active == CadCommandKind::None ? StepSubmitResult::Finished : StepSubmitResult::Handled;
  }
  return StepSubmitResult::Handled;
}

void PolylineSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                     std::vector<std::string>& log) {
  double wx = pick.localX;
  double wy = pick.localY;
  const bool nextPt = (phase == PolylinePhase::NeedNextPoint);
  if (nextPt && !draftVerts.empty() && wx == firstX && wy == firstY) {
    if (draftSegments == 0)
      log.push_back("POLYLINE CLOSE — need at least one segment after the start point.");
    else
      CommitPolylineDraft(st, /*closed=*/true, log);
    return;
  }
  if (!ApplySegmentAnglePick(st, angle, wx, wy, nextPt, log))
    SubmitPolylineVertex(st, static_cast<float>(wx), static_cast<float>(wy), log);
}

void PolylineSession::onCancel(AppCommandState&, std::vector<std::string>& log) {
  log.push_back("POLYLINE canceled.");
}

void PolylineSession::onReset(AppCommandState& st) {
  st.polylineTypedZValid = false;
  st.polylineTypedZRelative = false;
  st.polylineTypedZ = 0.f;
}

// ============================================================================
// CIRCLE
// ============================================================================

StepDescriptor CircleSession::currentStep(const AppCommandState&) const {
  StepDynInputSpec dyn;
  dyn.pointEntry = (phase == CirclePhase::WaitCenterOrMode || phase == CirclePhase::ThreeP_WaitP1 ||
                    phase == CirclePhase::ThreeP_WaitP2 || phase == CirclePhase::ThreeP_WaitP3);
  const char* prompt = "";
  switch (phase) {
  case CirclePhase::WaitCenterOrMode:
    prompt = "CIRCLE: Click or type center | Type [3P] for three-point circle | ESC cancel";
    break;
  case CirclePhase::WaitRadius:
    prompt = "CIRCLE: Click edge for radius | Type radius | D <value> or D<value> for diameter | ESC cancel";
    break;
  case CirclePhase::ThreeP_WaitP1:
    prompt = "CIRCLE (3P): Point 1 of 3 — click or X,Y | ESC cancel";
    break;
  case CirclePhase::ThreeP_WaitP2:
    prompt = "CIRCLE (3P): Point 2 of 3 — click or X,Y | ESC cancel";
    break;
  case CirclePhase::ThreeP_WaitP3:
    prompt = "CIRCLE (3P): Point 3 of 3 — click or X,Y | ESC cancel";
    break;
  }
  return StepDescriptor::Custom(prompt, StepClickRoute::SnappedPointPick, BlankEnterPolicy::Custom,
                                false, false, dyn);
}

StepSubmitResult CircleSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                       std::vector<std::string>& log) {
  return HandleCircleTextInput(trimmedLine, st, log) ? StepSubmitResult::Handled : StepSubmitResult::Unparsed;
}

StepSubmitResult CircleSession::onBlankEnter(AppCommandState&, std::vector<std::string>&) {
  return StepSubmitResult::Handled;
}

void CircleSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                   std::vector<std::string>& log) {
  const float wx = static_cast<float>(pick.localX);
  const float wy = static_cast<float>(pick.localY);
  switch (phase) {
  case CirclePhase::WaitCenterOrMode:
    cx = wx;
    cy = wy;
    cz = CadCommitElevation(st);
    phase = CirclePhase::WaitRadius;
    log.push_back("Center set — specify radius (click near edge), type radius, or D + diameter.");
    break;
  case CirclePhase::WaitRadius:
    CommitCircleFromRimPick(st, cx, cy, cz, wx, wy, CadCommitElevation(st), log);
    break;
  case CirclePhase::ThreeP_WaitP1:
    c3p1x = wx;
    c3p1y = wy;
    c3p1z = CadCommitElevation(st);
    phase = CirclePhase::ThreeP_WaitP2;
    log.push_back("Second point of circle:");
    break;
  case CirclePhase::ThreeP_WaitP2:
    c3p2x = wx;
    c3p2y = wy;
    c3p2z = CadCommitElevation(st);
    phase = CirclePhase::ThreeP_WaitP3;
    log.push_back("Third point of circle:");
    break;
  case CirclePhase::ThreeP_WaitP3:
    if (!CommitCircleThreePoints(st, c3p1x, c3p1y, c3p1z, c3p2x, c3p2y, c3p2z, wx, wy,
                                 CadCommitElevation(st), log))
      log.push_back("Points are collinear — pick a non-collinear third point.");
    break;
  }
}

void CircleSession::onCancel(AppCommandState&, std::vector<std::string>& log) {
  log.push_back("CIRCLE canceled.");
}

// ============================================================================
// ARC
// ============================================================================

StepDescriptor ArcSession::currentStep(const AppCommandState&) const {
  const char* prompt = "";
  switch (phase) {
  case ArcPhase::WaitStart: prompt = "ARC: Start point | ESC cancel"; break;
  case ArcPhase::WaitMid:   prompt = "ARC: Point on arc | ESC cancel"; break;
  case ArcPhase::WaitEnd:   prompt = "ARC: End point | ESC cancel"; break;
  }
  StepDynInputSpec dyn;
  dyn.pointEntry = true;
  return StepDescriptor::Point(prompt, dyn, BlankEnterPolicy::Custom);
}

StepSubmitResult ArcSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                    std::vector<std::string>& log) {
  float px = 0.f, py = 0.f;
  if (ParseStoragePoint(st, trimmedLine, &px, &py, false, 0.f, 0.f)) {
    StepPickInput p;
    p.localX = px;
    p.localY = py;
    onViewportPick(st, p, log);
    return StepSubmitResult::Handled;
  }
  return StepSubmitResult::Unparsed;
}

StepSubmitResult ArcSession::onBlankEnter(AppCommandState&, std::vector<std::string>&) {
  return StepSubmitResult::Handled;
}

void ArcSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                std::vector<std::string>& log) {
  const float wx = static_cast<float>(pick.localX);
  const float wy = static_cast<float>(pick.localY);
  switch (phase) {
  case ArcPhase::WaitStart:
    ax = wx;
    ay = wy;
    az = CadCommitElevation(st);
    phase = ArcPhase::WaitMid;
    log.push_back("ARC — pick middle point on arc:");
    break;
  case ArcPhase::WaitMid:
    bx = wx;
    by = wy;
    bz = CadCommitElevation(st);
    phase = ArcPhase::WaitEnd;
    log.push_back("ARC — pick end point:");
    break;
  case ArcPhase::WaitEnd:
    CommitArcThreePoints(st, ax, ay, az, bx, by, bz, wx, wy, CadCommitElevation(st), log);
    break;
  }
}

void ArcSession::onCancel(AppCommandState&, std::vector<std::string>& log) {
  log.push_back("ARC canceled.");
}

// ============================================================================
// ELLIPSE
// ============================================================================

StepDescriptor EllipseSession::currentStep(const AppCommandState&) const {
  StepDynInputSpec dyn;
  dyn.pointEntry = (phase == EllipsePhase::WaitCenter || phase == EllipsePhase::WaitMajorEnd);
  const char* prompt = "";
  switch (phase) {
  case EllipsePhase::WaitCenter:   prompt = "ELLIPSE: Center | ESC cancel"; break;
  case EllipsePhase::WaitMajorEnd: prompt = "ELLIPSE: Major axis end | ESC cancel"; break;
  case EllipsePhase::WaitRatio:    prompt = "ELLIPSE: Ratio (0-1] on command line | Enter = 0.5 | ESC cancel"; break;
  }
  return StepDescriptor::Custom(prompt, StepClickRoute::SnappedPointPick, BlankEnterPolicy::Custom,
                                false, false, dyn);
}

StepSubmitResult EllipseSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                        std::vector<std::string>& log) {
  if (phase == EllipsePhase::WaitRatio) {
    float ratio = 0.5f;
    if (!trimmedLine.empty() && !ParseSingleFloatTokenLocal(trimmedLine, &ratio)) {
      log.push_back("ELLIPSE — enter one number for minor/major ratio (0-1], or blank for 0.5.");
      return StepSubmitResult::Handled;
    }
    FinishEllipseFromRatio(st, ratio, log);
    return st.active == CadCommandKind::None ? StepSubmitResult::Finished : StepSubmitResult::Handled;
  }
  float px = 0.f, py = 0.f;
  if (ParseStoragePoint(st, trimmedLine, &px, &py, false, 0.f, 0.f)) {
    StepPickInput p;
    p.localX = px;
    p.localY = py;
    onViewportPick(st, p, log);
    return StepSubmitResult::Handled;
  }
  return StepSubmitResult::Unparsed;
}

StepSubmitResult EllipseSession::onBlankEnter(AppCommandState& st, std::vector<std::string>& log) {
  if (phase == EllipsePhase::WaitRatio) {
    FinishEllipseFromRatio(st, 0.5f, log);
    return st.active == CadCommandKind::None ? StepSubmitResult::Finished : StepSubmitResult::Handled;
  }
  return StepSubmitResult::Handled;
}

void EllipseSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                    std::vector<std::string>& log) {
  const float wx = static_cast<float>(pick.localX);
  const float wy = static_cast<float>(pick.localY);
  switch (phase) {
  case EllipsePhase::WaitCenter:
    cx = wx;
    cy = wy;
    cz = CadCommitElevation(st);
    phase = EllipsePhase::WaitMajorEnd;
    log.push_back("ELLIPSE — major axis endpoint:");
    break;
  case EllipsePhase::WaitMajorEnd:
    majEx = wx;
    majEy = wy;
    majEz = CadCommitElevation(st);
    phase = EllipsePhase::WaitRatio;
    log.push_back("ELLIPSE — type minor/major ratio (0-1], or Enter for 0.5:");
    break;
  case EllipsePhase::WaitRatio:
    log.push_back("ELLIPSE — type ratio on command line (middle mouse pick ignored here).");
    break;
  }
}

void EllipseSession::onCancel(AppCommandState&, std::vector<std::string>& log) {
  log.push_back("ELLIPSE canceled.");
}

// ============================================================================
// RECT
// ============================================================================

StepDescriptor RectSession::currentStep(const AppCommandState& st) const {
  StepDynInputSpec dyn;
  dyn.pointEntry = true;
  if (phase == RectPhase::WaitSecondCorner) {
    dyn.haveBase = true;
    dyn.basePoint = ray3d::Vec3{x1, y1, CadWorkPlaneElevation(st)};
  }
  const char* prompt = (phase == RectPhase::WaitFirstCorner)
                           ? "RECT: First corner — click or X,Y | ESC cancel"
                           : "RECT: Opposite corner — click or X,Y / @dx,dy | ESC cancel";
  return StepDescriptor::Point(prompt, dyn, BlankEnterPolicy::Custom);
}

StepSubmitResult RectSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                     std::vector<std::string>& log) {
  const bool second = (phase == RectPhase::WaitSecondCorner);
  float px = 0.f, py = 0.f;
  if (ParseStoragePoint(st, trimmedLine, &px, &py, second, x1, y1)) {
    if (second) {
      CommitRectangle(st, x1, y1, px, py, log);
    } else {
      x1 = px;
      y1 = py;
      st.anchorX = px;
      st.anchorY = py;
      phase = RectPhase::WaitSecondCorner;
      log.push_back("RECT — pick the opposite corner (or type X,Y / @dx,dy):");
    }
    return StepSubmitResult::Handled;
  }
  log.push_back(std::string("RECT — could not parse point. Use X,Y") +
                (second ? " or @dx,dy for an exact width x height." : "."));
  return StepSubmitResult::Handled;
}

StepSubmitResult RectSession::onBlankEnter(AppCommandState&, std::vector<std::string>&) {
  return StepSubmitResult::Handled;
}

void RectSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                 std::vector<std::string>& log) {
  const float wx = static_cast<float>(pick.localX);
  const float wy = static_cast<float>(pick.localY);
  if (phase == RectPhase::WaitFirstCorner) {
    x1 = wx;
    y1 = wy;
    phase = RectPhase::WaitSecondCorner;
    st.anchorX = wx;
    st.anchorY = wy;
    log.push_back("RECT — pick the opposite corner (or type X,Y / @dx,dy):");
  } else {
    CommitRectangle(st, x1, y1, wx, wy, log);
  }
}

void RectSession::onCancel(AppCommandState&, std::vector<std::string>& log) {
  log.push_back("RECT canceled.");
}

// ============================================================================
// SPLINE
// ============================================================================

StepDescriptor SplineSession::currentStep(const AppCommandState&) const {
  char buf[160];
  std::snprintf(buf, sizeof(buf),
                "SPLINE: %zu points — click or type X,Y | CV / Fit | Close / End / Undo | ESC cancel",
                draftPoints.size());
  StepDynInputSpec dyn;
  dyn.pointEntry = false;
  return StepDescriptor::Custom(buf, StepClickRoute::SnappedPointPick, BlankEnterPolicy::Custom,
                                false, false, dyn);
}

StepSubmitResult SplineSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                       std::vector<std::string>& log) {
  (void)HandleSplineTextInput(trimmedLine, st, log);
  return st.active == CadCommandKind::None ? StepSubmitResult::Finished : StepSubmitResult::Handled;
}

StepSubmitResult SplineSession::onBlankEnter(AppCommandState& st, std::vector<std::string>& log) {
  (void)HandleSplineTextInput("", st, log);
  return st.active == CadCommandKind::None ? StepSubmitResult::Finished : StepSubmitResult::Handled;
}

void SplineSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                   std::vector<std::string>& log) {
  draftPoints.push_back({pick.localX, pick.localY, CadCommitElevation(st)});
  log.push_back("SPLINE — point added. Pick next point, or type Close / End / Undo:");
}

void SplineSession::onCancel(AppCommandState&, std::vector<std::string>& log) {
  log.push_back("SPLINE canceled — nothing was added.");
}

// ============================================================================
// HATCH
// ============================================================================

StepDescriptor HatchSession::currentStep(const AppCommandState&) const {
  return StepDescriptor::Custom("HATCH: Pick an internal point inside a closed area | ESC cancel",
                                StepClickRoute::HatchPick, BlankEnterPolicy::Custom);
}

StepSubmitResult HatchSession::onText(AppCommandState&, const std::string&, std::vector<std::string>&) {
  return StepSubmitResult::Unparsed;
}

StepSubmitResult HatchSession::onBlankEnter(AppCommandState&, std::vector<std::string>&) {
  return StepSubmitResult::Handled;
}

void HatchSession::onViewportPick(AppCommandState&, const StepPickInput&, std::vector<std::string>&) {}

void HatchSession::onCancel(AppCommandState& st, std::vector<std::string>&) {
  st.hatchPreviewValid = false;
  st.hatchPreviewLoop.clear();
}

void HatchSession::onReset(AppCommandState& st) {
  st.hatchPreviewValid = false;
  st.hatchPreviewLoop.clear();
}

// ============================================================================
// TEXT
// ============================================================================

StepDescriptor TextSession::currentStep(const AppCommandState&) const {
  StepDynInputSpec dyn;
  dyn.pointEntry = (phase == TextCmdPhase::WaitInsertion);
  const char* prompt = "";
  switch (phase) {
  case TextCmdPhase::WaitInsertion: prompt = "TEXT: Insertion — click or X,Y | ESC cancel"; break;
  case TextCmdPhase::WaitHeight:    prompt = "TEXT: Height — Enter for plot-scale default | ESC cancel"; break;
  case TextCmdPhase::WaitRotation:  prompt = "TEXT: Rotation ° CW from north — decimal/DMS or Enter=0 | ESC cancel"; break;
  case TextCmdPhase::WaitString:    prompt = "TEXT: Enter content | ESC cancel"; break;
  }
  return StepDescriptor::Custom(prompt, StepClickRoute::SnappedPointPick, BlankEnterPolicy::Custom,
                                false, false, dyn);
}

static StepSubmitResult HandleTextSessionInput(TextSession& sess, AppCommandState& st,
                                               const std::string& line, std::vector<std::string>& log) {
  if (sess.phase == TextCmdPhase::WaitInsertion) {
    float px = 0.f, py = 0.f;
    if (!ParseStoragePoint(st, line, &px, &py, false, 0.f, 0.f)) {
      log.push_back("TEXT — type insertion X,Y or click in viewport.");
      return StepSubmitResult::Handled;
    }
    sess.insX = px;
    sess.insY = py;
    sess.phase = TextCmdPhase::WaitHeight;
    log.push_back("TEXT height — Enter for plot-scale default:");
    return StepSubmitResult::Handled;
  }
  if (sess.phase == TextCmdPhase::WaitHeight) {
    const bool paperText = ActivePaperGeometryTarget(st) != nullptr;
    const std::string tr = StringUtil::trimCopy(line);
    if (tr.empty())
      sess.heightDraft = paperText ? st.drawing.defaultPlottedTextHeightInches : DefaultAnnotationTextHeightWorld(st);
    else if (!ParseSingleFloatTokenLocal(tr, &sess.heightDraft) || !std::isfinite(sess.heightDraft) ||
             sess.heightDraft <= 0.f) {
      log.push_back("TEXT — invalid height.");
      return StepSubmitResult::Handled;
    }
    sess.phase = TextCmdPhase::WaitRotation;
    log.push_back("TEXT rotation ° clockwise from north (decimal/DMS) — Enter for 90 (horizontal):");
    return StepSubmitResult::Handled;
  }
  if (sess.phase == TextCmdPhase::WaitRotation) {
    const std::string tr = StringUtil::trimCopy(line);
    float deg = 90.f;
    if (!tr.empty() && (!ParseAngleDegrees(tr, &deg) || !std::isfinite(deg))) {
      log.push_back("TEXT — could not parse angle.");
      return StepSubmitResult::Handled;
    }
    sess.rotDraft = MathAngleRadFromBearingCwNorthDeg(deg);
    sess.phase = TextCmdPhase::WaitString;
    log.push_back("TEXT — enter content:");
    return StepSubmitResult::Handled;
  }
  // WaitString
  CadAnnotation ann;
  ann.kind = CadAnnotation::Kind::Text;
  ann.insX = sess.insX;
  ann.insY = sess.insY;
  ann.rotationRad = sess.rotDraft;
  ann.text = line;
  if (!ann.text.empty()) {
    const bool paperTarget = ActivePaperGeometryTarget(st) != nullptr;
    const float plottedHeightInches =
        paperTarget ? sess.heightDraft
                    : sess.heightDraft / std::max(st.drawing.modelUnitsPerPlottedInch, 1.e-6f);
    const float insZ = paperTarget ? 0.f : CadCommitElevation(st);
    ann.plottedHeightInches = plottedHeightInches;
    float rbMnX = 0.f, rbMnY = 0.f, rbMxX = 0.f, rbMxY = 0.f;
    CadAnnotationRoughBounds(ann, st.drawing.modelUnitsPerPlottedInch, &rbMnX, &rbMnY, &rbMxX, &rbMxY);
    if (!std::isfinite(ann.insX) || !std::isfinite(ann.insY) || !std::isfinite(insZ) ||
        !std::isfinite(ann.rotationRad) || !std::isfinite(plottedHeightInches) ||
        plottedHeightInches <= 0.f || !std::isfinite(rbMnX) || !std::isfinite(rbMnY) ||
        !std::isfinite(rbMxX) || !std::isfinite(rbMxY)) {
      log.push_back("TEXT rejected - the insertion point, elevation, rotation, or height is not a finite number.");
      EndSession(st);
      return StepSubmitResult::Finished;
    }
    PushUndoSnapshot(st, "Text");
    if (PaperLayout* L = ActivePaperGeometryTarget(st)) {
      L->paperTexts.push_back(std::move(ann));
      L->paperTextAttrs.push_back(MakeNewEntityAttrs(st));
    } else {
      ann.insZ = insZ;
      StampActiveTextStyleOnNewText(st, ann);
      st.drawing.cadAnnotations.push_back(std::move(ann));
      st.drawing.cadAnnotationAttrs.push_back(MakeNewEntityAttrs(st));
    }
    BumpCadGpuCache(st);
    log.push_back("TEXT placed.");
  } else {
    log.push_back("TEXT — empty; canceled.");
  }
  EndSession(st);
  return StepSubmitResult::Finished;
}

StepSubmitResult TextSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                     std::vector<std::string>& log) {
  return HandleTextSessionInput(*this, st, trimmedLine, log);
}

StepSubmitResult TextSession::onBlankEnter(AppCommandState& st, std::vector<std::string>& log) {
  if (phase == TextCmdPhase::WaitInsertion)
    return StepSubmitResult::Handled;
  return HandleTextSessionInput(*this, st, "", log);
}

void TextSession::onViewportPick(AppCommandState&, const StepPickInput& pick,
                                 std::vector<std::string>& log) {
  if (phase == TextCmdPhase::WaitInsertion) {
    insX = static_cast<float>(pick.localX);
    insY = static_cast<float>(pick.localY);
    phase = TextCmdPhase::WaitHeight;
    log.push_back("TEXT — height (Enter = plot-scale default):");
  } else {
    log.push_back("TEXT — continue on command line (height / rotation / text).");
  }
}

void TextSession::onCancel(AppCommandState&, std::vector<std::string>& log) {
  log.push_back("TEXT canceled.");
}

// ============================================================================
// MTEXT
// ============================================================================

StepDescriptor MtextSession::currentStep(const AppCommandState&) const {
  StepDynInputSpec dyn;
  dyn.pointEntry = (phase == MtextPhase::WaitCorner1 || phase == MtextPhase::WaitCorner2);
  const char* prompt = "";
  switch (phase) {
  case MtextPhase::WaitCorner1: prompt = "MTEXT: First corner | ESC cancel"; break;
  case MtextPhase::WaitCorner2: prompt = "MTEXT: Opposite corner | ESC cancel"; break;
  case MtextPhase::WaitString:  prompt = "MTEXT: Edit in drawing box — Ctrl+Enter reformats | Save to place | Esc cancel"; break;
  }
  return StepDescriptor::Custom(prompt, StepClickRoute::SnappedPointPick, BlankEnterPolicy::Custom,
                                false, false, dyn);
}

StepSubmitResult MtextSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                      std::vector<std::string>& log) {
  if (phase == MtextPhase::WaitString) {
    log.push_back("MTEXT — type in the on-screen editor over the box (Ctrl+Enter reformats; Save to place).");
    return StepSubmitResult::Handled;
  }
  float px = 0.f, py = 0.f;
  if (ParseStoragePoint(st, trimmedLine, &px, &py, false, 0.f, 0.f)) {
    StepPickInput p;
    p.localX = px;
    p.localY = py;
    onViewportPick(st, p, log);
    return StepSubmitResult::Handled;
  }
  return StepSubmitResult::Unparsed;
}

StepSubmitResult MtextSession::onBlankEnter(AppCommandState&, std::vector<std::string>&) {
  return StepSubmitResult::Handled;
}

void MtextSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                  std::vector<std::string>& log) {
  const float wx = static_cast<float>(pick.localX);
  const float wy = static_cast<float>(pick.localY);
  switch (phase) {
  case MtextPhase::WaitCorner1:
    x1 = wx;
    y1 = wy;
    phase = MtextPhase::WaitCorner2;
    log.push_back("MTEXT — opposite corner:");
    break;
  case MtextPhase::WaitCorner2:
    x2 = wx;
    y2 = wy;
    phase = MtextPhase::WaitString;
    OpenMtextRichEditorForPlacement(st, &log);
    break;
  case MtextPhase::WaitString:
    break;
  }
}

void MtextSession::onCancel(AppCommandState& st, std::vector<std::string>& log) {
  CloseMtextRichEditorUi(st);
  log.push_back("MTEXT canceled.");
}

void MtextSession::onReset(AppCommandState& st) {
  CloseMtextRichEditorUi(st);
}

// ============================================================================
// MLEADER & MLEADERADDLEADER
// ============================================================================

StepDescriptor MleaderSession::currentStep(const AppCommandState&) const {
  const char* prompt = "";
  switch (phase) {
  case MleaderPhase::WaitArrowTip: prompt = "MLEADER: Arrowhead location | ESC cancel"; break;
  case MleaderPhase::WaitLanding:  prompt = "MLEADER: Landing location (text side) | ESC cancel"; break;
  case MleaderPhase::WaitLabel:    prompt = "MLEADER: Edit label — Save to place | Esc cancel"; break;
  }
  return StepDescriptor::Custom(prompt,
                                phase == MleaderPhase::WaitLabel ? StepClickRoute::Ignore
                                                                 : StepClickRoute::SnappedPointPick,
                                BlankEnterPolicy::Custom);
}

StepSubmitResult MleaderSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                        std::vector<std::string>& log) {
  if (phase == MleaderPhase::WaitLabel)
    return StepSubmitResult::Handled;
  float px = 0.f, py = 0.f;
  if (ParseStoragePoint(st, trimmedLine, &px, &py, false, 0.f, 0.f)) {
    StepPickInput p;
    p.localX = px;
    p.localY = py;
    onViewportPick(st, p, log);
    return StepSubmitResult::Handled;
  }
  return StepSubmitResult::Unparsed;
}

StepSubmitResult MleaderSession::onBlankEnter(AppCommandState&, std::vector<std::string>&) {
  return StepSubmitResult::Handled;
}

void MleaderSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                    std::vector<std::string>& log) {
  const float wx = static_cast<float>(pick.localX);
  const float wy = static_cast<float>(pick.localY);
  switch (phase) {
  case MleaderPhase::WaitArrowTip:
    tipX = wx;
    tipY = wy;
    tipZ = CadCommitElevation(st);
    phase = MleaderPhase::WaitLanding;
    log.push_back("MLEADER — specify landing location (text side):");
    break;
  case MleaderPhase::WaitLanding:
    CommitMleaderLandingAt(st, wx, wy, log);
    break;
  case MleaderPhase::WaitLabel:
    break;
  }
}

void MleaderSession::onCancel(AppCommandState& st, std::vector<std::string>& log) {
  if (st.mtextRichEditorMultileaderJustPlaced)
    AbandonJustPlacedMultileader(st);
  CloseMtextRichEditorUi(st);
  log.push_back("MLEADER canceled.");
}

void MleaderSession::onReset(AppCommandState& st) {
  if (st.mtextRichEditorMultileaderIndex >= 0 && st.mtextRichEditorMultileaderJustPlaced)
    AbandonJustPlacedMultileader(st);
  if (st.mtextRichEditorMultileaderIndex >= 0)
    CloseMtextRichEditorUi(st);
}

StepDescriptor MleaderAddLeaderSession::currentStep(const AppCommandState&) const {
  return StepDescriptor::Custom("Add Leader: Arrowhead location | ESC cancel",
                                StepClickRoute::SnappedPointPick, BlankEnterPolicy::Custom);
}

StepSubmitResult MleaderAddLeaderSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                                 std::vector<std::string>& log) {
  float px = 0.f, py = 0.f;
  if (ParseStoragePoint(st, trimmedLine, &px, &py, false, 0.f, 0.f)) {
    CommitMleaderAddLeaderAt(st, px, py, log);
    return StepSubmitResult::Finished;
  }
  return StepSubmitResult::Unparsed;
}

StepSubmitResult MleaderAddLeaderSession::onBlankEnter(AppCommandState&, std::vector<std::string>&) {
  return StepSubmitResult::Handled;
}

void MleaderAddLeaderSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                             std::vector<std::string>& log) {
  CommitMleaderAddLeaderAt(st, static_cast<float>(pick.localX), static_cast<float>(pick.localY), log);
}

void MleaderAddLeaderSession::onCancel(AppCommandState&, std::vector<std::string>& log) {
  log.push_back("Add Leader canceled.");
}

// ============================================================================
// DIMALIGNED / DIMLINEAR
// ============================================================================

StepDescriptor DimSession::currentStep(const AppCommandState&) const {
  StepDynInputSpec dyn;
  dyn.pointEntry = true;
  const char* prompt = "";
  switch (phase) {
  case DimPhase::WaitExt1:
    prompt = linear ? "DIMLINEAR: Extension 1 | ESC cancel" : "DIMALIGNED: Extension 1 | ESC cancel";
    break;
  case DimPhase::WaitExt2:
    prompt = linear ? "DIMLINEAR: Extension 2 | ESC cancel" : "DIMALIGNED: Extension 2 | ESC cancel";
    break;
  case DimPhase::WaitDimLinePt:
    prompt = linear
                 ? "DIMLINEAR: Line — dominant X vs Y from chord mid; [H] / [V] keys; X,Y | @ from chord mid | ESC"
                 : "DIMALIGNED: Offset — click | X,Y | @ from chord mid | ESC";
    break;
  }
  return StepDescriptor::Point(prompt, dyn, BlankEnterPolicy::Custom);
}

StepSubmitResult DimSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                    std::vector<std::string>& log) {
  const std::string dimLow = StringUtil::toLowerAsciiCopy(trimmedLine);
  if (linear && phase == DimPhase::WaitDimLinePt && (dimLow == "h" || dimLow == "v")) {
    CadDimLinearApplyHVHotkey(st, dimLow == "v", log);
    return StepSubmitResult::Handled;
  }
  float px = 0.f, py = 0.f;
  bool allowRel = false;
  float ax = 0.f, ay = 0.f;
  if (phase == DimPhase::WaitExt2) {
    allowRel = true;
    ax = e1x;
    ay = e1y;
  } else if (phase == DimPhase::WaitDimLinePt) {
    allowRel = true;
    ax = 0.5f * (e1x + e2x);
    ay = 0.5f * (e1y + e2y);
  }
  if (!ParseStoragePoint(st, trimmedLine, &px, &py, allowRel, ax, ay)) {
    log.push_back(linear ? "DIMLINEAR — X,Y or @dx,dy from chord mid; H / V locks orientation."
                         : "DIMALIGNED — X,Y or @dx,dy from chord mid.");
    return StepSubmitResult::Handled;
  }
  StepPickInput p;
  p.localX = px;
  p.localY = py;
  onViewportPick(st, p, log);
  return StepSubmitResult::Handled;
}

StepSubmitResult DimSession::onBlankEnter(AppCommandState& st, std::vector<std::string>& log) {
  if (phase == DimPhase::WaitDimLinePt) {
    float lx = 0.f, ly = 0.f;
    CadCoord::LocalFromWorld(st, static_cast<double>(st.uiCursorWorldX),
                             static_cast<double>(st.uiCursorWorldY), &lx, &ly);
    if (linear)
      CommitDimLinearAt(st, lx, ly, log);
    else
      CommitDimAlignedAt(st, lx, ly, log);
  }
  return StepSubmitResult::Handled;
}

void DimSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                std::vector<std::string>& log) {
  const float wx = static_cast<float>(pick.localX);
  const float wy = static_cast<float>(pick.localY);
  switch (phase) {
  case DimPhase::WaitExt1:
    e1x = wx;
    e1y = wy;
    phase = DimPhase::WaitExt2;
    log.push_back(std::string(linear ? "DIMLINEAR" : "DIMALIGNED") + " — second extension point:");
    break;
  case DimPhase::WaitExt2:
    e2x = wx;
    e2y = wy;
    phase = DimPhase::WaitDimLinePt;
    if (linear) {
      linearOrientUserLock = false;
      CadDimLinearUpdateDraftOrientation(st, wx, wy);
      log.push_back("DIMLINEAR — pick dimension line position (horizontal vs vertical follows cursor; H / V to lock); type X,Y or @dx,dy from chord mid.");
    } else {
      log.push_back("DIMALIGNED — pick dimension line position (offset from measured segment).");
    }
    break;
  case DimPhase::WaitDimLinePt:
    if (linear)
      CommitDimLinearAt(st, wx, wy, log);
    else
      CommitDimAlignedAt(st, wx, wy, log);
    break;
  }
}

void DimSession::onCancel(AppCommandState&, std::vector<std::string>& log) {
  log.push_back(linear ? "DIMLINEAR canceled." : "DIMALIGNED canceled.");
}

// ============================================================================
// DIMANGULAR
// ============================================================================

StepDescriptor DimAngularSession::currentStep(const AppCommandState&) const {
  StepDynInputSpec dyn;
  dyn.pointEntry = (phase == DimAngularPhase::WaitVertex || phase == DimAngularPhase::WaitRay1 ||
                    phase == DimAngularPhase::WaitRay2);
  const char* prompt = "";
  switch (phase) {
  case DimAngularPhase::WaitVertex: prompt = "DIMANGULAR: Vertex | X,Y | ESC cancel"; break;
  case DimAngularPhase::WaitRay1:   prompt = "DIMANGULAR: First ray point | X,Y | @ from vertex | ESC"; break;
  case DimAngularPhase::WaitRay2:   prompt = "DIMANGULAR: Second ray point | X,Y | @ from vertex | ESC"; break;
  case DimAngularPhase::WaitArc:    prompt = "DIMANGULAR: Arc radius (bisector) | click | X,Y | @ from vertex | ESC"; break;
  }
  return StepDescriptor::Custom(prompt, StepClickRoute::SnappedPointPick, BlankEnterPolicy::Custom,
                                false, false, dyn);
}

StepSubmitResult DimAngularSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                           std::vector<std::string>& log) {
  float px = 0.f, py = 0.f;
  const bool allowRel = (phase != DimAngularPhase::WaitVertex);
  if (!ParseStoragePoint(st, trimmedLine, &px, &py, allowRel, vx, vy)) {
    log.push_back("DIMANGULAR — X,Y or @dx,dy from vertex.");
    return StepSubmitResult::Handled;
  }
  StepPickInput p;
  p.localX = px;
  p.localY = py;
  onViewportPick(st, p, log);
  return StepSubmitResult::Handled;
}

StepSubmitResult DimAngularSession::onBlankEnter(AppCommandState&, std::vector<std::string>&) {
  return StepSubmitResult::Handled;
}

void DimAngularSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                       std::vector<std::string>& log) {
  const float wx = static_cast<float>(pick.localX);
  const float wy = static_cast<float>(pick.localY);
  switch (phase) {
  case DimAngularPhase::WaitVertex:
    vx = wx;
    vy = wy;
    phase = DimAngularPhase::WaitRay1;
    log.push_back("DIMANGULAR — first ray point (on first leg):");
    break;
  case DimAngularPhase::WaitRay1:
    e1x = wx;
    e1y = wy;
    phase = DimAngularPhase::WaitRay2;
    log.push_back("DIMANGULAR — second ray point (on second leg):");
    break;
  case DimAngularPhase::WaitRay2:
    e2x = wx;
    e2y = wy;
    phase = DimAngularPhase::WaitArc;
    log.push_back("DIMANGULAR — pick arc / label side (radius along angle bisector); type X,Y or @dx,dy from vertex.");
    break;
  case DimAngularPhase::WaitArc:
    CommitDimAngularAt(st, wx, wy, log);
    break;
  }
}

void DimAngularSession::onCancel(AppCommandState&, std::vector<std::string>& log) {
  log.push_back("DIMANGULAR canceled.");
}

// ============================================================================
// ID / INVERSE / DIST
// ============================================================================

StepDescriptor IdPointSession::currentStep(const AppCommandState&) const {
  StepDynInputSpec dyn;
  dyn.pointEntry = true;
  return StepDescriptor::Point("ID: Pick point (OSNAP when enabled) or type X,Y — logs UCS World | ESC cancel",
                               dyn, BlankEnterPolicy::Custom);
}

StepSubmitResult IdPointSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                        std::vector<std::string>& log) {
  float px = 0.f, py = 0.f;
  if (!ParseStoragePoint(st, trimmedLine, &px, &py, false, 0.f, 0.f)) {
    log.push_back("ID — pick in viewport or type X,Y (model units, UCS World).");
    return StepSubmitResult::Handled;
  }
  CommitIdPointAt(st, px, py, log);
  return StepSubmitResult::Finished;
}

StepSubmitResult IdPointSession::onBlankEnter(AppCommandState&, std::vector<std::string>&) {
  return StepSubmitResult::Handled;
}

void IdPointSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                    std::vector<std::string>& log) {
  CommitIdPointAt(st, static_cast<float>(pick.localX), static_cast<float>(pick.localY), log);
}

void IdPointSession::onCancel(AppCommandState&, std::vector<std::string>& log) {
  log.push_back("ID canceled.");
}

StepDescriptor SurveyInverseSession::currentStep(const AppCommandState&) const {
  StepDynInputSpec dyn;
  dyn.pointEntry = true;
  const char* prompt = (phase == SurveyInversePhase::WaitFrom)
                           ? "INVERSE: First point — pick or X,Y (Easting, Northing) | ESC cancel"
                           : "INVERSE: Second point — pick or X,Y / @ from first | ESC cancel";
  return StepDescriptor::Point(prompt, dyn, BlankEnterPolicy::Custom);
}

StepSubmitResult SurveyInverseSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                              std::vector<std::string>& log) {
  float px = 0.f, py = 0.f;
  if (phase == SurveyInversePhase::WaitFrom) {
    if (!ParseStoragePoint(st, trimmedLine, &px, &py, false, 0.f, 0.f)) {
      log.push_back("INVERSE — type X,Y (World) or pick first point in Drawing1.");
      return StepSubmitResult::Handled;
    }
    fromX = px;
    fromY = py;
    phase = SurveyInversePhase::WaitTo;
    log.push_back("INVERSE — second point (X,Y or @dx,dy from first).");
    return StepSubmitResult::Handled;
  }
  if (!ParseStoragePoint(st, trimmedLine, &px, &py, true, fromX, fromY)) {
    log.push_back("INVERSE — type X,Y or @dx,dy from first point.");
    return StepSubmitResult::Handled;
  }
  CommitSurveyInverseSecondPoint(st, px, py, log);
  return st.active == CadCommandKind::None ? StepSubmitResult::Finished : StepSubmitResult::Handled;
}

StepSubmitResult SurveyInverseSession::onBlankEnter(AppCommandState&, std::vector<std::string>&) {
  return StepSubmitResult::Handled;
}

void SurveyInverseSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                          std::vector<std::string>& log) {
  const float wx = static_cast<float>(pick.localX);
  const float wy = static_cast<float>(pick.localY);
  if (phase == SurveyInversePhase::WaitFrom) {
    fromX = wx;
    fromY = wy;
    phase = SurveyInversePhase::WaitTo;
    log.push_back("INVERSE — second point (pick or type X,Y; @dx,dy from first):");
    return;
  }
  CommitSurveyInverseSecondPoint(st, wx, wy, log);
}

void SurveyInverseSession::onCancel(AppCommandState&, std::vector<std::string>& log) {
  log.push_back("INVERSE canceled.");
}

StepDescriptor DistSession::currentStep(const AppCommandState&) const {
  StepDynInputSpec dyn;
  dyn.pointEntry = true;
  const char* prompt = (phase == DistPhase::WaitFrom)
                           ? "DIST: First point — pick or X,Y | ESC cancel"
                           : "DIST: Second point — pick or X,Y / @ from first | ESC cancel";
  return StepDescriptor::Point(prompt, dyn, BlankEnterPolicy::Custom);
}

StepSubmitResult DistSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                     std::vector<std::string>& log) {
  float px = 0.f, py = 0.f;
  if (phase == DistPhase::WaitFrom) {
    if (!ParseStoragePoint(st, trimmedLine, &px, &py, false, 0.f, 0.f)) {
      log.push_back("DIST — type X,Y (World) or pick first point in Drawing1.");
      return StepSubmitResult::Handled;
    }
    fromX = px;
    fromY = py;
    fromZ = CadCommitElevation(st);
    phase = DistPhase::WaitTo;
    log.push_back("DIST — second point (X,Y or @dx,dy from first).");
    return StepSubmitResult::Handled;
  }
  if (!ParseStoragePoint(st, trimmedLine, &px, &py, true, fromX, fromY)) {
    log.push_back("DIST — type X,Y or @dx,dy from first point.");
    return StepSubmitResult::Handled;
  }
  CommitDistSecondPoint(st, fromX, fromY, fromZ, px, py, CadCommitElevation(st), log);
  return st.active == CadCommandKind::None ? StepSubmitResult::Finished : StepSubmitResult::Handled;
}

StepSubmitResult DistSession::onBlankEnter(AppCommandState&, std::vector<std::string>&) {
  return StepSubmitResult::Handled;
}

void DistSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                 std::vector<std::string>& log) {
  const float wx = static_cast<float>(pick.localX);
  const float wy = static_cast<float>(pick.localY);
  if (phase == DistPhase::WaitFrom) {
    fromX = wx;
    fromY = wy;
    fromZ = CadCommitElevation(st);
    phase = DistPhase::WaitTo;
    log.push_back("DIST — second point (pick or type X,Y; @dx,dy from first):");
    return;
  }
  CommitDistSecondPoint(st, fromX, fromY, fromZ, wx, wy, CadCommitElevation(st), log);
}

void DistSession::onCancel(AppCommandState&, std::vector<std::string>& log) {
  log.push_back("DIST canceled.");
}

// ============================================================================
// DRAWINQUIRY: POINT, LEADER, PEDIT, MEASURE, DIVIDE, AREA, LIST, DATAEXTRACT, TABLE, POINTTABLE
// ============================================================================

StepDescriptor PointSession::currentStep(const AppCommandState& st) const {
  const char* h = CadDrawInquiryCommandInputHint(st);
  return StepDescriptor::Custom(h ? h : "", StepClickRoute::SnappedPointPick, BlankEnterPolicy::Custom);
}

StepSubmitResult PointSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                      std::vector<std::string>& log) {
  (void)HandleCadDrawInquiryTextInput(trimmedLine, st, log);
  return st.active == CadCommandKind::None ? StepSubmitResult::Finished : StepSubmitResult::Handled;
}

StepSubmitResult PointSession::onBlankEnter(AppCommandState& st, std::vector<std::string>& log) {
  (void)HandleCadDrawInquiryTextInput("", st, log);
  return st.active == CadCommandKind::None ? StepSubmitResult::Finished : StepSubmitResult::Handled;
}

void PointSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                  std::vector<std::string>& log) {
  (void)SubmitCadDrawInquiryViewportPick(st, pick.localX, pick.localY, log);
}

void PointSession::onCancel(AppCommandState&, std::vector<std::string>& log) {
  log.push_back("POINT canceled.");
}

StepDescriptor LeaderSession::currentStep(const AppCommandState& st) const {
  const char* h = CadDrawInquiryCommandInputHint(st);
  const StepClickRoute route =
      (phase == LeaderPhase::WaitAnnotation || phase == LeaderPhase::WaitFormat)
          ? StepClickRoute::Ignore
          : StepClickRoute::SnappedPointPick;
  return StepDescriptor::Custom(h ? h : "", route, BlankEnterPolicy::Custom);
}

StepSubmitResult LeaderSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                       std::vector<std::string>& log) {
  (void)HandleCadDrawInquiryTextInput(trimmedLine, st, log);
  return st.active == CadCommandKind::None ? StepSubmitResult::Finished : StepSubmitResult::Handled;
}

StepSubmitResult LeaderSession::onBlankEnter(AppCommandState& st, std::vector<std::string>& log) {
  (void)HandleCadDrawInquiryTextInput("", st, log);
  return st.active == CadCommandKind::None ? StepSubmitResult::Finished : StepSubmitResult::Handled;
}

void LeaderSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                   std::vector<std::string>& log) {
  (void)SubmitCadDrawInquiryViewportPick(st, pick.localX, pick.localY, log);
}

void LeaderSession::onCancel(AppCommandState&, std::vector<std::string>& log) {
  log.push_back("LEADER canceled — nothing was added.");
}

StepDescriptor PeditSession::currentStep(const AppCommandState& st) const {
  const char* h = CadDrawInquiryCommandInputHint(st);
  StepClickRoute route = StepClickRoute::Ignore;
  if (phase == PeditPhase::WaitObject || phase == PeditPhase::WaitJoin)
    route = StepClickRoute::RawEntityPick;
  else if (phase == PeditPhase::WaitVertexMove || phase == PeditPhase::WaitVertexInsert)
    route = StepClickRoute::SnappedPointPick;
  return StepDescriptor::Custom(h ? h : "", route, BlankEnterPolicy::Custom);
}

StepSubmitResult PeditSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                      std::vector<std::string>& log) {
  (void)HandleCadDrawInquiryTextInput(trimmedLine, st, log);
  return st.active == CadCommandKind::None ? StepSubmitResult::Finished : StepSubmitResult::Handled;
}

StepSubmitResult PeditSession::onBlankEnter(AppCommandState& st, std::vector<std::string>& log) {
  (void)HandleCadDrawInquiryTextInput("", st, log);
  return st.active == CadCommandKind::None ? StepSubmitResult::Finished : StepSubmitResult::Handled;
}

void PeditSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                  std::vector<std::string>& log) {
  (void)SubmitCadDrawInquiryViewportPick(st, pick.localX, pick.localY, log);
}

void PeditSession::onCancel(AppCommandState&, std::vector<std::string>& log) {
  log.push_back("PEDIT canceled.");
}

StepDescriptor MeasureDivideSession::currentStep(const AppCommandState& st) const {
  const char* h = CadDrawInquiryCommandInputHint(st);
  const StepClickRoute route =
      (phase == MeasurePhase::WaitObject) ? StepClickRoute::RawEntityPick : StepClickRoute::Ignore;
  return StepDescriptor::Custom(h ? h : "", route, BlankEnterPolicy::Custom);
}

StepSubmitResult MeasureDivideSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                              std::vector<std::string>& log) {
  (void)HandleCadDrawInquiryTextInput(trimmedLine, st, log);
  return st.active == CadCommandKind::None ? StepSubmitResult::Finished : StepSubmitResult::Handled;
}

StepSubmitResult MeasureDivideSession::onBlankEnter(AppCommandState& st, std::vector<std::string>& log) {
  (void)HandleCadDrawInquiryTextInput("", st, log);
  return st.active == CadCommandKind::None ? StepSubmitResult::Finished : StepSubmitResult::Handled;
}

void MeasureDivideSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                          std::vector<std::string>& log) {
  (void)SubmitCadDrawInquiryViewportPick(st, pick.localX, pick.localY, log);
}

void MeasureDivideSession::onCancel(AppCommandState&, std::vector<std::string>& log) {
  log.push_back(divide ? "DIVIDE canceled." : "MEASURE canceled.");
}

StepDescriptor AreaSession::currentStep(const AppCommandState& st) const {
  const char* h = CadDrawInquiryCommandInputHint(st);
  const StepClickRoute route =
      (phase == AreaPhase::WaitObject) ? StepClickRoute::RawEntityPick : StepClickRoute::SnappedPointPick;
  return StepDescriptor::Custom(h ? h : "", route, BlankEnterPolicy::Custom);
}

StepSubmitResult AreaSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                     std::vector<std::string>& log) {
  (void)HandleCadDrawInquiryTextInput(trimmedLine, st, log);
  return st.active == CadCommandKind::None ? StepSubmitResult::Finished : StepSubmitResult::Handled;
}

StepSubmitResult AreaSession::onBlankEnter(AppCommandState& st, std::vector<std::string>& log) {
  (void)HandleCadDrawInquiryTextInput("", st, log);
  return st.active == CadCommandKind::None ? StepSubmitResult::Finished : StepSubmitResult::Handled;
}

void AreaSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                 std::vector<std::string>& log) {
  (void)SubmitCadDrawInquiryViewportPick(st, pick.localX, pick.localY, log);
}

void AreaSession::onCancel(AppCommandState&, std::vector<std::string>& log) {
  log.push_back("AREA canceled.");
}

StepDescriptor ListSession::currentStep(const AppCommandState& st) const {
  const char* h = CadDrawInquiryCommandInputHint(st);
  return StepDescriptor::Custom(h ? h : "", StepClickRoute::RawEntityPick, BlankEnterPolicy::Custom);
}

StepSubmitResult ListSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                     std::vector<std::string>& log) {
  (void)HandleCadDrawInquiryTextInput(trimmedLine, st, log);
  return st.active == CadCommandKind::None ? StepSubmitResult::Finished : StepSubmitResult::Handled;
}

StepSubmitResult ListSession::onBlankEnter(AppCommandState& st, std::vector<std::string>& log) {
  (void)HandleCadDrawInquiryTextInput("", st, log);
  return st.active == CadCommandKind::None ? StepSubmitResult::Finished : StepSubmitResult::Handled;
}

void ListSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                 std::vector<std::string>& log) {
  (void)SubmitCadDrawInquiryViewportPick(st, pick.localX, pick.localY, log);
}

void ListSession::onCancel(AppCommandState&, std::vector<std::string>& log) {
  log.push_back("LIST canceled.");
}

StepDescriptor DataExtractSession::currentStep(const AppCommandState&) const {
  return StepDescriptor::Custom("", StepClickRoute::Ignore, BlankEnterPolicy::Custom);
}

StepSubmitResult DataExtractSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                            std::vector<std::string>& log) {
  (void)HandleCadDrawInquiryTextInput(trimmedLine, st, log);
  return st.active == CadCommandKind::None ? StepSubmitResult::Finished : StepSubmitResult::Handled;
}

StepSubmitResult DataExtractSession::onBlankEnter(AppCommandState& st, std::vector<std::string>& log) {
  (void)HandleCadDrawInquiryTextInput("", st, log);
  return st.active == CadCommandKind::None ? StepSubmitResult::Finished : StepSubmitResult::Handled;
}

void DataExtractSession::onViewportPick(AppCommandState&, const StepPickInput&, std::vector<std::string>&) {}

void DataExtractSession::onCancel(AppCommandState&, std::vector<std::string>& log) {
  log.push_back("DATAEXTRACTION canceled.");
}

StepDescriptor TableSession::currentStep(const AppCommandState&) const {
  return StepDescriptor::Custom("", stage == 3 ? StepClickRoute::SnappedPointPick : StepClickRoute::Ignore,
                                BlankEnterPolicy::Custom);
}

StepSubmitResult TableSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                      std::vector<std::string>& log) {
  (void)HandleCadDrawInquiryTextInput(trimmedLine, st, log);
  return st.active == CadCommandKind::None ? StepSubmitResult::Finished : StepSubmitResult::Handled;
}

StepSubmitResult TableSession::onBlankEnter(AppCommandState& st, std::vector<std::string>& log) {
  (void)HandleCadDrawInquiryTextInput("", st, log);
  return st.active == CadCommandKind::None ? StepSubmitResult::Finished : StepSubmitResult::Handled;
}

void TableSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                  std::vector<std::string>& log) {
  (void)SubmitCadDrawInquiryViewportPick(st, pick.localX, pick.localY, log);
}

void TableSession::onCancel(AppCommandState&, std::vector<std::string>& log) {
  log.push_back("TABLE canceled.");
}

StepDescriptor PointTableSession::currentStep(const AppCommandState&) const {
  return StepDescriptor::Custom("", stage == 2 ? StepClickRoute::SnappedPointPick : StepClickRoute::Ignore,
                                BlankEnterPolicy::Custom);
}

StepSubmitResult PointTableSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                           std::vector<std::string>& log) {
  (void)HandleCadDrawInquiryTextInput(trimmedLine, st, log);
  return st.active == CadCommandKind::None ? StepSubmitResult::Finished : StepSubmitResult::Handled;
}

StepSubmitResult PointTableSession::onBlankEnter(AppCommandState& st, std::vector<std::string>& log) {
  (void)HandleCadDrawInquiryTextInput("", st, log);
  return st.active == CadCommandKind::None ? StepSubmitResult::Finished : StepSubmitResult::Handled;
}

void PointTableSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                       std::vector<std::string>& log) {
  (void)SubmitCadDrawInquiryViewportPick(st, pick.localX, pick.localY, log);
}

void PointTableSession::onCancel(AppCommandState&, std::vector<std::string>& log) {
  log.push_back("ADDPOINTTABLE canceled.");
}

// ============================================================================
// Registration
// ============================================================================

void RegisterDrawCommands(CommandRegistry& reg) {
  reg.Add({"line", "l", "Draw line segments", CadCommandKind::Line, true, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartLineCommand(st, log);
           }});
  reg.Add({"centermark", "", "Add a center mark to the selected circles and arcs", CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             ExecuteCenterMarkFromSelection(st, log);
           }});
  reg.Add({"centerline", "", "Draw a centerline between the two selected lines", CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             ExecuteCenterlineFromSelection(st, log);
           }});
  reg.Add({"circle", "c", "Draw a circle", CadCommandKind::Circle, true, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartCircleCommand(st, log);
           }});
  reg.Add({"polyline", "pl", "Draw a connected polyline", CadCommandKind::Polyline, true, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartPolylineCommand(st, log);
           }});
  reg.Add({"3dpoly", "3dp, 3dpolyline", "Draw a polyline whose vertices each carry their own elevation",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartPolyline3dCommand(st, log);
           }});
  reg.Add({"rect", "rectang, rectangle", "Draw a rectangle (two opposite corners)", CadCommandKind::Rect, true, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartRectCommand(st, log);
           }});
  reg.Add({"arc", "", "Draw an arc", CadCommandKind::Arc, true, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartArcCommand(st, log);
           }});
  reg.Add({"ellipse", "el", "Draw an ellipse", CadCommandKind::Ellipse, true, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartEllipseCommand(st, log);
           }});
  reg.Add({"spline", "spl", "Draw a fit-point or control-vertex spline (CV, Close, End, Undo)",
           CadCommandKind::Spline, true, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartSplineCommand(st, log);
           }});
  reg.Add({"point", "", "Place CAD point entities (PDMODE / PDSIZE control the marker)", CadCommandKind::Point, true, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartPointCommand(st, log);
           }});
  reg.Add({"pdmode", "", "Point display mode: 0 dot, 1 none, 2 plus, 3 X, 4 tick; +32 circle, +64 square",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             ExecutePdModeCommand(st, args, log);
           }});
  reg.Add({"pdsize", "", "Point display size: 0 = 5% of view, negative = percent, positive = drawing units",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             ExecutePdSizeCommand(st, args, log);
           }});
  reg.Add({"leader", "le", "Draw a leader: points, then Mtext or None annotation", CadCommandKind::Leader, true, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartLeaderCommand(st, log);
           }});
  reg.Add({"mleader", "ml", "Draw a multileader: arrow tip, landing, then label", CadCommandKind::Mleader, true, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartMleaderCommand(st, log);
           }});
  reg.Add({"pedit", "pe", "Edit a polyline (Close, Open, Join, Reverse, Edit vertex, Fit, Spline, Decurve)",
           CadCommandKind::Pedit, true, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartPeditCommand(st, log);
           }});
  reg.Add({"measure", "me", "Place points at a measured interval along an object", CadCommandKind::Measure, true, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartMeasureCommand(st, log);
           }});
  reg.Add({"divide", "div", "Place points that divide an object into equal segments", CadCommandKind::Divide, true, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartDivideCommand(st, log);
           }});
  reg.Add({"area", "aa", "Measure the area and perimeter of points or a closed object", CadCommandKind::Area, true, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartAreaCommand(st, log);
           }});
  reg.Add({"list", "li", "List type, layer, space and geometry of selected objects", CadCommandKind::List, true, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartListCommand(st, log);
           }});
  reg.Add({"dataextraction", "", "Extract selected objects to an Excel or CSV file: type, layer, text, coordinates",
           CadCommandKind::DataExtract, true, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartDataExtractCommand(st, log);
           }});
  reg.Add({"table", "", "Insert an empty table: style, columns, rows, then the insertion point",
           CadCommandKind::Table, true, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartTableCommand(st, log);
           }});
  reg.Add({"addpointtable", "", "Add a point table of survey points in a point table style (dialog in the editor)",
           CadCommandKind::PointTable, true, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartAddPointTableCommand(st, log);
           }});
  reg.Add({"hatch", "h, bhatch", "Fill a closed area (pick an internal point)", CadCommandKind::Hatch, true, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartHatchCommand(st, log);
           }});
  reg.Add({"text", "", "Place single-line text", CadCommandKind::Text, true, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartTextCommand(st, log);
           }});
  reg.Add({"mtext", "mt", "Place multiline text", CadCommandKind::Mtext, true, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartMtextCommand(st, log);
           }});
  reg.Add({"dimaligned", "dal", "Aligned dimension", CadCommandKind::DimAligned, true, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartDimAlignedCommand(st, log);
           }});
  reg.Add({"dimlinear", "dli", "Linear dimension", CadCommandKind::DimLinear, true, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartDimLinearCommand(st, log);
           }});
  reg.Add({"dimangular", "dan", "Angular dimension", CadCommandKind::DimAngular, true, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartDimAngularCommand(st, log);
           }});
  reg.Add({"dimsty", "dimstyle, dsty", "Dimension style editor", CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartDimStyleCommand(st, log);
           }});
  reg.Add({"tablestyle", "tsty", "Drawing table style manager", CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             st.showTableStyleDialog = true;
             log.push_back("TABLESTYLE — table style manager opened.");
           }});
  reg.Add({"msty", "mleaderstyle, mlsty", "Multileader style editor", CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartMleaderStyleCommand(st, log);
           }});
  reg.Add({"id", "", "Identify point coordinates", CadCommandKind::IdPoint, false, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartIdPointCommand(st, log);
           }});
  reg.Add({"inverse", "inv", "Inverse between two points", CadCommandKind::SurveyInverse, false, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartSurveyInverseCommand(st, log);
           }});
  reg.Add({"dist", "di", "3D distance between two points: dX, dY, dZ, slope distance", CadCommandKind::Dist, false, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartDistCommand(st, log);
           }});
}

} // namespace stepengine
