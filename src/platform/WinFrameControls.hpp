#pragma once

struct GLFWwindow;

#if defined(_WIN32)
void GlfwPlatformInstallBorderlessResize(GLFWwindow* window);
void GlfwPlatformBeginCaptionDrag(GLFWwindow* window);
/// Called each frame with the title bar row height and button-strip width (in ImGui/screen pixels).
/// The WndProc uses these to return HTCAPTION for the drag area and HTCLIENT for the button strip.
void GlfwPlatformSetTitleBarMetrics(float rowHeightPx, float btnStripWidthPx);
/// REQ-093 splash: clip the borderless window to rounded corners (desktop shows in the cutouts).
void GlfwPlatformApplySplashRoundedRegion(GLFWwindow* window, float cornerRadiusPx);
/// Remove a prior \ref GlfwPlatformApplySplashRoundedRegion before the main shell resizes.
void GlfwPlatformClearWindowRegion(GLFWwindow* window);
#else
inline void GlfwPlatformInstallBorderlessResize(GLFWwindow*) {}
inline void GlfwPlatformBeginCaptionDrag(GLFWwindow*) {}
inline void GlfwPlatformSetTitleBarMetrics(float, float) {}
inline void GlfwPlatformApplySplashRoundedRegion(GLFWwindow*, float) {}
inline void GlfwPlatformClearWindowRegion(GLFWwindow*) {}
#endif
