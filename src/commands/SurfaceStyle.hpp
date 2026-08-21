#pragma once

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "CadEntities.hpp"

// Pure surface-style helpers (REQ-070 / ADR-036 (d)). Dependency-free — no AppCommandState, no UI,
// no GL — so `SurfaceStyleTests` links them without the GUI stack, exactly as `TextStyle.hpp` beside
// this file is. The drawing owns the `std::vector<SurfaceStyle>`; each `CadSurface` references one by
// name, and resolution is on READ (ADR-036 (d)): nothing is baked onto the surface, so editing a
// style cannot touch the surface's definition and therefore cannot re-triangulate it — which is
// REQ-070's central constraint, made structural rather than remembered.

namespace SurfaceStyles {

inline constexpr const char* kStandardName = "Standard";

/// Find a style by exact name, or nullptr.
inline const SurfaceStyle* Find(const std::vector<SurfaceStyle>& styles, const std::string& name) {
  for (const auto& s : styles)
    if (s.name == name) return &s;
  return nullptr;
}
inline SurfaceStyle* Find(std::vector<SurfaceStyle>& styles, const std::string& name) {
  for (auto& s : styles)
    if (s.name == name) return &s;
  return nullptr;
}

/// The built-in "Standard" style — what a new drawing starts with and what an unresolved name falls
/// back to.
///
/// **A surface opens as a contour map: border plus major and minor contours, triangles OFF.** That is
/// the Civil 3D default and it is what a topo plan is actually for — the triangulation is the model,
/// the contours are the deliverable, and a mesh of 200,000 edges is not something anyone wants to
/// look at once the surface is built. Points stay off because the surface's source points are already
/// drawn as survey points, and drawing them a second time is not a default anyone asked for.
///
/// This is a **deliberate change of appearance for drawings that already contain a surface**: before
/// REQ-070 a surface drew as its triangle edges and nothing else, so the day this ships those
/// surfaces become contour maps. Chosen by the user on 2026-08-21 over the no-visual-change
/// alternative (triangles left on), and recorded here rather than only in the task log because it is
/// the kind of decision a later reader would otherwise assume was an oversight. TRIANGLES is one
/// checkbox away in the Surface Style dialog.
///
/// Colours are named presets rather than ByLayer for the two contour sets, so a major and a minor
/// contour are distinguishable on a fresh drawing without visiting the dialog first. Everything the
/// user can already reason about from the layer — triangles, border, points — stays ByLayer.
inline SurfaceStyle StandardSurfaceStyle() {
  SurfaceStyle s;
  s.name = kStandardName;

  s.triangles.visible = false;
  s.border.visible = true;
  s.points.visible = false;

  s.minorContour.visible = true;
  s.minorContour.color = "Green";
  s.majorContour.visible = true;
  s.majorContour.color = "Orange";
  // A major contour is the one that carries the label on a plan sheet, so it is the heavier line.
  s.majorContour.lineweightMm = 0.35f;

  s.minorIntervalFt = 2.0;
  s.majorIntervalFt = 10.0;
  return s;
}

/// Guarantee a "Standard" style exists (inserted at the front if missing). Returns it.
inline SurfaceStyle& EnsureStandard(std::vector<SurfaceStyle>& styles) {
  if (SurfaceStyle* s = Find(styles, kStandardName)) return *s;
  styles.insert(styles.begin(), StandardSurfaceStyle());
  return styles.front();
}

/// The style table a brand-new drawing starts with — "Standard" and nothing else.
///
/// Defined in terms of \ref EnsureStandard so there is exactly one description of what "Standard" is.
/// A second literal here could drift from the one the loader synthesizes for a legacy file, which is
/// the class of difference issue #57 was made of.
inline std::vector<SurfaceStyle> DefaultSurfaceStyles() {
  std::vector<SurfaceStyle> styles;
  EnsureStandard(styles);
  return styles;
}

/// The style \p name refers to, falling back to "Standard", or nullptr if the table is empty.
///
/// This is the whole of "resolution is on read". An empty name and a name whose style was deleted
/// take the same path deliberately: REQ-070 states the deleted case, and the empty case is what every
/// surface in every `.gs` written before this field existed will present on first load.
inline const SurfaceStyle* Resolve(const std::vector<SurfaceStyle>& styles, const std::string& name) {
  if (!name.empty())
    if (const SurfaceStyle* s = Find(styles, name)) return s;
  if (const SurfaceStyle* s = Find(styles, kStandardName)) return s;
  return styles.empty() ? nullptr : &styles.front();
}

/// An interval in feet, formatted for a message a person reads (REQ-201): "2" and "0.5", never
/// "2.000000" or "5e-01".
///
/// Shared rather than duplicated at each message site, so a rejection and the confirmation that
/// follows it print the same number the same way.
inline std::string FormatFt(double v) {
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%.4f", v);
  std::string s(buf);
  while (!s.empty() && s.back() == '0') s.pop_back();
  if (!s.empty() && s.back() == '.') s.pop_back();
  return s;
}

/// True when \p majorFt is a whole multiple of \p minorFt — REQ-070's rule, with \p why filled in
/// with the specific message when it is not.
///
/// **Why this is a rule and not a preference:** a major contour is drawn heavier and is the one a
/// plan reader takes an elevation from. If the major interval is not a whole multiple of the minor,
/// major levels land between minor levels, and the heavy line at 107 sits in a sheet whose other
/// lines are at even feet — a mis-labelled contour, which is the failure REQ-070 names.
///
/// Compared by ratio with a tolerance, never with `==` on floats: 0.1 and 0.5 are both inexact in
/// binary, and `std::fmod(0.5, 0.1)` is 0.09999999999999995, not 0. The tolerance is relative to the
/// ratio, so it holds across a 0.1 ft interval and a 100 ft one alike.
inline bool IntervalsCompatible(double minorFt, double majorFt, std::string* why) {
  const auto fail = [&](const std::string& msg) {
    if (why) *why = msg;
    return false;
  };
  const auto fmt = [](double v) { return FormatFt(v); };

  if (!std::isfinite(minorFt) || minorFt <= 0.0)
    return fail("The minor contour interval must be greater than zero.");
  if (!std::isfinite(majorFt) || majorFt <= 0.0)
    return fail("The major contour interval must be greater than zero.");
  if (majorFt < minorFt)
    return fail("The major contour interval (" + fmt(majorFt) +
                ") must be at least as large as the minor interval (" + fmt(minorFt) + ").");

  const double ratio = majorFt / minorFt;
  const double nearest = std::floor(ratio + 0.5);
  if (std::fabs(ratio - nearest) > 1.0e-6 * (nearest > 1.0 ? nearest : 1.0)) {
    // Naming the two intervals that WOULD work turns a rejection into something actionable, which is
    // what REQ-201 asks a rejection to be.
    const double below = std::floor(ratio) * minorFt;
    const double above = std::ceil(ratio) * minorFt;
    return fail("A major interval of " + fmt(majorFt) + " ft is not a whole multiple of the " +
                fmt(minorFt) + " ft minor interval, so major contours would be labelled at "
                "elevations no minor contour is drawn at. Use " + fmt(below) + " or " + fmt(above) +
                " ft.");
  }
  if (why) why->clear();
  return true;
}

}  // namespace SurfaceStyles
