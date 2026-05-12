#pragma once

struct GLFWwindow;

/// Call before \p glfwCreateWindow: borderless, non-resizable chrome for splash-only stage.
/// Also requests a transparent framebuffer so the desktop can show outside the splash card (OS-dependent).
void GlfwApplySplashStageWindowHints();

/// After splash: title bar on, resizable, maximize, and app title (main CAD shell).
void GlfwApplyMainStageWindowChrome(GLFWwindow* window);

/// Centered splash card (~⅓ of the work area) on a dimmed full-screen backdrop; theme colors; tries
/// `icons/main_logo.png` first (near-white keyed to transparent), then `logo.png`, `white_logo2.png`.
void RunStartupSplash(GLFWwindow* window, double durationSec = 2.0);
