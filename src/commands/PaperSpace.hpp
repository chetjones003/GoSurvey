#pragma once

#include <string>

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
constexpr int kDefaultPaperPresetIdx = 8;  // ARCH D

struct PaperLayout {
  std::string name = "Layout1";
  // Portrait dimensions in inches (width <= height); orientation chooses how they map to the sheet.
  float portraitWidthIn = kPaperSizePresets[kDefaultPaperPresetIdx].widthIn;
  float portraitHeightIn = kPaperSizePresets[kDefaultPaperPresetIdx].heightIn;
  bool landscape = true;
  int presetIdx = kDefaultPaperPresetIdx;  // index into kPaperSizePresets, or -1 = custom

  float sheetWidthIn() const { return landscape ? portraitHeightIn : portraitWidthIn; }
  float sheetHeightIn() const { return landscape ? portraitWidthIn : portraitHeightIn; }
};
