# TASK-040 — REQ-064: shaded visual styles

- Type:    feature
- Status:  done — REQ-064 delivered for model space (see §9)
- Opened:  2026-08-12
- Owner:   Workshop

## 1. Authority

- Requirements: **REQ-064** (accepted 2026-08-12). Step 1 of the M-Models milestone.
- Architecture: **ADR-026 (e)** — visual styles turn depth testing on; 2D Wireframe keeps it off.
  **ADR-026 (f)** — a second shader, not a rendering abstraction.
- Supersedes **ADR-025 ASSUMPTION-1**, which left depth testing disabled *pending exactly this
  requirement*.
- Constraints: REQ-100 (frame budget), architecture §11 invariants.
- Owning subsystem: Renderer (draw), UI (selector + command), IO (persistence).

## 2. Scope

- **In scope:** a `VisualStyle` on the model viewport — 2D Wireframe / Hidden / Shaded — with depth
  testing, a lit triangle shader, a `VISUALSTYLE` command, a ribbon selector, and persistence.
- **Out of scope:** meshes (REQ-063 — there is nothing but filled regions to shade yet, see §5);
  per-paper-viewport style (§6).

## 3. Architectural boundary check (workshop/workflow.md §4)

- [x] **No** — proceed. Every decision was taken in ADR-026 before this task opened. The style is a
      field on `RenderTuning`, which is already the "how to draw" parameter object; the lit shader is
      a second concrete program beside the existing three, not an abstraction (§11.4); no new global,
      dependency or backend.

## 4. The constraint that shapes the whole change

REQ-064's first acceptance condition is that **2D Wireframe renders pixel-identical to the
pre-change build** — the same parity gate REQ-058 was held to, because every existing drawing is a
2D wireframe drawing.

The renderer's current appearance is **entirely order-dependent**: PDF underlays, then grid, then
filled regions ("drawn under the linework"), then committed geometry, then hover, selection, rubber,
window-select, transform previews, survey markers, snap glyph, gizmo. Nothing writes depth
(`glDepthMask(GL_FALSE)`), so the last thing drawn wins.

That makes the safe design obvious: **do not reorder a single draw call.** Depth state is the only
thing that changes, and in 2D Wireframe it does not change at all — so parity holds *by
construction* rather than by careful re-checking.

For Hidden and Shaded the passes split in place:
- **depth-tested** (writes depth): PDF underlays, grid, filled regions, committed lines/circles,
  survey markers — the things that represent geometry at a real elevation;
- **never depth-tested**: hover, selection, rubber, window-select, transform previews, the snap
  glyph and the axes gizmo. These are UI. A selection highlight that disappears behind the object it
  is highlighting is a bug, not occlusion, and the same goes for a snap marker.

## 5. Assumptions

```
ASSUMPTION-1: Shaded has almost nothing to shade until REQ-063 lands, and that is expected.
- Because:       today's only filled surfaces are CadFilledRegion (solid hatch). Everything else is
                 edges. So "Shaded" currently means "lit hatches"; the visible payoff of this task
                 is Hidden (real occlusion under orbit), and the lit shader is the piece REQ-063
                 needs to exist before meshes arrive.
- Not vacuous:   a horizontal hatch lit by a headlight DIMS as the view orbits toward edge-on
                 (dot(N, viewDir) → 0), which is a real, checkable effect and is what §8 verifies.
- Risk if wrong: none to correctness. The risk is judging the feature by an invisible result, which
                 is why this is written down rather than discovered later.
- Validate by:   the orbit-dims-the-hatch check in §8, plus a unit test of the lighting term.
```

```
ASSUMPTION-2: The filled-region stencil pass must not write depth in its parity phase.
- Because:       solid fills are drawn in two passes — pass 1 writes even-odd parity into the
                 stencil with the colour mask off, pass 2 draws a covering quad limited by that
                 stencil. Pass 1's fan covers area that is NOT part of the final region, so letting
                 it write depth would occlude geometry through holes in the hatch.
- Risk if wrong: geometry vanishes behind the bounding box of a hatch with islands.
- Validate by:   pass 1 sets glDepthMask(GL_FALSE) and keeps the depth TEST on; pass 2 restores the
                 write. Checked visually against a hatch with a hole.
```

## 6. Questions / deferrals

REQ-064 says the style is per-viewport and "each paper-space viewport (REQ-061) carries its own".
**REQ-061 is not implemented** — paper-space viewports have no camera yet, so there is no per-viewport
view state for a style to attach to. This task implements the model-space style and the persistence;
the per-paper-viewport style lands with REQ-061, which is where the state it belongs to is created.
Recorded as a deferral rather than silently narrowing the requirement.

## 7. Plan

1. `VisualStyle` enum in `CadEntities.hpp` (dependency-free, already the shared entity header).
2. `AppCommandState::viewportVisualStyle` + `RenderTuning::visualStyle`.
3. Renderer: depth enable/disable around the two groups from §4; a lit triangle program used for
   filled regions in Shaded.
4. `VISUALSTYLE` / `VS` command with `2D` | `HIDDEN` | `SHADED`, bare form reporting the current
   value (the TRIMSTATE/ELEV precedent). Scriptable, which is what makes §8 verifiable.
5. Ribbon View-panel selector.
6. Persistence: `.gs` settings + user prefs, additive, no format version bump.

- Test approach: the lighting term is pure and unit-tested. The GL state machine is not linkable
  (TASK-035 §11), so parity and occlusion are verified by driving the built app — the same
  before/after screenshot method as TASK-036/037.

## 8. Verification

**Tests:** 64,934 → **64,992 assertions**, 198 → **203 cases**, green. `tests/VisualStyleTests.cpp`
covers the one piece of this feature that is pure and has a real bug class behind it — the command's
input parsing, where a mistyped alias silently selects the wrong style and is invisible in review.
`VisualStyleFromName` was moved into the dependency-free `CadEntities.hpp` for exactly that reason.
Cases: every accepted spelling maps correctly; an unrecognised style **leaves the current one
untouched** (REQ-201 — a typo must not change the view); a null destination is refused; the
canonical names the ribbon shows round-trip back through the parser; and `Wireframe2D == 0`, which
is load-bearing for persistence (a pre-REQ-064 file has no style key, and the zero value must mean
"the classic view").

**The parity gate, measured rather than eyeballed.** Scripted: draw two overlapping rectangles at
ELEV 0 and ELEV 40, hatch both, orbit, then cycle
2D Wireframe → Hidden → Shaded → 2D Wireframe, screenshotting each. Comparing the drawing canvas
byte-for-byte:

| Comparison | Differing bytes |
|---|---|
| **wireframe → … → back to wireframe (the REQ-064 parity gate)** | **0** |
| wireframe vs Hidden | 7,554 |
| Hidden vs Shaded | 2,794,980 |

Zero. Returning to 2D Wireframe reproduces the canvas exactly, which is the acceptance condition
stated as a number instead of an opinion — and the other two rows confirm the styles are not
no-ops while doing it.

Two false starts are worth recording, because both would have produced a passing-looking result
that proved nothing:
- The first run compared **896 differing bytes** and I nearly took it as a parity failure. Localising
  the pixels put them in a 138×134 box centred exactly on the cursor: the **crosshair**, which is
  drawn at the pointer. Fixed by parking the cursor off-canvas before every shot.
- The first run also reported Hidden and Shaded as **byte-identical**. They were — because the script
  drew rectangles but never hatched them, so there were no filled surfaces: nothing for depth
  testing to occlude with and nothing to light. That is ASSUMPTION-1 arriving as a test artefact.

**ASSUMPTION-1 validated quantitatively.** A horizontal hatch, Shaded, sampled at its brightest fill
pixel: **199 in plan view → 55 tilted toward edge-on**. The predicted floor is
`ambient × colour = 0.25 × 217 ≈ 54`. The lighting is live and lands on its ambient floor within a
count, which is a stronger check than "it looks shaded".

**REQ-100 re-measured, because this task turns depth testing on.** At 250,000 segments:

| Style | p95 |
|---|---|
| 2D Wireframe | 8.25 ms |
| Hidden (depth testing on) | 10.30 ms |

Depth testing costs ~2 ms — about 25% — and the 16 ms budget still passes with ~36% headroom. Worth
knowing before REQ-063 adds meshes on top of it.

## 9. Outcome

- REQ-064 delivered for model space. Acceptance met, with one condition **not yet verifiable**:
  "in Shaded, a curved surface shows a lighting gradient" — there are no curved surfaces to shade
  until REQ-063. The lighting itself is verified by the 199→55 measurement above; the gradient
  across one surface needs meshes. Recorded rather than claimed.
- Per-paper-viewport style deferred to REQ-061 (§6), which is where the state it attaches to is
  created.
- Next: **REQ-063**, the mesh entity.

---

COMPLETION REPORT — TASK-040 — 2026-08-12
- Requirements satisfied:  REQ-064 (Acceptance met: yes for model space; the curved-surface gradient
                           condition awaits REQ-063 — see §9). Supersedes ADR-025 ASSUMPTION-1.
- Summary:                 2D Wireframe / Hidden / Shaded on the model viewport, with depth testing,
                           a diffuse headlight shader, a VISUALSTYLE command, a ribbon selector and
                           persistence. 2D Wireframe is byte-identical to the previous renderer.
- Tests:                   tests/VisualStyleTests.cpp, 5 cases. 64,992 assertions / 203 cases, green.
- Verification verdict:    PASS
- Assumptions:             ASSUMPTION-1 validated (199 → 55 fill brightness under orbit);
                           ASSUMPTION-2 (stencil parity pass must not write depth) implemented and
                           reasoned, but only exercised by hatches without islands so far.
- Architectural decisions: none made by Workshop — all taken in ADR-026 before this task opened.
- Dependencies:            none added
- Technical debt noted:    none new. Survey markers are deliberately non-occluded (documented at the
                           overlay boundary); revisit once meshes exist.
- Build:                   reproducible, clean on target platform
- Docs updated:            this task log; spec statuses flipped to accepted on the same decision.

## 10. Progress

- 2026-08-12 — task opened; REQ-063/064/065 + ADR-026 accepted the same day. Implemented, verified,
  REQ-100 re-measured under depth testing.
