#include "DxfIo.hpp"

#include "CadCommands.hpp"
#include "CadCoordinateFrame.hpp"
#include "CadLinetype.hpp"
#include "DxfColors.hpp"
#include "DxfEntityEmit.hpp"
#include "MtextRichFormat.hpp"
#include "TextStyle.hpp"
#include "util/cadtable.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDegToRad = kPi / 180.0;

/// Degrees as a DXF ARC states them → the `float` radians a `CadArc` holds.
///
/// The ONE definition of that conversion. The reader uses it to build an arc; the writer uses it to
/// know what the reader will build (issue #111). Two copies of this would be two answers to "what
/// arc does this file describe", which is the whole defect.
void DxfArcAnglesFromDegrees(double a0deg, double a1deg, float* startRad, float* sweepRad) {
  double sweep = a1deg - a0deg;
  // DXF has no way to state a zero-length arc: a full circle written as an ARC has group 51 equal to
  // group 50, so an end that does not differ from the start means one full turn, by convention.
  if (std::fabs(sweep) < 1e-9)
    sweep = 360.0;
  while (sweep < 0)
    sweep += 360.0;
  while (sweep > 360.0)
    sweep -= 360.0;
  if (sweep < 1e-9)
    sweep = 360.0;
  *startRad = static_cast<float>(a0deg * kDegToRad);
  *sweepRad = static_cast<float>(sweep * kDegToRad);
}

/// The arc a DXF file can actually state, for an arc GoSurvey holds in memory.
///
/// `CadArc` holds a start and a sweep as `float` RADIANS. A DXF ARC states a start and an END in
/// DEGREES, rounded to six decimals like every other number this writer emits. Both conversions lose
/// information, so the arc that comes back from a round trip is not always the arc that went out —
/// and every part of the writer has to agree about WHICH of the two it is describing, or the header
/// and the entity records describe different drawings and the file never settles (issue #111; the
/// same failure shape TASK-083 fixed for polylines, one entity type over).
///
/// This is the one answer. `startDeg`/`endDeg` are the exact text of groups 50 and 51;
/// `startRad`/`sweepRad` are what the reader reconstructs from that text, and so are what the header
/// extents must be swept from.
struct DxfArcAsWritten {
  std::string startDeg;
  std::string endDeg;
  float startRad = 0.f;
  float sweepRad = 0.f;
};

/// The WORLD point a curve's group 10/20/30 names, given its group 210 (REQ-312).
///
/// Group 210 does not merely annotate a world point with a direction: it makes the entity's own
/// coordinates OBJECT-coordinate values, in the Arbitrary Axis Algorithm frame the normal defines.
/// Reading 10/20/30 as world coordinates when 210 is not +Z - which is what this importer did until
/// now, by never reading 210 at all - lands a tilted ARC or CIRCLE flat and misplaced, with no
/// message (REQ-201).
///
/// `ucs::FromNormal` IS that algorithm (REQ-311), and for a +Z normal it returns the world axes
/// exactly, so a flat entity's OCS point is its world point unchanged.
///
/// False for a degenerate 210, which is a malformed file. The caller reports it rather than
/// adopting a garbage frame.
[[nodiscard]] bool DxfOcsToWorld(double x, double y, double z, double nx, double ny, double nz,
                                 ray3d::Vec3* out) {
  ucs::Ucs frame;
  if (!ucs::FromNormal({0.0, 0.0, 0.0}, {nx, ny, nz}, &frame))
    return false;
  if (out)
    *out = ucs::UcsToWorld(frame, {x, y, z});
  return true;
}

/// The OCS group 10/20/30 a curve's WORLD centre is written as, given its group 210 (REQ-312) — the
/// inverse of `DxfOcsToWorld`, through the same `ucs::FromNormal` frame. False for a degenerate 210.
///
/// Used on export to reconstruct the centre a READER will hold: our OCS point, rounded to the six
/// decimals `std::to_string` writes, projected back. Sweeping `$EXTMIN/$EXTMAX` from that rather
/// than from the in-memory centre is what lets a tilted arc's DXF byte-settle at state-plane
/// magnitude (issue #188) — the same writer/reader agreement the flat and angle paths already keep.
[[nodiscard]] bool DxfWorldToOcs(double wx, double wy, double wz, double nx, double ny, double nz,
                                 ray3d::Vec3* out) {
  ucs::Ucs frame;
  if (!ucs::FromNormal({0.0, 0.0, 0.0}, {nx, ny, nz}, &frame))
    return false;
  if (out)
    *out = ucs::WorldToUcs(frame, {wx, wy, wz});
  return true;
}

/// True when a parsed group 210 is the default world +Z, i.e. the entity is flat.
[[nodiscard]] bool DxfExtrusionIsFlat(double nx, double ny, double nz) {
  return nx == 0.0 && ny == 0.0 && nz == 1.0;
}

DxfArcAsWritten DxfArcToWrite(const CadArc& arc) {
  auto normDeg = [](double rad) {
    double d = rad * (180.0 / kPi);
    d = std::fmod(d, 360.0);
    if (d < 0.0)
      d += 360.0;
    return d;
  };

  // DIRECTION IS THE TRAP HERE. A DXF ARC always runs COUNTER-CLOCKWISE from group 50 to group 51,
  // while `CadArc::sweepRad` is SIGNED. Writing 50 = startRad and 51 = startRad + sweepRad for a
  // clockwise arc describes the COMPLEMENT of the intended arc — a file that opens fine and shows
  // the wrong geometry. A negative sweep therefore swaps the ends rather than negating anything.
  const double sweep = static_cast<double>(arc.sweepRad);
  const double ccwStart = (sweep >= 0.0) ? static_cast<double>(arc.startRad)
                                         : static_cast<double>(arc.startRad) + sweep;

  DxfArcAsWritten w;

  // (1) `ccwStart` is a DOUBLE sum of two floats whenever the sweep is negative — a value no
  //     `CadArc` can hold. Emitting it means the reader stores a neighbouring float and the very
  //     next export states a different angle. Narrow it first, so the file states an angle that
  //     survives the trip.
  const float startHeld = static_cast<float>(normDeg(ccwStart) * kDegToRad);
  w.startDeg = std::to_string(normDeg(static_cast<double>(startHeld)));

  // (2) The reader derives the sweep as `group 51 - group 50`, a difference of two INDEPENDENTLY
  //     rounded angles, so it lands up to 1e-6 deg from the sweep we hold — and above ~8.4 deg that
  //     is wider than the `float` spacing, so it lands on a different float. State the end as the
  //     WRITTEN start plus the sweep, and that subtraction gives our sweep back exactly.
  double endDeg = std::stod(w.startDeg) + std::fabs(sweep) * (180.0 / kPi);
  endDeg = std::fmod(endDeg, 360.0);
  if (endDeg < 0.0)
    endDeg += 360.0;
  w.endDeg = std::to_string(endDeg);

  DxfArcAnglesFromDegrees(std::stod(w.startDeg), std::stod(w.endDeg), &w.startRad, &w.sweepRad);
  return w;
}

/// The ellipse a DXF file can actually state, for an ellipse GoSurvey holds in memory.
///
/// `CadEllipse` holds `majVx`, `majVy`, and `ratio` as `float`. A DXF ELLIPSE states the
/// major-axis vector (groups 11/21) and the ratio (group 40) at six decimals. Both lose
/// information, so the ellipse that comes back from a round trip is not always the one that went
/// out. Sweeping the header extents from the in-memory ellipse while the entity record describes
/// the post-round-trip one describes two drawings in one file (issue #113, same shape #111 fixed
/// for arcs and TASK-083 fixed for polylines). This is the one answer: the quantized values the
/// file actually states and the reader reconstructs.
inline float DxfQuantizeFloat(float v) {
  return static_cast<float>(std::stod(std::to_string(static_cast<double>(v))));
}

struct DxfEllipseAsWritten {
  float majVx = 0.f;
  float majVy = 0.f;
  float ratio = 0.f;
};

DxfEllipseAsWritten DxfEllipseToWrite(const CadEllipse& el) {
  DxfEllipseAsWritten w{};
  w.majVx = DxfQuantizeFloat(el.majVx);
  w.majVy = DxfQuantizeFloat(el.majVy);
  w.ratio = DxfQuantizeFloat(el.ratio);
  return w;
}

std::string Trim(const std::string& s) {
  size_t a = 0;
  while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a])))
    ++a;
  size_t b = s.size();
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
    --b;
  return s.substr(a, b - a);
}

bool EqCiNorm(const std::string& aRaw, const char* expectAscii) {
  const std::string a = Trim(aRaw);
  const std::string e(expectAscii);
  if (a.size() != e.size())
    return false;
  for (size_t i = 0; i < a.size(); ++i) {
    const unsigned char ca = static_cast<unsigned char>(a[i]);
    const unsigned char cb = static_cast<unsigned char>(e[i]);
    if (std::tolower(ca) != std::tolower(cb))
      return false;
  }
  return true;
}

bool EqCiStr(const std::string& a, const std::string& b) {
  if (a.size() != b.size())
    return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
      return false;
  }
  return true;
}

struct DxfPair {
  int code = 0;
  std::string value;
};

bool LoadDxfPairs(const std::filesystem::path& path, std::vector<DxfPair>* out, std::string* err) {
  out->clear();
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    *err = "Could not open DXF file.";
    return false;
  }
  std::string blob((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (blob.size() >= 22 && blob.compare(0, 22, "AutoCAD Binary DXF") == 0) {
    *err = "Binary DXF is not supported — in AutoCAD use Save As → ASCII DXF.";
    return false;
  }
  if (blob.size() >= 3 && static_cast<unsigned char>(blob[0]) == 0xEF && static_cast<unsigned char>(blob[1]) == 0xBB &&
      static_cast<unsigned char>(blob[2]) == 0xBF)
    blob.erase(0, 3);
  std::istringstream lines(blob);
  std::string lc;
  std::string lv;
  while (std::getline(lines, lc)) {
    if (!std::getline(lines, lv)) {
      *err = "DXF truncated (missing value line).";
      return false;
    }
    // Strip CR when CRLF
    if (!lc.empty() && lc.back() == '\r')
      lc.pop_back();
    if (!lv.empty() && lv.back() == '\r')
      lv.pop_back();
    DxfPair p{};
    try {
      p.code = std::stoi(Trim(lc));
    } catch (...) {
      *err = "DXF parse error at group code line.";
      return false;
    }
    p.value = Trim(lv);
    out->push_back(std::move(p));
  }
  return true;
}

bool ParseDouble(const std::string& s, double* o) {
  try {
    size_t idx = 0;
    *o = std::stod(s, &idx);
    return idx > 0;
  } catch (...) {
    return false;
  }
}

bool ParseIntFlexible(const std::string& s, int* o) {
  try {
    *o = std::stoi(s);
    return true;
  } catch (...) {
    return false;
  }
}

int NormalizeAci(int raw) {
  int v = raw;
  if (v < 0)
    v = -v;
  v = v % 256;
  if (v == 0 || v == 256)
    return 256; // ByLayer sentinel
  return std::clamp(v, 1, 255);
}

static int HexNibble(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return 10 + (c - 'a');
  if (c >= 'A' && c <= 'F')
    return 10 + (c - 'A');
  return -1;
}

bool Hex7ToRgbPacked(const std::string& h, uint32_t* rgb) {
  if (h.size() != 7 || h[0] != '#')
    return false;
  const int h10 = HexNibble(h[1]);
  const int h11 = HexNibble(h[2]);
  const int h20 = HexNibble(h[3]);
  const int h21 = HexNibble(h[4]);
  const int h30 = HexNibble(h[5]);
  const int h31 = HexNibble(h[6]);
  if (h10 < 0 || h11 < 0 || h20 < 0 || h21 < 0 || h30 < 0 || h31 < 0)
    return false;
  const int rh = (h10 << 4) | h11;
  const int gh = (h20 << 4) | h21;
  const int bh = (h30 << 4) | h31;
  *rgb = (static_cast<uint32_t>(rh) << 16) | (static_cast<uint32_t>(gh) << 8) | static_cast<uint32_t>(bh);
  return true;
}

bool NamedColorToRgbPacked(const std::string& c, uint32_t* rgb) {
  static const struct {
    const char* name;
    uint32_t rgb;
  } k[] = {{"Red", 0xFF0000},       {"Yellow", 0xFFFF00}, {"Green", 0x00FF00}, {"Cyan", 0x00FFFF},
           {"Blue", 0x0000FF},      {"Magenta", 0xFF00FF}, {"White", 0xFFFFFF}, {"Gray", 0x808080},
           {"Black", 0x000000},     {"Orange", 0xFF8000}};
  for (const auto& e : k) {
    if (c == e.name) {
      *rgb = e.rgb;
      return true;
    }
  }
  return false;
}

uint32_t ResolveLayerRgbPacked(const std::unordered_map<std::string, uint32_t>& layerRgb,
                               const std::string& layer) {
  auto it = layerRgb.find(layer);
  if (it != layerRgb.end())
    return it->second & 0xFFFFFFu;
  return DxfRgbPackedFromAci(7);
}

std::string HexFromRgbPacked(uint32_t rgb) {
  char buf[16];
  std::snprintf(buf, sizeof(buf), "#%06X", static_cast<unsigned>(rgb & 0xFFFFFFu));
  return std::string(buf);
}

std::string EntityColorStorage(int color62, bool has420, int rgb420, const std::string& layer,
                               const std::unordered_map<std::string, uint32_t>& layerRgb) {
  uint32_t rgb = 0;
  if (has420) {
    rgb = static_cast<uint32_t>(rgb420) & 0xFFFFFFu;
    return HexFromRgbPacked(rgb);
  }
  const int aci = NormalizeAci(color62);
  if (aci == 256)
    return HexFromRgbPacked(ResolveLayerRgbPacked(layerRgb, layer));
  rgb = DxfRgbPackedFromAci(aci);
  return HexFromRgbPacked(rgb);
}

/// Finds DXF section body `[contentBegin, contentEnd)` — indices of pairs inside section before ENDSEC.
bool FindSectionBounds(const std::vector<DxfPair>& t, const char* sectionNameCi, size_t* contentBegin,
                       size_t* contentEnd) {
  for (size_t i = 0; i < t.size(); ++i) {
    if (t[i].code != 0 || !EqCiNorm(t[i].value, "SECTION"))
      continue;
    for (size_t k = i + 1; k < t.size() && k < i + 160; ++k) {
      if (t[k].code == 0 && EqCiNorm(t[k].value, "SECTION"))
        break;
      if (t[k].code == 2 && EqCiNorm(t[k].value, sectionNameCi)) {
        const size_t start = k + 1;
        for (size_t j = start; j < t.size(); ++j) {
          if (t[j].code == 0 && EqCiNorm(t[j].value, "ENDSEC")) {
            *contentBegin = start;
            *contentEnd = j;
            return true;
          }
        }
        return false;
      }
    }
  }
  return false;
}

bool FindMatchingEndBlk(const std::vector<DxfPair>& t, size_t blockIdx, size_t limitExclusive, size_t* endBlkIdx) {
  size_t j = blockIdx + 1;
  int depth = 1;
  while (j < limitExclusive) {
    if (t[j].code == 0 && EqCiNorm(t[j].value, "BLOCK"))
      depth++;
    else if (t[j].code == 0 && EqCiNorm(t[j].value, "ENDBLK")) {
      depth--;
      if (depth == 0) {
        *endBlkIdx = j;
        return true;
      }
    }
    ++j;
  }
  return false;
}

bool IsModelSpaceBlockName(const std::string& raw) {
  // AutoCAD R2000+ uses *Model_Space; older/other writers use *MODEL_SPACE or *MODEL_SPACE*.
  std::string s = Trim(raw);
  for (char& ch : s)
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  while (!s.empty() && s.back() == '*')
    s.pop_back();
  return s == "*model_space";
}

/// Geometry sometimes lives only inside block *MODEL_SPACE (no ENTITIES section). Range is [begin,end) exclusive of ENDBLK.
bool FindModelSpaceEntityRange(const std::vector<DxfPair>& t, size_t* beginOut, size_t* endOut) {
  size_t bs0 = 0;
  size_t bs1 = 0;
  if (!FindSectionBounds(t, "BLOCKS", &bs0, &bs1))
    return false;

  size_t i = bs0;
  while (i < bs1) {
    if (!(t[i].code == 0 && EqCiNorm(t[i].value, "BLOCK"))) {
      ++i;
      continue;
    }
    size_t be = 0;
    if (!FindMatchingEndBlk(t, i, bs1, &be)) {
      ++i;
      continue;
    }
    bool isMs = false;
    for (size_t k = i; k < be; ++k) {
      if (t[k].code == 2 && IsModelSpaceBlockName(t[k].value)) {
        isMs = true;
        break;
      }
    }
    if (isMs) {
      *beginOut = i + 1;
      *endOut = be;
      return true;
    }
    i = be + 1;
  }
  return false;
}

bool EntityRangeIsPaperSpace(const std::vector<DxfPair>& t, size_t lo, size_t hi) {
  for (size_t k = lo; k < hi; ++k) {
    if (t[k].code != 67)
      continue;
    int v = 0;
    if (ParseIntFlexible(t[k].value, &v) && v != 0)
      return true;
  }
  return false;
}

bool IsPaperSpaceBlockName(const std::string& raw) {
  std::string s = Trim(raw);
  for (char& ch : s)
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  return s.find("*paper_space") != std::string::npos;
}

struct Affine2D {
  double m00 = 1, m01 = 0, m02 = 0;
  double m10 = 0, m11 = 1, m12 = 0;

  bool isIdentity() const {
    return std::fabs(m00 - 1.0) < 1e-12 && std::fabs(m01) < 1e-12 && std::fabs(m02) < 1e-12 &&
           std::fabs(m10) < 1e-12 && std::fabs(m11 - 1.0) < 1e-12 && std::fabs(m12) < 1e-12;
  }

  void apply(double x, double y, double* ox, double* oy) const {
    *ox = m00 * x + m01 * y + m02;
    *oy = m10 * x + m11 * y + m12;
  }

  Affine2D compose(const Affine2D& b) const {
    Affine2D r;
    r.m00 = m00 * b.m00 + m01 * b.m10;
    r.m01 = m00 * b.m01 + m01 * b.m11;
    r.m02 = m00 * b.m02 + m01 * b.m12 + m02;
    r.m10 = m10 * b.m00 + m11 * b.m10;
    r.m11 = m10 * b.m01 + m11 * b.m11;
    r.m12 = m10 * b.m02 + m11 * b.m12 + m12;
    return r;
  }

  static Affine2D FromInsert(double ix, double iy, double sx, double sy, double rotDeg) {
    const double rad = rotDeg * kDegToRad;
    const double c = std::cos(rad);
    const double s = std::sin(rad);
    Affine2D r;
    r.m00 = c * sx;
    r.m01 = -s * sy;
    r.m02 = ix;
    r.m10 = s * sx;
    r.m11 = c * sy;
    r.m12 = iy;
    return r;
  }
};

/// A DXF text style (STYLE table record): the font it maps to and whether it is oblique (italic).
struct DxfTextStyle {
  std::string font;     // group 3 primary font file, e.g. "romans.shx" or "arial.ttf"
  bool italic = false;  // group 50 oblique angle != 0
};

inline void UpdateCoordMag(double* mag, double x, double y) {
  const double m = std::fabs(x) + std::fabs(y);
  if (m > *mag)
    *mag = m;
}

// Convert AutoCAD TEXT/MTEXT "%%" control codes to plain text: %%d→°, %%p→±, %%c→Ø,
// %%%→%, and underline/overline/strike toggles (%%u %%o %%k) are dropped. %%nnn → ASCII char.
std::string DxfPercentCodesToPlain(const std::string& in) {
  std::string out;
  out.reserve(in.size());
  for (size_t i = 0; i < in.size();) {
    if (in[i] == '%' && i + 2 < in.size() && in[i + 1] == '%') {
      const char code = static_cast<char>(std::tolower(static_cast<unsigned char>(in[i + 2])));
      switch (code) {
        case 'd': out += "\xC2\xB0"; i += 3; continue;  // degree °
        case 'p': out += "\xC2\xB1"; i += 3; continue;  // plus/minus ±
        case 'c': out += "\xC3\x98"; i += 3; continue;  // diameter Ø
        case '%': out += "%";        i += 3; continue;
        case 'u': case 'o': case 'k': i += 3; continue; // underline/overline/strike toggles
        default: break;
      }
      // %%nnn numeric ASCII code (up to 3 digits)
      if (std::isdigit(static_cast<unsigned char>(in[i + 2]))) {
        int val = 0, n = 0;
        size_t k = i + 2;
        while (k < in.size() && n < 3 && std::isdigit(static_cast<unsigned char>(in[k]))) {
          val = val * 10 + (in[k] - '0');
          ++k;
          ++n;
        }
        if (val > 0 && val < 256)
          out.push_back(static_cast<char>(val));
        i = k;
        continue;
      }
    }
    out.push_back(in[i]);
    ++i;
  }
  return out;
}

// Convert AutoCAD MTEXT formatting to the GoSurvey rich wire ([[b]]/[[i]]/[[u]]/[[font:…]]), preserving
// styling instead of discarding it. Handles \P (newline), \L/\l (underline on/off), \f…|bX|iX; (font +
// bold/italic), { } scoping, and the %%u/%%d/%%p/%%c codes. Other codes are dropped.
std::string DxfMtextToRichWire(const std::string& in) {
  struct State { std::string font; bool b = false, i = false, u = false; };
  State cur;
  std::vector<State> scope;
  std::string out;
  bool haveOpen = false;
  State open;
  auto closeTags = [&]() {
    if (!haveOpen) return;
    if (open.u) out += "[[/u]]";
    if (open.i) out += "[[/i]]";
    if (open.b) out += "[[/b]]";
    if (!open.font.empty()) out += "[[/font]]";
    haveOpen = false;
  };
  auto openTags = [&]() {
    if (haveOpen) return;
    if (!cur.font.empty()) out += "[[font:" + cur.font + "]]";
    if (cur.b) out += "[[b]]";
    if (cur.i) out += "[[i]]";
    if (cur.u) out += "[[u]]";
    open = cur;
    haveOpen = true;
  };
  auto sync = [&]() {
    if (haveOpen && (open.font != cur.font || open.b != cur.b || open.i != cur.i || open.u != cur.u))
      closeTags();
    openTags();
  };
  auto emit = [&](char ch) { sync(); out.push_back(ch); };

  for (size_t i = 0; i < in.size();) {
    const char c = in[i];
    if (c == '\\' && i + 1 < in.size()) {
      const char n = in[i + 1];
      switch (n) {
        case 'P': case 'n': emit('\n'); i += 2; continue;
        case '~': emit(' ');  i += 2; continue;
        case '\\': emit('\\'); i += 2; continue;
        case '{': emit('{'); i += 2; continue;
        case '}': emit('}'); i += 2; continue;
        case 'L': cur.u = true;  i += 2; continue;
        case 'l': cur.u = false; i += 2; continue;
        case 'f': case 'F': {
          // \f FontName | b0/1 | i0/1 | c.. | p.. ;  — set current font + bold/italic.
          size_t k = i + 2;
          std::string name;
          while (k < in.size() && in[k] != '|' && in[k] != ';') name.push_back(in[k++]);
          while (k < in.size() && in[k] != ';') {
            if (in[k] == '|' && k + 1 < in.size()) {
              const char f = in[k + 1];
              if (f == 'b') cur.b = (k + 2 < in.size() && in[k + 2] != '0');
              else if (f == 'i') cur.i = (k + 2 < in.size() && in[k + 2] != '0');
            }
            ++k;
          }
          if (!name.empty()) cur.font = name;
          i = (k < in.size()) ? k + 1 : k;  // skip ';'
          continue;
        }
        case 'C': case 'c': case 'H': case 'W': case 'Q': case 'T': case 'A': case 'p': {
          i += 2;
          while (i < in.size() && in[i] != ';') ++i;
          if (i < in.size()) ++i;
          continue;
        }
        case 'O': case 'o': case 'K': case 'k': i += 2; continue;  // overline/strike — unsupported
        default: emit(n); i += 2; continue;
      }
    }
    if (c == '{') { scope.push_back(cur); ++i; continue; }
    if (c == '}') { if (!scope.empty()) { cur = scope.back(); scope.pop_back(); } ++i; continue; }
    if (c == '%' && i + 2 < in.size() && in[i + 1] == '%') {
      const char code = static_cast<char>(std::tolower(static_cast<unsigned char>(in[i + 2])));
      if (code == 'u') { cur.u = !cur.u; i += 3; continue; }
      if (code == 'o' || code == 'k') { i += 3; continue; }
      if (code == 'd') { sync(); out += "\xC2\xB0"; i += 3; continue; }
      if (code == 'p') { sync(); out += "\xC2\xB1"; i += 3; continue; }
      if (code == 'c') { sync(); out += "\xC3\x98"; i += 3; continue; }
      if (code == '%') { emit('%'); i += 3; continue; }
    }
    emit(c);
    ++i;
  }
  closeTags();
  return out;
}

void CollectBlockDefinitions(const std::vector<DxfPair>& t,
                             std::unordered_map<std::string, std::pair<size_t, size_t>>* defsOut) {
  defsOut->clear();
  size_t bs0 = 0;
  size_t bs1 = 0;
  if (!FindSectionBounds(t, "BLOCKS", &bs0, &bs1))
    return;
  size_t i = bs0;
  while (i < bs1) {
    if (!(t[i].code == 0 && EqCiNorm(t[i].value, "BLOCK"))) {
      ++i;
      continue;
    }
    size_t be = 0;
    if (!FindMatchingEndBlk(t, i, bs1, &be)) {
      ++i;
      continue;
    }
    std::string blkName;
    for (size_t k = i + 1; k < be && k < i + 120; ++k) {
      if (t[k].code == 2) {
        blkName = Trim(t[k].value);
        break;
      }
    }
    if (!blkName.empty())
      (*defsOut)[blkName] = {i + 1, be};
    i = be + 1;
  }
}

void ParseEntityRegion(const std::vector<DxfPair>& t, size_t entBegin, size_t entEnd, AppCommandState& st,
                       const std::unordered_map<std::string, uint32_t>& layerRgb,
                       const std::unordered_map<std::string, std::pair<size_t, size_t>>* blockDefs, const Affine2D& xf,
                       int insertDepth, double* coordMagMax, int* skippedPaper, int* skippedViewport,
                       int* skippedUnknown, std::unordered_map<std::string, int>* skipCounts,
                       std::vector<SurveyPoint>* embeddedPointsLocal,
                       const std::unordered_map<std::string, DxfTextStyle>* textStyles,
                       int* degenerateExtrusionsOut) {
  // Counted here, reported once by the caller, in the shape the other four counters already use.
  const auto refuseDegenerateExtrusion = [&]() {
    if (degenerateExtrusionsOut)
      ++*degenerateExtrusionsOut;
  };

  constexpr int kMaxInsertDepth = 64;

  // Resolve a TEXT/MTEXT style name (DXF group 7; empty → "Standard") to its registered style name (for the
  // annotation's live style reference, REQ-044) + font + italic flag. The returned name matches a drawing
  // TextStyle registered by RegisterDxfTextStylesIntoDrawing, so a style edit ripples to this annotation.
  auto resolveStyle = [&](const std::string& styleName, std::string* outStyleName, std::string* outFont,
                          bool* outItalic) {
    *outStyleName = styleName.empty() ? std::string("Standard") : styleName;
    *outFont = std::string();
    *outItalic = false;
    if (!textStyles)
      return;
    auto it = textStyles->find(*outStyleName);
    if (it == textStyles->end()) {
      // case-insensitive fallback
      for (const auto& kv : *textStyles)
        if (EqCiStr(kv.first, *outStyleName)) { it = textStyles->find(kv.first); break; }
    }
    if (it != textStyles->end()) {
      *outStyleName = it->first;  // canonical registered name (matches the drawing's TextStyle)
      *outFont = it->second.font;
      *outItalic = it->second.italic;
    }
  };

  auto collectEntityRange = [&](size_t startIdx, size_t* endIdxOut) {
    size_t jj = startIdx + 1;
    while (jj < entEnd && !(t[jj].code == 0))
      ++jj;
    *endIdxOut = jj;
  };

  // \p z0 / \p z1 are DXF group 30 / 31 elevations (REQ-057). They default to 0 so the many
  // callers that build inherently flat geometry (expanded POINT cross-lines, tessellated curves,
  // dimension leaders) stay unchanged. Z is carried through **unrebased**: the document origin is
  // X/Y-only (ADR-025 D2), and \c xf is a 2D transform, so a block INSERT's Z scale/translation is
  // not applied — a known limitation, recorded rather than silently approximated.
  auto appendSegXF = [&](double x0, double y0, double x1, double y1, const EntityAttributes& at,
                         double z0 = 0.0, double z1 = 0.0) {
    double ox0 = 0, oy0 = 0, ox1 = 0, oy1 = 0;
    xf.apply(x0, y0, &ox0, &oy0);
    xf.apply(x1, y1, &ox1, &oy1);
    UpdateCoordMag(coordMagMax, ox0, oy0);
    UpdateCoordMag(coordMagMax, ox1, oy1);
    st.userLinesFlat.push_back(static_cast<float>(ox0 - st.worldDocumentOriginX));
    st.userLinesFlat.push_back(static_cast<float>(oy0 - st.worldDocumentOriginY));
    st.userLinesFlat.push_back(static_cast<float>(z0));
    st.userLinesFlat.push_back(static_cast<float>(ox1 - st.worldDocumentOriginX));
    st.userLinesFlat.push_back(static_cast<float>(oy1 - st.worldDocumentOriginY));
    st.userLinesFlat.push_back(static_cast<float>(z1));
    st.userLineAttrs.push_back(at);
  };

  // A DXF POLYLINE/LWPOLYLINE vertex on its way into the polyline store: model-space X/Y (still to
  // be transformed and rebased) and the absolute Z the entity gave it.
  struct ImportPolyVert {
    double x = 0, y = 0, z = 0;
    double bulge = 0;  // REQ-316 / ADR-047: DXF group 42 on the vertex; 0 = straight
  };

  // Store a vertex run AS a polyline (REQ-053's four parallel arrays) rather than as loose segments.
  // Until this existed the importer had no polyline sink at all — every POLYLINE and LWPOLYLINE was
  // decomposed into `userLinesFlat`, so a DXF round trip shattered every polyline into unrelated
  // lines (issue #64), and so did an ordinary import of any file Civil 3D wrote. Same transform and
  // same `local = world - worldDocumentOrigin` rebase as appendSegXF; Z is carried unrebased, the
  // document origin being X/Y-only (ADR-025 D2).
  auto appendPolylineXF = [&](const std::vector<ImportPolyVert>& pts, bool closed,
                              const EntityAttributes& at) {
    if (pts.size() < 2)
      return;
    const int baseVert = st.userPolylineOffsets.empty() ? 0 : st.userPolylineOffsets.back();
    if (st.userPolylineOffsets.empty())
      st.userPolylineOffsets.push_back(baseVert);
    bool anyBulge = false;
    for (const ImportPolyVert& p : pts)
      if (std::fabs(p.bulge) > 1e-12) { anyBulge = true; break; }
    // REQ-316 / ADR-047: keep the polyline store's bulge array in step. Bulge is preserved verbatim
    // (the import transform here is rigid + uniform, which maps circular arcs to circular arcs).
    if (anyBulge && st.userPolylineVertsBulge.size() < st.userPolylineVerts.size() / 3)
      st.userPolylineVertsBulge.resize(st.userPolylineVerts.size() / 3, 0.0f);
    for (const ImportPolyVert& p : pts) {
      double ox = 0, oy = 0;
      xf.apply(p.x, p.y, &ox, &oy);
      UpdateCoordMag(coordMagMax, ox, oy);
      st.userPolylineVerts.push_back(static_cast<float>(ox - st.worldDocumentOriginX));
      st.userPolylineVerts.push_back(static_cast<float>(oy - st.worldDocumentOriginY));
      st.userPolylineVerts.push_back(static_cast<float>(p.z));
      if (anyBulge || !st.userPolylineVertsBulge.empty())
        st.userPolylineVertsBulge.push_back(static_cast<float>(p.bulge));
    }
    st.userPolylineOffsets.push_back(baseVert + static_cast<int>(pts.size()));
    st.userPolylineClosed.push_back(closed ? uint8_t{1} : uint8_t{0});
    st.userPolylineAttrs.push_back(at);
  };

  // Map a model-space (insertion, height, rotation) triple through the active INSERT transform into the
  // document's local frame, producing the annotation fields. plottedHeightInches is set so that
  // CadAnnotationHeightWorld(...) reproduces the model height (round-trips DXF group 40 exactly).
  struct AnnXf { double insX, insY; float plottedH; float rotWorld; double scaleX, scaleY; };
  auto xfAnnotation = [&](double x, double y, double hModel, double rotModel) -> AnnXf {
    double ox = 0, oy = 0;
    xf.apply(x, y, &ox, &oy);
    UpdateCoordMag(coordMagMax, ox, oy);
    const double cdx = xf.m00 * std::cos(rotModel) + xf.m01 * std::sin(rotModel);
    const double cdy = xf.m10 * std::cos(rotModel) + xf.m11 * std::sin(rotModel);
    const double scaleX = std::hypot(xf.m00, xf.m10);
    const double scaleY = std::hypot(xf.m01, xf.m11);
    const double mup = std::max(static_cast<double>(st.modelUnitsPerPlottedInch), 1e-6);
    AnnXf r;
    r.insX = ox - st.worldDocumentOriginX;
    r.insY = oy - st.worldDocumentOriginY;
    r.plottedH = static_cast<float>((hModel * scaleY) / mup);
    r.rotWorld = static_cast<float>(std::atan2(cdy, cdx));
    r.scaleX = scaleX;
    r.scaleY = scaleY;
    return r;
  };

  // REQ-316 / ADR-047: the old `appendBulgeXF` lambda that tessellated a bulge arc into loose
  // segments is gone — POLYLINE/LWPOLYLINE bulges are now stored on the polyline (group 42 ->
  // per-vertex bulge, see appendPolylineXF), so the arc and the entity both survive the round trip.

  auto appendEllipseXF = [&](double cx, double cy, double majx, double majy, double ratio, double t0, double t1,
                             const EntityAttributes& at, double cz = 0.0) {
    const double a = std::hypot(majx, majy);
    if (a < 1e-12)
      return;
    const double b = ratio * a;
    // #63: this function READ like a sink and was not one — it tessellated every ellipse, so
    // ellipses lost identity on import as well as being dropped on export. A FULL ellipse now takes
    // the real store, on the same identity-vs-tessellate split `appendCircleXF` established.
    //
    // A TRIMMED ellipse still tessellates, and that is forced rather than chosen: `CadEllipse` has
    // no start/end parameter, so the range cannot be stored, and adding one is a `.gs` data-format
    // change — a SPEC GAP, not bug-fix work (TASK-114 DEBT-1). The full-turn test below reads a
    // zero-length span as "no range given" = a full turn, matching the tessellating path's own rule.
    double spanFull = t1 - t0;
    while (spanFull < 0.0)
      spanFull += 2.0 * kPi;
    const bool isFullTurn = (spanFull < 1e-9) || (std::fabs(spanFull - 2.0 * kPi) < 1e-6);
    if (isFullTurn && xf.isIdentity()) {
      double ocx = 0, ocy = 0;
      xf.apply(cx, cy, &ocx, &ocy);
      UpdateCoordMag(coordMagMax, ocx, ocy);
      UpdateCoordMag(coordMagMax, ocx + a, ocy + a);
      CadEllipse el{};
      el.cx = static_cast<float>(ocx - st.worldDocumentOriginX);
      el.cy = static_cast<float>(ocy - st.worldDocumentOriginY);
      // Groups 11/21 are a VECTOR from the centre, not a point, so they take no origin shift.
      el.majVx = static_cast<float>(majx);
      el.majVy = static_cast<float>(majy);
      el.ratio = static_cast<float>(ratio);
      el.z = static_cast<float>(cz);  // group 30 (REQ-057), unrebased
      st.userEllipses.push_back(el);
      st.userEllAttrs.push_back(at);
      return;
    }
    const double ux = majx / a;
    const double uy = majy / a;
    const double vx = -uy;
    const double vy = ux;
    auto eval = [&](double t, double* ox, double* oy) {
      *ox = cx + a * std::cos(t) * ux + b * std::sin(t) * vx;
      *oy = cy + a * std::cos(t) * uy + b * std::sin(t) * vy;
    };
    double tt0 = t0;
    double tt1 = t1;
    while (tt1 < tt0)
      tt1 += 2.0 * kPi;
    double span = tt1 - tt0;
    if (span < 1e-9)
      span = 2.0 * kPi;
    const int nseg = std::clamp(static_cast<int>(std::ceil(span / (kPi / 32))), 8, 256);
    double ox0 = 0, oy0 = 0;
    eval(tt0, &ox0, &oy0);
    for (int s = 1; s <= nseg; ++s) {
      const double u = tt0 + span * (static_cast<double>(s) / static_cast<double>(nseg));
      double ox1 = 0, oy1 = 0;
      eval(u, &ox1, &oy1);
      appendSegXF(ox0, oy0, ox1, oy1, at);
      ox0 = ox1;
      oy0 = oy1;
    }
  };

  // \p cx,\p cy,\p cz are WORLD, already resolved out of the OCS by the caller (REQ-312); \p nx..nz
  // is the plane the circle lies in, world +Z for every flat one.
  auto appendCircleXF = [&](double cx, double cy, double rad, const EntityAttributes& at, double cz = 0.0,
                            double nx = 0.0, double ny = 0.0, double nz = 1.0) {
    if (rad <= 1e-9)
      return;
    const bool flat = DxfExtrusionIsFlat(nx, ny, nz);
    if (xf.isIdentity()) {
      double ocx = 0, ocy = 0;
      xf.apply(cx, cy, &ocx, &ocy);
      UpdateCoordMag(coordMagMax, ocx, ocy);
      UpdateCoordMag(coordMagMax, ocx + rad, ocy);
      st.userCirclesCxCyZR.push_back(static_cast<float>(ocx - st.worldDocumentOriginX));
      st.userCirclesCxCyZR.push_back(static_cast<float>(ocy - st.worldDocumentOriginY));
      st.userCirclesCxCyZR.push_back(static_cast<float>(cz));  // group 30 (REQ-057), unrebased
      st.userCirclesCxCyZR.push_back(static_cast<float>(rad));
      st.userCircleAttrs.push_back(at);
      PushCircleNormal(st.userCircleNormals, static_cast<float>(nx), static_cast<float>(ny),
                       static_cast<float>(nz));  // REQ-312: group 210
      return;
    }
    // Under a non-identity INSERT transform the circle loses its identity and becomes segments (see
    // appendArcXF's note). A tilted one is walked in its own plane first, so what degrades to
    // segments is the ring the file states rather than its XY shadow.
    constexpr int nseg = 64;
    const ucs::Ucs plane =
        flat ? ucs::Ucs{} : CurvePlane(cx, cy, cz, nx, ny, nz);
    for (int s = 0; s < nseg; ++s) {
      const double u0 = (kPi * 2.0) * (static_cast<double>(s) / static_cast<double>(nseg));
      const double u1 = (kPi * 2.0) * (static_cast<double>(s + 1) / static_cast<double>(nseg));
      if (flat) {
        appendSegXF(cx + rad * std::cos(u0), cy + rad * std::sin(u0), cx + rad * std::cos(u1),
                    cy + rad * std::sin(u1), at, cz, cz);  // the tessellated ring stays on its own plane
        continue;
      }
      const ray3d::Vec3 p0 = CurvePointAt(plane, rad, u0);
      const ray3d::Vec3 p1 = CurvePointAt(plane, rad, u1);
      appendSegXF(p0.x, p0.y, p1.x, p1.y, at, p0.z, p1.z);
    }
  };

  // Arcs (#63). Before TASK-114 there was no sink at all: the ARC branch parsed groups 50/51
  // correctly and then threw the identity away into 48 line segments, so an arc from Civil 3D
  // arrived as loose segments that could not be selected, trimmed or offset as the object it is.
  //
  // The identity-vs-tessellate split is `appendCircleXF`'s, deliberately reused rather than
  // reinvented: `CadArc` stores ONE radius, so a non-uniform or skewed INSERT scale produces a
  // curve it cannot represent, and degrading to segments is the honest outcome there.
  //
  // `a0` / `sweep` arrive already normalized by the caller to a CCW span in (0, 360], which is what
  // DXF guarantees. That canonicalizes direction: a clockwise arc drawn here, exported and
  // re-imported comes back as the same geometry described CCW. The shape is identical and the
  // re-export is byte-identical — only the internal sign convention settles to one form.
  // \p cx,\p cy,\p cz are WORLD, already resolved out of the OCS by the caller (REQ-312); \p a0 and
  // \p sweep are measured in the arc's own frame, which is the frame group 210 defines - so they
  // need no adjustment, only the centre does.
  auto appendArcXF = [&](double cx, double cy, double rad, double a0, double sweep,
                         const EntityAttributes& at, double cz = 0.0, double nx = 0.0, double ny = 0.0,
                         double nz = 1.0) {
    if (rad <= 1e-9)
      return;
    const bool flat = DxfExtrusionIsFlat(nx, ny, nz);
    if (xf.isIdentity()) {
      double ocx = 0, ocy = 0;
      xf.apply(cx, cy, &ocx, &ocy);
      UpdateCoordMag(coordMagMax, ocx, ocy);
      UpdateCoordMag(coordMagMax, ocx + rad, ocy);
      CadArc arc{};
      arc.cx = static_cast<float>(ocx - st.worldDocumentOriginX);
      arc.cy = static_cast<float>(ocy - st.worldDocumentOriginY);
      arc.r = static_cast<float>(rad);
      arc.startRad = static_cast<float>(a0);
      arc.sweepRad = static_cast<float>(sweep);
      arc.z = static_cast<float>(cz);  // group 30 (REQ-057), unrebased
      arc.nx = static_cast<float>(nx);  // REQ-312: group 210
      arc.ny = static_cast<float>(ny);
      arc.nz = static_cast<float>(nz);
      st.userArcs.push_back(arc);
      st.userArcAttrs.push_back(at);
      return;
    }
    constexpr int nseg = 48;
    const ucs::Ucs plane = flat ? ucs::Ucs{} : CurvePlane(cx, cy, cz, nx, ny, nz);
    for (int s = 0; s < nseg; ++s) {
      const double u0 = a0 + sweep * (static_cast<double>(s) / static_cast<double>(nseg));
      const double u1 = a0 + sweep * (static_cast<double>(s + 1) / static_cast<double>(nseg));
      if (flat) {
        appendSegXF(cx + rad * std::cos(u0), cy + rad * std::sin(u0), cx + rad * std::cos(u1),
                    cy + rad * std::sin(u1), at, cz, cz);  // the arc stays on its group-30 plane
        continue;
      }
      const ray3d::Vec3 p0 = CurvePointAt(plane, rad, u0);
      const ray3d::Vec3 p1 = CurvePointAt(plane, rad, u1);
      appendSegXF(p0.x, p0.y, p1.x, p1.y, at, p0.z, p1.z);
    }
  };

  auto hatchEmit = [&](size_t lo, size_t hi, const EntityAttributes& at) -> bool {
    bool any = false;
    size_t k = lo;
    while (k < hi) {
      if (t[k].code != 72) {
        ++k;
        continue;
      }
      const std::string edgeKind = Trim(t[k].value);
      if (edgeKind == "1") {
        double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        size_t kk = k + 1;
        for (; kk < hi && t[kk].code != 72 && t[kk].code != 91 && !(t[kk].code == 0); ++kk) {
          if (t[kk].code == 10)
            ParseDouble(t[kk].value, &x0);
          else if (t[kk].code == 20)
            ParseDouble(t[kk].value, &y0);
          else if (t[kk].code == 11)
            ParseDouble(t[kk].value, &x1);
          else if (t[kk].code == 21)
            ParseDouble(t[kk].value, &y1);
        }
        appendSegXF(x0, y0, x1, y1, at);
        any = true;
        k = kk;
        continue;
      }
      if (edgeKind == "2") {
        double cx = 0, cy = 0, r = 0, a0deg = 0, a1deg = 0;
        size_t kk = k + 1;
        for (; kk < hi && t[kk].code != 72 && t[kk].code != 91 && !(t[kk].code == 0); ++kk) {
          if (t[kk].code == 10)
            ParseDouble(t[kk].value, &cx);
          else if (t[kk].code == 20)
            ParseDouble(t[kk].value, &cy);
          else if (t[kk].code == 40)
            ParseDouble(t[kk].value, &r);
          else if (t[kk].code == 50)
            ParseDouble(t[kk].value, &a0deg);
          else if (t[kk].code == 51)
            ParseDouble(t[kk].value, &a1deg);
        }
        if (r > 1e-9) {
          double sweep = a1deg - a0deg;
          while (sweep < 0)
            sweep += 360.0;
          while (sweep > 360.0)
            sweep -= 360.0;
          if (sweep < 1e-6)
            sweep = 360.0;
          const double a0 = a0deg * kDegToRad;
          const double sw = sweep * kDegToRad;
          constexpr int nseg = 24;
          for (int s = 0; s < nseg; ++s) {
            const double u0 = a0 + sw * (static_cast<double>(s) / static_cast<double>(nseg));
            const double u1 = a0 + sw * (static_cast<double>(s + 1) / static_cast<double>(nseg));
            appendSegXF(cx + r * std::cos(u0), cy + r * std::sin(u0), cx + r * std::cos(u1), cy + r * std::sin(u1),
                        at);
          }
          any = true;
        }
        k = kk;
        continue;
      }
      ++k;
    }
    return any;
  };

  // Collect SOLID-fill HATCH boundary loops as filled regions (ADR-011). Each boundary path (group 92)
  // becomes one CadFilledRegion: line edges contribute their start vertex, arc edges are tessellated,
  // polyline boundaries contribute their listed vertices. Coordinates pass through the active INSERT
  // transform and are rebased to local storage, exactly like appendSegXF.
  auto appendHatchSolidFills = [&](size_t lo, size_t hi, const EntityAttributes& at) {
    // Accumulate ALL boundary paths of one hatch into a single region: loop 0 outer, the rest holes.
    CadFilledRegion region;
    auto pushVertWorld = [&](double mx, double my) {
      double ox = 0, oy = 0;
      xf.apply(mx, my, &ox, &oy);
      UpdateCoordMag(coordMagMax, ox, oy);
      region.vertsXyz.push_back(static_cast<float>(ox - st.worldDocumentOriginX));
      region.vertsXyz.push_back(static_cast<float>(oy - st.worldDocumentOriginY));
      // Z: a HATCH's boundary vertices are 10/20 only — the elevation lives on the HATCH entity
      // itself (group 30), which this parser does not yet read. Kept at 0 so widening the store
      // is a pure refactor; real elevations land with the group-30 work (REQ-057, TASK-034 step 6).
      region.vertsXyz.push_back(0.f);
    };
    auto endLoop = [&]() {
      // Drop a just-finished loop that has fewer than 3 vertices (degenerate).
      if (!region.loopStart.empty()) {
        const int start = region.loopStart.back();
        if (static_cast<int>(region.vertsXyz.size() / 3) - start < 3) {
          region.vertsXyz.resize(static_cast<size_t>(start) * 3);
          region.loopStart.pop_back();
        }
      }
    };
    auto beginLoop = [&]() { region.loopStart.push_back(static_cast<int>(region.vertsXyz.size() / 3)); };
    size_t k = lo;
    bool inBoundary = false;
    bool polylineBoundary = false;
    while (k < hi) {
      const int c = t[k].code;
      if (c == 0) break;  // end of entity
      if (c == 91) { ++k; continue; }  // number of boundary paths
      if (c == 92) {                   // new boundary path → new loop
        endLoop();
        beginLoop();
        inBoundary = true;
        int flags = 0;
        ParseIntFlexible(t[k].value, &flags);
        polylineBoundary = (flags & 2) != 0;
        ++k;
        continue;
      }
      // Pattern/seed/gradient data follows the boundary paths — stop collecting vertices there.
      if (c == 75 || c == 76 || c == 98 || c == 450 || c == 470) { inBoundary = false; ++k; continue; }
      if (!inBoundary) { ++k; continue; }
      if (polylineBoundary) {
        if (c == 10) {
          double x = 0, y = 0;
          ParseDouble(t[k].value, &x);
          if (k + 1 < hi && t[k + 1].code == 20)
            ParseDouble(t[k + 1].value, &y);
          pushVertWorld(x, y);
          k += 2;
          continue;
        }
        ++k;
        continue;
      }
      if (c == 72) {  // edge type within an edge-based boundary
        int et = 1;
        ParseIntFlexible(t[k].value, &et);
        size_t kk = k + 1;
        if (et == 2) {  // arc edge — tessellate (angles in degrees, group 73 = CCW flag)
          double cx = 0, cy = 0, r = 0, a0 = 0, a1 = 0;
          int ccw = 1;
          for (; kk < hi && t[kk].code != 72 && t[kk].code != 92 && t[kk].code != 97 &&
                 t[kk].code != 98 && t[kk].code != 0; ++kk) {
            const int cc = t[kk].code;
            if (cc == 10) ParseDouble(t[kk].value, &cx);
            else if (cc == 20) ParseDouble(t[kk].value, &cy);
            else if (cc == 40) ParseDouble(t[kk].value, &r);
            else if (cc == 50) ParseDouble(t[kk].value, &a0);
            else if (cc == 51) ParseDouble(t[kk].value, &a1);
            else if (cc == 73) ParseIntFlexible(t[kk].value, &ccw);
          }
          if (r > 1e-9) {
            double sweep = a1 - a0;
            if (ccw) { while (sweep < 0) sweep += 360.0; }
            else     { while (sweep > 0) sweep -= 360.0; }
            if (std::fabs(sweep) < 1e-6) sweep = ccw ? 360.0 : -360.0;
            constexpr int nseg = 24;
            for (int s = 0; s < nseg; ++s) {  // skip the final point — next edge supplies it
              const double u = (a0 + sweep * (static_cast<double>(s) / nseg)) * kDegToRad;
              pushVertWorld(cx + r * std::cos(u), cy + r * std::sin(u));
            }
          }
          k = kk;
          continue;
        }
        // Line (et == 1) and other edge types: take the edge start point (10,20).
        double x = 0, y = 0;
        for (; kk < hi && t[kk].code != 72 && t[kk].code != 92 && t[kk].code != 97 &&
               t[kk].code != 98 && t[kk].code != 0; ++kk) {
          const int cc = t[kk].code;
          if (cc == 10) ParseDouble(t[kk].value, &x);
          else if (cc == 20) ParseDouble(t[kk].value, &y);
        }
        pushVertWorld(x, y);
        k = kk;
        continue;
      }
      ++k;
    }
    endLoop();
    if (!region.loopStart.empty() && region.vertsXyz.size() >= 9) {  // >= 3 vertices × 3 floats
      st.cadFilledRegions.push_back(std::move(region));
      st.cadFilledRegionAttrs.push_back(at);
    }
  };

  // Common entity base fields — layer, linetype, ACI color index, and true-color override.
  // parse() handles group codes 8/6/62/420; makeAttr() builds EntityAttributes.
  struct EntityBase {
    std::string layer{"0"};
    std::string ltype;  // DXF group 6; empty → EntityAttributes default ByLayer
    int c62{256};
    bool has420{false};
    int rgb420{0};
    void parse(int code, const std::string& v) {
      if      (code ==   8) layer = v;
      else if (code ==   6) ltype = Trim(v);
      else if (code ==  62) ParseIntFlexible(v, &c62);
      else if (code == 420) has420 = ParseIntFlexible(v, &rgb420);
    }
    EntityAttributes makeAttr(const std::unordered_map<std::string, uint32_t>& lrgb) const {
      EntityAttributes at{};
      at.layer = layer;
      at.color = EntityColorStorage(c62, has420, rgb420, layer, lrgb);
      if (!ltype.empty())
        at.linetype = CadCanonicalLinetypeNameForDxf(ltype);
      return at;
    }
  };

  struct PendingLw {
    std::string layer = "0";
    std::string ltype;
    int c62 = 256;
    bool has420 = false;
    int rgb420 = 0;
    int flags = 0;
    std::vector<double> vx;
    std::vector<double> vy;
    std::vector<double> vb;  // group 42 bulge, one per vertex (0 = the edge leaving it is straight)
  };

  size_t i = entBegin;
  while (i < entEnd) {
    if (t[i].code != 0) {
      ++i;
      continue;
    }
    const std::string& typ = t[i].value;
    size_t j = entEnd;
    collectEntityRange(i, &j);

    if (EqCiNorm(typ, "VIEWPORT")) {
      (*skippedViewport)++;
      i = j;
      continue;
    }

    if (EqCiNorm(typ, "POLYLINE")) {
      EntityBase base;
      int flags70 = 0;
      size_t k = i + 1;
      for (; k < entEnd && t[k].code != 0; ++k) {
        const int c = t[k].code;
        const std::string& v = t[k].value;
        base.parse(c, v);
        if (c == 70) ParseIntFlexible(v, &flags70);
      }

      struct PolyVtx {
        double x = 0, y = 0, z = 0, bulge = 0;
      };
      std::vector<PolyVtx> verts;

      while (k < entEnd && t[k].code == 0 && EqCiNorm(t[k].value, "VERTEX")) {
        size_t vEnd = entEnd;
        collectEntityRange(k, &vEnd);
        PolyVtx pv{};
        for (size_t kk = k + 1; kk < vEnd; ++kk) {
          const int c = t[kk].code;
          const std::string& v = t[kk].value;
          if (c == 10)
            ParseDouble(v, &pv.x);
          else if (c == 20)
            ParseDouble(v, &pv.y);
          else if (c == 30)
            ParseDouble(v, &pv.z);  // a 3D POLYLINE gives every vertex its own elevation (REQ-057)
          else if (c == 42)
            ParseDouble(v, &pv.bulge);
        }
        verts.push_back(pv);
        k = vEnd;
      }

      size_t seqEnd = k;
      if (k < entEnd && t[k].code == 0 && EqCiNorm(t[k].value, "SEQEND"))
        collectEntityRange(k, &seqEnd);

      if (EntityRangeIsPaperSpace(t, i + 1, seqEnd)) {
        (*skippedPaper)++;
        i = seqEnd;
        continue;
      }

      const auto at = base.makeAttr(layerRgb);
      const int nv = static_cast<int>(verts.size());
      if (nv >= 2) {
        // REQ-316 / ADR-047: bulges are stored on the polyline now (group 42 -> per-vertex bulge),
        // not tessellated into loose segments — the entity keeps its identity (REQ-053 / REQ-204).
        std::vector<ImportPolyVert> pts;
        pts.reserve(verts.size());
        for (const PolyVtx& pv : verts)
          pts.push_back(ImportPolyVert{pv.x, pv.y, pv.z, pv.bulge});
        appendPolylineXF(pts, (flags70 & 1) != 0, at);
      }

      i = seqEnd;
      continue;
    }

    if (EntityRangeIsPaperSpace(t, i + 1, j)) {
      (*skippedPaper)++;
      i = j;
      continue;
    }

    if (typ == "LINE") {
      EntityBase base;
      double x0 = 0, y0 = 0, z0 = 0, x1 = 0, y1 = 0, z1 = 0;
      for (size_t k = i + 1; k < j; ++k) {
        const int c = t[k].code;
        const std::string& v = t[k].value;
        base.parse(c, v);
        if      (c == 10) ParseDouble(v, &x0);
        else if (c == 20) ParseDouble(v, &y0);
        else if (c == 30) ParseDouble(v, &z0);
        else if (c == 11) ParseDouble(v, &x1);
        else if (c == 21) ParseDouble(v, &y1);
        else if (c == 31) ParseDouble(v, &z1);
      }
      appendSegXF(x0, y0, x1, y1, base.makeAttr(layerRgb), z0, z1);  // groups 30/31 (REQ-057)
      i = j;
      continue;
    }

    if (typ == "LWPOLYLINE") {
      PendingLw lw;
      double pendX = NAN;
      double lwElev = 0.0;  // DXF group 38 — the polyline's constant Z (absent → 0)
      for (size_t k = i + 1; k < j; ++k) {
        const int c = t[k].code;
        const std::string& v = t[k].value;
        if (c == 8)
          lw.layer = v;
        else if (c == 6)
          lw.ltype = Trim(v);
        else if (c == 62)
          ParseIntFlexible(v, &lw.c62);
        else if (c == 420)
          lw.has420 = ParseIntFlexible(v, &lw.rgb420);
        else if (c == 70)
          ParseIntFlexible(v, &lw.flags);
        else if (c == 38)
          ParseDouble(v, &lwElev);  // LWPOLYLINE carries ONE elevation for all vertices (REQ-057)
        else if (c == 10) {
          ParseDouble(v, &pendX);
        } else if (c == 20) {
          double yv = 0;
          if (ParseDouble(v, &yv) && std::isfinite(pendX)) {
            lw.vx.push_back(pendX);
            lw.vy.push_back(yv);
            lw.vb.push_back(0.0);
            pendX = NAN;
          }
        } else if (c == 42 && !lw.vb.empty()) {
          ParseDouble(v, &lw.vb.back());  // group 42 trails the 10/20 pair of the vertex it bulges
        }
      }
      EntityAttributes at{};
      at.layer = lw.layer;
      at.color = EntityColorStorage(lw.c62, lw.has420, lw.rgb420, lw.layer, layerRgb);
      if (!lw.ltype.empty())
        at.linetype = CadCanonicalLinetypeNameForDxf(lw.ltype);
      const int nv = static_cast<int>(lw.vx.size());
      if (nv >= 2) {
        // REQ-316 / ADR-047: group 42 bulges are stored on the polyline (per-vertex), not
        // tessellated — the LWPOLYLINE keeps its identity and its arcs (REQ-053 / REQ-204).
        std::vector<ImportPolyVert> pts;
        pts.reserve(lw.vx.size());
        for (int a = 0; a < nv; ++a)
          pts.push_back(ImportPolyVert{lw.vx[static_cast<size_t>(a)], lw.vy[static_cast<size_t>(a)], lwElev,
                                       lw.vb[static_cast<size_t>(a)]});
        appendPolylineXF(pts, (lw.flags & 1) != 0, at);
      }
      i = j;
      continue;
    }

    if (typ == "CIRCLE") {
      EntityBase base;
      double cx = 0, cy = 0, cz = 0, rad = 0;
      // Group 210 defaults to world +Z when absent, which is what every flat DXF omits.
      double nx = 0, ny = 0, nz = 1;
      for (size_t k = i + 1; k < j; ++k) {
        const int c = t[k].code;
        const std::string& v = t[k].value;
        base.parse(c, v);
        if      (c == 10) ParseDouble(v, &cx);
        else if (c == 20) ParseDouble(v, &cy);
        else if (c == 30) ParseDouble(v, &cz);
        else if (c == 40) ParseDouble(v, &rad);
        else if (c == 210) ParseDouble(v, &nx);   // REQ-312
        else if (c == 220) ParseDouble(v, &ny);
        else if (c == 230) ParseDouble(v, &nz);
      }
      ray3d::Vec3 w{cx, cy, cz};
      if (!DxfExtrusionIsFlat(nx, ny, nz) && !DxfOcsToWorld(cx, cy, cz, nx, ny, nz, &w)) {
        refuseDegenerateExtrusion();  // a zero-length 210: refused, not silently taken as flat (REQ-201)
        i = j;
        continue;
      }
      appendCircleXF(w.x, w.y, rad, base.makeAttr(layerRgb), w.z, nx, ny, nz);  // group 30 (REQ-057)
      i = j;
      continue;
    }

    if (typ == "ARC") {
      EntityBase base;
      double cx = 0, cy = 0, cz = 0, rad = 0, a0deg = 0, a1deg = 0;
      double nx = 0, ny = 0, nz = 1;   // group 210 default (REQ-312)
      for (size_t k = i + 1; k < j; ++k) {
        const int c = t[k].code;
        const std::string& v = t[k].value;
        base.parse(c, v);
        if      (c == 10) ParseDouble(v, &cx);
        else if (c == 20) ParseDouble(v, &cy);
        else if (c == 30) ParseDouble(v, &cz);
        else if (c == 40) ParseDouble(v, &rad);
        else if (c == 50) ParseDouble(v, &a0deg);
        else if (c == 51) ParseDouble(v, &a1deg);
        else if (c == 210) ParseDouble(v, &nx);
        else if (c == 220) ParseDouble(v, &ny);
        else if (c == 230) ParseDouble(v, &nz);
      }
      const auto at = base.makeAttr(layerRgb);
      ray3d::Vec3 w{cx, cy, cz};
      if (!DxfExtrusionIsFlat(nx, ny, nz) && !DxfOcsToWorld(cx, cy, cz, nx, ny, nz, &w)) {
        refuseDegenerateExtrusion();
        i = j;
        continue;
      }
      if (rad > 1e-9) {
        float startRadF = 0.f, sweepRadF = 0.f;
        DxfArcAnglesFromDegrees(a0deg, a1deg, &startRadF, &sweepRadF);
        appendArcXF(w.x, w.y, rad, static_cast<double>(startRadF), static_cast<double>(sweepRadF), at,
                    w.z, nx, ny, nz);  // group 30 (REQ-057)
      }
      i = j;
      continue;
    }

    if (typ == "ELLIPSE") {
      EntityBase base;
      double cx = 0, cy = 0, cz = 0, mx = 1, my = 0, mz = 0, ratio = 0.5, t0 = 0, t1 = 2.0 * kPi;
      for (size_t k = i + 1; k < j; ++k) {
        const int c = t[k].code;
        const std::string& v = t[k].value;
        base.parse(c, v);
        if      (c == 10) ParseDouble(v, &cx);
        else if (c == 20) ParseDouble(v, &cy);
        else if (c == 30) ParseDouble(v, &cz);
        else if (c == 11) ParseDouble(v, &mx);
        else if (c == 21) ParseDouble(v, &my);
        else if (c == 31) ParseDouble(v, &mz);
        else if (c == 40) ParseDouble(v, &ratio);
        else if (c == 41) ParseDouble(v, &t0);
        else if (c == 42) ParseDouble(v, &t1);
      }
      (void)mz;
      if (ratio <= 1e-12) ratio = 1e-12;
      appendEllipseXF(cx, cy, mx, my, ratio, t0, t1, base.makeAttr(layerRgb), cz);  // group 30 (REQ-057)
      i = j;
      continue;
    }

    if (typ == "POINT") {
      EntityBase base;
      double px = 0, py = 0, pz = 0;
      bool isGoSurvey = false, inGoSurveyXdata = false;
      int sid = 0, slabel = 0;
      std::string sdesc, sraw;
      int nGoSurveyStrings = 0;  // how many XDATA 1000s seen; position identifies which field
      for (size_t k = i + 1; k < j; ++k) {
        const int c = t[k].code;
        const std::string& v = t[k].value;
        base.parse(c, v);
        if      (c == 10) ParseDouble(v, &px);
        else if (c == 20) ParseDouble(v, &py);
        else if (c == 30) ParseDouble(v, &pz);
        else if (c == 1001) { inGoSurveyXdata = (v == "GOSURVEY"); if (inGoSurveyXdata) isGoSurvey = true; }
        else if (inGoSurveyXdata && c == 1071) ParseIntFlexible(v, &sid);
        else if (inGoSurveyXdata && c == 1070) ParseIntFlexible(v, &slabel);
        else if (inGoSurveyXdata && c == 1000) {
          // Order is the discriminator (REQ-066): first 1000 = description, second = raw
          // description. A pre-REQ-066 POINT carries only the first, so sraw stays empty.
          if (nGoSurveyStrings == 0) sdesc = v;
          else if (nGoSurveyStrings == 1) sraw = v;
          ++nGoSurveyStrings;
        }
      }
      const auto at = base.makeAttr(layerRgb);
      if (isGoSurvey) {
        // REQ-023: reconstruct the survey point, applying the same INSERT transform + world-origin offset
        // used for geometry (so it lands 1:1, in precise local coords). Collected in a side buffer and
        // merged with the session's existing points after parsing rather than pushed directly.
        double wx = 0, wy = 0;
        xf.apply(px, py, &wx, &wy);
        SurveyPoint sp;
        sp.id        = sid;
        sp.easting   = static_cast<float>(wx - st.worldDocumentOriginX);
        sp.northing  = static_cast<float>(wy - st.worldDocumentOriginY);
        sp.elevation = static_cast<float>(pz);
        sp.description = sdesc;
        sp.rawDescription = sraw;  // empty for a pre-REQ-066 DXF — the documented fallback case
        sp.layer = at.layer.empty() ? std::string("0") : at.layer;
        sp.labelStyle = static_cast<SurveyPointLabelStyle>(
            std::clamp(slabel, 0, static_cast<int>(SurveyPointLabelStyle::NumberNorthEastElev)));
        if (embeddedPointsLocal)
          embeddedPointsLocal->push_back(sp);
        i = j;
        continue;
      }
      double arm = (*coordMagMax > 1.0) ? (*coordMagMax * 1e-7) : 0.01;
      arm = std::clamp(arm, 1e-6, std::max(*coordMagMax * 1e-6, 0.5));
      // Split each arm at center so (px,py) is a stored endpoint — snap hits the exact point.
      appendSegXF(px - arm, py, px, py, at);
      appendSegXF(px, py, px + arm, py, at);
      appendSegXF(px, py - arm, px, py, at);
      appendSegXF(px, py, px, py + arm, at);
      i = j;
      continue;
    }

    if (typ == "TEXT" || typ == "ATTDEF") {
      EntityBase base;
      double x = 0, y = 0, ax = 0, ay = 0, h = 2.5, rot = 0;
      int halign = 0, valign = 0;
      bool haveAlign = false;
      std::string txt, styleName, attrTag, attrPrompt;
      for (size_t k = i + 1; k < j; ++k) {
        const int c = t[k].code;
        const std::string& v = t[k].value;
        base.parse(c, v);
        if      (c == 10) ParseDouble(v, &x);
        else if (c == 20) ParseDouble(v, &y);
        else if (c == 11) { ParseDouble(v, &ax); haveAlign = true; }
        else if (c == 21) { ParseDouble(v, &ay); haveAlign = true; }
        else if (c == 40) ParseDouble(v, &h);
        else if (c == 50) ParseDouble(v, &rot); // ObjectARX: AcDbText group 50 is rotation in DEGREES
        else if (c ==  7) styleName = Trim(v);
        else if (c == 72) ParseIntFlexible(v, &halign);
        else if (c == 73) ParseIntFlexible(v, &valign);
        else if (c == 74) ParseIntFlexible(v, &valign);  // ATTDEF vertical alignment
        else if (c ==  1) txt = v;
        else if (c ==  2 && typ == "ATTDEF") attrTag = Trim(v);
        else if (c ==  3) attrPrompt = v;
      }
      if (typ == "ATTDEF") {
        const bool useAlignPt = haveAlign && (halign != 0 || valign != 0);
        const double px = useAlignPt ? ax : x;
        const double py = useAlignPt ? ay : y;
        const AnnXf a = xfAnnotation(px, py, h, rot * kDegToRad);
        CadBlockAttrDef ad;
        ad.tag = attrTag.empty() ? std::string("TAG") : attrTag;
        ad.prompt = attrPrompt;
        ad.defaultValue = txt;
        ad.localX = static_cast<float>(a.insX);
        ad.localY = static_cast<float>(a.insY);
        ad.height = (a.plottedH > 1.e-6f) ? a.plottedH : 0.125f;
        ad.rotationRad = a.rotWorld;
        st.importedDxfAttrDefs.push_back(std::move(ad));
        i = j;
        continue;
      }
      // %%u toggles underline; the title-block convention is a leading %%u (underline the whole string).
      auto hasUnderlineCode = [](const std::string& s) {
        for (size_t p = 0; p + 2 < s.size(); ++p)
          if (s[p] == '%' && s[p + 1] == '%' && (s[p + 2] == 'u' || s[p + 2] == 'U'))
            return true;
        return false;
      };
      const bool underline = hasUnderlineCode(txt);
      txt = DxfPercentCodesToPlain(txt);
      if (txt.empty()) { i = j; continue; }
      std::string styleRef, styleFont;
      bool styleItalic = false;
      resolveStyle(styleName, &styleRef, &styleFont, &styleItalic);
      // When the text is non-left/baseline aligned, group 11/21 carries the true placement point
      // (group 10/20 is then the unused first-fit point), so prefer it.
      const bool useAlignPt = haveAlign && (halign != 0 || valign != 0);
      const double px = useAlignPt ? ax : x;
      const double py = useAlignPt ? ay : y;
      const AnnXf a = xfAnnotation(px, py, h, rot * kDegToRad);
      // The renderer draws TEXT downward from insX/insY (top-left), but DXF group 10/20 is the baseline
      // (or the alignment point). Convert to the top so the glyphs sit above the baseline like AutoCAD.
      const double worldH = h * a.scaleY;
      double topY = a.insY + worldH;                 // valign 0 (baseline) / 1 (bottom)
      if (valign == 2)      topY = a.insY + 0.5 * worldH;  // middle
      else if (valign == 3) topY = a.insY;                 // top
      CadAnnotation an;
      an.kind = CadAnnotation::Kind::Text;
      an.insX = static_cast<float>(a.insX);
      an.insY = static_cast<float>(topY);
      an.plottedHeightInches = a.plottedH;
      an.rotationRad = a.rotWorld;
      an.text = txt;
      an.fontFamily = styleFont;
      an.italic = styleItalic;
      an.underline = underline;
      // REQ-044: link to the imported STYLE so editing that style ripples to this text. The DXF group-40
      // height is per-text, so it is an override (ovHeight) — a style edit changes font/italic, not height.
      an.styleName = styleRef;
      an.ovHeight = true;
      st.cadAnnotations.push_back(an);
      st.cadAnnotationAttrs.push_back(base.makeAttr(layerRgb));
      i = j;
      continue;
    }

    if (typ == "MTEXT") {
      EntityBase base;
      double ix = 0, iy = 0, h = 3, refW = 0, rot = 0, dirX = 0, dirY = 0;
      int attach = 1;  // 1 = top-left (DXF default)
      bool haveDir = false, haveRot = false;
      std::string txt, styleName;
      bool isSurveyLabel = false;
      // AutoCAD 2018+ (AC1032) appends an "Embedded Object" (group 101) to MTEXT — a second serialization
      // of the same text that REUSES the entity's group codes with different meanings: 10/20/30 is the
      // X-axis direction, 11/21/31 the insertion point, 40 the reference-rectangle width, 41/42/43 the
      // rectangle/extent sizes, 71/72 fixed flags. Feeding those to the entity fields overwrote the
      // insertion point with the direction vector (~1,0), read the insertion point as a direction (a
      // garbage rotation), and replaced the text height with the box width — so every label landed far
      // off the drawing at a wildly wrong size. The embedded object is a separate object per the DXF
      // spec, so skip its codes; XDATA (>= 1000) still trails it and is still read.
      bool inEmbedded = false;
      for (size_t k = i + 1; k < j; ++k) {
        const int c = t[k].code;
        const std::string& v = t[k].value;
        if (c == 101) { inEmbedded = true; continue; }
        if (inEmbedded) {
          if (c < 1000) continue;
          inEmbedded = false;
        }
        base.parse(c, v);
        if      (c == 10) ParseDouble(v, &ix);
        else if (c == 20) ParseDouble(v, &iy);
        else if (c == 11) { ParseDouble(v, &dirX); haveDir = true; }
        else if (c == 21) { ParseDouble(v, &dirY); haveDir = true; }
        else if (c == 40) ParseDouble(v, &h);
        else if (c == 41) ParseDouble(v, &refW);
        else if (c == 50) { ParseDouble(v, &rot); haveRot = true; } // group 50 is DEGREES
        else if (c == 71) ParseIntFlexible(v, &attach);
        else if (c ==  7) styleName = Trim(v);
        else if (c == 1 || c == 3) txt += v;
        else if (c == 1001 && v == "GOSURVEY") isSurveyLabel = true;
      }
      // REQ-023: a survey-point label is regenerated from the reconstructed point;
      // skip the exported MTEXT so we don't create a duplicate/orphan annotation.
      if (isSurveyLabel) { i = j; continue; }
      txt = DxfMtextToRichWire(txt);  // preserve \L underline, \f font, bold/italic as rich tags
      if (txt.empty()) { i = j; continue; }
      std::string styleRef, styleFont;
      bool styleItalic = false;
      resolveStyle(styleName, &styleRef, &styleFont, &styleItalic);
      // Rotation: prefer the X-axis direction vector (group 11/21); else group 50 degrees.
      double rotModel = 0;
      if (haveDir && (std::fabs(dirX) > 1e-12 || std::fabs(dirY) > 1e-12))
        rotModel = std::atan2(dirY, dirX);
      else if (haveRot)
        rotModel = rot * kDegToRad;
      const AnnXf a = xfAnnotation(ix, iy, h, rotModel);
      // Box: width = reference width (model→world via scaleX); height from line count. Positioned by the
      // attachment point (group 71): col 0/1/2 = left/center/right, row 0/1/2 = top/middle/bottom.
      int lineCount = 1;
      for (char ch : txt)
        if (ch == '\n') ++lineCount;
      const double hWorld = static_cast<double>(a.plottedH) *
                            std::max(static_cast<double>(st.modelUnitsPerPlottedInch), 1e-6);
      const double textW = static_cast<double>(txt.size()) * hWorld * 0.62;
      double boxW = (refW > 1e-9 ? refW * a.scaleX : textW);
      if (boxW < hWorld) boxW = std::max(textW, hWorld);
      const double boxH = std::max(hWorld * 1.3, lineCount * hWorld * 1.3);
      const int col = (attach - 1) % 3;  // 0 left, 1 center, 2 right
      const int row = (attach - 1) / 3;  // 0 top, 1 middle, 2 bottom
      const double left = a.insX - (col == 0 ? 0.0 : col == 1 ? boxW * 0.5 : boxW);
      const double top  = a.insY + (row == 0 ? 0.0 : row == 1 ? boxH * 0.5 : boxH);
      CadAnnotation an;
      an.kind = CadAnnotation::Kind::Mtext;
      an.mtextAttach = std::clamp(attach, 1, 9);
      an.plottedHeightInches = a.plottedH;
      an.rotationRad = a.rotWorld;
      an.text = txt;
      an.fontFamily = styleFont;  // base typeface; per-run [[font:…]] tags may override
      an.italic = styleItalic;
      // REQ-044: link to the imported STYLE (font/italic ripple on a style edit); DXF group-40 height is a
      // per-text override so the imported box height is preserved.
      an.styleName = styleRef;
      an.ovHeight = true;
      an.boxMinX = static_cast<float>(left);
      an.boxMaxX = static_cast<float>(left + boxW);
      an.boxMinY = static_cast<float>(top - boxH);
      an.boxMaxY = static_cast<float>(top);
      an.insX = an.boxMinX;  // GoSurvey stores the box bottom-left as the insertion point
      an.insY = an.boxMinY;
      st.cadAnnotations.push_back(an);
      st.cadAnnotationAttrs.push_back(base.makeAttr(layerRgb));
      i = j;
      continue;
    }

    if (EqCiNorm(typ, "DIMENSION")) {
      EntityBase base;
      double px10 = NAN, py10 = NAN, px13 = NAN, py13 = NAN, px14 = NAN, py14 = NAN,
             px15 = NAN, py15 = NAN, px16 = NAN, py16 = NAN;
      for (size_t k = i + 1; k < j; ++k) {
        const int c = t[k].code;
        const std::string& v = t[k].value;
        base.parse(c, v);
        if      (c == 10) ParseDouble(v, &px10);
        else if (c == 20) ParseDouble(v, &py10);
        else if (c == 13) ParseDouble(v, &px13);
        else if (c == 23) ParseDouble(v, &py13);
        else if (c == 14) ParseDouble(v, &px14);
        else if (c == 24) ParseDouble(v, &py14);
        else if (c == 15) ParseDouble(v, &px15);
        else if (c == 25) ParseDouble(v, &py15);
        else if (c == 16) ParseDouble(v, &px16);
        else if (c == 26) ParseDouble(v, &py16);
      }
      const auto at = base.makeAttr(layerRgb);
      auto finitePair = [](double x, double y) { return std::isfinite(x) && std::isfinite(y); };
      if (finitePair(px13, py13) && finitePair(px14, py14))
        appendSegXF(px13, py13, px14, py14, at);
      else if (finitePair(px10, py10) && finitePair(px15, py15))
        appendSegXF(px10, py10, px15, py15, at);
      else if (finitePair(px10, py10) && finitePair(px16, py16))
        appendSegXF(px10, py10, px16, py16, at);
      else if (finitePair(px10, py10) && finitePair(px13, py13))
        appendSegXF(px10, py10, px13, py13, at);
      else if (finitePair(px13, py13) && finitePair(px15, py15))
        appendSegXF(px13, py13, px15, py15, at);
      i = j;
      continue;
    }

    if (EqCiNorm(typ, "ACAD_TABLE")) {
      EntityBase base;
      double minx = 1e300, maxx = -1e300, miny = 1e300, maxy = -1e300;
      bool anyPt = false, haveX = false;
      double pendX = 0;
      for (size_t k = i + 1; k < j; ++k) {
        const int c = t[k].code;
        const std::string& v = t[k].value;
        base.parse(c, v);
        if (c == 10 || c == 11) {
          ParseDouble(v, &pendX);
          haveX = true;
        } else if ((c == 20 || c == 21) && haveX) {
          double yv = 0;
          if (ParseDouble(v, &yv)) {
            minx = std::min(minx, pendX); maxx = std::max(maxx, pendX);
            miny = std::min(miny, yv);   maxy = std::max(maxy, yv);
            anyPt = true;
          }
          haveX = false;
        }
      }
      const auto at = base.makeAttr(layerRgb);
      if (anyPt && maxx > minx && maxy > miny && maxx - minx < 1e200 && maxy - miny < 1e200) {
        appendSegXF(minx, miny, maxx, miny, at);
        appendSegXF(maxx, miny, maxx, maxy, at);
        appendSegXF(maxx, maxy, minx, maxy, at);
        appendSegXF(minx, maxy, minx, miny, at);
      }
      i = j;
      continue;
    }

    if (typ == "HATCH") {
      EntityBase base;
      bool solid = false;
      std::string patName;
      for (size_t k = i + 1; k < j; ++k) {
        const int c = t[k].code;
        const std::string& v = t[k].value;
        base.parse(c, v);
        if      (c == 70) { int f = 0; if (ParseIntFlexible(v, &f)) solid = (f != 0); } // 70 = solid-fill flag
        else if (c ==  2) patName = Trim(v);
      }
      const auto at = base.makeAttr(layerRgb);
      // ADR-011: a SOLID-fill hatch becomes filled region(s); a pattern hatch keeps the boundary outline.
      if (solid || EqCiStr(patName, "SOLID"))
        appendHatchSolidFills(i + 1, j, at);
      else
        (void)hatchEmit(i + 1, j, at);
      i = j;
      continue;
    }

    if (typ == "INSERT") {
      EntityBase base;
      std::string blk;
      double ix = 0, iy = 0, sx = 1, sy = 1, rot = 0;
      bool has41 = false, has42 = false;
      for (size_t k = i + 1; k < j; ++k) {
        const int c = t[k].code;
        const std::string& v = t[k].value;
        base.parse(c, v);
        if      (c ==  2) blk = Trim(v);
        else if (c == 10) ParseDouble(v, &ix);
        else if (c == 20) ParseDouble(v, &iy);
        else if (c == 41) { ParseDouble(v, &sx); has41 = true; }
        else if (c == 42) { ParseDouble(v, &sy); has42 = true; }
        else if (c == 50) ParseDouble(v, &rot);
      }
      if (!has41) sx = 1;
      if (!has42) sy = 1;
      const auto at = base.makeAttr(layerRgb);

      if (!blockDefs || blk.empty() || insertDepth >= kMaxInsertDepth) {
        (*skippedUnknown)++;
        if (skipCounts)
          (*skipCounts)[typ]++;
        i = j;
        continue;
      }
      if (IsPaperSpaceBlockName(blk)) {
        (*skippedPaper)++;
        i = j;
        continue;
      }
      if (IsModelSpaceBlockName(blk)) {
        (*skippedUnknown)++;
        if (skipCounts)
          (*skipCounts)["INSERT *MODEL_SPACE"]++;
        i = j;
        continue;
      }

      std::pair<size_t, size_t> br = {0, 0};
      const auto itEx = blockDefs->find(blk);
      if (itEx != blockDefs->end())
        br = itEx->second;
      else {
        for (const auto& kv : *blockDefs) {
          if (EqCiStr(Trim(kv.first), Trim(blk))) {
            br = kv.second;
            break;
          }
        }
      }
      if (br.first >= br.second) {
        (*skippedUnknown)++;
        if (skipCounts)
          (*skipCounts)["INSERT ?"]++;
        i = j;
        continue;
      }

      const Affine2D ins = Affine2D::FromInsert(ix, iy, sx, sy, rot);
      const Affine2D nest = xf.compose(ins);
      ParseEntityRegion(t, br.first, br.second, st, layerRgb, blockDefs, nest, insertDepth + 1, coordMagMax, skippedPaper,
                        skippedViewport, skippedUnknown, skipCounts, embeddedPointsLocal, textStyles,
                        degenerateExtrusionsOut);
      // Store the INSERT insertion point as a zero-length segment so snap can hit the exact
      // world coordinate — Civil 3D COGO points use INSERT entities whose center must be snap-able.
      appendSegXF(ix, iy, ix, iy, base.makeAttr(layerRgb));
      i = j;
      continue;
    }

    if (EqCiNorm(typ, "ENDSEC") || EqCiNorm(typ, "SECTION") || EqCiNorm(typ, "ENDBLK"))
      break;

    if (EqCiNorm(typ, "VERTEX")) {
      i = j;
      continue;
    }

    if (EqCiNorm(typ, "BLOCK") || EqCiNorm(typ, "SEQEND")) {
      collectEntityRange(i, &j);
      i = j;
      continue;
    }

    (*skippedUnknown)++;
    if (skipCounts)
      (*skipCounts)[typ]++;
    i = j;
  }
}
bool BuildLayerRgbTable(const std::vector<DxfPair>& t, std::unordered_map<std::string, uint32_t>* layerRgb,
                        std::vector<std::string>& log) {
  layerRgb->clear();
  size_t i = 0;
  while (i < t.size()) {
    if (t[i].code != 0 || t[i].value != "LAYER") {
      ++i;
      continue;
    }
    size_t j = i + 1;
    while (j < t.size() && t[j].code != 0)
      ++j;

    bool foundSubclass = false;
    for (size_t k = i + 1; k < j; ++k) {
      if (t[k].code == 100 && t[k].value == "AcDbLayerTableRecord") {
        foundSubclass = true;
        break;
      }
    }
    if (!foundSubclass) {
      i = j;
      continue;
    }

    std::string name = "0";
    int c62 = 7;
    bool has420 = false;
    int rgb420 = 0;
    bool pastSubclass = false;
    for (size_t k = i + 1; k < j; ++k) {
      if (!pastSubclass) {
        if (t[k].code == 100 && t[k].value == "AcDbLayerTableRecord")
          pastSubclass = true;
        continue;
      }
      if (t[k].code == 2)
        name = t[k].value;
      else if (t[k].code == 62)
        ParseIntFlexible(t[k].value, &c62);
      else if (t[k].code == 420) {
        has420 = ParseIntFlexible(t[k].value, &rgb420);
      }
    }

    uint32_t packed = DxfRgbPackedFromAci(7);
    if (has420)
      packed = static_cast<uint32_t>(rgb420) & 0xFFFFFFu;
    else {
      const int aci = NormalizeAci(c62);
      if (aci != 256)
        packed = DxfRgbPackedFromAci(aci);
      else
        packed = DxfRgbPackedFromAci(7);
    }
    (*layerRgb)[name] = packed;
    i = j;
  }
  log.push_back("DXF layer table — " + std::to_string(layerRgb->size()) + " layer color(s).");
  return true;
}

// Build a map of STYLE-table records: style name → {font file, italic}. TEXT/MTEXT group 7 references
// these; the default style is "Standard". Font names are passed to the font registry, which substitutes
// SHX fonts (romans.shx, txt, …) with the closest TrueType.
void BuildTextStyleTable(const std::vector<DxfPair>& t, std::unordered_map<std::string, DxfTextStyle>* styles) {
  styles->clear();
  size_t i = 0;
  while (i < t.size()) {
    if (t[i].code != 0 || !EqCiNorm(t[i].value, "STYLE")) {
      ++i;
      continue;
    }
    size_t j = i + 1;
    while (j < t.size() && t[j].code != 0)
      ++j;
    std::string name, font;
    double oblique = 0;
    for (size_t k = i + 1; k < j; ++k) {
      if (t[k].code == 2) name = Trim(t[k].value);
      else if (t[k].code == 3) font = Trim(t[k].value);
      else if (t[k].code == 50) ParseDouble(t[k].value, &oblique);
    }
    if (!name.empty()) {
      DxfTextStyle s;
      s.font = font;
      s.italic = std::fabs(oblique) > 1e-6;
      (*styles)[name] = s;
    }
    i = j;
  }
}

// Register each imported DXF STYLE record as a live GoSurvey text style (REQ-044) so that editing the
// style later updates every TEXT/MTEXT that references it — completing the DXF STYLE-table round-trip that
// ADR-020 deferred. A style already present in the drawing is left as-is, except that an unset (empty) font
// is filled from the DXF, so a user/session-defined style is never clobbered. Name matching is exact: the
// imported entities link by the same DXF style name, so the reference always resolves to a real style.
void RegisterDxfTextStylesIntoDrawing(AppCommandState& st,
                                      const std::unordered_map<std::string, DxfTextStyle>& dxf) {
  TextStyles::EnsureStandard(st.textStyles);
  for (const auto& kv : dxf) {
    const std::string& name = kv.first;
    if (name.empty())
      continue;
    if (TextStyle* existing = TextStyles::Find(st.textStyles, name)) {
      if (existing->fontFamily.empty() && !kv.second.font.empty()) {
        existing->fontFamily = kv.second.font;
        existing->italic = kv.second.italic;
      }
      continue;
    }
    TextStyle s;
    s.name = name;
    s.fontFamily = kv.second.font;
    s.italic = kv.second.italic;
    st.textStyles.push_back(s);
  }
}

uint32_t AttrResolvedRgbPacked(const EntityAttributes& e,
                               const std::unordered_map<std::string, uint32_t>& layerRgbHint) {
  const std::string& c = e.color;
  uint32_t rgb = 0;
  if (c.empty() || c == "ByLayer") {
    auto it = layerRgbHint.find(e.layer);
    if (it != layerRgbHint.end())
      return it->second & 0xFFFFFFu;
    return DxfRgbPackedFromAci(7);
  }
  if (!c.empty() && c[0] == '#' && Hex7ToRgbPacked(c, &rgb))
    return rgb & 0xFFFFFFu;
  if (NamedColorToRgbPacked(c, &rgb))
    return rgb & 0xFFFFFFu;
  return DxfRgbPackedFromAci(7);
}

void BuildExportLayerRgbHint(const AppCommandState& st, std::unordered_map<std::string, uint32_t>* hint) {
  hint->clear();
  std::unordered_set<std::string> layers;
  const size_t nSeg = st.userLinesFlat.size() / 6;
  for (size_t i = 0; i < nSeg && i < st.userLineAttrs.size(); ++i)
    layers.insert(st.userLineAttrs[i].layer.empty() ? std::string("0") : st.userLineAttrs[i].layer);
  const size_t nCirc = st.userCirclesCxCyZR.size() / 4;
  for (size_t i = 0; i < nCirc && i < st.userCircleAttrs.size(); ++i)
    layers.insert(st.userCircleAttrs[i].layer.empty() ? std::string("0") : st.userCircleAttrs[i].layer);
  for (size_t i = 0; i < st.cadAnnotationAttrs.size(); ++i) {
    const EntityAttributes& at = st.cadAnnotationAttrs[i];
    layers.insert(at.layer.empty() ? std::string("0") : at.layer);
  }
  for (const SurveyPoint& p : st.surveyPoints)
    layers.insert(p.layer.empty() ? std::string("0") : p.layer);
  layers.insert("0");
  const uint32_t defw = DxfRgbPackedFromAci(7);
  for (const auto& lyr : layers)
    (*hint)[lyr] = defw;

  auto seedFromAttrs = [&](const EntityAttributes& at) {
    if (at.color.empty() || at.color == "ByLayer")
      return;
    uint32_t rgb = 0;
    if (!at.color.empty() && at.color[0] == '#' && Hex7ToRgbPacked(at.color, &rgb))
      (*hint)[at.layer.empty() ? std::string("0") : at.layer] = rgb & 0xFFFFFFu;
    else if (NamedColorToRgbPacked(at.color, &rgb))
      (*hint)[at.layer.empty() ? std::string("0") : at.layer] = rgb & 0xFFFFFFu;
  };

  for (size_t i = 0; i < nSeg && i < st.userLineAttrs.size(); ++i)
    seedFromAttrs(st.userLineAttrs[i]);
  for (size_t i = 0; i < nCirc && i < st.userCircleAttrs.size(); ++i)
    seedFromAttrs(st.userCircleAttrs[i]);
  for (size_t i = 0; i < st.cadAnnotationAttrs.size(); ++i)
    seedFromAttrs(st.cadAnnotationAttrs[i]);

  for (const CadLayerRow& row : st.drawingLayerTable) {
    if (row.name.empty())
      continue;
    uint32_t rgb = 0;
    if (!row.color.empty() && row.color[0] == '#' && Hex7ToRgbPacked(row.color, &rgb))
      (*hint)[row.name] = rgb & 0xFFFFFFu;
    else if (NamedColorToRgbPacked(row.color, &rgb))
      (*hint)[row.name] = rgb & 0xFFFFFFu;
  }
}

const CadLayerRow* FindLayerRowDxfExport(const AppCommandState& st, const std::string& name) {
  for (const auto& r : st.drawingLayerTable) {
    if (EqCiStr(r.name, name))
      return &r;
  }
  return nullptr;
}

std::string DxfExportEntityLtype6(const EntityAttributes& at) {
  const std::string c = CadCanonicalLinetypeNameForDxf(at.linetype);
  if (c.empty() || c == "ByLayer")
    return "BYLAYER";
  if (c == "ByBlock")
    return "BYBLOCK";
  return CadCanonicalLinetypeNameForDxf(at.linetype);
}

bool ImportDxfFile_Impl(AppCommandState& st, const char* pathUtf8, std::vector<std::string>& log) {
  if (!pathUtf8 || pathUtf8[0] == '\0') {
    log.push_back("DXF import — no path.");
    return false;
  }
  std::vector<DxfPair> pairs;
  std::string err;
  const std::filesystem::path p = std::filesystem::path(pathUtf8);
  if (!LoadDxfPairs(p, &pairs, &err)) {
    log.push_back(err);
    return false;
  }

  std::unordered_map<std::string, uint32_t> layerRgb;
  BuildLayerRgbTable(pairs, &layerRgb, log);

  std::unordered_map<std::string, std::pair<size_t, size_t>> blockDefs;
  CollectBlockDefinitions(pairs, &blockDefs);

  std::unordered_map<std::string, DxfTextStyle> textStyles;
  BuildTextStyleTable(pairs, &textStyles);
  RegisterDxfTextStylesIntoDrawing(st, textStyles);  // REQ-044: imported STYLEs become live text styles

  size_t eb = 0, ee = 0;
  const bool hasEntitiesSec = FindSectionBounds(pairs, "ENTITIES", &eb, &ee);
  size_t mb = 0, me = 0;
  const bool hasModelSpace = FindModelSpaceEntityRange(pairs, &mb, &me);

  if (!hasEntitiesSec && !hasModelSpace) {
    log.push_back(
        "DXF import — no ENTITIES section and no *MODEL_SPACE block found. Save as ASCII DXF (not Binary) from AutoCAD.");
    return false;
  }

  // Import replaces the CAD drawing, but KEEPS survey points already in the session (e.g. CSV-imported)
  // so the two orders — points-then-DXF and DXF-then-points — behave the same. Points embedded in the
  // DXF are reconstructed below and merged with the existing ones (ID conflicts resolved via a prompt).
  const double oldOriginX = st.worldDocumentOriginX;
  const double oldOriginY = st.worldDocumentOriginY;
  ResetCadToolStateToIdle(st);
  ClearCadGeometry(st);  // zeroes the document origin and drops all annotations (incl. survey labels)
  st.selectedSurveyPointIndices.clear();
  const bool hadExistingPoints = !st.surveyPoints.empty();
  if (hadExistingPoints) {
    // The points are still in the OLD local frame but the origin is now 0; move them to world space so the
    // origin established below rebases them with the new geometry. Their label links were just cleared, so
    // reset each so EnsureSurveyPointLabelMtext rebuilds fresh labels.
    if (oldOriginX != 0.0 || oldOriginY != 0.0)
      CadCoord::ShiftAllStorageBy(st, oldOriginX, oldOriginY);
    for (SurveyPoint& p : st.surveyPoints)
      p.labelMtextAnnId = 0;
  }

  // Parse HEADER $EXTMIN/$EXTMAX to set the world origin before entity parsing.
  // This lets appendSegXF subtract a near-zero offset before the double→float cast,
  // preserving sub-unit precision for state-plane coordinates (Civil 3D, etc.).
  {
    size_t hb = 0, he = 0;
    if (FindSectionBounds(pairs, "HEADER", &hb, &he)) {
      double minX = 0, minY = 0, maxX = 0, maxY = 0;
      bool gotMin = false, gotMax = false;
      for (size_t k = hb; k < he; ++k) {
        if (pairs[k].code != 9)
          continue;
        const bool isMin = (pairs[k].value == "$EXTMIN");
        const bool isMax = (pairs[k].value == "$EXTMAX");
        if (!isMin && !isMax)
          continue;
        double vx = 0, vy = 0;
        bool hx = false, hy = false;
        for (size_t kk = k + 1; kk < he && pairs[kk].code != 9; ++kk) {
          if (pairs[kk].code == 10) { ParseDouble(pairs[kk].value, &vx); hx = true; }
          else if (pairs[kk].code == 20) { ParseDouble(pairs[kk].value, &vy); hy = true; }
        }
        if (hx && hy) {
          if (isMin) { minX = vx; minY = vy; gotMin = true; }
          else       { maxX = vx; maxY = vy; gotMax = true; }
        }
      }
      // Drawing unit (REQ-022): adopt $INSUNITS as a relabel only — never scale
      // coordinates, so the import stays 1:1 (REQ-002). Only the offered codes are
      // accepted; anything else leaves the current unit unchanged.
      for (size_t k = hb; k < he; ++k) {
        if (pairs[k].code != 9 || pairs[k].value != "$INSUNITS")
          continue;
        for (size_t kk = k + 1; kk < he && pairs[kk].code != 9; ++kk) {
          if (pairs[kk].code == 70) {
            int u = 0;
            if (ParseIntFlexible(pairs[kk].value, &u) && (u == 0 || u == 2 || u == 6))
              st.drawingInsUnits = u;
            break;
          }
        }
        break;
      }

      // Only trust HEADER extents if min and max are in the same coordinate system.
      // Civil 3D sometimes writes $EXTMIN in local/paper space and $EXTMAX in state-plane
      // (or vice-versa), producing a span of millions of feet. A span > 1e6 ft (~190 miles)
      // signals a mixed-system HEADER — ignore it and fall back to entity scanning below.
      const double spanX = gotMin && gotMax ? std::fabs(maxX - minX) : 0.0;
      const double spanY = gotMin && gotMax ? std::fabs(maxY - minY) : 0.0;
      const bool headerSane = std::max(spanX, spanY) < 1e6;
      // Only rebase when the coordinates are actually large enough to need it (#94).
      //
      // `.gs` load has always been gated this way — `MaybeRebaseLargeCoordinates` fires only above
      // `kLargeCoordinateRebaseThreshold`. DXF import was not, and rebased on EVERY import, so a
      // drawing spanning 150 units had its origin moved to the extents centre and every stored
      // float re-rounded — buying no precision whatsoever, since local storage only helps when the
      // world coordinates are large. That re-rounding is what walked the drawing ~2e-6 further on
      // every export/import cycle, monotonically, with no convergence (#94).
      //
      // The magnitude test reads the HEADER extents rather than the geometry, because at this point
      // no geometry has been parsed — the origin has to be established before `appendSegXF` casts
      // anything to float, which is the entire reason this block runs first.
      const double headerMag =
          std::max({std::fabs(minX), std::fabs(maxX), std::fabs(minY), std::fabs(maxY)});
      if (headerSane && headerMag >= CadCoord::kLargeCoordinateRebaseThreshold) {
        // ApplyDocumentOriginRebase (rather than a blind assignment) so any survey points kept from the
        // session are shifted into the new origin's local frame instead of being left in the old one.
        if (gotMin && gotMax && (spanX > 0.0 || spanY > 0.0)) {
          CadCoord::ApplyDocumentOriginRebase(st, 0.5 * (minX + maxX), 0.5 * (minY + maxY), &log);
        } else if (gotMin) {
          CadCoord::ApplyDocumentOriginRebase(st, minX, minY, &log);
        }
      }
    }
  }

  // If HEADER gave no usable origin, scan entity coordinates for the first large-magnitude
  // value to use as the world origin (avoids float precision loss for state-plane coordinates).
  if (st.worldDocumentOriginX == 0.0 && st.worldDocumentOriginY == 0.0) {
    double candX = 0.0, candY = 0.0;
    auto prescanEntities = [&](size_t a, size_t b) {
      bool gotX = false, gotY = false;
      for (size_t k = a; k < b; ++k) {
        if (!gotX && pairs[k].code == 10) {
          double v = 0;
          if (ParseDouble(pairs[k].value, &v) && std::fabs(v) > 10000.0) {
            candX = v; gotX = true;
          }
        }
        if (!gotY && pairs[k].code == 20) {
          double v = 0;
          if (ParseDouble(pairs[k].value, &v) && std::fabs(v) > 10000.0) {
            candY = v; gotY = true;
          }
        }
        if (gotX && gotY) break;
      }
    };
    if (hasEntitiesSec) prescanEntities(eb, ee);
    else if (hasModelSpace) prescanEntities(mb, me);
    // Rebase (not assign) so kept survey points shift into the new local frame.
    if (candX != 0.0 || candY != 0.0)
      CadCoord::ApplyDocumentOriginRebase(st, candX, candY, &log);
  }

  double coordMagMax = 0;
  int skippedPaper = 0;
  int skippedViewport = 0;
  int skipped = 0;
  // Curves whose group 210 is a zero-length vector (REQ-312). A malformed file: the extrusion names
  // no plane, so there is no frame to read the entity's coordinates in. Refused and counted rather
  // than quietly taken as flat, which would place the curve somewhere it is not (REQ-201).
  int degenerateExtrusions = 0;
  std::unordered_map<std::string, int> skipHist;
  std::vector<SurveyPoint> embeddedPoints;  // GOSURVEY XDATA points, local coords (rel. parse-time origin)
  const Affine2D xfRoot{};
  if (hasEntitiesSec)
    ParseEntityRegion(pairs, eb, ee, st, layerRgb, &blockDefs, xfRoot, 0, &coordMagMax, &skippedPaper,
                      &skippedViewport, &skipped, &skipHist, &embeddedPoints, &textStyles,
                      &degenerateExtrusions);

  // Polylines count as geometry. Before they had a sink of their own, a file holding nothing but
  // polylines still filled userLinesFlat; now it does not, and without this a polyline-only DXF
  // would be judged empty and read a SECOND time out of the *MODEL_SPACE block — duplicating it.
  // Arcs and ellipses count as geometry too, and must be listed here for the same reason polylines
  // were: until #63 they always landed in `userLinesFlat` as tessellation, so this test could not
  // see them and did not need to. Now that they have stores of their own, a DXF holding nothing but
  // arcs would otherwise be judged empty and read a SECOND time from the *MODEL_SPACE block —
  // duplicating every entity in it.
  const bool noGeom = st.userLinesFlat.empty() && st.userCirclesCxCyZR.empty() &&
                      st.userPolylineOffsets.empty() && st.userArcs.empty() && st.userEllipses.empty();
  if ((!hasEntitiesSec || noGeom) && hasModelSpace) {
    if (!hasEntitiesSec)
      log.push_back("DXF import — ENTITIES section missing; reading geometry from *MODEL_SPACE block.");
    else if (noGeom)
      log.push_back("DXF import — ENTITIES empty after model-space filter; reading geometry from *MODEL_SPACE block.");
    ParseEntityRegion(pairs, mb, me, st, layerRgb, &blockDefs, xfRoot, 0, &coordMagMax, &skippedPaper,
                      &skippedViewport, &skipped, &skipHist, &embeddedPoints, &textStyles,
                      &degenerateExtrusions);
  }

  const size_t nLines = st.userLinesFlat.size() / 6;
  const size_t nCirc = st.userCirclesCxCyZR.size() / 4;
  const size_t nPoly = st.userPolylineOffsets.empty() ? 0 : st.userPolylineOffsets.size() - 1;
  std::ostringstream os;
  os << "DXF import — " << nLines << " line segment(s), " << nCirc << " circle(s), " << nPoly
     << " polyline(s).";
  log.push_back(os.str());
  if (skippedPaper > 0)
    log.push_back("DXF import — skipped " + std::to_string(skippedPaper) +
                  " paper-space-only ENTITIES (group 67); layouts/title blocks not imported.");
  if (skippedViewport > 0)
    log.push_back("DXF import — skipped " + std::to_string(skippedViewport) + " VIEWPORT record(s).");
  if (degenerateExtrusions > 0)
    log.push_back("DXF import — refused " + std::to_string(degenerateExtrusions) +
                  " ARC/CIRCLE record(s) whose group 210 extrusion is a zero-length vector.");
  if (skipped > 0) {
    log.push_back("DXF import — skipped " + std::to_string(skipped) + " unsupported ENTITIES record(s).");
    int printed = 0;
    for (const auto& kv : skipHist) {
      if (printed >= 8)
        break;
      log.push_back("  skipped \"" + kv.first + "\" × " + std::to_string(kv.second));
      ++printed;
    }
  }
  // Merge the DXF's embedded survey points with the session's existing ones. Embedded coords are local
  // (relative to the current/parse origin). Non-colliding IDs are added now so the rebase below shifts and
  // labels them with the rest; colliding IDs are converted to world coords and deferred to the conflict
  // modal (overwrite vs. offset). With no pre-existing points there are no conflicts — all just import.
  std::vector<SurveyPoint> embeddedConflictsWorld;
  {
    auto idInUse = [&](int id) {
      for (const SurveyPoint& p : st.surveyPoints)
        if (p.id == id)
          return true;
      return false;
    };
    for (SurveyPoint& sp : embeddedPoints) {
      if (hadExistingPoints && idInUse(sp.id)) {
        SurveyPoint w = sp;
        w.easting = static_cast<float>(static_cast<double>(sp.easting) + st.worldDocumentOriginX);
        w.northing = static_cast<float>(static_cast<double>(sp.northing) + st.worldDocumentOriginY);
        embeddedConflictsWorld.push_back(w);
      } else {
        sp.labelMtextAnnId = 0;
        st.surveyPoints.push_back(sp);
      }
    }
  }

  // #94: was an UNCONDITIONAL RebaseDrawingToLocalOrigin. `MaybeRebaseLargeCoordinates`
  // is the same call behind `.gs` load's two guards — it no-ops when the origin is already set (the
  // large-coordinate case, where the header block above just established it) and no-ops below
  // `kLargeCoordinateRebaseThreshold` (the small-drawing case, which is #94). So the composition is:
  //   small drawing            -> header skipped, this skips  -> origin stays (0,0), nothing re-rounds
  //   large, sane header       -> header fires, this no-ops   -> exactly one rebase
  //   large, missing/bad header-> header skipped, this fires  -> exactly one rebase
  CadCoord::MaybeRebaseLargeCoordinates(st, &log);
  {
    double rawMnX = 0., rawMxX = 0., rawMnY = 0., rawMxY = 0.;
    if (ComputeWorldExtents(st, &rawMnX, &rawMxX, &rawMnY, &rawMxY)) {
      char buf[256];
      std::snprintf(buf, sizeof(buf),
                    "DXF import — full local bbox: X %.6g..%.6g (span %.6g), Y %.6g..%.6g (span %.6g).", rawMnX,
                    rawMxX, rawMxX - rawMnX, rawMnY, rawMxY, rawMxY - rawMnY);
      log.push_back(buf);
    }
    double mnX = 0., mxX = 0., mnY = 0., mxY = 0.;
    int skipped = 0;
    if (ComputeRobustWorldExtents(st, &mnX, &mxX, &mnY, &mxY, &skipped)) {
      char buf[256];
      std::snprintf(
          buf, sizeof(buf),
          "DXF import — robust local bbox: X %.6g..%.6g (span %.6g), Y %.6g..%.6g (span %.6g), skipped=%d outlier(s).",
          mnX, mxX, mxX - mnX, mnY, mxY, mxY - mnY, skipped);
      log.push_back(buf);
    }
  }
  const int fbW = std::max(st.viewportLastFbW, 1);
  const int fbH = std::max(st.viewportLastFbH, 1);
  const float aspect = static_cast<float>(fbW) / static_cast<float>(fbH);
  if (CadCoord::FitViewportToDrawing(st, aspect, fbW, fbH)) {
    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "DXF import — zoom extents applied (pan=%.6g,%.6g zoom=%.6g fb=%dx%d).", st.viewportPanX,
                  st.viewportPanY, static_cast<double>(st.viewportZoom), fbW, fbH);
    log.push_back(buf);
  } else
    st.pendingZoomExtents = true;
  // REQ-023: regenerate the linked label for each reconstructed survey point
  // (their exported label MTEXTs were skipped above to avoid duplicates).
  for (size_t spi = 0; spi < st.surveyPoints.size(); ++spi)
    EnsureSurveyPointLabelMtext(st, spi, nullptr);

  // Embedded points whose IDs collide with existing session points were deferred; ask the user how to
  // reconcile them (overwrite the existing rows, or offset the imported IDs).
  if (!embeddedConflictsWorld.empty()) {
    int maxId = 0;
    for (const SurveyPoint& p : st.surveyPoints)
      maxId = std::max(maxId, p.id);
    st.pendingDxfConflictPoints = std::move(embeddedConflictsWorld);
    st.dxfPointConflictOffset = std::max(maxId, 1);  // default offset clears the existing range
    st.dxfPointConflictModalOpen = true;
    st.dxfPointConflictModalOpenRequested = true;
    log.push_back("DXF import — " + std::to_string(st.pendingDxfConflictPoints.size()) +
                  " imported survey point ID(s) conflict with existing points; choose overwrite or offset.");
  }

  BumpCadGpuCache(st);
  return true;
}

bool ExportDxfFile_Impl(const AppCommandState& st, const char* pathUtf8, std::vector<std::string>& log) {
  if (!pathUtf8 || pathUtf8[0] == '\0') {
    log.push_back("DXF export — no path.");
    return false;
  }
  std::unordered_map<std::string, uint32_t> layerRgbHint;
  BuildExportLayerRgbHint(st, &layerRgbHint);

  std::unordered_set<std::string> layerNames;
  auto addLayerName = [&](const std::string& raw) {
    layerNames.insert(raw.empty() ? std::string("0") : raw);
  };
  for (size_t i = 0; i < st.userLineAttrs.size(); ++i)
    addLayerName(st.userLineAttrs[i].layer);
  for (size_t i = 0; i < st.userCircleAttrs.size(); ++i)
    addLayerName(st.userCircleAttrs[i].layer);
  for (size_t i = 0; i < st.cadAnnotationAttrs.size(); ++i)
    addLayerName(st.cadAnnotationAttrs[i].layer.empty() ? std::string("0") : st.cadAnnotationAttrs[i].layer);
  for (size_t i = 0; i < st.cadTableAttrs.size(); ++i)
    addLayerName(st.cadTableAttrs[i].layer);
  // Polylines (#72) and filled regions (the same omission, unreported) — the two entity branches
  // added to the writer without being added here. Every layer this sweep misses is a layer some
  // entity's group 8 can name with no LAYER table row behind it: an invalid file, written silently.
  // `drawingLayerTable` below masks it for layers created in this session, which is why it survived
  // this long, but not for one that arrived on an imported entity.
  for (size_t i = 0; i < st.userPolylineAttrs.size(); ++i)
    addLayerName(st.userPolylineAttrs[i].layer);
  for (size_t i = 0; i < st.cadFilledRegionAttrs.size(); ++i)
    addLayerName(st.cadFilledRegionAttrs[i].layer);
  // Arcs and ellipses (#63) — the third instance of the same omission. They reached this sweep only
  // now because until this change they had no export branch to name a layer from at all.
  for (size_t i = 0; i < st.userArcAttrs.size(); ++i)
    addLayerName(st.userArcAttrs[i].layer);
  for (size_t i = 0; i < st.userEllAttrs.size(); ++i)
    addLayerName(st.userEllAttrs[i].layer);
  for (const SurveyPoint& p : st.surveyPoints)
    addLayerName(p.layer);
  for (const CadLayerRow& lr : st.drawingLayerTable) {
    if (!lr.name.empty())
      addLayerName(lr.name);
  }
  layerNames.insert("0");

  const size_t nSeg = st.userLinesFlat.size() / 6;
  const size_t nCirc = st.userCirclesCxCyZR.size() / 4;
  const size_t nLayerRows = layerNames.size();
  // Symbol handles (hex, unique): fixed small IDs for tables/rows where handle < layer base (0x10),
  // then layer rows 0x10.., then VPORT, VIEW, UCS, APPID, DIMSTYLE, BLOCK_RECORD, BLOCK pairs.
  const uint64_t symAfterLayers = 0x10ull + static_cast<uint64_t>(nLayerRows);
  const uint64_t symVportTab = symAfterLayers;
  const uint64_t symVportActive = symAfterLayers + 1;
  const uint64_t symViewTab = symAfterLayers + 2;
  const uint64_t symUcsTab = symAfterLayers + 3;
  const uint64_t symAppidTab = symAfterLayers + 4;
  const uint64_t symAppidAcad = symAfterLayers + 5;
  const uint64_t symDimstyleTab = symAfterLayers + 6;
  const uint64_t symDimstyleStd = symAfterLayers + 7;
  const uint64_t symBlockRecordTab = symAfterLayers + 8;
  const uint64_t symBrModel = symAfterLayers + 9;
  const uint64_t symBrPaper = symAfterLayers + 10;
  const uint64_t symBlkModel0 = symAfterLayers + 11;
  const uint64_t symBlkModel1 = symAfterLayers + 12;
  const uint64_t symBlkPaper0 = symAfterLayers + 13;
  const uint64_t symBlkPaper1 = symAfterLayers + 14;
  // Registered APPID for survey-point XDATA (REQ-023, ADR-005). Appended at the end
  // of the symbol-handle range so no existing handle shifts.
  const uint64_t symGoSurveyAppid = symAfterLayers + 15;
  const uint64_t lastSymHandle = symGoSurveyAppid;

  // Every store the writer below emits an entity from must be counted here, or its entities are
  // handed handles the OBJECTS block also owns (#71). Polylines and filled regions were missing:
  // the LWPOLYLINE branch arrived with REQ-053 and the HATCH branch earlier, each updating the
  // writer and neither updating this sum. Counting the STORES rather than the entities the writer
  // will keep is deliberate — the writer skips degenerate polylines (< 2 vertices) and degenerate
  // regions (< 3 vertices), so this can over-count, which leaves an unused gap ahead of OBJECTS.
  // DXF permits a gap in the handle sequence; it does not permit a duplicate.
  //
  // `userArcs` and `userEllipses` are counted here as of TASK-114 (#63), which gave them the export
  // branch they had never had. Before that they consumed no handles because nothing emitted them —
  // this sum and the writer must move together, which is what the note left here by TASK-107 asked.
  const size_t nPoly =
      st.userPolylineOffsets.size() > 0 ? st.userPolylineOffsets.size() - 1 : 0;
  uint64_t entityHandleCount = static_cast<uint64_t>(nSeg) + static_cast<uint64_t>(nCirc) +
                               static_cast<uint64_t>(st.surveyPoints.size()) +
                               static_cast<uint64_t>(nPoly) +
                               static_cast<uint64_t>(st.cadFilledRegions.size()) +
                               static_cast<uint64_t>(st.userArcs.size()) +
                               static_cast<uint64_t>(st.userEllipses.size());
  for (size_t ai = 0; ai < st.cadAnnotations.size(); ++ai) {
    const CadAnnotation& an = st.cadAnnotations[ai];
    if (an.kind == CadAnnotation::Kind::Text)
      ++entityHandleCount;
    else if (an.kind == CadAnnotation::Kind::DimAligned || an.kind == CadAnnotation::Kind::DimLinear) {
      float sx1 = 0.f, sy1 = 0.f, sx2 = 0.f, sy2 = 0.f, tx = 0.f, ty = 0.f, nx = 0.f, ny = 0.f, meas = 0.f;
      if (!CadDimAnyGeometry(an, &sx1, &sy1, &sx2, &sy2, &tx, &ty, &nx, &ny, &meas))
        continue;
      entityHandleCount += 4; // three LINE + one TEXT
    } else
      ++entityHandleCount;
  }
  const uint64_t entityHandleStart = std::max(0x1000ull, lastSymHandle + 1ull);
  // OBJECTS: root NOD + ACAD_GROUP + ACAD_PLOTSTYLENAME (AcDbDictionaryWithDefault + Normal -> placeholder).
  // LAYER rows require group 390 -> plot-style placeholder (AutoCAD / Civil 3D DXF2000+).
  const uint64_t objDictRoot = entityHandleStart + entityHandleCount;
  const uint64_t objDictAcadGroup = objDictRoot + 1ull;
  const uint64_t objPlotDictWdflt = objDictAcadGroup + 1ull;
  const uint64_t objPlotPlaceholder = objPlotDictWdflt + 1ull;
  const uint64_t handSeedVal = objPlotPlaceholder + 1ull;
  char handSeedBuf[24];
  std::snprintf(handSeedBuf, sizeof(handSeedBuf), "%llX", static_cast<unsigned long long>(handSeedVal));

  char hVportTab[24], hVportAct[24], hViewTab[24], hUcsTab[24], hAppidTab[24], hAppidAcad[24], hDimTab[24],
      hDimStd[24];
  char hBrTab[24], hBrModel[24], hBrPaper[24], hBlkM0[24], hBlkM1[24], hBlkP0[24], hBlkP1[24];
  char hObjRoot[24], hObjAcadGroup[24], hObjPlotDict[24], hObjPlotPh[24];
  std::snprintf(hVportTab, sizeof(hVportTab), "%llX", static_cast<unsigned long long>(symVportTab));
  std::snprintf(hVportAct, sizeof(hVportAct), "%llX", static_cast<unsigned long long>(symVportActive));
  std::snprintf(hViewTab, sizeof(hViewTab), "%llX", static_cast<unsigned long long>(symViewTab));
  std::snprintf(hUcsTab, sizeof(hUcsTab), "%llX", static_cast<unsigned long long>(symUcsTab));
  std::snprintf(hAppidTab, sizeof(hAppidTab), "%llX", static_cast<unsigned long long>(symAppidTab));
  std::snprintf(hAppidAcad, sizeof(hAppidAcad), "%llX", static_cast<unsigned long long>(symAppidAcad));
  char hGoSurveyAppid[24];
  std::snprintf(hGoSurveyAppid, sizeof(hGoSurveyAppid), "%llX", static_cast<unsigned long long>(symGoSurveyAppid));
  std::snprintf(hDimTab, sizeof(hDimTab), "%llX", static_cast<unsigned long long>(symDimstyleTab));
  std::snprintf(hDimStd, sizeof(hDimStd), "%llX", static_cast<unsigned long long>(symDimstyleStd));
  std::snprintf(hBrTab, sizeof(hBrTab), "%llX", static_cast<unsigned long long>(symBlockRecordTab));
  std::snprintf(hBrModel, sizeof(hBrModel), "%llX", static_cast<unsigned long long>(symBrModel));
  std::snprintf(hBrPaper, sizeof(hBrPaper), "%llX", static_cast<unsigned long long>(symBrPaper));
  std::snprintf(hBlkM0, sizeof(hBlkM0), "%llX", static_cast<unsigned long long>(symBlkModel0));
  std::snprintf(hBlkM1, sizeof(hBlkM1), "%llX", static_cast<unsigned long long>(symBlkModel1));
  std::snprintf(hBlkP0, sizeof(hBlkP0), "%llX", static_cast<unsigned long long>(symBlkPaper0));
  std::snprintf(hBlkP1, sizeof(hBlkP1), "%llX", static_cast<unsigned long long>(symBlkPaper1));
  std::snprintf(hObjRoot, sizeof(hObjRoot), "%llX", static_cast<unsigned long long>(objDictRoot));
  std::snprintf(hObjAcadGroup, sizeof(hObjAcadGroup), "%llX", static_cast<unsigned long long>(objDictAcadGroup));
  std::snprintf(hObjPlotDict, sizeof(hObjPlotDict), "%llX", static_cast<unsigned long long>(objPlotDictWdflt));
  std::snprintf(hObjPlotPh, sizeof(hObjPlotPh), "%llX", static_cast<unsigned long long>(objPlotPlaceholder));

  double extMnX = 0., extMxX = 0., extMnY = 0., extMxY = 0., extMnZ = 0., extMxZ = 0.;
  bool extAny = false;
  bool extZAny = false;
  auto accExt = [&](double x, double y) {
    if (!extAny) {
      extMnX = extMxX = x;
      extMnY = extMxY = y;
      extAny = true;
    } else {
      extMnX = std::min(extMnX, x);
      extMxX = std::max(extMxX, x);
      extMnY = std::min(extMnY, y);
      extMxY = std::max(extMxY, y);
    }
  };
  auto accExtZ = [&](double z) {
    if (!extZAny) {
      extMnZ = extMxZ = z;
      extZAny = true;
    } else {
      extMnZ = std::min(extMnZ, z);
      extMxZ = std::max(extMxZ, z);
    }
  };
  for (size_t i = 0; i < nSeg; ++i) {
    accExt(static_cast<double>(st.userLinesFlat[i * 6 + 0]), static_cast<double>(st.userLinesFlat[i * 6 + 1]));
    accExt(static_cast<double>(st.userLinesFlat[i * 6 + 3]), static_cast<double>(st.userLinesFlat[i * 6 + 4]));
    accExtZ(static_cast<double>(st.userLinesFlat[i * 6 + 2]));
    accExtZ(static_cast<double>(st.userLinesFlat[i * 6 + 5]));
  }
  for (size_t ci = 0; ci < nCirc; ++ci) {
    const double cx = static_cast<double>(st.userCirclesCxCyZR[ci * 4]);
    const double cy = static_cast<double>(st.userCirclesCxCyZR[ci * 4 + 1]);
    const double rr = std::fabs(static_cast<double>(st.userCirclesCxCyZR[ci * 4 + 3]));
    accExt(cx - rr, cy - rr);
    accExt(cx + rr, cy + rr);
  }
  for (const CadAnnotation& an : st.cadAnnotations) {
    float amnX = 0.f, amnY = 0.f, amxX = 0.f, amxY = 0.f;
    CadAnnotationRoughBounds(an, st.modelUnitsPerPlottedInch, &amnX, &amnY, &amxX, &amxY);
    accExt(static_cast<double>(amnX), static_cast<double>(amnY));
    accExt(static_cast<double>(amxX), static_cast<double>(amxY));
  }
  for (const CadTable& t : st.cadTables) {
    float tmnX = 0.f, tmnY = 0.f, tmxX = 0.f, tmxY = 0.f;
    CadTableWorldAabb(t, &tmnX, &tmnY, &tmxX, &tmxY);
    accExt(static_cast<double>(tmnX), static_cast<double>(tmnY));
    accExt(static_cast<double>(tmxX), static_cast<double>(tmxY));
    accExtZ(static_cast<double>(t.insZ));
  }
  for (const SurveyPoint& p : st.surveyPoints) {
    accExt(static_cast<double>(p.easting), static_cast<double>(p.northing));
    accExtZ(static_cast<double>(p.elevation));
  }
  // Polylines are geometry too, and this sweep did not know they existed — REQ-053 gave the exporter
  // a LWPOLYLINE branch but not an extents branch. The omission travels, because the IMPORTER sets
  // the document origin from $EXTMIN/$EXTMAX: a drawing whose polylines lie outside its lines came
  // back centred on the wrong point, and re-exported to different bytes (issue #64).
  {
    const size_t nPolyVert =
        st.userPolylineOffsets.empty() ? 0u : static_cast<size_t>(st.userPolylineOffsets.back());
    for (size_t vi = 0; vi < nPolyVert && vi * 3 + 2 < st.userPolylineVerts.size(); ++vi) {
      accExt(static_cast<double>(st.userPolylineVerts[vi * 3]),
             static_cast<double>(st.userPolylineVerts[vi * 3 + 1]));
      accExtZ(static_cast<double>(st.userPolylineVerts[vi * 3 + 2]));
    }
  }
  // Arcs and ellipses (#63), for the same reason and with the same consequence: an entity type the
  // sweep does not know about is one the document origin is not derived from, so a drawing whose
  // arcs lie outside its linework re-imports centred on the wrong point and never settles.
  //
  // THE SAMPLING BELOW MUST MATCH `ComputeWorldExtents` IN CadCommands.cpp EXACTLY — same segment
  // counts, same angles, same order of operations. That is not a stylistic preference, and a
  // "tighter" or "more conservative" box here is not a free choice:
  //
  //   DXF import ends by calling `RebaseDrawingToLocalOrigin`, which re-centres the document origin
  //   on `ComputeWorldExtents`. If this sweep disagrees with that one, the origin the header asked
  //   for is not the origin the import settles on, so `ShiftAllStorageBy` moves every stored
  //   coordinate — through FLOAT — and the re-export lands ~1e-6 off in every Y. Export → import →
  //   export then never settles, which is precisely REQ-204's stability invariant.
  //
  //   Agreement makes the rebase delta zero, so it takes its own `< 1e-9` early-out and no lossy
  //   shift happens at all. That, not the accuracy of the box, is what the round trip needs. A
  //   whole-circle box for arcs was tried first and failed exactly this way.
  //
  // Swept from the arc THIS FILE STATES, not the one in memory — `DxfArcToWrite` is the single
  // answer to which that is (issue #111). The two differ whenever a stored angle survives the trip
  // to six-decimal degrees and back as a neighbouring float, and a header swept from the in-memory
  // arc then describes a drawing the entity records below do not contain. It also STRENGTHENS the
  // agreement the note above demands: the document a reader builds holds exactly these angles, so
  // its `ComputeWorldExtents` matches this sweep rather than merely coming close.
  for (const CadArc& a : st.userArcs) {
    const DxfArcAsWritten aw = DxfArcToWrite(a);
    const double dr = std::fabs(static_cast<double>(a.r));
    if (dr <= 1e-12)
      continue;
    const int n =
        std::max(8, static_cast<int>(std::fabs(static_cast<double>(aw.sweepRad)) / (3.14159265 / 16.0)) + 1);
    // A tilted arc (REQ-312) is walked in its own plane here for the same reason the angles come
    // from `aw`: the box has to be the one a READER computes from this file, and a reader holds the
    // arc's plane. Walking it in the XY projection instead gives a box that is too SMALL, and the
    // agreement the note above depends on is then lost in the direction that crops geometry.
    const bool arcFlat = IsFlatNormal(a.nx, a.ny, a.nz);

    // The centre is the one THIS FILE STATES too (issue #188), for the same reason `aw` gives the
    // angles: a tilted arc's group 10/20/30 is an OCS coordinate `std::to_string` rounds to six
    // decimals, and at state-plane magnitude that rounding, projected back through the group-210
    // frame, lands the reader's centre a sub-micron off the in-memory one. Sweeping from the
    // in-memory centre then makes `$EXTMIN/$EXTMAX` describe a drawing the entity records do not
    // contain, the import rebase shifts every coordinate through `float`, and the file never
    // byte-settles. Reconstructing the reader's centre here makes the rebase delta zero — its own
    // `< 1e-9` early-out then fires and nothing shifts. A flat arc's OCS point is its world point
    // unchanged, so this is a no-op for it.
    double dcx = static_cast<double>(a.cx);
    double dcy = static_cast<double>(a.cy);
    double dcz = static_cast<double>(a.z);
    if (!arcFlat) {
      const auto snap6 = [](double v) {
        return std::isfinite(v) ? std::stod(std::to_string(v)) : v;
      };
      ray3d::Vec3 ocs{}, wc{};
      if (DxfWorldToOcs(dcx + st.worldDocumentOriginX, dcy + st.worldDocumentOriginY, dcz,
                        static_cast<double>(a.nx), static_cast<double>(a.ny),
                        static_cast<double>(a.nz), &ocs) &&
          DxfOcsToWorld(snap6(ocs.x), snap6(ocs.y), snap6(ocs.z), static_cast<double>(a.nx),
                        static_cast<double>(a.ny), static_cast<double>(a.nz), &wc)) {
        dcx = wc.x - st.worldDocumentOriginX;
        dcy = wc.y - st.worldDocumentOriginY;
        dcz = wc.z;
      }
    }
    const ucs::Ucs arcPlane =
        arcFlat ? ucs::Ucs{}
                : CurvePlane(dcx, dcy, dcz, static_cast<double>(a.nx), static_cast<double>(a.ny),
                             static_cast<double>(a.nz));
    for (int i = 0; i <= n; ++i) {
      const double u = static_cast<double>(i) / static_cast<double>(n);
      const double t = static_cast<double>(aw.startRad) + static_cast<double>(aw.sweepRad) * u;
      if (arcFlat) {
        accExt(dcx + dr * std::cos(t), dcy + dr * std::sin(t));
        continue;
      }
      const ray3d::Vec3 p = CurvePointAt(arcPlane, dr, t);
      accExt(p.x, p.y);
      accExtZ(p.z);  // a tilted arc spans elevations; its centre's Z is not its extent
    }
    accExtZ(static_cast<double>(a.z));
  }
  for (const CadEllipse& el : st.userEllipses) {
    // Swept from the ellipse THIS FILE STATES — `DxfEllipseToWrite` is the single answer to
    // which that is (issue #113). The in-memory `ratio`/`majV` do not survive six decimals,
    // so a header swept from them describes a drawing the entity records below do not contain.
    const DxfEllipseAsWritten ew = DxfEllipseToWrite(el);
    const double ma = std::hypot(static_cast<double>(ew.majVx), static_cast<double>(ew.majVy));
    if (ma < 1e-12)
      continue;
    constexpr int n = 48;
    constexpr double kTwoPi = 6.283185307179586;
    const double ux = static_cast<double>(ew.majVx) / ma;
    const double uy = static_cast<double>(ew.majVy) / ma;
    const double px = -uy;
    const double py = ux;
    const double mb = ma * static_cast<double>(ew.ratio);
    const double ecx = static_cast<double>(el.cx);
    const double ecy = static_cast<double>(el.cy);
    for (int i = 0; i < n; ++i) {
      const double ang = kTwoPi * static_cast<double>(i) / static_cast<double>(n);
      const double c = std::cos(ang);
      const double s = std::sin(ang);
      accExt(ecx + ux * (ma * c) + px * (mb * s), ecy + uy * (ma * c) + py * (mb * s));
    }
    accExtZ(static_cast<double>(el.z));
  }
  if (!extAny) {
    extMnX = extMxX = extMnY = extMxY = 0.;
  } else {
    const double pad = std::max((extMxX - extMnX), (extMxY - extMnY)) * 0.05 + 1.0;
    extMnX -= pad;
    extMxX += pad;
    extMnY -= pad;
    extMxY += pad;
    // Every entity below is written in WORLD coordinates (the `worldX`/`worldY` helpers add the
    // document origin); the sweep above read the LOCAL store. Shift the result so the header
    // describes the same frame as the body. Without this the file changed whenever the origin
    // moved — and importing a file is precisely what moves it, so an export → import → export
    // cycle never settled (REQ-204's stability invariant). Z needs no shift: the document origin
    // is X/Y-only (ADR-025 D2).
    extMnX += st.worldDocumentOriginX;
    extMxX += st.worldDocumentOriginX;
    extMnY += st.worldDocumentOriginY;
    extMxY += st.worldDocumentOriginY;
  }
  if (!extZAny) {
    extMnZ = extMxZ = 0.;
  } else {
    const double zSpan = extMxZ - extMnZ;
    if (zSpan < 1e-9) {
      // Coplanar Z: keep a single elevation in $EXTMIN/$EXTMAX (no ± padding); matches typical 2D survey.
    } else {
      const double padZ = std::max((extMxZ - extMnZ) * 0.05 + 0.01, 1e-9);
      extMnZ -= padZ;
      extMxZ += padZ;
    }
  }

  // Every number in this file is written by `std::to_string`, which rounds to six decimals. Snap
  // the extents to that grid HERE, before anything is derived from them, so the variables below
  // hold exactly what the file will state.
  //
  // The header view (VPORT groups 12/22/40/41) is derived from the extents, and `* 1.1 + 1.0`
  // AMPLIFIES: two runs whose raw extents differ by less than half of the last written decimal —
  // which is all a DXF round trip can leave behind, since a re-imported coordinate is the
  // six-decimal text read back — round to the SAME $EXTMIN/$EXTMAX and still land on different
  // sixth decimals in the view height. That is issue #98: a first export and a second differed on
  // group 40 alone, by 1e-6, with every other byte identical. Deriving from the snapped extents
  // removes the amplifier — the view this file states is exactly the view a reader derives from
  // the extents the same file states — so the FIRST export is already a fixed point (REQ-204
  // stability invariant), with no idempotence carve-out of the kind REQ-079 needed for `.gs`.
  const auto snapToWritePrecision = [](double v) {
    return std::isfinite(v) ? std::stod(std::to_string(v)) : v;
  };
  extMnX = snapToWritePrecision(extMnX);
  extMxX = snapToWritePrecision(extMxX);
  extMnY = snapToWritePrecision(extMnY);
  extMxY = snapToWritePrecision(extMxY);
  extMnZ = snapToWritePrecision(extMnZ);
  extMxZ = snapToWritePrecision(extMxZ);

  const double vCx = (extMnX + extMxX) * 0.5;
  const double vCy = (extMnY + extMxY) * 0.5;
  const double vW = extMxX - extMnX;
  const double vHspan = extMxY - extMnY;
  const double vViewH = std::max(1.0, std::max(vW, vHspan) * 1.1 + 1.0);
  const double vAsp = (vHspan > 1e-12) ? (vW / vHspan) : 1.0;

  std::ofstream out(std::filesystem::path(pathUtf8), std::ios::binary);
  if (!out) {
    log.push_back("DXF export — could not open file for writing.");
    return false;
  }
  out << std::setprecision(16);

  auto emitPair = [&](int code, const std::string& val) {
    out << code << "\r\n" << val << "\r\n";
  };

  auto dxfEmitTransparency440IfNeeded = [&](float transparency01) {
    int pack = 0;
    if (DxfTransparency440(transparency01, &pack))
      emitPair(440, std::to_string(pack));
  };
  // Same packing, but returning the value instead of emitting it — for record descriptors
  // (DxfEntityEmit.hpp) that carry group 440 as an optional pre-formatted field.
  auto dxfTransparency440Str = [](float transparency01, std::string* out) -> bool {
    int pack = 0;
    if (!DxfTransparency440(transparency01, &pack))
      return false;
    if (out)
      *out = std::to_string(pack);
    return true;
  };
  auto emitTextRecord = [&](const DxfTextRecord& rec) {
    std::vector<DxfOutPair> pairs;
    DxfAppendTextRecord(rec, &pairs);
    for (const DxfOutPair& p : pairs)
      emitPair(p.code, p.value);
  };
  auto emitLwPolylineRecord = [&](const DxfLwPolylineRecord& rec) {
    std::vector<DxfOutPair> pairs;
    DxfAppendLwPolylineRecord(rec, &pairs);
    for (const DxfOutPair& p : pairs)
      emitPair(p.code, p.value);
  };
  auto dxfEntityLineweight370Str = [](const EntityAttributes& at) -> std::string {
    if (at.lineweightMm < 0.f)
      return "-1";
    return std::to_string(CadDxfLineweightEnum370FromMm(at.lineweightMm));
  };
  auto dxfLayerLineweight370Str = [](const CadLayerRow* row) -> std::string {
    if (!row || row->lineweightMm < 0.f)
      return "-3";
    return std::to_string(CadDxfLineweightEnum370FromMm(row->lineweightMm));
  };

  emitPair(0, "SECTION");
  emitPair(2, "HEADER");
  emitPair(9, "$ACADVER");
  emitPair(1, "AC1032");
  emitPair(9, "$ACADMAINTVER");
  emitPair(90, "378");
  emitPair(9, "$DWGCODEPAGE");
  emitPair(3, "ANSI_1252");
  emitPair(9, "$LASTSAVEDBY");
  emitPair(1, "GoSurvey");
  emitPair(9, "$REQUIREDVERSIONS");
  emitPair(160, "0");
  emitPair(9, "$INSBASE");
  emitPair(10, "0.0");
  emitPair(20, "0.0");
  emitPair(30, "0.0");
  // Drawing unit (REQ-022): AutoCAD INSUNITS relabel. Metadata only — coordinates
  // above/below are written unscaled, so this never changes geometry.
  emitPair(9, "$INSUNITS");
  emitPair(70, std::to_string(st.drawingInsUnits));
  emitPair(9, "$EXTMIN");
  emitPair(10, std::to_string(extMnX));
  emitPair(20, std::to_string(extMnY));
  emitPair(30, std::to_string(extMnZ));
  emitPair(9, "$EXTMAX");
  emitPair(10, std::to_string(extMxX));
  emitPair(20, std::to_string(extMxY));
  emitPair(30, std::to_string(extMxZ));
  emitPair(9, "$LIMMIN");
  emitPair(10, "0.0");
  emitPair(20, "0.0");
  emitPair(9, "$LIMMAX");
  emitPair(10, "4000.0");
  emitPair(20, "4000.0");
  emitPair(9, "$ORTHOMODE");
  emitPair(70, "0");
  emitPair(9, "$REGENMODE");
  emitPair(70, "1");
  emitPair(9, "$FILLMODE");
  emitPair(70, "1");
  emitPair(9, "$QTEXTMODE");
  emitPair(70, "0");
  emitPair(9, "$MIRRTEXT");
  emitPair(70, "0");
  if (!st.surveyPoints.empty()) {
    // Match PTYPE "X": point display as X-cross (PDMODE 3). PDSIZE 0 = 5% of viewport height.
    emitPair(9, "$PDMODE");
    emitPair(70, "3");
    emitPair(9, "$PDSIZE");
    emitPair(40, "0.0");
  }
  emitPair(9, "$LTSCALE");
  emitPair(40, "1.0");
  emitPair(9, "$ATTMODE");
  emitPair(70, "1");
  emitPair(9, "$TEXTSIZE");
  emitPair(40, "0.1");
  emitPair(9, "$TRACEWID");
  emitPair(40, "0.05");
  emitPair(9, "$TEXTSTYLE");
  emitPair(7, "Standard");
  emitPair(9, "$CLAYER");
  emitPair(8, "0");
  emitPair(9, "$CELTYPE");
  emitPair(6, "ByLayer");
  emitPair(9, "$CECOLOR");
  emitPair(62, "256");
  emitPair(9, "$PSTYLEMODE");
  emitPair(290, "1");
  emitPair(9, "$CELTSCALE");
  emitPair(40, "1.0");
  emitPair(9, "$DISPSILH");
  emitPair(70, "1");
  emitPair(9, "$DIMSCALE");
  emitPair(40, "1.0");
  emitPair(9, "$DIMASZ");
  emitPair(40, "0.05");
  emitPair(9, "$DIMEXO");
  emitPair(40, "0.0");
  emitPair(9, "$DIMDLI");
  emitPair(40, "0.2");
  emitPair(9, "$DIMRND");
  emitPair(40, "0.0");
  emitPair(9, "$DIMDLE");
  emitPair(40, "0.0");
  emitPair(9, "$DIMEXE");
  emitPair(40, "0.0");
  emitPair(9, "$DIMTP");
  emitPair(40, "0.0");
  emitPair(9, "$DIMTM");
  emitPair(40, "0.0");
  emitPair(9, "$DIMTXT");
  emitPair(40, "0.1");
  emitPair(9, "$DIMCEN");
  emitPair(40, "0.09");
  emitPair(9, "$DIMTSZ");
  emitPair(40, "0.0");
  emitPair(9, "$DIMTOL");
  emitPair(70, "0");
  emitPair(9, "$DIMLIM");
  emitPair(70, "0");
  emitPair(9, "$DIMTIH");
  emitPair(70, "0");
  emitPair(9, "$DIMTOH");
  emitPair(70, "0");
  emitPair(9, "$DIMSE1");
  emitPair(70, "0");
  emitPair(9, "$DIMSE2");
  emitPair(70, "0");
  emitPair(9, "$DIMTAD");
  emitPair(70, "1");
  emitPair(9, "$DIMZIN");
  emitPair(70, "0");
  emitPair(9, "$DIMBLK");
  emitPair(1, "");
  emitPair(9, "$DIMASO");
  emitPair(70, "1");
  emitPair(9, "$DIMSHO");
  emitPair(70, "1");
  emitPair(9, "$DIMPOST");
  emitPair(1, "");
  emitPair(9, "$DIMAPOST");
  emitPair(1, "");
  emitPair(9, "$DIMALT");
  emitPair(70, "0");
  emitPair(9, "$DIMALTD");
  emitPair(70, "2");
  emitPair(9, "$DIMALTF");
  emitPair(40, "25.4");
  emitPair(9, "$DIMLFAC");
  emitPair(40, "1.0");
  emitPair(9, "$DIMTOFL");
  emitPair(70, "0");
  emitPair(9, "$DIMTVP");
  emitPair(40, "0.0");
  emitPair(9, "$DIMTIX");
  emitPair(70, "0");
  emitPair(9, "$DIMSOXD");
  emitPair(70, "0");
  emitPair(9, "$DIMSAH");
  emitPair(70, "0");
  emitPair(9, "$DIMBLK1");
  emitPair(1, "");
  emitPair(9, "$DIMBLK2");
  emitPair(1, "");
  emitPair(9, "$DIMSTYLE");
  emitPair(2, "Standard");
  emitPair(9, "$DIMCLRD");
  emitPair(70, "256");
  emitPair(9, "$DIMCLRE");
  emitPair(70, "256");
  emitPair(9, "$DIMCLRT");
  emitPair(70, "256");
  emitPair(9, "$DIMTFAC");
  emitPair(40, "1.0");
  emitPair(9, "$DIMGAP");
  emitPair(40, "0.09");
  emitPair(9, "$DIMJUST");
  emitPair(70, "0");
  emitPair(9, "$DIMSD1");
  emitPair(70, "0");
  emitPair(9, "$DIMSD2");
  emitPair(70, "0");
  emitPair(9, "$DIMTOLJ");
  emitPair(70, "1");
  emitPair(9, "$DIMTZIN");
  emitPair(70, "0");
  emitPair(9, "$DIMALTZ");
  emitPair(70, "0");
  emitPair(9, "$DIMALTTZ");
  emitPair(70, "0");
  emitPair(9, "$DIMUPT");
  emitPair(70, "0");
  emitPair(9, "$DIMDEC");
  emitPair(70, "4");
  emitPair(9, "$DIMTDEC");
  emitPair(70, "4");
  emitPair(9, "$DIMALTU");
  emitPair(70, "2");
  emitPair(9, "$DIMALTTD");
  emitPair(70, "2");
  emitPair(9, "$DIMTXSTY");
  emitPair(7, "Standard");
  emitPair(9, "$DIMAUNIT");
  emitPair(70, "0");
  emitPair(9, "$DIMADEC");
  emitPair(70, "0");
  emitPair(9, "$DIMALTRND");
  emitPair(40, "0.0");
  emitPair(9, "$DIMAZIN");
  emitPair(70, "0");
  emitPair(9, "$DIMDSEP");
  emitPair(70, "46");
  emitPair(9, "$DIMATFIT");
  emitPair(70, "3");
  emitPair(9, "$DIMFRAC");
  emitPair(70, "0");
  emitPair(9, "$DIMLDRBLK");
  emitPair(1, "");
  emitPair(9, "$DIMLUNIT");
  emitPair(70, "2");
  emitPair(9, "$DIMLWD");
  emitPair(70, "-2");
  emitPair(9, "$DIMLWE");
  emitPair(70, "-2");
  emitPair(9, "$DIMTMOVE");
  emitPair(70, "0");
  emitPair(9, "$HANDSEED");
  emitPair(5, handSeedBuf);
  emitPair(0, "ENDSEC");

  // DXF R13+: CLASSES is required (may be empty). See ezdxf "DXF File Structure" / Autodesk DXF reference.
  emitPair(0, "SECTION");
  emitPair(2, "CLASSES");
  emitPair(0, "ENDSEC");

  emitPair(0, "SECTION");
  emitPair(2, "TABLES");

  emitPair(0, "TABLE");
  emitPair(2, "LTYPE");
  emitPair(5, "3");
  emitPair(100, "AcDbSymbolTable");
  emitPair(70, "7");
  emitPair(0, "LTYPE");
  emitPair(5, "4");
  emitPair(330, "3");
  emitPair(100, "AcDbSymbolTableRecord");
  emitPair(100, "AcDbLinetypeTableRecord");
  emitPair(2, "ByBlock");
  emitPair(70, "0");
  emitPair(3, "");
  emitPair(72, "65");
  emitPair(73, "0");
  emitPair(40, "0.0");
  emitPair(0, "LTYPE");
  emitPair(5, "5");
  emitPair(330, "3");
  emitPair(100, "AcDbSymbolTableRecord");
  emitPair(100, "AcDbLinetypeTableRecord");
  emitPair(2, "ByLayer");
  emitPair(70, "0");
  emitPair(3, "");
  emitPair(72, "65");
  emitPair(73, "0");
  emitPair(40, "0.0");
  emitPair(0, "LTYPE");
  emitPair(5, "6");
  emitPair(330, "3");
  emitPair(100, "AcDbSymbolTableRecord");
  emitPair(100, "AcDbLinetypeTableRecord");
  emitPair(2, "Continuous");
  emitPair(70, "0");
  emitPair(3, "Solid line");
  emitPair(72, "65");
  emitPair(73, "0");
  emitPair(40, "0.0");
  emitPair(0, "LTYPE");
  emitPair(5, "7");
  emitPair(330, "3");
  emitPair(100, "AcDbSymbolTableRecord");
  emitPair(100, "AcDbLinetypeTableRecord");
  emitPair(2, "DASHED");
  emitPair(70, "0");
  emitPair(3, "Dashed __ __ __ __ __ __ __ __ __ __ __ __ __ __");
  emitPair(72, "65");
  emitPair(73, "2");
  emitPair(40, "0.75");
  emitPair(49, "0.5");
  emitPair(74, "0");
  emitPair(49, "-0.25");
  emitPair(74, "0");
  emitPair(0, "LTYPE");
  emitPair(5, "8");
  emitPair(330, "3");
  emitPair(100, "AcDbSymbolTableRecord");
  emitPair(100, "AcDbLinetypeTableRecord");
  emitPair(2, "HIDDEN");
  emitPair(70, "0");
  emitPair(3, "Hidden __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __");
  emitPair(72, "65");
  emitPair(73, "2");
  emitPair(40, "0.375");
  emitPair(49, "0.25");
  emitPair(74, "0");
  emitPair(49, "-0.125");
  emitPair(74, "0");
  emitPair(0, "LTYPE");
  emitPair(5, "9");
  emitPair(330, "3");
  emitPair(100, "AcDbSymbolTableRecord");
  emitPair(100, "AcDbLinetypeTableRecord");
  emitPair(2, "CENTER");
  emitPair(70, "0");
  emitPair(3, "Center ____ _ ____ _ ____ _ ____ _ ____ _ ____");
  emitPair(72, "65");
  emitPair(73, "4");
  emitPair(40, "2.0");
  emitPair(49, "1.25");
  emitPair(74, "0");
  emitPair(49, "-0.25");
  emitPair(74, "0");
  emitPair(49, "0.25");
  emitPair(74, "0");
  emitPair(49, "-0.25");
  emitPair(74, "0");
  emitPair(0, "LTYPE");
  emitPair(5, "A");
  emitPair(330, "3");
  emitPair(100, "AcDbSymbolTableRecord");
  emitPair(100, "AcDbLinetypeTableRecord");
  emitPair(2, "PHANTOM");
  emitPair(70, "0");
  emitPair(3, "Phantom ______  __  __  ______  __  __  ______  __  __  _");
  emitPair(72, "65");
  emitPair(73, "6");
  emitPair(40, "2.5");
  emitPair(49, "1.25");
  emitPair(74, "0");
  emitPair(49, "-0.25");
  emitPair(74, "0");
  emitPair(49, "0.25");
  emitPair(74, "0");
  emitPair(49, "-0.25");
  emitPair(74, "0");
  emitPair(49, "0.25");
  emitPair(74, "0");
  emitPair(49, "-0.25");
  emitPair(74, "0");
  emitPair(0, "ENDTAB");

  emitPair(0, "TABLE");
  emitPair(2, "STYLE");
  emitPair(5, "B");
  emitPair(100, "AcDbSymbolTable");
  emitPair(70, "1");
  emitPair(0, "STYLE");
  emitPair(5, "C");
  emitPair(100, "AcDbSymbolTableRecord");
  emitPair(100, "AcDbTextStyleTableRecord");
  emitPair(2, "Standard");
  emitPair(70, "0");
  emitPair(40, "0.0");
  emitPair(41, "1.0");
  emitPair(50, "0.0");
  emitPair(71, "0");
  emitPair(42, "2.5");
  emitPair(3, "txt");
  emitPair(0, "ENDTAB");

  emitPair(0, "TABLE");
  emitPair(2, "LAYER");
  emitPair(5, "2");
  emitPair(100, "AcDbSymbolTable");
  emitPair(70, std::to_string(static_cast<int>(std::max<size_t>(1, nLayerRows))));

  uint32_t layerRowHandle = 0x10;
  for (const std::string& lyr : layerNames) {
    char hbuf[16];
    std::snprintf(hbuf, sizeof(hbuf), "%X", layerRowHandle++);
    uint32_t lr = DxfRgbPackedFromAci(7);
    auto it = layerRgbHint.find(lyr);
    if (it != layerRgbHint.end())
      lr = it->second & 0xFFFFFFu;
    const int lac = DxfNearestAciFromRgbPacked(lr);

    const CadLayerRow* row = FindLayerRowDxfExport(st, lyr);
    std::string ltype6 = "Continuous";
    if (row && !row->linetype.empty()) {
      ltype6 = CadCanonicalLinetypeNameForDxf(row->linetype);
      if (ltype6.empty())
        ltype6 = "Continuous";
    }

    emitPair(0, "LAYER");
    emitPair(5, hbuf);
    emitPair(330, "2");
    emitPair(100, "AcDbSymbolTableRecord");
    emitPair(100, "AcDbLayerTableRecord");
    emitPair(2, lyr);
    emitPair(70, "0");
    emitPair(62, std::to_string(lac));
    emitPair(6, ltype6);
    emitPair(290, "1");
    emitPair(370, dxfLayerLineweight370Str(row));
    if (row)
      dxfEmitTransparency440IfNeeded(row->transparency);
    emitPair(390, hObjPlotPh);
  }

  emitPair(0, "ENDTAB");

  emitPair(0, "TABLE");
  emitPair(2, "VPORT");
  emitPair(5, hVportTab);
  emitPair(100, "AcDbSymbolTable");
  emitPair(70, "1");
  emitPair(0, "VPORT");
  emitPair(5, hVportAct);
  emitPair(100, "AcDbSymbolTableRecord");
  emitPair(100, "AcDbViewportTableRecord");
  emitPair(2, "*Active");
  emitPair(70, "0");
  emitPair(10, "0.0");
  emitPair(20, "0.0");
  emitPair(11, "1.0");
  emitPair(21, "1.0");
  emitPair(12, std::to_string(vCx));
  emitPair(22, std::to_string(vCy));
  emitPair(13, "0.0");
  emitPair(23, "0.0");
  emitPair(14, "10.0");
  emitPair(24, "10.0");
  emitPair(15, "10.0");
  emitPair(25, "10.0");
  emitPair(16, "0.0");
  emitPair(26, "0.0");
  emitPair(36, "1.0");
  emitPair(17, "0.0");
  emitPair(27, "0.0");
  emitPair(37, "0.0");
  emitPair(40, std::to_string(vViewH));
  emitPair(41, std::to_string(vAsp));
  emitPair(42, "50.0");
  emitPair(43, "0.0");
  emitPair(44, "0.0");
  emitPair(50, "0.0");
  emitPair(51, "0.0");
  emitPair(71, "0");
  emitPair(72, "100");
  emitPair(73, "1");
  emitPair(74, "3");
  emitPair(75, "0");
  emitPair(76, "0");
  emitPair(77, "0");
  emitPair(78, "0");
  emitPair(281, "0");
  emitPair(65, "1");
  emitPair(110, "0.0");
  emitPair(120, "0.0");
  emitPair(130, "0.0");
  emitPair(111, "1.0");
  emitPair(121, "0.0");
  emitPair(131, "0.0");
  emitPair(112, "0.0");
  emitPair(122, "1.0");
  emitPair(132, "0.0");
  emitPair(79, "0");
  emitPair(146, "0.0");
  emitPair(0, "ENDTAB");

  emitPair(0, "TABLE");
  emitPair(2, "VIEW");
  emitPair(5, hViewTab);
  emitPair(330, "0");
  emitPair(100, "AcDbSymbolTable");
  emitPair(70, "0");
  emitPair(0, "ENDTAB");

  emitPair(0, "TABLE");
  emitPair(2, "UCS");
  emitPair(5, hUcsTab);
  emitPair(330, "0");
  emitPair(100, "AcDbSymbolTable");
  emitPair(70, "0");
  emitPair(0, "ENDTAB");

  emitPair(0, "TABLE");
  emitPair(2, "APPID");
  emitPair(5, hAppidTab);
  emitPair(100, "AcDbSymbolTable");
  emitPair(70, "2");
  emitPair(0, "APPID");
  emitPair(5, hAppidAcad);
  emitPair(100, "AcDbSymbolTableRecord");
  emitPair(100, "AcDbRegAppTableRecord");
  emitPair(2, "ACAD");
  emitPair(70, "0");
  // Registered app id for survey-point identity XDATA (REQ-023).
  emitPair(0, "APPID");
  emitPair(5, hGoSurveyAppid);
  emitPair(100, "AcDbSymbolTableRecord");
  emitPair(100, "AcDbRegAppTableRecord");
  emitPair(2, "GOSURVEY");
  emitPair(70, "0");
  emitPair(0, "ENDTAB");

  emitPair(0, "TABLE");
  emitPair(2, "DIMSTYLE");
  emitPair(5, hDimTab);
  emitPair(100, "AcDbSymbolTable");
  emitPair(70, "1");
  emitPair(100, "AcDbDimStyleTable");
  emitPair(71, "1");
  emitPair(0, "DIMSTYLE");
  emitPair(105, hDimStd);
  emitPair(330, hDimTab);
  emitPair(100, "AcDbSymbolTableRecord");
  emitPair(100, "AcDbDimStyleTableRecord");
  emitPair(2, "Standard");
  emitPair(70, "0");
  emitPair(3, "");
  emitPair(40, "1.0");
  emitPair(41, "2.5");
  emitPair(42, "0.625");
  emitPair(43, "3.75");
  emitPair(44, "1.25");
  emitPair(45, "0.0");
  emitPair(46, "0.0");
  emitPair(47, "0.0");
  emitPair(48, "0.0");
  emitPair(140, "2.5");
  emitPair(141, "2.5");
  emitPair(142, "0.625");
  emitPair(143, "0.03937007874016");
  emitPair(144, "1.0");
  emitPair(145, "0.0");
  emitPair(146, "0.0");
  emitPair(147, "0.625");
  emitPair(148, "0.0");
  emitPair(71, "0");
  emitPair(72, "0");
  emitPair(73, "0");
  emitPair(74, "0");
  emitPair(75, "0");
  emitPair(0, "ENDTAB");

  emitPair(0, "TABLE");
  emitPair(2, "BLOCK_RECORD");
  emitPair(5, hBrTab);
  emitPair(100, "AcDbSymbolTable");
  emitPair(70, "2");
  emitPair(0, "BLOCK_RECORD");
  emitPair(5, hBrModel);
  emitPair(100, "AcDbSymbolTableRecord");
  emitPair(100, "AcDbBlockTableRecord");
  emitPair(2, "*Model_Space");
  emitPair(70, "0");
  emitPair(280, "1");
  emitPair(281, "0");
  emitPair(0, "BLOCK_RECORD");
  emitPair(5, hBrPaper);
  emitPair(100, "AcDbSymbolTableRecord");
  emitPair(100, "AcDbBlockTableRecord");
  emitPair(2, "*Paper_Space");
  emitPair(70, "0");
  emitPair(280, "1");
  emitPair(281, "0");
  emitPair(0, "ENDTAB");

  emitPair(0, "ENDSEC");

  emitPair(0, "SECTION");
  emitPair(2, "BLOCKS");
  emitPair(0, "BLOCK");
  emitPair(5, hBlkM0);
  emitPair(330, hBrModel);
  emitPair(100, "AcDbEntity");
  emitPair(8, "0");
  emitPair(100, "AcDbBlockBegin");
  emitPair(2, "*Model_Space");
  emitPair(70, "0");
  emitPair(10, "0.0");
  emitPair(20, "0.0");
  emitPair(30, "0.0");
  emitPair(3, "*Model_Space");
  emitPair(0, "ENDBLK");
  emitPair(5, hBlkM1);
  emitPair(330, hBrModel);
  emitPair(100, "AcDbEntity");
  emitPair(8, "0");
  emitPair(100, "AcDbBlockEnd");
  emitPair(0, "BLOCK");
  emitPair(5, hBlkP0);
  emitPair(330, hBrPaper);
  emitPair(100, "AcDbEntity");
  emitPair(8, "0");
  emitPair(100, "AcDbBlockBegin");
  emitPair(2, "*Paper_Space");
  emitPair(70, "1");
  emitPair(10, "0.0");
  emitPair(20, "0.0");
  emitPair(30, "0.0");
  emitPair(3, "*Paper_Space");
  emitPair(0, "ENDBLK");
  emitPair(5, hBlkP1);
  emitPair(330, hBrPaper);
  emitPair(100, "AcDbEntity");
  emitPair(8, "0");
  emitPair(100, "AcDbBlockEnd");
  emitPair(0, "ENDSEC");

  emitPair(0, "SECTION");
  emitPair(2, "ENTITIES");

  const double ox = st.worldDocumentOriginX;
  const double oy = st.worldDocumentOriginY;
  auto worldX = [&](float lx) { return static_cast<double>(lx) + ox; };
  auto worldY = [&](float ly) { return static_cast<double>(ly) + oy; };

  // Group 210 is written at FULL double precision, not through `std::to_string` like every other
  // number here (REQ-312). It is the one value in a DXF whose error is ANGULAR rather than
  // positional: the reader rebuilds the entity's whole coordinate frame from it, and an angular
  // error of dTheta moves a point R from the world origin by about R * dTheta. At state-plane
  // magnitude R is ~1e6, so the six decimals that are ample for a coordinate are not remotely
  // enough for a direction.
  //
  // Measured over 400,000 random normals with centres out to +/-2e6, worst case:
  //     six decimals (std::to_string)          65.4       ft   - fails REQ-101 by ~6500x
  //     %.9g          (round-trips a float)     0.009     ft   - inside +/-0.01, with no margin
  //     %.17g         (round-trips a double)    0.00000086 ft  - what this uses
  // %.9g is not enough because the READER parses to double: nine digits identify the float but not
  // the double the reader ends up holding, and that residual is an angle too.
  //
  // Flat curves never reach this - they emit the literal "0.0"/"0.0"/"1.0" below, unchanged, so no
  // existing DXF changes by a byte.
  auto extrusionText = [](double v) {
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%.17g", v);
    return std::string(buf);
  };

  // A curve's group 10/20/30 in the OCS its group 210 implies (REQ-312).
  //
  // Group 210 does not merely annotate a world point with a direction: it makes the entity's own
  // coordinates OBJECT-coordinate values, in the Arbitrary Axis Algorithm frame built from the
  // normal. Writing world coordinates alongside a non-+Z 210 would describe a different circle to
  // every other DXF consumer. `ucs::FromNormal` IS that algorithm (REQ-311, D-2026-08-31-e), and
  // for a +Z normal it returns the world axes exactly - so a flat curve's OCS point is its world
  // point, bit for bit, and the flat path below is unchanged.
  auto ocsPointOf = [](double wx, double wy, double wz, double nx, double ny, double nz) {
    ucs::Ucs frame;
    if (!ucs::FromNormal({0.0, 0.0, 0.0}, {nx, ny, nz}, &frame))
      return ray3d::Vec3{wx, wy, wz};  // refused upstream; never a silent garbage frame (REQ-201)
    return ucs::WorldToUcs(frame, {wx, wy, wz});
  };
  // Common DXF entity header: handle, model-space owner, AcDbEntity subclass, layer, linetype, color, lineweight, transparency.
  auto emitEntityHeader = [&](const char* hb, const std::string& layer8, const EntityAttributes& at,
                               int aci, const CadLayerRow* lyr) {
    emitPair(5, hb);
    emitPair(330, hBrModel);
    emitPair(100, "AcDbEntity");
    emitPair(8, layer8);
    emitPair(6, DxfExportEntityLtype6(at));
    emitPair(62, std::to_string(aci));
    emitPair(370, dxfEntityLineweight370Str(at));
    dxfEmitTransparency440IfNeeded(EffectiveEntityTransparency01(at, lyr));
  };

  uint64_t entHandle = entityHandleStart;
  for (size_t i = 0; i < nSeg; ++i) {
    char hb[24];
    std::snprintf(hb, sizeof(hb), "%llX", static_cast<unsigned long long>(entHandle++));
    const float x0 = st.userLinesFlat[i * 6 + 0];
    const float y0 = st.userLinesFlat[i * 6 + 1];
    const float z0 = st.userLinesFlat[i * 6 + 2];
    const float x1 = st.userLinesFlat[i * 6 + 3];
    const float y1 = st.userLinesFlat[i * 6 + 4];
    const float z1 = st.userLinesFlat[i * 6 + 5];
    EntityAttributes at{};
    if (i < st.userLineAttrs.size())
      at = st.userLineAttrs[i];
    const uint32_t rgb =
        AttrResolvedRgbPacked(at, layerRgbHint) & 0xFFFFFFu;
    const int entAci = DxfNearestAciFromRgbPacked(rgb);

    const std::string layer8 = at.layer.empty() ? std::string("0") : at.layer;
    const CadLayerRow* lyr = FindLayerRowDxfExport(st, layer8);

    emitPair(0, "LINE");
    emitEntityHeader(hb, layer8, at, entAci, lyr);
    emitPair(100, "AcDbLine");
    emitPair(10, std::to_string(worldX(x0)));
    emitPair(20, std::to_string(worldY(y0)));
    emitPair(30, std::to_string(static_cast<double>(z0)));
    emitPair(11, std::to_string(worldX(x1)));
    emitPair(21, std::to_string(worldY(y1)));
    emitPair(31, std::to_string(static_cast<double>(z1)));
    emitPair(210, "0.0");
    emitPair(220, "0.0");
    emitPair(230, "1.0");
  }

  for (size_t ci = 0; ci < nCirc; ++ci) {
    char hb[24];
    std::snprintf(hb, sizeof(hb), "%llX", static_cast<unsigned long long>(entHandle++));
    EntityAttributes at{};
    if (ci < st.userCircleAttrs.size())
      at = st.userCircleAttrs[ci];
    const uint32_t rgb =
        AttrResolvedRgbPacked(at, layerRgbHint) & 0xFFFFFFu;
    const int entAci = DxfNearestAciFromRgbPacked(rgb);
    const float cx = st.userCirclesCxCyZR[ci * 4];
    const float cy = st.userCirclesCxCyZR[ci * 4 + 1];
    const float cz = st.userCirclesCxCyZR[ci * 4 + 2];
    const float rr = st.userCirclesCxCyZR[ci * 4 + 3];

    const std::string layer8 = at.layer.empty() ? std::string("0") : at.layer;
    const CadLayerRow* lyr = FindLayerRowDxfExport(st, layer8);

    float cnx = kFlatNormalX;
    float cny = kFlatNormalY;
    float cnz = kFlatNormalZ;
    CircleNormalAt(st.userCircleNormals, ci, &cnx, &cny, &cnz);
    const bool circFlat = IsFlatNormal(cnx, cny, cnz);
    const ray3d::Vec3 p10 =
        circFlat ? ray3d::Vec3{worldX(cx), worldY(cy), static_cast<double>(cz)}
                 : ocsPointOf(worldX(cx), worldY(cy), static_cast<double>(cz), static_cast<double>(cnx),
                              static_cast<double>(cny), static_cast<double>(cnz));

    emitPair(0, "CIRCLE");
    emitEntityHeader(hb, layer8, at, entAci, lyr);
    emitPair(100, "AcDbCircle");
    emitPair(10, std::to_string(p10.x));
    emitPair(20, std::to_string(p10.y));
    emitPair(30, std::to_string(p10.z));  // elevation (REQ-057), absolute; OCS Z when tilted
    emitPair(40, std::to_string(static_cast<double>(rr)));
    if (circFlat) {
      emitPair(210, "0.0");
      emitPair(220, "0.0");
      emitPair(230, "1.0");
    } else {
      emitPair(210, extrusionText(static_cast<double>(cnx)));
      emitPair(220, extrusionText(static_cast<double>(cny)));
      emitPair(230, extrusionText(static_cast<double>(cnz)));
    }
  }

  // Arcs (#63). The exporter named `userArcs` nowhere, so every arc GoSurvey ever wrote to a DXF was
  // silently absent from the file — REQ-204's "an entity type silently dropped by an exporter with
  // no branch for it", found by the oracle written to find it.
  //
  // The angles themselves come from `DxfArcToWrite` — including the counter-clockwise direction
  // trap, which lives next to the code that handles it (issue #111).
  for (size_t ai = 0; ai < st.userArcs.size(); ++ai) {
    char hb[24];
    std::snprintf(hb, sizeof(hb), "%llX", static_cast<unsigned long long>(entHandle++));
    const CadArc& arc = st.userArcs[ai];
    EntityAttributes at{};
    if (ai < st.userArcAttrs.size())
      at = st.userArcAttrs[ai];
    const uint32_t rgb = AttrResolvedRgbPacked(at, layerRgbHint) & 0xFFFFFFu;
    const int entAci = DxfNearestAciFromRgbPacked(rgb);

    const std::string layer8 = at.layer.empty() ? std::string("0") : at.layer;
    const CadLayerRow* lyr = FindLayerRowDxfExport(st, layer8);

    // The one answer to what arc this file states — the same one the header extents above were
    // swept from (issue #111).
    const DxfArcAsWritten aw = DxfArcToWrite(arc);

    // Groups 50/51 need no adjustment for a tilted arc: `DxfArcToWrite` measures them in the arc's
    // own frame, `ucs::FromNormal(centre, normal)`, and the OCS shares that frame's AXES - it
    // differs only in where its origin sits, which an angle about the centre cannot see.
    const bool arcFlat = IsFlatNormal(arc.nx, arc.ny, arc.nz);
    const ray3d::Vec3 a10 =
        arcFlat ? ray3d::Vec3{worldX(arc.cx), worldY(arc.cy), static_cast<double>(arc.z)}
                : ocsPointOf(worldX(arc.cx), worldY(arc.cy), static_cast<double>(arc.z),
                             static_cast<double>(arc.nx), static_cast<double>(arc.ny),
                             static_cast<double>(arc.nz));

    emitPair(0, "ARC");
    emitEntityHeader(hb, layer8, at, entAci, lyr);
    emitPair(100, "AcDbCircle");
    emitPair(10, std::to_string(a10.x));
    emitPair(20, std::to_string(a10.y));
    emitPair(30, std::to_string(a10.z));  // elevation (REQ-057), absolute; OCS Z when tilted
    emitPair(40, std::to_string(static_cast<double>(arc.r)));
    if (arcFlat) {
      emitPair(210, "0.0");
      emitPair(220, "0.0");
      emitPair(230, "1.0");
    } else {
      emitPair(210, extrusionText(static_cast<double>(arc.nx)));
      emitPair(220, extrusionText(static_cast<double>(arc.ny)));
      emitPair(230, extrusionText(static_cast<double>(arc.nz)));
    }
    emitPair(100, "AcDbArc");
    emitPair(50, aw.startDeg);
    emitPair(51, aw.endDeg);
  }

  // Ellipses (#63), dropped the same way and by the same omission. Group 11/21/31 is the major-axis
  // ENDPOINT RELATIVE TO THE CENTRE — a vector, not a point — so it takes no document-origin shift,
  // unlike the centre above. `CadEllipse` holds no parameter range, so every ellipse it can describe
  // is a full one: 41/42 span exactly one turn. A trimmed ELLIPSE read from another program is
  // tessellated on import and never reaches this store (TASK-114 DEBT-1).
  for (size_t ei = 0; ei < st.userEllipses.size(); ++ei) {
    char hb[24];
    std::snprintf(hb, sizeof(hb), "%llX", static_cast<unsigned long long>(entHandle++));
    const CadEllipse& el = st.userEllipses[ei];
    EntityAttributes at{};
    if (ei < st.userEllAttrs.size())
      at = st.userEllAttrs[ei];
    const uint32_t rgb = AttrResolvedRgbPacked(at, layerRgbHint) & 0xFFFFFFu;
    const int entAci = DxfNearestAciFromRgbPacked(rgb);

    const std::string layer8 = at.layer.empty() ? std::string("0") : at.layer;
    const CadLayerRow* lyr = FindLayerRowDxfExport(st, layer8);

    emitPair(0, "ELLIPSE");
    emitEntityHeader(hb, layer8, at, entAci, lyr);
    emitPair(100, "AcDbEllipse");
    // The one answer to what ellipse this file states — the same one the header extents
    // above were swept from (issue #113).
    const DxfEllipseAsWritten ewEmit = DxfEllipseToWrite(el);
    emitPair(10, std::to_string(worldX(el.cx)));
    emitPair(20, std::to_string(worldY(el.cy)));
    emitPair(30, std::to_string(static_cast<double>(el.z)));  // elevation (REQ-057), absolute
    emitPair(11, std::to_string(static_cast<double>(ewEmit.majVx)));
    emitPair(21, std::to_string(static_cast<double>(ewEmit.majVy)));
    emitPair(31, "0.0");
    emitPair(210, "0.0");
    emitPair(220, "0.0");
    emitPair(230, "1.0");
    emitPair(40, std::to_string(static_cast<double>(ewEmit.ratio)));
    emitPair(41, "0.0");
    emitPair(42, std::to_string(2.0 * kPi));
  }

  // Polylines — including every RECT, which is stored as a 4-vertex closed polyline (REQ-053). Before this
  // the exporter had no LWPOLYLINE branch at all, so polylines were dropped from the DXF without a word.
  size_t nPolyOut = 0;
  {
    const int polyCount =
        static_cast<int>(st.userPolylineOffsets.size() > 0 ? st.userPolylineOffsets.size() - 1 : 0);
    for (int pi = 0; pi < polyCount; ++pi) {
      const int v0 = st.userPolylineOffsets[static_cast<size_t>(pi)];
      const int v1 = st.userPolylineOffsets[static_cast<size_t>(pi + 1)];
      if (v1 - v0 < 2)
        continue;
      EntityAttributes at{};
      if (static_cast<size_t>(pi) < st.userPolylineAttrs.size())
        at = st.userPolylineAttrs[static_cast<size_t>(pi)];
      const uint32_t rgb = AttrResolvedRgbPacked(at, layerRgbHint) & 0xFFFFFFu;
      const std::string layer8 = at.layer.empty() ? std::string("0") : at.layer;
      const CadLayerRow* lyr = FindLayerRowDxfExport(st, layer8);

      char hb[24];
      std::snprintf(hb, sizeof(hb), "%llX", static_cast<unsigned long long>(entHandle++));

      DxfLwPolylineRecord rec;
      rec.handleHex = hb;
      rec.ownerHandleHex = hBrModel;
      rec.layer = layer8;
      rec.linetype = DxfExportEntityLtype6(at);
      rec.colorAci = std::to_string(DxfNearestAciFromRgbPacked(rgb));
      rec.lineweight370 = dxfEntityLineweight370Str(at);
      rec.hasTransparency =
          dxfTransparency440Str(EffectiveEntityTransparency01(at, lyr), &rec.transparency440);
      rec.closed = static_cast<size_t>(pi) < st.userPolylineClosed.size() &&
                   st.userPolylineClosed[static_cast<size_t>(pi)] != 0;
      // LWPOLYLINE carries ONE elevation (group 38) for the whole polyline, so a genuinely 3D
      // polyline cannot round-trip through it — the first vertex's Z is written and the rest are
      // dropped. Recorded as technical debt in the TASK-034 log: carrying per-vertex Z needs the
      // 3D POLYLINE/VERTEX entity pair, which is its own change.
      if (v1 > v0)
        rec.elevation38 = std::to_string(static_cast<double>(st.userPolylineVerts[static_cast<size_t>(v0 * 3 + 2)]));
      rec.vertices.reserve(static_cast<size_t>(v1 - v0));
      for (int vi = v0; vi < v1; ++vi)
        rec.vertices.emplace_back(std::to_string(worldX(st.userPolylineVerts[static_cast<size_t>(vi * 3)])),
                                  std::to_string(worldY(st.userPolylineVerts[static_cast<size_t>(vi * 3 + 1)])));
      // REQ-316 / ADR-047: per-vertex bulge (group 42). Only emitted when this polyline has any
      // curved segment; a straight polyline writes no group 42 and round-trips exactly as before.
      {
        bool anyBulge = false;
        for (int vi = v0; vi < v1 && vi < static_cast<int>(st.userPolylineVertsBulge.size()); ++vi)
          if (st.userPolylineVertsBulge[static_cast<size_t>(vi)] != 0.0f) { anyBulge = true; break; }
        if (anyBulge) {
          rec.bulges.reserve(static_cast<size_t>(v1 - v0));
          for (int vi = v0; vi < v1; ++vi) {
            const float b = vi < static_cast<int>(st.userPolylineVertsBulge.size())
                                ? st.userPolylineVertsBulge[static_cast<size_t>(vi)] : 0.0f;
            rec.bulges.push_back(b == 0.0f ? std::string("0") : std::to_string(static_cast<double>(b)));
          }
        }
      }
      emitLwPolylineRecord(rec);
      ++nPolyOut;
    }
  }

  size_t nPointOut = 0;
  for (const SurveyPoint& p : st.surveyPoints) {
    char hb[24];
    std::snprintf(hb, sizeof(hb), "%llX", static_cast<unsigned long long>(entHandle++));
    EntityAttributes at{};
    at.layer = p.layer.empty() ? std::string("0") : p.layer;
    at.color = "ByLayer";
    const uint32_t rgb = AttrResolvedRgbPacked(at, layerRgbHint) & 0xFFFFFFu;
    const int entAci = DxfNearestAciFromRgbPacked(rgb);
    const std::string layer = at.layer;
    const CadLayerRow* lyr = FindLayerRowDxfExport(st, layer);

    emitPair(0, "POINT");
    emitEntityHeader(hb, layer, at, entAci, lyr);
    emitPair(100, "AcDbPoint");
    emitPair(10, std::to_string(worldX(p.easting)));
    emitPair(20, std::to_string(worldY(p.northing)));
    emitPair(30, std::to_string(static_cast<double>(p.elevation)));
    emitPair(39, "0.0");
    emitPair(210, "0.0");
    emitPair(220, "0.0");
    emitPair(230, "1.0");
    // Survey-point identity XDATA (REQ-023, ADR-005): id, label style, description.
    // Coordinates stay in 10/20/30 and layer in 8; a reader that ignores the
    // GOSURVEY app id still sees a valid POINT.
    {
      auto flatten = [](std::string s) {
        for (char& c : s)
          if (c == '\r' || c == '\n') c = ' ';
        if (s.size() > 255) s.resize(255);  // XDATA string limit
        return s;
      };
      emitPair(1001, "GOSURVEY");
      emitPair(1071, std::to_string(p.id));
      emitPair(1070, std::to_string(static_cast<int>(p.labelStyle)));
      // Two 1000 strings, distinguished by ORDER: first = description, second = raw description
      // (REQ-066). 1000 is the only general string code XDATA offers, so position is the only
      // discriminator available; the reader counts them the same way. A pre-REQ-066 file emits one,
      // which is why an old DXF loads with rawDescription empty rather than misreading the
      // description as a field code.
      emitPair(1000, flatten(p.description));
      emitPair(1000, flatten(p.rawDescription));
    }
    ++nPointOut;
  }

  auto sanitizeDxfText = [](std::string t) {
    for (char& c : t) {
      if (c == '\r' || c == '\n')
        c = ' ';
    }
    return t;
  };

  size_t nTextOut = 0;
  size_t nMtextOut = 0;
  size_t nDimExplodedLines = 0;
  for (size_t ai = 0; ai < st.cadAnnotations.size(); ++ai) {
    const CadAnnotation& an = st.cadAnnotations[ai];
    EntityAttributes at{};
    if (ai < st.cadAnnotationAttrs.size())
      at = st.cadAnnotationAttrs[ai];
    const uint32_t rgb =
        AttrResolvedRgbPacked(at, layerRgbHint) & 0xFFFFFFu;
    const int entAci = DxfNearestAciFromRgbPacked(rgb);
    const std::string layer = at.layer.empty() ? std::string("0") : at.layer;
    const CadLayerRow* annLyr = FindLayerRowDxfExport(st, layer);

    if (an.kind == CadAnnotation::Kind::Text) {
      char hb[24];
      std::snprintf(hb, sizeof(hb), "%llX", static_cast<unsigned long long>(entHandle++));
      // Underline round-trips through the AutoCAD %%u toggle.
      const std::string txt = (an.underline ? std::string("%%u") : std::string()) + sanitizeDxfText(an.text);
      const double rotRad = static_cast<double>(an.rotationRad);
      const double hWorld = static_cast<double>(CadAnnotationHeightWorld(an, st.modelUnitsPerPlottedInch));
      // Record layout (incl. the mandatory second AcDbText subclass) lives in DxfEntityEmit.hpp
      // so it can be unit-tested without the GUI stack — see that header for why.
      DxfTextRecord rec;
      rec.handleHex = hb;
      rec.ownerHandleHex = hBrModel;
      rec.layer = layer;
      rec.linetype = DxfExportEntityLtype6(at);
      rec.colorAci = std::to_string(entAci);
      rec.lineweight370 = dxfEntityLineweight370Str(at);
      rec.hasTransparency = dxfTransparency440Str(EffectiveEntityTransparency01(at, annLyr),
                                                  &rec.transparency440);
      // insX/insY is the top-left; DXF group 10/20 is the baseline (one text height lower).
      rec.x = std::to_string(static_cast<double>(an.insX));
      rec.y = std::to_string(static_cast<double>(an.insY) - hWorld);
      rec.z = std::to_string(static_cast<double>(an.insZ));  // elevation (REQ-057)
      rec.height = std::to_string(hWorld);
      rec.text = txt;
      rec.rotationDeg = std::to_string(rotRad * (180.0 / kPi)); // DXF group 50 is DEGREES
      emitTextRecord(rec);
      ++nTextOut;
    } else if (an.kind == CadAnnotation::Kind::Table && an.tableCols > 0) {
      std::vector<CadTableCellRect> cells;
      CadTableLayoutCells(an.boxMinX, an.boxMinY, an.boxMaxX, an.boxMaxY, an.tableCols, an.tableCells, &cells);
      const double hWorld = static_cast<double>(CadAnnotationHeightWorld(an, st.modelUnitsPerPlottedInch));
      for (size_t ci = 0; ci < cells.size() && ci < an.tableCells.size(); ++ci) {
        char hb[24];
        std::snprintf(hb, sizeof(hb), "%llX", static_cast<unsigned long long>(entHandle++));
        DxfTextRecord rec;
        rec.handleHex = hb;
        rec.ownerHandleHex = hBrModel;
        rec.layer = layer;
        rec.linetype = DxfExportEntityLtype6(at);
        rec.colorAci = std::to_string(entAci);
        rec.lineweight370 = dxfEntityLineweight370Str(at);
        rec.hasTransparency = dxfTransparency440Str(EffectiveEntityTransparency01(at, annLyr),
                                                    &rec.transparency440);
        rec.x = std::to_string(worldX(cells[ci].x0));
        rec.y = std::to_string(worldY(static_cast<float>(static_cast<double>(cells[ci].y1) - hWorld)));
        rec.z = std::to_string(static_cast<double>(an.insZ));
        rec.height = std::to_string(hWorld);
        rec.text = sanitizeDxfText(an.tableCells[ci]);
        rec.rotationDeg = "0";
        emitTextRecord(rec);
        ++nTextOut;
      }
    } else if (an.kind == CadAnnotation::Kind::DimAligned || an.kind == CadAnnotation::Kind::DimLinear) {
      float sx1 = 0.f, sy1 = 0.f, sx2 = 0.f, sy2 = 0.f, tx = 0.f, ty = 0.f, nx = 0.f, ny = 0.f, meas = 0.f;
      if (!CadDimAnyGeometry(an, &sx1, &sy1, &sx2, &sy2, &tx, &ty, &nx, &ny, &meas))
        continue;
      const float gap = std::clamp(0.012f * meas, 1.e-5f * meas, 0.12f * meas);
      const float over = std::clamp(0.02f * meas, 1.e-5f * meas, 0.1f * meas);
      const float leg1 = std::hypot(sx1 - an.dimExt1X, sy1 - an.dimExt1Y);
      const float u1 = leg1 > 1.e-8f ? gap / leg1 : 0.f;
      const float ex1 = an.dimExt1X + (sx1 - an.dimExt1X) * u1;
      const float ey1 = an.dimExt1Y + (sy1 - an.dimExt1Y) * u1;
      const float leg2 = std::hypot(sx2 - an.dimExt2X, sy2 - an.dimExt2Y);
      const float u2 = leg2 > 1.e-8f ? gap / leg2 : 0.f;
      const float ex2 = an.dimExt2X + (sx2 - an.dimExt2X) * u2;
      const float ey2 = an.dimExt2Y + (sy2 - an.dimExt2Y) * u2;
      auto emitLine = [&](float x0, float y0, float x1, float y1) {
        char hb[24];
        std::snprintf(hb, sizeof(hb), "%llX", static_cast<unsigned long long>(entHandle++));
        emitPair(0, "LINE");
        emitEntityHeader(hb, layer, at, entAci, annLyr);
        emitPair(100, "AcDbLine");
        // A dimension's leader/extension lines sit on the dimension's own plane (REQ-057).
        const std::string dimZ = std::to_string(static_cast<double>(an.insZ));
        emitPair(10, std::to_string(static_cast<double>(x0)));
        emitPair(20, std::to_string(static_cast<double>(y0)));
        emitPair(30, dimZ);
        emitPair(11, std::to_string(static_cast<double>(x1)));
        emitPair(21, std::to_string(static_cast<double>(y1)));
        emitPair(31, dimZ);
        emitPair(210, "0.0");
        emitPair(220, "0.0");
        emitPair(230, "1.0");
        ++nDimExplodedLines;
      };
      emitLine(ex1, ey1, sx1 + nx * over, sy1 + ny * over);
      emitLine(ex2, ey2, sx2 + nx * over, sy2 + ny * over);
      emitLine(sx1, sy1, sx2, sy2);
      char hb[24];
      std::snprintf(hb, sizeof(hb), "%llX", static_cast<unsigned long long>(entHandle++));
      const std::string txt = sanitizeDxfText(an.text);
      const double rotRad = static_cast<double>(an.rotationRad);
      DxfTextRecord rec;
      rec.handleHex = hb;
      rec.ownerHandleHex = hBrModel;
      rec.layer = layer;
      rec.linetype = DxfExportEntityLtype6(at);
      rec.colorAci = std::to_string(entAci);
      rec.lineweight370 = dxfEntityLineweight370Str(at);
      rec.hasTransparency = dxfTransparency440Str(EffectiveEntityTransparency01(at, annLyr),
                                                  &rec.transparency440);
      rec.x = std::to_string(static_cast<double>(an.insX));
      rec.y = std::to_string(static_cast<double>(an.insY));
      rec.z = std::to_string(static_cast<double>(an.insZ));  // elevation (REQ-057)
      rec.height =
          std::to_string(static_cast<double>(CadAnnotationHeightWorld(an, st.modelUnitsPerPlottedInch)));
      rec.text = txt;
      rec.rotationDeg = std::to_string(rotRad * (180.0 / kPi)); // DXF group 50 is DEGREES
      emitTextRecord(rec);
      ++nTextOut;
    } else {
      char hb[24];
      std::snprintf(hb, sizeof(hb), "%llX", static_cast<unsigned long long>(entHandle++));
      const std::string txt = sanitizeDxfText(MtextRichFlattenToPlain(an.text));
      const float bw =
          std::max(1.f, std::fabs(an.boxMaxX - an.boxMinX));
      const double rotRad = static_cast<double>(an.rotationRad);
      const double m11 = std::cos(rotRad);
      const double m21 = std::sin(rotRad);
      emitPair(0, "MTEXT");
      emitPair(5, hb);
      emitPair(330, hBrModel);
      emitPair(100, "AcDbEntity");
      emitPair(8, layer);
      emitPair(6, DxfExportEntityLtype6(at));
      emitPair(7, "Standard");
      emitPair(62, std::to_string(entAci));
      emitPair(370, dxfEntityLineweight370Str(at));
      dxfEmitTransparency440IfNeeded(EffectiveEntityTransparency01(at, annLyr));
      emitPair(100, "AcDbMText");
      // Insertion point is the box corner/edge selected by the attachment (group 71):
      // col 0/1/2 = left/center/right (min/mid/max X), row 0/1/2 = top/middle/bottom (max/mid/min Y).
      const int attach = std::clamp(an.mtextAttach, 1, 9);
      const int acol = (attach - 1) % 3;
      const int arow = (attach - 1) / 3;
      const float bMnX = std::min(an.boxMinX, an.boxMaxX), bMxX = std::max(an.boxMinX, an.boxMaxX);
      const float bMnY = std::min(an.boxMinY, an.boxMaxY), bMxY = std::max(an.boxMinY, an.boxMaxY);
      const double insXw = acol == 0 ? bMnX : acol == 1 ? 0.5 * (bMnX + bMxX) : bMxX;
      const double insYw = arow == 0 ? bMxY : arow == 1 ? 0.5 * (bMnY + bMxY) : bMnY;
      emitPair(10, std::to_string(insXw));
      emitPair(20, std::to_string(insYw));
      emitPair(30, std::to_string(static_cast<double>(an.insZ)));  // elevation (REQ-057)
      emitPair(40, std::to_string(static_cast<double>(CadAnnotationHeightWorld(an, st.modelUnitsPerPlottedInch))));
      emitPair(41, std::to_string(static_cast<double>(bw)));
      emitPair(71, std::to_string(attach));
      emitPair(72, "0");
      emitPair(11, std::to_string(m11));
      emitPair(21, std::to_string(m21));
      emitPair(31, "0.0");
      emitPair(50, std::to_string(rotRad * (180.0 / kPi))); // DXF group 50 is DEGREES
      emitPair(210, "0.0");
      emitPair(220, "0.0");
      emitPair(230, "1.0");
      emitPair(1, txt);
      // Survey-point label marker (REQ-023): import skips these and lets the
      // reconstructed point regenerate its own linked label (avoids duplicates).
      if (an.surveyPointLabelForId >= 0)
        emitPair(1001, "GOSURVEY");
      ++nMtextOut;
    }
  }

  for (size_t ti = 0; ti < st.cadTables.size(); ++ti) {
    const CadTable& t = st.cadTables[ti];
    if (t.cols <= 0)
      continue;
    EntityAttributes at{};
    if (ti < st.cadTableAttrs.size())
      at = st.cadTableAttrs[ti];
    const uint32_t rgb = AttrResolvedRgbPacked(at, layerRgbHint) & 0xFFFFFFu;
    const int entAci = DxfNearestAciFromRgbPacked(rgb);
    const std::string layer = at.layer.empty() ? std::string("0") : at.layer;
    const CadLayerRow* tblLyr = FindLayerRowDxfExport(st, layer);
    std::vector<CadTableCellRect> cells;
    CadTableLayoutWorldCells(t, &cells);
    const double hWorld = static_cast<double>(CadTableHeightWorld(t, st.modelUnitsPerPlottedInch));
    const double rotDeg = static_cast<double>(t.rotationRad) * (180.0 / kPi);
    for (size_t ci = 0; ci < cells.size() && ci < t.cells.size(); ++ci) {
      char hb[24];
      std::snprintf(hb, sizeof(hb), "%llX", static_cast<unsigned long long>(entHandle++));
      DxfTextRecord rec;
      rec.handleHex = hb;
      rec.ownerHandleHex = hBrModel;
      rec.layer = layer;
      rec.linetype = DxfExportEntityLtype6(at);
      rec.colorAci = std::to_string(entAci);
      rec.lineweight370 = dxfEntityLineweight370Str(at);
      rec.hasTransparency = dxfTransparency440Str(EffectiveEntityTransparency01(at, tblLyr), &rec.transparency440);
      rec.x = std::to_string(worldX(cells[ci].x0));
      rec.y = std::to_string(worldY(static_cast<float>(static_cast<double>(cells[ci].y1) - hWorld)));
      rec.z = std::to_string(static_cast<double>(t.insZ));
      rec.height = std::to_string(hWorld);
      rec.text = sanitizeDxfText(t.cells[ci]);
      rec.rotationDeg = std::to_string(rotDeg);
      emitTextRecord(rec);
      ++nTextOut;
    }
  }

  // Filled regions (ADR-011) → SOLID HATCH; each boundary loop (outer + holes) is one polyline path.
  size_t nHatchOut = 0;
  for (size_t fi = 0; fi < st.cadFilledRegions.size(); ++fi) {
    const CadFilledRegion& fr = st.cadFilledRegions[fi];
    if (fr.loopStart.empty() || fr.vertsXyz.size() < 9)  // < 3 vertices × 3 floats
      continue;
    EntityAttributes at{};
    if (fi < st.cadFilledRegionAttrs.size())
      at = st.cadFilledRegionAttrs[fi];
    const uint32_t rgb = AttrResolvedRgbPacked(at, layerRgbHint) & 0xFFFFFFu;
    const int entAci = DxfNearestAciFromRgbPacked(rgb);
    const std::string layer = at.layer.empty() ? std::string("0") : at.layer;
    const CadLayerRow* lyr = FindLayerRowDxfExport(st, layer);
    char hb[24];
    std::snprintf(hb, sizeof(hb), "%llX", static_cast<unsigned long long>(entHandle++));
    emitPair(0, "HATCH");
    emitEntityHeader(hb, layer, at, entAci, lyr);
    emitPair(100, "AcDbHatch");
    emitPair(10, "0.0");  // elevation point
    emitPair(20, "0.0");
    emitPair(30, "0.0");
    emitPair(210, "0.0");
    emitPair(220, "0.0");
    emitPair(230, "1.0");
    emitPair(2, "SOLID");
    emitPair(70, "1");  // solid fill
    emitPair(71, "0");  // non-associative
    emitPair(91, std::to_string(fr.loopStart.size()));  // boundary path count
    for (size_t li = 0; li < fr.loopStart.size(); ++li) {
      const int begin = fr.loopStart[li];
      const int cnt = fr.loopCount(li);
      if (cnt < 3)
        continue;
      emitPair(92, "3");  // external (1) + polyline (2)
      emitPair(72, "0");  // polyline boundary, no bulges
      emitPair(73, "1");  // closed
      emitPair(93, std::to_string(cnt));
      for (int v = 0; v < cnt; ++v) {
        emitPair(10, std::to_string(worldX(fr.vertsXyz[static_cast<size_t>(begin + v) * 3 + 0])));
        emitPair(20, std::to_string(worldY(fr.vertsXyz[static_cast<size_t>(begin + v) * 3 + 1])));
      }
      emitPair(97, "0");  // number of source boundary objects
    }
    emitPair(75, "0");  // hatch style = normal
    emitPair(76, "1");  // predefined pattern type
    emitPair(98, "1");  // one seed point
    emitPair(10, std::to_string(worldX(fr.vertsXyz[0])));
    emitPair(20, std::to_string(worldY(fr.vertsXyz[1])));
    ++nHatchOut;
  }

  emitPair(0, "ENDSEC");

  // Named object dictionary: ACAD_GROUP + ACAD_PLOTSTYLENAME (required for LAYER group 390).
  emitPair(0, "SECTION");
  emitPair(2, "OBJECTS");
  emitPair(0, "DICTIONARY");
  emitPair(5, hObjRoot);
  emitPair(330, "0");
  emitPair(100, "AcDbDictionary");
  emitPair(281, "1");
  emitPair(3, "ACAD_GROUP");
  emitPair(350, hObjAcadGroup);
  emitPair(3, "ACAD_PLOTSTYLENAME");
  emitPair(350, hObjPlotDict);
  emitPair(0, "DICTIONARY");
  emitPair(5, hObjAcadGroup);
  emitPair(330, hObjRoot);
  emitPair(100, "AcDbDictionary");
  emitPair(281, "1");
  emitPair(0, "ACDBPLACEHOLDER");
  emitPair(5, hObjPlotPh);
  emitPair(330, hObjPlotDict);
  emitPair(0, "ACDBDICTIONARYWDFLT");
  emitPair(5, hObjPlotDict);
  emitPair(330, hObjRoot);
  emitPair(100, "AcDbDictionary");
  emitPair(281, "1");
  emitPair(3, "Normal");
  emitPair(350, hObjPlotPh);
  emitPair(100, "AcDbDictionaryWithDefault");
  emitPair(340, hObjPlotPh);
  emitPair(0, "ENDSEC");

  emitPair(0, "EOF");

  log.push_back("DXF export — wrote " + std::to_string(nSeg) + " LINE(s), " + std::to_string(nCirc) + " CIRCLE(s), " +
                std::to_string(nPolyOut) + " LWPOLYLINE(s), " +
                std::to_string(nPointOut) + " POINT(s), " + std::to_string(nTextOut) + " TEXT, " +
                std::to_string(nMtextOut) + " MTEXT, " + std::to_string(nDimExplodedLines) + " LINE(s) from dimensions, " +
                std::to_string(nHatchOut) + " HATCH(es).");

  // REQ-068 / ADR-028 (f): surfaces have no lossless DXF representation and are not written. The
  // exclusion is NAMED rather than left to be discovered — a drawing that quietly lost its surface
  // on export and a drawing that never had one look identical in the resulting file (REQ-201). Same
  // treatment REQ-063 meshes get.
  if (!st.cadSurfaces.empty()) {
    std::string names;
    for (size_t i = 0; i < st.cadSurfaces.size(); ++i)
      names += (i ? ", " : "") + st.cadSurfaces[i].name;
    log.push_back("DXF export — excluded " + std::to_string(st.cadSurfaces.size()) +
                  " TIN surface(s) (no DXF representation): " + names +
                  ". Extract contours first if they are needed in the DXF.");
  }

  // REQ-313 / ADR-045 (i): solids are excluded, and the exclusion is NAMED and COUNTED. A real
  // solid in DXF is an ACIS 3DSOLID — a proprietary binary B-rep GoSurvey cannot write without a
  // third-party kernel REQ-300 does not permit — and a tessellated approximation was considered and
  // rejected: it hands the user a picture of their solid that round-trips back as an uneditable bag
  // of triangles with an approximate volume. Same treatment, and the same reason, as the meshes and
  // surfaces above: a drawing that quietly lost its solids on export and one that never had any
  // look identical in the resulting file (REQ-201).
  if (!st.cadSolids.empty()) {
    log.push_back("DXF export — skipped " + std::to_string(st.cadSolids.size()) +
                  " solid(s): DXF has no lossless representation for a B-rep solid (ADR-045).");
  }
  return true;
}

} // namespace

bool ImportDxfFile(AppCommandState& st, const char* pathUtf8, std::vector<std::string>& log) {
  return ImportDxfFile_Impl(st, pathUtf8, log);
}

bool ExportDxfFile(const AppCommandState& st, const char* pathUtf8, std::vector<std::string>& log) {
  return ExportDxfFile_Impl(st, pathUtf8, log);
}
