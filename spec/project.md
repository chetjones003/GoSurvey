# Project Specification

> **Template.** This is the top of the source-of-truth tree. It answers *what*
> this project is, *who* it is for, and *what rules* bind every decision below
> it. Fill the placeholders (`<…>`), delete guidance you don't need, and keep it
> short — if this document grows past a few pages, push detail down into
> `requirements.md`, `architecture.md`, or `roadmap.md`.

---

## 1. Identity

| Field | Value |
|-------|-------|
| **Name** | `<ProjectName>` |
| **One-line description** | `<e.g. A desktop CAD editor for land-survey traverse computation>` |
| **Primary language(s)** | `<C++ / Rust / Zig / Go>` |
| **Platform target(s)** | `<Windows 11 (MSVC) / Linux / macOS>` |
| **Rendering / graphics** | `<OpenGL 4.x / Vulkan / none>` |
| **Domain** | `<CAD / survey / tooling / simulation>` |
| **License** | `<MIT / proprietary / …>` |
| **Status** | `<prototype / alpha / production>` |

## 2. Purpose

> Two short paragraphs, maximum. Why does this exist? What does the world look
> like once it works? Avoid feature lists here — those live in `requirements.md`.

**Problem.** `<What concrete problem are we solving, for whom?>`

**Outcome.** `<What does success look like, in the user's terms?>`

## 3. Users and use cases

> Name the real users. A "user" with no concrete workflow is a guess, and guesses
> drive over-engineering.

| User | Goal | Primary workflow |
|------|------|------------------|
| `<Surveyor>` | `<Compute a closed traverse>` | `<Import FBK → review → adjust → export>` |
| `<…>` | `<…>` | `<…>` |

## 4. Scope

State scope as two explicit lists. The **out-of-scope** list is the more
important of the two — it is the project's primary defense against drift.

### In scope (today)
- `<concrete capability 1>`
- `<concrete capability 2>`

### Out of scope (explicitly not building)
- `<capability we will NOT build, and why>`
- `<predicted-future feature we are deliberately deferring>`

> **Principle — solve today's problem.** Architecture emerges from real
> requirements, not speculation. If something is "for later," it belongs on the
> roadmap, not in the codebase.

## 5. Non-negotiable principles

These bind every requirement, design, and review. They are listed here so the
rest of the spec can reference them by name.

1. **Minimal abstractions.** No interface, template, generic, or layer of
   indirection without two or more concrete, present-day call sites that need
   it. A `class CommandManager` beats an `ICommandFactory`/`ICommandRegistry`
   tower every time.
2. **Strong, explicit ownership.** Every resource has exactly one owner, visible
   in the type. Prefer `unique_ptr`/value semantics (C++), single-owner moves
   (Rust), `defer`-based cleanup (Zig/Go). Shared ownership is a justified
   exception, not a default.
3. **Clear layering.** Dependencies flow downward only. Lower layers never know
   about higher ones.
4. **Data-oriented thinking.** Design around the data and how it is transformed,
   not around taxonomies of objects. Lay out data for the access pattern that
   dominates.
5. **Performance awareness.** Know the cost of what you write; measure before you
   optimize; never pessimize a hot path casually.
6. **Readability over cleverness.** Code is read far more than written. The
   simplest version that is fast enough wins.

> These mirror a Cherno-style philosophy: small, explicit, fast, and honest about
> what the machine is doing.

## 6. Quality bar

> The observable definition of "good enough to ship." Keep it measurable.

- **Correctness:** `<domain results match reference within <tolerance>>`
- **Stability:** `<no crash on the core workflows; malformed input is rejected, not absorbed>`
- **Performance:** `<frame budget / startup time / import time targets>`
- **Maintainability:** `<new code passes coding-standards.md without new unjustified abstraction>`

## 7. Constraints

> Hard limits that bound every solution. Cross-reference, don't duplicate, the
> architecture and standards docs.

- **Platform:** `<Windows 11, MSVC toolchain>`
- **Dependencies:** `<dependency policy — see §below>`
- **Build:** `<reproducible, deterministic; artifacts to /build, not the source tree>`
- **Performance budget:** `<concrete numbers>`

### Dependency policy

Before adding a third-party dependency, answer all three:

1. Can this be implemented simply in-tree?
2. Is the dependency actively maintained and worth the build-time cost?
3. Does it solve a problem we have **today**?

If any answer is "no," don't add it. Prefer a small, well-understood standard
library and a handful of vetted libraries over a deep dependency graph.

## 8. Glossary

> Define domain terms once, here, so requirements can use them precisely.

| Term | Meaning |
|------|---------|
| `<Traverse>` | `<A connected series of survey measurements>` |
| `<Closure>` | `<The error between a traverse's computed and known endpoints>` |
| `<…>` | `<…>` |

## 9. Decision log

> The most important section for preventing drift. Record *why* significant
> choices were made, so they are not silently reversed later. One line per
> decision; link out for detail.

| Date | Decision | Rationale | Status |
|------|----------|-----------|--------|
| `<2026-06-10>` | `<Use OpenGL 4.5, not Vulkan>` | `<Faster to ship; team familiarity; portability sufficient>` | accepted |
| 2026-06-10 | Accept REQ-010–017 (Traverse Editor raw-measurement display + least-squares closure analysis) | Extends FEAT-002; gives surveyors full observation review and rigorous adjustment with residuals | accepted |
| 2026-06-10 | ADR-001 — in-tree least-squares adjustment module, no third-party linear-algebra dependency | Closed-loop systems are tiny; a small in-tree solver satisfies REQ-016 without violating the REQ-300 dependency policy | accepted |
| 2026-06-10 | Add Catch2 (v3, via FetchContent) as a test-only dependency (REQ-300) | (1) An assert harness is possible in-tree but a framework gives proper numeric-tolerance assertions and reporting; (2) Catch2 is actively maintained and widely used; (3) the accepted REQ-011/012/015/016 require committed regression tests and no harness exists today. Test-only — not linked into GoSurvey. | accepted |
| 2026-06-10 | ADR-002 — separate `GoSurveyTests` executable + ctest for domain regression tests | Establishes the project's first test target without coupling tests to the GUI executable | accepted |
| 2026-06-11 | Accept REQ-018 (editable per-leg observation sets) and amend REQ-013 (view-only scoped to the collapsed summary; editing via the expander) | User wants to expand a leg to add/edit observations and have the traverse re-derive; protection intent preserved (editing requires deliberate expand) | accepted |
| 2026-06-11 | ADR-003 — store backsight reading on the leg + one `ReduceLegFromSets` reduction shared by import and edit | Edited sets need the backsight reference to reduce; keeps literal circle readings (REQ-010) and avoids silent no-op edits (REQ-201) | accepted |
| 2026-06-11 | Accept REQ-020/021/022 (UNITS command + Drawing Units dialog; configurable angle display; insertion scale stored + settings persisted) | User wants an AutoCAD-style UNITS dialog owning display precision and angle/bearing format; follow-up to the display-precision unification. Display-only scope keeps storage/compute and angle entry unchanged | accepted |
| 2026-06-11 | ADR-004 — configurable angle DISPLAY via a pure `AngleFormat` module; angle input convention unchanged | One tested seam for angle formatting reused by every readout (§11.4); preserves the CW-from-north convention for entry/geometry and REQ-101 fidelity; input-side ANGBASE/ANGDIR deferred | accepted |
| 2026-06-12 | Amend REQ-022 — drawing unit is an AutoCAD INSUNITS-style relabel (Unitless/Feet/Meters), persisted in `.gs` and the DXF `$INSUNITS` header; never scales geometry; import adopts `$INSUNITS` without scaling coordinates | User chose AutoCAD-faithful relabel over destructive ft↔m conversion (which would break SPCS state-plane georeferencing); `$INSUNITS` is an additive header var, so DXF coordinate round-trip stays 1:1 (REQ-002). DWG `$INSUNITS` cannot distinguish US-survey vs international foot, so only Feet/Meters/Unitless are offered | accepted |
| 2026-06-12 | Accept REQ-023 (survey points survive a DXF round-trip) + ADR-005 (identity via a registered `GOSURVEY` XDATA schema on POINT/label MTEXT) | Resolves issue #37 (export POINT → import 4 cross-lines lost all survey points); XDATA keeps third-party POINT reading valid while restoring full identity for GoSurvey; contained to DxfIo with no new dependency | accepted |
| 2026-06-12 | Accept REQ-024 (AutoCAD-style two-field live-coordinate dynamic input for point prompts) | User wants the cursor input to match AutoCAD: prompt + live X/Y boxes at displayLinearPrecision, type-to-lock, Tab between fields, Enter/click to commit, no Send button; single field kept for non-point prompts. UI-only, owned by the UI layer (no new abstraction/dependency) | accepted |
| 2026-06-15 | Amend REQ-023 — DXF import preserves existing session survey points (replaces only CAD geometry) and merges the DXF's reconstructed points; id conflicts prompt overwrite-vs-offset | Original "import replaces the drawing" silently wiped CSV-imported points when a DXF was imported after; preserving + merging makes points-then-DXF and DXF-then-points behave the same. Civil 3D COGO points are custom objects absent from standard DXF, so CSV remains the survey-point interchange; DXF round-trips GoSurvey's own XDATA points. Contained to DxfIo + a small conflict modal; no new dependency | accepted |

> **Change protocol:** any change to this file, `requirements.md`, or
> `architecture.md` is a deliberate decision recorded here — never a quiet edit
> to rationalize code that already exists.
