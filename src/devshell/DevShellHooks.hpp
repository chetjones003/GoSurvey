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
inline void DevShell_OnPick(float x, float y)
{
  DevShell_Logf("viewport", "pick %.4f,%.4f", static_cast<double>(x), static_cast<double>(y));
}
#else
inline void DevShell_OnUi(const char*) {}
inline void DevShell_OnCommand(const char*) {}
inline void DevShell_OnPick(float, float) {}
#endif
