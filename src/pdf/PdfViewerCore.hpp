#pragma once

// REQ-387 / ADR-067 — the pure half of the PDF viewer: where each page sits, which pages are on
// screen, what to render next, and what to keep. No PDFium, no ImGui, no OpenGL, so every rule that
// keeps a 500-page file smooth is unit-testable without a window.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace pdfview {

struct PageSize {
  float wPt = 612.f; ///< PDF points (1/72 in)
  float hPt = 792.f;
};

/// Pages stacked top to bottom, centred, with a gap, in PDF points at zoom 1.
struct Layout {
  std::vector<float> top;    ///< y of each page's top edge
  std::vector<PageSize> size;
  float totalHeight = 0.f;
  float maxWidth = 0.f;

  static Layout Build(const std::vector<PageSize>& sizes, float gapPt);
  int PageCount() const { return static_cast<int>(size.size()); }
  /// The page containing (or nearest above) y; -1 when empty.
  int PageAt(float y) const;
};

struct VisibleRange {
  int first = 0;
  int last = -1; ///< inclusive; last < first means none
  bool Empty() const { return last < first; }
  int Center() const { return (first + last) / 2; }
};

/// Pages whose rectangle meets [scrollPt, scrollPt + viewPt).
VisibleRange VisiblePages(const Layout& layout, float scrollPt, float viewPt);

/// Render level: a small stand-in is cheap and shown at once; the display level is sharp.
enum class Level : int { StandIn = 0, Display = 1 };

struct RenderRequest {
  int page = 0;
  Level level = Level::Display;
  int scaleKey = 0; ///< display scale in 1/16 px per pt (ignored for StandIn)
  bool operator==(const RenderRequest& o) const {
    return page == o.page && level == o.level && scaleKey == o.scaleKey;
  }
};

/// What the cache already holds for a page, so the planner asks only for what is missing.
struct Have {
  bool standIn = false;
  bool displayAtKey = false; ///< a Display entry at exactly the wanted scaleKey
};

/// REQ-387 clause 4: the order pages are rendered in. Visible pages come first (stand-in before
/// sharp), then the pages ahead of the scroll direction, then those behind, nearest first. Pages
/// already satisfied are left out, so an empty result means "nothing to do".
/// scrollDir: +1 scrolling down, -1 up, 0 unknown (treated as down).
std::vector<RenderRequest> PlanRequests(const VisibleRange& vis, int pageCount, int scrollDir, int readAhead,
                                        int scaleKey, const std::function<Have(int page)>& have);

/// How many pages ahead the planner may ask for so that visible + ahead + behind (ahead/2) sharp pages
/// still fit in the cache; asking for more makes every upload evict a page that is asked for again.
/// A third of the cap is left for the stand-ins (thumbnails and placeholders).
int ReadAheadThatFits(size_t capBytes, size_t pageBytes, int visibleCount, int maxAhead);

/// Quantise a display scale (px per pt) so a smooth zoom does not request a render per frame.
int ScaleKeyFor(float pxPerPt);
float PxPerPtFor(int scaleKey);

/// A bounded store of page images, described by size only: the pixels live elsewhere (a GPU texture
/// in the window, a plain number in a test), and the owner learns what to free from Put's result.
class PageCache {
public:
  struct Entry {
    int page = 0;
    Level level = Level::Display;
    int scaleKey = 0;
    size_t bytes = 0;
    uint32_t handle = 0; ///< owner's token (GL texture id)
  };

  explicit PageCache(size_t capBytes) : cap_(capBytes) {}

  /// Insert or replace (page, level). Then evict, farthest from centerPage first, until the total
  /// is within the cap; the entry just inserted and entries for pages in `keep` are never evicted.
  /// Returns every entry removed (replaced or evicted) so the owner can free them.
  std::vector<Entry> Put(const Entry& e, int centerPage, const VisibleRange& keep);

  const Entry* Find(int page, Level level) const;
  /// The best image to show now: the Display entry if any (even at a stale scale), else the stand-in.
  const Entry* Best(int page) const;
  Have HaveFor(int page, int scaleKey) const;

  size_t Bytes() const { return bytes_; }
  size_t Cap() const { return cap_; }
  size_t Count() const { return entries_.size(); }
  std::vector<Entry> Clear();

private:
  size_t cap_;
  size_t bytes_ = 0;
  std::vector<Entry> entries_;
};

/// A synthetic PDF with `pages` pages of vector linework and text (one line of text per page naming
/// it), used by the unit tests and `BENCH PDFVIEW`. Pages alternate portrait/landscape every
/// `variedEvery` pages when > 0. Returns the file bytes.
std::string MakeSyntheticPdf(int pages, int linesPerPage, int variedEvery);

} // namespace pdfview
