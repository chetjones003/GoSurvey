// REQ-024 / REQ-354 (GitHub issue #564 §5): the point-entry dynamic input's model. See the header.

#include "CadDynInput.hpp"

#include "CadCommands.hpp"
#include "CadCoordinateFrame.hpp"
#include "StringUtil.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace dyninput {
namespace {

Field FieldFrom(const std::string& text) {
  Field f;
  f.text = text;
  f.locked = !StringUtil::trimCopy(text).empty();
  return f;
}

void PushTransition(Group& g, int landSlot, bool layoutChanged) {
  Transition t;
  t.prev = g.mode;
  t.prevFields = g.f;
  t.landSlot = landSlot;
  t.layoutChanged = layoutChanged;
  t.prevAtTyped = g.atTyped;
  g.history.push_back(t);
}

void MoveFocus(Group& g, int slot) {
  g.focus = slot;
  g.requestFocus = slot;
}

std::string WithoutChar(std::string s, char c) {
  s.erase(std::remove(s.begin(), s.end(), c), s.end());
  return s;
}

// The whole trimmed text is one finite number.
bool ParseNumber(const std::string& raw, double* out) {
  const std::string s = StringUtil::trimCopy(raw);
  if (s.empty())
    return false;
  char* end = nullptr;
  const double v = std::strtod(s.c_str(), &end);
  if (!end || *end != '\0' || !std::isfinite(v))
    return false;
  *out = v;
  return true;
}

bool HasLetter(const std::string& s) {
  for (const char c : s)
    if (std::isalpha(static_cast<unsigned char>(c)))
      return true;
  return false;
}

std::string Fmt(double v) {
  char buf[64];
  std::snprintf(buf, sizeof buf, "%.6f", v);
  return buf;
}

}  // namespace

void Reset(Group& g, Mode initial, bool showZ) {
  g = Group{};
  g.initial = initial;
  g.mode = initial;
  g.showZ = showZ;
}

int FieldCount(const Group& g) {
  if (IsPolar(g.mode))
    return 2;
  return (g.showZ || g.zRevealed) ? 3 : 2;
}

std::string Label(const Group& g, int slot) {
  if (IsPolar(g.mode))
    return slot == 0 ? "Distance" : "Angle";
  static const char* kAxis[3] = {"X", "Y", "Z"};
  const std::string axis = kAxis[slot < 0 ? 0 : (slot > 2 ? 2 : slot)];
  return g.mode == Mode::Relative ? std::string("\xce\x94") + axis : axis;  // U+0394 GREEK CAPITAL DELTA
}

std::string LabelLine(const Group& g) {
  std::string s = IsRelative(g.mode) ? "@ " : "";
  const int n = FieldCount(g);
  for (int i = 0; i < n; ++i) {
    if (i > 0)
      s += IsPolar(g.mode) ? " < " : " ";
    s += Label(g, i);
  }
  return s;
}

void EditText(Group& g, int slot, const std::string& textIn) {
  if (slot < 0 || slot > 2)
    return;
  std::string text = textIn;

  // `@` — the relative form. Consumed at the start of a field: it is never part of a number, and a
  // box still showing it while labelled as an absolute X is exactly what the section complains of.
  const size_t first = text.find_first_not_of(" \t");
  if (first != std::string::npos && text[first] == '@') {
    text.erase(0, first + 1);
    Mode next = g.mode;
    if (g.mode == Mode::Absolute)
      next = Mode::Relative;
    else if (g.mode == Mode::Polar)
      next = Mode::RelativePolar;
    else if (g.mode == Mode::RelativePolar && !g.atTyped)
      next = Mode::Relative;  // LINE's own Distance < Angle: `@` asks for ΔX / ΔY (issue #564 §5)
    if (next != g.mode) {
      const bool layoutChanged = IsPolar(next) != IsPolar(g.mode);
      PushTransition(g, slot, layoutChanged);
      g.mode = next;
      if (layoutChanged)
        g.f = {};  // the old boxes held a distance and an angle; a locked angle must not become a ΔY
    }
    g.atTyped = true;
  }

  // `<` — Distance < Angle. It follows a distance, so it means something only in the first field.
  const size_t lt = text.find('<');
  if (lt != std::string::npos) {
    const std::string left = text.substr(0, lt);
    const std::string right = WithoutChar(WithoutChar(text.substr(lt + 1), '<'), ',');
    if (slot != 0) {
      g.f[static_cast<size_t>(slot)] = FieldFrom(left + right);
      return;
    }
    if (!IsPolar(g.mode)) {
      PushTransition(g, 1, true);
      g.mode = IsRelative(g.mode) ? Mode::RelativePolar : Mode::Polar;
      g.f[2] = Field{};
    }
    g.f[0] = FieldFrom(left);
    g.f[1] = FieldFrom(right);
    MoveFocus(g, 1);
    return;
  }

  // `,` — on to the next field.
  const size_t comma = text.find(',');
  if (comma != std::string::npos) {
    const std::string left = text.substr(0, comma);
    const std::string right = text.substr(comma + 1);
    if (IsPolar(g.mode)) {
      if (slot != 0) {
        g.f[static_cast<size_t>(slot)] = FieldFrom(WithoutChar(text, ','));
        return;
      }
      // A comma after a number in the Distance box makes it a coordinate: absolute X / Y, the
      // command line's own reading of `5,5` — or ΔX / ΔY once `@` was typed (D-2026-09-28-j).
      PushTransition(g, 1, true);
      g.mode = g.atTyped ? Mode::Relative : Mode::Absolute;
      g.f[0] = FieldFrom(left);
      g.f[1] = Field{};
      g.f[2] = Field{};
      MoveFocus(g, 1);
      if (!right.empty())
        EditText(g, 1, right);
      return;
    }
    if (slot == 1 && FieldCount(g) == 2)
      g.zRevealed = true;  // `x,y,` asks for a Z: show its box rather than drop the value
    if (slot + 1 < FieldCount(g)) {
      g.f[static_cast<size_t>(slot)] = FieldFrom(left);
      g.f[static_cast<size_t>(slot + 1)] = Field{};
      MoveFocus(g, slot + 1);
      if (!right.empty())
        EditText(g, slot + 1, right);
      return;
    }
    // A comma in the Z box has nowhere to go. It is kept, so the command refuses the point by name
    // ("too many coordinates") instead of the extra value vanishing.
  }

  g.f[static_cast<size_t>(slot)] = FieldFrom(text);
}

void TypeText(Group& g, const std::string& text) {
  for (const char c : text) {
    const size_t slot = static_cast<size_t>(g.focus);
    const std::string base = g.f[slot].locked ? g.f[slot].text : std::string();
    EditText(g, g.focus, base + c);
    if (g.requestFocus >= 0) {
      g.focus = g.requestFocus;
      g.requestFocus = -1;
    }
  }
}

bool BackspaceAtEmpty(Group& g, int slot) {
  if (g.history.empty() || g.history.back().landSlot != slot)
    return false;
  const Transition t = g.history.back();
  g.history.pop_back();
  g.mode = t.prev;
  g.atTyped = t.prevAtTyped;
  if (t.layoutChanged) {
    // What was typed before the mode character (the distance, or the X) stays where it is; the
    // boxes from the landing field on get back what they held before the change.
    for (size_t i = static_cast<size_t>(slot); i < 3; ++i)
      g.f[i] = t.prevFields[i];
    if (slot > 0)
      MoveFocus(g, slot - 1);
  }
  return true;
}

std::string Compose(const Group& g, const Live& live, bool directDistance) {
  const int n = FieldCount(g);
  bool anyLocked = false;
  for (int i = 0; i < n; ++i)
    anyLocked = anyLocked || g.f[static_cast<size_t>(i)].locked;
  if (!anyLocked)
    return {};

  double num = 0.0;
  const std::string t0 = StringUtil::trimCopy(g.f[0].text);
  // A keyword typed into the first box (`C`, `U`, `END`, `2P`) answers the prompt; it is not a
  // coordinate, so it goes to the command as typed — as the single field always sent it.
  if (g.f[0].locked && !ParseNumber(t0, &num) && HasLetter(t0))
    return t0;

  const std::string at = IsRelative(g.mode) ? "@" : "";
  const auto value = [&](int i) {
    const Field& f = g.f[static_cast<size_t>(i)];
    return f.locked ? StringUtil::trimCopy(f.text) : Fmt(live.v[i]);
  };

  if (!IsPolar(g.mode)) {
    std::string s = at + value(0) + "," + value(1);
    // An untouched Z box reads the cursor, which sits on the work plane — where the command puts a
    // point with no Z anyway — so only a typed Z is sent.
    if (n == 3 && g.f[2].locked)
      s += "," + StringUtil::trimCopy(g.f[2].text);
    return s;
  }

  if (directDistance && g.f[0].locked && !g.f[1].locked)
    return t0;

  double dist = live.v[0];
  double mathDeg = live.v[1];
  // The command line has no polar grammar, so an unreadable box is sent as typed and the command
  // refuses it by name rather than this quietly substituting the cursor (REQ-201).
  if (g.f[0].locked && !ParseNumber(g.f[0].text, &dist))
    return at + value(0) + "<" + value(1);
  if (g.f[1].locked) {
    double typed = 0.0;
    if (!ParseNumber(g.f[1].text, &typed))
      return at + value(0) + "<" + value(1);
    // The box shows, and so is typed as, a bearing clockwise from north (REQ-021 / ADR-004) — the
    // convention REQ-024's distance / angle pair already used.
    mathDeg = 90.0 - typed;
  }
  constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
  return at + Fmt(dist * std::cos(mathDeg * kDegToRad)) + "," + Fmt(dist * std::sin(mathDeg * kDegToRad));
}

}  // namespace dyninput

// REQ-024: the active prompt takes a coordinate point (moved from CadUi.cpp so the headless driver
// asks the same question the viewport does).
bool CadCommandExpectsPointEntry(const AppCommandState& cmd) {
  using K = AppCommandState::Kind;
  switch (cmd.active) {
  case K::Line: {
    using LP = AppCommandState::LinePhase;
    using SAP = AppCommandState::SegmentAnglePickPhase;
    if (cmd.linePhase == LP::NeedFirstPoint) return true;
    if (cmd.linePhase == LP::NeedNextPoint)
      return !(cmd.segmentAngleKeyboardAwaitBearing || cmd.segmentAngleLockActive ||
               cmd.segmentAnglePickPhase != SAP::Idle);
    return false;
  }
  case K::Polyline: {
    using PP = AppCommandState::PolylinePhase;
    using SAP = AppCommandState::SegmentAnglePickPhase;
    if (cmd.polylinePhase == PP::NeedFirstPoint) return true;
    if (cmd.polylinePhase == PP::NeedNextPoint)
      return !(cmd.segmentAngleKeyboardAwaitBearing || cmd.segmentAngleLockActive ||
               cmd.segmentAnglePickPhase != SAP::Idle);
    return false;
  }
  case K::Arc: return true;
  case K::Rect: return true;  // both corners are point prompts (REQ-024/REQ-053)
  case K::Ellipse: {
    using EP = AppCommandState::EllipsePhase;
    return cmd.ellPhase == EP::WaitCenter || cmd.ellPhase == EP::WaitMajorEnd;
  }
  case K::Text:
    return cmd.textPhase == AppCommandState::TextCmdPhase::WaitInsertion;
  case K::Mtext: {
    using MP = AppCommandState::MtextPhase;
    return cmd.mtextPhase == MP::WaitCorner1 || cmd.mtextPhase == MP::WaitCorner2;
  }
  case K::DimAligned:
  case K::DimLinear: return true;
  case K::DimAngular: {
    using DAP = AppCommandState::DimAngularPhase;
    return cmd.dimAngularPhase == DAP::WaitVertex || cmd.dimAngularPhase == DAP::WaitRay1 ||
           cmd.dimAngularPhase == DAP::WaitRay2;
  }
  case K::IdPoint: return true;
  case K::SurveyInverse: return true;
  case K::Dist: return true;
  // REQ-074. Missing here as well as from the viewport click dispatch, so SURFELEV got neither
  // typed-point entry nor a usable pick — the same pre-existing TASK-055 gap, in the second of the
  // two lists a point-picking command has to appear in.
  case K::SurfaceElevGrade: return true;
  case K::WaterDrop: return true;
  case K::Catchment: return true;
  case K::SwapTinEdge: return true;
  case K::AddTinPoint: return true;
  case K::DelTinPoint: return true;
  case K::MoveTinPoint: return true;
  case K::DelTinLine: return true;
  case K::QuickProfile: return true;
  case K::GeoMarkPoint: return true;       // REQ-359
  case K::GeoReorientMarker: return true;  // REQ-359
  case K::DrawingSettingsPick: return true;  // REQ-360
  case K::GeoCaptureArea:  // REQ-364: Pick Area's corners; nothing to type while it captures
    return cmd.geoCmdPhase != AppCommandState::GeoCmdPhase::Capturing;
  // REQ-154. The second of the two lists a point-picking command has to appear in — UCS was missing
  // from both, so it had neither dynamic input nor a working click. Same phases that
  // ViewportClickRouteFor routes: everything that takes a coordinate, and nothing that wants a
  // keyword or a number.
  case K::Ucs: {
    using UPh = AppCommandState::UcsPhase;
    return cmd.ucsPhase == UPh::WaitOriginOrOption || cmd.ucsPhase == UPh::WaitXAxisPoint ||
           cmd.ucsPhase == UPh::WaitXyPoint || cmd.ucsPhase == UPh::WaitRotationAngleP1 ||
           cmd.ucsPhase == UPh::WaitRotationAngleP2 || cmd.ucsPhase == UPh::WaitZAxisOrigin ||
           cmd.ucsPhase == UPh::WaitZAxisPoint;
  }
  case K::Circle: {
    using CP = AppCommandState::CirclePhase;
    return cmd.circlePhase == CP::WaitCenterOrMode || cmd.circlePhase == CP::ThreeP_WaitP1 ||
           cmd.circlePhase == CP::ThreeP_WaitP2 || cmd.circlePhase == CP::ThreeP_WaitP3;
  }
  case K::Move:
  case K::Copy: {
    using MP = AppCommandState::ModifyPhase;
    return cmd.modifyPhase == MP::NeedBase || cmd.modifyPhase == MP::NeedDestination;
  }
  case K::Scale: {
    using MP = AppCommandState::ModifyPhase;
    using SP = AppCommandState::ScalePhase;
    if (cmd.modifyPhase == MP::NeedBase) return true;
    if (cmd.modifyPhase == MP::NeedDestination)
      return cmd.scalePhase == SP::Ref_WaitP1 || cmd.scalePhase == SP::Ref_WaitP2 ||
             cmd.scalePhase == SP::NewLength_WaitP2;
    return false;
  }
  case K::Rotate: {
    using RP = AppCommandState::RotatePhase;
    return cmd.rotatePhase == RP::NeedBase || cmd.rotatePhase == RP::Ref_WaitP1 ||
           cmd.rotatePhase == RP::Ref_WaitP2 || cmd.rotatePhase == RP::AnglePoints_WaitP1 ||
           cmd.rotatePhase == RP::AnglePoints_WaitP2;
  }
  case K::Trim: {
    using TP = AppCommandState::TrimPhase;
    return cmd.trimPhase == TP::CuttingLine_WaitP1 || cmd.trimPhase == TP::CuttingLine_WaitP2;
  }
  case K::Mirror: {
    using MirP = AppCommandState::MirrorPhase;
    return cmd.mirrorPhase == MirP::NeedP1 || cmd.mirrorPhase == MirP::NeedP2;
    // NeedEraseAnswer is a Yes/No text prompt, not a point (HandleMirrorText).
  }
  case K::Stretch: {
    // REQ-103 step 5. Base and destination are both real points (typed or picked), so STRETCH
    // gets the same dynamic-input prompt MOVE/COPY do — it was omitted here, the second of the
    // two lists a point-picking command has to appear in (TASK-099 F2).
    using MP = AppCommandState::ModifyPhase;
    return cmd.modifyPhase == MP::NeedBase || cmd.modifyPhase == MP::NeedDestination;
  }
  case K::InsertBlock: {
    using IPh = AppCommandState::InsertBlockPhase;
    return cmd.insertBlockPhase == IPh::WaitInsertPoint || cmd.insertBlockPhase == IPh::WaitScale;
  }
  // REQ-354 / GitHub issue #564 section 5: PIPERUN's start and next points are point prompts like
  // any other (its size and wall prompts are not). It was missing, so PIPERUN had a single field.
  case K::PipeRun: {
    using PRP = AppCommandState::PipeRunPhase;
    return cmd.pipeRunPhase == PRP::WaitFirstPoint || cmd.pipeRunPhase == PRP::WaitNextPoint;
  }
  default:
    return false;
  }
}

// REQ-154 / REQ-024. The two UCS axis prompts show a POLAR pair — distance and angle — because what
// those prompts ask for is a DIRECTION. Returns false, and leaves the output alone, for every prompt
// that is not one of them.
bool CadUcsPolarPromptBase(const AppCommandState& cmd, ray3d::Vec3* baseWorld) {
  if (cmd.active != AppCommandState::Kind::Ucs || !baseWorld)
    return false;
  using UPh = AppCommandState::UcsPhase;
  switch (cmd.ucsPhase) {
  case UPh::WaitXAxisPoint:
  case UPh::WaitXyPoint:
    // Both measure from the ORIGIN, not from each other — one reference for both boxes, so the
    // second prompt does not silently re-base the angle the first one showed.
    *baseWorld = cmd.ucsPendingOrigin;
    return true;
  case UPh::WaitRotationAngleP2:
    *baseWorld = cmd.ucsAngleBasePoint;
    return true;
  default:
    return false;
  }
}

bool CadViewIsPlanToActiveUcs(const AppCommandState& cmd) {
  const ray3d::Vec3 fwd = CadViewCamera(cmd).ForwardWorld();
  return ray3d::Dot(fwd, cmd.activeUcs.zAxis) < -(1.0 - 1e-6);
}

CadDynInputPrompt CadDynInputPromptFor(const AppCommandState& cmd) {
  CadDynInputPrompt p;
  p.pointEntry = CadCommandExpectsPointEntry(cmd);
  if (!p.pointEntry)
    return p;
  if (CadUcsPolarPromptBase(cmd, &p.ucsPolarBase)) {
    p.ucsPolar = true;
    return p;
  }
  const bool modelSpace = cmd.activeSpaceIndex < 0 || InFloatingModelSpace(cmd);
  p.showZ = modelSpace && !CadViewIsPlanToActiveUcs(cmd);

  // The base each command's own parser measures `@` from, so the live ΔX / ΔY are exactly the
  // numbers that would be typed.
  const auto base = [&](float lx, float ly, float z) {
    double wx = 0.0, wy = 0.0;
    CadCoord::WorldFromLocal(cmd, lx, ly, &wx, &wy);
    p.haveBase = true;
    p.baseWorld = ray3d::Vec3{wx, wy, static_cast<double>(z)};
  };
  using K = AppCommandState::Kind;
  switch (cmd.active) {
  case K::Line:
    if (cmd.linePhase == AppCommandState::LinePhase::NeedNextPoint) {
      base(cmd.anchorX, cmd.anchorY, cmd.anchorZ);
      p.initialMode = dyninput::Mode::RelativePolar;  // REQ-024: an anchored segment
    }
    break;
  case K::Polyline:
    if (cmd.polylinePhase == AppCommandState::PolylinePhase::NeedNextPoint) {
      base(cmd.anchorX, cmd.anchorY, cmd.anchorZ);
      p.initialMode = dyninput::Mode::RelativePolar;
    }
    break;
  case K::Move:
  case K::Copy:
  case K::Stretch:
    if (cmd.modifyPhase == AppCommandState::ModifyPhase::NeedDestination)
      base(cmd.modifyBaseX, cmd.modifyBaseY, cmd.modifyBaseZ);
    break;
  case K::Rect:
    if (cmd.rectPhase == AppCommandState::RectPhase::WaitSecondCorner)
      base(cmd.rectX1, cmd.rectY1, CadWorkPlaneElevation(cmd));
    break;
  case K::PipeRun:
    if (cmd.pipeRunPhase == AppCommandState::PipeRunPhase::WaitNextPoint && cmd.pipeRunDraftVerts.size() >= 3) {
      const size_t n = cmd.pipeRunDraftVerts.size();
      base(static_cast<float>(cmd.pipeRunDraftVerts[n - 3]), static_cast<float>(cmd.pipeRunDraftVerts[n - 2]),
           static_cast<float>(cmd.pipeRunDraftVerts[n - 1]));
      p.initialMode = dyninput::Mode::RelativePolar;  // a rubber-banded segment, like LINE's
      p.directDistance = cmd.pipeRunCompassOn;         // REQ-346: the compass owns the direction
    }
    break;
  default:
    break;
  }
  return p;
}

dyninput::Live CadDynInputLive(const AppCommandState& cmd, const CadDynInputPrompt& prompt, dyninput::Mode mode,
                               double wx, double wy, double wz) {
  using dyninput::Mode;
  dyninput::Live live;
  const ucs::Ucs& u = cmd.activeUcs;
  const bool world = ucs::IsWorld(u);
  const ray3d::Vec3 cursor{wx, wy, wz};
  // Absolute readings are in the frame a typed point is read in (REQ-154): world under the WCS,
  // the active UCS otherwise — so an untouched box commits exactly the number it shows.
  const ray3d::Vec3 abs = world ? cursor : ucs::WorldToUcs(u, cursor);
  ray3d::Vec3 rel{0.0, 0.0, 0.0};
  if (prompt.haveBase) {
    const ray3d::Vec3 d = ray3d::Sub(cursor, prompt.baseWorld);
    rel = world ? d : ucs::WorldVectorToUcs(u, d);
  }
  constexpr double kRadToDeg = 180.0 / 3.14159265358979323846;
  const auto polar = [&](const ray3d::Vec3& v) {
    live.v[0] = std::hypot(v.x, v.y);
    double a = std::atan2(v.y, v.x) * kRadToDeg;
    if (a < 0.0)
      a += 360.0;
    live.v[1] = a;
  };
  switch (mode) {
  case Mode::Absolute:
    live.v[0] = abs.x;
    live.v[1] = abs.y;
    live.v[2] = abs.z;
    break;
  case Mode::Relative:
    live.v[0] = rel.x;
    live.v[1] = rel.y;
    live.v[2] = rel.z;
    break;
  case Mode::Polar:
    polar(abs);
    break;
  case Mode::RelativePolar:
    polar(rel);
    break;
  }
  return live;
}
