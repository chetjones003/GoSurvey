#include "CadCommandsInternal.hpp"
#include "modify/CadTrimInternal.hpp"
#include "CadCoordinateFrame.hpp"
#include <GoSurvey/Geometry/curveintersect.hpp>
#include <GoSurvey/Geometry/geom2d.hpp>
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

namespace {

// The model trim solver already owns the project's intersection and pick-side rules. A paper
// layout uses the same value types but different stores, so stage its native geometry in a temporary
// command state, run that solver there, then copy only changed line/polyline stores back.
static bool StagePaperTrimGeometry(const PaperLayout& layout, AppCommandState* staged) {
  if (!staged)
    return false;
  staged->drawing.userLinesFlat.assign(layout.paperLines.begin(), layout.paperLines.end());
  staged->drawing.userLineAttrs = layout.paperLineAttrs;
  staged->drawing.userArcs = layout.paperArcs;
  staged->drawing.userArcAttrs = layout.paperArcAttrs;
  staged->drawing.userEllipses = layout.paperEllipses;
  staged->drawing.userEllAttrs = layout.paperEllAttrs;
  staged->drawing.userPolylineOffsets = layout.paperPolyOffsets;
  staged->drawing.userPolylineVerts.assign(layout.paperPolyVerts.begin(), layout.paperPolyVerts.end());
  staged->drawing.userPolylineClosed = layout.paperPolyClosed;
  staged->drawing.userPolylineAttrs = layout.paperPolyAttrs;
  staged->drawing.userCirclesCxCyZR.clear();
  for (size_t i = 0; i + 2 < layout.paperCircles.size(); i += 3) {
    staged->drawing.pushCircle(CadCircleRecord{layout.paperCircles[i], layout.paperCircles[i + 1], 0.0, layout.paperCircles[i + 2]});
  }
  return true;
}

static SelectedEntity PaperTrimEntity(const PaperEntityRef& ref) {
  SelectedEntity e{};
  e.index = ref.index;
  switch (ref.type) {
  case PaperEntityRef::Type::Line: e.type = SelectedEntity::Type::LineSeg; break;
  case PaperEntityRef::Type::Circle: e.type = SelectedEntity::Type::Circle; break;
  case PaperEntityRef::Type::Arc: e.type = SelectedEntity::Type::Arc; break;
  case PaperEntityRef::Type::Ellipse: e.type = SelectedEntity::Type::Ellipse; break;
  case PaperEntityRef::Type::Polyline: e.type = SelectedEntity::Type::Polyline; break;
  default: e.type = SelectedEntity::Type::Annotation; break;
  }
  return e;
}

static bool CommitPaperTrimGeometry(AppCommandState& st, const AppCommandState& staged) {
  PaperLayout* layout = ActivePaperGeometryTarget(st);
  if (!layout)
    return false;
  layout->paperLines.assign(staged.drawing.userLinesFlat.begin(), staged.drawing.userLinesFlat.end());
  layout->paperLineAttrs = staged.drawing.userLineAttrs;
  layout->paperPolyOffsets = staged.drawing.userPolylineOffsets;
  layout->paperPolyVerts.assign(staged.drawing.userPolylineVerts.begin(), staged.drawing.userPolylineVerts.end());
  layout->paperPolyClosed = staged.drawing.userPolylineClosed;
  layout->paperPolyAttrs = staged.drawing.userPolylineAttrs;
  BumpCadGpuCache(st);
  return true;
}

static bool SubmitPaperTrimPick(AppCommandState& st, float x, float y, float tol,
                                std::vector<std::string>& log) {
  using TP = AppCommandState::TrimPhase;
  PaperLayout* layout = ActivePaperGeometryTarget(st);
  if (!layout) {
    log.push_back("TRIM — active paper layout is unavailable; nothing changed.");
    return false;
  }
  AppCommandState staged;
  StagePaperTrimGeometry(*layout, &staged);
  staged.trimState = st.trimState;
  staged.trimCutters.clear();

  if (st.trimPhase == TP::CuttingLine_WaitP1) {
    st.trimCutInfP1x = x; st.trimCutInfP1y = y; st.trimCutInfP1z = 0.f;
    st.trimPhase = TP::CuttingLine_WaitP2;
    log.push_back("TRIM — second point: finishes trim on nearest paper edge along your line.");
    return true;
  }
  if (st.trimPhase == TP::CuttingLine_WaitP2) {
    float x2 = x, y2 = y;
    if (st.orthoMode) {
      if (std::fabs(x2 - st.trimCutInfP1x) >= std::fabs(y2 - st.trimCutInfP1y)) y2 = st.trimCutInfP1y;
      else x2 = st.trimCutInfP1x;
    }
    if (std::hypot(x2 - st.trimCutInfP1x, y2 - st.trimCutInfP1y) < 1e-9f) {
      log.push_back("TRIM — line too short.");
      return false;
    }
    const auto oldLines = staged.drawing.userLinesFlat;
    const auto oldVerts = staged.drawing.userPolylineVerts;
    const auto oldOffsets = staged.drawing.userPolylineOffsets;
    ExecuteDrawnSegmentTrimOnce(staged, st.trimCutInfP1x, st.trimCutInfP1y, x2, y2, tol, log);
    st.trimPhase = TP::SelectCuttingEdges;
    st.active = AppCommandState::Kind::None;
    if (oldLines != staged.drawing.userLinesFlat || oldVerts != staged.drawing.userPolylineVerts || oldOffsets != staged.drawing.userPolylineOffsets) {
      PushUndoSnapshot(st, "Trim paper geometry");
      CommitPaperTrimGeometry(st, staged);
    }
    return true;
  }

  PaperEntityRef hit{};
  if (!PickPaperEntityAt(*layout, x, y, tol, &hit)) {
    log.push_back("TRIM — no object at pick.");
    return false;
  }
  const auto isCuttable = [](PaperEntityRef::Type t) {
    return t == PaperEntityRef::Type::Line || t == PaperEntityRef::Type::Circle ||
           t == PaperEntityRef::Type::Arc || t == PaperEntityRef::Type::Ellipse ||
           t == PaperEntityRef::Type::Polyline;
  };
  if (st.trimPhase == TP::SelectCuttingEdges) {
    if (!isCuttable(hit.type)) {
      log.push_back("TRIM — text or block cannot be a cutting edge; pick a line, circle, arc, ellipse, or polyline.");
      return false;
    }
    if (std::find_if(st.paperTrimCutters.begin(), st.paperTrimCutters.end(), [&](const auto& c) {
          return c.type == hit.type && c.index == hit.index;
        }) != st.paperTrimCutters.end()) {
      log.push_back("TRIM — already a cutting edge.");
      return true;
    }
    st.paperTrimCutters.push_back(hit);
    log.push_back("TRIM — paper cutting edge added.");
    return true;
  }

  if (hit.type != PaperEntityRef::Type::Line && hit.type != PaperEntityRef::Type::Polyline) {
    log.push_back("TRIM — only a paper line or polyline segment can be trimmed.");
    return false;
  }

  TrimTargetEdge target{};
  double ax = 0.0, ay = 0.0, bx = 0.0, by = 0.0;
  float d2 = 0.f;
  if (!PickClosestTrimTarget(staged, x, y, tol, &target, &ax, &ay, &bx, &by, &d2)) {
    log.push_back("TRIM — nothing to trim at paper-space pick.");
    return false;
  }
  std::vector<SelectedEntity> cutters;
  cutters.reserve(st.paperTrimCutters.size());
  for (const auto& c : st.paperTrimCutters) cutters.push_back(PaperTrimEntity(c));
  std::vector<std::array<double, 4>> cuts;
  BuildTrimCutSegments(staged, cutters, &target, &cuts);
  if (cuts.empty()) {
    log.push_back("TRIM — no cutting segments (check paper cutting edges).");
    return false;
  }
  if (TrimSegmentToCuttingEdges(staged, target, ax, ay, bx, by, x, y, cuts, false,
                                0.f, 0.f, 0.f, 0.f, log)) {
    PushUndoSnapshot(st, "Trim paper geometry");
    CommitPaperTrimGeometry(st, staged);
  }
  return true;
}

}  // namespace

bool SubmitTrimViewportPick(AppCommandState& st, float wx, float wy, float tolWorld,
                            std::vector<std::string>& log, const ray3d::Ray* pickRay) {
  ClearPendingOneShotObjectSnap(st);
  using K = AppCommandState::Kind;
  using TP = AppCommandState::TrimPhase;
  if (st.active != K::Trim)
    return false;

  if (st.activeSpaceIndex != kModelSpaceIndex && !InFloatingModelSpace(st))
    return SubmitPaperTrimPick(st, wx, wy, tolWorld, log);

  // issue #399 increment 4: an orbited camera / non-world UCS carries a valid pick ray. The drawn
  // trim line's two points then keep their real elevation (the committed cursor or snap Z), and the
  // trim resolves in true 3D via \ref Try3DDrawnLineTrim. Plan view (pickRay null) is byte-identical
  // to before — the fields' Z stays 0 and the flat \ref ExecuteDrawnSegmentTrimOnce path runs.
  const bool trim3d = pickRay && pickRay->valid();
  const float commitZ = st.viewportSnapPickValid ? static_cast<float>(st.viewportSnapPickLocalZ) : static_cast<float>(st.uiCursorWorldZ);

  if (st.trimPhase == TP::CuttingLine_WaitP1) {
    st.trimCutInfP1x = wx;
    st.trimCutInfP1y = wy;
    st.trimCutInfP1z = trim3d ? commitZ : 0.f;
    st.trimPhase = TP::CuttingLine_WaitP2;
    log.push_back("TRIM — second point: finishes trim on nearest edge along your line (dashed preview).");
    return true;
  }

  if (st.trimPhase == TP::CuttingLine_WaitP2) {
    float p2x = wx;
    float p2y = wy;
    float p2z = trim3d ? commitZ : 0.f;
    if (trim3d) {
      // ORTHO / POLAR in the active UCS plane, carrying Z (issue #371's helper) — the flat branch
      // below stays exactly as it was for the plan-view regression guard.
      ApplyOrthoConstrainFromAnchor(st, st.trimCutInfP1x, st.trimCutInfP1y, &p2x, &p2y, st.orthoMode,
                                    st.trimCutInfP1z, p2z, &p2z);
    } else if (st.orthoMode) {
      const float dx = p2x - st.trimCutInfP1x;
      const float dy = p2y - st.trimCutInfP1y;
      if (std::fabs(dx) >= std::fabs(dy))
        p2y = st.trimCutInfP1y;
      else
        p2x = st.trimCutInfP1x;
    }
    const float ddx = p2x - st.trimCutInfP1x;
    const float ddy = p2y - st.trimCutInfP1y;
    const float ddz = p2z - st.trimCutInfP1z;
    if (ddx * ddx + ddy * ddy + ddz * ddz < 1e-18f) {
      log.push_back("TRIM — line too short.");
      return false;
    }
    st.trimCutInfP2x = p2x;
    st.trimCutInfP2y = p2y;
    st.trimCutInfP2z = p2z;
    if (trim3d) {
      Try3DDrawnLineTrim(st, ray3d::Vec3{st.trimCutInfP1x, st.trimCutInfP1y, st.trimCutInfP1z},
                         ray3d::Vec3{p2x, p2y, p2z}, tolWorld, log);
    } else {
      ExecuteDrawnSegmentTrimOnce(st, st.trimCutInfP1x, st.trimCutInfP1y, p2x, p2y, tolWorld, log);
    }
    st.trimCutters.clear();
    st.paperTrimCutters.clear();
    st.trimPhase = TP::SelectCuttingEdges;
    st.active = K::None;
    return true;
  }

  if (st.trimPhase == TP::SelectCuttingEdges) {
    SelectedEntity hit{};
    float d2 = 0.f;
    if (!PickClosestCadEntity(st, wx, wy, tolWorld, &hit, &d2, pickRay)) {
      log.push_back("TRIM — no object at pick.");
      return false;
    }
    if (hit.type == SelectedEntity::Type::Annotation) {
      log.push_back("TRIM — use a line, circle, arc, ellipse, or polyline as a cutting edge.");
      return false;
    }
    // REQ-087 / REQ-201. Same reasoning as the Annotation refusal above it: a feature line is
    // pickable, so it would otherwise be accepted as a cutting edge and then contribute no cut
    // segments, leaving the user with "no cutting segments" and no idea why.
    if (hit.type == SelectedEntity::Type::FeatureLine) {
      log.push_back("TRIM — 1 feature line ignored: a feature line cannot be a cutting edge. Use a "
                    "line, circle, arc, ellipse, or polyline.");
      return false;
    }
    // REQ-068 / ADR-036 (c). Same class as the two refusals above: a surface is pickable now, so it
    // would otherwise be accepted as a cutting edge, contribute no cut segments, and leave the user
    // with "no cutting segments" and nothing to explain it.
    if (hit.type == SelectedEntity::Type::Surface) {
      log.push_back("TRIM — 1 surface ignored: a surface cannot be a cutting edge. Use a line, "
                    "circle, arc, ellipse, or polyline.");
      return false;
    }
    if (hit.type == SelectedEntity::Type::Spline) {
      log.push_back("TRIM — splines cannot be cutting edges; nothing changed.");
      return false;
    }
    for (const auto& c : st.trimCutters) {
      if (SelectedEntityMatches(c, hit)) {
        log.push_back("TRIM — already a cutting edge.");
        return true;
      }
    }
    st.trimCutters.push_back(hit);
    log.push_back("TRIM — cutting edge added.");
    return true;
  }

  // issue #399 increments 1-2: an orbited camera resolves through a real pick ray (\ref
  // Try3DLineTrim); plan view / world UCS (pickRay null) falls through to the original,
  // byte-identical flat-XY path below.
  if (pickRay && pickRay->valid())
    return Try3DLineTrim(st, wx, wy, tolWorld, *pickRay, log);

  TrimTargetEdge tgt{};
  double ax = 0.0, ay = 0.0, bx = 0.0, by = 0.0;
  float d2 = 0.f;
  if (!PickClosestTrimTarget(st, wx, wy, tolWorld, &tgt, &ax, &ay, &bx, &by, &d2)) {
    // REQ-087 / REQ-201. "Nothing to trim" is a lie when there plainly IS something under the
    // cursor and it happens to be a feature line. Say which it is: trimming one is undefined —
    // the new end would need an elevation, and neither REQ-087 nor REQ-088 says where it comes from.
    SelectedEntity under{};
    float ud2 = 0.f;
    if (PickClosestCadEntity(st, wx, wy, tolWorld, &under, &ud2) &&
        under.type == SelectedEntity::Type::FeatureLine)
      log.push_back("TRIM — 1 feature line ignored: trimming one has no defined elevation for the "
                    "new end.");
    else if (under.type == SelectedEntity::Type::Spline)
      log.push_back("TRIM — spline targets are not supported; nothing changed.");
    else
      log.push_back("TRIM — nothing to trim at pick.");
    return false;
  }

  std::vector<std::array<double, 4>> cuts;
  BuildTrimCutSegments(st, st.trimCutters, &tgt, &cuts);
  if (cuts.empty()) {
    log.push_back("TRIM — no cutting segments (check cutting edges).");
    return false;
  }

  PushUndoSnapshot(st, "Trim");
  if (!TrimSegmentToCuttingEdges(st, tgt, ax, ay, bx, by, wx, wy, cuts, false, 0.f, 0.f, 0.f, 0.f, log))
    return true;
  BumpCadGpuCache(st);
  return true;
}


void StartTrimCommand(AppCommandState& st, std::vector<std::string>& log) {
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  st.active = AppCommandState::Kind::Trim;
  st.trimCutters.clear();
  st.paperTrimCutters.clear();
  st.selBoxWaitingSecond = false;
  // TRIMSTATE picks the starting mode (REQ-056). 0 is smart trim: no cutting edges to pick, just draw a
  // line across what should go.
  if (st.trimState == 1) {
    st.trimPhase = AppCommandState::TrimPhase::SelectCuttingEdges;
    log.push_back(
        "TRIM — pick cutting edges, Enter, then click the pieces to trim (L switches to line trim). ESC cancels.");
  } else {
    st.trimPhase = AppCommandState::TrimPhase::CuttingLine_WaitP1;
    log.push_back(
        "TRIM — draw a line across what should go: first point (T picks cutting edges instead). ESC cancels.");
  }
}

void StartExtendCommand(AppCommandState& st, std::vector<std::string>& log) {
  ResetPaperModifyDrafts(st);
  using K = AppCommandState::Kind;
  if (st.activeSpaceIndex != kModelSpaceIndex && !InFloatingModelSpace(st)) {  // paper routing (REQ-103 step 3)
    st.active = K::None;
    st.paperExtendPhase = 1;
    st.paperExtendBoundaries.clear();
    log.push_back("EXTEND — click a boundary edge (Enter when done picking edges). Esc cancels.");
    return;
  }
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  st.active = K::Extend;
  st.lastCommand = K::Extend;
  st.extendPhase = AppCommandState::ExtendPhase::SelectBoundaries;
  st.extendBoundaries.clear();
  st.selBoxWaitingSecond = false;
  log.push_back("EXTEND — pick boundary edges, Enter, then click objects to extend (near the end to "
                "stretch). ESC cancels.");
}

void StartTrimStateCommand(AppCommandState& st, std::vector<std::string>& log) {
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  st.active = AppCommandState::Kind::TrimState;
  char buf[128];
  std::snprintf(buf, sizeof(buf), "Enter new value for TRIMSTATE <%d>:  (0 = draw a line to trim, 1 = pick cutting edges)",
                st.trimState);
  log.push_back(buf);
}

// Shared by the prompt and the inline `TRIMSTATE 1` form. Returns false (with a message) on a bad value,
// so neither entry point can quietly accept something outside 0/1 (REQ-201).
bool ApplyTrimStateValue(AppCommandState& st, int value, std::vector<std::string>& log) {
  if (value != 0 && value != 1) {
    log.push_back("TRIMSTATE — value must be 0 (draw a line to trim) or 1 (pick cutting edges).");
    return false;
  }
  st.trimState = value;
  log.push_back(value == 1 ? "TRIMSTATE = 1 — TRIM starts by picking cutting edges."
                           : "TRIMSTATE = 0 — TRIM starts by drawing a line across what should go.");
  return true;
}

// ELEV — set the elevation new geometry is drawn at (REQ-058).
