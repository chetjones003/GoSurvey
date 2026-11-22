#include "CadCommandsInternal.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

// -------------------------------------------------------------------------------------------------
// The seven primitive commands.
//
// One typed line each, which is exactly what REQ-313's acceptance asks for ("exact dimensions typed
// at the command line") and no more. There is deliberately NO interactive pick-and-drag flow in this
// increment: rubber-banding a solid needs a 3D draft preview, and that belongs with #120's Phase 5
// direct-modelling work rather than being invented here. Stated in the usage text, so a user who
// types a bare `BOX` is told what the command wants rather than left in a prompt that never comes.
// -------------------------------------------------------------------------------------------------

namespace {

struct SolidVerbSpec {
  const char* verb;
  const char* alias;
  brep::PrimitiveKind kind;
  int dimensionCount;   ///< numbers required AFTER the base point
  const char* usage;
};

const SolidVerbSpec kSolidVerbs[] = {
    {"box", "", brep::PrimitiveKind::Box, 3, "BOX <X,Y[,Z]> <length> <width> <height>"},
    {"wedge", "we", brep::PrimitiveKind::Wedge, 3, "WEDGE <X,Y[,Z]> <length> <width> <height>"},
    {"pyramid", "pyr", brep::PrimitiveKind::Pyramid, 4,
     "PYRAMID <X,Y[,Z]> <sides> <base radius> <top radius> <height>"},
    {"cylinder", "cyl", brep::PrimitiveKind::Cylinder, 2, "CYLINDER <X,Y[,Z]> <radius> <height>"},
    {"cone", "", brep::PrimitiveKind::Cone, 3, "CONE <X,Y[,Z]> <base radius> <top radius> <height>"},
    {"sphere", "sph", brep::PrimitiveKind::Sphere, 1, "SPHERE <X,Y[,Z]> <radius>"},
    {"torus", "tor", brep::PrimitiveKind::Torus, 2, "TORUS <X,Y[,Z]> <radius> <tube radius>"},
};

const SolidVerbSpec* FindSolidVerb(const std::string& verb) {
  for (const SolidVerbSpec& s : kSolidVerbs) {
    if (verb == s.verb || (*s.alias && verb == s.alias))
      return &s;
  }
  return nullptr;
}

/// The placement frame: the active UCS's orientation, moved to the base point.
///
/// Reusing the UCS is what gives a cylinder or cone an arbitrary 3D axis without a new command or a
/// new axis argument (#120: "the cylinder should support arbitrary 3D axis orientation") — set the
/// work plane, then draw, exactly the rule REQ-312 settled for tilted arcs and circles. The axes are
/// directions, so the local/world translation of the origin does not touch them.
ucs::Ucs SolidPlacementFrame(const AppCommandState& st, const ray3d::Vec3& storageOrigin) {
  ucs::Ucs f = st.activeUcs;
  f.origin = storageOrigin;
  if (!ucs::IsRightHandedOrthonormal(f, 1e-9))
    f = ucs::Ucs{};  // a corrupt active UCS must not silently skew a solid; the builder refuses it
  f.origin = storageOrigin;
  return f;
}

} // namespace

bool CadIsSolidPrimitiveVerb(const std::string& verb) { return FindSolidVerb(verb) != nullptr; }

/// What both authoring forms say when a solid is created — one sentence, one place.
///
/// A self-intersecting solid (a torus whose tube exceeds its ring, ADR-045 (f) as amended) has no
/// meaningful volume: its surface encloses part of space twice, so the closed form is a number
/// rather than an answer. Saying that is the honest report; printing the number would be the silent
/// wrong answer REQ-201 exists to prevent.
std::string SolidCreatedMessage(brep::PrimitiveKind kind, const brep::MassProperties& mp) {
  char msg[240];
  if (mp.valid) {
    std::snprintf(msg, sizeof(msg), "%s created — volume %.4f, surface area %.4f.",
                  brep::PrimitiveKindName(kind), mp.volume, mp.surfaceArea);
  } else {
    std::snprintf(msg, sizeof(msg),
                  "%s created — it passes through itself, so volume and area are not reported.",
                  brep::PrimitiveKindName(kind));
  }
  return msg;
}

/// The circumradius `brep::MakePyramid` needs, from the base radius the user gave.
///
/// A pyramid's base radius is read as the polygon's APOTHEM by default — "circumscribed", the way
/// AutoCAD's PYRAMID reads it — so the circumradius the kernel takes is larger by `1/cos(pi/sides)`.
/// `inscribed` (the prompted form's `I` toggle) reads the given value as the circumradius directly.
///
/// BOTH authoring forms call this, so the same numbers through either route describe the same
/// pyramid (REQ-313: "the same numbers through either route produce the identical solid"). A value
/// of zero (a cone/pyramid apex) and a side count below three pass straight through.
static double PyramidCircumradius(double baseRadius, int sides, bool inscribed) {
  if (inscribed || sides < 3 || baseRadius == 0.0)
    return baseRadius;
  const double k = std::cos(3.14159265358979323846 / static_cast<double>(sides));
  return k > 1e-9 ? baseRadius / k : baseRadius;
}

void CadCreateSolidPrimitive(AppCommandState& st, const std::string& verb, const std::string& rest,
                             std::vector<std::string>& log) {
  const SolidVerbSpec* spec = FindSolidVerb(verb);
  if (!spec)
    return;
  std::string verbUpper = spec->verb;
  for (char& c : verbUpper)
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

  std::istringstream args(rest);
  std::string pointText;
  if (!(args >> pointText)) {
    log.push_back(std::string("Usage: ") + spec->usage + ".");
    return;
  }

  ray3d::Vec3 origin{};
  if (!ParseSolidBasePoint(st, pointText, &origin, log, verbUpper.c_str()))
    return;

  double dims[5] = {0, 0, 0, 0, 0};
  for (int i = 0; i < spec->dimensionCount; ++i) {
    std::string tok;
    if (!(args >> tok)) {
      log.push_back(std::string("Usage: ") + spec->usage + ".");
      return;
    }
    char* end = nullptr;
    dims[i] = std::strtod(tok.c_str(), &end);
    if (tok.empty() || !end || *end != '\0' || !std::isfinite(dims[i])) {
      std::string msg = verbUpper;
      msg += " — \"";
      msg += tok;
      msg += "\" is not a number.";
      log.push_back(std::move(msg));
      return;
    }
  }
  std::string extra;
  if (args >> extra) {
    log.push_back(verbUpper + " — too many values. Usage: " + spec->usage + ".");
    return;
  }

  const ucs::Ucs frame = SolidPlacementFrame(st, origin);
  brep::Solid solid;
  brep::Problem why = brep::Problem::Ok;
  bool built = false;
  switch (spec->kind) {
  case brep::PrimitiveKind::Box:
    built = brep::MakeBox(frame, dims[0], dims[1], dims[2], &solid, &why);
    break;
  case brep::PrimitiveKind::Wedge:
    built = brep::MakeWedge(frame, dims[0], dims[1], dims[2], &solid, &why);
    break;
  case brep::PrimitiveKind::Pyramid: {
    const double sidesD = dims[0];
    // Checked here rather than left to a silent truncation: `PYRAMID ... 4.5 ...` must be refused,
    // not quietly built as a square (REQ-201).
    if (sidesD != std::floor(sidesD)) {
      log.push_back(verbUpper + " — the side count must be a whole number.");
      return;
    }
    // The base radius is the APOTHEM (circumscribed, AutoCAD's default) — the same reading the
    // prompted form uses when its `I` toggle is not set, so `PYRAMID x,y 4 6 0 15` and the prompted
    // pyramid with base radius 6 build one identical solid.
    const int sides = static_cast<int>(sidesD);
    const double baseR = PyramidCircumradius(dims[1], sides, /*inscribed=*/false);
    const double topR = PyramidCircumradius(dims[2], sides, /*inscribed=*/false);
    built = brep::MakePyramid(frame, sides, baseR, topR, dims[3], &solid, &why);
    break;
  }
  case brep::PrimitiveKind::Cylinder:
    built = brep::MakeCylinder(frame, dims[0], dims[1], &solid, &why);
    break;
  case brep::PrimitiveKind::Cone:
    built = brep::MakeCone(frame, dims[0], dims[1], dims[2], &solid, &why);
    break;
  case brep::PrimitiveKind::Sphere:
    built = brep::MakeSphere(frame, dims[0], &solid, &why);
    break;
  case brep::PrimitiveKind::Torus:
    built = brep::MakeTorus(frame, dims[0], dims[1], &solid, &why);
    break;
  case brep::PrimitiveKind::None:
    break;
  }

  if (!built) {
    // The kernel's own reason, verbatim. Nothing is stored, and nothing is repaired (REQ-201).
    log.push_back(verbUpper + " — " + brep::ProblemText(why));
    return;
  }

  const brep::MassProperties mp = brep::ComputeMassProperties(solid);

  PushUndoSnapshot(st, std::string("Create ") + brep::PrimitiveKindName(spec->kind));
  st.drawing.cadSolids.push_back(std::make_shared<const brep::Solid>(std::move(solid)));
  st.drawing.cadSolidAttrs.push_back(MakeNewObjectAttrs(st, ObjectLayerKind::Solid, {}));  // REQ-361
  BumpCadGpuCache(st);

  log.push_back(SolidCreatedMessage(spec->kind, mp));
}

// -------------------------------------------------------------------------------------------------
// The prompted form of the seven primitives (REQ-313 as amended).
//
// `CYLINDER` on its own asks for the base point, then for its dimensions by letter — R for radius,
// H for height — which is the shape CIRCLE already uses for its own radius prompt ("click, type a
// value, or D + diameter") and which LENGTHEN and UCS use for their keyword sets.
//
// `CYLINDER 0,0 4 25` still works and means exactly the same thing. The two forms share the same
// parameter table and the same commit, so neither can accept a solid the other would refuse.
// -------------------------------------------------------------------------------------------------

namespace {

/// The named dimensions of each primitive, in the order a bare typed number fills them.
///
/// The ORDER is load-bearing twice over: it is the order the one-line form reads its arguments, and
/// it is the order the prompted form assigns bare numbers to. Those two being one list is what makes
/// `CYLINDER 0,0 4 25` and typing `4` then `25` at the prompt produce the same solid.
using PK = SolidPickKind;

// Table order is the ONE-LINE form's argument order and must not change — `PYRAMID x,y 4 6 0 15`
// means sides, base radius, top radius, height, and a transcript asserts it. The PICK order is the
// subset whose `pick` is not `Typed`, walked in this same order, which is how a pyramid asks for a
// radius and a height while its side count stays a keyword with a default (AutoCAD's own shape).
const SolidParamSpec kBoxParams[] = {{'L', "length", false, PK::CornerXY},
                                     {'W', "width", false, PK::CornerXY},
                                     {'H', "height", false, PK::Height}};
const SolidParamSpec kPyramidParams[] = {{'S', "sides", true, PK::Typed, 4.0},
                                         {'R', "base radius", false, PK::Radius},
                                         {'T', "top radius", true, PK::Typed, 0.0},
                                         {'H', "height", false, PK::Height}};
const SolidParamSpec kCylinderParams[] = {{'R', "radius", false, PK::Radius},
                                          {'H', "height", false, PK::Height}};
const SolidParamSpec kConeParams[] = {{'R', "base radius", false, PK::Radius},
                                      {'T', "top radius", true, PK::Typed, 0.0},
                                      {'H', "height", false, PK::Height}};
const SolidParamSpec kSphereParams[] = {{'R', "radius", false, PK::Radius}};
// The tube is picked as a distance from the CENTRE, like the ring itself — which is what makes a
// tube larger than the ring reachable by dragging outward, the self-intersecting shape AutoCAD
// builds (ADR-045 (f) as amended).
const SolidParamSpec kTorusParams[] = {{'R', "radius", false, PK::Radius},
                                       {'T', "tube radius", false, PK::Radius}};

} // namespace

const SolidParamSpec* CadSolidParamSpecs(brep::PrimitiveKind kind, int* outCount) {
  auto give = [&](const SolidParamSpec* p, int n) {
    if (outCount)
      *outCount = n;
    return p;
  };
  switch (kind) {
  case brep::PrimitiveKind::Box:      return give(kBoxParams, 3);
  case brep::PrimitiveKind::Wedge:    return give(kBoxParams, 3);  // same three, same order
  case brep::PrimitiveKind::Pyramid:  return give(kPyramidParams, 4);
  case brep::PrimitiveKind::Cylinder: return give(kCylinderParams, 2);
  case brep::PrimitiveKind::Cone:     return give(kConeParams, 3);
  case brep::PrimitiveKind::Sphere:   return give(kSphereParams, 1);
  case brep::PrimitiveKind::Torus:    return give(kTorusParams, 2);
  case brep::PrimitiveKind::None:     break;
  }
  return give(nullptr, 0);
}

void CancelSolidCommand(AppCommandState& st) {
  st.solidKind = brep::PrimitiveKind::None;
  st.solidPhase = AppCommandState::SolidPhase::WaitBasePoint;
  st.solidBase = ray3d::Vec3{};
  st.solidPendingParam = -1;
  st.solidPendingIsDiameter = false;
  st.solidPickValid = false;
  st.solidBaseAngleRad = 0.0;
  st.solidInscribed = false;
  st.solidBaseIsCorner = false;
  st.solidCornerDx = 0.0;
  st.solidCornerDy = 0.0;
  for (int i = 0; i < AppCommandState::kMaxSolidParams; ++i) {
    st.solidParamValue[i] = 0.0;
    st.solidParamSet[i] = false;
  }
}


/// Upper-case name of the running primitive, for prompts and messages.
std::string SolidVerbUpper(brep::PrimitiveKind kind) {
  std::string s = brep::PrimitiveKindName(kind);
  for (char& c : s)
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return s;
}

namespace {

/// `4` -> "4", `4.5` -> "4.5" — a value echoed back the way it was meant, not as "4.000000".
std::string TrimNumber(double v) {
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%.6g", v);
  return buf;
}

} // namespace

std::string CadSolidPromptText(const AppCommandState& st) {
  if (st.solidKind == brep::PrimitiveKind::None)
    return {};
  const std::string verb = SolidVerbUpper(st.solidKind);
  if (st.solidPhase == AppCommandState::SolidPhase::WaitBasePoint) {
    const char* what = "base centre point";
    if (st.solidBaseIsCorner)
      what = "first corner";  // BOX / WEDGE are drawn corner-to-corner, like AutoCAD
    else if (st.solidKind == brep::PrimitiveKind::Sphere || st.solidKind == brep::PrimitiveKind::Torus)
      what = "centre point";
    return verb + " — " + std::string(what) + " (click, or type X,Y or X,Y,Z):";
  }

  int n = 0;
  const SolidParamSpec* specs = CadSolidParamSpecs(st.solidKind, &n);
  if (st.solidPendingParam >= 0 && st.solidPendingParam < n)
    return verb + " — " + specs[st.solidPendingParam].label + ":";

  // Lead with the dimension being PICKED, because that is what the cursor is currently changing and
  // what the rubber band is showing — a prompt that opened with a list would bury it.
  const int picking = CadSolidCurrentPickParam(st);
  std::string s = verb + " — ";
  if (picking >= 0) {
    s += "specify ";
    s += specs[picking].label;
    if (specs[picking].pick == SolidPickKind::CornerXY)
      s += " (opposite corner)";
    s += " (click or type)";
    if (specs[picking].pick == SolidPickKind::Radius)
      s += ", [D]iameter";
    if (st.solidKind == brep::PrimitiveKind::Pyramid)
      s += std::string(", [I]nscribed (now ") + (st.solidInscribed ? "inscribed" : "circumscribed") + ")";
    s += ". ";
  }

  // Then every option, with the ones already set echoed back. A prompt that only listed the letters
  // would make the user hold four numbers in their head; showing them is what lets a value be
  // re-typed to correct it.
  for (int i = 0; i < n; ++i) {
    if (i)
      s += ", ";
    s += specs[i].letter;
    s += " ";
    s += specs[i].label;
    if (st.solidParamSet[i])
      s += " = " + TrimNumber(st.solidParamValue[i]);
  }
  s += ". Enter creates.";
  return s;
}


ucs::Ucs CadSolidPlacementFrameFor(const AppCommandState& st) { return SolidPlacementFrame(st, st.solidBase); }

int CadSolidCurrentPickParam(const AppCommandState& st) {
  int n = 0;
  const SolidParamSpec* specs = CadSolidParamSpecs(st.solidKind, &n);
  for (int i = 0; i < n; ++i) {
    if (specs[i].pick == SolidPickKind::Typed || specs[i].optional)
      continue;  // keyword-and-default dimensions are never picked, and never block the sequence
    if (!st.solidParamSet[i])
      return i;
  }
  return -1;
}

void CadResolveSolidPick(AppCommandState& st, const ray3d::Vec3& cursorOnPlane, const ray3d::Ray* ray) {
  st.solidPickValid = false;
  if (st.solidKind == brep::PrimitiveKind::None ||
      st.solidPhase != AppCommandState::SolidPhase::WaitParameters)
    return;
  const int picking = CadSolidCurrentPickParam(st);
  if (picking < 0)
    return;
  int specCount = 0;
  const SolidParamSpec* specs = CadSolidParamSpecs(st.solidKind, &specCount);

  const ucs::Ucs f = CadSolidPlacementFrameFor(st);
  const ray3d::Vec3 d = ray3d::Sub(cursorOnPlane, st.solidBase);
  const double inPlaneX = ray3d::Dot(d, f.xAxis);
  const double inPlaneY = ray3d::Dot(d, f.yAxis);

  switch (specs[picking].pick) {
  case SolidPickKind::Radius:
    st.solidPickA = std::sqrt(inPlaneX * inPlaneX + inPlaneY * inPlaneY);
    st.solidPickAngleRad = std::atan2(inPlaneY, inPlaneX);
    st.solidPickValid = st.solidPickA > 1e-9;
    break;
  case SolidPickKind::CornerXY:
    st.solidPickA = inPlaneX;
    st.solidPickB = inPlaneY;
    // Both extents must be real: a corner dragged along one axis alone describes a line, not a box,
    // and building it would just be refused a moment later as a non-positive width.
    st.solidPickValid = std::fabs(inPlaneX) > 1e-9 && std::fabs(inPlaneY) > 1e-9;
    break;
  case SolidPickKind::Height: {
    // Height cannot come from the work plane: the cursor is ON it, so its offset along the axis is
    // always zero. It comes from the point on the solid's AXIS closest to the cursor ray, which is
    // what makes dragging upward raise the solid.
    //
    // Plan view has no ray and therefore no answer here — looking straight down the axis, "how far
    // up is the cursor" is not a question the screen can answer. The prompt asks for a typed value
    // instead of inventing one.
    if (!ray || !ray->valid())
      break;
    const ray3d::Vec3 axisDir = f.zAxis;  // unit by construction
    const ray3d::Vec3 w0 = ray3d::Sub(st.solidBase, ray->origin);
    const double b = ray3d::Dot(axisDir, ray->dir);
    const double den = 1.0 - b * b;  // both unit length, so this is sin^2 of the angle between them
    if (!(den > 1e-9))
      break;  // the axis and the ray are parallel: every height projects to the same pixel
    const double dv = ray3d::Dot(axisDir, w0);
    const double ev = ray3d::Dot(ray->dir, w0);
    st.solidPickA = (b * ev - dv) / den;
    st.solidPickValid = std::fabs(st.solidPickA) > 1e-9;
    break;
  }
  case SolidPickKind::Typed:
    break;
  }
  if (!std::isfinite(st.solidPickA) || !std::isfinite(st.solidPickB) ||
      !std::isfinite(st.solidPickAngleRad))
    st.solidPickValid = false;
}

bool CadBuildSolidFromCommand(const AppCommandState& st, bool applyPick, brep::Solid* out,
                              brep::Problem* outWhy) {
  if (!out || st.solidKind == brep::PrimitiveKind::None)
    return false;
  int n = 0;
  const SolidParamSpec* specs = CadSolidParamSpecs(st.solidKind, &n);

  // Effective values: what has been set, an optional dimension's default, and — for the one being
  // picked right now — the cursor.
  double v[AppCommandState::kMaxSolidParams] = {0.0, 0.0, 0.0, 0.0};
  bool have[AppCommandState::kMaxSolidParams] = {false, false, false, false};
  for (int i = 0; i < n; ++i) {
    if (st.solidParamSet[i]) {
      v[i] = st.solidParamValue[i];
      have[i] = true;
    } else if (specs[i].optional) {
      v[i] = specs[i].defaultValue;
      have[i] = true;
    }
  }

  double cornerDx = st.solidCornerDx;
  double cornerDy = st.solidCornerDy;
  const int picking = CadSolidCurrentPickParam(st);
  if (applyPick && st.solidPickValid && picking >= 0) {
    if (specs[picking].pick == SolidPickKind::CornerXY) {
      // One pick, two dimensions — and the SIGNED offsets too, because `length` and `width` cannot
      // say which way the box was dragged and the frame origin has to move to the midpoint.
      cornerDx = st.solidPickA;
      cornerDy = st.solidPickB;
      v[picking] = std::fabs(st.solidPickA);
      have[picking] = true;
      if (picking + 1 < n) {
        v[picking + 1] = std::fabs(st.solidPickB);
        have[picking + 1] = true;
      }
    } else {
      v[picking] = st.solidPickA;
      have[picking] = true;
    }
  }

  for (int i = 0; i < n; ++i) {
    if (!have[i])
      { if (outWhy) *outWhy = brep::Problem::NonFiniteParameter; return false; }  // still being specified
  }

  // Placement. A box or wedge's base point is the first CORNER, so the frame origin moves to the
  // midpoint of the two corners; every other primitive is placed on its base centre already.
  //
  // The offset magnitude is always the committed length / width — a picked opposite corner only
  // supplies the SIGN (which way it was dragged, which `length` and `width` cannot say). Typed
  // dimensions carry no sign, so they anchor at the first corner in the positive direction; without
  // this the "first corner" prompt would be a lie for the typed path and the box would land centred
  // on that point instead.
  ray3d::Vec3 origin = st.solidBase;
  ucs::Ucs frame = SolidPlacementFrame(st, origin);
  if (st.solidBaseIsCorner) {
    const double sx = cornerDx < 0.0 ? -1.0 : 1.0;
    const double sy = cornerDy < 0.0 ? -1.0 : 1.0;
    cornerDx = sx * v[0];
    cornerDy = sy * v[1];
    origin = ray3d::Add(origin, ray3d::Add(ray3d::Scale(frame.xAxis, cornerDx * 0.5),
                                           ray3d::Scale(frame.yAxis, cornerDy * 0.5)));
    frame.origin = origin;
  }

  brep::Problem why = brep::Problem::Ok;
  bool built = false;
  switch (st.solidKind) {
  case brep::PrimitiveKind::Box:
    built = brep::MakeBox(frame, v[0], v[1], v[2], out, &why);
    break;
  case brep::PrimitiveKind::Wedge:
    built = brep::MakeWedge(frame, v[0], v[1], v[2], out, &why);
    break;
  case brep::PrimitiveKind::Pyramid: {
    const double sidesD = v[0];
    if (sidesD != std::floor(sidesD))
    { if (outWhy) *outWhy = brep::Problem::SideCountOutOfRange; return false; }
    const int sides = static_cast<int>(sidesD);
    // The base turns with the cursor, the way AutoCAD's does: the radius pick's DIRECTION is the
    // polygon's rotation, not just its size.
    ucs::Ucs baseFrame = ucs::RotatedAboutZ(frame, st.solidBaseAngleRad * 180.0 / 3.14159265358979323846);
    baseFrame.origin = frame.origin;
    // Inscribed means the given radius IS the circumradius (the polygon sits inside that circle);
    // circumscribed — AutoCAD's default — means it is the apothem, so the circumradius is larger.
    // `PyramidCircumradius` is the one place that conversion lives, and the one-line form calls it
    // too, so the same numbers through either route build one identical pyramid.
    const double baseR = PyramidCircumradius(v[1], sides, st.solidInscribed);
    const double topR = PyramidCircumradius(v[2], sides, st.solidInscribed);
    built = brep::MakePyramid(baseFrame, sides, baseR, topR, v[3], out, &why);
    break;
  }
  case brep::PrimitiveKind::Cylinder:
    built = brep::MakeCylinder(frame, v[0], v[1], out, &why);
    break;
  case brep::PrimitiveKind::Cone:
    built = brep::MakeCone(frame, v[0], v[1], v[2], out, &why);
    break;
  case brep::PrimitiveKind::Sphere:
    built = brep::MakeSphere(frame, v[0], out, &why);
    break;
  case brep::PrimitiveKind::Torus:
    built = brep::MakeTorus(frame, v[0], v[1], out, &why);
    break;
  case brep::PrimitiveKind::None:
    break;
  }
  if (outWhy)
    *outWhy = why;
  return built;
}

void StartSolidPrimitiveCommand(AppCommandState& st, const std::string& verb,
                                std::vector<std::string>& log) {
  const SolidVerbSpec* spec = FindSolidVerb(verb);
  if (!spec)
    return;
  CancelSolidCommand(st);
  st.active = AppCommandState::Kind::Solid;
  st.solidKind = spec->kind;
  st.solidPhase = AppCommandState::SolidPhase::WaitBasePoint;
  // BOX and WEDGE are drawn corner-to-corner here, which is what AutoCAD does and what the base
  // point means at this prompt. The ONE-LINE form is unchanged and still takes a base CENTRE plus
  // explicit length and width — with the dimensions given outright, a centre is the natural anchor,
  // and changing it would break the acceptance that rests on it.
  st.solidBaseIsCorner = spec->kind == brep::PrimitiveKind::Box || spec->kind == brep::PrimitiveKind::Wedge;
  log.push_back(CadSolidPromptText(st));
}

namespace {

/// Build and store the solid from the base point and the parameters gathered so far.
///
/// Routes through the SAME `MakeX` call the one-line form uses, so the two forms cannot disagree
/// about what a set of numbers means, and every refusal is the kernel's own (REQ-201).
void CommitPromptedSolid(AppCommandState& st, std::vector<std::string>& log) {
  int n = 0;
  const SolidParamSpec* specs = CadSolidParamSpecs(st.solidKind, &n);
  const std::string verb = SolidVerbUpper(st.solidKind);

  // Refuse a half-specified solid by NAMING what is missing, rather than assuming a size for it.
  std::string missing;
  for (int i = 0; i < n; ++i) {
    if (st.solidParamSet[i] || specs[i].optional)
      continue;
    if (!missing.empty())
      missing += ", ";
    missing += specs[i].label;
  }
  if (!missing.empty()) {
    log.push_back(verb + " — still need: " + missing + ".");
    return;
  }

  brep::Solid solid;
  brep::Problem why = brep::Problem::Ok;
  // Through the SAME builder the live preview draws from, so the solid that appears on Enter is
  // exactly the one the user was looking at.
  const bool built = CadBuildSolidFromCommand(st, /*applyPick=*/false, &solid, &why);

  if (!built) {
    // The command stays open on a refusal, deliberately: the user has given a base point and two or
    // three dimensions, and throwing all of it away because one was wrong would be the worse
    // outcome. They retype the offending letter and press Enter again.
    log.push_back(verb + " — " + brep::ProblemText(why));
    return;
  }

  const brep::MassProperties mp = brep::ComputeMassProperties(solid);
  PushUndoSnapshot(st, std::string("Create ") + brep::PrimitiveKindName(st.solidKind));
  st.drawing.cadSolids.push_back(std::make_shared<const brep::Solid>(std::move(solid)));
  st.drawing.cadSolidAttrs.push_back(MakeNewObjectAttrs(st, ObjectLayerKind::Solid, {}));  // REQ-361
  BumpCadGpuCache(st);

  log.push_back(SolidCreatedMessage(st.solidKind, mp));

  CancelSolidCommand(st);
  st.active = AppCommandState::Kind::None;
}

/// Store \p value in parameter \p index and report it back.
void SetSolidParam(AppCommandState& st, int index, double value, std::vector<std::string>& log) {
  int n = 0;
  const SolidParamSpec* specs = CadSolidParamSpecs(st.solidKind, &n);
  if (index < 0 || index >= n)
    return;
  st.solidParamValue[index] = value;
  st.solidParamSet[index] = true;
  st.solidPendingParam = -1;
  log.push_back(SolidVerbUpper(st.solidKind) + " — " + specs[index].label + " = " + TrimNumber(value) +
                ".");
  log.push_back(CadSolidPromptText(st));
}

} // namespace

bool HandleSolidTextInput(const std::string& lineIn, AppCommandState& st, std::vector<std::string>& log) {
  if (st.solidKind == brep::PrimitiveKind::None)
    return false;
  const std::string line = StringUtil::trimCopy(lineIn);
  const std::string verb = SolidVerbUpper(st.solidKind);

  if (st.solidPhase == AppCommandState::SolidPhase::WaitBasePoint) {
    if (line.empty())
      return false;  // a bare Enter here has nothing to act on; the prompt stands
    ray3d::Vec3 base{};
    if (!ParseSolidBasePoint(st, line, &base, log, verb.c_str()))
      return true;  // the reason has been reported; stay on this prompt rather than cancel
    st.solidBase = base;
    st.solidPhase = AppCommandState::SolidPhase::WaitParameters;
    log.push_back(CadSolidPromptText(st));
    return true;
  }

  int n = 0;
  const SolidParamSpec* specs = CadSolidParamSpecs(st.solidKind, &n);

  // Enter creates it.
  if (line.empty()) {
    CommitPromptedSolid(st, log);
    return true;
  }

  auto parseNumber = [](const std::string& text, double* out) {
    char* end = nullptr;
    const double v = std::strtod(text.c_str(), &end);
    if (text.empty() || !end || *end != '\0' || !std::isfinite(v))
      return false;
    *out = v;
    return true;
  };

  // A letter was armed on the previous line: this one is its value.
  if (st.solidPendingParam >= 0) {
    double v = 0.0;
    if (!parseNumber(line, &v)) {
      log.push_back(verb + " — \"" + line + "\" is not a number.");
      log.push_back(CadSolidPromptText(st));
      return true;
    }
    // `D` armed a DIAMETER, so it is halved into the radius it names. This is the ONE place the
    // halving happens — `D <value>` on a single line arms the parameter and falls through to here
    // too — so a diameter can never reach the kernel as a radius.
    if (st.solidPendingIsDiameter)
      v *= 0.5;
    st.solidPendingIsDiameter = false;
    SetSolidParam(st, st.solidPendingParam, v, log);
    return true;
  }

  const char first = static_cast<char>(std::toupper(static_cast<unsigned char>(line[0])));

  // `I` — PYRAMID's inscribed / circumscribed toggle. The base radius means the polygon's
  // circumradius when inscribed and its apothem when circumscribed (AutoCAD's default), so this
  // changes what an already-typed radius MEANS and is echoed back with the prompt.
  if (st.solidKind == brep::PrimitiveKind::Pyramid && first == 'I' &&
      StringUtil::trimCopy(line.substr(1)).empty()) {
    st.solidInscribed = !st.solidInscribed;
    log.push_back(verb + " — base is now " + (st.solidInscribed ? "INSCRIBED" : "CIRCUMSCRIBED") + ".");
    log.push_back(CadSolidPromptText(st));
    return true;
  }

  // `D <value>` — a diameter for whichever radius is being picked, halved into it. AutoCAD offers it
  // at every radius prompt, and it is the one abbreviation people reach for without thinking.
  if (first == 'D') {
    const int target = CadSolidCurrentPickParam(st);
    if (target >= 0 && specs[target].pick == SolidPickKind::Radius) {
      const std::string rest = StringUtil::trimCopy(line.substr(1));
      if (rest.empty()) {
        st.solidPendingParam = target;
        st.solidPendingIsDiameter = true;
        log.push_back(verb + " — " + specs[target].label + " diameter:");
        return true;
      }
      double d = 0.0;
      if (!parseNumber(rest, &d)) {
        log.push_back(verb + " — \"" + rest + "\" is not a number.");
        log.push_back(CadSolidPromptText(st));
        return true;
      }
      // Halved in the ONE place a diameter is ever halved — the armed-value branch above — reached
      // by arming the parameter and handing it the value, so `D 10` and `D` then `10` are one path.
      st.solidPendingParam = target;
      st.solidPendingIsDiameter = true;
      char buf[64];
      std::snprintf(buf, sizeof(buf), "%.17g", d);
      return HandleSolidTextInput(buf, st, log);
    }
  }

  // `R 4` / `R4` / `R` — a leading letter naming one of this primitive's dimensions.
  for (int i = 0; i < n; ++i) {
    if (specs[i].letter != first)
      continue;
    const std::string rest = StringUtil::trimCopy(line.substr(1));
    if (rest.empty()) {
      st.solidPendingParam = i;  // arm it; the next line is the value
      log.push_back(CadSolidPromptText(st));
      return true;
    }
    double v = 0.0;
    if (!parseNumber(rest, &v)) {
      std::string msg = verb;
      msg += " — \"";
      msg += rest;
      msg += "\" is not a number.";
      log.push_back(std::move(msg));
      log.push_back(CadSolidPromptText(st));
      return true;
    }
    SetSolidParam(st, i, v, log);
    return true;
  }

  // A bare number fills the next dimension that has not been set — which is what makes typing
  // `4` then `25` at a CYLINDER prompt mean radius then height, the same order the one-line form
  // takes them in.
  double v = 0.0;
  if (parseNumber(line, &v)) {
    for (int i = 0; i < n; ++i) {
      if (!st.solidParamSet[i]) {
        SetSolidParam(st, i, v, log);
        return true;
      }
    }
    log.push_back(verb + " — every dimension is already set. Press Enter to create, or type a letter"
                         " + value to change one.");
    return true;
  }

  log.push_back(verb + " — \"" + line + "\" is not a dimension. " + CadSolidPromptText(st));
  return true;
}

void SubmitSolidViewportPick(AppCommandState& st, float wx, float wy, std::vector<std::string>& log) {
  if (st.solidKind == brep::PrimitiveKind::None)
    return;
  if (st.solidPhase == AppCommandState::SolidPhase::WaitBasePoint) {
    st.solidBase = ray3d::Vec3{static_cast<double>(wx), static_cast<double>(wy),
                               static_cast<double>(CadCommitElevation(st))};
    st.solidPhase = AppCommandState::SolidPhase::WaitParameters;
    log.push_back(CadSolidPromptText(st));
    return;
  }

  // A click at a DIMENSION prompt commits whatever the cursor is currently worth — the same value
  // the rubber preview has been drawing, republished each frame by the viewport (`solidPickA/B`).
  const int picking = CadSolidCurrentPickParam(st);
  if (picking < 0) {
    log.push_back(SolidVerbUpper(st.solidKind) + " — every dimension is set. Press Enter to create.");
    return;
  }
  if (!st.solidPickValid) {
    // No value under the cursor. Height needs a pick RAY, which plan view does not have: there is no
    // "how far up is the cursor" to read, and inventing one would place the solid somewhere the user
    // never pointed. Say so rather than swallow the click (the TASK-099 lesson).
    log.push_back(SolidVerbUpper(st.solidKind) +
                  " — this dimension cannot be picked from here; type a value, or orbit the view.");
    return;
  }
  int n = 0;
  const SolidParamSpec* specs = CadSolidParamSpecs(st.solidKind, &n);
  if (specs[picking].pick == SolidPickKind::CornerXY) {
    // One click, two dimensions, plus the signed offsets the frame origin needs (BOX / WEDGE).
    st.solidCornerDx = st.solidPickA;
    st.solidCornerDy = st.solidPickB;
    SetSolidParam(st, picking, std::fabs(st.solidPickA), log);
    if (picking + 1 < n)
      SetSolidParam(st, picking + 1, std::fabs(st.solidPickB), log);
  } else {
    if (specs[picking].pick == SolidPickKind::Radius)
      st.solidBaseAngleRad = st.solidPickAngleRad;  // the base turns with the cursor (PYRAMID)
    SetSolidParam(st, picking, st.solidPickA, log);
  }

  // Every dimension in: create it, so a fully-picked solid needs no extra Enter — AutoCAD's shape.
  if (CadSolidCurrentPickParam(st) < 0)
    CommitPromptedSolid(st, log);
}

// ---------------------------------------------------------------------------------------------
// REQ-317 POLYSOLID — a wall swept along a picked path.
//
// Its own command Kind rather than an eighth row in `CadSolidParamSpecs`: a polysolid is built from
// a PATH, so its state is a growing list rather than a fixed set of named dimensions, and folding it
// into `Kind::Solid` would put a variable-length entry in a table no other row uses.
//
// What it shares with the seven primitives is everything after the geometry: one builder feeding the
// preview, the click and Enter alike; the kernel's own refusals; and the same store, undo step and
// created-message.
// ---------------------------------------------------------------------------------------------
namespace {

/// The placement frame: the active UCS anchored at the first picked point.
[[nodiscard]] ucs::Ucs PolysolidFrame(const AppCommandState& st) {
  return SolidPlacementFrame(st, st.polysolidBase);
}

[[nodiscard]] const char* JustifyName(brep::Justify j) {
  switch (j) {
  case brep::Justify::Left: return "Left";
  case brep::Justify::Center: return "Center";
  case brep::Justify::Right: return "Right";
  }
  return "Center";
}

/// The path's end point, in the frame's plane.
[[nodiscard]] ucs::Point2D PolysolidEndPoint(const brep::Path& p) {
  return p.segs.empty() ? p.start : p.segs.back().end;
}

/// The unit tangent at the end of \p p, pointing along travel. `{0,0}` when there is no segment yet
/// — the case an arc cannot be drawn from, and the caller refuses it by name rather than guessing.
[[nodiscard]] ucs::Point2D PolysolidEndTangent(const brep::Path& p) {
  if (p.segs.empty())
    return ucs::Point2D{0.0, 0.0};
  const ucs::Point2D a = p.segs.size() == 1 ? p.start : p.segs[p.segs.size() - 2].end;
  const ucs::Point2D b = p.segs.back().end;
  const double dx = b.x - a.x;
  const double dy = b.y - a.y;
  const double len = std::sqrt(dx * dx + dy * dy);
  if (!(len > 1e-12))
    return ucs::Point2D{0.0, 0.0};
  const ucs::Point2D chord{dx / len, dy / len};
  const double sweep = p.segs.back().sweep;
  if (std::fabs(sweep) <= 1e-12)
    return chord;
  // The end tangent of an arc is its chord turned by half the included angle: the chord is the
  // average of the two end tangents, so each is half a sweep either side of it.
  const double c = std::cos(sweep * 0.5);
  const double s = std::sin(sweep * 0.5);
  return ucs::Point2D{chord.x * c - chord.y * s, chord.x * s + chord.y * c};
}

/// The sweep of an arc that leaves \p from along \p tangent and arrives at \p to.
///
/// One pick determines it, which is what makes an arc segment a gesture rather than a form: the
/// start point and the direction out of the previous segment are already known, and a circle through
/// a point with a given tangent at another is unique. PLINE's own rule, and AutoCAD's.
[[nodiscard]] double TangentArcSweep(const ucs::Point2D& from, const ucs::Point2D& tangent,
                                     const ucs::Point2D& to) {
  const double dx = from.x - to.x;
  const double dy = from.y - to.y;
  const ucs::Point2D n{-tangent.y, tangent.x};  // the left normal: the centre lies along it
  const double dn = dx * n.x + dy * n.y;
  const double d2 = dx * dx + dy * dy;
  if (!(std::fabs(dn) > 1e-12 * std::max(1.0, d2)))
    return 0.0;  // the far point is straight ahead: the "arc" is a straight run, and is one
  const double r = -d2 / (2.0 * dn);
  const ucs::Point2D centre{from.x + r * n.x, from.y + r * n.y};
  const double a0 = std::atan2(from.y - centre.y, from.x - centre.x);
  const double a1 = std::atan2(to.y - centre.y, to.x - centre.x);
  double d = a1 - a0;
  const double sign = r > 0.0 ? 1.0 : -1.0;  // r > 0 puts the centre on the left, which sweeps CCW
  while (d <= 0.0)
    d += 2.0 * 3.14159265358979323846;
  while (d > 2.0 * 3.14159265358979323846)
    d -= 2.0 * 3.14159265358979323846;
  return sign >= 0.0 ? d : d - 2.0 * 3.14159265358979323846;
}

} // namespace

ucs::Ucs CadPolysolidFrameFor(const AppCommandState& st) { return PolysolidFrame(st); }

void CancelPolysolidCommand(AppCommandState& st) {
  st.polysolidPhase = AppCommandState::PolysolidPhase::WaitFirstPoint;
  st.polysolidPath = brep::Path{};
  st.polysolidBase = ray3d::Vec3{};
  st.polysolidArcMode = false;
  st.polysolidPending = 0;
  // Width, height and justification deliberately SURVIVE the cancel: they are the settings the next
  // wall will want, which is what AutoCAD's PSOLWIDTH and PSOLHEIGHT are for.
}

bool CadBuildPolysolidFromCommand(const AppCommandState& st, const ucs::Point2D* cursor,
                                  brep::Solid* out, brep::Problem* outWhy) {
  brep::Path p = st.polysolidPath;
  if (cursor) {
    // The segment the cursor is currently proposing, so the preview and the click that commits it
    // are built from ONE function — a preview computed separately eventually shows a wall the click
    // does not build (ADR-046 (a), the argument REQ-313 already made for its own preview).
    const double sweep =
        st.polysolidArcMode ? TangentArcSweep(PolysolidEndPoint(p), PolysolidEndTangent(p), *cursor)
                            : 0.0;
    p.segs.push_back(brep::PathSeg{*cursor, sweep});
  }
  if (p.segs.empty())
    return false;
  return brep::MakePolysolid(PolysolidFrame(st), p, st.polysolidWidth, st.polysolidHeight,
                             st.polysolidJustify, out, outWhy);
}

std::string CadPolysolidPromptText(const AppCommandState& st) {
  char buf[320];
  const std::string dims = "[H=" + TrimNumber(st.polysolidHeight) + " W=" +
                           TrimNumber(st.polysolidWidth) + " J=" + JustifyName(st.polysolidJustify) +
                           "]";
  switch (st.polysolidPhase) {
  case AppCommandState::PolysolidPhase::WaitObject:
    std::snprintf(buf, sizeof(buf),
                  "POLYSOLID - select a line, arc, circle or polyline to sweep along %s",
                  dims.c_str());
    return buf;
  case AppCommandState::PolysolidPhase::WaitFirstPoint:
    std::snprintf(buf, sizeof(buf),
                  "POLYSOLID - start point, or Object / Height / Width / Justify %s", dims.c_str());
    return buf;
  case AppCommandState::PolysolidPhase::WaitNextPoint:
    break;
  }
  std::snprintf(buf, sizeof(buf),
                "POLYSOLID - next point%s, or %s / Close / Undo / Height / Width / Justify, Enter to "
                "finish %s",
                st.polysolidPath.segs.empty() ? "" : " (Enter finishes)",
                st.polysolidArcMode ? "Line" : "Arc", dims.c_str());
  return buf;
}

namespace {

/// Store the wall built from the path so far and end the command.
void CommitPolysolid(AppCommandState& st, std::vector<std::string>& log) {
  if (st.polysolidPath.segs.empty()) {
    log.push_back("POLYSOLID - a wall needs at least two points; Esc cancels.");
    return;
  }
  brep::Solid solid;
  brep::Problem why = brep::Problem::Ok;
  if (!CadBuildPolysolidFromCommand(st, nullptr, &solid, &why)) {
    // The kernel's own reason, and the command STAYS OPEN — the points already picked are worth
    // more than the message, and `U` can take back the one that caused it.
    log.push_back(std::string("POLYSOLID - ") + brep::ProblemText(why));
    log.push_back(CadPolysolidPromptText(st));
    return;
  }
  const brep::MassProperties mp = brep::ComputeMassProperties(solid);
  PushUndoSnapshot(st, "Create Polysolid");
  st.drawing.cadSolids.push_back(std::make_shared<const brep::Solid>(std::move(solid)));
  st.drawing.cadSolidAttrs.push_back(MakeNewObjectAttrs(st, ObjectLayerKind::Solid, {}));  // REQ-361
  BumpCadGpuCache(st);
  log.push_back(SolidCreatedMessage(brep::PrimitiveKind::Polysolid, mp));
  CancelPolysolidCommand(st);
  st.active = AppCommandState::Kind::None;
}

/// Add the segment ending at \p pt, in the frame's plane.
void AddPolysolidPoint(AppCommandState& st, const ucs::Point2D& pt, std::vector<std::string>& log) {
  const ucs::Point2D from = PolysolidEndPoint(st.polysolidPath);
  if (std::fabs(pt.x - from.x) < 1e-9 && std::fabs(pt.y - from.y) < 1e-9) {
    log.push_back("POLYSOLID - that is the same point as the last one.");
    return;
  }
  double sweep = 0.0;
  if (st.polysolidArcMode) {
    if (st.polysolidPath.segs.empty()) {
      // An arc has to be tangent to SOMETHING. There is no incoming direction at the very first
      // segment, so this is refused rather than given a direction the user did not choose.
      log.push_back("POLYSOLID - an arc needs a straight run before it to be tangent to; draw one "
                    "first, or press L for a straight segment.");
      return;
    }
    sweep = TangentArcSweep(from, PolysolidEndTangent(st.polysolidPath), pt);
  }
  st.polysolidPath.segs.push_back(brep::PathSeg{pt, sweep});
  st.polysolidPhase = AppCommandState::PolysolidPhase::WaitNextPoint;
  log.push_back(CadPolysolidPromptText(st));
}

/// Set one of the three remembered settings from \p value.
bool SetPolysolidSetting(AppCommandState& st, char letter, const std::string& value,
                         std::vector<std::string>& log) {
  const std::string v = StringUtil::trimCopy(value);
  if (letter == 'J') {
    const char c = v.empty() ? '\0' : static_cast<char>(std::toupper(static_cast<unsigned char>(v[0])));
    if (c == 'L')
      st.polysolidJustify = brep::Justify::Left;
    else if (c == 'C')
      st.polysolidJustify = brep::Justify::Center;
    else if (c == 'R')
      st.polysolidJustify = brep::Justify::Right;
    else {
      log.push_back("POLYSOLID - justification must be Left, Center or Right.");
      return false;
    }
    log.push_back(std::string("POLYSOLID - justification = ") + JustifyName(st.polysolidJustify) + ".");
    return true;
  }
  double d = 0.0;
  {
    char* end = nullptr;
    d = std::strtod(v.c_str(), &end);
    if (v.empty() || !end || *end != 0 || !std::isfinite(d) || !(d > 0.0)) {
      log.push_back(std::string("POLYSOLID - ") + (letter == 'H' ? "height" : "width") +
                    " must be a positive number.");
      return false;
    }
  }
  if (letter == 'H') {
    st.polysolidHeight = d;
    log.push_back("POLYSOLID - height = " + TrimNumber(d) + ".");
  } else {
    st.polysolidWidth = d;
    log.push_back("POLYSOLID - width = " + TrimNumber(d) + ".");
  }
  return true;
}

} // namespace

void StartPolysolidCommand(AppCommandState& st, std::vector<std::string>& log) {
  CancelPolysolidCommand(st);
  st.active = AppCommandState::Kind::Polysolid;
  log.push_back(CadPolysolidPromptText(st));
}

bool HandlePolysolidTextInput(const std::string& lineIn, AppCommandState& st,
                              std::vector<std::string>& log) {
  const std::string line = StringUtil::trimCopy(lineIn);

  // A letter armed on its own line takes the next line as its value, which is what makes `H` then
  // `4` work as well as `H 4` — the shape the prompted primitives already established.
  if (st.polysolidPending != 0) {
    const char letter = st.polysolidPending;
    st.polysolidPending = 0;
    if (line.empty()) {
      log.push_back(CadPolysolidPromptText(st));
      return true;
    }
    (void)SetPolysolidSetting(st, letter, line, log);
    log.push_back(CadPolysolidPromptText(st));
    return true;
  }

  if (line.empty()) {
    // Enter FINISHES an open wall. At the very first prompt there is nothing to finish, so it ends
    // the command rather than reporting a refusal for something the user never started.
    if (st.polysolidPhase == AppCommandState::PolysolidPhase::WaitFirstPoint) {
      log.push_back("POLYSOLID canceled.");
      CancelPolysolidCommand(st);
      st.active = AppCommandState::Kind::None;
      return true;
    }
    CommitPolysolid(st, log);
    return true;
  }

  const char head = static_cast<char>(std::toupper(static_cast<unsigned char>(line[0])));
  const std::string rest = StringUtil::trimCopy(line.substr(1));
  const bool bareLetter = rest.empty();

  // No coordinate begins with one of these letters, so there is nothing to disambiguate against.
  if (head == 'H' || head == 'W' || head == 'J') {
    if (bareLetter) {
      st.polysolidPending = head;
      log.push_back(head == 'J' ? "POLYSOLID - justification (Left / Center / Right)?"
                                : (head == 'H' ? "POLYSOLID - height?" : "POLYSOLID - width?"));
      return true;
    }
    (void)SetPolysolidSetting(st, head, rest, log);
    log.push_back(CadPolysolidPromptText(st));
    return true;
  }

  if (head == 'O' && bareLetter) {
    if (st.polysolidPhase == AppCommandState::PolysolidPhase::WaitNextPoint) {
      log.push_back("POLYSOLID - Object converts something already drawn; it cannot join a run "
                    "already under way.");
      return true;
    }
    st.polysolidPhase = AppCommandState::PolysolidPhase::WaitObject;
    log.push_back(CadPolysolidPromptText(st));
    return true;
  }

  if ((head == 'A' || head == 'L') && bareLetter) {
    st.polysolidArcMode = head == 'A';
    log.push_back(st.polysolidArcMode ? "POLYSOLID - arc segments." : "POLYSOLID - straight segments.");
    log.push_back(CadPolysolidPromptText(st));
    return true;
  }

  if (head == 'C' && bareLetter) {
    if (st.polysolidPath.segs.size() < 2) {
      log.push_back("POLYSOLID - a closed wall needs at least three points.");
      return true;
    }
    // Close by returning to the start: a straight run, or an arc tangent to the last one, exactly as
    // a further picked point would be.
    const ucs::Point2D from = PolysolidEndPoint(st.polysolidPath);
    const double sweep =
        st.polysolidArcMode
            ? TangentArcSweep(from, PolysolidEndTangent(st.polysolidPath), st.polysolidPath.start)
            : 0.0;
    st.polysolidPath.segs.push_back(brep::PathSeg{st.polysolidPath.start, sweep});
    st.polysolidPath.closed = true;
    CommitPolysolid(st, log);
    if (st.active == AppCommandState::Kind::Polysolid) {
      // Refused: take the closing segment back off so the run is exactly where it was.
      st.polysolidPath.segs.pop_back();
      st.polysolidPath.closed = false;
    }
    return true;
  }

  if (head == 'U' && bareLetter) {
    if (st.polysolidPath.segs.empty()) {
      log.push_back("POLYSOLID - nothing to undo yet.");
      return true;
    }
    st.polysolidPath.segs.pop_back();
    if (st.polysolidPath.segs.empty())
      log.push_back("POLYSOLID - back to the start point.");
    log.push_back(CadPolysolidPromptText(st));
    return true;
  }

  // Anything else is a coordinate — except at the Object prompt, which wants something already
  // drawn. Typing a point there would silently start an ordinary run instead, which is not what was
  // asked for.
  if (st.polysolidPhase == AppCommandState::PolysolidPhase::WaitObject) {
    log.push_back("POLYSOLID - click the line, arc, circle or polyline to sweep along (Esc cancels).");
    return true;
  }
  ray3d::Vec3 pt{};
  if (!ParseSolidBasePoint(st, line, &pt, log, "POLYSOLID"))
    return true;  // the reason has been reported; the prompt stands
  if (st.polysolidPhase == AppCommandState::PolysolidPhase::WaitFirstPoint) {
    st.polysolidBase = pt;
    st.polysolidPath = brep::Path{};
    st.polysolidPhase = AppCommandState::PolysolidPhase::WaitNextPoint;
    log.push_back(CadPolysolidPromptText(st));
    return true;
  }
  const ucs::Point2D local = ucs::WorldToPlane(PolysolidFrame(st), pt);
  AddPolysolidPoint(st, local, log);
  return true;
}

void SubmitPolysolidViewportPick(AppCommandState& st, float wx, float wy,
                                 std::vector<std::string>& log) {
  if (st.polysolidPhase == AppCommandState::PolysolidPhase::WaitObject) {
    CadPolysolidConvertObjectAt(st, wx, wy, log);
    return;
  }
  const ray3d::Vec3 pt{static_cast<double>(wx), static_cast<double>(wy), CadCommitElevation(st)};
  if (st.polysolidPhase == AppCommandState::PolysolidPhase::WaitFirstPoint) {
    st.polysolidBase = pt;
    st.polysolidPath = brep::Path{};
    st.polysolidPhase = AppCommandState::PolysolidPhase::WaitNextPoint;
    log.push_back(CadPolysolidPromptText(st));
    return;
  }
  AddPolysolidPoint(st, ucs::WorldToPlane(PolysolidFrame(st), pt), log);
}

namespace {

/// Append the straight run \p from -> \p to, in \p frame's plane, refusing a point off that plane.
[[nodiscard]] bool AppendFlatPoint(const ucs::Ucs& frame, const ray3d::Vec3& p, double tol,
                                   brep::Path* path, bool first) {
  double off = 0.0;
  const ucs::Point2D q = ucs::WorldToPlane(frame, p, &off);
  if (std::fabs(off) > tol)
    return false;
  if (first)
    path->start = q;
  else
    path->segs.push_back(brep::PathSeg{q, 0.0});
  return true;
}

} // namespace

void CadPolysolidConvertObjectAt(AppCommandState& st, float wx, float wy,
                                 std::vector<std::string>& log) {
  SelectedEntity hit;
  const float tol = CadOffsetEntityPickTolWorld(st);
  if (!PickClosestCadEntity(st, wx, wy, tol, &hit, nullptr)) {
    log.push_back("POLYSOLID - nothing there. Pick a line, arc, circle or polyline (Esc cancels).");
    return;
  }

  using T = SelectedEntity::Type;
  if (hit.type != T::LineSeg && hit.type != T::Arc && hit.type != T::Circle &&
      hit.type != T::Polyline) {
    log.push_back("POLYSOLID - a wall can follow a line, an arc, a circle or a polyline. Nothing "
                  "else has a path to sweep along.");
    return;
  }

  // The path has to lie in the plane the wall rises from, so the source is measured against the
  // active work plane and refused by name if it does not lie in it. Anchoring the frame at the
  // source's own start point is what makes that test meaningful rather than circular.
  ray3d::Vec3 anchor{};
  const std::size_t i = static_cast<std::size_t>(hit.index);
  switch (hit.type) {
  case T::LineSeg:
    anchor = ray3d::Vec3{st.drawing.line(i).x0, st.drawing.line(i).y0, st.drawing.line(i).z0};
    break;
  case T::Arc: {
    const CadArc& a = st.drawing.userArcs[i];
    anchor = CurveWorldPointOnArc(a, static_cast<double>(a.startRad));
    break;
  }
  case T::Circle: {
    const CadCircleRecord circle = st.drawing.circle(i);
    anchor = ray3d::Vec3{circle.cx + circle.r, circle.cy, circle.cz};
    break;
  }
  default: {
    const std::size_t base = static_cast<std::size_t>(st.drawing.userPolylineOffsets[i]);
    anchor = ray3d::Vec3{st.drawing.vertex(base).x, st.drawing.vertex(base).y,
                         st.drawing.vertex(base).z};
    break;
  }
  }

  const AppCommandState::PolysolidPhase savedPhase = st.polysolidPhase;
  st.polysolidBase = anchor;
  const ucs::Ucs frame = PolysolidFrame(st);
  const double planeTol = 1e-6 * std::max(1.0, std::fabs(anchor.x) + std::fabs(anchor.y));

  brep::Path path;
  bool flat = true;
  switch (hit.type) {
  case T::LineSeg: {
    flat = AppendFlatPoint(frame, anchor, planeTol, &path, true) &&
           AppendFlatPoint(frame,
                           ray3d::Vec3{st.drawing.line(i).x1, st.drawing.line(i).y1, st.drawing.line(i).z1},
                           planeTol, &path, false);
    break;
  }
  case T::Arc: {
    const CadArc& a = st.drawing.userArcs[i];
    const ray3d::Vec3 n = ray3d::Normalize(ray3d::Vec3{a.nx, a.ny, a.nz});
    if (std::fabs(std::fabs(ray3d::Dot(n, frame.zAxis)) - 1.0) > 1e-6) {
      flat = false;
      break;
    }
    // The arc's own sweep sign is measured about ITS normal; the path's is about the frame's, and
    // an arc lying in the plane the other way up runs the opposite way round.
    const double dir = ray3d::Dot(n, frame.zAxis) > 0.0 ? 1.0 : -1.0;
    double sweep = static_cast<double>(a.sweepRad) * dir;
    path.start = ucs::WorldToPlane(frame, CurveWorldPointOnArc(a, static_cast<double>(a.startRad)));
    // A rim that goes all the way round has no chord, so it is seamed into halves — the same split
    // every closed curve in the kernel already gets (ADR-045 (d)).
    if (std::fabs(sweep) >= 2.0 * 3.14159265358979323846 - 1e-9) {
      path.segs.push_back(brep::PathSeg{ucs::WorldToPlane(frame, CurveWorldPointOnArc(a, static_cast<double>(a.startRad) + static_cast<double>(a.sweepRad) * 0.5)), sweep * 0.5});
      path.segs.push_back(brep::PathSeg{path.start, sweep * 0.5});
      path.closed = true;
    } else {
      path.segs.push_back(brep::PathSeg{ucs::WorldToPlane(frame, CurveWorldPointOnArc(a, static_cast<double>(a.startRad) + static_cast<double>(a.sweepRad))), sweep});
    }
    break;
  }
  case T::Circle: {
    float cnx = 0.f, cny = 0.f, cnz = 1.f;
    CircleNormalAt(st.drawing.userCircleNormals, i, &cnx, &cny, &cnz);
    const ray3d::Vec3 n = ray3d::Normalize(ray3d::Vec3{cnx, cny, cnz});
    if (std::fabs(std::fabs(ray3d::Dot(n, frame.zAxis)) - 1.0) > 1e-6) {
      flat = false;
      break;
    }
    const CadCircleRecord circle = st.drawing.circle(i);
    const double r = circle.r;
    const ucs::Point2D c = ucs::WorldToPlane(frame, ray3d::Vec3{circle.cx, circle.cy, circle.cz});
    const double kPiLocal = 3.14159265358979323846;
    path.start = ucs::Point2D{c.x + r, c.y};
    path.segs.push_back(brep::PathSeg{ucs::Point2D{c.x - r, c.y}, kPiLocal});
    path.segs.push_back(brep::PathSeg{path.start, kPiLocal});
    path.closed = true;
    break;
  }
  default: {
    const std::size_t b = static_cast<std::size_t>(st.drawing.userPolylineOffsets[i]);
    const std::size_t e = static_cast<std::size_t>(st.drawing.userPolylineOffsets[i + 1]);
    // A polyline's ARC segments come across too (REQ-316 / ADR-047 gave the store per-vertex
    // bulges). The bulge on vertex `v` describes the segment LEAVING it, and `tan(theta/4)` is the
    // DXF convention both stores share — so the included angle is `4*atan(bulge)`, which is exactly
    // what `PathSeg::sweep` wants, sign and all. Converted here rather than in the kernel, because
    // `brep` does not know what a `CadPolyline` is (ADR-050 (a)).
    auto bulgeAt = [&](std::size_t v) {
      return v < st.drawing.userPolylineVertsBulge.size()
                 ? static_cast<double>(st.drawing.userPolylineVertsBulge[v])
                 : 0.0;
    };
    for (std::size_t v = b; v < e && flat; ++v) {
      double off = 0.0;
      const ucs::Point2D q = ucs::WorldToPlane(
          frame,
          ray3d::Vec3{st.drawing.vertex(static_cast<size_t>(v)).x, st.drawing.vertex(static_cast<size_t>(v)).y,
                      st.drawing.vertex(static_cast<size_t>(v)).z},
          &off);
      if (std::fabs(off) > planeTol) {
        flat = false;
        break;
      }
      if (v == b)
        path.start = q;
      else
        path.segs.push_back(brep::PathSeg{q, 4.0 * std::atan(bulgeAt(v - 1))});
    }
    if (flat && i < st.drawing.userPolylineClosed.size() && st.drawing.userPolylineClosed[i] != 0 &&
        path.segs.size() >= 2) {
      path.segs.push_back(brep::PathSeg{path.start, 4.0 * std::atan(bulgeAt(e - 1))});
      path.closed = true;
    }
    break;
  }
  }

  if (!flat) {
    st.polysolidPhase = savedPhase;
    log.push_back("POLYSOLID - that object does not lie in the current work plane, so there is no "
                  "flat path to sweep. Set the UCS to its plane first.");
    return;
  }

  st.polysolidPath = std::move(path);
  brep::Solid solid;
  brep::Problem why = brep::Problem::Ok;
  if (!CadBuildPolysolidFromCommand(st, nullptr, &solid, &why)) {
    st.polysolidPath = brep::Path{};
    st.polysolidPhase = savedPhase;
    log.push_back(std::string("POLYSOLID - ") + brep::ProblemText(why));
    log.push_back(CadPolysolidPromptText(st));
    return;
  }
  const brep::MassProperties mp = brep::ComputeMassProperties(solid);
  PushUndoSnapshot(st, "Create Polysolid");
  st.drawing.cadSolids.push_back(std::make_shared<const brep::Solid>(std::move(solid)));
  st.drawing.cadSolidAttrs.push_back(MakeNewObjectAttrs(st, ObjectLayerKind::Solid, {}));  // REQ-361
  BumpCadGpuCache(st);
  log.push_back(SolidCreatedMessage(brep::PrimitiveKind::Polysolid, mp));
  CancelPolysolidCommand(st);
  st.active = AppCommandState::Kind::None;
}
