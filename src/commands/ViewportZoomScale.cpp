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

void ApplyViewportZoomScaleIndex(AppCommandState& st, int index, float viewportHeightPx, float dpiY) {
  EnsureDefaultViewportZoomScales(st.viewportZoomScales);
  if (index < 0 || index >= static_cast<int>(st.viewportZoomScales.size()))
    return;
  st.currentViewportZoomScaleIndex = index;
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

void SyncCurrentViewportZoomScaleIndex(AppCommandState& st, float viewportHeightPx, float dpiY) {
  if (st.viewportZoomScales.empty()) {
    st.currentViewportZoomScaleIndex = -1;
    return;
  }
  const float currentZoom = std::max(st.viewportZoom, 1.e-9f);
  int best = -1;
  float bestErr = 1e9f;
  for (int i = 0; i < static_cast<int>(st.viewportZoomScales.size()); ++i) {
    const float mupi =
        CadViewportZoomScaleModelUnitsPerInch(st.viewportZoomScales[static_cast<std::size_t>(i)]);
    const float expected = ViewportZoomForModelUnitsPerInch(mupi, viewportHeightPx, dpiY);
    const float err = std::fabs(expected - currentZoom) / currentZoom;
    if (err < bestErr) {
      bestErr = err;
      best = i;
    }
  }
  st.currentViewportZoomScaleIndex = bestErr < 0.02f ? best : -1;
}

std::string CadViewportZoomScaleStatusLabel(const CadAnnotationScale& scale) {
  return CadAnnotationScaleStatusLabel(scale);
}
