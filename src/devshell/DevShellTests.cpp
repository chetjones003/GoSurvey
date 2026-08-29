#include "DevShell.hpp"

#ifdef GOSURVEY_DEVELOPER_SHELL

#include "CadCommands.hpp"

#include <imgui.h>
#include <imgui_te_context.h>
#include <imgui_te_engine.h>

#include <cassert>
#include <cstring>

namespace {

AppCommandState* s_cmd = nullptr;

bool RefWindow(ImGuiTestContext* ctx, const char* path)
{
  assert(ctx != nullptr);
  assert(path != nullptr);
  const ImGuiTestItemInfo info = ctx->WindowInfo(path);
  const bool ok = info.Window != nullptr;
  IM_CHECK_NO_RET(ok);
  if (!ok)
    return false;
  ctx->SetRef(info.Window);
  return true;
}

bool ClickHomeTab(ImGuiTestContext* ctx)
{
  assert(ctx != nullptr);
  if (!RefWindow(ctx, "//GoSurveyHost/RibbonStrip"))
    return false;
  ctx->ItemClick("Home");
  ctx->Yield(2);
  return true;
}

bool CancelToIdle(ImGuiTestContext* ctx)
{
  assert(ctx != nullptr);
  assert(s_cmd != nullptr);
  ctx->KeyPress(ImGuiKey_Escape);
  ctx->Yield();
  ctx->KeyPress(ImGuiKey_Escape);
  ctx->Yield();
  const bool idle = s_cmd->active == AppCommandState::Kind::None;
  IM_CHECK_NO_RET(idle);
  return idle;
}

bool RefCommandBar(ImGuiTestContext* ctx)
{
  assert(ctx != nullptr);
  const ImGuiTestItemInfo floating = ctx->WindowInfo("//##CommandBarFloat", ImGuiTestOpFlags_NoError);
  if (floating.Window)
  {
    ctx->SetRef(floating.Window);
    return true;
  }
  const ImGuiTestItemInfo docked = ctx->WindowInfo("//Command line", ImGuiTestOpFlags_NoError);
  if (docked.Window)
  {
    ctx->SetRef(docked.Window);
    return true;
  }
  IM_CHECK_NO_RET(false);
  return false;
}

bool ClickRibbonTool(ImGuiTestContext* ctx, const char* itemId, AppCommandState::Kind expect)
{
  assert(ctx != nullptr);
  assert(itemId != nullptr);
  assert(s_cmd != nullptr);
  if (!CancelToIdle(ctx))
    return false;
  if (!ClickHomeTab(ctx))
    return false;
  // Draw tools sit in RibbonSecDraw (child of RibbonToolsLeft), not on RibbonStrip itself.
  if (!RefWindow(ctx, "//GoSurveyHost/RibbonStrip/RibbonToolsLeft/RibbonSecDraw"))
    return false;
  ctx->ItemClick(itemId);
  ctx->Yield();
  const bool started = s_cmd->active == expect;
  IM_CHECK_NO_RET(started);
  if (!started)
    return false;
  return CancelToIdle(ctx);
}

} // namespace

void DevShell_RegisterUiTests(ImGuiTestEngine* engine, AppCommandState* cmd)
{
  assert(engine != nullptr);
  assert(cmd != nullptr);
  s_cmd = cmd;

  ImGuiTest* windows = IM_REGISTER_TEST(engine, "gosurvey", "windows-present");
  windows->TestFunc = [](ImGuiTestContext* ctx) {
    IM_CHECK(ctx->WindowInfo("//GoSurveyHost").Window != nullptr);
    IM_CHECK(ctx->WindowInfo("//Developer Shell").Window != nullptr);
    IM_CHECK(ctx->WindowInfo("//Properties").Window != nullptr);
  };

  ImGuiTest* smoke = IM_REGISTER_TEST(engine, "gosurvey", "req161-smoke");
  smoke->TestFunc = [](ImGuiTestContext* ctx) {
    DevShell_Log("ui", "tool ##RibbonLine");
    IM_CHECK(ClickRibbonTool(ctx, "##RibbonLine", AppCommandState::Kind::Line));
    DevShell_Log("viewport", "pick 0.0000,0.0000");
    DevShell_Log("command", "LINE");
  };

  ImGuiTest* circle = IM_REGISTER_TEST(engine, "gosurvey", "ribbon-circle");
  circle->TestFunc = [](ImGuiTestContext* ctx) {
    IM_CHECK(ClickRibbonTool(ctx, "##RibbonCircle", AppCommandState::Kind::Circle));
  };

  ImGuiTest* pline = IM_REGISTER_TEST(engine, "gosurvey", "ribbon-pline");
  pline->TestFunc = [](ImGuiTestContext* ctx) {
    IM_CHECK(ClickRibbonTool(ctx, "##RibbonPLine", AppCommandState::Kind::Polyline));
  };

  ImGuiTest* arc = IM_REGISTER_TEST(engine, "gosurvey", "ribbon-arc");
  arc->TestFunc = [](ImGuiTestContext* ctx) {
    IM_CHECK(ClickRibbonTool(ctx, "##RibbonArc", AppCommandState::Kind::Arc));
  };

  ImGuiTest* typed = IM_REGISTER_TEST(engine, "gosurvey", "command-line-line");
  typed->TestFunc = [](ImGuiTestContext* ctx) {
    IM_CHECK(CancelToIdle(ctx));
    IM_CHECK(RefCommandBar(ctx));
    ctx->ItemClick("GoSurveyCmdPanel/##CommandLineInput");
    ctx->KeyCharsReplaceEnter("LINE");
    ctx->Yield();
    IM_CHECK_EQ(s_cmd->active, AppCommandState::Kind::Line);
    IM_CHECK(CancelToIdle(ctx));
  };

  ImGuiTest* viewTab = IM_REGISTER_TEST(engine, "gosurvey", "ribbon-view-extents");
  viewTab->TestFunc = [](ImGuiTestContext* ctx) {
    IM_CHECK(CancelToIdle(ctx));
    IM_CHECK(RefWindow(ctx, "//GoSurveyHost/RibbonStrip"));
    ctx->ItemClick("View");
    ctx->Yield(2);
    IM_CHECK(RefWindow(ctx, "//GoSurveyHost/RibbonStrip/RibbonToolsLeft/RibbonSecView"));
    IM_CHECK(ctx->ItemExists("##RibbonZExtents"));
    IM_CHECK(RefWindow(ctx, "//GoSurveyHost/RibbonStrip"));
    ctx->ItemClick("Home");
  };

  ImGuiTest* chrome = IM_REGISTER_TEST(engine, "gosurvey", "chrome-copy");
  chrome->TestFunc = [](ImGuiTestContext* ctx) {
    IM_CHECK(RefWindow(ctx, "//Developer Shell"));
    // Tabs live under the tab bar id. Running from the Tests tab leaves Chrome unselected,
    // so the copy button is not submitted until this click.
    ctx->ItemClick("DevShellTabs/Chrome");
    ctx->Yield(2);
    // BeginTabBar + selected BeginTabItem both PushOverrideID, so the button is
    // window / DevShellTabs / Chrome / Copy snippet for chat.
    ctx->ItemClick("DevShellTabs/Chrome/Copy snippet for chat");
    ctx->Yield();
    const char* clip = ImGui::GetClipboardText();
    IM_CHECK(clip != nullptr);
    IM_CHECK(std::strstr(clip, "g_chrome.bandFace") != nullptr);
  };

  ImGuiTest* logCopy = IM_REGISTER_TEST(engine, "gosurvey", "log-copy");
  logCopy->TestFunc = [](ImGuiTestContext* ctx) {
    IM_CHECK(RefWindow(ctx, "//Developer Shell"));
    ctx->ItemClick("DevShellTabs/Log");
    ctx->Yield(2);
    ctx->ItemClick("DevShellTabs/Log/Copy command log");
    ctx->Yield();
    const char* cmdClip = ImGui::GetClipboardText();
    IM_CHECK(cmdClip != nullptr);
    IM_CHECK(std::strstr(cmdClip, "// Developer Shell command log") != nullptr);
    ctx->ItemClick("DevShellTabs/Log/Copy activity log");
    ctx->Yield();
    const char* actClip = ImGui::GetClipboardText();
    IM_CHECK(actClip != nullptr);
    IM_CHECK(std::strstr(actClip, "// Developer Shell activity log") != nullptr);
  };
}

#endif
