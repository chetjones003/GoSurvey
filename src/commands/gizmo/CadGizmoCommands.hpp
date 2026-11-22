// --- The translate gizmo (REQ-060, GitHub issue #148 Phase 5 slice 4b) --------------------------
//
// One rule decides the shape of everything below: **the gizmo commits through
// `ApplyTranslationToSelection`, the same function typed MOVE calls.** REQ-060 requires that "a
// gizmo drag and the equivalent typed command produce coordinates agreeing within REQ-101"; going
// through the one function makes that hold by construction rather than by two implementations
// happening to match, which is the failure mode a tolerance-based acceptance invites.
//
// The gizmo aligns with the ACTIVE UCS, not with world axes. Everything else in this program that
// takes a direction from the user - the grid, ORTHO, coordinate entry - follows the UCS (REQ-154),
// and in the World UCS the two are identical, so the default view is unchanged.


/// What the gizmo is currently acting on, derived from the selection and never stored.
///
/// The two selections are mutually exclusive already (D-2026-09-04-a), so a stored mode would be a
/// third thing that can disagree with them. `None` is the answer whenever no gizmo may be drawn —
/// which is REQ-060's third acceptance bullet and, for a face, the honest answer for a selection the
/// kernel has no operation for.
enum class CadGizmoMode : std::uint8_t {
  None,
  /// The entity selection: three handles along the active UCS, committing through
  /// `ApplyTranslationToSelection`.
  Entity,
  /// Exactly one solid FACE: ONE handle along that face's own normal, committing through
  /// `CadApplyPushPull` — so a drag and the typed `PRESSPULL` agree by construction (issue #148
  /// acceptance 4).
  ///
  /// One handle and not three, because `brep::PushPullFace` takes a distance along the face normal
  /// and nothing else. A handle along UCS X on a face whose normal is Z would advertise a move the
  /// kernel cannot make; drawing it and then refusing the drag is worse than not drawing it.
  SubObjectFace,
  /// Exactly one solid EDGE: TWO handles, along the two adjacent faces' own outward normals,
  /// committing through `CadApplyMoveEdge` (REQ-333).
  ///
  /// Two because two planes meet there and each has one degree of freedom that keeps it planar, and
  /// together they span exactly the plane perpendicular to the edge. **There is deliberately no
  /// third**: the along-the-edge direction is not a motion at all — an edge slid along its own line
  /// is the same edge (REQ-333 item 4) — so a handle there would advertise a drag that does nothing.
  SubObjectEdge,
  /// Exactly one solid VERTEX: THREE handles, along the active UCS, committing through
  /// `CadApplyMoveVertex` (REQ-333).
  ///
  /// Three because three planes meet there, which is three degrees of freedom — every direction is
  /// reachable, so unlike the face and the edge there is nothing to leave out.
  SubObjectVertex,
};

/// Which subject the gizmo has right now. \c None when no gizmo may be drawn.
///
/// Returns \c SubObjectFace only under \c CadGizmoOp::Translate: no kernel operation rotates or
/// scales a single face, so under Rotate or Scale a face selection gets no gizmo at all — the same
/// answer an edge or a vertex already gets, for the same reason.
[[nodiscard]] CadGizmoMode CadGizmoModeFor(const AppCommandState& st);

/// Maximum gizmo axes. Three: the UCS X, Y and Z. Named so the loops below say why they are 3.
inline constexpr int kGizmoAxisCount = 3;

/// How many handles the gizmo actually has right now: 3 in entity mode, 1 in face mode, 0 for none.
[[nodiscard]] int CadGizmoAxisCountFor(const AppCommandState& st);
/// Handle length, in screen pixels, held constant at every zoom (REQ-060's "as displayed").
inline constexpr float kGizmoHandleLenPx = 70.f;
/// Grab aperture around a handle, in screen pixels.
inline constexpr float kGizmoHandleGrabPx = 7.f;
/// A rotate ring whose normal is within this cosine of perpendicular to the view ray is seen
/// (nearly) edge-on — a line on screen, round which a drag names no angle — so it is not pickable.
/// About 5 degrees (D-2026-09-28-b).
inline constexpr double kGizmoRingEdgeOnCos = 0.09;
/// The move gizmo's PLANE handles (D-2026-09-28-c) are handle numbers 3, 4, 5 — after the three
/// axes — for the UCS XY, YZ and ZX planes. Each is a square from the anchor out to this fraction of
/// the handle length along its two axes.
inline constexpr int kGizmoPlaneHandleFirst = 3;
inline constexpr double kGizmoPlaneHandleFrac = 0.35;
/// The two UCS axes (0 = X, 1 = Y, 2 = Z) spanning plane handle \p plane (0 = XY, 1 = YZ, 2 = ZX).
inline void CadGizmoPlaneAxes(int plane, int* a, int* b) {
  *a = plane;
  *b = (plane + 1) % 3;
}
/// How many plane handles the gizmo has: 3 for an entity selection under Translate, else 0.
[[nodiscard]] int CadGizmoPlaneHandleCountFor(const AppCommandState& st);

/// Where the gizmo hangs, in WCS. False when there is nothing for it to hang off.
///
/// **Entity mode:** the centre of the selection's bounding box. Deliberately not
/// \ref ComputeSelectionCentroidWorld: that answers ROTATE's different question (a pivot, in plan,
/// over ROTATE's own type set), and changing it to serve this one would change where ROTATE and
/// ARRAY turn things about.
///
/// **What this anchor has to be depends on the operation, and the original note here was written
/// when there was only one** (TASK-232). For a TRANSLATE it is cosmetic: a drag distance is the
/// change in the axis parameter between the grab and the drop, so the anchor appears in both terms
/// and cancels, and it decides only where the handles are DRAWN — which is why conservative
/// per-type bounds were good enough. For a ROTATE or a SCALE it is the **pivot** and the **base**:
/// it does not cancel, it decides the answer.
///
/// That does not make conservative bounds wrong here, but it changes what is being relied on. What
/// REQ-060 requires is agreement with *the equivalent typed command*, and the equivalent command is
/// ROTATE / SCALE **about this same anchor** — so the anchor must be DETERMINISTIC, not accurate.
/// Widening the bounds later would move where a gizmo rotation pivots, which a reader of the
/// original note would not have expected.
///
/// **Face mode:** the face's centroid (\ref CadSubObjectFaceGrip), where the grip this replaced
/// already put its handle. A corner would read as a vertex grip, which is a different edit.
[[nodiscard]] bool CadGizmoAnchorWorld(const AppCommandState& st, ray3d::Vec3* out);

/// Unit direction of gizmo axis \p axis in WCS: the active UCS's X / Y / Z in entity mode, and the
/// selected face's outward normal (axis 0, the only one) in face mode.
[[nodiscard]] ray3d::Vec3 CadGizmoAxisWorld(const AppCommandState& st, int axis);

/// Handle length in drawing units for the current view - \ref kGizmoHandleLenPx converted through
/// the same ortho half-height \ref CadViewCamera builds its projection from, so the handle is the
/// stated pixel length on screen and a transcript with no window still gets a definite answer.
[[nodiscard]] float CadGizmoHandleLenWorld(const AppCommandState& st);

/// True when a gizmo should be drawn at all: \ref CadGizmoModeFor is not \c None and an anchor
/// resolves. REQ-060's third acceptance bullet ("no gizmo when the selection is empty") is this.
[[nodiscard]] bool CadGizmoVisible(const AppCommandState& st);

/// True for \c Kind::Move3d / \c Rotate3d / \c Scale3d (GitHub issue #564 section 3).
[[nodiscard]] bool IsGizmoCommandKind(AppCommandState::Kind k);
/// Whether the gizmo has been ASKED for: the persistent `GIZMO` setting is on, or a 3D gizmo
/// command is at its handles step. A selection alone never summons it (D-2026-09-28-a).
[[nodiscard]] bool CadGizmoSummoned(const AppCommandState& st);
/// 3DMOVE / 3DROTATE / 3DSCALE. Honours a pre-selection (straight to the handles), otherwise asks
/// for objects, Enter when done. Refuses with a stated reason outside model space.
void StartGizmoCommand(AppCommandState& st, AppCommandState::Kind kind,
                       std::vector<std::string>& log);
/// Ends a 3D gizmo command however it ends: abandons any armed drag (nothing has changed yet),
/// restores \ref AppCommandState::gizmoOp and sets \c active back to \c None.
void EndGizmoCommand(AppCommandState& st);
/// Typed input to a running 3D gizmo command. Blank: confirm the selection, commit an armed drag,
/// or end the command when nothing is armed. A number: the exact value — distance (move), degrees
/// (rotate) or factor (scale) — along the grabbed handle, or along the only handle when there is
/// just one. Returns false when no 3D gizmo command is running.
bool HandleGizmoCommandTextInput(const std::string& line, AppCommandState& st,
                                 std::vector<std::string>& log);
/// The base point of a running 3D gizmo command, picked or typed (D-2026-09-28-c): the gizmo moves
/// there and the command goes on to its handles. Ignored outside that step.
void SubmitGizmoBasePoint(AppCommandState& st, double x, double y, double z,
                          std::vector<std::string>& log);
/// The prompt for the running 3D gizmo command's current step.
[[nodiscard]] std::string CadGizmoCommandPromptText(const AppCommandState& st);

/// Signed position along the line (\p anchor, \p axisDir) of the point on it nearest \p ray.
///
/// The standard skew-line solve. False when the ray is within \p parallelTol of parallel to the
/// axis - looking straight down a handle, every point of it projects to the same pixel and there is
/// no distance the gesture could mean. Refusing is the honest answer; the alternative is a huge
/// number from a near-singular divide, which reads as the selection flying off the screen.
[[nodiscard]] bool CadAxisDragParam(const ray3d::Vec3& anchor, const ray3d::Vec3& axisDir,
                                    const ray3d::Ray& ray, double* outParam,
                                    double parallelTol = 1.e-6);

/// Angle of \p ray's hit on the plane through \p anchor with normal \p axisDir, measured in that
/// plane's own frame, CCW-positive about \p axisDir (REQ-060 rotate, TASK-232).
///
/// The rotation counterpart of \ref CadAxisDragParam, and it refuses for the same kind of reason
/// that one does. False when the ray is within \p parallelTol of PARALLEL to the plane — it never
/// meets it, so the gesture names no point — and false when the hit lands within \p minRadius of the
/// anchor, where there is no direction to take an angle of. Both are cases where a number could be
/// produced and would be meaningless; returning one would read as the selection spinning wildly.
///
/// The frame is built from \p axisDir alone (not from the camera), so the angle a drag reports does
/// not change when the view orbits.
[[nodiscard]] bool CadAxisDragAngle(const ray3d::Vec3& anchor, const ray3d::Vec3& axisDir,
                                    const ray3d::Ray& ray, double* outAngle,
                                    double parallelTol = 1.e-6, double minRadius = 1.e-9);

/// Which axis handle \p ray hits, or -1. \p tolWorld is the grab aperture in drawing units.
///
/// Nearest handle wins, measured as the true 3D distance between the ray and the handle SEGMENT -
/// so a handle pointing away from the camera, which projects to almost nothing, is hard to grab by
/// accident and the one pointing across the view is easy.
[[nodiscard]] int PickGizmoAxis(const AppCommandState& st, const ray3d::Ray& ray, double tolWorld);

/// One click on the gizmo, in the command layer where a transcript can drive it.
///
/// Grabs the handle under \p ray if one is there and nothing is armed yet; otherwise commits the
/// armed drag at the ray's position. Returns true when the click was the gizmo's - the caller must
/// then NOT treat it as an ordinary selection click.
bool SubmitGizmoClick(AppCommandState& st, const ray3d::Ray& ray, double tolWorld,
                      std::vector<std::string>& log);

/// Refresh \ref AppCommandState::gizmoDragDistance from the cursor. No-op when nothing is armed.
void UpdateGizmoDrag(AppCommandState& st, const ray3d::Ray& ray);

/// Refresh \ref AppCommandState::gizmoHoverAxis from the cursor. No-op while a drag is armed - the
/// grabbed handle stays lit, and a pre-highlight of a handle the click cannot reach is a lie.
void UpdateGizmoHover(AppCommandState& st, const ray3d::Ray& ray, double tolWorld);

/// Apply the armed drag as ONE undoable step, and disarm.
///
/// Entity mode goes through `ApplyTranslationToSelection`, the function typed MOVE calls; face mode
/// goes through `CadApplyPushPull`, the function typed `PRESSPULL` calls. Both agreements are then
/// structural rather than two implementations that happen to match today — REQ-060's second
/// acceptance bullet and issue #148's fourth, stated the same way.
///
/// False (and nothing changed) when no drag is armed, when the distance is zero, or when the kernel
/// refuses the push - in which case `CadApplyPushPull` has already logged its own sentence and the
/// document is untouched.
bool CommitGizmoDrag(AppCommandState& st, std::vector<std::string>& log);

/// Disarm without moving anything. Safe to call at any time; ESC and a right-click both do.
void CancelGizmoDrag(AppCommandState& st);
