# TASK-148 — REQ-175 DWG drawing document + GoSurvey payload

- Type:    feature
- Status:  done
- Opened:  2026-08-29
- Owner:   workshop

## 1. Authority
- Goal:         DWG as drawing file (user 2026-08-29)
- Requirements: REQ-175 (accepted D-2026-08-29-j)
- Constraints:  CON-07, REQ-201, REQ-170 CAD still for foreign DWG
- Acceptance:   restated in REQ-175
- Owning subsystem: IO (DwgIo trailer + LibreDWG), UI dialogs, headless OPEN/SAVEAS

## 3. Architectural boundary check
- [x] No — trailer is a file format recorded in ADR-044; no new layer/dependency.

## 5. Assumptions
```
ASSUMPTION-1: LibreDWG ignores a trailing JSON payload so AutoCAD still opens the CAD body.
- Because: XRECORD/dictionary add APIs are not used in this increment
- Risk if wrong: AutoCAD Recover prompt
- Validate by: user open in AutoCAD; LibreDWG re-read of CAD-only files still works
```

## 6. Plan
- Embed BuildRoot JSON after dwg_write_file; Open prefers trailer.
- File dialogs DWG; `.gs` APIs kept.
- Headless SAVEAS/OPEN of `%OUT%/*.dwg`; samples stay `.gs`.
