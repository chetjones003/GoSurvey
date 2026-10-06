#pragma once

#include <string>

namespace startupFailure {

enum class Stage {
  GlfwInit,
  GlfwCreateWindow,
  OpenGlInit,
};

void NoteGlfwError(int code, const char* description);

/// Writes `%APPDATA%\\GoSurvey\\startup-failure.log`, shows a modal dialog (Windows), then exits.
[[noreturn]] void FailAndShow(Stage stage, const std::string& detailUtf8 = std::string());

}  // namespace startupFailure
