#pragma once

// REQ-395 — the automatic scale audit. Pure matching and statistics: no PDFium, no window.
//
// A drawing already prints hundreds of dimensions. Given the page's text runs and its straight line work, this finds
// each dimension text's dimension line (a straight horizontal or vertical run with an extension line at each end),
// measures the span between the extension lines, and compares it with what the text states (REQ-394's verdicts and
// best fit). The result is a set of suggestions; nothing here changes a scale or a mark.

#include "PdfScaleCheck.hpp"

#include <functional>
#include <string>
#include <vector>

namespace pdfview {

/// One run of text and its box, in page points (as PdfDocument::AuditPageData returns them).
struct DimText {
  std::string text; ///< UTF-8
  float x0 = 0.f, y0 = 0.f, x1 = 0.f, y1 = 0.f;
};

/// One straight stroke of the page's line work, in page points.
struct DimSeg {
  float x0 = 0.f, y0 = 0.f, x1 = 0.f, y1 = 0.f;
};

/// One dimension text paired with the dimension line it labels.
struct DimMatch {
  std::string text;        ///< as the drawing prints it
  double stated = 0.0;     ///< its value, in \p unit
  Unit unit = Unit::Foot;
  float x0 = 0.f, y0 = 0.f, x1 = 0.f, y1 = 0.f; ///< the measured span: from one extension line to the other
  float tx = 0.f, ty = 0.f;                      ///< the text's centre
  double lengthPt = 0.0;
  double metresPerPt = 0.0; ///< the scale this one dimension implies (stated / measured)
};

struct DimMatchSet {
  std::vector<DimMatch> matches;
  int textsRead = 0;      ///< runs that read as a length with a unit
  int unmatchedText = 0;  ///< of those, the ones with no dimension line (or whose line a closer text took)
  int unmatchedLines = 0; ///< dimension lines (extension line at each end) that no text claimed
};

struct DimMatchParams {
  float minLinePt = 10.f;     ///< a dimension line shorter than this is not considered
  float axisTolPt = 0.5f;     ///< a stroke this close to horizontal or vertical counts as one
  float endTolPt = 6.f;       ///< an extension line this close (along the line) to the line's end belongs to it
  float coverTolPt = 2.f;     ///< and must reach the line's own position within this
  float minExtensionPt = 2.f; ///< shorter strokes at an end (arrowheads) are not extension lines
  float maxOffsetMin = 18.f;  ///< the text sits within max(this, 3 x its height) of the line
};

/// Pairs texts with dimension lines. \p cancel is polled; false when cancelled (nothing in \p out is a result).
bool MatchDimensions(const std::vector<DimText>& texts, const std::vector<DimSeg>& segs, const DimMatchParams& params,
                     const std::function<bool()>& cancel, DimMatchSet& out);

struct DimAudit {
  DimMatchSet set;
  std::vector<CheckResult> results; ///< per match, against the page's current scale
  int good = 0, check = 0, blunder = 0;
  bool consensusValid = false;      ///< at least kMinForConsensus dimensions matched
  double consensusMetresPerPt = 0.0;
  std::vector<int> offenders;       ///< indices into set.matches, worst first (every Check and Blunder)
  std::string message;              ///< "No dimension text found", "Only 3 dimensions matched (5 needed ...)", or ""
};

constexpr int kMinForConsensus = 5;

/// Verdicts against \p scale, the robust consensus scale (the best fit without outliers, so a few wrong matches
/// do not move it) and the worst offenders.
DimAudit AuditDimensions(DimMatchSet set, const PageScale& scale, const CheckLimits& lim);

/// The scale that metres-per-point describes, in \p previous's real unit per page inch, noted "audit consensus,
/// n dimensions"; \p previous is remembered as the original.
PageScale ScaleFromConsensus(double metresPerPt, int n, const PageScale& previous);

} // namespace pdfview
