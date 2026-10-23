#pragma once

#include <string>

namespace startupFailure {

enum class Stage {
  GraphicsContext,
  OpenGlInit,
};

/// REQ-425 §4–§5: appends `%APPDATA%\\GoSurvey\\startup-failure.log`, sends the report once in the
/// background (silently), and returns the report text for the caller's report window.
std::string RecordAndSend(Stage stage, const std::string& detailUtf8 = std::string());

/// RecordAndSend, then the Win32 report window ("GoSurvey could not start"), then exits.
[[noreturn]] void FailAndShow(Stage stage, const std::string& detailUtf8 = std::string());

}  // namespace startupFailure
