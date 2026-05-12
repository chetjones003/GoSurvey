#pragma once

struct GLFWwindow;

/// Call before \p glfwCreateWindow: borderless, non-resizable chrome for splash-only stage.
/// Also requests a transparent framebuffer so the desktop can show outside the splash card (OS-dependent).
void GlfwApplySplashStageWindowHints();

/// After splash: main CAD shell window chrome (borderless + custom controls on Windows; decorated on
/// other platforms), resizable, maximized, taskbar title.
void GlfwApplyMainStageWindowChrome(GLFWwindow* window);

/// Windows: custom caption strip (drag, min / max / close). No-op elsewhere.
void DrawMainWindowTitleBar(GLFWwindow* window);

/// Centered splash card (~⅓ of the work area) on a dimmed full-screen backdrop; theme colors; logo from
/// \ref ResolveAppLogoPngPath (`icons/bitmap.png` or `bitmap.png`).
void RunStartupSplash(GLFWwindow* window, double durationSec = 2.0);
