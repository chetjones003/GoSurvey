#pragma once

#include <string>
#include <vector>

struct AppCommandState;

enum class SurveyCsvLayout {
  PENZD_PN, ///< P,N,E,Z,D
  PENZD_PE, ///< P,E,N,Z,D
  NEZ,
  ENZ,
};

SurveyCsvLayout SurveyCsvLayoutFromUiIndex(int idx);

/// Uses path / layout / skip-first-row from \p st; fills preview strings and clears dirty flag.
void SurveyCsvRefreshImportPreview(AppCommandState& st);

bool SurveyCsvImportFile(AppCommandState& st, std::vector<std::string>& log);

/// One point read straight out of a file, in the file's own WORLD coordinates and full double
/// precision — no document origin subtracted, because nothing here is stored in the drawing.
struct SurveyFilePoint {
  double easting = 0.0;
  double northing = 0.0;
  float  elevation = 0.f;
};

/// Reads a point file into plain points **without touching the drawing** — REQ-086's linked surface
/// data source, where the file feeds the triangulation and its points never become survey points.
///
/// Shares \c ParseDataRow with \ref SurveyCsvImportFile rather than re-implementing the format, so a
/// file a surface links to and the same file imported through REQ-083 cannot disagree about what it
/// contains. Unparseable rows are skipped and counted in \p skippedOut, matching the importer; a file
/// that cannot be opened at all returns false with the reason in \p errorOut.
bool SurveyCsvReadPointsOnly(const char* pathUtf8, SurveyCsvLayout layout, bool skipFirstRow,
                             std::vector<SurveyFilePoint>* out, int* skippedOut, std::string* errorOut);

bool SurveyCsvExportFile(AppCommandState& st, std::vector<std::string>& log);
