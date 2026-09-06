#pragma once

// REQ-302 / ADR-053 / issue #325 — RibbonLayout::DrawSection ties the three foundation passes
// (data model #322 -> measure #323 -> place #324) together and performs the actual ImGui draw
// calls, reusing RibbonDrawButtonForLayout (CadUi.cpp) so the chrome matches every existing
// hand-built ribbon section exactly. No ribbon tab is migrated to this API yet — that's #326-#338.

#include "RibbonLayoutTypes.hpp"

#include <functional>

namespace RibbonLayout {

/// Runs measure -> place -> draw for `section` within `availableWidth`, at the ImGui cursor's
/// current screen position (must be called inside an ImGui window). `onClick`, if set, is invoked
/// with the id of any button pressed this frame — command dispatch stays the caller's job, same
/// as every existing ribbon section separates "draw the button" from "run the command". Items the
/// place pass marked as overflow are NOT drawn (routing them to the ADR-038 overflow popup is a
/// later sub-issue); returns the number of items actually drawn.
int DrawSection(const ribbonlayout::RibbonSectionSpec& section, float availableWidth,
                const std::function<void(const std::string& buttonId)>& onClick = nullptr);

} // namespace RibbonLayout
