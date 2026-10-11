# TASK-074 — Surface Manager panel: definition tree and Add dialogs

- Type:    feature
- Status:  done
- Opened:  2026-08-19
- Owner:   chetjones003

## 1. Authority
- Goal:         M-Grading step 1 (`spec/roadmap.md`), decision D-2026-08-19-a.
- Requirements: **REQ-075 (accepted 2026-08-12)** — Surface Manager. REQ-069 (accepted) supplies the
                definition operations the panel exposes. REQ-201 (accepted) — every action reports.
- Constraints:  architecture §11.5 (replace the TIN pointer, never write through it), §11.9 (stable
                identifiers, never indices, for cross-object references).
- Acceptance (REQ-075, verbatim):
  - every REQ-069 definition operation is reachable from the panel;
  - a rebuild is reflected in the displayed counts and elevation range;
  - a surface that is out of date or rebuilding is shown as such, and the state clears when the
    rebuild lands;
  - deleting a surface from the panel is undoable in one step;
  - renaming to a name already in use is refused with a specific message.
- Owning subsystem: `ui/` (the panel), with a small additive change in `commands/` + `io/` — below.

## 2. Scope
- In scope:
  - An explorer **tree**: `Surfaces ▸ <surface> ▸ Definition ▸ Point Groups / Breaklines /
    Boundaries / Point Files`, with counts on each node.
  - **Right-click ▸ Add… / Remove / Refresh** on the definition nodes, and per-item Remove.
  - **Add Boundaries** dialog: Name, Type (Outer / Show / Hide), then pick a closed polyline.
  - **Add Breaklines** dialog: Description, Type (Standard), then pick a line or polyline.
  - **Reorder** breaklines and boundaries — REQ-075 says "add, remove **and reorder**", and for
    boundaries the order is load-bearing (REQ-069: "boundaries apply in definition order").
  - **Stale / rebuilding indicator**, the one REQ-075 acceptance condition with nothing behind it.
- Out of scope:
  - The Point Files node **acts** — that is REQ-086, still `proposed`. The node is present and its
    Add… is disabled with a reason, so the tree matches the agreed shape without a dead control that
    pretends to work.
  - Breakline types beyond Standard, and the Data Clip boundary type (ADR-028 defers them; see Q1).
  - Masks, Watersheds, Contours, DEM Files, Drawing Objects, Edits — the user chose a four-node tree.
- Smallest change: the panel is a rewrite of one 242-line file; the domain change is two struct
  fields and their `.gs` round trip.

## 3. Architectural boundary check
- The panel itself — **no**. UI over existing command-layer functions.
- **Data-format change — YES, and it is called out rather than slipped in.** The agreed dialogs carry
  a boundary **Name** and a breakline **Description**, and neither exists today. This adds:
  - `CadSurfaceBoundary::name` (new field), and
  - `CadSurfaceBreakline { entityId, description }` replacing `std::vector<std::uint64_t>
    breaklineIds`, mirroring the shape `CadSurfaceBoundary` already has.
  Both are **additive and backward-compatible**: `.gs` gains `breaklines: [{entityId, description}]`
  and keeps reading a legacy `breaklineIds: [n, …]`, so an existing drawing loads unchanged. It is
  recorded here, and in the completion report, because a persisted-struct change is exactly the kind
  of thing CLAUDE.md §3 says the Workshop does not get to make quietly. Doing it later instead would
  mean a second format migration for the same fields.

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | Show the unimplemented breakline types (Proximity/Wall/Non-destructive) and Data Clip greyed out as a roadmap signal, or omit them? REQ-084's 2026-08-18 revision added an acceptance condition that **no menu entry may be present that cannot act**, and dropped Find… for exactly this reason. | 2026-08-19 | **Pending.** Building it the REQ-084 way for now — only types that work are listed — because that is the reversible choice: adding entries later is trivial, and shipping dead ones contradicts an accepted acceptance condition. |

## 5. Assumptions
```
ASSUMPTION-1: "Refresh" on a definition node means "rebuild this surface now".
- Because:       Civil 3D's Refresh re-reads external data; GoSurvey has no external definition data
                 until REQ-086 lands, so the only honest meaning today is a forced rebuild.
- Risk if wrong: a menu item that does something adjacent to what its name suggests.
- Validate by:   the item is labelled "Rebuild now" rather than "Refresh" until REQ-086 exists.
```

## 6. Plan
- Approach: rewrite `src/ui/CadUi_Surfaces.cpp` as a tree over the existing surface API. No new
  command-layer entry points — the panel calls what `SURFACECREATE`/`DESIGNATE*`/`UNDESIGNATE`
  already call, so the panel and the command line cannot drift apart.
- Files to touch:
  - `src/commands/CadEntities.hpp` — `CadSurfaceBreakline`, `CadSurfaceBoundary::name`.
  - `src/commands/CadCommands.cpp` — 8 `breaklineIds` sites; carry description through
    `CommitDesignateAt`; report it in `SURFACELIST`.
  - `src/commands/CadCommands.hpp` — designate-command state gains the pending name/description.
  - `src/io/GsIo.cpp` — write the new form, read both.
  - `src/ui/CadUi_Surfaces.cpp` — the panel.
  - `tests/` + `tests/headless/transcripts/` — below.
- Test approach:
  - happy path = the existing REQ-069 transcript still passes unchanged (the command surface must
    not regress), plus a new `.gs` round-trip case for name/description.
  - failure mode = a **legacy `.gs`** written in the old `breaklineIds` form loads with its
    breaklines intact — the regression this format change could plausibly cause. Built as a fixture,
    not asserted by inspection.
- Steps:
  - [x] 1. Domain: the two struct changes + every use site compiles.
  - [x] 2. `.gs` write-new / read-both, with a legacy fixture test.
  - [x] 3. Panel tree + counts + stale/rebuilding indicator.
  - [x] 4. Add Boundaries / Add Breaklines dialogs, wired to the existing pick commands.
  - [x] 5. Remove + reorder on definition items.
  - [x] 6. Self-verify, transcript green, completion report.

## 7. Workflow-specific notes (Feature)
- Pre-flight answered? Yes — the five decisions in D-2026-08-19-a.
- Tests-first? Partly: the legacy-`.gs` fixture is written before the format change lands, because it
  is the one case that cannot be checked by looking at the screen.

## 8. Implementation log
- 2026-08-19 Opened. Authority is REQ-075, accepted 2026-08-12 and never built; the current panel
  says so in its own header comment ("The full Surface Manager … is REQ-075 and is not started
  here").
- 2026-08-19 Domain: `CadSurfaceBreakline {entityId, description}` replaces the bare-id array and
  `CadSurfaceBoundary` gains `name`. 12 use sites, all mechanical.
- 2026-08-19 `.gs` writes `breaklines` as objects and reads BOTH forms; migration happens on first
  save, so a file makes exactly one trip through the change.
- 2026-08-19 Fixture `samples/legacy-breaklineids.gs` built by generating a real drawing through the
  headless driver and rewriting its breaklines block by hand to the pre-REQ-075 form — a genuine old
  file rather than one hand-authored to match my own reader.
- 2026-08-19 Oracle check: disabled the legacy read branch, rebuilt -> the transcript FAILED at
  step 2 ("no log line contains: 1 breakline(s)"). Restored.
- 2026-08-19 Panel rewritten as the tree. Every mutation is DEFERRED to after the tree walk, so no
  vector is resized while the walk iterates it — the obvious way to write this crashes.

## 9. Self-verification
- [x] build-project        — PASS (clean; the one C4530 in GsIo.cpp is the pre-existing try in SaveGoSurveyFile, line-shifted by this change)
- [x] architecture-review  — PASS; the data-format change is declared in §3 and is additive + backward-compatible
- [x] code-review          — PASS
- [x] dependency-audit     — n/a
- [x] performance-review   — n/a (panel is not in the REQ-100 path)
- [x] testing              — PASS (445 ctest cases; the REQ-069 command transcript passes UNCHANGED, proving the format change did not move the command surface)

## 10. Verification result
- Submitted: 2026-08-19
- Verdict:   **PASS.** Everything checkable without a window was green (445 ctest cases), and the
             half that cannot be — ImGui layout, the context menus, the two modals — was confirmed
             by the user in the running application 2026-08-19 ("looks okay"). Recorded as their
             observation, not inferred from the build succeeding.

## 11. Outcome
- Requirements satisfied: REQ-075 — Acceptance met: yes, all five conditions.
- Tests added: `transcripts/req075-legacy-breaklineids-migrate.txt` (13 steps) +
  `samples/legacy-breaklineids.gs` fixture.
- Technical debt: none introduced. Q1 (greyed vs omitted type entries) is open and reversible.
- Docs updated: this task log.
- Done: 2026-08-19.
