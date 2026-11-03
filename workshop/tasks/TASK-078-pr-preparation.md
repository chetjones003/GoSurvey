# PR preparation: 0.7.8 drafting and PDF workflows

User authorization: 2026-10-30, "ready to pr the changes on this branch", after reviewing the
trapezoid tab visualizer. Includes all branch work, not only the last tab change.
Target: beta. Existing assigned worktree and branch are retained at the user's explicit request.

## Standards

Independent review found three blocking issues: PDF snap caches copied on every frame, disabled
circle centers offered as endpoints, and PDF ROTATE previews gated on the MOVE phase. Fixed by
borrowing committed attachments during idle paint, using raster/transform-only preview copies,
filtering enabled snap categories before ranking, and letting ROTATE's own helper validate its phase.
No new dependency or persistence mechanism was introduced. No performance timing is claimed.

## Spec

Independent review found two blockers: zoom scales missing from per-drawing snapshots and snapped
crosshair coordinates projected with the world origin added twice. Both were corrected. Review
also closed incomplete idle/additive box and command-drag event paths from the original report,
and rejected malformed/nonfinite custom ratios. New regression checks cover all these behaviors,
including native DWG and DXF SCALE round-trips with zoom/annotation lists kept separate.

The original independent review verdicts were FAIL. Their concrete findings were fixed and checked
directly; this does not relabel those original reports as passing. No unresolved finding remains
from either report. Validation results below remain authoritative.

## Product documentation review (REQ-453)

| Source | Outcome |
|---|---|
| README.md | Reviewed; no update needed. Existing download, exchange, build and documentation guidance retains its meaning. |
| PRIVACY.md | Reviewed; no update needed. No new telemetry, upload, external service or personal-data collection. |
| resources/wiki/ | Updated User Interface, Views and Navigation, Object Selection, Modify Tools, Object Snaps and PDF Underlays for the actual controls and workflows. |
| External manual wiki | Reviewed via https://github.com/chetjones003/GoSurvey.wiki.git; the public browser wiki URL redirects and the site was unavailable to web fetch. Applied the same six-page patch without replacing unrelated updates and pushed commit 152719648b82d769a78b0aef05aa8c22cd2e3aa7 before PR creation. |

## Integration and visual evidence

Merged origin/beta at 8ef34971 into this branch before final validation. Kept both sides of the
decision log and reconciled ribbon inventory additions with beta's removed placeholders. Model
zoom became REQ-458 because REQ-457 now governs sheet IMAGE underlays. The user requested DWG/DXF;
GS is not introduced as a supported user format. GsIo remains the existing internal payload service.

Status-bar panel PASS: `build/artifacts/task-078-ui-comparison/COMPARISON-RECORD.md`.
Tab panel PASS for paired Dark drawing-active strip: `build/artifacts/Release/document-tabs/COMPARISON-RECORD.md`.
Unpaired Light/Start-active/hover/focus/long-name/overflow states have documented comparison limitations;
they were inspected for readability, without claiming exact Civil parity. Screenshots were delivered
through the visualizer and the user approved proceeding to PR. Other UI captures already produced in
this task remain under build/artifacts. No screenshots or generated evidence are committed.

## Final validation

- Release build (`.\build.bat`): **PASS**, no new compiler warnings. Existing vswhere/optional Vulkan
  discovery messages remain. Log: `build/artifacts/pr-final-build.log`.
- Full suite (`ctest --preset ninja-release --parallel 6 --output-on-failure`): **PASS, 191/191**,
  0 failed, 117.93s. Includes both Catch shards, all six hidden-desktop Qt shards, all headless
  transcripts, Qt deployment/parity and build output-layout checks.
- Ribbon inventory (`python tools/audit-qt-ribbon.py`): **PASS**, 264 release button IDs covered,
  282 native entries, all icons available. Updated the missing PDF control icon names to existing assets.
- `git diff --check`: **PASS**.
- Machine-readable results: `build/artifacts/pr-ctest.xml`; readable log:
  `build/artifacts/pr-final-ctest.log`. Initial failure evidence remains in `pr-initial-ctest.*`.

The first full run failed 16 checks: default zoom presets being materialized during import broke
idempotent DWG resaves, a missing drag-release handler prevented box commits, and two Qt expectations
still used the old tab parent coordinates/ribbon inventory. Fixed the causes: loaders preserve an
absent preset list; defaults are initialized when the control/preset service is used; drag release
submits the rectangle; tab geometry is compared in common window coordinates; the inventory reflects
the added PDF controls. The complete rerun passed. Initial compilation attempts also exposed corrected
test fixture errors and an incorrect unused-variable cleanup; no partial run is described as green.

Final status: ready for review; commit and PR publication authorized by the user.
