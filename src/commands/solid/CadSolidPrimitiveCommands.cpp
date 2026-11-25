#include "CadCommandsInternal.hpp"
#include "engine/StepEngine.hpp"

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
  if (auto* ss = st.sessionAs<stepengine::SolidPrimitiveSession>()) {
    ss->solidKind = brep::PrimitiveKind::None;
    ss->phase = stepengine::SolidPhase::WaitBasePoint;
    ss->solidBase = ray3d::Vec3{};
    ss->solidPendingParam = -1;
    ss->solidPendingIsDiameter = false;
    ss->solidPickValid = false;
    ss->solidBaseAngleRad = 0.0;
    ss->solidInscribed = false;
    ss->solidBaseIsCorner = false;
    ss->solidCornerDx = 0.0;
    ss->solidCornerDy = 0.0;
    for (int i = 0; i < stepengine::SolidPrimitiveSession::kMaxSolidParams; ++i) {
      ss->solidParamValue[i] = 0.0;
      ss->solidParamSet[i] = false;
    }
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
  const auto* ss = st.sessionAs<stepengine::SolidPrimitiveSession>();
  if (!ss || ss->solidKind == brep::PrimitiveKind::None)
    return {};
  const std::string verb = SolidVerbUpper(ss->solidKind);
  if (ss->phase == stepengine::SolidPhase::WaitBasePoint) {
    const char* what = "base centre point";
    if (ss->solidBaseIsCorner)
      what = "first corner";
    else if (ss->solidKind == brep::PrimitiveKind::Sphere || ss->solidKind == brep::PrimitiveKind::Torus)
      what = "centre point";
    return verb + " — " + std::string(what) + " (click, or type X,Y or X,Y,Z):";
  }

  int n = 0;
  const SolidParamSpec* specs = CadSolidParamSpecs(ss->solidKind, &n);
  if (ss->solidPendingParam >= 0 && ss->solidPendingParam < n)
    return verb + " — " + specs[ss->solidPendingParam].label + ":";

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
    if (ss->solidKind == brep::PrimitiveKind::Pyramid)
      s += std::string(", [I]nscribed (now ") + (ss->solidInscribed ? "inscribed" : "circumscribed") + ")";
    s += ". ";
  }

  for (int i = 0; i < n; ++i) {
    if (i)
      s += ", ";
    s += specs[i].letter;
    s += " ";
    s += specs[i].label;
    if (ss->solidParamSet[i])
      s += " = " + TrimNumber(ss->solidParamValue[i]);
  }
  s += ". Enter creates.";
  return s;
}


ucs::Ucs CadSolidPlacementFrameFor(const AppCommandState& st) {
  const auto* ss = st.sessionAs<stepengine::SolidPrimitiveSession>();
  return SolidPlacementFrame(st, ss ? ss->solidBase : ray3d::Vec3{});
}

int CadSolidCurrentPickParam(const AppCommandState& st) {
  const auto* ss = st.sessionAs<stepengine::SolidPrimitiveSession>();
  if (!ss)
    return -1;
  int n = 0;
  const SolidParamSpec* specs = CadSolidParamSpecs(ss->solidKind, &n);
  for (int i = 0; i < n; ++i) {
    if (specs[i].pick == SolidPickKind::Typed || specs[i].optional)
      continue;
    if (!ss->solidParamSet[i])
      return i;
  }
  return -1;
}

void CadResolveSolidPick(AppCommandState& st, const ray3d::Vec3& cursorOnPlane, const ray3d::Ray* ray) {
  auto* ss = st.sessionAs<stepengine::SolidPrimitiveSession>();
  if (!ss)
    return;
  ss->solidPickValid = false;
  if (ss->solidKind == brep::PrimitiveKind::None ||
      ss->phase != stepengine::SolidPhase::WaitParameters)
    return;
  const int picking = CadSolidCurrentPickParam(st);
  if (picking < 0)
    return;
  int specCount = 0;
  const SolidParamSpec* specs = CadSolidParamSpecs(ss->solidKind, &specCount);

  const ucs::Ucs f = CadSolidPlacementFrameFor(st);
  const ray3d::Vec3 d = ray3d::Sub(cursorOnPlane, ss->solidBase);
  const double inPlaneX = ray3d::Dot(d, f.xAxis);
  const double inPlaneY = ray3d::Dot(d, f.yAxis);

  switch (specs[picking].pick) {
  case SolidPickKind::Radius:
    ss->solidPickA = std::sqrt(inPlaneX * inPlaneX + inPlaneY * inPlaneY);
    ss->solidPickAngleRad = std::atan2(inPlaneY, inPlaneX);
    ss->solidPickValid = ss->solidPickA > 1e-9;
    break;
  case SolidPickKind::CornerXY:
    ss->solidPickA = inPlaneX;
    ss->solidPickB = inPlaneY;
    ss->solidPickValid = std::fabs(inPlaneX) > 1e-9 && std::fabs(inPlaneY) > 1e-9;
    break;
  case SolidPickKind::Height: {
    if (!ray || !ray->valid())
      break;
    const ray3d::Vec3 axisDir = f.zAxis;
    const ray3d::Vec3 w0 = ray3d::Sub(ss->solidBase, ray->origin);
    const double b = ray3d::Dot(axisDir, ray->dir);
    const double den = 1.0 - b * b;
    if (!(den > 1e-9))
      break;
    const double dv = ray3d::Dot(axisDir, w0);
    const double ev = ray3d::Dot(ray->dir, w0);
    ss->solidPickA = (b * ev - dv) / den;
    ss->solidPickValid = std::fabs(ss->solidPickA) > 1e-9;
    break;
  }
  case SolidPickKind::Typed:
    break;
  }
  if (!std::isfinite(ss->solidPickA) || !std::isfinite(ss->solidPickB) ||
      !std::isfinite(ss->solidPickAngleRad))
    ss->solidPickValid = false;
}

bool CadBuildSolidFromCommand(const AppCommandState& st, bool applyPick, brep::Solid* out,
                              brep::Problem* outWhy) {
  const auto* ss = st.sessionAs<stepengine::SolidPrimitiveSession>();
  if (!out || !ss || ss->solidKind == brep::PrimitiveKind::None)
    return false;
  int n = 0;
  const SolidParamSpec* specs = CadSolidParamSpecs(ss->solidKind, &n);

  double v[stepengine::SolidPrimitiveSession::kMaxSolidParams] = {0.0, 0.0, 0.0, 0.0};
  bool have[stepengine::SolidPrimitiveSession::kMaxSolidParams] = {false, false, false, false};
  for (int i = 0; i < n; ++i) {
    if (ss->solidParamSet[i]) {
      v[i] = ss->solidParamValue[i];
      have[i] = true;
    } else if (specs[i].optional) {
      v[i] = specs[i].defaultValue;
      have[i] = true;
    }
  }

  double cornerDx = ss->solidCornerDx;
  double cornerDy = ss->solidCornerDy;
  const int picking = CadSolidCurrentPickParam(st);
  if (applyPick && ss->solidPickValid && picking >= 0) {
    if (specs[picking].pick == SolidPickKind::CornerXY) {
      cornerDx = ss->solidPickA;
      cornerDy = ss->solidPickB;
      v[picking] = std::fabs(ss->solidPickA);
      have[picking] = true;
      if (picking + 1 < n) {
        v[picking + 1] = std::fabs(ss->solidPickB);
        have[picking + 1] = true;
      }
    } else {
      v[picking] = ss->solidPickA;
      have[picking] = true;
    }
  }

  for (int i = 0; i < n; ++i) {
    if (!have[i])
      { if (outWhy) *outWhy = brep::Problem::NonFiniteParameter; return false; }
  }

  ray3d::Vec3 origin = ss->solidBase;
  ucs::Ucs frame = SolidPlacementFrame(st, origin);
  if (ss->solidBaseIsCorner) {
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
  switch (ss->solidKind) {
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
    ucs::Ucs baseFrame = ucs::RotatedAboutZ(frame, ss->solidBaseAngleRad * 180.0 / 3.14159265358979323846);
    baseFrame.origin = frame.origin;
    const double baseR = PyramidCircumradius(v[1], sides, ss->solidInscribed);
    const double topR = PyramidCircumradius(v[2], sides, ss->solidInscribed);
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
  auto sess = std::make_unique<stepengine::SolidPrimitiveSession>();
  sess->solidKind = spec->kind;
  sess->phase = stepengine::SolidPhase::WaitBasePoint;
  sess->solidBaseIsCorner = spec->kind == brep::PrimitiveKind::Box || spec->kind == brep::PrimitiveKind::Wedge;
  stepengine::BeginSession(st, std::move(sess), false);
  log.push_back(CadSolidPromptText(st));
}

namespace {

void CommitPromptedSolid(AppCommandState& st, std::vector<std::string>& log) {
  auto* ss = st.sessionAs<stepengine::SolidPrimitiveSession>();
  if (!ss)
    return;
  int n = 0;
  const SolidParamSpec* specs = CadSolidParamSpecs(ss->solidKind, &n);
  const std::string verb = SolidVerbUpper(ss->solidKind);

  std::string missing;
  for (int i = 0; i < n; ++i) {
    if (ss->solidParamSet[i] || specs[i].optional)
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
  const bool built = CadBuildSolidFromCommand(st, /*applyPick=*/false, &solid, &why);

  if (!built) {
    log.push_back(verb + " — " + brep::ProblemText(why));
    return;
  }

  const brep::PrimitiveKind kind = ss->solidKind;
  const brep::MassProperties mp = brep::ComputeMassProperties(solid);
  PushUndoSnapshot(st, std::string("Create ") + brep::PrimitiveKindName(kind));
  st.drawing.cadSolids.push_back(std::make_shared<const brep::Solid>(std::move(solid)));
  st.drawing.cadSolidAttrs.push_back(MakeNewObjectAttrs(st, ObjectLayerKind::Solid, {}));
  BumpCadGpuCache(st);

  log.push_back(SolidCreatedMessage(kind, mp));
  stepengine::EndSession(st);
}

void SetSolidParam(AppCommandState& st, int index, double value, std::vector<std::string>& log) {
  auto* ss = st.sessionAs<stepengine::SolidPrimitiveSession>();
  if (!ss)
    return;
  int n = 0;
  const SolidParamSpec* specs = CadSolidParamSpecs(ss->solidKind, &n);
  if (index < 0 || index >= n)
    return;
  ss->solidParamValue[index] = value;
  ss->solidParamSet[index] = true;
  ss->solidPendingParam = -1;
  log.push_back(SolidVerbUpper(ss->solidKind) + " — " + specs[index].label + " = " + TrimNumber(value) +
                ".");
  log.push_back(CadSolidPromptText(st));
}

} // namespace

bool HandleSolidTextInput(const std::string& lineIn, AppCommandState& st, std::vector<std::string>& log) {
  auto* ss = st.sessionAs<stepengine::SolidPrimitiveSession>();
  if (!ss || ss->solidKind == brep::PrimitiveKind::None)
    return false;
  const std::string line = StringUtil::trimCopy(lineIn);
  const std::string verb = SolidVerbUpper(ss->solidKind);

  if (ss->phase == stepengine::SolidPhase::WaitBasePoint) {
    if (line.empty())
      return false;
    ray3d::Vec3 base{};
    if (!ParseSolidBasePoint(st, line, &base, log, verb.c_str()))
      return true;
    ss->solidBase = base;
    ss->phase = stepengine::SolidPhase::WaitParameters;
    log.push_back(CadSolidPromptText(st));
    return true;
  }

  int n = 0;
  const SolidParamSpec* specs = CadSolidParamSpecs(ss->solidKind, &n);

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

  if (ss->solidPendingParam >= 0) {
    double v = 0.0;
    if (!parseNumber(line, &v)) {
      log.push_back(verb + " — \"" + line + "\" is not a number.");
      log.push_back(CadSolidPromptText(st));
      return true;
    }
    if (ss->solidPendingIsDiameter)
      v *= 0.5;
    ss->solidPendingIsDiameter = false;
    SetSolidParam(st, ss->solidPendingParam, v, log);
    return true;
  }

  const char first = static_cast<char>(std::toupper(static_cast<unsigned char>(line[0])));

  if (ss->solidKind == brep::PrimitiveKind::Pyramid && first == 'I' &&
      StringUtil::trimCopy(line.substr(1)).empty()) {
    ss->solidInscribed = !ss->solidInscribed;
    log.push_back(verb + " — base is now " + (ss->solidInscribed ? "INSCRIBED" : "CIRCUMSCRIBED") + ".");
    log.push_back(CadSolidPromptText(st));
    return true;
  }

  if (first == 'D') {
    const int target = CadSolidCurrentPickParam(st);
    if (target >= 0 && specs[target].pick == SolidPickKind::Radius) {
      const std::string rest = StringUtil::trimCopy(line.substr(1));
      if (rest.empty()) {
        ss->solidPendingParam = target;
        ss->solidPendingIsDiameter = true;
        log.push_back(verb + " — " + specs[target].label + " diameter:");
        return true;
      }
      double d = 0.0;
      if (!parseNumber(rest, &d)) {
        log.push_back(verb + " — \"" + rest + "\" is not a number.");
        log.push_back(CadSolidPromptText(st));
        return true;
      }
      ss->solidPendingParam = target;
      ss->solidPendingIsDiameter = true;
      char buf[64];
      std::snprintf(buf, sizeof(buf), "%.17g", d);
      return HandleSolidTextInput(buf, st, log);
    }
  }

  for (int i = 0; i < n; ++i) {
    if (specs[i].letter != first)
      continue;
    const std::string rest = StringUtil::trimCopy(line.substr(1));
    if (rest.empty()) {
      ss->solidPendingParam = i;
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

  double v = 0.0;
  if (parseNumber(line, &v)) {
    for (int i = 0; i < n; ++i) {
      if (!ss->solidParamSet[i]) {
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
  auto* ss = st.sessionAs<stepengine::SolidPrimitiveSession>();
  if (!ss || ss->solidKind == brep::PrimitiveKind::None)
    return;
  if (ss->phase == stepengine::SolidPhase::WaitBasePoint) {
    ss->solidBase = ray3d::Vec3{static_cast<double>(wx), static_cast<double>(wy),
                                static_cast<double>(CadCommitElevation(st))};
    ss->phase = stepengine::SolidPhase::WaitParameters;
    log.push_back(CadSolidPromptText(st));
    return;
  }

  const int picking = CadSolidCurrentPickParam(st);
  if (picking < 0) {
    log.push_back(SolidVerbUpper(ss->solidKind) + " — every dimension is set. Press Enter to create.");
    return;
  }
  if (!ss->solidPickValid) {
    log.push_back(SolidVerbUpper(ss->solidKind) +
                  " — this dimension cannot be picked from here; type a value, or orbit the view.");
    return;
  }
  int n = 0;
  const SolidParamSpec* specs = CadSolidParamSpecs(ss->solidKind, &n);
  if (specs[picking].pick == SolidPickKind::CornerXY) {
    ss->solidCornerDx = ss->solidPickA;
    ss->solidCornerDy = ss->solidPickB;
    SetSolidParam(st, picking, std::fabs(ss->solidPickA), log);
    if (picking + 1 < n)
      SetSolidParam(st, picking + 1, std::fabs(ss->solidPickB), log);
  } else {
    if (specs[picking].pick == SolidPickKind::Radius)
      ss->solidBaseAngleRad = ss->solidPickAngleRad;
    SetSolidParam(st, picking, ss->solidPickA, log);
  }

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
  const auto* ps = st.sessionAs<stepengine::PolysolidSession>();
  return SolidPlacementFrame(st, ps ? ps->base : ray3d::Vec3{});
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
  if (auto* ps = st.sessionAs<stepengine::PolysolidSession>()) {
    ps->phase = stepengine::PolysolidPhase::WaitFirstPoint;
    ps->path = brep::Path{};
    ps->base = ray3d::Vec3{};
    ps->arcMode = false;
    ps->pending = 0;
  }
}

bool CadBuildPolysolidFromCommand(const AppCommandState& st, const ucs::Point2D* cursor,
                                  brep::Solid* out, brep::Problem* outWhy) {
  const auto* ps = st.sessionAs<stepengine::PolysolidSession>();
  if (!ps)
    return false;
  brep::Path p = ps->path;
  if (cursor) {
    const double sweep =
        ps->arcMode ? TangentArcSweep(PolysolidEndPoint(p), PolysolidEndTangent(p), *cursor)
                    : 0.0;
    p.segs.push_back(brep::PathSeg{*cursor, sweep});
  }
  if (p.segs.empty())
    return false;
  return brep::MakePolysolid(PolysolidFrame(st), p, st.polysolidWidth, st.polysolidHeight,
                             st.polysolidJustify, out, outWhy);
}

std::string CadPolysolidPromptText(const AppCommandState& st) {
  const auto* ps = st.sessionAs<stepengine::PolysolidSession>();
  const auto phase = ps ? ps->phase : stepengine::PolysolidPhase::WaitFirstPoint;
  const bool emptySegs = !ps || ps->path.segs.empty();
  const bool arcMode = ps && ps->arcMode;
  char buf[320];
  const std::string dims = "[H=" + TrimNumber(st.polysolidHeight) + " W=" +
                           TrimNumber(st.polysolidWidth) + " J=" + JustifyName(st.polysolidJustify) +
                           "]";
  switch (phase) {
  case stepengine::PolysolidPhase::WaitObject:
    std::snprintf(buf, sizeof(buf),
                  "POLYSOLID - select a line, arc, circle or polyline to sweep along %s",
                  dims.c_str());
    return buf;
  case stepengine::PolysolidPhase::WaitFirstPoint:
    std::snprintf(buf, sizeof(buf),
                  "POLYSOLID - start point, or Object / Height / Width / Justify %s", dims.c_str());
    return buf;
  case stepengine::PolysolidPhase::WaitNextPoint:
    break;
  }
  std::snprintf(buf, sizeof(buf),
                "POLYSOLID - next point%s, or %s / Close / Undo / Height / Width / Justify, Enter to "
                "finish %s",
                emptySegs ? "" : " (Enter finishes)",
                arcMode ? "Line" : "Arc", dims.c_str());
  return buf;
}

namespace {

void CommitPolysolid(AppCommandState& st, std::vector<std::string>& log) {
  auto* ps = st.sessionAs<stepengine::PolysolidSession>();
  if (!ps)
    return;
  if (ps->path.segs.empty()) {
    log.push_back("POLYSOLID - a wall needs at least two points; Esc cancels.");
    return;
  }
  brep::Solid solid;
  brep::Problem why = brep::Problem::Ok;
  if (!CadBuildPolysolidFromCommand(st, nullptr, &solid, &why)) {
    log.push_back(std::string("POLYSOLID - ") + brep::ProblemText(why));
    log.push_back(CadPolysolidPromptText(st));
    return;
  }
  const brep::MassProperties mp = brep::ComputeMassProperties(solid);
  PushUndoSnapshot(st, "Create Polysolid");
  st.drawing.cadSolids.push_back(std::make_shared<const brep::Solid>(std::move(solid)));
  st.drawing.cadSolidAttrs.push_back(MakeNewObjectAttrs(st, ObjectLayerKind::Solid, {}));
  BumpCadGpuCache(st);
  log.push_back(SolidCreatedMessage(brep::PrimitiveKind::Polysolid, mp));
  stepengine::EndSession(st);
}

void AddPolysolidPoint(AppCommandState& st, const ucs::Point2D& pt, std::vector<std::string>& log) {
  auto* ps = st.sessionAs<stepengine::PolysolidSession>();
  if (!ps)
    return;
  const ucs::Point2D from = PolysolidEndPoint(ps->path);
  if (std::fabs(pt.x - from.x) < 1e-9 && std::fabs(pt.y - from.y) < 1e-9) {
    log.push_back("POLYSOLID - that is the same point as the last one.");
    return;
  }
  double sweep = 0.0;
  if (ps->arcMode) {
    if (ps->path.segs.empty()) {
      log.push_back("POLYSOLID - an arc needs a straight run before it to be tangent to; draw one "
                    "first, or press L for a straight segment.");
      return;
    }
    sweep = TangentArcSweep(from, PolysolidEndTangent(ps->path), pt);
  }
  ps->path.segs.push_back(brep::PathSeg{pt, sweep});
  ps->phase = stepengine::PolysolidPhase::WaitNextPoint;
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
  stepengine::BeginSession(st, std::make_unique<stepengine::PolysolidSession>(), false);
  log.push_back(CadPolysolidPromptText(st));
}

bool HandlePolysolidTextInput(const std::string& lineIn, AppCommandState& st,
                              std::vector<std::string>& log) {
  auto* ps = st.sessionAs<stepengine::PolysolidSession>();
  if (!ps)
    return false;
  const std::string line = StringUtil::trimCopy(lineIn);

  if (ps->pending != 0) {
    const char letter = ps->pending;
    ps->pending = 0;
    if (line.empty()) {
      log.push_back(CadPolysolidPromptText(st));
      return true;
    }
    (void)SetPolysolidSetting(st, letter, line, log);
    log.push_back(CadPolysolidPromptText(st));
    return true;
  }

  if (line.empty()) {
    if (ps->phase == stepengine::PolysolidPhase::WaitFirstPoint) {
      log.push_back("POLYSOLID canceled.");
      stepengine::EndSession(st);
      return true;
    }
    CommitPolysolid(st, log);
    return true;
  }

  const char head = static_cast<char>(std::toupper(static_cast<unsigned char>(line[0])));
  const std::string rest = StringUtil::trimCopy(line.substr(1));
  const bool bareLetter = rest.empty();

  if (head == 'H' || head == 'W' || head == 'J') {
    if (bareLetter) {
      ps->pending = head;
      log.push_back(head == 'J' ? "POLYSOLID - justification (Left / Center / Right)?"
                                : (head == 'H' ? "POLYSOLID - height?" : "POLYSOLID - width?"));
      return true;
    }
    (void)SetPolysolidSetting(st, head, rest, log);
    log.push_back(CadPolysolidPromptText(st));
    return true;
  }

  if (head == 'O' && bareLetter) {
    if (ps->phase == stepengine::PolysolidPhase::WaitNextPoint) {
      log.push_back("POLYSOLID - Object converts something already drawn; it cannot join a run "
                    "already under way.");
      return true;
    }
    ps->phase = stepengine::PolysolidPhase::WaitObject;
    log.push_back(CadPolysolidPromptText(st));
    return true;
  }

  if ((head == 'A' || head == 'L') && bareLetter) {
    ps->arcMode = head == 'A';
    log.push_back(ps->arcMode ? "POLYSOLID - arc segments." : "POLYSOLID - straight segments.");
    log.push_back(CadPolysolidPromptText(st));
    return true;
  }

  if (head == 'C' && bareLetter) {
    if (ps->path.segs.size() < 2) {
      log.push_back("POLYSOLID - a closed wall needs at least three points.");
      return true;
    }
    const ucs::Point2D from = PolysolidEndPoint(ps->path);
    const double sweep =
        ps->arcMode
            ? TangentArcSweep(from, PolysolidEndTangent(ps->path), ps->path.start)
            : 0.0;
    ps->path.segs.push_back(brep::PathSeg{ps->path.start, sweep});
    ps->path.closed = true;
    CommitPolysolid(st, log);
    if (auto* still = st.sessionAs<stepengine::PolysolidSession>()) {
      still->path.segs.pop_back();
      still->path.closed = false;
    }
    return true;
  }

  if (head == 'U' && bareLetter) {
    if (ps->path.segs.empty()) {
      log.push_back("POLYSOLID - nothing to undo yet.");
      return true;
    }
    ps->path.segs.pop_back();
    if (ps->path.segs.empty())
      log.push_back("POLYSOLID - back to the start point.");
    log.push_back(CadPolysolidPromptText(st));
    return true;
  }

  if (ps->phase == stepengine::PolysolidPhase::WaitObject) {
    log.push_back("POLYSOLID - click the line, arc, circle or polyline to sweep along (Esc cancels).");
    return true;
  }
  ray3d::Vec3 pt{};
  if (!ParseSolidBasePoint(st, line, &pt, log, "POLYSOLID"))
    return true;
  if (ps->phase == stepengine::PolysolidPhase::WaitFirstPoint) {
    ps->base = pt;
    ps->path = brep::Path{};
    ps->phase = stepengine::PolysolidPhase::WaitNextPoint;
    log.push_back(CadPolysolidPromptText(st));
    return true;
  }
  const ucs::Point2D local = ucs::WorldToPlane(PolysolidFrame(st), pt);
  AddPolysolidPoint(st, local, log);
  return true;
}

void SubmitPolysolidViewportPick(AppCommandState& st, float wx, float wy,
                                 std::vector<std::string>& log) {
  auto* ps = st.sessionAs<stepengine::PolysolidSession>();
  if (!ps)
    return;
  if (ps->phase == stepengine::PolysolidPhase::WaitObject) {
    CadPolysolidConvertObjectAt(st, wx, wy, log);
    return;
  }
  const ray3d::Vec3 pt{static_cast<double>(wx), static_cast<double>(wy), CadCommitElevation(st)};
  if (ps->phase == stepengine::PolysolidPhase::WaitFirstPoint) {
    ps->base = pt;
    ps->path = brep::Path{};
    ps->phase = stepengine::PolysolidPhase::WaitNextPoint;
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

  auto* ps = st.sessionAs<stepengine::PolysolidSession>();
  if (!ps)
    return;
  const stepengine::PolysolidPhase savedPhase = ps->phase;
  ps->base = anchor;
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
    ps->phase = savedPhase;
    log.push_back("POLYSOLID - that object does not lie in the current work plane, so there is no "
                  "flat path to sweep. Set the UCS to its plane first.");
    return;
  }

  ps->path = std::move(path);
  brep::Solid solid;
  brep::Problem why = brep::Problem::Ok;
  if (!CadBuildPolysolidFromCommand(st, nullptr, &solid, &why)) {
    ps->path = brep::Path{};
    ps->phase = savedPhase;
    log.push_back(std::string("POLYSOLID - ") + brep::ProblemText(why));
    log.push_back(CadPolysolidPromptText(st));
    return;
  }
  const brep::MassProperties mp = brep::ComputeMassProperties(solid);
  PushUndoSnapshot(st, "Create Polysolid");
  st.drawing.cadSolids.push_back(std::make_shared<const brep::Solid>(std::move(solid)));
  st.drawing.cadSolidAttrs.push_back(MakeNewObjectAttrs(st, ObjectLayerKind::Solid, {}));
  BumpCadGpuCache(st);
  log.push_back(SolidCreatedMessage(brep::PrimitiveKind::Polysolid, mp));
  stepengine::EndSession(st);
}
