#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

// Pure stroke helpers for the PDF plot's lineweight and linetype path (REQ-446). Header-only and free of
// PDFium so the width and dash-unit rules are unit-testable (the PlotFont / OrthoConstrain precedent).
// PdfPlot pairs these with the viewport geometry and the shared linetype tessellator; those stay in the .cpp.
namespace plotstroke {

constexpr float kPtPerIn = 72.f;
constexpr float kPtPerMm = kPtPerIn / 25.4f;

// Stroke width of a line whose lineweight is unset (ByLayer resolving to none). This is the fixed width every
// plotted line had before REQ-446, so a plot with only default lineweights and continuous linetypes is unchanged.
constexpr float kDefaultWidthPt = 0.5f;

// Plotted stroke width in points for an effective lineweight in millimetres. A negative value means no lineweight
// is set anywhere (entity or layer) and plots at kDefaultWidthPt.
inline float WidthPt(float lineweightMm) {
  return lineweightMm < 0.f ? kDefaultWidthPt : lineweightMm * kPtPerMm;
}

// Strokes are grouped into one PDF path per (colour, width). Width is kept to 1/100 pt so equal widths from
// different entities share a path; the default 0.5 pt maps to exactly 50.
inline uint32_t WidthKey(float widthPt) {
  return static_cast<uint32_t>(std::lround(std::max(widthPt, 0.f) * 100.f));
}

// Linetype scale. The drawing has no LTSCALE setting yet, so it is fixed at 1, the value the DXF export writes.
constexpr float kLtScale = 1.f;

// Dash unit in paper inches (one linetype pattern unit) for a viewport at \p modelUnitsPerPaperIn. AutoCAD's rule:
// a model-space pattern unit is LTSCALE model units, and the viewport shrinks it by its scale. Clamped away from
// zero so the dash tessellator always advances.
inline float DashUnitIn(float modelUnitsPerPaperIn) {
  return std::max(kLtScale / std::max(modelUnitsPerPaperIn, 1.e-6f), 1.e-4f);
}

// Dash unit on the sheet itself. Paper-space geometry is already in paper inches, so one pattern unit is LTSCALE
// inches (AutoCAD's PSLTSCALE default).
constexpr float kSheetDashUnitIn = kLtScale;

}  // namespace plotstroke
