#include "CadCommandsInternal.hpp"
#include "input/CadInputInternal.hpp"
#include "CadCoordinateFrame.hpp"
#include "OrthoConstrain.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

double MathAngleRadFromBearingCwNorthDegD(double bearingDegClockwiseFromNorth) {
  constexpr double kDegToRad = 0.01745329251994329577;
  const double br = bearingDegClockwiseFromNorth * kDegToRad;
  return std::atan2(std::cos(br), std::sin(br));
}

float MathAngleRadFromBearingCwNorthDeg(float bearingDegClockwiseFromNorth) {
  return static_cast<float>(MathAngleRadFromBearingCwNorthDegD(static_cast<double>(bearingDegClockwiseFromNorth)));
}

float BearingCwNorthDegFromMathAngleRad(float mathAngleRadFromEastCcw) {
  const double deg =
      std::atan2(static_cast<double>(std::cos(mathAngleRadFromEastCcw)),
                 static_cast<double>(std::sin(mathAngleRadFromEastCcw))) *
      (180.0 / 3.14159265358979323846);
  double out = deg;
  if (out < 0.0)
    out += 360.0;
  return static_cast<float>(out);
}


bool ParseAngleDegrees(const std::string& raw, float* degreesOut) {
  return ParseAngleDegreesInternal(raw, degreesOut);
}

// Parse a typed world point in DOUBLE (REQ-101). This is the real implementation; the float overload
// below narrows its result.
//
// The precision this preserves is not academic. A typed easting is a decimal string, and narrowing it
// to `float` before the document origin has been subtracted quantizes it at the magnitude of the
// WORLD value: at easting 2e6 the float spacing is 0.25 ft, so `2000000.10` became `2000000.125` —
// 0.025 ft of error introduced by the parse itself, before any commit, save or load, and REQ-101
// allows 0.01 ft. Subtracting the origin first and narrowing afterwards quantizes at the magnitude of
// the LOCAL value instead, which is small by construction, so the same input lands within ~1e-4 ft.
// Nothing downstream can recover what the old order threw away.
bool ParseWorldPointD(const std::string& raw, double* ox, double* oy, bool allowRelative, double baseX,
                      double baseY) {
  if (!ox || !oy)
    return false;
  std::string s = StringUtil::trimCopy(raw);
  if (s.empty())
    return false;
  if (!s.empty() && s[0] == '@') {
    if (!allowRelative)
      return false;
    s = StringUtil::trimCopy(s.substr(1));
    double dx = 0.;
    double dy = 0.;
    if (!ParseTwoDoubles(s, &dx, &dy))
      return false;
    *ox = baseX + dx;
    *oy = baseY + dy;
    // The sum can overflow even though the base and the delta are each representable, and this is the
    // ONE path into a coordinate that is non-finite from finite input. An absolute coordinate that
    // overflows is already refused below — the stream extraction sets failbit — so re-checking here
    // restores this function's own existing guarantee rather than adding a new rule. Every caller
    // already reports a false return as a parse failure, which is what satisfies REQ-201.
    // Found while fixing issue #59; reproduced for LINE, POLYLINE and RECT.
    if (!std::isfinite(*ox) || !std::isfinite(*oy))
      return false;
    return true;
  }
  return ParseTwoDoubles(s, ox, oy);
}

bool ParseWorldPoint(const std::string& raw, float* ox, float* oy, bool allowRelative, float baseX, float baseY) {
  if (!ox || !oy)
    return false;
  double wx = 0.;
  double wy = 0.;
  if (!ParseWorldPointD(raw, &wx, &wy, allowRelative, static_cast<double>(baseX), static_cast<double>(baseY)))
    return false;
  // Narrowing can overflow to infinity where the double did not, so the finiteness guarantee has to be
  // re-checked at the narrower type rather than inherited from the call above.
  const float fx = static_cast<float>(wx);
  const float fy = static_cast<float>(wy);
  if (!std::isfinite(fx) || !std::isfinite(fy))
    return false;
  *ox = fx;
  *oy = fy;
  return true;
}

// Split a typed point into its two numbers plus whether it was relative, WITHOUT deciding what
// frame they are in. That decision belongs to the caller, and separating it is what lets the same
// text mean a world pair under the WCS and a UCS pair under a UCS (REQ-154) with one parser.
bool ParsePointComponents(const std::string& raw, double* a, double* b, bool* isRelative, bool allowRelative) {
  if (!a || !b || !isRelative)
    return false;
  std::string s = StringUtil::trimCopy(raw);
  if (s.empty())
    return false;
  *isRelative = false;
  if (s[0] == '@') {
    if (!allowRelative)
      return false;
    *isRelative = true;
    s = StringUtil::trimCopy(s.substr(1));
  }
  return ParseTwoDoubles(s, a, b);
}

// A typed point's optional third coordinate (REQ-354 / D-2026-09-28-j): `x,y,z` or `@dx,dy,dz` splits
// into the two-number text the parsers below read, plus the Z. False for a malformed Z or for four or
// more numbers — which the shared 2D parse would otherwise read as their first two, silently.
static bool PeelOptionalPointZ(const std::string& raw, std::string* outXy, double* outZ, bool* outHasZ) {
  const std::string trimmed = StringUtil::trimCopy(raw);
  *outXy = trimmed;
  *outHasZ = false;
  const size_t c1 = trimmed.find(',');
  const size_t c2 = (c1 == std::string::npos) ? std::string::npos : trimmed.find(',', c1 + 1);
  if (c2 == std::string::npos)
    return true;
  if (trimmed.find(',', c2 + 1) != std::string::npos)
    return false;
  const std::string zText = StringUtil::trimCopy(trimmed.substr(c2 + 1));
  char* zEnd = nullptr;
  const double zv = std::strtod(zText.c_str(), &zEnd);
  if (zText.empty() || !zEnd || *zEnd != '\0' || !std::isfinite(zv))
    return false;
  *outZ = zv;
  *outHasZ = true;
  *outXy = trimmed.substr(0, c2);
  return true;
}

// The UCS-aware point parse (REQ-154). Also reports the resolved point's WORLD Z, which a tilted
// work plane makes vary from point to point — see AppCommandState::resolvedPointZ.
//
// Under the WCS this takes the original code path unchanged, deliberately: every existing drawing,
// transcript and test goes through that branch, so the UCS work cannot perturb them even by a
// rounding step.
//
// A typed Z (REQ-354 / D-2026-09-28-j) is read at every prompt that comes through here: `x,y,z` puts
// the point at elevation z (a UCS Z under a UCS), and `@dx,dy,dz` lifts it dz from the base — the
// caller's \p baseWorldZ when it has one (LINE's anchor), the work plane otherwise. The Z reaches
// the ~29 commit sites the way a tilted plane's already does, through resolvedPointZ, and it beats a
// leftover mouse snap: the user just typed it.
// The double-output core (issue #1668). Dimension points are stored in double, and a float local
// coordinate near 5,000 ft is only good to ~0.0005 ft — a minute of arc over a 1 ft ray.
static bool ParseStoragePointCore(AppCommandState& st, const std::string& raw, double* lx, double* ly,
                                  double* outWorldZ, bool allowRelative, double baseLocalX, double baseLocalY,
                                  const float* baseWorldZ, bool requireFloatOutput) {
  if (!lx || !ly)
    return false;
  // A Z typed for an EARLIER point describes that point, not this one — including as the base of
  // an `@dx,dy,dz` below. The GUI clears it when it re-publishes the cursor each frame; the headless
  // driver has no frames, so it is dropped here, and the work plane answers as it would have.
  if (st.resolvedPointZTyped) {
    st.resolvedPointZValid = false;
    st.resolvedPointZTyped = false;
  }
  {
    float sx = 0.f, sy = 0.f;  // station snaps resolve in float; they land on alignment geometry
    if (TryParseAlignmentStationSnapStoragePoint(st, raw, &sx, &sy, outWorldZ, allowRelative,
                                                 static_cast<float>(baseLocalX), static_cast<float>(baseLocalY))) {
      *lx = sx;
      *ly = sy;
      return true;
    }
  }
  if (!st.alignmentStationSnapRejectReason.empty())
    return false;
  std::string xy;
  double typedZ = 0.0;
  bool hasZ = false;
  if (!PeelOptionalPointZ(raw, &xy, &typedZ, &hasZ))
    return false;
  const bool relText = !xy.empty() && xy[0] == '@';
  const double baseZ =
      baseWorldZ ? static_cast<double>(*baseWorldZ) : static_cast<double>(CadWorkPlaneElevation(st));

  double baseWx = 0.0, baseWy = 0.0;
  CadCoord::WorldFromLocal(st, baseLocalX, baseLocalY, &baseWx, &baseWy);
  const auto validLocal = [requireFloatOutput](double value) {
    return std::isfinite(value) &&
           (!requireFloatOutput || std::abs(value) <= std::numeric_limits<float>::max());
  };

  if (!ucs::IsWorld(st.activeUcs)) {
    double a = 0.;
    double b = 0.;
    bool rel = false;
    if (!ParsePointComponents(xy, &a, &b, &rel, allowRelative))
      return false;
    ray3d::Vec3 world;
    if (rel) {
      // `@dx,dy` is a delta along the UCS axes, so it has to be added in UCS space rather than to
      // the world pair. The base keeps its own out-of-plane offset (the UCS Z component), which is
      // what makes a relative move from a snapped point stay where the user put it.
      const ray3d::Vec3 baseUcs = ucs::WorldToUcs(
          st.activeUcs,
          {baseWx, baseWy, hasZ ? baseZ : static_cast<double>(CadWorkPlaneElevation(st))});
      world = ucs::UcsToWorld(st.activeUcs, {baseUcs.x + a, baseUcs.y + b, baseUcs.z + (hasZ ? typedZ : 0.0)});
    } else {
      world = ucs::UcsToWorld(st.activeUcs, {a, b, hasZ ? typedZ : 0.0});
    }
    if (!std::isfinite(world.x) || !std::isfinite(world.y) || !std::isfinite(world.z))
      return false;
    CadCoord::LocalFromWorld(st, world.x, world.y, lx, ly);
    if (!validLocal(*lx) || !validLocal(*ly))
      return false;
    if (outWorldZ)
      *outWorldZ = world.z;
    // Publish the point's own elevation so the ~29 geometry-creation sites downstream commit AT it
    // (see resolvedPointZ). Only on the UCS branch: under the WCS the work plane is flat and the
    // origin's Z already describes every point on it, so leaving the channel untouched there keeps
    // the pre-UCS path exactly as it was.
    st.resolvedPointZValid = true;
    st.resolvedPointZ = static_cast<float>(world.z);
    st.resolvedPointZTyped = hasZ;
    if (hasZ)
      st.viewportSnapPickValid = false;
    return true;
  }

  // Parsed in double and narrowed by LocalFromWorld only AFTER the origin is subtracted — that
  // ordering is the whole point (REQ-101). The origin itself is established before dispatch, by
  // MaybeEstablishDocumentOriginFromTypedPoint in ProcessCommandLineSubmit, so by the time any
  // command's parse runs the frame can already represent what was typed.
  double wx = 0.;
  double wy = 0.;
  if (!ParseWorldPointD(xy, &wx, &wy, allowRelative, baseWx, baseWy))
    return false;
  CadCoord::LocalFromWorld(st, wx, wy, lx, ly);
  if (!validLocal(*lx) || !validLocal(*ly))
    return false;
  if (hasZ) {
    const double z = relText ? baseZ + typedZ : typedZ;
    if (!std::isfinite(z) || !std::isfinite(static_cast<float>(z)))
      return false;
    st.resolvedPointZValid = true;
    st.resolvedPointZ = static_cast<float>(z);
    st.resolvedPointZTyped = true;
    st.viewportSnapPickValid = false;
    if (outWorldZ)
      *outWorldZ = z;
    return true;
  }
  if (outWorldZ)
    *outWorldZ = static_cast<double>(CadWorkPlaneElevation(st));
  return true;
}

bool ParseStoragePointZ(AppCommandState& st, const std::string& raw, float* lx, float* ly, double* outWorldZ,
                        bool allowRelative, float baseLocalX, float baseLocalY, const float* baseWorldZ) {
  if (!lx || !ly)
    return false;
  double x = 0.0, y = 0.0;
  if (!ParseStoragePointCore(st, raw, &x, &y, outWorldZ, allowRelative, baseLocalX, baseLocalY, baseWorldZ, true))
    return false;
  *lx = static_cast<float>(x);
  *ly = static_cast<float>(y);
  return true;
}

bool ParseStoragePoint(AppCommandState& st, const std::string& raw, double* lx, double* ly, bool allowRelative,
                       double baseLocalX, double baseLocalY, const float* baseWorldZ) {
  return ParseStoragePointCore(st, raw, lx, ly, nullptr, allowRelative, baseLocalX, baseLocalY, baseWorldZ, false);
}

// The 38 existing call sites keep this two-coordinate signature: they ask "where is this point?",
// and the Z of the answer is published through AppCommandState rather than threaded through every
// one of them (see resolvedPointZ for why that is the seam).
bool ParseStoragePoint(AppCommandState& st, const std::string& raw, float* lx, float* ly, bool allowRelative,
                       float baseLocalX, float baseLocalY, const float* baseWorldZ) {
  return ParseStoragePointZ(st, raw, lx, ly, nullptr, allowRelative, baseLocalX, baseLocalY, baseWorldZ);
}

// Lift a storage (x,y) onto the active work plane by SOLVING the plane equation for Z. Only valid
// while the plane is close enough to horizontal that Z is a genuine function of (x,y) — it has no
// solution (and falls back to the plane's own origin Z, which is simply wrong) once the plane
// stands on edge to world Z, e.g. a Front/Left/Right-style UCS (issue #371). Kept only as the
// fallback for callers that cannot supply the point's real, already-resolved Z.
static ray3d::Vec3 ApproximateOnWorkPlaneFromXy(const ucs::Ucs& frame, double x, double y) {
  const ray3d::Vec3 n = frame.zAxis;
  const double z = (std::fabs(n.z) > 1e-9)
                       ? frame.origin.z - (n.x * (x - frame.origin.x) + n.y * (y - frame.origin.y)) / n.z
                       : frame.origin.z;
  return ray3d::Vec3{x, y, z};
}

// Shared core of ApplyPolarConstrainFromAnchor and ApplyPipeRunCompassFromAnchor: snap *wx/*wy (and,
// if resolvable, *wz) onto the nearest ray from anchor at a multiple of incrementDeg or one of
// extraAnglesDeg, measured in frame's XY plane from its +X. Callers decide their own gate (POLAR's
// st.polarMode, PIPERUN's st.pipeRunCompassOn) and pass the already-checked increment/extra angles.
static void SnapPointToAngleSet(const ucs::Ucs& frame, float anchorX, float anchorY, float* wx, float* wy,
                                float anchorZ, float targetZ, float* wz, double incrementDeg,
                                const std::vector<double>& extraAnglesDeg) {
  // Prefer the REAL, already-resolved elevation of each point (issue #371) over solving the plane
  // equation for Z, which only has a real answer while the plane is close to horizontal.
  const bool haveRealZ = std::isfinite(anchorZ) && std::isfinite(targetZ);
  const ray3d::Vec3 anchorPt = haveRealZ ? ray3d::Vec3{anchorX, anchorY, anchorZ}
                                         : ApproximateOnWorkPlaneFromXy(frame, anchorX, anchorY);
  const ray3d::Vec3 targetPt =
      haveRealZ ? ray3d::Vec3{*wx, *wy, targetZ} : ApproximateOnWorkPlaneFromXy(frame, *wx, *wy);
  const ray3d::Vec3 snapped = ucs::SnapToPolarRay(frame, anchorPt, targetPt, incrementDeg,
                                                  extraAnglesDeg.empty() ? nullptr : extraAnglesDeg.data(),
                                                  static_cast<int>(extraAnglesDeg.size()));
  if (!std::isfinite(snapped.x) || !std::isfinite(snapped.y))
    return;  // leave the point alone rather than move it somewhere undefined (REQ-201)
  *wx = static_cast<float>(snapped.x);
  *wy = static_cast<float>(snapped.y);
  if (wz && std::isfinite(snapped.z))
    *wz = static_cast<float>(snapped.z);
}

void ApplyPolarConstrainFromAnchor(const AppCommandState& st, float anchorX, float anchorY, float* wx, float* wy,
                                   bool polar, float anchorZ, float targetZ, float* wz) {
  if (!polar || !st.polarMode || !wx || !wy)
    return;
  SnapPointToAngleSet(CadActiveUcsStorage(st), anchorX, anchorY, wx, wy, anchorZ, targetZ, wz,
                      st.polarIncrementDeg, st.polarExtraAnglesDeg);
}

void ApplyPipeRunCompassFromAnchor(const AppCommandState& st, float anchorX, float anchorY, float* wx, float* wy,
                                   bool compass, float anchorZ, float targetZ, float* wz) {
  if (!compass || !st.pipeRunCompassOn || !wx || !wy)
    return;
  const ucs::Ucs frame = CadActiveUcsStorage(st);
  // Unlike ordinary POLAR (fed a mouse pick that already lies ON the work plane, so an out-of-plane
  // offset never arises), PIPERUN's compass is routinely fed an object-snap hit on a real 3D
  // FEATURE — a flange face, an existing pipe's own wall — that can sit off the anchor's own UCS
  // plane. SnapToPolarRay's documented contract PRESERVES that offset unchanged; silently carrying
  // it through here would pull the new segment off the flat disc the compass ring promises on
  // screen, landing the pipe somewhere the ring never showed. So the pick is FLATTENED onto the
  // anchor's own plane first — the compass is a 2D dial, not a free 3D reach.
  const bool haveRealZ = std::isfinite(anchorZ) && std::isfinite(targetZ);
  const ray3d::Vec3 anchorPt = haveRealZ ? ray3d::Vec3{anchorX, anchorY, anchorZ}
                                         : ApproximateOnWorkPlaneFromXy(frame, anchorX, anchorY);
  const ray3d::Vec3 targetPt =
      haveRealZ ? ray3d::Vec3{*wx, *wy, targetZ} : ApproximateOnWorkPlaneFromXy(frame, *wx, *wy);
  const ucs::Ucs anchoredFrame = ucs::WithOrigin(frame, anchorPt);
  const ucs::Point2D flatUv = ucs::WorldToPlane(anchoredFrame, targetPt);
  const ray3d::Vec3 flatTarget = ucs::PlaneToWorld(anchoredFrame, flatUv);  // offset 0: exactly on plane
  float fwx = static_cast<float>(flatTarget.x);
  float fwy = static_cast<float>(flatTarget.y);
  float fwz = static_cast<float>(flatTarget.z);
  SnapPointToAngleSet(frame, anchorX, anchorY, &fwx, &fwy, anchorZ, fwz, &fwz, st.polarIncrementDeg,
                      st.polarExtraAnglesDeg);
  *wx = fwx;
  *wy = fwy;
  if (wz)
    *wz = fwz;
}

void ApplyOrthoConstrainFromAnchor(const AppCommandState& st, float anchorX, float anchorY, float* wx, float* wy,
                                   bool ortho, float anchorZ, float targetZ, float* wz) {
  if (!ortho || !wx || !wy) {
    // ORTHO and POLAR are mutually exclusive; when ORTHO is off, POLAR (if on) constrains instead.
    ApplyPolarConstrainFromAnchor(st, anchorX, anchorY, wx, wy, !ortho, anchorZ, targetZ, wz);
    return;
  }
  // Under the WCS this is the original world-axis constraint, byte for byte — REQ-047's one tested
  // implementation, still reached by every drawing that never touches UCS.
  if (CadUcsIsWorld(st)) {
    OrthoConstrainPoint(anchorX, anchorY, wx, wy, ortho);
    return;
  }
  // Under a UCS, "square" means square with the UCS axes (REQ-154). Both points need their real 3D
  // position on (or off) the work plane; ORTHO is a constraint within that plane and a tilted
  // plane's Z varies across it — constraining the flat XY projection instead would slide the point
  // off the plane. Prefer the caller's already-resolved Z (issue #371); solving the plane equation
  // for Z from (x,y) alone breaks down once the plane stands on edge to world Z, which is exactly
  // the Front/Left/Right-UCS case the issue reports.
  const ucs::Ucs frame = CadActiveUcsStorage(st);
  const bool haveRealZ = std::isfinite(anchorZ) && std::isfinite(targetZ);
  const ray3d::Vec3 anchorPt = haveRealZ ? ray3d::Vec3{anchorX, anchorY, anchorZ}
                                         : ApproximateOnWorkPlaneFromXy(frame, anchorX, anchorY);
  const ray3d::Vec3 targetPt =
      haveRealZ ? ray3d::Vec3{*wx, *wy, targetZ} : ApproximateOnWorkPlaneFromXy(frame, *wx, *wy);
  // issue #371 second follow-up: under an orbited camera, comparing raw UCS-delta magnitude picks
  // the wrong axis, because an oblique view mixes both in-plane axes into any one screen direction
  // (confirmed against AutoCAD: its ORTHO decision is a screen one, not a world one). Decide on
  // screen whenever a live viewport size is published; fall back to the world-space decision
  // otherwise (headless/tests with no viewport, or before the first frame publishes a size).
  const bool screenAware = st.uiViewportWidthPx > 0.f && st.uiViewportHeightPx > 0.f;
  const ray3d::Vec3 constrained = screenAware
      ? ConstrainToUcsOrthoOnScreen(frame, anchorPt, targetPt, CadViewCamera(st), st.uiViewportWidthPx,
                                    st.uiViewportHeightPx)
      : ConstrainToUcsOrtho(frame, anchorPt, targetPt);
  if (!std::isfinite(constrained.x) || !std::isfinite(constrained.y) || !std::isfinite(constrained.z))
    return;  // leave the point alone rather than move it somewhere undefined (REQ-201)
  *wx = static_cast<float>(constrained.x);
  *wy = static_cast<float>(constrained.y);
  // issue #371 follow-up: the locked axis is not necessarily world X or Y — squaring to a
  // Front/Left/Right-style UCS's vertical axis locks world Z to the ANCHOR's, not the cursor's raw
  // elevation. Report it through wz so a caller that commits or previews Z independently (via
  // CadCommitElevation / uiCursorWorldZ) can pick up the lock instead of silently overwriting it.
  if (wz)
    *wz = static_cast<float>(constrained.z);
}

void ApplySegmentAngleLockToWorldPick(float anchorX, float anchorY, float lockUx, float lockUy, float* wx, float* wy,
                                      bool forwardOnly) {
  if (!wx || !wy)
    return;
  const float dx = *wx - anchorX;
  const float dy = *wy - anchorY;
  float t = dx * lockUx + dy * lockUy;
  if (forwardOnly && t < 0.f)
    t = 0.f;
  *wx = anchorX + t * lockUx;
  *wy = anchorY + t * lockUy;
}


void CancelSegmentAnglePick(AppCommandState& st, std::vector<std::string>* log) {
  auto* ang = stepengine::ActiveSegmentAngleState(st);
  if (!ang)
    return;
  const bool hadPick = ang->pickPhase != stepengine::SegmentAnglePickPhase::Idle;
  ang->pickPhase = stepengine::SegmentAnglePickPhase::Idle;
  ang->keyboardAwaitBearing = false;
  if (hadPick && log)
    log->push_back("Bearing pick — canceled.");
}

bool TryParseSegmentAngleLockCommand(AppCommandState& st, const std::string& lineIn, std::vector<std::string>& log) {
  auto* ang = stepengine::ActiveSegmentAngleState(st);
  if (!ang)
    return false;
  using SAP = stepengine::SegmentAnglePickPhase;
  const std::string s = StringUtil::trimCopy(lineIn);
  if (s.empty())
    return false;
  const std::string low = StringUtil::toLowerAsciiCopy(s);
  if (low == "2p" || low == "2 p" || low == "ap" || low == "anglepick" || low == "a p") {
    ang->reset();
    ang->keyboardAwaitBearing = false;
    ang->pickPhase = SAP::WaitP1;
    log.push_back("Bearing pick — first reference point (viewport click). ESC cancels pick.");
    return true;
  }
  if (low == "a" || low == "angle") {
    if (ang->lockActive || ang->pickPhase != SAP::Idle) {
      ang->reset();
      log.push_back("Segment bearing lock — off.");
    } else {
      ang->keyboardAwaitBearing = true;
      log.push_back("Bearing ° clockwise from north (decimal/DMS); blank Enter cancels.");
    }
    return true;
  }
  std::string rest;
  if (low.rfind("angle ", 0) == 0)
    rest = StringUtil::trimCopy(s.substr(6));
  else if (low.rfind("a ", 0) == 0)
    rest = StringUtil::trimCopy(s.substr(2));
  else if (low.rfind("angle", 0) == 0 && low.size() > 5)
    rest = StringUtil::trimCopy(s.substr(5));
  else if (low.rfind('a', 0) == 0 && low.size() > 1)
    rest = StringUtil::trimCopy(s.substr(1));
  else
    return false;

  if (rest.empty()) {
    if (ang->lockActive || ang->pickPhase != SAP::Idle) {
      ang->reset();
      log.push_back("Segment bearing lock — off.");
    } else {
      ang->keyboardAwaitBearing = true;
      log.push_back("Bearing ° clockwise from north (decimal/DMS); blank Enter cancels.");
    }
    return true;
  }
  float combined = 0.f;
  if (!ParseBearingCwNorthStringWithOptionalDelta(rest, &combined, log))
    return true;
  const float theta = MathAngleRadFromBearingCwNorthDeg(combined);
  ang->lockUx = std::cos(theta);
  ang->lockUy = std::sin(theta);
  ang->lockActive = true;
  ang->pickPhase = SAP::Idle;
  char buf[144];
  std::snprintf(buf, sizeof(buf),
                "Bearing lock %.6g° clockwise from north — distance (+/- along ray) or click (A clears).",
                static_cast<double>(combined));
  log.push_back(buf);
  return true;
}

// Direct-distance entry (REQ-047): the ORTHO axis unit vector from the draft anchor toward the crosshair.
// `uiCursorWorld*` is published in WORLD coordinates while `anchor*` is LOCAL storage, so the crosshair is
// converted to local first — passing the two frames straight to OrthoUnitTowardPoint added the document
// origin to dx alone and pinned every typed distance to +X (see OrthoConstrain.hpp).
bool OrthoUnitTowardUiCursorFromAnchor(const AppCommandState& st, float* ux, float* uy) {
  float cursorLocalX = 0.f;
  float cursorLocalY = 0.f;
  CadCoord::LocalFromWorld(st, static_cast<double>(st.uiCursorWorldX), static_cast<double>(st.uiCursorWorldY),
                           &cursorLocalX, &cursorLocalY);
  return OrthoUnitTowardPoint(st.anchorX, st.anchorY, cursorLocalX, cursorLocalY, ux, uy);
}

// Direct-distance entry under a UCS (issue #371 5th follow-up). OrthoUnitTowardUiCursorFromAnchor
// above is a flat X/Y computation — it never looks at the active UCS's frame or at Z at all, which is
// exactly right under the World UCS (REQ-047's original, still-tested behavior) and exactly wrong
// otherwise: under a Front/Left/Right-style UCS the typed distance travelled in a direction with no
// relation to the UCS's own axes, landing at whatever elevation the mouse happened to be hovering
// over at the time — independent of the direction AND of the distance just typed.
//
// This instead builds the anchor and the raw cursor hit as real 3D points in the SAME storage frame
// CadActiveUcsStorage returns (uiCursorWorldX/Y need CadCoord::LocalFromWorld first, same as above;
// Z is frame-invariant), runs them through the identical UCS-ortho decision the mouse-drag path uses
// (screen-aware when a live viewport is published, landing in the plane through the anchor — issue
// #371's 3rd and 4th follow-ups), and returns the point \p dist units from the anchor along
// whichever axis that decision locked onto. The resulting elevation is published through
// AppCommandState::resolvedPointZ / resolvedPointZValid, the same channel CadCommitElevation reads,
// so the caller's ordinary SubmitLineVertex/SubmitPolylineVertex commit picks it up with no other
// change. Returns false when the cursor coincides with the anchor's locked direction — nothing to
// take a distance along.
bool OrthoUcsDirectDistancePoint(AppCommandState& st, float dist, float* outX, float* outY) {
  if (!outX || !outY)
    return false;
  const ucs::Ucs frame = CadActiveUcsStorage(st);
  float cursorLocalX = 0.f;
  float cursorLocalY = 0.f;
  CadCoord::LocalFromWorld(st, static_cast<double>(st.uiCursorWorldX), static_cast<double>(st.uiCursorWorldY),
                           &cursorLocalX, &cursorLocalY);
  const ray3d::Vec3 anchorPt{st.anchorX, st.anchorY, st.anchorZ};
  const ray3d::Vec3 targetPt{cursorLocalX, cursorLocalY, st.uiCursorWorldZ};
  const bool screenAware = st.uiViewportWidthPx > 0.f && st.uiViewportHeightPx > 0.f;
  const ray3d::Vec3 constrained = screenAware
      ? ConstrainToUcsOrthoOnScreen(frame, anchorPt, targetPt, CadViewCamera(st), st.uiViewportWidthPx,
                                    st.uiViewportHeightPx)
      : ConstrainToUcsOrtho(frame, anchorPt, targetPt);
  const ray3d::Vec3 dir = ray3d::Sub(constrained, anchorPt);
  const double len = ray3d::Length(dir);
  if (!(len > 1e-9))
    return false;
  const ray3d::Vec3 unit = ray3d::Scale(dir, 1.0 / len);
  const ray3d::Vec3 result = ray3d::Add(anchorPt, ray3d::Scale(unit, static_cast<double>(dist)));
  *outX = static_cast<float>(result.x);
  *outY = static_cast<float>(result.y);
  st.resolvedPointZ = static_cast<float>(result.z);
  st.resolvedPointZValid = true;
  // This Z is authoritative for the point about to commit — a stale mouse snap from before the
  // distance was typed must not override it (REQ-047's "object snap overrides ORTHO" is about a
  // LIVE snap under the cursor, not a leftover flag from an earlier hover).
  st.viewportSnapPickValid = false;
  return true;
}

bool ParseSingleFloatToken(const std::string& raw, float* out) {
  std::istringstream iss(raw);
  iss >> std::ws;
  if (!(iss >> *out))
    return false;
  iss >> std::ws;
  return iss.eof();
}
