#include "PdfDiff.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace pdfdiff {

namespace {

constexpr double kPtPerMm = 72.0 / 25.4;

using Mask = std::vector<uint8_t>;

Mask InkMask(const pdfview::Bitmap& b) {
  Mask m(static_cast<size_t>(b.w) * static_cast<size_t>(b.h), 0);
  const size_t n = std::min(m.size(), b.bgra.size() / 4u);
  for (size_t i = 0; i < n; ++i)
    m[i] = pdfalign::IsInk(&b.bgra[i * 4]) ? 1 : 0;
  return m;
}

/// Every pixel within \p r (a square, both ways) of a set pixel becomes set. Two passes over prefix sums, so the
/// cost does not grow with the radius.
Mask Dilate(const Mask& in, int w, int h, int r) {
  if (r <= 0)
    return in;
  Mask tmp(in.size(), 0), out(in.size(), 0);
  std::vector<int> pre(static_cast<size_t>(std::max(w, h)) + 1);
  for (int y = 0; y < h; ++y) {
    const uint8_t* row = &in[static_cast<size_t>(y) * static_cast<size_t>(w)];
    pre[0] = 0;
    for (int x = 0; x < w; ++x)
      pre[static_cast<size_t>(x) + 1] = pre[static_cast<size_t>(x)] + row[x];
    uint8_t* o = &tmp[static_cast<size_t>(y) * static_cast<size_t>(w)];
    for (int x = 0; x < w; ++x) {
      const int lo = std::max(0, x - r), hi = std::min(w, x + r + 1);
      o[x] = pre[static_cast<size_t>(hi)] - pre[static_cast<size_t>(lo)] > 0 ? 1 : 0;
    }
  }
  for (int x = 0; x < w; ++x) {
    pre[0] = 0;
    for (int y = 0; y < h; ++y)
      pre[static_cast<size_t>(y) + 1] = pre[static_cast<size_t>(y)] + tmp[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)];
    for (int y = 0; y < h; ++y) {
      const int lo = std::max(0, y - r), hi = std::min(h, y + r + 1);
      out[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] = pre[static_cast<size_t>(hi)] - pre[static_cast<size_t>(lo)] > 0 ? 1 : 0;
    }
  }
  return out;
}

} // namespace

const char* KindName(Kind k) { return k == Kind::Added ? "Added" : k == Kind::Removed ? "Removed" : "Changed"; }

Result FindChanges(const pdfview::Bitmap& base, const pdfview::Bitmap& rev, double pxPerPt, double baseHPt, const Settings& s,
                   const std::function<bool()>& cancel, const std::function<void(float)>& progress) {
  Result res;
  const auto stop = [&] { return cancel && cancel(); };
  const auto report = [&](float f) {
    if (progress)
      progress(f);
  };
  if (base.w != rev.w || base.h != rev.h || base.w <= 0 || base.h <= 0 || pxPerPt <= 0.0)
    return res;
  const int w = base.w, h = base.h;
  const int tolPx = std::max(1, static_cast<int>(std::lround(s.toleranceMm * kPtPerMm * pxPerPt)));
  const int mergePx = std::max(1, static_cast<int>(std::lround(s.mergeMm * kPtPerMm * pxPerPt)));

  const Mask bInk = InkMask(base), rInk = InkMask(rev);
  report(0.1f);
  if (stop()) {
    res.cancelled = true;
    return res;
  }
  const Mask bNear = Dilate(bInk, w, h, tolPx), rNear = Dilate(rInk, w, h, tolPx);
  report(0.4f);
  if (stop()) {
    res.cancelled = true;
    return res;
  }
  // 1 = a revision mark with none near it on the base (added), 2 = a base mark with none near it on the revision (removed).
  Mask diff(bInk.size(), 0), any(bInk.size(), 0);
  for (size_t i = 0; i < diff.size(); ++i) {
    if (rInk[i] && !bNear[i])
      diff[i] = 1;
    else if (bInk[i] && !rNear[i])
      diff[i] = 2;
    any[i] = diff[i] != 0 ? 1 : 0;
  }
  const Mask grown = Dilate(any, w, h, mergePx);
  report(0.6f);
  if (stop()) {
    res.cancelled = true;
    return res;
  }

  // Connected regions of the grown differences; each keeps the box and counts of its own (ungrown) differences.
  std::vector<uint8_t> seen(grown.size(), 0);
  std::vector<size_t> stack;
  const double minPt = s.minSizeMm * kPtPerMm;
  for (int y0 = 0; y0 < h; ++y0) {
    if ((y0 & 63) == 0) {
      report(0.6f + 0.4f * static_cast<float>(y0) / static_cast<float>(h));
      if (stop()) {
        res.cancelled = true;
        res.regions.clear();
        return res;
      }
    }
    for (int x0 = 0; x0 < w; ++x0) {
      const size_t start = static_cast<size_t>(y0) * static_cast<size_t>(w) + static_cast<size_t>(x0);
      if (!grown[start] || seen[start])
        continue;
      int minx = w, miny = h, maxx = -1, maxy = -1;
      long added = 0, removed = 0;
      seen[start] = 1;
      stack.push_back(start);
      while (!stack.empty()) {
        const size_t i = stack.back();
        stack.pop_back();
        const int x = static_cast<int>(i % static_cast<size_t>(w)), y = static_cast<int>(i / static_cast<size_t>(w));
        if (diff[i] != 0) {
          minx = std::min(minx, x);
          maxx = std::max(maxx, x);
          miny = std::min(miny, y);
          maxy = std::max(maxy, y);
          (diff[i] == 1 ? added : removed) += 1;
        }
        const int nx[4] = {x - 1, x + 1, x, x};
        const int ny[4] = {y, y, y - 1, y + 1};
        for (int k = 0; k < 4; ++k) {
          if (nx[k] < 0 || ny[k] < 0 || nx[k] >= w || ny[k] >= h)
            continue;
          const size_t j = static_cast<size_t>(ny[k]) * static_cast<size_t>(w) + static_cast<size_t>(nx[k]);
          if (grown[j] && !seen[j]) {
            seen[j] = 1;
            stack.push_back(j);
          }
        }
      }
      if (maxx < 0)
        continue; // grown pixels only (cannot happen for a seed that is itself a difference, but be safe)
      Region r;
      r.kind = added > 0 && removed > 0 ? Kind::Changed : added > 0 ? Kind::Added : Kind::Removed;
      r.x0 = minx / pxPerPt;
      r.x1 = (maxx + 1) / pxPerPt;
      r.y1 = baseHPt - miny / pxPerPt;
      r.y0 = baseHPt - (maxy + 1) / pxPerPt;
      r.addedPx = added;
      r.removedPx = removed;
      if (std::max(r.Width(), r.Height()) < minPt)
        continue; // a speck
      res.regions.push_back(r);
    }
  }
  // Top-to-bottom, left-to-right reading order for the list and Next / Previous.
  std::sort(res.regions.begin(), res.regions.end(), [](const Region& a, const Region& b) {
    if (std::fabs(a.y1 - b.y1) > 1e-6)
      return a.y1 > b.y1;
    return a.x0 < b.x0;
  });
  report(1.f);
  return res;
}

std::vector<pdfview::Annot> RegionsToMarkups(const std::vector<Region>& regions, int revPage, const pdfalign::Transform& baseToRev) {
  std::vector<pdfview::Annot> out;
  for (const Region& r : regions) {
    double lo[2] = {1e30, 1e30}, hi[2] = {-1e30, -1e30};
    for (const pdfalign::Pt c : {pdfalign::Pt{r.x0, r.y0}, pdfalign::Pt{r.x1, r.y0}, pdfalign::Pt{r.x0, r.y1}, pdfalign::Pt{r.x1, r.y1}}) {
      const pdfalign::Pt p = baseToRev.Apply(c);
      lo[0] = std::min(lo[0], p.x);
      lo[1] = std::min(lo[1], p.y);
      hi[0] = std::max(hi[0], p.x);
      hi[1] = std::max(hi[1], p.y);
    }
    pdfview::Annot a;
    a.kind = pdfview::Annot::Kind::Rect;
    a.page = revPage;
    a.x0 = static_cast<float>(lo[0]);
    a.y0 = static_cast<float>(lo[1]);
    a.x1 = static_cast<float>(hi[0]);
    a.y1 = static_cast<float>(hi[1]);
    a.color = r.kind == Kind::Added ? 0x1E9E4B : r.kind == Kind::Removed ? 0xD93A3A : 0xF0A020;
    a.thickness = 1.5f;
    char note[120];
    std::snprintf(note, sizeof(note), "%s, %.1f x %.1f pt", KindName(r.kind), r.Width(), r.Height());
    a.text = note;
    out.push_back(a);
  }
  return out;
}

} // namespace pdfdiff
