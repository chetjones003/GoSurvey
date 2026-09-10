#include "MarkdownImGui.hpp"

#include "md4c.h"

#include <imgui.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#endif

#include <cassert>
#include <string>

namespace {

struct MdRenderState {
  int listDepth = 0;
  bool inStrong = false;
  bool inEm = false;
  bool inCode = false;
  bool inLink = false;
  std::string linkHref;
  std::string lineAccum;
  bool paragraphOpen = false;
  bool tightListItem = false;
};

void FlushLine(MdRenderState& st) {
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

int EnterBlock(MD_BLOCKTYPE type, void* detail, void* userdata) {
  auto* st = static_cast<MdRenderState*>(userdata);
  assert(st != nullptr);
  (void)detail;
  switch (type) {
    case MD_BLOCK_DOC:
      break;
    case MD_BLOCK_H: {
      FlushLine(*st);
      const auto* h = static_cast<MD_BLOCK_H_DETAIL*>(detail);
      const float scale = (h && h->level <= 1) ? 1.55f : (h && h->level == 2) ? 1.35f : 1.2f;
      ImGui::SetWindowFontScale(scale);
      st->paragraphOpen = true;
      break;
    }
    case MD_BLOCK_P:
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
      break;
    case MD_BLOCK_QUOTE:
      FlushLine(*st);
      ImGui::Indent();
      break;
    case MD_BLOCK_HR:
      FlushLine(*st);
      ImGui::Separator();
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
    case MD_BLOCK_H:
      FlushLine(*st);
      ImGui::SetWindowFontScale(1.f);
      ImGui::Spacing();
      st->paragraphOpen = false;
      break;
    case MD_BLOCK_P:
      FlushLine(*st);
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
      ImGui::Spacing();
      break;
    case MD_BLOCK_QUOTE:
      FlushLine(*st);
      ImGui::Unindent();
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
      break;
    case MD_SPAN_EM:
      st->inEm = false;
      break;
    case MD_SPAN_CODE:
      st->inCode = false;
      break;
    case MD_SPAN_A:
      // Link text was accumulated; emit as a clickable text button-ish line.
      if (!st->lineAccum.empty()) {
        const ImVec4 linkCol = ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered);
        ImGui::PushStyleColor(ImGuiCol_Text, linkCol);
        if (ImGui::Selectable(st->lineAccum.c_str(), false))
          OpenUrl(st->linkHref);
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("%s", st->linkHref.c_str());
        st->lineAccum.clear();
      }
      st->inLink = false;
      st->linkHref.clear();
      break;
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

void DrawMarkdownImGui(std::string_view markdown) {
  if (markdown.empty()) {
    ImGui::TextDisabled("(empty)");
    return;
  }

  MdRenderState st;
  MD_PARSER parser{};
  parser.abi_version = 0;
  parser.flags = 0;  // CommonMark defaults (ADR-056)
  parser.enter_block = EnterBlock;
  parser.leave_block = LeaveBlock;
  parser.enter_span = EnterSpan;
  parser.leave_span = LeaveSpan;
  parser.text = OnText;
  parser.debug_log = nullptr;
  parser.syntax = nullptr;

  const int rc = md_parse(markdown.data(), static_cast<MD_SIZE>(markdown.size()), &parser, &st);
  if (rc != 0) {
    ImGui::TextWrapped("%.*s", static_cast<int>(markdown.size()), markdown.data());
    return;
  }
  FlushLine(st);
}
