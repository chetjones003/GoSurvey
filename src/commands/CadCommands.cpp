#include "CadCommands.hpp"
#include "OrthoConstrain.hpp"
#include "TextStyle.hpp"
#include "CadCoordinateFrame.hpp"
#include "CadLinetype.hpp"
#include "HatchGeom.hpp"
#include "HatchBoundary.hpp"
#include "geom2d.hpp"
#include "util/benchscene.hpp"
#include "util/meshgeom.hpp"
#include "util/tinbuild.hpp"
#include "util/contourgen.hpp"  // REQ-070 contour generation (ADR-036 (f)) — pure, like tinbuild
#include "io/SurveyCsv.hpp"  // REQ-086: a surface reads its linked point files through the REQ-083 parser
#include "util/gltfimport.hpp"
#include "util/stlimport.hpp"
#include "DwgMeshConvert.hpp"
#include "DwgIo.hpp"
#include "WinFileDialogs.hpp"
#include "NumFormat.hpp"
#include "MtextRichFormat.hpp"
#include "FontRegistry.hpp"
#include "StringUtil.hpp"
#include "AppIcon.hpp"

#include "CadSnap.hpp"

#include <imgui.h>

#include <algorithm>
#include <functional>
#include <array>
#include <cctype>
#include <cstddef>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <limits>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <numeric>
#include <utility>

// REQ-044: stamp the active text style onto a new user TEXT/MTEXT (defined below, used at the commit sites).
static void StampActiveTextStyleOnNewText(AppCommandState& st, CadAnnotation& a);


void SaveDocumentToSnapshot(AppCommandState& cmd, int idx) {
  if (idx < 0 || static_cast<size_t>(idx) >= cmd.documents.size()) return;
  DrawingDocument& doc       = cmd.documents[static_cast<size_t>(idx)];
  doc.viewportPanX           = cmd.viewportPanX;
  doc.viewportPanY           = cmd.viewportPanY;
  doc.viewportZoom           = cmd.viewportZoom;
  doc.viewportPanZ           = cmd.viewportPanZ;
  doc.viewportAzimuthDeg     = cmd.viewportAzimuthDeg;    // camera orientation is per-drawing (REQ-058)
  doc.viewportElevationDeg   = cmd.viewportElevationDeg;
  doc.worldDocumentOriginX   = cmd.worldDocumentOriginX;
  doc.worldDocumentOriginY   = cmd.worldDocumentOriginY;
  doc.nextEntityId           = cmd.nextEntityId;  // per-drawing id counter (REQ-076)
  doc.userLinesFlat          = cmd.userLinesFlat;
  doc.userLineAttrs          = cmd.userLineAttrs;
  doc.userCirclesCxCyZR       = cmd.userCirclesCxCyZR;
  doc.userCircleAttrs        = cmd.userCircleAttrs;
  doc.userArcs               = cmd.userArcs;
  doc.userArcAttrs           = cmd.userArcAttrs;
  doc.userEllipses           = cmd.userEllipses;
  doc.userEllAttrs           = cmd.userEllAttrs;
  doc.userPolylineOffsets    = cmd.userPolylineOffsets;
  doc.userPolylineVerts      = cmd.userPolylineVerts;
  doc.userPolylineClosed     = cmd.userPolylineClosed;
  doc.userPolylineAttrs      = cmd.userPolylineAttrs;
  doc.featureLineOffsets     = cmd.featureLineOffsets;   // REQ-087
  doc.featureLineVerts       = cmd.featureLineVerts;
  doc.featureLineClosed      = cmd.featureLineClosed;
  doc.featureLineElevPt      = cmd.featureLineElevPt;
  doc.featureLineInfo        = cmd.featureLineInfo;
  doc.featureLineAttrs       = cmd.featureLineAttrs;
  doc.cadAnnotations         = cmd.cadAnnotations;
  doc.cadAnnotationAttrs     = cmd.cadAnnotationAttrs;
  doc.cadFilledRegions       = cmd.cadFilledRegions;
  doc.cadFilledRegionAttrs   = cmd.cadFilledRegionAttrs;
  doc.cadMeshes              = cmd.cadMeshes;      // pointers, not payloads (REQ-063)
  doc.cadMeshAttrs           = cmd.cadMeshAttrs;
  doc.cadSurfaces            = cmd.cadSurfaces;
  doc.cadSurfaceAttrs        = cmd.cadSurfaceAttrs;
  doc.surveyPoints           = cmd.surveyPoints;
  doc.pointGroups            = cmd.pointGroups;
  doc.selectedSurveyPointIndices = cmd.selectedSurveyPointIndices;
  doc.drawingLayerTable      = cmd.drawingLayerTable;
  doc.textStyles             = cmd.textStyles;
  doc.surfaceStyles          = cmd.surfaceStyles;
  doc.activeTextStyleName    = cmd.activeTextStyleName;
  doc.pdfAttachments         = cmd.pdfAttachments;
  doc.selection              = cmd.selection;
  doc.hiddenEntityIds        = cmd.hiddenEntityIds;  // isolation is per-drawing (REQ-084 (d))
  doc.paperLayouts           = cmd.paperLayouts;
  doc.savedPageSetups        = cmd.savedPageSetups;
  doc.activeSpaceIndex       = cmd.activeSpaceIndex;
  doc.cadGpuRevision         = cmd.cadGpuRevision;
  doc.savedRevision          = cmd.activeDocSavedRevision;
  doc.filePath               = cmd.activeDocFilePath;
}

void RestoreDocumentFromSnapshot(AppCommandState& cmd, int idx) {
  if (idx < 0 || static_cast<size_t>(idx) >= cmd.documents.size()) return;
  const DrawingDocument& doc     = cmd.documents[static_cast<size_t>(idx)];
  cmd.viewportPanX               = doc.viewportPanX;
  cmd.viewportPanY               = doc.viewportPanY;
  cmd.viewportZoom               = doc.viewportZoom;
  cmd.viewportPanZ               = doc.viewportPanZ;
  cmd.viewportAzimuthDeg         = doc.viewportAzimuthDeg;
  cmd.viewportElevationDeg       = doc.viewportElevationDeg;
  cmd.viewAnimActive             = false;  // never resume another tab's animation
  cmd.worldDocumentOriginX       = doc.worldDocumentOriginX;
  cmd.worldDocumentOriginY       = doc.worldDocumentOriginY;
  cmd.nextEntityId               = doc.nextEntityId;  // per-drawing id counter (REQ-076)
  // Force a re-sweep for the incoming drawing. The guard compares against cadGpuRevision, which is
  // per-document and restored just below — two tabs can legitimately sit at the same revision, and
  // a match there would wrongly certify this drawing's entities as already swept.
  cmd.entityIdSweepRevision      = kEntityIdSweepNever;
  cmd.userLinesFlat              = doc.userLinesFlat;
  cmd.userLineAttrs              = doc.userLineAttrs;
  cmd.userCirclesCxCyZR           = doc.userCirclesCxCyZR;
  cmd.userCircleAttrs            = doc.userCircleAttrs;
  cmd.userArcs                   = doc.userArcs;
  cmd.userArcAttrs               = doc.userArcAttrs;
  cmd.userEllipses               = doc.userEllipses;
  cmd.userEllAttrs               = doc.userEllAttrs;
  cmd.userPolylineOffsets        = doc.userPolylineOffsets;
  cmd.userPolylineVerts          = doc.userPolylineVerts;
  cmd.userPolylineClosed         = doc.userPolylineClosed;
  cmd.userPolylineAttrs          = doc.userPolylineAttrs;
  cmd.featureLineOffsets         = doc.featureLineOffsets;   // REQ-087
  cmd.featureLineVerts           = doc.featureLineVerts;
  cmd.featureLineClosed          = doc.featureLineClosed;
  cmd.featureLineElevPt          = doc.featureLineElevPt;
  cmd.featureLineInfo            = doc.featureLineInfo;
  cmd.featureLineAttrs           = doc.featureLineAttrs;
  cmd.cadAnnotations             = doc.cadAnnotations;
  cmd.cadAnnotationAttrs         = doc.cadAnnotationAttrs;
  cmd.cadFilledRegions           = doc.cadFilledRegions;
  cmd.cadFilledRegionAttrs       = doc.cadFilledRegionAttrs;
  cmd.cadMeshes                  = doc.cadMeshes;
  cmd.cadMeshAttrs               = doc.cadMeshAttrs;
  cmd.cadSurfaces                = doc.cadSurfaces;
  cmd.cadSurfaceAttrs            = doc.cadSurfaceAttrs;
  cmd.surveyPoints               = doc.surveyPoints;
  cmd.pointGroups                = doc.pointGroups;
  cmd.selectedSurveyPointIndices = doc.selectedSurveyPointIndices;
  cmd.drawingLayerTable          = doc.drawingLayerTable;
  cmd.textStyles                 = doc.textStyles;
  cmd.surfaceStyles              = doc.surfaceStyles;
  cmd.activeTextStyleName        = doc.activeTextStyleName;
  cmd.pdfAttachments             = doc.pdfAttachments;
  cmd.selection                  = doc.selection;
  cmd.hiddenEntityIds            = doc.hiddenEntityIds;  // isolation is per-drawing (REQ-084 (d))
  cmd.paperLayouts               = doc.paperLayouts;
  cmd.savedPageSetups            = doc.savedPageSetups;
  cmd.activeSpaceIndex           = doc.activeSpaceIndex;
  cmd.lastPaperLayoutIndex       = doc.activeSpaceIndex >= 0 ? doc.activeSpaceIndex : 0;
  cmd.selectedViewports.clear();  // transient selection; indices don't carry across documents
  cmd.selectedViewportIndex = -1;
  cmd.selectedViewportLayout = -1;
  cmd.paperGripCorner = -2;
  cmd.paperMovePhase = 0;
  cmd.paperSelBoxActive = false;
  cmd.floatingViewportLayout = -1;  // floating model space is transient, not per-document
  cmd.floatingViewportIndex = -1;
  cmd.modelViewSaved = false;       // restored view belongs to the restored active space
  cmd.cadGpuRevision             = doc.cadGpuRevision;
  cmd.activeDocSavedRevision     = doc.savedRevision;
  cmd.activeDocFilePath          = doc.filePath;
  cmd.active = AppCommandState::Kind::None;  // cancel any in-progress command on switch
}

// ---------------------------------------------------------------------------
// Paper space (REQ-025)
// ---------------------------------------------------------------------------

int AddPaperLayout(AppCommandState& cmd) {
  // Pick a unique "Layout N" name.
  int n = static_cast<int>(cmd.paperLayouts.size()) + 1;
  auto nameTaken = [&](const std::string& s) {
    for (const PaperLayout& l : cmd.paperLayouts)
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
  cmd.paperLayouts.push_back(layout);
  const int idx = static_cast<int>(cmd.paperLayouts.size()) - 1;
  cmd.lastPaperLayoutIndex = idx;
  BumpCadGpuCache(cmd);
  return idx;
}

void DeletePaperLayout(AppCommandState& cmd, int idx) {
  if (idx < 0 || static_cast<size_t>(idx) >= cmd.paperLayouts.size())
    return;
  cmd.paperLayouts.erase(cmd.paperLayouts.begin() + idx);
  // Fix up the active space and the toggle target.
  if (cmd.activeSpaceIndex == idx)
    cmd.activeSpaceIndex = kModelSpaceIndex;          // deleted the active layout → fall back to model
  else if (cmd.activeSpaceIndex > idx)
    --cmd.activeSpaceIndex;                            // indices after the removed one shift down
  cmd.lastPaperLayoutIndex =
      std::clamp(cmd.lastPaperLayoutIndex, 0, std::max(0, static_cast<int>(cmd.paperLayouts.size()) - 1));
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
  const int prev = cmd.activeSpaceIndex;
  // Save the view of the space we're leaving so each space keeps its own pan/zoom.
  if (prev == kModelSpaceIndex) {
    cmd.modelViewPanX = cmd.viewportPanX;
    cmd.modelViewPanY = cmd.viewportPanY;
    cmd.modelViewZoom = cmd.viewportZoom;
    cmd.modelViewSaved = true;
  } else if (prev >= 0 && static_cast<size_t>(prev) < cmd.paperLayouts.size()) {
    PaperLayout& pl = cmd.paperLayouts[static_cast<size_t>(prev)];
    pl.viewPanX = cmd.viewportPanX;
    pl.viewPanY = cmd.viewportPanY;
    pl.viewZoom = cmd.viewportZoom;
    pl.viewInit = true;
  }

  int ns;
  if (spaceIndex < 0 || cmd.paperLayouts.empty()) {
    ns = kModelSpaceIndex;
  } else {
    ns = std::clamp(spaceIndex, 0, static_cast<int>(cmd.paperLayouts.size()) - 1);
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
    PaperLayout& pl = cmd.paperLayouts[static_cast<size_t>(ns)];
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
  cmd.active = AppCommandState::Kind::None;  // leaving a space cancels any in-progress command
  BumpCadGpuCache(cmd);
}

void ToggleModelPaperSpace(AppCommandState& cmd) {
  if (cmd.activeSpaceIndex == kModelSpaceIndex) {
    if (cmd.paperLayouts.empty())
      AddPaperLayout(cmd);  // create one on first switch so PAPER has somewhere to go
    SetActiveSpace(cmd, std::clamp(cmd.lastPaperLayoutIndex, 0, static_cast<int>(cmd.paperLayouts.size()) - 1));
  } else {
    cmd.lastPaperLayoutIndex = cmd.activeSpaceIndex;
    SetActiveSpace(cmd, kModelSpaceIndex);
  }
}

int AddViewport(AppCommandState& cmd, int layoutIdx) {
  if (layoutIdx < 0 || static_cast<size_t>(layoutIdx) >= cmd.paperLayouts.size())
    return -1;
  PaperLayout& L = cmd.paperLayouts[static_cast<size_t>(layoutIdx)];
  const float sw = L.sheetWidthIn();
  const float sh = L.sheetHeightIn();
  Viewport vp;
  vp.paperWIn = sw * 0.7f;
  vp.paperHIn = sh * 0.7f;
  vp.paperXIn = (sw - vp.paperWIn) * 0.5f;
  vp.paperYIn = (sh - vp.paperHIn) * 0.5f;
  // worldDocumentOrigin is ~the model centroid (the load/import rebase centers on it), so it makes a
  // sensible default viewport center; scale defaults to the drawing's plot scale.
  vp.modelCenterX = cmd.worldDocumentOriginX;
  vp.modelCenterY = cmd.worldDocumentOriginY;
  vp.scaleModelPerPaperIn = cmd.modelUnitsPerPlottedInch > 1.e-6f ? cmd.modelUnitsPerPlottedInch : 50.f;
  L.viewports.push_back(vp);
  const int idx = static_cast<int>(L.viewports.size()) - 1;
  cmd.selectedViewportLayout = layoutIdx;
  cmd.selectedViewportIndex = idx;
  cmd.selectedViewports = {idx};
  BumpCadGpuCache(cmd);
  return idx;
}

int AddViewportRect(AppCommandState& cmd, int layoutIdx, float x0In, float y0In, float x1In, float y1In) {
  if (layoutIdx < 0 || static_cast<size_t>(layoutIdx) >= cmd.paperLayouts.size())
    return -1;
  PaperLayout& L = cmd.paperLayouts[static_cast<size_t>(layoutIdx)];
  Viewport vp;
  vp.paperXIn = std::min(x0In, x1In);
  vp.paperYIn = std::min(y0In, y1In);
  vp.paperWIn = std::max(0.1f, std::fabs(x1In - x0In));
  vp.paperHIn = std::max(0.1f, std::fabs(y1In - y0In));
  vp.modelCenterX = cmd.worldDocumentOriginX;
  vp.modelCenterY = cmd.worldDocumentOriginY;
  vp.scaleModelPerPaperIn = cmd.modelUnitsPerPlottedInch > 1.e-6f ? cmd.modelUnitsPerPlottedInch : 50.f;
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
      static_cast<size_t>(cmd.activeSpaceIndex) >= cmd.paperLayouts.size()) {
    log.push_back("Rectangular viewport — switch to a paper layout first (MODEL/PAPER button).");
    return;
  }
  cmd.active = AppCommandState::Kind::PaperRectViewport;
  cmd.lastCommand = AppCommandState::Kind::PaperRectViewport;
  cmd.paperVpPhase = 0;
  log.push_back("Rectangular viewport — click the first corner on the sheet (Esc to cancel).");
}

void DeleteViewport(AppCommandState& cmd, int layoutIdx, int vpIdx) {
  if (layoutIdx < 0 || static_cast<size_t>(layoutIdx) >= cmd.paperLayouts.size())
    return;
  PaperLayout& L = cmd.paperLayouts[static_cast<size_t>(layoutIdx)];
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
  if (cmd.activeSpaceIndex < 0 || static_cast<size_t>(cmd.activeSpaceIndex) >= cmd.paperLayouts.size() ||
      cmd.selectedViewports.empty()) {
    log.push_back("DELETE — no viewport selected.");
    return;
  }
  PaperLayout& L = cmd.paperLayouts[static_cast<size_t>(cmd.activeSpaceIndex)];
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
  if (cmd.activeSpaceIndex < 0 || static_cast<size_t>(cmd.activeSpaceIndex) >= cmd.paperLayouts.size() ||
      cmd.selectedViewports.empty())
    return;
  PaperLayout& L = cmd.paperLayouts[static_cast<size_t>(cmd.activeSpaceIndex)];
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
    log.push_back(std::string(copy ? "COPY" : "MOVE") + " — select object(s) first.");
    return;
  }
  cmd.active = AppCommandState::Kind::None;  // paper-space edit ops are not a model command
  cmd.paperMovePhase = 1;
  cmd.paperMoveIsCopy = copy;
  log.push_back(std::string(copy ? "COPY" : "MOVE") + " — click the base point (Esc to cancel).");
}

// --- Floating model space (REQ-036) ---
// In-place: the active space stays the paper layout (so the sheet + all viewports stay visible), and
// model edit/snap/draw is routed through the active viewport's rectangle. The model is already drawn
// inside the viewport by the paper overlay, so entering does not change the view.

bool InFloatingModelSpace(const AppCommandState& cmd) { return cmd.floatingViewportIndex >= 0; }

PaperLayout* ActivePaperGeometryTarget(AppCommandState& st) {
  // REQ-037 / ADR-009: a draw/edit command writes to the active layout's paper store when a paper
  // layout is active and NOT in floating model space; otherwise it targets model space (→ nullptr).
  if (st.activeSpaceIndex < 0 || InFloatingModelSpace(st))
    return nullptr;
  if (static_cast<size_t>(st.activeSpaceIndex) >= st.paperLayouts.size())
    return nullptr;
  return &st.paperLayouts[static_cast<size_t>(st.activeSpaceIndex)];
}

// --- Native paper-space geometry: selection + edit (REQ-037, ADR-009) ---

using PaperRef = PaperEntityRef;

void ClearPaperEntitySelection(AppCommandState& st) { st.selectedPaperEntities.clear(); }

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

bool PickPaperEntityAt(const PaperLayout& L, float x, float y, float tolIn, PaperRef* out) {
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
      const float dx = x - e.cx, dy = y - e.cy;
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
    const int v1 = L.paperPolyOffsets[static_cast<size_t>(pi + 1)];
    for (int vi = v0; vi + 1 < v1; ++vi) {
      const size_t a = static_cast<size_t>(vi) * 3, b = static_cast<size_t>(vi + 1) * 3;
      if (PaperPointSegDist2(x, y, L.paperPolyVerts[a], L.paperPolyVerts[a + 1], L.paperPolyVerts[b],
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
  return false;
}

void TogglePaperEntitySelection(AppCommandState& st, PaperRef ref, bool additive) {
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
  if (pi < 0 || static_cast<size_t>(pi + 1) >= L.paperPolyOffsets.size())
    return;
  const int v0 = L.paperPolyOffsets[static_cast<size_t>(pi)];
  const int v1 = L.paperPolyOffsets[static_cast<size_t>(pi + 1)];
  const int nv = v1 - v0;
  L.paperPolyVerts.erase(L.paperPolyVerts.begin() + static_cast<std::ptrdiff_t>(v0) * 3,
                         L.paperPolyVerts.begin() + static_cast<std::ptrdiff_t>(v1) * 3);
  L.paperPolyOffsets.erase(L.paperPolyOffsets.begin() + (pi + 1));  // drop this poly's end marker
  for (size_t k = static_cast<size_t>(pi + 1); k < L.paperPolyOffsets.size(); ++k)
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
  std::vector<int> byType[6];
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
  ClearPaperEntitySelection(st);
  BumpCadGpuCache(st);
  log.push_back("DELETE — removed " + std::to_string(n) + " paper object(s).");
}

void TranslateSelectedPaperEntities(AppCommandState& st, float dxIn, float dyIn, bool copy,
                                    std::vector<std::string>& log) {
  PaperLayout* L = ActivePaperGeometryTarget(st);
  if (!L || st.selectedPaperEntities.empty())
    return;
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
      if (pi < 0 || static_cast<size_t>(pi + 1) >= L->paperPolyOffsets.size())
        break;
      const int v0 = L->paperPolyOffsets[static_cast<size_t>(pi)];
      const int v1 = L->paperPolyOffsets[static_cast<size_t>(pi + 1)];
      if (copy) {
        const int baseVert = L->paperPolyOffsets.empty() ? 0 : L->paperPolyOffsets.back();
        for (int vi = v0; vi < v1; ++vi) {
          L->paperPolyVerts.push_back(L->paperPolyVerts[static_cast<size_t>(vi * 3)] + dxIn);
          L->paperPolyVerts.push_back(L->paperPolyVerts[static_cast<size_t>(vi * 3 + 1)] + dyIn);
          L->paperPolyVerts.push_back(L->paperPolyVerts[static_cast<size_t>(vi * 3 + 2)]);
        }
        L->paperPolyOffsets.push_back(baseVert + (v1 - v0));
        L->paperPolyClosed.push_back(static_cast<size_t>(pi) < L->paperPolyClosed.size() ? L->paperPolyClosed[static_cast<size_t>(pi)] : 0u);
        L->paperPolyAttrs.push_back(attrAt(L->paperPolyAttrs, pi));
        newSel.push_back({PaperRef::Type::Polyline, static_cast<int>(L->paperPolyOffsets.size()) - 2});
      } else {
        for (int vi = v0; vi < v1; ++vi) {
          L->paperPolyVerts[static_cast<size_t>(vi * 3)] += dxIn;
          L->paperPolyVerts[static_cast<size_t>(vi * 3 + 1)] += dyIn;
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
    }
  }
  if (copy && !newSel.empty())
    st.selectedPaperEntities = newSel;
  BumpCadGpuCache(st);
  log.push_back(copy ? "COPY — duplicated paper object(s)." : "MOVE — moved paper object(s).");
}

void RotateSelectedPaperEntities(AppCommandState& st, float baseX, float baseY, float angRad,
                                 std::vector<std::string>& log) {
  PaperLayout* L = ActivePaperGeometryTarget(st);
  if (!L || st.selectedPaperEntities.empty())
    return;
  const float c = std::cos(angRad), s = std::sin(angRad);
  auto rot = [&](float& x, float& y) {
    const float dx = x - baseX, dy = y - baseY;
    x = baseX + dx * c - dy * s;
    y = baseY + dx * s + dy * c;
  };
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
      if (pi < 0 || static_cast<size_t>(pi + 1) >= L->paperPolyOffsets.size())
        break;
      const int v0 = L->paperPolyOffsets[static_cast<size_t>(pi)];
      const int v1 = L->paperPolyOffsets[static_cast<size_t>(pi + 1)];
      for (int vi = v0; vi < v1; ++vi)
        rot(L->paperPolyVerts[static_cast<size_t>(vi * 3)], L->paperPolyVerts[static_cast<size_t>(vi * 3 + 1)]);
      break;
    }
    case PaperRef::Type::Text: {
      if (r.index >= 0 && static_cast<size_t>(r.index) < L->paperTexts.size()) {
        rot(L->paperTexts[static_cast<size_t>(r.index)].insX, L->paperTexts[static_cast<size_t>(r.index)].insY);
        L->paperTexts[static_cast<size_t>(r.index)].rotationRad += angRad;
      }
      break;
    }
    }
  }
  BumpCadGpuCache(st);
  log.push_back("ROTATE — rotated paper object(s).");
}

bool TryBeginEntityGripAtLocal(AppCommandState& cmd, float lx, float ly, float tolWorld) {
  // REQ-036: grab the nearest grip of a selected entity within tolWorld of (lx,ly) in LOCAL coords. Mirrors
  // the model-space grab but with a world-distance hit test (the model path uses a screen-space test, which
  // does not work through the floating viewport transform). On a hit, arms the grip drag + stores originals.
  if (cmd.selection.empty())
    return false;
  float bestD2 = tolWorld * tolWorld;
  int bestWhich = -1;
  SelectedEntity bestSel{};
  float bestGripX = 0.f, bestGripY = 0.f;
  auto tryGrip = [&](const SelectedEntity& sel, float gx, float gy, int which) {
    const float dx = lx - gx, dy = ly - gy;
    const float d2 = dx * dx + dy * dy;
    if (d2 < bestD2) {
      bestD2 = d2;
      bestWhich = which;
      bestSel = sel;
      bestGripX = gx;  // ORTHO / typed-distance anchor for the drag (REQ-047)
      bestGripY = gy;
    }
  };
  for (const SelectedEntity& sel : cmd.selection) {
    switch (sel.type) {
    case SelectedEntity::Type::LineSeg: {
      const size_t k = static_cast<size_t>(sel.index) * 6;
      if (k + 5 < cmd.userLinesFlat.size()) {
        tryGrip(sel, cmd.userLinesFlat[k], cmd.userLinesFlat[k + 1], 0);
        tryGrip(sel, cmd.userLinesFlat[k + 3], cmd.userLinesFlat[k + 4], 1);
      }
      break;
    }
    case SelectedEntity::Type::Circle: {
      const size_t k = static_cast<size_t>(sel.index) * 4;
      if (k + 3 < cmd.userCirclesCxCyZR.size()) {
        const float cx = cmd.userCirclesCxCyZR[k], cy = cmd.userCirclesCxCyZR[k + 1], r = cmd.userCirclesCxCyZR[k + 3];
        tryGrip(sel, cx, cy, 0);
        tryGrip(sel, cx + r, cy, 1);
      }
      break;
    }
    case SelectedEntity::Type::Polyline: {
      const int np = cmd.userPolylineOffsets.size() > 0 ? static_cast<int>(cmd.userPolylineOffsets.size() - 1) : 0;
      if (sel.index >= 0 && sel.index < np) {
        const int startV = cmd.userPolylineOffsets[static_cast<size_t>(sel.index)];
        const int endV = cmd.userPolylineOffsets[static_cast<size_t>(sel.index + 1)];
        for (int vi = 0; vi < endV - startV; ++vi) {
          const size_t xIdx = static_cast<size_t>(startV + vi) * 3;
          if (xIdx + 1 >= cmd.userPolylineVerts.size())
            break;
          tryGrip(sel, cmd.userPolylineVerts[xIdx], cmd.userPolylineVerts[xIdx + 1], vi);
        }
      }
      break;
    }
    case SelectedEntity::Type::Arc: {
      if (sel.index >= 0 && static_cast<size_t>(sel.index) < cmd.userArcs.size()) {
        const CadArc& a = cmd.userArcs[static_cast<size_t>(sel.index)];
        const float endRad = a.startRad + a.sweepRad;
        tryGrip(sel, a.cx, a.cy, 0);
        tryGrip(sel, a.cx + a.r * std::cos(a.startRad), a.cy + a.r * std::sin(a.startRad), 1);
        tryGrip(sel, a.cx + a.r * std::cos(endRad), a.cy + a.r * std::sin(endRad), 2);
      }
      break;
    }
    case SelectedEntity::Type::Ellipse: {
      if (sel.index >= 0 && static_cast<size_t>(sel.index) < cmd.userEllipses.size()) {
        const CadEllipse& el = cmd.userEllipses[static_cast<size_t>(sel.index)];
        const float perpX = -el.majVy, perpY = el.majVx;
        tryGrip(sel, el.cx, el.cy, 0);
        tryGrip(sel, el.cx + el.majVx, el.cy + el.majVy, 1);
        tryGrip(sel, el.cx + perpX * el.ratio, el.cy + perpY * el.ratio, 2);
      }
      break;
    }
    default:
      break;
    }
  }
  if (bestWhich < 0)
    return false;
  PushUndoSnapshot(cmd, "Grip edit");
  cmd.entityGripMoveActive = true;
  cmd.entityGripType = bestSel.type;
  cmd.entityGripEntityIndex = bestSel.index;
  cmd.entityGripWhich = bestWhich;
  cmd.entityGripAnchorX = bestGripX;
  cmd.entityGripAnchorY = bestGripY;
  cmd.entityGripTypedDistanceValid = false;
  switch (bestSel.type) {  // store originals for RMB / Esc cancel
  case SelectedEntity::Type::LineSeg: {
    const size_t k = static_cast<size_t>(bestSel.index) * 6;
    cmd.entityGripOrigX0 = cmd.userLinesFlat[k];
    cmd.entityGripOrigY0 = cmd.userLinesFlat[k + 1];
    cmd.entityGripOrigX1 = cmd.userLinesFlat[k + 3];
    cmd.entityGripOrigY1 = cmd.userLinesFlat[k + 4];
    break;
  }
  case SelectedEntity::Type::Circle: {
    const size_t k = static_cast<size_t>(bestSel.index) * 4;
    cmd.entityGripOrigCx = cmd.userCirclesCxCyZR[k];
    cmd.entityGripOrigCy = cmd.userCirclesCxCyZR[k + 1];
    cmd.entityGripOrigR = cmd.userCirclesCxCyZR[k + 3];
    break;
  }
  case SelectedEntity::Type::Polyline: {
    const int startV = cmd.userPolylineOffsets[static_cast<size_t>(bestSel.index)];
    const size_t xIdx = static_cast<size_t>(startV + bestWhich) * 3;
    cmd.entityGripOrigPolylineXIdx = static_cast<int>(xIdx);
    cmd.entityGripOrigPolyVertX = cmd.userPolylineVerts[xIdx];
    cmd.entityGripOrigPolyVertY = cmd.userPolylineVerts[xIdx + 1];
    break;
  }
  case SelectedEntity::Type::Arc: {
    const CadArc& a = cmd.userArcs[static_cast<size_t>(bestSel.index)];
    cmd.entityGripOrigCx = a.cx;
    cmd.entityGripOrigCy = a.cy;
    cmd.entityGripOrigR = a.r;
    cmd.entityGripOrigStartRad = a.startRad;
    cmd.entityGripOrigSweepRad = a.sweepRad;
    break;
  }
  case SelectedEntity::Type::Ellipse: {
    const CadEllipse& el = cmd.userEllipses[static_cast<size_t>(bestSel.index)];
    cmd.entityGripOrigEllCx = el.cx;
    cmd.entityGripOrigEllCy = el.cy;
    cmd.entityGripOrigEllMajVx = el.majVx;
    cmd.entityGripOrigEllMajVy = el.majVy;
    cmd.entityGripOrigEllRatio = el.ratio;
    break;
  }
  default:
    break;
  }
  return true;
}

void EnterFloatingModelSpace(AppCommandState& cmd, int layoutIdx, int vpIdx, std::vector<std::string>& log) {
  if (layoutIdx < 0 || static_cast<size_t>(layoutIdx) >= cmd.paperLayouts.size())
    return;
  PaperLayout& L = cmd.paperLayouts[static_cast<size_t>(layoutIdx)];
  if (vpIdx < 0 || static_cast<size_t>(vpIdx) >= L.viewports.size())
    return;
  cmd.floatingViewportLayout = layoutIdx;
  cmd.floatingViewportIndex = vpIdx;
  cmd.active = AppCommandState::Kind::None;
  cmd.paperMovePhase = 0;
  cmd.paperGripCorner = -2;
  cmd.paperSelBoxActive = false;
  log.push_back("Floating model space — editing viewport " + std::to_string(vpIdx + 1) +
                " in place; Esc / FLOAT button / PSPACE returns to paper editing.");
  BumpCadGpuCache(cmd);
}

void ExitFloatingModelSpace(AppCommandState& cmd, std::vector<std::string>& log) {
  if (cmd.floatingViewportIndex < 0)
    return;
  cmd.floatingViewportLayout = -1;
  cmd.floatingViewportIndex = -1;
  cmd.active = AppCommandState::Kind::None;
  log.push_back("Returned to paper space.");
  BumpCadGpuCache(cmd);
}

// --- Page setups (named, drawing-wide) ---

void EnsureStandardPageSetup(AppCommandState& cmd) {
  for (const PageSetup& p : cmd.savedPageSetups)
    if (p.name == "Standard")
      return;
  PageSetup std;
  std.name = "Standard";  // defaults: ARCH D, landscape, 1:1
  cmd.savedPageSetups.insert(cmd.savedPageSetups.begin(), std);
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
  return ps;
}

void MoveOrCopyLayout(AppCommandState& cmd, int layoutIdx, int beforeIdx, bool makeCopy,
                      std::vector<std::string>& log) {
  const int n = static_cast<int>(cmd.paperLayouts.size());
  if (layoutIdx < 0 || layoutIdx >= n)
    return;
  PaperLayout item = cmd.paperLayouts[static_cast<size_t>(layoutIdx)];
  if (makeCopy) {
    // Unique "Name (2)" style name.
    std::string base = item.name;
    int k = 2;
    auto taken = [&](const std::string& s) {
      for (const PaperLayout& l : cmd.paperLayouts)
        if (l.name == s)
          return true;
      return false;
    };
    std::string nm;
    do {
      nm = base + " (" + std::to_string(k++) + ")";
    } while (taken(nm));
    item.name = nm;
    int insertAt = std::clamp(beforeIdx, 0, n);
    cmd.paperLayouts.insert(cmd.paperLayouts.begin() + insertAt, item);
    SetActiveSpace(cmd, insertAt);
    log.push_back("Layout copied as \"" + item.name + "\".");
  } else {
    cmd.paperLayouts.erase(cmd.paperLayouts.begin() + layoutIdx);
    int insertAt = beforeIdx;
    if (insertAt > layoutIdx)
      --insertAt;  // account for the removed slot
    insertAt = std::clamp(insertAt, 0, static_cast<int>(cmd.paperLayouts.size()));
    cmd.paperLayouts.insert(cmd.paperLayouts.begin() + insertAt, item);
    SetActiveSpace(cmd, insertAt);
    log.push_back("Layout \"" + item.name + "\" moved.");
  }
  BumpCadGpuCache(cmd);
}

// ---------------------------------------------------------------------------
// Undo / Redo
// ---------------------------------------------------------------------------

namespace {

static void WriteUndoHistoryLogLine(const std::string& msg) {
  const std::filesystem::path dir = UserDataDirectory();
  if (dir.empty())
    return;
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  const std::filesystem::path logPath = dir / "history.log";
  std::ofstream f(logPath, std::ios::app);
  if (!f)
    return;
  std::time_t t = std::time(nullptr);
  char timeBuf[32];
  struct tm tmInfo{};
#ifdef _WIN32
  localtime_s(&tmInfo, &t);
#else
  localtime_r(&t, &tmInfo);
#endif
  std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", &tmInfo);
  f << timeBuf << "  " << msg << "\n";
}

static DrawingGeometrySnapshot CaptureGeometrySnapshot(const AppCommandState& st, const std::string& description) {
  DrawingGeometrySnapshot snap;
  // Meshes copy as pointers, not payloads (architecture §11.5 as amended 2026-08-12) — this line is
  // O(number of meshes), not O(triangles), which is what makes undo affordable with a model loaded.
  snap.cadMeshes            = st.cadMeshes;
  snap.cadMeshAttrs         = st.cadMeshAttrs;
  // Surfaces copy as strings + a refcount bump, never as triangles (REQ-068, architecture §11.5).
  snap.cadSurfaces          = st.cadSurfaces;
  snap.cadSurfaceAttrs      = st.cadSurfaceAttrs;
  snap.userLinesFlat        = st.userLinesFlat;
  snap.userLineAttrs        = st.userLineAttrs;
  snap.userCirclesCxCyZR     = st.userCirclesCxCyZR;
  snap.userCircleAttrs      = st.userCircleAttrs;
  snap.userArcs             = st.userArcs;
  snap.userArcAttrs         = st.userArcAttrs;
  snap.userEllipses         = st.userEllipses;
  snap.userEllAttrs         = st.userEllAttrs;
  snap.userPolylineOffsets  = st.userPolylineOffsets;
  snap.userPolylineVerts    = st.userPolylineVerts;
  snap.userPolylineClosed   = st.userPolylineClosed;
  snap.userPolylineAttrs    = st.userPolylineAttrs;
  snap.featureLineOffsets   = st.featureLineOffsets;   // REQ-087
  snap.featureLineVerts     = st.featureLineVerts;
  snap.featureLineClosed    = st.featureLineClosed;
  snap.featureLineElevPt    = st.featureLineElevPt;
  snap.featureLineInfo      = st.featureLineInfo;
  snap.featureLineAttrs     = st.featureLineAttrs;
  snap.cadAnnotations       = st.cadAnnotations;
  snap.cadAnnotationAttrs   = st.cadAnnotationAttrs;
  snap.cadFilledRegions     = st.cadFilledRegions;
  snap.cadFilledRegionAttrs = st.cadFilledRegionAttrs;
  snap.surveyPoints         = st.surveyPoints;
  snap.pointGroups          = st.pointGroups;   // group edits are undoable (REQ-067)
  snap.drawingLayerTable    = st.drawingLayerTable;
  snap.textStyles           = st.textStyles;  // style edits are undoable (REQ-044)
  snap.surfaceStyles        = st.surfaceStyles;  // and surface-style edits (REQ-070)
  snap.pdfAttachments       = st.pdfAttachments;
  snap.paperLayouts         = st.paperLayouts;  // native paper geometry is undoable (REQ-037/038)
  // Zero GL texture IDs: restored snapshots must not reference freed GPU resources.
  for (auto& att : snap.pdfAttachments)
    att.glTexId = 0;
  snap.worldDocumentOriginX = st.worldDocumentOriginX;
  snap.worldDocumentOriginY = st.worldDocumentOriginY;
  snap.description          = description;
  return snap;
}

static void RestoreGeometrySnapshot(AppCommandState& st, const DrawingGeometrySnapshot& snap) {
  st.userLinesFlat        = snap.userLinesFlat;
  st.userLineAttrs        = snap.userLineAttrs;
  st.userCirclesCxCyZR     = snap.userCirclesCxCyZR;
  st.userCircleAttrs      = snap.userCircleAttrs;
  st.userArcs             = snap.userArcs;
  st.userArcAttrs         = snap.userArcAttrs;
  st.userEllipses         = snap.userEllipses;
  st.userEllAttrs         = snap.userEllAttrs;
  st.userPolylineOffsets  = snap.userPolylineOffsets;
  st.userPolylineVerts    = snap.userPolylineVerts;
  st.userPolylineClosed   = snap.userPolylineClosed;
  st.userPolylineAttrs    = snap.userPolylineAttrs;
  st.featureLineOffsets   = snap.featureLineOffsets;   // REQ-087
  st.featureLineVerts     = snap.featureLineVerts;
  st.featureLineClosed    = snap.featureLineClosed;
  st.featureLineElevPt    = snap.featureLineElevPt;
  st.featureLineInfo      = snap.featureLineInfo;
  st.featureLineAttrs     = snap.featureLineAttrs;
  st.cadAnnotations       = snap.cadAnnotations;
  st.cadAnnotationAttrs   = snap.cadAnnotationAttrs;
  st.cadFilledRegions     = snap.cadFilledRegions;
  st.cadFilledRegionAttrs = snap.cadFilledRegionAttrs;
  st.cadMeshes            = snap.cadMeshes;
  st.cadMeshAttrs         = snap.cadMeshAttrs;
  st.cadSurfaces          = snap.cadSurfaces;
  st.cadSurfaceAttrs      = snap.cadSurfaceAttrs;
  st.surveyPoints         = snap.surveyPoints;
  st.pointGroups          = snap.pointGroups;
  st.drawingLayerTable    = snap.drawingLayerTable;
  st.textStyles           = snap.textStyles;
  st.surfaceStyles        = snap.surfaceStyles;
  st.pdfAttachments       = snap.pdfAttachments;
  st.paperLayouts         = snap.paperLayouts;
  st.selectedPaperEntities.clear();  // restored layouts invalidate paper-entity indices
  st.worldDocumentOriginX = snap.worldDocumentOriginX;
  st.worldDocumentOriginY = snap.worldDocumentOriginY;
}

} // namespace

namespace {

/// The fixed order the id sweep walks, and the order \ref FindEntityById searches.
///
/// **Order is load-bearing**: it is what makes assignment deterministic, and therefore what makes a
/// legacy `.gs` load with the same ids every time (REQ-076). Appending a new entity kind is safe;
/// reordering the existing entries would renumber every legacy drawing on its next load.
/// The order ids are handed out in. **Append only** — see \ref EntityKind. Reordering, or inserting
/// anywhere but the end, renumbers every entity in every existing drawing on its next load, and
/// REQ-069's breakline and boundary references are stored by exactly those ids.
const EntityKind kEntityKindsInSweepOrder[] = {
    EntityKind::Line,       EntityKind::Circle,       EntityKind::Arc,  EntityKind::Ellipse,
    EntityKind::Polyline,   EntityKind::Annotation,   EntityKind::FilledRegion, EntityKind::Mesh,
    EntityKind::FeatureLine,
    EntityKind::Surface};  ///< REQ-068 / ADR-036 (a) — last, so the nine above keep their ids.

/// The attribute array for a kind. One accessor for both the const and mutable walks, so the
/// two can never disagree about which arrays are covered.
template <typename StateT>
auto* AttrsForKind(StateT& st, EntityKind k) {
  switch (k) {
  case EntityKind::Line:         return &st.userLineAttrs;
  case EntityKind::Circle:       return &st.userCircleAttrs;
  case EntityKind::Arc:          return &st.userArcAttrs;
  case EntityKind::Ellipse:      return &st.userEllAttrs;
  case EntityKind::Polyline:     return &st.userPolylineAttrs;
  case EntityKind::Annotation:   return &st.cadAnnotationAttrs;
  case EntityKind::FilledRegion: return &st.cadFilledRegionAttrs;
  case EntityKind::Mesh:         return &st.cadMeshAttrs;
  case EntityKind::FeatureLine:  return &st.featureLineAttrs;
  case EntityKind::Surface:      return &st.cadSurfaceAttrs;  // REQ-068 / ADR-036 (a)
  }
  return &st.userLineAttrs;
}

} // namespace

bool SurfaceVisible(const AppCommandState& st, size_t surfaceIndex) {
  if (surfaceIndex >= st.cadSurfaces.size())
    return false;
  const CadSurface& s = st.cadSurfaces[surfaceIndex];
  if (!s.tin || s.tin->indices.empty())
    return false;
  if (surfaceIndex >= st.cadSurfaceAttrs.size())
    return true;  // attrs are length-locked to cadSurfaces; a short array means "defaults", not hidden
  const EntityAttributes& a = st.cadSurfaceAttrs[surfaceIndex];
  // REQ-084 (d): an isolated-out surface is invisible, so it must not be drawn OR answer a click.
  if (CadEntityIdHidden(&st.hiddenEntityIds, a.id))
    return false;
  // REQ-068: a surface on a frozen or off layer is not drawn.
  const auto it = std::find_if(st.drawingLayerTable.begin(), st.drawingLayerTable.end(),
                               [&](const CadLayerRow& r) { return r.name == a.layer; });
  return !(it != st.drawingLayerTable.end() && (!it->on || it->frozen));
}

namespace {

/// Ceiling on how many contour levels one surface may generate at (REQ-070).
///
/// The generator's cost is proportional to total contour LENGTH, not to the level count — its inner
/// loop only visits levels lying inside each triangle's own Z range — so a small interval on a flat
/// surface is cheap and cannot run away on its own. This cap is not for that; it is for the value
/// nobody typed. The style editor refuses an out-of-range interval where the user enters it, but a
/// hand-edited `.gs` or a future importer can still present 0.0001 ft, and the display path must
/// degrade instead of locking the UI up. 20,000 is far past any plan sheet — a 1,000 ft relief at a
/// 0.05 ft interval — and well short of a hang.
constexpr size_t kMaxContourLevels = 20000;

/// Flatten \p r's chained polylines into the `GL_LINES` layout the renderer consumes: six floats per
/// segment, both endpoints written out.
///
/// The doubling is deliberate. `GL_LINE_STRIP` would need one draw call per contour — thousands of
/// them — where this needs one for all of them, and the existing surface/marker/snap paths are all
/// already `GL_LINES`, so this keeps one layout in the renderer instead of two.
void AppendContourLinesFrom(const ContourResult& r, std::vector<float>* out) {
  for (int c = 0; c < r.contourCount(); ++c) {
    const int begin = r.offsets[static_cast<size_t>(c)];
    const int end = r.offsets[static_cast<size_t>(c) + 1];
    const auto emit = [&](int v) {
      out->push_back(r.vertsXyz[static_cast<size_t>(v) * 3 + 0]);
      out->push_back(r.vertsXyz[static_cast<size_t>(v) * 3 + 1]);
      out->push_back(r.vertsXyz[static_cast<size_t>(v) * 3 + 2]);
    };
    for (int v = begin; v + 1 < end; ++v) {
      emit(v);
      emit(v + 1);
    }
    // A closed contour's last vertex is not a repeat of its first (contourgen does not emit the seam
    // twice), so the closing segment has to be written here or every ring would render with a gap.
    if (end - begin > 2 && r.closed[static_cast<size_t>(c)]) {
      emit(end - 1);
      emit(begin);
    }
  }
}

/// Every triangulation edge of \p t in `GL_LINES` layout — the appearance a surface had before
/// REQ-070, now the style's "triangles" component.
void AppendTriangleEdges(const CadTin& t, std::vector<float>* out) {
  const auto emit = [&](std::uint32_t a, std::uint32_t b) {
    out->push_back(t.vertsXyz[a * 3 + 0]);
    out->push_back(t.vertsXyz[a * 3 + 1]);
    out->push_back(t.vertsXyz[a * 3 + 2]);
    out->push_back(t.vertsXyz[b * 3 + 0]);
    out->push_back(t.vertsXyz[b * 3 + 1]);
    out->push_back(t.vertsXyz[b * 3 + 2]);
  };
  // Each interior edge is emitted twice, once per adjoining triangle. De-duplicating would cost a
  // hash of every edge to halve a buffer the line pipeline already handles at this size (REQ-100's
  // measured envelope is 750k segments); that trade is worth revisiting only if the surface profile
  // misses its budget.
  for (size_t i = 0; i + 2 < t.indices.size(); i += 3) {
    emit(t.indices[i], t.indices[i + 1]);
    emit(t.indices[i + 1], t.indices[i + 2]);
    emit(t.indices[i + 2], t.indices[i]);
  }
}

/// How many levels \c ContourLevels would produce, without producing them.
///
/// The cap below has to be checked BEFORE the lists are built, not after: a 0.0001 ft interval over a
/// 33 ft surface is 330,000 doubles materialised and immediately discarded, which took 2.4 s in
/// testing — a visible stall to reach a decision that is pure arithmetic.
double ContourLevelCount(double minZ, double maxZ, double interval) {
  if (!std::isfinite(minZ) || !std::isfinite(maxZ) || !std::isfinite(interval) || interval <= 0.0 ||
      maxZ < minZ)
    return 0.0;
  const double first = std::ceil(minZ / interval);
  const double last = std::floor(maxZ / interval);
  return last < first ? 0.0 : last - first + 1.0;
}

/// What a surface's style says its contours are: the levels, split by component, or the reason there
/// are none.
///
/// **One decision, two callers** — the per-frame display pass and REQ-071's EXTRACT. They MUST agree:
/// REQ-071's first acceptance condition is that extraction produces polylines "at exactly the
/// displayed contour elevations", and two functions that agree today drift tomorrow. Sharing the
/// decision makes that condition true by construction rather than by inspection.
struct SurfaceContourLevels {
  std::vector<double> minor;  ///< Minor levels, with the majors already removed.
  std::vector<double> major;
  bool suppressed = false;    ///< The style asked for more levels than \ref kMaxContourLevels.
  int levelsAsked = 0;        ///< How many, when suppressed. 0 otherwise.
  double minZ = 0.0, maxZ = 0.0;
  bool haveRange = false;     ///< False for a triangulation with no vertices.

  [[nodiscard]] bool empty() const { return minor.empty() && major.empty(); }
};

/// Split a surface's contour levels into major and minor, with the majors removed from the minors so
/// no level is drawn twice.
///
/// A major level is a minor level by construction — the interval rule makes the major interval a
/// whole multiple of the minor — so without the removal every major contour would be drawn once in
/// each colour, and which one a user saw would depend on draw order rather than on the style.
void SplitContourLevels(double minZ, double maxZ, const SurfaceStyle& style,
                        std::vector<double>* minorOut, std::vector<double>* majorOut) {
  minorOut->clear();
  majorOut->clear();
  if (style.majorContour.visible)
    ContourLevels(minZ, maxZ, style.majorIntervalFt, majorOut);
  if (!style.minorContour.visible)
    return;

  std::vector<double> allMinor;
  ContourLevels(minZ, maxZ, style.minorIntervalFt, &allMinor);
  if (majorOut->empty()) {
    *minorOut = std::move(allMinor);
    return;
  }
  // Matched with a tolerance, never `==`: both lists are `step * interval` products, and a level a
  // plan reader calls "110" is not the same double when reached as 55x2 and as 11x10.
  const double tol = std::max(1.0e-9, style.minorIntervalFt * 1.0e-9);
  for (double lv : allMinor) {
    const auto near = std::lower_bound(majorOut->begin(), majorOut->end(), lv - tol);
    if (near != majorOut->end() && std::fabs(*near - lv) <= tol)
      continue;
    minorOut->push_back(lv);
  }
}

/// Everything both contour consumers need to decide from \p tin and \p style: the surface's Z range,
/// the cap verdict, and the two level lists.
///
/// The order matters and is the whole point of sharing it. The Z range comes from the triangulation,
/// the cap is checked ARITHMETICALLY before any list is built (a 0.0001 ft interval is ~332,000
/// doubles materialised only to be discarded), and only then are the levels generated and the majors
/// removed from the minors. A second implementation of that sequence would be a second chance to get
/// the order — or the tolerance in the removal — subtly different.
SurfaceContourLevels ResolveSurfaceContourLevels(const CadTin& tin, const SurfaceStyle& style) {
  SurfaceContourLevels out;
  for (size_t v = 2; v < tin.vertsXyz.size(); v += 3) {
    const double z = static_cast<double>(tin.vertsXyz[v]);
    if (!out.haveRange) {
      out.minZ = out.maxZ = z;
      out.haveRange = true;
    } else {
      out.minZ = std::min(out.minZ, z);
      out.maxZ = std::max(out.maxZ, z);
    }
  }
  if (!out.haveRange)
    return out;  // no vertices: no range, no contours, and not an error
  if (!style.minorContour.visible && !style.majorContour.visible)
    return out;

  // The cap is on the PAIR, not on each list: two intervals that are individually sane can still be
  // asked for together, and it is the total that gets paid for.
  const double wanted =
      (style.minorContour.visible ? ContourLevelCount(out.minZ, out.maxZ, style.minorIntervalFt) : 0.0) +
      (style.majorContour.visible ? ContourLevelCount(out.minZ, out.maxZ, style.majorIntervalFt) : 0.0);
  if (wanted > static_cast<double>(kMaxContourLevels)) {
    out.suppressed = true;
    out.levelsAsked = wanted > 2.0e9 ? 2000000000 : static_cast<int>(wanted);  // saturate, never overflow
    return out;
  }

  SplitContourLevels(out.minZ, out.maxZ, style, &out.minor, &out.major);
  return out;
}

/// The resolved RGBA and lineweight for one component of a surface, folding the ByLayer chain the
/// same way an entity's own attributes do.
///
/// A component's "ByLayer" means the surface's own colour, which in turn may itself be ByLayer and
/// resolve to the layer's. Doing it through the shared resolvers rather than by hand is what keeps a
/// contour's ByLayer and a line's ByLayer meaning the same thing.
SurfaceDisplayBatch ResolveComponentBatch(const AppCommandState& st, const EntityAttributes& surfAttr,
                                          const SurfaceComponentStyle& comp,
                                          const std::vector<float>* verts) {
  SurfaceDisplayBatch b;
  b.verts = verts;

  const CadLayerRow* layer = FindDrawingLayerRowCi(st, surfAttr.layer);
  // The surface's own effective colour first — that is what a component's ByLayer defers to.
  float surfaceRgba[4] = {1.f, 1.f, 1.f, 1.f};
  ResolveEntityRgbaForViewport(surfAttr, layer, 0.42f, 0.62f, 0.78f, surfaceRgba);
  ResolveStoredColorForViewport(comp.color, surfAttr.transparency < 0.f ? 0.f : surfAttr.transparency,
                                surfaceRgba[0], surfaceRgba[1], surfaceRgba[2], b.rgba);
  b.rgba[3] = surfaceRgba[3];

  b.lineweightMm = comp.lineweightMm >= 0.f ? comp.lineweightMm
                                            : EffectiveEntityLineweightMm(surfAttr, layer);
  return b;
}

} // namespace

void RefreshSurfaceDisplayGeometry(AppCommandState& st) {
  // Reap first: an entry whose surface id no longer resolves belongs to an erased surface. Ids are
  // never reused (REQ-076), so "does not resolve" cannot mean "not yet created."
  st.surfaceDisplayCache.erase(
      std::remove_if(st.surfaceDisplayCache.begin(), st.surfaceDisplayCache.end(),
                     [&](const AppCommandState::SurfaceDisplayCacheEntry& e) {
                       return FindSurfaceIndexById(st, e.surfaceId) < 0;
                     }),
      st.surfaceDisplayCache.end());

  for (size_t si = 0; si < st.cadSurfaces.size(); ++si) {
    const std::uint64_t id = si < st.cadSurfaceAttrs.size() ? st.cadSurfaceAttrs[si].id : 0;
    if (id == 0)
      continue;  // not swept yet; it gets an entry next frame rather than one under an unusable key
    const CadSurface& surf = st.cadSurfaces[si];
    const std::shared_ptr<const CadTin>& tin = surf.tin;

    // Resolve-on-read (ADR-036 (d)): nothing is baked onto the surface, so a style edit touches the
    // table and nothing else — which is why it cannot reach the definition and cannot retriangulate.
    const SurfaceStyle* style = SurfaceStyles::Resolve(st.surfaceStyles, surf.styleName);
    const SurfaceStyle resolved = style ? *style : SurfaceStyles::StandardSurfaceStyle();

    auto it = std::find_if(st.surfaceDisplayCache.begin(), st.surfaceDisplayCache.end(),
                           [&](const AppCommandState::SurfaceDisplayCacheEntry& e) { return e.surfaceId == id; });
    if (it != st.surfaceDisplayCache.end() && it->builtFrom.lock() == tin && it->style == resolved)
      continue;  // BEFORE any clear/reserve — see the header comment; this early-out is the budget

    if (!tin) {
      if (it != st.surfaceDisplayCache.end())
        st.surfaceDisplayCache.erase(it);
      continue;  // never built: no geometry, and no empty entry left behind to keep re-checking
    }
    if (it == st.surfaceDisplayCache.end()) {
      st.surfaceDisplayCache.push_back({});
      it = st.surfaceDisplayCache.end() - 1;
      it->surfaceId = id;
    }
    it->builtFrom = tin;
    it->style = resolved;
    ++st.surfaceDisplayRegenCount;  // past the early-out: this frame is doing the real work

    // Each component is generated only when its own toggle is on, and its buffer is released when it
    // is off — REQ-070's "a style with triangles off and contours on draws only contours" is then a
    // property of what exists, not of what the renderer remembers to skip. `shrink_to_fit` because a
    // 14 MB triangle-edge buffer left capacity-resident by a `clear()` would defeat turning it off.
    const auto release = [](std::vector<float>* v) {
      v->clear();
      v->shrink_to_fit();
    };

    if (resolved.triangles.visible) {
      // Cleared, not appended to. This entry may already hold the edges generated for the PREVIOUS
      // style — a style edit is a cache miss on a triangulation that did not change — and appending
      // to it would double the buffer on every edit. Capacity is kept, because what follows refills
      // it to the same size.
      it->triangleEdges.clear();
      AppendTriangleEdges(*tin, &it->triangleEdges);
    } else {
      release(&it->triangleEdges);
    }

    if (resolved.border.visible)
      TinBorderEdges(tin->vertsXyz, tin->indices, &it->borderEdges);
    else
      release(&it->borderEdges);

    release(&it->minorContours);
    release(&it->majorContours);
    it->contoursSuppressed = false;
    it->suppressedLevelCount = 0;
    if (resolved.minorContour.visible || resolved.majorContour.visible) {
      // The SAME decision REQ-071's EXTRACT makes, from the same function — which is what makes
      // "extraction produces polylines at exactly the displayed contour elevations" structural.
      const SurfaceContourLevels levels = ResolveSurfaceContourLevels(*tin, resolved);
      if (levels.suppressed) {
        // Recorded, never absorbed (REQ-201). Nothing here can log — this runs once a frame with no
        // command in flight — so the fact is carried on the entry and the Surface Manager reports
        // it. Silently drawing no contours would look like a defect in the generator.
        it->contoursSuppressed = true;
        it->suppressedLevelCount = levels.levelsAsked;
      } else {
        ContourResult r;
        if (!levels.minor.empty()) {
          GenerateContours(tin->vertsXyz, tin->indices, levels.minor, &r);
          AppendContourLinesFrom(r, &it->minorContours);
        }
        if (!levels.major.empty()) {
          GenerateContours(tin->vertsXyz, tin->indices, levels.major, &r);
          AppendContourLinesFrom(r, &it->majorContours);
        }
      }
    }
  }

  // Assemble what the renderer is handed. Cheap by construction: the batches BORROW the buffers
  // above (see SurfaceDisplayBatch), so this pass copies pointers and colours, never vertices, and
  // is therefore safe to redo every frame — which it must be, because layer visibility and isolation
  // can change without any surface's geometry changing at all.
  st.surfaceDisplayGeometry.lines.clear();
  for (size_t si = 0; si < st.cadSurfaces.size(); ++si) {
    if (!SurfaceVisible(st, si))
      continue;  // layer off/frozen or isolated out — filtered here, so the renderer stays ignorant
    const std::uint64_t id = si < st.cadSurfaceAttrs.size() ? st.cadSurfaceAttrs[si].id : 0;
    if (id == 0)
      continue;
    const auto it = std::find_if(st.surfaceDisplayCache.begin(), st.surfaceDisplayCache.end(),
                                 [&](const AppCommandState::SurfaceDisplayCacheEntry& e) { return e.surfaceId == id; });
    if (it == st.surfaceDisplayCache.end())
      continue;
    const EntityAttributes& attr = st.cadSurfaceAttrs[si];

    // Draw order: triangles, then contours, then the border last so an outline stays readable over
    // its own triangulation.
    const auto add = [&](const SurfaceComponentStyle& comp, const std::vector<float>* verts) {
      if (verts->empty())
        return;
      st.surfaceDisplayGeometry.lines.push_back(ResolveComponentBatch(st, attr, comp, verts));
    };
    add(it->style.triangles, &it->triangleEdges);
    add(it->style.minorContour, &it->minorContours);
    add(it->style.majorContour, &it->majorContours);
    add(it->style.border, &it->borderEdges);
  }
}

bool SurfaceContoursSuppressed(const AppCommandState& st, size_t surfaceIndex, int* levelsAsked) {
  if (levelsAsked)
    *levelsAsked = 0;
  if (surfaceIndex >= st.cadSurfaceAttrs.size())
    return false;
  const std::uint64_t id = st.cadSurfaceAttrs[surfaceIndex].id;
  if (id == 0)
    return false;
  for (const auto& e : st.surfaceDisplayCache) {
    if (e.surfaceId != id)
      continue;
    if (levelsAsked)
      *levelsAsked = e.suppressedLevelCount;
    return e.contoursSuppressed;
  }
  return false;
}

int SurfaceDisplayContourSegs(const AppCommandState& st) {
  size_t floats = 0;
  for (const auto& e : st.surfaceDisplayCache)
    floats += e.minorContours.size() + e.majorContours.size();
  return static_cast<int>(floats / 6);
}

const std::vector<float>* SurfaceBorderEdges(const AppCommandState& st, size_t surfaceIndex) {
  if (surfaceIndex >= st.cadSurfaceAttrs.size())
    return nullptr;
  const std::uint64_t id = st.cadSurfaceAttrs[surfaceIndex].id;
  if (id == 0)
    return nullptr;
  for (const auto& e : st.surfaceDisplayCache)
    if (e.surfaceId == id)
      return e.borderEdges.empty() ? nullptr : &e.borderEdges;
  return nullptr;
}

int FindSurfaceIndex(const AppCommandState& st, const std::string& name) {
  auto eqCI = [](const std::string& a, const std::string& b) {
    if (a.size() != b.size())
      return false;
    for (size_t i = 0; i < a.size(); ++i)
      if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
        return false;
    return true;
  };
  for (size_t i = 0; i < st.cadSurfaces.size(); ++i)
    if (eqCI(st.cadSurfaces[i].name, name))
      return static_cast<int>(i);
  return -1;
}

int FindSurfaceIndexById(const AppCommandState& st, std::uint64_t id) {
  if (id == 0)
    return -1;  // 0 is "unassigned", never a real id — it must not match the first id-less surface
  for (size_t i = 0; i < st.cadSurfaceAttrs.size() && i < st.cadSurfaces.size(); ++i)
    if (st.cadSurfaceAttrs[i].id == id)
      return static_cast<int>(i);
  return -1;
}

namespace {

/// A breakline/boundary source's vertex chain, in WORLD double coordinates (matching the convention
/// \ref BuildSurfaceFromSources already uses for point-group points — ADR-028 (d)). Z is carried
/// through as-is: it is stored absolute, not offset by the document origin (ADR-025 D2), the same
/// rule \ref CadTin and \ref CadFilledRegion already follow.
struct ResolvedChain {
  std::vector<std::array<double, 3>> verts;
  bool closed = false;
};

/// Resolves a breakline/boundary reference (REQ-069) to its vertex chain. A Line or a Polyline are
/// both valid breakline sources; only a Polyline can be closed, so only a Polyline can ever satisfy
/// \p requireClosed. Returns false (chain left empty) when the id does not resolve to a usable
/// entity — the caller's job is to then drop that id from the definition, not to invent a chain.
bool ResolveDefinitionChain(const AppCommandState& st, std::uint64_t id, bool requireClosed,
                           ResolvedChain& out) {
  out.verts.clear();
  out.closed = false;
  const EntityRef ref = FindEntityById(st, id);
  if (!ref.valid())
    return false;

  if (ref.kind == EntityKind::Line) {
    if (requireClosed)
      return false;  // a 2-point line can never be a closed boundary ring
    const size_t base = static_cast<size_t>(ref.index) * 6;
    if (base + 5 >= st.userLinesFlat.size())
      return false;
    out.verts.push_back({static_cast<double>(st.userLinesFlat[base + 0]) + st.worldDocumentOriginX,
                         static_cast<double>(st.userLinesFlat[base + 1]) + st.worldDocumentOriginY,
                         static_cast<double>(st.userLinesFlat[base + 2])});
    out.verts.push_back({static_cast<double>(st.userLinesFlat[base + 3]) + st.worldDocumentOriginX,
                         static_cast<double>(st.userLinesFlat[base + 4]) + st.worldDocumentOriginY,
                         static_cast<double>(st.userLinesFlat[base + 5])});
    return true;
  }

  if (ref.kind == EntityKind::Polyline) {
    const size_t pi = static_cast<size_t>(ref.index);
    if (pi + 1 >= st.userPolylineOffsets.size())
      return false;
    const bool closed = pi < st.userPolylineClosed.size() && st.userPolylineClosed[pi] != 0;
    if (requireClosed && !closed)
      return false;
    const int vBegin = st.userPolylineOffsets[pi], vEnd = st.userPolylineOffsets[pi + 1];
    if (vEnd - vBegin < 2)
      return false;
    for (int v = vBegin; v < vEnd; ++v) {
      const size_t base = static_cast<size_t>(v) * 3;
      if (base + 2 >= st.userPolylineVerts.size())
        return false;
      out.verts.push_back({static_cast<double>(st.userPolylineVerts[base + 0]) + st.worldDocumentOriginX,
                           static_cast<double>(st.userPolylineVerts[base + 1]) + st.worldDocumentOriginY,
                           static_cast<double>(st.userPolylineVerts[base + 2])});
    }
    out.closed = closed;
    return true;
  }

  // REQ-087 / REQ-088. Without this a feature line does not merely fail to resolve — it resolves as
  // ABSENT, and ResolveSurfaceInputs treats an unresolvable id as an entity that no longer exists,
  // so it strips the breakline from the STORED definition and reports "breakline(s) no longer
  // exist" about a line sitting in plain view. Designating one was silently self-undoing.
  //
  // Elevation points need no handling here, and that is ADR-035 (a) paying off rather than an
  // omission: a flagged vertex is an ordinary vertex in the plan chain, so the triangulator folds
  // its elevation in with every other vertex's and the constraint edge follows the line exactly.
  if (ref.kind == EntityKind::FeatureLine) {
    const size_t fi = static_cast<size_t>(ref.index);
    if (fi + 1 >= st.featureLineOffsets.size())
      return false;
    const bool closed = fi < st.featureLineClosed.size() && st.featureLineClosed[fi] != 0;
    if (requireClosed && !closed)
      return false;
    const int vBegin = st.featureLineOffsets[fi], vEnd = st.featureLineOffsets[fi + 1];
    if (vEnd - vBegin < 2)
      return false;
    for (int v = vBegin; v < vEnd; ++v) {
      const size_t base = static_cast<size_t>(v) * 3;
      if (base + 2 >= st.featureLineVerts.size())
        return false;
      out.verts.push_back({static_cast<double>(st.featureLineVerts[base + 0]) + st.worldDocumentOriginX,
                           static_cast<double>(st.featureLineVerts[base + 1]) + st.worldDocumentOriginY,
                           static_cast<double>(st.featureLineVerts[base + 2])});
    }
    out.closed = closed;
    return true;
  }

  return false;  // any other entity kind is not a valid breakline/boundary source
}

/// Appends one \ref TinConstraint per edge of \p chain — consecutive vertex pairs, plus the closing
/// edge (last→first) when \p forceClosed or the source itself is closed.
void AppendChainConstraints(const ResolvedChain& chain, bool forceClosed, std::vector<TinConstraint>& out) {
  const size_t n = chain.verts.size();
  if (n < 2)
    return;
  for (size_t i = 0; i + 1 < n; ++i) {
    TinConstraint c;
    c.ax = chain.verts[i][0]; c.ay = chain.verts[i][1]; c.az = static_cast<float>(chain.verts[i][2]);
    c.bx = chain.verts[i + 1][0]; c.by = chain.verts[i + 1][1]; c.bz = static_cast<float>(chain.verts[i + 1][2]);
    out.push_back(c);
  }
  if ((forceClosed || chain.closed) && n >= 3) {
    TinConstraint c;
    c.ax = chain.verts[n - 1][0]; c.ay = chain.verts[n - 1][1]; c.az = static_cast<float>(chain.verts[n - 1][2]);
    c.bx = chain.verts[0][0]; c.by = chain.verts[0][1]; c.bz = static_cast<float>(chain.verts[0][2]);
    out.push_back(c);
  }
}

/// Everything a surface build needs, already resolved against \c AppCommandState — plain data, safe
/// to copy into a worker thread with no further access to drawing state (architecture §8 rule 1).
struct SurfaceBuildInputs {
  std::vector<TinInputPoint> pts;
  std::vector<TinConstraint> constraints;
  std::vector<TinBoundaryLoop> cullLoops;
  double originX = 0.0, originY = 0.0;
  /// A linked point file could not be read (REQ-086). The build is ABANDONED rather than run on what
  /// is left: a surface whose file went missing must keep its last good triangulation, not quietly
  /// rebuild itself smaller (REQ-001 — reject, never absorb). Distinct from a build that fails on its
  /// own merits, because the inputs here are known-incomplete before the triangulator ever runs.
  bool inputsIncomplete = false;
  std::string incompleteReason;
};

/// UI-thread only: resolves a surface's definition against CURRENT drawing state — point groups,
/// breaklines, boundaries — pruning any id that no longer resolves (REQ-069) and logging what was
/// dropped or found in conflict. This is the only part of a surface build that touches
/// \c AppCommandState; everything after it (\ref RunSurfaceBuild) is pure and thread-safe.
SurfaceBuildInputs ResolveSurfaceInputs(AppCommandState& st, CadSurface& surface, std::vector<std::string>& log) {
  SurfaceBuildInputs in;
  in.originX = st.worldDocumentOriginX;
  in.originY = st.worldDocumentOriginY;

  // Gather points from every named group. Groups are referenced by name (REQ-067), so a name that
  // no longer resolves is reported rather than silently contributing nothing — otherwise a renamed
  // group would quietly shrink a surface with no indication why.
  int unresolvedGroups = 0;
  for (const std::string& gname : surface.sourcePointGroups) {
    const int gi = FindPointGroupIndex(st, gname);
    if (gi < 0) {
      ++unresolvedGroups;
      log.push_back("Surface \"" + surface.name + "\": point group \"" + gname + "\" no longer exists.");
      continue;
    }
    for (int pi : ResolvePointGroup(st, st.pointGroups[static_cast<size_t>(gi)], &log)) {
      const SurveyPoint& p = st.surveyPoints[static_cast<size_t>(pi)];
      // Triangulate in WORLD coordinates, in double: at state-plane magnitudes the local frame is
      // what keeps float storage precise, but the predicates need the real spacing between points
      // (ADR-028 (d)). The result is converted back to local by \ref ToLocalTin.
      in.pts.push_back({static_cast<double>(p.easting) + st.worldDocumentOriginX,
                        static_cast<double>(p.northing) + st.worldDocumentOriginY, p.elevation});
    }
  }
  (void)unresolvedGroups;

  // Linked point files (REQ-086). Read HERE, on the UI thread, so the rebuild worker keeps touching
  // neither AppCommandState nor the filesystem. Coordinates come out of the file already in world
  // double, which is the frame the triangulator wants — no origin round trip.
  for (const CadSurfacePointFile& pf : surface.sourcePointFiles) {
    std::vector<SurveyFilePoint> filePts;
    int skipped = 0;
    std::string err;
    if (!SurveyCsvReadPointsOnly(pf.path.c_str(), SurveyCsvLayoutFromUiIndex(pf.layoutIndex),
                                 pf.skipFirstRow, &filePts, &skipped, &err)) {
      // Named, not swallowed — and it stops the build (see SurfaceBuildInputs::inputsIncomplete).
      in.inputsIncomplete = true;
      in.incompleteReason = "point file \"" + pf.path + "\" could not be read (" + err + ")";
      log.push_back("Surface \"" + surface.name + "\": " + in.incompleteReason +
                    ". Keeping the previous triangulation.");
      continue;
    }
    if (skipped > 0)
      log.push_back("Surface \"" + surface.name + "\": point file \"" + pf.path + "\" — " +
                    std::to_string(skipped) + " unreadable row(s) skipped.");
    for (const SurveyFilePoint& p : filePts)
      in.pts.push_back({p.easting, p.northing, p.elevation});
  }

  // Breaklines (REQ-069): resolve each by stable entity id, dropping — not merely skipping — any
  // that no longer resolve, so the STORED definition never holds a dangling reference (§8 ASSUMPTION-1).
  std::vector<CadSurfaceBreakline> resolvedBreaklines;
  int droppedBreaklines = 0;
  for (const CadSurfaceBreakline& bl : surface.breaklines) {
    ResolvedChain chain;
    if (!ResolveDefinitionChain(st, bl.entityId, /*requireClosed=*/false, chain)) {
      ++droppedBreaklines;
      continue;
    }
    resolvedBreaklines.push_back(bl);
    AppendChainConstraints(chain, /*forceClosed=*/false, in.constraints);
  }
  surface.breaklines = std::move(resolvedBreaklines);
  if (droppedBreaklines > 0)
    log.push_back("Surface \"" + surface.name + "\": " + std::to_string(droppedBreaklines) +
                  " breakline(s) no longer exist and were removed from the definition.");

  // Boundaries (REQ-069): same dangling-id handling; each ring's edges become constraints too (Q1),
  // so the triangulation conforms exactly to the boundary and culling is exact, not approximate.
  std::vector<CadSurfaceBoundary> resolvedBoundaries;
  int droppedBoundaries = 0;
  for (const CadSurfaceBoundary& b : surface.boundaries) {
    ResolvedChain chain;
    if (!ResolveDefinitionChain(st, b.entityId, /*requireClosed=*/true, chain)) {
      ++droppedBoundaries;
      continue;
    }
    resolvedBoundaries.push_back(b);
    AppendChainConstraints(chain, /*forceClosed=*/true, in.constraints);
    TinBoundaryLoop loop;
    loop.kind = b.kind == CadBoundaryKind::Outer ? TinBoundaryKind::Outer
              : b.kind == CadBoundaryKind::Hide  ? TinBoundaryKind::Hide
                                                  : TinBoundaryKind::Show;
    for (const auto& v : chain.verts)
      loop.ring.push_back({v[0], v[1]});
    in.cullLoops.push_back(std::move(loop));
  }
  surface.boundaries = std::move(resolvedBoundaries);
  if (droppedBoundaries > 0)
    log.push_back("Surface \"" + surface.name + "\": " + std::to_string(droppedBoundaries) +
                  " boundary(ies) no longer exist and were removed from the definition.");

  // Crossing breaklines at different elevations (REQ-069): reported by name and location so the
  // conflict can actually be found and fixed, rather than left to be inferred from a stray edge.
  for (const TinCrossingIssue& issue : TinFindCrossingConflicts(in.constraints))
    log.push_back("Surface \"" + surface.name + "\": breaklines cross at (" +
                  std::to_string(issue.x) + ", " + std::to_string(issue.y) +
                  ") with different elevations (" + std::to_string(issue.zFromA) + " vs " +
                  std::to_string(issue.zFromB) + ").");

  return in;
}

/// Pure: the triangulation + boundary culling, given already-resolved inputs. Touches no
/// \c AppCommandState, so it is safe to run on a worker thread (architecture §8) or synchronously.
TinBuildResult RunSurfaceBuild(const SurfaceBuildInputs& in) {
  TinBuildResult r = BuildTin(in.pts, in.constraints);
  if (r.ok())
    TinCullByBoundaries(r.indices, r.vertsXyz, in.cullLoops);
  return r;
}

/// Converts a world-space \ref TinBuildResult into a local-frame \ref CadTin (the local-storage
/// invariant: world = local + origin — architecture §11.8), or null when \p r has no surviving
/// triangle (a successful build that boundaries clipped to nothing is still "no surface" — REQ-001).
std::shared_ptr<CadTin> ToLocalTin(const TinBuildResult& r, double originX, double originY) {
  if (!r.ok() || r.indices.empty())
    return nullptr;
  auto tin = std::make_shared<CadTin>();
  tin->vertsXyz.resize(r.vertsXyz.size());
  for (int i = 0; i < r.vertexCount(); ++i) {
    tin->vertsXyz[static_cast<size_t>(i) * 3 + 0] =
        static_cast<float>(static_cast<double>(r.vertsXyz[static_cast<size_t>(i) * 3 + 0]) - originX);
    tin->vertsXyz[static_cast<size_t>(i) * 3 + 1] =
        static_cast<float>(static_cast<double>(r.vertsXyz[static_cast<size_t>(i) * 3 + 1]) - originY);
    tin->vertsXyz[static_cast<size_t>(i) * 3 + 2] = r.vertsXyz[static_cast<size_t>(i) * 3 + 2];  // Z absolute
  }
  tin->indices = r.indices;
  return tin;
}

} // namespace

bool BuildSurfaceFromSources(AppCommandState& st, CadSurface& surface, std::vector<std::string>& log) {
  // Synchronous path: explicit user actions (create, the manual Rebuild button) that should show a
  // result immediately rather than waiting a frame for the async path below to pick them up.
  const SurfaceBuildInputs in = ResolveSurfaceInputs(st, surface, log);
  if (in.inputsIncomplete) {
    // REQ-086: a source that could not be read leaves the surface exactly as it was — no partial
    // rebuild on what survived. The revision IS advanced so the tick does not reopen a missing file
    // every frame; `lastBuildIncomplete` is what keeps the surface showing as not-current.
    surface.builtAtRevision = st.cadGpuRevision;
    surface.lastBuildIncomplete = true;
    surface.lastBuildMessage = "Not rebuilt: " + in.incompleteReason + ".";
    return false;
  }
  const TinBuildResult r = RunSurfaceBuild(in);
  surface.builtAtRevision = st.cadGpuRevision;
  surface.lastBuildIncomplete = false;
  if (!r.ok()) {
    // No partial surface, and the previous triangulation is left alone (REQ-001).
    surface.lastBuildMessage = r.message;
    log.push_back("Surface \"" + surface.name + "\" not built: " + r.message);
    return false;
  }
  std::shared_ptr<CadTin> tin = ToLocalTin(r, in.originX, in.originY);
  if (!tin) {
    surface.lastBuildMessage = "Boundaries left no surface.";
    log.push_back("Surface \"" + surface.name + "\" not built: boundaries left no surface.");
    return false;
  }

  // Replace the pointer, never write through it (architecture §11.5).
  surface.tin = std::move(tin);
  surface.lastBuildMessage = r.message;

  std::string msg = "Surface \"" + surface.name + "\": " + std::to_string(surface.vertexCount()) +
                    " points, " + std::to_string(surface.triangleCount()) + " triangles.";
  if (!r.message.empty())
    msg += " " + r.message;
  if (r.constraintsUnresolved > 0)
    msg += " " + std::to_string(r.constraintsUnresolved) + " constraint edge(s) could not be enforced.";
  log.push_back(msg);
  return true;
}

int CreateSurfaceFromPointGroups(AppCommandState& st, const std::string& name,
                                 const std::vector<std::string>& groupNames,
                                 std::vector<std::string>& log) {
  if (name.empty()) {
    log.push_back("Surface name cannot be empty.");
    return -1;
  }
  if (FindSurfaceIndex(st, name) >= 0) {
    log.push_back("A surface named \"" + name + "\" already exists.");
    return -1;
  }
  // Bumped before the build (not after, as every other surface-mutating call site here does) so the
  // freshly built surface's builtAtRevision already matches the revision this creation settles at —
  // otherwise the very next TickSurfaceRebuilds tick would see it one revision stale and redispatch
  // a redundant rebuild of a surface that was just built moments ago.
  BumpCadGpuCache(st);
  CadSurface s;
  s.name = name;
  s.sourcePointGroups = groupNames;
  if (!BuildSurfaceFromSources(st, s, log))
    return -1;  // nothing built → no surface added, rather than an empty one to puzzle over

  st.cadSurfaces.push_back(std::move(s));
  EnsureAttrCounts(st);  // owns attribute-array growth for every entity type, surfaces included
  return static_cast<int>(st.cadSurfaces.size()) - 1;
}

void EraseSurfaceAtIndex(AppCommandState& st, size_t index) {
  if (index >= st.cadSurfaces.size())
    return;
  st.cadSurfaces.erase(st.cadSurfaces.begin() + static_cast<std::ptrdiff_t>(index));
  if (index < st.cadSurfaceAttrs.size())
    st.cadSurfaceAttrs.erase(st.cadSurfaceAttrs.begin() + static_cast<std::ptrdiff_t>(index));
  BumpCadGpuCache(st);
}

void TickSurfaceRebuilds(AppCommandState& st, std::vector<std::string>& log) {
  using SurfaceJob = AppCommandState::SurfaceRebuildAsync;

  // Reap finished workers first, so a surface freed up this frame can be redispatched this same
  // frame rather than waiting one extra frame.
  for (size_t i = 0; i < st.surfaceRebuildAsync.size();) {
    SurfaceJob& job = *st.surfaceRebuildAsync[i];
    if (!job.done.load(std::memory_order_acquire)) {
      ++i;
      continue;
    }
    job.thread.join();
    const int si = FindSurfaceIndexById(st, job.surfaceId);
    // Applied only if the surface still exists AND nothing has changed since this job was
    // dispatched (architecture §8 rule 4). Either condition failing means discard: the surface was
    // erased, or an undo / further edit landed while this ran — REQ-069's "the in-flight result is
    // discarded." A discarded surface simply looks dirty again next tick and gets redispatched.
    if (si >= 0 && st.cadGpuRevision == job.generation) {
      CadSurface& surface = st.cadSurfaces[static_cast<size_t>(si)];
      const TinBuildResult& r = job.result;
      std::shared_ptr<CadTin> tin = ToLocalTin(r, job.originX, job.originY);
      if (tin) {
        surface.tin = std::move(tin);  // replace the pointer, never write through it (§11.5)
        surface.lastBuildMessage = r.message;
        std::string msg = "Surface \"" + surface.name + "\": " + std::to_string(surface.vertexCount()) +
                          " points, " + std::to_string(surface.triangleCount()) + " triangles.";
        if (!r.message.empty())
          msg += " " + r.message;
        if (r.constraintsUnresolved > 0)
          msg += " " + std::to_string(r.constraintsUnresolved) + " constraint edge(s) could not be enforced.";
        log.push_back(msg);
      } else {
        // No partial surface; the previous triangulation (if any) is left alone (REQ-001).
        surface.lastBuildMessage = r.ok() ? "Boundaries left no surface." : r.message;
        log.push_back("Surface \"" + surface.name + "\" not rebuilt: " + surface.lastBuildMessage);
      }
      surface.builtAtRevision = job.generation;
      surface.lastBuildIncomplete = false;  // a result that landed means the inputs were complete
    }
    st.surfaceRebuildAsync.erase(st.surfaceRebuildAsync.begin() + static_cast<std::ptrdiff_t>(i));
  }

  // Dispatch a rebuild for every surface whose definition might have changed and does not already
  // have one in flight — one dispatch per surface per revision is REQ-069's "at most one rebuild per
  // command/undo boundary," since a command bumps cadGpuRevision once however many points it moved.
  for (size_t sIdx = 0; sIdx < st.cadSurfaces.size(); ++sIdx) {
    CadSurface& surface = st.cadSurfaces[sIdx];
    if (surface.builtAtRevision == st.cadGpuRevision)
      continue;
    // The id is assigned by EnsureEntityIds, which main.cpp runs immediately before this every frame.
    // A surface created since that sweep has id 0 and is skipped for exactly one frame rather than
    // dispatched under a key that cannot be looked up again.
    const std::uint64_t surfaceId =
        sIdx < st.cadSurfaceAttrs.size() ? st.cadSurfaceAttrs[sIdx].id : 0;
    if (surfaceId == 0)
      continue;
    const bool alreadyRunning =
        std::any_of(st.surfaceRebuildAsync.begin(), st.surfaceRebuildAsync.end(),
                   [&](const std::unique_ptr<SurfaceJob>& j) { return j->surfaceId == surfaceId; });
    if (alreadyRunning)
      continue;

    // Resolution against AppCommandState happens HERE, on the UI thread — the worker below receives
    // only the already-resolved, plain-data result and touches no drawing state (architecture §8
    // rule 1). This is also where dangling breakline/boundary ids actually get dropped from the
    // definition, so that observably happens the very next frame after the referenced entity is
    // deleted, not only once the (possibly slower) background triangulation finishes.
    std::vector<std::string> resolveLog;
    SurfaceBuildInputs inputs = ResolveSurfaceInputs(st, surface, resolveLog);
    for (std::string& m : resolveLog)
      log.push_back(std::move(m));

    if (inputs.inputsIncomplete) {
      // REQ-086: don't dispatch a worker to triangulate inputs already known to be short. The surface
      // keeps the triangulation it has. The revision is advanced so this does not re-read a missing
      // file every frame — `lastBuildIncomplete` carries the not-current state instead, and the retry
      // comes with the next drawing change or an explicit rebuild. ResolveSurfaceInputs already
      // logged which file and why.
      surface.builtAtRevision = st.cadGpuRevision;
      surface.lastBuildIncomplete = true;
      surface.lastBuildMessage = "Not rebuilt: " + inputs.incompleteReason + ".";
      continue;
    }

    auto job = std::make_unique<SurfaceJob>();
    job->surfaceId = surfaceId;
    job->generation = st.cadGpuRevision;
    job->originX = st.worldDocumentOriginX;
    job->originY = st.worldDocumentOriginY;
    SurfaceJob* jobPtr = job.get();
    jobPtr->thread = std::thread([jobPtr, in = std::move(inputs)]() {
      if (jobPtr->cancel.load(std::memory_order_acquire)) {
        jobPtr->done.store(true, std::memory_order_release);
        return;
      }
      jobPtr->result = RunSurfaceBuild(in);
      jobPtr->done.store(true, std::memory_order_release);
    });
    st.surfaceRebuildAsync.push_back(std::move(job));
  }
}

namespace {

/// Splits `a, b, c` into trimmed fields.
///
/// Commas rather than spaces because surface names and point-group names routinely contain them
/// ("Existing Ground", "Ground + Curb") — the same problem DESIGNATEBOUNDARY solves by reading its
/// kind off the END of the line, which does not generalise to a variable-length group list. Empty
/// fields are kept rather than skipped so the caller can reject them by name instead of silently
/// building from a shorter list than the user typed.
std::vector<std::string> SplitCommaFields(const std::string& s) {
  std::vector<std::string> out;
  size_t start = 0;
  for (;;) {
    const size_t comma = s.find(',', start);
    const size_t len = (comma == std::string::npos) ? std::string::npos : comma - start;
    out.push_back(StringUtil::trimCopy(s.substr(start, len)));
    if (comma == std::string::npos)
      break;
    start = comma + 1;
  }
  return out;
}

/// The full definition of every surface, one line each, plus a line per definition item.
///
/// This is the only way to observe a surface without a window: a surface has no entity id, so
/// nothing else in the command layer can report one, and the Surfaces panel is unreachable from the
/// REQ-203 driver. The per-item indices printed here are exactly what UNDESIGNATE takes.
void ReportSurfaces(const AppCommandState& st, std::vector<std::string>& log) {
  if (st.cadSurfaces.empty()) {
    log.push_back("SURFACELIST — no surfaces in the drawing.");
    return;
  }
  for (const CadSurface& s : st.cadSurfaces) {
    std::string line = "Surface \"" + s.name + "\": ";
    line += s.tin ? (std::to_string(s.vertexCount()) + " points, " + std::to_string(s.triangleCount()) +
                     " triangles")
                  : std::string("not built");
    line += ", " + std::to_string(s.breaklines.size()) + " breakline(s), " +
            std::to_string(s.boundaries.size()) + " boundary(ies), " +
            std::to_string(s.sourcePointFiles.size()) + " point file(s).";
    log.push_back(line);

    for (size_t i = 0; i < s.sourcePointGroups.size(); ++i) {
      const bool exists = FindPointGroupIndex(st, s.sourcePointGroups[i]) >= 0;
      log.push_back("  group " + std::to_string(i + 1) + ": \"" + s.sourcePointGroups[i] + "\"" +
                    (exists ? "" : "  (missing)"));
    }
    for (size_t i = 0; i < s.sourcePointFiles.size(); ++i) {
      const CadSurfacePointFile& pf = s.sourcePointFiles[i];
      const char* lay = pf.layoutIndex == 1 ? "PENZD" : pf.layoutIndex == 2 ? "NEZ" : pf.layoutIndex == 3 ? "ENZ" : "PNEZD";
      log.push_back("  point file " + std::to_string(i + 1) + ": \"" + pf.path + "\" (" + lay +
                    (pf.skipFirstRow ? ", header" : "") + ")");
    }
    for (size_t i = 0; i < s.breaklines.size(); ++i)
      log.push_back("  breakline " + std::to_string(i + 1) + ": entity id " +
                    std::to_string(s.breaklines[i].entityId) +
                    (s.breaklines[i].description.empty() ? "" : "  \"" + s.breaklines[i].description + "\""));
    for (size_t i = 0; i < s.boundaries.size(); ++i) {
      const CadSurfaceBoundary& b = s.boundaries[i];
      const char* kindName = b.kind == CadBoundaryKind::Outer ? "outer"
                            : b.kind == CadBoundaryKind::Hide  ? "hide"
                                                               : "show";
      log.push_back("  boundary " + std::to_string(i + 1) + ": " + kindName + ", entity id " +
                    std::to_string(b.entityId) + (b.name.empty() ? "" : "  \"" + b.name + "\""));
    }
    if (!s.lastBuildMessage.empty())
      log.push_back("  last build: " + s.lastBuildMessage);
  }
}

/// `SURFACECREATE <name>, <group>[, <group>…]` — the command-line twin of the Surfaces panel's
/// "New from group…". Every named group must resolve: a typo that silently contributed no points
/// would produce a surface built from less than the user asked for, with nothing on screen saying so.
void RunSurfaceCreate(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitCommaFields(args);
  if (f.size() < 2 || f[0].empty()) {
    log.push_back("SURFACECREATE — usage: SURFACECREATE <name>, <point group>[, <point group>…].");
    return;
  }
  std::vector<std::string> groups;
  for (size_t i = 1; i < f.size(); ++i) {
    if (f[i].empty()) {
      log.push_back("SURFACECREATE — empty point group name in the list.");
      return;
    }
    if (FindPointGroupIndex(st, f[i]) < 0) {
      log.push_back("SURFACECREATE — no point group named \"" + f[i] + "\".");
      return;
    }
    groups.push_back(f[i]);
  }
  PushUndoSnapshot(st, "Create surface");
  CreateSurfaceFromPointGroups(st, f[0], groups, log);  // reports its own failure (REQ-201)
}

/// `SURFACERENAME <old>, <new>` — same duplicate-name refusal as the panel (REQ-075).
void RunSurfaceRename(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitCommaFields(args);
  if (f.size() != 2 || f[0].empty() || f[1].empty()) {
    log.push_back("SURFACERENAME — usage: SURFACERENAME <old name>, <new name>.");
    return;
  }
  const int si = FindSurfaceIndex(st, f[0]);
  if (si < 0) {
    log.push_back("SURFACERENAME — no surface named \"" + f[0] + "\".");
    return;
  }
  const int clash = FindSurfaceIndex(st, f[1]);
  if (clash >= 0 && clash != si) {
    log.push_back("SURFACERENAME — a surface named \"" + f[1] + "\" already exists — rename refused.");
    return;
  }
  PushUndoSnapshot(st, "Rename surface");
  log.push_back("Renamed surface \"" + st.cadSurfaces[static_cast<size_t>(si)].name + "\" to \"" + f[1] + "\".");
  st.cadSurfaces[static_cast<size_t>(si)].name = f[1];
  BumpCadGpuCache(st);
}

/// `SURFACEDELETE <name>` — undoable in one step (REQ-068), like the panel's Delete.
void RunSurfaceDelete(AppCommandState& st, const std::string& name, std::vector<std::string>& log) {
  if (name.empty()) {
    log.push_back("SURFACEDELETE — usage: SURFACEDELETE <surface name>.");
    return;
  }
  const int si = FindSurfaceIndex(st, name);
  if (si < 0) {
    log.push_back("SURFACEDELETE — no surface named \"" + name + "\".");
    return;
  }
  PushUndoSnapshot(st, "Delete surface");
  log.push_back("Deleted surface \"" + st.cadSurfaces[static_cast<size_t>(si)].name + "\".");
  EraseSurfaceAtIndex(st, static_cast<size_t>(si));
}

/// `SURFACEREBUILD [<name>]` — rebuilds one surface, or every surface when the name is omitted.
/// Synchronous (\ref BuildSurfaceFromSources), so the result is in the log by the time the command
/// returns — which is what lets a REQ-203 transcript assert on it without pumping a frame loop.
void RunSurfaceRebuild(AppCommandState& st, const std::string& name, std::vector<std::string>& log) {
  if (st.cadSurfaces.empty()) {
    log.push_back("SURFACEREBUILD — no surfaces in the drawing.");
    return;
  }
  if (name.empty()) {
    PushUndoSnapshot(st, "Rebuild surfaces");
    for (CadSurface& s : st.cadSurfaces)
      BuildSurfaceFromSources(st, s, log);
    BumpCadGpuCache(st);
    return;
  }
  const int si = FindSurfaceIndex(st, name);
  if (si < 0) {
    log.push_back("SURFACEREBUILD — no surface named \"" + name + "\".");
    return;
  }
  PushUndoSnapshot(st, "Rebuild surface");
  BuildSurfaceFromSources(st, st.cadSurfaces[static_cast<size_t>(si)], log);
  BumpCadGpuCache(st);
}

/// `SURFACEADDFILE <surface>, <path>[, <layout>[, HEADER]]` — links a point file into a surface's
/// definition (REQ-086). The file is NOT imported: its points feed the triangulation and never become
/// drawing survey points. `<layout>` is one of PNEZD / PENZD / NEZ / ENZ, defaulting to the first;
/// `HEADER` says the file's first row is a header.
///
/// The file is read once here purely to refuse a path that cannot be read at all — linking something
/// unreadable and only discovering it at the next rebuild would put the error a long way from the
/// action that caused it (REQ-201).
void RunSurfaceAddFile(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitCommaFields(args);
  if (f.size() < 2 || f[0].empty() || f[1].empty()) {
    log.push_back("SURFACEADDFILE — usage: SURFACEADDFILE <surface>, <path>[, <PNEZD|PENZD|NEZ|ENZ>[, HEADER]].");
    return;
  }
  const int si = FindSurfaceIndex(st, f[0]);
  if (si < 0) {
    log.push_back("SURFACEADDFILE — no surface named \"" + f[0] + "\".");
    return;
  }
  CadSurfacePointFile pf;
  pf.path = f[1];
  if (f.size() >= 3 && !f[2].empty()) {
    const std::string lay = StringUtil::toLowerAsciiCopy(f[2]);
    if (lay == "pnezd")      pf.layoutIndex = 0;
    else if (lay == "penzd") pf.layoutIndex = 1;
    else if (lay == "nez")   pf.layoutIndex = 2;
    else if (lay == "enz")   pf.layoutIndex = 3;
    else {
      log.push_back("SURFACEADDFILE — layout must be PNEZD, PENZD, NEZ or ENZ.");
      return;
    }
  }
  for (size_t i = 3; i < f.size(); ++i)
    if (StringUtil::toLowerAsciiCopy(f[i]) == "header")
      pf.skipFirstRow = true;

  std::vector<SurveyFilePoint> probe;
  int skipped = 0;
  std::string err;
  if (!SurveyCsvReadPointsOnly(pf.path.c_str(), SurveyCsvLayoutFromUiIndex(pf.layoutIndex), pf.skipFirstRow,
                               &probe, &skipped, &err)) {
    log.push_back("SURFACEADDFILE — cannot read \"" + pf.path + "\": " + err + ". Not linked.");
    return;
  }

  PushUndoSnapshot(st, "Link point file to surface");
  CadSurface& s = st.cadSurfaces[static_cast<size_t>(si)];
  log.push_back("SURFACEADDFILE — linked \"" + pf.path + "\" to \"" + s.name + "\" (" +
                std::to_string(probe.size()) + " point(s)" +
                (skipped > 0 ? ", " + std::to_string(skipped) + " row(s) unreadable" : "") + ").");
  s.sourcePointFiles.push_back(std::move(pf));
  BumpCadGpuCache(st);
}

/// `SURFACEIMPORTFILE <surface>, <n>` — REQ-086's "break the link": reads the linked file once
/// through the REQ-083 import path, so its points become real survey points in the drawing and a
/// point group the surface references, then drops the link. The surface must still build identically
/// afterwards, which is the acceptance condition this exists to satisfy.
void RunSurfaceImportFile(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitCommaFields(args);
  if (f.size() != 2 || f[0].empty()) {
    log.push_back("SURFACEIMPORTFILE — usage: SURFACEIMPORTFILE <surface>, <number> (SURFACELIST numbers them).");
    return;
  }
  const int si = FindSurfaceIndex(st, f[0]);
  if (si < 0) {
    log.push_back("SURFACEIMPORTFILE — no surface named \"" + f[0] + "\".");
    return;
  }
  CadSurface& s = st.cadSurfaces[static_cast<size_t>(si)];
  char* numEnd = nullptr;
  const long parsed = std::strtol(f[1].c_str(), &numEnd, 10);
  const bool numOk = !f[1].empty() && numEnd && *numEnd == '\0';
  const int n = numOk ? static_cast<int>(parsed) : 0;
  if (n < 1 || static_cast<size_t>(n) > s.sourcePointFiles.size()) {
    log.push_back("SURFACEIMPORTFILE — \"" + s.name + "\" has " + std::to_string(s.sourcePointFiles.size()) +
                  " point file(s); " + f[1] + " is out of range.");
    return;
  }

  const CadSurfacePointFile pf = s.sourcePointFiles[static_cast<size_t>(n - 1)];
  PushUndoSnapshot(st, "Import surface point file");

  // Drive the REQ-083 importer through its own state, so a file imported this way and a file imported
  // from the Import Points panel go down exactly one code path.
  const int savedLayout = st.surveyImportCsvLayoutIdx;
  const bool savedSkip = st.surveyImportCsvSkipFirstRow;
  std::string savedPath = st.surveyImportCsvPath;
  std::snprintf(st.surveyImportCsvPath, sizeof(st.surveyImportCsvPath), "%s", pf.path.c_str());
  st.surveyImportCsvLayoutIdx = pf.layoutIndex;
  st.surveyImportCsvSkipFirstRow = pf.skipFirstRow;
  const size_t before = st.surveyPoints.size();
  const bool ok = SurveyCsvImportFile(st, log);
  std::snprintf(st.surveyImportCsvPath, sizeof(st.surveyImportCsvPath), "%s", savedPath.c_str());
  st.surveyImportCsvLayoutIdx = savedLayout;
  st.surveyImportCsvSkipFirstRow = savedSkip;

  if (!ok) {
    log.push_back("SURFACEIMPORTFILE — import failed; the link is left in place.");
    return;
  }
  const size_t added = st.surveyPoints.size() - before;
  if (added == 0) {
    // The importer skips rows whose point id already exists (REQ-083's rule), so a file whose ids
    // collide with the drawing imports nothing. Breaking the link here would silently delete the
    // file's contribution from the surface — the link is the only thing still supplying those
    // points. Keep it, and say why.
    log.push_back("SURFACEIMPORTFILE — no points were imported (see the lines above; duplicate point "
                  "ids are skipped). The link to \"" + pf.path + "\" is left in place.");
    return;
  }

  // A group covering exactly the points just imported, so the surface keeps the same points by the
  // same rule every other source uses (REQ-067) rather than by a second mechanism.
  PointGroup g;
  g.name = "Imported: " + std::filesystem::path(pf.path).filename().string();
  for (int i = 1; i < 10000 && FindPointGroupIndex(st, g.name) >= 0; ++i)
    g.name = "Imported: " + std::filesystem::path(pf.path).filename().string() + " (" + std::to_string(i + 1) + ")";
  // Explicit ids, not a description rule: the group must mean "exactly the points this file brought
  // in", and a description wildcard would silently pick up unrelated shots that happen to match.
  for (size_t i = before; i < st.surveyPoints.size(); ++i)
    g.rule.explicitIds.push_back(st.surveyPoints[i].id);
  st.pointGroups.push_back(g);

  s.sourcePointGroups.push_back(g.name);
  s.sourcePointFiles.erase(s.sourcePointFiles.begin() + (n - 1));
  log.push_back("SURFACEIMPORTFILE — imported " + std::to_string(added) + " point(s) from \"" + pf.path +
                "\" into point group \"" + g.name + "\"; the link is broken.");
  BumpCadGpuCache(st);
}

/// EXTRACT (REQ-071) — defined further down, beside the layer helpers it needs, and declared here
/// because the command dispatch above reaches it first.
void ExecuteExtractCommand(AppCommandState& st, const std::string& args, std::vector<std::string>& log);

// SURFSTYLE (REQ-070) — the command form of the Surface Style editor.
//
// It exists alongside the dialog rather than instead of it for two reasons. A dialog cannot be
// driven by a headless transcript, and REQ-070's acceptance conditions are end-to-end claims —
// "changing the contour interval updates the display without rebuilding the triangulation", "two
// surfaces sharing a style both change" — that no unit test can reach, because they are about what
// the command state machine does to the document. The dialog calls the same helpers.
//
// **Comma-separated arguments**, like every other surface command: style names and surface names
// routinely contain spaces ("Existing Ground", "Contours 1 ft"), so splitting on whitespace would
// make half the names in a real drawing unaddressable. Same reason SURFACECREATE does it.
//
// Every edit goes through PushUndoSnapshot, so a style change is a single undo step. The ADR-020
// document-owned-table pattern makes that free: the table is already in the geometry snapshot.

/// The component \p word names, or nullptr with \p why set — REQ-201, so a typo says what it would
/// have accepted rather than "invalid".
SurfaceComponentStyle* SurfaceComponentByName(SurfaceStyle& s, const std::string& word,
                                              std::string* why) {
  const std::string w = StringUtil::toLowerAsciiCopy(word);
  if (w == "triangles" || w == "triangle") return &s.triangles;
  if (w == "border") return &s.border;
  if (w == "major" || w == "majorcontour") return &s.majorContour;
  if (w == "minor" || w == "minorcontour") return &s.minorContour;
  if (w == "points" || w == "point") return &s.points;
  if (why)
    *why = "Unknown component \"" + word + "\" — expected triangles, border, major, minor or points.";
  return nullptr;
}

/// Parse one interval field. A separate function so the message names the FIELD, not just the value:
/// "the minor interval" is what the user has to go and fix.
bool ParseIntervalField(const std::string& text, const char* which, double* out,
                        std::vector<std::string>& log) {
  try {
    size_t used = 0;
    const double v = std::stod(text, &used);
    if (used == StringUtil::trimCopy(text).size()) {
      *out = v;
      return true;
    }
  } catch (...) {
    // Not a number — reported below rather than silently treated as zero.
  }
  log.push_back(std::string("SURFSTYLE — the ") + which + " interval must be a number, not \"" +
                text + "\".");
  return false;
}

void ExecuteSurfStyleCommand(AppCommandState& st, const std::string& args,
                             std::vector<std::string>& log) {
  SurfaceStyles::EnsureStandard(st.surfaceStyles);

  std::string rest;
  const std::string verb = StringUtil::toLowerAsciiCopy(
      StringUtil::trimCopy(args.substr(0, args.find_first_of(" \t,"))));
  {
    const size_t sp = args.find_first_of(" \t");
    rest = sp == std::string::npos ? std::string() : StringUtil::trimCopy(args.substr(sp + 1));
  }

  if (verb.empty()) {
    st.showSurfaceStyleWindow = true;
    log.push_back("SURFSTYLE — surface style editor opened.");
    return;
  }

  const auto usage = [&]() {
    log.push_back("SURFSTYLE — usage: SURFSTYLE (opens the editor) | NEW <style> | DELETE <style> | "
                  "INTERVAL <style>, <minor>, <major> | SHOW|HIDE <style>, "
                  "<triangles|border|major|minor|points> | ASSIGN <surface>, <style>");
  };

  if (verb == "new") {
    const std::string name = StringUtil::trimCopy(rest);
    if (name.empty()) {
      usage();
      return;
    }
    if (SurfaceStyles::Find(st.surfaceStyles, name)) {
      log.push_back("SURFSTYLE — a style named \"" + name + "\" already exists.");
      return;
    }
    PushUndoSnapshot(st, "Surface style");
    // Copied from Standard rather than value-initialised: a new style that drew nothing would look
    // like the create had failed.
    SurfaceStyle s = SurfaceStyles::StandardSurfaceStyle();
    s.name = name;
    st.surfaceStyles.push_back(std::move(s));
    BumpCadGpuCache(st);
    log.push_back("SURFSTYLE — created style \"" + name + "\" (copied from Standard).");
    return;
  }

  if (verb == "delete") {
    const std::string name = StringUtil::trimCopy(rest);
    if (name.empty()) {
      usage();
      return;
    }
    if (name == SurfaceStyles::kStandardName) {
      log.push_back("SURFSTYLE — \"Standard\" cannot be deleted; it is what every unresolved style "
                    "name falls back to.");
      return;
    }
    const auto it = std::find_if(st.surfaceStyles.begin(), st.surfaceStyles.end(),
                                 [&](const SurfaceStyle& s) { return s.name == name; });
    if (it == st.surfaceStyles.end()) {
      log.push_back("SURFSTYLE — no style named \"" + name + "\".");
      return;
    }
    // Deleting a style that surfaces are using is ALLOWED, unlike a text style. REQ-070 makes the
    // fallback an acceptance condition — "a surface whose style was deleted falls back to a default
    // style rather than failing to draw" — so refusing the delete would leave that path unreachable
    // and untested. The surfaces keep their styleName, so re-creating a style with that name adopts
    // them back rather than leaving the reference silently rewritten.
    int usedBy = 0;
    for (const CadSurface& s : st.cadSurfaces)
      if (s.styleName == name)
        ++usedBy;
    PushUndoSnapshot(st, "Surface style");
    st.surfaceStyles.erase(it);
    BumpCadGpuCache(st);
    log.push_back("SURFSTYLE — deleted style \"" + name + "\"." +
                  (usedBy > 0 ? " " + std::to_string(usedBy) +
                                    " surface(s) using it now draw with \"Standard\"."
                              : std::string()));
    return;
  }

  if (verb == "interval") {
    const std::vector<std::string> f = SplitCommaFields(rest);
    if (f.size() != 3 || f[0].empty()) {
      usage();
      return;
    }
    SurfaceStyle* s = SurfaceStyles::Find(st.surfaceStyles, f[0]);
    if (!s) {
      log.push_back("SURFSTYLE — no style named \"" + f[0] + "\".");
      return;
    }
    double minor = 0.0, major = 0.0;
    if (!ParseIntervalField(f[1], "minor", &minor, log) ||
        !ParseIntervalField(f[2], "major", &major, log))
      return;
    // REQ-070: rejected with a SPECIFIC message, and rejected BEFORE the value is stored, so the
    // invalid pair never exists to generate mis-labelled contours from.
    std::string why;
    if (!SurfaceStyles::IntervalsCompatible(minor, major, &why)) {
      log.push_back("SURFSTYLE — " + why);
      return;
    }
    PushUndoSnapshot(st, "Surface style");
    s->minorIntervalFt = minor;
    s->majorIntervalFt = major;
    // Bumps the drawing revision because the DISPLAY changed, which is what marks the tab dirty. It
    // does not mark any surface for rebuild: a surface's staleness is its triangulation pointer and
    // its resolved style, never this counter (ADR-036 (e)) — which is exactly why an interval change
    // cannot retriangulate.
    BumpCadGpuCache(st);
    log.push_back("SURFSTYLE — \"" + s->name + "\" contours: minor " + SurfaceStyles::FormatFt(minor) +
                  " ft, major " + SurfaceStyles::FormatFt(major) + " ft.");
    return;
  }

  if (verb == "show" || verb == "hide") {
    const std::vector<std::string> f = SplitCommaFields(rest);
    if (f.size() != 2 || f[0].empty() || f[1].empty()) {
      usage();
      return;
    }
    SurfaceStyle* s = SurfaceStyles::Find(st.surfaceStyles, f[0]);
    if (!s) {
      log.push_back("SURFSTYLE — no style named \"" + f[0] + "\".");
      return;
    }
    std::string why;
    SurfaceComponentStyle* comp = SurfaceComponentByName(*s, f[1], &why);
    if (!comp) {
      log.push_back("SURFSTYLE — " + why);
      return;
    }
    PushUndoSnapshot(st, "Surface style");
    comp->visible = (verb == "show");
    BumpCadGpuCache(st);
    log.push_back("SURFSTYLE — \"" + s->name + "\" " + f[1] + " " +
                  (comp->visible ? "shown." : "hidden."));
    return;
  }

  if (verb == "assign") {
    const std::vector<std::string> f = SplitCommaFields(rest);
    if (f.size() != 2 || f[0].empty() || f[1].empty()) {
      usage();
      return;
    }
    const int si = FindSurfaceIndex(st, f[0]);
    if (si < 0) {
      log.push_back("SURFSTYLE — no surface named \"" + f[0] + "\".");
      return;
    }
    if (!SurfaceStyles::Find(st.surfaceStyles, f[1])) {
      log.push_back("SURFSTYLE — no style named \"" + f[1] + "\".");
      return;
    }
    PushUndoSnapshot(st, "Surface style");
    st.cadSurfaces[static_cast<size_t>(si)].styleName = f[1];
    BumpCadGpuCache(st);
    log.push_back("SURFSTYLE — surface \"" + st.cadSurfaces[static_cast<size_t>(si)].name +
                  "\" now uses style \"" + f[1] + "\".");
    return;
  }

  usage();
}


/// `UNDESIGNATE <surface>, <BREAKLINE|BOUNDARY|POINTFILE>, <n>` — removes one item from a surface's
/// definition (REQ-069's "remove", the counterpart to DESIGNATEBREAKLINE/DESIGNATEBOUNDARY). \p n is
/// 1-based and matches the numbering SURFACELIST prints. Deleting the referenced entity also removes
/// the item, but only that entity's other uses go with it — this removes the item alone.
void RunUndesignate(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitCommaFields(args);
  if (f.size() != 3 || f[0].empty()) {
    log.push_back("UNDESIGNATE — usage: UNDESIGNATE <surface name>, <BREAKLINE|BOUNDARY>, <number>.");
    return;
  }
  const int si = FindSurfaceIndex(st, f[0]);
  if (si < 0) {
    log.push_back("UNDESIGNATE — no surface named \"" + f[0] + "\".");
    return;
  }
  const std::string what = StringUtil::toLowerAsciiCopy(f[1]);
  const bool isBoundary = (what == "boundary");
  const bool isPointFile = (what == "pointfile");
  if (!isBoundary && !isPointFile && what != "breakline") {
    log.push_back("UNDESIGNATE — second argument must be BREAKLINE, BOUNDARY or POINTFILE.");
    return;
  }
  // strtol rather than stoi: a non-numeric argument is a user typo, not an exceptional condition,
  // and this translation unit is compiled without exception unwinding (C4530).
  char* numEnd = nullptr;
  const long parsed = std::strtol(f[2].c_str(), &numEnd, 10);
  const bool numOk = !f[2].empty() && numEnd && *numEnd == '\0';
  const int n = numOk ? static_cast<int>(parsed) : 0;  // 0 fails the range check below

  CadSurface& s = st.cadSurfaces[static_cast<size_t>(si)];
  const size_t count = isBoundary     ? s.boundaries.size()
                       : isPointFile  ? s.sourcePointFiles.size()
                                      : s.breaklines.size();
  if (n < 1 || static_cast<size_t>(n) > count) {
    log.push_back("UNDESIGNATE — \"" + s.name + "\" has " + std::to_string(count) + " " + what +
                  "(s); " + f[2] + " is out of range (SURFACELIST numbers them).");
    return;
  }
  PushUndoSnapshot(st, isBoundary    ? "Remove surface boundary"
                       : isPointFile ? "Unlink surface point file"
                                     : "Remove surface breakline");
  if (isBoundary)
    s.boundaries.erase(s.boundaries.begin() + (n - 1));
  else if (isPointFile)
    s.sourcePointFiles.erase(s.sourcePointFiles.begin() + (n - 1));
  else
    s.breaklines.erase(s.breaklines.begin() + (n - 1));
  log.push_back("UNDESIGNATE — removed " + what + " " + std::to_string(n) + " from \"" + s.name + "\".");
  BumpCadGpuCache(st);  // TickSurfaceRebuilds picks the change up; SURFACEREBUILD forces it now
}

} // namespace

void EnsureEntityIds(AppCommandState& st) {
  // Geometry has not changed since the last sweep, so nothing can be missing an id. This is what
  // lets callers invoke it unconditionally — including once a frame — without paying for a walk.
  if (st.entityIdSweepRevision == st.cadGpuRevision)
    return;
  st.entityIdSweepRevision = st.cadGpuRevision;

  std::vector<std::vector<EntityAttributes>*> arrays;
  arrays.reserve(std::size(kEntityKindsInSweepOrder));
  for (EntityKind k : kEntityKindsInSweepOrder)
    arrays.push_back(AttrsForKind(st, k));
  st.nextEntityId = AssignMissingEntityIds(arrays, st.nextEntityId);
}

std::uint64_t AllocEntityId(AppCommandState& st) { return st.nextEntityId++; }

EntityRef FindEntityById(const AppCommandState& st, std::uint64_t id) {
  for (EntityKind k : kEntityKindsInSweepOrder) {
    const int ix = FindEntityIndexById(*AttrsForKind(st, k), id);
    if (ix >= 0)
      return {k, ix};
  }
  // Erased, or never issued. Note this is NOT the entity that took its index — that is the point.
  return {};
}


void PushUndoSnapshot(AppCommandState& st, const std::string& description) {
  const int idx = st.activeDrawingIdx;
  if (idx < 0 || static_cast<size_t>(idx) >= st.documents.size())
    return;
  // Ids before the copy, so every undo frame is id-complete and a restored frame carries the same
  // identities it was captured with (REQ-076 — "unchanged by undo/redo").
  EnsureEntityIds(st);
  auto& doc = st.documents[static_cast<size_t>(idx)];
  doc.undoStack.push_back(CaptureGeometrySnapshot(st, description));
  doc.redoStack.clear();
  if (st.undoHistoryMaxSize > 0) {
    while (static_cast<int>(doc.undoStack.size()) > st.undoHistoryMaxSize)
      doc.undoStack.erase(doc.undoStack.begin());
  }
}

bool CanUndo(const AppCommandState& st) {
  const int idx = st.activeDrawingIdx;
  if (idx < 0 || static_cast<size_t>(idx) >= st.documents.size())
    return false;
  return !st.documents[static_cast<size_t>(idx)].undoStack.empty();
}

bool CanRedo(const AppCommandState& st) {
  const int idx = st.activeDrawingIdx;
  if (idx < 0 || static_cast<size_t>(idx) >= st.documents.size())
    return false;
  return !st.documents[static_cast<size_t>(idx)].redoStack.empty();
}

bool DoUndo(AppCommandState& st, std::vector<std::string>& log) {
  const int idx = st.activeDrawingIdx;
  if (idx < 0 || static_cast<size_t>(idx) >= st.documents.size())
    return false;
  auto& doc = st.documents[static_cast<size_t>(idx)];
  if (doc.undoStack.empty()) {
    log.push_back("Nothing to undo.");
    return false;
  }
  DrawingGeometrySnapshot current = CaptureGeometrySnapshot(st, "");
  doc.redoStack.push_back(std::move(current));
  const DrawingGeometrySnapshot& frame = doc.undoStack.back();
  const std::string desc = frame.description;
  RestoreGeometrySnapshot(st, frame);
  doc.undoStack.pop_back();
  BumpCadGpuCache(st);
  st.active = AppCommandState::Kind::None;
  st.selection.clear();
  st.selectedSurveyPointIndices.clear();
  log.push_back("UNDO" + (desc.empty() ? "." : ": " + desc));
  WriteUndoHistoryLogLine("UNDO: " + desc);
  return true;
}

bool DoRedo(AppCommandState& st, std::vector<std::string>& log) {
  const int idx = st.activeDrawingIdx;
  if (idx < 0 || static_cast<size_t>(idx) >= st.documents.size())
    return false;
  auto& doc = st.documents[static_cast<size_t>(idx)];
  if (doc.redoStack.empty()) {
    log.push_back("Nothing to redo.");
    return false;
  }
  DrawingGeometrySnapshot current = CaptureGeometrySnapshot(st, "");
  doc.undoStack.push_back(std::move(current));
  const DrawingGeometrySnapshot& frame = doc.redoStack.back();
  const std::string desc = frame.description;
  RestoreGeometrySnapshot(st, frame);
  doc.redoStack.pop_back();
  BumpCadGpuCache(st);
  st.active = AppCommandState::Kind::None;
  st.selection.clear();
  st.selectedSurveyPointIndices.clear();
  log.push_back("REDO" + (desc.empty() ? "." : ": " + desc));
  WriteUndoHistoryLogLine("REDO: " + desc);
  return true;
}

bool SubmitLineVertex(AppCommandState& st, float x, float y, std::vector<std::string>& log);

bool SubmitPolylineVertex(AppCommandState& st, float x, float y, std::vector<std::string>& log);

namespace {

constexpr float kPiAngF = 3.14159265358979323846f;

static float CadAngNormalizeMinusPiToPi(float a) {
  while (a > kPiAngF)
    a -= 2.f * kPiAngF;
  while (a < -kPiAngF)
    a += 2.f * kPiAngF;
  return a;
}

static bool CadDimAngularComputeFrame(const CadAnnotation& a, float* a1Out, float* a2Out, float* sweepOut, float* bisx,
                                      float* bisy, float* thetaInterior) {
  if (a.kind != CadAnnotation::Kind::DimAngular)
    return false;
  const float vx = a.dimAngVertexX, vy = a.dimAngVertexY;
  const float p1x = a.dimExt1X, p1y = a.dimExt1Y, p2x = a.dimExt2X, p2y = a.dimExt2Y;
  const float u1x = p1x - vx, u1y = p1y - vy;
  const float u2x = p2x - vx, u2y = p2y - vy;
  const float l1 = std::hypot(u1x, u1y);
  const float l2 = std::hypot(u2x, u2y);
  if (l1 < 1.e-8f || l2 < 1.e-8f)
    return false;
  const float n1x = u1x / l1, n1y = u1y / l1;
  const float n2x = u2x / l2, n2y = u2y / l2;
  const float dot = n1x * n2x + n1y * n2y;
  const float a1 = std::atan2(n1y, n1x);
  const float a2 = std::atan2(n2y, n2x);
  const float sweep = CadAngNormalizeMinusPiToPi(a2 - a1);
  const float theta = std::acos(std::clamp(dot, -1.f, 1.f));
  float bx = n1x + n2x;
  float by = n1y + n2y;
  const float bl = std::hypot(bx, by);
  if (bl > 1.e-6f) {
    bx /= bl;
    by /= bl;
  } else {
    bx = -n1y;
    by = n1x;
  }
  const float mid = a1 + 0.5f * sweep;
  const float mdx = std::cos(mid);
  const float mdy = std::sin(mid);
  if (bx * mdx + by * mdy < 0.f) {
    bx = -bx;
    by = -by;
  }
  *a1Out = a1;
  *a2Out = a2;
  *sweepOut = sweep;
  *bisx = bx;
  *bisy = by;
  *thetaInterior = theta;
  return theta > 1.e-7f;
}

static float CadDimAngularPickRadius(float vx, float vy, float bisx, float bisy, float pickx, float picky, float rMin,
                                     float rMax) {
  const float wx = pickx - vx;
  const float wy = picky - vy;
  float t = wx * bisx + wy * bisy;
  if (t <= 1.e-8f) {
    t = wx * -bisx + wy * -bisy;
    if (t <= 1.e-8f)
      t = rMin;
  }
  return std::clamp(t, rMin, rMax);
}

} // namespace

float CadOffsetEntityPickTolWorld(const AppCommandState& st);
void CommitDesignateAt(AppCommandState& st, float wx, float wy, bool isBoundary, std::vector<std::string>& log);

float RotateDeltaFromReferenceAndNewSegment(float refX1, float refY1, float refX2, float refY2,
                                             float newX1, float newY1, float newX2, float newY2) {
  const float thetaRef = std::atan2(refY2 - refY1, refX2 - refX1);
  const float thetaNew = std::atan2(newY2 - newY1, newX2 - newX1);
  return thetaNew - thetaRef;
}

bool ComputeCircumcircle(float ax, float ay, float bx, float by, float cx, float cy, float* ox, float* oy,
                         float* r) {
  const double dax = static_cast<double>(ax);
  const double day = static_cast<double>(ay);
  const double dbx = static_cast<double>(bx);
  const double dby = static_cast<double>(by);
  const double dcx = static_cast<double>(cx);
  const double dcy = static_cast<double>(cy);
  const double d = 2.0 * (dax * (dby - dcy) + dbx * (dcy - day) + dcx * (day - dby));
  if (std::fabs(d) < 1e-6)
    return false;
  const double a2 = dax * dax + day * day;
  const double b2 = dbx * dbx + dby * dby;
  const double c2 = dcx * dcx + dcy * dcy;
  const double ux = (a2 * (dby - dcy) + b2 * (dcy - day) + c2 * (day - dby)) / d;
  const double uy = (a2 * (dcx - dbx) + b2 * (dax - dcx) + c2 * (dbx - dax)) / d;
  const double dx = ux - dax;
  const double dy = uy - day;
  *ox = static_cast<float>(ux);
  *oy = static_cast<float>(uy);
  *r = static_cast<float>(std::sqrt(dx * dx + dy * dy));
  return true;
}

bool CadDimAlignedGeometry(const CadAnnotation& a, float* sx1, float* sy1, float* sx2, float* sy2, float* tx,
                           float* ty, float* nx, float* ny, float* measLen) {
  if (a.kind != CadAnnotation::Kind::DimAligned)
    return false;
  const float x1 = a.dimExt1X, y1 = a.dimExt1Y, x2 = a.dimExt2X, y2 = a.dimExt2Y;
  float vx = x2 - x1;
  float vy = y2 - y1;
  const float len = std::hypot(vx, vy);
  if (len < 1.e-8f)
    return false;
  vx /= len;
  vy /= len;
  const float n0x = -vy;
  const float n0y = vx;
  const float cmx = 0.5f * (x1 + x2);
  const float cmy = 0.5f * (y1 + y2);
  const float dmx = cmx + n0x * a.dimSignedOffset;
  const float dmy = cmy + n0y * a.dimSignedOffset;
  // Feet on the dimension line (parallel to chord through dmx,dmy): perpendicular from each extension point.
  const float t1 = (x1 - dmx) * vx + (y1 - dmy) * vy;
  const float t2 = (x2 - dmx) * vx + (y2 - dmy) * vy;
  *sx1 = dmx + vx * t1;
  *sy1 = dmy + vy * t1;
  *sx2 = dmx + vx * t2;
  *sy2 = dmy + vy * t2;
  *tx = vx;
  *ty = vy;
  *nx = n0x;
  *ny = n0y;
  *measLen = len;
  return true;
}

bool CadDimLinearGeometry(const CadAnnotation& a, float* sx1, float* sy1, float* sx2, float* sy2, float* tx,
                          float* ty, float* nx, float* ny, float* measLen) {
  if (a.kind != CadAnnotation::Kind::DimLinear)
    return false;
  const float x1 = a.dimExt1X, y1 = a.dimExt1Y, x2 = a.dimExt2X, y2 = a.dimExt2Y;
  const float cmx = 0.5f * (x1 + x2);
  const float cmy = 0.5f * (y1 + y2);
  if (!a.dimLinearVertical) {
    const float span = std::fabs(x2 - x1);
    if (span < 1.e-8f)
      return false;
    const float dmy = cmy + a.dimSignedOffset;
    *sx1 = x1;
    *sy1 = dmy;
    *sx2 = x2;
    *sy2 = dmy;
    *tx = (x2 >= x1) ? 1.f : -1.f;
    *ty = 0.f;
    *nx = 0.f;
    *ny = 1.f;
    *measLen = span;
  } else {
    const float span = std::fabs(y2 - y1);
    if (span < 1.e-8f)
      return false;
    const float dmx = cmx + a.dimSignedOffset;
    *sx1 = dmx;
    *sy1 = y1;
    *sx2 = dmx;
    *sy2 = y2;
    *tx = 0.f;
    *ty = (y2 >= y1) ? 1.f : -1.f;
    *nx = 1.f;
    *ny = 0.f;
    *measLen = span;
  }
  return true;
}

bool CadDimAnyGeometry(const CadAnnotation& a, float* sx1, float* sy1, float* sx2, float* sy2, float* tx, float* ty,
                       float* nx, float* ny, float* measLen) {
  if (a.kind == CadAnnotation::Kind::DimAligned)
    return CadDimAlignedGeometry(a, sx1, sy1, sx2, sy2, tx, ty, nx, ny, measLen);
  if (a.kind == CadAnnotation::Kind::DimLinear)
    return CadDimLinearGeometry(a, sx1, sy1, sx2, sy2, tx, ty, nx, ny, measLen);
  return false;
}

/// Place measurement text on the far side of the dimension line from the measured chord (CAD "above" the dim line).
static void CadDimAlignedPlaceTextBeyondDimLine(float chordMidX, float chordMidY, float dimMidX, float dimMidY,
                                                float n0x, float n0y, float hWorld, float* outIx, float* outIy) {
  const float dOff = (dimMidX - chordMidX) * n0x + (dimMidY - chordMidY) * n0y;
  float s = 1.f;
  if (dOff > 1.e-8f)
    s = 1.f;
  else if (dOff < -1.e-8f)
    s = -1.f;
  // Slightly more than half the annotation height so the label clears the dim line but still reads "just above" it.
  const float lift = 1.08f * hWorld;
  *outIx = dimMidX + n0x * (s * lift);
  *outIy = dimMidY + n0y * (s * lift);
}

void CadDimAlignedApplyInsFromLocalOffset(CadAnnotation* ann, float alongN, float alongT) {
  if (!ann)
    return;
  if (ann->kind == CadAnnotation::Kind::DimAngular) {
    const float vx = ann->dimAngVertexX, vy = ann->dimAngVertexY;
    float a1 = 0.f, a2 = 0.f, sweep = 0.f, theta = 0.f, bisx = 0.f, bisy = 0.f;
    if (!CadDimAngularComputeFrame(*ann, &a1, &a2, &sweep, &bisx, &bisy, &theta))
      return;
    const float R = std::max(ann->dimSignedOffset, 1.e-6f);
    const float mid = a1 + 0.5f * sweep;
    const float mx = vx + std::cos(mid) * R;
    const float my = vy + std::sin(mid) * R;
    const float tx = -std::sin(mid);
    const float ty = std::cos(mid);
    ann->insX = mx + bisx * alongN + tx * alongT;
    ann->insY = my + bisy * alongN + ty * alongT;
    ann->rotationRad = std::atan2(bisy, bisx);
    return;
  }
  if (ann->kind != CadAnnotation::Kind::DimAligned && ann->kind != CadAnnotation::Kind::DimLinear)
    return;
  float sx1 = 0.f, sy1 = 0.f, sx2 = 0.f, sy2 = 0.f, tx = 0.f, ty = 0.f, nx = 0.f, ny = 0.f, ml = 0.f;
  if (!CadDimAnyGeometry(*ann, &sx1, &sy1, &sx2, &sy2, &tx, &ty, &nx, &ny, &ml))
    return;
  const float dmx = 0.5f * (sx1 + sx2);
  const float dmy = 0.5f * (sy1 + sy2);
  ann->insX = dmx + nx * alongN + tx * alongT;
  ann->insY = dmy + ny * alongN + ty * alongT;
}

bool CadDimAlignedBuildDraft(const AppCommandState& st, float cursorWx, float cursorWy, CadAnnotation* out) {
  if (!out || st.active != AppCommandState::Kind::DimAligned ||
      st.dimPhase != AppCommandState::DimPhase::WaitDimLinePt)
    return false;
  const float x1 = st.dimE1x, y1 = st.dimE1y, x2 = st.dimE2x, y2 = st.dimE2y;
  const float lx = cursorWx, ly = cursorWy;
  float vx = x2 - x1;
  float vy = y2 - y1;
  const float len = std::hypot(vx, vy);
  if (len < 1.e-8f)
    return false;
  vx /= len;
  vy /= len;
  const float t1 = (x1 - lx) * vx + (y1 - ly) * vy;
  const float t2 = (x2 - lx) * vx + (y2 - ly) * vy;
  const float sx1 = lx + vx * t1;
  const float sy1 = ly + vy * t1;
  const float sx2 = lx + vx * t2;
  const float sy2 = ly + vy * t2;
  const float cmx = 0.5f * (x1 + x2);
  const float cmy = 0.5f * (y1 + y2);
  const float n0x = -vy;
  const float n0y = vx;
  const float dmx = 0.5f * (sx1 + sx2);
  const float dmy = 0.5f * (sy1 + sy2);
  const float dOff = (dmx - cmx) * n0x + (dmy - cmy) * n0y;
  CadAnnotation d{};
  d.kind = CadAnnotation::Kind::DimAligned;
  d.insZ = CadCommitElevation(st);  // the draft previews at the elevation it will commit to (REQ-058)
  d.dimExt1X = x1;
  d.dimExt1Y = y1;
  d.dimExt2X = x2;
  d.dimExt2Y = y2;
  d.dimSignedOffset = dOff;
  d.plottedHeightInches = std::max(st.defaultPlottedTextHeightInches * 0.85f, 1.e-6f);
  d.text = FormatLinear(static_cast<double>(len), st.displayLinearPrecision);
  d.rotationRad = std::atan2(vy, vx);
  const float hWorld = CadAnnotationHeightWorld(d, st.modelUnitsPerPlottedInch);
  CadDimAlignedPlaceTextBeyondDimLine(cmx, cmy, dmx, dmy, n0x, n0y, hWorld, &d.insX, &d.insY);
  *out = std::move(d);
  return true;
}

void CadDimLinearUpdateDraftOrientation(AppCommandState& st, float cursorWx, float cursorWy) {
  if (st.active != AppCommandState::Kind::DimLinear || st.dimPhase != AppCommandState::DimPhase::WaitDimLinePt)
    return;
  const float cmx = 0.5f * (st.dimE1x + st.dimE2x);
  const float cmy = 0.5f * (st.dimE1y + st.dimE2y);
  const float chord = std::hypot(st.dimE2x - st.dimE1x, st.dimE2y - st.dimE1y);
  const float unlockTol = 1.e-3f * std::max(1.f, chord);

  const float dxSpan = std::fabs(st.dimE2x - st.dimE1x);
  const float dySpan = std::fabs(st.dimE2y - st.dimE1y);
  const float spanTol =
      std::max(1e-8f, 1e-12f * std::max(std::max(std::fabs(st.dimE1x), std::fabs(st.dimE1y)),
                                        std::max(std::fabs(st.dimE2x), std::fabs(st.dimE2y))));
  const bool mustVertical = dxSpan <= spanTol && dySpan > spanTol;
  const bool mustHorizontal = dySpan <= spanTol && dxSpan > spanTol;
  if (mustVertical || mustHorizontal) {
    st.dimLinearDraftVertical = mustVertical;
    st.dimLinearOrientUserLock = false;
    return;
  }

  if (st.dimLinearOrientUserLock) {
    if (std::hypot(cursorWx - st.dimLinearLockCursorWx, cursorWy - st.dimLinearLockCursorWy) > unlockTol)
      st.dimLinearOrientUserLock = false;
  }
  if (!st.dimLinearOrientUserLock) {
    const float adx = std::fabs(cursorWx - cmx);
    const float ady = std::fabs(cursorWy - cmy);
    st.dimLinearDraftVertical = adx > ady;
  }
}

void CadDimLinearApplyHVHotkey(AppCommandState& st, bool vertical, std::vector<std::string>& log) {
  if (st.active != AppCommandState::Kind::DimLinear || st.dimPhase != AppCommandState::DimPhase::WaitDimLinePt)
    return;
  st.dimLinearDraftVertical = vertical;
  st.dimLinearOrientUserLock = true;
  st.dimLinearLockCursorWx = st.uiCursorWorldX;
  st.dimLinearLockCursorWy = st.uiCursorWorldY;
  log.push_back(vertical ? "DIMLINEAR — vertical span (V). Move crosshair to unlock orientation."
                         : "DIMLINEAR — horizontal span (H). Move crosshair to unlock orientation.");
  BumpCadGpuCache(st);
}

bool CadDimLinearBuildDraft(AppCommandState& st, float cursorWx, float cursorWy, CadAnnotation* out) {
  if (!out || st.active != AppCommandState::Kind::DimLinear ||
      st.dimPhase != AppCommandState::DimPhase::WaitDimLinePt)
    return false;
  CadDimLinearUpdateDraftOrientation(st, cursorWx, cursorWy);
  const float x1 = st.dimE1x, y1 = st.dimE1y, x2 = st.dimE2x, y2 = st.dimE2y;
  const float cmx = 0.5f * (x1 + x2);
  const float cmy = 0.5f * (y1 + y2);
  const bool vert = st.dimLinearDraftVertical;
  const float meas = vert ? std::fabs(y2 - y1) : std::fabs(x2 - x1);
  if (meas < 1.e-8f)
    return false;
  float dmx = cmx;
  float dmy = cmy;
  float n0x = 0.f;
  float n0y = 1.f;
  float dOff = 0.f;
  if (!vert) {
    dmy = cursorWy;
    dOff = dmy - cmy;
  } else {
    dmx = cursorWx;
    dOff = dmx - cmx;
    n0x = 1.f;
    n0y = 0.f;
  }
  CadAnnotation d{};
  d.kind = CadAnnotation::Kind::DimLinear;
  d.insZ = CadCommitElevation(st);  // the draft previews at the elevation it will commit to (REQ-058)
  d.dimExt1X = x1;
  d.dimExt1Y = y1;
  d.dimExt2X = x2;
  d.dimExt2Y = y2;
  d.dimSignedOffset = dOff;
  d.dimLinearVertical = vert;
  d.plottedHeightInches = std::max(st.defaultPlottedTextHeightInches * 0.85f, 1.e-6f);
  d.text = FormatLinear(static_cast<double>(meas), st.displayLinearPrecision);
  float tx = 0.f, ty = 0.f;
  if (!vert) {
    tx = (x2 >= x1) ? 1.f : -1.f;
    ty = 0.f;
  } else {
    tx = 0.f;
    ty = (y2 >= y1) ? 1.f : -1.f;
  }
  d.rotationRad = std::atan2(ty, tx);
  const float hWorld = CadAnnotationHeightWorld(d, st.modelUnitsPerPlottedInch);
  CadDimAlignedPlaceTextBeyondDimLine(cmx, cmy, dmx, dmy, n0x, n0y, hWorld, &d.insX, &d.insY);
  *out = std::move(d);
  return true;
}

std::string CadFormatAngleDegMinSecFromRad(float angleRad) {
  double deg = static_cast<double>(angleRad) * (180.0 / 3.14159265358979323846);
  if (deg < 0.0)
    deg = -deg;
  if (deg > 180.0)
    deg = 360.0 - deg;
  int id = static_cast<int>(std::floor(deg + 1.e-9));
  double minf = (deg - static_cast<double>(id)) * 60.0;
  if (minf < 0.0)
    minf = 0.0;
  int im = static_cast<int>(std::floor(minf + 1.e-9));
  double sec = (minf - static_cast<double>(im)) * 60.0;
  if (sec < 0.0)
    sec = 0.0;
  if (im >= 60) {
    im = 0;
    id = std::min(id + 1, 359);
  }
  if (sec >= 59.95) {
    sec = 0.0;
    ++im;
    if (im >= 60) {
      im = 0;
      ++id;
    }
  }
  char buf[96];
  std::snprintf(buf, sizeof(buf), "%d\xc2\xb0%d'%.1f\"", id, im, static_cast<double>(sec));
  return std::string(buf);
}

std::string CadFormatBearingCwNorthDegMinSec(float bearingDegClockwiseFromNorth) {
  double deg = std::fmod(static_cast<double>(bearingDegClockwiseFromNorth), 360.0);
  if (deg < 0.0)
    deg += 360.0;
  int id = static_cast<int>(std::floor(deg + 1.e-9));
  double minf = (deg - static_cast<double>(id)) * 60.0;
  if (minf < 0.0)
    minf = 0.0;
  int im = static_cast<int>(std::floor(minf + 1.e-9));
  double sec = (minf - static_cast<double>(im)) * 60.0;
  if (sec < 0.0)
    sec = 0.0;
  if (im >= 60) {
    im = 0;
    id = (id + 1) % 360;
  }
  if (sec >= 59.95) {
    sec = 0.0;
    ++im;
    if (im >= 60) {
      im = 0;
      id = (id + 1) % 360;
    }
  }
  char buf[96];
  std::snprintf(buf, sizeof(buf), "%d\xc2\xb0%d'%.1f\"", id, im, static_cast<double>(sec));
  return std::string(buf);
}

void CadDimRefreshMeasurementText(CadAnnotation* ann, int linearPrecision, const AngleDisplaySettings& angle) {
  if (!ann)
    return;
  if (ann->kind == CadAnnotation::Kind::DimAligned || ann->kind == CadAnnotation::Kind::DimLinear) {
    float sx1 = 0.f, sy1 = 0.f, sx2 = 0.f, sy2 = 0.f, tx = 0.f, ty = 0.f, nx = 0.f, ny = 0.f, ml = 0.f;
    if (!CadDimAnyGeometry(*ann, &sx1, &sy1, &sx2, &sy2, &tx, &ty, &nx, &ny, &ml))
      return;
    ann->text = FormatLinear(static_cast<double>(ml), linearPrecision);
  } else if (ann->kind == CadAnnotation::Kind::DimAngular) {
    float a1 = 0.f, a2 = 0.f, sweep = 0.f, theta = 0.f, bisx = 0.f, bisy = 0.f;
    if (!CadDimAngularComputeFrame(*ann, &a1, &a2, &sweep, &bisx, &bisy, &theta))
      return;
    ann->text = FormatSweptAngle(static_cast<double>(theta) * (180.0 / 3.14159265358979323846), angle);
  }
}

void CadDimAngularSyncTextPlacement(CadAnnotation* ann, float mupi) {
  if (!ann || ann->kind != CadAnnotation::Kind::DimAngular)
    return;
  const float vx = ann->dimAngVertexX, vy = ann->dimAngVertexY;
  float a1 = 0.f, a2 = 0.f, sweep = 0.f, theta = 0.f, bisx = 0.f, bisy = 0.f;
  if (!CadDimAngularComputeFrame(*ann, &a1, &a2, &sweep, &bisx, &bisy, &theta))
    return;
  const float R = std::max(ann->dimSignedOffset, 1.e-6f);
  const float mid = a1 + 0.5f * sweep;
  const float mx = vx + std::cos(mid) * R;
  const float my = vy + std::sin(mid) * R;
  const float hWorld = CadAnnotationHeightWorld(*ann, mupi);
  const float lift = 1.12f * hWorld;
  ann->insX = mx + bisx * lift;
  ann->insY = my + bisy * lift;
  ann->rotationRad = std::atan2(bisy, bisx);
}

bool CadDimAngularBuildDraft(const AppCommandState& st, float cursorWx, float cursorWy, CadAnnotation* out) {
  if (!out || st.active != AppCommandState::Kind::DimAngular ||
      st.dimAngularPhase != AppCommandState::DimAngularPhase::WaitArc)
    return false;
  const float vx = st.dimAngVx, vy = st.dimAngVy;
  CadAnnotation d{};
  d.kind = CadAnnotation::Kind::DimAngular;
  d.insZ = CadCommitElevation(st);  // the draft previews at the elevation it will commit to (REQ-058)
  d.dimAngVertexX = vx;
  d.dimAngVertexY = vy;
  d.dimExt1X = st.dimE1x;
  d.dimExt1Y = st.dimE1y;
  d.dimExt2X = st.dimE2x;
  d.dimExt2Y = st.dimE2y;
  float a1 = 0.f, a2 = 0.f, sweep = 0.f, theta = 0.f, bisx = 0.f, bisy = 0.f;
  if (!CadDimAngularComputeFrame(d, &a1, &a2, &sweep, &bisx, &bisy, &theta))
    return false;
  const float leg = std::min(std::hypot(d.dimExt1X - vx, d.dimExt1Y - vy), std::hypot(d.dimExt2X - vx, d.dimExt2Y - vy));
  const float rMax = std::max(1.e-4f, 0.92f * leg);
  const float rMin = std::max(1.e-4f, 0.02f * leg);
  const float R = CadDimAngularPickRadius(vx, vy, bisx, bisy, cursorWx, cursorWy, rMin, rMax);
  d.dimSignedOffset = R;
  d.plottedHeightInches = std::max(st.defaultPlottedTextHeightInches * 0.85f, 1.e-6f);
  d.text = FormatSweptAngle(static_cast<double>(theta) * (180.0 / 3.14159265358979323846), CadAngleDisplaySettings(st));
  CadDimAngularSyncTextPlacement(&d, st.modelUnitsPerPlottedInch);
  *out = std::move(d);
  return true;
}

void CadAnnotationRoughBounds(const CadAnnotation& a, float modelUnitsPerPlottedInch, float* outMnX, float* outMnY,
                              float* outMxX, float* outMxY) {
  const float h = CadAnnotationHeightWorld(a, modelUnitsPerPlottedInch);
  if (a.kind == CadAnnotation::Kind::Mtext) {
    *outMnX = std::min(a.boxMinX, a.boxMaxX);
    *outMxX = std::max(a.boxMinX, a.boxMaxX);
    *outMnY = std::min(a.boxMinY, a.boxMaxY);
    *outMxY = std::max(a.boxMinY, a.boxMaxY);
    return;
  }
  if (a.kind == CadAnnotation::Kind::DimAligned || a.kind == CadAnnotation::Kind::DimLinear) {
    float sx1 = 0.f, sy1 = 0.f, sx2 = 0.f, sy2 = 0.f, tx = 0.f, ty = 0.f, nx = 0.f, ny = 0.f, meas = 0.f;
    if (!CadDimAnyGeometry(a, &sx1, &sy1, &sx2, &sy2, &tx, &ty, &nx, &ny, &meas)) {
      *outMnX = *outMxX = a.insX;
      *outMnY = *outMxY = a.insY;
      return;
    }
    auto expandSeg = [&](float ax, float ay, float bx, float by) {
      *outMnX = std::min(*outMnX, std::min(ax, bx));
      *outMxX = std::max(*outMxX, std::max(ax, bx));
      *outMnY = std::min(*outMnY, std::min(ay, by));
      *outMxY = std::max(*outMxY, std::max(ay, by));
    };
    *outMnX = *outMxX = sx1;
    *outMnY = *outMxY = sy1;
    expandSeg(sx1, sy1, sx2, sy2);
    const float gap = std::clamp(0.012f * meas, 1.e-5f * meas, 0.12f * meas);
    const float over = std::clamp(0.02f * meas, 1.e-5f * meas, 0.1f * meas);
    const float leg1 = std::hypot(sx1 - a.dimExt1X, sy1 - a.dimExt1Y);
    const float u1 = leg1 > 1.e-8f ? gap / leg1 : 0.f;
    const float ex1 = a.dimExt1X + (sx1 - a.dimExt1X) * u1;
    const float ey1 = a.dimExt1Y + (sy1 - a.dimExt1Y) * u1;
    const float leg2 = std::hypot(sx2 - a.dimExt2X, sy2 - a.dimExt2Y);
    const float u2 = leg2 > 1.e-8f ? gap / leg2 : 0.f;
    const float ex2 = a.dimExt2X + (sx2 - a.dimExt2X) * u2;
    const float ey2 = a.dimExt2Y + (sy2 - a.dimExt2Y) * u2;
    expandSeg(ex1, ey1, sx1 + nx * over, sy1 + ny * over);
    expandSeg(ex2, ey2, sx2 + nx * over, sy2 + ny * over);
    const float charFactor = 0.55f;
    const float tw = std::max(h * charFactor * std::max(1.f, static_cast<float>(a.text.size())), h * 2.f);
    const float c = std::cos(a.rotationRad);
    const float s = std::sin(a.rotationRad);
    auto corner = [&](float lx, float ly, float* ox, float* oy) {
      const float rx = lx * c - ly * s;
      const float ry = lx * s + ly * c;
      *ox = a.insX + rx;
      *oy = a.insY + ry;
    };
    float xs[4]{};
    float ys[4]{};
    corner(0.f, 0.f, &xs[0], &ys[0]);
    corner(tw, 0.f, &xs[1], &ys[1]);
    corner(tw, -h, &xs[2], &ys[2]);
    corner(0.f, -h, &xs[3], &ys[3]);
    for (int i = 0; i < 4; ++i)
      expandSeg(xs[i], ys[i], xs[i], ys[i]);
    return;
  }
  const float charFactor = 0.55f;
  const float w = std::max(h * charFactor * std::max(1.f, static_cast<float>(a.text.size())), h * 2.f);
  const float c = std::cos(a.rotationRad);
  const float s = std::sin(a.rotationRad);
  auto corner = [&](float lx, float ly, float* ox, float* oy) {
    const float rx = lx * c - ly * s;
    const float ry = lx * s + ly * c;
    *ox = a.insX + rx;
    *oy = a.insY + ry;
  };
  float xs[4]{};
  float ys[4]{};
  corner(0.f, 0.f, &xs[0], &ys[0]);
  corner(w, 0.f, &xs[1], &ys[1]);
  corner(w, -h, &xs[2], &ys[2]);
  corner(0.f, -h, &xs[3], &ys[3]);
  *outMnX = *outMxX = xs[0];
  *outMnY = *outMxY = ys[0];
  for (int i = 1; i < 4; ++i) {
    *outMnX = std::min(*outMnX, xs[i]);
    *outMxX = std::max(*outMxX, xs[i]);
    *outMnY = std::min(*outMnY, ys[i]);
    *outMxY = std::max(*outMxY, ys[i]);
  }
}

int PickCadAnnotationAt(float wx, float wy, const AppCommandState& cmd, float orthoHalfHeightWorld,
                        float viewportHeightPx) {
  const float tol =
      CadSnap::WorldToleranceFromPixels(viewportHeightPx, orthoHalfHeightWorld, cmd.objectSnapAperturePx);
  const float tol2 = tol * tol;
  auto distSqSeg = [](float px, float py, float ax, float ay, float bx, float by) -> float {
    const float vx = bx - ax;
    const float vy = by - ay;
    const float len2 = vx * vx + vy * vy;
    if (len2 < 1.e-18f) {
      const float dx = px - ax;
      const float dy = py - ay;
      return dx * dx + dy * dy;
    }
    const float t = std::clamp(((px - ax) * vx + (py - ay) * vy) / len2, 0.f, 1.f);
    const float qx = ax + t * vx;
    const float qy = ay + t * vy;
    const float dx = px - qx;
    const float dy = py - qy;
    return dx * dx + dy * dy;
  };
  for (int i = static_cast<int>(cmd.cadAnnotations.size()) - 1; i >= 0; --i) {
    // REQ-084 (d): isolated-out text is not drawn, so it must not be pickable or hoverable.
    if (!cmd.hiddenEntityIds.empty() && static_cast<size_t>(i) < cmd.cadAnnotationAttrs.size() &&
        CadEntityIdHidden(&cmd.hiddenEntityIds, cmd.cadAnnotationAttrs[static_cast<size_t>(i)].id))
      continue;
    const CadAnnotation& a = cmd.cadAnnotations[static_cast<size_t>(i)];
    if (a.kind == CadAnnotation::Kind::DimAligned || a.kind == CadAnnotation::Kind::DimLinear) {
      float sx1 = 0.f, sy1 = 0.f, sx2 = 0.f, sy2 = 0.f, tx = 0.f, ty = 0.f, nx = 0.f, ny = 0.f, meas = 0.f;
      if (!CadDimAnyGeometry(a, &sx1, &sy1, &sx2, &sy2, &tx, &ty, &nx, &ny, &meas))
        continue;
      float best = tol2 + 1.f;
      auto upd = [&](float ax, float ay, float bx, float by) {
        best = std::min(best, distSqSeg(wx, wy, ax, ay, bx, by));
      };
      const float gap = std::clamp(0.012f * meas, 1.e-5f * meas, 0.12f * meas);
      const float over = std::clamp(0.02f * meas, 1.e-5f * meas, 0.1f * meas);
      const float leg1 = std::hypot(sx1 - a.dimExt1X, sy1 - a.dimExt1Y);
      const float u1 = leg1 > 1.e-8f ? gap / leg1 : 0.f;
      const float ex1 = a.dimExt1X + (sx1 - a.dimExt1X) * u1;
      const float ey1 = a.dimExt1Y + (sy1 - a.dimExt1Y) * u1;
      const float leg2 = std::hypot(sx2 - a.dimExt2X, sy2 - a.dimExt2Y);
      const float u2 = leg2 > 1.e-8f ? gap / leg2 : 0.f;
      const float ex2 = a.dimExt2X + (sx2 - a.dimExt2X) * u2;
      const float ey2 = a.dimExt2Y + (sy2 - a.dimExt2Y) * u2;
      upd(ex1, ey1, sx1 + nx * over, sy1 + ny * over);
      upd(ex2, ey2, sx2 + nx * over, sy2 + ny * over);
      upd(sx1, sy1, sx2, sy2);
      const float h = CadAnnotationHeightWorld(a, cmd.modelUnitsPerPlottedInch);
      const float charFactor = 0.55f;
      const float tw = std::max(h * charFactor * std::max(1.f, static_cast<float>(a.text.size())), h * 2.f);
      const float c = std::cos(a.rotationRad);
      const float s = std::sin(a.rotationRad);
      auto corner = [&](float lx, float ly, float* ox, float* oy) {
        const float rx = lx * c - ly * s;
        const float ry = lx * s + ly * c;
        *ox = a.insX + rx;
        *oy = a.insY + ry;
      };
      float xs[4]{};
      float ys[4]{};
      corner(0.f, 0.f, &xs[0], &ys[0]);
      corner(tw, 0.f, &xs[1], &ys[1]);
      corner(tw, -h, &xs[2], &ys[2]);
      corner(0.f, -h, &xs[3], &ys[3]);
      for (int e = 0; e < 4; ++e) {
        const int e2 = (e + 1) % 4;
        upd(xs[e], ys[e], xs[e2], ys[e2]);
      }
      if (best <= tol2)
        return i;
      continue;
    }
    float mnX = 0.f;
    float mnY = 0.f;
    float mxX = 0.f;
    float mxY = 0.f;
    CadAnnotationRoughBounds(a, cmd.modelUnitsPerPlottedInch, &mnX, &mnY, &mxX, &mxY);
    if (wx >= mnX - tol && wx <= mxX + tol && wy >= mnY - tol && wy <= mxY + tol)
      return i;
  }
  return -1;
}

static void ResetModifyRotateDraft(AppCommandState& st) {
  st.modifyPhase = AppCommandState::ModifyPhase::PickSelection;
  st.modifyBaseX = st.modifyBaseY = 0.f;
  st.scaleRefDist = 1.f;
  st.scalePhase = AppCommandState::ScalePhase::FactorPick;
  st.scaleRefP1X = st.scaleRefP1Y = 0.f;
  st.scaleNewLenP1X = st.scaleNewLenP1Y = 0.f;
  st.rotatePhase = AppCommandState::RotatePhase::PickSelection;
  st.rotateBaseX = st.rotateBaseY = 0.f;
  st.rotateRefX1 = st.rotateRefY1 = st.rotateRefX2 = st.rotateRefY2 = 0.f;
  st.rotateAnglePt1X = st.rotateAnglePt1Y = 0.f;
  st.rotateCopyMode = false;
}

static void ClearPendingViewportZoom(AppCommandState& st) {
  st.pendingZoomExtents = false;
  st.pendingZoomWindow = false;
}

namespace CadCmdGeom {

[[nodiscard]] float DistSqPointSegment(float px, float py, float ax, float ay, float bx, float by) {
  const float vx = bx - ax;
  const float vy = by - ay;
  const float len2 = vx * vx + vy * vy;
  if (len2 < 1.e-18f) {
    const float dx = px - ax;
    const float dy = py - ay;
    return dx * dx + dy * dy;
  }
  const float t = std::clamp(((px - ax) * vx + (py - ay) * vy) / len2, 0.f, 1.f);
  const float qx = ax + t * vx;
  const float qy = ay + t * vy;
  const float dx = px - qx;
  const float dy = py - qy;
  return dx * dx + dy * dy;
}

/// Minimum squared distance between finite segments AB and CD (dense sampling handles skew / parallel robustly).
[[nodiscard]] float MinDistSqSegSeg(float ax, float ay, float bx, float by, float cx, float cy, float dx, float dy) {
  float best = DistSqPointSegment(ax, ay, cx, cy, dx, dy);
  best = std::min(best, DistSqPointSegment(bx, by, cx, cy, dx, dy));
  best = std::min(best, DistSqPointSegment(cx, cy, ax, ay, bx, by));
  best = std::min(best, DistSqPointSegment(dx, dy, ax, ay, bx, by));
  constexpr int N = 28;
  for (int i = 1; i < N; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(N);
    const float px = ax + t * (bx - ax);
    const float py = ay + t * (by - ay);
    best = std::min(best, DistSqPointSegment(px, py, cx, cy, dx, dy));
    const float qx = cx + t * (dx - cx);
    const float qy = cy + t * (dy - cy);
    best = std::min(best, DistSqPointSegment(qx, qy, ax, ay, bx, by));
  }
  return best;
}

} // namespace CadCmdGeom

void ExecuteJoinSelection(AppCommandState& st, std::vector<std::string>& log);

namespace {

static EntityAttributes MakeNewEntityAttrs(const AppCommandState& st) {
  EntityAttributes a;
  a.layer = st.currentLayer.empty() ? std::string("0") : st.currentLayer;
  a.color = "ByLayer";
  a.linetype = "ByLayer";
  a.lineweightMm = -1.f;
  a.transparency = -1.f;
  return a;
}

bool ParseTwoFloats(std::string s, float* x, float* y) {
  for (char& c : s) {
    if (c == ',')
      c = ' ';
  }
  std::istringstream iss(s);
  if (!(iss >> *x))
    return false;
  if (!(iss >> *y))
    return false;
  return true;
}

// Double-precision sibling of ParseTwoFloats, for the typed-coordinate path (REQ-101). Same grammar,
// same comma-or-space separation; the only difference is that it does not throw away the decimals a
// state-plane easting needs before the document origin has been subtracted.
bool ParseTwoDoubles(std::string s, double* x, double* y) {
  for (char& c : s) {
    if (c == ',')
      c = ' ';
  }
  std::istringstream iss(s);
  if (!(iss >> *x))
    return false;
  if (!(iss >> *y))
    return false;
  return true;
}

bool ParseOneFloat(const std::string& s, float* v) {
  std::istringstream iss(StringUtil::trimCopy(s));
  return static_cast<bool>(iss >> *v);
}

struct CmdEntry {
  const char* primary;
  const char* aliases;
  const char* description;
};

const CmdEntry kRegistry[] = {
    {"line", "l", "Draw line segments"},
    {"circle", "c", "Draw a circle"},
    {"polyline", "pl", "Draw a connected polyline"},
    {"3dpoly", "3dp, 3dpolyline", "Draw a polyline whose vertices each carry their own elevation"},
    {"featureline", "fl", "Draw a feature line: named 3D linework with per-vertex elevations (REQ-087)"},
    {"featurelinelist", "fllist", "List every feature line and its vertices"},
    {"rect", "rectang, rectangle", "Draw a rectangle (two opposite corners)"},
    {"trimstate", "", "TRIM mode: 0 = draw a line to trim (default), 1 = pick cutting edges"},
    {"bench", "",
     "REQ-100 frame-budget benchmark: BENCH [segments] | BENCH SURFACE [points] | BENCH MESH [triangles]"},
    {"visualstyle", "vs, vscurrent", "Viewport visual style: 2D / HIDDEN / SHADED"},
    {"importmodel", "gltf, import3d", "Import a glTF/GLB 3D model as reference geometry"},
    {"elev", "ucs", "Elevation new geometry is drawn at (W = world Z 0)"},
    {"arc", "", "Draw an arc"},
    {"ellipse", "el", "Draw an ellipse"},
    {"hatch", "h, bhatch", "Fill a closed area (pick an internal point)"},
    {"text", "", "Place single-line text"},
    {"mtext", "mt", "Place multiline text"},
    {"dimaligned", "dal", "Aligned dimension"},
    {"dimlinear", "dli", "Linear dimension"},
    {"dimangular", "dan", "Angular dimension"},
    {"id", "", "Identify point coordinates"},
    {"inverse", "inv", "Inverse between two points"},
    {"surfelev", "se", "Surface elevation at a point; grade between two"},
    {"designatebreakline", "dbl", "Add a picked line/polyline as a surface breakline"},
    {"designateboundary", "dbd", "Add a picked closed polyline as a surface boundary (outer/hide/show)"},
    {"surfacecreate", "sfcreate", "Create a surface from point groups: SURFACECREATE <name>, <group>[, <group>…]"},
    {"surfacerename", "sfrename", "Rename a surface: SURFACERENAME <old>, <new>"},
    {"surfacedelete", "sfdelete", "Delete a surface: SURFACEDELETE <name>"},
    {"surfacerebuild", "sfrebuild", "Rebuild a surface now (all surfaces if no name): SURFACEREBUILD [<name>]"},
    {"surfacelist", "sflist", "List every surface and its full definition"},
    {"undesignate", "undes", "Remove one definition item: UNDESIGNATE <surface>, <BREAKLINE|BOUNDARY|POINTFILE>, <n>"},
    {"surfaceaddfile", "sfaddfile", "Link a point file into a surface: SURFACEADDFILE <surface>, <path>[, <layout>[, HEADER]]"},
    {"surfaceimportfile", "sfimportfile", "Import a linked point file into the drawing and break the link"},
    {"flelev", "", "Feature line elevations: FLELEV <n> [SET|GRADEAHEAD|GRADEBACK|RAISE|INSERT|DELETE …]"},
    {"flelevedit", "", "Open the feature line elevation editor: FLELEVEDIT [<n>]"},
    {"plotscale", "pscale", "Set the plot scale"},
    {"move", "m", "Move objects"},
    {"copy", "cp", "Copy objects"},
    {"rotate", "ro", "Rotate objects"},
    {"scale", "sc", "Scale objects"},
    {"delete", "del", "Erase objects"},
    {"join", "j", "Join collinear objects"},
    {"trim", "tr", "Trim objects to an edge"},
    {"offset", "o", "Offset at a distance"},
    {"zoomextents", "ze", "Zoom to drawing extents"},
    {"zoomwindow", "zw", "Zoom to a window"},
    {"pan", "p", "Pan the view (drag with the left mouse button)"},
    {"orbit", "3dorbit, 3do", "Free orbit the model view (drag with the left mouse button)"},
    {"isolateobjects", "isolate", "Hide everything except the selection"},
    {"hideobjects", "", "Hide the selected objects"},
    {"unisolateobjects", "unisolate", "Show every object hidden by isolation"},
    {"vpfreeze", "vpf", "Freeze the picked entities' layers in the current viewport"},
    {"vpthaw", "vpt", "Thaw the picked entities' layers in the current viewport"},
    {"createpoints", "crtpts", "Create survey points"},
    {"viewpoints", "vwpts", "View / edit survey points"},
    {"importpoints", "imppts", "Import survey points"},
    {"exportpoints", "exppts", "Export survey points"},
    {"select", "", "Build a selection set"},
    {"help", "", "Show command help"},
    {"regen", "re", "Regenerate the drawing"},
    {"layer", "la", "Open the Layer manager"},
    {"style", "st, ddstyle", "Text style manager: create / edit named text styles"},
    {"surfstyle", "ss", "Surface style editor: contours, triangles, border (REQ-070)"},
    {"extract", "", "Bake a surface's displayed contours into polylines: EXTRACT <surface>[, <layer>]"},
    {"units", "un, ddunits", "Drawing units: display precision & angle format"},
    {"pdfattach", "pa", "Attach a PDF underlay"},
    {"overkill",     "ok", "Remove duplicate geometry"},
    {"align",        "al", "Align objects to others"},
    {"quickselect",  "qs", "Select by object properties"},
    {"paste",        "", "Paste from clipboard"},
    {"pasteorig",    "po", "Paste at original coordinates"},
    {"mview",        "rectviewport, rectvp", "Rectangular paper-space viewport (two clicks)"},
    {"mspace",       "ms", "Edit the model through the selected viewport (floating model space)"},
    {"pspace",       "ps", "Return to paper space from a floating viewport"},
};

bool DispatchByPrimary(const std::string& primary, AppCommandState& st, std::vector<std::string>& log);

int FuzzySubsequenceScore(std::string_view query, std::string_view cand) {
  if (query.empty())
    return 0;
  size_t qi = 0;
  int score = 0;
  bool prevMatch = false;
  for (size_t i = 0; i < cand.size() && qi < query.size(); ++i) {
    char qc = static_cast<char>(std::tolower(static_cast<unsigned char>(query[qi])));
    char cc = static_cast<char>(std::tolower(static_cast<unsigned char>(cand[i])));
    if (qc == cc) {
      score += 10 + (prevMatch ? 5 : 0);
      prevMatch = true;
      ++qi;
    } else
      prevMatch = false;
  }
  if (qi != query.size())
    return -1;
  score += static_cast<int>(50 - cand.size());
  return score;
}

bool TryStrongFuzzyDispatch(const std::string& lineIn, AppCommandState& st, std::vector<std::string>& log) {
  std::string line = StringUtil::trimCopy(lineIn);
  if (line.empty())
    return false;
  std::vector<std::string> tokens;
  std::istringstream iss(line);
  std::string tok;
  while (iss >> tok)
    tokens.push_back(StringUtil::toLowerAsciiCopy(tok));
  if (tokens.empty())
    return false;

  std::unordered_map<std::string, int> bestPerPrimary;
  for (const std::string& t : tokens) {
    for (const CmdEntry& e : kRegistry) {
      const std::string prim = StringUtil::toLowerAsciiCopy(std::string(e.primary));
      auto considerCand = [&](const std::string& candLower) {
        const int sc = FuzzySubsequenceScore(t, candLower);
        if (sc < 0)
          return;
        auto it = bestPerPrimary.find(prim);
        if (it == bestPerPrimary.end() || sc > it->second)
          bestPerPrimary[prim] = sc;
      };
      considerCand(prim);
      if (e.aliases[0] == '\0')
        continue;
      std::istringstream als(std::string(e.aliases));
      std::string a;
      while (std::getline(als, a, ',')) {
        a = StringUtil::trimCopy(a);
        if (a.empty())
          continue;
        considerCand(StringUtil::toLowerAsciiCopy(a));
      }
    }
  }

  std::vector<std::pair<int, std::string>> ranked;
  ranked.reserve(bestPerPrimary.size());
  for (const auto& kv : bestPerPrimary)
    ranked.push_back({kv.second, kv.first});
  std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
    if (a.first != b.first)
      return a.first > b.first;
    return a.second < b.second;
  });
  if (ranked.empty())
    return false;

  const int bestSc = ranked[0].first;
  const int secondSc = ranked.size() > 1 ? ranked[1].first : -1;

  size_t maxTokLen = 0;
  for (const auto& t : tokens)
    maxTokLen = std::max(maxTokLen, t.size());

  const bool shortQuery = (tokens.size() == 1 && maxTokLen <= 2);
  const int minScore = shortQuery ? 72 : 45;
  const int margin = shortQuery ? 48 : 22;
  if (bestSc < minScore)
    return false;
  if (secondSc >= 0 && bestSc - secondSc < margin)
    return false;

  DispatchByPrimary(ranked[0].second, st, log);
  log.push_back("Matched \"" + ranked[0].second + "\" from fuzzy command match.");
  return true;
}

void ResetCircleDraft(AppCommandState& st) {
  st.circleStyle = AppCommandState::CircleStyle::CenterRadius;
  st.circlePhase = AppCommandState::CirclePhase::WaitCenterOrMode;
  st.circleCx = st.circleCy = 0.f;
  st.c3p1x = st.c3p1y = st.c3p2x = st.c3p2y = 0.f;
}

void ResetPolylineDraft(AppCommandState& st) {
  st.polylinePhase = AppCommandState::PolylinePhase::NeedFirstPoint;
  st.polyFirstX = st.polyFirstY = 0.f;
  st.polyDraftSegments = 0;
  st.polylineDraftVerts.clear();
  st.polylineDraft3d = false;  // REQ-085: the next POLYLINE is 2D unless 3DPOLY says otherwise
  st.polylineTypedZValid = false;
  st.polylineTypedZRelative = false;
  st.polylineTypedZ = 0.f;
}

/// REQ-087 / TASK-082. The feature-line draft had no reset function at all: StartFeatureLineCommand
/// cleared it by hand and ResetAllCadDraftTools did not touch it, so cancelling FEATURELINE left the
/// draft vertices behind — harmless only because nothing read them, which BUG-2's preview changes.
void ResetFeatureLineDraft(AppCommandState& st) {
  st.featureLineDraftVerts.clear();
  st.featureLineDraftElevPt.clear();
  st.featureLineDraftName.clear();
  st.featureLinePendingPoint = false;
  st.featureLinePendingX = st.featureLinePendingY = 0.f;
  st.featureLinePendingDefaultZ = 0.f;
  st.featureLineNextIsElevPoint = false;
}

void ResetArcDraft(AppCommandState& st) {
  st.arcPhase = AppCommandState::ArcPhase::WaitStart;
  st.arcAx = st.arcAy = st.arcBx = st.arcBy = 0.f;
}

void ResetEllipseDraft(AppCommandState& st) {
  st.ellPhase = AppCommandState::EllipsePhase::WaitCenter;
  st.ellCx = st.ellCy = 0.f;
  st.ellMajEx = st.ellMajEy = 0.f;
}

void ResetRectDraft(AppCommandState& st) {
  st.rectPhase = AppCommandState::RectPhase::WaitFirstCorner;
  st.rectX1 = st.rectY1 = 0.f;
}

void ResetTextCmdDraft(AppCommandState& st) {
  st.textPhase = AppCommandState::TextCmdPhase::WaitInsertion;
  st.textInsX = st.textInsY = 0.f;
  st.textHeightDraft = DefaultAnnotationTextHeightWorld(st);
  st.textRotDraft = 0.f;
}

void ResetMtextDraft(AppCommandState& st) {
  st.mtextPhase = AppCommandState::MtextPhase::WaitCorner1;
  st.mtxtX1 = st.mtxtY1 = st.mtxtX2 = st.mtxtY2 = 0.f;
  CloseMtextRichEditorUi(st);
}

void ResetDimDraft(AppCommandState& st) {
  st.dimPhase = AppCommandState::DimPhase::WaitExt1;
  st.dimE1x = st.dimE1y = st.dimE2x = st.dimE2y = 0.f;
  st.dimLinearDraftVertical = false;
  st.dimLinearOrientUserLock = false;
  st.dimLinearLockCursorWx = st.dimLinearLockCursorWy = 0.f;
}

void ResetDimAngularDraft(AppCommandState& st) {
  st.dimAngularPhase = AppCommandState::DimAngularPhase::WaitVertex;
  st.dimAngVx = st.dimAngVy = 0.f;
}

static void ResetSurveyInverseDraft(AppCommandState& st) {
  st.surveyInversePhase = AppCommandState::SurveyInversePhase::WaitFrom;
  st.surveyInverseFromX = st.surveyInverseFromY = 0.f;
}

static void ResetAllCadDraftTools(AppCommandState& st) {
  ResetCircleDraft(st);
  ResetPolylineDraft(st);
  ResetFeatureLineDraft(st);  // REQ-087: was missing, so a cancelled FEATURELINE kept its draft
  ResetArcDraft(st);
  ResetEllipseDraft(st);
  ResetRectDraft(st);
  ResetTextCmdDraft(st);
  ResetMtextDraft(st);
  ResetDimDraft(st);
  ResetDimAngularDraft(st);
  ResetSurveyInverseDraft(st);
  ClearDimGripInteraction(st);
  AbortMtextGripInteraction(st);
  // Release PDF draft resources if any command was running
  if (st.pdfDraftCache) {
    PdfDraftCache_Free(st.pdfDraftCache);
    st.pdfDraftCache = nullptr;
  }
  if (st.pdfAttachPreviewReady) {
    PdfAttach_ReleaseTexture(st.pdfAttachPreview);
    st.pdfAttachPreviewReady = false;
  }
  st.pdfAttachDialogOpen = false;
  st.pdfAttachPhase = AppCommandState::PdfAttachPhase::WaitDialog;
}

static void CommitDimAngularAt(AppCommandState& st, float wx, float wy, std::vector<std::string>& log) {
  CadAnnotation d{};
  d.kind = CadAnnotation::Kind::DimAngular;
  d.insZ = CadCommitElevation(st);  // lands on the active work plane (REQ-058), as TEXT does
  d.dimAngVertexX = st.dimAngVx;
  d.dimAngVertexY = st.dimAngVy;
  d.dimExt1X = st.dimE1x;
  d.dimExt1Y = st.dimE1y;
  d.dimExt2X = st.dimE2x;
  d.dimExt2Y = st.dimE2y;
  float a1 = 0.f, a2 = 0.f, sweep = 0.f, theta = 0.f, bisx = 0.f, bisy = 0.f;
  if (!CadDimAngularComputeFrame(d, &a1, &a2, &sweep, &bisx, &bisy, &theta)) {
    log.push_back("DIMANGULAR — points are degenerate or collinear.");
    return;
  }
  if (theta < 1e-5f) {
    log.push_back("DIMANGULAR — angle is zero (ray points coincide with vertex direction).");
    return;
  }
  const float vx = st.dimAngVx, vy = st.dimAngVy;
  const float leg = std::min(std::hypot(st.dimE1x - vx, st.dimE1y - vy), std::hypot(st.dimE2x - vx, st.dimE2y - vy));
  const float rMax = std::max(1.e-4f, 0.92f * leg);
  const float rMin = std::max(1.e-4f, 0.02f * leg);
  const float R = CadDimAngularPickRadius(vx, vy, bisx, bisy, wx, wy, rMin, rMax);
  d.dimSignedOffset = R;
  d.plottedHeightInches = st.defaultPlottedTextHeightInches * 0.85f;
  d.text = FormatSweptAngle(static_cast<double>(theta) * (180.0 / 3.14159265358979323846), CadAngleDisplaySettings(st));
  CadDimAngularSyncTextPlacement(&d, st.modelUnitsPerPlottedInch);
  EntityAttributes at = MakeNewEntityAttrs(st);
  at.color = "#e1b12c";
  PushUndoSnapshot(st, "DIMANGULAR");
  st.cadAnnotations.push_back(std::move(d));
  st.cadAnnotationAttrs.push_back(at);
  BumpCadGpuCache(st);
  ResetDimAngularDraft(st);
  ResetDimDraft(st);
  log.push_back("DIMANGULAR complete.");
  log.push_back("DIMANGULAR — vertex, two ray points, then arc position. ESC to exit.");
}

void CommitCircle(AppCommandState& st, float cx, float cy, float r, std::vector<std::string>& log) {
  // The radius is DERIVED from the distance between two points the user supplied, so it can overflow
  // float while both of those points are perfectly representable: a centre at state-plane magnitude
  // and a picked point far from it make dx*dx + dy*dy infinite, and sqrt(inf) is inf. Issue #59,
  // REQ-204 ("no coordinate is NaN or infinite") and REQ-201 (the refusal is reported, not swallowed).
  //
  // This must come BEFORE the radius-too-small test, which cannot do the job: `inf < 1e-5f` is false
  // and every comparison against NaN is false, so both slipped straight through it into the store.
  if (!std::isfinite(cx) || !std::isfinite(cy) || !std::isfinite(r)) {
    log.push_back("Circle rejected — the center or radius is not a finite number.");
    return;
  }
  if (r < 1e-5f) {
    log.push_back("Circle radius too small.");
    return;
  }
  PushUndoSnapshot(st, "Circle");
  st.userCirclesCxCyZR.push_back(cx);
  st.userCirclesCxCyZR.push_back(cy);
  // A new circle lands on the active work plane (REQ-058) — the ELEV command moves it.
  st.userCirclesCxCyZR.push_back(CadCommitElevation(st));
  st.userCirclesCxCyZR.push_back(r);
  st.userCircleAttrs.push_back(MakeNewEntityAttrs(st));
  BumpCadGpuCache(st);
  ResetCircleDraft(st);
  log.push_back("Circle complete.");
  log.push_back("CIRCLE — center + radius (or 3P). ESC to exit.");
}

bool ParseRadiusOrDiameter(const std::string& raw, float* radiusOut, std::vector<std::string>& log) {
  std::string s = StringUtil::trimCopy(raw);
  if (s.empty())
    return false;
  std::string low = StringUtil::toLowerAsciiCopy(s);
  if (!low.empty() && low[0] == 'd') {
    float dia = 0.f;
    if (!ParseOneFloat(low.substr(1), &dia)) {
      log.push_back("Expected diameter after D (e.g. D 40 or D40).");
      return false;
    }
    if (dia <= 0.f) {
      log.push_back("Diameter must be positive.");
      return false;
    }
    *radiusOut = dia * 0.5f;
    return true;
  }
  if (!ParseOneFloat(s, radiusOut)) {
    log.push_back("Expected a radius (number) or D + diameter.");
    return false;
  }
  if (*radiusOut <= 0.f) {
    log.push_back("Radius must be positive.");
    return false;
  }
  return true;
}

bool DispatchByPrimary(const std::string& primary, AppCommandState& st, std::vector<std::string>& log) {
  if (primary == "line") {
    StartLineCommand(st, log);
    return true;
  }
  if (primary == "circle") {
    StartCircleCommand(st, log);
    return true;
  }
  if (primary == "polyline") {
    StartPolylineCommand(st, log);
    return true;
  }
  if (primary == "3dpoly") {
    StartPolyline3dCommand(st, log);  // REQ-085
    return true;
  }
  if (primary == "rect") {
    StartRectCommand(st, log);
    return true;
  }
  if (primary == "elev" || primary == "ucs") {
    StartElevCommand(st, log);
    return true;
  }
  if (primary == "trimstate") {
    StartTrimStateCommand(st, log);
    return true;
  }
  if (primary == "arc") {
    StartArcCommand(st, log);
    return true;
  }
  if (primary == "ellipse") {
    StartEllipseCommand(st, log);
    return true;
  }
  if (primary == "text") {
    StartTextCommand(st, log);
    return true;
  }
  if (primary == "mtext") {
    StartMtextCommand(st, log);
    return true;
  }
  if (primary == "dimaligned") {
    StartDimAlignedCommand(st, log);
    return true;
  }
  if (primary == "dimlinear") {
    StartDimLinearCommand(st, log);
    return true;
  }
  if (primary == "dimangular") {
    StartDimAngularCommand(st, log);
    return true;
  }
  if (primary == "id") {
    StartIdPointCommand(st, log);
    return true;
  }
  if (primary == "inverse") {
    StartSurveyInverseCommand(st, log);
    return true;
  }
  if (primary == "extract") {
    ExecuteExtractCommand(st, std::string(), log);
    return true;
  }
  if (primary == "surfstyle") {
    ExecuteSurfStyleCommand(st, std::string(), log);
    return true;
  }
  if (primary == "surfelev") {
    StartSurfaceElevGradeCommand(st, log);
    return true;
  }
  if (primary == "plotscale") {
    log.push_back(
        "PLOTSCALE sets drawing units per plotted inch (e.g. 50 for civil 1\"=50'). Usage: PLOTSCALE 50");
    return true;
  }
  if (primary == "regen") {
    BumpCadGpuCache(st);
    log.push_back("REGEN — viewport caches refreshed.");
    return true;
  }
  if (primary == "layer") {
    SyncDrawingLayerTableWithGeometry(st);
    st.showLayerManagerWindow = true;
    log.push_back("LAYER — layer manager opened.");
    return true;
  }
  if (primary == "units") {
    st.showUnitsWindow = true;
    log.push_back("UNITS — drawing units dialog opened.");
    return true;
  }
  if (primary == "style") {
    TextStyles::EnsureStandard(st.textStyles);
    st.showTextStyleManagerWindow = true;
    log.push_back("STYLE — text style manager opened.");
    return true;
  }
  if (primary == "move") {
    StartMoveCommand(st, log);
    return true;
  }
  if (primary == "copy") {
    StartCopyCommand(st, log);
    return true;
  }
  if (primary == "rotate") {
    StartRotateCommand(st, log);
    return true;
  }
  if (primary == "scale") {
    StartScaleCommand(st, log);
    return true;
  }
  if (primary == "delete") {
    StartDeleteCommand(st, log);
    return true;
  }
  if (primary == "join") {
    StartJoinCommand(st, log);
    return true;
  }
  if (primary == "trim") {
    StartTrimCommand(st, log);
    return true;
  }
  if (primary == "offset") {
    StartOffsetCommand(st, log);
    return true;
  }
  if (primary == "hatch" || primary == "bhatch" || primary == "h") {
    StartHatchCommand(st, log);
    return true;
  }
  if (primary == "zoomextents") {
    StartZoomExtentsCommand(st, log);
    return true;
  }
  if (primary == "zoomwindow") {
    StartZoomWindowCommand(st, log);
    return true;
  }
  if (primary == "pan") {
    StartPanCommand(st, log);
    return true;
  }
  // REQ-084 (c): the shortcut menu's view + isolation entries are real commands, so they are also
  // typeable. AutoCAD's aliases are kept so muscle memory carries over.
  if (primary == "orbit" || primary == "3dorbit" || primary == "3do") {
    StartOrbitCommand(st, log);
    return true;
  }
  if (primary == "isolateobjects" || primary == "isolate") {
    IsolateSelectedObjects(st, log);
    return true;
  }
  if (primary == "hideobjects") {
    HideSelectedObjects(st, log);
    return true;
  }
  if (primary == "unisolateobjects" || primary == "unisolate") {
    EndObjectIsolation(st, log);
    return true;
  }
  if (primary == "vpfreeze") {
    StartVpFreezeCommand(st, log);
    return true;
  }
  if (primary == "vpthaw") {
    StartVpThawCommand(st, log);
    return true;
  }
  if (primary == "createpoints") {
    StartCreatePointsCommand(st, log);
    return true;
  }
  if (primary == "viewpoints") {
    StartViewPointsCommand(st, log);
    return true;
  }
  if (primary == "importpoints") {
    StartImportPointsCommand(st, log);
    return true;
  }
  if (primary == "exportpoints") {
    StartExportPointsCommand(st, log);
    return true;
  }
  if (primary == "pdfattach" || primary == "pdfatt") {
    StartPdfAttachCommand(st, log);
    return true;
  }
  if (primary == "select") {
    ClearPendingViewportZoom(st);
    ClearSelection(st);
    st.selBoxWaitingSecond = false;
    log.push_back("SELECT — click two corners for a window (default when no command is active).");
    return true;
  }
  if (primary == "help") {
    log.push_back(
        "LINE (L), POLYLINE (PL, CLOSE to close), ARC (3-point), ELLIPSE (center, axis end, ratio), TEXT, MTEXT, "
        "DIMALIGNED (DAL), DIMLINEAR (DLI), DIMANGULAR (DAN), ID, INVERSE (INV), CIRCLE (C), MOVE (M), COPY (CP), ROTATE (RO), SCALE (SC), DELETE (DEL), OFFSET (O), ZOOM (ZE/ZW), "
        "OVERKILL (OK), ALIGN (AL), PLOTSCALE "
        "(PSCALE), REGEN (RE), LAYER (LA). SURVEY: CRTPTS, VWPTS, IMPPTS, EXPPTS, INVERSE (INV). Idle: two-click box selects. ESC.");
    log.push_back(
        "LINE: @dx,dy from anchor; A or ANGLE alone then bearing on next line (blank Enter cancels); A<bearing> (+ "
        "optional +90) on one line; 2P (=AP) + two picks then Enter (or +90) locks bearing; distance (+/-) along ray; A "
        "clears when lock or 2P pick is active. Ortho: distance toward cursor.");
    log.push_back(
        "ROTATE: ° clockwise from north / DMS; R reference; then bearing or P. SCALE: after base, factor or pick from "
        "base; R / REFERENCE then two-point ref length, then new length (type or two picks). INVERSE: two points "
        "(World X=E, Y=N); logs dE, dN, distance, bearing in deg/min/sec and decimal deg CW from north. DELETE / ZW use unsnapped "
        "windows. TRIM "
        "matches Civil 3D: cutting edges, Enter, trim clicks. OFFSET: pick entity, distance + side or through-click "
        "(line/circle/arc). ZE fits geometry. OVERKILL: remove zero-length/duplicate/overlapping lines, merge "
        "collinear overlapping lines, remove arcs over circles, deduplicate circles and arcs — operates on entire drawing.");
    return true;
  }
  if (primary == "overkill") {
    ExecuteOverkill(st, log);
    return true;
  }
  if (primary == "align") {
    StartAlignCommand(st, log);
    return true;
  }
  if (primary == "quickselect" || primary == "qs") {
    StartQuickSelectCommand(st, log);
    return true;
  }
  if (primary == "paste") {
    StartPasteCommand(st, log);
    return true;
  }
  if (primary == "pasteorig" || primary == "po") {
    StartPasteOrigCommand(st, log);
    return true;
  }
  if (primary == "mview" || primary == "rectviewport" || primary == "rectvp") {
    StartPaperRectViewportCommand(st, log);
    return true;
  }
  if (primary == "mspace" || primary == "ms") {
    if (st.activeSpaceIndex != kModelSpaceIndex && st.selectedViewportIndex >= 0)
      EnterFloatingModelSpace(st, st.activeSpaceIndex, st.selectedViewportIndex, log);
    else
      log.push_back("MSPACE — select a viewport in a paper layout first.");
    return true;
  }
  if (primary == "pspace" || primary == "ps") {
    if (InFloatingModelSpace(st))
      ExitFloatingModelSpace(st, log);
    else
      log.push_back("PSPACE — not in a floating viewport.");
    return true;
  }
  return false;
}

bool HandleCircleTextInput(const std::string& lineIn, AppCommandState& st, std::vector<std::string>& log) {
  std::string line = StringUtil::trimCopy(lineIn);

  switch (st.circlePhase) {
  case AppCommandState::CirclePhase::WaitCenterOrMode: {
    if (StringUtil::toLowerAsciiCopy(line) == "3p") {
      st.circleStyle = AppCommandState::CircleStyle::ThreePoint;
      st.circlePhase = AppCommandState::CirclePhase::ThreeP_WaitP1;
      log.push_back("Three-point circle — specify first point.");
      return true;
    }
    float px = 0.f;
    float py = 0.f;
    if (!ParseStoragePoint(st, line, &px, &py, false, 0.f, 0.f))
      return false;
    st.circleCx = px;
    st.circleCy = py;
    st.circlePhase = AppCommandState::CirclePhase::WaitRadius;
    log.push_back("Center set — radius (click), type value, or D + diameter.");
    return true;
  }
  case AppCommandState::CirclePhase::WaitRadius: {
    float rad = 0.f;
    if (!ParseRadiusOrDiameter(line, &rad, log))
      return false;
    CommitCircle(st, st.circleCx, st.circleCy, rad, log);
    return true;
  }
  case AppCommandState::CirclePhase::ThreeP_WaitP1: {
    float px = 0.f;
    float py = 0.f;
    if (!ParseStoragePoint(st, line, &px, &py, false, 0.f, 0.f))
      return false;
    st.c3p1x = px;
    st.c3p1y = py;
    st.circlePhase = AppCommandState::CirclePhase::ThreeP_WaitP2;
    log.push_back("Second point:");
    return true;
  }
  case AppCommandState::CirclePhase::ThreeP_WaitP2: {
    float px = 0.f;
    float py = 0.f;
    if (!ParseStoragePoint(st, line, &px, &py, false, 0.f, 0.f))
      return false;
    st.c3p2x = px;
    st.c3p2y = py;
    st.circlePhase = AppCommandState::CirclePhase::ThreeP_WaitP3;
    log.push_back("Third point:");
    return true;
  }
  case AppCommandState::CirclePhase::ThreeP_WaitP3: {
    float px = 0.f;
    float py = 0.f;
    if (!ParseStoragePoint(st, line, &px, &py, false, 0.f, 0.f))
      return false;
    float ox = 0.f;
    float oy = 0.f;
    float r = 0.f;
    if (!ComputeCircumcircle(st.c3p1x, st.c3p1y, st.c3p2x, st.c3p2y, px, py, &ox, &oy, &r)) {
      log.push_back("Points are collinear — no circle.");
      return true;
    }
    CommitCircle(st, ox, oy, r, log);
    return true;
  }
  }
  return false;
}

bool SelectedEntityEqual(const SelectedEntity& a, const SelectedEntity& b) {
  return a.type == b.type && a.index == b.index;
}

void ArcRoughBounds(const CadArc& a, float* outMnX, float* outMxX, float* outMnY, float* outMxY, bool* any) {
  const int n = std::max(8, static_cast<int>(std::fabs(static_cast<double>(a.sweepRad)) / (3.14159265 / 16.0)) + 1);
  for (int i = 0; i <= n; ++i) {
    const float u = static_cast<float>(i) / static_cast<float>(n);
    const float t = a.startRad + a.sweepRad * u;
    const float x = a.cx + a.r * std::cos(t);
    const float y = a.cy + a.r * std::sin(t);
    if (!*any) {
      *outMnX = *outMxX = x;
      *outMnY = *outMxY = y;
      *any = true;
    } else {
      *outMnX = std::min(*outMnX, x);
      *outMxX = std::max(*outMxX, x);
      *outMnY = std::min(*outMnY, y);
      *outMxY = std::max(*outMxY, y);
    }
  }
}

void EllipseRoughBounds(const CadEllipse& e, float* outMnX, float* outMxX, float* outMnY, float* outMxY,
                        bool* any) {
  const float ma = std::hypot(e.majVx, e.majVy);
  if (ma < 1e-8f)
    return;
  const float ux = e.majVx / ma;
  const float uy = e.majVy / ma;
  const float px = -uy;
  const float py = ux;
  const float mb = ma * e.ratio;
  constexpr int n = 48;
  constexpr float twopi = 6.28318530718f;
  for (int i = 0; i <= n; ++i) {
    const float ang = twopi * static_cast<float>(i) / static_cast<float>(n);
    const float c = std::cos(ang);
    const float s = std::sin(ang);
    const float x = e.cx + ux * (ma * c) + px * (mb * s);
    const float y = e.cy + uy * (ma * c) + py * (mb * s);
    if (!*any) {
      *outMnX = *outMxX = x;
      *outMnY = *outMxY = y;
      *any = true;
    } else {
      *outMnX = std::min(*outMnX, x);
      *outMxX = std::max(*outMxX, x);
      *outMnY = std::min(*outMnY, y);
      *outMxY = std::max(*outMxY, y);
    }
  }
}

/// \param toTest Optional world->test-space mapping. Null keeps the historical plan behaviour
///        (world XY tested against a world rect). When the camera is orbited the caller supplies a
///        projection so vertices are tested in SCREEN space, where the drag rectangle actually is
///        (REQ-058). Per-vertex projection keeps the polyline test exact rather than conservative.
/// Takes the three arrays explicitly so FEATURE LINES box-select through the identical test
/// (REQ-087): same CSR shape, so a separate copy could only drift.
bool ChainHitsRect(const std::vector<int>& OFF, const std::vector<float>& V,
                   const std::vector<uint8_t>& CLOSED, int pi, float mnX, float mxX, float mnY,
                   float mxY, bool windowMode,
                   const std::function<void(float, float, float, float*, float*)>* toTest) {
  if (pi < 0 || static_cast<size_t>(pi + 1) >= OFF.size())
    return false;
  const int v0 = OFF[static_cast<size_t>(pi)];
  const int v1 = OFF[static_cast<size_t>(pi + 1)];
  if (v0 >= v1)
    return false;
  const bool closed =
      static_cast<size_t>(pi) < CLOSED.size() && CLOSED[static_cast<size_t>(pi)];
  const int nVert = v1 - v0;
  // Vertex in test space: identity in plan view, projected when the caller supplies a mapping.
  auto vert = [&](int vi, float* ox, float* oy) {
    const size_t k = static_cast<size_t>(vi) * 3;
    if (toTest && *toTest)
      (*toTest)(V[k], V[k + 1], V[k + 2], ox, oy);
    else {
      *ox = V[k];
      *oy = V[k + 1];
    }
  };
  if (windowMode) {
    for (int vi = v0; vi < v1; ++vi) {
      float x, y;
      vert(vi, &x, &y);
      if (!PointInsideClosedRect(x, y, mnX, mxX, mnY, mxY))
        return false;
    }
    return true;
  }
  for (int vi = v0; vi + 1 < v1; ++vi) {
    float x0, y0, x1, y1;
    vert(vi, &x0, &y0);
    vert(vi + 1, &x1, &y1);
    if (SegIntersectsAABB(x0, y0, x1, y1, mnX, mxX, mnY, mxY))
      return true;
  }
  if (closed && nVert >= 2) {
    float x0, y0, x1, y1;
    vert(v1 - 1, &x0, &y0);
    vert(v0, &x1, &y1);
    if (SegIntersectsAABB(x0, y0, x1, y1, mnX, mxX, mnY, mxY))
      return true;
  }
  return false;
}

void ComputeSelectionFromRect(AppCommandState& st, float xa, float ya, float xb, float yb, bool subtract,
                              bool windowMode, bool includeSurveyPoints, const Camera* cam, float vpW,
                              float vpH) {
  // Under an orbited camera a screen rectangle is NOT a world-axis-aligned rectangle — it projects
  // to a rotated quad — so testing world bounds against a world AABB selects the wrong objects
  // (REQ-058). When \p cam is supplied the whole test moves to SCREEN space: the drag corners and
  // every entity's bounds are projected, and the comparisons below are unchanged.
  //
  // Projecting an entity's world bounding box gives a conservative screen box (the bound of the
  // projection, not the projection of the bound), so crossing mode can occasionally include an
  // object whose box grazes the rect. Lines — by far the most-selected entity — are projected
  // endpoint-wise and stay exact. Recorded as a limitation rather than hidden.
  const bool proj = cam != nullptr && vpW > 0.f && vpH > 0.f;
  auto SP = [&](float wx, float wy, float wz, float* sx, float* sy) {
    if (!proj) {
      *sx = wx;
      *sy = wy;
      return;
    }
    cam->WorldToScreen(static_cast<double>(wx), static_cast<double>(wy), static_cast<double>(wz), vpW, vpH, sx, sy);
  };
  // A world box -> the screen box that bounds its projected corners.
  auto SPBox = [&](float bx0, float by0, float bx1, float by1, float* o0x, float* o0y, float* o1x, float* o1y) {
    if (!proj) {
      *o0x = bx0; *o0y = by0; *o1x = bx1; *o1y = by1;
      return;
    }
    // Inputs are copied BEFORE anything is written: callers pass the same variables as in and out
    // to transform a box in place, so writing first would clobber the corners still to be read.
    const float cxs[4] = {bx0, bx1, bx0, bx1};
    const float cys[4] = {by0, by0, by1, by1};
    float lo0x = 1e30f, lo0y = 1e30f, lo1x = -1e30f, lo1y = -1e30f;
    for (int i = 0; i < 4; ++i) {
      float ax, ay;
      SP(cxs[i], cys[i], 0.f, &ax, &ay);
      lo0x = std::min(lo0x, ax); lo1x = std::max(lo1x, ax);
      lo0y = std::min(lo0y, ay); lo1y = std::max(lo1y, ay);
    }
    *o0x = lo0x; *o0y = lo0y; *o1x = lo1x; *o1y = lo1y;
  };
  // Same mapping as \c SP, in the type PolylineHitsRect accepts so polylines test per-vertex.
  const std::function<void(float, float, float, float*, float*)> projFn =
      [&](float wx, float wy, float wz, float* sx, float* sy) { SP(wx, wy, wz, sx, sy); };
  if (proj) {  // the drag corners arrive in world coords; move them to screen too
    SP(xa, ya, 0.f, &xa, &ya);
    SP(xb, yb, 0.f, &xb, &yb);
  }
  float mnX = std::min(xa, xb);
  float mxX = std::max(xa, xb);
  float mnY = std::min(ya, yb);
  float mxY = std::max(ya, yb);
  const float expand = 1e-4f;
  if (mxX - mnX < expand) {
    mnX -= expand;
    mxX += expand;
  }
  if (mxY - mnY < expand) {
    mnY -= expand;
    mxY += expand;
  }

  std::vector<SelectedEntity> hits;
  std::vector<int> surveyHits;
  const auto& L = st.userLinesFlat;
  if (L.size() % 6 == 0) {
    for (size_t i = 0; i + 5 < L.size(); i += 6) {
      // Endpoint-wise projection keeps the line test EXACT in screen space (each endpoint carries
      // its own Z, so a sloped line is tested where it actually appears).
      float x0, y0, x1, y1;
      SP(L[i], L[i + 1], L[i + 2], &x0, &y0);
      SP(L[i + 3], L[i + 4], L[i + 5], &x1, &y1);
      bool hit = false;
      if (windowMode)
        hit = PointInsideClosedRect(x0, y0, mnX, mxX, mnY, mxY) &&
              PointInsideClosedRect(x1, y1, mnX, mxX, mnY, mxY);
      else
        hit = SegIntersectsAABB(x0, y0, x1, y1, mnX, mxX, mnY, mxY);
      if (hit) {
        SelectedEntity e{};
        e.type = SelectedEntity::Type::LineSeg;
        e.index = static_cast<int>(i / 6);
        hits.push_back(e);
      }
    }
  }
  const auto& C = st.userCirclesCxCyZR;
  if (C.size() % 4 == 0) {
    for (size_t ci = 0; ci + 3 < C.size(); ci += 4) {
      const float cx = C[ci];
      const float cy = C[ci + 1];
      const float r = C[ci + 3];
      bool hit = false;
      if (proj) {
        // A circle projects to an ellipse; test its bounding box (conservative, see the note above).
        float b0x, b0y, b1x, b1y;
        SPBox(cx - r, cy - r, cx + r, cy + r, &b0x, &b0y, &b1x, &b1y);
        hit = windowMode ? (b0x >= mnX && b1x <= mxX && b0y >= mnY && b1y <= mxY)
                         : !(b1x < mnX || b0x > mxX || b1y < mnY || b0y > mxY);
      } else if (windowMode)
        hit = CircleFullyInsideRect(cx, cy, r, mnX, mxX, mnY, mxY);
      else
        hit = CircleIntersectsAABB(cx, cy, r, mnX, mxX, mnY, mxY);
      if (hit) {
        SelectedEntity e{};
        e.type = SelectedEntity::Type::Circle;
        e.index = static_cast<int>(ci / 4);
        hits.push_back(e);
      }
    }
  }
  const size_t nAnn = st.cadAnnotations.size();
  for (size_t ai = 0; ai < nAnn; ++ai) {
    float amnX = 0.f;
    float amnY = 0.f;
    float amxX = 0.f;
    float amxY = 0.f;
    CadAnnotationRoughBounds(st.cadAnnotations[ai], st.modelUnitsPerPlottedInch, &amnX, &amnY, &amxX, &amxY);
    SPBox(amnX, amnY, amxX, amxY, &amnX, &amnY, &amxX, &amxY);  // screen space when orbited
    bool hit = false;
    if (windowMode)
      hit = amnX >= mnX && amxX <= mxX && amnY >= mnY && amxY <= mxY;
    else
      hit = !(amxX < mnX || amnX > mxX || amxY < mnY || amnY > mxY);
    if (hit) {
      SelectedEntity e{};
      e.type = SelectedEntity::Type::Annotation;
      e.index = static_cast<int>(ai);
      hits.push_back(e);
    }
  }
  for (size_t ai = 0; ai < st.userArcs.size(); ++ai) {
    float amnX = 0.f;
    float amxX = 0.f;
    float amnY = 0.f;
    float amxY = 0.f;
    bool any = false;
    ArcRoughBounds(st.userArcs[ai], &amnX, &amxX, &amnY, &amxY, &any);
    if (!any)
      continue;
    SPBox(amnX, amnY, amxX, amxY, &amnX, &amnY, &amxX, &amxY);  // screen space when orbited
    bool hit = false;
    if (windowMode)
      hit = amnX >= mnX && amxX <= mxX && amnY >= mnY && amxY <= mxY;
    else
      hit = !(amxX < mnX || amnX > mxX || amxY < mnY || amnY > mxY);
    if (hit) {
      SelectedEntity e{};
      e.type = SelectedEntity::Type::Arc;
      e.index = static_cast<int>(ai);
      hits.push_back(e);
    }
  }
  for (size_t ei = 0; ei < st.userEllipses.size(); ++ei) {
    float emnX = 0.f;
    float emxX = 0.f;
    float emnY = 0.f;
    float emxY = 0.f;
    bool any = false;
    EllipseRoughBounds(st.userEllipses[ei], &emnX, &emxX, &emnY, &emxY, &any);
    if (!any)
      continue;
    SPBox(emnX, emnY, emxX, emxY, &emnX, &emnY, &emxX, &emxY);  // screen space when orbited
    bool hit = false;
    if (windowMode)
      hit = emnX >= mnX && emxX <= mxX && emnY >= mnY && emxY <= mxY;
    else
      hit = !(emxX < mnX || emnX > mxX || emxY < mnY || emnY > mxY);
    if (hit) {
      SelectedEntity e{};
      e.type = SelectedEntity::Type::Ellipse;
      e.index = static_cast<int>(ei);
      hits.push_back(e);
    }
  }
  const int nPoly =
      static_cast<int>(st.userPolylineOffsets.size() > 0 ? st.userPolylineOffsets.size() - 1 : 0);
  for (int pi = 0; pi < nPoly; ++pi) {
    if (ChainHitsRect(st.userPolylineOffsets, st.userPolylineVerts, st.userPolylineClosed, pi, mnX, mxX,
                      mnY, mxY, windowMode, proj ? &projFn : nullptr)) {
      SelectedEntity e{};
      e.type = SelectedEntity::Type::Polyline;
      e.index = pi;
      hits.push_back(e);
    }
  }
  // Feature lines (REQ-087) — the same test, through the same `hits` list, so a box drag treats them
  // exactly as it treats polylines. ADR-034 names this and PickClosestCadEntity as the two selection
  // funnels; both now know about feature lines.
  {
    const int nFl =
        static_cast<int>(st.featureLineOffsets.size() > 0 ? st.featureLineOffsets.size() - 1 : 0);
    for (int fi = 0; fi < nFl; ++fi) {
      if (ChainHitsRect(st.featureLineOffsets, st.featureLineVerts, st.featureLineClosed, fi, mnX, mxX,
                        mnY, mxY, windowMode, proj ? &projFn : nullptr)) {
        SelectedEntity e{};
        e.type = SelectedEntity::Type::FeatureLine;
        e.index = fi;
        hits.push_back(e);
      }
    }
  }
  // TIN surfaces (REQ-068 / ADR-036 (b)) — hit-tested by their bounding box, matching filled regions
  // and meshes rather than the per-segment chain test above. A surface is selected as a whole, so
  // there is nothing a per-triangle test would decide differently: window mode requires the whole
  // surface inside the rect and crossing requires it to overlap, and both answers come from the
  // bounds. Walking 200k triangles to reach the same conclusion would only cost the frame.
  for (size_t si = 0; si < st.cadSurfaces.size(); ++si) {
    if (!SurfaceVisible(st, si))
      continue;
    const CadTin& t = *st.cadSurfaces[si].tin;
    float smnX = 0.f, smxX = 0.f, smnY = 0.f, smxY = 0.f;
    bool first = true;
    for (size_t v = 0; v + 2 < t.vertsXyz.size(); v += 3) {
      const float vx = t.vertsXyz[v], vy = t.vertsXyz[v + 1];
      if (first) {
        smnX = smxX = vx;
        smnY = smxY = vy;
        first = false;
      } else {
        smnX = std::min(smnX, vx);
        smxX = std::max(smxX, vx);
        smnY = std::min(smnY, vy);
        smxY = std::max(smxY, vy);
      }
    }
    if (first)
      continue;
    SPBox(smnX, smnY, smxX, smxY, &smnX, &smnY, &smxX, &smxY);  // screen space when orbited
    const bool hit = windowMode ? (smnX >= mnX && smxX <= mxX && smnY >= mnY && smxY <= mxY)
                                : !(smxX < mnX || smnX > mxX || smxY < mnY || smnY > mxY);
    if (hit) {
      SelectedEntity e{};
      e.type = SelectedEntity::Type::Surface;
      e.index = static_cast<int>(si);
      hits.push_back(e);
    }
  }
  // Filled regions (REQ-042): hit-test the outer-loop bounding box, matching annotations/arcs/PDF — window
  // requires the bbox fully inside; crossing requires the bbox to intersect the rect.
  for (size_t fi = 0; fi < st.cadFilledRegions.size(); ++fi) {
    float fmnX = 0.f, fmxX = 0.f, fmnY = 0.f, fmxY = 0.f;
    if (!hatchgeom::OuterBounds(st.cadFilledRegions[fi], &fmnX, &fmnY, &fmxX, &fmxY))
      continue;
    SPBox(fmnX, fmnY, fmxX, fmxY, &fmnX, &fmnY, &fmxX, &fmxY);  // screen space when orbited
    const bool hit = windowMode ? (fmnX >= mnX && fmxX <= mxX && fmnY >= mnY && fmxY <= mxY)
                                : !(fmxX < mnX || fmnX > mxX || fmxY < mnY || fmnY > mxY);
    if (hit) {
      SelectedEntity e{};
      e.type = SelectedEntity::Type::FilledRegion;
      e.index = static_cast<int>(fi);
      hits.push_back(e);
    }
  }
  if (includeSurveyPoints) {
    for (size_t si = 0; si < st.surveyPoints.size(); ++si) {
      const SurveyPoint& sp = st.surveyPoints[si];
      // The point's elevation IS its Z (REQ-057), so an orbited box-select tests it where it is
      // actually drawn rather than at its plan position.
      float spx, spy;
      SP(sp.easting, sp.northing, sp.elevation, &spx, &spy);
      const bool hitPoint = PointInsideClosedRect(spx, spy, mnX, mxX, mnY, mxY);
      bool hitLabel = false;
      const int lix = FindSurveyLabelAnnIndex(st, sp);
      if (lix >= 0) {
        const CadAnnotation& lab = st.cadAnnotations[static_cast<size_t>(lix)];
        if (lab.kind == CadAnnotation::Kind::Mtext && lab.surveyPointLabelForId == sp.id) {
          float amnX = 0.f;
          float amnY = 0.f;
          float amxX = 0.f;
          float amxY = 0.f;
          CadAnnotationRoughBounds(lab, st.modelUnitsPerPlottedInch, &amnX, &amnY, &amxX, &amxY);
          SPBox(amnX, amnY, amxX, amxY, &amnX, &amnY, &amxX, &amxY);  // screen space when orbited
          if (windowMode)
            hitLabel = amnX >= mnX && amxX <= mxX && amnY >= mnY && amxY <= mxY;
          else
            hitLabel = !(amxX < mnX || amnX > mxX || amxY < mnY || amnY > mxY);
        }
      }
      if (hitPoint || hitLabel)
        surveyHits.push_back(static_cast<int>(si));
    }
  }

  // PDF underlays: hit if the rotated bounding box intersects/is-contained-by the selection rect.
  constexpr float kPdfPi = 3.14159265f;
  for (int pi = 0; pi < static_cast<int>(st.pdfAttachments.size()); ++pi) {
    const PdfAttachment& patt = st.pdfAttachments[static_cast<size_t>(pi)];
    if (patt.pageWidthPts <= 0.f || patt.pageHeightPts <= 0.f)
      continue;
    const float W    = patt.pageWidthPts  * patt.scale;
    const float H    = patt.pageHeightPts * patt.scale;
    const float cosR = std::cos(patt.rotationDeg * kPdfPi / 180.f);
    const float sinR = std::sin(patt.rotationDeg * kPdfPi / 180.f);
    // Four corners in world space.
    float cx[4] = {
      patt.insertX,
      patt.insertX + W * cosR,
      patt.insertX + W * cosR - H * sinR,
      patt.insertX - H * sinR
    };
    float cy[4] = {
      patt.insertY,
      patt.insertY + W * sinR,
      patt.insertY + W * sinR + H * cosR,
      patt.insertY + H * cosR
    };
    float pmnX = cx[0], pmxX = cx[0], pmnY = cy[0], pmxY = cy[0];
    for (int k = 1; k < 4; ++k) {
      pmnX = std::min(pmnX, cx[k]); pmxX = std::max(pmxX, cx[k]);
      pmnY = std::min(pmnY, cy[k]); pmxY = std::max(pmxY, cy[k]);
    }
    bool hit = false;
    if (windowMode)
      hit = pmnX >= mnX && pmxX <= mxX && pmnY >= mnY && pmxY <= mxY;
    else
      hit = !(pmxX < mnX || pmnX > mxX || pmxY < mnY || pmnY > mxY);
    if (hit) {
      SelectedEntity e{};
      e.type  = SelectedEntity::Type::PdfUnderlay;
      e.index = pi;
      hits.push_back(e);
    }
  }

  // REQ-084 (d): drop isolated-out entities before the hits are applied, so a box drag across
  // where they used to be selects nothing. One filter here covers window and crossing, add and
  // subtract alike.
  if (!st.hiddenEntityIds.empty()) {
    hits.erase(std::remove_if(hits.begin(), hits.end(),
                              [&](const SelectedEntity& e) { return CadSelectedEntityHidden(st, e); }),
               hits.end());
  }

  if (subtract) {
    std::vector<SelectedEntity> kept;
    kept.reserve(st.selection.size());
    for (const auto& e : st.selection) {
      bool remove = false;
      for (const auto& h : hits) {
        if (SelectedEntityEqual(h, e)) {
          remove = true;
          break;
        }
      }
      if (!remove)
        kept.push_back(e);
    }
    st.selection = std::move(kept);
    auto& sv = st.selectedSurveyPointIndices;
    sv.erase(std::remove_if(sv.begin(), sv.end(),
                            [&](int ix) {
                              return std::find(surveyHits.begin(), surveyHits.end(), ix) != surveyHits.end();
                            }),
             sv.end());
  } else {
    for (const auto& h : hits) {
      bool has = false;
      for (const auto& e : st.selection) {
        if (SelectedEntityEqual(h, e)) {
          has = true;
          break;
        }
      }
      if (!has)
        st.selection.push_back(h);
    }
    for (int six : surveyHits) {
      auto& sv = st.selectedSurveyPointIndices;
      if (std::find(sv.begin(), sv.end(), six) == sv.end())
        sv.push_back(six);
    }
  }
}

void RotateAroundBase(float bx, float by, float rad, float* x, float* y) {
  const float c = std::cos(rad);
  const float s = std::sin(rad);
  float dx = *x - bx;
  float dy = *y - by;
  *x = bx + c * dx - s * dy;
  *y = by + s * dx + c * dy;
}

/// Rotates \c DimLinear extension points and offset; keeps \p ann.dimLinearVertical; refreshes \p ann.rotationRad.

static void RotateCadDimLinearAroundBase(float bx, float by, float rad, CadAnnotation* ann) {
  if (!ann || ann->kind != CadAnnotation::Kind::DimLinear)
    return;
  const float x1 = ann->dimExt1X, y1 = ann->dimExt1Y, x2 = ann->dimExt2X, y2 = ann->dimExt2Y;
  const float cmx = 0.5f * (x1 + x2);
  const float cmy = 0.5f * (y1 + y2);
  float dmx = cmx;
  float dmy = cmy;
  if (!ann->dimLinearVertical)
    dmy = cmy + ann->dimSignedOffset;
  else
    dmx = cmx + ann->dimSignedOffset;
  RotateAroundBase(bx, by, rad, &ann->dimExt1X, &ann->dimExt1Y);
  RotateAroundBase(bx, by, rad, &ann->dimExt2X, &ann->dimExt2Y);
  RotateAroundBase(bx, by, rad, &dmx, &dmy);
  const float ncmx = 0.5f * (ann->dimExt1X + ann->dimExt2X);
  const float ncmy = 0.5f * (ann->dimExt1Y + ann->dimExt2Y);
  if (!ann->dimLinearVertical)
    ann->dimSignedOffset = dmy - ncmy;
  else
    ann->dimSignedOffset = dmx - ncmx;
  float sx1 = 0.f, sy1 = 0.f, sx2 = 0.f, sy2 = 0.f, tx = 0.f, ty = 0.f, nx = 0.f, ny = 0.f, ml = 0.f;
  if (CadDimLinearGeometry(*ann, &sx1, &sy1, &sx2, &sy2, &tx, &ty, &nx, &ny, &ml))
    ann->rotationRad = std::atan2(ty, tx);
}

static float NormalizeAngleRadMinusPiToPi(float a) {
  constexpr float kPi = 3.14159265358979323846f;
  constexpr float kTwoPi = 2.f * kPi;
  while (a > kPi)
    a -= kTwoPi;
  while (a < -kPi)
    a += kTwoPi;
  return a;
}

static void ApplyTranslationToSelectedSurveyPoints(AppCommandState& st, float dx, float dy) {
  std::vector<int> ix = st.selectedSurveyPointIndices;
  std::sort(ix.begin(), ix.end());
  ix.erase(std::unique(ix.begin(), ix.end()), ix.end());
  for (int i : ix) {
    if (i >= 0 && static_cast<size_t>(i) < st.surveyPoints.size()) {
      st.surveyPoints[static_cast<size_t>(i)].easting += dx;
      st.surveyPoints[static_cast<size_t>(i)].northing += dy;
    }
  }
  for (int i : ix) {
    if (i >= 0 && static_cast<size_t>(i) < st.surveyPoints.size())
      RepositionSurveyLabelMtextForPoint(st, static_cast<size_t>(i));
  }
}

static void ApplyRotationToSelectedSurveyPoints(AppCommandState& st, float bx, float by, float rad) {
  std::vector<int> ix = st.selectedSurveyPointIndices;
  std::sort(ix.begin(), ix.end());
  ix.erase(std::unique(ix.begin(), ix.end()), ix.end());
  for (int i : ix) {
    if (i < 0 || static_cast<size_t>(i) >= st.surveyPoints.size())
      continue;
    float x = st.surveyPoints[static_cast<size_t>(i)].easting;
    float y = st.surveyPoints[static_cast<size_t>(i)].northing;
    RotateAroundBase(bx, by, rad, &x, &y);
    st.surveyPoints[static_cast<size_t>(i)].easting = x;
    st.surveyPoints[static_cast<size_t>(i)].northing = y;
  }
  for (int i : ix) {
    if (i >= 0 && static_cast<size_t>(i) < st.surveyPoints.size())
      RepositionSurveyLabelMtextForPoint(st, static_cast<size_t>(i));
  }
}

/// A duplicate is a NEW entity, so it takes the source's layer, colour and linetype but NOT its id.
/// Zero is the "unassigned" marker `MakeNewEntityAttrs` already uses; `EnsureEntityIds` sweeps on the
/// next `BumpCadGpuCache` and hands out a fresh one. Copying the id across instead — which both
/// duplicate paths did until 2026-08-20 — leaves two entities claiming one identity, and every
/// id-keyed lookup then resolves to whichever the sweep reaches first. A surface tracking a
/// breakline by id (REQ-069) would silently follow the wrong line. REQ-076; TASK-079 BUG-1.
static EntityAttributes DuplicatedEntityAttrs(EntityAttributes a) {
  a.id = 0;
  return a;
}

/// Visit each selected feature line once as (index, firstVertex, lastVertexExclusive).
///
/// The ranges are COLLECTED FIRST and visited afterwards, which is not incidental: two of the five
/// callers append to `featureLineOffsets` while they work, and walking the offsets table as it grows
/// would read from a reallocated buffer. Selections are small, so the copy costs nothing.
///
/// Five callers — translate, rotate, scale, and the two duplicate paths — plus the centroid and
/// extent helpers. That is the point of it: ADR-035 (g) names a missed case as this entity's whole
/// risk, and seven hand-copied CSR walks would be seven chances to miss one. REQ-087.
template <class Fn>
static void ForEachSelectedFeatureLine(const AppCommandState& st, Fn&& fn) {
  std::vector<std::array<int, 3>> ranges;
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::FeatureLine)
      continue;
    const int fi = e.index;
    if (fi < 0 || static_cast<size_t>(fi + 1) >= st.featureLineOffsets.size())
      continue;
    const int v0 = st.featureLineOffsets[static_cast<size_t>(fi)];
    const int v1 = st.featureLineOffsets[static_cast<size_t>(fi + 1)];
    if (v1 <= v0 || static_cast<size_t>(v1) * 3 > st.featureLineVerts.size())
      continue;
    ranges.push_back({fi, v0, v1});
  }
  for (const std::array<int, 3>& r : ranges)
    fn(r[0], r[1], r[2]);
}

/// Apply \p xform to the plan position of every vertex of every selected feature line. Elevation is
/// deliberately not offered: MOVE, ROTATE and SCALE are all plan operations here, matching the
/// polyline behaviour they sit beside, and a transform that could silently alter Z would break the
/// surface a feature line feeds (REQ-069) without touching its plan geometry.
template <class Xf>
static void TransformSelectedFeatureLinesInPlace(AppCommandState& st, Xf&& xform) {
  ForEachSelectedFeatureLine(st, [&](int /*fi*/, int v0, int v1) {
    for (int vi = v0; vi < v1; ++vi) {
      const size_t b = static_cast<size_t>(vi) * 3;
      xform(&st.featureLineVerts[b], &st.featureLineVerts[b + 1]);
    }
  });
}

/// Append a copy of feature line \p fi, with \p xform applied to each vertex's plan position.
///
/// Everything that makes the line what it is carries across — elevations, the elevation-point flags,
/// the closed flag, the name — and the id does not, because the copy is a new entity (REQ-076).
/// The vertex range is validated BEFORE the first push so this cannot bail out half way and leave
/// `featureLineVerts` longer than `featureLineOffsets` says it is; `EraseFeatureLineByIndex` already
/// showed that the flags and the vertices are cut on different strides and that a length mismatch
/// between them is completely silent.
template <class Xf>
static void AppendFeatureLineCopy(AppCommandState& st, int fi, int v0, int v1, Xf&& xform) {
  const int nv = v1 - v0;
  if (nv < 2 || static_cast<size_t>(v1) * 3 > st.featureLineVerts.size())
    return;
  if (st.featureLineOffsets.empty())
    st.featureLineOffsets.push_back(0);
  const int baseVert = st.featureLineOffsets.back();
  for (int vi = v0; vi < v1; ++vi) {
    const size_t b = static_cast<size_t>(vi) * 3;
    float x = st.featureLineVerts[b];
    float y = st.featureLineVerts[b + 1];
    const float z = st.featureLineVerts[b + 2];
    xform(&x, &y);
    st.featureLineVerts.push_back(x);
    st.featureLineVerts.push_back(y);
    st.featureLineVerts.push_back(z);
    st.featureLineElevPt.push_back(static_cast<size_t>(vi) < st.featureLineElevPt.size()
                                       ? st.featureLineElevPt[static_cast<size_t>(vi)]
                                       : static_cast<uint8_t>(0));
  }
  st.featureLineOffsets.push_back(baseVert + nv);
  st.featureLineClosed.push_back(static_cast<size_t>(fi) < st.featureLineClosed.size()
                                     ? st.featureLineClosed[static_cast<size_t>(fi)]
                                     : static_cast<uint8_t>(0));
  st.featureLineInfo.push_back(static_cast<size_t>(fi) < st.featureLineInfo.size()
                                   ? st.featureLineInfo[static_cast<size_t>(fi)]
                                   : CadFeatureLineInfo{});
  st.featureLineAttrs.push_back(DuplicatedEntityAttrs(
      static_cast<size_t>(fi) < st.featureLineAttrs.size()
          ? st.featureLineAttrs[static_cast<size_t>(fi)]
          : MakeNewEntityAttrs(st)));
}

static void DuplicateCadSelectionTranslated(AppCommandState& st, float dx, float dy) {
  const size_t polyVertsBefore = st.userPolylineVerts.size();
  const size_t featureVertsBefore = st.featureLineVerts.size();
  std::vector<float> newLines;
  std::vector<float> newCircles;
  std::vector<EntityAttributes> newLineAttrs;
  std::vector<EntityAttributes> newCircleAttrs;
  std::vector<CadAnnotation> newAnn;
  std::vector<EntityAttributes> newAnnAttrs;
  std::vector<CadArc> newArcs;
  std::vector<EntityAttributes> newArcAttrs;
  std::vector<CadEllipse> newEll;
  std::vector<EntityAttributes> newEllAttrs;
  std::vector<CadFilledRegion> newFills;
  std::vector<EntityAttributes> newFillAttrs;

  for (const auto& e : st.selection) {
    if (e.type == SelectedEntity::Type::FilledRegion) {
      const size_t fk = static_cast<size_t>(e.index);
      if (fk < st.cadFilledRegions.size()) {
        CadFilledRegion fr = st.cadFilledRegions[fk];
        hatchgeom::Translate(fr, dx, dy);
        newFills.push_back(std::move(fr));
        newFillAttrs.push_back(DuplicatedEntityAttrs(
            fk < st.cadFilledRegionAttrs.size() ? st.cadFilledRegionAttrs[fk] : EntityAttributes{}));
      }
    } else if (e.type == SelectedEntity::Type::LineSeg) {
      size_t k = static_cast<size_t>(e.index) * 6;
      if (k + 5 < st.userLinesFlat.size()) {
        for (int j = 0; j < 6; ++j)
          newLines.push_back(st.userLinesFlat[k + static_cast<size_t>(j)]);
        newLines[newLines.size() - 6] += dx;
        newLines[newLines.size() - 5] += dy;
        newLines[newLines.size() - 3] += dx;
        newLines[newLines.size() - 2] += dy;
        EntityAttributes a{};
        if (e.index >= 0 && static_cast<size_t>(e.index) < st.userLineAttrs.size())
          a = st.userLineAttrs[static_cast<size_t>(e.index)];
        newLineAttrs.push_back(DuplicatedEntityAttrs(a));
      }
    } else if (e.type == SelectedEntity::Type::Circle) {
      size_t k = static_cast<size_t>(e.index) * 4;
      if (k + 3 < st.userCirclesCxCyZR.size()) {
        newCircles.push_back(st.userCirclesCxCyZR[k] + dx);
        newCircles.push_back(st.userCirclesCxCyZR[k + 1] + dy);
        newCircles.push_back(st.userCirclesCxCyZR[k + 2]);  // z
        newCircles.push_back(st.userCirclesCxCyZR[k + 3]);  // r
        EntityAttributes a{};
        if (e.index >= 0 && static_cast<size_t>(e.index) < st.userCircleAttrs.size())
          a = st.userCircleAttrs[static_cast<size_t>(e.index)];
        newCircleAttrs.push_back(DuplicatedEntityAttrs(a));
      }
    } else if (e.type == SelectedEntity::Type::Annotation) {
      const size_t k = static_cast<size_t>(e.index);
      if (k < st.cadAnnotations.size()) {
        CadAnnotation c = st.cadAnnotations[k];
        c.surveyPointLabelForId = -1;
        c.insX += dx;
        c.insY += dy;
        if (c.kind == CadAnnotation::Kind::Mtext) {
          c.boxMinX += dx;
          c.boxMinY += dy;
          c.boxMaxX += dx;
          c.boxMaxY += dy;
        } else if (c.kind == CadAnnotation::Kind::DimAligned || c.kind == CadAnnotation::Kind::DimLinear) {
          c.dimExt1X += dx;
          c.dimExt1Y += dy;
          c.dimExt2X += dx;
          c.dimExt2Y += dy;
        }
        newAnn.push_back(std::move(c));
        EntityAttributes a{};
        if (k < st.cadAnnotationAttrs.size())
          a = st.cadAnnotationAttrs[k];
        newAnnAttrs.push_back(DuplicatedEntityAttrs(a));
      }
    } else if (e.type == SelectedEntity::Type::Arc) {
      const size_t k = static_cast<size_t>(e.index);
      if (k < st.userArcs.size()) {
        CadArc a = st.userArcs[k];
        a.cx += dx;
        a.cy += dy;
        newArcs.push_back(a);
        EntityAttributes at{};
        if (k < st.userArcAttrs.size())
          at = st.userArcAttrs[k];
        newArcAttrs.push_back(DuplicatedEntityAttrs(at));
      }
    } else if (e.type == SelectedEntity::Type::Ellipse) {
      const size_t k = static_cast<size_t>(e.index);
      if (k < st.userEllipses.size()) {
        CadEllipse el = st.userEllipses[k];
        el.cx += dx;
        el.cy += dy;
        newEll.push_back(el);
        EntityAttributes at{};
        if (k < st.userEllAttrs.size())
          at = st.userEllAttrs[k];
        newEllAttrs.push_back(DuplicatedEntityAttrs(at));
      }
    } else if (e.type == SelectedEntity::Type::Polyline) {
      const int pi = e.index;
      if (pi < 0 || static_cast<size_t>(pi + 1) >= st.userPolylineOffsets.size())
        continue;
      const int v0 = st.userPolylineOffsets[static_cast<size_t>(pi)];
      const int v1 = st.userPolylineOffsets[static_cast<size_t>(pi + 1)];
      const int nv = v1 - v0;
      if (nv < 2)
        continue;
      if (st.userPolylineOffsets.empty())
        st.userPolylineOffsets.push_back(0);
      const int baseVert = st.userPolylineOffsets.back();
      for (int vi = v0; vi < v1; ++vi) {
        st.userPolylineVerts.push_back(st.userPolylineVerts[static_cast<size_t>(vi * 3 + 0)] + dx);
        st.userPolylineVerts.push_back(st.userPolylineVerts[static_cast<size_t>(vi * 3 + 1)] + dy);
        st.userPolylineVerts.push_back(st.userPolylineVerts[static_cast<size_t>(vi * 3 + 2)]);
      }
      st.userPolylineOffsets.push_back(baseVert + nv);
      uint8_t cl = 0;
      if (static_cast<size_t>(pi) < st.userPolylineClosed.size())
        cl = st.userPolylineClosed[static_cast<size_t>(pi)];
      st.userPolylineClosed.push_back(cl);
      EntityAttributes at{};
      if (static_cast<size_t>(pi) < st.userPolylineAttrs.size())
        at = st.userPolylineAttrs[static_cast<size_t>(pi)];
      st.userPolylineAttrs.push_back(DuplicatedEntityAttrs(at));
    }
  }
  st.userLinesFlat.insert(st.userLinesFlat.end(), newLines.begin(), newLines.end());
  st.userCirclesCxCyZR.insert(st.userCirclesCxCyZR.end(), newCircles.begin(), newCircles.end());
  st.userLineAttrs.insert(st.userLineAttrs.end(), newLineAttrs.begin(), newLineAttrs.end());
  st.userCircleAttrs.insert(st.userCircleAttrs.end(), newCircleAttrs.begin(), newCircleAttrs.end());
  st.cadAnnotations.insert(st.cadAnnotations.end(), newAnn.begin(), newAnn.end());
  st.cadAnnotationAttrs.insert(st.cadAnnotationAttrs.end(), newAnnAttrs.begin(), newAnnAttrs.end());
  st.userArcs.insert(st.userArcs.end(), newArcs.begin(), newArcs.end());
  st.userArcAttrs.insert(st.userArcAttrs.end(), newArcAttrs.begin(), newArcAttrs.end());
  st.userEllipses.insert(st.userEllipses.end(), newEll.begin(), newEll.end());
  st.userEllAttrs.insert(st.userEllAttrs.end(), newEllAttrs.begin(), newEllAttrs.end());
  st.cadFilledRegions.insert(st.cadFilledRegions.end(), newFills.begin(), newFills.end());
  st.cadFilledRegionAttrs.insert(st.cadFilledRegionAttrs.end(), newFillAttrs.begin(), newFillAttrs.end());

  // Feature lines (REQ-087). Appended after the loop above rather than inside it only for
  // readability — ForEachSelectedFeatureLine snapshots its ranges, so growing the store mid-walk is
  // safe either way.
  ForEachSelectedFeatureLine(st, [&](int fi, int v0, int v1) {
    AppendFeatureLineCopy(st, fi, v0, v1, [&](float* x, float* y) {
      *x += dx;
      *y += dy;
    });
  });

  if (!newLines.empty() || !newCircles.empty() || !newAnn.empty() || !newArcs.empty() || !newEll.empty() ||
      !newFills.empty() || st.userPolylineVerts.size() != polyVertsBefore ||
      st.featureLineVerts.size() != featureVertsBefore)
    BumpCadGpuCache(st);
}

// Paste clipboard geometry into MODEL space with (dx, dy) applied. Builds st.selection from the pasted
// entities (REQ-038 #5). Caller pushes the undo snapshot.
static void CommitPasteIntoModel(AppCommandState& st, float dx, float dy) {
  const CadClipboard& cb = st.clipboard;
  using ST = SelectedEntity::Type;
  st.selection.clear();

  for (size_t i = 0; i + 5 < cb.lines.size() + 1; i += 6) {
    st.userLinesFlat.push_back(cb.lines[i + 0] + dx);
    st.userLinesFlat.push_back(cb.lines[i + 1] + dy);
    st.userLinesFlat.push_back(cb.lines[i + 2]);
    st.userLinesFlat.push_back(cb.lines[i + 3] + dx);
    st.userLinesFlat.push_back(cb.lines[i + 4] + dy);
    st.userLinesFlat.push_back(cb.lines[i + 5]);
    st.userLineAttrs.push_back(cb.lineAttrs[i / 6]);
    st.selection.push_back({ST::LineSeg, static_cast<int>(st.userLineAttrs.size()) - 1});
  }
  // Clipboard → model: both are cx,cy,z,r, so Z carries through a paste unchanged (REQ-057).
  for (size_t i = 0; i + 3 < cb.circlesCxCyZR.size() + 1; i += 4) {
    st.userCirclesCxCyZR.push_back(cb.circlesCxCyZR[i + 0] + dx);
    st.userCirclesCxCyZR.push_back(cb.circlesCxCyZR[i + 1] + dy);
    st.userCirclesCxCyZR.push_back(cb.circlesCxCyZR[i + 2]);
    st.userCirclesCxCyZR.push_back(cb.circlesCxCyZR[i + 3]);
    st.userCircleAttrs.push_back(cb.circleAttrs[i / 4]);
    st.selection.push_back({ST::Circle, static_cast<int>(st.userCircleAttrs.size()) - 1});
  }
  for (size_t i = 0; i < cb.arcs.size(); ++i) {
    CadArc a = cb.arcs[i];
    a.cx += dx;
    a.cy += dy;
    st.userArcs.push_back(a);
    st.userArcAttrs.push_back(cb.arcAttrs[i]);
    st.selection.push_back({ST::Arc, static_cast<int>(st.userArcs.size()) - 1});
  }
  for (size_t i = 0; i < cb.ellipses.size(); ++i) {
    CadEllipse el = cb.ellipses[i];
    el.cx += dx;
    el.cy += dy;
    st.userEllipses.push_back(el);
    st.userEllAttrs.push_back(cb.ellAttrs[i]);
    st.selection.push_back({ST::Ellipse, static_cast<int>(st.userEllipses.size()) - 1});
  }
  const int nPoly = static_cast<int>(cb.polyOffsets.size()) - 1;
  for (int pi = 0; pi < nPoly; ++pi) {
    const int v0 = cb.polyOffsets[static_cast<size_t>(pi)];
    const int v1 = cb.polyOffsets[static_cast<size_t>(pi + 1)];
    const int nv = v1 - v0;
    if (nv < 2)
      continue;
    const int baseVert = st.userPolylineOffsets.empty() ? 0 : st.userPolylineOffsets.back();
    for (int vi = v0; vi < v1; ++vi) {
      st.userPolylineVerts.push_back(cb.polyVerts[static_cast<size_t>(vi * 3 + 0)] + dx);
      st.userPolylineVerts.push_back(cb.polyVerts[static_cast<size_t>(vi * 3 + 1)] + dy);
      st.userPolylineVerts.push_back(cb.polyVerts[static_cast<size_t>(vi * 3 + 2)]);
    }
    if (st.userPolylineOffsets.empty())
      st.userPolylineOffsets.push_back(baseVert);
    st.userPolylineOffsets.push_back(baseVert + nv);
    uint8_t cl = (static_cast<size_t>(pi) < cb.polyClosed.size()) ? cb.polyClosed[static_cast<size_t>(pi)] : 0u;
    st.userPolylineClosed.push_back(cl);
    st.userPolylineAttrs.push_back(cb.polyAttrs[static_cast<size_t>(pi)]);
    st.selection.push_back({ST::Polyline, static_cast<int>(st.userPolylineOffsets.size()) - 2});
  }
  for (size_t i = 0; i < cb.annotations.size(); ++i) {
    CadAnnotation a = cb.annotations[i];
    a.surveyPointLabelForId = -1;  // strip survey link so pasted labels are independent
    a.surveyLabelHasUserOffset = false;
    a.insX += dx;
    a.insY += dy;
    if (a.kind == CadAnnotation::Kind::Mtext) {
      a.boxMinX += dx; a.boxMinY += dy;
      a.boxMaxX += dx; a.boxMaxY += dy;
    } else if (a.kind == CadAnnotation::Kind::DimAligned || a.kind == CadAnnotation::Kind::DimLinear) {
      a.dimExt1X += dx; a.dimExt1Y += dy;
      a.dimExt2X += dx; a.dimExt2Y += dy;
    } else if (a.kind == CadAnnotation::Kind::DimAngular) {
      a.dimAngVertexX += dx; a.dimAngVertexY += dy;
      a.dimExt1X += dx; a.dimExt1Y += dy;
      a.dimExt2X += dx; a.dimExt2Y += dy;
    }
    // Cross-space height (paper→model): paper text height is in paper inches, which transfer 1:1 to model units;
    // model text stores plotted-inches (× modelUnitsPerPlottedInch = model units), so divide by the factor.
    if (cb.fromPaper)
      a.plottedHeightInches /= std::max(st.modelUnitsPerPlottedInch, 1.e-6f);
    st.cadAnnotations.push_back(std::move(a));
    st.cadAnnotationAttrs.push_back(cb.annotationAttrs[i]);
    st.selection.push_back({ST::Annotation, static_cast<int>(st.cadAnnotations.size()) - 1});
  }
  for (size_t i = 0; i < cb.filledRegions.size(); ++i) {  // solid fills — now selectable (REQ-042)
    CadFilledRegion fr = cb.filledRegions[i];
    // Model-space paste: offset X/Y and carry the clipboard's Z through unchanged (REQ-057) —
    // paste must not flatten elevation.
    for (size_t v = 0; v + 2 < fr.vertsXyz.size(); v += 3) {
      fr.vertsXyz[v] += dx;
      fr.vertsXyz[v + 1] += dy;
    }
    st.cadFilledRegions.push_back(std::move(fr));
    st.cadFilledRegionAttrs.push_back(cb.filledRegionAttrs[i]);
    st.selection.push_back({ST::FilledRegion, static_cast<int>(st.cadFilledRegions.size()) - 1});
  }
  BumpCadGpuCache(st);
}

// Paste clipboard geometry into a PAPER layout's store with (dx, dy) applied (paper inches). Builds
// st.selectedPaperEntities (REQ-038 #5). Returns the count of non-text annotations skipped (paper holds
// only Text/Mtext). Caller pushes the undo snapshot. ADR-013: cross-space is an explicit 1:1 transfer.
static int CommitPasteIntoPaper(AppCommandState& st, PaperLayout& L, float dx, float dy) {
  const CadClipboard& cb = st.clipboard;
  using PT = PaperEntityRef::Type;
  st.selectedPaperEntities.clear();
  int skipped = 0;

  for (size_t i = 0; i + 5 < cb.lines.size() + 1; i += 6) {
    L.paperLines.push_back(cb.lines[i + 0] + dx);
    L.paperLines.push_back(cb.lines[i + 1] + dy);
    L.paperLines.push_back(cb.lines[i + 2]);
    L.paperLines.push_back(cb.lines[i + 3] + dx);
    L.paperLines.push_back(cb.lines[i + 4] + dy);
    L.paperLines.push_back(cb.lines[i + 5]);
    L.paperLineAttrs.push_back(cb.lineAttrs[i / 6]);
    st.selectedPaperEntities.push_back({PT::Line, static_cast<int>(L.paperLines.size() / 6) - 1});
  }
  // Clipboard (cx,cy,z,r) → paper (cx,cy,r): Z is DROPPED at the sheet boundary, not carried.
  // A sheet is 2D (ADR-025 (g)), so an elevated model circle pasted onto paper lands flat —
  // deliberate and visible, rather than silently keeping an elevation paper cannot represent.
  for (size_t i = 0; i + 3 < cb.circlesCxCyZR.size() + 1; i += 4) {
    L.paperCircles.push_back(cb.circlesCxCyZR[i + 0] + dx);
    L.paperCircles.push_back(cb.circlesCxCyZR[i + 1] + dy);
    L.paperCircles.push_back(cb.circlesCxCyZR[i + 3]);  // radius — [i+2] is the discarded Z
    L.paperCircleAttrs.push_back(cb.circleAttrs[i / 4]);
    st.selectedPaperEntities.push_back({PT::Circle, static_cast<int>(L.paperCircles.size() / 3) - 1});
  }
  for (size_t i = 0; i < cb.arcs.size(); ++i) {
    CadArc a = cb.arcs[i];
    a.cx += dx;
    a.cy += dy;
    L.paperArcs.push_back(a);
    L.paperArcAttrs.push_back(cb.arcAttrs[i]);
    st.selectedPaperEntities.push_back({PT::Arc, static_cast<int>(L.paperArcs.size()) - 1});
  }
  for (size_t i = 0; i < cb.ellipses.size(); ++i) {
    CadEllipse el = cb.ellipses[i];
    el.cx += dx;
    el.cy += dy;
    L.paperEllipses.push_back(el);
    L.paperEllAttrs.push_back(cb.ellAttrs[i]);
    st.selectedPaperEntities.push_back({PT::Ellipse, static_cast<int>(L.paperEllipses.size()) - 1});
  }
  const int nPoly = static_cast<int>(cb.polyOffsets.size()) - 1;
  for (int pi = 0; pi < nPoly; ++pi) {
    const int v0 = cb.polyOffsets[static_cast<size_t>(pi)];
    const int v1 = cb.polyOffsets[static_cast<size_t>(pi + 1)];
    const int nv = v1 - v0;
    if (nv < 2)
      continue;
    const int baseVert = L.paperPolyOffsets.empty() ? 0 : L.paperPolyOffsets.back();
    for (int vi = v0; vi < v1; ++vi) {
      L.paperPolyVerts.push_back(cb.polyVerts[static_cast<size_t>(vi * 3 + 0)] + dx);
      L.paperPolyVerts.push_back(cb.polyVerts[static_cast<size_t>(vi * 3 + 1)] + dy);
      L.paperPolyVerts.push_back(cb.polyVerts[static_cast<size_t>(vi * 3 + 2)]);
    }
    if (L.paperPolyOffsets.empty())
      L.paperPolyOffsets.push_back(baseVert);
    L.paperPolyOffsets.push_back(baseVert + nv);
    uint8_t cl = (static_cast<size_t>(pi) < cb.polyClosed.size()) ? cb.polyClosed[static_cast<size_t>(pi)] : 0u;
    L.paperPolyClosed.push_back(cl);
    L.paperPolyAttrs.push_back(cb.polyAttrs[static_cast<size_t>(pi)]);
    st.selectedPaperEntities.push_back({PT::Polyline, static_cast<int>(L.paperPolyOffsets.size()) - 2});
  }
  for (size_t i = 0; i < cb.annotations.size(); ++i) {
    const CadAnnotation& src = cb.annotations[i];
    if (src.kind != CadAnnotation::Kind::Text && src.kind != CadAnnotation::Kind::Mtext) {
      ++skipped;  // dimensions have no paper store (ADR-013 scope); reported by the caller, not dropped silently.
      continue;
    }
    CadAnnotation a = src;
    a.surveyPointLabelForId = -1;
    a.surveyLabelHasUserOffset = false;
    a.insX += dx;
    a.insY += dy;
    if (a.kind == CadAnnotation::Kind::Mtext) {
      a.boxMinX += dx; a.boxMinY += dy;
      a.boxMaxX += dx; a.boxMaxY += dy;
    }
    // Cross-space height: a model annotation's world height is plottedHeightInches × modelUnitsPerPlottedInch
    // (model units), which transfers 1:1 to paper inches. Paper text uses plottedHeightInches AS paper inches,
    // so scale it up by the factor. (Paper→paper keeps the value.)
    if (!cb.fromPaper)
      a.plottedHeightInches *= std::max(st.modelUnitsPerPlottedInch, 1.e-6f);
    L.paperTexts.push_back(std::move(a));
    L.paperTextAttrs.push_back(cb.annotationAttrs[i]);
    st.selectedPaperEntities.push_back({PT::Text, static_cast<int>(L.paperTexts.size()) - 1});
  }
  for (size_t i = 0; i < cb.filledRegions.size(); ++i) {  // solid fills onto the sheet (not selectable)
    CadFilledRegion fr = cb.filledRegions[i];
    for (size_t v = 0; v + 2 < fr.vertsXyz.size(); v += 3) {
      fr.vertsXyz[v] += dx;
      fr.vertsXyz[v + 1] += dy;
      fr.vertsXyz[v + 2] = 0.f;  // landing on a sheet: paper space is 2D (ADR-025 (g))
    }
    L.paperFilledRegions.push_back(std::move(fr));
    L.paperFilledRegionAttrs.push_back(cb.filledRegionAttrs[i]);
  }
  BumpCadGpuCache(st);
  return skipped;
}

// Paste clipboard geometry into the ACTIVE space (model or the active paper layout) with (dx, dy) applied.
// Routes by ActivePaperGeometryTarget (ADR-009/013). Caller pushes the undo snapshot.
static void CommitPasteFromClipboard(AppCommandState& st, float dx, float dy, std::vector<std::string>& log) {
  // A pasted entity is a *different* entity and must not inherit its source's identity (REQ-076).
  // Cleared on the clipboard rather than on the destination arrays because this is the one place
  // both paste paths (PASTE and PASTEORIG) and both destination spaces pass through; the next
  // EnsureEntityIds then mints fresh ids. Re-clearing on a repeat paste is a harmless no-op.
  ClearEntityIdsFrom(st.clipboard.lineAttrs, 0);
  ClearEntityIdsFrom(st.clipboard.circleAttrs, 0);
  ClearEntityIdsFrom(st.clipboard.arcAttrs, 0);
  ClearEntityIdsFrom(st.clipboard.ellAttrs, 0);
  ClearEntityIdsFrom(st.clipboard.polyAttrs, 0);
  ClearEntityIdsFrom(st.clipboard.annotationAttrs, 0);
  ClearEntityIdsFrom(st.clipboard.filledRegionAttrs, 0);
  if (PaperLayout* L = ActivePaperGeometryTarget(st)) {
    st.selection.clear();  // crossing into paper: model selection no longer applies
    const int skipped = CommitPasteIntoPaper(st, *L, dx, dy);
    if (skipped > 0)
      log.push_back("PASTE — " + std::to_string(skipped) +
                    " dimension(s) skipped (paper space stores lines, text, circles, arcs, ellipses, polylines).");
  } else {
    ClearPaperEntitySelection(st);
    CommitPasteIntoModel(st, dx, dy);
  }
}

static void DuplicateCadSelectionRotated(AppCommandState& st, float bx, float by, float rad) {
  const size_t polyVertsBefore = st.userPolylineVerts.size();
  const size_t featureVertsBefore = st.featureLineVerts.size();
  std::vector<float> newLines;
  std::vector<float> newCircles;
  std::vector<EntityAttributes> newLineAttrs;
  std::vector<EntityAttributes> newCircleAttrs;
  std::vector<CadAnnotation> newAnn;
  std::vector<EntityAttributes> newAnnAttrs;
  std::vector<CadArc> newArcs;
  std::vector<EntityAttributes> newArcAttrs;
  std::vector<CadEllipse> newEll;
  std::vector<EntityAttributes> newEllAttrs;

  for (const auto& e : st.selection) {
    if (e.type == SelectedEntity::Type::LineSeg) {
      size_t k = static_cast<size_t>(e.index) * 6;
      if (k + 5 < st.userLinesFlat.size()) {
        float x0 = st.userLinesFlat[k];
        float y0 = st.userLinesFlat[k + 1];
        float z0 = st.userLinesFlat[k + 2];
        float x1 = st.userLinesFlat[k + 3];
        float y1 = st.userLinesFlat[k + 4];
        float z1 = st.userLinesFlat[k + 5];
        RotateAroundBase(bx, by, rad, &x0, &y0);
        RotateAroundBase(bx, by, rad, &x1, &y1);
        newLines.push_back(x0);
        newLines.push_back(y0);
        newLines.push_back(z0);
        newLines.push_back(x1);
        newLines.push_back(y1);
        newLines.push_back(z1);
        EntityAttributes a{};
        if (e.index >= 0 && static_cast<size_t>(e.index) < st.userLineAttrs.size())
          a = st.userLineAttrs[static_cast<size_t>(e.index)];
        newLineAttrs.push_back(DuplicatedEntityAttrs(a));
      }
    } else if (e.type == SelectedEntity::Type::Circle) {
      size_t k = static_cast<size_t>(e.index) * 4;
      if (k + 3 < st.userCirclesCxCyZR.size()) {
        float cx = st.userCirclesCxCyZR[k];
        float cy = st.userCirclesCxCyZR[k + 1];
        float r = st.userCirclesCxCyZR[k + 3];
        RotateAroundBase(bx, by, rad, &cx, &cy);
        newCircles.push_back(cx);
        newCircles.push_back(cy);
        newCircles.push_back(st.userCirclesCxCyZR[k + 2]);  // z — rotation is about the Z axis
        newCircles.push_back(r);
        EntityAttributes a{};
        if (e.index >= 0 && static_cast<size_t>(e.index) < st.userCircleAttrs.size())
          a = st.userCircleAttrs[static_cast<size_t>(e.index)];
        newCircleAttrs.push_back(DuplicatedEntityAttrs(a));
      }
    } else if (e.type == SelectedEntity::Type::Annotation) {
      const size_t k = static_cast<size_t>(e.index);
      if (k < st.cadAnnotations.size()) {
        CadAnnotation c = st.cadAnnotations[k];
        c.surveyPointLabelForId = -1;
        RotateAroundBase(bx, by, rad, &c.insX, &c.insY);
        if (c.kind == CadAnnotation::Kind::Text) {
          c.rotationRad += rad;
        } else if (c.kind == CadAnnotation::Kind::DimLinear) {
          RotateCadDimLinearAroundBase(bx, by, rad, &c);
        } else if (c.kind == CadAnnotation::Kind::DimAligned) {
          RotateAroundBase(bx, by, rad, &c.dimExt1X, &c.dimExt1Y);
          RotateAroundBase(bx, by, rad, &c.dimExt2X, &c.dimExt2Y);
          float sx1 = 0.f, sy1 = 0.f, sx2 = 0.f, sy2 = 0.f, tx = 0.f, ty = 0.f, nx = 0.f, ny = 0.f, ml = 0.f;
          if (CadDimAlignedGeometry(c, &sx1, &sy1, &sx2, &sy2, &tx, &ty, &nx, &ny, &ml))
            c.rotationRad = std::atan2(ty, tx);
        } else {
          float xs[4] = {c.boxMinX, c.boxMaxX, c.boxMaxX, c.boxMinX};
          float ys[4] = {c.boxMinY, c.boxMinY, c.boxMaxY, c.boxMaxY};
          float mnX = xs[0];
          float mxX = xs[0];
          float mnY = ys[0];
          float mxY = ys[0];
          for (int i = 0; i < 4; ++i) {
            RotateAroundBase(bx, by, rad, &xs[i], &ys[i]);
            mnX = std::min(mnX, xs[i]);
            mxX = std::max(mxX, xs[i]);
            mnY = std::min(mnY, ys[i]);
            mxY = std::max(mxY, ys[i]);
          }
          c.boxMinX = mnX;
          c.boxMaxX = mxX;
          c.boxMinY = mnY;
          c.boxMaxY = mxY;
          c.insX = mnX;
          c.insY = mnY;
        }
        newAnn.push_back(std::move(c));
        EntityAttributes a{};
        if (k < st.cadAnnotationAttrs.size())
          a = st.cadAnnotationAttrs[k];
        newAnnAttrs.push_back(DuplicatedEntityAttrs(a));
      }
    } else if (e.type == SelectedEntity::Type::Arc) {
      const size_t k = static_cast<size_t>(e.index);
      if (k < st.userArcs.size()) {
        CadArc a = st.userArcs[k];
        RotateAroundBase(bx, by, rad, &a.cx, &a.cy);
        a.startRad += rad;
        newArcs.push_back(a);
        EntityAttributes at{};
        if (k < st.userArcAttrs.size())
          at = st.userArcAttrs[k];
        newArcAttrs.push_back(DuplicatedEntityAttrs(at));
      }
    } else if (e.type == SelectedEntity::Type::Ellipse) {
      const size_t k = static_cast<size_t>(e.index);
      if (k < st.userEllipses.size()) {
        CadEllipse el = st.userEllipses[k];
        float mx = el.cx + el.majVx;
        float my = el.cy + el.majVy;
        RotateAroundBase(bx, by, rad, &el.cx, &el.cy);
        RotateAroundBase(bx, by, rad, &mx, &my);
        el.majVx = mx - el.cx;
        el.majVy = my - el.cy;
        newEll.push_back(el);
        EntityAttributes at{};
        if (k < st.userEllAttrs.size())
          at = st.userEllAttrs[k];
        newEllAttrs.push_back(DuplicatedEntityAttrs(at));
      }
    } else if (e.type == SelectedEntity::Type::Polyline) {
      const int pi = e.index;
      if (pi < 0 || static_cast<size_t>(pi + 1) >= st.userPolylineOffsets.size())
        continue;
      const int v0 = st.userPolylineOffsets[static_cast<size_t>(pi)];
      const int v1 = st.userPolylineOffsets[static_cast<size_t>(pi + 1)];
      const int nv = v1 - v0;
      if (nv < 2)
        continue;
      if (st.userPolylineOffsets.empty())
        st.userPolylineOffsets.push_back(0);
      const int baseVert = st.userPolylineOffsets.back();
      for (int vi = v0; vi < v1; ++vi) {
        float px = st.userPolylineVerts[static_cast<size_t>(vi * 3 + 0)];
        float py = st.userPolylineVerts[static_cast<size_t>(vi * 3 + 1)];
        float pz = st.userPolylineVerts[static_cast<size_t>(vi * 3 + 2)];
        RotateAroundBase(bx, by, rad, &px, &py);
        st.userPolylineVerts.push_back(px);
        st.userPolylineVerts.push_back(py);
        st.userPolylineVerts.push_back(pz);
      }
      st.userPolylineOffsets.push_back(baseVert + nv);
      uint8_t cl = 0;
      if (static_cast<size_t>(pi) < st.userPolylineClosed.size())
        cl = st.userPolylineClosed[static_cast<size_t>(pi)];
      st.userPolylineClosed.push_back(cl);
      EntityAttributes at{};
      if (static_cast<size_t>(pi) < st.userPolylineAttrs.size())
        at = st.userPolylineAttrs[static_cast<size_t>(pi)];
      st.userPolylineAttrs.push_back(DuplicatedEntityAttrs(at));
    }
  }
  st.userLinesFlat.insert(st.userLinesFlat.end(), newLines.begin(), newLines.end());
  st.userCirclesCxCyZR.insert(st.userCirclesCxCyZR.end(), newCircles.begin(), newCircles.end());
  st.userLineAttrs.insert(st.userLineAttrs.end(), newLineAttrs.begin(), newLineAttrs.end());
  st.userCircleAttrs.insert(st.userCircleAttrs.end(), newCircleAttrs.begin(), newCircleAttrs.end());
  st.cadAnnotations.insert(st.cadAnnotations.end(), newAnn.begin(), newAnn.end());
  st.cadAnnotationAttrs.insert(st.cadAnnotationAttrs.end(), newAnnAttrs.begin(), newAnnAttrs.end());
  st.userArcs.insert(st.userArcs.end(), newArcs.begin(), newArcs.end());
  st.userArcAttrs.insert(st.userArcAttrs.end(), newArcAttrs.begin(), newArcAttrs.end());
  st.userEllipses.insert(st.userEllipses.end(), newEll.begin(), newEll.end());
  st.userEllAttrs.insert(st.userEllAttrs.end(), newEllAttrs.begin(), newEllAttrs.end());

  // Feature lines (REQ-087) — the same append as the translated path, differing only in the point
  // function, which is exactly why both go through AppendFeatureLineCopy.
  ForEachSelectedFeatureLine(st, [&](int fi, int v0, int v1) {
    AppendFeatureLineCopy(st, fi, v0, v1,
                          [&](float* x, float* y) { RotateAroundBase(bx, by, rad, x, y); });
  });

  if (!newLines.empty() || !newCircles.empty() || !newAnn.empty() || !newArcs.empty() || !newEll.empty() ||
      st.userPolylineVerts.size() != polyVertsBefore ||
      st.featureLineVerts.size() != featureVertsBefore)
    BumpCadGpuCache(st);
}

static void FinalizeCopyTranslation(AppCommandState& st, float dx, float dy, std::vector<std::string>& log) {
  st.pendingSurveyDupIsRotate = false;
  DuplicateCadSelectionTranslated(st, dx, dy);
  // Stay in COPY — same selection + base, ready for another destination.
  st.modifyPhase = AppCommandState::ModifyPhase::NeedDestination;
  if (!st.selectedSurveyPointIndices.empty()) {
    st.pendingCopyDx = dx;
    st.pendingCopyDy = dy;
    st.copySurveyDupModalOpen = true;
    st.copySurveyDupModalOpenRequested = true;
    log.push_back("COPY — CAD geometry duplicated; choose survey ID policy.");
  } else {
    log.push_back("COPY — next destination (ESC to exit):");
  }
}

/// Remove TIN surfaces from the selection before a transform runs, and **say so** (REQ-201).
///
/// ADR-036 (b)/(c): a surface is display-only. Its geometry is derived from its definition, so a
/// translated surface would be silently un-translated by the next rebuild — a transform that appears
/// to work and then quietly undoes itself is worse than one that declines.
///
/// The refusal is spoken rather than performed by omission. Every Apply* funnel below simply skips
/// entity types it does not handle, so without this the user would drag a selection containing a
/// surface, watch everything else move, and be told nothing about why one object stayed put. That
/// silence is the exact failure mode ADR-035 (g) was written about.
///
/// Called from the funnels rather than from the Start* commands because a surface can be added to
/// the selection by a window drag AFTER the command starts.
void DropSurfacesFromSelectionForTransform(AppCommandState& st, const char* commandName,
                                           std::vector<std::string>& log) {
  const size_t before = st.selection.size();
  st.selection.erase(std::remove_if(st.selection.begin(), st.selection.end(),
                                    [](const SelectedEntity& e) {
                                      return e.type == SelectedEntity::Type::Surface;
                                    }),
                     st.selection.end());
  const size_t dropped = before - st.selection.size();
  if (dropped == 0)
    return;
  log.push_back(std::string(commandName) + " — " + std::to_string(dropped) + " surface(s) excluded: a surface's" +
                " shape comes from its definition, so moving it would be undone by the next rebuild." +
                " Edit its definition in the Surfaces panel instead.");
}

void ApplyRotationToSelection(AppCommandState& st, float bx, float by, float rad, std::vector<std::string>& log) {
  DropSurfacesFromSelectionForTransform(st, "ROTATE", log);
  std::vector<bool> lineMark(std::max<size_t>(1, st.userLinesFlat.size() / 6), false);
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::LineSeg)
      continue;
    if (e.index >= 0 && static_cast<size_t>(e.index) < lineMark.size())
      lineMark[static_cast<size_t>(e.index)] = true;
  }
  if (!lineMark.empty()) {
    for (size_t i = 0; i < lineMark.size(); ++i) {
      if (!lineMark[i])
        continue;
      size_t k = i * 6;
      if (k + 5 < st.userLinesFlat.size()) {
        RotateAroundBase(bx, by, rad, &st.userLinesFlat[k], &st.userLinesFlat[k + 1]);
        RotateAroundBase(bx, by, rad, &st.userLinesFlat[k + 3], &st.userLinesFlat[k + 4]);
      }
    }
  }
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::Circle)
      continue;
    size_t k = static_cast<size_t>(e.index) * 4;
    if (k + 3 < st.userCirclesCxCyZR.size()) {
      RotateAroundBase(bx, by, rad, &st.userCirclesCxCyZR[k], &st.userCirclesCxCyZR[k + 1]);
    }
  }
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::Arc)
      continue;
    const size_t k = static_cast<size_t>(e.index);
    if (k >= st.userArcs.size())
      continue;
    CadArc& a = st.userArcs[k];
    RotateAroundBase(bx, by, rad, &a.cx, &a.cy);
    a.startRad += rad;
  }
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::Ellipse)
      continue;
    const size_t k = static_cast<size_t>(e.index);
    if (k >= st.userEllipses.size())
      continue;
    CadEllipse& el = st.userEllipses[k];
    float mx = el.cx + el.majVx;
    float my = el.cy + el.majVy;
    RotateAroundBase(bx, by, rad, &el.cx, &el.cy);
    RotateAroundBase(bx, by, rad, &mx, &my);
    el.majVx = mx - el.cx;
    el.majVy = my - el.cy;
  }
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::Polyline)
      continue;
    const int pi = e.index;
    if (pi < 0 || static_cast<size_t>(pi + 1) >= st.userPolylineOffsets.size())
      continue;
    const int v0 = st.userPolylineOffsets[static_cast<size_t>(pi)];
    const int v1 = st.userPolylineOffsets[static_cast<size_t>(pi + 1)];
    for (int vi = v0; vi < v1; ++vi) {
      RotateAroundBase(bx, by, rad, &st.userPolylineVerts[static_cast<size_t>(vi * 3 + 0)],
                       &st.userPolylineVerts[static_cast<size_t>(vi * 3 + 1)]);
    }
  }
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::Annotation)
      continue;
    const size_t k = static_cast<size_t>(e.index);
    if (k >= st.cadAnnotations.size())
      continue;
    CadAnnotation& a = st.cadAnnotations[k];
    RotateAroundBase(bx, by, rad, &a.insX, &a.insY);
    if (a.kind == CadAnnotation::Kind::Text) {
      a.rotationRad += rad;
    } else if (a.kind == CadAnnotation::Kind::DimLinear) {
      RotateCadDimLinearAroundBase(bx, by, rad, &a);
    } else if (a.kind == CadAnnotation::Kind::DimAligned) {
      RotateAroundBase(bx, by, rad, &a.dimExt1X, &a.dimExt1Y);
      RotateAroundBase(bx, by, rad, &a.dimExt2X, &a.dimExt2Y);
      float sx1 = 0.f, sy1 = 0.f, sx2 = 0.f, sy2 = 0.f, tx = 0.f, ty = 0.f, nx = 0.f, ny = 0.f, ml = 0.f;
      if (CadDimAlignedGeometry(a, &sx1, &sy1, &sx2, &sy2, &tx, &ty, &nx, &ny, &ml))
        a.rotationRad = std::atan2(ty, tx);
    } else {
      float xs[4] = {a.boxMinX, a.boxMaxX, a.boxMaxX, a.boxMinX};
      float ys[4] = {a.boxMinY, a.boxMinY, a.boxMaxY, a.boxMaxY};
      float mnX = xs[0];
      float mxX = xs[0];
      float mnY = ys[0];
      float mxY = ys[0];
      for (int i = 0; i < 4; ++i) {
        RotateAroundBase(bx, by, rad, &xs[i], &ys[i]);
        mnX = std::min(mnX, xs[i]);
        mxX = std::max(mxX, xs[i]);
        mnY = std::min(mnY, ys[i]);
        mxY = std::max(mxY, ys[i]);
      }
      a.boxMinX = mnX;
      a.boxMaxX = mxX;
      a.boxMinY = mnY;
      a.boxMaxY = mxY;
      a.insX = mnX;
      a.insY = mnY;
    }
  }
  // PDF underlays: rotate insertion point around base; accumulate rotation angle.
  constexpr float kPdfRadToDeg = 180.f / 3.14159265f;
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::PdfUnderlay)
      continue;
    if (e.index < 0 || static_cast<size_t>(e.index) >= st.pdfAttachments.size())
      continue;
    PdfAttachment& att = st.pdfAttachments[static_cast<size_t>(e.index)];
    RotateAroundBase(bx, by, rad, &att.insertX, &att.insertY);
    att.rotationDeg += rad * kPdfRadToDeg;
  }
  // Feature lines (REQ-087) — every vertex, PIs and elevation points alike, so the elevation points
  // stay on the line (ADR-035 (b)).
  TransformSelectedFeatureLinesInPlace(
      st, [&](float* x, float* y) { RotateAroundBase(bx, by, rad, x, y); });
  ApplyRotationToSelectedSurveyPoints(st, bx, by, rad);
  BumpCadGpuCache(st);
}

void ApplyTranslationToSelection(AppCommandState& st, float dx, float dy, std::vector<std::string>& log) {
  DropSurfacesFromSelectionForTransform(st, "MOVE", log);
  std::vector<bool> lineMark(std::max<size_t>(1, st.userLinesFlat.size() / 6), false);
  for (const auto& e : st.selection) {
    if (e.type == SelectedEntity::Type::LineSeg && e.index >= 0 &&
        static_cast<size_t>(e.index) < lineMark.size())
      lineMark[static_cast<size_t>(e.index)] = true;
  }
  for (size_t i = 0; i < lineMark.size(); ++i) {
    if (!lineMark[i])
      continue;
    size_t k = i * 6;
    if (k + 5 < st.userLinesFlat.size()) {
      st.userLinesFlat[k] += dx;
      st.userLinesFlat[k + 1] += dy;
      st.userLinesFlat[k + 3] += dx;
      st.userLinesFlat[k + 4] += dy;
    }
  }
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::Circle)
      continue;
    size_t k = static_cast<size_t>(e.index) * 4;
    if (k + 3 < st.userCirclesCxCyZR.size()) {
      st.userCirclesCxCyZR[k] += dx;
      st.userCirclesCxCyZR[k + 1] += dy;
    }
  }
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::Arc)
      continue;
    const size_t k = static_cast<size_t>(e.index);
    if (k >= st.userArcs.size())
      continue;
    st.userArcs[k].cx += dx;
    st.userArcs[k].cy += dy;
  }
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::Ellipse)
      continue;
    const size_t k = static_cast<size_t>(e.index);
    if (k >= st.userEllipses.size())
      continue;
    st.userEllipses[k].cx += dx;
    st.userEllipses[k].cy += dy;
  }
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::Polyline)
      continue;
    const int pi = e.index;
    if (pi < 0 || static_cast<size_t>(pi + 1) >= st.userPolylineOffsets.size())
      continue;
    const int v0 = st.userPolylineOffsets[static_cast<size_t>(pi)];
    const int v1 = st.userPolylineOffsets[static_cast<size_t>(pi + 1)];
    for (int vi = v0; vi < v1; ++vi) {
      st.userPolylineVerts[static_cast<size_t>(vi * 3 + 0)] += dx;
      st.userPolylineVerts[static_cast<size_t>(vi * 3 + 1)] += dy;
    }
  }
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::Annotation)
      continue;
    const size_t k = static_cast<size_t>(e.index);
    if (k >= st.cadAnnotations.size())
      continue;
    CadAnnotation& a = st.cadAnnotations[k];
    a.insX += dx;
    a.insY += dy;
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
  // PDF underlays: shift insertion point.
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::PdfUnderlay)
      continue;
    if (e.index < 0 || static_cast<size_t>(e.index) >= st.pdfAttachments.size())
      continue;
    st.pdfAttachments[static_cast<size_t>(e.index)].insertX += dx;
    st.pdfAttachments[static_cast<size_t>(e.index)].insertY += dy;
  }
  // Filled regions (REQ-042): translate every loop vertex.
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::FilledRegion)
      continue;
    if (e.index < 0 || static_cast<size_t>(e.index) >= st.cadFilledRegions.size())
      continue;
    hatchgeom::Translate(st.cadFilledRegions[static_cast<size_t>(e.index)], dx, dy);
  }
  // Feature lines (REQ-087) — see ApplyRotationToSelection.
  TransformSelectedFeatureLinesInPlace(st, [&](float* x, float* y) {
    *x += dx;
    *y += dy;
  });
  ApplyTranslationToSelectedSurveyPoints(st, dx, dy);
  BumpCadGpuCache(st);
}

static void ScalePtAroundBase(float bx, float by, float sc, float* x, float* y) {
  *x = bx + sc * (*x - bx);
  *y = by + sc * (*y - by);
}

static void ScaleCadDimLinearAroundBase(float bx, float by, float sc, CadAnnotation* ann) {
  if (!ann || ann->kind != CadAnnotation::Kind::DimLinear)
    return;
  const float x1 = ann->dimExt1X, y1 = ann->dimExt1Y, x2 = ann->dimExt2X, y2 = ann->dimExt2Y;
  const float cmx = 0.5f * (x1 + x2);
  const float cmy = 0.5f * (y1 + y2);
  float dmx = cmx;
  float dmy = cmy;
  if (!ann->dimLinearVertical)
    dmy = cmy + ann->dimSignedOffset;
  else
    dmx = cmx + ann->dimSignedOffset;
  ScalePtAroundBase(bx, by, sc, &ann->dimExt1X, &ann->dimExt1Y);
  ScalePtAroundBase(bx, by, sc, &ann->dimExt2X, &ann->dimExt2Y);
  ScalePtAroundBase(bx, by, sc, &dmx, &dmy);
  const float ncmx = 0.5f * (ann->dimExt1X + ann->dimExt2X);
  const float ncmy = 0.5f * (ann->dimExt1Y + ann->dimExt2Y);
  if (!ann->dimLinearVertical)
    ann->dimSignedOffset = dmy - ncmy;
  else
    ann->dimSignedOffset = dmx - ncmx;
  float sx1 = 0.f, sy1 = 0.f, sx2 = 0.f, sy2 = 0.f, tx = 0.f, ty = 0.f, nx = 0.f, ny = 0.f, ml = 0.f;
  if (CadDimLinearGeometry(*ann, &sx1, &sy1, &sx2, &sy2, &tx, &ty, &nx, &ny, &ml))
    ann->rotationRad = std::atan2(ty, tx);
}

static bool ComputeSelectionCentroidWorld(const AppCommandState& st, float* outCx, float* outCy) {
  if (!outCx || !outCy)
    return false;
  double accx = 0.0;
  double accy = 0.0;
  int n = 0;
  for (const auto& e : st.selection) {
    if (e.type == SelectedEntity::Type::LineSeg) {
      const size_t k = static_cast<size_t>(e.index) * 6;
      if (k + 5 < st.userLinesFlat.size()) {
        accx += 0.5 * static_cast<double>(st.userLinesFlat[k] + st.userLinesFlat[k + 3]);
        accy += 0.5 * static_cast<double>(st.userLinesFlat[k + 1] + st.userLinesFlat[k + 4]);
        ++n;
      }
    } else if (e.type == SelectedEntity::Type::Circle) {
      const size_t k = static_cast<size_t>(e.index) * 4;
      if (k + 3 < st.userCirclesCxCyZR.size()) {
        accx += static_cast<double>(st.userCirclesCxCyZR[k]);
        accy += static_cast<double>(st.userCirclesCxCyZR[k + 1]);
        ++n;
      }
    } else if (e.type == SelectedEntity::Type::Arc) {
      const size_t k = static_cast<size_t>(e.index);
      if (k < st.userArcs.size()) {
        accx += static_cast<double>(st.userArcs[k].cx);
        accy += static_cast<double>(st.userArcs[k].cy);
        ++n;
      }
    } else if (e.type == SelectedEntity::Type::Ellipse) {
      const size_t k = static_cast<size_t>(e.index);
      if (k < st.userEllipses.size()) {
        accx += static_cast<double>(st.userEllipses[k].cx);
        accy += static_cast<double>(st.userEllipses[k].cy);
        ++n;
      }
    } else if (e.type == SelectedEntity::Type::Polyline) {
      const int pi = e.index;
      if (pi < 0 || static_cast<size_t>(pi + 1) >= st.userPolylineOffsets.size())
        continue;
      const int v0 = st.userPolylineOffsets[static_cast<size_t>(pi)];
      const int v1 = st.userPolylineOffsets[static_cast<size_t>(pi + 1)];
      double sx = 0.0, sy = 0.0;
      int nv = 0;
      for (int vi = v0; vi < v1; ++vi) {
        sx += static_cast<double>(st.userPolylineVerts[static_cast<size_t>(vi * 3)]);
        sy += static_cast<double>(st.userPolylineVerts[static_cast<size_t>(vi * 3 + 1)]);
        ++nv;
      }
      if (nv > 0) {
        accx += sx / static_cast<double>(nv);
        accy += sy / static_cast<double>(nv);
        ++n;
      }
    } else if (e.type == SelectedEntity::Type::Annotation) {
      const size_t k = static_cast<size_t>(e.index);
      if (k < st.cadAnnotations.size()) {
        const CadAnnotation& a = st.cadAnnotations[k];
        accx += static_cast<double>(a.insX);
        accy += static_cast<double>(a.insY);
        ++n;
      }
    } else if (e.type == SelectedEntity::Type::PdfUnderlay) {
      const size_t k = static_cast<size_t>(e.index);
      if (k < st.pdfAttachments.size()) {
        const PdfAttachment& patt = st.pdfAttachments[k];
        constexpr float kPi = 3.14159265f;
        const float cosR = std::cos(patt.rotationDeg * kPi / 180.f);
        const float sinR = std::sin(patt.rotationDeg * kPi / 180.f);
        const float hw   = patt.pageWidthPts  * patt.scale * 0.5f;
        const float hh   = patt.pageHeightPts * patt.scale * 0.5f;
        accx += static_cast<double>(patt.insertX + cosR * hw - sinR * hh);
        accy += static_cast<double>(patt.insertY + sinR * hw + cosR * hh);
        ++n;
      }
    }
  }
  // Feature lines (REQ-087): one contribution per line, at its own vertex centroid — the same
  // weighting the polyline branch above uses, so a selection of both is not skewed toward whichever
  // happens to have more vertices.
  ForEachSelectedFeatureLine(st, [&](int /*fi*/, int v0, int v1) {
    double sx = 0.0, sy = 0.0;
    for (int vi = v0; vi < v1; ++vi) {
      sx += static_cast<double>(st.featureLineVerts[static_cast<size_t>(vi) * 3]);
      sy += static_cast<double>(st.featureLineVerts[static_cast<size_t>(vi) * 3 + 1]);
    }
    const double nv = static_cast<double>(v1 - v0);
    accx += sx / nv;
    accy += sy / nv;
    ++n;
  });
  for (int si : st.selectedSurveyPointIndices) {
    if (si >= 0 && static_cast<size_t>(si) < st.surveyPoints.size()) {
      accx += static_cast<double>(st.surveyPoints[static_cast<size_t>(si)].easting);
      accy += static_cast<double>(st.surveyPoints[static_cast<size_t>(si)].northing);
      ++n;
    }
  }
  if (n <= 0)
    return false;
  *outCx = static_cast<float>(accx / static_cast<double>(n));
  *outCy = static_cast<float>(accy / static_cast<double>(n));
  return true;
}

static void ComputeMaxSelectionDistanceFromPoint(const AppCommandState& st, float bx, float by, float* outMax) {
  if (!outMax)
    return;
  float m = 0.f;
  for (const auto& e : st.selection) {
    if (e.type == SelectedEntity::Type::LineSeg) {
      const size_t k = static_cast<size_t>(e.index) * 6;
      if (k + 5 < st.userLinesFlat.size()) {
        for (int i = 0; i < 2; ++i) {
          const float x = st.userLinesFlat[k + i * 3];
          const float y = st.userLinesFlat[k + i * 3 + 1];
          m = std::max(m, std::hypot(x - bx, y - by));
        }
      }
    } else if (e.type == SelectedEntity::Type::Circle) {
      const size_t k = static_cast<size_t>(e.index) * 4;
      if (k + 3 < st.userCirclesCxCyZR.size()) {
        const float cx = st.userCirclesCxCyZR[k];
        const float cy = st.userCirclesCxCyZR[k + 1];
        const float r = st.userCirclesCxCyZR[k + 3];
        m = std::max(m, std::hypot(cx - bx, cy - by) + r);
      }
    } else if (e.type == SelectedEntity::Type::Arc) {
      const size_t k = static_cast<size_t>(e.index);
      if (k < st.userArcs.size()) {
        const CadArc& a = st.userArcs[k];
        m = std::max(m, std::hypot(a.cx - bx, a.cy - by) + a.r);
      }
    } else if (e.type == SelectedEntity::Type::Ellipse) {
      const size_t k = static_cast<size_t>(e.index);
      if (k < st.userEllipses.size()) {
        const CadEllipse& el = st.userEllipses[k];
        const float ma = std::hypot(el.majVx, el.majVy);
        const float mb = ma * el.ratio;
        m = std::max(m, std::hypot(el.cx - bx, el.cy - by) + std::max(ma, mb));
      }
    } else if (e.type == SelectedEntity::Type::Polyline) {
      const int pi = e.index;
      if (pi < 0 || static_cast<size_t>(pi + 1) >= st.userPolylineOffsets.size())
        continue;
      const int v0 = st.userPolylineOffsets[static_cast<size_t>(pi)];
      const int v1 = st.userPolylineOffsets[static_cast<size_t>(pi + 1)];
      for (int vi = v0; vi < v1; ++vi) {
        const float x = st.userPolylineVerts[static_cast<size_t>(vi * 3)];
        const float y = st.userPolylineVerts[static_cast<size_t>(vi * 3 + 1)];
        m = std::max(m, std::hypot(x - bx, y - by));
      }
    } else if (e.type == SelectedEntity::Type::Annotation) {
      const size_t k = static_cast<size_t>(e.index);
      if (k >= st.cadAnnotations.size())
        continue;
      const CadAnnotation& a = st.cadAnnotations[k];
      if (a.kind == CadAnnotation::Kind::Mtext) {
        float xs[4] = {a.boxMinX, a.boxMaxX, a.boxMaxX, a.boxMinX};
        float ys[4] = {a.boxMinY, a.boxMinY, a.boxMaxY, a.boxMaxY};
        for (int i = 0; i < 4; ++i)
          m = std::max(m, std::hypot(xs[i] - bx, ys[i] - by));
      } else if (a.kind == CadAnnotation::Kind::DimAligned || a.kind == CadAnnotation::Kind::DimLinear) {
        m = std::max(m, std::hypot(a.dimExt1X - bx, a.dimExt1Y - by));
        m = std::max(m, std::hypot(a.dimExt2X - bx, a.dimExt2Y - by));
        m = std::max(m, std::hypot(a.insX - bx, a.insY - by));
      } else if (a.kind == CadAnnotation::Kind::DimAngular) {
        m = std::max(m, std::hypot(a.dimAngVertexX - bx, a.dimAngVertexY - by));
        m = std::max(m, std::hypot(a.dimExt1X - bx, a.dimExt1Y - by));
        m = std::max(m, std::hypot(a.dimExt2X - bx, a.dimExt2Y - by));
        m = std::max(m, std::hypot(a.insX - bx, a.insY - by));
      } else
        m = std::max(m, std::hypot(a.insX - bx, a.insY - by));
    } else if (e.type == SelectedEntity::Type::PdfUnderlay) {
      const size_t k = static_cast<size_t>(e.index);
      if (k < st.pdfAttachments.size()) {
        const PdfAttachment& patt = st.pdfAttachments[k];
        if (patt.pageWidthPts > 0.f && patt.pageHeightPts > 0.f) {
          constexpr float kPi = 3.14159265f;
          const float cosR = std::cos(patt.rotationDeg * kPi / 180.f);
          const float sinR = std::sin(patt.rotationDeg * kPi / 180.f);
          const float W    = patt.pageWidthPts  * patt.scale;
          const float H    = patt.pageHeightPts * patt.scale;
          const float lcx[4] = {0.f, W, W, 0.f};
          const float lcy[4] = {0.f, 0.f, H, H};
          for (int ci = 0; ci < 4; ++ci) {
            const float wx = patt.insertX + cosR * lcx[ci] - sinR * lcy[ci];
            const float wy = patt.insertY + sinR * lcx[ci] + cosR * lcy[ci];
            m = std::max(m, std::hypot(wx - bx, wy - by));
          }
        }
      }
    }
  }
  // Feature lines (REQ-087) — farthest vertex, as for a polyline.
  ForEachSelectedFeatureLine(st, [&](int /*fi*/, int v0, int v1) {
    for (int vi = v0; vi < v1; ++vi) {
      const float x = st.featureLineVerts[static_cast<size_t>(vi) * 3];
      const float y = st.featureLineVerts[static_cast<size_t>(vi) * 3 + 1];
      m = std::max(m, std::hypot(x - bx, y - by));
    }
  });
  for (int si : st.selectedSurveyPointIndices) {
    if (si >= 0 && static_cast<size_t>(si) < st.surveyPoints.size()) {
      const SurveyPoint& sp = st.surveyPoints[static_cast<size_t>(si)];
      m = std::max(m, std::hypot(sp.easting - bx, sp.northing - by));
    }
  }
  *outMax = m;
}

static float ComputeScaleReferenceDistance(const AppCommandState& st, float bx, float by) {
  float cx = 0.f, cy = 0.f;
  const bool haveC = ComputeSelectionCentroidWorld(st, &cx, &cy);
  const float dCent = haveC ? std::hypot(cx - bx, cy - by) : 0.f;
  float dMax = 0.f;
  ComputeMaxSelectionDistanceFromPoint(st, bx, by, &dMax);
  const float ref = std::max(dCent, 0.25f * std::max(dMax, 1e-6f));
  return std::max(ref, 1e-6f);
}

static void ApplyScaleToSelectedSurveyPoints(AppCommandState& st, float bx, float by, float sc) {
  std::vector<int> ix = st.selectedSurveyPointIndices;
  std::sort(ix.begin(), ix.end());
  ix.erase(std::unique(ix.begin(), ix.end()), ix.end());
  for (int i : ix) {
    if (i < 0 || static_cast<size_t>(i) >= st.surveyPoints.size())
      continue;
    float x = st.surveyPoints[static_cast<size_t>(i)].easting;
    float y = st.surveyPoints[static_cast<size_t>(i)].northing;
    ScalePtAroundBase(bx, by, sc, &x, &y);
    st.surveyPoints[static_cast<size_t>(i)].easting = x;
    st.surveyPoints[static_cast<size_t>(i)].northing = y;
  }
  for (int i : ix) {
    if (i >= 0 && static_cast<size_t>(i) < st.surveyPoints.size())
      RepositionSurveyLabelMtextForPoint(st, static_cast<size_t>(i));
  }
}

void ApplyScaleToSelection(AppCommandState& st, float bx, float by, float sc, std::vector<std::string>& log) {
  if (!(sc > 0.f) || !std::isfinite(sc))
    return;
  DropSurfacesFromSelectionForTransform(st, "SCALE", log);
  std::vector<bool> lineMark(std::max<size_t>(1, st.userLinesFlat.size() / 6), false);
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::LineSeg)
      continue;
    if (e.index >= 0 && static_cast<size_t>(e.index) < lineMark.size())
      lineMark[static_cast<size_t>(e.index)] = true;
  }
  if (!lineMark.empty()) {
    for (size_t i = 0; i < lineMark.size(); ++i) {
      if (!lineMark[i])
        continue;
      size_t k = i * 6;
      if (k + 5 < st.userLinesFlat.size()) {
        ScalePtAroundBase(bx, by, sc, &st.userLinesFlat[k], &st.userLinesFlat[k + 1]);
        ScalePtAroundBase(bx, by, sc, &st.userLinesFlat[k + 3], &st.userLinesFlat[k + 4]);
      }
    }
  }
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::Circle)
      continue;
    size_t k = static_cast<size_t>(e.index) * 4;
    if (k + 3 < st.userCirclesCxCyZR.size()) {
      ScalePtAroundBase(bx, by, sc, &st.userCirclesCxCyZR[k], &st.userCirclesCxCyZR[k + 1]);
      st.userCirclesCxCyZR[k + 3] *= sc;
    }
  }
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::Arc)
      continue;
    const size_t k = static_cast<size_t>(e.index);
    if (k >= st.userArcs.size())
      continue;
    CadArc& a = st.userArcs[k];
    ScalePtAroundBase(bx, by, sc, &a.cx, &a.cy);
    a.r *= sc;
  }
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::Ellipse)
      continue;
    const size_t k = static_cast<size_t>(e.index);
    if (k >= st.userEllipses.size())
      continue;
    CadEllipse& el = st.userEllipses[k];
    float mx = el.cx + el.majVx;
    float my = el.cy + el.majVy;
    ScalePtAroundBase(bx, by, sc, &el.cx, &el.cy);
    ScalePtAroundBase(bx, by, sc, &mx, &my);
    el.majVx = mx - el.cx;
    el.majVy = my - el.cy;
  }
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::Polyline)
      continue;
    const int pi = e.index;
    if (pi < 0 || static_cast<size_t>(pi + 1) >= st.userPolylineOffsets.size())
      continue;
    const int v0 = st.userPolylineOffsets[static_cast<size_t>(pi)];
    const int v1 = st.userPolylineOffsets[static_cast<size_t>(pi + 1)];
    for (int vi = v0; vi < v1; ++vi)
      ScalePtAroundBase(bx, by, sc, &st.userPolylineVerts[static_cast<size_t>(vi * 3 + 0)],
                        &st.userPolylineVerts[static_cast<size_t>(vi * 3 + 1)]);
  }
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::Annotation)
      continue;
    const size_t k = static_cast<size_t>(e.index);
    if (k >= st.cadAnnotations.size())
      continue;
    CadAnnotation& a = st.cadAnnotations[k];
    if (a.kind == CadAnnotation::Kind::Text) {
      ScalePtAroundBase(bx, by, sc, &a.insX, &a.insY);
      a.plottedHeightInches = std::max(a.plottedHeightInches * sc, 1.e-6f);
    } else if (a.kind == CadAnnotation::Kind::Mtext) {
      ScalePtAroundBase(bx, by, sc, &a.boxMinX, &a.boxMinY);
      ScalePtAroundBase(bx, by, sc, &a.boxMaxX, &a.boxMaxY);
      if (a.boxMinX > a.boxMaxX)
        std::swap(a.boxMinX, a.boxMaxX);
      if (a.boxMinY > a.boxMaxY)
        std::swap(a.boxMinY, a.boxMaxY);
      a.insX = a.boxMinX;
      a.insY = a.boxMinY;
      a.plottedHeightInches = std::max(a.plottedHeightInches * sc, 1.e-6f);
    } else if (a.kind == CadAnnotation::Kind::DimLinear) {
      ScaleCadDimLinearAroundBase(bx, by, sc, &a);
      ScalePtAroundBase(bx, by, sc, &a.insX, &a.insY);
      a.plottedHeightInches = std::max(a.plottedHeightInches * sc, 1.e-6f);
      CadDimRefreshMeasurementText(&a, st.displayLinearPrecision, CadAngleDisplaySettings(st));
    } else if (a.kind == CadAnnotation::Kind::DimAligned) {
      ScalePtAroundBase(bx, by, sc, &a.dimExt1X, &a.dimExt1Y);
      ScalePtAroundBase(bx, by, sc, &a.dimExt2X, &a.dimExt2Y);
      a.dimSignedOffset *= sc;
      ScalePtAroundBase(bx, by, sc, &a.insX, &a.insY);
      a.plottedHeightInches = std::max(a.plottedHeightInches * sc, 1.e-6f);
      float sx1 = 0.f, sy1 = 0.f, sx2 = 0.f, sy2 = 0.f, tx = 0.f, ty = 0.f, nx = 0.f, ny = 0.f, ml = 0.f;
      if (CadDimAlignedGeometry(a, &sx1, &sy1, &sx2, &sy2, &tx, &ty, &nx, &ny, &ml))
        a.rotationRad = std::atan2(ty, tx);
      CadDimRefreshMeasurementText(&a, st.displayLinearPrecision, CadAngleDisplaySettings(st));
    } else if (a.kind == CadAnnotation::Kind::DimAngular) {
      ScalePtAroundBase(bx, by, sc, &a.dimAngVertexX, &a.dimAngVertexY);
      ScalePtAroundBase(bx, by, sc, &a.dimExt1X, &a.dimExt1Y);
      ScalePtAroundBase(bx, by, sc, &a.dimExt2X, &a.dimExt2Y);
      a.dimSignedOffset *= sc;
      ScalePtAroundBase(bx, by, sc, &a.insX, &a.insY);
      a.plottedHeightInches = std::max(a.plottedHeightInches * sc, 1.e-6f);
      CadDimAngularSyncTextPlacement(&a, st.modelUnitsPerPlottedInch);
      CadDimRefreshMeasurementText(&a, st.displayLinearPrecision, CadAngleDisplaySettings(st));
    }
  }
  // PDF underlays: scale insertion point around base; multiply uniform scale factor.
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::PdfUnderlay)
      continue;
    if (e.index < 0 || static_cast<size_t>(e.index) >= st.pdfAttachments.size())
      continue;
    PdfAttachment& att = st.pdfAttachments[static_cast<size_t>(e.index)];
    ScalePtAroundBase(bx, by, sc, &att.insertX, &att.insertY);
    att.scale = std::max(att.scale * sc, 1e-9f);
  }
  // Feature lines (REQ-087). Plan only — elevations are NOT scaled, matching the polyline above.
  TransformSelectedFeatureLinesInPlace(
      st, [&](float* x, float* y) { ScalePtAroundBase(bx, by, sc, x, y); });
  ApplyScaleToSelectedSurveyPoints(st, bx, by, sc);
  BumpCadGpuCache(st);
}

bool ParseAngleDegreesInternal(const std::string& raw, float* degreesOut) {
  std::string s = StringUtil::trimCopy(raw);
  if (s.empty())
    return false;
  std::string low = StringUtil::toLowerAsciiCopy(s);
  bool neg = false;
  if (!low.empty() && low[0] == '-') {
    neg = true;
    low = StringUtil::trimCopy(low.substr(1));
  }
  if (!low.empty() && low[0] == '+')
    low = StringUtil::trimCopy(low.substr(1));
  float deg = 0.f;
  float min = 0.f;
  float sec = 0.f;
  size_t pd = low.find('d');
  if (pd != std::string::npos) {
    if (!(std::istringstream(low.substr(0, pd)) >> deg))
      return false;
    std::string rest = low.substr(pd + 1);
    size_t pm = rest.find('m');
    if (pm != std::string::npos) {
      if (!(std::istringstream(rest.substr(0, pm)) >> min))
        return false;
      std::string rest2 = rest.substr(pm + 1);
      size_t ps = rest2.find('s');
      if (ps != std::string::npos) {
        if (!(std::istringstream(rest2.substr(0, ps)) >> sec))
          return false;
      } else {
        if (!(std::istringstream(rest2) >> sec))
          sec = 0.f;
      }
    } else {
      if (!(std::istringstream(rest) >> min))
        min = 0.f;
    }
    *degreesOut = (neg ? -1.f : 1.f) * (deg + min / 60.f + sec / 3600.f);
    return true;
  }
  if (!(std::istringstream(low) >> deg))
    return false;
  *degreesOut = neg ? -deg : deg;
  return true;
}

bool HandleModifyText(AppCommandState& st, bool isCopy, const std::string& lineIn, std::vector<std::string>& log) {
  std::string line = StringUtil::trimCopy(lineIn);
  using MP = AppCommandState::ModifyPhase;
  if (st.modifyPhase == MP::NeedBase) {
    float px = 0.f;
    float py = 0.f;
    if (!ParseStoragePoint(st, line, &px, &py, false, 0.f, 0.f))
      return false;
    st.modifyBaseX = px;
    st.modifyBaseY = py;
    st.modifyPhase = MP::NeedDestination;
    log.push_back(isCopy ? "COPY — specify second point (destination)." : "MOVE — specify second point (destination).");
    return true;
  }
  if (st.modifyPhase == MP::NeedDestination) {
    float px = 0.f;
    float py = 0.f;
    if (!ParseStoragePoint(st, line, &px, &py, true, st.modifyBaseX, st.modifyBaseY))
      return false;
    float dx = px - st.modifyBaseX;
    float dy = py - st.modifyBaseY;
    PushUndoSnapshot(st, isCopy ? "Copy" : "Move");
    if (isCopy)
      FinalizeCopyTranslation(st, dx, dy, log);
    else {
      ApplyTranslationToSelection(st, dx, dy, log);
      // Stay in MOVE — same selection at new position, ready for another base+destination.
      st.modifyPhase = AppCommandState::ModifyPhase::NeedBase;
      log.push_back("MOVE complete — base point (ESC to exit):");
    }
    return true;
  }
  (void)log;
  return false;
}

static void FinishScaleCommand(AppCommandState& st, float scaleFactor, std::vector<std::string>& log) {
  PushUndoSnapshot(st, "Scale");
  const float s = std::max(scaleFactor, 1e-6f);
  ApplyScaleToSelection(st, st.modifyBaseX, st.modifyBaseY, s, log);
  st.active = AppCommandState::Kind::None;
  ResetModifyRotateDraft(st);
  log.push_back("SCALE complete.");
}

static bool HandleScaleText(AppCommandState& st, const std::string& lineIn, std::vector<std::string>& log) {
  std::string line = StringUtil::trimCopy(lineIn);
  using MP = AppCommandState::ModifyPhase;
  using SP = AppCommandState::ScalePhase;
  if (st.modifyPhase == MP::NeedBase) {
    float px = 0.f;
    float py = 0.f;
    if (!ParseStoragePoint(st, line, &px, &py, false, 0.f, 0.f))
      return false;
    st.modifyBaseX = px;
    st.modifyBaseY = py;
    st.scaleRefDist = ComputeScaleReferenceDistance(st, px, py);
    st.scalePhase = SP::FactorPick;
    st.modifyPhase = MP::NeedDestination;
    log.push_back(
        "SCALE — pick second point or type factor (>0), or R / REFERENCE for two-point reference length then new "
        "length.");
    return true;
  }
  if (st.modifyPhase != MP::NeedDestination)
    return false;

  switch (st.scalePhase) {
  case SP::FactorPick: {
    const std::string low = StringUtil::toLowerAsciiCopy(line);
    if (low == "r" || low == "ref" || low == "reference") {
      st.scalePhase = SP::Ref_WaitP1;
      log.push_back("SCALE ref — first point of reference length:");
      return true;
    }
    float sf = 0.f;
    if (ParseOneFloat(line, &sf)) {
      if (!(sf > 0.f) || !std::isfinite(sf)) {
        log.push_back("SCALE — scale factor must be a positive finite number.");
        return false;
      }
      FinishScaleCommand(st, sf, log);
      return true;
    }
    float px = 0.f;
    float py = 0.f;
    if (!ParseStoragePoint(st, line, &px, &py, true, st.modifyBaseX, st.modifyBaseY))
      return false;
    const float d = std::hypot(px - st.modifyBaseX, py - st.modifyBaseY);
    FinishScaleCommand(st, d / std::max(st.scaleRefDist, 1e-20f), log);
    return true;
  }
  case SP::Ref_WaitP1: {
    float px = 0.f;
    float py = 0.f;
    if (!ParseStoragePoint(st, line, &px, &py, false, 0.f, 0.f))
      return false;
    st.scaleRefP1X = px;
    st.scaleRefP1Y = py;
    st.scalePhase = SP::Ref_WaitP2;
    log.push_back("SCALE ref — second point of reference length:");
    return true;
  }
  case SP::Ref_WaitP2: {
    float px = 0.f;
    float py = 0.f;
    if (!ParseStoragePoint(st, line, &px, &py, false, 0.f, 0.f))
      return false;
    const float refLen = std::hypot(px - st.scaleRefP1X, py - st.scaleRefP1Y);
    if (!(refLen > 1e-8f) || !std::isfinite(refLen)) {
      log.push_back("SCALE ref — reference length is too small; pick two distinct points.");
      return false;
    }
    st.scaleRefDist = refLen;
    st.scalePhase = SP::NewLength_WaitTypedOrP1;
    log.push_back("SCALE ref — type new length (model units) or pick first point of new length segment.");
    return true;
  }
  case SP::NewLength_WaitTypedOrP1: {
    float L = 0.f;
    if (ParseOneFloat(line, &L)) {
      if (!(L > 0.f) || !std::isfinite(L)) {
        log.push_back("SCALE ref — new length must be a positive finite number.");
        return false;
      }
      FinishScaleCommand(st, L / std::max(st.scaleRefDist, 1e-20f), log);
      return true;
    }
    float px = 0.f;
    float py = 0.f;
    if (!ParseStoragePoint(st, line, &px, &py, false, 0.f, 0.f))
      return false;
    st.scaleNewLenP1X = px;
    st.scaleNewLenP1Y = py;
    st.scalePhase = SP::NewLength_WaitP2;
    log.push_back("SCALE ref — second point of new length segment:");
    return true;
  }
  case SP::NewLength_WaitP2: {
    float px = 0.f;
    float py = 0.f;
    if (!ParseStoragePoint(st, line, &px, &py, false, 0.f, 0.f))
      return false;
    const float newLen = std::hypot(px - st.scaleNewLenP1X, py - st.scaleNewLenP1Y);
    if (!(newLen > 1e-8f) || !std::isfinite(newLen)) {
      log.push_back("SCALE ref — new length is too small; pick two distinct points.");
      return false;
    }
    FinishScaleCommand(st, newLen / std::max(st.scaleRefDist, 1e-20f), log);
    return true;
  }
  default:
    return false;
  }
}

static bool TryRotateCopyToggle(AppCommandState& st, const std::string& lineIn, std::vector<std::string>& log) {
  const std::string low = StringUtil::toLowerAsciiCopy(StringUtil::trimCopy(lineIn));
  if (low != "c" && low != "copy")
    return false;
  st.rotateCopyMode = !st.rotateCopyMode;
  log.push_back(st.rotateCopyMode ? "ROTATE — copy mode on (original kept)." : "ROTATE — copy mode off.");
  return true;
}

static void FinishRotateCommand(AppCommandState& st, float bx, float by, float rad, std::vector<std::string>& log) {
  PushUndoSnapshot(st, st.rotateCopyMode ? "Rotate-copy" : "Rotate");
  using K = AppCommandState::Kind;
  if (st.rotateCopyMode) {
    DuplicateCadSelectionRotated(st, bx, by, rad);
    st.rotateCopyMode = false;
    st.active = K::None;
    ResetModifyRotateDraft(st);
    if (!st.selectedSurveyPointIndices.empty()) {
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
    ApplyRotationToSelection(st, bx, by, rad, log);
    st.active = K::None;
    ResetModifyRotateDraft(st);
    log.push_back("ROTATE complete.");
  }
}

bool HandleRotateText(AppCommandState& st, const std::string& lineIn, std::vector<std::string>& log) {
  std::string line = StringUtil::trimCopy(lineIn);
  using RP = AppCommandState::RotatePhase;
  constexpr float kDegToRad = 0.01745329251994329577f;

  if (st.rotatePhase == RP::NeedBase) {
    float px = 0.f;
    float py = 0.f;
    if (!ParseStoragePoint(st, line, &px, &py, false, 0.f, 0.f))
      return false;
    st.rotateBaseX = px;
    st.rotateBaseY = py;
    st.rotatePhase = RP::NeedAngleOrReference;
    log.push_back("ROTATE — ° clockwise from north (decimal/DMS), R reference, C copy — click-drag preview.");
    return true;
  }

  if (st.rotatePhase == RP::NeedAngleOrReference) {
    if (TryRotateCopyToggle(st, line, log))
      return true;
    std::string low = StringUtil::toLowerAsciiCopy(line);
    if (low == "r" || low == "ref" || low == "reference") {
      st.rotatePhase = RP::Ref_WaitP1;
      log.push_back("Reference — first point:");
      return true;
    }
    float deg = 0.f;
    if (!ParseAngleDegreesInternal(line, &deg))
      return false;
    // Clockwise-from-north degrees → internal CCW-positive rotation used by RotateAroundBase.
    FinishRotateCommand(st, st.rotateBaseX, st.rotateBaseY, -deg * kDegToRad, log);
    return true;
  }

  if (st.rotatePhase == RP::Ref_WaitP1) {
    if (TryRotateCopyToggle(st, line, log))
      return true;
    float px = 0.f;
    float py = 0.f;
    if (!ParseStoragePoint(st, line, &px, &py, false, 0.f, 0.f))
      return false;
    st.rotateRefX1 = px;
    st.rotateRefY1 = py;
    st.rotatePhase = RP::Ref_WaitP2;
    log.push_back("Reference — second point:");
    return true;
  }

  if (st.rotatePhase == RP::Ref_WaitP2) {
    if (TryRotateCopyToggle(st, line, log))
      return true;
    float px = 0.f;
    float py = 0.f;
    if (!ParseStoragePoint(st, line, &px, &py, false, 0.f, 0.f))
      return false;
    st.rotateRefX2 = px;
    st.rotateRefY2 = py;
    st.rotatePhase = RP::AfterReference_WaitAngleOrP;
    log.push_back(
        "Enter new bearing from north ° (decimal/DMS — matches properties), or P for two-point line (C toggles copy).");
    return true;
  }

  if (st.rotatePhase == RP::AfterReference_WaitAngleOrP) {
    std::string low = StringUtil::toLowerAsciiCopy(line);
    if (low == "p") {
      st.rotatePhase = RP::AnglePoints_WaitP1;
      log.push_back("Angle — first point:");
      return true;
    }
    if (TryRotateCopyToggle(st, line, log))
      return true;
    float deg = 0.f;
    if (!ParseAngleDegreesInternal(line, &deg))
      return false;
    const float thetaRef =
        std::atan2(st.rotateRefY2 - st.rotateRefY1, st.rotateRefX2 - st.rotateRefX1);
    // Degrees are clockwise-from-north bearing (same as properties), matching atan2(dx,dy) convention.
    const float targetMath = MathAngleRadFromBearingCwNorthDeg(deg);
    const float delta = NormalizeAngleRadMinusPiToPi(targetMath - thetaRef);
    FinishRotateCommand(st, st.rotateBaseX, st.rotateBaseY, delta, log);
    return true;
  }

  if (st.rotatePhase == RP::AnglePoints_WaitP1) {
    if (TryRotateCopyToggle(st, line, log))
      return true;
    float px = 0.f;
    float py = 0.f;
    if (!ParseStoragePoint(st, line, &px, &py, false, 0.f, 0.f))
      return false;
    st.rotateAnglePt1X = px;
    st.rotateAnglePt1Y = py;
    st.rotatePhase = RP::AnglePoints_WaitP2;
    log.push_back("Angle — second point:");
    return true;
  }

  if (st.rotatePhase == RP::AnglePoints_WaitP2) {
    if (TryRotateCopyToggle(st, line, log))
      return true;
    float px = 0.f;
    float py = 0.f;
    if (!ParseStoragePoint(st, line, &px, &py, false, 0.f, 0.f))
      return false;
    const float delta = RotateDeltaFromReferenceAndNewSegment(st.rotateRefX1, st.rotateRefY1, st.rotateRefX2,
                                                               st.rotateRefY2, st.rotateAnglePt1X,
                                                               st.rotateAnglePt1Y, px, py);
    FinishRotateCommand(st, st.rotateBaseX, st.rotateBaseY, delta, log);
    return true;
  }

  return false;
}

static void ComputeArcSweepRad(double ox, double oy, double ax, double ay, double bx, double by, double cx,
                               double cy, double* startRad, double* sweepRad) {
  constexpr double twopi = 6.28318530717958647692;
  auto normPos = [](double x) {
    double r = std::fmod(x, twopi);
    if (r < 0)
      r += twopi;
    return r;
  };
  const double ta = std::atan2(ay - oy, ax - ox);
  const double tb = std::atan2(by - oy, bx - ox);
  const double tc = std::atan2(cy - oy, cx - ox);
  const double arc_ab = normPos(tb - ta);
  const double arc_ac = normPos(tc - ta);
  const bool useCcw = arc_ab <= arc_ac + 1e-10;
  double sweep = useCcw ? arc_ac : arc_ac - twopi;
  if (std::fabs(sweep) < 1e-12)
    sweep = twopi;
  *startRad = ta;
  *sweepRad = sweep;
}

static void CommitArcThreePoints(AppCommandState& st, float ax, float ay, float bx, float by, float cx, float cy,
                                 std::vector<std::string>& log) {
  float ox = 0.f, oy = 0.f, r = 0.f;
  if (!ComputeCircumcircle(ax, ay, bx, by, cx, cy, &ox, &oy, &r) || r < 1e-8f) {
    log.push_back("ARC — points are collinear.");
    st.active = AppCommandState::Kind::None;
    ResetArcDraft(st);
    return;
  }
  double sr = 0.;
  double sw = 0.;
  ComputeArcSweepRad(ox, oy, ax, ay, bx, by, cx, cy, &sr, &sw);
  CadArc arc{};
  arc.cx = ox;
  arc.cy = oy;
  arc.r = r;
  arc.startRad = static_cast<float>(sr);
  arc.sweepRad = static_cast<float>(sw);
  arc.z = CadCommitElevation(st);  // lands on the active work plane (REQ-058)
  PushUndoSnapshot(st, "Arc");
  st.userArcs.push_back(arc);
  st.userArcAttrs.push_back(MakeNewEntityAttrs(st));
  BumpCadGpuCache(st);
  st.active = AppCommandState::Kind::None;
  ResetArcDraft(st);
  log.push_back("ARC complete.");
}

static void CommitDimAlignedAt(AppCommandState& st, float lx, float ly, std::vector<std::string>& log) {
  const float x1 = st.dimE1x, y1 = st.dimE1y;
  const float x2 = st.dimE2x, y2 = st.dimE2y;
  float vx = x2 - x1;
  float vy = y2 - y1;
  const float len = std::hypot(vx, vy);
  if (len < 1e-8f) {
    log.push_back("DIMALIGNED — extension points coincide.");
    return;
  }
  vx /= len;
  vy /= len;
  const float t1 = (x1 - lx) * vx + (y1 - ly) * vy;
  const float t2 = (x2 - lx) * vx + (y2 - ly) * vy;
  const float sx1 = lx + vx * t1;
  const float sy1 = ly + vy * t1;
  const float sx2 = lx + vx * t2;
  const float sy2 = ly + vy * t2;
  const float cmx = 0.5f * (x1 + x2);
  const float cmy = 0.5f * (y1 + y2);
  const float n0x = -vy;
  const float n0y = vx;
  const float dmx = 0.5f * (sx1 + sx2);
  const float dmy = 0.5f * (sy1 + sy2);
  const float dOff = (dmx - cmx) * n0x + (dmy - cmy) * n0y;
  char buf[96];
  std::snprintf(buf, sizeof(buf), "%.4f", static_cast<double>(len));
  CadAnnotation ann;
  ann.kind = CadAnnotation::Kind::DimAligned;
  ann.insZ = CadCommitElevation(st);  // lands on the active work plane (REQ-058), as TEXT does
  ann.dimExt1X = x1;
  ann.dimExt1Y = y1;
  ann.dimExt2X = x2;
  ann.dimExt2Y = y2;
  ann.dimSignedOffset = dOff;
  ann.plottedHeightInches = st.defaultPlottedTextHeightInches * 0.85f;
  ann.rotationRad = std::atan2(vy, vx);
  ann.text = buf;
  const float hWorld = CadAnnotationHeightWorld(ann, st.modelUnitsPerPlottedInch);
  CadDimAlignedPlaceTextBeyondDimLine(cmx, cmy, dmx, dmy, n0x, n0y, hWorld, &ann.insX, &ann.insY);
  EntityAttributes at = MakeNewEntityAttrs(st);
  at.color = "#e1b12c";
  PushUndoSnapshot(st, "DIMALIGNED");
  st.cadAnnotations.push_back(std::move(ann));
  st.cadAnnotationAttrs.push_back(at);
  BumpCadGpuCache(st);
  ResetDimDraft(st);
  log.push_back("DIMALIGNED complete.");
  log.push_back("DIMALIGNED — extension 1, extension 2, then offset. ESC to exit.");
}

static void CommitDimLinearAt(AppCommandState& st, float lx, float ly, std::vector<std::string>& log) {
  CadDimLinearUpdateDraftOrientation(st, lx, ly);
  const float x1 = st.dimE1x, y1 = st.dimE1y;
  const float x2 = st.dimE2x, y2 = st.dimE2y;
  const float cmx = 0.5f * (x1 + x2);
  const float cmy = 0.5f * (y1 + y2);
  const bool vert = st.dimLinearDraftVertical;
  const float meas = vert ? std::fabs(y2 - y1) : std::fabs(x2 - x1);
  if (meas < 1e-8f) {
    log.push_back(vert ? "DIMLINEAR — extension points have the same Y (zero vertical span)."
                       : "DIMLINEAR — extension points have the same X (zero horizontal span).");
    return;
  }
  float dmx = cmx;
  float dmy = cmy;
  float n0x = 0.f;
  float n0y = 1.f;
  float dOff = 0.f;
  if (!vert) {
    dmy = ly;
    dOff = dmy - cmy;
  } else {
    dmx = lx;
    dOff = dmx - cmx;
    n0x = 1.f;
    n0y = 0.f;
  }
  char buf[96];
  std::snprintf(buf, sizeof(buf), "%.4f", static_cast<double>(meas));
  CadAnnotation ann;
  ann.kind = CadAnnotation::Kind::DimLinear;
  ann.insZ = CadCommitElevation(st);  // lands on the active work plane (REQ-058), as TEXT does
  ann.dimExt1X = x1;
  ann.dimExt1Y = y1;
  ann.dimExt2X = x2;
  ann.dimExt2Y = y2;
  ann.dimSignedOffset = dOff;
  ann.dimLinearVertical = vert;
  ann.plottedHeightInches = st.defaultPlottedTextHeightInches * 0.85f;
  float tx = 0.f, ty = 0.f;
  if (!vert) {
    tx = (x2 >= x1) ? 1.f : -1.f;
    ty = 0.f;
  } else {
    tx = 0.f;
    ty = (y2 >= y1) ? 1.f : -1.f;
  }
  ann.rotationRad = std::atan2(ty, tx);
  ann.text = buf;
  const float hWorld = CadAnnotationHeightWorld(ann, st.modelUnitsPerPlottedInch);
  CadDimAlignedPlaceTextBeyondDimLine(cmx, cmy, dmx, dmy, n0x, n0y, hWorld, &ann.insX, &ann.insY);
  EntityAttributes at = MakeNewEntityAttrs(st);
  at.color = "#e1b12c";
  PushUndoSnapshot(st, "DIMLINEAR");
  st.cadAnnotations.push_back(std::move(ann));
  st.cadAnnotationAttrs.push_back(at);
  BumpCadGpuCache(st);
  ResetDimDraft(st);
  log.push_back("DIMLINEAR complete.");
  log.push_back("DIMLINEAR — extension 1, extension 2, then dimension line. ESC to exit.");
}

static void CommitIdPointAt(AppCommandState& st, float lx, float ly, std::vector<std::string>& log) {
  double wx = 0.;
  double wy = 0.;
  CadCoord::WorldFromLocal(st, lx, ly, &wx, &wy);
  const int p = st.displayLinearPrecision;
  char buf[256];
  std::snprintf(buf, sizeof(buf), "ID — UCS (World)  X = %s  Y = %s  Z = %s",
                FormatLinear(wx, p).c_str(), FormatLinear(wy, p).c_str(), FormatLinear(0.0, p).c_str());
  log.push_back(buf);
  st.active = AppCommandState::Kind::None;
}

static void CommitSurveyInverseSecondPoint(AppCommandState& st, float x2, float y2, std::vector<std::string>& log) {
  using K = AppCommandState::Kind;
  using SIP = AppCommandState::SurveyInversePhase;
  const float de = x2 - st.surveyInverseFromX;
  const float dn = y2 - st.surveyInverseFromY;
  const float horiz = std::hypot(de, dn);
  if (horiz < 1e-10f) {
    log.push_back("INVERSE — horizontal distance is zero; pick a different second point.");
    return;
  }
  const float theta = std::atan2(dn, de);
  const float brg = BearingCwNorthDegFromMathAngleRad(theta);
  const std::string brgStr = FormatBearing(static_cast<double>(brg), CadAngleDisplaySettings(st));
  const int p = st.displayLinearPrecision;
  char buf[512];
  std::snprintf(buf, sizeof(buf), "INVERSE — ΔE = %s  ΔN = %s  horiz dist = %s  bearing = %s",
                FormatLinear(static_cast<double>(de), p).c_str(), FormatLinear(static_cast<double>(dn), p).c_str(),
                FormatLinear(static_cast<double>(horiz), p).c_str(), brgStr.c_str());
  log.push_back(buf);
  st.active = K::None;
  st.surveyInversePhase = SIP::WaitFrom;
}

// --- REQ-074 spot elevation and grade ----------------------------------------------------------

/// Interpolated elevation of every surface covering (\p x, \p y), paired with its name.
///
/// **Every** covering surface, not one of them (TASK-055 Q1): overlapping surfaces are the existing
/// vs proposed case, which is the grading question this command exists to answer, and a bare number
/// from an unnamed surface would be worse than no number at all.
///
/// Invisible surfaces are skipped, via the shared \ref SurfaceVisible — the readout should describe
/// the surfaces the user can see, and REQ-068 already established that rule. Routing through the
/// shared predicate also fixed a real gap here: this walk checked layer on/frozen but not
/// `hiddenEntityIds`, so SURFELEV reported an elevation from a surface REQ-084 (d) had isolated out.
static std::vector<std::pair<std::string, double>> SurfaceElevationsAt(const AppCommandState& st, double x,
                                                                       double y) {
  std::vector<std::pair<std::string, double>> out;
  for (size_t si = 0; si < st.cadSurfaces.size(); ++si) {
    if (!SurfaceVisible(st, si))
      continue;
    const CadSurface& s = st.cadSurfaces[si];
    double z = 0.0;
    if (TinElevationAt(s.tin->vertsXyz, s.tin->indices, x, y, &z))
      out.emplace_back(s.name, z);
  }
  return out;
}

/// First pick: report the elevation on each covering surface, and remember them for the grade.
static void ReportSurfaceElevationAt(AppCommandState& st, double x, double y, std::vector<std::string>& log) {
  st.surfaceElevFromX = x;
  st.surfaceElevFromY = y;
  st.surfaceElevFromZ = SurfaceElevationsAt(st, x, y);

  if (st.cadSurfaces.empty()) {
    log.push_back("SURFELEV — there are no surfaces in the drawing. Build one from a point group first.");
    return;
  }
  if (st.surfaceElevFromZ.empty()) {
    // REQ-074: "a pick outside the surface reports that it is outside; it never extrapolates."
    log.push_back("SURFELEV — outside surface. No elevation at that point.");
    return;
  }
  const int p = st.displayLinearPrecision;
  for (const auto& e : st.surfaceElevFromZ)
    log.push_back("SURFELEV — " + e.first + ": elevation " + FormatLinear(e.second, p));
}

/// Second pick: grade from the first, computed **within each surface**, never across two.
static void ReportSurfaceGradeTo(AppCommandState& st, double x, double y, std::vector<std::string>& log) {
  using K = AppCommandState::Kind;
  using SEP = AppCommandState::SurfaceElevPhase;
  const int p = st.displayLinearPrecision;

  const double dx = x - st.surfaceElevFromX;
  const double dy = y - st.surfaceElevFromY;
  const double run = std::hypot(dx, dy);

  // REQ-074: "two picks at the same location report zero distance rather than dividing by zero."
  // The threshold is kTinPlanEpsilon, not an arbitrary epsilon: below it the project already
  // considers two positions to be the same site (it is what BuildTin de-duplicates on).
  if (run < kTinPlanEpsilon) {
    log.push_back("SURFELEV — both picks are at the same location: horizontal distance 0. No grade.");
    st.active = K::None;
    st.surfaceElevPhase = SEP::WaitFirst;
    st.surfaceElevFromZ.clear();
    return;
  }

  const std::vector<std::pair<std::string, double>> to = SurfaceElevationsAt(st, x, y);
  bool reportedAny = false;
  for (const auto& from : st.surfaceElevFromZ) {
    // `from.first` rather than a structured binding: capturing one in the lambda below is a C++20
    // extension, and this file is built as C++17.
    const std::string& name = from.first;
    const double z1 = from.second;
    const auto it = std::find_if(to.begin(), to.end(), [&](const auto& e) { return e.first == name; });
    if (it == to.end()) {
      log.push_back("SURFELEV — " + name + ": second point is outside this surface. No grade.");
      continue;
    }
    const double rise = it->second - z1;
    const double slopePct = rise / run * 100.0;
    char buf[320];
    if (std::abs(rise) < 1e-9) {
      // Flat: a run:rise ratio would be a division by zero, and "level" is what a surveyor would
      // write on the sheet anyway.
      std::snprintf(buf, sizeof(buf), "SURFELEV — %s: level (0.00%%)  horiz %s  vert %s", name.c_str(),
                    FormatLinear(run, p).c_str(), FormatLinear(rise, p).c_str());
    } else {
      // Both conventions, because both are used: percent for the grade, run:rise for the slope.
      std::snprintf(buf, sizeof(buf), "SURFELEV — %s: grade %.2f%%  slope %.2f:1  horiz %s  vert %s",
                    name.c_str(), slopePct, run / std::abs(rise), FormatLinear(run, p).c_str(),
                    FormatLinear(rise, p).c_str());
    }
    log.push_back(buf);
    reportedAny = true;
  }
  if (!reportedAny && st.surfaceElevFromZ.empty())
    log.push_back("SURFELEV — outside surface at the first point; no grade to report.");

  st.active = K::None;
  st.surfaceElevPhase = SEP::WaitFirst;
  st.surfaceElevFromZ.clear();
}

namespace OffsetCmd {

static void ResetOffsetDraft(AppCommandState& st) {
  st.offsetEntityValid = false;
  st.offsetEntity = {};
  st.offsetTypedDistance = 0.f;
  st.offsetPhase = AppCommandState::OffsetPhase::WaitSelectEntity;
  st.offsetHoverHighlightValid = false;
  st.offsetHoverEntity = {};
}

static void ClosestPointOnSegment(float ax, float ay, float bx, float by, float px, float py, float* qx,
                                  float* qy) {
  const float vx = bx - ax;
  const float vy = by - ay;
  const float len2 = vx * vx + vy * vy;
  if (len2 < 1e-18f) {
    *qx = ax;
    *qy = ay;
    return;
  }
  const float t = std::clamp(((px - ax) * vx + (py - ay) * vy) / len2, 0.f, 1.f);
  *qx = ax + t * vx;
  *qy = ay + t * vy;
}

static bool LineLineIntersectInf(float ax, float ay, float bx, float by, float cx, float cy, float dx, float dy,
                                 float* ox, float* oy) {
  const float rx = bx - ax, ry = by - ay;
  const float sx = dx - cx, sy = dy - cy;
  const float det = rx * sy - ry * sx;
  if (std::fabs(det) < 1e-12f * std::max(1.f, std::hypot(rx, ry) * std::hypot(sx, sy)))
    return false;
  const float t = ((cx - ax) * sy - (cy - ay) * sx) / det;
  *ox = ax + t * rx;
  *oy = ay + t * ry;
  return true;
}

static void UnitLeftNormal(float ax, float ay, float bx, float by, float* nx, float* ny) {
  float vx = bx - ax;
  float vy = by - ay;
  const float len = std::hypot(vx, vy);
  if (len < 1e-12f) {
    *nx = 0.f;
    *ny = 1.f;
    return;
  }
  vx /= len;
  vy /= len;
  *nx = -vy;
  *ny = vx;
}

static float SignedSideLine(float ax, float ay, float bx, float by, float px, float py) {
  float qx = 0.f, qy = 0.f;
  ClosestPointOnSegment(ax, ay, bx, by, px, py, &qx, &qy);
  float nx = 0.f, ny = 0.f;
  UnitLeftNormal(ax, ay, bx, by, &nx, &ny);
  return (px - qx) * nx + (py - qy) * ny;
}

static float SignedSideCircle(float cx, float cy, float r, float px, float py) {
  const float d = std::hypot(px - cx, py - cy);
  return d - r;
}

/// Append the attribute row for an OFFSET copy of the entity at \p srcIndex within \p attrs.
///
/// The copy **inherits** layer, colour, linetype, lineweight and transparency from its source —
/// that is what makes it read as an offset of that entity rather than a stranger on the current
/// layer. It must **not** inherit the source's id.
///
/// Ids are unique and never reused within a drawing (REQ-076), and architecture §11.9 builds on
/// that: a reference from one object to another *is* an id, so an id naming two entities makes
/// every such reference ambiguous. \ref EnsureEntityIds only fills ids that are 0, so a copied
/// non-zero id is never repaired — it persists to `.gs` and is permanent. Clearing it here hands
/// assignment back to the sweep, exactly as the clipboard does via \ref ClearEntityIdsFrom
/// (see CopySelectionToClipboard). Every caller bumps the GPU revision the sweep is gated on, so
/// the fresh id is assigned before anything can observe or save the entity.
///
/// One helper rather than the same three lines in five places, deliberately: the defect this fixes
/// (issue #58) was five copies of a correct-looking pattern that were all missing the same step.
static void PushOffsetCopyAttrs(AppCommandState& st, std::vector<EntityAttributes>& attrs,
                                int srcIndex) {
  if (srcIndex >= 0 && static_cast<size_t>(srcIndex) < attrs.size()) {
    attrs.push_back(attrs[static_cast<size_t>(srcIndex)]);
    attrs.back().id = 0;  // REQ-076: assigned fresh by EnsureEntityIds, never inherited
  } else {
    attrs.push_back(MakeNewEntityAttrs(st));  // already id 0
  }
}

static bool CommitOffsetLine(AppCommandState& st, int lineIx, float signedD, std::vector<std::string>& log) {
  const size_t k = static_cast<size_t>(lineIx) * 6;
  if (k + 5 >= st.userLinesFlat.size())
    return false;
  PushUndoSnapshot(st, "Offset line");
  const float x0 = st.userLinesFlat[k];
  const float y0 = st.userLinesFlat[k + 1];
  const float z0 = st.userLinesFlat[k + 2];  // read before push_back may reallocate
  const float x1 = st.userLinesFlat[k + 3];
  const float y1 = st.userLinesFlat[k + 4];
  const float z1 = st.userLinesFlat[k + 5];
  const float dx = x1 - x0;
  const float dy = y1 - y0;
  if (std::hypot(dx, dy) < 1e-8f) {
    log.push_back("OFFSET — zero-length line.");
    return false;
  }
  float nx = 0.f, ny = 0.f;
  UnitLeftNormal(x0, y0, x1, y1, &nx, &ny);
  const float ox0 = x0 + nx * signedD;
  const float oy0 = y0 + ny * signedD;
  const float ox1 = x1 + nx * signedD;
  const float oy1 = y1 + ny * signedD;
  // The offset copy stays on the source line's plane — offsetting an elevated line must not
  // flatten it (REQ-057). The offset itself is horizontal, so each end keeps its own Z.
  st.userLinesFlat.push_back(ox0);
  st.userLinesFlat.push_back(oy0);
  st.userLinesFlat.push_back(z0);
  st.userLinesFlat.push_back(ox1);
  st.userLinesFlat.push_back(oy1);
  st.userLinesFlat.push_back(z1);
  PushOffsetCopyAttrs(st, st.userLineAttrs, lineIx);
  BumpCadGpuCache(st);
  return true;
}

static bool CommitOffsetCircle(AppCommandState& st, int ci, float signedD, std::vector<std::string>& log) {
  const size_t k = static_cast<size_t>(ci) * 4;
  if (k + 3 >= st.userCirclesCxCyZR.size())
    return false;
  PushUndoSnapshot(st, "Offset circle");
  const float cx = st.userCirclesCxCyZR[k];
  const float cy = st.userCirclesCxCyZR[k + 1];
  const float cz = st.userCirclesCxCyZR[k + 2];  // read before any push_back reallocates
  const float r = st.userCirclesCxCyZR[k + 3];
  const float nr = r + signedD;
  if (nr <= 1e-6f) {
    log.push_back("OFFSET — resulting circle radius too small.");
    return false;
  }
  st.userCirclesCxCyZR.push_back(cx);
  st.userCirclesCxCyZR.push_back(cy);
  st.userCirclesCxCyZR.push_back(cz);  // the offset copy stays on the source circle's plane
  st.userCirclesCxCyZR.push_back(nr);
  PushOffsetCopyAttrs(st, st.userCircleAttrs, ci);
  BumpCadGpuCache(st);
  return true;
}

static bool CommitOffsetArc(AppCommandState& st, int ai, float signedD, std::vector<std::string>& log) {
  if (ai < 0 || static_cast<size_t>(ai) >= st.userArcs.size())
    return false;
  PushUndoSnapshot(st, "Offset arc");
  const CadArc& a = st.userArcs[static_cast<size_t>(ai)];
  const float nr = a.r + signedD;
  if (nr <= 1e-6f) {
    log.push_back("OFFSET — resulting arc radius too small.");
    return false;
  }
  CadArc o = a;
  o.r = nr;
  st.userArcs.push_back(o);
  PushOffsetCopyAttrs(st, st.userArcAttrs, ai);
  BumpCadGpuCache(st);
  return true;
}

static bool CommitOffsetEllipse(AppCommandState& st, int ei, float signedD, std::vector<std::string>& log) {
  if (ei < 0 || static_cast<size_t>(ei) >= st.userEllipses.size())
    return false;
  PushUndoSnapshot(st, "Offset ellipse");
  const CadEllipse& e = st.userEllipses[static_cast<size_t>(ei)];
  const float ma = std::hypot(e.majVx, e.majVy);
  if (ma < 1e-8f) {
    log.push_back("OFFSET — degenerate ellipse.");
    return false;
  }
  const float f = (ma + signedD) / ma;
  if (ma + signedD <= 1e-6f) {
    log.push_back("OFFSET — resulting ellipse too small.");
    return false;
  }
  CadEllipse o = e;
  o.majVx *= f;
  o.majVy *= f;
  st.userEllipses.push_back(o);
  PushOffsetCopyAttrs(st, st.userEllAttrs, ei);
  BumpCadGpuCache(st);
  return true;
}

static bool CommitOffsetPolyline(AppCommandState& st, int pi, float signedD, std::vector<std::string>& log) {
  if (pi < 0 || static_cast<size_t>(pi + 1) >= st.userPolylineOffsets.size())
    return false;
  const int v0 = st.userPolylineOffsets[static_cast<size_t>(pi)];
  const int v1 = st.userPolylineOffsets[static_cast<size_t>(pi + 1)];
  const int nv = v1 - v0;
  if (nv < 2) {
    log.push_back("OFFSET — polyline needs at least two vertices.");
    return false;
  }
  const bool closed =
      static_cast<size_t>(pi) < st.userPolylineClosed.size() && st.userPolylineClosed[static_cast<size_t>(pi)];

  std::vector<std::pair<float, float>> v;
  v.reserve(static_cast<size_t>(nv));
  for (int i = v0; i < v1; ++i) {
    v.push_back({st.userPolylineVerts[static_cast<size_t>(i * 3)], st.userPolylineVerts[static_cast<size_t>(i * 3 + 1)]});
  }

  const int n = static_cast<int>(v.size());
  const int nEdges = closed ? n : n - 1;
  if (nEdges < 1) {
    log.push_back("OFFSET — not enough edges.");
    return false;
  }

  std::vector<std::pair<float, float>> pa(static_cast<size_t>(nEdges)), pb(static_cast<size_t>(nEdges));
  for (int ei = 0; ei < nEdges; ++ei) {
    const int ia = ei;
    const int ib = closed ? (ei + 1) % n : ei + 1;
    const float ax = v[static_cast<size_t>(ia)].first;
    const float ay = v[static_cast<size_t>(ia)].second;
    const float bx = v[static_cast<size_t>(ib)].first;
    const float by = v[static_cast<size_t>(ib)].second;
    float nx = 0.f, ny = 0.f;
    UnitLeftNormal(ax, ay, bx, by, &nx, &ny);
    pa[static_cast<size_t>(ei)] = {ax + nx * signedD, ay + ny * signedD};
    pb[static_cast<size_t>(ei)] = {bx + nx * signedD, by + ny * signedD};
  }

  std::vector<std::pair<float, float>> out;
  if (!closed) {
    if (nEdges == 1) {
      out.push_back(pa[0]);
      out.push_back(pb[0]);
    } else {
      out.push_back(pa[0]);
      for (int ei = 0; ei < nEdges - 1; ++ei) {
        const auto& a0 = pa[static_cast<size_t>(ei)];
        const auto& b0 = pb[static_cast<size_t>(ei)];
        const auto& a1 = pa[static_cast<size_t>(ei + 1)];
        const auto& b1 = pb[static_cast<size_t>(ei + 1)];
        float ix = 0.f, iy = 0.f;
        if (LineLineIntersectInf(a0.first, a0.second, b0.first, b0.second, a1.first, a1.second, b1.first, b1.second,
                                  &ix, &iy))
          out.push_back({ix, iy});
        else {
          const float mx = 0.5f * (b0.first + a1.first);
          const float my = 0.5f * (b0.second + a1.second);
          out.push_back({mx, my});
        }
      }
      out.push_back(pb[static_cast<size_t>(nEdges - 1)]);
    }
  } else {
    out.resize(static_cast<size_t>(nEdges));
    for (int ei = 0; ei < nEdges; ++ei) {
      const int en = (ei + 1) % nEdges;
      const auto& a0 = pa[static_cast<size_t>(ei)];
      const auto& b0 = pb[static_cast<size_t>(ei)];
      const auto& a1 = pa[static_cast<size_t>(en)];
      const auto& b1 = pb[static_cast<size_t>(en)];
      float ix = 0.f, iy = 0.f;
      if (LineLineIntersectInf(a0.first, a0.second, b0.first, b0.second, a1.first, a1.second, b1.first, b1.second, &ix,
                               &iy))
        out[static_cast<size_t>(ei)] = {ix, iy};
      else
        out[static_cast<size_t>(ei)] = {0.5f * (b0.first + a1.first), 0.5f * (b0.second + a1.second)};
    }
  }

  if (out.size() < 2) {
    log.push_back("OFFSET — could not build offset polyline.");
    return false;
  }

  PushUndoSnapshot(st, "Offset polyline");
  if (st.userPolylineOffsets.empty())
    st.userPolylineOffsets.push_back(0);
  const int baseVert = st.userPolylineOffsets.back();
  // Carry the source's elevations rather than flattening (REQ-057). The offset is rebuilt from
  // edge intersections, so its vertex count only usually matches the source; when it does, Z maps
  // 1:1, and when it does not the first vertex's Z is used for the whole run — never 0, which
  // would silently drop a sloped polyline onto the datum.
  std::vector<float> srcZ;  // captured before appending, so the reads never alias the writes
  srcZ.reserve(static_cast<size_t>(nv));
  for (int i = v0; i < v1; ++i)
    srcZ.push_back(st.userPolylineVerts[static_cast<size_t>(i) * 3 + 2]);
  const bool zMaps1to1 = out.size() == srcZ.size();
  const float fallbackZ = srcZ.empty() ? 0.f : srcZ.front();
  for (size_t oi = 0; oi < out.size(); ++oi) {
    st.userPolylineVerts.push_back(out[oi].first);
    st.userPolylineVerts.push_back(out[oi].second);
    st.userPolylineVerts.push_back(zMaps1to1 ? srcZ[oi] : fallbackZ);
  }
  st.userPolylineOffsets.push_back(baseVert + static_cast<int>(out.size()));
  st.userPolylineClosed.push_back(closed ? 1u : 0u);
  PushOffsetCopyAttrs(st, st.userPolylineAttrs, pi);
  BumpCadGpuCache(st);
  return true;
}

static bool CommitOffsetSigned(AppCommandState& st, float signedD, std::vector<std::string>& log) {
  if (!st.offsetEntityValid)
    return false;
  const SelectedEntity& e = st.offsetEntity;
  bool ok = false;
  switch (e.type) {
  case SelectedEntity::Type::LineSeg:
    ok = CommitOffsetLine(st, e.index, signedD, log);
    break;
  case SelectedEntity::Type::Circle:
    ok = CommitOffsetCircle(st, e.index, signedD, log);
    break;
  case SelectedEntity::Type::Arc:
    ok = CommitOffsetArc(st, e.index, signedD, log);
    break;
  case SelectedEntity::Type::Ellipse:
    ok = CommitOffsetEllipse(st, e.index, signedD, log);
    break;
  case SelectedEntity::Type::Polyline:
    ok = CommitOffsetPolyline(st, e.index, signedD, log);
    break;
  default:
    log.push_back("OFFSET — unsupported entity type.");
    return false;
  }
  if (ok)
    log.push_back("OFFSET — created parallel / concentric geometry.");
  return ok;
}

static void FinishOffsetAndIdle(AppCommandState& st, std::vector<std::string>& log) {
  (void)log;
  ResetOffsetDraft(st);
  st.active = AppCommandState::Kind::None;
}

static void HandleOffsetThroughPick(AppCommandState& st, float px, float py, std::vector<std::string>& log) {
  if (!st.offsetEntityValid)
    return;
  const SelectedEntity& e = st.offsetEntity;
  float signedD = 0.f;
  switch (e.type) {
  case SelectedEntity::Type::LineSeg: {
    const size_t k = static_cast<size_t>(e.index) * 6;
    if (k + 5 >= st.userLinesFlat.size())
      return;
    const float x0 = st.userLinesFlat[k];
    const float y0 = st.userLinesFlat[k + 1];
    const float x1 = st.userLinesFlat[k + 3];
    const float y1 = st.userLinesFlat[k + 4];
    signedD = SignedSideLine(x0, y0, x1, y1, px, py);
    break;
  }
  case SelectedEntity::Type::Circle: {
    const size_t k = static_cast<size_t>(e.index) * 4;
    if (k + 3 >= st.userCirclesCxCyZR.size())
      return;
    const float cx = st.userCirclesCxCyZR[k];
    const float cy = st.userCirclesCxCyZR[k + 1];
    const float r = st.userCirclesCxCyZR[k + 3];
    signedD = SignedSideCircle(cx, cy, r, px, py);
    break;
  }
  case SelectedEntity::Type::Arc: {
    if (e.index < 0 || static_cast<size_t>(e.index) >= st.userArcs.size())
      return;
    const CadArc& a = st.userArcs[static_cast<size_t>(e.index)];
    signedD = SignedSideCircle(a.cx, a.cy, a.r, px, py);
    break;
  }
  case SelectedEntity::Type::Polyline:
    log.push_back("OFFSET — polyline: type a distance, then pick a side (through-click not supported).");
    return;
  case SelectedEntity::Type::Ellipse:
    log.push_back("OFFSET — ellipse: type a distance, then pick a side (through-click not supported).");
    return;
  default:
    return;
  }
  if (std::fabs(signedD) < 1e-8f) {
    log.push_back("OFFSET — through point on original; pick farther away.");
    return;
  }
  if (CommitOffsetSigned(st, signedD, log))
    FinishOffsetAndIdle(st, log);
}

static void HandleOffsetSidePick(AppCommandState& st, float px, float py, std::vector<std::string>& log) {
  if (!st.offsetEntityValid || st.offsetTypedDistance <= 0.f)
    return;
  const float d = st.offsetTypedDistance;
  const SelectedEntity& e = st.offsetEntity;
  float sgn = 1.f;
  switch (e.type) {
  case SelectedEntity::Type::LineSeg: {
    const size_t k = static_cast<size_t>(e.index) * 6;
    if (k + 5 >= st.userLinesFlat.size())
      return;
    const float sd = SignedSideLine(st.userLinesFlat[k], st.userLinesFlat[k + 1], st.userLinesFlat[k + 3],
                                    st.userLinesFlat[k + 4], px, py);
    sgn = sd >= 0.f ? 1.f : -1.f;
    break;
  }
  case SelectedEntity::Type::Circle: {
    const size_t k = static_cast<size_t>(e.index) * 4;
    if (k + 3 >= st.userCirclesCxCyZR.size())
      return;
    const float cx = st.userCirclesCxCyZR[k];
    const float cy = st.userCirclesCxCyZR[k + 1];
    const float r = st.userCirclesCxCyZR[k + 3];
    const float side = SignedSideCircle(cx, cy, r, px, py);
    sgn = side >= 0.f ? 1.f : -1.f;
    break;
  }
  case SelectedEntity::Type::Arc: {
    if (e.index < 0 || static_cast<size_t>(e.index) >= st.userArcs.size())
      return;
    const CadArc& a = st.userArcs[static_cast<size_t>(e.index)];
    const float side = SignedSideCircle(a.cx, a.cy, a.r, px, py);
    sgn = side >= 0.f ? 1.f : -1.f;
    break;
  }
  case SelectedEntity::Type::Ellipse:
  case SelectedEntity::Type::Polyline: {
    if (e.type == SelectedEntity::Type::Polyline) {
      const int pi = e.index;
      if (pi >= 0 && static_cast<size_t>(pi + 1) < st.userPolylineOffsets.size()) {
        const int v0 = st.userPolylineOffsets[static_cast<size_t>(pi)];
        const int v1 = st.userPolylineOffsets[static_cast<size_t>(pi + 1)];
        float best = 1e30f;
        float bestS = 1.f;
        for (int vi = v0; vi + 1 < v1; ++vi) {
          const float ax = st.userPolylineVerts[static_cast<size_t>(vi * 3)];
          const float ay = st.userPolylineVerts[static_cast<size_t>(vi * 3 + 1)];
          const float bx = st.userPolylineVerts[static_cast<size_t>((vi + 1) * 3)];
          const float by = st.userPolylineVerts[static_cast<size_t>((vi + 1) * 3 + 1)];
          float qx = 0.f, qy = 0.f;
          ClosestPointOnSegment(ax, ay, bx, by, px, py, &qx, &qy);
          const float sd = SignedSideLine(ax, ay, bx, by, px, py);
          const float dx = px - qx;
          const float dy = py - qy;
          const float dist2 = dx * dx + dy * dy;
          if (dist2 < best) {
            best = dist2;
            bestS = sd >= 0.f ? 1.f : -1.f;
          }
        }
        sgn = bestS;
      }
    } else if (e.index >= 0 && static_cast<size_t>(e.index) < st.userEllipses.size()) {
      const CadEllipse& el = st.userEllipses[static_cast<size_t>(e.index)];
      const float ma = std::hypot(el.majVx, el.majVy);
      if (ma >= 1e-8f) {
        const float ux = el.majVx / ma;
        const float uy = el.majVy / ma;
        const float pxn = -uy;
        const float pyn = ux;
        const float mb = ma * el.ratio;
        constexpr float twopi = 6.28318530718f;
        float best = 1e30f;
        float bx = el.cx, by = el.cy;
        for (int i = 0; i <= 48; ++i) {
          const float ang = twopi * static_cast<float>(i) / 48.f;
          const float c0 = std::cos(ang);
          const float s0 = std::sin(ang);
          const float ex = el.cx + ux * (ma * c0) + pxn * (mb * s0);
          const float ey = el.cy + uy * (ma * c0) + pyn * (mb * s0);
          const float dx = px - ex;
          const float dy = py - ey;
          const float dist2 = dx * dx + dy * dy;
          if (dist2 < best) {
            best = dist2;
            bx = ex;
            by = ey;
          }
        }
        const float ox = bx - el.cx;
        const float oy = by - el.cy;
        const float inX = px - el.cx;
        const float inY = py - el.cy;
        sgn = (inX * ox + inY * oy) >= 0.f ? 1.f : -1.f;
      }
    }
    break;
  }
  default:
    return;
  }
  const float signedD = d * sgn;
  if (CommitOffsetSigned(st, signedD, log))
    FinishOffsetAndIdle(st, log);
}

static void HandleOffsetViewportPick(AppCommandState& st, float wx, float wy, std::vector<std::string>& log) {
  using OP = AppCommandState::OffsetPhase;
  switch (st.offsetPhase) {
  case OP::WaitSelectEntity: {
    SelectedEntity hit{};
    float d2 = 0.f;
    if (!PickClosestCadEntity(st, wx, wy, CadOffsetEntityPickTolWorld(st), &hit, &d2)) {
      log.push_back("OFFSET — nothing under cursor; try again.");
      return;
    }
    // REQ-087 / REQ-201. A feature line is pickable (it has to be, to select and move), so without
    // this it would be accepted here and then dropped silently by CommitOffsetSigned's `default:`.
    // Refusing is not a limitation we are hiding — offsetting a 3D chain has no defined answer for
    // what elevation the offset copy carries, and neither REQ-087 nor REQ-088 supplies one, so the
    // Workshop does not get to pick one (CLAUDE.md layer rule 3). Stays in the select phase.
    if (hit.type == SelectedEntity::Type::FeatureLine) {
      log.push_back("OFFSET — 1 feature line ignored: offsetting one has no defined elevation for "
                    "the new line. Pick a line, circle, arc, ellipse, or polyline.");
      return;
    }
    // REQ-068 / ADR-036 (c) — the same reasoning, one kind later. A surface became pickable, so
    // without this it would be accepted here and then dropped by CommitOffsetSigned's `default:`,
    // leaving the user having picked something and watched nothing happen. There is also no answer
    // to offer: "offset a surface" would mean a second surface at a vertical or normal displacement,
    // which is a grading operation no requirement defines.
    if (hit.type == SelectedEntity::Type::Surface) {
      log.push_back("OFFSET — 1 surface ignored: a surface cannot be offset. Pick a line, circle, "
                    "arc, ellipse, or polyline.");
      return;
    }
    st.offsetEntity = hit;
    st.offsetEntityValid = true;
    st.offsetPhase = OP::WaitDistanceOrThrough;
    st.offsetTypedDistance = 0.f;
    log.push_back("OFFSET — distance (number) + pick side, or click through point for line / circle / arc.");
    return;
  }
  case OP::WaitDistanceOrThrough:
    HandleOffsetThroughPick(st, wx, wy, log);
    return;
  case OP::WaitSidePick:
    HandleOffsetSidePick(st, wx, wy, log);
    return;
  }
}

} // namespace OffsetCmd

// Returns true when the bearing-pick state fully consumed the click (caller must return).
// When false, the caller should call SubmitLineVertex / SubmitPolylineVertex with the
// (possibly angle-locked or ortho-clamped) wx/wy.
static bool ApplySegmentAnglePickToViewportPick(AppCommandState& st, float& wx, float& wy,
                                                bool inNextPtPhase, std::vector<std::string>& log) {
  using SAP = AppCommandState::SegmentAnglePickPhase;
  if (!inNextPtPhase) return false;
  if (st.segmentAngleKeyboardAwaitBearing) {
    log.push_back("Finish bearing entry on the command line (blank Enter cancels) before viewport picks.");
    return true;
  }
  if (st.segmentAnglePickPhase == SAP::WaitP1) {
    st.segmentPickRefX1 = wx; st.segmentPickRefY1 = wy;
    st.segmentAnglePickPhase = SAP::WaitP2;
    log.push_back("Bearing pick — second reference point:");
    return true;
  }
  if (st.segmentAnglePickPhase == SAP::WaitP2) {
    const float dx = wx - st.segmentPickRefX1, dy = wy - st.segmentPickRefY1;
    if (std::hypot(dx, dy) < 1e-8f)
      log.push_back("Bearing pick — points coincide; pick again.");
    else {
      st.segmentPickDraftBearingDeg = BearingCwNorthDegFromMathAngleRad(std::atan2(dy, dx));
      st.segmentAnglePickPhase = SAP::WaitAdjustOrCommit;
      log.push_back("Bearing from picks — Enter locks as-is; type +90 / -45 (° CW from N) to adjust and lock (one line).");
    }
    return true;
  }
  if (st.segmentAnglePickPhase == SAP::WaitAdjustOrCommit) {
    log.push_back("Bearing pick — press Enter to lock (or type +90 / -45); viewport click ignored in this step.");
    return true;
  }
  if (st.segmentAngleLockActive)
    ApplySegmentAngleLockToWorldPick(st.anchorX, st.anchorY, st.segmentLockUx, st.segmentLockUy, &wx, &wy, false);
  else
    OrthoConstrainPoint(st.anchorX, st.anchorY, &wx, &wy, st.orthoMode);  // no-op when ORTHO off (REQ-047)
  return false;
}

void SubmitViewportPickImpl(AppCommandState& st, float wx, float wy, std::vector<std::string>& log,
                             bool windowSelectionSubtract, bool fenceLeftToRightWindowMode) {
  using K = AppCommandState::Kind;
  using MP = AppCommandState::ModifyPhase;
  using RP = AppCommandState::RotatePhase;
  using SP = AppCommandState::ScalePhase;

  // Box-selection projects to screen space only when the view is actually orbited; in plan view a
  // null camera keeps the historical world-rect test byte-for-byte (REQ-058).
  const Camera boxSelCamValue = CadViewCamera(st);
  const Camera* const boxSelCam = CadViewIsPlan(st) ? nullptr : &boxSelCamValue;
  const Camera* const boxSelCam2 = boxSelCam;
  const Camera* const boxSelCam3 = boxSelCam;
  auto finishBox = [&]() {
    const bool inclSurvey = (st.active == AppCommandState::Kind::None || st.active == K::Move ||
                             st.active == K::Copy || st.active == K::Rotate || st.active == K::Scale ||
                             st.active == K::Align);
    ComputeSelectionFromRect(st, st.selBoxAnchorX, st.selBoxAnchorY, wx, wy, windowSelectionSubtract,
                             fenceLeftToRightWindowMode, inclSurvey, boxSelCam, st.uiViewportWidthPx,
                             st.uiViewportHeightPx);
    st.selBoxWaitingSecond = false;
    log.push_back("Fence — CAD " + std::to_string(st.selection.size()) + ", survey " +
                  std::to_string(st.selectedSurveyPointIndices.size()) +
                  (fenceLeftToRightWindowMode ? " (window)." : " (crossing)."));
  };

  if (st.active == K::Line) {
    using LP = AppCommandState::LinePhase;
    const bool nextPt = st.linePhase == LP::NeedNextPoint;
    if (!ApplySegmentAnglePickToViewportPick(st, wx, wy, nextPt, log))
      SubmitLineVertex(st, wx, wy, log);
    return;
  }

  if (st.active == K::Polyline) {
    using PP = AppCommandState::PolylinePhase;
    const bool nextPt = st.polylinePhase == PP::NeedNextPoint;
    if (!ApplySegmentAnglePickToViewportPick(st, wx, wy, nextPt, log))
      SubmitPolylineVertex(st, wx, wy, log);
    return;
  }

  // REQ-087 / TASK-082 BUG-1. Missing entirely until 2026-08-20, so every click during FEATURELINE
  // was silently discarded and the command appeared to hang on its first prompt — the exact failure
  // the comment above CadUi.cpp's point-picking list warns about. A transcript could not catch it
  // because a transcript types coordinates and never clicks.
  //
  // A click carries X and Y only, so it goes to SubmitFeatureLinePoint, which prompts for the
  // elevation rather than taking one from the work plane the way POLYLINE does.
  if (st.active == K::FeatureLine) {
    if (st.featureLinePendingPoint) {
      // A second click while an elevation is owed. Move the pending point to where they clicked
      // rather than ignoring it: the click is unambiguous, and silently dropping it would repeat
      // BUG-1 in miniature.
      st.featureLinePendingX = wx;
      st.featureLinePendingY = wy;
      char buf[192];
      std::snprintf(buf, sizeof(buf),
                    "FEATURELINE — point moved. Elevation for %s %zu <%.3f>:",
                    st.featureLineNextIsElevPoint ? "elevation point" : "point",
                    st.featureLineDraftElevPt.size() + 1,
                    static_cast<double>(st.featureLinePendingDefaultZ));
      log.push_back(buf);
      return;
    }
    SubmitFeatureLinePoint(st, wx, wy, log);
    return;
  }

  if (st.active == K::Rect) {
    using RectP = AppCommandState::RectPhase;
    if (st.rectPhase == RectP::WaitFirstCorner) {
      st.rectX1 = wx;
      st.rectY1 = wy;
      st.rectPhase = RectP::WaitSecondCorner;
      // The anchor is the base for relative (@dx,dy) entry, exactly as it is for LINE.
      st.anchorX = wx;
      st.anchorY = wy;
      log.push_back("RECT — pick the opposite corner (or type X,Y / @dx,dy):");
    } else {
      CommitRectangle(st, st.rectX1, st.rectY1, wx, wy, log);
    }
    return;
  }

  if (st.active == K::Arc) {
    using AP = AppCommandState::ArcPhase;
    switch (st.arcPhase) {
    case AP::WaitStart:
      st.arcAx = wx;
      st.arcAy = wy;
      st.arcPhase = AP::WaitMid;
      log.push_back("ARC — pick middle point on arc:");
      break;
    case AP::WaitMid:
      st.arcBx = wx;
      st.arcBy = wy;
      st.arcPhase = AP::WaitEnd;
      log.push_back("ARC — pick end point:");
      break;
    case AP::WaitEnd:
      CommitArcThreePoints(st, st.arcAx, st.arcAy, st.arcBx, st.arcBy, wx, wy, log);
      break;
    }
    return;
  }

  if (st.active == K::Ellipse) {
    using EP = AppCommandState::EllipsePhase;
    switch (st.ellPhase) {
    case EP::WaitCenter:
      st.ellCx = wx;
      st.ellCy = wy;
      st.ellPhase = EP::WaitMajorEnd;
      log.push_back("ELLIPSE — major axis endpoint:");
      break;
    case EP::WaitMajorEnd:
      st.ellMajEx = wx;
      st.ellMajEy = wy;
      st.ellPhase = EP::WaitRatio;
      log.push_back("ELLIPSE — type minor/major ratio (0-1], or Enter for 0.5:");
      break;
    case EP::WaitRatio:
      log.push_back("ELLIPSE — type ratio on command line (middle mouse pick ignored here).");
      break;
    }
    return;
  }

  if (st.active == K::Text) {
    using TP = AppCommandState::TextCmdPhase;
    if (st.textPhase == TP::WaitInsertion) {
      st.textInsX = wx;
      st.textInsY = wy;
      st.textPhase = TP::WaitHeight;
      log.push_back("TEXT — height (Enter = plot-scale default):");
    } else
      log.push_back("TEXT — continue on command line (height / rotation / text).");
    return;
  }

  if (st.active == K::Mtext) {
    using MPt = AppCommandState::MtextPhase;
    switch (st.mtextPhase) {
    case MPt::WaitCorner1:
      st.mtxtX1 = wx;
      st.mtxtY1 = wy;
      st.mtextPhase = MPt::WaitCorner2;
      log.push_back("MTEXT — opposite corner:");
      break;
    case MPt::WaitCorner2:
      st.mtxtX2 = wx;
      st.mtxtY2 = wy;
      st.mtextPhase = MPt::WaitString;
      OpenMtextRichEditorForPlacement(st, &log);
      break;
    case MPt::WaitString:
      break;
    }
    return;
  }

  if (st.active == K::DimAligned || st.active == K::DimLinear) {
    using DP = AppCommandState::DimPhase;
    const bool linear = st.active == K::DimLinear;
    switch (st.dimPhase) {
    case DP::WaitExt1:
      st.dimE1x = wx;
      st.dimE1y = wy;
      st.dimPhase = DP::WaitExt2;
      log.push_back(std::string(linear ? "DIMLINEAR" : "DIMALIGNED") + " — second extension point:");
      break;
    case DP::WaitExt2:
      st.dimE2x = wx;
      st.dimE2y = wy;
      st.dimPhase = DP::WaitDimLinePt;
      if (linear) {
        st.dimLinearOrientUserLock = false;
        CadDimLinearUpdateDraftOrientation(st, wx, wy);
        log.push_back(
            "DIMLINEAR — pick dimension line position (horizontal vs vertical follows cursor; H / V to lock); type X,Y or @dx,dy from chord mid.");
      } else
        log.push_back("DIMALIGNED — pick dimension line position (offset from measured segment).");
      break;
    case DP::WaitDimLinePt:
      if (linear)
        CommitDimLinearAt(st, wx, wy, log);
      else
        CommitDimAlignedAt(st, wx, wy, log);
      break;
    }
    return;
  }

  if (st.active == K::DimAngular) {
    using DAP = AppCommandState::DimAngularPhase;
    switch (st.dimAngularPhase) {
    case DAP::WaitVertex:
      st.dimAngVx = wx;
      st.dimAngVy = wy;
      st.dimAngularPhase = DAP::WaitRay1;
      log.push_back("DIMANGULAR — first ray point (on first leg):");
      break;
    case DAP::WaitRay1:
      st.dimE1x = wx;
      st.dimE1y = wy;
      st.dimAngularPhase = DAP::WaitRay2;
      log.push_back("DIMANGULAR — second ray point (on second leg):");
      break;
    case DAP::WaitRay2:
      st.dimE2x = wx;
      st.dimE2y = wy;
      st.dimAngularPhase = DAP::WaitArc;
      log.push_back("DIMANGULAR — pick arc / label side (radius along angle bisector); type X,Y or @dx,dy from vertex.");
      break;
    case DAP::WaitArc:
      CommitDimAngularAt(st, wx, wy, log);
      break;
    }
    return;
  }

  if (st.active == K::IdPoint) {
    CommitIdPointAt(st, wx, wy, log);
    return;
  }

  if (st.active == K::SurveyInverse) {
    using SIP = AppCommandState::SurveyInversePhase;
    if (st.surveyInversePhase == SIP::WaitFrom) {
      st.surveyInverseFromX = wx;
      st.surveyInverseFromY = wy;
      st.surveyInversePhase = SIP::WaitTo;
      log.push_back("INVERSE — second point (pick or type X,Y; @dx,dy from first):");
      return;
    }
    CommitSurveyInverseSecondPoint(st, wx, wy, log);
    return;
  }

  if (st.active == K::SurfaceElevGrade) {
    using SEP = AppCommandState::SurfaceElevPhase;
    if (st.surfaceElevPhase == SEP::WaitFirst) {
      ReportSurfaceElevationAt(st, wx, wy, log);
      st.surfaceElevPhase = SEP::WaitSecond;
      log.push_back("SURFELEV — second point for grade, or ESC to stop here.");
      return;
    }
    ReportSurfaceGradeTo(st, wx, wy, log);
    return;
  }

  if (st.active == K::DesignateBreakline) {
    CommitDesignateAt(st, wx, wy, /*isBoundary=*/false, log);
    return;
  }
  if (st.active == K::DesignateBoundary) {
    CommitDesignateAt(st, wx, wy, /*isBoundary=*/true, log);
    return;
  }

  if (st.active == K::Align) {
    using AP = AppCommandState::AlignPhase;
    if (st.alignPhase == AP::PickSelection) {
      if (st.selBoxWaitingSecond)
        finishBox();
      return;
    }
    if (st.alignPhase == AP::PickSrc) {
      AppCommandState::AlignControlPt cp{};
      cp.srcX = wx;
      cp.srcY = wy;
      st.alignControlPts.push_back(cp);
      st.alignPhase = AP::PickDst;
      log.push_back("ALIGN — destination for pair " + std::to_string(st.alignControlPts.size()) +
                    " (pick or type real-world X,Y):");
    } else {
      st.alignControlPts.back().dstX = wx;
      st.alignControlPts.back().dstY = wy;
      st.alignPhase = AP::PickSrc;
      const size_t n = st.alignControlPts.size();
      log.push_back("ALIGN — pair " + std::to_string(n) + " added.  Pick next source, or Enter to apply (" +
                    std::to_string(n) + " pair" + (n == 1 ? "" : "s") + " ready).");
    }
    return;
  }

  if (st.active == K::Offset) {
    OffsetCmd::HandleOffsetViewportPick(st, wx, wy, log);
    return;
  }

  if (st.active == K::Circle) {
    switch (st.circlePhase) {
    case AppCommandState::CirclePhase::WaitCenterOrMode:
      st.circleCx = wx;
      st.circleCy = wy;
      st.circlePhase = AppCommandState::CirclePhase::WaitRadius;
      log.push_back("Center set — specify radius (click near edge), type radius, or D + diameter.");
      break;
    case AppCommandState::CirclePhase::WaitRadius: {
      const float dx = wx - st.circleCx;
      const float dy = wy - st.circleCy;
      const float r = std::sqrt(dx * dx + dy * dy);
      CommitCircle(st, st.circleCx, st.circleCy, r, log);
      break;
    }
    case AppCommandState::CirclePhase::ThreeP_WaitP1:
      st.c3p1x = wx;
      st.c3p1y = wy;
      st.circlePhase = AppCommandState::CirclePhase::ThreeP_WaitP2;
      log.push_back("Second point of circle:");
      break;
    case AppCommandState::CirclePhase::ThreeP_WaitP2:
      st.c3p2x = wx;
      st.c3p2y = wy;
      st.circlePhase = AppCommandState::CirclePhase::ThreeP_WaitP3;
      log.push_back("Third point of circle:");
      break;
    case AppCommandState::CirclePhase::ThreeP_WaitP3: {
      float ox = 0.f;
      float oy = 0.f;
      float r = 0.f;
      if (!ComputeCircumcircle(st.c3p1x, st.c3p1y, st.c3p2x, st.c3p2y, wx, wy, &ox, &oy, &r))
        log.push_back("Points are collinear — pick a non-collinear third point.");
      else
        CommitCircle(st, ox, oy, r, log);
      break;
    }
    }
    return;
  }

  if (st.active == K::Zoom) {
    if (st.selBoxWaitingSecond) {
      st.pendingZoomMnX = std::min(st.selBoxAnchorX, wx);
      st.pendingZoomMxX = std::max(st.selBoxAnchorX, wx);
      st.pendingZoomMnY = std::min(st.selBoxAnchorY, wy);
      st.pendingZoomMxY = std::max(st.selBoxAnchorY, wy);
      st.selBoxWaitingSecond = false;
      st.pendingZoomWindow = true;
      st.active = K::None;
    }
    return;
  }

  if (st.active == K::Join) {
    if (st.selBoxWaitingSecond) {
      ComputeSelectionFromRect(st, st.selBoxAnchorX, st.selBoxAnchorY, wx, wy, windowSelectionSubtract,
                               fenceLeftToRightWindowMode, false, boxSelCam2, st.uiViewportWidthPx,
                               st.uiViewportHeightPx);
      st.selBoxWaitingSecond = false;
      if (st.selection.empty())
        log.push_back("Nothing selected — pick two corners again.");
      else {
        ExecuteJoinSelection(st, log);
        st.active = K::None;
        ResetModifyRotateDraft(st);
      }
    }
    return;
  }

  if (st.active == K::Delete) {
    if (st.selBoxWaitingSecond) {
      ComputeSelectionFromRect(st, st.selBoxAnchorX, st.selBoxAnchorY, wx, wy, windowSelectionSubtract,
                               fenceLeftToRightWindowMode, false, boxSelCam3, st.uiViewportWidthPx,
                               st.uiViewportHeightPx);
      st.selBoxWaitingSecond = false;
      if (st.selection.empty())
        log.push_back("Nothing selected — pick two corners again.");
      else {
        ExecuteDeleteSelection(st, log);
        st.active = K::None;
        ResetModifyRotateDraft(st);
      }
    }
    return;
  }

  if (st.active == K::Move || st.active == K::Copy) {
    if (st.modifyPhase == MP::PickSelection) {
      if (st.selBoxWaitingSecond) {
        finishBox();
        if (st.selection.empty() && st.selectedSurveyPointIndices.empty())
          log.push_back("Nothing selected — pick two corners again.");
        else {
          st.modifyPhase = MP::NeedBase;
          log.push_back(st.active == K::Copy ? "COPY — base point:" : "MOVE — base point:");
        }
      }
      return;
    }
    if (st.modifyPhase == MP::NeedBase) {
      st.modifyBaseX = wx;
      st.modifyBaseY = wy;
      st.modifyPhase = MP::NeedDestination;
      log.push_back(st.active == K::Copy ? "COPY — destination:" : "MOVE — destination:");
      return;
    }
    if (st.modifyPhase == MP::NeedDestination) {
      const bool wasCopy = (st.active == K::Copy);
      const float dx = wx - st.modifyBaseX;
      const float dy = wy - st.modifyBaseY;
      PushUndoSnapshot(st, wasCopy ? "Copy" : "Move");
      if (wasCopy)
        FinalizeCopyTranslation(st, dx, dy, log);
      else {
        ApplyTranslationToSelection(st, dx, dy, log);
        // Stay in MOVE — same selection at new position, ready for another base+destination.
        st.modifyPhase = MP::NeedBase;
        log.push_back("MOVE complete — base point (ESC to exit):");
      }
    }
    return;
  }

  if (st.active == K::Paste && st.modifyPhase == MP::NeedDestination) {
    CommitClipboardPasteAt(st, wx, wy, log);  // routes by active space; builds the new selection
    return;
  }

  if (st.active == K::Scale) {
    if (st.modifyPhase == MP::PickSelection) {
      if (st.selBoxWaitingSecond) {
        finishBox();
        if (st.selection.empty() && st.selectedSurveyPointIndices.empty())
          log.push_back("Nothing selected — pick two corners again.");
        else {
          st.modifyPhase = MP::NeedBase;
          log.push_back("SCALE — base point:");
        }
      }
      return;
    }
    if (st.modifyPhase == MP::NeedBase) {
      st.modifyBaseX = wx;
      st.modifyBaseY = wy;
      st.scaleRefDist = ComputeScaleReferenceDistance(st, wx, wy);
      st.scalePhase = SP::FactorPick;
      st.modifyPhase = MP::NeedDestination;
      log.push_back(
          "SCALE — pick second point or type factor (>0), or R / REFERENCE on command line for two-point reference "
          "length.");
      return;
    }
    if (st.modifyPhase == MP::NeedDestination) {
      switch (st.scalePhase) {
      case SP::FactorPick: {
        const float d = std::hypot(wx - st.modifyBaseX, wy - st.modifyBaseY);
        const float s = std::max(d / std::max(st.scaleRefDist, 1e-20f), 1e-6f);
        FinishScaleCommand(st, s, log);
        return;
      }
      case SP::Ref_WaitP1:
        st.scaleRefP1X = wx;
        st.scaleRefP1Y = wy;
        st.scalePhase = SP::Ref_WaitP2;
        log.push_back("SCALE ref — second point of reference length:");
        return;
      case SP::Ref_WaitP2: {
        const float refLen = std::hypot(wx - st.scaleRefP1X, wy - st.scaleRefP1Y);
        if (!(refLen > 1e-8f) || !std::isfinite(refLen)) {
          log.push_back("SCALE ref — reference length is too small; pick two distinct points.");
          return;
        }
        st.scaleRefDist = refLen;
        st.scalePhase = SP::NewLength_WaitTypedOrP1;
        log.push_back("SCALE ref — type new length (model units) or pick first point of new length segment.");
        return;
      }
      case SP::NewLength_WaitTypedOrP1:
        st.scaleNewLenP1X = wx;
        st.scaleNewLenP1Y = wy;
        st.scalePhase = SP::NewLength_WaitP2;
        log.push_back("SCALE ref — second point of new length segment:");
        return;
      case SP::NewLength_WaitP2: {
        const float newLen = std::hypot(wx - st.scaleNewLenP1X, wy - st.scaleNewLenP1Y);
        if (!(newLen > 1e-8f) || !std::isfinite(newLen)) {
          log.push_back("SCALE ref — new length is too small; pick two distinct points.");
          return;
        }
        FinishScaleCommand(st, newLen / std::max(st.scaleRefDist, 1e-20f), log);
        return;
      }
      }
    }
    return;
  }

  if (st.active == K::Rotate) {
    if (st.rotatePhase == RP::PickSelection) {
      if (st.selBoxWaitingSecond) {
        finishBox();
        if (st.selection.empty() && st.selectedSurveyPointIndices.empty())
          log.push_back("Nothing selected — pick two corners again.");
        else {
          st.rotatePhase = RP::NeedBase;
          log.push_back("ROTATE — base point:");
        }
      }
      return;
    }
    if (st.rotatePhase == RP::NeedBase) {
      st.rotateBaseX = wx;
      st.rotateBaseY = wy;
      st.rotatePhase = RP::NeedAngleOrReference;
      log.push_back(
          "ROTATE — ° clockwise from north or R reference or C copy; decimal/DMS or click-drag preview.");
      return;
    }
    if (st.rotatePhase == RP::NeedAngleOrReference) {
      // Click confirms the angle shown in the live preview: bearing CW from north, base→cursor.
      const float dx = wx - st.rotateBaseX;
      const float dy = wy - st.rotateBaseY;
      FinishRotateCommand(st, st.rotateBaseX, st.rotateBaseY, -std::atan2(dx, dy), log);
      return;
    }
    if (st.rotatePhase == RP::AfterReference_WaitAngleOrP) {
      // Click confirms the angle shown in the live preview: angle from reference segment to base→cursor.
      const float thetaRef =
          std::atan2(st.rotateRefY2 - st.rotateRefY1, st.rotateRefX2 - st.rotateRefX1);
      const float delta = std::atan2(wy - st.rotateBaseY, wx - st.rotateBaseX) - thetaRef;
      FinishRotateCommand(st, st.rotateBaseX, st.rotateBaseY, delta, log);
      return;
    }
    if (st.rotatePhase == RP::Ref_WaitP1) {
      st.rotateRefX1 = wx;
      st.rotateRefY1 = wy;
      st.rotatePhase = RP::Ref_WaitP2;
      log.push_back("Reference — second point:");
      return;
    }
    if (st.rotatePhase == RP::Ref_WaitP2) {
      st.rotateRefX2 = wx;
      st.rotateRefY2 = wy;
      st.rotatePhase = RP::AfterReference_WaitAngleOrP;
      log.push_back("Enter new bearing from north ° (matches properties), or P for two-point line.");
      return;
    }
    if (st.rotatePhase == RP::AnglePoints_WaitP1) {
      st.rotateAnglePt1X = wx;
      st.rotateAnglePt1Y = wy;
      st.rotatePhase = RP::AnglePoints_WaitP2;
      log.push_back("Angle — second point:");
      return;
    }
    if (st.rotatePhase == RP::AnglePoints_WaitP2) {
      const float delta =
          RotateDeltaFromReferenceAndNewSegment(st.rotateRefX1, st.rotateRefY1, st.rotateRefX2, st.rotateRefY2,
                                                  st.rotateAnglePt1X, st.rotateAnglePt1Y, wx, wy);
      FinishRotateCommand(st, st.rotateBaseX, st.rotateBaseY, delta, log);
    }
    return;
  }

  if (st.active == K::None && st.selBoxWaitingSecond)
    finishBox();
}

} // namespace

// Public paste entry point (REQ-038): place the clipboard at point (x,y) in the ACTIVE space's coordinates
// (world for model, paper inches for a paper layout). Used by the model pick path and the paper overlay click.
// Calls the file-local CommitPasteFromClipboard (visible here via the anonymous namespace's using-directive).
void CommitClipboardPasteAt(AppCommandState& st, float x, float y, std::vector<std::string>& log) {
  if (st.clipboard.empty())
    return;
  PushUndoSnapshot(st, "Paste");
  CommitPasteFromClipboard(st, x - st.clipboard.basePtX, y - st.clipboard.basePtY, log);
  st.active = AppCommandState::Kind::None;
  st.modifyPhase = AppCommandState::ModifyPhase::PickSelection;
  log.push_back("PASTE complete.");
}

// Append filled regions fully enclosed by [mnX,mxX]×[mnY,mxY] to the clipboard (REQ-038 addendum). Fills are
// not an independently selectable entity, so a copy carries the fills inside the selection's bounding box.
static void CopyEnclosedFilledRegions(CadClipboard& cb, const std::vector<CadFilledRegion>& regions,
                                      const std::vector<EntityAttributes>& attrs, float mnX, float mnY, float mxX,
                                      float mxY, const std::set<int>* skipIdx = nullptr) {
  if (mnX > mxX)
    return;
  for (size_t i = 0; i < regions.size(); ++i) {
    if (skipIdx && skipIdx->count(static_cast<int>(i)))
      continue;  // already copied as a directly-selected fill (REQ-042)
    const CadFilledRegion& fr = regions[i];
    if (fr.vertsXyz.size() < 9)  // < 3 vertices × 3 floats
      continue;
    bool inside = true;
    // Enclosure is tested in plan (X/Y) only — the selection box is a 2D window (REQ-038 addendum).
    for (size_t v = 0; v + 2 < fr.vertsXyz.size(); v += 3)
      if (fr.vertsXyz[v] < mnX || fr.vertsXyz[v] > mxX || fr.vertsXyz[v + 1] < mnY ||
          fr.vertsXyz[v + 1] > mxY) {
        inside = false;
        break;
      }
    if (!inside)
      continue;
    cb.filledRegions.push_back(fr);
    cb.filledRegionAttrs.push_back(i < attrs.size() ? attrs[i] : EntityAttributes{});
  }
}

// Copy the active paper layout's selected entities into st.clipboard (REQ-038, ADR-013). Coordinates are
// paper inches; they enter the clipboard verbatim and a later paste applies the 1:1 transfer.
static void CopyPaperSelectionToClipboard(AppCommandState& st, PaperLayout& L, std::vector<std::string>& log) {
  if (st.selectedPaperEntities.empty()) {
    log.push_back("COPYCLIP — nothing selected. Select paper objects first.");
    return;
  }
  CadClipboard& cb = st.clipboard;
  cb = CadClipboard{};
  cb.fromPaper = true;
  float mnX = 1e30f, mnY = 1e30f, mxX = -1e30f, mxY = -1e30f;
  auto expandBbox = [&](float x, float y) {
    mnX = std::min(mnX, x); mnY = std::min(mnY, y);
    mxX = std::max(mxX, x); mxY = std::max(mxY, y);
  };
  auto attrAt = [](const std::vector<EntityAttributes>& v, int i) {
    return (i >= 0 && static_cast<size_t>(i) < v.size()) ? v[static_cast<size_t>(i)] : EntityAttributes{};
  };
  for (const PaperRef& r : st.selectedPaperEntities) {
    switch (r.type) {
    case PaperRef::Type::Line: {
      const size_t k = static_cast<size_t>(r.index) * 6;
      if (k + 5 >= L.paperLines.size())
        break;
      for (int j = 0; j < 6; ++j)
        cb.lines.push_back(L.paperLines[k + static_cast<size_t>(j)]);
      cb.lineAttrs.push_back(attrAt(L.paperLineAttrs, r.index));
      expandBbox(L.paperLines[k], L.paperLines[k + 1]);
      expandBbox(L.paperLines[k + 3], L.paperLines[k + 4]);
      break;
    }
    case PaperRef::Type::Circle: {
      const size_t k = static_cast<size_t>(r.index) * 3;
      if (k + 2 >= L.paperCircles.size())
        break;
      // Paper (cx,cy,r) → clipboard (cx,cy,z,r): a sheet has no elevation, so Z enters as 0.
      cb.circlesCxCyZR.push_back(L.paperCircles[k]);
      cb.circlesCxCyZR.push_back(L.paperCircles[k + 1]);
      cb.circlesCxCyZR.push_back(0.f);
      cb.circlesCxCyZR.push_back(L.paperCircles[k + 2]);  // radius
      cb.circleAttrs.push_back(attrAt(L.paperCircleAttrs, r.index));
      expandBbox(L.paperCircles[k], L.paperCircles[k + 1]);
      break;
    }
    case PaperRef::Type::Arc: {
      if (static_cast<size_t>(r.index) >= L.paperArcs.size())
        break;
      cb.arcs.push_back(L.paperArcs[static_cast<size_t>(r.index)]);
      cb.arcAttrs.push_back(attrAt(L.paperArcAttrs, r.index));
      expandBbox(L.paperArcs[static_cast<size_t>(r.index)].cx, L.paperArcs[static_cast<size_t>(r.index)].cy);
      break;
    }
    case PaperRef::Type::Ellipse: {
      if (static_cast<size_t>(r.index) >= L.paperEllipses.size())
        break;
      cb.ellipses.push_back(L.paperEllipses[static_cast<size_t>(r.index)]);
      cb.ellAttrs.push_back(attrAt(L.paperEllAttrs, r.index));
      expandBbox(L.paperEllipses[static_cast<size_t>(r.index)].cx, L.paperEllipses[static_cast<size_t>(r.index)].cy);
      break;
    }
    case PaperRef::Type::Polyline: {
      const int pi = r.index;
      if (pi < 0 || static_cast<size_t>(pi + 1) >= L.paperPolyOffsets.size())
        break;
      const int v0 = L.paperPolyOffsets[static_cast<size_t>(pi)];
      const int v1 = L.paperPolyOffsets[static_cast<size_t>(pi + 1)];
      if (v1 - v0 < 2)
        break;
      if (cb.polyOffsets.empty())
        cb.polyOffsets.push_back(0);
      const int baseVert = cb.polyOffsets.back();
      for (int vi = v0; vi < v1; ++vi) {
        cb.polyVerts.push_back(L.paperPolyVerts[static_cast<size_t>(vi * 3 + 0)]);
        cb.polyVerts.push_back(L.paperPolyVerts[static_cast<size_t>(vi * 3 + 1)]);
        cb.polyVerts.push_back(L.paperPolyVerts[static_cast<size_t>(vi * 3 + 2)]);
        expandBbox(L.paperPolyVerts[static_cast<size_t>(vi * 3 + 0)], L.paperPolyVerts[static_cast<size_t>(vi * 3 + 1)]);
      }
      cb.polyOffsets.push_back(baseVert + (v1 - v0));
      cb.polyClosed.push_back(static_cast<size_t>(pi) < L.paperPolyClosed.size() ? L.paperPolyClosed[static_cast<size_t>(pi)] : 0u);
      cb.polyAttrs.push_back(attrAt(L.paperPolyAttrs, pi));
      break;
    }
    case PaperRef::Type::Text: {
      if (static_cast<size_t>(r.index) >= L.paperTexts.size())
        break;
      cb.annotations.push_back(L.paperTexts[static_cast<size_t>(r.index)]);
      cb.annotationAttrs.push_back(attrAt(L.paperTextAttrs, r.index));
      expandBbox(L.paperTexts[static_cast<size_t>(r.index)].insX, L.paperTexts[static_cast<size_t>(r.index)].insY);
      break;
    }
    }
  }
  CopyEnclosedFilledRegions(cb, L.paperFilledRegions, L.paperFilledRegionAttrs, mnX, mnY, mxX, mxY);
  if (mnX > mxX) {
    cb.basePtX = 0.f;
    cb.basePtY = 0.f;
  } else {
    cb.basePtX = (mnX + mxX) * 0.5f;
    cb.basePtY = (mnY + mxY) * 0.5f;
  }
  log.push_back("COPYCLIP — " + std::to_string(st.selectedPaperEntities.size()) + " paper object(s) copied to clipboard.");
}

void CopySelectionToClipboard(AppCommandState& st, std::vector<std::string>& log) {
  // Route by active space (ADR-009/013): a paper layout active (and not floating model space) copies the
  // paper selection; otherwise the model selection.
  if (PaperLayout* L = ActivePaperGeometryTarget(st)) {
    CopyPaperSelectionToClipboard(st, *L, log);
    return;
  }
  if (st.selection.empty()) {
    log.push_back("COPYCLIP — nothing selected. Select objects first.");
    return;
  }
  // A surface is not copyable (ADR-036 (b)): the clipboard carries geometry, and a surface's geometry
  // is derived from a definition that names point groups, breaklines and boundaries in THIS drawing.
  // Pasting the triangles alone would produce something that looks like a surface, rebuilds into
  // nothing, and shares an id with the original. Refused out loud rather than by the silent skip the
  // copy loop below would otherwise perform (REQ-201).
  {
    const size_t nSurf = static_cast<size_t>(std::count_if(
        st.selection.begin(), st.selection.end(),
        [](const SelectedEntity& e) { return e.type == SelectedEntity::Type::Surface; }));
    if (nSurf > 0) {
      log.push_back("COPYCLIP — " + std::to_string(nSurf) +
                    " surface(s) not copied: a surface is defined by its point groups, breaklines and"
                    " boundaries, which do not travel with a paste. Create a surface in the target"
                    " drawing instead.");
      if (nSurf == st.selection.size())
        return;  // nothing else was selected — do not go on to clear the clipboard and report success
    }
  }
  CadClipboard& cb = st.clipboard;
  cb = CadClipboard{};
  cb.fromPaper = false;  // copied from model space

  float mnX = 1e30f, mnY = 1e30f, mxX = -1e30f, mxY = -1e30f;
  auto expandBbox = [&](float x, float y) {
    mnX = std::min(mnX, x); mnY = std::min(mnY, y);
    mxX = std::max(mxX, x); mxY = std::max(mxY, y);
  };
  std::set<int> directFills;  // fill indices copied because they were directly selected (REQ-042)

  for (const auto& e : st.selection) {
    if (e.type == SelectedEntity::Type::LineSeg) {
      const size_t k = static_cast<size_t>(e.index) * 6;
      if (k + 5 >= st.userLinesFlat.size())
        continue;
      for (int j = 0; j < 6; ++j)
        cb.lines.push_back(st.userLinesFlat[k + static_cast<size_t>(j)]);
      cb.lineAttrs.push_back(static_cast<size_t>(e.index) < st.userLineAttrs.size()
                                 ? st.userLineAttrs[static_cast<size_t>(e.index)] : EntityAttributes{});
      expandBbox(st.userLinesFlat[k], st.userLinesFlat[k + 1]);
      expandBbox(st.userLinesFlat[k + 3], st.userLinesFlat[k + 4]);
    } else if (e.type == SelectedEntity::Type::Circle) {
      const size_t k = static_cast<size_t>(e.index) * 4;
      if (k + 3 >= st.userCirclesCxCyZR.size())
        continue;
      cb.circlesCxCyZR.push_back(st.userCirclesCxCyZR[k]);
      cb.circlesCxCyZR.push_back(st.userCirclesCxCyZR[k + 1]);
      cb.circlesCxCyZR.push_back(st.userCirclesCxCyZR[k + 2]);  // z survives a model copy
      cb.circlesCxCyZR.push_back(st.userCirclesCxCyZR[k + 3]);
      cb.circleAttrs.push_back(static_cast<size_t>(e.index) < st.userCircleAttrs.size()
                                   ? st.userCircleAttrs[static_cast<size_t>(e.index)] : EntityAttributes{});
      expandBbox(st.userCirclesCxCyZR[k], st.userCirclesCxCyZR[k + 1]);
    } else if (e.type == SelectedEntity::Type::Arc) {
      const size_t k = static_cast<size_t>(e.index);
      if (k >= st.userArcs.size())
        continue;
      cb.arcs.push_back(st.userArcs[k]);
      cb.arcAttrs.push_back(k < st.userArcAttrs.size() ? st.userArcAttrs[k] : EntityAttributes{});
      expandBbox(st.userArcs[k].cx, st.userArcs[k].cy);
    } else if (e.type == SelectedEntity::Type::Ellipse) {
      const size_t k = static_cast<size_t>(e.index);
      if (k >= st.userEllipses.size())
        continue;
      cb.ellipses.push_back(st.userEllipses[k]);
      cb.ellAttrs.push_back(k < st.userEllAttrs.size() ? st.userEllAttrs[k] : EntityAttributes{});
      expandBbox(st.userEllipses[k].cx, st.userEllipses[k].cy);
    } else if (e.type == SelectedEntity::Type::Polyline) {
      const int pi = e.index;
      if (pi < 0 || static_cast<size_t>(pi + 1) >= st.userPolylineOffsets.size())
        continue;
      const int v0 = st.userPolylineOffsets[static_cast<size_t>(pi)];
      const int v1 = st.userPolylineOffsets[static_cast<size_t>(pi + 1)];
      const int nv = v1 - v0;
      if (nv < 2)
        continue;
      if (cb.polyOffsets.empty())
        cb.polyOffsets.push_back(0);
      const int baseVert = cb.polyOffsets.back();
      for (int vi = v0; vi < v1; ++vi) {
        cb.polyVerts.push_back(st.userPolylineVerts[static_cast<size_t>(vi * 3 + 0)]);
        cb.polyVerts.push_back(st.userPolylineVerts[static_cast<size_t>(vi * 3 + 1)]);
        cb.polyVerts.push_back(st.userPolylineVerts[static_cast<size_t>(vi * 3 + 2)]);
        expandBbox(st.userPolylineVerts[static_cast<size_t>(vi * 3 + 0)],
                   st.userPolylineVerts[static_cast<size_t>(vi * 3 + 1)]);
      }
      cb.polyOffsets.push_back(baseVert + nv);
      uint8_t cl = static_cast<size_t>(pi) < st.userPolylineClosed.size()
                       ? st.userPolylineClosed[static_cast<size_t>(pi)] : 0u;
      cb.polyClosed.push_back(cl);
      cb.polyAttrs.push_back(static_cast<size_t>(pi) < st.userPolylineAttrs.size()
                                 ? st.userPolylineAttrs[static_cast<size_t>(pi)] : EntityAttributes{});
    } else if (e.type == SelectedEntity::Type::Annotation) {
      const size_t k = static_cast<size_t>(e.index);
      if (k >= st.cadAnnotations.size())
        continue;
      cb.annotations.push_back(st.cadAnnotations[k]);
      cb.annotationAttrs.push_back(k < st.cadAnnotationAttrs.size()
                                       ? st.cadAnnotationAttrs[k] : EntityAttributes{});
      expandBbox(st.cadAnnotations[k].insX, st.cadAnnotations[k].insY);
    } else if (e.type == SelectedEntity::Type::FilledRegion) {
      const size_t k = static_cast<size_t>(e.index);
      if (k >= st.cadFilledRegions.size())
        continue;
      directFills.insert(static_cast<int>(k));
      const CadFilledRegion& fr = st.cadFilledRegions[k];
      cb.filledRegions.push_back(fr);
      cb.filledRegionAttrs.push_back(k < st.cadFilledRegionAttrs.size() ? st.cadFilledRegionAttrs[k]
                                                                        : EntityAttributes{});
      for (size_t v = 0; v + 2 < fr.vertsXyz.size(); v += 3)
        expandBbox(fr.vertsXyz[v], fr.vertsXyz[v + 1]);
    }
  }
  // Directly-selected fills are copied above; CopyEnclosedFilledRegions adds any *other* fills inside the
  // selection bbox (e.g. a title block's logo hatch carried with its linework — ADR-013 addendum), skipping
  // the ones already taken so they aren't duplicated.
  CopyEnclosedFilledRegions(cb, st.cadFilledRegions, st.cadFilledRegionAttrs, mnX, mnY, mxX, mxY, &directFills);

  if (mnX > mxX) {
    cb.basePtX = 0.f;
    cb.basePtY = 0.f;
  } else {
    cb.basePtX = (mnX + mxX) * 0.5f;
    cb.basePtY = (mnY + mxY) * 0.5f;
  }

  // REQ-087 / REQ-201. The clipboard has no feature-line store, so the walk above skips them — and
  // the count below would still report them as copied, because it counts the SELECTION rather than
  // what was actually taken. A user would then paste and find the feature line missing with no
  // message anywhere. Copying them properly needs clipboard arrays, which is stage 3's successor,
  // not stage 3; saying so is what stops the gap being invisible until someone loses work.
  int featureLinesNotCopied = 0;
  for (const auto& e : st.selection)
    if (e.type == SelectedEntity::Type::FeatureLine)
      ++featureLinesNotCopied;
  log.push_back("COPYCLIP — " +
                std::to_string(st.selection.size() - static_cast<size_t>(featureLinesNotCopied)) +
                " object(s) copied to clipboard.");
  if (featureLinesNotCopied > 0)
    log.push_back("COPYCLIP — " + std::to_string(featureLinesNotCopied) + " feature line" +
                  (featureLinesNotCopied == 1 ? "" : "s") +
                  " not copied: the clipboard cannot carry a feature line yet. Use COPY to "
                  "duplicate one in place.");
}

void StartPasteCommand(AppCommandState& st, std::vector<std::string>& log) {
  if (st.clipboard.empty()) {
    log.push_back("PASTE — clipboard is empty. Use Ctrl+C to copy objects first.");
    return;
  }
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  st.active = AppCommandState::Kind::Paste;
  st.lastCommand = AppCommandState::Kind::Paste;
  st.modifyPhase = AppCommandState::ModifyPhase::NeedDestination;
  st.modifyBaseX = st.clipboard.basePtX;
  st.modifyBaseY = st.clipboard.basePtY;
  st.selBoxWaitingSecond = false;
  log.push_back("PASTE — click destination point to place copied objects. ESC to cancel.");
}

void StartPasteOrigCommand(AppCommandState& st, std::vector<std::string>& log) {
  if (st.clipboard.empty()) {
    log.push_back("PASTEORIG — clipboard is empty. Use Ctrl+C to copy objects first.");
    return;
  }
  PushUndoSnapshot(st, "Paste original");
  CommitPasteFromClipboard(st, 0.f, 0.f, log);
  log.push_back("PASTEORIG — objects pasted at original coordinates.");
}

void StartOffsetCommand(AppCommandState& st, std::vector<std::string>& log) {
  using K = AppCommandState::Kind;
  if (st.active != K::None) {
    log.push_back("OFFSET — finish or cancel the active command first.");
    return;
  }
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  OffsetCmd::ResetOffsetDraft(st);
  st.active = K::Offset;
  st.selBoxWaitingSecond = false;
  log.push_back("OFFSET — select line, circle, arc, ellipse, or polyline. ESC cancels.");
}

float MathAngleRadFromBearingCwNorthDeg(float bearingDegClockwiseFromNorth) {
  constexpr float kDegToRad = 0.01745329251994329577f;
  const float br = bearingDegClockwiseFromNorth * kDegToRad;
  return std::atan2(std::cos(br), std::sin(br));
}

float BearingCwNorthDegFromMathAngleRad(float mathAngleRadFromEastCcw) {
  const double deg =
      std::atan2(static_cast<double>(std::cos(mathAngleRadFromEastCcw)),
                 static_cast<double>(std::sin(mathAngleRadFromEastCcw))) *
      (180.0 / 3.14159265358979323846);
  double out = deg;
  if (out < 0.0)
    out += 360.0;
  return static_cast<float>(out);
}

static void RotatePtForAnnotationPreview(float bx, float by, float rad, float* x, float* y) {
  const float c = std::cos(rad);
  const float s = std::sin(rad);
  float dx = *x - bx;
  float dy = *y - by;
  *x = bx + c * dx - s * dy;
  *y = by + s * dx + c * dy;
}

bool CadRotatePreviewTheta(const AppCommandState& cmd, float curX, float curY, float* outThetaRad) {
  using K = AppCommandState::Kind;
  using RP = AppCommandState::RotatePhase;
  if (cmd.active != K::Rotate || !outThetaRad)
    return false;
  if (cmd.rotatePhase == RP::NeedAngleOrReference) {
    const float dx = curX - cmd.rotateBaseX;
    const float dy = curY - cmd.rotateBaseY;
    // Match typed convention: bearing CW from north (−atan2(dx,dy) equals −bearing_rad for RotateAroundBase).
    *outThetaRad = -std::atan2(dx, dy);
  }
  else if (cmd.rotatePhase == RP::AfterReference_WaitAngleOrP) {
    const float thetaRef =
        std::atan2(cmd.rotateRefY2 - cmd.rotateRefY1, cmd.rotateRefX2 - cmd.rotateRefX1);
    *outThetaRad = std::atan2(curY - cmd.rotateBaseY, curX - cmd.rotateBaseX) - thetaRef;
  } else if (cmd.rotatePhase == RP::AnglePoints_WaitP2)
    *outThetaRad = RotateDeltaFromReferenceAndNewSegment(cmd.rotateRefX1, cmd.rotateRefY1, cmd.rotateRefX2,
                                                         cmd.rotateRefY2, cmd.rotateAnglePt1X,
                                                         cmd.rotateAnglePt1Y, curX, curY);
  else
    return false;
  return true;
}

bool CadScalePreviewFactor(const AppCommandState& cmd, float curX, float curY, float* outScale) {
  using K = AppCommandState::Kind;
  using MP = AppCommandState::ModifyPhase;
  using SP = AppCommandState::ScalePhase;
  if (cmd.active != K::Scale || !outScale)
    return false;
  if (cmd.modifyPhase != MP::NeedDestination)
    return false;
  if (cmd.scalePhase == SP::FactorPick) {
    const float d = std::hypot(curX - cmd.modifyBaseX, curY - cmd.modifyBaseY);
    float s = d / std::max(cmd.scaleRefDist, 1e-20f);
    *outScale = std::max(s, 1e-6f);
    return true;
  }
  if (cmd.scalePhase == SP::NewLength_WaitP2) {
    const float d = std::hypot(curX - cmd.scaleNewLenP1X, curY - cmd.scaleNewLenP1Y);
    float s = d / std::max(cmd.scaleRefDist, 1e-20f);
    *outScale = std::max(s, 1e-6f);
    return true;
  }
  return false;
}

static void CadAnnotationPreviewTranslated(const CadAnnotation& src, float dx, float dy, CadAnnotation* out) {
  if (!out)
    return;
  *out = src;
  out->insX += dx;
  out->insY += dy;
  if (out->kind == CadAnnotation::Kind::Mtext) {
    out->boxMinX += dx;
    out->boxMinY += dy;
    out->boxMaxX += dx;
    out->boxMaxY += dy;
  } else if (out->kind == CadAnnotation::Kind::DimAligned || out->kind == CadAnnotation::Kind::DimLinear) {
    out->dimExt1X += dx;
    out->dimExt1Y += dy;
    out->dimExt2X += dx;
    out->dimExt2Y += dy;
  }
}

static void CadAnnotationPreviewRotated(const CadAnnotation& src, float bx, float by, float rad, CadAnnotation* out) {
  if (!out)
    return;
  *out = src;
  CadAnnotation& a = *out;
  RotatePtForAnnotationPreview(bx, by, rad, &a.insX, &a.insY);
  if (a.kind == CadAnnotation::Kind::Text) {
    a.rotationRad += rad;
  } else if (a.kind == CadAnnotation::Kind::DimLinear) {
    RotateCadDimLinearAroundBase(bx, by, rad, &a);
  } else if (a.kind == CadAnnotation::Kind::DimAligned) {
    RotatePtForAnnotationPreview(bx, by, rad, &a.dimExt1X, &a.dimExt1Y);
    RotatePtForAnnotationPreview(bx, by, rad, &a.dimExt2X, &a.dimExt2Y);
    float sx1 = 0.f, sy1 = 0.f, sx2 = 0.f, sy2 = 0.f, tx = 0.f, ty = 0.f, nx = 0.f, ny = 0.f, ml = 0.f;
    if (CadDimAlignedGeometry(a, &sx1, &sy1, &sx2, &sy2, &tx, &ty, &nx, &ny, &ml))
      a.rotationRad = std::atan2(ty, tx);
  } else {
    float xs[4] = {a.boxMinX, a.boxMaxX, a.boxMaxX, a.boxMinX};
    float ys[4] = {a.boxMinY, a.boxMinY, a.boxMaxY, a.boxMaxY};
    float mnX = xs[0];
    float mxX = xs[0];
    float mnY = ys[0];
    float mxY = ys[0];
    for (int i = 0; i < 4; ++i) {
      RotatePtForAnnotationPreview(bx, by, rad, &xs[i], &ys[i]);
      mnX = std::min(mnX, xs[i]);
      mxX = std::max(mxX, xs[i]);
      mnY = std::min(mnY, ys[i]);
      mxY = std::max(mxY, ys[i]);
    }
    a.boxMinX = mnX;
    a.boxMaxX = mxX;
    a.boxMinY = mnY;
    a.boxMaxY = mxY;
    a.insX = mnX;
    a.insY = mnY;
  }
}

static void CadAnnotationPreviewScaled(const CadAnnotation& src, float bx, float by, float sc, CadAnnotation* out) {
  if (!out)
    return;
  *out = src;
  CadAnnotation& a = *out;
  ScalePtAroundBase(bx, by, sc, &a.insX, &a.insY);
  if (a.kind == CadAnnotation::Kind::Text) {
    a.plottedHeightInches = std::max(a.plottedHeightInches * sc, 1.e-6f);
  } else if (a.kind == CadAnnotation::Kind::DimLinear) {
    ScaleCadDimLinearAroundBase(bx, by, sc, &a);
    ScalePtAroundBase(bx, by, sc, &a.insX, &a.insY);
    a.plottedHeightInches = std::max(a.plottedHeightInches * sc, 1.e-6f);
  } else if (a.kind == CadAnnotation::Kind::DimAligned) {
    ScalePtAroundBase(bx, by, sc, &a.dimExt1X, &a.dimExt1Y);
    ScalePtAroundBase(bx, by, sc, &a.dimExt2X, &a.dimExt2Y);
    a.dimSignedOffset *= sc;
    ScalePtAroundBase(bx, by, sc, &a.insX, &a.insY);
    a.plottedHeightInches = std::max(a.plottedHeightInches * sc, 1.e-6f);
    float sx1 = 0.f, sy1 = 0.f, sx2 = 0.f, sy2 = 0.f, tx = 0.f, ty = 0.f, nx = 0.f, ny = 0.f, ml = 0.f;
    if (CadDimAlignedGeometry(a, &sx1, &sy1, &sx2, &sy2, &tx, &ty, &nx, &ny, &ml))
      a.rotationRad = std::atan2(ty, tx);
  } else if (a.kind == CadAnnotation::Kind::DimAngular) {
    ScalePtAroundBase(bx, by, sc, &a.dimAngVertexX, &a.dimAngVertexY);
    ScalePtAroundBase(bx, by, sc, &a.dimExt1X, &a.dimExt1Y);
    ScalePtAroundBase(bx, by, sc, &a.dimExt2X, &a.dimExt2Y);
    a.dimSignedOffset *= sc;
    ScalePtAroundBase(bx, by, sc, &a.insX, &a.insY);
    a.plottedHeightInches = std::max(a.plottedHeightInches * sc, 1.e-6f);
  } else {
    ScalePtAroundBase(bx, by, sc, &a.boxMinX, &a.boxMinY);
    ScalePtAroundBase(bx, by, sc, &a.boxMaxX, &a.boxMaxY);
    if (a.boxMinX > a.boxMaxX)
      std::swap(a.boxMinX, a.boxMaxX);
    if (a.boxMinY > a.boxMaxY)
      std::swap(a.boxMinY, a.boxMaxY);
    a.insX = a.boxMinX;
    a.insY = a.boxMinY;
    a.plottedHeightInches = std::max(a.plottedHeightInches * sc, 1.e-6f);
  }
}

void CadAnnotationCollectTransformPreviews(const AppCommandState& cmd, float curX, float curY,
                                           std::vector<CadAnnotation>* out) {
  if (!out)
    return;
  out->clear();
  using K = AppCommandState::Kind;
  using MP = AppCommandState::ModifyPhase;
  if ((cmd.active == K::Move || cmd.active == K::Copy) && cmd.modifyPhase == MP::NeedDestination) {
    const float dx = curX - cmd.modifyBaseX;
    const float dy = curY - cmd.modifyBaseY;
    for (const auto& e : cmd.selection) {
      if (e.type != SelectedEntity::Type::Annotation)
        continue;
      const size_t k = static_cast<size_t>(e.index);
      if (k >= cmd.cadAnnotations.size())
        continue;
      CadAnnotation p{};
      CadAnnotationPreviewTranslated(cmd.cadAnnotations[k], dx, dy, &p);
      out->push_back(p);
    }
    return;
  }
  if (cmd.active == K::Paste && cmd.modifyPhase == MP::NeedDestination) {
    const float dx = curX - cmd.modifyBaseX;
    const float dy = curY - cmd.modifyBaseY;
    for (const auto& ann : cmd.clipboard.annotations) {
      CadAnnotation p{};
      CadAnnotationPreviewTranslated(ann, dx, dy, &p);
      out->push_back(p);
    }
    return;
  }
  float sc = 1.f;
  if (cmd.active == K::Scale && cmd.modifyPhase == MP::NeedDestination) {
    if (!CadScalePreviewFactor(cmd, curX, curY, &sc))
      return;
    const float bx = cmd.modifyBaseX;
    const float by = cmd.modifyBaseY;
    for (const auto& e : cmd.selection) {
      if (e.type != SelectedEntity::Type::Annotation)
        continue;
      const size_t k = static_cast<size_t>(e.index);
      if (k >= cmd.cadAnnotations.size())
        continue;
      CadAnnotation p{};
      CadAnnotationPreviewScaled(cmd.cadAnnotations[k], bx, by, sc, &p);
      out->push_back(p);
    }
    return;
  }
  float theta = 0.f;
  if (!CadRotatePreviewTheta(cmd, curX, curY, &theta))
    return;
  const float bx = cmd.rotateBaseX;
  const float by = cmd.rotateBaseY;
  for (const auto& e : cmd.selection) {
    if (e.type != SelectedEntity::Type::Annotation)
      continue;
    const size_t k = static_cast<size_t>(e.index);
    if (k >= cmd.cadAnnotations.size())
      continue;
    CadAnnotation p{};
    CadAnnotationPreviewRotated(cmd.cadAnnotations[k], bx, by, theta, &p);
    out->push_back(p);
  }
}

bool ComputeWorldExtents(const AppCommandState& st, double* outMnX, double* outMxX, double* outMnY, double* outMxY) {
  bool any = false;
  double mnX = 0.;
  double mxX = 0.;
  double mnY = 0.;
  double mxY = 0.;
  auto consider = [&](double x, double y) { ExpandExtents(x, y, &mnX, &mxX, &mnY, &mxY, &any); };

  const auto& L = st.userLinesFlat;
  if (L.size() % 6 == 0) {
    for (size_t i = 0; i + 5 < L.size(); i += 6) {
      consider(static_cast<double>(L[i]), static_cast<double>(L[i + 1]));
      consider(static_cast<double>(L[i + 3]), static_cast<double>(L[i + 4]));
    }
  }
  const auto& C = st.userCirclesCxCyZR;
  if (C.size() % 4 == 0) {
    for (size_t ci = 0; ci + 3 < C.size(); ci += 4) {
      const double cx = static_cast<double>(C[ci]);
      const double cy = static_cast<double>(C[ci + 1]);
      const double r = std::fabs(static_cast<double>(C[ci + 3]));
      if (r <= 1e-12)
        continue;
      consider(cx - r, cy - r);
      consider(cx + r, cy - r);
      consider(cx - r, cy + r);
      consider(cx + r, cy + r);
    }
  }
  for (const SurveyPoint& p : st.surveyPoints)
    consider(static_cast<double>(p.easting), static_cast<double>(p.northing));

  for (const CadAnnotation& a : st.cadAnnotations) {
    float amnX = 0.f;
    float amnY = 0.f;
    float amxX = 0.f;
    float amxY = 0.f;
    CadAnnotationRoughBounds(a, st.modelUnitsPerPlottedInch, &amnX, &amnY, &amxX, &amxY);
    consider(static_cast<double>(amnX), static_cast<double>(amnY));
    consider(static_cast<double>(amxX), static_cast<double>(amxY));
  }

  for (const CadArc& a : st.userArcs) {
    const double dcx = static_cast<double>(a.cx);
    const double dcy = static_cast<double>(a.cy);
    const double dr = std::fabs(static_cast<double>(a.r));
    if (dr <= 1e-12)
      continue;
    const int n = std::max(8, static_cast<int>(std::fabs(static_cast<double>(a.sweepRad)) / (3.14159265 / 16.0)) + 1);
    for (int i = 0; i <= n; ++i) {
      const double u = static_cast<double>(i) / static_cast<double>(n);
      const double t = static_cast<double>(a.startRad) + static_cast<double>(a.sweepRad) * u;
      double wx = 0.;
      double wy = 0.;
      CirclePointWorld(dcx, dcy, dr, t, &wx, &wy);
      consider(wx, wy);
    }
  }

  for (const CadEllipse& el : st.userEllipses) {
    const double ma = std::hypot(static_cast<double>(el.majVx), static_cast<double>(el.majVy));
    if (ma < 1e-12)
      continue;
    constexpr int n = 48;
    constexpr double kTwoPi = 6.283185307179586;
    const double ux = static_cast<double>(el.majVx) / ma;
    const double uy = static_cast<double>(el.majVy) / ma;
    const double px = -uy;
    const double py = ux;
    const double mb = ma * static_cast<double>(el.ratio);
    const double ecx = static_cast<double>(el.cx);
    const double ecy = static_cast<double>(el.cy);
    for (int i = 0; i < n; ++i) {
      const double ang = kTwoPi * static_cast<double>(i) / static_cast<double>(n);
      const double c = std::cos(ang);
      const double s = std::sin(ang);
      consider(ecx + ux * (ma * c) + px * (mb * s), ecy + uy * (ma * c) + py * (mb * s));
    }
  }

  const auto& PV = st.userPolylineVerts;
  const auto& PO = st.userPolylineOffsets;
  if (PO.size() >= 2) {
    for (size_t pi = 0; pi + 1 < PO.size(); ++pi) {
      const int v0 = PO[pi];
      const int v1 = PO[pi + 1];
      for (int vi = v0; vi < v1; ++vi) {
        consider(static_cast<double>(PV[static_cast<size_t>(vi * 3 + 0)]),
                 static_cast<double>(PV[static_cast<size_t>(vi * 3 + 1)]));
      }
    }
  }

  // Feature lines (REQ-087). This is the SMALL-drawing path — under 16 entities — and a drawing
  // holding only feature lines lands here, so missing it would mean ZOOM EXTENTS reporting
  // "nothing to frame" on a drawing that plainly has content.
  {
    const auto& FV = st.featureLineVerts;
    const auto& FO = st.featureLineOffsets;
    for (size_t fi = 0; fi + 1 < FO.size(); ++fi) {
      const int v0 = FO[fi];
      const int v1 = FO[fi + 1];
      for (int vi = v0; vi < v1; ++vi) {
        if (static_cast<size_t>(vi * 3 + 1) >= FV.size())
          break;
        consider(static_cast<double>(FV[static_cast<size_t>(vi * 3 + 0)]),
                 static_cast<double>(FV[static_cast<size_t>(vi * 3 + 1)]));
      }
    }
  }

  for (const CadFilledRegion& fr : st.cadFilledRegions)
    for (size_t i = 0; i + 2 < fr.vertsXyz.size(); i += 3)
      consider(static_cast<double>(fr.vertsXyz[i]), static_cast<double>(fr.vertsXyz[i + 1]));

  // Meshes (REQ-063). Their precomputed bounds, not their vertices: this path runs for small
  // drawings, and "small" counts entities — one mesh can still hold two million triangles.
  for (const auto& mp : st.cadMeshes) {
    if (!mp)
      continue;
    const meshgeom::Bounds mb = meshgeom::ComputeBounds(mp->vertsXyz);
    if (!mb.valid)
      continue;
    consider(static_cast<double>(mb.mnX), static_cast<double>(mb.mnY));
    consider(static_cast<double>(mb.mxX), static_cast<double>(mb.mxY));
  }

  // TIN surfaces (REQ-068: "surfaces are included in zoom-extents and in the drawing's bounding
  // box"). Their bounds, not their vertices, for the same reason meshes use theirs above — a single
  // surface can hold 200k triangles.
  for (const CadSurface& s : st.cadSurfaces) {
    if (!s.tin)
      continue;
    const meshgeom::Bounds sb = meshgeom::ComputeBounds(s.tin->vertsXyz);
    if (!sb.valid)
      continue;
    consider(static_cast<double>(sb.mnX), static_cast<double>(sb.mnY));
    consider(static_cast<double>(sb.mxX), static_cast<double>(sb.mxY));
  }

  if (!any)
    return false;
  *outMnX = mnX;
  *outMxX = mxX;
  *outMnY = mnY;
  *outMxY = mxY;
  return true;
}

namespace {

struct EntityBox {
  double cx;
  double cy;
  double mnX;
  double mxX;
  double mnY;
  double mxY;
};

[[nodiscard]] double NthPercentile(std::vector<double>& v, double p) {
  if (v.empty())
    return 0.;
  const size_t n = v.size();
  const double idxF = p * static_cast<double>(n - 1);
  const size_t k = std::clamp(static_cast<size_t>(idxF), size_t{0}, n - 1);
  std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(k), v.end());
  return v[k];
}

void CollectEntityBoxes(const AppCommandState& st, std::vector<EntityBox>& out) {
  const auto& L = st.userLinesFlat;
  if (L.size() % 6 == 0) {
    for (size_t i = 0; i + 5 < L.size(); i += 6) {
      EntityBox b{};
      b.mnX = std::min(static_cast<double>(L[i]), static_cast<double>(L[i + 3]));
      b.mxX = std::max(static_cast<double>(L[i]), static_cast<double>(L[i + 3]));
      b.mnY = std::min(static_cast<double>(L[i + 1]), static_cast<double>(L[i + 4]));
      b.mxY = std::max(static_cast<double>(L[i + 1]), static_cast<double>(L[i + 4]));
      b.cx = 0.5 * (b.mnX + b.mxX);
      b.cy = 0.5 * (b.mnY + b.mxY);
      out.push_back(b);
    }
  }
  const auto& C = st.userCirclesCxCyZR;
  if (C.size() % 4 == 0) {
    for (size_t ci = 0; ci + 3 < C.size(); ci += 4) {
      const double cx = static_cast<double>(C[ci]);
      const double cy = static_cast<double>(C[ci + 1]);
      const double r = std::fabs(static_cast<double>(C[ci + 3]));
      if (r <= 1e-12)
        continue;
      EntityBox b{};
      b.mnX = cx - r;
      b.mxX = cx + r;
      b.mnY = cy - r;
      b.mxY = cy + r;
      b.cx = cx;
      b.cy = cy;
      out.push_back(b);
    }
  }
  for (const SurveyPoint& p : st.surveyPoints) {
    EntityBox b{};
    b.mnX = b.mxX = b.cx = static_cast<double>(p.easting);
    b.mnY = b.mxY = b.cy = static_cast<double>(p.northing);
    out.push_back(b);
  }
  for (const CadAnnotation& a : st.cadAnnotations) {
    float amnX = 0.f;
    float amnY = 0.f;
    float amxX = 0.f;
    float amxY = 0.f;
    CadAnnotationRoughBounds(a, st.modelUnitsPerPlottedInch, &amnX, &amnY, &amxX, &amxY);
    EntityBox b{};
    b.mnX = static_cast<double>(amnX);
    b.mxX = static_cast<double>(amxX);
    b.mnY = static_cast<double>(amnY);
    b.mxY = static_cast<double>(amxY);
    b.cx = 0.5 * (b.mnX + b.mxX);
    b.cy = 0.5 * (b.mnY + b.mxY);
    out.push_back(b);
  }
  for (const CadArc& a : st.userArcs) {
    const double dr = std::fabs(static_cast<double>(a.r));
    if (dr <= 1e-12)
      continue;
    EntityBox b{};
    b.mnX = static_cast<double>(a.cx) - dr;
    b.mxX = static_cast<double>(a.cx) + dr;
    b.mnY = static_cast<double>(a.cy) - dr;
    b.mxY = static_cast<double>(a.cy) + dr;
    b.cx = static_cast<double>(a.cx);
    b.cy = static_cast<double>(a.cy);
    out.push_back(b);
  }
  for (const CadEllipse& el : st.userEllipses) {
    const double ma = std::hypot(static_cast<double>(el.majVx), static_cast<double>(el.majVy));
    if (ma < 1e-12)
      continue;
    const double mb = ma * static_cast<double>(el.ratio);
    const double rrx = std::hypot(ma, mb);
    EntityBox b{};
    b.mnX = static_cast<double>(el.cx) - rrx;
    b.mxX = static_cast<double>(el.cx) + rrx;
    b.mnY = static_cast<double>(el.cy) - rrx;
    b.mxY = static_cast<double>(el.cy) + rrx;
    b.cx = static_cast<double>(el.cx);
    b.cy = static_cast<double>(el.cy);
    out.push_back(b);
  }
  const auto& PV = st.userPolylineVerts;
  const auto& PO = st.userPolylineOffsets;
  if (PO.size() >= 2) {
    for (size_t pi = 0; pi + 1 < PO.size(); ++pi) {
      const int v0 = PO[pi];
      const int v1 = PO[pi + 1];
      if (v1 <= v0)
        continue;
      EntityBox b{};
      bool any = false;
      for (int vi = v0; vi < v1; ++vi) {
        const double vx = static_cast<double>(PV[static_cast<size_t>(vi * 3 + 0)]);
        const double vy = static_cast<double>(PV[static_cast<size_t>(vi * 3 + 1)]);
        if (!any) {
          b.mnX = b.mxX = vx;
          b.mnY = b.mxY = vy;
          any = true;
        } else {
          b.mnX = std::min(b.mnX, vx);
          b.mxX = std::max(b.mxX, vx);
          b.mnY = std::min(b.mnY, vy);
          b.mxY = std::max(b.mxY, vy);
        }
      }
      if (!any)
        continue;
      b.cx = 0.5 * (b.mnX + b.mxX);
      b.cy = 0.5 * (b.mnY + b.mxY);
      out.push_back(b);
    }
  }

  // Feature lines (REQ-087) — same CSR walk. Without this, ZOOM EXTENTS frames a drawing as though
  // its feature lines were not there, which is the quiet kind of miss ADR-035 (g) warns about.
  const auto& FO = st.featureLineOffsets;
  const auto& FV = st.featureLineVerts;
  if (FO.size() >= 2) {
    for (size_t fi = 0; fi + 1 < FO.size(); ++fi) {
      const int v0 = FO[fi];
      const int v1 = FO[fi + 1];
      if (v1 <= v0)
        continue;
      EntityBox b{};
      bool any = false;
      for (int vi = v0; vi < v1; ++vi) {
        if (static_cast<size_t>(vi * 3 + 1) >= FV.size())
          break;
        const double vx = static_cast<double>(FV[static_cast<size_t>(vi * 3 + 0)]);
        const double vy = static_cast<double>(FV[static_cast<size_t>(vi * 3 + 1)]);
        if (!any) {
          b.mnX = b.mxX = vx;
          b.mnY = b.mxY = vy;
          any = true;
        } else {
          b.mnX = std::min(b.mnX, vx);
          b.mxX = std::max(b.mxX, vx);
          b.mnY = std::min(b.mnY, vy);
          b.mxY = std::max(b.mxY, vy);
        }
      }
      if (!any)
        continue;
      b.cx = 0.5 * (b.mnX + b.mxX);
      b.cy = 0.5 * (b.mnY + b.mxY);
      out.push_back(b);
    }
  }

  // Imported meshes (REQ-063: "meshes are included in zoom-extents and the drawing's bounding
  // box"). One box per mesh rather than per triangle — the extents pass is an outlier-trimmed
  // statistic over ENTITIES, and feeding it two million triangles would both swamp that statistic
  // and make ZE cost a full mesh walk per invocation.
  for (const auto& mp : st.cadMeshes) {
    if (!mp)
      continue;
    const meshgeom::Bounds mb = meshgeom::ComputeBounds(mp->vertsXyz);
    if (!mb.valid)
      continue;
    EntityBox b{};
    b.mnX = static_cast<double>(mb.mnX);
    b.mxX = static_cast<double>(mb.mxX);
    b.mnY = static_cast<double>(mb.mnY);
    b.mxY = static_cast<double>(mb.mxY);
    b.cx = 0.5 * (b.mnX + b.mxX);
    b.cy = 0.5 * (b.mnY + b.mxY);
    out.push_back(b);
  }

  // TIN surfaces (REQ-068), one box per surface — same reasoning as the meshes above.
  for (const CadSurface& s : st.cadSurfaces) {
    if (!s.tin)
      continue;
    const meshgeom::Bounds sb = meshgeom::ComputeBounds(s.tin->vertsXyz);
    if (!sb.valid)
      continue;
    EntityBox b{};
    b.mnX = static_cast<double>(sb.mnX);
    b.mxX = static_cast<double>(sb.mxX);
    b.mnY = static_cast<double>(sb.mnY);
    b.mxY = static_cast<double>(sb.mxY);
    b.cx = 0.5 * (b.mnX + b.mxX);
    b.cy = 0.5 * (b.mnY + b.mxY);
    out.push_back(b);
  }
}

} // namespace

bool ComputeRobustWorldExtents(const AppCommandState& st, double* outMnX, double* outMxX, double* outMnY,
                               double* outMxY, int* outSkipped) {
  if (outSkipped)
    *outSkipped = 0;
  std::vector<EntityBox> ents;
  ents.reserve(st.userLinesFlat.size() / 6 + st.userCirclesCxCyZR.size() / 4 + st.userArcs.size() +
               st.userEllipses.size() + st.cadAnnotations.size() + st.surveyPoints.size() +
               (st.userPolylineOffsets.empty() ? 0 : st.userPolylineOffsets.size() - 1));
  CollectEntityBoxes(st, ents);

  if (ents.size() < 16)
    return ComputeWorldExtents(st, outMnX, outMxX, outMnY, outMxY);

  std::vector<double> xs;
  std::vector<double> ys;
  xs.reserve(ents.size());
  ys.reserve(ents.size());
  for (const EntityBox& b : ents) {
    xs.push_back(b.cx);
    ys.push_back(b.cy);
  }

  std::vector<double> xsCopy = xs;
  std::vector<double> ysCopy = ys;
  const double xP05 = NthPercentile(xsCopy, 0.05);
  xsCopy = xs;
  const double xP95 = NthPercentile(xsCopy, 0.95);
  const double yP05 = NthPercentile(ysCopy, 0.05);
  ysCopy = ys;
  const double yP95 = NthPercentile(ysCopy, 0.95);

  const double bulkSpanX = std::max(xP95 - xP05, 0.);
  const double bulkSpanY = std::max(yP95 - yP05, 0.);
  const double midX = 0.5 * (xP05 + xP95);
  const double midY = 0.5 * (yP05 + yP95);

  // Outlier window: entities whose center is within ±5× the bulk span from the bulk midpoint are kept.
  // A 5× pad is generous enough to retain legitimate sparse content while still rejecting (0,0)-anchored
  // strays in large DXFs.
  const double radX = std::max(bulkSpanX * 5.0, 1.0);
  const double radY = std::max(bulkSpanY * 5.0, 1.0);

  bool any = false;
  double mnX = 0., mxX = 0., mnY = 0., mxY = 0.;
  int skipped = 0;
  for (const EntityBox& b : ents) {
    if (std::fabs(b.cx - midX) > radX || std::fabs(b.cy - midY) > radY) {
      ++skipped;
      continue;
    }
    if (!any) {
      mnX = b.mnX;
      mxX = b.mxX;
      mnY = b.mnY;
      mxY = b.mxY;
      any = true;
    } else {
      mnX = std::min(mnX, b.mnX);
      mxX = std::max(mxX, b.mxX);
      mnY = std::min(mnY, b.mnY);
      mxY = std::max(mxY, b.mxY);
    }
  }

  if (!any)
    return ComputeWorldExtents(st, outMnX, outMxX, outMnY, outMxY);

  *outMnX = mnX;
  *outMxX = mxX;
  *outMnY = mnY;
  *outMxY = mxY;
  if (outSkipped)
    *outSkipped = skipped;
  return true;
}

void ApplyViewportZoomToWorldRect(double mnX, double mxX, double mnY, double mxY, double* panX, double* panY,
                                  float* zoom, int fbW, int fbH, float viewportAspect) {
  (void)fbW;
  (void)fbH;
  const float aspect = std::max(viewportAspect, 1e-6f);
  constexpr float kMargin = 0.08f;
  constexpr double kMinSpan = 1e-5;
  double dmnX = mnX;
  double dmxX = mxX;
  double dmnY = mnY;
  double dmxY = mxY;
  double rw = dmxX - dmnX;
  double rh = dmxY - dmnY;
  if (rw < kMinSpan) {
    dmnX -= kMinSpan;
    dmxX += kMinSpan;
    rw = dmxX - dmnX;
  }
  if (rh < kMinSpan) {
    dmnY -= kMinSpan;
    dmxY += kMinSpan;
    rh = dmxY - dmnY;
  }
  const double cx = 0.5 * (dmnX + dmxX);
  const double cy = 0.5 * (dmnY + dmxY);
  const double denom = 2.0 * (1.0 - static_cast<double>(kMargin));
  const double needHalfH = std::max(rh / denom, rw / (static_cast<double>(aspect) * denom));
  constexpr float kOrthoHalfHRef = 50.f;
  *panX = cx;
  *panY = cy;
  *zoom = std::clamp(kOrthoHalfHRef / static_cast<float>(std::max(needHalfH, 1e-8)), 1.e-9f, 1.e9f);
}

bool ParseAngleDegrees(const std::string& raw, float* degreesOut) {
  return ParseAngleDegreesInternal(raw, degreesOut);
}

// Parse a typed world point in DOUBLE (REQ-101). This is the real implementation; the float overload
// below narrows its result.
//
// The precision this preserves is not academic. A typed easting is a decimal string, and narrowing it
// to `float` before the document origin has been subtracted quantizes it at the magnitude of the
// WORLD value: at easting 2e6 the float spacing is 0.25 ft, so `2000000.10` became `2000000.125` —
// 0.025 ft of error introduced by the parse itself, before any commit, save or load, and REQ-101
// allows 0.01 ft. Subtracting the origin first and narrowing afterwards quantizes at the magnitude of
// the LOCAL value instead, which is small by construction, so the same input lands within ~1e-4 ft.
// Nothing downstream can recover what the old order threw away.
bool ParseWorldPointD(const std::string& raw, double* ox, double* oy, bool allowRelative, double baseX,
                      double baseY) {
  if (!ox || !oy)
    return false;
  std::string s = StringUtil::trimCopy(raw);
  if (s.empty())
    return false;
  if (!s.empty() && s[0] == '@') {
    if (!allowRelative)
      return false;
    s = StringUtil::trimCopy(s.substr(1));
    double dx = 0.;
    double dy = 0.;
    if (!ParseTwoDoubles(s, &dx, &dy))
      return false;
    *ox = baseX + dx;
    *oy = baseY + dy;
    // The sum can overflow even though the base and the delta are each representable, and this is the
    // ONE path into a coordinate that is non-finite from finite input. An absolute coordinate that
    // overflows is already refused below — the stream extraction sets failbit — so re-checking here
    // restores this function's own existing guarantee rather than adding a new rule. Every caller
    // already reports a false return as a parse failure, which is what satisfies REQ-201.
    // Found while fixing issue #59; reproduced for LINE, POLYLINE and RECT.
    if (!std::isfinite(*ox) || !std::isfinite(*oy))
      return false;
    return true;
  }
  return ParseTwoDoubles(s, ox, oy);
}

bool ParseWorldPoint(const std::string& raw, float* ox, float* oy, bool allowRelative, float baseX, float baseY) {
  if (!ox || !oy)
    return false;
  double wx = 0.;
  double wy = 0.;
  if (!ParseWorldPointD(raw, &wx, &wy, allowRelative, static_cast<double>(baseX), static_cast<double>(baseY)))
    return false;
  // Narrowing can overflow to infinity where the double did not, so the finiteness guarantee has to be
  // re-checked at the narrower type rather than inherited from the call above.
  const float fx = static_cast<float>(wx);
  const float fy = static_cast<float>(wy);
  if (!std::isfinite(fx) || !std::isfinite(fy))
    return false;
  *ox = fx;
  *oy = fy;
  return true;
}

bool ParseStoragePoint(const AppCommandState& st, const std::string& raw, float* lx, float* ly, bool allowRelative,
                       float baseLocalX, float baseLocalY) {
  if (!lx || !ly)
    return false;
  double baseWx = 0.;
  double baseWy = 0.;
  CadCoord::WorldFromLocal(st, baseLocalX, baseLocalY, &baseWx, &baseWy);
  // Parsed in double and narrowed by LocalFromWorld only AFTER the origin is subtracted — that
  // ordering is the whole point (REQ-101). The origin itself is established before dispatch, by
  // MaybeEstablishDocumentOriginFromTypedPoint in ProcessCommandLineSubmit, so by the time any
  // command's parse runs the frame can already represent what was typed.
  double wx = 0.;
  double wy = 0.;
  if (!ParseWorldPointD(raw, &wx, &wy, allowRelative, baseWx, baseWy))
    return false;
  CadCoord::LocalFromWorld(st, wx, wy, lx, ly);
  return !std::isfinite(*lx) || !std::isfinite(*ly) ? false : true;
}

void ApplyOrthoConstrainFromAnchor(float anchorX, float anchorY, float* wx, float* wy, bool ortho) {
  OrthoConstrainPoint(anchorX, anchorY, wx, wy, ortho);  // REQ-047: one tested implementation
}

void ApplySegmentAngleLockToWorldPick(float anchorX, float anchorY, float lockUx, float lockUy, float* wx, float* wy,
                                      bool forwardOnly) {
  if (!wx || !wy)
    return;
  const float dx = *wx - anchorX;
  const float dy = *wy - anchorY;
  float t = dx * lockUx + dy * lockUy;
  if (forwardOnly && t < 0.f)
    t = 0.f;
  *wx = anchorX + t * lockUx;
  *wy = anchorY + t * lockUy;
}

static float NormalizeBearingDegreesCwNorth(float deg) {
  deg = std::fmod(deg, 360.f);
  if (deg < 0.f)
    deg += 360.f;
  return deg;
}

static bool ParseBearingCwNorthStringWithOptionalDelta(const std::string& raw, float* bearingCombinedDegOut,
                                                       std::vector<std::string>& log) {
  std::string work = StringUtil::trimCopy(raw);
  if (work.empty())
    return false;
  float bear = 0.f;
  float delta = 0.f;
  bool hasDelta = false;
  std::string bearOnly = work;

  const size_t sp = work.rfind(' ');
  if (sp != std::string::npos && sp + 1 < work.size()) {
    std::string tail = StringUtil::trimCopy(work.substr(sp + 1));
    std::string head = StringUtil::trimCopy(work.substr(0, sp));
    if (!tail.empty() && (tail[0] == '+' || tail[0] == '-')) {
      if (!ParseAngleDegreesInternal(tail, &delta)) {
        log.push_back("Invalid adjustment — use +90 / -45 (decimal or DMS).");
        return false;
      }
      hasDelta = true;
      bearOnly = head;
    }
  }

  if (!hasDelta) {
    for (size_t k = 1; k < work.size(); ++k) {
      if (work[k] != '+' && work[k] != '-')
        continue;
      std::string head = StringUtil::trimCopy(work.substr(0, k));
      std::string tail = StringUtil::trimCopy(work.substr(k));
      if (head.empty() || tail.empty())
        continue;
      if (!ParseAngleDegreesInternal(head, &bear))
        continue;
      if (!ParseAngleDegreesInternal(tail, &delta))
        continue;
      *bearingCombinedDegOut = NormalizeBearingDegreesCwNorth(bear + delta);
      return true;
    }
  }

  if (!ParseAngleDegreesInternal(bearOnly, &bear)) {
    log.push_back("Could not parse bearing — decimal degrees or DMS (° clockwise from north).");
    return false;
  }
  *bearingCombinedDegOut = NormalizeBearingDegreesCwNorth(bear + (hasDelta ? delta : 0.f));
  return true;
}

static void CommitSegmentAnglePickLock(AppCommandState& st, std::vector<std::string>& log) {
  using SAP = AppCommandState::SegmentAnglePickPhase;
  const float br = NormalizeBearingDegreesCwNorth(st.segmentPickDraftBearingDeg);
  const float theta = MathAngleRadFromBearingCwNorthDeg(br);
  st.segmentLockUx = std::cos(theta);
  st.segmentLockUy = std::sin(theta);
  st.segmentAngleLockActive = true;
  st.segmentAnglePickPhase = SAP::Idle;
  char buf[144];
  std::snprintf(buf, sizeof(buf),
                "Bearing locked %.6g° clockwise from north — distance (+/-) or click on ray (A clears).",
                static_cast<double>(br));
  log.push_back(buf);
}

void CancelSegmentAnglePick(AppCommandState& st, std::vector<std::string>* log) {
  using SAP = AppCommandState::SegmentAnglePickPhase;
  const bool hadPick = st.segmentAnglePickPhase != SAP::Idle;
  st.segmentAnglePickPhase = SAP::Idle;
  st.segmentAngleKeyboardAwaitBearing = false;
  if (hadPick && log)
    log->push_back("Bearing pick — canceled.");
}

bool TryParseSegmentAngleLockCommand(AppCommandState& st, const std::string& lineIn, std::vector<std::string>& log) {
  using SAP = AppCommandState::SegmentAnglePickPhase;
  const std::string s = StringUtil::trimCopy(lineIn);
  if (s.empty())
    return false;
  const std::string low = StringUtil::toLowerAsciiCopy(s);
  if (low == "2p" || low == "2 p" || low == "ap" || low == "anglepick" || low == "a p") {
    ResetSegmentAngleLock(st);
    st.segmentAngleKeyboardAwaitBearing = false;
    st.segmentAnglePickPhase = SAP::WaitP1;
    log.push_back("Bearing pick — first reference point (viewport click). ESC cancels pick.");
    return true;
  }
  if (low == "a" || low == "angle") {
    if (st.segmentAngleLockActive || st.segmentAnglePickPhase != SAP::Idle) {
      ResetSegmentAngleLock(st);
      log.push_back("Segment bearing lock — off.");
    } else {
      st.segmentAngleKeyboardAwaitBearing = true;
      log.push_back("Bearing ° clockwise from north (decimal/DMS); blank Enter cancels.");
    }
    return true;
  }
  std::string rest;
  if (low.rfind("angle ", 0) == 0)
    rest = StringUtil::trimCopy(s.substr(6));
  else if (low.rfind("a ", 0) == 0)
    rest = StringUtil::trimCopy(s.substr(2));
  else if (low.rfind("angle", 0) == 0 && low.size() > 5)
    rest = StringUtil::trimCopy(s.substr(5));
  else if (low.rfind("a", 0) == 0 && low.size() > 1)
    rest = StringUtil::trimCopy(s.substr(1));
  else
    return false;

  if (rest.empty()) {
    if (st.segmentAngleLockActive || st.segmentAnglePickPhase != SAP::Idle) {
      ResetSegmentAngleLock(st);
      log.push_back("Segment bearing lock — off.");
    } else {
      st.segmentAngleKeyboardAwaitBearing = true;
      log.push_back("Bearing ° clockwise from north (decimal/DMS); blank Enter cancels.");
    }
    return true;
  }
  float combined = 0.f;
  if (!ParseBearingCwNorthStringWithOptionalDelta(rest, &combined, log))
    return true;
  const float theta = MathAngleRadFromBearingCwNorthDeg(combined);
  st.segmentLockUx = std::cos(theta);
  st.segmentLockUy = std::sin(theta);
  st.segmentAngleLockActive = true;
  st.segmentAnglePickPhase = SAP::Idle;
  char buf[144];
  std::snprintf(buf, sizeof(buf),
                "Bearing lock %.6g° clockwise from north — distance (+/- along ray) or click (A clears).",
                static_cast<double>(combined));
  log.push_back(buf);
  return true;
}

// Direct-distance entry (REQ-047): the ORTHO axis unit vector from the draft anchor toward the crosshair.
// `uiCursorWorld*` is published in WORLD coordinates while `anchor*` is LOCAL storage, so the crosshair is
// converted to local first — passing the two frames straight to OrthoUnitTowardPoint added the document
// origin to dx alone and pinned every typed distance to +X (see OrthoConstrain.hpp).
bool OrthoUnitTowardUiCursorFromAnchor(const AppCommandState& st, float* ux, float* uy) {
  float cursorLocalX = 0.f;
  float cursorLocalY = 0.f;
  CadCoord::LocalFromWorld(st, static_cast<double>(st.uiCursorWorldX), static_cast<double>(st.uiCursorWorldY),
                           &cursorLocalX, &cursorLocalY);
  return OrthoUnitTowardPoint(st.anchorX, st.anchorY, cursorLocalX, cursorLocalY, ux, uy);
}

bool ParseSingleFloatToken(const std::string& raw, float* out) {
  std::istringstream iss(raw);
  iss >> std::ws;
  if (!(iss >> *out))
    return false;
  iss >> std::ws;
  return iss.eof();
}

void StartLineCommand(AppCommandState& st, std::vector<std::string>& log) {
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  ResetSegmentAngleLock(st);
  st.selectedSurveyPointIndices.clear();
  st.selBoxWaitingSecond = false;
  st.active = AppCommandState::Kind::Line;
  st.lastCommand = AppCommandState::Kind::Line;
  st.linePhase = AppCommandState::LinePhase::NeedFirstPoint;
  st.lineDraftSegments = 0;
  log.push_back("LINE — specify first point (click or type X,Y / X Y). ESC to cancel.");
}

void StartCircleCommand(AppCommandState& st, std::vector<std::string>& log) {
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  st.selectedSurveyPointIndices.clear();
  st.selBoxWaitingSecond = false;
  st.active = AppCommandState::Kind::Circle;
  st.lastCommand = AppCommandState::Kind::Circle;
  log.push_back(
      "CIRCLE — center + radius: click/type center, then radius (click edge), type radius, or D + diameter.");
  log.push_back("Or type 3P first for a three-point circle. ESC to cancel.");
}

void StartPolylineCommand(AppCommandState& st, std::vector<std::string>& log) {
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  ResetSegmentAngleLock(st);
  st.selectedSurveyPointIndices.clear();
  st.selBoxWaitingSecond = false;
  st.active = AppCommandState::Kind::Polyline;
  st.polylinePhase = AppCommandState::PolylinePhase::NeedFirstPoint;
  log.push_back("POLYLINE — like LINE (A / 2P bearing lock); CLOSE/CL; ortho; ESC cancels.");
}

void StartFeatureLineCommand(AppCommandState& st, const std::string& name, std::vector<std::string>& log) {
  // REQ-087 / TASK-082. Drawn like LINE: a click gives X and Y, then the command PROMPTS for the
  // elevation. That is the difference from POLYLINE and 3DPOLY, which take Z from the work plane
  // without asking — right for tracing linework, wrong for a grading design, where every elevation
  // is a decision. Typing X,Y,Z still commits in one go, since it already answers the prompt.
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);  // clears the feature-line draft, including any pending point
  ResetSegmentAngleLock(st);
  st.selectedSurveyPointIndices.clear();
  st.selBoxWaitingSecond = false;
  st.active = AppCommandState::Kind::FeatureLine;
  st.featureLineDraftName = name;
  st.polylineTypedZValid = false;   // shared with the 3DPOLY peel below
  st.polylineTypedZRelative = false;
  log.push_back("FEATURELINE" + (name.empty() ? std::string() : " \"" + name + "\"") +
                " — click a point (you will be asked for its elevation), or type X,Y / X,Y,Z. "
                "E marks the next point an elevation point. CLOSE/CL, END, ESC cancels.");
}

/// Appends one vertex to the feature-line draft. \p isElevPoint marks it an elevation point rather
/// than a PI (ADR-035 (a)) — geometrically it lies on the line either way.
bool SubmitFeatureLineVertex(AppCommandState& st, float x, float y, bool isElevPoint,
                             std::vector<std::string>& log) {
  if (st.active != AppCommandState::Kind::FeatureLine)
    return false;

  float vz = CadCommitElevation(st);
  if (st.polylineTypedZValid) {
    vz = st.polylineTypedZRelative ? st.anchorZ + st.polylineTypedZ : st.polylineTypedZ;
    st.polylineTypedZValid = false;
    st.polylineTypedZRelative = false;
  }
  st.featureLineDraftVerts.push_back(x);
  st.featureLineDraftVerts.push_back(y);
  st.featureLineDraftVerts.push_back(vz);
  st.featureLineDraftElevPt.push_back(isElevPoint ? 1u : 0u);
  st.anchorX = x;
  st.anchorY = y;
  st.anchorZ = vz;
  char buf[128];
  std::snprintf(buf, sizeof(buf), "FEATURELINE %s %zu at elevation %.3f.",
                isElevPoint ? "elevation point" : "PI",
                st.featureLineDraftElevPt.size(), static_cast<double>(vz));
  log.push_back(buf);
  return true;
}

/// TASK-082: take an X,Y that arrived WITHOUT an elevation — a viewport click, or a typed `X,Y` —
/// and hold it while the command prompts for one.
///
/// This is the difference between FEATURELINE and POLYLINE/3DPOLY. Those take Z from the work plane
/// silently, which is right for linework being traced; a feature line is a grading design, where
/// every elevation is a decision and the user asked to be asked.
bool SubmitFeatureLinePoint(AppCommandState& st, float x, float y, std::vector<std::string>& log) {
  if (st.active != AppCommandState::Kind::FeatureLine)
    return false;

  // ASSUMPTION-1, in order. The snap override is CadCommitElevation's existing REQ-058 rule: an
  // object snap returns the object's real 3D point, and offering anything else as the default would
  // make snapping to a 3D object silently ignore the object. Otherwise the previous vertex, because
  // grading a line means most points sit near the last one.
  float defZ = CadWorkPlaneElevation(st);
  if (st.viewportSnapPickValid)
    defZ = st.viewportSnapPickLocalZ;
  else if (st.featureLineDraftVerts.size() >= 3)
    defZ = st.featureLineDraftVerts[st.featureLineDraftVerts.size() - 1];

  st.featureLinePendingPoint = true;
  st.featureLinePendingX = x;
  st.featureLinePendingY = y;
  st.featureLinePendingDefaultZ = defZ;

  char buf[192];
  std::snprintf(buf, sizeof(buf), "FEATURELINE — elevation for %s %zu <%.3f>:",
                st.featureLineNextIsElevPoint ? "elevation point" : "point",
                st.featureLineDraftElevPt.size() + 1, static_cast<double>(defZ));
  log.push_back(buf);
  return true;
}

/// TASK-082: resolve the pending point at \p z and add it to the draft.
void CommitFeatureLinePendingPoint(AppCommandState& st, float z, std::vector<std::string>& log) {
  if (!st.featureLinePendingPoint)
    return;
  const float x = st.featureLinePendingX;
  const float y = st.featureLinePendingY;
  const bool isElevPt = st.featureLineNextIsElevPoint;
  st.featureLinePendingPoint = false;
  st.featureLineNextIsElevPoint = false;
  // Route through SubmitFeatureLineVertex rather than pushing here, so the draft is appended in
  // exactly one place: typed X,Y,Z and click-then-elevation must not be able to disagree about what
  // a vertex is.
  st.polylineTypedZ = z;
  st.polylineTypedZRelative = false;
  st.polylineTypedZValid = true;
  SubmitFeatureLineVertex(st, x, y, isElevPt, log);
}

void CommitFeatureLineDraft(AppCommandState& st, bool closed, std::vector<std::string>& log) {
  const size_t nvert = st.featureLineDraftVerts.size() / 3;
  if (closed ? nvert < 3 : nvert < 2) {
    log.push_back(std::string("FEATURELINE — need at least ") + (closed ? "three" : "two") +
                  " vertices.");
    return;
  }
  PushUndoSnapshot(st, closed ? "Feature line (closed)" : "Feature line");
  if (st.featureLineOffsets.empty())
    st.featureLineOffsets.push_back(0);
  const int baseVert = st.featureLineOffsets.back();
  st.featureLineVerts.insert(st.featureLineVerts.end(), st.featureLineDraftVerts.begin(),
                             st.featureLineDraftVerts.end());
  st.featureLineElevPt.insert(st.featureLineElevPt.end(), st.featureLineDraftElevPt.begin(),
                              st.featureLineDraftElevPt.end());
  st.featureLineOffsets.push_back(baseVert + static_cast<int>(nvert));
  st.featureLineClosed.push_back(static_cast<uint8_t>(closed ? 1 : 0));
  CadFeatureLineInfo info;
  info.name = st.featureLineDraftName;
  st.featureLineInfo.push_back(std::move(info));
  st.featureLineAttrs.push_back(MakeNewEntityAttrs(st));
  BumpCadGpuCache(st);
  st.active = AppCommandState::Kind::None;
  st.featureLineDraftVerts.clear();
  st.featureLineDraftElevPt.clear();
  st.featureLineDraftName.clear();
  log.push_back(std::string("FEATURELINE ") + (closed ? "closed" : "complete") + " — " +
                std::to_string(nvert) + " vertices.");
}

// --- REQ-088 — feature line elevation editing ---------------------------------------------------
//
// ADR-035 (e): the elevation editor is a VIEW, not a store. Station, length, grade back and grade
// ahead are all derived here on demand; nothing below adds a field to any store, and `.gs` is
// untouched. The edits write elevations back into the existing stride-3 vertex array.

namespace {

/// Two points closer than this in plan are the same point. Well inside REQ-101's ±0.01 ft, so a
/// grade over a run this short is reported as undefined rather than as a huge number.
constexpr double kFeatureLinePlanEps = 1e-4;

/// Vertex range [*v0, *v1) of feature line \p fi, with the vertex array proven long enough to
/// address all of it — so every caller below can index without re-checking.
bool FeatureLineRange(const AppCommandState& st, int fi, int* v0, int* v1) {
  if (fi < 0 || static_cast<size_t>(fi + 1) >= st.featureLineOffsets.size())
    return false;
  const int a = st.featureLineOffsets[static_cast<size_t>(fi)];
  const int b = st.featureLineOffsets[static_cast<size_t>(fi + 1)];
  if (b - a < 2 || a < 0 || static_cast<size_t>(b) * 3 > st.featureLineVerts.size())
    return false;
  *v0 = a;
  *v1 = b;
  return true;
}

} // namespace

bool BuildFeatureLineElevTable(const AppCommandState& st, int fi, std::vector<FeatureLineElevRow>* out) {
  if (!out)
    return false;
  out->clear();
  int v0 = 0, v1 = 0;
  if (!FeatureLineRange(st, fi, &v0, &v1))
    return false;
  const int n = v1 - v0;
  const bool closed = static_cast<size_t>(fi) < st.featureLineClosed.size() &&
                      st.featureLineClosed[static_cast<size_t>(fi)] != 0;

  const auto px = [&](int i) { return static_cast<double>(st.featureLineVerts[static_cast<size_t>(v0 + i) * 3]); };
  const auto py = [&](int i) { return static_cast<double>(st.featureLineVerts[static_cast<size_t>(v0 + i) * 3 + 1]); };
  const auto pz = [&](int i) { return st.featureLineVerts[static_cast<size_t>(v0 + i) * 3 + 2]; };
  // PLAN length, not slope length — REQ-088 says stations and lengths agree with the feature line's
  // plan geometry, and grade is rise over the horizontal run (as SURFELEV computes it).
  const auto planLen = [&](int a, int b) { return std::hypot(px(b) - px(a), py(b) - py(a)); };

  const double kNaN = std::numeric_limits<double>::quiet_NaN();
  double station = 0.0;
  for (int i = 0; i < n; ++i) {
    // A closed line's last point continues to the first, and its first point is preceded by the
    // last; an open line's ends simply have no such segment.
    const int next = (i + 1 < n) ? i + 1 : (closed ? 0 : -1);
    const int prev = (i > 0) ? i - 1 : (closed ? n - 1 : -1);

    FeatureLineElevRow r;
    r.vertexIndex = i;
    r.isElevationPoint = static_cast<size_t>(v0 + i) < st.featureLineElevPt.size() &&
                         st.featureLineElevPt[static_cast<size_t>(v0 + i)] != 0;
    r.station = station;
    r.elevation = pz(i);
    r.lengthAhead = next >= 0 ? planLen(i, next) : 0.0;
    r.gradeAheadPct = (next >= 0 && r.lengthAhead > kFeatureLinePlanEps)
                          ? (static_cast<double>(pz(next)) - pz(i)) / r.lengthAhead * 100.0
                          : kNaN;
    const double lenBack = prev >= 0 ? planLen(prev, i) : 0.0;
    r.gradeBackPct = (prev >= 0 && lenBack > kFeatureLinePlanEps)
                         ? (static_cast<double>(pz(i)) - pz(prev)) / lenBack * 100.0
                         : kNaN;
    out->push_back(r);
    station += r.lengthAhead;
  }
  return true;
}

namespace {

/// Writable Z of point \p i of feature line \p fi. Callers have already validated the range.
float& FeatureLineZ(AppCommandState& st, int v0, int i) {
  return st.featureLineVerts[static_cast<size_t>(v0 + i) * 3 + 2];
}

/// Everything every elevation edit has to do around the mutation itself, in one place: validate the
/// feature line and the point index, snapshot for undo, then bump the revision.
///
/// The bump is what makes REQ-088's last acceptance condition — "the surface rebuilds with no user
/// action" — true without a line of new surface code: TickSurfaceRebuilds' dirty check is exactly
/// "cadGpuRevision moved", so a feature line used as a breakline (REQ-069) re-triangulates on the
/// next frame. Doing it in one place is also what makes "undoable in one step" uniform across all
/// six edits rather than six chances to forget the snapshot.
///
/// **\p fn must call `commit()` immediately before its first mutation and not before.** The snapshot
/// is deferred this way because several of these edits can only discover they must refuse after
/// computing the table — and PushUndoSnapshot CLEARS THE REDO STACK. Pushing eagerly and popping on
/// refusal would therefore destroy the user's redo history as the price of a rejected keystroke,
/// which is a worse bug than the one it tidies.
template <class Fn>
bool EditFeatureLineElevations(AppCommandState& st, int flNumber, int pointNumber, bool needPoint,
                               const char* undoLabel, std::vector<std::string>& log, Fn&& fn) {
  const int fi = flNumber - 1;  // commands are 1-based; the store is 0-based
  int v0 = 0, v1 = 0;
  if (!FeatureLineRange(st, fi, &v0, &v1)) {
    log.push_back("FLELEV — no feature line " + std::to_string(flNumber) + ".");
    return false;
  }
  const int n = v1 - v0;
  if (needPoint && (pointNumber < 1 || pointNumber > n)) {
    log.push_back("FLELEV — feature line " + std::to_string(flNumber) + " has " + std::to_string(n) +
                  " points; there is no point " + std::to_string(pointNumber) + ".");
    return false;
  }
  bool pushed = false;
  const auto commit = [&] {
    if (!pushed) {
      PushUndoSnapshot(st, undoLabel);
      pushed = true;
    }
  };
  if (!fn(fi, v0, n, commit))
    return false;
  BumpCadGpuCache(st);
  return true;
}

} // namespace

/// REQ-088: set one point's elevation outright.
bool SetFeatureLinePointElevation(AppCommandState& st, int flNumber, int pointNumber, float elevation,
                                  std::vector<std::string>& log) {
  return EditFeatureLineElevations(
      st, flNumber, pointNumber, true, "Feature line elevation", log,
      [&](int /*fi*/, int v0, int /*n*/, const auto& commit) {
        commit();
        FeatureLineZ(st, v0, pointNumber - 1) = elevation;
        char buf[160];
        std::snprintf(buf, sizeof(buf), "FLELEV — feature line %d point %d elevation %.3f.",
                      flNumber, pointNumber, static_cast<double>(elevation));
        log.push_back(buf);
        return true;
      });
}

/// REQ-088: set the grade of the segment AHEAD of \p pointNumber, which moves the NEXT point.
///
/// ASSUMPTION-1: a grade edit holds the earlier point and moves the later one. The Acceptance pins
/// this half — "typing a grade ahead moves the next point's elevation and leaves the current one
/// alone" — and GRADEBACK below is the same rule seen from the other end.
bool SetFeatureLineGradeAhead(AppCommandState& st, int flNumber, int pointNumber, double gradePct,
                              std::vector<std::string>& log) {
  return EditFeatureLineElevations(
      st, flNumber, pointNumber, true, "Feature line grade ahead", log,
      [&](int fi, int v0, int n, const auto& commit) {
        std::vector<FeatureLineElevRow> rows;
        if (!BuildFeatureLineElevTable(st, fi, &rows))
          return false;
        const int i = pointNumber - 1;
        const bool closed = static_cast<size_t>(fi) < st.featureLineClosed.size() &&
                            st.featureLineClosed[static_cast<size_t>(fi)] != 0;
        const int next = (i + 1 < n) ? i + 1 : (closed ? 0 : -1);
        if (next < 0) {
          log.push_back("FLELEV — point " + std::to_string(pointNumber) +
                        " is the last point of an open feature line; it has no grade ahead.");
          return false;
        }
        if (rows[static_cast<size_t>(i)].lengthAhead <= kFeatureLinePlanEps) {
          log.push_back("FLELEV — the segment ahead of point " + std::to_string(pointNumber) +
                        " has no plan length, so a grade cannot set an elevation across it.");
          return false;
        }
        commit();
        FeatureLineZ(st, v0, next) =
            static_cast<float>(static_cast<double>(FeatureLineZ(st, v0, i)) +
                               gradePct / 100.0 * rows[static_cast<size_t>(i)].lengthAhead);
        char buf[200];
        std::snprintf(buf, sizeof(buf),
                      "FLELEV — feature line %d grade ahead of point %d = %.2f%%; point %d is now %.3f.",
                      flNumber, pointNumber, gradePct, next + 1,
                      static_cast<double>(FeatureLineZ(st, v0, next)));
        log.push_back(buf);
        return true;
      });
}

/// REQ-088: set the grade of the segment BEHIND \p pointNumber, which moves THIS point and holds the
/// previous one — ASSUMPTION-1, and what "updates the downstream elevations" means read directionally.
bool SetFeatureLineGradeBack(AppCommandState& st, int flNumber, int pointNumber, double gradePct,
                             std::vector<std::string>& log) {
  return EditFeatureLineElevations(
      st, flNumber, pointNumber, true, "Feature line grade back", log,
      [&](int fi, int v0, int n, const auto& commit) {
        std::vector<FeatureLineElevRow> rows;
        if (!BuildFeatureLineElevTable(st, fi, &rows))
          return false;
        const int i = pointNumber - 1;
        const bool closed = static_cast<size_t>(fi) < st.featureLineClosed.size() &&
                            st.featureLineClosed[static_cast<size_t>(fi)] != 0;
        const int prev = (i > 0) ? i - 1 : (closed ? n - 1 : -1);
        if (prev < 0) {
          log.push_back("FLELEV — point " + std::to_string(pointNumber) +
                        " is the first point of an open feature line; it has no grade back.");
          return false;
        }
        const double lenBack = rows[static_cast<size_t>(prev)].lengthAhead;
        if (lenBack <= kFeatureLinePlanEps) {
          log.push_back("FLELEV — the segment behind point " + std::to_string(pointNumber) +
                        " has no plan length, so a grade cannot set an elevation across it.");
          return false;
        }
        commit();
        FeatureLineZ(st, v0, i) = static_cast<float>(
            static_cast<double>(FeatureLineZ(st, v0, prev)) + gradePct / 100.0 * lenBack);
        char buf[200];
        std::snprintf(buf, sizeof(buf),
                      "FLELEV — feature line %d grade back of point %d = %.2f%%; point %d is now %.3f.",
                      flNumber, pointNumber, gradePct, pointNumber,
                      static_cast<double>(FeatureLineZ(st, v0, i)));
        log.push_back(buf);
        return true;
      });
}

/// REQ-088: "Points may be raised or lowered as a set by a delta." A negative delta lowers.
bool RaiseFeatureLineElevations(AppCommandState& st, int flNumber, float delta,
                                std::vector<std::string>& log) {
  return EditFeatureLineElevations(
      st, flNumber, 0, false, "Feature line raise/lower", log, [&](int /*fi*/, int v0, int n, const auto& commit) {
        commit();
        for (int i = 0; i < n; ++i)
          FeatureLineZ(st, v0, i) += delta;
        char buf[160];
        std::snprintf(buf, sizeof(buf), "FLELEV — feature line %d: all %d point(s) %s %.3f.",
                      flNumber, n, delta < 0.f ? "lowered by" : "raised by",
                      std::fabs(static_cast<double>(delta)));
        log.push_back(buf);
        return true;
      });
}

/// REQ-088: insert an elevation point at plan station \p station.
///
/// The plan position is INTERPOLATED along the segment the station falls in, so the new vertex lies
/// exactly on the line by construction — which is why ADR-035 (b)'s drift risk does not arise here.
/// It carries the elevation-point flag, so it is not a PI: it changes the surface without changing
/// the plan shape, which is REQ-088's fourth acceptance condition.
bool InsertFeatureLineElevationPoint(AppCommandState& st, int flNumber, double station, float elevation,
                                     std::vector<std::string>& log) {
  return EditFeatureLineElevations(
      st, flNumber, 0, false, "Insert elevation point", log, [&](int fi, int v0, int n, const auto& commit) {
        std::vector<FeatureLineElevRow> rows;
        if (!BuildFeatureLineElevTable(st, fi, &rows))
          return false;
        if (station <= kFeatureLinePlanEps) {
          log.push_back("FLELEV — station must be past the start of the feature line.");
          return false;
        }
        // Find the segment holding the station. The last row's lengthAhead is 0 on an open line, so
        // a station past the end matches nothing and is reported rather than clamped.
        int seg = -1;
        double along = 0.0;
        for (int i = 0; i + 1 < n; ++i) {
          const double s0 = rows[static_cast<size_t>(i)].station;
          const double len = rows[static_cast<size_t>(i)].lengthAhead;
          if (station < s0 + len - kFeatureLinePlanEps) {
            seg = i;
            along = station - s0;
            break;
          }
        }
        if (seg < 0 || along <= kFeatureLinePlanEps) {
          const double total = rows.back().station;
          char buf[200];
          std::snprintf(buf, sizeof(buf),
                        "FLELEV — station %.3f is not inside a segment (the feature line runs 0.000 "
                        "to %.3f, and a station AT an existing point would add nothing).",
                        station, total);
          log.push_back(buf);
          return false;
        }

        const size_t a = static_cast<size_t>(v0 + seg) * 3;
        const size_t b = static_cast<size_t>(v0 + seg + 1) * 3;
        const double t = along / rows[static_cast<size_t>(seg)].lengthAhead;
        const float nx = static_cast<float>(st.featureLineVerts[a] +
                                            t * (st.featureLineVerts[b] - st.featureLineVerts[a]));
        const float ny = static_cast<float>(st.featureLineVerts[a + 1] +
                                            t * (st.featureLineVerts[b + 1] - st.featureLineVerts[a + 1]));

        // Insert AFTER point `seg`, i.e. at vertex slot v0+seg+1. The three arrays are cut on
        // different strides — verts by 3, flags by 1 — which EraseFeatureLineByIndex already showed
        // is silent when it goes wrong, so both are done here together.
        const int at = v0 + seg + 1;
        commit();
        const float v[3] = {nx, ny, elevation};
        st.featureLineVerts.insert(st.featureLineVerts.begin() + static_cast<std::ptrdiff_t>(at) * 3,
                                   v, v + 3);
        if (st.featureLineElevPt.size() < static_cast<size_t>(at))
          st.featureLineElevPt.resize(static_cast<size_t>(at), 0);
        st.featureLineElevPt.insert(st.featureLineElevPt.begin() + static_cast<std::ptrdiff_t>(at),
                                    static_cast<uint8_t>(1));
        // Every feature line after this one starts a vertex later.
        for (size_t k = static_cast<size_t>(fi) + 1; k < st.featureLineOffsets.size(); ++k)
          st.featureLineOffsets[k] += 1;

        char buf[200];
        std::snprintf(buf, sizeof(buf),
                      "FLELEV — feature line %d: elevation point added at station %.3f, elevation "
                      "%.3f (now point %d of %d).",
                      flNumber, station, static_cast<double>(elevation), seg + 2, n + 1);
        log.push_back(buf);
        return true;
      });
}

/// REQ-088: delete an elevation point. Refuses a PI — removing one is geometry editing, which is
/// REQ-087's "insert and delete a PI" and is not built. Refusing out loud beats silently deleting
/// a vertex the user thought was only carrying an elevation.
bool DeleteFeatureLineElevationPoint(AppCommandState& st, int flNumber, int pointNumber,
                                     std::vector<std::string>& log) {
  return EditFeatureLineElevations(
      st, flNumber, pointNumber, true, "Delete elevation point", log,
      [&](int fi, int v0, int /*n*/, const auto& commit) {
        const int at = v0 + pointNumber - 1;
        const bool isElevPt = static_cast<size_t>(at) < st.featureLineElevPt.size() &&
                              st.featureLineElevPt[static_cast<size_t>(at)] != 0;
        if (!isElevPt) {
          log.push_back("FLELEV — point " + std::to_string(pointNumber) +
                        " is a PI, not an elevation point. Deleting a PI changes the plan shape and "
                        "is not an elevation edit.");
          return false;
        }
        commit();
        st.featureLineVerts.erase(
            st.featureLineVerts.begin() + static_cast<std::ptrdiff_t>(at) * 3,
            st.featureLineVerts.begin() + static_cast<std::ptrdiff_t>(at + 1) * 3);
        st.featureLineElevPt.erase(st.featureLineElevPt.begin() + static_cast<std::ptrdiff_t>(at));
        for (size_t k = static_cast<size_t>(fi) + 1; k < st.featureLineOffsets.size(); ++k)
          st.featureLineOffsets[k] -= 1;
        log.push_back("FLELEV — feature line " + std::to_string(flNumber) + ": elevation point " +
                      std::to_string(pointNumber) + " deleted.");
        return true;
      });
}

void StartPolyline3dCommand(AppCommandState& st, std::vector<std::string>& log) {
  // REQ-085. Same draft as POLYLINE — the store is already stride-3 XYZ — with per-vertex elevation
  // entry switched on. ResetAllCadDraftTools inside StartPolylineCommand clears the flag, so it is
  // set afterwards, not before.
  StartPolylineCommand(st, log);
  log.pop_back();  // replace POLYLINE's prompt rather than printing both
  st.polylineDraft3d = true;
  log.push_back("3DPOLY — vertices carry their own elevation: type X,Y,Z (or @dx,dy,dz), or snap. "
                "X,Y alone uses the snapped point's elevation, else ELEV. CLOSE/CL, END, ESC cancels.");
}

void StartArcCommand(AppCommandState& st, std::vector<std::string>& log) {
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  st.selectedSurveyPointIndices.clear();
  st.selBoxWaitingSecond = false;
  st.active = AppCommandState::Kind::Arc;
  st.arcPhase = AppCommandState::ArcPhase::WaitStart;
  log.push_back("ARC — three picks: start, point on arc, end. ESC cancels.");
}

void StartEllipseCommand(AppCommandState& st, std::vector<std::string>& log) {
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  st.selectedSurveyPointIndices.clear();
  st.selBoxWaitingSecond = false;
  st.active = AppCommandState::Kind::Ellipse;
  st.ellPhase = AppCommandState::EllipsePhase::WaitCenter;
  log.push_back("ELLIPSE — center, major axis endpoint, then minor/major ratio (0-1] on command line (Enter=0.5).");
}

void StartRectCommand(AppCommandState& st, std::vector<std::string>& log) {
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  st.selectedSurveyPointIndices.clear();
  st.selBoxWaitingSecond = false;
  st.active = AppCommandState::Kind::Rect;
  st.lastCommand = AppCommandState::Kind::Rect;
  st.rectPhase = AppCommandState::RectPhase::WaitFirstCorner;
  log.push_back("RECT — pick the first corner (or type X,Y):");
}

// REQ-053: a rectangle is a 4-vertex CLOSED polyline, matching AutoCAD's RECTANG (which produces an
// LWPOLYLINE). Storing it that way means DXF/DWG write, grips, snaps — including the geometric center —
// and the offset/trim/join paths all work on it with no new entity type.
void CommitRectangle(AppCommandState& st, float x1, float y1, float x2, float y2,
                     std::vector<std::string>& log) {
  const float mnX = std::min(x1, x2);
  const float mxX = std::max(x1, x2);
  const float mnY = std::min(y1, y2);
  const float mxY = std::max(y1, y2);
  if (mxX - mnX < 1.e-9f || mxY - mnY < 1.e-9f) {
    log.push_back("RECT — corners give a zero-width or zero-height rectangle; pick again.");
    st.rectPhase = AppCommandState::RectPhase::WaitFirstCorner;
    return;
  }

  const float xs[4] = {mnX, mxX, mxX, mnX};
  const float ys[4] = {mnY, mnY, mxY, mxY};

  PushUndoSnapshot(st, "Rectangle");
  if (PaperLayout* L = ActivePaperGeometryTarget(st)) {
    // Paper-space RECT (REQ-037): the corners are paper inches; commit to the layout's paper store.
    const int baseVert = L->paperPolyOffsets.empty() ? 0 : L->paperPolyOffsets.back();
    for (int i = 0; i < 4; ++i) {
      L->paperPolyVerts.push_back(xs[i]);
      L->paperPolyVerts.push_back(ys[i]);
      L->paperPolyVerts.push_back(0.f);
    }
    if (L->paperPolyOffsets.empty())
      L->paperPolyOffsets.push_back(baseVert);
    L->paperPolyOffsets.push_back(baseVert + 4);
    L->paperPolyClosed.push_back(1u);
    L->paperPolyAttrs.push_back(MakeNewEntityAttrs(st));
  } else {
    if (st.userPolylineOffsets.empty())
      st.userPolylineOffsets.push_back(0);
    const int baseVert = st.userPolylineOffsets.back();
    for (int i = 0; i < 4; ++i) {
      st.userPolylineVerts.push_back(xs[i]);
      st.userPolylineVerts.push_back(ys[i]);
      // Lands on the active work plane (REQ-058) — see SubmitLineVertex.
      st.userPolylineVerts.push_back(CadCommitElevation(st));
    }
    st.userPolylineOffsets.push_back(baseVert + 4);
    st.userPolylineClosed.push_back(1u);
    st.userPolylineAttrs.push_back(MakeNewEntityAttrs(st));
  }

  char msg[128];
  std::snprintf(msg, sizeof(msg), "RECT — %.6g x %.6g rectangle created.",
                static_cast<double>(mxX - mnX), static_cast<double>(mxY - mnY));
  log.push_back(msg);

  st.active = AppCommandState::Kind::None;
  ResetRectDraft(st);
  EnsureAttrCounts(st);
  BumpCadGpuCache(st);
}

void StartTextCommand(AppCommandState& st, std::vector<std::string>& log) {
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  st.selectedSurveyPointIndices.clear();
  st.selBoxWaitingSecond = false;
  st.active = AppCommandState::Kind::Text;
  st.textPhase = AppCommandState::TextCmdPhase::WaitInsertion;
  log.push_back(
      "TEXT — pick insertion, then height / rotation / string on command line (defaults from plot scale). ESC "
      "cancels.");
}

void StartMtextCommand(AppCommandState& st, std::vector<std::string>& log) {
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  st.selectedSurveyPointIndices.clear();
  st.selBoxWaitingSecond = false;
  st.active = AppCommandState::Kind::Mtext;
  st.mtextPhase = AppCommandState::MtextPhase::WaitCorner1;
  log.push_back("MTEXT — two corners for box, then type in the on-screen editor (Ctrl+Enter reformats; Save to place). ESC cancels.");
}

void OpenMtextRichEditorForPlacement(AppCommandState& st, std::vector<std::string>* log) {
  CloseMtextRichEditorUi(st);
  st.mtextRichEditorPlacement = true;
  st.mtextRichEditorAnnIndex = -1;
  st.mtextRichEditorBuf.clear();
  st.mtextRichEditorOpen = true;
  st.mtextRichEditorFocusRequest = true;
  if (log)
    log->push_back("MTEXT — type in the box; Ctrl+Enter reformats; Save to place; Esc to cancel.");
}

void OpenMtextRichEditorForAnnotation(AppCommandState& st, int annIndex, std::vector<std::string>* log) {
  if (annIndex < 0 || static_cast<size_t>(annIndex) >= st.cadAnnotations.size())
    return;
  CadAnnotation& a = st.cadAnnotations[static_cast<size_t>(annIndex)];
  // REQ-039 phase 2: double-click edits MTEXT (rich) or single-line TEXT (plain). Other kinds are not text.
  if (a.kind != CadAnnotation::Kind::Mtext && a.kind != CadAnnotation::Kind::Text)
    return;
  CloseMtextRichEditorUi(st);
  st.mtextRichEditorPlacement = false;
  st.mtextRichEditorPaper = false;
  st.mtextRichEditorPaperLayout = -1;
  st.mtextRichEditorPlain = (a.kind == CadAnnotation::Kind::Text);
  st.mtextRichEditorAnnIndex = annIndex;
  st.mtextRichEditorBuf = a.text;
  st.mtextRichEditorOpen = true;
  st.mtextRichEditorFocusRequest = true;
  if (log)
    log->push_back(st.mtextRichEditorPlain
                       ? "TEXT — edit the contents; Enter or Save to update; Esc to cancel."
                       : "MTEXT — edit in the box; Ctrl+Enter reformats; Save to update; Esc to cancel.");
}

// In-place editor for a native paper-space text (REQ-039 phase 2): the SAME editor as model text, retargeted
// to the active layout's paperTexts store. Plain box for Kind::Text, rich box for Kind::Mtext.
void OpenPaperTextEditor(AppCommandState& st, int layoutIndex, int textIndex, std::vector<std::string>* log) {
  if (layoutIndex < 0 || static_cast<size_t>(layoutIndex) >= st.paperLayouts.size())
    return;
  PaperLayout& L = st.paperLayouts[static_cast<size_t>(layoutIndex)];
  if (textIndex < 0 || static_cast<size_t>(textIndex) >= L.paperTexts.size())
    return;
  CadAnnotation& a = L.paperTexts[static_cast<size_t>(textIndex)];
  CloseMtextRichEditorUi(st);
  st.mtextRichEditorPlacement = false;
  st.mtextRichEditorPaper = true;
  st.mtextRichEditorPaperLayout = layoutIndex;
  st.mtextRichEditorPlain = (a.kind == CadAnnotation::Kind::Text);
  st.mtextRichEditorAnnIndex = textIndex;
  st.mtextRichEditorBuf = a.text;
  st.mtextRichEditorOpen = true;
  st.mtextRichEditorFocusRequest = true;
  if (log)
    log->push_back(st.mtextRichEditorPlain
                       ? "Paper text — edit the contents; Enter or Save to update; Esc to cancel."
                       : "Paper MTEXT — edit in the box; Ctrl+Enter reformats; Save to update; Esc to cancel.");
}

void CommitMtextRichEditor(AppCommandState& st, std::vector<std::string>& log) {
  if (!st.mtextRichEditorOpen)
    return;
  using K = AppCommandState::Kind;
  if (st.mtextRichEditorPlacement) {
    if (st.active != K::Mtext || st.mtextPhase != AppCommandState::MtextPhase::WaitString) {
      CloseMtextRichEditorUi(st);
      return;
    }
    const std::string normalized = MtextRichNormalize(st.mtextRichEditorBuf);
    if (!StringUtil::trimCopy(MtextRichFlattenToPlain(normalized)).empty()) {
      PushUndoSnapshot(st, "MTEXT");
      CadAnnotation ann;
      ann.kind = CadAnnotation::Kind::Mtext;
      ann.boxMinX = std::min(st.mtxtX1, st.mtxtX2);
      ann.boxMinY = std::min(st.mtxtY1, st.mtxtY2);
      ann.boxMaxX = std::max(st.mtxtX1, st.mtxtX2);
      ann.boxMaxY = std::max(st.mtxtY1, st.mtxtY2);
      ann.insX = ann.boxMinX;
      ann.insY = ann.boxMinY;
      ann.plottedHeightInches = st.defaultPlottedTextHeightInches;
      ann.text = normalized;
      ann.insZ = CadCommitElevation(st);  // lands on the active work plane (REQ-058)
      StampActiveTextStyleOnNewText(st, ann);  // REQ-044: new MTEXT adopts the active text style
      st.cadAnnotations.push_back(std::move(ann));
      st.cadAnnotationAttrs.push_back(MakeNewEntityAttrs(st));
      BumpCadGpuCache(st);
      log.push_back("MTEXT placed.");
    } else
      log.push_back("MTEXT — empty; canceled.");
    st.active = K::None;
    ResetMtextDraft(st);
    CloseMtextRichEditorUi(st);
    return;
  }
  // Edit an existing text object (model cadAnnotations or paper paperTexts; MTEXT rich or TEXT plain).
  const bool paper = st.mtextRichEditorPaper;
  const bool plain = st.mtextRichEditorPlain;
  if (MtextRichEditorTargetAnnotation(st)) {
    PushUndoSnapshot(st, paper ? "Paper text edit" : (plain ? "TEXT edit" : "MTEXT edit"));
    // Re-resolve after the snapshot (it does not mutate the live stores, but keep the access pattern safe).
    CadAnnotation* ann = MtextRichEditorTargetAnnotation(st);
    if (ann) {
      if (plain) {
        // Single-line TEXT: store contents verbatim, collapsing any stray newlines (the box is single-line,
        // but paste can introduce them). No MTEXT normalization — that would inject [[…]] tags into TEXT.
        std::string t = st.mtextRichEditorBuf;
        for (char& c : t)
          if (c == '\n' || c == '\r')
            c = ' ';
        ann->text = std::move(t);
      } else {
        ann->text = MtextRichNormalize(st.mtextRichEditorBuf);
        const int linkedPi = paper ? -1 : SurveyPointIndexForId(st, ann->surveyPointLabelForId);
        if (linkedPi >= 0)
          RepositionSurveyLabelMtextForPoint(st, static_cast<size_t>(linkedPi));
      }
      BumpCadGpuCache(st);
      log.push_back(paper ? "Paper text updated." : (plain ? "TEXT updated." : "MTEXT updated."));
    }
  }
  CloseMtextRichEditorUi(st);
}

void CancelMtextRichEditor(AppCommandState& st, std::vector<std::string>* log) {
  if (!st.mtextRichEditorOpen)
    return;
  if (st.mtextRichEditorPlacement) {
    if (log)
      log->push_back("MTEXT — canceled.");
    st.active = AppCommandState::Kind::None;
    ResetMtextDraft(st);
    CloseMtextRichEditorUi(st);
    return;
  }
  CloseMtextRichEditorUi(st);
}

void StartDimAlignedCommand(AppCommandState& st, std::vector<std::string>& log) {
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  st.selectedSurveyPointIndices.clear();
  st.selBoxWaitingSecond = false;
  st.active = AppCommandState::Kind::DimAligned;
  st.lastCommand = AppCommandState::Kind::DimAligned;
  st.dimPhase = AppCommandState::DimPhase::WaitExt1;
  log.push_back("DIMALIGNED — extension 1, extension 2, then offset (point away from measured line). ESC cancels.");
}

void StartDimLinearCommand(AppCommandState& st, std::vector<std::string>& log) {
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  st.selectedSurveyPointIndices.clear();
  st.selBoxWaitingSecond = false;
  st.active = AppCommandState::Kind::DimLinear;
  st.lastCommand = AppCommandState::Kind::DimLinear;
  st.dimPhase = AppCommandState::DimPhase::WaitExt1;
  log.push_back("DIMLINEAR — ortho distance in X or Y between extension points; third pick sets dimension line; "
                "cursor or H/V chooses horizontal vs vertical. ESC cancels.");
}

void StartDimAngularCommand(AppCommandState& st, std::vector<std::string>& log) {
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  st.selectedSurveyPointIndices.clear();
  st.selBoxWaitingSecond = false;
  st.active = AppCommandState::Kind::DimAngular;
  st.lastCommand = AppCommandState::Kind::DimAngular;
  st.dimAngularPhase = AppCommandState::DimAngularPhase::WaitVertex;
  log.push_back("DIMANGULAR — vertex, two ray points, then arc position (radius). Text is degrees/minutes/seconds. ESC "
                "cancels.");
}

void StartIdPointCommand(AppCommandState& st, std::vector<std::string>& log) {
  using K = AppCommandState::Kind;
  if (st.active != K::None) {
    log.push_back("ID — finish or cancel the active command first.");
    return;
  }
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  st.selectedSurveyPointIndices.clear();
  st.selBoxWaitingSecond = false;
  st.active = K::IdPoint;
  log.push_back("ID — specify point (click in drawing or type X,Y). UCS = World. ESC cancels.");
}

void StartSurfaceElevGradeCommand(AppCommandState& st, std::vector<std::string>& log) {
  using K = AppCommandState::Kind;
  using SEP = AppCommandState::SurfaceElevPhase;
  if (st.active != K::None) {
    log.push_back("SURFELEV — finish or cancel the active command first.");
    return;
  }
  // Said before the first pick rather than after it: a user who has not built a surface yet should
  // not have to click to find out there is nothing to read (REQ-201).
  if (st.cadSurfaces.empty()) {
    log.push_back("SURFELEV — there are no surfaces in the drawing. Build one from a point group first.");
    return;
  }
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  st.selectedSurveyPointIndices.clear();
  st.selBoxWaitingSecond = false;
  st.surfaceElevPhase = SEP::WaitFirst;
  st.surfaceElevFromZ.clear();
  st.active = K::SurfaceElevGrade;
  log.push_back("SURFELEV — pick a point for its surface elevation; pick a second for grade. ESC cancels.");
}

namespace {
/// The stable id (REQ-076) of a picked Line or Polyline, or 0 for anything else / an out-of-range
/// index — 0 is never a real id (\ref EntityAttributes::id), so it doubles as "not applicable" here.
std::uint64_t EntityIdOfLineOrPolylinePick(const AppCommandState& st, const SelectedEntity& hit) {
  const auto idOf = [](const std::vector<EntityAttributes>& v, int i) -> std::uint64_t {
    return (i >= 0 && static_cast<size_t>(i) < v.size()) ? v[static_cast<size_t>(i)].id : 0;
  };
  switch (hit.type) {
  case SelectedEntity::Type::LineSeg:     return idOf(st.userLineAttrs, hit.index);
  case SelectedEntity::Type::Polyline:    return idOf(st.userPolylineAttrs, hit.index);
  case SelectedEntity::Type::FeatureLine: return idOf(st.featureLineAttrs, hit.index);  // REQ-087
  default:                                return 0;
  }
}
} // namespace

void StartDesignateBreaklineCommand(AppCommandState& st, const std::string& surfaceName,
                                    std::vector<std::string>& log) {
  using K = AppCommandState::Kind;
  if (st.active != K::None) {
    log.push_back("DESIGNATEBREAKLINE — finish or cancel the active command first.");
    return;
  }
  if (FindSurfaceIndex(st, surfaceName) < 0) {
    log.push_back("DESIGNATEBREAKLINE — no surface named \"" + surfaceName + "\".");
    return;
  }
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  st.selBoxWaitingSecond = false;
  st.designateSurfaceName = surfaceName;
  st.active = K::DesignateBreakline;
  log.push_back("DESIGNATEBREAKLINE — pick a line or polyline to use as a breakline on \"" + surfaceName +
               "\". ESC cancels.");
}

void StartDesignateBoundaryCommand(AppCommandState& st, const std::string& surfaceName, CadBoundaryKind kind,
                                   std::vector<std::string>& log) {
  using K = AppCommandState::Kind;
  if (st.active != K::None) {
    log.push_back("DESIGNATEBOUNDARY — finish or cancel the active command first.");
    return;
  }
  if (FindSurfaceIndex(st, surfaceName) < 0) {
    log.push_back("DESIGNATEBOUNDARY — no surface named \"" + surfaceName + "\".");
    return;
  }
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  st.selBoxWaitingSecond = false;
  st.designateSurfaceName = surfaceName;
  st.designateBoundaryKind = kind;
  st.active = K::DesignateBoundary;
  const char* kindName = kind == CadBoundaryKind::Outer ? "outer" : kind == CadBoundaryKind::Hide ? "hide" : "show";
  log.push_back(std::string("DESIGNATEBOUNDARY — pick a CLOSED polyline to use as a ") + kindName +
               " boundary on \"" + surfaceName + "\". ESC cancels.");
}

/// Shared commit for both DESIGNATEBREAKLINE and DESIGNATEBOUNDARY: pick, validate, append to the
/// target surface's definition, and trigger the dynamic rebuild (REQ-069) — a plain BumpCadGpuCache
/// is enough, since TickSurfaceRebuilds' dirty check is exactly "cadGpuRevision moved."
void CommitDesignateAt(AppCommandState& st, float wx, float wy, bool isBoundary, std::vector<std::string>& log) {
  using K = AppCommandState::Kind;
  const std::string cmdName = isBoundary ? "DESIGNATEBOUNDARY" : "DESIGNATEBREAKLINE";
  const int si = FindSurfaceIndex(st, st.designateSurfaceName);
  if (si < 0) {
    log.push_back(cmdName + " — surface \"" + st.designateSurfaceName + "\" no longer exists.");
    st.active = K::None;
    return;
  }
  SelectedEntity hit{};
  float d2 = 0.f;
  if (!PickClosestCadEntity(st, wx, wy, CadOffsetEntityPickTolWorld(st), &hit, &d2)) {
    log.push_back(cmdName + " — nothing under cursor; try again, or ESC to cancel.");
    return;  // stays active — try again, matching OFFSET's WaitSelectEntity miss behaviour
  }
  // REQ-087: a feature line is design linework whose whole purpose is to be a breakline, so it is
  // accepted here alongside lines and polylines.
  if (hit.type != SelectedEntity::Type::LineSeg && hit.type != SelectedEntity::Type::Polyline &&
      hit.type != SelectedEntity::Type::FeatureLine) {
    log.push_back(cmdName + " — that is not a line, polyline, or feature line; try again, or ESC to cancel.");
    return;
  }
  if (isBoundary && hit.type == SelectedEntity::Type::LineSeg) {
    log.push_back(cmdName + " — a boundary must be a closed polyline or feature line, not a line; "
                            "try again, or ESC to cancel.");
    return;
  }
  if (isBoundary) {
    const size_t ix = static_cast<size_t>(hit.index);
    const bool closed = hit.type == SelectedEntity::Type::FeatureLine
                            ? (ix < st.featureLineClosed.size() && st.featureLineClosed[ix] != 0)
                            : (ix < st.userPolylineClosed.size() && st.userPolylineClosed[ix] != 0);
    if (!closed) {
      log.push_back(cmdName + " — that " +
                    (hit.type == SelectedEntity::Type::FeatureLine ? "feature line" : "polyline") +
                    " is not closed; try again, or ESC to cancel.");
      return;
    }
  }
  const std::uint64_t id = EntityIdOfLineOrPolylinePick(st, hit);
  if (id == 0) {
    log.push_back(cmdName + " — could not identify that entity; try again, or ESC to cancel.");
    return;
  }

  CadSurface& surface = st.cadSurfaces[static_cast<size_t>(si)];
  if (isBoundary) {
    CadSurfaceBoundary b;
    b.entityId = id;
    b.kind = st.designateBoundaryKind;
    b.name = st.designateBoundaryName;  // REQ-075; empty when the command was typed, not dialogued
    surface.boundaries.push_back(b);
    const char* kindName = b.kind == CadBoundaryKind::Outer ? "outer"
                          : b.kind == CadBoundaryKind::Hide  ? "hide"
                                                              : "show";
    log.push_back("DESIGNATEBOUNDARY — added a " + std::string(kindName) + " boundary" +
                  (b.name.empty() ? "" : " \"" + b.name + "\"") + " to \"" + surface.name + "\".");
  } else {
    CadSurfaceBreakline bl;
    bl.entityId = id;
    bl.description = st.designateBreaklineDescription;
    surface.breaklines.push_back(bl);
    log.push_back("DESIGNATEBREAKLINE — added a breakline" +
                  (bl.description.empty() ? "" : " \"" + bl.description + "\"") + " to \"" + surface.name + "\".");
  }
  // Consumed: a later typed DESIGNATE* must not inherit the last dialog's text.
  st.designateBreaklineDescription.clear();
  st.designateBoundaryName.clear();
  BumpCadGpuCache(st);  // marks every surface's dirty check; TickSurfaceRebuilds picks this one up next frame
  st.active = K::None;
}

void StartSurveyInverseCommand(AppCommandState& st, std::vector<std::string>& log) {
  using K = AppCommandState::Kind;
  using SIP = AppCommandState::SurveyInversePhase;
  if (st.active != K::None) {
    log.push_back("INVERSE — finish or cancel the active command first.");
    return;
  }
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  st.selectedSurveyPointIndices.clear();
  st.selBoxWaitingSecond = false;
  st.active = K::SurveyInverse;
  st.surveyInversePhase = SIP::WaitFrom;
  log.push_back(
      "INVERSE — first point (World X=Easting, Y=Northing); then second. "
      "Result: ΔE, ΔN, horizontal distance, bearing D°M'S\" and decimal ° clockwise from north. ESC cancels.");
}

void ClearCadSelection(AppCommandState& st) {
  st.selection.clear();
  st.selBoxWaitingSecond = false;
  AbortMtextGripInteraction(st);
  ClearDimGripInteraction(st);
}

void EnsureAttrCounts(AppCommandState& st) {
  bool grew = false;
  const size_t nl = st.userLinesFlat.size() / 6;
  while (st.userLineAttrs.size() < nl) {
    st.userLineAttrs.push_back(MakeNewEntityAttrs(st));
    grew = true;
  }
  const size_t nc = st.userCirclesCxCyZR.size() / 4;
  while (st.userCircleAttrs.size() < nc) {
    st.userCircleAttrs.push_back(MakeNewEntityAttrs(st));
    grew = true;
  }
  while (st.userArcAttrs.size() < st.userArcs.size()) {
    st.userArcAttrs.push_back(MakeNewEntityAttrs(st));
    grew = true;
  }
  while (st.userEllAttrs.size() < st.userEllipses.size()) {
    st.userEllAttrs.push_back(MakeNewEntityAttrs(st));
    grew = true;
  }
  const size_t np = st.userPolylineOffsets.size() > 0 ? st.userPolylineOffsets.size() - 1 : 0;
  while (st.userPolylineAttrs.size() < np) {
    st.userPolylineAttrs.push_back(MakeNewEntityAttrs(st));
    grew = true;
  }
  const size_t na = st.cadAnnotations.size();
  while (st.cadAnnotationAttrs.size() < na) {
    st.cadAnnotationAttrs.push_back(MakeNewEntityAttrs(st));
    grew = true;
  }
  while (st.cadSurfaceAttrs.size() < st.cadSurfaces.size()) {  // REQ-068
    st.cadSurfaceAttrs.push_back(MakeNewEntityAttrs(st));
    grew = true;
  }
  if (grew)
    BumpCadGpuCache(st);
}

static void CollectLayersUsedInDrawing(const AppCommandState& st, std::set<std::string>* out) {
  out->insert("0");
  auto add = [&](const std::string& s) {
    if (!s.empty())
      out->insert(s);
  };
  for (const auto& a : st.userLineAttrs)
    add(a.layer);
  for (const auto& a : st.userCircleAttrs)
    add(a.layer);
  for (const auto& a : st.userArcAttrs)
    add(a.layer);
  for (const auto& a : st.userEllAttrs)
    add(a.layer);
  for (const auto& a : st.userPolylineAttrs)
    add(a.layer);
  for (const auto& a : st.cadAnnotationAttrs)
    add(a.layer);
  for (const auto& p : st.surveyPoints)
    add(p.layer);
  add(st.currentLayer);
}

void SyncDrawingLayerTableWithGeometry(AppCommandState& st) {
  if (st.drawingLayerTable.empty()) {
    CadLayerRow z;
    z.name = "0";
    st.drawingLayerTable.push_back(z);
  }
  std::set<std::string> have;
  for (const auto& r : st.drawingLayerTable)
    have.insert(r.name);
  std::set<std::string> used;
  CollectLayersUsedInDrawing(st, &used);
  for (const std::string& name : used) {
    if (!have.count(name)) {
      CadLayerRow row;
      row.name = name;
      st.drawingLayerTable.push_back(row);
      have.insert(name);
    }
  }
  if (st.currentLayer.empty())
    st.currentLayer = "0";
  if (!have.count(st.currentLayer))
    st.currentLayer = "0";
  // Text styles (REQ-044): guarantee the baseline "Standard" style and a valid active style.
  TextStyles::EnsureStandard(st.textStyles);
  if (st.activeTextStyleName.empty() || !TextStyles::Find(st.textStyles, st.activeTextStyleName))
    st.activeTextStyleName = TextStyles::kStandardName;
  // Surface styles (REQ-070): the same guarantee, and it is what a legacy `.gs` relies on — a file
  // written before this table existed has no styles section at all, and every surface in it carries
  // an empty styleName that must resolve to something drawable (ADR-036 (d)). Left to the loader
  // alone it would hold for `.gs` and not for DXF import or a fresh document, so it lives in the one
  // normalisation pass every route to a drawing already goes through.
  SurfaceStyles::EnsureStandard(st.surfaceStyles);
}

const TextStyle* ActiveTextStyle(const AppCommandState& st) {
  const std::string name = st.activeTextStyleName.empty() ? std::string(TextStyles::kStandardName)
                                                          : st.activeTextStyleName;
  if (const TextStyle* s = TextStyles::Find(st.textStyles, name)) return s;
  return TextStyles::Find(st.textStyles, TextStyles::kStandardName);
}

void SetActiveTextStyle(AppCommandState& st, const std::string& name) {
  st.activeTextStyleName = name;
  if (const TextStyle* s = ActiveTextStyle(st))
    st.defaultPlottedTextHeightInches = std::max(s->heightInches, 1.e-6f);
}

// Stamp the active text style onto a freshly built user TEXT/MTEXT (REQ-044 create path). Font/oblique/
// bold/italic come from the style; height is left as the caller set it (it already equals the style height
// via the SetActiveTextStyle → defaultPlottedTextHeightInches sync), and any height typed at the TEXT
// prompt is preserved. Per-property overrides land in a later phase.
static void StampActiveTextStyleOnNewText(AppCommandState& st, CadAnnotation& a) {
  const TextStyle* s = ActiveTextStyle(st);
  if (!s) return;
  a.styleName = s->name;
  a.ovFont = a.ovHeight = a.ovOblique = a.ovBold = a.ovItalic = false;
  a.fontFamily = s->fontFamily;
  a.obliqueDeg = s->obliqueDeg;
  a.bold = s->bold;
  a.italic = s->italic;
}

static bool ValidNewLayerNameChars(const std::string& n) {
  if (n.empty() || n.size() > 255)
    return false;
  for (unsigned char c : n) {
    if (c < 32 || c == 127)
      return false;
    if (c == '<' || c == '>' || c == '/' || c == '\\' || c == '"' || c == ':' || c == ';' || c == '?' ||
        c == '*' || c == '|' || c == ',' || c == '`')
      return false;
  }
  return true;
}

static bool LayerNameExistsCi(const AppCommandState& st, const std::string& name) {
  const std::string nl = StringUtil::toLowerAsciiCopy(name);
  for (const auto& r : st.drawingLayerTable) {
    if (StringUtil::toLowerAsciiCopy(r.name) == nl)
      return true;
  }
  return false;
}

bool CadAddDrawingLayer(AppCommandState& st, const std::string& raw, std::string* err) {
  const std::string name = StringUtil::trimCopy(raw);
  if (!ValidNewLayerNameChars(name)) {
    if (err)
      *err = "Invalid layer name (empty, too long, or reserved characters).";
    return false;
  }
  SyncDrawingLayerTableWithGeometry(st);
  if (LayerNameExistsCi(st, name)) {
    if (err)
      *err = "Layer already exists.";
    return false;
  }
  PushUndoSnapshot(st, "Add layer");
  CadLayerRow row;
  row.name = name;
  st.drawingLayerTable.push_back(row);
  return true;
}

namespace {

// EXTRACT (REQ-071) — bake a surface's currently displayed contours into ordinary polylines.
//
// **The contours are regenerated here, not read back from the display cache**, and that is what makes
// REQ-071's first acceptance condition ("at exactly the displayed contour elevations") structural
// rather than a claim. `GenerateContours` is a pure function of (verts, indices, levels); this passes
// the same triangulation pointer and the same resolved style the display pass resolved, through the
// same `ResolveSurfaceContourLevels`, so the output is byte-identical to what is on screen. The cache
// holds the FLATTENED GL_LINES form, which has thrown away the per-contour offsets and the closed
// flag a polyline needs — so reading it back would be both harder and weaker.
//
// **The result is deliberately unlinked from the surface** (REQ-071's statement, not an oversight):
// no back-reference is stored in either direction, so a later rebuild does not touch these polylines
// and erasing the surface does not remove them. There is simply nothing joining them.

/// Append one `ContourResult` as polylines carrying \p attrs. Returns how many were created.
int AppendContoursAsPolylines(AppCommandState& st, const ContourResult& r, const EntityAttributes& attrs) {
  int made = 0;
  for (int c = 0; c < r.contourCount(); ++c) {
    const int begin = r.offsets[static_cast<size_t>(c)];
    const int end = r.offsets[static_cast<size_t>(c) + 1];
    if (end - begin < 2)
      continue;  // a one-vertex contour is a point, not a polyline — see contourgen on the peak case
    if (st.userPolylineOffsets.empty())
      st.userPolylineOffsets.push_back(0);
    const int baseVert = st.userPolylineOffsets.back();
    for (int v = begin; v < end; ++v) {
      st.userPolylineVerts.push_back(r.vertsXyz[static_cast<size_t>(v) * 3 + 0]);
      st.userPolylineVerts.push_back(r.vertsXyz[static_cast<size_t>(v) * 3 + 1]);
      st.userPolylineVerts.push_back(r.vertsXyz[static_cast<size_t>(v) * 3 + 2]);
    }
    st.userPolylineOffsets.push_back(baseVert + (end - begin));
    // Straight through from the generator: a contour that closed in the surface's interior becomes a
    // closed polyline, and one that ran off the border stays open. Re-deriving it from the vertices
    // would mean comparing floats for the seam, which is what contourgen avoids in the first place.
    st.userPolylineClosed.push_back(r.closed[static_cast<size_t>(c)] ? 1u : 0u);
    st.userPolylineAttrs.push_back(attrs);
    ++made;
  }
  return made;
}

void ExecuteExtractCommand(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  SurfaceStyles::EnsureStandard(st.surfaceStyles);

  const std::vector<std::string> f = SplitCommaFields(StringUtil::trimCopy(args));
  std::string surfaceName = f.empty() ? std::string() : f[0];
  const std::string layerArg = f.size() > 1 ? f[1] : std::string();

  if (f.size() > 2) {
    log.push_back("EXTRACT — usage: EXTRACT <surface>[, <layer>]");
    return;
  }

  // A drawing with exactly one surface does not need to be told which one. With more than one, the
  // command names them rather than guessing — picking silently is how the wrong surface gets baked.
  if (surfaceName.empty()) {
    if (st.cadSurfaces.size() == 1) {
      surfaceName = st.cadSurfaces[0].name;
    } else if (st.cadSurfaces.empty()) {
      log.push_back("EXTRACT — the drawing has no surfaces.");
      return;
    } else {
      std::string names;
      for (const CadSurface& s : st.cadSurfaces)
        names += (names.empty() ? "" : ", ") + s.name;
      log.push_back("EXTRACT — usage: EXTRACT <surface>[, <layer>]. Surfaces: " + names + ".");
      return;
    }
  }

  const int si = FindSurfaceIndex(st, surfaceName);
  if (si < 0) {
    log.push_back("EXTRACT — no surface named \"" + surfaceName + "\".");
    return;
  }
  const CadSurface& surf = st.cadSurfaces[static_cast<size_t>(si)];
  if (!surf.tin || surf.tin->indices.empty()) {
    log.push_back("EXTRACT — \"" + surf.name + "\" has never been built; nothing to extract.");
    return;
  }

  const SurfaceStyle* stylePtr = SurfaceStyles::Resolve(st.surfaceStyles, surf.styleName);
  const SurfaceStyle style = stylePtr ? *stylePtr : SurfaceStyles::StandardSurfaceStyle();

  // REQ-071's last acceptance condition. Refused BEFORE anything is created, and the message says
  // which style switched them off — "nothing happened" with no reason is the exact silence REQ-201
  // exists to prevent.
  if (!style.minorContour.visible && !style.majorContour.visible) {
    log.push_back("EXTRACT — \"" + surf.name + "\" has both contour components switched off in style \"" +
                  style.name + "\"; nothing extracted. Turn contours on in the Surface Style editor first.");
    return;
  }

  const SurfaceContourLevels levels = ResolveSurfaceContourLevels(*surf.tin, style);
  if (levels.suppressed) {
    // ASSUMPTION-2: a style whose interval is too fine to DISPLAY is displaying no contours, so
    // extracting some would be exactly the "silently extracting a hidden interval" the requirement
    // forbids — and it is the runaway the display cap exists to prevent.
    log.push_back("EXTRACT — \"" + surf.name + "\" style \"" + style.name + "\" asks for " +
                  std::to_string(levels.levelsAsked) +
                  " contour levels, more than are drawn, so none are displayed and none were "
                  "extracted. Use a larger contour interval.");
    return;
  }
  if (levels.empty()) {
    log.push_back("EXTRACT — \"" + surf.name + "\" displays no contours at style \"" + style.name +
                  "\"'s intervals over its " + SurfaceStyles::FormatFt(levels.minZ) + " to " +
                  SurfaceStyles::FormatFt(levels.maxZ) + " ft range; nothing extracted.");
    return;
  }

  // Resolve the target layer BEFORE the snapshot, so an invalid name refuses without leaving an undo
  // step that did nothing.
  std::string layer = st.currentLayer.empty() ? std::string("0") : st.currentLayer;
  bool layerCreated = false;
  if (!layerArg.empty()) {
    if (const CadLayerRow* row = FindDrawingLayerRowCi(st, layerArg)) {
      layer = row->name;  // the existing spelling, so a case variant does not become a second layer
    } else {
      if (!ValidNewLayerNameChars(StringUtil::trimCopy(layerArg))) {
        log.push_back("EXTRACT — \"" + layerArg +
                      "\" is not a valid layer name (empty, too long, or reserved characters).");
        return;
      }
      layer = StringUtil::trimCopy(layerArg);
      layerCreated = true;
    }
  }

  // ONE snapshot for the whole command, so EXTRACT is a single undo step even when it also created
  // the layer. CadAddDrawingLayer is deliberately not used: it pushes a snapshot of its own, which
  // would make undoing an extraction take two presses.
  PushUndoSnapshot(st, "Extract contours");
  if (layerCreated) {
    CadLayerRow row;
    row.name = layer;
    st.drawingLayerTable.push_back(row);
  }

  const auto attrsFor = [&](const SurfaceComponentStyle& comp) {
    EntityAttributes a;
    a.layer = layer;
    // Q2: the extracted set looks like what was on screen, so a major contour stays heavier than a
    // minor one once it is ordinary geometry. A component's "ByLayer" now resolves against the
    // POLYLINE's layer, which is correct — it is an ordinary entity from here on.
    a.color = comp.color;
    a.linetype = comp.linetype;
    a.lineweightMm = comp.lineweightMm;
    a.transparency = -1.f;
    return a;  // id stays 0; the next EnsureEntityIds sweep assigns it, like any created entity
  };

  ContourResult r;
  int minorMade = 0, majorMade = 0;
  if (!levels.minor.empty()) {
    GenerateContours(surf.tin->vertsXyz, surf.tin->indices, levels.minor, &r);
    minorMade = AppendContoursAsPolylines(st, r, attrsFor(style.minorContour));
  }
  if (!levels.major.empty()) {
    GenerateContours(surf.tin->vertsXyz, surf.tin->indices, levels.major, &r);
    majorMade = AppendContoursAsPolylines(st, r, attrsFor(style.majorContour));
  }
  BumpCadGpuCache(st);

  // Levels existed but every contour at them was degenerate — a level sitting exactly on a peak is a
  // single point, not a polyline (see contourgen). Reported as the nothing it is, rather than as
  // "0 polyline(s)" wedged into a sentence that reads as a success.
  if (minorMade + majorMade == 0) {
    log.push_back("EXTRACT — \"" + surf.name + "\" produced no contour polylines at style \"" +
                  style.name + "\"'s intervals; nothing was created.");
    return;
  }

  // REQ-201, and Q3: BOTH intervals, each with its own count. Reporting one interval after
  // extracting two sets would be true and misleading at the same time.
  std::string msg = "EXTRACT — \"" + surf.name + "\": ";
  if (minorMade > 0)
    msg += std::to_string(minorMade) + " minor contour(s) at " +
           SurfaceStyles::FormatFt(style.minorIntervalFt) + " ft";
  if (minorMade > 0 && majorMade > 0)
    msg += ", ";
  if (majorMade > 0)
    msg += std::to_string(majorMade) + " major contour(s) at " +
           SurfaceStyles::FormatFt(style.majorIntervalFt) + " ft";
  msg += " — " + std::to_string(minorMade + majorMade) + " polyline(s) on layer \"" + layer + "\"" +
         (layerCreated ? " (created)" : "") + ".";
  log.push_back(msg);

  // Q4: a half-enabled style extracts the half that is displayed and SAYS which half it skipped,
  // rather than refusing work the user can plainly see on screen.
  if (!style.minorContour.visible)
    log.push_back("EXTRACT — minor contours are switched off in style \"" + style.name +
                  "\" and were not extracted.");
  if (!style.majorContour.visible)
    log.push_back("EXTRACT — major contours are switched off in style \"" + style.name +
                  "\" and were not extracted.");
  log.push_back("EXTRACT — the new polylines are ordinary geometry and are not linked to the "
                "surface: rebuilding or erasing it leaves them unchanged.");
}

} // namespace

bool CadRenameDrawingLayer(AppCommandState& st, const std::string& oldNameRaw, const std::string& newNameRaw,
                           std::string* err) {
  const std::string oldN = StringUtil::trimCopy(oldNameRaw);
  const std::string newN = StringUtil::trimCopy(newNameRaw);
  if (oldN == "0") {
    if (err)
      *err = "Layer 0 cannot be renamed.";
    return false;
  }
  if (!ValidNewLayerNameChars(newN)) {
    if (err)
      *err = "Invalid new layer name.";
    return false;
  }
  if (newN == oldN)
    return true;
  if (LayerNameExistsCi(st, newN) && StringUtil::toLowerAsciiCopy(newN) != StringUtil::toLowerAsciiCopy(oldN)) {
    if (err)
      *err = "A layer with that name already exists.";
    return false;
  }
  auto it = std::find_if(st.drawingLayerTable.begin(), st.drawingLayerTable.end(),
                         [&](const CadLayerRow& r) { return r.name == oldN; });
  if (it == st.drawingLayerTable.end()) {
    if (err)
      *err = "Layer not found in table.";
    return false;
  }
  PushUndoSnapshot(st, "Rename layer");
  auto reassign = [&](std::string& L) {
    if (L == oldN)
      L = newN;
  };
  for (auto& a : st.userLineAttrs)
    reassign(a.layer);
  for (auto& a : st.userCircleAttrs)
    reassign(a.layer);
  for (auto& a : st.userArcAttrs)
    reassign(a.layer);
  for (auto& a : st.userEllAttrs)
    reassign(a.layer);
  for (auto& a : st.userPolylineAttrs)
    reassign(a.layer);
  for (auto& a : st.cadAnnotationAttrs)
    reassign(a.layer);
  for (auto& p : st.surveyPoints)
    reassign(p.layer);
  if (st.currentLayer == oldN)
    st.currentLayer = newN;
  it->name = newN;
  BumpCadGpuCache(st);
  return true;
}

bool CadDeleteDrawingLayer(AppCommandState& st, const std::string& nameRaw, std::string* err) {
  const std::string name = StringUtil::trimCopy(nameRaw);
  if (name == "0") {
    if (err)
      *err = "Layer 0 cannot be deleted.";
    return false;
  }
  const auto itRow = std::find_if(st.drawingLayerTable.begin(), st.drawingLayerTable.end(),
                                  [&](const CadLayerRow& r) { return r.name == name; });
  if (itRow == st.drawingLayerTable.end()) {
    if (err)
      *err = "Layer not found.";
    return false;
  }
  PushUndoSnapshot(st, "Delete layer");
  auto reassign = [&](std::string& L) {
    if (L == name)
      L = "0";
  };
  for (auto& a : st.userLineAttrs)
    reassign(a.layer);
  for (auto& a : st.userCircleAttrs)
    reassign(a.layer);
  for (auto& a : st.userArcAttrs)
    reassign(a.layer);
  for (auto& a : st.userEllAttrs)
    reassign(a.layer);
  for (auto& a : st.userPolylineAttrs)
    reassign(a.layer);
  for (auto& a : st.cadAnnotationAttrs)
    reassign(a.layer);
  for (auto& p : st.surveyPoints)
    reassign(p.layer);
  if (st.currentLayer == name)
    st.currentLayer = "0";
  st.drawingLayerTable.erase(itRow);
  BumpCadGpuCache(st);
  return true;
}

void ApplyEntityGripPoint(AppCommandState& st, float x, float y) {
  if (!st.entityGripMoveActive)
    return;
  const int idx = st.entityGripEntityIndex;
  if (idx < 0)
    return;
  switch (st.entityGripType) {
  case SelectedEntity::Type::LineSeg: {
    if (static_cast<size_t>(idx) * 6 + 5 >= st.userLinesFlat.size())
      return;
    const size_t k = static_cast<size_t>(idx) * 6;
    if (st.entityGripWhich == 0) {
      st.userLinesFlat[k] = x;
      st.userLinesFlat[k + 1] = y;
    } else if (st.entityGripWhich == 1) {
      st.userLinesFlat[k + 3] = x;
      st.userLinesFlat[k + 4] = y;
    }
    return;
  }
  case SelectedEntity::Type::Circle: {
    if (static_cast<size_t>(idx) * 4 + 3 >= st.userCirclesCxCyZR.size())
      return;
    const size_t k = static_cast<size_t>(idx) * 4;
    float& cx = st.userCirclesCxCyZR[k];
    float& cy = st.userCirclesCxCyZR[k + 1];
    float& r = st.userCirclesCxCyZR[k + 3];
    if (st.entityGripWhich == 0) {
      cx = x;
      cy = y;
    } else if (st.entityGripWhich == 1) {
      r = std::hypot(x - cx, y - cy);
    }
    return;
  }
  case SelectedEntity::Type::Polyline: {
    const int np = st.userPolylineOffsets.size() > 0 ? static_cast<int>(st.userPolylineOffsets.size() - 1) : 0;
    if (idx >= np)
      return;
    const int startV = st.userPolylineOffsets[static_cast<size_t>(idx)];
    const int globalV = startV + st.entityGripWhich;
    const size_t xIdx = static_cast<size_t>(globalV) * 3;
    if (xIdx + 1 >= st.userPolylineVerts.size())
      return;
    st.userPolylineVerts[xIdx] = x;
    st.userPolylineVerts[xIdx + 1] = y;
    return;
  }
  case SelectedEntity::Type::Arc: {
    if (static_cast<size_t>(idx) >= st.userArcs.size())
      return;
    CadArc& a = st.userArcs[static_cast<size_t>(idx)];
    if (st.entityGripWhich == 0) {
      a.cx = x;
      a.cy = y;
    } else if (st.entityGripWhich == 1) {
      a.r = std::hypot(x - a.cx, y - a.cy);
      a.startRad = std::atan2(y - a.cy, x - a.cx);
    } else if (st.entityGripWhich == 2) {
      a.r = std::hypot(x - a.cx, y - a.cy);
      a.sweepRad = std::atan2(y - a.cy, x - a.cx) - a.startRad;
    }
    return;
  }
  case SelectedEntity::Type::Ellipse: {
    if (static_cast<size_t>(idx) >= st.userEllipses.size())
      return;
    CadEllipse& el = st.userEllipses[static_cast<size_t>(idx)];
    if (st.entityGripWhich == 0) {
      el.cx = x;
      el.cy = y;
    } else if (st.entityGripWhich == 1) {
      el.majVx = x - el.cx;
      el.majVy = y - el.cy;
    } else if (st.entityGripWhich == 2) {
      const float majLen2 = el.majVx * el.majVx + el.majVy * el.majVy;
      if (majLen2 < 1e-12f)
        return;
      el.ratio = std::clamp(((x - el.cx) * -el.majVy + (y - el.cy) * el.majVx) / majLen2, 0.f, 1.f);
    }
    return;
  }
  default:
    return;
  }
}

// "Similar" = same object type AND same layer AND same colour, matching AutoCAD's SELECTSIMILAR with
// layer and colour in SELECTSIMILARMODE. Type alone swept up every line in the drawing, which is not a
// useful selection on a survey plan where layer *is* the classification.
namespace {

/// Layer names are compared case-insensitively and an empty name means layer "0" (the storage default),
/// so entities that differ only in how their layer was spelled still match.
bool SelectSimilarAttrsMatch(const EntityAttributes& a, const EntityAttributes& b) {
  const std::string la = StringUtil::toLowerAsciiCopy(a.layer.empty() ? std::string("0") : a.layer);
  const std::string lb = StringUtil::toLowerAsciiCopy(b.layer.empty() ? std::string("0") : b.layer);
  if (la != lb)
    return false;
  // Colour is a free-form string ("ByLayer", "Red", "#RRGGBB") — compare case-insensitively so
  // "ByLayer" and "BYLAYER" are the same colour, which is how the rest of the code treats them.
  const std::string ca = StringUtil::toLowerAsciiCopy(a.color.empty() ? std::string("ByLayer") : a.color);
  const std::string cb = StringUtil::toLowerAsciiCopy(b.color.empty() ? std::string("ByLayer") : b.color);
  return ca == cb;
}

/// Attributes of one entity, or defaults when the parallel attr array is short (older drawings).
EntityAttributes SelectSimilarAttrsOf(const AppCommandState& st, const SelectedEntity& e) {
  const auto pick = [](const std::vector<EntityAttributes>& v, int i) {
    return (i >= 0 && static_cast<size_t>(i) < v.size()) ? v[static_cast<size_t>(i)] : EntityAttributes{};
  };
  switch (e.type) {
  case SelectedEntity::Type::LineSeg:    return pick(st.userLineAttrs, e.index);
  case SelectedEntity::Type::Circle:     return pick(st.userCircleAttrs, e.index);
  case SelectedEntity::Type::Polyline:   return pick(st.userPolylineAttrs, e.index);
  case SelectedEntity::Type::Arc:        return pick(st.userArcAttrs, e.index);
  case SelectedEntity::Type::Ellipse:    return pick(st.userEllAttrs, e.index);
  case SelectedEntity::Type::Annotation: return pick(st.cadAnnotationAttrs, e.index);
  default:                               return EntityAttributes{};
  }
}

} // namespace

void SelectSimilarToCurrentSelection(AppCommandState& st, std::vector<std::string>* log) {
  AbortMtextGripInteraction(st);
  ClearDimGripInteraction(st);
  ClearEntityGripInteraction(st);

  if (!st.selection.empty()) {
    st.selectedSurveyPointIndices.clear();
    const SelectedEntity& lead = st.selection.front();
    const EntityAttributes want = SelectSimilarAttrsOf(st, lead);
    std::vector<SelectedEntity> next;

    // Candidate of the lead's type; keep it only when its layer and colour match the lead's.
    const auto consider = [&](SelectedEntity::Type type, int index) {
      SelectedEntity e{};
      e.type = type;
      e.index = index;
      if (SelectSimilarAttrsMatch(SelectSimilarAttrsOf(st, e), want))
        next.push_back(e);
    };

    switch (lead.type) {
    case SelectedEntity::Type::LineSeg: {
      const size_t n = st.userLinesFlat.size() / 6;
      for (size_t i = 0; i < n; ++i)
        consider(SelectedEntity::Type::LineSeg, static_cast<int>(i));
      break;
    }
    case SelectedEntity::Type::Circle: {
      const size_t n = st.userCirclesCxCyZR.size() / 4;
      for (size_t i = 0; i < n; ++i)
        consider(SelectedEntity::Type::Circle, static_cast<int>(i));
      break;
    }
    case SelectedEntity::Type::Polyline: {
      const int np =
          st.userPolylineOffsets.size() > 1 ? static_cast<int>(st.userPolylineOffsets.size()) - 1 : 0;
      for (int i = 0; i < np; ++i)
        consider(SelectedEntity::Type::Polyline, i);
      break;
    }
    case SelectedEntity::Type::Arc: {
      for (size_t i = 0; i < st.userArcs.size(); ++i)
        consider(SelectedEntity::Type::Arc, static_cast<int>(i));
      break;
    }
    case SelectedEntity::Type::Ellipse: {
      for (size_t i = 0; i < st.userEllipses.size(); ++i)
        consider(SelectedEntity::Type::Ellipse, static_cast<int>(i));
      break;
    }
    case SelectedEntity::Type::Annotation: {
      // Annotations narrow further by annotation kind: TEXT is not similar to a dimension even on
      // one layer in one colour.
      const int lix = lead.index;
      if (lix < 0 || static_cast<size_t>(lix) >= st.cadAnnotations.size())
        break;
      const CadAnnotation::Kind wantKind = st.cadAnnotations[static_cast<size_t>(lix)].kind;
      for (size_t ai = 0; ai < st.cadAnnotations.size(); ++ai) {
        if (st.cadAnnotations[ai].kind == wantKind)
          consider(SelectedEntity::Type::Annotation, static_cast<int>(ai));
      }
      break;
    }
    case SelectedEntity::Type::Surface: {
      // REQ-068 / ADR-036 (b). Skips invisible surfaces, so SELECTSIMILAR cannot select something
      // the user cannot see — the same rule the pick funnel applies (REQ-084 (d)).
      for (size_t i = 0; i < st.cadSurfaces.size(); ++i)
        if (SurfaceVisible(st, i))
          consider(SelectedEntity::Type::Surface, static_cast<int>(i));
      break;
    }
    default:
      break;
    }
    st.selection = std::move(next);
    EnsureAttrCounts(st);
    BumpCadGpuCache(st);
    if (log) {
      const std::string layerName = want.layer.empty() ? std::string("0") : want.layer;
      const std::string colorName = want.color.empty() ? std::string("ByLayer") : want.color;
      log->push_back("Select similar — " + std::to_string(st.selection.size()) +
                     " object(s) on layer " + layerName + ", colour " + colorName + ".");
    }
    return;
  }

  if (!st.selectedSurveyPointIndices.empty()) {
    ClearCadSelection(st);
    st.selectedSurveyPointIndices.clear();
    for (size_t i = 0; i < st.surveyPoints.size(); ++i)
      st.selectedSurveyPointIndices.push_back(static_cast<int>(i));
    for (int svi : st.selectedSurveyPointIndices) {
      if (svi >= 0 && static_cast<size_t>(svi) < st.surveyPoints.size())
        SyncSurveyPointLinkedMtextSelection(st, svi);
    }
    BumpCadGpuCache(st);
    if (log)
      log->push_back("Select similar — all " + std::to_string(st.surveyPoints.size()) + " survey point(s).");
  } else if (log)
    log->push_back("Select similar — nothing selected.");
}

void ClearCadGeometry(AppCommandState& st) {
  st.worldDocumentOriginX = 0.0;
  st.worldDocumentOriginY = 0.0;
  // A cleared drawing is a new drawing: its id space restarts. Ids are unique *within* a drawing
  // (REQ-076), not globally, and every entity that could have held one is about to be erased. A
  // `.gs` load overwrites this from the file immediately after clearing.
  st.nextEntityId = 1;
  st.entityIdSweepRevision = kEntityIdSweepNever;  // whatever is loaded next must be swept
  st.userLinesFlat.clear();
  st.userLineAttrs.clear();
  st.userCirclesCxCyZR.clear();
  st.userCircleAttrs.clear();
  st.userArcs.clear();
  st.userArcAttrs.clear();
  st.userEllipses.clear();
  st.userEllAttrs.clear();
  st.userPolylineVerts.clear();
  st.userPolylineOffsets.clear();
  st.userPolylineClosed.clear();
  st.userPolylineAttrs.clear();
  st.cadAnnotations.clear();
  st.cadAnnotationAttrs.clear();
  st.cadFilledRegions.clear();
  st.cadFilledRegionAttrs.clear();
  st.cadMeshes.clear();
  st.cadMeshAttrs.clear();
  ClearPendingOneShotObjectSnap(st);
  ClearCadSelection(st);
  // Isolation is keyed on entity ids, and the id space restarts above — so a hidden set kept here
  // would name whatever the next drawing happens to number the same way. This is also what makes
  // "opening a drawing always shows all of it" true (REQ-084 (d)), since every load clears first.
  st.hiddenEntityIds.clear();
  BumpCadGpuCache(st);
}

void ClearPendingOneShotObjectSnap(AppCommandState& st) {
  st.pendingOneShotSnapValid = false;
}

void ResetCadToolStateToIdle(AppCommandState& st) {
  ClearPendingOneShotObjectSnap(st);
  st.active = AppCommandState::Kind::None;
  st.linePhase = AppCommandState::LinePhase::NeedFirstPoint;
  ResetSegmentAngleLock(st);
  st.trimPhase = AppCommandState::TrimPhase::SelectCuttingEdges;
  st.trimCutters.clear();
  ResetAllCadDraftTools(st);
  ResetModifyRotateDraft(st);
  st.selBoxWaitingSecond = false;
  st.copySurveyDupModalOpen = false;
  st.copySurveyDupModalOpenRequested = false;
  AbortMtextGripInteraction(st);
}

void ClearSelection(AppCommandState& st) {
  ClearCadSelection(st);
  st.selectedSurveyPointIndices.clear();
}

void ApplySurveyPointClickSelection(AppCommandState& st, int surveyPointIndex, bool shiftModifier,
                                    std::vector<std::string>* log) {
  if (surveyPointIndex < 0 || static_cast<size_t>(surveyPointIndex) >= st.surveyPoints.size())
    return;
  auto& v = st.selectedSurveyPointIndices;
  const auto it = std::find(v.begin(), v.end(), surveyPointIndex);
  if (shiftModifier) {
    if (it != v.end()) {
      v.erase(it);
      if (log)
        log->push_back("Survey point removed from selection.");
    } else {
      v.push_back(surveyPointIndex);
      if (log)
        log->push_back("Survey point added to selection.");
    }
  } else if (it == v.end()) {
    v.push_back(surveyPointIndex);
  } else {
    v.clear();
    v.push_back(surveyPointIndex);
  }
}

static void ErasePolylineByIndex(AppCommandState& st, int pi) {
  if (pi < 0 || static_cast<size_t>(pi + 1) >= st.userPolylineOffsets.size())
    return;
  const int np = static_cast<int>(st.userPolylineOffsets.size()) - 1;
  std::vector<int> nvPer(static_cast<size_t>(np));
  for (int i = 0; i < np; ++i)
    nvPer[static_cast<size_t>(i)] = st.userPolylineOffsets[static_cast<size_t>(i + 1)] - st.userPolylineOffsets[static_cast<size_t>(i)];
  const int a = st.userPolylineOffsets[static_cast<size_t>(pi)];
  const int b = st.userPolylineOffsets[static_cast<size_t>(pi + 1)];
  st.userPolylineVerts.erase(st.userPolylineVerts.begin() + static_cast<std::ptrdiff_t>(3 * a),
                             st.userPolylineVerts.begin() + static_cast<std::ptrdiff_t>(3 * b));
  std::vector<int> newOff;
  newOff.reserve(static_cast<size_t>(std::max(0, np - 1) + 1));
  newOff.push_back(0);
  int run = 0;
  for (int i = 0; i < np; ++i) {
    if (i == pi)
      continue;
    run += nvPer[static_cast<size_t>(i)];
    newOff.push_back(run);
  }
  // Erasing the ONLY polyline leaves the seeded 0 with nothing appended after it, and `{0}` is not
  // how this store spells "no polylines" — an EMPTY table is (CommitPolylineDraft seeds the 0 only
  // when it has a polyline to describe, and GsIo's reader rejects a single entry outright). Writing
  // `{0}` produced a .gs that saved successfully and could never be reopened: issue #60, REQ-079.
  // The paper-space sibling ErasePaperPolyline has always done this; this store was the outlier.
  if (newOff.size() == 1)
    newOff.clear();
  st.userPolylineOffsets = std::move(newOff);
  if (static_cast<size_t>(pi) < st.userPolylineClosed.size())
    st.userPolylineClosed.erase(st.userPolylineClosed.begin() + static_cast<std::ptrdiff_t>(pi));
  if (static_cast<size_t>(pi) < st.userPolylineAttrs.size())
    st.userPolylineAttrs.erase(st.userPolylineAttrs.begin() + static_cast<std::ptrdiff_t>(pi));
}

/// REQ-087. The polyline eraser's twin, with one extra array: the per-VERTEX elevation-point flags
/// are cut over the same [3a, 3b) span the vertices are — in flags, that is [a, b), not the triplet
/// range. Getting that wrong would leave the flag array the right length overall while shifting every
/// flag after the erased line by a vertex or three, which is silent (ADR-035 (a)).
static void EraseFeatureLineByIndex(AppCommandState& st, int fi) {
  if (fi < 0 || static_cast<size_t>(fi + 1) >= st.featureLineOffsets.size())
    return;
  const int nfl = static_cast<int>(st.featureLineOffsets.size()) - 1;
  std::vector<int> nvPer(static_cast<size_t>(nfl));
  for (int i = 0; i < nfl; ++i)
    nvPer[static_cast<size_t>(i)] =
        st.featureLineOffsets[static_cast<size_t>(i + 1)] - st.featureLineOffsets[static_cast<size_t>(i)];
  const int a = st.featureLineOffsets[static_cast<size_t>(fi)];
  const int b = st.featureLineOffsets[static_cast<size_t>(fi + 1)];
  if (static_cast<size_t>(3 * b) <= st.featureLineVerts.size())
    st.featureLineVerts.erase(st.featureLineVerts.begin() + static_cast<std::ptrdiff_t>(3 * a),
                              st.featureLineVerts.begin() + static_cast<std::ptrdiff_t>(3 * b));
  if (static_cast<size_t>(b) <= st.featureLineElevPt.size())
    st.featureLineElevPt.erase(st.featureLineElevPt.begin() + static_cast<std::ptrdiff_t>(a),
                               st.featureLineElevPt.begin() + static_cast<std::ptrdiff_t>(b));
  std::vector<int> newOff;
  newOff.reserve(static_cast<size_t>(std::max(0, nfl - 1) + 1));
  newOff.push_back(0);
  int run = 0;
  for (int i = 0; i < nfl; ++i) {
    if (i == fi)
      continue;
    run += nvPer[static_cast<size_t>(i)];
    newOff.push_back(run);
  }
  // Same rule as polylines, and for the same reason (issue #60): zero feature lines is spelled as an
  // EMPTY table, never `{0}`. The invariant added for this store checks it too.
  if (newOff.size() == 1)
    newOff.clear();
  st.featureLineOffsets = std::move(newOff);
  if (static_cast<size_t>(fi) < st.featureLineClosed.size())
    st.featureLineClosed.erase(st.featureLineClosed.begin() + static_cast<std::ptrdiff_t>(fi));
  if (static_cast<size_t>(fi) < st.featureLineInfo.size())
    st.featureLineInfo.erase(st.featureLineInfo.begin() + static_cast<std::ptrdiff_t>(fi));
  if (static_cast<size_t>(fi) < st.featureLineAttrs.size())
    st.featureLineAttrs.erase(st.featureLineAttrs.begin() + static_cast<std::ptrdiff_t>(fi));
}

void EraseCadAnnotationAtIndex(AppCommandState& st, size_t annIndex) {
  if (annIndex >= st.cadAnnotations.size())
    return;
  // Clear the owning point's link. There is deliberately **no renumbering pass** here any more:
  // survey points reference their label by stable id (REQ-076 / architecture §11.9), so erasing an
  // annotation cannot change what any other point's link means. The loop this replaces walked every
  // survey point decrementing indices, and it was the evidence that index references do not scale —
  // one reference pair, ~46 maintenance sites (ADR-027 Context).
  const std::uint64_t doomedId = annIndex < st.cadAnnotationAttrs.size()
                                     ? st.cadAnnotationAttrs[annIndex].id
                                     : 0;
  if (doomedId != 0)
    for (SurveyPoint& q : st.surveyPoints)
      if (q.labelMtextAnnId == doomedId)
        q.labelMtextAnnId = 0;
  st.selection.erase(std::remove_if(st.selection.begin(), st.selection.end(),
                                    [&](const SelectedEntity& e) {
                                      return e.type == SelectedEntity::Type::Annotation &&
                                             e.index == static_cast<int>(annIndex);
                                    }),
                     st.selection.end());
  for (SelectedEntity& e : st.selection) {
    if (e.type == SelectedEntity::Type::Annotation && e.index > static_cast<int>(annIndex))
      --e.index;
  }
  st.cadAnnotations.erase(st.cadAnnotations.begin() + static_cast<std::ptrdiff_t>(annIndex));
  if (annIndex < st.cadAnnotationAttrs.size())
    st.cadAnnotationAttrs.erase(st.cadAnnotationAttrs.begin() + static_cast<std::ptrdiff_t>(annIndex));
}

void DeleteSelectedSurveyPoints(AppCommandState& st, std::vector<std::string>& log) {
  if (st.selectedSurveyPointIndices.empty())
    return;
  PushUndoSnapshot(st, "Delete survey points");
  std::vector<int> ix = st.selectedSurveyPointIndices;
  std::sort(ix.begin(), ix.end(), std::greater<int>());
  ix.erase(std::unique(ix.begin(), ix.end()), ix.end());
  size_t n = 0;
  for (int i : ix) {
    if (i >= 0 && static_cast<size_t>(i) < st.surveyPoints.size()) {
      RemoveSurveyPointAt(st, static_cast<size_t>(i));
      ++n;
    }
  }
  st.selectedSurveyPointIndices.clear();
  if (n > 0) {
    BumpCadGpuCache(st);
    log.push_back("Deleted " + std::to_string(n) + " survey point(s).");
  }
}

void SyncSurveyPointLinkedMtextSelection(AppCommandState& st, int surveyPointIndex) {
  if (surveyPointIndex < 0 || static_cast<size_t>(surveyPointIndex) >= st.surveyPoints.size())
    return;
  const bool selected =
      std::find(st.selectedSurveyPointIndices.begin(), st.selectedSurveyPointIndices.end(), surveyPointIndex) !=
      st.selectedSurveyPointIndices.end();
  const int annIx = FindSurveyLabelAnnIndex(st, st.surveyPoints[static_cast<size_t>(surveyPointIndex)]);
  if (annIx < 0)
    return;
  const auto hasAnnSel = [&]() {
    return std::find_if(st.selection.begin(), st.selection.end(), [&](const SelectedEntity& e) {
             return e.type == SelectedEntity::Type::Annotation && e.index == annIx;
           }) != st.selection.end();
  };
  if (selected) {
    if (!hasAnnSel()) {
      SelectedEntity se{};
      se.type = SelectedEntity::Type::Annotation;
      se.index = annIx;
      st.selection.push_back(se);
    }
  } else {
    st.selection.erase(std::remove_if(st.selection.begin(), st.selection.end(),
                                      [&](const SelectedEntity& e) {
                                        return e.type == SelectedEntity::Type::Annotation && e.index == annIx;
                                      }),
                       st.selection.end());
  }
}

void ApplyLinkedSurveyForAnnotationPick(AppCommandState& st, int annIndex, bool keyShift) {
  if (annIndex < 0 || static_cast<size_t>(annIndex) >= st.cadAnnotations.size())
    return;
  const CadAnnotation& a = st.cadAnnotations[static_cast<size_t>(annIndex)];
  if (a.kind != CadAnnotation::Kind::Mtext || a.surveyPointLabelForId < 0)
    return;
  const int spi = SurveyPointIndexForId(st, a.surveyPointLabelForId);
  if (spi < 0)
    return;
  auto& sv = st.selectedSurveyPointIndices;
  const auto sit = std::find(sv.begin(), sv.end(), spi);
  if (keyShift) {
    if (sit != sv.end())
      sv.erase(sit);
    else
      sv.push_back(spi);
  } else {
    sv.clear();
    sv.push_back(spi);
  }
}

void ExecuteDeleteSelection(AppCommandState& st, std::vector<std::string>& log) {
  if (st.selection.empty())
    return;
  PushUndoSnapshot(st, "Delete");
  std::set<int> lineIx;
  std::set<int> circIx;
  std::set<int> annIx;
  std::set<int> arcIx;
  std::set<int> ellIx;
  std::set<int> polyIx;
  std::set<int> flIx;  // REQ-087
  const size_t nLines = st.userLinesFlat.size() / 6;
  const size_t nCirc = st.userCirclesCxCyZR.size() / 4;
  const size_t nAnn = st.cadAnnotations.size();
  const size_t nArc = st.userArcs.size();
  const size_t nEll = st.userEllipses.size();
  const size_t nPoly = st.userPolylineOffsets.size() > 0 ? st.userPolylineOffsets.size() - 1 : 0;
  const size_t nFl = st.featureLineOffsets.size() > 0 ? st.featureLineOffsets.size() - 1 : 0;
  for (const auto& e : st.selection) {
    if (e.type == SelectedEntity::Type::LineSeg && e.index >= 0 && static_cast<size_t>(e.index) < nLines)
      lineIx.insert(e.index);
    else if (e.type == SelectedEntity::Type::Circle && e.index >= 0 && static_cast<size_t>(e.index) < nCirc)
      circIx.insert(e.index);
    else if (e.type == SelectedEntity::Type::Annotation && e.index >= 0 && static_cast<size_t>(e.index) < nAnn)
      annIx.insert(e.index);
    else if (e.type == SelectedEntity::Type::Arc && e.index >= 0 && static_cast<size_t>(e.index) < nArc)
      arcIx.insert(e.index);
    else if (e.type == SelectedEntity::Type::Ellipse && e.index >= 0 && static_cast<size_t>(e.index) < nEll)
      ellIx.insert(e.index);
    else if (e.type == SelectedEntity::Type::Polyline && e.index >= 0 && static_cast<size_t>(e.index) < nPoly)
      polyIx.insert(e.index);
    else if (e.type == SelectedEntity::Type::FeatureLine && e.index >= 0 &&
             static_cast<size_t>(e.index) < nFl)
      flIx.insert(e.index);  // REQ-087
  }

  std::vector<int> pv(polyIx.begin(), polyIx.end());
  std::sort(pv.begin(), pv.end(), std::greater<int>());
  for (int idx : pv)
    ErasePolylineByIndex(st, idx);

  // REQ-087. Highest index first, like every other compacting erase here, so an earlier removal
  // cannot shift an index that has not been used yet.
  std::vector<int> flv(flIx.begin(), flIx.end());
  std::sort(flv.begin(), flv.end(), std::greater<int>());
  for (int idx : flv)
    EraseFeatureLineByIndex(st, idx);

  std::vector<int> lv(lineIx.begin(), lineIx.end());
  std::sort(lv.begin(), lv.end(), std::greater<int>());
  for (int idx : lv) {
    const size_t k = static_cast<size_t>(idx) * 6;
    if (k + 5 >= st.userLinesFlat.size())
      continue;
    st.userLinesFlat.erase(st.userLinesFlat.begin() + static_cast<std::ptrdiff_t>(k),
                           st.userLinesFlat.begin() + static_cast<std::ptrdiff_t>(k + 6));
    if (static_cast<size_t>(idx) < st.userLineAttrs.size())
      st.userLineAttrs.erase(st.userLineAttrs.begin() + static_cast<std::ptrdiff_t>(idx));
  }

  std::vector<int> cv(circIx.begin(), circIx.end());
  std::sort(cv.begin(), cv.end(), std::greater<int>());
  for (int idx : cv) {
    const size_t k = static_cast<size_t>(idx) * 4;
    if (k + 3 >= st.userCirclesCxCyZR.size())
      continue;
    // k + 4, not k + 3: erase() takes a HALF-OPEN range and this store is stride 4
    // (cx, cy, z, r — architecture §11.8). Removing three floats left the array a non-multiple of
    // the stride and shifted every later circle by one slot, so each read its predecessor's radius
    // as its centre X — and SAVEAS wrote that out. Issue #62.
    st.userCirclesCxCyZR.erase(st.userCirclesCxCyZR.begin() + static_cast<std::ptrdiff_t>(k),
                               st.userCirclesCxCyZR.begin() + static_cast<std::ptrdiff_t>(k + 4));
    if (static_cast<size_t>(idx) < st.userCircleAttrs.size())
      st.userCircleAttrs.erase(st.userCircleAttrs.begin() + static_cast<std::ptrdiff_t>(idx));
  }

  std::vector<int> av(annIx.begin(), annIx.end());
  std::sort(av.begin(), av.end(), std::greater<int>());
  for (int idx : av)
    EraseCadAnnotationAtIndex(st, static_cast<size_t>(idx));

  std::vector<int> arv(arcIx.begin(), arcIx.end());
  std::sort(arv.begin(), arv.end(), std::greater<int>());
  for (int idx : arv) {
    const size_t k = static_cast<size_t>(idx);
    if (k >= st.userArcs.size())
      continue;
    st.userArcs.erase(st.userArcs.begin() + static_cast<std::ptrdiff_t>(idx));
    if (k < st.userArcAttrs.size())
      st.userArcAttrs.erase(st.userArcAttrs.begin() + static_cast<std::ptrdiff_t>(idx));
  }

  std::vector<int> ev(ellIx.begin(), ellIx.end());
  std::sort(ev.begin(), ev.end(), std::greater<int>());
  for (int idx : ev) {
    const size_t k = static_cast<size_t>(idx);
    if (k >= st.userEllipses.size())
      continue;
    st.userEllipses.erase(st.userEllipses.begin() + static_cast<std::ptrdiff_t>(idx));
    if (k < st.userEllAttrs.size())
      st.userEllAttrs.erase(st.userEllAttrs.begin() + static_cast<std::ptrdiff_t>(idx));
  }

  // Filled regions (REQ-042): erase from cadFilledRegions + parallel attrs, highest index first.
  std::set<int> fillIx;
  const size_t nFill = st.cadFilledRegions.size();
  for (const auto& e : st.selection) {
    if (e.type == SelectedEntity::Type::FilledRegion && e.index >= 0 && static_cast<size_t>(e.index) < nFill)
      fillIx.insert(e.index);
  }
  std::vector<int> fv(fillIx.begin(), fillIx.end());
  std::sort(fv.begin(), fv.end(), std::greater<int>());
  for (int idx : fv) {
    st.cadFilledRegions.erase(st.cadFilledRegions.begin() + static_cast<std::ptrdiff_t>(idx));
    if (static_cast<size_t>(idx) < st.cadFilledRegionAttrs.size())
      st.cadFilledRegionAttrs.erase(st.cadFilledRegionAttrs.begin() + static_cast<std::ptrdiff_t>(idx));
  }

  // Imported meshes (REQ-063: "erasing a mesh is undoable in one step" — the caller has already
  // pushed one snapshot for this whole erase, so removing the pointer here is that one step).
  std::set<int> meshIx;
  const size_t nMesh = st.cadMeshes.size();
  for (const auto& e : st.selection) {
    if (e.type == SelectedEntity::Type::Mesh && e.index >= 0 && static_cast<size_t>(e.index) < nMesh)
      meshIx.insert(e.index);
  }
  std::vector<int> mv(meshIx.begin(), meshIx.end());
  std::sort(mv.begin(), mv.end(), std::greater<int>());
  for (int idx : mv) {
    st.cadMeshes.erase(st.cadMeshes.begin() + static_cast<std::ptrdiff_t>(idx));
    if (static_cast<size_t>(idx) < st.cadMeshAttrs.size())
      st.cadMeshAttrs.erase(st.cadMeshAttrs.begin() + static_cast<std::ptrdiff_t>(idx));
  }

  // TIN surfaces (REQ-068: "erasing a surface is undoable in one step" — the caller has already
  // pushed one snapshot for this whole erase, so removing it here is that one step).
  //
  // Goes through EraseSurfaceAtIndex rather than erasing the two vectors inline like the blocks
  // above, so there is one erase path to keep correct. An in-flight rebuild (REQ-069) needs no
  // cancellation here: the job is keyed on the surface's stable id (ADR-036 (a)), that id is never
  // reused (REQ-076), so when the worker finishes `FindSurfaceIndexById` returns -1 and the result is
  // discarded — REQ-069's own rule, reached without a second mechanism.
  {
    std::set<int> surfIx;
    const size_t nSurf = st.cadSurfaces.size();
    for (const auto& e : st.selection) {
      if (e.type == SelectedEntity::Type::Surface && e.index >= 0 && static_cast<size_t>(e.index) < nSurf)
        surfIx.insert(e.index);
    }
    std::vector<int> sv(surfIx.begin(), surfIx.end());
    std::sort(sv.begin(), sv.end(), std::greater<int>());
    for (int idx : sv)
      EraseSurfaceAtIndex(st, static_cast<size_t>(idx));
  }

  // PDF underlays: release GL texture and erase from highest index downward.
  std::set<int> pdfIx;
  const size_t nPdf = st.pdfAttachments.size();
  for (const auto& e : st.selection) {
    if (e.type == SelectedEntity::Type::PdfUnderlay && e.index >= 0 &&
        static_cast<size_t>(e.index) < nPdf)
      pdfIx.insert(e.index);
  }
  std::vector<int> pdfv(pdfIx.begin(), pdfIx.end());
  std::sort(pdfv.begin(), pdfv.end(), std::greater<int>());
  for (int idx : pdfv) {
    PdfAttach_ReleaseTexture(st.pdfAttachments[static_cast<size_t>(idx)]);
    st.pdfAttachments.erase(st.pdfAttachments.begin() + static_cast<std::ptrdiff_t>(idx));
  }

  const size_t nDel = lineIx.size() + circIx.size() + annIx.size() + arcIx.size() + ellIx.size() +
                      polyIx.size() + pdfIx.size() + fillIx.size() + meshIx.size();
  st.selection.clear();
  AbortMtextGripInteraction(st);
  ClearDimGripInteraction(st);
  if (nDel > 0) {
    BumpCadGpuCache(st);
    log.push_back("Deleted " + std::to_string(nDel) + " object(s).");
  }
}

static bool SegSegIntersectParam(float ax, float ay, float bx, float by, float cx, float cy, float dx, float dy,
                                 float* tAB) {
  const float rx = bx - ax;
  const float ry = by - ay;
  const float sx = dx - cx;
  const float sy = dy - cy;
  const float denom = rx * sy - ry * sx;
  if (std::fabs(denom) < 1e-12f)
    return false;
  /// Solve A + t r = C + u s  =>  t r - u s = C - A  (Cramer's rule on t, u).
  const float wx = cx - ax;
  const float wy = cy - ay;
  const float t = (wx * sy - wy * sx) / denom;
  const float u = (wx * ry - wy * rx) / denom;
  if (t >= 0.f && t <= 1.f && u >= 0.f && u <= 1.f) {
    *tAB = t;
    return true;
  }
  return false;
}

struct TrimTargetEdge {
  enum Kind : uint8_t { Line = 0, Poly = 1 } kind = Line;
  int lineIx = -1;
  int polyIx = -1;
  int vLo = -1;
};

static bool SelectedEntityMatches(const SelectedEntity& a, const SelectedEntity& b) {
  return a.type == b.type && a.index == b.index;
}

static void CollectCutSegments(const AppCommandState& st, const SelectedEntity& cut,
                               std::vector<std::array<float, 4>>* out) {
  using ST = SelectedEntity::Type;
  if (cut.type == ST::LineSeg) {
    const size_t k = static_cast<size_t>(cut.index) * 6;
    if (k + 5 < st.userLinesFlat.size())
      out->push_back({st.userLinesFlat[k], st.userLinesFlat[k + 1], st.userLinesFlat[k + 3],
                      st.userLinesFlat[k + 4]});
    return;
  }
  if (cut.type == ST::Circle) {
    const size_t k = static_cast<size_t>(cut.index) * 4;
    if (k + 3 >= st.userCirclesCxCyZR.size())
      return;
    const float cx = st.userCirclesCxCyZR[k];
    const float cy = st.userCirclesCxCyZR[k + 1];
    const float r = st.userCirclesCxCyZR[k + 3];
    constexpr int n = 48;
    const double dcx = static_cast<double>(cx);
    const double dcy = static_cast<double>(cy);
    const double dr = static_cast<double>(r);
    double px = 0.;
    double py = 0.;
    CirclePointWorld(dcx, dcy, dr, 0.0, &px, &py);
    for (int i = 1; i <= n; ++i) {
      constexpr double kTwoPi = 6.283185307179586;
      const double t = kTwoPi * static_cast<double>(i) / static_cast<double>(n);
      double x = 0.;
      double y = 0.;
      CirclePointWorld(dcx, dcy, dr, t, &x, &y);
      out->push_back({static_cast<float>(px), static_cast<float>(py), static_cast<float>(x), static_cast<float>(y)});
      px = x;
      py = y;
    }
    return;
  }
  if (cut.type == ST::Arc) {
    const size_t k = static_cast<size_t>(cut.index);
    if (k >= st.userArcs.size())
      return;
    const CadArc& a = st.userArcs[k];
    constexpr int n = 40;
    const double dcx = static_cast<double>(a.cx);
    const double dcy = static_cast<double>(a.cy);
    const double dr = static_cast<double>(a.r);
    double px = 0.;
    double py = 0.;
    CirclePointWorld(dcx, dcy, dr, static_cast<double>(a.startRad), &px, &py);
    for (int i = 1; i <= n; ++i) {
      const double u = static_cast<double>(i) / static_cast<double>(n);
      const double ang = static_cast<double>(a.startRad + a.sweepRad * u);
      double x = 0.;
      double y = 0.;
      CirclePointWorld(dcx, dcy, dr, ang, &x, &y);
      out->push_back({static_cast<float>(px), static_cast<float>(py), static_cast<float>(x), static_cast<float>(y)});
      px = x;
      py = y;
    }
    return;
  }
  if (cut.type == ST::Ellipse) {
    const size_t k = static_cast<size_t>(cut.index);
    if (k >= st.userEllipses.size())
      return;
    const CadEllipse& el = st.userEllipses[k];
    const double ma = std::hypot(static_cast<double>(el.majVx), static_cast<double>(el.majVy));
    if (ma < 1e-12)
      return;
    const double ux = static_cast<double>(el.majVx) / ma;
    const double uy = static_cast<double>(el.majVy) / ma;
    const double pxv = -uy;
    const double pyv = ux;
    const double mb = ma * static_cast<double>(el.ratio);
    constexpr int n = 48;
    constexpr double kTwoPi = 6.283185307179586;
    const double ecx = static_cast<double>(el.cx);
    const double ecy = static_cast<double>(el.cy);
    double px = ecx + ux * ma;
    double py = ecy + uy * ma;
    for (int i = 1; i <= n; ++i) {
      const double ang = kTwoPi * static_cast<double>(i) / static_cast<double>(n);
      const double c = std::cos(ang);
      const double s = std::sin(ang);
      const double x = ecx + ux * (ma * c) + pxv * (mb * s);
      const double y = ecy + uy * (ma * c) + pyv * (mb * s);
      out->push_back({static_cast<float>(px), static_cast<float>(py), static_cast<float>(x), static_cast<float>(y)});
      px = x;
      py = y;
    }
    return;
  }
  if (cut.type == ST::Polyline) {
    const int pi = cut.index;
    if (pi < 0 || static_cast<size_t>(pi + 1) >= st.userPolylineOffsets.size())
      return;
    const int v0 = st.userPolylineOffsets[static_cast<size_t>(pi)];
    const int v1 = st.userPolylineOffsets[static_cast<size_t>(pi + 1)];
    const bool closed =
        static_cast<size_t>(pi) < st.userPolylineClosed.size() && st.userPolylineClosed[static_cast<size_t>(pi)];
    for (int vi = v0; vi + 1 < v1; ++vi) {
      const float ax = st.userPolylineVerts[static_cast<size_t>(vi * 3)];
      const float ay = st.userPolylineVerts[static_cast<size_t>(vi * 3 + 1)];
      const float bx = st.userPolylineVerts[static_cast<size_t>((vi + 1) * 3)];
      const float by = st.userPolylineVerts[static_cast<size_t>((vi + 1) * 3 + 1)];
      out->push_back({ax, ay, bx, by});
    }
    if (closed && v1 - v0 >= 2) {
      const float ax = st.userPolylineVerts[static_cast<size_t>((v1 - 1) * 3)];
      const float ay = st.userPolylineVerts[static_cast<size_t>((v1 - 1) * 3 + 1)];
      const float bx = st.userPolylineVerts[static_cast<size_t>(v0 * 3)];
      const float by = st.userPolylineVerts[static_cast<size_t>(v0 * 3 + 1)];
      out->push_back({ax, ay, bx, by});
    }
  }
}

static void AppendPolylineCutEdgesExcept(const AppCommandState& st, int pi, int skipEdgeVi,
                                         std::vector<std::array<float, 4>>* out) {
  if (pi < 0 || static_cast<size_t>(pi + 1) >= st.userPolylineOffsets.size())
    return;
  const int v0 = st.userPolylineOffsets[static_cast<size_t>(pi)];
  const int v1 = st.userPolylineOffsets[static_cast<size_t>(pi + 1)];
  const bool closed =
      static_cast<size_t>(pi) < st.userPolylineClosed.size() && st.userPolylineClosed[static_cast<size_t>(pi)];
  auto pushEdge = [&](int vi) {
    if (vi == skipEdgeVi)
      return;
    const float ax = st.userPolylineVerts[static_cast<size_t>(vi * 3)];
    const float ay = st.userPolylineVerts[static_cast<size_t>(vi * 3 + 1)];
    const float bx = st.userPolylineVerts[static_cast<size_t>((vi + 1) * 3)];
    const float by = st.userPolylineVerts[static_cast<size_t>((vi + 1) * 3 + 1)];
    out->push_back({ax, ay, bx, by});
  };
  for (int vi = v0; vi + 1 < v1; ++vi)
    pushEdge(vi);
  if (closed && v1 - v0 >= 2) {
    const int closingVi = v1 - 1;
    if (closingVi != skipEdgeVi) {
      const float ax = st.userPolylineVerts[static_cast<size_t>(closingVi * 3)];
      const float ay = st.userPolylineVerts[static_cast<size_t>(closingVi * 3 + 1)];
      const float bx = st.userPolylineVerts[static_cast<size_t>(v0 * 3)];
      const float by = st.userPolylineVerts[static_cast<size_t>(v0 * 3 + 1)];
      out->push_back({ax, ay, bx, by});
    }
  }
}

static void CollectAllDrawingCutSegmentsExceptTarget(const AppCommandState& st, const TrimTargetEdge* excludeEdge,
                                                     std::vector<std::array<float, 4>>* out) {
  out->clear();
  const auto& Lf = st.userLinesFlat;
  if (Lf.size() % 6 == 0) {
    for (size_t li = 0; li + 5 < Lf.size(); li += 6) {
      const int idx = static_cast<int>(li / 6);
      if (excludeEdge && excludeEdge->kind == TrimTargetEdge::Line && excludeEdge->lineIx == idx)
        continue;
      SelectedEntity cut{};
      cut.type = SelectedEntity::Type::LineSeg;
      cut.index = idx;
      CollectCutSegments(st, cut, out);
    }
  }
  const auto& C = st.userCirclesCxCyZR;
  if (C.size() % 4 == 0) {
    for (size_t ci = 0; ci + 3 < C.size(); ci += 4) {
      SelectedEntity cut{};
      cut.type = SelectedEntity::Type::Circle;
      cut.index = static_cast<int>(ci / 4);
      CollectCutSegments(st, cut, out);
    }
  }
  for (size_t ai = 0; ai < st.userArcs.size(); ++ai) {
    SelectedEntity cut{};
    cut.type = SelectedEntity::Type::Arc;
    cut.index = static_cast<int>(ai);
    CollectCutSegments(st, cut, out);
  }
  for (size_t ei = 0; ei < st.userEllipses.size(); ++ei) {
    SelectedEntity cut{};
    cut.type = SelectedEntity::Type::Ellipse;
    cut.index = static_cast<int>(ei);
    CollectCutSegments(st, cut, out);
  }
  const int nPoly =
      static_cast<int>(st.userPolylineOffsets.size() > 0 ? st.userPolylineOffsets.size() - 1 : 0);
  for (int pi = 0; pi < nPoly; ++pi) {
    if (excludeEdge && excludeEdge->kind == TrimTargetEdge::Poly && excludeEdge->polyIx == pi)
      AppendPolylineCutEdgesExcept(st, pi, excludeEdge->vLo, out);
    else {
      SelectedEntity cut{};
      cut.type = SelectedEntity::Type::Polyline;
      cut.index = pi;
      CollectCutSegments(st, cut, out);
    }
  }
}

static void BuildTrimCutSegments(const AppCommandState& st, const std::vector<SelectedEntity>& cutters,
                                 const TrimTargetEdge* excludeEdge, std::vector<std::array<float, 4>>* out) {
  out->clear();
  for (const SelectedEntity& cut : cutters) {
    if (excludeEdge && cut.type == SelectedEntity::Type::LineSeg && excludeEdge->kind == TrimTargetEdge::Line &&
        cut.index == excludeEdge->lineIx)
      continue;
    if (excludeEdge && cut.type == SelectedEntity::Type::Polyline && excludeEdge->kind == TrimTargetEdge::Poly &&
        cut.index == excludeEdge->polyIx) {
      AppendPolylineCutEdgesExcept(st, cut.index, excludeEdge->vLo, out);
      continue;
    }
    CollectCutSegments(st, cut, out);
  }
}

static bool PickClosestTrimTarget(const AppCommandState& st, float wx, float wy, float tolWorld,
                                  TrimTargetEdge* outRef, float* ax, float* ay, float* bx, float* by,
                                  float* outDistSq) {
  if (!outRef || !ax || !ay || !bx || !by || !outDistSq)
    return false;
  const float tol2 = tolWorld * tolWorld;
  bool any = false;
  float best = 0.f;
  TrimTargetEdge bestR{};
  float bax = 0.f, bay = 0.f, bbx = 0.f, bby = 0.f;

  const auto& Lf = st.userLinesFlat;
  if (Lf.size() % 6 == 0) {
    for (size_t li = 0; li + 5 < Lf.size(); li += 6) {
      const float x0 = Lf[li];
      const float y0 = Lf[li + 1];
      const float x1 = Lf[li + 3];
      const float y1 = Lf[li + 4];
      const float d2 = CadCmdGeom::DistSqPointSegment(wx, wy, x0, y0, x1, y1);
      if (d2 > tol2)
        continue;
      if (!any || d2 < best - 1e-12f) {
        any = true;
        best = d2;
        bestR.kind = TrimTargetEdge::Line;
        bestR.lineIx = static_cast<int>(li / 6);
        bax = x0;
        bay = y0;
        bbx = x1;
        bby = y1;
      }
    }
  }

  const int nPoly =
      static_cast<int>(st.userPolylineOffsets.size() > 0 ? st.userPolylineOffsets.size() - 1 : 0);
  for (int pi = 0; pi < nPoly; ++pi) {
    const int v0 = st.userPolylineOffsets[static_cast<size_t>(pi)];
    const int v1 = st.userPolylineOffsets[static_cast<size_t>(pi + 1)];
    const bool closed =
        static_cast<size_t>(pi) < st.userPolylineClosed.size() && st.userPolylineClosed[static_cast<size_t>(pi)];
    auto tryEdge = [&](int vi) {
      const float x0 = st.userPolylineVerts[static_cast<size_t>(vi * 3)];
      const float y0 = st.userPolylineVerts[static_cast<size_t>(vi * 3 + 1)];
      const float x1 = st.userPolylineVerts[static_cast<size_t>((vi + 1) * 3)];
      const float y1 = st.userPolylineVerts[static_cast<size_t>((vi + 1) * 3 + 1)];
      const float d2 = CadCmdGeom::DistSqPointSegment(wx, wy, x0, y0, x1, y1);
      if (d2 > tol2)
        return;
      if (!any || d2 < best - 1e-12f) {
        any = true;
        best = d2;
        bestR.kind = TrimTargetEdge::Poly;
        bestR.polyIx = pi;
        bestR.vLo = vi;
        bax = x0;
        bay = y0;
        bbx = x1;
        bby = y1;
      }
    };
    for (int vi = v0; vi + 1 < v1; ++vi)
      tryEdge(vi);
    if (closed && v1 - v0 >= 2)
      tryEdge(v1 - 1);
  }

  if (!any)
    return false;
  *outRef = bestR;
  *ax = bax;
  *ay = bay;
  *bx = bbx;
  *by = bby;
  *outDistSq = best;
  return true;
}

/// Drawing edge (line or poly segment) whose geometry passes closest to the user-drawn segment \p u1–\p u2.
static bool PickTrimTargetClosestToDrawnSegment(const AppCommandState& st, float u1x, float u1y, float u2x, float u2y,
                                                 float tolWorld, TrimTargetEdge* outRef, float* ax, float* ay,
                                                 float* bx, float* by, float* outDistSq) {
  if (!outRef || !ax || !ay || !bx || !by || !outDistSq)
    return false;
  const float tol2 = tolWorld * tolWorld;
  bool any = false;
  float best = 0.f;
  TrimTargetEdge bestR{};
  float bax = 0.f, bay = 0.f, bbx = 0.f, bby = 0.f;

  const auto& Lf = st.userLinesFlat;
  if (Lf.size() % 6 == 0) {
    for (size_t li = 0; li + 5 < Lf.size(); li += 6) {
      const float x0 = Lf[li];
      const float y0 = Lf[li + 1];
      const float x1 = Lf[li + 3];
      const float y1 = Lf[li + 4];
      const float d2 = CadCmdGeom::MinDistSqSegSeg(u1x, u1y, u2x, u2y, x0, y0, x1, y1);
      if (d2 > tol2)
        continue;
      if (!any || d2 < best - 1e-18f) {
        any = true;
        best = d2;
        bestR.kind = TrimTargetEdge::Line;
        bestR.lineIx = static_cast<int>(li / 6);
        bax = x0;
        bay = y0;
        bbx = x1;
        bby = y1;
      }
    }
  }

  const int nPoly =
      static_cast<int>(st.userPolylineOffsets.size() > 0 ? st.userPolylineOffsets.size() - 1 : 0);
  for (int pi = 0; pi < nPoly; ++pi) {
    const int v0 = st.userPolylineOffsets[static_cast<size_t>(pi)];
    const int v1 = st.userPolylineOffsets[static_cast<size_t>(pi + 1)];
    const bool closed =
        static_cast<size_t>(pi) < st.userPolylineClosed.size() && st.userPolylineClosed[static_cast<size_t>(pi)];
    auto tryEdge = [&](int vi) {
      const float x0 = st.userPolylineVerts[static_cast<size_t>(vi * 3)];
      const float y0 = st.userPolylineVerts[static_cast<size_t>(vi * 3 + 1)];
      const float x1 = st.userPolylineVerts[static_cast<size_t>((vi + 1) * 3)];
      const float y1 = st.userPolylineVerts[static_cast<size_t>((vi + 1) * 3 + 1)];
      const float d2 = CadCmdGeom::MinDistSqSegSeg(u1x, u1y, u2x, u2y, x0, y0, x1, y1);
      if (d2 > tol2)
        return;
      if (!any || d2 < best - 1e-18f) {
        any = true;
        best = d2;
        bestR.kind = TrimTargetEdge::Poly;
        bestR.polyIx = pi;
        bestR.vLo = vi;
        bax = x0;
        bay = y0;
        bbx = x1;
        bby = y1;
      }
    };
    for (int vi = v0; vi + 1 < v1; ++vi)
      tryEdge(vi);
    if (closed && v1 - v0 >= 2)
      tryEdge(v1 - 1);
  }

  if (!any)
    return false;
  *outRef = bestR;
  *ax = bax;
  *ay = bay;
  *bx = bbx;
  *by = bby;
  *outDistSq = best;
  return true;
}

static bool TrimSegmentIntersectPickSide(float ax, float ay, float bx, float by, float pickX, float pickY,
                                         const std::vector<std::array<float, 4>>& cuts, const AppCommandState& st,
                                         float fenceFx, float fenceFy, float fenceGx, float fenceGy,
                                         bool useFenceToPickIntersection,
                                         float* outIx, float* outIy, bool* trimFromA, std::vector<std::string>* log) {
  float epsGeom = 1e-5f;
  double dmnX = 0.;
  double dmxX = 0.;
  double dmnY = 0.;
  double dmxY = 0.;
  if (ComputeWorldExtents(st, &dmnX, &dmxX, &dmnY, &dmxY))
    epsGeom = std::max(1e-8f, static_cast<float>(1e-6 * std::max(dmxX - dmnX, dmxY - dmnY)));

  const float vx = bx - ax;
  const float vy = by - ay;
  const float len2 = vx * vx + vy * vy;
  if (len2 < 1e-18f) {
    if (log)
      log->push_back("TRIM — degenerate segment.");
    return false;
  }
  const float segLen = std::sqrt(len2);
  const float epsT =
      std::clamp(segLen > 1e-12f ? epsGeom / segLen : 1e-7f, 1e-9f, 0.05f);

  std::vector<float> ts;
  for (const auto& seg : cuts) {
    float t = 0.f;
    if (SegSegIntersectParam(ax, ay, bx, by, seg[0], seg[1], seg[2], seg[3], &t)) {
      if (t > epsT && t < 1.f - epsT)
        ts.push_back(t);
    }
  }
  if (ts.empty()) {
    if (log)
      log->push_back("TRIM — segment does not cross a cutting edge.");
    return false;
  }

  std::sort(ts.begin(), ts.end());
  ts.erase(std::unique(ts.begin(), ts.end(),
                       [&](float a, float b) { return std::fabs(a - b) < std::max(1e-7f, epsGeom / segLen); }),
           ts.end());

  const float u =
      std::clamp(((pickX - ax) * vx + (pickY - ay) * vy) / len2, 0.f, 1.f);

  const float fdx = fenceGx - fenceFx;
  const float fdy = fenceGy - fenceFy;
  const float fenceLen2 = fdx * fdx + fdy * fdy;
  const bool fenceOk = useFenceToPickIntersection && fenceLen2 >= 1e-24f;

  float tNear = ts.front();
  if (fenceOk) {
    float bestFenceD2 = 1e30f;
    float bestPickAbs = 1e30f;
    for (float t : ts) {
      const float ix = ax + t * vx;
      const float iy = ay + t * vy;
      const float df2 = CadCmdGeom::DistSqPointSegment(ix, iy, fenceFx, fenceFy, fenceGx, fenceGy);
      const float dp = std::fabs(t - u);
      if (df2 < bestFenceD2 - 1e-18f ||
          (std::fabs(df2 - bestFenceD2) <= 1e-18f && dp < bestPickAbs - 1e-12f)) {
        bestFenceD2 = df2;
        bestPickAbs = dp;
        tNear = t;
      }
    }
  } else {
    float bestAbs = std::fabs(ts.front() - u);
    for (float t : ts) {
      const float d = std::fabs(t - u);
      if (d < bestAbs - 1e-12f) {
        bestAbs = d;
        tNear = t;
      }
    }
  }

  const float ix = ax + tNear * vx;
  const float iy = ay + tNear * vy;
  const float epsParam = std::max(1e-7f, epsGeom / segLen);

  bool trimA = false;
  if (u < tNear - epsParam)
    trimA = true;
  else if (u > tNear + epsParam)
    trimA = false;
  else {
    const float dA = (pickX - ax) * (pickX - ax) + (pickY - ay) * (pickY - ay);
    const float dB = (pickX - bx) * (pickX - bx) + (pickY - by) * (pickY - by);
    trimA = dA <= dB;
  }

  *outIx = ix;
  *outIy = iy;
  *trimFromA = trimA;
  return true;
}

/// AutoCAD-style: shorten segment toward the nearest cutting intersection from the pick (portion containing pick is
/// removed).
static bool TrimSegmentToCuttingEdges(AppCommandState& st, const TrimTargetEdge& tgt, float ax, float ay, float bx,
                                      float by, float pickX, float pickY,
                                      const std::vector<std::array<float, 4>>& cuts, bool useFence,
                                      float fenceFx, float fenceFy, float fenceGx, float fenceGy,
                                      std::vector<std::string>& log) {
  float ix = 0.f, iy = 0.f;
  bool trimA = false;
  if (!TrimSegmentIntersectPickSide(ax, ay, bx, by, pickX, pickY, cuts, st, fenceFx, fenceFy, fenceGx, fenceGy,
                                    useFence, &ix, &iy, &trimA, &log))
    return false;

  const float rx = ix;
  const float ry = iy;

  if (tgt.kind == TrimTargetEdge::Line) {
    const size_t k = static_cast<size_t>(tgt.lineIx) * 6;
    if (k + 5 >= st.userLinesFlat.size())
      return false;
    if (trimA) {
      st.userLinesFlat[k] = rx;
      st.userLinesFlat[k + 1] = ry;
    } else {
      st.userLinesFlat[k + 3] = rx;
      st.userLinesFlat[k + 4] = ry;
    }
    const float exx = st.userLinesFlat[k + 3] - st.userLinesFlat[k];
    const float eyy = st.userLinesFlat[k + 4] - st.userLinesFlat[k + 1];
    if (exx * exx + eyy * eyy < 1e-12f) {
      st.userLinesFlat.erase(st.userLinesFlat.begin() + static_cast<std::ptrdiff_t>(k),
                             st.userLinesFlat.begin() + static_cast<std::ptrdiff_t>(k + 6));
      if (static_cast<size_t>(tgt.lineIx) < st.userLineAttrs.size())
        st.userLineAttrs.erase(st.userLineAttrs.begin() + static_cast<std::ptrdiff_t>(tgt.lineIx));
    }
  } else {
    const int vi = trimA ? tgt.vLo : tgt.vLo + 1;
    const size_t vk = static_cast<size_t>(vi * 3);
    if (vk + 1 >= st.userPolylineVerts.size())
      return false;
    st.userPolylineVerts[vk] = rx;
    st.userPolylineVerts[vk + 1] = ry;
  }

  log.push_back("TRIM — segment shortened.");
  return true;
}

// Squared point-segment distance in double precision. Picking math must run in double: at state-plane
// magnitudes (eastings/northings in the millions) a 32-bit float's ULP is ~1 ft, so a float-only distance
// suffers catastrophic cancellation and stops matching the rendered position — entities then "hover" while
// the cursor is feet away. Stored geometry is promoted per-coordinate so distances match what is drawn.
static double PickDistSqPointSegmentD(double px, double py, double ax, double ay, double bx, double by) {
  const double vx = bx - ax;
  const double vy = by - ay;
  const double len2 = vx * vx + vy * vy;
  if (len2 < 1e-24) {
    const double dx = px - ax;
    const double dy = py - ay;
    return dx * dx + dy * dy;
  }
  const double t = std::clamp(((px - ax) * vx + (py - ay) * vy) / len2, 0.0, 1.0);
  const double qx = ax + t * vx;
  const double qy = ay + t * vy;
  const double dx = px - qx;
  const double dy = py - qy;
  return dx * dx + dy * dy;
}

bool PickClosestCadEntity(const AppCommandState& st, double wx, double wy, float tolWorld, SelectedEntity* out,
                          float* outDistSq, const ray3d::Ray* pickRay) {
  if (!out || !outDistSq)
    return false;
  const double tol2 = static_cast<double>(tolWorld) * static_cast<double>(tolWorld);

  // Distance metric (REQ-058). With no ray this is the historical plan-view XY distance, unchanged
  // and bit-identical. With a ray — an orbited camera — it is the true 3D distance from the
  // cursor's ray to the geometry, which is the only way to pick something that is elevated: the
  // ray crosses the work plane at one XY and the elevated entity at another, so an XY test would
  // measure against the wrong point entirely.
  //
  // Only the METRIC changes here. Entity enumeration, strides and indexing are shared by both
  // paths, so the orbited path cannot drift out of step with the plan path.
  const bool useRay = pickRay != nullptr && pickRay->valid();
  auto d2Point = [&](double px, double py, double pz) -> double {
    if (!useRay) {
      const double dx = wx - px, dy = wy - py;
      return dx * dx + dy * dy;
    }
    const double d = ray3d::RayPointDistance(*pickRay, ray3d::Vec3{px, py, pz});
    return d * d;
  };
  auto d2Segment = [&](double ax, double ay, double az, double bx, double by, double bz) -> double {
    if (!useRay)
      return PickDistSqPointSegmentD(wx, wy, ax, ay, bx, by);
    const double d = ray3d::RaySegmentDistance(*pickRay, ray3d::Vec3{ax, ay, az}, ray3d::Vec3{bx, by, bz});
    return d * d;
  };
  bool any = false;
  double best = 0.0;
  SelectedEntity bestE{};
  auto consider = [&](const SelectedEntity& e, double d2) {
    if (d2 > tol2)
      return;
    // REQ-084 (d): an isolated-out entity is invisible, so it must not answer a click either.
    // Gated at this single funnel — every entity type this function enumerates passes through it,
    // so there is no per-type gate to forget.
    if (CadSelectedEntityHidden(st, e))
      return;
    if (!any || d2 < best - 1e-12) {
      any = true;
      best = d2;
      bestE = e;
    }
  };

  const auto& L = st.userLinesFlat;
  if (L.size() % 6 == 0) {
    for (size_t i = 0; i + 5 < L.size(); i += 6) {
      SelectedEntity e{};
      e.type = SelectedEntity::Type::LineSeg;
      e.index = static_cast<int>(i / 6);
      const double d2 = d2Segment(L[i], L[i + 1], L[i + 2], L[i + 3], L[i + 4], L[i + 5]);
      consider(e, d2);
    }
  }
  const auto& C = st.userCirclesCxCyZR;
  if (C.size() % 4 == 0) {
    for (size_t ci = 0; ci + 3 < C.size(); ci += 4) {
      SelectedEntity e{};
      e.type = SelectedEntity::Type::Circle;
      e.index = static_cast<int>(ci / 4);
      const double cx = static_cast<double>(C[ci]);
      const double cy = static_cast<double>(C[ci + 1]);
      const double cz = static_cast<double>(C[ci + 2]);
      const double r = static_cast<double>(C[ci + 3]);
      if (!useRay) {
        // Plan view keeps the exact analytic distance-to-circumference — unchanged.
        const double d = std::hypot(wx - cx, wy - cy);
        const double dr = d - r;
        consider(e, dr * dr);
      } else {
        // Orbited: sample the circumference on the circle's own plane, matching how arcs and
        // ellipses are already picked in this function.
        double bestD2 = 1e300;
        constexpr int n = 48;
        constexpr double twopi = 6.28318530717958647692;
        for (int i = 0; i < n; ++i) {
          const double ang = twopi * static_cast<double>(i) / static_cast<double>(n);
          bestD2 = std::min(bestD2, d2Point(cx + r * std::cos(ang), cy + r * std::sin(ang), cz));
        }
        consider(e, bestD2);
      }
    }
  }
  for (size_t ai = 0; ai < st.userArcs.size(); ++ai) {
    const CadArc& a = st.userArcs[ai];
    SelectedEntity e{};
    e.type = SelectedEntity::Type::Arc;
    e.index = static_cast<int>(ai);
    double bestD2 = 1e300;
    constexpr int n = 36;
    for (int i = 0; i <= n; ++i) {
      const double u = static_cast<double>(i) / static_cast<double>(n);
      const double ang = static_cast<double>(a.startRad) + static_cast<double>(a.sweepRad) * u;
      const double x = static_cast<double>(a.cx) + static_cast<double>(a.r) * std::cos(ang);
      const double y = static_cast<double>(a.cy) + static_cast<double>(a.r) * std::sin(ang);
      bestD2 = std::min(bestD2, d2Point(x, y, static_cast<double>(a.z)));
    }
    consider(e, bestD2);
  }
  for (size_t ei = 0; ei < st.userEllipses.size(); ++ei) {
    const CadEllipse& el = st.userEllipses[ei];
    SelectedEntity e{};
    e.type = SelectedEntity::Type::Ellipse;
    e.index = static_cast<int>(ei);
    const double ma = std::hypot(static_cast<double>(el.majVx), static_cast<double>(el.majVy));
    double bestD2 = 1e300;
    if (ma >= 1e-8) {
      const double ux = static_cast<double>(el.majVx) / ma;
      const double uy = static_cast<double>(el.majVy) / ma;
      const double px = -uy;
      const double py = ux;
      const double mb = ma * static_cast<double>(el.ratio);
      constexpr int n = 36;
      constexpr double twopi = 6.28318530717958647692;
      for (int i = 0; i <= n; ++i) {
        const double ang = twopi * static_cast<double>(i) / static_cast<double>(n);
        const double c = std::cos(ang);
        const double s = std::sin(ang);
        const double x = static_cast<double>(el.cx) + ux * (ma * c) + px * (mb * s);
        const double y = static_cast<double>(el.cy) + uy * (ma * c) + py * (mb * s);
        bestD2 = std::min(bestD2, d2Point(x, y, static_cast<double>(el.z)));
      }
    }
    consider(e, bestD2);
  }
  const int nPoly =
      static_cast<int>(st.userPolylineOffsets.size() > 0 ? st.userPolylineOffsets.size() - 1 : 0);
  for (int pi = 0; pi < nPoly; ++pi) {
    const int v0 = st.userPolylineOffsets[static_cast<size_t>(pi)];
    const int v1 = st.userPolylineOffsets[static_cast<size_t>(pi + 1)];
    const bool closed =
        static_cast<size_t>(pi) < st.userPolylineClosed.size() && st.userPolylineClosed[static_cast<size_t>(pi)];
    SelectedEntity e{};
    e.type = SelectedEntity::Type::Polyline;
    e.index = pi;
    double bestD2 = 1e300;
    for (int vi = v0; vi + 1 < v1; ++vi) {
      const size_t A = static_cast<size_t>(vi) * 3, B = static_cast<size_t>(vi + 1) * 3;
      bestD2 = std::min(bestD2, d2Segment(st.userPolylineVerts[A], st.userPolylineVerts[A + 1],
                                          st.userPolylineVerts[A + 2], st.userPolylineVerts[B],
                                          st.userPolylineVerts[B + 1], st.userPolylineVerts[B + 2]));
    }
    if (closed && v1 - v0 >= 2) {
      const size_t A = static_cast<size_t>(v1 - 1) * 3, B = static_cast<size_t>(v0) * 3;
      bestD2 = std::min(bestD2, d2Segment(st.userPolylineVerts[A], st.userPolylineVerts[A + 1],
                                          st.userPolylineVerts[A + 2], st.userPolylineVerts[B],
                                          st.userPolylineVerts[B + 1], st.userPolylineVerts[B + 2]));
    }
    consider(e, bestD2);
  }

  // Feature lines (REQ-087) — identical segment walk, through the same `consider` funnel, which is
  // what makes a feature line compete with every other entity on distance rather than being picked
  // by a separate rule. ADR-034 relies on this being the single funnel; so does this.
  {
    const int nFl =
        static_cast<int>(st.featureLineOffsets.size() > 0 ? st.featureLineOffsets.size() - 1 : 0);
    const auto& FV = st.featureLineVerts;
    for (int fi = 0; fi < nFl; ++fi) {
      const int v0 = st.featureLineOffsets[static_cast<size_t>(fi)];
      const int v1 = st.featureLineOffsets[static_cast<size_t>(fi + 1)];
      const bool closed = static_cast<size_t>(fi) < st.featureLineClosed.size() &&
                          st.featureLineClosed[static_cast<size_t>(fi)];
      SelectedEntity e{};
      e.type = SelectedEntity::Type::FeatureLine;
      e.index = fi;
      double bestD2 = 1e300;
      for (int vi = v0; vi + 1 < v1; ++vi) {
        const size_t A = static_cast<size_t>(vi) * 3, B = static_cast<size_t>(vi + 1) * 3;
        if (B + 2 >= FV.size())
          break;
        bestD2 = std::min(bestD2, d2Segment(FV[A], FV[A + 1], FV[A + 2], FV[B], FV[B + 1], FV[B + 2]));
      }
      if (closed && v1 - v0 >= 2) {
        const size_t A = static_cast<size_t>(v1 - 1) * 3, B = static_cast<size_t>(v0) * 3;
        if (A + 2 < FV.size() && B + 2 < FV.size())
          bestD2 = std::min(bestD2, d2Segment(FV[A], FV[A + 1], FV[A + 2], FV[B], FV[B + 1], FV[B + 2]));
      }
      consider(e, bestD2);
    }
  }

  // TIN surfaces (REQ-068 / ADR-036 (b)) — a click anywhere near a triangle edge selects the WHOLE
  // surface, so this competes on distance through the same `consider` funnel as everything above.
  //
  // **Layer and isolation are filtered here, not in `consider`.** Every other kind reaches
  // `consider` and is gated there by `CadSelectedEntityHidden`; a surface is gated one level earlier
  // by `SurfaceVisible`, because that predicate also carries the layer rule the renderer applies to
  // surfaces (REQ-068) and which `consider` does not know about. Filtering the whole surface once is
  // also what keeps a hidden 200k-triangle surface from costing a full walk per frame.
  //
  // **The per-triangle plan-AABB reject is load-bearing, not a micro-optimisation.** Hover runs this
  // every frame, and REQ-100's surface profile is ~200k triangles = 600k edges. Four compares that
  // discard a triangle before three distance computations is the difference between a pick that fits
  // the frame budget and one that does not. Under a ray (an orbited camera) the plan AABB does not
  // bound the ray, so the reject is skipped rather than made approximately correct — a pick that
  // silently misses is worse than a slow one.
  for (size_t si = 0; si < st.cadSurfaces.size(); ++si) {
    if (!SurfaceVisible(st, si))
      continue;
    const CadTin& t = *st.cadSurfaces[si].tin;
    const std::vector<float>& V = t.vertsXyz;
    SelectedEntity e{};
    e.type = SelectedEntity::Type::Surface;
    e.index = static_cast<int>(si);
    double bestD2 = 1e300;
    for (size_t i = 0; i + 2 < t.indices.size(); i += 3) {
      const size_t a = static_cast<size_t>(t.indices[i]) * 3;
      const size_t b = static_cast<size_t>(t.indices[i + 1]) * 3;
      const size_t c = static_cast<size_t>(t.indices[i + 2]) * 3;
      if (a + 2 >= V.size() || b + 2 >= V.size() || c + 2 >= V.size())
        break;
      if (!useRay) {
        const double lo_x = std::min(std::min(V[a], V[b]), V[c]);
        const double hi_x = std::max(std::max(V[a], V[b]), V[c]);
        const double lo_y = std::min(std::min(V[a + 1], V[b + 1]), V[c + 1]);
        const double hi_y = std::max(std::max(V[a + 1], V[b + 1]), V[c + 1]);
        if (wx < lo_x - tolWorld || wx > hi_x + tolWorld || wy < lo_y - tolWorld || wy > hi_y + tolWorld)
          continue;
      }
      bestD2 = std::min(bestD2, d2Segment(V[a], V[a + 1], V[a + 2], V[b], V[b + 1], V[b + 2]));
      bestD2 = std::min(bestD2, d2Segment(V[b], V[b + 1], V[b + 2], V[c], V[c + 1], V[c + 2]));
      bestD2 = std::min(bestD2, d2Segment(V[c], V[c + 1], V[c + 2], V[a], V[a + 1], V[a + 2]));
    }
    consider(e, bestD2);
  }

  if (!any)
    return false;
  *out = bestE;
  *outDistSq = static_cast<float>(best);
  return true;
}

bool CadFilledRegionContainsPoint(const CadFilledRegion& fr, double x, double y) {
  return hatchgeom::ContainsPoint(fr, x, y);
}

int PickFilledRegionAt(const AppCommandState& st, double wx, double wy) {
  int best = -1;
  double bestArea = 0.0;
  for (size_t i = 0; i < st.cadFilledRegions.size(); ++i) {
    // REQ-084 (d): an isolated-out fill is invisible and must not answer a click.
    if (!st.hiddenEntityIds.empty() && i < st.cadFilledRegionAttrs.size() &&
        CadEntityIdHidden(&st.hiddenEntityIds, st.cadFilledRegionAttrs[i].id))
      continue;
    if (!hatchgeom::ContainsPoint(st.cadFilledRegions[i], wx, wy))
      continue;
    const double area = hatchgeom::OuterAreaAbs(st.cadFilledRegions[i]);
    if (best < 0 || area < bestArea) {
      best = static_cast<int>(i);
      bestArea = area;
    }
  }
  return best;
}

void StartHatchCommand(AppCommandState& st, std::vector<std::string>& log) {
  st.active = AppCommandState::Kind::Hatch;
  st.lastCommand = AppCommandState::Kind::Hatch;
  st.hatchPreviewValid = false;
  st.hatchPreviewLoop.clear();
  ClearCadSelection(st);
  log.push_back("HATCH — pick an internal point inside a closed area (Esc to cancel).");
}

// Gather every model boundary segment into \p out (local coordinates): line segments, polyline edges
// (closing edge when closed), and tessellated arcs/circles/ellipses. Used by the HATCH boundary trace.
static void CadCollectBoundarySegments(const AppCommandState& st, std::vector<hatchboundary::Seg>* out) {
  const auto& L = st.userLinesFlat;
  for (size_t i = 0; i + 5 < L.size(); i += 6)
    out->push_back({L[i], L[i + 1], L[i + 3], L[i + 4]});

  const int nPoly = static_cast<int>(st.userPolylineOffsets.size() > 0 ? st.userPolylineOffsets.size() - 1 : 0);
  for (int pi = 0; pi < nPoly; ++pi) {
    const int v0 = st.userPolylineOffsets[static_cast<size_t>(pi)];
    const int v1 = st.userPolylineOffsets[static_cast<size_t>(pi + 1)];
    for (int vi = v0; vi + 1 < v1; ++vi)
      out->push_back({st.userPolylineVerts[static_cast<size_t>(vi * 3)],
                      st.userPolylineVerts[static_cast<size_t>(vi * 3 + 1)],
                      st.userPolylineVerts[static_cast<size_t>((vi + 1) * 3)],
                      st.userPolylineVerts[static_cast<size_t>((vi + 1) * 3 + 1)]});
    const bool closed =
        static_cast<size_t>(pi) < st.userPolylineClosed.size() && st.userPolylineClosed[static_cast<size_t>(pi)];
    if (closed && v1 - v0 >= 2)
      out->push_back({st.userPolylineVerts[static_cast<size_t>((v1 - 1) * 3)],
                      st.userPolylineVerts[static_cast<size_t>((v1 - 1) * 3 + 1)],
                      st.userPolylineVerts[static_cast<size_t>(v0 * 3)],
                      st.userPolylineVerts[static_cast<size_t>(v0 * 3 + 1)]});
  }

  auto tessellate = [&](auto pointAt, int n) {
    float px = 0.f, py = 0.f;
    for (int i = 0; i <= n; ++i) {
      float x = 0.f, y = 0.f;
      pointAt(i, &x, &y);
      if (i > 0)
        out->push_back({px, py, x, y});
      px = x;
      py = y;
    }
  };
  const auto& C = st.userCirclesCxCyZR;
  for (size_t ci = 0; ci + 3 < C.size(); ci += 4) {
    const float cx = C[ci], cy = C[ci + 1], r = C[ci + 3];
    tessellate([&](int i, float* x, float* y) {
      const double a = 6.283185307179586 * i / 48.0;
      *x = cx + r * static_cast<float>(std::cos(a));
      *y = cy + r * static_cast<float>(std::sin(a));
    }, 48);
  }
  for (const CadArc& a : st.userArcs) {
    tessellate([&](int i, float* x, float* y) {
      const double t = static_cast<double>(a.startRad) + static_cast<double>(a.sweepRad) * (i / 48.0);
      *x = a.cx + a.r * static_cast<float>(std::cos(t));
      *y = a.cy + a.r * static_cast<float>(std::sin(t));
    }, 48);
  }
  for (const CadEllipse& el : st.userEllipses) {
    const double ma = std::hypot(static_cast<double>(el.majVx), static_cast<double>(el.majVy));
    if (ma < 1e-8)
      continue;
    const double ux = el.majVx / ma, uy = el.majVy / ma;
    const double mb = ma * static_cast<double>(el.ratio);
    tessellate([&](int i, float* x, float* y) {
      const double t = 6.283185307179586 * i / 64.0;
      const double cc = std::cos(t), ss = std::sin(t);
      *x = el.cx + static_cast<float>(ux * ma * cc - uy * mb * ss);
      *y = el.cy + static_cast<float>(uy * ma * cc + ux * mb * ss);
    }, 64);
  }
}

bool CadHatchTraceAt(const AppCommandState& st, double wx, double wy, std::vector<float>* outLoop) {
  if (!outLoop)
    return false;
  std::vector<hatchboundary::Seg> segs;
  CadCollectBoundarySegments(st, &segs);
  return hatchboundary::TraceEnclosingLoop(segs, wx, wy, outLoop);
}

bool CadHatchCommitLoop(AppCommandState& st, const std::vector<float>& loop, std::vector<std::string>& log) {
  if (loop.size() < 6)
    return false;
  PushUndoSnapshot(st, "Hatch");
  CadFilledRegion fr;
  // \p loop is the traced boundary as flat local x,y pairs; the store is interleaved x,y,z
  // (ADR-025 (a)). The work plane exists now, so the region lands on it like every other created
  // entity (REQ-058) rather than always on the datum. The tracer works in XY and returns no
  // elevations, so all vertices share one Z — a hatch is planar anyway.
  const float hatchZ = CadCommitElevation(st);
  fr.vertsXyz.reserve(loop.size() / 2 * 3);
  for (size_t i = 0; i + 1 < loop.size(); i += 2) {
    fr.vertsXyz.push_back(loop[i + 0]);
    fr.vertsXyz.push_back(loop[i + 1]);
    fr.vertsXyz.push_back(hatchZ);
  }
  fr.loopStart = {0};
  fr.patternName = st.hatchPatternName;  // "" / "SOLID" = solid; else a line pattern (ADR-018)
  fr.patternAngleDeg = st.hatchAngleDeg;
  fr.patternScale = std::max(0.01f, st.hatchScale);
  EntityAttributes at = MakeNewEntityAttrs(st);
  if (!st.hatchLayer.empty())
    at.layer = st.hatchLayer;
  char hex[8];
  std::snprintf(hex, sizeof(hex), "#%02X%02X%02X", static_cast<int>(std::lround(st.hatchColorRgb[0] * 255.f)),
                static_cast<int>(std::lround(st.hatchColorRgb[1] * 255.f)),
                static_cast<int>(std::lround(st.hatchColorRgb[2] * 255.f)));
  at.color = hex;
  at.transparency = std::clamp(st.hatchTransparency01, 0.f, 1.f);
  st.cadFilledRegions.push_back(std::move(fr));
  st.cadFilledRegionAttrs.push_back(at);
  ClearCadSelection(st);
  st.selection.push_back({SelectedEntity::Type::FilledRegion,
                          static_cast<int>(st.cadFilledRegions.size()) - 1});
  BumpCadGpuCache(st);
  log.push_back("HATCH — solid fill placed.");
  return true;
}

namespace {

static void OfsClosestPtSeg(float ax, float ay, float bx, float by, float px, float py, float* qx, float* qy) {
  const float vx = bx - ax;
  const float vy = by - ay;
  const float len2 = vx * vx + vy * vy;
  if (len2 < 1e-18f) {
    *qx = ax;
    *qy = ay;
    return;
  }
  const float t = std::clamp(((px - ax) * vx + (py - ay) * vy) / len2, 0.f, 1.f);
  *qx = ax + t * vx;
  *qy = ay + t * vy;
}

static void OfsUnitLeftNormal(float ax, float ay, float bx, float by, float* nx, float* ny) {
  float vx = bx - ax;
  float vy = by - ay;
  const float len = std::hypot(vx, vy);
  if (len < 1e-12f) {
    *nx = 0.f;
    *ny = 1.f;
    return;
  }
  vx /= len;
  vy /= len;
  *nx = -vy;
  *ny = vx;
}

static float OfsSignedSideLine(float ax, float ay, float bx, float by, float px, float py) {
  float qx = 0.f, qy = 0.f;
  OfsClosestPtSeg(ax, ay, bx, by, px, py, &qx, &qy);
  float nx = 0.f, ny = 0.f;
  OfsUnitLeftNormal(ax, ay, bx, by, &nx, &ny);
  return (px - qx) * nx + (py - qy) * ny;
}

static float OfsSignedSideCircle(float cx, float cy, float r, float px, float py) {
  const float d = std::hypot(px - cx, py - cy);
  return d - r;
}

static bool OfsLineLineIntersectInf(float ax, float ay, float bx, float by, float cx, float cy, float dx, float dy,
                                    float* ox, float* oy) {
  const float rx = bx - ax, ry = by - ay;
  const float sx = dx - cx, sy = dy - cy;
  const float det = rx * sy - ry * sx;
  if (std::fabs(det) < 1e-12f * std::max(1.f, std::hypot(rx, ry) * std::hypot(sx, sy)))
    return false;
  const float t = ((cx - ax) * sy - (cy - ay) * sx) / det;
  *ox = ax + t * rx;
  *oy = ay + t * ry;
  return true;
}

static bool TryOffsetSignedDFromCursor(const AppCommandState& st, float px, float py, float* signedDOut) {
  using OP = AppCommandState::OffsetPhase;
  using T = SelectedEntity::Type;
  if (!st.offsetEntityValid)
    return false;
  const SelectedEntity& e = st.offsetEntity;
  if (st.offsetPhase == OP::WaitSidePick) {
    if (st.offsetTypedDistance <= 0.f)
      return false;
    const float d = st.offsetTypedDistance;
    float sgn = 1.f;
    switch (e.type) {
    case T::LineSeg: {
      const size_t k = static_cast<size_t>(e.index) * 6;
      if (k + 5 >= st.userLinesFlat.size())
        return false;
      const float sd = OfsSignedSideLine(st.userLinesFlat[k], st.userLinesFlat[k + 1], st.userLinesFlat[k + 3],
                                         st.userLinesFlat[k + 4], px, py);
      sgn = sd >= 0.f ? 1.f : -1.f;
      break;
    }
    case T::Circle: {
      const size_t k = static_cast<size_t>(e.index) * 4;
      if (k + 3 >= st.userCirclesCxCyZR.size())
        return false;
      const float cx = st.userCirclesCxCyZR[k];
      const float cy = st.userCirclesCxCyZR[k + 1];
      const float r = st.userCirclesCxCyZR[k + 3];
      const float side = OfsSignedSideCircle(cx, cy, r, px, py);
      sgn = side >= 0.f ? 1.f : -1.f;
      break;
    }
    case T::Arc: {
      if (e.index < 0 || static_cast<size_t>(e.index) >= st.userArcs.size())
        return false;
      const CadArc& a = st.userArcs[static_cast<size_t>(e.index)];
      const float side = OfsSignedSideCircle(a.cx, a.cy, a.r, px, py);
      sgn = side >= 0.f ? 1.f : -1.f;
      break;
    }
    case T::Ellipse:
    case T::Polyline: {
      if (e.type == T::Polyline) {
        const int pi = e.index;
        if (pi >= 0 && static_cast<size_t>(pi + 1) < st.userPolylineOffsets.size()) {
          const int v0 = st.userPolylineOffsets[static_cast<size_t>(pi)];
          const int v1 = st.userPolylineOffsets[static_cast<size_t>(pi + 1)];
          float best = 1e30f;
          float bestS = 1.f;
          for (int vi = v0; vi + 1 < v1; ++vi) {
            const float ax = st.userPolylineVerts[static_cast<size_t>(vi * 3)];
            const float ay = st.userPolylineVerts[static_cast<size_t>(vi * 3 + 1)];
            const float bx = st.userPolylineVerts[static_cast<size_t>((vi + 1) * 3)];
            const float by = st.userPolylineVerts[static_cast<size_t>((vi + 1) * 3 + 1)];
            float qx = 0.f, qy = 0.f;
            OfsClosestPtSeg(ax, ay, bx, by, px, py, &qx, &qy);
            const float sd = OfsSignedSideLine(ax, ay, bx, by, px, py);
            const float dx = px - qx;
            const float dy = py - qy;
            const float dist2 = dx * dx + dy * dy;
            if (dist2 < best) {
              best = dist2;
              bestS = sd >= 0.f ? 1.f : -1.f;
            }
          }
          sgn = bestS;
        }
      } else if (e.index >= 0 && static_cast<size_t>(e.index) < st.userEllipses.size()) {
        const CadEllipse& el = st.userEllipses[static_cast<size_t>(e.index)];
        const float ma = std::hypot(el.majVx, el.majVy);
        if (ma >= 1e-8f) {
          const float ux = el.majVx / ma;
          const float uy = el.majVy / ma;
          const float pxn = -uy;
          const float pyn = ux;
          const float mb = ma * el.ratio;
          constexpr float twopi = 6.28318530718f;
          float best = 1e30f;
          float bx = el.cx, by = el.cy;
          for (int i = 0; i <= 48; ++i) {
            const float ang = twopi * static_cast<float>(i) / 48.f;
            const float c0 = std::cos(ang);
            const float s0 = std::sin(ang);
            const float ex = el.cx + ux * (ma * c0) + pxn * (mb * s0);
            const float ey = el.cy + uy * (ma * c0) + pyn * (mb * s0);
            const float dx = px - ex;
            const float dy = py - ey;
            const float dist2 = dx * dx + dy * dy;
            if (dist2 < best) {
              best = dist2;
              bx = ex;
              by = ey;
            }
          }
          const float ox = bx - el.cx;
          const float oy = by - el.cy;
          const float inX = px - el.cx;
          const float inY = py - el.cy;
          sgn = (inX * ox + inY * oy) >= 0.f ? 1.f : -1.f;
        }
      }
      break;
    }
    default:
      return false;
    }
    *signedDOut = d * sgn;
    return true;
  }
  if (st.offsetPhase == OP::WaitDistanceOrThrough) {
    float signedD = 0.f;
    switch (e.type) {
    case T::LineSeg: {
      const size_t k = static_cast<size_t>(e.index) * 6;
      if (k + 5 >= st.userLinesFlat.size())
        return false;
      signedD = OfsSignedSideLine(st.userLinesFlat[k], st.userLinesFlat[k + 1], st.userLinesFlat[k + 3],
                                  st.userLinesFlat[k + 4], px, py);
      break;
    }
    case T::Circle: {
      const size_t k = static_cast<size_t>(e.index) * 4;
      if (k + 3 >= st.userCirclesCxCyZR.size())
        return false;
      signedD = OfsSignedSideCircle(st.userCirclesCxCyZR[k], st.userCirclesCxCyZR[k + 1], st.userCirclesCxCyZR[k + 3], px,
                                  py);
      break;
    }
    case T::Arc: {
      if (e.index < 0 || static_cast<size_t>(e.index) >= st.userArcs.size())
        return false;
      const CadArc& a = st.userArcs[static_cast<size_t>(e.index)];
      signedD = OfsSignedSideCircle(a.cx, a.cy, a.r, px, py);
      break;
    }
    default:
      return false;
    }
    if (std::fabs(signedD) < 1e-7f)
      return false;
    *signedDOut = signedD;
    return true;
  }
  return false;
}

static void AppendArcPreviewStrip(float z, const CadArc& a, std::vector<float>* lines) {
  AppendArcLineSegments(*lines, static_cast<double>(a.cx), static_cast<double>(a.cy), static_cast<double>(a.r),
                        static_cast<double>(a.startRad), static_cast<double>(a.sweepRad), 56, z);
}

static void AppendEllipsePreviewStrip(float z, const CadEllipse& el, std::vector<float>* lines) {
  AppendEllipseLineSegments(*lines, static_cast<double>(el.cx), static_cast<double>(el.cy),
                            static_cast<double>(el.majVx), static_cast<double>(el.majVy),
                            static_cast<double>(el.ratio), 56, z);
}

static void AppendPolylineOffsetPreview(const AppCommandState& st, int pi, float signedD, float z,
                                        std::vector<float>* lines) {
  if (pi < 0 || static_cast<size_t>(pi + 1) >= st.userPolylineOffsets.size())
    return;
  const int v0 = st.userPolylineOffsets[static_cast<size_t>(pi)];
  const int v1 = st.userPolylineOffsets[static_cast<size_t>(pi + 1)];
  const int nv = v1 - v0;
  if (nv < 2)
    return;
  const bool closed =
      static_cast<size_t>(pi) < st.userPolylineClosed.size() && st.userPolylineClosed[static_cast<size_t>(pi)];

  std::vector<std::pair<float, float>> v;
  v.reserve(static_cast<size_t>(nv));
  for (int i = v0; i < v1; ++i)
    v.push_back({st.userPolylineVerts[static_cast<size_t>(i * 3)], st.userPolylineVerts[static_cast<size_t>(i * 3 + 1)]});

  const int n = static_cast<int>(v.size());
  const int nEdges = closed ? n : n - 1;
  if (nEdges < 1)
    return;

  std::vector<std::pair<float, float>> pa(static_cast<size_t>(nEdges)), pb(static_cast<size_t>(nEdges));
  for (int ei = 0; ei < nEdges; ++ei) {
    const int ia = ei;
    const int ib = closed ? (ei + 1) % n : ei + 1;
    const float ax = v[static_cast<size_t>(ia)].first;
    const float ay = v[static_cast<size_t>(ia)].second;
    const float bx = v[static_cast<size_t>(ib)].first;
    const float by = v[static_cast<size_t>(ib)].second;
    float nx = 0.f, ny = 0.f;
    OfsUnitLeftNormal(ax, ay, bx, by, &nx, &ny);
    pa[static_cast<size_t>(ei)] = {ax + nx * signedD, ay + ny * signedD};
    pb[static_cast<size_t>(ei)] = {bx + nx * signedD, by + ny * signedD};
  }

  std::vector<std::pair<float, float>> out;
  if (!closed) {
    if (nEdges == 1) {
      out.push_back(pa[0]);
      out.push_back(pb[0]);
    } else {
      out.push_back(pa[0]);
      for (int ei = 0; ei < nEdges - 1; ++ei) {
        const auto& a0 = pa[static_cast<size_t>(ei)];
        const auto& b0 = pb[static_cast<size_t>(ei)];
        const auto& a1 = pa[static_cast<size_t>(ei + 1)];
        const auto& b1 = pb[static_cast<size_t>(ei + 1)];
        float ix = 0.f, iy = 0.f;
        if (OfsLineLineIntersectInf(a0.first, a0.second, b0.first, b0.second, a1.first, a1.second, b1.first, b1.second,
                                    &ix, &iy))
          out.push_back({ix, iy});
        else {
          out.push_back({0.5f * (b0.first + a1.first), 0.5f * (b0.second + a1.second)});
        }
      }
      out.push_back(pb[static_cast<size_t>(nEdges - 1)]);
    }
  } else {
    out.resize(static_cast<size_t>(nEdges));
    for (int ei = 0; ei < nEdges; ++ei) {
      const int en = (ei + 1) % nEdges;
      const auto& a0 = pa[static_cast<size_t>(ei)];
      const auto& b0 = pb[static_cast<size_t>(ei)];
      const auto& a1 = pa[static_cast<size_t>(en)];
      const auto& b1 = pb[static_cast<size_t>(en)];
      float ix = 0.f, iy = 0.f;
      if (OfsLineLineIntersectInf(a0.first, a0.second, b0.first, b0.second, a1.first, a1.second, b1.first, b1.second,
                                  &ix, &iy))
        out[static_cast<size_t>(ei)] = {ix, iy};
      else
        out[static_cast<size_t>(ei)] = {0.5f * (b0.first + a1.first), 0.5f * (b0.second + a1.second)};
    }
  }
  if (out.size() < 2)
    return;
  for (size_t i = 0; i + 1 < out.size(); ++i) {
    lines->push_back(out[i].first);
    lines->push_back(out[i].second);
    lines->push_back(z);
    lines->push_back(out[i + 1].first);
    lines->push_back(out[i + 1].second);
    lines->push_back(z);
  }
  if (closed && out.size() >= 2) {
    lines->push_back(out.back().first);
    lines->push_back(out.back().second);
    lines->push_back(z);
    lines->push_back(out[0].first);
    lines->push_back(out[0].second);
    lines->push_back(z);
  }
}

} // namespace

float CadOffsetEntityPickTolWorld(const AppCommandState& st) {
  double mnX = 0.;
  double mxX = 0.;
  double mnY = 0.;
  double mxY = 0.;
  float geom = 1e-3f;
  // Use the robust (outlier-trimmed) extent: stray entities sitting millions of feet away — common in
  // Civil 3D state-plane DXFs — would otherwise inflate this scale floor into a tens-of-feet "magnetic"
  // pick radius that highlights lines the cursor is nowhere near.
  int skipped = 0;
  if (ComputeRobustWorldExtents(st, &mnX, &mxX, &mnY, &mxY, &skipped))
    geom = std::max(1e-5f, static_cast<float>(2.5e-5 * std::max(mxX - mnX, mxY - mnY)));
  const float px = CadSnap::WorldToleranceFromPixels(st.viewportLastSurveyLayoutHeightPx,
                                                     st.viewportLastSurveyLayoutOrthoHalfH, st.objectSnapAperturePx);
  return std::max(geom, px * 1.5f);
}

float CadHoverEntityPickTolWorld(const AppCommandState& st) {
  // Idle hover highlight is pure visual feedback, so it must require the cursor to actually touch the drawn
  // stroke — unlike OFFSET/selection picking, which keeps a forgiving aperture. A small fixed pixel aperture
  // (no scale floor) means the tolerance is constant in screen space at every zoom level.
  constexpr float kHoverAperturePx = 3.0f;
  return CadSnap::WorldToleranceFromPixels(st.viewportLastSurveyLayoutHeightPx,
                                           st.viewportLastSurveyLayoutOrthoHalfH, kHoverAperturePx);
}

void CadOffsetAppendLivePreview(const AppCommandState& cmd, float cursorWx, float cursorWy,
                                std::vector<float>* previewLines, std::vector<float>* previewCircles) {
  if (!previewLines || !previewCircles)
    return;
  previewLines->clear();
  previewCircles->clear();
  if (!cmd.offsetEntityValid)
    return;
  float signedD = 0.f;
  if (!TryOffsetSignedDFromCursor(cmd, cursorWx, cursorWy, &signedD))
    return;

  using T = SelectedEntity::Type;
  const SelectedEntity& e = cmd.offsetEntity;
  constexpr float zl = 0.022f;

  switch (e.type) {
  case T::LineSeg: {
    const size_t k = static_cast<size_t>(e.index) * 6;
    if (k + 5 >= cmd.userLinesFlat.size())
      return;
    const float x0 = cmd.userLinesFlat[k];
    const float y0 = cmd.userLinesFlat[k + 1];
    const float x1 = cmd.userLinesFlat[k + 3];
    const float y1 = cmd.userLinesFlat[k + 4];
    const float dx = x1 - x0;
    const float dy = y1 - y0;
    if (std::hypot(dx, dy) < 1e-8f)
      return;
    float nx = 0.f, ny = 0.f;
    OfsUnitLeftNormal(x0, y0, x1, y1, &nx, &ny);
    previewLines->push_back(x0 + nx * signedD);
    previewLines->push_back(y0 + ny * signedD);
    previewLines->push_back(zl);
    previewLines->push_back(x1 + nx * signedD);
    previewLines->push_back(y1 + ny * signedD);
    previewLines->push_back(zl);
    break;
  }
  case T::Circle: {
    const size_t k = static_cast<size_t>(e.index) * 4;
    if (k + 3 >= cmd.userCirclesCxCyZR.size())
      return;
    const float cx = cmd.userCirclesCxCyZR[k];
    const float cy = cmd.userCirclesCxCyZR[k + 1];
    const float r = cmd.userCirclesCxCyZR[k + 3];
    const float nr = r + signedD;
    if (nr <= 1e-6f)
      return;
    previewCircles->push_back(cx);
    previewCircles->push_back(cy);
    previewCircles->push_back(nr);
    break;
  }
  case T::Arc: {
    if (e.index < 0 || static_cast<size_t>(e.index) >= cmd.userArcs.size())
      return;
    CadArc o = cmd.userArcs[static_cast<size_t>(e.index)];
    o.r += signedD;
    if (o.r <= 1e-6f)
      return;
    AppendArcPreviewStrip(zl, o, previewLines);
    break;
  }
  case T::Ellipse: {
    if (e.index < 0 || static_cast<size_t>(e.index) >= cmd.userEllipses.size())
      return;
    const CadEllipse& el0 = cmd.userEllipses[static_cast<size_t>(e.index)];
    const float ma = std::hypot(el0.majVx, el0.majVy);
    if (ma < 1e-8f || ma + signedD <= 1e-6f)
      return;
    CadEllipse el = el0;
    const float f = (ma + signedD) / ma;
    el.majVx *= f;
    el.majVy *= f;
    AppendEllipsePreviewStrip(zl, el, previewLines);
    break;
  }
  case T::Polyline:
    AppendPolylineOffsetPreview(cmd, e.index, signedD, zl, previewLines);
    break;
  default:
    break;
  }
}

void CadTrimAppendCutLineRemovedPreview(const AppCommandState& st, float fenceP1x, float fenceP1y, float fenceP2x,
                                        float fenceP2y, float pickPreviewX, float pickPreviewY,
                                        std::vector<float>* previewLinesOut) {
  if (!previewLinesOut)
    return;

  const auto pushRemoved = [&](const TrimTargetEdge& tgt, float ax, float ay, float bx, float by) {
    std::vector<std::array<float, 4>> cuts;
    CollectAllDrawingCutSegmentsExceptTarget(st, &tgt, &cuts);
    if (cuts.empty())
      return;
    float ix = 0.f, iy = 0.f;
    bool trimA = false;
    if (!TrimSegmentIntersectPickSide(ax, ay, bx, by, pickPreviewX, pickPreviewY, cuts, st, fenceP1x, fenceP1y,
                                      fenceP2x, fenceP2y, true, &ix, &iy, &trimA, nullptr))
      return;
    if (trimA) {
      previewLinesOut->push_back(ax);
      previewLinesOut->push_back(ay);
      previewLinesOut->push_back(0.f);
      previewLinesOut->push_back(ix);
      previewLinesOut->push_back(iy);
      previewLinesOut->push_back(0.f);
    } else {
      previewLinesOut->push_back(ix);
      previewLinesOut->push_back(iy);
      previewLinesOut->push_back(0.f);
      previewLinesOut->push_back(bx);
      previewLinesOut->push_back(by);
      previewLinesOut->push_back(0.f);
    }
  };

  const auto& Lf = st.userLinesFlat;
  if (Lf.size() % 6 == 0) {
    for (size_t li = 0; li + 5 < Lf.size(); li += 6) {
      TrimTargetEdge tgt{};
      tgt.kind = TrimTargetEdge::Line;
      tgt.lineIx = static_cast<int>(li / 6);
      pushRemoved(tgt, Lf[li], Lf[li + 1], Lf[li + 3], Lf[li + 4]);
    }
  }

  const int nPoly =
      static_cast<int>(st.userPolylineOffsets.size() > 0 ? st.userPolylineOffsets.size() - 1 : 0);
  for (int pi = 0; pi < nPoly; ++pi) {
    const int v0 = st.userPolylineOffsets[static_cast<size_t>(pi)];
    const int v1 = st.userPolylineOffsets[static_cast<size_t>(pi + 1)];
    const bool closed =
        static_cast<size_t>(pi) < st.userPolylineClosed.size() && st.userPolylineClosed[static_cast<size_t>(pi)];
    for (int vi = v0; vi + 1 < v1; ++vi) {
      TrimTargetEdge tgt{};
      tgt.kind = TrimTargetEdge::Poly;
      tgt.polyIx = pi;
      tgt.vLo = vi;
      const float ax = st.userPolylineVerts[static_cast<size_t>(vi * 3)];
      const float ay = st.userPolylineVerts[static_cast<size_t>(vi * 3 + 1)];
      const float bx = st.userPolylineVerts[static_cast<size_t>((vi + 1) * 3)];
      const float by = st.userPolylineVerts[static_cast<size_t>((vi + 1) * 3 + 1)];
      pushRemoved(tgt, ax, ay, bx, by);
    }
    if (closed && v1 - v0 >= 2) {
      TrimTargetEdge tgt{};
      tgt.kind = TrimTargetEdge::Poly;
      tgt.polyIx = pi;
      tgt.vLo = v1 - 1;
      const float ax = st.userPolylineVerts[static_cast<size_t>((v1 - 1) * 3)];
      const float ay = st.userPolylineVerts[static_cast<size_t>((v1 - 1) * 3 + 1)];
      const float bx = st.userPolylineVerts[static_cast<size_t>(v0 * 3)];
      const float by = st.userPolylineVerts[static_cast<size_t>(v0 * 3 + 1)];
      pushRemoved(tgt, ax, ay, bx, by);
    }
  }
}

static void ExecuteDrawnSegmentTrimOnce(AppCommandState& st, float p1x, float p1y, float p2x, float p2y,
                                        float tolWorld, std::vector<std::string>& log) {
  float matchTol = std::max(tolWorld * 4.f, 1e-6f);
  double mnX = 0.;
  double mxX = 0.;
  double mnY = 0.;
  double mxY = 0.;
  if (ComputeWorldExtents(st, &mnX, &mxX, &mnY, &mxY))
    matchTol = std::max(matchTol, static_cast<float>(2e-5 * std::max(mxX - mnX, mxY - mnY)));
  TrimTargetEdge tgt{};
  float ax = 0.f, ay = 0.f, bx = 0.f, by = 0.f, dEdge = 0.f;
  if (!PickTrimTargetClosestToDrawnSegment(st, p1x, p1y, p2x, p2y, matchTol, &tgt, &ax, &ay, &bx, &by, &dEdge)) {
    log.push_back("TRIM — no segment close enough to your line (draw along the edge to shorten).");
    return;
  }
  std::vector<std::array<float, 4>> cuts;
  CollectAllDrawingCutSegmentsExceptTarget(st, &tgt, &cuts);
  if (cuts.empty()) {
    log.push_back("TRIM — nothing crosses that segment.");
    return;
  }
  const float pmx = (p1x + p2x) * 0.5f;
  const float pmy = (p1y + p2y) * 0.5f;
  PushUndoSnapshot(st, "Trim");
  if (!TrimSegmentToCuttingEdges(st, tgt, ax, ay, bx, by, pmx, pmy, cuts, true, p1x, p1y, p2x, p2y, log))
    return;
  BumpCadGpuCache(st);
}

bool SubmitTrimViewportPick(AppCommandState& st, float wx, float wy, float tolWorld, std::vector<std::string>& log) {
  ClearPendingOneShotObjectSnap(st);
  using K = AppCommandState::Kind;
  using TP = AppCommandState::TrimPhase;
  if (st.active != K::Trim)
    return false;

  if (st.trimPhase == TP::CuttingLine_WaitP1) {
    st.trimCutInfP1x = wx;
    st.trimCutInfP1y = wy;
    st.trimPhase = TP::CuttingLine_WaitP2;
    log.push_back("TRIM — second point: finishes trim on nearest edge along your line (dashed preview).");
    return true;
  }

  if (st.trimPhase == TP::CuttingLine_WaitP2) {
    float p2x = wx;
    float p2y = wy;
    if (st.orthoMode) {
      const float dx = p2x - st.trimCutInfP1x;
      const float dy = p2y - st.trimCutInfP1y;
      if (std::fabs(dx) >= std::fabs(dy))
        p2y = st.trimCutInfP1y;
      else
        p2x = st.trimCutInfP1x;
    }
    const float ddx = p2x - st.trimCutInfP1x;
    const float ddy = p2y - st.trimCutInfP1y;
    if (ddx * ddx + ddy * ddy < 1e-18f) {
      log.push_back("TRIM — line too short.");
      return false;
    }
    st.trimCutInfP2x = p2x;
    st.trimCutInfP2y = p2y;
    ExecuteDrawnSegmentTrimOnce(st, st.trimCutInfP1x, st.trimCutInfP1y, p2x, p2y, tolWorld, log);
    st.trimCutters.clear();
    st.trimPhase = TP::SelectCuttingEdges;
    st.active = K::None;
    return true;
  }

  if (st.trimPhase == TP::SelectCuttingEdges) {
    SelectedEntity hit{};
    float d2 = 0.f;
    if (!PickClosestCadEntity(st, wx, wy, tolWorld, &hit, &d2)) {
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

  TrimTargetEdge tgt{};
  float ax = 0.f, ay = 0.f, bx = 0.f, by = 0.f, d2 = 0.f;
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
    else
      log.push_back("TRIM — nothing to trim at pick.");
    return false;
  }

  std::vector<std::array<float, 4>> cuts;
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

void ExecuteJoinSelection(AppCommandState& st, std::vector<std::string>& log) {
  PushUndoSnapshot(st, "Join");
  using ST = SelectedEntity::Type;

  // REQ-087 / REQ-201. Feature lines in the selection are skipped by the edge walk below, and until
  // now that skip was silent — the user would see "JOIN — 2 edges joined" on a selection of three
  // objects and have no way to learn which one was left out. Joining two feature lines is not
  // merely unimplemented: it is undefined, because the shared endpoint would need one elevation and
  // the two lines each supply their own, and nothing in REQ-087 or REQ-088 says which wins.
  int featureLinesSkipped = 0;
  for (const auto& se : st.selection)
    if (se.type == ST::FeatureLine)
      ++featureLinesSkipped;
  if (featureLinesSkipped > 0)
    log.push_back("JOIN — " + std::to_string(featureLinesSkipped) + " feature line" +
                  (featureLinesSkipped == 1 ? "" : "s") +
                  " ignored: joining feature lines has no defined elevation at the shared end.");

  struct Edge {
    float x0, y0, x1, y1;
    int lineIx;
    int polyIx;
  };
  std::vector<Edge> edges;
  float tol = 1e-3f;
  double mnX = 0.;
  double mxX = 0.;
  double mnY = 0.;
  double mxY = 0.;
  if (ComputeWorldExtents(st, &mnX, &mxX, &mnY, &mxY))
    tol = std::max(1e-5f, static_cast<float>(1e-4 * std::max(mxX - mnX, mxY - mnY)));

  auto readLine = [&](int idx, float* x0, float* y0, float* x1, float* y1) -> bool {
    const size_t k = static_cast<size_t>(idx) * 6;
    if (k + 5 >= st.userLinesFlat.size())
      return false;
    *x0 = st.userLinesFlat[k];
    *y0 = st.userLinesFlat[k + 1];
    *x1 = st.userLinesFlat[k + 3];
    *y1 = st.userLinesFlat[k + 4];
    return true;
  };

  for (const auto& se : st.selection) {
    if (se.type == ST::LineSeg && se.index >= 0) {
      float x0 = 0.f, y0 = 0.f, x1 = 0.f, y1 = 0.f;
      if (!readLine(se.index, &x0, &y0, &x1, &y1))
        continue;
      edges.push_back({x0, y0, x1, y1, se.index, -1});
    } else if (se.type == ST::Polyline && se.index >= 0) {
      const int pi = se.index;
      if (static_cast<size_t>(pi + 1) >= st.userPolylineOffsets.size())
        continue;
      const int v0 = st.userPolylineOffsets[static_cast<size_t>(pi)];
      const int v1 = st.userPolylineOffsets[static_cast<size_t>(pi + 1)];
      const bool closed =
          static_cast<size_t>(pi) < st.userPolylineClosed.size() && st.userPolylineClosed[static_cast<size_t>(pi)];
      for (int vi = v0; vi + 1 < v1; ++vi) {
        const float ax = st.userPolylineVerts[static_cast<size_t>(vi * 3)];
        const float ay = st.userPolylineVerts[static_cast<size_t>(vi * 3 + 1)];
        const float bx = st.userPolylineVerts[static_cast<size_t>((vi + 1) * 3)];
        const float by = st.userPolylineVerts[static_cast<size_t>((vi + 1) * 3 + 1)];
        edges.push_back({ax, ay, bx, by, -1, pi});
      }
      if (closed && v1 - v0 >= 2) {
        const float ax = st.userPolylineVerts[static_cast<size_t>((v1 - 1) * 3)];
        const float ay = st.userPolylineVerts[static_cast<size_t>((v1 - 1) * 3 + 1)];
        const float bx = st.userPolylineVerts[static_cast<size_t>(v0 * 3)];
        const float by = st.userPolylineVerts[static_cast<size_t>(v0 * 3 + 1)];
        edges.push_back({ax, ay, bx, by, -1, pi});
      }
    }
  }

  if (edges.size() < 2) {
    log.push_back("JOIN — select at least two connected lines or polylines.");
    st.selection.clear();
    return;
  }

  const int n = static_cast<int>(edges.size());
  struct UF {
    std::vector<int> p;
    explicit UF(int nn) : p(static_cast<size_t>(nn)) { std::iota(p.begin(), p.end(), 0); }
    int find(int x) {
      return p[static_cast<size_t>(x)] == x ? x : (p[static_cast<size_t>(x)] = find(p[static_cast<size_t>(x)]));
    }
    void unite(int a, int b) {
      a = find(a);
      b = find(b);
      if (a != b)
        p[static_cast<size_t>(a)] = b;
    }
  };
  UF uf(2 * n);
  auto nearPt = [&](float ax, float ay, float bx, float by) {
    const float dx = ax - bx;
    const float dy = ay - by;
    return dx * dx + dy * dy <= tol * tol;
  };
  for (int i = 0; i < n; ++i) {
    for (int j = i + 1; j < n; ++j) {
      const Edge& A = edges[static_cast<size_t>(i)];
      const Edge& B = edges[static_cast<size_t>(j)];
      if (nearPt(A.x0, A.y0, B.x0, B.y0))
        uf.unite(2 * i, 2 * j);
      if (nearPt(A.x0, A.y0, B.x1, B.y1))
        uf.unite(2 * i, 2 * j + 1);
      if (nearPt(A.x1, A.y1, B.x0, B.y0))
        uf.unite(2 * i + 1, 2 * j);
      if (nearPt(A.x1, A.y1, B.x1, B.y1))
        uf.unite(2 * i + 1, 2 * j + 1);
    }
  }

  auto clusterOf = [&](int ep) { return uf.find(ep); };
  std::vector<char> edgeUsed(static_cast<size_t>(n), 0);
  std::unordered_set<int> lineDel;
  std::unordered_set<int> polyDel;
  int polysOut = 0;

  for (int ei = 0; ei < n; ++ei) {
    if (edgeUsed[static_cast<size_t>(ei)])
      continue;
    std::vector<int> comp;
    std::vector<int> stk = {ei};
    edgeUsed[static_cast<size_t>(ei)] = 1;
    while (!stk.empty()) {
      const int cur = stk.back();
      stk.pop_back();
      comp.push_back(cur);
      const int cua = clusterOf(2 * cur);
      const int cub = clusterOf(2 * cur + 1);
      for (int ej = 0; ej < n; ++ej) {
        if (edgeUsed[static_cast<size_t>(ej)])
          continue;
        const int cva = clusterOf(2 * ej);
        const int cvb = clusterOf(2 * ej + 1);
        if (cua == cva || cua == cvb || cub == cva || cub == cvb) {
          edgeUsed[static_cast<size_t>(ej)] = 1;
          stk.push_back(ej);
        }
      }
    }

    std::unordered_map<int, std::pair<float, float>> rep;
    for (int ej : comp) {
      const Edge& E = edges[static_cast<size_t>(ej)];
      const int k0 = clusterOf(2 * ej);
      const int k1 = clusterOf(2 * ej + 1);
      if (!rep.count(k0))
        rep[k0] = {E.x0, E.y0};
      if (!rep.count(k1))
        rep[k1] = {E.x1, E.y1};
    }

    std::unordered_map<int, int> deg;
    for (int ej : comp) {
      const int u = clusterOf(2 * ej);
      const int v = clusterOf(2 * ej + 1);
      deg[u]++;
      deg[v]++;
    }
    int odd = 0;
    for (const auto& kv : deg)
      if (kv.second % 2 == 1)
        odd++;
    if (odd != 0 && odd != 2) {
      log.push_back("JOIN — skipped a group (branching junction).");
      continue;
    }

    std::vector<int> clusters;
    clusters.reserve(rep.size());
    for (const auto& kv : rep)
      clusters.push_back(kv.first);
    std::sort(clusters.begin(), clusters.end());
    std::unordered_map<int, int> dense;
    for (size_t i = 0; i < clusters.size(); ++i)
      dense[clusters[static_cast<size_t>(i)]] = static_cast<int>(i);
    const int K = static_cast<int>(clusters.size());
    std::vector<std::vector<std::pair<int, int>>> adj(static_cast<size_t>(K));
    std::vector<char> eu(static_cast<size_t>(n), 0);
    for (int ej : comp) {
      const int u = dense[clusterOf(2 * ej)];
      const int v = dense[clusterOf(2 * ej + 1)];
      adj[static_cast<size_t>(u)].push_back({v, ej});
      adj[static_cast<size_t>(v)].push_back({u, ej});
    }

    int start = 0;
    if (odd == 2) {
      start = -1;
      for (const auto& kv : deg) {
        if (kv.second % 2 == 1) {
          start = dense[kv.first];
          break;
        }
      }
      if (start < 0)
        start = 0;
    } else if (!comp.empty())
      start = dense[clusterOf(2 * comp[0])];

    std::vector<std::vector<std::pair<int, int>>> adjW = adj;
    std::vector<int> stkE = {start};
    std::vector<int> pathVerts;
    while (!stkE.empty()) {
      const int v = stkE.back();
      while (!adjW[static_cast<size_t>(v)].empty() &&
             eu[static_cast<size_t>(adjW[static_cast<size_t>(v)].back().second)])
        adjW[static_cast<size_t>(v)].pop_back();
      if (adjW[static_cast<size_t>(v)].empty()) {
        pathVerts.push_back(v);
        stkE.pop_back();
      } else {
        const auto pr = adjW[static_cast<size_t>(v)].back();
        adjW[static_cast<size_t>(v)].pop_back();
        const int to = pr.first;
        const int eix = pr.second;
        if (eu[static_cast<size_t>(eix)])
          continue;
        eu[static_cast<size_t>(eix)] = 1;
        stkE.push_back(to);
      }
    }
    std::reverse(pathVerts.begin(), pathVerts.end());
    if (pathVerts.size() < 2)
      continue;

    std::vector<float> pv;
    auto appendCluster = [&](int d) {
      const int cid = clusters[static_cast<size_t>(d)];
      const auto& pt = rep[cid];
      if (!pv.empty()) {
        const size_t z = pv.size();
        if (z >= 3 && pv[z - 3] == pt.first && pv[z - 2] == pt.second)
          return;
      }
      pv.push_back(pt.first);
      pv.push_back(pt.second);
      pv.push_back(0.f);
    };
    for (const int d : pathVerts)
      appendCluster(d);

    bool closed = pathVerts.front() == pathVerts.back();
    if (closed && pv.size() >= 9)
      pv.resize(pv.size() - 3);

    if (pv.size() < 6)
      continue;

    if (st.userPolylineOffsets.empty())
      st.userPolylineOffsets.push_back(0);
    const int baseVert = st.userPolylineOffsets.back();
    const int nv = static_cast<int>(pv.size() / 3);
    st.userPolylineVerts.insert(st.userPolylineVerts.end(), pv.begin(), pv.end());
    st.userPolylineOffsets.push_back(baseVert + nv);
    st.userPolylineClosed.push_back(static_cast<uint8_t>(closed ? 1 : 0));
    st.userPolylineAttrs.push_back(MakeNewEntityAttrs(st));
    polysOut++;

    for (int ej : comp) {
      const Edge& E = edges[static_cast<size_t>(ej)];
      if (E.lineIx >= 0)
        lineDel.insert(E.lineIx);
      if (E.polyIx >= 0)
        polyDel.insert(E.polyIx);
    }
  }

  std::vector<int> pDel(polyDel.begin(), polyDel.end());
  std::sort(pDel.begin(), pDel.end(), std::greater<int>());
  for (int ix : pDel)
    ErasePolylineByIndex(st, ix);

  std::vector<int> lDel(lineDel.begin(), lineDel.end());
  std::sort(lDel.begin(), lDel.end(), std::greater<int>());
  for (int idx : lDel) {
    const size_t k = static_cast<size_t>(idx) * 6;
    if (k + 5 >= st.userLinesFlat.size())
      continue;
    st.userLinesFlat.erase(st.userLinesFlat.begin() + static_cast<std::ptrdiff_t>(k),
                           st.userLinesFlat.begin() + static_cast<std::ptrdiff_t>(k + 6));
    if (static_cast<size_t>(idx) < st.userLineAttrs.size())
      st.userLineAttrs.erase(st.userLineAttrs.begin() + static_cast<std::ptrdiff_t>(idx));
  }

  st.selection.clear();
  if (polysOut > 0) {
    BumpCadGpuCache(st);
    log.push_back("JOIN — created " + std::to_string(polysOut) + " polyline(s).");
  } else
    log.push_back("JOIN — nothing merged.");
}

// =============================================================================
// OVERKILL — batch cleanup of the entire drawing.
//   • Lines: delete zero-length, exact duplicates, merge collinear overlapping
//            / contiguous segments into the shortest covering segment.
//   • Circles: delete exact duplicates (same center + radius).
//   • Arcs: delete arcs whose circle matches an existing full circle; delete
//           exact duplicate arcs (same center/radius/start/sweep).
//   • Polylines: remove zero-length (coincident) vertex steps.
// Tolerance is auto-derived from drawing extents (1e-4 × span, min 1e-6).
// =============================================================================
void ExecuteOverkill(AppCommandState& st, std::vector<std::string>& log) {
  PushUndoSnapshot(st, "Overkill");
  // ── tolerance ────────────────────────────────────────────────────────────
  float tol = 1e-3f;
  {
    double mnX = 0., mxX = 0., mnY = 0., mxY = 0.;
    if (ComputeWorldExtents(st, &mnX, &mxX, &mnY, &mxY))
      tol = std::max(1e-6f, static_cast<float>(1e-4 * std::max(mxX - mnX, mxY - mnY)));
  }
  const float tolSq = tol * tol;
  int nRemoved = 0;

  // =========================================================================
  // 1. LINE SEGMENTS
  // =========================================================================
  {
    struct LSeg { float x0, y0, x1, y1; EntityAttributes attr; };

    // Snapshot into a working vector that carries attrs
    const size_t nL = st.userLinesFlat.size() / 6;
    std::vector<LSeg> segs;
    segs.reserve(nL);
    for (size_t i = 0; i < nL; ++i) {
      const size_t k = i * 6;
      segs.push_back({ st.userLinesFlat[k],     st.userLinesFlat[k + 1],
                       st.userLinesFlat[k + 3], st.userLinesFlat[k + 4],
                       i < st.userLineAttrs.size() ? st.userLineAttrs[i] : MakeNewEntityAttrs(st) });
    }

    // ── 1a. Zero-length segments ──────────────────────────────────────────
    {
      const size_t before = segs.size();
      segs.erase(std::remove_if(segs.begin(), segs.end(),
                                [&](const LSeg& s) {
                                  const float dx = s.x1 - s.x0, dy = s.y1 - s.y0;
                                  return dx * dx + dy * dy < tolSq;
                                }),
                 segs.end());
      nRemoved += static_cast<int>(before - segs.size());
    }

    // Canonicalize direction so that any reversed duplicate looks identical:
    // ensure dx > 0, or (dx ≈ 0 and dy > 0), by swapping P0↔P1 if needed.
    for (auto& s : segs) {
      const float dx = s.x1 - s.x0, dy = s.y1 - s.y0;
      if (dx < 0.f || (std::fabs(dx) < 1e-12f && dy < 0.f)) {
        std::swap(s.x0, s.x1);
        std::swap(s.y0, s.y1);
      }
    }

    // ── 1b. Exact duplicates ─────────────────────────────────────────────
    // Sort by (x0, y0, x1, y1); compare adjacent clusters within tol.
    {
      std::sort(segs.begin(), segs.end(), [](const LSeg& a, const LSeg& b) {
        if (a.x0 != b.x0) return a.x0 < b.x0;
        if (a.y0 != b.y0) return a.y0 < b.y0;
        if (a.x1 != b.x1) return a.x1 < b.x1;
        return a.y1 < b.y1;
      });
      std::vector<bool> dead(segs.size(), false);
      for (size_t i = 0; i + 1 < segs.size(); ++i) {
        if (dead[i]) continue;
        for (size_t j = i + 1; j < segs.size(); ++j) {
          if (segs[j].x0 - segs[i].x0 > tol) break; // no more same-x0 candidates
          if (dead[j]) continue;
          const float dx0 = segs[j].x0 - segs[i].x0, dy0 = segs[j].y0 - segs[i].y0;
          const float dx1 = segs[j].x1 - segs[i].x1, dy1 = segs[j].y1 - segs[i].y1;
          if (dx0 * dx0 + dy0 * dy0 < tolSq && dx1 * dx1 + dy1 * dy1 < tolSq) {
            dead[j] = true;
            ++nRemoved;
          }
        }
      }
      std::vector<LSeg> kept;
      kept.reserve(segs.size());
      for (size_t i = 0; i < segs.size(); ++i)
        if (!dead[i]) kept.push_back(std::move(segs[i]));
      segs = std::move(kept);
    }

    // ── 1c. Collinear overlap / contiguous merge ──────────────────────────
    // Two segments are merged when they are:
    //   (a) parallel (|sin angle| < 0.001, ≈ 0.06°), AND
    //   (b) collinear (perpendicular distance < tol), AND
    //   (c) overlapping or touching (1-D intervals on shared axis overlap or touch within tol).
    // Union-Find groups them; within each group, project onto canonical direction,
    // merge the 1-D interval cover, and emit the minimal set of segments.
    {
      const int n = static_cast<int>(segs.size());
      std::vector<int> par(static_cast<size_t>(n));
      std::iota(par.begin(), par.end(), 0);

      // Iterative union-find with path halving
      auto find = [&](int x) {
        while (par[x] != x) { par[x] = par[par[x]]; x = par[x]; }
        return x;
      };
      auto unite = [&](int a, int b) { par[find(a)] = find(b); };

      for (int i = 0; i < n; ++i) {
        const float dxi = segs[i].x1 - segs[i].x0;
        const float dyi = segs[i].y1 - segs[i].y0;
        const float li  = std::sqrt(dxi * dxi + dyi * dyi);
        if (li < 1e-12f) continue;
        const float uxi = dxi / li, uyi = dyi / li;

        for (int j = i + 1; j < n; ++j) {
          if (find(i) == find(j)) continue; // already same group

          const float dxj = segs[j].x1 - segs[j].x0;
          const float dyj = segs[j].y1 - segs[j].y0;
          const float lj  = std::sqrt(dxj * dxj + dyj * dyj);
          if (lj < 1e-12f) continue;
          const float uxj = dxj / lj, uyj = dyj / lj;

          // (a) Parallel?
          if (std::fabs(uxi * uyj - uyi * uxj) > 0.001f) continue;

          // (b) Collinear? — perpendicular distance from j.P0 to line through i
          {
            const float vx = segs[j].x0 - segs[i].x0;
            const float vy = segs[j].y0 - segs[i].y0;
            if (std::fabs(vx * uyi - vy * uxi) > tol) continue;
          }

          // (c) Overlap or touch along uxi,uyi?
          {
            const float tj0 = (segs[j].x0 - segs[i].x0) * uxi + (segs[j].y0 - segs[i].y0) * uyi;
            const float tj1 = (segs[j].x1 - segs[i].x0) * uxi + (segs[j].y1 - segs[i].y0) * uyi;
            const float jMin = std::min(tj0, tj1);
            const float jMax = std::max(tj0, tj1);
            if (jMax < -tol || jMin > li + tol) continue;
          }

          unite(i, j);
        }
      }

      // Group segments by their union-find root
      std::unordered_map<int, std::vector<int>> groups;
      groups.reserve(static_cast<size_t>(n));
      for (int i = 0; i < n; ++i)
        groups[find(i)].push_back(i);

      std::vector<LSeg> result;
      result.reserve(static_cast<size_t>(n));

      for (auto& [root, members] : groups) {
        (void)root;
        if (members.size() == 1) {
          result.push_back(std::move(segs[static_cast<size_t>(members[0])]));
          continue;
        }

        // Pick canonical direction from the longest member
        int best = members[0];
        float bestLen = 0.f;
        for (int idx : members) {
          const float dx = segs[idx].x1 - segs[idx].x0, dy = segs[idx].y1 - segs[idx].y0;
          const float l = std::sqrt(dx * dx + dy * dy);
          if (l > bestLen) { bestLen = l; best = idx; }
        }
        const float dxb = segs[best].x1 - segs[best].x0;
        const float dyb = segs[best].y1 - segs[best].y0;
        const float lb  = std::sqrt(dxb * dxb + dyb * dyb);
        if (lb < 1e-12f) { nRemoved += static_cast<int>(members.size()); continue; }
        const float ux = dxb / lb, uy = dyb / lb;
        const float ox = segs[best].x0, oy = segs[best].y0;

        // Project each member's endpoints onto the canonical axis
        struct Iv { float t0, t1; int idx; };
        std::vector<Iv> ivs;
        ivs.reserve(members.size());
        for (int idx : members) {
          const float ta = (segs[idx].x0 - ox) * ux + (segs[idx].y0 - oy) * uy;
          const float tb = (segs[idx].x1 - ox) * ux + (segs[idx].y1 - oy) * uy;
          ivs.push_back({ std::min(ta, tb), std::max(ta, tb), idx });
        }
        std::sort(ivs.begin(), ivs.end(), [](const Iv& a, const Iv& b) { return a.t0 < b.t0; });

        // Sweep through intervals and merge overlapping/touching ones
        struct Mv { float t0, t1; int idx; }; // idx carries representative attrs
        std::vector<Mv> merged;
        merged.reserve(ivs.size());
        for (const auto& iv : ivs) {
          if (merged.empty() || iv.t0 > merged.back().t1 + tol)
            merged.push_back({ iv.t0, iv.t1, iv.idx });
          else if (iv.t1 > merged.back().t1)
            merged.back().t1 = iv.t1;
        }

        // members.size() originals → merged.size() outputs; log net removal
        nRemoved += static_cast<int>(members.size()) - static_cast<int>(merged.size());

        for (const auto& m : merged) {
          LSeg nl;
          nl.x0   = ox + ux * m.t0;  nl.y0 = oy + uy * m.t0;
          nl.x1   = ox + ux * m.t1;  nl.y1 = oy + uy * m.t1;
          nl.attr = segs[static_cast<size_t>(m.idx)].attr;
          result.push_back(std::move(nl));
        }
      }

      segs = std::move(result);
    }

    // Write back
    st.userLinesFlat.clear();
    st.userLineAttrs.clear();
    st.userLinesFlat.reserve(segs.size() * 6);
    st.userLineAttrs.reserve(segs.size());
    for (const auto& s : segs) {
      st.userLinesFlat.insert(st.userLinesFlat.end(), { s.x0, s.y0, 0.f, s.x1, s.y1, 0.f });
      st.userLineAttrs.push_back(s.attr);
    }
  }

  // =========================================================================
  // 2. CIRCLES — remove exact duplicates (same center + radius)
  // =========================================================================
  {
    const size_t nC = st.userCirclesCxCyZR.size() / 4;
    struct Circ { float cx, cy, z, r; EntityAttributes attr; };
    std::vector<Circ> cs;
    cs.reserve(nC);
    for (size_t i = 0; i < nC; ++i) {
      const size_t k = i * 4;
      cs.push_back({ st.userCirclesCxCyZR[k], st.userCirclesCxCyZR[k + 1], st.userCirclesCxCyZR[k + 2],
                     st.userCirclesCxCyZR[k + 3],
                     i < st.userCircleAttrs.size() ? st.userCircleAttrs[i] : MakeNewEntityAttrs(st) });
    }
    // Sort by radius then center; allows early break on radius mismatch
    std::sort(cs.begin(), cs.end(), [](const Circ& a, const Circ& b) {
      if (a.r != b.r) return a.r < b.r;
      if (a.cx != b.cx) return a.cx < b.cx;
      return a.cy < b.cy;
    });
    std::vector<bool> dead(cs.size(), false);
    for (size_t i = 0; i + 1 < cs.size(); ++i) {
      if (dead[i]) continue;
      for (size_t j = i + 1; j < cs.size(); ++j) {
        if (cs[j].r - cs[i].r > tol) break; // sorted by r; no more matching radii
        if (dead[j]) continue;
        // Z participates: two circles sharing a centre in plan but sitting at different
        // elevations are distinct objects in 3D, not duplicates (REQ-057).
        const float dx = cs[j].cx - cs[i].cx, dy = cs[j].cy - cs[i].cy, dz = cs[j].z - cs[i].z;
        const float dr = cs[j].r  - cs[i].r;
        if (dx * dx + dy * dy + dz * dz < tolSq && dr * dr < tolSq) { dead[j] = true; ++nRemoved; }
      }
    }
    st.userCirclesCxCyZR.clear();
    st.userCircleAttrs.clear();
    for (size_t i = 0; i < cs.size(); ++i) {
      if (!dead[i]) {
        st.userCirclesCxCyZR.push_back(cs[i].cx);
        st.userCirclesCxCyZR.push_back(cs[i].cy);
        st.userCirclesCxCyZR.push_back(cs[i].z);
        st.userCirclesCxCyZR.push_back(cs[i].r);
        st.userCircleAttrs.push_back(cs[i].attr);
      }
    }
  }

  // =========================================================================
  // 3. ARCS
  //   3a. Delete arcs whose (center, radius) matches an existing full circle.
  //   3b. Delete exact duplicate arcs (same center/radius/startRad/sweepRad).
  // =========================================================================
  {
    const size_t nA = st.userArcs.size();
    const size_t nC = st.userCirclesCxCyZR.size() / 4;
    std::vector<bool> dead(nA, false);

    // 3a — arcs over full circles
    for (size_t i = 0; i < nA; ++i) {
      if (dead[i]) continue;
      const CadArc& a = st.userArcs[i];
      for (size_t c = 0; c < nC; ++c) {
        const float dx = a.cx - st.userCirclesCxCyZR[c * 4];
        const float dy = a.cy - st.userCirclesCxCyZR[c * 4 + 1];
        const float dr = a.r  - st.userCirclesCxCyZR[c * 4 + 3];
        if (dx * dx + dy * dy < tolSq && dr * dr < tolSq) { dead[i] = true; ++nRemoved; break; }
      }
    }

    // 3b — exact duplicate arcs
    // Normalize startRad to [0, 2π) for comparison
    constexpr float kTwoPi = 2.f * 3.14159265358979f;
    auto normAngle = [&](float a) -> float {
      a = std::fmod(a, kTwoPi);
      if (a < 0.f) a += kTwoPi;
      return a;
    };
    constexpr float kAngTol = 1e-4f; // ≈ 0.006°
    for (size_t i = 0; i < nA; ++i) {
      if (dead[i]) continue;
      const CadArc& a = st.userArcs[i];
      const float aStart = normAngle(a.startRad);
      for (size_t j = i + 1; j < nA; ++j) {
        if (dead[j]) continue;
        const CadArc& b = st.userArcs[j];
        const float dxC = a.cx - b.cx, dyC = a.cy - b.cy, drR = a.r - b.r;
        if (dxC * dxC + dyC * dyC >= tolSq || drR * drR >= tolSq) continue;
        const float dStart = std::fabs(aStart - normAngle(b.startRad));
        const float dSweep = std::fabs(a.sweepRad - b.sweepRad);
        // Handle 0 vs 2π wrap for start angle
        const float dStartW = std::min(dStart, std::fabs(dStart - kTwoPi));
        if (dStartW < kAngTol && dSweep < kAngTol) { dead[j] = true; ++nRemoved; }
      }
    }

    std::vector<CadArc>           newArcs;
    std::vector<EntityAttributes> newArcAttrs;
    newArcs.reserve(nA);
    newArcAttrs.reserve(nA);
    for (size_t i = 0; i < nA; ++i) {
      if (!dead[i]) {
        newArcs.push_back(st.userArcs[i]);
        newArcAttrs.push_back(i < st.userArcAttrs.size() ? st.userArcAttrs[i] : MakeNewEntityAttrs(st));
      }
    }
    st.userArcs     = std::move(newArcs);
    st.userArcAttrs = std::move(newArcAttrs);
  }

  // =========================================================================
  // 4. POLYLINES — remove zero-length (coincident) vertex steps
  // =========================================================================
  {
    const int nP = static_cast<int>(st.userPolylineOffsets.size()) > 0
                     ? static_cast<int>(st.userPolylineOffsets.size()) - 1
                     : 0;
    // Collect polylines to erase (those that become degenerate after cleanup).
    // Iterate high→low so earlier polyline offsets stay valid while we mutate.
    std::vector<int> polyToErase;

    for (int pi = nP - 1; pi >= 0; --pi) {
      const int v0 = st.userPolylineOffsets[static_cast<size_t>(pi)];
      const int v1 = st.userPolylineOffsets[static_cast<size_t>(pi + 1)];
      if (v1 - v0 < 2) { polyToErase.push_back(pi); continue; }

      const bool closed = static_cast<size_t>(pi) < st.userPolylineClosed.size() &&
                          st.userPolylineClosed[static_cast<size_t>(pi)];

      // Collect this polyline's vertices
      std::vector<std::pair<float, float>> verts;
      verts.reserve(static_cast<size_t>(v1 - v0));
      for (int vi = v0; vi < v1; ++vi) {
        verts.push_back({ st.userPolylineVerts[static_cast<size_t>(vi * 3)],
                          st.userPolylineVerts[static_cast<size_t>(vi * 3 + 1)] });
      }

      // Remove consecutive duplicate vertices (zero-length steps)
      std::vector<std::pair<float, float>> clean;
      clean.reserve(verts.size());
      clean.push_back(verts[0]);
      for (size_t k = 1; k < verts.size(); ++k) {
        const float dx = verts[k].first  - clean.back().first;
        const float dy = verts[k].second - clean.back().second;
        if (dx * dx + dy * dy >= tolSq)
          clean.push_back(verts[k]);
        else
          ++nRemoved;
      }
      // For closed polylines, also remove zero-length wrap (last vertex ≈ first)
      if (closed) {
        while (clean.size() >= 2) {
          const float dx = clean.back().first  - clean.front().first;
          const float dy = clean.back().second - clean.front().second;
          if (dx * dx + dy * dy < tolSq) { clean.pop_back(); ++nRemoved; }
          else break;
        }
      }

      if (clean.size() < 2)   { polyToErase.push_back(pi); continue; }
      if (clean.size() == verts.size()) continue; // unchanged

      // Replace the vertex slice [v0*3, v1*3) with cleaned data
      const int nNew  = static_cast<int>(clean.size());
      const int delta = nNew - (v1 - v0);
      st.userPolylineVerts.erase(
          st.userPolylineVerts.begin() + static_cast<std::ptrdiff_t>(v0 * 3),
          st.userPolylineVerts.begin() + static_cast<std::ptrdiff_t>(v1 * 3));

      std::vector<float> newV;
      newV.reserve(static_cast<size_t>(nNew * 3));
      for (const auto& p : clean) { newV.push_back(p.first); newV.push_back(p.second); newV.push_back(0.f); }
      st.userPolylineVerts.insert(
          st.userPolylineVerts.begin() + static_cast<std::ptrdiff_t>(v0 * 3),
          newV.begin(), newV.end());

      // Adjust all offsets after pi by the vertex delta
      for (size_t oi = static_cast<size_t>(pi + 1); oi < st.userPolylineOffsets.size(); ++oi)
        st.userPolylineOffsets[oi] += delta;
    }

    // polyToErase is in descending order (loop ran high→low, push_back is stable)
    for (int pi : polyToErase)
      ErasePolylineByIndex(st, pi);
  }

  // ── report ────────────────────────────────────────────────────────────────
  st.selection.clear();
  if (nRemoved > 0) {
    BumpCadGpuCache(st);
    char buf[128];
    std::snprintf(buf, sizeof(buf), "OVERKILL — cleaned up %d object(s).", nRemoved);
    log.push_back(buf);
  } else {
    log.push_back("OVERKILL — nothing to clean up.");
  }
}

void StartJoinCommand(AppCommandState& st, std::vector<std::string>& log) {
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  st.selectedSurveyPointIndices.clear();
  if (!st.selection.empty()) {
    ExecuteJoinSelection(st, log);
    return;
  }
  st.active = AppCommandState::Kind::Join;
  st.selBoxWaitingSecond = false;
  log.push_back("JOIN — window-select lines/polylines that meet at endpoints. ESC cancels.");
}

void StartTrimCommand(AppCommandState& st, std::vector<std::string>& log) {
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  st.active = AppCommandState::Kind::Trim;
  st.trimCutters.clear();
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
//
// This is the UCS in the only form the application currently produces: the work plane stays
// parallel to world XY and moves in Z. AutoCAD splits the same idea across ELEV (elevation) and
// UCS (a full coordinate system); only the elevation half exists here, so it carries AutoCAD's
// name for that half rather than pretending to be a full UCS.
void StartElevCommand(AppCommandState& st, std::vector<std::string>& log) {
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  st.active = AppCommandState::Kind::Elev;
  char buf[160];
  std::snprintf(buf, sizeof(buf), "Specify new default elevation <%.4f>:  (W = back to world Z 0)",
                static_cast<double>(CadWorkPlaneElevation(st)));
  log.push_back(buf);
}

// Shared by the prompt and the inline `ELEV 12.5` form, so neither can set a value the other would
// reject (REQ-201).
bool ApplyElevValue(AppCommandState& st, double z, std::vector<std::string>& log) {
  if (!std::isfinite(z)) {
    log.push_back("ELEV — elevation must be a finite number.");
    return false;
  }
  st.ucsOriginZ = z;
  char buf[128];
  std::snprintf(buf, sizeof(buf), "Elevation = %.4f — new geometry is drawn on this plane.", z);
  log.push_back(buf);
  return true;
}

// --- REQ-100 frame-budget benchmark -------------------------------------------------------------

/// Install the bench scene, saving the user's polylines so the run cannot cost them their drawing.
///
/// The camera is saved too and forced to a fixed starting orientation: a benchmark that began from
/// whatever the user was looking at would measure a different amount of geometry every run, and the
/// number would not be comparable to the one in the requirement.
bool StartFrameBudgetBench(AppCommandState& st, int segments, int frames, std::vector<std::string>& log) {
  if (st.bench.active) {
    log.push_back("BENCH — a run is already in progress.");
    return false;
  }
  if (st.activeSpaceIndex != kModelSpaceIndex) {
    log.push_back("BENCH — switch to model space first (REQ-100 measures the model viewport).");
    return false;
  }
  if (segments < 1 || frames < 1) {
    log.push_back("BENCH — segment count and frame count must both be positive.");
    return false;
  }

  AppCommandState::BenchRun& b = st.bench;
  b.savedPolyVerts = st.userPolylineVerts;
  b.savedPolyOffsets = st.userPolylineOffsets;
  b.savedPolyClosed = st.userPolylineClosed;
  b.savedPolyAttrs = st.userPolylineAttrs;
  b.savedSurfaces = st.cadSurfaces;
  b.savedSurfaceAttrs = st.cadSurfaceAttrs;
  b.savedMeshes = st.cadMeshes;
  b.savedMeshAttrs = st.cadMeshAttrs;
  b.savedVisualStyle = st.viewportVisualStyle;
  b.savedAzimuthDeg = st.viewportAzimuthDeg;
  b.savedElevationDeg = st.viewportElevationDeg;
  b.savedZoom = st.viewportZoom;
  b.savedPanX = st.viewportPanX;
  b.savedPanY = st.viewportPanY;
  b.savedPanZ = st.viewportPanZ;

  if (b.meshTriangleCount > 0) {
    // Shaded-mesh profile (REQ-100 (b), density decided 2026-08-15). The line stores are emptied
    // for the same reason the surface profile empties them: the number has to be the mesh's cost
    // and nothing else. Surfaces are cleared too, so the two large profiles can never overlap.
    st.userPolylineVerts.clear();
    st.userPolylineOffsets.clear();  // empty, not {0} — see ErasePolylineByIndex / issue #60
    st.userPolylineClosed.clear();
    st.userPolylineAttrs.clear();
    st.cadSurfaces.clear();
    st.cadSurfaceAttrs.clear();

    auto mesh = std::make_shared<CadMesh>();
    b.meshTriangleCount =
        benchscene::BuildMeshScene(b.meshTriangleCount, &mesh->vertsXyz, &mesh->normalsXyz, &mesh->indices);
    mesh->sourceName = "BENCH mesh";
    // One part, deliberately: this is the shape TASK-041 measured the mesh path against, so the two
    // numbers are comparable. A real import has hundreds of parts and therefore hundreds of draw
    // calls — that per-part cost is NOT in this profile, and saying so is better than inventing a
    // part count nobody chose.
    CadMeshPart part;
    part.name = "terrain";
    part.indexBegin = 0;
    part.indexCount = static_cast<int>(mesh->indices.size());
    mesh->parts.push_back(part);
    st.cadMeshes.assign(1, std::move(mesh));
    st.cadMeshAttrs.assign(1, MakeNewEntityAttrs(st));

    // The profile is *shaded* meshes. In 2D Wireframe the mesh is not drawn at all (TASK-041 §9),
    // so measuring in the user's current style would measure whatever they happened to be in.
    st.viewportVisualStyle = VisualStyle::Shaded;
    b.segmentCount = 0;  // no line segments in this profile; the report prints triangles instead
  } else if (b.surfacePointCount > 0) {
    // Surface profile (REQ-100 as amended / ADR-028): the scene is ONE surface, and the line stores
    // are emptied so the measurement is the surface's cost and nothing else. Triangle edges are
    // regenerated display geometry, which is exactly why this profile is not implied by the other
    // two and has to be measured on its own.
    st.userPolylineVerts.clear();
    st.userPolylineOffsets.clear();  // empty, not {0} — see ErasePolylineByIndex / issue #60
    st.userPolylineClosed.clear();
    st.userPolylineAttrs.clear();

    std::vector<float> ptsXyz;
    const int n = benchscene::BuildSurfacePointScene(b.surfacePointCount, &ptsXyz);
    std::vector<TinInputPoint> pts;
    pts.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
      pts.push_back({static_cast<double>(ptsXyz[static_cast<size_t>(i) * 3 + 0]),
                     static_cast<double>(ptsXyz[static_cast<size_t>(i) * 3 + 1]),
                     ptsXyz[static_cast<size_t>(i) * 3 + 2]});
    const TinBuildResult tr = BuildTin(pts);
    if (!tr.ok()) {
      log.push_back(std::string("BENCH — surface scene failed to triangulate: ") + tr.message);
      return false;
    }
    auto tin = std::make_shared<CadTin>();
    tin->vertsXyz = tr.vertsXyz;
    tin->indices = tr.indices;
    CadSurface bs;
    bs.name = "BENCH surface";
    bs.tin = std::move(tin);
    b.surfaceTriangleCount = bs.triangleCount();
    st.cadSurfaces.assign(1, std::move(bs));
    st.cadSurfaceAttrs.assign(1, MakeNewEntityAttrs(st));
    b.segmentCount = b.surfaceTriangleCount * 3;  // triangulation edges, for the report

    // REQ-100 profile (c) is defined as a **contoured** surface, and until REQ-070 landed this case
    // measured an uncontoured one because contours did not exist. The bench surface carries no
    // styleName, so it resolves to "Standard" — which draws contours and the border — and the record
    // below states the interval, because a contour count is meaningless without it.
    SurfaceStyles::EnsureStandard(st.surfaceStyles);
    if (const SurfaceStyle* bstyle = SurfaceStyles::Resolve(st.surfaceStyles, std::string())) {
      b.surfaceMinorIntervalFt = bstyle->minorIntervalFt;
      b.surfaceMajorIntervalFt = bstyle->majorIntervalFt;
    }
    b.regenBaselineTaken = false;
    b.regenDuringRun = 0;
  } else {
    b.segmentCount = benchscene::BuildContourScene(segments, &st.userPolylineVerts, &st.userPolylineOffsets,
                                                   &st.userPolylineClosed);
    st.userPolylineAttrs.assign(st.userPolylineClosed.size(), MakeNewEntityAttrs(st));
  }

  // Frame the whole scene from a tilted view. The framing is computed from the geometry rather than
  // hard-coded: if any of the scene falls outside the viewport the GPU stops rasterising it and the
  // benchmark measures less than the density the requirement names. Sizing to the scene's bounding
  // SPHERE means that stays true through the whole orbit, at every azimuth.
  double mnX = 1e300, mxX = -1e300, mnY = 1e300, mxY = -1e300, mnZ = 1e300, mxZ = -1e300;
  // Whichever store the profile filled: the contour scene lives in the polylines, the surface
  // profile in the TIN, the mesh profile in the mesh. Framing from the wrong one would put the
  // scene off screen and measure a viewport with nothing in it.
  const std::vector<float>& frameVerts =
      (b.meshTriangleCount > 0 && !st.cadMeshes.empty() && st.cadMeshes[0])
          ? st.cadMeshes[0]->vertsXyz
          : ((b.surfacePointCount > 0 && !st.cadSurfaces.empty() && st.cadSurfaces[0].tin)
                 ? st.cadSurfaces[0].tin->vertsXyz
                 : st.userPolylineVerts);
  for (size_t i = 0; i + 2 < frameVerts.size(); i += 3) {
    mnX = std::min(mnX, static_cast<double>(frameVerts[i]));
    mxX = std::max(mxX, static_cast<double>(frameVerts[i]));
    mnY = std::min(mnY, static_cast<double>(frameVerts[i + 1]));
    mxY = std::max(mxY, static_cast<double>(frameVerts[i + 1]));
    mnZ = std::min(mnZ, static_cast<double>(frameVerts[i + 2]));
    mxZ = std::max(mxZ, static_cast<double>(frameVerts[i + 2]));
  }
  const double cx = 0.5 * (mnX + mxX);
  const double cy = 0.5 * (mnY + mxY);
  const double cz = 0.5 * (mnZ + mxZ);
  const double radius =
      0.5 * std::sqrt((mxX - mnX) * (mxX - mnX) + (mxY - mnY) * (mxY - mnY) + (mxZ - mnZ) * (mxZ - mnZ));
  st.viewportPanX = cx;
  st.viewportPanY = cy;
  st.viewportPanZ = cz;
  const double halfH = std::max(radius * 1.05, 1.0);  // 5% margin so nothing clips at the edge
  st.viewportZoom = static_cast<float>(50.0 / halfH);
  st.viewportAzimuthDeg = 0.f;
  st.viewportElevationDeg = 55.f;

  b.frameMs.clear();
  b.frameMs.reserve(static_cast<size_t>(frames));
  b.framesTotal = frames;
  b.warmupFrames = 60;
  b.frameIndex = 0;
  b.orbitDegPerFrame = 0.5;  // a full turn every 720 frames — continuous, and never repeats a frame
  b.sceneInstalled = true;
  b.active = true;
  BumpCadGpuCache(st);

  char scene[64];
  if (b.meshTriangleCount > 0)
    std::snprintf(scene, sizeof(scene), "%d triangles, Shaded", b.meshTriangleCount);
  else if (b.surfacePointCount > 0)
    std::snprintf(scene, sizeof(scene), "%d points", b.surfacePointCount);
  else
    std::snprintf(scene, sizeof(scene), "%d segments", b.segmentCount);

  char msg[256];
  std::snprintf(msg, sizeof(msg),
                "BENCH — REQ-100: %s, %d frames (%d warm-up), continuous orbit. Vsync is disabled for the "
                "run; the drawing%s restored when it finishes.",
                scene, frames, b.warmupFrames,
                b.meshTriangleCount > 0 ? " and the visual style are" : " is");
  log.push_back(msg);
  return true;
}

/// Restore the drawing and camera, and report. Called from the frame loop when the run completes.
void FinishFrameBudgetBench(AppCommandState& st, std::vector<std::string>& log) {
  AppCommandState::BenchRun& b = st.bench;
  if (!b.active)
    return;
  if (b.sceneInstalled) {
    st.userPolylineVerts = std::move(b.savedPolyVerts);
    st.userPolylineOffsets = std::move(b.savedPolyOffsets);
    st.userPolylineClosed = std::move(b.savedPolyClosed);
    st.userPolylineAttrs = std::move(b.savedPolyAttrs);
    st.cadSurfaces = std::move(b.savedSurfaces);
    st.cadSurfaceAttrs = std::move(b.savedSurfaceAttrs);
    st.cadMeshes = std::move(b.savedMeshes);
    st.cadMeshAttrs = std::move(b.savedMeshAttrs);
    st.viewportVisualStyle = b.savedVisualStyle;
    b.savedMeshes.clear();
    b.savedMeshAttrs.clear();
    b.savedPolyVerts.clear();
    b.savedPolyOffsets.clear();
    b.savedPolyClosed.clear();
    b.savedPolyAttrs.clear();
    b.savedSurfaces.clear();
    b.savedSurfaceAttrs.clear();
    st.viewportAzimuthDeg = b.savedAzimuthDeg;
    st.viewportElevationDeg = b.savedElevationDeg;
    st.viewportZoom = b.savedZoom;
    st.viewportPanX = b.savedPanX;
    st.viewportPanY = b.savedPanY;
    st.viewportPanZ = b.savedPanZ;
    b.sceneInstalled = false;
    BumpCadGpuCache(st);
  }
  b.active = false;

  const benchscene::FrameStats s = benchscene::Summarize(b.frameMs);
  if (s.frames < 1) {
    log.push_back("BENCH — no frames were timed; nothing to report.");
    return;
  }
  constexpr double kBudgetMs = 16.0;  // REQ-100
  const bool pass = s.p95Ms <= kBudgetMs;
  // Name the profile, and describe the scene that produced the number: REQ-100 has three profiles,
  // and a p95 quoted without saying which one it measured is as unreproducible as one quoted
  // without the reference machine. Built once and used by BOTH the console line and the file
  // record below — the record is the half that outlives the session, and it is the half that used
  // to omit this (a surface run was preserved as "segments 599898", indistinguishable from a
  // 600k-segment line scene).
  const char* profileName = "line segments";
  char scene[128];
  std::snprintf(scene, sizeof(scene), "%d segments", b.segmentCount);
  if (b.meshTriangleCount > 0) {
    profileName = "shaded meshes";
    std::snprintf(scene, sizeof(scene), "%d triangles, Shaded", b.meshTriangleCount);
  } else if (b.surfacePointCount > 0) {
    profileName = "surface (contoured)";
    std::snprintf(scene, sizeof(scene), "%d points, %d triangles, contoured at %s/%s ft (%d contour segs)",
                  b.surfacePointCount, b.surfaceTriangleCount,
                  SurfaceStyles::FormatFt(b.surfaceMinorIntervalFt).c_str(),
                  SurfaceStyles::FormatFt(b.surfaceMajorIntervalFt).c_str(), b.surfaceContourSegs);
  }

  char msg[320];
  std::snprintf(msg, sizeof(msg), "BENCH (%s) — %s, %d timed frames: p95 %.2f ms (budget %.0f ms) — %s.",
                profileName, scene, s.frames, s.p95Ms, kBudgetMs, pass ? "PASS" : "FAIL");
  log.push_back(msg);
  std::snprintf(msg, sizeof(msg), "BENCH — min %.2f  median %.2f  mean %.2f  p95 %.2f  p99 %.2f  max %.2f ms.",
                s.minMs, s.medianMs, s.meanMs, s.p95Ms, s.p99Ms, s.maxMs);
  log.push_back(msg);

  // ADR-036 (e)'s separate obligation, reported separately: the timing above would look identical
  // whether the cache held or not on a fast enough machine, so "held" has to be its own claim.
  const bool cacheHeld = b.regenDuringRun == 0;
  if (b.surfacePointCount > 0) {
    std::snprintf(msg, sizeof(msg),
                  "BENCH — surface display cache regenerated %llu time(s) across %d timed frames "
                  "(expected 0) — %s.",
                  static_cast<unsigned long long>(b.regenDuringRun), s.frames,
                  cacheHeld ? "HELD" : "NOT HELD, contours are being regenerated per frame");
    log.push_back(msg);
  }

  // Also written to a file: a benchmark's value is in the record, and reading six figures off a
  // fading command line is how a number gets transcribed wrong into a completion report.
  const std::filesystem::path dir = UserDataDirectory();
  if (!dir.empty()) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    std::ofstream f(dir / "bench-req100.txt", std::ios::app);
    if (f) {
      const std::time_t t = std::time(nullptr);
      char timeBuf[32];
      struct tm tmInfo{};
#ifdef _WIN32
      localtime_s(&tmInfo, &t);
#else
      localtime_r(&t, &tmInfo);
#endif
      std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", &tmInfo);
      f << timeBuf << "  REQ-100 frame budget\n"
        << "  profile       " << profileName << "\n"
        << "  scene         " << scene << "\n"
        << "  timed frames  " << s.frames << " (after " << b.warmupFrames << " warm-up)\n"
        << "  orbit         " << b.orbitDegPerFrame << " deg/frame, vsync off\n"
        << "  min           " << s.minMs << " ms\n"
        << "  median        " << s.medianMs << " ms\n"
        << "  mean          " << s.meanMs << " ms\n"
        << "  p95           " << s.p95Ms << " ms   <-- REQ-100 is judged on this\n"
        << "  p99           " << s.p99Ms << " ms\n"
        << "  max           " << s.maxMs << " ms\n"
        << "  budget        " << kBudgetMs << " ms  => " << (pass ? "PASS" : "FAIL") << "\n";
      if (b.surfacePointCount > 0) {
        // The record's own half of ADR-036 (e). TASK-053's fix (b) applies here too: the permanent
        // record is the half that gets missed, and a p95 with no statement of whether the cache held
        // cannot be told apart from one measured with the defect present.
        f << "  contour interval  minor " << SurfaceStyles::FormatFt(b.surfaceMinorIntervalFt)
          << " ft, major " << SurfaceStyles::FormatFt(b.surfaceMajorIntervalFt) << " ft\n"
          << "  contour segs      " << b.surfaceContourSegs << "\n"
          << "  cache regens      " << b.regenDuringRun << " during the timed frames (expected 0)  => "
          << (cacheHeld ? "HELD" : "NOT HELD") << "\n";
      }
      f << "\n";
    }
  }
  b.frameMs.clear();
}

/// Import a glTF/GLB model as a REQ-063 mesh (REQ-065).
///
/// Nothing touches the drawing until the parse has fully succeeded — the undo snapshot is pushed
/// *after* the importer returns ok, so a malformed file leaves the drawing exactly as it was, which
/// is what REQ-065's "no partial import" condition means in practice.
bool ImportGltfModel(AppCommandState& st, const std::string& path, double unitScale, double insX, double insY,
                     double insZ, std::vector<std::string>& log) {
  modelimport::Options opt;
  opt.unitScale = unitScale;
  opt.insertX = insX;
  opt.insertY = insY;
  opt.insertZ = insZ;

  // Dispatch on the file's kind. A DWG cannot be read for 3D content directly — its geometry may be
  // vendor custom objects that only the vendor's enabler can decode — so it is routed through an
  // installed AutoCAD, which is ADR-024's converter pattern applied to 3D (ADR-026 addendum).
  std::string ext = std::filesystem::u8path(path).extension().u8string();
  ext = StringUtil::toLowerAsciiCopy(ext);

  modelimport::Result r;
  if (ext == ".dwg") {
    std::string whyNot;
    if (!dwgmesh::ConversionAvailable(&whyNot)) {
      log.push_back("Cannot import a DWG model — " + whyNot);
      return false;
    }
    log.push_back("Converting the DWG's 3D solids via " + FindDwgConverter().displayName +
                  " — this can take a few minutes on a large model.");
    const dwgmesh::ConvertResult cr = dwgmesh::ConvertDwgToMesh(path, opt);
    if (!cr.ok) {
      log.push_back("Model import failed — " + cr.error);
      return false;
    }
    r = cr.model;
  } else if (ext == ".stl") {
    r = stl::ImportStlFile(path, opt);
  } else {
    r = gltf::ImportGltfFile(path, opt);  // .glb / .gltf, and the fallback for an odd extension
  }

  if (!r.ok) {
    log.push_back("Model import failed — " + r.error);
    return false;
  }

  auto mesh = std::make_shared<CadMesh>();
  mesh->vertsXyz = r.vertsXyz;
  mesh->normalsXyz = r.normalsXyz;
  mesh->indices = r.indices;
  mesh->sourceName = std::filesystem::u8path(path).filename().u8string();
  mesh->parts.reserve(r.parts.size());
  for (const modelimport::Part& p : r.parts) {
    CadMeshPart cp;
    cp.name = p.name;
    cp.indexBegin = p.indexBegin;
    cp.indexCount = p.indexCount;
    cp.r = p.r;
    cp.g = p.g;
    cp.b = p.b;
    mesh->parts.push_back(std::move(cp));
  }

  PushUndoSnapshot(st, "Import model");
  st.cadMeshes.push_back(std::move(mesh));
  st.cadMeshAttrs.push_back(MakeNewEntityAttrs(st));
  BumpCadGpuCache(st);

  char msg[320];
  std::snprintf(msg, sizeof(msg), "Imported %s — %d triangles, %d parts, scale %.6g.",
                st.cadMeshes.back()->sourceName.c_str(), r.triangleCount(),
                static_cast<int>(r.parts.size()), unitScale);
  log.push_back(msg);
  // REQ-065 / REQ-201: what did not come in is stated, never dropped in silence.
  if (!r.skipped.empty()) {
    std::string s = "Not imported (geometry only): ";
    for (size_t i = 0; i < r.skipped.size(); ++i)
      s += (i ? ", " : "") + r.skipped[i];
    log.push_back(s + ".");
  }
  return true;
}

/// Shared by the prompt and the inline `VS SHADED` form, so neither can set a value the other would
/// reject (REQ-201). Accepts the AutoCAD-ish spellings a user is likely to try.
bool ApplyVisualStyleValue(AppCommandState& st, const std::string& raw, std::vector<std::string>& log) {
  VisualStyle s = st.viewportVisualStyle;
  if (!VisualStyleFromName(StringUtil::trimCopy(raw), &s)) {
    log.push_back("VISUALSTYLE — enter 2D, HIDDEN or SHADED.");
    return false;
  }
  st.viewportVisualStyle = s;
  log.push_back(std::string("Visual style = ") + VisualStyleName(s) + ".");
  return true;
}

// Reset the work plane to world XY. Kept separate from ApplyElevValue so the status readout and
// the "W" option have one shared meaning of "world".
void ApplyUcsWorld(AppCommandState& st, std::vector<std::string>& log) {
  st.ucsOriginX = st.ucsOriginY = st.ucsOriginZ = 0.0;
  st.ucsNormalX = st.ucsNormalY = 0.0;
  st.ucsNormalZ = 1.0;
  st.ucsAzimuthDeg = 0.f;
  log.push_back("UCS = World — new geometry is drawn on the world XY plane.");
}

void StartDeleteCommand(AppCommandState& st, std::vector<std::string>& log) {
  if (st.activeSpaceIndex != kModelSpaceIndex && !InFloatingModelSpace(st)) {  // paper space: geometry + viewports
    const bool hadEntities = !st.selectedPaperEntities.empty();
    const bool hadViewports = !st.selectedViewports.empty();
    if (hadEntities)
      DeleteSelectedPaperEntities(st, log);  // REQ-037
    if (hadViewports)
      DeleteSelectedViewports(st, log);  // REQ-035
    if (!hadEntities && !hadViewports)
      log.push_back("DELETE — select paper object(s) or viewport(s) first.");
    return;
  }
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  // Survey points take priority: deleting a point also removes its linked label.
  // Checking selection first caused the label annotation to be deleted on the first
  // keypress (since SyncSurveyPointLinkedMtextSelection adds the label to st.selection),
  // requiring a second Delete to remove the point itself.
  if (!st.selectedSurveyPointIndices.empty()) {
    DeleteSelectedSurveyPoints(st, log);
    if (!st.selection.empty())
      ExecuteDeleteSelection(st, log);
    return;
  }
  if (!st.selection.empty()) {
    ExecuteDeleteSelection(st, log);
    return;
  }
  st.active = AppCommandState::Kind::Delete;
  st.selBoxWaitingSecond = false;
  log.push_back("DELETE — click two corners to window-select objects to erase. ESC cancels.");
}

void StartZoomExtentsCommand(AppCommandState& st, std::vector<std::string>& log) {
  using K = AppCommandState::Kind;
  if (st.active != K::None) {
    log.push_back("ZOOM EXTENTS — finish or cancel the active command first.");
    return;
  }
  ClearPendingViewportZoom(st);
  st.pendingZoomExtents = true;
}

void StartZoomWindowCommand(AppCommandState& st, std::vector<std::string>& log) {
  using K = AppCommandState::Kind;
  if (st.active != K::None && st.active != K::Zoom) {
    log.push_back("ZOOM WINDOW — finish or cancel the active command first.");
    return;
  }
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  ResetModifyRotateDraft(st);
  st.active = K::Zoom;
  st.selBoxWaitingSecond = false;
  log.push_back(
      "ZOOM WINDOW — click two corners (crossing window corners use cursor position, not object snap). ESC "
      "cancels.");
}

void StartPanCommand(AppCommandState& st, std::vector<std::string>& log) {
  using K = AppCommandState::Kind;
  // PAN coexists with any view state but is its own command; cancel an in-progress draw/edit first.
  if (st.active != K::None && st.active != K::Pan) {
    log.push_back("PAN — finish or cancel the active command first.");
    return;
  }
  ResetAllCadDraftTools(st);
  ResetModifyRotateDraft(st);
  st.active      = K::Pan;
  st.lastCommand = K::Pan;
  st.selBoxWaitingSecond = false;
  log.push_back("PAN — drag with the left mouse button. Press Esc, Enter, or right-click to exit.");
}

void StartOrbitCommand(AppCommandState& st, std::vector<std::string>& log) {
  using K = AppCommandState::Kind;
  if (st.active != K::None && st.active != K::Orbit) {
    log.push_back("ORBIT — finish or cancel the active command first.");
    return;
  }
  // Paper space is 2D by definition (ADR-025 (g)) — a sheet has no orientation to tumble, and
  // letting it tilt would put the plot geometry somewhere the plot cannot follow.
  if (st.activeSpaceIndex != kModelSpaceIndex) {
    log.push_back("ORBIT — model space only; a paper sheet is 2D.");
    return;
  }
  ResetAllCadDraftTools(st);
  ResetModifyRotateDraft(st);
  st.active      = K::Orbit;
  st.lastCommand = K::Orbit;
  st.selBoxWaitingSecond = false;
  log.push_back("ORBIT — drag with the left mouse button to tumble. Press Esc, Enter, or right-click to exit.");
}

// ---------------------------------------------------------------------------
// Object isolation (REQ-084 (d) / ADR-034)
// ---------------------------------------------------------------------------

const EntityAttributes* CadEntityAttrsForSelected(const AppCommandState& st, const SelectedEntity& e) {
  using T = SelectedEntity::Type;
  if (e.index < 0)
    return nullptr;
  const size_t i = static_cast<size_t>(e.index);
  auto at = [i](const std::vector<EntityAttributes>& v) -> const EntityAttributes* {
    return i < v.size() ? &v[i] : nullptr;
  };
  switch (e.type) {
  case T::LineSeg:      return at(st.userLineAttrs);
  case T::Circle:       return at(st.userCircleAttrs);
  case T::Polyline:     return at(st.userPolylineAttrs);
  case T::Arc:          return at(st.userArcAttrs);
  case T::Ellipse:      return at(st.userEllAttrs);
  case T::Annotation:   return at(st.cadAnnotationAttrs);
  case T::FilledRegion: return at(st.cadFilledRegionAttrs);
  case T::Mesh:         return at(st.cadMeshAttrs);
  case T::FeatureLine:  return at(st.featureLineAttrs);  // REQ-087 — layer, colour, and its stable id
  // REQ-068 / ADR-036 (a). A surface has carried an EntityAttributes since the store was written —
  // it simply had no SelectedEntity::Type to be reached through, and no EntityKind, so its `id` was
  // never assigned. Adding it here is what gives a surface REQ-084 isolation gating and Properties
  // layer/colour, both inherited rather than re-implemented.
  case T::Surface:      return at(st.cadSurfaceAttrs);
  // Survey points and PDF underlays carry no EntityAttributes and are out of REQ-084's scope.
  default:              return nullptr;
  }
}

bool CadSelectedEntityHidden(const AppCommandState& st, const SelectedEntity& e) {
  if (st.hiddenEntityIds.empty())
    return false;
  const EntityAttributes* a = CadEntityAttrsForSelected(st, e);
  return a && CadEntityIdHidden(&st.hiddenEntityIds, a->id);
}

namespace {

/// Ids of the entity types isolation covers, in one place so "everything" and "the selection"
/// can never disagree about what the set of isolatable objects is.
void CollectIsolatableIds(const AppCommandState& st, std::vector<std::uint64_t>* out) {
  auto take = [out](const std::vector<EntityAttributes>& v) {
    for (const EntityAttributes& a : v)
      if (a.id != 0)
        out->push_back(a.id);
  };
  take(st.userLineAttrs);
  take(st.userCircleAttrs);
  take(st.userPolylineAttrs);
  take(st.userArcAttrs);
  take(st.userEllAttrs);
  take(st.cadAnnotationAttrs);
  take(st.cadFilledRegionAttrs);
  take(st.cadMeshAttrs);
}

/// Ids of the current selection that isolation can act on, plus how many picks it had to skip
/// (survey points / PDF underlays), so the caller can say so rather than silently dropping them.
void CollectSelectedIsolatableIds(const AppCommandState& st, std::vector<std::uint64_t>* out, int* outSkipped) {
  *outSkipped = 0;
  for (const SelectedEntity& e : st.selection) {
    const EntityAttributes* a = CadEntityAttrsForSelected(st, e);
    if (a && a->id != 0)
      out->push_back(a->id);
    else
      ++*outSkipped;
  }
  *outSkipped += static_cast<int>(st.selectedSurveyPointIndices.size());
}

void SortUniqueIds(std::vector<std::uint64_t>* v) {
  std::sort(v->begin(), v->end());
  v->erase(std::unique(v->begin(), v->end()), v->end());
}

/// Isolation hides objects, so leaving them selected would keep grips and the modify commands
/// pointed at geometry the user can no longer see.
void DropHiddenFromSelection(AppCommandState& st) {
  auto& sel = st.selection;
  sel.erase(std::remove_if(sel.begin(), sel.end(),
                           [&](const SelectedEntity& e) { return CadSelectedEntityHidden(st, e); }),
            sel.end());
}

void ReportSkippedPicks(int skipped, std::vector<std::string>& log) {
  if (skipped > 0)
    log.push_back("  (" + std::to_string(skipped) +
                  " selected item(s) skipped — survey points and PDF underlays are not isolatable.)");
}

} // namespace

void IsolateSelectedObjects(AppCommandState& st, std::vector<std::string>& log) {
  EnsureEntityIds(st);  // an entity created this frame has id 0 until the sweep runs
  std::vector<std::uint64_t> keep;
  int skipped = 0;
  CollectSelectedIsolatableIds(st, &keep, &skipped);
  if (keep.empty()) {
    log.push_back("ISOLATEOBJECTS — nothing isolatable is selected; nothing hidden.");
    ReportSkippedPicks(skipped, log);
    return;
  }
  SortUniqueIds(&keep);

  std::vector<std::uint64_t> all;
  CollectIsolatableIds(st, &all);

  st.hiddenEntityIds = CadIsolationHiddenSet(std::move(all), keep);  // sorted, as the gates require
  BumpCadGpuCache(st);
  log.push_back("ISOLATEOBJECTS — " + std::to_string(keep.size()) + " object(s) isolated, " +
                std::to_string(st.hiddenEntityIds.size()) + " hidden.");
  ReportSkippedPicks(skipped, log);
}

void HideSelectedObjects(AppCommandState& st, std::vector<std::string>& log) {
  EnsureEntityIds(st);
  std::vector<std::uint64_t> hide;
  int skipped = 0;
  CollectSelectedIsolatableIds(st, &hide, &skipped);
  if (hide.empty()) {
    log.push_back("HIDEOBJECTS — nothing isolatable is selected; nothing hidden.");
    ReportSkippedPicks(skipped, log);
    return;
  }
  const size_t before = st.hiddenEntityIds.size();
  st.hiddenEntityIds.insert(st.hiddenEntityIds.end(), hide.begin(), hide.end());
  SortUniqueIds(&st.hiddenEntityIds);
  DropHiddenFromSelection(st);
  BumpCadGpuCache(st);
  log.push_back("HIDEOBJECTS — " + std::to_string(st.hiddenEntityIds.size() - before) +
                " object(s) hidden (" + std::to_string(st.hiddenEntityIds.size()) + " hidden in total).");
  ReportSkippedPicks(skipped, log);
}

void EndObjectIsolation(AppCommandState& st, std::vector<std::string>& log) {
  if (st.hiddenEntityIds.empty()) {
    log.push_back("UNISOLATEOBJECTS — nothing was hidden.");
    return;
  }
  const size_t n = st.hiddenEntityIds.size();
  st.hiddenEntityIds.clear();
  BumpCadGpuCache(st);
  log.push_back("UNISOLATEOBJECTS — " + std::to_string(n) + " object(s) shown.");
}

Viewport* CurrentViewport(AppCommandState& st) {
  if (st.activeSpaceIndex < 0 || st.activeSpaceIndex >= static_cast<int>(st.paperLayouts.size()))
    return nullptr;
  PaperLayout& L = st.paperLayouts[static_cast<size_t>(st.activeSpaceIndex)];
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
  ResetModifyRotateDraft(st);
  st.active      = freeze ? K::VpFreeze : K::VpThaw;
  st.lastCommand = st.active;
  st.selBoxWaitingSecond = false;
  log.push_back(std::string(name) + " — select objects to " + (freeze ? "freeze" : "thaw") +
                " their layer in this viewport. Press Esc or Enter to exit.");
}

void StartVpFreezeCommand(AppCommandState& st, std::vector<std::string>& log) { StartVpFreezeThaw(st, true, log); }
void StartVpThawCommand(AppCommandState& st, std::vector<std::string>& log) { StartVpFreezeThaw(st, false, log); }

void ProcessPendingViewportZoom(AppCommandState& st, double* panX, double* panY, float* zoom, int fbW, int fbH,
                                float viewportAspect, std::vector<std::string>& log) {
  if (fbW <= 0 || fbH <= 0)
    return;
  if (st.pendingZoomExtents) {
    double mnX = 0.;
    double mxX = 0.;
    double mnY = 0.;
    double mxY = 0.;
    int skipped = 0;
    if (!ComputeRobustWorldExtents(st, &mnX, &mxX, &mnY, &mxY, &skipped)) {
      st.pendingZoomExtents = false;
      log.push_back("ZOOM EXTENTS — nothing to frame.");
      return;
    }
    ApplyViewportZoomToWorldRect(mnX, mxX, mnY, mxY, &st.viewportPanX, &st.viewportPanY, &st.viewportZoom, fbW, fbH,
                                 viewportAspect);
    BumpCadGpuCache(st);
    if (panX)
      *panX = st.viewportPanX;
    if (panY)
      *panY = st.viewportPanY;
    if (zoom)
      *zoom = st.viewportZoom;
    st.pendingZoomExtents = false;
    char buf[256];
    const double spanX = mxX - mnX;
    const double spanY = mxY - mnY;
    std::snprintf(buf, sizeof(buf),
                  "Zoom extents applied — span %.6g x %.6g (local %.6g..%.6g, %.6g..%.6g) zoom=%.6g skipped=%d.",
                  spanX, spanY, mnX, mxX, mnY, mxY, static_cast<double>(st.viewportZoom), skipped);
    log.push_back(buf);
    return;
  }
  if (st.pendingZoomWindow) {
    st.pendingZoomWindow = false;
    ApplyViewportZoomToWorldRect(static_cast<double>(st.pendingZoomMnX), static_cast<double>(st.pendingZoomMxX),
                                 static_cast<double>(st.pendingZoomMnY), static_cast<double>(st.pendingZoomMxY),
                                 &st.viewportPanX, &st.viewportPanY, &st.viewportZoom, fbW, fbH, viewportAspect);
    if (panX)
      *panX = st.viewportPanX;
    if (panY)
      *panY = st.viewportPanY;
    if (zoom)
      *zoom = st.viewportZoom;
    BumpCadGpuCache(st);
    log.push_back("Zoom window applied.");
  }
}

void BeginSelectionBoxCorner(AppCommandState& st, float wx, float wy, float anchorScreenX,
                             float anchorScreenY) {
  AbortMtextGripInteraction(st);
  ClearDimGripInteraction(st);
  st.selBoxAnchorX = wx;
  st.selBoxAnchorY = wy;
  st.selBoxAnchorScreenX = anchorScreenX;
  st.selBoxAnchorScreenY = anchorScreenY;
  st.selBoxWaitingSecond = true;
}

void StartMoveCommand(AppCommandState& st, std::vector<std::string>& log) {
  if (st.activeSpaceIndex != kModelSpaceIndex && !InFloatingModelSpace(st)) {  // REQ-035: move viewports
    StartPaperMoveCopyViewports(st, /*copy=*/false, log);
    return;
  }
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  st.active = AppCommandState::Kind::Move;
  st.lastCommand = AppCommandState::Kind::Move;
  st.modifyPhase = AppCommandState::ModifyPhase::PickSelection;
  st.selBoxWaitingSecond = false;
  if (!st.selection.empty() || !st.selectedSurveyPointIndices.empty()) {
    st.modifyPhase = AppCommandState::ModifyPhase::NeedBase;
    log.push_back("MOVE — specify base point (click or type X,Y). ESC to cancel.");
  } else
    log.push_back(
        "MOVE — click two corners to window-select objects, then base point and destination. ESC cancels.");
}

void StartCopyCommand(AppCommandState& st, std::vector<std::string>& log) {
  if (st.activeSpaceIndex != kModelSpaceIndex && !InFloatingModelSpace(st)) {  // REQ-035: copy viewports
    StartPaperMoveCopyViewports(st, /*copy=*/true, log);
    return;
  }
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  st.active = AppCommandState::Kind::Copy;
  st.lastCommand = AppCommandState::Kind::Copy;
  st.modifyPhase = AppCommandState::ModifyPhase::PickSelection;
  st.selBoxWaitingSecond = false;
  if (!st.selection.empty() || !st.selectedSurveyPointIndices.empty()) {
    st.modifyPhase = AppCommandState::ModifyPhase::NeedBase;
    log.push_back("COPY — specify base point (click or type X,Y). ESC to cancel.");
  } else
    log.push_back(
        "COPY — click two corners to window-select objects, then base point and destination. ESC cancels.");
}

void StartRotateCommand(AppCommandState& st, std::vector<std::string>& log) {
  if (st.activeSpaceIndex != kModelSpaceIndex && !InFloatingModelSpace(st)) {  // REQ-037: rotate paper geometry
    if (st.selectedPaperEntities.empty()) {
      log.push_back("ROTATE — select paper object(s) first.");
      return;
    }
    st.active = AppCommandState::Kind::None;  // paper-space edit ops are not a model command
    st.paperRotatePhase = 1;
    log.push_back("ROTATE — click the base point (Esc to cancel).");
    return;
  }
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  st.active = AppCommandState::Kind::Rotate;
  st.rotatePhase = AppCommandState::RotatePhase::PickSelection;
  st.rotateBaseX = st.rotateBaseY = 0.f;
  st.rotateCopyMode = false;
  st.pendingSurveyDupIsRotate = false;
  st.selBoxWaitingSecond = false;
  if (!st.selection.empty() || !st.selectedSurveyPointIndices.empty()) {
    st.rotatePhase = AppCommandState::RotatePhase::NeedBase;
    log.push_back("ROTATE — specify base point (click or type X,Y). ESC to cancel.");
  } else
    log.push_back(
        "ROTATE — window-select (two clicks), base point, then ° clockwise from north / DMS, or R reference. ESC.");
}

void StartScaleCommand(AppCommandState& st, std::vector<std::string>& log) {
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  st.active = AppCommandState::Kind::Scale;
  st.modifyPhase = AppCommandState::ModifyPhase::PickSelection;
  st.selBoxWaitingSecond = false;
  st.scaleRefDist = 1.f;
  st.scalePhase = AppCommandState::ScalePhase::FactorPick;
  if (!st.selection.empty() || !st.selectedSurveyPointIndices.empty()) {
    st.modifyPhase = AppCommandState::ModifyPhase::NeedBase;
    log.push_back("SCALE — specify base point (click or type X,Y). ESC to cancel.");
  } else
    log.push_back(
        "SCALE — window-select (two clicks), base point, then scale: second point or factor (>0), or R for "
        "two-point reference length then new length (type or two picks). ESC cancels.");
}

void CancelActiveCommand(AppCommandState& st, std::vector<std::string>& log) {
  if (st.active == AppCommandState::Kind::None)
    return;
  ClearPendingOneShotObjectSnap(st);
  const AppCommandState::Kind prev = st.active;
  if (st.active == AppCommandState::Kind::Line)
    log.push_back("LINE canceled.");
  else if (st.active == AppCommandState::Kind::Circle)
    log.push_back("CIRCLE canceled.");
  else if (st.active == AppCommandState::Kind::Polyline)
    log.push_back("POLYLINE canceled.");
  else if (st.active == AppCommandState::Kind::FeatureLine)
    log.push_back("FEATURELINE canceled.");  // REQ-201: was silent
  else if (st.active == AppCommandState::Kind::Arc)
    log.push_back("ARC canceled.");
  else if (st.active == AppCommandState::Kind::Ellipse)
    log.push_back("ELLIPSE canceled.");
  else if (st.active == AppCommandState::Kind::Rect)
    log.push_back("RECT canceled.");
  else if (st.active == AppCommandState::Kind::TrimState)
    log.push_back("TRIMSTATE unchanged (" + std::to_string(st.trimState) + ").");
  else if (st.active == AppCommandState::Kind::Elev)
    log.push_back("Elevation unchanged.");
  else if (st.active == AppCommandState::Kind::Text)
    log.push_back("TEXT canceled.");
  else if (st.active == AppCommandState::Kind::Mtext)
    log.push_back("MTEXT canceled.");
  else if (st.active == AppCommandState::Kind::DimAligned)
    log.push_back("DIMALIGNED canceled.");
  else if (st.active == AppCommandState::Kind::DimLinear)
    log.push_back("DIMLINEAR canceled.");
  else if (st.active == AppCommandState::Kind::DimAngular)
    log.push_back("DIMANGULAR canceled.");
  else if (st.active == AppCommandState::Kind::Move)
    log.push_back("MOVE canceled.");
  else if (st.active == AppCommandState::Kind::Copy)
    log.push_back("COPY canceled.");
  else if (st.active == AppCommandState::Kind::Rotate)
    log.push_back("ROTATE canceled.");
  else if (st.active == AppCommandState::Kind::Scale)
    log.push_back("SCALE canceled.");
  else if (st.active == AppCommandState::Kind::Delete)
    log.push_back("DELETE canceled.");
  else if (st.active == AppCommandState::Kind::Join)
    log.push_back("JOIN canceled.");
  else if (st.active == AppCommandState::Kind::Trim)
    log.push_back("TRIM canceled.");
  else if (st.active == AppCommandState::Kind::Offset)
    log.push_back("OFFSET canceled.");
  else if (st.active == AppCommandState::Kind::IdPoint)
    log.push_back("ID canceled.");
  else if (st.active == AppCommandState::Kind::SurveyInverse)
    log.push_back("INVERSE canceled.");
  else if (st.active == AppCommandState::Kind::Zoom)
    log.push_back("ZOOM WINDOW canceled.");
  else if (st.active == AppCommandState::Kind::Pan)
    log.push_back("PAN — exited.");
  else if (st.active == AppCommandState::Kind::Orbit)
    log.push_back("ORBIT — exited.");
  else if (st.active == AppCommandState::Kind::VpFreeze)
    log.push_back("VPFREEZE — exited.");
  else if (st.active == AppCommandState::Kind::VpThaw)
    log.push_back("VPTHAW — exited.");
  else if (st.active == AppCommandState::Kind::PdfAttach)
    log.push_back("PDFATTACH canceled.");
  else if (st.active == AppCommandState::Kind::Paste)
    log.push_back("PASTE canceled.");
  else if (st.active == AppCommandState::Kind::PaperRectViewport) {
    st.paperVpPhase = 0;
    log.push_back("Rectangular viewport canceled.");
  }
  else if (st.active == AppCommandState::Kind::Align) {
    st.alignControlPts.clear();
    st.alignSelectionSnapshot.clear();
    st.alignSurveySnapshot.clear();
    st.alignHasSelection    = false;
    st.alignPhase           = AppCommandState::AlignPhase::PickSrc;
    st.selBoxWaitingSecond  = false;
    log.push_back("ALIGN canceled.");
  }
  st.active = AppCommandState::Kind::None;
  if (prev == AppCommandState::Kind::Offset)
    OffsetCmd::ResetOffsetDraft(st);
  st.linePhase = AppCommandState::LinePhase::NeedFirstPoint;
  ResetSegmentAngleLock(st);
  st.trimPhase = AppCommandState::TrimPhase::SelectCuttingEdges;
  st.trimCutters.clear();
  ResetAllCadDraftTools(st);
  ResetModifyRotateDraft(st);
  st.selBoxWaitingSecond = false;
  st.copySurveyDupModalOpen = false;
  st.copySurveyDupModalOpenRequested = false;
  ClearEntityGripInteraction(st);
  ClearDimGripInteraction(st);
  if (prev == AppCommandState::Kind::Zoom)
    ClearPendingViewportZoom(st);
}

void ApplyCopySurveyDuplicateModalResult(AppCommandState& st, bool applySurveyDup, std::vector<std::string>& log) {
  if (!st.copySurveyDupModalOpen)
    return;
  st.copySurveyDupModalOpen = false;
  st.copySurveyDupModalOpenRequested = false;
  const bool wasRotateDup = st.pendingSurveyDupIsRotate;
  st.pendingSurveyDupIsRotate = false;
  if (applySurveyDup) {
    if (wasRotateDup)
      DuplicateSelectedSurveyPointsRotated(st, st.pendingRotateCopyBx, st.pendingRotateCopyBy, st.pendingRotateCopyRad,
                                           st.copySurveyDuplicatePolicy, log);
    else
      DuplicateSelectedSurveyPointsTranslated(st, st.pendingCopyDx, st.pendingCopyDy, st.copySurveyDuplicatePolicy,
                                              log);
  } else if (wasRotateDup)
    log.push_back("ROTATE COPY survey — skipped (CAD copy kept).");
  else
    log.push_back("COPY survey — skipped (CAD copy kept).");
}

static void FinishEllipseFromRatio(AppCommandState& st, float ratio, std::vector<std::string>& log) {
  using K = AppCommandState::Kind;
  using EP = AppCommandState::EllipsePhase;
  if (st.active != K::Ellipse || st.ellPhase != EP::WaitRatio)
    return;
  if (ratio <= 1e-8f || ratio > 1.f + 1e-4f) {
    log.push_back("ELLIPSE — ratio must be in (0, 1].");
    return;
  }
  const float vx0 = st.ellMajEx - st.ellCx;
  const float vy0 = st.ellMajEy - st.ellCy;
  const float ma = std::hypot(vx0, vy0);
  if (ma < 1e-8f) {
    log.push_back("ELLIPSE — major axis too short.");
    return;
  }
  CadEllipse ell{};
  ell.cx = st.ellCx;
  ell.cy = st.ellCy;
  ell.majVx = vx0;
  ell.majVy = vy0;
  ell.ratio = ratio;
  ell.z = CadCommitElevation(st);  // lands on the active work plane (REQ-058)
  PushUndoSnapshot(st, "Ellipse");
  st.userEllipses.push_back(ell);
  st.userEllAttrs.push_back(MakeNewEntityAttrs(st));
  BumpCadGpuCache(st);
  st.active = K::None;
  ResetEllipseDraft(st);
  log.push_back("ELLIPSE complete.");
}

static void CommitPolylineDraft(AppCommandState& st, bool closed, std::vector<std::string>& log) {
  const size_t nvert = st.polylineDraftVerts.size() / 3;
  if (closed) {
    if (nvert < 3) {
      log.push_back("POLYLINE CLOSE — need at least three vertices.");
      return;
    }
  } else if (nvert < 2) {
    log.push_back("POLYLINE — need at least two vertices (use END to finish open).");
    return;
  }
  PushUndoSnapshot(st, closed ? "Polyline (closed)" : "Polyline");
  if (st.userPolylineOffsets.empty())
    st.userPolylineOffsets.push_back(0);
  const int baseVert = st.userPolylineOffsets.back();
  st.userPolylineVerts.insert(st.userPolylineVerts.end(), st.polylineDraftVerts.begin(), st.polylineDraftVerts.end());
  st.userPolylineOffsets.push_back(baseVert + static_cast<int>(nvert));
  st.userPolylineClosed.push_back(static_cast<uint8_t>(closed ? 1 : 0));
  st.userPolylineAttrs.push_back(MakeNewEntityAttrs(st));
  BumpCadGpuCache(st);
  st.active = AppCommandState::Kind::None;
  ResetPolylineDraft(st);
  log.push_back(closed ? "POLYLINE closed." : "POLYLINE complete.");
}

bool SubmitLineVertex(AppCommandState& st, float x, float y, std::vector<std::string>& log) {
  if (st.active != AppCommandState::Kind::Line)
    return false;

  if (st.linePhase == AppCommandState::LinePhase::NeedFirstPoint) {
    st.anchorX = x;
    st.anchorZ = CadCommitElevation(st);
    st.anchorY = y;
    st.linePhase = AppCommandState::LinePhase::NeedNextPoint;
    log.push_back("First point set. Next: click; X, Y; @dx,dy; [A]zimuth, [2P];");
    return true;
  }

  PushUndoSnapshot(st, "Line segment");
  if (PaperLayout* L = ActivePaperGeometryTarget(st)) {
    // Paper-space LINE (REQ-037): anchor/x,y are paper inches; commit to the layout's paper store.
    L->paperLines.push_back(st.anchorX);
    L->paperLines.push_back(st.anchorY);
    L->paperLines.push_back(0.f);
    L->paperLines.push_back(x);
    L->paperLines.push_back(y);
    L->paperLines.push_back(0.f);
    L->paperLineAttrs.push_back(MakeNewEntityAttrs(st));
  } else {
    // New geometry lands on the active work plane (REQ-058); ELEV moves that plane, and the
    // default is world XY, so this is 0 until the user changes it.
    st.userLinesFlat.push_back(st.anchorX);
    st.userLinesFlat.push_back(st.anchorY);
    st.userLinesFlat.push_back(st.anchorZ);          // the anchor's own elevation
    st.userLinesFlat.push_back(x);
    st.userLinesFlat.push_back(y);
    st.userLinesFlat.push_back(CadCommitElevation(st));  // this end's (snap overrides ELEV)
    st.userLineAttrs.push_back(MakeNewEntityAttrs(st));
  }
  BumpCadGpuCache(st);

  st.anchorX = x;
  st.anchorZ = CadCommitElevation(st);
  st.anchorY = y;
  ++st.lineDraftSegments;
  log.push_back("Segment added — next point or ESC to finish.");
  return true;
}

bool SubmitPolylineVertex(AppCommandState& st, float x, float y, std::vector<std::string>& log) {
  if (st.active != AppCommandState::Kind::Polyline)
    return false;

  using PP = AppCommandState::PolylinePhase;

  // The vertex elevation, in priority order (REQ-085): an elevation typed on this line, then the
  // ordinary rule — the snapped point's own Z, else the work plane (REQ-058). A typed Z is consumed
  // here so it cannot leak into the NEXT vertex, which would silently repeat the last elevation.
  float vz = CadCommitElevation(st);
  if (st.polylineTypedZValid) {
    vz = st.polylineTypedZRelative ? st.anchorZ + st.polylineTypedZ : st.polylineTypedZ;
    st.polylineTypedZValid = false;
    st.polylineTypedZRelative = false;
  }
  const char* who = st.polylineDraft3d ? "3DPOLY" : "POLYLINE";

  if (st.polylinePhase == PP::NeedFirstPoint) {
    st.polylineDraftVerts.clear();
    st.polylineDraftVerts.push_back(x);
    st.polylineDraftVerts.push_back(y);
    st.polylineDraftVerts.push_back(vz);
    st.anchorX = x;
    st.anchorZ = vz;
    st.anchorY = y;
    st.polyFirstX = x;
    st.polyFirstY = y;
    st.polyDraftSegments = 0;
    st.polylinePhase = PP::NeedNextPoint;
    if (st.polylineDraft3d) {
      // Reported for the same reason every later vertex is: the elevation is the thing 3DPOLY exists
      // to control, and it is otherwise invisible until the drawing is saved (REQ-201).
      char buf[96];
      std::snprintf(buf, sizeof(buf), "3DPOLY first vertex at elevation %.3f.", static_cast<double>(vz));
      log.push_back(buf);
    }
    log.push_back(std::string(who) +
                  " — next vertex (A + bearing then distance like LINE), CLOSE / END, or ESC.");
    return true;
  }

  st.polylineDraftVerts.push_back(x);
  st.polylineDraftVerts.push_back(y);
  st.polylineDraftVerts.push_back(vz);
  ++st.polyDraftSegments;
  st.anchorX = x;
  st.anchorZ = vz;
  st.anchorY = y;
  if (st.polylineDraft3d) {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "3DPOLY vertex added at elevation %.3f.", static_cast<double>(vz));
    log.push_back(buf);
  } else {
    log.push_back("POLYLINE vertex added.");
  }
  return true;
}

// localX/localY are LOCAL storage coordinates, not world — see the declaration in CadCommands.hpp.
// The `wx`/`wy` spelling inside SubmitViewportPickImpl and its callees is kept deliberately: those
// names appear hundreds of times across the pick handlers, and renaming them would bury this
// two-line clarification in an unreviewable diff. The space is stated here, at the entry point, which
// is where a caller looks.
void SubmitViewportPick(AppCommandState& st, float localX, float localY, std::vector<std::string>& log,
                         bool windowSelectionSubtract, bool fenceLeftToRightWindowMode) {
  ClearPendingOneShotObjectSnap(st);
  SubmitViewportPickImpl(st, localX, localY, log, windowSelectionSubtract, fenceLeftToRightWindowMode);
}

// REQ-101 / decision D-2026-08-17-b: establish the document origin BEFORE a typed coordinate of
// state-plane magnitude is narrowed into the float local frame.
//
// Why here, and not at the commit sites: this is the single place every typed coordinate passes
// through before any command interprets it, so one call covers LINE, POLYLINE, RECT, CIRCLE and every
// other command with no signature change and no `const` removed from the 29 ParseStoragePoint callers.
// A "parse" function that silently rebased the whole drawing would also be the wrong owner for a
// document-wide mutation.
//
// Why it fires at most once per drawing: MaybeRebaseLargeCoordinates' own guard is
// `worldDocumentOrigin != (0,0) -> return false`, so once a frame is established this is inert. That
// matters more than it looks — re-centring the origin as the drawing grew would round every stored
// coordinate through float again on each move, which is exactly the compounding drift REQ-079's
// idempotence condition forbids. One establishment, never a moving target.
//
// The origin deliberately becomes the FIRST large coordinate's neighbourhood rather than the eventual
// extents centre. Precision only needs the locals to be small, not centred: a 5,000 ft site whose
// origin sits at one corner still stores locals under 5,000, which float represents to ~1e-4 ft. This
// also matches what the DXF importer already does when its header extents are untrustworthy.
static void MaybeEstablishDocumentOriginFromTypedPoint(AppCommandState& st, const std::string& line,
                                                       std::vector<std::string>& log) {
  if (st.worldDocumentOriginX != 0.0 || st.worldDocumentOriginY != 0.0)
    return;  // frame already established — inert, and must stay inert (see above)
  double wx = 0.;
  double wy = 0.;
  // Absolute points only. A relative `@dx,dy` is resolved against an anchor that is already in the
  // current frame, so it cannot be the first thing that needs a new one.
  if (!ParseWorldPointD(line, &wx, &wy, /*allowRelative=*/false, 0., 0.))
    return;  // not a coordinate at all — a command name, a distance, a bare Enter
  const double mag = std::max(std::fabs(wx), std::fabs(wy));
  if (mag < CadCoord::kLargeCoordinateRebaseThreshold)
    return;  // small enough that the float local frame is already precise
  // And an upper bound: past this a value is not a coordinate, and building a frame around it would
  // make garbage representable instead of refused. See kMaxEstablishableOriginMagnitude.
  if (mag > CadCoord::kMaxEstablishableOriginMagnitude)
    return;
  CadCoord::ApplyDocumentOriginRebase(st, wx, wy, &log);
}

void ProcessCommandLineSubmit(char* cmdBuf, int cmdBufSize, AppCommandState& st, std::vector<std::string>& log) {
  [&]() {
  (void)cmdBufSize;
  std::string line = StringUtil::trimCopy(std::string(cmdBuf));

  // Before anything interprets this text (REQ-101). Placed ahead of dispatch so the frame is already
  // able to represent the value by the time a command's ParseStoragePoint narrows it.
  MaybeEstablishDocumentOriginFromTypedPoint(st, line, log);

  // Record the entry for the command bar's history dropdown (REQ-040): newest last,
  // skip blanks and consecutive duplicates, cap at 20.
  if (!line.empty() && (st.cmdEnteredHistory.empty() || st.cmdEnteredHistory.back() != line)) {
    st.cmdEnteredHistory.push_back(line);
    if (st.cmdEnteredHistory.size() > 20)
      st.cmdEnteredHistory.erase(st.cmdEnteredHistory.begin());
  }

  using K = AppCommandState::Kind;
  using LP = AppCommandState::LinePhase;
  using PP = AppCommandState::PolylinePhase;
  using SAP = AppCommandState::SegmentAnglePickPhase;

  // Grip stretch, direct-distance entry (REQ-047): with a grip armed there is no active command, so a bare
  // number would otherwise be dispatched as a command name. Consume it here and pin the grip that far along
  // the ORTHO axis the crosshair indicates. Anything else falls through to normal command dispatch, so the
  // command line still works while a grip is up.
  if (st.entityGripMoveActive) {
    float gripDist = 0.f;
    if (ParseSingleFloatToken(line, &gripDist)) {
      if (std::fabs(gripDist) < 1e-20f) {
        log.push_back("Grip — distance must be non-zero.");
        return;
      }
      float cursorLocalX = 0.f;
      float cursorLocalY = 0.f;
      CadCoord::LocalFromWorld(st, static_cast<double>(st.uiCursorWorldX), static_cast<double>(st.uiCursorWorldY),
                               &cursorLocalX, &cursorLocalY);
      float ux = 0.f;
      float uy = 0.f;
      if (!OrthoUnitTowardPoint(st.entityGripAnchorX, st.entityGripAnchorY, cursorLocalX, cursorLocalY, &ux, &uy)) {
        log.push_back("Grip distance needs a direction — move the crosshair off the grip, then enter a distance.");
        return;
      }
      st.entityGripTypedX = st.entityGripAnchorX + ux * gripDist;
      st.entityGripTypedY = st.entityGripAnchorY + uy * gripDist;
      st.entityGripTypedDistanceValid = true;
      // Arming the grip already pushed the pre-drag undo snapshot, so the typed placement just overwrites
      // whatever the live drag had put there — one undo returns the entity to where it started.
      ApplyEntityGripPoint(st, st.entityGripTypedX, st.entityGripTypedY);
      char gripMsg[128];
      std::snprintf(gripMsg, sizeof(gripMsg), "Grip stretched %.6g %s.",
                    static_cast<double>(std::fabs(gripDist)),
                    ux > 0.f ? "right" : (ux < 0.f ? "left" : (uy > 0.f ? "up" : "down")));
      log.push_back(gripMsg);
      ClearEntityGripInteraction(st);
      BumpCadGpuCache(st);
      return;
    }
  }

  const bool segPickNeedAdjust =
      ((st.active == K::Line && st.linePhase == LP::NeedNextPoint) ||
       (st.active == K::Polyline && st.polylinePhase == PP::NeedNextPoint)) &&
      st.segmentAnglePickPhase == SAP::WaitAdjustOrCommit;

  if (segPickNeedAdjust) {
    if (line.empty()) {
      CommitSegmentAnglePickLock(st, log);
      return;
    }
    const std::string t = StringUtil::trimCopy(line);
    if (!t.empty() && (t[0] == '+' || t[0] == '-')) {
      float dlt = 0.f;
      if (!ParseAngleDegreesInternal(t, &dlt)) {
        log.push_back("Bearing pick — invalid adjustment (decimal/DMS). Blank Enter locks; +/- adds turn.");
        return;
      }
      st.segmentPickDraftBearingDeg = NormalizeBearingDegreesCwNorth(st.segmentPickDraftBearingDeg + dlt);
      CommitSegmentAnglePickLock(st, log);
      return;
    }
    log.push_back("Bearing pick — blank Enter to lock, or +90 / -45 first (° clockwise from north).");
    return;
  }

  const bool linePolyNextNeedPoint =
      (st.active == K::Line && st.linePhase == LP::NeedNextPoint) ||
      (st.active == K::Polyline && st.polylinePhase == PP::NeedNextPoint);

  if (linePolyNextNeedPoint && st.segmentAngleKeyboardAwaitBearing) {
    if (line.empty()) {
      st.segmentAngleKeyboardAwaitBearing = false;
      log.push_back("Bearing entry canceled.");
      return;
    }
    float combined = 0.f;
    if (!ParseBearingCwNorthStringWithOptionalDelta(line, &combined, log)) {
      return;
    }
    const float theta = MathAngleRadFromBearingCwNorthDeg(combined);
    st.segmentLockUx = std::cos(theta);
    st.segmentLockUy = std::sin(theta);
    st.segmentAngleLockActive = true;
    st.segmentAnglePickPhase = SAP::Idle;
    st.segmentAngleKeyboardAwaitBearing = false;
    char bufKb[144];
    std::snprintf(bufKb, sizeof(bufKb),
                  "Bearing lock %.6g° clockwise from north — distance (+/- along ray) or click (A clears).",
                  static_cast<double>(combined));
    log.push_back(bufKb);
    return;
  }

  if (st.active == K::Mtext && st.mtextPhase == AppCommandState::MtextPhase::WaitString && !line.empty()) {
    log.push_back("MTEXT — type in the on-screen editor over the box (Ctrl+Enter reformats; Save to place).");
    return;
  }

  if (line.empty()) {
    // TASK-082. FEATURELINE with a point awaiting its elevation: Enter accepts the default shown in
    // the prompt. This has to be handled HERE, not in the FEATURELINE block further down, because a
    // blank line never reaches that block — it is consumed by this one.
    if (st.active == K::FeatureLine && st.featureLinePendingPoint) {
      CommitFeatureLinePendingPoint(st, st.featureLinePendingDefaultZ, log);
      return;
    }
    if (st.active == K::Pan) {
      // Enter (or right-click in Enter mode) exits PAN; Esc exits via CancelActiveCommand.
      st.active = K::None;
      log.push_back("PAN — exited.");
      return;
    }
    if (st.active == K::Orbit) {
      // Same contract as PAN above (REQ-084 (c)).
      st.active = K::None;
      log.push_back("ORBIT — exited.");
      return;
    }
    if (st.active == K::VpFreeze || st.active == K::VpThaw) {
      // Enter exits VPFREEZE/VPTHAW; Esc exits via CancelActiveCommand (REQ-046).
      const char* nm = st.active == K::VpFreeze ? "VPFREEZE" : "VPTHAW";
      st.active = K::None;
      log.push_back(std::string(nm) + " — exited.");
      return;
    }
    if (st.active == K::Line && st.linePhase == AppCommandState::LinePhase::NeedNextPoint) {
      // Blank Enter ends the current line chain; restart LINE for the next one.
      ResetSegmentAngleLock(st);
      st.linePhase = AppCommandState::LinePhase::NeedFirstPoint;
      st.lineDraftSegments = 0;
      log.push_back("LINE — specify first point (click or type X,Y / X Y). ESC to cancel.");
      return;
    }
    if (st.active == K::Trim) {
      using TP = AppCommandState::TrimPhase;
      if (st.trimPhase == TP::SelectCuttingEdges) {
        if (st.trimCutters.empty())
          log.push_back("TRIM — pick at least one cutting edge before pressing Enter.");
        else {
          st.trimPhase = TP::SelectTrimTargets;
          log.push_back("TRIM — click segments to trim (near the piece to remove). Enter when done.");
        }
      } else if (st.trimPhase == TP::CuttingLine_WaitP1 || st.trimPhase == TP::CuttingLine_WaitP2) {
        log.push_back("TRIM — specify cutting-line points in the viewport.");
      } else {
        st.active = K::None;
        st.trimPhase = TP::SelectCuttingEdges;
        st.trimCutters.clear();
        log.push_back("TRIM — finished.");
      }
    } else if (st.active == K::Offset) {
      using OP = AppCommandState::OffsetPhase;
      if (st.offsetPhase == OP::WaitDistanceOrThrough)
        log.push_back("OFFSET — type a positive distance, or pick a through point (line / circle / arc).");
      else if (st.offsetPhase == OP::WaitSidePick)
        log.push_back("OFFSET — pick which side to offset.");
      else
        log.push_back("OFFSET — select an entity in the viewport.");
    } else if (st.active == K::Align) {
      using AP = AppCommandState::AlignPhase;
      if (st.alignPhase == AP::PickSelection) {
        st.alignSelectionSnapshot = st.selection;
        st.alignSurveySnapshot    = st.selectedSurveyPointIndices;
        st.alignHasSelection      = !st.alignSelectionSnapshot.empty() || !st.alignSurveySnapshot.empty();
        st.alignPhase             = AP::PickSrc;
        const int nCad = static_cast<int>(st.alignSelectionSnapshot.size());
        const int nSrv = static_cast<int>(st.alignSurveySnapshot.size());
        if (st.alignHasSelection)
          log.push_back("ALIGN — " + std::to_string(nCad) + " CAD, " + std::to_string(nSrv) +
                        " survey point(s). Pick SOURCE survey point 1 in drawing (snap to it).");
        else
          log.push_back("ALIGN — no selection; all geometry will be transformed. "
                        "Pick SOURCE survey point 1 in drawing (snap to it).");
      } else {
        ExecuteAlignCommand(st, log);
      }
    }
    return;
  }

  if (st.active == K::None) {
    std::istringstream issIdle(StringUtil::trimCopy(std::string(cmdBuf)));
    std::string plotTok;
    issIdle >> plotTok;
    plotTok = StringUtil::toLowerAsciiCopy(plotTok);
    // `TRIMSTATE 1` sets it in one line; a bare `TRIMSTATE` falls through to the registry and prompts.
    if (plotTok == "trimstate") {
      int tv = 0;
      if (issIdle >> tv) {
        ApplyTrimStateValue(st, tv, log);
        return;
      }
    }
    // `IMPORTMODEL [path] [scale] [x] [y] [z]` (REQ-065). Bare form opens the file browser and uses
    // scale 1 at the origin, stating both — the same shape as the other file commands, and the form
    // that makes the import scriptable for verification.
    if (plotTok == "importmodel" || plotTok == "gltf" || plotTok == "import3d") {
      std::string path;
      double scale = 1.0;
      double ix = 0.0;
      double iy = 0.0;
      double iz = 0.0;
      std::string rest;
      std::getline(issIdle, rest);
      rest = StringUtil::trimCopy(rest);
      // A path may contain spaces, so quoted-first: "C:\a b\m.glb" 0.0833 100 200
      if (!rest.empty() && rest.front() == '"') {
        const size_t close = rest.find('"', 1);
        if (close != std::string::npos) {
          path = rest.substr(1, close - 1);
          rest = StringUtil::trimCopy(rest.substr(close + 1));
        }
      } else if (!rest.empty()) {
        std::istringstream ps(rest);
        ps >> path;
        std::getline(ps, rest);
      }
      {
        std::istringstream ns(rest);
        double v = 0.0;
        if (ns >> v) scale = v;
        if (ns >> v) ix = v;
        if (ns >> v) iy = v;
        if (ns >> v) iz = v;
      }
      if (path.empty()) {
        char buf[1024]{};
        if (!BrowseOpenFileGltfUtf8(buf, sizeof(buf))) {
          log.push_back("IMPORTMODEL — cancelled.");
          return;
        }
        path = buf;
        log.push_back("IMPORTMODEL — unit scale 1, insertion 0,0,0. "
                      "Use IMPORTMODEL \"<path>\" <scale> <x> <y> <z> to place it otherwise.");
      }
      if (!std::isfinite(scale) || scale == 0.0) {
        log.push_back("IMPORTMODEL — unit scale must be a non-zero finite number.");
        return;
      }
      ImportGltfModel(st, path, scale, ix, iy, iz, log);
      return;
    }
    // `VS SHADED` in one line; a bare `VISUALSTYLE` reports the current value and lists the options,
    // the same shape as a system-variable prompt (the TRIMSTATE precedent).
    if (plotTok == "visualstyle" || plotTok == "vs" || plotTok == "vscurrent") {
      std::string vsArg;
      if (issIdle >> vsArg) {
        ApplyVisualStyleValue(st, vsArg, log);
      } else {
        log.push_back(std::string("Visual style = ") + VisualStyleName(st.viewportVisualStyle) +
                      ". Usage: VS 2D | HIDDEN | SHADED.");
      }
      return;
    }
    // `BENCH` runs the REQ-100 frame-budget measurement at the budget's own density; `BENCH <segs>`
    // and `BENCH <segs> <frames>` override it for a quick check or a longer sample.
    if (plotTok == "bench") {
      int segs = 250000;  // REQ-100: the density of a real topo with contours
      int frames = 900;   // ~1.25 turns of the scripted orbit after warm-up
      st.bench.surfacePointCount = 0;
      st.bench.surfaceTriangleCount = 0;
      st.bench.meshTriangleCount = 0;

      // `BENCH SURFACE [points] [frames]` selects the surface cost profile (REQ-100 as amended,
      // ADR-028), `BENCH MESH [triangles] [frames]` the shaded-mesh one (REQ-100 (b)). All three
      // are measured separately because none predicts the others: a surface's edges are regenerated
      // display geometry, and a shaded triangle is depth-tested and lit where a line segment is not.
      std::string benchArg;
      const std::streampos afterTok = issIdle.tellg();
      if (issIdle >> benchArg) {
        std::string lower;
        for (char c : benchArg)
          lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        if (lower == "surface" || lower == "surf" || lower == "s") {
          int pts = 100000;  // REQ-100's surface profile: ~200k triangles
          int v = 0;
          if (issIdle >> v)
            pts = v;
          if (issIdle >> v)
            frames = v;
          st.bench.surfacePointCount = pts;
          StartFrameBudgetBench(st, 1, frames, log);
          return;
        }
        if (lower == "mesh" || lower == "m") {
          int tris = 2000000;  // REQ-100 (b): the density decided 2026-08-15, TASK-041's fixture
          int v = 0;
          if (issIdle >> v)
            tris = v;
          if (issIdle >> v)
            frames = v;
          st.bench.meshTriangleCount = tris;
          StartFrameBudgetBench(st, 1, frames, log);
          return;
        }
        issIdle.clear();
        issIdle.seekg(afterTok);  // neither keyword: re-read the token as a segment count
      } else {
        issIdle.clear();
      }
      int v = 0;
      if (issIdle >> v)
        segs = v;
      if (issIdle >> v)
        frames = v;
      StartFrameBudgetBench(st, segs, frames, log);
      return;
    }
    // `ELEV 12.5` / `UCS W` — the inline forms. Falling through to the prompt when no argument
    // follows is what makes bare `ELEV` still work.
    if (plotTok == "elev" || plotTok == "ucs") {
      std::string evArg;
      if (issIdle >> evArg) {
        const std::string evLow = StringUtil::toLowerAsciiCopy(evArg);
        if (evLow == "w" || evLow == "world") {
          ApplyUcsWorld(st, log);
          return;
        }
        try {
          size_t used = 0;
          const double ez = std::stod(evArg, &used);
          if (used == evArg.size()) {
            ApplyElevValue(st, ez, log);
            return;
          }
        } catch (...) {
          // Not a number — fall through to the message below rather than silently ignoring it.
        }
        log.push_back("ELEV — usage: ELEV <elevation>, or ELEV W for world.");
        return;
      }
    }
    if (plotTok == "plotscale" || plotTok == "pscale") {
      float pv = 0.f;
      if (!(issIdle >> pv) || pv <= 0.f)
        log.push_back("PLOTSCALE — usage: PLOTSCALE <model_units_per_plotted_inch> (example: 50 for 1\"=50').");
      else {
        st.modelUnitsPerPlottedInch = pv;
        RepositionAllSurveyPointLabels(st);
        st.surveyLabelLayoutCacheHalfH = st.viewportLastSurveyLayoutOrthoHalfH;
        st.surveyLabelLayoutCacheVpHeightPx = st.viewportLastSurveyLayoutHeightPx;
        st.surveyLabelLayoutCacheMup = st.modelUnitsPerPlottedInch;
        BumpCadGpuCache(st);
        log.push_back("Plot scale: 1 plotted inch = " + std::to_string(pv) + " model units.");
      }
      return;
    }
    // REQ-071. `EXTRACT <surface>[, <layer>]` — comma-separated, because a surface name and a layer
    // name both routinely contain spaces.
    if (plotTok == "extract") {
      std::string rest;
      std::getline(issIdle, rest);
      ExecuteExtractCommand(st, StringUtil::trimCopy(rest), log);
      return;
    }
    // REQ-070. `SURFSTYLE [<verb> …]` — bare opens the editor; the verbs are what a headless
    // transcript drives, because REQ-070's acceptance conditions are end-to-end and a dialog is not
    // reachable from one.
    if (plotTok == "surfstyle") {
      std::string rest;
      std::getline(issIdle, rest);
      ExecuteSurfStyleCommand(st, StringUtil::trimCopy(rest), log);
      return;
    }
    // REQ-087. `FEATURELINE [<name>]` — the whole remainder is the name, so it may contain spaces.
    if (plotTok == "featureline" || plotTok == "fl") {
      std::string rest;
      std::getline(issIdle, rest);
      StartFeatureLineCommand(st, StringUtil::trimCopy(rest), log);
      return;
    }
    if (plotTok == "featurelinelist" || plotTok == "fllist") {
      const size_t n = st.featureLineOffsets.empty() ? 0 : st.featureLineOffsets.size() - 1;
      if (n == 0) {
        log.push_back("FEATURELINELIST — no feature lines in the drawing.");
        return;
      }
      for (size_t i = 0; i < n; ++i) {
        const int b = st.featureLineOffsets[i], e = st.featureLineOffsets[i + 1];
        const std::string nm = i < st.featureLineInfo.size() ? st.featureLineInfo[i].name : std::string();
        const bool closed = i < st.featureLineClosed.size() && st.featureLineClosed[i] != 0;
        int elevPts = 0;
        for (int v = b; v < e; ++v)
          if (static_cast<size_t>(v) < st.featureLineElevPt.size() && st.featureLineElevPt[v])
            ++elevPts;
        log.push_back("Feature line " + std::to_string(i + 1) + ": \"" + nm + "\" — " +
                      std::to_string(e - b) + " vertices (" + std::to_string((e - b) - elevPts) +
                      " PI, " + std::to_string(elevPts) + " elevation point(s))" +
                      (closed ? ", closed" : "") + ", entity id " +
                      std::to_string(i < st.featureLineAttrs.size() ? st.featureLineAttrs[i].id : 0));
        for (int v = b; v < e; ++v) {
          const size_t base = static_cast<size_t>(v) * 3;
          if (base + 2 >= st.featureLineVerts.size())
            break;
          char buf[160];
          std::snprintf(buf, sizeof(buf), "  %s %d: %.3f, %.3f, elevation %.3f",
                        (static_cast<size_t>(v) < st.featureLineElevPt.size() && st.featureLineElevPt[v])
                            ? "elev pt"
                            : "PI     ",
                        v - b + 1, static_cast<double>(st.featureLineVerts[base]),
                        static_cast<double>(st.featureLineVerts[base + 1]),
                        static_cast<double>(st.featureLineVerts[base + 2]));
          log.push_back(buf);
        }
      }
      return;
    }
    // REQ-088 — the elevation editor's whole command surface. One verb with sub-actions rather than
    // seven top-level words, following UNDESIGNATE's shape:
    //
    //   FLELEV <n>                        list the table
    //   FLELEV <n> SET        <point> <elev>
    //   FLELEV <n> GRADEAHEAD <point> <pct>
    //   FLELEV <n> GRADEBACK  <point> <pct>
    //   FLELEV <n> RAISE      <delta>     negative lowers
    //   FLELEV <n> INSERT     <station> <elev>
    //   FLELEV <n> DELETE     <point>
    //
    // REQ-203: stage 2's panel routes its edited cells through exactly these, the way the Surfaces
    // panel routes through ProcessCommandLineSubmit — so what the tests drive is what the UI drives.
    // REQ-088: open the elevation editor, optionally aimed at a feature line. Separate from FLELEV
    // so that FLELEV stays usable with no window (REQ-203) — the driver must be able to read and
    // edit the table without a panel existing.
    if (plotTok == "flelevedit" || plotTok == "featurelineelevedit") {
      const int flCount =
          st.featureLineOffsets.empty() ? 0 : static_cast<int>(st.featureLineOffsets.size()) - 1;
      if (flCount <= 0) {
        log.push_back("FLELEVEDIT — no feature lines in the drawing. Draw one with FEATURELINE first.");
        return;
      }
      int which = 0;
      if (issIdle >> which) {
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
      return;
    }
    if (plotTok == "flelev" || plotTok == "featurelineelev") {
      std::istringstream& fe = issIdle;  // the verb is already consumed
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
        const size_t ii = static_cast<size_t>(flNum - 1);
        const std::string nm = ii < st.featureLineInfo.size() ? st.featureLineInfo[ii].name : std::string();
        log.push_back("FLELEV — feature line " + std::to_string(flNum) + " \"" + nm + "\": " +
                      std::to_string(rows.size()) + " points.");
        log.push_back("   #  type     station    elevation   length ahead   grade back   grade ahead");
        for (const FeatureLineElevRow& r : rows) {
          // A dash, not "0.00%", where a segment does not exist — see FeatureLineElevRow. Printing
          // zero there would state that the ground is level at a place that has no ground.
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
          log.push_back("FLELEV — " + sub + " needs a " + (sub == "set" ? "elevation." : "grade in percent."));
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
      return;
    }
    // Surface definition commands (REQ-068/069). Each reads the WHOLE remainder of the line — names
    // contain spaces — and splits on commas where it needs more than one argument. Together with
    // DESIGNATEBREAKLINE/DESIGNATEBOUNDARY below they make every surface operation reachable without
    // the Surfaces panel, which is what lets the REQ-203 driver exercise them at all: a surface has
    // no entity id, and panel buttons are unreachable with no window.
    if (plotTok == "surfacecreate" || plotTok == "sfcreate" || plotTok == "surfacerename" ||
        plotTok == "sfrename" || plotTok == "surfacedelete" || plotTok == "sfdelete" ||
        plotTok == "surfacerebuild" || plotTok == "sfrebuild" || plotTok == "surfacelist" ||
        plotTok == "sflist" || plotTok == "undesignate" || plotTok == "undes" ||
        plotTok == "surfaceaddfile" || plotTok == "sfaddfile" || plotTok == "surfaceimportfile" ||
        plotTok == "sfimportfile") {
      std::string rest;
      std::getline(issIdle, rest);
      rest = StringUtil::trimCopy(rest);
      if (plotTok == "surfacecreate" || plotTok == "sfcreate")
        RunSurfaceCreate(st, rest, log);
      else if (plotTok == "surfacerename" || plotTok == "sfrename")
        RunSurfaceRename(st, rest, log);
      else if (plotTok == "surfacedelete" || plotTok == "sfdelete")
        RunSurfaceDelete(st, rest, log);
      else if (plotTok == "surfacerebuild" || plotTok == "sfrebuild")
        RunSurfaceRebuild(st, rest, log);
      else if (plotTok == "surfacelist" || plotTok == "sflist")
        ReportSurfaces(st, log);
      else if (plotTok == "surfaceaddfile" || plotTok == "sfaddfile")
        RunSurfaceAddFile(st, rest, log);
      else if (plotTok == "surfaceimportfile" || plotTok == "sfimportfile")
        RunSurfaceImportFile(st, rest, log);
      else
        RunUndesignate(st, rest, log);
      return;
    }
    // `DESIGNATEBREAKLINE <surface name>` (REQ-069) — the whole remainder is the name (surface
    // names can contain spaces, e.g. "Existing Ground"), so this reads the rest of the line rather
    // than one `>>` token.
    if (plotTok == "designatebreakline" || plotTok == "dbl") {
      std::string rest;
      std::getline(issIdle, rest);
      rest = StringUtil::trimCopy(rest);
      if (rest.empty()) {
        log.push_back("DESIGNATEBREAKLINE — usage: DESIGNATEBREAKLINE <surface name>.");
        return;
      }
      StartDesignateBreaklineCommand(st, rest, log);
      return;
    }
    // `DESIGNATEBOUNDARY <surface name> <OUTER|HIDE|SHOW>` — the kind is always the LAST token, so
    // the surface name is whatever remains before it, spaces and all.
    if (plotTok == "designateboundary" || plotTok == "dbd") {
      std::string rest;
      std::getline(issIdle, rest);
      rest = StringUtil::trimCopy(rest);
      const size_t sp = rest.find_last_of(" \t");
      const bool usageError = rest.empty() || sp == std::string::npos;
      const std::string name = usageError ? std::string() : StringUtil::trimCopy(rest.substr(0, sp));
      const std::string kindWord =
          usageError ? std::string() : StringUtil::toLowerAsciiCopy(StringUtil::trimCopy(rest.substr(sp + 1)));
      CadBoundaryKind kind = CadBoundaryKind::Outer;
      const bool kindOk = kindWord == "outer" || kindWord == "hide" || kindWord == "show";
      if (kindWord == "hide")
        kind = CadBoundaryKind::Hide;
      else if (kindWord == "show")
        kind = CadBoundaryKind::Show;
      if (usageError || name.empty() || !kindOk) {
        log.push_back("DESIGNATEBOUNDARY — usage: DESIGNATEBOUNDARY <surface name> <OUTER|HIDE|SHOW>.");
        return;
      }
      StartDesignateBoundaryCommand(st, name, kind, log);
      return;
    }
  }

  if (st.active == AppCommandState::Kind::Delete) {
    log.push_back("DELETE — finish window-select in the viewport (two clicks), or ESC to cancel.");
    return;
  }

  if (st.active == AppCommandState::Kind::Join) {
    log.push_back("JOIN — finish window-select in the viewport (two clicks), or ESC to cancel.");
    return;
  }

  if (st.active == AppCommandState::Kind::TrimState) {
    const std::string tsIn = StringUtil::trimCopy(line);
    if (tsIn.empty()) {  // bare Enter keeps the current value, as an AutoCAD system-variable prompt does
      log.push_back("TRIMSTATE unchanged (" + std::to_string(st.trimState) + ").");
      st.active = AppCommandState::Kind::None;
      return;
    }
    int tv = 0;
    std::istringstream tsIss(tsIn);
    if (!(tsIss >> tv) || !(tsIss >> std::ws).eof()) {
      log.push_back("TRIMSTATE — enter 0 or 1 (blank Enter keeps the current value).");
      return;
    }
    if (ApplyTrimStateValue(st, tv, log))
      st.active = AppCommandState::Kind::None;
    return;
  }

  if (st.active == AppCommandState::Kind::Elev) {
    const std::string evIn = StringUtil::trimCopy(line);
    if (evIn.empty()) {  // bare Enter keeps the current plane, as a system-variable prompt does
      char buf[96];
      std::snprintf(buf, sizeof(buf), "Elevation unchanged (%.4f).", static_cast<double>(CadWorkPlaneElevation(st)));
      log.push_back(buf);
      st.active = AppCommandState::Kind::None;
      return;
    }
    const std::string evLow = StringUtil::toLowerAsciiCopy(evIn);
    if (evLow == "w" || evLow == "world") {
      ApplyUcsWorld(st, log);
      st.active = AppCommandState::Kind::None;
      return;
    }
    double ez = 0.0;
    std::istringstream evIss(evIn);
    if (!(evIss >> ez) || !(evIss >> std::ws).eof()) {
      log.push_back("ELEV — enter an elevation, or W for world (blank Enter keeps the current value).");
      return;
    }
    if (ApplyElevValue(st, ez, log))
      st.active = AppCommandState::Kind::None;
    return;
  }

  if (st.active == AppCommandState::Kind::Trim) {
    using TP = AppCommandState::TrimPhase;
    const std::string low = StringUtil::toLowerAsciiCopy(StringUtil::trimCopy(line));
    // L and T switch modes mid-run, so either style is reachable whatever TRIMSTATE is set to.
    if (low == "l" || low == "line") {
      if (st.trimPhase == TP::CuttingLine_WaitP1 || st.trimPhase == TP::CuttingLine_WaitP2)
        log.push_back("TRIM — already drawing the trim line; pick its points in the viewport.");
      else {
        st.trimCutters.clear();
        st.trimPhase = TP::CuttingLine_WaitP1;
        log.push_back("TRIM — draw along the segment to trim: first point (rubber band shows dashed preview).");
      }
      return;
    }
    if (low == "t" || low == "cutting" || low == "edges") {
      if (st.trimPhase == TP::SelectCuttingEdges || st.trimPhase == TP::SelectTrimTargets)
        log.push_back("TRIM — already picking cutting edges.");
      else {
        st.trimCutters.clear();
        st.trimPhase = TP::SelectCuttingEdges;
        log.push_back("TRIM — pick cutting edges (hover highlights), Enter when done, then click pieces to trim.");
      }
      return;
    }
    log.push_back("TRIM — viewport picks; L draws the trim line, T picks cutting edges; ESC cancels.");
    return;
  }

  if (st.active == AppCommandState::Kind::Zoom) {
    log.push_back("ZOOM WINDOW — finish two clicks in the viewport, or ESC to cancel.");
    return;
  }

  if (st.active == K::Offset) {
    using OP = AppCommandState::OffsetPhase;
    if (st.offsetPhase != OP::WaitDistanceOrThrough) {
      log.push_back("OFFSET — use viewport picks; type a distance only after selecting the object.");
      return;
    }
    float d = 0.f;
    if (!ParseOneFloat(StringUtil::trimCopy(line), &d)) {
      log.push_back("OFFSET — type a positive offset distance (model units), then pick a side.");
      return;
    }
    if (d <= 0.f) {
      log.push_back("OFFSET — distance must be positive.");
      return;
    }
    st.offsetTypedDistance = d;
    st.offsetPhase = OP::WaitSidePick;
    log.push_back("OFFSET — pick which side of the object to offset.");
    return;
  }

  if (st.active == K::IdPoint) {
    float px = 0.f;
    float py = 0.f;
    if (!ParseStoragePoint(st, line, &px, &py, false, 0.f, 0.f)) {
      log.push_back("ID — pick in viewport or type X,Y (model units, UCS World).");
      return;
    }
    CommitIdPointAt(st, px, py, log);
    return;
  }

  if (st.active == K::SurveyInverse) {
    using SIP = AppCommandState::SurveyInversePhase;
    float px = 0.f;
    float py = 0.f;
    if (st.surveyInversePhase == SIP::WaitFrom) {
      if (!ParseStoragePoint(st, line, &px, &py, false, 0.f, 0.f)) {
        log.push_back("INVERSE — type X,Y (World) or pick first point in Drawing1.");
        return;
      }
      st.surveyInverseFromX = px;
      st.surveyInverseFromY = py;
      st.surveyInversePhase = SIP::WaitTo;
      log.push_back("INVERSE — second point (X,Y or @dx,dy from first).");
      return;
    }
    if (!ParseStoragePoint(st, line, &px, &py, true, st.surveyInverseFromX, st.surveyInverseFromY)) {
      log.push_back("INVERSE — type X,Y or @dx,dy from first point.");
      return;
    }
    CommitSurveyInverseSecondPoint(st, px, py, log);
    return;
  }

  if (st.active == K::SurfaceElevGrade) {
    using SEP = AppCommandState::SurfaceElevPhase;
    float px = 0.f;
    float py = 0.f;
    if (st.surfaceElevPhase == SEP::WaitFirst) {
      if (!ParseStoragePoint(st, line, &px, &py, false, 0.f, 0.f)) {
        log.push_back("SURFELEV — type X,Y (World) or pick a point in the drawing.");
        return;
      }
      ReportSurfaceElevationAt(st, px, py, log);
      st.surfaceElevPhase = SEP::WaitSecond;
      log.push_back("SURFELEV — second point for grade, or ESC to stop here.");
      return;
    }
    if (!ParseStoragePoint(st, line, &px, &py, true, static_cast<float>(st.surfaceElevFromX),
                           static_cast<float>(st.surfaceElevFromY))) {
      log.push_back("SURFELEV — type X,Y or @dx,dy from the first point.");
      return;
    }
    ReportSurfaceGradeTo(st, px, py, log);
    return;
  }

  if (st.active == K::Align) {
    using AP = AppCommandState::AlignPhase;
    // Empty Enter is handled above; PickSelection accepts no typed coordinates.
    if (st.alignPhase == AP::PickSelection) {
      log.push_back("ALIGN — window-select entities in the drawing, then press Enter.");
      return;
    }
    if (line.empty()) {
      ExecuteAlignCommand(st, log);
      return;
    }
    float px = 0.f, py = 0.f;
    if (!ParseStoragePoint(st, line, &px, &py, false, 0.f, 0.f)) {
      log.push_back("ALIGN — type X,Y for the point, or press Enter to apply with current pairs.");
      return;
    }
    if (st.alignPhase == AP::PickSrc) {
      AppCommandState::AlignControlPt cp{};
      cp.srcX = px;
      cp.srcY = py;
      st.alignControlPts.push_back(cp);
      st.alignPhase = AP::PickDst;
      log.push_back("ALIGN — destination for pair " + std::to_string(st.alignControlPts.size()) +
                    " (pick or type real-world X,Y):");
    } else {
      st.alignControlPts.back().dstX = px;
      st.alignControlPts.back().dstY = py;
      st.alignPhase = AP::PickSrc;
      const size_t n = st.alignControlPts.size();
      log.push_back("ALIGN — pair " + std::to_string(n) + " added.  Pick next source, or Enter to apply (" +
                    std::to_string(n) + " pair" + (n == 1 ? "" : "s") + " ready).");
    }
    return;
  }

  if (st.active == AppCommandState::Kind::Move || st.active == AppCommandState::Kind::Copy) {
    if (HandleModifyText(st, st.active == AppCommandState::Kind::Copy, line, log)) {
      return;
    }
    log.push_back("Could not parse MOVE/COPY input — use X,Y or @dx,dy from base.");
    return;
  }

  if (st.active == AppCommandState::Kind::Scale) {
    if (HandleScaleText(st, line, log)) {
      return;
    }
    log.push_back("Could not parse SCALE input — see command hints (base X,Y; factor; R + reference/new length).");
    return;
  }

  if (st.active == AppCommandState::Kind::Rotate) {
    if (HandleRotateText(st, line, log)) {
      return;
    }
    log.push_back("Could not parse ROTATE input — see command hints.");
    return;
  }

  if (st.active == K::FeatureLine) {
    // REQ-087. Same X,Y,Z entry as 3DPOLY — the peel below is shared — with one extra word: E marks
    // the next vertex an elevation point rather than a PI.
    const std::string lowFl = StringUtil::toLowerAsciiCopy(StringUtil::trimCopy(line));

    // TASK-082. A point is waiting for its elevation, so THIS line is that elevation and nothing
    // else — checked before CLOSE/END and before coordinate parsing, because "50" is a valid
    // elevation and would otherwise be read as a malformed point.
    if (st.featureLinePendingPoint) {
      const std::string zText = StringUtil::trimCopy(line);
      if (zText.empty()) {  // bare Enter accepts the default shown in the prompt
        CommitFeatureLinePendingPoint(st, st.featureLinePendingDefaultZ, log);
        return;
      }
      char* zEnd = nullptr;
      const double zv = std::strtod(zText.c_str(), &zEnd);
      if (!zEnd || *zEnd != '\0' || !std::isfinite(zv)) {
        // Stay in the prompt. Dropping back to point entry would silently discard the point the
        // user just clicked, and they would have no way to know which of the two it lost.
        char buf[192];
        std::snprintf(buf, sizeof(buf),
                      "FEATURELINE — elevation must be a number. Type one, or Enter for <%.3f>:",
                      static_cast<double>(st.featureLinePendingDefaultZ));
        log.push_back(buf);
        return;
      }
      CommitFeatureLinePendingPoint(st, static_cast<float>(zv), log);
      return;
    }

    if (lowFl == "close" || lowFl == "cl") {
      CommitFeatureLineDraft(st, true, log);
      return;
    }
    if (lowFl == "end") {
      CommitFeatureLineDraft(st, false, log);
      return;
    }
    bool nextIsElevPoint = false;
    std::string flText = StringUtil::trimCopy(line);
    if (!flText.empty() && (flText[0] == 'e' || flText[0] == 'E') &&
        (flText.size() == 1 || flText[1] == ' ' || flText[1] == '\t')) {
      nextIsElevPoint = true;
      flText = StringUtil::trimCopy(flText.substr(1));
      if (flText.empty()) {
        // TASK-082: bare E ARMS the flag rather than erroring. Before a click could supply X,Y, the
        // only way to place a point was to type it, so E had to be followed by coordinates on the
        // same line; now the coordinates arrive from the mouse and the flag has to wait for them.
        st.featureLineNextIsElevPoint = true;
        log.push_back("FEATURELINE — next point will be an elevation point. Click it, or type X,Y "
                      "/ X,Y,Z.");
        return;
      }
    }
    if (st.featureLineNextIsElevPoint)
      nextIsElevPoint = true;  // armed by an earlier bare E
    // Peel a typed elevation, exactly as 3DPOLY does, and for the same reason: the shared 2D parser
    // is REQ-101-critical and must not learn about a third component.
    {
      const size_t c1 = flText.find(',');
      const size_t c2 = (c1 == std::string::npos) ? std::string::npos : flText.find(',', c1 + 1);
      if (c2 != std::string::npos) {
        if (flText.find(',', c2 + 1) != std::string::npos) {
          log.push_back("FEATURELINE — too many coordinates: X,Y,Z or @dx,dy,dz.");
          return;
        }
        const std::string zText = StringUtil::trimCopy(flText.substr(c2 + 1));
        char* zEnd = nullptr;
        const double zv = std::strtod(zText.c_str(), &zEnd);
        if (zText.empty() || !zEnd || *zEnd != '\0' || !std::isfinite(zv)) {
          log.push_back("FEATURELINE — elevation must be a number: X,Y,Z or @dx,dy,dz.");
          return;
        }
        st.polylineTypedZ = static_cast<float>(zv);
        st.polylineTypedZRelative = !flText.empty() && flText[0] == '@';
        st.polylineTypedZValid = true;
        flText = flText.substr(0, c2);
      }
    }
    float fx = 0.f, fy = 0.f;
    const bool flRel = !st.featureLineDraftVerts.empty();
    if (ParseStoragePoint(st, flText, &fx, &fy, flRel, st.anchorX, st.anchorY)) {
      // TASK-082 Q1: the rule is "a point that arrives WITHOUT an elevation prompts for one", not
      // "clicks prompt". A typed X,Y,Z already carries its answer, so asking again would be noise —
      // and it is the form every existing transcript uses. A typed X,Y takes the same path a click
      // does.
      if (st.polylineTypedZValid) {
        st.featureLineNextIsElevPoint = false;  // consumed by this vertex
        SubmitFeatureLineVertex(st, fx, fy, nextIsElevPoint, log);
      } else {
        st.featureLineNextIsElevPoint = nextIsElevPoint;  // survives until the elevation arrives
        SubmitFeatureLinePoint(st, fx, fy, log);
      }
      return;
    }
    st.polylineTypedZValid = false;  // the point failed to parse; do not carry the Z to the next try
    log.push_back("FEATURELINE — click a point, or type X,Y / X,Y,Z (or @dx,dy,dz), E for an "
                  "elevation point, CLOSE, END, or ESC.");
    return;
  }

  if (st.active == K::Polyline) {
    using PP = AppCommandState::PolylinePhase;
    const std::string low = StringUtil::toLowerAsciiCopy(StringUtil::trimCopy(line));

    // REQ-085: peel a trailing `,z` off `x,y,z` so the shared 2D parser never sees it. That parser
    // is REQ-101-critical and used by every command; widening it to three components to serve one
    // command would put every other command's coordinate handling at risk for no gain. 3DPOLY only —
    // a plain POLYLINE keeps refusing a third component rather than silently ignoring it.
    std::string pointText = line;
    if (st.polylineDraft3d) {
      const std::string trimmed = StringUtil::trimCopy(line);
      const size_t c1 = trimmed.find(',');
      const size_t c2 = (c1 == std::string::npos) ? std::string::npos : trimmed.find(',', c1 + 1);
      if (c2 != std::string::npos) {
        // Strict about the field count, because the shared 2D parser is NOT: ParseTwoDoubles reads
        // two numbers and ignores whatever follows, so `1,2,3,4` would otherwise land silently at
        // (1,2). Refusing here keeps that quiet-drop out of the one command where a third field is
        // meaningful. (The same silent drop still applies to LINE/POLYLINE/RECT — pre-existing, and
        // reported rather than changed under this task.)
        if (trimmed.find(',', c2 + 1) != std::string::npos) {
          log.push_back("3DPOLY — too many coordinates: X,Y,Z or @dx,dy,dz.");
          return;
        }
        const std::string zText = StringUtil::trimCopy(trimmed.substr(c2 + 1));
        char* zEnd = nullptr;
        const double zv = std::strtod(zText.c_str(), &zEnd);
        if (!zText.empty() && zEnd && *zEnd == '\0' && std::isfinite(zv)) {
          st.polylineTypedZ = static_cast<float>(zv);
          st.polylineTypedZRelative = !trimmed.empty() && trimmed[0] == '@';
          st.polylineTypedZValid = true;
          pointText = trimmed.substr(0, c2);  // `x,y`, with any leading `@` still attached
        } else {
          log.push_back("3DPOLY — elevation must be a number: X,Y,Z or @dx,dy,dz.");
          return;
        }
      }
    }
    if (low == "close" || low == "cl") {
      CancelSegmentAnglePick(st, nullptr);
      if (st.polylinePhase != PP::NeedNextPoint || st.polyDraftSegments == 0)
        log.push_back("POLYLINE CLOSE — need at least one segment after the start point.");
      else
        CommitPolylineDraft(st, true, log);
      return;
    }
    if (low == "end") {
      CancelSegmentAnglePick(st, nullptr);
      if (st.polylinePhase != PP::NeedNextPoint || st.polyDraftSegments == 0)
        log.push_back("POLYLINE END — need at least one segment after the start point.");
      else
        CommitPolylineDraft(st, false, log);
      return;
    }

    float px = 0.f;
    float py = 0.f;
    const bool allowRel = st.polylinePhase == PP::NeedNextPoint;

    if (allowRel && TryParseSegmentAngleLockCommand(st, line, log)) {
      return;
    }

    // `pointText` is `line` with any 3DPOLY elevation already peeled off; identical to `line` for a
    // plain POLYLINE.
    if (ParseStoragePoint(st, pointText, &px, &py, allowRel, st.anchorX, st.anchorY)) {
      SubmitPolylineVertex(st, px, py, log);
      return;
    }

    if (allowRel && st.segmentAngleLockActive) {
      float dist = 0.f;
      if (ParseSingleFloatToken(line, &dist)) {
        if (std::fabs(dist) < 1e-20f)
          log.push_back("POLYLINE — distance must be non-zero.");
        else
          SubmitPolylineVertex(st, st.anchorX + st.segmentLockUx * dist, st.anchorY + st.segmentLockUy * dist, log);
        return;
      }
    }

    if (allowRel && st.orthoMode) {
      float dist = 0.f;
      if (ParseSingleFloatToken(line, &dist)) {
        float ux = 0.f;
        float uy = 0.f;
        if (!OrthoUnitTowardUiCursorFromAnchor(st, &ux, &uy))
          log.push_back(
              "Ortho distance needs cursor direction — move crosshair away from anchor, then enter distance.");
        else
          SubmitPolylineVertex(st, st.anchorX + ux * dist, st.anchorY + uy * dist, log);
        return;
      }
    }

    log.push_back("POLYLINE — X,Y / @dx,dy / A or 2P bearing / CLOSE / END / ortho distance.");
    return;
  }

  if (st.active == K::Ellipse && st.ellPhase == AppCommandState::EllipsePhase::WaitRatio) {
    const std::string tr = StringUtil::trimCopy(line);
    float ratio = 0.5f;
    if (!tr.empty() && !ParseSingleFloatToken(tr, &ratio)) {
      log.push_back("ELLIPSE — enter one number for minor/major ratio (0-1], or blank for 0.5.");
      return;
    }
    FinishEllipseFromRatio(st, ratio, log);
    return;
  }

  if (st.active == K::Text) {
    using TP = AppCommandState::TextCmdPhase;
    if (st.textPhase == TP::WaitInsertion) {
      float px = 0.f;
      float py = 0.f;
      if (!ParseStoragePoint(st, line, &px, &py, false, 0.f, 0.f)) {
        log.push_back("TEXT — type insertion X,Y or click in viewport.");
        return;
      }
      st.textInsX = px;
      st.textInsY = py;
      st.textPhase = TP::WaitHeight;
      log.push_back("TEXT height — Enter for plot-scale default:");
      return;
    }
    if (st.textPhase == TP::WaitHeight) {
      // In paper space the height is in plotted (paper) inches directly; in model space it is world units.
      const bool paperText = ActivePaperGeometryTarget(st) != nullptr;
      const std::string tr = StringUtil::trimCopy(line);
      if (tr.empty())
        st.textHeightDraft = paperText ? st.defaultPlottedTextHeightInches : DefaultAnnotationTextHeightWorld(st);
      else if (!ParseSingleFloatToken(tr, &st.textHeightDraft) || st.textHeightDraft <= 0.f) {
        log.push_back("TEXT — invalid height.");
        return;
      }
      st.textPhase = TP::WaitRotation;
      log.push_back("TEXT rotation ° clockwise from north (decimal/DMS) — Enter for 90 (horizontal):");
      return;
    }
    if (st.textPhase == TP::WaitRotation) {
      const std::string tr = StringUtil::trimCopy(line);
      // Bearing convention (clockwise from north): 0 = north (text runs up), 90 = east (reads
      // left-to-right). Enter defaults to 90 (horizontal), matching AutoCAD's default text orientation.
      float deg = 90.f;
      if (!tr.empty() && !ParseAngleDegrees(tr, &deg)) {
        log.push_back("TEXT — could not parse angle.");
        return;
      }
      st.textRotDraft = MathAngleRadFromBearingCwNorthDeg(deg);
      st.textPhase = TP::WaitString;
      log.push_back("TEXT — enter content:");
      return;
    }
    if (st.textPhase == TP::WaitString) {
      CadAnnotation ann;
      ann.kind = CadAnnotation::Kind::Text;
      ann.insX = st.textInsX;
      ann.insY = st.textInsY;
      ann.rotationRad = st.textRotDraft;
      ann.text = line;
      if (!ann.text.empty()) {
        PushUndoSnapshot(st, "Text");
        if (PaperLayout* L = ActivePaperGeometryTarget(st)) {
          // Paper-space TEXT (REQ-037): insX/insY are paper inches; textHeightDraft is already in
          // paper (plotted) inches — store it directly, no model-scale division.
          ann.plottedHeightInches = st.textHeightDraft;
          L->paperTexts.push_back(std::move(ann));
          L->paperTextAttrs.push_back(MakeNewEntityAttrs(st));
        } else {
          ann.plottedHeightInches = st.textHeightDraft / std::max(st.modelUnitsPerPlottedInch, 1.e-6f);
          ann.insZ = CadCommitElevation(st);  // model TEXT: snap overrides ELEV (REQ-058)
          StampActiveTextStyleOnNewText(st, ann);  // REQ-044: new TEXT adopts the active text style
          st.cadAnnotations.push_back(std::move(ann));
          st.cadAnnotationAttrs.push_back(MakeNewEntityAttrs(st));
        }
        // Issue #56. Not a rendering concern — this is what makes the new entity get an id at all.
        // MakeNewEntityAttrs leaves id 0 and relies on the EnsureEntityIds sweep, and that sweep
        // early-outs on `entityIdSweepRevision == cadGpuRevision` with the reasoning "geometry has
        // not changed since the last sweep, so nothing can be missing an id". Committing an entity
        // without bumping the revision makes that premise false, so the sweep skipped this
        // annotation and it was saved to `.gs` with id 0 (REQ-076), which also broke REQ-079 resave
        // idempotence. Every other commit path bumps here; TEXT was the one that did not.
        BumpCadGpuCache(st);
        log.push_back("TEXT placed.");
      } else
        log.push_back("TEXT — empty; canceled.");
      st.active = K::None;
      ResetTextCmdDraft(st);
      return;
    }
  }

  if (st.active == AppCommandState::Kind::Line) {
    float px = 0.f;
    float py = 0.f;
    const bool allowRel = st.linePhase == AppCommandState::LinePhase::NeedNextPoint;

    if (allowRel && TryParseSegmentAngleLockCommand(st, line, log)) {
      return;
    }

    if (ParseStoragePoint(st, line, &px, &py, allowRel, st.anchorX, st.anchorY)) {
      SubmitLineVertex(st, px, py, log);
      return;
    }

    if (allowRel && st.segmentAngleLockActive) {
      float dist = 0.f;
      if (ParseSingleFloatToken(line, &dist)) {
        if (std::fabs(dist) < 1e-20f)
          log.push_back("LINE — distance must be non-zero.");
        else {
          px = st.anchorX + st.segmentLockUx * dist;
          py = st.anchorY + st.segmentLockUy * dist;
          SubmitLineVertex(st, px, py, log);
        }
        return;
      }
    }

    if (allowRel && st.orthoMode) {
      float dist = 0.f;
      if (ParseSingleFloatToken(line, &dist)) {
        float ux = 0.f;
        float uy = 0.f;
        if (!OrthoUnitTowardUiCursorFromAnchor(st, &ux, &uy))
          log.push_back(
              "Ortho distance needs cursor direction — move crosshair away from anchor, then enter distance.");
        else {
          px = st.anchorX + ux * dist;
          py = st.anchorY + uy * dist;
          SubmitLineVertex(st, px, py, log);
        }
        return;
      }
    }

    log.push_back(
        std::string("Could not parse point. Use X,Y or X Y") +
        (allowRel ? "; @dx,dy; A / 2P (two picks); A 45 +90; ortho distance toward cursor." : "."));
    return;
  }

  if (st.active == K::Rect) {
    using RectP = AppCommandState::RectPhase;
    const bool second = st.rectPhase == RectP::WaitSecondCorner;
    float px = 0.f;
    float py = 0.f;
    if (ParseStoragePoint(st, line, &px, &py, second, st.rectX1, st.rectY1)) {
      if (second) {
        CommitRectangle(st, st.rectX1, st.rectY1, px, py, log);
      } else {
        st.rectX1 = px;
        st.rectY1 = py;
        st.anchorX = px;
        st.anchorY = py;
        st.rectPhase = RectP::WaitSecondCorner;
        log.push_back("RECT — pick the opposite corner (or type X,Y / @dx,dy):");
      }
      return;
    }
    log.push_back(std::string("RECT — could not parse point. Use X,Y") +
                  (second ? " or @dx,dy for an exact width x height." : "."));
    return;
  }

  if (st.active == AppCommandState::Kind::Circle) {
    if (HandleCircleTextInput(line, st, log)) {
      return;
    }
    log.push_back("Could not parse input for current CIRCLE step — see hint below.");
    return;
  }

  if (st.active == K::DimAligned || st.active == K::DimLinear) {
    using DP = AppCommandState::DimPhase;
    const bool linear = st.active == K::DimLinear;
    const std::string dimTrim = StringUtil::trimCopy(line);
    const std::string dimLow = StringUtil::toLowerAsciiCopy(dimTrim);
    if (linear && st.dimPhase == DP::WaitDimLinePt && (dimLow == "h" || dimLow == "v")) {
      CadDimLinearApplyHVHotkey(st, dimLow == "v", log);
      return;
    }
    float px = 0.f;
    float py = 0.f;
    bool allowRel = false;
    float ax = 0.f;
    float ay = 0.f;
    if (st.dimPhase == DP::WaitExt2) {
      allowRel = true;
      ax = st.dimE1x;
      ay = st.dimE1y;
    } else if (linear && st.dimPhase == DP::WaitDimLinePt) {
      allowRel = true;
      ax = 0.5f * (st.dimE1x + st.dimE2x);
      ay = 0.5f * (st.dimE1y + st.dimE2y);
    }
    if (!ParseStoragePoint(st, dimTrim, &px, &py, allowRel, ax, ay)) {
      log.push_back(linear ? "DIMLINEAR — X,Y or @dx,dy; at line step H / V locks orientation; move cursor to unlock."
                           : "DIMALIGNED — X,Y or @dx,dy from first extension.");
      return;
    }
    SubmitViewportPick(st, px, py, log, false, false);
    return;
  }

  if (st.active == K::DimAngular) {
    using DAP = AppCommandState::DimAngularPhase;
    const std::string dimTrim = StringUtil::trimCopy(line);
    float px = 0.f;
    float py = 0.f;
    bool allowRel = false;
    float ax = 0.f;
    float ay = 0.f;
    if (st.dimAngularPhase != DAP::WaitVertex) {
      allowRel = true;
      ax = st.dimAngVx;
      ay = st.dimAngVy;
    }
    if (!ParseStoragePoint(st, dimTrim, &px, &py, allowRel, ax, ay)) {
      log.push_back("DIMANGULAR — X,Y or @dx,dy from vertex.");
      return;
    }
    SubmitViewportPick(st, px, py, log, false, false);
    return;
  }

  std::string low = StringUtil::toLowerAsciiCopy(line);
  for (const CmdEntry& e : kRegistry) {
    if (low == StringUtil::toLowerAsciiCopy(e.primary)) {
      DispatchByPrimary(StringUtil::toLowerAsciiCopy(e.primary), st, log);
      return;
    }
    if (e.aliases[0] == '\0')
      continue;
    std::istringstream als(std::string(e.aliases));
    std::string a;
    while (std::getline(als, a, ',')) {
      a = StringUtil::trimCopy(a);
      if (a.empty())
        continue;
      if (low == StringUtil::toLowerAsciiCopy(a)) {
        DispatchByPrimary(StringUtil::toLowerAsciiCopy(e.primary), st, log);
        return;
      }
    }
  }

  if (TryStrongFuzzyDispatch(line, st, log)) {
    return;
  }

  auto fuzzy = FuzzyCommandMatches(line, 6);
  if (!fuzzy.empty()) {
    std::string hint = "Unknown command. Did you mean:";
    for (const auto& w : fuzzy)
      hint += " " + w + ",";
    if (!hint.empty() && hint.back() == ',')
      hint.pop_back();
    hint += "?";
    log.push_back(hint);
  } else
    log.push_back("Unknown command. Type HELP.");

  }();
  cmdBuf[0] = '\0';
}

std::vector<std::string> FuzzyCommandMatches(const std::string& query, int maxResults) {
  std::vector<std::string> names;
  for (const CmdEntry& e : kRegistry)
    names.push_back(e.primary);

  struct Scored {
    std::string name;
    int score;
  };
  std::vector<Scored> ranked;
  std::string qlow = StringUtil::toLowerAsciiCopy(StringUtil::trimCopy(query));
  if (qlow.empty())
    return {};

  for (const auto& n : names) {
    int sc = FuzzySubsequenceScore(qlow, n);
    if (sc >= 0)
      ranked.push_back({n, sc});
  }
  std::sort(ranked.begin(), ranked.end(), [](const Scored& a, const Scored& b) {
    if (a.score != b.score)
      return a.score > b.score;
    return a.name < b.name;
  });

  std::vector<std::string> out;
  for (size_t i = 0; i < ranked.size() && static_cast<int>(out.size()) < maxResults; ++i)
    out.push_back(ranked[i].name);
  return out;
}

std::vector<CommandSuggestion> FuzzyCommandSuggestions(const std::string& query, int maxResults) {
  struct Scored {
    const CmdEntry* entry;
    int score;
  };
  std::vector<Scored> ranked;
  const std::string qlow = StringUtil::toLowerAsciiCopy(StringUtil::trimCopy(query));
  if (qlow.empty())
    return {};

  // Prefix match only: the query must be a leading prefix of the command name or an
  // alias (e.g. "e" lists ELLIPSE, EXPORTPOINTS — not REGEN/DELETE). Ranked by how
  // strongly the query matches, so an exact alias hit beats a mere name prefix:
  //   4 = query == primary name      (exact)
  //   3 = query == an alias          (e.g. "l" is LINE's alias → LINE before LAYER)
  //   2 = primary name starts with query
  //   1 = an alias starts with query
  const auto isPrefix = [](const std::string& q, const std::string& s) {
    return s.size() >= q.size() && StringUtil::toLowerAsciiCopy(s).compare(0, q.size(), q) == 0;
  };
  for (const CmdEntry& e : kRegistry) {
    int best = -1;
    const std::string nameLow = StringUtil::toLowerAsciiCopy(e.primary);
    if (nameLow == qlow)
      best = 4;
    else if (isPrefix(qlow, e.primary))
      best = 2;
    if (e.aliases[0] != '\0') {
      std::istringstream als(std::string(e.aliases));
      std::string a;
      while (std::getline(als, a, ',')) {
        a = StringUtil::trimCopy(a);
        if (a.empty())
          continue;
        if (StringUtil::toLowerAsciiCopy(a) == qlow)
          best = std::max(best, 3);
        else if (isPrefix(qlow, a))
          best = std::max(best, 1);
      }
    }
    if (best >= 0)
      ranked.push_back({&e, best});
  }
  std::sort(ranked.begin(), ranked.end(), [](const Scored& a, const Scored& b) {
    if (a.score != b.score)
      return a.score > b.score;  // stronger matches (exact name/alias) first
    return std::string(a.entry->primary) < std::string(b.entry->primary);  // then alphabetical
  });

  std::vector<CommandSuggestion> out;
  for (size_t i = 0; i < ranked.size() && static_cast<int>(out.size()) < maxResults; ++i) {
    CommandSuggestion s;
    s.name = ranked[i].entry->primary;
    for (char& ch : s.name)
      ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));  // NAME in caps (nanoCAD style)
    s.description = ranked[i].entry->description ? ranked[i].entry->description : "";
    out.push_back(std::move(s));
  }
  return out;
}

const char* CircleCommandFooterHint(const AppCommandState& st) {
  if (st.active != AppCommandState::Kind::Circle)
    return "";
  switch (st.circlePhase) {
  case AppCommandState::CirclePhase::WaitCenterOrMode:
    return "CIRCLE: Click or type center | Type 3P for three-point circle | ESC cancel";
  case AppCommandState::CirclePhase::WaitRadius:
    return "CIRCLE: Click edge for radius | Type radius | D <value> or D<value> for diameter | ESC cancel";
  case AppCommandState::CirclePhase::ThreeP_WaitP1:
    return "CIRCLE (3P): Point 1 of 3 — click or X,Y | ESC cancel";
  case AppCommandState::CirclePhase::ThreeP_WaitP2:
    return "CIRCLE (3P): Point 2 of 3 — click or X,Y | ESC cancel";
  case AppCommandState::CirclePhase::ThreeP_WaitP3:
    return "CIRCLE (3P): Point 3 of 3 — click or X,Y | ESC cancel";
  }
  return "";
}

const char* ModifyCommandFooterHint(const AppCommandState& st) {
  using K = AppCommandState::Kind;
  using MP = AppCommandState::ModifyPhase;
  if (st.active == K::Paste && st.modifyPhase == MP::NeedDestination)
    return "PASTE: Click destination point | ESC cancel";
  if (st.active != K::Move && st.active != K::Copy)
    return "";
  if (st.modifyPhase == MP::PickSelection)
    return "MOVE/COPY: Click opposite corners of selection window | ESC cancel";
  if (st.modifyPhase == MP::NeedBase)
    return "MOVE/COPY: Base point — click or X,Y | ESC cancel";
  if (st.modifyPhase == MP::NeedDestination)
    return "MOVE/COPY: Second point — click or X,Y or @dx,dy from base | ESC cancel";
  return "";
}

const char* RotateCommandFooterHint(const AppCommandState& st) {
  using K = AppCommandState::Kind;
  using RP = AppCommandState::RotatePhase;
  if (st.active != K::Rotate)
    return "";
  switch (st.rotatePhase) {
  case RP::PickSelection:
    return "ROTATE: Window-select — click two corners | ESC cancel";
  case RP::NeedBase:
    return "ROTATE: Base point — click or X,Y | ESC cancel";
  case RP::NeedAngleOrReference:
    return "ROTATE: ° clockwise / DMS | R ref | C copy | ESC (north=0° CW)";
  case RP::Ref_WaitP1:
    return "ROTATE ref: First point | C toggles copy | ESC cancel";
  case RP::Ref_WaitP2:
    return "ROTATE ref: Second point | C toggles copy | ESC cancel";
  case RP::AfterReference_WaitAngleOrP:
    return "ROTATE ref: New bearing ° from north (like props) | P two pts | C copy | ESC";
  case RP::AnglePoints_WaitP1:
    return "ROTATE angle pts: First point | C copy | ESC cancel";
  case RP::AnglePoints_WaitP2:
    return "ROTATE angle pts: Second point | C copy | ESC cancel";
  }
  return "";
}

const char* ScaleCommandFooterHint(const AppCommandState& st) {
  using K = AppCommandState::Kind;
  using MP = AppCommandState::ModifyPhase;
  using SP = AppCommandState::ScalePhase;
  if (st.active != K::Scale)
    return "";
  if (st.modifyPhase == MP::PickSelection)
    return "SCALE: Window-select — click two corners | ESC cancel";
  if (st.modifyPhase == MP::NeedBase)
    return "SCALE: Base point — click or X,Y | ESC cancel";
  if (st.modifyPhase == MP::NeedDestination) {
    switch (st.scalePhase) {
    case SP::FactorPick:
      return "SCALE: Second point or type factor (>0) — dist/base-ref | R = two-point ref length | ESC";
    case SP::Ref_WaitP1:
      return "SCALE ref: First point of reference length | ESC cancel";
    case SP::Ref_WaitP2:
      return "SCALE ref: Second point (reference length) | ESC cancel";
    case SP::NewLength_WaitTypedOrP1:
      return "SCALE ref: Type new length (model units) or pick first point of new length | ESC";
    case SP::NewLength_WaitP2:
      return "SCALE ref: Second point of new length (preview) | ESC cancel";
    default:
      return "";
    }
  }
  return "";
}

const char* DeleteCommandFooterHint(const AppCommandState& st) {
  if (st.active != AppCommandState::Kind::Delete)
    return "";
  return "DELETE: Window-select — click two corners | ESC cancel";
}

const char* JoinCommandFooterHint(const AppCommandState& st) {
  if (st.active != AppCommandState::Kind::Join)
    return "";
  return "JOIN: Window-select — click two corners | ESC cancel";
}

const char* TrimCommandFooterHint(const AppCommandState& st) {
  using K = AppCommandState::Kind;
  using TP = AppCommandState::TrimPhase;
  if (st.active != K::Trim)
    return "";
  switch (st.trimPhase) {
  case TP::SelectCuttingEdges:
    return "TRIM: Pick cutting edges (hover highlights) | Enter | type L — draw the trim line | ESC cancel";
  case TP::CuttingLine_WaitP1:
    return "TRIM: First point of the trim line | type T — pick cutting edges instead | ESC cancel";
  case TP::CuttingLine_WaitP2:
    return "TRIM: Second point — dashed = removed part (midpoint picks side) | Ortho | ESC";
  case TP::SelectTrimTargets:
    return "TRIM: Click segment near end to remove | Enter done | ESC cancel";
  }
  return "";
}

const char* OffsetCommandFooterHint(const AppCommandState& st) {
  using K = AppCommandState::Kind;
  using OP = AppCommandState::OffsetPhase;
  if (st.active != K::Offset)
    return "";
  switch (st.offsetPhase) {
  case OP::WaitSelectEntity:
    return "OFFSET: Pick line, circle, arc, ellipse, or polyline | ESC cancel";
  case OP::WaitDistanceOrThrough:
    return "OFFSET: Type distance then pick side — or through-click (line / circle / arc) | ESC cancel";
  case OP::WaitSidePick:
    return "OFFSET: Pick side of object (polyline/ellipse use closest edge) | ESC cancel";
  }
  return "";
}

const char* ZoomCommandFooterHint(const AppCommandState& st) {
  if (st.active != AppCommandState::Kind::Zoom)
    return "";
  return "ZOOM WINDOW: Two corners (unsnapped) — rubber previews fit area | ESC cancel";
}

const char* LineCommandFooterHint(const AppCommandState& st) {
  using LP = AppCommandState::LinePhase;
  using SAP = AppCommandState::SegmentAnglePickPhase;
  if (st.active != AppCommandState::Kind::Line)
    return "";
  if (st.linePhase == LP::NeedFirstPoint)
    return "LINE: First point — click or X,Y | ESC ends command";
  if (st.linePhase == LP::NeedNextPoint && st.segmentAngleKeyboardAwaitBearing)
    return "LINE: Type bearing ° CW from N (decimal/DMS) | blank Enter cancels | ESC ends command";
  if (st.linePhase == LP::NeedNextPoint && st.segmentAnglePickPhase == SAP::WaitP1)
    return "LINE bearing pick: First direction point — click | 2P started | ESC cancels pick";
  if (st.linePhase == LP::NeedNextPoint && st.segmentAnglePickPhase == SAP::WaitP2)
    return "LINE bearing pick: Second point — direction | ESC cancels pick";
  if (st.linePhase == LP::NeedNextPoint && st.segmentAnglePickPhase == SAP::WaitAdjustOrCommit)
    return "LINE bearing pick: Enter locks | +90/-45 adjust+lock | ESC cancels pick";
  if (st.linePhase == LP::NeedNextPoint && st.segmentAngleLockActive)
    return "LINE (bearing lock): distance ± along ray | click on line | X,Y | @dx,dy | A clears";
  // Basic next-point (ortho or not). Bracketed [A]/[2P] are rendered as clickable
  // links in the command-line footer (DrawCommandLinePanel); the same text feeds the
  // dynamic-cursor label and the height calc, so keep it identical.
  return "Next: click; X, Y; @dx,dy; [A]zimuth, [2P];";
}

const char* DrawingExtrasFooterHint(const AppCommandState& st) {
  using K = AppCommandState::Kind;
  using PP = AppCommandState::PolylinePhase;
  using AP = AppCommandState::ArcPhase;
  using EP = AppCommandState::EllipsePhase;
  using TP = AppCommandState::TextCmdPhase;
  using MP = AppCommandState::MtextPhase;
  using DP = AppCommandState::DimPhase;

  if (st.active == K::IdPoint)
    return "ID: Pick point (OSNAP when enabled) or type X,Y — logs UCS World | ESC cancel";

  if (st.active == K::SurveyInverse) {
    using SIP = AppCommandState::SurveyInversePhase;
    if (st.surveyInversePhase == SIP::WaitFrom)
      return "INVERSE: First point — pick or X,Y (Easting, Northing) | ESC cancel";
    return "INVERSE: Second point — pick or X,Y / @ from first | ESC cancel";
  }

  if (st.active == K::SurfaceElevGrade) {
    using SEP = AppCommandState::SurfaceElevPhase;
    if (st.surfaceElevPhase == SEP::WaitFirst)
      return "SURFELEV: Pick a point for its surface elevation | ESC cancel";
    return "SURFELEV: Second point for grade | ESC stops after the elevation";
  }

  if (st.active == K::DesignateBreakline)
    return "DESIGNATEBREAKLINE: Pick a line or polyline | ESC cancel";
  if (st.active == K::DesignateBoundary)
    return "DESIGNATEBOUNDARY: Pick a CLOSED polyline | ESC cancel";

  if (st.active == K::Polyline) {
    using SAP = AppCommandState::SegmentAnglePickPhase;
    if (st.polylinePhase == PP::NeedFirstPoint)
      return "POLYLINE: First point — click or X,Y | CLOSE closes | ESC cancel";
    if (st.polylinePhase == PP::NeedNextPoint && st.segmentAngleKeyboardAwaitBearing)
      return "POLYLINE: Type bearing ° CW from N | blank Enter cancels | ESC cancel";
    if (st.polylinePhase == PP::NeedNextPoint && st.segmentAnglePickPhase == SAP::WaitP1)
      return "POLYLINE bearing pick: First direction click | ESC cancels pick";
    if (st.polylinePhase == PP::NeedNextPoint && st.segmentAnglePickPhase == SAP::WaitP2)
      return "POLYLINE bearing pick: Second point | ESC cancels pick";
    if (st.polylinePhase == PP::NeedNextPoint && st.segmentAnglePickPhase == SAP::WaitAdjustOrCommit)
      return "POLYLINE bearing pick: Enter locks | +90/-45 adjust+lock | ESC cancels pick";
    if (st.polylinePhase == PP::NeedNextPoint && st.segmentAngleLockActive)
      return "POLYLINE (bearing lock): distance ± | click on ray | X,Y | A clears | CLOSE / END";
    return "POLYLINE: Next — click | X,Y | @dx,dy | A/2P | CLOSE / END | ESC";
  }
  if (st.active == K::Arc) {
    switch (st.arcPhase) {
    case AP::WaitStart:
      return "ARC: Start point | ESC cancel";
    case AP::WaitMid:
      return "ARC: Point on arc | ESC cancel";
    case AP::WaitEnd:
      return "ARC: End point | ESC cancel";
    }
  }
  if (st.active == K::Ellipse) {
    switch (st.ellPhase) {
    case EP::WaitCenter:
      return "ELLIPSE: Center | ESC cancel";
    case EP::WaitMajorEnd:
      return "ELLIPSE: Major axis end | ESC cancel";
    case EP::WaitRatio:
      return "ELLIPSE: Ratio (0-1] on command line | Enter = 0.5 | ESC cancel";
    }
  }
  if (st.active == K::Text) {
    switch (st.textPhase) {
    case TP::WaitInsertion:
      return "TEXT: Insertion — click or X,Y | ESC cancel";
    case TP::WaitHeight:
      return "TEXT: Height — Enter for plot-scale default | ESC cancel";
    case TP::WaitRotation:
      return "TEXT: Rotation ° CW from north — decimal/DMS or Enter=0 | ESC cancel";
    case TP::WaitString:
      return "TEXT: Enter content | ESC cancel";
    }
  }
  if (st.active == K::Mtext) {
    switch (st.mtextPhase) {
    case MP::WaitCorner1:
      return "MTEXT: First corner | ESC cancel";
    case MP::WaitCorner2:
      return "MTEXT: Opposite corner | ESC cancel";
    case MP::WaitString:
      return "MTEXT: Edit in drawing box — Ctrl+Enter reformats | Save to place | Esc cancel";
    }
  }
  if (st.active == K::DimAligned || st.active == K::DimLinear) {
    switch (st.dimPhase) {
    case DP::WaitExt1:
      return st.active == K::DimLinear ? "DIMLINEAR: Extension 1 | ESC cancel" : "DIMALIGNED: Extension 1 | ESC cancel";
    case DP::WaitExt2:
      return st.active == K::DimLinear ? "DIMLINEAR: Extension 2 | ESC cancel" : "DIMALIGNED: Extension 2 | ESC cancel";
    case DP::WaitDimLinePt:
      return st.active == K::DimLinear
                 ? "DIMLINEAR: Line — dominant X vs Y from chord mid; H / V keys; X,Y | @ from chord mid | ESC"
                 : "DIMALIGNED: Offset — click | X,Y | @ from chord mid | ESC";
    }
  }
  if (st.active == K::DimAngular) {
    using DAP = AppCommandState::DimAngularPhase;
    switch (st.dimAngularPhase) {
    case DAP::WaitVertex:
      return "DIMANGULAR: Vertex | X,Y | ESC cancel";
    case DAP::WaitRay1:
      return "DIMANGULAR: First ray point | X,Y | @ from vertex | ESC";
    case DAP::WaitRay2:
      return "DIMANGULAR: Second ray point | X,Y | @ from vertex | ESC";
    case DAP::WaitArc:
      return "DIMANGULAR: Arc radius (bisector) | click | X,Y | @ from vertex | ESC";
    }
  }
  return "";
}

static bool ParseHexColorForViewport(const std::string& s, float* r, float* g, float* b) {
  if (s.size() < 4 || s[0] != '#')
    return false;
  auto hexVal = [](char c) -> int {
    if (c >= '0' && c <= '9')
      return c - '0';
    if (c >= 'a' && c <= 'f')
      return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F')
      return 10 + (c - 'A');
    return -1;
  };
  if (s.size() == 4) {
    const int rh = hexVal(s[1]);
    const int gh = hexVal(s[2]);
    const int bh = hexVal(s[3]);
    if (rh < 0 || gh < 0 || bh < 0)
      return false;
    *r = static_cast<float>(rh | (rh << 4)) / 255.f;
    *g = static_cast<float>(gh | (gh << 4)) / 255.f;
    *b = static_cast<float>(bh | (bh << 4)) / 255.f;
    return true;
  }
  if (s.size() != 7)
    return false;
  int rv = 0;
  int gv = 0;
  int bv = 0;
  for (int i = 0; i < 2; ++i) {
    const int d = hexVal(s[static_cast<size_t>(1 + i)]);
    if (d < 0)
      return false;
    rv = rv * 16 + d;
  }
  for (int i = 0; i < 2; ++i) {
    const int d = hexVal(s[static_cast<size_t>(3 + i)]);
    if (d < 0)
      return false;
    gv = gv * 16 + d;
  }
  for (int i = 0; i < 2; ++i) {
    const int d = hexVal(s[static_cast<size_t>(5 + i)]);
    if (d < 0)
      return false;
    bv = bv * 16 + d;
  }
  *r = static_cast<float>(rv) / 255.f;
  *g = static_cast<float>(gv) / 255.f;
  *b = static_cast<float>(bv) / 255.f;
  return true;
}

struct NamedRgbPreset {
  const char* storage;
  float r;
  float g;
  float b;
};

// Keep storage strings aligned with Properties combo (except ByLayer handled separately).

static const NamedRgbPreset kViewportColorPresets[] = {
    {"Red", 1.f, 0.f, 0.f},       {"Yellow", 1.f, 1.f, 0.f}, {"Green", 0.f, 1.f, 0.f},
    {"Cyan", 0.f, 1.f, 1.f},      {"Blue", 0.f, 0.f, 1.f}, {"Magenta", 1.f, 0.f, 1.f},
    {"White", 1.f, 1.f, 1.f},     {"Gray", 0.5f, 0.5f, 0.5f}, {"Black", 0.f, 0.f, 0.f},
    {"Orange", 1.f, 0.5f, 0.f},
};

static bool LookupNamedRgbPreset(const std::string& c, float* r, float* g, float* b) {
  for (const auto& p : kViewportColorPresets) {
    if (c == p.storage) {
      *r = p.r;
      *g = p.g;
      *b = p.b;
      return true;
    }
  }
  return false;
}

void ResolveStoredColorForViewport(const std::string& colorStorage, float transparency, float defaultR,
                                  float defaultG, float defaultB, float* outRgba) {
  const float tr = transparency < 0.f ? 0.f : std::clamp(transparency, 0.f, 1.f);
  const float alpha = 1.f - tr;
  const std::string& c = colorStorage;

  if (c.empty() || c == "ByLayer") {
    outRgba[0] = defaultR;
    outRgba[1] = defaultG;
    outRgba[2] = defaultB;
    outRgba[3] = alpha;
    return;
  }
  float r = defaultR;
  float g = defaultG;
  float bl = defaultB;
  if (!c.empty() && c[0] == '#') {
    if (ParseHexColorForViewport(c, &r, &g, &bl)) {
      outRgba[0] = r;
      outRgba[1] = g;
      outRgba[2] = bl;
      outRgba[3] = alpha;
      return;
    }
  }
  if (LookupNamedRgbPreset(c, &r, &g, &bl)) {
    outRgba[0] = r;
    outRgba[1] = g;
    outRgba[2] = bl;
    outRgba[3] = alpha;
    return;
  }
  outRgba[0] = defaultR;
  outRgba[1] = defaultG;
  outRgba[2] = defaultB;
  outRgba[3] = alpha;
}

static bool LayerNamesEqCi(const std::string& a, const std::string& b) {
  if (a.size() != b.size())
    return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
      return false;
  }
  return true;
}

const CadLayerRow* FindDrawingLayerRowCi(const AppCommandState& st, const std::string& layerName) {
  for (const auto& r : st.drawingLayerTable) {
    if (LayerNamesEqCi(r.name, layerName))
      return &r;
  }
  return nullptr;
}

float EffectiveEntityTransparency01(const EntityAttributes& e, const CadLayerRow* layer) {
  if (e.transparency >= 0.f)
    return std::clamp(e.transparency, 0.f, 1.f);
  if (layer)
    return std::clamp(layer->transparency, 0.f, 1.f);
  return 0.f;
}

float EffectiveEntityLineweightMm(const EntityAttributes& e, const CadLayerRow* layer) {
  if (e.lineweightMm >= 0.f)
    return e.lineweightMm;
  if (layer && layer->lineweightMm >= 0.f)
    return layer->lineweightMm;
  return 0.18f;
}

std::string EffectiveEntityLinetypeNameForViewport(const EntityAttributes& e, const CadLayerRow* layer) {
  const std::string c = CadCanonicalLinetypeNameForDxf(e.linetype);
  if (c.empty() || c == "ByLayer")
    return layer ? layer->linetype : std::string("Continuous");
  return e.linetype;
}

void ResolveEntityRgbaForViewport(const EntityAttributes& attr, const CadLayerRow* layer, float defaultR,
                                  float defaultG, float defaultB, float* outRgba) {
  const float tr = EffectiveEntityTransparency01(attr, layer);
  std::string col = attr.color;
  if (col.empty() || col == "ByLayer") {
    if (layer && !layer->color.empty() && layer->color != "ByLayer")
      col = layer->color;
  }
  ResolveStoredColorForViewport(col, tr, defaultR, defaultG, defaultB, outRgba);
}

struct DxfLwPair {
  int code;
  float mm;
};

static const DxfLwPair kDxfLwTable[] = {
    {0, 0.f},     {5, 0.05f},   {9, 0.09f},   {13, 0.13f},  {15, 0.15f},  {18, 0.18f},  {20, 0.20f},
    {25, 0.25f},  {30, 0.30f},  {35, 0.35f},  {40, 0.40f},  {50, 0.50f},  {53, 0.53f},  {60, 0.60f},
    {70, 0.70f},  {80, 0.80f},  {90, 0.90f},  {100, 1.00f}, {106, 1.06f}, {120, 1.20f}, {140, 1.40f},
    {158, 1.58f}, {200, 2.00f}, {211, 2.11f},
};

int CadDxfLineweightEnum370FromMm(float mm) {
  if (mm < 0.f)
    return -1;
  int best = 18;
  float bestD = 1e9f;
  for (const auto& e : kDxfLwTable) {
    const float d = std::fabs(e.mm - mm);
    if (d < bestD) {
      bestD = d;
      best = e.code;
    }
  }
  return best;
}

float CadDxfLineweightMmFromEnum370(int code) {
  if (code < 0)
    return -1.f;
  for (const auto& e : kDxfLwTable) {
    if (e.code == code)
      return e.mm;
  }
  return -1.f;
}

// ---------------------------------------------------------------------------
// PDFATTACH
// ---------------------------------------------------------------------------
void StartPdfAttachCommand(AppCommandState& st, std::vector<std::string>& log) {
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  st.selectedSurveyPointIndices.clear();
  st.selBoxWaitingSecond = false;
  st.active              = AppCommandState::Kind::PdfAttach;
  st.pdfAttachPhase      = AppCommandState::PdfAttachPhase::WaitDialog;
  st.pdfAttachDialogOpen = true;
  log.push_back("PDFATTACH — select PDF file and options in the dialog, then place the underlay.");
}

void SubmitPdfAttachInsertPoint(AppCommandState& st, float wx, float wy, std::vector<std::string>& log) {
  if (st.active != AppCommandState::Kind::PdfAttach ||
      st.pdfAttachPhase != AppCommandState::PdfAttachPhase::WaitInsertPoint)
    return;

  PdfAttachment att;
  bool ok = false;

  if (st.pdfAttachPreviewReady) {
    // Reuse the attachment already built by the async path — no rebuild needed.
    att                      = std::move(st.pdfAttachPreview);
    st.pdfAttachPreviewReady = false;
    ok                       = true;
  } else {
    // Fallback: synchronous build (should rarely happen — e.g. if the user
    // somehow reaches WaitInsertPoint without the async build completing).
    ok = PdfAttach_Build(st.pdfAttachFilePath,
                          st.pdfAttachSelectedPage,
                          st.pdfAttachRasterDpi,
                          st.pdfAttachSnapLines,
                          st.pdfAttachSnapCircles,
                          st.pdfAttachSnapText,
                          att);
  }

  if (ok) {
    att.insertX     = wx;
    att.insertY     = wy;
    att.scale       = st.pdfAttachScale;
    att.rotationDeg = st.pdfAttachRotDeg;
    const int nSnapLines   = static_cast<int>(att.snapLinesFlat.size())    / 4;
    const int nSnapCircles = static_cast<int>(att.snapCirclesCxCyR.size()) / 3;
    const int nSnapText    = static_cast<int>(att.snapTextPos.size())      / 2;
    PushUndoSnapshot(st, "PDF attach");
    st.pdfAttachments.push_back(std::move(att));
    char snapMsg[128];
    std::snprintf(snapMsg, sizeof(snapMsg),
                  "PDFATTACH — placed.  Snap: %d lines, %d circles, %d text.",
                  nSnapLines, nSnapCircles, nSnapText);
    log.push_back(snapMsg);
  } else {
    log.push_back("PDFATTACH — failed to rasterize page.");
  }

  st.active         = AppCommandState::Kind::None;
  st.pdfAttachPhase = AppCommandState::PdfAttachPhase::WaitDialog;
}

void CancelPdfAttachCommand(AppCommandState& st, std::vector<std::string>& log) {
  log.push_back("PDFATTACH cancelled.");
  // If a background build is running, detach it so Cancel returns immediately.
  // The AsyncBuild owns the thread; move it out to a short-lived cleanup thread
  // that waits for the render to finish before dropping the struct.
  if (st.pdfAttachAsync) {
    auto ab = std::move(st.pdfAttachAsync); // st.pdfAttachAsync is now null
    std::thread([moved = std::move(ab)]() mutable {
      if (moved && moved->thread.joinable())
        moved->thread.join();
    }).detach();
  }
  ResetAllCadDraftTools(st);
  st.active = AppCommandState::Kind::None;
}

void VectorizePdfAttachmentLines(AppCommandState& st, int pdfIndex, std::vector<std::string>& log) {
  if (pdfIndex < 0 || pdfIndex >= static_cast<int>(st.pdfAttachments.size()))
    return;
  const PdfAttachment& att = st.pdfAttachments[static_cast<size_t>(pdfIndex)];
  const auto& snap = att.snapLinesFlat;
  if (snap.empty()) {
    log.push_back("Vectorize — no line snap geometry in this PDF underlay.");
    return;
  }
  PushUndoSnapshot(st, "Vectorize PDF");
  constexpr float kPi = 3.14159265f;
  const float cosR = std::cos(att.rotationDeg * kPi / 180.f);
  const float sinR = std::sin(att.rotationDeg * kPi / 180.f);
  int n = 0;
  for (size_t i = 0; i + 3 < snap.size(); i += 4) {
    const float sx1 = snap[i]     * att.scale, sy1 = snap[i + 1] * att.scale;
    const float sx2 = snap[i + 2] * att.scale, sy2 = snap[i + 3] * att.scale;
    // Z = 0 is correct here regardless of the UCS: a PDF underlay is a flat raster/vector
    // sheet with no elevation of its own.
    st.userLinesFlat.push_back(att.insertX + sx1 * cosR - sy1 * sinR);
    st.userLinesFlat.push_back(att.insertY + sx1 * sinR + sy1 * cosR);
    st.userLinesFlat.push_back(0.f);
    st.userLinesFlat.push_back(att.insertX + sx2 * cosR - sy2 * sinR);
    st.userLinesFlat.push_back(att.insertY + sx2 * sinR + sy2 * cosR);
    st.userLinesFlat.push_back(0.f);
    ++n;
  }
  EnsureAttrCounts(st);
  // Tag newly-added lines with this underlay's layer.
  if (!att.layer.empty()) {
    const size_t total = st.userLinesFlat.size() / 6;
    const size_t firstNew = total - static_cast<size_t>(n);
    for (size_t k = firstNew; k < total && k < st.userLineAttrs.size(); ++k)
      st.userLineAttrs[k].layer = att.layer;
  }
  BumpCadGpuCache(st);
  char buf[128];
  std::snprintf(buf, sizeof(buf), "Vectorize — added %d line segment%s from PDF underlay.",
                n, n == 1 ? "" : "s");
  log.push_back(buf);
}

// ---------------------------------------------------------------------------
// ALIGN — 2D Helmert (similarity) transformation
// ---------------------------------------------------------------------------

// Solves the 4×4 normal equations A^T A p = A^T b via Gaussian elimination.
// Returns false if the system is singular (< 2 control pairs or degenerate).
static bool SolveHelmert4x4(
    const std::vector<AppCommandState::AlignControlPt>& pts,
    float* outA, float* outB, float* outTx, float* outTy)
{
  // Build M = A^T A (symmetric 4×4) and rhs = A^T b (4×1)
  // Using analytic structure:
  //   M[0][0] = Σ(xi²+yi²), M[0][1]=0, M[0][2]=Σxi, M[0][3]=Σyi
  //   M[1][1] = M[0][0],    M[1][2]=-Σyi, M[1][3]=Σxi
  //   M[2][2] = n,           M[2][3]=0
  //   M[3][3] = n
  double sr2 = 0, sx = 0, sy = 0, sX = 0, sY = 0, sxX_yY = 0, smyX_xY = 0;
  const int n = static_cast<int>(pts.size());
  for (const auto& p : pts) {
    const double xi = p.srcX, yi = p.srcY, Xi = p.dstX, Yi = p.dstY;
    sr2    += xi * xi + yi * yi;
    sx     += xi;
    sy     += yi;
    sX     += Xi;
    sY     += Yi;
    sxX_yY  += xi * Xi + yi * Yi;
    smyX_xY += -yi * Xi + xi * Yi;
  }

  // Augmented matrix [M | rhs], 4 rows × 5 cols
  double m[4][5] = {
    { sr2,  0.0,   sx,  sy,  sxX_yY  },
    { 0.0,  sr2,  -sy,  sx,  smyX_xY },
    {  sx,  -sy,  static_cast<double>(n), 0.0, sX },
    {  sy,   sx,  0.0,  static_cast<double>(n), sY },
  };

  // Gaussian elimination with partial pivoting
  for (int col = 0; col < 4; ++col) {
    int pivot = col;
    for (int row = col + 1; row < 4; ++row)
      if (std::fabs(m[row][col]) > std::fabs(m[pivot][col]))
        pivot = row;
    if (std::fabs(m[pivot][col]) < 1e-12)
      return false;
    if (pivot != col)
      for (int k = 0; k <= 4; ++k)
        std::swap(m[pivot][k], m[col][k]);
    const double inv = 1.0 / m[col][col];
    for (int row = col + 1; row < 4; ++row) {
      const double f = m[row][col] * inv;
      for (int k = col; k <= 4; ++k)
        m[row][k] -= f * m[col][k];
    }
  }
  // Back-substitution
  double sol[4];
  for (int row = 3; row >= 0; --row) {
    sol[row] = m[row][4];
    for (int k = row + 1; k < 4; ++k)
      sol[row] -= m[row][k] * sol[k];
    sol[row] /= m[row][row];
  }
  *outA  = static_cast<float>(sol[0]);
  *outB  = static_cast<float>(sol[1]);
  *outTx = static_cast<float>(sol[2]);
  *outTy = static_cast<float>(sol[3]);
  return true;
}

// Applies X' = a*x - b*y + tx, Y' = b*x + a*y + ty to a 2D point.
static inline void HelmertPt(float a, float b, float tx, float ty, float* x, float* y) {
  const float ox = *x, oy = *y;
  *x = a * ox - b * oy + tx;
  *y = b * ox + a * oy + ty;
}

static void ApplyHelmertToAllGeometry(AppCommandState& st, float a, float b, float tx, float ty,
                                       const std::vector<SelectedEntity>* selEnts = nullptr,
                                       const std::vector<int>* selSurvey = nullptr) {
  const float sc  = std::sqrt(a * a + b * b);
  const float rad = std::atan2(b, a);
  const bool selective = selEnts != nullptr;
  std::unordered_set<int> sLines, sCircles, sArcs, sEllipses, sPolylines, sAnns;
  if (selective) {
    for (const auto& se : *selEnts) {
      switch (se.type) {
      case SelectedEntity::Type::LineSeg:    sLines.insert(se.index);    break;
      case SelectedEntity::Type::Circle:     sCircles.insert(se.index);  break;
      case SelectedEntity::Type::Arc:        sArcs.insert(se.index);     break;
      case SelectedEntity::Type::Ellipse:    sEllipses.insert(se.index); break;
      case SelectedEntity::Type::Polyline:   sPolylines.insert(se.index);break;
      case SelectedEntity::Type::Annotation: sAnns.insert(se.index);     break;
      default: break;
      }
    }
  }

  // Lines
  for (size_t i = 0; i + 5 < st.userLinesFlat.size(); i += 6) {
    if (selective && !sLines.count(static_cast<int>(i / 6))) continue;
    HelmertPt(a, b, tx, ty, &st.userLinesFlat[i],     &st.userLinesFlat[i + 1]);
    HelmertPt(a, b, tx, ty, &st.userLinesFlat[i + 3], &st.userLinesFlat[i + 4]);
  }

  // Circles
  for (size_t i = 0; i + 3 < st.userCirclesCxCyZR.size(); i += 4) {
    if (selective && !sCircles.count(static_cast<int>(i / 3))) continue;
    HelmertPt(a, b, tx, ty, &st.userCirclesCxCyZR[i], &st.userCirclesCxCyZR[i + 1]);
    st.userCirclesCxCyZR[i + 3] *= sc;
  }

  // Arcs
  for (size_t ai = 0; ai < st.userArcs.size(); ++ai) {
    if (selective && !sArcs.count(static_cast<int>(ai))) continue;
    auto& arc = st.userArcs[ai];
    HelmertPt(a, b, tx, ty, &arc.cx, &arc.cy);
    arc.r        *= sc;
    arc.startRad += rad;
  }

  // Ellipses
  for (size_t ei = 0; ei < st.userEllipses.size(); ++ei) {
    if (selective && !sEllipses.count(static_cast<int>(ei))) continue;
    auto& el = st.userEllipses[ei];
    float mx = el.cx + el.majVx, my = el.cy + el.majVy;
    HelmertPt(a, b, tx, ty, &el.cx, &el.cy);
    HelmertPt(a, b, tx, ty, &mx, &my);
    el.majVx = mx - el.cx;
    el.majVy = my - el.cy;
  }

  // Polylines
  if (!selective) {
    for (size_t i = 0; i + 1 < st.userPolylineVerts.size(); i += 3)
      HelmertPt(a, b, tx, ty, &st.userPolylineVerts[i], &st.userPolylineVerts[i + 1]);
  } else {
    for (size_t pi = 0; pi + 1 < st.userPolylineOffsets.size(); ++pi) {
      if (!sPolylines.count(static_cast<int>(pi))) continue;
      const int vStart = st.userPolylineOffsets[pi];
      const int vEnd   = st.userPolylineOffsets[pi + 1];
      for (int vi = vStart; vi < vEnd; ++vi) {
        size_t base = static_cast<size_t>(vi) * 3;
        if (base + 1 < st.userPolylineVerts.size())
          HelmertPt(a, b, tx, ty, &st.userPolylineVerts[base], &st.userPolylineVerts[base + 1]);
      }
    }
  }

  // Annotations — skip survey-linked MTEXT; repositioned after survey pts below.
  for (size_t ani = 0; ani < st.cadAnnotations.size(); ++ani) {
    auto& ann = st.cadAnnotations[ani];
    if (ann.surveyPointLabelForId >= 0)
      continue;
    if (selective && !sAnns.count(static_cast<int>(ani))) continue;
    HelmertPt(a, b, tx, ty, &ann.insX, &ann.insY);
    switch (ann.kind) {
    case CadAnnotation::Kind::Text:
      ann.rotationRad += rad;
      break;
    case CadAnnotation::Kind::Mtext:
      HelmertPt(a, b, tx, ty, &ann.boxMinX, &ann.boxMinY);
      HelmertPt(a, b, tx, ty, &ann.boxMaxX, &ann.boxMaxY);
      if (ann.boxMinX > ann.boxMaxX) std::swap(ann.boxMinX, ann.boxMaxX);
      if (ann.boxMinY > ann.boxMaxY) std::swap(ann.boxMinY, ann.boxMaxY);
      ann.insX = ann.boxMinX;
      ann.insY = ann.boxMinY;
      ann.plottedHeightInches = std::max(ann.plottedHeightInches * sc, 1e-6f);
      break;
    case CadAnnotation::Kind::DimAligned:
    case CadAnnotation::Kind::DimLinear: {
      HelmertPt(a, b, tx, ty, &ann.dimExt1X, &ann.dimExt1Y);
      HelmertPt(a, b, tx, ty, &ann.dimExt2X, &ann.dimExt2Y);
      ann.dimSignedOffset *= sc;
      float sx1 = 0.f, sy1 = 0.f, sx2 = 0.f, sy2 = 0.f, ttx = 0.f, tty = 0.f, nx = 0.f, ny = 0.f, ml = 0.f;
      if (CadDimAnyGeometry(ann, &sx1, &sy1, &sx2, &sy2, &ttx, &tty, &nx, &ny, &ml))
        ann.rotationRad = std::atan2(tty, ttx);
      ann.plottedHeightInches = std::max(ann.plottedHeightInches * sc, 1e-6f);
      CadDimRefreshMeasurementText(&ann, st.displayLinearPrecision, CadAngleDisplaySettings(st));
      break;
    }
    case CadAnnotation::Kind::DimAngular:
      HelmertPt(a, b, tx, ty, &ann.dimAngVertexX, &ann.dimAngVertexY);
      HelmertPt(a, b, tx, ty, &ann.dimExt1X, &ann.dimExt1Y);
      HelmertPt(a, b, tx, ty, &ann.dimExt2X, &ann.dimExt2Y);
      ann.dimSignedOffset *= sc;
      ann.plottedHeightInches = std::max(ann.plottedHeightInches * sc, 1e-6f);
      CadDimAngularSyncTextPlacement(&ann, st.modelUnitsPerPlottedInch);
      CadDimRefreshMeasurementText(&ann, st.displayLinearPrecision, CadAngleDisplaySettings(st));
      break;
    }
  }

  // PDF underlays
  constexpr float kR2D = 180.f / 3.14159265f;
  for (auto& att : st.pdfAttachments) {
    HelmertPt(a, b, tx, ty, &att.insertX, &att.insertY);
    att.scale      = std::max(att.scale * sc, 1e-9f);
    att.rotationDeg += rad * kR2D;
  }

  // Survey points
  const bool hasSelSurvey = selSurvey != nullptr;
  std::unordered_set<int> surveySet;
  if (hasSelSurvey)
    for (int idx : *selSurvey) surveySet.insert(idx);
  for (size_t i = 0; i < st.surveyPoints.size(); ++i) {
    if (hasSelSurvey && !surveySet.count(static_cast<int>(i))) continue;
    HelmertPt(a, b, tx, ty, &st.surveyPoints[i].easting, &st.surveyPoints[i].northing);
  }
  for (size_t i = 0; i < st.surveyPoints.size(); ++i) {
    if (hasSelSurvey && !surveySet.count(static_cast<int>(i))) continue;
    RepositionSurveyLabelMtextForPoint(st, i);
  }

  BumpCadGpuCache(st);
}

void StartAlignCommand(AppCommandState& st, std::vector<std::string>& log) {
  CancelActiveCommand(st, log);
  st.active = AppCommandState::Kind::Align;
  st.alignControlPts.clear();
  st.alignSelectionSnapshot.clear();
  st.alignSurveySnapshot.clear();

  const bool hasExisting = !st.selection.empty() || !st.selectedSurveyPointIndices.empty();
  if (hasExisting) {
    st.alignSelectionSnapshot = st.selection;
    st.alignSurveySnapshot    = st.selectedSurveyPointIndices;
    st.alignHasSelection      = true;
    st.alignPhase             = AppCommandState::AlignPhase::PickSrc;
    log.push_back("ALIGN — " + std::to_string(st.alignSelectionSnapshot.size()) + " CAD, " +
                  std::to_string(st.alignSurveySnapshot.size()) +
                  " survey point(s) selected. Pick SOURCE survey point 1 in drawing, then its real-world destination. "
                  "≥ 2 pairs → Enter to solve. ESC cancels.");
  } else {
    st.alignHasSelection = false;
    st.alignPhase        = AppCommandState::AlignPhase::PickSelection;
    st.selBoxWaitingSecond = false;
    log.push_back("ALIGN — window-select entities to transform, then press Enter. ESC cancels.");
  }
}

void RecalcAlignResult(AppCommandState& st) {
  auto& res = st.alignLastResult;
  res = AppCommandState::HelmertResult{};
  const size_t n = st.alignControlPts.size();
  if (n == 0)
    return;
  res.nPairs = static_cast<int>(n);
  if (n == 1) {
    res.a = 1.f; res.b = 0.f;
    res.tx = st.alignControlPts[0].dstX - st.alignControlPts[0].srcX;
    res.ty = st.alignControlPts[0].dstY - st.alignControlPts[0].srcY;
    res.valid = true;
    res.scale = 1.f;
    res.rotationCwNorthDeg = 0.f;
  } else {
    if (!SolveHelmert4x4(st.alignControlPts, &res.a, &res.b, &res.tx, &res.ty))
      return;
    res.valid = true;
    res.scale = std::sqrt(res.a * res.a + res.b * res.b);
    res.rotationCwNorthDeg = BearingCwNorthDegFromMathAngleRad(std::atan2(res.b, res.a));
  }
  float sumSqRes = 0.f;
  for (const auto& cp : st.alignControlPts) {
    const float predX = res.a * cp.srcX - res.b * cp.srcY + res.tx;
    const float predY = res.b * cp.srcX + res.a * cp.srcY + res.ty;
    const float rx = predX - cp.dstX;
    const float ry = predY - cp.dstY;
    const float r  = std::sqrt(rx * rx + ry * ry);
    res.pairResiduals.push_back(r);
    sumSqRes += r * r;
  }
  res.rms = std::sqrt(sumSqRes / static_cast<float>(n));
}

void ApplyAlignCommand(AppCommandState& st, std::vector<std::string>& log, bool applyScale) {
  const auto& res = st.alignLastResult;
  if (!res.valid) {
    log.push_back("ALIGN — no valid solution to apply.");
    return;
  }
  PushUndoSnapshot(st, "Align");

  // Compute actual transform parameters — optionally strip scale.
  float a = res.a, b = res.b, tx = res.tx, ty = res.ty;
  if (!applyScale) {
    const float theta = std::atan2(res.b, res.a);
    a = std::cos(theta);
    b = std::sin(theta);
    // Re-derive translation so centroid of sources maps to centroid of destinations.
    const int n = static_cast<int>(st.alignControlPts.size());
    float csx = 0.f, csy = 0.f, cdx = 0.f, cdy = 0.f;
    for (const auto& cp : st.alignControlPts) { csx += cp.srcX; csy += cp.srcY; cdx += cp.dstX; cdy += cp.dstY; }
    const float inv = 1.f / static_cast<float>(n);
    csx *= inv; csy *= inv; cdx *= inv; cdy *= inv;
    tx = cdx - (a * csx - b * csy);
    ty = cdy - (b * csx + a * csy);
  }
  const float appliedRotDeg = BearingCwNorthDegFromMathAngleRad(std::atan2(b, a));

  // Identify source and destination survey points BEFORE the transform.
  constexpr float kMatchTol = 0.01f;
  const int nPairs = static_cast<int>(st.alignControlPts.size());
  std::vector<int> srcIdx(static_cast<size_t>(nPairs), -1);
  std::vector<int> dstIdx(static_cast<size_t>(nPairs), -1);
  for (int i = 0; i < nPairs; ++i) {
    const auto& cp = st.alignControlPts[static_cast<size_t>(i)];
    float bestSrc = kMatchTol * kMatchTol, bestDst = kMatchTol * kMatchTol;
    for (int j = 0; j < static_cast<int>(st.surveyPoints.size()); ++j) {
      const float ex = st.surveyPoints[static_cast<size_t>(j)].easting;
      const float ny = st.surveyPoints[static_cast<size_t>(j)].northing;
      const float dSrc = (ex - cp.srcX) * (ex - cp.srcX) + (ny - cp.srcY) * (ny - cp.srcY);
      const float dDst = (ex - cp.dstX) * (ex - cp.dstX) + (ny - cp.dstY) * (ny - cp.dstY);
      if (dSrc < bestSrc) { bestSrc = dSrc; srcIdx[static_cast<size_t>(i)] = j; }
      if (dDst < bestDst) { bestDst = dDst; dstIdx[static_cast<size_t>(i)] = j; }
    }
  }

  // Apply Helmert — only to selected entities when a selection snapshot exists.
  if (st.alignHasSelection)
    ApplyHelmertToAllGeometry(st, a, b, tx, ty, &st.alignSelectionSnapshot, &st.alignSurveySnapshot);
  else
    ApplyHelmertToAllGeometry(st, a, b, tx, ty);

  // Post-transform: restore destination survey points and tag ADJ/CON.
  // Restrict scan to the survey snapshot (only those actually moved).
  std::set<int> taggedCon, taggedAdj;
  for (int i = 0; i < nPairs; ++i) {
    const int idx = dstIdx[static_cast<size_t>(i)];
    if (idx < 0 || taggedCon.count(idx)) continue;
    taggedCon.insert(idx);
    auto& p = st.surveyPoints[static_cast<size_t>(idx)];
    p.easting  = st.alignControlPts[static_cast<size_t>(i)].dstX;
    p.northing = st.alignControlPts[static_cast<size_t>(i)].dstY;
    if (p.description.find("CON") == std::string::npos)
      p.description += (p.description.empty() ? "CON" : " CON");
    EnsureSurveyPointLabelMtext(st, static_cast<size_t>(idx), nullptr);
  }
  // Source (ADJ): already at correct transformed position, just append ADJ.
  for (int i = 0; i < nPairs; ++i) {
    const int idx = srcIdx[static_cast<size_t>(i)];
    if (idx < 0 || taggedCon.count(idx) || taggedAdj.count(idx)) continue;
    taggedAdj.insert(idx);
    auto& p = st.surveyPoints[static_cast<size_t>(idx)];
    if (p.description.find("ADJ") == std::string::npos)
      p.description += (p.description.empty() ? "ADJ" : " ADJ");
    EnsureSurveyPointLabelMtext(st, static_cast<size_t>(idx), nullptr);
  }
  BumpCadGpuCache(st);

  // Build report.
  const int n = res.nPairs;
  char buf[256];
  std::string report;
  report += "HELMERT TRANSFORMATION REPORT\n";
  report += "==============================\n";
  std::snprintf(buf, sizeof(buf), "Control pairs:  %d\n", n);                                                        report += buf;
  std::snprintf(buf, sizeof(buf), "Scale (solved): %.8f%s\n", static_cast<double>(res.scale),
                applyScale ? "" : "  [not applied — rotation+translation only]");                                    report += buf;
  report += "Rotation:       " + CadFormatBearingCwNorthDegMinSec(appliedRotDeg) + "\n";
  std::snprintf(buf, sizeof(buf), "Translation X:  %.6f\n", static_cast<double>(tx));                               report += buf;
  std::snprintf(buf, sizeof(buf), "Translation Y:  %.6f\n", static_cast<double>(ty));                               report += buf;
  std::snprintf(buf, sizeof(buf), "Point error:    %.6f  (avg. distance each src maps from its dst)\n\n",
                static_cast<double>(res.rms));                                                                       report += buf;
  report += "  Pair   Src Easting      Src Northing     Dst Easting      Dst Northing     Point Error\n";
  report += "  ----   -----------      ------------     -----------      ------------     -----------\n";
  for (int i = 0; i < n && i < static_cast<int>(st.alignControlPts.size()); ++i) {
    const auto& cp    = st.alignControlPts[static_cast<size_t>(i)];
    const float resid = i < static_cast<int>(res.pairResiduals.size()) ? res.pairResiduals[static_cast<size_t>(i)] : 0.f;
    std::snprintf(buf, sizeof(buf), "  %4d   %14.4f   %14.4f   %14.4f   %14.4f   %.6f\n",
                  i + 1, static_cast<double>(cp.srcX), static_cast<double>(cp.srcY),
                         static_cast<double>(cp.dstX), static_cast<double>(cp.dstY),
                         static_cast<double>(resid));
    report += buf;
  }

  char tabName[64];
  std::snprintf(tabName, sizeof(tabName), "ALIGN (%d pair%s, %s)", n, n == 1 ? "" : "s",
                applyScale ? "full" : "rot+trans");
  st.surveyReportTabs.emplace_back(std::string(tabName), std::move(report));
  st.surveyReportSelectLatestPending = true;

  st.alignControlPts.clear();
  st.alignPhase             = AppCommandState::AlignPhase::PickSrc;
  st.showAlignResultsWindow = false;
  log.push_back(std::string("ALIGN applied (") + (applyScale ? "scale+rot+trans" : "rot+trans only") +
                ") — report in the Reports tab.");
}

void ExecuteAlignCommand(AppCommandState& st, std::vector<std::string>& log) {
  if (st.alignControlPts.empty()) {
    log.push_back("ALIGN — need at least 1 control point pair.");
    return;
  }
  RecalcAlignResult(st);
  if (!st.alignLastResult.valid) {
    log.push_back("ALIGN — control points are degenerate (coincident or collinear); cannot solve.");
    return;
  }
  st.showAlignResultsWindow = true;
  st.active                 = AppCommandState::Kind::None;
  log.push_back("ALIGN — solution ready. Review pairs in the results window, then click Apply.");
}

const char* AlignCommandFooterHint(const AppCommandState& st) {
  if (st.active != AppCommandState::Kind::Align)
    return "";
  using AP = AppCommandState::AlignPhase;
  if (st.alignPhase == AP::PickSelection)
    return "ALIGN: window-select entities to transform, then press Enter";
  if (st.alignPhase == AP::PickSrc)
    return "ALIGN: pick SOURCE survey point in drawing (snap to it) — Enter to solve";
  return "ALIGN: pick/type DESTINATION real-world X,Y for this source point";
}

bool LoadApplicationFont() {
  ImGuiIO& io = ImGui::GetIO();
  // Tahoma is the classic nanoCAD / Windows-2000 UI font. Fall back to Segoe UI.
  const char* candidates[] = {
      "C:/Windows/Fonts/tahoma.ttf",
      "C:/Windows/Fonts/Tahoma.ttf",
      "C:/Windows/Fonts/segoeui.ttf",
      "C:/Windows/Fonts/calibri.ttf",
  };
  ImFontConfig cfg;
  cfg.OversampleH = 2;
  cfg.OversampleV = 1;
  cfg.PixelSnapH  = true;  // crisp small classic text
  for (const char* path : candidates) {
    ImFont* f = io.Fonts->AddFontFromFileTTF(path, 16.0f, &cfg);
    if (f) {
      io.FontDefault = f;
      FontReg::SetDefault(f);  // fallback for unresolved CAD fonts
      return true;
    }
  }
  return false;
}

void RepeatLastCommand(AppCommandState& st, std::vector<std::string>& log) {
  using K = AppCommandState::Kind;
  switch (st.lastCommand) {
    case K::Line:       StartLineCommand(st, log);       break;
    case K::Circle:     StartCircleCommand(st, log);     break;
    case K::Polyline:   StartPolylineCommand(st, log);   break;
    case K::Rect:       StartRectCommand(st, log);       break;
    case K::Arc:        StartArcCommand(st, log);        break;
    case K::Ellipse:    StartEllipseCommand(st, log);    break;
    case K::Text:       StartTextCommand(st, log);       break;
    case K::Mtext:      StartMtextCommand(st, log);      break;
    case K::DimAligned: StartDimAlignedCommand(st, log); break;
    case K::DimLinear:  StartDimLinearCommand(st, log);  break;
    case K::DimAngular: StartDimAngularCommand(st, log); break;
    case K::Move:       StartMoveCommand(st, log);       break;
    case K::Copy:       StartCopyCommand(st, log);       break;
    case K::Rotate:     StartRotateCommand(st, log);     break;
    case K::Scale:      StartScaleCommand(st, log);      break;
    case K::Delete:     StartDeleteCommand(st, log);     break;
    case K::Join:       StartJoinCommand(st, log);       break;
    case K::Trim:       StartTrimCommand(st, log);       break;
    case K::Offset:     StartOffsetCommand(st, log);     break;
    case K::Hatch:      StartHatchCommand(st, log);      break;
    default: break;
  }
}

void StartQuickSelectCommand(AppCommandState& st, std::vector<std::string>& log) {
  st.showQuickSelectWindow = true;
  log.push_back("QUICKSELECT — filter entities by type and property.");
}
