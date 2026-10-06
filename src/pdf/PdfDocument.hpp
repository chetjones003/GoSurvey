#pragma once

// REQ-387 / ADR-067 (b) — one open PDF, over PDFium. No window, no OpenGL. Safe to call from a
// worker thread: every PDFium call runs under PdfiumMutex(), because PDFium keeps process-wide state
// (fonts, caches) and is not safe to enter from two threads at once.
// The PDFium library itself must already be initialised (PdfAttach_Init at startup).

#include "PdfViewerCore.hpp"

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace pdfview {

/// The one lock around PDFium use by the viewer and by PDF plotting (PdfPlot takes it too).
std::mutex& PdfiumMutex();

struct Bitmap {
  int w = 0;
  int h = 0;
  std::vector<uint8_t> bgra; ///< w*h*4, top-down, opaque white background
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
  bool RenderPage(int page, int w, int h, Bitmap& out, const std::function<bool()>& cancel);

private:
  PdfDocument() = default;
  struct Impl;
  std::unique_ptr<Impl> impl_;
  std::filesystem::path path_;
  std::vector<PageSize> sizes_;
};

} // namespace pdfview
