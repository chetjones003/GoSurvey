#include "CadUi.hpp"
#include "CadUiInternal.hpp"

#include "CadColor.hpp"
#include "CadCommands.hpp"
#include "DxfColors.hpp"
#include "PaperSpace.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <string>

namespace {

enum class PickerTab : int { Index = 0, TrueColor = 1 };

constexpr float kSwatchMain = 18.f;
constexpr float kSwatchStd = 26.f;
constexpr float kSwatchGap = 2.f;
constexpr int kAciColumns = 24;
constexpr int kAciRows = 10;
constexpr int kAciGapAfterRow = 4;  // white band between rows 4 and 5 (AutoCAD layout)

ImVec4 RgbPackedToImVec4(uint32_t rgb) {
  return ImVec4(static_cast<float>((rgb >> 16) & 0xFFu) / 255.f,
                static_cast<float>((rgb >> 8) & 0xFFu) / 255.f,
                static_cast<float>(rgb & 0xFFu) / 255.f, 1.f);
}

/// AutoCAD column-major index: each column is one hue, 10 shades top→bottom.
int AciGridIndex(int column, int row) { return 10 + column * kAciRows + row; }

bool DrawAciSwatch(int aci, bool selected, float side) {
  const uint32_t rgb = DxfRgbPackedFromAci(aci);
  const ImVec4 col = RgbPackedToImVec4(rgb);
  ImGui::PushID(aci);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.f);
  if (selected)
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.f);
  const bool hit =
      ImGui::ColorButton("##aci", col,
                         ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop |
                             (selected ? 0 : ImGuiColorEditFlags_NoBorder),
                         ImVec2(side, side));
  if (selected)
    ImGui::PopStyleVar();
  ImGui::PopStyleVar();
  ImGui::PopID();
  return hit;
}

void DrawAciMainGrid(int* selectedAci) {
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(kSwatchGap, kSwatchGap));
  for (int col = 0; col < kAciColumns; ++col) {
    if (col > 0)
      ImGui::SameLine(0.f, kSwatchGap);
    ImGui::BeginGroup();
    for (int row = 0; row < kAciRows; ++row) {
      if (row == kAciGapAfterRow + 1)
        ImGui::Dummy(ImVec2(kSwatchMain, 5.f));
      const int aci = AciGridIndex(col, row);
      if (DrawAciSwatch(aci, *selectedAci == aci, kSwatchMain))
        *selectedAci = aci;
    }
    ImGui::EndGroup();
  }
  ImGui::PopStyleVar();
}

void InitPickerFromStorage(const std::string& storage, int* selectedAci, float trueRgb[3], PickerTab* tab) {
  int aci = 7;
  if (CadColorTryGetAci(storage, &aci))
    *selectedAci = aci;
  else
    *selectedAci = 7;
  CadColorResolveRgb(storage, 1.f, 1.f, 1.f, trueRgb);
  if (storage.size() >= 7 && storage[0] == '#')
    *tab = PickerTab::TrueColor;
  else
    *tab = PickerTab::Index;
}

std::string PickerResultStorage(int selectedAci, PickerTab tab, const float trueRgb[3]) {
  if (tab == PickerTab::TrueColor) {
    const int r = static_cast<int>(std::lround(std::clamp(trueRgb[0], 0.f, 1.f) * 255.f));
    const int g = static_cast<int>(std::lround(std::clamp(trueRgb[1], 0.f, 1.f) * 255.f));
    const int b = static_cast<int>(std::lround(std::clamp(trueRgb[2], 0.f, 1.f) * 255.f));
    const uint32_t packed = (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) |
                            static_cast<uint32_t>(b);
    return CadColorStorageFromRgbPacked(packed);
  }
  return CadColorStorageFromAci(selectedAci);
}

void ApplyPickerResult(AppCommandState& cmd, const std::string& storage) {
  using T = AppCommandState::SelectColorTarget;
  switch (cmd.selectColorTarget) {
  case T::LayerTable:
    if (cmd.selectColorLayerRowIndex < cmd.drawingLayerTable.size()) {
      cmd.drawingLayerTable[cmd.selectColorLayerRowIndex].color = storage;
      BumpCadGpuCache(cmd);
    }
    break;
  case T::VpLayerColor: {
    Viewport* vp = CurrentViewport(cmd);
    if (vp != nullptr) {
      SetViewportLayerColor(*vp, cmd.selectColorVpLayerName, storage);
      BumpCadGpuCache(cmd);
    }
    break;
  }
  case T::EntitySelection:
    ApplyColorToSelection(cmd, storage);
    break;
  case T::QuickSelectValue:
    std::snprintf(cmd.qsValueBuf, sizeof(cmd.qsValueBuf), "%s", storage.c_str());
    break;
  default:
    break;
  }
}

float FooterHeight() {
  return ImGui::GetFrameHeightWithSpacing() * 2.f + 56.f + ImGui::GetStyle().ItemSpacing.y * 3.f;
}

void DrawIndexColorBody(AppCommandState& cmd, int* selectedAci) {
  ImGui::TextUnformatted("AutoCAD Color Index (ACI):");
  ImGui::Spacing();

  const float footerH = FooterHeight();
  if (ImGui::BeginChild("##aci_body", ImVec2(0.f, -footerH), false)) {
    DrawAciMainGrid(selectedAci);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Bottom band: standard 1–9 + grayscale left; ByLayer/ByBlock + preview right (AutoCAD layout).
    const float bandH = kSwatchStd * 2.f + 24.f;
    ImGui::BeginChild("##aci_band", ImVec2(0.f, bandH), false);
    {
      ImGui::BeginGroup();
      for (int aci = 1; aci <= 9; ++aci) {
        if (aci > 1)
          ImGui::SameLine(0.f, 2.f);
        if (DrawAciSwatch(aci, *selectedAci == aci, kSwatchStd))
          *selectedAci = aci;
      }
      ImGui::Dummy(ImVec2(0.f, 4.f));
      for (int aci = 250; aci <= 255; ++aci) {
        if (aci > 250)
          ImGui::SameLine(0.f, 2.f);
        if (DrawAciSwatch(aci, *selectedAci == aci, kSwatchStd))
          *selectedAci = aci;
      }
      ImGui::EndGroup();

      ImGui::SameLine(0.f, 24.f);
      ImGui::BeginGroup();
      if (cmd.selectColorAllowByLayer &&
          ImGui::Button("ByLayer", ImVec2(110.f, kSwatchStd))) {
        ApplyPickerResult(cmd, "ByLayer");
        ImGui::CloseCurrentPopup();
      }
      if (cmd.selectColorAllowByBlock &&
          ImGui::Button("ByBlock", ImVec2(110.f, kSwatchStd))) {
        ApplyPickerResult(cmd, "ByBlock");
        ImGui::CloseCurrentPopup();
      }
      if (cmd.selectColorTarget == AppCommandState::SelectColorTarget::VpLayerColor &&
          ImGui::Button("Clear override", ImVec2(110.f, 0.f))) {
        Viewport* vp = CurrentViewport(cmd);
        if (vp != nullptr) {
          ClearViewportLayerColor(*vp, cmd.selectColorVpLayerName);
          BumpCadGpuCache(cmd);
        }
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndGroup();

      float previewRgb[3];
      CadColorResolveRgb(CadColorStorageFromAci(*selectedAci), 1.f, 1.f, 1.f, previewRgb);
      const float previewSide = kSwatchStd * 2.f + 4.f;
      ImGui::SameLine(0.f, 24.f);
      ImGui::ColorButton("##bandpreview", ImVec4(previewRgb[0], previewRgb[1], previewRgb[2], 1.f),
                         ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder,
                         ImVec2(previewSide, previewSide));
    }
    ImGui::EndChild();
  }
  ImGui::EndChild();
}

} // namespace

void RequestSelectColor(AppCommandState& cmd, const std::string& initialStorage,
                        AppCommandState::SelectColorTarget target, bool allowByLayer, bool allowByBlock,
                        size_t layerRowIndex, const std::string& vpLayerName) {
  cmd.selectColorInitial = initialStorage;
  cmd.selectColorTarget = target;
  cmd.selectColorAllowByLayer = allowByLayer;
  cmd.selectColorAllowByBlock = allowByBlock;
  cmd.selectColorLayerRowIndex = layerRowIndex;
  cmd.selectColorVpLayerName = vpLayerName;
  cmd.showSelectColorPopup = true;
}

bool DrawColorStorageCell(const std::string& storage, float defaultR, float defaultG, float defaultB) {
  float rgb[3];
  CadColorResolveRgb(storage, defaultR, defaultG, defaultB, rgb);
  const float side = std::max(18.f, ImGui::GetFrameHeight() - 2.f);
  const bool swatchHit = ImGui::ColorButton("##sw", ImVec4(rgb[0], rgb[1], rgb[2], 1.f),
                                            ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop,
                                            ImVec2(side, side));
  ImGui::SameLine(0.f, 6.f);
  const std::string label = CadColorDisplayLabel(storage);
  ImGui::AlignTextToFramePadding();
  const bool labelHit = ImGui::Selectable(label.c_str(), false, 0, ImVec2(0.f, side));
  return swatchHit || labelHit;
}

void DrawSelectColorPopup(AppCommandState& cmd) {
  static int selectedAci = 7;
  static float trueRgb[3] = {1.f, 1.f, 1.f};
  static PickerTab tab = PickerTab::Index;

  if (cmd.showSelectColorPopup) {
    InitPickerFromStorage(cmd.selectColorInitial, &selectedAci, trueRgb, &tab);
    ImGui::OpenPopup("Select Color##gosurvey");
    cmd.showSelectColorPopup = false;
  }

  // Wide enough for 24 hue columns; tall enough for grid + bottom band + pinned footer.
  ImGui::SetNextWindowSize(ImVec2(760.f, 720.f), ImGuiCond_Always);
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));

  PushProductDialogAccent();
  if (!ImGui::BeginPopupModal("Select Color##gosurvey", nullptr,
                              ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize)) {
    PopProductDialogAccent();
    return;
  }
  PaintProductDialogAccentFrame();
  BeginStyledDialog();

  if (ImGui::BeginTabBar("##colortabs")) {
    if (ImGui::BeginTabItem("Index Color")) {
      tab = PickerTab::Index;
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("True Color")) {
      tab = PickerTab::TrueColor;
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }

  if (tab == PickerTab::Index) {
    DrawIndexColorBody(cmd, &selectedAci);
  } else {
    const float footerH = FooterHeight();
    ImGui::BeginChild("##true_body", ImVec2(0.f, -footerH), false);
    ImGui::ColorPicker3("##truepick", trueRgb,
                        ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_InputRGB |
                            ImGuiColorEditFlags_PickerHueBar);
    ImGui::EndChild();
  }

  // Pinned footer — preview + OK/Cancel always visible.
  ImGui::Separator();
  {
    float previewRgb[3];
    if (tab == PickerTab::Index)
      CadColorResolveRgb(CadColorStorageFromAci(selectedAci), 1.f, 1.f, 1.f, previewRgb);
    else
      previewRgb[0] = trueRgb[0], previewRgb[1] = trueRgb[1], previewRgb[2] = trueRgb[2];
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Color:");
    ImGui::SameLine(80.f);
    const std::string previewStorage = PickerResultStorage(selectedAci, tab, trueRgb);
    char nameBuf[64]{};
    std::snprintf(nameBuf, sizeof(nameBuf), "%s", CadColorDisplayLabel(previewStorage).c_str());
    ImGui::SetNextItemWidth(200.f);
    ImGui::InputText("##colorname", nameBuf, sizeof(nameBuf), ImGuiInputTextFlags_ReadOnly);
    ImGui::SameLine();
    ImGui::ColorButton("##preview", ImVec4(previewRgb[0], previewRgb[1], previewRgb[2], 1.f),
                       ImGuiColorEditFlags_NoTooltip, ImVec2(36.f, ImGui::GetFrameHeight()));
    int aci = -1;
    if (CadColorTryGetAci(previewStorage, &aci)) {
      ImGui::SameLine();
      ImGui::TextDisabled("Index %d", aci);
    }
  }

  ImGui::Spacing();
  if (StyledButton("OK", ImVec2(120.f, 0.f), true)) {
    ApplyPickerResult(cmd, PickerResultStorage(selectedAci, tab, trueRgb));
    ImGui::CloseCurrentPopup();
  }
  ImGui::SameLine();
  if (StyledButton("Cancel", ImVec2(120.f, 0.f)))
    ImGui::CloseCurrentPopup();

  ImGui::EndPopup();
  PopProductDialogAccent();
}
