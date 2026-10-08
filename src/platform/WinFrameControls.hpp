#pragma once

struct GLFWwindow;

#if defined(_WIN32)
void GlfwPlatformInstallBorderlessResize(GLFWwindow* window);
void GlfwPlatformBeginCaptionDrag(GLFWwindow* window);
/// Called each frame with the title bar row height and button-strip width (in ImGui/screen pixels).
/// The WndProc uses these to return HTCAPTION for the drag area and HTCLIENT for the button strip.
void GlfwPlatformSetTitleBarMetrics(float rowHeightPx, float btnStripWidthPx);
/// Called each frame: true while another ImGui window (a dialog) is what the pointer is over, so a drag
/// there is not taken as a title-bar drag even when the dialog overlaps the title bar.
void GlfwPlatformSetCaptionBlocked(bool blocked);
/// REQ-093 splash: clip the borderless window to rounded corners (desktop shows in the cutouts).
void GlfwPlatformApplySplashRoundedRegion(GLFWwindow* window, float cornerRadiusPx);
/// Remove a prior \ref GlfwPlatformApplySplashRoundedRegion before the main shell resizes.
void GlfwPlatformClearWindowRegion(GLFWwindow* window);
/// REQ-399: bring \p window to the foreground even when another of this process's own OS windows (a
/// floating PDF viewer, ADR-067) currently has it. Restores it first if minimized; plain
/// glfwFocusWindow does not reliably win focus away from a sibling top-level window on Windows.
void GlfwPlatformForceFocus(GLFWwindow* window);
#else
inline void GlfwPlatformInstallBorderlessResize(GLFWwindow*) {}
inline void GlfwPlatformBeginCaptionDrag(GLFWwindow*) {}
inline void GlfwPlatformSetTitleBarMetrics(float, float) {}
inline void GlfwPlatformSetCaptionBlocked(bool) {}
inline void GlfwPlatformApplySplashRoundedRegion(GLFWwindow*, float) {}
inline void GlfwPlatformClearWindowRegion(GLFWwindow*) {}
inline void GlfwPlatformForceFocus(GLFWwindow*) {}
#endif
