#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "WinFrameControls.hpp"

#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include <algorithm>

namespace {

WNDPROC g_prevWndProc = nullptr;
GLFWwindow* g_borderlessWindow = nullptr;

// Updated each frame by GlfwPlatformSetTitleBarMetrics so WM_NCHITTEST can route correctly.
float g_titleBarRowH     = 0.f;
float g_btnStripWidthPx  = 0.f;

int EdgeBorderPx() {
  HDC dc = GetDC(nullptr);
  const int dpi = dc ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
  if (dc)
    ReleaseDC(nullptr, dc);
  return std::max(6, MulDiv(8, dpi, 96));
}

LRESULT CALLBACK BorderlessWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  if (msg == WM_NCHITTEST && g_prevWndProc) {
    LRESULT hit = CallWindowProc(g_prevWndProc, hwnd, msg, wParam, lParam);
    if (hit != HTCLIENT)
      return hit;

    const int px = static_cast<int>(static_cast<SHORT>(LOWORD(lParam)));
    const int py = static_cast<int>(static_cast<SHORT>(HIWORD(lParam)));
    POINT pt{px, py};
    ScreenToClient(hwnd, &pt);
    RECT cr{};
    GetClientRect(hwnd, &cr);
    const int ww = cr.right - cr.left;
    const int hh = cr.bottom - cr.top;
    const int b  = EdgeBorderPx();

    const bool maximized = g_borderlessWindow &&
                           glfwGetWindowAttrib(g_borderlessWindow, GLFW_MAXIMIZED) == GLFW_TRUE;

    // Top-corner resize grips — checked before the title bar so corners remain resizable.
    if (!maximized) {
      const bool onLeft  = pt.x < b;
      const bool onRight = pt.x >= ww - b;
      const bool onTop   = pt.y < b;
      // Right corner only if outside the button strip.
      const bool inBtnStrip = g_btnStripWidthPx > 0.f && pt.x >= ww - (int)g_btnStripWidthPx;
      if (onTop && onLeft)              return HTTOPLEFT;
      if (onTop && onRight && !inBtnStrip) return HTTOPRIGHT;
    }

    // Title bar — button strip stays HTCLIENT (ImGui handles it); drag area returns HTCAPTION
    // so Windows manages the move natively without going through ImGui.
    if (g_titleBarRowH > 0.f && pt.y >= 0 && pt.y < (int)g_titleBarRowH) {
      if (g_btnStripWidthPx > 0.f && pt.x >= ww - (int)g_btnStripWidthPx)
        return HTCLIENT;
      return HTCAPTION;
    }

    // Below-title-bar resize edges (skip when maximized).
    if (!maximized) {
      const bool onLeft   = pt.x < b;
      const bool onRight  = pt.x >= ww - b;
      const bool onBottom = pt.y >= hh - b;
      if (onBottom && onLeft)  return HTBOTTOMLEFT;
      if (onBottom && onRight) return HTBOTTOMRIGHT;
      if (onBottom)            return HTBOTTOM;
      if (onLeft)              return HTLEFT;
      if (onRight)             return HTRIGHT;
    }
    return HTCLIENT;
  }
  if (g_prevWndProc)
    return CallWindowProc(g_prevWndProc, hwnd, msg, wParam, lParam);
  return DefWindowProc(hwnd, msg, wParam, lParam);
}

} // namespace

void GlfwPlatformInstallBorderlessResize(GLFWwindow* window) {
  if (!window)
    return;
  static bool installed = false;
  if (installed)
    return;
  HWND hwnd = glfwGetWin32Window(window);
  if (!hwnd)
    return;
  g_prevWndProc = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(hwnd, GWLP_WNDPROC));
  if (!g_prevWndProc)
    return;
  g_borderlessWindow = window;
  SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(BorderlessWndProc));
  installed = true;
}

void GlfwPlatformBeginCaptionDrag(GLFWwindow* window) {
  if (!window)
    return;
  HWND hwnd = glfwGetWin32Window(window);
  if (!hwnd)
    return;
  ReleaseCapture();
  SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
}

void GlfwPlatformSetTitleBarMetrics(float rowHeightPx, float btnStripWidthPx) {
  g_titleBarRowH    = rowHeightPx;
  g_btnStripWidthPx = btnStripWidthPx;
}

#endif // _WIN32
