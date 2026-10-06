#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "PdfDocument.hpp"

#include <fpdf_progressive.h>
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
