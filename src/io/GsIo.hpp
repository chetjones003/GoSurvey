#pragma once

#include <string>
#include <string_view>
#include <vector>

struct AppCommandState;
struct DrawingSettings;

/// GoSurvey workspace-template (.gst) file I/O: drawing styles, setup and supported linework (JSON in UTF-8).
/// Templates omit survey-point instances and non-linework drawing objects. NOT the drawing format —
/// that is `.dwg` (ADR-044); this reads/writes the narrow startup-template file only. `GsIo` is a historical
/// module name: it serializes the document JSON payload and `.gst` templates, not standalone
/// `.gs` drawings (retired by issue #264).
bool SaveGoSurveyTemplateFile(const AppCommandState& st, const char* pathUtf8, std::vector<std::string>& log);
bool LoadGoSurveyTemplateFile(AppCommandState& st, const char* pathUtf8, std::vector<std::string>& log);
/// Full GoSurvey document JSON tree held by the REQ-175 DWG trailer. User-facing `.gst` files
/// use the same schema with drawing content filtered out.
std::string SerializeGoSurveyJson(const AppCommandState& st);
bool LoadGoSurveyFromJsonUtf8(AppCommandState& st, std::string_view jsonUtf8, std::vector<std::string>& log);
/// REQ-357 / REQ-375: the Drawing Settings object as JSON text (the trailer's `drawingSettings`).
/// The reader returns false when the text is not a JSON object; absent members keep the defaults.
std::string DrawingSettingsToJsonText(const DrawingSettings& ds);
bool DrawingSettingsFromJsonText(const std::string& jsonText, DrawingSettings* out);

namespace alignment { struct AlignmentCheckSet; }
/// REQ-502 design criteria files: check sets as JSON text, the same schema the drawing stores. The reader returns false
/// with `err` when the text is not a JSON array of check sets; entries without a name are skipped.
std::string AlignmentCheckSetsToJsonText(const std::vector<alignment::AlignmentCheckSet>& sets);
bool AlignmentCheckSetsFromJsonText(const std::string& jsonText, std::vector<alignment::AlignmentCheckSet>* out,
                                    std::string* err);
