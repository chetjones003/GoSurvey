# TASK-033 — TRIM defaults to smart line trim, behind a TRIMSTATE system variable

- Type:    feature
- Status:  self-verify (build green; awaiting the manual acceptance run — see §9)
- Opened:  2026-08-11
- Owner:   chetjones003

## 1. Authority
- Goal:         GOAL-01 (a CAD product usable for real survey deliverables)
- Requirements: REQ-056 (TRIM default + TRIMSTATE) — accepted 2026-08-11; REQ-201 (no silent failures)
- Constraints:  REQ-300 (no new dependency); REQ-301 (no abstraction without two present-day uses)
- Acceptance:   restated verbatim in `spec/requirements.md` under REQ-056
- Owning subsystem: Commands (mode + command), UI (hover gate), Viewport (highlight), IO (prefs)

## 2. Scope
- In scope: make the drawn-line trim the default; name the choice `TRIMSTATE` (0/1) and persist it;
  reuse the existing hover + selection highlighting while picking cutting edges; keep both modes
  reachable mid-run.
- Out of scope: a general system-variable registry (see §3); SETVAR/GETVAR; a Settings-dialog control for
  TRIMSTATE; extending hover to any other command.
- Smallest change: the drawn-line trim was already implemented behind the `L` option — this is a default,
  a name, and a persisted value over shipped behaviour, not a new trim algorithm.

## 3. Architectural boundary check
- New abstraction / layer / dependency / ownership change / global / data-format change?
    - [x] No — proceed.
- Reasoning: this is the codebase's **first system variable**, and the tempting move is a general
  variable registry (name → typed value, SETVAR/GETVAR, persistence table). That would be an
  architectural decision, and REQ-301 forbids it on one concrete use. TRIMSTATE is instead a plain `int`
  on `AppCommandState` plus a command, following the existing PLOTSCALE precedent for value-taking
  commands. **If a third or fourth system variable arrives, escalate a registry as a SPEC GAP then** —
  recorded in the decision log so the next person meets the question rather than re-deciding it silently.
- The hover change relaxes an existing rule for one command rather than adding a mechanism: OFFSET
  already carries its own hover field for the same reason, so the "commands suppress entity hover"
  rule was never absolute.

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| — | none — the request specified the default, the variable, its two values, and the feedback to reuse | — | — |

## 5. Assumptions
```
ASSUMPTION-1: TRIMSTATE persists in USER PREFERENCES, not in the .gs drawing.
- Because:       the request named the variable and its values but not its storage.
- Risk if wrong: the setting follows the user rather than the drawing; a drawing opened on another
                 machine trims in that machine's mode. Low impact — it changes no geometry.
- Validate by:   AutoCAD stores the comparable TRIMEXTENDMODE in the registry (per user), not the
                 drawing. Moving it to .gs later is additive and non-breaking.

ASSUMPTION-2: T (and the spellings "cutting"/"edges") is the option letter for switching to cutting-edge
              picking, mirroring the existing L.
- Because:       the request did not name a mid-run switch; L already existed, so its inverse was needed
                 for mode 0 to be escapable without restarting the command.
- Risk if wrong: a different letter is expected. Trivially changed.
- Validate by:   user run. AutoCAD's TRIM uses "cuTting edges" (T).
```

## 6. Plan
- Approach: choose the opening `TrimPhase` from `trimState` in `StartTrimCommand`; add the TRIMSTATE
  command in both AutoCAD prompt form and inline form over one shared validator; relax the hover gate
  for TRIM's two entity-picking phases; draw `trimCutters` through the existing selection highlight.
- Files/functions touched:
  - `commands/CadCommands.hpp` — `trimState`; `Kind::TrimState` + KindName; TrimPhase doc comment.
  - `commands/CadCommands.cpp` — `StartTrimCommand` mode choice; `StartTrimStateCommand`;
    `ApplyTrimStateValue` (one validator, both entry points); registry + dispatch; the TRIMSTATE prompt
    branch; `T`/`L` mid-run switches; cancel message; `TrimCommandFooterHint`.
  - `ui/CadUi.cpp` — hover gate exception for TRIM's pick phases; TRIMSTATE dynamic-input hint.
  - `viewport/TransformPreview.cpp` — cutters in `BuildSelectionHighlight`; already-picked cutter skipped
    in `BuildHoverHighlight`.
  - `io/UserPrefs.cpp` — persist + clamp `trimState`.
- Test approach:
  - happy path (manual): fresh profile trims with two clicks; `TRIMSTATE 1` restores edge picking and
    survives a restart; hover pre-highlights, picked edges stay highlighted.
  - failure mode: `TRIMSTATE 2` refused with a message and the value unchanged (`ApplyTrimStateValue`
    is the single gate, so neither the prompt nor the inline form can bypass it); a non-numeric value at
    the prompt re-prompts; blank Enter keeps the value; the loaded preference is clamped to 0/1.

## 7. Notes
- The value is validated in exactly one place. The prompt path and the `TRIMSTATE 1` inline path both
  call `ApplyTrimStateValue`, so a future third entry point cannot quietly accept an out-of-range value.
- `TRIMSTATE 2` leaves the command active and re-prompts rather than exiting, matching how the other
  in-command parse failures behave.
- No test-target coverage: every touched unit needs `AppCommandState` or ImGui, which the test binary
  deliberately does not link. The validator is the one piece that could be extracted to a pure header
  for testing, but at one `if` over two legal values that is not yet worth a header (REQ-301).
  Recorded as DEBT-1.

## 8. Technical debt
```
DEBT-1: REQ-056 has no automated coverage — mode selection, the validator and the hover gate are all
  manual-only.
- Constraint: they live in TUs the test target cannot link (AppCommandState / ImGui), the same
  constraint TASK-031 recorded for the DXF emitters.
- Remove when: the command layer gains a linkable test seam. `ApplyTrimStateValue` is the natural first
  candidate to move to a pure header if a second system variable arrives.

DEBT-2: TRIMSTATE is the first system variable and has no registry — by design (see §3).
- Constraint: one variable is not two concrete uses (REQ-301).
- Remove when: the NEXT system variable is requested. The user decided on 2026-08-11 that that request
  is the trigger to build the registry — the question is settled, so build it then rather than
  re-asking or adding a second ad-hoc int. TRIMSTATE moves into it as its first entry.
```

## 9. Verification
- `build-project`: clean. (The first link attempt failed with `lld-link: permission denied` because the
  app was still running; the user closed it and the relink succeeded — compilation had always been
  clean, only the write to the exe was blocked.)
- `testing`: 740 assertions / 115 cases green (unchanged — no test-target source was touched; see DEBT-1).
- `architecture-review`: no new abstraction, layer, dependency, global or data-format change; the
  registry temptation was refused and recorded. Each change sits in the subsystem that owns it.
- `dependency-audit`: none added.
- `performance-review`: the hover gate now runs the existing entity pick during TRIM's two pick phases —
  one `PickClosestCadEntity` per frame while hovering, the same cost OFFSET already pays in its select
  phase. Cutter highlighting is bounded by the number of picked edges.
- Not verified: everything in REQ-056's Acceptance list — all of it needs the running GUI.

COMPLETION REPORT — TASK-033 — 2026-08-11 (PROVISIONAL — acceptance run outstanding)
- Requirements satisfied:  REQ-056 (Acceptance: pending — manual GUI conditions, blocked on relink)
- Summary:                 TRIM now opens in smart line-trim mode by default; the choice is the new
                           TRIMSTATE system variable (0/1, persisted, prompt + inline forms over one
                           validator); T/L switch mid-run; cutting-edge picking reuses the existing
                           hover and selection highlighting.
- Tests:                   none added (DEBT-1); existing suite green
- Verification verdict:    INCOMPLETE — build and suite green; no manual acceptance run yet, and every
                           REQ-056 acceptance condition needs the running GUI
- Assumptions:             ASSUMPTION-1 (prefs, not .gs) and ASSUMPTION-2 (T as the option letter), both
                           open pending the user's run
- Architectural decisions: none made by Workshop; the system-variable-registry question was refused and
                           escalated into the decision log for the next variable
- Dependencies:            none added
- Technical debt noted:    DEBT-1 (no automated coverage), DEBT-2 (no registry, by design)
- Build:                   reproducible, clean on Windows (GoSurvey-0.4.0.exe relinked 2026-08-11)
- Docs updated:            spec/requirements.md (REQ-056 + matrix row), spec/project.md (decision log)
