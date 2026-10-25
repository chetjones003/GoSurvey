#pragma once

// REQ-387 / ADR-067 (b) — one open PDF, over PDFium. No window, no OpenGL. Safe to call from a
// worker thread: every PDFium call runs under PdfiumMutex(), because PDFium keeps process-wide state
// (fonts, caches) and is not safe to enter from two threads at once.
// The PDFium library itself must already be initialised (PdfAttach_Init at startup).

#include "PdfDimAudit.hpp"
#include "PdfSnap.hpp"
#include "PdfViewerCore.hpp"

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace pdfview {

/// The one lock around PDFium use: the viewer's worker, PDF plotting and the PDF underlay code all
/// take it. Recursive, so a function that holds it may call another that takes it.
std::recursive_mutex& PdfiumMutex();

struct Bitmap {
  int w = 0;
  int h = 0;
  std::vector<uint8_t> bgra; ///< w*h*4, top-down, opaque white background
  bool partial = false;      ///< the render stopped at its time budget; only part of the page is drawn
};

/// Pixel dimensions for a full-page render.
struct PixelSize {
  int width = 0;
  int height = 0;
};

/// A top-left-origin rectangle, in pixels.
struct PixelRect {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};

/// Area-averaged copy no larger than \p maxSide on its longer side (a stand-in made from a sharp render
/// costs a few milliseconds, where rendering one from the page costs as much as the sharp render did).
Bitmap Downscale(const Bitmap& src, int maxSide);

/// A rectangle in a page's viewer coordinates (points from the lower-left of the page as displayed).
struct ObjBox {
  float x0 = 0.f, y0 = 0.f, x1 = 0.f, y1 = 0.f;
};

/// REQ-429: one occurrence of a search text.
struct TextHit {
  int page = 0;
  std::vector<ObjBox> boxes;
};

struct TextSearch {
  std::vector<TextHit> hits;  ///< in page order, then reading order
  bool truncated = false;     ///< more than kMaxHits occurrences exist; the rest were not collected
  bool anyText = false;       ///< at least one page has a text layer (false: a scan, so nothing could match)
  static constexpr size_t kMaxHits = 10000;
};

class PdfDocument {
public:
  struct OpenResult {
    std::unique_ptr<PdfDocument> doc;
    std::string error; ///< non-empty when doc is null: a stated reason (REQ-201)
  };

  /// Streams the file (it is not read whole) and reads every page's size from the page tree.
  static OpenResult Open(const std::filesystem::path& path);

  ~PdfDocument();
  PdfDocument(const PdfDocument&) = delete;
  PdfDocument& operator=(const PdfDocument&) = delete;

  int PageCount() const { return static_cast<int>(sizes_.size()); }
  const std::vector<PageSize>& Sizes() const { return sizes_; }
  const std::filesystem::path& Path() const { return path_; }

  /// Render page `page` to w x h pixels. `cancel` is polled between slices of work; when it returns
  /// true the render stops and false is returned. Returns false on any failure.
  /// \p flags are FPDF_* render flags; the default is the sharp, annotated display render.
  /// \p budgetMs > 0: stop after that long and return what is drawn so far (out.partial = true).
  /// \p sliceMs > 0 with \p between: every sliceMs of drawing the render pauses and calls \p between (which
  /// may render OTHER pages of this document: PDFium keeps one render context per page), then resumes. This is
  /// how a seconds-long sharp page lets the thumbnail strip draw in between instead of waiting behind it.
  bool RenderPage(int page, int w, int h, Bitmap& out, const std::function<bool()>& cancel, int flags = kDisplayFlags,
                  int budgetMs = 0, int sliceMs = 0, const std::function<void()>& between = {});

  /// REQ-455: render only p region of the page drawn at p fullSize pixels, so a page can be
  /// shown at any zoom without a bitmap the size of the whole page. Same cancel and failure rules as RenderPage.
  bool RenderRegion(int page, PixelSize fullSize, PixelRect region, Bitmap& out,
                    const std::function<bool()>& cancel, int flags = kDisplayFlags);

  /// REQ-391 clause 5: the ends and corners of the vector line work on \p page (the corners of its paths,
  /// forms included), in the viewer's page coordinates (points from the page's bottom-left as displayed, so
  /// rotation and an offset page box are already accounted for). \p cancel is polled; the first \p maxPoints
  /// distinct points are kept. Returns false on any failure or when cancelled.
  bool SnapPoints(int page, std::vector<SnapPoint>& out, const std::function<bool()>& cancel,
                  size_t maxPoints = 2000000);

  /// REQ-393: the box of every text object on \p page (a run of text as the drawing program stored it: a word, a
  /// callout line), in the viewer's page coordinates like SnapPoints. Used only to colour a changed word whole; the
  /// comparison itself is of the drawn picture. \p cancel is polled; false on failure or cancel.
  bool TextBoxes(int page, std::vector<ObjBox>& out, const std::function<bool()>& cancel);

  /// REQ-429: where \p query (UTF-16) occurs in the document's text, page by page. Each hit has the page and
  /// the boxes covering the match (one per line fragment), in the viewer's page coordinates like TextBoxes.
  /// An empty query finds nothing. \p cancel is polled between pages and matches; false on failure or cancel.
  /// The PDFium lock is held for one page at a time, so rendering is not starved while a long file is searched.
  bool SearchText(const std::u16string& query, bool matchCase, TextSearch& out, const std::function<bool()>& cancel);

  /// REQ-395: every text run (its string and box) and every straight stroke on \p page, in the viewer's page
  /// coordinates, for the automatic scale audit. \p cancel is polled; false on failure or cancel.
  bool AuditPageData(int page, std::vector<DimText>& texts, std::vector<DimSeg>& segs, const std::function<bool()>& cancel);

  static constexpr int kDisplayFlags = 0x01 /*FPDF_ANNOT*/ | 0x02 /*FPDF_LCD_TEXT*/;

private:
  bool RenderWindow(int page, PixelSize fullSize, PixelRect region, Bitmap& out,
                    const std::function<bool()>& cancel, int flags, int budgetMs, int sliceMs,
                    const std::function<void()>& between);
  PdfDocument() = default;
  struct Impl;
  std::unique_ptr<Impl> impl_;
  std::filesystem::path path_;
  std::vector<PageSize> sizes_;
};

} // namespace pdfview
