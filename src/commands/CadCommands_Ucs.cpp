// CadCommands_Ucs.cpp — UCS / PLAN / named-view + named-UCS command handling
// (REQ-154, GitHub #126), split out of CadCommands.cpp (TASK-150 Phase 2, issue #142).
//
// A UCS is a way of READING the drawing, never a change to it: nothing here
// touches a stored coordinate. Entry points (StartUcsCommand, ProcessUcsCommandLine,
// ProcessUcsViewportPick, SetActiveUcs, the named-view/UCS helpers, …) are declared
// in CadCommands.hpp and dispatched from CadCommands.cpp; the prompt state machine
// helpers stay file-local statics.

#include "CadCommands.hpp"
#include "CadCommandsInternal.hpp"
#include "CadCoordinateFrame.hpp"
#include "StringUtil.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

// ================================================================================================
// UCS and PLAN (REQ-154, GitHub #126)
//
// The rule everything here obeys: a UCS is a way of READING the drawing, never a change to it.
// Nothing in this section touches a stored coordinate. What changes is how typed input is
// interpreted, how ORTHO is oriented, and what the readouts report.
// ================================================================================================

const NamedUcs* FindNamedUcs(const AppCommandState& st, const std::string& name) {
  const std::string want = StringUtil::toLowerAsciiCopy(StringUtil::trimCopy(name));
  for (const NamedUcs& n : st.ucsNamed) {
    if (StringUtil::toLowerAsciiCopy(n.name) == want)
      return &n;
  }
  return nullptr;
}

// ================================================================================================
// Named views (REQ-106)
//
// The same shape as the UCS's Named option deliberately: one lookup helper, case-insensitive match,
// a reserved refusal, and Save / Restore / Delete / ? on one prompt. A user who has learned
// `UCS N S <name>` already knows `VIEW S <name>`, and there is one convention in the app rather
// than two that differ in small ways.
// ================================================================================================

const NamedView* FindNamedView(const AppCommandState& st, const std::string& name) {
  const std::string want = StringUtil::toLowerAsciiCopy(StringUtil::trimCopy(name));
  for (const NamedView& v : st.namedViews) {
    if (StringUtil::toLowerAsciiCopy(v.name) == want)
      return &v;
  }
  return nullptr;
}

/// The saved view the camera is CURRENTLY sitting in, or nullptr for "Unsaved View".
///
/// Derived every time rather than remembered. A stored "active view name" goes stale the moment the
/// user pans, zooms or orbits — the label would then name a view the camera has already left, which
/// is worse than saying nothing, because the whole point of the word "Unsaved" is to tell you that
/// what you are looking at would be lost.
///
/// Compares everything a restore would set, the UCS included: a view whose camera matches but whose
/// frame does not is not the view you saved, because the coordinates you type in it differ.
const NamedView* CurrentNamedView(const AppCommandState& st) {
  constexpr double kPanTol = 1e-6;
  constexpr float kAngTol = 1e-3f;
  for (const NamedView& v : st.namedViews) {
    if (std::fabs(v.panX - st.viewportPanX) > kPanTol || std::fabs(v.panY - st.viewportPanY) > kPanTol ||
        std::fabs(v.panZ - st.viewportPanZ) > kPanTol)
      continue;
    if (std::fabs(v.zoom - st.viewportZoom) > 1e-6f)
      continue;
    if (std::fabs(v.azimuthDeg - st.viewportAzimuthDeg) > kAngTol ||
        std::fabs(v.elevationDeg - st.viewportElevationDeg) > kAngTol ||
        std::fabs(v.rollDeg - st.viewportRollDeg) > kAngTol)
      continue;
    if (!ucs::FramesMatch(v.ucs, st.activeUcs))
      continue;
    return &v;
  }
  return nullptr;
}

/// Capture the live camera and frame as a view record.
///
/// Reads the same four fields `CadViewCamera` builds its Camera from, so a saved view and the live
/// view cannot disagree about what they mean.
NamedView CaptureCurrentView(const AppCommandState& st, const std::string& name) {
  NamedView v;
  v.name = name;
  v.panX = st.viewportPanX;
  v.panY = st.viewportPanY;
  v.panZ = st.viewportPanZ;
  v.zoom = st.viewportZoom;
  v.azimuthDeg = st.viewportAzimuthDeg;
  v.elevationDeg = st.viewportElevationDeg;
  v.rollDeg = st.viewportRollDeg;  // #153
  v.ucs = st.activeUcs;
  return v;
}

/// Put the camera and frame back, and remember which view we are now sitting in.
///
/// The orientation eases rather than snapping (REQ-059's rule for the ViewCube, and for the same
/// reason: a hard jump makes it easy to lose track of which way the model turned). Pan and zoom are
/// set directly — there is no animation for those anywhere else in the app, and inventing one here
/// would be a second convention.
void RestoreNamedView(AppCommandState& st, const NamedView& v, std::vector<std::string>& log) {
  st.viewportPanX = v.panX;
  st.viewportPanY = v.panY;
  st.viewportPanZ = v.panZ;
  st.viewportZoom = v.zoom;
  // The UCS goes back too (REQ-106): a view saved while working to a lot line is not restored if
  // the camera returns but the coordinate frame does not, because the numbers you type afterwards
  // would mean something different from the ones you typed when you saved it.
  SetActiveUcs(st, v.ucs, log);
  CadStartViewAnimation(st, v.azimuthDeg, v.elevationDeg, v.rollDeg);
  st.activeViewName = v.name;
  log.push_back("VIEW - restored " + v.name + ".");
}

void ListNamedViews(const AppCommandState& st, std::vector<std::string>& log) {
  if (st.namedViews.empty()) {
    log.push_back("VIEW - no saved views in this drawing.");
    return;
  }
  std::string s = "VIEW - saved views:";
  for (const NamedView& v : st.namedViews)
    s += " " + v.name + ";";
  log.push_back(s);
}

/// `VIEW` — save, restore, delete and list named views (REQ-106).
///
/// Inline-only, like `UCSFOLLOW` and unlike `UCS`: every form is one line, so there is no prompt
/// state machine to get stuck in. `VIEW` alone opens the View Manager, which is where the dialog
/// half of REQ-106's "a VIEW command/dialog" lives.
bool ProcessViewCommandLine(AppCommandState& st, const std::string& rest, std::vector<std::string>& log) {
  std::istringstream iss(StringUtil::trimCopy(rest));
  std::string opt;
  if (!(iss >> opt)) {
    st.showViewManagerWindow = true;
    log.push_back("VIEW - View Manager opened.");
    return true;
  }
  const std::string low = StringUtil::toLowerAsciiCopy(opt);
  std::string name;
  std::getline(iss, name);
  name = StringUtil::trimCopy(name);

  if (low == "?" || low == "l" || low == "list") {
    ListNamedViews(st, log);
    return true;
  }
  if (low == "s" || low == "save") {
    if (name.empty()) {
      log.push_back("VIEW - name the view: VIEW S <name>.");
      return true;
    }
    // Overwriting by name is deliberate and matches `UCS N S`: a view is a bookmark, and re-saving
    // one you are standing in is the ordinary way to move it.
    for (NamedView& v : st.namedViews) {
      if (StringUtil::toLowerAsciiCopy(v.name) == StringUtil::toLowerAsciiCopy(name)) {
        const std::string keep = v.name;
        v = CaptureCurrentView(st, keep);
        st.activeViewName = keep;
        log.push_back("VIEW - " + keep + " updated to the current view.");
        return true;
      }
    }
    st.namedViews.push_back(CaptureCurrentView(st, name));
    st.activeViewName = name;
    log.push_back("VIEW - saved as " + name + ".");
    return true;
  }
  if (low == "r" || low == "restore") {
    const NamedView* v = FindNamedView(st, name);
    if (!v) {
      log.push_back("VIEW - there is no saved view named " + name + ".");
      return true;
    }
    RestoreNamedView(st, *v, log);
    return true;
  }
  if (low == "d" || low == "delete") {
    for (size_t i = 0; i < st.namedViews.size(); ++i) {
      if (StringUtil::toLowerAsciiCopy(st.namedViews[i].name) == StringUtil::toLowerAsciiCopy(name)) {
        // Deleting the view you are standing in leaves the camera exactly where it is and drops the
        // name — the view becomes "Unsaved View", which is true, rather than moving the drawing.
        if (StringUtil::toLowerAsciiCopy(st.activeViewName) == StringUtil::toLowerAsciiCopy(name))
          st.activeViewName.clear();
        log.push_back("VIEW - " + st.namedViews[i].name + " deleted.");
        st.namedViews.erase(st.namedViews.begin() + static_cast<std::ptrdiff_t>(i));
        return true;
      }
    }
    log.push_back("VIEW - there is no saved view named " + name + ".");
    return true;
  }
  log.push_back("VIEW - unrecognised option. VIEW [Save/Restore/Delete/?] <name>, or VIEW alone for "
                "the View Manager.");
  return true;
}

ray3d::Vec3 ConstrainToUcsOrtho(const ucs::Ucs& frame, const ray3d::Vec3& anchor, const ray3d::Vec3& target) {
  // ORTHO means "square with the axes" - and once a UCS exists, that means the UCS's axes, not the
  // world's (REQ-047 under REQ-154). Measure the offset in the frame, keep the dominant in-plane
  // component, drop the other. The out-of-plane component is preserved rather than zeroed: the
  // caller may be constraining a point an object snap legitimately lifted off the plane, and
  // flattening it here would move geometry the user had already placed.
  const ray3d::Vec3 d = ucs::WorldVectorToUcs(frame, ray3d::Sub(target, anchor));
  const ray3d::Vec3 keep =
      (std::fabs(d.y) > std::fabs(d.x)) ? ray3d::Vec3{0.0, d.y, d.z} : ray3d::Vec3{d.x, 0.0, d.z};
  return ray3d::Add(anchor, ucs::UcsVectorToWorld(frame, keep));
}

// A one-line description of a frame, for the command log. Coordinates are reported in WORLD, the
// only frame a UCS description can sensibly be stated in - describing a UCS in its own coordinates
// would report every UCS alike as "origin 0,0,0".
std::string DescribeUcs(const ucs::Ucs& u) {
  if (ucs::IsWorld(u))
    return "World";
  char buf[224];
  std::snprintf(buf, sizeof(buf), "origin (%.4f, %.4f, %.4f), X axis (%.4f, %.4f, %.4f)", u.origin.x, u.origin.y,
                u.origin.z, u.xAxis.x, u.xAxis.y, u.xAxis.z);
  return buf;
}

void ApplyPlanViewOf(AppCommandState& st, const ucs::Ucs& frame, std::vector<std::string>& log) {
  float az = st.viewportAzimuthDeg;
  float el = st.viewportElevationDeg;
  ucs::PlanViewAngles(frame, &az, &el);
  // The roll that also places the UCS +Y up the screen (#153). Zero whenever the frame's Z is world
  // +Z, so the flat survey case animates exactly as it did before this existed.
  const float roll = Camera::RollToPlaceUp(az, el, frame.yAxis);
  CadStartViewAnimation(st, az, el, roll);  // ease, never jump (REQ-059)
  (void)log;
}

void SetActiveUcs(AppCommandState& st, const ucs::Ucs& next, std::vector<std::string>& log, bool pushPrevious) {
  // A frame that is not orthonormal and right-handed would silently skew or mirror every coordinate
  // entered under it, so it is refused here rather than stored (REQ-201). Every construction path
  // funnels through this one check, which is why none of them repeat it.
  if (!ucs::IsRightHandedOrthonormal(next, 1e-6)) {
    log.push_back("UCS - that does not define a valid coordinate system; the current UCS is unchanged.");
    return;
  }
  if (pushPrevious) {
    st.ucsPrevious.push_back(st.activeUcs);
    if (st.ucsPrevious.size() > kUcsPreviousDepth)
      st.ucsPrevious.erase(st.ucsPrevious.begin());
  }
  st.activeUcs = next;
  // The grid, the UCS icon and the crosshair all draw from the frame, so the view is stale until
  // the cache is bumped.
  BumpCadGpuCache(st);
  if (ucs::IsWorld(st.activeUcs))
    log.push_back("UCS = World - new geometry is drawn on the world XY plane.");
  else
    log.push_back("UCS = " + DescribeUcs(st.activeUcs) + ". Coordinate entry is now in this frame.");
  // UCSFOLLOW is applied AFTER the frame is live, so the plan view it computes is of the NEW UCS.
  if (st.ucsFollow)
    ApplyPlanViewOf(st, st.activeUcs, log);
}

// ------------------------------------------------------------------------------------------------
// UCS Object: align to an entity the user clicks.
// ------------------------------------------------------------------------------------------------

// Build a frame whose X axis runs along \p dir and whose XY plane contains it, tilted as little as
// possible from the world XY plane. This is what "align to this line" has to mean once the line can
// be a 3D one; AutoCAD's flat-drawing rule (X along the line, Z = the entity's extrusion) is the
// special case of it that a horizontal line produces.
static bool UcsAlignedToDirection(const ray3d::Vec3& origin, const ray3d::Vec3& dir, ucs::Ucs* out) {
  return ucs::AlignedToDirection(origin, dir, out);
}

// Derive a UCS from the entity under \p pickWorld. Returns false, with a reason logged, for the
// entity kinds whose alignment is not defined here.
static bool UcsFromObjectPick(const AppCommandState& st, const ray3d::Vec3& pickWorld,
                              std::vector<std::string>& log, ucs::Ucs* out) {
  // The pick runs in storage space, like every other pick in the application.
  float px = 0.f;
  float py = 0.f;
  CadCoord::LocalFromWorld(st, pickWorld.x, pickWorld.y, &px, &py);
  const float tol = CadOffsetEntityPickTolWorld(st);
  SelectedEntity hit{};
  float pickDistSq = 0.f;
  if (!PickClosestCadEntity(st, static_cast<double>(px), static_cast<double>(py), tol, &hit, &pickDistSq)) {
    log.push_back("UCS Object - no object found at that point. Click a line, polyline, arc, circle, "
                  "ellipse or text.");
    return false;
  }

  auto storageToWorld = [&](float lx, float ly, float lz) {
    double wx = 0.;
    double wy = 0.;
    CadCoord::WorldFromLocal(st, lx, ly, &wx, &wy);
    return ray3d::Vec3{wx, wy, static_cast<double>(lz)};
  };

  switch (hit.type) {
    case SelectedEntity::Type::LineSeg: {
      const size_t i = static_cast<size_t>(hit.index) * 6;
      if (i + 5 >= st.userLinesFlat.size())
        return false;
      const ray3d::Vec3 a = storageToWorld(st.userLinesFlat[i], st.userLinesFlat[i + 1], st.userLinesFlat[i + 2]);
      const ray3d::Vec3 b =
          storageToWorld(st.userLinesFlat[i + 3], st.userLinesFlat[i + 4], st.userLinesFlat[i + 5]);
      // The endpoint nearest the pick becomes the origin and +X runs toward the other, so clicking
      // near either end gives a frame that reads along the line away from you.
      const bool nearA = ray3d::Length(ray3d::Sub(pickWorld, a)) <= ray3d::Length(ray3d::Sub(pickWorld, b));
      const ray3d::Vec3 o = nearA ? a : b;
      const ray3d::Vec3 f = nearA ? b : a;
      if (!UcsAlignedToDirection(o, ray3d::Sub(f, o), out)) {
        log.push_back("UCS Object - that line has no length to align to.");
        return false;
      }
      return true;
    }
    case SelectedEntity::Type::Circle: {
      const size_t i = static_cast<size_t>(hit.index) * 4;
      if (i + 3 >= st.userCirclesCxCyZR.size())
        return false;
      const ray3d::Vec3 c =
          storageToWorld(st.userCirclesCxCyZR[i], st.userCirclesCxCyZR[i + 1], st.userCirclesCxCyZR[i + 2]);
      // AutoCAD's rule: origin at the centre, +X from the centre out through the pick point.
      if (!UcsAlignedToDirection(c, ray3d::Sub(pickWorld, c), out)) {
        log.push_back("UCS Object - click away from the centre so the X axis has a direction.");
        return false;
      }
      return true;
    }
    case SelectedEntity::Type::Arc: {
      if (hit.index < 0 || static_cast<size_t>(hit.index) >= st.userArcs.size())
        return false;
      const CadArc& a = st.userArcs[static_cast<size_t>(hit.index)];
      const ray3d::Vec3 c = storageToWorld(a.cx, a.cy, a.z);
      if (!UcsAlignedToDirection(c, ray3d::Sub(pickWorld, c), out)) {
        log.push_back("UCS Object - click away from the centre so the X axis has a direction.");
        return false;
      }
      return true;
    }
    case SelectedEntity::Type::Ellipse: {
      if (hit.index < 0 || static_cast<size_t>(hit.index) >= st.userEllipses.size())
        return false;
      const CadEllipse& e = st.userEllipses[static_cast<size_t>(hit.index)];
      const ray3d::Vec3 c = storageToWorld(e.cx, e.cy, e.z);
      // The ellipse's own major axis beats the pick direction here: it is an intrinsic property of
      // the entity, so the resulting frame does not depend on where the user happened to click.
      if (!UcsAlignedToDirection(c, ray3d::Vec3{static_cast<double>(e.majVx), static_cast<double>(e.majVy), 0.0},
                                 out)) {
        log.push_back("UCS Object - that ellipse has no major axis to align to.");
        return false;
      }
      return true;
    }
    case SelectedEntity::Type::Annotation: {
      if (hit.index < 0 || static_cast<size_t>(hit.index) >= st.cadAnnotations.size())
        return false;
      const CadAnnotation& an = st.cadAnnotations[static_cast<size_t>(hit.index)];
      const ray3d::Vec3 o = storageToWorld(an.insX, an.insY, an.insZ);
      const double rad = static_cast<double>(an.rotationRad);
      if (!UcsAlignedToDirection(o, ray3d::Vec3{std::cos(rad), std::sin(rad), 0.0}, out)) {
        log.push_back("UCS Object - that text has no baseline direction to align to.");
        return false;
      }
      return true;
    }
    default:
      // Meshes, surfaces, feature lines, polylines and hatch fills. A mesh or a solid WOULD be the
      // most useful Object target of all - "align to this face" is the 3D modelling workflow the
      // issue names - but it needs face-level picking, which does not exist: PickClosestCadEntity
      // resolves a mesh as one object with no face identity. Refused with a reason, not guessed at.
      log.push_back("UCS Object - alignment to that object type is not supported yet. Faces of meshes "
                    "and solids need face-level picking, which this build does not have.");
      return false;
  }
}

// ------------------------------------------------------------------------------------------------
// The UCS command itself.
// ------------------------------------------------------------------------------------------------

static const char* kUcsPrompt = "Specify origin of UCS or [Named/Previous/View/World/X/Y/Z/ZAxis/Object] <World>:";

void StartUcsCommand(AppCommandState& st, std::vector<std::string>& log) {
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  st.active = AppCommandState::Kind::Ucs;
  st.ucsPhase = AppCommandState::UcsPhase::WaitOriginOrOption;
  log.push_back(kUcsPrompt);
}

void StartPlanCommand(AppCommandState& st, std::vector<std::string>& log) {
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  st.active = AppCommandState::Kind::Plan;
  st.planPhase = AppCommandState::PlanPhase::WaitOption;
  log.push_back("PLAN - enter an option [Current/Ucs/World] <Current>:");
}

static void EndUcsCommand(AppCommandState& st) {
  st.ucsPhase = AppCommandState::UcsPhase::Idle;
  st.active = AppCommandState::Kind::None;
}

// Resolve a point typed at a UCS prompt. Interpreted in the CURRENT UCS, as AutoCAD does - the new
// frame does not exist yet, so the only frame the numbers can mean anything in is the active one.
//
// **X,Y,Z is accepted here, not just X,Y**, and that is not a convenience: without a third
// component every typed point lies in the current UCS's XY plane, so the three-point and ZAxis
// options could only ever produce frames rotated about the current Z. A tilted UCS - the thing the
// whole 3D half of this feature exists for - would be reachable by mouse only. Z defaults to 0
// (the work plane) when omitted, which is what every 2D entry means.
/// The point a UCS prompt's polar form names: `@<distance><<angle>`, e.g. `@33.2311<17`.
///
/// Relative to \p base — the origin already picked for this frame — with the angle measured in the
/// current UCS's XY plane from its +X, the same reference \ref ucs::AngleInRotationPlaneDeg uses so
/// the typed form and the two-point form cannot disagree.
///
/// This is the syntax the cursor's distance/angle boxes assemble, so what a user picks with the
/// mouse is exactly what they could have typed — and it is what the command log then shows them,
/// which is how the two stay learnable from each other.
static bool ParseUcsPolarPoint(const AppCommandState& st, const std::string& raw, const ray3d::Vec3& base,
                               ray3d::Vec3* out) {
  std::string s = StringUtil::trimCopy(raw);
  if (s.size() < 2 || s[0] != '@')
    return false;
  const size_t lt = s.find('<');
  if (lt == std::string::npos)
    return false;
  const std::string distStr = StringUtil::trimCopy(s.substr(1, lt - 1));
  const std::string angStr = StringUtil::trimCopy(s.substr(lt + 1));
  if (distStr.empty() || angStr.empty())
    return false;
  double dist = 0., angDeg = 0.;
  {
    std::istringstream di(distStr);
    if (!(di >> dist) || !(di >> std::ws).eof())
      return false;
    std::istringstream ai(angStr);
    if (!(ai >> angDeg) || !(ai >> std::ws).eof())
      return false;
  }
  if (!std::isfinite(dist) || !std::isfinite(angDeg))
    return false;
  constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
  // Built in the UCS's own XY plane and then lifted to world, so a polar point on a tilted frame
  // lands on that frame's plane rather than on the world's.
  const ray3d::Vec3 baseLocal = ucs::WorldToUcs(st.activeUcs, base);
  const ray3d::Vec3 p = ucs::UcsToWorld(st.activeUcs, {baseLocal.x + dist * std::cos(angDeg * kDegToRad),
                                                       baseLocal.y + dist * std::sin(angDeg * kDegToRad),
                                                       baseLocal.z});
  if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
    return false;
  *out = p;
  return true;
}

static bool ParseUcsPromptPoint(const AppCommandState& st, const std::string& raw, ray3d::Vec3* out) {
  std::string s = StringUtil::trimCopy(raw);
  if (s.empty())
    return false;
  for (char& c : s) {
    if (c == ',')
      c = ' ';
  }
  std::istringstream iss(s);
  double a = 0.;
  double b = 0.;
  if (!(iss >> a) || !(iss >> b))
    return false;
  double c = 0.;
  if (!(iss >> c))
    c = 0.;  // no Z given: the point lies on the current work plane
  else if (!(iss >> std::ws).eof())
    return false;  // a fourth number is a typo, not a coordinate (REQ-201)
  if (!std::isfinite(a) || !std::isfinite(b) || !std::isfinite(c))
    return false;
  const ray3d::Vec3 p = ucs::UcsToWorld(st.activeUcs, {a, b, c});
  if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
    return false;
  *out = p;
  return true;
}

/// Apply a rotation of \p deg about \ref AppCommandState::ucsRotationAxis and close the command.
///
/// One place, because the angle now arrives by two routes — typed, or measured from two picks — and
/// a second copy of this three-way branch is how the two would eventually disagree about what `Y`
/// means.
static void ApplyUcsRotation(AppCommandState& st, double deg, std::vector<std::string>& log) {
  const ucs::Ucs next = (st.ucsRotationAxis == 'X')   ? ucs::RotatedAboutX(st.activeUcs, deg)
                        : (st.ucsRotationAxis == 'Y') ? ucs::RotatedAboutY(st.activeUcs, deg)
                                                      : ucs::RotatedAboutZ(st.activeUcs, deg);
  SetActiveUcs(st, next, log);
  EndUcsCommand(st);
}

/// Finish `UCS <axis>` + `2P`: measure the angle from \ref AppCommandState::ucsAngleBasePoint to
/// \p p2 in the rotation's own plane, and apply it.
///
/// Reports the angle it derived. The user picked two points and never saw a number, so echoing the
/// one that was used is what lets them notice a mis-pick — and it is the number they would type to
/// repeat the frame later.
static void CommitUcsRotationFromTwoPoints(AppCommandState& st, const ray3d::Vec3& p2,
                                           std::vector<std::string>& log) {
  const ray3d::Vec3 dir = ray3d::Sub(p2, st.ucsAngleBasePoint);
  double deg = 0.0;
  if (!ucs::AngleInRotationPlaneDeg(st.activeUcs, st.ucsRotationAxis, dir, &deg)) {
    // Either the two picks coincide, or the direction lies out of the plane being rotated — two
    // points straight up define no rotation about Z. Stay at the prompt so the second pick can be
    // retaken rather than losing the whole command (REQ-201).
    log.push_back(std::string("UCS - those two points define no angle in the ") + st.ucsRotationAxis +
                  " rotation plane. Pick the second point again, or ESC.");
    st.ucsPhase = AppCommandState::UcsPhase::WaitRotationAngleP2;
    return;
  }
  char buf[96];
  std::snprintf(buf, sizeof(buf), "UCS - angle from the two points: %.4f degrees.", deg);
  log.push_back(buf);
  ApplyUcsRotation(st, deg, log);
}

// The three named-UCS operations. Split apart deliberately: `UCS Named` now reaches only the first
// of them - it asks for a name and saves - while restore and delete are the View Manager's, which
// lists the saved frames so a user picks from what they can see rather than recalling a name. One
// function each keeps that split honest: the dialog and the command line call the same code.
//
// The name check they share: blank is refused, and "World" is reserved. World is always available
// and can be neither redefined nor deleted, which is what makes `UCS World` a guaranteed way back
// to a known frame.
static bool ValidUcsName(const std::string& rawName, const char* what, std::string* out,
                         std::vector<std::string>& log) {
  *out = StringUtil::trimCopy(rawName);
  if (out->empty()) {
    log.push_back(std::string("UCS ") + what + " - enter a name.");
    return false;
  }
  if (StringUtil::toLowerAsciiCopy(*out) == "world") {
    log.push_back(std::string("UCS ") + what + " - \"World\" is reserved and cannot be saved over or deleted.");
    return false;
  }
  return true;
}

void SaveNamedUcs(AppCommandState& st, const std::string& rawName, std::vector<std::string>& log) {
  std::string name;
  if (!ValidUcsName(rawName, "Named", &name, log))
    return;
  for (NamedUcs& n : st.ucsNamed) {
    if (StringUtil::toLowerAsciiCopy(n.name) == StringUtil::toLowerAsciiCopy(name)) {
      n.frame = st.activeUcs;  // redefining an existing name, which AutoCAD allows
      log.push_back("UCS " + n.name + " redefined.");
      return;
    }
  }
  NamedUcs n;
  n.name = name;
  n.frame = st.activeUcs;
  st.ucsNamed.push_back(std::move(n));
  log.push_back("UCS saved as " + name + ".");
}

bool RestoreNamedUcs(AppCommandState& st, const std::string& rawName, std::vector<std::string>& log) {
  std::string name;
  if (!ValidUcsName(rawName, "Restore", &name, log))
    return false;
  const NamedUcs* found = FindNamedUcs(st, name);
  if (!found) {
    log.push_back("UCS Restore - there is no saved UCS named " + name + ".");
    return false;
  }
  SetActiveUcs(st, found->frame, log);
  return true;
}

bool DeleteNamedUcs(AppCommandState& st, const std::string& rawName, std::vector<std::string>& log) {
  std::string name;
  if (!ValidUcsName(rawName, "Delete", &name, log))
    return false;
  for (size_t i = 0; i < st.ucsNamed.size(); ++i) {
    if (StringUtil::toLowerAsciiCopy(st.ucsNamed[i].name) == StringUtil::toLowerAsciiCopy(name)) {
      log.push_back("UCS " + st.ucsNamed[i].name + " deleted.");
      st.ucsNamed.erase(st.ucsNamed.begin() + static_cast<std::ptrdiff_t>(i));
      return true;
    }
  }
  log.push_back("UCS Delete - there is no saved UCS named " + name + ".");
  return false;
}

void ListNamedUcs(const AppCommandState& st, std::vector<std::string>& log) {
  log.push_back("Named coordinate systems:");
  log.push_back("  World (current)" + std::string(ucs::IsWorld(st.activeUcs) ? "" : ""));
  if (st.ucsNamed.empty()) {
    log.push_back("  (no saved UCS definitions)");
    return;
  }
  for (const NamedUcs& n : st.ucsNamed)
    log.push_back("  " + n.name + " - " + DescribeUcs(n.frame));
}

bool ProcessUcsCommandLine(AppCommandState& st, const std::string& line, std::vector<std::string>& log) {
  if (st.active != AppCommandState::Kind::Ucs)
    return false;
  const std::string in = StringUtil::trimCopy(line);
  const std::string low = StringUtil::toLowerAsciiCopy(in);

  switch (st.ucsPhase) {
    case AppCommandState::UcsPhase::WaitOriginOrOption: {
      if (in.empty() || low == "w" || low == "world") {  // bare Enter takes the <World> default
        ApplyUcsWorld(st, log);
        EndUcsCommand(st);
        return true;
      }
      if (low == "p" || low == "prev" || low == "previous") {
        if (st.ucsPrevious.empty()) {
          log.push_back("UCS Previous - no previous coordinate system to go back to.");
          EndUcsCommand(st);
          return true;
        }
        const ucs::Ucs prev = st.ucsPrevious.back();
        st.ucsPrevious.pop_back();
        // pushPrevious = false: restoring a previous UCS must not itself become a history entry, or
        // Previous would alternate between two frames forever instead of walking back.
        SetActiveUcs(st, prev, log, false);
        EndUcsCommand(st);
        return true;
      }
      if (low == "v" || low == "view") {
        // The UCS XY plane becomes parallel to the screen, Z toward the viewer. Built from the live
        // camera basis so it matches exactly what the user is looking at.
        const Camera cam = CadViewCamera(st);
        ucs::Ucs next;
        if (!ucs::FromBasis(st.activeUcs.origin, cam.RightWorld(), cam.UpWorld(), &next)) {
          log.push_back("UCS View - the current view does not define a usable plane.");
          EndUcsCommand(st);
          return true;
        }
        SetActiveUcs(st, next, log);
        EndUcsCommand(st);
        return true;
      }
      if (low == "x" || low == "y" || low == "z") {
        st.ucsRotationAxis = static_cast<char>(low[0] - 32);  // 'x' -> 'X'
        st.ucsPhase = AppCommandState::UcsPhase::WaitRotationAngle;
        log.push_back(std::string("Specify rotation angle about ") + st.ucsRotationAxis +
                      " axis <0>: or [2P] to pick two points  (positive follows the right-hand rule)");
        return true;
      }
      if (low == "za" || low == "zaxis") {
        st.ucsPhase = AppCommandState::UcsPhase::WaitZAxisOrigin;
        log.push_back("Specify new origin point <0,0,0>:");
        return true;
      }
      if (low == "ob" || low == "object") {
        st.ucsPhase = AppCommandState::UcsPhase::WaitObjectPick;
        log.push_back("Select object to align UCS with:  (click a line, polyline, arc, circle, ellipse or text)");
        return true;
      }
      if (low == "n" || low == "named") {
        // Straight to the name. Named's only job here is to keep the frame you have just built, so
        // a Save/Restore/Delete question in front of it asked something the user had already
        // answered by getting this far. Restoring and deleting are the View Manager's, where the
        // saved frames are listed - see SaveNamedUcs in the header.
        st.ucsPhase = AppCommandState::UcsPhase::WaitNamedName;
        log.push_back("Enter name to save current UCS as:");
        return true;
      }
      if (low == "?") {
        ListNamedUcs(st, log);
        EndUcsCommand(st);
        return true;
      }
      ray3d::Vec3 origin;
      if (ParseUcsPromptPoint(st, in, &origin)) {
        st.ucsPendingOrigin = origin;
        st.ucsPhase = AppCommandState::UcsPhase::WaitXAxisPoint;
        log.push_back("Specify point on positive portion of X axis <accept origin only>:");
        return true;
      }
      log.push_back(std::string("UCS - unrecognised option. ") + kUcsPrompt);
      return true;
    }

    case AppCommandState::UcsPhase::WaitXAxisPoint: {
      if (in.empty()) {
        // Origin only: keep the current orientation, which is AutoCAD's single-point behaviour.
        SetActiveUcs(st, ucs::WithOrigin(st.activeUcs, st.ucsPendingOrigin), log);
        EndUcsCommand(st);
        return true;
      }
      ray3d::Vec3 onX;
      // `@33.2311<17` - distance and angle from the origin just picked, the form the cursor's
      // distance/angle boxes assemble. Tried first because it is unambiguous: a leading '@' with a
      // '<' cannot be a coordinate pair.
      if (!ParseUcsPolarPoint(st, in, st.ucsPendingOrigin, &onX) && !ParseUcsPromptPoint(st, in, &onX)) {
        log.push_back("UCS - enter a point or @distance<angle, or press Enter to accept the origin alone.");
        return true;
      }
      st.ucsPendingXAxisPoint = onX;
      st.ucsPhase = AppCommandState::UcsPhase::WaitXyPoint;
      log.push_back("Specify point on positive-Y portion of the UCS XY plane <accept X axis only>:");
      return true;
    }

    case AppCommandState::UcsPhase::WaitXyPoint: {
      ucs::Ucs next;
      if (in.empty()) {
        // Origin + X only: the Z axis stays as close to the current frame's as it can, which is
        // what "rotate the frame to this direction" should mean.
        if (!UcsAlignedToDirection(st.ucsPendingOrigin, ray3d::Sub(st.ucsPendingXAxisPoint, st.ucsPendingOrigin),
                                   &next)) {
          log.push_back("UCS - the X-axis point coincides with the origin; no direction is defined.");
          EndUcsCommand(st);
          return true;
        }
        SetActiveUcs(st, next, log);
        EndUcsCommand(st);
        return true;
      }
      ray3d::Vec3 onXy;
      // Polar accepted here too, measured from the same origin as the X-axis step so both boxes
      // read against one reference rather than the second silently changing it.
      if (!ParseUcsPolarPoint(st, in, st.ucsPendingOrigin, &onXy) && !ParseUcsPromptPoint(st, in, &onXy)) {
        log.push_back("UCS - enter a point or @distance<angle, or press Enter to accept the X axis alone.");
        return true;
      }
      if (!ucs::FromThreePoints(st.ucsPendingOrigin, st.ucsPendingXAxisPoint, onXy, &next)) {
        log.push_back("UCS - those three points are collinear, so they define no plane. Pick again.");
        st.ucsPhase = AppCommandState::UcsPhase::WaitXyPoint;
        return true;
      }
      SetActiveUcs(st, next, log);
      EndUcsCommand(st);
      return true;
    }

    case AppCommandState::UcsPhase::WaitRotationAngle: {
      // `2P` — take the angle from two picked points instead of a typed number. A surveyor working
      // to a lot line or a building face knows the LINE, not its bearing; making them read a bearing
      // off the drawing and type it back in is arithmetic the app can do exactly and they can only
      // do approximately. Same keyword LINE and POLYLINE already use for "define this direction by
      // picking", so it is one convention rather than a second one.
      if (low == "2p" || low == "2") {
        st.ucsPhase = AppCommandState::UcsPhase::WaitRotationAngleP1;
        log.push_back("Specify first point of the angle:  (or ESC to cancel)");
        return true;
      }
      double deg = 0.0;
      if (!in.empty()) {
        std::istringstream iss(in);
        if (!(iss >> deg) || !(iss >> std::ws).eof()) {
          log.push_back("UCS - enter a rotation angle in degrees, or 2P to pick two points "
                        "(blank Enter for 0).");
          return true;
        }
      }
      if (!std::isfinite(deg)) {
        log.push_back("UCS - the rotation angle must be a finite number.");
        return true;
      }
      ApplyUcsRotation(st, deg, log);
      return true;
    }

    case AppCommandState::UcsPhase::WaitRotationAngleP1: {
      ray3d::Vec3 p1;
      if (!ParseUcsPromptPoint(st, in, &p1)) {
        log.push_back("UCS - enter the first point of the angle (click, or type X,Y / X,Y,Z).");
        return true;
      }
      st.ucsAngleBasePoint = p1;
      st.ucsPhase = AppCommandState::UcsPhase::WaitRotationAngleP2;
      log.push_back("Specify second point of the angle:");
      return true;
    }

    case AppCommandState::UcsPhase::WaitRotationAngleP2: {
      ray3d::Vec3 p2;
      if (!ParseUcsPromptPoint(st, in, &p2)) {
        log.push_back("UCS - enter the second point of the angle (click, or type X,Y / X,Y,Z).");
        return true;
      }
      CommitUcsRotationFromTwoPoints(st, p2, log);
      return true;
    }

    case AppCommandState::UcsPhase::WaitZAxisOrigin: {
      ray3d::Vec3 origin{0.0, 0.0, 0.0};
      if (!in.empty() && !ParseUcsPromptPoint(st, in, &origin)) {
        log.push_back("UCS ZAxis - enter an origin point (blank Enter for the current UCS origin).");
        return true;
      }
      if (in.empty())
        origin = st.activeUcs.origin;
      st.ucsPendingOrigin = origin;
      st.ucsPhase = AppCommandState::UcsPhase::WaitZAxisPoint;
      log.push_back("Specify point on positive portion of Z axis:");
      return true;
    }

    case AppCommandState::UcsPhase::WaitZAxisPoint: {
      ray3d::Vec3 onZ;
      if (!ParseUcsPromptPoint(st, in, &onZ)) {
        log.push_back("UCS ZAxis - enter a point on the positive Z axis.");
        return true;
      }
      ucs::Ucs next;
      if (!ucs::FromZAxis(st.ucsPendingOrigin, onZ, &next)) {
        log.push_back("UCS ZAxis - that point coincides with the origin, so no Z direction is defined.");
        return true;
      }
      SetActiveUcs(st, next, log);
      EndUcsCommand(st);
      return true;
    }

    case AppCommandState::UcsPhase::WaitObjectPick: {
      log.push_back("UCS Object - click an object in the drawing, or press Esc to cancel.");
      return true;
    }

    case AppCommandState::UcsPhase::WaitNamedName: {
      // `?` still lists, so the one keyboard shortcut for "what is already saved?" works at this
      // prompt too - and it cannot be a name, since a blank-or-World check would reject it anyway.
      if (low == "?") {
        ListNamedUcs(st, log);
        EndUcsCommand(st);
        return true;
      }
      SaveNamedUcs(st, in, log);
      EndUcsCommand(st);
      return true;
    }

    default:
      return false;
  }
}

bool ProcessUcsViewportPick(AppCommandState& st, const ray3d::Vec3& worldPoint, std::vector<std::string>& log) {
  if (st.active != AppCommandState::Kind::Ucs)
    return false;
  switch (st.ucsPhase) {
    case AppCommandState::UcsPhase::WaitOriginOrOption:
      st.ucsPendingOrigin = worldPoint;
      st.ucsPhase = AppCommandState::UcsPhase::WaitXAxisPoint;
      log.push_back("Specify point on positive portion of X axis <accept origin only>:");
      return true;
    case AppCommandState::UcsPhase::WaitXAxisPoint:
      st.ucsPendingXAxisPoint = worldPoint;
      st.ucsPhase = AppCommandState::UcsPhase::WaitXyPoint;
      log.push_back("Specify point on positive-Y portion of the UCS XY plane <accept X axis only>:");
      return true;
    case AppCommandState::UcsPhase::WaitXyPoint: {
      ucs::Ucs next;
      if (!ucs::FromThreePoints(st.ucsPendingOrigin, st.ucsPendingXAxisPoint, worldPoint, &next)) {
        log.push_back("UCS - those three points are collinear, so they define no plane. Pick again.");
        return true;
      }
      SetActiveUcs(st, next, log);
      EndUcsCommand(st);
      return true;
    }
    // The two picks that define a rotation angle. Clicking is the point of the option - a surveyor
    // picks the ends of a lot line rather than typing its bearing - so these route exactly as the
    // typed forms do, through the same commit helper.
    case AppCommandState::UcsPhase::WaitRotationAngleP1:
      st.ucsAngleBasePoint = worldPoint;
      st.ucsPhase = AppCommandState::UcsPhase::WaitRotationAngleP2;
      log.push_back("Specify second point of the angle:");
      return true;
    case AppCommandState::UcsPhase::WaitRotationAngleP2:
      CommitUcsRotationFromTwoPoints(st, worldPoint, log);
      return true;
    case AppCommandState::UcsPhase::WaitZAxisOrigin:
      st.ucsPendingOrigin = worldPoint;
      st.ucsPhase = AppCommandState::UcsPhase::WaitZAxisPoint;
      log.push_back("Specify point on positive portion of Z axis:");
      return true;
    case AppCommandState::UcsPhase::WaitZAxisPoint: {
      ucs::Ucs next;
      if (!ucs::FromZAxis(st.ucsPendingOrigin, worldPoint, &next)) {
        log.push_back("UCS ZAxis - that point coincides with the origin, so no Z direction is defined.");
        return true;
      }
      SetActiveUcs(st, next, log);
      EndUcsCommand(st);
      return true;
    }
    case AppCommandState::UcsPhase::WaitObjectPick: {
      ucs::Ucs next;
      if (!UcsFromObjectPick(st, worldPoint, log, &next))
        return true;  // stay in the pick phase so the user can try another object
      SetActiveUcs(st, next, log);
      EndUcsCommand(st);
      return true;
    }
    default:
      return false;
  }
}

bool ProcessPlanCommandLine(AppCommandState& st, const std::string& line, std::vector<std::string>& log) {
  if (st.active != AppCommandState::Kind::Plan)
    return false;
  const std::string in = StringUtil::trimCopy(line);
  const std::string low = StringUtil::toLowerAsciiCopy(in);

  if (st.planPhase == AppCommandState::PlanPhase::WaitNamedName) {
    const NamedUcs* found = FindNamedUcs(st, in);
    if (!found) {
      log.push_back("PLAN - there is no saved UCS named " + in + ".");
      st.planPhase = AppCommandState::PlanPhase::Idle;
      st.active = AppCommandState::Kind::None;
      return true;
    }
    // The named frame orients the CAMERA. The active UCS is deliberately left alone - Autodesk
    // documents that distinction, and it is the entire reason PLAN is not just another UCS option.
    ApplyPlanViewOf(st, found->frame, log);
    log.push_back("PLAN - view set to the XY plane of UCS " + found->name + ". The current UCS is unchanged.");
    st.planPhase = AppCommandState::PlanPhase::Idle;
    st.active = AppCommandState::Kind::None;
    return true;
  }

  if (in.empty() || low == "c" || low == "current" || low == "cu") {
    ApplyPlanViewOf(st, st.activeUcs, log);
    log.push_back("PLAN - view set to the current UCS's XY plane. The UCS itself is unchanged.");
  } else if (low == "w" || low == "world") {
    ApplyPlanViewOf(st, ucs::Ucs{}, log);
    log.push_back("PLAN - view set to the world XY plane. The current UCS is unchanged.");
  } else if (low == "u" || low == "ucs" || low == "named") {
    if (st.ucsNamed.empty()) {
      log.push_back("PLAN - there are no saved UCS definitions. Save one with UCS Named Save first.");
    } else {
      st.planPhase = AppCommandState::PlanPhase::WaitNamedName;
      log.push_back("Enter name of UCS:");
      return true;
    }
  } else {
    log.push_back("PLAN - enter Current, Ucs or World.");
    return true;
  }
  st.planPhase = AppCommandState::PlanPhase::Idle;
  st.active = AppCommandState::Kind::None;
  return true;
}

// Reset the coordinate system to the WCS. Kept separate from ApplyElevValue so the status readout
// and the "W" option have one shared meaning of "world".
//
// The WCS is immutable by definition, so this is a plain assignment of a default-constructed frame
// rather than a field-by-field reset — there is no sequence of edits that can leave it half-world.
void ApplyUcsWorld(AppCommandState& st, std::vector<std::string>& log) {
  SetActiveUcs(st, ucs::Ucs{}, log);
}
