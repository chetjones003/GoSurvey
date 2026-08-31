// REQ-308 / D-2026-08-30-a/b/c — the Start screen (drawingTabs[0]).
//
// Three columns: left = GoSurvey/version + Open/New + the project website link; middle = the
// recent-drawings grid/list with thumbnails; right = the "Connect" column (Welcome + email when
// signed in, an offline notice + Sign In when not). The launch sign-in gate (REQ-091 amended)
// means "signed out" here is only reachable when there was no network at launch.
//
// This file also owns the small glue the recent list needs that must NOT live in the pure
// RecentDrawings module: the %APPDATA% path resolution, the wall clock, the GL texture cache for
// thumbnails, and the thumbnail-capture hand-off to ViewportRenderer.

#include "CadUi.hpp"

#include "AppIcon.hpp"          // UserDataDirectory, LoadIconTextureRgba
#include "RecentDrawings.hpp"
#include "ThumbnailCache.hpp"
#include "ViewportRenderer.hpp"
#include "Version.hpp"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace {

constexpr const char* kWebsiteUrl  = "https://chetjones003.github.io/GoSurvey";
constexpr const char* kFeedbackUrl = "https://github.com/chetjones003/GoSurvey/issues";
constexpr int         kThumbLongSidePx = 512;   // capture resolution
constexpr std::size_t kThumbCacheMax   = 64;    // *.bmp files kept on disk (D-2026-08-30-c)

std::filesystem::path RecentJsonPath() {
  const auto dir = UserDataDirectory();
  return dir.empty() ? std::filesystem::path("gosurvey-recent.json") : dir / "gosurvey-recent.json";
}

std::filesystem::path ThumbnailDir() {
  const auto dir = UserDataDirectory();
  return (dir.empty() ? std::filesystem::path(".") : dir) / "thumbnails";
}

std::int64_t NowUnix() {
  return static_cast<std::int64_t>(std::time(nullptr));
}

std::string AbsPathUtf8(const std::string& p) {
  std::error_code ec;
  const auto abs = std::filesystem::absolute(std::filesystem::u8path(p), ec);
  return ec ? p : abs.u8string();
}

// --- Thumbnail texture cache (path -> GL texture), lazily loaded, invalidated on re-capture. -----
struct ThumbTex {
  unsigned int tex = 0;
  int          w = 0;
  int          h = 0;
  bool         tried = false;  // attempted a load; tex==0 && tried => no thumbnail, use the icon
};
std::unordered_map<std::string, ThumbTex> g_thumbTex;

const ThumbTex& ThumbTexFor(const std::string& drawingPath, const std::string& thumbFile) {
  ThumbTex& slot = g_thumbTex[drawingPath];
  if (slot.tried)
    return slot;
  slot.tried = true;
  if (thumbFile.empty())
    return slot;
  const auto full = ThumbnailDir() / thumbFile;
  std::error_code ec;
  if (!std::filesystem::exists(full, ec))
    return slot;
  slot.tex = LoadIconTextureRgba(full, &slot.w, &slot.h);
  return slot;
}

void InvalidateThumbTex(const std::string& drawingPath) {
  auto it = g_thumbTex.find(drawingPath);
  if (it != g_thumbTex.end()) {
    DestroyIconTexture(it->second.tex);
    g_thumbTex.erase(it);
  }
}

std::string DisplayNameFromEmail(const std::string& email) {
  const auto at = email.find('@');
  std::string name = (at == std::string::npos) ? email : email.substr(0, at);
  if (name.empty())
    return email;
  name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
  return name;
}

void OpenUrl(const char* url) {
#if defined(_WIN32)
  ::ShellExecuteA(nullptr, "open", url, nullptr, nullptr, SW_SHOWNORMAL);
#else
  (void)url;
#endif
}

// ---- Shared palette for the Start screen chrome. Derived from the active theme where possible so
//      it tracks Dark/Light, with one steel-blue accent that matches the nanoCAD-style chrome. ----
ImVec4 Lerp(const ImVec4& a, const ImVec4& b, float t) {
  return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t,
                a.w + (b.w - a.w) * t);
}
ImVec4 Accent()        { return ImVec4(0.26f, 0.56f, 0.86f, 1.f); }
ImVec4 AccentHi()      { return ImVec4(0.34f, 0.64f, 0.95f, 1.f); }
ImVec4 AccentLo()      { return ImVec4(0.20f, 0.45f, 0.72f, 1.f); }
bool   IsDark()        { return ImGui::GetStyleColorVec4(ImGuiCol_WindowBg).x < 0.35f; }
ImVec4 CardBg()        { return IsDark() ? ImVec4(0.17f, 0.18f, 0.20f, 1.f) : ImVec4(0.97f, 0.97f, 0.98f, 1.f); }
ImVec4 CardBgHover()   { return IsDark() ? ImVec4(0.21f, 0.23f, 0.26f, 1.f) : ImVec4(1.f, 1.f, 1.f, 1.f); }
ImVec4 CardBorder()    { return IsDark() ? ImVec4(0.30f, 0.32f, 0.36f, 1.f) : ImVec4(0.80f, 0.82f, 0.85f, 1.f); }
ImVec4 HeroBg()        { return IsDark() ? ImVec4(0.13f, 0.14f, 0.16f, 1.f) : ImVec4(0.93f, 0.94f, 0.96f, 1.f); }

// A rounded shadow cast straight down from a rect — cheap "lift" for cards and the hero band.
void SoftShadow(ImDrawList* dl, ImVec2 a, ImVec2 b, float rounding, float drop) {
  const ImU32 s1 = ImGui::GetColorU32(ImVec4(0.f, 0.f, 0.f, 0.28f));
  const ImU32 s0 = ImGui::GetColorU32(ImVec4(0.f, 0.f, 0.f, 0.f));
  dl->AddRectFilledMultiColor(ImVec2(a.x + 3.f, b.y), ImVec2(b.x + 3.f, b.y + drop), s1, s1, s0, s0);
  dl->AddRectFilled(ImVec2(a.x + 3.f, a.y + 4.f), ImVec2(b.x + 3.f, b.y + 3.f),
                    ImGui::GetColorU32(ImVec4(0.f, 0.f, 0.f, 0.18f)), rounding);
}

// Filled accent / ghost button with rounded corners. Returns click.
bool StyledButton(const char* label, bool primary, const ImVec2& size) {
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.f);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.f, 9.f));
  if (primary) {
    ImGui::PushStyleColor(ImGuiCol_Button, Accent());
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentHi());
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentLo());
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 1.f));
  } else {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.f, 0.f, 0.f, 0.f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Lerp(CardBg(), Accent(), 0.18f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, Lerp(CardBg(), Accent(), 0.30f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_Text));
  }
  const bool r = ImGui::Button(label, size);
  if (!primary) {
    const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
    ImGui::GetWindowDrawList()->AddRect(mn, mx, ImGui::GetColorU32(CardBorder()), 6.f, 0, 1.f);
  }
  ImGui::PopStyleColor(4);
  ImGui::PopStyleVar(2);
  return r;
}

// Section heading: larger text with a short accent underline.
void SectionHeading(const char* text) {
  const ImVec2 p = ImGui::GetCursorScreenPos();
  ImGui::SetWindowFontScale(1.25f);
  ImGui::TextUnformatted(text);
  ImGui::SetWindowFontScale(1.f);
  const float w = ImGui::GetItemRectSize().x;
  ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x, ImGui::GetItemRectMax().y + 3.f),
                                     ImVec2(p.x + std::max(w, 26.f), ImGui::GetItemRectMax().y + 3.f),
                                     ImGui::GetColorU32(Accent()), 2.5f);
  ImGui::Dummy(ImVec2(0.f, 8.f));
}

// Crisp vector "GS" app badge for the hero — resolution-independent, so it stays sharp at any DPI
// (the shipped title-bar icon is ~32px and blurs when scaled up here).
void DrawGsBadge(ImDrawList* dl, ImVec2 c, float sz) {
  const ImVec2 a(c.x - sz * 0.5f, c.y - sz * 0.5f);
  const ImVec2 b(c.x + sz * 0.5f, c.y + sz * 0.5f);
  const float rnd = sz * 0.24f;
  dl->AddRectFilled(ImVec2(a.x + 2.f, a.y + 3.f), ImVec2(b.x + 3.f, b.y + 4.f),
                    ImGui::GetColorU32(ImVec4(0.f, 0.f, 0.f, 0.35f)), rnd);
  dl->AddRectFilled(a, b, ImGui::GetColorU32(Accent()), rnd);
  // Smooth top-down sheen, clipped to the rounded body — no hard midline.
  dl->PushClipRect(a, b, true);
  dl->AddRectFilledMultiColor(a, b, ImGui::GetColorU32(ImVec4(1.f, 1.f, 1.f, 0.16f)),
                              ImGui::GetColorU32(ImVec4(1.f, 1.f, 1.f, 0.16f)),
                              ImGui::GetColorU32(ImVec4(1.f, 1.f, 1.f, 0.f)),
                              ImGui::GetColorU32(ImVec4(1.f, 1.f, 1.f, 0.f)));
  dl->PopClipRect();
  dl->AddRect(a, b, ImGui::GetColorU32(ImVec4(1.f, 1.f, 1.f, 0.30f)), rnd, 0, 1.5f);
  const float fs = sz / ImGui::GetFontSize() * 0.52f;
  ImGui::SetWindowFontScale(fs);
  const ImVec2 t = ImGui::CalcTextSize("GS");
  dl->AddText(ImVec2(c.x - t.x * 0.5f, c.y - t.y * 0.5f), IM_COL32_WHITE, "GS");
  ImGui::SetWindowFontScale(1.f);
}

// A flat "DWG" file glyph drawn when a recent drawing has no captured thumbnail (REQ-308).
void DrawDwgPlaceholder(ImDrawList* dl, ImVec2 a, ImVec2 b) {
  const ImU32 top = ImGui::GetColorU32(Lerp(HeroBg(), Accent(), 0.10f));
  const ImU32 bot = ImGui::GetColorU32(HeroBg());
  const ImU32 sheet = ImGui::GetColorU32(ImVec4(0.86f, 0.88f, 0.92f, 1.f));
  const ImU32 fold  = ImGui::GetColorU32(ImVec4(0.62f, 0.66f, 0.72f, 1.f));
  const ImU32 label = ImGui::GetColorU32(AccentHi());
  dl->AddRectFilledMultiColor(a, b, top, top, bot, bot);
  const float w = b.x - a.x, h = b.y - a.y;
  const ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
  const float pw = std::min(w, h) * 0.26f;
  const float ph = pw * 1.3f;
  const ImVec2 p0(c.x - pw, c.y - ph);
  const ImVec2 p1(c.x + pw, c.y + ph);
  const float foldSz = pw * 0.55f;
  dl->AddRectFilled(p0, p1, sheet, 2.f);
  dl->AddTriangleFilled(ImVec2(p1.x - foldSz, p0.y), ImVec2(p1.x, p0.y), ImVec2(p1.x, p0.y + foldSz), fold);
  const char* t = "DWG";
  const ImVec2 ts = ImGui::CalcTextSize(t);
  dl->AddText(ImVec2(c.x - ts.x * 0.5f, p1.y - ts.y - 3.f), label, t);
}

// End-truncate `s` with a trailing ellipsis until it fits `maxW` pixels at the current font.
// Steps back over UTF-8 continuation bytes so a multi-byte glyph is never split.
std::string EllipsizeToWidth(const std::string& s, float maxW) {
  if (s.empty() || ImGui::CalcTextSize(s.c_str()).x <= maxW)
    return s;
  const char* ell = "\xE2\x80\xA6";  // …
  std::string cut = s;
  while (!cut.empty()) {
    do {
      cut.pop_back();
    } while (!cut.empty() && (static_cast<unsigned char>(cut.back()) & 0xC0) == 0x80);
    if (ImGui::CalcTextSize((cut + ell).c_str()).x <= maxW)
      break;
  }
  return cut + ell;
}

std::string RelativeTimeText(std::int64_t unix) {
  if (unix <= 0)
    return "";
  const std::time_t t = static_cast<std::time_t>(unix);
  std::tm tmv{};
#if defined(_WIN32)
  localtime_s(&tmv, &t);
#else
  localtime_r(&t, &tmv);
#endif
  char buf[64];
  std::strftime(buf, sizeof(buf), "%a, %b %d %Y", &tmv);
  return buf;
}

// ------------------------------------------------------------------------------------------------

void DrawLeftColumn(AppCommandState& cmd, std::vector<std::string>& log) {
  ImGui::Dummy(ImVec2(0.f, 4.f));
  SectionHeading("Get Started");

  if (StyledButton("Open a Drawing", true, ImVec2(-FLT_MIN, 0.f)))
    OpenDrawingInNewTab(cmd, log, nullptr);
  ImGui::Dummy(ImVec2(0.f, 6.f));
  if (StyledButton("New Drawing", false, ImVec2(-FLT_MIN, 0.f)))
    NewDrawingInTab(cmd, log);

  ImGui::Dummy(ImVec2(0.f, 22.f));
  SectionHeading("Resources");
  if (ImGui::InvisibleButton("##web", ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetTextLineHeight())))
    OpenUrl(kWebsiteUrl);
  const bool hov = ImGui::IsItemHovered();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 lp = ImGui::GetItemRectMin();
  dl->AddText(lp, ImGui::GetColorU32(hov ? AccentHi() : Accent()), "Visit The Website");
  if (hov) {
    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    dl->AddLine(ImVec2(lp.x, ImGui::GetItemRectMax().y - 1.f),
                ImVec2(lp.x + ImGui::CalcTextSize("Visit The Website").x, ImGui::GetItemRectMax().y - 1.f),
                ImGui::GetColorU32(AccentHi()), 1.f);
  }
}

void DrawRecentColumn(AppCommandState& cmd, std::vector<std::string>& log) {
  static bool gridView = true;
  static int  sortMode = 0;  // 0 = last opened, 1 = name
  static char search[128] = {};

  ImGui::Dummy(ImVec2(0.f, 4.f));
  SectionHeading("Recent Drawings");

  // Toolbar row: segmented view toggle · sort · search.
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.f);
  {
    auto seg = [&](const char* lbl, bool on) {
      ImGui::PushStyleColor(ImGuiCol_Button, on ? Accent() : ImVec4(0, 0, 0, 0));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, on ? AccentHi() : Lerp(CardBg(), Accent(), 0.2f));
      ImGui::PushStyleColor(ImGuiCol_Text, on ? ImVec4(1, 1, 1, 1) : ImGui::GetStyleColorVec4(ImGuiCol_Text));
      const bool c = ImGui::Button(lbl);
      ImGui::PopStyleColor(3);
      return c;
    };
    if (seg("Grid", gridView)) gridView = true;
    ImGui::SameLine(0.f, 2.f);
    if (seg("List", !gridView)) gridView = false;
  }
  ImGui::SameLine(0.f, 16.f);
  ImGui::AlignTextToFramePadding();
  ImGui::TextDisabled("Sort");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(140.f);
  ImGui::Combo("##sort", &sortMode, "Last Opened\0Name\0");
  ImGui::SameLine(0.f, 16.f);
  ImGui::SetNextItemWidth(-FLT_MIN);
  ImGui::InputTextWithHint("##recentsearch", "Search drawings", search, sizeof(search));
  ImGui::PopStyleVar();

  std::vector<recent::Entry> entries = recent::Load(RecentJsonPath());

  // Filter by search text (case-insensitive substring on the display name).
  std::string needle = search;
  std::transform(needle.begin(), needle.end(), needle.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (!needle.empty()) {
    entries.erase(std::remove_if(entries.begin(), entries.end(),
                                 [&](const recent::Entry& e) {
                                   std::string n = e.name;
                                   std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) {
                                     return static_cast<char>(std::tolower(c));
                                   });
                                   return n.find(needle) == std::string::npos;
                                 }),
                  entries.end());
  }
  if (sortMode == 1)
    std::sort(entries.begin(), entries.end(),
              [](const recent::Entry& a, const recent::Entry& b) { return a.name < b.name; });
  else
    std::sort(entries.begin(), entries.end(), [](const recent::Entry& a, const recent::Entry& b) {
      return a.lastOpenedUnix > b.lastOpenedUnix;
    });

  ImGui::Dummy(ImVec2(0.f, 8.f));
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
  ImGui::BeginChild("##recentlist", ImVec2(0.f, 0.f), false);

  if (entries.empty()) {
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    ImGui::Dummy(ImVec2(0.f, avail.y * 0.34f));
    const char* l1 = "No recent drawings yet";
    const char* l2 = "Open or create a drawing and it will show up here.";
    ImGui::SetCursorPosX((ImGui::GetWindowSize().x - ImGui::CalcTextSize(l1).x) * 0.5f);
    ImGui::SetWindowFontScale(1.2f);
    ImGui::TextDisabled("%s", l1);
    ImGui::SetWindowFontScale(1.f);
    ImGui::SetCursorPosX((ImGui::GetWindowSize().x - ImGui::CalcTextSize(l2).x) * 0.5f);
    ImGui::TextDisabled("%s", l2);
    ImGui::EndChild();
    ImGui::PopStyleColor();
    return;
  }

  const char* clickedPath = nullptr;
  ImDrawList* dl = ImGui::GetWindowDrawList();

  if (gridView) {
    const float tileW = 208.f;
    const float thumbH = 132.f;
    const float capH = 52.f;
    const float gap = 18.f;
    const float availW = ImGui::GetContentRegionAvail().x;
    const int   perRow = std::max(1, static_cast<int>((availW + gap) / (tileW + gap)));
    int col = 0;
    for (const recent::Entry& e : entries) {
      ImGui::PushID(e.path.c_str());
      const ImVec2 p0 = ImGui::GetCursorScreenPos();
      const ImVec2 p1(p0.x + tileW, p0.y + thumbH + capH);
      if (ImGui::InvisibleButton("card", ImVec2(tileW, thumbH + capH)))
        clickedPath = e.path.c_str();
      const bool hov = ImGui::IsItemHovered();

      SoftShadow(dl, p0, p1, 8.f, hov ? 12.f : 7.f);
      dl->AddRectFilled(p0, p1, ImGui::GetColorU32(hov ? CardBgHover() : CardBg()), 8.f);

      // Thumbnail, clipped to the card's rounded top.
      const ImVec2 t0 = p0, t1(p1.x, p0.y + thumbH);
      dl->PushClipRect(t0, t1, true);
      const ThumbTex& tt = ThumbTexFor(e.path, e.thumb);
      if (tt.tex)
        dl->AddImage(static_cast<ImTextureID>(static_cast<std::intptr_t>(tt.tex)), t0, t1);
      else
        DrawDwgPlaceholder(dl, t0, t1);
      dl->PopClipRect();
      dl->AddLine(ImVec2(t0.x, t1.y), ImVec2(t1.x, t1.y), ImGui::GetColorU32(CardBorder()), 1.f);

      const float textW = tileW - 24.f;
      dl->AddText(ImVec2(p0.x + 12.f, t1.y + 8.f), ImGui::GetColorU32(ImGuiCol_Text),
                  EllipsizeToWidth(e.name, textW).c_str());
      dl->AddText(ImVec2(p0.x + 12.f, t1.y + 27.f), ImGui::GetColorU32(ImGuiCol_TextDisabled),
                  EllipsizeToWidth(RelativeTimeText(e.lastOpenedUnix), textW).c_str());

      dl->AddRect(p0, p1, ImGui::GetColorU32(hov ? Accent() : CardBorder()), 8.f, 0, hov ? 2.f : 1.f);
      if (hov)
        ImGui::SetTooltip("%s\n%s", e.name.c_str(), e.path.c_str());
      ImGui::PopID();
      if (++col < perRow)
        ImGui::SameLine(0.f, gap);
      else {
        col = 0;
        ImGui::Dummy(ImVec2(0.f, gap));
      }
    }
  } else {
    const float rowH = 60.f;
    for (const recent::Entry& e : entries) {
      ImGui::PushID(e.path.c_str());
      const ImVec2 p0 = ImGui::GetCursorScreenPos();
      const float rowW = ImGui::GetContentRegionAvail().x;
      const ImVec2 p1(p0.x + rowW, p0.y + rowH);
      if (ImGui::InvisibleButton("row", ImVec2(rowW, rowH)))
        clickedPath = e.path.c_str();
      const bool hov = ImGui::IsItemHovered();
      if (hov)
        dl->AddRectFilled(p0, p1, ImGui::GetColorU32(CardBgHover()), 6.f);

      const ImVec2 th0(p0.x + 8.f, p0.y + 7.f), th1(p0.x + 8.f + (rowH - 14.f) * 1.5f, p1.y - 7.f);
      dl->PushClipRect(th0, th1, true);
      const ThumbTex& tt = ThumbTexFor(e.path, e.thumb);
      if (tt.tex)
        dl->AddImage(static_cast<ImTextureID>(static_cast<std::intptr_t>(tt.tex)), th0, th1);
      else
        DrawDwgPlaceholder(dl, th0, th1);
      dl->PopClipRect();
      dl->AddRect(th0, th1, ImGui::GetColorU32(CardBorder()), 3.f);

      const float textW = std::max(40.f, p1.x - (th1.x + 12.f) - 12.f);
      dl->AddText(ImVec2(th1.x + 12.f, p0.y + 11.f), ImGui::GetColorU32(ImGuiCol_Text),
                  EllipsizeToWidth(e.name, textW).c_str());
      dl->AddText(ImVec2(th1.x + 12.f, p0.y + 31.f), ImGui::GetColorU32(ImGuiCol_TextDisabled),
                  EllipsizeToWidth(RelativeTimeText(e.lastOpenedUnix), textW).c_str());
      dl->AddLine(ImVec2(p0.x, p1.y), ImVec2(p1.x, p1.y), ImGui::GetColorU32(CardBorder()), 1.f);
      if (hov)
        ImGui::SetTooltip("%s\n%s", e.name.c_str(), e.path.c_str());
      ImGui::PopID();
    }
  }

  ImGui::EndChild();
  ImGui::PopStyleColor();

  if (clickedPath) {
    // Copy — OpenDrawingInNewTab mutates the recent store, invalidating `entries`.
    const std::string path = clickedPath;
    if (std::filesystem::exists(std::filesystem::u8path(path))) {
      OpenDrawingInNewTab(cmd, log, path.c_str());
    } else {
      log.push_back("Drawing not found: " + path);
      RemoveRecentDrawing(path);
      InvalidateThumbTex(path);
    }
  }
}

void DrawConnectColumn(AppCommandState& cmd) {
  ImGui::Dummy(ImVec2(0.f, 4.f));
  SectionHeading("Connect");

  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 c0 = ImGui::GetCursorScreenPos();
  const float cardW = ImGui::GetContentRegionAvail().x;

  if (cmd.authSignedIn) {
    const std::string name = DisplayNameFromEmail(cmd.authEmail);
    const float cardH = 108.f;
    const ImVec2 c1(c0.x + cardW, c0.y + cardH);
    SoftShadow(dl, c0, c1, 8.f, 8.f);
    dl->AddRectFilled(c0, c1, ImGui::GetColorU32(CardBg()), 8.f);
    dl->AddRect(c0, c1, ImGui::GetColorU32(CardBorder()), 8.f);

    // Avatar: accent disc with the first initial.
    const float r = 24.f;
    const ImVec2 ac(c0.x + 20.f + r, c0.y + cardH * 0.5f);
    dl->AddCircleFilled(ac, r, ImGui::GetColorU32(Accent()), 32);
    const std::string initial(1, name.empty() ? 'G' : static_cast<char>(std::toupper(
                                                          static_cast<unsigned char>(name[0]))));
    ImGui::SetWindowFontScale(1.4f);
    const ImVec2 is = ImGui::CalcTextSize(initial.c_str());
    dl->AddText(ImVec2(ac.x - is.x * 0.5f, ac.y - is.y * 0.5f), IM_COL32_WHITE, initial.c_str());
    ImGui::SetWindowFontScale(1.f);

    const float tx = c0.x + 20.f + r * 2.f + 16.f;
    ImGui::SetWindowFontScale(1.15f);
    dl->AddText(ImVec2(tx, c0.y + 30.f), ImGui::GetColorU32(ImGuiCol_Text),
                (std::string("Welcome ") + (name.empty() ? "back" : name) + "!").c_str());
    ImGui::SetWindowFontScale(1.f);
    if (!cmd.authEmail.empty())
      dl->AddText(ImVec2(tx, c0.y + 54.f), ImGui::GetColorU32(ImGuiCol_TextDisabled),
                  cmd.authEmail.c_str());
    ImGui::Dummy(ImVec2(cardW, cardH));
  } else {
    const float cardH = 150.f;
    const ImVec2 c1(c0.x + cardW, c0.y + cardH);
    SoftShadow(dl, c0, c1, 8.f, 8.f);
    dl->AddRectFilled(c0, c1, ImGui::GetColorU32(CardBg()), 8.f);
    dl->AddRect(c0, c1, ImGui::GetColorU32(CardBorder()), 8.f);
    ImGui::Dummy(ImVec2(0.f, 14.f));
    ImGui::Indent(16.f);
    ImGui::PushTextWrapPos(0.f);
    ImGui::TextUnformatted("You're not signed in.");
    ImGui::TextDisabled("GoSurvey couldn't reach the sign-in service at launch. If you're offline, "
                        "sign in once you're back on a connection.");
    ImGui::PopTextWrapPos();
    ImGui::Dummy(ImVec2(0.f, 8.f));
    ImGui::BeginDisabled(cmd.authBusy);
    if (StyledButton(cmd.authInteractiveBusy ? "Waiting for browser..." : "Sign In", true,
                     ImVec2(c0.x + cardW - 16.f - ImGui::GetCursorScreenPos().x, 0.f)))
      cmd.authSignInRequested = true;
    ImGui::EndDisabled();
    ImGui::Unindent(16.f);
    if (!cmd.authError.empty()) {
      ImGui::Dummy(ImVec2(0.f, 6.f));
      ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.4f, 1.f), "%s", cmd.authError.c_str());
    }
  }

  // --- Help improve the product ---
  ImGui::Dummy(ImVec2(0.f, 22.f));
  SectionHeading("Help Me Improve This Product");
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  ImGui::PushTextWrapPos(0.f);  // wrap at the column's right edge
  ImGui::TextUnformatted("Found a bug or have an idea? Open an issue on GitHub \xE2\x80\x94 "
                         "every report helps.");
  ImGui::PopTextWrapPos();
  ImGui::PopStyleColor();
  ImGui::Dummy(ImVec2(0.f, 8.f));
  if (StyledButton("Send Feedback", false, ImVec2(0.f, 0.f)))
    OpenUrl(kFeedbackUrl);
}

}  // namespace

void DrawStartScreen(AppCommandState& cmd, std::vector<std::string>& log) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  // Paint from the content-area top-left (below the drawing tab bar), not the window origin.
  const ImVec2 winMin = ImGui::GetCursorScreenPos();
  const ImVec2 avail = ImGui::GetContentRegionAvail();
  const ImVec2 winMax(winMin.x + avail.x, winMin.y + avail.y);

  // --- Full-bleed background: a faint vertical gradient with an accent glow up top. ---
  const ImVec4 bg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
  dl->AddRectFilledMultiColor(winMin, winMax, ImGui::GetColorU32(Lerp(bg, Accent(), 0.05f)),
                              ImGui::GetColorU32(Lerp(bg, Accent(), 0.05f)),
                              ImGui::GetColorU32(Lerp(bg, ImVec4(0, 0, 0, 1), IsDark() ? 0.18f : 0.f)),
                              ImGui::GetColorU32(Lerp(bg, ImVec4(0, 0, 0, 1), IsDark() ? 0.18f : 0.f)));

  const float hpad = 28.f;
  const float heroH = 128.f;

  // --- Hero band ---
  const ImVec2 h0(winMin.x, winMin.y);
  const ImVec2 h1(winMax.x, winMin.y + heroH);
  dl->AddRectFilledMultiColor(h0, h1, ImGui::GetColorU32(Lerp(HeroBg(), Accent(), 0.10f)),
                              ImGui::GetColorU32(HeroBg()), ImGui::GetColorU32(HeroBg()),
                              ImGui::GetColorU32(Lerp(HeroBg(), Accent(), 0.10f)));
  dl->AddRectFilled(ImVec2(h0.x, h1.y - 3.f), h1, ImGui::GetColorU32(Accent()));
  dl->AddRectFilled(h0, ImVec2(h0.x + 5.f, h1.y), ImGui::GetColorU32(Accent()));

  float textX = winMin.x + hpad;
  {
    const float badge = 60.f;
    DrawGsBadge(dl, ImVec2(textX + badge * 0.5f, winMin.y + heroH * 0.5f), badge);
    textX += badge + 22.f;
  }

  ImGui::SetWindowFontScale(2.6f);
  dl->AddText(ImVec2(textX, winMin.y + 26.f), ImGui::GetColorU32(ImGuiCol_Text), "GoSurvey");
  const float wordW = ImGui::CalcTextSize("GoSurvey").x;
  ImGui::SetWindowFontScale(1.f);

  // Version pill.
  {
    const std::string ver = std::string("v") + GOSURVEY_VERSION_FULL;
    const ImVec2 ts = ImGui::CalcTextSize(ver.c_str());
    const ImVec2 pillMin(textX + wordW + 14.f, winMin.y + 34.f);
    const ImVec2 pillMax(pillMin.x + ts.x + 16.f, pillMin.y + ts.y + 8.f);
    dl->AddRectFilled(pillMin, pillMax, ImGui::GetColorU32(Lerp(HeroBg(), Accent(), 0.35f)), 10.f);
    dl->AddText(ImVec2(pillMin.x + 8.f, pillMin.y + 4.f), ImGui::GetColorU32(ImGuiCol_Text), ver.c_str());
  }
  dl->AddText(ImVec2(textX + 2.f, winMin.y + 78.f), ImGui::GetColorU32(ImGuiCol_TextDisabled),
              "Land surveying \xC2\xB7 civil drafting \xC2\xB7 CAD");

  // --- Body: three columns below the hero. ---
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() + heroH + 18.f);
  ImGui::Indent(hpad);

  const float total = ImGui::GetContentRegionAvail().x - hpad;
  const float leftW = 250.f;
  const float rightW = 320.f;
  const float midW = std::max(220.f, total - leftW - rightW - 72.f);

  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
  ImGui::BeginChild("##startLeft", ImVec2(leftW, 0.f), false);
  DrawLeftColumn(cmd, log);
  ImGui::EndChild();

  ImGui::SameLine(0.f, 36.f);
  ImGui::BeginChild("##startMid", ImVec2(midW, 0.f), false);
  DrawRecentColumn(cmd, log);
  ImGui::EndChild();

  ImGui::SameLine(0.f, 36.f);
  ImGui::BeginChild("##startRight", ImVec2(rightW, 0.f), false);
  DrawConnectColumn(cmd);
  ImGui::EndChild();
  ImGui::PopStyleColor();

  ImGui::Unindent(hpad);
}

void RecordRecentDrawing(AppCommandState& cmd, const std::string& absDrawingPath) {
  if (absDrawingPath.empty())
    return;
  const std::string abs = AbsPathUtf8(absDrawingPath);
  recent::Note(RecentJsonPath(), abs, "", NowUnix());
  InvalidateThumbTex(abs);
  cmd.pendingThumbnailPath   = abs;
  cmd.pendingThumbnailTabIdx = cmd.activeDrawingIdx;
}

void RemoveRecentDrawing(const std::string& absDrawingPath) {
  recent::Remove(RecentJsonPath(), AbsPathUtf8(absDrawingPath));
}

void ServicePendingThumbnail(AppCommandState& cmd, const ViewportRenderer& renderer) {
  if (cmd.pendingThumbnailTabIdx < 0 || cmd.pendingThumbnailPath.empty())
    return;
  if (cmd.pendingThumbnailTabIdx != cmd.activeDrawingIdx)
    return;  // wait until the drawing being pictured is the one on screen / rendered

  const std::string path = cmd.pendingThumbnailPath;
  cmd.pendingThumbnailTabIdx = -1;
  cmd.pendingThumbnailPath.clear();

  const auto dir = ThumbnailDir();
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  const std::string fn = thumbs::ThumbFileName(path);
  if (renderer.CaptureThumbnailBmp((dir / fn).u8string().c_str(), kThumbLongSidePx)) {
    thumbs::EvictThumbnails(dir, kThumbCacheMax);
    recent::Note(RecentJsonPath(), path, fn, NowUnix());
    InvalidateThumbTex(path);
  }
}
