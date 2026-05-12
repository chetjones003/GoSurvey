#include "CadLinetype.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>

namespace {

bool EqCiAscii(const std::string& a, const char* b) {
  size_t i = 0;
  for (; i < a.size() && b[i]; ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
      return false;
  }
  return i == a.size() && b[i] == '\0';
}

std::string TrimAscii(const std::string& s) {
  size_t a = 0;
  while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a])))
    ++a;
  size_t b = s.size();
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
    --b;
  return s.substr(a, b - a);
}

// Pattern elements: positive = solid length, negative = gap, in abstract units (scaled to world outside).
struct Pattern {
  const char* nameCi;
  const double* elems;
  int count;
};

// DASHED ~ 0.5 on, 0.25 off (scaled); CENTER long-dash short-dash; HIDDEN dense; PHANTOM double dash.
const double kPatContinuous[] = {1.0};
const double kPatDashed[] = {0.5, -0.25};
const double kPatHidden[] = {0.25, -0.125};
const double kPatCenter[] = {1.25, -0.25, 0.25, -0.25};
const double kPatPhantom[] = {1.25, -0.25, 0.25, -0.25, 0.25, -0.25};
const double kPatDivide[] = {0.5, -0.125, 0.125, -0.125};
const double kPatBorder[] = {0.5, -0.25, 0.5, -0.25, 0.0, -0.25};

const Pattern kPatterns[] = {
    {"continuous", kPatContinuous, 1}, {"bylayer", kPatContinuous, 1}, {"byblock", kPatContinuous, 1},
    {"dashed", kPatDashed, 2},         {"hidden", kPatHidden, 2},      {"center", kPatCenter, 4},
    {"centre", kPatCenter, 4},        {"phantom", kPatPhantom, 6},   {"divide", kPatDivide, 4},
    {"border", kPatBorder, 6},
};

const Pattern* FindPatternCi(const std::string& name) {
  for (const auto& p : kPatterns) {
    if (EqCiAscii(name, p.nameCi))
      return &p;
  }
  return nullptr;
}

void EmitSolidSegVc(float x0, float y0, float z0, float x1, float y1, float z1, const float rgba[4],
                     std::vector<float>* o) {
  o->push_back(x0);
  o->push_back(y0);
  o->push_back(z0);
  o->push_back(rgba[0]);
  o->push_back(rgba[1]);
  o->push_back(rgba[2]);
  o->push_back(rgba[3]);
  o->push_back(x1);
  o->push_back(y1);
  o->push_back(z1);
  o->push_back(rgba[0]);
  o->push_back(rgba[1]);
  o->push_back(rgba[2]);
  o->push_back(rgba[3]);
}

} // namespace

bool CadLinetypeNameEqCi(const std::string& a, const std::string& b) {
  if (a.size() != b.size())
    return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
      return false;
  }
  return true;
}

std::string CadCanonicalLinetypeNameForDxf(const std::string& raw) {
  const std::string t = TrimAscii(raw);
  if (t.empty())
    return t;
  if (EqCiAscii(t, "ByLayer") || EqCiAscii(t, "BYLAYER"))
    return "ByLayer";
  if (EqCiAscii(t, "ByBlock") || EqCiAscii(t, "BYBLOCK"))
    return "ByBlock";
  if (EqCiAscii(t, "Continuous") || EqCiAscii(t, "CONTINUOUS"))
    return "Continuous";
  if (EqCiAscii(t, "Dashed") || EqCiAscii(t, "DASHED"))
    return "DASHED";
  if (EqCiAscii(t, "Hidden") || EqCiAscii(t, "HIDDEN"))
    return "HIDDEN";
  if (EqCiAscii(t, "Center") || EqCiAscii(t, "CENTER") || EqCiAscii(t, "Centre") || EqCiAscii(t, "CENTRE"))
    return "CENTER";
  if (EqCiAscii(t, "Phantom") || EqCiAscii(t, "PHANTOM"))
    return "PHANTOM";
  if (EqCiAscii(t, "Divide") || EqCiAscii(t, "DIVIDE") || EqCiAscii(t, "Divided"))
    return "DIVIDE";
  if (EqCiAscii(t, "Border") || EqCiAscii(t, "BORDER"))
    return "BORDER";
  return t;
}

bool CadLinetypeIsSolidCi(const std::string& name) {
  const std::string c = CadCanonicalLinetypeNameForDxf(name);
  return c.empty() || EqCiAscii(c, "Continuous") || EqCiAscii(c, "ByLayer") || EqCiAscii(c, "ByBlock");
}

void CadTessellateLinetypeSegmentVc(float x0, float y0, float z0, float x1, float y1, float z1,
                                     const std::string& linetypeName, float patternScaleWorld, const float rgba[4],
                                     std::vector<float>* outVerts7) {
  if (!outVerts7)
    return;
  const std::string canon = CadCanonicalLinetypeNameForDxf(linetypeName);
  if (CadLinetypeIsSolidCi(canon)) {
    EmitSolidSegVc(x0, y0, z0, x1, y1, z1, rgba, outVerts7);
    return;
  }
  const Pattern* pat = FindPatternCi(canon);
  if (!pat) {
    EmitSolidSegVc(x0, y0, z0, x1, y1, z1, rgba, outVerts7);
    return;
  }
  const float dx = x1 - x0;
  const float dy = y1 - y0;
  const float len = std::hypot(dx, dy);
  if (len < 1e-8f) {
    EmitSolidSegVc(x0, y0, z0, x1, y1, z1, rgba, outVerts7);
    return;
  }
  const float ux = dx / len;
  const float uy = dy / len;
  const double sc = std::max(static_cast<double>(patternScaleWorld), 1e-9);
  double cyc = 0;
  for (int i = 0; i < pat->count; ++i)
    cyc += std::fabs(pat->elems[i]) * sc;
  if (cyc < 1e-12) {
    EmitSolidSegVc(x0, y0, z0, x1, y1, z1, rgba, outVerts7);
    return;
  }

  double pos = 0;
  while (pos < static_cast<double>(len) - 1e-9) {
    const double inCyc = std::fmod(pos, cyc);
    double acc = 0;
    int ei = 0;
    double elWorld = 0;
    for (ei = 0; ei < pat->count; ++ei) {
      elWorld = std::fabs(pat->elems[ei]) * sc;
      if (inCyc < acc + elWorld - 1e-12)
        break;
      acc += elWorld;
    }
    const double into = inCyc - acc;
    const bool draw = pat->elems[ei] > 0;
    const double remainInElem = elWorld - into;
    const double remainLine = static_cast<double>(len) - pos;
    const double step = std::min(remainInElem, remainLine);
    if (step <= 1e-12)
      break;
    if (draw) {
      const float sx = x0 + ux * static_cast<float>(pos);
      const float sy = y0 + uy * static_cast<float>(pos);
      const float ex = x0 + ux * static_cast<float>(pos + step);
      const float ey = y0 + uy * static_cast<float>(pos + step);
      EmitSolidSegVc(sx, sy, z0, ex, ey, z1, rgba, outVerts7);
    }
    pos += step;
  }
}

void CadTessellateLinetypeChainVc(const float* xy, int nPts, float z, bool closed, const std::string& linetypeName,
                                  float patternScaleWorld, const float rgba[4], std::vector<float>* outVerts7) {
  if (!xy || nPts < 2 || !outVerts7)
    return;
  const std::string canon = CadCanonicalLinetypeNameForDxf(linetypeName);
  if (CadLinetypeIsSolidCi(canon)) {
    const int nSeg = closed ? nPts : nPts - 1;
    for (int i = 0; i < nSeg; ++i) {
      const int j = (i + 1) % nPts;
      EmitSolidSegVc(xy[i * 2], xy[i * 2 + 1], z, xy[j * 2], xy[j * 2 + 1], z, rgba, outVerts7);
    }
    return;
  }
  const Pattern* pat = FindPatternCi(canon);
  if (!pat) {
    const int nSeg = closed ? nPts : nPts - 1;
    for (int i = 0; i < nSeg; ++i) {
      const int j = (i + 1) % nPts;
      EmitSolidSegVc(xy[i * 2], xy[i * 2 + 1], z, xy[j * 2], xy[j * 2 + 1], z, rgba, outVerts7);
    }
    return;
  }
  const double sc = std::max(static_cast<double>(patternScaleWorld), 1e-9);
  double cyc = 0;
  for (int i = 0; i < pat->count; ++i)
    cyc += std::fabs(pat->elems[i]) * sc;
  if (cyc < 1e-12) {
    const int nSeg = closed ? nPts : nPts - 1;
    for (int i = 0; i < nSeg; ++i) {
      const int j = (i + 1) % nPts;
      EmitSolidSegVc(xy[i * 2], xy[i * 2 + 1], z, xy[j * 2], xy[j * 2 + 1], z, rgba, outVerts7);
    }
    return;
  }

  const int nSeg = closed ? nPts : nPts - 1;
  double patAcc = 0;
  for (int si = 0; si < nSeg; ++si) {
    const int j = (si + 1) % nPts;
    const float x0 = xy[si * 2];
    const float y0 = xy[si * 2 + 1];
    const float x1 = xy[j * 2];
    const float y1 = xy[j * 2 + 1];
    const float dx = x1 - x0;
    const float dy = y1 - y0;
    const float slen = std::hypot(dx, dy);
    if (slen < 1e-8f)
      continue;
    const float ux = dx / slen;
    const float uy = dy / slen;
    double u = 0;
    while (u < static_cast<double>(slen) - 1e-9) {
      const double inCyc = std::fmod(patAcc, cyc);
      double acc = 0;
      int ei = 0;
      double elWorld = 0;
      for (ei = 0; ei < pat->count; ++ei) {
        elWorld = std::fabs(pat->elems[ei]) * sc;
        if (inCyc < acc + elWorld - 1e-12)
          break;
        acc += elWorld;
      }
      const double into = inCyc - acc;
      const bool draw = pat->elems[ei] > 0;
      const double remainInElem = elWorld - into;
      const double remainLine = static_cast<double>(slen) - u;
      const double step = std::min(remainInElem, remainLine);
      if (step <= 1e-12)
        break;
      if (draw) {
        const float sx = x0 + ux * static_cast<float>(u);
        const float sy = y0 + uy * static_cast<float>(u);
        const float ex = x0 + ux * static_cast<float>(u + step);
        const float ey = y0 + uy * static_cast<float>(u + step);
        EmitSolidSegVc(sx, sy, z, ex, ey, z, rgba, outVerts7);
      }
      patAcc += step;
      u += step;
    }
  }
}

void CadTessellateLinetypePolylineVc(const float* xy, int nPts, float z, const std::string& linetypeName,
                                    float patternScaleWorld, const float rgba[4], std::vector<float>* outVerts7) {
  CadTessellateLinetypeChainVc(xy, nPts, z, false, linetypeName, patternScaleWorld, rgba, outVerts7);
}
