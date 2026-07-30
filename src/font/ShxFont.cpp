#define _CRT_SECURE_NO_WARNINGS  // std::getenv for USERPROFILE
#include "ShxFont.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <unordered_map>

namespace Shx {
namespace {

constexpr double kPi = 3.14159265358979323846;

// Unit vectors for the 16 SHX directions (each 22.5°): dominant axis = full length, the other = half.
const float kDirX[16] = {1, 1, 1, 0.5f, 0, -0.5f, -1, -1, -1, -1, -1, -0.5f, 0, 0.5f, 1, 1};
const float kDirY[16] = {0, 0.5f, 1, 1, 1, 1, 1, 0.5f, 0, -0.5f, -1, -1, -1, -1, -1, -0.5f};

int8_t S8(unsigned char b) { return static_cast<int8_t>(b); }

// Segments needed to keep an arc's chord sagging less than \p sagitta font units from the true curve.
// Callers pass a tolerance derived from the font's cap height, so the result is resolution-independent:
// tiny arcs stay cheap while a full bowl gets enough segments to read as a curve. Glyphs are built once
// and cached, so this cost is paid per glyph per font, never per frame.
int ArcSegments(double radius, double sweepAbs, double sagitta) {
  if (!(radius > sagitta) || !(sweepAbs > 0.0))
    return 2;
  const double maxStep = 2.0 * std::acos(std::max(-1.0, 1.0 - sagitta / radius));
  if (!(maxStep > 1e-9))
    return 96;
  const double n = std::ceil(sweepAbs / maxStep);
  return static_cast<int>(std::min(96.0, std::max(2.0, n)));
}

// Bytes consumed by the shape command starting at bc[i] (the command byte + its operands). Used to skip
// the command that follows a 0x0E (vertical-text-only) flag in horizontal text.
size_t CommandSpan(const std::vector<unsigned char>& bc, size_t i) {
  if (i >= bc.size())
    return 0;
  const unsigned char c = bc[i];
  if (c >= 0x10 || c == 0)
    return 1;  // single-byte vector or end
  switch (c) {
    case 1: case 2: case 5: case 6: case 0x0E: return 1;
    case 3: case 4: return 2;
    case 7: return 3;       // unifont subshape: 2-byte code
    case 8: return 3;       // dx, dy
    case 0x0A: return 3;    // octant arc: radius, code
    case 0x0B: return 6;    // fractional arc: 5 operands
    case 0x0C: return 4;    // bulge: dx, dy, bulge
    case 9: {               // multiple displacements until (0,0)
      size_t k = i + 1;
      while (k + 1 < bc.size() && !(bc[k] == 0 && bc[k + 1] == 0)) k += 2;
      return (k + 2) - i;
    }
    case 0x0D: {            // multiple bulges until (0,0)
      size_t k = i + 1;
      while (k + 1 < bc.size() && !(bc[k] == 0 && bc[k + 1] == 0)) k += 3;
      return (k + 2) - i;
    }
    default: return 1;
  }
}

}  // namespace

bool Font::LoadFromFile(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f.good())
    return false;
  std::vector<unsigned char> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  if (b.size() < 8)
    return false;
  size_t i = 0;
  while (i < b.size() && b[i] != 0x1A) ++i;  // skip ASCII header line
  if (i >= b.size())
    return false;
  ++i;
  auto u16 = [&](size_t p) -> unsigned { return p + 1 < b.size() ? (b[p] | (b[p + 1] << 8)) : 0; };
  const unsigned count = u16(i);
  i += 2;
  for (unsigned g = 0; g < count && i + 4 <= b.size(); ++g) {
    const unsigned code = u16(i);
    const unsigned len = u16(i + 2);
    i += 4;
    if (i + len > b.size())
      break;
    // Glyph data = name (null-terminated) + shape bytecode.
    size_t p = i;
    const size_t defEnd = i + len;
    while (p < defEnd && b[p] != 0) ++p;
    if (p < defEnd) ++p;  // skip the name terminator
    if (code == 0) {
      // Font descriptor glyph: after the name, byte[0] = above (cap height), byte[1] = below.
      if (p < defEnd)
        capHeight_ = static_cast<float>(b[p]);
    } else {
      defs_.emplace_back(code, std::vector<unsigned char>(b.begin() + p, b.begin() + defEnd));
    }
    i = defEnd;
  }
  loaded_ = !defs_.empty();
  return loaded_;
}

const Glyph* Font::glyph(unsigned code) {
  for (auto& kv : cache_)
    if (kv.first == code)
      return &kv.second;
  Glyph g;
  buildGlyph(code, &g);
  cache_.emplace_back(code, std::move(g));
  return &cache_.back().second;
}

void Font::buildGlyph(unsigned code, Glyph* out) {
  const std::vector<unsigned char>* bc = nullptr;
  for (auto& kv : defs_)
    if (kv.first == code) { bc = &kv.second; break; }
  if (!bc)
    return;

  float px = 0.f, py = 0.f;       // pen
  float scale = 1.f;
  bool penDown = false;
  std::vector<std::pair<float, float>> stack;
  std::vector<Vec2> curStroke;
  // Arc flattening tolerance in font units, tied to the cap height so every font flattens equally
  // finely. capHeight/3000 keeps the sag well under a pixel for text up to ~1000 px tall — far past
  // any readable size — which is what removes the visible faceting on bowls and rounds.
  const double arcSagitta = static_cast<double>(capHeight()) / 3000.0;
  auto flush = [&]() {
    if (curStroke.size() >= 2) {
      // Split the stroke wherever it turns sharply. A thick anti-aliased polyline miters every joint,
      // and at a near-reversal the two outer offsets cross each other and leave an unfilled sliver —
      // the dark nicks that showed up on serifed stroke fonts. Cutting the run at the cusp means only
      // gentle joints are ever mitred; the two pieces then simply overlap at the corner, which is what
      // a corner should look like. Done here, at build time, so it costs nothing per frame — and it is
      // safe for consumers that walk strokes segment by segment, which see the same segments either way.
      constexpr float kCuspCos = 0.259f;  // cos 75° — sharper than this starts a new polyline
      size_t begin = 0;
      for (size_t k = 1; k + 1 < curStroke.size(); ++k) {
        const float ax = curStroke[k].x - curStroke[k - 1].x, ay = curStroke[k].y - curStroke[k - 1].y;
        const float bx = curStroke[k + 1].x - curStroke[k].x, by = curStroke[k + 1].y - curStroke[k].y;
        const float la = std::hypot(ax, ay), lb = std::hypot(bx, by);
        if (la < 1e-6f || lb < 1e-6f)
          continue;
        if ((ax * bx + ay * by) / (la * lb) < kCuspCos) {
          out->strokes.emplace_back(curStroke.begin() + static_cast<std::ptrdiff_t>(begin),
                                    curStroke.begin() + static_cast<std::ptrdiff_t>(k) + 1);
          begin = k;
        }
      }
      out->strokes.emplace_back(curStroke.begin() + static_cast<std::ptrdiff_t>(begin), curStroke.end());
    }
    curStroke.clear();
  };
  auto moveTo = [&](float nx, float ny) {
    if (penDown) {
      if (curStroke.empty())
        curStroke.push_back(Vec2{px, py});
      curStroke.push_back(Vec2{nx, ny});
    }
    px = nx;
    py = ny;
  };
  auto vec = [&](float dx, float dy) { moveTo(px + dx * scale, py + dy * scale); };
  // Bulge arc from the pen to pen+(dx,dy); bulge/127 = tan(includedAngle/4) (sign = CCW/CW).
  auto bulgeTo = [&](int dxi, int dyi, int bulge) {
    const float ex = px + dxi * scale, ey = py + dyi * scale;
    const float sx = px, sy = py;
    const float chordx = ex - sx, chordy = ey - sy;
    const float chord = std::hypot(chordx, chordy);
    if (bulge == 0 || chord < 1e-6f) { moveTo(ex, ey); return; }
    const float ang = 4.f * std::atan(static_cast<float>(bulge) / 127.f);  // signed sweep
    const float s2 = std::sin(ang * 0.5f);
    if (std::fabs(s2) < 1e-6f) { moveTo(ex, ey); return; }
    const float R = chord / (2.f * s2);
    const float apo = R * std::cos(ang * 0.5f);
    const float nlx = -chordy / chord, nly = chordx / chord;  // left normal
    const float mx = (sx + ex) * 0.5f, my = (sy + ey) * 0.5f;
    // Centre sits at +apothem along the LEFT normal. R and apo carry the sweep's sign, so this one
    // expression puts the centre on the correct side for both directions and for major (>180°) arcs;
    // subtracting instead mirrored the centre, so every bulge arc swept away from its own end point.
    const float cx = mx + nlx * apo, cy = my + nly * apo;
    const float a0 = std::atan2(sy - cy, sx - cx);
    const float radius = std::hypot(sx - cx, sy - cy);
    const int seg = ArcSegments(radius, std::fabs(ang), arcSagitta);
    for (int k = 1; k <= seg; ++k) {
      const float t = a0 + ang * (static_cast<float>(k) / seg);
      moveTo(cx + radius * std::cos(t), cy + radius * std::sin(t));
    }
  };

  // Recursive interpreter (subshapes share pen/scale/penDown state via captured refs).
  std::function<void(const std::vector<unsigned char>&)> run = [&](const std::vector<unsigned char>& code_) {
    size_t i = 0;
    while (i < code_.size()) {
      const unsigned char c = code_[i++];
      if (c == 0) break;
      if (c >= 0x10) { vec(static_cast<float>(c >> 4) * kDirX[c & 0xF], static_cast<float>(c >> 4) * kDirY[c & 0xF]); continue; }
      switch (c) {
        case 1: penDown = true; break;
        case 2: penDown = false; flush(); break;
        case 3: if (i < code_.size()) scale /= std::max<float>(1.f, code_[i++]); break;
        case 4: if (i < code_.size()) scale *= std::max<float>(1.f, code_[i++]); break;
        case 5: stack.emplace_back(px, py); break;
        case 6: if (!stack.empty()) { flush(); px = stack.back().first; py = stack.back().second; stack.pop_back(); } break;
        case 7: {  // subshape (unifont: 2-byte code)
          if (i + 1 < code_.size()) {
            const unsigned sub = code_[i] | (code_[i + 1] << 8);
            i += 2;
            for (auto& kv : defs_)
              if (kv.first == sub) { run(kv.second); break; }
          }
          break;
        }
        case 8: if (i + 1 < code_.size()) { vec(S8(code_[i]), S8(code_[i + 1])); i += 2; } break;
        case 9:
          while (i + 1 < code_.size()) {
            const int dx = S8(code_[i]); const int dy = S8(code_[i + 1]); i += 2;
            if (dx == 0 && dy == 0) break;
            vec(static_cast<float>(dx), static_cast<float>(dy));
          }
          break;
        case 0x0A: {  // octant arc: radius, then bit7 = clockwise, bits 6-4 = start octant, bits 3-0 = count
          if (i + 1 < code_.size()) {
            const float r = static_cast<float>(code_[i]) * scale;
            const unsigned char sc = code_[i + 1];
            i += 2;
            // bit 7 is a direction FLAG, not a sign: the magnitude is the low 7 bits. Reading it as a
            // two's-complement int and taking abs() (0xA4 -> 92 instead of 0x24) decoded the wrong
            // start octant, so the arc was built around a bogus centre and left the pen — and hence
            // the glyph's advance width — badly wrong.
            const bool ccw = (sc & 0x80) == 0;
            const int a = sc & 0x7F;
            const int startOct = (a >> 4) & 0x7;
            int cnt = a & 0x0F; if (cnt == 0 || cnt > 8) cnt = 8;
            const double a0 = startOct * kPi / 4.0;
            const double cx = px - r * std::cos(a0), cy = py - r * std::sin(a0);
            const double sweep = (ccw ? 1 : -1) * cnt * kPi / 4.0;
            const int seg = ArcSegments(r, std::fabs(sweep), arcSagitta);
            for (int k = 1; k <= seg; ++k) {
              const double t = a0 + sweep * (static_cast<double>(k) / seg);
              moveTo(static_cast<float>(cx + r * std::cos(t)), static_cast<float>(cy + r * std::sin(t)));
            }
          }
          break;
        }
        case 0x0B: {  // fractional arc: startOff, endOff, highR, lowR, ±(start<<4|count)
          if (i + 4 < code_.size()) {
            const int startOff = code_[i], endOff = code_[i + 1];
            const float r = static_cast<float>(code_[i + 2] * 256 + code_[i + 3]) * scale;
            const unsigned char sc = code_[i + 4];
            i += 5;
            const bool ccw = (sc & 0x80) == 0;  // direction flag + 7-bit magnitude, as for 0x0A above
            const int a = sc & 0x7F;
            const int startOct = (a >> 4) & 0x7;
            int cnt = a & 0x0F; if (cnt == 0 || cnt > 8) cnt = 8;
            const double a0 = (startOct + startOff / 256.0) * kPi / 4.0;
            const double aEnd = ((startOct + cnt) - endOff / 256.0) * kPi / 4.0;
            const double cx = px - r * std::cos(a0), cy = py - r * std::sin(a0);
            const double sweep = (ccw ? 1 : -1) * std::fabs(aEnd - a0);
            const int seg = ArcSegments(r, std::fabs(sweep), arcSagitta);
            for (int k = 1; k <= seg; ++k) {
              const double t = a0 + sweep * (static_cast<double>(k) / seg);
              moveTo(static_cast<float>(cx + r * std::cos(t)), static_cast<float>(cy + r * std::sin(t)));
            }
          }
          break;
        }
        case 0x0C: if (i + 2 < code_.size()) { bulgeTo(S8(code_[i]), S8(code_[i + 1]), S8(code_[i + 2])); i += 3; } break;
        case 0x0D:
          // Triples of (dx, dy, bulge) ending at a (0,0) displacement — the terminator is the 2-byte
          // pair only, with no bulge byte, which is what CommandSpan already assumes. Consuming a
          // third byte for it desynchronised everything after the list.
          while (i + 1 < code_.size()) {
            const int dx = S8(code_[i]); const int dy = S8(code_[i + 1]); i += 2;
            if (dx == 0 && dy == 0) break;
            if (i >= code_.size()) break;
            const int bl = S8(code_[i]); ++i;
            bulgeTo(dx, dy, bl);
          }
          break;
        case 0x0E: i += CommandSpan(code_, i); break;  // skip the vertical-text-only command
        default: break;
      }
    }
    flush();
  };
  run(*bc);
  out->advance = px;  // net horizontal pen travel = glyph advance
}

Font* Resolve(const std::string& fontName) {
  static std::unordered_map<std::string, std::unique_ptr<Font>> cache;
  // Normalize: lowercase, ensure ".shx".
  std::string key;
  for (char c : fontName) key += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (key.size() < 4 || key.substr(key.size() - 4) != ".shx")
    key += ".shx";
  if (auto it = cache.find(key); it != cache.end())
    return it->second ? it->second.get() : nullptr;

  // Candidate font directories: installed Autodesk products + shared components.
  static std::vector<std::string> dirs = [] {
    std::vector<std::string> d;
    namespace fs = std::filesystem;
    std::error_code ec;
    auto scan = [&](const std::string& root, int depth) {
      if (!fs::is_directory(root, ec)) return;
      for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec);
           it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        if (it.depth() > depth) { it.disable_recursion_pending(); continue; }
        if (it->is_directory(ec)) {
          const std::string name = it->path().filename().string();
          std::string lo; for (char c : name) lo += static_cast<char>(std::tolower((unsigned char)c));
          if (lo == "fonts" || lo == "support")
            d.push_back(it->path().string());
        }
      }
    };
    scan("C:/Program Files/Autodesk", 3);
    scan("C:/Program Files/Common Files/Autodesk Shared", 5);
    if (const char* up = std::getenv("USERPROFILE"))
      scan(std::string(up) + "/AppData/Roaming/Autodesk", 5);
    return d;
  }();

  std::unique_ptr<Font> font;
  namespace fs = std::filesystem;
  std::error_code ec;
  for (const std::string& dir : dirs) {
    const std::string path = dir + "/" + key;
    if (fs::exists(path, ec)) {
      auto f = std::make_unique<Font>();
      if (f->LoadFromFile(path)) {
        font = std::move(f);
        break;
      }
    }
  }
  Font* raw = font.get();
  cache[key] = std::move(font);
  return raw;
}

float MeasureWidthPx(Font& font, const std::string& text, float capPx) {
  const float s = capPx / font.capHeight();
  float w = 0.f;
  for (unsigned char ch : text) {
    if (ch == '\n') continue;
    if (const Glyph* g = font.glyph(ch))
      w += g->advance * s;
  }
  return w;
}

}  // namespace Shx
