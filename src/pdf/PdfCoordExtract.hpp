#pragma once

// REQ-399 - extract coordinates from a selected region of a PDF page into survey points or
// circles. Pure matching: given the page's text runs (PdfDocument::AuditPageData, the same
// PDFium text route REQ-395 uses), classify the text inside a picked rectangle as a single
// monument block, a header table, or a bare northing/easting pair, and produce candidate
// points for the caller to show for Accept/Deny. No PDFium here, no window.

#include "PdfDimAudit.hpp"

#include <optional>
#include <cstdint>
#include <string>
#include <vector>

namespace pdfview {

/// One coordinate parsed out of a region: a survey point (description + elevation + point
/// number present or assignable) or a bare northing/easting pair (becomes a circle, REQ-399
/// clause 6).
struct CoordCandidate {
  std::optional<int> pointNumber; ///< from the source; unset means "auto-assign lowest free"
  double northing = 0.0;
  double easting = 0.0;
  std::optional<double> elevation;
  std::string description; ///< empty when the source gave none

  bool HasDescription() const { return !description.empty(); }
  bool HasElevation() const { return elevation.has_value(); }
  enum class CreateAs : std::uint8_t { Auto, Circle, SurveyPoint };
  CreateAs createAs = CreateAs::Auto;

  /// REQ-399 clause 6: a survey point needs elevation AND description; otherwise it is a circle.
  /// The review dialog may override that default per row.
  bool IsSurveyPoint() const {
    if (createAs == CreateAs::SurveyPoint) return true;
    if (createAs == CreateAs::Circle) return false;
    return HasDescription() && HasElevation();
  }

  /// The source text this candidate was read from, for the side-by-side review (REQ-399 clause 5).
  std::string sourceText;
};

/// A table row that was read but could not become a candidate (REQ-399 amended 2026-10-29).
struct SkippedTableRow {
  std::string sourceText;
  std::string reason;
};

enum class ExtractStatus {
  Ok,         ///< one or more candidates parsed (possibly alongside unparsed leftover text)
  NoText,     ///< the region had no selectable text at all (REQ-399 clause 3)
  Unparsed,   ///< the region had text, but none of it matched a recognised shape (clause 4)
};

struct ExtractResult {
  ExtractStatus status = ExtractStatus::NoText;
  std::vector<CoordCandidate> candidates;
  std::vector<SkippedTableRow> skippedTableRows;
  /// User-facing message for NoText / Unparsed (REQ-399 clause 3's required wording for NoText).
  std::string message;
};

/// Parses the text runs of \p texts whose box lies inside (\p rx0,\p ry0)-(\p rx1,\p ry1) (page
/// points, same convention as PdfDocument::AuditPageData / DimText) into coordinate candidates.
ExtractResult ExtractCoordinates(const std::vector<DimText>& texts, float rx0, float ry0, float rx1, float ry1);

} // namespace pdfview
