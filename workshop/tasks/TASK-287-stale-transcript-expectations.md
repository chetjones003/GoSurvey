# TASK-287 — seven transcripts were asserting behaviour the product had deliberately changed

- Type:    test
- Status:  review
- Opened:  2026-10-02
- Owner:   Workshop
- GitHub:  none — found by auditing `beta`'s standing test failures

## Why

`beta` carried a set of permanently-failing headless transcripts. They had been written off as
"known failures", which is how a real regression hides: nobody looks at a list that is always red.

Every one was run and diagnosed. **None was a product bug.** All seven were testing behaviour that a
deliberate, recorded change had moved out from under them, and nobody had updated the test.

## What each one was

| Transcript | Cause |
|---|---|
| `req313-solid-isolines` | `brep::kFullCircleSegments` was raised 50 → 256 (the "torn bolt holes" fix). Hand-computed counts 102/104/116/200 became 514/516/528/1024. The **deltas** the test is actually about (+2 per ruling, +14) are unchanged. |
| `req313-solid-primitives` (a) | `EXPECT FILELACKS … "solids"` is a bare substring test. The template now ships block definitions (`_matchline_*`) that contain solids of their own, so the string appears for a reason the test was never about. The drawing's own `solids` key **is** correctly omitted. |
| `req313-solid-primitives` (b) | **REQ-361 / D-2026-09-29-b**: a new solid goes on its *object layer* (`C-SOLID`), not the current layer. `CLAYER SOLIDS-A` stopped deciding where solids land, so `LAYERSTATE SOLIDS-A OFF` hid nothing and the whole layer block was exercising an empty layer. |
| `req068-surface-selection`, `issue402-offset-ucs`, `regression-58-offset-entity-id`, `req087-feature-line-modify` | **`deaccf94` — "fix(offset): distance-first select/side loop"**. OFFSET asks for the distance *before* the entity. Four transcripts answered the distance prompt with an entity pick and then asserted a refusal the command had never reached. |
| `issue233-command-name-at-point-prompt` | **`48930b13` — "CIRCLE command ends after one circle instead of repeating"**. The transcript's premise was that CIRCLE loops, so a completed circle left it running. It is single-shot now; the interception is asserted between the centre and the radius instead, which is still a point prompt with a command genuinely active. |

One upstream commit (`deaccf94`) accounted for four of the seven.

## What changed

- The seven transcripts, each with the cause written into the file beside the change, so the next
  reader learns why the old numbers were what they were rather than only that they moved.
- `tests/headless/HeadlessDriver.cpp` — `EXPECT FILECONTAINS` / `FILELACKS` understand `\n`, `\t`,
  `\"` and `\\`. Without `\n` the needle cannot be anchored to the start of a line, so a
  document-level key and the same key ten spaces deeper inside a block definition are
  indistinguishable — and "this drawing wrote no solids array" was not expressible at all. Four
  escapes, deliberately not a regex language inside a transcript.

The `FILELACKS` assertion is now paired with a `FILECONTAINS` on a key that *is* present at the same
indent. That second line is not decoration: it proves the anchor matches document-level keys at all,
so the `FILELACKS` is a check that can fail rather than one that can only pass.

## Coverage deliberately given up, and recorded rather than dropped quietly

`req313-solid-primitives` can no longer assert that the draw batch **splits** when two solids resolve
to different colours. Under REQ-361 every solid a transcript creates lands on the same object layer,
and there is no typed or driver route to the current colour (`CadRibbonPickColor` is ribbon-only —
the same reason `CLAYER` and `LAYERSTATE` exist as driver verbs at all). The merge half still holds
the coalescer honest. Restoring the split half wants either a driver verb for the current colour or a
unit test over the assembly; it is written into the transcript as a gap.

## Found while doing this, NOT fixed here

**Two surface transcripts segfault on `beta`, intermittently.**
`req069-surface-definition-commands` and `req070-surface-styles-contours`, roughly **6 crashes in 12
runs** — sometimes one, sometimes both, sometimes neither, which is why they had not been pinned.

Verified **not** caused by anything in this task: both crash with the driver change reverted.

Narrowed by truncation: the first 70 lines of `req069` never crash; the whole 85 do. The trigger is
the `SAVEAS` / `OPEN` round trip at lines 71-76, but **only with the accumulated surface-definition
state before it** — a five-line open/create/rebuild/save/reopen does not reproduce in 8 runs.

These are new since the older `beta` this suite was last run against, and recent upstream work in the
area includes a newly vendored `csmap` library (REQ-358), the Object Layers tab (REQ-361) and the
Geolocation tab (REQ-359). Reported rather than chased: it is upstream's own code and a crash wants
its own task.

## Tests

All seven transcripts pass. Full suite **2085/2086** — the single remaining failure is one of the two
intermittent surface segfaults above, which is why the count moves between runs.
