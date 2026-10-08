#include "LibreDwgPlant.hpp"

#include "util/ray3d.hpp"
#include "util/ucs.hpp"

#include <cmath>
#include <cstring>
#include <string_view>

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

/// AISC wide-flange, inches. The fillet between flange and web is omitted; depth, flange width,
/// web thickness and flange thickness are the published outer dimensions.
struct WideFlange {
  double depth;
  double flangeWidth;
  double flangeThickness;
  double webThickness;
};

struct WideFlangeRow {
  const char* name;  ///< "W27X178", no spaces.
  double depth;
  double width;
  double web;
  double flange;
};

/// Standard W shapes, W4 through W27. Columns are depth, flange width, web thickness, flange thickness.
constexpr WideFlangeRow kWideFlanges[] = {
    {"W27X178", 27.8, 14.09, 0.725, 1.190}, {"W27X161", 27.6, 14.02, 0.660, 1.080},
    {"W27X146", 27.4, 14.0, 0.605, 0.975},  {"W27X114", 27.3, 10.07, 0.570, 0.930},
    {"W27X102", 27.1, 10.02, 0.515, 0.830}, {"W27X94", 26.9, 10.0, 0.490, 0.745},
    {"W27X84", 26.7, 9.96, 0.460, 0.640},

    {"W24X162", 25.0, 13.0, 0.705, 1.220},  {"W24X146", 24.7, 12.9, 0.650, 1.090},
    {"W24X131", 24.5, 12.9, 0.605, 0.960},  {"W24X117", 24.3, 12.8, 0.550, 0.850},
    {"W24X104", 24.1, 12.75, 0.500, 0.750}, {"W24X94", 24.1, 9.07, 0.515, 0.875},
    {"W24X84", 24.1, 9.02, 0.470, 0.770},   {"W24X76", 23.9, 9.0, 0.440, 0.680},
    {"W24X68", 23.7, 8.97, 0.415, 0.585},   {"W24X62", 23.7, 7.04, 0.430, 0.590},
    {"W24X55", 23.6, 7.01, 0.395, 0.505},

    {"W21X147", 22.1, 12.51, 0.720, 1.150}, {"W21X132", 21.8, 12.44, 0.650, 1.035},
    {"W21X122", 21.7, 12.39, 0.600, 0.960}, {"W21X111", 21.5, 12.34, 0.550, 0.875},
    {"W21X101", 21.4, 12.29, 0.500, 0.800}, {"W21X93", 21.6, 8.42, 0.580, 0.930},
    {"W21X83", 21.4, 8.36, 0.515, 0.835},   {"W21X73", 21.2, 8.3, 0.455, 0.740},
    {"W21X68", 21.1, 8.27, 0.430, 0.685},   {"W21X62", 21.0, 8.24, 0.400, 0.615},
    {"W21X57", 21.1, 6.56, 0.405, 0.650},   {"W21X50", 20.8, 6.53, 0.380, 0.535},
    {"W21X44", 20.7, 6.5, 0.350, 0.450},

    {"W18X119", 19.0, 11.27, 0.655, 1.060}, {"W18X106", 18.7, 11.2, 0.590, 0.940},
    {"W18X97", 18.6, 11.15, 0.535, 0.870},  {"W18X86", 18.4, 11.09, 0.480, 0.770},
    {"W18X76", 18.2, 11.04, 0.425, 0.680},  {"W18X71", 18.5, 7.64, 0.495, 0.810},
    {"W18X65", 18.4, 7.59, 0.450, 0.750},   {"W18X60", 18.2, 7.56, 0.415, 0.695},
    {"W18X55", 18.1, 7.53, 0.390, 0.630},   {"W18X50", 18.0, 7.5, 0.355, 0.570},
    {"W18X46", 18.1, 6.06, 0.360, 0.605},   {"W18X40", 17.9, 6.02, 0.315, 0.525},
    {"W18X35", 17.7, 6.0, 0.300, 0.425},

    {"W16X100", 16.97, 10.425, 0.585, 0.985}, {"W16X89", 16.75, 10.365, 0.525, 0.875},
    {"W16X77", 16.52, 10.295, 0.455, 0.760},  {"W16X67", 16.33, 10.235, 0.395, 0.665},
    {"W16X57", 16.43, 7.120, 0.430, 0.715},   {"W16X50", 16.26, 7.070, 0.380, 0.630},
    {"W16X45", 16.13, 7.035, 0.345, 0.565},   {"W16X40", 16.01, 6.995, 0.305, 0.505},
    {"W16X36", 15.86, 6.985, 0.295, 0.430},   {"W16X31", 15.88, 5.525, 0.275, 0.440},
    {"W16X26", 15.69, 5.5, 0.250, 0.345},

    {"W14X132", 14.66, 14.725, 0.645, 1.030}, {"W14X120", 14.48, 14.670, 0.590, 0.940},
    {"W14X109", 14.32, 14.605, 0.525, 0.860}, {"W14X99", 14.16, 14.565, 0.485, 0.780},
    {"W14X90", 14.02, 14.520, 0.440, 0.710},  {"W14X82", 14.31, 10.130, 0.510, 0.855},
    {"W14X74", 14.17, 10.070, 0.450, 0.785},  {"W14X68", 14.04, 10.035, 0.415, 0.720},
    {"W14X61", 13.89, 9.995, 0.375, 0.645},   {"W14X53", 13.92, 8.060, 0.370, 0.660},
    {"W14X48", 13.79, 8.030, 0.340, 0.595},   {"W14X43", 13.66, 7.995, 0.305, 0.530},
    {"W14X38", 14.10, 6.770, 0.310, 0.515},   {"W14X34", 13.98, 6.745, 0.285, 0.455},
    {"W14X30", 13.84, 6.730, 0.270, 0.385},   {"W14X26", 13.91, 5.025, 0.255, 0.420},
    {"W14X22", 13.74, 5.0, 0.230, 0.335},

    {"W12X136", 13.41, 12.4, 0.790, 1.250},  {"W12X120", 13.12, 12.32, 0.710, 1.105},
    {"W12X106", 12.89, 12.22, 0.610, 0.990}, {"W12X96", 12.71, 12.16, 0.550, 0.900},
    {"W12X87", 12.53, 12.125, 0.515, 0.810}, {"W12X79", 12.38, 12.08, 0.470, 0.735},
    {"W12X72", 12.25, 12.04, 0.430, 0.670},  {"W12X65", 12.12, 12.0, 0.390, 0.605},
    {"W12X58", 12.19, 10.01, 0.360, 0.640},  {"W12X53", 12.06, 9.995, 0.345, 0.575},
    {"W12X50", 12.19, 8.08, 0.370, 0.640},   {"W12X45", 12.06, 8.045, 0.335, 0.575},
    {"W12X40", 11.94, 8.005, 0.295, 0.515},  {"W12X35", 12.50, 6.56, 0.300, 0.520},
    {"W12X30", 12.34, 6.52, 0.260, 0.440},   {"W12X26", 12.22, 6.490, 0.230, 0.380},
    {"W12X22", 12.31, 4.03, 0.260, 0.425},   {"W12X19", 12.16, 4.005, 0.235, 0.350},
    {"W12X16", 11.99, 3.990, 0.220, 0.265},  {"W12X14", 11.91, 3.970, 0.200, 0.225},

    {"W10X112", 11.36, 10.415, 0.755, 1.250}, {"W10X100", 11.1, 10.340, 0.680, 1.120},
    {"W10X88", 10.84, 10.265, 0.605, 0.990},  {"W10X77", 10.60, 10.190, 0.530, 0.870},
    {"W10X68", 10.40, 10.130, 0.470, 0.770},  {"W10X60", 10.22, 10.080, 0.420, 0.680},
    {"W10X54", 10.09, 10.030, 0.370, 0.615},  {"W10X49", 9.98, 10.0, 0.340, 0.560},
    {"W10X45", 10.10, 8.020, 0.350, 0.620},   {"W10X39", 9.92, 7.985, 0.315, 0.530},
    {"W10X33", 9.73, 7.960, 0.290, 0.435},    {"W10X30", 10.47, 5.81, 0.300, 0.510},
    {"W10X26", 10.33, 5.770, 0.260, 0.440},   {"W10X22", 10.17, 5.750, 0.240, 0.360},
    {"W10X19", 10.24, 4.020, 0.250, 0.395},   {"W10X17", 10.11, 4.010, 0.240, 0.330},
    {"W10X15", 9.99, 4.0, 0.230, 0.270},      {"W10X12", 9.87, 3.960, 0.190, 0.210},

    {"W8X67", 9.00, 8.280, 0.570, 0.935}, {"W8X58", 8.75, 8.220, 0.510, 0.810},
    {"W8X48", 8.50, 8.110, 0.400, 0.685}, {"W8X40", 8.25, 8.070, 0.360, 0.560},
    {"W8X35", 8.12, 8.020, 0.310, 0.495}, {"W8X31", 8.00, 7.995, 0.285, 0.435},
    {"W8X28", 8.06, 6.535, 0.285, 0.465}, {"W8X24", 7.93, 6.495, 0.245, 0.400},
    {"W8X21", 8.28, 5.270, 0.250, 0.400}, {"W8X18", 8.14, 5.250, 0.230, 0.330},
    {"W8X15", 8.11, 4.015, 0.245, 0.315}, {"W8X13", 7.99, 4.0, 0.230, 0.255},
    {"W8X10", 7.89, 3.940, 0.170, 0.205},

    {"W6X25", 6.38, 6.080, 0.320, 0.455}, {"W6X20", 6.20, 6.020, 0.260, 0.365},
    {"W6X16", 6.28, 4.030, 0.260, 0.405}, {"W6X15", 5.99, 5.990, 0.230, 0.260},
    {"W6X12", 6.03, 4.0, 0.230, 0.280},   {"W6X9", 5.90, 3.940, 0.170, 0.215},

    {"W5X19", 5.15, 5.030, 0.270, 0.430}, {"W5X16", 5.01, 5.0, 0.240, 0.360},

    {"W4X13", 4.16, 4.060, 0.280, 0.345},
};

static_assert(sizeof(kWideFlanges) / sizeof(kWideFlanges[0]) == 132, "standard wide-flange table");

bool LookupWideFlange(std::string_view name, WideFlange* out) {
  std::string key;
  key.reserve(name.size());
  for (const unsigned char c : name) {
    if (c == ' ' || c == '\t')
      continue;
    if (c == 'x' || c == 'X')
      key.push_back('X');
    else if (c >= 'a' && c <= 'z')
      key.push_back(static_cast<char>(c - 'a' + 'A'));
    else
      key.push_back(static_cast<char>(c));
  }
  for (const WideFlangeRow& row : kWideFlanges) {
    if (key == row.name) {
      *out = WideFlange{row.depth, row.width, row.flange, row.web};
      return true;
    }
  }
  return false;
}

std::string BeamProfileName(const Dwg_Object* obj, const Streams& s) {
  BitReader strings(reinterpret_cast<const unsigned char*>(obj->unknown_bits), obj->num_unknown_bits);
  strings.pos = s.stringsStart;
  std::string found;
  for (int i = 0; i < 24 && strings.ok && strings.pos + 2 < s.handlesStart; ++i) {
    const std::string text = strings.TU();
    if (text.size() >= 4 && (text[0] == 'W' || text[0] == 'w') &&
        text.find('x') != std::string::npos)
      found = text;
  }
  return found;
}

/// Centreline and section axes of an `ACPPSTRUCTUREBEAM`. The first survey-sized point in the data
/// stream is the start; the next is the end; after two unused zero vectors and a repeated point, nine
/// doubles are the section frame (rows X, Y, Z).
bool DecodeBeam(const Dwg_Object* obj, const Streams& s, Part* out, std::string* why) {
  const std::string profile = BeamProfileName(obj, s);
  WideFlange shape{};
  if (!LookupWideFlange(profile, &shape)) {
    *why = profile.empty() ? "beam profile name was not found"
                           : "beam profile '" + profile + "' is not a known standard shape";
    return false;
  }
  size_t origin = s.fieldsEnd;
  for (size_t at = 0; at + 40 < s.fieldsEnd; ++at) {
    BitReader probe(reinterpret_cast<const unsigned char*>(obj->unknown_bits), obj->num_unknown_bits);
    probe.pos = at;
    const Vec3 a = probe.BD3();
    const Vec3 b = probe.BD3();
    if (!probe.ok)
      continue;
    const auto placed = [](const Vec3& p) {
      return std::fabs(p.x) > 1.0e6 && std::fabs(p.y) > 1.0e6 && std::fabs(p.z) < 1.0e5;
    };
    const double span = ray3d::Length(ray3d::Sub(b, a));
    if (placed(a) && placed(b) && span > 1.0 && span < 1.0e5) {
      origin = at;
      break;
    }
  }
  if (origin >= s.fieldsEnd) {
    *why = "beam centreline was not found";
    return false;
  }
  BitReader r(reinterpret_cast<const unsigned char*>(obj->unknown_bits), obj->num_unknown_bits);
  r.pos = origin;
  const Vec3 start = r.BD3();
  const Vec3 end = r.BD3();
  if (!r.ok || !(ray3d::Length(ray3d::Sub(end, start)) > 1.0e-6)) {
    *why = "beam centreline was not found";
    return false;
  }
  const Vec3 member = ray3d::Normalize(ray3d::Sub(end, start));
  const size_t afterCentreline = r.pos;
  double frame[9]{};
  bool foundFrame = false;
  const auto isRotation = [](const double m[9]) {
    const Vec3 x{m[0], m[1], m[2]};
    const Vec3 y{m[3], m[4], m[5]};
    const Vec3 z{m[6], m[7], m[8]};
    return std::fabs(ray3d::Length(x) - 1.0) < 1.0e-3 && std::fabs(ray3d::Length(y) - 1.0) < 1.0e-3 &&
           std::fabs(ray3d::Length(z) - 1.0) < 1.0e-3 && std::fabs(ray3d::Dot(x, y)) < 1.0e-3 &&
           std::fabs(ray3d::Dot(x, z)) < 1.0e-3 && std::fabs(ray3d::Dot(y, z)) < 1.0e-3;
  };
  for (size_t at = afterCentreline; at + 18 < s.fieldsEnd && at < afterCentreline + 1600; ++at) {
    BitReader probe(reinterpret_cast<const unsigned char*>(obj->unknown_bits), obj->num_unknown_bits);
    probe.pos = at;
    double trial[9];
    bool read = true;
    for (double& v : trial) {
      v = probe.BD();
      if (!probe.ok) {
        read = false;
        break;
      }
    }
    if (!read || !isRotation(trial))
      continue;
    const Vec3 zRow{trial[6], trial[7], trial[8]};
    if (std::fabs(ray3d::Dot(zRow, member)) < 0.99)
      continue;
    for (int i = 0; i < 9; ++i)
      frame[i] = trial[i];
    foundFrame = true;
    break;
  }
  if (!foundFrame) {
    *why = "beam orientation is not a rotation";
    return false;
  }
  const Vec3 xAxis{frame[0], frame[1], frame[2]};
  const Vec3 yAxis{frame[3], frame[4], frame[5]};
  const Vec3 zAxis{frame[6], frame[7], frame[8]};
  const double xz = ray3d::Dot(xAxis, zAxis);
  const double xy = ray3d::Dot(xAxis, yAxis);
  const double yz = ray3d::Dot(yAxis, zAxis);
  if (ray3d::Length(ray3d::Sub(end, start)) < 1.0e-6 || std::fabs(ray3d::Length(xAxis) - 1.0) > 1.0e-3 ||
      std::fabs(ray3d::Length(yAxis) - 1.0) > 1.0e-3 || std::fabs(ray3d::Length(zAxis) - 1.0) > 1.0e-3 ||
      std::fabs(xy) > 1.0e-3 || std::fabs(xz) > 1.0e-3 || std::fabs(yz) > 1.0e-3) {
    *why = "beam orientation is not a rotation";
    return false;
  }
  out->kind = Part::Kind::Beam;
  out->blocks.clear();
  out->pipeStart = start;
  out->pipeEnd = end;
  out->beamX = xAxis;
  out->beamDepth = shape.depth;
  out->beamWidth = shape.flangeWidth;
  out->beamFlange = shape.flangeThickness;
  out->beamWeb = shape.webThickness;
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
  if (name == "ACPPSTRUCTUREBEAM")
    return DecodeBeam(obj, s, out, why);
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

bool MakeBeam(const Part& beam, const brep::Vec3& docOrigin, brep::Solid* out) {
  const Vec3 delta = ray3d::Sub(beam.pipeEnd, beam.pipeStart);
  const double length = ray3d::Length(delta);
  if (!(length > 1.0e-6) || !(beam.beamDepth > beam.beamFlange * 2.0) || !(beam.beamWidth > beam.beamWeb))
    return false;
  const Vec3 z = ray3d::Normalize(delta);
  Vec3 x = ray3d::Sub(beam.beamX, ray3d::Scale(z, ray3d::Dot(beam.beamX, z)));
  if (!(ray3d::Length(x) > 0.5))
    return false;
  x = ray3d::Normalize(x);
  const Vec3 y = ray3d::Normalize(ray3d::Cross(z, x));
  const Vec3 origin = ray3d::Sub(beam.pipeStart, docOrigin);
  const double hw = beam.beamWidth * 0.5;
  const double hd = beam.beamDepth * 0.5;
  const double tf = beam.beamFlange;
  const double ht = beam.beamWeb * 0.5;
  const double localX[12] = {-hw, hw, hw, ht, ht, hw, hw, -hw, -hw, -ht, -ht, -hw};
  const double localY[12] = {-hd, -hd, -hd + tf, -hd + tf, hd - tf, hd - tf, hd, hd, hd - tf, hd - tf, -hd + tf, -hd + tf};
  brep::Profile profile;
  profile.plane.origin = origin;
  profile.plane.xAxis = x;
  profile.plane.yAxis = y;
  profile.plane.zAxis = z;
  profile.vertices.resize(12);
  profile.edges.resize(12);
  for (int i = 0; i < 12; ++i) {
    profile.vertices[static_cast<size_t>(i)] =
        ray3d::Add(origin, ray3d::Add(ray3d::Scale(x, localX[i]), ray3d::Scale(y, localY[i])));
    profile.edges[static_cast<size_t>(i)].arc = false;
  }
  brep::Problem why = brep::Problem::Ok;
  return brep::Extrude(profile, length, out, &why);
}

}  // namespace libredwgplant
