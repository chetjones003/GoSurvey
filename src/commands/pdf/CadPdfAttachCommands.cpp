#include "CadCommandsInternal.hpp"
#include "CadCoordinateFrame.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------------
// PDFATTACH
// ---------------------------------------------------------------------------
void StartPdfAttachCommand(AppCommandState& st, std::vector<std::string>& log) {
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  st.selectedSurveyPointIndices.clear();
  st.selBoxWaitingSecond = false;
  stepengine::BeginSession(st, std::make_unique<stepengine::PdfAttachSession>(), /*recordAsLastCommand=*/false);
  st.pdfAttachPhase      = AppCommandState::PdfAttachPhase::WaitDialog;
  st.pdfAttachDialogOpen = true;
  log.push_back("PDFATTACH — select PDF file and options in the dialog, then place the underlay.");
}

void StartPdfAttachBuild(AppCommandState& cmd, std::vector<std::string>& log) {
  using Ph = AppCommandState::PdfAttachPhase;
  std::string capturedPath = cmd.pdfAttachFilePath;
  const int capturedPage = cmd.pdfAttachSelectedPage;
  const float capturedDpi = cmd.pdfAttachRasterDpi;
  const bool capturedLines = cmd.pdfAttachSnapLines;
  const bool capturedCirc = cmd.pdfAttachSnapCircles;
  const bool capturedText = cmd.pdfAttachSnapText;
  const bool capturedSpec = cmd.pdfAttachSpecifyInsert;
  auto build = std::make_unique<AppCommandState::AsyncBuild>();
  build->specifyInsert = capturedSpec;
  auto* buildPtr = build.get();
  buildPtr->thread = std::thread([buildPtr, capturedPath = std::move(capturedPath), capturedPage, capturedDpi,
                                  capturedLines, capturedCirc, capturedText]() noexcept {
    try {
      PdfAttach_BuildToBuffer(capturedPath.c_str(), capturedPage, capturedDpi,
                              capturedLines, capturedCirc, capturedText, buildPtr->result);
    } catch (...) {
      buildPtr->result.success = false;
    }
    buildPtr->done.store(true, std::memory_order_release);
  });
  cmd.pdfAttachAsync = std::move(build);
  cmd.pdfAttachPhase = Ph::Building;
  log.push_back("PDFATTACH — rasterizing, please wait...");
}

void SubmitPdfAttachInsertPoint(AppCommandState& st, float wx, float wy, std::vector<std::string>& log) {
  if (st.active != AppCommandState::Kind::PdfAttach ||
      st.pdfAttachPhase != AppCommandState::PdfAttachPhase::WaitInsertPoint)
    return;

  PdfAttachment att;
  bool ok = false;

  if (st.pdfAttachPreviewReady) {
    // Reuse the attachment already built by the async path — no rebuild needed.
    att                      = std::move(st.pdfAttachPreview);
    st.pdfAttachPreviewReady = false;
    ok                       = true;
  } else {
    // Fallback: synchronous build (should rarely happen — e.g. if the user
    // somehow reaches WaitInsertPoint without the async build completing).
    ok = PdfAttach_Build(st.pdfAttachFilePath,
                          st.pdfAttachSelectedPage,
                          st.pdfAttachRasterDpi,
                          st.pdfAttachSnapLines,
                          st.pdfAttachSnapCircles,
                          st.pdfAttachSnapText,
                          att);
  }

  if (ok) {
    att.insertX     = wx;
    att.insertY     = wy;
    att.scale       = st.pdfAttachScale;
    att.rotationDeg = st.pdfAttachRotDeg;
    const int nSnapLines   = static_cast<int>(att.snapLinesFlat.size())    / 4;
    const int nSnapCircles = static_cast<int>(att.snapCirclesCxCyR.size()) / 3;
    const int nSnapText    = static_cast<int>(att.snapTextPos.size())      / 2;
    PushUndoSnapshot(st, "PDF attach");
    st.drawing.pdfAttachments.push_back(std::move(att));
    char snapMsg[128];
    std::snprintf(snapMsg, sizeof(snapMsg),
                  "PDFATTACH — placed.  Snap: %d lines, %d circles, %d text.",
                  nSnapLines, nSnapCircles, nSnapText);
    log.push_back(snapMsg);
  } else {
    log.push_back("PDFATTACH — failed to rasterize page.");
  }

  stepengine::EndSession(st);
  st.pdfAttachPhase = AppCommandState::PdfAttachPhase::WaitDialog;
}

void CancelPdfAttachCommand(AppCommandState& st, std::vector<std::string>& log) {
  log.push_back("PDFATTACH cancelled.");
  // If a background build is running, detach it so Cancel returns immediately.
  // The AsyncBuild owns the thread; move it out to a short-lived cleanup thread
  // that waits for the render to finish before dropping the struct.
  if (st.pdfAttachAsync) {
    auto ab = std::move(st.pdfAttachAsync); // st.pdfAttachAsync is now null
    std::thread([moved = std::move(ab)]() mutable {
      if (moved && moved->thread.joinable())
        moved->thread.join();
    }).detach();
  }
  ResetAllCadDraftTools(st);
  stepengine::EndSession(st);
}

void VectorizePdfAttachmentLines(AppCommandState& st, int pdfIndex, std::vector<std::string>& log) {
  if (pdfIndex < 0 || pdfIndex >= static_cast<int>(st.drawing.pdfAttachments.size()))
    return;
  const PdfAttachment& att = st.drawing.pdfAttachments[static_cast<size_t>(pdfIndex)];
  const auto& snap = att.snapLinesFlat;
  if (snap.empty()) {
    log.push_back("Vectorize — no line snap geometry in this PDF underlay.");
    return;
  }
  PushUndoSnapshot(st, "Vectorize PDF");
  constexpr float kPi = 3.14159265f;
  const float cosR = std::cos(att.rotationDeg * kPi / 180.f);
  const float sinR = std::sin(att.rotationDeg * kPi / 180.f);
  int n = 0;
  for (size_t i = 0; i + 3 < snap.size(); i += 4) {
    const float sx1 = snap[i]     * att.scale, sy1 = snap[i + 1] * att.scale;
    const float sx2 = snap[i + 2] * att.scale, sy2 = snap[i + 3] * att.scale;
    // Z = 0 is correct here regardless of the UCS: a PDF underlay is a flat raster/vector
    // sheet with no elevation of its own.
    st.drawing.pushLine(CadLineRecord{att.insertX + sx1 * cosR - sy1 * sinR, att.insertY + sx1 * sinR + sy1 * cosR, 0.f, att.insertX + sx2 * cosR - sy2 * sinR, att.insertY + sx2 * sinR + sy2 * cosR, 0.f});
    ++n;
  }
  EnsureAttrCounts(st);
  // Tag newly-added lines with this underlay's layer.
  if (!att.layer.empty()) {
    const size_t total = st.drawing.lineCount();
    const size_t firstNew = total - static_cast<size_t>(n);
    for (size_t k = firstNew; k < total && k < st.drawing.userLineAttrs.size(); ++k)
      st.drawing.userLineAttrs[k].layer = att.layer;
  }
  BumpCadGpuCache(st);
  char buf[128];
  std::snprintf(buf, sizeof(buf), "Vectorize — added %d line segment%s from PDF underlay.",
                n, n == 1 ? "" : "s");
  log.push_back(buf);
}
