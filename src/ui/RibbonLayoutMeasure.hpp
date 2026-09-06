#pragma once

// REQ-302 / ADR-053 / issue #323 — measure pass for the ribbon layout engine (foundation part 2
// of 3: data model [#322] -> measure pass -> place pass [next], tied together by
// RibbonLayout::DrawSection in a later sub-issue).
//
// Computes each button/group/section's natural size from its actual content, replacing the
// per-tab hardcoded width formulas DrawRibbonBar currently hand-writes (`largeW`, `gridCell`,
// `colW(...)`, `RibbonBelowButtonWidth`, all private to CadUi.cpp). This issue does not touch
// those — it reuses the same layout constants that RibbonButtonEx's `RibbonLabel::Right` mode
// already draws by (CadUi.cpp:3202 RibbonButtonEx): the icon hugs the button edge with a 1px
// `iconPad`, the icon is a square sized to the row height, and the label sits 3px to its right.
// No draw calls here — only ImGui::CalcTextSize, which needs a live ImGui frame for font metrics.

#include "RibbonLayoutTypes.hpp"

#include <imgui.h>

#include <algorithm>

namespace ribbonlayout {

// Matches RibbonButtonEx's Right-mode icon inset (CadUi.cpp:3232, "icons hug the button edge").
inline constexpr float kMeasureIconPad = 1.f;
// Matches RibbonButtonEx's Right-mode icon-to-label gap (CadUi.cpp:3261, `iconMax.x + 3.f`).
inline constexpr float kMeasureLabelGap = 3.f;
// A button with no label still needs an icon square; ImGui's own row height (GetFrameHeight())
// is the same "one text line plus frame padding" metric CadUi.cpp derives its row heights from
// (e.g. CadUi.cpp:3694 `rowH`), so it is a reasonable icon side for content with no explicit size.
inline float MeasureIconSide() { return ImGui::GetFrameHeight(); }

struct RibbonMeasuredButton {
  const RibbonButtonSpec* spec = nullptr;
  ImVec2 size{0.f, 0.f};
};

struct RibbonMeasuredGroup {
  const RibbonGroupSpec* spec = nullptr;
  std::vector<RibbonMeasuredButton> buttons;
  std::vector<RibbonMeasuredGroup> groups;
  ImVec2 size{0.f, 0.f};
};

struct RibbonMeasuredSection {
  const RibbonSectionSpec* spec = nullptr;
  std::vector<RibbonMeasuredGroup> groups;
  ImVec2 size{0.f, 0.f};
};

// AutoFit: icon square (MeasureIconSide) + label text (CalcTextSize), side by side, padded the
// same way RibbonButtonEx's Right mode pads its content. Fixed: pass the spec's size through
// unchanged. Fill: left at (0, 0) — resolving Fill against available width is the place pass's job.
inline ImVec2 MeasureRibbonButton(const RibbonButtonSpec& btn) {
  switch (btn.sizePolicy) {
    case RibbonSizePolicy::Fixed:
      return ImVec2(btn.fixedSize, btn.fixedHeight > 0.f ? btn.fixedHeight : btn.fixedSize);
    case RibbonSizePolicy::Fill:
      return ImVec2(0.f, 0.f);
    case RibbonSizePolicy::AutoFit:
    default: {
      const float iconSide = MeasureIconSide();
      const bool hasLabel = !btn.label.empty();
      const ImVec2 textSize = hasLabel ? ImGui::CalcTextSize(btn.label.c_str()) : ImVec2(0.f, 0.f);
      const float w = kMeasureIconPad * 2.f + iconSide +
                      (hasLabel ? kMeasureLabelGap + textSize.x : 0.f);
      const float h = std::max(iconSide, textSize.y) + kMeasureIconPad * 2.f;
      return ImVec2(w, h);
    }
  }
}

// REQ-302/ADR-053 (issue #326): layout-aware group sizing. Row keeps the original sum/max
// behavior (gapX/gapY default to 0, so pre-existing Row-only specs measure identically to before);
// Column stacks buttons/sub-groups vertically; Grid wraps buttons into rows of `gridColumns`.
inline RibbonMeasuredGroup MeasureRibbonGroup(const RibbonGroupSpec& group) {
  RibbonMeasuredGroup out;
  out.spec = &group;
  out.buttons.reserve(group.buttons.size());
  out.groups.reserve(group.groups.size());

  for (const RibbonButtonSpec& btn : group.buttons) {
    RibbonMeasuredButton mb;
    mb.spec = &btn;
    mb.size = MeasureRibbonButton(btn);
    out.buttons.push_back(mb);
  }
  for (const RibbonGroupSpec& sub : group.groups)
    out.groups.push_back(MeasureRibbonGroup(sub));

  ImVec2 size(0.f, 0.f);
  switch (group.layout) {
    case RibbonGroupLayout::Column: {
      for (const RibbonMeasuredButton& mb : out.buttons) {
        size.x = std::max(size.x, mb.size.x);
        size.y += mb.size.y;
      }
      if (!out.buttons.empty())
        size.y += group.gapY * static_cast<float>(out.buttons.size() - 1);
      for (const RibbonMeasuredGroup& mg : out.groups) {
        size.x = std::max(size.x, mg.size.x);
        size.y += mg.size.y + group.gapY;
      }
      break;
    }
    case RibbonGroupLayout::Grid: {
      const int cols = group.gridColumns > 0 ? group.gridColumns : static_cast<int>(out.buttons.size());
      float cellW = 0.f, cellH = 0.f;
      for (const RibbonMeasuredButton& mb : out.buttons) {
        cellW = std::max(cellW, mb.size.x);
        cellH = std::max(cellH, mb.size.y);
      }
      const int n = static_cast<int>(out.buttons.size());
      const int rows = cols > 0 ? (n + cols - 1) / cols : 0;
      size.x = cols > 0 ? cols * cellW + std::max(0, cols - 1) * group.gapX : 0.f;
      size.y = rows > 0 ? rows * cellH + std::max(0, rows - 1) * group.gapY : 0.f;
      break;
    }
    case RibbonGroupLayout::Row:
    default: {
      for (const RibbonMeasuredButton& mb : out.buttons) {
        size.x += mb.size.x;
        size.y = std::max(size.y, mb.size.y);
      }
      if (!out.buttons.empty())
        size.x += group.gapX * static_cast<float>(out.buttons.size() - 1);
      for (const RibbonMeasuredGroup& mg : out.groups) {
        size.x += mg.size.x + group.gapX;
        size.y = std::max(size.y, mg.size.y);
      }
      break;
    }
  }
  out.size = size;
  return out;
}

inline RibbonMeasuredSection MeasureRibbonSection(const RibbonSectionSpec& section) {
  RibbonMeasuredSection out;
  out.spec = &section;
  out.groups.reserve(section.groups.size());

  ImVec2 size(0.f, 0.f);
  for (const RibbonGroupSpec& group : section.groups) {
    RibbonMeasuredGroup mg = MeasureRibbonGroup(group);
    size.x += mg.size.x;
    size.y = std::max(size.y, mg.size.y);
    out.groups.push_back(std::move(mg));
  }
  out.size = size;
  return out;
}

} // namespace ribbonlayout
