# TASK-305 — Multileaders + DWG (issue #619)

- Type:    feature (**SPEC GAP** — propose REQ-367, then Verification → Workshop)
- Status:  **complete** (REQ-367 v1 on `beta`, GitHub **#619 closed** 2026-10-02; PRs #645–#656)
- Opened:  2026-10-01
- GitHub:  #619, tracker #601
- Depends: R2018 export on beta (**#643**, D-2026-10-01-f) ✓

## Problem

GoSurvey has no multileader object or command. DWG open skips `MULTILEADER`; old `LEADER` imports as loose polyline + MTEXT (`ImportLeaderEntity`), not one editable callout.

## Proposed REQ-367 (draft — not in `spec/` until accepted)

**User goal:** Label monuments, easements, pipes, etc. with one object (arrow + landing + text), styled like AutoCAD MLEADER; open Civil 3D / AutoCAD callouts and re-save at R2018 without losing the object class when possible.

**Minimum scope (v1):**

1. **Domain:** `CadMultileader` (or extend `CadAnnotation`) with arrow tip, knee(s), text anchor, MTEXT payload, layer/colour/transparency, style name ref.
2. **Command:** `MLEADER` — pick arrow tip, pick landing/text location, open MTEXT editor (ribbon Multileader button wired).
3. **DWG import:** Decode `MULTILEADER` when LibreDWG provides geometry; else log + optional explode-to-lines/text fallback (REQ-201).
4. **DWG export (R2010+ default path):** Write `MULTILEADER` + `MLEADERSTYLE` (hand-built in vendored LibreDWG — no `dwg_add_MULTILEADER`). R2000/R2004 fallback: `LEADER` + `MTEXT` association where `dwg_add_LEADER` suffices.
5. **Tests:** Round-trip one synthetic multileader at R2018; import sample from AutoCAD if committed fixture exists.

**Out of v1:** block content multileaders (import log only), dogleg editing grips, custom named MLEADERSTYLE in DWG beyond Standard; full annotative (#622) for TEXT/dim/hatch/SCALE.

## Architecture check

- IO: `LibreDwgCad.cpp` import/export; possible vendored `encode.c` / object template for MULTILEADER.
- Domain: new type in `CadEntities.hpp`, persistence in `.gs` / ADR-044 trailer.
- UI: wire ribbon `##AnnMultileader`; reuse MTEXT editor patterns from Position Marker / dimensions.

## Verification gates (before code)

- [ ] User accepts REQ-367 wording in `spec/requirements.md`
- [ ] Record decision if v1 allows R2000 LEADER fallback vs R2010+ only
- [ ] architecture-review: new entity type + LibreDWG hand-write boundary

## References

- Issue #619, `docs/dwg-feature-gaps.md` Group B row
- Existing `ImportLeaderEntity` (~line 756 `LibreDwgCad.cpp`)
- D-2026-10-01-c (native DIMENSION precedent for hand-built DWG objects)
