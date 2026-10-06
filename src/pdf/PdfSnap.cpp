#include "PdfSnap.hpp"

#include <algorithm>
#include <cmath>

namespace pdfview {

void SnapIndex::Build(std::vector<SnapPoint>&& points) {
  points_ = std::move(points);
  grid_.clear();
  grid_.reserve(points_.size() / 4 + 1);
  for (size_t i = 0; i < points_.size(); ++i) {
    const int cx = static_cast<int>(std::floor(points_[i].x / kCell));
    const int cy = static_cast<int>(std::floor(points_[i].y / kCell));
    grid_[Key(cx, cy)].push_back(static_cast<uint32_t>(i));
  }
}

bool SnapIndex::Nearest(float x, float y, float radius, Pt& out, float mergeRadius) const {
  if (points_.empty() || radius <= 0.f)
    return false;
  const int x0 = static_cast<int>(std::floor((x - radius) / kCell)), x1 = static_cast<int>(std::floor((x + radius) / kCell));
  const int y0 = static_cast<int>(std::floor((y - radius) / kCell)), y1 = static_cast<int>(std::floor((y + radius) / kCell));
  struct Cand {
    uint32_t i;
    float d;
  };
  std::vector<Cand> near;
  for (int cx = x0; cx <= x1; ++cx)
    for (int cy = y0; cy <= y1; ++cy) {
      const auto it = grid_.find(Key(cx, cy));
      if (it == grid_.end())
        continue;
      for (uint32_t i : it->second) {
        const SnapPoint& p = points_[i];
        const float dx = p.x - x, dy = p.y - y, d = std::sqrt(dx * dx + dy * dy);
        if (d <= radius)
          near.push_back({i, d});
      }
    }
  float best = 1e30f;
  bool found = false;
  const float bonusPerStep = 0.15f * radius;
  for (const Cand& c : near) {
    const SnapPoint& p = points_[c.i];
    if (mergeRadius > 0.f) { // set aside a point that a stronger one sits right next to
      bool beaten = false;
      for (const Cand& o : near) {
        const SnapPoint& q = points_[o.i];
        if (q.weight > p.weight && std::hypot(q.x - p.x, q.y - p.y) <= mergeRadius) {
          beaten = true;
          break;
        }
      }
      if (beaten)
        continue;
    }
    const float effective = c.d - std::max(0.f, p.weight - 1.f) * bonusPerStep;
    if (effective < best) {
      best = effective;
      out = {p.x, p.y};
      found = true;
    }
  }
  return found;
}

} // namespace pdfview
