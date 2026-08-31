// CadCommandsInternal.hpp — command-layer helpers shared BETWEEN the translation
// units that CadCommands.cpp was split into (TASK-150 Phase 2, GitHub issue #142).
//
// These are NOT public product API. They are declared here — rather than being
// file-local statics of CadCommands.cpp — only so a cohesive command slice living
// in its own .cpp (CadCommands_Ucs.cpp, …) can call them. Everything here is
// defined once, in CadCommands.cpp.
//
// If a helper is needed by exactly one slice and nothing else, prefer moving it
// into that slice instead of adding it here.

#pragma once

#include <vector>

#include "CadEntities.hpp"  // EntityAttributes

struct AppCommandState;

/// Fresh entity attributes for newly-drawn geometry: current layer, everything
/// else ByLayer / default.
EntityAttributes MakeNewEntityAttrs(const AppCommandState& st);

// --- Draft-state resets (defined together as a cluster in CadCommands.cpp) ---
void ResetCircleDraft(AppCommandState& st);
void ResetPolylineDraft(AppCommandState& st);
void ResetFeatureLineDraft(AppCommandState& st);
void ResetArcDraft(AppCommandState& st);
void ResetEllipseDraft(AppCommandState& st);
void ResetRectDraft(AppCommandState& st);
void ResetTextCmdDraft(AppCommandState& st);
void ResetMtextDraft(AppCommandState& st);
void ResetDimDraft(AppCommandState& st);
void ResetDimAngularDraft(AppCommandState& st);
void ResetSurveyInverseDraft(AppCommandState& st);
/// Resets every in-progress draft/prompt tool. Call on command start, cancel, or
/// clear-geometry so no half-collected state leaks into the next command.
void ResetAllCadDraftTools(AppCommandState& st);

/// Clears a pending ZOOM EXTENTS / ZOOM WINDOW request.
void ClearPendingViewportZoom(AppCommandState& st);
