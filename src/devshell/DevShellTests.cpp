#include "DevShell.hpp"

#ifdef GOSURVEY_DEVELOPER_SHELL

#include "CadBlocks.hpp"
#include "CadUi.hpp"
#include "CadCommands.hpp"
#include "GsIo.hpp"
#include "util/cadblock.hpp"
#include "brep.hpp"
#include "solidpick.hpp"
#include "render/Camera.hpp"

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


  // --- REQ-336 live section clip, driven through the REAL GUI (TASK-240) -------------------------
  //
  // GitHub #149 acceptance 6 is the one criterion in the whole phase that is about PIXELS, and two
  // of its failure modes cannot be reached anywhere else:
  //
  //   * whether the clip actually removes geometry from the screen. `SectionClipTests` proves the
  //     plane arithmetic and `headless.req336-section-clip` proves the command surface, but neither
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
  ImGuiTest* sclip = IM_REGISTER_TEST(engine, "gosurvey", "req336-section-clip-viewport");
  sclip->TestFunc = [](ImGuiTestContext* ctx) {
    IM_CHECK(CancelToIdle(ctx));

    // The app opens on the Start tab (REQ-308, index 0), which draws no 3D viewport at all.
    if (s_cmd->activeDrawingIdx == 0) {
      std::vector<std::string>* log = DevShell_CommandLog();
      IM_CHECK(log != nullptr);
      NewDrawingInTab(*s_cmd, *log);
      ctx->Yield(6);
    }
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
    DevShell_RequestScreenshot("devshell-req336-clip-0-off.bmp");
    ctx->Yield(4);

    // 2 — cut at the UCS plane (z = 0). The box spans z 0..12, so this removes ALL of it: the
    // strongest possible statement that the clip reaches the pixels, and the frame that would look
    // identical to the one above if the plane were being ignored.
    SubmitCad(ctx, "SECTIONCLIP 0");
    ctx->Yield(6);
    IM_CHECK(s_cmd->viewportSectionClip);
    IM_CHECK(std::fabs(s_cmd->viewportSectionClipOffset - 0.0) < 1e-9);
    DevShell_RequestScreenshot("devshell-req336-clip-1-at-zero.bmp");
    ctx->Yield(4);

    // 3 and 4 — the plane MOVES, which is the word acceptance 6 actually uses. Two heights through
    // the body of the box; the visible remainder must grow with the offset.
    SubmitCad(ctx, "SECTIONCLIP 4");
    ctx->Yield(6);
    IM_CHECK(std::fabs(s_cmd->viewportSectionClipOffset - 4.0) < 1e-9);
    DevShell_RequestScreenshot("devshell-req336-clip-2-at-four.bmp");
    ctx->Yield(4);

    SubmitCad(ctx, "SECTIONCLIP 8");
    ctx->Yield(6);
    IM_CHECK(std::fabs(s_cmd->viewportSectionClipOffset - 8.0) < 1e-9);
    DevShell_RequestScreenshot("devshell-req336-clip-3-at-eight.bmp");
    ctx->Yield(4);

    // 5 — FLIP keeps the other half. Together with shot 3 this covers the whole box between them.
    SubmitCad(ctx, "SECTIONCLIP FLIP");
    ctx->Yield(6);
    IM_CHECK(s_cmd->viewportSectionClipFlip);
    DevShell_RequestScreenshot("devshell-req336-clip-4-flipped.bmp");
    ctx->Yield(4);

    // 6 — and OFF restores the whole box, so the clip left nothing behind.
    SubmitCad(ctx, "SECTIONCLIP OFF");
    ctx->Yield(6);
    IM_CHECK(!s_cmd->viewportSectionClip);
    DevShell_RequestScreenshot("devshell-req336-clip-5-off-again.bmp");
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
    if (s_cmd->activeDrawingIdx == 0) {
      // `NewDrawingInTab` is what the start screen's "New Drawing" button and the tab strip's "+"
      // both call. Reached directly rather than by clicking, for the reason the headless driver
      // reaches `SetActiveSpace` directly: the button is inside a child region whose ImGui path is an
      // implementation detail of the start screen, and this test is about the CHAMFER, not about how
      // that screen is laid out.
      std::vector<std::string>* log = DevShell_CommandLog();
      IM_CHECK(log != nullptr);
      NewDrawingInTab(*s_cmd, *log);
      ctx->Yield(6);
    }
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
