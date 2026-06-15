# Roadmap

> **Template.** The roadmap is where *future* work lives so it stays out of the
> codebase and out of the architecture until it is real. It sequences accepted
> requirements into shippable increments and records what is deliberately
> deferred. The roadmap describes *intent and order*; it is not a contract or a
> dated promise.

---

## How to use this document

- The roadmap pulls from `requirements.md`. A roadmap item should reference the
  `REQ-NNN`(s) it delivers. No requirement, no roadmap item — features are not
  invented here.
- Sequence by **value and dependency**, not by what is most fun to build.
- Keep "Now" small. A long in-flight list is a sign of unfinished work, not
  progress.
- When something moves from "Later" to "Now," that is a deliberate decision —
  record it in `project.md`'s decision log if it changes scope.

> **Anti-speculation rule.** Items in "Later" and "Someday" must not influence
> today's architecture. Build for the increment in front of you; let the design
> emerge from real requirements (see `project.md` §5).

---

## Milestones

State each milestone as an outcome a user would notice, with an explicit "done
when" and the requirements it closes.

### M1 — `<Walking skeleton>`
- **Goal:** `<End-to-end thinnest slice: import → display → export>`
- **Delivers:** REQ-001, REQ-200
- **Done when:** `<a real file imports, renders in the viewport, and exports back within tolerance>`
- **Status:** `<in progress / done>`

### M2 — `<Core domain correctness>`
- **Goal:** `<Computations match reference data>`
- **Delivers:** REQ-101
- **Done when:** `<the regression dataset passes at the stated tolerance>`
- **Status:** `<planned>`

### M3 — `<Interactive performance>`
- **Goal:** `<Smooth editing at target scene size>`
- **Delivers:** REQ-100
- **Done when:** `<benchmark scene holds the frame budget on reference hardware>`
- **Status:** `<planned>`

> Add milestones until the in-scope list in `project.md` is covered. Stop there.

---

## Now / Next / Later

A lightweight board that complements the milestones. Keep each column honest.

### Now (in flight — keep short)
- **Paper Space — Increment 1** (REQ-025, REQ-026, REQ-031 partial): model/paper
  spaces, layout tabs, MODEL/PAPER status toggle, paper size + orientation + sheet
  outline, `.gs` persistence of layouts.

### M-PaperSpace — Paper space & plotting (incremental)
- **Goal:** compose the model onto sheets and plot them.
- **Delivers:** REQ-025–031 (ADR-006, ADR-007).
- **Increments:**
  1. Spaces + layout tabs + MODEL/PAPER toggle + paper size/orientation + outline + `.gs` (REQ-025, REQ-026, REQ-031 part).
  2. Viewports: create/move/resize, independent scale/center, model rendered inside (REQ-027, REQ-031).
  3. Per-viewport layer freeze (REQ-028).
  4. Plot single + batch to PDF via PDFium (REQ-029, REQ-030).
- **Deferred:** DXF persistence of layouts/viewports; direct-to-OS-printer.
- **Status:** Increment 1 in progress.

### Next (accepted, sequenced, not started)
- `<REQ-101 — coordinate tolerance regression>`
- `<REQ-100 — frame-budget benchmark>`

### Later (real but deferred)
- `<Second file format>` — deferred until a user actually needs it.
- `<Undo/redo system>` — design only once edit operations stabilize.

### Someday / maybe (explicitly speculative — do NOT design for these yet)
- `<Pluggable rendering backend (Vulkan)>` — single backend until a second is a
  genuine requirement; revisit then.
- `<Plugin/scripting API>`

---

## Dependencies and sequencing

> Make ordering forced by dependency explicit, so no one starts a downstream item
> prematurely.

```
M1 walking skeleton
   └─> M2 domain correctness        (needs the import/model path from M1)
          └─> M3 interactive perf   (only meaningful once correctness holds)
```

- `<Item B>` cannot start before `<Item A>` because `<reason>`.

---

## Risks and unknowns

> Name what could invalidate the plan. A roadmap that lists no risks is hiding
> them.

| Risk | Impact | Mitigation / spike |
|------|--------|--------------------|
| `<Reference data unavailable for tolerance test>` | blocks M2 verification | `<obtain dataset / build synthetic reference>` |
| `<GL driver variance across target GPUs>` | perf budget unmet on some HW | `<define reference hardware; spike early>` |
| `<…>` | `<…>` | `<…>` |

---

## Explicitly out of roadmap

> Mirror `project.md` §4's out-of-scope list here so contributors see it while
> planning. These are not "Later" — they are decisions not to build.

- `<capability we are not building, and why>`

---

## Changelog of the plan

> The roadmap itself changes. Record significant re-sequencing so history is
> legible — what moved, when, and why.

| Date | Change | Reason |
|------|--------|--------|
| `<2026-06-10>` | `<Initial roadmap>` | `<—>` |
| `<…>` | `<Moved X from Later to Now>` | `<user need materialized>` |
