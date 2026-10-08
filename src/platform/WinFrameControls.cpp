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
bool  g_captionBlocked   = false;  // another ImGui window covers the title bar under the pointer

int EdgeBorderPx() {
  HDC dc = GetDC(nullptr);
  const int dpi = dc ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
  if (dc)
    ReleaseDC(nullptr, dc);
  return std::max(6, MulDiv(8, dpi, 96));
}

LRESULT CALLBACK BorderlessWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  // The window carries WS_THICKFRAME so Windows treats it as a normal resizable window (Aero Snap:
  // drag to the top to maximize, to a side to half-screen, snap layouts). The whole window is client
  // area, so the frame is never drawn. A caption-less window is maximized to exactly the work area (no
  // frame overhang), so nothing is trimmed here.
  if (msg == WM_NCCALCSIZE && wParam == TRUE)
    return 0;
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
    // A dialog dragged over the title bar must stay a dialog: the OS only gets the caption when the
    // title bar itself is what is under the pointer.
    if (!g_captionBlocked && g_titleBarRowH > 0.f && pt.y >= 0 && pt.y < (int)g_titleBarRowH) {
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
  HWND hwnd = glfwGetWin32Window(window);
  if (!hwnd)
    return;
  // Undecorated GLFW windows are plain WS_POPUP, which Windows never snaps or maximizes by drag.
  const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
  const LONG_PTR want = style | WS_THICKFRAME | WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_SYSMENU;
  if (want != style) {
    SetWindowLongPtrW(hwnd, GWL_STYLE, want);
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
  }
  static bool installed = false;
  if (installed)
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

void GlfwPlatformSetCaptionBlocked(bool blocked) {
  g_captionBlocked = blocked;
}

void GlfwPlatformApplySplashRoundedRegion(GLFWwindow* window, float cornerRadiusPx) {
  if (!window || cornerRadiusPx < 0.5f)
    return;
  HWND hwnd = glfwGetWin32Window(window);
  if (!hwnd)
    return;
  RECT rc{};
  if (!GetClientRect(hwnd, &rc))
    return;
  const int w = rc.right - rc.left;
  const int h = rc.bottom - rc.top;
  if (w <= 0 || h <= 0)
    return;
  const int r = std::max(1, static_cast<int>(cornerRadiusPx + 0.5f));
  const int diam = std::max(2, r * 2);
  HRGN rgn = CreateRoundRectRgn(0, 0, w + 1, h + 1, diam, diam);
  if (!rgn)
    return;
  if (!SetWindowRgn(hwnd, rgn, TRUE))
    DeleteObject(rgn);
}

void GlfwPlatformClearWindowRegion(GLFWwindow* window) {
  if (!window)
    return;
  HWND hwnd = glfwGetWin32Window(window);
  if (!hwnd)
    return;
  SetWindowRgn(hwnd, NULL, TRUE);
}

void GlfwPlatformForceFocus(GLFWwindow* window) {
  if (!window)
    return;
  HWND hwnd = glfwGetWin32Window(window);
  if (!hwnd)
    return;
  if (IsIconic(hwnd))
    ShowWindow(hwnd, SW_RESTORE);
  // AttachThreadInput lets this thread's SetForegroundWindow win even when Windows' foreground lock
  // would otherwise ignore it — both windows belong to this same process, but they can still be driven
  // by different message-pump calls (an ImGui viewport's own window loop), which is enough for Windows
  // to treat the request as "not the thread that currently owns the foreground."
  const DWORD thisThread = GetCurrentThreadId();
  const DWORD fgThread = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
  const bool attach = fgThread != 0 && fgThread != thisThread;
  if (attach)
    AttachThreadInput(thisThread, fgThread, TRUE);
  // The topmost-toggle trick: Windows lets ANY window raise itself to HWND_TOPMOST regardless of the
  // foreground lock, so flipping it on then straight back off forces the z-order change that
  // SetForegroundWindow alone is sometimes refused (silently, no error) when the caller is not the
  // thread Windows currently considers "has recent input" — which a same-process sibling window
  // (the floating PDF viewer, ADR-067) does not reliably count as.
  SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  BringWindowToTop(hwnd);
  SetForegroundWindow(hwnd);
  SetActiveWindow(hwnd);
  if (attach)
    AttachThreadInput(thisThread, fgThread, FALSE);
  glfwFocusWindow(window);
}

#endif // _WIN32
