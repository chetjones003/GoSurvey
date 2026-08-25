# TASK-109 — REQ-304: Dynamic cursor text matches the command line for every command state (GitHub issue #82)

- Type:    feature
- Status:  done — implemented, self-verified; manual GUI pass (wording/visual confirmation) pending
- Opened:  2026-08-25
- Owner:   Workshop

## 1. Authority

- Requirements: **REQ-304** (new, this task — Commands/UI), Acceptance items 1-17.
- Constraints:  REQ-301 (no new abstraction — extend the existing `DrawingExtrasFooterHint`
                delegate rather than inventing a second hint mechanism).
- Acceptance:   see REQ-304 in `spec/requirements.md`.
- Owning subsystem: Commands (`CadCommands.cpp` — `DrawingExtrasFooterHint`) / UI (`CadUi.cpp`,
                consumes the function unchanged).

## 2. Feature request (issue #82, filed by chetjones003)

Summary: dynamic cursor text is inconsistent across commands — LINE shows a correct state-specific
prompt near the cursor, other commands show nothing. Asked for a single source of truth driving both
the command line and the cursor prompt, a full sweep of every command/state, and no staleness across
transitions. 17-item acceptance checklist in the issue body.

## 3. Architectural boundary check

- Does this need a NEW abstraction / layer / dependency / ownership change / global state /
  public-API or data-format change / algorithm the spec didn't specify?
  - [x] No — proceed.
- Reading the code before planning anything showed the single-source-of-truth mechanism the issue
  asked for **already existed**: `CommandInputHint` (`CadUi.cpp:6111`) and its FooterHint delegates
  (declared `CadCommands.hpp:3721-3732`) are called fresh every frame and feed both
  `DrawCommandLinePanel` (`CadUi.cpp:7251`) and the dynamic-cursor palette (`CadUi.cpp:12681-12693`)
  from the identical value. This is not a new design — it is closing coverage gaps in one.

## 4. Investigation — the actual audit

Cross-referenced every value of `AppCommandState::Kind` (`CadCommands.hpp:1122`, 43 values) against
every branch in `CommandInputHint` and its delegates (`AlignCommandFooterHint`,
`LineCommandFooterHint`, `DrawingExtrasFooterHint`, `ModifyCommandFooterHint`,
`RotateCommandFooterHint`, `ScaleCommandFooterHint`, `DeleteCommandFooterHint`,
`JoinCommandFooterHint`, `TrimCommandFooterHint`, `OffsetCommandFooterHint`,
`ZoomCommandFooterHint`, `CircleCommandFooterHint`). 10 Kinds had no branch anywhere, falling
through to the generic `"Command:"` on both the command line and the cursor:
`FeatureLine`, `Fillet`, `Chamfer`, `PdfAttach`, `Hatch`, `Pan`, `VpFreeze`, `VpThaw`, `Elev`, `Orbit`.

Checked each before assuming it needed a fix:
- **`Pan`/`Orbit`**: `CadUi.cpp:12878-12884` already special-cases these two — a hand cursor replaces
  the crosshair and the point-entry palette is explicitly suppressed for `Pan`
  (`showViewportCmdPalette`'s condition excludes it, `CadUi.cpp:12646`). This is deliberate,
  documented (REQ-045/REQ-084 (c)) contextual feedback for a continuous drag with no typed value.
  **Not a gap** — excluded from the fix, documented as such in REQ-304.
- **`Fillet`/`Chamfer`**: had real per-transition text, but only as one-time `log.push_back`
  scrollback lines (`StartFilletCommand`, `HandleFilletViewportPick`, `HandleFilletText` and the
  CHAMFER equivalents) — never a live-queryable function. This is exactly the "command line says one
  thing, cursor says nothing" pattern the issue names.
- **`FeatureLine`, `PdfAttach`, `Hatch`, `VpFreeze`, `VpThaw`, `Elev`**: no per-state hint mechanism
  at all — confirmed by grepping every `Kind::<X>` reference in `CadUi.cpp` and `CadCommands.cpp`
  and finding only start/completion log lines, no live query.

## 5. Plan

- Approach: extend `DrawingExtrasFooterHint` (`CadCommands.cpp:22338`) with one new `if` block per
  gap, reusing each command's own existing state fields and, for FILLET/CHAMFER, the same
  `FilletPromptSuffix`/`ChamferPromptSuffix`-shaped `<R=.., Trim>` formatting already used in their
  log lines, so the live hint and the historical scrollback read consistently. Dynamic numeric values
  (radius, elevation, distances, angle) use a function-local `static char buf[...]` + `snprintf`,
  the same pattern the file already uses elsewhere for formatted `const char*` returns.
- Files/functions to touch: `src/commands/CadCommands.cpp` (`DrawingExtrasFooterHint` only).
- Test approach: build clean + full existing regression/headless suite green (no behavior changed,
  only new read-only query branches added — nothing to regress). No new automated test: see §6.
- Steps:
  - [x] enumerate every `Kind` value and cross-reference against `CommandInputHint`/delegates
  - [x] confirm `Pan`/`Orbit` are intentional, not gaps
  - [x] read FILLET/CHAMFER's phase enums, awaiting-flags, and existing log wording exactly
  - [x] read FeatureLine/PdfAttach/Hatch/VpFreeze/VpThaw/Elev's state fields and existing log wording
  - [x] add the 8 new branches to `DrawingExtrasFooterHint`
  - [x] rebuild, run full test suite
  - [x] write REQ-304, decision-log entry, this task log
  - [x] check off issue #82's acceptance checklist and comment with the findings

## 6. Testing — why this is manual, not automated

`GoSurveyTests` deliberately does not link `CadCommands.cpp` (its own top-of-target comment:
"stays free of the UI/GL/Win32 layers" — every test against `CadCommands.hpp` today is header-only
against pure/inline functions). `DrawingExtrasFooterHint` is a real, non-inline `.cpp` function, so
unit-testing it would mean linking the whole Commands layer into the pure-domain test binary — a
real change to that target's boundary, undiscussed and out of proportion to this task. The headless
transcript driver (REQ-203) validates document invariants, not UI prompt strings, so it cannot cover
this either. Verification is therefore build-clean + full regression green (proves no behavior
regression) plus a manual GUI pass for the wording/visual quality of the 8 new strings — the same
verification category REQ-024 itself uses, and the same "cannot simulate mouse hover"
limitation already on record (TASK-107/108, `project_gui_hover_not_automatable` memory).

## 7. Implementation log

- 2026-08-25 — audited all 43 `Kind` values; found 10 gaps, excluded 2 (Pan/Orbit) as by-design.
- 2026-08-25 — added 8 new branches to `DrawingExtrasFooterHint` (FeatureLine, Fillet, Chamfer,
  PdfAttach, Hatch, VpFreeze, VpThaw, Elev).
- 2026-08-25 — rebuilt (MSVC/VS "18", vcvars64 x64): clean, no new warnings introduced.
- 2026-08-25 — `ctest -C Release`: 593/593 passed (1 pre-existing disabled, unrelated), unchanged
  from the pre-task baseline.
- 2026-08-25 — REQ-304 written, D-2026-08-25-k recorded, issue #82 checklist updated + commented.

## 8. Self-verification

- [x] build-project        — PASS (clean rebuild, MSVC/VS "18")
- [x] architecture-review  — PASS (no new abstraction/dependency/layer; extends an existing,
      explicitly-designed-for-this extension point)
- [x] code-review          — PASS (new branches follow the file's own existing style exactly —
      same `using` alias pattern, same `static char buf` + `snprintf` idiom used elsewhere in this
      file for dynamic `const char*` returns)
- [x] dependency-audit     — n/a (no dependency touched)
- [x] performance-review   — n/a (a handful of extra `if` comparisons on an already-per-frame
      string-lookup function; no measurable cost)
- [x] testing              — PASS for what is automatable (full regression suite green, unchanged
      pass count); manual GUI pass for wording/visual quality explicitly left pending (§6)

## 9. Verification result

- Submitted:  2026-08-25
- Verdict:    PASS
- Findings:   none blocking; manual GUI pass noted as outstanding, same category as prior UI tasks

## 10. Outcome

- Requirements satisfied: REQ-304 (Acceptance met: yes, with the manual-pass caveat recorded above)
- Tests added:            none (see §6 for why); full existing suite re-run and green
- Refactors:              none
- Docs updated:           `spec/requirements.md` (REQ-304 + traceability row), `spec/project.md`
  (D-2026-08-25-k), this task log, GitHub issue #82 (checklist + comment)
- Done:                   2026-08-25
