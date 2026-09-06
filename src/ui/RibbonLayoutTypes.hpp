#pragma once

// REQ-302 / ADR-053 / issue #322 — declarative data model for the ribbon layout engine
// (foundation part 1 of 3: data model -> measure pass -> place pass, tied together by
// RibbonLayout::DrawSection in a later sub-issue). Plain data only: no ImGui calls, no
// measurement, no positions. Existing ribbon code (CadUi.cpp) is not touched by this issue.

#include <cstdint>
#include <string>
#include <vector>

namespace ribbonlayout {

// How an item's on-screen size is determined during the (later) measure/place passes.
enum class RibbonSizePolicy : std::uint8_t {
  Fixed,   // use the item's fixedSize as-is
  AutoFit, // size to content (label text metrics + icon size)
  Fill,    // share whatever width remains after Fixed/AutoFit items are placed
};

// A single ribbon button. `iconName` is the c3d_* icon identifier (as used by the existing
// ribbon icon table in CadUi.cpp) or empty for a text-only button; wiring an id/icon pair to
// that table is a later sub-issue's concern, not this one's.
struct RibbonButtonSpec {
  std::string id;
  std::string label;
  std::string iconName;
  RibbonSizePolicy sizePolicy = RibbonSizePolicy::AutoFit;
  float fixedSize = 0.f; // used only when sizePolicy == Fixed
};

struct RibbonGroupSpec; // fwd decl for the nested-group case below

// A group is either a flat run of buttons, or (rarely) nested sub-groups — e.g. a 2x2 grid of
// small buttons stacked under one group title. Exactly one of `buttons`/`groups` is expected to
// be populated by callers; both are left as plain vectors since this issue defines data only.
struct RibbonGroupSpec {
  std::string title;
  std::vector<RibbonButtonSpec> buttons;
  std::vector<RibbonGroupSpec> groups;
  RibbonSizePolicy sizePolicy = RibbonSizePolicy::AutoFit;
};

// A ribbon section/panel (e.g. "Draw", "Modify") — the vertical-divider-separated block seen in
// today's hand-built ribbon tabs.
struct RibbonSectionSpec {
  std::string title;
  std::vector<RibbonGroupSpec> groups;
};

} // namespace ribbonlayout
