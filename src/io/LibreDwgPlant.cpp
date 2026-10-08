#include "LibreDwgPlant.hpp"

#include "util/ray3d.hpp"
#include "util/ucs.hpp"

#include <cmath>
#include <cstring>

#include <dwg.h>

/// Field layouts below were read against AutoCAD 2027 on `samples/example-piping-system.dwg`: each
/// part's `entget` names the fields (DXF codes in brackets), exploding the part gives the INSERT or
/// solid AutoCAD itself produces, and the decoded values match both to full precision. The DWG stream
/// carries the same fields, in the same order, as DWG bit types; handles live in the handle stream
/// and strings in the R2007+ string stream, so neither appears inline.
///
///   AcPpDb3dPart (every part)   BL [90]  BL ports [90], per port: BL [90]  T [1]  3BD [10]  3BD [11]
///                               B [290]  BD [40]  BD [40]  T [1]  T [2]  B [290]; then B [290]  B [290]
///                               3BD [10]
///   ACPPPIPEINLINEASSET         BD [40]  3BD origin [10]  3BD X axis [210]  3BD Z axis [211]  BL [90]
///                               handle: the catalog block record [340]
///   ACPPCONNECTOR               3BD origin [10]  3BD X axis [210]  3BD Z axis [211]  BL sub-parts [90],
///                               then per sub-part its class name (string stream) and fields:
///     AcPpDb3dBlockSubPart        B [290]  BD [40]  12 BD [40] (3x4 placement, rows R|t)
///                                 handle: its block record [340], in sub-part order
///     AcPpDb3dBoltSetSubPart      B [290]  6 BD [40]  B [290]  (when set) 3BD [12]  3BD [13]
///     AcPpDb3dWeldSubPart         B [290]  5 BD [40]  B [290]  (when set) 3BD [12]  3BD [13]
///   ACPPPIPE                    3BD start [10]  3BD end [11]  BD radius [41]  BD [42]  B [290]
///                               BD start cut-back [46]  BD end cut-back [47] (the solid AutoCAD
///                               explodes to runs from start + [46] to end - [47])
///
/// The only placement handles a part carries are catalog block records, so the block handles are
/// the handle stream's block-record references, in order.
namespace libredwgplant {

namespace {

using ray3d::Vec3;

/// Reads DWG bit codes MSB-first from \ref Dwg_Object::unknown_bits. A read past the end sets
/// \ref ok false and returns zeros, so a decode can run to completion and be judged once.
class BitReader {
 public:
  BitReader(const unsigned char* data, size_t numBits) : data_(data), numBits_(numBits) {}

  size_t pos = 0;
  bool ok = true;

  unsigned Bit() {
    if (pos >= numBits_) {
      ok = false;
      return 0;
    }
    // LibreDWG's `bit_read_bits` keeps whole bytes MSB-first but packs the final partial byte's bits
    // LSB-first (its encoder writes them back the same way); read that tail accordingly, or the last
    // few bits of every handle stream come back as zero.
    const bool tail = pos >= (numBits_ & ~static_cast<size_t>(7));
    const unsigned shift = tail ? static_cast<unsigned>(pos & 7) : 7u - static_cast<unsigned>(pos & 7);
    const unsigned v = (data_[pos >> 3] >> shift) & 1u;
    ++pos;
    return v;
  }
  unsigned Bits(int n) {
    unsigned v = 0;
    for (int i = 0; i < n; ++i)
      v = (v << 1) | Bit();
    return v;
  }
  unsigned RC() { return Bits(8); }
  unsigned RS() {
    const unsigned lo = RC();
    return lo | (RC() << 8);
  }
  unsigned RL() {
    const unsigned lo = RS();
    return lo | (RS() << 16);
  }
  double RD() {
    unsigned char b[8];
    for (unsigned char& c : b)
      c = static_cast<unsigned char>(RC());
    double d = 0.0;
    std::memcpy(&d, b, sizeof d);
    return d;
  }
  unsigned BS() {
    switch (Bits(2)) {
    case 0: return RS();
    case 1: return RC();
    case 2: return 0;
    default: return 256;
    }
  }
  unsigned BL() {
    switch (Bits(2)) {
    case 0: return RL();
    case 1: return RC();
    case 2: return 0;
    default: ok = false; return 0;
    }
  }
  double BD() {
    switch (Bits(2)) {
    case 0: return RD();
    case 1: return 1.0;
    case 2: return 0.0;
    default: ok = false; return 0.0;
    }
  }
  Vec3 BD3() {
    Vec3 v;
    v.x = BD();
    v.y = BD();
    v.z = BD();
    return v;
  }
  /// An R2007+ string-stream string: BS length, then that many UTF-16 code units (Plant class names
  /// are ASCII; anything wider is kept as '?').
  std::string TU() {
    const unsigned n = BS();
    std::string s;
    for (unsigned i = 0; i < n && ok; ++i) {
      const unsigned c = RS();
      s.push_back(c < 0x80 ? static_cast<char>(c) : '?');
    }
    return s;
  }

 private:
  const unsigned char* data_;
  size_t numBits_;
};

/// Where the entity's own fields, strings and handles sit inside `unknown_bits` (which starts right
/// after the common entity data). Positions are bit offsets into `unknown_bits`.
struct Streams {
  size_t fieldsEnd = 0;  ///< First bit after the last data field: the string stream, or the has-strings flag.
  bool hasStrings = false;
  size_t stringsStart = 0;
  size_t handlesStart = 0;
  size_t handlesEnd = 0;
};

/// Mirrors LibreDWG's `obj_string_stream`: the data stream ends with a has-strings flag; when set,
/// the string stream's size sits in the 16 (or 32) bits before the flag and the stream itself
/// before that.
bool LocateStreams(const Dwg_Object* obj, Streams* s) {
  const size_t total = obj->num_unknown_bits;
  const size_t objectBits = static_cast<size_t>(obj->size) * 8;
  if (obj->unknown_bits == nullptr || total == 0 || objectBits < total)
    return false;
  const size_t base = objectBits - total;  // object-frame bit where unknown_bits starts
  if (obj->bitsize <= base || obj->hdlpos < base || obj->hdlpos - base > total)
    return false;
  const size_t flag = obj->bitsize - base - 1;
  BitReader r(reinterpret_cast<const unsigned char*>(obj->unknown_bits), total);
  r.pos = flag;
  s->hasStrings = r.Bit() != 0;
  s->fieldsEnd = flag;
  if (s->hasStrings) {
    if (flag < 16)
      return false;
    r.pos = flag - 16;
    size_t size = r.RS();
    size_t sizeStart = flag - 16;
    if (size & 0x8000u) {
      if (flag < 32)
        return false;
      r.pos = flag - 32;
      size = (size & 0x7FFFu) | (static_cast<size_t>(r.RS()) << 15);
      sizeStart = flag - 32;
    }
    if (!r.ok || size > sizeStart)
      return false;
    s->stringsStart = sizeStart - size;
    s->fieldsEnd = s->stringsStart;
  }
  s->handlesStart = obj->hdlpos - base;
  s->handlesEnd = total;
  return r.ok;
}

/// Every handle reference in the handle stream, resolved to an absolute handle (codes 6/8/A/C are
/// offsets from the entity's own handle).
std::vector<unsigned long long> ReadHandles(const Dwg_Object* obj, const Streams& s) {
  std::vector<unsigned long long> out;
  BitReader r(reinterpret_cast<const unsigned char*>(obj->unknown_bits), s.handlesEnd);
  r.pos = s.handlesStart;
  const unsigned long long self = obj->handle.value;
  while (r.pos + 8 <= s.handlesEnd) {
    const unsigned code = r.Bits(4);
    const unsigned count = r.Bits(4);
    if (r.pos + 8u * count > s.handlesEnd)
      break;
    unsigned long long value = 0;
    for (unsigned i = 0; i < count; ++i)
      value = (value << 8) | r.RC();
    switch (code) {
    case 0x6: value = self + 1; break;
    case 0x8: value = self - 1; break;
    case 0xA: value = self + value; break;
    case 0xC: value = self - value; break;
    default: break;
    }
    out.push_back(value);
  }
  return out;
}

/// Catalog block records among the entity's handles, in stream order (the model/paper space
/// records would be an owner, never a placed block).
std::vector<unsigned long long> BlockRecordHandles(const Dwg_Data* dwg, const Dwg_Object* obj, const Streams& s) {
  std::vector<unsigned long long> out;
  const Dwg_Object* modelSpace = dwg_model_space_object(const_cast<Dwg_Data*>(dwg));
  const Dwg_Object* paperSpace = dwg_paper_space_object(const_cast<Dwg_Data*>(dwg));
  for (const unsigned long long h : ReadHandles(obj, s)) {
    if (h == 0)
      continue;
    const Dwg_Object* o = dwg_resolve_handle(dwg, h);
    if (o == nullptr || o->fixedtype != DWG_TYPE_BLOCK_HEADER || o == modelSpace || o == paperSpace)
      continue;
    out.push_back(h);
  }
  return out;
}

/// The entity's data and string streams, read in step.
struct Fields {
  Fields(const Dwg_Object* obj, const Streams& s)
      : data(reinterpret_cast<const unsigned char*>(obj->unknown_bits), obj->num_unknown_bits),
        strings(reinterpret_cast<const unsigned char*>(obj->unknown_bits), obj->num_unknown_bits),
        hasStrings(s.hasStrings) {
    strings.pos = s.stringsStart;
  }
  BitReader data;
  BitReader strings;
  bool hasStrings;

  std::string Text() {
    if (!hasStrings) {
      data.ok = false;
      return {};
    }
    return strings.TU();
  }
  /// The data ended exactly where the stream says it does — the check that keeps a layout this
  /// reader does not know from being read as a placement.
  [[nodiscard]] bool EndsAt(const Streams& s) const { return data.ok && strings.ok && data.pos == s.fieldsEnd; }
};

void SkipPartHeader(Fields& f) {
  BitReader& r = f.data;
  (void)r.BL();
  const unsigned ports = r.BL();
  for (unsigned i = 0; i < ports && r.ok; ++i) {
    (void)r.BL();
    (void)f.Text();
    (void)r.BD3();
    (void)r.BD3();
    (void)r.Bit();
    (void)r.BD();
    (void)r.BD();
    (void)f.Text();
    (void)f.Text();
    (void)r.Bit();
  }
  (void)r.Bit();
  (void)r.Bit();
  (void)r.BD3();
}

/// The frame a fitting or connector places its block in: columns X, Z x X, Z.
bool FrameRotation(const Vec3& xAxis, const Vec3& zAxis, double rot[9]) {
  const Vec3 x = ray3d::Normalize(xAxis);
  const Vec3 z = ray3d::Normalize(zAxis);
  if (ray3d::Length(x) < 0.5 || ray3d::Length(z) < 0.5 || std::fabs(ray3d::Dot(x, z)) > 1e-6)
    return false;
  const Vec3 y = ray3d::Cross(z, x);
  const double m[9] = {x.x, y.x, z.x, x.y, y.y, z.y, x.z, y.z, z.z};
  std::memcpy(rot, m, sizeof m);
  return true;
}

bool DecodeFitting(const Dwg_Data* dwg, const Dwg_Object* obj, const Streams& s, Part* out, std::string* why) {
  Fields f(obj, s);
  SkipPartHeader(f);
  BitReader& r = f.data;
  (void)r.BD();
  BlockPlacement at;
  at.origin = r.BD3();
  const Vec3 xAxis = r.BD3();
  const Vec3 zAxis = r.BD3();
  (void)r.BL();
  if (!f.EndsAt(s)) {
    *why = "fitting data layout not recognized";
    return false;
  }
  if (!FrameRotation(xAxis, zAxis, at.rot)) {
    *why = "fitting axes are not orthonormal";
    return false;
  }
  const std::vector<unsigned long long> blocks = BlockRecordHandles(dwg, obj, s);
  if (blocks.size() != 1) {
    *why = "fitting does not name exactly one catalog block";
    return false;
  }
  at.blockHandle = blocks.front();
  out->kind = Part::Kind::Fitting;
  out->blocks = {at};
  return true;
}

/// Bolt sets and welds share a tail: a flag, and when it is set two more points.
void SkipFlaggedPointPair(BitReader& r) {
  if (r.Bit() != 0) {
    (void)r.BD3();
    (void)r.BD3();
  }
}

bool DecodeConnector(const Dwg_Data* dwg, const Dwg_Object* obj, const Streams& s, Part* out, std::string* why) {
  Fields f(obj, s);
  SkipPartHeader(f);
  BitReader& r = f.data;
  const Vec3 origin = r.BD3();
  const Vec3 xAxis = r.BD3();
  const Vec3 zAxis = r.BD3();
  const unsigned subParts = r.BL();
  double frame[9];
  if (!r.ok || !FrameRotation(xAxis, zAxis, frame)) {
    *why = "connector frame not recognized";
    return false;
  }
  std::vector<BlockPlacement> placed;
  for (unsigned i = 0; i < subParts && r.ok; ++i) {
    const std::string kind = f.Text();
    if (kind == "AcPpDb3dBlockSubPart") {
      (void)r.Bit();
      (void)r.BD();
      double m[12];
      for (double& v : m)
        v = r.BD();
      // world = F (M p + t) + origin, so R = F M and the translation is F t + origin.
      BlockPlacement at;
      for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col)
          at.rot[row * 3 + col] = frame[row * 3 + 0] * m[0 * 4 + col] + frame[row * 3 + 1] * m[1 * 4 + col] +
                                  frame[row * 3 + 2] * m[2 * 4 + col];
      const Vec3 t{m[3], m[7], m[11]};
      at.origin = ray3d::Add(origin, Vec3{frame[0] * t.x + frame[1] * t.y + frame[2] * t.z,
                                          frame[3] * t.x + frame[4] * t.y + frame[5] * t.z,
                                          frame[6] * t.x + frame[7] * t.y + frame[8] * t.z});
      placed.push_back(at);
    } else if (kind == "AcPpDb3dBoltSetSubPart") {
      (void)r.Bit();
      for (int k = 0; k < 6; ++k)
        (void)r.BD();
      SkipFlaggedPointPair(r);
    } else if (kind == "AcPpDb3dWeldSubPart") {
      (void)r.Bit();
      for (int k = 0; k < 5; ++k)
        (void)r.BD();
      SkipFlaggedPointPair(r);
    } else {
      *why = "connector sub-part '" + kind + "' not understood";
      return false;
    }
  }
  if (!f.EndsAt(s)) {
    *why = "connector data layout not recognized";
    return false;
  }
  const std::vector<unsigned long long> blocks = BlockRecordHandles(dwg, obj, s);
  if (blocks.size() != placed.size()) {
    *why = "connector block sub-parts and block references disagree";
    return false;
  }
  for (size_t i = 0; i < placed.size(); ++i)
    placed[i].blockHandle = blocks[i];
  out->kind = Part::Kind::Connector;
  out->blocks = std::move(placed);
  return true;
}

bool DecodePipe(const Dwg_Object* obj, const Streams& s, Part* out, std::string* why) {
  Fields f(obj, s);
  SkipPartHeader(f);
  BitReader& r = f.data;
  out->pipeStart = r.BD3();
  out->pipeEnd = r.BD3();
  out->pipeRadius = r.BD();
  (void)r.BD();
  (void)r.Bit();
  out->pipeStartTrim = r.BD();
  out->pipeEndTrim = r.BD();
  if (!f.EndsAt(s)) {
    *why = "pipe data layout not recognized";
    return false;
  }
  const double length = ray3d::Length(ray3d::Sub(out->pipeEnd, out->pipeStart));
  if (!(out->pipeRadius > 0.0) || out->pipeStartTrim < 0.0 || out->pipeEndTrim < 0.0 ||
      !(length > out->pipeStartTrim + out->pipeEndTrim)) {
    *why = "pipe has no length or radius";
    return false;
  }
  out->kind = Part::Kind::Pipe;
  out->blocks.clear();
  return true;
}

}  // namespace

std::string PlantClassName(const _dwg_struct* dwg, const _dwg_object* obj) {
  if (dwg == nullptr || obj == nullptr || obj->type < 500)
    return {};
  const unsigned idx = obj->type - 500u;
  if (idx >= dwg->num_classes || dwg->dwg_class == nullptr || dwg->dwg_class[idx].dxfname == nullptr)
    return {};
  const std::string name = dwg->dwg_class[idx].dxfname;
  return name.rfind("ACPP", 0) == 0 ? name : std::string();
}

bool DecodePart(const _dwg_struct* dwg, const _dwg_object* obj, Part* out, std::string* why) {
  const std::string name = PlantClassName(dwg, obj);
  if (dwg->header.version < R_2007) {
    *why = "Plant 3D entity in a pre-R2007 drawing (no string stream) not supported";
    return false;
  }
  Streams s;
  if (name.empty() || obj->fixedtype != DWG_TYPE_UNKNOWN_ENT || !LocateStreams(obj, &s)) {
    *why = "not a decodable Plant 3D entity";
    return false;
  }
  if (name == "ACPPPIPEINLINEASSET")
    return DecodeFitting(dwg, obj, s, out, why);
  if (name == "ACPPCONNECTOR")
    return DecodeConnector(dwg, obj, s, out, why);
  if (name == "ACPPPIPE")
    return DecodePipe(obj, s, out, why);
  *why = "no stored solid or placement for this Plant 3D class";
  return false;
}

bool PlaceSolid(const brep::Solid& local, const BlockPlacement& at, const brep::Vec3& docOrigin, brep::Solid* out) {
  const double* m = at.rot;
  const double det = m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) +
                     m[2] * (m[3] * m[7] - m[4] * m[6]);
  if (std::fabs(det - 1.0) > 1e-6)
    return false;
  Vec3 axis{};
  double angle = 0.0;
  if (!ray3d::RotationMatrixToAxisAngle(m, &axis, &angle))
    return false;
  brep::Solid turned = local;
  if (angle > 1e-12) {
    brep::Problem why = brep::Problem::Ok;
    if (!brep::Rotate(local, Vec3{0.0, 0.0, 0.0}, axis, angle, &turned, &why))
      return false;
  }
  *out = brep::Translate(turned, ray3d::Sub(at.origin, docOrigin));
  return true;
}

bool MakePipe(const Part& pipe, const brep::Vec3& docOrigin, brep::Solid* out) {
  const Vec3 dir = ray3d::Normalize(ray3d::Sub(pipe.pipeEnd, pipe.pipeStart));
  const double length = ray3d::Length(ray3d::Sub(pipe.pipeEnd, pipe.pipeStart)) - pipe.pipeStartTrim - pipe.pipeEndTrim;
  const Vec3 base = ray3d::Add(ray3d::Sub(pipe.pipeStart, docOrigin), ray3d::Scale(dir, pipe.pipeStartTrim));
  ucs::Ucs frame;
  if (!(length > 0.0) || !ucs::FromNormal(base, dir, &frame))
    return false;
  brep::Problem why = brep::Problem::Ok;
  return brep::MakeCylinder(frame, pipe.pipeRadius, length, out, &why);
}

}  // namespace libredwgplant
