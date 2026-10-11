# TASK-039 — REQ-100: frame-budget benchmark

- Type:    feature (closes the last open REQ-058 acceptance condition)
- Status:  done — §9's SPEC GAP was resolved the same day; see the note at the end of §9
- Opened:  2026-08-12
- Owner:   Workshop

## 1. Authority

- Requirements: **REQ-100** (accepted) — "16 ms frame at the 95th-percentile frame while continuously
  orbiting a 250,000-line-segment scene on the reference machine", acceptance: "a committed
  benchmark scene profiled on the reference machine stays within budget at the 95th-percentile frame
  during a scripted orbit."
- Also the last unmet acceptance condition of **REQ-058**.
- Owning subsystem: Renderer (the thing measured), util (pure scene + statistics), Commands (the
  `BENCH` entry point), app (frame-loop timing).

## 2. Result

**PASS — p95 8.93 ms against a 16 ms budget at 250,000 segments**, with the whole scene framed and
continuously orbiting. Roughly 45% headroom.

| segments | median | p95 | verdict |
|---|---|---|---|
| 25,000 | 4.72 ms | 5.91 ms | PASS |
| **250,000 — REQ-100** | **6.51 ms** | **8.93 ms** | **PASS** |
| 500,000 | 7.41 ms | 10.15 ms | PASS |
| 750,000 | 8.64 ms | 12.10 ms | PASS |
| 1,000,000 | 14.98 ms | 19.61 ms | **FAIL** |

The budget holds to somewhere between 750k and 1M segments — **3–4× the required density**. The
jump from 750k to 1M is sharply non-linear (12.1 → 19.6 ms for a 33% increase in geometry), which
looks like a bandwidth or allocation threshold rather than a gradual slope: the tessellated line
buffer is 7 floats per vertex and two vertices per segment, so 750k → 1M segments moves it from
~42 MB to ~56 MB. Not investigated further — it is well outside the requirement.

Machine measured on, since the spec names none (§9):
`AMD Ryzen 7 8745HX (8C/16T) · NVIDIA GeForce RTX 5060 Laptop, driver 32.0.16.1074 · 31.3 GB ·
Windows 11 26200 · 2560×1600`.

## 3. Two decisions that determine whether the number means anything

```
VSYNC. The app runs glfwSwapInterval(1). Timing frames with vsync on measures the MONITOR, not the
renderer: every frame is pinned to a multiple of the refresh interval, so the bench would report
~16.7 ms whatever the scene costs — a clean "16 ms" that happens to sit right at the budget and
means nothing. The run disables vsync for its duration and restores it afterwards.
```

```
FRAMING. The first version of the scene spaced contours a constant 20 ft apart, so 500 contours
spanned 10,000 ft while the camera framed ~3,200. Most of a 250k-segment scene was off-screen, the
GPU rasterised only the visible part, and **250k benchmarked FASTER than 20k** — the tell that the
measurement was flattering itself. Fixed two ways: the generator now spreads contours across a
FIXED plan extent, so segment count changes density rather than area (as a real topo sheet does);
and the bench frames the camera from the scene's own bounding SPHERE, so nothing leaves the
viewport at any azimuth during the orbit. A test now pins the extent property directly.
```

## 4. Design

**The scene is generated, not stored.** 250,000 segments is ~1.5 million coordinates — as `.gs`
JSON that is a >100 MB artifact in the repository. A deterministic generator *is* a committed bench
scene: same commit, same bytes, same geometry, and diffable, which a 100 MB blob is not. Determinism
comes from hashing the vertex's index rather than a stateful PRNG, so output depends on nothing but
position in the scene — no iteration order, no accumulated state.

**Contours are the right shape to measure.** REQ-100's 250k figure is explicitly "the density of a
real topo with contours". Contours are long polylines (exercising the per-vertex-Z path added in
TASK-036), each at a constant elevation (so an orbit genuinely separates them and defeats any
plan-view culling), and carry no text or hatch to flatter the result.

**What is timed** is the full frame-to-frame cost at the top of the loop — poll, UI, snap/hover,
render, swap — because that is what the user feels. 60 warm-up frames are discarded: the first
frames pay for shader compilation, the VBO upload and the tessellation cache, none of which recur
while orbiting. Note that the geometry cache is keyed on revision/anchor/scale and **not** on the
orbit angles, so a pure orbit keeps it warm — that is a real property of the design, and it is why
the steady-state orbit is affordable at this density.

**`BENCH` cannot cost the user their drawing.** There is no "new drawing" entry point in the command
layer, so the run swaps the bench scene into the active drawing's polyline arrays and swaps the
user's geometry (and camera) back when it finishes. `BENCH [segments] [frames]` overrides the
defaults; results go to the command log and are appended to `%APPDATA%\GoSurvey\bench-req100.txt`,
because reading six figures off a fading command line is how a number gets transcribed wrong into a
completion report.

## 5. Assumptions

```
ASSUMPTION-1: "Frame" means full frame-to-frame wall time, not GPU time alone.
- Because:       REQ-100 is about interactive responsiveness, and the user feels the whole loop.
                 A GPU-only figure would exclude the ImGui and snap work that this measurement
                 shows is a real floor (~4 ms at any density).
- Risk if wrong: if the intent were "renderer time", the reported figures overstate the cost —
                 the verdict would only get safer, never less safe.
- Validate by:   the sweep in §2 separates the two: the ~4 ms floor at 25k is the fixed per-frame
                 cost; everything above it scales with geometry.
```

## 6. Verification

**Tests:** 4252 → **64,932 assertions**, 190 → **198 cases**, green. `tests/BenchSceneTests.cpp`
(8 cases) covers what the printed number cannot show:

- the scene contains **exactly** the requested segment count across seven targets including 250,000
  — a generator that rounded to whole contours would quietly measure a different density than the
  requirement names;
- **byte-identical across runs** — "committed bench scene" is only true if regenerating reproduces
  it exactly;
- **segment count changes density, not extent** — the §3 framing bug, pinned so it cannot return;
- contours are iso-elevation and separated in Z, so the benchmark cannot pass on a renderer that
  ignores Z (which is the bug TASK-036 just fixed);
- nearest-rank percentile picks a **real sample** rather than interpolating, and `Summarize` is
  checked on 19×10 ms frames plus one 40 ms stall — the case where a p95 verdict and the flattering
  mean disagree.

**In the running app:** driven by script, the mid-run screenshot confirms the 250k scene fills the
viewport while tilted under orbit, and the post-run screenshot confirms the drawing and camera are
restored.

## 7. Outcome

- **REQ-100 met.** With it, **every acceptance condition of REQ-058 is now met** — plan-view parity,
  snaps from an orbited camera, intersection snaps (TASK-038), per-entity elevation (TASK-036),
  screen-facing glyphs (TASK-037), suite green, frame budget.
- REQ-058 can be signed off once the spec statuses are updated by a recorded decision (§9).

## 8. Technical debt

None added. One observation recorded rather than chased: the non-linear jump between 750k and 1M
segments. It is 3–4× outside the requirement, so investigating it now would be speculative work.

## 9. SPEC GAP — the reference machine is undefined

REQ-100's statement and acceptance both say **"on the reference machine"**, and no reference machine
is defined anywhere in `spec/`. `spec/roadmap.md` §Risks even lists "define reference hardware" as an
open mitigation. A performance requirement whose reference hardware is undefined is not reproducible:
the same commit passes or fails depending on who runs it.

This task measured on the machine named in §2 and recorded it, but **the Workshop cannot name the
project's reference machine** — that is a spec decision. Two things need a recorded decision:

1. **which machine is the reference** (proposal: the machine in §2, recorded in `spec/project.md`
   §7 Constraints, since it is the only one the budget has ever been measured on); and
2. **REQ-100's traceability row**, which still reads `` `<bench-frame>` `` / `proposed` in
   `spec/requirements.md` while the requirement itself is `accepted` — it should name
   `BenchSceneTests` + the `BENCH` command and move to `accepted`.

Both are left untouched here, per CLAUDE.md's rule that the spec changes only by a deliberate
recorded decision.

> **Resolved 2026-08-12.** The user took the decision and it is recorded in `spec/project.md`'s
> decision log: the machine in §2 is named the reference machine (now in §7 Constraints, with the
> note that changing it invalidates every recorded measurement), REQ-100's traceability row moved to
> `accepted`, and **REQ-058 is signed off**. `roadmap.md` M3 is marked done and its "define
> reference hardware" risk is marked mitigated — with the residual risk kept, since the budget is
> known on exactly one machine.

---

COMPLETION REPORT — TASK-039 — 2026-08-12
- Requirements satisfied:  REQ-100 (Acceptance met: yes, on the machine recorded in §2 — pending the
                           §9 decision naming it as the reference). Closes REQ-058's last condition.
- Summary:                 A deterministic 250k-segment contour bench scene, a `BENCH` command that
                           runs a scripted orbit with vsync disabled and restores the drawing, and
                           the measurement: p95 8.93 ms against a 16 ms budget.
- Tests:                   tests/BenchSceneTests.cpp, 8 cases. 64,932 assertions / 198 cases, green.
- Verification verdict:    PASS
- Assumptions:             ASSUMPTION-1 documented; the §2 sweep separates the fixed cost from the
                           geometry-dependent one, which validates it.
- Architectural decisions: none made by Workshop. One SPEC GAP raised, not resolved (§9).
- Dependencies:            none added
- Technical debt noted:    none (§8)
- Build:                   reproducible, clean on target platform
- Docs updated:            this task log. spec/requirements.md REQ-100 traceability row and the
                           reference-machine definition are flagged in §9, deliberately NOT edited.
