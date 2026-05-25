#include "PdfAttach.hpp"

#define NOMINMAX
#include <fpdfview.h>
#include <fpdf_edit.h>
#include <fpdf_text.h>

#include <GL/glew.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

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
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, 0);
  return static_cast<unsigned int>(tex);
}

// ---------------------------------------------------------------------------
// Rasterize one page at given DPI into a new GL texture.
// Returns 0 on failure.
// ---------------------------------------------------------------------------
static unsigned int RasterizePage(FPDF_PAGE page, float dpi, int* outW, int* outH) {
  const float w_pt = FPDF_GetPageWidthF(page);
  const float h_pt = FPDF_GetPageHeightF(page);
  const int   w    = std::max(1, static_cast<int>(w_pt * dpi / 72.f));
  const int   h    = std::max(1, static_cast<int>(h_pt * dpi / 72.f));

  std::vector<uint8_t> buf(static_cast<size_t>(w) * static_cast<size_t>(h) * 4, 0xFF);

  FPDF_BITMAP bm = FPDFBitmap_CreateEx(w, h, FPDFBitmap_BGRA, buf.data(), w * 4);
  if (!bm)
    return 0;
  FPDFBitmap_FillRect(bm, 0, 0, w, h, 0xFFFFFFFFu);
  FPDF_RenderPageBitmap(bm, page, 0, 0, w, h, 0, FPDF_PRINTING);
  FPDFBitmap_Destroy(bm);

  if (outW) *outW = w;
  if (outH) *outH = h;
  return UploadBgraTexture(buf, w, h);
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
  FPDF_DOCUMENT doc = nullptr;
  int pageCount     = 0;

  struct Thumb {
    unsigned int texId = 0;
    int          w = 0;
    int          h = 0;
  };
  std::vector<Thumb> thumbs;
  std::vector<float> pageWidths;  // pts
  std::vector<float> pageHeights; // pts
};

bool PdfAttach_Init() {
  FPDF_LIBRARY_CONFIG cfg{};
  cfg.version = 2;
  FPDF_InitLibraryWithConfig(&cfg);
  return true;
}

void PdfAttach_Shutdown() {
  FPDF_DestroyLibrary();
}

PdfDraftCache* PdfDraftCache_Create(const char* filePath) {
  if (!filePath || !filePath[0])
    return nullptr;

  FPDF_DOCUMENT doc = FPDF_LoadDocument(filePath, nullptr);
  if (!doc)
    return nullptr;

  const int pageCount = FPDF_GetPageCount(doc);
  if (pageCount <= 0) {
    FPDF_CloseDocument(doc);
    return nullptr;
  }

  auto* cache       = new PdfDraftCache();
  cache->doc        = doc;
  cache->pageCount  = pageCount;
  cache->thumbs.resize(static_cast<size_t>(pageCount));
  cache->pageWidths.resize(static_cast<size_t>(pageCount), 0.f);
  cache->pageHeights.resize(static_cast<size_t>(pageCount), 0.f);

  // Rasterize thumbnails at 36 DPI (small enough to be fast, legible for selection)
  for (int pi = 0; pi < pageCount; ++pi) {
    FPDF_PAGE page = FPDF_LoadPage(doc, pi);
    if (!page)
      continue;
    cache->pageWidths[static_cast<size_t>(pi)]  = FPDF_GetPageWidthF(page);
    cache->pageHeights[static_cast<size_t>(pi)] = FPDF_GetPageHeightF(page);
    int tw = 0, th = 0;
    const unsigned int tid = RasterizePage(page, 36.f, &tw, &th);
    cache->thumbs[static_cast<size_t>(pi)] = {tid, tw, th};
    FPDF_ClosePage(page);
  }
  return cache;
}

void PdfDraftCache_Free(PdfDraftCache* cache) {
  if (!cache)
    return;
  for (auto& t : cache->thumbs) {
    if (t.texId) {
      GLuint tid = t.texId;
      glDeleteTextures(1, &tid);
    }
  }
  if (cache->doc)
    FPDF_CloseDocument(cache->doc);
  delete cache;
}

int PdfDraftCache_PageCount(const PdfDraftCache* cache) {
  return cache ? cache->pageCount : 0;
}

unsigned int PdfDraftCache_ThumbnailTex(const PdfDraftCache* cache, int pageIndex) {
  if (!cache || pageIndex < 0 || pageIndex >= cache->pageCount)
    return 0;
  return cache->thumbs[static_cast<size_t>(pageIndex)].texId;
}

int PdfDraftCache_ThumbW(const PdfDraftCache* cache, int pageIndex) {
  if (!cache || pageIndex < 0 || pageIndex >= cache->pageCount)
    return 0;
  return cache->thumbs[static_cast<size_t>(pageIndex)].w;
}

int PdfDraftCache_ThumbH(const PdfDraftCache* cache, int pageIndex) {
  if (!cache || pageIndex < 0 || pageIndex >= cache->pageCount)
    return 0;
  return cache->thumbs[static_cast<size_t>(pageIndex)].h;
}

float PdfDraftCache_PageWidthPts(const PdfDraftCache* cache, int pageIndex) {
  if (!cache || pageIndex < 0 || pageIndex >= static_cast<int>(cache->pageWidths.size()))
    return 0.f;
  return cache->pageWidths[static_cast<size_t>(pageIndex)];
}

float PdfDraftCache_PageHeightPts(const PdfDraftCache* cache, int pageIndex) {
  if (!cache || pageIndex < 0 || pageIndex >= static_cast<int>(cache->pageHeights.size()))
    return 0.f;
  return cache->pageHeights[static_cast<size_t>(pageIndex)];
}

bool PdfDraftCache_RasterizePage(const PdfDraftCache* cache, int pageIndex, float dpi,
                                  unsigned int* outTexId, int* outTexW, int* outTexH,
                                  float* outWidthPts, float* outHeightPts) {
  if (!cache || pageIndex < 0 || pageIndex >= cache->pageCount)
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
  if (!cache || pageIndex < 0 || pageIndex >= cache->pageCount)
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

  FPDF_DOCUMENT doc = FPDF_LoadDocument(filePath, nullptr);
  if (!doc)
    return false;

  const int total = FPDF_GetPageCount(doc);
  if (pageIndex < 0 || pageIndex >= total) {
    FPDF_CloseDocument(doc);
    return false;
  }

  FPDF_PAGE page = FPDF_LoadPage(doc, pageIndex);
  if (!page) {
    FPDF_CloseDocument(doc);
    return false;
  }

  out.pageWidthPts  = FPDF_GetPageWidthF(page);
  out.pageHeightPts = FPDF_GetPageHeightF(page);

  int tw = 0, th = 0;
  out.glTexId = RasterizePage(page, dpi, &tw, &th);
  out.texW    = tw;
  out.texH    = th;

  out.snapLines   = doSnapLines;
  out.snapCircles = doSnapCircles;
  out.snapText    = doSnapText;

  ExtractSnapFromPage(page, doSnapLines, doSnapCircles, doSnapText,
                      out.snapLinesFlat, out.snapCirclesCxCyR, out.snapTextPos);

  FPDF_ClosePage(page);
  FPDF_CloseDocument(doc);

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
