#pragma once

#include <string>

/// One drawable component of a surface style (REQ-070 / ADR-036 (d) (i)): the triangle edges, the
/// border, the major or minor contours, or the points.
///
/// The four properties are encoded exactly as \ref EntityAttributes encodes the same four, so the
/// resolvers the renderer already has (\c ResolveStoredColorForViewport, the lineweight helpers) work
/// on a component unchanged, and there is no second parsing path to keep in step with the first.
///
/// **Layer and plot style are deliberately absent** (ADR-036 (i)). A surface has exactly one layer —
/// its own \ref EntityAttributes — and GoSurvey has no plot-style table, so a per-component column for
/// either could be shown but never honoured, and a control the product will not obey is worse than an
/// absent one.
struct SurfaceComponentStyle {
  bool visible = true;
  /// "ByLayer", a `#RRGGBB` literal, or a named preset — \ref EntityAttributes::color's encoding.
  std::string color = "ByLayer";
  std::string linetype = "ByLayer";
  float lineweightMm = -1.f;  ///< Millimetres on paper; \c -1.f means ByLayer.

  bool operator==(const SurfaceComponentStyle& o) const {
    return visible == o.visible && color == o.color && linetype == o.linetype &&
           lineweightMm == o.lineweightMm;
  }
  bool operator!=(const SurfaceComponentStyle& o) const { return !(*this == o); }
};
