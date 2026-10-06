#include "PdfSnap.hpp"

#include <cmath>

namespace pdfview {

void SnapIndex::Build(std::vector<Pt>&& points) {
  points_ = std::move(points);
  grid_.clear();
  grid_.reserve(points_.size() / 4 + 1);
  for (size_t i = 0; i < points_.size(); ++i) {
    const int cx = static_cast<int>(std::floor(points_[i].first / kCell));
    const int cy = static_cast<int>(std::floor(points_[i].second / kCell));
    grid_[Key(cx, cy)].push_back(static_cast<uint32_t>(i));
  }
}

bool SnapIndex::Nearest(float x, float y, float radius, Pt& out) const {
  if (points_.empty() || radius <= 0.f)
    return false;
  const int x0 = static_cast<int>(std::floor((x - radius) / kCell)), x1 = static_cast<int>(std::floor((x + radius) / kCell));
  const int y0 = static_cast<int>(std::floor((y - radius) / kCell)), y1 = static_cast<int>(std::floor((y + radius) / kCell));
  float best = radius * radius;
  bool found = false;
  for (int cx = x0; cx <= x1; ++cx)
    for (int cy = y0; cy <= y1; ++cy) {
      const auto it = grid_.find(Key(cx, cy));
      if (it == grid_.end())
        continue;
      for (uint32_t i : it->second) {
        const float dx = points_[i].first - x, dy = points_[i].second - y, d2 = dx * dx + dy * dy;
        if (d2 <= best) {
          best = d2;
          out = points_[i];
          found = true;
        }
      }
    }
  return found;
}

} // namespace pdfview
