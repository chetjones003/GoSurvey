# TASK-077 — Feature line entity: store, creation, persistence

- Type:    feature
- Status:  done (stage 1 of 3)
- Opened:  2026-08-19
- Owner:   chetjones003

## 1. Authority
- Goal:         M-Grading step 3 (`spec/roadmap.md`), decisions D-2026-08-19-a and the storage
                decision recorded in **ADR-035 (g)**, taken by the user 2026-08-19.
- Requirements: **REQ-087** (feature line entity) — `proposed`, to be promoted when this lands.
                REQ-088 (elevation editor) is a **separate task**, sequenced after this one per
                ADR-035's consequences. REQ-076 (stable ids), REQ-069 (breaklines), REQ-079 (`.gs`
                round trip), REQ-201 (report), REQ-204 (invariants) all constrain it.
- Acceptance (REQ-087, verbatim):
  - a feature line drawn with per-vertex elevations survives a `.gs` round trip byte-identically;
  - converting a closed polyline yields a closed feature line with the same vertices;
  - inserting a PI adds a vertex without changing the elevation of the existing ones;
  - adding a feature line to a surface forces triangulation edges along it, and moving the feature
    line rebuilds the surface with no user action;
  - deleting the feature line removes it from the surface's definition (REQ-069's rule);
  - a legacy `.gs` with no feature lines loads unchanged.
- Owning subsystem: `commands/`, `io/`, then `viewport/` + `ui/` in later stages.

## 2. Scope
This task is **stage 1 of three**, because ADR-035 (g) commits us to a ~612-site footprint and one
commit that large cannot be reviewed or bisected.

- **Stage 1 — this task.** The store exists, is created, persists, undoes, and is invariant-checked:
  `EntityKind::FeatureLine`, the geometry arrays, entity-id assignment, the undo snapshot, `.gs`
  read/write, `docinvariants`, extents, a `FEATURELINE` creation command and a `FEATURELINELIST`
  readout. End-to-end testable headlessly without any renderer work.
- **Stage 2 (TASK-078).** Render, pick, snap, selection, grips, transform preview, PDF plot, DXF.
- **Stage 3 (TASK-079).** Modify commands: move, copy, rotate, scale, trim, offset, join, delete —
  each either handling FeatureLine or **explicitly refusing it with a message** (REQ-201).
- **Out of scope entirely** (ADR-035 (f)): feature lines from an alignment / corridor / stepped
  offset, grading objects, styles beyond a name, weeding and supplementing factors.

## 3. Architectural boundary check
- **The architectural decision was already taken and recorded** — ADR-035, accepted 2026-08-19. This
  task implements it and makes none of its own.
- Worth restating because it is the whole risk of this task: **a missed case is silent.** A feature
  line that cannot be trimmed, or that the PDF writer skips, looks like nothing at all until someone
  needs it. ADR-035 (g) therefore makes the enumeration an acceptance condition. The checklist in §6
  is derived from counting the `userPolyline` sites, not from memory.

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | Separate store (~612 sites) or the polyline store + side table (~0 new cases)? | 2026-08-19 | **Separate store.** Asked twice: once on a ~12-site estimate, then again after counting the real footprint. The user confirmed with the measurement in front of them. ADR-035 (g). |

## 5. Assumptions
```
ASSUMPTION-1: An elevation point is a vertex carrying a flag, not a separate structure.
- Because:       it lies ON the line by construction, so including it in the plan vertex array is
                 geometrically a no-op — render, extents, snap, pick and plot need not know.
                 ADR-035 (a).
- Risk if wrong: moving a PI without re-projecting its adjacent elevation points puts a visible kink
                 in the line. LOUD, not silent — which is why this was preferred (ADR-035 (b)).
- Validate by:   stage 3's move test asserts the elevation points stay collinear after a PI moves.
```

## 6. Plan — stage 1
### The enumerated checklist (counted, not remembered)
`userPolyline*` appears 612 times across 11 files. Stage 1 covers only the data-layer subset:

| site | file | what stage 1 must add |
|---|---|---|
| store declarations x3 | `CadCommands.hpp` | `AppCommandState`, `DrawingDocument`, `DrawingGeometrySnapshot` |
| document copy in/out | `CadCommands.cpp` :72,:126 | both directions |
| undo snapshot in/out | `CadCommands.cpp` :1124,:1156 | both directions |
| `EntityKind` + `AttrsForKind` + `kEntityKindsInSweepOrder` | `CadCommands.cpp` :1187,:1194 | one case each — a **funnel**, so ids come free |
| `.gs` write + read | `GsIo.cpp` | additive arrays; legacy files lack them |
| invariants | `docinvariants.cpp` | stride `% 3`, CSR `N+1`, flags length, ids unique |
| extents | wherever polyline extents are computed | include feature lines |

Stages 2 and 3 carry the rest: 154 `SelectedEntity::Type` branches in `CadCommands.cpp`, 98 in
`CadUi.cpp`, 21 in `TransformPreview.cpp`, 11 in `CadSnap.cpp`.

### Store shape (ADR-035 (a), (c), (g))
```
featureLineOffsets : vector<int>              CSR, N+1 for N lines   (mirrors userPolylineOffsets)
featureLineVerts   : vector<float>            stride 3, XYZ          (§11.8 unchanged)
featureLineClosed  : vector<uint8_t>          per line
featureLineAttrs   : vector<EntityAttributes> per line — layer, colour, stable id
featureLineElevPt  : vector<uint8_t>          per VERTEX: 1 = elevation point, 0 = PI
featureLineInfo    : vector<CadFeatureLineInfo>  per line: name, description
```

### Test approach
- happy path = a transcript creating a feature line with per-vertex elevations, saving, reloading,
  and asserting the elevations and the name survive; plus `.gs` idempotence.
- failure mode = a **legacy `.gs`** with no feature-line arrays loads unchanged (the same class of
  regression TASK-074's fixture caught), and a deliberately-broken fixture per new invariant
  (REQ-204's rule: an oracle that has never fired is not known to be an oracle).

### Steps
- [x] 1. Store + `EntityKind` + `SelectedEntity::Type` + id funnel.
- [x] 2. Document + undo snapshot both directions.
- [x] 3. `.gs` write/read, legacy-safe.
- [x] 4. `docinvariants` rules + deliberately-broken fixtures.
- [x] 5. `FEATURELINE` create command (per-vertex elevations, like 3DPOLY) + `FEATURELINELIST`.
- [x] 6. Transcript, extents, self-verify, completion report.

## 7. Workflow-specific notes (Feature)
- Tests-first for the invariants (REQ-204 requires a breaking fixture per check) and for the legacy
  `.gs` case, which cannot be checked by looking at the screen.

## 8. Implementation log
- 2026-08-19 Opened. ADR-035 accepted the same day; storage confirmed by the user against the
  measured footprint rather than the estimate that preceded it.
- 2026-08-19 The id machinery came free: `AttrsForKind` and `kEntityKindsInSweepOrder` are a genuine
  funnel, so one case each gave feature lines stable ids, uniqueness checking and the `EnsureEntityIds`
  sweep with no per-site work. Counted the EntityKind switches first rather than assuming — there are
  only two in the tree, and every other `switch (k)` found by grep turns out to be over a DIFFERENT
  enum (annotation kind, ribbon icon, snap kind).
- 2026-08-19 The `.gs` READER needed guards the polyline block does not have: `doc["polylineOffsets"]`
  is unguarded, which is safe only because that key has always existed. Feature-line keys are new, so
  an unguarded read would throw on every drawing written before today.
- 2026-08-19 The elevation-point flag array got its own invariant because its failure is silent: a
  short array reads as "all PIs", so elevation points vanish while the plan geometry still looks
  right. Fixture added, and a valid-drawing counterweight beside it so the other fixtures cannot pass
  for the wrong reason.

## 9. Self-verification
- [x] build-project        — PASS (clean, no new warning)
- [x] architecture-review  — PASS; implements ADR-035, makes no decision of its own
- [x] code-review          — PASS
- [x] dependency-audit     — n/a
- [x] performance-review   — n/a (no per-frame work added)
- [x] testing              — PASS (455 ctest cases; 434 Catch2 cases / 213,774 assertions)

## 10. Verification result
- Submitted: 2026-08-19
- Verdict:   **PASS for stage 1.** The data layer is complete and exercised end to end headlessly.
             **A feature line is not yet visible or selectable** — render, pick, snap and the modify
             commands are stages 2 and 3. That is a deliberate seam, not an omission, but it means
             FEATURELINE is not usable from the GUI yet and the app should not be shipped here.

## 11. Outcome
- Requirements satisfied: REQ-087 partially — the entity exists, persists and undoes. The remaining
  acceptance conditions (convert from objects, insert PI, surface breakline, delete-prunes) need
  stages 2-3 and REQ-087 stays `proposed` until then.
- Tests added: `transcripts/req087-feature-line-entity.txt` (45 steps) + 7 `DocInvariantsTests`
  cases, one per new structural rule plus a valid-drawing counterweight (REQ-204).
- Architectural decisions: none made by Workshop; ADR-035 was accepted first.
- Docs updated: this log; ADR-035 written and accepted.
- Done: 2026-08-19 (stage 1).
