#pragma once

// REQ-378 clause 5 / D-2026-10-05-g (issue #696 P5) — applies one similarity transform (see
// geo/DrawingConversion.hpp) to a whole drawing held in an AppCommandState.
//
// Storage is local: world = local + document origin. A world-space similarity T(w) = A + sR·w becomes
// local' = sR·local on every stored coordinate plus origin' = A + sR·origin, so no stored coordinate
// ever carries the (large) shift and nothing loses precision to it.

#include "CadCommands.hpp"
#include "geo/DrawingConversion.hpp"

#include <string>
#include <vector>

/// Kinds of object (with counts, e.g. "3 surfaces") that the transform cannot move, so a drawing
/// holding any of them cannot be converted. \p rotates is true when the transform turns the drawing
/// (a coordinate-system change): an arc or ellipse in a tilted plane cannot be turned exactly then.
/// Empty = convertible.
[[nodiscard]] std::vector<std::string> UnconvertibleKinds(const AppCommandState& st, bool rotates);

/// The drawing's extents in WORLD coordinates (the document origin included). False when it is empty;
/// the rect is then the origin.
bool DrawingWorldExtents(const AppCommandState& st, double* minX, double* maxX, double* minY, double* maxY);

/// Moves every object of \p st by \p t and relabels the survey points. Saved views, named UCSs and the
/// active UCS refer to the old coordinates and are cleared; the geographic marker, the transformation
/// and captured map areas belong to the old coordinate system and are reset (each is reported in
/// \p log). The caller has checked UnconvertibleKinds. Does not change the drawing unit or zone.
void ApplyDrawingConversion(AppCommandState& st, const geo::Similarity& t, std::vector<std::string>& log);

// ---- a clipboard (REQ-383 clause 7 / D-2026-10-07-a) --------------------------------------------
// A paste into a drawing of another coordinate system or unit is converted with the same transform as a
// whole drawing; these three move the clipboard's objects instead of a drawing's.

/// Kinds of copied object the transform cannot move exactly (see the drawing overload). Empty = convertible.
[[nodiscard]] std::vector<std::string> UnconvertibleKinds(const CadClipboard& cb, bool rotates);

/// The copied objects' extents in the SOURCE drawing's WORLD coordinates (CadClipboard::srcOrigin included).
/// False when there is nothing to measure.
bool ClipboardWorldExtents(const CadClipboard& cb, double* minX, double* maxX, double* minY, double* maxY);

/// Moves every copied object by \p t and re-expresses it in a destination whose document origin is
/// (\p destOriginX, \p destOriginY), so it lands at the same ground position. The clipboard's own origin becomes the
/// destination's; the caller sets the coordinate system and unit it now belongs to.
void ApplyClipboardConversion(CadClipboard& cb, const geo::Similarity& t, double destOriginX, double destOriginY);
