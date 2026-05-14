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

bool SurveyCsvExportFile(AppCommandState& st, std::vector<std::string>& log);
