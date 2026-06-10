// gen_icons.cpp — standalone generator for GoSurvey toolbar icon PNGs.
//
// Renders each icon at 4x supersampling with a tiny software rasterizer, then
// alpha-weighted downsamples to 32x32 RGBA and writes a PNG (minimal encoder,
// no external deps). Run once from the repo root:
//
//   clang++ -std=c++17 -O2 tools/gen_icons.cpp -o build/gen_icons.exe
//   build/gen_icons.exe resources/icons
//
// Output: resources/icons/<name>.png for every command icon. Swap any file to
// customize; the app loads them at startup.

#include <cstdint>
#include <cstdio>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>
#include <filesystem>

// ---------------------------------------------------------------------------
// Minimal PNG writer (RGBA8, uncompressed zlib "stored" blocks)
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
  // Filtered raw data: each scanline prefixed with filter byte 0.
  std::vector<uint8_t> raw;
  raw.reserve(static_cast<size_t>(h) * (1 + w * 4));
  for (int y = 0; y < h; ++y) {
    raw.push_back(0);
    const uint8_t* row = &rgba[static_cast<size_t>(y) * w * 4];
    raw.insert(raw.end(), row, row + static_cast<size_t>(w) * 4);
  }
  // zlib stream: header + stored deflate blocks + adler32.
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
  // Adler32 of raw.
  uint32_t a = 1, b = 0;
  for (uint8_t byte : raw) { a = (a + byte) % 65521; b = (b + a) % 65521; }
  PutBE32(z, (b << 16) | a);

  std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  std::vector<uint8_t> ihdr;
  PutBE32(ihdr, w); PutBE32(ihdr, h);
  ihdr.push_back(8);   // bit depth
  ihdr.push_back(6);   // color type RGBA
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
// Supersampled software rasterizer
// ---------------------------------------------------------------------------

static constexpr int S  = 32;       // output icon size
static constexpr int SS = 4;        // supersample factor
static constexpr int B  = S * SS;   // big canvas size

struct Color { uint8_t r, g, b, a; };
static Color RGBA(int r, int g, int b, int a = 255) {
  return {static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b), static_cast<uint8_t>(a)};
}

// nanoCAD-ish palette.
static const Color BLUE   = RGBA(26, 72, 150);
static const Color GREEN  = RGBA(34, 130, 64);
static const Color TEAL   = RGBA(20, 120, 130);
static const Color ORANGE = RGBA(200, 110, 24);
static const Color SLATE  = RGBA(64, 72, 84);
static const Color RED    = RGBA(206, 38, 38);
static const Color YELLOW = RGBA(225, 178, 40);
static const Color WHITE  = RGBA(248, 248, 248);
static const Color PINK   = RGBA(225, 120, 130);

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

// All primitives take output-space coords (0..S); scaled by SS internally.
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
      float aa0 = a0, aa1 = a1;
      bool in = (aa1 - aa0 >= 6.2831f) || (ang >= aa0 && ang <= aa1) ||
                (ang + 6.2831853f >= aa0 && ang + 6.2831853f <= aa1);
      if (in) c.set(x, y, col);
    }
}

static void EllipseRing(Canvas& c, float cx, float cy, float rx, float ry, float th, Color col) {
  cx *= SS; cy *= SS; rx *= SS; ry *= SS;
  const float hr = th * SS * 0.5f;
  for (int y = std::max(0, (int)(cy - ry - hr - 1)); y <= std::min(B - 1, (int)(cy + ry + hr + 1)); ++y)
    for (int x = std::max(0, (int)(cx - rx - hr - 1)); x <= std::min(B - 1, (int)(cx + rx + hr + 1)); ++x) {
      const float dx = (x + 0.5f - cx) / rx, dy = (y + 0.5f - cy) / ry;
      const float d = std::sqrt(dx * dx + dy * dy);
      // approximate ring thickness in normalized space
      const float scale = (rx + ry) * 0.5f;
      if (std::fabs(d - 1.f) * scale <= hr) c.set(x, y, col);
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

// Small filled node square centered at (x,y), half-size hs (output space).
static void Node(Canvas& c, float x, float y, Color fill, float hs = 2.0f) {
  RectFill(c, x - hs, y - hs, x + hs, y + hs, fill);
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
  const float px = -dy * size * 0.5f, py = dx * size * 0.5f;
  Line(c, bx + px, by + py, tx, ty, th, col);
  Line(c, bx - px, by - py, tx, ty, th, col);
}

// ---------------------------------------------------------------------------
// Icon designs
// ---------------------------------------------------------------------------

static void DrawIcon(Canvas& c, const std::string& n) {
  const float cx = 16, cy = 16;
  if (n == "line") {
    Line(c, 6, 26, 26, 6, 2.4f, BLUE);
    Node(c, 6, 26, RED); Node(c, 26, 6, RED);
  } else if (n == "circle") {
    Ring(c, cx, cy, 9, 2.4f, BLUE);
    Node(c, cx, cy, RED, 1.6f);
  } else if (n == "polyline") {
    Line(c, 5, 25, 12, 11, 2.2f, BLUE);
    Line(c, 12, 11, 20, 18, 2.2f, BLUE);
    Line(c, 20, 18, 27, 7, 2.2f, BLUE);
    Node(c, 5, 25, RED); Node(c, 12, 11, RED); Node(c, 20, 18, RED); Node(c, 27, 7, RED);
  } else if (n == "arc") {
    Ring(c, cx, 22, 11, 2.4f, BLUE, 3.3414f, 6.083f);
    Node(c, 5.5f, 21, RED); Node(c, 26.5f, 21, RED);
  } else if (n == "ellipse") {
    EllipseRing(c, cx, cy, 11, 6.5f, 2.3f, BLUE);
    Node(c, cx, cy, RED, 1.6f);
  } else if (n == "dim") {
    Line(c, 6, 24, 11, 19, 2.0f, BLUE);   // witness
    Line(c, 21, 9, 26, 4, 2.0f, BLUE);
    Line(c, 8.5f, 21.5f, 23.5f, 6.5f, 2.0f, TEAL); // dim line
    Arrow(c, 8.5f, 21.5f, -1, 1, 4, 2.0f, TEAL);
    Arrow(c, 23.5f, 6.5f, 1, -1, 4, 2.0f, TEAL);
  } else if (n == "dimlinear") {
    Line(c, 7, 9, 7, 22, 2.0f, BLUE);
    Line(c, 25, 9, 25, 22, 2.0f, BLUE);
    Line(c, 7, 16, 25, 16, 2.0f, TEAL);
    Arrow(c, 7, 16, -1, 0, 4, 2.0f, TEAL);
    Arrow(c, 25, 16, 1, 0, 4, 2.0f, TEAL);
  } else if (n == "id") {
    Line(c, 11, 22, 11, 11, 2.0f, SLATE); Arrow(c, 11, 10, 0, -1, 3.5f, 2.0f, SLATE);
    Line(c, 11, 22, 22, 22, 2.0f, SLATE); Arrow(c, 23, 22, 1, 0, 3.5f, 2.0f, SLATE);
    Ring(c, 18, 14, 5, 2.0f, SLATE);
    Line(c, 21.5f, 17.5f, 25, 21, 2.4f, SLATE);
    Node(c, 18, 14, RED, 1.6f);
  } else if (n == "text") {
    PolyFill(c, {{16, 5}, {18, 5}, {25, 26}, {21, 26}, {19.5f, 21}, {12.5f, 21}, {11, 26}, {7, 26}}, BLUE);
    RectFill(c, 13.4f, 17.5f, 18.6f, 19.0f, WHITE); // crossbar notch
  } else if (n == "mtext") {
    RectOutline(c, 6, 7, 26, 25, 2.0f, BLUE);
    Line(c, 9, 12, 23, 12, 1.8f, BLUE);
    Line(c, 9, 16, 23, 16, 1.8f, BLUE);
    Line(c, 9, 20, 18, 20, 1.8f, BLUE);
  } else if (n == "move") {
    const float a = 9;
    Line(c, cx, cy - a, cx, cy + a, 2.0f, GREEN);
    Line(c, cx - a, cy, cx + a, cy, 2.0f, GREEN);
    Arrow(c, cx, cy - a, 0, -1, 4, 2.0f, GREEN);
    Arrow(c, cx, cy + a, 0, 1, 4, 2.0f, GREEN);
    Arrow(c, cx - a, cy, -1, 0, 4, 2.0f, GREEN);
    Arrow(c, cx + a, cy, 1, 0, 4, 2.0f, GREEN);
  } else if (n == "copy") {
    RectOutline(c, 6, 11, 18, 25, 2.0f, BLUE);
    RectOutline(c, 12, 5, 24, 19, 2.0f, GREEN);
  } else if (n == "rotate") {
    Ring(c, cx, cy, 8, 2.2f, GREEN, 0.9f, 5.2f);
    const float ae = 5.2f;
    Arrow(c, cx + std::cos(ae) * 8, cy + std::sin(ae) * 8, std::sin(ae), -std::cos(ae), 4.5f, 2.2f, GREEN);
    Disc(c, cx, cy, 1.8f, GREEN);
  } else if (n == "scale") {
    RectOutline(c, 6, 18, 14, 26, 2.0f, GREEN);
    RectOutline(c, 16, 6, 26, 16, 2.0f, GREEN);
    Line(c, 12, 20, 22, 10, 2.0f, GREEN);
    Arrow(c, 22, 10, 1, -1, 4, 2.0f, GREEN);
  } else if (n == "erase") {
    PolyFill(c, {{8, 20}, {18, 8}, {24, 13}, {14, 25}}, PINK);     // eraser body
    PolyFill(c, {{8, 20}, {12, 15.2f}, {18, 20.2f}, {14, 25}}, WHITE);
    Line(c, 8, 25.5f, 24, 25.5f, 1.6f, SLATE);                    // surface
    RectOutline(c, 8, 20, 24, 13, 0.0f, SLATE);                   // (noop safety)
  } else if (n == "join") {
    Line(c, 5, 16, 14, 16, 2.2f, GREEN); Arrow(c, 14.5f, 16, 1, 0, 4, 2.2f, GREEN);
    Line(c, 27, 16, 18, 16, 2.2f, GREEN); Arrow(c, 17.5f, 16, -1, 0, 4, 2.2f, GREEN);
    Node(c, 16, 16, RED, 1.6f);
  } else if (n == "trim") {
    Line(c, 8, 22, 12, 22, 2.0f, BLUE);
    Line(c, 20, 22, 24, 22, 2.0f, BLUE);
    Line(c, 10, 6, 22, 28, 2.2f, RED);     // cutting edge
  } else if (n == "offset") {
    Ring(c, 13, cy, 9, 2.2f, BLUE, 1.5708f, 4.7124f);
    Ring(c, 13, cy, 5, 2.2f, GREEN, 1.5708f, 4.7124f);
    Arrow(c, 22, cy, 1, 0, 4, 2.0f, GREEN);
  } else if (n == "zoomextents") {
    Ring(c, 14, 14, 7, 2.2f, SLATE);
    Line(c, 19, 19, 26, 26, 2.6f, SLATE);
    Line(c, 14, 10.5f, 14, 17.5f, 1.8f, SLATE); // plus
    Line(c, 10.5f, 14, 17.5f, 14, 1.8f, SLATE);
  } else if (n == "zoomwindow") {
    Ring(c, 14, 14, 7, 2.2f, SLATE);
    Line(c, 19, 19, 26, 26, 2.6f, SLATE);
    RectOutline(c, 10.5f, 11.5f, 17.5f, 16.5f, 1.6f, SLATE);
  } else if (n == "mirror") {
    PolyFill(c, {{6, 8}, {13, 8}, {13, 24}}, BLUE);
    PolyFill(c, {{26, 8}, {19, 8}, {19, 24}}, TEAL);
    for (int i = 0; i < 6; ++i) Line(c, 16, 6 + i * 3.6f, 16, 7.6f + i * 3.6f, 1.6f, SLATE); // dashed axis
  } else if (n == "surveypoint") {
    Ring(c, cx, cy, 6.5f, 2.0f, ORANGE);
    Line(c, cx, 5, cx, 27, 1.8f, ORANGE);
    Line(c, 5, cy, 27, cy, 1.8f, ORANGE);
    Disc(c, cx, cy, 2.2f, RED);
  } else if (n == "surveyinverse") {
    Line(c, 7, 24, 25, 8, 2.2f, ORANGE);
    Arrow(c, 25, 8, 1, -1, 4, 2.2f, ORANGE);
    Arrow(c, 7, 24, -1, 1, 4, 2.2f, ORANGE);
    Node(c, 7, 24, RED); Node(c, 25, 8, RED);
  } else if (n == "traverse") {
    std::vector<Pt> v = {{8, 22}, {6, 11}, {16, 6}, {26, 12}, {23, 23}};
    for (size_t i = 0; i < v.size(); ++i) {
      const Pt& a = v[i]; const Pt& b = v[(i + 1) % v.size()];
      Line(c, a.x, a.y, b.x, b.y, 2.0f, ORANGE);
    }
    for (auto& p : v) Node(c, p.x, p.y, RED, 1.6f);
  } else if (n == "layers") {
    PolyFill(c, {{16, 6}, {27, 12}, {16, 18}, {5, 12}}, BLUE);
    PolyFill(c, {{16, 12}, {27, 18}, {16, 24}, {5, 18}}, TEAL);
  } else if (n == "pdfattach") {
    RectFill(c, 8, 5, 24, 27, WHITE);
    RectOutline(c, 8, 5, 24, 27, 1.6f, SLATE);
    RectFill(c, 8, 19, 24, 27, RED);
    RectFill(c, 10, 21.5f, 12, 25, WHITE); RectFill(c, 14, 21.5f, 16, 25, WHITE); RectFill(c, 18, 21.5f, 20, 25, WHITE);
  } else if (n == "pdfshowbg") {
    RectFill(c, 7, 6, 23, 26, WHITE); RectOutline(c, 7, 6, 23, 26, 1.6f, SLATE);
    Ring(c, 22, 22, 5, 1.8f, GREEN); Disc(c, 22, 22, 2, GREEN);
  } else if (n == "pdfhidebg") {
    RectFill(c, 7, 6, 23, 26, WHITE); RectOutline(c, 7, 6, 23, 26, 1.6f, SLATE);
    Ring(c, 22, 22, 5, 1.8f, RED);
    Line(c, 18, 26, 26, 18, 2.0f, RED);
  } else if (n == "pdfvectorize") {
    RectFill(c, 7, 6, 23, 26, WHITE); RectOutline(c, 7, 6, 23, 26, 1.6f, SLATE);
    Line(c, 10, 22, 16, 12, 1.8f, BLUE); Line(c, 16, 12, 21, 18, 1.8f, BLUE);
    Node(c, 10, 22, RED, 1.4f); Node(c, 16, 12, RED, 1.4f); Node(c, 21, 18, RED, 1.4f);
  } else if (n == "undo") {
    Ring(c, cx, 17, 8, 2.2f, SLATE, 3.4f, 6.2831853f);
    Line(c, 8, 17, 8, 9, 2.2f, SLATE);
    Arrow(c, 8, 9, 0, -1, 4.5f, 2.2f, SLATE);
  } else if (n == "redo") {
    Ring(c, cx, 17, 8, 2.2f, SLATE, 3.4f, 6.2831853f);
    Line(c, 24, 17, 24, 9, 2.2f, SLATE);
    Arrow(c, 24, 9, 0, -1, 4.5f, 2.2f, SLATE);
  } else if (n == "clipboardcopy") {
    RectFill(c, 7, 6, 19, 24, WHITE); RectOutline(c, 7, 6, 19, 24, 1.6f, SLATE);
    RectFill(c, 10, 4, 16, 7, SLATE);
    RectOutline(c, 13, 12, 25, 27, 2.0f, GREEN);
  } else if (n == "clipboardpaste") {
    RectFill(c, 8, 7, 24, 27, WHITE); RectOutline(c, 8, 7, 24, 27, 1.6f, SLATE);
    RectFill(c, 12, 5, 20, 9, SLATE);
    Line(c, 11, 13, 21, 13, 1.6f, SLATE); Line(c, 11, 17, 21, 17, 1.6f, SLATE); Line(c, 11, 21, 18, 21, 1.6f, SLATE);
  } else {
    // Unknown: draw a placeholder box.
    RectOutline(c, 6, 6, 26, 26, 2.0f, SLATE);
  }
}

// ---------------------------------------------------------------------------
// Downsample (alpha-weighted) + driver
// ---------------------------------------------------------------------------

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
    "line", "circle", "polyline", "arc", "ellipse", "dim", "dimlinear", "id",
    "text", "mtext", "move", "copy", "rotate", "erase", "join", "trim", "offset",
    "zoomextents", "zoomwindow", "scale", "mirror", "surveypoint", "surveyinverse",
    "layers", "pdfattach", "pdfshowbg", "pdfhidebg", "pdfvectorize", "undo", "redo",
    "clipboardcopy", "clipboardpaste", "traverse",
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
