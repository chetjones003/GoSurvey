# TASK-027 — Fix SHX arc decoding (octant, fractional, bulge, multi-bulge) + smooth curve rendering

- Type:    bug
- Status:  done
- Opened:  2026-07-30
- Owner:   chetj

## 1. Authority
- Goal:         interoperable DXF — imported text reads as it does in AutoCAD
- Requirements: REQ-049 (accepted — "**Stroke (SHX) fonts** plot as their actual stroke geometry");
  REQ-023 (accepted — DXF round-trip). Follows TASK-026, which made the text visible and exposed this.
- Constraints:  ADR-012 / ADR-022 (SHX geometry module sits below UI; pure geometry, no imgui)
- Acceptance:   SHX glyphs render as the real stroke geometry of the .shx file — i.e. what AutoCAD draws.
- Owning subsystem: `src/font/ShxFont.cpp` — the shape-bytecode interpreter in `Font::buildGlyph`.

## 2. Scope
- In scope:  decoding of the four arc-bearing shape opcodes: `0x0A` octant arc, `0x0B` fractional
             arc, `0x0C` bulge arc, `0x0D` multi-bulge list.
- Out of scope:
  - `Shx::Resolve`'s search path — see the observation in §11; that is a packaging/licensing call.
  - Non-arc opcodes (`0x03`/`0x04` scale, `0x05`/`0x06` stack, `0x07` subshape, `0x09` multi-vector),
    all verified correct by hand-decode while tracing this.
- Smallest change: four localised corrections in one function. No API, data-model or renderer change.

## 3. Architectural boundary check
- New abstraction / layer / dependency / ownership change / global state / public-API or data-format
  change / unspecified algorithm?
    - [x] No — proceed. Conformance fix to an existing decoder, inside the owning module.

## 4. Questions
None — each defect was confirmed by hand-decoding real glyph bytecode against the AutoCAD shape spec,
then by rendering the glyph.

## 5. Assumptions
```
ASSUMPTION-1: in the 0x0A/0x0B control byte, bit 7 is a direction FLAG and bits 6-0 carry the
  magnitude (start octant in 6-4, octant count in 3-0).
- Because:     the spec writes the operand as "(-)0SC", which reads as a sign character followed by
               the octal-ish digits — not as a two's-complement integer.
- Risk if wrong: clockwise arcs decode to the wrong start octant.
- Validate by: hand-decode + render (done). isocp 'P' contains 0A 0A A4: under the two's-complement
  reading, abs((int8)0xA4) = 92 -> start octant 5; under the flag reading, 0xA4 & 0x7F = 0x24 ->
  start octant 2. Only octant 2 puts the bowl's centre at (16,28) so the arc closes on the stem at
  (16,18) and the pen lands on the advance point 32 that the glyph's own trailing move implies.
  Rendering confirms it. Note the two readings coincide for bytes < 0x80, so only CLOCKWISE arcs
  were ever affected — which is why romans/simplex/txt (all CCW arcs) showed no change at all.
```

## 6. Plan
- Approach: correct each opcode's decode in place; verify each against a real glyph that uses it.
- Files/functions to touch: `src/font/ShxFont.cpp` — `bulgeTo`, and cases `0x0A`, `0x0B`, `0x0D`.
- Test approach: happy path = glyphs using each opcode render correctly; failure mode = fonts that
  use none of the affected paths are byte-for-byte unchanged.
- Steps:
  - [x] hand-decode a broken glyph to isolate each defect
  - [x] fix, re-render, and diff every shipped font before/after
  - [x] rebuild + suite

## 7. Workflow-specific notes (Bug)
- Reported: after TASK-026 the drawing's text appeared, but glyphs were malformed — 'P' had a stray
  tail, '2' was shrunken and raised, and letter spacing was ragged.
- Root causes — four independent defects in the same decoder:

  | # | opcode | defect | effect |
  |---|--------|--------|--------|
  | 1 | `0x0A` octant arc | `abs((int8_t)b)` used for the magnitude instead of `b & 0x7F` | clockwise arcs got the wrong start octant → wrong centre, wrong end, wrong pen position → **wrong advance**, hence the ragged spacing |
  | 2 | `0x0C` bulge arc | centre placed at `mid − leftNormal×apothem` | mirrored centre: every bulge arc swept away from its own end point |
  | 3 | `0x0B` fractional arc | same sign defect as #1 | mangled glyphs |
  | 4 | `0x0D` multi-bulge | consumed 3 bytes for the `(0,0)` terminator | desynchronised the rest of the glyph; also disagreed with `CommandSpan`, which already assumed a 2-byte terminator |

  For #2, `R` and `apo` both carry the sweep's sign, so `mid + leftNormal×apo` is correct for both
  directions and for major (>180°) arcs. Checked algebraically at ang = ±90° and +270°: the arc
  lands exactly on its end point in all three; the old form landed on a mirrored point every time.
- Regression test fails-before? Yes — demonstrated by A/B rendering, §9.

## 8. Implementation log
- 2026-07-30 hand-decoded isocp 'P' (`02 08 06 02 01 08 00 24 08 0A 00 0A 0A A4 08 F6 00 02 08 1A EE 00`)
  and '2'; isolated defects 1 and 2. Found 3 and 4 by reading the neighbouring cases for the same
  class of error, then confirmed both against glyphs that actually use them.
- 2026-07-30 all four fixed; every shipped font diffed before/after; rebuild + suite green.

## 9. Self-verification
- [x] build-project        — PASS (release + debug link clean)
- [x] architecture-review  — PASS (module stays pure geometry; no imgui, no new surface)
- [x] code-review          — PASS (each fix is local and carries a comment stating the failure it prevents)
- [x] dependency-audit     — n-a
- [x] performance-review   — n-a (same work per glyph; glyphs are cached)
- [x] testing              — PASS. `GoSurveyTests` green (611 assertions / 98 cases). Glyph-level A/B
      via a probe (`scratchpad/shxprobe.cpp`) that renders the **real** `Shx::Font` output as ASCII art,
      run against both this code and `git show HEAD:src/font/ShxFont.cpp`:

      | glyph | opcode | before | after |
      |---|---|---|---|
      | isocp `P` | `0x0A` CW | no bowl, stray diagonal, advance **46.1** | correct bowl + stem, advance **32.0** (= the glyph's own trailing move) |
      | isocp `2` | `0x0C` | squashed, strokes crossing | correct curved head + diagonal + base bar |
      | isocp `S` `O` `8` `3` | `0x0A`/`0x0C` | malformed | correct |
      | complex `Ф` (U+0424) | `0x0D` | three stray strokes | correct oval with stem |
      | complex `€` (U+20AC) | `0x0B` | mangled, strokes reversed | correct C + two bars |

      Non-regression: full glyph output diffed before/after for isocp, romans, simplex, txt, romand,
      complex, gothice, italicc, scripts over `A-Z`, `0-9`, `a-z` and punctuation — **only isocp
      changed** (273 output lines), every other font byte-identical. Expected: the sign defects bite
      only on clockwise arcs and those fonts use CCW arcs exclusively.
- [x] Opcode coverage confirmed before changing anything (`scratchpad/opcount.cpp`, walking all 79
      shipped fonts / 9815 glyphs with correct command spans): `0x0A` ×1678, `0x0B` ×36, `0x0C` ×1178,
      `0x0D` ×171. No fix here is speculative — every path touched is exercised by real font data.

## 9b. Phase 2 — curve smoothness (same report thread)
With the arcs decoding correctly, the glyph shapes were right but curves still read as polygons.
Two separate causes, both fixed:

1. **Coarse flattening** (`src/font/ShxFont.cpp`). All three arc opcodes used a fixed segment count —
   `cnt * 3` for `0x0A`/`0x0B` (3 per 45° octant ≈ 15°) and `≈17°` steps for `0x0C`. Replaced with
   `ArcSegments(radius, sweep, sagitta)`, which picks the count from a chord-sag tolerance of
   `capHeight / 3000` font units, clamped to [2, 96]. Being relative to cap height it flattens every
   font equally finely regardless of its units, and small arcs still stay cheap.
2. **Per-segment draw** (`src/ui/ShxDraw.cpp`). `DrawText` issued an `AddLine` per segment, so each
   segment was an independently anti-aliased quad with no join handling — leaving a notch on the
   outside of every bend and a double-blended seam on the inside (the nicks visible along curves).
   Now one `AddPolyline` per stroke, which ImGui joins properly and which emits *fewer* vertices.

Measured on isocp — `maxTurnDeg` is the sharpest angle between consecutive segments, i.e. the
faceting angle (90° on `P`/`D` is a genuine stem-to-bowl corner, not faceting):

| glyph | HEAD (broken + coarse) | now (fixed + smoothed) |
|---|---|---|
| `O` | verts=27  maxTurn **52.5°** | verts=65  maxTurn **5.8°** |
| `S` | verts=28  maxTurn 143.3° | verts=66  maxTurn **7.5°** |
| `3` | verts=54  maxTurn 142.5° | verts=62  maxTurn **6.4°** |
| `8` | verts=52  maxTurn 52.5° | verts=115 maxTurn **6.5°** |
| `P` | verts=16  maxTurn 142.5° | verts=35  maxTurn 90.0° (real corner) |

Cost, over `A-Z a-z 0-9 .,-()`: isocp 1068 → 1610 glyph vertices (+51%, ~8 extra per glyph);
romans / simplex / complex **unchanged** (no arc opcodes in that range). Glyphs are built once and
cached per font, so this is not per-frame work — and the `AddPolyline` change reduces the per-frame
vertex count outright. Advances are identical before/after, as they must be: finer flattening adds
intermediate points but keeps the exact arc endpoints.

## 9c. Phase 3 — sharp-joint nicks
With SHX runs finally reaching the screen (TASK-028), a third artifact was visible: small dark nicks
inside the strokes, worst on the serifed `romand` / `romanc`.

Cause: ImGui's thick anti-aliased polyline miters **every** joint. At a near-reversal the two outer
offsets cross each other, so the quad self-intersects and leaves an unfilled sliver. Phase 2's move to
one polyline per stroke was still right — it removed the per-segment seams — but it handed ImGui
joints of up to 170° to mitre.

Fix: split a stroke into separate polylines wherever it turns by more than 75° (`Font::buildGlyph`'s
`flush`). Only gentle joints are then mitred, and the two pieces overlap at the cusp — which is what a
corner should look like. Done at build time, so it is free per frame, and consumers that walk strokes
segment by segment (the PDF plot path) see identical segments either way.

Sharpest joint remaining inside any single polyline, over `ROMANDCSIMPLEXTABGQW`:

| font | before | after | strokes |
|---|---|---|---|
| romanc  | **170.5°** | 63.4° | 111 → 131 |
| simplex | **159.1°** | 45.0° | 29 → 49 |
| txt     | **153.4°** | 45.0° | 32 → 58 |
| isocp   | **150.9°** | 7.5°  | 28 → 50 |
| romand  | 90.0°  | 74.5° | 96 → 117 |
| romans  | 45.0°  | 45.0° | 49 → 49 (no cusps — nothing to split) |

Every result is at or under the 75° threshold, as designed. The extra strokes are extra `AddPolyline`
calls, but each is shorter; total vertices are unchanged.

## 10. Verification result
- Submitted:  2026-07-30
- Verdict:    PASS (all three phases; suite green after each)
- Findings:   none outstanding

## 11. Outcome
- Requirements satisfied: REQ-049 (Acceptance met: yes — SHX glyphs now match the .shx stroke geometry)
- Tests added:            none automated — same DEBT-1 shape as TASK-026, though note `ShxFont.cpp` is
  pure geometry with no UI/GL dependency, so unlike `DxfIo.cpp` it **could** be linked into
  `GoSurveyTests` directly. A glyph-geometry test (e.g. isocp 'P' advance == 32, arc endpoints within
  tolerance) is cheap and would have caught all four of these. Recommended as the next testing task.
- Observation (NOT actioned — a visual decision, not a defect):
  SHX stroke width is `max(1, fontPx * 0.05)` at every draw site, i.e. 5% of cap height, so it grows
  with the text. AutoCAD draws SHX strokes at the entity's **lineweight** — typically hairline —
  independent of text height, which is why its samples look much finer than ours at the same size.
  Matching it would mean deriving the width from `EffectiveEntityLineweightMm` instead. That changes
  the weight of all stroke text, including the DXF-imported text already signed off in TASK-026/027,
  so it wants its own task and an explicit call from the user.
- Observation (NOT actioned — out of scope, screen rendering was the report):
  the PDF plot path (`src/io/PdfPlot.cpp`, `emitShxLine`) also walks glyph strokes segment by segment
  and calls `addSeg` per segment rather than emitting one path per stroke. It inherits the finer
  flattening automatically, so plotted curves are smoother too; whether the per-segment emission
  shows seams in a PDF depends on the line cap/join state, which was not investigated here.
- Observation (NOT actioned — needs a decision outside the Workshop):
  `Shx::Resolve` (`src/font/ShxFont.cpp`) searches only installed Autodesk font folders
  (`C:/Program Files/Autodesk`, `Common Files/Autodesk Shared`, `%USERPROFILE%/AppData/Roaming/Autodesk`).
  It does **not** search the repo's own `resources/fonts/`, which holds 79 `.shx` files — and CMake
  does not copy that folder beside the executable either (only `icons`, `hatches` and the template).
  So on a machine without AutoCAD installed, every SHX style silently falls back to a substituted TTF
  and this drawing would not look like AutoCAD at all. Whether to search and ship those files is a
  redistribution/licensing question for Autodesk-authored fonts, so it is explicitly left alone here.
- Docs updated:           none
- Done:                   2026-07-30
