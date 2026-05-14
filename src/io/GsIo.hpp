#pragma once

#include <string>
#include <vector>

struct AppCommandState;

/// GoSurvey workspace: drawing geometry, layers, survey points, and Settings-panel values (JSON in UTF-8).
bool SaveGoSurveyFile(const AppCommandState& st, const char* pathUtf8, std::vector<std::string>& log);
bool LoadGoSurveyFile(AppCommandState& st, const char* pathUtf8, std::vector<std::string>& log);
