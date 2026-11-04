# Verification: TASK-479 Civil 3D semantic mapping plan

Date: 2026-10-31

## Architecture

**PASS.** D-2026-10-31-a/b and ADR-074 require in-process semantic decoding, mapping into GoSurvey objects,
and ordinary AutoCAD entity export. Native Civil 3D custom-object authoring/identity is explicitly out of
scope. IO owns binary decoding and DWG entity export; Domain owns mapped objects/styles; UI reads domain state.

## Correctness and risk

The baseline gives a concrete oracle: 353 COGO points, one TIN surface, parcels, and drawing-owned styles.
The prior API export is useful reference evidence, but does not prove the native payload format. Tests must
compare raw-DWG decoding to those values and must run with no CAD application available. The surface test
must compare actual triangle topology and point coordinates, not only object counts. Proxy graphics cannot
substitute for semantic data. Unmapped source classes/fields need explicit import/export log entries.

## Dependencies and performance

**PASS.** No new shipped dependency is proposed. REA and the installed Civil 3D runtime are development-only
analysis/oracle tools. Parsing may affect file-open time; no frame/render path changes are planned.

## Required verification

1. A no-CAD-runtime import test against the supplied baseline with point fields, TIN topology and style refs.
2. Malformed/truncated custom-object payload tests that leave the document unchanged.
3. GoSurvey DWG save/reopen tests for mapped data and style references.
4. AutoCAD reopen without Recover for the ordinary entity output, when available as an external test oracle.
5. Full Windows release build, applicable CTest coverage, and an in-app baseline open.

## Verdict

**APPROVE amended plan for implementation.** This approves the mapping approach only; REQ-479 is not met until
all supported baseline fields, TIN topology, export validity, and app behavior have implementation and test evidence.

### Amendment (2026-10-31)

The user clarified that GoSurvey decodes Civil 3D semantics into GoSurvey objects and writes ordinary valid
DWG entities; it does not create Civil 3D objects or preserve their native identities. The previous review's
native-object round-trip requirements are superseded by D-2026-10-31-b.
