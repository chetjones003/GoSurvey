// --- PIPERUN (issue #486 increment B2 / REQ-345) -------------------------------------------------
/// Open the command: prompt for a nominal size (+ optional pressure class).
void StartPipeRunCommand(AppCommandState& st, std::vector<std::string>& log);

/// Finish an open PIPERUN because something else is about to take the command (D-2026-09-24-d).
///
/// Commits the route exactly as END does when it has at least two points, and otherwise drops the
/// draft; returns false when PIPERUN was not the active command, so callers can use it as a test.
/// Exists for the Pipe Fittings palette: picking a part starts INSERT, and before the route was
/// materialised that silently discarded the pipe the user had just drawn.
bool CadPipeRunFinishForHandoff(AppCommandState& st, std::vector<std::string>& log);
/// REQ-355 / D-2026-09-28-k: the Modeling ribbon's PIPERUN. Starts at the remembered size (the
/// ribbon dropdown) with that size's standard wall and goes straight to the start point — neither
/// question is asked. Falls back to the typed prompts when the size has no standard wall.
void StartPipeRunAtCurrentSize(AppCommandState& st, std::vector<std::string>& log);
/// REQ-355: the Modeling ribbon's size dropdown. Choosing a size is typing it alone at PIPERUN's size
/// prompt — the size is set and the pressure class cleared — so the two are one setting.
void ChoosePipeRunNominalSize(AppCommandState& st, const std::string& size);
/// The prompt line, computed rather than literal: it echoes the phase and the size/class in force.
[[nodiscard]] std::string CadPipeRunPromptText(const AppCommandState& st);
/// Handle one typed line: the size/class line, a coordinate, or one of `U UNDO END`. \neturn false
/// if not consumed.
bool HandlePipeRunTextInput(const std::string& line, AppCommandState& st, std::vector<std::string>& log);
/// Handle a viewport click: the run's start point, or a further vertex.
void SubmitPipeRunViewportPick(AppCommandState& st, float wx, float wy, std::vector<std::string>& log);
/// Reset the draft path, keeping the remembered nominal size and pressure class.
void CancelPipeRunCommand(AppCommandState& st);

// --- PIPEFIT (issue #486 increment B7 / REQ-345) -------------------------------------------------
/// Open the command: requires exactly one `CadPipeRun` selected and a recognized \p partTypeTok
/// (`CadPipePartType`'s own tags — valve, flange, reducer, coupling, ...); refuses immediately
/// (never entering the command) otherwise, mirroring `PIPECATALOG`'s own inline-argument refusals.
void StartPipeFitCommand(AppCommandState& st, const std::string& partTypeTok, std::vector<std::string>& log);
/// Flattens a `brep::Tessellation` into the flat per-vertex GL arrays the renderer uploads (REQ-313 /
/// ADR-045): triangle vertices, one normal per vertex, and the owning face id per TRIANGLE. Shared by
/// the viewport's solid display cache and REQ-350's part thumbnails, so the two cannot disagree about
/// what a solid's mesh is.
void ExpandTessellation(const brep::Tessellation& t, std::vector<float>* verts, std::vector<float>* normals,
                        std::vector<int>* faceIds);

/// REQ-350 (f) — the pipe run under \p pick, or none. "Under" means within the pipe's own outer
/// radius (with a little slack) of its centreline; when two runs overlap the pick, the nearer
/// centreline wins. This is what decides whether a part armed from the Pipe Fittings palette splices
/// into a run or is placed as a free INSERT.
[[nodiscard]] bool CadPipeRunUnderPick(const AppCommandState& st, const ray3d::Vec3& pick, int* outRunIdx);

/// REQ-350 (f) — splice the NAMED library part into `st.drawing.cadPipeRuns[runIdx]` at the point nearest
/// \p pick: exactly what `PIPEFIT <part type>` does after its catalog lookup, including the
/// engagement cutback, the run splitting into two pieces, and one undo step. Named rather than
/// looked up by type because the palette has already chosen a specific part, and the catalog lookup
/// refuses when several parts match a type.
bool CadPipeFitNamedAtPick(AppCommandState& st, int runIdx, const std::string& blockName,
                           const ray3d::Vec3& pick, std::vector<std::string>& log);
/// Handle a viewport click: the station point to splice the fitting in at. Always ends the command,
/// success or refusal — there is nothing left to pick after one point.
void SubmitPipeFitViewportPick(AppCommandState& st, float wx, float wy, std::vector<std::string>& log);
/// Handle one typed line as a station point (`X,Y,Z`). \neturn false if not consumed.
bool HandlePipeFitTextInput(const std::string& line, AppCommandState& st, std::vector<std::string>& log);

// --- PIPESPLIT / PIPEJOIN / PIPEPROP (issue #486 increment B8 / REQ-345) --------------------------
/// Open PIPESPLIT: requires exactly one `CadPipeRun` selected; refuses immediately otherwise.
void StartPipeSplitCommand(AppCommandState& st, std::vector<std::string>& log);
/// Handle a viewport click: the station point to split at. Always ends the command.
void SubmitPipeSplitViewportPick(AppCommandState& st, float wx, float wy, std::vector<std::string>& log);
/// Handle one typed line as a station point (`X,Y,Z`). \neturn false if not consumed.
bool HandlePipeSplitTextInput(const std::string& line, AppCommandState& st, std::vector<std::string>& log);
/// Merge the two selected `CadPipeRun`s into one — they must share the same nominal size and
/// pressure class and have exactly one coincident endpoint between them. One-shot, no `Kind` state
/// machine (nothing left to pick once the selection is already made), the same shape `PIPESYS`
/// itself uses.
void HandlePipeJoinCommand(AppCommandState& st, std::vector<std::string>& log);
/// Change nominal size (+ optional pressure class) on every selected `CadPipeRun`, refusing (not
/// applying anyway) any run whose resulting solid would fail to build — REQ-201's "refuse rather
/// than guess," applied per run so one bad candidate does not block the rest of the selection.
void HandlePipePropCommand(const std::string& args, AppCommandState& st, std::vector<std::string>& log);

// --- PIPESYS (issue #486 increment B3 / REQ-345) --------------------------------------------------
/// One-shot text dispatch for every PIPESYS subverb (NEW/ADD/REMOVE/RENAME/DELETE/LIST — a bare or
/// unrecognized \p args reports the current networks). \p args is everything typed after the
/// command name itself. ADD/REMOVE act on `AppCommandState::selection`'s pipe runs.
void HandlePipingSystemCommand(const std::string& args, AppCommandState& st, std::vector<std::string>& log);
