#pragma once

#include <string>
#include <vector>

/// A committed PDF underlay attachment in the drawing (local coordinate space).
struct PdfAttachment {
  std::string filePath;
  int pageIndex = 0;

  /// Insertion point in local (drawing) coordinates.
  float insertX = 0.f;
  float insertY = 0.f;

  /// Uniform scale: 1 PDF point maps to this many drawing units.
  float scale = 1.f;

  /// Counter-clockwise rotation in degrees from +X axis (math convention).
  float rotationDeg = 0.f;

  /// Page dimensions in PDF points (1 pt = 1/72 inch).
  float pageWidthPts = 0.f;
  float pageHeightPts = 0.f;

  /// OpenGL texture (owned); 0 = not loaded.
  unsigned int glTexId = 0;
  int texW = 0;
  int texH = 0;

  /// Snap geometry in PDF page space (pt units, Y-up, origin at page BL).
  /// Lines: interleaved (x1,y1, x2,y2) pairs.
  std::vector<float> snapLinesFlat;
  /// Circles: (cx,cy,r) triplets.
  std::vector<float> snapCirclesCxCyR;
  /// Text insertion points: (x,y) pairs.
  std::vector<float> snapTextPos;

  bool snapLines   = true;
  bool snapCircles = true;
  bool snapText    = true;

  bool        showBackground = true;  ///< Render the raster image; snap still active when false.
  float       fade           = 1.0f;  ///< Opacity multiplier (0 = transparent, 1 = opaque).
  std::string layer;                  ///< Drawing layer assignment.
};

/// Opaque per-document draft cache used by the PDFATTACH dialog.
struct PdfDraftCache;

/// Initialize the PDFium library.  Call once at application startup.
bool PdfAttach_Init();

/// Shut down the PDFium library.  Call once at application exit.
void PdfAttach_Shutdown();

/// Load a document and rasterize thumbnails at 72 DPI.
/// Returns nullptr on failure (bad path, corrupt PDF, etc.).
PdfDraftCache* PdfDraftCache_Create(const char* filePath);

/// Destroy cache and release GPU textures.
void PdfDraftCache_Free(PdfDraftCache* cache);

/// True while the background load thread is still running (doc not yet usable).
bool PdfDraftCache_IsLoading(const PdfDraftCache* cache);

/// True if the background load thread finished but FPDF_LoadDocument failed.
bool PdfDraftCache_IsFailed(const PdfDraftCache* cache);

/// Number of pages in the loaded document (returns 0 while still loading).
int PdfDraftCache_PageCount(const PdfDraftCache* cache);

/// GL thumbnail texture for page \p idx (0-based).
/// Returns 0 (placeholder) if the thumbnail has not been rasterized yet.
/// Call PdfDraftCache_TickThumb once per UI frame to load one thumbnail at a time.
unsigned int PdfDraftCache_ThumbnailTex(const PdfDraftCache* cache, int pageIndex);
int          PdfDraftCache_ThumbW(const PdfDraftCache* cache, int pageIndex);
int          PdfDraftCache_ThumbH(const PdfDraftCache* cache, int pageIndex);

/// Rasterize ONE pending thumbnail (the lowest-index not yet done) and return true.
/// Returns false when all thumbnails are up to date.  Call once per UI frame from
/// the thumbnail-strip draw function to avoid blocking the main thread.
bool PdfDraftCache_TickThumb(PdfDraftCache* cache);

/// Rasterize a single page at \p dpi, upload to a new GL texture, and return
/// the page dimensions in PDF points.  Returns false on failure.
bool PdfDraftCache_RasterizePage(const PdfDraftCache* cache, int pageIndex, float dpi,
                                  unsigned int* outTexId, int* outTexW, int* outTexH,
                                  float* outWidthPts, float* outHeightPts);

/// Extract snap geometry from a page into the output vectors (existing content
/// is replaced).  PDF-space coordinates (pts, Y-up).
void PdfDraftCache_ExtractSnap(const PdfDraftCache* cache, int pageIndex,
                                bool extractLines, bool extractCircles, bool extractText,
                                std::vector<float>& outLines,
                                std::vector<float>& outCircles,
                                std::vector<float>& outTextPos);

/// Build a PdfAttachment by rasterizing the chosen page and extracting snap geometry.
/// Returns false on failure; caller still owns \p out.glTexId (0 on failure).
/// NOTE: runs synchronously on the calling thread and may block for many seconds on
/// complex PDFs.  Prefer PdfAttach_BuildToBuffer + PdfAttach_FinishBuild for async use.
bool PdfAttach_Build(const char* filePath, int pageIndex, float dpi,
                     bool snapLines, bool snapCircles, bool snapText,
                     PdfAttachment& out);

/// Intermediate result from a background build — contains pixel data but NO GL texture.
/// Produced by PdfAttach_BuildToBuffer (background-thread safe).
struct PdfAttachPixelResult {
  bool success = false;

  std::vector<uint8_t> pixelBuf; ///< Top-down BGRA rows
  int   texW = 0, texH = 0;
  float pageWidthPts  = 0.f;
  float pageHeightPts = 0.f;

  std::vector<float> snapLinesFlat;
  std::vector<float> snapCirclesCxCyR;
  std::vector<float> snapTextPos;
};

/// Background-thread safe: rasterizes the page and extracts snap geometry into CPU
/// buffers; no OpenGL calls.  Try Windows.Data.Pdf first, fall back to pdfium.
/// On return call PdfAttach_FinishBuild on the MAIN THREAD to upload the GL texture.
bool PdfAttach_BuildToBuffer(const char* filePath, int pageIndex, float dpi,
                              bool snapLines, bool snapCircles, bool snapText,
                              PdfAttachPixelResult& out);

/// MAIN THREAD ONLY.  Uploads the pixel buffer to a new GL texture and fills \p att.
/// After this call \p att owns the GL texture (free with PdfAttach_ReleaseTexture).
bool PdfAttach_FinishBuild(const PdfAttachPixelResult& res, const char* filePath,
                            int pageIndex, PdfAttachment& att);

/// Release the GL texture held by an attachment (safe to call on default-init).
void PdfAttach_ReleaseTexture(PdfAttachment& att);

/// Page dimensions in PDF points from the draft cache (0 on bad index or null cache).
float PdfDraftCache_PageWidthPts (const PdfDraftCache* cache, int pageIndex);
float PdfDraftCache_PageHeightPts(const PdfDraftCache* cache, int pageIndex);
