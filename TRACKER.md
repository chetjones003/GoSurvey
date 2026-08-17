# PROJECT ISSUE & FEATURE TRACKER

> **BUG-014 onward are also GitHub issues.** The REQ-204 fuzz harness files findings to
> github.com/chetjones003/GoSurvey/issues so they can be triaged and deduplicated by signature
> (`docs/fuzz-harness.md` §6). They are recorded here too, because this file — not the issue
> tracker — is what a future reader of the repo actually finds.

## CHANGES

### v0.5.1's `latest.json` was re-uploaded by hand to correct its release notes — 2026-08-17
    - **A deliberate deviation from REQ-202** ("releases are produced by the pipeline, not by
      hand"), recorded rather than done quietly. Only the `notes` field was changed; the manifest
      is otherwise the pipeline's own output.
    - Why: `RELEASE_NOTES.md`'s `## 0.5.1` section had been written during the previous beta cycle
      and described only telemetry, surfaces and the offset/GPU fixes. It was never updated for
      what the stable release actually contained, so the update dialog offered users 0.5.1 without
      mentioning **BUG-023 — a crash that closed the program and lost unsaved drawings**. That is
      the single item most likely to make someone take the update.
    - The alternative was a 0.5.2 whose only content is corrected prose, which spends a version
      number and a second download on every user to fix a paragraph.
    - Verified before upload, not after: the installer's SHA-256 and byte size were re-derived from
      the published asset and matched the manifest; the edited file was parsed by **nlohmann/json,
      the same parser `UpdateCheck.cpp` uses**, not just by a shell JSON reader; and all nine
      non-`notes` fields were compared field-by-field against the original and found identical.
      Re-verified after upload against what GitHub actually serves.
    - **Follow-up worth having:** nothing in the pipeline notices that a stable release's notes
      section was written for an earlier cycle — the version gate checks the tag, not the prose.
      A release-review step that shows the notes for confirmation before publishing would have
      caught this, and is cheaper than the alternative of catching it afterwards again.

### Telemetry backend moved from Google Sheets to Cloudflare Worker + D1 — 2026-08-16
    - **Not a SPEC GAP.** ADR-032 delegates the receiving endpoint to the operator and names this
      exact shape ("a Cloudflare Worker + KV/D1"); `TelemetryEndpoint` is the one knob it says may
      move. REQ-080 is untouched — same five-field payload, same events, same client behaviour.
    - Motivation was not that Sheets was broken (BUG-020/021 fixed it and it worked), but that it
      was structurally the wrong backend: Apps Script cannot set an HTTP status code, so every
      failure answered `200`; ~1k requests/day per Google account; counting meant reading the whole
      sheet; and a public unauthenticated writer with no schema, no validation and no dedupe.
    - **D1, not KV** — despite the earlier backend notes recommending KV. KV's free tier
      allows ~1,000 writes/day and every ping is a write, so it would start dropping data at ~1k
      DAU: exactly the scale the telemetry exists to measure, and silently. D1 allows ~100k row
      writes/day and answers the questions in SQL.
    - New `tools/telemetry-worker/` (Worker, schema, queries, offline tests). Plain JS with no
      build step so deploying a 120-line endpoint needs no TypeScript toolchain. Not part of the
      CMake build; deployed with wrangler on its own cadence.
    - Server-side hardening the old backend had none of: field-shape validation with the offending
      field named in the 400; a 2 KiB body cap; `INSERT OR IGNORE` behind two partial unique
      indexes so a retried ping cannot inflate counts (install once ever per id, active once per
      id per day).
    - Privacy went **forward**, not sideways: no IP is stored (the Sheets script tried to and
      crashed doing it), only Cloudflare's country code, which is server-derived and outside the
      REQ-080 payload. Workers Logs left off deliberately — it retains client IPs.
    - Client change is one constant plus a comment. The `"ok":true` body check from BUG-020 is
      **kept**, not reverted: the Worker does return honest status codes, but a 200 can still come
      from a captive portal, proxy or stale DNS rather than from our endpoint.
    - Rollback is that same constant plus a rebuild; the Sheets deployment and its doc are intact.
    - **Deployed and verified live 2026-08-17** at
      `https://gosurvey-telemetry.gosurvey.workers.dev/v1/ping` (D1 `6a6a538a-…`). Checked against
      the running endpoint, not just locally: valid ping → `200 {"ok":true,"stored":true}`; both
      partial unique indexes confirmed present and both observed rejecting a same-day repeat as
      `stored:false`; bad `channel`/`event`/missing fields → `400` naming the field; non-JSON →
      `400`; wrong path → `404`; GET → `405`. Seven successful POSTs produced exactly two rows.
      `country` resolved (`US`) and no IP was stored anywhere. Test rows deleted; table empty.

## BUGS

### [BUG-023] VIEWPOINTS → Load crashed the application (access violation) — FIXED 2026-08-16
    - Found 2026-08-16 while verifying the Viewpoints grid restyle, by clicking Load on a points
      file. **The application terminates with `0xC0000005`** and loses any unsaved drawing.
    - **Pre-existing, and confirmed so rather than assumed**: the change under test was stashed,
      the pristine tree rebuilt, and the same click reproduced the same access violation. Nothing
      in the restyle touched the fault.
    - Cause: `LoadSurveyPointsFromJsonFile` (`survey/SurveyPoints.cpp`) clears
      `st.surveyPointIdBuffers` at the top and **never refills it**, while filling
      `st.surveyPoints` with N points. `surveyPointIdBuffers` is a parallel array the Viewpoints
      grid indexes by point index. That grid resizes the buffers at the *top* of its function —
      i.e. **before** it submits the Load button — so within the very frame the button fires, the
      table walks N rows reading `surveyPointIdBuffers[i]` on an empty vector.
    - Why it hid: the vector is empty only between a load and the next frame's resize, so any
      loader that ran outside a Viewpoints frame (DXF import, `.gs` open) left the buffers to be
      repaired on the next frame and looked fine. Only the button *inside* the grid's own frame
      exposes it. A release build reads freed/`nullptr` storage rather than asserting.
    - Fix, in two places on purpose: (1) the loader refills the buffers itself, so the invariant
      belongs to the owner and every future caller inherits it; (2) the grid re-checks the length
      immediately before reading rows, so any *other* control drawn above the table that mutates
      the point list mid-frame cannot reopen the same hole.
    - **Standing risk, not fixed here:** `surveyPointIdBuffers` being a parallel array at all is
      the underlying hazard — nothing in the type system ties its length to `surveyPoints`.
      Folding the edit buffer into `SurveyPoint` would remove this class of defect outright.
      Recorded as follow-up debt.

### [BUG-022] The Survey ribbon's "Groups" button was drawn outside its panel and never appeared — FIXED 2026-08-16
    - Reported 2026-08-16 by the user ("the survey ribbon tab is not big enough to accommodate the
      point group button"). Point Groups (REQ-067) shipped with a ribbon button that **no user could
      ever have clicked** — the panel manager was only reachable from the command bar.
    - Cause: a ribbon panel is exactly **three** small buttons tall by construction —
      `rowH = floor((colH - 4) / 3)` in `DrawRibbonBar` — and the Survey section stacked **four**
      (Inverse, Traverse, Surfaces, Groups) into a single `BeginGroup` column. The fourth was laid
      out past the section's bottom edge and clipped away by the child window. Nothing warns: the
      button is submitted, hit-tests against a clipped rect, and simply never draws.
    - The same trap had already been hit and worked around in the Inquiry section, whose comment
      says so verbatim: *"Two columns: the panel is three small buttons tall, so a fourth in one
      column is clipped."* Survey was written afterwards and did not follow it.
    - Fix: split Survey into two columns (Inverse/Traverse | Surfaces/Groups) and widen `wSrv` to
      match, exactly as Inquiry does.
    - **Standing risk, not fixed here:** the three-row limit is implicit in an expression rather
      than asserted, so the next section to add a fourth button will fail the same silent way. A
      debug assert in `RibbonSectionEnd` that the content fits its section would turn this class of
      defect from invisible into loud. Recorded as follow-up debt.

### [BUG-021] Telemetry could never emit `install`, and re-minted its id every launch — FIXED 2026-08-16
    - Found 2026-08-16 while verifying BUG-020, from evidence rather than review: the local
      `gosurvey-user.json` held `lastActivePingDate` with **no** `installId`, a combination the
      code should not be able to produce.
    - **Two coupled defects in `TelemetryWorker`, both from ordering:**
      1. The worker generated the install id *before* calling `DecideEventToSend`, which tells
         `install` from `active` precisely by whether an id already exists. The function never saw
         an empty id, so the `install` branch was unreachable — **every first run on a new machine
         reported `active`**, and `install` was dead code. `DecideEventToSend` itself is correct
         and its tests always passed; only the caller was wrong, which is why tests did not catch it.
      2. Because `install` never fired, the `if (event == "install")` block that persisted the id
         never ran either, and the success path wrote only the date (`UpdateTelemetryIds("", today)`).
         So **no id was ever stored** and a fresh one was minted on every launch. Unique-install
         counts would have equalled total ping counts and repeat users would have been invisible —
         the dataset would have been worthless, and silently so.
    - The observed prefs state is exactly what these two produce together, and is the proof.
    - **Fixed:** decide before minting; persist the id on the success path alongside the date.
      Storing on acknowledgement rather than on attempt also means a first ping that never landed
      is retried as `install` next launch instead of being downgraded to `active` forever. Cost: a
      duplicate install row in the narrow case where the row is written but the reply is lost —
      preferred over losing the install event on the offline-job-site path REQ-080 expects.
    - Existing installs self-heal: an absent id now decides `install` on next launch.
    - **Verified end to end 2026-08-16.** A real app launch wrote `event=install` with a 32-hex id
      to the sheet — an event the old ordering could not emit — and `gosurvey-user.json` now holds
      that same id (`a32726f3…`) alongside the date, where before it held the date alone.
    - **Test gap, acknowledged:** the defect lives in `TelemetryWorker`, which needs UserPrefs and a
      network, so no unit test covers it. Making it testable means extracting the ordering into a
      seam that would have exactly one caller — rejected under CLAUDE.md rule 2. The pure decision
      function is tested; the sequencing around it is not.

### [BUG-020] Telemetry pings never reached Google Sheets — FIXED 2026-08-16
    - Reported 2026-08-16: the app POSTs, the POST "succeeds", no row ever appears in the sheet.
    - **Three independent defects, each sufficient on its own.** Confirmed by probing the live
      endpoint, not inferred:
      1. **Wrong URL path form.** `TelemetryPing.hpp` held
         `/macros/d/{ID}/userweb`, which answers **404** — it is not an Apps Script route at all.
         A deployed web app lives only at `/macros/s/{DEPLOYMENT_ID}/exec`. The bad form came from
         the Sheets setup guide's Step 3, which taught it in four places (guide since deleted).
      2. **Deployment is not public.** `/exec` on the correct path still answers **401** to POST
         and **302 → accounts.google.com** to GET, so `doPost` never runs. "Who has access" is set
         to `Anyone with Google Account`; GoSurvey pings anonymously and must have `Anyone`.
      3. **The Apps Script would drop every row anyway.** `doPost` called
         `e.source.getLocalAddress()` for an IP column. A `doPost` event has no `source` property
         — Apps Script exposes no client IP at all — so it threw a `TypeError` on *every* request,
         the `catch` returned `{"error":…}`, and `appendRow` was never reached.
    - **Why it looked like success:** Apps Script cannot set an HTTP status code, so a thrown
      handler and a sign-in redirect both answer `200`. `HttpPostJson` checked only the status, so
      the client reported a successful ping in all three failure modes. That is what turned three
      one-line defects into a day of debugging, and it is the part most worth keeping fixed.
    - **Fixed:** URL corrected to `/exec`; `HttpPostJson` gained an optional `responseOut` and now
      drains the body; `TelemetryWorker` requires the literal `"ok":true` acknowledgement and logs
      the body otherwise; the documented Apps Script drops the IP column, guards a missing
      `postData`, and takes a `LockService` lock around `appendRow` so concurrent pings cannot
      interleave. Docs rewritten with a signed-out `curl` that distinguishes causes 1–3.
    - **Owner action, done 2026-08-16:** defect 2 was a Google console setting. Redeployed with
      access `Anyone` at script version 3; the deployment ID was preserved, so the constant in
      `TelemetryPing.hpp` remained correct. Verified end to end — a signed-out `curl` to `/exec`
      now returns `{"ok":true}` and appends a row.
    - **Diagnostic trap, documented:** `curl -X POST -L` against `/exec` returns **`411 Length
      Required`** even when everything works. `/exec` answers a `302` to
      `script.googleusercontent.com`, which must be followed with a GET; `-X POST` pins the method
      so curl re-POSTs with no body. This happens *after* `doPost` has run and written the row, so
      it reads as a failure on a working endpoint. `-d` without `-X` is correct, and WinHTTP
      converts the method properly, so GoSurvey never hits it. Both setup docs now say so.
    - Also fixed in passing: `tests/TelemetryPingTests.cpp` used C++20 designated initializers in a
      C++17 build, so the whole test binary failed to link and the ten telemetry tests had never
      once run. Suite is 390 tests / 203,335 assertions green.
    - **Known weakness, not fixed:** `BuildTelemetryJson` does not escape `"` or `\`, so a payload
      field containing either emits invalid JSON. Unreachable today (install ids are generated hex;
      version/channel/os are compile-time or enumerated), and the test named "escapes special
      characters" asserts the *unescaped* string, so it documents the gap rather than closing it.

### [BUG-025] The large-coordinate rebase threshold ignored the max-Y extent — FIXED 2026-08-17
    - Found 2026-08-17 while diagnosing BUG-019, by reading the code that issue pointed at.
    - `MaybeRebaseLargeCoordinates` decided whether a loaded drawing needs its precision rebase from
      `std::max({fabs(mnX), fabs(mxX), fabs(mnY), fabs(mnY)})` — **`mnY` twice, `mxY` never**. So a
      drawing whose only large coordinate was a large *positive* Y was never rebased: the precision
      repair silently did not fire, and nothing reported it.
    - **Demonstrated, not deduced.** Before the fix, two drawings that are the same shape rotated
      behaved differently: `LINE (0,0)→(0,1e+12)` was not rebased, while `LINE (0,0)→(1e+12,0)` was.
    - **Fixed (TASK-066)**: `fabs(mxY)` in the fourth slot.
    - Worth recording: fixing it **raised** the `gs-roundtrip` failure rate from 48/150 to 53/150
      seeds, because more drawings then correctly rebased and every rebase broke the old byte-identity
      rule. That interaction is why the two were resolved in one task rather than separately.
    - Regression test: `tests/headless/transcripts/regression-61a-rebase-threshold-max-y.txt`. It
      asserts the rebase *message*, deliberately, so it does not depend on how BUG-019 was resolved.

### [BUG-019] Large coordinates break `.gs` resave idempotence — RESOLVED 2026-08-17 as a **spec** defect ([#61](https://github.com/chetjones003/GoSurvey/issues/61))
    - Found 2026-08-16 by the `gs-roundtrip` oracle once BUG-014/015 were fixed and the oracle could
      see past them. ~325 of 1000 seeds hit it; minimized automatically 116 → 8 lines.
    - A drawing containing a coordinate of state-plane magnitude (`-1e+12`) is not byte-identical on
      resave. Violates REQ-079's first acceptance condition.
    - **Hypothesis, not a diagnosis:** the local-storage rebase. Geometry is stored local
      (`world = local + worldDocumentOrigin`) and the origin is re-established when large
      coordinates arrive; if load recomputes it differently, the local values shift. Fits the
      trigger, but unconfirmed — worth reading the load path before acting.
    - If the hypothesis holds the round trip is geometrically faithful and only the origin/local
      split differs, which changes how much this matters and whether REQ-079 should be met or
      amended. Determine that first.
    - This is why `fuzzgen::Options::emitRoundTrip` stays OFF by default: at ~1/3 of seeds it would
      bury every other finding.
    - **The hypothesis was correct** (TASK-066). `MaybeRebaseLargeCoordinates`, called at the end of
      `.gs` load (`GsIo.cpp:1487`), rebases the origin to the extents midpoint when the stored origin
      is exactly `(0,0)` and the magnitude clears the threshold. Confirmed by diffing the two files
      rather than by reading alone:
      ```
      file A:  origin (0, 0)                      local y = -999999995904
      file B:  origin (106.625, -499999998125.5)  local y = -499999995904
      ```
    - **But the "so it's only the storage split" half was wrong, and that mattered.** The world
      position does move — 1874.5 units on one endpoint of the reproducer. Chasing that down is what
      produced the actual answer: the drift is bounded by the float spacing of the *rebased*
      coordinate, which is never coarser than the spacing of the value it replaced. So the rebase
      **cannot** make precision worse than the file already had, and for realistic data it is a large
      improvement — a 5,000 ft survey at easting 2e6 goes from ~0.25 ft quantization to ~0.0002 ft.
      The 1874-unit number is an artifact of a synthetic drawing that *spans* 1e12, where no float
      storage can be precise. The rebase is right; only its interaction with REQ-079 was wrong.
    - **Resolved as a SPEC GAP, not a code fix** — decision **D-2026-08-17-a**, recorded in
      `spec/project.md`. REQ-079's first acceptance condition was asking the format to promise
      something the precision design contradicts, so the *requirement* was wrong. Three options were
      put to the user: (A) rebase at entry so a stored file is always already optimal; (B) amend the
      requirement and compare the 2nd and 3rd saves; (C) add an `originNormalized` field so load never
      re-decides. **The user chose (B).**
    - What changed:
      - REQ-079 now reads "no migration **and no normalization**", its Statement defines normalization
        as a bounded, reported, non-lossy storage change, and a **new** acceptance condition requires
        it to be **idempotent** — so the amendment strengthens the spec rather than merely excusing the
        code. Idempotence is the condition that catches what would actually hurt: geometry drifting
        further on every open/save cycle.
      - The `gs-roundtrip` oracle compares **B to C** instead of A to B, in both the generator and
        `tests/headless/transcripts/gs-roundtrip.txt`.
      - **`fuzzgen::Options::emitRoundTrip` is now ON by default**, and the flip followed that
        option's own written rule ("flip when a `--roundtrip` sweep comes back clean, not when a
        particular issue closes") rather than the decision alone: a **1000-seed sweep reports 0
        failures**, down from ~325. A 2000-seed sweep with the new default is also clean.
        `--no-roundtrip` was added to skip it.
    - Regression test: `tests/headless/transcripts/regression-61-large-coord-normalization.txt` —
      asserts the normalization is reported on the first load and that B and C are byte-identical.

### [BUG-018] Erasing the last polyline writes a `.gs` that cannot be reopened — FIXED 2026-08-17 ([#60](https://github.com/chetjones003/GoSurvey/issues/60))
    - Found 2026-08-16 by the `gs-roundtrip` oracle (seed 28, `--roundtrip`). **The only finding so
      far that loses work.**
    - Save reports success and writes the file; opening it fails with
      `.gs: polylineOffsets invalid (expected empty or at least two entries)`. The drawing is gone.
    - Root cause: `ErasePolylineByIndex` (`CadCommands.cpp:8354`) rebuilds the CSR offset array by
      seeding `newOff` with 0 and appending one entry per *surviving* polyline. Erase the only
      polyline (`np == 1`) and the loop body never runs, leaving `{0}` — a single-entry CSR array,
      which is not how "zero polylines" is spelled. `CommitPolylineDraft` assumes empty, and
      `GsIo.cpp:834` enforces empty-or-≥2 on read. Writer and reader disagree; the reader is right.
    - Fix shape: `if (newOff.size() == 1) newOff.clear();` before the assignment. Check the paper
      sibling `ErasePaperPolyline` (`CadCommands.cpp:566`) for the same shape.
    - **Harness gap:** the `polyline-offsets` invariant does not catch `{0}` — it checks start-at-0,
      monotonic, and ends-at-vertex-count, all of which `{0}` satisfies with zero vertices. Adding
      the reader's own rule as an invariant would catch this at the moment of corruption instead of
      at load, and is the right follow-up.
    - **Fixed (TASK-064)**, confirmed rather than assumed at every step. The corrupted file really
      does contain `"polylineOffsets":[0]` with `"polylineVerts": []`, and the reproducer's
      `CHECK ALL` — placed between the corruption and the save — **passed** on the unpatched build,
      which is the harness gap demonstrated rather than reasoned about.
      - `if (newOff.size() == 1) newOff.clear();` in `ErasePolylineByIndex`, as the issue proposed.
        The paper sibling `ErasePaperPolyline` (`CadCommands.cpp:577`) already did exactly this and
        says so in a comment, so the model store was the outlier and the fix is the existing
        convention, not a new one.
      - Two more sites created the same invalid state and are fixed with it: `StartFrameBudgetBench`
        set `userPolylineOffsets.assign(1, 0)` in **both** the mesh and surface profiles. Left alone,
        they would have made the new invariant false by construction — and a check that fires on the
        codebase's own behaviour is a check that gets waived.
      - The `polyline-offsets` invariant now carries the reader's rule ("empty, or at least two
        entries") with a fixture that breaks it *and* one asserting the legitimate empty table stays
        silent, since the false positive is the more expensive failure here.
      - Safe because empty is the store's **normal** state, not a new one: all 20+ read sites already
        guard it (`size() > 0 ? size() - 1 : 0`, `empty() ? 0 :`, `std::max(0, (int)size() - 1)`).
        Checked by grep rather than assumed.
    - **Two things this bug exposed, both left open deliberately:**
      1. The `io` failure signature cannot tell "file absent" from "file present but rejected" —
         both are `io|OPEN failed` — so the minimizer honestly reduced this to `NEW` + `OPEN`, a
         2-line reproducer for a different bug. The same weakness was already fixed once for
         `expect`. Hence the regression transcript is hand-written and asserts entity counts on both
         sides of the round trip.
      2. **The headless driver cannot drive selection at all**, so `DELETE` — the natural way to hit
         this — is untestable from a transcript. `SubmitViewportPickImpl` has no idle
         single-entity-select branch (click-select lives in the UI layer), and the window-select
         anchor is set by `BeginSelectionBoxCorner`, which the driver has no verb for. The
         reproducer uses `OVERKILL` instead: no selection needed, and it reaches the *same*
         `ErasePolylineByIndex` via its degenerate-polyline erase. A REQ-203 coverage gap.
    - Regression test: `tests/headless/transcripts/regression-60-erase-last-polyline.txt`, plus
      `polyline-offsets fires on a single-entry table` / `stays silent on an empty table`.
    - Seed 28 still fails — with `expect|SAMEFILE`, which is **BUG-019/#61** standing behind this
      one. A 120-seed `--roundtrip` sweep now shows that signature only, and no `io|OPEN failed` at
      all. **(Superseded 2026-08-17: BUG-019 is resolved, and seed 28 now passes outright.)**

### [BUG-024] A relative coordinate `@dx,dy` can resolve to a non-finite point — FIXED 2026-08-17
    - Found 2026-08-17 **while fixing BUG-017**, by probing the sibling commands issue #59 recommends
      guarding. Not in that issue, and **a different root cause**: nothing is derived from a
      distance here.
    - `ParseWorldPoint` resolves `@dx,dy` as `*ox = baseX + dx`. That sum overflows `float` while the
      base and the delta are each perfectly representable — `2e+38 + 2e+38 = 4e+38`, and
      `FLT_MAX ≈ 3.4e+38`. Reproduced independently for **LINE** (`userLinesFlat[3] = inf`),
      **POLYLINE** and **RECT** (`userPolylineVerts[3] = inf`), all three because they consume the
      same parser.
    - Violates REQ-204 ("no coordinate is NaN or infinite") and REQ-201 (no refusal was reported —
      there was no refusal).
    - **What made the fix one line:** an *absolute* coordinate that overflows is **already** refused
      and reported — `CMD 1e+40,0` fails stream extraction and the caller logs "Could not parse
      input". So the relative branch was the one path that can manufacture a non-finite coordinate
      out of finite input, and the one path with no check: it did the addition *after* the validation
      and never re-validated. The guard restores this function's own existing guarantee rather than
      inventing a new rule, which is why no new log message was needed.
    - **Fixed (TASK-065)** with `if (!std::isfinite(*ox) || !std::isfinite(*oy)) return false;` after
      the addition. Every caller already reports a false return as a parse failure, so REQ-201 is met
      by construction — and asserted, not assumed: the regression transcript checks
      `EXPECT LOG "Could not parse"`.
    - Regression test: `tests/headless/transcripts/regression-59b-relative-coord-overflow.txt` —
      one section per command, so a fix that only helped LINE would fail on POLYLINE or RECT.
    - Not filed as a GitHub issue: it was found and fixed in the same task, so an issue would have
      been opened and closed in one commit.

### [BUG-017] CIRCLE stores an infinite radius — FIXED 2026-08-17 ([#59](https://github.com/chetjones003/GoSurvey/issues/59))
    - Found 2026-08-16 by the REQ-204 fuzzer (seed 4737), minimized automatically 169 → 4 lines.
    - A centre far from the picked point makes the derived radius overflow `float`, and the circle
      is committed with `r = inf`. It is then saved, exported and fed to every extents/snap/render
      calculation. Neither magnitude is exotic: `1e12` is an ordinary state-plane easting.
    - Violates REQ-201 (the refusal is not reported — there is no refusal) and REQ-101.
    - Repro: `NEW` / `CMD CIRCLE` / `CMD -1e+12,1e+38` / `PICK 50 15`.
    - Fix shape: reject non-finite derived geometry at the commit site. Worth doing generally rather
      than per-command — LINE, ARC, ELLIPSE and OFFSET all derive lengths from two user points.
    - **Fixed (TASK-065)** at `CommitCircle`, covering all four CIRCLE routes (centre+radius typed,
      centre+radius picked, and both 3P paths) because they all funnel through it.
    - **The guard had to go BEFORE the existing `r < 1e-5f` test, and that is the whole lesson of this
      bug rather than a detail:** `inf < 1e-5f` is false, so the radius-too-small check waved `inf`
      straight through — and since every comparison against NaN is also false, it waved NaN through
      too. A magnitude test cannot screen non-finite values; it is exactly the wrong tool, and it
      looked like a validation.
    - **The issue's "guard LINE, ARC, ELLIPSE and OFFSET too" recommendation was probed, not taken on
      trust**, because guarding on a guess is a speculative fix:
      | Probe | Result |
      |---|---|
      | ARC, three picks at `±1e+38` | arc committed, `finite-coords` **passed** |
      | ELLIPSE, major axis endpoint `3e+38,3e+38` | ellipse committed, **passed** |
      | OFFSET of a circle at distance `1e+38` | copy committed, **passed** |
      | LINE with `@2e+38,0` | **FAILED** — but a *different* mechanism, filed as BUG-024 above |
      So ARC/ELLIPSE/OFFSET are **probed-and-not-reproduced, not proven safe**, and deliberately not
      guarded. If a seed ever reaches one it is a new finding with a real reproducer, which is worth
      more than a guard added today on a hunch.
    - The `IsStorableCoordinate()` helper the issue suggests was considered and **rejected**: once the
      evidence was in, the two real mechanisms needed different checks in different places, so the
      helper would have had one present-day use — REQ-301 violated in order to look general.
    - Regression test: `tests/headless/transcripts/regression-59-circle-infinite-radius.txt` — the
      fuzzer's own minimized reproducer (169 → 4 lines) verbatim, plus `EXPECT LOG` so a *silent*
      refusal cannot pass (REQ-201).
    - **Seed 4737 is now clean, and so is a full 5000-seed sweep** — the same sweep that originally
      produced #56–#59.

### [BUG-016] OFFSET duplicates the source entity's id — FIXED 2026-08-16 ([#58](https://github.com/chetjones003/GoSurvey/issues/58))
    - Found 2026-08-16 by the REQ-204 fuzzer (seeds 2004 and 3555), minimized automatically
      155 → 9 lines.
    - Root cause: all five `CommitOffset*` functions copied the source's `EntityAttributes`
      wholesale so the copy would inherit layer/colour/linetype/lineweight/transparency — and `id`
      came with them. `EnsureEntityIds` only fills ids that are 0, so a copied non-zero id was never
      repaired and was written to `.gs` permanently.
    - Violates REQ-076 (ids unique, never reused) and through it architecture §11.9: a cross-object
      reference *is* an id, so an id naming two entities makes every such reference ambiguous.
    - The codebase already had the answer — `CopySelectionToClipboard` clears ids on copy via
      `ClearEntityIdsFrom` (CadCommands.cpp:3698). OFFSET never got the same treatment.
    - **Fixed (TASK-057)** with a `PushOffsetCopyAttrs` helper that does the copy and clears the id,
      called from all five sites. One named helper rather than the same three lines in five places,
      because the defect *was* five copies of a pattern all missing the same step.
    - Regression test: `tests/headless/transcripts/regression-58-offset-entity-id.txt` — the
      fuzzer's own minimized reproducer, kept verbatim. It also asserts `EXPECT LINES 2`, since an
      id-collision check cannot fire if OFFSET silently stops producing anything.

### [BUG-015] An empty drawing fails `.gs` resave idempotence — FIXED 2026-08-16 ([#57](https://github.com/chetjones003/GoSurvey/issues/57))
    - Found 2026-08-16 by the REQ-204 `gs-roundtrip` oracle (seed 2), minimized 116 → 5 lines.
    - Load materializes a default layer `"0"` and text style `"Standard"` that a newly created
      drawing does not carry, so save → load → save is not byte-identical.
    - Violates REQ-079's first acceptance condition. The deeper issue is that a new drawing is
      briefly in a state the rest of the code is entitled to assume cannot happen — both are
      documented as always existing.
    - Masked in normal use because the startup template is loaded on launch and already has both.
    - **Fixed (TASK-058)** by initialising both tables on `AppCommandState` itself —
      `drawingLayerTable = DefaultDrawingLayerTable()` and `textStyles = TextStyles::DefaultTextStyles()`.
      Chosen over patching each creation site so that *every* route to a drawing (File > New, the
      headless driver, an importer, a test) gets it with no site left to forget. Safe because both
      load paths `clear()` these tables before repopulating, so a loaded file still wins.
    - The defaults are defined in terms of the same helpers the loader uses
      (`SyncDrawingLayerTableWithGeometry`'s row, `TextStyles::EnsureStandard`), so the created and
      synthesized versions cannot drift — a second literal would have been the same bug again.
    - Regression test: `tests/headless/transcripts/regression-57-empty-drawing-roundtrip.txt`.

### [BUG-014] TEXT is saved to `.gs` with id 0 — FIXED 2026-08-16 ([#56](https://github.com/chetjones003/GoSurvey/issues/56))
    - Found 2026-08-16 by the REQ-204 `gs-roundtrip` oracle on its first run.
    - The TEXT commit path (`CadCommands.cpp`, both the model and paper branches) never calls
      `BumpCadGpuCache`, so the `EnsureEntityIds` early-out — "geometry has not changed since the
      last sweep, so nothing can be missing an id" — returns on a false premise. The annotation
      keeps `id = 0` and is written out that way. The DIM* sibling paths do bump.
    - Violates REQ-076; also breaks REQ-079 resave idempotence, since load then assigns the id.
    - Rarely seen in the GUI because almost any later interaction bumps the revision and the sweep
      catches up before a save. It needs a save with no intervening geometry change.
    - **Fixed (TASK-058)** by adding the missing `BumpCadGpuCache(st)` after the TEXT commit, placed
      after the if/else so it covers the model and paper branches alike. An audit of every
      `cadAnnotations.push_back` site confirmed TEXT was the only one missing it — the clipboard
      paste site bumps once at the end of its loop, and the DIM* paths bump inline.
    - Regression test: `tests/headless/transcripts/regression-56-text-entity-id.txt`. Detects it
      through the round trip rather than by reading the id, so one assertion covers both the
      unassigned id and the resave breakage it causes. TEXT now saves with `id: 1`.

### [BUG-013] On a hybrid laptop GoSurvey renders on the integrated GPU — FIXED 2026-08-15
    - Found 2026-08-15 by TASK-053's acceptance run, not by a report: the same scene measured
      9.27 ms at 21:21 and 13.13 ms at 22:39 on an unchanged binary. `nvidia-smi` showed the
      RTX 5060 at 0% utilisation and 12 W idle *while the benchmark ran*; the process's 3D load was
      on the AMD 610M.
    - Root cause: GoSurvey exports neither `NvOptimusEnablement` (NVIDIA) nor
      `AmdPowerXpressRequestHighPerformance` (AMD). Those two exported symbols are how a hybrid
      laptop's driver is told an application wants the discrete GPU. Without them the choice falls
      to Windows' heuristics, which are free to answer differently between launches — which is
      exactly what happened here.
    - Impact on users: most field laptops are hybrid. Those users are silently getting the weak
      GPU for a 3D CAD application. Forced onto the discrete GPU the same scenes run **6-9x
      faster** (250k segments 9.27 -> 1.38 ms; a 2M-triangle shaded mesh 21.40 -> 1.97 ms).
    - Impact on the spec: every REQ-100 figure ever recorded — 8.93 ms (clang), 9.27 / 9.32 ms
      (MSVC, TASK-052) and the whole headroom sweep — is an integrated-GPU number, while
      `project.md` §7 names an RTX 5060. See TASK-053 FINDING-3.
    - Fix: export both symbols from a translation unit that is definitely linked (they must survive
      the linker — `main.cpp`, `extern "C" __declspec(dllexport) DWORD NvOptimusEnablement = 1;` and
      the AMD equivalent). ~6 lines. Verify with `nvidia-smi` showing real utilisation during BENCH,
      not by trusting the numbers to look better.
    - Watch out for: this changes which GPU every user's session runs on, and on some machines the
      discrete GPU costs battery life. It is a shipped-behaviour change and wants a recorded
      decision, which is why TASK-053 escalated it rather than fixing it in passing.
    - **Fixed (TASK-054)** with both halves the user asked for: the exported symbols make the
      discrete GPU the default even on a machine with no registry state, and a Settings → System
      checkbox ("Prefer the integrated GPU") records Windows' own per-application preference to hand
      it back, effective next launch. Measured on the reference machine with nvidia-smi as the
      instrument: exports alone -> discrete, 1.46 ms; setting checked -> integrated, 12.42 ms;
      unchecked -> discrete, 1.38 ms. The override was proven, not assumed — had the preference not
      beaten the exports, the checkbox would have been decorative.
    - Verified through the real UI, not only the API: clicking the checkbox wrote `GpuPreference=1;`
      and unchecking wrote `=2;`. 3 regression tests (registry round-trip, clear-removes-the-value,
      idempotence) touch only the test executable's own key and restore it; suite 332/332.

### [BUG-012] Double-clicking a .gs file opens GoSurvey empty — FIXED 2026-08-15
    - `int main()` in src/app/main.cpp took no argv, so the application could not receive a file
      path from the command line at all.
    - The installer registers `.gs` with `shell\open\command = "...\GoSurvey.exe" "%1"`, so the
      path WAS passed — and silently dropped. The drawing never opened.
    - Pre-existing; found while verifying REQ-079's reader change (TASK-051), not caused by it.
    - Fix: read the WIDE command line (CommandLineToArgvW) rather than adding argc/argv. argv is
      encoded in the process ANSI codepage, so a drawing under a path containing characters
      outside it would have arrived mangled and failed to open on a machine where the file
      plainly exists. A command-line file wins over the startup template; one that fails to load
      falls back to the template AND says so (REQ-201), because silence there looks identical to
      the original bug.
    - Verified: samples\surface-demo.gs opens and renders via the exact command line Explorer
      uses; a malformed .gs falls back with the app alive and responsive; a directory argument is
      ignored.
    - Follow-up, not done: DXF/DWG paths are not handled (only .gs is associated today), and the
      drawing tab still reads "Drawing 1" rather than the opened file's name.

### [BUG-011] ViewportRenderer did not compile with MSVC — FIXED 2026-08-15
    - Symptom: ~50 x C2362 in ViewportRenderer.cpp, "initialization of 'x' is skipped by
      'goto finish_render'". Never seen locally because CMakePresets' ninja-release pins no
      compiler, so CMake picks clang off PATH; surfaced on the first CI build, where
      msvc-dev-cmd puts cl.exe first.
    - Root cause: `goto finish_render` (paper-space early-exit) jumped over ~50 initialized
      declarations INTO their scope, which is ill-formed C++ ([stmt.dcl]/3). MSVC was correct
      to reject it; clang was the lenient one.
    - Fix: give the skipped model-space region its own block scope, so the label sits outside
      the scope of everything jumped over. 11 added lines, 0 modified — the goto and all 900
      lines of render code are untouched, so no behaviour could change.
    - Verified: MSVC 309/309 (separate cl.exe build tree) and clang 309/309.
    - Residual: CI builds only with clang, so this can regress unnoticed. A second matrix leg
      would catch it.

### [BUG-010] Four tests reported ***Failed while their bodies never ran — FIXED 2026-08-15
    - Symptom: `ctest` reported 4 failures (paper-circle stride, mesh state-plane origin, id sweep
      idempotence, erased-id resolution) that PASSED when run individually by name.
    - Root cause: each of those four `TEST_CASE` names contained an em dash. `catch_discover_tests`
      round-trips the name into a CTest filter through a codepage that mangles it, so Catch2
      received a filter matching nothing, printed "No test cases matched", and exited non-zero.
      The tested logic was never at fault and the test bodies never executed.
    - Fix: ASCII-only `TEST_CASE` names. Suite went 305/309 -> 309/309.
    - Prevention: rule recorded in `spec/coding-standards.md` §12 — ASCII in anything a toolchain
      re-parses (test names, `.rc` scripts), em dashes anywhere a human reads.
    - Why it mattered: REQ-202's release pipeline gates publication on a green `ctest`, so this
      would have blocked every automated release while looking like a real product defect.

## FEATURES

### [FEAT-012] Keyboard navigation inside the data grids — OPEN (deferred 2026-08-16)
    - The half of "behave like a spreadsheet" (REQ-082) that was deliberately **not** delivered in
      0.5.1: moving between cells with Tab / Enter / arrows, Esc to abandon an edit, a visibly
      focused cell that scrolls into view. Sorting, column resize/reorder/hide, frozen headers,
      uniform rows and cell-filling editors all shipped; every cell edits exactly as before, so
      nothing regressed by splitting it — the grids are simply navigated one control at a time.
    - **Blocked on a spec decision, not on code.** REQ-082 as accepted says nothing about moving
      between cells, so building it now would be implementing to an unwritten rule. Three questions
      have to be answered first (see the task): does Enter commit-and-move-down or commit-and-stay;
      does navigation past the last row create a record (a data decision — a new survey point needs
      an ID); and do the arrow keys move between cells or move the caret inside a focused field.
    - Plan, questions and the trap to avoid are in `workshop/tasks/TASK-063-req082-grid-keyboard-navigation.md`.
      The trap, recorded there: the grids iterate a **view** while addressing records by **storage**
      index, so navigation must walk the view or the cursor jumps whenever a sort is active.
    - Explicitly out of scope until separately requested: range selection, fill-down/right, cell-range
      copy/paste, one-step undo of a grid edit, formulas. Those are a spreadsheet *engine*, not a grid.

### [FEAT-011] BENCH has no mesh case, and its file record does not name the profile — DONE 2026-08-15
    - Delivered by TASK-053. `BENCH MESH [triangles] [frames]` measures a 2,000,000-triangle shaded
      terrain (the density decided 2026-08-15), forcing Shaded for the run and restoring the user's
      style afterwards; `bench-req100.txt` now records `profile` and `scene` lines for all three
      cases. Six new generator tests; suite 329/329.
    - Result: **p95 1.97 ms against 16 ms** on the RTX 5060, so REQ-100 profile (b) and REQ-064's
      "budget met in Shaded" both close. On the integrated GPU the same scene fails at 21.40 ms,
      which is how BUG-013 was found.
    - Known limit, stated up front: the scene is one mesh of one part, so per-part draw-call cost
      (a real import has hundreds) is not in this profile.
    - Original entry follows.

### [FEAT-011 detail] the gap as originally filed
    - REQ-100 defines three cost profiles. `BENCH` implements two: `BENCH [segments]` and
      `BENCH SURFACE`. There is no mesh scene and no `BENCH MESH`, so profile (b) — shaded meshes at
      the REQ-063 density — has never been measured and cannot be. TASK-041 §7 called this out on
      2026-08-12 and nothing has closed it since; TASK-052 hit it again on 2026-08-15.
    - Two requirements are held open by it: REQ-100 itself (its acceptance says "in each of the
      three profiles") and REQ-064, whose "budget met in Shaded" condition has no measurement behind
      it. Both are marked accordingly in `spec/requirements.md` rather than reading as met.
    - Fix (a): a mesh scene in `src/util/benchscene.*` at a stated triangle count, a `BENCH MESH
      [tris] [frames]` branch beside the SURFACE one in `CadCommands.cpp`, and a generator test in
      the shape of the existing ones (deterministic, byte-identical across runs, extent fixed so
      density changes rather than area — the trap TASK-039 §3 documents).
    - Fix (b), same writer, ~5 lines: `bench-req100.txt` records only `segments <n>`, so the surface
      run appears as `segments 599898` and is indistinguishable from a 600k-segment line scene. The
      console message names the profile and a comment right above the file writer explains exactly
      why that matters — the permanent record is the half that missed it. Write the profile name,
      and the point/triangle counts when it is a surface.
    - Watch out for: the mesh profile needs the depth buffer and Shaded style active to mean
      anything (TASK-040 measured depth testing at ~2 ms on its own, clang, with no meshes present),
      and the bench must restore the visual style along with everything else it already restores.

### [FEAT-010] Clean up downloaded update installers — OPEN
    - `%LOCALAPPDATA%\GoSurvey\updates` keeps every installer the updater has ever downloaded.
      Nothing deletes them after a successful install, so it grows ~6 MB per update forever.
      Observed 2026-08-16 holding both `0.5.0-beta.8` and `0.5.0` (~12 MB).
    - Not a correctness problem — the hash check and install both work — purely disk hygiene.
    - Fix: after `LaunchInstallerAndExit` succeeds, or on the next startup check, delete
      `GoSurvey-*-Installer.exe` in that folder other than the one just applied. Next-startup is
      safer: the app is exiting at launch time and the file is in use by the running installer.
    - Watch out for: a partially downloaded file from a killed run (REQ-078 already deletes those
      on failure, so anything left is from a hard kill), and not deleting an installer the user
      may still be mid-install with.

### [FEAT-002] Traverse Editor
    - [ ]   Survey Traverse editor window that allows user
            to create legs of a traverse in different ways
                - such as with a backsight point, backsight reference angle,
                face 1/face2 measurments, forsight point/points,
                horizontal distance, horizontal angle (DMS or decimal), vertical angle (DMS or decimal,
                zenith or from backsight), slope distance. All of this should optional input data based\
                on what the user wants to provide and it will be up to the traverse engine to solve the
                traverse and let the user know what data to provide if data is insufficient.
    - [~] Survey Least Squares Adjustment window that lets the user review traverse leg residuals to detect
            blunders and edit accordingly.
            - Done (REQ-014..017): "Calculate Closure" opens a window showing the unadjusted closure
              beside a weighted least-squares adjustment (closed-loop), with a per-observation
              residuals tab and configurable a-priori standard errors. Raw F1/F2 measurements and
              per-leg statistics now display in the editor (REQ-010..012). See spec ADR-001/002.
            - Done (REQ-018): each leg expands inline to an editable observation-set editor —
              add/remove sets and edit the literal F1/F2 circle readings, slope distances, and
              zenith angles; the leg re-reduces from its sets via ReduceLegFromSets (ADR-003,
              backsight reading stored on the leg). "+ Add Leg" moved into the table as its last row.
            - Remaining: connecting (point-to-point) traverses.
    - [ ] Ability to import raw data formats such as Autodesk .fbk, Bently RWD,
            Carlson RW5, Microsurvey RW5, TDS RAW,
            TDS RW5 and traverse editor gets filled in automatically

### [FEAT-003] Level Loop Editor
    - [ ] same concept as traverse editor, allowing the user to process level loop data, whether single wire
            or three wire with a least sqaures adjustment editor

# COMPLETED
~~### [BUG-002] Fuzzy find menu has some functionality problems
    - using the up arrow closes the menu
    - using the down arrow and selecting the highlighted command does not run that command
    - Fixed: command input claims Up/Down via SetItemKeyOwner so keyboard-nav no longer steals
      focus while the list is open; highlighted command is persisted across the Enter frame
      (single-line InputText self-deactivates on Enter) so submit runs the highlighted entry.
~~

~~### [BUG-003] Focusing different panels shows a ugly dark blue
    - I don't want to visually see different panels gaining focus. no color change
    - Fixed: light theme TitleBgActive set equal to TitleBg, so a focused docked node's tab-bar
      strip no longer flips to the dark caption blue (ImGui fills it with TitleBgActive on focus).
~~

~~### [BUG-001] Object hovering not working properly when in state plane coordinate system.
    - Object hovering triggers on objects even if my cursor is not "visually" touching the object
    - Fixed: pick distance math now runs in double precision (float cancellation at state-plane
      magnitudes was quantizing distances to ~1 ft); hover/click tolerance uses the robust
      outlier-trimmed extent instead of the raw bbox; idle hover highlight uses a tight fixed
      3px aperture so the cursor must visually touch the stroke at any zoom.
~~

~~### [FEAT-001] Undo Redo System
    - [ ] Full undo redo with configureable history size settings window
    - [ ] UI Buttons at the top ribbon
    - [ ] CRTL+Z and CRTL+SHIFT+Z for undo redo
    - [ ] history log should be in %APPDATA%\GoSurvey\
    - [ ] update INNO script if necessary
~~
