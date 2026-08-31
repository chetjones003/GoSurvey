#pragma once

#include "CadEntities.hpp"
#include "util/cadblock.hpp"

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

// A selected native paper-space entity (REQ-037/039): index into the active layout's paper* stores.
// Lives here (the dependency-free paper header) so both the command layer and header-only, unit-testable
// selection helpers can name it. \c index: Line = segment (flat offset/6); others = index into the matching
// paper* vector.
struct PaperEntityRef {
  enum class Type : std::uint8_t { Line = 0, Text = 1, Circle = 2, Arc = 3, Ellipse = 4, Polyline = 5, Block = 6 };
  Type type = Type::Line;
  int  index = 0;
};

// Paper space data model (REQ-025/026/031, ADR-006).
//   Increment 1: named layouts, each a sheet with a paper size + orientation.
//   Later increments add Viewport (REQ-027) and per-viewport frozen layers (REQ-028).
//
// A drawing has Model space (the existing geometry) plus zero or more PaperLayouts.
// AppCommandState::activeSpaceIndex selects the active space: -1 = Model, >=0 = index
// into AppCommandState::paperLayouts.

constexpr int kModelSpaceIndex = -1;

// Common sheet sizes in PORTRAIT inches (width <= height). Orientation is applied per
// layout via PaperLayout::landscape.
struct PaperSizePreset {
  const char* name;
  float widthIn;
  float heightIn;
};

inline const PaperSizePreset kPaperSizePresets[] = {
    {"ANSI A (8.5\" x 11\")", 8.5f, 11.f},  {"ANSI B (11\" x 17\")", 11.f, 17.f},
    {"ANSI C (17\" x 22\")", 17.f, 22.f},   {"ANSI D (22\" x 34\")", 22.f, 34.f},
    {"ANSI E (34\" x 44\")", 34.f, 44.f},   {"ARCH A (9\" x 12\")", 9.f, 12.f},
    {"ARCH B (12\" x 18\")", 12.f, 18.f},   {"ARCH C (18\" x 24\")", 18.f, 24.f},
    {"ARCH D (24\" x 36\")", 24.f, 36.f},   {"ARCH E (36\" x 48\")", 36.f, 48.f},
};
constexpr int kPaperSizePresetCount = static_cast<int>(sizeof(kPaperSizePresets) / sizeof(kPaperSizePresets[0]));
constexpr int kDefaultPaperPresetIdx = 1;  // ANSI B (11" x 17")

// A viewport (REQ-027): a rectangular window on a paper layout showing model space at a
// given scale and center. Rect is in paper inches with the sheet's lower-left at (0,0).
struct Viewport {
  float paperXIn = 1.f;   // lower-left of the viewport rect, paper inches
  float paperYIn = 1.f;
  float paperWIn = 10.f;  // rect size, paper inches
  float paperHIn = 7.5f;
  double modelCenterX = 0.0;  // model-space point shown at the viewport's center
  double modelCenterY = 0.0;
  float scaleModelPerPaperIn = 50.f;  // model units per paper inch (AutoCAD viewport scale)
  std::string layer = "0";            // viewport's layer; if not plottable, its border is omitted from plots
  std::vector<std::string> frozenLayers;  // layer names hidden only in this viewport (REQ-028)
  // Per-viewport layer COLOR override (REQ-046): parallel arrays (layer name -> override color, same
  // storage encoding as CadLayerRow::color). An entry recolors that layer's entities only within this
  // viewport (on screen and in the plot). No entry = the layer's normal color.
  std::vector<std::string> vpColorLayers;
  std::vector<std::string> vpColorValues;

  // REQ-061: the camera this viewport views model space through. The defaults — azimuth 0,
  // elevation 90 (straight down), no roll, orthographic — reproduce the historical flat plan
  // projection (ModelToPaperIn) to the last bit, so every legacy drawing and every legacy .gs
  // is unchanged. Orientation is measured exactly as the model-viewport Camera measures it
  // (azimuth about world +Z, 0 = looking north; elevation above the horizon, +90 = plan).
  // Persisted additively in .gs (GsIo). Named UCS/view definitions stay per-drawing (issue #155):
  // only this active orientation is per-viewport.
  float camAzimuthDeg = 0.f;
  float camElevationDeg = 90.f;
  float camRollDeg = 0.f;
  bool  camPerspective = false;
  float camFovDeg = 45.f;

  float safeScale() const { return scaleModelPerPaperIn > 1.e-6f ? scaleModelPerPaperIn : 1.e-6f; }

  // REQ-061: true when the camera is the historical straight-down orthographic plan view. Callers
  // keep the exact pre-change 2D path (ModelToPaperIn, ImGui AddCircle, uniform px/model) on this.
  [[nodiscard]] bool cameraIsPlan() const {
    return !camPerspective && std::fabs(camAzimuthDeg) < 1.e-4f &&
           std::fabs(camElevationDeg - 90.f) < 1.e-4f && std::fabs(camRollDeg) < 1.e-4f;
  }
};

// REQ-061: the standard views a user can point a paper-space viewport at from the Viewports window.
// Azimuth/elevation match the model-viewport Camera and the ViewCube (REQ-059): SW isometric is the
// ViewCube home — azimuth 45 deg, elevation atan(1/sqrt(2)) ~= 35.264 deg.
struct ViewportStandardView {
  const char* name;
  float azimuthDeg;
  float elevationDeg;
};
inline const ViewportStandardView kViewportStandardViews[] = {
    {"Plan (Top)", 0.f, 90.f},   {"Bottom", 0.f, -90.f},        {"Front", 0.f, 0.f},
    {"Back", 180.f, 0.f},        {"Left", 90.f, 0.f},          {"Right", 270.f, 0.f},
    {"SW Isometric", 45.f, 35.26439f},  {"SE Isometric", 315.f, 35.26439f},
    {"NE Isometric", 225.f, 35.26439f}, {"NW Isometric", 135.f, 35.26439f},
};
constexpr int kViewportStandardViewCount =
    static_cast<int>(sizeof(kViewportStandardViews) / sizeof(kViewportStandardViews[0]));

// Pure transform (REQ-027): a model-space point → paper-space inches within \p vp.
inline void ModelToPaperIn(const Viewport& vp, double mx, double my, float* outPaperX, float* outPaperY) {
  const float s = vp.safeScale();
  const float cx = vp.paperXIn + vp.paperWIn * 0.5f;
  const float cy = vp.paperYIn + vp.paperHIn * 0.5f;
  *outPaperX = cx + static_cast<float>((mx - vp.modelCenterX) / static_cast<double>(s));
  *outPaperY = cy + static_cast<float>((my - vp.modelCenterY) / static_cast<double>(s));
}

/// REQ-050: the model-units-per-plotted-inch that a **plain MTEXT** is sized by when it is drawn
/// THROUGH \p vp — the viewport's own scale, so the text's plotted height stays constant on the sheet
/// whatever that viewport is scaled to.
///
/// **Survey-point label MTEXT is excluded** and keeps \p drawingModelUnitsPerPlottedInch: REQ-050 says
/// so in as many words, because a survey label's size is owned by its own layout rather than by the
/// sheet it happens to be viewed through.
///
/// Pure, and shared with the overlay deliberately. The viewport text pass used to inline
/// `drawingModelUnitsPerPlottedInch` for every annotation, so the same MTEXT read at one size in model
/// space and another through a 1:50 viewport — and no test could see it, because the rule lived inside
/// a draw call. Anything that sizes MTEXT through a viewport calls this, so a regression in it is a
/// failing test rather than a screenshot someone has to notice.
[[nodiscard]] inline float MtextScaleThroughViewport(const CadAnnotation& a, const Viewport& vp,
                                                     float drawingModelUnitsPerPlottedInch) {
  // REQ-050 keeps single-line TEXT (and every non-MTEXT kind) on the drawing plot scale. Gate here
  // so a later caller cannot size TEXT off the viewport by accident.
  if (a.kind != CadAnnotation::Kind::Mtext || a.surveyPointLabelForId >= 0)
    return drawingModelUnitsPerPlottedInch;
  return vp.safeScale();
}

/// Height on the sheet (paper inches) of \p a when it is plotted through \p vp.
/// Plain MTEXT is \c plottedHeightInches (REQ-050 constant plotted height). TEXT, dimensions, and
/// survey-label MTEXT convert model height through the viewport scale, matching the on-screen overlay.
[[nodiscard]] inline float AnnotationPlottedHeightThroughViewport(const CadAnnotation& a, const Viewport& vp,
                                                                  float drawingModelUnitsPerPlottedInch) {
  const float mup = MtextScaleThroughViewport(a, vp, drawingModelUnitsPerPlottedInch);
  const float s = vp.safeScale();
  if (s <= 0.f)
    return a.plottedHeightInches;
  return a.plottedHeightInches * mup / s;
}

/// Everything the layout-viewport TEXT/MTEXT overlay needs from one annotation: isolation skip,
/// the scale REQ-050 picks, and the font family the draw call must pass to SHX/TTF resolve.
/// CadUi uses this struct's fields rather than inlining the drawing scale or ImGui::GetFont().
struct ViewportTextOverlayPlan {
  bool skipHidden = false;
  float modelUnitsPerPlottedInch = 50.f;
  std::string fontFamily;
};

[[nodiscard]] inline ViewportTextOverlayPlan PlanViewportTextOverlay(const CadAnnotation& a, bool entityHidden,
                                                                    const Viewport& vp,
                                                                    float drawingModelUnitsPerPlottedInch) {
  ViewportTextOverlayPlan p;
  p.skipHidden = entityHidden;
  p.modelUnitsPerPlottedInch = MtextScaleThroughViewport(a, vp, drawingModelUnitsPerPlottedInch);
  p.fontFamily = a.fontFamily;
  return p;
}

// Per-viewport layer freeze (REQ-028): toggle and query frozen layers for a viewport.
inline void ToggleFrozenLayerInViewport(Viewport& vp, const std::string& layerName) {
  for (size_t i = 0; i < vp.frozenLayers.size(); ++i) {
    if (vp.frozenLayers[i] == layerName) {
      vp.frozenLayers.erase(vp.frozenLayers.begin() + static_cast<std::ptrdiff_t>(i));
      return;
    }
  }
  vp.frozenLayers.push_back(layerName);
}

inline bool IsLayerFrozenInViewport(const Viewport& vp, const std::string& layerName) {
  for (const auto& fl : vp.frozenLayers) {
    if (fl == layerName)
      return true;
  }
  return false;
}

// VPFREEZE / VPTHAW (REQ-046): freeze (ensure present) or thaw (ensure absent) a layer in a viewport.
// Unlike ToggleFrozenLayerInViewport these are idempotent — freezing an already-frozen layer is a no-op.
inline void FreezeLayerInViewport(Viewport& vp, const std::string& layerName) {
  if (!IsLayerFrozenInViewport(vp, layerName))
    vp.frozenLayers.push_back(layerName);
}

inline void ThawLayerInViewport(Viewport& vp, const std::string& layerName) {
  for (size_t i = 0; i < vp.frozenLayers.size(); ++i) {
    if (vp.frozenLayers[i] == layerName) {
      vp.frozenLayers.erase(vp.frozenLayers.begin() + static_cast<std::ptrdiff_t>(i));
      return;
    }
  }
}

// Per-viewport layer COLOR override (REQ-046): set/replace, clear, and query a layer's override color
// (CadLayerRow::color encoding) within a viewport. Query returns nullptr when the layer has no override.
inline void SetViewportLayerColor(Viewport& vp, const std::string& layerName, const std::string& colorStorage) {
  for (size_t i = 0; i < vp.vpColorLayers.size(); ++i) {
    if (vp.vpColorLayers[i] == layerName) {
      vp.vpColorValues[i] = colorStorage;
      return;
    }
  }
  vp.vpColorLayers.push_back(layerName);
  vp.vpColorValues.push_back(colorStorage);
}

inline void ClearViewportLayerColor(Viewport& vp, const std::string& layerName) {
  for (size_t i = 0; i < vp.vpColorLayers.size(); ++i) {
    if (vp.vpColorLayers[i] == layerName) {
      vp.vpColorLayers.erase(vp.vpColorLayers.begin() + static_cast<std::ptrdiff_t>(i));
      vp.vpColorValues.erase(vp.vpColorValues.begin() + static_cast<std::ptrdiff_t>(i));
      return;
    }
  }
}

inline const std::string* ViewportLayerColorOverride(const Viewport& vp, const std::string& layerName) {
  for (size_t i = 0; i < vp.vpColorLayers.size() && i < vp.vpColorValues.size(); ++i)
    if (vp.vpColorLayers[i] == layerName)
      return &vp.vpColorValues[i];
  return nullptr;
}

// A named page setup (paper size + orientation + plot settings) the user can apply to layouts via the
// Page Setup Manager. Plot device is fixed to GoSurvey's PDF output for now (real plotting = Inc 4).
struct PageSetup {
  std::string name = "Standard";
  int   presetIdx = kDefaultPaperPresetIdx;  // index into kPaperSizePresets, or -1 = custom
  float portraitWidthIn = kPaperSizePresets[kDefaultPaperPresetIdx].widthIn;
  float portraitHeightIn = kPaperSizePresets[kDefaultPaperPresetIdx].heightIn;
  bool  landscape = true;
  bool  fitToPaper = false;
  float scaleModelPerPaperIn = 1.f;  // plot scale: model units per paper inch (when not fit-to-paper)
  int   plotArea = 0;                // 0 = Layout (only option for now)
  float offsetXIn = 0.f;
  float offsetYIn = 0.f;
  bool  centerPlot = false;
};

struct PaperLayout {
  std::string name = "Layout1";
  // Portrait dimensions in inches (width <= height); orientation chooses how they map to the sheet.
  float portraitWidthIn = kPaperSizePresets[kDefaultPaperPresetIdx].widthIn;
  float portraitHeightIn = kPaperSizePresets[kDefaultPaperPresetIdx].heightIn;
  bool landscape = true;
  int presetIdx = kDefaultPaperPresetIdx;  // index into kPaperSizePresets, or -1 = custom
  std::vector<Viewport> viewports;         // REQ-027

  // Native paper-space geometry (REQ-037, ADR-009): entities that live on this sheet, in PAPER INCHES
  // (sheet origin at 0,0), separate from model space and from viewport content. Owned by this layout.
  //   paperLines: flat (x0,y0,z0, x1,y1,z1) per segment, z = 0 on the sheet; paperLineAttrs is parallel.
  //   paperTexts: CadAnnotation with insX/insY in paper inches (kind Text/Mtext); paperTextAttrs parallel.
  std::vector<float>            paperLines;
  std::vector<EntityAttributes> paperLineAttrs;
  std::vector<CadAnnotation>    paperTexts;
  std::vector<EntityAttributes> paperTextAttrs;
  // Full primitive store (REQ-038, ADR-013): mirrors the model arrays exactly so clipboard paste can route
  // model↔paper. Coordinates are paper inches. Each vector is parallel to its *Attrs vector.
  std::vector<float>            paperCircles;     ///< flat cx,cy,r triples
  std::vector<EntityAttributes> paperCircleAttrs;
  std::vector<CadArc>           paperArcs;
  std::vector<EntityAttributes> paperArcAttrs;
  std::vector<CadEllipse>       paperEllipses;
  std::vector<EntityAttributes> paperEllAttrs;
  std::vector<int>              paperPolyOffsets;  ///< vertex-offset table (starts with 0); empty = no polylines
  std::vector<float>            paperPolyVerts;    ///< flat x,y,z triples
  std::vector<uint8_t>          paperPolyClosed;
  std::vector<EntityAttributes> paperPolyAttrs;
  std::vector<CadFilledRegion>  paperFilledRegions;     ///< Solid fills on the sheet, paper inches (REQ-038 addendum)
  std::vector<EntityAttributes> paperFilledRegionAttrs;
  std::vector<CadBlockRef>      paperBlockRefs;
  std::vector<EntityAttributes> paperBlockRefAttrs;
  // Current page-setup plot fields (the layout's own setup). Paper size/orientation above are part of it.
  std::string pageSetupName;               // name of the applied named setup, or "" = <None>
  bool  fitToPaper = false;
  float scaleModelPerPaperIn = 1.f;
  int   plotArea = 0;
  float offsetXIn = 0.f;
  float offsetYIn = 0.f;
  bool  centerPlot = false;
  // Saved paper-space view for this layout (so each layout keeps its own pan/zoom; fit to sheet on first entry).
  double viewPanX = 0.0;
  double viewPanY = 0.0;
  float  viewZoom = 1.f;
  bool   viewInit = false;

  float sheetWidthIn() const { return landscape ? portraitHeightIn : portraitWidthIn; }
  float sheetHeightIn() const { return landscape ? portraitWidthIn : portraitHeightIn; }
};

// Object snapping for native paper-space geometry (REQ-037, paper-only). Returns the nearest snap point —
// a paper line's endpoint or midpoint, or a text insertion point — within \p tolIn of (px,py), all in paper
// inches. False (and outputs untouched) when nothing is in range. Pure + header-only so it is unit-testable.
inline bool SnapPaperInchPoint(const PaperLayout& L, float px, float py, float tolIn, float* outX, float* outY) {
  float best2 = tolIn * tolIn;
  bool found = false;
  auto consider = [&](float x, float y) {
    const float dx = px - x, dy = py - y;
    const float d2 = dx * dx + dy * dy;
    if (d2 <= best2) {
      best2 = d2;
      *outX = x;
      *outY = y;
      found = true;
    }
  };
  for (size_t i = 0; i + 5 < L.paperLines.size(); i += 6) {
    consider(L.paperLines[i], L.paperLines[i + 1]);                                      // endpoint 1
    consider(L.paperLines[i + 3], L.paperLines[i + 4]);                                  // endpoint 2
    consider((L.paperLines[i] + L.paperLines[i + 3]) * 0.5f,
             (L.paperLines[i + 1] + L.paperLines[i + 4]) * 0.5f);                        // midpoint
  }
  for (const CadAnnotation& a : L.paperTexts)
    consider(a.insX, a.insY);  // text insertion point
  for (size_t i = 0; i + 2 < L.paperCircles.size(); i += 3) {
    const float cx = L.paperCircles[i], cy = L.paperCircles[i + 1], r = L.paperCircles[i + 2];
    consider(cx, cy);                                            // center
    consider(cx + r, cy); consider(cx - r, cy);                 // east / west quadrants
    consider(cx, cy + r); consider(cx, cy - r);                 // north / south quadrants
  }
  for (const CadArc& a : L.paperArcs) {
    consider(a.cx, a.cy);                                                       // center
    consider(a.cx + a.r * std::cos(a.startRad), a.cy + a.r * std::sin(a.startRad));               // start
    consider(a.cx + a.r * std::cos(a.startRad + a.sweepRad), a.cy + a.r * std::sin(a.startRad + a.sweepRad)); // end
  }
  for (const CadEllipse& e : L.paperEllipses)
    consider(e.cx, e.cy);  // ellipse center
  for (size_t v = 0; v + 2 < L.paperPolyVerts.size(); v += 3)
    consider(L.paperPolyVerts[v], L.paperPolyVerts[v + 1]);  // polyline vertices
  for (const CadBlockRef& br : L.paperBlockRefs)
    consider(br.xf.x, br.xf.y);
  return found;
}

// Approximate paper-text bounds in paper inches (REQ-039). The renderer treats the insertion point as the
// TOP-LEFT (matching model AddText): the glyphs occupy [insY - h, insY] in paper Y and [insX, insX + w] in X.
// Earlier code anchored the bounds above the text (insertion-as-bottom-left), so click-picking text was off by
// ~one line height (TASK-012 debt); anchor downward to match the glyphs. Pure + header-only so it is testable.
inline void PaperTextBoundsIn(const CadAnnotation& a, float* x0, float* y0, float* x1, float* y1) {
  const float h = std::max(0.01f, a.plottedHeightInches);
  const float w = std::max(h * 0.6f, h * 0.6f * static_cast<float>(a.text.size()));
  *x0 = a.insX;
  *y0 = a.insY - h;
  *x1 = a.insX + w;
  *y1 = a.insY;
}

// Box-select native paper-space geometry (REQ-039). Selects entities of every paper type inside the box
// [bx0,by0]-[bx1,by1] (paper inches, already normalized so bx0<=bx1, by0<=by1). windowMode=true (L→R drag):
// an entity is selected only when its bounding extent is fully inside the box; windowMode=false (R→L drag,
// crossing): selected when its extent overlaps the box. Text selects by its top-left bounds (matching the
// renderer + PickPaperEntityAt). Appends matches to \p out (does not clear). Pure + header-only.
inline void SelectPaperEntitiesInBox(const PaperLayout& L, float bx0, float by0, float bx1, float by1,
                                     bool windowMode, std::vector<PaperEntityRef>& out) {
  auto boxSel = [&](float ex0, float ey0, float ex1, float ey1) {
    return windowMode ? (ex0 >= bx0 && ex1 <= bx1 && ey0 >= by0 && ey1 <= by1)
                      : (ex0 <= bx1 && ex1 >= bx0 && ey0 <= by1 && ey1 >= by0);
  };
  for (int si = 0; si < static_cast<int>(L.paperLines.size() / 6); ++si) {
    const size_t i = static_cast<size_t>(si) * 6;
    const float lx0 = std::min(L.paperLines[i], L.paperLines[i + 3]);
    const float lx1 = std::max(L.paperLines[i], L.paperLines[i + 3]);
    const float ly0 = std::min(L.paperLines[i + 1], L.paperLines[i + 4]);
    const float ly1 = std::max(L.paperLines[i + 1], L.paperLines[i + 4]);
    if (boxSel(lx0, ly0, lx1, ly1))
      out.push_back({PaperEntityRef::Type::Line, si});
  }
  for (int ti = 0; ti < static_cast<int>(L.paperTexts.size()); ++ti) {
    float tx0, ty0, tx1, ty1;
    PaperTextBoundsIn(L.paperTexts[static_cast<size_t>(ti)], &tx0, &ty0, &tx1, &ty1);
    if (boxSel(tx0, ty0, tx1, ty1))
      out.push_back({PaperEntityRef::Type::Text, ti});
  }
  for (int ci = 0; ci < static_cast<int>(L.paperCircles.size() / 3); ++ci) {
    const size_t i = static_cast<size_t>(ci) * 3;
    const float cx = L.paperCircles[i], cy = L.paperCircles[i + 1], r = L.paperCircles[i + 2];
    if (boxSel(cx - r, cy - r, cx + r, cy + r))
      out.push_back({PaperEntityRef::Type::Circle, ci});
  }
  for (int ai = 0; ai < static_cast<int>(L.paperArcs.size()); ++ai) {
    const CadArc& a = L.paperArcs[static_cast<size_t>(ai)];
    if (boxSel(a.cx - a.r, a.cy - a.r, a.cx + a.r, a.cy + a.r))
      out.push_back({PaperEntityRef::Type::Arc, ai});
  }
  for (int ei = 0; ei < static_cast<int>(L.paperEllipses.size()); ++ei) {
    const CadEllipse& e = L.paperEllipses[static_cast<size_t>(ei)];
    const float rad = std::sqrt(e.majVx * e.majVx + e.majVy * e.majVy);
    if (boxSel(e.cx - rad, e.cy - rad, e.cx + rad, e.cy + rad))
      out.push_back({PaperEntityRef::Type::Ellipse, ei});
  }
  const int nPoly = static_cast<int>(L.paperPolyOffsets.size()) - 1;
  for (int pi = 0; pi < nPoly; ++pi) {
    const int v0 = L.paperPolyOffsets[static_cast<size_t>(pi)];
    const int v1 = L.paperPolyOffsets[static_cast<size_t>(pi + 1)];
    float ex0 = 1e30f, ey0 = 1e30f, ex1 = -1e30f, ey1 = -1e30f;
    for (int vi = v0; vi < v1; ++vi) {
      const float vx = L.paperPolyVerts[static_cast<size_t>(vi * 3)];
      const float vy = L.paperPolyVerts[static_cast<size_t>(vi * 3 + 1)];
      ex0 = std::min(ex0, vx); ey0 = std::min(ey0, vy);
      ex1 = std::max(ex1, vx); ey1 = std::max(ey1, vy);
    }
    if (v1 > v0 && boxSel(ex0, ey0, ex1, ey1))
      out.push_back({PaperEntityRef::Type::Polyline, pi});
  }
  for (int bi = 0; bi < static_cast<int>(L.paperBlockRefs.size()); ++bi) {
    const CadBlockRef& br = L.paperBlockRefs[static_cast<size_t>(bi)];
    if (boxSel(br.xf.x, br.xf.y, br.xf.x, br.xf.y))
      out.push_back({PaperEntityRef::Type::Block, bi});
  }
}
