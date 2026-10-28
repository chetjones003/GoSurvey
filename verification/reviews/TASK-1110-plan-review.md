# TASK-1110 plan review — 2026-10-28

## Authority and specification decision

**APPROVE.** GitHub issue #1041 gives a concrete Qt-only surface list, requires both themes, preserves the semantic accent palette and viewport, and names the interactions that must continue to work. Its existing owner-authored SPEC GAP comment identifies REQ-081's Light-theme freeze. The issue itself explicitly requests the REQ-081 amendment, and D-2026-10-28-b records the user's current instruction to resolve gaps through repository and product evidence. REQ-436's Light-surface freeze is amended only for those listed workspace marks; other palette and surface rules remain binding.

The Autodesk AutoCAD help pages describe ribbon tabs, ribbon panels, workspace membership, dock state, palette position and size, but do not prescribe rounding, elevation, or theme styling. Those pages support the surface ownership and functional regression checks; they do not add design requirements. Existing `QtAppearance.cpp` rules already own and style these Qt surfaces, so the codebase supplies the right implementation seam.

## Architectural impact

- Owner remains the Qt UI appearance layer.
- No new dependency, abstraction, state owner, public API, persistence, or data format.
- Drawing viewport/entity rendering remains outside scope.

## Risks

- Added margins, border widths, and rounding could reduce viewport space or clip controls at minimum width; keep spacing restrained and verify narrow/resized layouts.
- Global selectors could unintentionally change dialogs, PDF windows, or Start surfaces; scope selectors to the Qt drawing workspace and verify representative captures.
- Light/Dark switching can leave theme-specific values stale; verify round-trip switching.

## Required verification

- Focused Qt coverage for both themes, ribbon tabs' normal/hover/selected states, palette switching, and existing docking/resizing/navigation behavior.
- Release build plus relevant Qt editor tests and output-layout check.
- Captures of the drawing workspace in both themes, including a resized layout; the repo UI-review workflow requires presenting those images and waiting for user review before merge.
- Review README, PRIVACY, in-app wiki, and public manual wiki before opening the PR; appearance-only change is expected to need no product-doc update, but each source and outcome must be recorded.

## Verdict

**APPROVE** — the issue and recorded decision make the intended behavior testable and narrow enough to implement without further user choice. Proceed with D-2026-10-28-b and the TASK-1110 plan.
