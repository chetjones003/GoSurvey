#pragma once

#ifdef GOSURVEY_DEVELOPER_SHELL

#include "CadCommands.hpp"

#include <string>
#include <string_view>
#include <vector>

struct ImGuiTestEngine;
struct GLFWwindow;
/// Forward-declared rather than included: `ViewportRenderer.hpp` pulls the whole render stack, and
/// the capture hook below only ever takes a reference to one.
class ViewportRenderer;

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

void DevShell_Draw(AppCommandState& cmd, std::vector<std::string>& commandLog);

[[nodiscard]] std::vector<std::string>* DevShell_CommandLog();
[[nodiscard]] bool DevShell_CommandLogContains(std::string_view needle);
[[nodiscard]] std::string DevShell_ActivityLogText();
[[nodiscard]] bool DevShell_ActivityLogContains(std::string_view needle);
/// Directory of `devshell-activity.log`, `devshell-command.log`, and `devshell-testengine.log`
/// (next to the executable). Written whether or not the Shell window is visible.
[[nodiscard]] std::string DevShell_LogDirectoryUtf8();

/// Returns true if argv contained `--devshell-run <name>` (Debug only).
bool DevShell_ParseRunFlag(std::string* outTestName);

/// Queue `testName` and, once it finishes, set glfw close + process exit code (0 pass, 1 fail).
void DevShell_BeginCliRun(ImGuiTestEngine* engine, GLFWwindow* window, const char* testName);
bool DevShell_CliRunFinished(int* outExitCode);

/// Read the GL front buffer of the current GLFW window into a 24-bit BMP (Debug driver evidence).
[[nodiscard]] bool DevShell_SaveWindowScreenshot(const char* pathUtf8);

/// Queue a screenshot to be written on the next presented frame (from the main thread, so it is
/// safe to call from a Test Engine coroutine). Yield at least 2 frames after calling.
///
/// **Captures the WINDOW, and therefore depends on the window being presented.** On a window the
/// compositor is not showing — the normal case for an automated run — the `GL_FRONT` read this
/// performs returns pure black. When the evidence wanted is the 3D VIEWPORT, prefer
/// \ref DevShell_RequestViewportCapture, which does not have that dependency.
void DevShell_RequestScreenshot(const char* pathUtf8);

/// Queue a capture of the 3D VIEWPORT's own framebuffer, written after the next `RenderScene`
/// (REQ-161, TASK-248). Yield at least 2 frames after calling.
///
/// Unlike \ref DevShell_RequestScreenshot this reads the renderer's `fbo_` rather than the window's
/// front buffer, so it captures what was actually drawn whether or not the window is composited —
/// which is what makes it usable as evidence from a headless-ish automated run. `maxDim` bounds the
/// longer side; the image is never upscaled.
void DevShell_RequestViewportCapture(const char* pathUtf8, int maxDim);

/// Serviced by the main loop immediately after `RenderScene`, while the renderer's framebuffer still
/// holds this frame. No-op unless a capture is pending.
void DevShell_ServiceViewportCapture(const ViewportRenderer& renderer);

/// Resize the GLFW window (Debug driver / responsive-layout evidence). Yield a few frames after.
void DevShell_SetWindowSize(int w, int h);

/// Record / read the 3D viewport's screen rectangle (REQ-161). Written once a frame by `CadUi`
/// through `DevShell_OnViewportRect`; read by a Test Engine test that needs to aim the cursor at a
/// world point. Returns false until the viewport has drawn at least one frame.
void DevShell_SetViewportRect(float originX, float originY, float sizeX, float sizeY);
[[nodiscard]] bool DevShell_ViewportRect(float* outOriginX, float* outOriginY, float* outSizeX,
                                        float* outSizeY);

#endif
