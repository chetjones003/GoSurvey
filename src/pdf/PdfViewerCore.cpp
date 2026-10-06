#include "PdfViewerCore.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace pdfview {

Layout Layout::Build(const std::vector<PageSize>& sizes, float gapPt) {
  Layout l;
  l.size = sizes;
  l.top.reserve(sizes.size());
  float y = 0.f;
  for (const PageSize& s : sizes) {
    l.top.push_back(y);
    y += s.hPt + gapPt;
    l.maxWidth = std::max(l.maxWidth, s.wPt);
  }
  l.totalHeight = sizes.empty() ? 0.f : y - gapPt;
  return l;
}

int Layout::PageAt(float y) const {
  if (top.empty())
    return -1;
  auto it = std::upper_bound(top.begin(), top.end(), y);
  if (it == top.begin())
    return 0;
  return static_cast<int>(it - top.begin()) - 1;
}

VisibleRange VisiblePages(const Layout& layout, float scrollPt, float viewPt) {
  VisibleRange r;
  const int n = layout.PageCount();
  if (n == 0 || viewPt <= 0.f)
    return r;
  r.first = layout.PageAt(std::max(0.f, scrollPt));
  int last = layout.PageAt(scrollPt + viewPt);
  // PageAt returns the page whose top is at or above y; that page is visible only if its top is
  // above the bottom edge of the view (it always is, by construction).
  r.last = std::clamp(last, r.first, n - 1);
  // A page whose bottom edge is above the view top is not visible (we landed in a gap).
  if (r.first < n && layout.top[static_cast<size_t>(r.first)] + layout.size[static_cast<size_t>(r.first)].hPt <
                         scrollPt &&
      r.first < r.last)
    ++r.first;
  return r;
}

int ReadAheadThatFits(size_t capBytes, size_t pageBytes, int visibleCount, int maxAhead) {
  const size_t usable = capBytes - capBytes / 6;
  const int fit = static_cast<int>(usable / std::max<size_t>(1, pageBytes));
  return std::clamp((fit - visibleCount) * 2 / 3, 0, maxAhead);
}

int ScaleKeyFor(float pxPerPt) {
  // Steps of 1/8 of a pixel-per-point keep a smooth wheel zoom from asking for a render every frame
  // while staying sharp to within ~6 %.
  const int k = static_cast<int>(std::lround(pxPerPt * 16.f / 2.f)) * 2;
  return std::max(2, k);
}

float PxPerPtFor(int scaleKey) { return static_cast<float>(scaleKey) / 16.f; }

std::vector<RenderRequest> PlanRequests(const VisibleRange& vis, int pageCount, int scrollDir, int readAhead,
                                        int scaleKey, const std::function<Have(int page)>& have) {
  std::vector<RenderRequest> out;
  if (vis.Empty() || pageCount <= 0)
    return out;
  const int dir = scrollDir < 0 ? -1 : 1;

  auto addPage = [&](int p, bool standInFirst) {
    if (p < 0 || p >= pageCount)
      return;
    const Have h = have(p);
    if (standInFirst && !h.standIn && !h.displayAtKey)
      out.push_back({p, Level::StandIn, 0});
    if (!h.displayAtKey)
      out.push_back({p, Level::Display, scaleKey});
  };

  // 1. Visible pages: every stand-in first (cheap, so the screen is never blank), then sharp ones,
  //    nearest the middle of the view first.
  std::vector<int> visible;
  for (int p = vis.first; p <= vis.last; ++p)
    visible.push_back(p);
  const int mid = vis.Center();
  std::stable_sort(visible.begin(), visible.end(),
                   [&](int a, int b) { return std::abs(a - mid) < std::abs(b - mid); });
  for (int p : visible) {
    const Have h = have(p);
    if (!h.standIn && !h.displayAtKey)
      out.push_back({p, Level::StandIn, 0});
  }
  for (int p : visible) {
    if (!have(p).displayAtKey)
      out.push_back({p, Level::Display, scaleKey});
  }

  // 2. Ahead of the scroll direction, then behind, nearest first.
  for (int i = 1; i <= readAhead; ++i)
    addPage(dir > 0 ? vis.last + i : vis.first - i, true);
  for (int i = 1; i <= readAhead / 2; ++i)
    addPage(dir > 0 ? vis.first - i : vis.last + i, true);
  return out;
}

std::vector<PageCache::Entry> PageCache::Put(const Entry& e, int centerPage, const VisibleRange& keep) {
  std::vector<Entry> removed;
  for (size_t i = 0; i < entries_.size(); ++i) {
    if (entries_[i].page == e.page && entries_[i].level == e.level) {
      bytes_ -= entries_[i].bytes;
      removed.push_back(entries_[i]);
      entries_.erase(entries_.begin() + static_cast<long>(i));
      break;
    }
  }
  entries_.push_back(e);
  bytes_ += e.bytes;

  while (bytes_ > cap_) {
    int worst = -1;
    int worstDist = -1;
    for (size_t i = 0; i < entries_.size(); ++i) {
      const Entry& c = entries_[i];
      if (c.page == e.page && c.level == e.level)
        continue;
      if (!keep.Empty() && c.page >= keep.first && c.page <= keep.last)
        continue;
      // Sharp images go before stand-ins at equal distance: a stand-in is the cheap way to avoid a blank page.
      int d = std::abs(c.page - centerPage) * 2 + (c.level == Level::Display ? 1 : 0);
      if (d > worstDist) {
        worstDist = d;
        worst = static_cast<int>(i);
      }
    }
    if (worst < 0)
      break; // nothing evictable: only the new entry and the visible pages remain
    bytes_ -= entries_[static_cast<size_t>(worst)].bytes;
    removed.push_back(entries_[static_cast<size_t>(worst)]);
    entries_.erase(entries_.begin() + worst);
  }
  return removed;
}

const PageCache::Entry* PageCache::Find(int page, Level level) const {
  for (const Entry& e : entries_)
    if (e.page == page && e.level == level)
      return &e;
  return nullptr;
}

const PageCache::Entry* PageCache::Best(int page) const {
  if (const Entry* d = Find(page, Level::Display))
    return d;
  return Find(page, Level::StandIn);
}

Have PageCache::HaveFor(int page, int scaleKey) const {
  Have h;
  h.standIn = Find(page, Level::StandIn) != nullptr;
  const Entry* d = Find(page, Level::Display);
  h.displayAtKey = d != nullptr && d->scaleKey == scaleKey;
  return h;
}

std::vector<PageCache::Entry> PageCache::Clear() {
  std::vector<Entry> all = std::move(entries_);
  entries_.clear();
  bytes_ = 0;
  return all;
}

std::string MakeSyntheticPdf(int pages, int linesPerPage, int variedEvery) {
  std::string out = "%PDF-1.4\n";
  std::vector<size_t> offs;
  auto beginObj = [&](int num) {
    if (static_cast<int>(offs.size()) < num)
      offs.resize(static_cast<size_t>(num), 0);
    offs[static_cast<size_t>(num - 1)] = out.size();
    out += std::to_string(num) + " 0 obj\n";
  };
  // 1 catalog, 2 page tree, 3 font, then (page, content) pairs from 4.
  beginObj(1);
  out += "<< /Type /Catalog /Pages 2 0 R >>\nendobj\n";
  beginObj(2);
  out += "<< /Type /Pages /Count " + std::to_string(pages) + " /Kids [";
  for (int i = 0; i < pages; ++i)
    out += std::to_string(4 + i * 2) + " 0 R ";
  out += "] >>\nendobj\n";
  beginObj(3);
  out += "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>\nendobj\n";
  for (int i = 0; i < pages; ++i) {
    const bool wide = variedEvery > 0 && (i / variedEvery) % 2 == 1;
    const int w = wide ? 792 : 612, h = wide ? 612 : 792;
    std::string content = "0.2 w\n";
    char buf[96];
    for (int k = 0; k < linesPerPage; ++k) {
      const int x1 = 20 + (k * 37 + i * 11) % (w - 40), y1 = 20 + (k * 53) % (h - 40);
      const int x2 = 20 + (k * 71 + 13) % (w - 40), y2 = 20 + (k * 29 + i) % (h - 40);
      std::snprintf(buf, sizeof(buf), "%d %d m %d %d l S\n", x1, y1, x2, y2);
      content += buf;
    }
    content += "BT /F1 18 Tf 40 " + std::to_string(h - 40) + " Td (Page " + std::to_string(i + 1) + ") Tj ET\n";
    beginObj(4 + i * 2);
    out += "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " + std::to_string(w) + " " + std::to_string(h) +
           "] /Resources << /Font << /F1 3 0 R >> >> /Contents " + std::to_string(5 + i * 2) + " 0 R >>\nendobj\n";
    beginObj(5 + i * 2);
    out += "<< /Length " + std::to_string(content.size()) + " >>\nstream\n" + content + "endstream\nendobj\n";
  }
  const size_t xref = out.size();
  const int total = 3 + pages * 2;
  out += "xref\n0 " + std::to_string(total + 1) + "\n0000000000 65535 f \n";
  for (int n = 0; n < total; ++n) {
    char line[32];
    std::snprintf(line, sizeof(line), "%010zu 00000 n \n", offs[static_cast<size_t>(n)]);
    out += line;
  }
  out += "trailer\n<< /Size " + std::to_string(total + 1) + " /Root 1 0 R >>\nstartxref\n" + std::to_string(xref) +
         "\n%%EOF\n";
  return out;
}

} // namespace pdfview
