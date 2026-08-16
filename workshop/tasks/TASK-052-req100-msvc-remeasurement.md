# TASK-052 — REQ-100 re-measured under MSVC

- Type:    measurement (no `src/` change)
- Status:  done — profiles (a) and (c) measured and recorded; profile (b) cannot be measured
           because no mesh bench scene exists (§5, FINDING-1)
- Opened:  2026-08-15
- Owner:   Workshop

## 1. Authority

- Requirements: **REQ-100** — its own status says the recorded figure is invalid and instructs
  "Re-measure with `BENCH` and record the MSVC figure." This task is that instruction, not a new
  decision, so no SPEC GAP is raised for recording the numbers.
- Constraints:  `project.md` §7 — the reference machine, and (since 2026-08-15) the pinned MSVC
  toolchain. REQ-100's acceptance now names both: "a figure measured with a different compiler is
  a different result."
- Unblocks:     TASK-046 (REQ-068 could not claim the budget without the surface profile),
  TASK-049 debt (0b), and the REQ-058 sign-off text, which quoted the invalidated 8.93 ms.
- Owning subsystem: none — nothing is built here. The deliverable is a number and its record.

## 2. Result

**Both measurable profiles PASS under MSVC.**

| REQ-100 profile | scene | median | **p95** | budget | verdict |
|---|---|---|---|---|---|
| (a) line segments | 250,000 segments | 6.44 ms | **9.27 ms** | 16 ms | **PASS** |
| (b) shaded meshes | — | — | — | — | **NOT MEASURABLE — no bench scene exists (FINDING-1)** |
| (c) surface | 100,000 points / 199,966 triangles (599,898 edges) | 8.66 ms | **9.32 ms** | 16 ms | **PASS** |

Against the superseded clang figure, profile (a) moved 8.93 → **9.27 ms**: ~4% slower, comfortably
inside the budget, and small enough that the honest summary is "the toolchain change did not move
the required density," not "MSVC is slower."

Profile (c) is the **first ever measurement of the surface profile** — it has no clang predecessor
to compare against. It is recorded here rather than in TASK-046 because TASK-046 could not run it.

### Headroom sweep — this is where the change actually shows

| segments | median | p95 | verdict | clang (2026-08-12) |
|---|---|---|---|---|
| 25,000 | 5.16 ms | 6.93 ms | PASS | 5.91 ms |
| **250,000 — REQ-100** | **6.44 ms** | **9.27 ms** | **PASS** | 8.93 ms |
| 500,000 | 7.85 ms | 11.09 ms | PASS | 10.15 ms |
| 750,000 | 13.21 ms | 18.09 ms | **FAIL** | 12.10 ms — *passed* |
| 1,000,000 | 16.12 ms | 21.94 ms | **FAIL** | 19.61 ms |

**The headroom claim has to be revised down.** The budget was recorded as holding to 750k segments
— 3–4× the required density. Under MSVC it holds at 500k and fails at 750k, so the honest figure
is **~2× the required density**. The requirement itself is unaffected: 250k passes with ~42%
headroom. But every document that quoted "3–4×" was quoting a property of a binary the project no
longer ships, and a weaker GPU now has less margin than that text implied.

## 3. Environment

- Reference machine (`project.md` §7), confirmed before running rather than assumed:
  AMD Ryzen 7 8745HX (8C/16T) · NVIDIA GeForce RTX 5060 Laptop, driver 32.0.16.1074 ·
  31.3 GB RAM · Windows 11 build 26200 · 2560×1600. All four match the recorded reference.
- Build: `cmake --build build` under `vcvars64` — MSVC 14.50.35717, `ninja-release` preset,
  clean link. Warnings are the pre-existing C4244/C4456 set; no new ones.
- Every run: 900 timed frames after 60 warm-up, 0.5°/frame continuous orbit, vsync off (the bench
  disables and restores it), model space, one maximized window on the built-in display.
- Raw record: `%APPDATA%\GoSurvey\bench-req100.txt`, nine runs, 2026-08-15 21:21–21:28.

## 4. Repeatability — the check that makes the sweep quotable

Four benches ran back to back on a laptop GPU, and the 750k result had moved a long way from its
clang predecessor (12.10 → 18.09 ms). A thermal explanation would produce exactly that shape, so
the sweep was not recorded until it was ruled out:

| run | when | p95 |
|---|---|---|
| 250k | before the sweep | 9.27 ms |
| 250k | immediately after the 1M run | 9.11 ms |
| 750k | after 90 s idle | 16.49 ms |
| 250k | after a further 90 s idle | 9.08 ms |

250k reproduces within 0.19 ms across the whole session, warm or cold, so the machine is not
throttling at the required density and the recorded figure is stable. 750k varies more (18.09 vs
16.49) but fails the budget either way — the conclusion "the budget no longer holds at 750k" does
not rest on the difference between those two numbers, which is why the weaker of them is the one
quoted above.

## 5. Findings

```
FINDING-1 — REQ-100 profile (b) has never been measurable, and this is not new.
- REQ-100's acceptance requires the budget to be met "in each of the three profiles."
- `BENCH` implements two: a segment count, and `BENCH SURFACE`. There is no mesh scene and no
  `BENCH MESH`, so profile (b) has no case to run.
- TASK-041 §7 predicted this exactly: "`BENCH` still has no mesh scene, so the *budget* is
  unclaimed. That is REQ-100 work and belongs with `BENCH`, not here." Nothing has closed it since.
- Consequence, stated plainly: **REQ-100 cannot be marked fully met**, and REQ-064's acceptance
  condition "the frame budget is met in Shaded at the REQ-063 mesh density" is still unverified.
  TASK-040 measured 2D Wireframe 8.25 ms vs Hidden 10.30 ms (clang, no meshes present), which shows
  depth testing is affordable but is not the mesh profile.
- Not fixed here: adding a scene generator, a `BENCH MESH` branch and its tests is code in
  `benchscene.*` and `CadCommands.cpp`, which is a task of its own, not a line item in a
  measurement run. Recorded in TRACKER as FEAT-011.
```

```
FINDING-2 — the bench's permanent record does not say which profile produced the number.
- `bench-req100.txt` writes `segments <n>` for every run. The surface run therefore appears in the
  record as `segments 599898`, indistinguishable from a 600k-segment line scene.
- The console message gets this right and explains why, in a comment directly above the file
  writer: "a p95 quoted without saying which of REQ-100's three profiles produced it is as
  unreproducible as one quoted without the reference machine." The file is the half of that
  argument that was not carried through.
- It bit this task immediately: the surface profile's point and triangle counts had to be recovered
  from the console rather than read out of the record, and the count in §2 is stated from the edge
  count (599,898 = 3 × 199,966 triangles; 2n − h − 2 with n = 100,000 gives a 32-point hull, which
  is consistent).
- Small, and it belongs with FEAT-011 since both edit the same writer. Recorded, not sneaked in.
```

Note on TASK-046's recorded triangulation figure (199,957 triangles at 100k points, clang): this
run produced 199,966. Nine triangles apart on a deterministic scene is a hull-count difference, not
noise, and it is not worth chasing — but it is recorded rather than smoothed over, because the two
numbers were measured by different binaries and only one of them is current.

## 6. Verification

- [x] build-project      — PASS. Clean MSVC build immediately before the runs; the binary measured
      is the binary the presets produce.
- [x] performance-review — PASS for (a) and (c); (b) NOT RUN, and the reason is FINDING-1, not an
      omission by this task.
- [~] testing            — n-a. No code changed. The bench's own guards (`BenchSceneTests`) were
      already green in the build under measurement.
- [x] architecture-review, code-review, dependency-audit — n-a. Nothing was implemented.

## 7. Docs updated

- `spec/requirements.md` — REQ-100 status (MSVC figures, the revised headroom, and profile (b)
  stated as unmet), its traceability row, its revision history; REQ-058's sign-off figure.
- `spec/project.md` — decision-log entry recording the re-measurement and closing the consequence
  clause of the 2026-08-15 MSVC pin.
- `spec/roadmap.md` — M3 status; the GL-driver-variance risk row, whose "3–4×" is now "~2×".
- `workshop/tasks/TASK-046` — surface profile supplied; the task can close.
- `workshop/tasks/TASK-049` — debt (0b) cleared.
- `TRACKER.md` — FEAT-011 (mesh bench case + profile naming in the record).

---

COMPLETION REPORT — TASK-052 — 2026-08-15
- Requirements satisfied:  REQ-100 profiles (a) and (c) re-measured under MSVC on the reference
                           machine and recorded — **PASS** at 9.27 ms and 9.32 ms against 16 ms.
                           REQ-100 as a whole is **still not fully met**: profile (b) has no bench
                           case (FINDING-1).
- Summary:                 Nine bench runs on a freshly built MSVC binary; the two runnable profiles
                           pass, the headroom claim is corrected from 3–4× to ~2×, and the missing
                           mesh profile is named rather than glossed.
- Tests:                   none added; none needed — no code changed.
- Verification verdict:    PASS (measurement complete for what is measurable)
- Assumptions:             none. The reference machine was verified against `project.md` §7 rather
                           than assumed, and the sweep was repeatability-checked before being
                           recorded (§4).
- Architectural decisions: none made by Workshop.
- Dependencies:            none.
- Technical debt noted:    FINDING-1 (no mesh bench case — REQ-100 profile (b) and REQ-064's budget
                           condition both rest on it) and FINDING-2 (the record does not name the
                           profile), both filed as TRACKER FEAT-011.
- Build:                   reproducible, clean, MSVC 14.50.35717 via the pinned preset.
- Docs updated:            §7.
