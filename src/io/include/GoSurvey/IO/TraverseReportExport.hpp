#pragma once

#include <string>

namespace traverserpt {

/// Writes \p text verbatim to \p pathUtf8. Returns false (and sets \p err) if the file
/// could not be opened for writing.
bool ExportReportTxt(const std::string& pathUtf8, const std::string& text, std::string& err);

/// Writes \p text as a minimal monospace RTF document (Courier New) that opens natively in
/// Word, LibreOffice, and Google Docs — no DOCX-writing dependency (D-2026-10-14-b).
bool ExportReportRtf(const std::string& pathUtf8, const std::string& text, std::string& err);

/// Writes \p text as a paginated, monospace vector PDF, reusing the project's existing
/// PDFium-based PDF writer (ADR-007, see src/io/PdfPlot.cpp) — no new dependency.
bool ExportReportPdf(const std::string& pathUtf8, const std::string& text, std::string& err);

}  // namespace traverserpt
