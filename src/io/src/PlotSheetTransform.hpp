#pragma once

#include <algorithm>
#include <limits>

// Sheet placement for the PDF plot of a paper layout (REQ-445, D-2026-11-01-a). Header-only and free of PDFium so
// the placement rules are unit-testable (the PlotStroke / PlotFont precedent). PdfPlot gathers the sheet content into
// an Extent, builds one Transform per layout, and maps every sheet coordinate and text size through it before drawing.
//
// Rules, all in paper inches measured from the sheet's lower-left corner (no margins are modelled yet):
//   - scale:  fit-to-paper scales the content extent until it fills the sheet in its limiting direction;
//             otherwise the content is scaled by 1 / N for the plot ratio 1 : N.
//   - centre: when "centre the plot" is set, the scaled extent is centred on the sheet and the offsets are ignored.
//   - fit:    without centring, the scaled extent's lower-left corner moves to the sheet's lower-left corner.
//   - offset: otherwise the content moves by the stated X and Y offset.
// Lineweights are not scaled: they stay the width the style asks for, as in AutoCAD.
namespace plotsheet {

/// Bounding box of the sheet content in paper inches. Empty until a point is added.
struct Extent {
  float x0 = std::numeric_limits<float>::max();
  float y0 = std::numeric_limits<float>::max();
  float x1 = std::numeric_limits<float>::lowest();
  float y1 = std::numeric_limits<float>::lowest();

  bool empty() const { return x0 > x1 || y0 > y1; }
  void Add(float x, float y) {
    x0 = std::min(x0, x);
    y0 = std::min(y0, y);
    x1 = std::max(x1, x);
    y1 = std::max(y1, y);
  }
};

/// The layout's own Page Setup plot fields (PaperLayout), as the placement rules read them.
struct Settings {
  bool  fitToPaper = false;
  float plotRatio = 1.f;  ///< N in "1 : N"; values <= 0 plot at 1 : 1
  float offsetXIn = 0.f;
  float offsetYIn = 0.f;
  bool  centerPlot = false;
};

/// Maps a sheet point (paper inches) to its plotted position (paper inches). Text sizes are multiplied by k.
struct Transform {
  float k = 1.f;
  float dx = 0.f;
  float dy = 0.f;

  float X(float x) const { return x * k + dx; }
  float Y(float y) const { return y * k + dy; }
};

/// Placement of the content on a sheet of sheetW x sheetH inches. The default Settings give the identity transform,
/// so a layout with no Page Setup plot options plots exactly as before. An empty extent counts as the whole sheet.
inline Transform Compute(const Settings& s, float sheetW, float sheetH, const Extent& content) {
  float x0 = 0.f, y0 = 0.f, cw = sheetW, ch = sheetH;
  if (!content.empty()) {
    x0 = content.x0;
    y0 = content.y0;
    cw = std::max(content.x1 - content.x0, 1.e-6f);
    ch = std::max(content.y1 - content.y0, 1.e-6f);
  }
  Transform t;
  t.k = s.fitToPaper ? std::min(sheetW / cw, sheetH / ch) : 1.f / (s.plotRatio > 0.f ? s.plotRatio : 1.f);
  if (s.centerPlot) {
    t.dx = (sheetW - cw * t.k) * 0.5f - x0 * t.k;
    t.dy = (sheetH - ch * t.k) * 0.5f - y0 * t.k;
  } else {
    const float baseX = s.fitToPaper ? -x0 * t.k : 0.f;
    const float baseY = s.fitToPaper ? -y0 * t.k : 0.f;
    t.dx = baseX + s.offsetXIn;
    t.dy = baseY + s.offsetYIn;
  }
  return t;
}

}  // namespace plotsheet
