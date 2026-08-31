// CadUiInternal.hpp — UI-layer helpers shared BETWEEN the translation units that
// CadUi.cpp was split into (TASK-150 Phase 2, GitHub issue #142).
//
// Not public API. Declared here — rather than staying file-local statics of
// CadUi.cpp — only so a panel split into its own .cpp (CadUi_Properties.cpp, …)
// can still reach them. Defined once, in CadUi.cpp.
//
// If a helper is used by exactly one slice and nothing else, move it into that
// slice instead of adding it here.

#pragma once

#include <vector>
#include <string>

#include <imgui.h>

struct AppCommandState;

/// Fill the Properties panel body with its "nothing selected" placeholder.
void FillPropPanelEmpty();
/// Collapsible section header row inside the Properties panel. Returns open state.
bool PropSectionHeader(const char* label);
/// Paints the value-column cell background for the current Properties table row.
void PropValueCellBg();
/// Top highlight bevel on a chrome plate (title bars, panel headers).
void PlateTopHilite(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx);
/// Sorted-unique list of every layer name referenced anywhere in the drawing.
void CollectAllDrawingLayers(const AppCommandState& cmd, std::vector<std::string>* outSortedUnique);

/// Value-cell styling for a Properties-style table row. Push before the row's
/// widgets, pop after; each widget still needs SetNextItemWidth(-FLT_MIN).
void PushGridCellStyle();
void PopGridCellStyle();

/// Flags every spreadsheet-style grid uses, so they cannot drift: Sortable +
/// Reorderable + Resizable + a frozen header (the Sheets contract).
inline constexpr ImGuiTableFlags kGridTableFlags =
    ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
    ImGuiTableFlags_ScrollX | ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable |
    ImGuiTableFlags_Sortable | ImGuiTableFlags_SortMulti | ImGuiTableFlags_Hideable |
    ImGuiTableFlags_SizingStretchProp;
