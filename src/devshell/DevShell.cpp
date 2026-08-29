#include "DevShell.hpp"

#ifdef GOSURVEY_DEVELOPER_SHELL

#include "CadUiChrome.hpp"
#include "CadCommands.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <imgui.h>
#include <imgui_te_context.h>
#include <imgui_te_coroutine.h>
#include <imgui_te_engine.h>
#include <imgui_te_ui.h>

#include <GLFW/glfw3.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

extern ImGuiTestCoroutineInterface* Coroutine_ImplStdThread_GetInterface();

namespace {

constexpr int kMaxLog = 4000;

struct LogLine {
  std::string channel;
  std::string message;
};

std::mutex              g_logMu;
std::vector<LogLine>    g_log;
ImGuiTestEngine*        g_engine = nullptr;
std::string             g_cliTest;
GLFWwindow*             g_cliWindow = nullptr;
bool                    g_cliQueued = false;
bool                    g_cliDone = false;
int                     g_cliExit = 1;
int                     g_cliWait = 0;
bool                    g_logDrawPass = false;

void PushLog(std::string_view channel, std::string_view message)
{
  std::lock_guard<std::mutex> lock(g_logMu);
  if (static_cast<int>(g_log.size()) >= kMaxLog)
    g_log.erase(g_log.begin(), g_log.begin() + static_cast<int>(g_log.size()) - kMaxLog + 1);
  g_log.push_back({std::string(channel), std::string(message)});
}

void ColorU32(const char* label, ImU32* c)
{
  ImVec4 v = ImGui::ColorConvertU32ToFloat4(*c);
  if (ImGui::ColorEdit4(label, &v.x, ImGuiColorEditFlags_AlphaBar))
    *c = ImGui::ColorConvertFloat4ToU32(v);
}

void AppendHexU32(std::string* out, const char* field, ImU32 c)
{
  const unsigned r = (c >> IM_COL32_R_SHIFT) & 0xFFu;
  const unsigned g = (c >> IM_COL32_G_SHIFT) & 0xFFu;
  const unsigned b = (c >> IM_COL32_B_SHIFT) & 0xFFu;
  const unsigned a = (c >> IM_COL32_A_SHIFT) & 0xFFu;
  char line[128];
  if (a == 255)
    std::snprintf(line, sizeof(line), "  g_chrome.%s = HexU32(0x%02X%02X%02X);\n", field, r, g, b);
  else
    std::snprintf(line, sizeof(line), "  g_chrome.%s = HexU32(0x%02X%02X%02X, %u);\n", field, r, g, b, a);
  *out += line;
}

void AppendFloat(std::string* out, const char* field, float v)
{
  char line[128];
  std::snprintf(line, sizeof(line), "  g_chrome.%s = %.3ff;\n", field, static_cast<double>(v));
  *out += line;
}

void AppendStyleVec2(std::string* out, const char* field, ImVec2 v)
{
  char line[160];
  std::snprintf(line, sizeof(line), "  style.%s = ImVec2(%.3ff, %.3ff);\n", field,
                static_cast<double>(v.x), static_cast<double>(v.y));
  *out += line;
}

void AppendStyleFloat(std::string* out, const char* field, float v)
{
  char line[128];
  std::snprintf(line, sizeof(line), "  style.%s = %.3ff;\n", field, static_cast<double>(v));
  *out += line;
}

void CopyChromeSnippetToClipboard()
{
  const UiChrome& ch = CadUiChrome();
  const ImGuiStyle& st = ImGui::GetStyle();
  std::string dump;
  dump += "// Developer Shell chrome dump — paste this in chat to apply in ApplyCad*Theme\n";
  AppendStyleVec2(&dump, "WindowPadding", st.WindowPadding);
  AppendStyleVec2(&dump, "FramePadding", st.FramePadding);
  AppendStyleVec2(&dump, "ItemSpacing", st.ItemSpacing);
  AppendStyleVec2(&dump, "ItemInnerSpacing", st.ItemInnerSpacing);
  AppendStyleVec2(&dump, "CellPadding", st.CellPadding);
  AppendStyleFloat(&dump, "IndentSpacing", st.IndentSpacing);
  AppendStyleFloat(&dump, "ScrollbarSize", st.ScrollbarSize);
  AppendStyleFloat(&dump, "GrabMinSize", st.GrabMinSize);
  AppendStyleFloat(&dump, "WindowRounding", st.WindowRounding);
  AppendStyleFloat(&dump, "ChildRounding", st.ChildRounding);
  AppendStyleFloat(&dump, "FrameRounding", st.FrameRounding);
  AppendStyleFloat(&dump, "PopupRounding", st.PopupRounding);
  AppendStyleFloat(&dump, "ScrollbarRounding", st.ScrollbarRounding);
  AppendStyleFloat(&dump, "GrabRounding", st.GrabRounding);
  AppendStyleFloat(&dump, "TabRounding", st.TabRounding);
  AppendStyleFloat(&dump, "WindowBorderSize", st.WindowBorderSize);
  AppendStyleFloat(&dump, "ChildBorderSize", st.ChildBorderSize);
  AppendStyleFloat(&dump, "PopupBorderSize", st.PopupBorderSize);
  AppendStyleFloat(&dump, "FrameBorderSize", st.FrameBorderSize);
  AppendStyleFloat(&dump, "TabBorderSize", st.TabBorderSize);
  dump += "  // ImGui colors\n";
  for (int i = 0; i < ImGuiCol_COUNT; ++i)
  {
    const char* name = ImGui::GetStyleColorName(i);
    if (!name)
      continue;
    const ImVec4 c = st.Colors[i];
    char line[192];
    std::snprintf(line, sizeof(line),
                  "  colors[ImGuiCol_%s] = ImVec4(%.3ff, %.3ff, %.3ff, %.3ff);\n",
                  name, static_cast<double>(c.x), static_cast<double>(c.y),
                  static_cast<double>(c.z), static_cast<double>(c.w));
    dump += line;
  }
  AppendHexU32(&dump, "bandFace", ch.bandFace);
  AppendHexU32(&dump, "bandHilite", ch.bandHilite);
  AppendHexU32(&dump, "bandShadow", ch.bandShadow);
  AppendHexU32(&dump, "bandSunken", ch.bandSunken);
  AppendHexU32(&dump, "bandRaised", ch.bandRaised);
  AppendHexU32(&dump, "statusBarFace", ch.statusBarFace);
  AppendHexU32(&dump, "statusStripFace", ch.statusStripFace);
  AppendHexU32(&dump, "panelFill", ch.panelFill);
  AppendHexU32(&dump, "propValueBg", ch.propValueBg);
  AppendHexU32(&dump, "headerFaceL", ch.headerFaceL);
  AppendHexU32(&dump, "headerFaceR", ch.headerFaceR);
  AppendHexU32(&dump, "headerHoverL", ch.headerHoverL);
  AppendHexU32(&dump, "headerHoverR", ch.headerHoverR);
  AppendHexU32(&dump, "headerText", ch.headerText);
  AppendHexU32(&dump, "headerEdgeTop", ch.headerEdgeTop);
  AppendHexU32(&dump, "headerEdgeBot", ch.headerEdgeBot);
  AppendHexU32(&dump, "headerGlyphBg", ch.headerGlyphBg);
  AppendHexU32(&dump, "headerGlyphEdge", ch.headerGlyphEdge);
  AppendHexU32(&dump, "headerGlyph", ch.headerGlyph);
  dump += ch.headerBoxGlyph ? "  g_chrome.headerBoxGlyph = true;\n" : "  g_chrome.headerBoxGlyph = false;\n";
  AppendHexU32(&dump, "popupFace", ch.popupFace);
  AppendHexU32(&dump, "popupBorder", ch.popupBorder);
  AppendHexU32(&dump, "plateHilite", ch.plateHilite);
  AppendHexU32(&dump, "plateShadow", ch.plateShadow);
  AppendHexU32(&dump, "windowShadow", ch.windowShadow);
  dump += ch.axisBadges ? "  g_chrome.axisBadges = true;\n" : "  g_chrome.axisBadges = false;\n";
  AppendHexU32(&dump, "axisX", ch.axisX);
  AppendHexU32(&dump, "axisY", ch.axisY);
  AppendHexU32(&dump, "axisZ", ch.axisZ);
  AppendHexU32(&dump, "axisText", ch.axisText);
  AppendHexU32(&dump, "ribbonPanelRule", ch.ribbonPanelRule);
  AppendHexU32(&dump, "ribbonPanelTitle", ch.ribbonPanelTitle);
  AppendHexU32(&dump, "ribbonTabOn", ch.ribbonTabOn);
  AppendHexU32(&dump, "ribbonTabOnHovered", ch.ribbonTabOnHovered);
  AppendHexU32(&dump, "ribbonTabOnActive", ch.ribbonTabOnActive);
  AppendHexU32(&dump, "ribbonTabOnText", ch.ribbonTabOnText);
  AppendHexU32(&dump, "ribbonCtxTab", ch.ribbonCtxTab);
  AppendHexU32(&dump, "ribbonCtxTabDim", ch.ribbonCtxTabDim);
  AppendHexU32(&dump, "ribbonCtxTabHovered", ch.ribbonCtxTabHovered);
  AppendHexU32(&dump, "ribbonCtxTabActive", ch.ribbonCtxTabActive);
  AppendHexU32(&dump, "ribbonCtxTabText", ch.ribbonCtxTabText);
  AppendFloat(&dump, "ribbonTabPadY", ch.ribbonTabPadY);
  AppendFloat(&dump, "ribbonTabStripGapY", ch.ribbonTabStripGapY);
  AppendFloat(&dump, "ribbonBottomGutter", ch.ribbonBottomGutter);
  AppendFloat(&dump, "ribbonTitleH", ch.ribbonTitleH);
  AppendFloat(&dump, "ribbonBodyFontScale", ch.ribbonBodyFontScale);
  ImGui::SetClipboardText(dump.c_str());
  DevShell_Log("chrome", "copied dump to clipboard — paste in chat to persist");
}

void AppendLogLines(std::string* out, const std::vector<std::string>& lines)
{
  assert(out != nullptr);
  if (lines.empty())
  {
    *out += "(empty)\n";
    return;
  }
  for (const std::string& line : lines)
  {
    *out += line;
    *out += '\n';
  }
}

void CopyCommandLogToClipboard(const std::vector<std::string>& commandLog)
{
  std::string dump = "// Developer Shell command log\n";
  AppendLogLines(&dump, commandLog);
  ImGui::SetClipboardText(dump.c_str());
  DevShell_Log("log", "copied command log to clipboard");
}

void CopyActivityLogToClipboard()
{
  std::vector<LogLine> snap;
  {
    std::lock_guard<std::mutex> lock(g_logMu);
    snap = g_log;
  }
  std::string dump = "// Developer Shell activity log\n";
  if (snap.empty())
  {
    dump += "(empty)\n";
  }
  else
  {
    for (const LogLine& L : snap)
    {
      dump += L.channel;
      dump += " | ";
      dump += L.message;
      dump += '\n';
    }
  }
  ImGui::SetClipboardText(dump.c_str());
  DevShell_Log("log", "copied activity log to clipboard");
}

void StyleCol(const char* label, ImGuiCol col)
{
  ImGui::ColorEdit4(label, &ImGui::GetStyle().Colors[col].x, ImGuiColorEditFlags_AlphaBar);
}

void DrawChromeStyle(ImGuiStyle& st)
{
  if (!ImGui::CollapsingHeader("ImGui style", ImGuiTreeNodeFlags_DefaultOpen))
    return;
  ImGui::SliderFloat2("WindowPadding", &st.WindowPadding.x, 0.f, 24.f);
  ImGui::SliderFloat2("FramePadding", &st.FramePadding.x, 0.f, 16.f);
  ImGui::SliderFloat2("ItemSpacing", &st.ItemSpacing.x, 0.f, 16.f);
  ImGui::SliderFloat2("ItemInnerSpacing", &st.ItemInnerSpacing.x, 0.f, 16.f);
  ImGui::SliderFloat2("CellPadding", &st.CellPadding.x, 0.f, 16.f);
  ImGui::SliderFloat("IndentSpacing", &st.IndentSpacing, 0.f, 32.f);
  ImGui::SliderFloat("ScrollbarSize", &st.ScrollbarSize, 8.f, 24.f);
  ImGui::SliderFloat("GrabMinSize", &st.GrabMinSize, 4.f, 24.f);
  ImGui::SliderFloat("WindowRounding", &st.WindowRounding, 0.f, 12.f);
  ImGui::SliderFloat("ChildRounding", &st.ChildRounding, 0.f, 12.f);
  ImGui::SliderFloat("FrameRounding", &st.FrameRounding, 0.f, 12.f);
  ImGui::SliderFloat("PopupRounding", &st.PopupRounding, 0.f, 12.f);
  ImGui::SliderFloat("ScrollbarRounding", &st.ScrollbarRounding, 0.f, 12.f);
  ImGui::SliderFloat("GrabRounding", &st.GrabRounding, 0.f, 12.f);
  ImGui::SliderFloat("TabRounding", &st.TabRounding, 0.f, 12.f);
  ImGui::SliderFloat("WindowBorderSize", &st.WindowBorderSize, 0.f, 2.f);
  ImGui::SliderFloat("ChildBorderSize", &st.ChildBorderSize, 0.f, 2.f);
  ImGui::SliderFloat("PopupBorderSize", &st.PopupBorderSize, 0.f, 2.f);
  ImGui::SliderFloat("FrameBorderSize", &st.FrameBorderSize, 0.f, 2.f);
  ImGui::SliderFloat("TabBorderSize", &st.TabBorderSize, 0.f, 2.f);
}

void DrawChromeRibbon(UiChrome& ch)
{
  if (!ImGui::CollapsingHeader("Ribbon", ImGuiTreeNodeFlags_DefaultOpen))
    return;
  ImGui::TextDisabled("Band + button bevel (hover/press)");
  ColorU32("bandFace", &ch.bandFace);
  ColorU32("bandHilite", &ch.bandHilite);
  ColorU32("bandShadow", &ch.bandShadow);
  ColorU32("bandSunken", &ch.bandSunken);
  ColorU32("bandRaised", &ch.bandRaised);
  ColorU32("ribbonPanelRule", &ch.ribbonPanelRule);
  ColorU32("ribbonPanelTitle", &ch.ribbonPanelTitle);
  ImGui::TextDisabled("Active Home/Insert/… tab (also Model/Layout toggles)");
  ColorU32("ribbonTabOn", &ch.ribbonTabOn);
  ColorU32("ribbonTabOnHovered", &ch.ribbonTabOnHovered);
  ColorU32("ribbonTabOnActive", &ch.ribbonTabOnActive);
  ColorU32("ribbonTabOnText", &ch.ribbonTabOnText);
  ImGui::TextDisabled("Contextual tabs (surface / point / feature line)");
  ColorU32("ribbonCtxTab", &ch.ribbonCtxTab);
  ColorU32("ribbonCtxTabDim", &ch.ribbonCtxTabDim);
  ColorU32("ribbonCtxTabHovered", &ch.ribbonCtxTabHovered);
  ColorU32("ribbonCtxTabActive", &ch.ribbonCtxTabActive);
  ColorU32("ribbonCtxTabText", &ch.ribbonCtxTabText);
  ImGui::SliderFloat("ribbonTabPadY", &ch.ribbonTabPadY, 0.f, 12.f);
  ImGui::SliderFloat("ribbonTabStripGapY", &ch.ribbonTabStripGapY, 0.f, 16.f);
  ImGui::SliderFloat("ribbonBottomGutter", &ch.ribbonBottomGutter, 0.f, 24.f);
  ImGui::SliderFloat("ribbonTitleH", &ch.ribbonTitleH, 12.f, 32.f);
  ImGui::SliderFloat("ribbonBodyFontScale", &ch.ribbonBodyFontScale, 0.55f, 1.1f);
}

void DrawChromeShell(UiChrome& ch)
{
  if (!ImGui::CollapsingHeader("Panels / headers / popups"))
    return;
  ColorU32("statusBarFace", &ch.statusBarFace);
  ColorU32("statusStripFace", &ch.statusStripFace);
  ColorU32("panelFill", &ch.panelFill);
  ColorU32("propValueBg", &ch.propValueBg);
  ColorU32("headerFaceL", &ch.headerFaceL);
  ColorU32("headerFaceR", &ch.headerFaceR);
  ColorU32("headerHoverL", &ch.headerHoverL);
  ColorU32("headerHoverR", &ch.headerHoverR);
  ColorU32("headerText", &ch.headerText);
  ColorU32("headerEdgeTop", &ch.headerEdgeTop);
  ColorU32("headerEdgeBot", &ch.headerEdgeBot);
  ColorU32("headerGlyphBg", &ch.headerGlyphBg);
  ColorU32("headerGlyphEdge", &ch.headerGlyphEdge);
  ColorU32("headerGlyph", &ch.headerGlyph);
  ImGui::Checkbox("headerBoxGlyph", &ch.headerBoxGlyph);
  ColorU32("popupFace", &ch.popupFace);
  ColorU32("popupBorder", &ch.popupBorder);
  ColorU32("plateHilite", &ch.plateHilite);
  ColorU32("plateShadow", &ch.plateShadow);
  ColorU32("windowShadow", &ch.windowShadow);
  ImGui::Checkbox("axisBadges", &ch.axisBadges);
  ColorU32("axisX", &ch.axisX);
  ColorU32("axisY", &ch.axisY);
  ColorU32("axisZ", &ch.axisZ);
  ColorU32("axisText", &ch.axisText);
}

void DrawChromeImGuiColors()
{
  if (!ImGui::CollapsingHeader("ImGui colors"))
    return;
  StyleCol("Text", ImGuiCol_Text);
  StyleCol("TextDisabled", ImGuiCol_TextDisabled);
  StyleCol("WindowBg", ImGuiCol_WindowBg);
  StyleCol("ChildBg", ImGuiCol_ChildBg);
  StyleCol("PopupBg", ImGuiCol_PopupBg);
  StyleCol("Border", ImGuiCol_Border);
  StyleCol("FrameBg", ImGuiCol_FrameBg);
  StyleCol("FrameBgHovered", ImGuiCol_FrameBgHovered);
  StyleCol("FrameBgActive", ImGuiCol_FrameBgActive);
  StyleCol("TitleBg", ImGuiCol_TitleBg);
  StyleCol("TitleBgActive", ImGuiCol_TitleBgActive);
  StyleCol("MenuBarBg", ImGuiCol_MenuBarBg);
  StyleCol("Button", ImGuiCol_Button);
  StyleCol("ButtonHovered", ImGuiCol_ButtonHovered);
  StyleCol("ButtonActive", ImGuiCol_ButtonActive);
  StyleCol("Header", ImGuiCol_Header);
  StyleCol("HeaderHovered", ImGuiCol_HeaderHovered);
  StyleCol("Tab", ImGuiCol_Tab);
  StyleCol("TabHovered", ImGuiCol_TabHovered);
  StyleCol("TabActive", ImGuiCol_TabActive);
  StyleCol("Separator", ImGuiCol_Separator);
  StyleCol("CheckMark", ImGuiCol_CheckMark);
  StyleCol("SliderGrab", ImGuiCol_SliderGrab);
  StyleCol("ScrollbarGrab", ImGuiCol_ScrollbarGrab);
  StyleCol("TableHeaderBg", ImGuiCol_TableHeaderBg);
  StyleCol("DockingEmptyBg", ImGuiCol_DockingEmptyBg);
}

void DrawChromeTuner()
{
  UiChrome& ch = CadUiChrome();
  ImGuiStyle& st = ImGui::GetStyle();
  ImGui::TextUnformatted("Live UiChrome + ImGuiStyle (this frame).");
  if (ImGui::Button("Copy snippet for chat"))
    CopyChromeSnippetToClipboard();
  ImGui::SameLine();
  ImGui::TextDisabled("paste in chat to persist into ApplyCad*Theme");
  DrawChromeStyle(st);
  DrawChromeRibbon(ch);
  DrawChromeShell(ch);
  DrawChromeImGuiColors();
}

} // namespace

void DevShell_Log(std::string_view channel, std::string_view message)
{
  PushLog(channel, message);
}

void DevShell_Logf(const char* channel, const char* fmt, ...)
{
  char buf[512];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  PushLog(channel, buf);
}

void DevShell_Create(ImGuiTestEngine** outEngine)
{
  IM_ASSERT(outEngine != nullptr);
  ImGuiTestEngine* engine = ImGuiTestEngine_CreateContext();
  ImGuiTestEngineIO& io = ImGuiTestEngine_GetIO(engine);
  io.CoroutineFuncs = Coroutine_ImplStdThread_GetInterface();
  io.ConfigVerboseLevel = ImGuiTestVerboseLevel_Info;
  io.ConfigVerboseLevelOnError = ImGuiTestVerboseLevel_Debug;
  io.ConfigRunSpeed = ImGuiTestRunSpeed_Fast;
  io.ConfigLogToTTY = false;
  io.ConfigLogToFunc = [](ImGuiTestEngine*, ImGuiTestContext*, ImGuiTestVerboseLevel, const char* message, void*) {
    if (message)
      DevShell_Log("te", message);
  };
  ImGuiTestEngine_Start(engine, ImGui::GetCurrentContext());
  g_engine = engine;
  *outEngine = engine;
}

void DevShell_RegisterTests(ImGuiTestEngine* engine, AppCommandState* cmd)
{
  IM_ASSERT(engine != nullptr);
  IM_ASSERT(cmd != nullptr);
  DevShell_RegisterUiTests(engine, cmd);
}

void DevShell_PostSwap(ImGuiTestEngine* engine)
{
  if (!engine)
    return;
  ImGuiTestEngine_PostSwap(engine);
  if (g_cliQueued && !g_cliDone)
  {
    ++g_cliWait;
    if (g_cliWait > 8 && ImGuiTestEngine_IsTestQueueEmpty(engine))
    {
      int tested = 0;
      int success = 0;
      ImGuiTestEngine_GetResult(engine, tested, success);
      g_cliDone = true;
      g_cliExit = (tested > 0 && tested == success) ? 0 : 1;
      if (g_cliWindow)
        glfwSetWindowShouldClose(g_cliWindow, GLFW_TRUE);
    }
  }
}

void DevShell_Stop(ImGuiTestEngine* engine)
{
  if (!engine)
    return;
  ImGuiTestEngine_Stop(engine);
}

void DevShell_DestroyContext(ImGuiTestEngine* engine)
{
  if (!engine)
    return;
  ImGuiTestEngine_DestroyContext(engine);
  g_engine = nullptr;
}

void DevShell_Draw(AppCommandState& cmd, const std::vector<std::string>& commandLog)
{
  (void)cmd;
  ImGui::SetNextWindowSize(ImVec2(420.f, 520.f), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Developer Shell"))
  {
    ImGui::End();
    return;
  }

  if (ImGui::BeginTabBar("DevShellTabs"))
  {
    if (ImGui::BeginTabItem("Chrome"))
    {
      DrawChromeTuner();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Log"))
    {
      ImGui::Checkbox("one-line draw-pass (off by default)", &g_logDrawPass);
      if (g_logDrawPass)
        DevShell_Log("draw", "pass");
      ImGui::Separator();
      ImGui::TextUnformatted("Command log");
      ImGui::SameLine();
      if (ImGui::Button("Copy command log"))
        CopyCommandLogToClipboard(commandLog);
      ImGui::BeginChild("cmdlog", ImVec2(0, 140.f), true);
      for (const std::string& line : commandLog)
        ImGui::TextUnformatted(line.c_str());
      if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
        ImGui::SetScrollHereY(1.f);
      ImGui::EndChild();
      ImGui::TextUnformatted("Activity");
      ImGui::SameLine();
      if (ImGui::Button("Copy activity log"))
        CopyActivityLogToClipboard();
      ImGui::BeginChild("actlog", ImVec2(0, 0), true);
      std::vector<LogLine> snap;
      {
        std::lock_guard<std::mutex> lock(g_logMu);
        snap = g_log;
      }
      for (const LogLine& L : snap)
        ImGui::Text("%s | %s", L.channel.c_str(), L.message.c_str());
      if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
        ImGui::SetScrollHereY(1.f);
      ImGui::EndChild();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Tests"))
    {
      if (g_engine)
        ImGuiTestEngine_ShowTestEngineWindows(g_engine, nullptr);
      else
        ImGui::TextUnformatted("Test Engine not started.");
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }
  ImGui::End();
}

bool DevShell_ParseRunFlag(std::string* outTestName)
{
  IM_ASSERT(outTestName != nullptr);
#ifdef _WIN32
  int argc = 0;
  LPWSTR* argvW = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
  if (!argvW)
    return false;
  bool found = false;
  for (int i = 1; i < argc; ++i)
  {
    char utf8[1024];
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, argvW[i], -1, utf8, static_cast<int>(sizeof(utf8)), nullptr, nullptr);
    if (n <= 0)
      continue;
    if (std::strcmp(utf8, "--devshell-run") == 0 && i + 1 < argc)
    {
      char name[256];
      ::WideCharToMultiByte(CP_UTF8, 0, argvW[i + 1], -1, name, static_cast<int>(sizeof(name)), nullptr, nullptr);
      *outTestName = name;
      found = true;
      break;
    }
  }
  ::LocalFree(argvW);
  return found;
#else
  (void)outTestName;
  return false;
#endif
}

void DevShell_BeginCliRun(ImGuiTestEngine* engine, GLFWwindow* window, const char* testName)
{
  IM_ASSERT(engine != nullptr);
  IM_ASSERT(window != nullptr);
  IM_ASSERT(testName != nullptr);
  g_cliWindow = window;
  g_cliTest = testName;
  g_cliQueued = true;
  g_cliDone = false;
  ImGuiTestEngine_QueueTests(engine, ImGuiTestGroup_Tests, testName);
}

bool DevShell_CliRunFinished(int* outExitCode)
{
  if (!g_cliQueued || !g_cliDone)
    return false;
  if (outExitCode)
    *outExitCode = g_cliExit;
  return true;
}

#endif
