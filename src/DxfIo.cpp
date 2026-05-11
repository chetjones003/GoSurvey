#include "DxfIo.hpp"

#include "CadCommands.hpp"
#include "DxfColors.hpp"

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
      if (t[k].code == 2 && EqCiNorm(t[k].value, "*MODEL_SPACE")) {
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

bool IsModelSpaceBlockName(const std::string& raw) {
  return EqCiNorm(Trim(raw), "*MODEL_SPACE");
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

inline void UpdateCoordMag(double* mag, double x, double y) {
  const double m = std::fabs(x) + std::fabs(y);
  if (m > *mag)
    *mag = m;
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
                       int* skippedUnknown, std::unordered_map<std::string, int>* skipCounts) {

  constexpr int kMaxInsertDepth = 64;

  auto collectEntityRange = [&](size_t startIdx, size_t* endIdxOut) {
    size_t jj = startIdx + 1;
    while (jj < entEnd && !(t[jj].code == 0))
      ++jj;
    *endIdxOut = jj;
  };

  auto appendSegXF = [&](double x0, double y0, double x1, double y1, const EntityAttributes& at) {
    double ox0 = 0, oy0 = 0, ox1 = 0, oy1 = 0;
    xf.apply(x0, y0, &ox0, &oy0);
    xf.apply(x1, y1, &ox1, &oy1);
    UpdateCoordMag(coordMagMax, ox0, oy0);
    UpdateCoordMag(coordMagMax, ox1, oy1);
    st.userLinesFlat.push_back(static_cast<float>(ox0));
    st.userLinesFlat.push_back(static_cast<float>(oy0));
    st.userLinesFlat.push_back(0.f);
    st.userLinesFlat.push_back(static_cast<float>(ox1));
    st.userLinesFlat.push_back(static_cast<float>(oy1));
    st.userLinesFlat.push_back(0.f);
    st.userLineAttrs.push_back(at);
  };

  auto appendBulgeXF = [&](double x0, double y0, double x1, double y1, double bulge, const EntityAttributes& at) {
    if (std::fabs(bulge) < 1e-12) {
      appendSegXF(x0, y0, x1, y1, at);
      return;
    }
    const double thetaMag = 4.0 * std::atan(std::fabs(bulge));
    const double dx = x1 - x0;
    const double dy = y1 - y0;
    const double chord = std::hypot(dx, dy);
    if (chord < 1e-12 || thetaMag < 1e-12) {
      appendSegXF(x0, y0, x1, y1, at);
      return;
    }
    const double R = chord / (2.0 * std::sin(thetaMag * 0.5));
    const double alpha = std::atan2(dy, dx);
    const double gamma = (kPi - thetaMag) / 2.0;
    const double phi = alpha + (bulge >= 0.0 ? gamma : -gamma);
    const double cx = x0 + R * std::cos(phi);
    const double cy = y0 + R * std::sin(phi);
    const double a0 = std::atan2(y0 - cy, x0 - cx);
    const double a1 = std::atan2(y1 - cy, x1 - cx);
    double sweep = a1 - a0;
    if (bulge >= 0.0 && sweep < 0)
      sweep += 2.0 * kPi;
    if (bulge < 0.0 && sweep > 0)
      sweep -= 2.0 * kPi;
    const int nseg = std::clamp(static_cast<int>(std::ceil(std::fabs(sweep) / (kPi / 24))), 4, 96);
    for (int s = 0; s < nseg; ++s) {
      const double u0 = a0 + sweep * (static_cast<double>(s) / static_cast<double>(nseg));
      const double u1 = a0 + sweep * (static_cast<double>(s + 1) / static_cast<double>(nseg));
      appendSegXF(cx + R * std::cos(u0), cy + R * std::sin(u0), cx + R * std::cos(u1), cy + R * std::sin(u1), at);
    }
  };

  auto appendEllipseXF = [&](double cx, double cy, double majx, double majy, double ratio, double t0, double t1,
                             const EntityAttributes& at) {
    const double a = std::hypot(majx, majy);
    if (a < 1e-12)
      return;
    const double b = ratio * a;
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

  auto appendCircleXF = [&](double cx, double cy, double rad, const EntityAttributes& at) {
    if (rad <= 1e-9)
      return;
    if (xf.isIdentity()) {
      double ocx = 0, ocy = 0;
      xf.apply(cx, cy, &ocx, &ocy);
      UpdateCoordMag(coordMagMax, ocx, ocy);
      UpdateCoordMag(coordMagMax, ocx + rad, ocy);
      st.userCirclesCxCyR.push_back(static_cast<float>(ocx));
      st.userCirclesCxCyR.push_back(static_cast<float>(ocy));
      st.userCirclesCxCyR.push_back(static_cast<float>(rad));
      st.userCircleAttrs.push_back(at);
      return;
    }
    constexpr int nseg = 64;
    for (int s = 0; s < nseg; ++s) {
      const double u0 = (kPi * 2.0) * (static_cast<double>(s) / static_cast<double>(nseg));
      const double u1 = (kPi * 2.0) * (static_cast<double>(s + 1) / static_cast<double>(nseg));
      const double lx0 = cx + rad * std::cos(u0);
      const double ly0 = cy + rad * std::sin(u0);
      const double lx1 = cx + rad * std::cos(u1);
      const double ly1 = cy + rad * std::sin(u1);
      appendSegXF(lx0, ly0, lx1, ly1, at);
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

  struct PendingLw {
    std::string layer = "0";
    int c62 = 256;
    bool has420 = false;
    int rgb420 = 0;
    int flags = 0;
    std::vector<double> vx;
    std::vector<double> vy;
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
      std::string layer = "0";
      int c62 = 256;
      bool has420 = false;
      int rgb420 = 0;
      int flags70 = 0;
      size_t k = i + 1;
      for (; k < entEnd && t[k].code != 0; ++k) {
        const int c = t[k].code;
        const std::string& v = t[k].value;
        if (c == 8)
          layer = v;
        else if (c == 62)
          ParseIntFlexible(v, &c62);
        else if (c == 420)
          has420 = ParseIntFlexible(v, &rgb420);
        else if (c == 70)
          ParseIntFlexible(v, &flags70);
      }

      struct PolyVtx {
        double x = 0, y = 0, bulge = 0;
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

      EntityAttributes at{};
      at.layer = layer;
      at.color = EntityColorStorage(c62, has420, rgb420, layer, layerRgb);

      const int nv = static_cast<int>(verts.size());
      if (nv >= 2) {
        for (int vi = 0; vi < nv - 1; ++vi)
          appendBulgeXF(verts[static_cast<size_t>(vi)].x, verts[static_cast<size_t>(vi)].y,
                        verts[static_cast<size_t>(vi + 1)].x, verts[static_cast<size_t>(vi + 1)].y,
                        verts[static_cast<size_t>(vi)].bulge, at);
        if ((flags70 & 1) != 0)
          appendBulgeXF(verts[static_cast<size_t>(nv - 1)].x, verts[static_cast<size_t>(nv - 1)].y, verts[0].x, verts[0].y,
                        verts[static_cast<size_t>(nv - 1)].bulge, at);
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
      std::string layer = "0";
      int c62 = 256;
      bool has420 = false;
      int rgb420 = 0;
      double x0 = 0, y0 = 0, z0 = 0, x1 = 0, y1 = 0, z1 = 0;
      for (size_t k = i + 1; k < j; ++k) {
        const int c = t[k].code;
        const std::string& v = t[k].value;
        if (c == 8)
          layer = v;
        else if (c == 62)
          ParseIntFlexible(v, &c62);
        else if (c == 420)
          has420 = ParseIntFlexible(v, &rgb420);
        else if (c == 10)
          ParseDouble(v, &x0);
        else if (c == 20)
          ParseDouble(v, &y0);
        else if (c == 30)
          ParseDouble(v, &z0);
        else if (c == 11)
          ParseDouble(v, &x1);
        else if (c == 21)
          ParseDouble(v, &y1);
        else if (c == 31)
          ParseDouble(v, &z1);
      }
      EntityAttributes at{};
      at.layer = layer;
      at.color = EntityColorStorage(c62, has420, rgb420, layer, layerRgb);
      appendSegXF(x0, y0, x1, y1, at);
      i = j;
      continue;
    }

    if (typ == "LWPOLYLINE") {
      PendingLw lw;
      double pendX = NAN;
      for (size_t k = i + 1; k < j; ++k) {
        const int c = t[k].code;
        const std::string& v = t[k].value;
        if (c == 8)
          lw.layer = v;
        else if (c == 62)
          ParseIntFlexible(v, &lw.c62);
        else if (c == 420)
          lw.has420 = ParseIntFlexible(v, &lw.rgb420);
        else if (c == 70)
          ParseIntFlexible(v, &lw.flags);
        else if (c == 10) {
          ParseDouble(v, &pendX);
        } else if (c == 20) {
          double yv = 0;
          if (ParseDouble(v, &yv) && std::isfinite(pendX)) {
            lw.vx.push_back(pendX);
            lw.vy.push_back(yv);
            pendX = NAN;
          }
        }
      }
      EntityAttributes at{};
      at.layer = lw.layer;
      at.color = EntityColorStorage(lw.c62, lw.has420, lw.rgb420, lw.layer, layerRgb);
      const int nv = static_cast<int>(lw.vx.size());
      if (nv >= 2) {
        for (int a = 0; a < nv - 1; ++a)
          appendSegXF(lw.vx[static_cast<size_t>(a)], lw.vy[static_cast<size_t>(a)], lw.vx[static_cast<size_t>(a + 1)],
                      lw.vy[static_cast<size_t>(a + 1)], at);
        if ((lw.flags & 1) != 0 && nv >= 3)
          appendSegXF(lw.vx[static_cast<size_t>(nv - 1)], lw.vy[static_cast<size_t>(nv - 1)], lw.vx[0], lw.vy[0], at);
      }
      i = j;
      continue;
    }

    if (typ == "CIRCLE") {
      std::string layer = "0";
      int c62 = 256;
      bool has420 = false;
      int rgb420 = 0;
      double cx = 0, cy = 0, cz = 0, rad = 0;
      for (size_t k = i + 1; k < j; ++k) {
        const int c = t[k].code;
        const std::string& v = t[k].value;
        if (c == 8)
          layer = v;
        else if (c == 62)
          ParseIntFlexible(v, &c62);
        else if (c == 420)
          has420 = ParseIntFlexible(v, &rgb420);
        else if (c == 10)
          ParseDouble(v, &cx);
        else if (c == 20)
          ParseDouble(v, &cy);
        else if (c == 30)
          ParseDouble(v, &cz);
        else if (c == 40)
          ParseDouble(v, &rad);
      }
      EntityAttributes at{};
      at.layer = layer;
      at.color = EntityColorStorage(c62, has420, rgb420, layer, layerRgb);
      appendCircleXF(cx, cy, rad, at);
      i = j;
      continue;
    }

    if (typ == "ARC") {
      std::string layer = "0";
      int c62 = 256;
      bool has420 = false;
      int rgb420 = 0;
      double cx = 0, cy = 0, cz = 0, rad = 0, a0deg = 0, a1deg = 0;
      for (size_t k = i + 1; k < j; ++k) {
        const int c = t[k].code;
        const std::string& v = t[k].value;
        if (c == 8)
          layer = v;
        else if (c == 62)
          ParseIntFlexible(v, &c62);
        else if (c == 420)
          has420 = ParseIntFlexible(v, &rgb420);
        else if (c == 10)
          ParseDouble(v, &cx);
        else if (c == 20)
          ParseDouble(v, &cy);
        else if (c == 30)
          ParseDouble(v, &cz);
        else if (c == 40)
          ParseDouble(v, &rad);
        else if (c == 50)
          ParseDouble(v, &a0deg);
        else if (c == 51)
          ParseDouble(v, &a1deg);
      }
      EntityAttributes at{};
      at.layer = layer;
      at.color = EntityColorStorage(c62, has420, rgb420, layer, layerRgb);
      if (rad > 1e-9) {
        double sweep = a1deg - a0deg;
        if (std::fabs(sweep) < 1e-9)
          sweep = 360.0;
        while (sweep < 0)
          sweep += 360.0;
        while (sweep > 360.0)
          sweep -= 360.0;
        if (sweep < 1e-9)
          sweep = 360.0;
        const double a0 = a0deg * kDegToRad;
        const double sw = sweep * kDegToRad;
        constexpr int nseg = 48;
        for (int s = 0; s < nseg; ++s) {
          const double u0 = a0 + sw * (static_cast<double>(s) / static_cast<double>(nseg));
          const double u1 = a0 + sw * (static_cast<double>(s + 1) / static_cast<double>(nseg));
          const double lx0 = cx + rad * std::cos(u0);
          const double ly0 = cy + rad * std::sin(u0);
          const double lx1 = cx + rad * std::cos(u1);
          const double ly1 = cy + rad * std::sin(u1);
          appendSegXF(lx0, ly0, lx1, ly1, at);
        }
      }
      i = j;
      continue;
    }

    if (typ == "ELLIPSE") {
      std::string layer = "0";
      int c62 = 256;
      bool has420 = false;
      int rgb420 = 0;
      double cx = 0, cy = 0, cz = 0;
      double mx = 1, my = 0, mz = 0;
      double ratio = 0.5;
      double t0 = 0, t1 = 2.0 * kPi;
      for (size_t k = i + 1; k < j; ++k) {
        const int c = t[k].code;
        const std::string& v = t[k].value;
        if (c == 8)
          layer = v;
        else if (c == 62)
          ParseIntFlexible(v, &c62);
        else if (c == 420)
          has420 = ParseIntFlexible(v, &rgb420);
        else if (c == 10)
          ParseDouble(v, &cx);
        else if (c == 20)
          ParseDouble(v, &cy);
        else if (c == 30)
          ParseDouble(v, &cz);
        else if (c == 11)
          ParseDouble(v, &mx);
        else if (c == 21)
          ParseDouble(v, &my);
        else if (c == 31)
          ParseDouble(v, &mz);
        else if (c == 40)
          ParseDouble(v, &ratio);
        else if (c == 41)
          ParseDouble(v, &t0);
        else if (c == 42)
          ParseDouble(v, &t1);
      }
      (void)cz;
      (void)mz;
      EntityAttributes at{};
      at.layer = layer;
      at.color = EntityColorStorage(c62, has420, rgb420, layer, layerRgb);
      if (ratio <= 1e-12)
        ratio = 1e-12;
      appendEllipseXF(cx, cy, mx, my, ratio, t0, t1, at);
      i = j;
      continue;
    }

    if (typ == "POINT") {
      std::string layer = "0";
      int c62 = 256;
      bool has420 = false;
      int rgb420 = 0;
      double px = 0, py = 0;
      for (size_t k = i + 1; k < j; ++k) {
        const int c = t[k].code;
        const std::string& v = t[k].value;
        if (c == 8)
          layer = v;
        else if (c == 62)
          ParseIntFlexible(v, &c62);
        else if (c == 420)
          has420 = ParseIntFlexible(v, &rgb420);
        else if (c == 10)
          ParseDouble(v, &px);
        else if (c == 20)
          ParseDouble(v, &py);
      }
      EntityAttributes at{};
      at.layer = layer;
      at.color = EntityColorStorage(c62, has420, rgb420, layer, layerRgb);
      double arm = (*coordMagMax > 1.0) ? (*coordMagMax * 1e-7) : 0.01;
      arm = std::clamp(arm, 1e-6, std::max(*coordMagMax * 1e-6, 0.5));
      appendSegXF(px - arm, py, px + arm, py, at);
      appendSegXF(px, py - arm, px, py + arm, at);
      i = j;
      continue;
    }

    if (typ == "TEXT") {
      std::string layer = "0";
      int c62 = 256;
      bool has420 = false;
      int rgb420 = 0;
      double x = 0, y = 0, h = 2.5, rot = 0;
      std::string txt;
      for (size_t k = i + 1; k < j; ++k) {
        const int c = t[k].code;
        const std::string& v = t[k].value;
        if (c == 8)
          layer = v;
        else if (c == 62)
          ParseIntFlexible(v, &c62);
        else if (c == 420)
          has420 = ParseIntFlexible(v, &rgb420);
        else if (c == 10)
          ParseDouble(v, &x);
        else if (c == 20)
          ParseDouble(v, &y);
        else if (c == 40)
          ParseDouble(v, &h);
        else if (c == 50)
          ParseDouble(v, &rot);
        else if (c == 1)
          txt = v;
      }
      EntityAttributes at{};
      at.layer = layer;
      at.color = EntityColorStorage(c62, has420, rgb420, layer, layerRgb);
      const double rad = rot * kDegToRad;
      const double textLen = std::max(static_cast<double>(txt.size()), 1.0);
      const double w = std::max(h * 0.65 * textLen, h * 2.0);
      const double x1 = x + w * std::cos(rad);
      const double y1 = y + w * std::sin(rad);
      appendSegXF(x, y, x1, y1, at);
      i = j;
      continue;
    }

    if (typ == "MTEXT") {
      std::string layer = "0";
      int c62 = 256;
      bool has420 = false;
      int rgb420 = 0;
      double ix = 0, iy = 0, h = 3, boxW = 120, rot = 0;
      std::string txt;
      for (size_t k = i + 1; k < j; ++k) {
        const int c = t[k].code;
        const std::string& v = t[k].value;
        if (c == 8)
          layer = v;
        else if (c == 62)
          ParseIntFlexible(v, &c62);
        else if (c == 420)
          has420 = ParseIntFlexible(v, &rgb420);
        else if (c == 10)
          ParseDouble(v, &ix);
        else if (c == 20)
          ParseDouble(v, &iy);
        else if (c == 40)
          ParseDouble(v, &h);
        else if (c == 41)
          ParseDouble(v, &boxW);
        else if (c == 50)
          ParseDouble(v, &rot);
        else if (c == 1 || c == 3)
          txt += v;
      }
      EntityAttributes at{};
      at.layer = layer;
      at.color = EntityColorStorage(c62, has420, rgb420, layer, layerRgb);
      const double rad = rot * kDegToRad;
      const double cr = std::cos(rad);
      const double sr = std::sin(rad);
      const double hw = std::max(boxW, h * 0.55 * std::max(static_cast<double>(txt.size()) * 0.08 + 4.0, 8.0));
      const double hh = std::max(h * 1.25, h * (1.0 + static_cast<double>(txt.size()) / 48.0));
      auto corner = [&](double lx, double ly, double* ox, double* oy) {
        *ox = ix + cr * lx - sr * ly;
        *oy = iy + sr * lx + cr * ly;
      };
      double x0 = 0, y0 = 0, x1 = 0, y1 = 0, x2 = 0, y2 = 0, x3 = 0, y3 = 0;
      corner(0, 0, &x0, &y0);
      corner(hw, 0, &x1, &y1);
      corner(hw, hh, &x2, &y2);
      corner(0, hh, &x3, &y3);
      appendSegXF(x0, y0, x1, y1, at);
      appendSegXF(x1, y1, x2, y2, at);
      appendSegXF(x2, y2, x3, y3, at);
      appendSegXF(x3, y3, x0, y0, at);
      i = j;
      continue;
    }

    if (EqCiNorm(typ, "DIMENSION")) {
      std::string layer = "0";
      int c62 = 256;
      bool has420 = false;
      int rgb420 = 0;
      double px10 = NAN, py10 = NAN, px13 = NAN, py13 = NAN, px14 = NAN, py14 = NAN, px15 = NAN, py15 = NAN,
             px16 = NAN, py16 = NAN;
      for (size_t k = i + 1; k < j; ++k) {
        const int c = t[k].code;
        const std::string& v = t[k].value;
        if (c == 8)
          layer = v;
        else if (c == 62)
          ParseIntFlexible(v, &c62);
        else if (c == 420)
          has420 = ParseIntFlexible(v, &rgb420);
        else if (c == 10)
          ParseDouble(v, &px10);
        else if (c == 20)
          ParseDouble(v, &py10);
        else if (c == 13)
          ParseDouble(v, &px13);
        else if (c == 23)
          ParseDouble(v, &py13);
        else if (c == 14)
          ParseDouble(v, &px14);
        else if (c == 24)
          ParseDouble(v, &py14);
        else if (c == 15)
          ParseDouble(v, &px15);
        else if (c == 25)
          ParseDouble(v, &py15);
        else if (c == 16)
          ParseDouble(v, &px16);
        else if (c == 26)
          ParseDouble(v, &py16);
      }
      EntityAttributes at{};
      at.layer = layer;
      at.color = EntityColorStorage(c62, has420, rgb420, layer, layerRgb);
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
      std::string layer = "0";
      int c62 = 256;
      bool has420 = false;
      int rgb420 = 0;
      double minx = 1e300, maxx = -1e300, miny = 1e300, maxy = -1e300;
      bool anyPt = false;
      double pendX = 0;
      bool haveX = false;
      for (size_t k = i + 1; k < j; ++k) {
        const int c = t[k].code;
        const std::string& v = t[k].value;
        if (c == 8)
          layer = v;
        else if (c == 62)
          ParseIntFlexible(v, &c62);
        else if (c == 420)
          has420 = ParseIntFlexible(v, &rgb420);
        else if (c == 10 || c == 11) {
          ParseDouble(v, &pendX);
          haveX = true;
        } else if ((c == 20 || c == 21) && haveX) {
          double yv = 0;
          if (ParseDouble(v, &yv)) {
            minx = std::min(minx, pendX);
            maxx = std::max(maxx, pendX);
            miny = std::min(miny, yv);
            maxy = std::max(maxy, yv);
            anyPt = true;
          }
          haveX = false;
        }
      }
      EntityAttributes at{};
      at.layer = layer;
      at.color = EntityColorStorage(c62, has420, rgb420, layer, layerRgb);
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
      std::string layer = "0";
      int c62 = 256;
      bool has420 = false;
      int rgb420 = 0;
      for (size_t k = i + 1; k < j; ++k) {
        const int c = t[k].code;
        const std::string& v = t[k].value;
        if (c == 8)
          layer = v;
        else if (c == 62)
          ParseIntFlexible(v, &c62);
        else if (c == 420)
          has420 = ParseIntFlexible(v, &rgb420);
      }
      EntityAttributes at{};
      at.layer = layer;
      at.color = EntityColorStorage(c62, has420, rgb420, layer, layerRgb);
      (void)hatchEmit(i + 1, j, at);
      i = j;
      continue;
    }

    if (typ == "INSERT") {
      std::string layer = "0";
      int c62 = 256;
      bool has420 = false;
      int rgb420 = 0;
      std::string blk;
      double ix = 0, iy = 0, sx = 1, sy = 1, rot = 0;
      bool has41 = false, has42 = false;
      for (size_t k = i + 1; k < j; ++k) {
        const int c = t[k].code;
        const std::string& v = t[k].value;
        if (c == 8)
          layer = v;
        else if (c == 62)
          ParseIntFlexible(v, &c62);
        else if (c == 420)
          has420 = ParseIntFlexible(v, &rgb420);
        else if (c == 2)
          blk = Trim(v);
        else if (c == 10)
          ParseDouble(v, &ix);
        else if (c == 20)
          ParseDouble(v, &iy);
        else if (c == 41) {
          ParseDouble(v, &sx);
          has41 = true;
        } else if (c == 42) {
          ParseDouble(v, &sy);
          has42 = true;
        } else if (c == 50)
          ParseDouble(v, &rot);
      }
      if (!has41)
        sx = 1;
      if (!has42)
        sy = 1;
      EntityAttributes at{};
      at.layer = layer;
      at.color = EntityColorStorage(c62, has420, rgb420, layer, layerRgb);

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
                        skippedViewport, skippedUnknown, skipCounts);
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
  const size_t nCirc = st.userCirclesCxCyR.size() / 3;
  for (size_t i = 0; i < nCirc && i < st.userCircleAttrs.size(); ++i)
    layers.insert(st.userCircleAttrs[i].layer.empty() ? std::string("0") : st.userCircleAttrs[i].layer);
  for (size_t i = 0; i < st.cadAnnotationAttrs.size(); ++i) {
    const EntityAttributes& at = st.cadAnnotationAttrs[i];
    layers.insert(at.layer.empty() ? std::string("0") : at.layer);
  }
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
}

} // namespace

bool ImportDxfFile(AppCommandState& st, const char* pathUtf8, std::vector<std::string>& log) {
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

  size_t eb = 0, ee = 0;
  const bool hasEntitiesSec = FindSectionBounds(pairs, "ENTITIES", &eb, &ee);
  size_t mb = 0, me = 0;
  const bool hasModelSpace = FindModelSpaceEntityRange(pairs, &mb, &me);

  if (!hasEntitiesSec && !hasModelSpace) {
    log.push_back(
        "DXF import — no ENTITIES section and no *MODEL_SPACE block found. Save as ASCII DXF (not Binary) from AutoCAD.");
    return false;
  }

  ResetCadToolStateToIdle(st);
  ClearCadGeometry(st);

  double coordMagMax = 0;
  int skippedPaper = 0;
  int skippedViewport = 0;
  int skipped = 0;
  std::unordered_map<std::string, int> skipHist;
  const Affine2D xfRoot{};
  if (hasEntitiesSec)
    ParseEntityRegion(pairs, eb, ee, st, layerRgb, &blockDefs, xfRoot, 0, &coordMagMax, &skippedPaper,
                      &skippedViewport, &skipped, &skipHist);

  const bool noGeom = st.userLinesFlat.empty() && st.userCirclesCxCyR.empty();
  if ((!hasEntitiesSec || noGeom) && hasModelSpace) {
    if (!hasEntitiesSec)
      log.push_back("DXF import — ENTITIES section missing; reading geometry from *MODEL_SPACE block.");
    else if (noGeom)
      log.push_back("DXF import — ENTITIES empty after model-space filter; reading geometry from *MODEL_SPACE block.");
    ParseEntityRegion(pairs, mb, me, st, layerRgb, &blockDefs, xfRoot, 0, &coordMagMax, &skippedPaper,
                      &skippedViewport, &skipped, &skipHist);
  }

  const size_t nLines = st.userLinesFlat.size() / 6;
  const size_t nCirc = st.userCirclesCxCyR.size() / 3;
  std::ostringstream os;
  os << "DXF import — " << nLines << " line segment(s), " << nCirc << " circle(s).";
  log.push_back(os.str());
  if (skippedPaper > 0)
    log.push_back("DXF import — skipped " + std::to_string(skippedPaper) +
                  " paper-space-only ENTITIES (group 67); layouts/title blocks not imported.");
  if (skippedViewport > 0)
    log.push_back("DXF import — skipped " + std::to_string(skippedViewport) + " VIEWPORT record(s).");
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
  BumpCadGpuCache(st);
  return true;
}

bool ExportDxfFile(const AppCommandState& st, const char* pathUtf8, std::vector<std::string>& log) {
  if (!pathUtf8 || pathUtf8[0] == '\0') {
    log.push_back("DXF export — no path.");
    return false;
  }
  std::unordered_map<std::string, uint32_t> layerRgbHint;
  BuildExportLayerRgbHint(st, &layerRgbHint);

  std::unordered_set<std::string> layerNames;
  for (size_t i = 0; i < st.userLineAttrs.size(); ++i)
    layerNames.insert(st.userLineAttrs[i].layer);
  for (size_t i = 0; i < st.userCircleAttrs.size(); ++i)
    layerNames.insert(st.userCircleAttrs[i].layer);
  for (size_t i = 0; i < st.cadAnnotationAttrs.size(); ++i)
    layerNames.insert(st.cadAnnotationAttrs[i].layer.empty() ? std::string("0") : st.cadAnnotationAttrs[i].layer);
  layerNames.insert("0");

  std::ofstream out(std::filesystem::path(pathUtf8), std::ios::binary);
  if (!out) {
    log.push_back("DXF export — could not open file for writing.");
    return false;
  }
  out << std::setprecision(16);

  auto emitPair = [&](int code, const std::string& val) {
    out << code << "\r\n" << val << "\r\n";
  };

  emitPair(0, "SECTION");
  emitPair(2, "HEADER");
  emitPair(9, "$ACADVER");
  emitPair(1, "AC1018");
  emitPair(9, "$INSBASE");
  emitPair(10, "0.0");
  emitPair(20, "0.0");
  emitPair(30, "0.0");
  emitPair(0, "ENDSEC");

  emitPair(0, "SECTION");
  emitPair(2, "TABLES");
  emitPair(0, "TABLE");
  emitPair(2, "LAYER");
  emitPair(5, "2");
  emitPair(100, "AcDbSymbolTable");
  emitPair(70, std::to_string(static_cast<int>(layerNames.size())));

  uint32_t handle = 0x10;
  for (const std::string& lyr : layerNames) {
    char hbuf[16];
    std::snprintf(hbuf, sizeof(hbuf), "%X", handle++);
    uint32_t lr = DxfRgbPackedFromAci(7);
    auto it = layerRgbHint.find(lyr);
    if (it != layerRgbHint.end())
      lr = it->second & 0xFFFFFFu;
    const int lac = DxfNearestAciFromRgbPacked(lr);

    emitPair(0, "LAYER");
    emitPair(5, hbuf);
    emitPair(100, "AcDbSymbolTableRecord");
    emitPair(100, "AcDbLayerTableRecord");
    emitPair(2, lyr);
    emitPair(70, "0");
    emitPair(62, std::to_string(lac));
    emitPair(6, "Continuous");
    emitPair(370, "-3");
  }

  emitPair(0, "ENDTAB");
  emitPair(0, "ENDSEC");

  emitPair(0, "SECTION");
  emitPair(2, "BLOCKS");
  emitPair(0, "ENDSEC");

  emitPair(0, "SECTION");
  emitPair(2, "ENTITIES");

  const size_t nSeg = st.userLinesFlat.size() / 6;
  handle = 0x1000;
  for (size_t i = 0; i < nSeg; ++i) {
    char hb[16];
    std::snprintf(hb, sizeof(hb), "%X", handle++);
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

    emitPair(0, "LINE");
    emitPair(5, hb);
    emitPair(100, "AcDbEntity");
    emitPair(8, at.layer.empty() ? std::string("0") : at.layer);
    emitPair(62, "256");
    emitPair(420, std::to_string(static_cast<int>(rgb)));
    emitPair(100, "AcDbLine");
    emitPair(10, std::to_string(static_cast<double>(x0)));
    emitPair(20, std::to_string(static_cast<double>(y0)));
    emitPair(30, std::to_string(static_cast<double>(z0)));
    emitPair(11, std::to_string(static_cast<double>(x1)));
    emitPair(21, std::to_string(static_cast<double>(y1)));
    emitPair(31, std::to_string(static_cast<double>(z1)));
  }

  const size_t nCirc = st.userCirclesCxCyR.size() / 3;
  for (size_t ci = 0; ci < nCirc; ++ci) {
    char hb[16];
    std::snprintf(hb, sizeof(hb), "%X", handle++);
    EntityAttributes at{};
    if (ci < st.userCircleAttrs.size())
      at = st.userCircleAttrs[ci];
    const uint32_t rgb =
        AttrResolvedRgbPacked(at, layerRgbHint) & 0xFFFFFFu;
    const float cx = st.userCirclesCxCyR[ci * 3];
    const float cy = st.userCirclesCxCyR[ci * 3 + 1];
    const float rr = st.userCirclesCxCyR[ci * 3 + 2];

    emitPair(0, "CIRCLE");
    emitPair(5, hb);
    emitPair(100, "AcDbEntity");
    emitPair(8, at.layer.empty() ? std::string("0") : at.layer);
    emitPair(62, "256");
    emitPair(420, std::to_string(static_cast<int>(rgb)));
    emitPair(100, "AcDbCircle");
    emitPair(10, std::to_string(static_cast<double>(cx)));
    emitPair(20, std::to_string(static_cast<double>(cy)));
    emitPair(30, "0.0");
    emitPair(40, std::to_string(static_cast<double>(rr)));
  }

  auto sanitizeDxfText = [](std::string t) {
    for (char& c : t) {
      if (c == '\r' || c == '\n')
        c = ' ';
    }
    return t;
  };

  constexpr double kRadToDeg = 180.0 / 3.14159265358979323846;
  size_t nTextOut = 0;
  size_t nMtextOut = 0;
  for (size_t ai = 0; ai < st.cadAnnotations.size(); ++ai) {
    const CadAnnotation& an = st.cadAnnotations[ai];
    EntityAttributes at{};
    if (ai < st.cadAnnotationAttrs.size())
      at = st.cadAnnotationAttrs[ai];
    const uint32_t rgb =
        AttrResolvedRgbPacked(at, layerRgbHint) & 0xFFFFFFu;
    const std::string layer = at.layer.empty() ? std::string("0") : at.layer;

    if (an.kind == CadAnnotation::Kind::Text) {
      char hb[16];
      std::snprintf(hb, sizeof(hb), "%X", handle++);
      const std::string txt = sanitizeDxfText(an.text);
      const double rotDeg = static_cast<double>(an.rotationRad) * kRadToDeg;
      emitPair(0, "TEXT");
      emitPair(5, hb);
      emitPair(100, "AcDbEntity");
      emitPair(8, layer);
      emitPair(62, "256");
      emitPair(420, std::to_string(static_cast<int>(rgb)));
      emitPair(100, "AcDbText");
      emitPair(10, std::to_string(static_cast<double>(an.insX)));
      emitPair(20, std::to_string(static_cast<double>(an.insY)));
      emitPair(30, "0.0");
      emitPair(40, std::to_string(static_cast<double>(CadAnnotationHeightWorld(an, st.modelUnitsPerPlottedInch))));
      emitPair(50, std::to_string(rotDeg));
      emitPair(1, txt);
      ++nTextOut;
    } else {
      char hb[16];
      std::snprintf(hb, sizeof(hb), "%X", handle++);
      const std::string txt = sanitizeDxfText(an.text);
      const float bw =
          std::max(1.f, std::fabs(an.boxMaxX - an.boxMinX));
      emitPair(0, "MTEXT");
      emitPair(5, hb);
      emitPair(100, "AcDbEntity");
      emitPair(8, layer);
      emitPair(62, "256");
      emitPair(420, std::to_string(static_cast<int>(rgb)));
      emitPair(100, "AcDbMText");
      emitPair(10, std::to_string(static_cast<double>(an.insX)));
      emitPair(20, std::to_string(static_cast<double>(an.insY)));
      emitPair(30, "0.0");
      emitPair(40, std::to_string(static_cast<double>(CadAnnotationHeightWorld(an, st.modelUnitsPerPlottedInch))));
      emitPair(41, std::to_string(static_cast<double>(bw)));
      emitPair(50, "0.0");
      emitPair(71, "1");
      emitPair(1, txt);
      ++nMtextOut;
    }
  }

  emitPair(0, "ENDSEC");
  emitPair(0, "EOF");

  log.push_back("DXF export — wrote " + std::to_string(nSeg) + " LINE(s), " + std::to_string(nCirc) + " CIRCLE(s), " +
                std::to_string(nTextOut) + " TEXT, " + std::to_string(nMtextOut) + " MTEXT.");
  return true;
}
