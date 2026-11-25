#pragma once
// Public IO seam for survey CSV point file import/export and linked point-file reading.

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
void SurveyCsvRefreshImportPreview(AppCommandState& st);
bool SurveyCsvImportFile(AppCommandState& st, std::vector<std::string>& log);

struct SurveyFilePoint {
  double easting = 0.0;
  double northing = 0.0;
  float  elevation = 0.f;
};

bool SurveyCsvReadPointsOnly(const char* pathUtf8, SurveyCsvLayout layout, bool skipFirstRow,
                             std::vector<SurveyFilePoint>* out, int* skippedOut, std::string* errorOut);
bool SurveyCsvExportFile(AppCommandState& st, std::vector<std::string>& log);
