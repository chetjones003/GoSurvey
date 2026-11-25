/// RECT (REQ-053): two opposite corners create an axis-aligned rectangle.
void StartRectCommand(AppCommandState& st, std::vector<std::string>& log);
/// Store the rectangle spanned by the two corners as a 4-vertex closed polyline, ending the command.
/// Degenerate corners (zero width or height) are rejected and the command restarts at the first corner.
void CommitRectangle(AppCommandState& st, float x1, float y1, float x2, float y2, std::vector<std::string>& log);
void StartTextCommand(AppCommandState& st, std::vector<std::string>& log);
void StartMtextCommand(AppCommandState& st, std::vector<std::string>& log);
void OpenMtextRichEditorForPlacement(AppCommandState& st, std::vector<std::string>* log);
void OpenMtextRichEditorForAnnotation(AppCommandState& st, int annIndex, std::vector<std::string>* log);
void OpenPaperTextEditor(AppCommandState& st, int layoutIndex, int textIndex, std::vector<std::string>* log);
void CommitMtextRichEditor(AppCommandState& st, std::vector<std::string>& log);
void CancelMtextRichEditor(AppCommandState& st, std::vector<std::string>* log);
void StartDimAlignedCommand(AppCommandState& st, std::vector<std::string>& log);
void StartDimLinearCommand(AppCommandState& st, std::vector<std::string>& log);
void StartDimAngularCommand(AppCommandState& st, std::vector<std::string>& log);
void StartDimStyleCommand(AppCommandState& st, std::vector<std::string>& log);
void StartMleaderStyleCommand(AppCommandState& st, std::vector<std::string>& log);
void StartIdPointCommand(AppCommandState& st, std::vector<std::string>& log);

/// True if (x,y) is inside the filled region: inside its outer loop (0) and outside every hole loop (REQ-042).
bool CadFilledRegionContainsPoint(const CadFilledRegion& fr, double x, double y);
/// HATCH command (REQ-043): begin picking an internal point.
void StartHatchCommand(AppCommandState& st, std::vector<std::string>& log);
/// Trace the smallest closed boundary enclosing (wx,wy) from existing model geometry (lines/polylines/arcs/
/// circles/ellipses). Returns the ordered loop (flat local x,y) or false when no closed region contains it.
bool CadHatchTraceAt(const AppCommandState& st, double wx, double wy, std::vector<float>* outLoop);
/// Create a solid filled region from \p loop using the live HATCH appearance; selects it. Caller-agnostic of
/// the undo snapshot? No — this pushes its own snapshot. Returns false if the loop is degenerate.
bool CadHatchCommitLoop(AppCommandState& st, const std::vector<float>& loop, std::vector<std::string>& log);
/// Index of the smallest-area filled region containing (wx,wy), or -1. Lowest pick priority (fills sit under
/// linework) — the click handler calls this only after geometry/annotation picks miss (REQ-042).
/// \param ray As \ref PickCadAnnotationAt: when non-null, each region is tested where the ray meets
///        its own plane (the elevation of its first vertex) — issue #564 §2.
int PickFilledRegionAt(const AppCommandState& st, double wx, double wy, const ray3d::Ray* ray = nullptr);

/// Parse a .pat file's text into ef AppCommandState::hatchPatternLibrary. A changed library invalidates
/// every cached pattern; the same text again changes nothing.
void SetHatchPatternLibrary(AppCommandState& st, const std::string& patSource);

/// Bring ef AppCommandState::hatchDisplayGeometry up to date with the drawing's filled regions. Called
/// once a frame with the viewport's render revision; with the same revision it returns at once. Otherwise
/// only a region whose boundary or pattern settings changed has its pattern re-clipped, so editing one
/// hatch never rebuilds the others.
void RefreshHatchDisplayGeometry(AppCommandState& st, std::uint64_t renderRevision);
