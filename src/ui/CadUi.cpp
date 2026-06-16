#include "CadUi.hpp"
#include "CadCoordinateFrame.hpp"
#include "ViewportPickPolicy.hpp"
#include "MtextRichFormat.hpp"

#include "CadLinetype.hpp"
#include "NumFormat.hpp"
#include "DxfIo.hpp"
#include "AppIcon.hpp"
#include "GsIo.hpp"
#include "UserPrefs.hpp"
#include "ImGuiLayout.hpp"
#include "WinFileDialogs.hpp"
#include "SurveyPoints.hpp"
#include "StringUtil.hpp"
#include "imgui.h"

#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <set>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cfloat>
#include <cstdint>
#include <string>

static ImTextureID g_menuBarLogoTex{};
static ImVec2 g_menuBarLogoDims{};

void CadUiSetMenuBarLogo(ImTextureID texture, float widthPx, float heightPx) {
  g_menuBarLogoTex = texture;
  g_menuBarLogoDims = ImVec2(widthPx, heightPx);
}

void CadUiClearMenuBarLogo() {
  g_menuBarLogoTex = (ImTextureID)0;
  g_menuBarLogoDims = ImVec2(0.f, 0.f);
}

bool CadUiTitleBarLogoQuery(ImTextureID* outTexture, ImVec2* outDimsPx) {
  if (!outTexture || !outDimsPx)
    return false;
  if (!g_menuBarLogoTex || g_menuBarLogoDims.x <= 0.f || g_menuBarLogoDims.y <= 0.f)
    return false;
  *outTexture = g_menuBarLogoTex;
  *outDimsPx = g_menuBarLogoDims;
  return true;
}

namespace {

static int MtextRichEditorInputCallback(ImGuiInputTextCallbackData* data) {
  auto* cmd = static_cast<AppCommandState*>(data->UserData);
  if (!cmd)
    return 0;
  if (data->EventFlag == ImGuiInputTextFlags_CallbackAlways) {
    cmd->mtextRichEditorCursor = data->CursorPos;
    int a = data->SelectionStart;
    int b = data->SelectionEnd;
    if (a > b)
      std::swap(a, b);
    cmd->mtextRichEditorSelStart = a;
    cmd->mtextRichEditorSelEnd = b;
  }
  if (data->EventFlag == ImGuiInputTextFlags_CallbackCharFilter && cmd->mtextRichEditorTypingAllCaps) {
    const unsigned c = static_cast<unsigned>(data->EventChar);
    if (c >= static_cast<unsigned>('a') && c <= static_cast<unsigned>('z'))
      data->EventChar = static_cast<ImWchar>(c - static_cast<unsigned>('a' - 'A'));
  }
  return 0;
}

static void MtextRichInsertAtCaret(AppCommandState& cmd, const char* utf8) {
  int pos = cmd.mtextRichEditorCursor;
  if (cmd.mtextRichEditorSelStart != cmd.mtextRichEditorSelEnd)
    pos = cmd.mtextRichEditorSelStart;
  pos = std::clamp(pos, 0, static_cast<int>(cmd.mtextRichEditorBuf.size()));
  cmd.mtextRichEditorBuf.insert(static_cast<size_t>(pos), utf8);
}

static void MtextRichWrapSelection(AppCommandState& cmd, const char* open, const char* close) {
  std::string& s = cmd.mtextRichEditorBuf;
  int a = cmd.mtextRichEditorSelStart;
  int b = cmd.mtextRichEditorSelEnd;
  if (a > b)
    std::swap(a, b);
  a = std::clamp(a, 0, static_cast<int>(s.size()));
  b = std::clamp(b, 0, static_cast<int>(s.size()));
  const std::string mid = s.substr(static_cast<size_t>(a), static_cast<size_t>(b - a));
  s.replace(static_cast<size_t>(a), static_cast<size_t>(b - a), std::string(open) + mid + close);
}

/// Shell steals keyboard via SetKeyboardFocusHere() → navigation activation → InputText selects the
/// whole buffer on that frame. Collapse selection to end-of-buffer so the next keystroke appends.
/// Deliberate Ctrl+A keeps ActiveIdIsJustActivated false, so full selection is preserved.
// Set each frame by DrawCommandLinePanel to the highlighted suggestion (lowercased);
// Tab in the command input completes the buffer to this.
std::string g_cmdSuggestComplete;

int CommandLineInputCallback(ImGuiInputTextCallbackData* data) {
  if (data->EventFlag == ImGuiInputTextFlags_CallbackCompletion) {
    if (!g_cmdSuggestComplete.empty()) {
      data->DeleteChars(0, data->BufTextLen);
      data->InsertChars(0, g_cmdSuggestComplete.c_str());
    }
    return 0;
  }
  if (data->EventFlag != ImGuiInputTextFlags_CallbackAlways)
    return 0;

  ImGuiContext& g = *GImGui;
  const bool justActivated = (g.ActiveId == data->ID && g.ActiveIdIsJustActivated);
  const bool fullBufSelected =
      data->BufTextLen > 0 && data->SelectionStart == 0 && data->SelectionEnd == data->BufTextLen;

  if (justActivated && fullBufSelected) {
    data->CursorPos = data->BufTextLen;
    data->SelectionStart = data->SelectionEnd = data->CursorPos;
  }
  return 0;
}

/// Nothing wants text yet but the backend queued characters — merge into \p cmdBuf and drain the
/// queue so InputText does not insert the same codepoints twice after we focus it.
void RouteQueuedCharsToCmdBuf(char* cmdBuf, int cmdBufSize, ImGuiIO& io) {
  if (io.InputQueueCharacters.empty())
    return;
  for (int n = 0; n < io.InputQueueCharacters.Size; n++) {
    const unsigned int c = static_cast<unsigned int>(io.InputQueueCharacters[n]);
    char utf8[5];
    const int nbytes = ImTextCharToUtf8(utf8, c);
    if (nbytes <= 0)
      continue;
    const size_t len = std::strlen(cmdBuf);
    if (len + static_cast<size_t>(nbytes) >= static_cast<size_t>(cmdBufSize))
      break;
    std::memcpy(cmdBuf + len, utf8, static_cast<size_t>(nbytes));
    cmdBuf[len + static_cast<size_t>(nbytes)] = '\0';
  }
  io.InputQueueCharacters.clear();
}

} // namespace

void ApplyCadDarkTheme() {
  ImGuiStyle& style = ImGui::GetStyle();
  ImVec4* colors = style.Colors;

  style.WindowRounding = 4.f;
  style.ChildRounding = 3.f;
  style.FrameRounding = 3.f;
  style.PopupRounding = 4.f;
  style.ScrollbarRounding = 3.f;
  style.GrabRounding = 3.f;
  style.TabRounding = 3.f;
  style.WindowBorderSize = 1.f;
  style.FrameBorderSize = 0.f;
  style.TabBorderSize = 0.f;
  style.ScrollbarSize = 12.f;
  style.WindowPadding = ImVec2(8, 8);
  style.FramePadding = ImVec2(6, 4);
  style.ItemSpacing = ImVec2(8, 6);

  // Core UI palette — hex values match the FEAT-001 spec exactly
  const ImVec4 workspace  = ImVec4(0.051f, 0.059f, 0.071f, 1.f);  // #0D0F12  Workspace Background
  const ImVec4 secondary  = ImVec4(0.090f, 0.102f, 0.122f, 1.f);  // #171A1F  Secondary Background
  const ImVec4 panel      = ImVec4(0.125f, 0.145f, 0.173f, 1.f);  // #20252C  Panel Background
  const ImVec4 raised     = ImVec4(0.165f, 0.192f, 0.231f, 1.f);  // #2A313B  Raised Surface
  const ImVec4 border     = ImVec4(0.227f, 0.263f, 0.310f, 1.f);  // #3A434F  Border
  const ImVec4 separator  = ImVec4(0.294f, 0.337f, 0.392f, 1.f);  // #4B5664  Separator Lines
  const ImVec4 blue       = ImVec4(0.231f, 0.510f, 0.965f, 1.f);  // #3B82F6  Primary Blue accent
  const ImVec4 orange     = ImVec4(0.976f, 0.451f, 0.086f, 1.f);  // #F97316  Survey Orange (unused here; used in toggle buttons)
  const ImVec4 text       = ImVec4(0.898f, 0.906f, 0.922f, 1.f);  // #E5E7EB  Command Text / primary text

  (void)orange;

  colors[ImGuiCol_Text]                  = text;
  colors[ImGuiCol_TextDisabled]          = separator;
  colors[ImGuiCol_WindowBg]              = workspace;
  colors[ImGuiCol_ChildBg]               = secondary;
  colors[ImGuiCol_PopupBg]               = ImVec4(0.090f, 0.102f, 0.122f, 0.98f);
  colors[ImGuiCol_Border]                = border;
  colors[ImGuiCol_BorderShadow]          = ImVec4(0.f, 0.f, 0.f, 0.f);
  colors[ImGuiCol_FrameBg]               = panel;
  colors[ImGuiCol_FrameBgHovered]        = raised;
  colors[ImGuiCol_FrameBgActive]         = ImVec4(0.204f, 0.235f, 0.278f, 1.f);
  colors[ImGuiCol_TitleBg]               = secondary;
  colors[ImGuiCol_TitleBgActive]         = panel;
  colors[ImGuiCol_TitleBgCollapsed]      = workspace;
  colors[ImGuiCol_MenuBarBg]             = secondary;
  colors[ImGuiCol_ScrollbarBg]           = workspace;
  colors[ImGuiCol_ScrollbarGrab]         = raised;
  colors[ImGuiCol_ScrollbarGrabHovered]  = border;
  colors[ImGuiCol_ScrollbarGrabActive]   = separator;
  colors[ImGuiCol_CheckMark]             = blue;
  colors[ImGuiCol_SliderGrab]            = blue;
  colors[ImGuiCol_SliderGrabActive]      = ImVec4(0.380f, 0.588f, 0.984f, 1.f);
  colors[ImGuiCol_Button]                = panel;
  colors[ImGuiCol_ButtonHovered]         = raised;
  colors[ImGuiCol_ButtonActive]          = ImVec4(0.204f, 0.235f, 0.278f, 1.f);
  colors[ImGuiCol_Header]                = panel;
  colors[ImGuiCol_HeaderHovered]         = raised;
  colors[ImGuiCol_HeaderActive]          = ImVec4(0.204f, 0.235f, 0.278f, 1.f);
  colors[ImGuiCol_Separator]             = border;
  colors[ImGuiCol_SeparatorHovered]      = separator;
  colors[ImGuiCol_SeparatorActive]       = blue;
  colors[ImGuiCol_ResizeGrip]            = ImVec4(0.231f, 0.510f, 0.965f, 0.15f);
  colors[ImGuiCol_ResizeGripHovered]     = ImVec4(0.231f, 0.510f, 0.965f, 0.55f);
  colors[ImGuiCol_ResizeGripActive]      = blue;
  colors[ImGuiCol_Tab]                   = secondary;
  colors[ImGuiCol_TabHovered]            = raised;
  colors[ImGuiCol_TabActive]             = ImVec4(0.063f, 0.141f, 0.345f, 1.f);  // dark blue tint
  colors[ImGuiCol_TabUnfocused]          = workspace;
  colors[ImGuiCol_TabUnfocusedActive]    = ImVec4(0.063f, 0.141f, 0.345f, 1.f);  // same as TabActive — showing tab is always blue
  // Keep the overline accent explicitly — ImGui may copy HeaderActive into it and ours changed.
  colors[ImGuiCol_TabSelectedOverline]        = blue;
  colors[ImGuiCol_TabDimmedSelectedOverline]  = blue;  // showing tab overline stays blue even without focus
  colors[ImGuiCol_DockingPreview]        = ImVec4(0.231f, 0.510f, 0.965f, 0.35f);
  colors[ImGuiCol_DockingEmptyBg]        = workspace;
}

void ApplyCadLightTheme() {
  // nanoCAD "classic" Windows look: warm gray controls, white content cells,
  // steel-blue accents, square corners, 1px borders, compact rows.
  ImGuiStyle& style = ImGui::GetStyle();
  ImVec4* colors = style.Colors;

  // Square classic corners everywhere.
  style.WindowRounding    = 0.f;
  style.ChildRounding     = 0.f;
  style.FrameRounding     = 0.f;
  style.PopupRounding     = 0.f;
  style.ScrollbarRounding = 0.f;
  style.GrabRounding      = 0.f;
  style.TabRounding       = 0.f;
  // 1px 3D-style borders.
  style.WindowBorderSize  = 1.f;
  style.ChildBorderSize   = 1.f;
  style.FrameBorderSize   = 1.f;
  style.TabBorderSize     = 0.f;
  style.ScrollbarSize     = 16.f;
  style.GrabMinSize       = 12.f;
  // Compact spacing like a classic property grid.
  style.WindowPadding     = ImVec2(4, 4);
  style.FramePadding      = ImVec2(4, 2);
  style.ItemSpacing       = ImVec2(4, 3);
  style.ItemInnerSpacing  = ImVec2(4, 2);
  style.IndentSpacing     = 14.f;
  style.CellPadding       = ImVec2(4, 2);

  // --- nanoCAD classic palette ---
  const ImVec4 face       = ImVec4(0.275f, 0.275f, 0.275f, 1.f);  // #464646  dark control face (panels)
  const ImVec4 faceDk     = ImVec4(0.227f, 0.227f, 0.227f, 1.f);  // #3A3A3A  darker gray (buttons/tabs)
  const ImVec4 field      = ImVec4(0.176f, 0.176f, 0.176f, 1.f);  // #2D2D2D  recessed value cells / edits
  const ImVec4 hilite     = ImVec4(0.337f, 0.337f, 0.337f, 1.f);  // #565656  raised bevel highlight
  const ImVec4 shadow     = ImVec4(0.502f, 0.502f, 0.502f, 1.f);  // #808080  3D shadow
  const ImVec4 dkShadow   = ImVec4(0.251f, 0.251f, 0.251f, 1.f);  // #404040  3D dark shadow
  const ImVec4 text       = ImVec4(0.898f, 0.906f, 0.922f, 1.f);  // #E5E7EB  light text
  const ImVec4 textMuted  = ImVec4(0.627f, 0.627f, 0.627f, 1.f);  // #A0A0A0  disabled text
  // Steel-blue accents (section headers, active tab/title, selection).
  const ImVec4 steel      = ImVec4(0.235f, 0.333f, 0.459f, 1.f);  // #3C5575  dark steel (active/base, light text)
  const ImVec4 steelHi    = ImVec4(0.306f, 0.431f, 0.588f, 1.f);  // #4E6E96  brighter steel (hover, light text)
  const ImVec4 capBlue    = ImVec4(0.235f, 0.424f, 0.690f, 1.f);  // #3C6CB0  active caption blue
  const ImVec4 selBlue    = ImVec4(0.180f, 0.357f, 0.682f, 1.f);  // #2E5BAE  selection blue
  const ImVec4 mdiBlue    = ImVec4(0.357f, 0.486f, 0.659f, 1.f);  // #5B7CA8  steel MDI workspace

  colors[ImGuiCol_Text]                  = text;
  colors[ImGuiCol_TextDisabled]          = textMuted;
  colors[ImGuiCol_WindowBg]              = face;        // panel backgrounds
  colors[ImGuiCol_ChildBg]               = face;
  colors[ImGuiCol_PopupBg]               = face;
  colors[ImGuiCol_Border]                = shadow;      // 3D shadow border
  colors[ImGuiCol_BorderShadow]          = hilite;      // bottom-right highlight
  colors[ImGuiCol_FrameBg]               = field;       // edit fields / combos = recessed dark
  colors[ImGuiCol_FrameBgHovered]        = field;
  colors[ImGuiCol_FrameBgActive]         = ImVec4(0.235f, 0.235f, 0.235f, 1.f);  // #3C3C3C active field
  colors[ImGuiCol_TitleBg]               = face;
  // Keep focused panes the same color as unfocused: ImGui fills a docked node's tab-bar strip (and a
  // floating window's caption) with TitleBgActive when focused, so a contrasting color here makes every
  // panel flash dark blue as focus moves between them. Matching TitleBg removes that focus highlight.
  colors[ImGuiCol_TitleBgActive]         = face;
  colors[ImGuiCol_TitleBgCollapsed]      = faceDk;
  colors[ImGuiCol_MenuBarBg]             = face;
  colors[ImGuiCol_ScrollbarBg]           = faceDk;
  colors[ImGuiCol_ScrollbarGrab]         = face;
  colors[ImGuiCol_ScrollbarGrabHovered]  = hilite;
  colors[ImGuiCol_ScrollbarGrabActive]   = steel;
  colors[ImGuiCol_CheckMark]             = selBlue;
  colors[ImGuiCol_SliderGrab]            = faceDk;
  colors[ImGuiCol_SliderGrabActive]      = steel;
  colors[ImGuiCol_Button]                = faceDk;      // 3D gray buttons
  colors[ImGuiCol_ButtonHovered]         = steelHi;
  colors[ImGuiCol_ButtonActive]          = steel;
  colors[ImGuiCol_Header]                = steel;       // CollapsingHeader = steel-blue bar
  colors[ImGuiCol_HeaderHovered]         = steelHi;
  colors[ImGuiCol_HeaderActive]          = steel;
  colors[ImGuiCol_Separator]             = shadow;
  colors[ImGuiCol_SeparatorHovered]      = steel;
  colors[ImGuiCol_SeparatorActive]       = selBlue;
  colors[ImGuiCol_ResizeGrip]            = faceDk;
  colors[ImGuiCol_ResizeGripHovered]     = steel;
  colors[ImGuiCol_ResizeGripActive]      = selBlue;
  colors[ImGuiCol_Tab]                   = faceDk;      // inactive tab gray
  colors[ImGuiCol_TabHovered]            = steelHi;
  colors[ImGuiCol_TabActive]             = face;        // active tab = panel face (looks lifted)
  colors[ImGuiCol_TabUnfocused]          = faceDk;
  colors[ImGuiCol_TabUnfocusedActive]    = face;
  colors[ImGuiCol_TabSelectedOverline]        = capBlue;
  colors[ImGuiCol_TabDimmedSelectedOverline]  = capBlue;
  colors[ImGuiCol_TableHeaderBg]         = steel;
  colors[ImGuiCol_TableBorderStrong]     = shadow;
  colors[ImGuiCol_TableBorderLight]      = ImVec4(0.353f, 0.353f, 0.353f, 1.f);  // #5A5A5A gridline
  colors[ImGuiCol_TableRowBg]            = field;       // value rows = recessed dark
  colors[ImGuiCol_TableRowBgAlt]         = field;       // uniform (no zebra)
  colors[ImGuiCol_DockingPreview]        = ImVec4(0.235f, 0.424f, 0.690f, 0.40f);
  colors[ImGuiCol_DockingEmptyBg]        = face;        // empty MDI workspace = panel face (#464646)
  (void)mdiBlue;
  (void)dkShadow;
}

// ---------------------------------------------------------------------------
// nanoCAD-style property grid helpers
// ---------------------------------------------------------------------------

// Shared flags for all 2-column property tables: full gridlines, transparent rows.
// Rows are transparent so the gray panel face shows through the LABEL column,
// giving nanoCAD's two-tone look automatically; only value cells are painted white.
static constexpr ImGuiTableFlags kPropTableFlags =
    ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Borders;

// Recessed value column; the label column stays transparent (dark panel face).
static constexpr ImU32 kPropValueBg = IM_COL32(45, 45, 45, 255); // #2D2D2D

// Paint the value cell (column 1) of the current table row white, like nanoCAD.
// Call once per row (any time while that row is current). The label column is
// left transparent so the gray panel face shows through.
static void PropValueCellBg() {
  ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, kPropValueBg, 1);
}

// Fill the panel's empty area below the last item with the neutral gray
// workspace color (the grid above keeps its lighter window background for the
// label column), so inactive space reads as quiet gray rather than blue.
static void FillPropPanelEmpty() {
  ImGuiWindow* win = ImGui::GetCurrentWindow();
  if (!win) return;
  const float top = win->DC.CursorPos.y;
  const float bottom = win->Pos.y + win->Size.y;
  if (bottom <= top) return;
  win->DrawList->AddRectFilled(ImVec2(win->Pos.x, top),
                               ImVec2(win->Pos.x + win->Size.x, bottom),
                               IM_COL32(70, 70, 70, 255));  // #464646 neutral panel fill
}

// nanoCAD-style collapsible section header: blue gradient bar, navy bold-ish text,
// a [-]/[+] box on the right, and 3D highlight/shadow edges. Replaces
// ImGui::CollapsingHeader for property sections. Open state persists per-window.
static bool PropSectionHeader(const char* label) {
  ImGuiWindow* window = ImGui::GetCurrentWindow();
  if (window->SkipItems) return false;

  ImGuiContext& g = *GImGui;
  ImGuiStorage* storage = window->DC.StateStorage;
  const ImGuiID id = window->GetID(label);
  bool open = storage->GetInt(id, 1) != 0;

  const float h = ImGui::GetFrameHeight();
  const float w = ImGui::GetContentRegionAvail().x;
  const ImVec2 pos = window->DC.CursorPos;
  const ImRect bb(pos, ImVec2(pos.x + w, pos.y + h));

  ImGui::ItemSize(ImVec2(w, h), 0.f);
  if (!ImGui::ItemAdd(bb, id)) return open;

  bool hovered = false, held = false;
  const bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
  if (pressed) { open = !open; storage->SetInt(id, open ? 1 : 0); }

  ImDrawList* dl = window->DrawList;
  // Dark steel-blue gradient (lighter on the right), brighter when hovered.
  const ImU32 cL = hovered ? IM_COL32( 58,  88, 128, 255) : IM_COL32( 48,  72, 104, 255);
  const ImU32 cR = hovered ? IM_COL32( 78, 118, 168, 255) : IM_COL32( 60,  92, 134, 255);
  dl->AddRectFilledMultiColor(bb.Min, bb.Max, cL, cR, cR, cL);
  // 3D edges: subtle highlight on top, shadow on bottom.
  dl->AddLine(ImVec2(bb.Min.x, bb.Min.y), ImVec2(bb.Max.x, bb.Min.y), IM_COL32(255, 255, 255, 60));
  dl->AddLine(ImVec2(bb.Min.x, bb.Max.y - 1), ImVec2(bb.Max.x, bb.Max.y - 1), IM_COL32(0, 0, 0, 120));

  // [-]/[+] collapse box on the right.
  const float boxSz = ImGui::GetFontSize() * 0.62f;
  const ImVec2 boxC(bb.Max.x - boxSz, bb.Min.y + h * 0.5f);
  const ImRect box(ImVec2(boxC.x - boxSz * 0.5f, boxC.y - boxSz * 0.5f),
                   ImVec2(boxC.x + boxSz * 0.5f, boxC.y + boxSz * 0.5f));
  dl->AddRectFilled(box.Min, box.Max, IM_COL32(255, 255, 255, 255));
  dl->AddRect(box.Min, box.Max, IM_COL32(70, 90, 120, 255));
  const ImU32 glyph = IM_COL32(20, 50, 95, 255);
  const float my = (box.Min.y + box.Max.y) * 0.5f;
  dl->AddLine(ImVec2(box.Min.x + 2, my), ImVec2(box.Max.x - 2, my), glyph); // horizontal (minus)
  if (!open) {
    const float mx = (box.Min.x + box.Max.x) * 0.5f;
    dl->AddLine(ImVec2(mx, box.Min.y + 2), ImVec2(mx, box.Max.y - 2), glyph); // vertical → plus
  }

  // Light title text on the dark steel bar.
  dl->AddText(ImVec2(bb.Min.x + 6, bb.Min.y + (h - ImGui::GetFontSize()) * 0.5f),
              IM_COL32(229, 231, 235, 255), label);
  (void)g;
  return open;
}

void SetupMainDockLayout(ImGuiID dockspace_id, const ImVec2& dock_host_size) {
  ImGui::DockBuilderRemoveNode(dockspace_id);
  ImGuiDockNodeFlags node_flags = ImGuiDockNodeFlags_DockSpace;
  ImGui::DockBuilderAddNode(dockspace_id, node_flags);
  // Must match the actual DockSpace host rect (inside GoSurveyHost → ##GoSurveyDockWrap), not the full viewport —
  // otherwise dock nodes and .ini docking data disagree and panels stack at the default position on load.
  const ImVec2 sz(std::max(dock_host_size.x, 32.f), std::max(dock_host_size.y, 32.f));
  ImGui::DockBuilderSetNodeSize(dockspace_id, sz);

  ImGuiID dock_left = 0;
  ImGuiID dock_right = 0;
  ImGuiID dock_bottom = 0;
  ImGuiID dock_center = dockspace_id;

  ImGui::DockBuilderSplitNode(dock_center, ImGuiDir_Left, 0.22f, &dock_left, &dock_center);
  ImGui::DockBuilderSplitNode(dock_center, ImGuiDir_Right, 0.24f, &dock_right, &dock_center);
  ImGui::DockBuilderSplitNode(dock_center, ImGuiDir_Down, 0.30f, &dock_bottom, &dock_center);

  ImGui::DockBuilderDockWindow("Reports", dock_left);
  ImGui::DockBuilderDockWindow("Properties", dock_left);  // docked last → active tab
  ImGui::DockBuilderDockWindow("Command line", dock_bottom);
  ImGui::DockBuilderDockWindow("Viewports", dock_center);

  ImGui::DockBuilderFinish(dockspace_id);
}

void DrawMainMenuBar(AppCommandState& cmd, std::vector<std::string>& log) {
  static char dxfPath[4096]{};
  static char gsPath[4096]{};
#if !defined(_WIN32)
  if (g_menuBarLogoTex && g_menuBarLogoDims.x > 0.f && g_menuBarLogoDims.y > 0.f) {
    const ImGuiStyle& st = ImGui::GetStyle();
    const float fh = ImGui::GetFrameHeight();
    const float logoH = std::max(1.f, fh - st.FramePadding.y * 0.35f);
    const float aspect = g_menuBarLogoDims.x / g_menuBarLogoDims.y;
    const float logoW = logoH * aspect;
    const float yPad = std::max(0.f, (fh - logoH) * 0.5f);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + yPad);
    ImGui::Image(g_menuBarLogoTex, ImVec2(logoW, logoH), ImVec2(0.f, 1.f), ImVec2(1.f, 0.f));
    ImGui::SameLine(0.f, st.ItemInnerSpacing.x);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - yPad);
  }
#endif
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.f, 8.f));
  if (ImGui::BeginMenu("File")) {
    if (ImGui::MenuItem("New", nullptr)) {
      SaveDocumentToSnapshot(cmd, cmd.activeDrawingIdx);
      const int newIdx = static_cast<int>(cmd.drawingTabs.size());
      cmd.drawingTabs.push_back({"Drawing " + std::to_string(cmd.nextDrawingNumber++), cmd.nextTabUid++});
      cmd.documents.emplace_back();
      RestoreDocumentFromSnapshot(cmd, newIdx);  // load empty state into cmd
      cmd.activeDrawingIdx        = newIdx;
      cmd.prevDrawingIdx          = newIdx;  // tell main.cpp the switch already happened
      cmd.pendingDrawingTabSwitch = true;
      cmd.pendingViewportFocus    = true;
    }
    if (ImGui::MenuItem("Open", nullptr)) {
      if (BrowseOpenFileGsUtf8(gsPath, sizeof(gsPath))) {
        SaveDocumentToSnapshot(cmd, cmd.activeDrawingIdx);
        const std::string tabName = std::filesystem::path(gsPath).stem().string();
        const int newIdx = static_cast<int>(cmd.drawingTabs.size());
        cmd.drawingTabs.push_back({tabName.empty() ? "Drawing" : tabName, cmd.nextTabUid++});
        cmd.documents.emplace_back();
        RestoreDocumentFromSnapshot(cmd, newIdx);  // clear cmd to empty state
        if (LoadGoSurveyFile(cmd, gsPath, log)) {
          cmd.activeDocSavedRevision = cmd.cadGpuRevision;
          cmd.activeDocFilePath      = std::string(gsPath);
        }
        cmd.activeDrawingIdx        = newIdx;
        cmd.prevDrawingIdx          = newIdx;  // tell main.cpp the switch already happened
        cmd.pendingDrawingTabSwitch = true;
        cmd.pendingViewportFocus    = true;
      }
    }
    if (ImGui::MenuItem("Save", "Ctrl+S")) {
      const std::string& path = cmd.activeDocFilePath;
      if (!path.empty()) {
        if (SaveGoSurveyFile(cmd, path.c_str(), log))
          cmd.activeDocSavedRevision = cmd.cadGpuRevision;
      } else {
        if (BrowseSaveFileGsUtf8(gsPath, sizeof(gsPath), "drawing.gs")) {
          if (SaveGoSurveyFile(cmd, gsPath, log)) {
            cmd.activeDocSavedRevision = cmd.cadGpuRevision;
            cmd.activeDocFilePath      = std::string(gsPath);
            if (cmd.activeDrawingIdx < static_cast<int>(cmd.drawingTabs.size()))
              cmd.drawingTabs[cmd.activeDrawingIdx].name =
                  std::filesystem::path(gsPath).stem().string();
          }
        }
      }
    }
    if (ImGui::MenuItem("Save As...")) {
      const std::string defName = cmd.activeDocFilePath.empty()
          ? (cmd.activeDrawingIdx < static_cast<int>(cmd.drawingTabs.size())
                 ? cmd.drawingTabs[cmd.activeDrawingIdx].name + ".gs"
                 : std::string("drawing.gs"))
          : std::filesystem::path(cmd.activeDocFilePath).filename().string();
      if (BrowseSaveFileGsUtf8(gsPath, sizeof(gsPath), defName.c_str())) {
        if (SaveGoSurveyFile(cmd, gsPath, log)) {
          cmd.activeDocSavedRevision = cmd.cadGpuRevision;
          cmd.activeDocFilePath      = std::string(gsPath);
          if (cmd.activeDrawingIdx < static_cast<int>(cmd.drawingTabs.size()))
            cmd.drawingTabs[cmd.activeDrawingIdx].name =
                std::filesystem::path(gsPath).stem().string();
        }
      }
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Import DXF...", nullptr)) {
      if (BrowseOpenFileDxfUtf8(dxfPath, sizeof(dxfPath)))
        ImportDxfFile(cmd, dxfPath, log);
    }
    if (ImGui::MenuItem("Export DXF...", nullptr)) {
      if (BrowseSaveFileDxfUtf8(dxfPath, sizeof(dxfPath), "drawing.dxf"))
        ExportDxfFile(cmd, dxfPath, log);
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Quit Application", nullptr)) {
      bool anyDirty = (cmd.cadGpuRevision != cmd.activeDocSavedRevision);
      for (int i = 0; i < static_cast<int>(cmd.documents.size()) && !anyDirty; ++i) {
        if (i != cmd.activeDrawingIdx &&
            cmd.documents[i].cadGpuRevision != cmd.documents[i].savedRevision)
          anyDirty = true;
      }
      if (anyDirty)
        cmd.confirmCloseModal = true;
      else
        cmd.closeConfirmed = true;
    }
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Edit")) {
    const bool hasSelection = !cmd.selection.empty() || !cmd.selectedSurveyPointIndices.empty();
    const bool hasClipboard = !cmd.clipboard.empty();
    if (ImGui::MenuItem("Copy", "Ctrl+C", false, hasSelection))
      CopySelectionToClipboard(cmd, log);
    if (ImGui::MenuItem("Paste", "Ctrl+V", false, hasClipboard))
      StartPasteCommand(cmd, log);
    if (ImGui::MenuItem("Paste at Original Coordinates", nullptr, false, hasClipboard))
      StartPasteOrigCommand(cmd, log);
    ImGui::Separator();
    const bool canUndo = CanUndo(cmd);
    const bool canRedo = CanRedo(cmd);
    std::string undoLabel = "Undo";
    if (canUndo) {
      const auto& desc = cmd.documents[static_cast<size_t>(cmd.activeDrawingIdx)].undoStack.back().description;
      if (!desc.empty())
        undoLabel = "Undo: " + desc;
    }
    if (ImGui::MenuItem(undoLabel.c_str(), "Ctrl+Z", false, canUndo))
      DoUndo(cmd, log);
    if (ImGui::MenuItem("Redo", "Ctrl+Shift+Z", false, canRedo))
      DoRedo(cmd, log);
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("View")) {
    if (ImGui::MenuItem("Reset layout", nullptr))
      cmd.pendingBuiltinDockLayoutReset = true;
    ImGuiLayout_DrawViewLayoutMenu(cmd, log);
    ImGui::Separator();
    if (ImGui::MenuItem("Settings...", nullptr))
      cmd.showSettingsWindow = true;
    ImGui::EndMenu();
  }
  ImGui::PopStyleVar();
}

static void CollectAllDrawingLayers(const AppCommandState& cmd, std::vector<std::string>* outSortedUnique) {
  std::set<std::string> layers;
  layers.insert("0");
  for (const auto& row : cmd.drawingLayerTable) {
    if (!row.name.empty())
      layers.insert(row.name);
  }
  auto add = [&layers](const std::string& s) {
    if (!s.empty())
      layers.insert(s);
  };
  for (const auto& a : cmd.userLineAttrs)
    add(a.layer);
  for (const auto& a : cmd.userCircleAttrs)
    add(a.layer);
  for (const auto& a : cmd.userArcAttrs)
    add(a.layer);
  for (const auto& a : cmd.userEllAttrs)
    add(a.layer);
  for (const auto& a : cmd.userPolylineAttrs)
    add(a.layer);
  for (const auto& a : cmd.cadAnnotationAttrs)
    add(a.layer);
  for (const auto& p : cmd.surveyPoints)
    add(p.layer);
  if (!cmd.currentLayer.empty())
    add(cmd.currentLayer);
  outSortedUnique->assign(layers.begin(), layers.end());
}


// nanoCAD-classic toolbar band palette. One warm system-gray for the whole
// strip (no Office-blue ribbon), with 3D bevel hi/lo tones for button states
// and grippers. Kept here so the strip, sections and buttons stay in sync.
constexpr ImU32 kBandFace   = IM_COL32( 70,  70,  70, 255);  // #464646 toolbar band
constexpr ImU32 kBandHilite = IM_COL32( 86,  86,  86, 255);  // #565656 top-left bevel
constexpr ImU32 kBandShadow = IM_COL32( 32,  32,  32, 255);  // #202020 bottom-right bevel
constexpr ImU32 kBandSunken = IM_COL32( 58,  58,  58, 255);  // #3A3A3A pressed face

// Height of the bottom title strip inside each ribbon panel (Civil 3D-style).
constexpr float kRibbonTitleH = 17.f;
// Carries the active panel's title from Begin to End (panels never nest).
static const char* s_ribbonPanelTitle = nullptr;

// Usable content height above the bottom title strip, for sizing buttons.
static float RibbonPanelContentH(float panelH) {
  return std::max(24.f, panelH - kRibbonTitleH);
}

static void RibbonSectionBegin(const char* childId, const char* title, float width, float height) {
  ImGui::BeginGroup();
  // Flat panel: same gray as the band, no border. Buttons float on a uniform
  // strip; the panel title is pinned at the bottom by RibbonSectionEnd.
  ImGui::PushStyleColor(ImGuiCol_ChildBg, kBandFace);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.f, 2.f));
  ImGui::BeginChild(childId, ImVec2(width, height), false, ImGuiWindowFlags_NoScrollbar);
  ImGui::PopStyleVar();
  s_ribbonPanelTitle = title;
}

static void RibbonSectionEnd() {
  // Bottom-centered panel title + dropdown chevron (Civil 3D-style).
  ImGuiWindow* win = ImGui::GetCurrentWindow();
  const float wh = win->Size.y;
  const float ww = win->Size.x;
  const ImVec2 wp = win->Pos;
  ImDrawList* dl = win->DrawList;
  const char* title = s_ribbonPanelTitle ? s_ribbonPanelTitle : "";
  const ImVec2 ts = ImGui::CalcTextSize(title);

  const float titleTop = wp.y + wh - kRibbonTitleH;
  dl->AddLine(ImVec2(wp.x + 3.f, titleTop), ImVec2(wp.x + ww - 3.f, titleTop), IM_COL32(0, 0, 0, 26), 1.f);

  const ImVec4 t4 = ImGui::GetStyleColorVec4(ImGuiCol_Text);
  const ImVec4 d4 = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
  const ImU32 tcol = ImGui::GetColorU32(
      ImVec4(t4.x * 0.5f + d4.x * 0.5f, t4.y * 0.5f + d4.y * 0.5f, t4.z * 0.5f + d4.z * 0.5f, 1.f));

  constexpr float chevSz = 3.f;
  constexpr float gap = 5.f;
  const float totalW = ts.x + gap + chevSz * 2.f;
  const float tx = wp.x + (ww - totalW) * 0.5f;
  const float ty = titleTop + (kRibbonTitleH - ts.y) * 0.5f;
  dl->AddText(ImVec2(tx, ty), tcol, title);
  const float cx = tx + ts.x + gap + chevSz;
  const float cy = titleTop + kRibbonTitleH * 0.5f;
  dl->AddTriangleFilled(ImVec2(cx - chevSz, cy - chevSz * 0.55f), ImVec2(cx + chevSz, cy - chevSz * 0.55f),
                        ImVec2(cx, cy + chevSz * 0.75f), tcol);

  ImGui::EndChild();
  ImGui::PopStyleColor();

  // Etched vertical divider in the gap to the right of the panel.
  const ImVec2 mn = ImGui::GetItemRectMin();
  const ImVec2 mx = ImGui::GetItemRectMax();
  ImDrawList* pdl = ImGui::GetWindowDrawList();
  const float dx = mx.x + 4.f;
  pdl->AddLine(ImVec2(dx, mn.y + 3.f), ImVec2(dx, mx.y - 3.f), kBandShadow, 1.f);
  pdl->AddLine(ImVec2(dx + 1.f, mn.y + 3.f), ImVec2(dx + 1.f, mx.y - 3.f), kBandHilite, 1.f);

  ImGui::EndGroup();
}

enum class RibbonIconKind : std::uint8_t {
  Line,
  Circle,
  Polyline,
  Arc,
  Ellipse,
  Dim,
  DimLinear,
  Id,
  Text,
  Mtext,
  Move,
  Copy,
  Rotate,
  Erase,
  Join,
  Trim,
  Offset,
  ZoomExtents,
  ZoomWindow,
  Scale,
  Mirror,
  SurveyPoint,
  SurveyInverse,
  Layers,
  PdfAttach,
  PdfShowBg,
  PdfHideBg,
  PdfVectorize,
  Undo,
  Redo,
  ClipboardCopy,
  ClipboardPaste,
  Traverse,
};

static ImVec2 RibbonLerp(const ImVec2& a, const ImVec2& b, float u, float v) {
  return ImVec2(a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * v);
}

static void RibbonStrokeArrow(ImDrawList* dl, ImVec2 tip, ImVec2 dir, float headLen, ImU32 col, float th) {
  const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
  if (len < 1e-4f)
    return;
  const float inv = 1.f / len;
  const ImVec2 d(dir.x * inv, dir.y * inv);
  const ImVec2 base(tip.x - d.x * headLen, tip.y - d.y * headLen);
  const ImVec2 perp(-d.y * headLen * 0.45f, d.x * headLen * 0.45f);
  dl->AddLine(base, tip, col, th);
  dl->AddLine(tip, ImVec2(base.x + perp.x, base.y + perp.y), col, th);
  dl->AddLine(tip, ImVec2(base.x - perp.x, base.y - perp.y), col, th);
}

static void RibbonGripSquare(ImDrawList* dl, ImVec2 ctr, float half, ImU32 fillCol, ImU32 edgeCol, float edgeTh) {
  const ImVec2 a(ctr.x - half, ctr.y - half);
  const ImVec2 b(ctr.x + half, ctr.y + half);
  dl->AddRectFilled(a, b, fillCol);
  dl->AddRect(a, b, edgeCol, 0.f, 0, edgeTh);
}

// Muted 2-tone fallback art: all geometry uses one dark slate "ink"; node and
// emphasis accents (drawn separately below) use steel-blue. Matches the
// regenerated bitmap icon set so missing PNGs degrade consistently.
static ImU32 RibbonIconColor(RibbonIconKind /*k*/) {
  return IM_COL32(58, 64, 74, 255);  // #3A404A primary geometry ink
}

static void PaintRibbonIcon(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, RibbonIconKind k, ImU32 col, bool hovered) {
  IM_UNUSED(hovered);
  IM_UNUSED(col);
  const float w = std::max(1.f, mx.x - mn.x);
  const float h = std::max(1.f, mx.y - mn.y);
  const float t = std::clamp(std::min(w, h) * 0.060f, 1.35f, 2.6f);
  const ImVec2 c((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f);
  // Dark slate geometry with steel-blue node/emphasis accents (2-tone set).
  col = RibbonIconColor(k);
  const ImU32 acc = IM_COL32(46, 91, 174, 255);      // #2E5BAE  steel-blue endpoint/grip nodes
  const float grip = std::clamp(std::min(w, h) * 0.05f, 2.2f, 4.8f);

  switch (k) {
  case RibbonIconKind::Line: {
    const ImVec2 p0 = RibbonLerp(mn, mx, 0.15f, 0.85f);
    const ImVec2 p1 = RibbonLerp(mn, mx, 0.85f, 0.15f);
    dl->AddLine(p0, p1, col, t);
    RibbonGripSquare(dl, p0, grip, acc, acc, t);
    RibbonGripSquare(dl, p1, grip, acc, acc, t);
    break;
  }
  case RibbonIconKind::Circle: {
    const float r = std::min(w, h) * 0.33f;
    dl->AddCircle(c, r, col, 32, t);
    const float sq = std::min(w, h) * 0.055f;
    dl->AddRectFilled(ImVec2(c.x - sq, c.y - sq), ImVec2(c.x + sq, c.y + sq), acc);
    dl->AddRect(ImVec2(c.x - sq, c.y - sq), ImVec2(c.x + sq, c.y + sq), col, 0.f, 0, t);
    const float a = -3.14159265f * 0.25f;
    const float margin = t * 2.5f;
    const ImVec2 tip(c.x + std::cos(a) * (r - margin), c.y + std::sin(a) * (r - margin));
    const float innerR = sq * 2.5f;
    const ImVec2 inner(c.x + std::cos(a) * innerR, c.y + std::sin(a) * innerR);
    dl->AddLine(inner, tip, acc, t);
    const float head = std::min(w, h) * 0.09f;
    RibbonStrokeArrow(dl, tip, ImVec2(tip.x - inner.x, tip.y - inner.y), head, acc, t);
    break;
  }
  case RibbonIconKind::Polyline: {
    const ImVec2 v0 = RibbonLerp(mn, mx, 0.12f, 0.75f);
    const ImVec2 v1 = RibbonLerp(mn, mx, 0.35f, 0.35f);
    const ImVec2 v2 = RibbonLerp(mn, mx, 0.72f, 0.55f);
    const ImVec2 v3 = RibbonLerp(mn, mx, 0.88f, 0.22f);
    dl->AddLine(v0, v1, col, t);
    dl->AddLine(v1, v2, col, t);
    dl->AddLine(v2, v3, col, t);
    RibbonGripSquare(dl, v0, grip, acc, acc, t);
    RibbonGripSquare(dl, v1, grip, acc, acc, t);
    RibbonGripSquare(dl, v2, grip, acc, acc, t);
    RibbonGripSquare(dl, v3, grip, acc, acc, t);
    break;
  }
  case RibbonIconKind::Arc: {
    const float r = std::min(w, h) * 0.36f;
    const float a0 = 3.55f;
    const float a1 = 5.95f;
    dl->PathClear();
    dl->PathArcTo(c, r, a0, a1, 20);
    dl->PathStroke(col, t, 0);
    auto apt = [&](float ang) { return ImVec2(c.x + std::cos(ang) * r, c.y + std::sin(ang) * r); };
    RibbonGripSquare(dl, apt(a0), grip, acc, acc, t);
    RibbonGripSquare(dl, apt((a0 + a1) * 0.5f), grip, acc, acc, t);
    RibbonGripSquare(dl, apt(a1), grip, acc, acc, t);
    break;
  }
  case RibbonIconKind::Ellipse: {
    const float rx = w * 0.36f;
    const float ry = h * 0.22f;
    dl->AddEllipse(c, ImVec2(rx, ry), col, 0.f, 28, t);
    RibbonGripSquare(dl, c, grip, acc, acc, t);
    RibbonGripSquare(dl, ImVec2(c.x, c.y - ry), grip, acc, acc, t);
    RibbonGripSquare(dl, ImVec2(c.x + rx, c.y), grip, acc, acc, t);
    break;
  }
  case RibbonIconKind::Dim: {
    // Aligned dimension: two parallel witness segments (white) + perpendicular dim line with outward arrows (accent).
    const float inv = 0.70710678f;
    const ImVec2 u(inv, -inv);   // extension direction (~45° up-right in screen space)
    const ImVec2 v(inv, inv);    // dimension line (perpendicular to extensions)
    const float dimHalf = std::min(w, h) * 0.26f;
    const float extHalf = std::min(w, h) * 0.11f;
    const ImVec2 p0(c.x - v.x * dimHalf, c.y - v.y * dimHalf);
    const ImVec2 p1(c.x + v.x * dimHalf, c.y + v.y * dimHalf);
    dl->AddLine(ImVec2(p0.x - u.x * extHalf, p0.y - u.y * extHalf), ImVec2(p0.x + u.x * extHalf, p0.y + u.y * extHalf),
                col, t);
    dl->AddLine(ImVec2(p1.x - u.x * extHalf, p1.y - u.y * extHalf), ImVec2(p1.x + u.x * extHalf, p1.y + u.y * extHalf),
                col, t);
    dl->AddLine(p0, p1, acc, t * 1.05f);
    const float head = std::clamp(std::min(w, h) * 0.095f, 2.5f, 5.5f);
    RibbonStrokeArrow(dl, p0, ImVec2(-v.x, -v.y), head, acc, t);
    RibbonStrokeArrow(dl, p1, ImVec2(v.x, v.y), head, acc, t);
    break;
  }
  case RibbonIconKind::DimLinear: {
    const float xL = mn.x + w * 0.24f;
    const float xR = mx.x - w * 0.24f;
    const float yTop = mn.y + h * 0.2f;
    const float yBot = mx.y - h * 0.2f;
    const float yDim = c.y;
    const ImU32 accentBlue = IM_COL32(46, 91, 174, 255);  // #2E5BAE steel-blue accent
    dl->AddLine(ImVec2(xL, yTop), ImVec2(xL, yBot), col, t);
    dl->AddLine(ImVec2(xR, yTop), ImVec2(xR, yBot), col, t);
    dl->AddLine(ImVec2(xL, yDim), ImVec2(xR, yDim), accentBlue, t * 1.05f);
    const float head = std::clamp(std::min(w, h) * 0.1f, 2.5f, 5.5f);
    RibbonStrokeArrow(dl, ImVec2(xL, yDim), ImVec2(-1.f, 0.f), head, accentBlue, t);
    RibbonStrokeArrow(dl, ImVec2(xR, yDim), ImVec2(1.f, 0.f), head, accentBlue, t);
    break;
  }
  case RibbonIconKind::Id: {
    // Axes + magnifier + sky-blue pick point (inquiry).
    const ImVec2 org(c.x - w * 0.1f, c.y + h * 0.06f);
    const float ax = w * 0.2f;
    const float ay = h * 0.2f;
    const ImVec2 yTip(org.x, org.y - ay);
    const ImVec2 xTip(org.x + ax, org.y);
    dl->AddLine(org, yTip, col, t);
    dl->AddLine(org, xTip, col, t);
    const float ah = std::clamp(std::min(w, h) * 0.065f, 2.5f, 5.f);
    RibbonStrokeArrow(dl, yTip, ImVec2(0.f, -1.f), ah, col, t);
    RibbonStrokeArrow(dl, xTip, ImVec2(1.f, 0.f), ah, col, t);
    const float glassR = std::min(w, h) * 0.19f;
    dl->AddCircle(c, glassR, IM_COL32(255, 255, 255, 255), 22, t);
    const float inv = 0.70710678f;
    const ImVec2 h0(c.x + inv * glassR * 0.62f, c.y + inv * glassR * 0.62f);
    const ImVec2 h1(h0.x + inv * glassR * 0.95f, h0.y + inv * glassR * 0.95f);
    const ImU32 tanCol = IM_COL32(198, 162, 128, 255);
    dl->AddLine(h0, h1, tanCol, t * 1.35f);
    const float sq = std::clamp(std::min(w, h) * 0.048f, 2.f, 4.5f);
    const ImU32 sky = IM_COL32(115, 192, 245, 255);
    dl->AddRectFilled(ImVec2(c.x - sq, c.y - sq), ImVec2(c.x + sq, c.y + sq), sky);
    break;
  }
  case RibbonIconKind::Text: {
    // Single-line text: I-beam cursor + baseline (distinct from MTEXT frame).
    const float xc = c.x;
    const float capW = w * 0.11f;
    const float top = mn.y + h * 0.22f;
    const float bot = mx.y - h * 0.26f;
    dl->AddLine(ImVec2(xc, top), ImVec2(xc, bot), col, t);
    dl->AddLine(ImVec2(xc - capW, top), ImVec2(xc + capW, top), col, t);
    dl->AddLine(ImVec2(xc - capW, bot), ImVec2(xc + capW, bot), col, t);
    const float yb = mx.y - h * 0.12f;
    dl->AddLine(ImVec2(mn.x + w * 0.12f, yb), ImVec2(mx.x - w * 0.12f, yb), col, t * 0.9f);
    break;
  }
  case RibbonIconKind::Mtext: {
    const ImVec2 a(mn.x + w * 0.1f, mn.y + h * 0.12f);
    const ImVec2 b(mx.x - w * 0.1f, mx.y - h * 0.12f);
    dl->AddRect(a, b, col, 2.f, 0, t);
    for (int i = 0; i < 4; ++i) {
      const float yy = a.y + (b.y - a.y) * (0.28f + static_cast<float>(i) * 0.14f);
      dl->AddLine(ImVec2(a.x + w * 0.08f, yy), ImVec2(b.x - w * 0.08f, yy), col, t * 0.85f);
    }
    break;
  }
  case RibbonIconKind::Move: {
    const float arm = std::min(w, h) * 0.19f;
    dl->AddLine(c, ImVec2(c.x, c.y - arm), col, t);
    dl->AddLine(c, ImVec2(c.x, c.y + arm), col, t);
    dl->AddLine(c, ImVec2(c.x - arm, c.y), col, t);
    dl->AddLine(c, ImVec2(c.x + arm, c.y), col, t);
    RibbonStrokeArrow(dl, ImVec2(c.x, c.y - arm), ImVec2(0.f, -1.f), arm * 0.55f, col, t);
    RibbonStrokeArrow(dl, ImVec2(c.x, c.y + arm), ImVec2(0.f, 1.f), arm * 0.55f, col, t);
    RibbonStrokeArrow(dl, ImVec2(c.x - arm, c.y), ImVec2(-1.f, 0.f), arm * 0.55f, col, t);
    RibbonStrokeArrow(dl, ImVec2(c.x + arm, c.y), ImVec2(1.f, 0.f), arm * 0.55f, col, t);
    break;
  }
  case RibbonIconKind::Copy: {
    const ImVec2 off(w * 0.14f, h * 0.12f);
    const ImVec2 r0(mn.x + w * 0.12f, mn.y + h * 0.2f);
    const ImVec2 r1(r0.x + w * 0.38f, r0.y + h * 0.38f);
    const ImVec2 s0(r0.x + off.x, r0.y - off.y);
    const ImVec2 s1(r1.x + off.x, r1.y - off.y);
    dl->AddRect(r0, r1, col, 1.5f, 0, t);
    dl->AddRect(s0, s1, acc, 1.5f, 0, t);
    RibbonStrokeArrow(dl, ImVec2(s0.x - w * 0.04f, s0.y + h * 0.08f), ImVec2(-1.f, 0.35f), std::min(w, h) * 0.1f, col, t);
    break;
  }
  case RibbonIconKind::Rotate: {
    const float r = std::min(w, h) * 0.28f;
    dl->PathClear();
    dl->PathArcTo(c, r, 0.9f, 4.6f, 24);
    dl->PathStroke(col, t, 0);
    const float ae = 4.6f;
    const ImVec2 tip(c.x + std::cos(ae) * r, c.y + std::sin(ae) * r);
    const ImVec2 prev(c.x + std::cos(ae - 0.25f) * r, c.y + std::sin(ae - 0.25f) * r);
    RibbonStrokeArrow(dl, tip, ImVec2(tip.x - prev.x, tip.y - prev.y), std::min(w, h) * 0.12f, col, t);
    dl->AddCircleFilled(c, t * 0.9f, col, 8);
    break;
  }
  case RibbonIconKind::Erase: {
    // Delete = red circle-slash with an X (nanoCAD convention).
    const float rC = std::min(w, h) * 0.28f;
    dl->AddCircle(c, rC, acc, 24, t);
    const float pad = std::min(w, h) * 0.14f;
    dl->AddLine(ImVec2(c.x - pad, c.y - pad), ImVec2(c.x + pad, c.y + pad), acc, t * 1.1f);
    dl->AddLine(ImVec2(c.x + pad, c.y - pad), ImVec2(c.x - pad, c.y + pad), acc, t * 1.1f);
    const float yb = mx.y - h * 0.1f;
    const float gap = std::min(w, h) * 0.035f;
    dl->AddLine(ImVec2(mn.x + w * 0.14f, yb), ImVec2(c.x - gap, yb), col, t);
    dl->AddLine(ImVec2(c.x + gap, yb), ImVec2(mx.x - w * 0.14f, yb), col, t);
    break;
  }
  case RibbonIconKind::Join: {
    // Two colinear segments with small inward-pointing arrows meeting at center (accent).
    const float y = c.y;
    const float head = std::clamp(std::min(w, h) * 0.026f, 1.6f, 3.f);
    const float eps = head * 0.22f;
    const ImVec2 tipL(c.x - eps, y);
    const ImVec2 tipR(c.x + eps, y);
    const float xLO = mn.x + w * 0.14f;
    const float xRO = mx.x - w * 0.14f;
    dl->AddLine(ImVec2(xLO, y), ImVec2(tipL.x - head, y), col, t);
    dl->AddLine(ImVec2(xRO, y), ImVec2(tipR.x + head, y), col, t);
    RibbonStrokeArrow(dl, tipL, ImVec2(1.f, 0.f), head, acc, t);
    RibbonStrokeArrow(dl, tipR, ImVec2(-1.f, 0.f), head, acc, t);
    break;
  }
  case RibbonIconKind::Trim: {
    // Cutting edge (oblique) + horizontal segment with a gap where it was trimmed away.
    const ImVec2 blade0(c.x - w * 0.28f, mn.y + h * 0.2f);
    const ImVec2 blade1(c.x + w * 0.32f, mx.y - h * 0.18f);
    dl->AddLine(blade0, blade1, col, t * 1.05f);
    const float ySeg = c.y + h * 0.18f;
    dl->AddLine(ImVec2(mn.x + w * 0.1f, ySeg), ImVec2(c.x - w * 0.12f, ySeg), col, t);
    dl->AddLine(ImVec2(c.x + w * 0.12f, ySeg), ImVec2(mx.x - w * 0.1f, ySeg), col, t);
    break;
  }
  case RibbonIconKind::Offset: {
    // Nested U opening to the right: outer accent, inner primary (AutoCAD-style offset).
    const float m = std::min(w, h);
    const float xc = c.x + m * 0.1f;
    const float yc = c.y;
    const float rOut = m * 0.3f;
    const float rIn = m * 0.21f;
    const float xArm = mx.x - m * 0.1f;
    const int seg = 16;
    auto strokeU = [&](float r, ImU32 clr, float th) {
      for (int i = 0; i < seg; ++i) {
        const float u0 = 3.14159265f * 0.5f + (float)i / (float)seg * 3.14159265f;
        const float u1 = 3.14159265f * 0.5f + (float)(i + 1) / (float)seg * 3.14159265f;
        const ImVec2 p0(xc + std::cos(u0) * r, yc - std::sin(u0) * r);
        const ImVec2 p1(xc + std::cos(u1) * r, yc - std::sin(u1) * r);
        dl->AddLine(p0, p1, clr, th);
      }
      const float yTop = yc - r;
      const float yBot = yc + r;
      dl->AddLine(ImVec2(xc, yTop), ImVec2(xArm, yTop), clr, th);
      dl->AddLine(ImVec2(xc, yBot), ImVec2(xArm, yBot), clr, th);
    };
    strokeU(rOut, acc, t * 1.08f);
    strokeU(rIn, col, t);
    break;
  }
  case RibbonIconKind::ZoomExtents: {
    // X with outward arrows on three arms + magnifier on bottom-right (monochrome).
    const float inv = 0.70710678f;
    const float L = std::min(w, h) * 0.36f;
    const float cx = c.x, cy = c.y;
    const ImVec2 pTL(cx - inv * L, cy - inv * L);
    const ImVec2 pTR(cx + inv * L, cy - inv * L);
    const ImVec2 pBL(cx - inv * L, cy + inv * L);
    const float head = std::min(w, h) * 0.1f;

    dl->AddLine(pTR, pBL, col, t);
    RibbonStrokeArrow(dl, pTR, ImVec2(inv, -inv), head, col, t);
    RibbonStrokeArrow(dl, pBL, ImVec2(-inv, inv), head, col, t);

    const ImVec2 dBR(inv, inv);
    const float glassR = std::min(w, h) * 0.1f;
    const float glassDist = L * 0.58f;
    const ImVec2 glassC(cx + dBR.x * glassDist, cy + dBR.y * glassDist);
    const ImVec2 glassEdgeIn(glassC.x - dBR.x * glassR, glassC.y - dBR.y * glassR);
    dl->AddLine(pTL, glassEdgeIn, col, t);
    RibbonStrokeArrow(dl, pTL, ImVec2(-inv, -inv), head, col, t);

    dl->AddCircle(glassC, glassR, col, 20, t);
    const float hlen = std::min(w, h) * 0.14f;
    const ImVec2 h0(glassC.x + dBR.x * glassR * 0.75f, glassC.y + dBR.y * glassR * 0.75f);
    const ImVec2 h1(h0.x + dBR.x * hlen, h0.y + dBR.y * hlen);
    dl->AddLine(h0, h1, col, t);
    break;
  }
  case RibbonIconKind::ZoomWindow: {
    // Window rectangle + magnifier overlapping bottom-right corner (monochrome).
    const float pad = std::min(w, h) * 0.1f;
    const ImVec2 sq0(mn.x + pad, mn.y + pad);
    const ImVec2 sq1(mx.x - pad, mx.y - pad);
    dl->AddRect(sq0, sq1, col, 0.f, 0, t);
    const float inv = 0.70710678f;
    const ImVec2 dBR(inv, inv);
    const float glassR = std::min(w, h) * 0.11f;
    const ImVec2 glassC(sq1.x - glassR * 0.5f, sq1.y - glassR * 0.5f);
    dl->AddCircle(glassC, glassR, col, 20, t);
    const float hlen = std::min(w, h) * 0.13f;
    const ImVec2 h0(glassC.x + dBR.x * glassR * 0.72f, glassC.y + dBR.y * glassR * 0.72f);
    const ImVec2 h1(h0.x + dBR.x * hlen, h0.y + dBR.y * hlen);
    dl->AddLine(h0, h1, col, t);
    break;
  }
  case RibbonIconKind::Scale: {
    // Overlapping squares: larger accent outline (up-right) + smaller white (down-left).
    const float m = std::min(w, h);
    const ImVec2 shift(m * 0.13f, -m * 0.11f);
    const float big = m * 0.44f;
    const float sml = m * 0.27f;
    const ImVec2 cBig(c.x + shift.x, c.y + shift.y);
    const ImVec2 cSml(c.x - shift.x * 0.7f, c.y - shift.y * 0.55f);
    const ImVec2 B0(cBig.x - big * 0.5f, cBig.y - big * 0.5f);
    const ImVec2 B1(cBig.x + big * 0.5f, cBig.y + big * 0.5f);
    const ImVec2 S0(cSml.x - sml * 0.5f, cSml.y - sml * 0.5f);
    const ImVec2 S1(cSml.x + sml * 0.5f, cSml.y + sml * 0.5f);
    dl->AddRect(B0, B1, acc, 0.f, 0, t);
    dl->AddRect(S0, S1, col, 0.f, 0, t);
    break;
  }
  case RibbonIconKind::Mirror: {
    // Shared vertical mirror + left triangle (white) + right triangle (orange outline only).
    const float xm = c.x;
    const float yT = mn.y + h * 0.2f;
    const float yB = mx.y - h * 0.2f;
    const float xL = mn.x + w * 0.14f;
    const float xR = mx.x - w * 0.14f;
    dl->AddLine(ImVec2(xm, yT), ImVec2(xm, yB), col, t * 1.1f);
    dl->AddLine(ImVec2(xm, yT), ImVec2(xL, c.y), col, t);
    dl->AddLine(ImVec2(xL, c.y), ImVec2(xm, yB), col, t);
    dl->AddLine(ImVec2(xm, yT), ImVec2(xR, c.y), acc, t);
    dl->AddLine(ImVec2(xR, c.y), ImVec2(xm, yB), acc, t);
    break;
  }
  case RibbonIconKind::SurveyPoint: {
    dl->AddLine(ImVec2(c.x, mn.y + h * 0.15f), ImVec2(c.x, mx.y - h * 0.15f), col, t * 0.75f);
    dl->AddLine(ImVec2(mn.x + w * 0.15f, c.y), ImVec2(mx.x - w * 0.15f, c.y), col, t * 0.75f);
    dl->AddCircleFilled(c, std::min(w, h) * 0.12f, col, 16);
    dl->AddCircle(c, std::min(w, h) * 0.12f, col, 16, t);
    break;
  }
  case RibbonIconKind::SurveyInverse: {
    const float r = std::min(w, h) * 0.07f;
    const ImVec2 p0(mn.x + w * 0.22f, mx.y - h * 0.28f);
    const ImVec2 p1(mx.x - w * 0.22f, mn.y + h * 0.28f);
    dl->AddCircleFilled(p0, r, col, 12);
    dl->AddCircleFilled(p1, r, col, 12);
    dl->AddLine(p0, p1, acc, t * 1.05f);
    const ImVec2 d(p1.x - p0.x, p1.y - p0.y);
    const float len = std::sqrt(d.x * d.x + d.y * d.y);
    if (len > 1e-4f) {
      const float inv = 1.f / len;
      RibbonStrokeArrow(dl, p1, ImVec2(d.x * inv, d.y * inv), std::clamp(std::min(w, h) * 0.1f, 2.5f, 5.5f), acc,
                         t);
    }
    break;
  }
  case RibbonIconKind::Layers: {
    for (int i = 0; i < 3; ++i) {
      const float y = mn.y + h * (0.18f + static_cast<float>(i) * 0.22f);
      const float inset = static_cast<float>(i) * w * 0.06f;
      dl->AddLine(ImVec2(mn.x + w * 0.1f + inset, y), ImVec2(mx.x - w * 0.1f - inset * 0.3f, y), col, t);
    }
    break;
  }
  case RibbonIconKind::PdfShowBg: {
    // Filled page = raster background visible
    const ImVec2 tl2(mn.x + w * 0.15f, mn.y + h * 0.12f);
    const ImVec2 br2(mx.x - w * 0.15f, mx.y - h * 0.12f);
    dl->AddRectFilled(tl2, br2, IM_COL32(60, 70, 80, 160));
    dl->AddRect(tl2, br2, col, 0.f, 0, t);
    for (int li = 0; li < 3; ++li) {
      const float ly = tl2.y + (br2.y - tl2.y) * (0.25f + static_cast<float>(li) * 0.27f);
      dl->AddLine({tl2.x + w * 0.07f, ly}, {br2.x - w * 0.07f, ly}, col, t * 0.75f);
    }
    break;
  }
  case RibbonIconKind::PdfHideBg: {
    // Hollow page with diagonal strikethrough = raster hidden
    const ImVec2 tl2(mn.x + w * 0.15f, mn.y + h * 0.12f);
    const ImVec2 br2(mx.x - w * 0.15f, mx.y - h * 0.12f);
    dl->AddRect(tl2, br2, col, 0.f, 0, t * 0.5f);
    dl->AddLine(tl2, br2, IM_COL32(200, 80, 80, 200), t);
    dl->AddLine({tl2.x, br2.y}, {br2.x, tl2.y}, IM_COL32(200, 80, 80, 200), t);
    break;
  }
  case RibbonIconKind::PdfVectorize: {
    // Three dots (left) → arrow → three crisp lines (right)
    const float dotR = std::max(1.5f, w * 0.045f);
    for (int di = 0; di < 3; ++di) {
      const float y = c.y + (di - 1) * h * 0.22f;
      dl->AddCircleFilled({c.x - w * 0.30f, y}, dotR, col, 8);
    }
    dl->AddLine({c.x - w * 0.08f, c.y}, {c.x + w * 0.04f, c.y}, col, t);
    dl->AddTriangleFilled({c.x + w * 0.04f, c.y},
                          {c.x - w * 0.02f, c.y - h * 0.07f},
                          {c.x - w * 0.02f, c.y + h * 0.07f}, col);
    for (int li = 0; li < 3; ++li) {
      const float y = c.y + (li - 1) * h * 0.22f;
      dl->AddLine({c.x + w * 0.16f, y}, {c.x + w * 0.38f, y}, col, t);
    }
    break;
  }
  case RibbonIconKind::PdfAttach: {
    // Page outline with folded top-right corner, plus three content lines.
    const float pl = mn.x + w * 0.18f;
    const float pr = mx.x - w * 0.18f;
    const float pt = mn.y + h * 0.10f;
    const float pb = mx.y - h * 0.10f;
    const float fold = std::min(w, h) * 0.18f;
    // Page border (without top-right corner segment).
    dl->AddLine(ImVec2(pl, pt),          ImVec2(pr - fold, pt),  col, t);
    dl->AddLine(ImVec2(pr - fold, pt),   ImVec2(pr, pt + fold),  col, t);
    dl->AddLine(ImVec2(pr, pt + fold),   ImVec2(pr, pb),         col, t);
    dl->AddLine(ImVec2(pr, pb),          ImVec2(pl, pb),         col, t);
    dl->AddLine(ImVec2(pl, pb),          ImVec2(pl, pt),         col, t);
    // Three horizontal content lines.
    const float lx0 = pl + w * 0.10f;
    const float lx1 = pr - w * 0.10f;
    for (int li = 0; li < 3; ++li) {
      const float ly = pt + fold + (pb - pt - fold) * (0.20f + static_cast<float>(li) * 0.28f);
      dl->AddLine(ImVec2(lx0, ly), ImVec2(lx1, ly), col, t * 0.85f);
    }
    break;
  }
  case RibbonIconKind::Undo: {
    // Nearly-complete circle going CCW (in screen space), gap at top, arrowhead at 1-o'clock end.
    const float kPi = 3.14159265f;
    const float r2 = std::min(w, h) * 0.30f;
    const float thick2 = std::clamp(std::min(w, h) * 0.095f, 2.f, 5.5f);
    const float arrowH = std::min(w, h) * 0.20f;
    const float gapHalf = 0.42f; // ~24° each side
    const float gapCtr = kPi * 1.5f; // 12 o'clock in ImGui (Y-down, sin(3PI/2)<0 = up)
    // CCW arc: starts at 11-o'clock, decreasing-angle for ~316°, ends at 1-o'clock
    const float a0 = gapCtr - gapHalf;
    const float a1 = a0 - (2.f * kPi - 2.f * gapHalf);
    dl->PathArcTo(c, r2, a0, a1, 32);
    dl->PathStroke(col, 0, thick2);
    // Arrowhead at end (a1 ≈ 1-o'clock), CCW tangent = (sin θ, -cos θ)
    const float etx = std::sin(a1), ety = -std::cos(a1);
    const ImVec2 tip2 = {c.x + r2 * std::cos(a1), c.y + r2 * std::sin(a1)};
    const ImVec2 bas2 = {tip2.x - etx * arrowH, tip2.y - ety * arrowH};
    dl->AddTriangleFilled(tip2,
      {bas2.x + ety * arrowH * 0.45f, bas2.y - etx * arrowH * 0.45f},
      {bas2.x - ety * arrowH * 0.45f, bas2.y + etx * arrowH * 0.45f}, col);
    break;
  }
  case RibbonIconKind::Redo: {
    // Nearly-complete circle going CW (in screen space), gap at top, arrowhead at 11-o'clock end.
    const float kPi = 3.14159265f;
    const float r2 = std::min(w, h) * 0.30f;
    const float thick2 = std::clamp(std::min(w, h) * 0.095f, 2.f, 5.5f);
    const float arrowH = std::min(w, h) * 0.20f;
    const float gapHalf = 0.42f;
    const float gapCtr = kPi * 1.5f;
    // CW arc: starts at 1-o'clock, increasing-angle for ~316°, ends at 11-o'clock
    const float a0 = gapCtr + gapHalf;
    const float a1 = a0 + (2.f * kPi - 2.f * gapHalf);
    dl->PathArcTo(c, r2, a0, a1, 32);
    dl->PathStroke(col, 0, thick2);
    // Arrowhead at end (a1 ≈ 11-o'clock), CW tangent = (-sin θ, cos θ)
    const float etx = -std::sin(a1), ety = std::cos(a1);
    const ImVec2 tip2 = {c.x + r2 * std::cos(a1), c.y + r2 * std::sin(a1)};
    const ImVec2 bas2 = {tip2.x - etx * arrowH, tip2.y - ety * arrowH};
    dl->AddTriangleFilled(tip2,
      {bas2.x + ety * arrowH * 0.45f, bas2.y - etx * arrowH * 0.45f},
      {bas2.x - ety * arrowH * 0.45f, bas2.y + etx * arrowH * 0.45f}, col);
    break;
  }
  case RibbonIconKind::ClipboardCopy: {
    // Clipboard body + small "C" copy-to arrow (copy selection → clipboard)
    const float cpl = mn.x + w * 0.22f, cpr = mx.x - w * 0.22f;
    const float cpt = mn.y + h * 0.20f, cpb = mx.y - h * 0.14f;
    const float clipW = (cpr - cpl) * 0.32f;
    const float clipH = h * 0.12f;
    const float clipL = c.x - clipW * 0.5f, clipR = c.x + clipW * 0.5f;
    // Clipboard body (rounded rect)
    dl->AddRect(ImVec2(cpl, cpt + clipH * 0.6f), ImVec2(cpr, cpb), col, 2.f, 0, t);
    // Clip at top-center
    dl->AddRectFilled(ImVec2(clipL, cpt), ImVec2(clipR, cpt + clipH), col, 1.f);
    // Horizontal content lines on the clipboard
    const float lineY1 = cpt + h * 0.37f, lineY2 = cpt + h * 0.50f;
    dl->AddLine(ImVec2(cpl + w * 0.08f, lineY1), ImVec2(cpr - w * 0.08f, lineY1), col, t * 0.65f);
    dl->AddLine(ImVec2(cpl + w * 0.08f, lineY2), ImVec2(cpr - w * 0.08f, lineY2), col, t * 0.65f);
    // Small "copy" arrow in accent color (up-right corner)
    const float ax = cpr - w * 0.04f, ay = cpt + h * 0.05f;
    const float ahead = std::min(w, h) * 0.10f;
    RibbonStrokeArrow(dl, ImVec2(ax, ay), ImVec2(0.7f, -0.7f), ahead, acc, t * 0.9f);
    break;
  }
  case RibbonIconKind::ClipboardPaste: {
    // Clipboard body + downward arrow (paste from clipboard → drawing)
    const float cpl = mn.x + w * 0.22f, cpr = mx.x - w * 0.22f;
    const float cpt = mn.y + h * 0.16f, cpb = mx.y - h * 0.10f;
    const float clipW = (cpr - cpl) * 0.32f;
    const float clipH = h * 0.12f;
    const float clipL = c.x - clipW * 0.5f, clipR = c.x + clipW * 0.5f;
    dl->AddRect(ImVec2(cpl, cpt + clipH * 0.6f), ImVec2(cpr, cpb), col, 2.f, 0, t);
    dl->AddRectFilled(ImVec2(clipL, cpt), ImVec2(clipR, cpt + clipH), col, 1.f);
    // Downward arrow in accent color — paste direction
    const float arrowX = c.x, arrowYtop = cpt + clipH * 0.5f, arrowYbot = cpb - h * 0.04f;
    dl->AddLine(ImVec2(arrowX, arrowYtop + h * 0.08f), ImVec2(arrowX, arrowYbot - h * 0.12f), acc, t * 1.1f);
    const float ahead = std::min(w, h) * 0.12f;
    RibbonStrokeArrow(dl, ImVec2(arrowX, arrowYbot - h * 0.04f), ImVec2(0.f, 1.f), ahead, acc, t);
    break;
  }
  case RibbonIconKind::Traverse: {
    // Traverse icon: four dots connected by a zigzag path representing a traverse loop.
    const float r = std::min(w, h) * 0.065f;
    const ImVec2 p0(mn.x + w * 0.20f, mx.y - h * 0.24f);
    const ImVec2 p1(mx.x - w * 0.24f, mx.y - h * 0.20f);
    const ImVec2 p2(mx.x - w * 0.18f, mn.y + h * 0.28f);
    const ImVec2 p3(mn.x + w * 0.22f, mn.y + h * 0.22f);
    dl->AddLine(p0, p1, col, t);
    dl->AddLine(p1, p2, col, t);
    dl->AddLine(p2, p3, col, t);
    dl->AddLine(p3, p0, acc, t * 0.65f); // closing leg (lighter, dashed effect via alpha)
    for (const auto& p : {p0, p1, p2, p3})
      dl->AddCircleFilled(p, r, col, 10);
    break;
  }
  default:
    break;
  }
}

// ---------------------------------------------------------------------------
// Bitmap toolbar icons — PNGs from resources/icons/, with the vector art as a
// fallback if a file is missing. Filenames match RibbonIconColor categories.
// ---------------------------------------------------------------------------

static const char* RibbonIconName(RibbonIconKind k) {
  switch (k) {
  case RibbonIconKind::Line:           return "line";
  case RibbonIconKind::Circle:         return "circle";
  case RibbonIconKind::Polyline:       return "polyline";
  case RibbonIconKind::Arc:            return "arc";
  case RibbonIconKind::Ellipse:        return "ellipse";
  case RibbonIconKind::Dim:            return "dim";
  case RibbonIconKind::DimLinear:      return "dimlinear";
  case RibbonIconKind::Id:             return "id";
  case RibbonIconKind::Text:           return "text";
  case RibbonIconKind::Mtext:          return "mtext";
  case RibbonIconKind::Move:           return "move";
  case RibbonIconKind::Copy:           return "copy";
  case RibbonIconKind::Rotate:         return "rotate";
  case RibbonIconKind::Erase:          return "erase";
  case RibbonIconKind::Join:           return "join";
  case RibbonIconKind::Trim:           return "trim";
  case RibbonIconKind::Offset:         return "offset";
  case RibbonIconKind::ZoomExtents:    return "zoomextents";
  case RibbonIconKind::ZoomWindow:     return "zoomwindow";
  case RibbonIconKind::Scale:          return "scale";
  case RibbonIconKind::Mirror:         return "mirror";
  case RibbonIconKind::SurveyPoint:    return "surveypoint";
  case RibbonIconKind::SurveyInverse:  return "surveyinverse";
  case RibbonIconKind::Layers:         return "layers";
  case RibbonIconKind::PdfAttach:      return "pdfattach";
  case RibbonIconKind::PdfShowBg:      return "pdfshowbg";
  case RibbonIconKind::PdfHideBg:      return "pdfhidebg";
  case RibbonIconKind::PdfVectorize:   return "pdfvectorize";
  case RibbonIconKind::Undo:           return "undo";
  case RibbonIconKind::Redo:           return "redo";
  case RibbonIconKind::ClipboardCopy:  return "clipboardcopy";
  case RibbonIconKind::ClipboardPaste: return "clipboardpaste";
  case RibbonIconKind::Traverse:       return "traverse";
  }
  return "";
}

static ImTextureID g_ribbonIconTex[static_cast<int>(RibbonIconKind::Traverse) + 1] = {};
static bool g_ribbonIconsLoaded = false;

static void EnsureRibbonIconsLoaded() {
  if (g_ribbonIconsLoaded) return;
  g_ribbonIconsLoaded = true;  // attempt once; missing files fall back to vector art
  for (int i = 0; i <= static_cast<int>(RibbonIconKind::Traverse); ++i) {
    const std::string nm = RibbonIconName(static_cast<RibbonIconKind>(i));
    if (nm.empty()) continue;
    const std::filesystem::path p =
        ResolveBundledAssetPath(std::filesystem::path("resources") / "icons" / (nm + ".png"));
    if (p.empty()) continue;
    const unsigned int tex = LoadIconTextureRgba(p);
    if (tex) g_ribbonIconTex[i] = static_cast<ImTextureID>(static_cast<intptr_t>(tex));
  }
}

// Map an UPPERCASE command name to a ribbon icon, for the command autocomplete list.
static bool CommandIconKind(const std::string& upperName, RibbonIconKind* out) {
  struct M { const char* n; RibbonIconKind k; };
  static const M m[] = {
    {"LINE", RibbonIconKind::Line}, {"CIRCLE", RibbonIconKind::Circle}, {"POLYLINE", RibbonIconKind::Polyline},
    {"ARC", RibbonIconKind::Arc}, {"ELLIPSE", RibbonIconKind::Ellipse}, {"TEXT", RibbonIconKind::Text},
    {"MTEXT", RibbonIconKind::Mtext}, {"DIMALIGNED", RibbonIconKind::Dim}, {"DIMLINEAR", RibbonIconKind::DimLinear},
    {"ID", RibbonIconKind::Id}, {"INVERSE", RibbonIconKind::SurveyInverse}, {"MOVE", RibbonIconKind::Move},
    {"COPY", RibbonIconKind::Copy}, {"ROTATE", RibbonIconKind::Rotate}, {"SCALE", RibbonIconKind::Scale},
    {"DELETE", RibbonIconKind::Erase}, {"JOIN", RibbonIconKind::Join}, {"TRIM", RibbonIconKind::Trim},
    {"OFFSET", RibbonIconKind::Offset}, {"ZOOMEXTENTS", RibbonIconKind::ZoomExtents},
    {"ZOOMWINDOW", RibbonIconKind::ZoomWindow}, {"CREATEPOINTS", RibbonIconKind::SurveyPoint},
    {"VIEWPOINTS", RibbonIconKind::SurveyPoint}, {"LAYER", RibbonIconKind::Layers},
    {"PDFATTACH", RibbonIconKind::PdfAttach}, {"PASTE", RibbonIconKind::ClipboardPaste},
    {"PASTEORIG", RibbonIconKind::ClipboardPaste},
  };
  for (const M& e : m)
    if (upperName == e.n) { *out = e.k; return true; }
  return false;
}

// Classic Win32/nanoCAD 3D button background: flat at rest; raised bevel on
// hover (light top-left, dark bottom-right); sunken bevel when pressed.
static void DrawRibbonButtonBevel(ImDrawList* dl, const ImRect& bb, bool sunken) {
  const ImU32 face = sunken ? kBandSunken : IM_COL32(240, 240, 235, 255);  // raised face slightly lighter than band
  dl->AddRectFilled(bb.Min, bb.Max, face, 0.f);
  const ImU32 tl = sunken ? kBandShadow : kBandHilite;  // top-left edge
  const ImU32 br = sunken ? kBandHilite : kBandShadow;  // bottom-right edge
  dl->AddLine(ImVec2(bb.Min.x, bb.Min.y + 0.5f), ImVec2(bb.Max.x - 1.f, bb.Min.y + 0.5f), tl, 1.f);
  dl->AddLine(ImVec2(bb.Min.x + 0.5f, bb.Min.y), ImVec2(bb.Min.x + 0.5f, bb.Max.y - 1.f), tl, 1.f);
  dl->AddLine(ImVec2(bb.Min.x, bb.Max.y - 0.5f), ImVec2(bb.Max.x, bb.Max.y - 0.5f), br, 1.f);
  dl->AddLine(ImVec2(bb.Max.x - 0.5f, bb.Min.y), ImVec2(bb.Max.x - 0.5f, bb.Max.y), br, 1.f);
}

// Draw an icon (bitmap if loaded, else procedural fallback) centered as a
// square inside [iconMin, iconMax], dimmed by the current style alpha.
static void DrawRibbonIconArt(ImDrawList* dl, RibbonIconKind icon, const ImVec2& iconMin, const ImVec2& iconMax) {
  if (iconMax.y <= iconMin.y + 2.f || iconMax.x <= iconMin.x + 2.f)
    return;
  EnsureRibbonIconsLoaded();
  const ImTextureID tex = g_ribbonIconTex[static_cast<int>(icon)];
  if (tex) {
    const float side = std::min(iconMax.x - iconMin.x, iconMax.y - iconMin.y);
    const ImVec2 ctr((iconMin.x + iconMax.x) * 0.5f, (iconMin.y + iconMax.y) * 0.5f);
    const ImVec2 a(ctr.x - side * 0.5f, ctr.y - side * 0.5f);
    const ImVec2 b(ctr.x + side * 0.5f, ctr.y + side * 0.5f);
    const int alpha = static_cast<int>(ImGui::GetStyle().Alpha * 255.f);
    dl->AddImage(tex, a, b, ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, alpha));
  } else {
    const ImU32 fg = ImGui::GetColorU32(ImGuiCol_Text);
    PaintRibbonIcon(dl, iconMin, iconMax, icon, fg, false);
  }
}

// Where a button's text label sits relative to its icon.
enum class RibbonLabel { None, Right, Below };

// Flexible ribbon button: icon-only (None), icon + label to the right (Right),
// or a large icon with the label centered below (Below). Shares the 3D bevel
// and icon art with every ribbon button so states stay consistent.
static bool RibbonButtonEx(const char* str_id, RibbonIconKind icon, const char* label,
                           const ImVec2& size, RibbonLabel mode) {
  ImGuiWindow* window = ImGui::GetCurrentWindow();
  if (window->SkipItems)
    return false;

  const ImGuiID id = window->GetID(str_id);
  const ImVec2 pos = window->DC.CursorPos;
  const ImRect bb(pos, ImVec2(pos.x + size.x, pos.y + size.y));
  ImGui::ItemSize(size, 0.f);
  if (!ImGui::ItemAdd(bb, id))
    return false;

  bool hovered = false;
  bool held = false;
  const bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held, ImGuiButtonFlags_None);
  ImGui::RenderNavCursor(bb, id);

  ImDrawList* dl = window->DrawList;
  const bool sunken = held && hovered;
  float shift = 0.f;
  if (hovered || held) {
    DrawRibbonButtonBevel(dl, bb, sunken);
    if (sunken)
      shift = 1.f;
  }

  const bool hasLabel = (label && label[0] && mode != RibbonLabel::None);
  const ImVec2 ts = hasLabel ? ImGui::CalcTextSize(label) : ImVec2(0.f, 0.f);
  const ImU32 textCol = ImGui::GetColorU32(ImGuiCol_Text);
  constexpr float pad = 3.f;

  ImVec2 iconMin, iconMax, labelPos;
  if (mode == RibbonLabel::Below && hasLabel) {
    const float iconArea = size.y - ts.y - pad * 2.f - 1.f;
    const float sideMax = std::min(size.x - pad * 2.f, iconArea);
    const ImVec2 ctr(bb.Min.x + size.x * 0.5f + shift, bb.Min.y + pad + iconArea * 0.5f + shift);
    iconMin = ImVec2(ctr.x - sideMax * 0.5f, ctr.y - sideMax * 0.5f);
    iconMax = ImVec2(ctr.x + sideMax * 0.5f, ctr.y + sideMax * 0.5f);
    labelPos = ImVec2(bb.Min.x + (size.x - ts.x) * 0.5f + shift, bb.Max.y - ts.y - pad + shift);
  } else if (mode == RibbonLabel::Right && hasLabel) {
    const float side = size.y - pad * 2.f;
    iconMin = ImVec2(bb.Min.x + pad + shift, bb.Min.y + pad + shift);
    iconMax = ImVec2(iconMin.x + side, iconMin.y + side);
    labelPos = ImVec2(iconMax.x + 4.f + shift, bb.Min.y + (size.y - ts.y) * 0.5f + shift);
  } else {
    const float side = std::min(size.x, size.y) - pad * 2.f;
    const ImVec2 ctr(bb.Min.x + size.x * 0.5f + shift, bb.Min.y + size.y * 0.5f + shift);
    iconMin = ImVec2(ctr.x - side * 0.5f, ctr.y - side * 0.5f);
    iconMax = ImVec2(ctr.x + side * 0.5f, ctr.y + side * 0.5f);
  }

  DrawRibbonIconArt(dl, icon, iconMin, iconMax);
  if (hasLabel)
    dl->AddText(labelPos, textCol, label);

  return pressed;
}


static void RibbonItemHelp(const char* text, ImGuiHoveredFlags extraFlags = 0) {
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | extraFlags) && ImGui::BeginTooltip()) {
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 26.f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
  }
}

void DrawRibbonBar(float height, AppCommandState& cmd, std::vector<std::string>& log) {
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 3));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(5, 4));
  // nanoCAD-style flat system-gray toolbar band; icons float on it as 3D buttons.
  ImGui::PushStyleColor(ImGuiCol_ChildBg, kBandFace);
  ImGui::BeginChild("RibbonStrip", ImVec2(0, height), true,
                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  ImGui::PopStyleColor();

  const ImGuiStyle& st = ImGui::GetStyle();
  const float panelH = height - st.WindowPadding.y * 2.f;
  constexpr float kLayerPanelW = 500.f;

  // Civil 3D-style panel metrics: a button column fills the height above the
  // bottom title; small labeled buttons stack 3 to a column; the icon grid
  // uses 2 rows of square cells.
  const float colH     = std::max(48.f, RibbonPanelContentH(panelH) - 4.f);
  const float rowH     = std::floor((colH - 4.f) / 3.f);
  const float gridCell = std::floor((colH - 2.f) / 2.f);
  constexpr float largeW = 60.f;

  auto largeBtn = [&](const char* id, RibbonIconKind ic, const char* label) {
    return RibbonButtonEx(id, ic, label, ImVec2(largeW, colH), RibbonLabel::Below);
  };
  auto smallBtn = [&](const char* id, RibbonIconKind ic, const char* label, float w) {
    return RibbonButtonEx(id, ic, label, ImVec2(w, rowH), RibbonLabel::Right);
  };
  auto gridBtn = [&](const char* id, RibbonIconKind ic) {
    return RibbonButtonEx(id, ic, nullptr, ImVec2(gridCell, gridCell), RibbonLabel::None);
  };
  // Column width = small icon + gap + the widest label in the column.
  auto colW = [&](std::initializer_list<const char*> labels) {
    float m = 0.f;
    for (const char* l : labels)
      m = std::max(m, ImGui::CalcTextSize(l).x);
    return rowH + 8.f + m;
  };

  const float wEdit = 8.f + largeW + 4.f + colW({"Copy", "Undo", "Redo"});
  const float wDraw = 8.f + gridCell * 3.f + 4.f * 2.f;
  const float wMod  = 8.f + largeW + 4.f + colW({"Copy", "Rotate", "Scale"}) + 4.f +
                      colW({"Erase", "Trim", "Offset"}) + 4.f + colW({"Join", "Mirror"});
  const float wAnn  = 8.f + colW({"Text", "Mtext"});
  const float wInq  = 8.f + colW({"Aligned", "Linear", "ID Point"});
  const float wSrv  = 8.f + largeW + 4.f + colW({"Inverse", "Traverse"});
  const float wView = 8.f + colW({"Extents", "Window"});
  // REQ-032 contextual ribbon: Layout tools in paper space, but the normal model ribbon while editing a
  // viewport in place (floating model space, REQ-036) so the draw/modify tools are available.
  const bool  ribbonPaperSpace = cmd.activeSpaceIndex != kModelSpaceIndex && !InFloatingModelSpace(cmd);
  const float wLayout = 8.f + largeW + 4.f + colW({"Poly VP"}) + 8.f + largeW + 4.f + colW({"Batch"});

  ImGui::PushStyleColor(ImGuiCol_ChildBg, kBandFace);
  ImGui::BeginChild("RibbonToolsLeft", ImVec2(-kLayerPanelW - st.ItemSpacing.x, panelH), false,
                    ImGuiWindowFlags_HorizontalScrollbar);
  ImGui::PopStyleColor();

  RibbonSectionBegin("RibbonSecUndo", "Edit", wEdit, panelH);
  {
    const bool canUndo = CanUndo(cmd);
    const bool canRedo = CanRedo(cmd);
    const bool hasSelection = !cmd.selection.empty() || !cmd.selectedSurveyPointIndices.empty();
    const bool hasClipboard = !cmd.clipboard.empty();
    const float cw = colW({"Copy", "Undo", "Redo"});

    if (!hasClipboard)
      ImGui::BeginDisabled();
    if (largeBtn("##RibbonClipPaste", RibbonIconKind::ClipboardPaste, "Paste"))
      StartPasteCommand(cmd, log);
    if (!hasClipboard)
      ImGui::EndDisabled();
    RibbonItemHelp("Paste (Ctrl+V) — place clipboard objects at cursor position.\nRight-click Edit menu for Paste at Original Coordinates.",
                   hasClipboard ? ImGuiHoveredFlags_None : ImGuiHoveredFlags_AllowWhenDisabled);

    ImGui::SameLine(0, 4);
    ImGui::BeginGroup();
    if (!hasSelection)
      ImGui::BeginDisabled();
    if (smallBtn("##RibbonClipCopy", RibbonIconKind::ClipboardCopy, "Copy", cw))
      CopySelectionToClipboard(cmd, log);
    if (!hasSelection)
      ImGui::EndDisabled();
    RibbonItemHelp("Copy (Ctrl+C) — copy selected objects to clipboard.\nPaste later with Ctrl+V or the Paste button.",
                   hasSelection ? ImGuiHoveredFlags_None : ImGuiHoveredFlags_AllowWhenDisabled);
    if (!canUndo)
      ImGui::BeginDisabled();
    if (smallBtn("##RibbonUndo", RibbonIconKind::Undo, "Undo", cw))
      DoUndo(cmd, log);
    if (!canUndo)
      ImGui::EndDisabled();
    RibbonItemHelp("Undo (Ctrl+Z) — restore previous state.", canUndo ? ImGuiHoveredFlags_None : ImGuiHoveredFlags_AllowWhenDisabled);
    if (!canRedo)
      ImGui::BeginDisabled();
    if (smallBtn("##RibbonRedo", RibbonIconKind::Redo, "Redo", cw))
      DoRedo(cmd, log);
    if (!canRedo)
      ImGui::EndDisabled();
    RibbonItemHelp("Redo (Ctrl+Shift+Z) — restore next state.", canRedo ? ImGuiHoveredFlags_None : ImGuiHoveredFlags_AllowWhenDisabled);
    ImGui::EndGroup();
  }
  RibbonSectionEnd();
  ImGui::SameLine(0, 8);

  if (!ribbonPaperSpace) {
  RibbonSectionBegin("RibbonSecDraw", "Draw", wDraw, panelH);
  {
    if (gridBtn("##RibbonLine", RibbonIconKind::Line))
      StartLineCommand(cmd, log);
    RibbonItemHelp("Line — draw straight segments between points.\nCommand bar: LINE or L");
    ImGui::SameLine(0, 4);
    if (gridBtn("##RibbonCircle", RibbonIconKind::Circle))
      StartCircleCommand(cmd, log);
    RibbonItemHelp("Circle — center point and radius.\nCommand bar: CIRCLE or C");
    ImGui::SameLine(0, 4);
    if (gridBtn("##RibbonPLine", RibbonIconKind::Polyline))
      StartPolylineCommand(cmd, log);
    RibbonItemHelp("Polyline — chain of segments; optional close.\nCommand bar: POLYLINE or PL");
    if (gridBtn("##RibbonArc", RibbonIconKind::Arc))  // wraps to the second row
      StartArcCommand(cmd, log);
    RibbonItemHelp("Arc — three-point arc (start, mid, end).\nCommand bar: ARC");
    ImGui::SameLine(0, 4);
    if (gridBtn("##RibbonEllipse", RibbonIconKind::Ellipse))
      StartEllipseCommand(cmd, log);
    RibbonItemHelp("Ellipse — center, axis endpoint, then ratio on command line.\nCommand bar: ELLIPSE or EL");
    ImGui::SameLine(0, 4);
    if (gridBtn("##RibbonPdfAttach", RibbonIconKind::PdfAttach))
      StartPdfAttachCommand(cmd, log);
    RibbonItemHelp("PDF Attach — attach a PDF page as a raster underlay with snap recognition.\nCommand bar: PDFATTACH");
  }
  RibbonSectionEnd();
  ImGui::SameLine(0, 8);

  RibbonSectionBegin("RibbonSecModify", "Modify", wMod, panelH);
  {
    if (largeBtn("##RibbonMove", RibbonIconKind::Move, "Move"))
      StartMoveCommand(cmd, log);
    RibbonItemHelp("Move — relocate selected entities by base point and offset.\nCommand bar: MOVE or M");

    ImGui::SameLine(0, 4);
    const float c1 = colW({"Copy", "Rotate", "Scale"});
    ImGui::BeginGroup();
    if (smallBtn("##RibbonCopy", RibbonIconKind::Copy, "Copy", c1))
      StartCopyCommand(cmd, log);
    RibbonItemHelp("Copy — duplicate selection with base point and offset.\nCommand bar: COPY or CP");
    if (smallBtn("##RibbonRotate", RibbonIconKind::Rotate, "Rotate", c1))
      StartRotateCommand(cmd, log);
    RibbonItemHelp("Rotate — turn selection around a base point by angle.\nCommand bar: ROTATE or RO");
    if (smallBtn("##RibbonScale", RibbonIconKind::Scale, "Scale", c1))
      StartScaleCommand(cmd, log);
    RibbonItemHelp("Scale — uniform scale about a base point (window-select like Move).\nAfter base: pick scale, "
                     "type factor (>0), or R / REFERENCE for two-point reference length then new length (type or two "
                     "picks).\nCommand bar: SCALE or SC");
    ImGui::EndGroup();

    ImGui::SameLine(0, 4);
    const float c2 = colW({"Erase", "Trim", "Offset"});
    ImGui::BeginGroup();
    if (smallBtn("##RibbonErase", RibbonIconKind::Erase, "Erase", c2))
      StartDeleteCommand(cmd, log);
    RibbonItemHelp("Erase — remove entities (window or crossing selection).\nCommand bar: DELETE or DEL");
    if (smallBtn("##RibbonTrim", RibbonIconKind::Trim, "Trim", c2))
      StartTrimCommand(cmd, log);
    RibbonItemHelp("Trim — shorten segments to cutting edges or drawn trim line.\nCommand bar: TRIM or TR");
    if (smallBtn("##RibbonOffset", RibbonIconKind::Offset, "Offset", c2))
      StartOffsetCommand(cmd, log);
    RibbonItemHelp(
        "Offset — parallel lines, concentric circles/arcs, offset polylines and ellipses.\nCommand bar: OFFSET or O");
    ImGui::EndGroup();

    ImGui::SameLine(0, 4);
    const float c3 = colW({"Join", "Mirror"});
    ImGui::BeginGroup();
    if (smallBtn("##RibbonJoin", RibbonIconKind::Join, "Join", c3))
      StartJoinCommand(cmd, log);
    RibbonItemHelp("Join — merge colinear line segments.\nCommand bar: JOIN or J");
    ImGui::BeginDisabled();
    smallBtn("##RibbonMirror", RibbonIconKind::Mirror, "Mirror", c3);
    RibbonItemHelp("Mirror — flip selection across a mirror line (not implemented yet).\nCommand bar: (none yet)",
                   ImGuiHoveredFlags_AllowWhenDisabled);
    ImGui::EndDisabled();
    ImGui::EndGroup();
  }
  RibbonSectionEnd();
  ImGui::SameLine(0, 8);

  RibbonSectionBegin("RibbonSecAnnotate", "Annotate", wAnn, panelH);
  {
    const float cw = colW({"Text", "Mtext"});
    ImGui::BeginGroup();
    if (smallBtn("##RibbonText", RibbonIconKind::Text, "Text", cw))
      StartTextCommand(cmd, log);
    RibbonItemHelp("Text — single-line annotation at insertion.\nCommand bar: TEXT");
    if (smallBtn("##RibbonMtext", RibbonIconKind::Mtext, "Mtext", cw))
      StartMtextCommand(cmd, log);
    RibbonItemHelp("Mtext — multiline in a frame; after box, edit in the on-drawing editor (Ctrl+Enter reformats; Save to place). Double-click MTEXT to edit.\nCommand bar: MTEXT or MT");
    ImGui::EndGroup();
  }
  RibbonSectionEnd();
  ImGui::SameLine(0, 8);

  RibbonSectionBegin("RibbonSecInquiry", "Inquiry", wInq, panelH);
  {
    const float cw = colW({"Aligned", "Linear", "ID Point"});
    ImGui::BeginGroup();
    if (smallBtn("##RibbonDim", RibbonIconKind::Dim, "Aligned", cw))
      StartDimAlignedCommand(cmd, log);
    RibbonItemHelp("Aligned dimension — extension lines and text.\nCommand bar: DIMALIGNED or DAL");
    if (smallBtn("##RibbonDimLin", RibbonIconKind::DimLinear, "Linear", cw))
      StartDimLinearCommand(cmd, log);
    RibbonItemHelp(
        "Linear dimension — horizontal or vertical distance in X or Y; third pick sets line position (cursor or H/V).\nCommand bar: DIMLINEAR or DLI");
    if (smallBtn("##RibbonId", RibbonIconKind::Id, "ID Point", cw))
      StartIdPointCommand(cmd, log);
    RibbonItemHelp("ID — list UCS (World) X,Y,Z at a point (click or type coordinates).\nCommand bar: ID");
    ImGui::EndGroup();
  }
  RibbonSectionEnd();
  ImGui::SameLine(0, 8);

  RibbonSectionBegin("RibbonSecSurvey", "Survey", wSrv, panelH);
  {
    if (largeBtn("##RibbonPoint", RibbonIconKind::SurveyPoint, "Points"))
      StartCreatePointsCommand(cmd, log);
    RibbonItemHelp(
        "Create points — open the create-points panel and click in the drawing to place survey points.\n"
        "Command bar: CREATEPOINTS or CRTPTS");

    ImGui::SameLine(0, 4);
    const float cw = colW({"Inverse", "Traverse"});
    ImGui::BeginGroup();
    if (smallBtn("##RibbonInverse", RibbonIconKind::SurveyInverse, "Inverse", cw))
      StartSurveyInverseCommand(cmd, log);
    RibbonItemHelp(
        "Inverse — two-point survey leg: horizontal distance and bearing (clockwise from north) in the command log "
        "(World X=Easting, Y=Northing).\nCommand bar: INVERSE or INV");
    if (smallBtn("##RibbonTraverse", RibbonIconKind::Traverse, "Traverse", cw))
      cmd.showTraverseEditorWindow = true;
    RibbonItemHelp("Traverse Editor — enter traverse leg observations (horizontal angles, distances, vertical angles)\nto compute coordinates and closure. Face 1/Face 2 support included.");
    ImGui::EndGroup();
  }
  RibbonSectionEnd();
  ImGui::SameLine(0, 8);
  } else {
    // Layout contextual ribbon (REQ-032): paper-space commands.
    RibbonSectionBegin("RibbonSecLayout", "Layout", wLayout, panelH);
    {
      if (largeBtn("##RibbonRectVp", RibbonIconKind::ZoomWindow, "Rect VP"))
        StartPaperRectViewportCommand(cmd, log);
      RibbonItemHelp("Rectangular viewport — two clicks define a viewport on the sheet.\nCommand bar: MVIEW / RECTVP");
      ImGui::SameLine(0, 4);
      ImGui::BeginGroup();
      const float cwL = colW({"Poly VP"});
      ImGui::BeginDisabled();
      smallBtn("##RibbonPolyVp", RibbonIconKind::Polyline, "Poly VP", cwL);
      ImGui::EndDisabled();
      RibbonItemHelp("Polygonal viewport — coming in a later increment (REQ-034).",
                     ImGuiHoveredFlags_AllowWhenDisabled);
      ImGui::EndGroup();

      ImGui::SameLine(0, 8);
      if (largeBtn("##RibbonPlot", RibbonIconKind::PdfAttach, "Plot"))
        PlotActiveLayout(cmd, log);
      RibbonItemHelp("Plot the current layout to a vector PDF.\nCommand bar: PLOT");
      ImGui::SameLine(0, 4);
      if (smallBtn("##RibbonBatchPlot", RibbonIconKind::PdfAttach, "Batch", colW({"Batch"}))) {
        cmd.batchPlotSelected.clear();
        if (cmd.activeSpaceIndex >= 0)
          cmd.batchPlotSelected.push_back(cmd.activeSpaceIndex);
        cmd.showBatchPlotDialog = true;
      }
      RibbonItemHelp("Batch plot — pick layouts to plot into one multi-page PDF.");
    }
    RibbonSectionEnd();
    ImGui::SameLine(0, 8);
  }

  RibbonSectionBegin("RibbonSecView", "View", wView, panelH);
  {
    const float cw = colW({"Extents", "Window"});
    ImGui::BeginGroup();
    if (smallBtn("##RibbonZExtents", RibbonIconKind::ZoomExtents, "Extents", cw))
      StartZoomExtentsCommand(cmd, log);
    RibbonItemHelp("Zoom extents — fit all drawing content in the view.\nCommand bar: ZOOMEXTENTS or ZE");
    if (smallBtn("##RibbonZWindow", RibbonIconKind::ZoomWindow, "Window", cw))
      StartZoomWindowCommand(cmd, log);
    RibbonItemHelp("Zoom window — zoom to a rectangle you pick with two clicks.\nCommand bar: ZOOMWINDOW or ZW");
    ImGui::EndGroup();
  }
  RibbonSectionEnd();

  // Contextual "PDF Underlay" section — shown when a PDF attachment is selected
  {
    int selPdfCtxIdx = -1;
    for (const auto& e : cmd.selection)
      if (e.type == SelectedEntity::Type::PdfUnderlay) { selPdfCtxIdx = e.index; break; }
    if (!cmd.pdfAttachments.empty() && selPdfCtxIdx >= 0 &&
        selPdfCtxIdx < static_cast<int>(cmd.pdfAttachments.size())) {
    ImGui::SameLine(0, 8);
    PdfAttachment& pdfSel = cmd.pdfAttachments[static_cast<size_t>(selPdfCtxIdx)];
    const float ctrlW     = 185.f;
    const float pdfCw     = colW({"Background", "Vectorize"});
    const float wPdfCtx   = 8.f + pdfCw + 6.f + ctrlW;
    RibbonSectionBegin("RibbonSecPdfCtx", "PDF Underlay", wPdfCtx, panelH);
    {
      ImGui::BeginGroup();
      if (smallBtn("##PdfBgBtn", pdfSel.showBackground ? RibbonIconKind::PdfShowBg : RibbonIconKind::PdfHideBg,
                   "Background", pdfCw))
        pdfSel.showBackground = !pdfSel.showBackground;
      RibbonItemHelp(pdfSel.showBackground
                     ? "Background ON — lines visible, paper transparent.  Click to show paper."
                     : "Background OFF — full raster image visible.  Click to hide paper.");
      if (smallBtn("##PdfVecBtn", RibbonIconKind::PdfVectorize, "Vectorize", pdfCw))
        VectorizePdfAttachmentLines(cmd, selPdfCtxIdx, log);
      RibbonItemHelp("Vectorize Lines — add PDF snap-line geometry as drawing entities on the current layer.");
      ImGui::EndGroup();
      ImGui::SameLine(0, 6);
      ImGui::BeginGroup();
      {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Fade");
        ImGui::SameLine(0, 4.f);
        float fadePct = pdfSel.fade * 100.f;
        ImGui::SetNextItemWidth(ctrlW - 38.f);
        if (ImGui::SliderFloat("##PdfCtxFade", &fadePct, 0.f, 100.f, "%.0f%%"))
          pdfSel.fade = fadePct / 100.f;
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Snap:");
        ImGui::SameLine(0, 3.f);
        ImGui::Checkbox("L##pcs", &pdfSel.snapLines);
        ImGui::SameLine(0, 3.f);
        ImGui::Checkbox("C##pcs", &pdfSel.snapCircles);
        ImGui::SameLine(0, 3.f);
        ImGui::Checkbox("T##pcs", &pdfSel.snapText);
      }
      ImGui::EndGroup();
    }
    RibbonSectionEnd();
    } // selPdfCtxIdx >= 0
  } // contextual PDF block

  ImGui::EndChild();

  ImGui::SameLine(0, st.ItemSpacing.x);
  RibbonSectionBegin("RibbonLayerStrip", "Layers", kLayerPanelW, panelH);
  {
    std::vector<std::string> layerList;
    CollectAllDrawingLayers(cmd, &layerList);
    if (std::find(layerList.begin(), layerList.end(), cmd.currentLayer) == layerList.end())
      layerList.insert(layerList.begin(), cmd.currentLayer);

    if (largeBtn("##RibbonLAY", RibbonIconKind::Layers, "Layers")) {
      SyncDrawingLayerTableWithGeometry(cmd);
      cmd.showLayerManagerWindow = true;
      log.push_back("LAYER — layer manager opened.");
    }
    RibbonItemHelp("Open layer manager — table of all layers.\nCommand bar: LAYER or LA");

    ImGui::SameLine(0, 6);
    ImGui::BeginGroup();
    ImGui::Dummy(ImVec2(0.f, 6.f));
    {
      const ImVec4 txt = ImGui::GetStyleColorVec4(ImGuiCol_Text);
      const ImVec4 dis = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(txt.x * 0.55f + dis.x * 0.45f, txt.y * 0.55f + dis.y * 0.45f,
                                                  txt.z * 0.55f + dis.z * 0.45f, 1.f));
      ImGui::TextUnformatted("Current layer");
      ImGui::PopStyleColor();
    }
    ImGui::SetNextItemWidth(std::max(120.f, kLayerPanelW - largeW - 40.f));
    const char* preview = cmd.currentLayer.empty() ? "0" : cmd.currentLayer.c_str();
    ImGui::PushID("RibbonLayerCombo");
    if (ImGui::BeginCombo("##ribbonlayerpick", preview, ImGuiComboFlags_HeightLargest)) {
      for (const auto& L : layerList) {
        const bool sel = L == cmd.currentLayer;
        if (ImGui::Selectable(L.c_str(), sel)) {
          cmd.currentLayer = L;
          SyncDrawingLayerTableWithGeometry(cmd);
        }
        if (sel)
          ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }
    ImGui::PopID();
    RibbonItemHelp("Current layer for new geometry (LINE, CIRCLE, TEXT, …).");
    ImGui::EndGroup();
  }
  RibbonSectionEnd();

  ImGui::EndChild();
  ImGui::PopStyleVar(2);
}

namespace {

constexpr const char* kVaries = "VARIES";

std::string MergeStrings(const std::vector<std::string>& v) {
  if (v.empty())
    return "---";
  const std::string& ref = v.front();
  for (const auto& s : v)
    if (s != ref)
      return kVaries;
  return ref;
}

std::string MergeFloatsFmt(const std::vector<float>& v, const char* fmt, float eps = 1e-5f) {
  if (v.empty())
    return "---";
  float r = v.front();
  for (float x : v)
    if (std::fabs(x - r) > eps)
      return kVaries;
  char buf[96];
  std::snprintf(buf, sizeof(buf), fmt, static_cast<double>(r));
  return buf;
}

std::string FormatXY(float x, float y, int precision) {
  return FormatLinear(static_cast<double>(x), precision) + ", " + FormatLinear(static_cast<double>(y), precision);
}

/// Clockwise from north (+Y): **north = 0°**, east = 90°, decimal degrees [0, 360). App-wide bearing convention.

float BearingDegreesCwFromNorth(float dx, float dy) {
  const double rad = std::atan2(static_cast<double>(dx), static_cast<double>(dy));
  double deg = rad * (180.0 / 3.14159265358979323846);
  if (deg < 0.0)
    deg += 360.0;
  return static_cast<float>(deg);
}

const EntityAttributes& LineAttr(const AppCommandState& cmd, int idx) {
  static const EntityAttributes kDef{};
  if (idx < 0)
    return kDef;
  const size_t u = static_cast<size_t>(idx);
  if (u >= cmd.userLineAttrs.size())
    return kDef;
  return cmd.userLineAttrs[u];
}

const EntityAttributes& CircleAttr(const AppCommandState& cmd, int idx) {
  static const EntityAttributes kDef{};
  if (idx < 0)
    return kDef;
  const size_t u = static_cast<size_t>(idx);
  if (u >= cmd.userCircleAttrs.size())
    return kDef;
  return cmd.userCircleAttrs[u];
}

const EntityAttributes& ArcAttr(const AppCommandState& cmd, int idx) {
  static const EntityAttributes kDef{};
  if (idx < 0)
    return kDef;
  const size_t u = static_cast<size_t>(idx);
  if (u >= cmd.userArcAttrs.size())
    return kDef;
  return cmd.userArcAttrs[u];
}

const EntityAttributes& EllipseAttr(const AppCommandState& cmd, int idx) {
  static const EntityAttributes kDef{};
  if (idx < 0)
    return kDef;
  const size_t u = static_cast<size_t>(idx);
  if (u >= cmd.userEllAttrs.size())
    return kDef;
  return cmd.userEllAttrs[u];
}

const EntityAttributes& PolylineAttr(const AppCommandState& cmd, int idx) {
  static const EntityAttributes kDef{};
  if (idx < 0)
    return kDef;
  const size_t u = static_cast<size_t>(idx);
  if (u >= cmd.userPolylineAttrs.size())
    return kDef;
  return cmd.userPolylineAttrs[u];
}

const EntityAttributes& AnnAttr(const AppCommandState& cmd, int idx) {
  static const EntityAttributes kDef{};
  if (idx < 0)
    return kDef;
  const size_t u = static_cast<size_t>(idx);
  if (u >= cmd.cadAnnotationAttrs.size())
    return kDef;
  return cmd.cadAnnotationAttrs[u];
}

bool ReadLineEndpoints(const AppCommandState& cmd, int idx, float* x0, float* y0, float* x1, float* y1) {
  const size_t k = static_cast<size_t>(idx) * 6;
  if (k + 5 >= cmd.userLinesFlat.size())
    return false;
  *x0 = cmd.userLinesFlat[k];
  *y0 = cmd.userLinesFlat[k + 1];
  *x1 = cmd.userLinesFlat[k + 3];
  *y1 = cmd.userLinesFlat[k + 4];
  return true;
}

bool ReadCircle(const AppCommandState& cmd, int idx, float* cx, float* cy, float* r) {
  const size_t k = static_cast<size_t>(idx) * 3;
  if (k + 2 >= cmd.userCirclesCxCyR.size())
    return false;
  *cx = cmd.userCirclesCxCyR[k];
  *cy = cmd.userCirclesCxCyR[k + 1];
  *r = cmd.userCirclesCxCyR[k + 2];
  return true;
}

void PropRow(const char* label, const std::string& value) {
  ImGui::TableNextRow();
  PropValueCellBg();  // white value column; gray label shows panel face (nanoCAD two-tone)
  ImGui::TableNextColumn();
  ImGui::TextUnformatted(label);
  ImGui::TableNextColumn();
  ImGui::TextUnformatted(value.c_str());
}

std::string TrimUi(std::string s) {
  auto notSpace = [](unsigned char c) { return !std::isspace(c); };
  while (!s.empty() && !notSpace(static_cast<unsigned char>(s.front())))
    s.erase(s.begin());
  while (!s.empty() && !notSpace(static_cast<unsigned char>(s.back())))
    s.pop_back();
  return s;
}

void CollectGeneralAttrs(const AppCommandState& cmd, const std::vector<SelectedEntity>& sel,
                         std::vector<std::string>* layers, std::vector<std::string>* colors,
                         std::vector<std::string>* ltypes, std::vector<float>* lws,
                         std::vector<float>* trans) {
  layers->clear();
  colors->clear();
  ltypes->clear();
  lws->clear();
  trans->clear();
  for (const auto& e : sel) {
    if (e.type == SelectedEntity::Type::LineSeg) {
      const size_t k = static_cast<size_t>(e.index) * 6;
      if (k + 5 >= cmd.userLinesFlat.size())
        continue;
      const EntityAttributes& a = LineAttr(cmd, e.index);
      layers->push_back(a.layer);
      colors->push_back(a.color);
      ltypes->push_back(a.linetype);
      lws->push_back(a.lineweightMm);
      trans->push_back(a.transparency);
    } else if (e.type == SelectedEntity::Type::Circle) {
      const size_t k = static_cast<size_t>(e.index) * 3;
      if (k + 2 >= cmd.userCirclesCxCyR.size())
        continue;
      const EntityAttributes& a = CircleAttr(cmd, e.index);
      layers->push_back(a.layer);
      colors->push_back(a.color);
      ltypes->push_back(a.linetype);
      lws->push_back(a.lineweightMm);
      trans->push_back(a.transparency);
    } else if (e.type == SelectedEntity::Type::Annotation) {
      if (e.index < 0 || static_cast<size_t>(e.index) >= cmd.cadAnnotations.size())
        continue;
      const EntityAttributes& a = AnnAttr(cmd, e.index);
      layers->push_back(a.layer);
      colors->push_back(a.color);
      ltypes->push_back(a.linetype);
      lws->push_back(a.lineweightMm);
      trans->push_back(a.transparency);
    } else if (e.type == SelectedEntity::Type::Arc) {
      if (e.index < 0 || static_cast<size_t>(e.index) >= cmd.userArcs.size())
        continue;
      const EntityAttributes& a = ArcAttr(cmd, e.index);
      layers->push_back(a.layer);
      colors->push_back(a.color);
      ltypes->push_back(a.linetype);
      lws->push_back(a.lineweightMm);
      trans->push_back(a.transparency);
    } else if (e.type == SelectedEntity::Type::Ellipse) {
      if (e.index < 0 || static_cast<size_t>(e.index) >= cmd.userEllipses.size())
        continue;
      const EntityAttributes& a = EllipseAttr(cmd, e.index);
      layers->push_back(a.layer);
      colors->push_back(a.color);
      ltypes->push_back(a.linetype);
      lws->push_back(a.lineweightMm);
      trans->push_back(a.transparency);
    } else if (e.type == SelectedEntity::Type::Polyline) {
      const int np =
          static_cast<int>(cmd.userPolylineOffsets.size() > 0 ? cmd.userPolylineOffsets.size() - 1 : 0);
      if (e.index < 0 || e.index >= np)
        continue;
      const EntityAttributes& a = PolylineAttr(cmd, e.index);
      layers->push_back(a.layer);
      colors->push_back(a.color);
      ltypes->push_back(a.linetype);
      lws->push_back(a.lineweightMm);
      trans->push_back(a.transparency);
    }
  }
}

struct NamedColorPreset {
  const char* label;
  const char* storage;
  float r;
  float g;
  float b;
};

static const NamedColorPreset kNamedColors[] = {
    {"By Layer", "ByLayer", 1.f, 1.f, 1.f}, {"Red", "Red", 1.f, 0.f, 0.f}, {"Yellow", "Yellow", 1.f, 1.f, 0.f},
    {"Green", "Green", 0.f, 1.f, 0.f},    {"Cyan", "Cyan", 0.f, 1.f, 1.f}, {"Blue", "Blue", 0.f, 0.f, 1.f},
    {"Magenta", "Magenta", 1.f, 0.f, 1.f}, {"White", "White", 1.f, 1.f, 1.f}, {"Gray", "Gray", 0.5f, 0.5f, 0.5f},
    {"Black", "Black", 0.f, 0.f, 0.f},    {"Orange", "Orange", 1.f, 0.5f, 0.f},
};

// Full named palette first, then any custom color strings from entities not in the palette.
// Each entry: { display label, storage string }.
static void CollectQsColorOptions(const AppCommandState& cmd,
                                   std::vector<std::pair<std::string, std::string>>* out) {
  out->clear();
  for (const auto& p : kNamedColors)
    out->push_back({ p.label, p.storage });
  std::set<std::string> known;
  for (const auto& p : kNamedColors) known.insert(p.storage);
  auto addExtra = [&](const std::string& c) {
    if (!c.empty() && known.find(c) == known.end()) {
      out->push_back({ c, c });
      known.insert(c);
    }
  };
  for (const auto& a : cmd.userLineAttrs)      addExtra(a.color);
  for (const auto& a : cmd.userCircleAttrs)    addExtra(a.color);
  for (const auto& a : cmd.userArcAttrs)       addExtra(a.color);
  for (const auto& a : cmd.userEllAttrs)       addExtra(a.color);
  for (const auto& a : cmd.userPolylineAttrs)  addExtra(a.color);
  for (const auto& a : cmd.cadAnnotationAttrs) addExtra(a.color);
}

static const char* kEntityLinetypeLabels[] = {"By Layer", "By Block", "Continuous", "Dashed", "Hidden", "Center",
                                            "Phantom", "Divide", "Border"};
static const char* kEntityLinetypeStorage[] = {"ByLayer", "ByBlock", "Continuous", "DASHED", "HIDDEN", "CENTER",
                                               "PHANTOM", "DIVIDE", "BORDER"};
static constexpr int kEntityLinetypeCount =
    static_cast<int>(sizeof(kEntityLinetypeLabels) / sizeof(kEntityLinetypeLabels[0]));

static const char* kLayerLinetypeLabels[] = {"Continuous", "Dashed", "Hidden", "Center", "Phantom", "Divide", "Border"};
static const char* kLayerLinetypeStorage[] = {"Continuous", "DASHED", "HIDDEN", "CENTER", "PHANTOM", "DIVIDE",
                                                "BORDER"};
static constexpr int kLayerLinetypeCount =
    static_cast<int>(sizeof(kLayerLinetypeLabels) / sizeof(kLayerLinetypeLabels[0]));

static constexpr float kUiLineweightMmPresets[] = {
    -1.f,  0.f,   0.05f, 0.09f, 0.13f, 0.15f, 0.18f, 0.20f, 0.25f, 0.30f, 0.35f, 0.40f,
    0.50f, 0.53f, 0.60f, 0.70f, 0.80f, 0.90f, 1.00f, 1.06f, 1.20f, 1.40f, 1.58f, 2.00f, 2.11f};
static constexpr int kUiLineweightPresetCount =
    static_cast<int>(sizeof(kUiLineweightMmPresets) / sizeof(kUiLineweightMmPresets[0]));

static constexpr float kUiTransparencyPresets[] = {-1.f, 0.f, 0.25f, 0.5f, 0.75f, 0.9f, 1.f};
static constexpr int kUiTransparencyPresetCount =
    static_cast<int>(sizeof(kUiTransparencyPresets) / sizeof(kUiTransparencyPresets[0]));

static int EntityLinetypeComboIndex(const std::string& s) {
  const std::string c = CadCanonicalLinetypeNameForDxf(s);
  for (int i = 0; i < kEntityLinetypeCount; ++i) {
    if (CadCanonicalLinetypeNameForDxf(kEntityLinetypeStorage[i]) == c)
      return i;
  }
  return -1;
}

static int LayerLinetypeComboIndex(const std::string& s) {
  const std::string c = CadCanonicalLinetypeNameForDxf(s);
  for (int i = 0; i < kLayerLinetypeCount; ++i) {
    if (CadCanonicalLinetypeNameForDxf(kLayerLinetypeStorage[i]) == c)
      return i;
  }
  return 0;
}

static void SnprintLineweightPresetLabel(char* buf, size_t cap, float mm, bool layerRow) {
  if (mm < 0.f) {
    if (layerRow)
      std::snprintf(buf, cap, "Default");
    else
      std::snprintf(buf, cap, "By Layer");
    return;
  }
  std::snprintf(buf, cap, "%.2f mm", static_cast<double>(mm));
}

static int LineweightPresetIndexFromMm(float mm) {
  if (mm < 0.f)
    return 0;
  int best = 1;
  float bestD = 1e18f;
  for (int i = 1; i < kUiLineweightPresetCount; ++i) {
    const float d = std::fabs(mm - kUiLineweightMmPresets[i]);
    if (d < bestD) {
      bestD = d;
      best = i;
    }
  }
  return best;
}

static int TransparencyPresetIndexFromValue(float a) {
  if (a < -0.5f)
    return 0;
  int best = 1;
  float bestD = 1e18f;
  for (int i = 1; i < kUiTransparencyPresetCount; ++i) {
    const float d = std::fabs(a - kUiTransparencyPresets[i]);
    if (d < bestD) {
      bestD = d;
      best = i;
    }
  }
  return best;
}

static const char* TransparencyPresetLabel(int idx) {
  static const char* kLab[] = {"By Layer", "0 %", "25 %", "50 %", "75 %", "90 %", "100 %"};
  if (idx < 0 || idx >= kUiTransparencyPresetCount)
    return "?";
  return kLab[idx];
}

bool ParseHexColorRgb(const std::string& s, float* r, float* g, float* b) {
  if (s.size() < 4 || s[0] != '#')
    return false;
  auto hexVal = [](char c) -> int {
    if (c >= '0' && c <= '9')
      return c - '0';
    if (c >= 'a' && c <= 'f')
      return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F')
      return 10 + (c - 'A');
    return -1;
  };
  if (s.size() == 4) {
    const int rh = hexVal(s[1]);
    const int gh = hexVal(s[2]);
    const int bh = hexVal(s[3]);
    if (rh < 0 || gh < 0 || bh < 0)
      return false;
    const float rf = static_cast<float>(rh | (rh << 4)) / 255.f;
    const float gf = static_cast<float>(gh | (gh << 4)) / 255.f;
    const float bf = static_cast<float>(bh | (bh << 4)) / 255.f;
    *r = rf;
    *g = gf;
    *b = bf;
    return true;
  }
  if (s.size() != 7)
    return false;
  int rv = 0;
  int gv = 0;
  int bv = 0;
  for (int i = 0; i < 2; ++i)
    rv = rv * 16 + hexVal(s[1 + i]);
  for (int i = 0; i < 2; ++i)
    gv = gv * 16 + hexVal(s[3 + i]);
  for (int i = 0; i < 2; ++i)
    bv = bv * 16 + hexVal(s[5 + i]);
  if (rv < 0 || gv < 0 || bv < 0)
    return false;
  *r = static_cast<float>(rv) / 255.f;
  *g = static_cast<float>(gv) / 255.f;
  *b = static_cast<float>(bv) / 255.f;
  return true;
}

std::string FormatHexColorRgb(float r, float g, float b) {
  const int R = static_cast<int>(std::lround(std::clamp(r, 0.f, 1.f) * 255.f));
  const int G = static_cast<int>(std::lround(std::clamp(g, 0.f, 1.f) * 255.f));
  const int B = static_cast<int>(std::lround(std::clamp(b, 0.f, 1.f) * 255.f));
  char buf[16]{};
  std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", R, G, B);
  return std::string(buf);
}

bool LookupNamedColorRgb(const std::string& storage, float* r, float* g, float* b) {
  for (const auto& p : kNamedColors) {
    if (storage == p.storage) {
      *r = p.r;
      *g = p.g;
      *b = p.b;
      return true;
    }
  }
  return false;
}

std::string ColorStorageToPreviewLabel(const std::string& mergedFromSelection) {
  if (mergedFromSelection == kVaries)
    return "(mixed)";
  if (mergedFromSelection == "---" || mergedFromSelection.empty())
    return "---";
  if (mergedFromSelection == "ByLayer")
    return "By Layer";
  for (const auto& p : kNamedColors) {
    if (mergedFromSelection == p.storage)
      return p.label;
  }
  if (!mergedFromSelection.empty() && mergedFromSelection[0] == '#')
    return std::string("Custom ") + mergedFromSelection;
  return mergedFromSelection;
}

static float gCustomColorPicker[4] = {1.f, 1.f, 1.f, 1.f};

void PrepareCustomColorPicker(const AppCommandState& cmd) {
  std::vector<std::string> layers, colors, ltypes;
  std::vector<float> lws, trans;
  CollectGeneralAttrs(cmd, cmd.selection, &layers, &colors, &ltypes, &lws, &trans);
  const std::string merged = MergeStrings(colors);
  std::string seed = colors.empty() ? std::string("ByLayer") : colors.front();
  if (merged != kVaries)
    seed = merged;

  float r = 1.f;
  float g = 1.f;
  float b = 1.f;
  if (!ParseHexColorRgb(seed, &r, &g, &b))
    LookupNamedColorRgb(seed, &r, &g, &b);

  gCustomColorPicker[0] = r;
  gCustomColorPicker[1] = g;
  gCustomColorPicker[2] = b;
  gCustomColorPicker[3] = 1.f;
}

uint64_t SelectionFingerprint(const std::vector<SelectedEntity>& sel) {
  uint64_t h = sel.size();
  for (const auto& e : sel) {
    h = h * 1315423911ull + static_cast<uint64_t>(static_cast<int>(e.type));
    h = h * 1315423911ull + static_cast<uint64_t>(static_cast<uint32_t>(e.index));
  }
  return h;
}

static char gBufLayer[160]{};
static char gBufLinetype[160]{};
static float gLineweightMm = 0.18f;
static float gTransparency01 = 0.f;
static bool gLineweightMixed = false;
static bool gTransparencyMixed = false;
static bool gHintLayerMixed = false;
static bool gHintLinetypeMixed = false;
static uint64_t gPropsSelFingerprint = ~0ull;

void RefreshMixedHintFlags(AppCommandState& cmd) {
  std::vector<std::string> layers, colors, ltypes;
  std::vector<float> lws, trans;
  CollectGeneralAttrs(cmd, cmd.selection, &layers, &colors, &ltypes, &lws, &trans);
  if (!layers.empty()) {
    gHintLayerMixed = (MergeStrings(layers) == kVaries);
    gHintLinetypeMixed = (MergeStrings(ltypes) == kVaries);
  }
  gLineweightMixed = false;
  if (!lws.empty()) {
    const float r = lws.front();
    for (float w : lws) {
      if (std::fabs(w - r) > 1e-5f) {
        gLineweightMixed = true;
        break;
      }
    }
  }
  gTransparencyMixed = false;
  if (!trans.empty()) {
    const float t0 = trans.front();
    for (float t : trans) {
      if (std::fabs(t - t0) > 1e-5f) {
        gTransparencyMixed = true;
        break;
      }
    }
  }
}

void RefreshPropsBuffersFromModel(AppCommandState& cmd, const std::vector<SelectedEntity>& sel) {
  std::vector<std::string> layers, colors, ltypes;
  std::vector<float> lws, trans;
  CollectGeneralAttrs(cmd, sel, &layers, &colors, &ltypes, &lws, &trans);
  (void)colors;

  const std::string ml = MergeStrings(layers);
  const std::string mt = MergeStrings(ltypes);
  gHintLayerMixed = (ml == kVaries);
  gHintLinetypeMixed = (mt == kVaries);

  auto fillTextBuf = [&](char* buf, int bufSize, const std::string& merged) {
    if (merged.empty() || merged == kVaries || merged == "---") {
      buf[0] = '\0';
    } else {
      ImStrncpy(buf, merged.c_str(), bufSize);
      buf[bufSize - 1] = '\0';
    }
  };

  fillTextBuf(gBufLayer, IM_ARRAYSIZE(gBufLayer), ml);
  fillTextBuf(gBufLinetype, IM_ARRAYSIZE(gBufLinetype), mt);

  gLineweightMixed = false;
  gTransparencyMixed = false;
  if (!lws.empty()) {
    gLineweightMm = lws.front();
    for (float w : lws) {
      if (std::fabs(w - lws.front()) > 1e-5f) {
        gLineweightMixed = true;
        break;
      }
    }
  }
  if (!trans.empty()) {
    gTransparency01 = trans.front();
    for (float t : trans) {
      if (std::fabs(t - trans.front()) > 1e-5f) {
        gTransparencyMixed = true;
        break;
      }
    }
  }
}

void ApplyLayerToSelection(AppCommandState& cmd, const std::string& v) {
  if (v.empty())
    return;
  EnsureAttrCounts(cmd);
  for (const auto& e : cmd.selection) {
    if (e.type == SelectedEntity::Type::LineSeg) {
      const size_t k = static_cast<size_t>(e.index) * 6;
      if (k + 5 >= cmd.userLinesFlat.size() || static_cast<size_t>(e.index) >= cmd.userLineAttrs.size())
        continue;
      cmd.userLineAttrs[static_cast<size_t>(e.index)].layer = v;
    } else if (e.type == SelectedEntity::Type::Circle) {
      const size_t k = static_cast<size_t>(e.index) * 3;
      if (k + 2 >= cmd.userCirclesCxCyR.size() || static_cast<size_t>(e.index) >= cmd.userCircleAttrs.size())
        continue;
      cmd.userCircleAttrs[static_cast<size_t>(e.index)].layer = v;
    } else if (e.type == SelectedEntity::Type::Annotation) {
      if (e.index < 0 || static_cast<size_t>(e.index) >= cmd.cadAnnotations.size() ||
          static_cast<size_t>(e.index) >= cmd.cadAnnotationAttrs.size())
        continue;
      cmd.cadAnnotationAttrs[static_cast<size_t>(e.index)].layer = v;
    } else if (e.type == SelectedEntity::Type::Arc) {
      if (e.index < 0 || static_cast<size_t>(e.index) >= cmd.userArcs.size() ||
          static_cast<size_t>(e.index) >= cmd.userArcAttrs.size())
        continue;
      cmd.userArcAttrs[static_cast<size_t>(e.index)].layer = v;
    } else if (e.type == SelectedEntity::Type::Ellipse) {
      if (e.index < 0 || static_cast<size_t>(e.index) >= cmd.userEllipses.size() ||
          static_cast<size_t>(e.index) >= cmd.userEllAttrs.size())
        continue;
      cmd.userEllAttrs[static_cast<size_t>(e.index)].layer = v;
    } else if (e.type == SelectedEntity::Type::Polyline) {
      const int np =
          static_cast<int>(cmd.userPolylineOffsets.size() > 0 ? cmd.userPolylineOffsets.size() - 1 : 0);
      if (e.index < 0 || e.index >= np || static_cast<size_t>(e.index) >= cmd.userPolylineAttrs.size())
        continue;
      cmd.userPolylineAttrs[static_cast<size_t>(e.index)].layer = v;
    }
  }
  SyncDrawingLayerTableWithGeometry(cmd);
  BumpCadGpuCache(cmd);
  RefreshMixedHintFlags(cmd);
}

void ApplyColorToSelection(AppCommandState& cmd, const std::string& v) {
  if (v.empty())
    return;
  EnsureAttrCounts(cmd);
  for (const auto& e : cmd.selection) {
    if (e.type == SelectedEntity::Type::LineSeg) {
      const size_t k = static_cast<size_t>(e.index) * 6;
      if (k + 5 >= cmd.userLinesFlat.size() || static_cast<size_t>(e.index) >= cmd.userLineAttrs.size())
        continue;
      cmd.userLineAttrs[static_cast<size_t>(e.index)].color = v;
    } else if (e.type == SelectedEntity::Type::Circle) {
      const size_t k = static_cast<size_t>(e.index) * 3;
      if (k + 2 >= cmd.userCirclesCxCyR.size() || static_cast<size_t>(e.index) >= cmd.userCircleAttrs.size())
        continue;
      cmd.userCircleAttrs[static_cast<size_t>(e.index)].color = v;
    } else if (e.type == SelectedEntity::Type::Annotation) {
      if (e.index < 0 || static_cast<size_t>(e.index) >= cmd.cadAnnotations.size() ||
          static_cast<size_t>(e.index) >= cmd.cadAnnotationAttrs.size())
        continue;
      cmd.cadAnnotationAttrs[static_cast<size_t>(e.index)].color = v;
    } else if (e.type == SelectedEntity::Type::Arc) {
      if (e.index < 0 || static_cast<size_t>(e.index) >= cmd.userArcs.size() ||
          static_cast<size_t>(e.index) >= cmd.userArcAttrs.size())
        continue;
      cmd.userArcAttrs[static_cast<size_t>(e.index)].color = v;
    } else if (e.type == SelectedEntity::Type::Ellipse) {
      if (e.index < 0 || static_cast<size_t>(e.index) >= cmd.userEllipses.size() ||
          static_cast<size_t>(e.index) >= cmd.userEllAttrs.size())
        continue;
      cmd.userEllAttrs[static_cast<size_t>(e.index)].color = v;
    } else if (e.type == SelectedEntity::Type::Polyline) {
      const int np =
          static_cast<int>(cmd.userPolylineOffsets.size() > 0 ? cmd.userPolylineOffsets.size() - 1 : 0);
      if (e.index < 0 || e.index >= np || static_cast<size_t>(e.index) >= cmd.userPolylineAttrs.size())
        continue;
      cmd.userPolylineAttrs[static_cast<size_t>(e.index)].color = v;
    }
  }
  BumpCadGpuCache(cmd);
  RefreshMixedHintFlags(cmd);
}

void ApplyLinetypeToSelection(AppCommandState& cmd, const std::string& v) {
  if (v.empty())
    return;
  EnsureAttrCounts(cmd);
  for (const auto& e : cmd.selection) {
    if (e.type == SelectedEntity::Type::LineSeg) {
      const size_t k = static_cast<size_t>(e.index) * 6;
      if (k + 5 >= cmd.userLinesFlat.size() || static_cast<size_t>(e.index) >= cmd.userLineAttrs.size())
        continue;
      cmd.userLineAttrs[static_cast<size_t>(e.index)].linetype = v;
    } else if (e.type == SelectedEntity::Type::Circle) {
      const size_t k = static_cast<size_t>(e.index) * 3;
      if (k + 2 >= cmd.userCirclesCxCyR.size() || static_cast<size_t>(e.index) >= cmd.userCircleAttrs.size())
        continue;
      cmd.userCircleAttrs[static_cast<size_t>(e.index)].linetype = v;
    } else if (e.type == SelectedEntity::Type::Annotation) {
      if (e.index < 0 || static_cast<size_t>(e.index) >= cmd.cadAnnotations.size() ||
          static_cast<size_t>(e.index) >= cmd.cadAnnotationAttrs.size())
        continue;
      cmd.cadAnnotationAttrs[static_cast<size_t>(e.index)].linetype = v;
    } else if (e.type == SelectedEntity::Type::Arc) {
      if (e.index < 0 || static_cast<size_t>(e.index) >= cmd.userArcs.size() ||
          static_cast<size_t>(e.index) >= cmd.userArcAttrs.size())
        continue;
      cmd.userArcAttrs[static_cast<size_t>(e.index)].linetype = v;
    } else if (e.type == SelectedEntity::Type::Ellipse) {
      if (e.index < 0 || static_cast<size_t>(e.index) >= cmd.userEllipses.size() ||
          static_cast<size_t>(e.index) >= cmd.userEllAttrs.size())
        continue;
      cmd.userEllAttrs[static_cast<size_t>(e.index)].linetype = v;
    } else if (e.type == SelectedEntity::Type::Polyline) {
      const int np =
          static_cast<int>(cmd.userPolylineOffsets.size() > 0 ? cmd.userPolylineOffsets.size() - 1 : 0);
      if (e.index < 0 || e.index >= np || static_cast<size_t>(e.index) >= cmd.userPolylineAttrs.size())
        continue;
      cmd.userPolylineAttrs[static_cast<size_t>(e.index)].linetype = v;
    }
  }
  BumpCadGpuCache(cmd);
  RefreshMixedHintFlags(cmd);
}

void ApplyLineweightToSelection(AppCommandState& cmd, float mm) {
  const float stored = (mm < 0.f) ? -1.f : std::max(0.f, mm);
  EnsureAttrCounts(cmd);
  for (const auto& e : cmd.selection) {
    if (e.type == SelectedEntity::Type::LineSeg) {
      const size_t k = static_cast<size_t>(e.index) * 6;
      if (k + 5 >= cmd.userLinesFlat.size() || static_cast<size_t>(e.index) >= cmd.userLineAttrs.size())
        continue;
      cmd.userLineAttrs[static_cast<size_t>(e.index)].lineweightMm = stored;
    } else if (e.type == SelectedEntity::Type::Circle) {
      const size_t k = static_cast<size_t>(e.index) * 3;
      if (k + 2 >= cmd.userCirclesCxCyR.size() || static_cast<size_t>(e.index) >= cmd.userCircleAttrs.size())
        continue;
      cmd.userCircleAttrs[static_cast<size_t>(e.index)].lineweightMm = stored;
    } else if (e.type == SelectedEntity::Type::Annotation) {
      if (e.index < 0 || static_cast<size_t>(e.index) >= cmd.cadAnnotations.size() ||
          static_cast<size_t>(e.index) >= cmd.cadAnnotationAttrs.size())
        continue;
      cmd.cadAnnotationAttrs[static_cast<size_t>(e.index)].lineweightMm = stored;
    } else if (e.type == SelectedEntity::Type::Arc) {
      if (e.index < 0 || static_cast<size_t>(e.index) >= cmd.userArcs.size() ||
          static_cast<size_t>(e.index) >= cmd.userArcAttrs.size())
        continue;
      cmd.userArcAttrs[static_cast<size_t>(e.index)].lineweightMm = stored;
    } else if (e.type == SelectedEntity::Type::Ellipse) {
      if (e.index < 0 || static_cast<size_t>(e.index) >= cmd.userEllipses.size() ||
          static_cast<size_t>(e.index) >= cmd.userEllAttrs.size())
        continue;
      cmd.userEllAttrs[static_cast<size_t>(e.index)].lineweightMm = stored;
    } else if (e.type == SelectedEntity::Type::Polyline) {
      const int np =
          static_cast<int>(cmd.userPolylineOffsets.size() > 0 ? cmd.userPolylineOffsets.size() - 1 : 0);
      if (e.index < 0 || e.index >= np || static_cast<size_t>(e.index) >= cmd.userPolylineAttrs.size())
        continue;
      cmd.userPolylineAttrs[static_cast<size_t>(e.index)].lineweightMm = stored;
    }
  }
  BumpCadGpuCache(cmd);
  RefreshMixedHintFlags(cmd);
}

void ApplyTransparencyToSelection(AppCommandState& cmd, float a) {
  const float stored = (a < -0.5f) ? -1.f : std::clamp(a, 0.f, 1.f);
  EnsureAttrCounts(cmd);
  for (const auto& e : cmd.selection) {
    if (e.type == SelectedEntity::Type::LineSeg) {
      const size_t k = static_cast<size_t>(e.index) * 6;
      if (k + 5 >= cmd.userLinesFlat.size() || static_cast<size_t>(e.index) >= cmd.userLineAttrs.size())
        continue;
      cmd.userLineAttrs[static_cast<size_t>(e.index)].transparency = stored;
    } else if (e.type == SelectedEntity::Type::Circle) {
      const size_t k = static_cast<size_t>(e.index) * 3;
      if (k + 2 >= cmd.userCirclesCxCyR.size() || static_cast<size_t>(e.index) >= cmd.userCircleAttrs.size())
        continue;
      cmd.userCircleAttrs[static_cast<size_t>(e.index)].transparency = stored;
    } else if (e.type == SelectedEntity::Type::Annotation) {
      if (e.index < 0 || static_cast<size_t>(e.index) >= cmd.cadAnnotations.size() ||
          static_cast<size_t>(e.index) >= cmd.cadAnnotationAttrs.size())
        continue;
      cmd.cadAnnotationAttrs[static_cast<size_t>(e.index)].transparency = stored;
    } else if (e.type == SelectedEntity::Type::Arc) {
      if (e.index < 0 || static_cast<size_t>(e.index) >= cmd.userArcs.size() ||
          static_cast<size_t>(e.index) >= cmd.userArcAttrs.size())
        continue;
      cmd.userArcAttrs[static_cast<size_t>(e.index)].transparency = stored;
    } else if (e.type == SelectedEntity::Type::Ellipse) {
      if (e.index < 0 || static_cast<size_t>(e.index) >= cmd.userEllipses.size() ||
          static_cast<size_t>(e.index) >= cmd.userEllAttrs.size())
        continue;
      cmd.userEllAttrs[static_cast<size_t>(e.index)].transparency = stored;
    } else if (e.type == SelectedEntity::Type::Polyline) {
      const int np =
          static_cast<int>(cmd.userPolylineOffsets.size() > 0 ? cmd.userPolylineOffsets.size() - 1 : 0);
      if (e.index < 0 || e.index >= np || static_cast<size_t>(e.index) >= cmd.userPolylineAttrs.size())
        continue;
      cmd.userPolylineAttrs[static_cast<size_t>(e.index)].transparency = stored;
    }
  }
  BumpCadGpuCache(cmd);
  RefreshMixedHintFlags(cmd);
}

/// \return true if user chose Custom — caller must `OpenPopup("GoSurveyCustomColor")` after combo/popups close.
bool DrawColorPickerRow(AppCommandState& cmd) {
  bool requestCustomPopup = false;
  std::vector<std::string> layers, colors, ltypes;
  std::vector<float> lws, trans;
  CollectGeneralAttrs(cmd, cmd.selection, &layers, &colors, &ltypes, &lws, &trans);
  (void)ltypes;
  (void)lws;

  const std::string mergedLayer = MergeStrings(layers);
  const std::string merged = MergeStrings(colors);
  float mergedTrans = 0.f;
  if (!trans.empty()) {
    mergedTrans = trans.front();
    for (float t : trans) {
      if (std::fabs(t - mergedTrans) > 1e-5f) {
        mergedTrans = 0.f;
        break;
      }
    }
  }

  int nLine = 0;
  int nCirc = 0;
  int nAnn = 0;
  for (const auto& e : cmd.selection) {
    if (e.type == SelectedEntity::Type::LineSeg)
      ++nLine;
    else if (e.type == SelectedEntity::Type::Circle)
      ++nCirc;
    else if (e.type == SelectedEntity::Type::Annotation)
      ++nAnn;
  }
  float dr = 0.35f;
  float dg = 0.95f;
  float db = 1.f;
  if (nCirc > 0 && nLine == 0 && nAnn == 0) {
    dr = 0.92f;
    dg = 0.55f;
    db = 1.f;
  } else if (nAnn > 0 && nLine == 0 && nCirc == 0) {
    dr = 0.85f;
    dg = 0.95f;
    db = 0.65f;
  }

  ImGui::TableNextRow();
  ImGui::TableNextColumn();
  ImGui::TextUnformatted("Color");
  ImGui::TableNextColumn();

  const float frameH = ImGui::GetFrameHeight();
  ImVec4 swatchRgb;
  if (merged == kVaries || merged == "---") {
    swatchRgb = ImVec4(0.45f, 0.45f, 0.47f, 1.f - mergedTrans);
  } else {
    float rgba[4];
    // When color is "ByLayer" and all selected entities share one layer, resolve
    // the swatch to that layer's actual color instead of the ByLayer placeholder.
    std::string effectiveColor = merged;
    if (merged == "ByLayer" && mergedLayer != kVaries && mergedLayer != "---" && !mergedLayer.empty()) {
      const CadLayerRow* row = FindDrawingLayerRowCi(cmd, mergedLayer);
      if (row && !row->color.empty())
        effectiveColor = row->color;
    }
    ResolveStoredColorForViewport(effectiveColor, mergedTrans, dr, dg, db, rgba);
    swatchRgb = ImVec4(rgba[0], rgba[1], rgba[2], rgba[3]);
  }

  ImGui::ColorButton("##colorswatch", swatchRgb,
                     ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop,
                     ImVec2(std::max(18.f, frameH - 2.f), std::max(18.f, frameH - 2.f)));
  ImGui::SameLine(0.f, 6.f);
  ImGui::SetNextItemWidth(std::max(40.f, ImGui::GetContentRegionAvail().x));

  const std::string preview = ColorStorageToPreviewLabel(merged);
  const ImVec2 rowSwatchSize(18.f, ImGui::GetTextLineHeightWithSpacing());
  const ImGuiColorEditFlags rowSwatchFlags =
      ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop;

  if (ImGui::BeginCombo("##colorcombo", preview.c_str())) {
    for (const auto& p : kNamedColors) {
      const bool selected =
          (merged != kVaries && merged != "---" && !merged.empty() && merged == p.storage);
      ImGui::PushID(p.storage);
      float prgba[4];
      ResolveStoredColorForViewport(p.storage, 0.f, dr, dg, db, prgba);
      bool hit = ImGui::ColorButton("##rowsw", ImVec4(prgba[0], prgba[1], prgba[2], prgba[3]), rowSwatchFlags,
                                    rowSwatchSize);
      ImGui::SameLine(0.f, 8.f);
      hit |= ImGui::Selectable(p.label, selected, ImGuiSelectableFlags_SpanAvailWidth, ImVec2(0.f, rowSwatchSize.y));
      if (hit)
        ApplyColorToSelection(cmd, p.storage);
      ImGui::PopID();
    }
    ImGui::Separator();

    ImGui::PushID("custom_row");
    float customPreview[4];
    if (!merged.empty() && merged[0] == '#' && merged != kVaries)
      ResolveStoredColorForViewport(merged, mergedTrans, dr, dg, db, customPreview);
    else {
      customPreview[0] = customPreview[1] = customPreview[2] = 1.f;
      customPreview[3] = 1.f;
    }
    bool openCustom = ImGui::ColorButton(
        "##customrowsw", ImVec4(customPreview[0], customPreview[1], customPreview[2], customPreview[3]), rowSwatchFlags,
        rowSwatchSize);
    ImGui::SameLine(0.f, 8.f);
    openCustom |= ImGui::Selectable("Custom color…", false, ImGuiSelectableFlags_SpanAvailWidth,
                                  ImVec2(0.f, rowSwatchSize.y));
    if (openCustom) {
      PrepareCustomColorPicker(cmd);
      requestCustomPopup = true;
    }
    ImGui::PopID();

    ImGui::EndCombo();
  }

  return requestCustomPopup;
}

void DrawEditableGeneralSection(AppCommandState& cmd, const std::vector<SelectedEntity>& sel) {
  (void)sel;
  if (!PropSectionHeader("General"))
    return;

  const ImGuiInputTextFlags tflags = ImGuiInputTextFlags_EnterReturnsTrue;
  bool requestCustomColorPopup = false;

  if (ImGui::BeginTable("props_gen_ed", 2, kPropTableFlags)) {
    ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthStretch, 0.38f);
    ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch, 0.62f);

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("Layer");
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-1);
    const bool layerEnter =
        ImGui::InputTextWithHint("##layer", gHintLayerMixed ? "Mixed — enter applies to all" : "", gBufLayer,
                                 IM_ARRAYSIZE(gBufLayer), tflags);
    const bool layerDeactivated = ImGui::IsItemDeactivatedAfterEdit();
    if (layerEnter || layerDeactivated) {
      const std::string vv = TrimUi(std::string(gBufLayer));
      if (!vv.empty())
        ApplyLayerToSelection(cmd, vv);
    }

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("Layer list");
    ImGui::TableNextColumn();
    {
      std::vector<std::string> layerOpts;
      CollectAllDrawingLayers(cmd, &layerOpts);
      const char* cprev = gHintLayerMixed ? "(mixed)" : (gBufLayer[0] ? gBufLayer : "— choose —");
      ImGui::SetNextItemWidth(-1);
      if (ImGui::BeginCombo("##layerpicklist", cprev)) {
        for (const auto& L : layerOpts) {
          if (ImGui::Selectable(L.c_str())) {
            ImStrncpy(gBufLayer, L.c_str(), IM_ARRAYSIZE(gBufLayer));
            gBufLayer[IM_ARRAYSIZE(gBufLayer) - 1] = '\0';
            ApplyLayerToSelection(cmd, L);
          }
        }
        ImGui::EndCombo();
      }
    }

    requestCustomColorPopup = DrawColorPickerRow(cmd);

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("Linetype");
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-1);
    {
      std::vector<std::string> layers2, colors2, ltypes2;
      std::vector<float> lws2, trans2;
      CollectGeneralAttrs(cmd, cmd.selection, &layers2, &colors2, &ltypes2, &lws2, &trans2);
      const std::string mtLt = MergeStrings(ltypes2);
      char ltPrev[180];
      if (mtLt == kVaries)
        std::snprintf(ltPrev, sizeof(ltPrev), "(mixed)");
      else {
        const int lix = EntityLinetypeComboIndex(mtLt);
        if (lix >= 0)
          std::snprintf(ltPrev, sizeof(ltPrev), "%s", kEntityLinetypeLabels[lix]);
        else
          ImStrncpy(ltPrev, mtLt.c_str(), sizeof(ltPrev));
      }
      ltPrev[sizeof(ltPrev) - 1] = '\0';
      if (ImGui::BeginCombo("##linetypecombo", ltPrev)) {
        for (int j = 0; j < kEntityLinetypeCount; ++j) {
          const bool sel = (mtLt != kVaries && EntityLinetypeComboIndex(mtLt) == j);
          if (ImGui::Selectable(kEntityLinetypeLabels[j], sel)) {
            ApplyLinetypeToSelection(cmd, kEntityLinetypeStorage[j]);
            ImStrncpy(gBufLinetype, kEntityLinetypeStorage[j], IM_ARRAYSIZE(gBufLinetype));
            gBufLinetype[IM_ARRAYSIZE(gBufLinetype) - 1] = '\0';
            gHintLinetypeMixed = false;
          }
          if (sel)
            ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }
    }

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("Lineweight");
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-1);
    {
      char lwPrev[96];
      if (gLineweightMixed)
        std::snprintf(lwPrev, sizeof(lwPrev), "(mixed)");
      else
        SnprintLineweightPresetLabel(lwPrev, sizeof(lwPrev), gLineweightMm, false);
      if (ImGui::BeginCombo("##lwcombo", lwPrev)) {
        for (int j = 0; j < kUiLineweightPresetCount; ++j) {
          char lab[96];
          SnprintLineweightPresetLabel(lab, sizeof(lab), kUiLineweightMmPresets[j], false);
          const bool sel = !gLineweightMixed && LineweightPresetIndexFromMm(gLineweightMm) == j;
          if (ImGui::Selectable(lab, sel)) {
            ApplyLineweightToSelection(cmd, kUiLineweightMmPresets[j]);
            gLineweightMm = kUiLineweightMmPresets[j];
            gLineweightMixed = false;
          }
          if (sel)
            ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }
    }

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("Transparency");
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-1);
    {
      char trPrev[96];
      if (gTransparencyMixed)
        std::snprintf(trPrev, sizeof(trPrev), "(mixed)");
      else {
        const float trV = (gTransparency01 < -0.5f) ? -1.f : gTransparency01;
        const int tix = TransparencyPresetIndexFromValue(trV);
        std::snprintf(trPrev, sizeof(trPrev), "%s", TransparencyPresetLabel(tix));
      }
      if (ImGui::BeginCombo("##trcombo", trPrev)) {
        for (int j = 0; j < kUiTransparencyPresetCount; ++j) {
          const bool sel = !gTransparencyMixed && TransparencyPresetIndexFromValue(gTransparency01 < -0.5f ? -1.f
                                                                                                         : gTransparency01) == j;
          if (ImGui::Selectable(TransparencyPresetLabel(j), sel)) {
            ApplyTransparencyToSelection(cmd, kUiTransparencyPresets[j]);
            gTransparency01 = kUiTransparencyPresets[j];
            gTransparencyMixed = false;
          }
          if (sel)
            ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }
    }

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("Default text height (in)");
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputFloat("##defplottxt", &cmd.defaultPlottedTextHeightInches, 0.005f, 0.02f, "%.4f");
    if (cmd.defaultPlottedTextHeightInches <= 0.f)
      cmd.defaultPlottedTextHeightInches = 0.0625f;
    if (ImGui::IsItemDeactivatedAfterEdit())
      BumpCadGpuCache(cmd);

    ImGui::EndTable();
  }

  if (requestCustomColorPopup)
    ImGui::OpenPopup("GoSurveyCustomColor");

  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

  if (ImGui::BeginPopupModal("GoSurveyCustomColor", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::ColorPicker4("##custpick", gCustomColorPicker,
                        ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_InputRGB |
                            ImGuiColorEditFlags_NoAlpha);
    ImGui::Separator();
    if (ImGui::Button("Apply", ImVec2(120.f, 0.f))) {
      ApplyColorToSelection(cmd, FormatHexColorRgb(gCustomColorPicker[0], gCustomColorPicker[1],
                                                   gCustomColorPicker[2]));
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120.f, 0.f)))
      ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
}

void DrawSingleLineGeometryEditable(AppCommandState& cmd, int lineIdx) {
  if (!PropSectionHeader("Geometry"))
    return;
  const size_t k = static_cast<size_t>(lineIdx) * 6;
  if (k + 5 >= cmd.userLinesFlat.size())
    return;
  float* x0 = &cmd.userLinesFlat[k];
  float* y0 = &cmd.userLinesFlat[k + 1];
  float* x1 = &cmd.userLinesFlat[k + 3];
  float* y1 = &cmd.userLinesFlat[k + 4];
  const std::string cfmt = DisplayFloatFmt(cmd.displayLinearPrecision);

  if (ImGui::BeginTable("props_geom_line_ed", 2, kPropTableFlags)) {
    ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthStretch, 0.38f);
    ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch, 0.62f);

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("Start X");
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputFloat("##lsx", x0, 0.f, 0.f, cfmt.c_str());
    if (ImGui::IsItemDeactivatedAfterEdit())
      BumpCadGpuCache(cmd);

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("Start Y");
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputFloat("##lsy", y0, 0.f, 0.f, cfmt.c_str());
    if (ImGui::IsItemDeactivatedAfterEdit())
      BumpCadGpuCache(cmd);

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("End X");
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputFloat("##lex", x1, 0.f, 0.f, cfmt.c_str());
    if (ImGui::IsItemDeactivatedAfterEdit())
      BumpCadGpuCache(cmd);

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("End Y");
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputFloat("##ley", y1, 0.f, 0.f, cfmt.c_str());
    if (ImGui::IsItemDeactivatedAfterEdit())
      BumpCadGpuCache(cmd);

    ImGui::EndTable();
  }

  const float dx = *x1 - *x0;
  const float dy = *y1 - *y0;
  const float len = std::sqrt(dx * dx + dy * dy);
  const float bear = BearingDegreesCwFromNorth(dx, dy);
  const std::string lenStr = FormatLinear(static_cast<double>(len), cmd.displayLinearPrecision);
  const std::string bearStr = FormatBearing(static_cast<double>(bear), CadAngleDisplaySettings(cmd));

  ImGui::Spacing();
  ImGui::TextDisabled("Derived");
  if (ImGui::BeginTable("props_geom_line_derived", 2, kPropTableFlags)) {
    ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthStretch, 0.38f);
    ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch, 0.62f);
    PropRow("Length", lenStr.c_str());
    PropRow("Rotation rel. north", bearStr.c_str());
    ImGui::EndTable();
  }
}

void DrawSingleCircleGeometryEditable(AppCommandState& cmd, int circleIdx) {
  if (!PropSectionHeader("Geometry"))
    return;
  const size_t k = static_cast<size_t>(circleIdx) * 3;
  if (k + 2 >= cmd.userCirclesCxCyR.size())
    return;
  float* cx = &cmd.userCirclesCxCyR[k];
  float* cy = &cmd.userCirclesCxCyR[k + 1];
  float* r = &cmd.userCirclesCxCyR[k + 2];
  const std::string cfmt = DisplayFloatFmt(cmd.displayLinearPrecision);

  if (ImGui::BeginTable("props_geom_circ_ed", 2, kPropTableFlags)) {
    ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthStretch, 0.38f);
    ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch, 0.62f);

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("Center X");
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputFloat("##cx", cx, 0.f, 0.f, cfmt.c_str());
    if (ImGui::IsItemDeactivatedAfterEdit())
      BumpCadGpuCache(cmd);

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("Center Y");
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputFloat("##cy", cy, 0.f, 0.f, cfmt.c_str());
    if (ImGui::IsItemDeactivatedAfterEdit())
      BumpCadGpuCache(cmd);

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("Radius");
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputFloat("##cr", r, 0.f, 0.f, cfmt.c_str());
    if (*r < 1e-6f)
      *r = 1e-6f;
    if (ImGui::IsItemDeactivatedAfterEdit())
      BumpCadGpuCache(cmd);

    ImGui::EndTable();
  }

  constexpr float kPi = 3.14159265358979323846f;
  const float diam = 2.f * (*r);
  const float circ = 2.f * kPi * (*r);
  const float area = kPi * (*r) * (*r);
  const std::string dStr = FormatLinear(static_cast<double>(diam), cmd.displayLinearPrecision);
  const std::string cStr = FormatLinear(static_cast<double>(circ), cmd.displayLinearPrecision);
  const std::string aStr = FormatLinear(static_cast<double>(area), cmd.displayLinearPrecision);

  ImGui::Spacing();
  ImGui::TextDisabled("Derived");
  if (ImGui::BeginTable("props_geom_circ_derived", 2, kPropTableFlags)) {
    ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthStretch, 0.38f);
    ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch, 0.62f);
    PropRow("Diameter", dStr.c_str());
    PropRow("Circumference", cStr.c_str());
    PropRow("Area", aStr.c_str());
    ImGui::EndTable();
  }
}

void DrawLineGeometryOnly(const AppCommandState& cmd, const std::vector<SelectedEntity>& linesOnly) {
  std::vector<float> vx0, vy0, vx1, vy1, vlen, vbear;
  for (const auto& e : linesOnly) {
    float x0 = 0.f, y0 = 0.f, x1 = 0.f, y1 = 0.f;
    if (!ReadLineEndpoints(cmd, e.index, &x0, &y0, &x1, &y1))
      continue;
    vx0.push_back(x0);
    vy0.push_back(y0);
    vx1.push_back(x1);
    vy1.push_back(y1);
    const float dx = x1 - x0;
    const float dy = y1 - y0;
    const float len = std::sqrt(dx * dx + dy * dy);
    vlen.push_back(len);
    vbear.push_back(BearingDegreesCwFromNorth(dx, dy));
  }
  auto mergeCoord = [&](const std::vector<float>& xs, const std::vector<float>& ys) -> std::string {
    if (xs.empty() || ys.empty() || xs.size() != ys.size())
      return "---";
    std::string ref = FormatXY(xs[0], ys[0], cmd.displayLinearPrecision);
    for (size_t i = 1; i < xs.size(); ++i) {
      if (FormatXY(xs[i], ys[i], cmd.displayLinearPrecision) != ref)
        return kVaries;
    }
    return ref;
  };
  const std::string startPt = mergeCoord(vx0, vy0);
  const std::string endPt = mergeCoord(vx1, vy1);
  const std::string lenStr = MergeFloatsFmt(vlen, DisplayFloatFmt(cmd.displayLinearPrecision).c_str());
  std::string bearStr;
  if (vbear.empty()) {
    bearStr = "---";
  } else {
    const AngleDisplaySettings as = CadAngleDisplaySettings(cmd);
    bearStr = FormatBearing(static_cast<double>(vbear[0]), as);
    for (size_t i = 1; i < vbear.size(); ++i)
      if (FormatBearing(static_cast<double>(vbear[i]), as) != bearStr) { bearStr = kVaries; break; }
  }

  if (!PropSectionHeader("Geometry"))
    return;
  if (ImGui::BeginTable("props_geom_line", 2, kPropTableFlags)) {
    ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthStretch, 0.42f);
    ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch, 0.58f);
    PropRow("Start point", startPt);
    PropRow("End point", endPt);
    PropRow("Length", lenStr);
    PropRow("Rotation rel. north", bearStr);
    ImGui::EndTable();
  }
}

void DrawCircleGeometryOnly(const AppCommandState& cmd, const std::vector<SelectedEntity>& circlesOnly) {
  std::vector<float> cxv, cyv, rv, diamv, circv, areav;
  for (const auto& e : circlesOnly) {
    float cx = 0.f, cy = 0.f, r = 0.f;
    if (!ReadCircle(cmd, e.index, &cx, &cy, &r))
      continue;
    cxv.push_back(cx);
    cyv.push_back(cy);
    rv.push_back(r);
    diamv.push_back(2.f * r);
    constexpr float kPi = 3.14159265358979323846f;
    circv.push_back(2.f * kPi * r);
    areav.push_back(kPi * r * r);
  }
  const std::string ctr = [&]() -> std::string {
    if (cxv.empty())
      return "---";
    std::string ref = FormatXY(cxv[0], cyv[0], cmd.displayLinearPrecision);
    for (size_t i = 1; i < cxv.size(); ++i) {
      if (FormatXY(cxv[i], cyv[i], cmd.displayLinearPrecision) != ref)
        return kVaries;
    }
    return ref;
  }();

  if (!PropSectionHeader("Geometry"))
    return;
  if (ImGui::BeginTable("props_geom_circ", 2, kPropTableFlags)) {
    ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthStretch, 0.42f);
    ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch, 0.58f);
    const std::string cfmt = DisplayFloatFmt(cmd.displayLinearPrecision);
    PropRow("Center", ctr);
    PropRow("Radius", MergeFloatsFmt(rv, cfmt.c_str()));
    PropRow("Diameter", MergeFloatsFmt(diamv, cfmt.c_str()));
    PropRow("Circumference", MergeFloatsFmt(circv, cfmt.c_str()));
    PropRow("Area", MergeFloatsFmt(areav, cfmt.c_str()));
    ImGui::EndTable();
  }
}

void DrawSingleAnnotationGeometryEditable(AppCommandState& cmd, int annIdx) {
  if (!PropSectionHeader("Geometry"))
    return;
  if (annIdx < 0 || static_cast<size_t>(annIdx) >= cmd.cadAnnotations.size())
    return;
  EnsureAttrCounts(cmd);
  CadAnnotation& ann = cmd.cadAnnotations[static_cast<size_t>(annIdx)];
  const std::string cfmt = DisplayFloatFmt(cmd.displayLinearPrecision);

  const char* kindLabel =
      ann.kind == CadAnnotation::Kind::Text       ? "TEXT"
      : ann.kind == CadAnnotation::Kind::Mtext   ? "MTEXT"
      : ann.kind == CadAnnotation::Kind::DimAligned   ? "DIMALIGNED"
      : ann.kind == CadAnnotation::Kind::DimLinear    ? "DIMLINEAR"
                                                      : "?";

  auto syncMtextInsFromBox = [&]() {
    const float mnX = std::min(ann.boxMinX, ann.boxMaxX);
    const float mxX = std::max(ann.boxMinX, ann.boxMaxX);
    const float mnY = std::min(ann.boxMinY, ann.boxMaxY);
    const float mxY = std::max(ann.boxMinY, ann.boxMaxY);
    ann.boxMinX = mnX;
    ann.boxMaxX = mxX;
    ann.boxMinY = mnY;
    ann.boxMaxY = mxY;
    ann.insX = mnX;
    ann.insY = mnY;
  };

  if (ImGui::BeginTable("props_geom_ann_ed", 2, kPropTableFlags)) {
    ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthStretch, 0.38f);
    ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch, 0.62f);

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("Kind");
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(kindLabel);

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("Insertion X");
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputFloat("##ainsx", &ann.insX, 0.f, 0.f, cfmt.c_str());
    if (ImGui::IsItemDeactivatedAfterEdit()) {
      if (ann.kind == CadAnnotation::Kind::Mtext) {
        const float dx = ann.insX - ann.boxMinX;
        ann.boxMinX += dx;
        ann.boxMaxX += dx;
      }
      BumpCadGpuCache(cmd);
    }

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("Insertion Y");
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputFloat("##ainsy", &ann.insY, 0.f, 0.f, cfmt.c_str());
    if (ImGui::IsItemDeactivatedAfterEdit()) {
      if (ann.kind == CadAnnotation::Kind::Mtext) {
        const float dy = ann.insY - ann.boxMinY;
        ann.boxMinY += dy;
        ann.boxMaxY += dy;
      }
      BumpCadGpuCache(cmd);
    }

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("Plotted height (in)");
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputFloat("##annph", &ann.plottedHeightInches, 0.001f, 0.f, "%.4f");
    if (ann.plottedHeightInches <= 0.f)
      ann.plottedHeightInches = 0.0625f;
    if (ImGui::IsItemDeactivatedAfterEdit())
      BumpCadGpuCache(cmd);

    if (ann.kind == CadAnnotation::Kind::Text) {
      float rotDeg = BearingCwNorthDegFromMathAngleRad(ann.rotationRad);
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted("Rotation ° CW from N");
      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(-1);
      ImGui::InputFloat("##anntrot", &rotDeg, 0.f, 0.f, "%.2f");
      if (ImGui::IsItemDeactivatedAfterEdit()) {
        ann.rotationRad = MathAngleRadFromBearingCwNorthDeg(rotDeg);
        BumpCadGpuCache(cmd);
      }
    }

    if (ann.kind == CadAnnotation::Kind::Mtext) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted("Box min X");
      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(-1);
      ImGui::InputFloat("##bmix", &ann.boxMinX, 0.f, 0.f, cfmt.c_str());
      if (ImGui::IsItemDeactivatedAfterEdit()) {
        syncMtextInsFromBox();
        BumpCadGpuCache(cmd);
      }

      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted("Box min Y");
      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(-1);
      ImGui::InputFloat("##bmiy", &ann.boxMinY, 0.f, 0.f, cfmt.c_str());
      if (ImGui::IsItemDeactivatedAfterEdit()) {
        syncMtextInsFromBox();
        BumpCadGpuCache(cmd);
      }

      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted("Box max X");
      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(-1);
      ImGui::InputFloat("##bmax", &ann.boxMaxX, 0.f, 0.f, cfmt.c_str());
      if (ImGui::IsItemDeactivatedAfterEdit()) {
        syncMtextInsFromBox();
        BumpCadGpuCache(cmd);
      }

      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted("Box max Y");
      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(-1);
      ImGui::InputFloat("##bmay", &ann.boxMaxY, 0.f, 0.f, cfmt.c_str());
      if (ImGui::IsItemDeactivatedAfterEdit()) {
        syncMtextInsFromBox();
        BumpCadGpuCache(cmd);
      }
    }

    ImGui::EndTable();
  }

  ImGui::Spacing();
  ImGui::TextUnformatted("Content");
  ImGui::InputTextMultiline("##anntxtmul", &ann.text, ImVec2(-FLT_MIN, 96.f));
  if (ann.kind == CadAnnotation::Kind::Mtext)
    ImGui::TextDisabled("MTEXT wire: [[b]],[[i]],[[u]],[[caps]] with matching [[/…]]; DXF export is plain text.");
  if (ImGui::IsItemDeactivatedAfterEdit())
    BumpCadGpuCache(cmd);

  ImGui::Spacing();
  ImGui::TextDisabled("Derived");
  const float hWorld = CadAnnotationHeightWorld(ann, cmd.modelUnitsPerPlottedInch);
  const std::string hStr = FormatLinear(static_cast<double>(hWorld), cmd.displayLinearPrecision) + " model units";
  if (ImGui::BeginTable("props_geom_ann_derived", 2, kPropTableFlags)) {
    ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthStretch, 0.38f);
    ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch, 0.62f);
    PropRow("Model text height", hStr);
    if (ann.kind == CadAnnotation::Kind::Mtext) {
      const float bw = std::fabs(ann.boxMaxX - ann.boxMinX);
      const float bh = std::fabs(ann.boxMaxY - ann.boxMinY);
      PropRow("Box width", FormatLinear(static_cast<double>(bw), cmd.displayLinearPrecision));
      PropRow("Box height", FormatLinear(static_cast<double>(bh), cmd.displayLinearPrecision));
    }
    ImGui::EndTable();
  }
}

void DrawAnnotationGeometryOnly(const AppCommandState& cmd, const std::vector<SelectedEntity>& annOnly) {
  std::vector<std::string> kinds;
  std::vector<float> phIn, mwHeight, insX, insY, rotDeg;
  for (const auto& e : annOnly) {
    if (e.index < 0 || static_cast<size_t>(e.index) >= cmd.cadAnnotations.size())
      continue;
    const CadAnnotation& a = cmd.cadAnnotations[static_cast<size_t>(e.index)];
    kinds.push_back(a.kind == CadAnnotation::Kind::Text       ? "TEXT"
                    : a.kind == CadAnnotation::Kind::Mtext    ? "MTEXT"
                    : a.kind == CadAnnotation::Kind::DimAligned ? "DIMALIGNED"
                    : a.kind == CadAnnotation::Kind::DimLinear  ? "DIMLINEAR"
                                                              : "?");
    phIn.push_back(a.plottedHeightInches);
    mwHeight.push_back(CadAnnotationHeightWorld(a, cmd.modelUnitsPerPlottedInch));
    insX.push_back(a.insX);
    insY.push_back(a.insY);
    rotDeg.push_back(BearingCwNorthDegFromMathAngleRad(a.rotationRad));
  }

  if (!PropSectionHeader("Geometry"))
    return;
  if (ImGui::BeginTable("props_geom_ann", 2, kPropTableFlags)) {
    ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthStretch, 0.42f);
    ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch, 0.58f);
    PropRow("Kind", MergeStrings(kinds));
    const std::string mfmt = DisplayFloatFmt(cmd.displayLinearPrecision);
    PropRow("Plotted height (in)", MergeFloatsFmt(phIn, "%.4f"));
    PropRow("Model text height", MergeFloatsFmt(mwHeight, mfmt.c_str()));
    const int prec = cmd.displayLinearPrecision;
    const std::string insXs = [&]() -> std::string {
      if (insX.empty())
        return "---";
      std::string ref = FormatLinear(static_cast<double>(insX[0]), prec);
      for (size_t i = 1; i < insX.size(); ++i) {
        if (FormatLinear(static_cast<double>(insX[i]), prec) != ref)
          return kVaries;
      }
      return ref;
    }();
    const std::string insYs = [&]() -> std::string {
      if (insY.empty())
        return "---";
      std::string ref = FormatLinear(static_cast<double>(insY[0]), prec);
      for (size_t i = 1; i < insY.size(); ++i) {
        if (FormatLinear(static_cast<double>(insY[i]), prec) != ref)
          return kVaries;
      }
      return ref;
    }();
    PropRow("Insertion X", insXs);
    PropRow("Insertion Y", insYs);
    const std::string rotStr = [&]() -> std::string {
      if (rotDeg.empty())
        return "---";
      const AngleDisplaySettings as = CadAngleDisplaySettings(cmd);
      std::string ref = FormatBearing(static_cast<double>(rotDeg[0]), as);
      for (size_t i = 1; i < rotDeg.size(); ++i)
        if (FormatBearing(static_cast<double>(rotDeg[i]), as) != ref)
          return kVaries;
      return ref;
    }();
    PropRow("Rotation rel. north", rotStr);
    ImGui::EndTable();
  }
  ImGui::TextDisabled("Select a single TEXT or MTEXT to edit content and box here.");
}

int PickSurveyPointIndex(const std::vector<SurveyPoint>& pts, double wx, double wy, float surveyCrossHalfWorld,
                         float viewportHeightPx, float orthoHalfHeightWorld, float viewportPickAperturePx) {
  if (pts.empty())
    return -1;
  const float arm = std::max(surveyCrossHalfWorld, 1.e-8f);
  const float tol = CadSnap::WorldToleranceFromPixels(viewportHeightPx, orthoHalfHeightWorld, viewportPickAperturePx);
  const double radius = static_cast<double>(std::max(arm, tol)) * 1.38;
  const double r2 = radius * radius;
  int best = -1;
  double bestD2 = 0.0;
  // Distances are computed in double: at state-plane magnitudes a float subtraction of two ~1e7 coordinates
  // loses ~1 ft of precision, so the hit test stops matching the rendered cross position.
  for (size_t i = 0; i < pts.size(); ++i) {
    const double dx = wx - static_cast<double>(pts[i].easting);
    const double dy = wy - static_cast<double>(pts[i].northing);
    const double d2 = dx * dx + dy * dy;
    if (d2 <= r2 && (best < 0 || d2 < bestD2)) {
      bestD2 = d2;
      best = static_cast<int>(i);
    }
  }
  return best;
}

void DrawSurveyPointPickProps(AppCommandState& cmd, std::vector<std::string>* log) {
  std::vector<std::string> discard;
  if (!log)
    log = &discard;
  const auto& ixv = cmd.selectedSurveyPointIndices;
  if (ixv.empty())
    return;

  cmd.surveyPointIdBuffers.resize(cmd.surveyPoints.size());

  if (ixv.size() == 1) {
    const int rowIx = ixv.front();
    if (rowIx < 0 || static_cast<size_t>(rowIx) >= cmd.surveyPoints.size())
      return;
    SurveyPoint& p = cmd.surveyPoints[static_cast<size_t>(rowIx)];
    if (cmd.surveyPointIdBuffers[static_cast<size_t>(rowIx)].empty())
      cmd.surveyPointIdBuffers[static_cast<size_t>(rowIx)] = std::to_string(p.id);
    ImGui::TextUnformatted("Survey — 1 point");
    if (ImGui::BeginTable("props_pick_survey", 2, kPropTableFlags)) {
      ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthStretch, 0.42f);
      ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch, 0.58f);
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted("Label style");
      ImGui::TableNextColumn();
      {
        int styleI = static_cast<int>(p.labelStyle);
        styleI = std::clamp(styleI, 0, static_cast<int>(SurveyPointLabelStyle::NumberNorthEastElev));
        const char* items =
            "None\0"
            "Point number and description\0"
            "Point number only\0"
            "Description only\0"
            "Point number and elevation\0"
            "Point number, elevation, and description\0"
            "Point number, northing, and easting\0"
            "Northing and easting\0"
            "Point number, northing, easting, and elevation\0\0";
        if (ImGui::Combo("##svy_lbl_style", &styleI, items)) {
          p.labelStyle = static_cast<SurveyPointLabelStyle>(styleI);
          EnsureSurveyPointLabelMtext(cmd, static_cast<size_t>(rowIx), log);
          SyncSurveyPointLinkedMtextSelection(cmd, rowIx);
        }
      }
      // Label color (via cadAnnotationAttrs of the linked label).
      if (p.labelMtextAnnIndex >= 0 && static_cast<size_t>(p.labelMtextAnnIndex) < cmd.cadAnnotationAttrs.size()) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Label color");
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
        EntityAttributes& lattr = cmd.cadAnnotationAttrs[static_cast<size_t>(p.labelMtextAnnIndex)];
        if (ImGui::InputText("##svy_lbl_color", &lattr.color, ImGuiInputTextFlags_EnterReturnsTrue))
          BumpCadGpuCache(cmd);
        if (ImGui::IsItemDeactivatedAfterEdit())
          BumpCadGpuCache(cmd);
      }
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted("Point ID");
      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(-FLT_MIN);
      ImGui::InputText("##svy_id", &cmd.surveyPointIdBuffers[static_cast<size_t>(rowIx)]);
      if (ImGui::IsItemDeactivatedAfterEdit()) {
        std::string t = StringUtil::trimCopy(cmd.surveyPointIdBuffers[static_cast<size_t>(rowIx)]);
        char* end = nullptr;
        const long v = std::strtol(t.c_str(), &end, 10);
        const bool parsed =
            end == t.c_str() + static_cast<std::ptrdiff_t>(t.size()) && end != t.c_str();
        if (!parsed) {
          log->push_back("Properties — point ID must be a whole number.");
          cmd.surveyPointIdBuffers[static_cast<size_t>(rowIx)] = std::to_string(p.id);
        } else {
          const int nid = static_cast<int>(v);
          bool dup = false;
          for (size_t j = 0; j < cmd.surveyPoints.size(); ++j) {
            if (j != static_cast<size_t>(rowIx) && cmd.surveyPoints[j].id == nid)
              dup = true;
          }
          if (dup) {
            log->push_back("Properties — duplicate point ID " + std::to_string(nid) + ".");
            cmd.surveyPointIdBuffers[static_cast<size_t>(rowIx)] = std::to_string(p.id);
          } else {
            p.id = nid;
            cmd.surveyPointIdBuffers[static_cast<size_t>(rowIx)] = std::to_string(nid);
          }
        }
        EnsureSurveyPointLabelMtext(cmd, static_cast<size_t>(rowIx), log);
      }
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted("Northing (Y)");
      ImGui::TableNextColumn();
      {
        double dn = static_cast<double>(CadCoord::WorldYFromLocal(cmd, p.northing));
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputDouble("##svy_n", &dn, 0., 0., DisplayFloatFmt(cmd.surveyPointDisplayPrecision).c_str());
        if (ImGui::IsItemDeactivatedAfterEdit()) {
          const double wx = static_cast<double>(CadCoord::WorldXFromLocal(cmd, p.easting));
          CadCoord::LocalFromWorld(cmd, wx, dn, &p.easting, &p.northing);
          EnsureSurveyPointLabelMtext(cmd, static_cast<size_t>(rowIx), log);
        }
      }
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted("Easting (X)");
      ImGui::TableNextColumn();
      {
        double de = static_cast<double>(CadCoord::WorldXFromLocal(cmd, p.easting));
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputDouble("##svy_e", &de, 0., 0., DisplayFloatFmt(cmd.surveyPointDisplayPrecision).c_str());
        if (ImGui::IsItemDeactivatedAfterEdit()) {
          const double wy = static_cast<double>(CadCoord::WorldYFromLocal(cmd, p.northing));
          CadCoord::LocalFromWorld(cmd, de, wy, &p.easting, &p.northing);
          EnsureSurveyPointLabelMtext(cmd, static_cast<size_t>(rowIx), log);
        }
      }
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted("Elevation");
      ImGui::TableNextColumn();
      {
        double dz = static_cast<double>(p.elevation);
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputDouble("##svy_z", &dz, 0., 0., DisplayFloatFmt(cmd.surveyPointDisplayPrecision).c_str());
        if (ImGui::IsItemDeactivatedAfterEdit()) {
          p.elevation = static_cast<float>(dz);
          EnsureSurveyPointLabelMtext(cmd, static_cast<size_t>(rowIx), log);
        }
      }
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted("Layer");
      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(-FLT_MIN);
      ImGui::InputText("##svy_layer", &p.layer);
      if (ImGui::IsItemDeactivatedAfterEdit())
        RepositionSurveyLabelMtextForPoint(cmd, static_cast<size_t>(rowIx));
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted("Description");
      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(-FLT_MIN);
      ImGui::InputTextMultiline("##svy_desc", &p.description, ImVec2(-FLT_MIN, 72.f));
      if (ImGui::IsItemDeactivatedAfterEdit())
        EnsureSurveyPointLabelMtext(cmd, static_cast<size_t>(rowIx), log);
      ImGui::EndTable();
    }
    return;
  }

  ImGui::Text("Survey — %zu points", ixv.size());
  ImGui::Separator();

  static uint64_t gMultiFp = ~0ull;
  static std::string gBufE, gBufN, gBufZ, gBufLayer, gBufDesc, gBufId;
  static bool gSameStyle = true;
  static int gStyleLead = 0;
  static bool gSameId = true;
  static bool gSameE = true, gSameN = true, gSameZ = true, gSameLayer = true, gSameDesc = true;

  static const char* kLblStyleNames[] = {
      "None",
      "Point number and description",
      "Point number only",
      "Description only",
      "Point number and elevation",
      "Point number, elevation, and description",
      "Point number, northing, and easting",
      "Northing and easting",
      "Point number, northing, easting, and elevation",
  };

  constexpr float kHorizTol = 5e-5f;
  constexpr float kElevTol = 5e-4f;
  auto sameHoriz = [](float a, float b) { return std::fabs(a - b) <= kHorizTol; };
  auto sameElev = [](float a, float b) { return std::fabs(a - b) <= kElevTol; };

  const uint64_t fp = [&]() {
    std::vector<int> sorted(ixv.begin(), ixv.end());
    std::sort(sorted.begin(), sorted.end());
    uint64_t h = 1469598103934665603ull;
    h ^= sorted.size() * 0x9e3779b9u;
    for (int ix : sorted) {
      h ^= static_cast<uint64_t>(static_cast<uint32_t>(ix)) + 0x9e3779b97f4a7c15ull;
      h *= 1099511628211ull;
    }
    return h;
  }();
  if (fp != gMultiFp) {
    gMultiFp = fp;
    const int i0 = ixv.front();
    if (i0 >= 0 && static_cast<size_t>(i0) < cmd.surveyPoints.size()) {
      const SurveyPoint& r = cmd.surveyPoints[static_cast<size_t>(i0)];
      gStyleLead = static_cast<int>(r.labelStyle);
      gSameStyle = true;
      gSameId = true;
      gSameE = gSameN = gSameZ = gSameLayer = gSameDesc = true;
      for (int ix : ixv) {
        if (ix < 0 || static_cast<size_t>(ix) >= cmd.surveyPoints.size())
          continue;
        const SurveyPoint& q = cmd.surveyPoints[static_cast<size_t>(ix)];
        if (static_cast<int>(q.labelStyle) != gStyleLead)
          gSameStyle = false;
        if (q.id != r.id)
          gSameId = false;
        if (!sameHoriz(q.easting, r.easting))
          gSameE = false;
        if (!sameHoriz(q.northing, r.northing))
          gSameN = false;
        if (!sameElev(q.elevation, r.elevation))
          gSameZ = false;
        if (q.layer != r.layer)
          gSameLayer = false;
        if (q.description != r.description)
          gSameDesc = false;
      }
      const int sprec = cmd.surveyPointDisplayPrecision;
      gBufId = gSameId ? std::to_string(r.id) : std::string("VARIES");
      if (gSameE)
        gBufE = FormatLinear(static_cast<double>(CadCoord::WorldXFromLocal(cmd, r.easting)), sprec);
      else
        gBufE = "VARIES";
      if (gSameN)
        gBufN = FormatLinear(static_cast<double>(CadCoord::WorldYFromLocal(cmd, r.northing)), sprec);
      else
        gBufN = "VARIES";
      if (gSameZ)
        gBufZ = FormatLinear(static_cast<double>(r.elevation), sprec);
      else
        gBufZ = "VARIES";
      gBufLayer = gSameLayer ? r.layer : std::string("VARIES");
      gBufDesc = gSameDesc ? r.description : std::string("VARIES");
    }
  }

  if (ImGui::BeginTable("props_pick_survey_m", 2, kPropTableFlags)) {
    ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthStretch, 0.42f);
    ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch, 0.58f);
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("Label style");
    ImGui::TableNextColumn();
    {
      gStyleLead = std::clamp(gStyleLead, 0, static_cast<int>(SurveyPointLabelStyle::NumberNorthEastElev));
      const char* preview = gSameStyle ? kLblStyleNames[gStyleLead] : "VARIES";
      ImGui::SetNextItemWidth(-FLT_MIN);
      if (ImGui::BeginCombo("##svy_lbl_style_m", preview)) {
        for (int si = 0; si <= static_cast<int>(SurveyPointLabelStyle::NumberNorthEastElev); ++si) {
          const bool selected = gSameStyle && si == gStyleLead;
          if (ImGui::Selectable(kLblStyleNames[si], selected)) {
            for (int ix : ixv) {
              if (ix < 0 || static_cast<size_t>(ix) >= cmd.surveyPoints.size())
                continue;
              cmd.surveyPoints[static_cast<size_t>(ix)].labelStyle = static_cast<SurveyPointLabelStyle>(si);
              EnsureSurveyPointLabelMtext(cmd, static_cast<size_t>(ix), log);
              SyncSurveyPointLinkedMtextSelection(cmd, ix);
            }
            gMultiFp = ~0ull;
          }
        }
        ImGui::EndCombo();
      }
    }

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("Point ID");
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputText("##svy_id_m", &gBufId, ImGuiInputTextFlags_ReadOnly);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort) && ImGui::BeginTooltip()) {
      ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.f);
      ImGui::TextUnformatted(
          "Point IDs must stay unique. Edit ID when only one survey point is selected, or use VIEWPOINTS (VWPTS) "
          "for the table.");
      ImGui::PopTextWrapPos();
      ImGui::EndTooltip();
    }

    auto applyCoord = [&](const char* label, const char* idSame, const char* idVaries, std::string* buf,
                          bool sameFlag, float SurveyPoint::* memb, const char* fmt) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(label);
      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(-FLT_MIN);
      if (sameFlag) {
        const int refIx = ixv.front();
        const SurveyPoint& ref = cmd.surveyPoints[static_cast<size_t>(refIx)];
        double dv = static_cast<double>(ref.*memb);
        if (memb == &SurveyPoint::easting)
          dv = static_cast<double>(CadCoord::WorldXFromLocal(cmd, ref.easting));
        else if (memb == &SurveyPoint::northing)
          dv = static_cast<double>(CadCoord::WorldYFromLocal(cmd, ref.northing));
        ImGui::InputDouble(idSame, &dv, 0., 0., fmt);
        if (ImGui::IsItemDeactivatedAfterEdit()) {
          for (int ix : ixv) {
            if (ix < 0 || static_cast<size_t>(ix) >= cmd.surveyPoints.size())
              continue;
            SurveyPoint& pt = cmd.surveyPoints[static_cast<size_t>(ix)];
            if (memb == &SurveyPoint::easting) {
              const double wy = static_cast<double>(CadCoord::WorldYFromLocal(cmd, pt.northing));
              CadCoord::LocalFromWorld(cmd, dv, wy, &pt.easting, &pt.northing);
            } else if (memb == &SurveyPoint::northing) {
              const double wx = static_cast<double>(CadCoord::WorldXFromLocal(cmd, pt.easting));
              CadCoord::LocalFromWorld(cmd, wx, dv, &pt.easting, &pt.northing);
            } else
              pt.*memb = static_cast<float>(dv);
            EnsureSurveyPointLabelMtext(cmd, static_cast<size_t>(ix), log);
          }
          gMultiFp = ~0ull;
        }
      } else {
        ImGui::InputText(idVaries, buf);
        if (ImGui::IsItemDeactivatedAfterEdit()) {
          std::string t = StringUtil::trimCopy(*buf);
          if (t == "VARIES" || t.empty()) {
            log->push_back("Properties — enter a numeric value to apply to all selected points.");
            gMultiFp = ~0ull;
            return;
          }
          char* end = nullptr;
          const double v = std::strtod(t.c_str(), &end);
          if (end == t.c_str()) {
            log->push_back(std::string("Properties — invalid number for ") + label + ".");
            gMultiFp = ~0ull;
            return;
          }
          for (int ix : ixv) {
            if (ix < 0 || static_cast<size_t>(ix) >= cmd.surveyPoints.size())
              continue;
            SurveyPoint& pt = cmd.surveyPoints[static_cast<size_t>(ix)];
            if (memb == &SurveyPoint::easting) {
              const double wy = static_cast<double>(CadCoord::WorldYFromLocal(cmd, pt.northing));
              CadCoord::LocalFromWorld(cmd, v, wy, &pt.easting, &pt.northing);
            } else if (memb == &SurveyPoint::northing) {
              const double wx = static_cast<double>(CadCoord::WorldXFromLocal(cmd, pt.easting));
              CadCoord::LocalFromWorld(cmd, wx, v, &pt.easting, &pt.northing);
            } else
              pt.*memb = static_cast<float>(v);
            EnsureSurveyPointLabelMtext(cmd, static_cast<size_t>(ix), log);
          }
          gMultiFp = ~0ull;
        }
      }
    };

    const std::string svyFmt = DisplayFloatFmt(cmd.surveyPointDisplayPrecision);
    applyCoord("Northing (Y)", "##svy_m_n_d", "##svy_m_n", &gBufN, gSameN, &SurveyPoint::northing, svyFmt.c_str());
    applyCoord("Easting (X)", "##svy_m_e_d", "##svy_m_e", &gBufE, gSameE, &SurveyPoint::easting, svyFmt.c_str());
    applyCoord("Elevation", "##svy_m_z_d", "##svy_m_z", &gBufZ, gSameZ, &SurveyPoint::elevation, svyFmt.c_str());

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("Layer");
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputText("##svy_layer_m", &gBufLayer);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
      std::string t = StringUtil::trimCopy(gBufLayer);
      if (t != "VARIES") {
        for (int ix : ixv) {
          if (ix < 0 || static_cast<size_t>(ix) >= cmd.surveyPoints.size())
            continue;
          cmd.surveyPoints[static_cast<size_t>(ix)].layer = t;
          RepositionSurveyLabelMtextForPoint(cmd, static_cast<size_t>(ix));
        }
        BumpCadGpuCache(cmd);
      }
      gMultiFp = ~0ull;
    }

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("Description");
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextMultiline("##svy_desc_m", &gBufDesc, ImVec2(-FLT_MIN, 72.f));
    if (ImGui::IsItemDeactivatedAfterEdit()) {
      std::string t = StringUtil::trimCopy(gBufDesc);
      if (t != "VARIES") {
        for (int ix : ixv) {
          if (ix < 0 || static_cast<size_t>(ix) >= cmd.surveyPoints.size())
            continue;
          cmd.surveyPoints[static_cast<size_t>(ix)].description = t;
          EnsureSurveyPointLabelMtext(cmd, static_cast<size_t>(ix), log);
        }
      }
      gMultiFp = ~0ull;
    }

    ImGui::EndTable();
  }
}

} // namespace

void DrawPropertiesPanel(AppCommandState& cmd, std::vector<std::string>* log) {
  ImGui::SetNextWindowSize(ImVec2(320, 560), ImGuiCond_FirstUseEver);
  if (cmd.pendingPropertiesFocus)
    ImGui::SetNextWindowFocus();
  if (!ImGui::Begin("Properties", nullptr)) {
    cmd.propertiesPanelActive = false;
    ImGui::End();
    return;
  }
  cmd.propertiesPanelActive = true;

  auto& svyIx = cmd.selectedSurveyPointIndices;
  svyIx.erase(std::remove_if(svyIx.begin(), svyIx.end(),
                             [&](int ix) { return ix < 0 || static_cast<size_t>(ix) >= cmd.surveyPoints.size(); }),
              svyIx.end());

  const auto& sel = cmd.selection;
  const bool haveSurveyPick = !svyIx.empty();
  const bool haveCadSel = !sel.empty();

  if (!haveSurveyPick && !haveCadSel) {
    gPropsSelFingerprint = ~0ull;
    if (PropSectionHeader("General")) {
      ImGui::BeginDisabled();
      if (ImGui::BeginTable("props_gen_empty", 2, kPropTableFlags)) {
        ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthStretch, 0.38f);
        ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch, 0.62f);
        static const char* kGeneralRows[] = {
          "Layer", "Layer list", "Color", "Linetype", "Lineweight", "Transparency", "Plot style"
        };
        for (const char* label : kGeneralRows) {
          ImGui::TableNextRow();
          PropValueCellBg();
          ImGui::TableNextColumn(); ImGui::TextUnformatted(label);
          ImGui::TableNextColumn(); ImGui::TextDisabled("\xe2\x80\x94");  // em dash
        }
        ImGui::EndTable();
      }
      ImGui::EndDisabled();
    }
    if (PropSectionHeader("Geometry")) {
      ImGui::BeginDisabled();
      if (ImGui::BeginTable("props_geo_empty", 2, kPropTableFlags)) {
        ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthStretch, 0.38f);
        ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch, 0.62f);
        static const char* kGeomRows[] = {"Start X", "Start Y", "End X", "End Y"};
        for (const char* label : kGeomRows) {
          ImGui::TableNextRow();
          PropValueCellBg();
          ImGui::TableNextColumn(); ImGui::TextUnformatted(label);
          ImGui::TableNextColumn(); ImGui::TextDisabled("\xe2\x80\x94");
        }
        ImGui::EndTable();
      }
      ImGui::EndDisabled();
    }
    FillPropPanelEmpty();
    ImGui::End();
    return;
  }

  if (haveSurveyPick) {
    DrawSurveyPointPickProps(cmd, log);
    ImGui::Separator();
  }

  if (!haveCadSel) {
    gPropsSelFingerprint = ~0ull;
    ImGui::TextDisabled("Bulk editing: VIEWPOINTS (VWPTS).");
    FillPropPanelEmpty();
    ImGui::End();
    return;
  }

  EnsureAttrCounts(cmd);

  const uint64_t fp = SelectionFingerprint(sel);
  if (fp != gPropsSelFingerprint) {
    gPropsSelFingerprint = fp;
    RefreshPropsBuffersFromModel(cmd, sel);
  }

  int nLine = 0;
  int nCirc = 0;
  int nAnn  = 0;
  int nPdf  = 0;
  for (const auto& e : sel) {
    if      (e.type == SelectedEntity::Type::LineSeg)    ++nLine;
    else if (e.type == SelectedEntity::Type::Circle)     ++nCirc;
    else if (e.type == SelectedEntity::Type::Annotation) ++nAnn;
    else if (e.type == SelectedEntity::Type::PdfUnderlay)++nPdf;
  }

  ImGui::Text("Selected: %d object(s)", static_cast<int>(sel.size()));
  const int typeKinds = (nLine > 0 ? 1 : 0) + (nCirc > 0 ? 1 : 0) + (nAnn > 0 ? 1 : 0) + (nPdf > 0 ? 1 : 0);
  if (typeKinds > 1)
    ImGui::TextDisabled("(Mixed: Line %d, Circle %d, Ann %d, PDF %d)", nLine, nCirc, nAnn, nPdf);
  else if (nLine > 1)
    ImGui::TextDisabled("%d lines", nLine);
  else if (nCirc > 1)
    ImGui::TextDisabled("%d circles", nCirc);
  else if (nAnn > 1)
    ImGui::TextDisabled("%d annotations", nAnn);
  else if (nPdf > 1)
    ImGui::TextDisabled("%d PDF underlays", nPdf);
  else if (nLine == 1)
    ImGui::TextDisabled("Line");
  else if (nCirc == 1)
    ImGui::TextDisabled("Circle");
  else if (nPdf == 1)
    ImGui::TextDisabled("PDF Underlay");
  else if (nAnn == 1) {
    int ix = -1;
    for (const auto& e : sel) {
      if (e.type == SelectedEntity::Type::Annotation) {
        ix = e.index;
        break;
      }
    }
    if (ix >= 0 && static_cast<size_t>(ix) < cmd.cadAnnotations.size()) {
      const CadAnnotation::Kind k = cmd.cadAnnotations[static_cast<size_t>(ix)].kind;
      const char* lab = k == CadAnnotation::Kind::Text       ? "TEXT"
                        : k == CadAnnotation::Kind::Mtext    ? "MTEXT"
                        : k == CadAnnotation::Kind::DimAligned ? "DIMALIGNED"
                        : k == CadAnnotation::Kind::DimLinear  ? "DIMLINEAR"
                                                              : "Annotation";
      ImGui::TextDisabled("%s", lab);
    } else
      ImGui::TextDisabled("Annotation");
  }

  ImGui::Separator();

  DrawEditableGeneralSection(cmd, sel);

  if (nLine == 0 && nCirc == 0 && nAnn > 0) {
    std::vector<SelectedEntity> annOnly;
    annOnly.reserve(static_cast<size_t>(nAnn));
    for (const auto& e : sel) {
      if (e.type == SelectedEntity::Type::Annotation)
        annOnly.push_back(e);
    }
    if (annOnly.size() == 1)
      DrawSingleAnnotationGeometryEditable(cmd, annOnly.front().index);
    else
      DrawAnnotationGeometryOnly(cmd, annOnly);
  } else if (nCirc == 0 && nAnn == 0 && nLine > 0) {
    std::vector<SelectedEntity> linesOnly;
    linesOnly.reserve(static_cast<size_t>(nLine));
    for (const auto& e : sel) {
      if (e.type == SelectedEntity::Type::LineSeg)
        linesOnly.push_back(e);
    }
    if (linesOnly.size() == 1)
      DrawSingleLineGeometryEditable(cmd, linesOnly.front().index);
    else
      DrawLineGeometryOnly(cmd, linesOnly);
  } else if (nLine == 0 && nAnn == 0 && nCirc > 0) {
    std::vector<SelectedEntity> circOnly;
    circOnly.reserve(static_cast<size_t>(nCirc));
    for (const auto& e : sel) {
      if (e.type == SelectedEntity::Type::Circle)
        circOnly.push_back(e);
    }
    if (circOnly.size() == 1)
      DrawSingleCircleGeometryEditable(cmd, circOnly.front().index);
    else
      DrawCircleGeometryOnly(cmd, circOnly);
  } else if (nPdf > 0 && nLine == 0 && nCirc == 0 && nAnn == 0) {
    // PDF Underlay properties — single or multi
    int pdfIdx = -1;
    for (const auto& e : sel)
      if (e.type == SelectedEntity::Type::PdfUnderlay) { pdfIdx = e.index; break; }

    if (nPdf == 1 && pdfIdx >= 0 && pdfIdx < static_cast<int>(cmd.pdfAttachments.size())) {
      PdfAttachment& att = cmd.pdfAttachments[static_cast<size_t>(pdfIdx)];
      if (PropSectionHeader("PDF Underlay")) {
        ImGui::TextDisabled("File: %s", att.filePath.c_str());
        ImGui::TextDisabled("Page: %d", att.pageIndex + 1);
        ImGui::Separator();

        char layBuf[128] = {};
        att.layer.copy(layBuf, sizeof(layBuf) - 1);
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::InputText("Layer##pdfprop", layBuf, sizeof(layBuf)))
          att.layer = layBuf;

        ImGui::Checkbox("Show Paper Background##pdfprop", &att.showBackground);
        ImGui::SetNextItemWidth(-FLT_MIN);
        float fadePct = att.fade * 100.f;
        if (ImGui::SliderFloat("Fade##pdfprop", &fadePct, 0.f, 100.f, "%.0f%%"))
          att.fade = fadePct / 100.f;

        ImGui::Separator();
        ImGui::TextDisabled("Placement");
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputFloat("Insert X##pdfprop", &att.insertX, 0.f, 0.f, DisplayFloatFmt(cmd.displayLinearPrecision).c_str());
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputFloat("Insert Y##pdfprop", &att.insertY, 0.f, 0.f, DisplayFloatFmt(cmd.displayLinearPrecision).c_str());
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputFloat("Scale##pdfprop", &att.scale, 0.f, 0.f, "%.6f");
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputFloat("Rotation (deg)##pdfprop", &att.rotationDeg, 0.f, 0.f, "%.2f");

        ImGui::Separator();
        ImGui::TextDisabled("Object Snap");
        ImGui::Checkbox("Lines##pdfsnap",   &att.snapLines);
        ImGui::SameLine();
        ImGui::Checkbox("Circles##pdfsnap", &att.snapCircles);
        ImGui::SameLine();
        ImGui::Checkbox("Text##pdfsnap",    &att.snapText);
      }
    } else {
      if (PropSectionHeader("PDF Underlay")) {
        ImGui::TextDisabled("%d PDF underlays selected.", nPdf);
        // Bulk fade
        float fadePct = 50.f; // no common value; just a slider stub
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::SliderFloat("Fade##pdfbulk", &fadePct, 0.f, 100.f, "%.0f%%")) {
          for (const auto& e : sel)
            if (e.type == SelectedEntity::Type::PdfUnderlay &&
                e.index >= 0 && e.index < static_cast<int>(cmd.pdfAttachments.size()))
              cmd.pdfAttachments[static_cast<size_t>(e.index)].fade = fadePct / 100.f;
        }
      }
    }
  } else {
    if (PropSectionHeader("Geometry")) {
      ImGui::TextWrapped("Mixed entity types — geometry is read-only here. Edit General above, or select only "
                         "lines, circles, or annotations.");
    }
  }

  if (haveSurveyPick) {
    ImGui::Separator();
    ImGui::TextDisabled("Survey bulk edit: VIEWPOINTS (VWPTS).");
  }

  FillPropPanelEmpty();
  ImGui::End();
}

namespace {

static bool gPolarTrackingEnabled = false;

void PushModeToggleButtonColors(bool on, int themeIdx) {
  if (!on)
    return;
  if (themeIdx == 0) {
    // Dark mode: dark blue tint — clearly "on" against deep panel background
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.063f, 0.141f, 0.345f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.086f, 0.180f, 0.420f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.051f, 0.114f, 0.290f, 1.f));
  } else {
    // Light mode: Primary Blue fills — clearly active against light gray
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.231f, 0.510f, 0.965f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.310f, 0.565f, 0.980f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.180f, 0.431f, 0.831f, 1.f));
  }
}

void PopModeToggleButtonColors(bool on) {
  if (on)
    ImGui::PopStyleColor(3);
}

static void ItemHelpTooltip(const char* text) {
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort) && ImGui::BeginTooltip()) {
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
  }
}

/// \p modelUnitsPerPlottedInch matches common civil notation (e.g. 50 → 1"=50' when model unit is feet).
static void DrawPlotScaleCombo(AppCommandState& cmd) {
  static constexpr struct {
    const char* label;
    float modelUnitsPerPlottedInch;
  } kScales[] = {
      {"1\" = 1'", 1.f},       {"1\" = 2'", 2.f},       {"1\" = 5'", 5.f},       {"1\" = 10'", 10.f},
      {"1\" = 20'", 20.f},     {"1\" = 30'", 30.f},     {"1\" = 40'", 40.f},     {"1\" = 50'", 50.f},
      {"1\" = 60'", 60.f},     {"1\" = 80'", 80.f},     {"1\" = 100'", 100.f},   {"1\" = 120'", 120.f},
      {"1\" = 200'", 200.f},   {"1\" = 300'", 300.f},   {"1\" = 400'", 400.f},   {"1\" = 500'", 500.f},
  };

  constexpr int kN = static_cast<int>(sizeof(kScales) / sizeof(kScales[0]));

  // Target: the viewport we're "in" (floating), else a single selected viewport in paper space, else the
  // drawing's model plot scale. The combo then sets that viewport's scale (user request).
  Viewport* tvp = nullptr;
  if (InFloatingModelSpace(cmd) && cmd.floatingViewportLayout >= 0 &&
      cmd.floatingViewportLayout < static_cast<int>(cmd.paperLayouts.size())) {
    PaperLayout& L = cmd.paperLayouts[static_cast<size_t>(cmd.floatingViewportLayout)];
    if (cmd.floatingViewportIndex >= 0 && cmd.floatingViewportIndex < static_cast<int>(L.viewports.size()))
      tvp = &L.viewports[static_cast<size_t>(cmd.floatingViewportIndex)];
  } else if (cmd.activeSpaceIndex >= 0 && cmd.activeSpaceIndex < static_cast<int>(cmd.paperLayouts.size()) &&
             cmd.selectedViewports.size() == 1) {
    PaperLayout& L = cmd.paperLayouts[static_cast<size_t>(cmd.activeSpaceIndex)];
    if (cmd.selectedViewportIndex >= 0 && cmd.selectedViewportIndex < static_cast<int>(L.viewports.size()))
      tvp = &L.viewports[static_cast<size_t>(cmd.selectedViewportIndex)];
  }
  const float curVal = tvp ? tvp->scaleModelPerPaperIn : cmd.modelUnitsPerPlottedInch;

  int selected = -1;
  for (int i = 0; i < kN; ++i) {
    if (std::fabs(curVal - kScales[i].modelUnitsPerPlottedInch) < 0.051f) {
      selected = i;
      break;
    }
  }

  char preview[96];
  const char* pfx = tvp ? "VP " : "";
  if (selected >= 0)
    std::snprintf(preview, sizeof(preview), "%s%s", pfx, kScales[selected].label);
  else
    std::snprintf(preview, sizeof(preview), "%s1\" = %.3g' (custom)", pfx, static_cast<double>(curVal));

  ImGui::PushID("plotscalecombo");
  ImGui::SetNextItemWidth(158.f);
  if (ImGui::BeginCombo("##plotscale", preview, ImGuiComboFlags_HeightLargest)) {
    for (int i = 0; i < kN; ++i) {
      const bool isSel = (selected == i);
      if (ImGui::Selectable(kScales[i].label, isSel)) {
        if (tvp) {
          tvp->scaleModelPerPaperIn = kScales[i].modelUnitsPerPlottedInch;  // set THIS viewport's scale
          BumpCadGpuCache(cmd);
        } else {
          cmd.modelUnitsPerPlottedInch = kScales[i].modelUnitsPerPlottedInch;
          RepositionAllSurveyPointLabels(cmd);
          cmd.surveyLabelLayoutCacheHalfH = cmd.viewportLastSurveyLayoutOrthoHalfH;
          cmd.surveyLabelLayoutCacheVpHeightPx = cmd.viewportLastSurveyLayoutHeightPx;
          cmd.surveyLabelLayoutCacheMup = cmd.modelUnitsPerPlottedInch;
          BumpCadGpuCache(cmd);
        }
      }
      if (isSel)
        ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  ItemHelpTooltip(tvp ? "Viewport scale: model units per paper inch for the active/selected viewport."
                      : "Drawing scale: model units per plotted inch (e.g. 50 for 1\" = 50'). "
                        "Use PSCALE for values not in the list.");
  ImGui::PopID();
}

} // namespace

static const char* CommandInputHint(const AppCommandState& cmd) {
  if (cmd.active == AppCommandState::Kind::PaperRectViewport)
    return cmd.paperVpPhase == 0 ? "Rectangular viewport — first corner (click on the sheet):"
                                 : "Rectangular viewport — opposite corner:";
  if (cmd.active == AppCommandState::Kind::Line) {
    using SAP = AppCommandState::SegmentAnglePickPhase;
    if (cmd.linePhase == AppCommandState::LinePhase::NeedFirstPoint)
      return "First point (click or X,Y):";
    if (cmd.linePhase == AppCommandState::LinePhase::NeedNextPoint && cmd.segmentAngleKeyboardAwaitBearing)
      return "LINE bearing ° (CW from N); blank Enter cancels:";
    if (cmd.linePhase == AppCommandState::LinePhase::NeedNextPoint && cmd.segmentAnglePickPhase == SAP::WaitP1)
      return "Bearing pick — first click:";
    if (cmd.linePhase == AppCommandState::LinePhase::NeedNextPoint && cmd.segmentAnglePickPhase == SAP::WaitP2)
      return "Bearing pick — second click:";
    if (cmd.linePhase == AppCommandState::LinePhase::NeedNextPoint &&
        cmd.segmentAnglePickPhase == SAP::WaitAdjustOrCommit)
      return "Bearing pick — Enter or +90/-45:";
    if (cmd.linePhase == AppCommandState::LinePhase::NeedNextPoint && cmd.segmentAngleLockActive)
      return "LINE distance ± / click ray / X,Y / A clears:";
    if (cmd.orthoMode)
      return "Next — click / X,Y / @dx,dy / ortho / A or AP:";
    return "Next — click / X,Y / @dx,dy / A or AP bearing:";
  }
  if (cmd.active == AppCommandState::Kind::Polyline) {
    using SAP = AppCommandState::SegmentAnglePickPhase;
    if (cmd.polylinePhase == AppCommandState::PolylinePhase::NeedFirstPoint)
      return "POLYLINE first point:";
    if (cmd.polylinePhase == AppCommandState::PolylinePhase::NeedNextPoint && cmd.segmentAngleKeyboardAwaitBearing)
      return "POLYLINE bearing ° (CW from N); blank Enter cancels:";
    if (cmd.polylinePhase == AppCommandState::PolylinePhase::NeedNextPoint && cmd.segmentAnglePickPhase == SAP::WaitP1)
      return "POLYLINE bearing pick — first click:";
    if (cmd.polylinePhase == AppCommandState::PolylinePhase::NeedNextPoint && cmd.segmentAnglePickPhase == SAP::WaitP2)
      return "POLYLINE bearing pick — second click:";
    if (cmd.polylinePhase == AppCommandState::PolylinePhase::NeedNextPoint &&
        cmd.segmentAnglePickPhase == SAP::WaitAdjustOrCommit)
      return "POLYLINE bearing — Enter or +90/-45:";
    if (cmd.polylinePhase == AppCommandState::PolylinePhase::NeedNextPoint && cmd.segmentAngleLockActive)
      return "POLYLINE distance ± / ray click / A clears / CLOSE:";
    if (cmd.orthoMode)
      return "POLYLINE next — ortho / X,Y / A / AP / CLOSE:";
    return "POLYLINE next — X,Y / A / AP / CLOSE:";
  }
  if (cmd.active == AppCommandState::Kind::Arc) {
    switch (cmd.arcPhase) {
    case AppCommandState::ArcPhase::WaitStart:
      return "ARC start:";
    case AppCommandState::ArcPhase::WaitMid:
      return "ARC mid:";
    case AppCommandState::ArcPhase::WaitEnd:
      return "ARC end:";
    }
  }
  if (cmd.active == AppCommandState::Kind::Ellipse) {
    switch (cmd.ellPhase) {
    case AppCommandState::EllipsePhase::WaitCenter:
      return "ELLIPSE center:";
    case AppCommandState::EllipsePhase::WaitMajorEnd:
      return "ELLIPSE axis end:";
    case AppCommandState::EllipsePhase::WaitRatio:
      return "ELLIPSE ratio (cmd line):";
    }
  }
  if (cmd.active == AppCommandState::Kind::Text) {
    switch (cmd.textPhase) {
    case AppCommandState::TextCmdPhase::WaitInsertion:
      return "TEXT insertion X,Y:";
    case AppCommandState::TextCmdPhase::WaitHeight:
      return "TEXT height:";
    case AppCommandState::TextCmdPhase::WaitRotation:
      return "TEXT rotation ° CW from N:";
    case AppCommandState::TextCmdPhase::WaitString:
      return "TEXT content:";
    }
  }
  if (cmd.active == AppCommandState::Kind::Mtext) {
    switch (cmd.mtextPhase) {
    case AppCommandState::MtextPhase::WaitCorner1:
      return "MTEXT corner 1:";
    case AppCommandState::MtextPhase::WaitCorner2:
      return "MTEXT corner 2:";
    case AppCommandState::MtextPhase::WaitString:
      return "MTEXT — edit in drawing box (Ctrl+Enter reformats; Save to place):";
    }
  }
  if (cmd.active == AppCommandState::Kind::DimAligned || cmd.active == AppCommandState::Kind::DimLinear) {
    switch (cmd.dimPhase) {
    case AppCommandState::DimPhase::WaitExt1:
      return cmd.active == AppCommandState::Kind::DimLinear ? "DIMLINEAR ext 1:" : "DIM ext 1:";
    case AppCommandState::DimPhase::WaitExt2:
      return cmd.active == AppCommandState::Kind::DimLinear ? "DIMLINEAR ext 2:" : "DIM ext 2:";
    case AppCommandState::DimPhase::WaitDimLinePt:
      return cmd.active == AppCommandState::Kind::DimLinear ? "DIMLINEAR line (cursor/H/V) or X,Y:"
                                                           : "DIM line pt:";
    }
  }
  if (cmd.active == AppCommandState::Kind::DimAngular) {
    switch (cmd.dimAngularPhase) {
    case AppCommandState::DimAngularPhase::WaitVertex:
      return "DIMANGULAR vertex:";
    case AppCommandState::DimAngularPhase::WaitRay1:
      return "DIMANGULAR ray 1:";
    case AppCommandState::DimAngularPhase::WaitRay2:
      return "DIMANGULAR ray 2:";
    case AppCommandState::DimAngularPhase::WaitArc:
      return "DIMANGULAR arc / radius:";
    }
  }
  if (cmd.active == AppCommandState::Kind::IdPoint)
    return "ID — point (X,Y or click):";
  if (cmd.active == AppCommandState::Kind::SurveyInverse) {
    using SIP = AppCommandState::SurveyInversePhase;
    if (cmd.surveyInversePhase == SIP::WaitFrom)
      return "INVERSE — first point X,Y (Easting, Northing):";
    return "INVERSE — second point X,Y or @ from first:";
  }
  if (cmd.active == AppCommandState::Kind::Circle) {
    using CP = AppCommandState::CirclePhase;
    switch (cmd.circlePhase) {
    case CP::WaitCenterOrMode:
      return "Center or type 3P:";
    case CP::WaitRadius:
      return "Radius, D+diameter, or click:";
    case CP::ThreeP_WaitP1:
      return "3P — point 1:";
    case CP::ThreeP_WaitP2:
      return "3P — point 2:";
    case CP::ThreeP_WaitP3:
      return "3P — point 3:";
    }
  }
  if (cmd.active == AppCommandState::Kind::Move || cmd.active == AppCommandState::Kind::Copy) {
    using MP = AppCommandState::ModifyPhase;
    if (cmd.modifyPhase == MP::PickSelection)
      return "Window opposite corner or cancel:";
    if (cmd.modifyPhase == MP::NeedBase)
      return "Base point X,Y:";
    return "Destination @dx,dy or X,Y:";
  }
  if (cmd.active == AppCommandState::Kind::Scale) {
    using MP = AppCommandState::ModifyPhase;
    using SP = AppCommandState::ScalePhase;
    if (cmd.modifyPhase == MP::PickSelection)
      return "SCALE — window opposite corner or cancel:";
    if (cmd.modifyPhase == MP::NeedBase)
      return "SCALE — base point X,Y:";
    if (cmd.modifyPhase == MP::NeedDestination) {
      switch (cmd.scalePhase) {
      case SP::FactorPick:
        return "SCALE — scale factor, second point from base, or R (reference):";
      case SP::Ref_WaitP1:
        return "SCALE ref — first point X,Y:";
      case SP::Ref_WaitP2:
        return "SCALE ref — second point X,Y:";
      case SP::NewLength_WaitTypedOrP1:
        return "SCALE ref — new length (number) or first point X,Y:";
      case SP::NewLength_WaitP2:
        return "SCALE ref — second point X,Y:";
      default:
        return "SCALE — command input:";
      }
    }
    return "SCALE — command input:";
  }
  if (cmd.active == AppCommandState::Kind::Rotate) {
    using RP = AppCommandState::RotatePhase;
    switch (cmd.rotatePhase) {
    case RP::PickSelection:
      return "Window opposite corner:";
    case RP::NeedBase:
      return "Base point X,Y:";
    case RP::NeedAngleOrReference:
      return "° CW from north / DMS / R / C (copy):";
    case RP::Ref_WaitP1:
    case RP::Ref_WaitP2:
      return "Reference point X,Y (C toggles copy):";
    case RP::AfterReference_WaitAngleOrP:
      return "Bearing ° from north / DMS / P / C (copy):";
    case RP::AnglePoints_WaitP1:
    case RP::AnglePoints_WaitP2:
      return "Angle point X,Y (C toggles copy):";
    }
  }
  if (cmd.active == AppCommandState::Kind::Delete)
    return "DELETE — window opposite corner or ESC:";
  if (cmd.active == AppCommandState::Kind::Join)
    return "JOIN — window opposite corner or ESC:";
  if (cmd.active == AppCommandState::Kind::Trim) {
    using TP = AppCommandState::TrimPhase;
    if (cmd.trimPhase == TP::SelectCuttingEdges)
      return "TRIM — cutting edges, Enter (or L = draw on segment, two clicks):";
    if (cmd.trimPhase == TP::CuttingLine_WaitP1)
      return "TRIM line-trim — first point:";
    if (cmd.trimPhase == TP::CuttingLine_WaitP2)
      return "TRIM line-trim — second point (finishes trim):";
    return "TRIM — click to trim (near end to remove), Enter when done:";
  }
  if (cmd.active == AppCommandState::Kind::Offset) {
    using OP = AppCommandState::OffsetPhase;
    if (cmd.offsetPhase == OP::WaitSelectEntity)
      return "OFFSET — pick object:";
    if (cmd.offsetPhase == OP::WaitDistanceOrThrough)
      return "OFFSET — distance (or through-click):";
    return "OFFSET — pick side:";
  }
  if (cmd.active == AppCommandState::Kind::Zoom)
    return "ZOOM WINDOW — opposite corner or ESC:";
  // Fallback: delegate to footer-hint functions. Handles ALIGN and any future commands
  // that define a footer hint — they automatically appear in the dynamic cursor too.
  {
    const char* h;
    h = AlignCommandFooterHint(cmd);    if (h && h[0]) return h;
    h = LineCommandFooterHint(cmd);     if (h && h[0]) return h;
    h = DrawingExtrasFooterHint(cmd);   if (h && h[0]) return h;
    h = ModifyCommandFooterHint(cmd);   if (h && h[0]) return h;
    h = RotateCommandFooterHint(cmd);   if (h && h[0]) return h;
    h = ScaleCommandFooterHint(cmd);    if (h && h[0]) return h;
    h = DeleteCommandFooterHint(cmd);   if (h && h[0]) return h;
    h = JoinCommandFooterHint(cmd);     if (h && h[0]) return h;
    h = TrimCommandFooterHint(cmd);     if (h && h[0]) return h;
    h = OffsetCommandFooterHint(cmd);   if (h && h[0]) return h;
    h = ZoomCommandFooterHint(cmd);     if (h && h[0]) return h;
  }
  return "Command:";
}

// True when the active prompt expects a coordinate POINT, so the cursor dynamic
// input shows AutoCAD-style live X/Y fields (REQ-024). Mirrors the point phases
// of CommandInputHint; non-point prompts (bearing/angle/distance/factor/option/
// selection) return false and keep a single input field.
static bool CommandExpectsPointEntry(const AppCommandState& cmd) {
  using K = AppCommandState::Kind;
  switch (cmd.active) {
  case K::Line: {
    using LP = AppCommandState::LinePhase;
    using SAP = AppCommandState::SegmentAnglePickPhase;
    if (cmd.linePhase == LP::NeedFirstPoint) return true;
    if (cmd.linePhase == LP::NeedNextPoint)
      return !(cmd.segmentAngleKeyboardAwaitBearing || cmd.segmentAngleLockActive ||
               cmd.segmentAnglePickPhase != SAP::Idle);
    return false;
  }
  case K::Polyline: {
    using PP = AppCommandState::PolylinePhase;
    using SAP = AppCommandState::SegmentAnglePickPhase;
    if (cmd.polylinePhase == PP::NeedFirstPoint) return true;
    if (cmd.polylinePhase == PP::NeedNextPoint)
      return !(cmd.segmentAngleKeyboardAwaitBearing || cmd.segmentAngleLockActive ||
               cmd.segmentAnglePickPhase != SAP::Idle);
    return false;
  }
  case K::Arc: return true;
  case K::Ellipse: {
    using EP = AppCommandState::EllipsePhase;
    return cmd.ellPhase == EP::WaitCenter || cmd.ellPhase == EP::WaitMajorEnd;
  }
  case K::Text:
    return cmd.textPhase == AppCommandState::TextCmdPhase::WaitInsertion;
  case K::Mtext: {
    using MP = AppCommandState::MtextPhase;
    return cmd.mtextPhase == MP::WaitCorner1 || cmd.mtextPhase == MP::WaitCorner2;
  }
  case K::DimAligned:
  case K::DimLinear: return true;
  case K::DimAngular: {
    using DAP = AppCommandState::DimAngularPhase;
    return cmd.dimAngularPhase == DAP::WaitVertex || cmd.dimAngularPhase == DAP::WaitRay1 ||
           cmd.dimAngularPhase == DAP::WaitRay2;
  }
  case K::IdPoint: return true;
  case K::SurveyInverse: return true;
  case K::Circle: {
    using CP = AppCommandState::CirclePhase;
    return cmd.circlePhase == CP::WaitCenterOrMode || cmd.circlePhase == CP::ThreeP_WaitP1 ||
           cmd.circlePhase == CP::ThreeP_WaitP2 || cmd.circlePhase == CP::ThreeP_WaitP3;
  }
  case K::Move:
  case K::Copy: {
    using MP = AppCommandState::ModifyPhase;
    return cmd.modifyPhase == MP::NeedBase || cmd.modifyPhase == MP::NeedDestination;
  }
  case K::Scale: {
    using MP = AppCommandState::ModifyPhase;
    using SP = AppCommandState::ScalePhase;
    if (cmd.modifyPhase == MP::NeedBase) return true;
    if (cmd.modifyPhase == MP::NeedDestination)
      return cmd.scalePhase == SP::Ref_WaitP1 || cmd.scalePhase == SP::Ref_WaitP2 ||
             cmd.scalePhase == SP::NewLength_WaitP2;
    return false;
  }
  case K::Rotate: {
    using RP = AppCommandState::RotatePhase;
    return cmd.rotatePhase == RP::NeedBase || cmd.rotatePhase == RP::Ref_WaitP1 ||
           cmd.rotatePhase == RP::Ref_WaitP2 || cmd.rotatePhase == RP::AnglePoints_WaitP1 ||
           cmd.rotatePhase == RP::AnglePoints_WaitP2;
  }
  case K::Trim: {
    using TP = AppCommandState::TrimPhase;
    return cmd.trimPhase == TP::CuttingLine_WaitP1 || cmd.trimPhase == TP::CuttingLine_WaitP2;
  }
  default:
    return false;
  }
}

// Ordinal word for the point being specified ("first", "second", … then "11th").
static std::string OrdinalWord(int n) {
  static const char* kWords[] = {"zeroth", "first", "second", "third",   "fourth", "fifth",
                                 "sixth",  "seventh", "eighth", "ninth", "tenth"};
  if (n >= 1 && n <= 10)
    return kWords[n];
  const char* suf = "th";
  const int mod100 = n % 100;
  if (mod100 < 11 || mod100 > 13) {
    switch (n % 10) {
    case 1: suf = "st"; break;
    case 2: suf = "nd"; break;
    case 3: suf = "rd"; break;
    default: break;
    }
  }
  return std::to_string(n) + suf;
}

// AutoCAD-style "Specify … :" label for the dynamic-input point prompt (REQ-024).
// Only meaningful when CommandExpectsPointEntry(cmd) is true. Multi-point chains
// (LINE, POLYLINE) count the point being specified: first, second, third, …
static std::string CadPointPromptLabel(const AppCommandState& cmd) {
  using K = AppCommandState::Kind;
  switch (cmd.active) {
  case K::Line:
    return cmd.linePhase == AppCommandState::LinePhase::NeedFirstPoint
               ? std::string("Specify first point:")
               : "Specify " + OrdinalWord(static_cast<int>(cmd.lineDraftSegments) + 2) + " point:";
  case K::Polyline:
    return cmd.polylinePhase == AppCommandState::PolylinePhase::NeedFirstPoint
               ? std::string("Specify first point:")
               : "Specify " + OrdinalWord(static_cast<int>(cmd.polyDraftSegments) + 2) + " point:";
  case K::Arc:
    switch (cmd.arcPhase) {
    case AppCommandState::ArcPhase::WaitStart: return "Specify start point:";
    case AppCommandState::ArcPhase::WaitMid:   return "Specify second point:";
    case AppCommandState::ArcPhase::WaitEnd:   return "Specify end point:";
    }
    return "Specify point:";
  case K::Ellipse:
    return cmd.ellPhase == AppCommandState::EllipsePhase::WaitCenter ? "Specify center point:"
                                                                     : "Specify axis endpoint:";
  case K::Circle:
    switch (cmd.circlePhase) {
    case AppCommandState::CirclePhase::WaitCenterOrMode: return "Specify center point:";
    case AppCommandState::CirclePhase::ThreeP_WaitP1:    return "Specify first point:";
    case AppCommandState::CirclePhase::ThreeP_WaitP2:    return "Specify second point:";
    case AppCommandState::CirclePhase::ThreeP_WaitP3:    return "Specify third point:";
    default:                                             return "Specify point:";
    }
  case K::Text:
    return "Specify start point:";
  case K::Mtext:
    return cmd.mtextPhase == AppCommandState::MtextPhase::WaitCorner1 ? "Specify first corner:"
                                                                      : "Specify opposite corner:";
  case K::DimAligned:
  case K::DimLinear:
    switch (cmd.dimPhase) {
    case AppCommandState::DimPhase::WaitExt1:      return "Specify first extension line origin:";
    case AppCommandState::DimPhase::WaitExt2:      return "Specify second extension line origin:";
    case AppCommandState::DimPhase::WaitDimLinePt: return "Specify dimension line location:";
    }
    return "Specify point:";
  case K::DimAngular:
    switch (cmd.dimAngularPhase) {
    case AppCommandState::DimAngularPhase::WaitVertex: return "Specify vertex:";
    case AppCommandState::DimAngularPhase::WaitRay1:   return "Specify first ray point:";
    case AppCommandState::DimAngularPhase::WaitRay2:   return "Specify second ray point:";
    default:                                          return "Specify point:";
    }
  case K::IdPoint:
    return "Specify point:";
  case K::SurveyInverse:
    return cmd.surveyInversePhase == AppCommandState::SurveyInversePhase::WaitFrom ? "Specify first point:"
                                                                                  : "Specify second point:";
  case K::Move:
  case K::Copy:
    return cmd.modifyPhase == AppCommandState::ModifyPhase::NeedBase ? "Specify base point:"
                                                                     : "Specify second point:";
  case K::Scale:
    return cmd.modifyPhase == AppCommandState::ModifyPhase::NeedBase ? "Specify base point:" : "Specify point:";
  case K::Rotate:
    return cmd.rotatePhase == AppCommandState::RotatePhase::NeedBase ? "Specify base point:" : "Specify point:";
  default:
    return "Specify point:";
  }
}

float CadStatusBarStripHeightPx() {
  constexpr float kPadY = 4.f;
  const ImGuiStyle& st = ImGui::GetStyle();
  const float sep = st.ItemSpacing.y + 1.f + st.ItemSpacing.y;
  return kPadY * 2.f + sep + ImGui::GetFrameHeight();
}

void DrawCadStatusBarStrip(AppCommandState& cmd, double cursorX, double cursorY, float cursorZ,
                           bool* ortho_mode_enabled, bool* grid_visible) {
  ImGuiViewport* vp = ImGui::GetMainViewport();
  const float sh = CadStatusBarStripHeightPx();
  ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y + vp->WorkSize.y - sh));
  ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, sh), ImGuiCond_Always);
  ImGui::SetNextWindowViewport(vp->ID);

  constexpr float kPadX = 8.f;
  constexpr float kPadY = 4.f;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(kPadX, kPadY));
  ImGuiWindowFlags wf = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings |
                        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNavFocus;
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.117f, 0.117f, 0.117f, 1.f));  // #464646 gray band (matches toolbar)
  ImGui::Begin("##CadStatusBarStrip", nullptr, wf);
  ImGui::PopStyleColor();

  ImGui::Separator();
  const float statusBtnH = ImGui::GetFrameHeight();
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.275f, 0.275f, 0.275f, 1.f));  // #464646 gray band (matches toolbar)
  ImGui::BeginChild("StatusBarStrip", ImVec2(0, statusBtnH), false, ImGuiWindowFlags_HorizontalScrollbar);
  ImGui::PopStyleColor();
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(3.f, 0.f));

  // ---- LEFT: hamburger menu + layout tabs ----
  {
    const ImVec2 hbPos = ImGui::GetCursorScreenPos();
    if (ImGui::Button("##statusmenu", ImVec2(statusBtnH, statusBtnH)))
      ImGui::OpenPopup("status_menu");
    ItemHelpTooltip("Layouts & spaces menu");
    ImDrawList* hdl = ImGui::GetWindowDrawList();
    const float hx0 = hbPos.x + statusBtnH * 0.28f, hx1 = hbPos.x + statusBtnH * 0.72f;
    for (int k = 0; k < 3; ++k) {
      const float yy = hbPos.y + statusBtnH * (0.34f + 0.16f * static_cast<float>(k));
      hdl->AddLine(ImVec2(hx0, yy), ImVec2(hx1, yy), IM_COL32(220, 220, 220, 255), 1.6f);
    }
    if (ImGui::BeginPopup("status_menu")) {
      if (ImGui::MenuItem("Model space"))
        SetActiveSpace(cmd, kModelSpaceIndex);
      ImGui::Separator();
      for (int i = 0; i < static_cast<int>(cmd.paperLayouts.size()); ++i)
        if (ImGui::MenuItem(cmd.paperLayouts[static_cast<size_t>(i)].name.c_str(), nullptr,
                            cmd.activeSpaceIndex == i))
          SetActiveSpace(cmd, i);
      ImGui::Separator();
      if (ImGui::MenuItem("New paper layout"))
        SetActiveSpace(cmd, AddPaperLayout(cmd));
      ImGui::EndPopup();
    }
    ImGui::SameLine(0, 6);
  }

  {
    // Paper space layout tabs + sheet picker (REQ-025/026): Model | Layout… | +
    auto spaceTab = [&](const char* label, bool active) {
      PushModeToggleButtonColors(active, cmd.displayColorThemeIdx);
      const bool clicked = ImGui::Button(label, ImVec2(0.f, statusBtnH));
      PopModeToggleButtonColors(active);
      ImGui::SameLine(0, 2);
      return clicked;
    };
    if (spaceTab("Model", cmd.activeSpaceIndex == kModelSpaceIndex))
      SetActiveSpace(cmd, kModelSpaceIndex);

    int pendingDelete = -1;
    for (int i = 0; i < static_cast<int>(cmd.paperLayouts.size()); ++i) {
      ImGui::PushID(i);
      const bool act = cmd.activeSpaceIndex == i;
      PushModeToggleButtonColors(act, cmd.displayColorThemeIdx);
      if (ImGui::Button(cmd.paperLayouts[static_cast<size_t>(i)].name.c_str(), ImVec2(0.f, statusBtnH)))
        SetActiveSpace(cmd, i);
      PopModeToggleButtonColors(act);
      if (ImGui::BeginPopupContextItem("layout_ctx")) {
        ImGui::TextDisabled("Layout");
        ImGui::Separator();
        ImGui::SetNextItemWidth(180.f);
        ImGui::InputText("Rename", &cmd.paperLayouts[static_cast<size_t>(i)].name);
        ImGui::Separator();
        if (ImGui::MenuItem("Move or Copy…")) {
          cmd.pageSetupLayoutIdx = i;
          cmd.moveCopyBeforeSel = i;
          cmd.moveCopyCreateCopy = false;
          cmd.showMoveCopyLayout = true;
          ImGui::CloseCurrentPopup();
        }
        if (ImGui::MenuItem("Page Setup Manager…")) {
          EnsureStandardPageSetup(cmd);
          cmd.pageSetupLayoutIdx = i;
          cmd.pageSetupManagerSel = -1;
          cmd.showPageSetupManager = true;
          ImGui::CloseCurrentPopup();
        }
        if (ImGui::MenuItem("Viewports…")) {
          SetActiveSpace(cmd, i);
          cmd.showViewportsWindow = true;
          ImGui::CloseCurrentPopup();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Delete")) {
          pendingDelete = i;
          ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
      }
      ImGui::PopID();
      ImGui::SameLine(0, 2);
    }
    if (ImGui::Button("+##addlayout", ImVec2(0.f, statusBtnH)))
      SetActiveSpace(cmd, AddPaperLayout(cmd));
    if (pendingDelete >= 0)
      DeletePaperLayout(cmd, pendingDelete);

    // Paper size / orientation / plot settings now live in the per-layout Page Setup Manager
    // (right-click a layout tab). Viewports moved to the Viewports… window.
    ImGui::SameLine(0, 6);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("|");
    ImGui::SameLine(0, 6);
  }

  // Coordinate readout (left side, after the layout tabs).
  {
    const int p = cmd.displayLinearPrecision;
    ImGui::AlignTextToFramePadding();
    ImGui::Text("X %s  Y %s  Z %s  |  UCS: World", FormatLinear(cursorX, p).c_str(),
                FormatLinear(cursorY, p).c_str(), FormatLinear(cursorZ, p).c_str());
  }

  // ---- RIGHT (right-aligned): space toggle + mode tools ----
  {
    const ImGuiStyle& sty = ImGui::GetStyle();
    auto bw = [&](const char* t) { return ImGui::CalcTextSize(t).x + sty.FramePadding.x * 2.f; };
    const char* spaceLbl = InFloatingModelSpace(cmd)
                               ? "FLOAT"
                               : (cmd.activeSpaceIndex != kModelSpaceIndex ? "PAPER" : "MODEL");
    constexpr float sp = 4.f;
    const float rightW = bw(spaceLbl) + bw("VPLOCK") + bw("OSNAP") + bw("ORTHO") + bw("GRID") + bw("POLAR") +
                         140.f /*plot scale combo*/ + bw("SEL") + sp * 8.f + 24.f;
    ImGui::SameLine(0, 8);
    const float rx = ImGui::GetWindowContentRegionMax().x - rightW;
    if (rx > ImGui::GetCursorPosX())
      ImGui::SetCursorPosX(rx);

    // Space toggle (MODEL / PAPER / FLOAT) — REQ-025/036.
    if (InFloatingModelSpace(cmd)) {
      PushModeToggleButtonColors(true, cmd.displayColorThemeIdx);
      if (ImGui::Button("FLOAT", ImVec2(0.f, statusBtnH))) {
        std::vector<std::string> sbLog;
        ExitFloatingModelSpace(cmd, sbLog);
      }
      PopModeToggleButtonColors(true);
      ItemHelpTooltip("Floating model space — editing the model through a viewport. Click (or Esc) to return.");
    } else {
      const bool paper = cmd.activeSpaceIndex != kModelSpaceIndex;
      PushModeToggleButtonColors(paper, cmd.displayColorThemeIdx);
      if (ImGui::Button(paper ? "PAPER" : "MODEL", ImVec2(0.f, statusBtnH)))
        ToggleModelPaperSpace(cmd);
      PopModeToggleButtonColors(paper);
      ItemHelpTooltip("Toggle Model / current Paper layout. Double-click a viewport to edit the model through it.");
    }
    ImGui::SameLine(0, sp);

    // VPLOCK — viewport zoom lock (user request).
    {
      const bool on = cmd.viewportZoomLocked;
      PushModeToggleButtonColors(on, cmd.displayColorThemeIdx);
      if (ImGui::Button("VPLOCK", ImVec2(0.f, statusBtnH)))
        cmd.viewportZoomLocked = !cmd.viewportZoomLocked;
      PopModeToggleButtonColors(on);
      ItemHelpTooltip("Viewport zoom lock: ON = pan/zoom always moves the sheet; OFF = while editing a viewport "
                      "in place, pan/zoom adjusts that viewport's model framing.");
      ImGui::SameLine(0, sp);
    }

    // OSNAP (+ snap-type popup).
    {
      const bool on = cmd.objectSnapEnabled;
      PushModeToggleButtonColors(on, cmd.displayColorThemeIdx);
      if (ImGui::Button("OSNAP", ImVec2(0.f, statusBtnH)))
        cmd.objectSnapEnabled = !cmd.objectSnapEnabled;
      PopModeToggleButtonColors(on);
      ItemHelpTooltip("Object snap — F3 toggles; right-click for snap types.");
      if (ImGui::BeginPopupContextItem("osnap_modes", ImGuiPopupFlags_MouseButtonRight)) {
        ImGui::TextDisabled("Snap to");
        ImGui::Separator();
        ImGui::Checkbox("Endpoint", &cmd.objectSnapEndpoint);
        ImGui::Checkbox("Midpoint", &cmd.objectSnapMidpoint);
        ImGui::Checkbox("Center", &cmd.objectSnapCenter);
        ImGui::Checkbox("Perpendicular", &cmd.objectSnapPerpendicular);
        ImGui::Checkbox("Survey point", &cmd.objectSnapSurveyPoint);
        ImGui::Checkbox("Geometric center (closed polyline)", &cmd.objectSnapGeometricCenter);
        ImGui::EndPopup();
      }
      ImGui::SameLine(0, sp);
    }
    if (ortho_mode_enabled) {
      const bool on = *ortho_mode_enabled;
      PushModeToggleButtonColors(on, cmd.displayColorThemeIdx);
      if (ImGui::Button("ORTHO", ImVec2(0.f, statusBtnH)))
        *ortho_mode_enabled = !*ortho_mode_enabled;
      PopModeToggleButtonColors(on);
      ItemHelpTooltip("Ortho mode — constrain to horizontal / vertical (F8)");
      ImGui::SameLine(0, sp);
    }
    if (grid_visible) {
      const bool on = *grid_visible;
      PushModeToggleButtonColors(on, cmd.displayColorThemeIdx);
      if (ImGui::Button("GRID", ImVec2(0.f, statusBtnH)))
        *grid_visible = !*grid_visible;
      PopModeToggleButtonColors(on);
      ItemHelpTooltip("Drawing grid");
      ImGui::SameLine(0, sp);
    }
    {
      const bool on = gPolarTrackingEnabled;
      PushModeToggleButtonColors(on, cmd.displayColorThemeIdx);
      if (ImGui::Button("POLAR", ImVec2(0.f, statusBtnH)))
        gPolarTrackingEnabled = !gPolarTrackingEnabled;
      PopModeToggleButtonColors(on);
      ItemHelpTooltip("Polar tracking (UI only for now)");
      ImGui::SameLine(0, sp);
    }
    DrawPlotScaleCombo(cmd);
    ImGui::SameLine(0, sp);
    {
      const bool on = cmd.showSelectionCyclingWindow;
      PushModeToggleButtonColors(on, cmd.displayColorThemeIdx);
      if (ImGui::Button("SEL", ImVec2(0.f, statusBtnH))) {
        if (!cmd.showSelectionCyclingWindow) {
          cmd.selectionCycleEntities      = cmd.selection;
          cmd.selectionCycleSurveyPoints  = cmd.selectedSurveyPointIndices;
          cmd.showSelectionCyclingWindow  = true;
        } else {
          cmd.showSelectionCyclingWindow = false;
        }
      }
      PopModeToggleButtonColors(on);
      ItemHelpTooltip("Selection panel — lists selected entities so you can toggle each one on or off.");
    }
  }

  ImGui::PopStyleVar();
  ImGui::EndChild();
  ImGui::PopStyleVar();

  ImGui::End();
  ImGui::PopStyleVar();
}

// Last drawing-crosshair screen position, captured while the viewport is hovered.
// Persists when focus moves to the command input so the autocomplete popup can
// anchor at the crosshair (AutoCAD dynamic-input style) rather than the command bar.
// (-1,-1) means "not yet known" → popup falls back to the command-input anchor.
static ImVec2 s_lastCrosshairScreen = ImVec2(-1.f, -1.f);

// Screen rect of the command-autocomplete popup while it is open, so the viewport
// click handler can ignore a left-click that lands on the suggestion list (REQ-024).
static bool   s_cmdSugPopupOpen = false;
static ImVec2 s_cmdSugPopupMin = ImVec2(0.f, 0.f);
static ImVec2 s_cmdSugPopupMax = ImVec2(0.f, 0.f);

void DrawCommandLinePanel(std::vector<std::string>& log, char* cmdBuf, int cmdBufSize, AppCommandState& cmd) {
  const bool isDark = (cmd.displayColorThemeIdx == 0);
  // Console background is slightly distinct from the main workspace in both themes.
  const ImVec4 consoleBg = isDark
      ? ImVec4(0.067f, 0.078f, 0.094f, 1.f)   // #111418 dark console
      : ImVec4(0.235f, 0.235f, 0.235f, 1.f);  // #3C3C3C console panel (recessed vs #464646 band)
  const ImVec4 promptColor = isDark
      ? ImVec4(0.133f, 0.773f, 0.369f, 1.f)   // #22C55E bright green on dark
      : ImVec4(0.180f, 0.720f, 0.400f, 1.f);  // #2EB766 bright green on dark console
  ImGui::SetNextWindowSize(ImVec2(900, 220), ImGuiCond_FirstUseEver);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, consoleBg);
  if (!ImGui::Begin("Command line", nullptr)) {
    ImGui::End();
    ImGui::PopStyleColor();
    return;
  }

  const char* circFooter   = CircleCommandFooterHint(cmd);
  const char* lineFooter   = LineCommandFooterHint(cmd);
  const char* modFooter    = ModifyCommandFooterHint(cmd);
  const char* scaleFooter  = ScaleCommandFooterHint(cmd);
  const char* rotFooter    = RotateCommandFooterHint(cmd);
  const char* delFooter    = DeleteCommandFooterHint(cmd);
  const char* joinFooter   = JoinCommandFooterHint(cmd);
  const char* trimFooter   = TrimCommandFooterHint(cmd);
  const char* offsetFooter = OffsetCommandFooterHint(cmd);
  const char* alignFooter  = AlignCommandFooterHint(cmd);
  const char* zmFooter     = ZoomCommandFooterHint(cmd);
  const char* drawXFooter  = DrawingExtrasFooterHint(cmd);

  const ImGuiStyle& st = ImGui::GetStyle();
  const float wrapW = ImGui::GetContentRegionAvail().x;
  const bool cmdInputOnViewport =
      cmd.active != AppCommandState::Kind::None && cmd.viewportDrawingHovered && !cmd.mtextRichEditorOpen;

  auto footerNonEmpty = [](const char* s) { return s && s[0] != '\0'; };
  auto wrappedBlockH = [&](const char* s) -> float {
    if (!footerNonEmpty(s))
      return 0.f;
    return ImGui::CalcTextSize(s, nullptr, false, wrapW).y + st.ItemSpacing.y;
  };

  // Exact footer height (separator + input + wrapped hints). Avoids a tall empty band when the dock is tall
  // but only one short hint is visible (old line-budget heuristic summed +2 lines per hint category).
  const float sepBeforeInput = st.ItemSpacing.y + 1.f;
  float footerH = sepBeforeInput;
  if (cmdInputOnViewport) {
    footerH += ImGui::CalcTextSize("Command input follows the cursor on the drawing (viewport).", nullptr, false, wrapW).y;
  } else {
    footerH += ImGui::GetFrameHeight();
  }
  footerH += st.ItemSpacing.y;
  footerH += wrappedBlockH(circFooter);
  footerH += wrappedBlockH(lineFooter);
  footerH += wrappedBlockH(modFooter);
  footerH += wrappedBlockH(scaleFooter);
  footerH += wrappedBlockH(rotFooter);
  footerH += wrappedBlockH(delFooter);
  footerH += wrappedBlockH(joinFooter);
  footerH += wrappedBlockH(trimFooter);
  footerH += wrappedBlockH(offsetFooter);
  footerH += wrappedBlockH(alignFooter);
  footerH += wrappedBlockH(zmFooter);
  footerH += wrappedBlockH(drawXFooter);
  footerH += 1.f;

  const float sendBtnW = ImGui::CalcTextSize("Send").x + st.FramePadding.x * 2.f + 8.f;
  const float availY = ImGui::GetContentRegionAvail().y;
  const float scrollH = std::max(40.f, availY - footerH);

  {
    // Build a contiguous UTF-8 buffer of the log for TextUnformatted (faster than per-line Text() calls, and the
    // clipboard copy uses the same buffer). Also drives the AutoCAD-style "Copy log" button.
    size_t neededBytes = 1;
    for (const auto& line : log)
      neededBytes += line.size() + 1;
    if (cmd.commandLogCacheBytes.size() < neededBytes)
      cmd.commandLogCacheBytes.assign(neededBytes + 64, '\0');
    size_t pos = 0;
    for (size_t li = 0; li < log.size(); ++li) {
      const std::string& line = log[li];
      std::memcpy(cmd.commandLogCacheBytes.data() + pos, line.data(), line.size());
      pos += line.size();
      if (li + 1 < log.size())
        cmd.commandLogCacheBytes[pos++] = '\n';
    }
    cmd.commandLogCacheBytes[pos] = '\0';

    // Header strip above the log: "Copy log" button (for debug paste) and "Clear scroll lock" if user has scrolled up.
    const float copyBtnW = ImGui::CalcTextSize("Copy log").x + ImGui::GetStyle().FramePadding.x * 2.f + 8.f;
    if (ImGui::Button("Copy log", ImVec2(copyBtnW, 0.f)))
      ImGui::SetClipboardText(cmd.commandLogCacheBytes.data());
    ItemHelpTooltip("Copies the entire command log to the clipboard.");
    ImGui::SameLine();
    ImGui::TextDisabled("(%zu line%s)", log.size(), log.size() == 1 ? "" : "s");

    // Canonical ImGui scrolling-log pattern: child window holds TextUnformatted, then SetScrollHereY(1) AT THE END
    // of the child (after content is laid out so ScrollMaxY is current). InputTextMultiline can't autoscroll
    // reliably in read-only mode because it never owns an active caret to scroll-into-view.
    //
    // Selection note: TextUnformatted doesn't support drag-selection; for debug paste use the "Copy log" button
    // above (one click → whole log on clipboard). Right-click anywhere in the child also exposes the same action.
    const float headerH = ImGui::GetFrameHeightWithSpacing();
    const float logChildH = std::max(40.f, scrollH - headerH);
    ImGui::BeginChild("##CmdLogChild", ImVec2(0.f, logChildH), true, ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x);
    ImGui::TextUnformatted(cmd.commandLogCacheBytes.data());
    ImGui::PopTextWrapPos();
    if (ImGui::BeginPopupContextWindow("##cmdLogCtx")) {
      if (ImGui::MenuItem("Copy log to clipboard"))
        ImGui::SetClipboardText(cmd.commandLogCacheBytes.data());
      ImGui::EndPopup();
    }
    const bool logGrew = (log.size() != cmd.commandLogLastSizeForAutoscroll);
    if (logGrew) {
      cmd.commandLogLastSizeForAutoscroll = log.size();
      ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
  }

  ImGui::Separator();
  ImGui::PushID("GoSurveyCmdPanel");

  ImGuiIO& io = ImGui::GetIO();
  if (!io.WantTextInput && io.InputQueueCharacters.Size > 0 && !cmdInputOnViewport) {
    RouteQueuedCharsToCmdBuf(cmdBuf, cmdBufSize, io);
    ImGui::SetKeyboardFocusHere(0);
  }

  const float inputAvailW = ImGui::GetContentRegionAvail().x;

  // nanoCAD-style command autocomplete. State persists; popup drawn after End().
  static int  s_cmdSel = 0;
  static bool s_cmdDismissed = false;
  static std::string s_cmdLastQuery;
  // Highlighted suggestion persisted across the Enter frame: a single-line InputText with EnterReturnsTrue
  // deactivates itself when Enter is pressed, so on that frame the list isn't rebuilt (inputActive is false).
  // We capture the highlight while the list is open and consume it on submit.
  static bool s_cmdSugVisible = false;
  static std::string s_cmdHighlight;
  static bool s_cmdScrollToSel = false;  // request: scroll the keyboard-selected row into view
  // Suggestions persisted from the frame they were built. Clicking a row deactivates the command
  // InputText (focus moves to the popup), so on the click frame inputActive is false and the list
  // would otherwise rebuild empty — taking the popup (and its row buttons) down before the click
  // resolves. We keep the list alive from this cache while the mouse is over the popup.
  static std::vector<CommandSuggestion> s_cmdSugCache;
  std::vector<CommandSuggestion> cmdSug;
  ImVec2 cmdInputMin(0, 0), cmdInputMax(0, 0);
  bool   cmdShowSug = false;

  if (!cmdInputOnViewport) {
    ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue |
                                ImGuiInputTextFlags_CallbackAlways |
                                ImGuiInputTextFlags_CallbackCompletion |  // Tab completes to highlighted
                                ImGuiInputTextFlags_EscapeClearsAll;      // Esc clears the buffer (unfreezes crosshair)
    ImGui::PushStyleColor(ImGuiCol_Text, promptColor);
    ImGui::TextUnformatted(">");
    ImGui::PopStyleColor();
    const float promptW = ImGui::GetItemRectSize().x + st.ItemSpacing.x * 0.5f;
    ImGui::SameLine(0, st.ItemSpacing.x * 0.5f);
    ImGui::SetNextItemWidth(std::max(64.f, inputAvailW - sendBtnW - st.ItemSpacing.x - promptW));
    bool exec = ImGui::InputTextWithHint("##CommandLineInput", CommandInputHint(cmd), cmdBuf,
                                         static_cast<size_t>(cmdBufSize), flags, CommandLineInputCallback, nullptr);
    cmdInputMin = ImGui::GetItemRectMin();
    cmdInputMax = ImGui::GetItemRectMax();
    const bool inputActive = ImGui::IsItemActive();
    ImGui::SetItemDefaultFocus();

    std::string query(cmdBuf);
    while (!query.empty() && std::isspace(static_cast<unsigned char>(query.front()))) query.erase(query.begin());
    while (!query.empty() && std::isspace(static_cast<unsigned char>(query.back())))  query.pop_back();
    if (query != s_cmdLastQuery) {
      s_cmdLastQuery = query; s_cmdSel = 0; s_cmdDismissed = false;
      s_cmdSugVisible = false; s_cmdHighlight.clear();
    }

    const bool singleToken = query.find_first_of(" \t") == std::string::npos;
    // Mouse is over last frame's popup rect — a row is being hovered/clicked.
    const bool overCmdSugPopup = s_cmdSugPopupOpen &&
        ImGui::IsMouseHoveringRect(s_cmdSugPopupMin, s_cmdSugPopupMax, false);
    if (inputActive && !query.empty() && singleToken && !s_cmdDismissed) {
      cmdSug = FuzzyCommandSuggestions(query, 20);
      s_cmdSugCache = cmdSug;
    } else if (overCmdSugPopup && !s_cmdDismissed && !s_cmdSugCache.empty()) {
      // Input lost focus to a click on the popup; keep the cached list alive this frame so the
      // row's InvisibleButton (which fires on mouse release) can run the command.
      cmdSug = s_cmdSugCache;
    }

    if (!cmdSug.empty()) {
      const int n = static_cast<int>(cmdSug.size());
      s_cmdSel = std::clamp(s_cmdSel, 0, n - 1);
      // Claim the arrow keys for the (single-line) command input while the suggestion list is open.
      // ImGuiConfigFlags_NavEnableKeyboard is on globally, and a single-line InputText doesn't consume
      // Up/Down, so keyboard-nav would otherwise steal them: Up moves focus off the field (closing the
      // list) and Down jumps focus to the Send button (so Enter runs the typed text, not the highlight).
      ImGui::SetItemKeyOwner(ImGuiKey_UpArrow);
      ImGui::SetItemKeyOwner(ImGuiKey_DownArrow);
      if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true)) { s_cmdSel = (s_cmdSel + 1) % n; s_cmdScrollToSel = true; }
      if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true))   { s_cmdSel = (s_cmdSel - 1 + n) % n; s_cmdScrollToSel = true; }
      g_cmdSuggestComplete = cmdSug[s_cmdSel].name;
      for (char& ch : g_cmdSuggestComplete) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
      cmdShowSug = true;
      s_cmdSugVisible = true;
      s_cmdHighlight = g_cmdSuggestComplete;
    } else {
      g_cmdSuggestComplete.clear();
      // Clear the persisted highlight only when the user is actively in the field with no list (e.g. a
      // full/multi-token command). On the Enter frame the input is already inactive, so the highlight
      // survives to be consumed by the submit branch below.
      if (inputActive) { s_cmdSugVisible = false; s_cmdHighlight.clear(); }
    }

    ImGui::SameLine(0, st.ItemSpacing.x);
    //const bool sendClicked = ImGui::Button("Send", ImVec2(sendBtnW, 0.f));
    if (exec) {
      // Enter with the list open runs the highlighted command. The list state is read from the persisted
      // s_cmd* values because Enter deactivates the input, so cmdShowSug/cmdSug are already empty this frame.
      if (exec && s_cmdSugVisible && !s_cmdHighlight.empty())
        std::snprintf(cmdBuf, static_cast<size_t>(cmdBufSize), "%s", s_cmdHighlight.c_str());
      s_cmdDismissed = true;
      s_cmdLastQuery.clear();
      cmdShowSug = false;
      s_cmdSugVisible = false;
      s_cmdHighlight.clear();
      ProcessCommandLineSubmit(cmdBuf, cmdBufSize, cmd, log);
    }
  } else {
    ImGui::TextDisabled("Command input follows the cursor on the drawing (viewport).");
  }

  auto renderHint = [](const char* s) {
    if (!s || !s[0]) return;
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", s);
    ImGui::PopStyleColor();
  };

  renderHint(circFooter);
  renderHint(lineFooter);
  renderHint(modFooter);
  renderHint(scaleFooter);
  renderHint(rotFooter);
  renderHint(delFooter);
  renderHint(joinFooter);
  renderHint(trimFooter);
  renderHint(offsetFooter);
  renderHint(alignFooter);
  renderHint(zmFooter);
  renderHint(drawXFooter);

  ImGui::PopID();

  ImGui::End();
  ImGui::PopStyleColor();

  // --- nanoCAD-style command autocomplete popup (anchored at the drawing crosshair) ---
  s_cmdSugPopupOpen = false;
  if (cmdShowSug && !cmdSug.empty()) {
    const float rowH  = ImGui::GetTextLineHeight() + 7.f;
    const float padY  = 3.f;
    // Cap the visible height; if there are more suggestions than fit, the popup scrolls.
    const int   kMaxRows = 8;
    const int   nSug     = static_cast<int>(cmdSug.size());
    const int   visRows  = std::min(nSug, kMaxRows);
    const bool  scrolls  = nSug > visRows;
    const float listH = padY * 2.f + rowH * static_cast<float>(visRows);
    // Size the popup snugly to its content (name + description), not the command
    // input width — keeps it compact at the cursor.
    const float gutter = 14.f;  // arrow gutter (mirrors the row layout below)
    float contentW = 0.f;
    for (const CommandSuggestion& s : cmdSug) {
      float w = ImGui::CalcTextSize(s.name.c_str()).x;
      if (!s.description.empty())
        w += ImGui::CalcTextSize(("  (" + s.description + ")").c_str()).x;
      contentW = std::max(contentW, w);
    }
    float listW = std::clamp(gutter + rowH + contentW + 12.f, 150.f, 460.f);
    if (scrolls) listW += ImGui::GetStyle().ScrollbarSize;  // keep text clear of the scrollbar
    // Pop up at the crosshair (AutoCAD dynamic-input style); fall back to above the
    // command input if the crosshair position isn't known yet.
    ImVec2 pos;
    if (s_lastCrosshairScreen.x >= 0.f) {
      const float offX = 16.f, offY = 18.f;  // clear the crosshair pickbox
      pos = ImVec2(s_lastCrosshairScreen.x + offX, s_lastCrosshairScreen.y + offY);
      const ImGuiViewport* vp = ImGui::GetMainViewport();
      const ImVec2 wmax(vp->WorkPos.x + vp->WorkSize.x, vp->WorkPos.y + vp->WorkSize.y);
      if (pos.y + listH > wmax.y) pos.y = s_lastCrosshairScreen.y - offY - listH;  // flip above
      if (pos.x + listW > wmax.x) pos.x = wmax.x - listW;
      pos.x = std::max(pos.x, vp->WorkPos.x);
      pos.y = std::max(pos.y, vp->WorkPos.y);
    } else {
      pos = ImVec2(cmdInputMin.x, cmdInputMin.y - listH - 3.f);
    }
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(ImVec2(listW, listH));
    s_cmdSugPopupOpen = true;
    s_cmdSugPopupMin = pos;
    s_cmdSugPopupMax = ImVec2(pos.x + listW, pos.y + listH);
    const ImGuiWindowFlags pf = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(1.f, padY));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.275f, 0.275f, 0.275f, 1.f));  // #464646 list (matches panels)
    ImGui::PushStyleColor(ImGuiCol_Border,  ImVec4(0.45f, 0.45f, 0.45f, 1.f));
    if (ImGui::Begin("##CmdSuggestPopup", nullptr, pf)) {
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const float rowW = ImGui::GetContentRegionAvail().x;
      for (int i = 0; i < static_cast<int>(cmdSug.size()); ++i) {
        ImGui::PushID(i);
        const ImVec2 rmin = ImGui::GetCursorScreenPos();
        // gutter declared above (popup-width calc): left gutter for the selection arrow marker

        if (ImGui::InvisibleButton("row", ImVec2(rowW, rowH))) {
          std::string pick = cmdSug[i].name;
          for (char& ch : pick) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
          std::snprintf(cmdBuf, static_cast<size_t>(cmdBufSize), "%s", pick.c_str());
          s_cmdDismissed = true; s_cmdLastQuery.clear(); s_cmdSugCache.clear();
          ProcessCommandLineSubmit(cmdBuf, cmdBufSize, cmd, log);
        }
        if (ImGui::IsItemHovered()) s_cmdSel = i;

        // Keep the keyboard-selected row visible when the list scrolls.
        if (i == s_cmdSel && s_cmdScrollToSel) ImGui::SetScrollHereY(0.5f);

        // Selected row gets a steel-blue highlight bar + a right-pointing arrow marker (nanoCAD style).
        if (i == s_cmdSel) {
          dl->AddRectFilled(rmin, ImVec2(rmin.x + rowW, rmin.y + rowH), IM_COL32(60, 92, 134, 255));
          const float cy = rmin.y + rowH * 0.5f;
          const float ax = rmin.x + 4.f;
          dl->AddTriangleFilled(ImVec2(ax, cy - 4.5f), ImVec2(ax, cy + 4.5f), ImVec2(ax + 6.f, cy),
                                IM_COL32(150, 190, 240, 255));
        }

        // Icon (if the command has one), then NAME and (description), after the arrow gutter.
        float textX = rmin.x + gutter + 2.f;
        RibbonIconKind ik{};
        if (CommandIconKind(cmdSug[i].name, &ik)) {
          const ImTextureID tex = g_ribbonIconTex[static_cast<int>(ik)];
          if (tex) {
            const float isz = rowH - 5.f;
            dl->AddImage(tex, ImVec2(rmin.x + gutter, rmin.y + 2.5f),
                         ImVec2(rmin.x + gutter + isz, rmin.y + 2.5f + isz));
          }
          textX = rmin.x + gutter + rowH - 2.f;
        }
        const float ty = rmin.y + (rowH - ImGui::GetTextLineHeight()) * 0.5f;
        dl->AddText(ImVec2(textX, ty), IM_COL32(229, 231, 235, 255), cmdSug[i].name.c_str());
        const float nameW = ImGui::CalcTextSize(cmdSug[i].name.c_str()).x;
        if (!cmdSug[i].description.empty()) {
          const std::string d = "  (" + cmdSug[i].description + ")";
          dl->AddText(ImVec2(textX + nameW, ty), IM_COL32(160, 160, 160, 255), d.c_str());
        }
        ImGui::PopID();
      }
      s_cmdScrollToSel = false;  // request consumed this frame
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
  }
}

static void RotateImDrawListVertsXY(ImDrawList* dl, int vtxStart, int vtxEnd, const ImVec2& pivot, float cosA,
                                    float sinA) {
  if (!dl || vtxStart >= vtxEnd)
    return;
  ImDrawVert* vbuf = dl->VtxBuffer.Data;
  for (int i = vtxStart; i < vtxEnd; ++i) {
    ImDrawVert* v = &vbuf[i];
    const float dx = v->pos.x - pivot.x;
    const float dy = v->pos.y - pivot.y;
    v->pos.x = pivot.x + dx * cosA - dy * sinA;
    v->pos.y = pivot.y + dx * sinA + dy * cosA;
  }
}

static void AddAlignedDimText(ImDrawList* dl, ImFont* font, float fontPx, const ImVec2& pivotSp, float screenAngRad,
                              ImU32 textCol, const char* text) {
  if (!dl || !text || !text[0])
    return;
  const float fs = ImGui::GetFontSize();
  const float scale = fontPx / std::max(fs, 1.e-6f);
  const ImVec2 ts = ImGui::CalcTextSize(text);
  const float tw = ts.x * scale;
  const float th = ts.y * scale;
  const int v0 = dl->VtxBuffer.Size;
  dl->AddText(font, fontPx, ImVec2(pivotSp.x - tw * 0.5f, pivotSp.y - th * 0.5f), textCol, text);
  const int v1 = dl->VtxBuffer.Size;
  RotateImDrawListVertsXY(dl, v0, v1, pivotSp, std::cos(screenAngRad), std::sin(screenAngRad));
}

static int HitTestDimGrip(float mouseSx, float mouseSy, ImVec2 imgPos, ImVec2 avail, float worldLeft, float worldRight,
                          float worldBottom, float worldTop, const CadAnnotation& ann, float gripRadiusPx) {
  if (ann.kind != CadAnnotation::Kind::DimAligned && ann.kind != CadAnnotation::Kind::DimLinear)
    return -1;
  float sx1 = 0.f, sy1 = 0.f, sx2 = 0.f, sy2 = 0.f, tx = 0.f, ty = 0.f, nx = 0.f, ny = 0.f, meas = 0.f;
  if (!CadDimAnyGeometry(ann, &sx1, &sy1, &sx2, &sy2, &tx, &ty, &nx, &ny, &meas))
    return -1;
  const float wx[5] = {ann.dimExt1X, ann.dimExt2X, sx1, sx2, ann.insX};
  const float wy[5] = {ann.dimExt1Y, ann.dimExt2Y, sy1, sy2, ann.insY};
  const float denx = worldRight - worldLeft + 1e-12f;
  const float deny = worldTop - worldBottom + 1e-12f;
  const float r2 = gripRadiusPx * gripRadiusPx;
  for (int i = 4; i >= 0; --i) {
    const float u = (wx[i] - worldLeft) / denx;
    const float v = (worldTop - wy[i]) / deny;
    const float sx = imgPos.x + u * avail.x;
    const float sy = imgPos.y + v * avail.y;
    const float dx = mouseSx - sx;
    const float dy = mouseSy - sy;
    if (dx * dx + dy * dy <= r2)
      return i;
  }
  return -1;
}

static int HitTestMtextGrip(float mouseSx, float mouseSy, ImVec2 imgPos, ImVec2 avail, float worldLeft,
                            float worldRight, float worldBottom, float worldTop, const CadAnnotation& ann,
                            float gripRadiusPx) {
  if (ann.kind != CadAnnotation::Kind::Mtext)
    return -1;
  const float denx = worldRight - worldLeft + 1e-12f;
  const float deny = worldTop - worldBottom + 1e-12f;
  const float r2 = gripRadiusPx * gripRadiusPx;
  if (ann.surveyPointLabelFor >= 0) {
    const float cx = 0.5f * (ann.boxMinX + ann.boxMaxX);
    const float cy = 0.5f * (ann.boxMinY + ann.boxMaxY);
    const float u = (cx - worldLeft) / denx;
    const float v = (worldTop - cy) / deny;
    const float sx = imgPos.x + u * avail.x;
    const float sy = imgPos.y + v * avail.y;
    const float dx = mouseSx - sx;
    const float dy = mouseSy - sy;
    if (dx * dx + dy * dy <= r2)
      return 4;
    return -1;
  }
  const float wx[4] = {ann.boxMinX, ann.boxMaxX, ann.boxMaxX, ann.boxMinX};
  const float wy[4] = {ann.boxMinY, ann.boxMinY, ann.boxMaxY, ann.boxMaxY};
  for (int i = 0; i < 4; ++i) {
    const float u = (wx[i] - worldLeft) / denx;
    const float v = (worldTop - wy[i]) / deny;
    const float sx = imgPos.x + u * avail.x;
    const float sy = imgPos.y + v * avail.y;
    const float dx = mouseSx - sx;
    const float dy = mouseSy - sy;
    if (dx * dx + dy * dy <= r2)
      return i;
  }
  return -1;
}

static void DrawMtextRichEditorOverlay(AppCommandState& cmd, std::vector<std::string>& log, float worldLeft,
                                       float worldRight, float worldBottom, float worldTop, ImVec2 imgPos,
                                       ImVec2 avail) {
  if (!cmd.mtextRichEditorOpen)
    return;
  using AK = AppCommandState::Kind;
  using AMP = AppCommandState::MtextPhase;
  float bx0 = 0.f, bx1 = 0.f, by0 = 0.f, by1 = 0.f;
  if (cmd.mtextRichEditorPlacement) {
    if (cmd.active != AK::Mtext || cmd.mtextPhase != AMP::WaitString)
      return;
    bx0 = std::min(cmd.mtxtX1, cmd.mtxtX2);
    bx1 = std::max(cmd.mtxtX1, cmd.mtxtX2);
    by0 = std::min(cmd.mtxtY1, cmd.mtxtY2);
    by1 = std::max(cmd.mtxtY1, cmd.mtxtY2);
  } else {
    if (cmd.mtextRichEditorAnnIndex < 0 ||
        static_cast<size_t>(cmd.mtextRichEditorAnnIndex) >= cmd.cadAnnotations.size())
      return;
    const CadAnnotation& a = cmd.cadAnnotations[static_cast<size_t>(cmd.mtextRichEditorAnnIndex)];
    if (a.kind != CadAnnotation::Kind::Mtext)
      return;
    bx0 = a.boxMinX;
    bx1 = a.boxMaxX;
    by0 = a.boxMinY;
    by1 = a.boxMaxY;
  }

  const float denx = worldRight - worldLeft + 1e-12f;
  const float deny = worldTop - worldBottom + 1e-12f;
  auto ws = [&](float wx, float wy, ImVec2* o) {
    const float u = (wx - worldLeft) / denx;
    const float v = (worldTop - wy) / deny;
    o->x = imgPos.x + u * avail.x;
    o->y = imgPos.y + v * avail.y;
  };
  ImVec2 p00{}, p01{}, p10{}, p11{};
  ws(bx0, by0, &p00);
  ws(bx1, by0, &p01);
  ws(bx0, by1, &p10);
  ws(bx1, by1, &p11);
  const float sx0 = std::min({p00.x, p01.x, p10.x, p11.x});
  const float sx1 = std::max({p00.x, p01.x, p10.x, p11.x});
  const float sy0 = std::min({p00.y, p01.y, p10.y, p11.y});

  const ImVec2 imgMin(imgPos.x, imgPos.y);
  const ImVec2 imgMax(imgPos.x + avail.x, imgPos.y + avail.y);
  constexpr float kPad = 3.f;
  const float boxScreenW = std::max(140.f, sx1 - sx0 - 2.f * kPad);
  float w = std::max(300.f, boxScreenW);
  w = std::min(w, imgMax.x - imgMin.x - 4.f);

  const ImGuiStyle& ist = ImGui::GetStyle();
  const float innerW = std::max(24.f, w - ist.WindowPadding.x * 2.f);
  int editorLineCount = 1;
  for (unsigned char ch : cmd.mtextRichEditorBuf) {
    if (ch == '\n')
      ++editorLineCount;
  }
  const float lh = ImGui::GetTextLineHeightWithSpacing();
  const float editorH =
      std::clamp(static_cast<float>(editorLineCount) * lh + ist.FramePadding.y * 2.f + 12.f, 96.f, 420.f);
  const float hintH = ImGui::GetTextLineHeightWithSpacing() + 4.f;
  const float fmtRowH = ImGui::GetFrameHeightWithSpacing() + 4.f;
  const float btnRowH = ImGui::GetFrameHeightWithSpacing() + 8.f;
  const float hContent = ist.WindowPadding.y * 2.f + hintH + ist.ItemSpacing.y + editorH + ist.ItemSpacing.y + fmtRowH +
                         ist.ItemSpacing.y + btnRowH;

  float wxp = sx0 + kPad;
  float wyp = sy0 + kPad;
  wxp = std::clamp(wxp, imgMin.x + 2.f, imgMax.x - w - 2.f);
  const float maxChildH = imgMax.y - imgMin.y - 4.f;
  float h = std::min(hContent, maxChildH);
  wyp = std::clamp(wyp, imgMin.y + 2.f, imgMax.y - h - 2.f);

  ImGui::SetCursorScreenPos(ImVec2(wxp, wyp));
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
  ImGuiWindowFlags editorWinFlags = 0;
  if (hContent > h + 0.5f)
    editorWinFlags |= ImGuiWindowFlags_AlwaysVerticalScrollbar;
  if (ImGui::BeginChild("##MtextRichEditor", ImVec2(w, h), ImGuiChildFlags_Borders, editorWinFlags)) {
    ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + innerW);
    ImGui::TextDisabled("%s",
                        "Rich tags: [[b]],[[i]],[[u]],[[caps]]…[[/…]]. Ctrl+Enter: normalize. Esc: cancel unsaved.");
    ImGui::PopTextWrapPos();
    if (cmd.mtextRichEditorFocusRequest) {
      ImGui::SetKeyboardFocusHere(0);
      cmd.mtextRichEditorFocusRequest = false;
    }
    ImGuiInputTextFlags flags = ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_CallbackAlways |
                                ImGuiInputTextFlags_CallbackCharFilter;
    ImGui::InputTextMultiline("##mtext_rte_body", &cmd.mtextRichEditorBuf, ImVec2(-FLT_MIN, editorH), flags,
                              MtextRichEditorInputCallback, static_cast<void*>(&cmd));
    const ImGuiID bodyId = ImGui::GetItemID();
    ImGuiIO& io = ImGui::GetIO();
    if (ImGui::GetActiveID() == bodyId && io.KeyCtrl &&
        (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)))
      cmd.mtextRichEditorBuf = MtextRichNormalize(cmd.mtextRichEditorBuf);

    ImGui::PushID("mtext_rte_tb");
    if (ImGui::SmallButton("B"))
      MtextRichWrapSelection(cmd, "[[b]]", "[[/b]]");
    ImGui::SameLine();
    if (ImGui::SmallButton("I"))
      MtextRichWrapSelection(cmd, "[[i]]", "[[/i]]");
    ImGui::SameLine();
    if (ImGui::SmallButton("U"))
      MtextRichWrapSelection(cmd, "[[u]]", "[[/u]]");
    ImGui::SameLine();
    if (ImGui::SmallButton("CAPS"))
      MtextRichWrapSelection(cmd, "[[caps]]", "[[/caps]]");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(std::min(140.f, std::max(72.f, ImGui::GetContentRegionAvail().x * 0.28f)));
    static const struct {
      const char* label;
      const char* utf8;
    } kMathPick[] = {
        {"\xCF\x80 pi", "\xCF\x80"},               // U+03C0
        {"\xCE\xA3 Sigma", "\xCE\xA3"},           // U+03A3
        {"\xE2\x88\x9E infinity", "\xE2\x88\x9E"}, // U+221E
        {"\xE2\x89\xA4 leq", "\xE2\x89\xA4"},     // U+2264
        {"\xE2\x89\xA5 geq", "\xE2\x89\xA5"},     // U+2265
        {"\xC2\xB1 plus-minus", "\xC2\xB1"},       // U+00B1
        {"\xE2\x88\x9A sqrt", "\xE2\x88\x9A"},     // U+221A
        {"\xE2\x88\xAB integral", "\xE2\x88\xAB"}, // U+222B
        {"\xC3\x97 times", "\xC3\x97"},           // U+00D7
        {"\xC2\xB7 dot", "\xC2\xB7"},             // U+00B7
        {"\xCE\xB1 alpha", "\xCE\xB1"},           // U+03B1
        {"\xCE\xB8 theta", "\xCE\xB8"},           // U+03B8
        {"\xC2\xB0 degrees", "\xC2\xB0"},         // U+00B0
    };
    if (ImGui::BeginCombo("##mtext_math_ins", "Insert…")) {
      for (const auto& e : kMathPick) {
        if (ImGui::Selectable(e.label))
          MtextRichInsertAtCaret(cmd, e.utf8);
      }
      ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Abc##mtext_caps", &cmd.mtextRichEditorTypingAllCaps);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
      ImGui::SetTooltip("Type new ASCII in ALL CAPS");
    ImGui::PopID();

    const float saveW = ImGui::CalcTextSize("Save").x + ist.FramePadding.x * 2.f + 16.f;
    const float cancelW = ImGui::CalcTextSize("Cancel").x + ist.FramePadding.x * 2.f + 16.f;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.f, ImGui::GetContentRegionAvail().x - saveW - cancelW -
                                                                     ist.ItemInnerSpacing.x));
    if (ImGui::Button(cmd.mtextRichEditorPlacement ? "Place##mtext_rte_ok" : "Save##mtext_rte_ok"))
      CommitMtextRichEditor(cmd, log);
    ImGui::SameLine();
    if (ImGui::Button("Cancel##mtext_rte_cancel"))
      CancelMtextRichEditor(cmd, &log);
  }
  ImGui::EndChild();
  ImGui::PopStyleColor();
}

static std::vector<CadSnap::SnapCandidateEntry> g_snapPickMenuScratch;
static int g_snapMenuStep = 0; ///< 0 = choose snap type, 1 = choose instance
static float g_snapMenuSortX = 0.f;
static float g_snapMenuSortY = 0.f;
static CadSnap::Kind g_snapMenuSelectedKind = CadSnap::Kind::Endpoint;

static const char* SnapKindLabelForUi(CadSnap::Kind k) {
  switch (k) {
  case CadSnap::Kind::Endpoint:
    return "Endpoint";
  case CadSnap::Kind::Midpoint:
    return "Midpoint";
  case CadSnap::Kind::Center:
    return "Center";
  case CadSnap::Kind::Perpendicular:
    return "Perpendicular";
  case CadSnap::Kind::SurveyCenter:
    return "Survey";
  case CadSnap::Kind::GeometricCenter:
    return "Geo center";
  case CadSnap::Kind::Grip:
    return "Grip";
  }
  return "Snap";
}

static void FormatSnapPickLine(char* line, size_t cap, const AppCommandState& cmd, const CadSnap::Hit& h) {
  if (cap < 8)
    return;
  if (h.kind == CadSnap::Kind::SurveyCenter) {
    for (size_t i = 0; i < cmd.surveyPoints.size(); ++i) {
      const auto& p = cmd.surveyPoints[i];
      if (std::fabs(p.easting - h.x) < 1e-4f && std::fabs(p.northing - h.y) < 1e-4f) {
        std::snprintf(line, cap, "%s — ID %d — %s, %s", SnapKindLabelForUi(h.kind), p.id,
                      FormatLinear(static_cast<double>(h.x), cmd.displayLinearPrecision).c_str(),
                      FormatLinear(static_cast<double>(h.y), cmd.displayLinearPrecision).c_str());
        return;
      }
    }
  }
  std::snprintf(line, cap, "%s — %s, %s", SnapKindLabelForUi(h.kind),
                FormatLinear(static_cast<double>(h.x), cmd.displayLinearPrecision).c_str(),
                FormatLinear(static_cast<double>(h.y), cmd.displayLinearPrecision).c_str());
}

/// When a single annotation with viewport grips is selected, pull the cursor to the nearest grip inside the OSNAP
/// aperture (competes with geometry snap by closest distance to raw pick).
static void ApplyGripMagnetToGrips(AppCommandState& cmd, double rawX, double rawY, float halfH, float availY,
                                   double* ioX, double* ioY, CadSnap::Hit* out_snap) {
  if (!ioX || !ioY)
    return;
  if (cmd.selection.size() != 1 || cmd.selection[0].type != SelectedEntity::Type::Annotation)
    return;
  const int ix = cmd.selection[0].index;
  if (ix < 0 || static_cast<size_t>(ix) >= cmd.cadAnnotations.size())
    return;
  const CadAnnotation& a = cmd.cadAnnotations[static_cast<size_t>(ix)];
  const float tol = CadSnap::WorldToleranceFromPixels(availY, halfH, cmd.objectSnapAperturePx);
  const double tol2 = static_cast<double>(tol) * static_cast<double>(tol);
  auto dist2 = [](double px, double py, float qx, float qy) {
    const double dx = px - static_cast<double>(qx);
    const double dy = py - static_cast<double>(qy);
    return dx * dx + dy * dy;
  };
  double bestD2 = dist2(rawX, rawY, static_cast<float>(*ioX), static_cast<float>(*ioY));
  double bx = *ioX;
  double by = *ioY;
  auto offer = [&](float gx, float gy) {
    const double h = dist2(rawX, rawY, gx, gy);
    if (h <= tol2 && h < bestD2 - 1.e-15) {
      bestD2 = h;
      bx = static_cast<double>(gx);
      by = static_cast<double>(gy);
    }
  };
  offer(static_cast<float>(*ioX), static_cast<float>(*ioY));
  if (a.kind == CadAnnotation::Kind::DimAligned || a.kind == CadAnnotation::Kind::DimLinear) {
    float sx1 = 0.f, sy1 = 0.f, sx2 = 0.f, sy2 = 0.f, tx = 0.f, ty = 0.f, nx = 0.f, ny = 0.f, ml = 0.f;
    if (CadDimAnyGeometry(a, &sx1, &sy1, &sx2, &sy2, &tx, &ty, &nx, &ny, &ml)) {
      offer(a.dimExt1X, a.dimExt1Y);
      offer(a.dimExt2X, a.dimExt2Y);
      offer(sx1, sy1);
      offer(sx2, sy2);
      offer(a.insX, a.insY);
    }
  } else if (a.kind == CadAnnotation::Kind::Mtext) {
    if (a.surveyPointLabelFor >= 0)
      offer(0.5f * (a.boxMinX + a.boxMaxX), 0.5f * (a.boxMinY + a.boxMaxY));
    else {
      offer(a.boxMinX, a.boxMinY);
      offer(a.boxMaxX, a.boxMinY);
      offer(a.boxMaxX, a.boxMaxY);
      offer(a.boxMinX, a.boxMaxY);
    }
  }
  if (bx != *ioX || by != *ioY) {
    *ioX = bx;
    *ioY = by;
    cmd.viewportSnapPickValid = true;
    cmd.viewportSnapPickWorldX = bx;
    cmd.viewportSnapPickWorldY = by;
    if (out_snap) {
      out_snap->valid = true;
      out_snap->kind = CadSnap::Kind::Grip;
      out_snap->x = bx;
      out_snap->y = by;
    }
  }
}

void DrawDrawingViewport(unsigned int viewportTextureId, AppCommandState& cmd, std::vector<std::string>& log,
                         char* cmdBuf, int cmdBufSize, double* panX, double* panY, float* zoom, double* outCursorX,
                         double* outCursorY, double* outCursorRawX, double* outCursorRawY, int* outFbW, int* outFbH,
                         CadSnap::Hit* out_snap) {
  ImGui::SetNextWindowSize(ImVec2(900, 650), ImGuiCond_FirstUseEver);
  if (cmd.pendingViewportFocus) {
    ImGui::SetNextWindowFocus();
    cmd.pendingViewportFocus = false;
  }
  if (!ImGui::Begin("Viewports", nullptr)) {
    cmd.viewportDrawingHovered = false;
    cmd.viewportCmdPaletteEngaged = false;
    ImGui::End();
    return;
  }

  // Drawing tab bar — each open drawing is a closeable tab; "+" creates a new one.
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, 3.f));
  if (ImGui::BeginTabBar("##DrawingTabs",
                         ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_FittingPolicyScroll)) {
    for (int i = 0; i < static_cast<int>(cmd.drawingTabs.size()); ++i) {
      bool tabOpen = true;
      // SetSelected only fires on the one frame after a programmatic switch (e.g. "+").
      // Applying it every frame would override ImGui's own click handling.
      const bool wantSelect = cmd.pendingDrawingTabSwitch && (i == cmd.activeDrawingIdx);
      const ImGuiTabItemFlags tflags = wantSelect ? ImGuiTabItemFlags_SetSelected : 0;
      // Append "##<uid>" so each tab has a unique ImGui ID even when two tabs share the same display name.
      const std::string tabLabel = cmd.drawingTabs[i].name + "##dt" + std::to_string(cmd.drawingTabs[i].uid);
      if (ImGui::BeginTabItem(tabLabel.c_str(), &tabOpen, tflags)) {
        cmd.activeDrawingIdx = i;
        cmd.pendingDrawingTabSwitch = false;  // consumed
        ImGui::EndTabItem();
      }
      if (!tabOpen && cmd.drawingTabs.size() > 1) {
        const int closeIdx  = i;
        const int tabCount  = static_cast<int>(cmd.drawingTabs.size());

        // Which tab becomes active after this one closes?
        int newActive = cmd.activeDrawingIdx;
        if (closeIdx == cmd.activeDrawingIdx) {
          newActive = (closeIdx > 0) ? closeIdx - 1 : closeIdx + 1;
          // Load that tab's snapshot into cmd before we erase anything.
          RestoreDocumentFromSnapshot(cmd, newActive);
        }
        // Adjust for the index shift the erase will produce.
        if (newActive > closeIdx) --newActive;
        newActive = std::max(0, std::min(newActive, tabCount - 2));

        // Erase tab + matching document snapshot so indices stay aligned.
        cmd.drawingTabs.erase(cmd.drawingTabs.begin() + closeIdx);
        if (closeIdx < static_cast<int>(cmd.documents.size()))
          cmd.documents.erase(cmd.documents.begin() + closeIdx);

        // Tell main.cpp to erase + shut down the matching renderer.
        cmd.pendingTabErase = closeIdx;

        cmd.activeDrawingIdx    = newActive;
        cmd.prevDrawingIdx      = newActive;  // suppress spurious switch detection
        cmd.pendingDrawingTabSwitch = true;   // visually select the new active tab
        --i;
      }
    }
    // Trailing "+" to open a new empty drawing.
    if (ImGui::TabItemButton("+", ImGuiTabItemFlags_Trailing | ImGuiTabItemFlags_NoTooltip)) {
      cmd.drawingTabs.push_back({"Drawing " + std::to_string(cmd.nextDrawingNumber++), cmd.nextTabUid++});
      cmd.activeDrawingIdx        = static_cast<int>(cmd.drawingTabs.size()) - 1;
      cmd.pendingDrawingTabSwitch = true;
      cmd.pendingViewportFocus    = true;
    }
    ImGui::EndTabBar();
  }
  ImGui::PopStyleVar();

  ImVec2 avail = ImGui::GetContentRegionAvail();
  avail.y = std::max(avail.y, 80.f);
  ImVec2 imgPos = ImGui::GetCursorScreenPos();

  const float aspect = avail.x / std::max(avail.y, 1.f);

  ImGui::Image(static_cast<ImTextureID>(static_cast<std::intptr_t>(viewportTextureId)), avail, ImVec2(0, 1),
               ImVec2(1, 0));

  const bool hovered = ImGui::IsItemHovered();
  const ImVec2 mouse = ImGui::GetIO().MousePos;
  const float mx = mouse.x - imgPos.x;
  const float my = mouse.y - imgPos.y;
  // In paper space, model-entity picking/selection is suppressed (pan/zoom still work); paper-space
  // viewport interaction is handled separately (REQ-025/027).
  const bool modelSpace = cmd.activeSpaceIndex == kModelSpaceIndex;

  // Floating model space (REQ-036): Esc does NOT exit — it cancels the active model command so the user
  // keeps editing in the viewport. Exit is via double-click outside the viewport (below), the FLOAT
  // button, or PSPACE.

  // When editing a viewport in place and the zoom lock is OFF, pan/zoom targets the viewport's model
  // framing instead of the sheet (handled below, after paper coords are available).
  const bool routeZoomToViewport = InFloatingModelSpace(cmd) && !cmd.viewportZoomLocked;

  if (hovered && !routeZoomToViewport) {
    const float wheel = ImGui::GetIO().MouseWheel;
    if (wheel != 0.f && mx >= 0.f && mx < avail.x && my >= 0.f && my < avail.y) {
      const double u = static_cast<double>(mx) / static_cast<double>(std::max(avail.x, 1.f));
      const double v = static_cast<double>(my) / static_cast<double>(std::max(avail.y, 1.f));
      const double z0 = static_cast<double>(*zoom);
      const double halfH0 = (1.0 / std::max(z0, 1.e-9)) * 50.0;
      // AutoCAD ZOOMFACTOR analog: settings → Display → Zoom factor (1.01..3.0). Each wheel notch multiplies the
      // zoom by `factor`. Sub-notch wheel deltas use the same `factor^wheel` curve.
      const double factor = std::clamp(static_cast<double>(cmd.displayWheelZoomFactor), 1.01, 3.0);
      const double mul = std::pow(factor, static_cast<double>(wheel));
      const double z1 = std::clamp(z0 * mul, 1.e-9, 1.e9);
      const double halfH1 = (1.0 / std::max(z1, 1.e-9)) * 50.0;
      const double dh = halfH0 - halfH1;
      const double aspectD = static_cast<double>(aspect);
      *panX += (u - 0.5) * 2.0 * aspectD * dh;
      *panY += dh * (1.0 - 2.0 * v);
      *zoom = static_cast<float>(z1);
    }

    if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
      ImVec2 d = ImGui::GetIO().MouseDelta;
      const double aspectD = static_cast<double>(avail.x) / static_cast<double>(std::max(avail.y, 1.f));
      const double halfH = (1.0 / std::max(static_cast<double>(*zoom), 1.e-9)) * 50.0;
      const double halfW = halfH * aspectD;
      *panX -= (static_cast<double>(d.x) / static_cast<double>(std::max(avail.x, 1.f))) * (2.0 * halfW);
      *panY += (static_cast<double>(d.y) / static_cast<double>(std::max(avail.y, 1.f))) * (2.0 * halfH);
    }
  }

  const int vpFbW = static_cast<int>(std::max(1.f, std::floor(avail.x)));
  const int vpFbH = static_cast<int>(std::max(1.f, std::floor(avail.y)));
  ProcessPendingViewportZoom(cmd, panX, panY, zoom, vpFbW, vpFbH, aspect, log);
  const float halfH = (1.f / std::max(*zoom, 1.e-9f)) * 50.f;
  const float halfW = halfH * aspect;
  const double panXd = *panX;
  const double panYd = *panY;
  const double halfWd = static_cast<double>(halfW);
  const double halfHd = static_cast<double>(halfH);
  const double worldLeft = -halfWd + panXd;
  const double worldRight = halfWd + panXd;
  const double worldBottom = -halfHd + panYd;
  const double worldTop = halfHd + panYd;
  const float surveyCrossHalfW =
      SurveyPointCrossHalfWorldFromPaper(cmd.surveyPointCrossSpanPlottedInches, cmd.modelUnitsPerPlottedInch);

  // Paper coords (inches) under the cursor — paper inches are the "world" units in paper space.
  auto screenToPaperIn = [&](float* outX, float* outY) {
    *outX = static_cast<float>(worldLeft + (mx / std::max(avail.x, 1.f)) * (worldRight - worldLeft));
    *outY = static_cast<float>(worldTop - (my / std::max(avail.y, 1.f)) * (worldTop - worldBottom));
  };

  // Viewport zoom/pan (user request): editing a viewport in place with the lock OFF — wheel zooms the
  // viewport's model framing about the cursor; middle-drag pans the model within the viewport.
  if (routeZoomToViewport && hovered && cmd.floatingViewportLayout >= 0 &&
      cmd.floatingViewportLayout < static_cast<int>(cmd.paperLayouts.size())) {
    PaperLayout& FZ = cmd.paperLayouts[static_cast<size_t>(cmd.floatingViewportLayout)];
    if (cmd.floatingViewportIndex >= 0 && cmd.floatingViewportIndex < static_cast<int>(FZ.viewports.size())) {
      Viewport& vp = FZ.viewports[static_cast<size_t>(cmd.floatingViewportIndex)];
      float cpx = 0.f, cpy = 0.f;
      screenToPaperIn(&cpx, &cpy);
      const bool inside = cpx >= vp.paperXIn && cpx <= vp.paperXIn + vp.paperWIn && cpy >= vp.paperYIn &&
                          cpy <= vp.paperYIn + vp.paperHIn;
      const float vcx = vp.paperXIn + vp.paperWIn * 0.5f;
      const float vcy = vp.paperYIn + vp.paperHIn * 0.5f;
      if (inside) {
        const float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.f) {
          const double factor = std::clamp(static_cast<double>(cmd.displayWheelZoomFactor), 1.01, 3.0);
          const double mul = std::pow(factor, static_cast<double>(wheel));
          const float s0 = vp.safeScale();
          const float s1 = std::clamp(static_cast<float>(s0 / mul), 1.e-6f, 1.e9f);  // wheel up → zoom in
          const double curMX = vp.modelCenterX + static_cast<double>(cpx - vcx) * s0;  // keep cursor point fixed
          const double curMY = vp.modelCenterY + static_cast<double>(cpy - vcy) * s0;
          vp.modelCenterX = curMX - static_cast<double>(cpx - vcx) * s1;
          vp.modelCenterY = curMY - static_cast<double>(cpy - vcy) * s1;
          vp.scaleModelPerPaperIn = s1;
          BumpCadGpuCache(cmd);
        }
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
          const ImVec2 d = ImGui::GetIO().MouseDelta;
          const double dPaperX = static_cast<double>(d.x / std::max(avail.x, 1.f)) * (worldRight - worldLeft);
          const double dPaperY = static_cast<double>(d.y / std::max(avail.y, 1.f)) * (worldTop - worldBottom);
          const float s = vp.safeScale();
          vp.modelCenterX -= dPaperX * static_cast<double>(s);
          vp.modelCenterY += dPaperY * static_cast<double>(s);
          BumpCadGpuCache(cmd);
        }
      }
    }
  }

  // Rectangular-viewport command (REQ-033): two clicks define the rect (preview in the overlay below).
  bool consumedPaperClick = false;  // the click that finished RECTVP must not also grab a grip this frame.
  if (!modelSpace && cmd.active == AppCommandState::Kind::PaperRectViewport && hovered &&
      ImGui::IsMouseClicked(ImGuiMouseButton_Left) && mx >= 0 && mx < avail.x && my >= 0 && my < avail.y) {
    float px = 0.f, py = 0.f;
    screenToPaperIn(&px, &py);
    if (cmd.paperVpPhase == 0) {
      cmd.paperVpFirstXIn = px;
      cmd.paperVpFirstYIn = py;
      cmd.paperVpPhase = 1;
      log.push_back("Rectangular viewport — click the opposite corner (Esc to cancel).");
    } else {
      AddViewportRect(cmd, cmd.activeSpaceIndex, cmd.paperVpFirstXIn, cmd.paperVpFirstYIn, px, py);
      cmd.active = AppCommandState::Kind::None;
      cmd.paperVpPhase = 0;
      consumedPaperClick = true;
      log.push_back("Rectangular viewport created.");
    }
  }

  // Paper-space viewport selection + grip edit + MOVE/COPY (REQ-035). Active only while idle of the
  // rectangular-viewport command and in a paper layout.
  if (!modelSpace && !InFloatingModelSpace(cmd) && cmd.active == AppCommandState::Kind::None &&
      cmd.activeSpaceIndex >= 0 && cmd.activeSpaceIndex < static_cast<int>(cmd.paperLayouts.size())) {
    PaperLayout& L = cmd.paperLayouts[static_cast<size_t>(cmd.activeSpaceIndex)];
    float curX = 0.f, curY = 0.f;
    screenToPaperIn(&curX, &curY);
    const float pxPerWorld = avail.x / std::max(1.e-6f, static_cast<float>(worldRight - worldLeft));
    const float gripTolIn = 7.f / std::max(1.e-6f, pxPerWorld);
    const bool clickL = hovered && !consumedPaperClick && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                        mx >= 0 && mx < avail.x && my >= 0 && my < avail.y;

    // Double-click inside a viewport → floating model space (REQ-036). A real double-click is two clicks
    // at the same spot, so it's distinct from a two-corner window box. The first click may have started a
    // window box (interior clicks do) — cancel it and enter the viewport instead.
    bool enteredFloat = false;
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && mx >= 0 && mx < avail.x && my >= 0 &&
        my < avail.y && cmd.paperMovePhase == 0 && cmd.paperGripCorner == -2) {
      for (int vi = static_cast<int>(L.viewports.size()) - 1; vi >= 0; --vi) {
        const Viewport& v = L.viewports[static_cast<size_t>(vi)];
        if (curX >= v.paperXIn && curX <= v.paperXIn + v.paperWIn && curY >= v.paperYIn &&
            curY <= v.paperYIn + v.paperHIn) {
          cmd.paperSelBoxActive = false;  // discard the box the first click of the double-click started
          EnterFloatingModelSpace(cmd, cmd.activeSpaceIndex, vi, log);
          enteredFloat = true;
          break;
        }
      }
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
      if (cmd.paperMovePhase != 0 || cmd.paperGripCorner != -2 || cmd.paperSelBoxActive) {
        cmd.paperMovePhase = 0;
        cmd.paperGripCorner = -2;
        cmd.paperSelBoxActive = false;
      } else if (!cmd.selectedViewports.empty()) {
        ClearViewportSelection(cmd);
      }
    }

    auto primaryVp = [&]() -> Viewport* {
      if (cmd.selectedViewports.size() == 1 && cmd.selectedViewportIndex >= 0 &&
          cmd.selectedViewportIndex < static_cast<int>(L.viewports.size()))
        return &L.viewports[static_cast<size_t>(cmd.selectedViewportIndex)];
      return nullptr;
    };

    if (clickL && cmd.paperMovePhase == 1) {  // MOVE/COPY: base point
      cmd.paperMoveBaseXIn = curX;
      cmd.paperMoveBaseYIn = curY;
      cmd.paperMovePhase = 2;
      log.push_back("Click the destination point.");
    } else if (clickL && cmd.paperMovePhase == 2) {  // MOVE/COPY: destination
      TranslateSelectedViewports(cmd, curX - cmd.paperMoveBaseXIn, curY - cmd.paperMoveBaseYIn,
                                 cmd.paperMoveIsCopy, log);
      cmd.paperMovePhase = 0;
    } else if (clickL && cmd.paperGripCorner != -2) {  // commit an in-progress grip edit
      cmd.paperGripCorner = -2;
      log.push_back("Viewport edited.");
    } else if (clickL && cmd.paperSelBoxActive) {
      // A window-select box is open — this click closes it (priority over grip/body picking). Same
      // convention as model geometry: left-to-right = window (viewport fully inside); right-to-left =
      // crossing (any overlap).
      const bool windowMode = curX >= cmd.paperSelBoxX0In;
      const float bx0 = std::min(cmd.paperSelBoxX0In, curX), bx1 = std::max(cmd.paperSelBoxX0In, curX);
      const float by0 = std::min(cmd.paperSelBoxY0In, curY), by1 = std::max(cmd.paperSelBoxY0In, curY);
      cmd.selectedViewports.clear();
      for (int vi = 0; vi < static_cast<int>(L.viewports.size()); ++vi) {
        const Viewport& v = L.viewports[static_cast<size_t>(vi)];
        const float vx0 = v.paperXIn, vy0 = v.paperYIn, vx1 = v.paperXIn + v.paperWIn, vy1 = v.paperYIn + v.paperHIn;
        const bool sel = windowMode ? (vx0 >= bx0 && vx1 <= bx1 && vy0 >= by0 && vy1 <= by1)
                                    : (vx0 <= bx1 && vx1 >= bx0 && vy0 <= by1 && vy1 >= by0);
        if (sel)
          cmd.selectedViewports.push_back(vi);
      }
      cmd.selectedViewportIndex = cmd.selectedViewports.empty() ? -1 : cmd.selectedViewports.back();
      cmd.selectedViewportLayout = cmd.selectedViewports.empty() ? -1 : cmd.activeSpaceIndex;
      cmd.paperSelBoxActive = false;
      BumpCadGpuCache(cmd);
    } else if (clickL && !enteredFloat) {
      // 1) a grip of the single selected viewport?
      int gripCorner = -2;
      if (Viewport* v = primaryVp()) {
        const float x0 = v->paperXIn, y0 = v->paperYIn, x1 = v->paperXIn + v->paperWIn, y1 = v->paperYIn + v->paperHIn;
        const float cxp = (x0 + x1) * 0.5f, cyp = (y0 + y1) * 0.5f;
        const float gx[5] = {x0, x1, x1, x0, cxp};
        const float gy[5] = {y0, y0, y1, y1, cyp};
        const int gc[5] = {0, 1, 2, 3, -1};  // corners 0..3, center = -1 (move)
        for (int i = 0; i < 5; ++i)
          if (std::fabs(curX - gx[i]) <= gripTolIn && std::fabs(curY - gy[i]) <= gripTolIn) {
            gripCorner = gc[i];
            break;
          }
      }
      if (gripCorner != -2) {
        cmd.paperGripCorner = gripCorner;
        log.push_back(gripCorner == -1 ? "Move viewport — click the new location." : "Resize — click the new corner.");
      } else {
        // 2) a viewport BORDER (topmost wins)? Clicking the interior is the model view, so it does not
        // select — that lets a window box start even when viewports cover the sheet (AutoCAD behavior).
        const float bt = std::max(gripTolIn, 5.f / std::max(1.e-6f, pxPerWorld));
        int hit = -1;
        for (int vi = static_cast<int>(L.viewports.size()) - 1; vi >= 0; --vi) {
          const Viewport& v = L.viewports[static_cast<size_t>(vi)];
          const float x0 = v.paperXIn, y0 = v.paperYIn, x1 = v.paperXIn + v.paperWIn, y1 = v.paperYIn + v.paperHIn;
          const bool inOuter = curX >= x0 - bt && curX <= x1 + bt && curY >= y0 - bt && curY <= y1 + bt;
          const bool inInner = curX >= x0 + bt && curX <= x1 - bt && curY >= y0 + bt && curY <= y1 - bt;
          if (inOuter && !inInner) {
            hit = vi;
            break;
          }
        }
        if (hit >= 0) {
          SelectViewport(cmd, hit, ImGui::GetIO().KeyShift);
        } else {  // interior or empty: start a window-select box
          cmd.paperSelBoxActive = true;
          cmd.paperSelBoxX0In = curX;
          cmd.paperSelBoxY0In = curY;
        }
      }
    }

    // Live grip edit: the grabbed viewport follows the cursor until the commit click.
    if (cmd.paperGripCorner != -2) {
      if (Viewport* v = primaryVp()) {
        if (cmd.paperGripCorner == -1) {  // move whole viewport (center follows cursor)
          v->paperXIn = curX - v->paperWIn * 0.5f;
          v->paperYIn = curY - v->paperHIn * 0.5f;
        } else {  // resize: grabbed corner → cursor, opposite corner fixed
          float x0 = v->paperXIn, y0 = v->paperYIn, x1 = v->paperXIn + v->paperWIn, y1 = v->paperYIn + v->paperHIn;
          switch (cmd.paperGripCorner) {
          case 0: x0 = curX; y0 = curY; break;
          case 1: x1 = curX; y0 = curY; break;
          case 2: x1 = curX; y1 = curY; break;
          case 3: x0 = curX; y1 = curY; break;
          default: break;
          }
          v->paperXIn = std::min(x0, x1);
          v->paperYIn = std::min(y0, y1);
          v->paperWIn = std::max(0.1f, std::fabs(x1 - x0));
          v->paperHIn = std::max(0.1f, std::fabs(y1 - y0));
        }
        BumpCadGpuCache(cmd);
      } else {
        cmd.paperGripCorner = -2;  // selection changed underneath us
      }
    }
  }

  // Floating model space (REQ-036): map the cursor through the active viewport and route model command
  // clicks so the model is edited IN PLACE inside the viewport rect (the sheet stays visible).
  if (InFloatingModelSpace(cmd) && cmd.floatingViewportLayout >= 0 &&
      cmd.floatingViewportLayout < static_cast<int>(cmd.paperLayouts.size())) {
    PaperLayout& FL = cmd.paperLayouts[static_cast<size_t>(cmd.floatingViewportLayout)];
    if (cmd.floatingViewportIndex >= 0 && cmd.floatingViewportIndex < static_cast<int>(FL.viewports.size())) {
      const Viewport& fv = FL.viewports[static_cast<size_t>(cmd.floatingViewportIndex)];
      float px = 0.f, py = 0.f;
      screenToPaperIn(&px, &py);
      const bool inside = px >= fv.paperXIn && px <= fv.paperXIn + fv.paperWIn && py >= fv.paperYIn &&
                          py <= fv.paperYIn + fv.paperHIn;
      const float vcx = fv.paperXIn + fv.paperWIn * 0.5f;
      const float vcy = fv.paperYIn + fv.paperHIn * 0.5f;
      const float s = fv.safeScale();
      const double mLocalX = (fv.modelCenterX + static_cast<double>(px - vcx) * s) - cmd.worldDocumentOriginX;
      const double mLocalY = (fv.modelCenterY + static_cast<double>(py - vcy) * s) - cmd.worldDocumentOriginY;
      if (inside) {
        if (outCursorX && outCursorY) {
          *outCursorX = mLocalX;
          *outCursorY = mLocalY;
        }
        if (outCursorRawX && outCursorRawY) {
          *outCursorRawX = mLocalX;
          *outCursorRawY = mLocalY;
        }
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            cmd.active != AppCommandState::Kind::None)
          SubmitViewportPick(cmd, static_cast<float>(mLocalX), static_cast<float>(mLocalY), log);
      } else if (hovered && cmd.active == AppCommandState::Kind::None &&
                 ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && mx >= 0 && mx < avail.x && my >= 0 &&
                 my < avail.y) {
        // Double-click outside the active viewport returns to paper editing (REQ-036).
        ExitFloatingModelSpace(cmd, log);
      }
    }
  }

  {
    ImGuiIO& ioVpRmb = ImGui::GetIO();
    if (modelSpace && hovered && mx >= 0 && mx < avail.x && my >= 0 && my < avail.y) {
      const float uR = mx / std::max(avail.x, 1.f);
      const float vR = my / std::max(avail.y, 1.f);
      const double rmbWx = worldLeft + static_cast<double>(uR) * (worldRight - worldLeft);
      const double rmbWy = worldTop - static_cast<double>(vR) * (worldTop - worldBottom);
      using AK = AppCommandState::Kind;
      const bool blockSnapPickMenu = cmd.mtextRichEditorOpen || cmd.selBoxWaitingSecond || cmd.dimGripMoveActive ||
                                     cmd.entityGripMoveActive || cmd.mtextGripMoveActive;
      const bool allowSnapCycle =
          cmd.active != AK::None && cmd.objectSnapEnabled && !blockSnapPickMenu;
      if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        if (ioVpRmb.KeyShift && allowSnapCycle) {
          g_snapMenuSortX = static_cast<float>(rmbWx);
          g_snapMenuSortY = static_cast<float>(rmbWy);
          g_snapMenuStep = 0;
          g_snapPickMenuScratch.clear();
          ImGui::OpenPopup("##gos_snap_pick");
        } else if (!blockSnapPickMenu) {
          using DM = AppCommandState::RightClickDefaultMode;
          using EM = AppCommandState::RightClickEditMode;
          using CM = AppCommandState::RightClickCommandMode;
          const bool hasSel = !cmd.selection.empty() || !cmd.selectedSurveyPointIndices.empty();
          if (cmd.active != AK::None) {
            switch (cmd.rightClickCommandMode) {
            case CM::Enter:
              ProcessCommandLineSubmit(cmdBuf, cmdBufSize, cmd, log);
              break;
            case CM::ShortcutMenuAlways:
            case CM::ShortcutMenuWhenOptions:
              ImGui::OpenPopup("##drawing1_vp_ctx");
              break;
            }
          } else if (hasSel) {
            switch (cmd.rightClickEditMode) {
            case EM::RepeatLastCommand:
              if (cmd.lastCommand != AK::None) RepeatLastCommand(cmd, log);
              else ImGui::OpenPopup("##drawing1_vp_ctx");
              break;
            case EM::ShortcutMenu:
              ImGui::OpenPopup("##drawing1_vp_ctx");
              break;
            }
          } else {
            switch (cmd.rightClickDefaultMode) {
            case DM::RepeatLastCommand:
              if (cmd.lastCommand != AK::None) RepeatLastCommand(cmd, log);
              else ImGui::OpenPopup("##drawing1_vp_ctx");
              break;
            case DM::ShortcutMenu:
              ImGui::OpenPopup("##drawing1_vp_ctx");
              break;
            }
          }
        }
      }
    }
  }

  cmd.viewportLastSurveyLayoutOrthoHalfH = halfH;
  cmd.viewportLastSurveyLayoutHeightPx = avail.y;

  {
    const float ch = cmd.surveyLabelLayoutCacheHalfH;
    const float cy = cmd.surveyLabelLayoutCacheVpHeightPx;
    const float cm = cmd.surveyLabelLayoutCacheMup;
    const bool layoutCacheValid = ch >= 0.f && cy >= 0.f && cm >= 0.f;
    const float relHalf = std::max(ch, 1.e-4f);
    const bool zoomViewportChanged =
        !layoutCacheValid || std::fabs(halfH - ch) > std::max(0.002f * relHalf, 1.e-5f) ||
        std::fabs(avail.y - cy) > 0.5f ||
        std::fabs(cmd.modelUnitsPerPlottedInch - cm) > 0.001f;
    if (zoomViewportChanged && !cmd.surveyPoints.empty()) {
      cmd.surveyLabelLayoutCacheHalfH = halfH;
      cmd.surveyLabelLayoutCacheVpHeightPx = avail.y;
      cmd.surveyLabelLayoutCacheMup = cmd.modelUnitsPerPlottedInch;
      RepositionAllSurveyPointLabels(cmd);
      BumpCadGpuCache(cmd);
    }
  }

  cmd.viewportHoverSurveyPointIndex = -1;
  cmd.offsetHoverHighlightValid = false;
  cmd.viewportSnapPickValid = false;
  *out_snap = {};

  if (hovered && mx >= 0 && mx < avail.x && my >= 0 && my < avail.y) {
    const float u = mx / std::max(avail.x, 1.f);
    const float v = my / std::max(avail.y, 1.f);
    const double rawX = worldLeft + static_cast<double>(u) * (worldRight - worldLeft);
    const double rawY = worldTop - static_cast<double>(v) * (worldTop - worldBottom);

    if (outCursorRawX)
      *outCursorRawX = rawX;
    if (outCursorRawY)
      *outCursorRawY = rawY;

    using OP = AppCommandState::OffsetPhase;
    if (cmd.active == AppCommandState::Kind::Offset && cmd.offsetPhase == OP::WaitSelectEntity) {
      SelectedEntity hit{};
      float d2 = 0.f;
      const float offTol = CadOffsetEntityPickTolWorld(cmd);
      if (PickClosestCadEntity(cmd, rawX, rawY, offTol, &hit, &d2)) {
        cmd.offsetHoverHighlightValid = true;
        cmd.offsetHoverEntity = hit;
      } else {
        cmd.offsetHoverHighlightValid = false;
      }
    } else
      cmd.offsetHoverHighlightValid = false;

    using AK = AppCommandState::Kind;
    const bool blockSurveyHover = cmd.active != AK::None || cmd.dimGripMoveActive || cmd.entityGripMoveActive ||
                                  cmd.mtextGripMoveActive || cmd.mtextRichEditorOpen || cmd.selBoxWaitingSecond;
    if (!cmd.surveyPoints.empty() && !blockSurveyHover)
      cmd.viewportHoverSurveyPointIndex =
          PickSurveyPointIndex(cmd.surveyPoints, rawX, rawY, surveyCrossHalfW, avail.y, halfH,
                               cmd.objectSnapAperturePx);

    // Idle hover: detect CAD entity under cursor for subtle highlight feedback.
    {
      const bool blockEntityHover = cmd.active != AK::None || cmd.dimGripMoveActive ||
                                    cmd.entityGripMoveActive || cmd.mtextGripMoveActive || cmd.selBoxWaitingSecond;
      if (!blockEntityHover) {
        SelectedEntity hoverHit{};
        float hoverD2 = 0.f;
        const float hoverTol = CadHoverEntityPickTolWorld(cmd);
        if (PickClosestCadEntity(cmd, rawX, rawY, hoverTol, &hoverHit, &hoverD2)) {
          cmd.viewportHoverEntityValid = true;
          cmd.viewportHoverEntity = hoverHit;
        } else {
          cmd.viewportHoverEntityValid = false;
        }
      } else {
        cmd.viewportHoverEntityValid = false;
      }
    }

    if (cmd.pendingOneShotSnapValid && outCursorX && outCursorY) {
      *outCursorX = cmd.pendingOneShotSnapX;
      *outCursorY = cmd.pendingOneShotSnapY;
      cmd.viewportSnapPickValid = true;
      cmd.viewportSnapPickWorldX = cmd.pendingOneShotSnapX;
      cmd.viewportSnapPickWorldY = cmd.pendingOneShotSnapY;
      if (out_snap) {
        out_snap->valid = true;
        out_snap->kind = static_cast<CadSnap::Kind>(cmd.pendingOneShotSnapKind);
        out_snap->x = cmd.pendingOneShotSnapX;
        out_snap->y = cmd.pendingOneShotSnapY;
      }
    } else {
      cmd.viewportSnapPickValid = false;
      const bool midCmd = cmd.active != AppCommandState::Kind::None || cmd.showCreatePointsWindow ||
                          cmd.dimGripMoveActive || cmd.entityGripMoveActive || cmd.mtextGripMoveActive;
      const bool snapViewportActive = cmd.objectSnapEnabled && midCmd;
      CadSnap::Hit snap{};
      if (snapViewportActive) {
        const float tol = CadSnap::WorldToleranceFromPixels(avail.y, halfH, cmd.objectSnapAperturePx);
        CadSnap::SnapExclude exclude{};
        if (cmd.entityGripMoveActive && cmd.entityGripEntityIndex >= 0) {
          exclude.valid = true;
          exclude.type  = cmd.entityGripType;
          exclude.index = cmd.entityGripEntityIndex;
        }
        snap = CadSnap::FindBest(rawX, rawY, cmd, midCmd, tol, exclude);
        if (snap.valid) {
          cmd.viewportSnapPickValid = true;
          cmd.viewportSnapPickWorldX = snap.x;
          cmd.viewportSnapPickWorldY = snap.y;
          if (out_snap)
            *out_snap = snap;
          const double dx = static_cast<double>(snap.x) - rawX;
          const double dy = static_cast<double>(snap.y) - rawY;
          const double dist = std::hypot(dx, dy);
          const double outer = static_cast<double>(tol) * 2.75;
          double alpha = 0.;
          if (outer > 1.e-12 && dist < outer) {
            const double uMag = std::clamp(1.0 - dist / outer, 0.0, 1.0);
            alpha = uMag * uMag * 0.58;
            if (dist < static_cast<double>(tol))
              alpha = std::max(alpha, 0.88);
            alpha = std::min(alpha, 0.92);
          }
          *outCursorX = rawX + alpha * dx;
          *outCursorY = rawY + alpha * dy;
        } else {
          if (out_snap)
            out_snap->valid = false;
          *outCursorX = rawX;
          *outCursorY = rawY;
        }
      } else {
        if (out_snap)
          out_snap->valid = false;
        *outCursorX = rawX;
        *outCursorY = rawY;
      }
    }
    if (!cmd.pendingOneShotSnapValid && outCursorX && outCursorY &&
        !cmd.dimGripMoveActive && !cmd.entityGripMoveActive && !cmd.mtextGripMoveActive) {
      ApplyGripMagnetToGrips(cmd, rawX, rawY, halfH, avail.y, outCursorX, outCursorY, out_snap);
      // Silent grip snap for all selected entities — no glyph, works regardless of OSNAP toggle.
      if (!cmd.selection.empty() || !cmd.selectedSurveyPointIndices.empty()) {
        const float gripTol = CadSnap::WorldToleranceFromPixels(avail.y, halfH, cmd.objectSnapAperturePx);
        const CadSnap::Hit gs = CadSnap::FindGripSnap(rawX, rawY, cmd, gripTol);
        if (gs.valid && outCursorX && outCursorY) {
          *outCursorX = gs.x;
          *outCursorY = gs.y;
          // Intentionally do NOT set viewportSnapPickValid or out_snap — grip snap is silent.
        }
      }
    }
  }

  // MTEXT box grips: first click arms; snapped cursor updates box live; second LMB commits (like dim / entity grips).
  if (cmd.mtextGripMoveActive && cmd.mtextGripAnnotationIndex >= 0 && outCursorX && outCursorY && hovered &&
      mx >= 0.f && mx < avail.x && my >= 0.f && my < avail.y) {
    const float curWx = cmd.viewportSnapPickValid ? cmd.viewportSnapPickWorldX : *outCursorX;
    const float curWy = cmd.viewportSnapPickValid ? cmd.viewportSnapPickWorldY : *outCursorY;
    const size_t gi = static_cast<size_t>(cmd.mtextGripAnnotationIndex);
    if (gi < cmd.cadAnnotations.size()) {
      CadAnnotation& ann = cmd.cadAnnotations[gi];
      if (ann.kind == CadAnnotation::Kind::Mtext) {
        if (ann.surveyPointLabelFor >= 0 && cmd.mtextGripCorner == 4) {
          const float dx = curWx - cmd.mtextGripDownWorldX;
          const float dy = curWy - cmd.mtextGripDownWorldY;
          ann.boxMinX = cmd.mtextGripOrigBoxMinX + dx;
          ann.boxMaxX = cmd.mtextGripOrigBoxMaxX + dx;
          ann.boxMinY = cmd.mtextGripOrigBoxMinY + dy;
          ann.boxMaxY = cmd.mtextGripOrigBoxMaxY + dy;
          ann.insX = ann.boxMinX;
          ann.insY = ann.boxMinY;
        } else {
          const float fx = cmd.mtextGripFixedCornerX;
          const float fy = cmd.mtextGripFixedCornerY;
          ann.boxMinX = std::min(fx, curWx);
          ann.boxMaxX = std::max(fx, curWx);
          ann.boxMinY = std::min(fy, curWy);
          ann.boxMaxY = std::max(fy, curWy);
          ann.insX = ann.boxMinX;
          ann.insY = ann.boxMinY;
        }
      }
    }
    BumpCadGpuCache(cmd);
  }

  if (cmd.dimGripMoveActive && cmd.dimGripAnnotationIndex >= 0 && outCursorX && outCursorY && hovered &&
      mx >= 0.f && mx < avail.x && my >= 0.f && my < avail.y) {
    const float curWx = cmd.viewportSnapPickValid ? cmd.viewportSnapPickWorldX : *outCursorX;
    const float curWy = cmd.viewportSnapPickValid ? cmd.viewportSnapPickWorldY : *outCursorY;
    const size_t gi = static_cast<size_t>(cmd.dimGripAnnotationIndex);
    if (gi < cmd.cadAnnotations.size()) {
      CadAnnotation& ann = cmd.cadAnnotations[gi];
      if (ann.kind == CadAnnotation::Kind::DimAligned || ann.kind == CadAnnotation::Kind::DimLinear) {
        switch (cmd.dimGripWhich) {
        case 0:
          ann.dimExt1X = curWx;
          ann.dimExt1Y = curWy;
          CadDimAlignedApplyInsFromLocalOffset(&ann, cmd.dimGripTextAlongN, cmd.dimGripTextAlongT);
          break;
        case 1:
          ann.dimExt2X = curWx;
          ann.dimExt2Y = curWy;
          CadDimAlignedApplyInsFromLocalOffset(&ann, cmd.dimGripTextAlongN, cmd.dimGripTextAlongT);
          break;
        case 2:
        case 3:
          ann.dimSignedOffset = cmd.dimGripOrigSignedOffset + (curWx - cmd.dimGripDownWorldX) * cmd.dimGripDragNx +
                                (curWy - cmd.dimGripDownWorldY) * cmd.dimGripDragNy;
          CadDimAlignedApplyInsFromLocalOffset(&ann, cmd.dimGripTextAlongN, cmd.dimGripTextAlongT);
          break;
        case 4:
          ann.insX = cmd.dimGripOrigInsX + (curWx - cmd.dimGripDownWorldX);
          ann.insY = cmd.dimGripOrigInsY + (curWy - cmd.dimGripDownWorldY);
          break;
        default:
          break;
        }
        CadDimRefreshMeasurementText(&ann, cmd.displayLinearPrecision, CadAngleDisplaySettings(cmd));
        float sx1 = 0.f, sy1 = 0.f, sx2 = 0.f, sy2 = 0.f, tx = 0.f, ty = 0.f, nx = 0.f, ny = 0.f, ml = 0.f;
        if (CadDimAnyGeometry(ann, &sx1, &sy1, &sx2, &sy2, &tx, &ty, &nx, &ny, &ml))
          ann.rotationRad = std::atan2(ty, tx);
      }
    }
    BumpCadGpuCache(cmd);
  }

  // Entity grips: first click arms (stores originals); cursor updates geometry live; second LMB commits
  // (same pattern as dim grips). RMB / ESC restore originals.
  if (cmd.entityGripMoveActive && cmd.entityGripEntityIndex >= 0 && outCursorX && outCursorY && hovered &&
      mx >= 0.f && mx < avail.x && my >= 0.f && my < avail.y) {
    // Snap to other geometry if OSNAP fired (entity's own geometry is excluded); otherwise raw cursor.
    const float curWx = cmd.viewportSnapPickValid
        ? cmd.viewportSnapPickWorldX
        : (outCursorRawX ? static_cast<float>(*outCursorRawX) : static_cast<float>(*outCursorX));
    const float curWy = cmd.viewportSnapPickValid
        ? cmd.viewportSnapPickWorldY
        : (outCursorRawY ? static_cast<float>(*outCursorRawY) : static_cast<float>(*outCursorY));
    const int idx = cmd.entityGripEntityIndex;
    switch (cmd.entityGripType) {
      case SelectedEntity::Type::LineSeg: {
        if (idx < 0 || static_cast<size_t>(idx) * 6 + 5 >= cmd.userLinesFlat.size())
          break;
        const size_t k = static_cast<size_t>(idx) * 6;
        if (cmd.entityGripWhich == 0) {
          cmd.userLinesFlat[k] = curWx;
          cmd.userLinesFlat[k + 1] = curWy;
        } else if (cmd.entityGripWhich == 1) {
          cmd.userLinesFlat[k + 3] = curWx;
          cmd.userLinesFlat[k + 4] = curWy;
        }
        break;
      }
      case SelectedEntity::Type::Circle: {
        if (idx < 0 || static_cast<size_t>(idx) * 3 + 2 >= cmd.userCirclesCxCyR.size())
          break;
        const size_t k = static_cast<size_t>(idx) * 3;
        float& cx = cmd.userCirclesCxCyR[k];
        float& cy = cmd.userCirclesCxCyR[k + 1];
        float& r = cmd.userCirclesCxCyR[k + 2];
        if (cmd.entityGripWhich == 0) {
          cx = curWx;
          cy = curWy;
        } else if (cmd.entityGripWhich == 1) {
          r = std::hypot(curWx - cx, curWy - cy);
        }
        break;
      }
      case SelectedEntity::Type::Polyline: {
        const int np = cmd.userPolylineOffsets.size() > 0 ? static_cast<int>(cmd.userPolylineOffsets.size() - 1) : 0;
        if (idx < 0 || idx >= np)
          break;
        const int startV = cmd.userPolylineOffsets[static_cast<size_t>(idx)];
        const int viLocal = cmd.entityGripWhich;
        const int globalV = startV + viLocal;
        const size_t xIdx = static_cast<size_t>(globalV) * 3;
        if (xIdx + 1 >= cmd.userPolylineVerts.size())
          break;
        cmd.userPolylineVerts[xIdx] = curWx;
        cmd.userPolylineVerts[xIdx + 1] = curWy;
        break;
      }
      case SelectedEntity::Type::Arc: {
        if (idx < 0 || static_cast<size_t>(idx) >= cmd.userArcs.size())
          break;
        CadArc& a = cmd.userArcs[static_cast<size_t>(idx)];
        if (cmd.entityGripWhich == 0) {
          a.cx = curWx;
          a.cy = curWy;
        } else if (cmd.entityGripWhich == 1) {
          a.r = std::hypot(curWx - a.cx, curWy - a.cy);
          a.startRad = std::atan2(curWy - a.cy, curWx - a.cx);
        } else if (cmd.entityGripWhich == 2) {
          a.r = std::hypot(curWx - a.cx, curWy - a.cy);
          const float endRad = std::atan2(curWy - a.cy, curWx - a.cx);
          a.sweepRad = endRad - a.startRad;
        }
        break;
      }
      case SelectedEntity::Type::Ellipse: {
        if (idx < 0 || static_cast<size_t>(idx) >= cmd.userEllipses.size())
          break;
        CadEllipse& el = cmd.userEllipses[static_cast<size_t>(idx)];
        if (cmd.entityGripWhich == 0) {
          el.cx = curWx;
          el.cy = curWy;
        } else if (cmd.entityGripWhich == 1) {
          el.majVx = curWx - el.cx;
          el.majVy = curWy - el.cy;
        } else if (cmd.entityGripWhich == 2) {
          const float majLen2 = el.majVx * el.majVx + el.majVy * el.majVy;
          if (majLen2 < 1e-12f)
            break;
          const float dx = curWx - el.cx;
          const float dy = curWy - el.cy;
          const float perpX = -el.majVy;
          const float perpY = el.majVx;
          const float ratioNew = std::clamp((dx * perpX + dy * perpY) / majLen2, 0.f, 1.f);
          el.ratio = ratioNew;
        }
        break;
      }
      default:
        break;
    }

    BumpCadGpuCache(cmd);
  }

  if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && mx >= 0 && mx < avail.x && my >= 0 &&
      my < avail.y && cmd.entityGripMoveActive && cmd.entityGripEntityIndex >= 0) {
    const int idx = cmd.entityGripEntityIndex;
    switch (cmd.entityGripType) {
    case SelectedEntity::Type::LineSeg: {
      if (idx < 0 || static_cast<size_t>(idx) * 6 + 5 >= cmd.userLinesFlat.size())
        break;
      const size_t k = static_cast<size_t>(idx) * 6;
      cmd.userLinesFlat[k] = cmd.entityGripOrigX0;
      cmd.userLinesFlat[k + 1] = cmd.entityGripOrigY0;
      cmd.userLinesFlat[k + 3] = cmd.entityGripOrigX1;
      cmd.userLinesFlat[k + 4] = cmd.entityGripOrigY1;
      break;
    }
    case SelectedEntity::Type::Circle: {
      if (idx < 0 || static_cast<size_t>(idx) * 3 + 2 >= cmd.userCirclesCxCyR.size())
        break;
      const size_t k = static_cast<size_t>(idx) * 3;
      cmd.userCirclesCxCyR[k] = cmd.entityGripOrigCx;
      cmd.userCirclesCxCyR[k + 1] = cmd.entityGripOrigCy;
      cmd.userCirclesCxCyR[k + 2] = cmd.entityGripOrigR;
      break;
    }
    case SelectedEntity::Type::Polyline: {
      if (cmd.entityGripOrigPolylineXIdx >= 0 &&
          static_cast<size_t>(cmd.entityGripOrigPolylineXIdx + 1) < cmd.userPolylineVerts.size()) {
        cmd.userPolylineVerts[static_cast<size_t>(cmd.entityGripOrigPolylineXIdx)] = cmd.entityGripOrigPolyVertX;
        cmd.userPolylineVerts[static_cast<size_t>(cmd.entityGripOrigPolylineXIdx) + 1] = cmd.entityGripOrigPolyVertY;
      }
      break;
    }
    case SelectedEntity::Type::Arc: {
      if (idx < 0 || static_cast<size_t>(idx) >= cmd.userArcs.size())
        break;
      CadArc& a = cmd.userArcs[static_cast<size_t>(idx)];
      a.cx = cmd.entityGripOrigCx;
      a.cy = cmd.entityGripOrigCy;
      a.r = cmd.entityGripOrigR;
      a.startRad = cmd.entityGripOrigStartRad;
      a.sweepRad = cmd.entityGripOrigSweepRad;
      break;
    }
    case SelectedEntity::Type::Ellipse: {
      if (idx < 0 || static_cast<size_t>(idx) >= cmd.userEllipses.size())
        break;
      CadEllipse& el = cmd.userEllipses[static_cast<size_t>(idx)];
      el.cx = cmd.entityGripOrigEllCx;
      el.cy = cmd.entityGripOrigEllCy;
      el.majVx = cmd.entityGripOrigEllMajVx;
      el.majVy = cmd.entityGripOrigEllMajVy;
      el.ratio = cmd.entityGripOrigEllRatio;
      break;
    }
    default:
      break;
    }

    ClearEntityGripInteraction(cmd);
    BumpCadGpuCache(cmd);
  }

  if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && mx >= 0 && mx < avail.x && my >= 0 &&
      my < avail.y && cmd.mtextGripMoveActive && cmd.mtextGripAnnotationIndex >= 0) {
    AbortMtextGripInteraction(cmd);
    BumpCadGpuCache(cmd);
  }

  if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && mx >= 0 && mx < avail.x && my >= 0 &&
      my < avail.y && cmd.dimGripMoveActive && cmd.dimGripAnnotationIndex >= 0) {
    const size_t gi = static_cast<size_t>(cmd.dimGripAnnotationIndex);
    if (gi < cmd.cadAnnotations.size()) {
      CadAnnotation& ann = cmd.cadAnnotations[gi];
      if (ann.kind == CadAnnotation::Kind::DimAligned || ann.kind == CadAnnotation::Kind::DimLinear) {
        ann.dimExt1X = cmd.dimGripOrigExt1X;
        ann.dimExt1Y = cmd.dimGripOrigExt1Y;
        ann.dimExt2X = cmd.dimGripOrigExt2X;
        ann.dimExt2Y = cmd.dimGripOrigExt2Y;
        ann.dimSignedOffset = cmd.dimGripOrigSignedOffset;
        ann.insX = cmd.dimGripOrigInsX;
        ann.insY = cmd.dimGripOrigInsY;
        float sx1 = 0.f, sy1 = 0.f, sx2 = 0.f, sy2 = 0.f, tx = 0.f, ty = 0.f, nx = 0.f, ny = 0.f, ml = 0.f;
        if (CadDimAnyGeometry(ann, &sx1, &sy1, &sx2, &sy2, &tx, &ty, &nx, &ny, &ml))
          ann.rotationRad = std::atan2(ty, tx);
      }
    }
    ClearDimGripInteraction(cmd);
    BumpCadGpuCache(cmd);
  }

  const bool overCmdSugPopup =
      s_cmdSugPopupOpen && ImGui::IsMouseHoveringRect(s_cmdSugPopupMin, s_cmdSugPopupMax, false);
  if (modelSpace && hovered && !overCmdSugPopup && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && mx >= 0 &&
      mx < avail.x && my >= 0 && my < avail.y) {
    if (cmd.dimGripMoveActive) {
      cmd.dimGripMoveActive = false;
      ClearDimGripInteraction(cmd);
      BumpCadGpuCache(cmd);
    } else if (cmd.entityGripMoveActive) {
      ClearEntityGripInteraction(cmd);
      BumpCadGpuCache(cmd);
    } else if (cmd.mtextGripMoveActive) {
      // Before clearing, persist user-dragged offset for survey labels so reposition calls keep this position.
      const int gripAnnIdx = cmd.mtextGripAnnotationIndex;
      if (gripAnnIdx >= 0 && static_cast<size_t>(gripAnnIdx) < cmd.cadAnnotations.size() &&
          cmd.mtextGripCorner == 4) {
        CadAnnotation& gripAnn = cmd.cadAnnotations[static_cast<size_t>(gripAnnIdx)];
        const int spi = gripAnn.surveyPointLabelFor;
        if (spi >= 0 && static_cast<size_t>(spi) < cmd.surveyPoints.size()) {
          const SurveyPoint& sp = cmd.surveyPoints[static_cast<size_t>(spi)];
          const float newCx = 0.5f * (gripAnn.boxMinX + gripAnn.boxMaxX);
          const float newCy = 0.5f * (gripAnn.boxMinY + gripAnn.boxMaxY);
          gripAnn.surveyLabelUserOffsetEast  = newCx - sp.easting;
          gripAnn.surveyLabelUserOffsetNorth = newCy - sp.northing;
          gripAnn.surveyLabelHasUserOffset   = true;
        }
      }
      ClearMtextGripInteraction(cmd);
      BumpCadGpuCache(cmd);
    } else {
    using K = AppCommandState::Kind;
    using MP = AppCommandState::ModifyPhase;
    using RP = AppCommandState::RotatePhase;

    const bool haveSnapPick = outCursorX && outCursorY && cmd.viewportSnapPickValid;
    const float commitX = haveSnapPick ? cmd.viewportSnapPickWorldX : *outCursorX;
    const float commitY = haveSnapPick ? cmd.viewportSnapPickWorldY : *outCursorY;

    const float uPick = mx / std::max(avail.x, 1.f);
    const float vPick = my / std::max(avail.y, 1.f);
    const double rawPickX = worldLeft + static_cast<double>(uPick) * (worldRight - worldLeft);
    const double rawPickY = worldTop - static_cast<double>(vPick) * (worldTop - worldBottom);
    const float rawPickXf = static_cast<float>(rawPickX);
    const float rawPickYf = static_cast<float>(rawPickY);

    const bool useRawWorldForWindowRect = ViewportUseRawWorldForSelectionRectPick(cmd);
    const float wxPick = useRawWorldForWindowRect ? rawPickXf : *outCursorX;
    const float wyPick = useRawWorldForWindowRect ? rawPickYf : *outCursorY;
    const bool keyShift = ImGui::GetIO().KeyShift;
    constexpr float kFenceDirTolPx = 3.f;
    const float fenceDragDx = mx - cmd.selBoxAnchorScreenX;
    const bool fenceWindowMode = fenceDragDx > kFenceDirTolPx;

    if (cmd.showCreatePointsWindow && cmd.active == K::None) {
      const int hitIx =
          cmd.surveyPoints.empty()
              ? -1
              : PickSurveyPointIndex(cmd.surveyPoints, rawPickX, rawPickY, surveyCrossHalfW, avail.y, halfH,
                                     cmd.objectSnapAperturePx);
      if (hitIx >= 0) {
        ClearCadSelection(cmd);
        ApplySurveyPointClickSelection(cmd, hitIx, keyShift, &log);
        for (int svi : cmd.selectedSurveyPointIndices) {
          if (svi >= 0 && static_cast<size_t>(svi) < cmd.surveyPoints.size())
            SyncSurveyPointLinkedMtextSelection(cmd, svi);
        }
      } else {
        TryPlaceSurveyPoint(cmd, commitX, commitY, cmd.createPointsOpts.defaultElevation, log);
      }
    } else if (cmd.active == K::Offset)
      SubmitViewportPick(cmd, rawPickX, rawPickY, log);
    else if (cmd.active == K::PdfAttach &&
             cmd.pdfAttachPhase == AppCommandState::PdfAttachPhase::WaitInsertPoint)
      SubmitPdfAttachInsertPoint(cmd, commitX, commitY, log);
    else if (cmd.active == K::Align) {
      using AP = AppCommandState::AlignPhase;
      if (cmd.alignPhase == AP::PickSelection) {
        if (!cmd.selBoxWaitingSecond)
          BeginSelectionBoxCorner(cmd, wxPick, wyPick, mx, my);
        else
          SubmitViewportPick(cmd, wxPick, wyPick, log, keyShift, fenceWindowMode);
      } else {
        SubmitViewportPick(cmd, commitX, commitY, log);
      }
    }
    else if (cmd.active == K::Line || cmd.active == K::Circle || cmd.active == K::Polyline ||
             cmd.active == K::Arc || cmd.active == K::Ellipse || cmd.active == K::Text ||
             cmd.active == K::Mtext || cmd.active == K::DimAligned || cmd.active == K::DimLinear ||
             cmd.active == K::DimAngular ||
             cmd.active == K::IdPoint || cmd.active == K::SurveyInverse || cmd.active == K::Paste)
      SubmitViewportPick(cmd, commitX, commitY, log);
    else if (cmd.active == K::Move || cmd.active == K::Copy || cmd.active == K::Scale) {
      if (cmd.modifyPhase == MP::PickSelection) {
        if (!cmd.selBoxWaitingSecond)
          BeginSelectionBoxCorner(cmd, wxPick, wyPick, mx, my);
        else
          SubmitViewportPick(cmd, wxPick, wyPick, log, keyShift, fenceWindowMode);
      } else
        SubmitViewportPick(cmd, commitX, commitY, log);
    } else if (cmd.active == K::Rotate) {
      if (cmd.rotatePhase == RP::PickSelection) {
        if (!cmd.selBoxWaitingSecond)
          BeginSelectionBoxCorner(cmd, wxPick, wyPick, mx, my);
        else
          SubmitViewportPick(cmd, wxPick, wyPick, log, keyShift, fenceWindowMode);
      } else
        SubmitViewportPick(cmd, commitX, commitY, log);
    } else if (cmd.active == K::Delete) {
      if (!cmd.selBoxWaitingSecond)
        BeginSelectionBoxCorner(cmd, wxPick, wyPick, mx, my);
      else
        SubmitViewportPick(cmd, wxPick, wyPick, log, keyShift, fenceWindowMode);
    } else if (cmd.active == K::Join) {
      if (!cmd.selBoxWaitingSecond)
        BeginSelectionBoxCorner(cmd, wxPick, wyPick, mx, my);
      else
        SubmitViewportPick(cmd, wxPick, wyPick, log, keyShift, fenceWindowMode);
    } else if (cmd.active == K::Trim) {
      const float trimTol = CadSnap::WorldToleranceFromPixels(avail.y, halfH, cmd.objectSnapAperturePx);
      using TP = AppCommandState::TrimPhase;
      const bool trimCutLinePt =
          cmd.trimPhase == TP::CuttingLine_WaitP1 || cmd.trimPhase == TP::CuttingLine_WaitP2;
      const float tx = trimCutLinePt ? commitX : rawPickX;
      const float ty = trimCutLinePt ? commitY : rawPickY;
      SubmitTrimViewportPick(cmd, tx, ty, trimTol, log);
    } else if (cmd.active == K::Zoom) {
      if (!cmd.selBoxWaitingSecond)
        BeginSelectionBoxCorner(cmd, wxPick, wyPick, mx, my);
      else
        SubmitViewportPick(cmd, wxPick, wyPick, log, keyShift, fenceWindowMode);
    } else if (cmd.active == K::None) {
      bool handled = false;
      if (cmd.selBoxWaitingSecond) {
        SubmitViewportPick(cmd, wxPick, wyPick, log, keyShift, fenceWindowMode);
        for (int svi : cmd.selectedSurveyPointIndices) {
          if (svi >= 0 && static_cast<size_t>(svi) < cmd.surveyPoints.size())
            SyncSurveyPointLinkedMtextSelection(cmd, svi);
        }
        BumpCadGpuCache(cmd);
        handled = true;
      }
      int gripCorner = -1;
      int dimGripHit = -1;
      if (!handled && cmd.selection.size() == 1 && cmd.selection[0].type == SelectedEntity::Type::Annotation) {
        const int aix = cmd.selection[0].index;
        if (aix >= 0 && static_cast<size_t>(aix) < cmd.cadAnnotations.size()) {
          const CadAnnotation& can = cmd.cadAnnotations[static_cast<size_t>(aix)];
          if (can.kind == CadAnnotation::Kind::Mtext) {
            gripCorner =
                HitTestMtextGrip(mouse.x, mouse.y, imgPos, avail, static_cast<float>(worldLeft),
                                 static_cast<float>(worldRight), static_cast<float>(worldBottom),
                                 static_cast<float>(worldTop), can,
                                 10.f);
          } else if (can.kind == CadAnnotation::Kind::DimAligned || can.kind == CadAnnotation::Kind::DimLinear) {
            dimGripHit =
                HitTestDimGrip(mouse.x, mouse.y, imgPos, avail, static_cast<float>(worldLeft),
                               static_cast<float>(worldRight), static_cast<float>(worldBottom),
                               static_cast<float>(worldTop), can,
                               10.f);
          }
        }
      }
      if (dimGripHit >= 0) {
        const int aix = cmd.selection[0].index;
        CadAnnotation& ann = cmd.cadAnnotations[static_cast<size_t>(aix)];
        cmd.dimGripAnnotationIndex = aix;
        cmd.dimGripWhich = dimGripHit;
        cmd.dimGripOrigSignedOffset = ann.dimSignedOffset;
        cmd.dimGripOrigExt1X = ann.dimExt1X;
        cmd.dimGripOrigExt1Y = ann.dimExt1Y;
        cmd.dimGripOrigExt2X = ann.dimExt2X;
        cmd.dimGripOrigExt2Y = ann.dimExt2Y;
        cmd.dimGripOrigInsX = ann.insX;
        cmd.dimGripOrigInsY = ann.insY;
        float sx1 = 0.f, sy1 = 0.f, sx2 = 0.f, sy2 = 0.f, tx = 0.f, ty = 0.f, nx = 0.f, ny = 0.f, ml = 0.f;
        if (CadDimAnyGeometry(ann, &sx1, &sy1, &sx2, &sy2, &tx, &ty, &nx, &ny, &ml)) {
          cmd.dimGripDragNx = nx;
          cmd.dimGripDragNy = ny;
          const float dmx = 0.5f * (sx1 + sx2);
          const float dmy = 0.5f * (sy1 + sy2);
          cmd.dimGripTextAlongN = (ann.insX - dmx) * nx + (ann.insY - dmy) * ny;
          cmd.dimGripTextAlongT = (ann.insX - dmx) * tx + (ann.insY - dmy) * ty;
        }
        if (outCursorX && outCursorY) {
          cmd.dimGripDownWorldX = commitX;
          cmd.dimGripDownWorldY = commitY;
        } else {
          cmd.dimGripDownWorldX = wxPick;
          cmd.dimGripDownWorldY = wyPick;
        }
        cmd.dimGripMoveActive = true;
        AbortMtextGripInteraction(cmd);
        handled = true;
      } else if (gripCorner >= 0) {
        const int aix = cmd.selection[0].index;
        CadAnnotation& ann = cmd.cadAnnotations[static_cast<size_t>(aix)];
        cmd.mtextGripOrigBoxMinX = ann.boxMinX;
        cmd.mtextGripOrigBoxMaxX = ann.boxMaxX;
        cmd.mtextGripOrigBoxMinY = ann.boxMinY;
        cmd.mtextGripOrigBoxMaxY = ann.boxMaxY;
        cmd.mtextGripAnnotationIndex = aix;
        cmd.mtextGripCorner = gripCorner;
        cmd.mtextGripMoveActive = true;
        if (ann.surveyPointLabelFor >= 0 && gripCorner == 4) {
          if (outCursorX && outCursorY) {
            cmd.mtextGripDownWorldX = commitX;
            cmd.mtextGripDownWorldY = commitY;
          } else {
            cmd.mtextGripDownWorldX = wxPick;
            cmd.mtextGripDownWorldY = wyPick;
          }
        } else {
          static const int kOpp[4] = {2, 3, 0, 1};
          const int opp = kOpp[gripCorner];
          const float cx[4] = {ann.boxMinX, ann.boxMaxX, ann.boxMaxX, ann.boxMinX};
          const float cy[4] = {ann.boxMinY, ann.boxMinY, ann.boxMaxY, ann.boxMaxY};
          cmd.mtextGripFixedCornerX = cx[opp];
          cmd.mtextGripFixedCornerY = cy[opp];
        }
        ClearDimGripInteraction(cmd);
        ClearEntityGripInteraction(cmd);
        handled = true;
      }

      if (!handled && !cmd.selection.empty()) {
        const double denx = worldRight - worldLeft + 1e-12;
        const double deny = worldTop - worldBottom + 1e-12;
        auto wtsRel = [&](double wx, double wy) -> ImVec2 {
          const float u = static_cast<float>((wx - worldLeft) / denx);
          const float v = static_cast<float>((worldTop - wy) / deny);
          return ImVec2(u * avail.x, v * avail.y); // relative to image top-left
        };

        const float gripHitPx = 10.f;
        const float r2 = gripHitPx * gripHitPx;
        float bestD2 = r2;
        int bestWhich = -1;
        SelectedEntity bestSel{};

        auto tryGrip = [&](const SelectedEntity& sel, float gx, float gy, int which) {
          ImVec2 p = wtsRel(gx, gy);
          const float dx = mx - p.x, dy = my - p.y;
          const float d2 = dx * dx + dy * dy;
          if (d2 < bestD2) { bestD2 = d2; bestWhich = which; bestSel = sel; }
        };

        for (const SelectedEntity& sel : cmd.selection) {
          switch (sel.type) {
          case SelectedEntity::Type::LineSeg: {
            const size_t k = static_cast<size_t>(sel.index) * 6;
            if (k + 5 < cmd.userLinesFlat.size()) {
              tryGrip(sel, cmd.userLinesFlat[k],     cmd.userLinesFlat[k + 1], 0);
              tryGrip(sel, cmd.userLinesFlat[k + 3], cmd.userLinesFlat[k + 4], 1);
            }
            break;
          }
          case SelectedEntity::Type::Circle: {
            const size_t k = static_cast<size_t>(sel.index) * 3;
            if (k + 2 < cmd.userCirclesCxCyR.size()) {
              const float cx = cmd.userCirclesCxCyR[k];
              const float cy = cmd.userCirclesCxCyR[k + 1];
              const float r  = cmd.userCirclesCxCyR[k + 2];
              tryGrip(sel, cx,     cy, 0);
              tryGrip(sel, cx + r, cy, 1);
            }
            break;
          }
          case SelectedEntity::Type::Polyline: {
            const int np = cmd.userPolylineOffsets.size() > 0 ? static_cast<int>(cmd.userPolylineOffsets.size() - 1) : 0;
            if (sel.index >= 0 && sel.index < np) {
              const int startV = cmd.userPolylineOffsets[static_cast<size_t>(sel.index)];
              const int endV   = cmd.userPolylineOffsets[static_cast<size_t>(sel.index + 1)];
              for (int vi = 0; vi < endV - startV; ++vi) {
                const size_t xIdx = static_cast<size_t>(startV + vi) * 3;
                if (xIdx + 1 >= cmd.userPolylineVerts.size()) break;
                tryGrip(sel, cmd.userPolylineVerts[xIdx], cmd.userPolylineVerts[xIdx + 1], vi);
              }
            }
            break;
          }
          case SelectedEntity::Type::Arc: {
            if (sel.index >= 0 && static_cast<size_t>(sel.index) < cmd.userArcs.size()) {
              const CadArc& a = cmd.userArcs[static_cast<size_t>(sel.index)];
              const float endRad = a.startRad + a.sweepRad;
              tryGrip(sel, a.cx, a.cy, 0);
              tryGrip(sel, a.cx + a.r * std::cos(a.startRad), a.cy + a.r * std::sin(a.startRad), 1);
              tryGrip(sel, a.cx + a.r * std::cos(endRad),     a.cy + a.r * std::sin(endRad),     2);
            }
            break;
          }
          case SelectedEntity::Type::Ellipse: {
            if (sel.index >= 0 && static_cast<size_t>(sel.index) < cmd.userEllipses.size()) {
              const CadEllipse& el = cmd.userEllipses[static_cast<size_t>(sel.index)];
              const float perpX = -el.majVy, perpY = el.majVx;
              tryGrip(sel, el.cx,                                 el.cy,                                 0);
              tryGrip(sel, el.cx + el.majVx,                     el.cy + el.majVy,                     1);
              tryGrip(sel, el.cx + perpX * el.ratio,              el.cy + perpY * el.ratio,              2);
            }
            break;
          }
          default:
            break;
          }
        }

        if (bestWhich >= 0) {
          cmd.entityGripMoveActive = true;
          cmd.entityGripType = bestSel.type;
          cmd.entityGripEntityIndex = bestSel.index;
          cmd.entityGripWhich = bestWhich;

          // Store originals for RMB cancel.
          switch (bestSel.type) {
          case SelectedEntity::Type::LineSeg: {
            const size_t k = static_cast<size_t>(bestSel.index) * 6;
            cmd.entityGripOrigX0 = cmd.userLinesFlat[k];
            cmd.entityGripOrigY0 = cmd.userLinesFlat[k + 1];
            cmd.entityGripOrigX1 = cmd.userLinesFlat[k + 3];
            cmd.entityGripOrigY1 = cmd.userLinesFlat[k + 4];
            break;
          }
          case SelectedEntity::Type::Circle: {
            const size_t k = static_cast<size_t>(bestSel.index) * 3;
            cmd.entityGripOrigCx = cmd.userCirclesCxCyR[k];
            cmd.entityGripOrigCy = cmd.userCirclesCxCyR[k + 1];
            cmd.entityGripOrigR  = cmd.userCirclesCxCyR[k + 2];
            break;
          }
          case SelectedEntity::Type::Polyline: {
            const int startV  = cmd.userPolylineOffsets[static_cast<size_t>(bestSel.index)];
            const int globalV = startV + bestWhich;
            const size_t xIdx = static_cast<size_t>(globalV) * 3;
            cmd.entityGripOrigPolylineXIdx = static_cast<int>(xIdx);
            cmd.entityGripOrigPolyVertX    = cmd.userPolylineVerts[xIdx];
            cmd.entityGripOrigPolyVertY    = cmd.userPolylineVerts[xIdx + 1];
            break;
          }
          case SelectedEntity::Type::Arc: {
            const CadArc& a = cmd.userArcs[static_cast<size_t>(bestSel.index)];
            cmd.entityGripOrigCx       = a.cx;
            cmd.entityGripOrigCy       = a.cy;
            cmd.entityGripOrigR        = a.r;
            cmd.entityGripOrigStartRad = a.startRad;
            cmd.entityGripOrigSweepRad = a.sweepRad;
            break;
          }
          case SelectedEntity::Type::Ellipse: {
            const CadEllipse& el = cmd.userEllipses[static_cast<size_t>(bestSel.index)];
            cmd.entityGripOrigEllCx    = el.cx;
            cmd.entityGripOrigEllCy    = el.cy;
            cmd.entityGripOrigEllMajVx = el.majVx;
            cmd.entityGripOrigEllMajVy = el.majVy;
            cmd.entityGripOrigEllRatio = el.ratio;
            break;
          }
          default:
            break;
          }

          AbortMtextGripInteraction(cmd);
          ClearDimGripInteraction(cmd);
          handled = true;
        }
      }

      if (!handled && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        const int dIx = PickCadAnnotationAt(rawPickX, rawPickY, cmd, halfH, avail.y);
        if (dIx >= 0 && static_cast<size_t>(dIx) < cmd.cadAnnotations.size() &&
            cmd.cadAnnotations[static_cast<size_t>(dIx)].kind == CadAnnotation::Kind::Mtext) {
          AbortMtextGripInteraction(cmd);
          ClearDimGripInteraction(cmd);
          ClearCadSelection(cmd);
          SelectedEntity se;
          se.type = SelectedEntity::Type::Annotation;
          se.index = dIx;
          cmd.selection.push_back(se);
          EnsureAttrCounts(cmd);
          const CadAnnotation& da = cmd.cadAnnotations[static_cast<size_t>(dIx)];
          if (da.kind == CadAnnotation::Kind::Mtext && da.surveyPointLabelFor >= 0) {
            ApplyLinkedSurveyForAnnotationPick(cmd, dIx, false);
            SyncSurveyPointLinkedMtextSelection(cmd, da.surveyPointLabelFor);
          } else
            cmd.selectedSurveyPointIndices.clear();
          OpenMtextRichEditorForAnnotation(cmd, dIx, &log);
          cmd.selBoxWaitingSecond = false;
          handled = true;
        }
      }

      if (!handled) {
        const int annIx = PickCadAnnotationAt(rawPickX, rawPickY, cmd, halfH, avail.y);
        if (annIx >= 0) {
          AbortMtextGripInteraction(cmd);
          ClearDimGripInteraction(cmd);
          const CadAnnotation& pickedAnn = cmd.cadAnnotations[static_cast<size_t>(annIx)];
          const bool linkedSurvey =
              pickedAnn.kind == CadAnnotation::Kind::Mtext && pickedAnn.surveyPointLabelFor >= 0;
          SelectedEntity se;
          se.type = SelectedEntity::Type::Annotation;
          se.index = annIx;
          if (keyShift) {
            auto it = std::find_if(cmd.selection.begin(), cmd.selection.end(), [&](const SelectedEntity& x) {
              return x.type == SelectedEntity::Type::Annotation && x.index == annIx;
            });
            if (it != cmd.selection.end())
              cmd.selection.erase(it);
            else
              cmd.selection.push_back(se);
            if (linkedSurvey)
              ApplyLinkedSurveyForAnnotationPick(cmd, annIx, true);
          } else {
            ClearCadSelection(cmd);
            if (linkedSurvey)
              ApplyLinkedSurveyForAnnotationPick(cmd, annIx, false);
            else
              cmd.selectedSurveyPointIndices.clear();
            cmd.selection.push_back(se);
            if (linkedSurvey)
              SyncSurveyPointLinkedMtextSelection(cmd, pickedAnn.surveyPointLabelFor);
          }
          EnsureAttrCounts(cmd);
          cmd.selBoxWaitingSecond = false;
          handled = true;
        }
      }
      // PDF underlays are selected via the standard 2-click box selection
      // (ComputeSelectionFromRect handles PdfUnderlay hit testing).

      // Click-to-select: pick the closest CAD entity under the cursor (line, circle, arc, ellipse, polyline).
      if (!handled) {
        SelectedEntity clickHit{};
        float clickD2 = 0.f;
        const float clickTol = CadOffsetEntityPickTolWorld(cmd);
        if (PickClosestCadEntity(cmd, rawPickX, rawPickY, clickTol, &clickHit, &clickD2)) {
          AbortMtextGripInteraction(cmd);
          ClearDimGripInteraction(cmd);
          if (keyShift) {
            // Shift+click: remove entity from selection (subtractive).
            auto it = std::find_if(cmd.selection.begin(), cmd.selection.end(), [&](const SelectedEntity& x) {
              return x.type == clickHit.type && x.index == clickHit.index;
            });
            if (it != cmd.selection.end())
              cmd.selection.erase(it);
          } else {
            // Plain click: add entity to selection (additive).
            const bool alreadySelected = std::any_of(cmd.selection.begin(), cmd.selection.end(),
              [&](const SelectedEntity& x) { return x.type == clickHit.type && x.index == clickHit.index; });
            if (!alreadySelected)
              cmd.selection.push_back(clickHit);
          }
          EnsureAttrCounts(cmd);
          cmd.selBoxWaitingSecond = false;
          handled = true;
        }
      }

      if (!handled) {
        if (!cmd.surveyPoints.empty()) {
          const int hitIx = PickSurveyPointIndex(cmd.surveyPoints, rawPickX, rawPickY, surveyCrossHalfW, avail.y,
                                                 halfH, cmd.objectSnapAperturePx);
          if (hitIx >= 0) {
            ClearCadSelection(cmd);
            ApplySurveyPointClickSelection(cmd, hitIx, keyShift, &log);
            for (int svi : cmd.selectedSurveyPointIndices) {
              if (svi >= 0 && static_cast<size_t>(svi) < cmd.surveyPoints.size())
                SyncSurveyPointLinkedMtextSelection(cmd, svi);
            }
          } else if (!cmd.selBoxWaitingSecond)
            BeginSelectionBoxCorner(cmd, wxPick, wyPick, mx, my);
          else
            SubmitViewportPick(cmd, wxPick, wyPick, log, keyShift, fenceWindowMode);
        } else {
          if (!cmd.selBoxWaitingSecond)
            BeginSelectionBoxCorner(cmd, wxPick, wyPick, mx, my);
          else
            SubmitViewportPick(cmd, wxPick, wyPick, log, keyShift, fenceWindowMode);
        }
      }
    }
    }
  }

  // --- PDF overlays: insertion bounding-box preview + selection border ---
  {
    const double denx = std::max(worldRight - worldLeft, 1e-12);
    const double deny = std::max(worldTop - worldBottom, 1e-12);
    auto wts = [&](float wx, float wy) -> ImVec2 {
      const float u = static_cast<float>((static_cast<double>(wx) - worldLeft) / denx);
      const float v = static_cast<float>((worldTop - static_cast<double>(wy)) / deny);
      return {imgPos.x + u * avail.x, imgPos.y + v * avail.y};
    };
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Bounding-box ghost during WaitInsertPoint phase
    if (cmd.active == AppCommandState::Kind::PdfAttach &&
        cmd.pdfAttachPhase == AppCommandState::PdfAttachPhase::WaitInsertPoint &&
        cmd.pdfDraftCache && hovered && outCursorX && outCursorY) {
      const float pageW = PdfDraftCache_PageWidthPts(cmd.pdfDraftCache, cmd.pdfAttachSelectedPage);
      const float pageH = PdfDraftCache_PageHeightPts(cmd.pdfDraftCache, cmd.pdfAttachSelectedPage);
      if (pageW > 0.f && pageH > 0.f) {
        constexpr float kPiOv = 3.14159265f;
        const float W    = pageW * cmd.pdfAttachScale;
        const float H    = pageH * cmd.pdfAttachScale;
        const float cosR = std::cos(cmd.pdfAttachRotDeg * kPiOv / 180.f);
        const float sinR = std::sin(cmd.pdfAttachRotDeg * kPiOv / 180.f);
        const float ix   = static_cast<float>(*outCursorX);
        const float iy   = static_cast<float>(*outCursorY);
        auto cp = [&](float px, float py) -> ImVec2 {
          return wts(ix + px * cosR - py * sinR, iy + px * sinR + py * cosR);
        };
        const ImVec2 bl = cp(0, 0), br = cp(W, 0), tr = cp(W, H), tl = cp(0, H);
        const ImU32 previewCol = IM_COL32(0, 220, 100, 180);
        dl->AddLine(bl, br, previewCol, 1.5f);
        dl->AddLine(br, tr, previewCol, 1.5f);
        dl->AddLine(tr, tl, previewCol, 1.5f);
        dl->AddLine(tl, bl, previewCol, 1.5f);
      }
    }

    // Selection border for the selected PDF underlay
    {
    int selPdfBorderIdx = -1;
    for (const auto& e : cmd.selection)
      if (e.type == SelectedEntity::Type::PdfUnderlay) { selPdfBorderIdx = e.index; break; }
    if (selPdfBorderIdx >= 0 &&
        selPdfBorderIdx < static_cast<int>(cmd.pdfAttachments.size())) {
      const PdfAttachment& sa = cmd.pdfAttachments[static_cast<size_t>(selPdfBorderIdx)];
      if (sa.pageWidthPts > 0.f && sa.pageHeightPts > 0.f) {
        constexpr float kPiOv = 3.14159265f;
        const float W    = sa.pageWidthPts  * sa.scale;
        const float H    = sa.pageHeightPts * sa.scale;
        const float cosR = std::cos(sa.rotationDeg * kPiOv / 180.f);
        const float sinR = std::sin(sa.rotationDeg * kPiOv / 180.f);
        auto cp = [&](float px, float py) -> ImVec2 {
          return wts(sa.insertX + px * cosR - py * sinR,
                     sa.insertY + px * sinR + py * cosR);
        };
        const ImVec2 bl = cp(0, 0), br = cp(W, 0), tr = cp(W, H), tl = cp(0, H);
        const ImU32 selCol = IM_COL32(0, 180, 255, 220);
        dl->AddLine(bl, br, selCol, 1.5f);
        dl->AddLine(br, tr, selCol, 1.5f);
        dl->AddLine(tr, tl, selCol, 1.5f);
        dl->AddLine(tl, bl, selCol, 1.5f);
        constexpr float hSz = 4.f;
        for (const ImVec2& sc : {bl, br, tr, tl})
          dl->AddRectFilled({sc.x - hSz, sc.y - hSz}, {sc.x + hSz, sc.y + hSz}, selCol);
      }
    }
    } // selPdfBorderIdx block
  }

  // Paper space sheet outline (REQ-026). The sheet spans (0,0)..(sheetW,sheetH) in paper inches,
  // mapped through the same world→screen transform so it pans/zooms with the view.
  if (cmd.activeSpaceIndex >= 0 && cmd.activeSpaceIndex < static_cast<int>(cmd.paperLayouts.size())) {
    const PaperLayout& L = cmd.paperLayouts[static_cast<size_t>(cmd.activeSpaceIndex)];
    const double denx = worldRight - worldLeft + 1e-12;
    const double deny = worldTop - worldBottom + 1e-12;
    auto w2s = [&](float wx, float wy) {
      const float u = static_cast<float>((static_cast<double>(wx) - worldLeft) / denx);
      const float v = static_cast<float>((worldTop - static_cast<double>(wy)) / deny);
      return ImVec2(imgPos.x + u * avail.x, imgPos.y + v * avail.y);
    };
    const ImVec2 p0 = w2s(0.f, 0.f);
    const ImVec2 p1 = w2s(L.sheetWidthIn(), L.sheetHeightIn());
    const ImVec2 a(std::min(p0.x, p1.x), std::min(p0.y, p1.y));
    const ImVec2 b(std::max(p0.x, p1.x), std::max(p0.y, p1.y));
    ImDrawList* sdl = ImGui::GetWindowDrawList();
    sdl->AddRectFilled(ImVec2(a.x + 5.f, a.y + 5.f), ImVec2(b.x + 5.f, b.y + 5.f), IM_COL32(0, 0, 0, 90));  // shadow
    sdl->AddRectFilled(a, b, IM_COL32(244, 244, 244, 255));                                                 // sheet
    sdl->AddRect(a, b, IM_COL32(40, 40, 40, 255), 0.f, 0, 1.5f);                                            // border

    // Viewports (REQ-027): each shows model space clipped + scaled inside its rect. Drawn via the
    // overlay this increment; the GL-batch pass is tracked tech debt (TASK-002 §7).
    const double oX = cmd.worldDocumentOriginX;
    const double oY = cmd.worldDocumentOriginY;
    constexpr ImU32 kVpModelCol = IM_COL32(25, 25, 30, 255);
    for (int vi = 0; vi < static_cast<int>(L.viewports.size()); ++vi) {
      const Viewport& vp = L.viewports[static_cast<size_t>(vi)];
      const ImVec2 r0 = w2s(vp.paperXIn, vp.paperYIn);
      const ImVec2 r1 = w2s(vp.paperXIn + vp.paperWIn, vp.paperYIn + vp.paperHIn);
      const ImVec2 rmin(std::min(r0.x, r1.x), std::min(r0.y, r1.y));
      const ImVec2 rmax(std::max(r0.x, r1.x), std::max(r0.y, r1.y));
      // World (model, in world coords) → screen, through this viewport.
      auto m2s = [&](double wx, double wy) {
        float px = 0.f, py = 0.f;
        ModelToPaperIn(vp, wx, wy, &px, &py);
        return w2s(px, py);
      };
      const float pxPerWorld = avail.x / static_cast<float>(denx);  // screen px per paper inch (uniform)
      const float pxPerModel = pxPerWorld / vp.safeScale();
      sdl->PushClipRect(rmin, rmax, true);
      // Lines (REQ-028: skip frozen layers).
      for (size_t i = 0; i + 5 < cmd.userLinesFlat.size(); i += 6) {
        const size_t lineIdx = i / 6;
        const EntityAttributes& attr = LineAttr(cmd, static_cast<int>(lineIdx));
        if (IsLayerFrozenInViewport(vp, attr.layer))
          continue;
        const ImVec2 s0 = m2s(cmd.userLinesFlat[i] + oX, cmd.userLinesFlat[i + 1] + oY);
        const ImVec2 s1 = m2s(cmd.userLinesFlat[i + 3] + oX, cmd.userLinesFlat[i + 4] + oY);
        sdl->AddLine(s0, s1, kVpModelCol, 1.0f);
      }
      // Polylines (REQ-028: skip frozen layers).
      for (size_t pi = 0; pi < cmd.userPolylineOffsets.size(); ++pi) {
        const EntityAttributes& attr = PolylineAttr(cmd, static_cast<int>(pi));
        if (IsLayerFrozenInViewport(vp, attr.layer))
          continue;
        const int start = cmd.userPolylineOffsets[pi];
        const int end = (pi + 1 < cmd.userPolylineOffsets.size())
                            ? cmd.userPolylineOffsets[pi + 1]
                            : static_cast<int>(cmd.userPolylineVerts.size() / 3);
        ImVec2 prev{};
        bool have = false;
        for (int k = start; k < end; ++k) {
          const ImVec2 s = m2s(cmd.userPolylineVerts[static_cast<size_t>(k) * 3] + oX,
                               cmd.userPolylineVerts[static_cast<size_t>(k) * 3 + 1] + oY);
          if (have)
            sdl->AddLine(prev, s, kVpModelCol, 1.0f);
          prev = s;
          have = true;
        }
      }
      // Circles (REQ-028: skip frozen layers).
      for (size_t i = 0; i + 2 < cmd.userCirclesCxCyR.size(); i += 3) {
        const size_t circleIdx = i / 3;
        const EntityAttributes& attr = CircleAttr(cmd, static_cast<int>(circleIdx));
        if (IsLayerFrozenInViewport(vp, attr.layer))
          continue;
        const ImVec2 c = m2s(cmd.userCirclesCxCyR[i] + oX, cmd.userCirclesCxCyR[i + 1] + oY);
        const float rPx = cmd.userCirclesCxCyR[i + 2] * pxPerModel;
        if (rPx >= 0.5f)
          sdl->AddCircle(c, rPx, kVpModelCol, 0, 1.0f);
      }
      // Arcs (REQ-028: skip frozen layers, sampled).
      for (size_t arcIdx = 0; arcIdx < cmd.userArcs.size(); ++arcIdx) {
        const CadArc& arc = cmd.userArcs[arcIdx];
        const EntityAttributes& attr = ArcAttr(cmd, static_cast<int>(arcIdx));
        if (IsLayerFrozenInViewport(vp, attr.layer))
          continue;
        const int segs = std::clamp(static_cast<int>(std::fabs(arc.sweepRad) / 0.15f) + 2, 2, 180);
        ImVec2 prev{};
        for (int k = 0; k <= segs; ++k) {
          const float t = arc.startRad + arc.sweepRad * (static_cast<float>(k) / static_cast<float>(segs));
          const ImVec2 s = m2s(static_cast<double>(arc.cx + arc.r * std::cos(t)) + oX,
                               static_cast<double>(arc.cy + arc.r * std::sin(t)) + oY);
          if (k > 0)
            sdl->AddLine(prev, s, kVpModelCol, 1.0f);
          prev = s;
        }
      }
      // Survey-point crosses (REQ-028: skip frozen layers).
      const float crossPx = 4.f;
      for (const SurveyPoint& sp : cmd.surveyPoints) {
        if (IsLayerFrozenInViewport(vp, sp.layer))
          continue;
        const ImVec2 c = m2s(static_cast<double>(sp.easting) + oX, static_cast<double>(sp.northing) + oY);
        sdl->AddLine(ImVec2(c.x - crossPx, c.y), ImVec2(c.x + crossPx, c.y), kVpModelCol, 1.0f);
        sdl->AddLine(ImVec2(c.x, c.y - crossPx), ImVec2(c.x, c.y + crossPx), kVpModelCol, 1.0f);
      }
      sdl->PopClipRect();
      // Viewport border; selected ones accented. The active floating viewport (REQ-036) is green.
      const bool selVp = IsViewportSelected(cmd, vi);
      const bool floatVp = InFloatingModelSpace(cmd) && cmd.floatingViewportLayout == cmd.activeSpaceIndex &&
                           vi == cmd.floatingViewportIndex;
      sdl->AddRect(rmin, rmax,
                   floatVp ? IM_COL32(90, 220, 120, 255)
                           : (selVp ? IM_COL32(59, 130, 246, 255) : IM_COL32(90, 90, 100, 255)),
                   0.f, 0, (floatVp || selVp) ? 2.0f : 1.2f);
      if (selVp && !floatVp && cmd.selectedViewports.size() == 1) {
        const ImVec2 corners[4] = {rmin, ImVec2(rmax.x, rmin.y), rmax, ImVec2(rmin.x, rmax.y)};
        for (const ImVec2& cp : corners)
          sdl->AddRectFilled(ImVec2(cp.x - 4.f, cp.y - 4.f), ImVec2(cp.x + 4.f, cp.y + 4.f),
                             IM_COL32(59, 130, 246, 255));
        const ImVec2 ctr((rmin.x + rmax.x) * 0.5f, (rmin.y + rmax.y) * 0.5f);  // center = move grip
        sdl->AddRectFilled(ImVec2(ctr.x - 4.f, ctr.y - 4.f), ImVec2(ctr.x + 4.f, ctr.y + 4.f),
                           IM_COL32(245, 200, 70, 255));
      }
    }
    const float curPX = static_cast<float>(worldLeft + (mx / std::max(avail.x, 1.f)) * (worldRight - worldLeft));
    const float curPY = static_cast<float>(worldTop - (my / std::max(avail.y, 1.f)) * (worldTop - worldBottom));
    // Rectangular-viewport rubber-band preview (REQ-033) between the first click and the cursor.
    if (cmd.active == AppCommandState::Kind::PaperRectViewport && cmd.paperVpPhase == 1 && hovered) {
      const ImVec2 q0 = w2s(cmd.paperVpFirstXIn, cmd.paperVpFirstYIn);
      const ImVec2 q1 = w2s(curPX, curPY);
      sdl->AddRect(ImVec2(std::min(q0.x, q1.x), std::min(q0.y, q1.y)),
                   ImVec2(std::max(q0.x, q1.x), std::max(q0.y, q1.y)), IM_COL32(59, 130, 246, 220), 0.f, 0, 1.5f);
    }
    // MOVE/COPY ghost preview (REQ-035): selected viewports translated by (cursor − base).
    if (cmd.paperMovePhase == 2 && hovered) {
      const float dxIn = curPX - cmd.paperMoveBaseXIn;
      const float dyIn = curPY - cmd.paperMoveBaseYIn;
      for (int sv : cmd.selectedViewports) {
        if (sv < 0 || sv >= static_cast<int>(L.viewports.size()))
          continue;
        const Viewport& v = L.viewports[static_cast<size_t>(sv)];
        const ImVec2 g0 = w2s(v.paperXIn + dxIn, v.paperYIn + dyIn);
        const ImVec2 g1 = w2s(v.paperXIn + v.paperWIn + dxIn, v.paperYIn + v.paperHIn + dyIn);
        sdl->AddRect(ImVec2(std::min(g0.x, g1.x), std::min(g0.y, g1.y)),
                     ImVec2(std::max(g0.x, g1.x), std::max(g0.y, g1.y)),
                     cmd.paperMoveIsCopy ? IM_COL32(120, 220, 120, 220) : IM_COL32(245, 200, 70, 220), 0.f, 0, 1.5f);
      }
    }
    // Window-select box (REQ-035): blue (window, left→right) or green (crossing, right→left), like geometry.
    if (cmd.paperSelBoxActive && hovered) {
      const ImVec2 s0 = w2s(cmd.paperSelBoxX0In, cmd.paperSelBoxY0In);
      const ImVec2 s1 = w2s(curPX, curPY);
      const ImVec2 a2(std::min(s0.x, s1.x), std::min(s0.y, s1.y));
      const ImVec2 b2(std::max(s0.x, s1.x), std::max(s0.y, s1.y));
      const bool windowMode = curPX >= cmd.paperSelBoxX0In;
      const ImU32 fill = windowMode ? IM_COL32(59, 130, 246, 40) : IM_COL32(90, 220, 120, 40);
      const ImU32 edge = windowMode ? IM_COL32(59, 130, 246, 200) : IM_COL32(90, 220, 120, 220);
      sdl->AddRectFilled(a2, b2, fill);
      sdl->AddRect(a2, b2, edge, 0.f, 0, 1.0f);
    }
    // Floating model space (REQ-036): in-place model cursor + LINE/POLYLINE rubber inside the viewport.
    if (InFloatingModelSpace(cmd) && cmd.floatingViewportLayout == cmd.activeSpaceIndex &&
        cmd.floatingViewportIndex >= 0 && cmd.floatingViewportIndex < static_cast<int>(L.viewports.size()) &&
        hovered) {
      const Viewport& fv = L.viewports[static_cast<size_t>(cmd.floatingViewportIndex)];
      const float vcx = fv.paperXIn + fv.paperWIn * 0.5f;
      const float vcy = fv.paperYIn + fv.paperHIn * 0.5f;
      const float s = fv.safeScale();
      auto mlToScreen = [&](float lx, float ly) {
        float pIx = 0.f, pIy = 0.f;
        ModelToPaperIn(fv, static_cast<double>(lx) + cmd.worldDocumentOriginX,
                       static_cast<double>(ly) + cmd.worldDocumentOriginY, &pIx, &pIy);
        return w2s(pIx, pIy);
      };
      const float curLX = static_cast<float>((fv.modelCenterX + static_cast<double>(curPX - vcx) * s) -
                                             cmd.worldDocumentOriginX);
      const float curLY = static_cast<float>((fv.modelCenterY + static_cast<double>(curPY - vcy) * s) -
                                             cmd.worldDocumentOriginY);
      const ImVec2 r0 = w2s(fv.paperXIn, fv.paperYIn);
      const ImVec2 r1 = w2s(fv.paperXIn + fv.paperWIn, fv.paperYIn + fv.paperHIn);
      sdl->PushClipRect(ImVec2(std::min(r0.x, r1.x), std::min(r0.y, r1.y)),
                        ImVec2(std::max(r0.x, r1.x), std::max(r0.y, r1.y)), true);
      const bool lineRubber = cmd.active == AppCommandState::Kind::Line &&
                              cmd.linePhase == AppCommandState::LinePhase::NeedNextPoint;
      const bool plineRubber = cmd.active == AppCommandState::Kind::Polyline &&
                               cmd.polylinePhase == AppCommandState::PolylinePhase::NeedNextPoint;
      if (lineRubber || plineRubber)
        sdl->AddLine(mlToScreen(cmd.anchorX, cmd.anchorY), mlToScreen(curLX, curLY),
                     IM_COL32(90, 220, 120, 230), 1.5f);
      const ImVec2 cc = mlToScreen(curLX, curLY);  // model cursor crosshair
      sdl->AddLine(ImVec2(cc.x - 7.f, cc.y), ImVec2(cc.x + 7.f, cc.y), IM_COL32(90, 220, 120, 210), 1.f);
      sdl->AddLine(ImVec2(cc.x, cc.y - 7.f), ImVec2(cc.x, cc.y + 7.f), IM_COL32(90, 220, 120, 210), 1.f);
      sdl->PopClipRect();
    }
  }

  // Floating model space (REQ-036): a banner so the in-place edit mode is obvious (the active viewport is
  // outlined in green).
  if (InFloatingModelSpace(cmd)) {
    ImDrawList* bdl = ImGui::GetWindowDrawList();
    char msg[128];
    std::snprintf(msg, sizeof(msg),
                  "FLOATING MODEL SPACE — editing Viewport %d in place   (Esc / FLOAT button / PSPACE to return)",
                  cmd.floatingViewportIndex + 1);
    const ImVec2 ts = ImGui::CalcTextSize(msg);
    const float pad = 8.f;
    const ImVec2 bmin(imgPos.x + (avail.x - ts.x) * 0.5f - pad, imgPos.y + 6.f);
    const ImVec2 bmax(bmin.x + ts.x + pad * 2.f, bmin.y + ts.y + pad);
    bdl->AddRectFilled(bmin, bmax, IM_COL32(30, 80, 50, 225), 4.f);
    bdl->AddRect(bmin, bmax, IM_COL32(120, 220, 150, 255), 4.f);
    bdl->AddText(ImVec2(bmin.x + pad, bmin.y + pad * 0.5f), IM_COL32(225, 245, 230, 255), msg);
  }

  std::vector<CadAnnotation> transformAnnPreviews;
  if (outCursorX && outCursorY)
    CadAnnotationCollectTransformPreviews(cmd, *outCursorX, *outCursorY, &transformAnnPreviews);

  using AK = AppCommandState::Kind;
  using AMP = AppCommandState::MtextPhase;
  using ADP = AppCommandState::DimPhase;
  const bool showMtextCmdDraft =
      cmd.active == AK::Mtext && cmd.mtextPhase == AMP::WaitString && !cmd.mtextRichEditorOpen;
  const bool showDimCmdDraft =
      (cmd.active == AK::DimAligned || cmd.active == AK::DimLinear) && cmd.dimPhase == ADP::WaitDimLinePt &&
      outCursorX && outCursorY;

  if (!cmd.cadAnnotations.empty() || !transformAnnPreviews.empty() || showMtextCmdDraft || showDimCmdDraft) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    auto worldToScreen = [&](float wx, float wy, ImVec2* out) {
      const double denx = worldRight - worldLeft + 1e-12;
      const double deny = worldTop - worldBottom + 1e-12;
      const float u = static_cast<float>((static_cast<double>(wx) - worldLeft) / denx);
      const float v = static_cast<float>((worldTop - static_cast<double>(wy)) / deny);
      out->x = imgPos.x + u * avail.x;
      out->y = imgPos.y + v * avail.y;
    };
    const float worldPerPxY = static_cast<float>((worldTop - worldBottom) / static_cast<double>(std::max(avail.y, 1.f)));
    constexpr ImU32 kAnnCol = IM_COL32(230, 232, 238, 255);
    constexpr ImU32 kAnnTfPrevCol = IM_COL32(160, 220, 255, 130);
    constexpr ImU32 kMtextDraftCol = IM_COL32(210, 200, 140, 200);
    constexpr ImU32 kAnnSelCol = IM_COL32(120, 200, 255, 255);
    constexpr ImU32 kGripFill = IM_COL32(59, 130, 246, 255);
    constexpr ImU32 kGripBorder = IM_COL32(30, 64, 175, 255);
    ImFont* font = ImGui::GetFont();

    auto drawAnnotationVisual = [&](const CadAnnotation& a, const EntityAttributes* attrPtr, ImU32 colFallback) {
      const float hWorld = CadAnnotationHeightWorld(a, cmd.modelUnitsPerPlottedInch);
      if (a.kind == CadAnnotation::Kind::Text) {
        ImVec2 sp{};
        worldToScreen(a.insX, a.insY, &sp);
        const float fontPx =
            std::clamp(hWorld / std::max(worldPerPxY, 1.e-6f), cmd.viewportTextMinPx, cmd.viewportTextMaxPx);
        float rgba[4];
        if (attrPtr)
          ResolveEntityColorForViewport(*attrPtr, 230 / 255.f, 232 / 255.f, 238 / 255.f, rgba);
        else {
          rgba[0] = 0.9f;
          rgba[1] = 0.91f;
          rgba[2] = 0.93f;
          rgba[3] = 1.f;
        }
        const ImU32 col = IM_COL32(static_cast<int>(rgba[0] * 255.f), static_cast<int>(rgba[1] * 255.f),
                                   static_cast<int>(rgba[2] * 255.f), static_cast<int>(rgba[3] * 255.f));
        dl->AddText(font, fontPx, sp, col, a.text.c_str());
      } else if (a.kind == CadAnnotation::Kind::DimAligned || a.kind == CadAnnotation::Kind::DimLinear) {
        float sx1 = 0.f, sy1 = 0.f, sx2 = 0.f, sy2 = 0.f, tx = 0.f, ty = 0.f, nx = 0.f, ny = 0.f, meas = 0.f;
        if (!CadDimAnyGeometry(a, &sx1, &sy1, &sx2, &sy2, &tx, &ty, &nx, &ny, &meas))
          return;
        float rgba[4];
        if (attrPtr)
          ResolveEntityColorForViewport(*attrPtr, 225 / 255.f, 177 / 255.f, 44 / 255.f, rgba);
        else {
          rgba[0] = 0.9f;
          rgba[1] = 0.72f;
          rgba[2] = 0.25f;
          rgba[3] = 1.f;
        }
        const ImU32 lineCol = IM_COL32(static_cast<int>(rgba[0] * 255.f), static_cast<int>(rgba[1] * 255.f),
                                       static_cast<int>(rgba[2] * 255.f), static_cast<int>(rgba[3] * 255.f));
        constexpr ImU32 kDimTextCol = IM_COL32(248, 250, 252, 255);
        const float denx = worldRight - worldLeft + 1.e-12f;
        const float deny = worldTop - worldBottom + 1.e-12f;
        auto ws = [&](float wx, float wy, ImVec2* o) {
          const float u = (wx - worldLeft) / denx;
          const float v = (worldTop - wy) / deny;
          o->x = imgPos.x + u * avail.x;
          o->y = imgPos.y + v * avail.y;
        };
        const float fontPx =
            std::clamp(hWorld / std::max(worldPerPxY, 1.e-6f), cmd.viewportDimTextMinPx, cmd.viewportDimTextMaxPx);
        const float extPx = std::clamp(cmd.viewportDimExtLinePx, 0.25f, 16.f);
        const float dimLnPx = std::clamp(cmd.viewportDimDimLinePx, 0.25f, 16.f);
        const float gap = std::clamp(0.012f * meas, 1.e-5f * meas, 0.12f * meas);
        const float over = std::clamp(0.02f * meas, 1.e-5f * meas, 0.1f * meas);
        const float leg1 = std::hypot(sx1 - a.dimExt1X, sy1 - a.dimExt1Y);
        const float u1 = leg1 > 1.e-8f ? gap / leg1 : 0.f;
        const float ex1 = a.dimExt1X + (sx1 - a.dimExt1X) * u1;
        const float ey1 = a.dimExt1Y + (sy1 - a.dimExt1Y) * u1;
        const float leg2 = std::hypot(sx2 - a.dimExt2X, sy2 - a.dimExt2Y);
        const float u2 = leg2 > 1.e-8f ? gap / leg2 : 0.f;
        const float ex2 = a.dimExt2X + (sx2 - a.dimExt2X) * u2;
        const float ey2 = a.dimExt2Y + (sy2 - a.dimExt2Y) * u2;
        ImVec2 A{}, B{};
        ws(ex1, ey1, &A);
        ws(sx1 + nx * over, sy1 + ny * over, &B);
        dl->AddLine(A, B, lineCol, extPx);
        ws(ex2, ey2, &A);
        ws(sx2 + nx * over, sy2 + ny * over, &B);
        dl->AddLine(A, B, lineCol, extPx);
        // Arrow length in world units tracks annotation height (drawing scale); tiny floor from meas for readability.
        const float alenW =
            std::max(cmd.viewportDimArrowScale * 0.32f * hWorld, cmd.viewportDimArrowScale * 0.012f * meas);
        const float dlen = std::hypot(sx2 - sx1, sy2 - sy1);
        if (dlen > 1.e-6f) {
          const float ux = (sx2 - sx1) / dlen;
          const float uy = (sy2 - sy1) / dlen;
          const float tipInset =
              std::clamp(0.18f * alenW, 1.e-7f * meas, std::max(1.e-6f, 0.22f * dlen));
          const float maxAlen = 0.47f * std::max(0.f, dlen - 2.f * tipInset);
          const float alenUse = std::max(1.e-6f, std::min(alenW, maxAlen));
          // Tips sit near the extension intersections, pointing inward along the dimension line (CAD default).
          const float tip1x = sx1 + ux * tipInset;
          const float tip1y = sy1 + uy * tipInset;
          const float tip2x = sx2 - ux * tipInset;
          const float tip2y = sy2 - uy * tipInset;
          const float base1x = tip1x + ux * alenUse;
          const float base1y = tip1y + uy * alenUse;
          const float base2x = tip2x - ux * alenUse;
          const float base2y = tip2y - uy * alenUse;
          if (std::hypot(base2x - base1x, base2y - base1y) > 1.e-5f) {
            ws(base1x, base1y, &A);
            ws(base2x, base2y, &B);
            dl->AddLine(A, B, lineCol, dimLnPx);
          }
          const float hw = alenUse * 0.48f;
          const float ox = -uy * hw;
          const float oy = ux * hw;
          ImVec2 t0{}, t1{}, t2{};
          ws(tip1x, tip1y, &t0);
          ws(base1x + ox, base1y + oy, &t1);
          ws(base1x - ox, base1y - oy, &t2);
          dl->AddTriangleFilled(t0, t1, t2, lineCol);
          ws(tip2x, tip2y, &t0);
          ws(base2x + ox, base2y + oy, &t1);
          ws(base2x - ox, base2y - oy, &t2);
          dl->AddTriangleFilled(t0, t1, t2, lineCol);
        }
        ImVec2 sp{};
        ws(a.insX, a.insY, &sp);
        const float dsx = std::cos(a.rotationRad) * avail.x / denx;
        const float dsy = -std::sin(a.rotationRad) * avail.y / deny;
        const float screenAng = std::atan2(dsy, dsx);
        AddAlignedDimText(dl, font, fontPx, sp, screenAng, kDimTextCol, a.text.c_str());
      } else {
        ImVec2 sa{}, sb{};
        worldToScreen(a.boxMinX, a.boxMinY, &sa);
        worldToScreen(a.boxMaxX, a.boxMaxY, &sb);
        const float rx0 = std::min(sa.x, sb.x);
        const float ry0 = std::min(sa.y, sb.y);
        const float rx1 = std::max(sa.x, sb.x);
        const float ry1 = std::max(sa.y, sb.y);
        const float fontPx =
            std::clamp(hWorld / std::max(worldPerPxY, 1.e-6f), cmd.viewportMtextMinPx, cmd.viewportMtextMaxPx);
        ImU32 col = colFallback;
        if (attrPtr) {
          float rgba[4];
          ResolveEntityColorForViewport(*attrPtr, 230 / 255.f, 232 / 255.f, 238 / 255.f, rgba);
          col = IM_COL32(static_cast<int>(rgba[0] * 255.f), static_cast<int>(rgba[1] * 255.f),
                         static_cast<int>(rgba[2] * 255.f), static_cast<int>(rgba[3] * 255.f));
        }
        float drawX = rx0 + 4.f;
        float drawY = ry0 + 4.f;
        float wrapW = std::max(8.f, rx1 - rx0 - 8.f);
        if (a.surveyPointLabelFor >= 0) {
          float pw = 8.f;
          float ph = fontPx * 1.22f;
          MtextRichNaturalContentPx(font, fontPx, a.text, &pw, &ph);
          drawX = rx0 + 0.5f * ((rx1 - rx0) - pw);
          drawY = ry0 + 0.5f * ((ry1 - ry0) - ph);
          wrapW = std::max(pw, 8.f);

          // Draw leader line from label to point when label is manually offset far enough.
          if (a.surveyLabelHasUserOffset &&
              static_cast<size_t>(a.surveyPointLabelFor) < cmd.surveyPoints.size()) {
            const SurveyPoint& lsp = cmd.surveyPoints[static_cast<size_t>(a.surveyPointLabelFor)];
            const float lcx = 0.5f * (a.boxMinX + a.boxMaxX);
            const float lcy = 0.5f * (a.boxMinY + a.boxMaxY);
            const float bwHalf = 0.5f * std::fabs(a.boxMaxX - a.boxMinX);
            const float bhHalf = 0.5f * std::fabs(a.boxMaxY - a.boxMinY);
            const float halfDiag = std::hypot(bwHalf, bhHalf);
            const float distToPoint = std::hypot(lsp.easting - lcx, lsp.northing - lcy);
            if (distToPoint > halfDiag * 1.1f) {
              ImVec2 ptScreen{};
              worldToScreen(lsp.easting, lsp.northing, &ptScreen);
              const float cx_s = 0.5f * (rx0 + rx1);
              const float cy_s = 0.5f * (ry0 + ry1);
              // Direction from label to point in screen space.
              const float ldx = ptScreen.x - cx_s;
              const float ldy = ptScreen.y - cy_s;
              const float ldist = std::hypot(ldx, ldy);
              if (ldist > 1.f) {
                const float udx = ldx / ldist;
                const float udy = ldy / ldist;
                // Clip the line start to the label box edge.
                const float halfBoxPxW = 0.5f * (rx1 - rx0);
                const float halfBoxPxH = 0.5f * (ry1 - ry0);
                // Parametric distance to box edge along direction (udx, udy).
                const float tEdge = (std::fabs(udx) > 1e-5f && std::fabs(udy) > 1e-5f)
                    ? std::min(halfBoxPxW / std::fabs(udx), halfBoxPxH / std::fabs(udy))
                    : (std::fabs(udx) > 1e-5f ? halfBoxPxW / std::fabs(udx) : halfBoxPxH / std::fabs(udy));
                const float lineStartX = cx_s + udx * tEdge;
                const float lineStartY = cy_s + udy * tEdge;
                // Survey orange, fully opaque leader.
                const ImU32 leaderCol  = IM_COL32(249, 115, 22, 220);
                const ImU32 leaderShadow = IM_COL32(0, 0, 0, 120);
                // Thin dark shadow under the line for contrast on dark backgrounds.
                dl->AddLine(ImVec2(lineStartX + 1.f, lineStartY + 1.f),
                            ImVec2(ptScreen.x  + 1.f, ptScreen.y  + 1.f),
                            leaderShadow, 2.0f);
                dl->AddLine(ImVec2(lineStartX, lineStartY), ptScreen, leaderCol, 1.5f);
                // Arrowhead at the point end (aLen derived from half-width to keep a fixed aspect).
                const float aHalf = cmd.surveyLabelLeaderArrowPx;
                const float aLen  = aHalf * 2.36f;
                const float bx = ptScreen.x - udx * aLen;
                const float by = ptScreen.y - udy * aLen;
                dl->AddTriangleFilled(
                    ptScreen,
                    ImVec2(bx - udy * aHalf, by + udx * aHalf),
                    ImVec2(bx + udy * aHalf, by - udx * aHalf),
                    leaderCol);
              }
            }
          }
        }
        dl->PushClipRect(ImVec2(rx0, ry0), ImVec2(rx1, ry1), true);
        MtextRichDrawWrapped(dl, font, fontPx, ImVec2(drawX, drawY), wrapW, col, a.text);
        dl->PopClipRect();
      }
    };

    auto isAnnSelected = [&](size_t ix) {
      for (const auto& e : cmd.selection) {
        if (e.type == SelectedEntity::Type::Annotation && static_cast<size_t>(e.index) == ix)
          return true;
      }
      return false;
    };

    for (size_t ai = 0; ai < cmd.cadAnnotations.size(); ++ai) {
      const EntityAttributes* ap =
          ai < cmd.cadAnnotationAttrs.size() ? &cmd.cadAnnotationAttrs[ai] : nullptr;
      drawAnnotationVisual(cmd.cadAnnotations[ai], ap, kAnnCol);
    }

    for (const CadAnnotation& ap : transformAnnPreviews)
      drawAnnotationVisual(ap, nullptr, kAnnTfPrevCol);

    if (showMtextCmdDraft) {
      CadAnnotation d{};
      d.kind = CadAnnotation::Kind::Mtext;
      d.plottedHeightInches = std::max(cmd.defaultPlottedTextHeightInches, 1.e-6f);
      d.text = "MTEXT";
      d.boxMinX = std::min(cmd.mtxtX1, cmd.mtxtX2);
      d.boxMaxX = std::max(cmd.mtxtX1, cmd.mtxtX2);
      d.boxMinY = std::min(cmd.mtxtY1, cmd.mtxtY2);
      d.boxMaxY = std::max(cmd.mtxtY1, cmd.mtxtY2);
      d.insX = d.boxMinX;
      d.insY = d.boxMinY;
      drawAnnotationVisual(d, nullptr, kMtextDraftCol);
    }

    if (showDimCmdDraft) {
      CadAnnotation d{};
      const bool ok = cmd.active == AK::DimLinear
                          ? CadDimLinearBuildDraft(cmd, *outCursorX, *outCursorY, &d)
                          : CadDimAlignedBuildDraft(cmd, *outCursorX, *outCursorY, &d);
      if (ok)
        drawAnnotationVisual(d, nullptr, kAnnTfPrevCol);
    }

    const float gripHalf = cmd.gripSizePx;
    for (size_t ai = 0; ai < cmd.cadAnnotations.size(); ++ai) {
      const CadAnnotation& a = cmd.cadAnnotations[ai];
      if (a.kind != CadAnnotation::Kind::Mtext || !isAnnSelected(ai))
        continue;
      // Survey-linked labels: grips only (no selection rectangle).
      if (a.surveyPointLabelFor < 0) {
        ImVec2 sa{}, sb{};
        worldToScreen(a.boxMinX, a.boxMinY, &sa);
        worldToScreen(a.boxMaxX, a.boxMaxY, &sb);
        const float rx0 = std::min(sa.x, sb.x);
        const float ry0 = std::min(sa.y, sb.y);
        const float rx1 = std::max(sa.x, sb.x);
        const float ry1 = std::max(sa.y, sb.y);
        dl->AddRect(ImVec2(rx0, ry0), ImVec2(rx1, ry1), kAnnSelCol, 0.f, 0, 2.f);
      }
      if (a.surveyPointLabelFor >= 0) {
        const float cx = 0.5f * (a.boxMinX + a.boxMaxX);
        const float cy = 0.5f * (a.boxMinY + a.boxMaxY);
        ImVec2 gp{};
        worldToScreen(cx, cy, &gp);
        dl->AddRectFilled(ImVec2(gp.x - gripHalf, gp.y - gripHalf), ImVec2(gp.x + gripHalf, gp.y + gripHalf),
                          kGripFill);
        dl->AddRect(ImVec2(gp.x - gripHalf, gp.y - gripHalf), ImVec2(gp.x + gripHalf, gp.y + gripHalf), kGripBorder,
                    0.f, 0, 1.f);
      } else {
        const float wx[4] = {a.boxMinX, a.boxMaxX, a.boxMaxX, a.boxMinX};
        const float wy[4] = {a.boxMinY, a.boxMinY, a.boxMaxY, a.boxMaxY};
        for (int c = 0; c < 4; ++c) {
          ImVec2 gp{};
          worldToScreen(wx[c], wy[c], &gp);
          dl->AddRectFilled(ImVec2(gp.x - gripHalf, gp.y - gripHalf), ImVec2(gp.x + gripHalf, gp.y + gripHalf),
                            kGripFill);
          dl->AddRect(ImVec2(gp.x - gripHalf, gp.y - gripHalf), ImVec2(gp.x + gripHalf, gp.y + gripHalf), kGripBorder,
                      0.f, 0, 1.f);
        }
      }
    }

    for (size_t ai = 0; ai < cmd.cadAnnotations.size(); ++ai) {
      const CadAnnotation& a = cmd.cadAnnotations[ai];
      if ((a.kind != CadAnnotation::Kind::DimAligned && a.kind != CadAnnotation::Kind::DimLinear) ||
          !isAnnSelected(ai))
        continue;
      float sx1 = 0.f, sy1 = 0.f, sx2 = 0.f, sy2 = 0.f, tx = 0.f, ty = 0.f, nx = 0.f, ny = 0.f, meas = 0.f;
      if (!CadDimAnyGeometry(a, &sx1, &sy1, &sx2, &sy2, &tx, &ty, &nx, &ny, &meas))
        continue;
      const float wx[5] = {a.dimExt1X, a.dimExt2X, sx1, sx2, a.insX};
      const float wy[5] = {a.dimExt1Y, a.dimExt2Y, sy1, sy2, a.insY};
      for (int c = 0; c < 5; ++c) {
        ImVec2 gp{};
        worldToScreen(wx[c], wy[c], &gp);
        dl->AddRectFilled(ImVec2(gp.x - gripHalf, gp.y - gripHalf), ImVec2(gp.x + gripHalf, gp.y + gripHalf),
                          kGripFill);
        dl->AddRect(ImVec2(gp.x - gripHalf, gp.y - gripHalf), ImVec2(gp.x + gripHalf, gp.y + gripHalf), kGripBorder,
                    0.f, 0, 1.f);
      }
    }
  }

  if (cmd.viewportHoverSurveyPointIndex >= 0 &&
      static_cast<size_t>(cmd.viewportHoverSurveyPointIndex) < cmd.surveyPoints.size()) {
    const SurveyPoint& hp = cmd.surveyPoints[static_cast<size_t>(cmd.viewportHoverSurveyPointIndex)];
    ImDrawList* dlHov = ImGui::GetWindowDrawList();
    const float denxH = worldRight - worldLeft + 1e-12f;
    const float denyH = worldTop - worldBottom + 1e-12f;
    const float uH = (hp.easting - worldLeft) / denxH;
    const float vH = (worldTop - hp.northing) / denyH;
    const ImVec2 cH(imgPos.x + uH * avail.x, imgPos.y + vH * avail.y);
    const float worldPerPxYH = (worldTop - worldBottom) / std::max(avail.y, 1.f);
    const float armH =
        SurveyPointCrossHalfWorldFromPaper(cmd.surveyPointCrossSpanPlottedInches, cmd.modelUnitsPerPlottedInch);
    const float tolH = CadSnap::WorldToleranceFromPixels(avail.y, halfH, cmd.objectSnapAperturePx);
    const float rWorldH = std::max(armH, tolH) * 1.38f;
    const float rPxH = rWorldH / std::max(worldPerPxYH, 1e-6f);
    dlHov->AddCircle(cH, std::max(rPxH, 4.f), IM_COL32(100, 215, 255, 230), 48, 2.25f);
  }

  if (!cmd.surveyPoints.empty() && cmd.surveyPointShowIdInViewport) {
    ImDrawList* dlS = ImGui::GetWindowDrawList();
    const float worldPerPxYL = (worldTop - worldBottom) / std::max(avail.y, 1.f);
    const float hWorldL =
        cmd.surveyPointLabelPlottedHeightInches * std::max(cmd.modelUnitsPerPlottedInch, 1.e-6f);
    const float fontPxL =
        std::clamp(hWorldL / std::max(worldPerPxYL, 1.e-6f), cmd.viewportTextMinPx, cmd.viewportTextMaxPx);
    ImFont* fontL = ImGui::GetFont();
    constexpr ImU32 kPtIdCol = IM_COL32(255, 248, 200, 255);
    auto wts = [&](float wx, float wy, ImVec2* o) {
      const float denx = worldRight - worldLeft + 1e-12f;
      const float deny = worldTop - worldBottom + 1e-12f;
      const float u = (wx - worldLeft) / denx;
      const float v = (worldTop - wy) / deny;
      o->x = imgPos.x + u * avail.x;
      o->y = imgPos.y + v * avail.y;
    };
    for (const auto& p : cmd.surveyPoints) {
      ImVec2 sp{};
      wts(p.easting, p.northing, &sp);
      char idb[32];
      std::snprintf(idb, sizeof(idb), "%d", p.id);
      dlS->AddText(fontL, fontPxL, ImVec2(sp.x + 6.f, sp.y - fontPxL * 0.35f), kPtIdCol, idb);
    }
  }

  // --- CAD ENTITY GRIPS (viewport direct edit) ---
  if (!cmd.selection.empty()) {
    ImDrawList* dlG = ImGui::GetWindowDrawList();
    const float gripHalf = cmd.gripSizePx;
    constexpr ImU32 kGripFillE = IM_COL32(59, 130, 246, 255);
    constexpr ImU32 kGripBorderE = IM_COL32(30, 64, 175, 255);

    const float denx = worldRight - worldLeft + 1e-12f;
    const float deny = worldTop - worldBottom + 1e-12f;
    auto wts = [&](float wx, float wy, ImVec2* o) {
      const float u = (wx - worldLeft) / denx;
      const float v = (worldTop - wy) / deny;
      o->x = imgPos.x + u * avail.x;
      o->y = imgPos.y + v * avail.y;
    };

    auto drawGrip = [&](float wx, float wy) {
      ImVec2 gp{};
      wts(wx, wy, &gp);
      dlG->AddRectFilled(ImVec2(gp.x - gripHalf, gp.y - gripHalf), ImVec2(gp.x + gripHalf, gp.y + gripHalf),
                          kGripFillE);
      dlG->AddRect(ImVec2(gp.x - gripHalf, gp.y - gripHalf), ImVec2(gp.x + gripHalf, gp.y + gripHalf),
                   kGripBorderE, 0.f, 0, 1.f);
    };

    for (const SelectedEntity& sel : cmd.selection) {
      if (sel.type == SelectedEntity::Type::LineSeg) {
        const size_t k = static_cast<size_t>(sel.index) * 6;
        if (k + 5 < cmd.userLinesFlat.size()) {
          drawGrip(cmd.userLinesFlat[k], cmd.userLinesFlat[k + 1]);
          drawGrip(cmd.userLinesFlat[k + 3], cmd.userLinesFlat[k + 4]);
        }
      } else if (sel.type == SelectedEntity::Type::Circle) {
        const size_t k = static_cast<size_t>(sel.index) * 3;
        if (k + 2 < cmd.userCirclesCxCyR.size()) {
          const float cx = cmd.userCirclesCxCyR[k];
          const float cy = cmd.userCirclesCxCyR[k + 1];
          const float r = cmd.userCirclesCxCyR[k + 2];
          drawGrip(cx, cy);
          drawGrip(cx + r, cy);
        }
      } else if (sel.type == SelectedEntity::Type::Polyline) {
        const int np = cmd.userPolylineOffsets.size() > 0 ? static_cast<int>(cmd.userPolylineOffsets.size() - 1) : 0;
        if (sel.index >= 0 && sel.index < np) {
          const int startV = cmd.userPolylineOffsets[static_cast<size_t>(sel.index)];
          const int endV = cmd.userPolylineOffsets[static_cast<size_t>(sel.index + 1)];
          for (int vi = 0; vi < endV - startV; ++vi) {
            const size_t xIdx = static_cast<size_t>(startV + vi) * 3;
            if (xIdx + 1 >= cmd.userPolylineVerts.size())
              break;
            drawGrip(cmd.userPolylineVerts[xIdx], cmd.userPolylineVerts[xIdx + 1]);
          }
        }
      } else if (sel.type == SelectedEntity::Type::Arc) {
        if (sel.index >= 0 && static_cast<size_t>(sel.index) < cmd.userArcs.size()) {
          const CadArc& a = cmd.userArcs[static_cast<size_t>(sel.index)];
          drawGrip(a.cx, a.cy);
          drawGrip(a.cx + a.r * std::cos(a.startRad), a.cy + a.r * std::sin(a.startRad));
          const float endRad = a.startRad + a.sweepRad;
          drawGrip(a.cx + a.r * std::cos(endRad), a.cy + a.r * std::sin(endRad));
        }
      } else if (sel.type == SelectedEntity::Type::Ellipse) {
        if (sel.index >= 0 && static_cast<size_t>(sel.index) < cmd.userEllipses.size()) {
          const CadEllipse& el = cmd.userEllipses[static_cast<size_t>(sel.index)];
          drawGrip(el.cx, el.cy);
          drawGrip(el.cx + el.majVx, el.cy + el.majVy);
          const float perpX = -el.majVy;
          const float perpY = el.majVx;
          drawGrip(el.cx + perpX * el.ratio, el.cy + perpY * el.ratio);
        }
      }
    }
  }

  using VK = AppCommandState::Kind;
  const bool inImage = hovered && mx >= 0.f && mx < avail.x && my >= 0.f && my < avail.y;

  if (cmd.active == VK::None) {
    cmd.viewportCmdPaletteEngaged = false;
    cmd.viewportDrawingHovered = false;
  } else {
    ImGuiIO& ioEng = ImGui::GetIO();
    if (inImage)
      cmd.viewportCmdPaletteEngaged = true;
    else if (cmd.viewportCmdPaletteEngaged) {
      const bool hasDraft = cmdBuf && cmdBuf[0] != '\0';
      if (!hasDraft && !ioEng.WantTextInput && ImGui::GetActiveID() == 0)
        cmd.viewportCmdPaletteEngaged = false;
    }
  }

  const bool showViewportCmdPalette =
      cmd.active != VK::None && cmd.viewportCmdPaletteEngaged && cmdBuf && cmdBufSize > 0 &&
      !cmd.mtextRichEditorOpen;
  cmd.viewportDrawingHovered = showViewportCmdPalette;

  // Detect the palette's open edge so the two-field coordinate input resets its
  // typed-value locks when it (re)appears (REQ-024).
  static bool s_vpPalShownPrev = false;
  const bool vpPalJustOpened = showViewportCmdPalette && !s_vpPalShownPrev;
  s_vpPalShownPrev = showViewportCmdPalette;

  if (showViewportCmdPalette) {
    ImGuiIO& io = ImGui::GetIO();
    const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
    const float pad = 14.f;
    ImVec2 wp(mouse.x + pad, mouse.y + pad);
    const float estW = std::min(520.f, std::max(260.f, avail.x * 0.5f));
    const float estH = 78.f;
    wp.x = std::clamp(wp.x, mainViewport->WorkPos.x + 4.f,
                      mainViewport->WorkPos.x + mainViewport->WorkSize.x - estW - 8.f);
    wp.y = std::clamp(wp.y, mainViewport->WorkPos.y + 4.f,
                      mainViewport->WorkPos.y + mainViewport->WorkSize.y - estH - 8.f);

    ImGui::SetNextWindowPos(wp, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.94f);
    ImGuiWindowFlags wf = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize |
                          ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.f, 8.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.f);
    ImGui::Begin("##ViewportCommandInput", nullptr, wf);

    const bool pointEntry = CommandExpectsPointEntry(cmd);

    // Prompt label (AutoCAD "Specify ... :"). Reset the two-field locks whenever
    // the prompt changes (new point, including after a commit or viewport click)
    // or the palette just reopened for a fresh command.
    const char* curHint = CommandInputHint(cmd);
    static const char* s_lastDynHint = nullptr;
    const bool promptChanged = (curHint != s_lastDynHint) || vpPalJustOpened;
    s_lastDynHint = curHint;

    // Prompt label: a muted/secondary tone with a little gap below it, so it reads
    // as a label separated from the input boxes. Point prompts get an AutoCAD-style
    // "Specify … :" label; other prompts keep the full guidance hint.
    const ImVec4 hintCol = (cmd.displayColorThemeIdx == 0)
        ? ImVec4(0.90f, 0.93f, 0.98f, 1.f)
        : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    ImGui::PushStyleColor(ImGuiCol_Text, hintCol);
    const std::string promptLabel = pointEntry ? CadPointPromptLabel(cmd) : std::string(curHint);
    ImGui::TextUnformatted(promptLabel.c_str());
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0.f, 4.f));

    if (pointEntry) {
      // Two live coordinate fields: track the crosshair's world X/Y at the display
      // precision until the user types into a field (which locks it). Tab switches
      // fields; Enter (or a viewport click) commits. REQ-024.
      double liveWx = 0.0, liveWy = 0.0;
      if (outCursorX && outCursorY)
        CadCoord::WorldFromLocal(cmd, static_cast<float>(*outCursorX), static_cast<float>(*outCursorY), &liveWx,
                                 &liveWy);

      static char dxBuf[64] = {0}, dyBuf[64] = {0};
      static bool xLocked = false, yLocked = false;
      if (promptChanged) { xLocked = false; yLocked = false; }

      const int prec = cmd.displayLinearPrecision;
      if (!xLocked) std::snprintf(dxBuf, sizeof(dxBuf), "%s", FormatLinear(liveWx, prec).c_str());
      if (!yLocked) std::snprintf(dyBuf, sizeof(dyBuf), "%s", FormatLinear(liveWy, prec).c_str());

      const float fieldW = std::clamp(120.f * io.FontGlobalScale, 84.f, 220.f);
      const ImGuiInputTextFlags pf = ImGuiInputTextFlags_EnterReturnsTrue;

      const ImGuiID idX = ImGui::GetID("##dynX");
      const ImGuiID idY = ImGui::GetID("##dynY");
      const ImGuiID activeId = ImGui::GetActiveID();

      // Type-to-start: the first keystroke with neither field focused begins entry
      // in X immediately — clear the live value, capture the typed char, then lock
      // and focus X. (Without seeding, ImGui's first key only grabs focus, so it
      // took two presses to start typing.)
      if (activeId != idX && activeId != idY && !io.WantTextInput && io.InputQueueCharacters.Size > 0) {
        dxBuf[0] = '\0';
        xLocked = true;
        RouteQueuedCharsToCmdBuf(dxBuf, static_cast<int>(sizeof(dxBuf)), io);
        ImGui::SetKeyboardFocusHere();  // focus X (the next item)
      }

      ImGui::SetNextItemWidth(fieldW);
      const bool xEnter = ImGui::InputText("##dynX", dxBuf, sizeof(dxBuf), pf);
      // Clicking/Tabbing into a field clears its live readout for fresh entry.
      if (ImGui::IsItemActivated() && !xLocked) { dxBuf[0] = '\0'; xLocked = true; }
      if (ImGui::IsItemEdited()) xLocked = true;
      ImGui::SameLine(0.f, 6.f);
      ImGui::SetNextItemWidth(fieldW);
      const bool yEnter = ImGui::InputText("##dynY", dyBuf, sizeof(dyBuf), pf);
      if (ImGui::IsItemActivated() && !yLocked) { dyBuf[0] = '\0'; yLocked = true; }
      if (ImGui::IsItemEdited()) yLocked = true;

      if (xEnter || yEnter) {
        char combined[160];
        std::snprintf(combined, sizeof(combined), "%s,%s", dxBuf, dyBuf);
        ProcessCommandLineSubmit(combined, static_cast<int>(sizeof(combined)), cmd, log);
      }
    } else {
      // Single field for non-point prompts (bearing/angle/distance/option/command).
      if (!io.WantTextInput && io.InputQueueCharacters.Size > 0) {
        RouteQueuedCharsToCmdBuf(cmdBuf, cmdBufSize, io);
        ImGui::SetKeyboardFocusHere(0);
      }
      const ImGuiInputTextFlags itf = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackAlways;
      ImGui::SetNextItemWidth(std::clamp(360.f * io.FontGlobalScale, 200.f, mainViewport->WorkSize.x * 0.48f));
      const bool exec =
          ImGui::InputTextWithHint("##vp_cmd_buf", "Type value or Enter", cmdBuf, static_cast<size_t>(cmdBufSize),
                                   itf, CommandLineInputCallback, nullptr);
      if (exec)
        ProcessCommandLineSubmit(cmdBuf, cmdBufSize, cmd, log);
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
  }

  // CAD-style crosshair (viewport only): OS cursor hidden; position follows world cursor (sticky OSNAP blend in
  // command). Pick box matches object snap aperture; arms from Settings.
  //
  // While the user is typing a command NAME (no active command yet, command buffer
  // non-empty), the crosshair freezes at its last position so the autocomplete popup
  // anchored to it stays put. The OS mouse stays free — the user can move/click the
  // popup. It unfreezes when the command is entered (cmd.active set, buffer cleared)
  // or cancelled (buffer cleared on Esc).
  const bool typingCommand =
      (cmd.active == AppCommandState::Kind::None) && cmdBuf && cmdBuf[0] != '\0';
  const bool liveHover = hovered && mx >= 0.f && mx < avail.x && my >= 0.f && my < avail.y;
  const bool frozenHair = typingCommand && s_lastCrosshairScreen.x >= 0.f;
  if (liveHover || frozenHair) {
    const ImVec2 imgMin = imgPos;
    const ImVec2 imgMax(imgPos.x + avail.x, imgPos.y + avail.y);
    float cx, cy;
    if (frozenHair) {
      // Frozen: hold the last position; leave the OS cursor visible (user has the mouse).
      cx = s_lastCrosshairScreen.x;
      cy = s_lastCrosshairScreen.y;
    } else {
      ImGui::SetMouseCursor(ImGuiMouseCursor_None);
      cx = mouse.x;
      cy = mouse.y;
      if (outCursorX && outCursorY) {
        const float denx = worldRight - worldLeft + 1.e-12f;
        const float deny = worldTop - worldBottom + 1.e-12f;
        const float uSnap = (*outCursorX - worldLeft) / denx;
        const float vSnap = (worldTop - *outCursorY) / deny;
        cx = imgPos.x + uSnap * avail.x;
        cy = imgPos.y + vSnap * avail.y;
      }
      // Remember the live crosshair so the popup can anchor here, and so the position
      // is preserved once we freeze on the next typed character.
      s_lastCrosshairScreen = ImVec2(cx, cy);
    }
    const float ap = std::clamp(cmd.objectSnapAperturePx, 4.f, 64.f);
    const float phx = ap * 0.5f;
    const float phy = ap * 0.5f;
    const float hair = std::clamp(cmd.viewportCrosshairHairPx, 0.5f, 4.f);
    const float frx = std::clamp(cmd.viewportCrosshairArmFracX, 0.001f, 0.5f);
    const float fry = std::clamp(cmd.viewportCrosshairArmFracY, 0.001f, 0.5f);
    const float armX = frx * avail.x;
    const float armY = fry * avail.y;
    const float cr = std::clamp(cmd.viewportCrosshairR, 0.f, 1.f);
    const float cg = std::clamp(cmd.viewportCrosshairG, 0.f, 1.f);
    const float cb = std::clamp(cmd.viewportCrosshairB, 0.f, 1.f);
    const ImU32 kCad =
        IM_COL32(static_cast<int>(cr * 255.f), static_cast<int>(cg * 255.f), static_cast<int>(cb * 255.f), 255);
    ImDrawList* wdl = ImGui::GetWindowDrawList();
    wdl->PushClipRect(imgMin, imgMax, true);
    const float xl = std::max(imgMin.x, cx - phx - armX);
    const float xr = std::min(imgMax.x, cx + phx + armX);
    const float yt = std::max(imgMin.y, cy - phy - armY);
    const float yb = std::min(imgMax.y, cy + phy + armY);
    wdl->AddLine(ImVec2(xl, cy), ImVec2(cx - phx, cy), kCad, hair);
    wdl->AddLine(ImVec2(cx + phx, cy), ImVec2(xr, cy), kCad, hair);
    wdl->AddLine(ImVec2(cx, yt), ImVec2(cx, cy - phy), kCad, hair);
    wdl->AddLine(ImVec2(cx, cy + phy), ImVec2(cx, yb), kCad, hair);
    const float l = cx - phx;
    const float r = cx + phx;
    const float t = cy - phy;
    const float b = cy + phy;
    wdl->AddLine(ImVec2(l, t), ImVec2(r, t), kCad, hair);
    wdl->AddLine(ImVec2(r, t), ImVec2(r, b), kCad, hair);
    wdl->AddLine(ImVec2(r, b), ImVec2(l, b), kCad, hair);
    wdl->AddLine(ImVec2(l, b), ImVec2(l, t), kCad, hair);
    wdl->PopClipRect();
  }

  if (ImGui::BeginPopup("##drawing1_vp_ctx")) {
    using AK = AppCommandState::Kind;
    const bool gripActive = cmd.dimGripMoveActive || cmd.entityGripMoveActive ||
                            cmd.mtextGripMoveActive || cmd.mtextRichEditorOpen;
    const bool hasSel = !cmd.selection.empty() || !cmd.selectedSurveyPointIndices.empty();

    if (cmd.active != AK::None) {
      // Command Mode shortcut menu
      if (ImGui::MenuItem("Enter")) {
        char empty[2] = {};
        ProcessCommandLineSubmit(empty, static_cast<int>(sizeof(empty)), cmd, log);
        ImGui::CloseCurrentPopup();
      }
      if (ImGui::MenuItem("Cancel")) {
        CancelActiveCommand(cmd, log);
        ImGui::CloseCurrentPopup();
      }
    } else if (!gripActive) {
      // Edit Mode / Default Mode shortcut menu
      if (cmd.lastCommand != AK::None) {
        char repeatLabel[64];
        std::snprintf(repeatLabel, sizeof(repeatLabel), "Repeat %s",
                      AppCommandState::KindName(cmd.lastCommand));
        if (ImGui::MenuItem(repeatLabel))
          RepeatLastCommand(cmd, log);
        ImGui::Separator();
      }
      if (hasSel) {
        if (ImGui::MenuItem("Move"))   { StartMoveCommand(cmd, log);   ImGui::CloseCurrentPopup(); }
        if (ImGui::MenuItem("Copy"))   { StartCopyCommand(cmd, log);   ImGui::CloseCurrentPopup(); }
        if (ImGui::MenuItem("Rotate")) { StartRotateCommand(cmd, log); ImGui::CloseCurrentPopup(); }
        if (ImGui::MenuItem("Scale"))  { StartScaleCommand(cmd, log);  ImGui::CloseCurrentPopup(); }
        if (ImGui::MenuItem("Delete")) { StartDeleteCommand(cmd, log); ImGui::CloseCurrentPopup(); }
        ImGui::Separator();
        if (ImGui::MenuItem("Select similar"))
          SelectSimilarToCurrentSelection(cmd, &log);
        if (ImGui::MenuItem("Selection...")) {
          cmd.selectionCycleEntities     = cmd.selection;
          cmd.selectionCycleSurveyPoints = cmd.selectedSurveyPointIndices;
          cmd.showSelectionCyclingWindow = true;
        }
        if (ImGui::MenuItem("Clear selection")) {
          ClearCadSelection(cmd);
          BumpCadGpuCache(cmd);
        }
      }
    }
    ImGui::EndPopup();
  }
  ImGui::SetNextWindowPos(ImGui::GetMousePos(), ImGuiCond_Appearing);
  if (ImGui::BeginPopup("##gos_snap_pick", ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGuiIO& ioSnapPick = ImGui::GetIO();
    const auto goSnapKind = [&](CadSnap::Kind k) {
      g_snapPickMenuScratch.clear();
      CadSnap::GatherAllSnapsOfKind(k, g_snapMenuSortX, g_snapMenuSortY, cmd, true, g_snapPickMenuScratch);
      g_snapMenuSelectedKind = k;
      g_snapMenuStep = 1;
    };

    if (g_snapMenuStep == 0) {
      ImGui::TextUnformatted("Snap once — choose type");
      ImGui::Separator();
      if (cmd.objectSnapEndpoint && ImGui::Selectable("Endpoint"))
        goSnapKind(CadSnap::Kind::Endpoint);
      if (cmd.objectSnapMidpoint && ImGui::Selectable("Midpoint"))
        goSnapKind(CadSnap::Kind::Midpoint);
      if (cmd.objectSnapCenter && ImGui::Selectable("Center"))
        goSnapKind(CadSnap::Kind::Center);
      if (cmd.objectSnapPerpendicular && CadSnap::CommandHasPerpendicularSnapReference(cmd, true) &&
          ImGui::Selectable("Perpendicular"))
        goSnapKind(CadSnap::Kind::Perpendicular);
      if (cmd.objectSnapSurveyPoint && ImGui::Selectable("Survey"))
        goSnapKind(CadSnap::Kind::SurveyCenter);
      if (cmd.objectSnapGeometricCenter && ImGui::Selectable("Geometric center"))
        goSnapKind(CadSnap::Kind::GeometricCenter);
    } else {
      char title[96];
      std::snprintf(title, sizeof(title), "%s — all in model (sorted by distance from click)",
                    SnapKindLabelForUi(g_snapMenuSelectedKind));
      ImGui::TextWrapped("%s", title);
      if (ImGui::Button("Back")) {
        g_snapMenuStep = 0;
        g_snapPickMenuScratch.clear();
      }
      ImGui::Separator();
      const float lineH = ImGui::GetTextLineHeightWithSpacing();
      const float maxListH = std::clamp(22.f * lineH, 180.f, 420.f * ioSnapPick.FontGlobalScale);
      ImGui::BeginChild("##gos_snap_pick_list", ImVec2(360.f * ioSnapPick.FontGlobalScale, maxListH), true,
                        ImGuiWindowFlags_AlwaysVerticalScrollbar);
      if (g_snapPickMenuScratch.empty()) {
        ImGui::TextUnformatted("No matching snaps in the current geometry.");
      } else {
        for (size_t i = 0; i < g_snapPickMenuScratch.size(); ++i) {
          const CadSnap::Hit& h = g_snapPickMenuScratch[i].hit;
          char line[192];
          FormatSnapPickLine(line, sizeof(line), cmd, h);
          ImGui::PushID(static_cast<int>(i));
          if (ImGui::Selectable(line)) {
            cmd.pendingOneShotSnapValid = true;
            cmd.pendingOneShotSnapX = h.x;
            cmd.pendingOneShotSnapY = h.y;
            cmd.pendingOneShotSnapKind = static_cast<int>(h.kind);
            ImGui::CloseCurrentPopup();
          }
          ImGui::PopID();
        }
      }
      ImGui::EndChild();
    }
    ImGui::EndPopup();
  }

  DrawMtextRichEditorOverlay(cmd, log, static_cast<float>(worldLeft), static_cast<float>(worldRight),
                             static_cast<float>(worldBottom), static_cast<float>(worldTop), imgPos, avail);

  // REQ-028: Per-viewport layer freeze UI. Shows layer checkboxes when a viewport is selected in paper space.
  if (cmd.activeSpaceIndex >= 0 && cmd.activeSpaceIndex < static_cast<int>(cmd.paperLayouts.size()) &&
      cmd.selectedViewports.size() == 1 && cmd.selectedViewportIndex >= 0) {
    PaperLayout& L = cmd.paperLayouts[static_cast<size_t>(cmd.activeSpaceIndex)];
    if (cmd.selectedViewportIndex < static_cast<int>(L.viewports.size())) {
      Viewport& vp = L.viewports[static_cast<size_t>(cmd.selectedViewportIndex)];
      ImGui::Separator();
      if (ImGui::CollapsingHeader("Frozen Layers", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::BeginChild("##frozenLayersChild", ImVec2(-1.f, 150.f), ImGuiChildFlags_Borders);
        for (const auto& row : cmd.drawingLayerTable) {
          bool frozen = IsLayerFrozenInViewport(vp, row.name);
          if (ImGui::Checkbox(row.name.c_str(), &frozen)) {
            ToggleFrozenLayerInViewport(vp, row.name);
            BumpCadGpuCache(cmd);
          }
        }
        ImGui::EndChild();
      }
    }
  }

  *outFbW = vpFbW;
  *outFbH = vpFbH;
  cmd.viewportLastFbW = vpFbW;
  cmd.viewportLastFbH = vpFbH;
  cmd.viewportPanX = *panX;
  cmd.viewportPanY = *panY;
  cmd.viewportZoom = *zoom;

  ImGui::End();
}

// ---------------------------------------------------------------------------
// QUICKSELECT
// ---------------------------------------------------------------------------

static void ExecuteQuickSelect(AppCommandState& cmd, std::vector<std::string>& log) {
  using OT = AppCommandState::QsObjectType;
  using QP = AppCommandState::QsProperty;
  using QO = AppCommandState::QsOperator;
  using QI = AppCommandState::QsInclude;
  using T  = SelectedEntity::Type;

  float numVal = 0.f;
  try { numVal = std::stof(cmd.qsValueBuf); } catch (...) {}
  const std::string strVal = cmd.qsValueBuf;

  auto matchStr = [&](const std::string& prop) -> bool {
    switch (cmd.qsOperator) {
    case QO::SelectAll:  return true;
    case QO::Equals:     return prop == strVal;
    case QO::NotEquals:  return prop != strVal;
    default:             return false;
    }
  };
  auto matchNum = [&](float prop) -> bool {
    switch (cmd.qsOperator) {
    case QO::SelectAll:     return true;
    case QO::Equals:        return std::fabs(prop - numVal) < 1e-5f;
    case QO::NotEquals:     return std::fabs(prop - numVal) >= 1e-5f;
    case QO::LessThan:      return prop < numVal;
    case QO::GreaterThan:   return prop > numVal;
    }
    return false;
  };
  auto typeMatches = [&](OT t) -> bool {
    return cmd.qsObjectType == OT::All || cmd.qsObjectType == t;
  };
  auto getAttrs = [&](const SelectedEntity& e) -> const EntityAttributes* {
    switch (e.type) {
    case T::LineSeg:    return (size_t)e.index < cmd.userLineAttrs.size()       ? &cmd.userLineAttrs[(size_t)e.index]       : nullptr;
    case T::Circle:     return (size_t)e.index < cmd.userCircleAttrs.size()     ? &cmd.userCircleAttrs[(size_t)e.index]     : nullptr;
    case T::Arc:        return (size_t)e.index < cmd.userArcAttrs.size()        ? &cmd.userArcAttrs[(size_t)e.index]        : nullptr;
    case T::Ellipse:    return (size_t)e.index < cmd.userEllAttrs.size()        ? &cmd.userEllAttrs[(size_t)e.index]        : nullptr;
    case T::Polyline:   return (size_t)e.index < cmd.userPolylineAttrs.size()   ? &cmd.userPolylineAttrs[(size_t)e.index]   : nullptr;
    case T::Annotation: return (size_t)e.index < cmd.cadAnnotationAttrs.size()  ? &cmd.cadAnnotationAttrs[(size_t)e.index]  : nullptr;
    default:            return nullptr;
    }
  };

  auto testEntity = [&](const SelectedEntity& e) -> bool {
    // Type gate
    switch (e.type) {
    case T::LineSeg:  if (!typeMatches(OT::Line))    return false; break;
    case T::Circle:   if (!typeMatches(OT::Circle))  return false; break;
    case T::Arc:      if (!typeMatches(OT::Arc))     return false; break;
    case T::Ellipse:  if (!typeMatches(OT::Ellipse)) return false; break;
    case T::Polyline: if (!typeMatches(OT::Polyline))return false; break;
    case T::Annotation: {
      if ((size_t)e.index >= cmd.cadAnnotations.size()) return false;
      const auto k = cmd.cadAnnotations[(size_t)e.index].kind;
      using AK = CadAnnotation::Kind;
      if (cmd.qsObjectType == OT::Text       && k != AK::Text)       return false;
      if (cmd.qsObjectType == OT::Mtext      && k != AK::Mtext)      return false;
      if (cmd.qsObjectType == OT::DimAligned && k != AK::DimAligned) return false;
      if (cmd.qsObjectType == OT::DimLinear  && k != AK::DimLinear)  return false;
      if (cmd.qsObjectType == OT::DimAngular && k != AK::DimAngular) return false;
      if (cmd.qsObjectType != OT::All && cmd.qsObjectType != OT::Text &&
          cmd.qsObjectType != OT::Mtext && cmd.qsObjectType != OT::DimAligned &&
          cmd.qsObjectType != OT::DimLinear && cmd.qsObjectType != OT::DimAngular)
        return false;
      break;
    }
    default: return false;
    }
    // Property test
    const EntityAttributes* attrs = getAttrs(e);
    switch (cmd.qsProperty) {
    case QP::Layer:   return attrs ? matchStr(attrs->layer) : (cmd.qsOperator == QO::SelectAll);
    case QP::Color: {
      if (!attrs) return cmd.qsOperator == QO::SelectAll;
      // Resolve "ByLayer" to the layer's actual color so filtering by "Red" finds
      // entities that visually appear red even when their stored color is ByLayer.
      std::string effectiveColor = attrs->color;
      if (effectiveColor == "ByLayer") {
        const CadLayerRow* row = FindDrawingLayerRowCi(cmd, attrs->layer);
        if (row && !row->color.empty())
          effectiveColor = row->color;
      }
      return matchStr(effectiveColor);
    }
    case QP::Length: {
      float len = 0.f;
      if (e.type == T::LineSeg) {
        const size_t k = (size_t)e.index * 6;
        if (k + 4 < cmd.userLinesFlat.size())
          len = std::hypot(cmd.userLinesFlat[k+3] - cmd.userLinesFlat[k],
                           cmd.userLinesFlat[k+4] - cmd.userLinesFlat[k+1]);
      } else if (e.type == T::Polyline) {
        const int np = (int)cmd.userPolylineOffsets.size();
        if (e.index >= 0 && e.index + 1 < np) {
          const int sv = cmd.userPolylineOffsets[(size_t)e.index];
          const int ev = cmd.userPolylineOffsets[(size_t)e.index + 1];
          for (int vi = sv; vi + 1 < ev; ++vi) {
            const size_t xi = (size_t)vi * 3;
            if (xi + 3 < cmd.userPolylineVerts.size())
              len += std::hypot(cmd.userPolylineVerts[xi+3] - cmd.userPolylineVerts[xi],
                                cmd.userPolylineVerts[xi+4] - cmd.userPolylineVerts[xi+1]);
          }
        }
      }
      return matchNum(len);
    }
    case QP::Radius: {
      float r = 0.f;
      if (e.type == T::Circle) {
        const size_t k = (size_t)e.index * 3;
        if (k + 2 < cmd.userCirclesCxCyR.size()) r = cmd.userCirclesCxCyR[k + 2];
      } else if (e.type == T::Arc && (size_t)e.index < cmd.userArcs.size()) {
        r = cmd.userArcs[(size_t)e.index].r;
      }
      return matchNum(r);
    }
    case QP::Closed:
      if (e.type == T::Polyline && (size_t)e.index < cmd.userPolylineClosed.size()) {
        const bool closed = cmd.userPolylineClosed[(size_t)e.index] != 0;
        if (cmd.qsOperator == QO::SelectAll) return true;
        const bool want = (strVal == "Yes" || strVal == "yes" || strVal == "1" || strVal == "true");
        return (cmd.qsOperator == QO::Equals) ? (closed == want) : (closed != want);
      }
      return false;
    case QP::Content:
      if (e.type == T::Annotation && (size_t)e.index < cmd.cadAnnotations.size())
        return matchStr(cmd.cadAnnotations[(size_t)e.index].text);
      return false;
    default: return cmd.qsOperator == QO::SelectAll;
    }
  };

  auto testSurvey = [&](int spi) -> bool {
    if (!typeMatches(OT::SurveyPoint)) return false;
    if ((size_t)spi >= cmd.surveyPoints.size()) return false;
    const SurveyPoint& sp = cmd.surveyPoints[(size_t)spi];
    switch (cmd.qsProperty) {
    case QP::Layer:       return matchStr(sp.layer);
    case QP::Id:          return matchNum(static_cast<float>(sp.id));
    case QP::Elevation:   return matchNum(sp.elevation);
    case QP::Easting:     return matchNum(sp.easting);
    case QP::Northing:    return matchNum(sp.northing);
    case QP::Description: return matchStr(sp.description);
    default:              return cmd.qsOperator == QO::SelectAll;
    }
  };

  const bool exclude = (cmd.qsIncludeMode == QI::Exclude);
  std::vector<SelectedEntity> newCad;
  std::vector<int> newSurvey;

  auto addCad = [&](const SelectedEntity& e) {
    if (testEntity(e) != exclude) newCad.push_back(e);
  };
  auto addSurvey = [&](int spi) {
    if (testSurvey(spi) != exclude) newSurvey.push_back(spi);
  };

  if (cmd.qsApplyTo == AppCommandState::QsApplyTo::EntireDrawing) {
    const int nLines = (int)(cmd.userLinesFlat.size() / 6);
    for (int i = 0; i < nLines; ++i)  addCad({SelectedEntity::Type::LineSeg, i});
    const int nCirc = (int)(cmd.userCirclesCxCyR.size() / 3);
    for (int i = 0; i < nCirc; ++i)   addCad({SelectedEntity::Type::Circle, i});
    for (int i = 0; i < (int)cmd.userArcs.size(); ++i)      addCad({SelectedEntity::Type::Arc, i});
    for (int i = 0; i < (int)cmd.userEllipses.size(); ++i)  addCad({SelectedEntity::Type::Ellipse, i});
    const int nPoly = std::max(0, (int)cmd.userPolylineOffsets.size() - 1);
    for (int i = 0; i < nPoly; ++i)   addCad({SelectedEntity::Type::Polyline, i});
    for (int i = 0; i < (int)cmd.cadAnnotations.size(); ++i) addCad({SelectedEntity::Type::Annotation, i});
    for (int i = 0; i < (int)cmd.surveyPoints.size(); ++i)   addSurvey(i);
  } else {
    for (const auto& e : cmd.selection)           addCad(e);
    for (int spi : cmd.selectedSurveyPointIndices) addSurvey(spi);
  }

  if (cmd.qsAppendToExisting) {
    for (const auto& e : newCad) {
      if (!std::any_of(cmd.selection.begin(), cmd.selection.end(),
            [&](const SelectedEntity& s){ return s.type == e.type && s.index == e.index; }))
        cmd.selection.push_back(e);
    }
    for (int spi : newSurvey) {
      if (std::find(cmd.selectedSurveyPointIndices.begin(), cmd.selectedSurveyPointIndices.end(), spi)
          == cmd.selectedSurveyPointIndices.end())
        cmd.selectedSurveyPointIndices.push_back(spi);
    }
  } else {
    cmd.selection = std::move(newCad);
    cmd.selectedSurveyPointIndices = std::move(newSurvey);
  }

  EnsureAttrCounts(cmd);
  BumpCadGpuCache(cmd);

  const int total = (int)(cmd.selection.size() + cmd.selectedSurveyPointIndices.size());
  char msg[128];
  std::snprintf(msg, sizeof(msg), "QUICKSELECT — %d item%s selected.", total, total == 1 ? "" : "s");
  log.push_back(msg);
}

// Property lists per object type (indices into QsProperty enum).
struct QsTypeProps {
  const char* label;
  AppCommandState::QsObjectType type;
  // Which properties are valid, as QsProperty values
  std::initializer_list<AppCommandState::QsProperty> props;
};

static const QsTypeProps kQsTypes[] = {
  { "All",              AppCommandState::QsObjectType::All,        { AppCommandState::QsProperty::Layer, AppCommandState::QsProperty::Color } },
  { "Line",             AppCommandState::QsObjectType::Line,       { AppCommandState::QsProperty::Layer, AppCommandState::QsProperty::Color, AppCommandState::QsProperty::Length } },
  { "Circle",           AppCommandState::QsObjectType::Circle,     { AppCommandState::QsProperty::Layer, AppCommandState::QsProperty::Color, AppCommandState::QsProperty::Radius } },
  { "Arc",              AppCommandState::QsObjectType::Arc,        { AppCommandState::QsProperty::Layer, AppCommandState::QsProperty::Color, AppCommandState::QsProperty::Radius } },
  { "Ellipse",          AppCommandState::QsObjectType::Ellipse,    { AppCommandState::QsProperty::Layer, AppCommandState::QsProperty::Color } },
  { "Polyline",         AppCommandState::QsObjectType::Polyline,   { AppCommandState::QsProperty::Layer, AppCommandState::QsProperty::Color, AppCommandState::QsProperty::Length, AppCommandState::QsProperty::Closed } },
  { "Text",             AppCommandState::QsObjectType::Text,       { AppCommandState::QsProperty::Layer, AppCommandState::QsProperty::Color, AppCommandState::QsProperty::Content } },
  { "MText",            AppCommandState::QsObjectType::Mtext,      { AppCommandState::QsProperty::Layer, AppCommandState::QsProperty::Color, AppCommandState::QsProperty::Content } },
  { "Dim (Aligned)",    AppCommandState::QsObjectType::DimAligned, { AppCommandState::QsProperty::Layer, AppCommandState::QsProperty::Color } },
  { "Dim (Linear)",     AppCommandState::QsObjectType::DimLinear,  { AppCommandState::QsProperty::Layer, AppCommandState::QsProperty::Color } },
  { "Dim (Angular)",    AppCommandState::QsObjectType::DimAngular, { AppCommandState::QsProperty::Layer, AppCommandState::QsProperty::Color } },
  { "Survey Point",     AppCommandState::QsObjectType::SurveyPoint,{ AppCommandState::QsProperty::Layer, AppCommandState::QsProperty::Id, AppCommandState::QsProperty::Elevation, AppCommandState::QsProperty::Easting, AppCommandState::QsProperty::Northing, AppCommandState::QsProperty::Description } },
};

static const char* QsPropertyLabel(AppCommandState::QsProperty p) {
  using QP = AppCommandState::QsProperty;
  switch (p) {
  case QP::Layer:       return "Layer";
  case QP::Color:       return "Color";
  case QP::Length:      return "Length";
  case QP::Radius:      return "Radius";
  case QP::Closed:      return "Closed";
  case QP::Content:     return "Content";
  case QP::Id:          return "ID";
  case QP::Elevation:   return "Elevation";
  case QP::Easting:     return "Easting";
  case QP::Northing:    return "Northing";
  case QP::Description: return "Description";
  }
  return "";
}

static bool QsPropertyIsNumeric(AppCommandState::QsProperty p) {
  using QP = AppCommandState::QsProperty;
  return p == QP::Length || p == QP::Radius || p == QP::Id ||
         p == QP::Elevation || p == QP::Easting || p == QP::Northing;
}

void DrawQuickSelectWindow(AppCommandState& cmd, std::vector<std::string>& log) {
  if (!cmd.showQuickSelectWindow)
    return;

  ImGui::SetNextWindowSize(ImVec2(400, 380), ImGuiCond_FirstUseEver);
  bool open = cmd.showQuickSelectWindow;
  if (!ImGui::Begin("Quick Select", &open, ImGuiWindowFlags_NoCollapse)) {
    cmd.showQuickSelectWindow = open;
    ImGui::End();
    return;
  }
  cmd.showQuickSelectWindow = open;

  using QP = AppCommandState::QsProperty;
  using QO = AppCommandState::QsOperator;
  using QI = AppCommandState::QsInclude;
  constexpr int kNumTypes = (int)(sizeof(kQsTypes) / sizeof(kQsTypes[0]));

  // --- Apply to ---
  ImGui::TextUnformatted("Apply to:");
  ImGui::SameLine(120.f);
  ImGui::SetNextItemWidth(-FLT_MIN);
  {
    static const char* kApplyItems[] = { "Entire drawing", "Current selection" };
    int sel = static_cast<int>(cmd.qsApplyTo);
    if (ImGui::Combo("##qs_apply", &sel, kApplyItems, 2))
      cmd.qsApplyTo = static_cast<AppCommandState::QsApplyTo>(sel);
  }

  // --- Object type ---
  ImGui::TextUnformatted("Object type:");
  ImGui::SameLine(120.f);
  ImGui::SetNextItemWidth(-FLT_MIN);
  {
    const char* curTypeName = kQsTypes[0].label;
    for (int i = 0; i < kNumTypes; ++i)
      if (kQsTypes[i].type == cmd.qsObjectType) { curTypeName = kQsTypes[i].label; break; }
    if (ImGui::BeginCombo("##qs_type", curTypeName)) {
      for (int i = 0; i < kNumTypes; ++i) {
        const bool sel = (kQsTypes[i].type == cmd.qsObjectType);
        if (ImGui::Selectable(kQsTypes[i].label, sel)) {
          cmd.qsObjectType = kQsTypes[i].type;
          // Reset property to first valid one for this type
          if (kQsTypes[i].props.size() != 0)
            cmd.qsProperty = *kQsTypes[i].props.begin();
          // Reset operator if it's numeric-only but new property is a string property
          if (!QsPropertyIsNumeric(cmd.qsProperty) &&
              (cmd.qsOperator == QO::LessThan || cmd.qsOperator == QO::GreaterThan))
            cmd.qsOperator = QO::Equals;
        }
        if (sel) ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }
  }

  // Collect valid properties for current type
  std::vector<QP> validProps;
  for (int i = 0; i < kNumTypes; ++i)
    if (kQsTypes[i].type == cmd.qsObjectType) { validProps.assign(kQsTypes[i].props); break; }
  // Ensure current property is valid; reset if not
  if (!validProps.empty() && std::find(validProps.begin(), validProps.end(), cmd.qsProperty) == validProps.end())
    cmd.qsProperty = validProps[0];

  // --- Properties ---
  ImGui::TextUnformatted("Properties:");
  ImGui::SameLine(120.f);
  ImGui::SetNextItemWidth(-FLT_MIN);
  {
    const char* curPropName = validProps.empty() ? "Layer" : QsPropertyLabel(cmd.qsProperty);
    if (ImGui::BeginCombo("##qs_prop", curPropName)) {
      for (QP p : validProps) {
        const bool sel = (p == cmd.qsProperty);
        if (ImGui::Selectable(QsPropertyLabel(p), sel)) {
          cmd.qsProperty = p;
          if (!QsPropertyIsNumeric(p) &&
              (cmd.qsOperator == QO::LessThan || cmd.qsOperator == QO::GreaterThan))
            cmd.qsOperator = QO::Equals;
        }
        if (sel) ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }
  }

  // --- Operator ---
  const bool isNumeric = QsPropertyIsNumeric(cmd.qsProperty);
  ImGui::TextUnformatted("Operator:");
  ImGui::SameLine(120.f);
  ImGui::SetNextItemWidth(-FLT_MIN);
  {
    static const char* kAllOps[]  = { "= Equals", "<> Not Equal", "< Less Than", "> Greater Than", "Select All" };
    static const char* kStrOps[]  = { "= Equals", "<> Not Equal", "Select All" };
    const char** ops   = isNumeric ? kAllOps : kStrOps;
    const int    nOps  = isNumeric ? 5 : 3;
    // Map current QsOperator to the index in the active list
    static const QO kAllOpVals[] = { QO::Equals, QO::NotEquals, QO::LessThan, QO::GreaterThan, QO::SelectAll };
    static const QO kStrOpVals[] = { QO::Equals, QO::NotEquals, QO::SelectAll };
    const QO* opVals = isNumeric ? kAllOpVals : kStrOpVals;
    int curIdx = 0;
    for (int i = 0; i < nOps; ++i) if (opVals[i] == cmd.qsOperator) { curIdx = i; break; }
    if (ImGui::BeginCombo("##qs_op", ops[curIdx])) {
      for (int i = 0; i < nOps; ++i) {
        const bool sel = (i == curIdx);
        if (ImGui::Selectable(ops[i], sel)) cmd.qsOperator = opVals[i];
        if (sel) ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }
  }

  // --- Value ---
  const bool needValue = (cmd.qsOperator != QO::SelectAll);
  ImGui::TextUnformatted("Value:");
  ImGui::SameLine(120.f);
  ImGui::SetNextItemWidth(-FLT_MIN);
  ImGui::BeginDisabled(!needValue);
  if (cmd.qsProperty == QP::Layer) {
    std::vector<std::string> layers;
    CollectAllDrawingLayers(cmd, &layers);
    // Ensure current value is in the list; default to first entry.
    const std::string curVal = cmd.qsValueBuf;
    if (std::find(layers.begin(), layers.end(), curVal) == layers.end() && !layers.empty())
      std::snprintf(cmd.qsValueBuf, sizeof(cmd.qsValueBuf), "%s", layers[0].c_str());
    if (ImGui::BeginCombo("##qs_val_layer", cmd.qsValueBuf)) {
      for (const auto& lay : layers) {
        const bool sel = (lay == cmd.qsValueBuf);
        if (ImGui::Selectable(lay.c_str(), sel))
          std::snprintf(cmd.qsValueBuf, sizeof(cmd.qsValueBuf), "%s", lay.c_str());
        if (sel) ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }
  } else if (cmd.qsProperty == QP::Color) {
    std::vector<std::pair<std::string, std::string>> colorOpts;
    CollectQsColorOptions(cmd, &colorOpts);
    // Ensure the stored value is valid; default to first option.
    const std::string curStorage = cmd.qsValueBuf;
    const bool curValid = std::any_of(colorOpts.begin(), colorOpts.end(),
      [&](const std::pair<std::string,std::string>& p){ return p.second == curStorage; });
    if (!curValid && !colorOpts.empty())
      std::snprintf(cmd.qsValueBuf, sizeof(cmd.qsValueBuf), "%s", colorOpts[0].second.c_str());
    // Find current display label for preview.
    const char* preview = cmd.qsValueBuf;
    for (const auto& opt : colorOpts)
      if (opt.second == cmd.qsValueBuf) { preview = opt.first.c_str(); break; }
    if (ImGui::BeginCombo("##qs_val_color", preview)) {
      for (const auto& opt : colorOpts) {
        const bool sel = (opt.second == cmd.qsValueBuf);
        if (ImGui::Selectable(opt.first.c_str(), sel))
          std::snprintf(cmd.qsValueBuf, sizeof(cmd.qsValueBuf), "%s", opt.second.c_str());
        if (sel) ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }
  } else if (cmd.qsProperty == QP::Closed) {
    static const char* kClosedOpts[] = { "Yes", "No" };
    int closedSel = (std::string(cmd.qsValueBuf) == "Yes" || std::string(cmd.qsValueBuf) == "yes") ? 0 : 1;
    if (ImGui::Combo("##qs_val_closed", &closedSel, kClosedOpts, 2))
      std::snprintf(cmd.qsValueBuf, sizeof(cmd.qsValueBuf), "%s", kClosedOpts[closedSel]);
  } else {
    ImGui::InputText("##qs_val", cmd.qsValueBuf, sizeof(cmd.qsValueBuf));
  }
  ImGui::EndDisabled();

  ImGui::Separator();

  // --- How to apply ---
  ImGui::TextUnformatted("How to apply:");
  {
    int inc = static_cast<int>(cmd.qsIncludeMode);
    if (ImGui::RadioButton("Include in new selection",  &inc, 0)) cmd.qsIncludeMode = QI::Include;
    if (ImGui::RadioButton("Exclude from new selection",&inc, 1)) cmd.qsIncludeMode = QI::Exclude;
  }

  ImGui::Checkbox("Append to current selection", &cmd.qsAppendToExisting);

  ImGui::Separator();

  const float btnW = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
  if (ImGui::Button("OK", ImVec2(btnW, 0))) {
    ExecuteQuickSelect(cmd, log);
    cmd.showQuickSelectWindow = false;
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel", ImVec2(btnW, 0)))
    cmd.showQuickSelectWindow = false;

  ImGui::End();
}

// Returns a display label for a SelectedEntity, e.g. "Line 3", "MTEXT 2".
static void FormatSelectedEntityLabel(const AppCommandState& cmd, const SelectedEntity& e,
                                      char* buf, size_t bufSize) {
  using T = SelectedEntity::Type;
  switch (e.type) {
  case T::LineSeg:
    std::snprintf(buf, bufSize, "Line %d", e.index + 1);
    break;
  case T::Circle:
    std::snprintf(buf, bufSize, "Circle %d", e.index + 1);
    break;
  case T::Arc:
    std::snprintf(buf, bufSize, "Arc %d", e.index + 1);
    break;
  case T::Ellipse:
    std::snprintf(buf, bufSize, "Ellipse %d", e.index + 1);
    break;
  case T::Polyline:
    std::snprintf(buf, bufSize, "Polyline %d", e.index + 1);
    break;
  case T::Annotation: {
    const char* kindStr = "Annotation";
    if (static_cast<size_t>(e.index) < cmd.cadAnnotations.size()) {
      switch (cmd.cadAnnotations[static_cast<size_t>(e.index)].kind) {
      case CadAnnotation::Kind::Text:       kindStr = "Text";             break;
      case CadAnnotation::Kind::Mtext:      kindStr = "MText";            break;
      case CadAnnotation::Kind::DimAligned: kindStr = "Dim (Aligned)";    break;
      case CadAnnotation::Kind::DimLinear:  kindStr = "Dim (Linear)";     break;
      case CadAnnotation::Kind::DimAngular: kindStr = "Dim (Angular)";    break;
      }
    }
    std::snprintf(buf, bufSize, "%s %d", kindStr, e.index + 1);
    break;
  }
  case T::PdfUnderlay:
    std::snprintf(buf, bufSize, "PDF Underlay %d", e.index + 1);
    break;
  default:
    std::snprintf(buf, bufSize, "Entity %d", e.index + 1);
    break;
  }
}

void DrawSelectionCyclingPanel(AppCommandState& cmd) {
  if (!cmd.showSelectionCyclingWindow)
    return;

  const int nCad    = static_cast<int>(cmd.selectionCycleEntities.size());
  const int nSurvey = static_cast<int>(cmd.selectionCycleSurveyPoints.size());
  const int total   = nCad + nSurvey;

  ImGui::SetNextWindowSize(ImVec2(280, 320), ImGuiCond_FirstUseEver);
  bool open = cmd.showSelectionCyclingWindow;
  if (!ImGui::Begin("Selection", &open, ImGuiWindowFlags_NoCollapse)) {
    cmd.showSelectionCyclingWindow = open;
    ImGui::End();
    return;
  }
  cmd.showSelectionCyclingWindow = open;

  char header[64];
  std::snprintf(header, sizeof(header), "%d item%s in list", total, total == 1 ? "" : "s");
  ImGui::TextDisabled("%s", header);
  if (ImGui::SmallButton("Refresh")) {
    cmd.selectionCycleEntities     = cmd.selection;
    cmd.selectionCycleSurveyPoints = cmd.selectedSurveyPointIndices;
  }
  ItemHelpTooltip("Re-snapshot the current selection into this list.");
  ImGui::Separator();

  const float footerH = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
  ImGui::BeginChild("##sel_list", ImVec2(0.f, -footerH), false);

  // CAD entities from the snapshot.
  for (int i = 0; i < nCad; ++i) {
    const SelectedEntity& e = cmd.selectionCycleEntities[static_cast<size_t>(i)];
    char label[128];
    FormatSelectedEntityLabel(cmd, e, label, sizeof(label));

    bool isSelected = std::any_of(cmd.selection.begin(), cmd.selection.end(), [&](const SelectedEntity& s) {
      return s.type == e.type && s.index == e.index;
    });
    ImGui::PushID(i);
    if (ImGui::Checkbox(label, &isSelected)) {
      if (isSelected) {
        cmd.selection.push_back(e);
      } else {
        cmd.selection.erase(std::remove_if(cmd.selection.begin(), cmd.selection.end(), [&](const SelectedEntity& s) {
          return s.type == e.type && s.index == e.index;
        }), cmd.selection.end());
      }
      EnsureAttrCounts(cmd);
      BumpCadGpuCache(cmd);
    }
    ImGui::PopID();
  }

  // Survey points from the snapshot.
  for (int i = 0; i < nSurvey; ++i) {
    const int spi = cmd.selectionCycleSurveyPoints[static_cast<size_t>(i)];
    char label[128];
    if (static_cast<size_t>(spi) < cmd.surveyPoints.size()) {
      const SurveyPoint& sp = cmd.surveyPoints[static_cast<size_t>(spi)];
      if (!sp.description.empty())
        std::snprintf(label, sizeof(label), "Survey Pt #%d (%s)", sp.id, sp.description.c_str());
      else
        std::snprintf(label, sizeof(label), "Survey Pt #%d", sp.id);
    } else {
      std::snprintf(label, sizeof(label), "Survey Pt %d", spi + 1);
    }

    bool isSelected = std::find(cmd.selectedSurveyPointIndices.begin(),
                                cmd.selectedSurveyPointIndices.end(), spi) !=
                      cmd.selectedSurveyPointIndices.end();
    ImGui::PushID(10000 + i);
    if (ImGui::Checkbox(label, &isSelected)) {
      if (isSelected) {
        cmd.selectedSurveyPointIndices.push_back(spi);
      } else {
        auto it = std::remove(cmd.selectedSurveyPointIndices.begin(),
                              cmd.selectedSurveyPointIndices.end(), spi);
        cmd.selectedSurveyPointIndices.erase(it, cmd.selectedSurveyPointIndices.end());
      }
    }
    ImGui::PopID();
  }

  ImGui::EndChild();

  ImGui::Separator();
  if (ImGui::Button("Deselect All")) {
    ClearCadSelection(cmd);
    BumpCadGpuCache(cmd);
  }
  ImGui::SameLine();
  if (ImGui::Button("Select All")) {
    for (const auto& e : cmd.selectionCycleEntities) {
      const bool already = std::any_of(cmd.selection.begin(), cmd.selection.end(), [&](const SelectedEntity& s) {
        return s.type == e.type && s.index == e.index;
      });
      if (!already)
        cmd.selection.push_back(e);
    }
    for (int spi : cmd.selectionCycleSurveyPoints) {
      if (std::find(cmd.selectedSurveyPointIndices.begin(),
                    cmd.selectedSurveyPointIndices.end(), spi) == cmd.selectedSurveyPointIndices.end())
        cmd.selectedSurveyPointIndices.push_back(spi);
    }
    EnsureAttrCounts(cmd);
    BumpCadGpuCache(cmd);
  }

  ImGui::End();
}

void DrawCreatePointsPanel(AppCommandState& cmd, std::vector<std::string>& log) {
  if (!cmd.showCreatePointsWindow)
    return;

  ImGui::SetNextWindowSize(ImVec2(420, 340), ImGuiCond_FirstUseEver);
  bool open = cmd.showCreatePointsWindow;
  if (!ImGui::Begin("Create points", &open)) {
    cmd.showCreatePointsWindow = open;
    ImGui::End();
    return;
  }
  cmd.showCreatePointsWindow = open;

  ImGui::TextDisabled("Click in the drawing to place points. Clicks on existing markers select them.");
  ImGui::Separator();

  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("Next point ID");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(100.f);
  ImGui::InputInt("##next_survey_id", &cmd.createPointsNextId);

  ImGui::Separator();

  CreatePointsOptions& o = cmd.createPointsOpts;
  ImGui::InputText("Layer##cp_layer", &o.layer);
  ImGui::InputTextMultiline("Description##cp_desc", &o.defaultDescription, ImVec2(-FLT_MIN, 60.f));
  ImGui::InputFloat("Elevation##cp_z", &o.defaultElevation);

  int pol = static_cast<int>(o.duplicatePolicy);
  if (ImGui::Combo("If ID exists##cp_dup", &pol,
                   "Notify (skip)\0Renumber (next free)\0Merge (update coords)\0Overwrite\0\0"))
    o.duplicatePolicy = static_cast<SurveyDuplicatePolicy>(pol);

  ImGui::Separator();
  static char pathBuf[512] = "gosurvey_points.json";
  ImGui::InputText("File##cp_file", pathBuf, sizeof(pathBuf));
  if (ImGui::Button("Save"))
    SaveSurveyPointsToJsonFile(cmd, pathBuf, log);
  ImGui::SameLine();
  if (ImGui::Button("Load"))
    LoadSurveyPointsFromJsonFile(cmd, pathBuf, log);

  ImGui::End();
}

void DrawLayerManagerWindow(AppCommandState& cmd, std::vector<std::string>* log) {
  std::vector<std::string> discard;
  if (!log)
    log = &discard;
  SyncDrawingLayerTableWithGeometry(cmd);
  if (!cmd.showLayerManagerWindow)
    return;

  ImGui::SetNextWindowSize(ImVec2(1040, 520), ImGuiCond_FirstUseEver);
  bool open = cmd.showLayerManagerWindow;
  if (!ImGui::Begin("Layer Manager", &open)) {
    cmd.showLayerManagerWindow = open;
    ImGui::End();
    return;
  }
  cmd.showLayerManagerWindow = open;

  ImGui::TextWrapped(
      "Layers group objects for display and DXF. New geometry uses the current layer from the ribbon (top right). "
      "Layer 0 cannot be renamed or deleted.");
  ImGui::Separator();

  static char newLayerBuf[160] = "NewLayer";
  ImGui::InputText("New layer name", newLayerBuf, IM_ARRAYSIZE(newLayerBuf));
  ImGui::SameLine();
  if (ImGui::Button("Add layer")) {
    std::string err;
    if (CadAddDrawingLayer(cmd, std::string(newLayerBuf), &err))
      log->push_back("Layer added: " + TrimUi(std::string(newLayerBuf)));
    else
      log->push_back("LAYER — " + err);
  }

  ImGui::Separator();
  const ImGuiTableFlags tflags =
      ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY;
  if (ImGui::BeginTable("laymgr", 11, tflags, ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 16.f))) {
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.16f);
    ImGui::TableSetupColumn("On", ImGuiTableColumnFlags_WidthFixed, 36.f);
    ImGui::TableSetupColumn("Freeze", ImGuiTableColumnFlags_WidthFixed, 52.f);
    ImGui::TableSetupColumn("Lock", ImGuiTableColumnFlags_WidthFixed, 44.f);
    ImGui::TableSetupColumn("Plot", ImGuiTableColumnFlags_WidthFixed, 40.f);
    ImGui::TableSetupColumn("Current", ImGuiTableColumnFlags_WidthFixed, 64.f);
    ImGui::TableSetupColumn("Color", ImGuiTableColumnFlags_WidthStretch, 0.12f);
    ImGui::TableSetupColumn("Linetype", ImGuiTableColumnFlags_WidthStretch, 0.11f);
    ImGui::TableSetupColumn("Lineweight", ImGuiTableColumnFlags_WidthStretch, 0.10f);
    ImGui::TableSetupColumn("Transparency", ImGuiTableColumnFlags_WidthStretch, 0.10f);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 72.f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 1.f));
    ImGui::TableHeadersRow();
    ImGui::PopStyleColor();

    for (size_t i = 0; i < cmd.drawingLayerTable.size();) {
      CadLayerRow& row = cmd.drawingLayerTable[i];
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::PushID(static_cast<int>(i));
      if (row.name == "0") {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("0");
      } else {
        char nmBuf[256];
        ImStrncpy(nmBuf, row.name.c_str(), IM_ARRAYSIZE(nmBuf));
        nmBuf[IM_ARRAYSIZE(nmBuf) - 1] = '\0';
        if (ImGui::InputText("##nm", nmBuf, IM_ARRAYSIZE(nmBuf))) {
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
          const std::string nn = TrimUi(std::string(nmBuf));
          if (!nn.empty() && nn != row.name) {
            std::string err;
            const std::string oldNm = row.name;
            if (!CadRenameDrawingLayer(cmd, oldNm, nn, &err))
              log->push_back("LAYER — " + err);
            else
              log->push_back("Layer renamed.");
          }
        }
      }
      ImGui::TableNextColumn();
      if (ImGui::Checkbox("##on", &row.on))
        BumpCadGpuCache(cmd);
      ImGui::TableNextColumn();
      if (ImGui::Checkbox("##fr", &row.frozen))
        BumpCadGpuCache(cmd);
      ImGui::TableNextColumn();
      if (ImGui::Checkbox("##lk", &row.locked))
        BumpCadGpuCache(cmd);
      ImGui::TableNextColumn();
      if (ImGui::Checkbox("##plot", &row.plottable))  // REQ-029/030: exclude from plots when off
        BumpCadGpuCache(cmd);
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Plottable — when off, this layer's geometry (and viewports on it) is excluded from plots.");
      ImGui::TableNextColumn();
      if (ImGui::RadioButton("##cur", cmd.currentLayer == row.name)) {
        cmd.currentLayer = row.name;
        SyncDrawingLayerTableWithGeometry(cmd);
      }

      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(-1);
      {
        char cprev[120];
        ImStrncpy(cprev, ColorStorageToPreviewLabel(row.color).c_str(), sizeof(cprev));
        cprev[sizeof(cprev) - 1] = '\0';
        if (ImGui::BeginCombo("##laycol", cprev)) {
          for (const auto& p : kNamedColors) {
            if (std::string(p.storage) == "ByLayer")
              continue;
            const bool sel = (row.color == p.storage);
            if (ImGui::Selectable(p.label, sel)) {
              row.color = p.storage;
              BumpCadGpuCache(cmd);
            }
            if (sel)
              ImGui::SetItemDefaultFocus();
          }
          ImGui::EndCombo();
        }
      }

      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(-1);
      {
        const int li = LayerLinetypeComboIndex(row.linetype);
        char lprev[64];
        std::snprintf(lprev, sizeof(lprev), "%s", kLayerLinetypeLabels[li]);
        if (ImGui::BeginCombo("##laylt", lprev)) {
          for (int j = 0; j < kLayerLinetypeCount; ++j) {
            const bool sel = (j == li);
            if (ImGui::Selectable(kLayerLinetypeLabels[j], sel)) {
              row.linetype = kLayerLinetypeStorage[j];
              BumpCadGpuCache(cmd);
            }
            if (sel)
              ImGui::SetItemDefaultFocus();
          }
          ImGui::EndCombo();
        }
      }

      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(-1);
      {
        const int wi = LineweightPresetIndexFromMm(row.lineweightMm);
        char wprev[64];
        SnprintLineweightPresetLabel(wprev, sizeof(wprev), row.lineweightMm, true);
        if (ImGui::BeginCombo("##laylw", wprev)) {
          for (int j = 0; j < kUiLineweightPresetCount; ++j) {
            char lab[64];
            SnprintLineweightPresetLabel(lab, sizeof(lab), kUiLineweightMmPresets[j], true);
            const bool sel = (j == wi);
            if (ImGui::Selectable(lab, sel)) {
              row.lineweightMm = kUiLineweightMmPresets[j];
              BumpCadGpuCache(cmd);
            }
            if (sel)
              ImGui::SetItemDefaultFocus();
          }
          ImGui::EndCombo();
        }
      }

      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(-1);
      {
        static constexpr float kLayTrans[] = {0.f, 0.25f, 0.5f, 0.75f, 0.9f, 1.f};
        static constexpr const char* kLayTransLab[] = {"0 %", "25 %", "50 %", "75 %", "90 %", "100 %"};
        constexpr int kNtr = static_cast<int>(sizeof(kLayTrans) / sizeof(kLayTrans[0]));
        int ti = 0;
        float bd = 1e9f;
        for (int j = 0; j < kNtr; ++j) {
          const float d = std::fabs(row.transparency - kLayTrans[j]);
          if (d < bd) {
            bd = d;
            ti = j;
          }
        }
        if (ImGui::BeginCombo("##laytr", kLayTransLab[ti])) {
          for (int j = 0; j < kNtr; ++j) {
            const bool sel = (j == ti);
            if (ImGui::Selectable(kLayTransLab[j], sel)) {
              row.transparency = kLayTrans[j];
              BumpCadGpuCache(cmd);
            }
            if (sel)
              ImGui::SetItemDefaultFocus();
          }
          ImGui::EndCombo();
        }
      }

      ImGui::TableNextColumn();
      if (row.name != "0") {
        if (ImGui::SmallButton("Delete")) {
          std::string err;
          if (!CadDeleteDrawingLayer(cmd, row.name, &err))
            log->push_back("LAYER — " + err);
          ImGui::PopID();
          continue;
        }
      } else {
        ImGui::TextDisabled("—");
      }

      ImGui::PopID();
      ++i;
    }
    ImGui::EndTable();
  }

  ImGui::Separator();
  ImGui::TextDisabled(
      "On / Freeze / Lock are stored for future visibility and editing rules; all layers still draw. "
      "Color, linetype, lineweight, and transparency apply to entities set to ByLayer / defaults.");

  ImGui::End();
}

// Settings panel implementation lives in CadUiSettings.cpp.

// DELETED: duplicate BoxBegin and DrawSettingsPanel — do not re-add here.


void DrawCloseConfirmModal(AppCommandState& cmd, std::vector<std::string>& log) {
  if (cmd.confirmCloseModal) {
    ImGui::OpenPopup("Unsaved Changes##closeconf");
    cmd.confirmCloseModal = false;
  }

  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  if (!ImGui::BeginPopupModal("Unsaved Changes##closeconf", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    return;

  // Collect dirty drawings: active doc first, then inactive snapshots.
  struct DirtyEntry { int idx; std::string name; };
  std::vector<DirtyEntry> dirty;
  if (cmd.cadGpuRevision != cmd.activeDocSavedRevision &&
      cmd.activeDrawingIdx < static_cast<int>(cmd.drawingTabs.size()))
    dirty.push_back({cmd.activeDrawingIdx, cmd.drawingTabs[cmd.activeDrawingIdx].name});
  for (int i = 0; i < static_cast<int>(cmd.documents.size()); ++i) {
    if (i == cmd.activeDrawingIdx) continue;
    if (cmd.documents[i].cadGpuRevision != cmd.documents[i].savedRevision &&
        i < static_cast<int>(cmd.drawingTabs.size()))
      dirty.push_back({i, cmd.drawingTabs[i].name});
  }

  if (dirty.empty()) {
    cmd.closeConfirmed = true;
    ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    return;
  }

  ImGui::TextUnformatted("The following drawings have unsaved changes:");
  ImGui::Spacing();
  for (const auto& e : dirty)
    ImGui::BulletText("%s", e.name.c_str());
  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  static char s_savePath[4096]{};

  if (ImGui::Button("Save All & Close", ImVec2(0, 0))) {
    bool allOk = true;
    for (const auto& e : dirty) {
      const bool isActive = (e.idx == cmd.activeDrawingIdx);
      if (!isActive) {
        // Temporarily bring this doc's data into cmd.
        SaveDocumentToSnapshot(cmd, cmd.activeDrawingIdx);
        RestoreDocumentFromSnapshot(cmd, e.idx);
      }
      std::string path = cmd.activeDocFilePath;
      if (path.empty()) {
        const std::string def = e.name + ".gs";
        if (BrowseSaveFileGsUtf8(s_savePath, sizeof(s_savePath), def.c_str()))
          path = s_savePath;
      }
      if (!path.empty() && SaveGoSurveyFile(cmd, path.c_str(), log)) {
        cmd.activeDocSavedRevision = cmd.cadGpuRevision;
        cmd.activeDocFilePath      = path;
        if (!isActive) {
          // Commit updated saved-revision back into the snapshot.
          SaveDocumentToSnapshot(cmd, e.idx);
        }
      } else {
        allOk = false;
      }
      if (!isActive)
        RestoreDocumentFromSnapshot(cmd, cmd.activeDrawingIdx);
    }
    if (allOk) {
      cmd.closeConfirmed = true;
      ImGui::CloseCurrentPopup();
    }
  }

  ImGui::SameLine();
  if (ImGui::Button("Close Without Saving", ImVec2(0, 0))) {
    cmd.closeConfirmed = true;
    ImGui::CloseCurrentPopup();
  }

  ImGui::SameLine();
  if (ImGui::Button("Cancel", ImVec2(0, 0)))
    ImGui::CloseCurrentPopup();

  ImGui::EndPopup();
}

void DrawAlignResultsWindow(AppCommandState& cmd, std::vector<std::string>& log) {
  if (!cmd.showAlignResultsWindow)
    return;

  ImGui::SetNextWindowSize(ImVec2(600, 480), ImGuiCond_FirstUseEver);
  bool open = cmd.showAlignResultsWindow;
  if (!ImGui::Begin("ALIGN — Helmert transformation", &open, ImGuiWindowFlags_NoCollapse)) {
    cmd.showAlignResultsWindow = open;
    ImGui::End();
    return;
  }
  cmd.showAlignResultsWindow = open;

  const auto& res = cmd.alignLastResult;

  // Solution summary
  if (res.valid) {
    ImGui::Text("Pairs: %d", res.nPairs);
    ImGui::SameLine(110.f);
    ImGui::Text("Scale: %.8f", static_cast<double>(res.scale));
    ImGui::Text("Rotation:     %s", FormatBearing(static_cast<double>(res.rotationCwNorthDeg), CadAngleDisplaySettings(cmd)).c_str());
    ImGui::Text("Translation:  X = %s   Y = %s",
                FormatLinear(static_cast<double>(res.tx), cmd.displayLinearPrecision).c_str(),
                FormatLinear(static_cast<double>(res.ty), cmd.displayLinearPrecision).c_str());
    ImGui::Text("Point error:  %s (avg. distance each source maps from its destination)",
                FormatLinear(static_cast<double>(res.rms), cmd.displayLinearPrecision).c_str());
  } else if (cmd.alignControlPts.empty()) {
    ImGui::TextColored(ImVec4(1.f, 0.5f, 0.5f, 1.f), "No pairs — add control pairs and solve again.");
  } else {
    ImGui::TextColored(ImVec4(1.f, 0.5f, 0.5f, 1.f), "Degenerate — pairs are coincident or collinear.");
  }

  ImGui::Separator();

  // Pairs table with per-row Remove button
  const float footerH = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y * 2.f;
  const float tableH  = std::max(60.f, ImGui::GetContentRegionAvail().y - footerH);
  const ImGuiTableFlags tf = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                             ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;
  int removeIdx = -1;
  if (ImGui::BeginTable("##align_pairs", 7, tf, ImVec2(0.f, tableH))) {
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("##rm",  ImGuiTableColumnFlags_WidthFixed,   26.f);
    ImGui::TableSetupColumn("Pair",  ImGuiTableColumnFlags_WidthFixed,   36.f);
    ImGui::TableSetupColumn("Src X", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Src Y", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Dst X", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Dst Y", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Resid", ImGuiTableColumnFlags_WidthFixed,   72.f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 1.f));
    ImGui::TableHeadersRow();
    ImGui::PopStyleColor();

    for (int i = 0; i < static_cast<int>(cmd.alignControlPts.size()); ++i) {
      const auto& cp    = cmd.alignControlPts[static_cast<size_t>(i)];
      const float resid = (res.valid && i < static_cast<int>(res.pairResiduals.size()))
                              ? res.pairResiduals[static_cast<size_t>(i)] : 0.f;
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::PushID(i);
      if (ImGui::SmallButton("-"))
        removeIdx = i;
      ImGui::PopID();
      const int pp = cmd.displayLinearPrecision;
      ImGui::TableNextColumn(); ImGui::Text("%d", i + 1);
      ImGui::TableNextColumn(); ImGui::TextUnformatted(FormatLinear(static_cast<double>(cp.srcX), pp).c_str());
      ImGui::TableNextColumn(); ImGui::TextUnformatted(FormatLinear(static_cast<double>(cp.srcY), pp).c_str());
      ImGui::TableNextColumn(); ImGui::TextUnformatted(FormatLinear(static_cast<double>(cp.dstX), pp).c_str());
      ImGui::TableNextColumn(); ImGui::TextUnformatted(FormatLinear(static_cast<double>(cp.dstY), pp).c_str());
      ImGui::TableNextColumn();
      if (res.valid) ImGui::TextUnformatted(FormatLinear(static_cast<double>(resid), pp).c_str());
      else ImGui::TextUnformatted("—");
    }
    ImGui::EndTable();
  }

  if (removeIdx >= 0) {
    cmd.alignControlPts.erase(cmd.alignControlPts.begin() + removeIdx);
    RecalcAlignResult(cmd);
  }

  ImGui::Separator();
  static bool s_applyScale = true;
  ImGui::Checkbox("Apply scale", &s_applyScale);
  ImGui::SameLine();
  const bool canApply = res.valid;
  if (!canApply)
    ImGui::BeginDisabled();
  if (ImGui::Button("Apply", ImVec2(90.f, 0.f)))
    ApplyAlignCommand(cmd, log, s_applyScale);
  if (!canApply)
    ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Close", ImVec2(70.f, 0.f))) {
    cmd.showAlignResultsWindow = false;
    cmd.alignControlPts.clear();
    cmd.alignPhase = AppCommandState::AlignPhase::PickSrc;
  }

  ImGui::End();
}

void DrawViewPointsPanel(AppCommandState& cmd, std::vector<std::string>& log) {
  if (!cmd.showViewPointsWindow)
    return;

  ImGui::SetNextWindowSize(ImVec2(960, 480), ImGuiCond_FirstUseEver);
  bool open = cmd.showViewPointsWindow;
  if (!ImGui::Begin("Viewpoints — survey database", &open)) {
    cmd.showViewPointsWindow = open;
    ImGui::End();
    return;
  }
  cmd.showViewPointsWindow = open;

  cmd.surveyPointIdBuffers.resize(cmd.surveyPoints.size());
  for (size_t i = 0; i < cmd.surveyPoints.size(); ++i) {
    if (cmd.surveyPointIdBuffers[i].empty())
      cmd.surveyPointIdBuffers[i] = std::to_string(cmd.surveyPoints[i].id);
  }

  ImGui::Text("%zu point(s)", cmd.surveyPoints.size());
  static char pathBuf[512] = "gosurvey_points.json";
  ImGui::InputText("File##vp_path", pathBuf, sizeof(pathBuf));
  if (ImGui::Button("Save##vp"))
    SaveSurveyPointsToJsonFile(cmd, pathBuf, log);
  ImGui::SameLine();
  if (ImGui::Button("Load##vp"))
    LoadSurveyPointsFromJsonFile(cmd, pathBuf, log);

  ImGui::Separator();

  const ImGuiTableFlags tf = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                             ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable;
  int pendingDelete = -1;
  if (ImGui::BeginTable("survey_pts", 7, tf, ImVec2(0.f, -ImGui::GetFrameHeightWithSpacing()))) {
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 64.f);
    ImGui::TableSetupColumn("Easting", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Northing", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Elev", ImGuiTableColumnFlags_WidthFixed, 84.f);
    ImGui::TableSetupColumn("Layer", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Del", ImGuiTableColumnFlags_WidthFixed, 56.f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 1.f));
    ImGui::TableHeadersRow();
	ImGui::PopStyleColor();

    for (size_t i = 0; i < cmd.surveyPoints.size(); ++i) {
      SurveyPoint& p = cmd.surveyPoints[i];
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::PushID(static_cast<int>(i));
      ImGui::SetNextItemWidth(96.f);
      ImGui::InputText("##id", &cmd.surveyPointIdBuffers[i]);
      if (ImGui::IsItemDeactivatedAfterEdit()) {
        std::string t = StringUtil::trimCopy(cmd.surveyPointIdBuffers[i]);
        char* end = nullptr;
        const long v = std::strtol(t.c_str(), &end, 10);
        const bool parsed =
            end == t.c_str() + static_cast<std::ptrdiff_t>(t.size()) && end != t.c_str();
        if (!parsed) {
          log.push_back("VIEWPOINTS — ID must be a whole number (no spaces or extra text).");
          cmd.surveyPointIdBuffers[i] = std::to_string(p.id);
        } else {
          const int nid = static_cast<int>(v);
          bool dup = false;
          for (size_t j = 0; j < cmd.surveyPoints.size(); ++j) {
            if (j != i && cmd.surveyPoints[j].id == nid)
              dup = true;
          }
          if (dup) {
            log.push_back("VIEWPOINTS — duplicate ID " + std::to_string(nid) + ".");
            cmd.surveyPointIdBuffers[i] = std::to_string(p.id);
          } else {
            p.id = nid;
            cmd.surveyPointIdBuffers[i] = std::to_string(nid);
          }
        }
        EnsureSurveyPointLabelMtext(cmd, i, &log);
      }
      ImGui::TableNextColumn();
      double de = static_cast<double>(CadCoord::WorldXFromLocal(cmd, p.easting));
      ImGui::InputDouble("##e", &de, 0., 0., DisplayFloatFmt(cmd.surveyPointDisplayPrecision).c_str());
      if (ImGui::IsItemDeactivatedAfterEdit()) {
        const double wy = static_cast<double>(CadCoord::WorldYFromLocal(cmd, p.northing));
        CadCoord::LocalFromWorld(cmd, de, wy, &p.easting, &p.northing);
        EnsureSurveyPointLabelMtext(cmd, i, &log);
      }
      ImGui::TableNextColumn();
      double dn = static_cast<double>(CadCoord::WorldYFromLocal(cmd, p.northing));
      ImGui::InputDouble("##n", &dn, 0., 0., DisplayFloatFmt(cmd.surveyPointDisplayPrecision).c_str());
      if (ImGui::IsItemDeactivatedAfterEdit()) {
        const double wx = static_cast<double>(CadCoord::WorldXFromLocal(cmd, p.easting));
        CadCoord::LocalFromWorld(cmd, wx, dn, &p.easting, &p.northing);
        EnsureSurveyPointLabelMtext(cmd, i, &log);
      }
      ImGui::TableNextColumn();
      double dz = static_cast<double>(p.elevation);
      ImGui::InputDouble("##z", &dz, 0., 0., DisplayFloatFmt(cmd.surveyPointDisplayPrecision).c_str());
      if (ImGui::IsItemDeactivatedAfterEdit()) {
        p.elevation = static_cast<float>(dz);
        EnsureSurveyPointLabelMtext(cmd, i, &log);
      }
      ImGui::TableNextColumn();
      ImGui::InputText("##layer", &p.layer);
      if (ImGui::IsItemDeactivatedAfterEdit()) {
        RepositionSurveyLabelMtextForPoint(cmd, i);
        BumpCadGpuCache(cmd);
      }
      ImGui::TableNextColumn();
      ImGui::InputTextMultiline("##desc", &p.description, ImVec2(-FLT_MIN, 52.f));
      if (ImGui::IsItemDeactivatedAfterEdit())
        EnsureSurveyPointLabelMtext(cmd, i, &log);
      ImGui::TableNextColumn();
      if (ImGui::SmallButton("X"))
        pendingDelete = static_cast<int>(i);
      ImGui::PopID();
    }
    ImGui::EndTable();
  }

  if (pendingDelete >= 0)
    RemoveSurveyPointAt(cmd, static_cast<size_t>(pendingDelete));

  ImGui::End();
}
