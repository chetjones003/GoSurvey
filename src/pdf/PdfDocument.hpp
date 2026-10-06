#pragma once

// REQ-387 / ADR-067 (b) — one open PDF, over PDFium. No window, no OpenGL. Safe to call from a
// worker thread: every PDFium call runs under PdfiumMutex(), because PDFium keeps process-wide state
// (fonts, caches) and is not safe to enter from two threads at once.
// The PDFium library itself must already be initialised (PdfAttach_Init at startup).

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

/// Area-averaged copy no larger than \p maxSide on its longer side (a stand-in made from a sharp render
/// costs a few milliseconds, where rendering one from the page costs as much as the sharp render did).
Bitmap Downscale(const Bitmap& src, int maxSide);

/// A rectangle in a page's viewer coordinates (points from the lower-left of the page as displayed).
struct ObjBox {
  float x0 = 0.f, y0 = 0.f, x1 = 0.f, y1 = 0.f;
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

  static constexpr int kDisplayFlags = 0x01 /*FPDF_ANNOT*/ | 0x02 /*FPDF_LCD_TEXT*/;

private:
  PdfDocument() = default;
  struct Impl;
  std::unique_ptr<Impl> impl_;
  std::filesystem::path path_;
  std::vector<PageSize> sizes_;
};

} // namespace pdfview
