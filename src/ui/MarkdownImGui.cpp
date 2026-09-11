#include "MarkdownImGui.hpp"

#include "WikiHelp.hpp"

#include "md4c.h"

#include <imgui.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#endif

#include <algorithm>
#include <cassert>
#include <string>

namespace {

struct MdRenderState {
  const MarkdownImGuiHooks* hooks = nullptr;
  int                       listDepth = 0;
  bool                      inStrong = false;
  bool                      inEm = false;
  bool                      inCode = false;
  bool                      inLink = false;
  std::string               linkHref;
  std::string               lineAccum;
  bool                      paragraphOpen = false;
  bool                      tightListItem = false;
  bool                      inTable = false;
  bool                      inTableCell = false;
  bool                      inTableHeader = false;
  int                       tableColumns = 0;
  int                       nextTableIndex = 0;
  int                       tableSerial = 0;
  int                       textStylePushes = 0;
  bool                      headingFontPushed = false;
  bool                      monoFontPushed = false;
};

void SetupMarkdownTableColumns(const int colCount) {
  assert(colCount > 0);
  if (colCount == 1) {
    ImGui::TableSetupColumn("##mdc0", ImGuiTableColumnFlags_WidthStretch, 1.f);
    return;
  }
  if (colCount == 2) {
    ImGui::TableSetupColumn("##mdc0", ImGuiTableColumnFlags_WidthStretch, 0.34f);
    ImGui::TableSetupColumn("##mdc1", ImGuiTableColumnFlags_WidthStretch, 0.66f);
    return;
  }
  if (colCount == 3) {
    ImGui::TableSetupColumn("##mdc0", ImGuiTableColumnFlags_WidthStretch, 0.24f);
    ImGui::TableSetupColumn("##mdc1", ImGuiTableColumnFlags_WidthStretch, 0.16f);
    ImGui::TableSetupColumn("##mdc2", ImGuiTableColumnFlags_WidthStretch, 0.60f);
    return;
  }
  const float weight = 1.f / static_cast<float>(colCount);
  for (int i = 0; i < colCount; ++i) {
    const std::string id = "##mdc" + std::to_string(i);
    ImGui::TableSetupColumn(id.c_str(), ImGuiTableColumnFlags_WidthStretch, weight);
  }
}

void FlushTableCellText(MdRenderState& st) {
  if (st.lineAccum.empty())
    return;

  const float wrapX = ImGui::GetCursorPos().x + ImGui::GetContentRegionAvail().x;
  if (wrapX > ImGui::GetCursorPos().x)
    ImGui::PushTextWrapPos(wrapX);
  ImGui::TextUnformatted(st.lineAccum.c_str());
  if (wrapX > ImGui::GetCursorPos().x)
    ImGui::PopTextWrapPos();
  st.lineAccum.clear();
}

ImVec4 MarkdownLinkColor() {
  // Readable steel-blue on dark dialog panels — not ButtonHovered grey.
  return ImVec4(0.62f, 0.82f, 1.00f, 1.f);
}

ImVec4 MarkdownStrongColor() {
  const ImVec4 base = ImGui::GetStyleColorVec4(ImGuiCol_Text);
  return ImVec4((std::min)(1.f, base.x + 0.08f), (std::min)(1.f, base.y + 0.08f),
                (std::min)(1.f, base.z + 0.06f), base.w);
}

ImVec4 MarkdownQuoteColor() {
  const ImVec4 base = ImGui::GetStyleColorVec4(ImGuiCol_Text);
  return ImVec4(base.x * 0.92f + 0.06f, base.y * 0.92f + 0.07f, base.z * 0.92f + 0.09f, base.w);
}

void PushMarkdownTextColor(const ImVec4& col, MdRenderState& st) {
  ImGui::PushStyleColor(ImGuiCol_Text, col);
  ++st.textStylePushes;
}

void PopMarkdownTextColor(MdRenderState& st) {
  if (st.textStylePushes <= 0)
    return;
  ImGui::PopStyleColor();
  --st.textStylePushes;
}

void FlushLine(MdRenderState& st) {
  if (st.inTableCell) {
    FlushTableCellText(st);
    return;
  }

  if (st.lineAccum.empty() && !st.paragraphOpen)
    return;

  const char* text = st.lineAccum.c_str();
  if (st.listDepth > 0) {
    ImGui::BulletText("%s", text);
  } else {
    ImGui::TextWrapped("%s", text);
  }
  st.lineAccum.clear();
}

void AppendText(MdRenderState& st, const MD_CHAR* text, MD_SIZE size) {
  assert(text != nullptr || size == 0);
  if (size == 0)
    return;
  st.lineAccum.append(text, static_cast<size_t>(size));
}

void OpenUrl(const std::string& url) {
  if (url.empty())
    return;
#ifdef _WIN32
  ::ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
  (void)url;
#endif
}

void ActivateLink(MdRenderState& st, const std::string& href) {
  assert(!href.empty());
  if (st.hooks && st.hooks->onLink && st.hooks->onLink(href))
    return;
  OpenUrl(href);
}

void DrawWrappedLinkText(MdRenderState& st, const char* label, const std::string& href) {
  if (label == nullptr || label[0] == '\0')
    return;

  ImGui::PushStyleColor(ImGuiCol_Text, MarkdownLinkColor());
  const float wrapX = ImGui::GetCursorPos().x + ImGui::GetContentRegionAvail().x;
  if (wrapX > ImGui::GetCursorPos().x)
    ImGui::PushTextWrapPos(wrapX);
  ImGui::TextUnformatted(label);
  if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    ActivateLink(st, href);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("%s", href.c_str());
  if (wrapX > ImGui::GetCursorPos().x)
    ImGui::PopTextWrapPos();
  ImGui::PopStyleColor();
}

void DrawImage(MdRenderState& st, const std::string& src, const std::string& alt) {
  if (!st.hooks || !st.hooks->resolveImage) {
    ImGui::TextDisabled("[image: %s]", alt.empty() ? src.c_str() : alt.c_str());
    return;
  }

  int w = 0;
  int h = 0;
  const unsigned int tex = st.hooks->resolveImage(src, &w, &h);
  if (tex == 0 || w <= 0 || h <= 0) {
    ImGui::TextDisabled("[image unavailable: %s]", src.c_str());
    return;
  }

  const float maxW = ImGui::GetContentRegionAvail().x;
  float         drawW = static_cast<float>(w);
  float         drawH = static_cast<float>(h);
  if (drawW > maxW && maxW > 0.f) {
    const float scale = maxW / drawW;
    drawW             = maxW;
    drawH *= scale;
  }

  ImGui::Image(static_cast<ImTextureID>(static_cast<std::intptr_t>(tex)), ImVec2(drawW, drawH));
  if (!alt.empty() && ImGui::IsItemHovered())
    ImGui::SetTooltip("%s", alt.c_str());
  ImGui::Spacing();
}

int EnterBlock(MD_BLOCKTYPE type, void* detail, void* userdata) {
  auto* st = static_cast<MdRenderState*>(userdata);
  assert(st != nullptr);
  switch (type) {
    case MD_BLOCK_DOC:
      break;
    case MD_BLOCK_H: {
      FlushLine(*st);
      const auto* h = static_cast<MD_BLOCK_H_DETAIL*>(detail);
      const float scale = (h && h->level <= 1) ? 1.55f : (h && h->level == 2) ? 1.35f : 1.2f;
      if (st->hooks != nullptr && st->hooks->headingFont != nullptr) {
        ImGui::PushFont(st->hooks->headingFont);
        st->headingFontPushed = true;
      }
      ImGui::SetWindowFontScale(scale);
      st->paragraphOpen = true;
      break;
    }
    case MD_BLOCK_P:
      if (!st->inTableCell)
        FlushLine(*st);
      st->paragraphOpen = true;
      break;
    case MD_BLOCK_UL:
    case MD_BLOCK_OL:
      FlushLine(*st);
      ++st->listDepth;
      break;
    case MD_BLOCK_LI:
      FlushLine(*st);
      st->tightListItem = true;
      st->paragraphOpen = true;
      break;
    case MD_BLOCK_CODE:
      FlushLine(*st);
      st->inCode = true;
      st->paragraphOpen = true;
      if (st->hooks != nullptr && st->hooks->monoFont != nullptr) {
        ImGui::PushFont(st->hooks->monoFont);
        st->monoFontPushed = true;
      }
      break;
    case MD_BLOCK_QUOTE:
      FlushLine(*st);
      ImGui::Indent();
      PushMarkdownTextColor(MarkdownQuoteColor(), *st);
      break;
    case MD_BLOCK_HR:
      FlushLine(*st);
      ImGui::Separator();
      break;
    case MD_BLOCK_TABLE: {
      FlushLine(*st);
      const auto* td = static_cast<const MD_BLOCK_TABLE_DETAIL*>(detail);
      st->tableColumns = (td && td->col_count > 0) ? static_cast<int>(td->col_count) : 1;
      st->tableSerial  = ++st->nextTableIndex;
      ImGui::Spacing();
      const std::string tableId = "##mdTable" + std::to_string(st->tableSerial);
      const ImGuiTableFlags tableFlags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuter |
                                         ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp |
                                         ImGuiTableFlags_PadOuterX;
      if (ImGui::BeginTable(tableId.c_str(), st->tableColumns, tableFlags)) {
        SetupMarkdownTableColumns(st->tableColumns);
        st->inTable = true;
        break;
      }
      st->tableColumns = 0;
      st->tableSerial  = 0;
      break;
    }
    case MD_BLOCK_THEAD:
    case MD_BLOCK_TBODY:
      break;
    case MD_BLOCK_TR:
      if (st->tableColumns > 0)
        ImGui::TableNextRow();
      break;
    case MD_BLOCK_TH:
      if (st->tableColumns > 0) {
        ImGui::TableNextColumn();
        st->inTableCell = true;
        st->inTableHeader = true;
        st->paragraphOpen = true;
        PushMarkdownTextColor(MarkdownStrongColor(), *st);
      }
      break;
    case MD_BLOCK_TD:
      if (st->tableColumns > 0) {
        ImGui::TableNextColumn();
        st->inTableCell = true;
        st->paragraphOpen = true;
      }
      break;
    default:
      break;
  }
  return 0;
}

int LeaveBlock(MD_BLOCKTYPE type, void* detail, void* userdata) {
  auto* st = static_cast<MdRenderState*>(userdata);
  assert(st != nullptr);
  (void)detail;
  switch (type) {
    case MD_BLOCK_H: {
      const std::string headingText = st->lineAccum;
      FlushLine(*st);
      ImGui::SetWindowFontScale(1.f);
      if (st->headingFontPushed) {
        ImGui::PopFont();
        st->headingFontPushed = false;
      }
      ImGui::Spacing();
      st->paragraphOpen = false;
      if (st->hooks && !st->hooks->scrollToCommandPrimary.empty() &&
          WikiHeadingMatchesCommand(headingText, st->hooks->scrollToCommandPrimary)) {
        ImGui::SetScrollHereY(0.08f);
        if (st->hooks->scrollApplied != nullptr)
          *st->hooks->scrollApplied = true;
      }
      break;
    }
    case MD_BLOCK_P:
      FlushLine(*st);
      if (!st->inTableCell)
        ImGui::Spacing();
      st->paragraphOpen = false;
      break;
    case MD_BLOCK_UL:
    case MD_BLOCK_OL:
      FlushLine(*st);
      if (st->listDepth > 0)
        --st->listDepth;
      break;
    case MD_BLOCK_LI:
      FlushLine(*st);
      st->tightListItem = false;
      st->paragraphOpen = false;
      break;
    case MD_BLOCK_CODE:
      FlushLine(*st);
      st->inCode = false;
      st->paragraphOpen = false;
      if (st->monoFontPushed) {
        ImGui::PopFont();
        st->monoFontPushed = false;
      }
      ImGui::Spacing();
      break;
    case MD_BLOCK_QUOTE:
      FlushLine(*st);
      PopMarkdownTextColor(*st);
      ImGui::Unindent();
      break;
    case MD_BLOCK_TH:
      FlushLine(*st);
      PopMarkdownTextColor(*st);
      st->inTableCell = false;
      st->inTableHeader = false;
      st->paragraphOpen = false;
      break;
    case MD_BLOCK_TD:
      FlushLine(*st);
      st->inTableCell = false;
      st->paragraphOpen = false;
      break;
    case MD_BLOCK_TABLE:
      if (st->tableColumns > 0)
        ImGui::EndTable();
      st->inTable      = false;
      st->tableColumns = 0;
      st->tableSerial  = 0;
      ImGui::Spacing();
      break;
    default:
      FlushLine(*st);
      break;
  }
  return 0;
}

int EnterSpan(MD_SPANTYPE type, void* detail, void* userdata) {
  auto* st = static_cast<MdRenderState*>(userdata);
  assert(st != nullptr);
  switch (type) {
    case MD_SPAN_STRONG:
      st->inStrong = true;
      PushMarkdownTextColor(MarkdownStrongColor(), *st);
      break;
    case MD_SPAN_EM:
      st->inEm = true;
      break;
    case MD_SPAN_CODE:
      st->inCode = true;
      break;
    case MD_SPAN_A: {
      st->inLink = true;
      st->linkHref.clear();
      const auto* a = static_cast<MD_SPAN_A_DETAIL*>(detail);
      if (a && a->href.size > 0)
        st->linkHref.assign(a->href.text, static_cast<size_t>(a->href.size));
      break;
    }
    case MD_SPAN_IMG: {
      FlushLine(*st);
      const auto* img = static_cast<MD_SPAN_IMG_DETAIL*>(detail);
      std::string src;
      std::string alt;
      if (img) {
        if (img->src.size > 0)
          src.assign(img->src.text, static_cast<size_t>(img->src.size));
        if (img->title.size > 0)
          alt.assign(img->title.text, static_cast<size_t>(img->title.size));
      }
      st->linkHref = src;
      st->inLink = true;
      (void)alt;
      break;
    }
    default:
      break;
  }
  return 0;
}

int LeaveSpan(MD_SPANTYPE type, void* detail, void* userdata) {
  auto* st = static_cast<MdRenderState*>(userdata);
  assert(st != nullptr);
  (void)detail;
  switch (type) {
    case MD_SPAN_STRONG:
      st->inStrong = false;
      PopMarkdownTextColor(*st);
      break;
    case MD_SPAN_EM:
      st->inEm = false;
      break;
    case MD_SPAN_CODE:
      st->inCode = false;
      break;
    case MD_SPAN_A:
      if (!st->lineAccum.empty()) {
        if (st->inTableCell)
          DrawWrappedLinkText(*st, st->lineAccum.c_str(), st->linkHref);
        else {
          ImGui::PushStyleColor(ImGuiCol_Text, MarkdownLinkColor());
          if (ImGui::Selectable(st->lineAccum.c_str(), false, ImGuiSelectableFlags_None))
            ActivateLink(*st, st->linkHref);
          ImGui::PopStyleColor();
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", st->linkHref.c_str());
        }
        st->lineAccum.clear();
      }
      st->inLink = false;
      st->linkHref.clear();
      break;
    case MD_SPAN_IMG: {
      const std::string alt = st->lineAccum;
      st->lineAccum.clear();
      DrawImage(*st, st->linkHref, alt);
      st->inLink = false;
      st->linkHref.clear();
      break;
    }
    default:
      break;
  }
  return 0;
}

int OnText(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size, void* userdata) {
  auto* st = static_cast<MdRenderState*>(userdata);
  assert(st != nullptr);
  switch (type) {
    case MD_TEXT_NORMAL:
    case MD_TEXT_CODE:
      AppendText(*st, text, size);
      break;
    case MD_TEXT_NULLCHAR:
      AppendText(*st, "\xEF\xBF\xBD", 3);  // U+FFFD
      break;
    case MD_TEXT_BR:
      FlushLine(*st);
      break;
    case MD_TEXT_SOFTBR:
      AppendText(*st, " ", 1);
      break;
    case MD_TEXT_ENTITY:
      AppendText(*st, text, size);
      break;
    default:
      AppendText(*st, text, size);
      break;
  }
  return 0;
}

}  // namespace

void DrawMarkdownImGui(std::string_view markdown, const MarkdownImGuiHooks* hooks) {
  if (markdown.empty()) {
    ImGui::TextDisabled("(empty)");
    return;
  }

  MdRenderState st;
  st.hooks = hooks;
  MD_PARSER parser{};
  parser.abi_version = 0;
  parser.flags       = MD_FLAG_TABLES;
  parser.enter_block = EnterBlock;
  parser.leave_block = LeaveBlock;
  parser.enter_span  = EnterSpan;
  parser.leave_span  = LeaveSpan;
  parser.text        = OnText;
  parser.debug_log   = nullptr;
  parser.syntax      = nullptr;

  const int rc = md_parse(markdown.data(), static_cast<MD_SIZE>(markdown.size()), &parser, &st);
  if (rc != 0) {
    ImGui::TextWrapped("%.*s", static_cast<int>(markdown.size()), markdown.data());
    return;
  }
  FlushLine(st);
  while (st.textStylePushes > 0)
    PopMarkdownTextColor(st);
}
