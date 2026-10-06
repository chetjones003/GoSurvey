#pragma once

// REQ-391 clause 5 — snapping to the ends and corners of a PDF page's own line work. SnapIndex is a plain
// nearest-point grid (no PDFium); PdfDocument::SnapPoints (PdfDocument.hpp) reads the points off a page.

#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

namespace pdfview {

class SnapIndex {
public:
  using Pt = std::pair<float, float>;

  void Build(std::vector<Pt>&& points);
  size_t Size() const { return points_.size(); }

  /// The point nearest (x, y) within \p radius (same units as the points); false when none is that close.
  bool Nearest(float x, float y, float radius, Pt& out) const;

private:
  static constexpr float kCell = 24.f; ///< grid cell size in points
  static int64_t Key(int cx, int cy) { return (static_cast<int64_t>(cx) << 32) ^ static_cast<uint32_t>(cy); }
  std::vector<Pt> points_;
  std::unordered_map<int64_t, std::vector<uint32_t>> grid_;
};

} // namespace pdfview
