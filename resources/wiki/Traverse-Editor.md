# Traverse Editor

**Ribbon:** Survey → Traverse

![Traverse Editor](wiki-img:10-traverse.png)

*The Traverse Editor after importing `samples/closed_loop_trav.fbk`: starting station, three legs
with their computed bearings and coordinates, and the closure summary in green —
`ΔE=0.0066 ΔN=0.0056 Linear=0.0087 1:252482`.*

The Traverse Editor works from **raw field observations** — horizontal angles, distances, vertical
angles — rather than reduced coordinates. It computes traverse coordinates, runs a least-squares
adjustment, reports closure with per-observation residuals, and commits the result to the drawing
as survey points.

The editor has two tabs over the same traverse data:

- **Form** — the table/panel view described below: starting station, legs, side shots, GPS
  vectors, and the closure/least-squares dialog.
- **Command** — a plain-text view of the same traverse, in GoSurvey's own traverse command
  language (see [Command Text editor](#command-text-editor) below). Either tab edits the real
  traverse; switching tabs never loses work.

On top of manual entry in either tab, four raw-format importers (covering five distinct field-data
sources — see [Importing raw data](#importing-raw-data)) can load a traverse directly from a file.

---

## Starting station

Before any legs, set where the traverse begins.

| Field | Meaning |
|---|---|
| **Start ID** | Point number for the starting station |
| **Easting** · **Northing** · **Elevation** | Coordinates of the starting station |
| **Ref Bearing°** | *Reference orientation at the start station (° CW from N).* This is the azimuth of the backsight direction, or any reference mark. If you know the first leg's bearing directly, enter it here — the first leg's bearing then becomes this value plus the first row's H.Angle |
| **Closed Loop** | Tick when the traverse returns to the start station. Required for closure analysis |

Labels stack above their inputs so long field names stay readable.

---

## Legs

Press **+ Add Leg** to add a traverse leg; **- Remove Last** removes the last one; **Clear All**
empties the traverse.

### The collapsed summary row

| Column | Meaning |
|---|---|
| **#** | Leg number, with the expand arrow |
| **Stn ID** | Station number this leg arrives at |
| **Desc** | Station description |
| **H.Angle°** | Horizontal angle, averaged over all observation sets |
| **H.Dist** · **S.Dist** | Horizontal and slope distance |
| **V.Angle°** | Vertical angle |
| **Z** | Zenith flag for this leg |
| **Bearing°** | Computed bearing of the leg |
| **ΔE** · **ΔN** · **ΔZ** | Computed components |
| **Easting** · **Northing** · **Elev** | Computed coordinates of the station |
| **Status** | Per-leg state, including `✓ accepted` after a least-squares result is applied |

The summary row is **view-only on purpose** — *"Expand this leg to view and edit its individual
observations."* Edits are made in the observation sets, so a reduced value can never be typed over
by accident.

### Observation sets

Expand a leg to reach its observations:

```
Leg 2  →  Station 103 — observations  (2 sets)
```

| Field | Meaning |
|---|---|
| **F1 Hz°** / **F1 VA°** | Face 1 horizontal and vertical angle |
| **F2 Hz°** / **F2 VA°** | Face 2 horizontal and vertical angle |
| **Zenith°** checkbox | *Checked = zenith angle (90° is level). Unchecked = elevation angle (0° is level)* |
| **Horizontal distance** | Direct measurement |
| **Slope (EDM) distance** | Requires a vertical angle to reduce to horizontal |

**+ Add Observation** appends another set to the leg. **Remove this observation set** deletes one.

> *The leg's angle and distance re-average over all sets.* Adding or removing a set re-derives the
> leg immediately — there is no recompute step for this.

Below the sets the editor shows **Reduced (face-averaged) per-set statistics** — a small table of
**Quantity / N / Mean / Sum / Std Dev** for `Horizontal°`, `Distance`, and `Zenith°` — and then the
reduction itself:

```
Horizontal distance: 696.1612    Slope distance: 696.1613    (zenith 90.04155°)
```

The **Std Dev** column is the fastest read in the dialog: a set that disagrees with its siblings
shows up here before it ever reaches the closure.

---

## Side Shots and Check Shots

A **side shot** is a shot from an already-occupied station that does not advance the traverse — a
radial detail point, like a building corner or a utility cover shot from a station you are already
standing on. GoSurvey's traverse model is a sequential chain of legs (each one's foresight becomes
the next one's occupied station), so side shots live in their own table rather than the Legs table:
they can be shot from *any* earlier station, not just the chain's current end.

| Column | Meaning |
|---|---|
| **At** | The already-occupied station the shot was taken from |
| **Point** | The shot's own point number |
| **Desc** | Description |
| **H.Angle°** | Horizontal angle from that station's backsight |
| **Dist** | Horizontal distance |
| **Check** | Tick to flag this as a **check shot** — see below |
| **Easting** · **Northing** | Computed coordinates |
| **Discrepancy** | Only for a check shot with known coordinates — see below |

**+ Add Side Shot** defaults **At** to the last occupied station; edit it to shoot from any earlier
one. **- Remove Last Side Shot** removes the most recently added row.

A **check shot** (tick **Check**) is the same kind of shot, flagged as a repeat observation meant
to verify a setup rather than record a new point — typically a re-shot of the backsight or another
already-known control point. If you also know that point's real coordinates, enter them so GoSurvey
can report the **Discrepancy** (the straight-line distance between where the shot computes to and
where the point is actually known to be) — a quick way to catch a bad setup before it propagates
into the rest of the traverse. Leaving the known coordinates blank still computes and displays the
shot normally; only the discrepancy comparison is skipped.

---

## GPS Vectors

A GPS vector ties two stations together with a GPS baseline instead of a turned angle and a taped
or EDM distance — the raw form a GPS/GNSS processing package (or an importer like StarNet, see
below) reports it in is an **ECEF delta**: the straight-line X/Y/Z difference between the two
stations in Earth-Centered-Earth-Fixed meters, the same global 3D coordinate frame GPS satellites
themselves use. GoSurvey's traverse is a local, flat easting/northing/elevation job, so a GPS
vector has to be rotated from that global frame into your local one before it means anything on the
page.

That rotation needs an **origin** — a latitude/longitude GoSurvey can anchor the local
east/north/up directions to. GoSurvey has no coordinate datum of its own (no "where on Earth is
this job" built in), so you supply it: tick the checkbox above the GPS Vectors table and enter the
**Lat**/**Lon** (WGS84, decimal degrees) of a station the GPS vectors are tied to — the same point a
GPS receiver would have logged a position for.

| Column | Meaning |
|---|---|
| **From** · **To** | The two stations the vector connects |
| **ΔX (m)** · **ΔY (m)** · **ΔZ (m)** | The raw ECEF baseline components, meters |
| **Easting** · **Northing** · **Elevation** | The resolved local coordinates of **To**, once rotated |

A vector resolves once its **From** station has a known position (from a leg, the starting
station, or another already-resolved GPS vector) and an origin is set — so GPS vectors can chain:
vector A can place a station that vector B then uses as its own **From**. A vector that cannot yet
resolve (unknown **From**, or no origin set) shows its reason on hover instead of coordinates.

GPS vectors are not just a convenience display — a vector carrying a real a-priori error estimate
(variance/covariance, loaded automatically from a StarNet import's `.GPS WEIGHT COVARIANCE` data)
is also folded into the least-squares adjustment; see **Vert σ** and **GPS Vector Residuals** under
[Closure](#closure) below.

---

## Closure

Press **Calculate Closure…** to open **Traverse Closure Analysis**.

![Traverse Closure Analysis](wiki-img:10b-traverse-closure.png)

*Closure Analysis on the sample field book: unadjusted misclosure and precision on the left, the
least-squares result on the right.*

> *Open the closure analysis: unadjusted misclosure beside a weighted least-squares adjustment,
> with per-observation residuals.*

**Closure requires a closed loop that returns to the start station.** Without **Closed Loop**
ticked, the summary reads `No closure (set Closed Loop and compute).`

### Unadjusted tab

| Value | Meaning |
|---|---|
| **Misclosure ΔN / ΔE** | The gap back to the starting station |
| **Linear misclosure** | Its magnitude |
| **Perimeter** | Total traverse length |
| **Precision: 1:N** | Perimeter divided by linear misclosure. A perfect closure reports `Precision: perfect` |

### Least Squares tab

A weighted least-squares adjustment, solved in-tree — no external linear-algebra dependency.

**A-priori standard errors** — the weights you give the observations:

| Field | Meaning |
|---|---|
| **Angle σ (sec)** | Standard error of an angle observation, in seconds |
| **Dist σ (ft)** | Constant part of the distance standard error |
| **Dist ppm** | Proportional part, in parts per million |
| **Vert σ (ft)** / **Vert ppm** | *Only shown when the traverse has GPS vectors.* Same constant + ppm model, applied to each leg's vertical (elevation) observation |

**Results:**

| Value | Meaning |
|---|---|
| **Observations** | Number of observations used |
| **Unknowns** | Number of unknown parameters |
| **Redundancy** | Observations minus unknowns |
| **Iterations** | Solver iterations |
| **Std dev of unit weight** | How well the observations agree with the stated standard errors. Near 1.0 means the weights were realistic |
| **Adjusted misclosure** | Misclosure after adjustment |
| **GPS vectors folded in** | *Only shown when at least one GPS vector was usable* — how many vectors contributed to the adjustment, and how many were skipped (no a-priori covariance, so GoSurvey has nothing to weight them by) |
| **Residuals** table | Per-observation **Angle resid (sec)**, **Dist resid (ft)**, and — when GPS vectors were used — **Vert resid (ft)** |

A GPS vector changes more than the GPS Vectors table's own numbers: when at least one usable
vector is present (a-priori covariance known), the adjustment starts solving **elevation** too —
every station's elevation becomes a real unknown, not just a side calculation — and a **GPS Vector
Residuals** table appears below the per-leg residuals, showing each used vector's East/North/Up
residual in the local frame. With no GPS vectors, this behaves exactly as it always has:
easting/northing only, no elevation unknown.

If the adjustment cannot run, the tab says `Least squares unavailable: <reason>` and
`No residuals: <reason>` rather than showing numbers you should not trust.

**Recompute** re-runs it after changing the standard errors.

**Accept Least-Squares Result** applies the adjusted coordinates (and, when GPS vectors were used,
the adjusted elevation) to the traverse. The log confirms `TRAVERSE — accepted least-squares
adjustment (adjusted misclosure …)` and the leg is marked `✓ accepted`.

---

## Importing raw data

Four buttons on the **Form** tab each load a different field-data file format directly into the
Traverse Editor — covering five distinct raw-format sources, since one importer (RW5) reads the
same record format shared by three of them. **All four replace the current traverse** — save (see
[Saving and reloading](#saving-and-reloading)) or commit anything you want to keep first.

| Button | Format | Source |
|---|---|---|
| **Import .fbk…** | Autodesk Field Book (`.fbk`) | Autodesk/Civil 3D field books |
| **Import StarNet .dat…** | MicroSurvey STAR\*NET traverse input (`.dat`) | MicroSurvey STAR\*NET |
| **Import RW5…** | Carlson/SurvCE/SurvPC raw data (`.rw5`) | Carlson SurvCE, SurvPC, and (same record format) TDS raw data |
| **Import JobXML…** | Trimble JobXML (`.jxl`/`.xml`) | Trimble Access |

On success the log reports `TRAVERSE — imported <format> (…)`; on failure,
`TRAVERSE — <format> import failed: <reason>` and the current traverse is left untouched.

Every importer is grounded in that format's own published reference (STAR\*NET's v10 Reference
Manual, Carlson's SurvCE RW5 v3.03 spec, Trimble's public JobXML schema) — never guessed — and each
one only understands a **2D conventional traverse** (occupied station, turned angle, horizontal/
slope distance, vertical angle) plus, where the format has them, GPS vectors. A record type outside
that scope is a defined gap: the importer reports it by name and line number and refuses the file,
rather than silently dropping data it isn't sure it read correctly. The exact supported/rejected
record list for each format is documented in its own source header
(`src/traverse/{FbkImport,StarNetImport,Rw5Import,JobXmlImport}.hpp`) if you need to know precisely
what a given file will or won't bring in.

A StarNet `.dat` file carrying `G0`-`G3` GPS vector records (its `.GPS WEIGHT COVARIANCE` mode)
imports those vectors too, straight into the GPS Vectors table — see [GPS Vectors](#gps-vectors)
above. The other formats' GPS-related records (RW5's `GPS`/`GNSS` records, JobXML's GPS/ECEF/WGS84
elements) are a defined gap for now: a file containing one is rejected rather than silently losing
the GPS observations it carries.

Sample field books are in the repository under `samples/` — `traverse_sample.fbk` and
`closed_loop_trav.fbk`.

---

## Command Text editor

Everything the Form tab edits with tables and checkboxes, the **Command** tab edits as plain text —
GoSurvey's own traverse command language. It exists for the same reason a CAD command line exists
alongside toolbar buttons: typing `HDIST 2 3 245.12` to fix one number is faster than hunting
through a table, and a whole traverse in this form is easy to read, diff, hand-edit, or generate
from a script, without needing a binary or XML file format.

It is a deliberately small, plain-text, line-oriented syntax — **not** a copy of any importer's raw
format. Every line is whitespace-separated words with no `key=value` punctuation; comments start
with `#`. A handful of header directives set up the job, and the rest are per-leg directives keyed
by the station occupied (**AT**) and the station sighted (**TO**):

```
STATION 1 5000.0000 5000.0000 100.00
BEARING 0.0
LOOP ON
ERRORS 5.00 0.0200 2.00

STN 1 2
HANG REF 1 90.000000
HDIST 1 2 100.0000

STN 2 3
HANG 1 2 90.000000
HDIST 2 3 100.0000
```

| Directive | Meaning |
|---|---|
| `STATION <id> <e> <n> <z>` | The starting station's point number and coordinates |
| `BEARING <deg>` | Reference orientation at the start station |
| `LOOP ON\|OFF` | Whether the traverse closes back to the start |
| `SCALE ON\|OFF [COMBINED\|GRID]` | Apply the drawing's ground→grid scale factor (REQ-401) |
| `ERRORS <angle-arcsec> <dist-const> <dist-ppm>` | Traverse-wide a-priori standard errors |
| `STN <at> <to> [SIDE \| CHECK [e n]]` | Declares a leg, or (with `SIDE`/`CHECK`) a side/check shot |
| `HANG <from> <at> <to> <deg>` | Horizontal angle; `<from>` is informational (see below) |
| `HDIST` / `SDIST <at> <to> <dist>` | Horizontal / slope distance |
| `VANG <at> <to> <deg> [ZENITH\|ELEV]` | Vertical angle (zenith assumed if omitted) |
| `DESC <at> <to> <text>` | Station description |
| `PRISM <at> <to> <constant> <error>` | EDM prism constant correction and its a-priori error |
| `POLE <at> <to> <error>` | Pole centering a-priori error |
| `TSERR <at> <to> <arcsec> <dist-const> <dist-ppm>` | This leg's own equipment spec, overriding `ERRORS` |

`HANG`'s `<from>` token has no effect on the computed bearing (that is always the start bearing
plus the running sum of turned angles) — it is informational, matching what the instrument's
backsight actually was, and GoSurvey fills it in for you whenever it regenerates this text. The
first leg has no modeled backsight station, so that slot always round-trips as the literal `REF`.

`PRISM`'s and `POLE`'s error values, and `TSERR`'s distance terms, only matter to the least-squares
adjustment — they describe how much to trust a measurement, not how to correct one (except for
`PRISM`'s constant, which is a real correction applied before reduction). A prism's rated accuracy
or a pole's centering error combine with the leg's own distance standard error in quadrature,
the same way a real adjustment package (StarNet, SALSA) treats them: a leg you trust less absorbs
more of a loop's misclosure, not less.

Switching from Form to Command (or back) regenerates the text/tables from whichever side you
edited last — both tabs are views onto the same traverse, not a one-way export. **Apply** parses
the Command tab's text back into the traverse; a malformed line is reported as `line N: <reason>`
and the traverse is left unchanged, the same defined-gap discipline the importers use.

---

## Saving and reloading

**Save** / **Save As…** write the traverse's raw inputs — starting station, every leg's
observations, side shots, GPS vectors, closure options, and a-priori weights — to its own file
(extension `.gstrav`). **Load…** reads one back and replaces the current traverse the same way an
import does. Only inputs are stored; computed coordinates, closure, and the least-squares result
are recomputed on load, so a reopened traverse shows the same closure it showed when saved. When
the drawing belongs to an open GoSurvey Project and the file is saved inside that project's folder,
it is tracked automatically, the same way PDFs and point clouds are.

---

## Committing to the drawing

**Commit to Drawing** writes the computed traverse stations into the drawing as **survey points**.
The log confirms `TRAVERSE — committed points to drawing.`, and the operation is a single undo step
named *Traverse commit*.

From there the stations behave like any other survey points — snappable, labellable, exportable to
CSV, usable in point groups. See [[Survey Points]].

---

## Reports

Traverse results are written to the **Reports** tab of the Properties panel, alongside ALIGN and
exported-point reports.

---

## A typical session

1. **Survey → Traverse.**
2. Fill in the **Starting Station**: ID, northing, easting, elevation, and the **Ref Bearing°** of
   the backsight.
3. Tick **Closed Loop** if the traverse returns to the start.
4. **+ Add Leg** for each leg. Expand it and enter the Face 1 and Face 2 observations, setting the
   **Zenith°** checkbox to match how your instrument reports vertical angles.
5. Read the computed bearings and coordinates in the summary rows.
6. **Calculate Closure…** and read the **Unadjusted** precision.
7. Move to **Least Squares**, set realistic **Angle σ**, **Dist σ**, and **Dist ppm**, and press
   **Recompute**.
8. Check the **Std dev of unit weight** and the residuals. A residual far larger than the others
   points at a blunder — go back and check that observation.
9. **Accept Least-Squares Result.**
10. **Commit to Drawing.**

Or, instead of steps 2–4: press one of the **Import …** buttons and load a field-data file
([Importing raw data](#importing-raw-data)), or switch to the **Command** tab and type the
traverse directly in GoSurvey's command language ([Command Text editor](#command-text-editor)) —
then continue from step 6.

---

## Common problems

| Problem | Cause | Solution |
|---|---|---|
| Closure shows nothing | The traverse is not marked as a closed loop | Tick **Closed Loop** and recompute |
| A summary field will not accept typing | Summary rows are view-only by design | Expand the leg and edit the observation set |
| Vertical angles reduce wrongly | The **Zenith°** checkbox does not match the instrument | Checked = zenith (90° level); unchecked = elevation angle (0° level) |
| A slope distance is ignored | No vertical angle to reduce it with | Enter the vertical angle, or use the horizontal distance field |
| `Least squares unavailable` | Not enough redundancy, or the geometry is degenerate | Add observations, or check for a duplicated station |
| Std dev of unit weight is far from 1 | The a-priori standard errors do not describe the real observations | Adjust **Angle σ** / **Dist σ** / **Dist ppm** to match your instrument |
| An import wiped my traverse | Every importer (and Command **Apply**) replaces the current traverse by design | **Save** (or commit) before importing |
| A GPS vector shows no coordinates | No origin lat/long set, or its **From** station has no known position yet | Tick the origin checkbox and set Lat/Lon; make sure **From** is a station a leg (or an earlier GPS vector) already resolved |
| GPS vectors don't affect elevation | None of them carry a-priori covariance | Only StarNet's `.GPS WEIGHT COVARIANCE` records (or manually entered variance/covariance) make a vector usable for the adjustment |
| An RW5/JobXML file with GPS shots is rejected | GPS/GNSS records in those formats are a defined gap — only StarNet's G-records import GPS vectors today | Use StarNet `.dat` if the job has GPS vectors, or strip them before import |
| A Command-tab edit reports `line N: …` | The text has a syntax error, or breaks the chain (e.g. `AT` doesn't continue the previous leg's `TO`) | Fix that line; the traverse is left unchanged until it parses |

---

## Related

[[Survey Points]] · [[Coordinate Alignment]] · [[Inquiry Commands]] · [[Import and Export]]
