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
  std::vector<ImVec2> curStroke;
  auto flush = [&]() {
    if (curStroke.size() >= 2)
      out->strokes.push_back(curStroke);
    curStroke.clear();
  };
  auto moveTo = [&](float nx, float ny) {
    if (penDown) {
      if (curStroke.empty())
        curStroke.push_back(ImVec2(px, py));
      curStroke.push_back(ImVec2(nx, ny));
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
    const float cx = mx - nlx * apo, cy = my - nly * apo;
    const float a0 = std::atan2(sy - cy, sx - cx);
    const float radius = std::hypot(sx - cx, sy - cy);
    const int seg = std::max(2, static_cast<int>(std::fabs(ang) / 0.3f) + 2);
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
        case 0x0A: {  // octant arc: radius, ±(start<<4 | count)
          if (i + 1 < code_.size()) {
            const float r = static_cast<float>(code_[i]) * scale;
            const int s2 = S8(code_[i + 1]);
            i += 2;
            const bool ccw = s2 >= 0;
            const int a = std::abs(s2);
            const int startOct = (a >> 4) & 0x7;
            int cnt = a & 0x7; if (cnt == 0) cnt = 8;
            const double a0 = startOct * kPi / 4.0;
            const double cx = px - r * std::cos(a0), cy = py - r * std::sin(a0);
            const double sweep = (ccw ? 1 : -1) * cnt * kPi / 4.0;
            const int seg = std::max(2, cnt * 3);
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
            const int s2 = S8(code_[i + 4]);
            i += 5;
            const bool ccw = s2 >= 0;
            const int a = std::abs(s2);
            const int startOct = (a >> 4) & 0x7;
            int cnt = a & 0x7; if (cnt == 0) cnt = 8;
            const double a0 = (startOct + startOff / 256.0) * kPi / 4.0;
            const double aEnd = ((startOct + cnt) - endOff / 256.0) * kPi / 4.0;
            const double cx = px - r * std::cos(a0), cy = py - r * std::sin(a0);
            const double sweep = (ccw ? 1 : -1) * std::fabs(aEnd - a0);
            const int seg = std::max(2, cnt * 3);
            for (int k = 1; k <= seg; ++k) {
              const double t = a0 + sweep * (static_cast<double>(k) / seg);
              moveTo(static_cast<float>(cx + r * std::cos(t)), static_cast<float>(cy + r * std::sin(t)));
            }
          }
          break;
        }
        case 0x0C: if (i + 2 < code_.size()) { bulgeTo(S8(code_[i]), S8(code_[i + 1]), S8(code_[i + 2])); i += 3; } break;
        case 0x0D:
          while (i + 2 < code_.size()) {
            const int dx = S8(code_[i]); const int dy = S8(code_[i + 1]); const int bl = S8(code_[i + 2]); i += 3;
            if (dx == 0 && dy == 0) break;
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

void DrawText(ImDrawList* dl, Font& font, ImVec2 baseline, float capPx, float rotRad, ImU32 col,
              const std::string& text, float thicknessPx) {
  if (!dl)
    return;
  const float s = capPx / font.capHeight();
  const float cr = std::cos(rotRad), sr = std::sin(rotRad);
  float penX = 0.f;
  for (unsigned char ch : text) {
    if (ch == '\n')
      continue;
    const Glyph* g = font.glyph(ch);
    if (!g)
      continue;
    for (const auto& stroke : g->strokes) {
      if (stroke.size() < 2)
        continue;
      for (size_t k = 0; k + 1 < stroke.size(); ++k) {
        auto map = [&](const ImVec2& p) {
          // font units → text-local px (+y up), then rotate, then offset from baseline (screen y down).
          const float lx = (penX + p.x) * s;
          const float ly = p.y * s;
          return ImVec2(baseline.x + lx * cr + ly * sr, baseline.y + lx * sr - ly * cr);
        };
        dl->AddLine(map(stroke[k]), map(stroke[k + 1]), col, thicknessPx);
      }
    }
    penX += g->advance;
  }
}

}  // namespace Shx
