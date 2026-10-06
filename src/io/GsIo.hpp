#pragma once

#include <string>
#include <string_view>
#include <vector>

struct AppCommandState;
struct DrawingSettings;

/// GoSurvey workspace-template (.gst) file I/O: drawing geometry, layers, survey points, and
/// Settings-panel values (JSON in UTF-8). NOT the drawing document format — that is `.dwg`
/// (ADR-044); this reads/writes the narrow startup-template file only.
bool SaveGoSurveyTemplateFile(const AppCommandState& st, const char* pathUtf8, std::vector<std::string>& log);
bool LoadGoSurveyTemplateFile(AppCommandState& st, const char* pathUtf8, std::vector<std::string>& log);
/// Same JSON tree a `.gst` template file holds (REQ-175 DWG trailer).
std::string SerializeGoSurveyJson(const AppCommandState& st);
bool LoadGoSurveyFromJsonUtf8(AppCommandState& st, std::string_view jsonUtf8, std::vector<std::string>& log);
/// REQ-357 / REQ-375: the Drawing Settings object as JSON text (the trailer's `drawingSettings`).
/// The reader returns false when the text is not a JSON object; absent members keep the defaults.
std::string DrawingSettingsToJsonText(const DrawingSettings& ds);
bool DrawingSettingsFromJsonText(const std::string& jsonText, DrawingSettings* out);
