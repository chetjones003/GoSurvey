# Headless driver and fuzz harness — design

> Status: **design only**. Nothing here is built. The product-level requirements are
> REQ-203 (drivability) and REQ-204 (randomized invariant checking); the structural
> decision is ADR-031. This document is the engineering detail underneath them, plus
> the developer tooling that is deliberately *not* in the spec: triage, deduplication,
> and issue filing.
>
> Read the spec entries first. Where this document and the spec disagree, the spec wins
> and this document is wrong.

---

## 1. Why this is cheap, and where the cost actually is

> **Corrected 2026-08-16.** The first version of this section was written from `grep` and got
> two facts wrong in the optimistic direction. The table below is re-derived from `dumpbin
> /SYMBOLS` on the built objects, which is the only way to read a link surface honestly.
> What changed and why it matters is in §1.1.

The enabling measurement, taken from the tree at `5cc964d` by dumping undefined externals from
`build/CMakeFiles/GoSurvey.dir/**`:

| Fact | Consequence |
|---|---|
| `CadCommands.hpp` and its whole header closure include no imgui/GLFW/GL | `AppCommandState` can be constructed in a console program |
| `ProcessCommandLineSubmit(char*, int, AppCommandState&, log&)` — `CadCommands.hpp:2070` | Text commands already have a public entry point |
| `SubmitViewportPick(AppCommandState&, float, float, log&, …)` — `CadCommands.hpp:2067` | So do mouse picks |
| `kRegistry` — `CadCommands.cpp:2208` — holds ~54 commands with aliases and descriptions | The fuzz alphabet already exists, in-tree, self-describing |
| `CadCommands.cpp.obj` needs only **1** of the 11 `WinFileDialogs` functions (`BrowseOpenFileGltfUtf8`) | The dialog seam is one stub, not eleven |
| `CadCommands.cpp.obj` needs **3** `PdfAttach` symbols, and `PdfAttach.cpp` includes `<GL/glew.h>` and calls `glGenTextures`/`glDeleteTextures` | A second platform seam is required — GL reaches the Commands layer through PDF underlays |
| `DxfIo.cpp.obj` needs **8** symbols from `CadCommands.cpp`; `GsIo.cpp.obj` needs **9** | ✗ The parsers are **not** free-standing — both drag in `CadCommands.cpp` |
| `GsMigrate.cpp` and `SurveyCsv.cpp` need **0**; `gltfimport`/`stlimport` need **1** (`meshgeom::MeshProblemText`) | These four genuinely are free-standing, and already link in `GoSurveyTests` |
| `SurveyPoints.cpp` calls `ImGui::GetFont()` to size survey-label boxes, and `GsIo.cpp`'s load path calls into it | ✗ **Loading a `.gs` file requires a live ImGui context.** See §1.1 |

So the build plumbing is a day's work, not a rewrite. **The cost is in the oracles and the
triage discipline**, and that is where the design effort below is concentrated. A harness
that only detects crashes will find a handful of null-deref bugs in its first week and then
go quiet while the interesting defects — an undo that doesn't fully undo, an exporter that
drops an entity type — keep shipping.

### 1.1 What the corrected measurement changed

Two claims in the original draft were wrong, and both were load-bearing:

**"`DxfIo.cpp` calls one symbol from `CadCommands.cpp`; `GsIo.cpp` calls none."** It is 8 and 9.
This was the stated reason for fuzzing the parsers *before* building the driver — the parsers
looked separable and the driver looked expensive. They are not separable: both parsers pull
`CadCommands.cpp`, which pulls its own closure. **The staging in §8 is therefore wrong as
originally written.** The build work is shared, so it happens once, up front, and *then* both
the parser fuzzer and the transcript driver are cheap consumers of it. Parser fuzzing remains
the first consumer to build, because its oracles are simpler — but that is now a small ordering
preference rather than a stage that stands alone.

**"`CadCommands.cpp` contains exactly one ImGui call, so the refactoring cost is one function."**
The first half is true and the conclusion does not follow. The *domain layer* has a second
ImGui dependency, in `SurveyPoints.cpp:666`: sizing a survey point's label box calls
`ImGui::GetFont()` and measures the text with `MtextRichNaturalContentPx(font, …)`. Unlike
`LoadApplicationFont`, this is not an outlier that can be moved — **text metrics determine
stored geometry** (`boxMinX/boxMaxX/boxMinY/boxMaxY` on the annotation), and `GsIo.cpp`'s load
path calls `RepositionAllSurveyPointLabels` on every open. Opening a drawing needs a font.

That is a genuine architectural question rather than a plumbing detail, and it is escalated as
a SPEC GAP in `workshop/tasks/TASK-056`. It invalidates ADR-031 (c) as written and makes
REQ-203's "links with no imgui" acceptance condition unachievable in that form.

---

## 2. The transcript

A transcript is a text file. It is a **test fixture, not a file format**: no version header,
no escaping rules beyond the obvious, no forward-compatibility promises. If the grammar needs
to change, the corpus is regenerated.

```
# lines starting with # are comments; blank lines are ignored
# ---- setup ----
NEW                          # start from the default template
OPEN samples/site.gs         # or open a fixture (paths are repo-relative)
DIALOG OPEN  C:/tmp/in.dxf   # queue an answer for the next Browse*Open* call
DIALOG SAVE  %OUT%/out.gs    # queue an answer for the next Browse*Save* call
DIALOG CANCEL                # queue a cancelled dialog (returns false)

# ---- driving ----
CMD LINE                     # -> ProcessCommandLineSubmit("LINE")
CMD 100,100                  # -> ProcessCommandLineSubmit("100,100")
CMD @50,0                    # relative point, exactly as a user would type it
CMD                          # bare Enter (ends LINE) — an empty CMD is meaningful
PICK 305.2 118.9             # -> SubmitViewportPick(x, y)
PICK 305.2 118.9 SHIFT       # modifier flags map to the existing bool parameters
ESC                          # cancel the active command
UNDO / REDO                  # explicit, so the oracle can bracket them

# ---- interchange (as distinct from OPEN/SAVEAS, which are the drawing's own .gs) ----
EXPORT DXF %OUT%/out.dxf     # -> ExportDxfFile
IMPORT DXF %OUT%/out.dxf     # -> ImportDxfFile

# ---- checking ----
CHECK ALL                    # run every invariant now (they also run after every step)
EXPECT LINES 3               # assert a count; fails the run if wrong
EXPECT LOG "Unknown command" # assert a log line was emitted (REQ-201 checks)
EXPECT SAMEFILE a b          # byte-identical; the differential oracles' comparison
EXPECT DIFFERENTFILE a b     # NOT byte-identical — asserts a step actually changed something
DUMP entities.json           # write state for offline diffing
```

Design notes that matter:

- **`CMD` with no argument is not a no-op.** Bare Enter is how half of GoSurvey's commands
  terminate, and a transcript that cannot express it cannot express a polyline.
- **`DIALOG` queues answers rather than configuring a mode.** A transcript that opens two
  files in sequence needs two different answers, and a mode cannot give them.
- **Picks carry LOCAL coordinates, not screen coordinates** — `world = local +
  worldDocumentOrigin`. Screen coordinates would make every transcript depend on a window size that
  does not exist. This bullet said **world** until 2026-08-17 and was simply wrong: `SubmitViewportPick`
  takes local storage coordinates, its values go straight into the flat stores, and the parameters were
  named `worldX`/`worldY` while every caller passed local. It matters for reading transcripts — on a
  drawing whose origin has been established at easting 2e6, `PICK 10 10` means local `(10,10)`, i.e.
  world `(2000010, 500010)`, not a point near the world origin. Pinned by
  `transcripts/regression-pick-local-coordinates.txt`.
- **`%OUT%` expands to a per-run temp directory.** Transcripts must never write into the
  source tree (REQ-200, CON-07), and a fuzz run writes a lot of files.
- **`EXPECT DIFFERENTFILE` exists because differential oracles pass when nothing happened.**
  "Do something, undo it, compare" is green on a document where the something never occurred, and a
  check that cannot fail reports success forever. It is the counterweight to `SAMEFILE`: assert the
  document moved, *then* assert it came back. Added 2026-08-18 after the undo/redo oracle shipped
  without it and immediately produced a false finding (§8).
- **`EXPORT`/`IMPORT` take the format as a separate word** (`EXPORT DXF <path>`), so stage 6 can add
  `GLTF` and `STL` without inventing a verb per format.

### Driver CLI

```
gosurvey_headless run <transcript>            # exit 0 = pass, non-zero = failure
                   --json <path>              # machine-readable result
                   --check-every-step         # default on; --check-at-end for speed
                   --timeout-ms <n>           # hang detection
```

The JSON result carries: exit reason, failing step index and source line, the invariant that
fired, entity counts before/after, and the full command log. That log is the single most
useful field in a bug report and costs nothing to capture.

---

## 3. The oracles

Implemented in `src/util/docinvariants.{hpp,cpp}` — pure, no GL, no ImGui — so the Catch2
suite and the driver share one implementation (ADR-031 (d)).

```cpp
struct InvariantViolation {
  const char* name;        // stable id, used in the dedupe signature
  std::string detail;      // "userLinesFlat.size()=19 not divisible by 6"
  int entityIndex = -1;
};
// Appends one entry per violation; empty result means the document is sound.
void CheckDocumentInvariants(const AppCommandState&, std::vector<InvariantViolation>* out);
```

| Id | Check | Rationale |
|---|---|---|
| `undo-redo-identity` | `UNDO` then `REDO` yields a document equal to the one before — where "equal" is byte-identical `.gs`, and the undo is preceded by an anchor edit and followed by `EXPECT DIFFERENTFILE` so the check cannot be vacuous | The classic CAD defect: an edit that mutates state the snapshot doesn't capture. Note `UNDO`/`REDO` are **not** inverses on an exhausted stack — see the seventh harness defect in §8 |
| `gs-roundtrip` | save → load → save → load → save, and the **last two** are byte-identical | REQ-079 as amended 2026-08-17. A field written but not read, or read but not written. It is the last two rather than the first two because a load may legitimately *normalize* the drawing's storage (the large-coordinate origin rebase), and that normalization is idempotent — see #61 |
| `dxf-export-stable` | **two halves.** *survival*: entity counts before export == counts after import. *stability*: export → import → export converges | The two halves catch different defects, and the distinction was got wrong when this row was first written. Export→import→export does **not** catch an exporter with no branch: the type is missing from both files, so they match. It catches exporter/importer **asymmetry** (in the file, gone after the round trip). Catching the missing branch needs the *document* compared across the round trip, which is the survival half. Both fire today — ARC/ELLIPSE dropped, LWPOLYLINE shattered into lines |
| `finite-coords` | no stored coordinate is NaN or ±inf | Degenerate input reaching storage |
| `local-storage` | every stored coordinate is local; `world = local + worldDocumentOrigin` | The project's recurring bug class: a world-coordinate importer that forgot to subtract |
| `flat-strides` | `userLinesFlat % 6`, `userPolylineVerts % 3`, `userCirclesCxCyZR % 4`, `vertsXyz % 3` | Architecture §11.8, in the exact terms that invariant is written |
| `entity-ids` | ids unique; `nextEntityId` > every id | REQ-076 |
| `selection-in-range` | every selection index valid for its store | §11.9 — a stale index surviving a compacting erase |
| `command-logged` | every submitted command emitted ≥1 log line | REQ-201, converted from a review convention into a check |

Two of these — `undo-redo-identity` and `gs-roundtrip` — are **differential** oracles: they
need no knowledge of what is correct, only that two paths agree. Those are the ones that find
defects nobody predicted, and they should be built first.

**Every invariant needs a fixture that deliberately breaks it** (REQ-204 acceptance). An
oracle that has never fired is not known to be an oracle; this is the single most common way
a harness like this quietly stops working.

---

## 4. The generator

Structure-aware, seeded, and driven by `kRegistry` so it cannot go stale as commands are
added:

> **As built.** `tests/headless/FuzzGenerator.{hpp,cpp}`. The command list is a **parameter**, not
> something the generator fetches: `kRegistry` is in an anonymous namespace inside CadCommands.cpp,
> and exposing it would be a public-API change the Workshop does not get to make. `FuzzMain.cpp`
> enumerates the 53 commands through the already-public `FuzzyCommandSuggestions`, unioning
> single-letter queries a–z (a one-character query is a subsequence match, so every command name
> containing any letter is returned). Passing the list in also makes the generator unit-testable
> with no application linked.

0. **A fixed prelude first.** Before any random action, build a base drawing: a closed box of lines,
   two circles, a polyline, one text. This was added after measuring, not by design instinct —
   without it a 50-line generated transcript produced **one entity**, because nearly every modify
   command (`MOVE`, `ROTATE`, `TRIM`, `OFFSET`, `ERASE`, `JOIN`) is a no-op with nothing selected and
   nothing to select. The fuzzer spent its whole budget bouncing off commands that declined to act,
   and looked like it was working the entire time. For the same reason, two thirds of generated
   `PICK`s aim at known prelude geometry rather than into empty space — a pick that hits nothing
   clears the selection, which leaves every following modify command inert.
1. **Weighted command choice.** Uniform choice over 53 commands wastes budget on `HELP`.
   Weight drawing and modify commands up; weight commands that only open a UI panel down (in
   headless they are shallow by construction). A small **denylist** (not an allowlist) removes
   `bench` (minutes per call), the model importers (spawn an external converter), and the plot and
   quit commands. A denylist means a command added tomorrow is fuzzed by default; an allowlist would
   silently exclude every new command, which is how a fuzzer quietly stops finding things.
2. **Phase-aware arguments.** After `CMD LINE`, emit points — not another command name.
   The state machine is readable from `AppCommandState::Kind` and the phase enums; the
   generator follows it about 80% of the time and violates it deliberately the rest, because
   "a command name typed mid-command" is a real user action and a real bug source.
3. **Hostile coordinates,** drawn from a fixed ladder: ordinary values, zero, negative,
   `1e12` (state-plane magnitudes — the project has been bitten here before), denormals,
   exact duplicates of a previous point, NaN, ±inf, and coordinates that make geometry
   degenerate (zero-length line, zero-radius circle, three collinear points for a 3P arc).
4. **Structural churn.** `ESC` at every phase, `UNDO`/`REDO` storms, model↔paper space flips
   mid-command, layer deletion while entities are on it, save/load in the middle of a run.
5. **Reproducibility.** A seed determines the transcript completely. No clock, no ASLR, no
   iteration over an unordered container in generator code.

---

## 5. Minimization

The seed is not the bug artifact — the **minimized transcript** is, because it survives
changes to the generator that would invalidate the seed.

Delta debugging over transcript lines:

1. Run the full transcript; record the failure signature (§6).
2. Repeatedly try removing a contiguous block of lines; keep the removal if the failure
   **with the same signature** still reproduces.
3. Halve the block size when a pass yields no removals; stop at size 1 with no removals.
4. Then minimize *within* lines: simplify coordinates toward `0,0`, collapse `PICK` runs.
5. Bound the whole thing (attempts and wall clock) and report the reduction ratio.

Matching on the signature, not merely on "it failed," is what stops minimization from
sliding onto a different, easier bug — which is the standard failure mode of naive delta
debugging and produces reports that describe the wrong defect.

Expect 200-line transcripts to reduce to 3–10 lines. That is the difference between a report
someone acts on and one they close.

> **As built.** `tests/headless/Minimizer.{hpp,cpp}`, pure and parameterized by a
> `StillFailsFn` predicate, so termination, the attempt cap and the reduction ratio are unit-tested
> without launching anything. Measured on real findings: 155 → 9 lines in 74 candidates (#58),
> 169 → 4 in 31 (#59), 116 → 5 in 31 (#57).
>
> Two implementation details are load-bearing:
>
> - **Every candidate runs as a subprocess, in its own empty output directory.** Subprocess because
>   a crash is the outcome most worth finding and an in-process harness dies with it — and because
>   `RunProcessAndWait` already has a timeout, so hang detection is free. The empty directory is not
>   hygiene, it is correctness: see finding 3 in §8's harness-defect list.
> - **After a successful removal the scan does not advance.** The lines that shifted into that
>   position are unexamined, and stepping past them is how a minimizer silently leaves half its work
>   undone while still reporting a healthy-looking ratio.
>
> The signature is `reason|kind` — e.g. `invariant|entity-ids`, `expect|SAMEFILE` — deliberately
> dropping the indices and byte offsets from the detail string, because those shift as lines are
> removed and a signature that changes under minimization makes the minimizer chase its own tail.
> The `reason` alone is too coarse: every `EXPECT` failure would share one signature, so a second
> unrelated defect found in the same run would be discarded as a duplicate.

---

## 6. Triage and issue filing

This is developer tooling, outside the spec (ADR-031, consequences). It is also the part that
decides whether the whole system is useful or is a machine for generating noise.

### Failure signature

```
sig = sha1( invariant-id | active-command-kind | top-3-normalized-stack-frames | exit-reason )[:12]
```

Normalizing frames means stripping addresses, line numbers, and template noise — a signature
that changes when an unrelated line is inserted above the crash cannot deduplicate anything.

### Pipeline

```
fuzz run (N seeds, ASAN, timeout)
   │
   ├── clean ────────────────────────► exit 0, summary line, nothing filed
   │
   └── failure
         ├─ 1. minimize (§5) ─────────► shortest transcript with the same signature
         ├─ 2. classify ──────────────► crash | asan | hang | invariant
         ├─ 3. dedupe:
         │      gh issue list --label fuzz --state all --search "<sig>"
         │      hit  → comment "also seed 41827" (or nothing if already noted) and stop
         │      miss → continue
         ├─ 4. attribute ─────────────► git blame the failing site → likely REQ-NNN
         └─ 5. gh issue create
```

### Issue template

```markdown
**Signature** `<!-- fuzz-sig: a19f3c0b2d41 -->`
**Class** invariant / `undo-redo-identity`
**Build** 5cc964d, MSVC 19.4x, ASAN on
**First seen** seed 41827

### Minimized reproducer
```
NEW
CMD LINE
CMD 0,0
CMD 1e12,0
CMD
UNDO
REDO
```

### Expected
After UNDO then REDO the document equals its pre-UNDO state (REQ-204 `undo-redo-identity`).

### Actual
`userLinesFlat` has 6 floats before UNDO and 12 after REDO — the redo re-applies the segment
without clearing the restored snapshot.

### Evidence
<driver JSON, command log, stack if any>

### Likely requirement
REQ-076 / REQ-204. Owner-layer: Commands.
```

The `fuzz-sig` HTML comment is what makes step 3 work on later runs, and it is invisible in
rendered issues. Labels: `fuzz` plus `bug`, and a severity label (`sev:crash` / `sev:corrupt`
/ `sev:cosmetic`) so the queue can be read at a glance.

### Guardrails — non-negotiable if this runs unattended

- **A daily cap.** No more than *N* issues filed per day, hard stop. If the cap is hit, the
  run stops and says so instead of continuing to file.
- **Dedupe searches `--state all`, not just open.** A closed-as-wontfix bug must not come
  back every night.
- **A minimized reproducer is required to file.** No reproducer, no issue — an unminimized
  200-line transcript is not actionable and should be dropped with a log line.
- **Filed issues are never auto-closed**, and the agent never edits code. It reports.

---

## 7. Running it: what a Claude instance actually contributes

The harness fuzzes far faster than any agent can, so the agent's value is not in generating
input. It is in the judgment-shaped steps: deciding whether two failures are the same defect,
writing an issue a human can act on in thirty seconds, and noticing when the harness itself
has broken (a sudden flood of identical signatures usually means a bad build, not 400 bugs).

**Orchestration** — one instance per seed range, each in its own git worktree so parallel
runs do not fight over `build/`:

```
agent A  --seeds 0..999       worktree fuzz-a
agent B  --seeds 1000..1999   worktree fuzz-b
```

Disjoint ranges keep the work non-overlapping without any coordination protocol, and the
dedupe step at §6.3 handles the case where two agents independently find the same defect.

For recurring unattended runs, the `schedule` skill (cron cloud agent) fits better than
`/loop`, since a fuzz session is long-running and should survive the terminal closing.

---

## 8. Staging

Ordered by cost-to-value, not by dependency — each stage is independently useful, and each
one de-risks the next.

> **Revised 2026-08-16 (TASK-056).** The original staging put parser fuzzing first, on the premise
> that the parsers were free-standing. §1.1 shows they are not — both pull `CadCommands.cpp` — so
> the build work is shared and happens once, up front. Stages 1 and 2 have therefore merged, and
> what was "Stage 2" turned out to be the cheaper starting point rather than the expensive one.

| Stage | Work | Status |
|---|---|---|
| **0** | Accept REQ-203/204 + ADR-031 | **done** — decision log, 2026-08-16 |
| **1** | The shared domain link surface: `GOSURVEY_DOMAIN_SOURCES`, `imgui_core` split from `imgui_backend`, the two platform seams, `platform/AppPaths` extracted from `AppIcon` | **done** |
| **2** | `gosurvey_headless` + transcript format + driver | **done** — REQ-203 |
| **3** | `docinvariants` + a deliberately-broken fixture per check | **done** — 8 invariants, 28 fixtures |
| **4** | Differential oracles: `gs-roundtrip` (found #56, #57, #60, #61; **on by default since 2026-08-17**), `undo-redo-identity` (**on by default since 2026-08-18** — 1000-seed sweep clean), `dxf-export-stable` (transcript written, held DISABLED against #63 and #64) | **done** |
| **5** | Seeded generator + delta-debugging minimizer | **done** — found #58 and #59, minimizing 155→9 and 169→4 lines automatically |
| **6** | Byte fuzzing of `DxfIo`/`GsIo`/glTF/STL/CSV over a seed corpus | **pending** — now cheap, since the link surface exists |
| **7** | Triage agent + dedupe + `gh issue create` | **pending** — signature + in-run dedupe are implemented; cross-run `gh issue list` dedupe and filing are still manual |
| **8** | Coverage-guided (`/fsanitize=fuzzer`) on the parser half | **pending** |

The ordering lesson worth keeping: the expensive part was never the driver, it was **finding out
what actually links**. Once that was measured, everything downstream got cheaper than estimated.

### What the harness has found so far

| # | Finding | How | Minimized |
|---|---|---|---|
| [#56](https://github.com/chetjones003/GoSurvey/issues/56) | A `TEXT` entity is saved to `.gs` with `id: 0` — the commit path never bumps the revision the `EnsureEntityIds` sweep gates on, so the sweep early-outs on a false premise (REQ-076; also breaks REQ-079 resave idempotence) | `gs-roundtrip` oracle, first run | by hand, 8 lines |
| [#57](https://github.com/chetjones003/GoSurvey/issues/57) | An **empty** drawing fails `.gs` resave idempotence: load materializes a default layer `"0"` and text style `"Standard"` that a new drawing does not have, so a new drawing is briefly in a state the rest of the code assumes cannot occur (REQ-079) | `gs-roundtrip`, seed 2 | auto, 116 → 5 |
| [#58](https://github.com/chetjones003/GoSurvey/issues/58) | `OFFSET` gives the new entity the **same `id`** as its source, so two entities share one id and §11.9's "a reference is a stable id" stops holding. All five `CommitOffset*` functions. The clipboard already solves this with `ClearEntityIdsFrom`; OFFSET never got it (REQ-076) | `entity-ids` invariant, seeds 2004 + 3555 | auto, 155 → 9 |
| [#59](https://github.com/chetjones003/GoSurvey/issues/59) | `CIRCLE` commits and stores an **infinite radius** when the centre is far enough from the picked point that the distance overflows `float` (REQ-201, REQ-101) | `finite-coords` invariant, seed 4737 | auto, 169 → 4 |
| (no issue) | A **relative** coordinate `@dx,dy` resolves to a non-finite point: `ParseWorldPoint` computes `base + delta`, which overflows `float` while both are representable. The absolute path was already refused by stream extraction, so the relative branch was the one route to a non-finite coordinate from finite input — and the one with no check. LINE, POLYLINE and RECT (REQ-204, REQ-201) | probing #59's siblings by hand, not a seed | n/a — found by probe |
| [#60](https://github.com/chetjones003/GoSurvey/issues/60) | Erasing the **last** polyline leaves `userPolylineOffsets` as `{0}`, which the writer serialises and the reader then refuses — **a saved drawing that can never be reopened**, the only finding so far that loses work (REQ-079) | `gs-roundtrip`, seed 28 `--roundtrip` | **auto-minimizer degenerated**; by hand, see below |
| [#61](https://github.com/chetjones003/GoSurvey/issues/61) | A coordinate of state-plane magnitude (`-1e+12`) breaks `.gs` resave idempotence, hit by ~1/3 of seeds. **Resolved as a spec defect, not a code defect** (decision D-2026-08-17-a): the origin rebase on load is the local-storage design working, so REQ-079 was amended to carve out normalization and require it to be idempotent instead. The oracle now compares B to C, and `emitRoundTrip` is **ON by default** — a 1000-seed sweep went from ~325 failures to **0** | `gs-roundtrip`, ~325/1000 seeds | auto, 116 → 8 |
| (no issue) | The rebase threshold read `fabs(mnY)` **twice** and `fabs(mxY)` never, so a drawing whose only large coordinate was a large *positive* Y was silently never rebased — the precision repair simply did not fire. Found while diagnosing #61 and demonstrated by probe: `(0,0)→(0,1e+12)` was not rebased while `(0,0)→(1e+12,0)` was | reading the code #61 pointed at | n/a — found by probe |
| [#62](https://github.com/chetjones003/GoSurvey/issues/62) — **fixed 2026-08-18 (TASK-071)** | **`DELETE` erases 3 floats from the stride-4 circle store.** `userCirclesCxCyZR` holds `cx, cy, z, r`, and the erase loop removes `[k, k+3)` where the LINE loop twenty lines above correctly removes `[k, k+6)` from its stride-6 store. Every circle after the erased one shifts by one float and reads its predecessor's **radius as its centre X**: three circles at `(0,0) r1`, `(500,0) r2`, `(1000,0) r3` become, after deleting the first, a circle at `(1, 500)` with radius 2. Nothing warns, and `SAVEAS` writes the corrupted array to the `.gs`, so deleting one circle silently moves every other circle in the drawing — permanently | `flat-strides` invariant, reached by `BOX` select + `DELETE` while writing the `undo-redo-identity` transcript | by hand, 8 lines — `transcripts/delete-circle-stride.txt` |
| [#63](https://github.com/chetjones003/GoSurvey/issues/63) | **DXF export drops ARC and ELLIPSE entirely.** `DxfIo.cpp`'s writer has branches for LINE, CIRCLE, POINT, LWPOLYLINE, MTEXT and HATCH/SOLID and mentions `userArcs`/`userEllipses` nowhere in the export path. Measured, not inferred: a drawing holding exactly one ellipse exports a DXF containing **zero entities of any kind**. This is precisely the defect REQ-204's `dxf-export-stable` row was written to describe | `dxf-export-stable` survival half, first run | `transcripts/dxf-export-stable.txt` |
| [#64](https://github.com/chetjones003/GoSurvey/issues/64) — **fixed 2026-08-21 (TASK-083)** | **A LWPOLYLINE did not survive a DXF round trip as a polyline.** It exported correctly and the *importer* read it back as loose segments ("DXF import — 4 line segment(s)"), so a drawing that went out to DXF and back had every polyline shattered into unrelated lines. Distinct from the row above, and caught by a different half of the same oracle: this one is an exporter/importer **asymmetry**, so the two exports differ (7194 vs 7367 bytes). Root cause: the importer had no polyline sink — `ParseEntityRegion` predates the polyline store, which REQ-053 added and taught only the *exporter* about. Fixing it uncovered a second asymmetry underneath, invisible while the first one stood: `$EXTMIN`/`$EXTMAX` were swept from lines, circles, annotations and points only (never polylines) and were written from the **local** store while every entity is written in **world** — so the header described a different frame from the body, and the file changed whenever the origin moved. Importing is what moves it, so the cycle never settled | `dxf-export-stable` stability half | `transcripts/regression-64-dxf-polyline-identity.txt` |

**Five defects were found in the harness itself before it found any of these**, which is the part
worth remembering:

1. An `attr-counts` check that fired on every valid polyline, because `userPolylineOffsets` is CSR
   and the first implementation counted it directly. A false-positive oracle files garbage and
   teaches everyone to ignore the harness — it is the failure mode §9 warns about, and it was caught
   only by running the checks against known-good data. Hence `DocInvariantsTests.cpp` asserts the
   clean document is **silent**, not merely that broken ones are noisy.
2. Three wrong assumptions about the command model, all caught by the driver refusing to agree with
   them (bare Enter restarts LINE rather than exiting; commands repeat until ESC; POLYLINE needs
   `END`).
3. **A minimizer that produced a confident lie.** Candidates shared one `%OUT%` directory, so a
   candidate that dropped the `SAVEAS` lines still found the previous candidate's files on disk,
   still compared them, and still "failed" — reducing a 116-line transcript to
   `NEW` + `EXPECT SAMEFILE …` and reporting 98% reduction for a reproducer describing no bug at
   all. Every candidate now gets an empty output directory. **State leaking between candidates is
   how a minimizer lies to you**, and the only reason it was caught is that the 2-line output looked
   too good and got checked by hand.

A sixth was found later, by #60 rather than before it, so it is recorded separately: **the failure
signature is too coarse to distinguish "file absent" from "file present but rejected."** Both are
`io|OPEN failed`, so the minimizer legitimately reduced #60 to `NEW` + `OPEN %OUT%/rt-a.gs` — a
2-line reproducer that still fails, for an entirely different reason, and describes none of the bug.
This is the same weakness already fixed once for `expect`, resurfacing in `io`. Until the signature
carries the reader's reason, an `io|OPEN failed` minimization has to be read by hand; #60's
regression transcript was written by hand for exactly that reason, and asserts entity counts on both
sides of the round trip so it cannot pass for the wrong reason.

A **seventh**, 2026-08-18, and it is the cleanest example yet of why a new oracle is measured before
its default is flipped: **the first `undo-redo-identity` oracle reported a defect against correct
behaviour.** The naive shape — `SAVEAS a` / `UNDO` / `REDO` / `SAVEAS b` / `EXPECT SAMEFILE a b` —
assumes `UNDO` and `REDO` are inverses. They are not at the bottom of the stack:

> **`UNDO` on an exhausted undo stack is a no-op. `REDO` after it is not.**

A fuzzed transcript issues undos at random, so it often reaches the oracle with the undo stack empty
and the redo stack full. The pair then moves the document *forward* by one operation, the files
differ, and the harness reports a bug in behaviour that AutoCAD shares. A 1000-seed sweep produced
2 such failures (seeds 260 and 263) and a minimized reproducer that looked entirely convincing —
`POLYLINE`, `UNDO`, save, `REDO`, save, compare — until the question "which `UNDO` is that?" got
asked. It was not the oracle's; the minimizer had deleted the oracle's own `UNDO` and kept an
earlier random one, because `expect|SAMEFILE` cannot tell the two apart. **That is the coarse-signature
weakness of the sixth defect, recurring a third time.**

Two changes fix it at the root rather than by tolerance, and both are in `FuzzGenerator.cpp`:

1. **An anchor edit** — one committed entity immediately before the first save — so the undo stack is
   guaranteed non-empty and its top entry is known. The entity *type* varies with the seed (each type
   has its own snapshot/restore path, which is where the defect class lives); the *coordinates* stay
   plain, because a hostile value the command legitimately refuses would leave the stack untouched
   and put the false positive straight back.
2. **`EXPECT DIFFERENTFILE` across the undo**, before `EXPECT SAMEFILE` across the redo. This is what
   makes the oracle two-sided: it proves the document moved before it proves it came back.

The rebuilt oracle sweeps 1000 seeds with **0 failures** — and because `DIFFERENTFILE` fails on a
no-op undo, those 1000 passes are also 1000 proofs that the check was not vacuous. The lesson to
keep: **an oracle's first failures are more likely to be the oracle's fault than the product's**, and
the only reason this one was caught is that a 10-line reproducer got read instead of filed.

---

## 9. Known risks

- **The oracles are where the bugs in *this* system will be.** A false-positive invariant
  files garbage issues and destroys trust in the harness faster than any amount of real
  findings builds it. Hence the deliberately-broken fixture per check.
- ~~**`undo-redo-identity` needs a document equality predicate**, and defining "equal" is a
  real design question (float tolerance, id renumbering, ordering within stores).~~
  **Resolved 2026-08-18, by noticing the predicate already existed.** `gs-roundtrip` compares
  documents by saving each to `.gs` and diffing the bytes, and `.gs` *is* the canonical
  serialization of an `AppCommandState` — so `EXPECT SAMEFILE` over two saves is the predicate, and
  it needs no new code. It is also the *strictest* available answer, which is the right one here and
  is why the three sub-questions dissolve rather than needing decisions: undo is a **restore**, not
  a computation, so REQ-101's tolerance has no bearing on a snapshot agreeing with itself; an id
  that changes across undo/redo violates REQ-076 outright; and a reordered store is exactly what
  §11.9 exists to worry about. A predicate that had to *choose* how much drift to forgive would
  have been defining the bug out of existence.
  **The risk this row named turned out to be real, but somewhere else** — see the seventh harness
  defect in §8. The oracle's first version was noisy, and not because equality was too strict.
- **Fuzzing finds shallow crashes fast and then plateaus.** That plateau is expected and is
  not a failure — it is the point at which the differential oracles start earning their keep.
- **A second target drifts** if the shared source list is bypassed. Failure mode is a link
  error, which is the acceptable kind.
