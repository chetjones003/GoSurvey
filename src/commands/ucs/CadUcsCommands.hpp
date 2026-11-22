// --- UCS and PLAN (REQ-154, GitHub #126) --------------------------------------------------------

/// Make \p next the active UCS: pushes the outgoing frame onto the Previous stack, honours
/// UCSFOLLOW, and reports the change.
///
/// **Every** path that changes the UCS goes through here — that is what makes "Previous" and
/// UCSFOLLOW work for options added later without each one remembering to maintain them.
/// \p pushPrevious is false only for `UCS Previous` itself, which must not push the state it is
/// popping (AutoCAD's rule: restoring a previous UCS does not add a history entry).
void SetActiveUcs(AppCommandState& st, const ucs::Ucs& next, std::vector<std::string>& log,
                  bool pushPrevious = true);

/// UCS: the top-level prompt.
void StartUcsCommand(AppCommandState& st, std::vector<std::string>& log);
/// PLAN: the view-orientation prompt.
void StartPlanCommand(AppCommandState& st, std::vector<std::string>& log);

/// Feed one typed line to whichever of UCS / PLAN is active. Returns true when the line was
/// consumed. Shared by the command line and the at-cursor dynamic input (REQ-024), so both accept
/// exactly the same keywords.
bool ProcessUcsCommandLine(AppCommandState& st, const std::string& line, std::vector<std::string>& log);
/// REQ-398 — one typed line while GRADING is running. Reached from the blank-line branch too,
/// because a bare Enter is GRADING's action rather than a no-op.
void ProcessGradingCommandLine(AppCommandState& st, const std::string& line,
                               std::vector<std::string>& log);
bool ProcessPlanCommandLine(AppCommandState& st, const std::string& line, std::vector<std::string>& log);

/// Feed a viewport pick (world coordinates) to the UCS command. Returns true when consumed.
///
/// \p pickRay, when non-null and valid, is the camera ray behind this click in an orbited/non-plan
/// model view (issue #156). `UCS Object` uses it to align the frame to a planar face of a B-rep
/// solid under the cursor — the true-3D sub-object pick, which a flattened work-plane XY cannot do.
/// Null in plan view and paper space, where the 2D entity pick is all that applies.
bool ProcessUcsViewportPick(AppCommandState& st, const ray3d::Vec3& worldPoint, std::vector<std::string>& log,
                            const ray3d::Ray* pickRay = nullptr);

/// Orient the view to a PLAN view of \p frame without touching the active UCS.
void ApplyPlanViewOf(AppCommandState& st, const ucs::Ucs& frame, std::vector<std::string>& log);

/// Find a saved UCS by name, case-insensitively. Returns nullptr when there is none.
const NamedUcs* FindNamedUcs(const AppCommandState& st, const std::string& name);

/// Save / restore / delete a named UCS (REQ-154).
///
/// Only \ref SaveNamedUcs is reachable from the `UCS` command: `UCS Named` asks for a name and
/// nothing else. Restore and delete live in the View Manager, where the saved frames are listed and
/// a user can see what they are choosing between - having to recall a name you cannot see was the
/// weakest part of the old Save/Restore/Delete sub-prompt. The dialog calls these functions, so the
/// two halves still share one implementation and cannot drift.
///
/// All three refuse the reserved name "World". Restore and delete return false, and say so in
/// \p log, when no such name is saved.
void SaveNamedUcs(AppCommandState& st, const std::string& rawName, std::vector<std::string>& log);
bool RestoreNamedUcs(AppCommandState& st, const std::string& rawName, std::vector<std::string>& log);
bool DeleteNamedUcs(AppCommandState& st, const std::string& rawName, std::vector<std::string>& log);

/// List the saved UCS definitions to \p log - what `UCS ?` prints.
void ListNamedUcs(const AppCommandState& st, std::vector<std::string>& log);

/// A one-line description of a frame, in WORLD coordinates - the only frame a UCS can sensibly be
/// stated in. Shared by the command log and the View Manager so the two cannot describe a saved
/// frame differently.
std::string DescribeUcs(const ucs::Ucs& u);

/// Named views (REQ-106). Same shape as the UCS Named helpers above, deliberately.
const NamedView* FindNamedView(const AppCommandState& st, const std::string& name);
/// The saved view the camera is currently in, or nullptr — derived, never remembered.
const NamedView* CurrentNamedView(const AppCommandState& st);
NamedView CaptureCurrentView(const AppCommandState& st, const std::string& name);
void RestoreNamedView(AppCommandState& st, const NamedView& v, std::vector<std::string>& log);
void ListNamedViews(const AppCommandState& st, std::vector<std::string>& log);
bool ProcessViewCommandLine(AppCommandState& st, const std::string& rest, std::vector<std::string>& log);

/// Apply ORTHO in the active UCS's axes rather than the world's (REQ-047 under REQ-154).
///
/// \p anchor and \p target are world 3D points; the constrained target is written back. Under the
/// WCS this reduces exactly to the world-axis constraint the 2D path always applied.
ray3d::Vec3 ConstrainToUcsOrtho(const ucs::Ucs& frame, const ray3d::Vec3& anchor, const ray3d::Vec3& target);

/// \ref ConstrainToUcsOrtho's screen-aware counterpart (issue #371 second follow-up). Under an
/// orbited (non-plan) camera, comparing raw UCS-delta magnitude no longer reliably says which axis
/// the cursor is "farther along" — an oblique view mixes both in-plane axes into any one screen
/// direction. This instead projects both candidate locked points and the raw cursor hit through
/// \p cam and keeps whichever candidate is actually closer to the cursor on screen, matching
/// AutoCAD's observed behavior (a Front-UCS ORTHO drag stays screen-vertical from any orbit).
/// Reduces to \ref ConstrainToUcsOrtho's decision in plan view, where the two always agree.
ray3d::Vec3 ConstrainToUcsOrthoOnScreen(const ucs::Ucs& frame, const ray3d::Vec3& anchor, const ray3d::Vec3& target,
                                        const Camera& cam, float viewportWidthPx, float viewportHeightPx);
