#include "PdfViewerCore.hpp"
#include "DevShell.hpp"

#ifdef GOSURVEY_DEVELOPER_SHELL

#include "CadBlocks.hpp"
#include "CadUi.hpp"
#include "CadColor.hpp"
#include "CadCommands.hpp"
#include "GsIo.hpp"
#include "util/cadblock.hpp"
#include "brep.hpp"
#include "solidpick.hpp"
#include "render/Camera.hpp"

#include <imgui.h>
#include <imgui_te_context.h>
#include <imgui_te_engine.h>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
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

/// Open a NEW drawing tab for a test that asserts on document contents, and put the app-wide view
/// settings a test may have changed back to their defaults.
///
/// Always a new tab, never "only when on the Start tab": the GUI tests run in ONE process, so a test
/// that reused whatever drawing the previous one left saw its BOX, its visual style and its section
/// clip, and asserted `cadSolids.size() == 1` against two solids (code review on #478, finding 7).
/// Per-tab state (the camera, the UCS, the section clip) starts fresh with the tab; the visual
/// style is app-wide, so it is reset here explicitly.
///
/// `NewDrawingInTab` is what the start screen's "New Drawing" button and the tab strip's "+" both
/// call — reached directly for the reason the headless driver reaches `SetActiveSpace` directly.
bool OpenFreshDrawing(ImGuiTestContext* ctx)
{
  assert(ctx != nullptr);
  assert(s_cmd != nullptr);
  std::vector<std::string>* log = DevShell_CommandLog();
  IM_CHECK_NO_RET(log != nullptr);
  if (!log)
    return false;
  NewDrawingInTab(*s_cmd, *log);
  ctx->Yield(6);
  SubmitCad(ctx, "VISUALSTYLE 2D");
  ctx->Yield(2);
  const bool fresh = s_cmd->activeDrawingIdx != 0 && s_cmd->cadSolids.empty() && !s_cmd->viewportSectionClip &&
                     s_cmd->viewportVisualStyle == VisualStyle::Wireframe2D;
  IM_CHECK_NO_RET(fresh);
  return fresh;
}

/// Pixels that differ between two viewport captures written by `DevShell_RequestViewportCapture`
/// (24-bit, 54-byte header), by more than \p tol in any channel. -1 when either file is unreadable or
/// the two are not the same size — which a caller must treat as a failure, never as "no difference".
long CountDifferingPixels(const char* pathA, const char* pathB, int tol)
{
  const auto slurp = [](const char* p, std::vector<unsigned char>* out) {
    std::FILE* f = std::fopen(p, "rb");
    if (!f)
      return false;
    unsigned char buf[65536];
    std::size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0)
      out->insert(out->end(), buf, buf + n);
    std::fclose(f);
    return out->size() > 54;
  };
  std::vector<unsigned char> a, b;
  if (!slurp(pathA, &a) || !slurp(pathB, &b) || a.size() != b.size())
    return -1;
  long diff = 0;
  for (std::size_t i = 54; i + 2 < a.size(); i += 3) {
    for (int c = 0; c < 3; ++c) {
      if (std::abs(static_cast<int>(a[i + c]) - static_cast<int>(b[i + c])) > tol) {
        ++diff;
        break;
      }
    }
  }
  return diff;
}

bool CadLogHas(std::string_view needle)
{
  return DevShell_CommandLogContains(needle);
}

/// Click a clickable prompt option in the command bar by its visible text (REQ-040 / REQ-341).
///
/// By ID rather than by path, and the reason is worth keeping: the option links ARE ordinary ImGui
/// items — a gather of the command bar lists them as `ON`, `OFF`, `FLIP` — but they are not
/// addressable as `//##CommandBarFloat/ON`, because they sit inside the bar's own ID scope rather
/// than at the window root. Gathering and matching the label sidesteps the path question entirely,
/// and fails loudly (returns false) if the link is not on screen at all, which is the thing a test
/// actually wants to know.
bool ClickCommandBarLink(ImGuiTestContext* ctx, const char* label)
{
  assert(ctx != nullptr);
  assert(label != nullptr);
  ImGuiTestItemList items;
  ctx->GatherItems(&items, "");
  for (int i = 0; i < items.GetSize(); ++i)
  {
    const ImGuiTestItemInfo* it = items.GetByIndex(i);
    if (it && std::strcmp(it->DebugLabel, label) == 0)
    {
      ctx->ItemClick(it->ID);
      return true;
    }
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


  // D-2026-09-24-b — Enter alone at a prompt that advertises a default must take that default.
  //
  // Driven through the REAL ImGui tree because that is where the defect lives: the command layer's
  // blank-Enter branch is already unit-tested and correct, and the failure is entirely in which
  // widget swallows the keypress (there are TWO InputTexts bound to `cmdBuf` — the floating command
  // bar and the viewport dynamic input — plus a raw poll in main.cpp gated on `io.WantTextInput`).
  // A unit test cannot see any of that.
  //
  //   build\devshell\GoSurvey.exe --devshell-run req024-blank-enter-default
  // REQ-387 (#732): the PDF viewer window, driven in the real app. The pure core and the PDFium
  // wrapper are unit-tested (PdfViewerTests.cpp); this proves the window opens, shows the first page
  // and survives being scrolled end to end, and reports the REQ-387 timings.
  //
  //   build\devshell\GoSurvey.exe --devshell-run pdfview-bench   (report goes to stderr)
  ImGuiTest* pdfBench = IM_REGISTER_TEST(engine, "gosurvey", "pdfview-bench");
  pdfBench->TestFunc = [](ImGuiTestContext* ctx) {
    auto viewerShown = [] {
      for (ImGuiWindow* w : ImGui::GetCurrentContext()->Windows)
        if (w->WasActive && std::strstr(w->Name, "###pdfview") != nullptr)
          return true;
      return false;
    };
    IM_CHECK(CancelToIdle(ctx));
    SubmitCad(ctx, "BENCH PDFVIEW 500");
    ctx->Yield(10);
    IM_CHECK(viewerShown());
    // The bench closes its own viewer when the report is written.
    for (int i = 0; i < 400 && viewerShown(); ++i)
      ctx->Yield(30);
    IM_CHECK(!viewerShown());
  };

  // REQ-392: the comparison overlay of two generated 500-page PDFs, zoomed and panned in the real app; the
  // viewer's cost per frame is reported on stderr (target: no frame over 16 ms).
  //
  //   build\devshell\GoSurvey.exe --devshell-run pdfcompare-bench
  ImGuiTest* pdfCompareBench = IM_REGISTER_TEST(engine, "gosurvey", "pdfcompare-bench");
  pdfCompareBench->TestFunc = [](ImGuiTestContext* ctx) {
    auto viewerShown = [] {
      for (ImGuiWindow* w : ImGui::GetCurrentContext()->Windows)
        if (w->WasActive && std::strstr(w->Name, "###pdfview") != nullptr)
          return true;
      return false;
    };
    IM_CHECK(CancelToIdle(ctx));
    SubmitCad(ctx, "BENCH PDFCOMPARE 500");
    ctx->Yield(10);
    IM_CHECK(viewerShown());
    // The bench closes its own viewer (and deletes its two files) when the report is written.
    for (int i = 0; i < 400 && viewerShown(); ++i)
      ctx->Yield(30);
    IM_CHECK(!viewerShown());
  };

  // REQ-387 clause 7 (D-2026-10-06-b): the viewer is its own Windows window with the OS frame, docks
  // into GoSurvey's layout and comes back out. Real window creation and docking run in the real app;
  // what a person sees on screen is still checked by hand.
  //
  //   build\devshell\GoSurvey.exe --devshell-run pdfview-window
  // The same measurement on a REAL file named by GOSURVEY_BENCH_PDF: how long until the first page and the
  // visible thumbnail strip are on screen (REQ-387; reported on stderr like pdfview-bench).
  ImGuiTest* pdfReal = IM_REGISTER_TEST(engine, "gosurvey", "pdfview-real");
  pdfReal->TestFunc = [](ImGuiTestContext* ctx) {
    const char* file = std::getenv("GOSURVEY_BENCH_PDF");
    IM_CHECK(file != nullptr);
    if (file == nullptr)
      return;
    auto viewerShown = [] {
      for (ImGuiWindow* w : ImGui::GetCurrentContext()->Windows)
        if (w->WasActive && std::strstr(w->Name, "###pdfview") != nullptr)
          return true;
      return false;
    };
    IM_CHECK(CancelToIdle(ctx));
    s_cmd->pdfViewBenchPath = file;
    ctx->Yield(10);
    IM_CHECK(viewerShown());
    for (int i = 0; i < 600 && viewerShown(); ++i)
      ctx->Yield(30);
    IM_CHECK(!viewerShown());
  };

  ImGuiTest* pdfWin = IM_REGISTER_TEST(engine, "gosurvey", "pdfview-window");
  pdfWin->TestFunc = [](ImGuiTestContext* ctx) {
    namespace fs = std::filesystem;
    const fs::path pdf = fs::temp_directory_path() / "gosurvey_pdfview_window.pdf";
    {
      const std::string bytes = pdfview::MakeSyntheticPdf(30, 200, 0);
      std::ofstream f(pdf, std::ios::binary);
      f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
    auto findViewer = []() -> ImGuiWindow* {
      for (ImGuiWindow* w : ImGui::GetCurrentContext()->Windows)
        if (w->WasActive && std::strstr(w->Name, "###pdfview") != nullptr)
          return w;
      return nullptr;
    };
    IM_CHECK(CancelToIdle(ctx));
    s_cmd->pdfViewerOpenRequest = pdf.u8string();
    ctx->Yield(60);
    ImGuiWindow* w = findViewer();
    IM_CHECK(w != nullptr);
    if (w == nullptr)
      return;
    const ImGuiID mainId = ImGui::GetMainViewport()->ID;
    // Its own OS window, with the operating system's frame (not ImGui's), and a task-bar entry.
    IM_CHECK(w->ViewportOwned);
    IM_CHECK(w->Viewport != nullptr && w->Viewport->ID != mainId);
    IM_CHECK(w->Viewport != nullptr && (w->Viewport->Flags & ImGuiViewportFlags_NoDecoration) == 0);
    IM_CHECK(w->Viewport != nullptr && (w->Viewport->Flags & ImGuiViewportFlags_NoTaskBarIcon) == 0);
    ctx->Yield(120);  // let it render: stays up, no crash with a second GL window

    // Dock it into the layout: it joins the main window's viewport.
    // Any panel docked in the main layout will do as the target.
    ImGuiWindow* target = nullptr;
    for (ImGuiWindow* o : ImGui::GetCurrentContext()->Windows)
      if (o->WasActive && o != w && o->DockIsActive && o->DockNode != nullptr && o->Viewport != nullptr && o->Viewport->ID == mainId) {
        target = o;
        break;
      }
    IM_CHECK(target != nullptr);
    if (target == nullptr)
      return;
    DevShell_Logf("pdfview", "docking into the node of '%s'", target->Name);
    // A floating viewer has no ImGui title bar to drag (the OS draws it), so the Test Engine cannot grab one;
    // the layout API docks it, which is the same state a drag onto the slot produces.
    ImGui::DockBuilderDockWindow(w->Name, target->DockNode->ID);
    ctx->Yield(10);
    ctx->Yield(10);
    w = findViewer();
    IM_CHECK(w != nullptr);
    IM_CHECK(w != nullptr && w->DockIsActive);
    IM_CHECK(w != nullptr && w->Viewport != nullptr && w->Viewport->ID == mainId);
    ctx->Yield(120);

    // And out again: a window of its own once more.
    if (w != nullptr)
      ctx->UndockWindow(w->Name);
    ctx->Yield(10);
    w = findViewer();
    IM_CHECK(w != nullptr && !w->DockIsActive);
    IM_CHECK(w != nullptr && w->ViewportOwned);

    std::error_code ec;
    fs::remove(pdf, ec);
  };

  ImGuiTest* blankEnter = IM_REGISTER_TEST(engine, "gosurvey", "req024-blank-enter-default");
  blankEnter->TestFunc = [](ImGuiTestContext* ctx) {
    IM_CHECK(CancelToIdle(ctx));
    IM_CHECK(OpenFreshDrawing(ctx));

    // Setup is submitted directly — what is under test is the KEYPRESS, not the typing.
    SubmitCad(ctx, "PIPERUN");
    ctx->Yield(2);
    IM_CHECK_EQ(s_cmd->active, AppCommandState::Kind::PipeRun);
    SubmitCad(ctx, "2in");
    ctx->Yield(2);
    IM_CHECK_EQ(s_cmd->pipeRunPhase, AppCommandState::PipeRunPhase::WaitWallThickness);

    // THE REPORTED BUG. The prompt reads "wall thickness in inches <0.154, schedule 40>, Enter to
    // accept:", so a bare Enter must take 0.154 and move on to the start point. It did nothing:
    // the global blank-line block in ProcessCommandLineSubmit consumes every empty Enter before the
    // per-command dispatch and had no PIPERUN case, so the command's own (correct, unit-tested)
    // blank-Enter handling was unreachable from the GUI.
    ctx->KeyPress(ImGuiKey_Enter);
    ctx->Yield(3);
    IM_CHECK_EQ(s_cmd->pipeRunPhase, AppCommandState::PipeRunPhase::WaitFirstPoint);
    IM_CHECK(s_cmd->pipeRunWallThicknessIn > 0.0);

    // And again at the routing prompt, where Enter is advertised as "finish": with a run of one
    // segment down, a bare Enter must COMMIT it rather than leave the command hanging.
    SubmitCad(ctx, "0,0");
    SubmitCad(ctx, "10,0");
    ctx->Yield(2);
    // The route is already REAL geometry while it is drawn (D-2026-09-24-d), so the run exists
    // before Enter; what Enter must do is FINISH it — retiring the provisional entity and committing
    // the finished route in its place, leaving one run and no active command.
    IM_CHECK_EQ(s_cmd->cadPipeRuns.size(), static_cast<std::size_t>(1));
    IM_CHECK(s_cmd->pipeRunLiveIndex >= 0);
    ctx->KeyPress(ImGuiKey_Enter);
    ctx->Yield(3);
    IM_CHECK_EQ(s_cmd->cadPipeRuns.size(), static_cast<std::size_t>(1));
    IM_CHECK_EQ(s_cmd->pipeRunLiveIndex, -1);
    IM_CHECK_EQ(s_cmd->active, AppCommandState::Kind::None);

    IM_CHECK(CancelToIdle(ctx));
  };

  // D-2026-09-24-f — ONE Enter keypress must produce exactly ONE submission.
  //
  // Reported as "Enter has random behavior": typing ORBIT and pressing Enter started the command
  // and immediately exited it, and PIPERUN walked two prompts per keypress until it announced
  // itself cancelled. Both are the same defect. An InputText flagged `EnterReturnsTrue` clears its
  // own active ID as the last thing it does, so main.cpp's raw Enter poll — gated on "no widget is
  // capturing" — ran later in the SAME frame, found nothing active, and submitted again; by then
  // `ProcessCommandLineSubmit` had emptied the buffer, so the second submission arrived as a bare
  // Enter and every command whose first prompt gives a blank Enter a meaning acted on it.
  //
  // Only reproducible through the real widget tree: the keypress has to reach an ImGui InputText
  // for the frame in question to exist at all, which is why `SubmitCad` (a direct call) cannot see
  // it and why the unit tests shipped green throughout.
  //
  //   build\devshell\GoSurvey.exe --devshell-run d-2026-09-24-f-one-enter-one-submit
  ImGuiTest* oneEnter = IM_REGISTER_TEST(engine, "gosurvey", "d-2026-09-24-f-one-enter-one-submit");
  oneEnter->TestFunc = [](ImGuiTestContext* ctx) {
    IM_CHECK(CancelToIdle(ctx));
    IM_CHECK(OpenFreshDrawing(ctx));

    // ORBIT is the clearest case: a bare Enter is its documented EXIT, so a phantom second submit
    // ends the command on the very keypress that started it.
    IM_CHECK(RefCommandBar(ctx));
    ctx->ItemClick("GoSurveyCmdPanel/##CommandLineInput");
    ctx->KeyCharsReplaceEnter("ORBIT");
    ctx->Yield(3);
    IM_CHECK_EQ(s_cmd->active, AppCommandState::Kind::Orbit);
    IM_CHECK(CancelToIdle(ctx));

    // PIPERUN shows the same defect as a SKIPPED prompt: answering the size must leave the command
    // at the wall-thickness prompt, not carry on through it on the same keypress.
    IM_CHECK(RefCommandBar(ctx));
    ctx->ItemClick("GoSurveyCmdPanel/##CommandLineInput");
    ctx->KeyCharsReplaceEnter("PIPERUN");
    ctx->Yield(3);
    IM_CHECK_EQ(s_cmd->active, AppCommandState::Kind::PipeRun);
    IM_CHECK_EQ(s_cmd->pipeRunPhase, AppCommandState::PipeRunPhase::WaitNominalSize);

    IM_CHECK(RefCommandBar(ctx));
    ctx->ItemClick("GoSurveyCmdPanel/##CommandLineInput");
    ctx->KeyCharsReplaceEnter("4in");
    ctx->Yield(3);
    IM_CHECK_EQ(s_cmd->pipeRunPhase, AppCommandState::PipeRunPhase::WaitWallThickness);

    IM_CHECK(CancelToIdle(ctx));
  };
  // PIPEPERF exercised end to end: route a real run through the app, then read the profile it
  // reports. Not an assertion about milliseconds — those belong on the reference machine with BENCH
  // — but a check that the counters are wired to the work, and a way to SEE where routing time goes.
  //
  //   build\devshell\GoSurvey.exe --devshell-run pipeperf-routing-profile
  ImGuiTest* pipePerf = IM_REGISTER_TEST(engine, "gosurvey", "pipeperf-routing-profile");
  pipePerf->TestFunc = [](ImGuiTestContext* ctx) {
    IM_CHECK(CancelToIdle(ctx));
    IM_CHECK(OpenFreshDrawing(ctx));
    SubmitCad(ctx, "PIPERUN");
    SubmitCad(ctx, "4in");
    SubmitCad(ctx, "");
    for (int i = 0; i < 4; ++i) {
      char pt[64];
      std::snprintf(pt, sizeof(pt), "%d,%d", 20 * ((i + 1) / 2), 20 * (i / 2));
      SubmitCad(ctx, pt);
      ctx->Yield(2);  // let the display rebuild and tessellate, as it would between clicks
    }
    SubmitCad(ctx, "END");
    ctx->Yield(4);
    IM_CHECK_EQ(s_cmd->cadPipeRuns.size(), static_cast<std::size_t>(1));
    IM_CHECK(s_cmd->pipeRunPerf.sweep.calls > 0);
    IM_CHECK(s_cmd->pipeRunPerf.tessellate.calls > 0);

    SubmitCad(ctx, "PIPEPERF");
    ctx->Yield(2);
    std::vector<std::string>* dlog = DevShell_CommandLog();
    if (dlog) {
      const std::size_t n = dlog->size();
      for (std::size_t i = (n > 12 ? n - 12 : 0); i < n; ++i)
        ctx->LogWarning("%s", (*dlog)[i].c_str());
    }
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


  // --- REQ-341 live section clip, driven through the REAL GUI (TASK-249) -------------------------
  //
  // GitHub #149 acceptance 6 is the one criterion in the whole phase that is about PIXELS, and two
  // of its failure modes cannot be reached anywhere else:
  //
  //   * whether the clip actually removes geometry from the screen. `SectionClipTests` proves the
  //     plane arithmetic and `headless.req341-section-clip` proves the command surface, but neither
  //     has a GL context, so neither can see a single pixel disappear.
  //   * whether the clip STAYS in the viewport. `gl_ClipDistance` is global GL state, and ImGui
  //     draws the entire interface immediately after `RenderScene` with shaders that never write
  //     it — a shader that leaves it unwritten while GL_CLIP_DISTANCE0 is enabled has UNDEFINED
  //     clip distances, so a missing `glDisable` can delete arbitrary parts of the UI. Nothing
  //     without a real frame can catch that, and the symptom would be a ribbon that flickers away
  //     only while the clip is on.
  //
  // The screenshots are the evidence for the first; the test surviving to its own end — every
  // `SubmitCad` after the clip is on still finding its widgets and the log still readable — is the
  // assertion for the second.
  // --- REQ-341: the ON / OFF / FLIP keywords are CLICKABLE (TASK-249 increment) ------------------
  //
  // The whole point of the prompt is that these three can be clicked instead of typed, and that
  // cannot be checked anywhere but the real GUI: the transcript can only type the tokens the links
  // submit, which proves the receiving end works and says nothing about whether a link is there to
  // click. A screenshot cannot show it either — the links are drawn in the command bar, which is
  // ImGui, not in the viewport framebuffer the other test captures.
  //
  // So the test CLICKS them by name. That covers both halves at once: the link exists as a real
  // ImGui item with that label, and clicking it reaches the command.
  // --- REQ-341 REPRO: what a user actually sees, with nothing set up for them -------------------
  //
  // The other GUI test forces `VISUALSTYLE SHADED` and an orbited camera before it clips anything.
  // A user typing SECTIONCLIP on a fresh box has neither, and reported the feature as not working.
  // This captures the DEFAULT path, one frame per step, so the difference can be looked at rather
  // than guessed at.
  ImGuiTest* sclipRepro = IM_REGISTER_TEST(engine, "gosurvey", "req341-section-clip-repro");
  sclipRepro->TestFunc = [](ImGuiTestContext* ctx) {
    IM_CHECK(CancelToIdle(ctx));
    IM_CHECK(OpenFreshDrawing(ctx));
    SubmitCad(ctx, "SECTIONCLIP OFF");
    SubmitCad(ctx, "BOX 0,0 20 14 12");
    ctx->Yield(4);
    IM_CHECK_EQ(s_cmd->cadSolids.size(), static_cast<std::size_t>(1));

    // Frame it, but change NOTHING else: default visual style (2D Wireframe) and the default PLAN
    // view are exactly what a user has on a fresh drawing.
    s_cmd->viewportPanX = 10.f;
    s_cmd->viewportPanY = 7.f;
    s_cmd->viewportPanZ = 6.f;
    s_cmd->viewportZoom = 2.4f;
    ctx->Yield(8);

    DevShell_RequestViewportCapture("repro-1-plan-wire-noclip.bmp", 1400);
    ctx->Yield(4);
    SubmitCad(ctx, "SECTIONCLIP 6");
    ctx->Yield(6);
    IM_CHECK(s_cmd->viewportSectionClip);
    DevShell_RequestViewportCapture("repro-2-plan-wire-clip6.bmp", 1400);
    ctx->Yield(4);

    // Now orbit, still in the DEFAULT wireframe style.
    s_cmd->viewportAzimuthDeg = 135.f;
    s_cmd->viewportElevationDeg = 22.f;
    ctx->Yield(8);
    DevShell_RequestViewportCapture("repro-3-orbit-wire-clip6.bmp", 1400);
    ctx->Yield(4);
    SubmitCad(ctx, "SECTIONCLIP OFF");
    ctx->Yield(6);
    DevShell_RequestViewportCapture("repro-4-orbit-wire-noclip.bmp", 1400);
    ctx->Yield(4);

    // And the same orbited view SHADED, which is the one already known to look right.
    SubmitCad(ctx, "VISUALSTYLE SHADED");
    SubmitCad(ctx, "SECTIONCLIP 6");
    ctx->Yield(8);
    DevShell_RequestViewportCapture("repro-5-orbit-shaded-clip6.bmp", 1400);
    ctx->Yield(4);

    IM_CHECK(CancelToIdle(ctx));
  };

  ImGuiTest* sclipLinks = IM_REGISTER_TEST(engine, "gosurvey", "req341-section-clip-links");
  sclipLinks->TestFunc = [](ImGuiTestContext* ctx) {
    IM_CHECK(CancelToIdle(ctx));
    IM_CHECK(OpenFreshDrawing(ctx));
    IM_CHECK(s_cmd->activeDrawingIdx != 0);

    // The floating bar's width is a persisted USER preference (`cmdBarWidth`), and it lays its
    // prompt out on one line that never wraps (REQ-040). At the 360 px a real preferences file held,
    // `FLIP` sat past the bar's right edge, so ImGui clipped it and there was no item to click — the
    // test was reading the machine's settings, not the feature. Pinned for the test, restored after.
    const float savedBarWidth = s_cmd->cmdBarWidth;
    s_cmd->cmdBarWidth = 1200.f;
    ctx->Yield(2);

    // A known starting state, so each click below is a visible transition rather than a no-op.
    SubmitCad(ctx, "SECTIONCLIP OFF");
    ctx->Yield(2);
    IM_CHECK(!s_cmd->viewportSectionClip);

    // Bare SECTIONCLIP opens the prompt that carries the links.
    SubmitCad(ctx, "SECTIONCLIP");
    ctx->Yield(4);
    IM_CHECK_EQ(s_cmd->active, AppCommandState::Kind::SectionClip);

    // The links only exist when the FLOATING bar is actually being drawn: the classic docked panel
    // takes a different branch, and a hidden bar leaves a stale ImGui window that `WindowInfo` still
    // finds while nothing is drawn into it. Both are asserted so a failure below says which.
    DevShell_Logf("ui", "cmdBarVisible=%d cmdLineClassicDock=%d", s_cmd->cmdBarVisible ? 1 : 0,
                  s_cmd->cmdLineClassicDock ? 1 : 0);
    IM_CHECK(s_cmd->cmdBarVisible);
    IM_CHECK(!s_cmd->cmdLineClassicDock);

    IM_CHECK(RefCommandBar(ctx));

    // ON — the link is an ImGui item labelled with its own text.
    IM_CHECK(ClickCommandBarLink(ctx, "ON"));
    ctx->Yield(4);
    IM_CHECK(s_cmd->viewportSectionClip);                              // the click reached the command
    IM_CHECK_EQ(s_cmd->active, AppCommandState::Kind::None);           // and closed the prompt

    // FLIP — reopen, click, and check the half swapped.
    const bool flipBefore = s_cmd->viewportSectionClipFlip;
    SubmitCad(ctx, "SECTIONCLIP");
    ctx->Yield(4);
    IM_CHECK(RefCommandBar(ctx));
    IM_CHECK(ClickCommandBarLink(ctx, "FLIP"));
    ctx->Yield(4);
    IM_CHECK(s_cmd->viewportSectionClipFlip != flipBefore);
    IM_CHECK_EQ(s_cmd->active, AppCommandState::Kind::None);

    // OFF — the third one, and the one that proves the links are not all wired to the same handler.
    SubmitCad(ctx, "SECTIONCLIP");
    ctx->Yield(4);
    IM_CHECK(RefCommandBar(ctx));
    IM_CHECK(ClickCommandBarLink(ctx, "OFF"));
    ctx->Yield(4);
    IM_CHECK(!s_cmd->viewportSectionClip);
    IM_CHECK_EQ(s_cmd->active, AppCommandState::Kind::None);

    s_cmd->cmdBarWidth = savedBarWidth;
    IM_CHECK(CancelToIdle(ctx));
  };

  ImGuiTest* sclip = IM_REGISTER_TEST(engine, "gosurvey", "req341-section-clip-viewport");
  sclip->TestFunc = [](ImGuiTestContext* ctx) {
    IM_CHECK(CancelToIdle(ctx));

    // The app opens on the Start tab (REQ-308, index 0), which draws no 3D viewport at all.
    IM_CHECK(OpenFreshDrawing(ctx));
    IM_CHECK(s_cmd->activeDrawingIdx != 0);

    // A box tall enough that a horizontal cut through it is unmistakable, shaded so the cut shows
    // as surface rather than as a gap in a wireframe.
    SubmitCad(ctx, "BOX 0,0 20 14 12");
    ctx->Yield(4);
    IM_CHECK_EQ(s_cmd->cadSolids.size(), static_cast<std::size_t>(1));
    SubmitCad(ctx, "VISUALSTYLE SHADED");
    ctx->Yield(2);

    // Orbited and framed explicitly, for the reason the chamfer test above gives: ZOOM EXTENTS
    // frames the plan footprint and leaves the target at z = 0, which puts a 12-tall box's top off
    // the image.
    s_cmd->viewportAzimuthDeg = 135.f;
    s_cmd->viewportElevationDeg = 22.f;
    s_cmd->viewportPanX = 0.f;
    s_cmd->viewportPanY = 0.f;
    s_cmd->viewportPanZ = 6.f;
    s_cmd->viewportZoom = 2.6f;
    ctx->Yield(10);

    // 1 — the whole box, for comparison.
    IM_CHECK(!s_cmd->viewportSectionClip);
    DevShell_RequestViewportCapture("devshell-req341-clip-0-off.bmp", 1400);
    ctx->Yield(4);

    // 2 — cut at the UCS plane (z = 0). The box spans z 0..12, so this removes ALL of it: the
    // strongest possible statement that the clip reaches the pixels, and the frame that would look
    // identical to the one above if the plane were being ignored.
    SubmitCad(ctx, "SECTIONCLIP 0");
    ctx->Yield(6);
    IM_CHECK(s_cmd->viewportSectionClip);
    IM_CHECK(std::fabs(s_cmd->viewportSectionClipOffset - 0.0) < 1e-9);
    DevShell_RequestViewportCapture("devshell-req341-clip-1-at-zero.bmp", 1400);
    ctx->Yield(4);

    // 3 and 4 — the plane MOVES, which is the word acceptance 6 actually uses. Two heights through
    // the body of the box; the visible remainder must grow with the offset.
    SubmitCad(ctx, "SECTIONCLIP 4");
    ctx->Yield(6);
    IM_CHECK(std::fabs(s_cmd->viewportSectionClipOffset - 4.0) < 1e-9);
    DevShell_RequestViewportCapture("devshell-req341-clip-2-at-four.bmp", 1400);
    ctx->Yield(4);

    SubmitCad(ctx, "SECTIONCLIP 8");
    ctx->Yield(6);
    IM_CHECK(std::fabs(s_cmd->viewportSectionClipOffset - 8.0) < 1e-9);
    DevShell_RequestViewportCapture("devshell-req341-clip-3-at-eight.bmp", 1400);
    ctx->Yield(4);

    // 5 — FLIP keeps the other half. Together with shot 3 this covers the whole box between them.
    SubmitCad(ctx, "SECTIONCLIP FLIP");
    ctx->Yield(6);
    IM_CHECK(s_cmd->viewportSectionClipFlip);
    DevShell_RequestViewportCapture("devshell-req341-clip-4-flipped.bmp", 1400);
    ctx->Yield(4);

    // 6 — and OFF restores the whole box, so the clip left nothing behind.
    SubmitCad(ctx, "SECTIONCLIP OFF");
    ctx->Yield(6);
    IM_CHECK(!s_cmd->viewportSectionClip);
    DevShell_RequestViewportCapture("devshell-req341-clip-5-off-again.bmp", 1400);
    ctx->Yield(4);

    // The solid is untouched by all of it — a view state changed nothing in the document. Checked
    // here as well as in the transcript because this is the path that actually rendered.
    IM_CHECK_EQ(s_cmd->cadSolids.size(), static_cast<std::size_t>(1));
    const brep::MassProperties mp = brep::ComputeMassProperties(*s_cmd->cadSolids[0]);
    IM_CHECK(mp.valid);
    IM_CHECK(std::fabs(mp.volume - 20.0 * 14.0 * 12.0) < 1e-6);
    IM_CHECK_EQ(s_cmd->cadSolids[0]->faces.size(), static_cast<std::size_t>(6));

    IM_CHECK(CancelToIdle(ctx));
  };

  // --- REQ-341: a TILTED cut does not move when the view pans (code review on #478, finding 1) -----
  //
  // Solids, meshes and linework are uploaded once against the pan point of the moment and then
  // drawn from that cache until the pan drifts past a budget; the MVP absorbs the difference. The
  // clip plane has to be packed against that SAME cached anchor. Packed against the current pan
  // instead, a non-horizontal cut slides by the pan distance while the cache holds, then jumps back
  // when it rebuilds — invisible to `SectionClipTests`, which test the packing on its own, and to a
  // level cut, which the anchoring never touches.
  //
  // So: pan a little (the cache holds), capture; force a re-upload at that SAME pan by panning far
  // away and back, capture again. Same camera, same plane, so the two frames must agree. With the
  // bug the first frame's cut is 3 ft off, about 100 px here.
  ImGuiTest* sclipPan = IM_REGISTER_TEST(engine, "gosurvey", "req341-section-clip-pan");
  sclipPan->TestFunc = [](ImGuiTestContext* ctx) {
    IM_CHECK(CancelToIdle(ctx));
    IM_CHECK(OpenFreshDrawing(ctx));
    SubmitCad(ctx, "BOX 0,0 20 14 12");
    ctx->Yield(4);
    IM_CHECK_EQ(s_cmd->cadSolids.size(), static_cast<std::size_t>(1));
    // A vertical cut through the box, normal along world Y, so the plane's anchor term is n.y * panY.
    SubmitCad(ctx, "UCS");
    SubmitCad(ctx, "X");
    SubmitCad(ctx, "90");
    IM_CHECK(std::fabs(s_cmd->activeUcs.zAxis.y + 1.0) < 1e-9);  // +Z now world -Y
    SubmitCad(ctx, "SECTIONCLIP 0");
    ctx->Yield(2);
    IM_CHECK(s_cmd->viewportSectionClip);

    s_cmd->viewportPanX = 0.f;
    s_cmd->viewportPanY = 0.f;
    s_cmd->viewportPanZ = 0.f;
    s_cmd->viewportZoom = 2.4f;  // orthoHalfH ~20.8, so the drift budget is ~10 ft
    ctx->Yield(8);               // uploaded at anchor (0, 0)

    s_cmd->viewportPanY = 3.f;  // inside the budget: the cache is drawn with a 3 ft MVP offset
    ctx->Yield(8);
    DevShell_RequestViewportCapture("devshell-req341-pan-cached.bmp", 1400);
    ctx->Yield(4);

    s_cmd->viewportPanY = 200.f;  // past the budget: re-uploaded at 200
    ctx->Yield(8);
    s_cmd->viewportPanY = 3.f;  // and past it again: re-uploaded at exactly this pan
    ctx->Yield(8);
    DevShell_RequestViewportCapture("devshell-req341-pan-fresh.bmp", 1400);
    ctx->Yield(4);

    // Not a black or empty frame (the GL_FRONT trap TASK-249 recorded): the clip-off frame must differ.
    SubmitCad(ctx, "SECTIONCLIP OFF");
    ctx->Yield(8);
    DevShell_RequestViewportCapture("devshell-req341-pan-noclip.bmp", 1400);
    ctx->Yield(4);

    const long cutMoved =
        CountDifferingPixels("devshell-req341-pan-cached.bmp", "devshell-req341-pan-fresh.bmp", 8);
    const long clipShows =
        CountDifferingPixels("devshell-req341-pan-fresh.bmp", "devshell-req341-pan-noclip.bmp", 8);
    DevShell_Logf("ui", "req341-pan: cached-vs-fresh differing px=%ld, clip-vs-noclip=%ld", cutMoved, clipShows);
    IM_CHECK(cutMoved >= 0);
    IM_CHECK(clipShows > 500);        // the captures are real, and the clip reaches them
    IM_CHECK(cutMoved < 50);          // and the cut did not move with the pan
    IM_CHECK(CancelToIdle(ctx));
  };

  // --- REQ-344: the section-plane handles, captured for LOOKING at (user GUI pass, 2026-09-16) ------
  //
  // The flip symbol was reported "at times hard to see" and was enlarged by
  // kSectionPlaneFlipScale, with "we can adjust from there". Size is a judgement made by eye, so this
  // test's job is to produce the frame to judge — orbited and shaded, the view the report came from —
  // and to assert only that the plane and its handles are really there to be seen.
  ImGuiTest* planeGrips = IM_REGISTER_TEST(engine, "gosurvey", "req344-section-plane-handles");
  planeGrips->TestFunc = [](ImGuiTestContext* ctx) {
    IM_CHECK(CancelToIdle(ctx));
    IM_CHECK(OpenFreshDrawing(ctx));
    SubmitCad(ctx, "BOX 0,0 20 14 12");
    SubmitCad(ctx, "VISUALSTYLE SHADED");
    ctx->Yield(4);
    IM_CHECK_EQ(s_cmd->cadSolids.size(), static_cast<std::size_t>(1));
    std::vector<std::string>* log = DevShell_CommandLog();
    IM_CHECK(log != nullptr);
    StartSectionPlaneCommand(*s_cmd, *log);
    ray3d::Ray down;
    down.origin = ray3d::Vec3{0.0, 0.0, 100.0};
    down.dir = ray3d::Vec3{0.0, 0.0, -1.0};
    solidpick::Tolerance tol;
    tol.vertex = tol.edge = 0.5;
    IM_CHECK(SubmitSectionPlaneFacePick(*s_cmd, down, tol, *log));  // the top face, z = 12
    IM_CHECK(s_cmd->viewportSectionClip);
    IM_CHECK(s_cmd->sectionPlaneSelected);  // selected on creation, so the handles draw
    s_cmd->viewportSectionClipOffset = -4.0;  // slid into the box so the cut is visible too
    s_cmd->viewportAzimuthDeg = 135.f;
    s_cmd->viewportElevationDeg = 28.f;
    s_cmd->viewportPanX = 0.f;
    s_cmd->viewportPanY = 0.f;
    s_cmd->viewportPanZ = 6.f;
    s_cmd->viewportZoom = 2.4f;
    ctx->Yield(10);
    DevShell_RequestViewportCapture("devshell-req344-plane-handles.bmp", 1400);
    ctx->Yield(4);
    IM_CHECK(CancelToIdle(ctx));
  };

  // --- REQ-331 CHAMFER, driven through the REAL GUI (TASK-229) -----------------------------------
  //
  // Everything else about the solid chamfer is covered by unit tests and headless transcripts. Two
  // things are not, and neither can be:
  //
  //   * the sub-object pre-highlight, which needs a MODIFIER-HELD CURSOR over a specific 3D edge.
  //     TASK-221 left this as DEBT-2 and TASK-222 shipped an "argument from similarity" in place of
  //     a measurement. This is the measurement.
  //   * that a Ctrl+click in the viewport reaches the chamfer at all. The transcripts use a
  //     `SUBOBJECT` driver verb, which is the pick's INTERNALS - it never exercises the routing from
  //     a real mouse button through `ViewportPickPolicy` to the sub-object pick.
  //
  // The cursor is aimed by projecting a world point with the same camera the viewport draws with,
  // then offsetting by the viewport image's screen origin (`DevShell_ViewportRect`, REQ-161).
  ImGuiTest* chamfer = IM_REGISTER_TEST(engine, "gosurvey", "req331-chamfer-viewport");
  chamfer->TestFunc = [](ImGuiTestContext* ctx) {
    IM_CHECK(CancelToIdle(ctx));

    // The app opens on the Start tab (REQ-308, index 0), which backs no document and draws no 3D
    // viewport at all. Everything below needs a real drawing, so make one the way a user does.
    IM_CHECK(OpenFreshDrawing(ctx));
    IM_CHECK(s_cmd->activeDrawingIdx != 0);

    SubmitCad(ctx, "BOX 0,0 20 10 8");
    ctx->Yield(4);
    IM_CHECK_EQ(s_cmd->cadSolids.size(), static_cast<std::size_t>(1));

    // Orbit, so a top edge is not directly behind the bottom edge under it - in PLAN view a ray at
    // one also passes through the other and which comes back is a depth-order detail. There is no
    // typed verb for this (the product routes are the ViewCube and a mouse drag), so the test writes
    // the same two fields they do - which is exactly what the headless driver's VIEWANGLES does.
    s_cmd->viewportAzimuthDeg = 135.f;
    s_cmd->viewportElevationDeg = 20.f;

    // Framed EXPLICITLY rather than with ZOOM EXTENTS, and the measurement is why: extents frames the
    // plan FOOTPRINT (its own log line says "span 20 x 10") and leaves the target at z = 0, so on an
    // orbited view of a box 8 tall the top-back edge projected to y = -33 - just off the top of the
    // image, which is where the cursor was landing. Target the box centre and give the view room:
    // orthoHalfH is 50/zoom, so zoom 3.2 shows +-15.6 vertically against a box whose largest half
    // extent is about 12.
    s_cmd->viewportPanX = 0.f;
    s_cmd->viewportPanY = 0.f;
    s_cmd->viewportPanZ = 4.f;
    s_cmd->viewportZoom = 3.2f;
    ctx->Yield(10);  // the rect is written while the viewport DRAWS, so let it draw

    float ox = 0.f;
    float oy = 0.f;
    float sw = 0.f;
    float sh = 0.f;
    IM_CHECK(DevShell_ViewportRect(&ox, &oy, &sw, &sh));

    // The top-back edge of a 20 x 10 x 8 box centred on the origin runs along X at y = 5, z = 8.
    const Camera cam = CadViewCamera(*s_cmd);
    float px = 0.f;
    float py = 0.f;
    cam.WorldToScreen(0.0, 5.0, 8.0, sw, sh, &px, &py);
    const ImVec2 onEdge(ox + px, oy + py);

    // --- The PRE-HIGHLIGHT, which is the whole reason this test is in the GUI ----------------------
    // CHAMFER with nothing selected opens the 2D command; holding Ctrl over a solid edge must then
    // light that edge up. Before REQ-331 nothing lit up at all while CHAMFER ran.
    SubmitCad(ctx, "CHAMFER");
    ctx->Yield();
    IM_CHECK_EQ(s_cmd->active, AppCommandState::Kind::Chamfer);

    // Ctrl goes down BEFORE the cursor arrives, and the cursor then arrives in two steps. The hover
    // pick is rate-gated and only re-runs when the cursor, the view or the geometry moves (issue
    // #166), so pressing Ctrl after the move can land in a window where the gate has already decided
    // "no hover" and has nothing to make it look again. Ordering it this way was flaky-then-green
    // once before this comment existed.
    ctx->KeyDown(ImGuiMod_Ctrl);
    ctx->Yield(2);
    ctx->MouseMoveToPos(ImVec2(onEdge.x + 24.f, onEdge.y + 24.f));
    ctx->Yield(2);
    ctx->MouseMoveToPos(onEdge);
    ctx->Yield(10);  // the hover pick is rate-gated to ~30 Hz, so one frame is not enough
    IM_CHECK(s_cmd->subObjectHoverValid);
    IM_CHECK_EQ(s_cmd->subObjectHover.kind, solidpick::Kind::Edge);
    DevShell_Logf("test", "hovered edge %d of solid %d", s_cmd->subObjectHover.index,
                  s_cmd->subObjectHover.solidIndex);

    // --- What lights up is what SELECTS -------------------------------------------------------------
    const int hoveredEdge = s_cmd->subObjectHover.index;
    ctx->MouseClick(ImGuiMouseButton_Left);
    ctx->Yield(2);
    ctx->KeyUp(ImGuiMod_Ctrl);
    ctx->Yield();
    IM_CHECK_EQ(s_cmd->subObjectSelection.size(), static_cast<std::size_t>(1));
    IM_CHECK_EQ(s_cmd->subObjectSelection[0].kind, solidpick::Kind::Edge);
    IM_CHECK_EQ(s_cmd->subObjectSelection[0].index, hoveredEdge);

    // --- The refusal, by name, with the solid untouched ---------------------------------------------
    SubmitCad(ctx, "100");
    ctx->Yield(2);
    IM_CHECK(CadLogHas("too large"));
    IM_CHECK(CadLogHas("specify a different distance"));
    // TASK-224: the refusal must NOT be followed by a contradictory parse complaint.
    IM_CHECK(!CadLogHas("Could not parse CHAMFER"));
    IM_CHECK_EQ(s_cmd->subObjectSelection.size(), static_cast<std::size_t>(1));

    // --- And the bevel itself, against REQ-331's closed form ----------------------------------------
    SubmitCad(ctx, "2");
    ctx->Yield(2);
    IM_CHECK_EQ(s_cmd->cadSolids.size(), static_cast<std::size_t>(1));
    const brep::MassProperties mp = brep::ComputeMassProperties(*s_cmd->cadSolids[0]);
    IM_CHECK(mp.valid);
    IM_CHECK(std::fabs(mp.volume - 1560.0) < 1e-6);
    IM_CHECK(std::fabs(mp.surfaceArea - (796.0 + 40.0 * 1.41421356237309504880)) < 1e-6);
    IM_CHECK_EQ(s_cmd->cadSolids[0]->faces.size(), static_cast<std::size_t>(7));
    IM_CHECK(s_cmd->subObjectSelection.empty());

    // Queued, not `DevShell_SaveWindowScreenshot`: that one reads the GL FRONT buffer as it is called,
    // and at the end of a test the frame it wants has not been presented - it captured pure black.
    DevShell_RequestScreenshot("devshell-req331-chamfer.bmp");
    ctx->Yield(4);
    IM_CHECK(CancelToIdle(ctx));
  };

  // The ribbon Create Block flow's on-screen base point, reported 2026-09-23: ticking
  // "Specify On-screen" and pressing OK leaves the "BLOCK — pick base point" prompt on screen, and
  // clicking in the viewport selects objects instead of committing the point.
  //
  // In the GUI on purpose. The base-point pick is NOT a routed command step — `cmd.active` stays
  // `Kind::None` and the whole interaction hangs off `cmd.blockCreatePhase`, so neither the headless
  // `CLICK` verb (which asks `ViewportClickRouteFor`) nor any unit test can reach it. A real left
  // button, through the real `DrawDrawingViewport` click block, is the only thing that can.
  ImGuiTest* blockBase = IM_REGISTER_TEST(engine, "gosurvey", "block-create-basepoint-pick");
  blockBase->TestFunc = [](ImGuiTestContext* ctx) {
    IM_CHECK(CancelToIdle(ctx));
    IM_CHECK(OpenFreshDrawing(ctx));
    std::vector<std::string>* log = DevShell_CommandLog();
    IM_CHECK(log != nullptr);

    // The reported scenario: a 3D SOLID selected in an ORBITED view, not a flat line in plan.
    SubmitCad(ctx, "BOX 0,0 20 10 8");
    ctx->Yield(4);
    IM_CHECK_EQ(s_cmd->cadSolids.size(), static_cast<std::size_t>(1));
    s_cmd->selection.clear();
    s_cmd->selection.push_back({SelectedEntity::Type::Solid, 0});
    s_cmd->viewportAzimuthDeg = 135.f;
    s_cmd->viewportElevationDeg = 20.f;
    s_cmd->viewportPanX = 0.f;
    s_cmd->viewportPanY = 0.f;
    s_cmd->viewportPanZ = 4.f;
    s_cmd->viewportZoom = 3.2f;
    ctx->Yield(10);
    IM_CHECK(!s_cmd->selection.empty());
    const int defsBefore = static_cast<int>(s_cmd->blockDefs.size());

    // What the ribbon's Create button does (CadUi.cpp, "##RibbonInsCreate").
    StartBlockCreateDialog(*s_cmd, *log);
    ctx->Yield(4);
    std::snprintf(s_cmd->blockCreateName, sizeof(s_cmd->blockCreateName), "%s", "PICKBASE");
    s_cmd->blockCreateSpecifyBase = true;  // the tick under test; it is also the default
    ctx->Yield(2);
    ctx->ItemClick("//Create Block/OK");
    ctx->Yield(4);

    // The prompt the user is left looking at.
    IM_CHECK_EQ(s_cmd->blockCreatePhase, AppCommandState::BlockCreatePhase::WaitBasePoint);
    IM_CHECK(!s_cmd->blockCreateDialogOpen);
    IM_CHECK(CadLogHas("pick base point"));

    float ox = 0.f, oy = 0.f, sw = 0.f, sh = 0.f;
    IM_CHECK(DevShell_ViewportRect(&ox, &oy, &sw, &sh));
    // Aimed at a real feature of the box — its top-back edge, at world (0, 5, 8) — rather than at
    // bare space, because the assertion below is about SNAPPING, which needs something to snap to.
    // The same point and camera `req331-chamfer-viewport` uses, for the same reason: it is known to
    // project inside the viewport and clear of the Developer Shell's own window, which floats over
    // the middle of the viewport and would make the click measure the harness (the viewport reads
    // as un-hovered under any overlapping window — see the `AllowWhenOverlappedByWindow` note in
    // CadUi.cpp).
    const Camera cam = CadViewCamera(*s_cmd);
    float px = 0.f;
    float py = 0.f;
    cam.WorldToScreen(0.0, 5.0, 8.0, sw, sh, &px, &py);
    const ImVec2 inViewport(ox + px, oy + py);
    ctx->MouseMoveToPos(ImVec2(inViewport.x + 30.f, inViewport.y + 30.f));
    ctx->Yield(2);
    ctx->MouseMoveToPos(inViewport);
    ctx->Yield(4);

    // Object snap must be LIVE during this pick. `cmd.active` stays `Kind::None` the whole time —
    // the phase is all there is — so the snap's mid-command gate had no idea the user was placing a
    // point, and the one pick whose purpose is to land on a corner of the geometry being blocked
    // snapped to nothing. `viewportSnapPickValid` is what the commit itself reads, so asserting it
    // here is asserting the base point can actually be aimed.
    IM_CHECK(s_cmd->objectSnapEnabled);
    IM_CHECK(s_cmd->viewportSnapPickValid);

    ctx->MouseClick(ImGuiMouseButton_Left);
    ctx->Yield(6);

    // The click must leave the base-point phase: either the block is created outright (name and
    // selection are both valid here, so this is the path taken) or the dialog reopens.
    DevShell_Logf("test", "after click: phase=%d dialogOpen=%d defs=%d",
                  static_cast<int>(s_cmd->blockCreatePhase), s_cmd->blockCreateDialogOpen ? 1 : 0,
                  static_cast<int>(s_cmd->blockDefs.size()));
    IM_CHECK(s_cmd->blockCreatePhase != AppCommandState::BlockCreatePhase::WaitBasePoint);
    IM_CHECK_EQ(static_cast<int>(s_cmd->blockDefs.size()), defsBefore + 1);
    // ASCII needle on purpose: the log line joins a `—` em dash, and how a literal one in this
    // file encodes is an MSVC source-charset question, not something this assertion is about.
    IM_CHECK(CadLogHas("created \"PICKBASE\""));
    IM_CHECK_EQ(s_cmd->blockDefs.back().content.solids.size(), static_cast<std::size_t>(1));
    // "Convert to block" is the dialog's default (blockCreateConvertMode == 1): the source geometry
    // is replaced by the reference. Until 2026-09-24 `EraseSelectedSources` had no Solid branch, so
    // the original solid survived alongside a block reference holding a copy of it.
    DevShell_Logf("test", "after create: cadSolids=%d blockRefs=%d",
                  static_cast<int>(s_cmd->cadSolids.size()),
                  static_cast<int>(s_cmd->cadBlockRefs.size()));
    IM_CHECK_EQ(s_cmd->cadSolids.size(), static_cast<std::size_t>(0));

    IM_CHECK(CancelToIdle(ctx));
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
        {"Survey", kRibbonTabSurvey},     {"Modeling", kRibbonTabModeling},
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

  // Open a DWG and capture the 3D viewport, shaded, from several orientations — the loop that
  // TASK-272 needed and did not have: three separate attempts at that bug each ended up asking a
  // human to eyeball the live app after every rebuild, and the one that finally found the cause
  // (a depth-buffer precision fight, not the tessellation) did it by looking at these captures.
  //
  //   set GOSURVEY_T272_DWG=C:\path\to\part.dwg
  //   build\devshell\GoSurvey.exe --devshell-run dwg-shaded-shots
  //
  // BMPs land next to the executable. Aimed at the part named by GOSURVEY_T272_CENTER (world
  // "x,y,z"), or at the drawing extents when that is unset.
  // REQ-354 / D-2026-09-28-j (GitHub issue #564 section 5): the dynamic input re-lays itself as the
  // user types. The headless transcripts drive the MODEL; this drives the real boxes — keystrokes into
  // the focused ImGui field, the callback that consumes `@` / `<` / `,`, the focus hand-off to the next
  // box, Backspace undoing a mode character — and checks the geometry that lands.
  ImGuiTest* dynModes = IM_REGISTER_TEST(engine, "gosurvey", "req354-dyninput-modes");
  dynModes->TestFunc = [](ImGuiTestContext* ctx) {
    IM_CHECK(CancelToIdle(ctx));
    IM_CHECK(OpenFreshDrawing(ctx));
    float ox = 0.f, oy = 0.f, sw = 0.f, sh = 0.f;
    IM_CHECK(DevShell_ViewportRect(&ox, &oy, &sw, &sh));
    // Low and to the left: clear of the Developer Shell, which floats over the middle of the viewport.
    ctx->MouseMoveToPos(ImVec2(ox + sw * 0.12f, oy + sh * 0.85f));
    ctx->Yield(2);
    SubmitCad(ctx, "LINE");
    ctx->Yield(6);
    const auto type = [ctx](const char* chars) {
      ctx->KeyChars(chars);
      ctx->Yield(3);
    };
    const auto enter = [ctx]() {
      ctx->KeyPress(ImGuiKey_Enter);
      ctx->Yield(6);
    };
    const auto lineEnd = [](std::size_t seg, double x, double y) {
      const std::vector<double>& f = s_cmd->userLinesFlat;
      const bool ok = f.size() >= (seg + 1) * 6 && std::abs(f[seg * 6 + 3] - x) < 1e-3 &&
                      std::abs(f[seg * 6 + 4] - y) < 1e-3;
      if (!ok && f.size() >= (seg + 1) * 6)
        DevShell_Logf("test", "segment %d ends at %.4f,%.4f", static_cast<int>(seg), f[seg * 6 + 3], f[seg * 6 + 4]);
      return ok;
    };

    type("1,2");  // X, then the comma moves on to Y
    enter();
    IM_CHECK(s_cmd->linePhase == AppCommandState::LinePhase::NeedNextPoint);
    type("@3,4");  // `@` switches LINE's Distance < Angle to dX / dY
    enter();
    IM_CHECK(lineEnd(0, 4.0, 6.0));
    type("5<180");  // distance 5, bearing 180 (south)
    enter();
    IM_CHECK(lineEnd(1, 4.0, 1.0));
    IM_CHECK(CancelToIdle(ctx));

    // Backspace takes a mode character back. At a FIRST point (X / Y), `7<` turns the boxes into
    // Distance < Angle; Backspace in the empty Angle box returns to X / Y with the 7 kept, and `,8`
    // then finishes an absolute point.
    SubmitCad(ctx, "LINE");
    ctx->Yield(6);
    type("7<");
    ctx->KeyPress(ImGuiKey_Backspace);
    ctx->Yield(4);
    type(",8");
    enter();
    const bool anchored = s_cmd->linePhase == AppCommandState::LinePhase::NeedNextPoint &&
                          std::abs(s_cmd->anchorX - 7.f) < 1e-3f && std::abs(s_cmd->anchorY - 8.f) < 1e-3f;
    if (!anchored) {
      const std::vector<std::string>* log = DevShell_CommandLog();
      for (std::size_t i = log && log->size() > 4 ? log->size() - 4 : 0; log && i < log->size(); ++i)
        DevShell_Logf("test", "log: %s", (*log)[i].c_str());
      DevShell_Logf("test", "anchor %.4f,%.4f phase %d", s_cmd->anchorX, s_cmd->anchorY,
                    static_cast<int>(s_cmd->linePhase));
    }
    IM_CHECK(anchored);
    IM_CHECK(CancelToIdle(ctx));
  };

  // REQ-355 (issue #564 section 8): the Modeling tab's buttons clicked for real — a primitive, a
  // second button while the first runs (it must cancel and start), the size dropdown, and the
  // ribbon PIPERUN going straight to its start point. Run with:
  //   GoSurvey.exe --devshell-run req355-modeling-ribbon
  ImGuiTest* modeling = IM_REGISTER_TEST(engine, "gosurvey", "req355-modeling-ribbon");
  modeling->TestFunc = [](ImGuiTestContext* ctx) {
    IM_CHECK(CancelToIdle(ctx));
    IM_CHECK(OpenFreshDrawing(ctx));  // the ribbon is inert on the Start tab (REQ-308)
    DevShell_SetWindowSize(2560, 1300);  // wide enough that no section collapses to a flyout
    ctx->Yield(6);
    IM_CHECK(RefWindow(ctx, "//GoSurveyHost/RibbonStrip"));
    ctx->ItemClick("Modeling");
    ctx->Yield(6);
    IM_CHECK(s_cmd->activeRibbonTab == kRibbonTabModeling);
    DevShell_RequestScreenshot("req355-modeling-tab.bmp");
    ctx->Yield(3);

    IM_CHECK(RefWindow(ctx, "//GoSurveyHost/RibbonStrip/RibbonToolsLeft/RibbonSecModPrimitives"));
    ctx->ItemClick("##RibbonLayout_##ModBox");  // DrawSection prefixes every id
    ctx->Yield();
    IM_CHECK(s_cmd->active == AppCommandState::Kind::Solid);
    IM_CHECK(RefWindow(ctx, "//GoSurveyHost/RibbonStrip/RibbonToolsLeft/RibbonSecModBooleans"));
    ctx->ItemClick("##RibbonLayout_##ModUnion");  // BOX is running: the click cancels it and starts UNION
    ctx->Yield();
    IM_CHECK(s_cmd->active == AppCommandState::Kind::Boolean);
    IM_CHECK(CancelToIdle(ctx));

    IM_CHECK(RefWindow(ctx, "//GoSurveyHost/RibbonStrip/RibbonToolsLeft/RibbonSecModPiping"));
    ctx->ComboClick("##ModPipeSize/6in");
    ctx->Yield();
    IM_CHECK(s_cmd->pipeRunNominalSize == "6in");
    ctx->ItemClick("##RibbonLayout_##ModPipeRun");
    ctx->Yield(2);
    IM_CHECK(s_cmd->active == AppCommandState::Kind::PipeRun);
    IM_CHECK(s_cmd->pipeRunPhase == AppCommandState::PipeRunPhase::WaitFirstPoint);
    IM_CHECK(std::fabs(s_cmd->pipeRunWallThicknessIn - 0.280) < 1e-9);
    DevShell_RequestScreenshot("req355-modeling-piperun.bmp");
    ctx->Yield(3);
    IM_CHECK(CancelToIdle(ctx));
    s_cmd->pipeRunNominalSize = "4in";
    IM_CHECK(ClickHomeTab(ctx));
  };

  // REQ-356 (GitHub issue #575) — the ribbon colour combo and Match Properties, through the real
  // ribbon. Unit tests call CadRibbonPickColor directly; only this reaches the combo's ids and shows
  // whether a second row fits in the Layers strip.
  //   GoSurvey.exe --devshell-run req356-ribbon-color
  ImGuiTest* ribbonColor = IM_REGISTER_TEST(engine, "gosurvey", "req356-ribbon-color");
  ribbonColor->TestFunc = [](ImGuiTestContext* ctx) {
    // Opens the combo and clicks one colour. Not ComboClick: the combo's id carries its PushID scope
    // ("RibbonColorCombo/..."), and ComboClick splits the path at the FIRST '/'. Each item is under
    // its own PushID (the colour's storage string), as ComboClick's `**` would otherwise find.
    const auto pickColor = [ctx](const char* storage, const char* label) {
      IM_CHECK_NO_RET(RefWindow(ctx, "//GoSurveyHost/RibbonStrip/RibbonLayerStrip"));
      ctx->ItemClick("RibbonColorCombo/##ribboncolorpick");
      ImGuiWindow* popup = ctx->GetWindowByRef("//$FOCUSED");
      IM_CHECK_NO_RET(popup != nullptr);
      if (popup)
        ctx->ItemClick((std::string("//") + popup->Name + "/" + storage + "/" + label).c_str());
      ctx->Yield(2);
    };
    IM_CHECK(CancelToIdle(ctx));
    IM_CHECK(OpenFreshDrawing(ctx));
    IM_CHECK(ClickHomeTab(ctx));
    ctx->Yield(6);
    ClearCadSelection(*s_cmd);

    // Nothing selected: the pick sets the current colour, and a new line is drawn in it.
    pickColor("ACI:1", "Red");
    IM_CHECK(s_cmd->currentColor == CadColorStorageFromAci(1));
    SubmitCad(ctx, "LINE");
    SubmitCad(ctx, "0,0");
    SubmitCad(ctx, "100,50");
    IM_CHECK(CancelToIdle(ctx));
    IM_CHECK(!s_cmd->userLineAttrs.empty());
    IM_CHECK(s_cmd->userLineAttrs.back().color == CadColorStorageFromAci(1));
    DevShell_RequestScreenshot("req356-ribbon-current-red.bmp");
    ctx->Yield(3);

    // With the line selected the combo recolours it and leaves the current colour alone.
    SelectedEntity e;
    e.type = SelectedEntity::Type::LineSeg;
    e.index = static_cast<int>(s_cmd->userLineAttrs.size()) - 1;
    s_cmd->selection = {e};
    ctx->Yield(2);
    pickColor("ACI:5", "Blue");
    IM_CHECK(s_cmd->userLineAttrs.back().color == CadColorStorageFromAci(5));
    IM_CHECK(s_cmd->currentColor == CadColorStorageFromAci(1));
    DevShell_RequestScreenshot("req356-ribbon-selection-blue.bmp");
    ctx->Yield(3);

    // Match Properties is a real button now.
    ClearCadSelection(*s_cmd);
    IM_CHECK(RefWindow(ctx, "//GoSurveyHost/RibbonStrip/RibbonToolsLeft/RibbonSecClipboard"));
    ctx->ItemClick("##RibbonLayout_##ClipMatchProps");
    ctx->Yield(2);
    IM_CHECK(s_cmd->active == AppCommandState::Kind::MatchProp);
    IM_CHECK(CancelToIdle(ctx));
    s_cmd->currentColor = "ByLayer";
  };

  ImGuiTest* t272 = IM_REGISTER_TEST(engine, "gosurvey", "dwg-shaded-shots");
  t272->TestFunc = [](ImGuiTestContext* ctx) {
    const char* dwg = std::getenv("GOSURVEY_T272_DWG");
    if (!dwg || !*dwg) {
      DevShell_Logf("dwg-shots", "set GOSURVEY_T272_DWG to the .dwg to capture; nothing to do");
      return;
    }
    IM_CHECK(CancelToIdle(ctx));
    s_cmd->showToolspaceWindow = false;
    ctx->WindowCollapse("//Developer Shell", true);
    DevShell_SetWindowSize(1800, 1300);
    ctx->Yield(6);

    std::vector<std::string>* log = DevShell_CommandLog();
    IM_CHECK(log != nullptr);
    OpenDrawingInNewTab(*s_cmd, *log, dwg);
    ctx->Yield(30);
    IM_CHECK(s_cmd->activeDrawingIdx != 0);

    SubmitCad(ctx, "ZOOMEXTENTS");
    ctx->Yield(10);

    // Aim at one part rather than the whole drawing when asked: a z-fighting artifact only shows on
    // a part filling a good share of the frame, and ZOOM EXTENTS on a pipe RUN leaves its flange a
    // hundred pixels wide.
    float cx = s_cmd->viewportPanX, cy = s_cmd->viewportPanY, cz = s_cmd->viewportPanZ;
    float halfH = 50.f / std::max(s_cmd->viewportZoom, 1.e-9f);
    if (const char* c = std::getenv("GOSURVEY_T272_CENTER")) {
      float px = 0.f, py = 0.f, pz = 0.f, ph = 0.f;
      const int n = std::sscanf(c, "%f,%f,%f,%f", &px, &py, &pz, &ph);
      if (n >= 3) {
        cx = px; cy = py; cz = pz;
        if (n >= 4 && ph > 0.f)
          halfH = ph;
      }
    }
    DevShell_Logf("dwg-shots", "framing (%.4f,%.4f,%.4f) halfH=%.4f  solids=%d blockrefs=%d", (double)cx,
                  (double)cy, (double)cz, (double)halfH, (int)s_cmd->cadSolids.size(),
                  (int)s_cmd->cadBlockRefs.size());

    struct Shot { const char* name; const char* style; float az; float el; float halfHMul; };
    const Shot shots[] = {
        {"1-az0", "SHADED", 0.f, 0.f, 1.f},
        {"2-az90", "SHADED", 90.f, 0.f, 1.f},
        {"3-az180", "SHADED", 180.f, 0.f, 1.f},
        {"4-az270", "SHADED", 270.f, 0.f, 1.f},
        {"5-az270-tilt", "SHADED", 270.f, 20.f, 1.f},
        {"6-az90-tilt", "SHADED", 90.f, 20.f, 1.f},
        {"7-az270-close", "SHADED", 270.f, 10.f, 0.4f},
        {"8-az90-close", "SHADED", 90.f, 10.f, 0.4f},
    };
    for (const Shot& s : shots) {
      char cmd[64];
      std::snprintf(cmd, sizeof(cmd), "VISUALSTYLE %s", s.style);
      SubmitCad(ctx, cmd);
      s_cmd->viewportAzimuthDeg = s.az;
      s_cmd->viewportElevationDeg = s.el;
      s_cmd->viewportPanX = cx;
      s_cmd->viewportPanY = cy;
      s_cmd->viewportPanZ = cz;
      s_cmd->viewportZoom = 50.f / (halfH * s.halfHMul);
      ctx->Yield(10);
      char path[96];
      std::snprintf(path, sizeof(path), "dwgshot-%s.bmp", s.name);
      DevShell_RequestViewportCapture(path, 1600);
      ctx->Yield(4);
    }
    ctx->WindowCollapse("//Developer Shell", false);
  };

  // --- REQ-358: the Drawing Settings Zone group, driven through the REAL window ------------------
  // The Zone group's selection logic lives in the UI (a unit test cannot reach it): typing a known
  // code selects it, an unknown one is refused and changes nothing, picking a category selects its
  // first system, No Datum clears the zone. Apply writes it to the drawing.
  //   build\devshell\GoSurvey.exe --devshell-run req358-zone-group
  // GOSURVEY_REQ358_HOLD=<seconds> keeps the window open at the end, for a desktop screenshot.
  ImGuiTest* zone = IM_REGISTER_TEST(engine, "gosurvey", "req358-zone-group");
  zone->TestFunc = [](ImGuiTestContext* ctx) {
    IM_CHECK(CancelToIdle(ctx));
    IM_CHECK(OpenFreshDrawing(ctx));
    IM_CHECK(s_cmd->drawingSettings.zoneCode.empty());
    SubmitCad(ctx, "DRAWINGSETTINGS");
    ctx->Yield(4);
    IM_CHECK(s_cmd->showDrawingSettingsWindow);
    ImGuiWindow* const dialog = ctx->WindowInfo("//$FOCUSED").Window;  // the modal, pinned
    IM_CHECK(dialog != nullptr);
    ctx->SetRef(dialog);

    const auto typeCode = [ctx](const char* code) {
      ctx->ItemClick("**/##ds_code");
      ctx->KeyCharsReplaceEnter(code);
      ctx->Yield(2);
      ctx->ItemClick("**/Apply");
      ctx->Yield(2);
    };
    typeCode("TX83-CF");
    IM_CHECK_STR_EQ(s_cmd->drawingSettings.zoneCode.c_str(), "TX83-CF");
    typeCode("NOSUCH");  // refused: the zone stays
    IM_CHECK_STR_EQ(s_cmd->drawingSettings.zoneCode.c_str(), "TX83-CF");
    typeCode("HARN/TX.TX-C");
    IM_CHECK_STR_EQ(s_cmd->drawingSettings.zoneCode.c_str(), "HARN/TX.TX-C");

    // A category selects its first system. BeginCombo does not report its label to the test engine,
    // so the combo is reached by ID, from the parent the code field (an InputText, which does) shares.
    const ImGuiTestItemInfo codeInfo = ctx->ItemInfo("**/##ds_code");
    IM_CHECK(codeInfo.ID != 0);
    ctx->ItemClick(ImHashStr("##ds_category", 0, codeInfo.ParentID));
    ctx->Yield(2);
    ctx->SetRef("//$FOCUSED");
    ctx->ItemClick("**/Lat Longs");
    ctx->Yield(2);
    ctx->SetRef(dialog);
    ctx->ItemClick("**/Apply");
    ctx->Yield(2);
    IM_CHECK(!s_cmd->drawingSettings.zoneCode.empty());
    IM_CHECK(s_cmd->drawingSettings.zoneCode != "HARN/TX.TX-C");

    typeCode("HARN/TX.TX-CF");  // left open on a Texas zone for the screenshot
    IM_CHECK_STR_EQ(s_cmd->drawingSettings.zoneCode.c_str(), "HARN/TX.TX-CF");
    if (const char* hold = std::getenv("GOSURVEY_REQ358_HOLD"))
      ctx->SleepNoSkip(static_cast<float>(std::atof(hold)), 0.1f);

    typeCode(".");  // No Datum, No Projection
    IM_CHECK(s_cmd->drawingSettings.zoneCode.empty());
    ctx->ItemClick("**/Cancel");
    ctx->Yield(2);
    IM_CHECK(!s_cmd->showDrawingSettingsWindow);
  };

  // --- REQ-360: the Transformation tab, through the REAL window --------------------------------------
  // Every control waits for "Apply transform settings"; a Pick button hides the window, the pick
  // (typed here) comes back into it, and Apply writes the transform to the drawing.
  //   build\devshell\GoSurvey.exe --devshell-run req360-transformation-tab
  // GOSURVEY_REQ360_HOLD=<seconds> keeps the window open on the tab, for a desktop screenshot.
  ImGuiTest* transformation = IM_REGISTER_TEST(engine, "gosurvey", "req360-transformation-tab");
  transformation->TestFunc = [](ImGuiTestContext* ctx) {
    IM_CHECK(CancelToIdle(ctx));
    IM_CHECK(OpenFreshDrawing(ctx));
    SubmitCad(ctx, "DRAWINGSETTINGS");
    ctx->Yield(4);
    IM_CHECK(s_cmd->showDrawingSettingsWindow);
    ImGuiWindow* dialog = ctx->WindowInfo("//$FOCUSED").Window;
    IM_CHECK(dialog != nullptr);
    ctx->SetRef(dialog);
    ctx->ItemClick("**/##ds_code");
    ctx->KeyCharsReplaceEnter("HARN/TX.TX-CF");
    ctx->Yield(2);

    ctx->ItemClick("**/Transformation");
    ctx->Yield(2);
    const auto disabled = [ctx](const char* ref) {
      return (ctx->ItemInfo(ref).ItemFlags & ImGuiItemFlags_Disabled) != 0;
    };
    IM_CHECK(!disabled("**/Apply transform settings"));
    for (const char* ref : {"**/Apply sea level scale factor", "**/Rotation point", "**/Pick Reference Point",
                            "**/##elev"})
      IM_CHECK(disabled(ref));
    ctx->ItemClick("**/Apply transform settings");
    ctx->Yield(2);
    for (const char* ref : {"**/Apply sea level scale factor", "**/Rotation point", "**/Pick Reference Point"})
      IM_CHECK(!disabled(ref));
    IM_CHECK(disabled("**/##elev"));  // until the sea level factor is on

    // Esc during a pick returns to the window; that Esc does not close the window too.
    ctx->ItemClick("**/Pick Reference Point");
    ctx->Yield(3);
    IM_CHECK(s_cmd->active == AppCommandState::Kind::DrawingSettingsPick);
    ctx->KeyPress(ImGuiKey_Escape);
    ctx->Yield(4);
    IM_CHECK(s_cmd->active == AppCommandState::Kind::None);
    IM_CHECK(s_cmd->showDrawingSettingsWindow);
    dialog = ctx->WindowInfo("//$FOCUSED").Window;
    IM_CHECK(dialog != nullptr);
    ctx->SetRef(dialog);

    // Pick Reference Point: the window hides, a typed point answers, the window comes back.
    ctx->ItemClick("**/Pick Reference Point");
    ctx->Yield(3);
    IM_CHECK(s_cmd->active == AppCommandState::Kind::DrawingSettingsPick);
    SubmitCad(ctx, "1000,2000");
    ctx->Yield(4);
    IM_CHECK(s_cmd->active == AppCommandState::Kind::None);
    IM_CHECK(s_cmd->showDrawingSettingsWindow);
    dialog = ctx->WindowInfo("//$FOCUSED").Window;
    IM_CHECK(dialog != nullptr);
    ctx->SetRef(dialog);
    if (const char* hold = std::getenv("GOSURVEY_REQ360_HOLD"))
      ctx->SleepNoSkip(static_cast<float>(std::atof(hold)), 0.1f);
    ctx->ItemClick("**/Apply");
    ctx->Yield(2);
    const DrawingSettings::Transform& t = s_cmd->drawingSettings.transform;
    IM_CHECK(t.apply);
    IM_CHECK(std::fabs(t.refLocalX - 1000.0) < 1e-3);
    IM_CHECK(std::fabs(t.refLocalY - 2000.0) < 1e-3);
    IM_CHECK(std::fabs(t.refGridE - 1000.0) < 1e-3);  // seeded: the point in zone units

    ctx->ItemClick("**/Cancel");
    ctx->Yield(2);
    IM_CHECK(!s_cmd->showDrawingSettingsWindow);
  };

  // --- REQ-361: the Object Layers tab, and a Locked row in a creation dialog -----------------------------
  // The tab shows its info line and the disabled display-components checkbox; the Surface row's
  // padlock, applied, makes Create Surface's layer read-only and set to the row's layer.
  //   build\devshell\GoSurvey.exe --devshell-run req361-object-layers
  // GOSURVEY_REQ361_HOLD=<seconds> keeps the window open on the tab, for a desktop screenshot.
  ImGuiTest* objectLayers = IM_REGISTER_TEST(engine, "gosurvey", "req361-object-layers");
  objectLayers->TestFunc = [](ImGuiTestContext* ctx) {
    IM_CHECK(CancelToIdle(ctx));
    IM_CHECK(OpenFreshDrawing(ctx));
    SubmitCad(ctx, "DRAWINGSETTINGS");
    ctx->Yield(4);
    ImGuiWindow* dialog = ctx->WindowInfo("//$FOCUSED").Window;
    IM_CHECK(dialog != nullptr);
    ctx->SetRef(dialog);
    ctx->ItemClick("**/Object Layers");
    ctx->Yield(2);
    // "\\/": the test engine reads '/' as a path separator.
    const ImGuiTestItemInfo display =
        ctx->ItemInfo("**/Immediate and independent layer on\\/off control of display components");
    IM_CHECK(display.ID != 0);
    IM_CHECK((display.ItemFlags & ImGuiItemFlags_Disabled) != 0);
    if (const char* hold = std::getenv("GOSURVEY_REQ361_HOLD"))
      ctx->SleepNoSkip(static_cast<float>(std::atof(hold)), 0.1f);
    const int surfaceRow = static_cast<int>(ObjectLayerKind::Surface);
    IM_CHECK(!s_cmd->drawingSettings.ObjectLayer(ObjectLayerKind::Surface).locked);
    ctx->ItemClick(("**/$$" + std::to_string(surfaceRow) + "/##lock").c_str());
    ctx->ItemClick("**/OK");
    ctx->Yield(2);
    IM_CHECK(!s_cmd->showDrawingSettingsWindow);
    IM_CHECK(s_cmd->drawingSettings.ObjectLayer(ObjectLayerKind::Surface).locked);

    s_cmd->showCreateSurfaceWindow = true;
    ctx->Yield(4);
    ImGuiWindow* create = ctx->WindowInfo("//Create Surface").Window;
    IM_CHECK(create != nullptr);
    const ImGuiTestItemInfo layer = ctx->ItemInfo(ImHashStr("##cslayer", 0, create->ID));
    IM_CHECK(layer.ID != 0);
    IM_CHECK((layer.ItemFlags & ImGuiItemFlags_Disabled) != 0);
    s_cmd->showCreateSurfaceWindow = false;
    ctx->Yield(2);
  };

  // REQ-359 (GitHub issue #582 increment 3): the contextual Geolocation tab in the real ribbon.
  // Appears with a zone without taking focus; Map / Capture Area present but disabled; Edit Location
  // opens Drawing Settings; Mark Position places a Position Marker (screenshot); Remove Location asks,
  // then hides the tab; one UNDO brings it back.
  //   build\devshell\GoSurvey.exe --devshell-run req359-geolocation-tab
  // GOSURVEY_REQ359_HOLD=<seconds> keeps the window open on the marker, for a desktop screenshot.
  ImGuiTest* geoTab = IM_REGISTER_TEST(engine, "gosurvey", "req359-geolocation-tab");
  geoTab->TestFunc = [](ImGuiTestContext* ctx) {
    IM_CHECK(CancelToIdle(ctx));
    IM_CHECK(OpenFreshDrawing(ctx));
    IM_CHECK(ClickHomeTab(ctx));
    IM_CHECK(RefWindow(ctx, "//GoSurveyHost/RibbonStrip"));
    IM_CHECK(!ctx->ItemExists("Geolocation"));

    std::vector<std::string> log;
    DrawingSettings s = s_cmd->drawingSettings;
    s.zoneCode = "HARN/TX.TX-CF";
    IM_CHECK(ApplyDrawingSettings(*s_cmd, 2, s_cmd->modelUnitsPerPlottedInch, s, log));
    ctx->Yield(3);
    IM_CHECK(ctx->ItemExists("Geolocation"));
    IM_CHECK_EQ(s_cmd->activeRibbonTab, kRibbonTabHome);  // it does not steal focus
    ctx->ItemClick("Geolocation");
    ctx->Yield(3);
    IM_CHECK_EQ(s_cmd->activeRibbonTab, kRibbonTabGeolocationCtx);

    IM_CHECK(RefWindow(ctx, "//GoSurveyHost/RibbonStrip/RibbonToolsLeft/RibbonSecGeoOnlineMap"));
    const ImGuiTestItemInfo capture = ctx->ItemInfo("##GeoCaptureArea");
    IM_CHECK(capture.ID != 0);
    IM_CHECK((capture.ItemFlags & ImGuiItemFlags_Disabled) != 0);

    // Edit Location's icon half opens Drawing Settings (Units and Zone).
    IM_CHECK(RefWindow(ctx, "//GoSurveyHost/RibbonStrip/RibbonToolsLeft/RibbonSecGeoLocation"));
    const ImGuiTestItemInfo edit = ctx->ItemInfo("##GeoEditLocation");
    IM_CHECK(edit.ID != 0);
    ctx->MouseMoveToPos(ImVec2(edit.RectFull.GetCenter().x, edit.RectFull.Min.y + 12.f));
    ctx->MouseClick();
    ctx->Yield(4);
    IM_CHECK(s_cmd->showDrawingSettingsWindow);
    ctx->SetRef("//$FOCUSED");
    ctx->ItemClick("**/Cancel");
    ctx->Yield(3);
    IM_CHECK(!s_cmd->showDrawingSettingsWindow);

    // Mark Position > Lat-Long at NGS AG9976, then the MTEXT editor on its label.
    SubmitCad(ctx, "GEOMARKLATLONG");
    SubmitCad(ctx, "30 17 10.51249N");
    SubmitCad(ctx, "97 44 21.71739W");
    ctx->Yield(3);
    IM_CHECK_EQ(static_cast<int>(s_cmd->cadPositionMarkers.size()), 1);
    IM_CHECK(s_cmd->mtextRichEditorOpen);
    IM_CHECK_EQ(s_cmd->mtextRichEditorMarkerIndex, 0);
    ctx->Yield(6);
    DevShell_RequestViewportCapture("devshell-req359-marker-editing.bmp", 1400);
    ctx->Yield(4);
    CommitMtextRichEditor(*s_cmd, log);
    SubmitCad(ctx, "ZOOMEXTENTS");
    ctx->Yield(6);
    DevShell_RequestViewportCapture("devshell-req359-marker.bmp", 1400);
    ctx->Yield(4);
    if (const char* hold = std::getenv("GOSURVEY_REQ359_HOLD"))
      ctx->SleepNoSkip(static_cast<float>(std::atof(hold)), 0.1f);

    // Remove Location asks first; confirming clears the zone and the tab goes away.
    IM_CHECK(RefWindow(ctx, "//GoSurveyHost/RibbonStrip/RibbonToolsLeft/RibbonSecGeoLocation"));
    ctx->ItemClick("##GeoRemoveLocation");
    ctx->Yield(3);
    IM_CHECK_STR_EQ(s_cmd->drawingSettings.zoneCode.c_str(), "HARN/TX.TX-CF");  // not yet: it asks
    ctx->SetRef("//$FOCUSED");
    ctx->ItemClick("**/Remove");
    ctx->Yield(3);
    IM_CHECK(s_cmd->drawingSettings.zoneCode.empty());
    IM_CHECK_EQ(s_cmd->activeRibbonTab, kRibbonTabHome);
    IM_CHECK(RefWindow(ctx, "//GoSurveyHost/RibbonStrip"));
    IM_CHECK(!ctx->ItemExists("Geolocation"));
    SubmitCad(ctx, "UNDO");
    ctx->Yield(3);
    IM_CHECK_STR_EQ(s_cmd->drawingSettings.zoneCode.c_str(), "HARN/TX.TX-CF");
    IM_CHECK(ctx->ItemExists("Geolocation"));
  };

  // REQ-363 (GitHub issue #583 increment 1): the Map dropdown in the real ribbon, with the REAL USGS
  // service. A 40 ft circle is drawn round NGS AG9976 (the University of Texas Tower); USGS Imagery is
  // picked from the menu; the viewport is captured once the tiles have landed, so the tower can be
  // seen inside the circle. Needs the internet.
  //   build\devshell\GoSurvey.exe --devshell-run req363-online-map
  ImGuiTest* onlineMap = IM_REGISTER_TEST(engine, "gosurvey", "req363-online-map");
  onlineMap->TestFunc = [](ImGuiTestContext* ctx) {
    IM_CHECK(CancelToIdle(ctx));
    IM_CHECK(OpenFreshDrawing(ctx));
    ctx->WindowCollapse("//Developer Shell", true);
    DevShell_SetWindowSize(1800, 1200);
    ctx->Yield(4);

    // A feet drawing in Texas Central, stored about a state-plane origin (REQ-101).
    constexpr double kE = 3115243.14, kN = 10077391.26, kOx = 3115000.0, kOy = 10077000.0;
    std::vector<std::string> log;
    s_cmd->worldDocumentOriginX = kOx;
    s_cmd->worldDocumentOriginY = kOy;
    DrawingSettings s = s_cmd->drawingSettings;
    s.zoneCode = "HARN/TX.TX-CF";
    IM_CHECK(ApplyDrawingSettings(*s_cmd, 2, s_cmd->modelUnitsPerPlottedInch, s, log));
    SubmitCad(ctx, "CIRCLE");
    SubmitCad(ctx, "3115243.14,10077391.26");  // typed X,Y are WORLD coordinates
    SubmitCad(ctx, "40");
    ctx->Yield(3);
    IM_CHECK_EQ(static_cast<int>(s_cmd->userCirclesCxCyZR.size()), 4);
    s_cmd->viewportPanX = static_cast<float>(kE - kOx);
    s_cmd->viewportPanY = static_cast<float>(kN - kOy);
    s_cmd->viewportZoom = 50.f / 250.f;  // 500 ft tall
    ctx->Yield(3);

    IM_CHECK(RefWindow(ctx, "//GoSurveyHost/RibbonStrip"));
    ctx->ItemClick("Geolocation");
    ctx->Yield(3);
    IM_CHECK(RefWindow(ctx, "//GoSurveyHost/RibbonStrip/RibbonToolsLeft/RibbonSecGeoOnlineMap"));
    IM_CHECK(!s_cmd->onlineMapDrawing);
    ctx->ItemClick("##GeoMap");
    ctx->Yield(3);
    ctx->SetRef("//$FOCUSED");
    ctx->ItemClick("usgsImagery/##row");
    ctx->Yield(3);
    IM_CHECK(s_cmd->drawingSettings.onlineMap == DrawingSettings::OnlineMap::UsgsImagery);

    // Let the tiles arrive (background fetch), then capture. Waited on the WALL clock: the Test
    // Engine's own sleeps run in its fast-forwarded time, far shorter than a download.
    const auto waitReal = [ctx](double seconds) {
      const auto until = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
      while (std::chrono::steady_clock::now() < until)
        ctx->Yield();
    };
    waitReal(4.0);
    IM_CHECK(s_cmd->onlineMapDrawing);
    DevShell_RequestViewportCapture("devshell-req363-usgs-imagery.bmp", 1600);
    ctx->Yield(4);
    s_cmd->viewportZoom = 50.f / 3000.f;  // zoomed out: many more tiles
    waitReal(4.0);
    DevShell_RequestViewportCapture("devshell-req363-usgs-imagery-wide.bmp", 1600);
    ctx->Yield(4);

    // REQ-363 item 5 / REQ-100: pan and orbit over new ground for 600 frames, so tiles keep streaming
    // in, and record the wall-clock frame time. p95 must hold 60 FPS.
    {
      std::vector<double> frameMs;
      const float x0 = s_cmd->viewportPanX, y0 = s_cmd->viewportPanY;
      for (int i = 0; i < 600; ++i) {
        s_cmd->viewportPanX = x0 + 40.f * static_cast<float>(i);  // 24,000 ft east over the run
        s_cmd->viewportPanY = y0 + 15.f * static_cast<float>(i);
        s_cmd->viewportAzimuthDeg = 0.3f * static_cast<float>(i);
        s_cmd->viewportElevationDeg = 90.f - 0.05f * static_cast<float>(i);
        ctx->Yield();
        if (i >= 30)  // past the first frames of the new view
          frameMs.push_back(s_cmd->perfFrameMs);
      }
      std::sort(frameMs.begin(), frameMs.end());
      const double p95 = frameMs[frameMs.size() * 95 / 100];
      DevShell_Logf("req363", "pan+orbit with the map streaming: p50 %.2f ms, p95 %.2f ms, max %.2f ms (%d frames)",
                    frameMs[frameMs.size() / 2], p95, frameMs.back(), static_cast<int>(frameMs.size()));
      IM_CHECK(p95 <= 1000.0 / 60.0 + 0.5);  // 60 FPS, with the vsync interval's own jitter
      s_cmd->viewportAzimuthDeg = 0.f;
      s_cmd->viewportElevationDeg = 90.f;
      s_cmd->viewportPanX = x0;
      s_cmd->viewportPanY = y0;
    }

    // Map Off from the same menu: nothing drawn.
    IM_CHECK(RefWindow(ctx, "//GoSurveyHost/RibbonStrip/RibbonToolsLeft/RibbonSecGeoOnlineMap"));
    ctx->ItemClick("##GeoMap");
    ctx->Yield(3);
    ctx->SetRef("//$FOCUSED");
    ctx->ItemClick("off/##row");
    ctx->Yield(4);
    IM_CHECK(s_cmd->drawingSettings.onlineMap == DrawingSettings::OnlineMap::Off);
    IM_CHECK(!s_cmd->onlineMapDrawing);
    ctx->WindowCollapse("//Developer Shell", false);
  };

  // REQ-364 (GitHub issue #583 increment 2): Capture Area through the real split button, with the
  // live USGS service. The icon half keeps the visible map; with Map Off the captured area still
  // draws (screenshot); Pick Area from the menu with typed corners; Remove Captured Areas.
  //   build\devshell\GoSurvey.exe --devshell-run req364-capture-area
  ImGuiTest* capture = IM_REGISTER_TEST(engine, "gosurvey", "req364-capture-area");
  capture->TestFunc = [](ImGuiTestContext* ctx) {
    IM_CHECK(CancelToIdle(ctx));
    IM_CHECK(OpenFreshDrawing(ctx));
    ctx->WindowCollapse("//Developer Shell", true);
    DevShell_SetWindowSize(1800, 1200);
    ctx->Yield(4);
    const auto waitReal = [ctx](double seconds) {
      const auto until = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
      while (std::chrono::steady_clock::now() < until)
        ctx->Yield();
    };
    constexpr double kE = 3115243.14, kN = 10077391.26, kOx = 3115000.0, kOy = 10077000.0;
    std::vector<std::string> log;
    s_cmd->worldDocumentOriginX = kOx;
    s_cmd->worldDocumentOriginY = kOy;
    DrawingSettings s = s_cmd->drawingSettings;
    s.zoneCode = "HARN/TX.TX-CF";
    IM_CHECK(ApplyDrawingSettings(*s_cmd, 2, s_cmd->modelUnitsPerPlottedInch, s, log));
    s_cmd->viewportPanX = static_cast<float>(kE - kOx);
    s_cmd->viewportPanY = static_cast<float>(kN - kOy);
    s_cmd->viewportZoom = 50.f / 800.f;
    IM_CHECK(SetOnlineMap(*s_cmd, DrawingSettings::OnlineMap::UsgsImagery, log));
    waitReal(4.0);

    IM_CHECK(RefWindow(ctx, "//GoSurveyHost/RibbonStrip"));
    ctx->ItemClick("Geolocation");
    ctx->Yield(3);
    IM_CHECK(RefWindow(ctx, "//GoSurveyHost/RibbonStrip/RibbonToolsLeft/RibbonSecGeoOnlineMap"));
    ctx->ItemClick("##GeoCaptureArea");  // the icon half: the visible area
    ctx->Yield(2);
    for (int i = 0; i < 400 && s_cmd->active == AppCommandState::Kind::GeoCaptureArea; ++i)
      waitReal(0.025);
    IM_CHECK(s_cmd->active == AppCommandState::Kind::None);
    IM_CHECK_EQ(static_cast<int>(s_cmd->drawingSettings.capturedAreas.size()), 1);
    DevShell_Logf("req364", "captured %d tile(s) at level %d",
                  static_cast<int>(s_cmd->drawingSettings.capturedAreas[0].tiles.size()),
                  s_cmd->drawingSettings.capturedAreas[0].level);

    // Map Off: the captured area is still drawn (and credited).
    IM_CHECK(SetOnlineMap(*s_cmd, DrawingSettings::OnlineMap::Off, log));
    s_cmd->viewportZoom = 50.f / 1600.f;  // wider than the capture, so its edge shows
    waitReal(2.0);
    IM_CHECK(s_cmd->onlineMapDrawing);
    DevShell_RequestViewportCapture("devshell-req364-captured-map-off.bmp", 1600);
    ctx->Yield(4);

    // Pick Area from the menu (the label half), typed corners; then Remove Captured Areas.
    IM_CHECK(SetOnlineMap(*s_cmd, DrawingSettings::OnlineMap::UsgsTopo, log));
    s_cmd->viewportZoom = 50.f / 800.f;
    waitReal(2.0);
    IM_CHECK(RefWindow(ctx, "//GoSurveyHost/RibbonStrip/RibbonToolsLeft/RibbonSecGeoOnlineMap"));
    const ImGuiTestItemInfo cap = ctx->ItemInfo("##GeoCaptureArea");
    IM_CHECK(cap.ID != 0);
    ctx->MouseMoveToPos(ImVec2(cap.RectFull.GetCenter().x, cap.RectFull.Max.y - 6.f));
    ctx->MouseClick();
    ctx->Yield(3);
    ctx->SetRef("//$FOCUSED");
    ctx->ItemClick("Pick Area");
    ctx->Yield(3);
    IM_CHECK(s_cmd->active == AppCommandState::Kind::GeoCaptureArea);
    SubmitCad(ctx, "3115200,10077350");
    SubmitCad(ctx, "3115300,10077450");
    for (int i = 0; i < 400 && s_cmd->active == AppCommandState::Kind::GeoCaptureArea; ++i)
      waitReal(0.025);
    IM_CHECK_EQ(static_cast<int>(s_cmd->drawingSettings.capturedAreas.size()), 2);
    IM_CHECK(s_cmd->drawingSettings.capturedAreas[1].map == DrawingSettings::OnlineMap::UsgsTopo);

    IM_CHECK(RefWindow(ctx, "//GoSurveyHost/RibbonStrip/RibbonToolsLeft/RibbonSecGeoOnlineMap"));
    ctx->MouseMoveToPos(ImVec2(cap.RectFull.GetCenter().x, cap.RectFull.Max.y - 6.f));
    ctx->MouseClick();
    ctx->Yield(3);
    ctx->SetRef("//$FOCUSED");
    ctx->ItemClick("Remove Captured Areas");
    ctx->Yield(3);
    IM_CHECK(s_cmd->drawingSettings.capturedAreas.empty());
    SubmitCad(ctx, "UNDO");
    ctx->Yield(3);
    IM_CHECK_EQ(static_cast<int>(s_cmd->drawingSettings.capturedAreas.size()), 2);
    IM_CHECK(SetOnlineMap(*s_cmd, DrawingSettings::OnlineMap::Off, log));
    ctx->WindowCollapse("//Developer Shell", false);
  };
}

#endif
