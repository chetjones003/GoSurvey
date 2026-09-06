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
// spec order — the same left-to-right reading order MeasureRibbonGroup summed widths in. Used
// only to resolve Fill widths/height globally, independent of each group's own layout.
inline void CollectMeasuredButtons(const RibbonMeasuredGroup& group,
                                    std::vector<const RibbonMeasuredButton*>& out) {
  for (const RibbonMeasuredButton& btn : group.buttons)
    out.push_back(&btn);
  for (const RibbonMeasuredGroup& sub : group.groups)
    CollectMeasuredButtons(sub, out);
}

// REQ-302/ADR-053 (issue #326): places one group's buttons/sub-groups starting at `origin`,
// according to the group's own layout (Row/Column/Grid), and returns the width the group actually
// occupies (so the caller can advance its own cursor). Row is the original behavior (gapX/gapY
// default to 0, so pre-existing Row-only specs place identically to before).
inline float PlaceGroupItems(const RibbonMeasuredGroup& group, ImVec2 origin, float fillWidth, float fillHeight,
                              std::vector<RibbonPlacedItem>& out) {
  const RibbonGroupLayout layout = group.spec != nullptr ? group.spec->layout : RibbonGroupLayout::Row;
  const float gapX = group.spec != nullptr ? group.spec->gapX : 0.f;
  const float gapY = group.spec != nullptr ? group.spec->gapY : 0.f;

  switch (layout) {
    case RibbonGroupLayout::Column: {
      float y = origin.y;
      float maxW = 0.f;
      for (const RibbonMeasuredButton& mb : group.buttons) {
        RibbonPlacedItem item;
        item.spec = mb.spec;
        const bool isFill = mb.spec != nullptr && mb.spec->sizePolicy == RibbonSizePolicy::Fill;
        item.size = isFill ? ImVec2(fillWidth, fillHeight) : mb.size;
        item.pos = ImVec2(origin.x, y);
        out.push_back(item);
        maxW = std::max(maxW, item.size.x);
        y += item.size.y + gapY;
      }
      for (const RibbonMeasuredGroup& sub : group.groups) {
        const float w = PlaceGroupItems(sub, ImVec2(origin.x, y), fillWidth, fillHeight, out);
        maxW = std::max(maxW, w);
        y += sub.size.y + gapY;
      }
      return maxW;
    }
    case RibbonGroupLayout::Grid: {
      const int cols = (group.spec != nullptr && group.spec->gridColumns > 0)
                            ? group.spec->gridColumns
                            : static_cast<int>(group.buttons.size());
      float cellW = 0.f, cellH = 0.f;
      for (const RibbonMeasuredButton& mb : group.buttons) {
        cellW = std::max(cellW, mb.size.x);
        cellH = std::max(cellH, mb.size.y);
      }
      int i = 0;
      for (const RibbonMeasuredButton& mb : group.buttons) {
        const int r = cols > 0 ? i / cols : 0;
        const int c = cols > 0 ? i % cols : i;
        RibbonPlacedItem item;
        item.spec = mb.spec;
        item.size = mb.size; // Fill is not meaningful inside a Grid; not resolved here.
        item.pos = ImVec2(origin.x + static_cast<float>(c) * (cellW + gapX),
                           origin.y + static_cast<float>(r) * (cellH + gapY));
        out.push_back(item);
        ++i;
      }
      return cols > 0 ? cols * cellW + std::max(0, cols - 1) * gapX : 0.f;
    }
    case RibbonGroupLayout::Row:
    default: {
      float x = origin.x;
      for (const RibbonMeasuredButton& mb : group.buttons) {
        RibbonPlacedItem item;
        item.spec = mb.spec;
        const bool isFill = mb.spec != nullptr && mb.spec->sizePolicy == RibbonSizePolicy::Fill;
        item.size = isFill ? ImVec2(fillWidth, fillHeight) : mb.size;
        item.pos = ImVec2(x, origin.y);
        out.push_back(item);
        x += item.size.x + gapX;
      }
      for (const RibbonMeasuredGroup& sub : group.groups) {
        const float w = PlaceGroupItems(sub, ImVec2(x, origin.y), fillWidth, fillHeight, out);
        x += w + gapX;
      }
      const bool hadAny = !group.buttons.empty() || !group.groups.empty();
      return hadAny ? (x - origin.x - gapX) : 0.f;
    }
  }
}

} // namespace detail

inline RibbonPlacedSection PlaceRibbonSection(const RibbonMeasuredSection& section, float availableWidth) {
  RibbonPlacedSection out;
  out.spec = section.spec;

  // Fill resolution stays global (same semantics as before this issue): claimed width/fill count
  // are gathered across every button in the section regardless of which group/layout it lives in.
  std::vector<const RibbonMeasuredButton*> flat;
  for (const RibbonMeasuredGroup& group : section.groups)
    detail::CollectMeasuredButtons(group, flat);

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

  const float groupGap = section.spec != nullptr ? section.spec->groupGapX : 0.f;
  float x = 0.f;
  for (const RibbonMeasuredGroup& group : section.groups) {
    const float w = detail::PlaceGroupItems(group, ImVec2(x, 0.f), fillWidth, fillHeight, out.items);
    x += w + groupGap;
  }

  for (RibbonPlacedItem& item : out.items)
    item.overflow = (item.pos.x + item.size.x > availableWidth);

  return out;
}

} // namespace ribbonlayout
