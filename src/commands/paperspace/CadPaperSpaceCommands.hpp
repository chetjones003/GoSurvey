// --- Paper space (REQ-025) ---
/// Append a new paper layout with a unique default name; returns its index.
int  AddPaperLayout(AppCommandState& cmd);
/// Delete the layout at \p idx, fixing up the active space.
void DeletePaperLayout(AppCommandState& cmd, int idx);
/// Set the active space: kModelSpaceIndex for model, else a paper-layout index (clamped).
void SetActiveSpace(AppCommandState& cmd, int spaceIndex);
/// Toggle between model space and the last/first paper layout (creating one if none exist).
void ToggleModelPaperSpace(AppCommandState& cmd);
/// Append a default viewport (centered on the model) to layout \p layoutIdx; returns its index (REQ-027).
int  AddViewport(AppCommandState& cmd, int layoutIdx);
/// Append a viewport with an explicit paper-inch rect (corners may be unordered); returns its index.
int  AddViewportRect(AppCommandState& cmd, int layoutIdx, float x0In, float y0In, float x1In, float y1In);
/// Delete viewport \p vpIdx from layout \p layoutIdx, fixing up the selection.
void DeleteViewport(AppCommandState& cmd, int layoutIdx, int vpIdx);
/// Start the rectangular-viewport command (REQ-033); requires an active paper layout.
void StartPaperRectViewportCommand(AppCommandState& cmd, std::vector<std::string>& log);

// --- Paper-space viewport selection / edit (REQ-035) ---
bool IsViewportSelected(const AppCommandState& cmd, int vi);
/// Select viewport \p vi in the active layout; \p additive toggles it within the current selection.
void SelectViewport(AppCommandState& cmd, int vi, bool additive);
void ClearViewportSelection(AppCommandState& cmd);
/// Delete all selected viewports in the active layout.
void DeleteSelectedViewports(AppCommandState& cmd, std::vector<std::string>& log);
/// Move (or, if \p copy, duplicate) the selected viewports by a paper-inch delta.
void TranslateSelectedViewports(AppCommandState& cmd, float dxIn, float dyIn, bool copy,
                                std::vector<std::string>& log);
/// Begin a two-click MOVE/COPY of the selected viewports (paper-inch base → destination).
void StartPaperMoveCopyViewports(AppCommandState& cmd, bool copy, std::vector<std::string>& log);
// REQ-307 (GitHub #106): Enter acting on the paper-space MOVE/COPY/DELETE selection step. See the
// definitions in CadCommands.cpp for why these are free functions rather than inline logic.
void ProcessPaperMoveWaitingSelectionEnter(AppCommandState& st, std::vector<std::string>& log);
void ProcessPaperDeleteWaitingSelectionEnter(AppCommandState& st, std::vector<std::string>& log);

// --- Per-viewport layer freeze (REQ-028) ---
/// Toggle the frozen state of a layer in a viewport.
void ToggleFrozenLayerInViewport(Viewport& vp, const std::string& layerName);
/// Check if a layer is frozen in a viewport.
bool IsLayerFrozenInViewport(const Viewport& vp, const std::string& layerName);

// --- Per-viewport layer overrides UI/commands (REQ-046) ---
/// The "current viewport" the Layer Manager VP columns and VPFREEZE/VPTHAW act on: the floating
/// viewport if inside one (REQ-036), else the single selected viewport in the active paper layout,
/// else nullptr (no current viewport → VP controls disabled).
Viewport* CurrentViewport(AppCommandState& st);
/// The visual style the user is working in: the current viewport's own (ef CurrentViewport), else model space's.
VisualStyle EffectiveVisualStyle(const AppCommandState& st);
/// Begin VPFREEZE / VPTHAW: pick entities in the current (floating) viewport to freeze/thaw their layers.
void StartVpFreezeCommand(AppCommandState& st, std::vector<std::string>& log);
void StartVpThawCommand(AppCommandState& st, std::vector<std::string>& log);

// --- Floating model space (REQ-036) ---
bool InFloatingModelSpace(const AppCommandState& cmd);

/// REQ-036: grab the nearest grip of a selected entity within \p tolWorld of (lx,ly) in LOCAL model coords;
/// arms the grip drag and stores originals (for the floating viewport, where the screen-space grab cannot be
/// used through the viewport transform). Returns true if a grip was grabbed.
bool TryBeginEntityGripAtLocal(AppCommandState& cmd, float lx, float ly, float tolWorld);

/// REQ-037 / ADR-009: the active layout's paper-space geometry store a draw/edit command writes to,
/// or nullptr when the command targets model space (model space active, or floating model space).
PaperLayout* ActivePaperGeometryTarget(AppCommandState& st);
const PaperLayout* ActivePaperGeometryTarget(const AppCommandState& st);

// Native paper-space geometry selection + edit (REQ-037). Indices are into the ACTIVE layout's stores.
void ClearPaperEntitySelection(AppCommandState& st);
/// Moves everything a model-space reader put into \p scratch (read with origin 0 and 1 unit per plotted
/// inch, so every number is already in paper inches) into \p L's own stores (issue #768, REQ-113). Shared
/// by the DWG and DXF readers. Paper POINT and SPLINE come through the same scratch (REQ-456).
void AppendScratchToPaperLayout(PaperLayout& L, const AppCommandState& scratch);

/// Topmost paper entity (text over line) within \p tolIn of (x,y) in paper inches; false if none.
bool PickPaperEntityAt(const PaperLayout& L, float x, float y, float tolIn, PaperEntityRef* out,
                       const std::vector<CadBlockDefinition>* blockDefs = nullptr);
void TogglePaperEntitySelection(AppCommandState& st, PaperEntityRef ref, bool additive);
void DeleteSelectedPaperEntities(AppCommandState& st, std::vector<std::string>& log);
void TranslateSelectedPaperEntities(AppCommandState& st, float dxIn, float dyIn, bool copy,
                                    std::vector<std::string>& log, bool pushUndo = true);
/// #764 - paper-space SCALE: uniform scale of the selected paper entities about a point (paper inches).
void ScaleSelectedPaperEntities(AppCommandState& st, float baseX, float baseY, float factor,
                                std::vector<std::string>& log);
/// Destination step of paper MOVE/COPY (click or typed): applies the (dx, dy) displacement in paper
/// inches to the selected paper entities and viewports, then ends the gesture.
void FinishPaperMoveCopy(AppCommandState& st, float dxIn, float dyIn, std::vector<std::string>& log);
/// Second-point step of paper MIRROR (click or typed). Refuses a zero-length mirror line.
void FinishPaperMirror(AppCommandState& st, float x2In, float y2In, std::vector<std::string>& log);
void RotateSelectedPaperEntities(AppCommandState& st, float baseX, float baseY, float angRad,
                                 std::vector<std::string>& log, bool pushUndo = true);
/// REQ-103 MIRROR, pure-paper-space path. Always duplicates and keeps the source (see the
/// definition's comment for why there is no erase-source toggle here).
void MirrorSelectedPaperEntities(AppCommandState& st, float x0In, float y0In, float x1In, float y1In,
                                 std::vector<std::string>& log);
/// REQ-103 LENGTHEN, pure-paper-space path. Single pick, single apply (no erase/duplicate step) —
/// see the definition's comment for the mode/value simplification.
bool ApplyLengthenToPaperEntity(AppCommandState& st, const PaperEntityRef& ref, float pickXIn, float pickYIn,
                                std::vector<std::string>& log);
/// REQ-103 EXTEND, pure-paper-space path — unlike LENGTHEN's, needs no typed value, so it is built
/// (not simplified away) as a real two-phase pick flow; see `paperExtendPhase`'s comment.
bool ApplyExtendToPaperEntity(AppCommandState& st, const PaperEntityRef& ref, float pickXIn, float pickYIn,
                              std::vector<std::string>& log);
/// REQ-103 BREAK, pure-paper-space path — a pure two-phase click flow like paper EXTEND, needing no
/// typed value; see `paperBreakPhase`'s comment. `p1` is the already-resolved first break point
/// (from the entity-selecting pick); this call resolves the second point at (pickXIn,pickYIn) itself.
bool ApplyBreakToPaperEntity(AppCommandState& st, const PaperEntityRef& ref, const BreakPoint& p1,
                             float pickXIn, float pickYIn, std::vector<std::string>& log);
/// REQ-103 STRETCH, pure-paper-space path. Translates every entity in \c st.selectedPaperEntities
/// by (dxIn,dyIn); if \c st.paperSelBoxLastValid, only each entity's definition points that fall
/// inside the last-captured box move (true partial stretch for Line/Polyline/Arc — Arc via
/// \c RecomputeArcFromEndpoints, shared with the model-space path), otherwise every entity
/// translates as a whole (degraded MOVE-equivalent, matching a non-crossing pickfirst set).
void ApplyStretchToPaperSelection(AppCommandState& st, float dxIn, float dyIn,
                                  std::vector<std::string>& log);
/// REQ-103 FILLET, pure-paper-space path (step 6a) — full parity with model space, a two-phase
/// click flow like paper EXTEND/BREAK needing no typed value at the pick itself (radius/trim mode
/// come from the model-space command line, the same way paper LENGTHEN reuses model-set values).
/// \p first is the entity+segment latched by the first click (\c st.paperFilletFirstEntity et al.);
/// \p second is the entity+segment resolved at the second click. Same Case A (same polyline,
/// adjacent segments) / Case B (two different curves, or a polyline's own end segment) split as the
/// model-space path, including the parallel-lines-semicircle special case.
bool ApplyFilletToPaperEntities(AppCommandState& st, const PaperEntityRef& first, int firstPolySeg,
                                float firstPickX, float firstPickY, const PaperEntityRef& second,
                                int secondPolySeg, float pickX, float pickY, std::vector<std::string>& log);
/// Paper-space equivalent of the model-space `FilletEligibility` (CadCommands.cpp) — is `ref`
/// (picked at pickX,pickY) an eligible FILLET curve, and if it's a Polyline, which edge (0-based)?
/// Declared here (not just in the .cpp) so CadUi.cpp's paper click block can resolve BOTH picks the
/// same way it already calls `PickPaperEntityAt`.
bool PaperFilletEligibility(const PaperLayout& L, const PaperEntityRef& ref, float pickX, float pickY,
                            int* outPolySeg, std::vector<std::string>& log);
/// REQ-103 CHAMFER, pure-paper-space path (step 6b) — same shape as `ApplyFilletToPaperEntities`.
bool ApplyChamferToPaperEntities(AppCommandState& st, const PaperEntityRef& first, int firstPolySeg,
                                 float firstPickX, float firstPickY, const PaperEntityRef& second,
                                 int secondPolySeg, float pickX, float pickY, std::vector<std::string>& log);
/// Paper-space equivalent of the model-space `ChamferEligibility` (CadCommands.cpp) — same shape as
/// `PaperFilletEligibility`, minus the Arc case (CHAMFER has none).
bool PaperChamferEligibility(const PaperLayout& L, const PaperEntityRef& ref, float pickX, float pickY,
                             int* outPolySeg, std::vector<std::string>& log);
/// Projects (px,py) onto model-space entity \p e, filling \p out — "the closest point ON this
/// entity", which \c PickClosestCadEntity does not answer (it names the entity and a distance).
/// BREAK's second pick commits this point, and BREAK's live preview must resolve the cursor
/// through the very same call or it previews a span other than the one about to be removed
/// (TASK-101). Declared here for the same reason \c ClosestPointOnPaperEntity below is.
bool ClosestPointOnEntity(const AppCommandState& st, const SelectedEntity& e, float px, float py,
                          BreakPoint* out);
/// Paper-space equivalent of \c ClosestPointOnEntity (CadCommands.cpp) — projects (px,py) onto
/// paper entity \p ref, filling \p out. Declared here (not just in the .cpp) so CadUi.cpp's paper
/// click block can resolve BREAK's first point the same way it already calls \c PickPaperEntityAt.
bool ClosestPointOnPaperEntity(const PaperLayout& L, const PaperEntityRef& ref, float px, float py,
                               BreakPoint* out);
/// Enter floating model space for viewport \p vpIdx of layout \p layoutIdx (edit the model through it).
void EnterFloatingModelSpace(AppCommandState& cmd, int layoutIdx, int vpIdx, std::vector<std::string>& log);
/// Save the floating view back to the viewport and return to paper space.
void ExitFloatingModelSpace(AppCommandState& cmd, std::vector<std::string>& log);

// --- Page setups (named, drawing-wide) ---
/// Ensure a built-in "Standard" page setup exists in cmd.drawing.savedPageSetups.
void EnsureStandardPageSetup(AppCommandState& cmd);
/// Copy a PageSetup's paper-size/orientation/plot fields into a layout (Set Current).
void ApplyPageSetupToLayout(PaperLayout& layout, const PageSetup& ps);
/// Snapshot a layout's current paper/plot fields into a PageSetup (for New "start with *layout*").
PageSetup PageSetupFromLayout(const PaperLayout& layout, const std::string& name);
/// Reorder/copy a layout to before \p beforeIdx (== size → move to end); copy clones it. Move-or-Copy.
void MoveOrCopyLayout(AppCommandState& cmd, int layoutIdx, int beforeIdx, bool makeCopy, std::vector<std::string>& log);
/// Duplicate a layout at the end, leaving the previously active layout selected. Returns the copy index or -1.
int CopyPaperLayout(AppCommandState& cmd, int layoutIdx, const std::string& requestedName,
                    std::vector<std::string>& log);

// --- LAYOUT / VPSCALE / PAGESETUP (REQ-025/026/027, issue #751/#753 V3) ----------------------------
/// One-shot text dispatch for LAYOUT's NEW/RENAME/DELETE subverbs — the typed equivalent of the
/// layout tab bar's add/rename/delete (REQ-025). \p args is everything typed after "LAYOUT".
void HandleLayoutCommand(const std::string& args, AppCommandState& st, std::vector<std::string>& log);
/// Sets `CurrentViewport(st)`'s scale from a typed "1:N" ratio or a bare model-units-per-inch
/// number; a blank \p args reports the current viewport's scale instead.
void HandleVpscaleCommand(const std::string& args, AppCommandState& st, std::vector<std::string>& log);
/// Apply one drawing-owned named annotation scale to every selected paper viewport in the active
/// layout. Unlike VPSCALE, this does not target a floating viewport or parse a free-form ratio.
void HandleVpMatchScaleCommand(const std::string& args, AppCommandState& st, std::vector<std::string>& log);
/// Opens the Page Setup Manager dialog for the active layout (refuses in model space).
void StartPageSetupCommand(AppCommandState& st, std::vector<std::string>& log);
