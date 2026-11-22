// ---------------------------------------------------------------------------------------------
// B-rep solids (REQ-313 / ADR-045, GitHub issue #146).
// ---------------------------------------------------------------------------------------------

/// True when solid \p solidIndex is drawn AND clickable. One predicate for both, for the reason
/// \ref SurfaceVisible gives for itself: "invisible" and "unclickable" must not be able to disagree
/// (REQ-084 (d)).
[[nodiscard]] bool SolidVisible(const AppCommandState& st, size_t solidIndex);

/// True when point cloud \p index is drawn AND clickable, mirroring \ref SolidVisible exactly
/// (REQ-171 part 14: point clouds had no candidate-generation in either pick path before this).
[[nodiscard]] bool PointCloudVisible(const AppCommandState& st, size_t index);

/// Returns the open `.gscloud` cache for cloud \p cloudIdx from
/// \ref AppCommandState::pointCloudOpenCaches, opening (or re-opening, if \ref
/// CadPointCloud::cloudCachePath changed since it was last opened) and caching it on first use.
/// nullptr when the cloud has no cache or it failed to open. Shared by EXTRACTCENTERLINE
/// (REQ-347) and the PointCloud object snap (REQ-348).
[[nodiscard]] const pointcloudcache::OpenCache* GetOrOpenPointCloudCache(AppCommandState& st,
                                                                          int cloudIdx);

/// Bring \ref AppCommandState::solidDisplayCache and \ref AppCommandState::solidDisplayGeometry up
/// to date. Called once a frame, beside \ref RefreshSurfaceDisplayGeometry.
///
/// Regenerates a solid's triangles and edges only when its staleness key — the solid pointer plus
/// the chord tolerance — has moved, which is #120's "do not regenerate a solid's render mesh every
/// frame" stated as a property of what the function does rather than as an intention. Entries whose
/// solid no longer exists are reaped in the same pass, so an erased solid's triangles do not outlive
/// it. The batch list at the end is rebuilt every call — it copies pointers and colours, never
/// vertices — because layer visibility and object isolation change with no solid changing at all.
void RefreshSolidDisplayGeometry(AppCommandState& st);

/// Create one of the seven primitives from a typed command line (REQ-313). \p verb is the
/// already-lowercased command token; \p rest is everything after it.
///
/// Every failure is reported by name and creates nothing (REQ-201): a bad dimension, a bad base
/// point, a wrong argument count, or a solid the kernel refuses to build.
void CadCreateSolidPrimitive(AppCommandState& st, const std::string& verb, const std::string& rest,
                             std::vector<std::string>& log);

/// True when \p verb names one of the seven primitive commands. Used by the command dispatch and by
/// the help registry, so the two cannot disagree about which commands exist.
[[nodiscard]] bool CadIsSolidPrimitiveVerb(const std::string& verb);

/// PRESSPULL (REQ-319, widened by GitHub issue #396) — the one-line shortcut `PRESSPULL <distance>`.
/// Its target is either the one Ctrl+clicked solid FACE (the REQ-318 sub-object selection) or a
/// selected closed polyline/circle, which PRESSPULL extrudes into (or out of) a new solid the same
/// way EXTRUDE does. Refuses — by name, with the document untouched — an empty or ineligible
/// selection, more than one candidate, a distance that is not a number, and every geometric refusal
/// `brep::PushPullFace` / `brep::Extrude` raises. One undo step for the whole edit.
///
/// `StartPressPullCommand` is the prompted form a bare `PRESSPULL` opens: select a target (if none is
/// yet), then a distance that can be typed or dragged from the cursor with a live ghost — EXTRUDE's
/// own shape.
void CadPressPull(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
void StartPressPullCommand(AppCommandState& st, std::vector<std::string>& log);

/// True when the REQ-318 sub-object selection holds solid EDGES and nothing else — the one state in
/// which a typed `FILLET` means the solid fillet rather than the 2D one (REQ-323).
[[nodiscard]] bool CadSubObjectSelectionIsAllEdges(const AppCommandState& st);

/// FILLET on the selected solid edge(s) (REQ-323 increment 1, GitHub issue #148 acceptance 5).
///
/// Paired with the sub-object selection exactly as `CadPressPull` is: Ctrl+click names the edge,
/// this rounds it. One solid at a time, one undo step however many edges are named, and every
/// refusal is the kernel's own sentence with the document untouched — `brep::FilletEdges` pre-checks
/// everything, so nothing was built rather than something rolled back.
///
/// **Clears the sub-object selection on success**, unlike push/pull, which re-points it. A fillet
/// changes the TOPOLOGY: the selected edge is gone and every index after it has shifted, so a kept
/// reference would name whatever edge inherited the number.
void CadFilletSolidEdges(AppCommandState& st, const std::string& args,
                         std::vector<std::string>& log);

/// Round the selected solid edge(s) at \p radius, as one undoable step. The shared commit behind
/// both the one-line `FILLET <radius>` and the prompted form, so the two cannot diverge about what a
/// fillet does. False (and nothing changed) on any refusal, which is already logged by name.
bool CadApplyFilletToSelectedEdges(AppCommandState& st, double radius,
                                   std::vector<std::string>& log);

/// Re-prompt after a `Ctrl`+click gathered (or failed to gather) a solid edge while FILLET is
/// running. Says how many edges are held and that a radius is what finishes the command.
void CadFilletReportEdgeSelection(AppCommandState& st, std::vector<std::string>& log);

/// CHAMFER on a solid EDGE (REQ-331, GitHub issue #148 acceptance 5). The same verb the 2D chamfer
/// uses and the same split `CadFilletSolidEdges` has: a bare `CHAMFER` with solid edges selected
/// prompts for the distance, `CHAMFER <d>` applies it in one line. The 2D flow is untouched — this
/// path is taken only when the sub-object selection holds solid edges and nothing else, a state the
/// 2D flow has never been able to reach.
void CadChamferSolidEdges(AppCommandState& st, const std::string& args,
                          std::vector<std::string>& log);

/// Bevel the selected solid edge(s) by \p distance, as one undoable step. The shared commit behind
/// both the one-line and the prompted form. False (and nothing changed) on any refusal, which is
/// already logged by name — every `brep::ChamferEdges` refusal is a pre-check, so "unchanged" is
/// true because nothing was built rather than because something was rolled back.
bool CadApplyChamferToSelectedEdges(AppCommandState& st, double distance,
                                    std::vector<std::string>& log);

/// Re-prompt after a `Ctrl`+click gathered (or failed to gather) a solid edge while CHAMFER is
/// running. The twin of `CadFilletReportEdgeSelection`.
void CadChamferReportEdgeSelection(AppCommandState& st, std::vector<std::string>& log);
void CancelPressPullCommand(AppCommandState& st);

/// The prompt for whatever the PRESSPULL command is waiting for — the target, or the distance with
/// the live cursor value shown. Shared by the command line and the at-cursor dynamic input (REQ-304).
[[nodiscard]] std::string CadPressPullPromptText(const AppCommandState& st);

/// Feed one typed line to a running PRESSPULL command. Returns false when the text was not
/// understood, which leaves the command where it was rather than cancelling it.
[[nodiscard]] bool HandlePressPullTextInput(const std::string& line, AppCommandState& st,
                                            std::vector<std::string>& log);

/// A viewport click during PRESSPULL: confirms the target (SelectTarget) or commits at the
/// cursor-resolved distance (WaitDistance).
void SubmitPressPullViewportPick(AppCommandState& st, float wx, float wy, std::vector<std::string>& log);

/// Resolve what the cursor is currently worth as a push/pull distance and publish it on \p st as
/// `pressPullDistPickValid` / `pressPullDistPick`. The same closest-approach-between-cursor-ray-and-
/// axis geometry \ref CadResolveExtrudePick uses, along the gathered target's own normal (face mode,
/// via \ref CadSubObjectFaceGrip) or plane normal (profile mode). \p cursorOnPlane is storage
/// coordinates; \p ray is the pick ray, or null in plan view (where a distance cannot be read off the
/// screen).
void CadResolvePressPullPick(AppCommandState& st, const ray3d::Vec3& cursorOnPlane, const ray3d::Ray* ray);

/// The candidate solid the running PRESSPULL command describes at \p distance, for the live ghost and
/// for the commit — one function, so the ghost cannot show a shape the click would not build. Returns
/// false (and leaves \p out untouched) when the number does not yet describe a solid.
[[nodiscard]] bool CadBuildPressPullSolid(const AppCommandState& st, double distance, brep::Solid* out);

/// Apply one push/pull and record it as a single undo step. The shared commit behind both the typed
/// `PRESSPULL` and the grip drag, so the two cannot diverge about what a push does — the same
/// single-implementation rule REQ-318 item 1 states for the pick.
///
/// Replaces the solid, re-points every sub-object reference that named it (the reference is keyed on
/// identity, and the solid has just been replaced), and logs the kernel's own sentence on a refusal
/// with the document untouched.
bool CadApplyPushPull(AppCommandState& st, const SelectedSubObject& ref, double distance,
                      std::vector<std::string>& log);

/// The face grip's anchor and slide axis: the face's centroid, and its outward normal (REQ-319
/// increment 2). False when \p ref does not resolve to a planar face of a live solid.
///
/// The centroid rather than a corner, because a grip is a handle on the *face* — a corner handle
/// reads as a vertex grip, which is a different edit (\ref CadSubObjectVertexGrip, REQ-333).
[[nodiscard]] bool CadSubObjectFaceGrip(const AppCommandState& st, const SelectedSubObject& ref,
                                        ray3d::Vec3* outAnchor, ray3d::Vec3* outAxis);

/// The vertex grip's anchor: the vertex itself (REQ-333). False unless the vertex is one where
/// **exactly three planar faces meet** — the only place `brep::MoveVertex` can work.
///
/// The check lives here so that no handle is drawn where the drag would be refused. A pyramid's apex
/// and a cylinder's rim are both easy to pick and both impossible to move, and a grip that appears
/// and then declines on release is worse than one that never appears — the same discipline
/// \ref CadSubObjectFaceGrip already keeps by returning false for a non-planar face.
[[nodiscard]] bool CadSubObjectVertexGrip(const AppCommandState& st, const SelectedSubObject& ref,
                                          ray3d::Vec3* outAnchor);

/// The edge grip's anchor and its two slide axes: the edge's midpoint, and the outward normals of
/// the two faces along it (REQ-333). False unless the edge is STRAIGHT with exactly two planar
/// faces, for the reason \ref CadSubObjectVertexGrip gives.
///
/// The two normals rather than a pair of UCS axes because they are what the kernel actually offsets,
/// and together they span exactly the plane perpendicular to the edge — which is the whole space of
/// moves the edge has.
[[nodiscard]] bool CadSubObjectEdgeGrip(const AppCommandState& st, const SelectedSubObject& ref,
                                        ray3d::Vec3* outAnchor, ray3d::Vec3* outAxisA,
                                        ray3d::Vec3* outAxisB);

/// Move one vertex, or one edge, and record it as a single undo step (REQ-333). The commit behind
/// the vertex and edge grips, shaped exactly like \ref CadApplyPushPull: replace the solid, re-point
/// every sub-object reference that named it, and log the kernel's own sentence on a refusal with the
/// document untouched.
///
/// **Unlike push/pull these have no typed command to agree with**, because REQ-333 defines kernel
/// operations and no requirement asks for a verb. REQ-060's "agrees with the equivalent typed
/// command" therefore has nothing to compare against here; the discipline it exists to enforce is
/// kept the only way it can be, by these being the single implementation — so a typed command added
/// later calls them rather than growing a second one.
bool CadApplyMoveVertex(AppCommandState& st, const SelectedSubObject& ref, const ray3d::Vec3& delta,
                        std::vector<std::string>& log);
bool CadApplyMoveEdge(AppCommandState& st, const SelectedSubObject& ref, const ray3d::Vec3& delta,
                      std::vector<std::string>& log);

/// One named dimension of a primitive: the letter that sets it, and what to call it in a prompt.
///
/// `optional` marks a parameter the primitive can be built without — only a cone's and a pyramid's
/// top radius, which default to zero and give an apex. Everything else must be set before Enter will
/// create anything, so a half-specified solid is refused with the missing names rather than built at
/// some assumed size (REQ-201).
/// How the cursor supplies a dimension, which is what the live preview and the click both read.
///
/// `Typed` is a real answer, not a gap: a pyramid's side count and a cone's top radius have no
/// natural mouse gesture, so they stay keyword-and-default and the pick sequence skips them.
enum class SolidPickKind : std::uint8_t {
  Typed,     ///< keyword + value only; never picked.
  Radius,    ///< distance from the base point, measured IN the work plane.
  Height,    ///< signed distance along the work plane's normal, from the axis nearest the cursor ray.
  CornerXY,  ///< box / wedge: one pick sets length AND width from the opposite corner.
};

struct SolidParamSpec {
  char letter = '\0';
  const char* label = "";
  bool optional = false;
  SolidPickKind pick = SolidPickKind::Typed;
  /// What an optional parameter is worth when the user never sets it — a cone's apex (0) and a
  /// pyramid's four sides. Ignored unless \ref optional.
  double defaultValue = 0.0;
};

/// The named dimensions of \p kind, in the order a bare typed number fills them — which is also the
/// order the one-line form takes its arguments, so `CYLINDER 0,0 4 25` and `CYLINDER` / `0,0` /
/// `4` / `25` mean the same thing.
///
/// **The single table both the prompt and the commit read.** A prompt that offered a letter the
/// commit did not know, or a commit that needed a value the prompt never asked for, is the failure
/// this exists to make impossible.
[[nodiscard]] const SolidParamSpec* CadSolidParamSpecs(brep::PrimitiveKind kind, int* outCount);

/// The placement frame the prompted solid command is building in: the active UCS's orientation moved
/// to the base point. Exposed so the live preview measures its rubber line along the SAME axes the
/// solid is built on - a measuring line drawn on the world frame would drift off a tilted solid.
[[nodiscard]] ucs::Ucs CadSolidPlacementFrameFor(const AppCommandState& st);

/// Work out what the cursor is currently worth to the prompted solid command, and publish it on
/// \p st as `solidPickValid` / `solidPickA` / `solidPickB` / `solidPickAngleRad`.
///
/// \p cursorOnPlane is the cursor resolved onto the work plane, in storage coordinates.
/// \p ray is the pick ray, or null in plan view.
///
/// **Domain logic, not viewport logic**, even though the viewport is what calls it every frame: it
/// is geometry — a distance in a plane, an angle, a closest approach between a ray and an axis — and
/// putting it here is what lets a transcript drive the same resolution the mouse does. A height
/// resolved one way for the preview and another for the test would be a test of nothing.
void CadResolveSolidPick(AppCommandState& st, const ray3d::Vec3& cursorOnPlane, const ray3d::Ray* ray);

/// Index of the dimension the prompted command is currently picking — the first required one still
/// unset whose \ref SolidParamSpec::pick is not `Typed`. -1 when nothing is left to pick.
[[nodiscard]] int CadSolidCurrentPickParam(const AppCommandState& st);

/// Build the solid the prompted command currently describes.
///
/// **The one place a set of numbers becomes a shape**, called by the live preview, by the click that
/// commits a dimension, and by Enter. A preview computed separately from the commit is a preview
/// that eventually shows a solid the click does not build, which is worse than no preview at all.
///
/// \p applyPick folds `solidPickA`/`solidPickB` into the dimension currently being picked, which is
/// what makes the preview follow the cursor; false uses only what has been committed.
///
/// Returns false with \p outWhy set when the numbers so far do not describe a solid — a zero radius
/// before the cursor has moved, a dimension still missing. The preview simply draws nothing then,
/// which is the honest answer while a value is still being chosen.
[[nodiscard]] bool CadBuildSolidFromCommand(const AppCommandState& st, bool applyPick, brep::Solid* out,
                                            brep::Problem* outWhy);

/// Begin the prompted form of a primitive command: `CYLINDER` with no arguments. \p verb is the
/// already-lowercased command token.
void StartSolidPrimitiveCommand(AppCommandState& st, const std::string& verb, std::vector<std::string>& log);

/// The prompt for whatever the solid command is waiting for — the base point, or the named
/// dimensions with the ones already set shown back. Shared by the command line and the at-cursor
/// dynamic input so the two cannot say different things (REQ-304).
[[nodiscard]] std::string CadSolidPromptText(const AppCommandState& st);

/// Feed one typed line to the running solid command. Returns false when the text was not understood,
/// which leaves the command where it was rather than cancelling it.
[[nodiscard]] bool HandleSolidTextInput(const std::string& line, AppCommandState& st,
                                        std::vector<std::string>& log);

/// Feed a picked point (storage X/Y, at the current work-plane elevation) to the running solid
/// command.
void SubmitSolidViewportPick(AppCommandState& st, float wx, float wy, std::vector<std::string>& log);

/// Clear the prompted solid command's state. Called by Esc and by every other command start, so a
/// half-built solid cannot leak into the next command.
void CancelSolidCommand(AppCommandState& st);

// --- REQ-317 POLYSOLID ---------------------------------------------------------------------------
/// Open the command: pick a start point, or `O` to sweep along something already drawn.
void StartPolysolidCommand(AppCommandState& st, std::vector<std::string>& log);
/// The prompt line, computed rather than literal: it echoes the height, width and justification in
/// force, which is what makes them discoverable without a separate report command (REQ-304).
[[nodiscard]] std::string CadPolysolidPromptText(const AppCommandState& st);
/// Handle one typed line: a coordinate, or one of `A L C U H W J O`. \neturn false if not consumed.
bool HandlePolysolidTextInput(const std::string& line, AppCommandState& st,
                              std::vector<std::string>& log);
/// Handle a viewport click: a path point, or — at the `O`bject prompt — the entity to sweep along.
void SubmitPolysolidViewportPick(AppCommandState& st, float wx, float wy, std::vector<std::string>& log);
/// Convert the Line / Arc / Circle / Polyline under (\p wx, \p wy) into a wall, or say why not.
void CadPolysolidConvertObjectAt(AppCommandState& st, float wx, float wy, std::vector<std::string>& log);
/// Reset the path, keeping the remembered height, width and justification.
void CancelPolysolidCommand(AppCommandState& st);
/// The frame a polysolid is built in: the active UCS anchored at the first picked point. Exposed so
/// the viewport can put the cursor into the same plane the builder reads it from - one frame, not two.
[[nodiscard]] ucs::Ucs CadPolysolidFrameFor(const AppCommandState& st);

/// The candidate wall, optionally including the segment \p cursor is currently proposing.
///
/// ONE builder for the preview, the click that commits a point and the Enter that finishes — a
/// preview computed separately from the commit is a preview that eventually shows a wall the click
/// does not build (ADR-046 (a)).
[[nodiscard]] bool CadBuildPolysolidFromCommand(const AppCommandState& st, const ucs::Point2D* cursor,
                                                brep::Solid* out, brep::Problem* outWhy);

/// Report a solid's properties into \p log — kind, dimensions, volume, surface area, and its
/// vertex/edge/face counts. The SOLIDLIST command, and the one place those numbers are formatted.
void CadReportSolids(const AppCommandState& st, std::vector<std::string>& log);

/// Mass properties including inertia and principal axes (REQ-349, GitHub #460) — the MASSPROP command.
void CadReportMassProperties(const AppCommandState& st, std::vector<std::string>& log);

/// REQ-313 as amended (D-2026-09-09-j) — SOLIDCHECK: report each solid's validity, and separately
/// whether its surface passes through itself. Read-only; nothing is repaired.
void CadCheckSolids(AppCommandState& st, std::vector<std::string>& log);

/// EXTRUDE (REQ-314 / ADR-046, GitHub issue #147): turn each eligible entity in the current
/// selection — a closed polyline or a circle — into a B-rep solid, swept a signed height
/// perpendicular to the profile's plane. One undo step; the source entities are left in place.
/// Every failure is reported by name and stores nothing (REQ-201).
///
/// `CadExtrudeSelection` is the one-line shortcut `EXTRUDE <height>`. `StartExtrudeCommand` is the
/// prompted form a bare `EXTRUDE` opens: select objects (if none are yet), then a height that can
/// be typed or dragged from the cursor with a live ghost.
void CadExtrudeSelection(AppCommandState& st, const std::string& rest, std::vector<std::string>& log);
void StartExtrudeCommand(AppCommandState& st, std::vector<std::string>& log);
void CancelExtrudeCommand(AppCommandState& st);

/// The prompt for whatever the EXTRUDE command is waiting for — the selection, or the height with
/// the live cursor value shown. Shared by the command line and the at-cursor dynamic input (REQ-304).
[[nodiscard]] std::string CadExtrudePromptText(const AppCommandState& st);

/// Feed one typed line to a running EXTRUDE command. Returns false when the text was not understood,
/// which leaves the command where it was rather than cancelling it.
[[nodiscard]] bool HandleExtrudeTextInput(const std::string& line, AppCommandState& st,
                                          std::vector<std::string>& log);

/// A viewport click during EXTRUDE: confirms the selection (SelectProfiles) or commits the solid at
/// the cursor-resolved height (WaitHeight).
void SubmitExtrudeViewportPick(AppCommandState& st, float wx, float wy, std::vector<std::string>& log);

/// Resolve what the cursor is currently worth as an extrusion height and publish it on \p st as
/// `extrudeHeightPickValid` / `extrudeHeightPick`. Domain logic, not viewport logic — it is the
/// closest approach between the cursor ray and the profile's plane normal, the same geometry the
/// prompted solid command's Height pick uses. \p cursorOnPlane is storage coordinates; \p ray is
/// the pick ray, or null in plan view (where a height cannot be read off the screen).
void CadResolveExtrudePick(AppCommandState& st, const ray3d::Vec3& cursorOnPlane, const ray3d::Ray* ray);

/// The candidate solids the running EXTRUDE command describes at \p height, for the live ghost and
/// for the commit — one function, so the ghost cannot show a shape the click would not build.
/// Returns false (and clears \p out) when the numbers do not yet describe a solid.
[[nodiscard]] bool CadBuildExtrudeSolids(const AppCommandState& st, double height,
                                         std::vector<brep::Solid>* out);

// --- LOFT (REQ-315 / ADR-048, GitHub issue #241) ---------------------------------------------

/// Begin the LOFT command: select two or more closed polylines / circles **in lofting order**, then
/// Enter to skin one B-rep solid through them (`brep::Loft` — freeform `SurfaceKind::Nurbs` side
/// faces). If two or more eligible profiles are already selected, a bare `LOFT` builds immediately.
/// One undo step; the source entities are left in place; every failure is reported by name (REQ-201).
void StartLoftCommand(AppCommandState& st, std::vector<std::string>& log);
void CancelLoftCommand(AppCommandState& st);

/// The prompt for the running LOFT command — always the "select profiles" line, with a count of what
/// is selected so far. Shared by the command line and the at-cursor dynamic input (REQ-304).
[[nodiscard]] std::string CadLoftPromptText(const AppCommandState& st);

/// Feed one typed line to a running LOFT command. An empty line (Enter) builds; anything else is not
/// understood and leaves the command running. Returns false when the command is not LOFT.
[[nodiscard]] bool HandleLoftTextInput(const std::string& line, AppCommandState& st,
                                       std::vector<std::string>& log);

/// The candidate solid the running LOFT command describes from the current selection, for the live
/// ghost and for the commit — one function, so the ghost cannot show a shape the Enter would not
/// build. Returns false when the selection does not yet hold two loftable profiles.
[[nodiscard]] bool CadBuildLoftSolid(const AppCommandState& st, brep::Solid* out);

// --- SWEEP (REQ-315 / ADR-048, GitHub issue #241) -------------------------------------------

/// Begin the SWEEP command: select **one** closed polyline / circle (the profile) and **one** line,
/// arc or open polyline (the path), then Enter to sweep the profile along the path (`brep::Sweep`).
/// The closed loop is taken as the profile and the open curve as the path, so the selection order
/// does not matter. A polyline path's per-vertex bulges become its arc segments (REQ-316); a sharp
/// corner in the path is refused by name (`brep::Problem::SweepPathCorner`). If both operands are
/// already selected, a bare `SWEEP` builds immediately. One undo step; the source entities are left
/// in place; every failure is reported by name (REQ-201).
void StartSweepCommand(AppCommandState& st, std::vector<std::string>& log);
void CancelSweepCommand(AppCommandState& st);

/// The prompt for the running SWEEP command. Shared by the command line and the at-cursor dynamic
/// input (REQ-304).
[[nodiscard]] std::string CadSweepPromptText(const AppCommandState& st);

/// Feed one typed line to a running SWEEP command. An empty line (Enter) builds; anything else is
/// not understood. Returns false when the command is not SWEEP.
[[nodiscard]] bool HandleSweepTextInput(const std::string& line, AppCommandState& st,
                                        std::vector<std::string>& log);

/// The candidate solid the running SWEEP command describes from the current selection, for the live
/// ghost and the commit. Returns false when the selection does not yet hold a profile and a path.
[[nodiscard]] bool CadBuildSweepSolid(const AppCommandState& st, brep::Solid* out);

// --- REVOLVE (REQ-314 / ADR-046 increment 2b) -------------------------------------------------

/// Begin the prompted REVOLVE command a bare `REVOLVE` opens: select a closed polyline or circle
/// (if none is selected), then the two ends of the revolve axis, then an angle. `REVOLVE <deg>`
/// with a selection and axis is not offered — the axis needs two points.
void StartRevolveCommand(AppCommandState& st, std::vector<std::string>& log);
void CancelRevolveCommand(AppCommandState& st);
[[nodiscard]] std::string CadRevolvePromptText(const AppCommandState& st);
[[nodiscard]] bool HandleRevolveTextInput(const std::string& line, AppCommandState& st,
                                          std::vector<std::string>& log);
void SubmitRevolveViewportPick(AppCommandState& st, float wx, float wy, std::vector<std::string>& log);

/// The candidate solids the running REVOLVE command describes at \p angleDeg — for the live ghost
/// and the commit. False (and \p out cleared) until a profile and both axis points are set.
[[nodiscard]] bool CadBuildRevolveSolids(const AppCommandState& st, double angleDeg,
                                         std::vector<brep::Solid>* out);

// --- UNION / SUBTRACT / INTERSECT (REQ-314 / ADR-046 increment 4, B1) -----------------------

enum class CadBooleanOp : std::uint8_t { Union, Subtract, Intersect };

/// Combine the two B-rep solids in the current selection (REQ-314 B1). UNION and INTERSECT do not
/// care about order; SUBTRACT keeps the first selected solid and removes the second. Both operands
/// are replaced by the result in one undo step. A pair the kernel refuses (a curved or non-convex
/// operand, no shared volume for INTERSECT) is reported and nothing in the document changes
/// (REQ-201). Needs exactly two selected solids.
void CadBooleanSelection(AppCommandState& st, CadBooleanOp op, std::vector<std::string>& log);

/// Begin the prompted UNION / SUBTRACT / INTERSECT command. SUBTRACT prompts for the solids to
/// subtract from, then the solids to subtract; UNION and INTERSECT prompt once. Honors a
/// pre-selection as the answer to the first prompt.
void StartBooleanCommand(AppCommandState& st, CadBooleanOp op, std::vector<std::string>& log);
void CancelBooleanCommand(AppCommandState& st);
[[nodiscard]] std::string CadBooleanPromptText(const AppCommandState& st);
[[nodiscard]] bool HandleBooleanTextInput(const std::string& line, AppCommandState& st,
                                          std::vector<std::string>& log);

// --- SLICE (REQ-314 / ADR-046 increment 3b) -------------------------------------------------

/// Begin the prompted SLICE command a bare `SLICE` opens: select solids (if none are selected),
/// three points for the cutting plane, then a point on the side to keep (or `B` for both).
void StartSliceCommand(AppCommandState& st, std::vector<std::string>& log);
void CancelSliceCommand(AppCommandState& st);
[[nodiscard]] std::string CadSlicePromptText(const AppCommandState& st);
[[nodiscard]] bool HandleSliceTextInput(const std::string& line, AppCommandState& st,
                                        std::vector<std::string>& log);
void SubmitSliceViewportPick(AppCommandState& st, float wx, float wy, std::vector<std::string>& log);

// SECTION (REQ-335 increment 2) — the same five entry points as SLICE above, because it is the same
// gesture: choose solids, then define a plane. Increment 1 had none of these; it read the current
// selection and ended, which is why a click during "select an object" fell through to plain picking.
void StartSectionCommand(AppCommandState& st, std::vector<std::string>& log);
void CancelSectionCommand(AppCommandState& st);
[[nodiscard]] std::string CadSectionPromptText(const AppCommandState& st);
[[nodiscard]] bool HandleSectionTextInput(const std::string& line, AppCommandState& st,
                                          std::vector<std::string>& log);
void SubmitSectionViewportPick(AppCommandState& st, float wx, float wy, std::vector<std::string>& log);

// --- The section plane as a manipulable object (REQ-343, GitHub issue #479 acceptance 4-7) -----

/// The clip plane as the renderer will build it this frame. Inactive when the clip is off.
[[nodiscard]] SectionClipPlane CadSectionClipPlane(const AppCommandState& st);

/// The rectangle the section plane is DRAWN as, built from the same inputs the renderer uses.
///
/// **One function, called by both sides.** The renderer draws this rectangle and the pick tests
/// against it; if they computed it separately and ever differed, the user would click where the
/// plane is drawn and grab nothing. It also owns the model-bounds walk that `main.cpp` used to do
/// inline, which is what made two copies possible in the first place.
[[nodiscard]] SectionClipIndicator CadSectionClipIndicator(const AppCommandState& st);

/// Handles for the selected section plane. Invalid when the plane is off or not selected.
[[nodiscard]] SectionPlaneGrips CadSectionPlaneGrips(const AppCommandState& st);

/// Which handle \p ray hits, or `None`. \p tolWorld is the grab aperture in drawing units.
[[nodiscard]] SectionPlaneGrip PickSectionPlaneGrip(const AppCommandState& st, const ray3d::Ray& ray,
                                                    double tolWorld);

/// One click on the section plane or its handles, in the command layer where a test can drive it.
///
/// Grabs a handle, commits an armed drag, toggles the flip handle, or selects/deselects the plane.
/// Returns true when the click was the section plane's — the caller must then NOT also treat it as
/// an ordinary selection click.
bool SubmitSectionPlaneClick(AppCommandState& st, const ray3d::Ray& ray, double tolWorld,
                             std::vector<std::string>& log);

/// Refresh the armed drag from the cursor. No-op when nothing is armed.
///
/// \p snapPoint, when given, is the object-snap point under the cursor (REQ-344). The handle then
/// lands where that point projects onto its drag axis, so a section plane can be placed exactly on
/// a midpoint, an endpoint or a face centre instead of wherever the cursor happened to be — which
/// is what makes the cut a measured thing rather than an eyeballed one.
///
/// **In STORAGE coordinates, like \p ray and like the plane itself** — `local = world - origin`.
/// Everything in this drag is in that frame: the clip frame comes from a solid's face, solids are
/// stored local like every other store, and the camera ray is the same one the sub-object pick
/// casts at them. Passing a world point here puts the snap a whole `worldDocumentOrigin` away from
/// the anchor it is measured against; it was documented as world and called with world until a user
/// reported the plane "snapping too far the other direction" (2026-09-11).
///
/// Only a NAMED feature should be passed. `Surface`, `Edge` and `Face` answer with the point
/// nearest the cursor, so under 3D OSNAP one is available almost everywhere on a solid, and an
/// absolute placement driven by those makes the plane skate across the model — see
/// `CadSnap::SnapClass`, and the gate in `CadUi.cpp` that applies it.
void UpdateSectionPlaneGripDrag(AppCommandState& st, const ray3d::Ray& ray,
                                const ray3d::Vec3* snapPoint = nullptr);

/// Refresh \ref AppCommandState::sectionPlaneGripHover. No-op while a drag is armed.
void UpdateSectionPlaneGripHover(AppCommandState& st, const ray3d::Ray& ray, double tolWorld);

/// Disarm and leave the plane where the drag has already put it. Every command start does this,
/// through `ResetAllCadDraftTools` — a drag left armed keeps rewriting the clip offset while the
/// NEXT command takes its picks.
void CancelSectionPlaneGripDrag(AppCommandState& st);

/// A TRUE cancel: put the plane back where it was when the handle was grabbed, then disarm.
///
/// What ESC means. The drag writes the offset and the extent every frame, so disarming alone would
/// COMMIT whatever the cursor last did — and REQ-343 records that a slide makes no undo entry, so
/// there would be no way back.
void AbortSectionPlaneGripDrag(AppCommandState& st);

/// Reverse which half of the model survives, and say so. Also what the flip handle does.
void ToggleSectionClipFlip(AppCommandState& st, std::vector<std::string>& log);
