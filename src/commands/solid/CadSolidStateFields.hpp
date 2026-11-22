  // --- The prompted solid-primitive command (REQ-313 as amended) ---------------------------------

  /// The most named dimensions any primitive has (PYRAMID: sides, base radius, top radius, height).
  static constexpr int kMaxSolidParams = 4;

  enum class SolidPhase : std::uint8_t {
    WaitBasePoint,   ///< Click, or type X,Y[,Z]. The base centre — or the centre, for sphere/torus.
    WaitParameters,  ///< Base set: type a letter + value, a bare value for the next unset one, or Enter.
  } solidPhase = SolidPhase::WaitBasePoint;

  /// Which primitive is being built. `None` means no solid command is running.
  brep::PrimitiveKind solidKind = brep::PrimitiveKind::None;
  /// The base point, in STORAGE coordinates (X/Y local, Z absolute) — the same convention the store
  /// itself uses, so the commit needs no second conversion.
  ray3d::Vec3 solidBase;
  double solidParamValue[kMaxSolidParams] = {0.0, 0.0, 0.0, 0.0};
  bool solidParamSet[kMaxSolidParams] = {false, false, false, false};

  // --- What the cursor is currently worth, republished every frame -------------------------------
  //
  // Resolved in the viewport, where the pick RAY lives, and read by BOTH the live preview and the
  // click that commits it. One value, two consumers: a preview computed separately from the commit
  // is a preview that eventually shows a solid the click does not build, which is worse than no
  // preview at all.
  bool solidPickValid = false;
  double solidPickA = 0.0;      ///< radius, height, or the corner's in-plane X offset.
  double solidPickB = 0.0;      ///< the corner's in-plane Y offset (CornerXY only).
  double solidPickAngleRad = 0.0;  ///< direction from the base point — the pyramid's base rotation.

  /// PYRAMID base rotation, radians in the work plane, taken from the radius pick's direction so the
  /// base turns with the cursor the way AutoCAD's does.
  double solidBaseAngleRad = 0.0;
  /// PYRAMID: is the given radius the polygon's circumradius (inscribed in that circle) or its
  /// apothem (circumscribed about it)? AutoCAD's default is circumscribed, and `I` toggles.
  bool solidInscribed = false;
  /// BOX / WEDGE only: the base point is the first CORNER, not the centre, so the commit shifts the
  /// frame origin to the midpoint of the two corners. Signed, in the work plane's own axes — the
  /// sign is what says which way the box was dragged, which `length` and `width` cannot.
  bool solidBaseIsCorner = false;
  double solidCornerDx = 0.0;
  double solidCornerDy = 0.0;
  /// A letter typed on its own arms the parameter and waits for the value on the next line, which is
  /// what makes `R` then `4` work as well as `R 4`. -1 = nothing armed.
  int solidPendingParam = -1;
  /// The armed parameter was reached through `D`, so the next value is a DIAMETER and is halved
  /// into the radius. Halved in exactly one place, so a diameter can never reach the kernel as a
  /// radius.
  bool solidPendingIsDiameter = false;

  // --- The EXTRUDE command (REQ-314 / ADR-046, GitHub #147) --------------------------------------

  enum class ExtrudePhase : std::uint8_t {
    SelectProfiles,  ///< Accumulate a selection of closed polylines / circles; Enter confirms.
    WaitHeight,      ///< Profiles gathered: type a height, or move the cursor and click.
  } extrudePhase = ExtrudePhase::SelectProfiles;

  /// The profiles gathered when the command left \ref ExtrudePhase::SelectProfiles, in STORAGE
  /// coordinates — kept so the live ghost and the commit build from exactly the same input, the
  /// same one-source-of-truth rule the prompted solid command follows.
  std::vector<brep::Profile> extrudeProfiles;
  /// What the cursor is currently worth as a height, republished every frame from the viewport (the
  /// only place the pick ray lives). Read by the ghost and by the click that commits it. Signed
  /// along the first profile's plane normal; positive is the +normal side.
  bool extrudeHeightPickValid = false;
  double extrudeHeightPick = 0.0;

  // --- The PRESSPULL command (REQ-319 / D-2026-09-04-c, widened by GitHub issue #396) ------------
  // Brought in line with EXTRUDE's flow: a select-target phase (a Ctrl+clicked solid FACE, or a
  // selected closed polyline/circle — mutually exclusive per D-2026-09-04-a), then a distance phase
  // with the same typed-or-dragged-with-a-live-ghost shape.

  enum class PressPullPhase : std::uint8_t {
    SelectTarget,  ///< A face named by Ctrl+click, or a closed-shape entity selection; Enter confirms.
    WaitDistance,  ///< Target gathered: type a distance, or move the cursor and click.
  } pressPullPhase = PressPullPhase::SelectTarget;

  /// True when the gathered target is an existing solid FACE (\ref pressPullFace); false when it is
  /// a closed 2D shape (\ref pressPullProfile) that PRESSPULL will extrude into — or out of — a new
  /// solid. Exactly one of the two is meaningful at a time, decided when \ref PressPullPhase leaves
  /// \c SelectTarget.
  bool pressPullOnFace = false;
  /// The face reference, valid when \ref pressPullOnFace is true. Keyed on the solid's identity
  /// (ADR-049), the same reference \ref subObjectSelection uses.
  SelectedSubObject pressPullFace;
  /// The closed profile, in STORAGE coordinates, valid when \ref pressPullOnFace is false. Kept so
  /// the live ghost and the commit build from exactly the same input, the same one-source-of-truth
  /// rule EXTRUDE's \ref extrudeProfiles follows.
  brep::Profile pressPullProfile;
  /// What the cursor is currently worth as a push/pull distance, republished every frame from the
  /// viewport. Read by the ghost and by the click that commits it. Signed along the target's own
  /// outward normal (face mode) or plane normal (profile mode); positive is outward / +normal.
  bool pressPullDistPickValid = false;
  double pressPullDistPick = 0.0;

  // --- The REVOLVE command (REQ-314 / ADR-046 increment 2, GitHub #147) --------------------------

  enum class RevolvePhase : std::uint8_t {
    SelectProfiles,  ///< Accumulate a selection of closed polylines / circles; Enter confirms.
    WaitAxisStart,   ///< Pick or type the first point of the revolve axis.
    WaitAxisEnd,     ///< Pick or type the second point of the revolve axis.
    WaitAngle,       ///< Type the angle in degrees; Enter takes the default (a full turn).
  } revolvePhase = RevolvePhase::SelectProfiles;

  std::vector<brep::Profile> revolveProfiles;
  ray3d::Vec3 revolveAxisStart;
  bool revolveAxisStartSet = false;
  ray3d::Vec3 revolveAxisEnd;
  double revolveAngleDeg = 360.0;

  // --- The SLICE command (REQ-314 / ADR-046 increment 3, GitHub #147) ---------------------------

  enum class SlicePhase : std::uint8_t {
    SelectSolids,   ///< Accumulate a selection of solids; Enter confirms.
    WaitP1,         ///< First of three points that define the cutting plane.
    WaitP2,
    WaitP3,
    WaitKeepSide,   ///< Pick a point on the side to keep, or [B]oth.
  } slicePhase = SlicePhase::SelectSolids;

  // --- The SECTION command (REQ-335 increment 2, GitHub #149) -----------------------------------
  //
  // Deliberately the SAME shape as SLICE above, because it is the same gesture: choose solids, then
  // define a plane by three points. The two commands ask one question and keep different answers —
  // SLICE keeps the pieces, SECTION keeps the outline — so a user who has learned one has learned
  // the other, and AutoCAD's own SECTION prompts in exactly this order.
  //
  // Increment 1 had no phases at all: it read the current selection and used the active UCS plane,
  // so typing SECTION with nothing selected printed "select one or more solids first" and ENDED.
  // That reads as a prompt and behaves as a refusal — the next click lands with the command already
  // over and merely selects the solid, which is precisely how it was reported.
  enum class SectionPhase : std::uint8_t {
    SelectSolids,  ///< Accumulate a selection of solids; Enter confirms.
    WaitP1,        ///< First of three points defining the section plane, or [UCS] for the work plane.
    WaitP2,
    WaitP3,
  } sectionPhase = SectionPhase::SelectSolids;
  std::vector<int> sectionSolidIndices;  ///< resolved at the end of the selection phase
  /// The solid each index named when it was resolved, so a changed store refuses instead of cutting
  /// whatever now occupies the slot. Parallel to \ref sectionSolidIndices.
  std::vector<std::weak_ptr<const brep::Solid>> sectionSolidOwners;
  ray3d::Vec3 sectionP1{};
  ray3d::Vec3 sectionP2{};
  ray3d::Vec3 sectionP3{};

  // --- SECTIONPLANE's own phases (REQ-342, 2026-09-18 revision) --------------------------------
  //
  // A face answers the command in one click and is still the first thing it offers. Any other point
  // starts a section LINE instead: the plane then stands square to the work plane, through that
  // line — which is the only way to aim a plane at a solid with no flat face at all, such as a
  // sphere or a torus.
  enum class SectionPlanePhase : std::uint8_t {
    PickFaceOrPoint,   ///< A flat face places the plane outright; any other point starts a line.
    WaitThroughPoint,  ///< The second point of the section line.
  } sectionPlanePhase = SectionPlanePhase::PickFaceOrPoint;
  ray3d::Vec3 sectionPlaneP1{};  ///< The first point of the section line, in world coordinates.
  /// Where the cursor is while the through point is being picked, so the plane can be PREVIEWED
  /// standing on the line to it (REQ-342, 2026-09-18). Set by the viewport each frame and cleared
  /// when the command is not asking; nothing is placed from it.
  ray3d::Vec3 sectionPlanePreviewPoint{};
  bool sectionPlanePreviewValid = false;

  // --- The LOFT command (REQ-315 / ADR-048, GitHub #241) ---------------------------------------

  enum class LoftPhase : std::uint8_t {
    SelectProfiles,  ///< Accumulate an ORDERED selection of closed polylines / circles; Enter builds.
  } loftPhase = LoftPhase::SelectProfiles;

  // --- The SWEEP command (REQ-315 / ADR-048, GitHub #241) --------------------------------------

  enum class SweepPhase : std::uint8_t {
    SelectInputs,  ///< Accumulate a closed profile and a line/arc path; Enter builds.
  } sweepPhase = SweepPhase::SelectInputs;

  double sweepTwistDeg = 0.0;      ///< SWEEP `T` keyword: constant twist over the path, degrees.
  bool sweepAlignToPath = true;    ///< SWEEP `A` keyword: stand the profile normal to the path.

  std::vector<int> sliceSolidIndices;  ///< indices into cadSolids, gathered when SelectSolids ends
  ray3d::Vec3 sliceP1;
  ray3d::Vec3 sliceP2;
  ray3d::Vec3 sliceP3;

  // --- The prompted UNION / SUBTRACT / INTERSECT command (REQ-314 / ADR-046, GitHub #147) --------

  enum class BooleanPhase : std::uint8_t {
    SelectOperands,     ///< UNION / INTERSECT: one "select objects" step.
    SelectMinuend,      ///< SUBTRACT step 1: the solids to subtract from.
    SelectSubtrahend,   ///< SUBTRACT step 2: the solids to subtract.
  } booleanPhase = BooleanPhase::SelectOperands;

  /// 0 = Union, 1 = Subtract, 2 = Intersect (matches CadBooleanOp; kept as int so the enum stays in
  /// the .cpp).
  int booleanOp = 0;
  std::vector<int> booleanMinuend;  ///< SUBTRACT: cadSolids indices gathered by step 1.
  // --- REQ-317 POLYSOLID: a wall swept along a picked path ---------------------------------------

  enum class PolysolidPhase : std::uint8_t {
    WaitFirstPoint,  ///< the start of the run, or `O` to convert something already drawn
    WaitObject,      ///< `O` was given: the next click names the entity to sweep along
    WaitNextPoint    ///< a run is under way; each further point commits a segment
  } polysolidPhase = PolysolidPhase::WaitFirstPoint;

  /// The path being drawn, in \ref polysolidBase's work-plane coordinates — the same form
  /// `brep::MakePolysolid` takes, so the command never holds a second representation of it.
  brep::Path polysolidPath;
  /// The first point, in storage coordinates: the origin of the placement frame.
  ray3d::Vec3 polysolidBase{};
  /// `A` draws arc segments and `L` straight ones, exactly as PLINE's own option does.
  bool polysolidArcMode = false;
  /// A letter typed on its own, waiting for its value on the next line (`H` then `4`, as well as
  /// `H 4`). 0 = nothing armed; otherwise the uppercase letter.
  char polysolidPending = 0;
