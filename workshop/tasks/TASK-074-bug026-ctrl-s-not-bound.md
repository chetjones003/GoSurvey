# TASK-074 — BUG-026: Ctrl+S is advertised in the File menu but bound to nothing

- Type:    bug
- Status:  done — fixed, built, tested, and verified in the running application
- Opened:  2026-08-20
- Owner:   Workshop (Nathan)

## 1. Authority

- Requirements: **REQ-201** (no silent failures — no failure path is empty)
- Precedent:    **REQ-047** — "F8 works even while the command bar has keyboard focus", the rule this
                fix follows for deciding that Ctrl+S is *not* gated on `WantTextInput`
- Constraints:  **REQ-301** (no new abstraction); architecture — UI owns the File menu, `app` owns
                the frame's key handling
- Acceptance:   pressing Ctrl+S performs File > Save; a control the UI advertises does what it says,
                or does not advertise it.
- Owning subsystem: UI (`CadUi` owns the save action) / App (`main.cpp` owns frame key handling).

### Authority caveat — read this before merging

**No `accepted` REQ states that a Ctrl+S shortcut exists.** REQ-201 is the closest fit and the one
cited: a menu item that draws the text "Ctrl+S" while installing no binding is an affordance that
fails silently, which is exactly the failure class REQ-201 exists to forbid. But that is an argument
by analogy, not a requirement being violated in its own words.

Recorded rather than papered over. Two ways to close it, both chet's call, neither blocking this fix:

1. Treat the menu label as the de-facto spec — the UI already promises this in shipped builds — and
   merge on REQ-201.
2. Add a one-line requirement (or an amendment to **REQ-040**, which owns the command line and its
   keys) naming the application's keyboard accelerators. That would also give Ctrl+Z/Y/C/V, F3 and
   F8 a written home; today F3/F8 are specified only incidentally inside REQ-047.

The alternative reading of CLAUDE.md step 1 — "if no requirement exists, stop" — would make this a
Specification task. I judged a UI control that lies about itself to be a defect rather than a
feature request, and proceeded. If that judgement is wrong the code is a 3-line revert.

## 2. Bug report

| # | Observed | Expected |
|---|----------|----------|
| 1 | File > Save displays the shortcut "Ctrl+S". Pressing Ctrl+S does **nothing at all** — no save, no dialog, no log line | Ctrl+S performs File > Save |
| 2 | On a drawing with a known path, the file's mtime is unchanged after Ctrl+S | the file is rewritten |

Found 2026-08-20 while learning the LINE command in the GUI, using Ctrl+S to read committed geometry
back out of a `.gs`. Not caused by any change under test; pre-existing in `master` @ `6024e0b`.

## 3. Root cause (evidence, not hypothesis)

`src/ui/CadUi.cpp`:

```cpp
if (ImGui::MenuItem("Save", "Ctrl+S")) { ... }
```

**ImGui's second `MenuItem` argument is a label it right-aligns and draws. It installs no binding.**
The application must handle the key itself, and nothing did: `main.cpp` handles F3 and F8 explicitly,
and has a `ctrlHeld && !WantTextInput` block for Ctrl+Z / Y / C / V — Ctrl+S was simply never added
to either. So the shortcut has never worked in any build; it has only ever been *drawn*.

## 4. Questions

| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | Authority: fix under REQ-201, or escalate as a SPEC GAP for a shortcuts requirement? | 2026-08-20 | Proceeded under REQ-201; escalation recorded in section 1 above for chet to settle. |

## 5. Assumptions

```
ASSUMPTION-1: Ctrl+S should fire even while the command bar has keyboard focus.
- Because:       the existing ctrlHeld block gates on !WantTextInput, so the gating question is live.
- Reasoning:     Ctrl+Z/C/V are gated because each has a meaning INSIDE a text field and must yield
                 to it. Ctrl+S has no such meaning. REQ-047 already settled the same question for
                 F3/F8 in favour of the key working during text input, and AutoCAD saves regardless
                 of where focus sits. Gating it would mean a user who typed a command then pressed
                 Ctrl+S would silently not save — re-creating this very bug in a narrower case.
- Risk if wrong: a user who wanted a literal Ctrl+S inside a text field cannot get one. No text
                 field in the application uses Ctrl+S for anything, so the risk is nil today.
- Validate by:   verified manually — Ctrl+S pressed with the command box focused DID save (section 8).
```

## 6. Plan

- Approach: extract the File > Save body into `SaveActiveDocument(cmd, log)` in the UI layer, then
  call it from both the menu item and a new Ctrl+S handler in `main.cpp`. One implementation, two
  callers — the menu and the accelerator cannot drift apart, which is the condition that let this bug
  exist unnoticed.
- Files/functions to touch:
  - `src/ui/CadUi.hpp` — declare `SaveActiveDocument`
  - `src/ui/CadUi.cpp` — define it; menu item becomes a one-line call
  - `src/app/main.cpp` — handle Ctrl+S beside the F3/F8 mode keys
- Test approach: happy path = Ctrl+S on a drawing with a path saves silently; failure mode = Ctrl+S
  on a drawing with **no** path browses, and adopts the destination so the *next* Ctrl+S is silent.
- Steps:
  - [x] extract `SaveActiveDocument`
  - [x] point the menu item at it
  - [x] bind Ctrl+S
  - [x] build + full suite
  - [x] verify all three cases in the running application

## 7. Workflow-specific notes

- Bug: root cause = an ImGui `MenuItem` shortcut string is decorative, and no key handler existed.
  Regression test fails-before? **No automated test — see section 9 "testing".** Keyboard accelerators
  are handled inside the ImGui frame loop in `main.cpp`, which the headless harness does not run: the
  harness drives `ProcessCommandLineSubmit` / `SubmitViewportPick` beneath the UI layer and has no
  verb that sets a key state. Verified manually instead, evidence in section 8.

## 8. Implementation log

- 2026-08-20 Reproduced against `master` @ `6024e0b`, release build: drew a line, pressed Ctrl+S,
  file mtime unchanged and no dialog. Confirmed the cause by reading the ImGui `MenuItem` contract.
- 2026-08-20 Extracted `SaveActiveDocument`. Kept its behaviour byte-identical to the old menu body,
  including adopting the chosen path and renaming the drawing tab — that adopt step previously lived
  only in the menu, so binding the key without extracting would have given Ctrl+S a *subtly different*
  save than the menu's.
- 2026-08-20 Bound Ctrl+S in `main.cpp` beside F3/F8, deliberately **not** inside the
  `ctrlHeld && !WantTextInput` block. Reasoning in ASSUMPTION-1.
- 2026-08-20 Build: initially failed with `cannot open include file: 'cstdint'` — environmental, the
  shell had no MSVC environment. Rebuilt under `vcvars64.bat`: clean, 64/64.
- 2026-08-20 Verified in the running application (all three, synthesized input, geometry read back
  out of the saved `.gs`):
  - Ctrl+S on a drawing with **no** path -> Save As dialog opened. Pressed **with the command box
    focused**, which also validates ASSUMPTION-1.
  - Completed that dialog -> file created, and the drawing tab renamed itself to `ctrls-saved`.
  - Drew a second line, Ctrl+S again -> **silent re-save**, mtime moved, and the saved `.gs` contained
    both lines: `(0,0)->(10,10)` and `(50,0)->(50,25)`.
  - Regression: File > Save still saves silently after a further edit.

## 9. Self-verification

- [x] build-project        — PASS (clean, 64/64 targets, MSVC 14.44)
- [x] architecture-review  — PASS. No new abstraction (REQ-301): one function extracted from an
      existing body, no new type, layer or dependency. Ownership unchanged — the save action stays in
      UI, which already owned it; `main.cpp` only *calls* it, which is the direction dependencies
      already flow.
- [x] code-review          — PASS. Net -11 lines. The extracted function is the old body with early
      returns instead of nesting.
- [x] dependency-audit     — n/a (no dependency change)
- [x] performance-review   — n/a (two key-state reads per frame, both already computed by ImGui)
- [x] testing              — **PARTIAL, and stated plainly:** full suite green (436/436, one
      pre-existing DISABLED test not run), but **no new automated test.** The bug lives in the ImGui
      frame loop, which no test harness drives; a test could only be written by first building a way
      to inject key state into `main.cpp`'s loop, which is an architectural change and out of scope
      for a 3-line bug fix. Manual verification is recorded in section 8 in enough detail to re-run.

## 10. Verification result

- Submitted:  2026-08-20 (PR to `chetjones003:master`)
- Verdict:    pending — chet's merge is the verification act
- Findings:   —

## 11. Outcome

- Requirements satisfied: REQ-201 (Acceptance met: yes — the advertised control now performs its
  advertised action). **Authority caveat in section 1 is open and is chet's to settle.**
- Tests added:            none — see section 9 `testing` for why, and section 7 for the harness limitation
- Refactors:              `SaveActiveDocument` extracted so the menu and the accelerator share one
                          implementation
- Docs updated:           `TRACKER.md` (BUG-026)
- Technical debt noted:   **(a)** keyboard accelerators have no spec home and no automated coverage —
                          the same class of bug can recur silently for Ctrl+Z/Y/C/V, F3, F8 or any
                          future shortcut, and nothing would catch it. **(b)** BUG-027, filed
                          alongside this and NOT fixed here: opening a `.gs` via the file association
                          leaves `activeDocFilePath` empty, so the first Ctrl+S on a double-clicked
                          drawing asks where to save it.
- Done:                   2026-08-20
