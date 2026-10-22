# TASK: Qt primary accent becomes topo-line amber (issue #1031)

## Requirement authority
REQ-436 (amended 2026-10-21), decision D-2026-10-21-f. Issue #1031 acceptance criteria: aqua/light-blue primary accents in Qt chrome use the topo-line amber in Dark and Light; theme-specific brightness keeps text and interactive states readable; drawing/entity colors unchanged; the direction is recorded in the SPEC before code.

## Files / subsystems
- `src/qt/QtAccent.hpp` (new): the one palette (strong, fill, fillHover, tint, hoverTint, edge, link, linkHover, quiet, warn) for Dark and Light, plus `QtApplyAccent` for `@token@` stylesheets.
- `src/qt/QtAppearance.cpp` and every Qt file that carried its own aqua / blue literal (dialogs, Start page, PDF viewer, project dialogs).
- Tests: `QtEditorTests` `issue1031AmberPrimaryAccentInBothAppearances`; `issue910` grip colour follows the new accent.

## Approach
Replace each hard-coded aqua (`#55d8d0` family) and Light blue (`#438fda`/`#0078d7` family) with the shared amber palette; secondary emphasis becomes a neutral, warnings keep a gold. Hue ~30 degrees from the contour artwork; lightness per theme so white-on-fill >= 4.5:1.

## Architectural-boundary check
Qt presentation only. No new dependencies, no drawing/entity/persisted-data change.

## Recorded assumptions
- Dark canvas grid tint and navy Light-theme banners are treated as surface/drawing-area colour and left unchanged (user approved the screenshots 2026-10-21).
- Dark command icons are tinted neutral light-grey instead of light cyan.

## Completion report
See PR.
