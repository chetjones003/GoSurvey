#include "CadCommandsInternal.hpp"
#include "paperspace/CadPaperSpaceInternal.hpp"
#include "CadBlocks.hpp"
#include "CadCoordinateFrame.hpp"
#include "GoSurvey/Document/PlotScales.hpp"
#include "GoSurvey/Document/MtextRichFormat.hpp"
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

// ---------------------------------------------------------------------------
// Paper space (REQ-025)
// ---------------------------------------------------------------------------

int AddPaperLayout(AppCommandState& cmd) {
  // Pick a unique "Layout N" name.
  int n = static_cast<int>(cmd.drawing.paperLayouts.size()) + 1;
  auto nameTaken = [&](const std::string& s) {
    for (const PaperLayout& l : cmd.drawing.paperLayouts)
      if (l.name == s)
        return true;
    return false;
  };
  std::string name;
  do {
    name = "Layout" + std::to_string(n++);
  } while (nameTaken(name));
  PaperLayout layout;  // defaults: ARCH D, landscape
  layout.name = name;
  cmd.drawing.paperLayouts.push_back(layout);
  const int idx = static_cast<int>(cmd.drawing.paperLayouts.size()) - 1;
  cmd.lastPaperLayoutIndex = idx;
  BumpCadGpuCache(cmd);
  return idx;
}

void DeletePaperLayout(AppCommandState& cmd, int idx) {
  if (idx < 0 || static_cast<size_t>(idx) >= cmd.drawing.paperLayouts.size())
    return;
  PushUndoSnapshot(cmd, "Delete layout");  // before the erase, so UNDO brings the layout back (#763)
  // Leave floating model space first when it targets the doomed layout (restores the drawing's UCS).
  if (cmd.floatingViewportIndex >= 0 && cmd.floatingViewportLayout == idx) {
    std::vector<std::string> ignored;
    ExitFloatingModelSpace(cmd, ignored);
  }
  cmd.drawing.paperLayouts.erase(cmd.drawing.paperLayouts.begin() + idx);
  // Re-point everything that holds a layout index.
  if (cmd.floatingViewportLayout > idx)
    --cmd.floatingViewportLayout;
  if (cmd.selectedViewportLayout == idx)
    ClearViewportSelection(cmd);
  else if (cmd.selectedViewportLayout > idx)
    --cmd.selectedViewportLayout;
  if (cmd.activeSpaceIndex == idx) {
    cmd.activeSpaceIndex = kModelSpaceIndex;          // deleted the active layout → fall back to model
    cmd.selectedPaperEntities.clear();                 // those indices belonged to the deleted layout
  } else if (cmd.activeSpaceIndex > idx) {
    --cmd.activeSpaceIndex;                            // indices after the removed one shift down
  }
  cmd.lastPaperLayoutIndex =
      std::clamp(cmd.lastPaperLayoutIndex, 0, std::max(0, static_cast<int>(cmd.drawing.paperLayouts.size()) - 1));
  BumpCadGpuCache(cmd);
}

static void FitViewToSheet(AppCommandState& cmd, const PaperLayout& pl) {
  const float sw = std::max(pl.sheetWidthIn(), 0.01f);
  const float sh = std::max(pl.sheetHeightIn(), 0.01f);
  float aspect = 1.9f;
  if (cmd.viewportLastFbW > 0 && cmd.viewportLastFbH > 0)
    aspect = static_cast<float>(cmd.viewportLastFbW) / static_cast<float>(cmd.viewportLastFbH);
  constexpr float margin = 1.15f;
  // Visible world (paper inches): height = 100/zoom, width = 100*aspect/zoom. Pick the binding fit.
  const float zoomH = 100.f / (sh * margin);
  const float zoomW = 100.f * aspect / (sw * margin);
  cmd.viewportZoom = std::min(zoomH, zoomW);
  cmd.viewportPanX = static_cast<double>(sw) * 0.5;  // pan = view center; center on the sheet
  cmd.viewportPanY = static_cast<double>(sh) * 0.5;
}

void SetActiveSpace(AppCommandState& cmd, int spaceIndex) {
  if (cmd.blockEditActive)  // ADR-043: a block-edit session is model-space only
    return;
  const int prev = cmd.activeSpaceIndex;
  // Save the view of the space we're leaving so each space keeps its own pan/zoom.
  if (prev == kModelSpaceIndex) {
    cmd.modelViewPanX = cmd.viewportPanX;
    cmd.modelViewPanY = cmd.viewportPanY;
    cmd.modelViewZoom = cmd.viewportZoom;
    cmd.modelViewSaved = true;
  } else if (prev >= 0 && static_cast<size_t>(prev) < cmd.drawing.paperLayouts.size()) {
    PaperLayout& pl = cmd.drawing.paperLayouts[static_cast<size_t>(prev)];
    pl.viewPanX = cmd.viewportPanX;
    pl.viewPanY = cmd.viewportPanY;
    pl.viewZoom = cmd.viewportZoom;
    pl.viewInit = true;
  }

  int ns;
  if (spaceIndex < 0 || cmd.drawing.paperLayouts.empty()) {
    ns = kModelSpaceIndex;
  } else {
    ns = std::clamp(spaceIndex, 0, static_cast<int>(cmd.drawing.paperLayouts.size()) - 1);
    cmd.lastPaperLayoutIndex = ns;
  }
  cmd.activeSpaceIndex = ns;

  // Load the new space's view (fit a layout's sheet on first entry).
  if (ns == kModelSpaceIndex) {
    if (cmd.modelViewSaved) {
      cmd.viewportPanX = cmd.modelViewPanX;
      cmd.viewportPanY = cmd.modelViewPanY;
      cmd.viewportZoom = cmd.modelViewZoom;
    }
  } else {
    PaperLayout& pl = cmd.drawing.paperLayouts[static_cast<size_t>(ns)];
    if (pl.viewInit) {
      cmd.viewportPanX = pl.viewPanX;
      cmd.viewportPanY = pl.viewPanY;
      cmd.viewportZoom = pl.viewZoom;
    } else {
      FitViewToSheet(cmd, pl);
      pl.viewPanX = cmd.viewportPanX;
      pl.viewPanY = cmd.viewportPanY;
      pl.viewZoom = cmd.viewportZoom;
      pl.viewInit = true;
    }
  }
  stepengine::EndSession(cmd);  // leaving a space cancels any in-progress command
  BumpCadGpuCache(cmd);
}

void ToggleModelPaperSpace(AppCommandState& cmd) {
  if (cmd.activeSpaceIndex == kModelSpaceIndex) {
    if (cmd.drawing.paperLayouts.empty())
      AddPaperLayout(cmd);  // create one on first switch so PAPER has somewhere to go
    SetActiveSpace(cmd, std::clamp(cmd.lastPaperLayoutIndex, 0, static_cast<int>(cmd.drawing.paperLayouts.size()) - 1));
  } else {
    cmd.lastPaperLayoutIndex = cmd.activeSpaceIndex;
    SetActiveSpace(cmd, kModelSpaceIndex);
  }
}

int AddViewport(AppCommandState& cmd, int layoutIdx) {
  if (layoutIdx < 0 || static_cast<size_t>(layoutIdx) >= cmd.drawing.paperLayouts.size())
    return -1;
  PaperLayout& L = cmd.drawing.paperLayouts[static_cast<size_t>(layoutIdx)];
  const float sw = L.sheetWidthIn();
  const float sh = L.sheetHeightIn();
  Viewport vp;
  vp.paperWIn = sw * 0.7f;
  vp.paperHIn = sh * 0.7f;
  vp.paperXIn = (sw - vp.paperWIn) * 0.5f;
  vp.paperYIn = (sh - vp.paperHIn) * 0.5f;
  // worldDocumentOrigin is ~the model centroid (the load/import rebase centers on it), so it makes a
  // sensible default viewport center; scale defaults to the drawing's plot scale.
  vp.modelCenterX = cmd.drawing.worldDocumentOriginX;
  vp.modelCenterY = cmd.drawing.worldDocumentOriginY;
  vp.scaleModelPerPaperIn = cmd.drawing.modelUnitsPerPlottedInch > 1.e-6f ? cmd.drawing.modelUnitsPerPlottedInch : 50.f;
  vp.annotationScaleName = CadActiveAnnotationScaleName(&cmd.drawing.annotationScales, cmd.currentAnnotationScaleIndex,
                                                        nullptr, cmd.drawing.modelUnitsPerPlottedInch);
  L.viewports.push_back(vp);
  const int idx = static_cast<int>(L.viewports.size()) - 1;
  cmd.selectedViewportLayout = layoutIdx;
  cmd.selectedViewportIndex = idx;
  cmd.selectedViewports = {idx};
  BumpCadGpuCache(cmd);
  return idx;
}

int AddViewportRect(AppCommandState& cmd, int layoutIdx, float x0In, float y0In, float x1In, float y1In) {
  if (layoutIdx < 0 || static_cast<size_t>(layoutIdx) >= cmd.drawing.paperLayouts.size())
    return -1;
  PaperLayout& L = cmd.drawing.paperLayouts[static_cast<size_t>(layoutIdx)];
  Viewport vp;
  vp.paperXIn = std::min(x0In, x1In);
  vp.paperYIn = std::min(y0In, y1In);
  vp.paperWIn = std::max(0.1f, std::fabs(x1In - x0In));
  vp.paperHIn = std::max(0.1f, std::fabs(y1In - y0In));
  vp.modelCenterX = cmd.drawing.worldDocumentOriginX;
  vp.modelCenterY = cmd.drawing.worldDocumentOriginY;
  vp.scaleModelPerPaperIn = cmd.drawing.modelUnitsPerPlottedInch > 1.e-6f ? cmd.drawing.modelUnitsPerPlottedInch : 50.f;
  vp.annotationScaleName = CadActiveAnnotationScaleName(&cmd.drawing.annotationScales, cmd.currentAnnotationScaleIndex,
                                                        nullptr, cmd.drawing.modelUnitsPerPlottedInch);
  L.viewports.push_back(vp);
  const int idx = static_cast<int>(L.viewports.size()) - 1;
  cmd.selectedViewportLayout = layoutIdx;
  cmd.selectedViewportIndex = idx;
  cmd.selectedViewports = {idx};
  BumpCadGpuCache(cmd);
  return idx;
}


void StartPaperRectViewportCommand(AppCommandState& cmd, std::vector<std::string>& log) {
  if (cmd.activeSpaceIndex == kModelSpaceIndex || cmd.activeSpaceIndex < 0 ||
      static_cast<size_t>(cmd.activeSpaceIndex) >= cmd.drawing.paperLayouts.size()) {
    log.push_back("MVIEW — switch to a paper layout first (MODEL/PAPER button).");
    return;
  }
  stepengine::BeginSession(cmd, std::make_unique<stepengine::PaperRectViewportSession>(), /*recordAsLastCommand=*/true);
  cmd.paperVpPhase = 0;
  cmd.paperVpMode = 0;
  log.push_back("MVIEW — click first corner or type [Fit/2/3/4/Object] (Esc to cancel).");
}

void DeleteViewport(AppCommandState& cmd, int layoutIdx, int vpIdx) {
  if (layoutIdx < 0 || static_cast<size_t>(layoutIdx) >= cmd.drawing.paperLayouts.size())
    return;
  PaperLayout& L = cmd.drawing.paperLayouts[static_cast<size_t>(layoutIdx)];
  if (vpIdx < 0 || static_cast<size_t>(vpIdx) >= L.viewports.size())
    return;
  L.viewports.erase(L.viewports.begin() + vpIdx);
  cmd.selectedViewportLayout = -1;
  cmd.selectedViewportIndex = -1;
  cmd.selectedViewports.clear();
  BumpCadGpuCache(cmd);
}

// --- Paper-space viewport selection + edit (REQ-035) ---

bool IsViewportSelected(const AppCommandState& cmd, int vi) {
  for (int s : cmd.selectedViewports)
    if (s == vi)
      return true;
  return false;
}

static void SetPrimarySelectedViewport(AppCommandState& cmd) {
  cmd.selectedViewportLayout = cmd.selectedViewports.empty() ? -1 : cmd.activeSpaceIndex;
  cmd.selectedViewportIndex = cmd.selectedViewports.empty() ? -1 : cmd.selectedViewports.back();
}

void SelectViewport(AppCommandState& cmd, int vi, bool additive) {
  if (!additive)
    cmd.selectedViewports.clear();
  bool present = false;
  for (size_t i = 0; i < cmd.selectedViewports.size(); ++i) {
    if (cmd.selectedViewports[i] == vi) {
      present = true;
      if (additive)
        cmd.selectedViewports.erase(cmd.selectedViewports.begin() + static_cast<std::ptrdiff_t>(i));  // toggle off
      break;
    }
  }
  if (!present && vi >= 0)
    cmd.selectedViewports.push_back(vi);
  SetPrimarySelectedViewport(cmd);
  BumpCadGpuCache(cmd);
}

void ClearViewportSelection(AppCommandState& cmd) {
  cmd.selectedViewports.clear();
  cmd.selectedViewportIndex = -1;
  cmd.selectedViewportLayout = -1;
  BumpCadGpuCache(cmd);
}

void DeleteSelectedViewports(AppCommandState& cmd, std::vector<std::string>& log) {
  if (cmd.activeSpaceIndex < 0 || static_cast<size_t>(cmd.activeSpaceIndex) >= cmd.drawing.paperLayouts.size() ||
      cmd.selectedViewports.empty()) {
    log.push_back("DELETE — no viewport selected.");
    return;
  }
  PaperLayout& L = cmd.drawing.paperLayouts[static_cast<size_t>(cmd.activeSpaceIndex)];
  std::vector<int> idxs = cmd.selectedViewports;
  std::sort(idxs.begin(), idxs.end(), [](int a, int b) { return a > b; });  // erase high→low keeps indices valid
  int n = 0;
  for (int vi : idxs) {
    if (vi >= 0 && static_cast<size_t>(vi) < L.viewports.size()) {
      L.viewports.erase(L.viewports.begin() + vi);
      ++n;
    }
  }
  ClearViewportSelection(cmd);
  log.push_back("DELETE — removed " + std::to_string(n) + " viewport(s).");
}

void TranslateSelectedViewports(AppCommandState& cmd, float dxIn, float dyIn, bool copy,
                                std::vector<std::string>& log) {
  if (cmd.activeSpaceIndex < 0 || static_cast<size_t>(cmd.activeSpaceIndex) >= cmd.drawing.paperLayouts.size() ||
      cmd.selectedViewports.empty())
    return;
  PaperLayout& L = cmd.drawing.paperLayouts[static_cast<size_t>(cmd.activeSpaceIndex)];
  std::vector<int> newSel;
  for (int vi : cmd.selectedViewports) {
    if (vi < 0 || static_cast<size_t>(vi) >= L.viewports.size())
      continue;
    if (copy) {
      Viewport v = L.viewports[static_cast<size_t>(vi)];
      v.paperXIn += dxIn;
      v.paperYIn += dyIn;
      L.viewports.push_back(v);
      newSel.push_back(static_cast<int>(L.viewports.size()) - 1);
    } else {
      L.viewports[static_cast<size_t>(vi)].paperXIn += dxIn;
      L.viewports[static_cast<size_t>(vi)].paperYIn += dyIn;
    }
  }
  if (copy && !newSel.empty())
    cmd.selectedViewports = newSel;
  SetPrimarySelectedViewport(cmd);
  log.push_back(copy ? "COPY — duplicated viewport(s)." : "MOVE — moved viewport(s).");
  BumpCadGpuCache(cmd);
}

void StartPaperMoveCopyViewports(AppCommandState& cmd, bool copy, std::vector<std::string>& log) {
  // Operates on whatever is selected in paper space: viewports (REQ-035) and/or native geometry (REQ-037).
  if (cmd.selectedViewports.empty() && cmd.selectedPaperEntities.empty()) {
    // REQ-307 (GitHub #106): nothing pre-selected — open a real selection step (click-or-box,
    // accumulate until Enter, REQ-121-style pickbox/OSNAP/prompt treatment) rather than refusing.
    // Pick-first stays the fast path when a selection already exists, above.
    cmd.paperMoveWaitingSelection = true;
    cmd.paperMoveIsCopy = copy;
    log.push_back(std::string(copy ? "COPY" : "MOVE") +
                  " — click objects or drag a selection window (Enter when done), then base point "
                  "and destination. ESC cancels.");
    return;
  }
  stepengine::EndSession(cmd);  // paper-space edit ops are not a model command
  cmd.paperMovePhase = 1;
  cmd.paperMoveIsCopy = copy;
  log.push_back(std::string(copy ? "COPY" : "MOVE") + " — specify base point (click or type X,Y). ESC to cancel.");
}

void FinishPaperMoveCopy(AppCommandState& st, float dxIn, float dyIn, std::vector<std::string>& log) {
  if (!st.selectedPaperEntities.empty())
    TranslateSelectedPaperEntities(st, dxIn, dyIn, st.paperMoveIsCopy, log);  // REQ-037
  if (!st.selectedViewports.empty())
    TranslateSelectedViewports(st, dxIn, dyIn, st.paperMoveIsCopy, log);      // REQ-035
  st.paperMovePhase = 0;
}

void FinishPaperMirror(AppCommandState& st, float x2In, float y2In, std::vector<std::string>& log) {
  if (std::hypot(x2In - st.paperMirrorP1XIn, y2In - st.paperMirrorP1YIn) < 1e-6f)
    log.push_back("MIRROR — mirror line needs two distinct points; click again.");
  else
    MirrorSelectedPaperEntities(st, st.paperMirrorP1XIn, st.paperMirrorP1YIn, x2In, y2In, log);
  st.paperMirrorPhase = 0;
}

// REQ-307 (GitHub #106): Enter acting on the paper-space MOVE/COPY/DELETE selection step. A free
// function rather than logic inlined at each caller so both the raw viewport Enter check (CadUi.cpp,
// matching EXTEND's own precedent for a paper-space phase ProcessCommandLineSubmit's Kind-keyed
// dispatch cannot reach, since these three never set cmd.active) and ProcessCommandLineSubmit's own
// blank-Enter branch below call the identical logic — the latter is what gives this a headless
// transcript path the raw-only EXTEND precedent does not have.
void ProcessPaperMoveWaitingSelectionEnter(AppCommandState& st, std::vector<std::string>& log) {
  if (!st.paperMoveWaitingSelection)
    return;
  if (st.selectedPaperEntities.empty() && st.selectedViewports.empty()) {
    log.push_back("Nothing selected — click objects or drag a selection window, then press Enter.");
    return;
  }
  st.paperMoveWaitingSelection = false;
  st.paperMovePhase = 1;
  log.push_back(std::string(st.paperMoveIsCopy ? "COPY" : "MOVE") + " — specify base point (click or type X,Y). ESC to cancel.");
}

void ProcessPaperDeleteWaitingSelectionEnter(AppCommandState& st, std::vector<std::string>& log) {
  if (!st.paperDeleteWaitingSelection)
    return;
  if (st.selectedPaperEntities.empty() && st.selectedViewports.empty()) {
    log.push_back("Nothing selected — click objects or drag a selection window, then press Enter.");
    return;
  }
  const bool hadEntities = !st.selectedPaperEntities.empty();
  const bool hadViewports = !st.selectedViewports.empty();
  if (hadEntities)
    DeleteSelectedPaperEntities(st, log);  // REQ-037
  if (hadViewports)
    DeleteSelectedViewports(st, log);      // REQ-035
  st.paperDeleteWaitingSelection = false;
}

// --- Floating model space (REQ-036) ---
// In-place: the active space stays the paper layout (so the sheet + all viewports stay visible), and
// model edit/snap/draw is routed through the active viewport's rectangle. The model is already drawn
// inside the viewport by the paper overlay, so entering does not change the view.

bool InFloatingModelSpace(const AppCommandState& cmd) { return cmd.floatingViewportIndex >= 0; }

const PaperLayout* ActivePaperGeometryTarget(const AppCommandState& st) {
  // REQ-037 / ADR-009: a draw/edit command writes to the active layout's paper store when a paper
  // layout is active and NOT in floating model space; otherwise it targets model space (→ nullptr).
  if (st.activeSpaceIndex < 0 || InFloatingModelSpace(st))
    return nullptr;
  if (static_cast<size_t>(st.activeSpaceIndex) >= st.drawing.paperLayouts.size())
    return nullptr;
  return &st.drawing.paperLayouts[static_cast<size_t>(st.activeSpaceIndex)];
}

PaperLayout* ActivePaperGeometryTarget(AppCommandState& st) {
  return const_cast<PaperLayout*>(ActivePaperGeometryTarget(static_cast<const AppCommandState&>(st)));
}

// --- Native paper-space geometry: selection + edit (REQ-037, ADR-009) ---


// ROTATE / SCALE / MIRROR refuse a paper multileader by name, exactly as model space does
// (DropMultileadersFromSelection): it can only be moved, copied or erased.
static void DropPaperLeadersFromSelection(AppCommandState& st, const char* verb, std::vector<std::string>& log) {
  const size_t before = st.selectedPaperEntities.size();
  st.selectedPaperEntities.erase(
      std::remove_if(st.selectedPaperEntities.begin(), st.selectedPaperEntities.end(),
                     [](const PaperRef& r) { return r.type == PaperRef::Type::Leader; }),
      st.selectedPaperEntities.end());
  const size_t dropped = before - st.selectedPaperEntities.size();
  if (dropped > 0)
    log.push_back(std::string(verb) + " � " + std::to_string(dropped) +
                  " Multileader(s) left unchanged: a Multileader can only be moved, copied or erased.");
}

// A plain click-select carries no crossing/window box (REQ-103 STRETCH), unlike closePaperSelBox's
// own selection, which sets paperSelBoxLastValid true right after populating selectedPaperEntities
// (CadUi.cpp). Invalidating here — the shared funnel every non-box selection change goes through —
// means STRETCH never mistakes a click-selected entity's stale prior box for a real one.
void ClearPaperEntitySelection(AppCommandState& st) {
  st.selectedPaperEntities.clear();
  st.paperSelBoxLastValid = false;
}


static float PaperPointSegDist2(float px, float py, float ax, float ay, float bx, float by) {
  const float vx = bx - ax, vy = by - ay;
  const float len2 = vx * vx + vy * vy;
  float t = len2 > 1.e-12f ? ((px - ax) * vx + (py - ay) * vy) / len2 : 0.f;
  t = std::clamp(t, 0.f, 1.f);
  const float dx = px - (ax + t * vx), dy = py - (ay + t * vy);
  return dx * dx + dy * dy;
}

// PaperTextBoundsIn (top-left text bounds, REQ-039) now lives in PaperSpace.hpp as an inline helper so the
// header-only box-select + unit tests can share it.

void AppendScratchToPaperLayout(PaperLayout& L, const AppCommandState& scratch) {
  for (size_t i = 0; i < scratch.drawing.circleCount(); ++i) {
    const CadCircleRecord c = scratch.drawing.circle(i);
    L.paperCircles.push_back(static_cast<float>(c.cx));
    L.paperCircles.push_back(static_cast<float>(c.cy));
    L.paperCircles.push_back(static_cast<float>(c.r));
  }
  L.paperCircleAttrs.insert(L.paperCircleAttrs.end(), scratch.drawing.userCircleAttrs.begin(), scratch.drawing.userCircleAttrs.end());
  L.paperArcs.insert(L.paperArcs.end(), scratch.drawing.userArcs.begin(), scratch.drawing.userArcs.end());
  L.paperArcAttrs.insert(L.paperArcAttrs.end(), scratch.drawing.userArcAttrs.begin(), scratch.drawing.userArcAttrs.end());
  L.paperEllipses.insert(L.paperEllipses.end(), scratch.drawing.userEllipses.begin(), scratch.drawing.userEllipses.end());
  L.paperEllAttrs.insert(L.paperEllAttrs.end(), scratch.drawing.userEllAttrs.begin(), scratch.drawing.userEllAttrs.end());
  for (size_t p = 0; p + 1 < scratch.drawing.userPolylineOffsets.size(); ++p) {
    if (L.paperPolyOffsets.empty())
      L.paperPolyOffsets.push_back(0);
    const int a = scratch.drawing.userPolylineOffsets[p];
    const int b = scratch.drawing.userPolylineOffsets[p + 1];
    for (int v = a; v < b; ++v) {
      const size_t base = static_cast<size_t>(v);
      L.paperPolyVerts.push_back(static_cast<float>(scratch.drawing.vertex(base).x));
      L.paperPolyVerts.push_back(static_cast<float>(scratch.drawing.vertex(base).y));
      L.paperPolyVerts.push_back(static_cast<float>(scratch.drawing.vertex(base).z));
      const float bg = static_cast<size_t>(v) < scratch.drawing.userPolylineVertsBulge.size()
                           ? scratch.drawing.userPolylineVertsBulge[static_cast<size_t>(v)]
                           : 0.f;
      L.paperPolyVertsBulge.push_back(bg);
    }
    L.paperPolyOffsets.push_back(L.paperPolyOffsets.back() + (b - a));
    L.paperPolyClosed.push_back(scratch.drawing.userPolylineClosed[p]);
    L.paperPolyAttrs.push_back(scratch.drawing.userPolylineAttrs[p]);
  }
  L.paperLines.insert(L.paperLines.end(), scratch.drawing.userLinesFlat.begin(), scratch.drawing.userLinesFlat.end());
  L.paperLineAttrs.insert(L.paperLineAttrs.end(), scratch.drawing.userLineAttrs.begin(), scratch.drawing.userLineAttrs.end());
  L.paperTexts.insert(L.paperTexts.end(), scratch.drawing.cadAnnotations.begin(), scratch.drawing.cadAnnotations.end());
  L.paperTextAttrs.insert(L.paperTextAttrs.end(), scratch.drawing.cadAnnotationAttrs.begin(), scratch.drawing.cadAnnotationAttrs.end());
  L.paperFilledRegions.insert(L.paperFilledRegions.end(), scratch.drawing.cadFilledRegions.begin(),
                              scratch.drawing.cadFilledRegions.end());
  L.paperFilledRegionAttrs.insert(L.paperFilledRegionAttrs.end(), scratch.drawing.cadFilledRegionAttrs.begin(),
                                  scratch.drawing.cadFilledRegionAttrs.end());
  L.paperMultileaders.insert(L.paperMultileaders.end(), scratch.drawing.cadMultileaders.begin(),
                             scratch.drawing.cadMultileaders.end());
  L.paperMultileaderAttrs.insert(L.paperMultileaderAttrs.end(), scratch.drawing.cadMultileaderAttrs.begin(),
                                 scratch.drawing.cadMultileaderAttrs.end());
  L.paperTables.insert(L.paperTables.end(), scratch.drawing.cadTables.begin(), scratch.drawing.cadTables.end());
  L.paperTableAttrs.insert(L.paperTableAttrs.end(), scratch.drawing.cadTableAttrs.begin(), scratch.drawing.cadTableAttrs.end());
  L.paperPoints.insert(L.paperPoints.end(), scratch.drawing.cadPoints.begin(), scratch.drawing.cadPoints.end());
  L.paperPointAttrs.insert(L.paperPointAttrs.end(), scratch.drawing.cadPointAttrs.begin(), scratch.drawing.cadPointAttrs.end());
  L.paperSplines.insert(L.paperSplines.end(), scratch.drawing.cadSplines.begin(), scratch.drawing.cadSplines.end());
  L.paperSplineAttrs.insert(L.paperSplineAttrs.end(), scratch.drawing.cadSplineAttrs.begin(), scratch.drawing.cadSplineAttrs.end());
}

bool PickPaperEntityAt(const PaperLayout& L, float x, float y, float tolIn, PaperRef* out,
                       const std::vector<CadBlockDefinition>* blockDefs) {
  assert(out != nullptr);
  for (int bi = static_cast<int>(L.paperBlockRefs.size()) - 1; bi >= 0; --bi) {
    const CadBlockRef& br = L.paperBlockRefs[static_cast<size_t>(bi)];
    bool hit = false;
    if (blockDefs)
      hit = CadBlockHitWorld(*blockDefs, br, x, y, tolIn);
    const float dx = x - br.xf.x;
    const float dy = y - br.xf.y;
    if (!hit)
      hit = (dx * dx + dy * dy) <= tolIn * tolIn;
    if (hit) {
      out->type = PaperRef::Type::Block;
      out->index = bi;
      return true;
    }
  }
  for (int ti = static_cast<int>(L.paperTables.size()) - 1; ti >= 0; --ti) {
    const CadTable& tb = L.paperTables[static_cast<size_t>(ti)];
    float lx = 0.f, ly = 0.f;
    CadTableWorldToLocal(tb, x, y, &lx, &ly);
    if (lx >= -tolIn && lx <= std::max(tb.width, 1.e-3f) + tolIn && ly >= -tolIn &&
        ly <= std::max(tb.height, 1.e-3f) + tolIn) {
      out->type = PaperRef::Type::Table;
      out->index = ti;
      return true;
    }
  }
  for (int mi = static_cast<int>(L.paperMultileaders.size()) - 1; mi >= 0; --mi) {
    double d2 = 0.0;
    if (CadMultileaderHit(L.paperMultileaders[static_cast<size_t>(mi)], x, y, tolIn, &d2)) {
      out->type = PaperRef::Type::Leader;
      out->index = mi;
      return true;
    }
  }
  // Text first (it draws on top); topmost = last in the vector. Then lines by segment distance.
  for (int ti = static_cast<int>(L.paperTexts.size()) - 1; ti >= 0; --ti) {
    float bx0, by0, bx1, by1;
    PaperTextBoundsIn(L.paperTexts[static_cast<size_t>(ti)], &bx0, &by0, &bx1, &by1);
    if (x >= bx0 - tolIn && x <= bx1 + tolIn && y >= by0 - tolIn && y <= by1 + tolIn) {
      out->type = PaperRef::Type::Text;
      out->index = ti;
      return true;
    }
  }
  const float tol2 = tolIn * tolIn;
  // Circles / arcs / ellipses / polylines (REQ-038, ADR-013) — newest first, topmost wins.
  for (int ci = static_cast<int>(L.paperCircles.size() / 3) - 1; ci >= 0; --ci) {
    const size_t i = static_cast<size_t>(ci) * 3;
    const float dr = std::sqrt((x - L.paperCircles[i]) * (x - L.paperCircles[i]) +
                               (y - L.paperCircles[i + 1]) * (y - L.paperCircles[i + 1])) - L.paperCircles[i + 2];
    if (std::abs(dr) <= tolIn) {
      out->type = PaperRef::Type::Circle;
      out->index = ci;
      return true;
    }
  }
  for (int ai = static_cast<int>(L.paperArcs.size()) - 1; ai >= 0; --ai) {
    const CadArc& a = L.paperArcs[static_cast<size_t>(ai)];
    if (std::abs(std::sqrt((x - a.cx) * (x - a.cx) + (y - a.cy) * (y - a.cy)) - a.r) <= tolIn) {
      out->type = PaperRef::Type::Arc;
      out->index = ai;
      return true;
    }
  }
  for (int ei = static_cast<int>(L.paperEllipses.size()) - 1; ei >= 0; --ei) {
    const CadEllipse& e = L.paperEllipses[static_cast<size_t>(ei)];
    const float a = std::sqrt(e.majVx * e.majVx + e.majVy * e.majVy);
    const float b = a * e.ratio;
    if (a > 1.e-6f && b > 1.e-6f) {
      const float dx = static_cast<float>(x - e.cx), dy = static_cast<float>(y - e.cy);
      const float n = (dx * dx) / (a * a) + (dy * dy) / (b * b);
      if (std::abs(n - 1.f) <= 0.15f) {  // proximity band around the rim
        out->type = PaperRef::Type::Ellipse;
        out->index = ei;
        return true;
      }
    }
  }
  for (int pi = static_cast<int>(L.paperPolyOffsets.size()) - 2; pi >= 0; --pi) {
    const int v0 = L.paperPolyOffsets[static_cast<size_t>(pi)];
    const int v1 = L.paperPolyOffsets[(static_cast<size_t>(pi)+ 1)];
    const bool closed = static_cast<size_t>(pi) < L.paperPolyClosed.size() && L.paperPolyClosed[static_cast<size_t>(pi)] != 0;
    const int nSeg = closed ? (v1 - v0) : (v1 - v0 - 1);
    for (int s = 0; s < nSeg; ++s) {
      const int ia = v0 + s;
      const int ib = v0 + ((s + 1) % (v1 - v0));
      const size_t a = static_cast<size_t>(ia) * 3, b = static_cast<size_t>(ib) * 3;
      const float bulge = static_cast<size_t>(ia) < L.paperPolyVertsBulge.size() ? L.paperPolyVertsBulge[static_cast<size_t>(ia)] : 0.f;
      if (std::fabs(bulge) > 1.e-8f) {
        const BulgeArcSpan span = BulgeArc(L.paperPolyVerts[a], L.paperPolyVerts[a + 1], L.paperPolyVerts[b],
                                           L.paperPolyVerts[b + 1], bulge);
        if (span.valid && PointArcDistanceSq(x, y, span) <= static_cast<double>(tol2)) {
          out->type = PaperRef::Type::Polyline;
          out->index = pi;
          return true;
        }
      } else if (PaperPointSegDist2(x, y, L.paperPolyVerts[a], L.paperPolyVerts[a + 1], L.paperPolyVerts[b],
                                    L.paperPolyVerts[b + 1]) <= tol2) {
        out->type = PaperRef::Type::Polyline;
        out->index = pi;
        return true;
      }
    }
  }
  for (int si = static_cast<int>(L.paperLines.size() / 6) - 1; si >= 0; --si) {
    const size_t i = static_cast<size_t>(si) * 6;
    if (PaperPointSegDist2(x, y, L.paperLines[i], L.paperLines[i + 1], L.paperLines[i + 3],
                           L.paperLines[i + 4]) <= tol2) {
      out->type = PaperRef::Type::Line;
      out->index = si;
      return true;
    }
  }
  for (int pi = static_cast<int>(L.paperPoints.size()) - 1; pi >= 0; --pi) {
    const CadPoint& p = L.paperPoints[static_cast<size_t>(pi)];
    const float dx = x - static_cast<float>(p.x);
    const float dy = y - static_cast<float>(p.y);
    if (dx * dx + dy * dy <= tol2) {
      out->type = PaperRef::Type::Point;
      out->index = pi;
      return true;
    }
  }
  for (int si = static_cast<int>(L.paperSplines.size()) - 1; si >= 0; --si) {
    std::vector<ray3d::Vec3> pts;
    spline::Tessellate(L.paperSplines[static_cast<size_t>(si)], kSplineSampleTolerance, &pts);
    for (size_t k = 1; k < pts.size(); ++k) {
      if (PaperPointSegDist2(x, y, static_cast<float>(pts[k - 1].x), static_cast<float>(pts[k - 1].y),
                             static_cast<float>(pts[k].x), static_cast<float>(pts[k].y)) <= tol2) {
        out->type = PaperRef::Type::Spline;
        out->index = si;
        return true;
      }
    }
  }
  return false;
}

void TogglePaperEntitySelection(AppCommandState& st, PaperRef ref, bool additive) {
  // Same invalidation as ClearPaperEntitySelection, same reason (REQ-103 STRETCH): a click-built
  // selection carries no box.
  st.paperSelBoxLastValid = false;
  if (!additive) {
    st.selectedPaperEntities.assign(1, ref);
    return;
  }
  for (size_t i = 0; i < st.selectedPaperEntities.size(); ++i)
    if (st.selectedPaperEntities[i].type == ref.type && st.selectedPaperEntities[i].index == ref.index) {
      st.selectedPaperEntities.erase(st.selectedPaperEntities.begin() + static_cast<std::ptrdiff_t>(i));
      return;
    }
  st.selectedPaperEntities.push_back(ref);
}

// Remove paper polyline \p pi from \p L, fixing the offset table + parallel arrays (REQ-038, ADR-013).
static void ErasePaperPolyline(PaperLayout& L, int pi) {
  if (pi < 0 || (static_cast<size_t>(pi)+ 1) >= L.paperPolyOffsets.size())
    return;
  const int v0 = L.paperPolyOffsets[static_cast<size_t>(pi)];
  const int v1 = L.paperPolyOffsets[(static_cast<size_t>(pi)+ 1)];
  const int nv = v1 - v0;
  L.paperPolyVerts.erase(L.paperPolyVerts.begin() + static_cast<std::ptrdiff_t>(v0) * 3,
                         L.paperPolyVerts.begin() + static_cast<std::ptrdiff_t>(v1) * 3);
  if (L.paperPolyVertsBulge.size() >= static_cast<size_t>(v1))
    L.paperPolyVertsBulge.erase(L.paperPolyVertsBulge.begin() + v0, L.paperPolyVertsBulge.begin() + v1);
  L.paperPolyOffsets.erase(L.paperPolyOffsets.begin() + (pi + 1));  // drop this poly's end marker
  for (size_t k = (static_cast<size_t>(pi)+ 1); k < L.paperPolyOffsets.size(); ++k)
    L.paperPolyOffsets[k] -= nv;                                    // shift the rest back
  if (L.paperPolyOffsets.size() == 1)  // last polyline removed → empty the table entirely
    L.paperPolyOffsets.clear();
  if (static_cast<size_t>(pi) < L.paperPolyClosed.size())
    L.paperPolyClosed.erase(L.paperPolyClosed.begin() + pi);
  if (static_cast<size_t>(pi) < L.paperPolyAttrs.size())
    L.paperPolyAttrs.erase(L.paperPolyAttrs.begin() + pi);
}

void DeleteSelectedPaperEntities(AppCommandState& st, std::vector<std::string>& log) {
  PaperLayout* L = ActivePaperGeometryTarget(st);
  if (!L || st.selectedPaperEntities.empty())
    return;
  // Group indices by type; erase each type's indices in descending order so earlier indices stay valid.
  std::vector<int> byType[11];
  for (const PaperRef& r : st.selectedPaperEntities)
    byType[static_cast<int>(r.type)].push_back(r.index);
  auto descUnique = [](std::vector<int>& v) {
    std::sort(v.begin(), v.end(), [](int a, int b) { return a > b; });
    v.erase(std::unique(v.begin(), v.end()), v.end());
  };
  for (auto& v : byType)
    descUnique(v);
  PushUndoSnapshot(st, "Delete paper geometry");
  size_t n = 0;
  auto eraseElem = [&](auto& vec, auto& attrs, int idx) {
    if (idx >= 0 && static_cast<size_t>(idx) < vec.size()) {
      vec.erase(vec.begin() + idx);
      if (static_cast<size_t>(idx) < attrs.size())
        attrs.erase(attrs.begin() + idx);
      ++n;
    }
  };
  for (int si : byType[static_cast<int>(PaperRef::Type::Line)])
    if (si >= 0 && static_cast<size_t>(si) * 6 + 5 < L->paperLines.size()) {
      L->paperLines.erase(L->paperLines.begin() + static_cast<std::ptrdiff_t>(si) * 6,
                          L->paperLines.begin() + static_cast<std::ptrdiff_t>(si) * 6 + 6);
      if (static_cast<size_t>(si) < L->paperLineAttrs.size())
        L->paperLineAttrs.erase(L->paperLineAttrs.begin() + si);
      ++n;
    }
  for (int ci : byType[static_cast<int>(PaperRef::Type::Circle)])
    if (ci >= 0 && static_cast<size_t>(ci) * 3 + 2 < L->paperCircles.size()) {
      L->paperCircles.erase(L->paperCircles.begin() + static_cast<std::ptrdiff_t>(ci) * 3,
                            L->paperCircles.begin() + static_cast<std::ptrdiff_t>(ci) * 3 + 3);
      if (static_cast<size_t>(ci) < L->paperCircleAttrs.size())
        L->paperCircleAttrs.erase(L->paperCircleAttrs.begin() + ci);
      ++n;
    }
  for (int ai : byType[static_cast<int>(PaperRef::Type::Arc)])
    eraseElem(L->paperArcs, L->paperArcAttrs, ai);
  for (int ei : byType[static_cast<int>(PaperRef::Type::Ellipse)])
    eraseElem(L->paperEllipses, L->paperEllAttrs, ei);
  for (int pi : byType[static_cast<int>(PaperRef::Type::Polyline)]) {
    ErasePaperPolyline(*L, pi);
    ++n;
  }
  for (int ti : byType[static_cast<int>(PaperRef::Type::Text)])
    eraseElem(L->paperTexts, L->paperTextAttrs, ti);
  for (int bi : byType[static_cast<int>(PaperRef::Type::Block)])
    eraseElem(L->paperBlockRefs, L->paperBlockRefAttrs, bi);
  for (int mi : byType[static_cast<int>(PaperRef::Type::Leader)])
    eraseElem(L->paperMultileaders, L->paperMultileaderAttrs, mi);
  for (int ti : byType[static_cast<int>(PaperRef::Type::Table)])
    eraseElem(L->paperTables, L->paperTableAttrs, ti);
  for (int si : byType[static_cast<int>(PaperRef::Type::Spline)])
    eraseElem(L->paperSplines, L->paperSplineAttrs, si);
  for (int pi : byType[static_cast<int>(PaperRef::Type::Point)])
    eraseElem(L->paperPoints, L->paperPointAttrs, pi);
  ClearPaperEntitySelection(st);
  BumpCadGpuCache(st);
  log.push_back("DELETE — removed " + std::to_string(n) + " paper object(s).");
}

void TranslateSelectedPaperEntities(AppCommandState& st, float dxIn, float dyIn, bool copy,
                                    std::vector<std::string>& log, bool pushUndo) {
  PaperLayout* L = ActivePaperGeometryTarget(st);
  if (!L || st.selectedPaperEntities.empty())
    return;
  if (pushUndo)
    PushUndoSnapshot(st, copy ? "Copy paper geometry" : "Move paper geometry");
  std::vector<PaperRef> newSel;
  auto attrAt = [](const std::vector<EntityAttributes>& v, int i) {
    return (i >= 0 && static_cast<size_t>(i) < v.size()) ? v[static_cast<size_t>(i)] : EntityAttributes{};
  };
  for (const PaperRef& r : st.selectedPaperEntities) {
    switch (r.type) {
    case PaperRef::Type::Line: {
      const size_t i = static_cast<size_t>(r.index) * 6;
      if (i + 5 >= L->paperLines.size())
        break;
      if (copy) {
        for (int k = 0; k < 6; ++k)
          L->paperLines.push_back(L->paperLines[i + static_cast<size_t>(k)]);
        const size_t j = L->paperLines.size() - 6;
        L->paperLines[j] += dxIn; L->paperLines[j + 1] += dyIn;
        L->paperLines[j + 3] += dxIn; L->paperLines[j + 4] += dyIn;
        L->paperLineAttrs.push_back(attrAt(L->paperLineAttrs, r.index));
        newSel.push_back({PaperRef::Type::Line, static_cast<int>(L->paperLines.size() / 6) - 1});
      } else {
        L->paperLines[i] += dxIn; L->paperLines[i + 1] += dyIn;
        L->paperLines[i + 3] += dxIn; L->paperLines[i + 4] += dyIn;
      }
      break;
    }
    case PaperRef::Type::Circle: {
      const size_t i = static_cast<size_t>(r.index) * 3;
      if (i + 2 >= L->paperCircles.size())
        break;
      if (copy) {
        L->paperCircles.push_back(L->paperCircles[i] + dxIn);
        L->paperCircles.push_back(L->paperCircles[i + 1] + dyIn);
        L->paperCircles.push_back(L->paperCircles[i + 2]);
        L->paperCircleAttrs.push_back(attrAt(L->paperCircleAttrs, r.index));
        newSel.push_back({PaperRef::Type::Circle, static_cast<int>(L->paperCircles.size() / 3) - 1});
      } else {
        L->paperCircles[i] += dxIn; L->paperCircles[i + 1] += dyIn;
      }
      break;
    }
    case PaperRef::Type::Arc: {
      if (r.index < 0 || static_cast<size_t>(r.index) >= L->paperArcs.size())
        break;
      if (copy) {
        CadArc a = L->paperArcs[static_cast<size_t>(r.index)];
        a.cx += dxIn; a.cy += dyIn;
        L->paperArcs.push_back(a);
        L->paperArcAttrs.push_back(attrAt(L->paperArcAttrs, r.index));
        newSel.push_back({PaperRef::Type::Arc, static_cast<int>(L->paperArcs.size()) - 1});
      } else {
        L->paperArcs[static_cast<size_t>(r.index)].cx += dxIn;
        L->paperArcs[static_cast<size_t>(r.index)].cy += dyIn;
      }
      break;
    }
    case PaperRef::Type::Ellipse: {
      if (r.index < 0 || static_cast<size_t>(r.index) >= L->paperEllipses.size())
        break;
      if (copy) {
        CadEllipse e = L->paperEllipses[static_cast<size_t>(r.index)];
        e.cx += dxIn; e.cy += dyIn;
        L->paperEllipses.push_back(e);
        L->paperEllAttrs.push_back(attrAt(L->paperEllAttrs, r.index));
        newSel.push_back({PaperRef::Type::Ellipse, static_cast<int>(L->paperEllipses.size()) - 1});
      } else {
        L->paperEllipses[static_cast<size_t>(r.index)].cx += dxIn;
        L->paperEllipses[static_cast<size_t>(r.index)].cy += dyIn;
      }
      break;
    }
    case PaperRef::Type::Polyline: {
      const int pi = r.index;
      if (pi < 0 || (static_cast<size_t>(pi)+ 1) >= L->paperPolyOffsets.size())
        break;
      const int v0 = L->paperPolyOffsets[static_cast<size_t>(pi)];
      const int v1 = L->paperPolyOffsets[(static_cast<size_t>(pi)+ 1)];
      if (copy) {
        const int baseVert = L->paperPolyOffsets.empty() ? 0 : L->paperPolyOffsets.back();
        for (int vi = v0; vi < v1; ++vi) {
          L->paperPolyVerts.push_back(L->paperPolyVerts[(static_cast<size_t>(vi)* 3)] + dxIn);
          L->paperPolyVerts.push_back(L->paperPolyVerts[(static_cast<size_t>(vi)* 3 + 1)] + dyIn);
          L->paperPolyVerts.push_back(L->paperPolyVerts[(static_cast<size_t>(vi)* 3 + 2)]);
          L->paperPolyVertsBulge.push_back(static_cast<size_t>(vi) < L->paperPolyVertsBulge.size()
                                               ? L->paperPolyVertsBulge[static_cast<size_t>(vi)]
                                               : 0.f);
        }
        L->paperPolyOffsets.push_back(baseVert + (v1 - v0));
        L->paperPolyClosed.push_back(static_cast<size_t>(pi) < L->paperPolyClosed.size() ? L->paperPolyClosed[static_cast<size_t>(pi)] : 0u);
        L->paperPolyAttrs.push_back(attrAt(L->paperPolyAttrs, pi));
        newSel.push_back({PaperRef::Type::Polyline, static_cast<int>(L->paperPolyOffsets.size()) - 2});
      } else {
        for (int vi = v0; vi < v1; ++vi) {
          L->paperPolyVerts[(static_cast<size_t>(vi)* 3)] += dxIn;
          L->paperPolyVerts[(static_cast<size_t>(vi)* 3 + 1)] += dyIn;
        }
      }
      break;
    }
    case PaperRef::Type::Text: {
      if (r.index < 0 || static_cast<size_t>(r.index) >= L->paperTexts.size())
        break;
      if (copy) {
        CadAnnotation a = L->paperTexts[static_cast<size_t>(r.index)];
        a.insX += dxIn; a.insY += dyIn;
        L->paperTexts.push_back(std::move(a));
        L->paperTextAttrs.push_back(attrAt(L->paperTextAttrs, r.index));
        newSel.push_back({PaperRef::Type::Text, static_cast<int>(L->paperTexts.size()) - 1});
      } else {
        L->paperTexts[static_cast<size_t>(r.index)].insX += dxIn;
        L->paperTexts[static_cast<size_t>(r.index)].insY += dyIn;
      }
      break;
    }
    case PaperRef::Type::Block: {
      if (r.index < 0 || static_cast<size_t>(r.index) >= L->paperBlockRefs.size())
        break;
      if (copy) {
        CadBlockRef c = L->paperBlockRefs[static_cast<size_t>(r.index)];
        CadBlockTranslate(&c, dxIn, dyIn, 0.f);
        L->paperBlockRefs.push_back(std::move(c));
        L->paperBlockRefAttrs.push_back(attrAt(L->paperBlockRefAttrs, r.index));
        newSel.push_back({PaperRef::Type::Block, static_cast<int>(L->paperBlockRefs.size()) - 1});
      } else {
        CadBlockTranslate(&L->paperBlockRefs[static_cast<size_t>(r.index)], dxIn, dyIn, 0.f);
      }
      break;
    }
    case PaperRef::Type::Leader: {
      if (r.index < 0 || static_cast<size_t>(r.index) >= L->paperMultileaders.size())
        break;
      if (copy) {
        CadMultileader c = L->paperMultileaders[static_cast<size_t>(r.index)];
        CadMultileaderTranslate(&c, dxIn, dyIn, 0.0);
        L->paperMultileaders.push_back(std::move(c));
        L->paperMultileaderAttrs.push_back(attrAt(L->paperMultileaderAttrs, r.index));
        newSel.push_back({PaperRef::Type::Leader, static_cast<int>(L->paperMultileaders.size()) - 1});
      } else {
        CadMultileaderTranslate(&L->paperMultileaders[static_cast<size_t>(r.index)], dxIn, dyIn, 0.0);
      }
      break;
    }
    case PaperRef::Type::Table: {
      if (r.index < 0 || static_cast<size_t>(r.index) >= L->paperTables.size())
        break;
      if (copy) {
        CadTable c = L->paperTables[static_cast<size_t>(r.index)];
        CadTableTranslate(&c, dxIn, dyIn);
        L->paperTables.push_back(std::move(c));
        L->paperTableAttrs.push_back(attrAt(L->paperTableAttrs, r.index));
        newSel.push_back({PaperRef::Type::Table, static_cast<int>(L->paperTables.size()) - 1});
      } else {
        CadTableTranslate(&L->paperTables[static_cast<size_t>(r.index)], dxIn, dyIn);
      }
      break;
    }
    case PaperRef::Type::Spline: {
      if (r.index < 0 || static_cast<size_t>(r.index) >= L->paperSplines.size())
        break;
      if (copy) {
        CadSpline c = L->paperSplines[static_cast<size_t>(r.index)];
        spline::MapPoints(&c, [&](const spline::Vec3& v) {
          return spline::Vec3{v.x + dxIn, v.y + dyIn, v.z};
        });
        L->paperSplines.push_back(std::move(c));
        L->paperSplineAttrs.push_back(attrAt(L->paperSplineAttrs, r.index));
        newSel.push_back({PaperRef::Type::Spline, static_cast<int>(L->paperSplines.size()) - 1});
      } else {
        spline::MapPoints(&L->paperSplines[static_cast<size_t>(r.index)], [&](const spline::Vec3& v) {
          return spline::Vec3{v.x + dxIn, v.y + dyIn, v.z};
        });
      }
      break;
    }
    case PaperRef::Type::Point: {
      if (r.index < 0 || static_cast<size_t>(r.index) >= L->paperPoints.size())
        break;
      if (copy) {
        CadPoint c = L->paperPoints[static_cast<size_t>(r.index)];
        c.x += dxIn;
        c.y += dyIn;
        L->paperPoints.push_back(c);
        L->paperPointAttrs.push_back(attrAt(L->paperPointAttrs, r.index));
        newSel.push_back({PaperRef::Type::Point, static_cast<int>(L->paperPoints.size()) - 1});
      } else {
        L->paperPoints[static_cast<size_t>(r.index)].x += dxIn;
        L->paperPoints[static_cast<size_t>(r.index)].y += dyIn;
      }
      break;
    }
    }
  }
  if (copy && !newSel.empty())
    st.selectedPaperEntities = newSel;
  BumpCadGpuCache(st);
  log.push_back(copy ? "COPY — duplicated paper object(s)." : "MOVE — moved paper object(s).");
}

void RotateSelectedPaperEntities(AppCommandState& st, float baseX, float baseY, float angRad,
                                 std::vector<std::string>& log, bool pushUndo) {
  PaperLayout* L = ActivePaperGeometryTarget(st);
  if (!L || st.selectedPaperEntities.empty())
    return;
  const float c = std::cos(angRad), s = std::sin(angRad);
  auto rot = [&](auto& x, auto& y) {
    const float dx = x - baseX, dy = y - baseY;
    x = baseX + dx * c - dy * s;
    y = baseY + dx * s + dy * c;
  };
  if (pushUndo)
    DropPaperLeadersFromSelection(st, "ROTATE", log);
  if (st.selectedPaperEntities.empty())
    return;
  if (pushUndo)
    PushUndoSnapshot(st, "Rotate paper geometry");
  for (const PaperRef& r : st.selectedPaperEntities) {
    switch (r.type) {
    case PaperRef::Type::Line: {
      const size_t i = static_cast<size_t>(r.index) * 6;
      if (i + 5 >= L->paperLines.size())
        break;
      rot(L->paperLines[i], L->paperLines[i + 1]);
      rot(L->paperLines[i + 3], L->paperLines[i + 4]);
      break;
    }
    case PaperRef::Type::Circle: {
      const size_t i = static_cast<size_t>(r.index) * 3;
      if (i + 2 < L->paperCircles.size())
        rot(L->paperCircles[i], L->paperCircles[i + 1]);  // radius is rotation-invariant
      break;
    }
    case PaperRef::Type::Arc: {
      if (r.index >= 0 && static_cast<size_t>(r.index) < L->paperArcs.size()) {
        CadArc& a = L->paperArcs[static_cast<size_t>(r.index)];
        rot(a.cx, a.cy);
        a.startRad += angRad;
      }
      break;
    }
    case PaperRef::Type::Ellipse: {
      if (r.index >= 0 && static_cast<size_t>(r.index) < L->paperEllipses.size()) {
        CadEllipse& e = L->paperEllipses[static_cast<size_t>(r.index)];
        rot(e.cx, e.cy);
        const float mx = e.majVx * c - e.majVy * s;  // rotate the major-axis vector
        const float my = e.majVx * s + e.majVy * c;
        e.majVx = mx; e.majVy = my;
      }
      break;
    }
    case PaperRef::Type::Polyline: {
      const int pi = r.index;
      if (pi < 0 || (static_cast<size_t>(pi)+ 1) >= L->paperPolyOffsets.size())
        break;
      const int v0 = L->paperPolyOffsets[static_cast<size_t>(pi)];
      const int v1 = L->paperPolyOffsets[(static_cast<size_t>(pi)+ 1)];
      for (int vi = v0; vi < v1; ++vi)
        rot(L->paperPolyVerts[(static_cast<size_t>(vi)* 3)], L->paperPolyVerts[(static_cast<size_t>(vi)* 3 + 1)]);
      break;
    }
    case PaperRef::Type::Text: {
      if (r.index >= 0 && static_cast<size_t>(r.index) < L->paperTexts.size()) {
        rot(L->paperTexts[static_cast<size_t>(r.index)].insX, L->paperTexts[static_cast<size_t>(r.index)].insY);
        L->paperTexts[static_cast<size_t>(r.index)].rotationRad += angRad;
      }
      break;
    }
    case PaperRef::Type::Block: {
      if (r.index >= 0 && static_cast<size_t>(r.index) < L->paperBlockRefs.size())
        CadBlockRotateZ(&L->paperBlockRefs[static_cast<size_t>(r.index)], baseX, baseY, angRad);
      break;
    }
    case PaperRef::Type::Table: {
      if (r.index >= 0 && static_cast<size_t>(r.index) < L->paperTables.size())
        CadTableRotateAround(&L->paperTables[static_cast<size_t>(r.index)], baseX, baseY, angRad);
      break;
    }
    case PaperRef::Type::Leader: {
      if (r.index >= 0 && static_cast<size_t>(r.index) < L->paperMultileaders.size()) {
        CadMultileader& leader = L->paperMultileaders[static_cast<size_t>(r.index)];
        auto rotatePath = [&](std::vector<float>& xyz) {
          for (size_t i = 0; i + 2 < xyz.size(); i += 3)
            rot(xyz[i], xyz[i + 1]);
        };
        rotatePath(leader.pathXyz);
        for (auto& path : leader.extraLeaderPaths) rotatePath(path);
        rot(leader.label.insX, leader.label.insY);
        float minX = std::numeric_limits<float>::infinity();
        float minY = std::numeric_limits<float>::infinity();
        float maxX = -minX;
        float maxY = -minY;
        const std::array<std::array<float, 2>, 4> corners{{
            {{leader.label.boxMinX, leader.label.boxMinY}},
            {{leader.label.boxMinX, leader.label.boxMaxY}},
            {{leader.label.boxMaxX, leader.label.boxMinY}},
            {{leader.label.boxMaxX, leader.label.boxMaxY}},
        }};
        for (const auto& corner : corners) {
          float x = corner[0], y = corner[1];
          rot(x, y);
          minX = std::min(minX, x); minY = std::min(minY, y);
          maxX = std::max(maxX, x); maxY = std::max(maxY, y);
        }
        leader.label.boxMinX = minX; leader.label.boxMinY = minY;
        leader.label.boxMaxX = maxX; leader.label.boxMaxY = maxY;
        leader.label.rotationRad += angRad;
      }
      break;
    }
    case PaperRef::Type::Spline: {
      if (r.index >= 0 && static_cast<size_t>(r.index) < L->paperSplines.size())
        spline::MapPoints(&L->paperSplines[static_cast<size_t>(r.index)], [&](const spline::Vec3& v) {
          float x = static_cast<float>(v.x), y = static_cast<float>(v.y);
          rot(x, y);
          return spline::Vec3{x, y, v.z};
        });
      break;
    }
    case PaperRef::Type::Point: {
      if (r.index >= 0 && static_cast<size_t>(r.index) < L->paperPoints.size()) {
        float x = static_cast<float>(L->paperPoints[static_cast<size_t>(r.index)].x);
        float y = static_cast<float>(L->paperPoints[static_cast<size_t>(r.index)].y);
        rot(x, y);
        L->paperPoints[static_cast<size_t>(r.index)].x = x;
        L->paperPoints[static_cast<size_t>(r.index)].y = y;
      }
      break;
    }
    }
  }
  BumpCadGpuCache(st);
  log.push_back("ROTATE — rotated paper object(s).");
}

/// REQ-103 MIRROR, pure-paper-space path. Deliberately simpler than the model-space command: it
/// always duplicates and keeps the source (no interactive "Erase source objects?" prompt), because
/// the pure-paper-space click flow (CadUi.cpp's \c paperMirrorPhase state machine) has no text-entry
/// surface to ask a Y/N question through — the same reason paper ROTATE has no copy mode at all.
/// Recorded as a scoped simplification in TASK-094, not a silent gap: extending paper space with an
/// erase-source toggle is separate future scope if a user asks for it.
void MirrorSelectedPaperEntities(AppCommandState& st, float x0In, float y0In, float x1In, float y1In,
                                 std::vector<std::string>& log) {
  PaperLayout* L = ActivePaperGeometryTarget(st);
  if (!L || st.selectedPaperEntities.empty())
    return;
  // Self-contained reflect math (not the free \c ReflectPtAcrossLine/ReflectAngleAcrossLine — those
  // are defined later in this translation unit; \c RotateSelectedPaperEntities above has the same
  // shape for the same reason, a local \c rot lambda rather than calling \c RotateAroundBase).
  const float mdx = x1In - x0In, mdy = y1In - y0In;
  const float mlen2 = mdx * mdx + mdy * mdy;
  auto refl = [&](auto& x, auto& y) {
    if (mlen2 < 1e-12f)
      return;
    const float t = ((x - x0In) * mdx + (y - y0In) * mdy) / mlen2;
    const float px = x0In + t * mdx, py = y0In + t * mdy;
    x = 2.f * px - x;
    y = 2.f * py - y;
  };
  const float mphi = std::atan2(mdy, mdx);
  auto reflAngle = [&](float ang) { return 2.f * mphi - ang; };
  DropPaperLeadersFromSelection(st, "MIRROR", log);
  if (st.selectedPaperEntities.empty())
    return;
  PushUndoSnapshot(st, "Mirror paper geometry");
  std::vector<PaperRef> newSel;
  auto attrAt = [](const std::vector<EntityAttributes>& v, int i) {
    return (i >= 0 && static_cast<size_t>(i) < v.size()) ? v[static_cast<size_t>(i)] : EntityAttributes{};
  };
  for (const PaperRef& r : st.selectedPaperEntities) {
    switch (r.type) {
    case PaperRef::Type::Line: {
      const size_t i = static_cast<size_t>(r.index) * 6;
      if (i + 5 >= L->paperLines.size())
        break;
      for (int k = 0; k < 6; ++k)
        L->paperLines.push_back(L->paperLines[i + static_cast<size_t>(k)]);
      const size_t j = L->paperLines.size() - 6;
      refl(L->paperLines[j], L->paperLines[j + 1]);
      refl(L->paperLines[j + 3], L->paperLines[j + 4]);
      L->paperLineAttrs.push_back(attrAt(L->paperLineAttrs, r.index));
      newSel.push_back({PaperRef::Type::Line, static_cast<int>(L->paperLines.size() / 6) - 1});
      break;
    }
    case PaperRef::Type::Circle: {
      const size_t i = static_cast<size_t>(r.index) * 3;
      if (i + 2 >= L->paperCircles.size())
        break;
      float cx = L->paperCircles[i], cy = L->paperCircles[i + 1];
      refl(cx, cy);
      L->paperCircles.push_back(cx);
      L->paperCircles.push_back(cy);
      L->paperCircles.push_back(L->paperCircles[i + 2]);  // radius — reflection is an isometry
      L->paperCircleAttrs.push_back(attrAt(L->paperCircleAttrs, r.index));
      newSel.push_back({PaperRef::Type::Circle, static_cast<int>(L->paperCircles.size() / 3) - 1});
      break;
    }
    case PaperRef::Type::Arc: {
      if (r.index < 0 || static_cast<size_t>(r.index) >= L->paperArcs.size())
        break;
      CadArc a = L->paperArcs[static_cast<size_t>(r.index)];
      // Same reflect-the-old-end-angle-into-the-new-start rule as the model-space path — a
      // reflection reverses handedness, so the center alone does not say which way it now sweeps.
      const float newStart = reflAngle(a.startRad + a.sweepRad);
      refl(a.cx, a.cy);
      a.startRad = newStart;
      L->paperArcs.push_back(a);
      L->paperArcAttrs.push_back(attrAt(L->paperArcAttrs, r.index));
      newSel.push_back({PaperRef::Type::Arc, static_cast<int>(L->paperArcs.size()) - 1});
      break;
    }
    case PaperRef::Type::Ellipse: {
      if (r.index < 0 || static_cast<size_t>(r.index) >= L->paperEllipses.size())
        break;
      CadEllipse e = L->paperEllipses[static_cast<size_t>(r.index)];
      float mx = static_cast<float>(e.cx + e.majVx), my = static_cast<float>(e.cy + e.majVy);
      refl(e.cx, e.cy);
      refl(mx, my);
      e.majVx = static_cast<float>(mx - e.cx);
      e.majVy = static_cast<float>(my - e.cy);
      L->paperEllipses.push_back(e);
      L->paperEllAttrs.push_back(attrAt(L->paperEllAttrs, r.index));
      newSel.push_back({PaperRef::Type::Ellipse, static_cast<int>(L->paperEllipses.size()) - 1});
      break;
    }
    case PaperRef::Type::Polyline: {
      const int pi = r.index;
      if (pi < 0 || (static_cast<size_t>(pi)+ 1) >= L->paperPolyOffsets.size())
        break;
      const int v0 = L->paperPolyOffsets[static_cast<size_t>(pi)];
      const int v1 = L->paperPolyOffsets[(static_cast<size_t>(pi)+ 1)];
      const int baseVert = L->paperPolyOffsets.empty() ? 0 : L->paperPolyOffsets.back();
      for (int vi = v0; vi < v1; ++vi) {
        float px = L->paperPolyVerts[(static_cast<size_t>(vi)* 3)];
        float py = L->paperPolyVerts[(static_cast<size_t>(vi)* 3 + 1)];
        refl(px, py);
        L->paperPolyVerts.push_back(px);
        L->paperPolyVerts.push_back(py);
        L->paperPolyVerts.push_back(L->paperPolyVerts[(static_cast<size_t>(vi)* 3 + 2)]);
      }
      L->paperPolyOffsets.push_back(baseVert + (v1 - v0));
      L->paperPolyClosed.push_back(static_cast<size_t>(pi) < L->paperPolyClosed.size() ? L->paperPolyClosed[static_cast<size_t>(pi)] : 0u);
      L->paperPolyAttrs.push_back(attrAt(L->paperPolyAttrs, pi));
      newSel.push_back({PaperRef::Type::Polyline, static_cast<int>(L->paperPolyOffsets.size()) - 2});
      break;
    }
    case PaperRef::Type::Text: {
      if (r.index < 0 || static_cast<size_t>(r.index) >= L->paperTexts.size())
        break;
      CadAnnotation a = L->paperTexts[static_cast<size_t>(r.index)];
      // MIRRTEXT-off (D-2026-08-23-j): insertion point reflects, rotationRad (glyph orientation)
      // is left untouched — same rule as the model-space Text/Mtext path.
      refl(a.insX, a.insY);
      L->paperTexts.push_back(std::move(a));
      L->paperTextAttrs.push_back(attrAt(L->paperTextAttrs, r.index));
      newSel.push_back({PaperRef::Type::Text, static_cast<int>(L->paperTexts.size()) - 1});
      break;
    }
    case PaperRef::Type::Block: {
      if (r.index < 0 || static_cast<size_t>(r.index) >= L->paperBlockRefs.size())
        break;
      CadBlockRef c = L->paperBlockRefs[static_cast<size_t>(r.index)];
      CadBlockMirror(&c, x0In, y0In, x1In, y1In);
      L->paperBlockRefs.push_back(std::move(c));
      L->paperBlockRefAttrs.push_back(attrAt(L->paperBlockRefAttrs, r.index));
      newSel.push_back({PaperRef::Type::Block, static_cast<int>(L->paperBlockRefs.size()) - 1});
      break;
    }
    case PaperRef::Type::Table: {
      if (r.index < 0 || static_cast<size_t>(r.index) >= L->paperTables.size())
        break;
      CadTable c = L->paperTables[static_cast<size_t>(r.index)];
      CadTableReflectAcrossLine(&c, x0In, y0In, x1In, y1In);
      L->paperTables.push_back(std::move(c));
      L->paperTableAttrs.push_back(attrAt(L->paperTableAttrs, r.index));
      newSel.push_back({PaperRef::Type::Table, static_cast<int>(L->paperTables.size()) - 1});
      break;
    }
    case PaperRef::Type::Leader:  // dropped from the selection above
      break;
    case PaperRef::Type::Spline: {
      if (r.index < 0 || static_cast<size_t>(r.index) >= L->paperSplines.size())
        break;
      CadSpline c = L->paperSplines[static_cast<size_t>(r.index)];
      spline::MapPoints(&c, [&](const spline::Vec3& v) {
        float x = static_cast<float>(v.x), y = static_cast<float>(v.y);
        refl(x, y);
        return spline::Vec3{x, y, v.z};
      });
      L->paperSplines.push_back(std::move(c));
      L->paperSplineAttrs.push_back(attrAt(L->paperSplineAttrs, r.index));
      newSel.push_back({PaperRef::Type::Spline, static_cast<int>(L->paperSplines.size()) - 1});
      break;
    }
    case PaperRef::Type::Point: {
      if (r.index < 0 || static_cast<size_t>(r.index) >= L->paperPoints.size())
        break;
      CadPoint c = L->paperPoints[static_cast<size_t>(r.index)];
      float x = static_cast<float>(c.x), y = static_cast<float>(c.y);
      refl(x, y);
      c.x = x;
      c.y = y;
      L->paperPoints.push_back(c);
      L->paperPointAttrs.push_back(attrAt(L->paperPointAttrs, r.index));
      newSel.push_back({PaperRef::Type::Point, static_cast<int>(L->paperPoints.size()) - 1});
      break;
    }
    }
  }
  if (!newSel.empty())
    st.selectedPaperEntities = newSel;
  BumpCadGpuCache(st);
  log.push_back("MIRROR — mirrored paper object(s) (source kept).");
}


void EnterFloatingModelSpace(AppCommandState& cmd, int layoutIdx, int vpIdx, std::vector<std::string>& log) {
  if (layoutIdx < 0 || static_cast<size_t>(layoutIdx) >= cmd.drawing.paperLayouts.size())
    return;
  PaperLayout& L = cmd.drawing.paperLayouts[static_cast<size_t>(layoutIdx)];
  if (vpIdx < 0 || static_cast<size_t>(vpIdx) >= L.viewports.size())
    return;
  if (!L.viewports[static_cast<size_t>(vpIdx)].on) {
    log.push_back("MSPACE - the selected viewport is off.");
    return;
  }
  cmd.floatingViewportLayout = layoutIdx;
  cmd.floatingViewportIndex = vpIdx;
  stepengine::EndSession(cmd);
  cmd.paperMovePhase = 0;
  cmd.paperGripCorner = -2;
  cmd.paperSelBoxActive = false;
  // REQ-155 (issue #155): adopt this viewport's active UCS as the working frame while editing
  // through it — coordinate entry, the grid, ORTHO, the readout and UCSFOLLOW all read cmd.activeUcs,
  // so the swap makes every one of them viewport-scoped with no per-site change. The drawing's own
  // UCS is parked in the stash and restored on exit.
  if (!cmd.floatingUcsSwapActive) {  // guard against a double-enter leaking the stash
    cmd.drawingActiveUcsStash = cmd.activeUcs;
    cmd.activeUcs = L.viewports[static_cast<size_t>(vpIdx)].activeUcs;
    cmd.floatingUcsSwapActive = true;
  }
  log.push_back("Floating model space — editing viewport " + std::to_string(vpIdx + 1) +
                " in place; Esc / FLOAT button / PSPACE returns to paper editing.");
  BumpCadGpuCache(cmd);
}

void ExitFloatingModelSpace(AppCommandState& cmd, std::vector<std::string>& log) {
  if (cmd.floatingViewportIndex < 0)
    return;
  // REQ-155: fold the working frame back into the viewport, restore the drawing's UCS. Done before
  // the indices are cleared because it needs them to find the viewport.
  if (cmd.floatingUcsSwapActive) {
    if (cmd.floatingViewportLayout >= 0 &&
        static_cast<size_t>(cmd.floatingViewportLayout) < cmd.drawing.paperLayouts.size()) {
      PaperLayout& L = cmd.drawing.paperLayouts[static_cast<size_t>(cmd.floatingViewportLayout)];
      if (cmd.floatingViewportIndex >= 0 &&
          static_cast<size_t>(cmd.floatingViewportIndex) < L.viewports.size())
        L.viewports[static_cast<size_t>(cmd.floatingViewportIndex)].activeUcs = cmd.activeUcs;
    }
    cmd.activeUcs = cmd.drawingActiveUcsStash;
    cmd.floatingUcsSwapActive = false;
  }
  cmd.floatingViewportLayout = -1;
  cmd.floatingViewportIndex = -1;
  stepengine::EndSession(cmd);
  log.push_back("Returned to paper space.");
  BumpCadGpuCache(cmd);
}

// --- Page setups (named, drawing-wide) ---

void EnsureStandardPageSetup(AppCommandState& cmd) {
  for (const PageSetup& p : cmd.drawing.savedPageSetups)
    if (p.name == "Standard")
      return;
  PageSetup std;
  std.name = "Standard";  // defaults: ARCH D, landscape, 1:1
  cmd.drawing.savedPageSetups.insert(cmd.drawing.savedPageSetups.begin(), std);
}

void ApplyPageSetupToLayout(PaperLayout& layout, const PageSetup& ps) {
  layout.presetIdx = ps.presetIdx;
  layout.portraitWidthIn = ps.portraitWidthIn;
  layout.portraitHeightIn = ps.portraitHeightIn;
  layout.landscape = ps.landscape;
  layout.fitToPaper = ps.fitToPaper;
  layout.scaleModelPerPaperIn = ps.scaleModelPerPaperIn;
  layout.plotArea = ps.plotArea;
  layout.offsetXIn = ps.offsetXIn;
  layout.offsetYIn = ps.offsetYIn;
  layout.centerPlot = ps.centerPlot;
  layout.plotStyleTablePath = ps.plotStyleTablePath;
  layout.pageSetupName = ps.name;
}

PageSetup PageSetupFromLayout(const PaperLayout& layout, const std::string& name) {
  PageSetup ps;
  ps.name = name;
  ps.presetIdx = layout.presetIdx;
  ps.portraitWidthIn = layout.portraitWidthIn;
  ps.portraitHeightIn = layout.portraitHeightIn;
  ps.landscape = layout.landscape;
  ps.fitToPaper = layout.fitToPaper;
  ps.scaleModelPerPaperIn = layout.scaleModelPerPaperIn;
  ps.plotArea = layout.plotArea;
  ps.offsetXIn = layout.offsetXIn;
  ps.offsetYIn = layout.offsetYIn;
  ps.centerPlot = layout.centerPlot;
  ps.plotStyleTablePath = layout.plotStyleTablePath;
  return ps;
}

void MoveOrCopyLayout(AppCommandState& cmd, int layoutIdx, int beforeIdx, bool makeCopy,
                      std::vector<std::string>& log) {
  const int n = static_cast<int>(cmd.drawing.paperLayouts.size());
  if (layoutIdx < 0 || layoutIdx >= n)
    return;
  if (makeCopy) {
    CopyPaperLayout(cmd, layoutIdx, {}, log);
    return;
  }
  PaperLayout item = cmd.drawing.paperLayouts[static_cast<size_t>(layoutIdx)];
  cmd.drawing.paperLayouts.erase(cmd.drawing.paperLayouts.begin() + layoutIdx);
  int insertAt = beforeIdx;
  if (insertAt > layoutIdx)
    --insertAt;  // account for the removed slot
  insertAt = std::clamp(insertAt, 0, static_cast<int>(cmd.drawing.paperLayouts.size()));
  cmd.drawing.paperLayouts.insert(cmd.drawing.paperLayouts.begin() + insertAt, item);
  SetActiveSpace(cmd, insertAt);
  log.push_back("Layout \"" + item.name + "\" moved.");
  BumpCadGpuCache(cmd);
}

int CopyPaperLayout(AppCommandState& cmd, int layoutIdx, const std::string& requestedName,
                    std::vector<std::string>& log) {
  const int n = static_cast<int>(cmd.drawing.paperLayouts.size());
  if (layoutIdx < 0 || layoutIdx >= n) {
    log.push_back("LAYOUT COPY - no source layout was found.");
    return -1;
  }

  auto nameTaken = [&](const std::string& name) {
    const std::string folded = StringUtil::toLowerAsciiCopy(name);
    return std::any_of(cmd.drawing.paperLayouts.begin(), cmd.drawing.paperLayouts.end(), [&](const PaperLayout& layout) {
      return StringUtil::toLowerAsciiCopy(layout.name) == folded;
    });
  };

  PaperLayout copy = cmd.drawing.paperLayouts[static_cast<size_t>(layoutIdx)];
  std::string name = StringUtil::trimCopy(requestedName);
  if (name.empty()) {
    const std::string base = copy.name;
    for (int suffix = 2;; ++suffix) {
      name = base + " (" + std::to_string(suffix) + ")";
      if (!nameTaken(name))
        break;
    }
  } else if (nameTaken(name)) {
    log.push_back("LAYOUT COPY - a layout named \"" + name + "\" already exists.");
    return -1;
  }

  copy.name = name;
  // Append instead of inserting beside the source. This keeps every existing layout index stable,
  // including the current layout index held by the undo system, which does not snapshot tab state.
  const int insertAt = n;
  // Validation above must finish before this snapshot so a collision is a true no-op, including history.
  PushUndoSnapshot(cmd, "Copy layout");
  cmd.drawing.paperLayouts.insert(cmd.drawing.paperLayouts.begin() + insertAt, std::move(copy));

  BumpCadGpuCache(cmd);
  log.push_back("Layout copied as \"" + name + "\".");
  return insertAt;
}


// #764 — paper-space SCALE: uniform scale of the selected paper entities about (baseX, baseY), all in
// paper inches. Model-space SCALE only ever sees the model selection, so before this a paper SCALE
// restarted at "click objects" and changed nothing on the sheet. Viewports are not scaled here.
void ScaleSelectedPaperEntities(AppCommandState& st, float baseX, float baseY, float factor,
                                std::vector<std::string>& log) {
  PaperLayout* L = ActivePaperGeometryTarget(st);
  if (!L || st.selectedPaperEntities.empty())
    return;
  if (!(factor > 0.f) || !std::isfinite(factor)) {
    log.push_back("SCALE — scale factor must be a positive finite number.");
    return;
  }
  DropPaperLeadersFromSelection(st, "SCALE", log);
  if (st.selectedPaperEntities.empty())
    return;
  PushUndoSnapshot(st, "Scale paper geometry");
  for (const PaperRef& r : st.selectedPaperEntities) {
    switch (r.type) {
    case PaperRef::Type::Line: {
      const size_t i = static_cast<size_t>(r.index) * 6;
      if (i + 5 >= L->paperLines.size())
        break;
      ScalePtAroundBase(baseX, baseY, factor, &L->paperLines[i], &L->paperLines[i + 1]);
      ScalePtAroundBase(baseX, baseY, factor, &L->paperLines[i + 3], &L->paperLines[i + 4]);
      break;
    }
    case PaperRef::Type::Circle: {
      const size_t i = static_cast<size_t>(r.index) * 3;
      if (i + 2 >= L->paperCircles.size())
        break;
      ScalePtAroundBase(baseX, baseY, factor, &L->paperCircles[i], &L->paperCircles[i + 1]);
      L->paperCircles[i + 2] *= factor;
      break;
    }
    case PaperRef::Type::Arc: {
      if (r.index < 0 || static_cast<size_t>(r.index) >= L->paperArcs.size())
        break;
      CadArc& a = L->paperArcs[static_cast<size_t>(r.index)];
      ScalePtAroundBase(baseX, baseY, factor, &a.cx, &a.cy);
      a.r *= factor;
      break;
    }
    case PaperRef::Type::Ellipse: {
      if (r.index < 0 || static_cast<size_t>(r.index) >= L->paperEllipses.size())
        break;
      CadEllipse& e = L->paperEllipses[static_cast<size_t>(r.index)];
      ScalePtAroundBase(baseX, baseY, factor, &e.cx, &e.cy);
      e.majVx *= factor;  // the major-axis vector is relative to the centre, so it just scales
      e.majVy *= factor;
      break;
    }
    case PaperRef::Type::Polyline: {
      const int pi = r.index;
      if (pi < 0 || (static_cast<size_t>(pi)+ 1) >= L->paperPolyOffsets.size())
        break;
      const int v0 = L->paperPolyOffsets[static_cast<size_t>(pi)];
      const int v1 = L->paperPolyOffsets[(static_cast<size_t>(pi)+ 1)];
      for (int vi = v0; vi < v1; ++vi)
        ScalePtAroundBase(baseX, baseY, factor, &L->paperPolyVerts[(static_cast<size_t>(vi)* 3)],
                          &L->paperPolyVerts[(static_cast<size_t>(vi)* 3 + 1)]);
      break;
    }
    case PaperRef::Type::Text: {
      if (r.index >= 0 && static_cast<size_t>(r.index) < L->paperTexts.size())
        ScaleAnnotationAroundBase(st, L->paperTexts[static_cast<size_t>(r.index)], baseX, baseY, factor);
      break;
    }
    case PaperRef::Type::Block: {
      if (r.index >= 0 && static_cast<size_t>(r.index) < L->paperBlockRefs.size())
        CadBlockScaleAbout(&L->paperBlockRefs[static_cast<size_t>(r.index)], baseX, baseY, factor);
      break;
    }
    case PaperRef::Type::Table: {
      if (r.index >= 0 && static_cast<size_t>(r.index) < L->paperTables.size())
        CadTableScaleAround(&L->paperTables[static_cast<size_t>(r.index)], baseX, baseY, factor);
      break;
    }
    case PaperRef::Type::Leader:  // dropped from the selection above
      break;
    case PaperRef::Type::Spline: {
      if (r.index >= 0 && static_cast<size_t>(r.index) < L->paperSplines.size())
        spline::MapPoints(&L->paperSplines[static_cast<size_t>(r.index)], [&](const spline::Vec3& v) {
          double x = v.x, y = v.y;
          ScalePtAroundBase(baseX, baseY, factor, &x, &y);
          return spline::Vec3{x, y, v.z};
        });
      break;
    }
    case PaperRef::Type::Point: {
      if (r.index >= 0 && static_cast<size_t>(r.index) < L->paperPoints.size()) {
        double x = L->paperPoints[static_cast<size_t>(r.index)].x;
        double y = L->paperPoints[static_cast<size_t>(r.index)].y;
        ScalePtAroundBase(baseX, baseY, factor, &x, &y);
        L->paperPoints[static_cast<size_t>(r.index)].x = x;
        L->paperPoints[static_cast<size_t>(r.index)].y = y;
      }
      break;
    }
    }
  }
  BumpCadGpuCache(st);
  log.push_back("SCALE — scaled paper object(s).");
}


// ---------------------------------------------------------------------------------------------
// LAYOUT (REQ-025, issue #751/#753 V3) — typed equivalent of the layout tab bar's add/rename/
// delete (REQ-025). One-shot, text-only, same shape as PIPESYS: a layout isn't routed or picked,
// so it needs no `AppCommandState::Kind` of its own.
// ---------------------------------------------------------------------------------------------

void HandleLayoutCommand(const std::string& args, AppCommandState& st, std::vector<std::string>& log) {
  std::istringstream iss(args);
  std::string verb;
  iss >> verb;
  for (char& c : verb) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

  if (verb.empty()) {
    log.push_back("LAYOUT - usage: LAYOUT [New/Rename/Delete].");
    return;
  }

  if (verb == "new") {
    std::string name;
    std::getline(iss, name);
    name = StringUtil::trimCopy(name);
    const int idx = AddPaperLayout(st);
    if (!name.empty()) {
      bool taken = false;
      for (int i = 0; i < static_cast<int>(st.drawing.paperLayouts.size()); ++i) {
        if (i != idx && st.drawing.paperLayouts[static_cast<size_t>(i)].name == name) {
          taken = true;
          break;
        }
      }
      if (taken) {
        log.push_back("LAYOUT NEW - a layout named \"" + name + "\" already exists; created \"" +
                      st.drawing.paperLayouts[static_cast<size_t>(idx)].name + "\" instead.");
      } else {
        st.drawing.paperLayouts[static_cast<size_t>(idx)].name = name;
      }
    }
    log.push_back("LAYOUT - created \"" + st.drawing.paperLayouts[static_cast<size_t>(idx)].name + "\".");
    return;
  }

  if (verb == "copy") {
    std::string rest;
    std::getline(iss, rest);
    rest = StringUtil::trimCopy(rest);
    int sourceIx = -1;
    size_t sourceLen = 0;
    for (size_t i = 0; i < st.drawing.paperLayouts.size(); ++i) {
      const std::string& candidate = st.drawing.paperLayouts[i].name;
      if (rest == candidate) {
        sourceIx = static_cast<int>(i);
        sourceLen = candidate.size();
        break;
      }
      if (rest.size() > candidate.size() && rest.compare(0, candidate.size(), candidate) == 0 &&
          rest[candidate.size()] == ' ' && candidate.size() > sourceLen) {
        sourceIx = static_cast<int>(i);
        sourceLen = candidate.size();
      }
    }
    std::string copyName;
    if (sourceIx >= 0) {
      copyName = StringUtil::trimCopy(rest.substr(sourceLen));
    } else if (rest.empty() && st.activeSpaceIndex >= 0 &&
               st.activeSpaceIndex < static_cast<int>(st.drawing.paperLayouts.size())) {
      sourceIx = st.activeSpaceIndex;
    }
    if (sourceIx < 0) {
      log.push_back("LAYOUT COPY - use LAYOUT COPY [source layout] [new name], or switch to a paper layout first.");
      return;
    }
    if (copyName.size() > 3 && StringUtil::toLowerAsciiCopy(copyName.substr(0, 3)) == "as ")
      copyName = StringUtil::trimCopy(copyName.substr(3));
    CopyPaperLayout(st, sourceIx, copyName, log);
    return;
  }

  // RENAME and DELETE both need an existing layout. Names may contain spaces, so the same
  // greedy-longest-prefix match PIPESYS RENAME uses (above) resolves <target> against the rest of
  // the line; with nothing left over after the match, the active layout is the default target.
  if (verb == "rename" || verb == "delete") {
    std::string rest;
    std::getline(iss, rest);
    rest = StringUtil::trimCopy(rest);
    int targetIx = -1;
    size_t matchLen = 0;
    for (size_t i = 0; i < st.drawing.paperLayouts.size(); ++i) {
      const std::string& candidate = st.drawing.paperLayouts[i].name;
      if (rest == candidate) {
        targetIx = static_cast<int>(i);
        matchLen = rest.size();
        break;
      }
      if (rest.size() > candidate.size() && rest.compare(0, candidate.size(), candidate) == 0 &&
          rest[candidate.size()] == ' ' && candidate.size() > matchLen) {
        targetIx = static_cast<int>(i);
        matchLen = candidate.size();
      }
    }
    std::string remainder = (targetIx >= 0) ? StringUtil::trimCopy(rest.substr(matchLen)) : rest;
    if (targetIx < 0) {
      // No layout name matched (or the whole line was blank) — fall back to the active layout.
      if (st.activeSpaceIndex >= 0 && st.activeSpaceIndex < static_cast<int>(st.drawing.paperLayouts.size())) {
        targetIx = st.activeSpaceIndex;
        remainder = rest;
      }
    }
    if (targetIx < 0) {
      log.push_back("LAYOUT " + verb + " - no paper layout to target; switch to one first, or name it, "
                    "e.g. LAYOUT " + verb + " Layout1.");
      return;
    }

    if (verb == "rename") {
      const std::string newName = StringUtil::trimCopy(remainder);
      if (newName.empty()) {
        log.push_back("LAYOUT RENAME - a new name is required, e.g. LAYOUT RENAME " +
                      st.drawing.paperLayouts[static_cast<size_t>(targetIx)].name + " Site Plan.");
        return;
      }
      for (int i = 0; i < static_cast<int>(st.drawing.paperLayouts.size()); ++i) {
        if (i != targetIx && st.drawing.paperLayouts[static_cast<size_t>(i)].name == newName) {
          log.push_back("LAYOUT RENAME - a layout named \"" + newName + "\" already exists.");
          return;
        }
      }
      const std::string oldName = st.drawing.paperLayouts[static_cast<size_t>(targetIx)].name;
      PushUndoSnapshot(st, "Rename layout");
      st.drawing.paperLayouts[static_cast<size_t>(targetIx)].name = newName;
      log.push_back("LAYOUT - \"" + oldName + "\" renamed to \"" + newName + "\".");
      return;
    }

    // DELETE: same path (confirmation/undo) as the tab-bar's own Delete, per REQ-401.
    const std::string deletedName = st.drawing.paperLayouts[static_cast<size_t>(targetIx)].name;
    DeletePaperLayout(st, targetIx);
    log.push_back("LAYOUT - \"" + deletedName + "\" deleted.");
    return;
  }

  log.push_back("LAYOUT - unknown option \"" + verb + "\". Use NEW, COPY, RENAME or DELETE.");
}

// ---------------------------------------------------------------------------------------------
// VPSCALE (REQ-027, issue #751/#753 V3) — typed equivalent of the viewport-scale combo
// (DrawPlotScaleCombo in CadUi.cpp), targeting the same "current viewport" `CurrentViewport`
// resolves: the floating viewport when inside one, else the single selected paper viewport.
// ---------------------------------------------------------------------------------------------

namespace {

/// Accepts "1:N" / "N:1" ratio notation (model:paper, matching the scale list's own sense) or a
/// bare positive number read directly as model units per plotted inch. Returns false if \p raw
/// parses as neither.
bool ParseTypedVpScale(const std::string& raw, int insUnits, float& outMup) {
  const std::string s = StringUtil::trimCopy(raw);
  if (s.empty())
    return false;
  const size_t colon = s.find(':');
  try {
    if (colon != std::string::npos) {
      const double paperPart = std::stod(StringUtil::trimCopy(s.substr(0, colon)));
      const double modelPart = std::stod(StringUtil::trimCopy(s.substr(colon + 1)));
      if (paperPart <= 0.0 || modelPart <= 0.0)
        return false;
      const double ratio = modelPart / paperPart;  // model units per one paper unit
      outMup = static_cast<float>(PlotScaleUnitsAreMetric(insUnits)
                                       ? MetricPlotScaleUnitsPerInch(insUnits) * ratio
                                       : (insUnits == 1 ? 12.0 : 1.0) * ratio);
      return true;
    }
    const double v = std::stod(s);
    if (v <= 0.0)
      return false;
    outMup = static_cast<float>(v);
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

} // namespace

void HandleVpscaleCommand(const std::string& args, AppCommandState& st, std::vector<std::string>& log) {
  Viewport* vp = CurrentViewport(st);
  if (!vp) {
    log.push_back("VPSCALE - no current viewport; enter a floating viewport (MSPACE) or select "
                  "exactly one viewport in a paper layout first.");
    return;
  }
  if (vp->locked) {
    log.push_back("VPSCALE - viewport display is locked; unlock it before changing the view scale.");
    return;
  }
  const std::string typed = StringUtil::trimCopy(args);
  if (typed.empty()) {
    log.push_back("VPSCALE - current viewport scale is " +
                  PlotScaleLabel(st.drawing.drawingInsUnits, vp->scaleModelPerPaperIn) +
                  ". Usage: VPSCALE 1:20, or VPSCALE 50.");
    return;
  }
  float mup = 0.f;
  if (!ParseTypedVpScale(typed, st.drawing.drawingInsUnits, mup)) {
    log.push_back("VPSCALE - could not read \"" + typed + "\" as a scale. Use 1:N (e.g. 1:20) or a "
                  "plain number of model units per plotted inch.");
    return;
  }
  PushUndoSnapshot(st, "Viewport scale");
  vp->scaleModelPerPaperIn = mup;
  BumpCadGpuCache(st);
  log.push_back("VPSCALE - viewport scale set to " + PlotScaleLabel(st.drawing.drawingInsUnits, mup) + ".");
}

void HandleVpMatchScaleCommand(const std::string& args, AppCommandState& st, std::vector<std::string>& log) {
  const std::string name = StringUtil::trimCopy(args);
  if (st.activeSpaceIndex < 0 || st.activeSpaceIndex >= static_cast<int>(st.drawing.paperLayouts.size())) {
    log.push_back("VPMATCHSCALE - switch to a paper layout first.");
    return;
  }
  if (name.empty()) {
    log.push_back("VPMATCHSCALE - specify a drawing scale name. Usage: VPMATCHSCALE 1:50.");
    return;
  }
  const auto scaleIt = std::find_if(st.drawing.annotationScales.begin(), st.drawing.annotationScales.end(),
      [&](const CadAnnotationScale& scale) { return scale.name == name; });
  if (scaleIt == st.drawing.annotationScales.end()) {
    log.push_back("VPMATCHSCALE - unknown drawing scale \"" + name + "\".");
    return;
  }
  const float modelPerPaperIn = CadAnnotationScaleModelUnitsPerPlottedInch(*scaleIt);
  if (!(modelPerPaperIn > 1.e-6f) || !std::isfinite(modelPerPaperIn)) {
    log.push_back("VPMATCHSCALE - drawing scale \"" + name + "\" has an invalid ratio.");
    return;
  }
  std::vector<int> indices = st.selectedViewports;
  if (indices.empty() && st.selectedViewportIndex >= 0)
    indices.push_back(st.selectedViewportIndex);
  std::sort(indices.begin(), indices.end());
  indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
  auto& viewports = st.drawing.paperLayouts[static_cast<size_t>(st.activeSpaceIndex)].viewports;
  if (indices.empty() || std::any_of(indices.begin(), indices.end(), [&](int i) {
        return i < 0 || i >= static_cast<int>(viewports.size()) || viewports[static_cast<size_t>(i)].locked;
      })) {
    log.push_back("VPMATCHSCALE - select unlocked paper viewport(s) in the active layout.");
    return;
  }
  if (std::all_of(indices.begin(), indices.end(), [&](int i) {
        return viewports[static_cast<size_t>(i)].scaleModelPerPaperIn == modelPerPaperIn;
      })) {
    log.push_back("VPMATCHSCALE - selected viewport scale already matches \"" + name + "\".");
    return;
  }
  PushUndoSnapshot(st, "Match paper viewport scale");
  for (int i : indices)
    viewports[static_cast<size_t>(i)].scaleModelPerPaperIn = modelPerPaperIn;
  BumpCadGpuCache(st);
  log.push_back("VPMATCHSCALE - applied \"" + name + "\" to " + std::to_string(indices.size()) +
                " paper viewport(s).");
}

// ---------------------------------------------------------------------------------------------
// PAGESETUP (REQ-026, issue #751/#753 V3) — typed equivalent of the layout tab bar's right-click
// "Page Setup Manager…" (CadUi.cpp), opening the same dialog for the active layout.
// ---------------------------------------------------------------------------------------------

void StartPageSetupCommand(AppCommandState& st, std::vector<std::string>& log) {
  if (st.activeSpaceIndex == kModelSpaceIndex) {
    log.push_back("PAGESETUP - not available in model space; switch to a paper layout first.");
    return;
  }
  EnsureStandardPageSetup(st);
  st.pageSetupLayoutIdx = st.activeSpaceIndex;
  st.pageSetupManagerSel = -1;
  st.showPageSetupManager = true;
  log.push_back("PAGESETUP - opened for \"" + st.drawing.paperLayouts[static_cast<size_t>(st.activeSpaceIndex)].name +
                "\".");
}

/// Shared by the prompt and the inline `VS SHADED` form, so neither can set a value the other would

Viewport* CurrentViewport(AppCommandState& st) {
  if (st.activeSpaceIndex < 0 || st.activeSpaceIndex >= static_cast<int>(st.drawing.paperLayouts.size()))
    return nullptr;
  PaperLayout& L = st.drawing.paperLayouts[static_cast<size_t>(st.activeSpaceIndex)];
  // Floating viewport wins when the user is editing the model through one (REQ-036).
  if (InFloatingModelSpace(st) && st.floatingViewportLayout == st.activeSpaceIndex &&
      st.floatingViewportIndex >= 0 && st.floatingViewportIndex < static_cast<int>(L.viewports.size()))
    return &L.viewports[static_cast<size_t>(st.floatingViewportIndex)];
  // Else the single selected viewport in paper space.
  if (st.selectedViewports.size() == 1 && st.selectedViewportIndex >= 0 &&
      st.selectedViewportIndex < static_cast<int>(L.viewports.size()))
    return &L.viewports[static_cast<size_t>(st.selectedViewportIndex)];
  return nullptr;
}

// VPFREEZE / VPTHAW (REQ-046): pick entities in the current viewport; the picked layers freeze/thaw in it.
// The pick itself is handled in the floating-viewport click path (CadUi); here we just enter the mode.
static void StartVpFreezeThaw(AppCommandState& st, bool freeze, std::vector<std::string>& log) {
  using K = AppCommandState::Kind;
  const char* name = freeze ? "VPFREEZE" : "VPTHAW";
  if (CurrentViewport(st) == nullptr) {
    log.push_back(std::string(name) +
                  " — no current viewport. Double-click into a viewport (or select one) first.");
    return;
  }
  if (st.active != K::None && st.active != K::VpFreeze && st.active != K::VpThaw) {
    log.push_back(std::string(name) + " — finish or cancel the active command first.");
    return;
  }
  ResetAllCadDraftTools(st);
  if (freeze)
    stepengine::BeginSession(st, std::make_unique<stepengine::VpFreezeSession>(), /*recordAsLastCommand=*/true);
  else
    stepengine::BeginSession(st, std::make_unique<stepengine::VpThawSession>(), /*recordAsLastCommand=*/true);
  st.selBoxWaitingSecond = false;
  log.push_back(std::string(name) + " — select objects to " + (freeze ? "freeze" : "thaw") +
                " their layer in this viewport. Press Esc or Enter to exit.");
}

void StartVpFreezeCommand(AppCommandState& st, std::vector<std::string>& log) { StartVpFreezeThaw(st, true, log); }
void StartVpThawCommand(AppCommandState& st, std::vector<std::string>& log) { StartVpFreezeThaw(st, false, log); }
