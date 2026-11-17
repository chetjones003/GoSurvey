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

}  // namespace traverserpt
