#pragma once

// Background-adaptive white/black (REQ-048 refinement — AutoCAD "color 7" behavior). White and black are
// the one color pair that must stay legible against either background, so they flip to match it:
//   - on a LIGHT background (the paper sheet / plot page), a NEAR-WHITE color renders BLACK;
//   - on a DARK background (model space), a NEAR-BLACK color renders WHITE.
// Every other color is left exactly as resolved. Applied AFTER color resolution at the paper-space
// render/plot sites. Pure + header-only so it is unit-testable and shared by UI (CadUi) and IO (PdfPlot).
inline void AdaptWhiteBlackToBackground(float* r, float* g, float* b, bool backgroundIsLight) {
  if (!r || !g || !b)
    return;
  const float lum = 0.299f * *r + 0.587f * *g + 0.114f * *b;  // perceived luminance (0..1)
  if (backgroundIsLight && lum >= 0.92f)
    *r = *g = *b = 0.f;  // near-white on a light sheet → black
  else if (!backgroundIsLight && lum <= 0.08f)
    *r = *g = *b = 1.f;  // near-black on a dark background → white
}
