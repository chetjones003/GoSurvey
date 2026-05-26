#include "PdfAttach.hpp"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>   // OutputDebugStringA, QueryPerformanceCounter
#include <ShlObj.h>    // SHCreateItemFromParsingName, IShellItemImageFactory
#include <wincodec.h>  // WIC — decode PNG output from Windows.Data.Pdf

#include <fpdfview.h>
#include <fpdf_edit.h>
#include <fpdf_text.h>
#include <fpdf_thumbnail.h>   // FPDFPage_GetThumbnailAsBitmap (fast embedded path)
#include <fpdf_progressive.h> // FPDF_RenderPageBitmap_Start / IFSDK_PAUSE (cancellable)

// C++/WinRT: Windows.Data.Pdf hardware-accelerated renderer.
// Enabled when the cppwinrt SDK headers are found by CMake.
#ifdef PDF_WINRT_ENABLED
#  define WINRT_LEAN_AND_MEAN
#  include <winrt/Windows.Foundation.h>
#  include <winrt/Windows.Data.Pdf.h>
#  include <winrt/Windows.Storage.h>
#  include <winrt/Windows.Storage.Streams.h>
#  include <winrt/Windows.Graphics.Imaging.h>
#endif

#include <GL/glew.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

// ---------------------------------------------------------------------------
// Timing helpers — output goes to the VS Output window (Debug > Windows > Output)
// ---------------------------------------------------------------------------
namespace {
struct PdfTimer {
  LARGE_INTEGER start{};
  PdfTimer() { QueryPerformanceCounter(&start); }
  double ms() const {
    LARGE_INTEGER end{}, freq{};
    QueryPerformanceCounter(&end);
    QueryPerformanceFrequency(&freq);
    return static_cast<double>(end.QuadPart - start.QuadPart) * 1000.0 /
           static_cast<double>(freq.QuadPart);
  }
};
void PdfLog(const char* fmt, ...) {
  char buf[512];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  OutputDebugStringA(buf);
}
} // namespace

// ---------------------------------------------------------------------------
// Shared: extract pixel data from an HBITMAP into a top-down BGRA buffer.
// Alpha is forced to 0xFF (shell thumbnails use BGRX / undefined alpha).
// ---------------------------------------------------------------------------
static bool ExtractHBitmapPixels(HBITMAP hBmp,
                                  std::vector<uint8_t>& outBuf,
                                  int* outW, int* outH) {
  if (!hBmp) return false;
  BITMAP bm{};
  GetObject(hBmp, sizeof(bm), &bm);
  const int w = bm.bmWidth;
  const int h = std::abs(bm.bmHeight);
  if (w <= 0 || h <= 0) return false;

  BITMAPINFOHEADER bi{};
  bi.biSize        = sizeof(bi);
  bi.biWidth       = w;
  bi.biHeight      = -h;   // negative = top-down rows
  bi.biPlanes      = 1;
  bi.biBitCount    = 32;
  bi.biCompression = BI_RGB;

  outBuf.resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
  HDC dc = GetDC(nullptr);
  if (!dc) { outBuf.clear(); return false; }
  const int rows = GetDIBits(dc, hBmp, 0, static_cast<UINT>(h),
                              outBuf.data(),
                              reinterpret_cast<BITMAPINFO*>(&bi),
                              DIB_RGB_COLORS);
  ReleaseDC(nullptr, dc);
  if (rows <= 0) { outBuf.clear(); return false; }
  for (size_t i = 3; i < outBuf.size(); i += 4)
    outBuf[i] = 0xFF;  // force opaque alpha
  if (outW) *outW = w;
  if (outH) *outH = h;
  return true;
}

// ---------------------------------------------------------------------------
// Windows Shell thumbnail — shared implementation.
// comInit=true: call CoInitializeEx (background thread, no existing COM).
// comInit=false: COM already initialized on calling thread (main thread).
// ---------------------------------------------------------------------------
static bool GetShellThumbnailImpl(const std::string& filePathUtf8, int maxPx,
                                   SIIGBF flags, bool comInit,
                                   std::vector<uint8_t>& outBuf,
                                   int* outW, int* outH) {
  bool doUninit = false;
  if (comInit) {
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    doUninit = (hr == S_OK);
  }

  const int wn = MultiByteToWideChar(CP_UTF8, 0, filePathUtf8.c_str(), -1, nullptr, 0);
  std::wstring wpath(static_cast<size_t>(wn), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, filePathUtf8.c_str(), -1, wpath.data(), wn);
  if (!wpath.empty() && wpath.back() == L'\0') wpath.pop_back();

  bool ok = false;
  IShellItem* pItem = nullptr;
  if (SUCCEEDED(SHCreateItemFromParsingName(
          wpath.c_str(), nullptr,
          IID_IShellItem, reinterpret_cast<void**>(&pItem))) && pItem) {
    IShellItemImageFactory* pFac = nullptr;
    if (SUCCEEDED(pItem->QueryInterface(
            IID_IShellItemImageFactory,
            reinterpret_cast<void**>(&pFac))) && pFac) {
      const SIZE sz  = { static_cast<LONG>(maxPx), static_cast<LONG>(maxPx) };
      HBITMAP   hBmp = nullptr;
      const HRESULT hrImg = pFac->GetImage(sz, flags, &hBmp);
      PdfLog("[PDF] ShellThumb GetImage hr=0x%08X flags=0x%X hBmp=%p\n",
             static_cast<unsigned>(hrImg), static_cast<unsigned>(flags),
             static_cast<void*>(hBmp));
      if (SUCCEEDED(hrImg) && hBmp) {
        ok = ExtractHBitmapPixels(hBmp, outBuf, outW, outH);
        DeleteObject(hBmp);
      }
      pFac->Release();
    }
    pItem->Release();
  }
  if (doUninit) CoUninitialize();
  return ok;
}

// Fast path: read only from Windows' thumbnail cache (never blocks).
// Call on the MAIN THREAD (COM/STA already initialized by PdfAttach_Init).
static bool GetShellThumbnailCached(const std::string& filePathUtf8, int maxPx,
                                     std::vector<uint8_t>& outBuf,
                                     int* outW, int* outH) {
  return GetShellThumbnailImpl(
      filePathUtf8, maxPx,
      static_cast<SIIGBF>(SIIGBF_RESIZETOFIT | SIIGBF_THUMBNAILONLY |
                           SIIGBF_INCACHEONLY),
      /*comInit=*/false, outBuf, outW, outH);
}

// ---------------------------------------------------------------------------
// WIC helper: decode a PNG/JPEG/BMP byte buffer to a top-down BGRA image.
// COM must already be initialized on the calling thread.
// ---------------------------------------------------------------------------
static bool WicDecodeImageToBgra(const uint8_t* data, size_t dataSize,
                                  std::vector<uint8_t>& outBgra,
                                  int* outW, int* outH) {
  if (!data || dataSize == 0) return false;

  // Wrap the byte buffer in a COM IStream (zero-copy via HGLOBAL).
  HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, dataSize);
  if (!hMem) return false;
  void* pMem = GlobalLock(hMem);
  if (!pMem) { GlobalFree(hMem); return false; }
  std::memcpy(pMem, data, dataSize);
  GlobalUnlock(hMem);

  IStream* pStream = nullptr;
  if (FAILED(CreateStreamOnHGlobal(hMem, /*fDeleteOnRelease=*/TRUE, &pStream))) {
    GlobalFree(hMem);
    return false;
  }

  bool ok = false;
  IWICImagingFactory* wic = nullptr;
  if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                  CLSCTX_INPROC_SERVER,
                                  IID_IWICImagingFactory,
                                  reinterpret_cast<void**>(&wic))) && wic) {
    IWICBitmapDecoder* decoder = nullptr;
    if (SUCCEEDED(wic->CreateDecoderFromStream(
            pStream, nullptr, WICDecodeMetadataCacheOnLoad, &decoder)) && decoder) {
      IWICBitmapFrameDecode* frame = nullptr;
      if (SUCCEEDED(decoder->GetFrame(0, &frame)) && frame) {
        IWICFormatConverter* conv = nullptr;
        if (SUCCEEDED(wic->CreateFormatConverter(&conv)) && conv) {
          if (SUCCEEDED(conv->Initialize(
                  frame, GUID_WICPixelFormat32bppBGRA,
                  WICBitmapDitherTypeNone, nullptr, 0.0,
                  WICBitmapPaletteTypeMedianCut))) {
            UINT w = 0, h = 0;
            conv->GetSize(&w, &h);
            if (w > 0 && h > 0) {
              outBgra.resize(static_cast<size_t>(w) * h * 4);
              if (SUCCEEDED(conv->CopyPixels(nullptr, w * 4,
                                              static_cast<UINT>(outBgra.size()),
                                              outBgra.data()))) {
                if (outW) *outW = static_cast<int>(w);
                if (outH) *outH = static_cast<int>(h);
                ok = true;
              } else {
                outBgra.clear();
              }
            }
          }
          conv->Release();
        }
        frame->Release();
      }
      decoder->Release();
    }
    wic->Release();
  }
  pStream->Release(); // also frees hMem (fDeleteOnRelease=TRUE)
  return ok;
}

// ---------------------------------------------------------------------------
// Try to extract the page's embedded thumbnail via pdfium.
// Many PDFs created by AutoCAD/Revit/Acrobat include a /Thumb entry in the
// page dictionary.  Extracting it is nearly instant (no rasterization).
// Returns false if no embedded thumbnail exists.
// ---------------------------------------------------------------------------
static bool TryEmbeddedThumbnail(FPDF_PAGE page,
                                  std::vector<uint8_t>& outBgra,
                                  int* outW, int* outH) {
  FPDF_BITMAP bm = FPDFPage_GetThumbnailAsBitmap(page);
  if (!bm) return false;

  const int w      = FPDFBitmap_GetWidth(bm);
  const int h      = FPDFBitmap_GetHeight(bm);
  const int stride = FPDFBitmap_GetStride(bm);
  if (w <= 0 || h <= 0) { FPDFBitmap_Destroy(bm); return false; }

  const void* buf = FPDFBitmap_GetBuffer(bm);
  outBgra.resize(static_cast<size_t>(w) * h * 4);
  for (int y = 0; y < h; ++y) {
    const uint8_t* src = static_cast<const uint8_t*>(buf) + y * stride;
    uint8_t*       dst = outBgra.data() + y * w * 4;
    std::memcpy(dst, src, static_cast<size_t>(w) * 4);
  }
  FPDFBitmap_Destroy(bm);

  if (outW) *outW = w;
  if (outH) *outH = h;
  return true;
}

// ---------------------------------------------------------------------------
// Windows.Data.Pdf fast path (C++/WinRT).
// Uses the Windows native PDF renderer (hardware-accelerated via Direct2D).
// Only compiled when PDF_WINRT_ENABLED is defined (cppwinrt headers found).
// COM must already be initialized on the calling thread (MTA is fine).
// ---------------------------------------------------------------------------
#ifdef PDF_WINRT_ENABLED
static bool RenderPageWithWinRT(const std::string& filePath, int pageIndex,
                                 int maxPx,
                                 std::vector<uint8_t>& outBgra,
                                 int* outW, int* outH) {
  try {
    // Initialise C++/WinRT on this thread (maps to CoInitializeEx MTA).
    // Safe to call multiple times; tracks init depth internally.
    winrt::init_apartment(winrt::apartment_type::multi_threaded);

    // Convert UTF-8 path to wide string for WinRT hstring.
    const int wlen = MultiByteToWideChar(CP_UTF8, 0, filePath.c_str(), -1, nullptr, 0);
    std::wstring wpath(static_cast<size_t>(wlen), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, filePath.c_str(), -1, wpath.data(), wlen);
    if (!wpath.empty() && wpath.back() == L'\0') wpath.pop_back();

    PdfTimer tTotal;

    // Obtain a StorageFile handle from the local filesystem path.
    auto sf = winrt::Windows::Storage::StorageFile::
        GetFileFromPathAsync(winrt::hstring(wpath)).get();

    // Load the PDF document (hardware decoder, no pdfium involvement).
    auto pdfDoc = winrt::Windows::Data::Pdf::PdfDocument::
        LoadFromFileAsync(sf).get();
    PdfLog("[PDF] WinRT: LoadFromFileAsync %.1f ms  (%u pages)\n",
           tTotal.ms(), static_cast<unsigned>(pdfDoc.PageCount()));

    if (pdfDoc.PageCount() == 0 ||
        static_cast<uint32_t>(pageIndex) >= pdfDoc.PageCount())
      return false;

    // Get the page and compute a destination size that preserves aspect ratio.
    auto pdfPage = pdfDoc.GetPage(static_cast<uint32_t>(pageIndex));
    auto sz = pdfPage.Size();
    const float pw = sz.Width, ph = sz.Height;
    uint32_t dw, dh;
    if (pw >= ph) {
      dw = static_cast<uint32_t>(maxPx);
      dh = std::max(1u, static_cast<uint32_t>(maxPx * ph / pw));
    } else {
      dh = static_cast<uint32_t>(maxPx);
      dw = std::max(1u, static_cast<uint32_t>(maxPx * pw / ph));
    }

    winrt::Windows::Data::Pdf::PdfPageRenderOptions opts;
    opts.DestinationWidth(dw);
    opts.DestinationHeight(dh);
    // Default BitmapEncoderId is PNG — WIC decodes it below.

    // Render into an in-memory stream.
    winrt::Windows::Storage::Streams::InMemoryRandomAccessStream stream;
    {
      PdfTimer tR;
      pdfPage.RenderToStreamAsync(stream, opts).get();
      PdfLog("[PDF] WinRT: RenderToStreamAsync %.1f ms  (%ux%u px)\n",
             tR.ms(), dw, dh);
    }

    // Read the PNG bytes from the stream.
    const uint64_t streamSize = stream.Size();
    stream.Seek(0);
    winrt::Windows::Storage::Streams::DataReader reader(stream.GetInputStreamAt(0));
    reader.LoadAsync(static_cast<uint32_t>(streamSize)).get();
    std::vector<uint8_t> pngData(static_cast<size_t>(streamSize));
    reader.ReadBytes(winrt::array_view<uint8_t>(pngData.data(), pngData.size()));

    // Decode PNG → top-down BGRA via WIC.
    const bool ok = WicDecodeImageToBgra(pngData.data(), pngData.size(),
                                          outBgra, outW, outH);
    PdfLog("[PDF] WinRT: total %.1f ms  decode %s\n",
           tTotal.ms(), ok ? "OK" : "FAILED");
    return ok;

  } catch (winrt::hresult_error const& ex) {
    PdfLog("[PDF] WinRT: hresult_error 0x%08X: %ls\n",
           static_cast<unsigned>(ex.code()),
           ex.message().c_str());
    return false;
  } catch (...) {
    PdfLog("[PDF] WinRT: unknown exception\n");
    return false;
  }
}
#endif // PDF_WINRT_ENABLED

// ---------------------------------------------------------------------------
// Internal helper: upload a row-flipped BGRA pixel buffer to a new GL texture.
// The pdfium bitmap has row 0 = top of page; we flip so GL row 0 = bottom.
// ---------------------------------------------------------------------------
static unsigned int UploadBgraTexture(const std::vector<uint8_t>& buf, int w, int h) {
  if (w <= 0 || h <= 0 || buf.empty())
    return 0;

  // Flip rows in a temporary buffer (pdfium top-down → OpenGL bottom-up).
  std::vector<uint8_t> flipped(buf.size());
  const int stride = w * 4;
  for (int y = 0; y < h; ++y) {
    const uint8_t* src = buf.data() + y * stride;
    uint8_t*       dst = flipped.data() + (h - 1 - y) * stride;
    std::memcpy(dst, src, static_cast<size_t>(stride));
  }

  GLuint tex = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_BGRA, GL_UNSIGNED_BYTE, flipped.data());
  glGenerateMipmap(GL_TEXTURE_2D);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, 0);
  return static_cast<unsigned int>(tex);
}

// ---------------------------------------------------------------------------
// Render a page to a CPU pixel buffer (no GL calls — background-thread safe).
// outBuf receives a top-down BGRA row-major image.
//
// The render uses pdfium's progressive API with an IFSDK_PAUSE callback so
// it can be interrupted when 'cancelled' is set.  If cancelled mid-render,
// returns false and clears outBuf.  Always call FPDF_RenderPage_Close on the
// same page handle after this function returns (even on cancellation).
// ---------------------------------------------------------------------------
static bool RenderPageToBuffer(FPDF_PAGE page, float dpi, int renderFlags,
                                const std::atomic<bool>& cancelled,
                                std::vector<uint8_t>& outBuf,
                                int* outW, int* outH) {
  const float w_pt = FPDF_GetPageWidthF(page);
  const float h_pt = FPDF_GetPageHeightF(page);
  const int   w    = std::max(1, static_cast<int>(w_pt * dpi / 72.f));
  const int   h    = std::max(1, static_cast<int>(h_pt * dpi / 72.f));

  PdfLog("[PDF]   RenderToBuffer %.1f DPI flags=0x%X → %dx%d px (%.0fx%.0f pts)\n",
         dpi, renderFlags, w, h, w_pt, h_pt);

  outBuf.assign(static_cast<size_t>(w) * static_cast<size_t>(h) * 4, 0xFF);
  FPDF_BITMAP bm = FPDFBitmap_CreateEx(w, h, FPDFBitmap_BGRA, outBuf.data(), w * 4);
  if (!bm) { outBuf.clear(); return false; }
  FPDFBitmap_FillRect(bm, 0, 0, w, h, 0xFFFFFFFFu);

  // IFSDK_PAUSE: checked by pdfium between individual drawing operations.
  // Returning TRUE causes RenderPageBitmap_Start to return FPDF_RENDER_TOBECONTINUED.
  struct PauseCtx { const std::atomic<bool>* cancelled; };
  PauseCtx pauseCtx{ &cancelled };
  IFSDK_PAUSE pause{};
  pause.version = 1;
  pause.user    = &pauseCtx;
  pause.NeedToPauseNow = [](IFSDK_PAUSE* p) -> FPDF_BOOL {
    return static_cast<PauseCtx*>(p->user)->cancelled->load(std::memory_order_acquire)
               ? TRUE : FALSE;
  };

  PdfTimer t;
  int status = FPDF_RenderPageBitmap_Start(bm, page, 0, 0, w, h, 0, renderFlags, &pause);
  while (status == FPDF_RENDER_TOBECONTINUED) {
    if (cancelled.load(std::memory_order_acquire)) {
      FPDF_RenderPage_Close(page);
      FPDFBitmap_Destroy(bm);
      outBuf.clear();
      PdfLog("[PDF]   RenderToBuffer: cancelled after %.1f ms\n", t.ms());
      return false;
    }
    status = FPDF_RenderPage_Continue(page, &pause);
  }
  PdfLog("[PDF]   FPDF_RenderPageBitmap(progressive): %.1f ms  status=%d\n",
         t.ms(), status);

  FPDF_RenderPage_Close(page);
  FPDFBitmap_Destroy(bm);

  if (status != FPDF_RENDER_DONE) { outBuf.clear(); return false; }
  if (outW) *outW = w;
  if (outH) *outH = h;
  return true;
}

// Non-cancellable wrapper for the main-thread rasterize path (full-quality attach).
static bool RenderPageToBufferBlocking(FPDF_PAGE page, float dpi, int renderFlags,
                                        std::vector<uint8_t>& outBuf,
                                        int* outW, int* outH) {
  const std::atomic<bool> neverCancel{false};
  return RenderPageToBuffer(page, dpi, renderFlags, neverCancel, outBuf, outW, outH);
}

// ---------------------------------------------------------------------------
// Rasterize one page at given DPI into a new GL texture (main-thread only).
// Returns 0 on failure.
// ---------------------------------------------------------------------------
static unsigned int RasterizePage(FPDF_PAGE page, float dpi, int* outW, int* outH) {
  std::vector<uint8_t> buf;
  int w = 0, h = 0;
  if (!RenderPageToBufferBlocking(page, dpi, FPDF_PRINTING, buf, &w, &h))
    return 0;
  if (outW) *outW = w;
  if (outH) *outH = h;
  PdfTimer t;
  const auto r = UploadBgraTexture(buf, w, h);
  PdfLog("[PDF]   UploadBgraTexture: %.1f ms\n", t.ms());
  return r;
}

// ---------------------------------------------------------------------------
// Snap geometry extraction
// ---------------------------------------------------------------------------
namespace {

constexpr float kCircleAspectThresh = 0.20f; // bbox width/height within 20%

// Transform a closed subpath's endpoint list into a center + radius.
// Returns false if the shape is not circle-like.
bool DetectCircle(const std::vector<float>& pts, float* cx, float* cy, float* r) {
  if (pts.size() < 4)
    return false;
  const size_t n = pts.size() / 2;
  float sumX = 0.f, sumY = 0.f;
  for (size_t i = 0; i < n; ++i) {
    sumX += pts[i * 2];
    sumY += pts[i * 2 + 1];
  }
  *cx = sumX / static_cast<float>(n);
  *cy = sumY / static_cast<float>(n);
  float sumR = 0.f;
  float maxR = 0.f, minR = 1e30f;
  for (size_t i = 0; i < n; ++i) {
    const float dx = pts[i * 2] - *cx;
    const float dy = pts[i * 2 + 1] - *cy;
    const float d  = std::sqrt(dx * dx + dy * dy);
    sumR += d;
    maxR = std::max(maxR, d);
    minR = std::min(minR, d);
  }
  *r = sumR / static_cast<float>(n);
  if (*r < 1.f)
    return false;
  // Variance check: min and max radius within ±25% of mean → circle-like.
  return (maxR - minR) / (*r + 1e-6f) < 0.25f;
}

// Apply a PDF transformation matrix to a point.
// PDF convention: x' = a*x + c*y + e,  y' = b*x + d*y + f
static void ApplyMat(const FS_MATRIX& m, float ix, float iy, float* ox, float* oy) {
  *ox = m.a * ix + m.c * iy + m.e;
  *oy = m.b * ix + m.d * iy + m.f;
}

// Concatenate two matrices so that child is applied first, then parent.
// Result(p) = parent( child(p) )
static FS_MATRIX MulMat(const FS_MATRIX& child, const FS_MATRIX& parent) {
  FS_MATRIX r;
  r.a = child.a * parent.a + child.b * parent.c;
  r.b = child.a * parent.b + child.b * parent.d;
  r.c = child.c * parent.a + child.d * parent.c;
  r.d = child.c * parent.b + child.d * parent.d;
  r.e = child.e * parent.a + child.f * parent.c + parent.e;
  r.f = child.e * parent.b + child.f * parent.d + parent.f;
  return r;
}

} // namespace

// ---------------------------------------------------------------------------
// Recursive snap extractor.  Processes one "level" of objects — either the
// top-level page objects or the children of a Form XObject.
//
// accum  — accumulated parent-to-page matrix (identity at the page level).
// depth  — recursion guard; stops at kMaxDepth to avoid infinite loops in
//           malformed PDFs that contain self-referencing forms.
// ---------------------------------------------------------------------------
static const int kMaxFormDepth = 8;

static void ExtractSnapFromObjectRange(
    int nObj, bool fromForm, FPDF_PAGE page, FPDF_PAGEOBJECT formObj,
    const FS_MATRIX& accum,
    bool doLines, bool doCircles, bool doText,
    std::vector<float>& outLines,
    std::vector<float>& outCircles,
    std::vector<float>& outTextPos,
    int depth)
{
  for (int oi = 0; oi < nObj; ++oi) {
    FPDF_PAGEOBJECT obj = fromForm
        ? FPDFFormObj_GetObject(formObj, static_cast<unsigned long>(oi))
        : FPDFPage_GetObject(page, oi);
    if (!obj)
      continue;
    const int type = FPDFPageObj_GetType(obj);

    // Get this object's local-to-parent matrix.
    FS_MATRIX objMat = {1.f, 0.f, 0.f, 1.f, 0.f, 0.f}; // identity default
    FPDFPageObj_GetMatrix(obj, &objMat);

    // Combined: first apply objMat (local→parent), then accum (parent→page).
    const FS_MATRIX combined = MulMat(objMat, accum);

    // ---- PATH ------------------------------------------------------------
    if (type == FPDF_PAGEOBJ_PATH && (doLines || doCircles)) {
      const int nSeg = FPDFPath_CountSegments(obj);
      if (nSeg <= 0)
        continue;

      float moveX = 0.f, moveY = 0.f;
      std::vector<float> subpathPts;
      bool inSubpath = false;

      auto flushSubpath = [&](bool closed) {
        if (!inSubpath)
          return;
        if (doCircles && closed && subpathPts.size() >= 6) {
          float cx = 0.f, cy = 0.f, cr = 0.f;
          if (DetectCircle(subpathPts, &cx, &cy, &cr)) {
            outCircles.push_back(cx);
            outCircles.push_back(cy);
            outCircles.push_back(cr);
            return;
          }
        }
        if (doLines && subpathPts.size() >= 4) {
          const size_t n = subpathPts.size() / 2;
          for (size_t i = 0; i + 1 < n; ++i) {
            outLines.push_back(subpathPts[i * 2]);
            outLines.push_back(subpathPts[i * 2 + 1]);
            outLines.push_back(subpathPts[(i + 1) * 2]);
            outLines.push_back(subpathPts[(i + 1) * 2 + 1]);
          }
          if (closed && n >= 2) {
            outLines.push_back(subpathPts[(n - 1) * 2]);
            outLines.push_back(subpathPts[(n - 1) * 2 + 1]);
            outLines.push_back(subpathPts[0]);
            outLines.push_back(subpathPts[1]);
          }
        }
      };

      for (int si = 0; si < nSeg; ++si) {
        FPDF_PATHSEGMENT seg = FPDFPath_GetPathSegment(obj, si);
        float sx = 0.f, sy = 0.f;
        if (!FPDFPathSegment_GetPoint(seg, &sx, &sy))
          continue;
        // Transform raw segment point to page space.
        float px = 0.f, py = 0.f;
        ApplyMat(combined, sx, sy, &px, &py);

        const int segType = FPDFPathSegment_GetType(seg);
        const bool closing = FPDFPathSegment_GetClose(seg) == TRUE;

        if (segType == FPDF_SEGMENT_MOVETO) {
          flushSubpath(false);
          subpathPts.clear();
          subpathPts.push_back(px);
          subpathPts.push_back(py);
          moveX = px; moveY = py;
          inSubpath = true;
        } else {
          subpathPts.push_back(px);
          subpathPts.push_back(py);
          if (closing) {
            flushSubpath(true);
            subpathPts.clear();
            subpathPts.push_back(moveX);
            subpathPts.push_back(moveY);
          }
        }
      }
      flushSubpath(false);
    }

    // ---- TEXT ------------------------------------------------------------
    if (type == FPDF_PAGEOBJ_TEXT && doText) {
      // FPDFPageObj_GetBounds returns bounds in the object's parent space
      // (already includes the object's own matrix).  Apply accum to get
      // page-space coordinates.
      float left = 0.f, bottom = 0.f, right = 0.f, top = 0.f;
      if (FPDFPageObj_GetBounds(obj, &left, &bottom, &right, &top)) {
        float px = 0.f, py = 0.f;
        ApplyMat(accum, left, bottom, &px, &py);
        outTextPos.push_back(px);
        outTextPos.push_back(py);
      }
    }

    // ---- FORM XOBJECT (recurse) ------------------------------------------
    if (type == FPDF_PAGEOBJ_FORM && depth < kMaxFormDepth) {
      const int fc = FPDFFormObj_CountObjects(obj);
      if (fc > 0) {
        // combined already incorporates the form's placement matrix.
        ExtractSnapFromObjectRange(fc, true, nullptr, obj,
                                   combined,
                                   doLines, doCircles, doText,
                                   outLines, outCircles, outTextPos,
                                   depth + 1);
      }
    }
  }
}

static void ExtractSnapFromPage(FPDF_PAGE page, bool doLines, bool doCircles, bool doText,
                                 std::vector<float>& outLines,
                                 std::vector<float>& outCircles,
                                 std::vector<float>& outTextPos) {
  outLines.clear();
  outCircles.clear();
  outTextPos.clear();

  const FS_MATRIX identity = {1.f, 0.f, 0.f, 1.f, 0.f, 0.f};
  const int nObj = FPDFPage_CountObjects(page);
  ExtractSnapFromObjectRange(nObj, false, page, nullptr,
                              identity,
                              doLines, doCircles, doText,
                              outLines, outCircles, outTextPos,
                              0);
}

// ---------------------------------------------------------------------------
// PdfDraftCache implementation
// ---------------------------------------------------------------------------
struct PdfDraftCache {
  // --- Async load state -------------------------------------------------
  // The background thread writes doc/pageCount/thumbs/pageWidths/pageHeights
  // and then publishes via ready (release store).  Main thread must
  // acquire-load ready before reading those fields.
  std::atomic<bool> ready{false};   ///< true once doc is loaded and sizes read
  std::atomic<bool> failed{false};  ///< true if FPDF_LoadDocument failed
  std::thread       loaderThread;
  std::string       loadPath;       ///< copy of file path owned by background thread

  // --- Published by the background thread (safe after ready == true) ----
  FPDF_DOCUMENT doc       = nullptr;
  int           pageCount = 0;

  struct Thumb {
    unsigned int texId       = 0;
    int          w           = 0;
    int          h           = 0;
    bool         needsRaster = true;  ///< False once rasterized (or if page failed to load).
  };
  std::vector<Thumb> thumbs;
  std::vector<float> pageWidths;  // pts
  std::vector<float> pageHeights; // pts

  // Async thumbnail worker -----------------------------------------------
  // The background thread renders one page at a time to a pixel buffer.
  // TickThumb() (main thread) uploads the result to a GL texture once done.
  struct ThumbResult {
    int                  pageIdx = -1;
    std::vector<uint8_t> buf;          // top-down BGRA; ready when done==true
    int                  w = 0, h = 0;
    std::atomic<bool>    done{false};  // background sets this last (release store)
  } thumbResult;
  std::thread thumbThread;
  std::atomic<bool> thumbCancelled{false};  // set by Free() to skip pending work
};

// True if PdfAttach_Init successfully initialized COM on the main thread.
// Needed to call CoUninitialize exactly once in PdfAttach_Shutdown.
static bool g_pdfComInited = false;

bool PdfAttach_Init() {
  FPDF_LIBRARY_CONFIG cfg{};
  cfg.version = 2;
  FPDF_InitLibraryWithConfig(&cfg);
  // Initialize COM as STA on the main thread.  This is required for the
  // IShellItemImageFactory thumbnail API (shell COM objects are STA-only).
  // S_OK  → we own the init;  S_FALSE → already STA (fine);
  // RPC_E_CHANGED_MODE → already MTA (shell thumbnail may not work, acceptable).
  const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  g_pdfComInited = (hr == S_OK);
  PdfLog("[PDF] PdfAttach_Init: CoInitializeEx hr=0x%08X\n",
         static_cast<unsigned>(hr));
  return true;
}

void PdfAttach_Shutdown() {
  FPDF_DestroyLibrary();
  if (g_pdfComInited) {
    CoUninitialize();
    g_pdfComInited = false;
  }
}

PdfDraftCache* PdfDraftCache_Create(const char* filePath) {
  if (!filePath || !filePath[0])
    return nullptr;

  auto* cache      = new PdfDraftCache();
  cache->loadPath  = filePath;

  PdfLog("[PDF] PdfDraftCache_Create (async): %s\n", filePath);

  // Kick off a background thread so the main thread (and UI) stay responsive
  // while pdfium parses the document.  The thread sets ready=true (release) when
  // done; all main-thread reads of doc/pageCount/thumbs/etc. are guarded by an
  // acquire load of ready.
  cache->loaderThread = std::thread([cache]() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    PdfLog("[PDF] BG: FPDF_LoadDocument start\n");
    PdfTimer tLoad;
    FPDF_DOCUMENT doc = FPDF_LoadDocument(cache->loadPath.c_str(), nullptr);
    PdfLog("[PDF] BG: FPDF_LoadDocument: %.1f ms  (%s)\n",
           tLoad.ms(), doc ? "OK" : "FAILED");

    if (!doc) {
      cache->failed.store(true, std::memory_order_release);
      return;
    }

    PdfTimer tCount;
    const int pageCount = FPDF_GetPageCount(doc);
    PdfLog("[PDF] BG: FPDF_GetPageCount: %.1f ms  → %d pages\n",
           tCount.ms(), pageCount);

    if (pageCount <= 0) {
      FPDF_CloseDocument(doc);
      cache->failed.store(true, std::memory_order_release);
      return;
    }

    cache->thumbs.resize(static_cast<size_t>(pageCount));
    cache->pageWidths.resize(static_cast<size_t>(pageCount), 0.f);
    cache->pageHeights.resize(static_cast<size_t>(pageCount), 0.f);

    // Read all page sizes from the cross-reference table (fast, no page tree
    // traversal) so the dialog can show page dimensions immediately.
    { PdfTimer t;
      for (int pi = 0; pi < pageCount; ++pi) {
        FS_SIZEF sz{};
        if (FPDF_GetPageSizeByIndexF(doc, pi, &sz)) {
          cache->pageWidths[static_cast<size_t>(pi)]  = sz.width;
          cache->pageHeights[static_cast<size_t>(pi)] = sz.height;
        }
      }
      PdfLog("[PDF] BG: GetPageSizeByIndexF x%d: %.1f ms\n", pageCount, t.ms()); }

    cache->doc       = doc;
    cache->pageCount = pageCount;
    // Release store: all preceding writes are now visible to any subsequent
    // acquire load of ready on the main thread.
    cache->ready.store(true, std::memory_order_release);
    PdfLog("[PDF] BG: ready — %d pages queued for lazy thumb render\n", pageCount);
  });

  return cache;  // returns immediately; doc/pageCount not valid until ready==true
}

void PdfDraftCache_Free(PdfDraftCache* cache) {
  if (!cache)
    return;

  // Signal both workers to bail out at the next IFSDK_PAUSE checkpoint or
  // atomic check.  For a complex page this can save the bulk of render time.
  cache->thumbCancelled.store(true, std::memory_order_release);

  // Delete GL textures now — this MUST happen on the main thread (OpenGL
  // context owner).  Zero out the IDs so the cleanup thread never sees them.
  for (auto& t : cache->thumbs) {
    if (t.texId) {
      GLuint tid = t.texId;
      glDeleteTextures(1, &tid);
      t.texId = 0;
    }
  }

  // Spawn a lightweight cleanup thread that joins the workers (which may
  // still be in a long pdfium render) and then frees the pdfium document.
  // This makes PdfDraftCache_Free non-blocking: the main thread returns
  // immediately; the old render finishes in the background and cleans itself up.
  std::thread([cache]() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    if (cache->loaderThread.joinable())
      cache->loaderThread.join();
    if (cache->thumbThread.joinable())
      cache->thumbThread.join();
    if (cache->doc)
      FPDF_CloseDocument(cache->doc);
    delete cache;
  }).detach();
}

bool PdfDraftCache_IsLoading(const PdfDraftCache* cache) {
  if (!cache) return false;
  return !cache->ready.load(std::memory_order_acquire) &&
         !cache->failed.load(std::memory_order_acquire);
}

bool PdfDraftCache_IsFailed(const PdfDraftCache* cache) {
  if (!cache) return false;
  return cache->failed.load(std::memory_order_acquire);
}

int PdfDraftCache_PageCount(const PdfDraftCache* cache) {
  if (!cache || !cache->ready.load(std::memory_order_acquire)) return 0;
  return cache->pageCount;
}

unsigned int PdfDraftCache_ThumbnailTex(const PdfDraftCache* cache, int pageIndex) {
  if (!cache || !cache->ready.load(std::memory_order_acquire)) return 0;
  if (pageIndex < 0 || pageIndex >= cache->pageCount) return 0;
  // Returns 0 (placeholder) if the thumbnail hasn't been rasterized yet.
  // Call PdfDraftCache_TickThumb once per frame to load one thumbnail at a time.
  return cache->thumbs[static_cast<size_t>(pageIndex)].texId;
}

bool PdfDraftCache_TickThumb(PdfDraftCache* cache) {
  if (!cache || !cache->ready.load(std::memory_order_acquire))
    return false;

  // -----------------------------------------------------------------------
  // Stage A: background render finished — upload result to GL (main thread).
  // -----------------------------------------------------------------------
  if (cache->thumbResult.done.load(std::memory_order_acquire)) {
    auto& r = cache->thumbResult;
    const int pi = r.pageIdx;
    if (pi >= 0 && pi < cache->pageCount && !r.buf.empty()) {
      PdfTimer t;
      auto& th = cache->thumbs[static_cast<size_t>(pi)];
      th.texId = UploadBgraTexture(r.buf, r.w, r.h);
      th.w     = r.w;
      th.h     = r.h;
      PdfLog("[PDF] TickThumb p%d: UploadBgraTexture %.1f ms\n", pi, t.ms());
    }
    // Reset the pending slot so Stage C can enqueue the next page.
    r.buf.clear();
    r.pageIdx = -1;
    r.done.store(false, std::memory_order_release);
    if (cache->thumbThread.joinable())
      cache->thumbThread.join();
    return true;
  }

  // -----------------------------------------------------------------------
  // Stage B: background thread is still rendering — keep ticking next frame.
  // -----------------------------------------------------------------------
  if (cache->thumbThread.joinable())
    return true;

  // -----------------------------------------------------------------------
  // Stage C: idle — find the next page needing a thumbnail and kick it off.
  // -----------------------------------------------------------------------
  for (int pi = 0; pi < cache->pageCount; ++pi) {
    PdfDraftCache::Thumb& th = cache->thumbs[static_cast<size_t>(pi)];
    if (!th.needsRaster)
      continue;

    // Main-thread fast path (page 0 only): read from Windows' thumbnail
    // cache without blocking.  If the user has ever browsed to this file in
    // Explorer the thumbnail is already cached and this returns in < 1 ms.
    // Subsequent opens of the same file are always instant via this path.
    if (pi == 0) {
      std::vector<uint8_t> shellBuf;
      int shellW = 0, shellH = 0;
      PdfTimer tCache;
      if (GetShellThumbnailCached(cache->loadPath, 300, shellBuf, &shellW, &shellH)) {
        PdfLog("[PDF] TickThumb p0: shell cache hit %.1f ms (%dx%d) — no thread needed\n",
               tCache.ms(), shellW, shellH);
        th.needsRaster = false;
        th.texId = UploadBgraTexture(shellBuf, shellW, shellH);
        th.w = shellW;
        th.h = shellH;
        return true;  // done this frame, no background thread needed
      }
      PdfLog("[PDF] TickThumb p0: shell cache miss (%.1f ms), starting thread\n",
             tCache.ms());
    }

    th.needsRaster = false;  // claim before the thread starts

    cache->thumbResult.pageIdx = pi;
    cache->thumbResult.done.store(false, std::memory_order_release);

    cache->thumbThread = std::thread([cache, pi]() {
      // Below-normal priority: complex engineering drawings can take many
      // seconds to render even at low DPI; this keeps the UI fully responsive.
      SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);

      PdfLog("[PDF] ThumbThread p%d: start\n", pi);

      if (cache->thumbCancelled.load(std::memory_order_acquire)) {
        cache->thumbResult.done.store(true, std::memory_order_release);
        return;
      }

      bool gotThumb = false;

      // -----------------------------------------------------------------
      // Fast path A: embedded /Thumb entry in the page dictionary.
      // AutoCAD, Revit, and Acrobat write these; extraction is instant.
      // Requires loading the page (a few ms), not rendering it.
      // -----------------------------------------------------------------
      {
        PdfTimer tEmbed;
        FPDF_PAGE page = FPDF_LoadPage(cache->doc, pi);
        if (page) {
          gotThumb = TryEmbeddedThumbnail(page,
                                          cache->thumbResult.buf,
                                          &cache->thumbResult.w,
                                          &cache->thumbResult.h);
          if (gotThumb)
            PdfLog("[PDF] ThumbThread p%d: embedded thumbnail %.1f ms (%dx%d)\n",
                   pi, tEmbed.ms(), cache->thumbResult.w, cache->thumbResult.h);
          else
            PdfLog("[PDF] ThumbThread p%d: no embedded thumbnail (%.1f ms)\n",
                   pi, tEmbed.ms());
          // Keep page open for fallback path B if needed.
          if (gotThumb || cache->thumbCancelled.load(std::memory_order_acquire)) {
            FPDF_ClosePage(page);
          } else {
            // -----------------------------------------------------------------
            // Fast path B: Windows.Data.Pdf native renderer (Direct2D / GPU).
            // Opens the file independently of the pdfium doc; may be
            // significantly faster than pdfium for complex vector drawings.
            // -----------------------------------------------------------------
#ifdef PDF_WINRT_ENABLED
            {
              PdfTimer tWinRT;
              gotThumb = RenderPageWithWinRT(cache->loadPath, pi, 300,
                                             cache->thumbResult.buf,
                                             &cache->thumbResult.w,
                                             &cache->thumbResult.h);
              PdfLog("[PDF] ThumbThread p%d: WinRT %s (%.1f ms)\n",
                     pi, gotThumb ? "OK" : "FAILED", tWinRT.ms());
            }
#endif
            if (!gotThumb && !cache->thumbCancelled.load(std::memory_order_acquire)) {
              // -----------------------------------------------------------------
              // Slow path C: pdfium progressive render (cancellable via IFSDK_PAUSE).
              // Used when embedded thumbnail is absent and WinRT is unavailable /
              // failed.  The IFSDK_PAUSE callback checks thumbCancelled between
              // drawing operations so the render can be aborted quickly.
              // -----------------------------------------------------------------
              const float w_pt   = FPDF_GetPageWidthF(page);
              const float h_pt   = FPDF_GetPageHeightF(page);
              const float maxPts = std::max(w_pt > 0.f ? w_pt : 1.f,
                                            h_pt > 0.f ? h_pt : 1.f);
              constexpr float kMaxThumbPx  = 280.f;
              constexpr float kMinThumbDpi =   4.f;
              constexpr float kMaxThumbDpi =  18.f;
              const float thumbDpi = std::max(kMinThumbDpi,
                                     std::min(kMaxThumbDpi,
                                              kMaxThumbPx * 72.f / maxPts));
              constexpr int kThumbFlags = FPDF_RENDER_NO_SMOOTHTEXT |
                                          FPDF_RENDER_NO_SMOOTHIMAGE |
                                          FPDF_RENDER_NO_SMOOTHPATH;
              PdfTimer tRender;
              gotThumb = RenderPageToBuffer(page, thumbDpi, kThumbFlags,
                                            cache->thumbCancelled,
                                            cache->thumbResult.buf,
                                            &cache->thumbResult.w,
                                            &cache->thumbResult.h);
              PdfLog("[PDF] ThumbThread p%d: pdfium render %s (%.1f ms @ %.1f DPI)\n",
                     pi, gotThumb ? "OK" : "cancelled/failed",
                     tRender.ms(), thumbDpi);
            }
            FPDF_ClosePage(page);
          }
        }
      }

      (void)gotThumb; // result is communicated via thumbResult.buf being non-empty
      // Release store: buf/w/h are fully written before main thread reads them.
      cache->thumbResult.done.store(true, std::memory_order_release);
    });

    return true;  // thread started; re-tick next frame for Stage A or B
  }
  return false;  // all thumbnails are up to date
}

int PdfDraftCache_ThumbW(const PdfDraftCache* cache, int pageIndex) {
  if (!cache || !cache->ready.load(std::memory_order_acquire)) return 0;
  if (pageIndex < 0 || pageIndex >= cache->pageCount) return 0;
  return cache->thumbs[static_cast<size_t>(pageIndex)].w;
}

int PdfDraftCache_ThumbH(const PdfDraftCache* cache, int pageIndex) {
  if (!cache || !cache->ready.load(std::memory_order_acquire)) return 0;
  if (pageIndex < 0 || pageIndex >= cache->pageCount) return 0;
  return cache->thumbs[static_cast<size_t>(pageIndex)].h;
}

float PdfDraftCache_PageWidthPts(const PdfDraftCache* cache, int pageIndex) {
  if (!cache || !cache->ready.load(std::memory_order_acquire)) return 0.f;
  if (pageIndex < 0 || pageIndex >= static_cast<int>(cache->pageWidths.size())) return 0.f;
  return cache->pageWidths[static_cast<size_t>(pageIndex)];
}

float PdfDraftCache_PageHeightPts(const PdfDraftCache* cache, int pageIndex) {
  if (!cache || !cache->ready.load(std::memory_order_acquire)) return 0.f;
  if (pageIndex < 0 || pageIndex >= static_cast<int>(cache->pageHeights.size())) return 0.f;
  return cache->pageHeights[static_cast<size_t>(pageIndex)];
}

bool PdfDraftCache_RasterizePage(const PdfDraftCache* cache, int pageIndex, float dpi,
                                  unsigned int* outTexId, int* outTexW, int* outTexH,
                                  float* outWidthPts, float* outHeightPts) {
  if (!cache || !cache->ready.load(std::memory_order_acquire)) return false;
  if (pageIndex < 0 || pageIndex >= cache->pageCount)
    return false;
  FPDF_PAGE page = FPDF_LoadPage(cache->doc, pageIndex);
  if (!page)
    return false;
  if (outWidthPts)  *outWidthPts  = FPDF_GetPageWidthF(page);
  if (outHeightPts) *outHeightPts = FPDF_GetPageHeightF(page);
  int w = 0, h = 0;
  const unsigned int tid = RasterizePage(page, dpi, &w, &h);
  FPDF_ClosePage(page);
  if (!tid)
    return false;
  if (outTexId) *outTexId = tid;
  if (outTexW)  *outTexW  = w;
  if (outTexH)  *outTexH  = h;
  return true;
}

void PdfDraftCache_ExtractSnap(const PdfDraftCache* cache, int pageIndex,
                                bool extractLines, bool extractCircles, bool extractText,
                                std::vector<float>& outLines,
                                std::vector<float>& outCircles,
                                std::vector<float>& outTextPos) {
  outLines.clear();
  outCircles.clear();
  outTextPos.clear();
  if (!cache || !cache->ready.load(std::memory_order_acquire)) return;
  if (pageIndex < 0 || pageIndex >= cache->pageCount)
    return;
  FPDF_PAGE page = FPDF_LoadPage(cache->doc, pageIndex);
  if (!page)
    return;
  ExtractSnapFromPage(page, extractLines, extractCircles, extractText,
                      outLines, outCircles, outTextPos);
  FPDF_ClosePage(page);
}

bool PdfAttach_Build(const char* filePath, int pageIndex, float dpi,
                     bool doSnapLines, bool doSnapCircles, bool doSnapText,
                     PdfAttachment& out) {
  out = PdfAttachment{};
  out.filePath   = filePath ? filePath : "";
  out.pageIndex  = pageIndex;

  PdfLog("[PDF] PdfAttach_Build: page %d @ %.0f DPI\n", pageIndex, dpi);

  FPDF_DOCUMENT doc;
  { PdfTimer t;
    doc = FPDF_LoadDocument(filePath, nullptr);
    PdfLog("[PDF]   FPDF_LoadDocument: %.1f ms\n", t.ms()); }
  if (!doc)
    return false;

  const int total = FPDF_GetPageCount(doc);
  if (pageIndex < 0 || pageIndex >= total) {
    FPDF_CloseDocument(doc);
    return false;
  }

  FPDF_PAGE page;
  { PdfTimer t;
    page = FPDF_LoadPage(doc, pageIndex);
    PdfLog("[PDF]   FPDF_LoadPage p%d: %.1f ms\n", pageIndex, t.ms()); }
  if (!page) {
    FPDF_CloseDocument(doc);
    return false;
  }

  out.pageWidthPts  = FPDF_GetPageWidthF(page);
  out.pageHeightPts = FPDF_GetPageHeightF(page);

  int tw = 0, th = 0;
  { PdfTimer t;
    out.glTexId = RasterizePage(page, dpi, &tw, &th);
    PdfLog("[PDF]   RasterizePage total: %.1f ms\n", t.ms()); }
  out.texW    = tw;
  out.texH    = th;

  out.snapLines   = doSnapLines;
  out.snapCircles = doSnapCircles;
  out.snapText    = doSnapText;

  { PdfTimer t;
    ExtractSnapFromPage(page, doSnapLines, doSnapCircles, doSnapText,
                        out.snapLinesFlat, out.snapCirclesCxCyR, out.snapTextPos);
    PdfLog("[PDF]   ExtractSnapFromPage: %.1f ms  (lines=%zu circles=%zu text=%zu)\n",
           t.ms(),
           out.snapLinesFlat.size() / 4,
           out.snapCirclesCxCyR.size() / 3,
           out.snapTextPos.size() / 2); }

  FPDF_ClosePage(page);
  FPDF_CloseDocument(doc);

  PdfLog("[PDF] PdfAttach_Build done\n");
  return out.glTexId != 0;
}

void PdfAttach_ReleaseTexture(PdfAttachment& att) {
  if (att.glTexId) {
    GLuint tid = att.glTexId;
    glDeleteTextures(1, &tid);
    att.glTexId = 0;
    att.texW    = 0;
    att.texH    = 0;
  }
}
