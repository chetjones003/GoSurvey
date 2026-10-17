#pragma once

#include <imgui.h>

struct GLFWwindow;

namespace update { struct UpdateState; }

/// Call before \p glfwCreateWindow: borderless, non-resizable chrome for splash-only stage.
/// Also requests a transparent framebuffer so the desktop can show outside the splash card (OS-dependent).
void GlfwApplySplashStageWindowHints();

/// After splash: main CAD shell window chrome (borderless + custom controls on Windows; decorated on
/// other platforms), resizable, maximized, taskbar title.
void GlfwApplyMainStageWindowChrome(GLFWwindow* window);

/// Windows: custom caption strip (drag, min / max / close), the GS logo/title (click opens the
/// File/Edit/View/Help menu) and the signed-in account email (click opens Account Details/Sign
/// Out). No-op elsewhere. `accountEmail` is the email to show, or nullptr/empty when signed out.
/// On return, `*outMenuClicked`/`*outAccountClicked` report whether that area was clicked this
/// frame, and `*outMenuAnchor`/`*outAccountAnchor` give the screen position (bottom-left) to open
/// each dropdown under.
void DrawMainWindowTitleBar(GLFWwindow* window, const char* accountEmail,
                            bool* outMenuClicked, ImVec2* outMenuAnchor,
                            bool* outAccountClicked, ImVec2* outAccountAnchor);

/// Centered splash card (~⅓ of the work area) on a dimmed full-screen backdrop; theme colors; logo from
/// \ref ResolveAppLogoPngPath (`icons/bitmap.png` or `bitmap.png`).
///
/// When \p updateState is non-null, REQ-077's startup check is polled each frame and the splash
/// stays up until the minimum \p durationSec elapses and the check is no longer in flight.
void RunStartupSplash(GLFWwindow* window, double durationSec = 2.0,
                      update::UpdateState* updateState = nullptr);
