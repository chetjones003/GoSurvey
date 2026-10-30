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

// Dash unit in paper inches for a sheet or viewport of the given paper size. Uses the same proportion the on-screen
// view uses (ViewportRenderer: dashPatScale = 0.045 x half-extent), so a dashed line on the sheet looks like the
// one on screen. Clamped away from zero so the dash tessellator always advances.
inline float DashUnitIn(float paperWIn, float paperHIn) {
  return std::max(0.01f, 0.045f * std::max(paperWIn, paperHIn) * 0.5f);
}

}  // namespace plotstroke
