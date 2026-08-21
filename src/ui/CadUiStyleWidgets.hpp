#pragma once
// The appearance vocabulary the UI offers — the named colour palette, the linetype names, and the
// lineweight ladder — plus the two small formatters that turn a stored value back into a label.
//
// **Moved here from CadUi.cpp, not copied.** They were file-static there, and the Surface Style
// dialog (REQ-070) needs the same three lists in a different translation unit. A second copy of a
// constant table is how a colour gets added to one picker and not the other — the same class of
// difference issue #57 was made of — so there is exactly one definition and both files include it.
//
// Nothing here knows about a document: these are lists of what the product offers, and every
// consumer maps them onto its own storage field.

#include <cmath>
#include <cstdio>
#include <cstddef>

/// One entry of the named colour palette. \c storage is what is written to
/// \c EntityAttributes::color and to a \c SurfaceComponentStyle; \c label is what the picker shows.
struct NamedColorPreset {
  const char* label;
  const char* storage;
  float r;
  float g;
  float b;
};

inline constexpr NamedColorPreset kNamedColors[] = {
    {"By Layer", "ByLayer", 1.f, 1.f, 1.f}, {"Red", "Red", 1.f, 0.f, 0.f}, {"Yellow", "Yellow", 1.f, 1.f, 0.f},
    {"Green", "Green", 0.f, 1.f, 0.f},    {"Cyan", "Cyan", 0.f, 1.f, 1.f}, {"Blue", "Blue", 0.f, 0.f, 1.f},
    {"Magenta", "Magenta", 1.f, 0.f, 1.f}, {"White", "White", 1.f, 1.f, 1.f}, {"Gray", "Gray", 0.5f, 0.5f, 0.5f},
    {"Black", "Black", 0.f, 0.f, 0.f},    {"Orange", "Orange", 1.f, 0.5f, 0.f},
};
inline constexpr int kNamedColorCount =
    static_cast<int>(sizeof(kNamedColors) / sizeof(kNamedColors[0]));

inline constexpr const char* kEntityLinetypeLabels[] = {"By Layer", "By Block", "Continuous", "Dashed", "Hidden", "Center",
                                            "Phantom", "Divide", "Border"};
inline constexpr const char* kEntityLinetypeStorage[] = {"ByLayer", "ByBlock", "Continuous", "DASHED", "HIDDEN", "CENTER",
                                               "PHANTOM", "DIVIDE", "BORDER"};
inline constexpr int kEntityLinetypeCount =
    static_cast<int>(sizeof(kEntityLinetypeLabels) / sizeof(kEntityLinetypeLabels[0]));

inline constexpr const char* kLayerLinetypeLabels[] = {"Continuous", "Dashed", "Hidden", "Center", "Phantom", "Divide", "Border"};
inline constexpr const char* kLayerLinetypeStorage[] = {"Continuous", "DASHED", "HIDDEN", "CENTER", "PHANTOM", "DIVIDE",
                                                "BORDER"};
inline constexpr int kLayerLinetypeCount =
    static_cast<int>(sizeof(kLayerLinetypeLabels) / sizeof(kLayerLinetypeLabels[0]));

inline constexpr float kUiLineweightMmPresets[] = {
    -1.f,  0.f,   0.05f, 0.09f, 0.13f, 0.15f, 0.18f, 0.20f, 0.25f, 0.30f, 0.35f, 0.40f,
    0.50f, 0.53f, 0.60f, 0.70f, 0.80f, 0.90f, 1.00f, 1.06f, 1.20f, 1.40f, 1.58f, 2.00f, 2.11f};
inline constexpr int kUiLineweightPresetCount =
    static_cast<int>(sizeof(kUiLineweightMmPresets) / sizeof(kUiLineweightMmPresets[0]));

inline constexpr float kUiTransparencyPresets[] = {-1.f, 0.f, 0.25f, 0.5f, 0.75f, 0.9f, 1.f};
inline constexpr int kUiTransparencyPresetCount =
    static_cast<int>(sizeof(kUiTransparencyPresets) / sizeof(kUiTransparencyPresets[0]));

/// Label for a lineweight in millimetres. \p layerRow picks the wording for the -1 sentinel: a layer
/// row's "no weight of its own" is "Default", an object's is "By Layer" — the same value meaning the
/// same thing about a different owner.
inline void SnprintLineweightPresetLabel(char* buf, size_t cap, float mm, bool layerRow) {
  if (mm < 0.f) {
    if (layerRow)
      std::snprintf(buf, cap, "Default");
    else
      std::snprintf(buf, cap, "By Layer");
    return;
  }
  std::snprintf(buf, cap, "%.2f mm", static_cast<double>(mm));
}

/// Index into \ref kUiLineweightMmPresets nearest to \p mm, or 0 for the ByLayer sentinel.
///
/// Nearest rather than exact: a lineweight can arrive from DXF at a value the ladder does not carry,
/// and a combo that showed nothing selected would look like the value had been lost.
inline int LineweightPresetIndexFromMm(float mm) {
  if (mm < 0.f)
    return 0;
  int best = 1;
  float bestD = 1e18f;
  for (int i = 1; i < kUiLineweightPresetCount; ++i) {
    const float d = std::fabs(mm - kUiLineweightMmPresets[i]);
    if (d < bestD) {
      bestD = d;
      best = i;
    }
  }
  return best;
}
