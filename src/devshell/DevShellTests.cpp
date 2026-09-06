#include "DevShell.hpp"

#ifdef GOSURVEY_DEVELOPER_SHELL

#include "CadBlocks.hpp"
#include "CadCommands.hpp"
#include "GsIo.hpp"
#include "util/cadblock.hpp"

#include <imgui.h>
#include <imgui_te_context.h>
#include <imgui_te_engine.h>

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

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

void SubmitCad(ImGuiTestContext* ctx, const char* line)
{
  assert(ctx != nullptr);
  assert(s_cmd != nullptr);
  assert(line != nullptr);
  std::vector<std::string>* log = DevShell_CommandLog();
  IM_CHECK_NO_RET(log != nullptr);
  if (!log)
    return;
  char buf[1024];
  std::snprintf(buf, sizeof(buf), "%s", line);
  ProcessCommandLineSubmit(buf, static_cast<int>(sizeof(buf)), *s_cmd, *log);
  ctx->Yield();
}

bool CadLogHas(std::string_view needle)
{
  return DevShell_CommandLogContains(needle);
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

  ImGuiTest* blocks = IM_REGISTER_TEST(engine, "gosurvey", "issue124-blocks");
  blocks->TestFunc = [](ImGuiTestContext* ctx) {
    IM_CHECK(CancelToIdle(ctx));
    IM_CHECK(DevShell_CommandLog() != nullptr);
    DevShell_Log("ui", "issue124-blocks");

    SubmitCad(ctx, "MKLINE -2,0, 2,0");
    SubmitCad(ctx, "SELLINE");
    SubmitCad(ctx, "BLOCK HYDRANT, 0, 0, CONVERT");
    IM_CHECK(CadLogHas("BLOCK — created \"HYDRANT\""));
    IM_CHECK_EQ(static_cast<int>(s_cmd->blockDefs.size()), 1);
    IM_CHECK_EQ(static_cast<int>(s_cmd->cadBlockRefs.size()), 1);
    IM_CHECK(s_cmd->blockDefs[0].content.lines.size() >= 6);

    SubmitCad(ctx, "INSERT HYDRANT, 100, 200, 1, 1, 0, 1, 5");
    IM_CHECK(CadLogHas("INSERT — placed \"HYDRANT\""));
    IM_CHECK_EQ(static_cast<int>(s_cmd->cadBlockRefs.size()), 2);
    IM_CHECK(s_cmd->cadBlockRefs[1].xf.x == 100.f);
    IM_CHECK(s_cmd->cadBlockRefs[1].xf.z == 5.f);

    SubmitCad(ctx, "SELBLOCK");
    SubmitCad(ctx, "COPYSEL 10, 0");
    IM_CHECK_EQ(static_cast<int>(s_cmd->cadBlockRefs.size()), 3);
    SubmitCad(ctx, "SELBLOCK");
    SubmitCad(ctx, "MOVESEL 1, 2");
    IM_CHECK(s_cmd->cadBlockRefs.back().xf.x == 111.f);
    SubmitCad(ctx, "ROTATESEL 0, 0, 90");
    SubmitCad(ctx, "SCALESEL 0, 0, 2");
    SubmitCad(ctx, "MIRRORSEL 0, 0, 0, 10");
    IM_CHECK(CadLogHas("MIRRORSEL — done."));

    SubmitCad(ctx, "SELBLOCK");
    SubmitCad(ctx, "COPYCLIP");
    const size_t beforePaste = s_cmd->cadBlockRefs.size();
    SubmitCad(ctx, "PASTEBLOCK 0, 50");
    IM_CHECK(s_cmd->cadBlockRefs.size() == beforePaste + 1);

    SubmitCad(ctx, "BEDIT HYDRANT");
    SubmitCad(ctx, "BEDITADD LINE, 0,1, 0,-1");
    SubmitCad(ctx, "ATTDEF TAG, Prompt, DEF, 0, 0.5");
    SubmitCad(ctx, "BPARAM LEN, LINEAR, 1");
    SubmitCad(ctx, "BACTION STRETCH, LEN, 0, 0, 1, 0, 0");
    SubmitCad(ctx, "BVISIBILITY OPEN");
    SubmitCad(ctx, "BSAVE");
    SubmitCad(ctx, "BSAVEAS HYDRANT2");
    SubmitCad(ctx, "BCLOSE");
    IM_CHECK(CadLogHas("BSAVE — saved"));
    IM_CHECK(CadLogHas("ATTDEF — tag TAG"));

    SubmitCad(ctx, "SELBLOCK");
    SubmitCad(ctx, "ATTEDIT TAG, A1");
    SubmitCad(ctx, "ATTSYNC");
    SubmitCad(ctx, "ATTEXT");
    IM_CHECK(CadLogHas("ATTEXT"));
    SubmitCad(ctx, "BSETVIS OPEN");
    SubmitCad(ctx, "BSETPARAM LEN, 2");
    SubmitCad(ctx, "BGRIP");

    SubmitCad(ctx, "MAKEBLOCK INNER");
    SubmitCad(ctx, "BEDIT INNER");
    SubmitCad(ctx, "BEDITADD LINE, 0,0, 0.5,0");
    SubmitCad(ctx, "BSAVE");
    SubmitCad(ctx, "BCLOSE");
    SubmitCad(ctx, "BLOCKNEST HYDRANT, INNER");
    IM_CHECK(CadLogHas("BLOCKNEST"));
    SubmitCad(ctx, "BLOCKNEST HYDRANT, HYDRANT");
    IM_CHECK(CadLogHas("circular"));

    SubmitCad(ctx, "MKLINE 8,8, 9,8");
    SubmitCad(ctx, "SELLINE");
    SubmitCad(ctx, "BLOCKREDEF HYDRANT, 8, 8");
    IM_CHECK(CadLogHas("BLOCKREDEF"));

    SubmitCad(ctx, "BLOCKLIST");
    SubmitCad(ctx, "BLOCKSTATS HYDRANT");
    SubmitCad(ctx, "BLOCKLIB");
    SubmitCad(ctx, "BLOCKSEARCH HYD");
    SubmitCad(ctx, "BLOCKFAV HYDRANT");
    SubmitCad(ctx, "BLOCKRECENT");
    IM_CHECK(CadLogHas("BLOCKLIB"));
    IM_CHECK(CadLogHas("BLOCKFAV"));

    SubmitCad(ctx, "BLOCKUNITS HYDRANT, inches");
    SubmitCad(ctx, "INSUNITS feet");
    SubmitCad(ctx, "INSERT HYDRANT, 0, 0");
    IM_CHECK(std::fabs(s_cmd->cadBlockRefs.back().xf.sx - (1.f / 12.f)) < 1.e-4f);

    SubmitCad(ctx, "BLOCKPAPER");
    SubmitCad(ctx, "INSERT HYDRANT, 2, 2");
    IM_CHECK(!s_cmd->paperLayouts.empty());
    IM_CHECK(!s_cmd->paperLayouts[0].paperBlockRefs.empty());
    SubmitCad(ctx, "BLOCKMODEL");

    // WBLOCK/BLOCKIMPORT's .gs round-trip was removed by issue #264 (D-2026-09-03-h) and
    // replaced with a .dwg-based one by issue #284 (CadBlockImportTests.cpp covers it).

    std::vector<std::string> ioLog;
    IM_CHECK(SaveGoSurveyTemplateFile(*s_cmd, "issue124-roundtrip.json", ioLog));
    {
      AppCommandState loaded;
      IM_CHECK(LoadGoSurveyTemplateFile(loaded, "issue124-roundtrip.json", ioLog));
      IM_CHECK(!loaded.blockDefs.empty());
      IM_CHECK(!loaded.cadBlockRefs.empty());
    }

    const size_t nRefs = s_cmd->cadBlockRefs.size();
    SubmitCad(ctx, "SELBLOCK");
    SubmitCad(ctx, "EXPLODE");
    IM_CHECK(s_cmd->cadBlockRefs.size() == nRefs - 1);
    IM_CHECK(CadLogHas("EXPLODE"));

    SubmitCad(ctx, "MAKEBLOCK TMPA");
    SubmitCad(ctx, "BLOCKRENAME TMPA, TMPB");
    IM_CHECK(CadLogHas("BLOCKRENAME"));
    SubmitCad(ctx, "PURGE TMPB");
    IM_CHECK(CadLogHas("PURGE"));

    SubmitCad(ctx, "UNDO");
    IM_CHECK(CadLogHas("UNDO"));
    SubmitCad(ctx, "REDO");

    IM_CHECK(CancelToIdle(ctx));
    IM_CHECK(RefWindow(ctx, "//GoSurveyHost/RibbonStrip"));
    ctx->ItemClick("Insert");
    ctx->Yield(8);
    IM_CHECK(RefWindow(ctx, "//GoSurveyHost/RibbonStrip/RibbonToolsLeft/RibbonSecInsBlock"));
    IM_CHECK(ctx->ItemExists("##RibbonInsInsert"));
    ctx->ItemClick("##RibbonInsCreate");
    ctx->Yield(2);
    IM_CHECK(CadLogHas("BLOCK"));
    IM_CHECK(RefWindow(ctx, "//GoSurveyHost/RibbonStrip"));
    ctx->ItemClick("Home");
    ctx->Yield(4);

    IM_CHECK(RefWindow(ctx, "//Developer Shell"));
    ctx->ItemClick("DevShellTabs/Log");
    ctx->Yield(2);
    IM_CHECK(ctx->ItemExists("DevShellTabs/Log/##LogFilter"));
    IM_CHECK(DevShell_ActivityLogContains("issue124-blocks"));
  };

  ImGuiTest* matchline = IM_REGISTER_TEST(engine, "gosurvey", "matchline-insert-text");
  matchline->TestFunc = [](ImGuiTestContext* ctx) {
    IM_CHECK(CancelToIdle(ctx));
    IM_CHECK(CadBlockFindDef(s_cmd->blockDefs, "_matchline_NORTHING") >= 0);

    SubmitCad(ctx, "INSERT");
    IM_CHECK_EQ(s_cmd->active, AppCommandState::Kind::InsertBlock);
    std::vector<std::string>* log = DevShell_CommandLog();
    IM_CHECK(log != nullptr);
    std::snprintf(s_cmd->insertBlockName, sizeof(s_cmd->insertBlockName), "_matchline_NORTHING");
    CadBlocksApplyInsertNameDefaults(*s_cmd);
    s_cmd->insertBlockSpecifyPoint = true;
    s_cmd->insertBlockSpecifyScale = false;
    s_cmd->insertBlockSpecifyRot = true;
    CadBlocksCommitInsertDialog(*s_cmd, *log);
    ctx->Yield(4);
    IM_CHECK_EQ(s_cmd->insertBlockPhase, AppCommandState::InsertBlockPhase::WaitInsertPoint);

    SubmitInsertBlockPick(*s_cmd, 0.f, 0.f, *log);
    ctx->Yield(2);
    IM_CHECK_EQ(s_cmd->insertBlockPhase, AppCommandState::InsertBlockPhase::WaitRotation);

    SubmitCad(ctx, "90");
    ctx->Yield(6);
    IM_CHECK(s_cmd->insertBlockAttrDialogOpen);
    IM_CHECK(!s_cmd->cadBlockRefs.empty());
    std::snprintf(s_cmd->insertBlockAttrBuf[0], sizeof(s_cmd->insertBlockAttrBuf[0]), "N123");
    std::snprintf(s_cmd->insertBlockAttrBuf[1], sizeof(s_cmd->insertBlockAttrBuf[1]), "5000.00");
    IM_CHECK(RefWindow(ctx, "//Edit Attributes"));
    CadBlocksCommitInsertAttrDialog(*s_cmd, *log);
    ctx->Yield(6);
    IM_CHECK(CadLogHas("INSERT — attributes updated."));
    IM_CHECK_EQ(s_cmd->active, AppCommandState::Kind::None);

    SubmitCad(ctx, "ZOOMEXTENTS");
    ctx->Yield(12);

    std::vector<CadAnnotation> anns;
    CadBlockCollectWorldAnnotations(s_cmd->blockDefs, s_cmd->cadBlockRefs[0], &anns);
    IM_CHECK(anns.size() >= 2);
    bool sawMatch = false;
    bool sawAttr = false;
    for (const CadAnnotation& a : anns) {
      if (a.text.find("MATCH") != std::string::npos)
        sawMatch = true;
      if (a.text.find("N123") != std::string::npos || a.text.find("5000") != std::string::npos)
        sawAttr = true;
    }
    IM_CHECK(sawMatch);
    IM_CHECK(sawAttr);
    IM_CHECK(CadNeedsAnnotationOverlay(s_cmd->cadAnnotations.size(), s_cmd->cadTables.size(),
                                       s_cmd->cadBlockRefs.size(), false, false));
  };

  // Screenshot every ribbon tab (evidence for the icon-wiring pass). Run with:
  //   GoSurvey.exe --devshell-run ribbon-tab-shots
  // BMPs land next to the executable as ribbon-<n>-<tab>.bmp.
  ImGuiTest* ribbonShots = IM_REGISTER_TEST(engine, "gosurvey", "ribbon-tab-shots");
  ribbonShots->TestFunc = [](ImGuiTestContext* ctx) {
    IM_CHECK(CancelToIdle(ctx));

    // Home tab at three widths — first, before anything opens overlay windows.
    s_cmd->showToolspaceWindow = false;
    ctx->WindowCollapse("//Developer Shell", true);
    if (RefWindow(ctx, "//GoSurveyHost/RibbonStrip"))
      ctx->ItemClick("Home");
    s_cmd->activeRibbonTab = kRibbonTabHome;
    for (const int wpx : {2560, 1500, 1000}) {
      DevShell_SetWindowSize(wpx, 1300);
      ctx->Yield(8);
      char hp[48];
      std::snprintf(hp, sizeof(hp), "ribbon-home-%d.bmp", wpx);
      DevShell_RequestScreenshot(hp);
      ctx->Yield(3);
    }
    DevShell_SetWindowSize(2560, 1300);
    ctx->Yield(6);

    struct Tab { const char* label; int idx; };
    const Tab tabs[] = {
        {"Home", kRibbonTabHome},       {"Insert", kRibbonTabInsert},
        {"Annotate", kRibbonTabAnnotate}, {"View", kRibbonTabView},
        {"Manage", kRibbonTabManage},    {"Output", kRibbonTabOutput},
        {"Survey", kRibbonTabSurvey},
    };
    int n = 0;
    for (const Tab& t : tabs) {
      if (RefWindow(ctx, "//GoSurveyHost/RibbonStrip"))
        ctx->ItemClick(t.label);
      s_cmd->activeRibbonTab = t.idx;  // belt-and-suspenders if the label click missed
      ctx->Yield(4);
      char path[64];
      std::snprintf(path, sizeof(path), "ribbon-%d-%s.bmp", ++n, t.label);
      DevShell_RequestScreenshot(path);
      ctx->Yield(3);
    }
    ctx->WindowCollapse("//Developer Shell", false);

    // Block Editor contextual tab — needs an open definition.
    SubmitCad(ctx, "MKLINE -2,0, 2,0");
    SubmitCad(ctx, "SELLINE");
    SubmitCad(ctx, "MAKEBLOCK SHOTBLK");
    SubmitCad(ctx, "BEDIT SHOTBLK");
    ctx->Yield(6);
    DevShell_RequestScreenshot("ribbon-8-BlockEditor.bmp");
    ctx->Yield(3);
    SubmitCad(ctx, "BCLOSE");
    ctx->Yield(2);
  };
}

#endif
