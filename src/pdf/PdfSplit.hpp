#pragma once

// REQ-389 / ADR-067 (b) — save chosen pages of a PDF as a new PDF. Pure: PDFium only, no window. Safe on a
// worker thread (every PDFium call runs under PdfiumMutex()). PDFium must already be initialised.

#include <atomic>
#include <filesystem>
#include <string>
#include <vector>

namespace pdfview {

struct PageListResult {
  std::vector<int> pages; ///< zero-based, in the order typed, duplicates kept
  std::string error;      ///< non-empty when the text was refused: names the problem (REQ-201)
};

/// Parses `1-5, 9, 12-20` against a file of \p pageCount pages (clause 1 of REQ-389).
PageListResult ParsePageList(const std::string& text, int pageCount);

/// Writes the pages \p pages (zero-based) of \p source to \p dest as a new PDF. The source is read and closed
/// at once, never modified; \p dest equal to \p source is refused. The file is written to a temporary beside
/// \p dest and renamed into place, so a failure leaves no partial \p dest. \p progress (optional) is set to the
/// number of pages copied so far. Returns "" on success, otherwise a stated reason; nothing is written on error.
std::string SplitPdf(const std::filesystem::path& source, const std::vector<int>& pages,
                     const std::filesystem::path& dest, std::atomic<int>* progress = nullptr);

} // namespace pdfview
