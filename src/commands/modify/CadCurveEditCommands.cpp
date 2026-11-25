#include "CadCommandsInternal.hpp"
#include "modify/CadModifyInternal.hpp"
#include "modify/CadModifySessions.hpp"
#include "engine/StepEngine.hpp"
#include "modify/CadCurveEditInternal.hpp"
#include "CadBlocks.hpp"
#include "CadCoordinateFrame.hpp"
#include <GoSurvey/Geometry/curveintersect.hpp>
#include <GoSurvey/Geometry/geom2d.hpp>
#include <GoSurvey/Geometry/spline.hpp>
#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// ============================================================================================
// LENGTHEN (REQ-103 step 2). Written for reuse by EXTEND/FILLET (steps 3/6) per REQ-103's own
// stated sequencing — the length/endpoint math below is free functions, not folded into the
// command handler. Shaped like OFFSET above (single entity per pick, loop back to "select
// object" until Enter/Esc) rather than like MIRROR/ROTATE/SCALE (selection-then-transform).
// ============================================================================================

/// Arc length along its sweep. Nothing in this codebase computed this before LENGTHEN — confirmed
/// by search, not assumed — not even for display in the Properties panel.

/// with a stated reason (REQ-201) — mirrors `CommitOffsetSigned`'s `switch(e.type)` shape.
static bool ApplyLengthenToEntity(AppCommandState& st, const SelectedEntity& e, bool nearFirst,
                                  float newLength, std::vector<std::string>& log) {
  switch (e.type) {
  case SelectedEntity::Type::LineSeg:
    return ApplyLengthenToLine(st, e.index, nearFirst, newLength, log);
  case SelectedEntity::Type::Arc:
    return ApplyLengthenToArc(st, e.index, nearFirst, newLength, log);
  case SelectedEntity::Type::Polyline:
    return ApplyLengthenToPolylineEnd(st, e.index, nearFirst, newLength, log);
  default:
    return false;
  }
}

/// Checks whether `e` is a LENGTHEN-eligible entity and, if so, its current length and which end
/// (`outNearFirst`) is nearest (pickX,pickY). Refuses — with a stated reason, never silently — a
/// Circle, Ellipse, closed Polyline, full-circle Arc, or any non-length-bearing entity kind.
/// Shared by LENGTHEN and EXTEND (TASK-096) — both need "is this a length-bearing entity, and
/// which end is nearest the pick" for the identical eligible set (Line, open Polyline,
/// non-full-circle Arc). \p cmdName names the caller in every refusal message (REQ-201) so EXTEND
/// doesn't log a misleading "LENGTHEN —" reason while it's the active command.
static bool LengthenEligibility(const AppCommandState& st, const SelectedEntity& e, float pickX, float pickY,
                                bool* outNearFirst, float* outCurLen, const char* cmdName,
                                std::vector<std::string>& log) {
  constexpr float kTwoPi = 6.28318530717958647692f;
  switch (e.type) {
  case SelectedEntity::Type::LineSeg: {
    const size_t k = static_cast<size_t>(e.index);
    if (k >= st.drawing.lineCount())
      return false;
    const float x0 = static_cast<float>(st.drawing.line(k).x0), y0 = static_cast<float>(st.drawing.line(k).y0);
    const float x1 = static_cast<float>(st.drawing.line(k).x1), y1 = static_cast<float>(st.drawing.line(k).y1);
    *outNearFirst = NearerToFirstPoint(pickX, pickY, x0, y0, x1, y1);
    *outCurLen = std::hypot(x1 - x0, y1 - y0);
    return true;
  }
  case SelectedEntity::Type::Arc: {
    if (e.index < 0 || static_cast<size_t>(e.index) >= st.drawing.userArcs.size())
      return false;
    const CadArc& a = st.drawing.userArcs[static_cast<size_t>(e.index)];
    if (std::fabs(std::fabs(a.sweepRad) - kTwoPi) < 1e-4f) {
      log.push_back(std::string(cmdName) + " — 1 full-circle arc ignored: a full circle has no end to lengthen from.");
      return false;
    }
    const float sx = static_cast<float>(a.cx + a.r * std::cos(a.startRad));
    const float sy = static_cast<float>(a.cy + a.r * std::sin(a.startRad));
    const float ex = static_cast<float>(a.cx + a.r * std::cos(a.startRad + a.sweepRad));
    const float ey = static_cast<float>(a.cy + a.r * std::sin(a.startRad + a.sweepRad));
    *outNearFirst = NearerToFirstPoint(pickX, pickY, sx, sy, ex, ey);
    *outCurLen = ArcLengthOf(static_cast<float>(a.r), a.sweepRad);
    return true;
  }
  case SelectedEntity::Type::Polyline: {
    const int pi = e.index;
    if (pi < 0 || (static_cast<size_t>(pi)+ 1) >= st.drawing.userPolylineOffsets.size())
      return false;
    if (static_cast<size_t>(pi) < st.drawing.userPolylineClosed.size() && st.drawing.userPolylineClosed[static_cast<size_t>(pi)]) {
      log.push_back(std::string(cmdName) + " — 1 closed polyline ignored: a closed polyline has no end to lengthen from.");
      return false;
    }
    const int v0 = st.drawing.userPolylineOffsets[static_cast<size_t>(pi)];
    const int v1 = st.drawing.userPolylineOffsets[(static_cast<size_t>(pi)+ 1)];
    if (v1 - v0 < 2)
      return false;
    const size_t a0 = static_cast<size_t>(v0), a1 = (static_cast<size_t>(v1)- 1);
    if (a1 >= st.drawing.vertexCount())
      return false;
    *outNearFirst = NearerToFirstPoint(pickX, pickY, static_cast<float>(st.drawing.vertex(a0).x), static_cast<float>(st.drawing.vertex(a0).y),
                                       static_cast<float>(st.drawing.vertex(a1).x), static_cast<float>(st.drawing.vertex(a1).y));
    *outCurLen = PolylineOpenLengthOf(st, pi);
    return true;
  }
  case SelectedEntity::Type::Circle:
    log.push_back(std::string(cmdName) + " — 1 circle ignored: a circle has no end to lengthen from. Pick a line, "
                  "open polyline, or arc.");
    return false;
  case SelectedEntity::Type::Spline:
    log.push_back(std::string(cmdName) + " — splines are not supported; nothing changed.");
    return false;
  case SelectedEntity::Type::Ellipse:
    log.push_back(std::string(cmdName) + " — 1 ellipse ignored: every ellipse in this drawing is a full closed "
                  "curve with no end. Pick a line, open polyline, or arc.");
    return false;
  default:
    log.push_back(std::string(cmdName) + " — 1 object ignored: only a line, open polyline, or arc can be "
                  "lengthened.");
    return false;
  }
}

// ============================================================================================
// EXTEND (REQ-103 step 3). Boundary intersection is analytic (curveisect, REQ-062's already-
// shipped library), not tessellated like TRIM's own cutting-edge math — REQ-062's own acceptance
// text already settled that "a tessellated approximation does not satisfy REQ-101 and is not
// acceptable" (D-2026-08-24-b). The actual geometry mutation is 100% reused from
// ApplyLengthenToLine/ToArc/ToPolylineEnd — EXTEND's only new code is finding the nearest valid
// boundary hit ahead of the current endpoint and converting it into the same `newLength` currency
// those functions already consume.
// ============================================================================================

/// Appends `e`'s geometry (from the MODEL-space stores) as curveisect boundary shapes. One `Seg`
/// per Line/Polyline edge, one `Conic` for Circle/Arc/Ellipse. No existing helper produces
/// curveisect types from a SelectedEntity — TRIM's own `CollectCutSegments` produces raw
/// tessellated floats, not analytic shapes — so this is new, and (like LENGTHEN's length/endpoint
/// functions) written for reuse by BREAK/FILLET (steps 4/6).
static void AppendModelBoundaryShapes(const AppCommandState& st, const SelectedEntity& e,
                                      std::vector<curveisect::Seg>* segs, std::vector<curveisect::Conic>* conics) {
  using T = SelectedEntity::Type;
  if (e.type == T::LineSeg) {
    const size_t k = static_cast<size_t>(e.index);
    if (k >= st.drawing.lineCount())
      return;
    segs->push_back({{st.drawing.line(k).x0, st.drawing.line(k).y0},
                     {st.drawing.line(k).x1, st.drawing.line(k).y1}});
  } else if (e.type == T::Circle) {
    if (e.index < 0 || static_cast<size_t>(e.index) >= st.drawing.circleCount())
      return;
    const CadCircleRecord c = st.drawing.circle(static_cast<size_t>(e.index));
    conics->push_back(curveisect::MakeCircle(c.cx, c.cy, c.r));
  } else if (e.type == T::Arc) {
    if (e.index < 0 || static_cast<size_t>(e.index) >= st.drawing.userArcs.size())
      return;
    const CadArc& a = st.drawing.userArcs[static_cast<size_t>(e.index)];
    conics->push_back(curveisect::MakeArc(a.cx, a.cy, a.r, a.startRad, a.sweepRad));
  } else if (e.type == T::Ellipse) {
    if (e.index < 0 || static_cast<size_t>(e.index) >= st.drawing.userEllipses.size())
      return;
    const CadEllipse& el = st.drawing.userEllipses[static_cast<size_t>(e.index)];
    conics->push_back(curveisect::MakeEllipse(el.cx, el.cy, el.majVx, el.majVy, el.ratio));
  } else if (e.type == T::Polyline) {
    const int pi = e.index;
    if (pi < 0 || (static_cast<size_t>(pi)+ 1) >= st.drawing.userPolylineOffsets.size())
      return;
    const int v0 = st.drawing.userPolylineOffsets[static_cast<size_t>(pi)];
    const int v1 = st.drawing.userPolylineOffsets[(static_cast<size_t>(pi)+ 1)];
    for (int vi = v0; vi + 1 < v1; ++vi) {
      const size_t a = static_cast<size_t>(vi), b = (static_cast<size_t>(vi)+ 1);
      if (b >= st.drawing.vertexCount())
        break;
      segs->push_back({{st.drawing.vertex(a).x, st.drawing.vertex(a).y},
                       {st.drawing.vertex(b).x, st.drawing.vertex(b).y}});
    }
    if (static_cast<size_t>(pi) < st.drawing.userPolylineClosed.size() && st.drawing.userPolylineClosed[static_cast<size_t>(pi)] &&
        v1 - v0 >= 2) {
      const size_t a = (static_cast<size_t>(v1)- 1), b = static_cast<size_t>(v0);
      segs->push_back({{st.drawing.vertex(a).x, st.drawing.vertex(a).y},
                       {st.drawing.vertex(b).x, st.drawing.vertex(b).y}});
    }
  }
  // Annotation/FeatureLine/Surface/Mesh/PdfUnderlay/FilledRegion are refused before this is
  // called (REQ-201) — matches TRIM's own boundary-edge refusal set.
}

/// Storage-agnostic: finds the nearest point along the ray from (fixedX,fixedY) through
/// (movingX,movingY) — and beyond — that crosses one of the given boundary shapes, strictly beyond
/// the current moving point. `curveisect` has no infinite-ray primitive (confirmed by research —
/// every function clamps to [0,1]/sweep-containment), so the "ray" is a deliberately long finite
/// query Seg fed into the existing IntersectSegSeg/IntersectSegConic unchanged, rather than adding
/// a new ray family to a shared library for this one caller. Returns the new total distance from
/// the fixed point via \p outNewLength — the same currency ApplyLengthenToLine/ToPolylineEnd
/// already consume, so the caller just calls them with it.
static bool FindExtendLineTarget(const std::vector<curveisect::Seg>& segs, const std::vector<curveisect::Conic>& conics,
                                 float fixedX, float fixedY, float movingX, float movingY, float* outNewLength) {
  const float dx = movingX - fixedX, dy = movingY - fixedY;
  const float curLen = std::hypot(dx, dy);
  if (curLen < 1e-9f)
    return false;
  const float ux = dx / curLen, uy = dy / curLen;
  constexpr float kExtendLen = 1.0e6f;  // generous relative to any real drawing's coordinate range
  const curveisect::Seg query{{fixedX, fixedY}, {fixedX + ux * kExtendLen, fixedY + uy * kExtendLen}};
  const float curT = curLen / kExtendLen;
  constexpr float kEpsT = 1e-7f;
  bool found = false;
  float bestT = 0.f;
  std::vector<curveisect::Hit2> hits;
  for (const auto& s : segs) {
    hits.clear();
    curveisect::IntersectSegSeg(query, s, &hits);
    for (const auto& h : hits) {
      const float t = static_cast<float>(h.tA);
      if (t > curT + kEpsT && (!found || t < bestT)) {
        found = true;
        bestT = t;
      }
    }
  }
  for (const auto& c : conics) {
    hits.clear();
    curveisect::IntersectSegConic(query, c, &hits);
    for (const auto& h : hits) {
      const float t = static_cast<float>(h.tA);
      if (t > curT + kEpsT && (!found || t < bestT)) {
        found = true;
        bestT = t;
      }
    }
  }
  if (!found)
    return false;
  *outNewLength = bestT * kExtendLen;
  return true;
}

/// Storage-agnostic Arc equivalent. Queries the target's FULL circle (`MakeCircle`, not `MakeArc`
/// — a hit beyond the current sweep must not be filtered out) against the boundaries, then keeps
/// only hits that extend the sweep in its own existing rotational direction past the current
/// endpoint (CW arcs have negative `sweepRad`; extending the START moves it opposite the sweep's
/// own sense, extending the END moves it the same sense — the exact rule
/// `ApplyLengthenToArc` already applies). Returns the equivalent `newLength` for that function to
/// consume directly, so the `startRad`/`sweepRad` update itself is never re-derived here.
static bool FindExtendArcTarget(const std::vector<curveisect::Seg>& segs, const std::vector<curveisect::Conic>& conics,
                                float cx, float cy, float r, float startRad, float sweepRad, bool nearFirst,
                                float* outNewLength) {
  if (r < 1e-6f)
    return false;
  const curveisect::Conic full = curveisect::MakeCircle(cx, cy, r);
  const float reference = nearFirst ? startRad : (startRad + sweepRad);
  const bool ccwExtend = nearFirst ? (sweepRad <= 0.f) : (sweepRad > 0.f);
  constexpr float kTwoPi = 6.28318530717958647692f;
  constexpr float kEpsAngle = 1e-6f;
  bool found = false;
  float bestD = 0.f;
  std::vector<curveisect::Hit2> hits;
  auto consider = [&](double thetaD) {
    const float theta = static_cast<float>(thetaD);
    float d;
    if (ccwExtend) {
      d = std::fmod(theta - reference, kTwoPi);
      if (d <= 0.f)
        d += kTwoPi;
    } else {
      d = std::fmod(reference - theta, kTwoPi);
      if (d <= 0.f)
        d += kTwoPi;
    }
    if (d > kEpsAngle && (!found || d < bestD)) {
      found = true;
      bestD = d;
    }
  };
  for (const auto& s : segs) {
    hits.clear();
    curveisect::IntersectSegConic(s, full, &hits);
    for (const auto& h : hits)
      consider(h.tB);
  }
  for (const auto& c : conics) {
    hits.clear();
    curveisect::IntersectConicConic(c, full, &hits);
    for (const auto& h : hits)
      consider(h.tB);
  }
  if (!found)
    return false;
  *outNewLength = r * (std::fabs(sweepRad) + bestD);
  return true;
}

/// Resolves the current mode+value into one target length. Percent/Total need the entity's
/// current length (`curLen`); DElta only needs its own typed value.
static float LengthenResolveTargetLength(const AppCommandState& st, float curLen) {
  switch (st.lengthenMode) {
  case AppCommandState::LengthenMode::Delta:   return curLen + st.lengthenDeltaValue;
  case AppCommandState::LengthenMode::Percent: return curLen * (st.lengthenPercentValue / 100.f);
  case AppCommandState::LengthenMode::Total:   return st.lengthenTotalValue;
  case AppCommandState::LengthenMode::Dynamic: return curLen;  // unused — Dynamic resolves live, see HandleLengthenViewportPick
  }
  return curLen;
}

static const char* LengthenModeName(AppCommandState::LengthenMode m) {
  using LM = AppCommandState::LengthenMode;
  switch (m) {
  case LM::Delta:   return "DElta";
  case LM::Percent: return "Percent";
  case LM::Total:   return "Total";
  case LM::Dynamic: return "DYnamic";
  }
  return "DElta";
}

void StartLengthenCommand(AppCommandState& st, std::vector<std::string>& log) {
  ResetPaperModifyDrafts(st);
  using K = AppCommandState::Kind;
  if (st.activeSpaceIndex != kModelSpaceIndex && !InFloatingModelSpace(st)) {  // REQ-037-style paper routing
    stepengine::EndSession(st);
    st.paperLengthenPhase = 1;
    log.push_back(std::string("LENGTHEN — click an object (mode: ") + LengthenModeName(st.lengthenMode) +
                  (st.lengthenModeValueSet ? "). Esc cancels." :
                   "; no value set yet — type DE/P/T on the command line first). Esc cancels."));
    return;
  }
  ClearPendingViewportZoom(st);
  stepengine::BeginSession(st, std::make_unique<stepengine::LengthenSession>(), true);
  log.push_back(std::string("LENGTHEN — select object, or [DElta/Percent/Total/DYnamic] <") +
                LengthenModeName(st.lengthenMode) + ">. ESC cancels.");
}

static bool TryLengthenModeToggle(AppCommandState& st, stepengine::LengthenSession& sess,
                                  const std::string& lineIn, std::vector<std::string>& log) {
  using LM = AppCommandState::LengthenMode;
  using LP = stepengine::LengthenPhase;
  const std::string low = StringUtil::toLowerAsciiCopy(StringUtil::trimCopy(lineIn));
  if (low == "de" || low == "delta") {
    st.lengthenMode = LM::Delta;
    sess.phase = LP::WaitDeltaValue;
    log.push_back("LENGTHEN DElta — length to add (+) or subtract (-):");
    return true;
  }
  if (low == "p" || low == "percent") {
    st.lengthenMode = LM::Percent;
    sess.phase = LP::WaitPercentValue;
    log.push_back("LENGTHEN Percent — new length as a percentage of current (100 = unchanged):");
    return true;
  }
  if (low == "t" || low == "total") {
    st.lengthenMode = LM::Total;
    sess.phase = LP::WaitTotalValue;
    log.push_back("LENGTHEN Total — new total length:");
    return true;
  }
  if (low == "dy" || low == "dynamic") {
    st.lengthenMode = LM::Dynamic;
    st.lengthenModeValueSet = true;
    sess.phase = LP::WaitSelectOrMode;
    log.push_back("LENGTHEN DYnamic — select object, then drag to the new length. ESC cancels.");
    return true;
  }
  return false;
}

static void LengthenBeginValuePromptForPick(AppCommandState& st, stepengine::LengthenSession& sess,
                                            const SelectedEntity& hit, bool nearFirst,
                                            float curLen, std::vector<std::string>& log) {
  using LM = AppCommandState::LengthenMode;
  using LP = stepengine::LengthenPhase;
  sess.pendingEntity = hit;
  sess.pendingNearFirst = nearFirst;
  sess.pendingCurrentLength = curLen;
  sess.pendingApplyOnValue = true;
  const char* prompt = "length to add (+) or subtract (-):";
  switch (st.lengthenMode) {
  case LM::Percent:
    sess.phase = LP::WaitPercentValue;
    prompt = "new length as a percentage of current (100 = unchanged):";
    break;
  case LM::Total:
    sess.phase = LP::WaitTotalValue;
    prompt = "new total length:";
    break;
  case LM::Delta:
  case LM::Dynamic:
    sess.phase = LP::WaitDeltaValue;
    break;
  }
  char buf[224];
  std::snprintf(buf, sizeof(buf), "LENGTHEN — current length %.3f. %s — %s",
                static_cast<double>(curLen), LengthenModeName(st.lengthenMode), prompt);
  log.push_back(buf);
}

static bool LengthenApplyPendingPick(AppCommandState& st, stepengine::LengthenSession& sess,
                                     std::vector<std::string>& log) {
  if (!sess.pendingApplyOnValue)
    return false;
  const float curLen = sess.pendingCurrentLength;
  const float newLen = LengthenResolveTargetLength(st, curLen);
  if (ApplyLengthenToEntity(st, sess.pendingEntity, sess.pendingNearFirst, newLen, log)) {
    log.push_back("LENGTHEN — length " + std::to_string(curLen) + " -> " + std::to_string(newLen) + ".");
    BumpCadGpuCache(st);
  }
  sess.pendingApplyOnValue = false;
  sess.pendingEntity = SelectedEntity{};
  return true;
}

bool HandleLengthenText(AppCommandState& st, const std::string& lineIn, std::vector<std::string>& log) {
  auto* sess = st.sessionAs<stepengine::LengthenSession>();
  if (!sess)
    return false;
  std::string line = StringUtil::trimCopy(lineIn);
  using LP = stepengine::LengthenPhase;
  if (sess->phase == LP::WaitSelectOrMode) {
    if (TryLengthenModeToggle(st, *sess, line, log))
      return true;
    log.push_back("LENGTHEN — type DE, P, T, or DY to set the mode, or pick an object in the viewport.");
    return false;
  }
  if (sess->pendingApplyOnValue &&
      (sess->phase == LP::WaitDeltaValue || sess->phase == LP::WaitPercentValue ||
       sess->phase == LP::WaitTotalValue)) {
    if (TryLengthenModeToggle(st, *sess, line, log)) {
      if (st.lengthenMode == AppCommandState::LengthenMode::Dynamic) {
        sess->pendingApplyOnValue = false;
        sess->phase = LP::WaitDynamicTarget;
        log.push_back("LENGTHEN DYnamic — drag to the new length, or type it:");
      }
      return true;
    }
  }
  if (sess->phase == LP::WaitDeltaValue) {
    float v = 0.f;
    if (!ParseOneFloat(line, &v) || !std::isfinite(v)) {
      log.push_back("LENGTHEN DElta — must be a finite number (may be negative).");
      return false;
    }
    st.lengthenDeltaValue = v;
    st.lengthenModeValueSet = true;
    sess->phase = LP::WaitSelectOrMode;
    LengthenApplyPendingPick(st, *sess, log);
    log.push_back(std::string("LENGTHEN — select object, or [DElta/Percent/Total/DYnamic] <DElta ") +
                  std::to_string(v) + ">. ESC cancels.");
    return true;
  }
  if (sess->phase == LP::WaitPercentValue) {
    float v = 0.f;
    if (!ParseOneFloat(line, &v) || !(v > 0.f) || !std::isfinite(v)) {
      log.push_back("LENGTHEN Percent — must be a positive finite number.");
      return false;
    }
    st.lengthenPercentValue = v;
    st.lengthenModeValueSet = true;
    sess->phase = LP::WaitSelectOrMode;
    LengthenApplyPendingPick(st, *sess, log);
    log.push_back(std::string("LENGTHEN — select object, or [DElta/Percent/Total/DYnamic] <Percent ") +
                  std::to_string(v) + ">. ESC cancels.");
    return true;
  }
  if (sess->phase == LP::WaitTotalValue) {
    float v = 0.f;
    if (!ParseOneFloat(line, &v) || !(v > 0.f) || !std::isfinite(v)) {
      log.push_back("LENGTHEN Total — must be a positive finite number.");
      return false;
    }
    st.lengthenTotalValue = v;
    st.lengthenModeValueSet = true;
    sess->phase = LP::WaitSelectOrMode;
    LengthenApplyPendingPick(st, *sess, log);
    log.push_back(std::string("LENGTHEN — select object, or [DElta/Percent/Total/DYnamic] <Total ") +
                  std::to_string(v) + ">. ESC cancels.");
    return true;
  }
  if (sess->phase == LP::WaitDynamicTarget) {
    float v = 0.f;
    if (!ParseOneFloat(line, &v) || !(v > 0.f) || !std::isfinite(v)) {
      log.push_back("LENGTHEN DYnamic — must be a positive finite new length, or click in the viewport.");
      return false;
    }
    if (ApplyLengthenToEntity(st, sess->pendingEntity, sess->pendingNearFirst, v, log)) {
      log.push_back("LENGTHEN — object updated.");
      BumpCadGpuCache(st);
    }
    sess->phase = LP::WaitSelectOrMode;
    return true;
  }
  return false;
}

static bool LengthenDynamicTargetLength(const AppCommandState& st, const stepengine::LengthenSession& sess,
                                        float wx, float wy, float* outLen) {
  const SelectedEntity& e = sess.pendingEntity;
  const bool nearFirst = sess.pendingNearFirst;
  switch (e.type) {
  case SelectedEntity::Type::LineSeg: {
    const size_t k = static_cast<size_t>(e.index);
    if (k >= st.drawing.lineCount())
      return false;
    const float x0 = static_cast<float>(st.drawing.line(k).x0), y0 = static_cast<float>(st.drawing.line(k).y0);
    const float x1 = static_cast<float>(st.drawing.line(k).x1), y1 = static_cast<float>(st.drawing.line(k).y1);
    const float fixedX = nearFirst ? x1 : x0, fixedY = nearFirst ? y1 : y0;
    const float movingX = nearFirst ? x0 : x1, movingY = nearFirst ? y0 : y1;
    const float dx = movingX - fixedX, dy = movingY - fixedY;
    const float curLen = std::hypot(dx, dy);
    if (curLen < 1e-9f)
      return false;
    const float ux = dx / curLen, uy = dy / curLen;
    *outLen = std::max((wx - fixedX) * ux + (wy - fixedY) * uy, 1e-6f);
    return true;
  }
  case SelectedEntity::Type::Polyline: {
    const int pi = e.index;
    if (pi < 0 || (static_cast<size_t>(pi)+ 1) >= st.drawing.userPolylineOffsets.size())
      return false;
    const int v0 = st.drawing.userPolylineOffsets[static_cast<size_t>(pi)];
    const int v1 = st.drawing.userPolylineOffsets[(static_cast<size_t>(pi)+ 1)];
    if (v1 - v0 < 2)
      return false;
    const int movingVi = nearFirst ? v0 : (v1 - 1);
    const int fixedVi = nearFirst ? (v0 + 1) : (v1 - 2);
    const size_t mIdx = static_cast<size_t>(movingVi), fIdx = static_cast<size_t>(fixedVi);
    if (mIdx >= st.drawing.vertexCount() || fIdx >= st.drawing.vertexCount())
      return false;
    const float fx = static_cast<float>(st.drawing.vertex(fIdx).x), fy = static_cast<float>(st.drawing.vertex(fIdx).y);
    const float mx = static_cast<float>(st.drawing.vertex(mIdx).x), my = static_cast<float>(st.drawing.vertex(mIdx).y);
    const float segLen = std::hypot(mx - fx, my - fy);
    if (segLen < 1e-9f)
      return false;
    const float ux = (mx - fx) / segLen, uy = (my - fy) / segLen;
    const float proj = (wx - fx) * ux + (wy - fy) * uy;
    const float curTotal = PolylineOpenLengthOf(st, pi);
    *outLen = std::max(curTotal - segLen + proj, 1e-6f);
    return true;
  }
  case SelectedEntity::Type::Arc: {
    if (e.index < 0 || static_cast<size_t>(e.index) >= st.drawing.userArcs.size())
      return false;
    const CadArc& a = st.drawing.userArcs[static_cast<size_t>(e.index)];
    if (a.r < 1e-6f)
      return false;
    const float pickAngle = static_cast<float>(std::atan2(wy - a.cy, wx - a.cx));
    const float fixedAngle = nearFirst ? (a.startRad + a.sweepRad) : a.startRad;
    // Bounded to a half-circle of drag range either way (NormalizeAngleRadMinusPiToPi) — a
    // deliberately simple, directionally-honest live-preview approximation; a change larger than
    // that is what Total mode's typed value is for.
    const float delta = NormalizeAngleRadMinusPiToPi(pickAngle - fixedAngle);
    *outLen = static_cast<float>(std::max<double>(a.r * std::fabs(delta), 1e-6));
    return true;
  }
  default:
    return false;
  }
}

/// Model-space + floating-model-space viewport-pick handler for LENGTHEN — called from
/// SubmitViewportPickImpl, the exact site RECT's history warns a new command must not go missing
/// from.
void HandleLengthenViewportPick(AppCommandState& st, float wx, float wy, std::vector<std::string>& log) {
  auto* sess = st.sessionAs<stepengine::LengthenSession>();
  if (!sess)
    return;
  using LP = stepengine::LengthenPhase;
  if (sess->phase == LP::WaitDynamicTarget) {
    float newLen = 0.f;
    if (!LengthenDynamicTargetLength(st, *sess, wx, wy, &newLen)) {
      log.push_back("LENGTHEN DYnamic — could not resolve a length from that pick; try again.");
      return;
    }
    if (ApplyLengthenToEntity(st, sess->pendingEntity, sess->pendingNearFirst, newLen, log)) {
      log.push_back("LENGTHEN — object updated.");
      BumpCadGpuCache(st);
    }
    sess->phase = LP::WaitSelectOrMode;
    return;
  }
  if (sess->phase != LP::WaitSelectOrMode)
    return;
  SelectedEntity hit{};
  float d2 = 0.f;
  if (!PickClosestCadEntity(st, wx, wy, CadOffsetEntityPickTolWorld(st), &hit, &d2)) {
    log.push_back("LENGTHEN — nothing under cursor; try again.");
    return;
  }
  bool nearFirst = false;
  float curLen = 0.f;
  if (!LengthenEligibility(st, hit, wx, wy, &nearFirst, &curLen, "LENGTHEN", log))
    return;
  if (st.lengthenMode == AppCommandState::LengthenMode::Dynamic) {
    sess->pendingEntity = hit;
    sess->pendingNearFirst = nearFirst;
    sess->pendingCurrentLength = curLen;
    sess->phase = LP::WaitDynamicTarget;
    log.push_back("LENGTHEN DYnamic — drag to the new length, or type it:");
    return;
  }
  if (!st.lengthenModeValueSet) {
    LengthenBeginValuePromptForPick(st, *sess, hit, nearFirst, curLen, log);
    return;
  }
  const float newLen = LengthenResolveTargetLength(st, curLen);
  if (ApplyLengthenToEntity(st, hit, nearFirst, newLen, log)) {
    log.push_back("LENGTHEN — length " + std::to_string(curLen) + " -> " + std::to_string(newLen) + ".");
    BumpCadGpuCache(st);
  }
}

void HandleExtendViewportPick(AppCommandState& st, float wx, float wy, std::vector<std::string>& log) {
  auto* ext = st.sessionAs<stepengine::ExtendSession>();
  if (!ext)
    return;
  using EP = stepengine::ExtendPhase;
  if (ext->phase == EP::SelectBoundaries) {
    SelectedEntity hit{};
    float d2 = 0.f;
    if (!PickClosestCadEntity(st, wx, wy, CadOffsetEntityPickTolWorld(st), &hit, &d2)) {
      log.push_back("EXTEND — no object at pick.");
      return;
    }
    if (hit.type == SelectedEntity::Type::Annotation) {
      log.push_back("EXTEND — use a line, circle, arc, ellipse, or polyline as a boundary edge.");
      return;
    }
    if (hit.type == SelectedEntity::Type::FeatureLine) {
      log.push_back("EXTEND — 1 feature line ignored: a feature line cannot be a boundary edge. Use "
                    "a line, circle, arc, ellipse, or polyline.");
      return;
    }
    if (hit.type == SelectedEntity::Type::Surface) {
      log.push_back("EXTEND — 1 surface ignored: a surface cannot be a boundary edge. Use a line, "
                    "circle, arc, ellipse, or polyline.");
      return;
    }
    if (hit.type == SelectedEntity::Type::Spline) {
      log.push_back("EXTEND — splines cannot be boundary edges; nothing changed.");
      return;
    }
    for (const auto& c : ext->boundaries) {
      if (c.type == hit.type && c.index == hit.index) {
        log.push_back("EXTEND — already a boundary edge.");
        return;
      }
    }
    ext->boundaries.push_back(hit);
    log.push_back("EXTEND — boundary edge added.");
    return;
  }

  // SelectTargets.
  SelectedEntity hit{};
  float d2 = 0.f;
  if (!PickClosestCadEntity(st, wx, wy, CadOffsetEntityPickTolWorld(st), &hit, &d2)) {
    log.push_back("EXTEND — nothing under cursor; try again.");
    return;
  }
  bool nearFirst = false;
  float curLen = 0.f;
  if (!LengthenEligibility(st, hit, wx, wy, &nearFirst, &curLen, "EXTEND", log))
    return;

  std::vector<curveisect::Seg> segs;
  std::vector<curveisect::Conic> conics;
  for (const auto& b : ext->boundaries)
    AppendModelBoundaryShapes(st, b, &segs, &conics);

  float newLen = 0.f;
  bool found = false;
  if (hit.type == SelectedEntity::Type::Arc) {
    const CadArc& a = st.drawing.userArcs[static_cast<size_t>(hit.index)];
    found = FindExtendArcTarget(segs, conics, static_cast<float>(a.cx), static_cast<float>(a.cy), static_cast<float>(a.r), a.startRad, a.sweepRad, nearFirst, &newLen);
  } else if (hit.type == SelectedEntity::Type::LineSeg) {
    const size_t k = static_cast<size_t>(hit.index);
    const float x0 = static_cast<float>(st.drawing.line(k).x0), y0 = static_cast<float>(st.drawing.line(k).y0);
    const float x1 = static_cast<float>(st.drawing.line(k).x1), y1 = static_cast<float>(st.drawing.line(k).y1);
    const float fixedX = nearFirst ? x1 : x0, fixedY = nearFirst ? y1 : y0;
    const float movingX = nearFirst ? x0 : x1, movingY = nearFirst ? y0 : y1;
    found = FindExtendLineTarget(segs, conics, fixedX, fixedY, movingX, movingY, &newLen);
  } else {  // Polyline — ray-cast along the LOCAL last-segment's own direction (the two vertices
            // ApplyLengthenToPolylineEnd itself extends between), not the whole polyline's global
            // first-to-last chord: for a bent (3+ vertex) polyline those directions differ, and the
            // chord would find a boundary hit that the actual per-segment mutation then misses.
    const int pi = hit.index;
    const int v0 = st.drawing.userPolylineOffsets[static_cast<size_t>(pi)];
    const int v1 = st.drawing.userPolylineOffsets[(static_cast<size_t>(pi)+ 1)];
    const int movingVi = nearFirst ? v0 : (v1 - 1);
    const int fixedVi = nearFirst ? (v0 + 1) : (v1 - 2);
    const size_t mIdx = static_cast<size_t>(movingVi), fIdx = static_cast<size_t>(fixedVi);
    const float fixedX = static_cast<float>(st.drawing.vertex(fIdx).x), fixedY = static_cast<float>(st.drawing.vertex(fIdx).y);
    const float movingX = static_cast<float>(st.drawing.vertex(mIdx).x), movingY = static_cast<float>(st.drawing.vertex(mIdx).y);
    const float segLen = std::hypot(movingX - fixedX, movingY - fixedY);
    float newSegLen = 0.f;
    found = FindExtendLineTarget(segs, conics, fixedX, fixedY, movingX, movingY, &newSegLen);
    if (found)
      newLen = curLen - segLen + newSegLen;  // curLen is the polyline's current TOTAL length
                                              // (LengthenEligibility/PolylineOpenLengthOf) — the
                                              // currency ApplyLengthenToPolylineEnd expects.
  }
  if (!found) {
    log.push_back("EXTEND — 1 object does not reach a boundary edge in that direction.");
    return;
  }
  if (ApplyLengthenToEntity(st, hit, nearFirst, newLen, log)) {
    log.push_back("EXTEND — extended to the boundary.");
    BumpCadGpuCache(st);
  }
  // Stays in SelectTargets — loop back for the next object, same shape as TRIM/OFFSET targets.
}

/// REQ-103 LENGTHEN, pure-paper-space path. Single-pick-apply, matching the paper click handler's
/// own shape (CadUi.cpp) — same simplification MIRROR's paper path already documents: no mode/
/// value prompt here, so this reuses whatever lengthenMode/lengthenDeltaValue/etc. the model-space
/// command last configured, refusing rather than applying a stray default if none was ever set, or
/// if the configured mode is Dynamic (which needs a two-step drag the paper click flow can't ask
/// for a target through, the same text-entry gap MIRROR's paper path hits for its erase prompt).
/// Shared by paper-space LENGTHEN and EXTEND (TASK-096) — both need "is this a length-bearing
/// paper entity, and which end is nearest the pick", mirroring the model-space
/// `LengthenEligibility` this same pair shares there. \p cmdName names the caller in refusal
/// messages (REQ-201).
static bool PaperLengthenEligibility(const PaperLayout& L, const PaperEntityRef& ref, float pickXIn, float pickYIn,
                                     bool* outNearFirst, float* outCurLen, const char* cmdName,
                                     std::vector<std::string>& log) {
  constexpr float kTwoPi = 6.28318530717958647692f;
  switch (ref.type) {
  case PaperEntityRef::Type::Line: {
    const size_t k = static_cast<size_t>(ref.index) * 6;
    if (k + 5 >= L.paperLines.size())
      return false;
    const float x0 = L.paperLines[k], y0 = L.paperLines[k + 1];
    const float x1 = L.paperLines[k + 3], y1 = L.paperLines[k + 4];
    *outNearFirst = NearerToFirstPoint(pickXIn, pickYIn, x0, y0, x1, y1);
    *outCurLen = std::hypot(x1 - x0, y1 - y0);
    return true;
  }
  case PaperEntityRef::Type::Arc: {
    if (ref.index < 0 || static_cast<size_t>(ref.index) >= L.paperArcs.size())
      return false;
    const CadArc& a = L.paperArcs[static_cast<size_t>(ref.index)];
    if (std::fabs(std::fabs(a.sweepRad) - kTwoPi) < 1e-4f) {
      log.push_back(std::string(cmdName) + " — 1 full-circle arc ignored: a full circle has no end to lengthen from.");
      return false;
    }
    const float sx = static_cast<float>(a.cx + a.r * std::cos(a.startRad)), sy = static_cast<float>(a.cy + a.r * std::sin(a.startRad));
    const float ex = static_cast<float>(a.cx + a.r * std::cos(a.startRad + a.sweepRad));
    const float ey = static_cast<float>(a.cy + a.r * std::sin(a.startRad + a.sweepRad));
    *outNearFirst = NearerToFirstPoint(pickXIn, pickYIn, sx, sy, ex, ey);
    *outCurLen = ArcLengthOf(static_cast<float>(a.r), a.sweepRad);
    return true;
  }
  case PaperEntityRef::Type::Polyline: {
    const int pi = ref.index;
    if (pi < 0 || (static_cast<size_t>(pi)+ 1) >= L.paperPolyOffsets.size())
      return false;
    if (static_cast<size_t>(pi) < L.paperPolyClosed.size() && L.paperPolyClosed[static_cast<size_t>(pi)]) {
      log.push_back(std::string(cmdName) + " — 1 closed polyline ignored: a closed polyline has no end to lengthen from.");
      return false;
    }
    const int v0 = L.paperPolyOffsets[static_cast<size_t>(pi)];
    const int v1 = L.paperPolyOffsets[(static_cast<size_t>(pi)+ 1)];
    if (v1 - v0 < 2)
      return false;
    const size_t a0 = static_cast<size_t>(v0) * 3, a1 = (static_cast<size_t>(v1)- 1) * 3;
    if (a1 + 1 >= L.paperPolyVerts.size())
      return false;
    *outNearFirst = NearerToFirstPoint(pickXIn, pickYIn, L.paperPolyVerts[a0], L.paperPolyVerts[a0 + 1],
                                       L.paperPolyVerts[a1], L.paperPolyVerts[a1 + 1]);
    float total = 0.f;
    for (int vi = v0; vi + 1 < v1; ++vi) {
      const size_t a = static_cast<size_t>(vi) * 3, b = (static_cast<size_t>(vi)+ 1) * 3;
      total += std::hypot(L.paperPolyVerts[b] - L.paperPolyVerts[a], L.paperPolyVerts[b + 1] - L.paperPolyVerts[a + 1]);
    }
    *outCurLen = total;
    return true;
  }
  default:
    log.push_back(std::string(cmdName) + " — only a line, open polyline, or arc can be lengthened.");
    return false;
  }
}

/// Shared mutation: moves the near end of paper entity `ref` so its length becomes `newLen`,
/// holding the other end fixed — the paper-store equivalent of `ApplyLengthenToLine`/`ToArc`/
/// `ToPolylineEnd`, used by both paper LENGTHEN and paper EXTEND so the Line/Arc/Polyline math

bool ApplyLengthenToPaperEntity(AppCommandState& st, const PaperEntityRef& ref, float pickXIn, float pickYIn,
                                std::vector<std::string>& log) {
  PaperLayout* L = ActivePaperGeometryTarget(st);
  if (!L)
    return false;
  if (st.lengthenMode == AppCommandState::LengthenMode::Dynamic) {
    log.push_back("LENGTHEN — DYnamic mode needs a live drag, which paper space does not offer a "
                  "prompt for; switch to DElta/Percent/Total in model space first.");
    return false;
  }
  bool nearFirst = false;
  float curLen = 0.f;
  if (!PaperLengthenEligibility(*L, ref, pickXIn, pickYIn, &nearFirst, &curLen, "LENGTHEN", log))
    return false;
  if (!st.lengthenModeValueSet) {
    // TASK-100. Model space latches the pick and prompts for the value; paper space has no command
    // state to hold that prompt (it runs with `active == None` — see paperLengthenPhase), so the
    // pick cannot be completed here. It can still be worth something: eligibility now runs FIRST,
    // so the pick at least reports what the object measures instead of being a bare refusal.
    char buf[224];
    std::snprintf(buf, sizeof(buf),
                  "LENGTHEN — current length %.3f in. No %s value set yet; type DE/P/T on the "
                  "model-space command line first, then pick here again.",
                  static_cast<double>(curLen), LengthenModeName(st.lengthenMode));
    log.push_back(buf);
    return false;
  }

  const float newLen = LengthenResolveTargetLength(st, curLen);
  if (!(newLen > 1e-6f) || !std::isfinite(newLen)) {
    log.push_back("LENGTHEN — that change would collapse the object to zero length; refused.");
    return false;
  }

  PushUndoSnapshot(st, "Lengthen paper geometry");
  ApplyLengthToPaperEntityMutation(L, ref, nearFirst, curLen, newLen);
  BumpCadGpuCache(st);
  log.push_back("LENGTHEN — paper object updated (" + std::to_string(curLen) + " -> " +
                std::to_string(newLen) + ").");
  return true;
}

/// Storage-agnostic geometry, mirroring `AppendModelBoundaryShapes` but reading `PaperLayout`'s
/// stores instead of `AppCommandState`'s — unavoidable duplication given this codebase's two-store
/// convention with no shared entity representation (the same duplication MIRROR's paper functions
/// already accepted), feeding the SAME shared `FindExtendLineTarget`/`FindExtendArcTarget`.
static void AppendPaperBoundaryShapes(const PaperLayout& L, const PaperEntityRef& ref,
                                      std::vector<curveisect::Seg>* segs, std::vector<curveisect::Conic>* conics) {
  using T = PaperEntityRef::Type;
  if (ref.type == T::Line) {
    const size_t k = static_cast<size_t>(ref.index) * 6;
    if (k + 5 >= L.paperLines.size())
      return;
    segs->push_back({{L.paperLines[k], L.paperLines[k + 1]}, {L.paperLines[k + 3], L.paperLines[k + 4]}});
  } else if (ref.type == T::Circle) {
    const size_t k = static_cast<size_t>(ref.index) * 3;
    if (k + 2 >= L.paperCircles.size())
      return;
    conics->push_back(curveisect::MakeCircle(L.paperCircles[k], L.paperCircles[k + 1], L.paperCircles[k + 2]));
  } else if (ref.type == T::Arc) {
    if (ref.index < 0 || static_cast<size_t>(ref.index) >= L.paperArcs.size())
      return;
    const CadArc& a = L.paperArcs[static_cast<size_t>(ref.index)];
    conics->push_back(curveisect::MakeArc(a.cx, a.cy, a.r, a.startRad, a.sweepRad));
  } else if (ref.type == T::Ellipse) {
    if (ref.index < 0 || static_cast<size_t>(ref.index) >= L.paperEllipses.size())
      return;
    const CadEllipse& el = L.paperEllipses[static_cast<size_t>(ref.index)];
    conics->push_back(curveisect::MakeEllipse(el.cx, el.cy, el.majVx, el.majVy, el.ratio));
  } else if (ref.type == T::Polyline) {
    const int pi = ref.index;
    if (pi < 0 || (static_cast<size_t>(pi)+ 1) >= L.paperPolyOffsets.size())
      return;
    const int v0 = L.paperPolyOffsets[static_cast<size_t>(pi)];
    const int v1 = L.paperPolyOffsets[(static_cast<size_t>(pi)+ 1)];
    for (int vi = v0; vi + 1 < v1; ++vi) {
      const size_t a = static_cast<size_t>(vi) * 3, b = (static_cast<size_t>(vi)+ 1) * 3;
      if (b + 1 >= L.paperPolyVerts.size())
        break;
      segs->push_back({{L.paperPolyVerts[a], L.paperPolyVerts[a + 1]}, {L.paperPolyVerts[b], L.paperPolyVerts[b + 1]}});
    }
    if (static_cast<size_t>(pi) < L.paperPolyClosed.size() && L.paperPolyClosed[static_cast<size_t>(pi)] && v1 - v0 >= 2) {
      const size_t a = (static_cast<size_t>(v1)- 1) * 3, b = static_cast<size_t>(v0) * 3;
      segs->push_back({{L.paperPolyVerts[a], L.paperPolyVerts[a + 1]}, {L.paperPolyVerts[b], L.paperPolyVerts[b + 1]}});
    }
  }
  // Text is refused before this is called (REQ-201) — paper's analogue of Annotation.
}

/// REQ-103 EXTEND, pure-paper-space path. Unlike LENGTHEN's paper path, EXTEND needs no typed
/// value at all, so it is NOT simplified away — this mirrors the model-space
/// `HandleExtendViewportPick`'s target-half exactly, against the paper store.
bool ApplyExtendToPaperEntity(AppCommandState& st, const PaperEntityRef& ref, float pickXIn, float pickYIn,
                              std::vector<std::string>& log) {
  PaperLayout* L = ActivePaperGeometryTarget(st);
  if (!L)
    return false;
  bool nearFirst = false;
  float curLen = 0.f;
  if (!PaperLengthenEligibility(*L, ref, pickXIn, pickYIn, &nearFirst, &curLen, "EXTEND", log))
    return false;

  std::vector<curveisect::Seg> segs;
  std::vector<curveisect::Conic> conics;
  for (const auto& b : st.paperExtendBoundaries)
    AppendPaperBoundaryShapes(*L, b, &segs, &conics);

  float newLen = 0.f;
  bool found = false;
  if (ref.type == PaperEntityRef::Type::Arc) {
    const CadArc& a = L->paperArcs[static_cast<size_t>(ref.index)];
    found = FindExtendArcTarget(segs, conics, static_cast<float>(a.cx), static_cast<float>(a.cy), static_cast<float>(a.r), a.startRad, a.sweepRad, nearFirst, &newLen);
  } else if (ref.type == PaperEntityRef::Type::Line) {
    const size_t k = static_cast<size_t>(ref.index) * 6;
    const float x0 = L->paperLines[k], y0 = L->paperLines[k + 1];
    const float x1 = L->paperLines[k + 3], y1 = L->paperLines[k + 4];
    const float fixedX = nearFirst ? x1 : x0, fixedY = nearFirst ? y1 : y0;
    const float movingX = nearFirst ? x0 : x1, movingY = nearFirst ? y0 : y1;
    found = FindExtendLineTarget(segs, conics, fixedX, fixedY, movingX, movingY, &newLen);
  } else {  // Polyline — LOCAL last-segment direction, matching model-space's own fix above and
            // ApplyLengthToPaperEntityMutation's own fixedVi/movingVi (not the whole polyline's
            // global first-to-last chord, which diverges from it once the polyline bends).
    const int pi = ref.index;
    const int v0 = L->paperPolyOffsets[static_cast<size_t>(pi)];
    const int v1 = L->paperPolyOffsets[(static_cast<size_t>(pi)+ 1)];
    const int movingVi = nearFirst ? v0 : (v1 - 1);
    const int fixedVi = nearFirst ? (v0 + 1) : (v1 - 2);
    const size_t mIdx = static_cast<size_t>(movingVi) * 3, fIdx = static_cast<size_t>(fixedVi) * 3;
    const float fixedX = L->paperPolyVerts[fIdx], fixedY = L->paperPolyVerts[fIdx + 1];
    const float movingX = L->paperPolyVerts[mIdx], movingY = L->paperPolyVerts[mIdx + 1];
    const float segLen = std::hypot(movingX - fixedX, movingY - fixedY);
    float newSegLen = 0.f;
    found = FindExtendLineTarget(segs, conics, fixedX, fixedY, movingX, movingY, &newSegLen);
    if (found)
      newLen = curLen - segLen + newSegLen;
  }
  if (!found) {
    log.push_back("EXTEND — 1 object does not reach a boundary edge in that direction.");
    return false;
  }

  PushUndoSnapshot(st, "Extend paper geometry");
  ApplyLengthToPaperEntityMutation(L, ref, nearFirst, curLen, newLen);
  BumpCadGpuCache(st);
  log.push_back("EXTEND — paper object extended to the boundary.");
  return true;
}

// BREAK reuses OFFSET's point-on-segment projection rather than re-deriving it (it is `static`

// ============================================================================================
// BREAK (REQ-103 step 4, D-2026-08-24-c). Splits a single entity at one or two picked points.
// Eligible: Line, Circle, Arc (any sweep), open and closed Polyline. Ellipse is refused — GoSurvey
// has no elliptical-arc entity kind to hold a broken-open ellipse, and adding one is a new entity
// kind this step's own spec note rules out. On an OPEN entity the two break points are ordered by
// position along the entity (independent of click order) and the material between them is
// removed. On a CLOSED entity (Circle, full-circle-sweep Arc, closed Polyline) click order is
// load-bearing: the material swept from point 1 to point 2, in the direction of increasing
// parameter, is removed, leaving one open result starting at point 2, ending at point 1 —
// AutoCAD's own circle-break convention, generalized to all three closed cases.
// ============================================================================================

/// How far around an Arc's OWN sweep direction (from `startRad`, growing the way `sweepRad`'s sign
/// already does) you'd travel to reach `theta` — radians, in [0, 2*pi). Same direction-of-travel
/// rule `FindExtendArcTarget`'s `consider` lambda already applies (CW arcs have negative sweepRad).
static float ArcSweepParam(float startRad, float sweepRad, float theta) {
  constexpr float kTwoPi = 6.28318530717958647692f;
  float d;
  if (sweepRad >= 0.f) {
    d = std::fmod(theta - startRad, kTwoPi);
    if (d < 0.f) d += kTwoPi;
  } else {
    d = std::fmod(startRad - theta, kTwoPi);
    if (d < 0.f) d += kTwoPi;
  }
  return d;
}

/// Shared by Circle and full-circle-sweep Arc: the material from `theta1` to `theta2`, travelling
/// COUNTERCLOCKWISE (AutoCAD's own circle-break convention — a full sweep's own stored direction
/// carries no meaning once it is a closed loop, so both cases use this same fixed sense), is
/// removed; the result starts at `theta2` and sweeps CCW to `theta1`. `theta1==theta2` (a repeated
/// pick) yields a full `2*pi` sweep — the "break at point" case, not a degenerate zero-length one.
static void CircleBreakStartSweep(float theta1, float theta2, float* outStart, float* outSweep) {
  constexpr float kTwoPi = 6.28318530717958647692f;
  float sweep = std::fmod(theta1 - theta2, kTwoPi);
  if (sweep <= 1e-7f)
    sweep += kTwoPi;
  *outStart = theta2;
  *outSweep = sweep;
}

/// Projects (px,py) onto entity \p e (model-space stores), filling \p out. No existing helper
/// resolves "the closest point ON this entity" — `PickClosestCadEntity` answers "which entity",
/// with a distance only.
///
/// TASK-101: no longer static. BREAK's live preview has to resolve the cursor to the SAME point the
/// second pick will commit, or it draws a span that is not the one that gets removed — the exact
/// class of mistake `project_3d_preview_commit_point` records ("previews read the cursor, picks
/// commit the snapped point"). One function, two callers, no second implementation. Declared in the
/// header for the same reason its paper-space sibling `ClosestPointOnPaperEntity` already is.
bool ClosestPointOnEntity(const AppCommandState& st, const SelectedEntity& e, float px, float py,
                                 BreakPoint* out) {
  switch (e.type) {
  case SelectedEntity::Type::LineSeg: {
    const size_t k = static_cast<size_t>(e.index);
    if (k >= st.drawing.lineCount())
      return false;
    const float x0 = static_cast<float>(st.drawing.line(k).x0), y0 = static_cast<float>(st.drawing.line(k).y0);
    const float x1 = static_cast<float>(st.drawing.line(k).x1), y1 = static_cast<float>(st.drawing.line(k).y1);
    ClosestPointOnSegment(x0, y0, x1, y1, px, py, &out->x, &out->y);
    out->param = std::hypot(out->x - x0, out->y - y0);
    // The cut's elevation, interpolated along the line (issue 01). ApplyBreakToLine already carried
    // the two ENDPOINT elevations through; this makes the cut itself right too, which matters as
    // soon as the line is sloped.
    {
      const float len = std::hypot(x1 - x0, y1 - y0);
      const float t = (len > 1e-9f) ? (out->param / len) : 0.f;
      out->z = static_cast<float>(st.drawing.line(k).z0 + (st.drawing.line(k).z1 - st.drawing.line(k).z0) * t);
    }
    return true;
  }
  case SelectedEntity::Type::Circle: {
    if (e.index < 0 || static_cast<size_t>(e.index) >= st.drawing.circleCount())
      return false;
    const CadCircleRecord c = st.drawing.circle(static_cast<size_t>(e.index));
    const float cx = static_cast<float>(c.cx), cy = static_cast<float>(c.cy), r = static_cast<float>(c.r);
    out->z = static_cast<float>(c.cz);  // the circle's plane (REQ-057 / ADR-025)
    out->theta = std::atan2(py - cy, px - cx);
    out->x = cx + r * std::cos(out->theta);
    out->y = cy + r * std::sin(out->theta);
    return true;
  }
  case SelectedEntity::Type::Arc: {
    if (e.index < 0 || static_cast<size_t>(e.index) >= st.drawing.userArcs.size())
      return false;
    const CadArc& a = st.drawing.userArcs[static_cast<size_t>(e.index)];
    out->theta = static_cast<float>(std::atan2(py - a.cy, px - a.cx));
    out->z = static_cast<float>(a.z);  // the arc's plane (REQ-057 / ADR-025)
    out->x = static_cast<float>(a.cx + a.r * std::cos(out->theta));
    out->y = static_cast<float>(a.cy + a.r * std::sin(out->theta));
    out->param = static_cast<float>(a.r * ArcSweepParam(a.startRad, a.sweepRad, out->theta));
    return true;
  }
  case SelectedEntity::Type::Polyline: {
    const int pi = e.index;
    if (pi < 0 || (static_cast<size_t>(pi)+ 1) >= st.drawing.userPolylineOffsets.size())
      return false;
    const int v0 = st.drawing.userPolylineOffsets[static_cast<size_t>(pi)];
    const int v1 = st.drawing.userPolylineOffsets[(static_cast<size_t>(pi)+ 1)];
    const bool closed =
        static_cast<size_t>(pi) < st.drawing.userPolylineClosed.size() && st.drawing.userPolylineClosed[static_cast<size_t>(pi)];
    bool any = false;
    float bestD2 = std::numeric_limits<float>::max();
    float cum = 0.f;
    for (int vi = v0; vi + 1 < v1; ++vi) {
      const size_t A = static_cast<size_t>(vi), B = (static_cast<size_t>(vi)+ 1);
      const float ax = static_cast<float>(st.drawing.vertex(A).x), ay = static_cast<float>(st.drawing.vertex(A).y);
      const float bx = static_cast<float>(st.drawing.vertex(B).x), by = static_cast<float>(st.drawing.vertex(B).y);
      const float az = static_cast<float>(st.drawing.vertex(A).z), bz = static_cast<float>(st.drawing.vertex(B).z);
      float qx = 0.f, qy = 0.f;
      ClosestPointOnSegment(ax, ay, bx, by, px, py, &qx, &qy);
      const float d2 = (qx - px) * (qx - px) + (qy - py) * (qy - py);
      if (d2 < bestD2) {
        bestD2 = d2;
        any = true;
        out->x = qx;
        out->y = qy;
        out->param = cum + std::hypot(qx - ax, qy - ay);
        {  // the cut's elevation, interpolated along this segment (issue 01)
          const float segLen = std::hypot(bx - ax, by - ay);
          const float t = (segLen > 1e-9f) ? (std::hypot(qx - ax, qy - ay) / segLen) : 0.f;
          out->z = az + (bz - az) * t;
        }
        out->segIndex = vi - v0;
      }
      cum += std::hypot(bx - ax, by - ay);
    }
    if (closed && v1 - v0 >= 2) {
      const size_t A = (static_cast<size_t>(v1)- 1), B = static_cast<size_t>(v0);
      const float ax = static_cast<float>(st.drawing.vertex(A).x), ay = static_cast<float>(st.drawing.vertex(A).y);
      const float bx = static_cast<float>(st.drawing.vertex(B).x), by = static_cast<float>(st.drawing.vertex(B).y);
      const float az = static_cast<float>(st.drawing.vertex(A).z), bz = static_cast<float>(st.drawing.vertex(B).z);
      float qx = 0.f, qy = 0.f;
      ClosestPointOnSegment(ax, ay, bx, by, px, py, &qx, &qy);
      const float d2 = (qx - px) * (qx - px) + (qy - py) * (qy - py);
      if (d2 < bestD2) {
        bestD2 = d2; // NOLINT(clang-analyzer-deadcode.DeadStores) - last candidate of this search; nothing reads it after
        any = true;
        out->x = qx;
        out->y = qy;
        out->param = cum + std::hypot(qx - ax, qy - ay);
        {  // the cut's elevation, interpolated along this segment (issue 01)
          const float segLen = std::hypot(bx - ax, by - ay);
          const float t = (segLen > 1e-9f) ? (std::hypot(qx - ax, qy - ay) / segLen) : 0.f;
          out->z = az + (bz - az) * t;
        }
        out->segIndex = (v1 - v0) - 1;
      }
    }
    return any;
  }
  default:
    return false;
  }
}

static void ApplyBreakToLine(AppCommandState& st, int index, const BreakPoint& p1, const BreakPoint& p2,
                             std::vector<std::string>& log) {
  const size_t k = static_cast<size_t>(index);
  if (k >= st.drawing.lineCount())
    return;
  const double x0 = st.drawing.line(k).x0, y0 = st.drawing.line(k).y0, z0 = st.drawing.line(k).z0;
  const double x1 = st.drawing.line(k).x1, y1 = st.drawing.line(k).y1, z1 = st.drawing.line(k).z1;
  const double totalLen = std::hypot(x1 - x0, y1 - y0);
  constexpr double kTol = 0.002;  // REQ-101 endpoint-coincidence tolerance
  const double nearP = std::min(p1.param, p2.param), farP = std::max(p1.param, p2.param);
  const bool nearIsStart = nearP <= kTol;
  const bool farIsEnd = farP >= totalLen - kTol;
  if (nearIsStart && farIsEnd) {
    log.push_back("BREAK — that would remove the entire line; refused.");
    return;
  }
  const double ux = (x1 - x0) / std::max(totalLen, 1e-9), uy = (y1 - y0) / std::max(totalLen, 1e-9);
  PushUndoSnapshot(st, "Break");
  if (nearIsStart) {
    st.drawing.lineComponent(k, CadLineComponent::X0) = x0 + ux * farP;
    st.drawing.lineComponent(k, CadLineComponent::Y0) = y0 + uy * farP;
  } else if (farIsEnd) {
    st.drawing.lineComponent(k, CadLineComponent::X1) = x0 + ux * nearP;
    st.drawing.lineComponent(k, CadLineComponent::Y1) = y0 + uy * nearP;
  } else {
    const EntityAttributes srcAttrs =
        (static_cast<size_t>(index) < st.drawing.userLineAttrs.size()) ? st.drawing.userLineAttrs[static_cast<size_t>(index)]
                                                                : EntityAttributes{};
    const float nx = static_cast<float>(x0 + ux * nearP), ny = static_cast<float>(y0 + uy * nearP);
    const float fx = static_cast<float>(x0 + ux * farP), fy = static_cast<float>(y0 + uy * farP);
    st.drawing.lineComponent(k, CadLineComponent::X1) = nx;
    st.drawing.lineComponent(k, CadLineComponent::Y1) = ny;
    st.drawing.pushLine(CadLineRecord{fx, fy, z0, x1, y1, z1});
    st.drawing.userLineAttrs.push_back(DuplicatedEntityAttrs(srcAttrs));
  }
  BumpCadGpuCache(st);
  log.push_back("BREAK — line broken.");
}

static void ApplyBreakToCircle(AppCommandState& st, int index, const BreakPoint& p1, const BreakPoint& p2,
                               std::vector<std::string>& log) {
  if (index < 0 || static_cast<size_t>(index) >= st.drawing.circleCount())
    return;
  const CadCircleRecord c = st.drawing.circle(static_cast<size_t>(index));
  const float cx = static_cast<float>(c.cx), cy = static_cast<float>(c.cy);
  const float z = static_cast<float>(c.cz), r = static_cast<float>(c.r);
  float bnx = 0.f, bny = 0.f, bnz = 1.f;   // read before the erase below drops the side-car entry
  CircleNormalAt(st.drawing.userCircleNormals, static_cast<size_t>(index), &bnx, &bny, &bnz);
  float startRad = 0.f, sweepRad = 0.f;
  CircleBreakStartSweep(p1.theta, p2.theta, &startRad, &sweepRad);
  const EntityAttributes srcAttrs =
      (static_cast<size_t>(index) < st.drawing.userCircleAttrs.size()) ? st.drawing.userCircleAttrs[static_cast<size_t>(index)]
                                                                : EntityAttributes{};
  const bool samePoint = std::hypot(p1.x - p2.x, p1.y - p2.y) < 1e-4f;
  PushUndoSnapshot(st, "Break");
  st.drawing.eraseCircle(static_cast<size_t>(index));
  if (static_cast<size_t>(index) < st.drawing.userCircleAttrs.size())
    st.drawing.userCircleAttrs.erase(st.drawing.userCircleAttrs.begin() + index);
  EraseCircleNormal(st.drawing.userCircleNormals, static_cast<size_t>(index));
  CadArc a{};
  a.cx = cx;
  a.cy = cy;
  a.r = r;
  a.z = z;
  a.nx = bnx;   // the arc inherits the circle plane it was cut out of (REQ-312)
  a.ny = bny;
  a.nz = bnz;
  a.startRad = startRad;
  a.sweepRad = sweepRad;
  st.drawing.userArcs.push_back(a);
  st.drawing.userArcAttrs.push_back(DuplicatedEntityAttrs(srcAttrs));
  // TASK-099 F5. The erase above is the only COMPACTING erase in BREAK: every circle after `index`
  // just moved down one slot, so any SelectedEntity still holding a higher circle index now names a
  // different circle. ExecuteDeleteSelection clears the selection after exactly this compaction for
  // exactly this reason; BREAK does not clear the selection when it starts, so it must clear it
  // here. (The other BREAK paths mutate in place or append, and leave indices alone.)
  ClearCadSelection(st);
  BumpCadGpuCache(st);
  log.push_back(samePoint ? "BREAK — circle opened into an arc at the pick point."
                         : "BREAK — circle converted to an arc.");
}

static void ApplyBreakToArc(AppCommandState& st, int index, const BreakPoint& p1, const BreakPoint& p2,
                            std::vector<std::string>& log) {
  if (index < 0 || static_cast<size_t>(index) >= st.drawing.userArcs.size())
    return;
  const CadArc src = st.drawing.userArcs[static_cast<size_t>(index)];  // a copy — the push_back below may
                                                                // reallocate the vector
  constexpr float kTwoPi = 6.28318530717958647692f;
  const bool full = std::fabs(std::fabs(src.sweepRad) - kTwoPi) < 1e-4f;
  if (full) {
    float startRad = 0.f, sweepRad = 0.f;
    CircleBreakStartSweep(p1.theta, p2.theta, &startRad, &sweepRad);
    const bool samePoint = std::hypot(p1.x - p2.x, p1.y - p2.y) < 1e-4f;
    PushUndoSnapshot(st, "Break");
    st.drawing.userArcs[static_cast<size_t>(index)].startRad = startRad;
    st.drawing.userArcs[static_cast<size_t>(index)].sweepRad = sweepRad;
    BumpCadGpuCache(st);
    log.push_back(samePoint ? "BREAK — full-circle arc re-opened at the pick point."
                           : "BREAK — full-circle arc broken.");
    return;
  }
  const double totalLen = src.r * std::fabs(src.sweepRad);
  constexpr double kTol = 0.002;
  const double nearP = std::min(p1.param, p2.param), farP = std::max(p1.param, p2.param);
  const bool nearIsStart = nearP <= kTol;
  const bool farIsEnd = farP >= totalLen - kTol;
  if (nearIsStart && farIsEnd) {
    log.push_back("BREAK — that would remove the entire arc; refused.");
    return;
  }
  const float sgn = src.sweepRad >= 0.f ? 1.f : -1.f;
  const float nearTheta = static_cast<float>(src.startRad + sgn * (nearP / std::max<double>(src.r, 1e-9)));
  const float farTheta = static_cast<float>(src.startRad + sgn * (farP / std::max<double>(src.r, 1e-9)));
  const float endRad0 = src.startRad + src.sweepRad;
  PushUndoSnapshot(st, "Break");
  if (nearIsStart) {
    st.drawing.userArcs[static_cast<size_t>(index)].startRad = farTheta;
    st.drawing.userArcs[static_cast<size_t>(index)].sweepRad = endRad0 - farTheta;
  } else if (farIsEnd) {
    st.drawing.userArcs[static_cast<size_t>(index)].sweepRad = nearTheta - src.startRad;
  } else {
    const EntityAttributes srcAttrs =
        (static_cast<size_t>(index) < st.drawing.userArcAttrs.size()) ? st.drawing.userArcAttrs[static_cast<size_t>(index)]
                                                              : EntityAttributes{};
    CadArc newA = src;
    newA.startRad = farTheta;
    newA.sweepRad = endRad0 - farTheta;
    st.drawing.userArcs.push_back(newA);  // may reallocate — index-based access only below
    st.drawing.userArcAttrs.push_back(DuplicatedEntityAttrs(srcAttrs));
    st.drawing.userArcs[static_cast<size_t>(index)].sweepRad = nearTheta - src.startRad;
  }
  BumpCadGpuCache(st);
  log.push_back("BREAK — arc broken.");
}

/// One polyline vertex, with its elevation.
///
/// Exists because the two rewrite helpers below used to take `std::vector<std::pair<float,float>>`
/// and write a literal `0.f` into the Z slot: a caller could not pass an elevation because there
/// was nowhere to put one. That single missing field is why BREAK, FILLET and CHAMFER all flattened
/// a polyline to datum (issue 01) — four commands, one root cause. Carrying Z here makes every
/// caller supply it or fail to compile, which is the point.
/// A polyline vertex being rewritten by FILLET / CHAMFER. Replaces the `std::pair<float, float>`
/// these helpers used to carry, which had nowhere to put a Z and so discarded every elevation it
/// touched (issue 01).
///
/// `float`, deliberately, and not because the store is: `userPolylineVerts` is `std::vector<double>`
/// since ADR-054 Phase A, but the X and Y in this record already arrive through `readVert`, which
/// narrows them. Z rides at the same width as the X and Y it belongs to rather than being the one
/// wide member of a narrow record — widening this path is ADR-054's own audit to make (REQ-101
/// Phase D), not this fix's. The casts at the push_back sites are explicit so that decision is

/// Appends a brand-new polyline to the end of the CSR arrays. Precondition: at least one polyline
/// already exists (BREAK only ever calls this while splitting an already-selected polyline), so
/// `userPolylineOffsets` is never empty here.
static void AppendNewPolyline(AppCommandState& st, const std::vector<PolyVert>& verts, bool closed,
                              EntityAttributes attrs) {
  const int base = st.drawing.userPolylineOffsets.back();
  for (const auto& p : verts) {
    st.drawing.pushVertex(CadPolyVertex{p.x, p.y, p.z});
  }
  st.drawing.userPolylineOffsets.push_back(base + static_cast<int>(verts.size()));
  st.drawing.userPolylineClosed.push_back(closed ? 1u : 0u);
  st.drawing.userPolylineAttrs.push_back(std::move(attrs));
  SyncPolylineBulge(st.drawing.userPolylineVertsBulge, st.drawing.userPolylineVerts.size());  // REQ-316 / ADR-047
  SyncPolylineNormal(st.drawing.userPolylineVertsNormal, st.drawing.userPolylineVerts.size());  // REQ-325 / ADR-053
}
static void ApplyBreakToOpenPolyline(AppCommandState& st, int pi, const BreakPoint& p1, const BreakPoint& p2,
                                     std::vector<std::string>& log) {
  const double totalLen = PolylineOpenLengthOf(st, pi);
  constexpr double kTol = 0.002;
  const bool p1First = p1.param <= p2.param;
  const BreakPoint& nearBp = p1First ? p1 : p2;
  const BreakPoint& farBp = p1First ? p2 : p1;
  const bool nearIsStart = nearBp.param <= kTol;
  const bool farIsEnd = farBp.param >= totalLen - kTol;
  if (nearIsStart && farIsEnd) {
    log.push_back("BREAK — that would remove the entire polyline; refused.");
    return;
  }
  const std::vector<PolyVert> orig = PolylineVertsOf(st, pi);

  auto buildPiece = [&](bool fromStart) {
    std::vector<PolyVert> out;
    if (fromStart) {
      for (int i = 0; i <= nearBp.segIndex; ++i)
        out.push_back(orig[static_cast<size_t>(i)]);
      const auto& last = out.back();
      if (std::hypot(last.x - nearBp.x, last.y - nearBp.y) > 1e-6f)
        out.push_back({nearBp.x, nearBp.y, nearBp.z});
    } else {
      out.push_back({farBp.x, farBp.y, farBp.z});
      for (int i = farBp.segIndex + 1; i < static_cast<int>(orig.size()); ++i)
        out.push_back(orig[static_cast<size_t>(i)]);
      if (out.size() >= 2 && std::hypot(out[0].x - out[1].x, out[0].y - out[1].y) < 1e-6f)
        out.erase(out.begin());
    }
    return out;
  };

  const EntityAttributes srcAttrs =
      (static_cast<size_t>(pi) < st.drawing.userPolylineAttrs.size()) ? st.drawing.userPolylineAttrs[static_cast<size_t>(pi)]
                                                               : EntityAttributes{};
  PushUndoSnapshot(st, "Break");
  if (nearIsStart) {
    ReplacePolylineVerts(st, pi, buildPiece(false));
  } else if (farIsEnd) {
    ReplacePolylineVerts(st, pi, buildPiece(true));
  } else {
    const std::vector<PolyVert> nearPiece = buildPiece(true);
    const std::vector<PolyVert> farPiece = buildPiece(false);
    ReplacePolylineVerts(st, pi, nearPiece);
    AppendNewPolyline(st, farPiece, false, DuplicatedEntityAttrs(srcAttrs));
  }
  BumpCadGpuCache(st);
  log.push_back("BREAK — polyline broken.");
}

static void ApplyBreakToClosedPolyline(AppCommandState& st, int pi, const BreakPoint& p1, const BreakPoint& p2,
                                       std::vector<std::string>& log) {
  const int v0 = st.drawing.userPolylineOffsets[static_cast<size_t>(pi)];
  const int v1 = st.drawing.userPolylineOffsets[(static_cast<size_t>(pi)+ 1)];
  const int n = v1 - v0;  // vertex count == edge count for a closed ring
  std::vector<float> vparam(static_cast<size_t>(n));
  float ringLen = 0.f;
  for (int i = 0; i < n; ++i) {
    vparam[static_cast<size_t>(i)] = ringLen;
    const int a = v0 + i, b = v0 + (i + 1) % n;
    ringLen += static_cast<float>(std::hypot(st.drawing.vertex(b).x - st.drawing.vertex(a).x,
                              st.drawing.vertex(b).y -
                              st.drawing.vertex(a).y));
  }
  if (ringLen < 1e-9f)
    return;
  // Rotate every position so point2's param becomes 0, then collect everything from point2
  // (inclusive) through point1 (inclusive) in ascending rotated order — "walk forward from point2
  // to point1, wrapping through the closing edge" expressed as a sort instead of segment-index
  // bookkeeping, so a same-edge-reversed pick needs no special case.
  auto rot = [&](float param) {
    float d = std::fmod(param - p2.param, ringLen);
    if (d < 0.f) d += ringLen;
    return d;
  };
  const bool samePoint = std::hypot(p1.x - p2.x, p1.y - p2.y) < 1e-4f;
  const float p1Rot = samePoint ? ringLen : rot(p1.param);
  // Elevation rides along with each kept vertex, and the two cut points bring their own
  // interpolated Z (issue 01) - this used to rebuild the ring flat at datum.
  std::vector<PolyVert> outVerts;
  outVerts.push_back({p2.x, p2.y, p2.z});
  for (int i = 0; i < n; ++i) {
    const float r = rot(vparam[static_cast<size_t>(i)]);
    if (r > 1e-6f && r < p1Rot - 1e-6f) {
      const size_t o = (static_cast<size_t>(v0)+ i);
      outVerts.push_back({static_cast<float>(st.drawing.vertex(o).x),
                          static_cast<float>(st.drawing.vertex(o).y),
                          static_cast<float>(st.drawing.vertex(o).z)});
    }
  }
  outVerts.push_back({p1.x, p1.y, p1.z});
  if (outVerts.size() >= 2 &&
      std::hypot(outVerts[0].x - outVerts[1].x, outVerts[0].y - outVerts[1].y) < 1e-6f)
    outVerts.erase(outVerts.begin() + 1);
  if (outVerts.size() >= 2 &&
      std::hypot(outVerts.back().x - outVerts[outVerts.size() - 2].x,
                 outVerts.back().y - outVerts[outVerts.size() - 2].y) < 1e-6f)
    outVerts.pop_back();
  PushUndoSnapshot(st, "Break");
  ReplacePolylineVerts(st, pi, outVerts);
  if (static_cast<size_t>(pi) < st.drawing.userPolylineClosed.size())
    st.drawing.userPolylineClosed[static_cast<size_t>(pi)] = 0u;
  BumpCadGpuCache(st);
  log.push_back(samePoint ? "BREAK — closed polyline opened at the pick point."
                         : "BREAK — closed polyline broken open.");
}

static void ApplyBreakToPolyline(AppCommandState& st, int pi, const BreakPoint& p1, const BreakPoint& p2,
                                 std::vector<std::string>& log) {
  const bool closed =
      static_cast<size_t>(pi) < st.drawing.userPolylineClosed.size() && st.drawing.userPolylineClosed[static_cast<size_t>(pi)];
  if (closed)
    ApplyBreakToClosedPolyline(st, pi, p1, p2, log);
  else
    ApplyBreakToOpenPolyline(st, pi, p1, p2, log);
}

/// Single choke point, dispatching by entity type — mirrors `ApplyLengthenToEntity`'s shape.
static void ApplyBreakToEntity(AppCommandState& st, const SelectedEntity& e, const BreakPoint& p1,
                               const BreakPoint& p2, std::vector<std::string>& log) {
  switch (e.type) {
  case SelectedEntity::Type::LineSeg: ApplyBreakToLine(st, e.index, p1, p2, log); break;
  case SelectedEntity::Type::Circle:  ApplyBreakToCircle(st, e.index, p1, p2, log); break;
  case SelectedEntity::Type::Arc:     ApplyBreakToArc(st, e.index, p1, p2, log); break;
  case SelectedEntity::Type::Polyline: ApplyBreakToPolyline(st, e.index, p1, p2, log); break;
  default: break;
  }
}

void StartBreakCommand(AppCommandState& st, std::vector<std::string>& log) {
  ResetPaperModifyDrafts(st);
  if (st.activeSpaceIndex != kModelSpaceIndex && !InFloatingModelSpace(st)) {
    stepengine::EndSession(st);
    st.paperBreakPhase = 1;
    log.push_back("BREAK — click an object (the pick is break point 1). Esc cancels.");
    return;
  }
  ClearPendingViewportZoom(st);
  stepengine::BeginSession(st, std::make_unique<stepengine::BreakSession>(), true);
  log.push_back("BREAK — select object (the pick is break point 1). ESC cancels.");
}

void HandleBreakViewportPick(AppCommandState& st, float wx, float wy, std::vector<std::string>& log) {
  auto* brk = st.sessionAs<stepengine::BreakSession>();
  if (!brk)
    return;
  using BP = stepengine::BreakPhase;
  if (brk->phase == BP::SelectFirstPoint) {
    SelectedEntity hit{};
    float d2 = 0.f;
    if (!PickClosestCadEntity(st, wx, wy, CadOffsetEntityPickTolWorld(st), &hit, &d2)) {
      log.push_back("BREAK — no object at pick.");
      return;
    }
    if (hit.type == SelectedEntity::Type::Ellipse) {
      log.push_back("BREAK — 1 ellipse ignored: every ellipse in this drawing is a full closed curve, "
                    "and GoSurvey has no elliptical-arc entity kind to hold a broken-open ellipse. "
                    "Pick a line, circle, arc, or polyline.");
      return;
    }
    if (hit.type == SelectedEntity::Type::FeatureLine) {
      log.push_back("BREAK — 1 feature line ignored: a feature line cannot be broken. Pick a line, "
                    "circle, arc, or polyline.");
      return;
    }
    if (hit.type == SelectedEntity::Type::Surface) {
      log.push_back("BREAK — 1 surface ignored: a surface cannot be broken. Pick a line, circle, arc, "
                    "or polyline.");
      return;
    }
    BreakPoint p1{};
    if (!ClosestPointOnEntity(st, hit, wx, wy, &p1)) {
      log.push_back("BREAK — could not resolve a point on that object; try again.");
      return;
    }
    brk->entity = hit;
    brk->p1 = p1;
    brk->phase = BP::SelectSecondPoint;
    log.push_back("BREAK — specify second break point:");
    return;
  }

  // SelectSecondPoint.
  BreakPoint p2{};
  if (!ClosestPointOnEntity(st, brk->entity, wx, wy, &p2)) {
    log.push_back("BREAK — could not resolve a second point on that object; try again.");
    return;
  }
  ApplyBreakToEntity(st, brk->entity, brk->p1, p2, log);
  brk->phase = BP::SelectFirstPoint;
  brk->entity = SelectedEntity{};
}

/// Paper-space equivalent of `ClosestPointOnEntity`, against `PaperLayout`'s stores. Paper circles
/// are stride 3 (cx,cy,r — paper z is always 0, ADR-025 (g)), unlike the model store's stride 4.
bool ClosestPointOnPaperEntity(const PaperLayout& L, const PaperEntityRef& ref, float px, float py,
                               BreakPoint* out) {
  using T = PaperEntityRef::Type;
  switch (ref.type) {
  case T::Line: {
    const size_t k = static_cast<size_t>(ref.index) * 6;
    if (k + 5 >= L.paperLines.size())
      return false;
    const float x0 = L.paperLines[k], y0 = L.paperLines[k + 1];
    ClosestPointOnSegment(x0, y0, L.paperLines[k + 3], L.paperLines[k + 4], px, py, &out->x, &out->y);
    out->param = std::hypot(out->x - x0, out->y - y0);
    return true;
  }
  case T::Circle: {
    const size_t k = static_cast<size_t>(ref.index) * 3;
    if (k + 2 >= L.paperCircles.size())
      return false;
    const float cx = L.paperCircles[k], cy = L.paperCircles[k + 1], r = L.paperCircles[k + 2];
    out->theta = std::atan2(py - cy, px - cx);
    out->x = cx + r * std::cos(out->theta);
    out->y = cy + r * std::sin(out->theta);
    return true;
  }
  case T::Arc: {
    if (ref.index < 0 || static_cast<size_t>(ref.index) >= L.paperArcs.size())
      return false;
    const CadArc& a = L.paperArcs[static_cast<size_t>(ref.index)];
    out->theta = static_cast<float>(std::atan2(py - a.cy, px - a.cx));
    out->x = static_cast<float>(a.cx + a.r * std::cos(out->theta));
    out->y = static_cast<float>(a.cy + a.r * std::sin(out->theta));
    out->param = static_cast<float>(a.r * ArcSweepParam(a.startRad, a.sweepRad, out->theta));
    return true;
  }
  case T::Polyline: {
    const int pi = ref.index;
    if (pi < 0 || (static_cast<size_t>(pi)+ 1) >= L.paperPolyOffsets.size())
      return false;
    const int v0 = L.paperPolyOffsets[static_cast<size_t>(pi)];
    const int v1 = L.paperPolyOffsets[(static_cast<size_t>(pi)+ 1)];
    const bool closed =
        static_cast<size_t>(pi) < L.paperPolyClosed.size() && L.paperPolyClosed[static_cast<size_t>(pi)];
    bool any = false;
    float bestD2 = std::numeric_limits<float>::max();
    float cum = 0.f;
    for (int vi = v0; vi + 1 < v1; ++vi) {
      const size_t A = static_cast<size_t>(vi) * 3, B = (static_cast<size_t>(vi)+ 1) * 3;
      const float ax = L.paperPolyVerts[A], ay = L.paperPolyVerts[A + 1];
      const float bx = L.paperPolyVerts[B], by = L.paperPolyVerts[B + 1];
      float qx = 0.f, qy = 0.f;
      ClosestPointOnSegment(ax, ay, bx, by, px, py, &qx, &qy);
      const float d2 = (qx - px) * (qx - px) + (qy - py) * (qy - py);
      if (d2 < bestD2) {
        bestD2 = d2;
        any = true;
        out->x = qx;
        out->y = qy;
        out->param = cum + std::hypot(qx - ax, qy - ay);
        out->segIndex = vi - v0;
      }
      cum += std::hypot(bx - ax, by - ay);
    }
    if (closed && v1 - v0 >= 2) {
      const size_t A = (static_cast<size_t>(v1)- 1) * 3, B = static_cast<size_t>(v0) * 3;
      const float ax = L.paperPolyVerts[A], ay = L.paperPolyVerts[A + 1];
      const float bx = L.paperPolyVerts[B], by = L.paperPolyVerts[B + 1];
      float qx = 0.f, qy = 0.f;
      ClosestPointOnSegment(ax, ay, bx, by, px, py, &qx, &qy);
      const float d2 = (qx - px) * (qx - px) + (qy - py) * (qy - py);
      if (d2 < bestD2) {
        bestD2 = d2; // NOLINT(clang-analyzer-deadcode.DeadStores) - last candidate of this search; nothing reads it after
        any = true;
        out->x = qx;
        out->y = qy;
        out->param = cum + std::hypot(qx - ax, qy - ay);
        out->segIndex = (v1 - v0) - 1;
      }
    }
    return any;
  }
  default:
    return false;
  }
}

static bool ApplyBreakToPaperLine(AppCommandState& st, PaperLayout* L, int index, const BreakPoint& p1,
                                  const BreakPoint& p2, std::vector<std::string>& log) {
  const size_t k = static_cast<size_t>(index) * 6;
  if (k + 5 >= L->paperLines.size())
    return false;
  const float x0 = L->paperLines[k], y0 = L->paperLines[k + 1];
  const float x1 = L->paperLines[k + 3], y1 = L->paperLines[k + 4];
  const float totalLen = std::hypot(x1 - x0, y1 - y0);
  constexpr float kTol = 0.01f;
  const float nearP = std::min(p1.param, p2.param), farP = std::max(p1.param, p2.param);
  const bool nearIsStart = nearP <= kTol;
  const bool farIsEnd = farP >= totalLen - kTol;
  if (nearIsStart && farIsEnd) {
    log.push_back("BREAK — that would remove the entire line; refused.");
    return false;
  }
  PushUndoSnapshot(st, "Break paper geometry");
  const float ux = (x1 - x0) / std::max(totalLen, 1e-9f), uy = (y1 - y0) / std::max(totalLen, 1e-9f);
  if (nearIsStart) {
    L->paperLines[k] = x0 + ux * farP;
    L->paperLines[k + 1] = y0 + uy * farP;
  } else if (farIsEnd) {
    L->paperLines[k + 3] = x0 + ux * nearP;
    L->paperLines[k + 4] = y0 + uy * nearP;
  } else {
    const EntityAttributes srcAttrs =
        (static_cast<size_t>(index) < L->paperLineAttrs.size()) ? L->paperLineAttrs[static_cast<size_t>(index)]
                                                                 : EntityAttributes{};
    const float nx = x0 + ux * nearP, ny = y0 + uy * nearP;
    const float fx = x0 + ux * farP, fy = y0 + uy * farP;
    L->paperLines[k + 3] = nx;
    L->paperLines[k + 4] = ny;
    L->paperLines.insert(L->paperLines.end(), {fx, fy, 0.f, x1, y1, 0.f});
    L->paperLineAttrs.push_back(srcAttrs);  // paper: id copied verbatim (existing precedent, not a
                                            // new policy — paper entities are outside REQ-076's sweep)
  }
  log.push_back("BREAK — paper line broken.");
  return true;
}

static bool ApplyBreakToPaperCircle(AppCommandState& st, PaperLayout* L, int index, const BreakPoint& p1,
                                    const BreakPoint& p2, std::vector<std::string>& log) {
  const size_t k = static_cast<size_t>(index) * 3;
  if (k + 2 >= L->paperCircles.size())
    return false;
  const float cx = L->paperCircles[k], cy = L->paperCircles[k + 1], r = L->paperCircles[k + 2];
  float startRad = 0.f, sweepRad = 0.f;
  CircleBreakStartSweep(p1.theta, p2.theta, &startRad, &sweepRad);
  const EntityAttributes srcAttrs =
      (static_cast<size_t>(index) < L->paperCircleAttrs.size()) ? L->paperCircleAttrs[static_cast<size_t>(index)]
                                                                 : EntityAttributes{};
  const bool samePoint = std::hypot(p1.x - p2.x, p1.y - p2.y) < 1e-4f;
  PushUndoSnapshot(st, "Break paper geometry");
  L->paperCircles.erase(L->paperCircles.begin() + static_cast<std::ptrdiff_t>(k),
                        L->paperCircles.begin() + (static_cast<std::ptrdiff_t>(k)+ 3));
  if (static_cast<size_t>(index) < L->paperCircleAttrs.size())
    L->paperCircleAttrs.erase(L->paperCircleAttrs.begin() + index);
  CadArc a{};
  a.cx = cx;
  a.cy = cy;
  a.r = r;
  a.startRad = startRad;
  a.sweepRad = sweepRad;
  L->paperArcs.push_back(a);
  L->paperArcAttrs.push_back(srcAttrs);
  log.push_back(samePoint ? "BREAK — paper circle opened into an arc at the pick point."
                         : "BREAK — paper circle converted to an arc.");
  return true;
}

static bool ApplyBreakToPaperArc(AppCommandState& st, PaperLayout* L, int index, const BreakPoint& p1,
                                 const BreakPoint& p2, std::vector<std::string>& log) {
  if (index < 0 || static_cast<size_t>(index) >= L->paperArcs.size())
    return false;
  const CadArc src = L->paperArcs[static_cast<size_t>(index)];
  constexpr float kTwoPi = 6.28318530717958647692f;
  const bool full = std::fabs(std::fabs(src.sweepRad) - kTwoPi) < 1e-4f;
  if (full) {
    float startRad = 0.f, sweepRad = 0.f;
    CircleBreakStartSweep(p1.theta, p2.theta, &startRad, &sweepRad);
    const bool samePoint = std::hypot(p1.x - p2.x, p1.y - p2.y) < 1e-4f;
    PushUndoSnapshot(st, "Break paper geometry");
    L->paperArcs[static_cast<size_t>(index)].startRad = startRad;
    L->paperArcs[static_cast<size_t>(index)].sweepRad = sweepRad;
    log.push_back(samePoint ? "BREAK — paper full-circle arc re-opened at the pick point."
                           : "BREAK — paper full-circle arc broken.");
    return true;
  }
  const float totalLen = static_cast<float>(src.r * std::fabs(src.sweepRad));
  constexpr float kTol = 0.01f;
  const float nearP = std::min(p1.param, p2.param), farP = std::max(p1.param, p2.param);
  const bool nearIsStart = nearP <= kTol;
  const bool farIsEnd = farP >= totalLen - kTol;
  if (nearIsStart && farIsEnd) {
    log.push_back("BREAK — that would remove the entire arc; refused.");
    return false;
  }
  const float sgn = src.sweepRad >= 0.f ? 1.f : -1.f;
  const float nearTheta = static_cast<float>(src.startRad + sgn * (nearP / std::max<double>(src.r, 1e-9)));
  const float farTheta = static_cast<float>(src.startRad + sgn * (farP / std::max<double>(src.r, 1e-9)));
  const float endRad0 = src.startRad + src.sweepRad;
  PushUndoSnapshot(st, "Break paper geometry");
  if (nearIsStart) {
    L->paperArcs[static_cast<size_t>(index)].startRad = farTheta;
    L->paperArcs[static_cast<size_t>(index)].sweepRad = endRad0 - farTheta;
  } else if (farIsEnd) {
    L->paperArcs[static_cast<size_t>(index)].sweepRad = nearTheta - src.startRad;
  } else {
    const EntityAttributes srcAttrs =
        (static_cast<size_t>(index) < L->paperArcAttrs.size()) ? L->paperArcAttrs[static_cast<size_t>(index)]
                                                                : EntityAttributes{};
    CadArc newA = src;
    newA.startRad = farTheta;
    newA.sweepRad = endRad0 - farTheta;
    L->paperArcs.push_back(newA);
    L->paperArcAttrs.push_back(srcAttrs);
    L->paperArcs[static_cast<size_t>(index)].sweepRad = nearTheta - src.startRad;
  }
  log.push_back("BREAK — paper arc broken.");
  return true;
}


/// Paper equivalent of `AppendNewPolyline`. Same precondition (at least one paper polyline already
/// exists — BREAK only calls this while splitting one).
static void AppendNewPaperPolyline(PaperLayout* L, const std::vector<std::pair<float, float>>& xy, bool closed,
                                   EntityAttributes attrs) {
  const int base = L->paperPolyOffsets.back();
  for (const auto& p : xy) {
    L->paperPolyVerts.push_back(p.first);
    L->paperPolyVerts.push_back(p.second);
    L->paperPolyVerts.push_back(0.f);
  }
  L->paperPolyOffsets.push_back(base + static_cast<int>(xy.size()));
  L->paperPolyClosed.push_back(closed ? 1u : 0u);
  L->paperPolyAttrs.push_back(std::move(attrs));
  SyncPolylineBulge(L->paperPolyVertsBulge, L->paperPolyVerts.size());
}

static bool ApplyBreakToPaperOpenPolyline(AppCommandState& st, PaperLayout* L, int pi, const BreakPoint& p1,
                                          const BreakPoint& p2, std::vector<std::string>& log) {
  const int v0 = L->paperPolyOffsets[static_cast<size_t>(pi)];
  const int v1 = L->paperPolyOffsets[(static_cast<size_t>(pi)+ 1)];
  float totalLen = 0.f;
  for (int vi = v0; vi + 1 < v1; ++vi) {
    const size_t A = static_cast<size_t>(vi) * 3, B = (static_cast<size_t>(vi)+ 1) * 3;
    totalLen += std::hypot(L->paperPolyVerts[B] - L->paperPolyVerts[A], L->paperPolyVerts[B + 1] - L->paperPolyVerts[A + 1]);
  }
  constexpr float kTol = 0.01f;
  const bool p1First = p1.param <= p2.param;
  const BreakPoint& nearBp = p1First ? p1 : p2;
  const BreakPoint& farBp = p1First ? p2 : p1;
  const bool nearIsStart = nearBp.param <= kTol;
  const bool farIsEnd = farBp.param >= totalLen - kTol;
  if (nearIsStart && farIsEnd) {
    log.push_back("BREAK — that would remove the entire polyline; refused.");
    return false;
  }
  std::vector<std::pair<float, float>> orig;
  orig.reserve((static_cast<size_t>(v1)- v0));
  for (int vi = v0; vi < v1; ++vi)
    orig.push_back({L->paperPolyVerts[static_cast<size_t>(vi) * 3], L->paperPolyVerts[static_cast<size_t>(vi) * 3 + 1]});

  auto buildPiece = [&](bool fromStart) {
    std::vector<std::pair<float, float>> out;
    if (fromStart) {
      for (int i = 0; i <= nearBp.segIndex; ++i)
        out.push_back(orig[static_cast<size_t>(i)]);
      const auto& last = out.back();
      if (std::hypot(last.first - nearBp.x, last.second - nearBp.y) > 1e-6f)
        out.push_back({nearBp.x, nearBp.y});
    } else {
      out.push_back({farBp.x, farBp.y});
      for (int i = farBp.segIndex + 1; i < static_cast<int>(orig.size()); ++i)
        out.push_back(orig[static_cast<size_t>(i)]);
      if (out.size() >= 2 &&
          std::hypot(out[0].first - out[1].first, out[0].second - out[1].second) < 1e-6f)
        out.erase(out.begin());
    }
    return out;
  };

  const EntityAttributes srcAttrs =
      (static_cast<size_t>(pi) < L->paperPolyAttrs.size()) ? L->paperPolyAttrs[static_cast<size_t>(pi)]
                                                            : EntityAttributes{};
  PushUndoSnapshot(st, "Break paper geometry");
  if (nearIsStart) {
    ReplacePaperPolylineVerts(L, pi, buildPiece(false));
  } else if (farIsEnd) {
    ReplacePaperPolylineVerts(L, pi, buildPiece(true));
  } else {
    std::vector<std::pair<float, float>> nearPiece = buildPiece(true);
    std::vector<std::pair<float, float>> farPiece = buildPiece(false);
    ReplacePaperPolylineVerts(L, pi, nearPiece);
    AppendNewPaperPolyline(L, farPiece, false, srcAttrs);
  }
  log.push_back("BREAK — paper polyline broken.");
  return true;
}

static bool ApplyBreakToPaperClosedPolyline(AppCommandState& st, PaperLayout* L, int pi, const BreakPoint& p1,
                                            const BreakPoint& p2, std::vector<std::string>& log) {
  const int v0 = L->paperPolyOffsets[static_cast<size_t>(pi)];
  const int v1 = L->paperPolyOffsets[(static_cast<size_t>(pi)+ 1)];
  const int n = v1 - v0;
  std::vector<float> vparam(static_cast<size_t>(n));
  float ringLen = 0.f;
  for (int i = 0; i < n; ++i) {
    vparam[static_cast<size_t>(i)] = ringLen;
    const int a = v0 + i, b = v0 + (i + 1) % n;
    ringLen += std::hypot(L->paperPolyVerts[static_cast<size_t>(b) * 3] - L->paperPolyVerts[static_cast<size_t>(a) * 3],
                          L->paperPolyVerts[static_cast<size_t>(b) * 3 + 1] -
                              L->paperPolyVerts[static_cast<size_t>(a) * 3 + 1]);
  }
  if (ringLen < 1e-9f)
    return false;
  auto rot = [&](float param) {
    float d = std::fmod(param - p2.param, ringLen);
    if (d < 0.f) d += ringLen;
    return d;
  };
  const bool samePoint = std::hypot(p1.x - p2.x, p1.y - p2.y) < 1e-4f;
  const float p1Rot = samePoint ? ringLen : rot(p1.param);
  std::vector<std::pair<float, float>> outVerts;
  outVerts.push_back({p2.x, p2.y});
  for (int i = 0; i < n; ++i) {
    const float r = rot(vparam[static_cast<size_t>(i)]);
    if (r > 1e-6f && r < p1Rot - 1e-6f)
      outVerts.push_back({L->paperPolyVerts[(static_cast<size_t>(v0)+ i) * 3],
                          L->paperPolyVerts[(static_cast<size_t>(v0)+ i) * 3 + 1]});
  }
  outVerts.push_back({p1.x, p1.y});
  if (outVerts.size() >= 2 &&
      std::hypot(outVerts[0].first - outVerts[1].first, outVerts[0].second - outVerts[1].second) < 1e-6f)
    outVerts.erase(outVerts.begin() + 1);
  if (outVerts.size() >= 2 &&
      std::hypot(outVerts.back().first - outVerts[outVerts.size() - 2].first,
                outVerts.back().second - outVerts[outVerts.size() - 2].second) < 1e-6f)
    outVerts.pop_back();
  PushUndoSnapshot(st, "Break paper geometry");
  ReplacePaperPolylineVerts(L, pi, outVerts);
  if (static_cast<size_t>(pi) < L->paperPolyClosed.size())
    L->paperPolyClosed[static_cast<size_t>(pi)] = 0u;
  log.push_back(samePoint ? "BREAK — closed paper polyline opened at the pick point."
                         : "BREAK — closed paper polyline broken open.");
  return true;
}

bool ApplyBreakToPaperEntity(AppCommandState& st, const PaperEntityRef& ref, const BreakPoint& p1, float pickXIn,
                             float pickYIn, std::vector<std::string>& log) {
  PaperLayout* L = ActivePaperGeometryTarget(st);
  if (!L)
    return false;
  BreakPoint p2{};
  if (!ClosestPointOnPaperEntity(*L, ref, pickXIn, pickYIn, &p2)) {
    log.push_back("BREAK — could not resolve a second point on that object; try again.");
    return false;
  }
  // Each ApplyBreakToPaper* function pushes its OWN undo snapshot, and only once it knows the break
  // will not be refused (mirrors the model-space functions' shape) — a blanket push here would
  // leave a spurious undo step behind on a "would remove the entire X" refusal.
  bool applied = false;
  switch (ref.type) {
  case PaperEntityRef::Type::Line:     applied = ApplyBreakToPaperLine(st, L, ref.index, p1, p2, log); break;
  case PaperEntityRef::Type::Circle:   applied = ApplyBreakToPaperCircle(st, L, ref.index, p1, p2, log); break;
  case PaperEntityRef::Type::Arc:      applied = ApplyBreakToPaperArc(st, L, ref.index, p1, p2, log); break;
  case PaperEntityRef::Type::Polyline: {
    const bool closed = static_cast<size_t>(ref.index) < L->paperPolyClosed.size() &&
                        L->paperPolyClosed[static_cast<size_t>(ref.index)];
    applied = closed ? ApplyBreakToPaperClosedPolyline(st, L, ref.index, p1, p2, log)
                     : ApplyBreakToPaperOpenPolyline(st, L, ref.index, p1, p2, log);
    break;
  }
  default: break;
  }
  if (applied)
    BumpCadGpuCache(st);
  return applied;
}

// ============================================================================
// STRETCH (REQ-103 step 5, TASK-098)
// ============================================================================

// RecomputeArcFromEndpoints now lives inline in CadCommands.hpp (unit-testable without linking
// this whole TU — see its doc comment there).

void StretchOneArc(CadArc& arc, float mnX, float mxX, float mnY, float mxY, float dx, float dy,
                   std::vector<std::string>& log) {
  constexpr float kTwoPi = 6.28318530717958647692f;
  if (std::fabs(std::fabs(arc.sweepRad) - kTwoPi) < 1e-4f) {
    // Full-circle sweep: endpoints coincide, so endpoint math is undefined — follow the Circle
    // rule instead, same tolerance BREAK's own full-circle guard uses.
    if (PointInsideClosedRect(static_cast<float>(arc.cx), static_cast<float>(arc.cy), mnX, mxX, mnY, mxY)) {
      arc.cx += dx;
      arc.cy += dy;
    }
    return;
  }
  const float sx = static_cast<float>(arc.cx + arc.r * std::cos(arc.startRad));
  const float sy = static_cast<float>(arc.cy + arc.r * std::sin(arc.startRad));
  const float endAng = arc.startRad + arc.sweepRad;
  const float ex = static_cast<float>(arc.cx + arc.r * std::cos(endAng));
  const float ey = static_cast<float>(arc.cy + arc.r * std::sin(endAng));
  const bool startIn = PointInsideClosedRect(sx, sy, mnX, mxX, mnY, mxY);
  const bool endIn = PointInsideClosedRect(ex, ey, mnX, mxX, mnY, mxY);
  if (!startIn && !endIn)
    return;  // selected but nothing to move — a legitimate no-op, matching AutoCAD.
  const float nsx = startIn ? sx + dx : sx;
  const float nsy = startIn ? sy + dy : sy;
  const float nex = endIn ? ex + dx : ex;
  const float ney = endIn ? ey + dy : ey;
  RecomputeArcFromEndpoints(arc, nsx, nsy, nex, ney, &log);
}

/// REQ-103 STRETCH, model-space apply. Mirrors \c ApplyTranslationToSelection's per-type loop shape,
/// but each entity's definition point(s) are tested against [mnX,mxX]x[mnY,mxY] individually — only
/// in-box points move by (dx,dy). Line/Polyline/FeatureLine vertices are independent (the genuine
/// stretch effect); Arc goes through \ref StretchOneArc; every other type has one definition point
/// and moves as a whole only if that point is in-box (matching AutoCAD's own behavior for them).
void ApplyStretchToSelection(AppCommandState& st, float dx, float dy, float dz, float mnX, float mxX,
                             float mnY, float mxY, bool rectInUcsPlane, std::vector<std::string>& log) {
  DropSurfacesFromSelectionForTransform(st, "STRETCH", log);
  DropPositionMarkersFromSelection(st, "STRETCH", log);  // REQ-359: move/copy/erase only
  DropMultileadersFromSelection(st, "STRETCH", log);     // REQ-367
  DropSolidsFromSelectionForTransform(st, "STRETCH", log);
  // REQ-329 increment 4: when the crossing box lives in a tilted work plane's local 2D frame, each
  // candidate vertex is projected the same way (WorldToPlane) before the box test; the displacement
  // carries a Z component. Under the World UCS `rectInUcsPlane` is false and `dz` is 0, so this is
  // the pre-REQ-329 flat test byte-for-byte.
  const ucs::Ucs stretchFrame = CadActiveUcsStorage(st);
  auto inBox = [&](float x, float y, float z = 0.f) {
    if (!rectInUcsPlane)
      return PointInsideClosedRect(x, y, mnX, mxX, mnY, mxY);
    const ucs::Point2D p = ucs::WorldToPlane(stretchFrame, {static_cast<double>(x), static_cast<double>(y),
                                                            static_cast<double>(z)});
    return PointInsideClosedRect(static_cast<float>(p.x), static_cast<float>(p.y), mnX, mxX, mnY, mxY);
  };

  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::LineSeg)
      continue;
    const size_t k = static_cast<size_t>(e.index);
    if (k >= st.drawing.lineCount())
      continue;
    if (inBox(static_cast<float>(st.drawing.line(k).x0), static_cast<float>(st.drawing.line(k).y0), static_cast<float>(st.drawing.line(k).z0))) {
      st.drawing.lineComponent(k, CadLineComponent::X0) += dx;
      st.drawing.lineComponent(k, CadLineComponent::Y0) += dy;
      st.drawing.lineComponent(k, CadLineComponent::Z0) += dz;
    }
    if (inBox(static_cast<float>(st.drawing.line(k).x1), static_cast<float>(st.drawing.line(k).y1), static_cast<float>(st.drawing.line(k).z1))) {
      st.drawing.lineComponent(k, CadLineComponent::X1) += dx;
      st.drawing.lineComponent(k, CadLineComponent::Y1) += dy;
      st.drawing.lineComponent(k, CadLineComponent::Z1) += dz;
    }
  }
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::Circle)
      continue;
    const size_t ci = static_cast<size_t>(e.index);
    if (ci >= st.drawing.circleCount())
      continue;
    CadCircleRecord cr = st.drawing.circle(ci);
    if (inBox(static_cast<float>(cr.cx), static_cast<float>(cr.cy), static_cast<float>(cr.cz))) {
      cr.cx += dx;
      cr.cy += dy;
      cr.cz += dz;
      st.drawing.setCircle(ci, cr);
    }
  }
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::Arc)
      continue;
    const size_t k = static_cast<size_t>(e.index);
    if (k >= st.drawing.userArcs.size())
      continue;
    if (rectInUcsPlane) {
      // A tilted arc's endpoint math (`cx + r*cos`) is planar; degrade to a whole-arc move when the
      // centre is in the box, matching the FilledRegion simplification just below.
      CadArc& a = st.drawing.userArcs[k];
      if (inBox(static_cast<float>(a.cx), static_cast<float>(a.cy), static_cast<float>(a.z))) {
        a.cx += dx;
        a.cy += dy;
        a.z += dz;
      }
    } else {
      StretchOneArc(st.drawing.userArcs[k], mnX, mxX, mnY, mxY, dx, dy, log);
    }
  }
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::Ellipse)
      continue;
    const size_t k = static_cast<size_t>(e.index);
    if (k >= st.drawing.userEllipses.size())
      continue;
    if (inBox(static_cast<float>(st.drawing.userEllipses[k].cx), static_cast<float>(st.drawing.userEllipses[k].cy), static_cast<float>(st.drawing.userEllipses[k].z))) {
      st.drawing.userEllipses[k].cx += dx;
      st.drawing.userEllipses[k].cy += dy;
      st.drawing.userEllipses[k].z += dz;
    }
  }
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::Polyline)
      continue;
    const int pi = e.index;
    if (pi < 0 || (static_cast<size_t>(pi)+ 1) >= st.drawing.userPolylineOffsets.size())
      continue;
    const int v0 = st.drawing.userPolylineOffsets[static_cast<size_t>(pi)];
    const int v1 = st.drawing.userPolylineOffsets[(static_cast<size_t>(pi)+ 1)];
    for (int vi = v0; vi < v1; ++vi) {
      const size_t b = static_cast<size_t>(vi);
      if (inBox(static_cast<float>(st.drawing.vertex(b).x), static_cast<float>(st.drawing.vertex(b).y), static_cast<float>(st.drawing.vertex(b).z))) {
        st.drawing.vertexComponent(b, CadVertexComponent::X) += dx;
        st.drawing.vertexComponent(b, CadVertexComponent::Y) += dy;
        st.drawing.vertexComponent(b, CadVertexComponent::Z) += dz;
      }
    }
  }
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::Annotation)
      continue;
    const size_t k = static_cast<size_t>(e.index);
    if (k >= st.drawing.cadAnnotations.size())
      continue;
    CadAnnotation& a = st.drawing.cadAnnotations[k];
    if (!inBox(a.insX, a.insY, a.insZ))
      continue;
    a.insX += dx;
    a.insY += dy;
    a.insZ += dz;
    if (a.kind == CadAnnotation::Kind::Mtext) {
      a.boxMinX += dx;
      a.boxMinY += dy;
      a.boxMaxX += dx;
      a.boxMaxY += dy;
    } else if (a.kind == CadAnnotation::Kind::DimAligned || a.kind == CadAnnotation::Kind::DimLinear) {
      a.dimExt1X += dx;
      a.dimExt1Y += dy;
      a.dimExt2X += dx;
      a.dimExt2Y += dy;
    }
  }
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::PdfUnderlay)
      continue;
    if (e.index < 0 || static_cast<size_t>(e.index) >= st.drawing.pdfAttachments.size())
      continue;
    PdfAttachment& att = st.drawing.pdfAttachments[static_cast<size_t>(e.index)];
    if (inBox(att.insertX, att.insertY)) {  // PDF underlay carries no elevation
      att.insertX += dx;
      att.insertY += dy;
    }
  }
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::Table)
      continue;
    if (e.index < 0 || static_cast<size_t>(e.index) >= st.drawing.cadTables.size())
      continue;
    CadTable& t = st.drawing.cadTables[static_cast<size_t>(e.index)];
    if (inBox(t.insX, t.insY, t.insZ)) {
      CadTableTranslate(&t, dx, dy);
      t.insZ += dz;
    }
  }
  // Filled regions (REQ-042): whole-region translate, gated on the first boundary vertex — no
  // per-vertex boundary stretch (spec-recorded simplification, REQ-103 STRETCH acceptance).
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::FilledRegion)
      continue;
    if (e.index < 0 || static_cast<size_t>(e.index) >= st.drawing.cadFilledRegions.size())
      continue;
    CadFilledRegion& fr = st.drawing.cadFilledRegions[static_cast<size_t>(e.index)];
    if (fr.vertsXyz.size() >= 3 && inBox(static_cast<float>(fr.vertsXyz[0]), static_cast<float>(fr.vertsXyz[1]), static_cast<float>(fr.vertsXyz[2]))) {
      hatchgeom::Translate(fr, dx, dy);
      if (dz != 0.f)
        for (size_t i = 2; i < fr.vertsXyz.size(); i += 3)
          fr.vertsXyz[i] += dz;
    }
  }
  // Feature lines (REQ-087): per-vertex. Elevation moves only under a tilted UCS (REQ-329
  // increment 4) — matching the "plan STRETCH leaves Z alone" restriction the other commands share.
  ForEachSelectedFeatureLine(st, [&](int /*fi*/, int v0, int v1) {
    for (int vi = v0; vi < v1; ++vi) {
      const size_t b = static_cast<size_t>(vi) * 3;
      if (inBox(static_cast<float>(st.drawing.featureLineVerts[b]), static_cast<float>(st.drawing.featureLineVerts[b + 1]), static_cast<float>(st.drawing.featureLineVerts[b + 2]))) {
        st.drawing.featureLineVerts[b] += dx;
        st.drawing.featureLineVerts[b + 1] += dy;
        st.drawing.featureLineVerts[b + 2] += dz;
      }
    }
  });
  {
    std::vector<int> ix = st.selectedSurveyPointIndices;
    std::sort(ix.begin(), ix.end());
    ix.erase(std::unique(ix.begin(), ix.end()), ix.end());
    for (int i : ix) {
      if (i < 0 || static_cast<size_t>(i) >= st.drawing.surveyPoints.size())
        continue;
      SurveyPoint& sp = st.drawing.surveyPoints[static_cast<size_t>(i)];
      if (inBox(static_cast<float>(sp.easting), static_cast<float>(sp.northing), static_cast<float>(sp.elevation))) {
        sp.easting += dx;
        sp.northing += dy;
        sp.elevation += dz;
      }
    }
    for (int i : ix) {
      if (i >= 0 && static_cast<size_t>(i) < st.drawing.surveyPoints.size())
        RepositionSurveyLabelMtextForPoint(st, static_cast<size_t>(i));
    }
  }
  BumpCadGpuCache(st);
}

/// REQ-103 STRETCH, pure-paper-space path. Mirrors \c TranslateSelectedPaperEntities's one-loop-
/// switch shape. If \c st.paperSelBoxLastValid, only each entity's definition point(s) that fall
/// inside the last-captured box move (true partial stretch for Line/Polyline/Arc — Arc via the
/// same \ref StretchOneArc the model path uses, since \c paperArcs is the same \c CadArc struct);
/// otherwise every selected entity translates as a whole (a plain click-select carries no box to
/// test against — degraded MOVE-equivalent, matching AutoCAD's own degradation for a non-crossing
/// pickfirst set).
void ApplyStretchToPaperSelection(AppCommandState& st, float dxIn, float dyIn, std::vector<std::string>& log) {
  PaperLayout* L = ActivePaperGeometryTarget(st);
  if (!L || st.selectedPaperEntities.empty())
    return;
  const bool haveBox = st.paperSelBoxLastValid;
  const float mnX = st.paperSelBoxLastMnXIn, mxX = st.paperSelBoxLastMxXIn;
  const float mnY = st.paperSelBoxLastMnYIn, mxY = st.paperSelBoxLastMxYIn;
  auto inBox = [&](float x, float y) { return !haveBox || PointInsideClosedRect(x, y, mnX, mxX, mnY, mxY); };
  PushUndoSnapshot(st, "Stretch paper geometry");
  for (const PaperRef& r : st.selectedPaperEntities) {
    switch (r.type) {
    case PaperRef::Type::Line: {
      const size_t i = static_cast<size_t>(r.index) * 6;
      if (i + 5 >= L->paperLines.size())
        break;
      if (inBox(L->paperLines[i], L->paperLines[i + 1])) {
        L->paperLines[i] += dxIn;
        L->paperLines[i + 1] += dyIn;
      }
      if (inBox(L->paperLines[i + 3], L->paperLines[i + 4])) {
        L->paperLines[i + 3] += dxIn;
        L->paperLines[i + 4] += dyIn;
      }
      break;
    }
    case PaperRef::Type::Circle: {
      const size_t i = static_cast<size_t>(r.index) * 3;
      if (i + 2 >= L->paperCircles.size())
        break;
      if (inBox(L->paperCircles[i], L->paperCircles[i + 1])) {
        L->paperCircles[i] += dxIn;
        L->paperCircles[i + 1] += dyIn;
      }
      break;
    }
    case PaperRef::Type::Arc: {
      if (r.index < 0 || static_cast<size_t>(r.index) >= L->paperArcs.size())
        break;
      CadArc& a = L->paperArcs[static_cast<size_t>(r.index)];
      if (haveBox)
        StretchOneArc(a, mnX, mxX, mnY, mxY, dxIn, dyIn, log);
      else {
        a.cx += dxIn;
        a.cy += dyIn;
      }
      break;
    }
    case PaperRef::Type::Ellipse: {
      if (r.index < 0 || static_cast<size_t>(r.index) >= L->paperEllipses.size())
        break;
      CadEllipse& e = L->paperEllipses[static_cast<size_t>(r.index)];
      if (inBox(static_cast<float>(e.cx), static_cast<float>(e.cy))) {
        e.cx += dxIn;
        e.cy += dyIn;
      }
      break;
    }
    case PaperRef::Type::Polyline: {
      const int pi = r.index;
      if (pi < 0 || (static_cast<size_t>(pi)+ 1) >= L->paperPolyOffsets.size())
        break;
      const int v0 = L->paperPolyOffsets[static_cast<size_t>(pi)];
      const int v1 = L->paperPolyOffsets[(static_cast<size_t>(pi)+ 1)];
      for (int vi = v0; vi < v1; ++vi) {
        const size_t b = static_cast<size_t>(vi) * 3;
        if (inBox(L->paperPolyVerts[b], L->paperPolyVerts[b + 1])) {
          L->paperPolyVerts[b] += dxIn;
          L->paperPolyVerts[b + 1] += dyIn;
        }
      }
      break;
    }
    case PaperRef::Type::Text: {
      if (r.index < 0 || static_cast<size_t>(r.index) >= L->paperTexts.size())
        break;
      CadAnnotation& a = L->paperTexts[static_cast<size_t>(r.index)];
      if (inBox(a.insX, a.insY)) {
        a.insX += dxIn;
        a.insY += dyIn;
      }
      break;
    }
    }
  }
  BumpCadGpuCache(st);
  log.push_back(haveBox ? "STRETCH — paper object(s) stretched."
                        : "STRETCH — paper object(s) moved (no crossing/window box on the current "
                          "selection).");
}

void StartStretchCommand(AppCommandState& st, std::vector<std::string>& log) {
  ResetPaperModifyDrafts(st);
  using K = AppCommandState::Kind;
  if (st.activeSpaceIndex != kModelSpaceIndex && !InFloatingModelSpace(st)) {
    if (st.selectedPaperEntities.empty()) {
      log.push_back("STRETCH — select paper object(s) first (a crossing/window box gives true "
                    "partial stretch; a plain click-select moves them as a whole).");
      return;
    }
    st.active = K::None;  // paper-space edit ops are not a model command (ROTATE/SCALE precedent)
    st.paperStretchPhase = 1;
    log.push_back("STRETCH — specify base point (click or type X,Y). ESC to cancel.");
    return;
  }
  ClearPendingViewportZoom(st);
  ClearSelection(st);
  stepengine::BeginSession(st, std::make_unique<stepengine::StretchSession>(), true);
  log.push_back(
      "STRETCH — click two corners to crossing/window-select objects (right-to-left = crossing), "
      "then base point and destination. ESC cancels.");
}
