#include "PdfAttachDialog.hpp"

#include "PdfAttach.hpp"
#include "WinFileDialogs.hpp"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <cmath>
#include <cstring>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
namespace {

// Ensure a draft cache exists for the current path.
// Called every frame while the dialog is open — cheap when the cache is already
// present.  PdfDraftCache_Create now returns immediately and loads the document
// on a background thread, so this function never blocks the main thread.
static void EnsureDraftCache(AppCommandState& cmd) {
  if (!cmd.pdfAttachFilePath[0]) {
    if (cmd.pdfDraftCache) {
      PdfDraftCache_Free(cmd.pdfDraftCache);
      cmd.pdfDraftCache = nullptr;
    }
    return;
  }
  if (!cmd.pdfDraftCache) {
    // Spawn the async loader — returns instantly.
    cmd.pdfDraftCache = PdfDraftCache_Create(cmd.pdfAttachFilePath);
  }
  // Once loading finishes, clamp the selected page in case the previous value
  // was out of range for this document.
  if (cmd.pdfDraftCache) {
    const int total = PdfDraftCache_PageCount(cmd.pdfDraftCache);  // 0 until ready
    if (total > 0 && cmd.pdfAttachSelectedPage >= total)
      cmd.pdfAttachSelectedPage = 0;
  }
}

// Draw one row of page thumbnails inside a scrollable child region.
static void DrawThumbnailStrip(AppCommandState& cmd) {
  if (!cmd.pdfDraftCache)
    return;

  const int total = PdfDraftCache_PageCount(cmd.pdfDraftCache);
  if (total <= 0)
    return;

  constexpr float kThumbH    = 120.f;
  constexpr float kThumbW    = 90.f;
  constexpr float kPad       = 6.f;
  const float     regionH    = kThumbH + kPad * 2.f + ImGui::GetTextLineHeightWithSpacing();

  ImGui::BeginChild("##PdfThumbs", ImVec2(0.f, regionH), true,
                    ImGuiWindowFlags_HorizontalScrollbar);
  for (int pi = 0; pi < total; ++pi) {
    if (pi > 0)
      ImGui::SameLine(0.f, kPad);

    const unsigned int tid = PdfDraftCache_ThumbnailTex(cmd.pdfDraftCache, pi);
    const int          tw  = PdfDraftCache_ThumbW(cmd.pdfDraftCache, pi);
    const int          th  = PdfDraftCache_ThumbH(cmd.pdfDraftCache, pi);

    const bool selected = (pi == cmd.pdfAttachSelectedPage);
    ImGui::BeginGroup();

    // Highlight border for selected page.
    if (selected) {
      ImVec2 p   = ImGui::GetCursorScreenPos();
      ImVec2 p2  = ImVec2(p.x + kThumbW, p.y + kThumbH);
      ImGui::GetWindowDrawList()->AddRect(p, p2, IM_COL32(0, 200, 80, 255), 0.f, 0, 2.5f);
    }

    if (tid && tw > 0 && th > 0) {
      // Preserve page aspect ratio inside the fixed thumbnail box.
      const float ratio = static_cast<float>(th) / static_cast<float>(tw);
      float       dw    = kThumbW;
      float       dh    = dw * ratio;
      if (dh > kThumbH) {
        dh = kThumbH;
        dw = dh / ratio;
      }
      // Centre inside the thumbnail box.
      const float offX = (kThumbW - dw) * 0.5f;
      const float offY = (kThumbH - dh) * 0.5f;
      ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offX);
      ImGui::SetCursorPosY(ImGui::GetCursorPosY() + offY);
      ImGui::Image(static_cast<ImTextureID>(tid), ImVec2(dw, dh));
    } else {
      // Placeholder box when thumbnail is not ready.
      ImGui::Dummy(ImVec2(kThumbW, kThumbH));
    }

    // Page number label.
    char lbl[16];
    std::snprintf(lbl, sizeof(lbl), "Page %d", pi + 1);
    const float tw2 = ImGui::CalcTextSize(lbl).x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (kThumbW - tw2) * 0.5f);
    ImGui::TextUnformatted(lbl);
    ImGui::EndGroup();

    // Click to select page.
    if (ImGui::IsItemClicked())
      cmd.pdfAttachSelectedPage = pi;
    // Also click on image.
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
      cmd.pdfAttachSelectedPage = pi;
  }
  ImGui::EndChild();

  // Progressive thumbnail loading: rasterize one pending page per frame.
  // This spreads the work across frames so the UI never hitches.
  PdfDraftCache_TickThumb(cmd.pdfDraftCache);
}

// One parameter row: label, float input, and optional "Pick" button.
// Returns true when the Pick button is clicked (caller sets the appropriate phase).
static bool ParameterRow(const char* label, float* val, const char* fmt,
                          bool specifyOnScreen, bool* specifyOnScreenOut,
                          bool picking) {
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(label);
  ImGui::SameLine(140.f);

  const bool disabled = picking;
  if (disabled) ImGui::BeginDisabled();

  char id[64];
  std::snprintf(id, sizeof(id), "##%s", label);
  ImGui::SetNextItemWidth(110.f);
  ImGui::InputFloat(id, val, 0.f, 0.f, fmt);

  ImGui::SameLine();
  // "Specify on screen" check
  char chkId[64];
  std::snprintf(chkId, sizeof(chkId), "On-screen##%s", label);
  bool tmp = specifyOnScreen;
  if (ImGui::Checkbox(chkId, &tmp))
    *specifyOnScreenOut = tmp;

  ImGui::SameLine();
  char pickId[64];
  std::snprintf(pickId, sizeof(pickId), "Pick##%s", label);
  const bool pickClicked = ImGui::Button(pickId);

  if (disabled) ImGui::EndDisabled();
  return pickClicked;
}

} // namespace

// ---------------------------------------------------------------------------
// Public entry point
// ---------------------------------------------------------------------------
bool DrawPdfAttachDialog(AppCommandState& cmd, std::vector<std::string>& log) {
  using K  = AppCommandState::Kind;
  using Ph = AppCommandState::PdfAttachPhase;

  if (cmd.active != K::PdfAttach)
    return false;

  // Ensure the draft cache matches the current file path.
  if (cmd.pdfAttachDialogOpen)
    EnsureDraftCache(cmd);

  // --- Viewport pick phases (dialog closed) ---
  if (!cmd.pdfAttachDialogOpen) {
    if (cmd.pdfAttachPhase == Ph::WaitInsertPoint) {
      // Handled by the viewport click path in CadUi.cpp.
      // Show a hint in the command palette area.
      ImGui::SetNextWindowPos(ImVec2(10.f, ImGui::GetIO().DisplaySize.y - 60.f),
                              ImGuiCond_Always);
      ImGui::SetNextWindowBgAlpha(0.75f);
      ImGui::Begin("##PdfPickHint", nullptr,
                   ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                   ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoNav |
                   ImGuiWindowFlags_NoMove);
      ImGui::TextUnformatted("PDFATTACH — click in viewport to set insertion point.  ESC to cancel.");
      ImGui::End();
      return true;
    }
    return cmd.active == K::PdfAttach;
  }

  // --- Main dialog ---
  ImGui::SetNextWindowSize(ImVec2(560.f, 640.f), ImGuiCond_FirstUseEver);
  bool open = true;
  if (!ImGui::Begin("PDF Attach", &open, ImGuiWindowFlags_NoDocking)) {
    ImGui::End();
    if (!open) {
      CancelPdfAttachCommand(cmd, log);
    }
    return cmd.active == K::PdfAttach;
  }
  if (!open) {
    ImGui::End();
    CancelPdfAttachCommand(cmd, log);
    return cmd.active == K::PdfAttach;
  }

  // --- File selection row ---
  ImGui::SeparatorText("PDF File");
  ImGui::SetNextItemWidth(-80.f);
  bool pathChanged = false;
  if (ImGui::InputText("##PdfPath", cmd.pdfAttachFilePath,
                        sizeof(cmd.pdfAttachFilePath),
                        ImGuiInputTextFlags_EnterReturnsTrue)) {
    pathChanged = true;
  }
  ImGui::SameLine();
  if (ImGui::Button("Browse...")) {
    char tmp[1024]{};
    std::strncpy(tmp, cmd.pdfAttachFilePath, sizeof(tmp) - 1);
    if (BrowseOpenFilePdfUtf8(tmp, sizeof(tmp))) {
      // Reload cache only if path actually changed.
      if (std::strcmp(tmp, cmd.pdfAttachFilePath) != 0) {
        std::strncpy(cmd.pdfAttachFilePath, tmp, sizeof(cmd.pdfAttachFilePath) - 1);
        if (cmd.pdfDraftCache) {
          PdfDraftCache_Free(cmd.pdfDraftCache);
          cmd.pdfDraftCache = nullptr;
        }
        cmd.pdfAttachSelectedPage = 0;
        pathChanged = true;
      }
    }
  }
  if (pathChanged) {
    if (cmd.pdfDraftCache) {
      PdfDraftCache_Free(cmd.pdfDraftCache);
      cmd.pdfDraftCache = nullptr;
    }
    EnsureDraftCache(cmd);
  }

  // Status line — reflects async load state.
  if (cmd.pdfDraftCache) {
    if (PdfDraftCache_IsLoading(cmd.pdfDraftCache)) {
      // Animate a simple "Loading..." spinner using elapsed time.
      const char* kSpinner[] = { "|", "/", "-", "\\" };
      const int   spin = static_cast<int>(ImGui::GetTime() * 8.0) & 3;
      ImGui::TextDisabled("Loading PDF...  %s", kSpinner[spin]);
    } else if (PdfDraftCache_IsFailed(cmd.pdfDraftCache)) {
      ImGui::TextColored(ImVec4(1.f, 0.4f, 0.4f, 1.f), "Unable to open PDF.");
    } else {
      const int total = PdfDraftCache_PageCount(cmd.pdfDraftCache);
      ImGui::TextDisabled("%d page%s", total, total == 1 ? "" : "s");
    }
  } else if (cmd.pdfAttachFilePath[0]) {
    ImGui::TextColored(ImVec4(1.f, 0.4f, 0.4f, 1.f), "Unable to open PDF.");
  }

  // --- Page thumbnails ---
  ImGui::Spacing();
  ImGui::SeparatorText("Page Selection");
  DrawThumbnailStrip(cmd);

  // --- Parameters ---
  ImGui::Spacing();
  ImGui::SeparatorText("Placement");

  ImGui::Columns(1);
  if (ParameterRow("Insertion X:", &cmd.pdfAttachInsertX, "%.4f",
                    cmd.pdfAttachSpecifyInsert, &cmd.pdfAttachSpecifyInsert,
                    cmd.pdfAttachPhase == Ph::WaitInsertPoint)) {
    // Pick button: close dialog, wait for viewport click
  }
  if (ParameterRow("Insertion Y:", &cmd.pdfAttachInsertY, "%.4f",
                    cmd.pdfAttachSpecifyInsert, &cmd.pdfAttachSpecifyInsert,
                    cmd.pdfAttachPhase == Ph::WaitInsertPoint)) {
  }
  if (ParameterRow("Scale:",       &cmd.pdfAttachScale, "%.6f",
                    cmd.pdfAttachSpecifyScale, &cmd.pdfAttachSpecifyScale,
                    false)) {
  }
  if (ParameterRow("Rotation °:", &cmd.pdfAttachRotDeg, "%.2f",
                    cmd.pdfAttachSpecifyRot, &cmd.pdfAttachSpecifyRot,
                    false)) {
  }

  ImGui::Spacing();
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("Raster DPI:");
  ImGui::SameLine(140.f);
  ImGui::SetNextItemWidth(80.f);
  ImGui::SliderFloat("##DPI", &cmd.pdfAttachRasterDpi, 72.f, 300.f, "%.0f");

  // --- Snap toggles ---
  ImGui::Spacing();
  ImGui::SeparatorText("Snap Recognition");
  ImGui::Checkbox("Lines",   &cmd.pdfAttachSnapLines);
  ImGui::SameLine();
  ImGui::Checkbox("Circles", &cmd.pdfAttachSnapCircles);
  ImGui::SameLine();
  ImGui::Checkbox("Text",    &cmd.pdfAttachSnapText);

  // --- Action buttons ---
  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  // --- Building phase: rasterize running in background ---
  if (cmd.pdfAttachPhase == Ph::Building && cmd.pdfAttachAsync) {
    auto& ab = *cmd.pdfAttachAsync;
    const char* kSpinner[] = { "|", "/", "-", "\\" };
    const int   spin = static_cast<int>(ImGui::GetTime() * 8.0) & 3;
    ImGui::TextDisabled("Rasterizing...  %s", kSpinner[spin]);

    if (ab.done.load(std::memory_order_acquire)) {
      // Background thread finished — upload texture on main thread.
      // Save fields from ab before reset() destroys it.
      const bool specifyInsert = ab.specifyInsert;
      PdfAttachment att;
      const bool ok = PdfAttach_FinishBuild(ab.result, cmd.pdfAttachFilePath,
                                             cmd.pdfAttachSelectedPage, att);
      if (ab.thread.joinable()) ab.thread.join();
      cmd.pdfAttachAsync.reset();  // ab is a dangling ref after this point

      if (ok) {
        if (specifyInsert) {
          // Store the built attachment in the preview and wait for viewport pick.
          cmd.pdfAttachPreview      = std::move(att);
          cmd.pdfAttachPreviewReady = true;
          cmd.pdfAttachDialogOpen   = false;
          cmd.pdfAttachPhase        = Ph::WaitInsertPoint;
          log.push_back("PDFATTACH — click in viewport to specify insertion point.  ESC to cancel.");
        } else {
          att.insertX     = cmd.pdfAttachInsertX;
          att.insertY     = cmd.pdfAttachInsertY;
          att.scale       = cmd.pdfAttachScale;
          att.rotationDeg = cmd.pdfAttachRotDeg;
          cmd.pdfAttachments.push_back(std::move(att));
          log.push_back("PDFATTACH — underlay placed.");
          if (cmd.pdfDraftCache) {
            PdfDraftCache_Free(cmd.pdfDraftCache);
            cmd.pdfDraftCache = nullptr;
          }
          if (cmd.pdfAttachPreviewReady) {
            PdfAttach_ReleaseTexture(cmd.pdfAttachPreview);
            cmd.pdfAttachPreviewReady = false;
          }
          cmd.pdfAttachDialogOpen = false;
          cmd.pdfAttachPhase      = Ph::WaitDialog;
          cmd.active              = AppCommandState::Kind::None;
        }
      } else {
        log.push_back("PDFATTACH — failed to rasterize page.");
        cmd.pdfAttachPhase = Ph::WaitDialog;  // return to dialog so user can retry
      }
    }

    // While building, disable both action buttons.
    ImGui::BeginDisabled();
    ImGui::Button("Attach", ImVec2(120.f, 0.f));
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(80.f, 0.f))) {
      CancelPdfAttachCommand(cmd, log);
    }
    ImGui::End();
    return cmd.active == K::PdfAttach;
  }

  const bool canAttach =
      cmd.pdfAttachFilePath[0] != '\0' && cmd.pdfDraftCache != nullptr &&
      PdfDraftCache_PageCount(cmd.pdfDraftCache) > 0;

  if (!canAttach) ImGui::BeginDisabled();
  if (ImGui::Button("Attach", ImVec2(120.f, 0.f))) {
    // Capture params before closing dialog or switching phase.
    const std::string  capturedPath  = cmd.pdfAttachFilePath;
    const int          capturedPage  = cmd.pdfAttachSelectedPage;
    const float        capturedDpi   = cmd.pdfAttachRasterDpi;
    const bool         capturedLines = cmd.pdfAttachSnapLines;
    const bool         capturedCirc  = cmd.pdfAttachSnapCircles;
    const bool         capturedText  = cmd.pdfAttachSnapText;
    const bool         capturedSpec  = cmd.pdfAttachSpecifyInsert;

    // Kick off the background rasterize task.
    // Building phase keeps the dialog open (spinner) until the thread completes.
    auto ab = std::make_unique<AppCommandState::AsyncBuild>();
    ab->specifyInsert = capturedSpec;
    auto* abPtr = ab.get();

    abPtr->thread = std::thread([abPtr, capturedPath, capturedPage, capturedDpi,
                                  capturedLines, capturedCirc, capturedText]() {
      PdfAttach_BuildToBuffer(capturedPath.c_str(), capturedPage, capturedDpi,
                               capturedLines, capturedCirc, capturedText,
                               abPtr->result);
      abPtr->done.store(true, std::memory_order_release);
    });

    cmd.pdfAttachAsync = std::move(ab);
    cmd.pdfAttachPhase = Ph::Building;
    log.push_back("PDFATTACH — rasterizing, please wait...");
  }
  if (!canAttach) ImGui::EndDisabled();

  ImGui::SameLine();
  if (ImGui::Button("Cancel", ImVec2(80.f, 0.f))) {
    CancelPdfAttachCommand(cmd, log);
  }

  ImGui::End();
  return cmd.active == K::PdfAttach;
}

