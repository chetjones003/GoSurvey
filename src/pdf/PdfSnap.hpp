#pragma once

// REQ-391 clause 5 — snapping to the ends and corners of a PDF page's own line work. SnapIndex is a plain
// nearest-point grid (no PDFium); PdfDocument::SnapPoints (PdfDocument.hpp) reads the points off a page.
//
// Real drawings are not tidy: a "circle" is often dozens of tiny straight steps, and a hatch or a symbol adds
// hundreds of vertices a few pixels apart. Two things keep snapping calm: the reader drops the vertices that are
// only the tiny steps of a curve, and every kept point carries a weight (a real line's end or corner outranks an
// odd vertex) that Nearest uses to prefer the point the user most likely means.

#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

namespace pdfview {

struct SnapPoint {
  float x = 0.f, y = 0.f;
  float weight = 1.f; ///< 3 = the end or corner of a real line; 1 = any other kept vertex
};

class SnapIndex {
public:
  using Pt = std::pair<float, float>;

  void Build(std::vector<SnapPoint>&& points);
  size_t Size() const { return points_.size(); }

  /// The best point within \p radius of (x, y) (same units as the points); false when none is that close. "Best" is
  /// the nearest, except that each step of weight above 1 counts as 15 % of the radius of extra closeness: a line's
  /// end a little farther away beats a stray vertex right under the pointer, but not one much farther.
  /// \p mergeRadius (> 0): a candidate with another of strictly higher weight within that distance is set aside, so a
  /// cluster of points a few screen pixels apart offers only its strongest (a circle's centre rather than its rim).
  /// Pass the screen distance in points: zoomed in, the cluster's points are far apart on screen and all return.
  bool Nearest(float x, float y, float radius, Pt& out, float mergeRadius = 0.f) const;

private:
  static constexpr float kCell = 24.f; ///< grid cell size in points
  static int64_t Key(int cx, int cy) { return (static_cast<int64_t>(cx) << 32) ^ static_cast<uint32_t>(cy); }
  std::vector<SnapPoint> points_;
  std::unordered_map<int64_t, std::vector<uint32_t>> grid_;
};

} // namespace pdfview
