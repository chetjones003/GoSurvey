#pragma once

#include <string>

#include "TraverseLeastSquares.hpp"

struct TraverseData;

/// Adjustment report: files/settings summary + residual listing (REQ-423).
/// No file, type, or identifier in this module names any third-party survey-adjustment
/// product — see D-2026-10-14-b.
namespace traverserpt {

/// Builds the full report text: a "Summary of Files Used and Option Settings" section
/// (imported files, project option settings, instrument standard error settings — REQ-422,
/// REQ-423 §1), then the per-measurement residual listing and kind/chi-square summary
/// already computed into \p result (REQ-418). Plain text, fixed-width columns, no markup.
std::string BuildReportText(const TraverseData& td, const LsaWeights& weights, const LsaResult& result,
                            const std::string& projectName, const std::string& linearUnitAbbrev);

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
