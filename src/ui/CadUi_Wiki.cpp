#include "CadUi.hpp"

#include "AppIcon.hpp"
#include "AppPaths.hpp"
#include "FontRegistry.hpp"
#include "MarkdownImGui.hpp"
#include "WikiContent.hpp"
#include "WikiHelp.hpp"

#include "CadCommands.hpp"

#include <imgui.h>

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

struct WikiImageCacheEntry {
  unsigned int tex = 0;
  int          w = 0;
  int          h = 0;
};

ImVec4 Lerp(const ImVec4& a, const ImVec4& b, const float t) {
  return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t,
                a.w + (b.w - a.w) * t);
}

ImVec4 WikiAccent() { return ImVec4(0.26f, 0.56f, 0.86f, 1.f); }
ImVec4 WikiAccentHi() { return ImVec4(0.34f, 0.64f, 0.95f, 1.f); }
ImVec4 WikiAccentLo() { return ImVec4(0.20f, 0.45f, 0.72f, 1.f); }

bool WikiIsDark() { return ImGui::GetStyleColorVec4(ImGuiCol_WindowBg).x < 0.35f; }

struct WikiPanelColors {
  ImVec4 winBg;
  ImVec4 sidebarBg;
  ImVec4 bodyBg;
  ImVec4 text;
  ImVec4 textMuted;
  ImVec4 border;
  ImVec4 tableHeaderBg;
  ImVec4 tableRowBg;
};

WikiPanelColors WikiPanelColorsForTheme() {
  if (WikiIsDark()) {
    const ImVec4 winBg = ImVec4(0.03f, 0.05f, 0.10f, 1.f);
    const ImVec4 accent = WikiAccent();
    return {
        winBg,
        Lerp(ImVec4(0.025f, 0.04f, 0.08f, 1.f), accent, 0.08f),
        Lerp(ImVec4(0.02f, 0.04f, 0.08f, 1.f), accent, 0.05f),
        ImVec4(0.90f, 0.93f, 0.97f, 1.f),
        ImVec4(0.72f, 0.78f, 0.86f, 1.f),
        Lerp(winBg, accent, 0.40f),
        Lerp(winBg, accent, 0.22f),
        Lerp(winBg, accent, 0.10f),
    };
  }

  const ImVec4 winBg = ImVec4(0.98f, 0.99f, 1.f, 1.f);
  const ImVec4 accent = WikiAccent();
  return {
      winBg,
      ImVec4(0.92f, 0.95f, 0.99f, 1.f),
      ImVec4(0.96f, 0.98f, 1.f, 1.f),
      ImVec4(0.12f, 0.14f, 0.18f, 1.f),
      ImVec4(0.35f, 0.40f, 0.48f, 1.f),
      Lerp(winBg, accent, 0.35f),
      Lerp(winBg, accent, 0.12f),
      Lerp(winBg, accent, 0.06f),
  };
}

void DrawWikiWindowGradient(ImDrawList* dl, const ImVec2 a, const ImVec2 b, const float rounding) {
  assert(dl != nullptr);
  const ImVec4 accent = WikiAccent();
  const ImVec4 topL = Lerp(ImVec4(0.03f, 0.05f, 0.10f, 1.f), accent, 0.07f);
  const ImVec4 topR = Lerp(ImVec4(0.025f, 0.04f, 0.08f, 1.f), accent, 0.05f);
  const ImVec4 botR = ImVec4(0.008f, 0.012f, 0.028f, 1.f);
  const ImVec4 botL = ImVec4(0.01f, 0.016f, 0.032f, 1.f);
  dl->AddRectFilledMultiColor(a, b, ImGui::ColorConvertFloat4ToU32(topL),
                              ImGui::ColorConvertFloat4ToU32(topR),
                              ImGui::ColorConvertFloat4ToU32(botR),
                              ImGui::ColorConvertFloat4ToU32(botL));
  if (rounding > 0.f)
    dl->AddRect(a, b, ImGui::GetColorU32(Lerp(WikiAccentLo(), ImVec4(0.f, 0.f, 0.f, 1.f), 0.72f)),
                rounding, 0, 1.f);
}

WikiBundle& CachedWikiBundle() {
  static WikiBundle bundle;
  static bool       loaded = false;
  if (!loaded) {
    bundle = LoadWikiBundle();
    WikiHelpBuildIndex(bundle);
    loaded = true;
  }
  return bundle;
}

std::unordered_map<std::string, WikiImageCacheEntry>& WikiImageCache() {
  static std::unordered_map<std::string, WikiImageCacheEntry> cache;
  return cache;
}

unsigned int ResolveWikiImage(std::string_view src, int* outW, int* outH) {
  assert(outW != nullptr);
  assert(outH != nullptr);

  const WikiBundle& bundle = CachedWikiBundle();
  if (!bundle.ok || bundle.rootPath.empty())
    return 0;

  std::string fileName(src);
  constexpr std::string_view kPrefix = "wiki-img:";
  if (fileName.rfind(kPrefix, 0) == 0)
    fileName.erase(0, kPrefix.size());

  WikiImageCacheEntry& slot = WikiImageCache()[fileName];
  if (slot.tex == 0) {
    const std::filesystem::path path =
        std::filesystem::path(bundle.rootPath) / "images" / fileName;
    if (!path.empty())
      slot.tex = LoadIconTextureRgba(path, &slot.w, &slot.h);
  }

  *outW = slot.w;
  *outH = slot.h;
  return slot.tex;
}

bool HandleWikiLink(std::string_view href, AppCommandState& cmd) {
  constexpr std::string_view kPrefix = "wiki:";
  if (href.rfind(kPrefix, 0) != 0)
    return false;

  const std::string slug = WikiTitleToSlug(href.substr(kPrefix.size()));
  if (slug.empty())
    return true;

  const WikiBundle& bundle = CachedWikiBundle();
  if (bundle.pagesBySlug.find(slug) == bundle.pagesBySlug.end())
    return true;

  cmd.wikiCurrentPage     = slug;
  cmd.wikiScrollToCommand.clear();
  return true;
}

const WikiPage* FindWikiPage(const WikiBundle& bundle, const std::string& slug) {
  const auto it = bundle.pagesBySlug.find(slug);
  if (it == bundle.pagesBySlug.end())
    return nullptr;
  return &it->second;
}

}  // namespace

void RequestWikiWindow(AppCommandState& cmd, std::string_view pageSlug) {
  cmd.showWikiWindow      = true;
  cmd.wikiScrollToCommand.clear();
  if (!pageSlug.empty())
    cmd.wikiCurrentPage = WikiTitleToSlug(pageSlug);
  if (cmd.wikiCurrentPage.empty())
    cmd.wikiCurrentPage = "Home";
}

namespace {

std::string FirstCommandTokenLower(const char* cmdBuf) {
  if (cmdBuf == nullptr || cmdBuf[0] == '\0')
    return {};
  std::string query(cmdBuf);
  while (!query.empty() && std::isspace(static_cast<unsigned char>(query.front())))
    query.erase(query.begin());
  const size_t sp = query.find_first_of(" \t\r\n");
  if (sp != std::string::npos)
    query.resize(sp);
  for (char& c : query)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return query;
}

WikiHelpTarget ResolveCommandBarWikiTarget(const char* cmdBuf, const AppCommandState& cmd) {
  std::string primary;
  if (cmd.active != AppCommandState::Kind::None) {
    const char* name = AppCommandState::KindName(cmd.active);
    if (name != nullptr && name[0] != '\0') {
      for (const char* p = name; *p; ++p)
        primary.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(*p))));
    }
  }

  if (primary.empty()) {
    // CadUi.cpp publishes fuzzy highlight through QueryCommandBarFuzzyPrimary().
    primary = QueryCommandBarFuzzyPrimary();
  }

  if (primary.empty()) {
    primary = FirstCommandTokenLower(cmdBuf);
    if (!primary.empty()) {
      const std::vector<CommandSuggestion> sug = FuzzyCommandSuggestions(primary, 1);
      if (!sug.empty())
        primary = sug.front().name;
    }
  }

  if (primary.empty())
    return {};
  return WikiHelpLookupCommand(primary);
}

}  // namespace

void RequestContextualWikiWindow(AppCommandState& cmd, const char* cmdBuf) {
  WikiHelpTarget target;
  const bool     cmdInputActive = CadUiIsCommandInputActive();
  const bool     typingCommand =
      cmdInputActive && cmdBuf != nullptr && cmdBuf[0] != '\0';
  const bool     activeCommand = cmd.active != AppCommandState::Kind::None;
  const bool     fuzzyMatch    = !QueryCommandBarFuzzyPrimary().empty();

  // Command bar wins when the user is typing a match, an active command is running, or fuzzy
  // autocomplete has a primary — otherwise a hovered ribbon/status control gets contextual help.
  const bool preferCommand = activeCommand || typingCommand || fuzzyMatch;

  if (preferCommand)
    target = ResolveCommandBarWikiTarget(cmdBuf, cmd);

  if (target.pageSlug.empty()) {
    std::string hoverTip;
    if (WikiHelpConsumeUiHover(&hoverTip))
      target = WikiHelpTargetFromUiTooltip(hoverTip);
  }

  if (target.pageSlug.empty())
    target.pageSlug = "Home";

  cmd.wikiCurrentPage = target.pageSlug;
  cmd.wikiScrollToCommand.clear();
  cmd.showWikiWindow  = true;
}

void DrawWikiWindow(AppCommandState& cmd) {
  if (!cmd.showWikiWindow)
    return;

  WikiBundle& bundle = CachedWikiBundle();

  if (cmd.wikiCurrentPage.empty())
    cmd.wikiCurrentPage = "Home";

  const WikiPanelColors panel = WikiPanelColorsForTheme();

  ImGui::SetNextWindowSize(ImVec2(1180.f, 820.f), ImGuiCond_FirstUseEver);
  bool open = cmd.showWikiWindow;

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.f, 12.f));
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.f);
  ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.f);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, panel.winBg);
  ImGui::PushStyleColor(ImGuiCol_Text, panel.text);
  ImGui::PushStyleColor(ImGuiCol_TextDisabled, panel.textMuted);
  ImGui::PushStyleColor(ImGuiCol_Separator, panel.border);
  ImGui::PushStyleColor(ImGuiCol_Header, Lerp(panel.sidebarBg, WikiAccent(), 0.18f));
  ImGui::PushStyleColor(ImGuiCol_HeaderHovered, Lerp(panel.sidebarBg, WikiAccent(), 0.30f));
  ImGui::PushStyleColor(ImGuiCol_HeaderActive, Lerp(panel.sidebarBg, WikiAccent(), 0.42f));
  ImGui::PushStyleColor(ImGuiCol_TableHeaderBg, panel.tableHeaderBg);
  ImGui::PushStyleColor(ImGuiCol_TableRowBg, panel.tableRowBg);
  ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, Lerp(panel.tableRowBg, WikiAccent(), 0.06f));
  ImGui::PushStyleColor(ImGuiCol_TableBorderStrong, panel.border);
  ImGui::PushStyleColor(ImGuiCol_TableBorderLight, Lerp(panel.border, panel.winBg, 0.35f));

  PushProductDialogAccent();
  if (!ImGui::Begin("GoSurvey User Manual", &open, ImGuiWindowFlags_None)) {
    cmd.showWikiWindow = open;
    ImGui::End();
    PopProductDialogAccent();
    ImGui::PopStyleColor(12);
    ImGui::PopStyleVar(3);
    return;
  }
  PaintProductDialogAccentFrame();
  cmd.showWikiWindow = open;

  ImFont* const wikiFont = FontReg::Wiki();
  if (wikiFont != nullptr)
    ImGui::PushFont(wikiFont);

  {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2  a  = ImGui::GetWindowPos();
    const ImVec2  b(a.x + ImGui::GetWindowSize().x, a.y + ImGui::GetWindowSize().y);
    DrawWikiWindowGradient(dl, a, b, ImGui::GetStyle().WindowRounding);
  }

  if (!bundle.ok) {
    ImGui::TextWrapped("%s", bundle.fallbackMessage.c_str());
    if (StyledButton("Close", ImVec2(0, 0), /*primary=*/false))
      cmd.showWikiWindow = false;
    if (wikiFont != nullptr)
      ImGui::PopFont();
    ImGui::End();
    PopProductDialogAccent();
    ImGui::PopStyleColor(12);
    ImGui::PopStyleVar(3);
    return;
  }

  const float sidebarW = 240.f;
  ImGui::PushStyleColor(ImGuiCol_ChildBg, panel.sidebarBg);
  ImGui::PushStyleColor(ImGuiCol_Border, panel.border);
  if (ImGui::BeginChild("##wikiSidebar", ImVec2(sidebarW, 0.f), true)) {
    ImGui::TextColored(panel.textMuted, "Contents");
    ImGui::Separator();
    for (const WikiNavEntry& entry : bundle.nav) {
      if (entry.isSection) {
        ImGui::Spacing();
        ImGui::TextColored(panel.textMuted, "%s", entry.label.c_str());
        continue;
      }

      const bool selected = (entry.slug == cmd.wikiCurrentPage);
      if (ImGui::Selectable(entry.label.c_str(), selected))
        cmd.wikiCurrentPage = entry.slug;
    }
  }
  ImGui::EndChild();
  ImGui::PopStyleColor(2);

  ImGui::SameLine();

  ImGui::PushStyleColor(ImGuiCol_ChildBg, panel.bodyBg);
  ImGui::PushStyleColor(ImGuiCol_Border, panel.border);
  if (ImGui::BeginChild("##wikiBody", ImVec2(0.f, 0.f), true)) {
    const WikiPage* page = FindWikiPage(bundle, cmd.wikiCurrentPage);
    if (page == nullptr) {
      ImGui::TextWrapped("Page not found: %s", cmd.wikiCurrentPage.c_str());
    } else {
      bool               scrollApplied = false;
      MarkdownImGuiHooks hooks;
      hooks.onLink = [&cmd](std::string_view href) { return HandleWikiLink(href, cmd); };
      hooks.resolveImage = ResolveWikiImage;
      hooks.headingFont  = FontReg::WikiHeading();
      hooks.monoFont     = FontReg::WikiMono();
      hooks.scrollToCommandPrimary = cmd.wikiScrollToCommand;
      hooks.scrollApplied          = &scrollApplied;
      DrawMarkdownImGui(page->markdown, &hooks);
      if (scrollApplied)
        cmd.wikiScrollToCommand.clear();
    }
  }
  ImGui::EndChild();
  ImGui::PopStyleColor(2);

  if (wikiFont != nullptr)
    ImGui::PopFont();

  ImGui::End();
  PopProductDialogAccent();
  ImGui::PopStyleColor(12);
  ImGui::PopStyleVar(3);
}
