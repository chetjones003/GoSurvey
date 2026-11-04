# Verification: TASK-780 DXF blocks and INSERTs — plan review

Date: 2026-11-04

## Authority and scope

Issue #780 acceptance is grounded in accepted REQ-113. The current writer has an explicit skip log for paper INSERTs and emits only the fixed model/paper BLOCK pairs. The issue asks for BLOCK definitions and INSERT references in model space and every layout, with matching names, transforms, attributes, and owners. REQ-113 also requires malformed paper entities to be rejected and unsupported ones to be reported.

## Architecture review

**PASS.** `AppCommandState` already owns `CadBlockDefinition` values, model `CadBlockRef` values, and each layout's paper block references. The plan serializes these existing values with standard DXF records and does not introduce a new owner, layer, dependency, or application format. Import preservation can use the same existing values. No SPEC gap is identified.

## Required plan refinements

1. Do not change imported INSERTs from flattened geometry to true references without checking current behavior and test expectations; the acceptance criterion requires true round-trip references, so any behavior change must be explicit and tested.
2. Handle only definition content the current `CadBlockContent`/DXF entity codec can represent. Report unsupported definition members by type/count per REQ-201; do not silently omit them.
3. Test attributes supported by `CadBlockRef::attributes`, and record any DXF attribute forms the model cannot represent instead of guessing.
4. Keep model geometry's document-origin conversion distinct from paper inches. Paper references must use the layout block owner and group 67/410 tags.
5. Include hard failure tests for missing definitions, malformed transforms, and cyclic/nested references; bound traversal and avoid dangling handles.

## Verdict

**APPROVE WITH THESE CONDITIONS.** The plan fits the existing architecture and accepted scope. Implementation must satisfy each listed failure/reporting condition and must not expand block-content support beyond the current model without a SPEC decision.
