#include "ViewportZoomScale.hpp"

#include "CadCommands.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>

namespace {

[[nodiscard]] std::string NormalizeScaleName(const std::string& name) {
  std::string out;
  out.reserve(name.size());
  for (unsigned char ch : name) {
    if (!std::isspace(ch))
      out.push_back(static_cast<char>(ch));
  }
  return out;
}

}  // namespace

void EnsureDefaultViewportZoomScales(std::vector<CadAnnotationScale>& scales) {
  if (!scales.empty())
    return;
  const CadAnnotationScale defaults[] = {
      {"1:1000", 1.f, 1000.f}, {"1:500", 1.f, 500.f}, {"1:200", 1.f, 200.f}, {"1:100", 1.f, 100.f},
      {"1:50", 1.f, 50.f},     {"1:20", 1.f, 20.f},   {"1:10", 1.f, 10.f},   {"1:1", 1.f, 1.f},
  };
  scales.assign(std::begin(defaults), std::end(defaults));
}

bool TryParseViewportZoomRatio(const std::string& text, float* paperUnits, float* drawingUnits) {
  if (paperUnits == nullptr || drawingUnits == nullptr)
    return false;
  const std::string trimmed = NormalizeScaleName(text);
  const auto colon = trimmed.find(':');
  if (colon == std::string::npos || colon == 0 || colon + 1 >= trimmed.size())
    return false;
  try {
    const std::string paperText = trimmed.substr(0, colon);
    const std::string drawingText = trimmed.substr(colon + 1);
    std::size_t paperEnd = 0, drawingEnd = 0;
    const float paper = std::stof(paperText, &paperEnd);
    const float drawing = std::stof(drawingText, &drawingEnd);
    if (paperEnd != paperText.size() || drawingEnd != drawingText.size() ||
        !std::isfinite(paper) || !std::isfinite(drawing) || !(paper > 0.f) || !(drawing > 0.f) ||
        !std::isfinite(drawing / paper) || !(drawing / paper > 0.f))
      return false;
    *paperUnits = paper;
    *drawingUnits = drawing;
    return true;
  } catch (...) {
    return false;
  }
}

bool AddViewportZoomScale(AppCommandState& st, const std::string& name, float paperUnits, float drawingUnits) {
  const std::string normalized = NormalizeScaleName(name);
  if (normalized.empty() || !std::isfinite(paperUnits) || !std::isfinite(drawingUnits) ||
      !(paperUnits > 0.f) || !(drawingUnits > 0.f) ||
      !std::isfinite(drawingUnits / paperUnits) || !(drawingUnits / paperUnits > 0.f))
    return false;
  EnsureDefaultViewportZoomScales(st.viewportZoomScales);
  for (std::size_t i = 0; i < st.viewportZoomScales.size(); ++i) {
    if (NormalizeScaleName(st.viewportZoomScales[i].name) == normalized) {
      st.currentViewportZoomScaleIndex = static_cast<int>(i);
      return true;
    }
  }
  CadAnnotationScale entry;
  entry.name = normalized;
  entry.paperUnits = paperUnits;
  entry.drawingUnits = drawingUnits;
  st.viewportZoomScales.push_back(std::move(entry));
  st.currentViewportZoomScaleIndex = static_cast<int>(st.viewportZoomScales.size()) - 1;
  return true;
}

float ViewportZoomForModelUnitsPerInch(float modelUnitsPerInch, float viewportHeightPx, float dpiY) {
  if (!(modelUnitsPerInch > 0.f) || !(viewportHeightPx > 0.f) || !(dpiY > 0.f))
    return 1.f;
  const float screenInches = viewportHeightPx / dpiY;
  const float visibleWorld = modelUnitsPerInch * screenInches;
  return 100.f / std::max(visibleWorld, 1.e-6f);
}

void EnsureStandardAnnotationScales(std::vector<CadAnnotationScale>& scales, int drawingInsUnits) {
  if (!scales.empty())
    return;
  // AutoCAD's Default Scale List in AutoCAD's order. Each entry is paper units : drawing units, so the
  // model units per plotted inch is drawing / paper. Imperial drawings are in inches: 1" = 5' is 60.
  static const CadAnnotationScale kImperial[] = {
      {"1\" = 1\"", 1.f, 1.f},        {"1\" = 5'", 1.f, 60.f},        {"1\" = 10'", 1.f, 120.f},
      {"1\" = 20'", 1.f, 240.f},      {"1\" = 30'", 1.f, 360.f},      {"1\" = 40'", 1.f, 480.f},
      {"1\" = 50'", 1.f, 600.f},      {"1\" = 60'", 1.f, 720.f},      {"1\" = 70'", 1.f, 840.f},
      {"1\" = 75'", 1.f, 900.f},      {"1\" = 80'", 1.f, 960.f},      {"1\" = 100'", 1.f, 1200.f},
      {"1\" = 150'", 1.f, 1800.f},    {"1\" = 200'", 1.f, 2400.f},    {"1\" = 250'", 1.f, 3000.f},
      {"1\" = 500'", 1.f, 6000.f},    {"1\" = 1000'", 1.f, 12000.f},  {"1\" = 2000'", 1.f, 24000.f},
      {"1\" = 5000'", 1.f, 60000.f},
  };
  static const CadAnnotationScale kMetric[] = {
      {"1:1", 1.f, 1.f},     {"1:2", 1.f, 2.f},     {"1:4", 1.f, 4.f},     {"1:5", 1.f, 5.f},
      {"1:8", 1.f, 8.f},     {"1:10", 1.f, 10.f},   {"1:16", 1.f, 16.f},   {"1:20", 1.f, 20.f},
      {"1:30", 1.f, 30.f},   {"1:40", 1.f, 40.f},   {"1:50", 1.f, 50.f},   {"1:100", 1.f, 100.f},
      {"2:1", 2.f, 1.f},     {"4:1", 4.f, 1.f},     {"8:1", 8.f, 1.f},     {"10:1", 10.f, 1.f},
      {"100:1", 100.f, 1.f},
  };
  const bool imperial = drawingInsUnits == 1 || drawingInsUnits == 2;
  if (!imperial) {
    scales.assign(std::begin(kMetric), std::end(kMetric));
    return;
  }
  scales.assign(std::begin(kImperial), std::end(kImperial));
  if (drawingInsUnits == 2) {  // feet: 1 foot = 12 inches, so 1" = 5' is 5 feet per plotted inch
    for (auto& scale : scales)
      scale.drawingUnits /= 12.f;
  }
}

void SetCurrentViewportZoomScaleIndex(AppCommandState& st, int index) {
  EnsureDefaultViewportZoomScales(st.viewportZoomScales);
  if (index < 0 || index >= static_cast<int>(st.viewportZoomScales.size()))
    return;
  st.currentViewportZoomScaleIndex = index;
}

void ApplyViewportZoomScaleIndex(AppCommandState& st, int index, float viewportHeightPx, float dpiY) {
  EnsureDefaultViewportZoomScales(st.viewportZoomScales);
  if (index < 0 || index >= static_cast<int>(st.viewportZoomScales.size()))
    return;
  SetCurrentViewportZoomScaleIndex(st, index);
  const float mupi = CadViewportZoomScaleModelUnitsPerInch(st.viewportZoomScales[static_cast<std::size_t>(index)]);
  st.viewportZoom = ViewportZoomForModelUnitsPerInch(mupi, viewportHeightPx, dpiY);
  if (st.activeSpaceIndex == kModelSpaceIndex) {
    st.modelViewPanX = st.viewportPanX;
    st.modelViewPanY = st.viewportPanY;
    st.modelViewZoom = st.viewportZoom;
    st.modelViewSaved = true;
  }
  BumpCadGpuCache(st);
}

void EnsureViewportZoomScaleIndexForDisplay(AppCommandState& st) {
  EnsureDefaultViewportZoomScales(st.viewportZoomScales);
  if (st.viewportZoomScales.empty()) {
    st.currentViewportZoomScaleIndex = -1;
    return;
  }
  if (st.currentViewportZoomScaleIndex >= 0 &&
      st.currentViewportZoomScaleIndex < static_cast<int>(st.viewportZoomScales.size()))
    return;
  for (int i = 0; i < static_cast<int>(st.viewportZoomScales.size()); ++i) {
    if (st.viewportZoomScales[static_cast<std::size_t>(i)].name == "1:50") {
      st.currentViewportZoomScaleIndex = i;
      return;
    }
  }
  st.currentViewportZoomScaleIndex = 0;
}

std::string CadViewportZoomScaleStatusLabel(const CadAnnotationScale& scale) {
  return CadAnnotationScaleStatusLabel(scale);
}
