// gen_c3d_icons.cpp — standalone generator for the Civil-3D-style placeholder
// ribbon icons (the `c3d_*` set) that have no match in the library icon set.
//
// Same minimal line-art style as the existing c3d_* PNGs: a single flat blue
// (#35A3DF) stroke on a transparent ground, a mid-gray (#808080) accent for
// fills/nodes, 128x128, 3x supersampled then alpha-weighted downsampled.
//
//   clang++ -std=c++17 -O2 tools/gen_c3d_icons.cpp -o build/gen_c3d_icons.exe
//   build/gen_c3d_icons.exe resources/icons
//
// Rerun after editing a design; the app loads the PNGs at startup.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Minimal PNG writer (RGBA8, stored zlib blocks) — same as tools/gen_icons.cpp
// ---------------------------------------------------------------------------
static uint32_t Crc32(const uint8_t* p, size_t n, uint32_t crc = 0xFFFFFFFFu) {
  static uint32_t table[256];
  static bool init = false;
  if (!init) {
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t c = i;
      for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
      table[i] = c;
    }
    init = true;
  }
  for (size_t i = 0; i < n; ++i) crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
  return crc;
}
static void PutBE32(std::vector<uint8_t>& v, uint32_t x) {
  v.push_back((x >> 24) & 0xFF); v.push_back((x >> 16) & 0xFF);
  v.push_back((x >> 8) & 0xFF);  v.push_back(x & 0xFF);
}
static void WriteChunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
  PutBE32(out, static_cast<uint32_t>(data.size()));
  const size_t crcStart = out.size();
  out.insert(out.end(), type, type + 4);
  out.insert(out.end(), data.begin(), data.end());
  const uint32_t crc = Crc32(&out[crcStart], out.size() - crcStart) ^ 0xFFFFFFFFu;
  PutBE32(out, crc);
}
static bool WritePng(const std::string& path, int w, int h, const std::vector<uint8_t>& rgba) {
  std::vector<uint8_t> raw;
  raw.reserve(static_cast<size_t>(h) * (1 + w * 4));
  for (int y = 0; y < h; ++y) {
    raw.push_back(0);
    const uint8_t* row = &rgba[static_cast<size_t>(y) * w * 4];
    raw.insert(raw.end(), row, row + static_cast<size_t>(w) * 4);
  }
  std::vector<uint8_t> z;
  z.push_back(0x78); z.push_back(0x01);
  size_t pos = 0;
  while (pos < raw.size()) {
    const size_t n = std::min<size_t>(65535, raw.size() - pos);
    const bool last = (pos + n) >= raw.size();
    z.push_back(last ? 1 : 0);
    z.push_back(n & 0xFF); z.push_back((n >> 8) & 0xFF);
    const uint16_t nlen = static_cast<uint16_t>(~n);
    z.push_back(nlen & 0xFF); z.push_back((nlen >> 8) & 0xFF);
    z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
    pos += n;
  }
  uint32_t a = 1, b = 0;
  for (uint8_t byte : raw) { a = (a + byte) % 65521; b = (b + a) % 65521; }
  PutBE32(z, (b << 16) | a);
  std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  std::vector<uint8_t> ihdr;
  PutBE32(ihdr, w); PutBE32(ihdr, h);
  ihdr.push_back(8); ihdr.push_back(6);
  ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);
  WriteChunk(out, "IHDR", ihdr);
  WriteChunk(out, "IDAT", z);
  WriteChunk(out, "IEND", {});
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return false;
  std::fwrite(out.data(), 1, out.size(), f);
  std::fclose(f);
  return true;
}

// ---------------------------------------------------------------------------
// Supersampled rasterizer (output space 0..S)
// ---------------------------------------------------------------------------
static constexpr int S  = 128;
static constexpr int SS = 3;
static constexpr int B  = S * SS;

struct Color { uint8_t r, g, b, a; };
static Color RGBA(int r, int g, int b, int a = 255) {
  return {static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b), static_cast<uint8_t>(a)};
}
static const Color BLUE = RGBA(53, 163, 223);   // #35A3DF primary stroke
static const Color GRAY = RGBA(128, 128, 128);  // #808080 accent fill / nodes
static const Color WHITE = RGBA(250, 250, 250);

struct Canvas {
  std::vector<uint8_t> p = std::vector<uint8_t>(static_cast<size_t>(B) * B * 4, 0);
  void set(int x, int y, Color c) {
    if (x < 0 || y < 0 || x >= B || y >= B) return;
    uint8_t* d = &p[(static_cast<size_t>(y) * B + x) * 4];
    d[0] = c.r; d[1] = c.g; d[2] = c.b; d[3] = c.a;
  }
};

static float DistSeg(float px, float py, float ax, float ay, float bx, float by) {
  const float dx = bx - ax, dy = by - ay;
  const float len2 = dx * dx + dy * dy;
  float tt = len2 > 1e-6f ? ((px - ax) * dx + (py - ay) * dy) / len2 : 0.f;
  tt = std::clamp(tt, 0.f, 1.f);
  const float qx = ax + dx * tt, qy = ay + dy * tt;
  return std::sqrt((px - qx) * (px - qx) + (py - qy) * (py - qy));
}
static void Line(Canvas& c, float x0, float y0, float x1, float y1, float th, Color col) {
  x0 *= SS; y0 *= SS; x1 *= SS; y1 *= SS;
  const float r = th * SS * 0.5f;
  const int minx = std::max(0, (int)std::floor(std::min(x0, x1) - r - 1));
  const int maxx = std::min(B - 1, (int)std::ceil(std::max(x0, x1) + r + 1));
  const int miny = std::max(0, (int)std::floor(std::min(y0, y1) - r - 1));
  const int maxy = std::min(B - 1, (int)std::ceil(std::max(y0, y1) + r + 1));
  for (int y = miny; y <= maxy; ++y)
    for (int x = minx; x <= maxx; ++x)
      if (DistSeg(x + 0.5f, y + 0.5f, x0, y0, x1, y1) <= r) c.set(x, y, col);
}
static void Disc(Canvas& c, float cx, float cy, float rad, Color col) {
  cx *= SS; cy *= SS; rad *= SS;
  for (int y = std::max(0, (int)(cy - rad - 1)); y <= std::min(B - 1, (int)(cy + rad + 1)); ++y)
    for (int x = std::max(0, (int)(cx - rad - 1)); x <= std::min(B - 1, (int)(cx + rad + 1)); ++x) {
      const float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
      if (dx * dx + dy * dy <= rad * rad) c.set(x, y, col);
    }
}
static void Ring(Canvas& c, float cx, float cy, float rad, float th, Color col,
                 float a0 = 0.f, float a1 = 6.2831853f) {
  cx *= SS; cy *= SS; rad *= SS;
  const float hr = th * SS * 0.5f;
  for (int y = std::max(0, (int)(cy - rad - hr - 1)); y <= std::min(B - 1, (int)(cy + rad + hr + 1)); ++y)
    for (int x = std::max(0, (int)(cx - rad - hr - 1)); x <= std::min(B - 1, (int)(cx + rad + hr + 1)); ++x) {
      const float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
      const float d = std::sqrt(dx * dx + dy * dy);
      if (std::fabs(d - rad) > hr) continue;
      float ang = std::atan2(dy, dx);
      if (ang < 0) ang += 6.2831853f;
      bool in = (a1 - a0 >= 6.2831f) || (ang >= a0 && ang <= a1) ||
                (ang + 6.2831853f >= a0 && ang + 6.2831853f <= a1);
      if (in) c.set(x, y, col);
    }
}
static void RectFill(Canvas& c, float x0, float y0, float x1, float y1, Color col) {
  x0 *= SS; y0 *= SS; x1 *= SS; y1 *= SS;
  for (int y = std::max(0, (int)y0); y < std::min(B, (int)std::ceil(y1)); ++y)
    for (int x = std::max(0, (int)x0); x < std::min(B, (int)std::ceil(x1)); ++x)
      c.set(x, y, col);
}
static void RectOutline(Canvas& c, float x0, float y0, float x1, float y1, float th, Color col) {
  Line(c, x0, y0, x1, y0, th, col);
  Line(c, x1, y0, x1, y1, th, col);
  Line(c, x1, y1, x0, y1, th, col);
  Line(c, x0, y1, x0, y0, th, col);
}
static void RoundRect(Canvas& c, float x0, float y0, float x1, float y1, float rad, float th, Color col) {
  Line(c, x0 + rad, y0, x1 - rad, y0, th, col);
  Line(c, x0 + rad, y1, x1 - rad, y1, th, col);
  Line(c, x0, y0 + rad, x0, y1 - rad, th, col);
  Line(c, x1, y0 + rad, x1, y1 - rad, th, col);
  Ring(c, x0 + rad, y0 + rad, rad, th, col, 3.1415927f, 4.712389f);
  Ring(c, x1 - rad, y0 + rad, rad, th, col, 4.712389f, 6.2831853f);
  Ring(c, x1 - rad, y1 - rad, rad, th, col, 0.f, 1.5707963f);
  Ring(c, x0 + rad, y1 - rad, rad, th, col, 1.5707963f, 3.1415927f);
}
struct Pt { float x, y; };
static void PolyFill(Canvas& c, const std::vector<Pt>& pts, Color col) {
  float minY = 1e9f, maxY = -1e9f, minX = 1e9f, maxX = -1e9f;
  for (auto& p : pts) { minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
                        minX = std::min(minX, p.x); maxX = std::max(maxX, p.x); }
  for (int y = std::max(0, (int)(minY * SS)); y <= std::min(B - 1, (int)(maxY * SS)); ++y)
    for (int x = std::max(0, (int)(minX * SS)); x <= std::min(B - 1, (int)(maxX * SS)); ++x) {
      const float fx = (x + 0.5f) / SS, fy = (y + 0.5f) / SS;
      bool inside = false;
      for (size_t i = 0, j = pts.size() - 1; i < pts.size(); j = i++) {
        if (((pts[i].y > fy) != (pts[j].y > fy)) &&
            (fx < (pts[j].x - pts[i].x) * (fy - pts[i].y) / (pts[j].y - pts[i].y) + pts[i].x))
          inside = !inside;
      }
      if (inside) c.set(x, y, col);
    }
}
static void Arrow(Canvas& c, float tx, float ty, float dx, float dy, float size, float th, Color col) {
  const float len = std::sqrt(dx * dx + dy * dy);
  if (len < 1e-4f) return;
  dx /= len; dy /= len;
  const float bx = tx - dx * size, by = ty - dy * size;
  const float px = -dy * size * 0.6f, py = dx * size * 0.6f;
  Line(c, bx + px, by + py, tx, ty, th, col);
  Line(c, bx - px, by - py, tx, ty, th, col);
}

// ---------------------------------------------------------------------------
// Icon designs — output space 0..128, ~14px margin
// ---------------------------------------------------------------------------
static constexpr float T = 8.f;   // primary stroke thickness

static void Folder(Canvas& c, Color col) {
  PolyFill(c, {{22, 40}, {46, 40}, {54, 48}, {106, 48}, {106, 96}, {22, 96}}, WHITE);
  Line(c, 22, 40, 46, 40, T, col);
  Line(c, 46, 40, 54, 48, T, col);
  Line(c, 54, 48, 106, 48, T, col);
  Line(c, 106, 48, 106, 96, T, col);
  Line(c, 106, 96, 22, 96, T, col);
  Line(c, 22, 96, 22, 40, T, col);
}
// Document sheet with a folded top-right corner.
static void Page(Canvas& c, float x0, float y0, float x1, float y1, Color col) {
  const float f = (x1 - x0) * 0.28f;
  PolyFill(c, {{x0, y0}, {x1 - f, y0}, {x1, y0 + f}, {x1, y1}, {x0, y1}}, WHITE);
  Line(c, x0, y0, x1 - f, y0, T, col);
  Line(c, x1 - f, y0, x1, y0 + f, T, col);
  Line(c, x1, y0 + f, x1, y1, T, col);
  Line(c, x1, y1, x0, y1, T, col);
  Line(c, x0, y1, x0, y0, T, col);
  Line(c, x1 - f, y0, x1 - f, y0 + f, T * 0.7f, col);
  Line(c, x1 - f, y0 + f, x1, y0 + f, T * 0.7f, col);
}

static void DrawIcon(Canvas& c, const std::string& n) {
  if (n == "c3d_datashortcut") {
    RectOutline(c, 28, 16, 84, 112, T, BLUE);
    // lightning bolt accent
    PolyFill(c, {{66, 30}, {54, 66}, {66, 66}, {58, 100}, {92, 56}, {74, 56}, {84, 30}}, GRAY);
  } else if (n == "c3d_newfolder") {
    Folder(c, BLUE);
    Line(c, 88, 84, 108, 84, T, GRAY);
    Line(c, 98, 74, 98, 94, T, GRAY);
  } else if (n == "c3d_setfolder") {
    Folder(c, BLUE);
    Line(c, 98, 40, 98, 78, T, GRAY);
    Arrow(c, 98, 80, 0, 1, 16, T, GRAY);
  } else if (n == "c3d_managedata") {
    Folder(c, BLUE);
    Ring(c, 92, 88, 12, T * 0.8f, GRAY);
    Disc(c, 92, 88, 4, GRAY);
    for (int i = 0; i < 8; ++i) {
      const float a = i * 0.7853982f;
      Line(c, 92 + std::cos(a) * 12, 88 + std::sin(a) * 12,
           92 + std::cos(a) * 20, 88 + std::sin(a) * 20, T * 0.7f, GRAY);
    }
  } else if (n == "c3d_validatedata") {
    Folder(c, BLUE);
    Line(c, 74, 74, 86, 90, T, GRAY);
    Line(c, 86, 90, 112, 56, T, GRAY);
  } else if (n == "c3d_syncref") {
    Ring(c, 64, 64, 34, T, BLUE, 0.5f, 5.2f);
    Arrow(c, 64 + std::cos(0.5f) * 34, 64 + std::sin(0.5f) * 34, std::sin(0.5f), -std::cos(0.5f), 18, T, BLUE);
    Arrow(c, 64 + std::cos(5.2f) * 34, 64 + std::sin(5.2f) * 34, -std::sin(5.2f), std::cos(5.2f), 18, T, BLUE);
  } else if (n == "c3d_record") {
    Ring(c, 64, 64, 40, T, BLUE);
    Disc(c, 64, 64, 20, GRAY);
  } else if (n == "c3d_play") {
    PolyFill(c, {{44, 32}, {44, 96}, {98, 64}}, BLUE);
  } else if (n == "c3d_cui") {
    RoundRect(c, 20, 26, 108, 102, 8, T, BLUE);
    Line(c, 20, 46, 108, 46, T, BLUE);
    Disc(c, 32, 36, 3.5f, GRAY); Disc(c, 44, 36, 3.5f, GRAY); Disc(c, 56, 36, 3.5f, GRAY);
    RectFill(c, 34, 60, 58, 88, GRAY);
  } else if (n == "c3d_toolpalette") {
    RoundRect(c, 40, 18, 96, 110, 6, T, BLUE);
    Line(c, 40, 40, 96, 40, T * 0.7f, BLUE);
    RectFill(c, 52, 52, 84, 66, GRAY);
    RectFill(c, 52, 78, 84, 92, GRAY);
  } else if (n == "c3d_editalias") {
    Line(c, 26, 96, 70, 96, T, BLUE);
    Line(c, 30, 96, 48, 44, T, BLUE);
    Line(c, 66, 96, 48, 44, T, BLUE);
    Line(c, 38, 74, 58, 74, T * 0.8f, BLUE);
    // pencil
    PolyFill(c, {{78, 92}, {94, 44}, {108, 50}, {92, 98}}, GRAY);
    PolyFill(c, {{78, 92}, {92, 98}, {80, 104}}, BLUE);
  } else if (n == "c3d_loadapp") {
    Ring(c, 60, 68, 22, T, BLUE);
    Disc(c, 60, 68, 7, GRAY);
    for (int i = 0; i < 8; ++i) {
      const float a = i * 0.7853982f;
      Line(c, 60 + std::cos(a) * 22, 68 + std::sin(a) * 22,
           60 + std::cos(a) * 32, 68 + std::sin(a) * 32, T * 0.8f, BLUE);
    }
    Line(c, 96, 40, 96, 16, T, GRAY);
    Arrow(c, 96, 14, 0, -1, 14, T, GRAY);
  } else if (n == "c3d_runscript") {
    RoundRect(c, 18, 30, 110, 98, 8, T, BLUE);
    Line(c, 34, 50, 48, 64, T, GRAY);
    Line(c, 48, 64, 34, 78, T, GRAY);
    Line(c, 56, 80, 80, 80, T, GRAY);
  } else if (n == "c3d_vbeditor") {
    Line(c, 48, 34, 24, 64, T, BLUE);
    Line(c, 24, 64, 48, 94, T, BLUE);
    Line(c, 80, 34, 104, 64, T, BLUE);
    Line(c, 104, 64, 80, 94, T, BLUE);
    Line(c, 70, 28, 58, 100, T, GRAY);
  } else if (n == "c3d_lispeditor") {
    Ring(c, 78, 64, 40, T, BLUE, 2.42f, 3.86f);   // "(" left
    Ring(c, 50, 64, 40, T, BLUE, -0.72f, 0.72f);  // ")" right
  } else if (n == "c3d_vbamacro") {
    RoundRect(c, 20, 30, 108, 98, 8, T, BLUE);
    Line(c, 36, 82, 36, 46, T, GRAY);
    Line(c, 36, 46, 54, 74, T, GRAY);
    Line(c, 54, 74, 72, 46, T, GRAY);
    Line(c, 72, 46, 72, 82, T, GRAY);
    Line(c, 84, 82, 96, 82, T, GRAY);
  } else if (n == "c3d_layertranslator") {
    PolyFill(c, {{28, 44}, {60, 30}, {92, 44}, {60, 58}}, WHITE);
    Line(c, 28, 44, 60, 30, T * 0.8f, BLUE); Line(c, 60, 30, 92, 44, T * 0.8f, BLUE);
    Line(c, 92, 44, 60, 58, T * 0.8f, BLUE); Line(c, 60, 58, 28, 44, T * 0.8f, BLUE);
    PolyFill(c, {{36, 92}, {68, 78}, {100, 92}, {68, 106}}, WHITE);
    Line(c, 36, 92, 68, 78, T * 0.8f, GRAY); Line(c, 68, 78, 100, 92, T * 0.8f, GRAY);
    Line(c, 100, 92, 68, 106, T * 0.8f, GRAY); Line(c, 68, 106, 36, 92, T * 0.8f, GRAY);
    Line(c, 76, 60, 60, 74, T, BLUE);
    Arrow(c, 60, 76, -0.6f, 0.8f, 13, T, BLUE);
  } else if (n == "c3d_cadstd_config") {
    RectOutline(c, 30, 18, 90, 110, T, BLUE);
    RoundRect(c, 44, 12, 76, 26, 5, T * 0.8f, BLUE);
    Line(c, 42, 44, 52, 56, T, GRAY);
    Line(c, 52, 56, 78, 32, T, GRAY);
    Line(c, 42, 78, 52, 90, T, GRAY);
    Line(c, 52, 90, 78, 66, T, GRAY);
  } else if (n == "c3d_propsets") {
    RectOutline(c, 20, 24, 92, 104, T, BLUE);
    Line(c, 20, 48, 92, 48, T * 0.7f, BLUE);
    Line(c, 20, 72, 92, 72, T * 0.7f, BLUE);
    Line(c, 56, 24, 56, 104, T * 0.7f, BLUE);
    PolyFill(c, {{82, 76}, {112, 76}, {112, 90}, {96, 90}, {90, 100}, {84, 90}, {82, 90}}, GRAY);
  } else if (n == "c3d_perfanalyzer") {
    Ring(c, 64, 74, 42, T, BLUE, 3.1415927f, 6.2831853f);
    Line(c, 64, 74, 92, 46, T, GRAY);
    Disc(c, 64, 74, 7, GRAY);
    for (int i = 0; i <= 4; ++i) {
      const float a = 3.1415927f + i * 0.7853982f;
      Line(c, 64 + std::cos(a) * 42, 74 + std::sin(a) * 42,
           64 + std::cos(a) * 34, 74 + std::sin(a) * 34, T * 0.7f, BLUE);
    }
  } else if (n == "c3d_dynamo") {
    Disc(c, 34, 40, 9, GRAY);
    Disc(c, 34, 92, 9, GRAY);
    Disc(c, 94, 66, 9, GRAY);
    Line(c, 34, 40, 94, 66, T * 0.8f, BLUE);
    Line(c, 34, 92, 94, 66, T * 0.8f, BLUE);
    Ring(c, 34, 40, 9, T * 0.7f, BLUE);
    Ring(c, 34, 92, 9, T * 0.7f, BLUE);
    Ring(c, 94, 66, 9, T * 0.7f, BLUE);
  } else if (n == "c3d_dynamoplayer") {
    Disc(c, 30, 38, 8, GRAY);
    Disc(c, 30, 84, 8, GRAY);
    Line(c, 30, 38, 74, 62, T * 0.8f, BLUE);
    Line(c, 30, 84, 74, 62, T * 0.8f, BLUE);
    Ring(c, 30, 38, 8, T * 0.7f, BLUE);
    Ring(c, 30, 84, 8, T * 0.7f, BLUE);
    PolyFill(c, {{78, 40}, {78, 96}, {118, 68}}, BLUE);

  // ---- Output tab ----
  } else if (n == "c3d_viewframes") {
    RectOutline(c, 18, 26, 110, 102, T, BLUE);
    Line(c, 64, 26, 64, 102, T * 0.8f, BLUE);
    Line(c, 18, 64, 110, 64, T * 0.8f, BLUE);
    RectFill(c, 26, 34, 56, 56, GRAY);
  } else if (n == "c3d_createsheets") {
    Page(c, 30, 16, 88, 112, BLUE);
    Line(c, 92, 92, 116, 92, T, GRAY);
    Line(c, 104, 80, 104, 104, T, GRAY);
  } else if (n == "c3d_sectionsheets") {
    Page(c, 30, 16, 88, 112, BLUE);
    Line(c, 40, 66, 78, 66, T, GRAY);
    Arrow(c, 78, 66, 1, 0, 14, T, GRAY);
    Line(c, 52, 44, 52, 88, T * 0.7f, GRAY);
  } else if (n == "c3d_plotpreview") {
    Page(c, 22, 16, 78, 96, BLUE);
    Ring(c, 82, 82, 20, T, GRAY);
    Line(c, 96, 96, 116, 116, T, GRAY);
  } else if (n == "c3d_plottermgr") {
    RectOutline(c, 22, 46, 92, 86, T, BLUE);
    RectFill(c, 36, 30, 78, 48, WHITE); RectOutline(c, 36, 30, 78, 48, T * 0.8f, BLUE);
    RectFill(c, 36, 84, 78, 108, WHITE); RectOutline(c, 36, 84, 78, 108, T * 0.8f, BLUE);
    Ring(c, 100, 92, 12, T * 0.8f, GRAY);
    for (int i = 0; i < 6; ++i) {
      const float a = i * 1.047f;
      Line(c, 100 + std::cos(a) * 12, 92 + std::sin(a) * 12,
           100 + std::cos(a) * 20, 92 + std::sin(a) * 20, T * 0.7f, GRAY);
    }
  } else if (n == "c3d_exportto") {
    Line(c, 30, 40, 30, 100, T, BLUE);
    Line(c, 30, 100, 84, 100, T, BLUE);
    Line(c, 84, 100, 84, 66, T, BLUE);
    Line(c, 30, 40, 62, 40, T, BLUE);
    Line(c, 58, 56, 100, 56, T, GRAY);
    Arrow(c, 104, 56, 1, 0, 18, T, GRAY);
    Line(c, 78, 34, 104, 56, T, GRAY);
    Line(c, 78, 78, 104, 56, T, GRAY);
  } else if (n == "c3d_landxml") {
    Page(c, 26, 14, 92, 114, BLUE);
    Line(c, 46, 52, 36, 66, T * 0.8f, GRAY); Line(c, 36, 66, 46, 80, T * 0.8f, GRAY);
    Line(c, 72, 52, 82, 66, T * 0.8f, GRAY); Line(c, 82, 66, 72, 80, T * 0.8f, GRAY);
    Line(c, 62, 46, 56, 86, T * 0.8f, GRAY);
  } else if (n == "c3d_exportpoints") {
    Disc(c, 32, 40, 5, GRAY); Disc(c, 30, 78, 5, GRAY); Disc(c, 52, 60, 5, GRAY);
    Disc(c, 44, 96, 5, GRAY); Disc(c, 66, 90, 5, GRAY);
    Line(c, 70, 44, 104, 44, T, BLUE);
    Arrow(c, 108, 44, 1, 0, 18, T, BLUE);
  } else if (n == "c3d_transferpoints") {
    Disc(c, 24, 42, 5, GRAY); Disc(c, 40, 30, 5, GRAY); Disc(c, 30, 60, 5, GRAY);
    Disc(c, 92, 84, 5, GRAY); Disc(c, 108, 72, 5, GRAY); Disc(c, 100, 102, 5, GRAY);
    Line(c, 44, 52, 88, 84, T, BLUE);
    Arrow(c, 90, 86, 0.8f, 0.6f, 16, T, BLUE);
  } else if (n == "c3d_publishsurf") {
    Line(c, 20, 96, 56, 52, T * 0.8f, BLUE); Line(c, 56, 52, 84, 92, T * 0.8f, BLUE);
    Line(c, 84, 92, 20, 96, T * 0.8f, BLUE); Line(c, 56, 52, 48, 96, T * 0.8f, BLUE);
    Line(c, 100, 60, 100, 22, T, GRAY);
    Arrow(c, 100, 20, 0, -1, 16, T, GRAY);
  } else if (n == "c3d_publishgis") {
    Ring(c, 56, 66, 38, T, BLUE);
    Ring(c, 30, 66, 38, T * 0.7f, BLUE, -1.2f, 1.2f);   // right meridian
    Ring(c, 82, 66, 38, T * 0.7f, BLUE, 2.0f, 4.28f);   // left meridian
    Line(c, 18, 66, 94, 66, T * 0.7f, BLUE);
    Line(c, 98, 42, 98, 22, T, GRAY);
    Arrow(c, 98, 20, 0, -1, 14, T, GRAY);
  } else if (n == "c3d_dwfx") {
    Page(c, 30, 14, 92, 114, BLUE);
    Line(c, 40, 60, 46, 92, T * 0.8f, GRAY); Line(c, 46, 92, 52, 68, T * 0.8f, GRAY);
    Line(c, 52, 68, 58, 92, T * 0.8f, GRAY); Line(c, 58, 92, 64, 60, T * 0.8f, GRAY);
    Line(c, 72, 60, 84, 92, T * 0.8f, GRAY); Line(c, 72, 92, 84, 60, T * 0.8f, GRAY);

  // ---- Survey tab ----
  } else if (n == "c3d_addlabels") {
    // label tag with a leader line
    PolyFill(c, {{20, 46}, {66, 46}, {84, 66}, {66, 86}, {20, 86}}, WHITE);
    Line(c, 20, 46, 66, 46, T, BLUE); Line(c, 66, 46, 84, 66, T, BLUE);
    Line(c, 84, 66, 66, 86, T, BLUE); Line(c, 66, 86, 20, 86, T, BLUE);
    Line(c, 20, 86, 20, 46, T, BLUE);
    Disc(c, 34, 66, 5, BLUE);
    Line(c, 84, 66, 108, 42, T, GRAY);
    Disc(c, 110, 40, 6, GRAY);
  } else if (n == "c3d_geodetic") {
    RoundRect(c, 30, 14, 98, 114, 8, T, BLUE);
    Line(c, 30, 44, 98, 44, T, BLUE);
    RectFill(c, 40, 24, 88, 36, GRAY);
    for (int r = 0; r < 3; ++r)
      for (int col = 0; col < 3; ++col)
        Disc(c, 44 + col * 20, 60 + r * 20, 5, GRAY);
  } else if (n == "c3d_surfedit") {
    Line(c, 16, 92, 52, 44, T * 0.8f, BLUE); Line(c, 52, 44, 84, 92, T * 0.8f, BLUE);
    Line(c, 84, 92, 16, 92, T * 0.8f, BLUE); Line(c, 52, 44, 44, 92, T * 0.8f, BLUE);
    PolyFill(c, {{74, 96}, {96, 40}, {110, 46}, {88, 102}}, GRAY);
    PolyFill(c, {{74, 96}, {88, 102}, {76, 110}}, BLUE);
  } else if (n == "c3d_mapcheck") {
    Ring(c, 60, 64, 40, T, BLUE);
    Ring(c, 34, 64, 40, T * 0.7f, BLUE, -1.15f, 1.15f);
    Ring(c, 86, 64, 40, T * 0.7f, BLUE, 1.99f, 4.29f);
    Line(c, 20, 64, 100, 64, T * 0.7f, BLUE);
    Line(c, 60, 24, 60, 104, T * 0.7f, BLUE);
    Line(c, 60, 64, 88, 40, T, GRAY);
    Arrow(c, 90, 38, 0.8f, -0.6f, 14, T, GRAY);
  } else if (n == "c3d_astro") {
    Ring(c, 58, 66, 20, T, BLUE);
    for (int i = 0; i < 8; ++i) {
      const float a = i * 0.7853982f;
      Line(c, 58 + std::cos(a) * 26, 66 + std::sin(a) * 26,
           58 + std::cos(a) * 36, 66 + std::sin(a) * 36, T * 0.8f, BLUE);
    }
    Line(c, 58, 66, 96, 28, T, GRAY);
    Arrow(c, 98, 26, 0.8f, -0.8f, 15, T, GRAY);
  } else if (n == "c3d_quickprofile") {
    Line(c, 18, 100, 18, 30, T * 0.8f, BLUE);
    Line(c, 18, 100, 110, 100, T * 0.8f, BLUE);
    Line(c, 24, 74, 44, 52, T, GRAY);
    Line(c, 44, 52, 66, 82, T, GRAY);
    Line(c, 66, 82, 88, 40, T, GRAY);
    Line(c, 88, 40, 104, 60, T, GRAY);
    Disc(c, 24, 74, 4.5f, BLUE); Disc(c, 104, 60, 4.5f, BLUE);
  } else {
    RectOutline(c, 24, 24, 104, 104, T, BLUE);
  }
}

static std::vector<uint8_t> Downsample(const Canvas& c) {
  std::vector<uint8_t> out(static_cast<size_t>(S) * S * 4, 0);
  for (int y = 0; y < S; ++y)
    for (int x = 0; x < S; ++x) {
      float ar = 0, ag = 0, ab = 0, aa = 0;
      for (int dy = 0; dy < SS; ++dy)
        for (int dx = 0; dx < SS; ++dx) {
          const uint8_t* d = &c.p[((static_cast<size_t>(y) * SS + dy) * B + (x * SS + dx)) * 4];
          const float a = d[3] / 255.f;
          ar += d[0] * a; ag += d[1] * a; ab += d[2] * a; aa += a;
        }
      uint8_t* o = &out[(static_cast<size_t>(y) * S + x) * 4];
      const float cnt = SS * SS;
      if (aa > 1e-4f) {
        o[0] = (uint8_t)std::clamp(ar / aa, 0.f, 255.f);
        o[1] = (uint8_t)std::clamp(ag / aa, 0.f, 255.f);
        o[2] = (uint8_t)std::clamp(ab / aa, 0.f, 255.f);
        o[3] = (uint8_t)std::clamp(aa / cnt * 255.f, 0.f, 255.f);
      }
    }
  return out;
}

int main(int argc, char** argv) {
  const std::string outDir = (argc > 1) ? argv[1] : "resources/icons";
  std::filesystem::create_directories(outDir);
  const char* names[] = {
    "c3d_datashortcut", "c3d_newfolder", "c3d_setfolder", "c3d_managedata",
    "c3d_validatedata", "c3d_syncref", "c3d_record", "c3d_play", "c3d_cui",
    "c3d_toolpalette", "c3d_editalias", "c3d_loadapp", "c3d_runscript",
    "c3d_vbeditor", "c3d_lispeditor", "c3d_vbamacro", "c3d_layertranslator",
    "c3d_cadstd_config", "c3d_propsets", "c3d_perfanalyzer", "c3d_dynamo",
    "c3d_dynamoplayer",
    "c3d_viewframes", "c3d_createsheets", "c3d_sectionsheets", "c3d_plotpreview",
    "c3d_plottermgr", "c3d_exportto", "c3d_landxml", "c3d_exportpoints",
    "c3d_transferpoints", "c3d_publishsurf", "c3d_publishgis", "c3d_dwfx",
    "c3d_addlabels", "c3d_geodetic", "c3d_surfedit", "c3d_mapcheck", "c3d_astro",
    "c3d_quickprofile",
  };
  int ok = 0;
  for (const char* nm : names) {
    Canvas c;
    DrawIcon(c, nm);
    const std::vector<uint8_t> img = Downsample(c);
    const std::string path = outDir + "/" + nm + ".png";
    if (WritePng(path, S, S, img)) { ++ok; std::printf("wrote %s\n", path.c_str()); }
    else std::printf("FAILED %s\n", path.c_str());
  }
  std::printf("done: %d/%d icons\n", ok, (int)(sizeof(names) / sizeof(names[0])));
  return 0;
}
