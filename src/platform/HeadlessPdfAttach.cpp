// Headless implementation of the PDF-underlay seam (REQ-203 / ADR-031 (b′)).
//
// PdfAttach.cpp is where OpenGL reaches the Commands layer: it includes <GL/glew.h> and calls
// glGenTextures/glDeleteTextures to turn a rasterised page into a texture. Linking it into a
// console program would put a GL dependency — and pdfium, and C++/WinRT — on a target whose whole
// point is having neither, and the GL calls would fault the moment they ran without a context.
//
// So headless links this file instead: the same header, a second implementation, chosen by the
// target (the mechanism the file-dialog seam uses, for the same reason).
//
// **These functions REFUSE rather than pretend.** A stub that reported success would hand the
// Commands layer an attachment with no page size and no snap geometry, and every downstream
// invariant would then be checked against a document the fuzzer had silently corrupted itself —
// findings that look like GoSurvey bugs and are not. Refusing is both honest and REQ-201-shaped:
// the failure is returned, not swallowed. The cost is that PDF underlay commands are not exercised
// headlessly, which is recorded as a known coverage gap in docs/fuzz-harness.md rather than hidden.
//
// Only three of these are reachable from CadCommands.cpp (PdfAttach_Build, PdfAttach_ReleaseTexture,
// PdfDraftCache_Free); the rest are implemented so the seam covers the whole header and a new call
// site is a behaviour change rather than a link error.

#include "PdfAttach.hpp"

// The opaque type the header forward-declares. Headless never creates one, but the definition has
// to exist for PdfDraftCache_Free's parameter to be a complete type at the call site.
struct PdfDraftCache {
  int unused = 0;
};

bool PdfAttach_Init() {
  return false;  // no pdfium headless; every dependent call refuses in turn
}

void PdfAttach_Shutdown() {}

PdfDraftCache* PdfDraftCache_Create(const char*) {
  return nullptr;
}

void PdfDraftCache_Free(PdfDraftCache*) {
  // Nothing is ever allocated by PdfDraftCache_Create above, so there is nothing to release.
  // Accepting the call (rather than asserting) keeps a command's cleanup path working unchanged.
}

bool PdfDraftCache_IsLoading(const PdfDraftCache*) {
  return false;  // never loading: a null cache is finished, not pending
}

bool PdfDraftCache_IsFailed(const PdfDraftCache*) {
  return true;  // and what it finished doing was failing
}

int PdfDraftCache_PageCount(const PdfDraftCache*) {
  return 0;
}

unsigned int PdfDraftCache_ThumbnailTex(const PdfDraftCache*, int) {
  return 0;
}

int PdfDraftCache_ThumbW(const PdfDraftCache*, int) {
  return 0;
}

int PdfDraftCache_ThumbH(const PdfDraftCache*, int) {
  return 0;
}

bool PdfDraftCache_TickThumb(PdfDraftCache*) {
  return false;  // nothing pending
}

bool PdfDraftCache_RasterizePage(const PdfDraftCache*, int, float, unsigned int* outTexId,
                                 int* outTexW, int* outTexH, float* outWidthPts,
                                 float* outHeightPts) {
  // Zero the outputs before refusing: a caller that ignores the false must not read a stale value.
  if (outTexId) *outTexId = 0;
  if (outTexW) *outTexW = 0;
  if (outTexH) *outTexH = 0;
  if (outWidthPts) *outWidthPts = 0.f;
  if (outHeightPts) *outHeightPts = 0.f;
  return false;
}

void PdfDraftCache_ExtractSnap(const PdfDraftCache*, int, bool, bool, bool,
                               std::vector<float>& outLines, std::vector<float>& outCircles,
                               std::vector<float>& outTextPos) {
  // The header documents that existing content is replaced. Honour that exactly: leaving the
  // caller's previous geometry in place would be a different contract from the real implementation,
  // and a seam that behaves differently is a bug generator rather than a substitute.
  outLines.clear();
  outCircles.clear();
  outTextPos.clear();
}

bool PdfAttach_Build(const char*, int, float, bool, bool, bool, PdfAttachment& out) {
  out = PdfAttachment{};  // no half-built attachment escapes into the drawing
  return false;
}

bool PdfAttach_BuildToBuffer(const char*, int, float, bool, bool, bool,
                             PdfAttachPixelResult& out) {
  out = PdfAttachPixelResult{};
  out.success = false;
  return false;
}

bool PdfAttach_FinishBuild(const PdfAttachPixelResult&, const char*, int, PdfAttachment& att) {
  att = PdfAttachment{};
  return false;
}

void PdfAttach_ReleaseTexture(PdfAttachment& att) {
  // No GL texture was ever created, so there is none to delete — but clear the handle so the
  // attachment is left in the same state the real implementation leaves it in.
  att.glTexId = 0;
  att.texW = 0;
  att.texH = 0;
}

float PdfDraftCache_PageWidthPts(const PdfDraftCache*, int) {
  return 0.f;
}

float PdfDraftCache_PageHeightPts(const PdfDraftCache*, int) {
  return 0.f;
}
