#pragma once

// REQ-302 / ADR-053 / issue #324 — place pass for the ribbon layout engine (foundation part 3 of
// 3: data model [#322] -> measure pass [#323] -> place pass, tied together by
// RibbonLayout::DrawSection in a later sub-issue).
//
// Takes a RibbonMeasuredSection (already-computed natural sizes) and an available width, and
// computes final left-to-right draw positions. Pure layout math: no ImGui draw calls and no live
// ImGui frame/context needed (RibbonMeasuredButton/Group/Section are plain data by the time this
// runs), so this is fully unit-testable with hand-built measured input.

#include "RibbonLayoutMeasure.hpp"

#include <algorithm>
#include <vector>

namespace ribbonlayout {

// One placed button: its final position/size and whether it fit within availableWidth.
// `overflow` items are still placed (in case a caller wants to know where they *would* have
// gone) but a caller should treat them as not drawn directly — routing them to the ADR-038
// overflow popup is a later sub-issue's job, not this one's.
struct RibbonPlacedItem {
  const RibbonButtonSpec* spec = nullptr;
  ImVec2 pos{0.f, 0.f};
  ImVec2 size{0.f, 0.f};
  bool overflow = false;
};

struct RibbonPlacedSection {
  const RibbonSectionSpec* spec = nullptr;
  std::vector<RibbonPlacedItem> items;
};

namespace detail {

// Depth-first flatten: a group's own buttons first, then its nested sub-groups' buttons, in
// spec order — the same left-to-right reading order MeasureRibbonGroup summed widths in.
inline void CollectMeasuredButtons(const RibbonMeasuredGroup& group,
                                    std::vector<const RibbonMeasuredButton*>& out) {
  for (const RibbonMeasuredButton& btn : group.buttons)
    out.push_back(&btn);
  for (const RibbonMeasuredGroup& sub : group.groups)
    CollectMeasuredButtons(sub, out);
}

} // namespace detail

inline RibbonPlacedSection PlaceRibbonSection(const RibbonMeasuredSection& section, float availableWidth) {
  RibbonPlacedSection out;
  out.spec = section.spec;

  std::vector<const RibbonMeasuredButton*> flat;
  for (const RibbonMeasuredGroup& group : section.groups)
    detail::CollectMeasuredButtons(group, flat);

  // Pass 1: total width already claimed by Fixed/AutoFit items, how many Fill items there are,
  // and the tallest non-Fill item (Fill items borrow that height — they carried (0,0) out of the
  // measure pass, since Fill has no natural size of its own).
  float claimedWidth = 0.f;
  int fillCount = 0;
  float fillHeight = 0.f;
  for (const RibbonMeasuredButton* mb : flat) {
    if (mb->spec != nullptr && mb->spec->sizePolicy == RibbonSizePolicy::Fill) {
      ++fillCount;
    } else {
      claimedWidth += mb->size.x;
      fillHeight = std::max(fillHeight, mb->size.y);
    }
  }
  const float fillWidth = fillCount > 0 ? std::max(0.f, availableWidth - claimedWidth) / static_cast<float>(fillCount)
                                         : 0.f;

  // Pass 2: lay out left-to-right, marking anything that runs past availableWidth as overflow.
  out.items.reserve(flat.size());
  float x = 0.f;
  for (const RibbonMeasuredButton* mb : flat) {
    RibbonPlacedItem item;
    item.spec = mb->spec;
    const bool isFill = mb->spec != nullptr && mb->spec->sizePolicy == RibbonSizePolicy::Fill;
    item.size = isFill ? ImVec2(fillWidth, fillHeight) : mb->size;
    item.pos = ImVec2(x, 0.f);
    item.overflow = (x + item.size.x > availableWidth);
    out.items.push_back(item);
    x += item.size.x;
  }

  return out;
}

} // namespace ribbonlayout
