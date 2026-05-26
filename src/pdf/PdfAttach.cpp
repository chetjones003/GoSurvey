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
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <future>
#include <memory>
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
// maxPx > 0  → aspect-fit thumbnail mode (used by ThumbThread, dpi ignored).
// dpi  > 0.f → exact DPI-based size (used by BuildToBuffer on a dedicated thread,
//              maxPx ignored).  WinRT exposes the page's point size directly so
//              this path needs no prior pdfium load to obtain page dimensions.
static bool RenderPageWithWinRT(const std::string& filePath, int pageIndex,
                                 int maxPx, float dpi,
                                 std::vector<uint8_t>& outBgra,
                                 int* outW, int* outH,
                                 float* outWPts = nullptr, float* outHPts = nullptr) {
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

    // Get the page and compute a destination size.
    auto pdfPage = pdfDoc.GetPage(static_cast<uint32_t>(pageIndex));
    auto sz = pdfPage.Size();
    const float pw = sz.Width, ph = sz.Height;
    if (outWPts) *outWPts = pw;
    if (outHPts) *outHPts = ph;
    uint32_t dw, dh;
    if (dpi > 0.f) {
      // Exact DPI-based sizing: 1 PDF point = 1/72 inch.
      dw = std::max(1u, static_cast<uint32_t>(pw * dpi / 72.f));
      dh = std::max(1u, static_cast<uint32_t>(ph * dpi / 72.f));
    } else if (pw >= ph) {
      dw = static_cast<uint32_t>(maxPx);
      dh = std::max(1u, static_cast<uint32_t>(maxPx * ph / pw));
    } else {
      dh = static_cast<uint32_t>(maxPx);
      dw = std::max(1u, static_cast<uint32_t>(maxPx * pw / ph));
    }

    winrt::Windows::Data::Pdf::PdfPageRenderOptions opts;
    opts.DestinationWidth(dw);
    opts.DestinationHeight(dh);
    // Default BitmapEncoderId is PNG.  PNG keeps the stream small (~2-5 MB for a
    // typical page vs ~75 MB raw) which is critical for InMemoryRandomAccessStream
    // performance — the stream grows by COM reallocation, so 75 MB of uncompressed
    // BMP data causes dramatically more overhead than a few MB of PNG.

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

  // Flip rows (pdfium top-down → OpenGL bottom-up) into a working buffer that
  // will also serve as the source for mip generation below.
  std::vector<uint8_t> prev(buf.size());
  const int stride = w * 4;
  for (int y = 0; y < h; ++y) {
    const uint8_t* src = buf.data() + y * stride;
    uint8_t*       dst = prev.data() + (h - 1 - y) * stride;
    std::memcpy(dst, src, static_cast<size_t>(stride));
  }

  GLuint tex = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_BGRA, GL_UNSIGNED_BYTE, prev.data());

  // Build a limited mip chain (levels 1-4) with a CPU 2×2 box filter.
  // This costs ~15-25 ms on the main thread for a 5000×3750 image — far cheaper
  // than glGenerateMipmap's ~200 ms GPU synchronisation stall — and gives correct
  // trilinear filtering so thin lines don't alias away at typical zoom-out levels.
  constexpr int kMaxMipLevels = 4;
  int prevW = w, prevH = h;
  int finalLevel = 0;
  for (int level = 1; level <= kMaxMipLevels && prevW > 1 && prevH > 1; ++level) {
    const int dw = std::max(1, prevW / 2);
    const int dh = std::max(1, prevH / 2);
    std::vector<uint8_t> curr(static_cast<size_t>(dw) * dh * 4);
    for (int y = 0; y < dh; ++y) {
      const uint8_t* row0 = prev.data() + static_cast<size_t>(std::min(2*y,   prevH-1)) * prevW * 4;
      const uint8_t* row1 = prev.data() + static_cast<size_t>(std::min(2*y+1, prevH-1)) * prevW * 4;
      uint8_t*       dst  = curr.data() + static_cast<size_t>(y) * dw * 4;
      for (int x = 0; x < dw; ++x) {
        const int x0 = std::min(2*x,   prevW-1) * 4;
        const int x1 = std::min(2*x+1, prevW-1) * 4;
        const int out = x * 4;
        dst[out+0] = static_cast<uint8_t>((row0[x0+0]+row0[x1+0]+row1[x0+0]+row1[x1+0]) >> 2);
        dst[out+1] = static_cast<uint8_t>((row0[x0+1]+row0[x1+1]+row1[x0+1]+row1[x1+1]) >> 2);
        dst[out+2] = static_cast<uint8_t>((row0[x0+2]+row0[x1+2]+row1[x0+2]+row1[x1+2]) >> 2);
        dst[out+3] = static_cast<uint8_t>((row0[x0+3]+row0[x1+3]+row1[x0+3]+row1[x1+3]) >> 2);
      }
    }
    glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA8, dw, dh, 0, GL_BGRA, GL_UNSIGNED_BYTE, curr.data());
    prev = std::move(curr);
    prevW = dw; prevH = dh;
    finalLevel = level;
  }

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL,  finalLevel);
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

struct SnapExtractStats {
  int pathsTotal    = 0;
  int pathsFillOnly = 0;  // skipped: filled, not stroked
  int pathsThin     = 0;  // skipped: strokeW < 0.5 pt
  int pathsAccepted = 0;
  // stroke-width buckets: [0,0.5), [0.5,1), [1,2), [2,inf)
  int swBucket[4]   = {};
  // segment-length buckets: [0,1), [1,5), [5,20), [20,inf)  (PDF pts)
  int slBucket[4]   = {};
};

static void ExtractSnapFromObjectRange(
    int nObj, bool fromForm, FPDF_PAGE page, FPDF_PAGEOBJECT formObj,
    const FS_MATRIX& accum,
    bool doLines, bool doCircles, bool doText,
    std::vector<float>& outLines,
    std::vector<float>& outCircles,
    std::vector<float>& outTextPos,
    int depth, SnapExtractStats& stats)
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

      ++stats.pathsTotal;

      // Skip paths that are filled but NOT stroked.
      {
        int fillmode = FPDF_FILLMODE_NONE;
        FPDF_BOOL stroked = FALSE;
        if (FPDFPath_GetDrawMode(obj, &fillmode, &stroked)) {
          if (fillmode != FPDF_FILLMODE_NONE && !stroked) {
            ++stats.pathsFillOnly;
            continue; // filled-only → skip
          }
        }
      }

      // Skip paths with sub-point stroke widths.
      float strokeW = -1.f;
      {
        if (FPDFPageObj_GetStrokeWidth(obj, &strokeW) && strokeW >= 0.f && strokeW < 0.5f) {
          ++stats.pathsThin;
          continue; // hairline → skip
        }
      }
      // Track stroke width bucket for accepted paths.
      if (strokeW >= 0.f) {
        if      (strokeW < 0.5f) ++stats.swBucket[0];
        else if (strokeW < 1.0f) ++stats.swBucket[1];
        else if (strokeW < 2.0f) ++stats.swBucket[2];
        else                     ++stats.swBucket[3];
      }
      ++stats.pathsAccepted;

      // Minimum segment length² threshold.
      // Diagnostic data showed 83% of emitted segments are < 5pt long — these
      // are tiny marks, small symbols, and fine detail that saturate the snap
      // grid and prevent useful snapping on dense PDFs.  Only segments ≥ 20 pt
      // (≈ 7 mm on paper) survive; collinear dedup already merges straight wall
      // runs into single long segments before this check, so long walls encoded
      // as many short collinear pieces are preserved.  The [20+] bucket held
      // 7,142 segments for this drawing vs. 418K endpoints at the 0.5pt floor.
      constexpr float kMinSegLenSq = 400.f; // 20 pt minimum (≈ 7 mm)

      float moveX = 0.f, moveY = 0.f;
      std::vector<float> subpathPts;
      bool inSubpath      = false;
      bool subpathHasBez  = false; // true if any BEZIERTO segment seen in this subpath
      // PDF cubic beziers are encoded as 3 consecutive BEZIERTO segments:
      //   seg[0] = CP1 (off-curve), seg[1] = CP2 (off-curve), seg[2] = P3 (on-curve).
      // We only want the on-curve endpoint; skip CP1 and CP2.
      int bezierPhase = 0; // 0=not in bezier, 1=CP1, 2=CP2, 3=endpoint

      auto flushSubpath = [&](bool closed) {
        if (!inSubpath)
          return;
        // Only attempt circle detection when the subpath uses Bézier curves.
        // Every rectangle has all 4 corners equidistant from its centroid, so
        // DetectCircle would return true for any closed rectangular LINETO path
        // — mis-classifying building walls as circles and losing their corner
        // endpoints for snap.  Real circles in PDF are always drawn with
        // cubic Béziers; pure-LINETO closed paths are polygons, not circles.
        if (doCircles && closed && subpathHasBez && subpathPts.size() >= 6) {
          float cx = 0.f, cy = 0.f, cr = 0.f;
          if (DetectCircle(subpathPts, &cx, &cy, &cr)) {
            outCircles.push_back(cx);
            outCircles.push_back(cy);
            outCircles.push_back(cr);
            // Do NOT return here.  PDF generators sometimes encode straight-line
            // rectangles using degenerate cubic Béziers (CP1=P0, CP2=P3), which
            // makes every rectangle's 4 corners pass DetectCircle's radius-
            // variance test.  By falling through we always emit the on-curve
            // endpoints to outLines as well, so building corners are always
            // reachable for endpoint snap regardless of how they were drawn.
          }
        }
        if (doLines && subpathPts.size() >= 4) {
          const size_t n = subpathPts.size() / 2;
          // Emit only segments between direction-change (corner) points.
          // A wall encoded as N collinear LINETO segments → only 2 endpoints
          // instead of 2N, preventing endpoint saturation on dense drawings.
          // kEps: sin(angle) threshold — anything < 0.57° is treated as straight.
          constexpr float kEps = 0.01f;
          size_t runStart = 0;
          for (size_t i = 1; i < n; ++i) {
            const bool lastPt = (i == n - 1);
            bool isBend = lastPt;
            if (!isBend) {
              // Cross-product test on consecutive segment directions (i-1→i) × (i→i+1).
              const float ax = subpathPts[i*2]     - subpathPts[(i-1)*2];
              const float ay = subpathPts[i*2+1]   - subpathPts[(i-1)*2+1];
              const float bx = subpathPts[(i+1)*2] - subpathPts[i*2];
              const float by = subpathPts[(i+1)*2+1]- subpathPts[i*2+1];
              const float cross = ax*by - ay*bx;
              isBend = (cross*cross) > (kEps*kEps * (ax*ax + ay*ay) * (bx*bx + by*by));
            }
            if (isBend) {
              const float x0 = subpathPts[runStart*2],   y0 = subpathPts[runStart*2+1];
              const float x1 = subpathPts[i*2],          y1 = subpathPts[i*2+1];
              const float ddx = x1 - x0, ddy = y1 - y0;
              const float lenSq = ddx*ddx + ddy*ddy;
              if (lenSq >= kMinSegLenSq) {
                const float len = std::sqrt(lenSq);
                if      (len < 1.f)  ++stats.slBucket[0];
                else if (len < 5.f)  ++stats.slBucket[1];
                else if (len < 20.f) ++stats.slBucket[2];
                else                 ++stats.slBucket[3];
                outLines.push_back(x0); outLines.push_back(y0);
                outLines.push_back(x1); outLines.push_back(y1);
              }
              runStart = i;
            }
          }
          if (closed && n >= 2) {
            const float x0 = subpathPts[(n-1)*2], y0 = subpathPts[(n-1)*2+1];
            const float x1 = subpathPts[0],       y1 = subpathPts[1];
            const float ddx = x1 - x0, ddy = y1 - y0;
            const float lenSq = ddx*ddx + ddy*ddy;
            if (lenSq >= kMinSegLenSq) {
              const float len = std::sqrt(lenSq);
              if      (len < 1.f)  ++stats.slBucket[0];
              else if (len < 5.f)  ++stats.slBucket[1];
              else if (len < 20.f) ++stats.slBucket[2];
              else                 ++stats.slBucket[3];
              outLines.push_back(x0); outLines.push_back(y0);
              outLines.push_back(x1); outLines.push_back(y1);
            }
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
          bezierPhase = 0;
          subpathHasBez = false;
        } else if (segType == FPDF_SEGMENT_LINETO) {
          bezierPhase = 0;
          subpathPts.push_back(px);
          subpathPts.push_back(py);
          if (closing) {
            flushSubpath(true);
            subpathPts.clear();
            subpathPts.push_back(moveX);
            subpathPts.push_back(moveY);
            subpathHasBez = false;
          }
        } else if (segType == FPDF_SEGMENT_BEZIERTO) {
          // PDF cubic bezier: CP1, CP2, P3.  Only P3 is on-curve.
          ++bezierPhase;
          if (bezierPhase == 3) {
            bezierPhase = 0; // reset for next bezier in this subpath
            subpathHasBez = true; // real circles use cubic Béziers
            // P3 is the on-curve endpoint — treat like a line to this point.
            subpathPts.push_back(px);
            subpathPts.push_back(py);
            if (closing) {
              flushSubpath(true);
              subpathPts.clear();
              subpathPts.push_back(moveX);
              subpathPts.push_back(moveY);
              subpathHasBez = false;
            }
          }
          // CP1 (phase=1) and CP2 (phase=2) are off-curve — skip entirely.
        }
        // FPDF_SEGMENT_UNKNOWN: ignore
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
                                   depth + 1, stats);
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
  SnapExtractStats stats;
  ExtractSnapFromObjectRange(nObj, false, page, nullptr,
                              identity,
                              doLines, doCircles, doText,
                              outLines, outCircles, outTextPos,
                              0, stats);
  const int totalSegs = static_cast<int>(outLines.size() / 4);
  PdfLog("[PDF] ExtractSnap: paths total=%d fill-skip=%d thin-skip=%d accepted=%d segs=%d\n",
         stats.pathsTotal, stats.pathsFillOnly, stats.pathsThin, stats.pathsAccepted, totalSegs);
  PdfLog("[PDF]   strokeW: [<0.5]=0 [0.5-1)=%d [1-2)=%d [2+)=%d (unknown/skipped=%d)\n",
         stats.swBucket[1], stats.swBucket[2], stats.swBucket[3],
         stats.pathsAccepted - stats.swBucket[1] - stats.swBucket[2] - stats.swBucket[3]);
  PdfLog("[PDF]   segLen(pts): [<1]=%d [1-5)=%d [5-20)=%d [20+]=%d\n",
         stats.slBucket[0], stats.slBucket[1], stats.slBucket[2], stats.slBucket[3]);
}

// ---------------------------------------------------------------------------
// Spatial endpoint grid — built once per attachment, queried O(1) per frame.
// ---------------------------------------------------------------------------

/// Build a CSR (Compressed Sparse Row) spatial grid from the (x1,y1,x2,y2)
/// snap lines already stored in \p att.snapLinesFlat.
/// Cells cover the page; each cell stores all endpoint (x,y) pairs that fall
/// inside it.  Grid dimensions are chosen to target ~200 endpoints per cell.
static void BuildSnapEndptGrid(PdfAttachment& att) {
  auto& grid = att.snapEndptGrid;
  grid = PdfAttachment::SnapGrid{};  // clear any previous data

  const auto& SL = att.snapLinesFlat;
  if (SL.empty()) return;

  const float pageW = att.pageWidthPts;
  const float pageH = att.pageHeightPts;
  if (pageW <= 0.f || pageH <= 0.f) return;

  // Choose grid dimensions so cells are roughly square and hold ~200 pts each.
  const size_t nEndpts = SL.size() / 2;  // 2 endpoints per line × 2 floats each = SL.size()/2 endpoints
  const float aspect   = pageH / std::max(pageW, 1e-6f);
  const int   cols     = std::max(4, std::min(512, static_cast<int>(
                           std::sqrt(static_cast<float>(nEndpts) / (200.f * aspect)))));
  const int   rows     = std::max(4, std::min(512, static_cast<int>(
                           static_cast<float>(cols) * aspect + 0.5f)));

  grid.cols    = cols;
  grid.rows    = rows;
  grid.originX = 0.f;
  grid.originY = 0.f;
  grid.cellW   = pageW / static_cast<float>(cols);
  grid.cellH   = pageH / static_cast<float>(rows);

  const int nCells = cols * rows;

  // Helper: map a page-space point to a grid cell index (clamped).
  auto cellOf = [&](float ex, float ey) -> int {
    int col = static_cast<int>((ex - grid.originX) / grid.cellW);
    int row = static_cast<int>((ey - grid.originY) / grid.cellH);
    col = std::clamp(col, 0, cols - 1);
    row = std::clamp(row, 0, rows - 1);
    return row * cols + col;
  };

  // Pass 1: count endpoints per cell.
  grid.offsets.assign(static_cast<size_t>(nCells + 1), 0u);
  for (size_t i = 0; i + 3 < SL.size(); i += 4) {
    ++grid.offsets[static_cast<size_t>(cellOf(SL[i],     SL[i + 1]) + 1)];
    ++grid.offsets[static_cast<size_t>(cellOf(SL[i + 2], SL[i + 3]) + 1)];
  }

  // Prefix sum → each cell's start offset.
  for (int ci = 0; ci < nCells; ++ci)
    grid.offsets[static_cast<size_t>(ci + 1)] += grid.offsets[static_cast<size_t>(ci)];

  // Pass 2: fill endpoint (x,y) pairs into the sorted cell buckets.
  grid.pts.resize(nEndpts * 2);  // nEndpts = SL.size()/2; each endpoint = 2 floats → SL.size() floats
  std::vector<uint32_t> writePos(grid.offsets.begin(),
                                  grid.offsets.begin() + static_cast<ptrdiff_t>(nCells));
  for (size_t i = 0; i + 3 < SL.size(); i += 4) {
    for (int ep = 0; ep < 2; ++ep) {
      const float ex  = SL[i + static_cast<size_t>(ep) * 2];
      const float ey  = SL[i + static_cast<size_t>(ep) * 2 + 1];
      const uint32_t slot = writePos[static_cast<size_t>(cellOf(ex, ey))]++;
      grid.pts[static_cast<size_t>(slot) * 2]     = ex;
      grid.pts[static_cast<size_t>(slot) * 2 + 1] = ey;
    }
  }

  PdfLog("[PDF] BuildSnapEndptGrid: %dx%d cells, %.0f MB, %zu endpoints\n",
         cols, rows,
         static_cast<double>(grid.offsets.size() * sizeof(uint32_t) +
                              grid.pts.size()    * sizeof(float)) / (1024.0 * 1024.0),
         nEndpts);
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
      // Fast path A: Windows.Data.Pdf native renderer (Direct2D / GPU).
      // Opens the PDF file completely independently of pdfium — no
      // FPDF_LoadPage required here, so it never waits on page parsing.
      // On hardware with a GPU this path is typically 10-100× faster than
      // pdfium for dense vector engineering drawings.
      // -----------------------------------------------------------------
#ifdef PDF_WINRT_ENABLED
      if (!cache->thumbCancelled.load(std::memory_order_acquire)) {
        PdfTimer tWinRT;
        gotThumb = RenderPageWithWinRT(cache->loadPath, pi,
                                       /*maxPx=*/300, /*dpi=*/0.f,
                                       cache->thumbResult.buf,
                                       &cache->thumbResult.w,
                                       &cache->thumbResult.h);
        PdfLog("[PDF] ThumbThread p%d: WinRT %s (%.1f ms)\n",
               pi, gotThumb ? "OK" : "FAILED", tWinRT.ms());
      }
#endif

      // -----------------------------------------------------------------
      // Paths B and C both require the pdfium page to be loaded.
      // Skip the FPDF_LoadPage cost entirely when WinRT already succeeded.
      // -----------------------------------------------------------------
      if (!gotThumb && !cache->thumbCancelled.load(std::memory_order_acquire)) {
        PdfTimer tPageLoad;
        FPDF_PAGE page = FPDF_LoadPage(cache->doc, pi);
        PdfLog("[PDF] ThumbThread p%d: FPDF_LoadPage %.1f ms  (%s)\n",
               pi, tPageLoad.ms(), page ? "OK" : "FAILED");

        if (page) {
          // -----------------------------------------------------------------
          // Fast path B: embedded /Thumb entry in the page dictionary.
          // AutoCAD, Revit, and Acrobat write these; extraction is nearly
          // instant since the image is already decoded by pdfium.
          // -----------------------------------------------------------------
          {
            PdfTimer tEmbed;
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
          }

          // -----------------------------------------------------------------
          // Slow path C: pdfium progressive render (cancellable via IFSDK_PAUSE).
          // Last resort when both WinRT and embedded thumbnail are unavailable.
          // The IFSDK_PAUSE callback checks thumbCancelled between individual
          // drawing operations so the render can be aborted quickly.
          // -----------------------------------------------------------------
          if (!gotThumb && !cache->thumbCancelled.load(std::memory_order_acquire)) {
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

// ---------------------------------------------------------------------------
// Async-friendly build helpers
// ---------------------------------------------------------------------------

bool PdfAttach_BuildToBuffer(const char* filePath, int pageIndex, float dpi,
                              bool doSnapLines, bool doSnapCircles, bool doSnapText,
                              PdfAttachPixelResult& out) {
  out = PdfAttachPixelResult{};

  PdfLog("[PDF] PdfAttach_BuildToBuffer: page %d @ %.0f DPI\n", pageIndex, dpi);

  // -----------------------------------------------------------------
  // Fast path: Windows.Data.Pdf (hardware-accelerated Direct2D render).
  // -----------------------------------------------------------------
#ifdef PDF_WINRT_ENABLED
  {
    // Run WinRT render and pdfium snap extraction in parallel:
    //   Sub-thread  — RenderPageWithWinRT (Direct2D hardware render)
    //   This thread — FPDF_LoadDocument + ExtractSnapFromPage
    // Total wall time ≈ max(WinRT, pdfium) instead of their sum.
    //
    // WinRT can hang for extremely long periods on PDFs with broken font data
    // (manifests as DWrite FileFormatException internally).  We give WinRT a
    // hard wall-clock budget.  If it exceeds that budget the thread is detached
    // — shared ownership via shared_ptr prevents use-after-free — and we fall
    // through to the pdfium CPU raster path so the user gets a result promptly.
    const std::string filePathStr(filePath ? filePath : "");

    // All WinRT output lives in a heap object so a detached thread can safely
    // write into it after the stack frame is gone.
    struct WinRTWork {
      std::vector<uint8_t> buf;
      int   w = 0, h = 0;
      float wPts = 0.f, hPts = 0.f;
      bool  ok  = false;
      std::promise<bool> done; // signalled when render completes (or fails)
    };
    auto work = std::make_shared<WinRTWork>();
    // get_future() must be called before spawning the thread.
    auto winrtFuture = work->done.get_future();

    PdfTimer tParallel;
    std::thread winrtThr([work, filePathStr, pageIndex, dpi]() {
      PdfTimer tWinRT;
      try {
        work->ok = RenderPageWithWinRT(filePathStr, pageIndex,
                                       /*maxPx=*/0, dpi,
                                       work->buf, &work->w, &work->h,
                                       &work->wPts, &work->hPts);
      } catch (...) {
        work->ok = false;
      }
      PdfLog("[PDF] BuildToBuffer WinRT sub-thread %s (%.1f ms) → %dx%d\n",
             work->ok ? "OK" : "FAILED", tWinRT.ms(), work->w, work->h);
      try { work->done.set_value(work->ok); } catch (...) {}
    });

    // Meanwhile on this thread: pdfium load for page size + snap geometry.
    float pdfWPts = 0.f, pdfHPts = 0.f;
    {
      FPDF_DOCUMENT doc2 = FPDF_LoadDocument(filePath, nullptr);
      if (doc2) {
        FS_SIZEF sz{};
        FPDF_GetPageSizeByIndexF(doc2, pageIndex, &sz);
        pdfWPts = sz.width;
        pdfHPts = sz.height;
        FPDF_PAGE page2 = FPDF_LoadPage(doc2, pageIndex);
        if (page2) {
          PdfTimer tSnap;
          ExtractSnapFromPage(page2, doSnapLines, doSnapCircles, doSnapText,
                              out.snapLinesFlat, out.snapCirclesCxCyR, out.snapTextPos);
          PdfLog("[PDF] BuildToBuffer snap (parallel): %.1f ms  (lines=%zu)\n",
                 tSnap.ms(), out.snapLinesFlat.size() / 4);
          FPDF_ClosePage(page2);
        }
        FPDF_CloseDocument(doc2);
      }
    }

    // Give WinRT a fixed total wall-clock budget from when we spawned it.
    // For normal PDFs WinRT is already done before snap finishes; the wait
    // returns immediately.  For broken PDFs (DWrite font errors, etc.) that
    // would otherwise hang for 60+ seconds we fall through to pdfium raster.
    constexpr double kWinRTBudgetMs = 12000.0;
    const double elapsed   = tParallel.ms();
    const double remaining = kWinRTBudgetMs - elapsed;

    const bool timedOut = (remaining <= 0.0) ||
                          (winrtFuture.wait_for(std::chrono::milliseconds(
                               static_cast<long long>(remaining)))
                           == std::future_status::timeout);

    if (timedOut) {
      PdfLog("[PDF] BuildToBuffer WinRT timed out (%.0f ms elapsed) — "
             "falling back to pdfium raster\n", tParallel.ms());
      winrtThr.detach(); // work shared_ptr keeps state alive until thread exits
    } else {
      winrtThr.join();
      PdfLog("[PDF] BuildToBuffer parallel total %.1f ms\n", tParallel.ms());
    }

    if (!timedOut && work->ok) {
      // Prefer pdfium page dimensions (canonical); fall back to WinRT's reading.
      const float wPts = pdfWPts > 0.f ? pdfWPts : work->wPts;
      const float hPts = pdfHPts > 0.f ? pdfHPts : work->hPts;
      out.pixelBuf      = std::move(work->buf);
      out.texW          = work->w;
      out.texH          = work->h;
      out.pageWidthPts  = wPts;
      out.pageHeightPts = hPts;
      out.success = true;
      return true;
    }

    // WinRT timed out or failed — discard any partial snap and fall through to
    // the pdfium CPU raster path, which will redo snap as part of its own render.
    out.snapLinesFlat.clear();
    out.snapCirclesCxCyR.clear();
    out.snapTextPos.clear();
  }
#endif

  // -----------------------------------------------------------------
  // Fallback: pdfium CPU render (progressive, non-blocking via caller's thread).
  // -----------------------------------------------------------------
  PdfLog("[PDF] BuildToBuffer: using pdfium fallback\n");
  FPDF_DOCUMENT doc = FPDF_LoadDocument(filePath, nullptr);
  if (!doc) return false;

  const int total = FPDF_GetPageCount(doc);
  if (pageIndex < 0 || pageIndex >= total) { FPDF_CloseDocument(doc); return false; }

  FPDF_PAGE page = FPDF_LoadPage(doc, pageIndex);
  if (!page) { FPDF_CloseDocument(doc); return false; }

  out.pageWidthPts  = FPDF_GetPageWidthF(page);
  out.pageHeightPts = FPDF_GetPageHeightF(page);

  const std::atomic<bool> neverCancel{false};
  RenderPageToBuffer(page, dpi, FPDF_PRINTING, neverCancel,
                     out.pixelBuf, &out.texW, &out.texH);

  ExtractSnapFromPage(page, doSnapLines, doSnapCircles, doSnapText,
                      out.snapLinesFlat, out.snapCirclesCxCyR, out.snapTextPos);

  FPDF_ClosePage(page);
  FPDF_CloseDocument(doc);

  out.success = !out.pixelBuf.empty();
  return out.success;
}

// ---------------------------------------------------------------------------
// Detect whether a PDF raster has a dark (black-canvas) background.
//
// Strategy: sample 8 small patches — the 4 corners and 4 edge midpoints of the
// page.  Those locations are almost always pure background with no drawn content,
// so their average luminance reliably identifies the page colour regardless of
// how large or colourful the actual drawing content is.
// Returns true when average corner luminance < 0.40 (dark/CAD canvas).
// ---------------------------------------------------------------------------
static bool DetectDarkBackground(const std::vector<uint8_t>& bgra, int w, int h) {
  if (bgra.empty() || w <= 0 || h <= 0) return false;

  // Average luminance of a kR×kR pixel patch centred on (cx, cy).
  constexpr int kR = 12;
  auto patchLum = [&](int cx, int cy) -> float {
    float sum = 0.f;
    int   n   = 0;
    const int x0 = std::max(0, cx - kR), x1 = std::min(w, cx + kR);
    const int y0 = std::max(0, cy - kR), y1 = std::min(h, cy + kR);
    for (int y = y0; y < y1; ++y)
      for (int x = x0; x < x1; ++x) {
        const uint8_t* p = bgra.data() + (static_cast<size_t>(y) * w + x) * 4;
        sum += (0.114f * p[0] + 0.587f * p[1] + 0.299f * p[2]) / 255.f;
        ++n;
      }
    return n > 0 ? sum / n : 0.5f;
  };

  // 4 corners + 4 edge midpoints
  const float lums[] = {
    patchLum(0,     0    ),  patchLum(w - 1, 0    ),
    patchLum(0,     h - 1),  patchLum(w - 1, h - 1),
    patchLum(w / 2, 0    ),  patchLum(w / 2, h - 1),
    patchLum(0,     h / 2),  patchLum(w - 1, h / 2),
  };
  float avg = 0.f;
  for (float l : lums) avg += l;
  avg /= 8.f;

  const bool dark = avg < 0.40f;
  PdfLog("[PDF] DetectDarkBackground: corner avg lum=%.3f -> %s\n", avg, dark ? "DARK" : "light");
  return dark;
}

// ---------------------------------------------------------------------------
// Build a kVisMaskW × kVisMaskH binary "is-there-content-here?" mask from the
// rasterised page image.  A cell is set to 1 if any pixel in its area is
// "foreground" — bright on a dark-bg PDF, dark on a light-bg PDF.
// Stored in att.snapVisMask so CadSnap can reject endpoints in blank regions.
// ---------------------------------------------------------------------------
static void BuildVisibilityMask(const std::vector<uint8_t>& bgra,
                                 int texW, int texH, bool darkBg,
                                 std::vector<uint8_t>& mask) {
  constexpr int MW = PdfAttachment::kVisMaskW;
  constexpr int MH = PdfAttachment::kVisMaskH;
  mask.assign(static_cast<size_t>(MW * MH), 0u);
  if (bgra.empty() || texW <= 0 || texH <= 0) return;

  // Luminance threshold: pixels past this value are "foreground".
  // Dark-bg PDF  → foreground is bright  → lum > 0.20
  // Light-bg PDF → foreground is dark    → lum < 0.80
  const float thresh = darkBg ? 0.20f : 0.80f;

  for (int my = 0; my < MH; ++my) {
    // Pixel row range for this mask row (top-down image, so row 0 = top of page).
    const int py0 = my * texH / MH;
    const int py1 = (my + 1) * texH / MH;
    for (int mx = 0; mx < MW; ++mx) {
      const int px0 = mx * texW / MW;
      const int px1 = (mx + 1) * texW / MW;
      bool found = false;
      for (int py = py0; py < py1 && !found; ++py) {
        for (int px = px0; px < px1 && !found; ++px) {
          const uint8_t* p = bgra.data() + (static_cast<size_t>(py) * texW + px) * 4;
          // BGRA: p[0]=B, p[1]=G, p[2]=R
          const float lum = (0.114f * p[0] + 0.587f * p[1] + 0.299f * p[2]) / 255.f;
          found = darkBg ? (lum > thresh) : (lum < thresh);
        }
      }
      mask[static_cast<size_t>(my) * MW + mx] = found ? 1u : 0u;
    }
  }
}

// Build snap targets from rendered raster via 8-connected topology analysis.
// Emits (x,y,x,y) degenerate segments for use by BuildSnapEndptGrid.
static void BuildImageSnapPts(
    const std::vector<uint8_t>& bgra, int texW, int texH,
    float pageW, float pageH, bool darkBg,
    std::vector<float>& outLines)
{
    if (bgra.empty() || texW <= 0 || texH <= 0 || pageW <= 0.f || pageH <= 0.f)
        return;

    // Build 512×512 binary foreground mask from rendered pixels.
    constexpr int MS = 512;
    std::vector<uint8_t> mask(static_cast<size_t>(MS) * MS, 0u);

    // For light-bg PDFs: foreground = dark pixels (lum < 0.80).
    // For dark-bg PDFs: foreground = bright pixels (lum > 0.20).
    const float thresh = darkBg ? 0.20f : 0.80f;

    for (int my = 0; my < MS; ++my) {
        const int py0 = my * texH / MS;
        const int py1 = (my + 1) * texH / MS;
        for (int mx = 0; mx < MS; ++mx) {
            const int px0 = mx * texW / MS;
            const int px1 = (mx + 1) * texW / MS;
            bool found = false;
            for (int py = py0; py < py1 && !found; ++py) {
                for (int px = px0; px < px1 && !found; ++px) {
                    const uint8_t* p = bgra.data() + (static_cast<size_t>(py) * texW + px) * 4;
                    const float lum = (0.114f * p[0] + 0.587f * p[1] + 0.299f * p[2]) / 255.f;
                    found = darkBg ? (lum > thresh) : (lum < thresh);
                }
            }
            mask[static_cast<size_t>(my) * MS + mx] = found ? 1u : 0u;
        }
    }

    // 8-neighbor direction offsets (N, NE, E, SE, S, SW, W, NW).
    // Indexed 0..7; opposite of direction i is direction (i+4)%8.
    constexpr int dx[8] = {  0, 1, 1,  1, 0, -1, -1, -1 };
    constexpr int dy[8] = { -1,-1, 0,  1, 1,  1,  0, -1 };

    outLines.clear();
    int snapCount = 0;

    for (int my = 1; my < MS - 1; ++my) {
        for (int mx = 1; mx < MS - 1; ++mx) {
            if (!mask[static_cast<size_t>(my) * MS + mx]) continue;

            // Collect which 8-neighbor directions are foreground.
            int nd[8];
            int nc = 0;
            for (int d = 0; d < 8; ++d) {
                const int nx = mx + dx[d];
                const int ny = my + dy[d];
                if (mask[static_cast<size_t>(ny) * MS + nx])
                    nd[nc++] = d;
            }

            bool emit = false;
            if (nc == 1) {
                // Endpoint: exactly one foreground neighbor.
                emit = true;
            } else if (nc == 2) {
                // Corner: two non-opposite neighbors (a straight run has
                // opposite neighbors, e.g. W+E = indices 6 and 2 differ by 4).
                emit = ((nd[1] - nd[0]) != 4);
            } else if (nc >= 3) {
                // Junction / T-intersection.
                emit = true;
            }

            if (emit) {
                // Cell centre → PDF page coordinates.
                // Image row 0 = page top, so Y is inverted.
                const float pdfX = (static_cast<float>(mx) + 0.5f) / MS * pageW;
                const float pdfY = (1.f - (static_cast<float>(my) + 0.5f) / MS) * pageH;
                outLines.push_back(pdfX); outLines.push_back(pdfY);
                outLines.push_back(pdfX); outLines.push_back(pdfY);
                ++snapCount;
            }
        }
    }

    PdfLog("[PDF] BuildImageSnapPts: mask=%dx%d -> %d snap targets\n", MS, MS, snapCount);
}

bool PdfAttach_FinishBuild(const PdfAttachPixelResult& res, const char* filePath,
                            int pageIndex, PdfAttachment& att) {
  if (!res.success || res.pixelBuf.empty()) return false;

  att = PdfAttachment{};
  att.filePath        = filePath ? filePath : "";
  att.pageIndex       = pageIndex;
  att.pageWidthPts    = res.pageWidthPts;
  att.pageHeightPts   = res.pageHeightPts;
  att.texW            = res.texW;
  att.texH            = res.texH;
  att.snapLinesFlat   = res.snapLinesFlat;
  att.snapCirclesCxCyR= res.snapCirclesCxCyR;
  att.snapTextPos     = res.snapTextPos;
  att.snapLines       = !res.snapLinesFlat.empty();
  att.snapCircles     = !res.snapCirclesCxCyR.empty();
  att.snapText        = !res.snapTextPos.empty();

  // Detect background type for snap-mask and shader selection.
  // Both light-bg and dark-bg PDFs now default to showBackground=false: the shader
  // un-premultiplies the background tint from each pixel and fades it to transparent,
  // so line colors are restored to full saturation on both PDF types.
  att.snapVisDark    = DetectDarkBackground(res.pixelBuf, res.texW, res.texH);
  att.showBackground = false;
  PdfLog("[PDF] FinishBuild: dark-bg detected = %s\n",
         att.snapVisDark ? "yes" : "no");

  { PdfTimer t;
    BuildVisibilityMask(res.pixelBuf, res.texW, res.texH,
                        att.snapVisDark, att.snapVisMask);
    PdfLog("[PDF] FinishBuild: BuildVisibilityMask %.1f ms\n", t.ms()); }

  // For dense PDFs (GIS exports, scanned drawings) the path-based snap extraction
  // yields unreliable endpoints. Switch to image-based topology analysis when the
  // path extractor returned more than 2 000 segments (a sign that path endpoints
  // don't correspond to visible geometry corners).
  if (att.snapLinesFlat.size() / 4 > 2000) {
    PdfTimer t;
    BuildImageSnapPts(res.pixelBuf, res.texW, res.texH,
                      att.pageWidthPts, att.pageHeightPts,
                      att.snapVisDark, att.snapLinesFlat);
    PdfLog("[PDF] FinishBuild: BuildImageSnapPts %.1f ms\n", t.ms());
  }

  { PdfTimer t;
    BuildSnapEndptGrid(att);
    PdfLog("[PDF] FinishBuild: BuildSnapEndptGrid %.1f ms\n", t.ms()); }

  PdfTimer t;
  att.glTexId = UploadBgraTexture(res.pixelBuf, res.texW, res.texH);
  PdfLog("[PDF] FinishBuild: UploadBgraTexture %.1f ms\n", t.ms());
  return att.glTexId != 0;
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

  { PdfTimer t;
    BuildSnapEndptGrid(out);
    PdfLog("[PDF]   BuildSnapEndptGrid: %.1f ms\n", t.ms()); }

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
