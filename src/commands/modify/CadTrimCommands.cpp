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

static float TrimWorldTolFromPixels(float viewportHeightPx, float orthoHalfHeightWorld, float pixels) {
  return pixels * ((2.f * orthoHalfHeightWorld) / std::max(viewportHeightPx, 1.f));
}

static bool SubmitPaperTrimPick(AppCommandState& st, stepengine::TrimSession& trim, float x, float y, float tol,
                                std::vector<std::string>& log) {
  using TP = stepengine::TrimPhase;
  PaperLayout* layout = ActivePaperGeometryTarget(st);
  if (!layout) {
    log.push_back("TRIM — active paper layout is unavailable; nothing changed.");
    return false;
  }
  AppCommandState staged;
  StagePaperTrimGeometry(*layout, &staged);
  staged.trimState = st.trimState;

  if (trim.phase == TP::CuttingLine_WaitP1) {
    trim.cutInfP1x = x; trim.cutInfP1y = y; trim.cutInfP1z = 0.f;
    trim.phase = TP::CuttingLine_WaitP2;
    log.push_back("TRIM — second point: finishes trim on nearest paper edge along your line.");
    return true;
  }
  if (trim.phase == TP::CuttingLine_WaitP2) {
    float x2 = x, y2 = y;
    if (st.orthoMode) {
      if (std::fabs(x2 - trim.cutInfP1x) >= std::fabs(y2 - trim.cutInfP1y)) y2 = trim.cutInfP1y;
      else x2 = trim.cutInfP1x;
    }
    if (std::hypot(x2 - trim.cutInfP1x, y2 - trim.cutInfP1y) < 1e-9f) {
      log.push_back("TRIM — line too short.");
      return false;
    }
    const auto oldLines = staged.drawing.userLinesFlat;
    const auto oldVerts = staged.drawing.userPolylineVerts;
    const auto oldOffsets = staged.drawing.userPolylineOffsets;
    ExecuteDrawnSegmentTrimOnce(staged, trim.cutInfP1x, trim.cutInfP1y, x2, y2, tol, log);
    stepengine::EndSession(st);
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
  if (trim.phase == TP::SelectCuttingEdges) {
    if (!isCuttable(hit.type)) {
      log.push_back("TRIM — text or block cannot be a cutting edge; pick a line, circle, arc, ellipse, or polyline.");
      return false;
    }
    if (std::find_if(trim.paperCutters.begin(), trim.paperCutters.end(), [&](const auto& c) {
          return c.type == hit.type && c.index == hit.index;
        }) != trim.paperCutters.end()) {
      log.push_back("TRIM — already a cutting edge.");
      return true;
    }
    trim.paperCutters.push_back(hit);
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
  cutters.reserve(trim.paperCutters.size());
  for (const auto& c : trim.paperCutters) cutters.push_back(PaperTrimEntity(c));
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
  auto* trim = st.sessionAs<stepengine::TrimSession>();
  if (!trim)
    return false;
  using TP = stepengine::TrimPhase;

  if (st.activeSpaceIndex != kModelSpaceIndex && !InFloatingModelSpace(st))
    return SubmitPaperTrimPick(st, *trim, wx, wy, tolWorld, log);

  const bool trim3d = pickRay && pickRay->valid();
  const float commitZ = st.viewportSnapPickValid ? static_cast<float>(st.viewportSnapPickLocalZ) : static_cast<float>(st.uiCursorWorldZ);

  if (trim->phase == TP::CuttingLine_WaitP1) {
    trim->cutInfP1x = wx;
    trim->cutInfP1y = wy;
    trim->cutInfP1z = trim3d ? commitZ : 0.f;
    trim->phase = TP::CuttingLine_WaitP2;
    log.push_back("TRIM — second point: finishes trim on nearest edge along your line (dashed preview).");
    return true;
  }

  if (trim->phase == TP::CuttingLine_WaitP2) {
    float p2x = wx;
    float p2y = wy;
    float p2z = trim3d ? commitZ : 0.f;
    if (trim3d) {
      ApplyOrthoConstrainFromAnchor(st, trim->cutInfP1x, trim->cutInfP1y, &p2x, &p2y, st.orthoMode,
                                    trim->cutInfP1z, p2z, &p2z);
    } else if (st.orthoMode) {
      const float dx = p2x - trim->cutInfP1x;
      const float dy = p2y - trim->cutInfP1y;
      if (std::fabs(dx) >= std::fabs(dy))
        p2y = trim->cutInfP1y;
      else
        p2x = trim->cutInfP1x;
    }
    const float ddx = p2x - trim->cutInfP1x;
    const float ddy = p2y - trim->cutInfP1y;
    const float ddz = p2z - trim->cutInfP1z;
    if (ddx * ddx + ddy * ddy + ddz * ddz < 1e-18f) {
      log.push_back("TRIM — line too short.");
      return false;
    }
    trim->cutInfP2x = p2x;
    trim->cutInfP2y = p2y;
    trim->cutInfP2z = p2z;
    if (trim3d) {
      Try3DDrawnLineTrim(st, ray3d::Vec3{trim->cutInfP1x, trim->cutInfP1y, trim->cutInfP1z},
                         ray3d::Vec3{p2x, p2y, p2z}, tolWorld, log);
    } else {
      ExecuteDrawnSegmentTrimOnce(st, trim->cutInfP1x, trim->cutInfP1y, p2x, p2y, tolWorld, log);
    }
    stepengine::EndSession(st);
    return true;
  }

  if (trim->phase == TP::SelectCuttingEdges) {
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
    if (hit.type == SelectedEntity::Type::FeatureLine) {
      log.push_back("TRIM — 1 feature line ignored: a feature line cannot be a cutting edge. Use a "
                    "line, circle, arc, ellipse, or polyline.");
      return false;
    }
    if (hit.type == SelectedEntity::Type::Surface) {
      log.push_back("TRIM — 1 surface ignored: a surface cannot be a cutting edge. Use a line, "
                    "circle, arc, ellipse, or polyline.");
      return false;
    }
    if (hit.type == SelectedEntity::Type::Spline) {
      log.push_back("TRIM — splines cannot be cutting edges; nothing changed.");
      return false;
    }
    for (const auto& c : trim->cutters) {
      if (SelectedEntityMatches(c, hit)) {
        log.push_back("TRIM — already a cutting edge.");
        return true;
      }
    }
    trim->cutters.push_back(hit);
    log.push_back("TRIM — cutting edge added.");
    return true;
  }

  if (pickRay && pickRay->valid())
    return Try3DLineTrim(st, wx, wy, tolWorld, *pickRay, log);

  TrimTargetEdge tgt{};
  double ax = 0.0, ay = 0.0, bx = 0.0, by = 0.0;
  float d2 = 0.f;
  if (!PickClosestTrimTarget(st, wx, wy, tolWorld, &tgt, &ax, &ay, &bx, &by, &d2)) {
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
  BuildTrimCutSegments(st, trim->cutters, &tgt, &cuts);
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

void CadTrimAppendCutLineRemovedPreview(const AppCommandState& st, float fenceP1x, float fenceP1y, float fenceP2x,
                                        float fenceP2y, float pickPreviewX, float pickPreviewY,
                                        std::vector<float>* previewLinesOut) {
  if (!previewLinesOut)
    return;
  float matchTol = std::max(TrimWorldTolFromPixels(st.viewportLastSurveyLayoutHeightPx,
                                                   st.viewportLastSurveyLayoutOrthoHalfH,
                                                   st.objectSnapAperturePx) * 4.f,
                            1e-6f);
  double mnX = 0., mxX = 0., mnY = 0., mxY = 0.;
  if (ComputeWorldExtents(st, &mnX, &mxX, &mnY, &mxY))
    matchTol = std::max(matchTol, static_cast<float>(2e-5 * std::max(mxX - mnX, mxY - mnY)));

  TrimTargetEdge tgt{};
  double ax = 0.0, ay = 0.0, bx = 0.0, by = 0.0;
  float dEdge = 0.f;
  if (!PickTrimTargetClosestToDrawnSegment(st, fenceP1x, fenceP1y, fenceP2x, fenceP2y, matchTol, &tgt, &ax, &ay,
                                           &bx, &by, &dEdge))
    return;

  std::vector<std::array<double, 4>> cuts;
  CollectAllDrawingCutSegmentsExceptTarget(st, &tgt, &cuts);
  if (cuts.empty())
    return;

  double ix = 0.0, iy = 0.0;
  bool trimA = false;
  if (!TrimSegmentIntersectPickSide(ax, ay, bx, by, pickPreviewX, pickPreviewY, cuts, st, fenceP1x, fenceP1y,
                                    fenceP2x, fenceP2y, true, &ix, &iy, &trimA, nullptr))
    return;

  const double rx = trimA ? ax : ix;
  const double ry = trimA ? ay : iy;
  const double sx = trimA ? ix : bx;
  const double sy = trimA ? iy : by;
  previewLinesOut->push_back(static_cast<float>(rx));
  previewLinesOut->push_back(static_cast<float>(ry));
  previewLinesOut->push_back(0.f);
  previewLinesOut->push_back(static_cast<float>(sx));
  previewLinesOut->push_back(static_cast<float>(sy));
  previewLinesOut->push_back(0.f);
}

void CadTrimAppendCutLineRemovedPreview3D(const AppCommandState& st, const ray3d::Vec3& f0, const ray3d::Vec3& f1,
                                          std::vector<float>* previewLinesOut) {
  if (!previewLinesOut)
    return;
  const float tolWorld = TrimWorldTolFromPixels(
      st.viewportLastSurveyLayoutHeightPx, st.viewportLastSurveyLayoutOrthoHalfH, st.objectSnapAperturePx);
  DrawnLineTrimSolution s;
  if (!Solve3DDrawnLineTrim(st, f0, f1, tolWorld, &s, nullptr))
    return;
  const ray3d::Vec3 a = s.trimA ? s.ta : s.cut;
  const ray3d::Vec3 b = s.trimA ? s.cut : s.tb;
  previewLinesOut->push_back(static_cast<float>(a.x));
  previewLinesOut->push_back(static_cast<float>(a.y));
  previewLinesOut->push_back(static_cast<float>(a.z));
  previewLinesOut->push_back(static_cast<float>(b.x));
  previewLinesOut->push_back(static_cast<float>(b.y));
  previewLinesOut->push_back(static_cast<float>(b.z));
}

void StartTrimCommand(AppCommandState& st, std::vector<std::string>& log) {
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  const auto initialPhase = (st.trimState == 1)
                                ? stepengine::TrimPhase::SelectCuttingEdges
                                : stepengine::TrimPhase::CuttingLine_WaitP1;
  stepengine::BeginSession(st, std::make_unique<stepengine::TrimSession>(initialPhase), false);
  if (st.trimState == 1) {
    log.push_back(
        "TRIM — pick cutting edges, Enter, then click the pieces to trim (L switches to line trim). ESC cancels.");
  } else {
    log.push_back(
        "TRIM — draw a line across what should go: first point (T picks cutting edges instead). ESC cancels.");
  }
}

void StartExtendCommand(AppCommandState& st, std::vector<std::string>& log) {
  ResetPaperModifyDrafts(st);
  if (st.activeSpaceIndex != kModelSpaceIndex && !InFloatingModelSpace(st)) {
    stepengine::EndSession(st);
    st.paperExtendPhase = 1;
    st.paperExtendBoundaries.clear();
    log.push_back("EXTEND — click a boundary edge (Enter when done picking edges). Esc cancels.");
    return;
  }
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  stepengine::BeginSession(st, std::make_unique<stepengine::ExtendSession>(), true);
  log.push_back("EXTEND — pick boundary edges, Enter, then click objects to extend (near the end to "
                "stretch). ESC cancels.");
}

void StartTrimStateCommand(AppCommandState& st, std::vector<std::string>& log) {
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  stepengine::BeginSession(st, std::make_unique<stepengine::TrimStateSession>(), false);
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
