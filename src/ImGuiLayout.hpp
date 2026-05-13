#pragma once

#include <string>
#include <vector>

struct AppCommandState;

/// Creates `resources/layouts` beside the executable (or cwd). When a saved .ini exists, defers loading until
/// \ref ImGuiLayout_CommitDeferredIniLoadIfNeeded (after the first full docking pass) so docking restores correctly.
/// \return true if that .ini already exists and is non-trivial — skip \ref SetupMainDockLayout on first frame.
bool ImGuiLayout_ConfigureIniPath(AppCommandState& st);

/// Call once per frame immediately before \c ImGui::Render() while startup deferred ini load is pending.
void ImGuiLayout_CommitDeferredIniLoadIfNeeded();

void ImGuiLayout_ListLayoutStems(std::vector<std::string>* out_sorted_stems);

bool ImGuiLayout_SwitchToLayout(AppCommandState& st, const char* layout_stem_utf8, std::vector<std::string>& log);

bool ImGuiLayout_SaveCurrentLayoutAs(AppCommandState& st, const char* layout_stem_utf8, std::vector<std::string>& log);

void ImGuiLayout_DrawViewLayoutMenu(AppCommandState& cmd, std::vector<std::string>& log);

void ImGuiLayout_DrawLayoutPopups(AppCommandState& cmd, std::vector<std::string>& log);
