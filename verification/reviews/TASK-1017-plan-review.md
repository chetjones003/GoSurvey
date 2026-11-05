# Verification: TASK-1017 paper viewport scale list and match-to-scale plan

Date: 2026-11-03

## Architecture

**PASS.** REQ-484 assigns the dropdown to Qt properties and scale validation/mutation to Commands. The named scale list and paper viewport values are already drawing-owned; selection is already represented in `AppCommandState`; undo is already command-owned. The plan introduces no new ownership, format, dependency, or global state. It explicitly preserves the existing model editor picker and floating `VPSCALE` workflow.

## Correctness and risk

The properties dropdown and command must update the actual viewport model-to-paper ratio, not the annotative scale attached to annotations. Both entry points should call the same underlying scale application operation. Snapshot the target viewports' scale values and use one undo item for the whole operation; do not copy, rebuild, or modify viewport centers or sheet rectangles. Ignore or reject invalid and locked targets consistently with existing viewport scale-change rules, and ensure a rejected command has no undo side effect. The active layout constrains selected indices; stale or cross-layout selection must not mutate another layout.

## Dependencies and performance

**PASS.** No new dependency or expensive algorithm is expected. Work is bounded by the current selection size and uses the existing named-scale conversion helper.

## Required verification

1. Properties UI exposes the drawing's named scales and applies a chosen value to selected paper viewport(s).
2. Command applies a chosen named scale to all selected paper viewports and reports invalid names or invalid selection without mutation.
3. Snapshot center coordinates and paper frame coordinates/dimensions before applying the scale and verify exact preservation afterward.
4. Verify one undo restores all affected scales and redo reapplies them.
5. Verify the model editor scale picker and existing `VPSCALE` floating viewport workflow remain unchanged.
6. Run the Windows build and focused command/Qt tests; capture affected Qt property states and follow the required Civil 3D comparison workflow. If Civil 3D is unavailable, record the comparison as inconclusive.

## Verdict

**APPROVE the plan for implementation.** The user's confirmed scope resolves the behavior; the plan respects the existing architecture and gives direct regression coverage for scale-only mutation, multi-selection, undo/redo, and preserved model-editor behavior. No tests or builds were run during this plan review.
