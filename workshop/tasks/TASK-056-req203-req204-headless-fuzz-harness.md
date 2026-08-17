# TASK-056 — Headless domain target, transcript driver, and parser fuzzing

- Type:    testing
- Status:  **implement** (SPEC GAP raised and resolved 2026-08-16, before any code)
- Opened:  2026-08-16
- Owner:   Workshop

## 1. Authority

- Goal:         maintainability / debuggability (project.md §6 quality bar)
- Requirements: REQ-203 (accepted 2026-08-16), REQ-204 (accepted 2026-08-16)
- Constraints:  CON-07 (artifacts to the build directory, never the source tree),
                REQ-200 (deterministic build), REQ-300 (dependency discipline — test-only deps),
                REQ-301 (no abstraction without ≥2 present-day uses)
- Owning subsystem: Build/Platform (the target), Commands (the driven entry points),
                util (`docinvariants`, pure)

### Acceptance (restated verbatim from spec/requirements.md)

**REQ-203**
- the headless target links with no imgui, glfw, GLEW, or `gl*` symbol on its link line — proven by
  the link line, not by inspection;  ← **this condition is what the SPEC GAP invalidates**
- a transcript drawing a line, a circle, and a polyline yields exactly what a user performing the
  same steps yields, compared by saving `.gs` and diffing;
- a transcript step that reaches a file dialog is answered from the transcript and never blocks;
- a failing run exits non-zero naming the failure, the step index, and the transcript line;
- the same transcript run twice produces byte-identical output;
- the transcript corpus runs in CI on every push and a non-zero exit fails the build (REQ-202).

**REQ-204**
- the same `--seed N` twice produces an identical transcript and an identical result;
- each listed invariant has a fixture that deliberately breaks it and proves the check fires;
- a failing run emits a minimized transcript that reproduces the failure standalone under the
  REQ-203 driver;
- minimization terminates, is bounded in attempts, and reports its reduction ratio;
- a clean run over a seed range exits zero and prints nothing but a summary;
- the generator is TEST-ONLY: the shipped `GoSurvey.exe` neither links nor contains it (REQ-300).

## 2. Scope

- In scope: a second CMake target linking the domain sources; link-time platform seams; the
  transcript driver; `util/docinvariants`; the parser fuzzer and its corpus.
- Out of scope: the triage / dedupe / `gh issue create` automation (tooling, not product —
  `docs/fuzz-harness.md` §6); coverage-guided fuzzing; any change to shipped application behaviour.
- Smallest change: **nothing yet — the task is blocked.** See §3.

## 3. Architectural boundary check  (workflow.md §4)

- Does this need a NEW abstraction / layer / dependency / ownership change / global state /
  public-API or data-format change / algorithm the spec didn't specify?
    - [ ] No — proceed.
    - [x] **Yes → STOP.** Escalated as SPEC GAP, Status set to `blocked`. Recorded below.

### The measurement that raised it

ADR-031's Context was derived from `grep`. Re-deriving it from `dumpbin /SYMBOLS` on the objects in
`build/CMakeFiles/GoSurvey.dir/**` contradicted it twice, both times optimistically:

| Claim in ADR-031 | Measured truth |
|---|---|
| `DxfIo.cpp` calls 1 symbol from `CadCommands.cpp`; `GsIo.cpp` calls 0 | **8** and **9** — neither parser is free-standing |
| `LoadApplicationFont` is the codebase's one upward dependency | `SurveyPoints.cpp:666` also calls `ImGui::GetFont()` |
| *(unstated)* | `CadCommands.cpp` needs 3 `PdfAttach` symbols, and `PdfAttach.cpp` includes `<GL/glew.h>` and calls `glGenTextures`/`glDeleteTextures` |
| *(unstated, favourable)* | Only **1** of the 11 `WinFileDialogs` functions is reachable from `CadCommands.cpp` |

Full closure of `CadCommands.cpp.obj` (undefined externals, CRT and `std::` removed): `geom2d`
(6 symbols), `SurveyPoints` (5), `PdfAttach` (3), ImGui (3, all from `LoadApplicationFont`),
`CadCoordinateFrame` (2), `benchscene` (4), `FontReg::SetDefault`, `CadSnap`,
`FindDwgConverter`, `BuildTin`, `meshgeom::ComputeBounds`, `gltf::ImportGltfFile`,
`stl::ImportStlFile`, `dwgmesh::*` (2), `CadCanonicalLinetypeNameForDxf`,
`BrowseOpenFileGltfUtf8`.

### The gap

**Text metrics are an input to stored geometry.** `RepositionSurveyLabelMtextForPoint`
(`SurveyPoints.cpp:666`) sizes a survey point's label box by asking ImGui for the current font and
measuring the text through `MtextRichNaturalContentPx(font, …)`, then writes
`boxMinX/boxMaxX/boxMinY/boxMaxY` onto the annotation. `GsIo.cpp`'s load path calls
`RepositionAllSurveyPointLabels` on every open (2 call sites), and `CadCommands.cpp` calls into that
family from 6 more.

So **opening a `.gs` file requires a live ImGui context with a built font atlas.** Consequences:

1. ADR-031 (c) is wrong in its premise *and its direction*. It moves font loading **up** to the UI
   layer on the grounds that fonts are a UI concern. But if metrics decide stored geometry, font
   availability is a **domain** concern, and moving it up puts it further from the layer that needs it.
2. REQ-203's first acceptance condition ("links with no imgui") cannot be met while `.gs` loading
   depends on a font, so the linker cannot be the enforcer that ADR-031 (a) relies on.
3. There is a **fidelity** requirement hiding here that no spec text states: if the headless build
   measures text with a *different* font from the GUI build, stored label geometry differs, and
   REQ-203's "a transcript yields exactly what a user performing the same steps yields, compared by
   saving `.gs` and diffing" fails for any drawing containing a survey point label — not because of
   a bug, but because the two builds disagree about a font.

This is an architectural decision (layer ownership of font/text metrics), which `CLAUDE.md` puts
outside the Workshop's authority. Escalated rather than assumed.

### Proposed resolutions (for the recorded decision — Workshop does not choose)

- **(A) Link ImGui core into the headless target and load the same font.** `imgui.cpp`,
  `imgui_draw.cpp`, `imgui_widgets.cpp`, `imgui_tables.cpp` — **no backends**, no GLFW, no GL. The
  atlas builds to a CPU bitmap; only *uploading* it needs a GPU, and headless never uploads.
  `CreateContext` + `AddFontFromFileTTF(tahoma)` + `Build()` + `NewFrame()` with a dummy
  `DisplaySize` gives `ImGui::GetFont()` a real font, so headless measures text through **the same
  code path and the same font** as the GUI — which is what makes the REQ-203 diff condition
  meaningful. ADR-031 (c) is reversed: `LoadApplicationFont` stays in the domain layer.
  REQ-203's acceptance condition is amended to "no glfw, no GLEW, no `gl*`, and no ImGui *backend*".
  *Cost:* the linker no longer proves "no imgui", weakening ADR-031 (a)'s enforcement argument to
  "no window, no GPU" — still enough for CI, and still a real boundary.
- **(B) Introduce a text-measurement seam** the domain layer calls, implemented by ImGui in the app
  and by a stub headless. *Cost:* a new abstraction — REQ-301 demands ≥2 present-day concrete uses,
  and a stub is not a use. Worse, a stub that measures differently silently changes stored geometry,
  turning the fidelity problem from visible into invisible.
- **(C) Make label box sizing independent of font metrics** (e.g. store the text extent at edit time
  and reuse it on load). *Cost:* a data-format change (REQ-079 migration) and a behaviour change to
  the shipped app, to serve a test harness — the tail wagging the dog.

**Workshop's recommendation: (A).** It is the only option that changes no shipped behaviour, adds no
abstraction, and keeps headless and GUI measuring text identically — which the REQ-203 acceptance
condition silently requires. The honest cost is that one acceptance condition must be reworded, and
(A) is the option that makes the reworded condition *true* rather than aspirational.

## 4. Questions

| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | Resolve the SPEC GAP above: (A) link ImGui core headless and reverse ADR-031 (c), (B) a text-measurement seam, or (C) remove the font dependency from label sizing? | 2026-08-16 | **(A)** — user decision, 2026-08-16. Recorded in the decision log; ADR-031 amended (b′)/(c′); REQ-203 acceptance condition 1 reworded. Task unblocked. |

## 5. Assumptions

```
ASSUMPTION-1: ImGui can build a font atlas and serve ImGui::GetFont() with no GL context
              and no backend, given CreateContext + io.DisplaySize + io.DeltaTime + NewFrame().
- Because:       the headless target must measure text to load a .gs (see §3), and option (A) rests on it.
- Risk if wrong: option (A) is not viable and the gap must resolve to (B) or (C).
- Validate by:   a throwaway console program linking imgui core only, before any target work.
- STATUS:        **VALIDATED 2026-08-16** — run before asking Q1, so the recommendation is
                 evidence-backed rather than asserted.
```

**ASSUMPTION-1 evidence.** A throwaway console program (`probe_imgui_headless.cpp`, scratchpad)
compiled against `imgui.cpp`/`imgui_draw.cpp`/`imgui_widgets.cpp`/`imgui_tables.cpp` **only** — no
backends, no GLFW, no GLEW — loading the same `C:/Windows/Fonts/tahoma.ttf` at 16 px that
`LoadApplicationFont` loads:

```
atlas built: 512x128, pixels=non-null, IsBuilt=1
ImGui::GetFont() -> non-null
CalcTextSizeA("PT 101  EL 1234.56  IRON ROD") -> 181.000 x 16.000 px
ImGui::CalcTextSize -> 181.000 x 16.000 px
PASS: imgui measures text headless, no GL context, no window, no backend
```

`dumpbin /DEPENDENTS` on the resulting binary lists `USER32 / KERNEL32 / SHELL32 / IMM32` and
**no `opengl32.dll`, no GLFW, no GLEW** — which is exactly the amended REQ-203 acceptance condition
option (A) proposes, demonstrated rather than promised. Building the atlas is CPU work; only
*uploading* it needs a GPU, and headless never uploads.

## 6. Plan  (pending the §4 answer — do not start)

- Approach: build the shared domain link surface **once**, then hang both consumers off it. The
  corrected measurement collapses the old two-stage split: the parser fuzzer and the transcript
  driver need the same sources linked, so staging them apart buys nothing.
- Files/functions to touch (provisional, assuming resolution (A)):
  - `CMakeLists.txt` — `GOSURVEY_DOMAIN_SOURCES` list shared by `GoSurvey` and `gosurvey_headless`;
    an `imgui_core` target (no backends) split out of the existing `imgui_backend`.
  - `src/platform/HeadlessFileDialogs.cpp` — second implementation of `WinFileDialogs.hpp` (1 of the
    11 functions is actually reachable; the rest return `false`).
  - `src/platform/HeadlessPdfAttach.cpp` — second implementation of the 3 reachable `PdfAttach.hpp`
    symbols, keeping `<GL/glew.h>` out of the headless link.
  - `src/util/docinvariants.{hpp,cpp}` — the REQ-204 oracle set, pure.
  - `tests/headless/` — driver `main`, transcript parser, generator, minimizer.
- Test approach: happy path = a hand-written transcript round-trips a `.gs`; failure mode = a
  fixture per invariant that deliberately breaks it and proves the check fires (REQ-204 acceptance).
- Steps:
  - [ ] **Q1 answered and recorded in the decision log** ← gate; nothing below starts first
  - [ ] validate ASSUMPTION-1 with a throwaway imgui-core-only console program
  - [ ] amend ADR-031 (b)/(c) + REQ-203 acceptance per the recorded decision
  - [ ] `GOSURVEY_DOMAIN_SOURCES` + `gosurvey_headless` target links clean
  - [ ] platform seams (`WinFileDialogs`, `PdfAttach`)
  - [ ] `util/docinvariants` + one deliberately-broken fixture per invariant
  - [ ] transcript format + driver + the REQ-203 diff test
  - [ ] parser fuzzer + corpus + minimizer
  - [ ] CI wiring (REQ-202)

## 7. Workflow-specific notes

- Testing: each check maps to a REQ (see the REQ-204 invariant table); tolerance-based comparisons
  follow REQ-101; every invariant must fail against deliberately-broken input before it is trusted.

## 8. Implementation log

- 2026-08-16 — opened. Spec (REQ-203, REQ-204, ADR-031) accepted and recorded in the decision log.
- 2026-08-16 — toolchain verified: VS 18 Community, `vcvars64.bat`, Ninja + MSVC; baseline
  `GoSurveyTests` build is clean ("ninja: no work to do").
- 2026-08-16 — link surface re-measured with `dumpbin /SYMBOLS` rather than `grep`. Two ADR-031
  claims contradicted; corrections written into `spec/architecture.md` (amendment banner),
  `spec/project.md` (decision log) and `docs/fuzz-harness.md` §1/§1.1.
- 2026-08-16 — **blocked: SPEC GAP** on font/text-metric layer ownership (§3). No code written.
- 2026-08-16 — ASSUMPTION-1 validated ahead of the gate (it does not depend on the Q1 answer, and it
  is what decides whether the recommended resolution is real): imgui core measures Tahoma text with
  no GL, no window and no backend; the probe binary imports no `opengl32.dll`. Recorded in §5.
- 2026-08-16 — Q1 answered **(A)**. Decision recorded in the log; ADR-031 amended (b′)/(c′);
  REQ-203 acceptance condition 1 reworded. Status → implement.
- 2026-08-16 — CMake restructured: `imgui_core` split from `imgui_backend` (backends now link core,
  so `GoSurvey`'s link line is unchanged); `GOSURVEY_DOMAIN_SOURCES` shared by both executables.
  `GoSurvey.exe` rebuilt green before the new target was added, to isolate the restructure.
- 2026-08-16 — **Within-boundary decision (not escalated):** `AppExecutableDirectory`,
  `UserDataDirectory`, `ResolveBundledAssetPath`, `ResolveAppLogoPngPath` and
  `ResolveDefaultWorkspaceTemplateGsPath` moved from `platform/AppIcon.cpp` to a new
  `platform/AppPaths.{hpp,cpp}`, verbatim. Found by the linker: `AppIcon.cpp` includes `<GL/glew.h>`
  and `<GLFW/glfw3.h>`, so resolving `%APPDATA%` transitively required a GPU. Judged **not**
  architectural — no new abstraction, layer, dependency, ownership change or public-API change; it
  is a file split inside the owning subsystem (Platform → Platform), and `AppIcon.hpp` includes the
  new header so no caller changed. Same shape as ADR-022's precedent. Deliberately **not** a third
  headless seam: unlike the file-dialog and PdfAttach functions, these behave identically with and
  without a desktop, so a second implementation would duplicate real logic and let the copies drift.
- 2026-08-16 — `gosurvey_headless` links. REQ-203 acceptance condition 1 **verified by measurement**:
  `dumpbin /DEPENDENTS` lists only SHELL32 / KERNEL32 / USER32 / IMM32 / MSVCP140 / VCRUNTIME / CRT
  — **no `opengl32.dll`, no glfw, no GLEW** — and `dumpbin /IMPORTS` matches no `gl*`/`glfw`/`wgl`
  symbol.
- 2026-08-16 — **The driver caught three of my own wrong assumptions before it caught any GoSurvey
  bug**, which is the harness working: (1) bare Enter does not end LINE, it ends the chain and
  restarts the command (`CadCommands.cpp:11904`) — ESC exits; (2) commands stay active and repeat
  after committing, so a following `CMD <name>` is eaten as a point (a live CIRCLE swallowed
  `POLYLINE` and turned its vertices into two extra circles); (3) `userPolylineOffsets` is **CSR**
  (N+1 offsets for N polylines), so the first `attr-counts` invariant reported a violation on every
  valid polyline. (3) is the important one: a false-positive oracle is worse than no oracle, and it
  is exactly the failure mode REQ-204's deliberately-broken-fixture condition exists to prevent.
- 2026-08-16 — `util/docinvariants` + `tests/DocInvariantsTests.cpp`: 28 cases / 31 assertions, all
  green. Each of the 8 invariants has a fixture that breaks it and proves it fires, plus negative
  fixtures pinning the cases that must **not** fire (unassigned id 0, a 1e12 state-plane coordinate,
  a survey label id that resolves to nothing per REQ-076).
- 2026-08-16 — **First real finding, on the first run of the `gs-roundtrip` oracle.** A `TEXT` entity
  is saved to `.gs` with `id: 0`. Root cause: the TEXT commit path (`CadCommands.cpp:12475-12492`,
  both model and paper branches) never calls `BumpCadGpuCache(st)`, so the `EnsureEntityIds` sweep
  early-out at `:1348` returns on a false premise and the annotation keeps its unassigned id. The
  DIM* sibling paths (`:4943`, `:5005`) do bump. Minimized to an 8-line transcript and filed as
  **[#56](https://github.com/chetjones003/GoSurvey/issues/56)** (`fuzz` / `bug` / `sev:corrupt`,
  dedupe signature `3a8a5d6fe776`). Not fixed here — a bug is its own task with its own authority.
- 2026-08-16 — full suite green: **369/369 ctest cases pass** (367 Catch2 + 2 transcripts), with
  `headless.gs-roundtrip.compare` DISABLED against #56 rather than deleted or `WILL_FAIL`ed.

### Generator + minimizer (REQ-204 remainder)

- 2026-08-16 — generator and minimizer built as **pure** modules (`tests/headless/FuzzGenerator.*`,
  `tests/headless/Minimizer.*`): the generator takes its command list as a parameter, the minimizer
  takes a predicate. That is what makes both unit-testable with no application linked, and it also
  sidesteps a public-API change — `kRegistry` is in an anonymous namespace, and exposing it would be
  architectural. `FuzzMain` enumerates the 53 commands through the already-public
  `FuzzyCommandSuggestions`, unioning single-letter queries a–z.
- 2026-08-16 — **candidates run as subprocesses**, not in-process. A crash is the outcome most worth
  finding and would otherwise take the minimizer and the remaining seeds with it; `RunProcessAndWait`
  also gives hang detection (REQ-204) for free via its existing timeout.
- 2026-08-16 — **measured, then fixed, a generator that was doing almost nothing.** A 50-line
  transcript produced ONE entity: modify commands are no-ops with nothing selected, so the run
  bounced off command after command while reporting "0 failures" — indistinguishable from success.
  Added a fixed prelude that builds a base drawing, and aimed two thirds of picks at that geometry.
  This is the finding I would most easily have missed by trusting the pass rate.
- 2026-08-16 — **the minimizer produced a confident lie, and it was caught by reading the output.**
  Candidates shared one `%OUT%` directory, so a candidate that dropped the `SAVEAS` lines still found
  the previous candidate's files on disk and still "failed": a 116-line transcript reduced to
  `NEW` + `EXPECT SAMEFILE …`, reported as 98% reduction, describing no bug at all. Fixed by giving
  every candidate an empty output directory. Recorded in `docs/fuzz-harness.md` §8 because the
  lesson generalises: **state leaking between candidates is how a minimizer lies**.
- 2026-08-16 — signature strengthened from `reason` to `reason|kind`. As first written, every
  `EXPECT` failure shared the signature `expect`, so a second unrelated defect in the same run would
  have been silently dropped as a duplicate — a fuzzer losing findings without saying so.
- 2026-08-16 — `EXPECT SAMEFILE <a> <b>` added to the driver, which makes the `gs-roundtrip` oracle
  expressible in a transcript (and therefore generatable). Round-trip generation is opt-in
  (`--roundtrip`) precisely because it currently fails for any drawing containing TEXT (#56), and
  leaving it on would bury every new finding under the same known one.
- 2026-08-16 — `tests/FuzzHarnessTests.cpp`: 13 cases / 6148 assertions. Covers REQ-204's remaining
  acceptance conditions — same seed → identical transcript; different seeds → different transcripts;
  denied commands never emitted; minimization terminates, respects its attempt cap, preserves order,
  keeps every line a conjunctive failure needs, refuses a non-failing input, and reports an honest
  ratio.
- 2026-08-16 — **fuzz sweep: 5000 seeds in 2m16s, 3 failures, 2 distinct signatures.** Both minimized
  automatically and verified to reproduce standalone, then filed:
  **[#58](https://github.com/chetjones003/GoSurvey/issues/58)** — OFFSET gives the new entity the
  source's `id` (all five `CommitOffset*`; the clipboard already solves this with
  `ClearEntityIdsFrom`, OFFSET never got it), 155 → 9 lines; and
  **[#59](https://github.com/chetjones003/GoSurvey/issues/59)** — CIRCLE stores an infinite radius
  when the centre-to-pick distance overflows `float`, 169 → 4 lines. The `--roundtrip` sweep filed
  **[#57](https://github.com/chetjones003/GoSurvey/issues/57)** (empty drawing fails resave
  idempotence), 116 → 5 lines.
- 2026-08-16 — `headless.fuzz-smoke` (seeds 1..12) registered with ctest as a **canary for the
  harness**, not a bug hunt: if generation stops producing commands or the runner breaks, it goes red
  instead of the fuzzer reporting "0 failures" forever.
- 2026-08-16 — full suite green: **383/383 ctest cases pass**.

## 9. Self-verification

- [x] build-project        — PASS (`GoSurvey.exe`, `gosurvey_headless.exe`, `GoSurveyTests.exe` all
      build clean; no new warnings in the files this task touched)
- [x] architecture-review  — PASS. No upward dependency added; the one layering *improvement*
      (AppPaths) is recorded in §8 as a within-boundary file split. No new abstraction:
      `util/docinvariants` has two present-day concrete uses on the day it was written
      (§11.4), and both platform seams are second implementations of existing headers, not new
      interfaces. No new global mutable state except the headless dialog queue, which is confined to
      the test-only translation unit and never linked into `GoSurvey.exe`.
- [x] code-review          — PASS. The `PolylineCount` CSR bug was found and fixed before it could
      produce a false finding; both copies (invariants and driver) carry the same note so they
      cannot drift apart silently.
- [x] dependency-audit     — PASS. **No new third-party dependency.** ImGui was already a
      dependency; this task only splits the existing target so the backends can be omitted. The
      headless and fuzz targets are TEST-ONLY in the REQ-300 sense, exactly as Catch2 is.
- [x] performance-review   — n/a. Nothing on a measured hot path changed. `CheckDocumentInvariants`
      runs per transcript step in a test binary only, and is never linked into the application.
- [x] testing              — PASS. 369/369 ctest cases green. Happy path = `smoke-draw` transcript;
      failure mode = 28 deliberately-broken invariant fixtures, each proving its check fires.

## 10. Verification result

- Submitted: 2026-08-16
- Verdict:   **PASS** for the delivered scope (REQ-203 in full; REQ-204 partially — see §11).
- Findings:  none outstanding against this task. One finding against the *product* was raised and
             filed as issue #56; it is deliberately not fixed here.

## 11. Outcome

**Delivered (REQ-203 — complete):**
- `gosurvey_headless` target; link surface proven GL-free and window-free by `dumpbin`.
- Transcript format + driver (`NEW`/`OPEN`/`SAVEAS`/`DIALOG`/`CMD`/`PICK`/`ESC`/`UNDO`/`REDO`/
  `CHECK`/`EXPECT`), JSON result, non-zero exit naming failure + step + source line.
- Two link-time platform seams; `platform/AppPaths` split out of `AppIcon`.
- Transcript corpus registered with ctest, so it runs on every push (REQ-202).

**Delivered (REQ-204 — partial):**
- `util/docinvariants`: 8 of the 9 invariants REQ-204 names, with a deliberately-broken fixture each.
- The `gs-roundtrip` differential oracle, which found #56 on its first run.

**Delivered (REQ-204 — generator + minimizer):**
- Seeded, structure-aware generator with a fixed prelude, weighted command choice, a hostile
  coordinate ladder, and a justified denylist.
- Delta-debugging minimizer, signature-matched, bounded, reporting its reduction ratio.
- `gosurvey_headless fuzz --seed N | --seeds A..B [--roundtrip]`, running every candidate as an
  isolated subprocess with timeout-based hang detection.
- `headless.fuzz-smoke` in CI as a canary for the harness itself.

**NOT yet delivered — the task stays open:**
- `undo-redo-identity` and `dxf-export-stable` oracles. Both need a document-equality predicate,
  which is a genuine design question (float tolerance under REQ-101, id renumbering, store
  ordering) and is the single biggest remaining gap: `undo-redo-identity` is the highest-value
  oracle in REQ-204's table and is not yet implemented.
- Byte fuzzing of the file parsers over a seed corpus (`docs/fuzz-harness.md` §8 stage 6).
- **The triage / dedupe / auto-file pipeline (stage 7).** In-run dedupe by signature works; what is
  missing is cross-run dedupe against `gh issue list --state all --search "<sig>"`, the daily filing
  cap, and the filing itself. Issues #56–#59 were filed **manually** in the §6 format, including the
  `fuzz-sig` markers, to prove the format before automating it.
- ASAN (`/fsanitize=address`) on the fuzz target — decided in ADR-031 (f), not yet wired up. Until it
  is, the fuzzer detects invariant violations, hangs and hard crashes, but not silent memory errors.

- Requirements satisfied: REQ-203 (Acceptance met: yes — condition 1 verified by `dumpbin`; the
  "matches the GUI on a `.gs` diff" condition is met in form but **not yet exercised against an
  actual GUI session**, which is recorded here rather than claimed).
  REQ-204 (Acceptance met: **yes for the generator, minimizer and invariant fixtures**; the oracle
  table is not yet complete — 8 of 9 invariants plus `gs-roundtrip`, with `undo-redo-identity` and
  `dxf-export-stable` outstanding).
- Tests added:            `tests/DocInvariantsTests.cpp` (28 cases), `tests/FuzzHarnessTests.cpp`
                          (13 cases), `headless.smoke-draw`, `headless.gs-roundtrip`,
                          `headless.fuzz-smoke`, `headless.gs-roundtrip.compare` (disabled, #56/#57)
- Refactors:              `platform/AppPaths` extracted from `platform/AppIcon` (§8)
- Docs updated:           `docs/fuzz-harness.md`, `spec/requirements.md`, `spec/architecture.md`,
                          `spec/project.md`
- Findings filed:         #56, #57, #58, #59 — all `fuzz` / `bug` / `sev:corrupt`
- Done:                   **not done** — open at the remainder above.
