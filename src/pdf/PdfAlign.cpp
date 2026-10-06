#include "PdfAlign.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace pdfalign {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr int kInkBelow = 170; // luminance under this is ink
} // namespace

double Transform::Scale() const { return std::hypot(a, b); }
double Transform::RotationDeg() const { return std::atan2(b, a) * 180.0 / kPi; }

Transform Transform::Inverse() const {
  const double d = a * a + b * b;
  Transform t;
  t.a = a / d;
  t.b = -b / d;
  t.tx = -(t.a * tx - t.b * ty);
  t.ty = -(t.b * tx + t.a * ty);
  return t;
}

Transform FromOnePoint(Pt rev, Pt base) {
  Transform t;
  t.tx = base.x - rev.x;
  t.ty = base.y - rev.y;
  return t;
}

bool FromTwoPoints(Pt rev1, Pt rev2, Pt base1, Pt base2, Transform& out, std::string& why) {
  const double rx = rev2.x - rev1.x, ry = rev2.y - rev1.y, bx = base2.x - base1.x, by = base2.y - base1.y;
  const double rr = rx * rx + ry * ry, bb = bx * bx + by * by;
  if (rr < 1e-9) {
    why = "the two points picked on the revision are the same point";
    return false;
  }
  if (bb < 1e-9) {
    why = "the two points picked on the base are the same point";
    return false;
  }
  // The complex ratio (base2 - base1) / (rev2 - rev1) is the scale and rotation together.
  Transform t;
  t.a = (bx * rx + by * ry) / rr;
  t.b = (by * rx - bx * ry) / rr;
  t.tx = base1.x - (t.a * rev1.x - t.b * rev1.y);
  t.ty = base1.y - (t.b * rev1.x + t.a * rev1.y);
  out = t;
  return true;
}

bool IsInk(const uint8_t* p) { return (p[0] + p[1] * 2 + p[2]) / 4 < kInkBelow; }

Ink Classify(bool baseInk, bool revInk) {
  if (baseInk && revInk)
    return Ink::Both;
  return baseInk ? Ink::BaseOnly : revInk ? Ink::RevOnly : Ink::None;
}

void TintBgra(Ink k, uint8_t o[4]) {
  // B, G, R, A
  switch (k) {
  case Ink::BaseOnly: o[0] = 235; o[1] = 120; o[2] = 40; break; // blue: only on the base (REQ-392, D-2026-10-06-m)
  case Ink::RevOnly: o[0] = 40; o[1] = 40; o[2] = 230; break;  // red: only on the revision
  case Ink::Both: o[0] = o[1] = o[2] = 70; break;
  default: o[0] = o[1] = o[2] = 255; break;
  }
  o[3] = 255;
}

void ResampleAligned(const pdfview::Bitmap& rev, float revHPt, float revPxPerPt, const Transform& revToBase, int outW, int outH,
                     float basePxPerPt, float baseHPt, pdfview::Bitmap& out) {
  out.w = outW;
  out.h = outH;
  out.bgra.assign(static_cast<size_t>(outW) * static_cast<size_t>(outH) * 4u, 255);
  if (rev.w <= 0 || rev.h <= 0 || revToBase.Scale() < 1e-9)
    return;
  const Transform inv = revToBase.Inverse();
  for (int oy = 0; oy < outH; ++oy) {
    uint8_t* row = &out.bgra[static_cast<size_t>(oy) * static_cast<size_t>(outW) * 4u];
    for (int ox = 0; ox < outW; ++ox) {
      const Pt basePt{(ox + 0.5) / basePxPerPt, baseHPt - (oy + 0.5) / basePxPerPt};
      const Pt rp = inv.Apply(basePt);
      const double fx = rp.x * revPxPerPt - 0.5, fy = (revHPt - rp.y) * revPxPerPt - 0.5;
      const int x0 = static_cast<int>(std::floor(fx)), y0 = static_cast<int>(std::floor(fy));
      if (x0 < -1 || y0 < -1 || x0 >= rev.w || y0 >= rev.h)
        continue; // off the revision's page: white
      const double tx = fx - x0, ty = fy - y0;
      for (int c = 0; c < 3; ++c) {
        const auto at = [&](int x, int y) -> double {
          if (x < 0 || y < 0 || x >= rev.w || y >= rev.h)
            return 255.0;
          return rev.bgra[(static_cast<size_t>(y) * static_cast<size_t>(rev.w) + static_cast<size_t>(x)) * 4u + static_cast<size_t>(c)];
        };
        const double v = (at(x0, y0) * (1 - tx) + at(x0 + 1, y0) * tx) * (1 - ty) + (at(x0, y0 + 1) * (1 - tx) + at(x0 + 1, y0 + 1) * tx) * ty;
        row[ox * 4 + c] = static_cast<uint8_t>(std::clamp(v + 0.5, 0.0, 255.0));
      }
    }
  }
}

std::vector<PixRect> BoxesToPixels(const std::vector<pdfview::ObjBox>& boxes, const Transform& toBase, double baseHPt, double pxPerPt, int w, int h) {
  std::vector<PixRect> out;
  for (const pdfview::ObjBox& b : boxes) {
    double lo[2] = {1e30, 1e30}, hi[2] = {-1e30, -1e30};
    for (const Pt c : {Pt{b.x0, b.y0}, Pt{b.x1, b.y0}, Pt{b.x0, b.y1}, Pt{b.x1, b.y1}}) {
      const Pt q = toBase.Apply(c);
      lo[0] = std::min(lo[0], q.x);
      hi[0] = std::max(hi[0], q.x);
      lo[1] = std::min(lo[1], q.y);
      hi[1] = std::max(hi[1], q.y);
    }
    PixRect r;
    r.x0 = std::max(0, static_cast<int>(std::floor(lo[0] * pxPerPt)) - 1);
    r.x1 = std::min(w, static_cast<int>(std::ceil(hi[0] * pxPerPt)) + 1);
    r.y0 = std::max(0, static_cast<int>(std::floor((baseHPt - hi[1]) * pxPerPt)) - 1);
    r.y1 = std::min(h, static_cast<int>(std::ceil((baseHPt - lo[1]) * pxPerPt)) + 1);
    if (r.x1 > r.x0 && r.y1 > r.y0)
      out.push_back(r);
  }
  return out;
}

void MarkOnlyIn(const pdfview::Bitmap& sheet, const pdfview::Bitmap& other, const uint8_t bgr[3], pdfview::Bitmap& out, double pxPerPt,
                const std::vector<PixRect>& units) {
  const int w = sheet.w, h = sheet.h;
  out.w = w;
  out.h = h;
  out.bgra.assign(sheet.bgra.size(), 0);
  if (other.w != w || other.h != h)
    return;
  const size_t n = static_cast<size_t>(w) * static_cast<size_t>(h);
  const double ppp = std::max(0.1, pxPerPt);
  // The other sheet "has" a mark near here when anything on it is even faintly dark within about half a point: a mark drawn
  // onto the base's pixel grid through the alignment is blurred a little, and a faint grey is still the same mark.
  const int tol = std::clamp(static_cast<int>(std::lround(0.5 * ppp)), 1, 3);
  const auto faintIn = [&](const pdfview::Bitmap& bm, int x, int y) {
    if (x < 0 || y < 0 || x >= w || y >= h)
      return false;
    const uint8_t* p = &bm.bgra[(static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4u];
    return (p[0] + p[1] * 2 + p[2]) / 4 < 225;
  };
  const auto faintAt = [&](int x, int y) { return faintIn(other, x, y); };
  std::vector<uint8_t> ink(n, 0), only(n, 0);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const size_t at = static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x);
      if (!IsInk(&sheet.bgra[at * 4u]))
        continue;
      ink[at] = 1;
      bool near = false;
      for (int dy = -tol; dy <= tol && !near; ++dy)
        for (int dx = -tol; dx <= tol && !near; ++dx)
          near = faintAt(x + dx, y + dy);
      only[at] = near ? 0 : 1;
    }
  // Is a box (a word, a text run) changed? Judged strictly, pixel on pixel: of all the ink the two sheets have in the box, the
  // share that has no ink (even faint) at the very same pixel on the other sheet. The sheets are aligned to a fraction of a
  // pixel and drawn by the same renderer, so the same word differs by almost nothing (0 to 3 % on a real pair) while a word with
  // even one changed digit differs by 8 % or more; the half-point tolerance used to colour pixels is far too loose for this
  // (one digit lies within it of another). Two running 2-D sums make a box one lookup.
  std::vector<uint32_t> diffSum(static_cast<size_t>(w + 1) * static_cast<size_t>(h + 1), 0), inkSum(diffSum.size(), 0);
  for (int y = 0; y < h; ++y) {
    uint32_t dRow = 0, iRow = 0;
    for (int x = 0; x < w; ++x) {
      const size_t at = (static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4u;
      const auto lum = [&](const pdfview::Bitmap& m) { const uint8_t* q = &m.bgra[at]; return (q[0] + q[1] * 2 + q[2]) / 4; };
      const bool a = lum(sheet) < 110, b = lum(other) < 110; // only well-covered pixels: a blurred edge is not a mark here
      iRow += (a ? 1u : 0u) + (b ? 1u : 0u);
      const auto darker = [&](const pdfview::Bitmap& m) { const uint8_t* q = &m.bgra[at]; return (q[0] + q[1] * 2 + q[2]) / 4 < 242; }; // any real darkening counts as ink here
      dRow += (a && !darker(other) ? 1u : 0u) + (b && !darker(sheet) ? 1u : 0u);
      const size_t cell = static_cast<size_t>(y + 1) * static_cast<size_t>(w + 1) + static_cast<size_t>(x + 1), up = static_cast<size_t>(y) * static_cast<size_t>(w + 1) + static_cast<size_t>(x + 1);
      diffSum[cell] = diffSum[up] + dRow;
      inkSum[cell] = inkSum[up] + iRow;
    }
  }
  const auto strictlyChangedIn = [&](int x0, int y0, int x1, int y1) -> bool {
    x0 = std::clamp(x0, 0, w);
    x1 = std::clamp(x1, 0, w);
    y0 = std::clamp(y0, 0, h);
    y1 = std::clamp(y1, 0, h);
    if (x1 <= x0 || y1 <= y0)
      return false;
    const auto box = [&](const std::vector<uint32_t>& sum) {
      const auto at = [&](int x, int y) { return sum[static_cast<size_t>(y) * static_cast<size_t>(w + 1) + static_cast<size_t>(x)]; };
      return static_cast<size_t>(at(x1, y1) + at(x0, y0) - at(x0, y1) - at(x1, y0));
    };
    const size_t d = box(diffSum), ink = box(inkSum);
    return d >= 6 && d * 100 >= ink * 6;
  };

  // Clean up by whole objects, not by pixel. Every connected mark of the sheet (a letter, a dash, a dot, a run of line) is
  // judged: a small one is "changed" when a sixth or more of it is new. Small marks that sit close together (the letters of a
  // word, the dots of a pattern) are one group, and a group with any changed member is coloured whole - so a changed
  // callout is red or blue all through, not letter by letter. A big connected mark (a frame, a long line) gets only its
  // new pixels coloured, and clusters of those under about 6 square points are dropped as specks.
  const size_t smallMax = static_cast<size_t>(400.0 * ppp * ppp);
  const size_t speck = static_cast<size_t>(std::max(3.0, 6.0 * ppp * ppp));
  const int gap = std::max(1, static_cast<int>(std::lround(2.0 * ppp)));
  const int textMax = static_cast<int>(30.0 * ppp); // only marks no bigger than a line of text are grouped
  struct Comp {
    int minx, miny, maxx, maxy;
    size_t count = 0, marked = 0;
    bool small = true;
    int parent = 0;
  };
  std::vector<Comp> comps;
  std::vector<int32_t> label(n, -1);
  std::vector<size_t> stack;
  const auto neighbours = [&](size_t p, auto&& visit) {
    const int x = static_cast<int>(p % static_cast<size_t>(w)), y = static_cast<int>(p / static_cast<size_t>(w));
    for (int dy = -1; dy <= 1; ++dy)
      for (int dx = -1; dx <= 1; ++dx) {
        const int nx = x + dx, ny = y + dy;
        if ((dx != 0 || dy != 0) && nx >= 0 && ny >= 0 && nx < w && ny < h)
          visit(static_cast<size_t>(ny) * static_cast<size_t>(w) + static_cast<size_t>(nx));
      }
  };
  for (size_t i0 = 0; i0 < n; ++i0) {
    if (!ink[i0] || label[i0] >= 0)
      continue;
    const int id = static_cast<int>(comps.size());
    Comp c;
    c.minx = c.maxx = static_cast<int>(i0 % static_cast<size_t>(w));
    c.miny = c.maxy = static_cast<int>(i0 / static_cast<size_t>(w));
    c.parent = id;
    label[i0] = id;
    stack.assign(1, i0);
    while (!stack.empty()) {
      const size_t p = stack.back();
      stack.pop_back();
      const int x = static_cast<int>(p % static_cast<size_t>(w)), y = static_cast<int>(p / static_cast<size_t>(w));
      c.minx = std::min(c.minx, x);
      c.maxx = std::max(c.maxx, x);
      c.miny = std::min(c.miny, y);
      c.maxy = std::max(c.maxy, y);
      ++c.count;
      c.marked += only[p];
      neighbours(p, [&](size_t q) {
        if (ink[q] && label[q] < 0) {
          label[q] = id;
          stack.push_back(q);
        }
      });
    }
    c.small = c.count <= smallMax;
    comps.push_back(c);
  }

  // Group the small, text-sized marks that lie within `gap` of each other (union-find over a coarse grid).
  const auto find = [&](int a) {
    while (comps[static_cast<size_t>(a)].parent != a) {
      comps[static_cast<size_t>(a)].parent = comps[static_cast<size_t>(comps[static_cast<size_t>(a)].parent)].parent;
      a = comps[static_cast<size_t>(a)].parent;
    }
    return a;
  };
  const int cell = std::max(16, 4 * gap);
  const int gw = w / cell + 1, gh = h / cell + 1;
  std::vector<std::vector<int>> grid(static_cast<size_t>(gw) * static_cast<size_t>(gh));
  const auto grouped = [&](const Comp& c) { return c.small && c.maxx - c.minx <= textMax && c.maxy - c.miny <= textMax; };
  for (int id = 0; id < static_cast<int>(comps.size()); ++id) {
    const Comp& c = comps[static_cast<size_t>(id)];
    if (!grouped(c))
      continue;
    for (int gy = std::max(0, (c.miny - gap) / cell); gy <= std::min(gh - 1, (c.maxy + gap) / cell); ++gy)
      for (int gx = std::max(0, (c.minx - gap) / cell); gx <= std::min(gw - 1, (c.maxx + gap) / cell); ++gx) {
        std::vector<int>& bucket = grid[static_cast<size_t>(gy) * static_cast<size_t>(gw) + static_cast<size_t>(gx)];
        for (int other : bucket) {
          const Comp& o = comps[static_cast<size_t>(other)];
          if (o.minx - gap <= c.maxx && c.minx - gap <= o.maxx && o.miny - gap <= c.maxy && c.miny - gap <= o.maxy) {
            const int ra = find(id), rb = find(other);
            if (ra != rb)
              comps[static_cast<size_t>(ra)].parent = rb;
          }
        }
        bucket.push_back(id);
      }
  }
  // A group (a word, a dot pattern) is changed when any letter of it is a sixth or more new, or a few percent of the whole
  // group is new, or the box passes the strict pixel-on-pixel test above: the last is what catches a changed number whose
  // digits all sit close to the other number's digits.
  struct Group {
    int minx = 1 << 30, miny = 1 << 30, maxx = -1, maxy = -1;
    size_t count = 0, marked = 0;
    bool anyChanged = false;
  };
  std::vector<Group> groups(comps.size());
  for (int id = 0; id < static_cast<int>(comps.size()); ++id) {
    const Comp& c = comps[static_cast<size_t>(id)];
    if (!grouped(c))
      continue;
    Group& g = groups[static_cast<size_t>(find(id))];
    g.minx = std::min(g.minx, c.minx);
    g.miny = std::min(g.miny, c.miny);
    g.maxx = std::max(g.maxx, c.maxx);
    g.maxy = std::max(g.maxy, c.maxy);
    g.count += c.count;
    g.marked += c.marked;
    g.anyChanged = g.anyChanged || c.marked * 6 >= c.count;
  }
  std::vector<uint8_t> groupChanged(comps.size(), 0);
  for (size_t id = 0; id < groups.size(); ++id) {
    const Group& g = groups[id];
    if (g.maxx < 0)
      continue;
    groupChanged[id] = g.anyChanged || (g.marked >= 3 && g.marked * 100 >= g.count * 4) || strictlyChangedIn(g.minx - 1, g.miny - 1, g.maxx + 2, g.maxy + 2) ? 1 : 0;
  }

  std::vector<uint8_t> keep(n, 0), big(n, 0);
  for (size_t p = 0; p < n; ++p) {
    if (label[p] < 0)
      continue;
    const Comp& c = comps[static_cast<size_t>(label[p])];
    if (grouped(c)) {
      if (groupChanged[static_cast<size_t>(find(label[p]))])
        keep[p] = 1;
    } else if (c.small) {
      if (c.marked * 6 >= c.count)
        keep[p] = 1; // a small mark too long to be text (a short run of line): whole when a sixth of it is new
    } else if (only[p]) {
      keep[p] = big[p] = 1;
    }
  }
  // A text run is changed or not as a whole: inside its box, ink of small marks (the letters) is coloured entirely when about
  // 6 % or more of the run's ink is new, and not coloured at all (a sliver of one letter's edge is noise) otherwise.
  for (const PixRect& u : units) {
    size_t inkN = 0, onlyN = 0;
    for (int y = u.y0; y < u.y1; ++y)
      for (int x = u.x0; x < u.x1; ++x) {
        const size_t p = static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x);
        inkN += ink[p];
        onlyN += only[p];
      }
    const bool changed = (onlyN >= 3 && onlyN * 1000 >= inkN * 25) || strictlyChangedIn(u.x0, u.y0, u.x1, u.y1);
    for (int y = u.y0; y < u.y1; ++y)
      for (int x = u.x0; x < u.x1; ++x) {
        const size_t p = static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x);
        if (!ink[p])
          continue;
        const bool smallMark = label[p] >= 0 && comps[static_cast<size_t>(label[p])].small;
        if (changed)
          keep[p] = (smallMark || only[p]) ? 1 : keep[p];
        // an unchanged run is left as the group rule judged it; clearing here hid real changes in a box that is mostly other ink

      }
  }
  std::vector<uint8_t> seen(n, 0);
  std::vector<size_t> cluster;
  for (size_t i0 = 0; i0 < n; ++i0) {
    if (!big[i0] || !keep[i0] || seen[i0])
      continue;
    cluster.clear();
    stack.assign(1, i0);
    seen[i0] = 1;
    while (!stack.empty()) {
      const size_t p = stack.back();
      stack.pop_back();
      cluster.push_back(p);
      neighbours(p, [&](size_t q) {
        if (big[q] && keep[q] && !seen[q]) {
          seen[q] = 1;
          stack.push_back(q);
        }
      });
    }
    if (cluster.size() < speck)
      for (size_t p : cluster)
        keep[p] = 0;
  }
  for (size_t i = 0; i < n; ++i)
    if (keep[i]) {
      out.bgra[i * 4u] = bgr[0];
      out.bgra[i * 4u + 1] = bgr[1];
      out.bgra[i * 4u + 2] = bgr[2];
      out.bgra[i * 4u + 3] = 255;
    }
}

void TintFromMarks(const pdfview::Bitmap& base, const pdfview::Bitmap& rev, const pdfview::Bitmap& baseOnly, const pdfview::Bitmap& revOnly,
                   pdfview::Bitmap& out) {
  out.w = base.w;
  out.h = base.h;
  out.bgra.assign(base.bgra.size(), 255);
  const size_t n = std::min({base.bgra.size(), rev.bgra.size(), baseOnly.bgra.size(), revOnly.bgra.size()}) / 4u;
  for (size_t i = 0; i < n; ++i) {
    const bool b = baseOnly.bgra[i * 4 + 3] != 0, r = revOnly.bgra[i * 4 + 3] != 0;
    Ink k = Classify(IsInk(&base.bgra[i * 4]), IsInk(&rev.bgra[i * 4]));
    if (r)
      k = Ink::RevOnly;
    else if (b)
      k = Ink::BaseOnly;
    else if (k != Ink::None)
      k = Ink::Both; // ink that is only a slight shift of the other sheet's is the same mark
    TintBgra(k, &out.bgra[i * 4]);
  }
}

void TintImage(const pdfview::Bitmap& base, const pdfview::Bitmap& rev, pdfview::Bitmap& out) {
  out.w = base.w;
  out.h = base.h;
  out.bgra.assign(base.bgra.size(), 255);
  const size_t n = std::min(base.bgra.size(), rev.bgra.size()) / 4u;
  for (size_t i = 0; i < n; ++i)
    TintBgra(Classify(IsInk(&base.bgra[i * 4]), IsInk(&rev.bgra[i * 4])), &out.bgra[i * 4]);
}

namespace {

struct Ink2 {
  std::vector<float> x, y; ///< ink pixels as page points (origin lower-left)
};

/// At most ~cap ink pixels of b, as points, evenly thinned.
Ink2 InkPoints(const pdfview::Bitmap& b, double hPt, double ppp, size_t cap) {
  size_t total = 0;
  for (size_t i = 0; i + 3 < b.bgra.size(); i += 4)
    total += IsInk(&b.bgra[i]) ? 1 : 0;
  const size_t stride = std::max<size_t>(1, total / cap);
  Ink2 out;
  size_t seen = 0;
  for (int py = 0; py < b.h; ++py)
    for (int px = 0; px < b.w; ++px)
      if (IsInk(&b.bgra[(static_cast<size_t>(py) * static_cast<size_t>(b.w) + static_cast<size_t>(px)) * 4u]) && (seen++ % stride) == 0) {
        out.x.push_back(static_cast<float>((px + 0.5) / ppp));
        out.y.push_back(static_cast<float>(hPt - (py + 0.5) / ppp));
      }
  return out;
}

/// For every pixel, the index of (about) the nearest ink pixel: a two-pass chamfer that carries the source along.
std::vector<int32_t> NearestInk(const pdfview::Bitmap& b) {
  const int w = b.w, h = b.h;
  std::vector<int32_t> nn(static_cast<size_t>(w) * static_cast<size_t>(h), -1);
  for (size_t i = 0; i < nn.size(); ++i)
    if (IsInk(&b.bgra[i * 4]))
      nn[i] = static_cast<int32_t>(i);
  const auto d2 = [w](int32_t src, int x, int y) {
    const double dx = src % w - x, dy = src / w - y;
    return dx * dx + dy * dy;
  };
  const auto relax = [&](int x, int y, int ox, int oy) {
    const int nx = x + ox, ny = y + oy;
    if (nx < 0 || ny < 0 || nx >= w || ny >= h)
      return;
    const int32_t cand = nn[static_cast<size_t>(ny) * static_cast<size_t>(w) + static_cast<size_t>(nx)];
    if (cand < 0)
      return;
    int32_t& cur = nn[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)];
    if (cur < 0 || d2(cand, x, y) < d2(cur, x, y))
      cur = cand;
  };
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      relax(x, y, -1, 0);
      relax(x, y, -1, -1);
      relax(x, y, 0, -1);
      relax(x, y, 1, -1);
    }
  for (int y = h - 1; y >= 0; --y)
    for (int x = w - 1; x >= 0; --x) {
      relax(x, y, 1, 0);
      relax(x, y, 1, 1);
      relax(x, y, 0, 1);
      relax(x, y, -1, 1);
    }
  return nn;
}

/// The shift (in bins) that best lines rev up with base, by correlating mean-removed, lightly smoothed profiles.
int BestShift(const std::vector<double>& base, const std::vector<double>& rev) {
  const auto prep = [](const std::vector<double>& v) {
    std::vector<double> s(v.size(), 0.0);
    for (size_t i = 0; i < v.size(); ++i) {
      double sum = 0;
      int n = 0;
      for (int k = -2; k <= 2; ++k) {
        const long j = static_cast<long>(i) + k;
        if (j >= 0 && j < static_cast<long>(v.size())) {
          sum += v[static_cast<size_t>(j)];
          ++n;
        }
      }
      s[i] = sum / n;
    }
    double mean = 0;
    for (double d : s)
      mean += d;
    mean /= static_cast<double>(std::max<size_t>(1, s.size()));
    for (double& d : s)
      d -= mean;
    return s;
  };
  const std::vector<double> b = prep(base), r = prep(rev);
  const long nb = static_cast<long>(b.size()), nr = static_cast<long>(r.size());
  double best = -1e300;
  long bestShift = 0;
  for (long shift = -nr + 1; shift < nb; ++shift) {
    double sum = 0;
    const long lo = std::max(0L, -shift), hi = std::min(nr, nb - shift);
    for (long i = lo; i < hi; ++i)
      sum += b[static_cast<size_t>(i + shift)] * r[static_cast<size_t>(i)];
    if (hi > lo)
      sum /= std::sqrt(static_cast<double>(hi - lo)); // a longer overlap is not a better match by length alone
    if (sum > best) {
      best = sum;
      bestShift = shift;
    }
  }
  return static_cast<int>(bestShift);
}

struct Fit {
  Transform xf;
  double confidence = 0.0;
};

/// Iterative closest point from start: pair each revision ink point with the nearest base ink point inside a gate
/// that narrows each round, fit shift + scale + rotation to the pairs, repeat.
Fit Refine(const Ink2& pts, const std::vector<int32_t>& nn, const pdfview::Bitmap& base, double baseHPt, double ppp, Transform start,
           const std::function<bool()>& cancel) {
  const double kMaxRot = 5.5 * kPi / 180.0;
  const auto nearestFor = [&](const Transform& t, size_t i, double& nx, double& ny, double& dist) {
    const Pt q = t.Apply({pts.x[i], pts.y[i]});
    const int px = static_cast<int>(std::floor(q.x * ppp)), py = static_cast<int>(std::floor((baseHPt - q.y) * ppp));
    if (px < 0 || py < 0 || px >= base.w || py >= base.h)
      return false;
    const int32_t s = nn[static_cast<size_t>(py) * static_cast<size_t>(base.w) + static_cast<size_t>(px)];
    if (s < 0)
      return false;
    nx = ((s % base.w) + 0.5) / ppp;
    ny = baseHPt - ((s / base.w) + 0.5) / ppp;
    dist = std::hypot(q.x - nx, q.y - ny);
    return true;
  };
  Transform t = start;
  double gate = std::max(30.0, 0.02 * std::max(base.w, base.h) / ppp);
  const double finalGate = std::max(2.0, 2.0 / ppp);
  for (int it = 0; it < 60; ++it) {
    if (cancel && cancel())
      break;
    gate = std::max(finalGate, gate * 0.85);
    double sx = 0, sy = 0, tx = 0, ty = 0;
    long n = 0;
    std::vector<std::pair<size_t, std::pair<double, double>>> pairs;
    for (size_t i = 0; i < pts.x.size(); ++i) {
      double nx, ny, d;
      if (!nearestFor(t, i, nx, ny, d) || d > gate)
        continue;
      pairs.push_back({i, {nx, ny}});
      sx += pts.x[i];
      sy += pts.y[i];
      tx += nx;
      ty += ny;
      ++n;
    }
    if (n < 20)
      break;
    const double pcx = sx / static_cast<double>(n), pcy = sy / static_cast<double>(n), ncx = tx / static_cast<double>(n),
                 ncy = ty / static_cast<double>(n);
    double re = 0, im = 0, den = 0; // sum conj(p - pc) * (n - nc), and |p - pc|^2
    for (const auto& pr : pairs) {
      const double ax = pts.x[pr.first] - pcx, ay = pts.y[pr.first] - pcy, bx = pr.second.first - ncx, by = pr.second.second - ncy;
      re += ax * bx + ay * by;
      im += ax * by - ay * bx;
      den += ax * ax + ay * ay;
    }
    if (den < 1e-9)
      break;
    Transform nt;
    nt.a = re / den;
    nt.b = im / den;
    nt.tx = ncx - (nt.a * pcx - nt.b * pcy);
    nt.ty = ncy - (nt.b * pcx + nt.a * pcy);
    if (std::fabs(std::atan2(nt.b, nt.a)) > kMaxRot || nt.Scale() < 0.5 || nt.Scale() > 2.0)
      break;
    const double move = std::fabs(nt.a - t.a) + std::fabs(nt.b - t.b) + std::fabs(nt.tx - t.tx) / 1000.0 + std::fabs(nt.ty - t.ty) / 1000.0;
    t = nt;
    if (gate <= finalGate && move < 1e-7)
      break;
  }
  long hit = 0;
  for (size_t i = 0; i < pts.x.size(); ++i) {
    double nx, ny, d;
    if (nearestFor(t, i, nx, ny, d) && d <= finalGate)
      ++hit;
  }
  Fit f;
  f.xf = t;
  f.confidence = pts.x.empty() ? 0.0 : static_cast<double>(hit) / static_cast<double>(pts.x.size());
  return f;
}

std::vector<double> Profile(const pdfview::Bitmap& b, bool columns) {
  std::vector<double> p(static_cast<size_t>(columns ? b.w : b.h), 0.0);
  for (int y = 0; y < b.h; ++y)
    for (int x = 0; x < b.w; ++x)
      if (IsInk(&b.bgra[(static_cast<size_t>(y) * static_cast<size_t>(b.w) + static_cast<size_t>(x)) * 4u]))
        p[static_cast<size_t>(columns ? x : b.h - 1 - y)] += 1.0; // rows are counted from the bottom, like page y
  return p;
}

} // namespace

AutoResult AutoAlign(const pdfview::Bitmap& base, double baseHPt, const pdfview::Bitmap& rev, double revHPt, double pxPerPt,
                     const std::function<bool()>& cancel) {
  AutoResult res;
  if (base.w <= 0 || rev.w <= 0 || pxPerPt <= 0.0)
    return res;
  const Ink2 pts = InkPoints(rev, revHPt, pxPerPt, 60000);
  if (pts.x.size() < 50)
    return res;
  const std::vector<int32_t> nn = NearestInk(base);

  // Two starting points: the page corners as they are, and the shift that lines the ink profiles up.
  Transform shifted;
  shifted.tx = BestShift(Profile(base, true), Profile(rev, true)) / pxPerPt;
  shifted.ty = BestShift(Profile(base, false), Profile(rev, false)) / pxPerPt;
  Fit best = Refine(pts, nn, base, baseHPt, pxPerPt, Transform{}, cancel);
  if (!(cancel && cancel())) {
    const Fit other = Refine(pts, nn, base, baseHPt, pxPerPt, shifted, cancel);
    if (other.confidence > best.confidence)
      best = other;
  }
  res.confidence = best.confidence;
  res.matched = best.confidence >= kLowConfidence;
  if (res.matched)
    res.xf = best.xf; // otherwise the identity: no match is claimed
  return res;
}

} // namespace pdfalign
