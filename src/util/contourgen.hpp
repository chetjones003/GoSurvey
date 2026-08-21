#pragma once

/// Contour generation over a TIN (REQ-070 / ADR-036 (f)).
///
/// Pure and dependency-free — only `<cstdint>` and `<vector>` — so `GoSurveyTests` links it without
/// a GL context or the GUI stack, for the same reason `tinbuild` beside it does (ADR-028 (c)).
///
/// **Contours are display geometry, not entities.** What comes out of here is regenerated from a
/// triangulation and a style, is never written to `.gs`, never enters selection, and never counts as
/// an entity in the drawing (REQ-070). The output is shaped like `userPolylineVerts` /
/// `userPolylineOffsets` / `userPolylineClosed` anyway, because that is what the line renderer
/// already consumes — and because REQ-071's later EXTRACT is then a copy rather than a conversion.
///
/// **Marching triangles, linear.** A contour level crossing a triangle is one segment between two
/// edge interpolations; those segments are chained into polylines through the triangle edges they
/// share. Smoothing is deliberately absent — ADR-028 left it undesigned, so ADR-036 (f) states there
/// is no smoothing slider rather than inventing one.
///
/// **Chaining is topological, never geometric.** Two segments join because they cross the *same
/// triangle edge*, identified by its two vertex indices — not because their endpoints compare equal
/// as floats. Contour vertices are computed by interpolation, so a float comparison is exactly the
/// wrong tool: two triangles sharing an edge would agree to within rounding and then fail to join,
/// leaving a contour that looks broken for no visible reason.
///
/// **The vertex tie is resolved once, for the whole surface.** A vertex whose Z equals a contour
/// level is treated as infinitesimally ABOVE it — everywhere, at every edge of every triangle
/// (ADR-036 (f)). Deciding it independently at each of a triangle's three edges is how a contour
/// ends up half-open: it passes through the vertex on one triangle and misses it on the neighbour.
/// The rule also removes the degenerate case outright, because the below-vertex of a crossed edge is
/// then always *strictly* below, so the interpolation denominator is never zero.
///
/// The arithmetic is `double` over `float` storage, per architecture §11.8 — the same widening
/// `TinElevationAt` documents, and for the same reason.

#include <cstdint>
#include <vector>

/// Contours for one call, in the `userPolyline*` layout the renderer already consumes.
struct ContourResult {
  /// Interleaved x,y,z (architecture §11.8), one triplet per contour vertex.
  std::vector<float> vertsXyz;

  /// Contour \c i owns vertex indices [`offsets[i]`, `offsets[i+1]`) — so \c offsets holds one more
  /// entry than there are contours, exactly as \c userPolylineOffsets does. Empty when there are no
  /// contours at all (not `{0}`), so `contourCount()` reads 0 without a special case.
  std::vector<int> offsets;

  /// The elevation each contour was generated at, one per contour. Carried out rather than left for
  /// the caller to re-derive: it is what REQ-071's EXTRACT names the resulting polyline by, and what
  /// a major/minor split is decided on.
  std::vector<double> levels;

  /// 1 when the contour returns to its own start, one per contour. A contour closes in the surface's
  /// interior and stops open where it runs off the border or into a REQ-069 void, and the renderer
  /// needs to know which — the same distinction \c userPolylineClosed carries.
  std::vector<std::uint8_t> closed;

  [[nodiscard]] int contourCount() const {
    return offsets.empty() ? 0 : static_cast<int>(offsets.size()) - 1;
  }
};

/// Contours through the triangulation (\p vertsXyz, \p indices) at every level in \p levels.
///
/// Takes the raw arrays rather than a `CadTin` so it stays GUI-free and testable without a GL
/// context, like everything in `tinbuild`; callers pass `tin->vertsXyz` and `tin->indices`.
///
/// \p levels need not be sorted or unique — it is normalised internally, and the output is ordered
/// by ascending level, so the same triangulation and the same set of levels always produce the same
/// contours in the same order however the caller listed them.
///
/// An empty triangulation, an empty level list, and a level list entirely outside the surface's
/// elevation range all produce an empty result. None of them is an error: a surface with no
/// triangles has no contours, which is not the same thing as a failure to generate them.
///
/// \p out is cleared first, and is left empty when it is null-checked away.
void GenerateContours(const std::vector<float>& vertsXyz, const std::vector<std::uint32_t>& indices,
                      const std::vector<double>& levels, ContourResult* out);

/// The whole multiples of \p interval lying within [\p minZ, \p maxZ] — the level list a contour
/// interval turns into.
///
/// Levels are multiples of the interval measured from elevation zero, not from the surface's low
/// point: contours at 100, 102, 104 are what a plan reader expects from a 2 ft interval, and they
/// must not move when a single low shot is added to the surface.
///
/// A \p interval that is zero, negative, or not finite yields no levels. That is a caller error, not
/// a value to interpret — REQ-070's rejection of a bad interval happens where the user typed it,
/// with the message it owes them; here it simply has nothing to generate. **The caller also bounds
/// the interval against the surface's range**: this function will happily produce a million levels
/// for a 0.001 ft interval over a 1,000 ft surface, because clamping that silently would draw
/// something other than what was asked for.
///
/// \p out is cleared first.
void ContourLevels(double minZ, double maxZ, double interval, std::vector<double>* out);
