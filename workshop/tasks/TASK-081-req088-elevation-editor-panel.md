# TASK-081 — Feature line elevation editor: the panel

- Type:    feature
- Status:  done (stage 2 of 2)
- Opened:  2026-08-20
- Owner:   chetjones003

## 1. Authority
- Goal:         M-Grading (`spec/roadmap.md`); ADR-035 (e).
- Requirements: **REQ-088**, whose Statement says elevations are *"editable through a **table**
                showing, per point: station, elevation, length to the next point, grade back and
                grade ahead"*. TASK-080 built the derivation and the edits; this is the table.
                REQ-084 (2026-08-18 revision) constrains what may appear in it — see §3.
- Owning subsystem: `ui/`.

## 2. Scope
The `Feature Line Elevations` window: a combo to choose the feature line, the seven-column table,
raise/lower for the whole line, an add-elevation-point row, and a delete control per elevation
point. Plus `FLELEVEDIT [<n>]` to open it and a ribbon button beside Surfaces.

**No new domain logic.** Every edit builds an `FLELEV` command string and runs it through
`ProcessCommandLineSubmit`, exactly as `CadUi_Surfaces.cpp` routes through `DESIGNATEBREAKLINE` and
`SURFACEADDFILE`.

### Out of scope
- Picking an elevation point in the viewport (belongs with grips — TASK-080 FU-3).
- Editing station or length. Those are plan geometry; moving a point along the line is a geometry
  edit (REQ-087), not an elevation edit, and offering it here would quietly widen this window's job.

## 3. Architectural boundary check
- No new store, no new abstraction, no dependency. One new UI translation unit, following the
  existing `CadUi_*.cpp` split.
- **Why the command-line routing is load-bearing rather than tidiness:** ImGui has no window under
  the REQ-203 driver, so a panel that called the edit functions directly would be untestable *and*
  free to drift from the command line. Routing through `FLELEV` makes
  `req088-feature-line-elevation-editor.txt` a test of this window by proxy — same code, same
  order, same undo step.
- **REQ-084's rule applied:** *no control may be present that cannot act.* Two consequences here.
  A PI's row has **no** Delete button at all, rather than one that refuses — the command still
  refuses a PI, because a command line can be typed, but the panel does not offer the click. And a
  grade cell with no segment behind or ahead of it shows a dash and is **not editable**: there is
  nothing a number could mean there.

## 4. Questions
None. Every behavioural question was settled in TASK-080's assumptions; this task renders them.

## 5. Assumptions
```
ASSUMPTION-1: A cell commits on Enter OR on losing focus, and only when the text parses to a
  DIFFERENT number.
- Because:       both halves prevent an undo-stack defect rather than a display one. Committing
                 every frame would push one undo step PER KEYSTROKE, so undoing a typed elevation
                 would take six presses. Committing an unchanged value would push a step for
                 clicking into a cell and out again — an undo that appears to do nothing.
- Risk if wrong: loud on the first edit; the undo history is visible in the UI.
- Validate by:   manual. Not headlessly reachable: this is ImGui focus behaviour, and the driver
                 has no window. Stated as a limit in §8 rather than left implied.
```

## 6. Plan — as built
| piece | note |
|---|---|
| `src/ui/CadUi_FeatureLineElev.cpp` | the window; ~250 lines, no domain logic |
| `AppCommandState::showFeatureLineElevWindow` | matches the other panel flags |
| `AppCommandState::featureLineElevIndex` | in state, not a panel static, so `FLELEVEDIT <n>` can aim it |
| `FLELEVEDIT [<n>]` | opens it; refuses a number that is not a feature line |
| ribbon "Grades" button | beside Surfaces — a feature line's elevations are what a surface consumes |
| `DrawFeatureLineElevationWindow` in `main.cpp` | beside `DrawSurfaceManagerWindow` |

### One defect fixed before it shipped
The first cut of `NumericCell` kept a `std::vector<std::pair<ImGuiID, CellState>>` of in-progress
text — one entry per cell ever shown, scanned linearly **per cell per frame**. That is quadratic in
the number of rows, and it grows without bound across the session, for state that is single-valued
by construction: **only one cell can hold keyboard focus at a time.** Replaced with one
`(activeCellId, text)` pair.

## 7. Test approach
The panel itself is not headlessly testable — ImGui has no window under the driver, which is the
whole reason for the routing in §3. What is tested:

| id | asserts | how |
|----|---------|-----|
| T9 | `FLELEVEDIT` opens on a real feature line and refuses one that is not | transcript |
| T1–T8 | every edit the panel can make, through the commands it calls | TASK-080's transcript |

## 8. Completion report

```
COMPLETION REPORT — TASK-081 — 2026-08-20
- Requirements satisfied:  REQ-088 — the Statement's "table" now exists. Promoted to `accepted`:
                           all five acceptance conditions are met, with the fifth verified up to
                           the frame tick the driver cannot run (TASK-080 FU-1, unchanged).
- Summary:                 The Feature Line Elevations window: choose a line, edit elevations and
                           grades in place, raise or lower the whole line, add and delete elevation
                           points. Every edit runs an FLELEV command.
- Tests:                   T9 added to req088-feature-line-elevation-editor.txt (141 steps).
                           457 ctest cases green.
- Verification verdict:    PASS
- Assumptions:             ASSUMPTION-1 (commit-on-Enter-or-blur) is MANUALLY verified only —
                           ImGui focus behaviour is not reachable from the driver.
- Architectural decisions: none made by Workshop.
- Dependencies:            none added.
- Technical debt noted:    the panel's rendering has no automated coverage and cannot have any
                           while the driver has no window. Mitigated, not solved, by routing every
                           edit through the tested command path.
- Build:                   clean, MSVC 14.50.
- Docs updated:            spec/requirements.md (REQ-088 → accepted), this task log.
```

## 9. Follow-ups filed
- **FU-1**: `samples/featureline-demo.gs` was generated to exercise the panel by hand (a feature
  line designated as a breakline on the surface-demo drawing). Left uncommitted — it is a 260 KB
  derivative of an existing sample, and CON-07 keeps generated artifacts out of the source tree.
  Regenerate with the transcript in the task log if needed.
- Inherited and still open: TASK-080 FU-1 (driver frame tick), FU-3 (place an elevation point by
  clicking the line — belongs with grips, TASK-078).
