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
  float fixedSize = 0.f;   // used only when sizePolicy == Fixed (width, and height if fixedHeight <= 0)
  float fixedHeight = 0.f; // Fixed only: independent height for a non-square Fixed button; <= 0 means square (use fixedSize)
  // REQ-302/ADR-053 (issue #326): a button that is not implemented yet (RibbonNyiButton's role in
  // the hand-built ribbon code). Disabled buttons are still measured/placed/drawn, but drawn
  // greyed-out, non-clickable, and never invoke onClick.
  bool disabled = false;
  // Tooltip text shown on hover (RibbonItemHelp's role). Empty means no tooltip.
  std::string tooltip;
  // Right (default, false) matches RibbonLabel::Right; true matches RibbonLabel::Below.
  bool labelBelow = false;
  // Opaque numeric id for CadUi.cpp's private RibbonIconKind enum (a real command's built-in
  // vector glyph, e.g. Line/Move/Circle), used instead of `iconName`'s c3d_* named-icon lookup.
  // -1 (default) means "no override" -- fall back to iconName, or the generic NYI glyph if that
  // is empty too. This header cannot name RibbonIconKind itself (it is private to CadUi.cpp);
  // the caller casts its own enum to int, and RibbonDrawButtonForLayout casts it back.
  int iconKind = -1;
};

// How a group arranges its own buttons/sub-groups relative to each other.
enum class RibbonGroupLayout : std::uint8_t {
  Row,    // left-to-right, single line (original/default behavior)
  Column, // top-to-bottom, single column
  Grid,   // wraps buttons into rows of `gridColumns` (0 = one row, same as Row)
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
  RibbonGroupLayout layout = RibbonGroupLayout::Row;
  int gridColumns = 0;  // Grid layout only: buttons per row (0 -> all buttons in one row)
  // Gap between consecutive buttons/sub-groups placed by this group (x for Row/Grid columns,
  // y for Column/Grid rows). Defaults to 0 to match the original flush-adjacent placement.
  float gapX = 0.f;
  float gapY = 0.f;
};

// A ribbon section/panel (e.g. "Draw", "Modify") — the vertical-divider-separated block seen in
// today's hand-built ribbon tabs.
struct RibbonSectionSpec {
  std::string title;
  std::vector<RibbonGroupSpec> groups;
  // Gap between consecutive top-level groups. Defaults to 0 to match the original behavior of
  // treating a section's groups as one continuous flattened row.
  float groupGapX = 0.f;
};

} // namespace ribbonlayout
