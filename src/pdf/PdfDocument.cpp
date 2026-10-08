#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "PdfDocument.hpp"

#include <fpdf_edit.h>
#include <fpdf_progressive.h>
#include <fpdf_text.h>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <chrono>
#include <fpdfview.h>

#include <algorithm>
#include <cstdio>

#if defined(_WIN32)
#include <io.h>
#endif

namespace pdfview {

std::recursive_mutex& PdfiumMutex() {
  static std::recursive_mutex m;
  return m;
}

namespace {

// FPDF_InitLibrary is idempotent inside PDFium; this only avoids relying on PdfAttach_Init having run
// (the unit tests do not link PdfAttach.cpp).
void EnsureLibrary() {
  static const bool once = [] {
    FPDF_InitLibrary();
    return true;
  }();
  (void)once;
}

// A seekable file read for FPDF_LoadCustomDocument: PDFium asks for the byte ranges it needs, so a
// 500-page file opens without reading it all, and a non-ASCII path works (FPDF_LoadDocument takes a
// narrow path only).
struct FileSource {
  FILE* f = nullptr;
  FPDF_FILEACCESS access{};
};

int GetBlock(void* param, unsigned long position, unsigned char* buf, unsigned long size) {
  FileSource* s = static_cast<FileSource*>(param);
  if (s->f == nullptr)
    return 0;
#if defined(_WIN32)
  if (_fseeki64(s->f, static_cast<long long>(position), SEEK_SET) != 0)
    return 0;
#else
  if (fseek(s->f, static_cast<long>(position), SEEK_SET) != 0)
    return 0;
#endif
  return std::fread(buf, 1, size, s->f) == size ? 1 : 0;
}

} // namespace

struct PdfDocument::Impl {
  FileSource src;
  FPDF_DOCUMENT doc = nullptr;
};

PdfDocument::OpenResult PdfDocument::Open(const std::filesystem::path& path) {
  EnsureLibrary();
  OpenResult r;
  // `d` is declared before the lock so a half-built document on an error path is destroyed AFTER the
  // lock is released (its destructor takes the same, non-recursive, lock).
  std::unique_ptr<PdfDocument> d(new PdfDocument());
  d->impl_ = std::make_unique<Impl>();
  d->path_ = path;
  std::lock_guard<std::recursive_mutex> lock(PdfiumMutex());

#if defined(_WIN32)
  d->impl_->src.f = _wfopen(path.c_str(), L"rb");
#else
  d->impl_->src.f = std::fopen(path.string().c_str(), "rb");
#endif
  if (d->impl_->src.f == nullptr) {
    r.error = "cannot open the file (missing or in use)";
    return r;
  }
#if defined(_WIN32)
  _fseeki64(d->impl_->src.f, 0, SEEK_END);
  const long long len = _ftelli64(d->impl_->src.f);
#else
  fseek(d->impl_->src.f, 0, SEEK_END);
  const long long len = ftell(d->impl_->src.f);
#endif
  if (len <= 0) {
    std::fclose(d->impl_->src.f);
    d->impl_->src.f = nullptr;
    r.error = "the file is empty";
    return r;
  }
  d->impl_->src.access.m_FileLen = static_cast<unsigned long>(len);
  d->impl_->src.access.m_GetBlock = &GetBlock;
  d->impl_->src.access.m_Param = &d->impl_->src;

  d->impl_->doc = FPDF_LoadCustomDocument(&d->impl_->src.access, nullptr);
  if (d->impl_->doc == nullptr) {
    const unsigned long err = FPDF_GetLastError();
    r.error = err == FPDF_ERR_PASSWORD   ? "the PDF is password-protected"
              : err == FPDF_ERR_FORMAT   ? "not a valid PDF (damaged or the wrong format)"
              : err == FPDF_ERR_SECURITY ? "the PDF uses an unsupported security scheme"
                                         : "the PDF could not be read";
    return r;
  }
  const int n = FPDF_GetPageCount(d->impl_->doc);
  if (n <= 0) {
    r.error = "the PDF has no pages";
    return r;
  }
  d->sizes_.resize(static_cast<size_t>(n));
  for (int i = 0; i < n; ++i) {
    FS_SIZEF s{};
    if (FPDF_GetPageSizeByIndexF(d->impl_->doc, i, &s) && s.width > 0.f && s.height > 0.f)
      d->sizes_[static_cast<size_t>(i)] = {s.width, s.height};
  }
  r.doc = std::move(d);
  return r;
}

PdfDocument::~PdfDocument() {
  std::lock_guard<std::recursive_mutex> lock(PdfiumMutex());
  if (impl_ != nullptr) {
    if (impl_->doc != nullptr)
      FPDF_CloseDocument(impl_->doc);
    if (impl_->src.f != nullptr)
      std::fclose(impl_->src.f);
  }
}

Bitmap Downscale(const Bitmap& src, int maxSide) {
  Bitmap out;
  if (src.w < 1 || src.h < 1 || maxSide < 1 || src.bgra.size() < static_cast<size_t>(src.w) * src.h * 4u)
    return out;
  const float k = std::min(1.f, static_cast<float>(maxSide) / static_cast<float>(std::max(src.w, src.h)));
  out.w = std::max(1, static_cast<int>(src.w * k));
  out.h = std::max(1, static_cast<int>(src.h * k));
  out.bgra.resize(static_cast<size_t>(out.w) * out.h * 4u);
  for (int y = 0; y < out.h; ++y) {
    const int y0 = static_cast<int>(static_cast<long long>(y) * src.h / out.h);
    const int y1 = std::max(y0 + 1, static_cast<int>(static_cast<long long>(y + 1) * src.h / out.h));
    for (int x = 0; x < out.w; ++x) {
      const int x0 = static_cast<int>(static_cast<long long>(x) * src.w / out.w);
      const int x1 = std::max(x0 + 1, static_cast<int>(static_cast<long long>(x + 1) * src.w / out.w));
      unsigned sum[4] = {0, 0, 0, 0};
      for (int yy = y0; yy < y1; ++yy) {
        const uint8_t* row = src.bgra.data() + (static_cast<size_t>(yy) * src.w + x0) * 4u;
        for (int xx = x0; xx < x1; ++xx, row += 4)
          for (int c = 0; c < 4; ++c)
            sum[c] += row[c];
      }
      const unsigned n = static_cast<unsigned>((y1 - y0) * (x1 - x0));
      uint8_t* d = out.bgra.data() + (static_cast<size_t>(y) * out.w + x) * 4u;
      for (int c = 0; c < 4; ++c)
        d[c] = static_cast<uint8_t>(sum[c] / n);
    }
  }
  return out;
}

namespace {

struct Mat {
  float a = 1.f, b = 0.f, c = 0.f, d = 1.f, e = 0.f, f = 0.f; ///< x' = a x + c y + e,  y' = b x + d y + f
};

// The transform that does \p first and then \p then.
Mat Then(const Mat& first, const Mat& then) {
  Mat r;
  r.a = then.a * first.a + then.c * first.b;
  r.c = then.a * first.c + then.c * first.d;
  r.e = then.a * first.e + then.c * first.f + then.e;
  r.b = then.b * first.a + then.d * first.b;
  r.d = then.b * first.c + then.d * first.d;
  r.f = then.b * first.e + then.d * first.f + then.f;
  return r;
}

struct SnapCtx {
  const std::function<bool()>& cancel;
  size_t maxPoints;
  Mat toViewer; ///< page user space -> the viewer's page coordinates
  std::vector<SnapPoint>& out;
  std::unordered_map<int64_t, size_t> seen; ///< quarter-point cell -> index in out (a point seen again keeps the larger weight)
  size_t visited = 0;
  bool stop = false;

  // A page point in the viewer's coordinates; false if it is not a finite number.
  bool ToViewer(const Mat& m, float x, float y, std::pair<float, float>& v) const {
    const float ux = m.a * x + m.c * y + m.e, uy = m.b * x + m.d * y + m.f;
    const float vx = toViewer.a * ux + toViewer.c * uy + toViewer.e, vy = toViewer.b * ux + toViewer.d * uy + toViewer.f;
    if (!std::isfinite(vx) || !std::isfinite(vy))
      return false;
    v = {vx, vy};
    return true;
  }

  void Add(float vx, float vy, float weight) {
    const int64_t key = (static_cast<int64_t>(std::lround(vx * 4.f)) << 32) ^ static_cast<uint32_t>(std::lround(vy * 4.f));
    const auto it = seen.find(key);
    if (it != seen.end()) {
      out[it->second].weight = std::max(out[it->second].weight, weight);
      return;
    }
    seen.emplace(key, out.size());
    out.push_back({vx, vy, weight});
    if (out.size() >= maxPoints)
      stop = true;
  }

  // One run of connected line work (viewer coordinates). Keeps the points a person would snap to and drops the noise:
  //  - a run shorter than 1.5 pt is a speck (a dot, the tip of a hatch) and gives nothing;
  //  - the ends of an open run, and every corner of a closed one, are the real thing (weight 3);
  //  - a vertex in the middle is dropped when both its neighbours are under 2.5 pt away and the line barely turns
  //    there (40 degrees): that is a curve drawn as tiny steps, not a corner;
  //  - any other vertex is kept, with weight 3 next to a long segment (6 pt or more) and weight 1 otherwise.
  void Subpath(const std::vector<std::pair<float, float>>& p, bool closed, bool hadCurve) {
    const size_t n = p.size();
    if (n < 2)
      return;
    const auto dist = [&](size_t a, size_t b) { return std::hypot(p[a].first - p[b].first, p[a].second - p[b].second); };
    float total = 0.f;
    for (size_t i = 1; i < n; ++i)
      total += dist(i, i - 1);
    if (closed)
      total += dist(0, n - 1);
    if (total < 1.5f)
      return;
    // A round shape (a circle drawn as four curves, or as a ring of short steps) is one thing with a centre and four
    // quadrant points, not a ring of vertices: the centre is the strongest point (weight 4), the quadrants weight 2.
    const bool ring = closed || dist(0, n - 1) < 0.75f;
    if (ring && n >= (hadCurve ? 4u : 12u)) {
      const size_t m = (!closed && dist(0, n - 1) < 0.75f) ? n - 1 : n; // the duplicate end point is not a vertex
      float cx = 0.f, cy = 0.f;
      for (size_t i = 0; i < m; ++i) {
        cx += p[i].first;
        cy += p[i].second;
      }
      cx /= static_cast<float>(m);
      cy /= static_cast<float>(m);
      float mean = 0.f;
      for (size_t i = 0; i < m; ++i)
        mean += std::hypot(p[i].first - cx, p[i].second - cy);
      mean /= static_cast<float>(m);
      float worst = 0.f;
      for (size_t i = 0; i < m; ++i)
        worst = std::max(worst, std::fabs(std::hypot(p[i].first - cx, p[i].second - cy) - mean));
      if (mean > 0.8f && worst / mean < (hadCurve ? 0.12f : 0.06f)) {
        size_t lo[2] = {0, 0}, hi[2] = {0, 0};
        for (size_t i = 1; i < m; ++i) {
          if (p[i].first < p[lo[0]].first) lo[0] = i;
          if (p[i].first > p[hi[0]].first) hi[0] = i;
          if (p[i].second < p[lo[1]].second) lo[1] = i;
          if (p[i].second > p[hi[1]].second) hi[1] = i;
        }
        Add(cx, cy, 4.f);
        for (size_t i : {lo[0], hi[0], lo[1], hi[1]})
          Add(p[i].first, p[i].second, 2.f);
        return;
      }
    }
    // Text drawn as outlines (every letter its own small shape) and other small, intricate shapes would put a dozen
    // points in a few pixels. Nobody snaps to the edge of a letter: a run no bigger than 14 pt that has 8 or more
    // vertices (6 with curves) and is not round gives nothing.
    {
      float minX = p[0].first, maxX = minX, minY = p[0].second, maxY = minY;
      for (const auto& q : p) {
        minX = std::min(minX, q.first);
        maxX = std::max(maxX, q.first);
        minY = std::min(minY, q.second);
        maxY = std::max(maxY, q.second);
      }
      if (std::max(maxX - minX, maxY - minY) <= 14.f && (n >= 8 || (hadCurve && n >= 6)))
        return;
    }
    for (size_t i = 0; i < n && !stop; ++i) {
      const bool hasPrev = i > 0 || closed, hasNext = i + 1 < n || closed;
      const size_t ip = (i + n - 1) % n, in = (i + 1) % n;
      const float lp = hasPrev ? dist(i, ip) : 0.f, ln = hasNext ? dist(i, in) : 0.f;
      float weight = 3.f;
      if (hasPrev && hasNext) { // a vertex in the middle of the run
        float turn = 0.f;
        if (lp > 1e-4f && ln > 1e-4f) {
          const float ax = p[i].first - p[ip].first, ay = p[i].second - p[ip].second;
          const float bx = p[in].first - p[i].first, by = p[in].second - p[i].second;
          const float c = std::clamp((ax * bx + ay * by) / (lp * ln), -1.f, 1.f);
          turn = std::acos(c) * 180.f / 3.14159265f;
        }
        if (lp < 2.5f && ln < 2.5f && turn < 40.f)
          continue;
        weight = std::max(lp, ln) >= 6.f ? 3.f : 1.f;
      }
      Add(p[i].first, p[i].second, weight);
    }
  }
};

void CollectSnap(FPDF_PAGEOBJECT obj, const Mat& outer, int depth, SnapCtx& ctx) {
  if (ctx.stop || obj == nullptr || depth > 8)
    return;
  if ((++ctx.visited & 255u) == 0 && ctx.cancel && ctx.cancel()) {
    ctx.stop = true;
    return;
  }
  FS_MATRIX fm{1, 0, 0, 1, 0, 0};
  FPDFPageObj_GetMatrix(obj, &fm);
  const Mat own{fm.a, fm.b, fm.c, fm.d, fm.e, fm.f};
  const int type = FPDFPageObj_GetType(obj);
  if (type == FPDF_PAGEOBJ_PATH) {
    const Mat t = Then(own, outer);
    const int n = FPDFPath_CountSegments(obj);
    std::vector<std::pair<float, float>> run;
    bool closed = false, curved = false;
    int bezier = 0;
    const auto flush = [&] {
      ctx.Subpath(run, closed, curved);
      run.clear();
      closed = false;
      curved = false;
    };
    for (int i = 0; i < n && !ctx.stop; ++i) {
      FPDF_PATHSEGMENT seg = FPDFPath_GetPathSegment(obj, i);
      if (seg == nullptr)
        continue;
      const int st = FPDFPathSegment_GetType(seg);
      if (st == FPDF_SEGMENT_MOVETO) {
        flush();
        bezier = 0;
      }
      if (st == FPDF_SEGMENT_BEZIERTO) { // a curve is three points: two controls, then the end that counts
        curved = true;
        if (++bezier % 3 != 0)
          continue;
      } else if (st != FPDF_SEGMENT_MOVETO) {
        bezier = 0;
      }
      float x = 0.f, y = 0.f;
      std::pair<float, float> v;
      if (FPDFPathSegment_GetPoint(seg, &x, &y) && ctx.ToViewer(t, x, y, v))
        run.push_back(v);
      if (FPDFPathSegment_GetClose(seg))
        closed = true;
    }
    flush();
  } else if (type == FPDF_PAGEOBJ_FORM) {
    const Mat inner = Then(own, outer);
    const unsigned long n = static_cast<unsigned long>(FPDFFormObj_CountObjects(obj));
    for (unsigned long i = 0; i < n && !ctx.stop; ++i)
      CollectSnap(FPDFFormObj_GetObject(obj, i), inner, depth + 1, ctx);
  }
}

// The viewer's coordinates: points from the bottom-left of the page as it is displayed. Found by asking PDFium
// where three user-space points land on a page-sized device, so rotation and the page box offset are included.
Mat ViewerMatrix(FPDF_PAGE p) {
  const float W = FPDF_GetPageWidthF(p), H = FPDF_GetPageHeightF(p);
  constexpr int kScale = 8;
  const int dw = std::max(1, static_cast<int>(std::lround(W * kScale))), dh = std::max(1, static_cast<int>(std::lround(H * kScale)));
  const float sx = static_cast<float>(dw) / std::max(1e-3f, W), sy = static_cast<float>(dh) / std::max(1e-3f, H);
  int ox = 0, oy = 0;
  FPDF_PageToDevice(p, 0, 0, dw, dh, 0, 0.0, 0.0, &ox, &oy);
  // The device grid is whole pixels, so the axes are measured over a 100-point baseline.
  int ex2 = 0, ey2 = 0, fx2 = 0, fy2 = 0;
  FPDF_PageToDevice(p, 0, 0, dw, dh, 0, 100.0, 0.0, &ex2, &ey2);
  FPDF_PageToDevice(p, 0, 0, dw, dh, 0, 0.0, 100.0, &fx2, &fy2);
  Mat toViewer; // user (x, y) -> device -> viewer (device / scale, flipped so y is up)
  const float a = static_cast<float>(ex2 - ox) / 100.f / sx, b = static_cast<float>(ey2 - oy) / 100.f / sy;
  const float c = static_cast<float>(fx2 - ox) / 100.f / sx, d = static_cast<float>(fy2 - oy) / 100.f / sy;
  toViewer.a = a;
  toViewer.c = c;
  toViewer.e = static_cast<float>(ox) / sx;
  toViewer.b = -b;
  toViewer.d = -d;
  toViewer.f = H - static_cast<float>(oy) / sy;
  return toViewer;
}

struct TextCtx {
  const std::function<bool()>& cancel;
  Mat toViewer;
  std::vector<ObjBox>& out;
  size_t visited = 0;
  bool stop = false;
};

void CollectText(FPDF_PAGEOBJECT obj, const Mat& outer, int depth, TextCtx& ctx) {
  if (ctx.stop || obj == nullptr || depth > 8)
    return;
  if ((++ctx.visited & 255u) == 0 && ctx.cancel && ctx.cancel()) {
    ctx.stop = true;
    return;
  }
  const int type = FPDFPageObj_GetType(obj);
  if (type == FPDF_PAGEOBJ_TEXT) {
    float l = 0.f, b = 0.f, r = 0.f, t = 0.f;
    if (!FPDFPageObj_GetBounds(obj, &l, &b, &r, &t))
      return;
    float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
    for (const std::pair<float, float> c : {std::pair<float, float>{l, b}, {r, b}, {l, t}, {r, t}}) {
      const float ux = outer.a * c.first + outer.c * c.second + outer.e, uy = outer.b * c.first + outer.d * c.second + outer.f;
      const float vx = ctx.toViewer.a * ux + ctx.toViewer.c * uy + ctx.toViewer.e, vy = ctx.toViewer.b * ux + ctx.toViewer.d * uy + ctx.toViewer.f;
      if (!std::isfinite(vx) || !std::isfinite(vy))
        return;
      x0 = std::min(x0, vx);
      x1 = std::max(x1, vx);
      y0 = std::min(y0, vy);
      y1 = std::max(y1, vy);
    }
    if (x1 - x0 >= 0.5f && y1 - y0 >= 0.5f)
      ctx.out.push_back({x0, y0, x1, y1});
  } else if (type == FPDF_PAGEOBJ_FORM) {
    FS_MATRIX fm{1, 0, 0, 1, 0, 0};
    FPDFPageObj_GetMatrix(obj, &fm);
    const Mat inner = Then(Mat{fm.a, fm.b, fm.c, fm.d, fm.e, fm.f}, outer);
    const unsigned long n = static_cast<unsigned long>(FPDFFormObj_CountObjects(obj));
    for (unsigned long i = 0; i < n && !ctx.stop; ++i)
      CollectText(FPDFFormObj_GetObject(obj, i), inner, depth + 1, ctx);
  }
}

} // namespace

bool PdfDocument::SnapPoints(int page, std::vector<SnapPoint>& out, const std::function<bool()>& cancel,
                             size_t maxPoints) {
  out.clear();
  if (page < 0 || page >= PageCount())
    return false;
  std::lock_guard<std::recursive_mutex> lock(PdfiumMutex());
  FPDF_PAGE p = FPDF_LoadPage(impl_->doc, page);
  if (p == nullptr)
    return false;
  const Mat toViewer = ViewerMatrix(p);
  SnapCtx ctx{cancel, maxPoints, toViewer, out, {}, 0, false};
  const int n = FPDFPage_CountObjects(p);
  for (int i = 0; i < n && !ctx.stop; ++i)
    CollectSnap(FPDFPage_GetObject(p, i), Mat{}, 0, ctx);
  FPDF_ClosePage(p);
  if (cancel && cancel()) {
    out.clear();
    return false;
  }
  return true;
}

namespace {

// REQ-395: text runs (string and box) and straight strokes, in viewer coordinates.
struct AuditCtx {
  const std::function<bool()>& cancel;
  Mat toViewer;
  FPDF_TEXTPAGE textPage;
  std::vector<DimText>& texts;
  std::vector<DimSeg>& segs;
  size_t visited = 0;
  bool stop = false;

  bool ToViewer(const Mat& m, float x, float y, float& vx, float& vy) const {
    const float ux = m.a * x + m.c * y + m.e, uy = m.b * x + m.d * y + m.f;
    vx = toViewer.a * ux + toViewer.c * uy + toViewer.e;
    vy = toViewer.b * ux + toViewer.d * uy + toViewer.f;
    return std::isfinite(vx) && std::isfinite(vy);
  }
  void Seg(float ax, float ay, float bx, float by) {
    if (std::fabs(ax - bx) > 0.f || std::fabs(ay - by) > 0.f)
      segs.push_back({ax, ay, bx, by});
  }
};

void CollectAudit(FPDF_PAGEOBJECT obj, const Mat& outer, int depth, AuditCtx& ctx) {
  if (ctx.stop || obj == nullptr || depth > 8)
    return;
  if ((++ctx.visited & 255u) == 0 && ctx.cancel && ctx.cancel()) {
    ctx.stop = true;
    return;
  }
  const int type = FPDFPageObj_GetType(obj);
  if (type == FPDF_PAGEOBJ_TEXT) {
    float l = 0.f, b = 0.f, r = 0.f, t = 0.f;
    if (!FPDFPageObj_GetBounds(obj, &l, &b, &r, &t))
      return;
    float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
    for (const std::pair<float, float> c : {std::pair<float, float>{l, b}, {r, b}, {l, t}, {r, t}}) {
      float vx = 0.f, vy = 0.f;
      if (!ctx.ToViewer(outer, c.first, c.second, vx, vy))
        return;
      x0 = std::min(x0, vx);
      x1 = std::max(x1, vx);
      y0 = std::min(y0, vy);
      y1 = std::max(y1, vy);
    }
    const unsigned long bytes = FPDFTextObj_GetText(obj, ctx.textPage, nullptr, 0);
    if (bytes < 4 || bytes > 4096)
      return;
    std::vector<FPDF_WCHAR> w(bytes / sizeof(FPDF_WCHAR) + 1, 0);
    FPDFTextObj_GetText(obj, ctx.textPage, w.data(), bytes);
    std::string utf8; // the drawings' value text is ASCII; anything else is kept as '?' (it cannot be a length anyway)
    for (size_t i = 0; i < w.size() && w[i] != 0; ++i)
      utf8.push_back(w[i] < 0x80 ? static_cast<char>(w[i]) : (w[i] == 0x2019 || w[i] == 0x2032 ? '\'' : w[i] == 0x201D || w[i] == 0x2033 ? '"' : '?'));
    while (!utf8.empty() && static_cast<unsigned char>(utf8.back()) <= ' ') // PDFium may end a run with a space or a break
      utf8.pop_back();
    if (!utf8.empty())
      ctx.texts.push_back({std::move(utf8), x0, y0, x1, y1});
  } else if (type == FPDF_PAGEOBJ_PATH) {
    FS_MATRIX fm{1, 0, 0, 1, 0, 0};
    FPDFPageObj_GetMatrix(obj, &fm);
    const Mat t = Then(Mat{fm.a, fm.b, fm.c, fm.d, fm.e, fm.f}, outer);
    const int n = FPDFPath_CountSegments(obj);
    float fx = 0.f, fy = 0.f, px = 0.f, py = 0.f; // the subpath's first point and the previous one
    bool have = false;
    for (int i = 0; i < n && !ctx.stop; ++i) {
      FPDF_PATHSEGMENT seg = FPDFPath_GetPathSegment(obj, i);
      if (seg == nullptr)
        continue;
      const int st = FPDFPathSegment_GetType(seg);
      float x = 0.f, y = 0.f, vx = 0.f, vy = 0.f;
      if (!FPDFPathSegment_GetPoint(seg, &x, &y) || !ctx.ToViewer(t, x, y, vx, vy)) {
        have = false;
        continue;
      }
      if (st == FPDF_SEGMENT_MOVETO) {
        fx = px = vx;
        fy = py = vy;
        have = true;
      } else if (st == FPDF_SEGMENT_LINETO) {
        if (have)
          ctx.Seg(px, py, vx, vy);
        px = vx;
        py = vy;
      } else { // a curve is not a dimension line; its end only moves the pen
        px = vx;
        py = vy;
      }
      if (FPDFPathSegment_GetClose(seg) && have)
        ctx.Seg(px, py, fx, fy);
    }
  } else if (type == FPDF_PAGEOBJ_FORM) {
    FS_MATRIX fm{1, 0, 0, 1, 0, 0};
    FPDFPageObj_GetMatrix(obj, &fm);
    const Mat inner = Then(Mat{fm.a, fm.b, fm.c, fm.d, fm.e, fm.f}, outer);
    const unsigned long n = static_cast<unsigned long>(FPDFFormObj_CountObjects(obj));
    for (unsigned long i = 0; i < n && !ctx.stop; ++i)
      CollectAudit(FPDFFormObj_GetObject(obj, i), inner, depth + 1, ctx);
  }
}

} // namespace

bool PdfDocument::AuditPageData(int page, std::vector<DimText>& texts, std::vector<DimSeg>& segs,
                                const std::function<bool()>& cancel) {
  texts.clear();
  segs.clear();
  if (page < 0 || page >= PageCount())
    return false;
  std::lock_guard<std::recursive_mutex> lock(PdfiumMutex());
  FPDF_PAGE p = FPDF_LoadPage(impl_->doc, page);
  if (p == nullptr)
    return false;
  FPDF_TEXTPAGE tp = FPDFText_LoadPage(p);
  if (tp == nullptr) {
    FPDF_ClosePage(p);
    return false;
  }
  AuditCtx ctx{cancel, ViewerMatrix(p), tp, texts, segs, 0, false};
  const int n = FPDFPage_CountObjects(p);
  for (int i = 0; i < n && !ctx.stop; ++i)
    CollectAudit(FPDFPage_GetObject(p, i), Mat{}, 0, ctx);
  FPDFText_ClosePage(tp);
  FPDF_ClosePage(p);
  if (cancel && cancel()) {
    texts.clear();
    segs.clear();
    return false;
  }
  return true;
}

bool PdfDocument::TextBoxes(int page, std::vector<ObjBox>& out, const std::function<bool()>& cancel) {
  out.clear();
  if (page < 0 || page >= PageCount())
    return false;
  std::lock_guard<std::recursive_mutex> lock(PdfiumMutex());
  FPDF_PAGE p = FPDF_LoadPage(impl_->doc, page);
  if (p == nullptr)
    return false;
  TextCtx ctx{cancel, ViewerMatrix(p), out, 0, false};
  const int n = FPDFPage_CountObjects(p);
  for (int i = 0; i < n && !ctx.stop; ++i)
    CollectText(FPDFPage_GetObject(p, i), Mat{}, 0, ctx);
  FPDF_ClosePage(p);
  if (cancel && cancel()) {
    out.clear();
    return false;
  }
  return true;
}

bool PdfDocument::RenderPage(int page, int w, int h, Bitmap& out, const std::function<bool()>& cancel, int flags,
                             int budgetMs, int sliceMs, const std::function<void()>& between) {
  out = {};
  if (page < 0 || page >= PageCount() || w < 1 || h < 1)
    return false;
  std::lock_guard<std::recursive_mutex> lock(PdfiumMutex());
  FPDF_PAGE p = FPDF_LoadPage(impl_->doc, page);
  if (p == nullptr)
    return false;

  out.w = w;
  out.h = h;
  out.bgra.assign(static_cast<size_t>(w) * static_cast<size_t>(h) * 4u, 0xFF);
  FPDF_BITMAP bm = FPDFBitmap_CreateEx(w, h, FPDFBitmap_BGRA, out.bgra.data(), w * 4);
  if (bm == nullptr) {
    FPDF_ClosePage(p);
    out = {};
    return false;
  }
  FPDFBitmap_FillRect(bm, 0, 0, w, h, 0xFFFFFFFFu);

  struct Pause {
    const std::function<bool()>* cancel;
    std::chrono::steady_clock::time_point deadline;
    bool budgeted;
    std::chrono::steady_clock::time_point sliceEnd;
    bool sliced;
  } ctx{&cancel,
        std::chrono::steady_clock::now() + std::chrono::milliseconds(budgetMs),
        budgetMs > 0,
        std::chrono::steady_clock::now() + std::chrono::milliseconds(sliceMs),
        sliceMs > 0 && static_cast<bool>(between)};
  auto overBudget = [&ctx] { return ctx.budgeted && std::chrono::steady_clock::now() >= ctx.deadline; };
  IFSDK_PAUSE pause{};
  pause.version = 1;
  pause.user = &ctx;
  pause.NeedToPauseNow = [](IFSDK_PAUSE* ip) -> FPDF_BOOL {
    const Pause* c = static_cast<Pause*>(ip->user);
    const auto now = std::chrono::steady_clock::now();
    return ((*c->cancel)() || (c->budgeted && now >= c->deadline) || (c->sliced && now >= c->sliceEnd)) ? TRUE : FALSE;
  };

  int status = FPDF_RenderPageBitmap_Start(bm, p, 0, 0, w, h, 0, flags, &pause);
  while (status == FPDF_RENDER_TOBECONTINUED) {
    if (cancel()) {
      FPDF_RenderPage_Close(p);
      FPDFBitmap_Destroy(bm);
      FPDF_ClosePage(p);
      out = {};
      return false;
    }
    if (overBudget()) {  // keep what is drawn: a draft
      FPDF_RenderPage_Close(p);
      FPDFBitmap_Destroy(bm);
      FPDF_ClosePage(p);
      out.partial = true;
      return true;
    }
    if (ctx.sliced) {  // let other pages (the thumbnail strip) draw, then carry on
      between();
      ctx.sliceEnd = std::chrono::steady_clock::now() + std::chrono::milliseconds(sliceMs);
    }
    status = FPDF_RenderPage_Continue(p, &pause);
  }
  FPDF_RenderPage_Close(p);
  FPDFBitmap_Destroy(bm);
  FPDF_ClosePage(p);
  if (status != FPDF_RENDER_DONE) {
    out = {};
    return false;
  }
  return true;
}

} // namespace pdfview
