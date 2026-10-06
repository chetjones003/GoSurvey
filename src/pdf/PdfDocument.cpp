#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "PdfDocument.hpp"

#include <fpdf_edit.h>
#include <fpdf_progressive.h>
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
  std::vector<std::pair<float, float>>& out;
  std::unordered_set<int64_t> seen;
  size_t visited = 0;
  bool stop = false;

  void Add(const Mat& m, float x, float y) {
    const float ux = m.a * x + m.c * y + m.e, uy = m.b * x + m.d * y + m.f;
    const float vx = toViewer.a * ux + toViewer.c * uy + toViewer.e, vy = toViewer.b * ux + toViewer.d * uy + toViewer.f;
    if (!std::isfinite(vx) || !std::isfinite(vy))
      return;
    const int64_t key = (static_cast<int64_t>(std::lround(vx * 4.f)) << 32) ^ static_cast<uint32_t>(std::lround(vy * 4.f));
    if (seen.insert(key).second) { // one point per quarter-point cell
      out.emplace_back(vx, vy);
      if (out.size() >= maxPoints)
        stop = true;
    }
  }
};

void CollectSnap(FPDF_PAGEOBJECT obj, const Mat& outer, int depth, SnapCtx& ctx) {
  if (ctx.stop || obj == nullptr || depth > 8)
    return;
  if ((++ctx.visited & 255u) == 0 && ctx.cancel()) {
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
    int bezier = 0;
    for (int i = 0; i < n && !ctx.stop; ++i) {
      FPDF_PATHSEGMENT seg = FPDFPath_GetPathSegment(obj, i);
      if (seg == nullptr)
        continue;
      const int st = FPDFPathSegment_GetType(seg);
      if (st == FPDF_SEGMENT_BEZIERTO) { // a curve is three points: two controls, then the end that counts
        if (++bezier % 3 != 0)
          continue;
      } else {
        bezier = 0;
      }
      float x = 0.f, y = 0.f;
      if (FPDFPathSegment_GetPoint(seg, &x, &y))
        ctx.Add(t, x, y);
    }
  } else if (type == FPDF_PAGEOBJ_FORM) {
    const Mat inner = Then(own, outer);
    const unsigned long n = static_cast<unsigned long>(FPDFFormObj_CountObjects(obj));
    for (unsigned long i = 0; i < n && !ctx.stop; ++i)
      CollectSnap(FPDFFormObj_GetObject(obj, i), inner, depth + 1, ctx);
  }
}

} // namespace

bool PdfDocument::SnapPoints(int page, std::vector<std::pair<float, float>>& out, const std::function<bool()>& cancel,
                             size_t maxPoints) {
  out.clear();
  if (page < 0 || page >= PageCount())
    return false;
  std::lock_guard<std::recursive_mutex> lock(PdfiumMutex());
  FPDF_PAGE p = FPDF_LoadPage(impl_->doc, page);
  if (p == nullptr)
    return false;
  // The viewer's coordinates: points from the bottom-left of the page as it is displayed. Found by asking PDFium
  // where three user-space points land on a page-sized device, so rotation and the page box offset are included.
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
  SnapCtx ctx{cancel, maxPoints, toViewer, out, {}, 0, false};
  const int n = FPDFPage_CountObjects(p);
  for (int i = 0; i < n && !ctx.stop; ++i)
    CollectSnap(FPDFPage_GetObject(p, i), Mat{}, 0, ctx);
  FPDF_ClosePage(p);
  if (cancel()) {
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
