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

constexpr int kMaxLinetypeSegmentsPerCall = 100000;

/// Locate the active non-zero pattern element at \p inCyc within [0, \p cyc).
/// Guarantees 0 <= *outIdx < pat->count and *outElWorld > 0 so callers never index out of bounds
/// when \p inCyc lands within floating-point epsilon of \p cyc or hits a zero-length dot element.
void FindPatternElement(const Pattern* pat, double sc, double cyc, double* inCyc, int* outIdx, double* outAcc,
                        double* outElWorld) {
  double c = *inCyc;
  if (c < 0.0 || c >= cyc) {
    c = std::fmod(c, cyc);
    if (c < 0.0)
      c += cyc;
  }
  double acc = 0.0;
  for (int i = 0; i < pat->count; ++i) {
    const double w = std::fabs(pat->elems[i]) * sc;
    if (w <= 0.0)
      continue;
    if (c < acc + w - 1e-12) {
      *inCyc = c;
      *outIdx = i;
      *outAcc = acc;
      *outElWorld = w;
      return;
    }
    acc += w;
  }
  // Landed within epsilon of the cycle end; wrap cleanly to the first non-zero element.
  for (int i = 0; i < pat->count; ++i) {
    const double w = std::fabs(pat->elems[i]) * sc;
    if (w > 0.0) {
      *inCyc = 0.0;
      *outIdx = i;
      *outAcc = 0.0;
      *outElWorld = w;
      return;
    }
  }
  *inCyc = 0.0;
  *outIdx = 0;
  *outAcc = 0.0;
  *outElWorld = cyc;
}

/// Liang-Barsky clip of the 2D segment (x0,y0)->(x1,y1) against [-halfExtent, +halfExtent]^2.
/// Returns false when the segment lies completely outside the square; otherwise writes the visible
/// parameter range [*t0Out, *t1Out] subset of [0, 1].
bool ClipSegmentToOriginSquare(double x0, double y0, double x1, double y1, double halfExtent, double* t0Out,
                               double* t1Out) {
  const double dx = x1 - x0;
  const double dy = y1 - y0;
  const double p[4] = {-dx, dx, -dy, dy};
  const double q[4] = {x0 + halfExtent, halfExtent - x0, y0 + halfExtent, halfExtent - y0};
  double t0 = 0.0;
  double t1 = 1.0;
  for (int i = 0; i < 4; ++i) {
    if (p[i] == 0.0) {
      if (q[i] < 0.0)
        return false;
      continue;
    }
    const double t = q[i] / p[i];
    if (p[i] < 0.0) {
      if (t > t1)
        return false;
      t0 = std::max(t0, t);
    } else {
      if (t < t0)
        return false;
      t1 = std::min(t1, t);
    }
  }
  *t0Out = t0;
  *t1Out = t1;
  return true;
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
  if (!std::isfinite(len) || cyc < 1e-12 ||
      static_cast<double>(len) / cyc > static_cast<double>(kMaxLinetypeSegmentsPerCall)) {
    EmitSolidSegVc(x0, y0, z0, x1, y1, z1, rgba, outVerts7);
    return;
  }

  double pos = 0;
  int emitted = 0;
  while (pos < static_cast<double>(len) - 1e-9) {
    if (emitted >= kMaxLinetypeSegmentsPerCall) {
      const float sx = x0 + ux * static_cast<float>(pos);
      const float sy = y0 + uy * static_cast<float>(pos);
      EmitSolidSegVc(sx, sy, z0, x1, y1, z1, rgba, outVerts7);
      break;
    }
    double inCyc = std::fmod(pos, cyc);
    double acc = 0;
    int ei = 0;
    double elWorld = 0;
    FindPatternElement(pat, sc, cyc, &inCyc, &ei, &acc, &elWorld);
    const double into = inCyc - acc;
    const bool draw = pat->elems[ei] > 0;
    const double remainInElem = elWorld - into;
    const double remainLine = static_cast<double>(len) - pos;
    const double step = std::min(remainInElem, remainLine);
    if (step <= 1e-12)
      break;
    const double nextPos = pos + step;
    if (!(nextPos > pos))
      break;
    if (draw) {
      const float sx = x0 + ux * static_cast<float>(pos);
      const float sy = y0 + uy * static_cast<float>(pos);
      const float ex = x0 + ux * static_cast<float>(nextPos);
      const float ey = y0 + uy * static_cast<float>(nextPos);
      EmitSolidSegVc(sx, sy, z0, ex, ey, z1, rgba, outVerts7);
      ++emitted;
    }
    pos = nextPos;
  }
}

void CadTessellateLinetypeChainVc(const float* xy, int nPts, float z, bool closed, const std::string& linetypeName,
                                  float patternScaleWorld, const float rgba[4], std::vector<float>* outVerts7,
                                  const float* zPerPt, float clipHalfExtent) {
  if (!xy || nPts < 2 || !outVerts7)
    return;
  // Elevation of point i: its own when the caller supplied a per-vertex array, else the chain's.
  const auto zAt = [&](int i) { return zPerPt ? zPerPt[i] : z; };
  const std::string canon = CadCanonicalLinetypeNameForDxf(linetypeName);
  const Pattern* pat = CadLinetypeIsSolidCi(canon) ? nullptr : FindPatternCi(canon);
  double cyc = 0;
  const double sc = std::max(static_cast<double>(patternScaleWorld), 1e-9);
  if (pat) {
    for (int i = 0; i < pat->count; ++i)
      cyc += std::fabs(pat->elems[i]) * sc;
  }
  // Solid linetype, unknown pattern name, or a degenerate zero-length cycle: one segment per edge.
  if (!pat || cyc < 1e-12) {
    const int nSeg = closed ? nPts : nPts - 1;
    for (int i = 0; i < nSeg; ++i) {
      const int j = (i + 1) % nPts;
      EmitSolidSegVc(xy[i * 2], xy[i * 2 + 1], zAt(i), xy[j * 2], xy[j * 2 + 1], zAt(j), rgba, outVerts7);
    }
    return;
  }

  const int nSeg = closed ? nPts : nPts - 1;
  const double clipR = (clipHalfExtent > 0.f && std::isfinite(clipHalfExtent))
                           ? static_cast<double>(clipHalfExtent)
                           : 0.0;
  double patAcc = 0;
  int emitted = 0;
  for (int si = 0; si < nSeg; ++si) {
    const int j = (si + 1) % nPts;
    const float x0 = xy[si * 2];
    const float y0 = xy[si * 2 + 1];
    const float x1 = xy[j * 2];
    const float y1 = xy[j * 2 + 1];
    const float z0 = zAt(si);
    const float z1 = zAt(j);
    const float dx = x1 - x0;
    const float dy = y1 - y0;
    const float slen = std::hypot(dx, dy);
    if (!std::isfinite(slen) || slen < 1e-8f)
      continue;
    const double slenD = static_cast<double>(slen);
    double t0 = 0.0;
    double t1 = 1.0;
    if (clipR > 0.0) {
      if (!ClipSegmentToOriginSquare(x0, y0, x1, y1, clipR, &t0, &t1)) {
        // Off-screen chord: advance dash phase in O(1) without emitting vertices.
        patAcc = std::fmod(patAcc + slenD, cyc);
        continue;
      }
    }
    const float ux = dx / slen;
    const float uy = dy / slen;
    // Each dash starts and ends partway along the edge, so its endpoints take the edge's
    // elevation interpolated at the same parameter — otherwise a dashed sloped segment would
    // stair-step between two flat levels.
    const auto zAlong = [&](double along) {
      return z0 + (z1 - z0) * static_cast<float>(along / slenD);
    };
    const double uStart = t0 * slenD;
    const double uEnd = t1 * slenD;
    if (uStart > 0.0)
      patAcc = std::fmod(patAcc + uStart, cyc);
    const double visLen = uEnd - uStart;
    if (visLen <= 1e-9) {
      patAcc = std::fmod(patAcc + (slenD - uStart), cyc);
      continue;
    }
    const int budget = kMaxLinetypeSegmentsPerCall - emitted;
    if (budget <= 0 || visLen / cyc > static_cast<double>(budget)) {
      // Sub-pixel dash density or safety cap reached: emit the visible span as one solid chord.
      const float sx = x0 + ux * static_cast<float>(uStart);
      const float sy = y0 + uy * static_cast<float>(uStart);
      const float ex = x0 + ux * static_cast<float>(uEnd);
      const float ey = y0 + uy * static_cast<float>(uEnd);
      EmitSolidSegVc(sx, sy, zAlong(uStart), ex, ey, zAlong(uEnd), rgba, outVerts7);
      ++emitted;
      patAcc = std::fmod(patAcc + (slenD - uStart), cyc);
      continue;
    }
    double u = uStart;
    while (u < uEnd - 1e-9) {
      if (emitted >= kMaxLinetypeSegmentsPerCall) {
        const float sx = x0 + ux * static_cast<float>(u);
        const float sy = y0 + uy * static_cast<float>(u);
        const float ex = x0 + ux * static_cast<float>(uEnd);
        const float ey = y0 + uy * static_cast<float>(uEnd);
        EmitSolidSegVc(sx, sy, zAlong(u), ex, ey, zAlong(uEnd), rgba, outVerts7);
        u = uEnd;
        break;
      }
      double inCyc = std::fmod(patAcc, cyc);
      double acc = 0;
      int ei = 0;
      double elWorld = 0;
      FindPatternElement(pat, sc, cyc, &inCyc, &ei, &acc, &elWorld);
      const double into = inCyc - acc;
      const bool draw = pat->elems[ei] > 0;
      const double remainInElem = elWorld - into;
      const double remainLine = uEnd - u;
      const double step = std::min(remainInElem, remainLine);
      if (step <= 1e-12)
        break;
      const double nextU = u + step;
      if (!(nextU > u))
        break;
      if (draw) {
        const float sx = x0 + ux * static_cast<float>(u);
        const float sy = y0 + uy * static_cast<float>(u);
        const float ex = x0 + ux * static_cast<float>(nextU);
        const float ey = y0 + uy * static_cast<float>(nextU);
        EmitSolidSegVc(sx, sy, zAlong(u), ex, ey, zAlong(nextU), rgba, outVerts7);
        ++emitted;
      }
      patAcc = std::fmod(patAcc + step, cyc);
      u = nextU;
    }
    if (slenD > u)
      patAcc = std::fmod(patAcc + (slenD - u), cyc);
  }
}

void CadTessellateLinetypePolylineVc(const float* xy, int nPts, float z, const std::string& linetypeName,
                                    float patternScaleWorld, const float rgba[4], std::vector<float>* outVerts7) {
  CadTessellateLinetypeChainVc(xy, nPts, z, false, linetypeName, patternScaleWorld, rgba, outVerts7);
}
