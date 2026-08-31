# TASK-149 — Build the Start screen tab (REQ-308)

- Type:    feature
- Status:  submitted (automated verification PASS; manual GUI pass with user outstanding)
- Opened:  2026-08-30
- Owner:   chetjones003

## 1. Authority
- Goal:         a landing surface on launch (no GOAL row — UX/shell scope, same as REQ-040/REQ-302)
- Requirements: REQ-308 (accepted 2026-08-30)
- Constraints:  CON-07 (build reproducibility); CLAUDE.md rules 1/2/3/5/6
- Acceptance:   see REQ-308 "Acceptance" — restated inline in §6 test map
- Owning subsystem: UI (`src/ui`), IO (`src/io`), Platform (`src/platform`) — per REQ-308 Owner-layer

## 2. Scope
- In scope: Start tab at drawing-tab index 0 (non-closable, pinned first, non-document);
  `DrawStartScreen` three-column panel; `RecentDrawings` MRU store; `ThumbnailCache`
  (BMP capture from the drawing viewport FBO + LRU eviction); GitHub Pages link;
  signed-in / offline-signed-out right column.
- Out of scope: real thumbnail for paper space; PNG thumbnails; a new auth flow
  (reuses `cmd.authSignInRequested`); reworking the tab/document vector model beyond
  reserving index 0.
- Smallest change: reserve `drawingTabs[0]` as a sentinel; branch `DrawDrawingViewport`
  to the start panel for index 0; two new small modules + one renderer method.

## 3. Architectural boundary check
- New data-format / ownership change / boundary touch? **Yes — escalated and recorded
  BEFORE this task:** D-2026-08-30-a (sentinel tab, architecture §11.5 amended),
  D-2026-08-30-b (`gosurvey-recent.json` store), D-2026-08-30-c (BMP thumbnail cache).
  All three `accepted`. No *further* architectural decision is taken by the Workshop.

## 5. Assumptions
```
ASSUMPTION-1: stb_image (stbi_load, already vendored) decodes the BMP thumbnails we write.
- Because: REQ-308/D-2026-08-30-c pick BMP; the loader path is LoadIconTextureRgba.
- Risk if wrong: thumbnails never display (icon fallback everywhere) — not a crash.
- Validate by: manual GUI check — save a drawing, relaunch, confirm the image renders.
```
```
ASSUMPTION-2: index 0 being active with no document swap does not desync documents[]/
  viewportRenderers[] — the provisioning loops already size to activeDrawingIdx.
- Because: main.cpp sizes both vectors with `while (size <= activeDrawingIdx)`.
- Risk if wrong: crash on launch dereferencing viewportRenderers[0].
- Validate by: build + launch; ViewportRenderer[0] is Init'd at main.cpp:248 already.
```

## 6. Plan
- **`src/platform/ThumbnailCache.hpp`** (header-only, pure): `ThumbFileName(drawingPath)`
  (FNV-1a hex + ".bmp"), `EvictThumbnails(dir, maxFiles)` (LRU by last-write-time).
- **`src/io/RecentDrawings.{hpp,cpp}`** (pure; domain + test targets): `Entry{path,name,thumb,lastOpenedUnix}`,
  `Load(jsonFile)`, `Note(jsonFile, path, thumbFilename, nowUnix)`, `Remove(jsonFile, path)`.
  Cap `kMaxEntries = 20`. Corruption / missing → `{}`.
- **`src/ui/CadUi_StartScreen.cpp`** (GoSurvey exe only): `DrawStartScreen(cmd, log)`;
  path helpers `RecentJsonPath()` / `ThumbnailDir()`; `RecordRecentDrawing(cmd, absPath)`;
  `ServicePendingThumbnail(cmd, renderer)`. Owns a small `path -> GL texture` cache for tiles.
- **`src/render/ViewportRenderer`**: add `bool CaptureThumbnailBmp(const char* path, int maxDim) const`
  (binds `fbo_`, `glReadPixels`, nearest-box downscale, writes bottom-up 24-bit BMP). Keeps `gl*`
  inside the renderer (invariant §11.6).
- **`src/commands/CadCommands.hpp`**: seed `drawingTabs{{"Start",0u},{"Drawing 1",1u}}`,
  `documents{2}`; `activeDrawingIdx`/`prevDrawingIdx` stay 0 (Start); add
  `inline int FirstDrawingTabIndex(){return 1;}`; add `pendingThumbnailPath` / `pendingThumbnailTabIdx`.
- **`src/ui/CadUi.cpp`**: tab bar — Start tab drawn with `Leading` + no close button + no `##uid` uid 0;
  close guard `i >= FirstDrawingTabIndex()`; `DrawDrawingViewport` branches to `DrawStartScreen`
  for `activeDrawingIdx == 0` (no GL image, no viewport interaction). `SaveActiveDocument` +
  File>Open + File>Save As + startup adoption call `RecordRecentDrawing`.
- **`src/app/main.cpp`**: after load, `SaveDocumentToSnapshot(cmd, FirstDrawingTabIndex())`;
  command-line-open lands on index 1 active; switch-detection skips `RestoreDocumentFromSnapshot`
  when new active is 0; skip `RenderScene` + service pending thumbnail when `activeDrawingIdx == 0`;
  `RecordRecentDrawing` on `OpenDrawingDocument` success (startup + Ctrl+S path at main.cpp:630).
- **`CMakeLists.txt`**: add `RecentDrawings.cpp` to `GOSURVEY_DOMAIN_SOURCES` + `GoSurveyTests`;
  add `CadUi_StartScreen.cpp` to the `GoSurvey` target; add `tests/RecentDrawingsTests.cpp` +
  `tests/StartScreenTests.cpp`.
- Test approach: happy = MRU round-trip / dedupe-reorder / cap / ThumbFileName determinism /
  eviction keeps newest / `FirstDrawingTabIndex` + close-guard predicate; failure = corrupt json
  → empty, missing file → empty, `Remove` of absent path is a no-op, eviction on an empty dir.
- Steps:
  - [x] spec: REQ-308 + D-2026-08-30-a/b/c + architecture §11.5 + docs
  - [x] ThumbnailCache.hpp
  - [x] RecentDrawings.{hpp,cpp}
  - [x] ViewportRenderer::CaptureThumbnailBmp
  - [x] CadCommands.hpp state
  - [x] CadUi_StartScreen.cpp
  - [x] CadUi.cpp tab bar + branch + record hooks
  - [x] main.cpp wiring
  - [x] CMake + tests
  - [x] build + ctest + self-verify

## 7. Workflow-specific notes
- Feature: pre-flight answered (thumbnails=BMP capture-on-save/open; signed-out=offline notice;
  URL=chetjones003.github.io/GoSurvey — all confirmed by user 2026-08-30). Tests-first for the
  two pure modules; the GUI panel is manual-verify (project norm — no ImGui automation).

## 8. Implementation log
- 2026-08-30 open → plan → spec layer recorded (REQ-308 accepted, 3 decisions) → implement
- 2026-08-30 splash->app transition glitch: after the 5s splash, GlfwApplyMainStageWindowChrome
  maximizes the small (~440x320) splash window; its stale front buffer was stretched fullscreen for
  the frames DWM takes to catch up. First fix (3x clear+swap) still showed it for a couple frames —
  DWM stretches the old content *during* the maximize, before any clear runs. Final fix in main.cpp:
  `glfwHideWindow` across the maximize AND the whole pre-loop setup; reveal with `glfwShowWindow`
  only after the first real UI frame is rendered+swapped (`mainWindowShown` flag at the bottom of
  the loop). A hidden window composites nothing, so there is no stale buffer to stretch. GLFW 3.4
  applies maximize state to a hidden window without showing it, so the first frame already renders
  full-size. GUI-only verification (handed to user).
- 2026-08-30 splash fixes: banded concentric-circle glow removed (looked stepped); phase text +
  progress bar now bottom-anchored via SetCursorPosY so the version pill can't push them out of the
  card; badge sheen on BOTH DrawGsBadge and SplashGsBadge changed to a clipped vertical gradient
  (no hard midline) so the two marks match exactly.
- 2026-08-30 user follow-on: restyled the REQ-093 startup splash (SplashScreen.cpp) to match — blue
  accent gradient card, top accent bar + soft glow, crisp vector "GS" badge (dropped the upscaled
  32px app.png load), centered version pill, accent progress bar. Cosmetic only (D-2026-08-23-g);
  timing / phases / 5s duration unchanged. `SplashGsBadge` is a deliberate ~15-line copy of the
  Start screen's `DrawGsBadge` (CLAUDE.md rule 2 — cheaper than a shared UI module for two cosmetic
  paints). 800/800.
- 2026-08-30 user fixes: (a) Connect blurb was clipped — `PushTextWrapPos` was fed a screen X, not
  a window-local one; now `PushTextWrapPos(0)`. Dropped the U+2197 glyphs (font has no arrow → box).
  (b) Hero icon replaced with a crisp vector `DrawGsBadge` (rounded accent square + "GS") — the
  32px title-bar texture blurred when upscaled. (c) Command line now also gated by
  `showDrawingPanels` in main.cpp — hidden on the Start tab, back on a drawing. Spec updated.
- 2026-08-30 user tweak: Connect column gains a "Help Me Improve This Product" section + a
  "Send Feedback" ghost button opening github.com/chetjones003/GoSurvey/issues. `OpenWebsite`
  generalised to `OpenUrl(const char*)`. Spec REQ-308 right-column bullet updated. 800/800.
- 2026-08-30 visual polish pass (user: "too boring and flat"): CadUi_StartScreen.cpp now paints a
  full-bleed gradient bg + hero band (accent bar, wordmark, version pill, tagline, app logo if set),
  rounded accent/ghost buttons, section headings with an accent underline, rounded recent-drawing
  cards with drop shadow + hover accent ring (grid default, bigger tiles), a styled list view, a
  centered empty state, and a Connect card with an initials avatar. All draw-list painting; no new
  state, no logic change. Builds clean, 800/800.
- 2026-08-30 user tweak: link text "Visit The Website" with no URL; Properties/Reports/Toolspace
  panels not submitted while `activeDrawingIdx == 0` (dock nodes collapse → Start screen full-bleed;
  they re-dock on switch to a drawing). `showDrawingPanels` bool in main.cpp gates all three. Spec
  REQ-308 amended. Rebuilt, 800/800.
- 2026-08-30 all steps done. New: `ThumbnailCache.hpp`, `RecentDrawings.{hpp,cpp}`,
  `CadUi_StartScreen.cpp`, `ViewportRenderer::CaptureThumbnailBmp`, `AppIcon::DestroyIconTexture`,
  `FirstDrawingTabIndex()`, `NewDrawingInTab`/`OpenDrawingInNewTab` (extracted from the File menu so
  the menu, the "+" button and the Start screen share one path). `ToolspaceActiveDrawingName` now
  skips the Start sentinel (fixed 2 ToolspaceCatalogTests that constructed a bare state expecting
  "Drawing 1"). Tests: `tests/RecentDrawingsTests.cpp` (8 cases), `tests/StartScreenTests.cpp`
  (5 cases). `./dev/build` clean, `./dev/test` 800/800.
- Within-boundary decision: `g_thumbTex` (file-local path→GL-texture cache in CadUi_StartScreen.cpp)
  — same category as the existing `g_menuBarLogoTex` UI texture handle, not new cross-subsystem
  state; reloading each tile per frame (decode + upload) was the alternative and is clearly worse.
- ASSUMPTION-1 (stb_image decodes our BMP): `LoadIconTextureRgba` → `stbi_load`, which supports BMP;
  our writer emits a standard bottom-up 24-bit BMP. Still flagged for the manual GUI check.

## 9. Self-verification
- [x] build-project        — PASS (`./dev/build`, MSVC /W4, no new warnings)
- [x] architecture-review  — PASS. gl* stays in ViewportRenderer (`CaptureThumbnailBmp`) and
      Platform (`DestroyIconTexture`); §11.5 amended by D-2026-08-30-a for the sentinel tab; new
      store is D-2026-08-30-b; BMP cache is D-2026-08-30-c. No Workshop architectural decision.
- [x] code-review          — PASS. Shared New/Open path removes prior duplication; close-guard and
      switch-detection both route index 0 through `FirstDrawingTabIndex()` / an explicit `!= 0`.
- [x] dependency-audit     — PASS. No new dependency (BMP via the in-tree writer pattern; decode via
      already-vendored stb_image).
- [x] performance-review   — n/a. Thumbnail capture is one FBO readback per save/open, serviced once
      then cleared; the Start screen is only drawn while its tab is active.
- [x] testing              — PASS. 13 new Catch2 cases; full suite 800/800.
- [ ] MANUAL GUI (handed to user): Start tab renders 3 columns / non-closable / pinned first;
      thumbnails appear after save+relaunch; DWG placeholder otherwise; grid+list+sort+search;
      recent click opens a focused tab; website link opens the browser; signed-in "Welcome".

## 10. Verification result
- Verdict: PASS (self-verified; automated coverage green). Manual GUI pass outstanding with the user.

## 11. Outcome
- Requirements satisfied: REQ-308 (Acceptance: automated conditions met; GUI conditions pending manual pass)
- Tests added: RecentDrawingsTests [req308] x8, StartScreenTests [req308] x5
- Refactors: File ▸ New / Open extracted to NewDrawingInTab / OpenDrawingInNewTab
- Docs updated: spec/requirements.md, spec/project.md, spec/architecture.md, docs/ARCHITECTURE.md, README.md
- Done: pending manual GUI confirmation
