void StartMoveCommand(AppCommandState& st, std::vector<std::string>& log);
void StartCopyCommand(AppCommandState& st, std::vector<std::string>& log);
void StartRotateCommand(AppCommandState& st, std::vector<std::string>& log);
void StartScaleCommand(AppCommandState& st, std::vector<std::string>& log);
void StartMirrorCommand(AppCommandState& st, std::vector<std::string>& log);
void StartArrayCommand(AppCommandState& st, std::vector<std::string>& log);
void StartLengthenCommand(AppCommandState& st, std::vector<std::string>& log);
/// Model-space + floating-model-space viewport-pick handler for LENGTHEN. Non-static (unlike most
/// of its cluster) purely so SubmitViewportPickImpl — inside an anonymous namespace that spans
/// this function's definition — can see it via this header; not intended for cross-TU use.
void HandleLengthenViewportPick(AppCommandState& st, float wx, float wy, std::vector<std::string>& log);
void StartExtendCommand(AppCommandState& st, std::vector<std::string>& log);
/// Model-space + floating-model-space viewport-pick handler for EXTEND. Non-static (unlike most of
/// its cluster) for the same anonymous-namespace/global-scope reason `HandleLengthenViewportPick`
/// is — `SubmitViewportPickImpl` needs to see it via this header.
void HandleExtendViewportPick(AppCommandState& st, float wx, float wy, std::vector<std::string>& log);
void StartBreakCommand(AppCommandState& st, std::vector<std::string>& log);
/// Model-space + floating-model-space viewport-pick handler for BREAK. Non-static for the same
/// anonymous-namespace/global-scope reason `HandleLengthenViewportPick`/`HandleExtendViewportPick`
/// are — `SubmitViewportPickImpl` needs to see it via this header.
void HandleBreakViewportPick(AppCommandState& st, float wx, float wy, std::vector<std::string>& log);
/// REQ-103 step 5. Model-space branch always forces a fresh box-select (\c ModifyPhase::PickSelection),
/// even if \c st.selection is already non-empty, since STRETCH's box rectangle must be captured
/// together with the selection (unlike MOVE/COPY/ROTATE/SCALE, which happily reuse a pre-existing
/// selection). Paper-space branch instead requires \c st.selectedPaperEntities already non-empty —
/// see \c paperStretchPhase's comment for why paper space follows the opposite convention.
void StartStretchCommand(AppCommandState& st, std::vector<std::string>& log);
void StartFilletCommand(AppCommandState& st, std::vector<std::string>& log);
/// Model-space + floating-model-space viewport-pick handler for FILLET. Non-static for the same
/// anonymous-namespace/global-scope reason `HandleLengthenViewportPick`/`HandleExtendViewportPick`/
/// `HandleBreakViewportPick` are — `SubmitViewportPickImpl` needs to see it via this header.
void HandleFilletViewportPick(AppCommandState& st, float wx, float wy, std::vector<std::string>& log,
                              const ray3d::Ray* pickRay = nullptr);
/// Typed command-line handling for FILLET's R(adius)/T(rim) sub-commands (REQ-103 step 6a) — the
/// mode-letter shape LENGTHEN's own `HandleLengthenText` established, simplified: FILLET has no
/// pending-pick-awaiting-a-value latch, since R/T only ever change a persisted setting, never
/// apply to an already-picked object.
/// **The return value means "was this input UNDERSTOOD", not "did the command advance"**, and the
/// distinction is load-bearing: the one and only thing the caller does with it is decide whether to
/// append `ReportUnparsedCommandInput`'s trailer. So a value the kernel REFUSED returns **true** —
/// it was understood, it was declined by name, and the prompt is still up — while a stray token
/// returns **false**, which is what earns the genuinely useful "FILLET is still running, so
/// \"circle\" was read as input to it; press Esc" hint.
///
/// Conflating the two is what TASK-224 fixed: a refused radius used to be followed by "Could not
/// parse FILLET input", which contradicted the refusal printed one line above it.
bool HandleFilletText(AppCommandState& st, const std::string& lineIn, std::vector<std::string>& log);
void StartChamferCommand(AppCommandState& st, std::vector<std::string>& log);
/// Model-space + floating-model-space viewport-pick handler for CHAMFER. Non-static for the same
/// anonymous-namespace/global-scope reason `HandleFilletViewportPick` is.
void HandleChamferViewportPick(AppCommandState& st, float wx, float wy, std::vector<std::string>& log);
/// Typed command-line handling for CHAMFER's D(istance)/A(ngle)/T(rim) sub-commands (REQ-103 step
/// 6b) — same shape as `HandleFilletText`, **including what its return value means**: true when the
/// input was understood (a refused distance included), false only when it was not.
bool HandleChamferText(AppCommandState& st, const std::string& lineIn, std::vector<std::string>& log);
void StartDeleteCommand(AppCommandState& st, std::vector<std::string>& log);
void StartJoinCommand(AppCommandState& st, std::vector<std::string>& log);
/// QUICKSELECT's filter itself (issue 05). Lives here rather than in the UI layer so it is
/// reachable from a headless transcript and from a unit test; the panel is a form that calls it.
void ExecuteQuickSelect(AppCommandState& cmd, std::vector<std::string>& log);
void StartQuickSelectCommand(AppCommandState& st, std::vector<std::string>& log);
void StartTrimCommand(AppCommandState& st, std::vector<std::string>& log);
void StartOffsetCommand(AppCommandState& st, std::vector<std::string>& log);
/// Re-invokes \c st.lastCommand (no-op if \c Kind::None).
void RepeatLastCommand(AppCommandState& st, std::vector<std::string>& log);

/// Removes selected entities from the drawing and clears selection. No-op if selection empty.
void EraseCadAnnotationAtIndex(AppCommandState& st, size_t annIndex);
void DeleteSelectedSurveyPoints(AppCommandState& st, std::vector<std::string>& log);
void SyncSurveyPointLinkedMtextSelection(AppCommandState& st, int surveyPointIndex);
void ApplyLinkedSurveyForAnnotationPick(AppCommandState& st, int annIndex, bool keyShift);
void ExecuteDeleteSelection(AppCommandState& st, std::vector<std::string>& log);
/// Join selected lines / polylines at coincident endpoints into polylines (window-select like DELETE).
void ExecuteJoinSelection(AppCommandState& st, std::vector<std::string>& log);
/// EXPLODE (REQ-103 step 8 / issue #390): decompose every selected Polyline into one LINE per straight
/// segment and one ARC per bulge segment (flat or tilted), preserving per-vertex Z and the polyline's
/// attributes; report other non-block selected kinds (REQ-201). Returns the polyline count exploded.
/// The caller owns the undo snapshot, the id sweep and the GPU-cache bump.
int ExplodeSelectedPolylines(AppCommandState& st, std::vector<std::string>& log);
/// OVERKILL — remove zero-length segments, exact duplicates, collinear overlapping/contiguous lines
/// (merged into one), duplicate circles/arcs, and arcs whose circle matches an existing full circle.
/// Operates on the entire drawing immediately; no selection required.
void ExecuteOverkill(AppCommandState& st, std::vector<std::string>& log);
/// TRIM — pick cutting edges, Enter, trim clicks; or \p L then two points: draws the segment to trim (nearest edge),
/// trims once at nearest crossing (fence disambiguates), then TRIM ends.
bool SubmitTrimViewportPick(AppCommandState& st, float wx, float wy, float tolWorld, std::vector<std::string>& log,
                            const ray3d::Ray* pickRay = nullptr);
/// Preview for TRIM \p L rubber phase; pass the drawn segment midpoint as \p pickPreview (same side rule as commit).
void CadTrimAppendCutLineRemovedPreview(const AppCommandState& st, float fenceP1x, float fenceP1y, float fenceP2x,
                                        float fenceP2y, float pickPreviewX, float pickPreviewY,
                                        std::vector<float>* previewLinesOut);
/// issue #399 increment 4: the orbited-view / non-world-UCS counterpart. Given the drawn trim
/// segment in true 3D (\p f0 -> \p f1, real elevations), appends the portion the commit would remove
/// as one XYZ line segment — resolved by the same 3D solve the commit uses (\ref Try3DDrawnLineTrim),
/// so the dashed hint lands on the geometry, not the datum. No-op when there is no trim to make.
void CadTrimAppendCutLineRemovedPreview3D(const AppCommandState& st, const ray3d::Vec3& f0, const ray3d::Vec3& f1,
                                          std::vector<float>* previewLinesOut);

/// World pick tolerance for OFFSET entity selection (geometry scale + screen aperture).
[[nodiscard]] float CadOffsetEntityPickTolWorld(const AppCommandState& st);
/// Tight world pick tolerance for the idle hover highlight: fixed small pixel aperture so the cursor must
/// visually touch the stroke at any zoom level.
[[nodiscard]] float CadHoverEntityPickTolWorld(const AppCommandState& st);
/// Live offset preview from cursor (through mode or typed distance + side); clears vectors first.
void CadOffsetAppendLivePreview(const AppCommandState& cmd, float cursorWx, float cursorWy,
                                std::vector<float>* previewLines, std::vector<float>* previewCircles);

/// Translate the whole selection by (\p dx, \p dy, \p dz) in WCS (REQ-322). Surfaces are dropped by
/// name (REQ-201); solids move through `brep::Translate`. The caller owns the undo snapshot.
///
/// Declared here — it had been a definition private to `CadCommands.cpp` — because REQ-060's
/// acceptance is that a gizmo drag and the typed command agree, and the only way to make that a
/// property rather than a hope is for both to call this. A test that asserts the agreement has to be
/// able to name it too.
void ApplyTranslationToSelection(AppCommandState& st, float dx, float dy, float dz,
                                 std::vector<std::string>& log);

/// Rotate the whole selection by \p rad about the axis through (\p bx, \p by, \p bz) parallel to the
/// ACTIVE UCS Z — the complete typed-ROTATE transform, dispatch included (REQ-329 increment 2,
/// REQ-332 increment 2). The caller owns the undo snapshot.
///
/// Declared here, and extracted out of `FinishRotateCommand`, for REQ-060's reason: typed ROTATE
/// does not call one function, it CHOOSES between two — `ApplyRotationToSelection` when the UCS Z is
/// world-Z-parallel and `RotateSelectionInPlaceAboutAxis` when it is tilted. A gizmo that called
/// only the inner function would agree with the typed command in plan view and diverge from it under
/// a tilted UCS, which is the half-agreement a tolerance-based acceptance would never catch.
void ApplyRotationAboutUcsZ(AppCommandState& st, double bx, double by, double bz, double rad,
                            std::vector<std::string>& log);

/// Rotate the whole selection in place by \p rad about the line through \p axisPoint along
/// \p axisUnit — the rotate gizmo's X and Y rings (D-2026-09-28-b). The same in-place 3D turn typed
/// ROTATE uses under a tilted UCS, with the same refusals. The caller owns the undo snapshot.
void ApplyRotationAboutAxis(AppCommandState& st, const ray3d::Vec3& axisPoint,
                            const ray3d::Vec3& axisUnit, double rad, std::vector<std::string>& log);

/// Scale the whole selection uniformly by \p sc about (\p bx, \p by, \p bz) — the complete typed
/// SCALE transform (REQ-329 increment 3, REQ-332 increment 2). The caller owns the undo snapshot.
///
/// Extracted for the same reason as \ref ApplyRotationAboutUcsZ: typed SCALE calls
/// `ApplyScaleToSelection` and THEN, only under a tilted UCS, `ScaleSelectionZAboutBase`. Both steps
/// are the command, so both belong behind one name the gizmo can call too.
void ApplyUniformScaleAboutBase(AppCommandState& st, float bx, float by, float bz, float sc,
                                std::vector<std::string>& log);
