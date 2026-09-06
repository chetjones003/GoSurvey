#include "RibbonLayoutDraw.hpp"

#include "CadUi.hpp"
#include "RibbonLayoutMeasure.hpp"
#include "RibbonLayoutPlace.hpp"

#include <imgui.h>

namespace RibbonLayout {

int DrawSection(const ribbonlayout::RibbonSectionSpec& section, float availableWidth,
                const std::function<void(const std::string& buttonId)>& onClick) {
  const ribbonlayout::RibbonMeasuredSection measured = ribbonlayout::MeasureRibbonSection(section);
  const ribbonlayout::RibbonPlacedSection placed = ribbonlayout::PlaceRibbonSection(measured, availableWidth);

  const ImVec2 origin = ImGui::GetCursorScreenPos();
  int drawnCount = 0;
  for (const ribbonlayout::RibbonPlacedItem& item : placed.items) {
    if (item.overflow || item.spec == nullptr)
      continue;
    if (item.size.x <= 0.f || item.size.y <= 0.f)
      continue;

    ImGui::SetCursorScreenPos(ImVec2(origin.x + item.pos.x, origin.y + item.pos.y));
    const std::string strId = "##RibbonLayout_" + item.spec->id;
    const bool pressed = RibbonDrawButtonForLayout(
        strId.c_str(), item.spec->label.c_str(), item.spec->iconName.c_str(), item.size,
        item.spec->labelBelow, item.spec->disabled,
        item.spec->tooltip.empty() ? nullptr : item.spec->tooltip.c_str(), item.spec->iconKind);
    if (pressed && onClick)
      onClick(item.spec->id);
    ++drawnCount;
  }

  ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + measured.size.y));
  return drawnCount;
}

} // namespace RibbonLayout
