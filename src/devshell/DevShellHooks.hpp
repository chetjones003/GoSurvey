#pragma once

#ifdef GOSURVEY_DEVELOPER_SHELL
#include "DevShell.hpp"
inline void DevShell_OnUi(const char* id)
{
  DevShell_Log("ui", id ? id : "");
}
inline void DevShell_OnCommand(const char* text)
{
  DevShell_Log("command", text ? text : "");
}
/// The 3D viewport's screen rectangle, recorded every frame (REQ-161).
///
/// Debug-only, and it exists for one reason: a Test Engine test that wants to put the cursor on a
/// known piece of GEOMETRY has to turn a world point into a screen point, and `Camera::WorldToScreen`
/// gives it the offset INSIDE the viewport image — not where that image sits on screen. Without this
/// the GUI driver can reach ribbons and command lines but never the viewport, which is where every
/// pick, hover and sub-object selection actually happens.
inline void DevShell_OnViewportRect(float originX, float originY, float sizeX, float sizeY)
{
  DevShell_SetViewportRect(originX, originY, sizeX, sizeY);
}

inline void DevShell_OnPick(float x, float y)
{
  DevShell_Logf("viewport", "pick %.4f,%.4f", static_cast<double>(x), static_cast<double>(y));
}
#else
inline void DevShell_OnUi(const char*) {}
inline void DevShell_OnCommand(const char*) {}
inline void DevShell_OnPick(float, float) {}
inline void DevShell_OnViewportRect(float, float, float, float) {}
#endif
