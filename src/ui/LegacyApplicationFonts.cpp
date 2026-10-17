#include "CadCommands.hpp"
#include "FontRegistry.hpp"
#include "AppPaths.hpp"
#include <imgui.h>
#include <filesystem>

namespace {

ImFont* LoadBundledUiFont(const char* fileName, const float sizePx, ImFontConfig* cfg) {
  namespace fs = std::filesystem;
  const fs::path path = ResolveBundledAssetPath(fs::path("resources") / "fonts" / fileName);
  if (path.empty())
    return nullptr;
  return ImGui::GetIO().Fonts->AddFontFromFileTTF(path.string().c_str(), sizePx, cfg);
}

}  // namespace

bool LoadApplicationFont() {
  ImGuiIO& io = ImGui::GetIO();
  // Segoe UI across the app (issue #850 follow-on request); falls back to the classic
  // Arial/Tahoma/Calibri chain if Segoe UI isn't installed. The LSA Editor's command editor and
  // output panel keep their own monospace font via explicit PushFont and are unaffected by this.
  const char* candidates[] = {
      "C:/Windows/Fonts/segoeui.ttf",
      "C:/Windows/Fonts/arial.ttf",
      "C:/Windows/Fonts/tahoma.ttf",
      "C:/Windows/Fonts/Tahoma.ttf",
      "C:/Windows/Fonts/calibri.ttf",
  };
  ImFontConfig cfg;
  cfg.OversampleH = 2;
  cfg.OversampleV = 1;
  cfg.PixelSnapH  = true;  // crisp small classic text
  ImFont* loaded = nullptr;
  for (const char* path : candidates) {
    ImFont* f = io.Fonts->AddFontFromFileTTF(path, 16.0f, &cfg);
    if (f) {
      io.FontDefault = f;
      FontReg::SetDefault(f);  // fallback for unresolved CAD fonts
      loaded = f;
      break;
    }
  }
  if (loaded == nullptr)
    return false;

  ImFontConfig tsCfg;
  tsCfg.OversampleH = 3;
  tsCfg.OversampleV = 2;
  tsCfg.PixelSnapH = true;
  const char* tsCandidates[] = {
      "C:/Windows/Fonts/segoeui.ttf",
      "C:/Windows/Fonts/arial.ttf",
      "C:/Windows/Fonts/segoeuisl.ttf",
      "C:/Windows/Fonts/calibri.ttf",
  };
  ImFont* tsFont = nullptr;
  for (const char* path : tsCandidates) {
    tsFont = io.Fonts->AddFontFromFileTTF(path, 16.0f, &tsCfg);
    if (tsFont)
      break;
  }
  FontReg::SetToolspace(tsFont != nullptr ? tsFont : loaded);

  // What's New billboard: Segoe UI (issue #850 follow-on) — Arial/Calibri fallback.
  ImFontConfig bbCfg;
  bbCfg.OversampleH = 3;
  bbCfg.OversampleV = 2;
  bbCfg.PixelSnapH = true;
  const char* bbCandidates[] = {
      "C:/Windows/Fonts/segoeui.ttf",
      "C:/Windows/Fonts/arial.ttf",
      "C:/Windows/Fonts/segoeuisl.ttf",
      "C:/Windows/Fonts/calibri.ttf",
  };
  ImFont* bbFont = nullptr;
  for (const char* path : bbCandidates) {
    bbFont = io.Fonts->AddFontFromFileTTF(path, 20.0f, &bbCfg);
    if (bbFont)
      break;
  }
  FontReg::SetBillboard(bbFont != nullptr ? bbFont : (tsFont != nullptr ? tsFont : loaded));

  // In-app wiki reader — same IBM Plex family as the GoSurvey website (site/index.html).
  ImFontConfig wikiCfg;
  wikiCfg.OversampleH = 3;
  wikiCfg.OversampleV = 2;
  wikiCfg.PixelSnapH  = true;
  ImFont* wikiBody = LoadBundledUiFont("IBMPlexSans-Regular.ttf", 16.0f, &wikiCfg);
  FontReg::SetWiki(wikiBody != nullptr ? wikiBody : loaded);
  ImFont* wikiHeading = LoadBundledUiFont("IBMPlexSansCondensed-SemiBold.ttf", 17.0f, &wikiCfg);
  FontReg::SetWikiHeading(wikiHeading != nullptr ? wikiHeading : FontReg::Wiki());
  ImFont* wikiMono = LoadBundledUiFont("IBMPlexMono-Regular.ttf", 15.0f, &wikiCfg);
  FontReg::SetWikiMono(wikiMono != nullptr ? wikiMono : FontReg::Wiki());
  return true;
}

bool LoadApplicationFontWithTuning(const FontTuning& t) {
  ImGuiIO& io = ImGui::GetIO();
  io.Fonts->Clear();

  ImFontConfig cfg;
  cfg.OversampleH = 2;
  cfg.OversampleV = 1;
  cfg.PixelSnapH  = true;
  ImFont* loaded = io.Fonts->AddFontFromFileTTF(t.defaultFont.path.c_str(), t.defaultFont.sizePx, &cfg);
  if (loaded == nullptr)
    return false;
  io.FontDefault = loaded;
  FontReg::SetDefault(loaded);

  ImFontConfig tsCfg;
  tsCfg.OversampleH = 3;
  tsCfg.OversampleV = 2;
  tsCfg.PixelSnapH = true;
  ImFont* tsFont = io.Fonts->AddFontFromFileTTF(t.toolspace.path.c_str(), t.toolspace.sizePx, &tsCfg);
  FontReg::SetToolspace(tsFont != nullptr ? tsFont : loaded);

  ImFontConfig bbCfg;
  bbCfg.OversampleH = 3;
  bbCfg.OversampleV = 2;
  bbCfg.PixelSnapH = true;
  ImFont* bbFont = io.Fonts->AddFontFromFileTTF(t.billboard.path.c_str(), t.billboard.sizePx, &bbCfg);
  FontReg::SetBillboard(bbFont != nullptr ? bbFont : (tsFont != nullptr ? tsFont : loaded));

  ImFontConfig wikiCfg;
  wikiCfg.OversampleH = 3;
  wikiCfg.OversampleV = 2;
  wikiCfg.PixelSnapH  = true;
  ImFont* wikiBody = LoadBundledUiFont(t.wikiBody.path.c_str(), t.wikiBody.sizePx, &wikiCfg);
  FontReg::SetWiki(wikiBody != nullptr ? wikiBody : loaded);
  ImFont* wikiHeading = LoadBundledUiFont(t.wikiHeading.path.c_str(), t.wikiHeading.sizePx, &wikiCfg);
  FontReg::SetWikiHeading(wikiHeading != nullptr ? wikiHeading : FontReg::Wiki());
  ImFont* wikiMono = LoadBundledUiFont(t.wikiMono.path.c_str(), t.wikiMono.sizePx, &wikiCfg);
  FontReg::SetWikiMono(wikiMono != nullptr ? wikiMono : FontReg::Wiki());
  return true;
}

