#pragma once

#include <string>
#include <string_view>
#include <vector>

struct AppCommandState;

/// GoSurvey workspace: drawing geometry, layers, survey points, and Settings-panel values (JSON in UTF-8).
bool SaveGoSurveyFile(const AppCommandState& st, const char* pathUtf8, std::vector<std::string>& log);
bool LoadGoSurveyFile(AppCommandState& st, const char* pathUtf8, std::vector<std::string>& log);
/// Same JSON tree a `.gs` file holds (REQ-175 DWG trailer).
std::string SerializeGoSurveyJson(const AppCommandState& st);
bool LoadGoSurveyFromJsonUtf8(AppCommandState& st, std::string_view jsonUtf8, std::vector<std::string>& log);
