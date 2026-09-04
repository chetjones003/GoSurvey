#pragma once

#include <string>
#include <string_view>
#include <vector>

struct AppCommandState;

/// GoSurvey workspace-template (.gst) file I/O: drawing geometry, layers, survey points, and
/// Settings-panel values (JSON in UTF-8). NOT the drawing document format — that is `.dwg`
/// (ADR-044); this reads/writes the narrow startup-template file only.
bool SaveGoSurveyTemplateFile(const AppCommandState& st, const char* pathUtf8, std::vector<std::string>& log);
bool LoadGoSurveyTemplateFile(AppCommandState& st, const char* pathUtf8, std::vector<std::string>& log);
/// Same JSON tree a `.gst` template file holds (REQ-175 DWG trailer).
std::string SerializeGoSurveyJson(const AppCommandState& st);
bool LoadGoSurveyFromJsonUtf8(AppCommandState& st, std::string_view jsonUtf8, std::vector<std::string>& log);
