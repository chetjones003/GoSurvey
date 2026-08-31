# SPEC GAP proposal — Per-viewport active UCS and UCSFOLLOW isolation (issue #155)

- Raised by:   TASK-157 (follow-up to TASK-156 / PR #176, issue #155 review)
- Date:        2026-08-31
- Status:      **ACCEPTED 2026-08-31** — D-2026-08-31-c recorded in `spec/project.md` §9; REQ-155
               added to `spec/requirements.md` as accepted; ADR-025 (e·2) added to
               `spec/architecture.md`. **Amended same day:** the per-viewport frame is a `ucs::Ucs`
               VALUE on `Viewport`, not a name into `ucsNamed` (§7 Q2 answer superseded) — a name
               cannot represent an ad-hoc UCS built while floating; named *definitions* still live
               only on `ucsNamed`. This file is kept as the decision's background record.
- Affects:     `spec/requirements.md` (new REQ-155), `spec/architecture.md` (ADR-025 (e) amendment),
               `spec/project.md` §9 (decision log entry)

---

## 1. The gap

`spec/requirements.md` §"Not in this requirement — and why" (under REQ-154 / GitHub issue #126),
item 3, defers **per-viewport UCS and UCSFOLLOW isolation**. Issue #126's acceptance list asked for
it ("UCS state does not incorrectly leak between independent viewports"; "UCSFOLLOW is handled per
viewport where applicable"; "Viewport-specific UCS behavior is supported where required"), and it is
the open body of GitHub issue #155.

Today (`src/commands/CadCommands.hpp:977-985`, `:1828-1846`) the active UCS is **one per drawing**:
`activeUcs`, `ucsPrevious`, `ucsNamed`, `namedViews`, `ucsFollow` are snapshot/restored per document
tab. That was the honest maximum while a `Viewport` was a 2D window and there was exactly one model
view per drawing.

**What changed:** REQ-061 (issue #175, TASK-156, PR #176) gave every paper-space `Viewport` its own
camera orientation. A viewport can now show an isometric while its sibling shows plan — but they
still share the drawing's single active UCS, so:

- Coordinate readout / `ID` in floating model space reports the drawing UCS regardless of which
  viewport is active.
- `UCSFOLLOW=1` re-plans "the view" — there is no notion of re-planning *only the active viewport*.
- Two viewports cannot present two different working frames, which is the issue #155 ask.

## 2. Why this is a SPEC GAP and not Workshop work

Per `workshop/workflow.md` §4, all of the following are the Workshop's to *implement*, never to
*decide*:

1. **Ownership change.** The active UCS selection moves from one owner (the document / tab) to a new
   owner (a viewport, or an "active viewport"). `architecture.md` §10.1 (single visible owner) and
   ADR-025 (e) ("the active work plane (UCS) stored on `AppCommandState`") both name the current
   owner explicitly; changing it is an architecture edit.
2. **New persisted state / on-disk format.** A per-viewport active-UCS selection is a new `.gs` key.
3. **Open scope question.** "Multiple simultaneous model-space viewports" (AutoCAD `VPORTS`) is not
   an accepted requirement. Issue #155's AC-4/AC-6 ("`UCSFOLLOW=1` re-plans only the active
   viewport"; "geometry typed in each [of two viewports] lands at the frame-implied world
   coordinates") are only fully meaningful if model space can show more than one viewport at once.

## 3. Recommended decision (the proposal)

**Scope per-viewport active UCS to the two places a viewport is already an independent view, and
defer split model space.**

| Carrier | Gets its own active UCS? | Rationale |
|---|---|---|
| Each paper-space `Viewport` | **Yes** — `Viewport` gains `activeUcsName` (empty = the drawing's current UCS) | It already carries a camera (REQ-061); floating model space (REQ-036) makes it *the* active model view, so drawing through it must resolve against its frame |
| Floating model space (REQ-036) | **Yes** — it adopts the active viewport's `activeUcsName` while entered | REQ-036 already says "model draw/edit/snap commands operate through the viewport's transform" — the UCS is part of that transform |
| The single model-space view (`activeSpaceIndex == kModelSpaceIndex`, not floating) | **No change** — keeps the drawing-scoped `activeUcs` | One model view per drawing; nothing to isolate from |
| Named UCS definitions (`ucsNamed`), named views (`namedViews`) | **Stay per drawing** | Issue #155 says so in as many words — only the *active selection* is per viewport |
| Split model space (multiple simultaneous model viewports / `VPORTS`) | **Explicitly deferred** — recorded as still-open scope, its own future decision | Not needed to satisfy issue #155 once floating model space carries the frame |

Under this decision issue #155's acceptance items map to:

- AC-1 (per-viewport camera) — **already met** (REQ-061 / TASK-156).
- AC-2 (each viewport carries its own active UCS + UCSFOLLOW) — met for paper-space viewports +
  floating model space.
- AC-3 (changing UCS in one viewport doesn't alter another's frame) — met: the selection lives on
  the `Viewport`.
- AC-4 (`UCSFOLLOW=1` re-plans only the active viewport) — met: while floating model space is
  entered, a UCS change re-plans that viewport's camera only; the drawing model view is untouched.
- AC-5 (named defs shared, only active selection per viewport) — met by design.
- AC-6 (two viewports, different UCSs, geometry typed in each lands at frame-implied world coords) —
  met by entering each viewport's floating model space in turn; a **simultaneous** two-viewport
  version is out of scope until split model space is decided, and the requirement's acceptance
  wording is written to that ("entered in turn").

---

## 4. Proposed `spec/requirements.md` edits

### 4a. New requirement

```
### REQ-155 — Per-viewport active UCS and UCSFOLLOW (GitHub issue #155)
- Purpose: a paper-space viewport (and floating model space inside it) resolves coordinate entry,
  the grid, ORTHO, the readout and UCSFOLLOW against its OWN active work plane, so two viewports on
  one sheet can present two different frames without leaking into each other or into the drawing's
  model view. Completes the per-viewport half of issue #126 / REQ-154, deferred there.
- Priority: should
- Type: functional
- Statement:
  - Each paper-space `Viewport` carries an **active UCS selection** — a name referring to a
    per-drawing named UCS, or empty for the World frame / the drawing's current elevation UCS.
    It is separate from the drawing-scoped `activeUcs`, which continues to govern the (single,
    non-floating) model-space view.
  - **Named UCS definitions and named views stay per drawing.** Only the *active selection* is
    per viewport. `World` still cannot be saved over or deleted.
  - On entering **floating model space** (REQ-036) for a viewport, that viewport's active UCS
    selection becomes the frame that draw / edit / snap / the grid / ORTHO / the coordinate
    readout / `ID` all resolve against, and the readout names which frame that is (REQ-154). On
    leaving floating model space the drawing's model view is unchanged.
  - **`UCSFOLLOW=1`**: while floating model space is entered, changing that viewport's active UCS
    re-plans **only that viewport's camera** (REQ-061) to a plan view of the new frame; no other
    viewport and not the drawing's model view is re-planned or otherwise altered. `UCSFOLLOW`
    itself remains a single per-drawing flag (0/1).
  - Changing one viewport's active UCS **leaves every stored coordinate untouched** and leaves
    every other viewport's frame, camera and displayed geometry untouched (REQ-154's
    "changing the UCS leaves every stored coordinate untouched", scoped per viewport).
  - The per-viewport active UCS selection **persists per viewport in `.gs`**, additively (no
    `kGsFormatVersion` bump): a file written before this requirement loads with every viewport
    referring to the drawing's frame (i.e. unchanged behaviour), and a file written after it that
    an older build reads simply ignores the key.
  - **Out of scope:** multiple simultaneous model-space viewports (`VPORTS`-style split model
    space). Until that is a separate accepted requirement, AC-6 below is satisfied by entering
    each viewport's floating model space in turn.
- Acceptance:
  - a layout with two viewports, viewport A's active UCS set to a rotated named UCS and viewport B
    left in World: entering A's floating model space and typing a point lands it at the world
    coordinates A's frame implies (asserted on the stored coordinate, not on entity counts);
    entering B's floating model space and typing the same relative input lands it at B's
    (World-frame) world coordinates;
  - with `UCSFOLLOW=1`, changing viewport A's active UCS while A's floating model space is entered
    re-plans A's camera to a plan of the new frame and leaves viewport B's camera, the drawing's
    model-view camera, and all stored geometry byte-identical;
  - changing viewport A's active UCS while nothing is floating changes no camera and no coordinate;
  - a pre-REQ-155 `.gs` loads with both viewports resolving against the drawing frame and renders
    identically to pre-change; a `.gs` saved after REQ-155 round-trips the per-viewport selection
    byte-identically and still carries every key an older build needs;
  - the coordinate readout inside a floating viewport names that viewport's frame (extends
    REQ-154's readout condition).
- Owner-layer: Commands (the per-viewport frame + coordinate entry + UCSFOLLOW re-plan), Renderer
  (grid in the viewport frame), UI (readout), IO (`.gs` persistence)
- Status: proposed
- Revisions: 2026-08-31 — initial. Split from REQ-154 / GitHub issue #126 as the deferred
  per-viewport item; raised by TASK-157 after REQ-061 (issue #175) removed the blocker.
```

### 4b. Edit to the "Not in this requirement — and why" list (item 3, REQ-154)

Replace item 3's closing sentences ("What is still missing is a per-viewport *active UCS* … Issue
#155 stays open on the remaining work.") with:

> Promoted to its own requirement **REQ-155** (proposed 2026-08-31, decision D-2026-08-31-c):
> per-viewport active UCS is scoped to paper-space viewports and the floating model space inside
> them, once REQ-061's per-viewport camera (issue #175) removed the blocker. Multiple simultaneous
> model-space viewports (`VPORTS` split) remain an explicitly open scope question and are **not**
> part of REQ-155.

### 4c. Verification-row table

Add a row alongside REQ-154's:

```
| REQ-155 | Commands/Renderer/UI/IO | planned — two-viewport frame-implied-coordinate test (stored
  coords, per REQ-154 style); UCSFOLLOW re-plans only the active viewport (camera bytes of the
  siblings unchanged); `.gs` round-trip + legacy load all-drawing-frame; readout names the viewport
  frame | proposed |
```

---

## 5. Proposed `spec/architecture.md` edit

Amend **ADR-025 (e)** ("Drawing resolves against an active work plane (UCS) stored on
`AppCommandState`"). Append:

> **(e·2) — Per-viewport active UCS (REQ-155, D-2026-08-31-c).** The drawing-scoped `activeUcs` on
> `AppCommandState` continues to own the frame for the single non-floating model-space view. In
> addition, each paper-space `Viewport` carries an **active UCS selection** (a name into the
> per-drawing `ucsNamed`, or empty). While **floating model space** (REQ-036) is entered for a
> viewport, that selection — resolved against the still-per-drawing `ucsNamed` — is the frame
> coordinate entry, the grid, ORTHO, the readout and `UCSFOLLOW` resolve against; `UCSFOLLOW=1`
> re-plans that viewport's REQ-061 camera only. Named UCS **definitions** and named views stay
> per drawing — one owner, `AppCommandState` (§10.1). This is a selection field on an existing
> owned type (`Viewport`, owned by `PaperLayout`), not a new abstraction (§11.4) and not a new
> owner. Persisted additively in `.gs`. **Split model space (multiple simultaneous model
> viewports) stays out of scope** — a future decision, not this one.

No `architecture.md` §11 invariant changes. §11.8 was already deleted by ADR-025.

---

## 6. Proposed `spec/project.md` §9 decision-log entry

```
| 2026-08-31 | **D-2026-08-31-c — Per-viewport active UCS + UCSFOLLOW (REQ-155, GitHub issue
#155); split model space stays deferred.** Accept REQ-155: each paper-space `Viewport` gains an
*active UCS selection* (a name into the per-drawing `ucsNamed`, empty = the drawing frame),
separate from the drawing-scoped `activeUcs` which still governs the single non-floating
model-space view. While floating model space (REQ-036) is entered for a viewport, that selection
is the frame all coordinate entry / grid / ORTHO / readout / `UCSFOLLOW` resolve against, and
`UCSFOLLOW=1` re-plans *only that viewport's* REQ-061 camera. Named UCS **definitions** and named
views stay per drawing (issue #155 says only the active selection is per viewport). Persisted
additively in `.gs` (no `kGsFormatVersion` bump); a legacy file loads with every viewport on the
drawing frame, unchanged. **Multiple simultaneous model-space viewports (`VPORTS` split model
space) are explicitly NOT in scope** — recorded as a still-open question for a separate future
decision; issue #155's AC-6 is met by entering each viewport's floating model space in turn.
ADR-025 (e) amended with clause (e·2). | Issue #126 / REQ-154 deferred per-viewport UCS because a
`Viewport` was 2D and there was one model view per drawing. REQ-061 (issue #175, 2026-08-31) gave
each viewport a camera, removing the blocker. Scoping the active UCS to paper-space viewports +
floating model space is decidable now and satisfies issue #155; a full `VPORTS` split touches the
model-view/render/pick model far more broadly and is not needed for #155, so it is kept as a
separate open question rather than bundled in. A selection field on `Viewport` (already owned by
`PaperLayout`) is not an ownership change or a new abstraction. | proposed |
```

---

## 7. Open questions for the decision-maker

| # | Question | Recommendation |
|---|----------|----------------|
| Q1 | Scope per-viewport UCS to paper-space viewports + floating model space, deferring `VPORTS` split model space? | **Yes** — it fully satisfies issue #155 and is decidable today. |
| Q2 | Store the per-viewport frame as a **name** into `ucsNamed` (proposed) or as an inline `ucs::Ucs` value on the `Viewport`? | **Name.** Keeps definitions single-owned (issue #155's explicit ask); avoids a copy of the frame drifting from its definition. Empty string = the drawing frame. |
| Q3 | Should `UCSFOLLOW` become a per-viewport flag too, or stay one per-drawing 0/1? | **Stay one per-drawing flag**; only its *effect* is scoped to the active viewport. Matches AutoCAD (`UCSFOLLOW` is a sysvar with a per-viewport bit, but a single-flag approximation is enough here and simpler — CLAUDE.md rule 1). Revisit if a user asks. |
| Q4 | Does the drawing's non-floating model view ever adopt a viewport's frame? | **No.** It keeps `activeUcs`. Isolation is the whole point. |

Once decided and recorded, TASK-157 moves from `blocked` to `plan` and cites REQ-155.
