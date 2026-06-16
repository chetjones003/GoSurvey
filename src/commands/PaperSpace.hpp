#pragma once

#include <string>
#include <vector>

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

  float safeScale() const { return scaleModelPerPaperIn > 1.e-6f ? scaleModelPerPaperIn : 1.e-6f; }
};

// Pure transform (REQ-027): a model-space point → paper-space inches within \p vp.
inline void ModelToPaperIn(const Viewport& vp, double mx, double my, float* outPaperX, float* outPaperY) {
  const float s = vp.safeScale();
  const float cx = vp.paperXIn + vp.paperWIn * 0.5f;
  const float cy = vp.paperYIn + vp.paperHIn * 0.5f;
  *outPaperX = cx + static_cast<float>((mx - vp.modelCenterX) / static_cast<double>(s));
  *outPaperY = cy + static_cast<float>((my - vp.modelCenterY) / static_cast<double>(s));
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
  // Current page-setup plot fields (the layout's own setup). Paper size/orientation above are part of it.
  std::string pageSetupName;               // name of the applied named setup, or "" = <None>
  bool  fitToPaper = false;
  float scaleModelPerPaperIn = 1.f;
  int   plotArea = 0;
  float offsetXIn = 0.f;
  float offsetYIn = 0.f;
  bool  centerPlot = false;

  float sheetWidthIn() const { return landscape ? portraitHeightIn : portraitWidthIn; }
  float sheetHeightIn() const { return landscape ? portraitWidthIn : portraitHeightIn; }
};
