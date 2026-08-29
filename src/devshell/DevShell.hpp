#pragma once

#ifdef GOSURVEY_DEVELOPER_SHELL

#include "CadCommands.hpp"

#include <string>
#include <string_view>

struct ImGuiTestEngine;
struct GLFWwindow;

void DevShell_Log(std::string_view channel, std::string_view message);
void DevShell_Logf(const char* channel, const char* fmt, ...);

void DevShell_Create(ImGuiTestEngine** outEngine);
void DevShell_RegisterUiTests(ImGuiTestEngine* engine, AppCommandState* cmd);
void DevShell_RegisterTests(ImGuiTestEngine* engine, AppCommandState* cmd);
void DevShell_PostSwap(ImGuiTestEngine* engine);
/// Stop the Test Engine coroutine while the ImGui context still exists.
void DevShell_Stop(ImGuiTestEngine* engine);
/// Destroy the Test Engine. Must run *after* ImGui::DestroyContext() (imgui_te_engine.h).
void DevShell_DestroyContext(ImGuiTestEngine* engine);

void DevShell_Draw(AppCommandState& cmd, const std::vector<std::string>& commandLog);

/// Returns true if argv contained `--devshell-run <name>` (Debug only).
bool DevShell_ParseRunFlag(std::string* outTestName);

/// Queue `testName` and, once it finishes, set glfw close + process exit code (0 pass, 1 fail).
void DevShell_BeginCliRun(ImGuiTestEngine* engine, GLFWwindow* window, const char* testName);
bool DevShell_CliRunFinished(int* outExitCode);

#endif
