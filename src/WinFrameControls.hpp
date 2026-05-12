#pragma once

struct GLFWwindow;

#if defined(_WIN32)
void GlfwPlatformInstallBorderlessResize(GLFWwindow* window);
void GlfwPlatformBeginCaptionDrag(GLFWwindow* window);
#else
inline void GlfwPlatformInstallBorderlessResize(GLFWwindow*) {}
inline void GlfwPlatformBeginCaptionDrag(GLFWwindow*) {}
#endif
