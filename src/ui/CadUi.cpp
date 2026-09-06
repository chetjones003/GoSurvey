#include "CadUi.hpp"
#include "CadUiInternal.hpp"
#include "CadUiChrome.hpp"
#include "CadBlocks.hpp"
#include "DevShellHooks.hpp"
// REQ-141 Analyze ribbon + contour label overlay.
#include "CadCoordinateFrame.hpp"
#include "ViewCube.hpp"
#include "UcsIcon.hpp"  // in-tree orientation widget (REQ-059)
#include "viewport/Crosshair3d.hpp"  // 3D crosshair axis projection (REQ-310)
#include "ViewportPickPolicy.hpp"
#include "MtextRichFormat.hpp"
#include "MtextToolbar.hpp"
#include "MtextTextOps.hpp"
#include "RichTextEdit.hpp"
#include "FontRegistry.hpp"
#include "ShxDraw.hpp"
#include "ColorContrast.hpp"

#include "CadLinetype.hpp"
#include "TextStyle.hpp"
#include "CadUiStyleWidgets.hpp"  // the shared colour / linetype / lineweight vocabulary
#include "HatchPattern.hpp"
#include "CommandBar.hpp"
#include "NumFormat.hpp"
#include "util/cadtable.hpp"
#include "DwgIo.hpp"
#include "DxfIo.hpp"
#include "AppIcon.hpp"
#include "GsIo.hpp"
#include "UserPrefs.hpp"
#include "ImGuiLayout.hpp"
#include "WinFileDialogs.hpp"
#include "SurveyPoints.hpp"
#include "SurfaceStyle.hpp"
#include "DimensionStyle.hpp"  // REQ-072 analysis legend (TASK-086 §6 (4))
#include "CadDimStroke.hpp"
#include "render/ViewportProjection.hpp"  // REQ-061: per-viewport camera projection
#include "CadFontName.hpp"
#include "StringUtil.hpp"
#include "imgui.h"

#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <chrono>
#include <set>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <functional>
#include <cfloat>
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cassert>

#include "HatchPat.hpp"

static void SubmitRibbonCommand(AppCommandState& cmd, std::vector<std::string>& log, const std::string& line) {
  assert(!line.empty());
  assert(line.size() < 4096);
  DevShell_OnCommand(line.c_str());
  std::vector<char> buf(line.begin(), line.end());
  buf.push_back('\0');
  ProcessCommandLineSubmit(buf.data(), static_cast<int>(buf.size()), cmd, log);
}

static void UiSubmitViewportPick(AppCommandState& cmd, float x, float y, std::vector<std::string>& log,
                                 bool windowSelectionSubtract = false, bool fenceLeftToRightWindowMode = false)
{
  DevShell_OnPick(x, y);
  SubmitViewportPick(cmd, x, y, log, windowSelectionSubtract, fenceLeftToRightWindowMode);
}

// Render a sample string in a text style's font/bold/italic, fit into box [tl, tl+sz] (REQ-044). Shared by
// the ribbon style flyout thumbnails and the Text Style dialog preview. Defined near the dialog below.
static void DrawTextStyleSample(ImDrawList* dl, ImVec2 tl, ImVec2 sz, const TextStyle& s, const char* sample,
                                ImU32 col);

// Hatch pattern library (REQ-043 follow-up): parse resources/hatches/acadiso.pat once and cache the
// definitions. Returns the cached list (empty if the file is missing). The .pat parser is pure + tested.
static const std::vector<hatchpat::Def>& HatchLibrary() {
  static std::vector<hatchpat::Def> defs;
  static bool loaded = false;
  if (!loaded) {
    loaded = true;
    const std::filesystem::path p =
        ResolveBundledAssetPath(std::filesystem::path("resources") / "hatches" / "acadiso.pat");
    if (!p.empty()) {
      std::ifstream f(p, std::ios::binary);
      if (f) {
        std::stringstream ss;
        ss << f.rdbuf();
        hatchpat::Parse(ss.str(), &defs);
      }
    }
  }
  return defs;
}

// Draw a small preview of a hatch pattern inside the rect [mn,mx]: a solid swatch for SOLID, otherwise the
// pattern's line family generated at a characteristic scale (~5 of its densest repeats) and clipped to the box.
static void DrawHatchThumbnail(ImDrawList* dl, ImVec2 mn, ImVec2 mx, const hatchpat::Def& def, ImU32 col) {
  const float pad = 3.f;
  const ImVec2 a(mn.x + pad, mn.y + pad), b(mx.x - pad, mx.y - pad);
  double minSp = 1e30;
  for (const auto& L : def.lines)
    if (std::fabs(L.dy) > 1e-6) minSp = std::min(minSp, std::fabs(L.dy));
  if (def.name == "SOLID" || def.lines.empty() || minSp > 1e29) {
    dl->AddRectFilled(a, b, col, 1.f);
    return;
  }
  const double S = minSp * 5.0;  // region side in pattern units → ~5 repeats of the densest family
  CadFilledRegion fr;
  // Pattern-preview swatch: a flat square at Z = 0, as x,y,z triplets (ADR-025 (a)).
  fr.vertsXyz = {0.f,
                 0.f,
                 0.f,
                 static_cast<float>(S),
                 0.f,
                 0.f,
                 static_cast<float>(S),
                 static_cast<float>(S),
                 0.f,
                 0.f,
                 static_cast<float>(S),
                 0.f};
  fr.loopStart = {0};
  fr.patternName = def.name;
  fr.patternScale = 1.f;
  std::vector<float> segs;
  hatchpattern::BuildSegments(fr, def, &segs);
  const float cw = b.x - a.x, ch = b.y - a.y;
  dl->PushClipRect(a, b, true);
  for (size_t s = 0; s + 3 < segs.size(); s += 4) {
    const float x0 = a.x + static_cast<float>(segs[s] / S) * cw;
    const float y0 = b.y - static_cast<float>(segs[s + 1] / S) * ch;  // flip Y to screen
    const float x1 = a.x + static_cast<float>(segs[s + 2] / S) * cw;
    const float y1 = b.y - static_cast<float>(segs[s + 3] / S) * ch;
    dl->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), col, 1.f);
  }
  dl->PopClipRect();
}

// True when a font name refers to an SHX stroke font (rendered from the real .shx, not a TrueType).
static bool CadIsShxFontName(const std::string& s) { return cadfont::IsShxFontName(s); }

static std::string CadDrawFontFamily(const std::string& stored) { return TextStyles::EffectiveFontFamily(stored); }

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

// ADR-023 removed the InputTextMultiline callback that used to cache the selection and up-case typed
// characters: RichTextEdit publishes the raw selection offsets itself and applies the ALL-CAPS toggle as
// it consumes the character queue. The two helpers below still serve the toolbar, which works in raw
// byte offsets exactly as before.

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

// sRGB hex → ImGui color. The palettes below are designed in hex and validated
// in hex (L* ladder, WCAG ratios), so they are written in hex here rather than
// as rounded floats nobody can check against the design.
static ImVec4 Hex(unsigned rgb, float a = 1.f) {
  return ImVec4(static_cast<float>((rgb >> 16) & 0xFFu) / 255.f,
                static_cast<float>((rgb >> 8) & 0xFFu) / 255.f,
                static_cast<float>(rgb & 0xFFu) / 255.f, a);
}
static ImU32 HexU32(unsigned rgb, int a = 255) {
  return IM_COL32((rgb >> 16) & 0xFFu, (rgb >> 8) & 0xFFu, rgb & 0xFFu, a);
}

// ---------------------------------------------------------------------------
// Theme chrome palette (REQ-081 / ADR-033)
// ---------------------------------------------------------------------------
// Colors for the parts of the shell this file paints itself, through ImDrawList
// or a PushStyleColor literal, rather than through a plain ImGuiCol_* lookup:
// the toolbar/ribbon band, the ribbon button bevel, the status bar, the command
// autocomplete popup, and the property grid. ImGui has no style slot meaning
// "toolbar band" or "property value cell", so these used to be file-scope
// constants in the classic theme's colors — which is why they rendered
// identically no matter which theme was active.
//
// One instance, written by whichever ApplyCad*Theme() runs, read everywhere
// else. Every theme entry point must fill EVERY field: a field left unwritten is
// a piece of the previous theme still on screen after the user switches themes.
static UiChrome g_chrome;

UiChrome& CadUiChrome()
{
  return g_chrome;
}

void ApplyCadDarkTheme() {
  // Hazel-editor look (REQ-081): a light-ish panel surface floating on a darker
  // ground, every panel outlined in something darker than both. That pairing —
  // not the hue — is what makes two docked panels tellable apart, and it is what
  // the previous palette lacked.
  ImGuiStyle& style = ImGui::GetStyle();
  ImVec4* colors = style.Colors;

  // Only FLOATING windows see WindowRounding — ImGui squares a window off when it
  // is docked — so a softer corner here reads on dialogs and popups without
  // touching the crisp seams the docked panels depend on.
  style.WindowRounding = 5.f;
  style.ChildRounding = 0.f;    // square children keep the seams between panels crisp
  style.FrameRounding = 2.5f;
  style.PopupRounding = 5.f;
  style.ScrollbarRounding = 2.f;
  style.GrabRounding = 2.f;
  style.TabRounding = 0.f;      // a squared selected tab reads as continuous with its panel
  style.WindowBorderSize = 1.f;
  style.ChildBorderSize = 1.f;
  style.PopupBorderSize = 1.f;
  style.FrameBorderSize = 0.f;  // fields are read by their recess, not by an outline
  style.TabBorderSize = 0.f;
  style.ScrollbarSize = 12.f;
  style.GrabMinSize = 10.f;
  style.WindowPadding = ImVec2(8, 8);
  style.FramePadding = ImVec2(6, 4);
  style.ItemSpacing = ImVec2(8, 6);
  style.CellPadding = ImVec2(6, 3);  // the property grid's rows need the air

  // -------------------------------------------------------------------------
  // Palette (REQ-081 revision 3, TASK-129: ladder lifted so chrome reads lighter).
  // Derived, not picked — the rules are:
  //
  //  1. Every neutral is ACHROMATIC (R = G = B). No surface carries a colour
  //     cast, so all chroma in the UI belongs to the accent and to the semantic
  //     triad, and anything coloured is therefore meaningful by construction.
  //     Revision 2 gave the neutrals a slight cool cast (H 220, S ~13%); the
  //     user asked for true neutral, and each tone below is the achromatic gray
  //     with the SAME luminance as its cool predecessor — so the ladder, every
  //     structural distance and every contrast ratio are unchanged (max drift
  //     0.14 L*). Do not "neutralise" a tone by dropping its blue channel: the
  //     luminance weights are 0.2126/0.7152/0.0722, so that shifts lightness.
  //  2. The neutral ladder steps on roughly even CIE L*. This is the fix that
  //     matters: the ramp before revision 2 put the border at L* 6.3 and the tab
  //     strip at L* 6.8 — 0.5 apart, i.e. no border at all where panels meet —
  //     while surface->group jumped 5.2. Even steps in L*, not in hex, because
  //     hex distance is not what the eye measures.
  //  3. The accent is ONE hue (37 deg) at several lightnesses and alphas. Warm
  //     marks on a neutral ground advance; that is what makes selection read
  //     instantly without shouting, and with rule 1 it is the ONLY warm thing
  //     on screen apart from the danger swatch.
  //  4. Text is held to measured contrast on the panel surface, not to taste.
  //  5. The semantic triad is equiluminant, so no member outranks the others.
  //
  // Every number below was validated before it was written; the ratios in the
  // comments are computed, not estimated.
  // -------------------------------------------------------------------------

  // Neutral ladder — achromatic, L* 13.2 / 16.1 / 19.4 / 21.7 / 26.2 / 30.6 / 35.7
  // (revision 3 ladder lifted so chrome is clearly lighter; step sizes held)
  const ImVec4 seam       = Hex(0x222222);  // L* 13.2  the gap between panels; darker than any surface
  const ImVec4 field      = Hex(0x282828);  // L* 16.1  recessed input / property value cell
  const ImVec4 ground     = Hex(0x2F2F2F);  // L* 19.4  app ground: dockspace, menu bar, status bar
  const ImVec4 titlebar   = Hex(0x343434);  // L* 21.7  title bar + tab strip (unselected tabs)
  const ImVec4 surface    = Hex(0x3E3E3E);  // L* 26.2  panel surface — the reference plane
  const ImVec4 raised     = Hex(0x484848);  // L* 30.6  raised: group header bar, button face
  const ImVec4 hover      = Hex(0x545454);  // L* 35.7  hover
  // Structural distances this buys, in L*:
  //   panel over ground 6.8 | seam under ground 6.2 | field under panel 10.1
  //   header over panel 4.4 | panel over tab strip 4.5
  const ImVec4 fieldHi    = Hex(0x2B2B2B);  // field, hovered
  const ImVec4 fieldOn    = Hex(0x333333);  // field, being edited
  const ImVec4 rule       = Hex(0x4B4B4B);  // table gridline — 5.7 L* over the surface, reads as a rule
  // Boxed / scrolling regions inside a window sit one step BELOW the window, so
  // a scroll box reads as a well cut into the dialog rather than as more dialog.
  // One step down (not two) keeps the recessed fields inside it — which are two
  // steps down at #282828 — still clearly recessed against it.
  const ImVec4 inset      = titlebar;       // #343434, L* 21.7: 4.5 under the surface

  // Text — measured on the panel surface (#3E3E3E)
  const ImVec4 text       = Hex(0xD7D7D7);  //  7.43:1  AAA
  const ImVec4 textDim    = Hex(0xB6B6B6);  //  5.27:1  AA. Lifted with the surface so secondary
                                            //  content (hints, derived readouts, command hints)
                                            //  keeps a similar margin above 4.5:1.

  // Accent — H 37, one hue at three lightnesses; the only warm family here
  const ImVec4 accentHi   = Hex(0xF0C67C);  //  9.18:1  marks on dark: check marks, tab overline
  const ImVec4 accent     = Hex(0xE0AE5E);  //  7.29:1  fills and active states
  //   ... and #C08F43 (5.11:1) is the ladder's pressed step. Its only present-day
  //   use is the mode-toggle buttons in PushModeToggleButtonColors, so it is
  //   written there rather than kept as an unused constant here.
  const ImVec4 accentWash = Hex(0xE0AE5E, 0.13f);  // hovered row
  const ImVec4 accentWash2= Hex(0xE0AE5E, 0.22f);  // selected row

  // Semantic triad — equiluminant within 1.7 L*, each carrying #F2F2F2 at >= 4.5:1
  const ImVec4 danger     = Hex(0xB34A4A);  // L* 45.6  H   0   4.70:1
  const ImVec4 success    = Hex(0x3E7643);  // L* 44.8  H 127   4.84:1
  const ImVec4 info       = Hex(0x3C6FB5);  // L* 46.5  H 215   4.54:1
  const ImVec4 infoText   = Hex(0x6BA5E0);  //  5.68:1  the same info hue, lightened to be readable AS text

  colors[ImGuiCol_Text]                  = text;
  colors[ImGuiCol_TextDisabled]          = textDim;
  colors[ImGuiCol_WindowBg]              = surface;
  colors[ImGuiCol_ChildBg]               = inset;
  colors[ImGuiCol_PopupBg]               = surface;
  colors[ImGuiCol_Border]                = seam;
  colors[ImGuiCol_BorderShadow]          = ImVec4(0.f, 0.f, 0.f, 0.f);
  colors[ImGuiCol_FrameBg]               = field;
  colors[ImGuiCol_FrameBgHovered]        = fieldHi;
  colors[ImGuiCol_FrameBgActive]         = fieldOn;
  colors[ImGuiCol_TitleBg]               = titlebar;
  // One ladder step up, so the focused dialog's title bar reads as live without
  // the panel-flash the classic theme's note warns about — ImGui paints a docked
  // node's tab strip with this too, and a *contrasting* colour here makes every
  // panel flare as focus moves. One step is a hint; a hue would be a flare.
  colors[ImGuiCol_TitleBgActive]         = raised;
  colors[ImGuiCol_TitleBgCollapsed]      = titlebar;
  colors[ImGuiCol_MenuBarBg]             = ground;
  colors[ImGuiCol_ScrollbarBg]           = ground;
  colors[ImGuiCol_ScrollbarGrab]         = raised;
  colors[ImGuiCol_ScrollbarGrabHovered]  = hover;
  colors[ImGuiCol_ScrollbarGrabActive]   = Hex(0x606060);
  colors[ImGuiCol_CheckMark]             = accentHi;
  colors[ImGuiCol_SliderGrab]            = hover;
  colors[ImGuiCol_SliderGrabActive]      = accent;
  colors[ImGuiCol_Button]                = raised;
  colors[ImGuiCol_ButtonHovered]         = hover;
  colors[ImGuiCol_ButtonActive]          = titlebar;  // pressed sinks below the surface
  colors[ImGuiCol_Header]                = raised;
  colors[ImGuiCol_HeaderHovered]         = accentWash;   // a warm wash on the hovered row
  colors[ImGuiCol_HeaderActive]          = accentWash2;
  colors[ImGuiCol_Separator]             = seam;
  colors[ImGuiCol_SeparatorHovered]      = hover;
  colors[ImGuiCol_SeparatorActive]       = accent;
  colors[ImGuiCol_ResizeGrip]            = Hex(0xE0AE5E, 0.16f);
  colors[ImGuiCol_ResizeGripHovered]     = Hex(0xE0AE5E, 0.47f);
  colors[ImGuiCol_ResizeGripActive]      = accent;
  // issue #183 follow-up: titlebar (21.7 L*) vs surface (26.2 L*) was only a
  // 4.5 L* gap — the same distance as "header over panel", which reads fine
  // for a bar with its own label but was too close once dialogs gained their
  // own gradient body (D-2026-09-01-a), making the whole tab row look like one
  // flat strip. `seam` widens the unselected tab to a full 13 L* below the
  // active one so the row reads as tabs, not wallpaper; the active tab keeps
  // `surface` so it still merges into the (now gradient) body it belongs to.
  colors[ImGuiCol_Tab]                   = seam;
  colors[ImGuiCol_TabHovered]            = raised;
  colors[ImGuiCol_TabActive]             = surface;  // selected tab == its panel, so the two read as one piece
  colors[ImGuiCol_TabUnfocused]          = seam;
  colors[ImGuiCol_TabUnfocusedActive]    = surface;
  // Keep the overline accent explicitly — ImGui may copy HeaderActive into it and ours changed.
  colors[ImGuiCol_TabSelectedOverline]        = accentHi;
  colors[ImGuiCol_TabDimmedSelectedOverline]  = Hex(0xE0AE5E, 0.40f);
  colors[ImGuiCol_TextSelectedBg]        = Hex(0xE0AE5E, 0.30f);
  colors[ImGuiCol_NavHighlight]          = accent;
  colors[ImGuiCol_DragDropTarget]        = infoText;
  colors[ImGuiCol_ModalWindowDimBg]      = Hex(0x222222, 0.65f);  // the dim is the seam colour, not black
  // Tables were never set here, so the property grid drew ImGui's stock blue-gray
  // borders under this theme however dark the rest of the panel got.
  colors[ImGuiCol_TableHeaderBg]         = raised;
  colors[ImGuiCol_TableBorderStrong]     = seam;
  colors[ImGuiCol_TableBorderLight]      = rule;
  colors[ImGuiCol_TableRowBg]            = ImVec4(0.f, 0.f, 0.f, 0.f);
  colors[ImGuiCol_TableRowBgAlt]         = ImVec4(1.f, 1.f, 1.f, 0.02f);
  colors[ImGuiCol_DockingPreview]        = Hex(0xE0AE5E, 0.35f);
  colors[ImGuiCol_DockingEmptyBg]        = ground;

  // Chrome, drawn from the same ladder so the hand-painted parts sit on the
  // same steps as the ImGui-painted parts (that is the whole point of ADR-033).
  g_chrome.bandFace        = HexU32(0x343434);  // toolbar band: one step under the panels
  g_chrome.bandHilite      = HexU32(0x545454);
  g_chrome.bandShadow      = HexU32(0x222222);
  g_chrome.bandSunken      = HexU32(0x282828);  // pressed reads as a recess, like a field
  g_chrome.bandRaised      = HexU32(0x484848);  // hover lifts to the group-header step
  g_chrome.statusBarFace   = HexU32(0x2F2F2F);  // status bar sits on the ground
  g_chrome.statusStripFace = HexU32(0x2F2F2F);
  g_chrome.panelFill       = HexU32(0x3E3E3E);  // == surface, so empty space is still the panel
  g_chrome.propValueBg     = HexU32(0x282828);  // == field, so a value cell is a recess
  g_chrome.headerFaceL     = HexU32(0x484848);  // flat bar: both ends the same
  g_chrome.headerFaceR     = HexU32(0x484848);
  g_chrome.headerHoverL    = HexU32(0x545454);
  g_chrome.headerHoverR    = HexU32(0x545454);
  g_chrome.headerText      = HexU32(0xE0E0E0);  // a touch brighter than body text — it is a title
  g_chrome.headerEdgeTop   = HexU32(0x000000, 0);  // no bevel; the bar is flat
  g_chrome.headerEdgeBot   = HexU32(0x222222);
  g_chrome.headerGlyphBg   = HexU32(0x000000, 0);
  g_chrome.headerGlyphEdge = HexU32(0x000000, 0);
  g_chrome.headerGlyph     = HexU32(0xB6B6B6);
  g_chrome.headerBoxGlyph  = false;                // disclosure triangle, at the leading edge
  g_chrome.popupFace       = HexU32(0x3E3E3E);
  g_chrome.popupBorder     = HexU32(0x222222);
  g_chrome.plateHilite     = IM_COL32(255, 255, 255, 20);  // a hint of light, not a visible white line
  g_chrome.plateShadow     = IM_COL32(0, 0, 0, 115);       // fades to 0 over kPlateShadowPx
  g_chrome.windowShadow    = IM_COL32(0, 0, 0, 150);       // innermost ring; fades out over 12px
  // REQ-081 rev 7 — dialog-only depth, dialled down from the Start screen's own
  // gradient/card language. Window fill brackets `surface` by one ladder step
  // each way rather than reusing raised/titlebar outright, so a dialog with no
  // buttons still reads as subtly lit without stealing either neighbour's tone.
  g_chrome.dlgWindowFillTop      = HexU32(0x454545);
  g_chrome.dlgWindowFillBottom   = HexU32(0x363636);
  g_chrome.dlgBtnFaceTop         = HexU32(0x565656);
  g_chrome.dlgBtnFaceBottom      = HexU32(0x3C3C3C);
  g_chrome.dlgBtnFaceHoverTop    = HexU32(0x616161);
  g_chrome.dlgBtnFaceHoverBottom = HexU32(0x454545);
  g_chrome.dlgBtnFaceSunkenTop    = HexU32(0x333333);  // inverted gradient direction reads as pressed
  g_chrome.dlgBtnFaceSunkenBottom = HexU32(0x3E3E3E);
  g_chrome.dlgBtnBevelLight      = HexU32(0x707070);
  g_chrome.dlgBtnBevelDark       = HexU32(0x1C1C1C);
  g_chrome.axisBadges      = true;
  g_chrome.axisX           = ImGui::ColorConvertFloat4ToU32(danger);
  g_chrome.axisY           = ImGui::ColorConvertFloat4ToU32(success);
  g_chrome.axisZ           = ImGui::ColorConvertFloat4ToU32(info);
  g_chrome.axisText        = HexU32(0xF2F2F2);
  g_chrome.ribbonPanelRule = IM_COL32(0, 0, 0, 26);
  g_chrome.ribbonPanelTitle = HexU32(0xC7C7C7);
  g_chrome.ribbonTabOn         = HexU32(0xE0AE5E);
  g_chrome.ribbonTabOnHovered  = HexU32(0xF0C67C);
  g_chrome.ribbonTabOnActive   = HexU32(0xC08F43);
  g_chrome.ribbonTabOnText     = HexU32(0x161616);
  g_chrome.ribbonCtxTab        = IM_COL32(0, 120, 215, 255);
  g_chrome.ribbonCtxTabDim     = IM_COL32(0, 120, 215, 180);
  g_chrome.ribbonCtxTabHovered = IM_COL32(30, 144, 255, 255);
  g_chrome.ribbonCtxTabActive  = IM_COL32(0, 90, 180, 255);
  g_chrome.ribbonCtxTabText    = IM_COL32(255, 255, 255, 255);
  g_chrome.ribbonTabPadY       = 5.f;
  g_chrome.ribbonTabStripGapY  = 4.f;
  g_chrome.ribbonBottomGutter  = 12.f;
  g_chrome.ribbonTitleH        = 20.f;
  g_chrome.ribbonBodyFontScale = 0.80f;
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

  // REQ-081/ADR-033: these are the literals the hand-painted chrome used to carry
  // inline, moved here verbatim so this theme renders exactly as it did before.
  // Do not "tidy" them toward the palette constants above — the point of copying
  // them unchanged is that this branch cannot regress.
  g_chrome.bandFace        = IM_COL32( 70,  70,  70, 255);  // #464646 toolbar band
  g_chrome.bandHilite      = IM_COL32( 86,  86,  86, 255);  // #565656 top-left bevel
  g_chrome.bandShadow      = IM_COL32( 32,  32,  32, 255);  // #202020 bottom-right bevel
  g_chrome.bandSunken      = IM_COL32( 58,  58,  58, 255);  // #3A3A3A pressed face
  g_chrome.bandRaised      = IM_COL32(240, 240, 235, 255);  // raised face, lighter than the band
  g_chrome.statusBarFace   = IM_COL32( 30,  30,  30, 255);
  g_chrome.statusStripFace = IM_COL32( 70,  70,  70, 255);
  g_chrome.panelFill       = IM_COL32( 70,  70,  70, 255);
  g_chrome.propValueBg     = IM_COL32( 45,  45,  45, 255);  // #2D2D2D recessed value cell
  g_chrome.headerFaceL     = IM_COL32( 48,  72, 104, 255);  // steel-blue gradient, left
  g_chrome.headerFaceR     = IM_COL32( 60,  92, 134, 255);  // ... right
  g_chrome.headerHoverL    = IM_COL32( 58,  88, 128, 255);
  g_chrome.headerHoverR    = IM_COL32( 78, 118, 168, 255);
  g_chrome.headerText      = IM_COL32(229, 231, 235, 255);
  g_chrome.headerEdgeTop   = IM_COL32(255, 255, 255,  60);
  g_chrome.headerEdgeBot   = IM_COL32(  0,   0,   0, 120);
  g_chrome.headerGlyphBg   = IM_COL32(255, 255, 255, 255);
  g_chrome.headerGlyphEdge = IM_COL32( 70,  90, 120, 255);
  g_chrome.headerGlyph     = IM_COL32( 20,  50,  95, 255);
  g_chrome.headerBoxGlyph  = true;
  g_chrome.popupFace       = IM_COL32( 70,  70,  70, 255);
  g_chrome.popupBorder     = IM_COL32(115, 115, 115, 255);
  // No cast shadows: this theme already states elevation with its 3D bevels, and
  // adding a second, contradictory depth cue would read as grime.
  g_chrome.plateHilite     = IM_COL32(0, 0, 0, 0);
  g_chrome.plateShadow     = IM_COL32(0, 0, 0, 0);
  g_chrome.windowShadow    = IM_COL32(0, 0, 0, 0);
  // REQ-081 rev 7 — same dialog-only depth as Dark, in this theme's own palette.
  // Window fill brackets `face` by the classic ladder's own two neighbours
  // (face/faceDk already used for panel vs. button); primary buttons pick up
  // the steel-blue accent already used for headers/selection, so OK/Apply reads
  // as the one 3D, "press me" surface in the dialog instead of another gray box.
  g_chrome.dlgWindowFillTop      = ImGui::ColorConvertFloat4ToU32(face);
  g_chrome.dlgWindowFillBottom   = ImGui::ColorConvertFloat4ToU32(faceDk);
  g_chrome.dlgBtnFaceTop         = ImGui::ColorConvertFloat4ToU32(steelHi);
  g_chrome.dlgBtnFaceBottom      = ImGui::ColorConvertFloat4ToU32(steel);
  g_chrome.dlgBtnFaceHoverTop    = ImGui::ColorConvertFloat4ToU32(capBlue);
  g_chrome.dlgBtnFaceHoverBottom = ImGui::ColorConvertFloat4ToU32(steelHi);
  g_chrome.dlgBtnFaceSunkenTop    = ImGui::ColorConvertFloat4ToU32(steel);     // inverted gradient direction
  g_chrome.dlgBtnFaceSunkenBottom = ImGui::ColorConvertFloat4ToU32(steelHi);   // reads as pressed
  g_chrome.dlgBtnBevelLight      = ImGui::ColorConvertFloat4ToU32(hilite);
  g_chrome.dlgBtnBevelDark       = ImGui::ColorConvertFloat4ToU32(dkShadow);
  // nanoCAD 5 has no axis badges, and this theme is a reproduction of it —
  // REQ-081's "the Light theme renders exactly as it does today" wins here.
  g_chrome.axisBadges      = false;
  g_chrome.axisX = g_chrome.axisY = g_chrome.axisZ = g_chrome.axisText = 0;
  g_chrome.ribbonPanelRule     = IM_COL32(0, 0, 0, 26);
  g_chrome.ribbonPanelTitle    = IM_COL32(160, 160, 160, 255);
  g_chrome.ribbonTabOn         = IM_COL32(59, 130, 246, 255);
  g_chrome.ribbonTabOnHovered  = IM_COL32(79, 144, 250, 255);
  g_chrome.ribbonTabOnActive   = IM_COL32(46, 110, 212, 255);
  g_chrome.ribbonTabOnText     = IM_COL32(229, 231, 235, 255);
  g_chrome.ribbonCtxTab        = IM_COL32(0, 120, 215, 255);
  g_chrome.ribbonCtxTabDim     = IM_COL32(0, 120, 215, 180);
  g_chrome.ribbonCtxTabHovered = IM_COL32(30, 144, 255, 255);
  g_chrome.ribbonCtxTabActive  = IM_COL32(0, 90, 180, 255);
  g_chrome.ribbonCtxTabText    = IM_COL32(255, 255, 255, 255);
  g_chrome.ribbonTabPadY       = 5.f;
  g_chrome.ribbonTabStripGapY  = 4.f;
  g_chrome.ribbonBottomGutter  = 12.f;
  g_chrome.ribbonTitleH        = 20.f;
  g_chrome.ribbonBodyFontScale = 0.80f;
}

// ---------------------------------------------------------------------------
// Surface rollover readout (REQ-089)
// ---------------------------------------------------------------------------
/// How long the cursor must rest before the readout appears.
///
/// Fixed rather than a setting, by decision D-2026-08-23-a (2): a persisted value would be a
/// data-format change for a number nobody tunes. Half a second is long enough that dragging the
/// cursor across a drawing never raises it and short enough that deliberately pointing at a surface
/// does not feel like waiting.
constexpr double kSurfaceRolloverDwellSec = 0.5;

/// How far the cursor may drift and still count as resting.
///
/// **Not zero.** A mouse physically at rest still reports sub-pixel jitter on a high-resolution
/// device, and a zero tolerance would restart the dwell every frame and the readout would never
/// appear at all. Pinned by `HoverDwellTests`.
constexpr float kSurfaceRolloverMoveTolPx = 2.f;

// ---------------------------------------------------------------------------
// Elevation cues (REQ-081)
// ---------------------------------------------------------------------------
// How far a cast shadow reaches before it has faded out.
constexpr float kPlateShadowPx = 9.f;

/// Draw the latched rollover readout beside the cursor (REQ-089), or nothing if there is nothing to
/// say.
///
/// **An ImGui tooltip rather than a hand-painted plate**, which is the whole reason this function is
/// short. A tooltip window already takes its background and border from the active theme and already
/// clamps itself to the monitor when the cursor is near an edge — so no `UiChrome` field is added,
/// and REQ-081's standing hazard (a theme that forgets to fill a chrome field leaves stale colours
/// behind) is avoided by not creating another field to forget.
///
/// Purely presentational: every string was formatted by `BuildSurfaceHoverRows` when the cursor came
/// to rest, so this reads `cmd` and computes nothing.
static void DrawSurfaceRolloverReadout(const AppCommandState& cmd) {
  if (cmd.surfaceHoverRows.empty())
    return;

  // Guarded, not assumed: BeginTooltip returns false when ImGui declines to open the window, and
  // EndTooltip must not be called then. The unguarded form appears elsewhere in this file; the
  // guarded one is the form the rest of the tooltips here use, and it is the correct one.
  if (!ImGui::BeginTooltip())
    return;

  // The value column is aligned by measuring the widest label once rather than by a table. A table
  // inside an auto-resizing window has sizing rules of its own, and four rows of two strings do not
  // need them; `SameLine` at a measured offset stays aligned in any font, which is the only thing
  // the table was buying.
  const float valueX = ImGui::CalcTextSize("Elevation").x + ImGui::GetStyle().ItemSpacing.x * 2.f;

  for (size_t i = 0; i < cmd.surfaceHoverRows.size(); ++i) {
    // One block per covering surface — REQ-089's "two overlapping visible surfaces produce one block
    // each, both named", which is the existing-vs-proposed case REQ-074 already reports on.
    if (i > 0)
      ImGui::Separator();

    const SurfaceHoverRow& row = cmd.surfaceHoverRows[i];
    ImGui::TextUnformatted("Tin Surface");
    ImGui::Spacing();

    const auto field = [valueX](const char* label, const std::string& value) {
      // The label is the quiet half — it is the same four words every time, and the value beside it
      // is what the user came here to read.
      ImGui::TextDisabled("%s", label);
      ImGui::SameLine(valueX);
      ImGui::TextUnformatted(value.c_str());
    };
    field("Name", row.name);
    field("Style", row.style);
    field("Layer", row.layer);
    field("Elevation", row.elevation);
  }
  ImGui::EndTooltip();
}

// ---------------------------------------------------------------------------
// REQ-072 analysis legend (TASK-086 §6 (4))
// ---------------------------------------------------------------------------

/// A double trimmed of trailing zeros, for a legend row — deliberately NOT `SurfaceStyles::FormatFt`
/// (`commands/SurfaceStyle.hpp`), which is a feet-specific name and would read wrong beside a
/// percent-grade band.
static std::string FormatLegendNumber(double v) {
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%.4f", v);
  std::string s(buf);
  while (!s.empty() && s.back() == '0')
    s.pop_back();
  if (!s.empty() && s.back() == '.')
    s.pop_back();
  return s;
}

/// The resolved RGBA for one REQ-072 band colour string, folding the ByLayer chain exactly as
/// `ResolveSurfaceStoredColorRgba` does in `CadCommands.cpp` — duplicated rather than shared because
/// that copy lives in that file's anonymous namespace and this legend is the first UI-side reader of
/// a raw \ref SurfaceBand::color; both are three calls to the same public resolvers
/// (`FindDrawingLayerRowCi` / `ResolveEntityRgbaForViewport` / `ResolveStoredColorForViewport`), so a
/// third copy would be the point to share instead.
static void ResolveSurfaceBandLegendRgba(const AppCommandState& cmd, const EntityAttributes& surfAttr,
                                         const std::string& colorStorage, float* outRgba) {
  const CadLayerRow* layer = FindDrawingLayerRowCi(cmd, surfAttr.layer);
  float surfaceRgba[4] = {1.f, 1.f, 1.f, 1.f};
  ResolveEntityRgbaForViewport(surfAttr, layer, 0.42f, 0.62f, 0.78f, surfaceRgba);
  ResolveStoredColorForViewport(colorStorage, surfAttr.transparency < 0.f ? 0.f : surfAttr.transparency,
                                surfaceRgba[0], surfaceRgba[1], surfaceRgba[2], outRgba);
  outRgba[3] = surfaceRgba[3];
}

/// Draw REQ-072's on-screen legend for every visible surface whose style has banding on
/// (`analysisMode != None`), stacked upward from the viewport's bottom-left corner.
///
/// **Reads the style's range table directly — never a copy of it.** This function and
/// \ref BuildSurfaceAnalysisGeometry (`CadCommands.cpp`) are "one source, two readers" of the same
/// `SurfaceStyle::bands`, which is what makes REQ-072's "the legend's displayed ranges equal the
/// table's, and change with it" true by construction rather than by two things that can drift apart.
static void DrawSurfaceAnalysisLegend(const AppCommandState& cmd, ImVec2 imgPos, ImVec2 avail) {
  if (cmd.activeSpaceIndex != kModelSpaceIndex)
    return;  // a sheet has no surfaces of its own to show a legend for (REQ-025 (g))

  ImDrawList* dl = ImGui::GetWindowDrawList();
  constexpr float kPad = 8.f;
  constexpr float kSwatch = 14.f;
  constexpr float kRowH = 18.f;
  constexpr float kBoxW = 200.f;
  constexpr float kGap = 8.f;

  float bottom = imgPos.y + avail.y - 10.f;  // stacks upward; each surface's box sits above the last

  for (size_t si = 0; si < cmd.cadSurfaces.size(); ++si) {
    if (!SurfaceVisible(cmd, si))
      continue;
    const CadSurface& surf = cmd.cadSurfaces[si];
    const SurfaceStyle* style = SurfaceStyles::Resolve(cmd.surfaceStyles, surf.styleName);
    const SurfaceStyle resolved = style ? *style : SurfaceStyles::StandardSurfaceStyle();
    if (resolved.analysisMode == SurfaceAnalysisMode::None)
      continue;
    if (si >= cmd.cadSurfaceAttrs.size())
      continue;
    const EntityAttributes& surfAttr = cmd.cadSurfaceAttrs[si];

    const bool byElevation = resolved.analysisMode == SurfaceAnalysisMode::Elevation;
    const bool byDirection = resolved.analysisMode == SurfaceAnalysisMode::Direction;
    const bool bySlopeAngle = resolved.analysisMode == SurfaceAnalysisMode::SlopeAngle;
    const char* unit = byElevation ? "ft" : (byDirection || bySlopeAngle ? "deg" : "%");
    // Title row + one row per band + one overflow row (only when there is a table to overflow).
    const size_t dataRows = resolved.bands.size() + (resolved.bands.empty() ? 0 : 1);
    const float boxH = (1.f + static_cast<float>(dataRows)) * kRowH + kPad * 2.f;
    const ImVec2 boxMin(imgPos.x + 10.f, bottom - boxH);
    const ImVec2 boxMax(imgPos.x + 10.f + kBoxW, bottom);
    bottom = boxMin.y - kGap;

    dl->AddRectFilled(boxMin, boxMax, IM_COL32(24, 24, 24, 205), 4.f);
    dl->AddRect(boxMin, boxMax, IM_COL32(95, 95, 95, 255), 4.f, 0, 1.f);

    float ty = boxMin.y + kPad;
    const std::string title = surf.name + " - " + (byElevation ? "Elevation" : (byDirection ? "Direction" : (bySlopeAngle ? "Slope angle" : "Slope")));
    dl->AddText(ImVec2(boxMin.x + kPad, ty), IM_COL32(230, 230, 230, 255), title.c_str());
    ty += kRowH;

    const auto row = [&](const std::string& label, const float rgba[4]) {
      const ImU32 fill = IM_COL32(static_cast<int>(rgba[0] * 255.f), static_cast<int>(rgba[1] * 255.f),
                                  static_cast<int>(rgba[2] * 255.f), 255);
      const ImVec2 sMin(boxMin.x + kPad, ty + 2.f);
      const ImVec2 sMax(sMin.x + kSwatch, sMin.y + kSwatch);
      dl->AddRectFilled(sMin, sMax, fill);
      dl->AddRect(sMin, sMax, IM_COL32(20, 20, 20, 255));
      dl->AddText(ImVec2(sMax.x + 6.f, ty), IM_COL32(220, 220, 220, 255), label.c_str());
      ty += kRowH;
    };

    // The lowest band is open at the bottom (AssignBand, util/surfaceanalysis.hpp) — labelled "<",
    // matching that rule rather than implying a lower edge the table does not have.
    double prevBound = 0.0;
    bool havePrev = false;
    for (const SurfaceBand& b : resolved.bands) {
      float rgba[4];
      ResolveSurfaceBandLegendRgba(cmd, surfAttr, b.color, rgba);
      const std::string label = (havePrev ? FormatLegendNumber(prevBound) + " - " + FormatLegendNumber(b.upperBound)
                                          : "< " + FormatLegendNumber(b.upperBound)) +
                                " " + unit;
      row(label, rgba);
      prevBound = b.upperBound;
      havePrev = true;
    }
    if (havePrev) {
      // The overflow bucket BuildSurfaceAnalysisGeometry draws in the plain Triangles colour — the
      // legend shows the same colour so a triangle beyond the table's top is still explained, not a
      // colour the legend never accounted for.
      float rgba[4];
      ResolveSurfaceBandLegendRgba(cmd, surfAttr, resolved.triangles.color, rgba);
      row("> " + FormatLegendNumber(prevBound) + " " + unit, rgba);
    }
  }
}

/// Draw the sub-object rollover beside the cursor (REQ-318 item 14) — what a `Ctrl` click would
/// take, and the properties of the solid it belongs to.
///
/// **Nothing is latched, unlike \ref DrawSurfaceRolloverReadout**, and for the same reason
/// \ref DrawSurveyPointRolloverReadout is not: the pick that supplies the answer has already run
/// this frame for the pre-highlight, so there is no expensive query to defer and nothing to hold
/// across frames. Only the DWELL is borrowed from REQ-089 — a panel that tracks the cursor
/// continuously covers the very geometry being picked, which is exactly the complaint that put a
/// rest timer on the surface readout.
///
/// Purely presentational: `BuildSubObjectHoverRow` resolved every string, so this reads and formats.
static void DrawSubObjectRolloverReadout(const AppCommandState& cmd) {
  if (!cmd.subObjectHoverValid)
    return;
  SubObjectHoverRow row;
  if (!BuildSubObjectHoverRow(cmd, cmd.subObjectHover, &row))
    return;  // the reference no longer resolves — say nothing rather than describe a stale solid

  // Guarded, as the surface readout's own note explains: BeginTooltip returns false when ImGui
  // declines to open the window, and EndTooltip must not be called then.
  if (!ImGui::BeginTooltip())
    return;

  // Same measured-offset alignment the surface readout uses, against this panel's widest label.
  const float valueX = ImGui::CalcTextSize("Linetype").x + ImGui::GetStyle().ItemSpacing.x * 2.f;
  ImGui::TextUnformatted(row.title.c_str());
  ImGui::Spacing();
  const auto field = [valueX](const char* label, const std::string& value) {
    ImGui::TextDisabled("%s", label);
    ImGui::SameLine(valueX);
    ImGui::TextUnformatted(value.c_str());
  };
  field("Solid", row.solid);
  field("Color", row.color);
  field("Layer", row.layer);
  field("Linetype", row.linetype);
  ImGui::EndTooltip();
}

/// Draw the survey-point rollover readout beside the cursor (REQ-090), for the point at
/// `cmd.surveyPoints[ix]`.
///
/// **Nothing here is latched, unlike \ref DrawSurfaceRolloverReadout.** The pick that supplies `ix`
/// (`PickSurveyPointAtCursor`) already runs every frame for the existing hover highlight, so there is
/// no expensive query to defer — the five rows are formatted fresh on every call. See TASK-089 §2 for
/// why this does not reuse REQ-089's one-shot latch: latching an index across frames would be
/// architecture §11.9's blocking finding, since `surveyPoints` compacts on erase.
///
/// Called only when a survey point is what precedence chose (REQ-090: "a survey point takes
/// precedence over a surface") — never in the same frame as \ref DrawSurfaceRolloverReadout, so this
/// is still the one `BeginTooltip` the surface readout's comment calls for.
static void DrawSurveyPointRolloverReadout(const AppCommandState& cmd, int ix) {
  if (ix < 0 || static_cast<size_t>(ix) >= cmd.surveyPoints.size())
    return;
  const SurveyPoint& p = cmd.surveyPoints[static_cast<size_t>(ix)];

  if (!ImGui::BeginTooltip())
    return;

  const float valueX = ImGui::CalcTextSize("Elevation").x + ImGui::GetStyle().ItemSpacing.x * 2.f;
  const auto field = [valueX](const char* label, const std::string& value) {
    ImGui::TextDisabled("%s", label);
    ImGui::SameLine(valueX);
    ImGui::TextUnformatted(value.c_str());
  };

  ImGui::TextUnformatted("Survey Point");
  ImGui::Spacing();
  const int sprec = cmd.surveyPointDisplayPrecision;
  // Northing/easting resolve through the same WorldXFromLocal/WorldYFromLocal conversion, at the same
  // precision, the Properties panel applies to the same point (CadUi.cpp ~4483-4499) — REQ-090's
  // acceptance condition that the two must agree. Elevation is absolute (not rebased): the
  // local-storage rebase is X/Y-only (architecture §11 / CadCoordinateFrame).
  field("Number", std::to_string(p.id));
  field("Layer", p.layer);
  field("Northing", FormatLinear(static_cast<double>(CadCoord::WorldYFromLocal(cmd, p.northing)), sprec));
  field("Easting", FormatLinear(static_cast<double>(CadCoord::WorldXFromLocal(cmd, p.easting)), sprec));
  field("Elevation", FormatLinear(static_cast<double>(p.elevation), sprec));
  ImGui::EndTooltip();
}

/// A raised plate catches the light along its top edge. Call with the plate's
/// own rect; draws a 1px line just inside the top.
///
/// Both of these push their own clip rect. A window's draw list is clipped to
/// its CONTENT region, which excludes exactly the window-padding band these
/// marks live in — without this they are computed, submitted, and then clipped
/// away, which looks identical to not drawing them at all.
void PlateTopHilite(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx) {
  if ((g_chrome.plateHilite >> IM_COL32_A_SHIFT) == 0 || mx.x <= mn.x)
    return;
  dl->PushClipRect(ImVec2(mn.x, mn.y), ImVec2(mx.x, mn.y + 2.f), false);
  dl->AddLine(ImVec2(mn.x, mn.y + 0.5f), ImVec2(mx.x, mn.y + 0.5f), g_chrome.plateHilite, 1.f);
  dl->PopClipRect();
}

/// Soft drop shadow drawn OUTSIDE a floating window's rect, as concentric
/// rounded outlines fading to nothing. Concentric outlines rather than four
/// gradient bands because the corners come out right for free, and a dozen
/// 1px rects is not a cost worth a cleverer shape.
static void DrawWindowDropShadow(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, float rounding) {
  const ImU32 base = g_chrome.windowShadow;
  const int a0 = static_cast<int>((base >> IM_COL32_A_SHIFT) & 0xFFu);
  if (a0 == 0 || mx.x <= mn.x || mx.y <= mn.y)
    return;
  constexpr int   kSteps = 12;
  constexpr float kDrop  = 2.f;  // light comes from the top, so the halo sits slightly low
  dl->PushClipRect(ImVec2(mn.x - kSteps - 2.f, mn.y - kSteps - 2.f),
                   ImVec2(mx.x + kSteps + 2.f, mx.y + kSteps + kDrop + 2.f), false);
  for (int i = kSteps; i >= 1; --i) {
    const float t = static_cast<float>(i) / static_cast<float>(kSteps);
    const int a = static_cast<int>(static_cast<float>(a0) * (1.f - t) * (1.f - t));  // quadratic falloff
    if (a <= 0)
      continue;
    const ImU32 c = (base & ~IM_COL32_A_MASK) | (static_cast<ImU32>(a) << IM_COL32_A_SHIFT);
    const float f = static_cast<float>(i);
    dl->AddRect(ImVec2(mn.x - f, mn.y - f + kDrop), ImVec2(mx.x + f, mx.y + f + kDrop), c,
                rounding + f, 0, 1.f);
  }
  dl->PopClipRect();
}

/// The other half of the same cue: the surface BELOW a raised plate receives the
/// shadow it casts. Call with the receiving surface's rect and say which of its
/// edges have a plate over them — the shadow is drawn inside that rect, fading
/// inward, so it lands on the lower surface where a real shadow would.
///
/// It has to be drawn LAST in the receiving window (over its content, not under
/// it): ImGui renders each dock node as its own window, so a shadow drawn by the
/// plate would be painted over by the very panel meant to receive it.
static void CastShadowInto(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, bool fromTop, bool fromLeft) {
  const ImU32 s = g_chrome.plateShadow;
  if ((s >> IM_COL32_A_SHIFT) == 0)
    return;
  const ImU32 clear = s & ~IM_COL32_A_MASK;  // same colour, zero alpha — fades, never greys
  const float d = std::min(kPlateShadowPx, std::min(mx.x - mn.x, mx.y - mn.y) * 0.5f);
  if (d <= 0.f)
    return;
  dl->PushClipRect(mn, mx, false);
  if (fromTop)
    dl->AddRectFilledMultiColor(mn, ImVec2(mx.x, mn.y + d), s, s, clear, clear);
  if (fromLeft)
    dl->AddRectFilledMultiColor(mn, ImVec2(mn.x + d, mx.y), s, clear, clear, s);
  dl->PopClipRect();
}

// REQ-081 revision 7 — one shared dialog-body gradient, painted right after
// Begin() so it lands BEHIND whatever content the caller submits next (draw
// lists are append-only, so timing is what keeps this off the widgets). This
// is per-dialog opt-in, unlike DrawFloatingWindowChrome's automatic shadow
// pass: the shadow generalises to every floating window with no per-dialog
// judgement call, but "which windows are dialogs that want this much depth"
// is exactly the kind of call the issue asked to start with a named list
// (Settings, Layer Manager, Viewpoints, Import/Export points, PDF Attach) and
// grow deliberately — see the migration checklist in CadUi.hpp.
void BeginStyledDialog() {
  if ((g_chrome.dlgWindowFillTop | g_chrome.dlgWindowFillBottom) == 0)
    return;  // theme opts out (kept symmetric with DrawFloatingWindowChrome's own opt-out)
  ImGuiWindow* w = ImGui::GetCurrentWindow();
  if (!w)
    return;
  const float titleH = w->TitleBarHeight;
  const ImVec2 mn(w->Pos.x, w->Pos.y + titleH);
  const ImVec2 mx(w->Pos.x + w->Size.x, w->Pos.y + w->Size.y);
  if (mx.x <= mn.x || mx.y <= mn.y)
    return;
  w->DrawList->AddRectFilledMultiColor(mn, mx, g_chrome.dlgWindowFillTop, g_chrome.dlgWindowFillTop,
                                        g_chrome.dlgWindowFillBottom, g_chrome.dlgWindowFillBottom);
}

// A 3D-bevelled button for dialog primary/secondary actions (REQ-081 rev 7):
// gradient face, lit top-left edge, dark bottom-right edge, inset on press.
// Drawn the same way DrawRibbonButtonBevel is — over an ItemAdd'd rect via
// ButtonBehavior, not a themed ImGui::Button — because a flat-colour Button
// cannot paint a gradient face. `primary` picks the accented gradient fields
// above; secondary buttons reuse the existing ribbon bevel (bandFace/bandRaised/
// bandSunken/bandHilite/bandShadow), which is already the "quieter version of
// the same" bevel language the issue asked for, with nothing new to fill in.
bool StyledButton(const char* label, const ImVec2& sizeArg, bool primary) {
  ImGuiWindow* window = ImGui::GetCurrentWindow();
  if (window->SkipItems)
    return false;
  const ImGuiID id = window->GetID(label);
  const ImVec2 labelSize = ImGui::CalcTextSize(label, nullptr, true);
  const ImGuiStyle& style = ImGui::GetStyle();
  const ImVec2 size = ImGui::CalcItemSize(sizeArg, labelSize.x + style.FramePadding.x * 2.f,
                                           labelSize.y + style.FramePadding.y * 2.f);
  const ImVec2 pos = window->DC.CursorPos;
  const ImRect bb(pos, ImVec2(pos.x + size.x, pos.y + size.y));
  ImGui::ItemSize(size, style.FramePadding.y);
  if (!ImGui::ItemAdd(bb, id))
    return false;
  bool hovered = false, held = false;
  const bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
  ImGui::RenderNavCursor(bb, id);

  ImDrawList* dl = window->DrawList;
  const bool sunken = held && hovered;
  ImU32 faceTop, faceBot, bevelTL, bevelBR;
  if (primary) {
    faceTop = sunken ? g_chrome.dlgBtnFaceSunkenTop : (hovered ? g_chrome.dlgBtnFaceHoverTop : g_chrome.dlgBtnFaceTop);
    faceBot = sunken ? g_chrome.dlgBtnFaceSunkenBottom : (hovered ? g_chrome.dlgBtnFaceHoverBottom : g_chrome.dlgBtnFaceBottom);
    bevelTL = sunken ? g_chrome.dlgBtnBevelDark : g_chrome.dlgBtnBevelLight;
    bevelBR = sunken ? g_chrome.dlgBtnBevelLight : g_chrome.dlgBtnBevelDark;
  } else {
    faceTop = faceBot = sunken ? g_chrome.bandSunken : (hovered ? g_chrome.bandRaised : g_chrome.bandFace);
    bevelTL = sunken ? g_chrome.bandShadow : g_chrome.bandHilite;
    bevelBR = sunken ? g_chrome.bandHilite : g_chrome.bandShadow;
  }
  dl->AddRectFilledMultiColor(bb.Min, bb.Max, faceTop, faceTop, faceBot, faceBot);
  dl->AddLine(ImVec2(bb.Min.x, bb.Min.y + 0.5f), ImVec2(bb.Max.x - 1.f, bb.Min.y + 0.5f), bevelTL, 1.f);
  dl->AddLine(ImVec2(bb.Min.x + 0.5f, bb.Min.y), ImVec2(bb.Min.x + 0.5f, bb.Max.y - 1.f), bevelTL, 1.f);
  dl->AddLine(ImVec2(bb.Min.x, bb.Max.y - 0.5f), ImVec2(bb.Max.x, bb.Max.y - 0.5f), bevelBR, 1.f);
  dl->AddLine(ImVec2(bb.Max.x - 0.5f, bb.Min.y), ImVec2(bb.Max.x - 0.5f, bb.Max.y), bevelBR, 1.f);

  const ImVec2 shift = sunken ? ImVec2(1.f, 1.f) : ImVec2(0.f, 0.f);  // pressed face reads as sinking in
  const char* labelDisplayEnd = ImGui::FindRenderedTextEnd(label);  // stop at "##id", like ImGui::Button
  ImGui::RenderTextClipped(ImVec2(bb.Min.x + shift.x, bb.Min.y + shift.y),
                            ImVec2(bb.Max.x + shift.x, bb.Max.y + shift.y),
                            label, labelDisplayEnd, &labelSize, style.ButtonTextAlign, &bb);
  return pressed;
}

// The surface-dialog property grids (Create Surface, Surface Properties) used to
// hard-code a pale-yellow Civil-3D row fill in both themes. In the Dark theme
// that put low-contrast light text on near-white, which the user asked to
// replace with a plain white "paper" sheet — the look of a property sheet
// dropped into the dialog: a light-gray cell fill, pure-white edit fields with
// a visible 1 px border, and dark text in the body. The header row keeps the
// theme's dark strip + light text (it is not "paper"), so the body text colour
// is pushed separately, after TableHeadersRow — see PushPropertyPaperBodyText.
// The classic theme keeps its cream grid and dark fields unchanged (REQ-081:
// "the Light theme renders exactly as it does today"): every value below
// resolves to the current style colour for it, so the push is a no-op there.
//
// Call order: PushPropertyPaperColors → BeginTable → TableSetupColumn(s) →
// TableHeadersRow → PushPropertyPaperBodyText → rows → PopPropertyPaperBodyText
// → EndTable → PopPropertyPaperColors. Pushes 6 colours + 1 style var.
void PushPropertyPaperColors(int themeIdx) {
  const bool dark = (themeIdx == 0);
  const ImVec4 rowBg   = dark ? ImVec4(0.90f, 0.90f, 0.90f, 1.f) : ImVec4(1.f, 0.97f, 0.82f, 1.f);
  const ImVec4 rowAlt  = dark ? ImVec4(0.90f, 0.90f, 0.90f, 1.f) : ImVec4(1.f, 0.99f, 0.90f, 1.f);
  const ImVec4 frame   = dark ? ImVec4(1.00f, 1.00f, 1.00f, 1.f) : ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
  const ImVec4 frameHi = dark ? ImVec4(0.93f, 0.95f, 1.00f, 1.f) : ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered);
  const ImVec4 frameAc = dark ? ImVec4(0.88f, 0.92f, 1.00f, 1.f) : ImGui::GetStyleColorVec4(ImGuiCol_FrameBgActive);
  const ImVec4 border  = dark ? ImVec4(0.45f, 0.45f, 0.45f, 1.f) : ImGui::GetStyleColorVec4(ImGuiCol_Border);
  const float  bsize   = dark ? 1.f : ImGui::GetStyle().FrameBorderSize;
  ImGui::PushStyleColor(ImGuiCol_TableRowBg, rowBg);
  ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, rowAlt);
  ImGui::PushStyleColor(ImGuiCol_FrameBg, frame);
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, frameHi);
  ImGui::PushStyleColor(ImGuiCol_FrameBgActive, frameAc);
  ImGui::PushStyleColor(ImGuiCol_Border, border);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, bsize);
}

void PopPropertyPaperColors() {
  ImGui::PopStyleVar(1);
  ImGui::PopStyleColor(6);
}

// Dark body text for the white "paper" rows — pushed AFTER TableHeadersRow so
// the dark header strip keeps its light label text. Also darkens the InputText
// caret: `ImGuiCol_InputTextCursor` is seeded from `ImGuiCol_Text` ONCE when the
// theme is built, so a runtime PushStyleColor on Text alone leaves the caret at
// the dark theme's light colour — invisible on a white field. No-op in classic.
void PushPropertyPaperBodyText(int themeIdx) {
  const bool dark = (themeIdx == 0);
  const ImVec4 text = dark ? ImVec4(0.11f, 0.11f, 0.11f, 1.f) : ImGui::GetStyleColorVec4(ImGuiCol_Text);
  const ImVec4 caret = dark ? ImVec4(0.06f, 0.06f, 0.06f, 1.f) : ImGui::GetStyleColorVec4(ImGuiCol_InputTextCursor);
  ImGui::PushStyleColor(ImGuiCol_Text, text);
  ImGui::PushStyleColor(ImGuiCol_InputTextCursor, caret);
}

void PopPropertyPaperBodyText() { ImGui::PopStyleColor(2); }

void DrawFloatingWindowChrome() {
  if ((g_chrome.windowShadow >> IM_COL32_A_SHIFT) == 0)
    return;
  // Runs after every window has been submitted and before ImGui::Render(), so
  // that no dialog has to opt in: settings, import points, attach PDF, edit
  // points, the traverse editor, the save-before-close modal and every menu or
  // combo popup get the same treatment from one place, and a dialog added later
  // is covered the day it is written.
  //
  // Appending to a window's OWN draw list is what makes the layering right: draw
  // lists are emitted in window order, so the halo lands over whatever is behind
  // that window and under any window above it. A shared background or foreground
  // list would put every shadow at one depth and get both of those wrong.
  ImGuiContext& g = *GImGui;
  for (ImGuiWindow* w : g.Windows) {
    if (!w || !w->WasActive || w->Hidden)
      continue;
    if (w->Flags & ImGuiWindowFlags_ChildWindow)
      continue;
    if (w->DockIsActive || w->DockNodeAsHost)
      continue;  // docked panels state their elevation with CastShadowInto instead
    // A title bar means "dialog"; the popup/tooltip flags catch menus and combos.
    // Everything else at top level is app furniture that paints its own edges —
    // the dockspace host, the status-bar strip, the floating command bar — and a
    // halo around those would trace a rect the user cannot see.
    const bool isPopup = (w->Flags & (ImGuiWindowFlags_Popup | ImGuiWindowFlags_Tooltip)) != 0;
    const bool hasTitleBar = (w->Flags & ImGuiWindowFlags_NoTitleBar) == 0;
    if (!isPopup && !hasTitleBar)
      continue;
    const ImVec2 mn = w->Pos;
    const ImVec2 mx(w->Pos.x + w->Size.x, w->Pos.y + w->Size.y);
    DrawWindowDropShadow(w->DrawList, mn, mx, w->WindowRounding);
    PlateTopHilite(w->DrawList, mn, mx);
  }
}

// ---------------------------------------------------------------------------
// Spreadsheet-style data grids (Viewpoints, Layer Manager)
// ---------------------------------------------------------------------------
// What stops an ImGui table reading as a spreadsheet is not the table — it is the
// widgets in it. A default InputText carries its own filled, rounded frame and
// sits inside the cell with padding around it, so a grid of them reads as a
// column of little boxes. In a spreadsheet the CELL is the control: the widget
// fills it edge to edge, has no frame of its own at rest, and the structure comes
// from the table's own gridlines and row banding. A frame appears only on hover
// and while editing, which is exactly the affordance Sheets gives you.
//
// Push before the row widgets, pop after. Each widget still needs
// ImGui::SetNextItemWidth(-FLT_MIN) so it fills its column.
void PushGridCellStyle() {
  // Read the frame colour BEFORE pushing over it — GetStyleColorVec4 returns the
  // current (already-pushed) value, so reading it after would make the active
  // cell transparent too and an edited cell would show no recess at all.
  const ImVec4 frame = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
  const ImVec4 hovered = ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.f);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.f, 2.f));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.f, 2.f));
  ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.f, 0.f, 0.f, 0.f));  // the row shows through
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, hovered);
  ImGui::PushStyleColor(ImGuiCol_FrameBgActive, frame);
}
void PopGridCellStyle() {
  ImGui::PopStyleColor(3);
  ImGui::PopStyleVar(4);
}

/// A checkbox inside a grid cell: centred in its column, and with its box drawn
/// even when unchecked. PushGridCellStyle makes frames transparent so that text
/// cells read as cells — which erases an unchecked checkbox completely, since a
/// checkbox IS its frame. This puts the recess back for the tick boxes only.
static bool GridCheckbox(const char* id, bool* v) {
  ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::ColorConvertU32ToFloat4(g_chrome.propValueBg));
  ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
  const float avail = ImGui::GetContentRegionAvail().x;
  const float box = ImGui::GetFrameHeight();
  if (avail > box)
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - box) * 0.5f);
  const bool changed = ImGui::Checkbox(id, v);
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();
  return changed;
}

/// Same treatment for the single-choice marker (the "Current" column).
static bool GridRadio(const char* id, bool active) {
  ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::ColorConvertU32ToFloat4(g_chrome.propValueBg));
  const float avail = ImGui::GetContentRegionAvail().x;
  const float box = ImGui::GetFrameHeight();
  if (avail > box)
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - box) * 0.5f);
  const bool clicked = ImGui::RadioButton(id, active);
  ImGui::PopStyleColor();
  return clicked;
}


// ---------------------------------------------------------------------------
// nanoCAD-style property grid helpers
// ---------------------------------------------------------------------------

// Shared flags for all 2-column property tables: full gridlines, transparent rows.
// Rows are transparent so the panel face shows through the LABEL column, giving
// the two-tone look automatically; only value cells are painted.
static constexpr ImGuiTableFlags kPropTableFlags =
    ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Borders;

// Paint the value cell (column 1) of the current table row as a recess.
// Call once per row (any time while that row is current). The label column is
// left transparent so the panel face shows through.
void PropValueCellBg() {
  ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, g_chrome.propValueBg, 1);
}

// Fill the panel's empty area below the last item with the panel face (the grid
// above keeps its own background for the label column), so inactive space reads
// as quiet panel rather than as whatever is behind the window.
void FillPropPanelEmpty() {
  ImGuiWindow* win = ImGui::GetCurrentWindow();
  if (!win) return;
  const float top = win->DC.CursorPos.y;
  const float bottom = win->Pos.y + win->Size.y;
  if (bottom <= top) return;
  win->DrawList->AddRectFilled(ImVec2(win->Pos.x, top),
                               ImVec2(win->Pos.x + win->Size.x, bottom),
                               g_chrome.panelFill);
}

// nanoCAD-style collapsible section header: blue gradient bar, navy bold-ish text,
// a [-]/[+] box on the right, and 3D highlight/shadow edges. Replaces
// ImGui::CollapsingHeader for property sections. Open state persists per-window.
bool PropSectionHeader(const char* label) {
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
  // Bar face. The classic theme wants a steel-blue gradient; the Hazel theme sets
  // both ends to the same tone, which makes this a flat fill for free.
  const ImU32 cL = hovered ? g_chrome.headerHoverL : g_chrome.headerFaceL;
  const ImU32 cR = hovered ? g_chrome.headerHoverR : g_chrome.headerFaceR;
  dl->AddRectFilledMultiColor(bb.Min, bb.Max, cL, cR, cR, cL);
  // Edges: a highlight above and a shadow below (classic 3D), or just the lower
  // rule (Hazel) — an edge color with zero alpha draws nothing.
  dl->AddLine(ImVec2(bb.Min.x, bb.Min.y), ImVec2(bb.Max.x, bb.Min.y), g_chrome.headerEdgeTop);
  dl->AddLine(ImVec2(bb.Min.x, bb.Max.y - 1), ImVec2(bb.Max.x, bb.Max.y - 1), g_chrome.headerEdgeBot);

  const float fontSz = ImGui::GetFontSize();
  float textX = bb.Min.x + 6.f;
  if (g_chrome.headerBoxGlyph) {
    // Classic: a [-]/[+] box at the trailing edge.
    const float boxSz = fontSz * 0.62f;
    const ImVec2 boxC(bb.Max.x - boxSz, bb.Min.y + h * 0.5f);
    const ImRect box(ImVec2(boxC.x - boxSz * 0.5f, boxC.y - boxSz * 0.5f),
                     ImVec2(boxC.x + boxSz * 0.5f, boxC.y + boxSz * 0.5f));
    dl->AddRectFilled(box.Min, box.Max, g_chrome.headerGlyphBg);
    dl->AddRect(box.Min, box.Max, g_chrome.headerGlyphEdge);
    const float my = (box.Min.y + box.Max.y) * 0.5f;
    dl->AddLine(ImVec2(box.Min.x + 2, my), ImVec2(box.Max.x - 2, my), g_chrome.headerGlyph); // minus
    if (!open) {
      const float mx = (box.Min.x + box.Max.x) * 0.5f;
      dl->AddLine(ImVec2(mx, box.Min.y + 2), ImVec2(mx, box.Max.y - 2), g_chrome.headerGlyph); // → plus
    }
  } else {
    // Hazel: a disclosure triangle at the LEADING edge, pointing down when open.
    const float t = fontSz * 0.34f;
    const ImVec2 c(bb.Min.x + 8.f + t, bb.Min.y + h * 0.5f);
    if (open)
      dl->AddTriangleFilled(ImVec2(c.x - t, c.y - t * 0.6f), ImVec2(c.x + t, c.y - t * 0.6f),
                            ImVec2(c.x, c.y + t * 0.7f), g_chrome.headerGlyph);
    else
      dl->AddTriangleFilled(ImVec2(c.x - t * 0.6f, c.y - t), ImVec2(c.x + t * 0.7f, c.y),
                            ImVec2(c.x - t * 0.6f, c.y + t), g_chrome.headerGlyph);
    textX = c.x + t + 8.f;
  }

  dl->AddText(ImVec2(textX, bb.Min.y + (h - fontSz) * 0.5f), g_chrome.headerText, label);
  (void)g;
  return open;
}

void SetupMainDockLayout(ImGuiID dockspace_id, const ImVec2& dock_host_size, bool reserveCommandDock) {
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
  // REQ-040: only reserve the bottom dock for the classic docked command line. With the
  // floating bar (default) the bottom strip would otherwise sit empty.
  if (reserveCommandDock)
    ImGui::DockBuilderSplitNode(dock_center, ImGuiDir_Down, 0.30f, &dock_bottom, &dock_center);

  ImGui::DockBuilderDockWindow("Reports", dock_left);
  ImGui::DockBuilderDockWindow("Properties", dock_left);
  ImGui::DockBuilderDockWindow("TOOLSPACE", dock_left);  // last → selected on first layout (REQ-142)
  if (reserveCommandDock)
    ImGui::DockBuilderDockWindow("Command line", dock_bottom);
  ImGui::DockBuilderDockWindow("Viewports", dock_center);

  ImGui::DockBuilderFinish(dockspace_id);
}

void SaveActiveDocument(AppCommandState& cmd, std::vector<std::string>& log) {
  char dwgPath[4096]{};
  const std::string& path = cmd.activeDocFilePath;
  if (!path.empty()) {
    if (SaveDrawingDocument(cmd, path.c_str(), log)) {
      cmd.activeDocSavedRevision = cmd.cadGpuRevision;
      RecordRecentDrawing(cmd, path);
    }
    return;
  }
  // No path yet (a New drawing, or one opened by file association — see BUG-027). Browse,
  // then ADOPT the destination: the tab takes the file's name and every later save is silent, which
  // is what makes a second Ctrl+S mean "save" rather than "ask again".
  if (!BrowseSaveFileDwgUtf8(dwgPath, sizeof(dwgPath), "drawing.dwg"))
    return;
  if (!SaveDrawingDocument(cmd, dwgPath, log))
    return;
  cmd.activeDocSavedRevision = cmd.cadGpuRevision;
  cmd.activeDocFilePath      = std::string(dwgPath);
  if (cmd.activeDrawingIdx < static_cast<int>(cmd.drawingTabs.size()))
    cmd.drawingTabs[cmd.activeDrawingIdx].name = std::filesystem::path(dwgPath).stem().string();
  RecordRecentDrawing(cmd, cmd.activeDocFilePath);
}

// REQ-055 / REQ-308: append a fresh empty drawing tab after the Start tab and focus it. Shared by
// File ▸ New, the tab bar's "+", and the Start screen's New button so they cannot drift apart.
void NewDrawingInTab(AppCommandState& cmd, std::vector<std::string>& log) {
  SaveDocumentToSnapshot(cmd, cmd.activeDrawingIdx);
  const int newIdx = static_cast<int>(cmd.drawingTabs.size());
  cmd.drawingTabs.push_back({"Drawing " + std::to_string(cmd.nextDrawingNumber++), cmd.nextTabUid++});
  cmd.documents.emplace_back();
  RestoreDocumentFromSnapshot(cmd, newIdx);  // load empty state into cmd
  LoadBundledBlockLibrary(cmd, log);
  cmd.activeDrawingIdx        = newIdx;
  cmd.prevDrawingIdx          = newIdx;  // tell main.cpp the switch already happened
  cmd.pendingDrawingTabSwitch = true;
  cmd.pendingViewportFocus    = true;
}

// REQ-055 / REQ-308: open \p dwgPathUtf8 (or browse when null) into a new focused tab. Shared by
// File ▸ Open and the Start screen's Open button / recent-drawing tiles.
void OpenDrawingInNewTab(AppCommandState& cmd, std::vector<std::string>& log, const char* dwgPathUtf8) {
  char browsed[4096]{};
  if (!dwgPathUtf8) {
    if (!BrowseOpenFileDwgUtf8(browsed, sizeof(browsed)))
      return;
    dwgPathUtf8 = browsed;
  }
  SaveDocumentToSnapshot(cmd, cmd.activeDrawingIdx);
  const std::string tabName = std::filesystem::path(dwgPathUtf8).stem().u8string();
  const int newIdx = static_cast<int>(cmd.drawingTabs.size());
  cmd.drawingTabs.push_back({tabName.empty() ? "Drawing" : tabName, cmd.nextTabUid++});
  cmd.documents.emplace_back();
  RestoreDocumentFromSnapshot(cmd, newIdx);  // clear cmd to empty state
  if (OpenDrawingDocument(cmd, dwgPathUtf8, log)) {
    cmd.activeDocSavedRevision = cmd.cadGpuRevision;
    cmd.activeDocFilePath      = std::string(dwgPathUtf8);
    RecordRecentDrawing(cmd, cmd.activeDocFilePath);
  } else {
    // REQ-308: a recent entry that no longer opens is dropped from the list.
    RemoveRecentDrawing(dwgPathUtf8);
  }
  cmd.activeDrawingIdx        = newIdx;
  cmd.prevDrawingIdx          = newIdx;  // tell main.cpp the switch already happened
  cmd.pendingDrawingTabSwitch = true;
  cmd.pendingViewportFocus    = true;
}

void DrawMainMenuBar(AppCommandState& cmd, std::vector<std::string>& log) {
  static char dxfPath[4096]{};
  static char dwgPath[4096]{};
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
      NewDrawingInTab(cmd, log);
    }
    if (ImGui::MenuItem("Open", nullptr)) {
      OpenDrawingInNewTab(cmd, log, nullptr);
    }
    // REQ-308: the Start tab has no document to save.
    ImGui::BeginDisabled(cmd.activeDrawingIdx == 0);
    if (ImGui::MenuItem("Save", "Ctrl+S")) {
      SaveActiveDocument(cmd, log);
    }
    if (ImGui::MenuItem("Save As...")) {
      const std::string defName = cmd.activeDocFilePath.empty()
          ? (cmd.activeDrawingIdx < static_cast<int>(cmd.drawingTabs.size())
                 ? cmd.drawingTabs[cmd.activeDrawingIdx].name + ".dwg"
                 : std::string("drawing.dwg"))
          : std::filesystem::path(cmd.activeDocFilePath).filename().string();
      if (BrowseSaveFileDwgUtf8(dwgPath, sizeof(dwgPath), defName.c_str())) {
        if (SaveDrawingDocument(cmd, dwgPath, log)) {
          cmd.activeDocSavedRevision = cmd.cadGpuRevision;
          cmd.activeDocFilePath      = std::string(dwgPath);
          if (cmd.activeDrawingIdx < static_cast<int>(cmd.drawingTabs.size()))
            cmd.drawingTabs[cmd.activeDrawingIdx].name =
                std::filesystem::path(dwgPath).stem().string();
          RecordRecentDrawing(cmd, cmd.activeDocFilePath);
        }
      }
    }
    ImGui::EndDisabled();
    ImGui::Separator();
    if (ImGui::MenuItem("Import DXF...", nullptr)) {
      if (BrowseOpenFileDxfUtf8(dxfPath, sizeof(dxfPath)))
        ImportDxfFile(cmd, dxfPath, log);
    }
    if (ImGui::MenuItem("Import Block...", nullptr)) {
      CadBlocksImportWithPicker(cmd, log);
    }
    if (ImGui::MenuItem("Export DXF...", nullptr)) {
      if (BrowseSaveFileDxfUtf8(dxfPath, sizeof(dxfPath), "drawing.dxf"))
        ExportDxfFile(cmd, dxfPath, log);
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Import DWG...", nullptr)) {
      if (BrowseOpenFileDwgUtf8(dwgPath, sizeof(dwgPath)))
        ImportDwgFile(cmd, dwgPath, log);
    }
    if (ImGui::MenuItem("Export DWG...", nullptr)) {
      if (BrowseSaveFileDwgUtf8(dwgPath, sizeof(dwgPath), "drawing.dwg")) {
        cmd.dwgPendingExportPath = dwgPath;
        cmd.dwgLossyExportModal  = true;
      }
    }
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("LibreDWG — R2000 (AC1015) on save.");
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
    // REQ-040: command-line visibility + classic-dock toggle.
    if (ImGui::MenuItem("Command line", "Ctrl+9", cmd.cmdBarVisible && !cmd.cmdLineClassicDock))
      cmd.cmdBarVisible = !cmd.cmdBarVisible;
    if (ImGui::MenuItem("Classic command dock", nullptr, cmd.cmdLineClassicDock))
      cmd.cmdLineClassicDock = !cmd.cmdLineClassicDock;
    ImGui::Separator();
    if (ImGui::MenuItem("Toolspace", nullptr, cmd.showToolspaceWindow))
      cmd.showToolspaceWindow = !cmd.showToolspaceWindow;
    if (ImGui::MenuItem("Settings...", nullptr))
      cmd.showSettingsWindow = true;
    ImGui::EndMenu();
  }

  // REQ-091: sign-in status at the far right of the menu bar — the same placement familiar CAD
  // tools (e.g. Civil 3D) use for the signed-in account. Nothing shown while signed out, which
  // (with the launch gate in place) only happens via its no-internet exception, so there is no
  // "Sign In" prompt to squeeze in here — Settings → System → Account already has one.
  // REQ-091 amendment (D-2026-09-01, GitHub issue #182): the email is a menu that opens an
  // account dropdown — Account Details (a read-only placeholder window) and Sign Out (the same
  // path Settings uses). Still nothing while signed out.
  if (cmd.authSignedIn && !cmd.authEmail.empty()) {
    const float textW = ImGui::CalcTextSize(cmd.authEmail.c_str()).x;
    const float pad   = 28.f;  // room for the menu's own frame padding around the label
    ImGui::SameLine(std::max(0.f, ImGui::GetWindowWidth() - textW - pad));
    if (ImGui::BeginMenu(cmd.authEmail.c_str())) {
      if (ImGui::MenuItem("Account Details"))
        cmd.showAccountDetailsWindow = true;
      ImGui::Separator();
      if (ImGui::MenuItem("Sign Out"))
        cmd.authSignOutRequested = true;
      ImGui::EndMenu();
    }
  }

  ImGui::PopStyleVar();
}

void CollectAllDrawingLayers(const AppCommandState& cmd, std::vector<std::string>* outSortedUnique) {
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


// Toolbar band palette: one tone for the whole strip, plus hi/lo tones for button
// states and grippers, so the strip, its sections and its buttons stay in sync.
// The values come from the active theme (REQ-081/ADR-033) — these were fixed
// constants in the classic theme's grays and so ignored the Dark theme entirely.

// Height of the bottom title strip inside each ribbon panel (Civil 3D-style).
// Carries the active panel's title from Begin to End (panels never nest).
static const char* s_ribbonPanelTitle = nullptr;

// Usable content height above the bottom title strip, for sizing buttons.
static float RibbonPanelContentH(float panelH) {
  return std::max(24.f, panelH - g_chrome.ribbonTitleH);
}

static void RibbonSectionBegin(const char* childId, const char* title, float width, float height) {
  ImGui::BeginGroup();
  // Transparent panel: the raised "panel tray" painted in DrawRibbonBar shows through, so every
  // section shares one continuous surface. Buttons float on it; the panel title is pinned at the
  // bottom by RibbonSectionEnd.
  ImGui::PushStyleColor(ImGuiCol_ChildBg, 0);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.f, 2.f));
  // NoScrollbar alone only hides the bar — a panel whose content is a hair wider than `width`
  // (rounding in colW(), an extra pixel of text) was still wheel-scrollable with no visible bar,
  // which is exactly what let the Survey panel keep scrolling after RibbonToolsLeft's own fix
  // (user GUI-pass feedback, 2026-08-25, follow-up to D-2026-08-25-d). NoScrollWithMouse closes it
  // for every ribbon panel, not just Survey's.
  ImGui::BeginChild(childId, ImVec2(width, height), false,
                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  ImGui::PopStyleVar();
  // A child window keeps its own FontWindowScale (defaults to 1.0) — SetWindowFontScale on the
  // ribbon strip does NOT propagate in. Without this the section body renders text 25% larger
  // than colW()/computeTabWidths measured it (those run in the parent scope at 0.80), so every
  // two-word button label clipped a character and the whole tools row overran into the Layers
  // strip. Match the measurement scale here.
  ImGui::SetWindowFontScale(g_chrome.ribbonBodyFontScale);
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

  const float titleTop = wp.y + wh - g_chrome.ribbonTitleH;
  dl->AddLine(ImVec2(wp.x + 3.f, titleTop), ImVec2(wp.x + ww - 3.f, titleTop), g_chrome.ribbonPanelRule, 1.f);

  const ImU32 tcol = g_chrome.ribbonPanelTitle;

  constexpr float chevSz = 3.f;
  constexpr float gap = 5.f;
  const float totalW = ts.x + gap + chevSz * 2.f;
  float tx = wp.x + (ww - totalW) * 0.5f;
  if (tx < wp.x + 3.f)
    tx = wp.x + 3.f;
  const float ty = titleTop + (g_chrome.ribbonTitleH - ts.y) * 0.5f;
  dl->AddText(ImVec2(tx, ty), tcol, title);
  const float cx = tx + ts.x + gap + chevSz;
  const float cy = titleTop + g_chrome.ribbonTitleH * 0.5f;
  dl->AddTriangleFilled(ImVec2(cx - chevSz, cy - chevSz * 0.55f), ImVec2(cx + chevSz, cy - chevSz * 0.55f),
                        ImVec2(cx, cy + chevSz * 0.75f), tcol);

  ImGui::EndChild();
  ImGui::PopStyleColor();

  // Etched vertical divider in the gap to the right of the panel.
  const ImVec2 mn = ImGui::GetItemRectMin();
  const ImVec2 mx = ImGui::GetItemRectMax();
  ImDrawList* pdl = ImGui::GetWindowDrawList();
  const float dx = mx.x + 4.f;
  pdl->AddLine(ImVec2(dx, mn.y + 3.f), ImVec2(dx, mx.y - 3.f), g_chrome.bandShadow, 1.f);
  pdl->AddLine(ImVec2(dx + 1.f, mn.y + 3.f), ImVec2(dx + 1.f, mx.y - 3.f), g_chrome.bandHilite, 1.f);

  ImGui::EndGroup();
}

enum class RibbonIconKind : std::uint8_t {
  Line,
  Circle,
  Polyline,
  Rect,
  Arc,
  Ellipse,
  Dim,
  DimLinear,
  DimAngular,
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
  Lengthen,
  Extend,
  Break,
  Stretch,
  Fillet,
  Chamfer,
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
  Hatch,
  // REQ-143 contextual TIN Surface tab (vector fallback; no PNGs required).
  SurfLabel,
  SurfLegend,
  SurfPropsHand,
  SurfInquiry,
  SurfIsolate,
  SurfDoc,
  SurfAddData,
  SurfEdit,
  SurfLodLow,
  SurfLodHigh,
  SurfWaterDrop,
  SurfBandage,
  SurfEye,
  SurfCatchment,
  SurfVolumes,
  SurfDrape,
  SurfExtract,
  SurfMoveTo,
  SurfQuickProfile,
  SurfProfile,
  SurfDataShortcut,
  SurfGrading,
  // D-2026-08-28-k Civil 3D Survey tab (vector fallback).
  SvyTripod,
  SvyQuery,
  SvyFigure,
  SvyPda,
  SvyPin,
  SvyRefresh,
  SvyGlobe,
  SvyGeodetic,
  SvySun,
  SvyRenumber,
  SvyLock,
  SvyUnlock,
  Array,
  Plot,
  Export,
  Import,
  Settings,
  ViewportRect,
  ViewportPoly,
  DimStyle,
  // Insert tab + View tab fixes (previously reused Copy/PdfAttach/Layers placeholders).
  Block,
  BlockEditor,
  BlockInsert,
  Toolspace,
  // Block Editor contextual tab — dedicated Block_Authoring_* art (previously all Copy).
  BeSaveBlock,
  BeAutoConstrain,
  BeConstraintShow,
  BeBlockTable,
  BeParameters,
  BePalettes,
  BeParamPoint,
  BeParamLinear,
  BeParamPolar,
  BeParamXY,
  BeParamRotation,
  BeParamAlignment,
  BeParamFlip,
  BeParamVisibility,
  BeParamLookup,
  BeParamBasepoint,
  // Generic placeholder for greyed "not implemented yet" ribbon buttons.
  Nyi,
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

static void RibbonPaintTinPyramid(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, ImU32 col, float t,
                                  bool filled) {
  const float w = std::max(1.f, mx.x - mn.x);
  const float h = std::max(1.f, mx.y - mn.y);
  const ImVec2 apex(mn.x + w * 0.50f, mn.y + h * 0.10f);
  const ImVec2 bl(mn.x + w * 0.10f, mx.y - h * 0.16f);
  const ImVec2 br(mx.x - w * 0.10f, mx.y - h * 0.16f);
  dl->AddTriangleFilled(apex, bl, br, IM_COL32(42, 132, 210, filled ? 230 : 80));
  dl->AddTriangle(apex, bl, br, filled ? IM_COL32(42, 132, 210, 255) : col, t);
  if (!filled) {
    const ImVec2 mid(mn.x + w * 0.50f, mx.y - h * 0.34f);
    dl->AddLine(apex, mid, IM_COL32(255, 255, 255, 200), t);
    dl->AddLine(bl, mid, col, t * 0.85f);
    dl->AddLine(br, mid, col, t * 0.85f);
  }
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
  case RibbonIconKind::Rect: {
    // Rectangle with grips on the two picked (opposite) corners, matching how the command is driven.
    const float rw = w * 0.34f;
    const float rh = h * 0.24f;
    const ImVec2 lo(c.x - rw, c.y - rh);
    const ImVec2 hi(c.x + rw, c.y + rh);
    dl->AddRect(lo, hi, col, 0.f, 0, t);
    RibbonGripSquare(dl, lo, grip, acc, acc, t);
    RibbonGripSquare(dl, hi, grip, acc, acc, t);
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
  case RibbonIconKind::DimAngular: {
    const ImVec2 v(c.x - w * 0.15f, c.y + h * 0.18f);
    const float r = std::min(w, h) * 0.32f;
    const ImVec2 p1(v.x + r * 0.95f, v.y - r * 0.22f);
    const ImVec2 p2(v.x + r * 0.55f, v.y - r * 0.82f);
    dl->AddLine(v, p1, col, t);
    dl->AddLine(v, p2, col, t);
    const ImU32 accentBlue2 = IM_COL32(46, 91, 174, 255);
    const int segs = 10;
    float a1 = std::atan2(p1.y - v.y, p1.x - v.x);
    float a2 = std::atan2(p2.y - v.y, p2.x - v.x);
    float sweep = a2 - a1;
    while (sweep > 3.14159f) sweep -= 6.28318f;
    while (sweep < -3.14159f) sweep += 6.28318f;
    for (int i = 0; i < segs; ++i) {
      float aa = a1 + sweep * (float)i / segs;
      float ab = a1 + sweep * (float)(i+1) / segs;
      dl->AddLine(ImVec2(v.x + std::cos(aa)*r*0.62f, v.y + std::sin(aa)*r*0.62f),
                  ImVec2(v.x + std::cos(ab)*r*0.62f, v.y + std::sin(ab)*r*0.62f), accentBlue2, t*1.05f);
    }
    const float head2 = std::clamp(std::min(w, h) * 0.09f, 2.5f, 5.5f);
    ImVec2 dir1(-std::sin(a1), std::cos(a1));
    if (sweep < 0) dir1 = ImVec2(std::sin(a1), -std::cos(a1));
    ImVec2 dir2(-std::sin(a2), std::cos(a2));
    if (sweep < 0) dir2 = ImVec2(std::sin(a2), -std::cos(a2));
    dir2 = ImVec2(-dir2.x, -dir2.y);
    RibbonStrokeArrow(dl, ImVec2(v.x + std::cos(a1)*r*0.62f, v.y + std::sin(a1)*r*0.62f), dir1, head2, accentBlue2, t);
    RibbonStrokeArrow(dl, ImVec2(v.x + std::cos(a2)*r*0.62f, v.y + std::sin(a2)*r*0.62f), dir2, head2, accentBlue2, t);
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
  case RibbonIconKind::Hatch: {
    // A square outline filled with diagonal hatch strokes (the HATCH command).
    const ImVec2 b0 = RibbonLerp(mn, mx, 0.2f, 0.78f);
    const ImVec2 b1 = RibbonLerp(mn, mx, 0.8f, 0.22f);
    const float bx0 = std::min(b0.x, b1.x), bx1 = std::max(b0.x, b1.x);
    const float by0 = std::min(b0.y, b1.y), by1 = std::max(b0.y, b1.y);
    dl->AddRect(ImVec2(bx0, by0), ImVec2(bx1, by1), col, 0.f, 0, t);
    dl->PushClipRect(ImVec2(bx0, by0), ImVec2(bx1, by1), true);
    const float bw = bx1 - bx0;
    for (float off = -bw + 0.25f * bw; off < bw; off += 0.28f * bw)  // 45° lines, clipped to the box
      dl->AddLine(ImVec2(bx0 + off, by1), ImVec2(bx0 + off + (by1 - by0), by0), acc, t * 0.8f);
    dl->PopClipRect();
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
  case RibbonIconKind::Lengthen: {
    // Horizontal segment with outward-pointing arrowheads at both ends (REQ-103 step 2) —
    // stretching/shrinking a line's own length.
    const float y = c.y;
    const ImVec2 pL(mn.x + w * 0.16f, y), pR(mx.x - w * 0.16f, y);
    dl->AddLine(pL, pR, col, t * 1.1f);
    RibbonStrokeArrow(dl, pL, ImVec2(-1.f, 0.f), std::min(w, h) * 0.22f, acc, t);
    RibbonStrokeArrow(dl, pR, ImVec2(1.f, 0.f), std::min(w, h) * 0.22f, acc, t);
    break;
  }
  case RibbonIconKind::Extend: {
    // A segment (primary) stretching rightward to meet a boundary edge (accent, vertical) — EXTEND
    // is TRIM's inverse, so this deliberately mirrors TRIM's icon shape with the gap closed instead
    // of opened.
    const float y = c.y;
    const ImVec2 p0(mn.x + w * 0.14f, y), p1(mx.x - w * 0.3f, y);
    dl->AddLine(p0, p1, col, t * 1.1f);
    RibbonStrokeArrow(dl, p1, ImVec2(1.f, 0.f), std::min(w, h) * 0.2f, col, t);
    const float boundaryX = mx.x - w * 0.16f;
    dl->AddLine(ImVec2(boundaryX, mn.y + h * 0.18f), ImVec2(boundaryX, mx.y - h * 0.18f), acc, t * 1.1f);
    break;
  }
  case RibbonIconKind::Break: {
    // A horizontal segment with a gap in the middle, plus two short accent tick marks bracketing
    // the gap — AutoCAD's own BREAK icon shape (a line broken at two points).
    const float y = c.y;
    const float gapHalf = w * 0.09f;
    dl->AddLine(ImVec2(mn.x + w * 0.16f, y), ImVec2(c.x - gapHalf, y), col, t * 1.1f);
    dl->AddLine(ImVec2(c.x + gapHalf, y), ImVec2(mx.x - w * 0.16f, y), col, t * 1.1f);
    const float tickH = h * 0.16f;
    dl->AddLine(ImVec2(c.x - gapHalf, y - tickH), ImVec2(c.x - gapHalf, y + tickH), acc, t);
    dl->AddLine(ImVec2(c.x + gapHalf, y - tickH), ImVec2(c.x + gapHalf, y + tickH), acc, t);
    break;
  }
  case RibbonIconKind::Stretch: {
    // A dashed crossing-window box with one vertex (filled dot) pulled out by an arrow — the
    // crossing-select-then-drag shape STRETCH is named for.
    const float bx0 = mn.x + w * 0.18f, by0 = mn.y + h * 0.22f;
    const float bx1 = mx.x - w * 0.4f, by1 = mx.y - h * 0.22f;
    auto dash = [&](ImVec2 a, ImVec2 b) {
      constexpr int n = 5;
      for (int i = 0; i < n; i += 2) {
        const float t0 = static_cast<float>(i) / n, t1 = static_cast<float>(i + 1) / n;
        dl->AddLine(ImVec2(a.x + (b.x - a.x) * t0, a.y + (b.y - a.y) * t0),
                   ImVec2(a.x + (b.x - a.x) * t1, a.y + (b.y - a.y) * t1), acc, t);
      }
    };
    dash(ImVec2(bx0, by0), ImVec2(bx1, by0));
    dash(ImVec2(bx1, by0), ImVec2(bx1, by1));
    dash(ImVec2(bx1, by1), ImVec2(bx0, by1));
    dash(ImVec2(bx0, by1), ImVec2(bx0, by0));
    const ImVec2 vert(bx1, c.y);
    dl->AddLine(vert, ImVec2(mx.x - w * 0.18f, c.y), col, t * 1.1f);
    RibbonStrokeArrow(dl, ImVec2(mx.x - w * 0.18f, c.y), ImVec2(1.f, 0.f), std::min(w, h) * 0.2f, col, t);
    dl->AddCircleFilled(vert, std::min(w, h) * 0.08f, col, 12);
    break;
  }
  case RibbonIconKind::Fillet: {
    // Two perpendicular legs joined by a small rounded corner — AutoCAD's own FILLET icon shape.
    const float rx = mn.x + w * 0.30f, ry = mn.y + h * 0.30f;  // rounded-corner center
    const float rad = std::min(w, h) * 0.20f;
    dl->AddLine(ImVec2(mn.x + w * 0.16f, ry), ImVec2(rx - rad, ry), col, t * 1.1f);
    dl->AddLine(ImVec2(rx, ry - rad), ImVec2(rx, mn.y + h * 0.16f), col, t * 1.1f);
    dl->PathArcTo(ImVec2(rx, ry), rad, 3.14159265f, 3.14159265f * 1.5f, 10);
    dl->PathStroke(col, 0, t * 1.1f);
    // Second leg pair (mirrored) reads as "two curves meeting" rather than one bent line.
    const float rx2 = mx.x - w * 0.30f, ry2 = mx.y - h * 0.30f;
    dl->AddLine(ImVec2(mx.x - w * 0.16f, ry2), ImVec2(rx2 + rad, ry2), acc, t);
    dl->AddLine(ImVec2(rx2, ry2 + rad), ImVec2(rx2, mx.y - h * 0.16f), acc, t);
    dl->PathArcTo(ImVec2(rx2, ry2), rad, 3.14159265f * 1.5f, 3.14159265f * 2.0f, 10);
    dl->PathStroke(acc, 0, t);
    break;
  }
  case RibbonIconKind::Chamfer: {
    // Two perpendicular legs joined by a straight bevel — AutoCAD's own CHAMFER icon shape,
    // visually distinct from FILLET's rounded corner above.
    const float bx = mn.x + w * 0.30f, by = mn.y + h * 0.30f;
    const float cut = std::min(w, h) * 0.20f;
    dl->AddLine(ImVec2(mn.x + w * 0.16f, by), ImVec2(bx - cut, by), col, t * 1.1f);
    dl->AddLine(ImVec2(bx, by - cut), ImVec2(bx, mn.y + h * 0.16f), col, t * 1.1f);
    dl->AddLine(ImVec2(bx - cut, by), ImVec2(bx, by - cut), col, t * 1.1f);
    const float bx2 = mx.x - w * 0.30f, by2 = mx.y - h * 0.30f;
    dl->AddLine(ImVec2(mx.x - w * 0.16f, by2), ImVec2(bx2 + cut, by2), acc, t);
    dl->AddLine(ImVec2(bx2, by2 + cut), ImVec2(bx2, mx.y - h * 0.16f), acc, t);
    dl->AddLine(ImVec2(bx2 + cut, by2), ImVec2(bx2, by2 + cut), acc, t);
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
  case RibbonIconKind::SurfLabel: {
    const ImVec2 a(mn.x + w * 0.22f, mn.y + h * 0.30f);
    const ImVec2 b(mx.x - w * 0.18f, mx.y - h * 0.28f);
    dl->AddCircleFilled(ImVec2(c.x, c.y), std::min(w, h) * 0.38f, IM_COL32(42, 132, 210, 230), 20);
    dl->AddRectFilled(a, b, IM_COL32(255, 255, 255, 240), 2.f);
    dl->AddTriangleFilled(ImVec2(a.x, c.y), ImVec2(mn.x + w * 0.12f, c.y - h * 0.08f),
                          ImVec2(mn.x + w * 0.12f, c.y + h * 0.08f), IM_COL32(255, 255, 255, 240));
    const ImVec2 plus(mx.x - w * 0.16f, mx.y - h * 0.16f);
    dl->AddLine(ImVec2(plus.x - 4.f, plus.y), ImVec2(plus.x + 4.f, plus.y), IM_COL32(255, 255, 255, 255), t);
    dl->AddLine(ImVec2(plus.x, plus.y - 4.f), ImVec2(plus.x, plus.y + 4.f), IM_COL32(255, 255, 255, 255), t);
    break;
  }
  case RibbonIconKind::SurfLegend: {
    dl->AddRect(ImVec2(mn.x + w * 0.18f, mn.y + h * 0.16f), ImVec2(mx.x - w * 0.18f, mx.y - h * 0.16f), col, 0.f, 0,
                t);
    for (int i = 1; i < 3; ++i) {
      const float x = mn.x + w * (0.18f + 0.213f * static_cast<float>(i));
      const float y = mn.y + h * (0.16f + 0.227f * static_cast<float>(i));
      dl->AddLine(ImVec2(x, mn.y + h * 0.16f), ImVec2(x, mx.y - h * 0.16f), col, t * 0.8f);
      dl->AddLine(ImVec2(mn.x + w * 0.18f, y), ImVec2(mx.x - w * 0.18f, y), col, t * 0.8f);
    }
    break;
  }
  case RibbonIconKind::SurfPropsHand: {
    dl->AddRect(ImVec2(mn.x + w * 0.28f, mn.y + h * 0.18f), ImVec2(mx.x - w * 0.14f, mx.y - h * 0.22f), col, 0.f, 0,
                t);
    dl->AddLine(ImVec2(mn.x + w * 0.36f, mn.y + h * 0.38f), ImVec2(mx.x - w * 0.22f, mn.y + h * 0.38f), col, t);
    dl->AddLine(ImVec2(mn.x + w * 0.36f, mn.y + h * 0.52f), ImVec2(mx.x - w * 0.22f, mn.y + h * 0.52f), col, t);
    dl->AddCircleFilled(ImVec2(mn.x + w * 0.22f, mx.y - h * 0.22f), std::min(w, h) * 0.08f, acc, 8);
    break;
  }
  case RibbonIconKind::SurfInquiry: {
    dl->AddCircle(ImVec2(c.x - w * 0.08f, c.y - h * 0.06f), std::min(w, h) * 0.22f, col, 16, t);
    dl->AddLine(ImVec2(c.x + w * 0.08f, c.y + h * 0.12f), ImVec2(mx.x - w * 0.14f, mx.y - h * 0.14f), col, t * 1.4f);
    break;
  }
  case RibbonIconKind::SurfIsolate: {
    dl->AddRectFilled(ImVec2(mn.x + w * 0.18f, mn.y + h * 0.22f), ImVec2(c.x + w * 0.06f, mx.y - h * 0.18f), acc);
    dl->AddRect(ImVec2(c.x - w * 0.06f, mn.y + h * 0.14f), ImVec2(mx.x - w * 0.16f, mx.y - h * 0.26f), col, 0.f, 0, t);
    break;
  }
  case RibbonIconKind::SurfDoc: {
    const ImVec2 tl2(mn.x + w * 0.22f, mn.y + h * 0.14f);
    const ImVec2 br2(mx.x - w * 0.22f, mx.y - h * 0.14f);
    dl->AddRectFilled(tl2, br2, IM_COL32(240, 240, 240, 230));
    dl->AddRect(tl2, br2, col, 0.f, 0, t);
    dl->AddCircleFilled(ImVec2(br2.x, tl2.y), std::min(w, h) * 0.10f, acc, 10);
    break;
  }
  case RibbonIconKind::SurfAddData: {
    RibbonPaintTinPyramid(dl, mn, mx, col, t, false);
    dl->AddCircleFilled(ImVec2(mx.x - w * 0.22f, mx.y - h * 0.22f), std::min(w, h) * 0.12f, IM_COL32(40, 160, 70, 255),
                        10);
    break;
  }
  case RibbonIconKind::SurfEdit: {
    RibbonPaintTinPyramid(dl, mn, mx, col, t, false);
    dl->AddLine(ImVec2(c.x - w * 0.08f, mx.y - h * 0.22f), ImVec2(mx.x - w * 0.18f, mn.y + h * 0.28f),
                IM_COL32(200, 170, 40, 255), t * 1.4f);
    break;
  }
  case RibbonIconKind::SurfLodLow: {
    RibbonPaintTinPyramid(dl, mn, mx, acc, t, true);
    break;
  }
  case RibbonIconKind::SurfLodHigh: {
    RibbonPaintTinPyramid(dl, mn, mx, col, t, false);
    break;
  }
  case RibbonIconKind::SurfWaterDrop: {
    const ImVec2 top(c.x, mn.y + h * 0.12f);
    const ImVec2 left(mn.x + w * 0.22f, c.y + h * 0.02f);
    const ImVec2 right(mx.x - w * 0.22f, c.y + h * 0.02f);
    const ImVec2 bot(c.x, mx.y - h * 0.14f);
    dl->AddTriangleFilled(top, left, bot, IM_COL32(42, 132, 210, 240));
    dl->AddTriangleFilled(top, right, bot, IM_COL32(42, 132, 210, 240));
    break;
  }
  case RibbonIconKind::SurfBandage: {
    dl->AddLine(ImVec2(mn.x + w * 0.12f, c.y - h * 0.12f), ImVec2(mx.x - w * 0.12f, c.y - h * 0.12f),
                IM_COL32(140, 90, 40, 255), t * 2.f);
    dl->AddLine(ImVec2(mn.x + w * 0.12f, c.y + h * 0.12f), ImVec2(mx.x - w * 0.12f, c.y + h * 0.12f),
                IM_COL32(140, 90, 40, 255), t * 2.f);
    dl->AddRectFilled(ImVec2(c.x - w * 0.12f, c.y - h * 0.18f), ImVec2(c.x + w * 0.12f, c.y + h * 0.18f),
                      IM_COL32(240, 240, 240, 255));
    break;
  }
  case RibbonIconKind::SurfEye: {
    dl->AddCircle(c, std::min(w, h) * 0.22f, col, 16, t);
    dl->AddCircleFilled(c, std::min(w, h) * 0.08f, acc, 10);
    dl->AddCircleFilled(ImVec2(mx.x - w * 0.22f, mn.y + h * 0.22f), std::min(w, h) * 0.09f, IM_COL32(40, 160, 70, 255),
                        8);
    break;
  }
  case RibbonIconKind::SurfCatchment: {
    dl->AddRect(ImVec2(mn.x + w * 0.16f, mn.y + h * 0.18f), ImVec2(mx.x - w * 0.16f, mx.y - h * 0.18f), col, 0.f, 0,
                t * 0.7f);
    dl->AddLine(ImVec2(mn.x + w * 0.28f, mn.y + h * 0.28f), ImVec2(c.x, mx.y - h * 0.28f), IM_COL32(200, 170, 40, 255), t);
    RibbonStrokeArrow(dl, ImVec2(c.x, mx.y - h * 0.28f), ImVec2(0.f, 1.f), std::min(w, h) * 0.12f, acc, t);
    break;
  }
  case RibbonIconKind::SurfVolumes: {
    dl->AddRectFilled(ImVec2(mn.x + w * 0.18f, c.y + h * 0.04f), ImVec2(mn.x + w * 0.34f, mx.y - h * 0.16f),
                      IM_COL32(180, 50, 50, 230));
    dl->AddRectFilled(ImVec2(mn.x + w * 0.42f, mn.y + h * 0.22f), ImVec2(mn.x + w * 0.58f, mx.y - h * 0.16f),
                      IM_COL32(40, 150, 70, 230));
    dl->AddRectFilled(ImVec2(mn.x + w * 0.66f, c.y - h * 0.06f), ImVec2(mn.x + w * 0.82f, mx.y - h * 0.16f),
                      IM_COL32(42, 132, 210, 230));
    dl->AddLine(ImVec2(mn.x + w * 0.12f, mx.y - h * 0.16f), ImVec2(mx.x - w * 0.12f, mx.y - h * 0.16f), col, t);
    break;
  }
  case RibbonIconKind::SurfDrape: {
    dl->AddLine(ImVec2(mn.x + w * 0.12f, mx.y - h * 0.28f), ImVec2(mx.x - w * 0.12f, mx.y - h * 0.18f), acc, t);
    dl->AddTriangleFilled(ImVec2(c.x, mn.y + h * 0.18f), ImVec2(c.x - w * 0.18f, c.y), ImVec2(c.x + w * 0.18f, c.y), acc);
    break;
  }
  case RibbonIconKind::SurfExtract: {
    dl->AddLine(ImVec2(mn.x + w * 0.12f, mx.y - h * 0.22f), ImVec2(mx.x - w * 0.12f, mx.y - h * 0.22f), acc, t);
    dl->AddRectFilled(ImVec2(c.x - w * 0.12f, mn.y + h * 0.18f), ImVec2(c.x + w * 0.12f, c.y + h * 0.04f),
                      IM_COL32(240, 240, 240, 230));
    RibbonStrokeArrow(dl, ImVec2(c.x, mn.y + h * 0.16f), ImVec2(0.f, -1.f), std::min(w, h) * 0.12f, col, t);
    break;
  }
  case RibbonIconKind::SurfMoveTo: {
    dl->AddLine(ImVec2(mn.x + w * 0.12f, mx.y - h * 0.22f), ImVec2(mx.x - w * 0.12f, mx.y - h * 0.22f), acc, t);
    dl->AddRectFilled(ImVec2(c.x - w * 0.12f, mn.y + h * 0.16f), ImVec2(c.x + w * 0.12f, c.y),
                      IM_COL32(240, 240, 240, 230));
    RibbonStrokeArrow(dl, ImVec2(c.x, mx.y - h * 0.28f), ImVec2(0.f, 1.f), std::min(w, h) * 0.12f, acc, t);
    break;
  }
  case RibbonIconKind::SurfQuickProfile: {
    const ImU32 profile = IM_COL32(50, 130, 220, 255);
    dl->AddLine(ImVec2(mn.x + w * 0.12f, mx.y - h * 0.22f), ImVec2(mn.x + w * 0.32f, mn.y + h * 0.42f), profile, t * 1.4f);
    dl->AddLine(ImVec2(mn.x + w * 0.32f, mn.y + h * 0.42f), ImVec2(mn.x + w * 0.48f, mn.y + h * 0.42f), profile, t * 1.4f);
    dl->AddLine(ImVec2(mn.x + w * 0.48f, mn.y + h * 0.42f), ImVec2(mn.x + w * 0.62f, mn.y + h * 0.28f), profile, t * 1.4f);
    dl->AddLine(ImVec2(mn.x + w * 0.62f, mn.y + h * 0.28f), ImVec2(mx.x - w * 0.12f, mx.y - h * 0.18f), profile, t * 1.4f);
    dl->AddCircleFilled(ImVec2(mn.x + w * 0.22f, mn.y + h * 0.20f), std::min(w, h) * 0.08f, IM_COL32(240, 190, 40, 255),
                        8);
    break;
  }
  case RibbonIconKind::SurfProfile: {
    dl->AddLine(ImVec2(mn.x + w * 0.16f, mx.y - h * 0.28f), ImVec2(mx.x - w * 0.16f, mx.y - h * 0.28f), col, t);
    dl->AddTriangleFilled(ImVec2(c.x, mn.y + h * 0.18f), ImVec2(c.x - w * 0.16f, c.y + h * 0.04f),
                          ImVec2(c.x + w * 0.16f, c.y + h * 0.04f), acc);
    break;
  }
  case RibbonIconKind::SurfDataShortcut: {
    dl->AddCircle(c, std::min(w, h) * 0.28f, col, 16, t);
    dl->AddCircle(c, std::min(w, h) * 0.16f, col, 16, t * 0.8f);
    RibbonStrokeArrow(dl, ImVec2(mx.x - w * 0.16f, mn.y + h * 0.22f), ImVec2(1.f, -1.f), std::min(w, h) * 0.12f, acc,
                      t);
    break;
  }
  case RibbonIconKind::SurfGrading: {
    RibbonPaintTinPyramid(dl, mn, mx, col, t, true);
    dl->AddLine(ImVec2(mn.x + w * 0.18f, mn.y + h * 0.22f), ImVec2(mx.x - w * 0.22f, mn.y + h * 0.38f),
                IM_COL32(220, 180, 40, 255), t);
    break;
  }
  case RibbonIconKind::SvyTripod: {
    const ImVec2 apex(c.x, mn.y + h * 0.22f);
    dl->AddLine(apex, ImVec2(mn.x + w * 0.18f, mx.y - h * 0.12f), col, t);
    dl->AddLine(apex, ImVec2(c.x, mx.y - h * 0.12f), col, t);
    dl->AddLine(apex, ImVec2(mx.x - w * 0.18f, mx.y - h * 0.12f), col, t);
    dl->AddCircleFilled(apex, std::min(w, h) * 0.10f, acc, 10);
    dl->AddRectFilled(ImVec2(c.x - w * 0.10f, mn.y + h * 0.08f), ImVec2(c.x + w * 0.10f, mn.y + h * 0.22f), acc);
    break;
  }
  case RibbonIconKind::SvyQuery: {
    const ImVec2 apex(mn.x + w * 0.38f, mn.y + h * 0.28f);
    dl->AddLine(apex, ImVec2(mn.x + w * 0.16f, mx.y - h * 0.12f), col, t);
    dl->AddLine(apex, ImVec2(mn.x + w * 0.38f, mx.y - h * 0.12f), col, t);
    dl->AddLine(apex, ImVec2(mn.x + w * 0.58f, mx.y - h * 0.12f), col, t);
    dl->AddCircleFilled(apex, std::min(w, h) * 0.08f, acc, 8);
    const ImVec2 glass(mx.x - w * 0.28f, mn.y + h * 0.32f);
    dl->AddCircle(glass, std::min(w, h) * 0.16f, acc, 16, t * 1.4f);
    dl->AddLine(ImVec2(glass.x + std::min(w, h) * 0.12f, glass.y + std::min(w, h) * 0.12f),
                ImVec2(mx.x - w * 0.10f, mx.y - h * 0.18f), acc, t * 1.4f);
    break;
  }
  case RibbonIconKind::SvyFigure: {
    dl->AddLine(ImVec2(mn.x + w * 0.22f, mx.y - h * 0.22f), ImVec2(mn.x + w * 0.22f, mn.y + h * 0.28f), col, t * 1.4f);
    dl->AddLine(ImVec2(mn.x + w * 0.22f, mx.y - h * 0.22f), ImVec2(mx.x - w * 0.22f, mx.y - h * 0.22f), col, t * 1.4f);
    dl->AddCircleFilled(ImVec2(mn.x + w * 0.22f, mn.y + h * 0.28f), 2.5f, acc, 8);
    dl->AddCircleFilled(ImVec2(mn.x + w * 0.22f, mx.y - h * 0.22f), 2.5f, acc, 8);
    dl->AddCircleFilled(ImVec2(mx.x - w * 0.22f, mx.y - h * 0.22f), 2.5f, acc, 8);
    dl->AddLine(ImVec2(mx.x - w * 0.34f, mn.y + h * 0.22f), ImVec2(mx.x - w * 0.18f, mn.y + h * 0.42f),
                IM_COL32(220, 180, 40, 255), t);
    break;
  }
  case RibbonIconKind::SvyPda: {
    const ImVec2 a(mn.x + w * 0.28f, mn.y + h * 0.14f);
    const ImVec2 b(mx.x - w * 0.28f, mx.y - h * 0.14f);
    dl->AddRectFilled(a, b, IM_COL32(245, 245, 245, 240), 3.f);
    dl->AddRect(a, b, col, 3.f, 0, t);
    dl->AddRectFilled(ImVec2(a.x + 3.f, a.y + 4.f), ImVec2(b.x - 3.f, a.y + h * 0.22f), acc, 1.f);
    break;
  }
  case RibbonIconKind::SvyPin: {
    dl->AddCircleFilled(ImVec2(c.x, mn.y + h * 0.32f), std::min(w, h) * 0.18f, IM_COL32(230, 230, 230, 255), 12);
    dl->AddCircle(ImVec2(c.x, mn.y + h * 0.32f), std::min(w, h) * 0.18f, col, 12, t);
    dl->AddTriangleFilled(ImVec2(c.x - w * 0.12f, mn.y + h * 0.42f), ImVec2(c.x + w * 0.12f, mn.y + h * 0.42f),
                          ImVec2(c.x, mx.y - h * 0.10f), acc);
    break;
  }
  case RibbonIconKind::SvyRefresh: {
    dl->AddRectFilled(ImVec2(mn.x + w * 0.18f, mn.y + h * 0.18f), ImVec2(mx.x - w * 0.18f, mx.y - h * 0.18f),
                      IM_COL32(46, 160, 80, 230), 3.f);
    const float r = std::min(w, h) * 0.18f;
    dl->AddCircle(c, r, IM_COL32(255, 255, 255, 255), 16, t);
    RibbonStrokeArrow(dl, ImVec2(c.x + r, c.y), ImVec2(0.f, -1.f), std::min(w, h) * 0.10f, IM_COL32(255, 255, 255, 255),
                      t);
    break;
  }
  case RibbonIconKind::SvyGlobe: {
    const float r = std::min(w, h) * 0.36f;
    dl->AddCircleFilled(c, r, IM_COL32(70, 140, 210, 230), 20);
    dl->AddCircle(c, r, col, 20, t);
    dl->AddLine(ImVec2(c.x, c.y - r), ImVec2(c.x, c.y + r), IM_COL32(255, 255, 255, 200), t * 0.8f);
    dl->AddLine(ImVec2(c.x - r, c.y), ImVec2(c.x + r, c.y), IM_COL32(255, 255, 255, 200), t * 0.8f);
    dl->AddCircle(c, r * 0.45f, IM_COL32(255, 255, 255, 180), 16, t * 0.7f);
    dl->AddCircleFilled(ImVec2(c.x + r * 0.35f, c.y - r * 0.20f), std::min(w, h) * 0.08f, IM_COL32(40, 90, 180, 255), 8);
    break;
  }
  case RibbonIconKind::SvyGeodetic: {
    const float r = std::min(w, h) * 0.32f;
    const ImVec2 gc(c.x - w * 0.08f, c.y);
    dl->AddCircleFilled(gc, r, IM_COL32(70, 140, 210, 230), 20);
    dl->AddCircle(gc, r, col, 20, t);
    dl->AddLine(ImVec2(gc.x, gc.y - r), ImVec2(gc.x, gc.y + r), IM_COL32(255, 255, 255, 200), t * 0.8f);
    dl->AddLine(ImVec2(gc.x - r, gc.y), ImVec2(gc.x + r, gc.y), IM_COL32(255, 255, 255, 200), t * 0.8f);
    const ImVec2 ca(mx.x - w * 0.42f, mx.y - h * 0.48f);
    const ImVec2 cb(mx.x - w * 0.08f, mx.y - h * 0.10f);
    dl->AddRectFilled(ca, cb, IM_COL32(230, 235, 240, 255), 2.f);
    dl->AddRect(ca, cb, col, 2.f, 0, t);
    dl->AddLine(ImVec2(ca.x + 3.f, ca.y + (cb.y - ca.y) * 0.35f), ImVec2(cb.x - 3.f, ca.y + (cb.y - ca.y) * 0.35f),
                col, t * 0.7f);
    break;
  }
  case RibbonIconKind::SvySun: {
    dl->AddCircleFilled(c, std::min(w, h) * 0.18f, IM_COL32(240, 190, 40, 255), 16);
    for (int i = 0; i < 8; ++i) {
      const float a = static_cast<float>(i) * 0.78539816f;
      const ImVec2 p0(c.x + std::cos(a) * std::min(w, h) * 0.24f, c.y + std::sin(a) * std::min(w, h) * 0.24f);
      const ImVec2 p1(c.x + std::cos(a) * std::min(w, h) * 0.40f, c.y + std::sin(a) * std::min(w, h) * 0.40f);
      dl->AddLine(p0, p1, IM_COL32(240, 190, 40, 255), t);
    }
    break;
  }
  case RibbonIconKind::SvyRenumber: {
    dl->AddRect(ImVec2(mn.x + w * 0.14f, mn.y + h * 0.18f), ImVec2(c.x + w * 0.06f, mx.y - h * 0.18f), col, 0.f, 0, t);
    dl->AddLine(ImVec2(mn.x + w * 0.14f, c.y), ImVec2(c.x + w * 0.06f, c.y), col, t * 0.8f);
    dl->AddLine(ImVec2(mn.x + w * 0.38f, mn.y + h * 0.18f), ImVec2(mn.x + w * 0.38f, mx.y - h * 0.18f), col, t * 0.8f);
    RibbonStrokeArrow(dl, ImVec2(mx.x - w * 0.18f, mx.y - h * 0.22f), ImVec2(0.f, 1.f), std::min(w, h) * 0.12f, acc, t);
    dl->AddLine(ImVec2(mx.x - w * 0.18f, mn.y + h * 0.22f), ImVec2(mx.x - w * 0.18f, mx.y - h * 0.28f), acc, t);
    break;
  }
  case RibbonIconKind::SvyLock:
  case RibbonIconKind::SvyUnlock: {
    const ImU32 gold = IM_COL32(220, 175, 50, 255);
    const float bw = w * 0.42f;
    const float bh = h * 0.36f;
    const ImVec2 b0(c.x - bw * 0.5f, c.y - h * 0.02f);
    const ImVec2 b1(c.x + bw * 0.5f, b0.y + bh);
    dl->AddRectFilled(b0, b1, gold, 2.f);
    dl->AddRect(b0, b1, col, 2.f, 0, t);
    const float r = std::min(w, h) * 0.16f;
    const ImVec2 sh(c.x, b0.y);
    if (k == RibbonIconKind::SvyLock) {
      dl->AddCircle(ImVec2(sh.x, sh.y - r * 0.15f), r, col, 16, t);
    } else {
      dl->PathArcTo(ImVec2(sh.x + r * 0.35f, sh.y - r * 0.10f), r, 3.4f, 6.0f, 10);
      dl->PathStroke(col, 0, t);
    }
    break;
  }
  case RibbonIconKind::Array:
  case RibbonIconKind::Plot:
  case RibbonIconKind::Export:
  case RibbonIconKind::Import:
  case RibbonIconKind::Settings:
  case RibbonIconKind::ViewportRect:
  case RibbonIconKind::ViewportPoly:
  case RibbonIconKind::DimStyle:
    break;
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
  case RibbonIconKind::Circle:         return "Circle_Center_Radius";
  case RibbonIconKind::Polyline:       return "polyline";
  case RibbonIconKind::Rect:           return "rect";
  case RibbonIconKind::Arc:            return "arc";
  case RibbonIconKind::Ellipse:        return "ellipse";
  case RibbonIconKind::Hatch:          return "hatch";
  case RibbonIconKind::Dim:            return "Dim_Aligned";
  case RibbonIconKind::DimLinear:      return "Dim_Linear";
  case RibbonIconKind::DimAngular:     return "dimangular";
  case RibbonIconKind::Id:             return "Single_Point";
  case RibbonIconKind::Text:           return "text";
  case RibbonIconKind::Mtext:          return "mtext";
  case RibbonIconKind::Move:           return "move";
  case RibbonIconKind::Copy:           return "copy";
  case RibbonIconKind::Rotate:         return "rotate";
  case RibbonIconKind::Erase:          return "erase";
  case RibbonIconKind::Join:           return "join";
  case RibbonIconKind::Trim:           return "trim";
  case RibbonIconKind::Offset:         return "offset";
  case RibbonIconKind::ZoomExtents:    return "Zoom_Extensis";
  case RibbonIconKind::ZoomWindow:     return "Zoom_Window";
  case RibbonIconKind::Scale:          return "scale";
  case RibbonIconKind::Mirror:         return "mirror";
  case RibbonIconKind::Lengthen:       return "lengthen";
  case RibbonIconKind::Extend:         return "extend";
  case RibbonIconKind::Break:          return "break";
  case RibbonIconKind::Stretch:        return "stretch";
  case RibbonIconKind::Fillet:         return "fillet";
  case RibbonIconKind::Chamfer:        return "chamfer";
  case RibbonIconKind::SurveyPoint:    return "Point_Style";
  case RibbonIconKind::SurveyInverse:  return "surveyinverse";
  case RibbonIconKind::Layers:         return "layers";
  case RibbonIconKind::PdfAttach:      return "PDF";
  case RibbonIconKind::PdfShowBg:      return "pdfshowbg";
  case RibbonIconKind::PdfHideBg:      return "pdfhidebg";
  case RibbonIconKind::PdfVectorize:   return "PDF_Import";
  case RibbonIconKind::Undo:           return "undo";
  case RibbonIconKind::Redo:           return "redo";
  case RibbonIconKind::ClipboardCopy:  return "Copy_Clip";
  case RibbonIconKind::ClipboardPaste: return "Paste";
  case RibbonIconKind::Traverse:       return "traverse";
  // REQ-143 TIN Surface contextual tab. Matched to library art where one fits; the
  // rest keep their hand-drawn vector fallback (return "" → PaintRibbonIcon).
  // GUI-pass 2026-08-30: repointed off the old baked-text Autodesk library art onto the c3d_*
  // blue line-art set so the Survey / contextual tabs match Home/Insert/Annotate/Manage/Output.
  case RibbonIconKind::SurfLabel:      return "c3d_addlabels";
  case RibbonIconKind::SurfLegend:     return "Table";
  case RibbonIconKind::SurfPropsHand:  return "c3d_properties";
  case RibbonIconKind::SurfInquiry:    return "Measure_Area";
  case RibbonIconKind::SurfIsolate:    return "c3d_isolate";
  case RibbonIconKind::SurfDoc:        return "c3d_properties";
  case RibbonIconKind::SurfAddData:    return "c3d_surfaces";
  case RibbonIconKind::SurfEdit:       return "c3d_surfedit";
  case RibbonIconKind::SurfLodLow:     return "surflodlow";
  case RibbonIconKind::SurfLodHigh:    return "surflodhigh";
  case RibbonIconKind::SurfWaterDrop:  return "surfwaterdrop";
  case RibbonIconKind::SurfBandage:    return "Breakline_Symbol";
  case RibbonIconKind::SurfEye:        return "surfeye";
  case RibbonIconKind::SurfCatchment:  return "surfcatchment";
  case RibbonIconKind::SurfVolumes:    return "Measure_Volume";
  case RibbonIconKind::SurfDrape:      return "surfdrape";
  case RibbonIconKind::SurfExtract:    return "Extract_Data";
  case RibbonIconKind::SurfMoveTo:     return "3D_Move";
  case RibbonIconKind::SurfQuickProfile: return "c3d_quickprofile";
  case RibbonIconKind::SurfProfile:    return "c3d_quickprofile";
  case RibbonIconKind::SurfDataShortcut: return "Attach";
  case RibbonIconKind::SurfGrading:    return "c3d_grading";
  // D-2026-08-28-k Civil 3D Survey tab / Survey Point contextual tab.
  case RibbonIconKind::SvyTripod:      return "svytripod";
  case RibbonIconKind::SvyQuery:       return "svyquery";
  case RibbonIconKind::SvyFigure:      return "svyfigure";
  case RibbonIconKind::SvyPda:         return "svypda";
  case RibbonIconKind::SvyPin:         return "surveypoint";
  case RibbonIconKind::SvyRefresh:     return "svyrefresh";
  case RibbonIconKind::SvyGlobe:       return "c3d_mapcheck";
  case RibbonIconKind::SvyGeodetic:    return "c3d_geodetic";
  case RibbonIconKind::SvySun:         return "c3d_astro";
  case RibbonIconKind::SvyRenumber:    return "svyrenumber";
  case RibbonIconKind::SvyLock:        return "Layer_Lock";
  case RibbonIconKind::SvyUnlock:      return "Layer_Unlock";
  case RibbonIconKind::Array:          return "array";
  case RibbonIconKind::Plot:           return "plot";
  case RibbonIconKind::Export:         return "export";
  case RibbonIconKind::Import:         return "import";
  case RibbonIconKind::Settings:       return "settings";
  case RibbonIconKind::ViewportRect:   return "viewportrect";
  case RibbonIconKind::ViewportPoly:   return "viewportpoly";
  case RibbonIconKind::DimStyle:       return "dimstyle";
  // Insert/View placeholder fixes.
  case RibbonIconKind::Block:          return "block";
  case RibbonIconKind::BlockEditor:    return "Block_Editor";
  case RibbonIconKind::BlockInsert:    return "Insert_Block";
  case RibbonIconKind::Toolspace:      return "toolspace";
  // Block Editor contextual tab.
  case RibbonIconKind::BeSaveBlock:       return "Write_Block";
  case RibbonIconKind::BeAutoConstrain:   return "AutoConstrain";
  case RibbonIconKind::BeConstraintShow:  return "Show_Geometric_Constraints";
  case RibbonIconKind::BeBlockTable:      return "Block_Authoring_Actions_Table";
  case RibbonIconKind::BeParameters:      return "Block_Authoring_Parameters_Linear";
  case RibbonIconKind::BePalettes:        return "Blocks_Palette";
  case RibbonIconKind::BeParamPoint:      return "Block_Authoring_Parameters_Point";
  case RibbonIconKind::BeParamLinear:     return "Block_Authoring_Parameters_Linear";
  case RibbonIconKind::BeParamPolar:      return "Block_Authoring_Parameters_Polar";
  case RibbonIconKind::BeParamXY:         return "Block_Authoring_Parameters_XY";
  case RibbonIconKind::BeParamRotation:   return "Block_Authoring_Parameters_Rotation";
  case RibbonIconKind::BeParamAlignment:  return "Block_Authoring_Parameters_Alignment";
  case RibbonIconKind::BeParamFlip:       return "Block_Authoring_Parameters_Flip";
  case RibbonIconKind::BeParamVisibility: return "Block_Authoring_Parameters_Visibility";
  case RibbonIconKind::BeParamLookup:     return "Block_Authoring_Parameters_Lookup";
  case RibbonIconKind::BeParamBasepoint:  return "Block_Authoring_Parameters_Base_Point";
  case RibbonIconKind::Nyi:               return "nyi";
  }
  return "";
}

static ImTextureID g_ribbonIconTex[static_cast<int>(RibbonIconKind::Nyi) + 1] = {};
static bool g_ribbonIconsLoaded = false;

static void EnsureRibbonIconsLoaded() {
  if (g_ribbonIconsLoaded) return;
  g_ribbonIconsLoaded = true;  // attempt once; missing files fall back to vector art
  for (int i = 0; i <= static_cast<int>(RibbonIconKind::Nyi); ++i) {
    const std::string nm = RibbonIconName(static_cast<RibbonIconKind>(i));
    if (nm.empty()) continue;
    const std::filesystem::path p =
        ResolveBundledAssetPath(std::filesystem::path("resources") / "icons" / (nm + ".png"));
    if (p.empty()) continue;
    const unsigned int tex = LoadIconTextureRgba(p);
    if (tex) g_ribbonIconTex[i] = static_cast<ImTextureID>(static_cast<intptr_t>(tex));
  }
}

// Load (once, cached) an arbitrary resources/icons/<name>.png as a texture. Used by ribbon buttons
// that carry a bespoke icon not in the RibbonIconKind enum (e.g. the greyed Civil 3D placeholders).
static ImTextureID RibbonNamedIconTex(const char* name) {
  static std::unordered_map<std::string, ImTextureID> cache;
  if (!name || !name[0]) return 0;
  auto it = cache.find(name);
  if (it != cache.end()) return it->second;
  ImTextureID tex = 0;
  const std::filesystem::path p =
      ResolveBundledAssetPath(std::filesystem::path("resources") / "icons" / (std::string(name) + ".png"));
  if (!p.empty()) {
    if (unsigned int gl = LoadIconTextureRgba(p))
      tex = static_cast<ImTextureID>(static_cast<intptr_t>(gl));
  }
  cache.emplace(name, tex);
  return tex;
}

// Map an UPPERCASE command name to a ribbon icon, for the command autocomplete list.
static bool CommandIconKind(const std::string& upperName, RibbonIconKind* out) {
  struct M { const char* n; RibbonIconKind k; };
  static const M m[] = {
    {"LINE", RibbonIconKind::Line}, {"CIRCLE", RibbonIconKind::Circle}, {"POLYLINE", RibbonIconKind::Polyline},
    {"RECT", RibbonIconKind::Rect},
    {"ARC", RibbonIconKind::Arc}, {"ELLIPSE", RibbonIconKind::Ellipse}, {"HATCH", RibbonIconKind::Hatch},
    {"TEXT", RibbonIconKind::Text},
    {"MTEXT", RibbonIconKind::Mtext}, {"DIMALIGNED", RibbonIconKind::Dim}, {"DIMLINEAR", RibbonIconKind::DimLinear}, {"DIMANGULAR", RibbonIconKind::DimAngular}, {"DIMSTY", RibbonIconKind::DimStyle},
    {"ID", RibbonIconKind::Id}, {"INVERSE", RibbonIconKind::SurveyInverse}, {"MOVE", RibbonIconKind::Move},
    {"COPY", RibbonIconKind::Copy}, {"ROTATE", RibbonIconKind::Rotate}, {"SCALE", RibbonIconKind::Scale},
    {"MIRROR", RibbonIconKind::Mirror},
    {"ARRAY", RibbonIconKind::Array},
    {"PLOT", RibbonIconKind::Plot},
    {"IMPORT", RibbonIconKind::Import},
    {"EXPORT", RibbonIconKind::Export},
    {"LENGTHEN", RibbonIconKind::Lengthen},
    {"EXTEND", RibbonIconKind::Extend},
    {"BREAK", RibbonIconKind::Break},
    {"STRETCH", RibbonIconKind::Stretch},
    {"FILLET", RibbonIconKind::Fillet},
    {"CHAMFER", RibbonIconKind::Chamfer},
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
  const ImU32 face = sunken ? g_chrome.bandSunken : g_chrome.bandRaised;
  dl->AddRectFilled(bb.Min, bb.Max, face, 0.f);
  const ImU32 tl = sunken ? g_chrome.bandShadow : g_chrome.bandHilite;  // top-left edge
  const ImU32 br = sunken ? g_chrome.bandHilite : g_chrome.bandShadow;  // bottom-right edge
  dl->AddLine(ImVec2(bb.Min.x, bb.Min.y + 0.5f), ImVec2(bb.Max.x - 1.f, bb.Min.y + 0.5f), tl, 1.f);
  dl->AddLine(ImVec2(bb.Min.x + 0.5f, bb.Min.y), ImVec2(bb.Min.x + 0.5f, bb.Max.y - 1.f), tl, 1.f);
  dl->AddLine(ImVec2(bb.Min.x, bb.Max.y - 0.5f), ImVec2(bb.Max.x, bb.Max.y - 0.5f), br, 1.f);
  dl->AddLine(ImVec2(bb.Max.x - 0.5f, bb.Min.y), ImVec2(bb.Max.x - 0.5f, bb.Max.y), br, 1.f);
}

// Draw an icon (bitmap if loaded, else procedural fallback) centered as a
// square inside [iconMin, iconMax], dimmed by the current style alpha.
static void DrawRibbonIconArt(ImDrawList* dl, RibbonIconKind icon, const ImVec2& iconMin, const ImVec2& iconMax,
                              const char* iconNameOverride = nullptr) {
  if (iconMax.y <= iconMin.y + 2.f || iconMax.x <= iconMin.x + 2.f)
    return;
  EnsureRibbonIconsLoaded();
  const ImTextureID tex = (iconNameOverride && iconNameOverride[0])
                              ? RibbonNamedIconTex(iconNameOverride)
                              : g_ribbonIconTex[static_cast<int>(icon)];
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

// Label-below wrapping: ImGui's wrap_width splits mid-word when a token is wider
// than the button. Civil 3D wraps only at spaces; the button grows to the longest word.
static float RibbonLongestWordWidth(const char* label) {
  assert(label != nullptr);
  assert(label[0] != '\0');
  float maxW = 0.f;
  const char* p = label;
  for (int guard = 0; guard < 64 && *p != '\0'; ++guard) {
    while (*p == ' ')
      ++p;
    if (*p == '\0')
      break;
    const char* start = p;
    while (*p != '\0' && *p != ' ')
      ++p;
    maxW = std::max(maxW, ImGui::CalcTextSize(start, p).x);
  }
  return maxW;
}

static std::string RibbonWrapAtSpaces(const char* label, float wrapW) {
  assert(label != nullptr);
  assert(wrapW > 0.f);
  std::string out;
  const char* p = label;
  float lineW = 0.f;
  bool firstOnLine = true;
  const float spaceW = ImGui::CalcTextSize(" ").x;
  for (int guard = 0; guard < 64 && *p != '\0'; ++guard) {
    while (*p == ' ')
      ++p;
    if (*p == '\0')
      break;
    const char* start = p;
    while (*p != '\0' && *p != ' ')
      ++p;
    const float ww = ImGui::CalcTextSize(start, p).x;
    if (!firstOnLine && lineW + spaceW + ww > wrapW) {
      out.push_back('\n');
      lineW = 0.f;
      firstOnLine = true;
    }
    if (!firstOnLine) {
      out.push_back(' ');
      lineW += spaceW;
    }
    out.append(start, static_cast<size_t>(p - start));
    lineW += ww;
    firstOnLine = false;
  }
  return out;
}

static int RibbonWrappedLineCount(const char* s) {
  assert(s != nullptr);
  int n = 1;
  for (const char* p = s; *p != '\0'; ++p) {
    if (*p == '\n')
      ++n;
  }
  assert(n >= 1);
  assert(n < 16);
  return n;
}

static float RibbonBelowButtonWidth(const char* label, float minW) {
  assert(label != nullptr);
  assert(minW > 0.f);
  constexpr float kPad = 20.f;
  const float word = RibbonLongestWordWidth(label);
  const float full = ImGui::CalcTextSize(label).x + kPad;
  float w = std::max(minW, word + kPad);
  for (int i = 0; i < 48; ++i) {
    const float inner = std::max(word + 2.f, w - 8.f);
    const std::string wrapped = RibbonWrapAtSpaces(label, inner);
    const ImVec2 ts = ImGui::CalcTextSize(wrapped.c_str());
    w = std::max(w, ts.x + kPad);
    if (RibbonWrappedLineCount(wrapped.c_str()) <= 2)
      return w;
    if (w >= full)
      return full;
    w += 4.f;
  }
  return w;
}

static float RibbonMaxLineWidth(const char* s) {
  assert(s != nullptr);
  assert(s[0] != '\0');
  float maxW = 0.f;
  const char* p = s;
  for (int guard = 0; guard < 8 && *p != '\0'; ++guard) {
    const char* e = p;
    while (*e != '\0' && *e != '\n')
      ++e;
    maxW = std::max(maxW, ImGui::CalcTextSize(p, e).x);
    if (*e != '\n')
      break;
    p = e + 1;
  }
  return maxW;
}

static float RibbonCaptionLineGap() {
  return 3.f;
}

static ImVec2 RibbonCaptionSize(const char* wrapped) {
  assert(wrapped != nullptr);
  const ImVec2 base = ImGui::CalcTextSize(wrapped);
  const int n = RibbonWrappedLineCount(wrapped);
  assert(n >= 1);
  if (n <= 1)
    return base;
  return ImVec2(base.x, base.y + RibbonCaptionLineGap() * static_cast<float>(n - 1));
}

static void RibbonAddCaption(ImDrawList* dl, ImVec2 pos, ImU32 col, const char* wrapped) {
  assert(dl != nullptr);
  assert(wrapped != nullptr);
  const float step = ImGui::GetFontSize() + RibbonCaptionLineGap();
  const char* p = wrapped;
  for (int guard = 0; guard < 8 && *p != '\0'; ++guard) {
    const char* e = p;
    while (*e != '\0' && *e != '\n')
      ++e;
    dl->AddText(pos, col, p, e);
    if (*e != '\n')
      break;
    pos.y += step;
    p = e + 1;
  }
}

// Where a button's text label sits relative to its icon.
enum class RibbonLabel { None, Right, Below };

// Flexible ribbon button: icon-only (None), icon + label to the right (Right),
// or a large icon with the label centered below (Below). Shares the 3D bevel
// and icon art with every ribbon button so states stay consistent.
static bool RibbonButtonEx(const char* str_id, RibbonIconKind icon, const char* label,
                           const ImVec2& size, RibbonLabel mode, const char* iconNameOverride = nullptr) {
  assert(str_id != nullptr);
  assert(size.x > 0.f && size.y > 0.f);
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
  constexpr float iconPad = 1.f;  // icons hug the button edge for readability
  std::string wrapped;
  const char* drawLabel = label;
  if (mode == RibbonLabel::Below && hasLabel) {
    if (std::strchr(label, '\n') != nullptr) {
      drawLabel = label;
    } else {
      wrapped = RibbonWrapAtSpaces(label, std::max(RibbonLongestWordWidth(label) + 2.f, size.x - 8.f));
      drawLabel = wrapped.c_str();
    }
  }
  const ImVec2 ts = hasLabel ? (mode == RibbonLabel::Below ? RibbonCaptionSize(drawLabel)
                                                           : ImGui::CalcTextSize(drawLabel))
                             : ImVec2(0.f, 0.f);
  const ImU32 textCol = ImGui::GetColorU32(ImGuiCol_Text);

  ImVec2 iconMin, iconMax, labelPos;
  if (mode == RibbonLabel::Below && hasLabel) {
    constexpr float botPad = 4.f;
    const float iconArea = std::max(18.f, size.y - ts.y - iconPad - botPad - 1.f);
    const float sideMax = std::min(size.x - iconPad * 2.f, iconArea);
    const ImVec2 ctr(bb.Min.x + size.x * 0.5f + shift, bb.Min.y + iconPad + iconArea * 0.5f + shift);
    iconMin = ImVec2(ctr.x - sideMax * 0.5f, ctr.y - sideMax * 0.5f);
    iconMax = ImVec2(ctr.x + sideMax * 0.5f, ctr.y + sideMax * 0.5f);
    labelPos = ImVec2(bb.Min.x + (size.x - ts.x) * 0.5f + shift, bb.Max.y - ts.y - botPad + shift);
  } else if (mode == RibbonLabel::Right && hasLabel) {
    const float side = size.y - iconPad * 2.f;
    iconMin = ImVec2(bb.Min.x + iconPad + shift, bb.Min.y + iconPad + shift);
    iconMax = ImVec2(iconMin.x + side, iconMin.y + side);
    labelPos = ImVec2(iconMax.x + 3.f + shift, bb.Min.y + (size.y - ts.y) * 0.5f + shift);
  } else {
    const float side = std::min(size.x, size.y) - iconPad * 2.f;
    const ImVec2 ctr(bb.Min.x + size.x * 0.5f + shift, bb.Min.y + size.y * 0.5f + shift);
    iconMin = ImVec2(ctr.x - side * 0.5f, ctr.y - side * 0.5f);
    iconMax = ImVec2(ctr.x + side * 0.5f, ctr.y + side * 0.5f);
  }

  DrawRibbonIconArt(dl, icon, iconMin, iconMax, iconNameOverride);
  if (hasLabel) {
    if (mode == RibbonLabel::Below)
      RibbonAddCaption(dl, labelPos, textCol, drawLabel);
    else
      dl->AddText(labelPos, textCol, drawLabel);
  }

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

static void RibbonNyiButton(const char* id, RibbonIconKind ic, const char* label, const ImVec2& size,
                            RibbonLabel mode, const char* iconNameOverride = nullptr) {
  assert(id != nullptr);
  assert(label != nullptr);
  ImGui::BeginDisabled();
  (void)RibbonButtonEx(id, ic, label, size, mode, iconNameOverride);
  char flat[160];
  size_t o = 0;
  for (const char* p = label; *p != '\0' && o + 1 < sizeof(flat); ++p) {
    flat[o++] = (*p == '\n') ? ' ' : *p;
  }
  flat[o] = '\0';
  char tip[192];
  std::snprintf(tip, sizeof(tip), "%s — not implemented yet.", flat);
  RibbonItemHelp(tip, ImGuiHoveredFlags_AllowWhenDisabled);
  ImGui::EndDisabled();
}

static int FirstSelectedSurfaceIndex(const AppCommandState& cmd) {
  const size_t n = cmd.cadSurfaces.size();
  assert(n < 10000000u);
  for (const SelectedEntity& e : cmd.selection) {
    assert(e.index >= -1);
    if (e.type != SelectedEntity::Type::Surface)
      continue;
    if (e.index < 0 || static_cast<size_t>(e.index) >= n)
      continue;
    return e.index;
  }
  return -1;
}

static int CountSelectedSurveyPoints(const AppCommandState& cmd) {
  const size_t n = cmd.surveyPoints.size();
  assert(n < 10000000u);
  assert(cmd.selectedSurveyPointIndices.size() < 10000000u);
  int count = 0;
  for (const int ix : cmd.selectedSurveyPointIndices) {
    if (ix < 0 || static_cast<size_t>(ix) >= n)
      continue;
    ++count;
  }
  return count;
}

static int FirstSelectedSurveyPointIndex(const AppCommandState& cmd) {
  const size_t n = cmd.surveyPoints.size();
  assert(n < 10000000u);
  assert(cmd.selectedSurveyPointIndices.size() < 10000000u);
  for (const int ix : cmd.selectedSurveyPointIndices) {
    if (ix < 0 || static_cast<size_t>(ix) >= n)
      continue;
    return ix;
  }
  return -1;
}

// REQ-302 increment 2 (ADR-038): measure-then-decide responsive breakpoints for the ribbon's own
// per-tab sections, plus a shared overflow popup for whatever doesn't fit at Narrow.
enum class RibbonBreakpoint { Wide, Medium, Narrow };

// One ribbon section's precomputed Wide/Medium total widths and its deferred render body. `render`
// must be invoked with `curCompact` (DrawRibbonBar's local) already set to whatever this call site
// decided — DecideRibbonFit/RenderRibbonFit below do that; nothing else should call `render`
// directly.
struct RibbonSectionSpec {
  float wideW = 0.f;
  float mediumW = 0.f;
  std::function<void()> render;
  // For the collapsed-panel button shown when this section doesn't fit inline (Civil 3D-style:
  // one titled flyout button per collapsed panel, not a shared "More"). Optional — a section
  // with no name collapses to an icon-only button.
  const char* name = "";
  RibbonIconKind icon = RibbonIconKind::Nyi;
  const char* iconName = nullptr;  // resources/icons/<iconName>.png; overrides `icon` when set
};

// Width of a collapsed-panel flyout button (icon + short title + chevron).
constexpr float kRibbonCollapsedW = 72.f;

struct RibbonFitResult {
  RibbonBreakpoint breakpoint = RibbonBreakpoint::Wide;
  std::vector<size_t> inlineIdx;    // section indices rendered in the ribbon row, in order
  std::vector<size_t> overflowIdx;  // section indices collapsed to a flyout button, in order
  float width = 0.f;                // total width actually consumed (inline sections + gaps + collapsed)
};

// Decides which of `specs` (in order) fit inline at `availW` and which overflow into a popup
// (ADR-038 (a)/(b)). Pure arithmetic, no ImGui calls, so it can run before RibbonToolsLeft's
// BeginChild needs a size — the section closures themselves don't execute until RenderRibbonFit.
static RibbonFitResult DecideRibbonFit(const std::vector<RibbonSectionSpec>& specs, float availW, float gap) {
  RibbonFitResult r;
  if (specs.empty())
    return r;
  float totalWide = 0.f, totalMed = 0.f;
  for (size_t i = 0; i < specs.size(); ++i) {
    totalWide += specs[i].wideW + (i ? gap : 0.f);
    totalMed += specs[i].mediumW + (i ? gap : 0.f);
  }
  if (availW >= totalWide) {
    r.breakpoint = RibbonBreakpoint::Wide;
    for (size_t i = 0; i < specs.size(); ++i)
      r.inlineIdx.push_back(i);
    r.width = totalWide;
    return r;
  }
  if (availW >= totalMed) {
    r.breakpoint = RibbonBreakpoint::Medium;
    for (size_t i = 0; i < specs.size(); ++i)
      r.inlineIdx.push_back(i);
    r.width = totalMed;
    return r;
  }
  r.breakpoint = RibbonBreakpoint::Narrow;
  // Civil 3D behaviour: collapse panels from the RIGHT, one at a time, each into a titled
  // flyout button (kRibbonCollapsedW wide). Once a panel collapses, every panel to its right
  // stays collapsed too — the row never has a collapsed panel left of an inline one.
  float used = 0.f;
  bool collapsing = false;
  for (size_t i = 0; i < specs.size(); ++i) {
    const float needInline = specs[i].mediumW + (i ? gap : 0.f);
    const float needCollapsed = kRibbonCollapsedW + (i ? gap : 0.f);
    // The first section always renders inline even if it alone would overflow `availW`
    // (TASK-105 ASSUMPTION-2) — an empty-looking tab reads as broken.
    if (!collapsing && (r.inlineIdx.empty() || used + needInline <= availW)) {
      r.inlineIdx.push_back(i);
      used += needInline;
    } else {
      collapsing = true;
      r.overflowIdx.push_back(i);
      used += needCollapsed;
    }
  }
  r.width = used;
  return r;
}

// Trim `s` to fit `maxW` px at the current font, appending "…" when shortened.
static std::string RibbonTruncate(const char* s, float maxW) {
  if (ImGui::CalcTextSize(s).x <= maxW)
    return s;
  std::string out;
  for (const char* p = s; *p; ++p) {
    std::string cand = out + *p + "\xE2\x80\xA6";
    if (ImGui::CalcTextSize(cand.c_str()).x > maxW)
      break;
    out.push_back(*p);
  }
  return out + "\xE2\x80\xA6";
}

// Renders a tab's sections per `fit` — inline sections at Medium metrics when the breakpoint isn't
// Wide, overflow sections inside a "More" popup at full Wide metrics (ADR-038 (b)/(c)). Must run
// inside the child window `fit.width` was used to size. Leaves `curCompact` false on return.
static void RenderRibbonFit(const std::vector<RibbonSectionSpec>& specs, const RibbonFitResult& fit,
                             float gap, float colH, bool& curCompact, const char* morePopupId) {
  if (specs.empty())
    return;
  curCompact = (fit.breakpoint != RibbonBreakpoint::Wide);
  for (size_t k = 0; k < fit.inlineIdx.size(); ++k) {
    if (k)
      ImGui::SameLine(0, gap);
    specs[fit.inlineIdx[k]].render();
  }
  // Each overflowed panel collapses to its own titled flyout button (Civil 3D-style).
  (void)morePopupId;
  for (size_t k = 0; k < fit.overflowIdx.size(); ++k) {
    const size_t si = fit.overflowIdx[k];
    const RibbonSectionSpec& s = specs[si];
    ImGui::SameLine(0, gap);
    char pid[32];
    std::snprintf(pid, sizeof(pid), "##ribcol%zu", si);
    const bool hasName = s.name && s.name[0];
    const std::string label = hasName ? RibbonTruncate(s.name, kRibbonCollapsedW - 12.f) : std::string();
    ImVec2 cur = ImGui::GetCursorScreenPos();
    if (RibbonButtonEx(pid, s.icon, hasName ? label.c_str() : nullptr, ImVec2(kRibbonCollapsedW, colH),
                       hasName ? RibbonLabel::Below : RibbonLabel::None, s.iconName))
      ImGui::OpenPopup(pid);
    // Down-chevron affordance, bottom-centre.
    ImGui::GetWindowDrawList()->AddTriangleFilled(
        ImVec2(cur.x + kRibbonCollapsedW * 0.5f - 3.f, cur.y + colH - 7.f),
        ImVec2(cur.x + kRibbonCollapsedW * 0.5f + 3.f, cur.y + colH - 7.f),
        ImVec2(cur.x + kRibbonCollapsedW * 0.5f, cur.y + colH - 3.f),
        ImGui::GetColorU32(ImGuiCol_Text));
    if (hasName) {
      char tip[96];
      std::snprintf(tip, sizeof(tip), "%s panel", s.name);
      RibbonItemHelp(tip);
    }
    ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(FLT_MAX, FLT_MAX));
    if (ImGui::BeginPopup(pid)) {
      curCompact = false;
      s.render();
      ImGui::EndPopup();
    }
  }
  curCompact = false;
}

// Forward-declared here (defined below, ~line 5650): reopening the same anonymous namespace so the
// REQ-302 tab strip in DrawRibbonBar below can reuse the Model/Layout tab toggle styling instead of
// inventing a second one.
namespace {
void PushModeToggleButtonColors(bool on, int themeIdx);
void PopModeToggleButtonColors(bool on);
}  // namespace

/// Run a command exactly as if it had been typed.
///
/// ProcessCommandLineSubmit takes a mutable buffer because the real command line owns one; ribbon
/// buttons have a literal. Copying into a scratch buffer here means every button goes through the
/// SAME parser the keyboard does, so a button and its documented command can never drift apart —
/// which is the DIMSTY-vs-UNITS failure this session already found once.
static void ProcessCommandLineSubmitStr(AppCommandState& cmd, const char* text,
                                        std::vector<std::string>& log) {
  char buf[256];
  std::snprintf(buf, sizeof(buf), "%s", text);
  ProcessCommandLineSubmit(buf, static_cast<int>(sizeof(buf)), cmd, log);
}

/// What to call the active coordinate frame (REQ-154): "WCS", a saved name when the frame IS one of
/// them, or "Unnamed" for a frame built but not saved.
///
/// Shared by the ViewCube dropdown and the Coordinates ribbon panel. Two widgets naming the same
/// thing must not be able to name it differently — and "Unnamed" is AutoCAD's own word, carrying the
/// useful hint that `UCS N` would keep this frame.
static std::string CadUcsFrameLabel(const AppCommandState& cmd) {
  if (CadUcsIsWorld(cmd))
    return "WCS";
  for (const NamedUcs& n : cmd.ucsNamed) {
    if (ucs::FramesMatch(n.frame, cmd.activeUcs))
      return n.name;
  }
  return "Unnamed";
}

void DrawRibbonBar(float height, AppCommandState& cmd, std::vector<std::string>& log) {
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 3));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(5, 4));
  // nanoCAD-style flat system-gray toolbar band; icons float on it as 3D buttons.
  ImGui::PushStyleColor(ImGuiCol_ChildBg, g_chrome.bandFace);
  ImGui::BeginChild("RibbonStrip", ImVec2(0, height), true,
                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  ImGui::PopStyleColor();

  // Top-lit vertical gradient over the flat band so the strip has depth.
  {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 rMin = ImGui::GetWindowPos();
    const ImVec2 rMax = ImVec2(rMin.x + ImGui::GetWindowSize().x, rMin.y + ImGui::GetWindowSize().y);
    dl->AddRectFilledMultiColor(rMin, rMax, HexU32(0x424242), HexU32(0x424242), HexU32(0x2A2A2A),
                                HexU32(0x2A2A2A));
    // Bright hairline along the very top, dark hairline along the bottom — frames the band.
    dl->AddLine(rMin, ImVec2(rMax.x, rMin.y), HexU32(0x555555), 1.f);
    dl->AddLine(ImVec2(rMin.x, rMax.y - 1.f), ImVec2(rMax.x, rMax.y - 1.f), HexU32(0x1E1E1E), 1.f);
  }

  const bool ribbonPaperSpaceEarly =
      cmd.activeSpaceIndex != kModelSpaceIndex && !InFloatingModelSpace(cmd);
  const int selSurfIdx = ribbonPaperSpaceEarly ? -1 : FirstSelectedSurfaceIndex(cmd);
  const int nSvyPts = ribbonPaperSpaceEarly ? 0 : CountSelectedSurveyPoints(cmd);
  const bool hasSvyPts = nSvyPts > 0;
  if (selSurfIdx >= 0) {
    if (!cmd.surfaceContextualRibbonArmed) {
      if (cmd.activeRibbonTab >= 0 && cmd.activeRibbonTab < kRibbonTabCount)
        cmd.ribbonTabBeforeSurfaceCtx = cmd.activeRibbonTab;
      cmd.activeRibbonTab = kRibbonTabSurfaceCtx;
      cmd.surfaceContextualRibbonArmed = true;
    }
  } else if (cmd.surfaceContextualRibbonArmed) {
    if (cmd.activeRibbonTab == kRibbonTabSurfaceCtx) {
      int prev = cmd.ribbonTabBeforeSurfaceCtx;
      if (prev < 0 || prev >= kRibbonTabCount)
        prev = kRibbonTabHome;
      if (hasSvyPts)
        prev = kRibbonTabSurveyPointCtx;
      cmd.activeRibbonTab = prev;
    }
    cmd.surfaceContextualRibbonArmed = false;
  }
  if (hasSvyPts) {
    if (!cmd.surveyPointContextualRibbonArmed) {
      if (cmd.activeRibbonTab >= 0 && cmd.activeRibbonTab < kRibbonTabCount)
        cmd.ribbonTabBeforeSurveyPointCtx = cmd.activeRibbonTab;
      if (cmd.activeRibbonTab != kRibbonTabSurfaceCtx)
        cmd.activeRibbonTab = kRibbonTabSurveyPointCtx;
      cmd.surveyPointContextualRibbonArmed = true;
    }
  } else if (cmd.surveyPointContextualRibbonArmed) {
    if (cmd.activeRibbonTab == kRibbonTabSurveyPointCtx) {
      int prev = cmd.ribbonTabBeforeSurveyPointCtx;
      if (prev < 0 || prev >= kRibbonTabCount)
        prev = kRibbonTabHome;
      if (selSurfIdx >= 0)
        prev = kRibbonTabSurfaceCtx;
      cmd.activeRibbonTab = prev;
    }
    cmd.surveyPointContextualRibbonArmed = false;
  }

  const bool inBedit = !cmd.blockEditorName.empty();
  if (inBedit) {
    if (!cmd.blockEditorContextualRibbonArmed) {
      if (cmd.activeRibbonTab >= 0 && cmd.activeRibbonTab < kRibbonTabCount)
        cmd.ribbonTabBeforeBlockEditor = cmd.activeRibbonTab;
      cmd.activeRibbonTab = kRibbonTabBlockEditor;
      cmd.blockEditorContextualRibbonArmed = true;
    }
  } else if (cmd.blockEditorContextualRibbonArmed) {
    if (cmd.activeRibbonTab == kRibbonTabBlockEditor) {
      int prev = cmd.ribbonTabBeforeBlockEditor;
      if (prev < 0 || prev >= kRibbonTabCount)
        prev = kRibbonTabHome;
      cmd.activeRibbonTab = prev;
    }
    cmd.blockEditorContextualRibbonArmed = false;
  }

  // REQ-302 tab strip: Home/Insert/Annotate/View/Manage/Output/Survey. Reuses the Model/Layout
  // tab toggle styling (PushModeToggleButtonColors, ~CadUi.cpp:6308, REQ-025/026 precedent) so the
  // active tab reads the same way the active space tab already does, rather than a second style.
  // Extra vertical FramePadding (user GUI-pass feedback, 2026-08-25: tab text sat flush against
  // the button's bottom edge) plus a gap row below the strip so it doesn't crowd the panels.
  const float kRibbonTabStripH = 18.f + g_chrome.ribbonTabPadY * 2.f;
  {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.f, g_chrome.ribbonTabPadY));
    auto ribbonTab = [&](const char* label, int tabIdx) {
      const bool active = cmd.activeRibbonTab == tabIdx;
      PushModeToggleButtonColors(active, cmd.displayColorThemeIdx);
      const bool clicked = ImGui::Button(label, ImVec2(0.f, kRibbonTabStripH));
      PopModeToggleButtonColors(active);
      if (clicked)
        cmd.activeRibbonTab = tabIdx;
      ImGui::SameLine(0, 2);
    };
    ribbonTab("Home",     kRibbonTabHome);
    ribbonTab("Insert",   kRibbonTabInsert);
    ribbonTab("Annotate", kRibbonTabAnnotate);
    ribbonTab("View",     kRibbonTabView);
    ribbonTab("Manage",   kRibbonTabManage);
    ribbonTab("Output",   kRibbonTabOutput);
    ribbonTab("Survey",   kRibbonTabSurvey);
    if (selSurfIdx >= 0) {
      char surfTab[160];
      const std::string& nm = cmd.cadSurfaces[static_cast<size_t>(selSurfIdx)].name;
      std::snprintf(surfTab, sizeof(surfTab), "Tin Surface: %s", nm.c_str());
      const bool ctxOn = cmd.activeRibbonTab == kRibbonTabSurfaceCtx;
      ImGui::PushStyleColor(ImGuiCol_Button,        IM_COL32(0, 120, 215, ctxOn ? 255 : 180));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(30, 144, 255, 255));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive,  IM_COL32(0, 90, 180, 255));
      ImGui::PushStyleColor(ImGuiCol_Text,          IM_COL32(255, 255, 255, 255));
      if (ImGui::Button(surfTab, ImVec2(0.f, kRibbonTabStripH)))
        cmd.activeRibbonTab = kRibbonTabSurfaceCtx;
      ImGui::PopStyleColor(4);
      ImGui::SameLine(0, 2);
    }
    if (hasSvyPts) {
      char ptTab[160];
      if (nSvyPts == 1) {
        const int pxi = FirstSelectedSurveyPointIndex(cmd);
        const int pid = (pxi >= 0) ? cmd.surveyPoints[static_cast<size_t>(pxi)].id : 0;
        std::snprintf(ptTab, sizeof(ptTab), "SURVEY Point: %d", pid);
      } else {
        std::snprintf(ptTab, sizeof(ptTab), "SURVEY Points");
      }
      const bool ptOn = cmd.activeRibbonTab == kRibbonTabSurveyPointCtx;
      ImGui::PushStyleColor(ImGuiCol_Button,        IM_COL32(0, 120, 215, ptOn ? 255 : 180));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(30, 144, 255, 255));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive,  IM_COL32(0, 90, 180, 255));
      ImGui::PushStyleColor(ImGuiCol_Text,          IM_COL32(255, 255, 255, 255));
      if (ImGui::Button(ptTab, ImVec2(0.f, kRibbonTabStripH)))
        cmd.activeRibbonTab = kRibbonTabSurveyPointCtx;
      ImGui::PopStyleColor(4);
      ImGui::SameLine(0, 2);
    }
    if (inBedit) {
      const bool beOn = cmd.activeRibbonTab == kRibbonTabBlockEditor;
      ImGui::PushStyleColor(ImGuiCol_Button,        IM_COL32(0, 120, 215, beOn ? 255 : 180));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(30, 144, 255, 255));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive,  IM_COL32(0, 90, 180, 255));
      ImGui::PushStyleColor(ImGuiCol_Text,          IM_COL32(255, 255, 255, 255));
      if (ImGui::Button("Block Editor", ImVec2(0.f, kRibbonTabStripH)))
        cmd.activeRibbonTab = kRibbonTabBlockEditor;
      ImGui::PopStyleColor(4);
      ImGui::SameLine(0, 2);
    }
    ImGui::PopStyleVar();
    ImGui::Dummy(ImVec2(1.f, g_chrome.ribbonTabStripGapY));
  }

  ImGui::SetWindowFontScale(g_chrome.ribbonBodyFontScale);

  const ImGuiStyle& st = ImGui::GetStyle();
  // Gutter below the sections so the panel titles ("Draw", "Modify", …) are not
  // flush against the ribbon's bottom edge. WindowPadding cannot express this —
  // it is symmetric, and adding it at the top too would waste the band's height.
  const float panelH = height - kRibbonTabStripH - g_chrome.ribbonTabStripGapY - st.WindowPadding.y * 2.f -
                        g_chrome.ribbonBottomGutter;
  constexpr float kLayerPanelW = 288.f;

  // Civil 3D-style panel metrics: a button column fills the height above the
  // bottom title; small labeled buttons stack 3 to a column; the icon grid
  // uses 2 rows of square cells.
  const float colH      = std::max(48.f, RibbonPanelContentH(panelH) - 8.f);
  const float rowH      = std::floor((colH - 4.f) / 3.f);
  const float gridCell  = std::floor((colH - 2.f) / 2.f);
  // Civil 3D's Home-tab icon grids (Draw, Palettes, layer state, Clipboard) are 3 rows of small
  // cells, not 2 — a 2-row cell clipped the bottom row.
  const float gridCell3 = std::floor((colH - 6.f) / 3.f);
  constexpr float largeW = 60.f;
  constexpr float kTsLargeW = 76.f;
  auto belowW = [&](const char* label) { return RibbonBelowButtonWidth(label, kTsLargeW); };
  auto capW = [&](const char* caption) { return std::max(kTsLargeW, RibbonMaxLineWidth(caption) + 16.f); };

  // REQ-302 increment 2 (ADR-038 (a)): `curCompact` is read by colW()/smallBtn() below — Medium
  // metrics are simply "the same button code, with this flag true." largeBtn/gridBtn are unaffected
  // (grid cells are already icon-only; a large button's label-below layout doesn't shrink further).
  bool curCompact = false;
  auto largeBtn = [&](const char* id, RibbonIconKind ic, const char* label) {
    const bool hit = RibbonButtonEx(id, ic, label, ImVec2(largeW, colH), RibbonLabel::Below);
    if (hit)
      DevShell_OnUi(id);
    return hit;
  };
  auto smallBtn = [&](const char* id, RibbonIconKind ic, const char* label, float w) {
    const RibbonLabel mode = curCompact ? RibbonLabel::None : RibbonLabel::Right;
    const bool hit = RibbonButtonEx(id, ic, curCompact ? nullptr : label, ImVec2(curCompact ? rowH : w, rowH), mode);
    if (hit)
      DevShell_OnUi(id);
    return hit;
  };
  auto gridBtn3 = [&](const char* id, RibbonIconKind ic) {
    const bool hit = RibbonButtonEx(id, ic, nullptr, ImVec2(gridCell3, gridCell3), RibbonLabel::None);
    if (hit)
      DevShell_OnUi(id);
    return hit;
  };
  (void)gridCell;
  // Column width = small icon + gap + the widest label in the column — or, compact, just the icon
  // (REQ-302 increment 2 Medium/Narrow: "switch button labels to icons," issue #83 strategy 3).
  auto colW = [&](std::initializer_list<const char*> labels) {
    if (curCompact)
      return rowH;
    float m = 0.f;
    for (const char* l : labels)
      m = std::max(m, ImGui::CalcTextSize(l).x);
    // icon (rowH-6) + 3px gaps each side + label; matches RibbonButtonEx Right-mode layout with
    // a small margin. (Was +8 — trimmed once the child-font-scale bug was fixed so the extra
    // slack is no longer needed, which is what lets the wide Home tab fit its full labels.)
    return rowH + 6.f + m;
  };

  // Home-tab helpers. MUST be at DrawRibbonBar function scope (not inside the `if (Home)` block):
  // the ribbonSpecs render closures capture them by reference and are invoked LATER by
  // RenderRibbonFit, after that block has exited. (Declaring them in the block left dangling
  // references that a Release build's stack reuse turned into a crash.)
  const float gcHome = gridCell3;
  // Labelled Home rows (Create Ground Data / Design / Profile) pack 3 to a column at a compact
  // height so the rows aren't spread out — the icon still fills most of the button.
  const float rowHome = std::min(rowH, 34.f);
  auto nyiGrid = [&](const char* id, const char* ic, const char* label) {
    RibbonNyiButton(id, RibbonIconKind::Nyi, label, ImVec2(gcHome, gcHome), RibbonLabel::None, ic);
  };
  auto nyiRow = [&](const char* id, const char* ic, const char* label, float w) {
    RibbonNyiButton(id, RibbonIconKind::Nyi, label, ImVec2(curCompact ? rowHome : w, rowHome),
                    curCompact ? RibbonLabel::None : RibbonLabel::Right, ic);
  };
  auto homeRow = [&](const char* id, RibbonIconKind ic, const char* label, float w) {
    return RibbonButtonEx(id, ic, curCompact ? nullptr : label, ImVec2(curCompact ? rowHome : w, rowHome),
                          curCompact ? RibbonLabel::None : RibbonLabel::Right);
  };

  // Insert-tab helpers — MUST be at function scope for the same reason the Home ones are (captured
  // by reference, invoked later by RenderRibbonFit). `iconName` is a resources/icons/<name>.png that
  // is not in the RibbonIconKind enum (library art reused for a not-yet-enumerated command).
  auto insRow = [&](const char* id, const char* iconName, const char* label, float w) {
    return RibbonButtonEx(id, RibbonIconKind::Nyi, curCompact ? nullptr : label,
                          ImVec2(curCompact ? rowH : w, rowH),
                          curCompact ? RibbonLabel::None : RibbonLabel::Right, iconName);
  };
  auto insNyi = [&](const char* id, const char* iconName, const char* label, float w) {
    // label stays non-null even when compact (RibbonNyiButton asserts on it and still needs it for
    // the auto NYI tooltip); RibbonLabel::None is what hides the text. Matches the Home `nyiRow`.
    RibbonNyiButton(id, RibbonIconKind::Nyi, label,
                    ImVec2(curCompact ? rowH : w, rowH),
                    curCompact ? RibbonLabel::None : RibbonLabel::Right, iconName);
  };

  const float annStyleW = 150.f;  // text-style dropdown width in the Annotate section (REQ-044)
  // Civil 3D Annotate tab shows a style/scale combo in most panels. Where GoSurvey has no picker
  // yet, render a disabled combo-shaped placeholder with the automatic NYI tooltip. MUST be at
  // function scope — the ribbonSpecs closures capture it and run after the tab block exits (the
  // fc0669c dangling-reference crash).
  auto annNyiCombo = [&](const char* id, const char* text) {
    ImGui::BeginDisabled();
    ImGui::Button((std::string(text) + "  \xE2\x96\xBC" + id).c_str(), ImVec2(annStyleW, 0.f));
    ImGui::EndDisabled();
    char tip[96];
    std::snprintf(tip, sizeof(tip), "%s \xE2\x80\x94 not implemented yet.", text);
    RibbonItemHelp(tip, ImGuiHoveredFlags_AllowWhenDisabled);
  };
  // Large (icon-above-label) NYI button using a resources/icons/<name>.png. Function scope for the
  // same reason as insRow/insNyi — captured by the deferred ribbonSpecs closures. Used by the
  // Annotate and Manage tab rebuilds.
  auto nyiLarge = [&](const char* id, const char* iconName, const char* label) {
    RibbonNyiButton(id, RibbonIconKind::Nyi, label, ImVec2(belowW(label), colH), RibbonLabel::Below, iconName);
  };
  // Visual-style combo width measured from its longest option text, not a guessed constant — a
  // hardcoded 132px clipped "2D Wireframe" (user GUI-pass feedback, 2026-08-25).
  const float visualStyleComboW = ImGui::CalcTextSize("2D Wireframe").x + 40.f;
  // REQ-106. Sized to the longest thing the combo shows, so a saved view with a long name does not
  // reflow the ribbon every time it becomes active.
  const float namedViewComboW = ImGui::CalcTextSize("SW Isometric").x + 56.f;
  // REQ-154. Sized to the widest label the frame combo shows.
  const float ucsComboW = ImGui::CalcTextSize("Coordinate system").x + 30.f;
  // REQ-032 contextual ribbon: Layout tools in paper space, but the normal model ribbon while editing a
  // viewport in place (floating model space, REQ-036) so the draw/modify tools are available.
  const bool ribbonPaperSpace = cmd.activeSpaceIndex != kModelSpaceIndex && !InFloatingModelSpace(cmd);

  // REQ-302 increment 2 (ADR-038 (a)): each tab's own section widths, computed once at Wide
  // metrics (`W`) and once at Medium (`M`) — same formulas as increment 1 shipped, since colW()
  // above already resolves compact vs. not; nothing here duplicates a button-sizing decision.
  struct RibbonTabWidths {
    float wEdit, wDraw, wMod, wInq, wSrv, wAnalyze, wView, wLayout;
    float wViewSettings;  // REQ-302 increment 3 (Insert/Output tabs now size their sections inline)
    float wNamedViews = 0.f;  // REQ-106
    float wCoords = 0.f;      // REQ-154
  };
  auto computeTabWidths = [&](bool compact) {
    curCompact = compact;
    RibbonTabWidths w{};
    w.wEdit = 8.f + largeW + 4.f + colW({"Copy", "Undo", "Redo"});
    w.wDraw = 8.f + gridCell * 4.f + 4.f * 3.f;  // grid buttons are already icon-only — no Medium delta
    // Four columns: a small-button column is 3 tall (colH), so a 4th item in one BeginGroup is
    // clipped by the child window's own bounds — the same "fourth needs its own column" rule
    // Inquiry/Survey below already follow. Join/Mirror/Lengthen exactly fills one column;
    // Extend/Break/Stretch exactly fills a second.
    w.wMod  = 8.f + largeW + 4.f + colW({"Copy", "Rotate", "Scale"}) + 4.f +
              colW({"Erase", "Trim", "Offset"}) + 4.f + colW({"Join", "Mirror", "Lengthen"}) + 4.f +
              colW({"Extend", "Break", "Stretch"}) + 4.f + colW({"Fillet", "Chamfer"});
    // Annotate tab sections compute their own widths inline (GUI-pass 2026-08-30, C3D 8-panel
    // rebuild) — no wAnnText/wAnnDim entries here.
    // Two columns: the panel is three small buttons tall, so a fourth in one column is clipped.
    // Aligned/Linear moved to Annotate's new Dimensions section above (2026-08-25 follow-up) — ID
    // Point/Elev-Grade are the two that remain genuinely survey-scoped inquiry tools.
    w.wInq  = 8.f + colW({"ID Point"}) + 4.f + colW({"Elev/Grade"});
    // Three columns after Points: the panel is three small buttons tall, so Surfaces/Volumes/
    // Grades/Groups (four items) needs its own two-column split, same as Modify's Extend/Break/
    // Stretch + Fillet/Chamfer split above (fixed 2026-08-25 — see the Survey section body).
    w.wSrv  = 8.f + largeW + 4.f + colW({"Inverse", "Traverse"}) + 4.f +
              colW({"Surfaces", "Volumes"}) + 4.f + colW({"Elev", "Drop"}) + 4.f +
              colW({"Shed", "Report"}) + 4.f + colW({"Grades", "Groups"});
    w.wAnalyze = 8.f + colW({"Slope", "Dir", "Arrows"}) + 4.f + colW({"Catch", "Stats", "Rebuild"}) + 4.f +
                 colW({"Breakln", "Contour", "Boundry"}) + 4.f + colW({"Vol Surf", "Props"});
    w.wView = 8.f + colW({"Extents", "Window"}) + 8.f + visualStyleComboW;  // REQ-064
    // REQ-106 Named Views: the combo carries the longest preset name, and two stacked buttons sit
    // beside it — the same shape AutoCAD's own Named Views panel uses.
    w.wNamedViews = 8.f + namedViewComboW + 4.f + colW({"New View", "Manager"});
    // REQ-154 Coordinates: three two-button columns and the frame combo, AutoCAD's own grouping.
    w.wCoords = 8.f + 3.f * (colW({"3-Point", "Object"}) + 4.f) + 8.f + ucsComboW;
    // REQ-302 increment 3: Plot/Batch Plot moved out to Output's "Plot" section — Layout keeps
    // only the viewport-authoring tools (Rect VP is a largeBtn placed outside colW; Poly VP is
    // the one column here).
    w.wLayout = 8.f + largeW + 4.f + colW({"Poly VP"});
    // REQ-302 increment 3 (content audit): View tab Settings section. Insert/Output/Annotate/Manage
    // tabs size their sections inline (GUI-pass 2026-08-30 C3D rebuilds).
    w.wViewSettings = 8.f + colW({"Settings"}) + 4.f + colW({"Toolspace"});
    return w;
  };
  const RibbonTabWidths W = computeTabWidths(false);
  const RibbonTabWidths M = computeTabWidths(true);
  curCompact = false;  // reset — RenderRibbonFit sets this per-section at actual render time

  // REQ-302 increment 2 (ADR-038): build the active tab's sections as deferred render closures,
  // decide Wide/Medium/Narrow from available width (pure arithmetic, no ImGui calls — safe to run
  // before RibbonToolsLeft's BeginChild needs a size), then size RibbonToolsLeft to exactly what
  // will be placed (inline sections, plus a "More" button in Narrow) — never wider than available,
  // so nothing can clip. No section's own body changes below; only what wraps it.
  const float secGap = 6.f;
  std::vector<RibbonSectionSpec> ribbonSpecs;

  // REQ-302 / GUI-pass 2026-08-30: the Home tab mirrors Civil 3D's Home ribbon 1:1 — Palettes,
  // Explore, Optimize, Create Ground Data, Create Design, Profile & Section Views, Draw, Modify,
  // Layers, Clipboard. Commands GoSurvey already implements are wired into their C3D-equivalent
  // slot; every other button is greyed with an automatic "… — not implemented yet." tooltip
  // (RibbonNyiButton). Responsive per-panel collapse (the C3D flyout behaviour) is a follow-up.
  if (cmd.activeRibbonTab == kRibbonTabHome) {
    const float gc = gcHome;

    // ---- Palettes -----------------------------------------------------------
    {
      const float w = 8.f + largeW + 4.f + gc * 3.f + 4.f * 2.f;
      ribbonSpecs.push_back({w, w, [&, w]() {
        RibbonSectionBegin("RibbonSecPalettes", "Palettes", w, panelH);
        if (largeBtn("##RibbonToolspaceHome", RibbonIconKind::Toolspace, "Toolspace"))
          cmd.showToolspaceWindow = true;
        RibbonItemHelp("Toolspace — drawing explorer (Prospector and Settings).\nCommand bar: TOOLSPACE");
        ImGui::SameLine(0, 4);
        ImGui::BeginGroup();
        nyiGrid("##PalPanorama", "c3d_panorama", "Panorama");           ImGui::SameLine(0, 4);
        nyiGrid("##PalProps", "c3d_properties", "Properties");             ImGui::SameLine(0, 4);
        nyiGrid("##PalRefMgr", "c3d_refmgr", "Reference Manager");
        nyiGrid("##PalCompEd", "c3d_comped", "Component Editor");       ImGui::SameLine(0, 4);
        nyiGrid("##PalSettings", "c3d_dwgsettings", "Drawing Settings");     ImGui::SameLine(0, 4);
        nyiGrid("##PalWorkFolder", "c3d_workfolder", "Set Working Folder");
        ImGui::EndGroup();
        RibbonSectionEnd();
      }, "Palettes", RibbonIconKind::Toolspace});
    }

    // ---- Explore -----------------------------------------------------------
    {
      const float bw = belowW("Project Explorer");
      const float w = 8.f + bw;
      ribbonSpecs.push_back({w, w, [&, w, bw]() {
        RibbonSectionBegin("RibbonSecExplore", "Explore", w, panelH);
        RibbonNyiButton("##ExpProjExplorer", RibbonIconKind::Nyi, "Project\nExplorer",
                        ImVec2(bw, colH), RibbonLabel::Below, "c3d_projexplorer");
        RibbonSectionEnd();
      }, "Explore", RibbonIconKind::Nyi, "c3d_projexplorer"});
    }

    // ---- Optimize ---------------------------------------------------------
    {
      const float bw = belowW("Grading Optimization");
      const float w = 8.f + bw;
      ribbonSpecs.push_back({w, w, [&, w, bw]() {
        RibbonSectionBegin("RibbonSecOptimize", "Optimize", w, panelH);
        RibbonNyiButton("##OptGrading", RibbonIconKind::Nyi, "Grading\nOptimization",
                        ImVec2(bw, colH), RibbonLabel::Below, "c3d_gradingopt");
        RibbonSectionEnd();
      }, "Optimize", RibbonIconKind::Nyi, "c3d_gradingopt"});
    }

    if (!ribbonPaperSpace) {
      // ---- Create Ground Data -------------------------------------------
      {
        const float cA = std::max(colW({"Points", "Feature Line", "Traverse"}), capW("Create Ground Data") - 60.f);
        const float cB = colW({"Surfaces", "Grading"});
        const float w = 8.f + cA + 4.f + cB;
        const float mw = 8.f + rowHome + 4.f + rowHome;
        ribbonSpecs.push_back({w, mw, [&, w, mw, cA, cB]() {
          RibbonSectionBegin("RibbonSecGroundData", "Create Ground Data", curCompact ? mw : w, panelH);
          ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.f, 1.f));
          ImGui::BeginGroup();
          if (homeRow("##CgdPoints", RibbonIconKind::SurveyPoint, "Points", cA))
            StartCreatePointsCommand(cmd, log);
          RibbonItemHelp("Create Points — pick or type survey points.\nCommand bar: CREATEPOINTS");
          nyiRow("##CgdFeatureLine", "c3d_featureline", "Feature Line", cA);
          nyiRow("##CgdTraverse", "c3d_traverse2", "Traverse", cA);
          ImGui::EndGroup();
          ImGui::SameLine(0, 4);
          ImGui::BeginGroup();
          nyiRow("##CgdSurfaces", "c3d_surfaces", "Surfaces", cB);
          nyiRow("##CgdGrading", "c3d_grading", "Grading", cB);
          ImGui::EndGroup();
          ImGui::PopStyleVar();
          RibbonSectionEnd();
        }, "Create Ground Data", RibbonIconKind::SurveyPoint});
      }

      // ---- Create Design ---------------------------------------------------
      {
        const float c1 = colW({"Parcel", "Feature Line", "Grading"});
        const float c2 = colW({"Alignment", "Profile", "Corridor"});
        const float c3 = colW({"Intersections", "Assembly", "Pipe Network"});
        const float c4 = colW({"Pond", "Underground Storage", "Channel"});
        const float w = 8.f + c1 + 4.f + c2 + 4.f + c3 + 4.f + c4;
        const float mw = 8.f + rowHome * 4.f + 4.f * 3.f;
        ribbonSpecs.push_back({w, mw, [&, w, mw]() {
          RibbonSectionBegin("RibbonSecCreateDesign", "Create Design", curCompact ? mw : w, panelH);
          ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.f, 1.f));
          const float d1 = colW({"Parcel", "Feature Line", "Grading"});
          const float d2 = colW({"Alignment", "Profile", "Corridor"});
          const float d3 = colW({"Intersections", "Assembly", "Pipe Network"});
          const float d4 = colW({"Pond", "Underground Storage", "Channel"});
          ImGui::BeginGroup();
          nyiRow("##CdParcel", "c3d_parcel", "Parcel", d1);
          nyiRow("##CdFeatureLine", "c3d_featureline", "Feature Line", d1);
          nyiRow("##CdGrading", "c3d_grading", "Grading", d1);
          ImGui::EndGroup(); ImGui::SameLine(0, 4);
          ImGui::BeginGroup();
          nyiRow("##CdAlignment", "c3d_alignment", "Alignment", d2);
          nyiRow("##CdProfile", "c3d_profile", "Profile", d2);
          nyiRow("##CdCorridor", "c3d_corridor", "Corridor", d2);
          ImGui::EndGroup(); ImGui::SameLine(0, 4);
          ImGui::BeginGroup();
          nyiRow("##CdIntersections", "c3d_intersections", "Intersections", d3);
          nyiRow("##CdAssembly", "c3d_assembly", "Assembly", d3);
          nyiRow("##CdPipeNetwork", "c3d_pipenet", "Pipe Network", d3);
          ImGui::EndGroup(); ImGui::SameLine(0, 4);
          ImGui::BeginGroup();
          nyiRow("##CdPond", "c3d_pond", "Pond", d4);
          nyiRow("##CdUgStorage", "c3d_ugstorage", "Underground Storage", d4);
          nyiRow("##CdChannel", "c3d_channel", "Channel", d4);
          ImGui::EndGroup();
          ImGui::PopStyleVar();
          RibbonSectionEnd();
        }, "Create Design", RibbonIconKind::Nyi, "c3d_alignment"});
      }

      // ---- Profile & Section Views ---------------------------------------
      {
        const float cP = std::max(colW({"Profile View", "Sample Lines", "Section Views"}),
                                  capW("Profile & Section Views") - 8.f);
        const float w = 8.f + cP;
        const float mw = 8.f + rowHome;
        ribbonSpecs.push_back({w, mw, [&, w, mw, cP]() {
          RibbonSectionBegin("RibbonSecProfSect", "Profile & Section Views", curCompact ? mw : w, panelH);
          ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.f, 1.f));
          ImGui::BeginGroup();
          nyiRow("##PsvProfileView", "c3d_profileview", "Profile View", cP);
          nyiRow("##PsvSampleLines", "c3d_samplelines", "Sample Lines", cP);
          nyiRow("##PsvSectionViews", "c3d_sectionviews", "Section Views", cP);
          ImGui::EndGroup();
          ImGui::PopStyleVar();
          RibbonSectionEnd();
        }, "Profile & Section Views", RibbonIconKind::Nyi, "c3d_profileview"});
      }

      // ---- Draw ---------------------------------------------------------
      {
        const float w = 8.f + gcHome * 4.f + 4.f * 3.f;
        ribbonSpecs.push_back({w, w, [&, w]() {
          RibbonSectionBegin("RibbonSecDraw", "Draw", w, panelH);
          if (gridBtn3("##RibbonLine", RibbonIconKind::Line)) StartLineCommand(cmd, log);
          RibbonItemHelp("Line — draw straight segments between points.\nCommand bar: LINE or L");
          ImGui::SameLine(0, 4);
          if (gridBtn3("##RibbonArc", RibbonIconKind::Arc)) StartArcCommand(cmd, log);
          RibbonItemHelp("Arc — three-point arc (start, mid, end).\nCommand bar: ARC");
          ImGui::SameLine(0, 4);
          if (gridBtn3("##RibbonPLine", RibbonIconKind::Polyline)) StartPolylineCommand(cmd, log);
          RibbonItemHelp("Polyline — chain of segments; optional close.\nCommand bar: POLYLINE or PL");
          ImGui::SameLine(0, 4);
          nyiGrid("##RibbonSpline", "c3d_spline", "Spline");

          if (gridBtn3("##RibbonCircle", RibbonIconKind::Circle)) StartCircleCommand(cmd, log);
          RibbonItemHelp("Circle — center point and radius.\nCommand bar: CIRCLE or C");
          ImGui::SameLine(0, 4);
          if (gridBtn3("##RibbonRect", RibbonIconKind::Rect)) StartRectCommand(cmd, log);
          RibbonItemHelp("Rectangle — two opposite corners; stored as a closed polyline.\nCommand bar: RECT");
          ImGui::SameLine(0, 4);
          if (gridBtn3("##RibbonEllipse", RibbonIconKind::Ellipse)) StartEllipseCommand(cmd, log);
          RibbonItemHelp("Ellipse — center, axis endpoint, then ratio.\nCommand bar: ELLIPSE or EL");
          ImGui::SameLine(0, 4);
          nyiGrid("##RibbonPoint", "c3d_point", "Point");

          if (gridBtn3("##RibbonHatch", RibbonIconKind::Hatch)) StartHatchCommand(cmd, log);
          RibbonItemHelp("Hatch — pick an internal point to fill a closed area.\nCommand bar: HATCH or H");
          ImGui::SameLine(0, 4);
          if (gridBtn3("##RibbonPdfAttach", RibbonIconKind::PdfAttach)) StartPdfAttachCommand(cmd, log);
          RibbonItemHelp("PDF Attach — attach a PDF page as a raster underlay.\nCommand bar: PDFATTACH");
          ImGui::SameLine(0, 4);
          nyiGrid("##RibbonRevcloud", "c3d_revcloud", "Revision Cloud");
          ImGui::SameLine(0, 4);
          nyiGrid("##RibbonWipeout", "c3d_wipeout", "Wipeout");
          RibbonSectionEnd();
        }, "Draw", RibbonIconKind::Line});
      }

      // ---- Modify (icon-only 4x3 grid, per GUI-pass request) --------------
      {
        const float w = 8.f + gcHome * 4.f + 4.f * 3.f;
        ribbonSpecs.push_back({w, w, [&, w]() {
          RibbonSectionBegin("RibbonSecModify", "Modify", w, panelH);
          if (gridBtn3("##RibbonMove", RibbonIconKind::Move)) StartMoveCommand(cmd, log);
          RibbonItemHelp("Move — relocate selected entities by base point and offset.\nCommand bar: MOVE or M");
          ImGui::SameLine(0, 4);
          if (gridBtn3("##RibbonRotate", RibbonIconKind::Rotate)) StartRotateCommand(cmd, log);
          RibbonItemHelp("Rotate — turn selection around a base point by angle.\nCommand bar: ROTATE or RO");
          ImGui::SameLine(0, 4);
          if (gridBtn3("##RibbonTrim", RibbonIconKind::Trim)) StartTrimCommand(cmd, log);
          RibbonItemHelp("Trim — shorten segments to cutting edges.\nCommand bar: TRIM or TR");
          ImGui::SameLine(0, 4);
          if (gridBtn3("##RibbonErase", RibbonIconKind::Erase)) StartDeleteCommand(cmd, log);
          RibbonItemHelp("Erase — remove entities.\nCommand bar: DELETE or DEL");

          if (gridBtn3("##RibbonCopy", RibbonIconKind::Copy)) StartCopyCommand(cmd, log);
          RibbonItemHelp("Copy — duplicate selection with base point and offset.\nCommand bar: COPY or CP");
          ImGui::SameLine(0, 4);
          if (gridBtn3("##RibbonMirror", RibbonIconKind::Mirror)) StartMirrorCommand(cmd, log);
          RibbonItemHelp("Mirror — flip selection across a mirror line.\nCommand bar: MIRROR or MI");
          ImGui::SameLine(0, 4);
          if (gridBtn3("##RibbonFillet", RibbonIconKind::Fillet)) StartFilletCommand(cmd, log);
          RibbonItemHelp("Fillet — tangent arc between two curves.\nCommand bar: FILLET or F");
          ImGui::SameLine(0, 4);
          if (gridBtn3("##RibbonOffset", RibbonIconKind::Offset)) StartOffsetCommand(cmd, log);
          RibbonItemHelp("Offset — parallel copy at a distance.\nCommand bar: OFFSET or O");

          if (gridBtn3("##RibbonStretch", RibbonIconKind::Stretch)) StartStretchCommand(cmd, log);
          RibbonItemHelp("Stretch — crossing/window-select, then base point and destination.\nCommand bar: STRETCH or S");
          ImGui::SameLine(0, 4);
          if (gridBtn3("##RibbonScale", RibbonIconKind::Scale)) StartScaleCommand(cmd, log);
          RibbonItemHelp("Scale — uniform scale about a base point.\nCommand bar: SCALE or SC");
          ImGui::SameLine(0, 4);
          RibbonNyiButton("##RibbonArray", RibbonIconKind::Array, "Array", ImVec2(gcHome, gcHome), RibbonLabel::None);
          ImGui::SameLine(0, 4);
          if (gridBtn3("##RibbonExtend", RibbonIconKind::Extend)) StartExtendCommand(cmd, log);
          RibbonItemHelp("Extend — lengthen to a boundary edge.\nCommand bar: EXTEND or EX");
          RibbonSectionEnd();
        }, "Modify", RibbonIconKind::Move});
      }
    } else {
      // Layout contextual ribbon (REQ-032): paper-space viewport-authoring tools. Plot/Batch Plot
      // moved to the Output tab's "Plot" section (REQ-302 increment 3, D-2026-08-25-h) — this
      // section no longer duplicates them.
      ribbonSpecs.push_back({W.wLayout, M.wLayout, [&]() {
        RibbonSectionBegin("RibbonSecLayout", "Layout", curCompact ? M.wLayout : W.wLayout, panelH);
        {
          if (largeBtn("##RibbonRectVp", RibbonIconKind::ViewportRect, "Rect VP"))
            StartPaperRectViewportCommand(cmd, log);
          RibbonItemHelp("Rectangular viewport — two clicks define a viewport on the sheet.\nCommand bar: MVIEW / RECTVP");
          ImGui::SameLine(0, 4);
          ImGui::BeginGroup();
          const float cwL = colW({"Poly VP"});
          ImGui::BeginDisabled();
          smallBtn("##RibbonPolyVp", RibbonIconKind::ViewportPoly, "Poly VP", cwL);
          ImGui::EndDisabled();
          RibbonItemHelp("Polygonal viewport — coming in a later increment (REQ-034).",
                         ImGuiHoveredFlags_AllowWhenDisabled);
          ImGui::EndGroup();
        }
        RibbonSectionEnd();
      }});
    } // if (!ribbonPaperSpace) — Draw/Modify vs Layout

    // (No Home "Layers" panel — the persistent RibbonLayerStrip after the tools row already
    // carries the current-layer dropdown on every tab; a second one here was redundant.)

    // ---- Clipboard ------------------------------------------------------
    {
      const bool hasClip = !cmd.clipboard.empty();
      const bool hasSel = !cmd.selection.empty() || !cmd.selectedSurveyPointIndices.empty();
      const float w = 8.f + largeW + 4.f + gc * 2.f + 4.f;
      ribbonSpecs.push_back({w, w, [&, w, hasClip, hasSel]() {
        RibbonSectionBegin("RibbonSecClipboard", "Clipboard", w, panelH);
        if (!hasClip) ImGui::BeginDisabled();
        if (largeBtn("##RibbonPasteHome", RibbonIconKind::ClipboardPaste, "Paste"))
          StartPasteCommand(cmd, log);
        if (!hasClip) ImGui::EndDisabled();
        RibbonItemHelp("Paste (Ctrl+V) — place clipboard objects at cursor position.",
                       hasClip ? ImGuiHoveredFlags_None : ImGuiHoveredFlags_AllowWhenDisabled);
        ImGui::SameLine(0, 4);
        ImGui::BeginGroup();
        if (!hasSel) ImGui::BeginDisabled();
        if (RibbonButtonEx("##RibbonCopyClipHome", RibbonIconKind::ClipboardCopy, nullptr, ImVec2(gc, gc), RibbonLabel::None))
          CopySelectionToClipboard(cmd, log);
        if (!hasSel) ImGui::EndDisabled();
        RibbonItemHelp("Copy (Ctrl+C) — copy selected objects to clipboard.",
                       hasSel ? ImGuiHoveredFlags_None : ImGuiHoveredFlags_AllowWhenDisabled);
        ImGui::SameLine(0, 4);
        nyiGrid("##ClipCut", "c3d_cut", "Cut");
        nyiGrid("##ClipMatchProps", "c3d_matchprops", "Match Properties");   ImGui::SameLine(0, 4);
        nyiGrid("##ClipPasteSpecial", "c3d_pastespecial", "Paste Special");
        ImGui::EndGroup();
        RibbonSectionEnd();
      }, "Clipboard", RibbonIconKind::ClipboardPaste});
    }
  } // if (activeRibbonTab == kRibbonTabHome)

  // REQ-302: Annotate tab. Unchanged condition (model space only, same as before this task).
  if (cmd.activeRibbonTab == kRibbonTabAnnotate && !ribbonPaperSpace) {
    // REQ-302 / GUI-pass 2026-08-30: the Annotate tab mirrors Civil 3D's Annotate ribbon 1:1 —
    // Labels & Tables, Text, Dimensions, Centerlines, Leaders, Tables, Markup, Annotation Scaling.
    // Commands GoSurvey implements (TEXT, MTEXT, DIMLINEAR/ALIGNED/ANGULAR, DIMSTYLE) are wired into
    // their C3D-equivalent slot; every other button is greyed with an automatic
    // "… — not implemented yet." tooltip (RibbonNyiButton / annNyiCombo).

    // ---- Labels & Tables --------------------------------------------------
    {
      const float w = 8.f + belowW("Add\nLabels") + 4.f + belowW("Add\nTables");
      ribbonSpecs.push_back({w, w, [&, w]() {
        RibbonSectionBegin("RibbonSecAnnLabels", "Labels & Tables", w, panelH);
        nyiLarge("##AnnAddLabels", "Annotation_Add", "Add\nLabels");
        ImGui::SameLine(0, 4);
        nyiLarge("##AnnAddTables", "Table", "Add\nTables");
        RibbonSectionEnd();
      }, "Labels & Tables", RibbonIconKind::Nyi, "Annotation_Add"});
    }

    // ---- Text -----------------------------------------------------------
    {
      const float cw = colW({"Text", "Find text"});
      const float w = 8.f + belowW("Multiline\nText") + 4.f + cw + 4.f + annStyleW;
      ribbonSpecs.push_back({w, w, [&, cw, w]() {
        RibbonSectionBegin("RibbonSecAnnotate", "Text", w, panelH);
        if (RibbonButtonEx("##RibbonMtextLarge", RibbonIconKind::Mtext, "Multiline\nText",
                           ImVec2(belowW("Multiline\nText"), colH), RibbonLabel::Below))
          StartMtextCommand(cmd, log);
        RibbonItemHelp("Multiline Text — multiline in a frame; after box, edit in the on-drawing editor (Ctrl+Enter reformats; Save to place). Double-click MTEXT to edit.\nCommand bar: MTEXT or MT");
        ImGui::SameLine(0, 4);
        ImGui::BeginGroup();
        if (smallBtn("##RibbonText", RibbonIconKind::Text, "Text", cw))
          StartTextCommand(cmd, log);
        RibbonItemHelp("Text — single-line annotation at insertion.\nCommand bar: TEXT");
        insNyi("##RibbonFindText", "Find", "Find text", cw);
        ImGui::EndGroup();
        // Active text style for new TEXT/MTEXT (REQ-044): an AutoCAD-style flyout of thumbnail previews.
        ImGui::SameLine(0, 4);
        ImGui::BeginGroup();
        ImGui::TextUnformatted("Text style");
        {
          TextStyles::EnsureStandard(cmd.textStyles);
          const TextStyle* active = ActiveTextStyle(cmd);
          const std::string preview = active ? active->name : std::string("Standard");
          if (ImGui::Button((preview + "##RibbonTextStyle").c_str(), ImVec2(annStyleW, 0.f)))
            ImGui::OpenPopup("##textstyleflyout");
          RibbonItemHelp("Active text style for new TEXT/MTEXT (REQ-044). Click for thumbnail previews.");
          if (ImGui::BeginPopup("##textstyleflyout")) {
            const float cardW = 132.f, thumbH = 50.f, cardGap = 8.f;
            const int perRow = 3;
            const float labelH = ImGui::GetTextLineHeight() + 4.f;
            ImDrawList* dl = ImGui::GetWindowDrawList();
            for (size_t i = 0; i < cmd.textStyles.size(); ++i) {
              const TextStyle& s = cmd.textStyles[i];
              ImGui::PushID(static_cast<int>(i));
              if (i % static_cast<size_t>(perRow) != 0)
                ImGui::SameLine(0, cardGap);
              const ImVec2 p0 = ImGui::GetCursorScreenPos();
              const bool sel = (s.name == cmd.activeTextStyleName);
              if (ImGui::InvisibleButton("##card", ImVec2(cardW, thumbH + labelH))) {
                SetActiveTextStyle(cmd, s.name);
                ImGui::CloseCurrentPopup();
              }
              const bool hovered = ImGui::IsItemHovered();
              const ImVec2 thBR(p0.x + cardW, p0.y + thumbH);
              dl->AddRectFilled(p0, thBR, IM_COL32(245, 245, 245, 255), 3.f);
              dl->AddRect(p0, thBR,
                          sel ? IM_COL32(90, 160, 230, 255)
                              : (hovered ? IM_COL32(150, 150, 150, 255) : IM_COL32(90, 90, 90, 255)),
                          3.f, 0, sel ? 2.f : 1.f);
              dl->PushClipRect(p0, thBR, true);
              DrawTextStyleSample(dl, p0, ImVec2(cardW, thumbH), s, "AaBb123", IM_COL32(20, 20, 20, 255));
              dl->PopClipRect();
              dl->AddText(ImVec2(p0.x + 2.f, p0.y + thumbH + 2.f), IM_COL32(232, 232, 232, 255), s.name.c_str());
              ImGui::PopID();
            }
            ImGui::Separator();
            if (ImGui::Selectable("Manage Text Styles…")) {
              TextStyles::EnsureStandard(cmd.textStyles);
              cmd.showTextStyleManagerWindow = true;
              ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
          }
        }
        ImGui::EndGroup();
        RibbonSectionEnd();
      }});
    }

    // ---- Dimensions ---------------------------------------------------------
    // REQ-302 follow-up (2026-08-25): a Dimensions group belongs under Annotate. GUI-pass
    // 2026-08-30: expanded to the C3D shape — a large Dimension button plus Linear/Aligned/
    // Angular and greyed Quick/Continue, with a style combo.
    {
      const float cA = colW({"Aligned", "Angular"});
      const float cB = colW({"Continue"});
      const float w = 8.f + belowW("Dimension") + 4.f + cA + 4.f + cB + 4.f + annStyleW;
      ribbonSpecs.push_back({w, w, [&, cA, cB, w]() {
        RibbonSectionBegin("RibbonSecAnnDim", "Dimensions", w, panelH);
        if (RibbonButtonEx("##RibbonDimLarge", RibbonIconKind::DimLinear, "Dimension",
                           ImVec2(belowW("Dimension"), colH), RibbonLabel::Below))
          StartDimLinearCommand(cmd, log);
        RibbonItemHelp(
            "Dimension — horizontal or vertical distance in X or Y; third pick sets line position.\nCommand bar: DIMLINEAR or DLI");
        ImGui::SameLine(0, 4);
        ImGui::BeginGroup();
        if (smallBtn("##RibbonDimLin", RibbonIconKind::DimLinear, "Linear", cA))
          StartDimLinearCommand(cmd, log);
        RibbonItemHelp("Linear dimension — horizontal or vertical distance in X or Y.\nCommand bar: DIMLINEAR or DLI");
        if (smallBtn("##RibbonDim", RibbonIconKind::Dim, "Aligned", cA))
          StartDimAlignedCommand(cmd, log);
        RibbonItemHelp("Aligned dimension — extension lines parallel to the picked points.\nCommand bar: DIMALIGNED or DAL");
        if (smallBtn("##RibbonDimAng", RibbonIconKind::DimAngular, "Angular", cA))
          StartDimAngularCommand(cmd, log);
        RibbonItemHelp("Angular dimension — vertex, two ray points, then arc position.\nCommand bar: DIMANGULAR or DAN");
        ImGui::EndGroup();
        ImGui::SameLine(0, 4);
        ImGui::BeginGroup();
        insNyi("##RibbonDimQuick", "Quick_Dimension", "Quick", cB);
        insNyi("##RibbonDimCont", "Dim_Continue", "Continue", cB);
        ImGui::EndGroup();
        ImGui::SameLine(0, 4);
        ImGui::BeginGroup();
        ImGui::TextUnformatted("Dimension style");
        if (ImGui::Button("Standard##RibbonDimStyle", ImVec2(annStyleW, 0.f)))
          StartDimStyleCommand(cmd, log);
        RibbonItemHelp("Dimension style — opens the dimension style manager.\nCommand bar: DIMSTYLE or D");
        annNyiCombo("##RibbonDimLayer", "Use current");
        ImGui::EndGroup();
        RibbonSectionEnd();
      }, "Dimensions", RibbonIconKind::DimLinear});
    }

    // ---- Centerlines ------------------------------------------------------
    {
      const float cw = colW({"Center Mark", "Centerline"});
      const float w = 8.f + cw;
      ribbonSpecs.push_back({w, w, [&, cw, w]() {
        RibbonSectionBegin("RibbonSecAnnCenterlines", "Centerlines", w, panelH);
        ImGui::BeginGroup();
        insNyi("##AnnCenterMark", "Center_Mark", "Center Mark", cw);
        insNyi("##AnnCenterline", "Centerline", "Centerline", cw);
        ImGui::EndGroup();
        RibbonSectionEnd();
      }, "Centerlines", RibbonIconKind::Nyi, "Center_Mark"});
    }

    // ---- Leaders --------------------------------------------------------
    {
      const float cw = colW({"Remove Leader"});
      const float w = 8.f + belowW("Multi\nleader") + 4.f + cw + 4.f + annStyleW;
      ribbonSpecs.push_back({w, w, [&, cw, w]() {
        RibbonSectionBegin("RibbonSecAnnLeaders", "Leaders", w, panelH);
        nyiLarge("##AnnMultileader", "Multileader", "Multi\nleader");
        ImGui::SameLine(0, 4);
        ImGui::BeginGroup();
        insNyi("##AnnAddLeader", "Add_Leader", "Add Leader", cw);
        insNyi("##AnnRemoveLeader", "Remove_Leader", "Remove Leader", cw);
        ImGui::EndGroup();
        ImGui::SameLine(0, 4);
        ImGui::BeginGroup();
        ImGui::TextUnformatted("Multileader style");
        annNyiCombo("##AnnMleaderStyle", "Standard");
        ImGui::EndGroup();
        RibbonSectionEnd();
      }, "Leaders", RibbonIconKind::Nyi, "Multileader"});
    }

    // ---- Tables --------------------------------------------------------
    {
      const float cw = colW({"Extract Data", "Link Data"});
      const float w = 8.f + belowW("Table") + 4.f + cw + 4.f + annStyleW;
      ribbonSpecs.push_back({w, w, [&, cw, w]() {
        RibbonSectionBegin("RibbonSecAnnTables", "Tables", w, panelH);
        nyiLarge("##AnnTable", "Table", "Table");
        ImGui::SameLine(0, 4);
        ImGui::BeginGroup();
        insNyi("##AnnExtractData", "Extract_Data", "Extract Data", cw);
        insNyi("##AnnLinkData", "Data_Link", "Link Data", cw);
        ImGui::EndGroup();
        ImGui::SameLine(0, 4);
        ImGui::BeginGroup();
        ImGui::TextUnformatted("Table style");
        annNyiCombo("##AnnTableStyle", "Standard");
        ImGui::EndGroup();
        RibbonSectionEnd();
      }, "Tables", RibbonIconKind::Nyi, "Table"});
    }

    // ---- Markup --------------------------------------------------------
    {
      const float cw = colW({"Revision Cloud"});
      const float w = 8.f + cw;
      ribbonSpecs.push_back({w, w, [&, cw, w]() {
        RibbonSectionBegin("RibbonSecAnnMarkup", "Markup", w, panelH);
        ImGui::BeginGroup();
        insNyi("##AnnWipeout", "Wipeout", "Wipeout", cw);
        insNyi("##AnnRevcloud", "c3d_revcloud", "Revision Cloud", cw);
        ImGui::EndGroup();
        RibbonSectionEnd();
      }, "Markup", RibbonIconKind::Nyi, "Wipeout"});
    }

    // ---- Annotation Scaling --------------------------------------------
    {
      const float cw = colW({"Add/Delete Scales", "Sync Positions"});
      const float w = 8.f + belowW("Add Current\nScale") + 4.f + cw;
      ribbonSpecs.push_back({w, w, [&, cw, w]() {
        RibbonSectionBegin("RibbonSecAnnScaling", "Annotation Scaling", w, panelH);
        nyiLarge("##AnnAddScale", "Add_Current_Scale", "Add Current\nScale");
        ImGui::SameLine(0, 4);
        ImGui::BeginGroup();
        insNyi("##AnnAddDeleteScales", "Add_Delete_Scales", "Add/Delete Scales", cw);
        insNyi("##AnnScaleList", "Scale_List", "Scale List", cw);
        insNyi("##AnnSyncScale", "Sync_Scale_Positions", "Sync Positions", cw);
        ImGui::EndGroup();
        RibbonSectionEnd();
      }, "Annotation Scaling", RibbonIconKind::Nyi, "Add_Current_Scale"});
    }
  } // if (activeRibbonTab == kRibbonTabAnnotate)

  // REQ-302 / GUI-pass 2026-08-30: Manage tab mirrors Civil 3D's Manage ribbon 1:1 — Data
  // Shortcuts, Action Recorder, Customization, Applications, CAD Standards, Styles, Property Set
  // Data, Performance, Visual Programming. GoSurvey implements none of these yet, so every button
  // is greyed with the automatic "… — not implemented yet." tooltip (RibbonNyiButton), same as the
  // Home tab's Civil 3D placeholders. `c3d_*` icons with no library match are generated by
  // tools/gen_c3d_icons.cpp in the set's blue/gray line-art style.
  if (cmd.activeRibbonTab == kRibbonTabManage) {
    // ---- Data Shortcuts -------------------------------------------------
    {
      const float cw = colW({"Synchronize References", "Validate Data Shortcuts"});
      const float w = 8.f + belowW("Create Data\nShortcuts") + 4.f + cw + 4.f + cw;
      ribbonSpecs.push_back({w, w, [&, cw, w]() {
        RibbonSectionBegin("RibbonSecMgManageDS", "Data Shortcuts", w, panelH);
        nyiLarge("##MgCreateDS", "c3d_datashortcut", "Create Data\nShortcuts");
        ImGui::SameLine(0, 4);
        ImGui::BeginGroup();
        insNyi("##MgNewDSFolder", "c3d_newfolder", "New Shortcuts Folder", cw);
        insNyi("##MgSetDSFolder", "c3d_setfolder", "Set Shortcuts Folder", cw);
        insNyi("##MgSetWorkFolder", "c3d_workfolder", "Set Working Folder", cw);
        ImGui::EndGroup();
        ImGui::SameLine(0, 4);
        ImGui::BeginGroup();
        insNyi("##MgManageDS", "c3d_managedata", "Manage Data Shortcuts", cw);
        insNyi("##MgValidateDS", "c3d_validatedata", "Validate Data Shortcuts", cw);
        insNyi("##MgSyncRefs", "c3d_syncref", "Synchronize References", cw);
        ImGui::EndGroup();
        RibbonSectionEnd();
      }, "Data Shortcuts", RibbonIconKind::Nyi, "c3d_datashortcut"});
    }

    // ---- Action Recorder ----------------------------------------------
    {
      const float cw = colW({"Insert Message", "Preferences"});
      const float w = 8.f + belowW("Record") + 4.f + cw;
      ribbonSpecs.push_back({w, w, [&, cw, w]() {
        RibbonSectionBegin("RibbonSecMgActionRec", "Action Recorder", w, panelH);
        nyiLarge("##MgRecord", "c3d_record", "Record");
        ImGui::SameLine(0, 4);
        ImGui::BeginGroup();
        insNyi("##MgPlay", "c3d_play", "Play", cw);
        insNyi("##MgInsertMsg", "Annotation", "Insert Message", cw);
        insNyi("##MgInsertVal", "Field", "Insert Value", cw);
        ImGui::EndGroup();
        RibbonSectionEnd();
      }, "Action Recorder", RibbonIconKind::Nyi, "c3d_record"});
    }

    // ---- Customization ----------------------------------------------
    {
      const float cw = colW({"Edit Aliases"});
      const float w = 8.f + belowW("User\nInterface") + 4.f + belowW("Tool\nPalettes") + 4.f + cw;
      ribbonSpecs.push_back({w, w, [&, cw, w]() {
        RibbonSectionBegin("RibbonSecMgCustomize", "Customization", w, panelH);
        nyiLarge("##MgCui", "c3d_cui", "User\nInterface");
        ImGui::SameLine(0, 4);
        nyiLarge("##MgToolPalettes", "c3d_toolpalette", "Tool\nPalettes");
        ImGui::SameLine(0, 4);
        ImGui::BeginGroup();
        insNyi("##MgCuiImport", "Import", "Import", cw);
        insNyi("##MgCuiExport", "Export", "Export", cw);
        insNyi("##MgEditAliases", "c3d_editalias", "Edit Aliases", cw);
        ImGui::EndGroup();
        RibbonSectionEnd();
      }, "Customization", RibbonIconKind::Nyi, "c3d_cui"});
    }

    // ---- Applications ----------------------------------------------
    {
      const float cw = colW({"Visual Basic Editor", "Visual LISP Editor"});
      const float w = 8.f + belowW("Load\nApplication") + 4.f + belowW("Run\nScript") + 4.f + cw;
      ribbonSpecs.push_back({w, w, [&, cw, w]() {
        RibbonSectionBegin("RibbonSecMgApps", "Applications", w, panelH);
        nyiLarge("##MgLoadApp", "c3d_loadapp", "Load\nApplication");
        ImGui::SameLine(0, 4);
        nyiLarge("##MgRunScript", "c3d_runscript", "Run\nScript");
        ImGui::SameLine(0, 4);
        ImGui::BeginGroup();
        insNyi("##MgVbEditor", "c3d_vbeditor", "Visual Basic Editor", cw);
        insNyi("##MgLispEditor", "c3d_lispeditor", "Visual LISP Editor", cw);
        insNyi("##MgVbaMacro", "c3d_vbamacro", "Run VBA Macro", cw);
        ImGui::EndGroup();
        RibbonSectionEnd();
      }, "Applications", RibbonIconKind::Nyi, "c3d_runscript"});
    }

    // ---- CAD Standards ----------------------------------------------
    {
      const float cw = colW({"Layer Translator", "Configure"});
      const float w = 8.f + cw;
      ribbonSpecs.push_back({w, w, [&, cw, w]() {
        RibbonSectionBegin("RibbonSecMgStandards", "CAD Standards", w, panelH);
        ImGui::BeginGroup();
        insNyi("##MgLayerTrans", "c3d_layertranslator", "Layer Translator", cw);
        insNyi("##MgStdCheck", "Check", "Check", cw);
        insNyi("##MgStdConfigure", "c3d_cadstd_config", "Configure", cw);
        ImGui::EndGroup();
        RibbonSectionEnd();
      }, "CAD Standards", RibbonIconKind::Nyi, "Check"});
    }

    // ---- Styles ----------------------------------------------
    {
      const float cw = colW({"Reference", "Purge"});
      const float w = 8.f + cw;
      ribbonSpecs.push_back({w, w, [&, cw, w]() {
        RibbonSectionBegin("RibbonSecMgStyles", "Styles", w, panelH);
        ImGui::BeginGroup();
        insNyi("##MgStylesImport", "Import", "Import", cw);
        insNyi("##MgStylesPurge", "Purge", "Purge", cw);
        insNyi("##MgStylesReference", "Attach", "Reference", cw);
        ImGui::EndGroup();
        RibbonSectionEnd();
      }, "Styles", RibbonIconKind::Nyi, "Purge"});
    }

    // ---- Property Set Data ----------------------------------------------
    {
      const float w = 8.f + belowW("Define\nProperty Sets");
      ribbonSpecs.push_back({w, w, [&, w]() {
        RibbonSectionBegin("RibbonSecMgPropSets", "Property Set Data", w, panelH);
        nyiLarge("##MgDefinePropSets", "c3d_propsets", "Define\nProperty Sets");
        RibbonSectionEnd();
      }, "Property Set Data", RibbonIconKind::Nyi, "c3d_propsets"});
    }

    // ---- Performance ----------------------------------------------
    {
      const float w = 8.f + belowW("Performance\nAnalyzer");
      ribbonSpecs.push_back({w, w, [&, w]() {
        RibbonSectionBegin("RibbonSecMgPerf", "Performance", w, panelH);
        nyiLarge("##MgPerfAnalyzer", "c3d_perfanalyzer", "Performance\nAnalyzer");
        RibbonSectionEnd();
      }, "Performance", RibbonIconKind::Nyi, "c3d_perfanalyzer"});
    }

    // ---- Visual Programming ----------------------------------------------
    {
      const float w = 8.f + belowW("Dynamo") + 4.f + belowW("Dynamo\nPlayer");
      ribbonSpecs.push_back({w, w, [&, w]() {
        RibbonSectionBegin("RibbonSecMgVisProg", "Visual Programming", w, panelH);
        nyiLarge("##MgDynamo", "c3d_dynamo", "Dynamo");
        ImGui::SameLine(0, 4);
        nyiLarge("##MgDynamoPlayer", "c3d_dynamoplayer", "Dynamo\nPlayer");
        RibbonSectionEnd();
      }, "Visual Programming", RibbonIconKind::Nyi, "c3d_dynamo"});
    }
  } // if (activeRibbonTab == kRibbonTabManage)

  // D-2026-08-28-k / REQ-141: Civil 3D Survey tab (screenshot 2). No Object Viewer.
  if (cmd.activeRibbonTab == kRibbonTabSurvey && !ribbonPaperSpace) {
    const float wLabelsSec =
        8.f + capW("Add\nLabels") + 4.f + capW("Add\nTables") + 4.f + capW("Renumber\nTags") + 8.f;
    ribbonSpecs.push_back({wLabelsSec, wLabelsSec, [&]() {
      const float wAddLabels = capW("Add\nLabels");
      const float wAddTables = capW("Add\nTables");
      const float wRenumber = capW("Renumber\nTags");
      const float wSec = 8.f + wAddLabels + 4.f + wAddTables + 4.f + wRenumber + 8.f;
      RibbonSectionBegin("RibbonSecSvyLabels", "Labels & Tables", wSec, panelH);
      RibbonNyiButton("##SvyAddLabels", RibbonIconKind::SurfLabel, "Add\nLabels", ImVec2(wAddLabels, colH),
                      RibbonLabel::Below);
      ImGui::SameLine(0, 4);
      if (RibbonButtonEx("##SvyAddTables", RibbonIconKind::SurfLegend, "Add\nTables", ImVec2(wAddTables, colH),
                         RibbonLabel::Below))
        ImGui::OpenPopup("##SvyAddTablesMenu");
      RibbonItemHelp("Add Tables — insert a volume TABLE or MTEXT report.\nCommand bar: VOLREPORT TABLE / VOLREPORT");
      if (ImGui::BeginPopup("##SvyAddTablesMenu")) {
        if (ImGui::MenuItem("Volume Table"))
          SubmitRibbonCommand(cmd, log, "VOLREPORT TABLE");
        if (ImGui::MenuItem("Volume Report (MTEXT)"))
          SubmitRibbonCommand(cmd, log, "VOLREPORT");
        ImGui::EndPopup();
      }
      ImGui::SameLine(0, 4);
      RibbonNyiButton("##SvyRenumber", RibbonIconKind::SvyRenumber, "Renumber\nTags", ImVec2(wRenumber, colH),
                      RibbonLabel::Below);
      RibbonSectionEnd();
    }});

    {
      const float wGen = 8.f + colW({"Properties", "Isolate Objects"}) + 8.f;
      ribbonSpecs.push_back({wGen, wGen, [&]() {
        const float cell = colW({"Properties", "Isolate Objects"});
        RibbonSectionBegin("RibbonSecSvyGen", "General Tools", 8.f + cell + 8.f, panelH);
        ImGui::BeginGroup();
        if (smallBtn("##SvyProps", RibbonIconKind::SurfPropsHand, "Properties", cell))
          cmd.pendingPropertiesFocus = true;
        RibbonItemHelp("Properties — the side Properties panel for the current selection.");
        if (smallBtn("##SvyIsolate", RibbonIconKind::SurfIsolate, "Isolate Objects", cell))
          ImGui::OpenPopup("##SvyIsolateMenu");
        RibbonItemHelp("Isolate Objects — isolate, hide, or end isolation (REQ-084).");
        if (ImGui::BeginPopup("##SvyIsolateMenu")) {
          if (ImGui::MenuItem("Isolate Objects"))
            IsolateSelectedObjects(cmd, log);
          if (ImGui::MenuItem("Hide Objects"))
            HideSelectedObjects(cmd, log);
          if (ImGui::MenuItem("End Object Isolation", nullptr, false, !cmd.hiddenEntityIds.empty()))
            EndObjectIsolation(cmd, log);
          ImGui::EndPopup();
        }
        ImGui::EndGroup();
        RibbonSectionEnd();
      }});
    }

    {
      const float wSurvey = 8.f + capW("Survey\nToolspace") + 4.f + capW("Network\nProperties") + 4.f +
                            capW("Figure\nProperties") + 8.f;
      ribbonSpecs.push_back({wSurvey, wSurvey, [&]() {
        const float wTs = capW("Survey\nToolspace");
        const float wNet = capW("Network\nProperties");
        const float wFig = capW("Figure\nProperties");
        RibbonSectionBegin("RibbonSecSvyTs", "Survey", 8.f + wTs + 4.f + wNet + 4.f + wFig + 8.f, panelH);
        if (RibbonButtonEx("##SvyToolspace", RibbonIconKind::SvyTripod, "Survey\nToolspace", ImVec2(wTs, colH),
                           RibbonLabel::Below))
          cmd.showToolspaceWindow = true;
        RibbonItemHelp("Survey Toolspace — drawing explorer (Prospector and Settings).\nCommand bar: TOOLSPACE");
        ImGui::SameLine(0, 4);
        RibbonNyiButton("##SvyNetProps", RibbonIconKind::SvyPda, "Network\nProperties", ImVec2(wNet, colH),
                        RibbonLabel::Below);
        ImGui::SameLine(0, 4);
        RibbonNyiButton("##SvyFigProps", RibbonIconKind::SvyFigure, "Figure\nProperties", ImVec2(wFig, colH),
                        RibbonLabel::Below);
        RibbonSectionEnd();
      }});
    }

    {
      const float wMod = 8.f + capW("Survey\nQuery") + 4.f +
                         colW({"Survey Figure Properties", "Survey Point Properties", "Browse to Survey Data"}) + 4.f +
                         colW({"Edit Geometry", "Edit Elevations", "Update Figure"}) + 8.f;
      ribbonSpecs.push_back({wMod, wMod, [&]() {
        const float wQuery = capW("Survey\nQuery");
        const float cFig = colW({"Survey Figure Properties", "Survey Point Properties", "Browse to Survey Data"});
        const float cEdit = colW({"Edit Geometry", "Edit Elevations", "Update Figure"});
        RibbonSectionBegin("RibbonSecSvyMod", "Modify", 8.f + wQuery + 4.f + cFig + 4.f + cEdit + 8.f, panelH);
        RibbonNyiButton("##SvyQuery", RibbonIconKind::SvyQuery, "Survey\nQuery", ImVec2(wQuery, colH),
                        RibbonLabel::Below);
        ImGui::SameLine(0, 4);
        ImGui::BeginGroup();
        RibbonNyiButton("##SvyFigProp2", RibbonIconKind::SvyFigure, "Survey Figure Properties", ImVec2(cFig, rowH),
                        RibbonLabel::Right);
        if (smallBtn("##SvyPtProps", RibbonIconKind::SurveyPoint, "Survey Point Properties", cFig))
          cmd.pendingPropertiesFocus = true;
        RibbonItemHelp("Survey Point Properties — the Properties panel for selected survey points.");
        RibbonNyiButton("##SvyBrowse", RibbonIconKind::SvyPin, "Browse to Survey Data", ImVec2(cFig, rowH),
                        RibbonLabel::Right);
        ImGui::EndGroup();
        ImGui::SameLine(0, 4);
        ImGui::BeginGroup();
        RibbonNyiButton("##SvyEditGeom", RibbonIconKind::Rect, "Edit Geometry", ImVec2(cEdit, rowH),
                        RibbonLabel::Right);
        if (smallBtn("##SvyEditElev", RibbonIconKind::SurfEdit, "Edit Elevations", cEdit))
          cmd.showFeatureLineElevWindow = true;
        RibbonItemHelp(
            "Edit Elevations — station, elevation, and grade for feature-line points.\n"
            "Feature Line Elevations window.");
        RibbonNyiButton("##SvyUpdateFig", RibbonIconKind::SvyRefresh, "Update Figure", ImVec2(cEdit, rowH),
                        RibbonLabel::Right);
        ImGui::EndGroup();
        RibbonSectionEnd();
      }});
    }

    {
      const float anCol = colW({"Mapcheck", "Geodetic Calculator", "Astronomic Direction"});
      const float wAn = 8.f + anCol + 8.f;
      ribbonSpecs.push_back({wAn, wAn, [&]() {
        const float cell = colW({"Mapcheck", "Geodetic Calculator", "Astronomic Direction"});
        RibbonSectionBegin("RibbonSecSvyAnalyze", "Analyze", 8.f + cell + 8.f, panelH);
        ImGui::BeginGroup();
        RibbonNyiButton("##SvyMapcheck", RibbonIconKind::SvyGlobe, "Mapcheck", ImVec2(cell, rowH), RibbonLabel::Right);
        RibbonNyiButton("##SvyGeodetic", RibbonIconKind::SvyGeodetic, "Geodetic Calculator", ImVec2(cell, rowH),
                        RibbonLabel::Right);
        RibbonNyiButton("##SvyAstro", RibbonIconKind::SvySun, "Astronomic Direction", ImVec2(cell, rowH),
                        RibbonLabel::Right);
        ImGui::EndGroup();
        RibbonSectionEnd();
      }});
    }

    {
      const float launchCol = colW({"Quick Profile", "Create Surface", "Grading Creation Tools"});
      const float wLaunch = 8.f + launchCol + 8.f;
      ribbonSpecs.push_back({wLaunch, wLaunch, [&]() {
        const float cell = colW({"Quick Profile", "Create Surface", "Grading Creation Tools"});
        RibbonSectionBegin("RibbonSecSvyLaunch", "Launch Pad", 8.f + cell + 8.f, panelH);
        ImGui::BeginGroup();
        if (smallBtn("##SvyQProfile", RibbonIconKind::SurfQuickProfile, "Quick Profile", cell)) {
          if (!cmd.cadSurfaces.empty())
            StartQuickProfileCommand(cmd, cmd.cadSurfaces[0].name, log);
          else
            SubmitRibbonCommand(cmd, log, "QUICKPROFILE");
        }
        RibbonItemHelp("Quick Profile — sample a surface along two plan points.\nCommand bar: QUICKPROFILE");
        if (smallBtn("##SvyCreateSurf", RibbonIconKind::SurfAddData, "Create Surface", cell))
          cmd.showCreateSurfaceWindow = true;
        RibbonItemHelp("Create Surface — TIN, grid, corridor, or volume type.\nToolspace: Create Surface...");
        RibbonNyiButton("##SvyGrading", RibbonIconKind::SurfGrading, "Grading Creation Tools", ImVec2(cell, rowH),
                        RibbonLabel::Right);
        ImGui::EndGroup();
        RibbonSectionEnd();
      }});
    }
  } // if (activeRibbonTab == kRibbonTabSurvey)

  // REQ-143: Civil 3D-shaped contextual TIN Surface tab (selected surface).
  if (cmd.activeRibbonTab == kRibbonTabSurfaceCtx && !ribbonPaperSpace && selSurfIdx >= 0) {
    const std::string& surfName = cmd.cadSurfaces[static_cast<size_t>(selSurfIdx)].name;

    const float tsLabelsSec = 8.f + belowW("Add Labels") + 4.f + belowW("Add Legend") + 8.f;
    ribbonSpecs.push_back({tsLabelsSec, tsLabelsSec, [&]() {
      const float tsAddLabels = belowW("Add Labels");
      const float tsAddLegend = belowW("Add Legend");
      RibbonSectionBegin("RibbonSecTsLabels", "Labels & Tables", 8.f + tsAddLabels + 4.f + tsAddLegend + 8.f, panelH);
      RibbonNyiButton("##TsAddLabels", RibbonIconKind::SurfLabel, "Add Labels", ImVec2(tsAddLabels, colH),
                      RibbonLabel::Below);
      ImGui::SameLine(0, 4);
      RibbonNyiButton("##TsAddLegend", RibbonIconKind::SurfLegend, "Add Legend", ImVec2(tsAddLegend, colH),
                      RibbonLabel::Below);
      RibbonSectionEnd();
    }});

    {
      const float genCell = colW({"Properties", "Inquiry", "Isolate Objects"});
      const float wGen = 8.f + genCell + 4.f + genCell;
      ribbonSpecs.push_back({wGen, wGen, [&]() {
      const float cell = colW({"Properties", "Inquiry", "Isolate Objects"});
      RibbonSectionBegin("RibbonSecTsGen", "General Tools", 8.f + cell + 4.f + cell, panelH);
      ImGui::BeginGroup();
      if (smallBtn("##TsProps", RibbonIconKind::SurfPropsHand, "Properties", cell))
        cmd.pendingPropertiesFocus = true;
      RibbonItemHelp("Properties — the side Properties panel for the selected surface.");
      if (smallBtn("##TsInquiry", RibbonIconKind::SurfInquiry, "Inquiry", cell))
        StartSurfaceElevGradeCommand(cmd, log);
      RibbonItemHelp("Inquiry — elevation and grade on this surface.\nCommand bar: SURFELEV");
      ImGui::EndGroup();
      ImGui::SameLine(0, 4);
      ImGui::BeginGroup();
      if (smallBtn("##TsIsolate", RibbonIconKind::SurfIsolate, "Isolate Objects", cell))
        ImGui::OpenPopup("##TsIsolateMenu");
      RibbonItemHelp("Isolate Objects — isolate, hide, or end isolation (REQ-084).");
      if (ImGui::BeginPopup("##TsIsolateMenu")) {
        if (ImGui::MenuItem("Isolate Objects"))
          IsolateSelectedObjects(cmd, log);
        if (ImGui::MenuItem("Hide Objects"))
          HideSelectedObjects(cmd, log);
        if (ImGui::MenuItem("End Object Isolation", nullptr, false, !cmd.hiddenEntityIds.empty()))
          EndObjectIsolation(cmd, log);
        ImGui::EndPopup();
      }
      ImGui::EndGroup();
      RibbonSectionEnd();
    }});
    }

    {
      const float wMod = 8.f + belowW("Surface Properties") + 4.f + belowW("Add Data") + 4.f + belowW("Edit Surface") + 8.f;
      ribbonSpecs.push_back({wMod, wMod, [&]() {
      const float tsSurfProps = belowW("Surface Properties");
      const float tsAddData = belowW("Add Data");
      const float tsEditSurf = belowW("Edit Surface");
      RibbonSectionBegin("RibbonSecTsMod", "Modify", 8.f + tsSurfProps + 4.f + tsAddData + 4.f + tsEditSurf + 8.f, panelH);
      if (RibbonButtonEx("##TsSurfProps", RibbonIconKind::SurfDoc, "Surface Properties", ImVec2(tsSurfProps, colH),
                         RibbonLabel::Below)) {
        cmd.surfacePropertiesIndex = selSurfIdx;
        cmd.showSurfacePropertiesWindow = true;
      }
      RibbonItemHelp("Surface Properties — Information, Definition, Analysis, Statistics.");
      ImGui::SameLine(0, 4);
      if (RibbonButtonEx("##TsAddData", RibbonIconKind::SurfAddData, "Add Data", ImVec2(tsAddData, colH),
                         RibbonLabel::Below))
        ImGui::OpenPopup("##TsAddDataMenu");
      RibbonItemHelp("Add Data — breaklines, contours, and boundaries on this surface.");
      if (ImGui::BeginPopup("##TsAddDataMenu")) {
        if (ImGui::MenuItem("Breaklines"))
          StartDesignateBreaklineCommand(cmd, surfName, log);
        if (ImGui::MenuItem("Contours"))
          StartDesignateContourCommand(cmd, surfName, log);
        if (ImGui::MenuItem("Boundary"))
          StartDesignateBoundaryCommand(cmd, surfName, CadBoundaryKind::Outer, log);
        if (ImGui::MenuItem("Point Groups"))
          cmd.showSurfaceManagerWindow = true;
        ImGui::BeginDisabled();
        ImGui::MenuItem("Point Files");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
          ImGui::SetTooltip("Point Files — not implemented yet.");
        ImGui::EndDisabled();
        ImGui::EndPopup();
      }
      ImGui::SameLine(0, 4);
      if (RibbonButtonEx("##TsEditSurf", RibbonIconKind::SurfEdit, "Edit Surface", ImVec2(tsEditSurf, colH),
                         RibbonLabel::Below))
        ImGui::OpenPopup("##TsEditSurfMenu");
      RibbonItemHelp("Edit Surface — add/delete/move points, delete TIN lines, swap edges, or rebuild.");
      if (ImGui::BeginPopup("##TsEditSurfMenu")) {
        if (ImGui::MenuItem("Add Point"))
          StartSurfAddPointCommand(cmd, surfName, log);
        if (ImGui::MenuItem("Delete Point"))
          StartSurfDelPointCommand(cmd, surfName, log);
        if (ImGui::MenuItem("Move Point"))
          StartSurfMovePointCommand(cmd, surfName, log);
        if (ImGui::MenuItem("Delete TIN Line"))
          StartSurfDelLineCommand(cmd, surfName, log);
        if (ImGui::MenuItem("Swap Edge"))
          StartSurfSwapEdgeCommand(cmd, surfName, log);
        if (ImGui::MenuItem("Rebuild"))
          SubmitRibbonCommand(cmd, log, "SURFACEREBUILD " + surfName);
        ImGui::EndPopup();
      }
      RibbonSectionEnd();
    }});
    }

    {
      const float lodW = colW({"Reduced LOD", "High LOD"});
      ribbonSpecs.push_back({8.f + lodW, 8.f + lodW, [&]() {
      const float cw = colW({"Reduced LOD", "High LOD"});
      RibbonSectionBegin("RibbonSecTsLod", "Level of Detail", 8.f + cw, panelH);
      ImGui::BeginGroup();
      RibbonNyiButton("##TsLodLow", RibbonIconKind::SurfLodLow, "Reduced LOD",
                      ImVec2(curCompact ? rowH : cw, rowH),
                      curCompact ? RibbonLabel::None : RibbonLabel::Right);
      RibbonNyiButton("##TsLodHigh", RibbonIconKind::SurfLodHigh, "High LOD",
                      ImVec2(curCompact ? rowH : cw, rowH),
                      curCompact ? RibbonLabel::None : RibbonLabel::Right);
      ImGui::EndGroup();
      RibbonSectionEnd();
    }});
    }

    {
      const float wAn = 8.f + belowW("Water Drop") + 4.f + colW({"Crossing BL", "Visibility", "Catchment", "Volumes"}) +
                        4.f + colW({"Crossing BL", "Visibility", "Catchment", "Volumes"}) + 8.f;
      ribbonSpecs.push_back({wAn, wAn, [&]() {
      const float tsWater = belowW("Water Drop");
      const float cell = colW({"Crossing BL", "Visibility", "Catchment", "Volumes"});
      RibbonSectionBegin("RibbonSecTsAnalyze", "Analyze", 8.f + tsWater + 4.f + cell + 4.f + cell + 8.f, panelH);
      if (RibbonButtonEx("##TsWaterDrop", RibbonIconKind::SurfWaterDrop, "Water Drop", ImVec2(tsWater, colH),
                         RibbonLabel::Below))
        StartWaterDropCommand(cmd, surfName, log);
      RibbonItemHelp("Water Drop — pick a point on this surface.\nCommand bar: WATERDROP");
      ImGui::SameLine(0, 4);
      ImGui::BeginGroup();
      RibbonNyiButton("##TsXBreak", RibbonIconKind::SurfBandage, "Crossing BL",
                      ImVec2(curCompact ? rowH : cell, rowH),
                      curCompact ? RibbonLabel::None : RibbonLabel::Right);
      RibbonNyiButton("##TsVisCheck", RibbonIconKind::SurfEye, "Visibility",
                      ImVec2(curCompact ? rowH : cell, rowH),
                      curCompact ? RibbonLabel::None : RibbonLabel::Right);
      ImGui::EndGroup();
      ImGui::SameLine(0, 4);
      ImGui::BeginGroup();
      if (smallBtn("##TsCatchment", RibbonIconKind::SurfCatchment, "Catchment", cell))
        StartCatchmentCommand(cmd, surfName, log);
      RibbonItemHelp("Catchment Area — pick an outlet on this surface.\nCommand bar: CATCHMENT");
      if (smallBtn("##TsVolDash", RibbonIconKind::SurfVolumes, "Volumes", cell))
        cmd.volumeDashboard.open = true;
      RibbonItemHelp("Volumes Dashboard — base vs comparison cut/fill.");
      ImGui::EndGroup();
      RibbonSectionEnd();
    }});
    }

    {
      const float toolW = colW({"Drape Image", "Extract", "Move to Surface"});
      ribbonSpecs.push_back({8.f + toolW, 8.f + toolW, [&]() {
      const float cw = colW({"Drape Image", "Extract", "Move to Surface"});
      RibbonSectionBegin("RibbonSecTsTools", "Surface Tools", 8.f + cw, panelH);
      ImGui::BeginGroup();
      RibbonNyiButton("##TsDrape", RibbonIconKind::SurfDrape, "Drape Image",
                      ImVec2(curCompact ? rowH : cw, rowH),
                      curCompact ? RibbonLabel::None : RibbonLabel::Right);
      if (smallBtn("##TsExtract", RibbonIconKind::SurfExtract, "Extract", cw))
        ImGui::OpenPopup("##TsExtractMenu");
      RibbonItemHelp("Extract from Surface — contours, water-drop, or catchment.");
      if (ImGui::BeginPopup("##TsExtractMenu")) {
        if (ImGui::MenuItem("Contours"))
          SubmitRibbonCommand(cmd, log, "EXTRACT " + surfName);
        if (ImGui::MenuItem("Water Drop Path"))
          SubmitRibbonCommand(cmd, log, "WATERDROP EXTRACT");
        if (ImGui::MenuItem("Water Drop Feature Line"))
          SubmitRibbonCommand(cmd, log, "WATERDROP EXTRACT FL");
        if (ImGui::MenuItem("Catchment"))
          SubmitRibbonCommand(cmd, log, "CATCHMENT EXTRACT");
        ImGui::EndPopup();
      }
      RibbonNyiButton("##TsMoveTo", RibbonIconKind::SurfMoveTo, "Move to Surface",
                      ImVec2(curCompact ? rowH : cw, rowH),
                      curCompact ? RibbonLabel::None : RibbonLabel::Right);
      ImGui::EndGroup();
      RibbonSectionEnd();
    }});
    }

    {
      const float wLaunch = 8.f + belowW("Quick Profile") + 4.f + colW({"Create Profile", "Data Shortcut", "Grading Tools"}) + 8.f;
      ribbonSpecs.push_back({wLaunch, wLaunch, [&]() {
      const float tsQP = belowW("Quick Profile");
      const float cell = colW({"Create Profile", "Data Shortcut", "Grading Tools"});
      RibbonSectionBegin("RibbonSecTsLaunch", "Launch Pad", 8.f + tsQP + 4.f + cell + 8.f, panelH);
      if (RibbonButtonEx("##TsQProfile", RibbonIconKind::SurfQuickProfile, "Quick Profile", ImVec2(tsQP, colH),
                         RibbonLabel::Below))
        StartQuickProfileCommand(cmd, surfName, log);
      RibbonItemHelp("Quick Profile — sample this surface along two plan points.\nCommand bar: QUICKPROFILE");
      ImGui::SameLine(0, 4);
      ImGui::BeginGroup();
      RibbonNyiButton("##TsCProfile", RibbonIconKind::SurfProfile, "Create Profile",
                      ImVec2(curCompact ? rowH : cell, rowH),
                      curCompact ? RibbonLabel::None : RibbonLabel::Right);
      RibbonNyiButton("##TsDShort", RibbonIconKind::SurfDataShortcut, "Data Shortcut",
                      ImVec2(curCompact ? rowH : cell, rowH),
                      curCompact ? RibbonLabel::None : RibbonLabel::Right);
      RibbonNyiButton("##TsGrade", RibbonIconKind::SurfGrading, "Grading Tools",
                      ImVec2(curCompact ? rowH : cell, rowH),
                      curCompact ? RibbonLabel::None : RibbonLabel::Right);
      ImGui::EndGroup();
      RibbonSectionEnd();
    }});
    }
  } // kRibbonTabSurfaceCtx

  // REQ-153: Civil 3D-shaped contextual SURVEY Point(s) tab (selected survey points).
  if (cmd.activeRibbonTab == kRibbonTabSurveyPointCtx && !ribbonPaperSpace && hasSvyPts) {
    const bool singlePt = (nSvyPts == 1);

    {
      const float wAddTables = capW("Add\nTables");
      const float wEditLbl = capW("Edit Label\nText");
      const float wLabels = singlePt ? (8.f + wAddTables + 4.f + wEditLbl + 8.f) : (8.f + wAddTables + 8.f);
      ribbonSpecs.push_back({wLabels, wLabels, [&]() {
        const float wTbl = capW("Add\nTables");
        const float wEdit = capW("Edit Label\nText");
        const bool one = (nSvyPts == 1);
        RibbonSectionBegin("RibbonSecSpLabels", one ? "Labels & Tables" : "Tables",
                           one ? (8.f + wTbl + 4.f + wEdit + 8.f) : (8.f + wTbl + 8.f), panelH);
        RibbonNyiButton("##SpAddTables", RibbonIconKind::SurfLegend, "Add\nTables", ImVec2(wTbl, colH),
                        RibbonLabel::Below);
        if (one) {
          ImGui::SameLine(0, 4);
          RibbonNyiButton("##SpEditLbl", RibbonIconKind::Text, "Edit Label\nText", ImVec2(wEdit, colH),
                          RibbonLabel::Below);
        }
        RibbonSectionEnd();
      }});
    }

    {
      const float wInq = capW("Inquiry");
      const float genCell = colW({"Properties", "Isolate Objects"});
      const float wGen = 8.f + wInq + 4.f + genCell + 8.f;
      ribbonSpecs.push_back({wGen, wGen, [&]() {
        const float wInquiry = capW("Inquiry");
        const float cell = colW({"Properties", "Isolate Objects"});
        RibbonSectionBegin("RibbonSecSpGen", "General Tools", 8.f + wInquiry + 4.f + cell + 8.f, panelH);
        if (RibbonButtonEx("##SpInquiry", RibbonIconKind::SurfInquiry, "Inquiry", ImVec2(wInquiry, colH),
                           RibbonLabel::Below))
          StartIdPointCommand(cmd, log);
        RibbonItemHelp("Inquiry — identify a point.\nCommand bar: ID");
        ImGui::SameLine(0, 4);
        ImGui::BeginGroup();
        if (smallBtn("##SpProps", RibbonIconKind::SurfPropsHand, "Properties", cell))
          cmd.pendingPropertiesFocus = true;
        RibbonItemHelp("Properties — the side Properties panel for the current selection.");
        if (smallBtn("##SpIsolate", RibbonIconKind::SurfIsolate, "Isolate Objects", cell))
          ImGui::OpenPopup("##SpIsolateMenu");
        RibbonItemHelp("Isolate Objects — isolate, hide, or end isolation (REQ-084).");
        if (ImGui::BeginPopup("##SpIsolateMenu")) {
          if (ImGui::MenuItem("Isolate Objects"))
            IsolateSelectedObjects(cmd, log);
          if (ImGui::MenuItem("Hide Objects"))
            HideSelectedObjects(cmd, log);
          if (ImGui::MenuItem("End Object Isolation", nullptr, false, !cmd.hiddenEntityIds.empty()))
            EndObjectIsolation(cmd, log);
          ImGui::EndPopup();
        }
        ImGui::EndGroup();
        RibbonSectionEnd();
      }});
    }

    {
      const float wEditList = capW("Edit/List\nPoints");
      const float wPgProps = capW("Point Group\nProperties");
      const float cRen = colW({"Renumber", "Datum", "Elevations from Surface"});
      const float cLock = colW({"Lock Points", "Unlock Points"});
      const float wMod = 8.f + wEditList + 4.f + wPgProps + 4.f + cRen + 4.f + cLock + 8.f;
      ribbonSpecs.push_back({wMod, wMod, [&]() {
        const float wList = capW("Edit/List\nPoints");
        const float wGrp = capW("Point Group\nProperties");
        const float cA = colW({"Renumber", "Datum", "Elevations from Surface"});
        const float cB = colW({"Lock Points", "Unlock Points"});
        RibbonSectionBegin("RibbonSecSpMod", "Modify", 8.f + wList + 4.f + wGrp + 4.f + cA + 4.f + cB + 8.f, panelH);
        if (RibbonButtonEx("##SpEditList", RibbonIconKind::SurveyPoint, "Edit/List\nPoints", ImVec2(wList, colH),
                           RibbonLabel::Below))
          StartViewPointsCommand(cmd, log);
        RibbonItemHelp("Edit/List Points — the survey point list.\nCommand bar: VIEWPOINTS");
        ImGui::SameLine(0, 4);
        if (RibbonButtonEx("##SpPgProps", RibbonIconKind::SurfPropsHand, "Point Group\nProperties", ImVec2(wGrp, colH),
                           RibbonLabel::Below))
          cmd.showPointGroupManagerWindow = true;
        RibbonItemHelp("Point Group Properties — create and edit point groups.");
        ImGui::SameLine(0, 4);
        ImGui::BeginGroup();
        RibbonNyiButton("##SpRenumber", RibbonIconKind::SvyRenumber, "Renumber", ImVec2(cA, rowH),
                        RibbonLabel::Right);
        RibbonNyiButton("##SpDatum", RibbonIconKind::Id, "Datum", ImVec2(cA, rowH), RibbonLabel::Right);
        RibbonNyiButton("##SpElevSurf", RibbonIconKind::SurfAddData, "Elevations from Surface", ImVec2(cA, rowH),
                        RibbonLabel::Right);
        ImGui::EndGroup();
        ImGui::SameLine(0, 4);
        ImGui::BeginGroup();
        RibbonNyiButton("##SpLock", RibbonIconKind::SvyLock, "Lock Points", ImVec2(cB, rowH), RibbonLabel::Right);
        RibbonNyiButton("##SpUnlock", RibbonIconKind::SvyUnlock, "Unlock Points", ImVec2(cB, rowH),
                        RibbonLabel::Right);
        ImGui::EndGroup();
        RibbonSectionEnd();
      }});
    }

    {
      const float wGeo = capW("Geodetic\nCalculator");
      ribbonSpecs.push_back({8.f + wGeo + 8.f, 8.f + wGeo + 8.f, [&]() {
        const float w = capW("Geodetic\nCalculator");
        RibbonSectionBegin("RibbonSecSpAnalyze", "Analyze", 8.f + w + 8.f, panelH);
        RibbonNyiButton("##SpGeodetic", RibbonIconKind::SvyGeodetic, "Geodetic\nCalculator", ImVec2(w, colH),
                        RibbonLabel::Below);
        RibbonSectionEnd();
      }});
    }

    {
      const float toolsCol = colW({"Import Points", "Export Points", "Transfer Points"});
      const float wTools = 8.f + toolsCol + 8.f;
      ribbonSpecs.push_back({wTools, wTools, [&]() {
        const float cell = colW({"Import Points", "Export Points", "Transfer Points"});
        RibbonSectionBegin("RibbonSecSpTools", "SURVEY Point Tools", 8.f + cell + 8.f, panelH);
        ImGui::BeginGroup();
        if (smallBtn("##SpImport", RibbonIconKind::Import, "Import Points", cell))
          StartImportPointsCommand(cmd, log);
        RibbonItemHelp("Import Points — load a point file.\nCommand bar: IMPORTPOINTS");
        if (smallBtn("##SpExport", RibbonIconKind::ClipboardCopy, "Export Points", cell))
          StartExportPointsCommand(cmd, log);
        RibbonItemHelp("Export Points — write selected or all points.\nCommand bar: EXPORTPOINTS");
        RibbonNyiButton("##SpTransfer", RibbonIconKind::SurfMoveTo, "Transfer Points", ImVec2(cell, rowH),
                        RibbonLabel::Right);
        ImGui::EndGroup();
        RibbonSectionEnd();
      }});
    }

    {
      const float wCreate = capW("Create\nPoints");
      const float launchCol = colW({"Create Point Group", "Import Points", "Create Surface"});
      const float wLaunch = 8.f + wCreate + 4.f + launchCol + 8.f;
      ribbonSpecs.push_back({wLaunch, wLaunch, [&]() {
        const float wPts = capW("Create\nPoints");
        const float cell = colW({"Create Point Group", "Import Points", "Create Surface"});
        RibbonSectionBegin("RibbonSecSpLaunch", "Launch Pad", 8.f + wPts + 4.f + cell + 8.f, panelH);
        if (RibbonButtonEx("##SpCreatePts", RibbonIconKind::SurveyPoint, "Create\nPoints", ImVec2(wPts, colH),
                           RibbonLabel::Below))
          StartCreatePointsCommand(cmd, log);
        RibbonItemHelp("Create Points — pick or type survey points.\nCommand bar: CREATEPOINTS");
        ImGui::SameLine(0, 4);
        ImGui::BeginGroup();
        if (smallBtn("##SpCreateGrp", RibbonIconKind::Layers, "Create Point Group", cell))
          cmd.showPointGroupManagerWindow = true;
        RibbonItemHelp("Create Point Group — the Point Groups window.");
        if (smallBtn("##SpImport2", RibbonIconKind::Import, "Import Points", cell))
          StartImportPointsCommand(cmd, log);
        RibbonItemHelp("Import Points — load a point file.\nCommand bar: IMPORTPOINTS");
        if (smallBtn("##SpCreateSurf", RibbonIconKind::SurfAddData, "Create Surface", cell))
          cmd.showCreateSurfaceWindow = true;
        RibbonItemHelp("Create Surface — TIN, grid, corridor, or volume type.");
        ImGui::EndGroup();
        RibbonSectionEnd();
      }});
    }
  } // kRibbonTabSurveyPointCtx

  if (cmd.activeRibbonTab == kRibbonTabBlockEditor && inBedit) {
    auto beditSubmit = [&](const char* line) {
      char buf[192];
      std::snprintf(buf, sizeof(buf), "%s", line);
      ProcessCommandLineSubmit(buf, static_cast<int>(sizeof(buf)), cmd, log);
    };
    const float wOpen = 8.f + colW({"Edit Block", "Save Block", "Test Block"});
    ribbonSpecs.push_back({wOpen, wOpen, [&]() {
      const float cw = colW({"Edit Block", "Save Block", "Test Block"});
      RibbonSectionBegin("RibbonSecBeOpen", "Open/Save", wOpen, panelH);
      if (smallBtn("##BeEdit", RibbonIconKind::BlockEditor, "Edit Block", cw))
        CadBlocksOpenEditPicker(cmd, log);
      RibbonItemHelp("Edit Block — BEDIT the selected INSERT, or the definition already open.");
      if (smallBtn("##BeSave", RibbonIconKind::BeSaveBlock, "Save Block", cw))
        beditSubmit("BSAVE");
      RibbonItemHelp("Save Block — BSAVE. References update immediately.");
      RibbonNyiButton("##BeTest", RibbonIconKind::BlockEditor, "Test Block", ImVec2(cw, rowH), RibbonLabel::Right);
      RibbonSectionEnd();
    }});
    const float wGeom = 8.f + colW({"Auto Constrain", "Show/Hide", "Show All"});
    ribbonSpecs.push_back({wGeom, wGeom, [&]() {
      const float cw = colW({"Auto Constrain", "Show/Hide", "Show All"});
      RibbonSectionBegin("RibbonSecBeGeom", "Geometric", wGeom, panelH);
      RibbonNyiButton("##BeAutoC", RibbonIconKind::BeAutoConstrain, "Auto Constrain", ImVec2(cw, rowH), RibbonLabel::Right);
      RibbonNyiButton("##BeShHide", RibbonIconKind::BeConstraintShow, "Show/Hide", ImVec2(cw, rowH), RibbonLabel::Right);
      RibbonNyiButton("##BeShAll", RibbonIconKind::BeConstraintShow, "Show All", ImVec2(cw, rowH), RibbonLabel::Right);
      RibbonSectionEnd();
    }});
    const float wDim = 8.f + colW({"Linear", "Aligned"});
    ribbonSpecs.push_back({wDim, wDim, [&]() {
      const float cw = colW({"Linear", "Aligned"});
      RibbonSectionBegin("RibbonSecBeDim", "Dimensional", wDim, panelH);
      RibbonNyiButton("##BeDimL", RibbonIconKind::DimLinear, "Linear", ImVec2(cw, rowH), RibbonLabel::Right);
      RibbonNyiButton("##BeDimA", RibbonIconKind::Dim, "Aligned", ImVec2(cw, rowH), RibbonLabel::Right);
      RibbonSectionEnd();
    }});
    const float wMan = 8.f + colW({"Block Table", "Parameters", "Palettes"});
    ribbonSpecs.push_back({wMan, wMan, [&]() {
      const float cw = colW({"Block Table", "Parameters", "Palettes"});
      RibbonSectionBegin("RibbonSecBeMan", "Manage", wMan, panelH);
      RibbonNyiButton("##BeBTable", RibbonIconKind::BeBlockTable, "Block Table", ImVec2(cw, rowH), RibbonLabel::Right);
      RibbonNyiButton("##BePMan", RibbonIconKind::BeParameters, "Parameters", ImVec2(cw, rowH), RibbonLabel::Right);
      if (smallBtn("##BePalettes", RibbonIconKind::BePalettes, "Palettes", cw))
        cmd.blockAuthoringPaletteOpen = !cmd.blockAuthoringPaletteOpen;
      RibbonItemHelp("Authoring Palettes — Parameters, Actions, Parameter Sets, Constraints.");
      RibbonSectionEnd();
    }});
    const float wAct = 8.f + colW({"Point", "Linear", "Polar"}) + 4.f + colW({"XY", "Rotation", "Align"}) + 4.f +
                       colW({"Flip", "Visibility", "Lookup"}) + 4.f + colW({"Basepoint"});
    ribbonSpecs.push_back({wAct, wAct, [&]() {
      const float c0 = colW({"Point", "Linear", "Polar"});
      const float c1 = colW({"XY", "Rotation", "Align"});
      const float c2 = colW({"Flip", "Visibility", "Lookup"});
      const float c3 = colW({"Basepoint"});
      RibbonSectionBegin("RibbonSecBeAct", "Action Parameters", wAct, panelH);
      auto addP = [&](const char* kind, RibbonIconKind icon, const char* id, const char* label, float cw) {
        if (smallBtn(id, icon, label, cw)) {
          char line[96];
          std::snprintf(line, sizeof(line), "BPARAM %s1, %s", label, kind);
          ProcessCommandLineSubmit(line, static_cast<int>(sizeof(line)), cmd, log);
        }
      };
      ImGui::BeginGroup();
      addP("point", RibbonIconKind::BeParamPoint, "##BePPoint", "Point", c0);
      addP("linear", RibbonIconKind::BeParamLinear, "##BePLin", "Linear", c0);
      addP("polar", RibbonIconKind::BeParamPolar, "##BePPol", "Polar", c0);
      ImGui::EndGroup();
      ImGui::SameLine(0, 4);
      ImGui::BeginGroup();
      addP("move", RibbonIconKind::BeParamXY, "##BePXY", "XY", c1);
      addP("rotation", RibbonIconKind::BeParamRotation, "##BePRot", "Rotation", c1);
      addP("linear", RibbonIconKind::BeParamAlignment, "##BePAln", "Align", c1);
      ImGui::EndGroup();
      ImGui::SameLine(0, 4);
      ImGui::BeginGroup();
      addP("flip", RibbonIconKind::BeParamFlip, "##BePFlip", "Flip", c2);
      addP("visibility", RibbonIconKind::BeParamVisibility, "##BePVis", "Visibility", c2);
      addP("lookup", RibbonIconKind::BeParamLookup, "##BePLook", "Lookup", c2);
      ImGui::EndGroup();
      ImGui::SameLine(0, 4);
      ImGui::BeginGroup();
      addP("point", RibbonIconKind::BeParamBasepoint, "##BePBase", "Basepoint", c3);
      ImGui::EndGroup();
      RibbonSectionEnd();
    }});
    ribbonSpecs.push_back({160.f, 160.f, [&]() {
      RibbonSectionBegin("RibbonSecBeVis", "Visibility", 160.f, panelH);
      const int di = CadBlockFindDef(cmd.blockDefs, cmd.blockEditorName);
      const char* vis = "VisibilityState0";
      if (di >= 0 && !cmd.blockDefs[static_cast<size_t>(di)].visibilityStates.empty())
        vis = cmd.blockDefs[static_cast<size_t>(di)].visibilityStates[0].c_str();
      ImGui::SetNextItemWidth(148.f);
      if (ImGui::BeginCombo("##BeVisCombo", vis)) {
        if (di >= 0) {
          for (const std::string& s : cmd.blockDefs[static_cast<size_t>(di)].visibilityStates) {
            if (ImGui::Selectable(s.c_str(), s == vis)) {
              char line[192];
              std::snprintf(line, sizeof(line), "BSETVIS %s", s.c_str());
              ProcessCommandLineSubmit(line, static_cast<int>(sizeof(line)), cmd, log);
            }
          }
        }
        ImGui::EndCombo();
      }
      RibbonSectionEnd();
    }});
    ribbonSpecs.push_back({136.f, 136.f, [&]() {
      RibbonSectionBegin("RibbonSecBeClose", "Close", 136.f, panelH);
      ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(180, 40, 40, 255));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(210, 50, 50, 255));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(140, 20, 20, 255));
      if (ImGui::Button("Close Block Editor", ImVec2(128.f, colH)))
        beditSubmit("BCLOSE");
      ImGui::PopStyleColor(3);
      RibbonItemHelp("Close Block Editor — BCLOSE. Prompts to save if there are unsaved edits.");
      RibbonSectionEnd();
    }});
  }

  // REQ-302: View tab.
  if (cmd.activeRibbonTab == kRibbonTabView) {
    ribbonSpecs.push_back({W.wView, M.wView, [&]() {
      RibbonSectionBegin("RibbonSecView", "View", curCompact ? M.wView : W.wView, panelH);
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
        // Visual style (REQ-064). Sits in View because it is a property of how this viewport draws,
        // not of the drawing — the same reasoning that put it on RenderTuning rather than on an entity.
        ImGui::SameLine(0, 8);
        ImGui::BeginGroup();
        ImGui::TextUnformatted("Visual style");
        ImGui::SetNextItemWidth(visualStyleComboW);
        int vsIdx = static_cast<int>(cmd.viewportVisualStyle);
        const char* kVsItems[] = {"2D Wireframe", "Hidden", "Shaded"};
        if (ImGui::Combo("##RibbonVisualStyle", &vsIdx, kVsItems, IM_ARRAYSIZE(kVsItems)))
          cmd.viewportVisualStyle = static_cast<VisualStyle>(vsIdx);
        RibbonItemHelp("How the viewport draws.\n"
                       "2D Wireframe — every edge visible, no depth testing (the classic view).\n"
                       "Hidden — near geometry hides far geometry.\n"
                       "Shaded — filled surfaces lit from the camera.\n"
                       "Command bar: VISUALSTYLE (VS) 2D | HIDDEN | SHADED");
        ImGui::EndGroup();
        // Projection (REQ-309). Beside visual style for the same reason it is in View at all: both
        // describe how this viewport draws, and neither changes a stored coordinate.
        ImGui::SameLine(0, 8);
        ImGui::BeginGroup();
        ImGui::TextUnformatted("Projection");
        ImGui::SetNextItemWidth(visualStyleComboW);
        int projIdx = cmd.viewportProjection == Camera::Projection::Perspective ? 1 : 0;
        const char* kProjItems[] = {"Orthographic", "Perspective"};
        if (ImGui::Combo("##RibbonProjection", &projIdx, kProjItems, IM_ARRAYSIZE(kProjItems)))
          cmd.viewportProjection =
              projIdx == 1 ? Camera::Projection::Perspective : Camera::Projection::Orthographic;
        RibbonItemHelp("How the view projects.\n"
                       "Orthographic — parallel projection; the engineering-drawing view.\n"
                       "Perspective — converging projection, for visually inspecting a 3D model.\n"
                       "Switching does not change the drawing.\n"
                       "Command bar: PERSPECTIVE ON | OFF, and FOV to set the angle");
        // The field of view only means something under perspective, so it appears only there
        // rather than sitting permanently greyed out.
        if (cmd.viewportProjection == Camera::Projection::Perspective) {
          ImGui::SetNextItemWidth(visualStyleComboW);
          float fov = cmd.viewportFovDeg;
          if (ImGui::SliderFloat("##RibbonFov", &fov, kMinFovDeg, kMaxFovDeg, "FOV %.0f°"))
            cmd.viewportFovDeg = std::clamp(fov, kMinFovDeg, kMaxFovDeg);
          RibbonItemHelp("Perspective field of view, in degrees.\nCommand bar: FOV <1-179>");
        }
        ImGui::EndGroup();
      }
      RibbonSectionEnd();
    }});

    // REQ-302 increment 3 (content audit, D-2026-08-25-h): Settings placed on View per the user's
    // explicit decision — same window the View menu's "Settings..." item already opens.
    // REQ-106 Named Views. AutoCAD puts this on the View tab beside the viewport tools, and the
    // combo does double duty there: it NAMES the current view and it is how you change it.
    ribbonSpecs.push_back({W.wNamedViews, M.wNamedViews, [&]() {
      RibbonSectionBegin("RibbonSecNamedViews", "Named Views",
                         curCompact ? M.wNamedViews : W.wNamedViews, panelH);
      {
        ImGui::BeginGroup();
        // "Unsaved View" whenever the camera does not correspond to a saved one — AutoCAD's own
        // wording, and a statement about the CAMERA rather than about unsaved drawing edits.
        const NamedView* curView = CurrentNamedView(cmd);
        const std::string label = curView ? curView->name : std::string("Unsaved View");
        ImGui::SetNextItemWidth(namedViewComboW);
        // HeightLargest so all ten orientations, the saved views and View Manager are visible at once.
        // The default height scrolls at eight, which hides the four isometrics behind a scrollbar -
        // and a preset list you have to scroll is slower than the ViewCube it is meant to complement.
        if (ImGui::BeginCombo("##RibbonNamedView", label.c_str(), ImGuiComboFlags_HeightLargest)) {
          // The ten standard orientations. Angles come from the ViewCube's own face table and its
          // isometric constant rather than a second copy — the two widgets must agree about where
          // "Front" is, and a duplicated table is how they would stop agreeing.
          struct Preset { const char* name; float az; float el; };
          static const Preset kPresets[] = {
              {"Top", 0.f, 90.f},      {"Bottom", 0.f, -90.f},
              {"Left", 270.f, 0.f},    {"Right", 90.f, 0.f},
              {"Front", 0.f, 0.f},     {"Back", 180.f, 0.f},
              {"SW Isometric", 45.f, viewcube::kIsometricElevationDeg},
              {"SE Isometric", 315.f, viewcube::kIsometricElevationDeg},
              {"NE Isometric", 135.f, viewcube::kIsometricElevationDeg},
              {"NW Isometric", 225.f, viewcube::kIsometricElevationDeg},
          };
          for (const Preset& p : kPresets) {
            if (ImGui::Selectable(p.name)) {
              // Orientation only: a preset says which way you are looking, not where you are or how
              // far. Keeping pan and zoom is what makes "show me this from the SW" usable without
              // losing the part of the drawing you were working on.
              CadStartViewAnimation(cmd, p.az, p.el);
            }
          }
          if (!cmd.namedViews.empty()) {
            ImGui::Separator();
            for (const NamedView& v : cmd.namedViews) {
              const bool sel = (curView && curView->name == v.name);
              if (ImGui::Selectable(v.name.c_str(), sel))
                RestoreNamedView(cmd, v, log);
            }
          }
          ImGui::Separator();
          if (ImGui::Selectable("View Manager..."))
            cmd.showViewManagerWindow = true;
          ImGui::EndCombo();
        }
        RibbonItemHelp("Named views (REQ-106). The ten standard orientations, then any views saved in\n"
                       "this drawing. A saved view restores the camera AND the coordinate frame.\n"
                       "Command bar: VIEW [Save/Restore/Delete/?] <name>");
        ImGui::EndGroup();

        ImGui::SameLine(0, 4);
        ImGui::BeginGroup();
        const float cwv = colW({"New View", "Manager"});
        if (smallBtn("##RibbonNewView", RibbonIconKind::ZoomWindow, "New View", cwv))
          cmd.showViewManagerNewPrompt = true;
        RibbonItemHelp("Save the current camera and coordinate frame under a name.\nCommand bar: VIEW S <name>");
        if (smallBtn("##RibbonViewMgr", RibbonIconKind::Layers, "Manager", cwv))
          cmd.showViewManagerWindow = true;
        RibbonItemHelp("View Manager — restore and delete saved views, and the drawing's saved\n"
                       "coordinate systems.\nCommand bar: VIEW");
        ImGui::EndGroup();
      }
      RibbonSectionEnd();
    }});

    // REQ-154 Coordinates. AutoCAD's own panel name and position — View tab, beside Named Views.
    // The frame selector is duplicated from the one under the ViewCube on purpose: that one is where
    // your eye already is while drawing, this one is where you go looking when you want to CHANGE
    // frames. Both read their label from CadUcsFrameLabel so they cannot disagree.
    ribbonSpecs.push_back({W.wCoords, M.wCoords, [&]() {
      RibbonSectionBegin("RibbonSecCoords", "Coordinates", curCompact ? M.wCoords : W.wCoords, panelH);
      {
        const float cwc = colW({"3-Point", "Object"});
        ImGui::BeginGroup();
        if (smallBtn("##RibbonUcsCmd", RibbonIconKind::ZoomWindow, "UCS", cwc))
          StartUcsCommand(cmd, log);
        RibbonItemHelp("UCS — manages user coordinate systems.\n"
                       "Pick an origin, then an X-axis point, then a point on the XY plane.\n"
                       "Command bar: UCS");
        if (smallBtn("##RibbonUcs3P", RibbonIconKind::ZoomWindow, "3-Point", cwc)) {
          StartUcsCommand(cmd, log);  // the three-point form IS the bare command's default path
        }
        RibbonItemHelp("Define a frame from three picks: origin, +X direction, and a point on the\n"
                       "+Y half of the plane.\nCommand bar: UCS then three points");
        ImGui::EndGroup();

        ImGui::SameLine(0, 4);
        ImGui::BeginGroup();
        if (smallBtn("##RibbonUcsWorld", RibbonIconKind::ZoomExtents, "World", cwc))
          ProcessCommandLineSubmitStr(cmd, "UCS W", log);
        RibbonItemHelp("Back to the World Coordinate System.\nCommand bar: UCS W");
        if (smallBtn("##RibbonUcsPrev", RibbonIconKind::ZoomExtents, "Previous", cwc))
          ProcessCommandLineSubmitStr(cmd, "UCS P", log);
        RibbonItemHelp("Step back to the previous frame.\nCommand bar: UCS P");
        ImGui::EndGroup();

        ImGui::SameLine(0, 4);
        ImGui::BeginGroup();
        if (smallBtn("##RibbonUcsObj", RibbonIconKind::Layers, "Object", cwc))
          ProcessCommandLineSubmitStr(cmd, "UCS OB", log);
        RibbonItemHelp("Align the frame to a picked line, arc, circle, ellipse or text.\nCommand bar: UCS OB");
        if (smallBtn("##RibbonUcsZ", RibbonIconKind::Layers, "Rotate Z", cwc))
          ProcessCommandLineSubmitStr(cmd, "UCS Z", log);
        RibbonItemHelp("Spin the frame about its own Z axis. Type an angle, or 2P to take it from\n"
                       "two picked points.\nCommand bar: UCS Z");
        ImGui::EndGroup();

        // The frame selector, same content as the ViewCube's.
        ImGui::SameLine(0, 8);
        ImGui::BeginGroup();
        ImGui::TextUnformatted("Coordinate system");
        const std::string frameLabel = CadUcsFrameLabel(cmd);
        ImGui::SetNextItemWidth(ucsComboW);
        if (ImGui::BeginCombo("##RibbonUcsPick", frameLabel.c_str(), ImGuiComboFlags_HeightLargest)) {
          const bool isW = CadUcsIsWorld(cmd);
          if (ImGui::Selectable("WCS", isW) && !isW)
            SetActiveUcs(cmd, ucs::Ucs{}, log);
          if (!cmd.ucsNamed.empty()) {
            ImGui::Separator();
            for (const NamedUcs& n : cmd.ucsNamed) {
              const bool sel = !isW && ucs::FramesMatch(n.frame, cmd.activeUcs);
              if (ImGui::Selectable(n.name.c_str(), sel) && !sel)
                SetActiveUcs(cmd, n.frame, log);
            }
          }
          ImGui::Separator();
          if (ImGui::Selectable("New UCS..."))
            StartUcsCommand(cmd, log);
          ImGui::EndCombo();
        }
        RibbonItemHelp("The active coordinate system: WCS, a saved name, or Unnamed for a frame\n"
                       "built but not saved. Save one with UCS N <name>; rename, restore and\n"
                       "delete saved frames in the View Manager.");
        ImGui::EndGroup();
      }
      RibbonSectionEnd();
    }});

    ribbonSpecs.push_back({W.wViewSettings, M.wViewSettings, [&]() {
      RibbonSectionBegin("RibbonSecViewSettings", "Settings", curCompact ? M.wViewSettings : W.wViewSettings, panelH);
      {
        if (smallBtn("##RibbonSettings", RibbonIconKind::Settings, "Settings", colW({"Settings"})))
          cmd.showSettingsWindow = true;
        RibbonItemHelp("Open application settings (same as View menu → Settings...).");
        ImGui::SameLine(0, 4);
        if (smallBtn("##RibbonToolspace", RibbonIconKind::Toolspace, "Toolspace", colW({"Toolspace"})))
          cmd.showToolspaceWindow = true;
        RibbonItemHelp("Toolspace — drawing explorer (Prospector and Settings).\nCommand bar: TOOLSPACE");
      }
      RibbonSectionEnd();
    }});
  } // if (activeRibbonTab == kRibbonTabView)

  // REQ-302 / GUI-pass 2026-08-30: Insert tab laid out like the Home tab — sections Import, Block,
  // Reference, Point Cloud, Data. Commands GoSurvey implements are wired to their slot; everything
  // else is greyed with an automatic "… — not implemented yet." tooltip (RibbonNyiButton), same as
  // the Home tab's Civil 3D placeholders. Import DXF/DWG/PDF also live on the File menu (second
  // entry point, not a move).
  if (cmd.activeRibbonTab == kRibbonTabInsert) {
    // ---- Import ---------------------------------------------------------
    {
      const float cA = colW({"Import DXF", "Import DWG", "Import PDF"});
      const float cB = colW({"LandXML", "Points From File", "Import Survey Data"});
      const float w = 8.f + (curCompact ? rowH * 2.f + 4.f : cA + 4.f + cB);
      const float mw = 8.f + rowH * 2.f + 4.f;
      ribbonSpecs.push_back({8.f + cA + 4.f + cB, mw, [&, cA, cB]() {
        static char ribbonDxfPath[4096]{};
        static char ribbonDwgPath[4096]{};
        const float wSec = curCompact ? mw : 8.f + cA + 4.f + cB;
        RibbonSectionBegin("RibbonSecInsImport", "Import", wSec, panelH);
        ImGui::BeginGroup();
        if (smallBtn("##RibbonImportDxf", RibbonIconKind::Import, "Import DXF", cA)) {
          if (BrowseOpenFileDxfUtf8(ribbonDxfPath, sizeof(ribbonDxfPath)))
            ImportDxfFile(cmd, ribbonDxfPath, log);
        }
        RibbonItemHelp("Import a DXF drawing into the current session.\nSame as File menu → Import DXF...");
        if (smallBtn("##RibbonImportDwg", RibbonIconKind::Import, "Import DWG", cA)) {
          if (BrowseOpenFileDwgUtf8(ribbonDwgPath, sizeof(ribbonDwgPath)))
            ImportDwgFile(cmd, ribbonDwgPath, log);
        }
        RibbonItemHelp("Import a DWG drawing (LibreDWG, no converter).\nSame as File menu → Import DWG...");
        if (insRow("##RibbonImportPdf", "PDF_Import", "Import PDF", cA))
          StartPdfAttachCommand(cmd, log);
        RibbonItemHelp("Attach a PDF page as a raster underlay.\nCommand bar: PDFATTACH");
        ImGui::EndGroup();
        ImGui::SameLine(0, 4);
        ImGui::BeginGroup();
        insNyi("##RibbonInsLandXml", "DGN_Import", "LandXML", cB);
        insNyi("##RibbonInsPointsFile", "Import", "Points From File", cB);
        insNyi("##RibbonInsSurveyData", "svytripod", "Import Survey Data", cB);
        ImGui::EndGroup();
        RibbonSectionEnd();
      }});
      (void)w;
    }

    // ---- Block ---------------------------------------------------------
    {
      const float cA = colW({"Insert", "Create"});
      const float cB = colW({"Edit", "Edit Attributes"});
      const float w = 8.f + cA + 4.f + cB;
      ribbonSpecs.push_back({w, w, [&, cA, cB]() {
        RibbonSectionBegin("RibbonSecInsBlock", "Block", w, panelH);
        ImGui::BeginGroup();
        if (smallBtn("##RibbonInsInsert", RibbonIconKind::BlockInsert, "Insert", cA))
          StartInsertBlockCommand(cmd, log);
        RibbonItemHelp("Insert a block. Opens the Insert dialog (same as INSERT).");
        if (insRow("##RibbonInsCreate", "Make_Block", "Create", cA)) {
          char buf[8] = "BLOCK";
          ProcessCommandLineSubmit(buf, static_cast<int>(sizeof(buf)), cmd, log);
        }
        RibbonItemHelp("Create a block definition from the selection.\nCommand bar: BLOCK <name>, <x>, <y>");
        ImGui::EndGroup();
        ImGui::SameLine(0, 4);
        ImGui::BeginGroup();
        if (insRow("##RibbonInsEdit", "Block_Editor", "Edit", cB))
          CadBlocksOpenEditPicker(cmd, log);
        RibbonItemHelp("Block Editor — choose a definition from the drawing or block library.\nCommand bar: BEDIT");
        insNyi("##RibbonInsEditAttr", "Multiple_Attributes", "Edit Attributes", cB);
        ImGui::EndGroup();
        RibbonSectionEnd();
      }});
    }

    // ---- Reference ---------------------------------------------------------
    {
      const float cw = colW({"Attach", "Clip", "Adjust"});
      const float w = 8.f + cw;
      ribbonSpecs.push_back({w, w, [&, cw]() {
        RibbonSectionBegin("RibbonSecInsReference", "Reference", w, panelH);
        ImGui::BeginGroup();
        insNyi("##RibbonRefAttach", "Attach", "Attach", cw);
        insNyi("##RibbonRefClip", "Clip", "Clip", cw);
        insNyi("##RibbonRefAdjust", "Adjust", "Adjust", cw);
        ImGui::EndGroup();
        RibbonSectionEnd();
      }});
    }

    // ---- Point Cloud ---------------------------------------------------------
    {
      const float cw = colW({"Attach"});
      const float w = 8.f + cw;
      ribbonSpecs.push_back({w, w, [&, cw]() {
        RibbonSectionBegin("RibbonSecInsPointCloud", "Point Cloud", w, panelH);
        ImGui::BeginGroup();
        insNyi("##RibbonPcAttach", "Attach", "Attach", cw);
        ImGui::EndGroup();
        RibbonSectionEnd();
      }});
    }

    // ---- Data ---------------------------------------------------------
    {
      const float cw = colW({"Field", "Hyperlink"});
      const float w = 8.f + cw;
      ribbonSpecs.push_back({w, w, [&, cw]() {
        RibbonSectionBegin("RibbonSecInsData", "Data", w, panelH);
        ImGui::BeginGroup();
        insNyi("##RibbonDataField", "Field", "Field", cw);
        insNyi("##RibbonDataHyperlink", "Hyperlink", "Hyperlink", cw);
        ImGui::EndGroup();
        RibbonSectionEnd();
      }});
    }
  } // if (activeRibbonTab == kRibbonTabInsert)

  // REQ-302 / GUI-pass 2026-08-30: Output tab mirrors Civil 3D's Output ribbon 1:1 — Plan
  // Production, Plot, Export, Publish, Export to DWF/PDF. GoSurvey's real commands (Export DXF/DWG,
  // Plot, Batch Plot, Export Points) are wired to their C3D-equivalent slot; every other button is
  // greyed with the automatic "… — not implemented yet." tooltip. `c3d_*` icons with no library
  // match come from tools/gen_c3d_icons.cpp.
  if (cmd.activeRibbonTab == kRibbonTabOutput) {
    // ---- Plan Production ---------------------------------------------------
    {
      const float w = 8.f + belowW("Create View\nFrames") + 4.f + belowW("Create\nSheets") + 4.f +
                      belowW("Create Section\nSheets");
      ribbonSpecs.push_back({w, w, [&, w]() {
        RibbonSectionBegin("RibbonSecOutPlanProd", "Plan Production", w, panelH);
        nyiLarge("##OutViewFrames", "c3d_viewframes", "Create View\nFrames");
        ImGui::SameLine(0, 4);
        nyiLarge("##OutCreateSheets", "c3d_createsheets", "Create\nSheets");
        ImGui::SameLine(0, 4);
        nyiLarge("##OutSectionSheets", "c3d_sectionsheets", "Create Section\nSheets");
        RibbonSectionEnd();
      }, "Plan Production", RibbonIconKind::Nyi, "c3d_createsheets"});
    }

    // ---- Plot -----------------------------------------------------------
    {
      const float cw = colW({"Page Setup Manager", "Plotter Manager"});
      const float w = 8.f + belowW("Plot") + 4.f + cw + 4.f + cw;
      ribbonSpecs.push_back({w, w, [&, cw, w]() {
        RibbonSectionBegin("RibbonSecOutPlot", "Plot", w, panelH);
        if (RibbonButtonEx("##RibbonPlot", RibbonIconKind::Plot, "Plot",
                           ImVec2(belowW("Plot"), colH), RibbonLabel::Below))
          PlotActiveLayout(cmd, log);
        RibbonItemHelp("Plot the current layout to a vector PDF.\nCommand bar: PLOT");
        ImGui::SameLine(0, 4);
        ImGui::BeginGroup();
        if (insRow("##RibbonBatchPlot", "Plot", "Batch Plot", cw)) {
          cmd.batchPlotSelected.clear();
          if (cmd.activeSpaceIndex >= 0)
            cmd.batchPlotSelected.push_back(cmd.activeSpaceIndex);
          cmd.showBatchPlotDialog = true;
        }
        RibbonItemHelp("Batch plot — pick layouts to plot into one multi-page PDF.");
        insNyi("##RibbonPlotPreview", "c3d_plotpreview", "Preview", cw);
        if (insRow("##RibbonPageSetupMgr", "Page_Setup", "Page Setup Manager", cw)) {
          EnsureStandardPageSetup(cmd);
          cmd.pageSetupLayoutIdx  = cmd.activeSpaceIndex >= 0 ? cmd.activeSpaceIndex : 0;
          cmd.pageSetupManagerSel = -1;
          cmd.showPageSetupManager = true;
        }
        RibbonItemHelp("Page Setup Manager — named paper size / plot settings for the active layout.");
        ImGui::EndGroup();
        ImGui::SameLine(0, 4);
        ImGui::BeginGroup();
        insNyi("##RibbonViewDetails", "Display_and_Plot_Frames", "View Details", cw);
        insNyi("##RibbonPlotterMgr", "c3d_plottermgr", "Plotter Manager", cw);
        ImGui::EndGroup();
        RibbonSectionEnd();
      }, "Plot", RibbonIconKind::Plot});
    }

    // ---- Export -------------------------------------------------------
    {
      static char ribbonExpDxfPath[4096]{};
      static char ribbonExpDwgPath[4096]{};
      const float cw = colW({"Export Civil 3D Drawing", "Export Civil Objects to SDF"});
      const float w = 8.f + colW({"Export DXF"}) + 4.f + cw * 4.f + 4.f * 3.f;
      ribbonSpecs.push_back({w, w, [&, cw, w]() {
        RibbonSectionBegin("RibbonSecOutExport", "Export", w, panelH);
        ImGui::BeginGroup();
        if (smallBtn("##RibbonExportDxf", RibbonIconKind::Export, "Export DXF", colW({"Export DXF"}))) {
          if (BrowseSaveFileDxfUtf8(ribbonExpDxfPath, sizeof(ribbonExpDxfPath), "drawing.dxf"))
            ExportDxfFile(cmd, ribbonExpDxfPath, log);
        }
        RibbonItemHelp("Export the current drawing to DXF.\nSame as File menu → Export DXF...");
        if (smallBtn("##RibbonExportDwg", RibbonIconKind::Export, "Export DWG", colW({"Export DXF"}))) {
          if (BrowseSaveFileDwgUtf8(ribbonExpDwgPath, sizeof(ribbonExpDwgPath), "drawing.dwg")) {
            cmd.dwgPendingExportPath = ribbonExpDwgPath;
            cmd.dwgLossyExportModal  = true;
          }
        }
        RibbonItemHelp("Save DWG as R2000 via LibreDWG.\nSame as File menu → Export DWG...");
        if (insRow("##RibbonExportPoints", "c3d_exportpoints", "Export Points", colW({"Export DXF"})))
          cmd.showExportPointsWindow = true;
        RibbonItemHelp("Export survey points to a point file (PNEZD / user format).");
        ImGui::EndGroup();
        ImGui::SameLine(0, 4);
        ImGui::BeginGroup();
        insNyi("##RibbonExpImx", "c3d_exportto", "Export IMX", cw);
        insNyi("##RibbonExpLandXml", "c3d_landxml", "Export to LandXML", cw);
        insNyi("##RibbonExpC3dDwg", "c3d_exportto", "Export Civil 3D Drawing", cw);
        ImGui::EndGroup();
        ImGui::SameLine(0, 4);
        ImGui::BeginGroup();
        insNyi("##RibbonExpFgdb", "c3d_exportto", "Export to FGDB", cw);
        insNyi("##RibbonRehabMgr", "c3d_exportto", "Rehab Manager", cw);
        insNyi("##RibbonTransferPoints", "c3d_transferpoints", "Transfer Points", cw);
        ImGui::EndGroup();
        ImGui::SameLine(0, 4);
        ImGui::BeginGroup();
        insNyi("##RibbonExpHecRas", "c3d_exportto", "Export to HEC RAS", cw);
        insNyi("##RibbonExpSdf", "c3d_exportto", "Export Civil Objects to SDF", cw);
        insNyi("##RibbonExpStorm", "c3d_exportto", "Export to Storm Sewers", cw);
        ImGui::EndGroup();
        ImGui::SameLine(0, 4);
        ImGui::BeginGroup();
        insNyi("##RibbonExp3dsMax", "c3d_exportto", "Export to 3ds Max", cw);
        ImGui::EndGroup();
        RibbonSectionEnd();
      }, "Export", RibbonIconKind::Export});
    }

    // ---- Publish -----------------------------------------------------
    {
      const float w = 8.f + belowW("Publish\nSurfaces") + 4.f + belowW("Publish to\nArcGIS");
      ribbonSpecs.push_back({w, w, [&, w]() {
        RibbonSectionBegin("RibbonSecOutPublish", "Publish", w, panelH);
        nyiLarge("##OutPublishSurf", "c3d_publishsurf", "Publish\nSurfaces");
        ImGui::SameLine(0, 4);
        nyiLarge("##OutPublishGis", "c3d_publishgis", "Publish to\nArcGIS");
        RibbonSectionEnd();
      }, "Publish", RibbonIconKind::Nyi, "c3d_publishgis"});
    }

    // ---- Export to DWF/PDF -----------------------------------------
    {
      const float w = 8.f + belowW("Export") + 4.f + annStyleW;
      ribbonSpecs.push_back({w, w, [&, w]() {
        RibbonSectionBegin("RibbonSecOutDwfPdf", "Export to DWF/PDF", w, panelH);
        nyiLarge("##OutDwfxExport", "c3d_dwfx", "Export");
        ImGui::SameLine(0, 4);
        ImGui::BeginGroup();
        ImGui::TextUnformatted("Export");
        annNyiCombo("##OutDwfExportArea", "Display");
        ImGui::TextUnformatted("Page Setup");
        annNyiCombo("##OutDwfPageSetup", "Current");
        ImGui::EndGroup();
        RibbonSectionEnd();
      }, "Export to DWF/PDF", RibbonIconKind::Nyi, "c3d_dwfx"});
    }
  } // if (activeRibbonTab == kRibbonTabOutput)

  // REQ-302 increment 2 (ADR-038 (a)): decide breakpoint from the width RibbonToolsLeft and
  // RibbonLayerStrip already compete for today (RibbonLayerStrip is the fixed-width sibling placed
  // right after RibbonToolsLeft's own EndChild via SameLine — see below) — measured here, before
  // RibbonToolsLeft's BeginChild needs a size.
  const float availForTools = std::max(largeW, ImGui::GetContentRegionAvail().x - st.ItemSpacing.x - kLayerPanelW);
  const RibbonFitResult ribbonFit = DecideRibbonFit(ribbonSpecs, availForTools, secGap);
  const float ribbonToolsW = ribbonSpecs.empty() ? largeW : ribbonFit.width;

  // Raised "panel tray" behind the button row so the ribbon reads as a distinct surface floating
  // on the gradient band, not painted flat onto it. Spans the full panel row (tools + layer strip)
  // out to the band's right edge.
  {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const float trayR = 6.f;
    const float trayX1 = ImGui::GetWindowPos().x + ImGui::GetWindowSize().x - st.WindowPadding.x;
    const ImVec2 a(p0.x - 3.f, p0.y - 3.f);
    const ImVec2 b(trayX1, p0.y + panelH + 3.f);
    // Drop shadow under the tray.
    dl->AddRectFilled(ImVec2(a.x + 3.f, b.y), ImVec2(b.x + 3.f, b.y + 4.f), HexU32(0x1C1C1C), trayR);
    // Raised face: lighter, top-lit.
    dl->AddRectFilledMultiColor(a, b, HexU32(0x4C4C4C), HexU32(0x4C4C4C), HexU32(0x363636),
                                HexU32(0x363636));
    // Inner top highlight + outer border for a crisp bevel.
    dl->AddLine(ImVec2(a.x + trayR, a.y + 1.f), ImVec2(b.x - trayR, a.y + 1.f), HexU32(0x606060), 1.f);
    dl->AddRect(a, b, HexU32(0x1F1F1F), trayR, 0, 1.5f);
  }
  ImGui::PushStyleColor(ImGuiCol_ChildBg, 0);
  ImGui::BeginChild("RibbonToolsLeft", ImVec2(ribbonToolsW, panelH), false,
                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  ImGui::PopStyleColor();
  ImGui::SetWindowFontScale(g_chrome.ribbonBodyFontScale);  // child keeps its own scale — see RibbonSectionBegin

  RenderRibbonFit(ribbonSpecs, ribbonFit, secGap, colH, curCompact, "RibbonMorePopup");

  // Contextual "PDF Underlay" section — shown when a PDF attachment is selected (REQ-302: renders
  // on every tab, unchanged — see REQ-302 acceptance, "contextual sections... render on every tab")
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

  // Contextual "Hatch" section (ADR-019). Shown while the HATCH command is active (editing the creation
  // defaults) OR when filled-region hatch(es) are selected — then the controls edit the selected object(s)'
  // pattern / color / transparency / layer / angle / scale live and undoably (REQ-042/043).
  std::vector<int> hatchSel;
  for (const auto& e : cmd.selection)
    if (e.type == SelectedEntity::Type::FilledRegion && e.index >= 0 &&
        static_cast<size_t>(e.index) < cmd.cadFilledRegions.size())
      hatchSel.push_back(e.index);
  const bool hatchEditing = !hatchSel.empty();
  if (cmd.active == AppCommandState::Kind::Hatch || hatchEditing) {
    ImGui::SameLine(0, 8);
    const float wHatchCtx = 360.f;
    RibbonSectionBegin("RibbonSecHatchCtx", hatchEditing ? "Hatch (selected)" : "Hatch", wHatchCtx, panelH);
    {
      const std::vector<hatchpat::Def>& lib = HatchLibrary();

      // Working values shown in the widgets: seeded from the first selected hatch when editing, else from
      // the creation defaults on AppCommandState.
      std::string wPattern = cmd.hatchPatternName;
      float wRgb[3] = {cmd.hatchColorRgb[0], cmd.hatchColorRgb[1], cmd.hatchColorRgb[2]};
      float wTrans = cmd.hatchTransparency01;
      std::string wLayer = cmd.hatchLayer.empty() ? cmd.currentLayer : cmd.hatchLayer;
      float wAngle = cmd.hatchAngleDeg;
      float wScale = cmd.hatchScale;
      if (hatchEditing) {
        const CadFilledRegion& fr0 = cmd.cadFilledRegions[static_cast<size_t>(hatchSel[0])];
        wPattern = fr0.patternName;
        wAngle = fr0.patternAngleDeg;
        wScale = fr0.patternScale;
        if (static_cast<size_t>(hatchSel[0]) < cmd.cadFilledRegionAttrs.size()) {
          const EntityAttributes& a0 = cmd.cadFilledRegionAttrs[static_cast<size_t>(hatchSel[0])];
          const CadLayerRow* lr = FindDrawingLayerRowCi(cmd, a0.layer);
          float rgba[4] = {0.78f, 0.78f, 0.78f, 1.f};
          ResolveEntityRgbaForViewport(a0, lr, 0.78f, 0.78f, 0.78f, rgba);
          wRgb[0] = rgba[0]; wRgb[1] = rgba[1]; wRgb[2] = rgba[2];
          wTrans = a0.transparency < 0.f ? 0.f : a0.transparency;
          if (!a0.layer.empty()) wLayer = a0.layer;
        }
      }
      const std::string curPat = wPattern.empty() ? std::string("SOLID") : wPattern;

      // Push one undo snapshot at the start of an edit interaction (called on widget grab / discrete change).
      auto snapEdit = [&]() { if (hatchEditing) PushUndoSnapshot(cmd, "Edit hatch"); };
      auto setPattern = [&](const std::string& p) {
        if (hatchEditing) { snapEdit(); for (int i : hatchSel) cmd.cadFilledRegions[static_cast<size_t>(i)].patternName = p; BumpCadGpuCache(cmd); }
        else cmd.hatchPatternName = p;
      };
      auto setColor = [&](const float c[3]) {
        if (hatchEditing) {
          char hex[8];
          std::snprintf(hex, sizeof(hex), "#%02X%02X%02X", static_cast<int>(std::lround(c[0] * 255.f)),
                        static_cast<int>(std::lround(c[1] * 255.f)), static_cast<int>(std::lround(c[2] * 255.f)));
          for (int i : hatchSel) if (static_cast<size_t>(i) < cmd.cadFilledRegionAttrs.size()) cmd.cadFilledRegionAttrs[static_cast<size_t>(i)].color = hex;
          BumpCadGpuCache(cmd);
        } else { cmd.hatchColorRgb[0] = c[0]; cmd.hatchColorRgb[1] = c[1]; cmd.hatchColorRgb[2] = c[2]; }
      };
      auto setTrans = [&](float t) {
        if (hatchEditing) { for (int i : hatchSel) if (static_cast<size_t>(i) < cmd.cadFilledRegionAttrs.size()) cmd.cadFilledRegionAttrs[static_cast<size_t>(i)].transparency = t; BumpCadGpuCache(cmd); }
        else cmd.hatchTransparency01 = t;
      };
      auto setLayer = [&](const std::string& ln) {
        if (hatchEditing) { snapEdit(); for (int i : hatchSel) if (static_cast<size_t>(i) < cmd.cadFilledRegionAttrs.size()) cmd.cadFilledRegionAttrs[static_cast<size_t>(i)].layer = ln; BumpCadGpuCache(cmd); }
        else cmd.hatchLayer = ln;
      };
      auto setAngle = [&](float v) {
        if (hatchEditing) { for (int i : hatchSel) cmd.cadFilledRegions[static_cast<size_t>(i)].patternAngleDeg = v; BumpCadGpuCache(cmd); }
        else cmd.hatchAngleDeg = v;
      };
      auto setScale = [&](float v) {
        v = std::max(0.01f, v);
        if (hatchEditing) { for (int i : hatchSel) cmd.cadFilledRegions[static_cast<size_t>(i)].patternScale = v; BumpCadGpuCache(cmd); }
        else cmd.hatchScale = v;
      };

      static hatchpat::Def s_solidDef = [] { hatchpat::Def d; d.name = "SOLID"; return d; }();
      const ImU32 swInk = IM_COL32(static_cast<int>(wRgb[0] * 255.f), static_cast<int>(wRgb[1] * 255.f),
                                   static_cast<int>(wRgb[2] * 255.f), 255);
      ImDrawList* tdl = ImGui::GetWindowDrawList();
      ImGui::BeginGroup();
      ImGui::TextDisabled("Pattern");
      // The current-pattern swatch is a button that opens a thumbnail palette of every pattern.
      const ImVec2 p0 = ImGui::GetCursorScreenPos();
      const float sz = 34.f;
      const bool openPalette = ImGui::InvisibleButton("##hatchPatBtn", ImVec2(sz, sz));
      const hatchpat::Def* curDef = hatchpat::Find(lib, curPat);
      tdl->AddRectFilled(p0, ImVec2(p0.x + sz, p0.y + sz),
                         ImGui::IsItemHovered() ? IM_COL32(55, 66, 82, 255) : IM_COL32(38, 38, 42, 255), 2.f);
      DrawHatchThumbnail(tdl, p0, ImVec2(p0.x + sz, p0.y + sz), curDef ? *curDef : s_solidDef, swInk);
      tdl->AddRect(p0, ImVec2(p0.x + sz, p0.y + sz), IM_COL32(120, 120, 120, 255), 2.f, 0, 1.f);
      if (openPalette)
        ImGui::OpenPopup("HatchPatPalette");
      ImGui::SameLine(0, 5);
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted(curPat.c_str());
      ImGui::EndGroup();

      if (ImGui::BeginPopup("HatchPatPalette")) {
        ImGui::TextDisabled("Hatch pattern — click to choose");
        ImGui::Separator();
        constexpr int kCols = 6;
        constexpr float kCell = 50.f;
        ImGui::BeginChild("##hpgrid", ImVec2(kCols * (kCell + 4.f) + 8.f, 320.f), false);
        ImDrawList* gdl = ImGui::GetWindowDrawList();
        // SOLID first, then every parsed pattern.
        std::vector<std::pair<std::string, const hatchpat::Def*>> entries;
        entries.emplace_back("SOLID", &s_solidDef);
        for (const hatchpat::Def& d : lib)
          if (d.name != "SOLID")
            entries.emplace_back(d.name, &d);
        for (size_t i = 0; i < entries.size(); ++i) {
          if (i % kCols != 0)
            ImGui::SameLine(0, 4);
          const std::string& nm = entries[i].first;
          const hatchpat::Def* def = entries[i].second;
          const ImVec2 c0 = ImGui::GetCursorScreenPos();
          const bool clicked = ImGui::InvisibleButton((std::string("##hp_") + nm).c_str(), ImVec2(kCell, kCell));
          const bool hov = ImGui::IsItemHovered();
          const bool selected = (curPat == nm);
          gdl->AddRectFilled(c0, ImVec2(c0.x + kCell, c0.y + kCell),
                             hov ? IM_COL32(55, 66, 82, 255) : IM_COL32(38, 38, 42, 255), 3.f);
          DrawHatchThumbnail(gdl, c0, ImVec2(c0.x + kCell, c0.y + kCell), *def, IM_COL32(225, 225, 225, 255));
          gdl->AddRect(c0, ImVec2(c0.x + kCell, c0.y + kCell),
                       selected ? IM_COL32(86, 156, 214, 255) : IM_COL32(90, 90, 95, 255), 3.f, 0,
                       selected ? 2.f : 1.f);
          if (hov) {
            if (def && !def->description.empty())
              ImGui::SetTooltip("%s — %s", nm.c_str(), def->description.c_str());
            else
              ImGui::SetTooltip("%s", nm.c_str());
          }
          if (clicked) {
            setPattern(nm);
            ImGui::CloseCurrentPopup();
          }
        }
        ImGui::EndChild();
        ImGui::EndPopup();
      }
      ImGui::SameLine(0, 8);

      ImGui::BeginGroup();
      ImGui::SetNextItemWidth(150.f);
      ImGui::ColorEdit3("Color##hatch", wRgb, ImGuiColorEditFlags_NoInputs);
      if (ImGui::IsItemActivated()) snapEdit();
      if (ImGui::IsItemEdited()) setColor(wRgb);
      float transPct = wTrans * 100.f;
      ImGui::SetNextItemWidth(150.f);
      const bool transChanged = ImGui::SliderFloat("Transp##hatch", &transPct, 0.f, 90.f, "%.0f%%");
      if (ImGui::IsItemActivated()) snapEdit();
      if (transChanged) setTrans(transPct / 100.f);
      ImGui::SetNextItemWidth(150.f);
      {
        std::vector<std::string> layerList;
        CollectAllDrawingLayers(cmd, &layerList);
        if (ImGui::BeginCombo("Layer##hatch", wLayer.c_str())) {
          for (const std::string& ln : layerList)
            if (ImGui::Selectable(ln.c_str(), ln == wLayer))
              setLayer(ln);
          ImGui::EndCombo();
        }
      }
      ImGui::EndGroup();
      ImGui::SameLine(0, 8);
      ImGui::BeginGroup();
      ImGui::SetNextItemWidth(90.f);
      const bool angChanged = ImGui::InputFloat("Angle##hatch", &wAngle, 0.f, 0.f, "%.0f\xC2\xB0");
      if (ImGui::IsItemActivated()) snapEdit();
      if (angChanged) setAngle(wAngle);
      ImGui::SetNextItemWidth(90.f);
      const bool scChanged = ImGui::InputFloat("Scale##hatch", &wScale, 0.f, 0.f, "%.2f");
      if (ImGui::IsItemActivated()) snapEdit();
      if (scChanged) setScale(wScale);
      ImGui::EndGroup();
    }
    RibbonSectionEnd();
  }

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

  // The ribbon is the topmost plate in the shell: light along its top edge, a
  // hard dark rule along its bottom. The shadow it casts is drawn by the panels
  // BELOW it (CastShadowInto) — see that function for why it cannot be drawn here.
  {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 mn = ImGui::GetWindowPos();
    const ImVec2 mx(mn.x + ImGui::GetWindowSize().x, mn.y + ImGui::GetWindowSize().y);
    PlateTopHilite(dl, mn, mx);
    if ((g_chrome.plateShadow >> IM_COL32_A_SHIFT) != 0)
      dl->AddLine(ImVec2(mn.x, mx.y - 0.5f), ImVec2(mx.x, mx.y - 0.5f), g_chrome.bandShadow, 1.f);
  }

  ImGui::EndChild();
  ImGui::SetWindowFontScale(1.f);
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

const EntityAttributes& TableAttr(const AppCommandState& cmd, int idx) {
  static const EntityAttributes kDef{};
  if (idx < 0)
    return kDef;
  const size_t u = static_cast<size_t>(idx);
  if (u >= cmd.cadTableAttrs.size())
    return kDef;
  return cmd.cadTableAttrs[u];
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
  const size_t k = static_cast<size_t>(idx) * 4;
  if (k + 3 >= cmd.userCirclesCxCyZR.size())
    return false;
  *cx = cmd.userCirclesCxCyZR[k];
  *cy = cmd.userCirclesCxCyZR[k + 1];
  *r = cmd.userCirclesCxCyZR[k + 3];
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
      const size_t k = static_cast<size_t>(e.index) * 4;
      if (k + 3 >= cmd.userCirclesCxCyZR.size())
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
    } else if (e.type == SelectedEntity::Type::Table) {
      if (e.index < 0 || static_cast<size_t>(e.index) >= cmd.cadTables.size())
        continue;
      const EntityAttributes& a = TableAttr(cmd, e.index);
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


// Font choices offered by the STYLE dialog and the MTEXT "Text Formatting" panel's font picker
// (REQ-044 / REQ-051). "" = the application default font. Defined here so both use sites see it.
static const char* kTextStyleFonts[] = {
    "romans.shx", "romand.shx", "romanc.shx", "txt.shx",         "simplex.shx",
    "isocp.shx", "italic.shx", "Arial",      "Times New Roman", "Consolas",   "Tahoma",
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
      const size_t k = static_cast<size_t>(e.index) * 4;
      if (k + 3 >= cmd.userCirclesCxCyZR.size() || static_cast<size_t>(e.index) >= cmd.userCircleAttrs.size())
        continue;
      cmd.userCircleAttrs[static_cast<size_t>(e.index)].layer = v;
    } else if (e.type == SelectedEntity::Type::Annotation) {
      if (e.index < 0 || static_cast<size_t>(e.index) >= cmd.cadAnnotations.size() ||
          static_cast<size_t>(e.index) >= cmd.cadAnnotationAttrs.size())
        continue;
      cmd.cadAnnotationAttrs[static_cast<size_t>(e.index)].layer = v;
    } else if (e.type == SelectedEntity::Type::Table) {
      if (e.index < 0 || static_cast<size_t>(e.index) >= cmd.cadTables.size() ||
          static_cast<size_t>(e.index) >= cmd.cadTableAttrs.size())
        continue;
      cmd.cadTableAttrs[static_cast<size_t>(e.index)].layer = v;
    } else if (e.type == SelectedEntity::Type::BlockRef) {
      if (e.index < 0 || static_cast<size_t>(e.index) >= cmd.cadBlockRefs.size() ||
          static_cast<size_t>(e.index) >= cmd.cadBlockRefAttrs.size())
        continue;
      cmd.cadBlockRefAttrs[static_cast<size_t>(e.index)].layer = v;
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
      const size_t k = static_cast<size_t>(e.index) * 4;
      if (k + 3 >= cmd.userCirclesCxCyZR.size() || static_cast<size_t>(e.index) >= cmd.userCircleAttrs.size())
        continue;
      cmd.userCircleAttrs[static_cast<size_t>(e.index)].color = v;
    } else if (e.type == SelectedEntity::Type::Annotation) {
      if (e.index < 0 || static_cast<size_t>(e.index) >= cmd.cadAnnotations.size() ||
          static_cast<size_t>(e.index) >= cmd.cadAnnotationAttrs.size())
        continue;
      cmd.cadAnnotationAttrs[static_cast<size_t>(e.index)].color = v;
    } else if (e.type == SelectedEntity::Type::Table) {
      if (e.index < 0 || static_cast<size_t>(e.index) >= cmd.cadTables.size() ||
          static_cast<size_t>(e.index) >= cmd.cadTableAttrs.size())
        continue;
      cmd.cadTableAttrs[static_cast<size_t>(e.index)].color = v;
    } else if (e.type == SelectedEntity::Type::BlockRef) {
      if (e.index < 0 || static_cast<size_t>(e.index) >= cmd.cadBlockRefs.size() ||
          static_cast<size_t>(e.index) >= cmd.cadBlockRefAttrs.size())
        continue;
      cmd.cadBlockRefAttrs[static_cast<size_t>(e.index)].color = v;
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
      const size_t k = static_cast<size_t>(e.index) * 4;
      if (k + 3 >= cmd.userCirclesCxCyZR.size() || static_cast<size_t>(e.index) >= cmd.userCircleAttrs.size())
        continue;
      cmd.userCircleAttrs[static_cast<size_t>(e.index)].linetype = v;
    } else if (e.type == SelectedEntity::Type::Annotation) {
      if (e.index < 0 || static_cast<size_t>(e.index) >= cmd.cadAnnotations.size() ||
          static_cast<size_t>(e.index) >= cmd.cadAnnotationAttrs.size())
        continue;
      cmd.cadAnnotationAttrs[static_cast<size_t>(e.index)].linetype = v;
    } else if (e.type == SelectedEntity::Type::Table) {
      if (e.index < 0 || static_cast<size_t>(e.index) >= cmd.cadTables.size() ||
          static_cast<size_t>(e.index) >= cmd.cadTableAttrs.size())
        continue;
      cmd.cadTableAttrs[static_cast<size_t>(e.index)].linetype = v;
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
      const size_t k = static_cast<size_t>(e.index) * 4;
      if (k + 3 >= cmd.userCirclesCxCyZR.size() || static_cast<size_t>(e.index) >= cmd.userCircleAttrs.size())
        continue;
      cmd.userCircleAttrs[static_cast<size_t>(e.index)].lineweightMm = stored;
    } else if (e.type == SelectedEntity::Type::Annotation) {
      if (e.index < 0 || static_cast<size_t>(e.index) >= cmd.cadAnnotations.size() ||
          static_cast<size_t>(e.index) >= cmd.cadAnnotationAttrs.size())
        continue;
      cmd.cadAnnotationAttrs[static_cast<size_t>(e.index)].lineweightMm = stored;
    } else if (e.type == SelectedEntity::Type::Table) {
      if (e.index < 0 || static_cast<size_t>(e.index) >= cmd.cadTables.size() ||
          static_cast<size_t>(e.index) >= cmd.cadTableAttrs.size())
        continue;
      cmd.cadTableAttrs[static_cast<size_t>(e.index)].lineweightMm = stored;
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
      const size_t k = static_cast<size_t>(e.index) * 4;
      if (k + 3 >= cmd.userCirclesCxCyZR.size() || static_cast<size_t>(e.index) >= cmd.userCircleAttrs.size())
        continue;
      cmd.userCircleAttrs[static_cast<size_t>(e.index)].transparency = stored;
    } else if (e.type == SelectedEntity::Type::Annotation) {
      if (e.index < 0 || static_cast<size_t>(e.index) >= cmd.cadAnnotations.size() ||
          static_cast<size_t>(e.index) >= cmd.cadAnnotationAttrs.size())
        continue;
      cmd.cadAnnotationAttrs[static_cast<size_t>(e.index)].transparency = stored;
    } else if (e.type == SelectedEntity::Type::Table) {
      if (e.index < 0 || static_cast<size_t>(e.index) >= cmd.cadTables.size() ||
          static_cast<size_t>(e.index) >= cmd.cadTableAttrs.size())
        continue;
      cmd.cadTableAttrs[static_cast<size_t>(e.index)].transparency = stored;
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

    // Current text style for new TEXT/MTEXT (REQ-044). The full STYLE management dialog lands in Phase 2.
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("Current text style");
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-1);
    {
      const TextStyle* active = ActiveTextStyle(cmd);
      const char* preview = active ? active->name.c_str() : "Standard";
      if (ImGui::BeginCombo("##curtextstyle", preview)) {
        for (const TextStyle& s : cmd.textStyles) {
          const bool sel = (s.name == cmd.activeTextStyleName);
          if (ImGui::Selectable(s.name.c_str(), sel))
            SetActiveTextStyle(cmd, s.name);
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

// One editable coordinate row in the model-space Properties panel.
//
// Undo is taken on **activation** (the frame the field gains focus), not on commit: by the time
// ImGui reports IsItemDeactivatedAfterEdit the value has already been overwritten, so a snapshot
// there would record the NEW value and Ctrl+Z would be a no-op. Snapshotting on activation is what
// makes REQ-057's "edit Z, then Ctrl+Z restores it" actually hold — and it closes the same gap for
// the X/Y/radius rows, which were never undoable before this.
// REQ-081: which axis a coordinate row edits, read off the end of its label
// ("Start X" → 'X', "Center Z" → 'Z', "Radius" → 0). ASSUMPTION-1 in TASK-058:
// every axis row in this file is labelled that way, and a row that is not simply
// gets no badge.
static char PropRowAxis(const char* label) {
  const size_t n = label ? std::strlen(label) : 0;
  if (n < 2 || label[n - 2] != ' ')
    return 0;
  const char c = label[n - 1];
  return (c == 'X' || c == 'Y' || c == 'Z') ? c : 0;
}

static void PropGeomRow(AppCommandState& cmd, const char* label, const char* id, float* v,
                        const char* fmt, const char* undoLabel) {
  ImGui::TableNextRow();
  ImGui::TableNextColumn();
  ImGui::TextUnformatted(label);
  ImGui::TableNextColumn();

  // A colored X/Y/Z chip left of the field, as in the reference. It is decoration
  // only — the field keeps the whole remaining width and behaves exactly as before.
  const char axis = g_chrome.axisBadges ? PropRowAxis(label) : 0;
  if (axis) {
    const float h = ImGui::GetFrameHeight();
    const float w = std::max(h * 0.72f, ImGui::CalcTextSize("X").x + 8.f);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImU32 fill = (axis == 'X') ? g_chrome.axisX : (axis == 'Y') ? g_chrome.axisY : g_chrome.axisZ;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), fill, ImGui::GetStyle().FrameRounding);
    const char letter[2] = {axis, '\0'};
    const ImVec2 ts = ImGui::CalcTextSize(letter);
    dl->AddText(ImVec2(p.x + (w - ts.x) * 0.5f, p.y + (h - ts.y) * 0.5f), g_chrome.axisText, letter);
    ImGui::Dummy(ImVec2(w, h));
    ImGui::SameLine(0.f, 4.f);
  }

  ImGui::SetNextItemWidth(-1);
  ImGui::InputFloat(id, v, 0.f, 0.f, fmt);
  if (ImGui::IsItemActivated())
    PushUndoSnapshot(cmd, undoLabel);
  if (ImGui::IsItemDeactivatedAfterEdit())
    BumpCadGpuCache(cmd);
}

void DrawSingleLineGeometryEditable(AppCommandState& cmd, int lineIdx) {
  if (!PropSectionHeader("Geometry"))
    return;
  const size_t k = static_cast<size_t>(lineIdx) * 6;
  if (k + 5 >= cmd.userLinesFlat.size())
    return;
  float* x0 = &cmd.userLinesFlat[k];
  float* y0 = &cmd.userLinesFlat[k + 1];
  float* z0 = &cmd.userLinesFlat[k + 2];
  float* x1 = &cmd.userLinesFlat[k + 3];
  float* y1 = &cmd.userLinesFlat[k + 4];
  float* z1 = &cmd.userLinesFlat[k + 5];
  const std::string cfmt = DisplayFloatFmt(cmd.displayLinearPrecision);

  if (ImGui::BeginTable("props_geom_line_ed", 2, kPropTableFlags)) {
    ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthStretch, 0.38f);
    ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch, 0.62f);

    // Per-endpoint Z (REQ-057): a line may be genuinely sloped, so each end carries its own.
    PropGeomRow(cmd, "Start X", "##lsx", x0, cfmt.c_str(), "Edit line X");
    PropGeomRow(cmd, "Start Y", "##lsy", y0, cfmt.c_str(), "Edit line Y");
    PropGeomRow(cmd, "Start Z", "##lsz", z0, cfmt.c_str(), "Edit line Z");
    PropGeomRow(cmd, "End X", "##lex", x1, cfmt.c_str(), "Edit line X");
    PropGeomRow(cmd, "End Y", "##ley", y1, cfmt.c_str(), "Edit line Y");
    PropGeomRow(cmd, "End Z", "##lez", z1, cfmt.c_str(), "Edit line Z");

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
    // "Length" keeps its established meaning — HORIZONTAL distance — because changing it would
    // silently alter a shipped readout that survey work depends on. The slope distance and grade
    // appear only when the line actually has rise, so a flat drawing looks exactly as before.
    PropRow("Length", lenStr.c_str());
    const float dz = *z1 - *z0;
    if (dz != 0.f) {
      const float slope = std::sqrt(dx * dx + dy * dy + dz * dz);
      PropRow("Length (slope)", FormatLinear(static_cast<double>(slope), cmd.displayLinearPrecision).c_str());
      PropRow("Rise", FormatLinear(static_cast<double>(dz), cmd.displayLinearPrecision).c_str());
      if (len > 1e-6f) {
        char gradeBuf[64];
        std::snprintf(gradeBuf, sizeof(gradeBuf), "%.2f%%", static_cast<double>(dz / len) * 100.0);
        PropRow("Grade", gradeBuf);
      }
    }
    PropRow("Rotation rel. north", bearStr.c_str());
    ImGui::EndTable();
  }
}

void DrawSingleCircleGeometryEditable(AppCommandState& cmd, int circleIdx) {
  if (!PropSectionHeader("Geometry"))
    return;
  const size_t k = static_cast<size_t>(circleIdx) * 4;
  if (k + 3 >= cmd.userCirclesCxCyZR.size())
    return;
  float* cx = &cmd.userCirclesCxCyZR[k];
  float* cy = &cmd.userCirclesCxCyZR[k + 1];
  float* cz = &cmd.userCirclesCxCyZR[k + 2];
  float* r = &cmd.userCirclesCxCyZR[k + 3];
  const std::string cfmt = DisplayFloatFmt(cmd.displayLinearPrecision);

  if (ImGui::BeginTable("props_geom_circ_ed", 2, kPropTableFlags)) {
    ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthStretch, 0.38f);
    ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch, 0.62f);

    PropGeomRow(cmd, "Center X", "##cx", cx, cfmt.c_str(), "Edit circle X");
    PropGeomRow(cmd, "Center Y", "##cy", cy, cfmt.c_str(), "Edit circle Y");
    PropGeomRow(cmd, "Center Z", "##cz", cz, cfmt.c_str(), "Edit circle Z");
    PropGeomRow(cmd, "Radius", "##cr", r, cfmt.c_str(), "Edit circle radius");
    if (*r < 1e-6f)
      *r = 1e-6f;

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
      : ann.kind == CadAnnotation::Kind::Table   ? "TABLE"
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
    if (ImGui::IsItemActivated())
      PushUndoSnapshot(cmd, "Edit text X");
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
    if (ImGui::IsItemActivated())
      PushUndoSnapshot(cmd, "Edit text Y");
    if (ImGui::IsItemDeactivatedAfterEdit()) {
      if (ann.kind == CadAnnotation::Kind::Mtext) {
        const float dy = ann.insY - ann.boxMinY;
        ann.boxMinY += dy;
        ann.boxMaxY += dy;
      }
      BumpCadGpuCache(cmd);
    }

    // Insertion Z (REQ-057). Unlike X/Y this needs no MTEXT box sync — the box is a 2D extent in
    // the text's own plane, so raising the text carries the box with it implicitly.
    PropGeomRow(cmd, "Insertion Z", "##ainsz", &ann.insZ, cfmt.c_str(), "Edit text Z");

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
      // Bearing convention (clockwise from north): 0 = north (text runs up), 90 = east (left-to-right).
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

void DrawSingleTableGeometryEditable(AppCommandState& cmd, int tableIdx) {
  if (!PropSectionHeader("Geometry"))
    return;
  if (tableIdx < 0 || static_cast<size_t>(tableIdx) >= cmd.cadTables.size())
    return;
  CadTable& t = cmd.cadTables[static_cast<size_t>(tableIdx)];
  const std::string cfmt = DisplayFloatFmt(cmd.displayLinearPrecision);
  if (ImGui::BeginTable("props_geom_tbl_ed", 2, kPropTableFlags)) {
    ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthStretch, 0.38f);
    ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch, 0.62f);
    PropGeomRow(cmd, "Insertion X", "##tblinsx", &t.insX, cfmt.c_str(), "Edit table X");
    PropGeomRow(cmd, "Insertion Y", "##tblinsy", &t.insY, cfmt.c_str(), "Edit table Y");
    PropGeomRow(cmd, "Insertion Z", "##tblinsz", &t.insZ, cfmt.c_str(), "Edit table Z");
    PropGeomRow(cmd, "Width", "##tblw", &t.width, cfmt.c_str(), "Edit table width");
    if (t.width < 1.e-3f)
      t.width = 1.e-3f;
    PropGeomRow(cmd, "Height", "##tblh", &t.height, cfmt.c_str(), "Edit table height");
    if (t.height < 1.e-3f)
      t.height = 1.e-3f;
    float rotDeg = t.rotationRad * 180.f / 3.14159265358979323846f;
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("Rotation °");
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputFloat("##tblrot", &rotDeg, 0.f, 0.f, "%.2f");
    if (ImGui::IsItemActivated())
      PushUndoSnapshot(cmd, "Edit table rotation");
    if (ImGui::IsItemDeactivatedAfterEdit()) {
      t.rotationRad = rotDeg * 3.14159265358979323846f / 180.f;
      BumpCadGpuCache(cmd);
    }
    ImGui::EndTable();
  }
  ImGui::TextDisabled("%d rows x %d columns", CadTableRowCount(t), std::max(t.cols, 0));
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
                    : a.kind == CadAnnotation::Kind::Table    ? "TABLE"
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

/// Screen-space context for picking a survey point in an orbited view (REQ-058).
///
/// In **plan** view a point's screen position depends only on its easting/northing, so the plain XY
/// test below is exact — and it is kept, unchanged, for parity with the pre-3D behaviour.
///
/// Once the view tilts this stops being true. The marker is DRAWN at the point's own elevation
/// (REQ-057), so it moves on screen, while the cursor's world position is its ray hit on the work
/// plane at Z = 0. An XY test then picks the point whose *plan* position lies under the cursor,
/// which is not the point the user can see there — on a 35 ft-relief surface that is hundreds of
/// pixels away. Supplying this makes the test agree with what is on screen, which is the only
/// definition of "the point under my cursor" that means anything.
struct SurveyPickScreen {
  const Camera* cam = nullptr;  ///< null = plan view → keep the pre-3D XY test
  float viewW = 0.f;
  float viewH = 0.f;
  float cursorX = 0.f;  ///< cursor in the same space Camera::WorldToScreen produces
  float cursorY = 0.f;
};

int PickSurveyPointIndex(const std::vector<SurveyPoint>& pts, double wx, double wy, float surveyCrossHalfWorld,
                         float viewportHeightPx, float orthoHalfHeightWorld, float viewportPickAperturePx,
                         const SurveyPickScreen* screen = nullptr) {
  if (pts.empty())
    return -1;
  const float arm = std::max(surveyCrossHalfWorld, 1.e-8f);

  if (screen && screen->cam) {
    // Same rule as the world-space path — the larger of the marker's own half-extent and the pick
    // aperture, times the same 1.38 — but measured in pixels, because that is the space the marker
    // is actually drawn in once the view is orbited.
    const float worldPerPx = (2.f * orthoHalfHeightWorld) / std::max(viewportHeightPx, 1.f);
    const float armPx = arm / std::max(worldPerPx, 1.e-6f);
    const float radPx = std::max(armPx, viewportPickAperturePx) * 1.38f;
    const float r2px = radPx * radPx;
    int best = -1;
    float bestD2 = 0.f;
    for (size_t i = 0; i < pts.size(); ++i) {
      float sx = 0.f, sy = 0.f;
      // The point's OWN elevation: this is the whole fix. Projecting at Z = 0 would reproduce the
      // plan test with extra steps.
      screen->cam->WorldToScreen(static_cast<double>(pts[i].easting), static_cast<double>(pts[i].northing),
                                 static_cast<double>(pts[i].elevation), screen->viewW, screen->viewH, &sx, &sy);
      const float dx = sx - screen->cursorX;
      const float dy = sy - screen->cursorY;
      const float d2 = dx * dx + dy * dy;
      if (d2 <= r2px && (best < 0 || d2 < bestD2)) {
        bestD2 = d2;
        best = static_cast<int>(i);
      }
    }
    return best;
  }

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

/// Pick the survey point under the cursor, choosing the screen-space test automatically when the
/// view is not plan.
///
/// Exists so hover and both click paths make the **same** decision. They previously each called the
/// picker directly, and any site left on the plan-only test would highlight one point while another
/// got selected — the two disagreeing is a worse bug than either being wrong consistently.
int PickSurveyPointAtCursor(const AppCommandState& cmd, double wx, double wy, float surveyCrossHalfWorld,
                            float viewW, float viewH, float orthoHalfHeightWorld, float cursorScreenX,
                            float cursorScreenY) {
  const Camera cam = CadViewCamera(cmd);
  SurveyPickScreen s;
  const bool plan = CadViewIsPlan(cmd);
  if (!plan) {
    s.cam = &cam;
    s.viewW = viewW;
    s.viewH = viewH;
    s.cursorX = cursorScreenX;
    s.cursorY = cursorScreenY;
  }
  return PickSurveyPointIndex(cmd.surveyPoints, wx, wy, surveyCrossHalfWorld, viewH, orthoHalfHeightWorld,
                              cmd.objectSnapAperturePx, plan ? nullptr : &s);
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
      const int labelAnnIx = FindSurveyLabelAnnIndex(cmd, p);
      if (labelAnnIx >= 0 && static_cast<size_t>(labelAnnIx) < cmd.cadAnnotationAttrs.size()) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Label color");
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
        EntityAttributes& lattr = cmd.cadAnnotationAttrs[static_cast<size_t>(labelAnnIx)];
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

// REQ-039: Properties for native paper-space entities. These live in per-layout stores (paper inches), not in
// cmd.selection, so they get a dedicated panel branch rather than the model path. Shows General (Layer/Color
// applied to the whole paper selection) plus a per-type Geometry/Text editor for a single selected object.
static void DrawPaperEntityProps(AppCommandState& cmd) {
  if (cmd.activeSpaceIndex < 0 || static_cast<size_t>(cmd.activeSpaceIndex) >= cmd.paperLayouts.size())
    return;
  PaperLayout& L = cmd.paperLayouts[static_cast<size_t>(cmd.activeSpaceIndex)];
  auto& selp = cmd.selectedPaperEntities;
  using T = PaperEntityRef::Type;

  // Drop refs that no longer point at a live object (store edited/undone since selection).
  auto liveCount = [&](T t) -> int {
    switch (t) {
      case T::Line:     return static_cast<int>(L.paperLines.size() / 6);
      case T::Text:     return static_cast<int>(L.paperTexts.size());
      case T::Circle:   return static_cast<int>(L.paperCircles.size() / 3);
      case T::Arc:      return static_cast<int>(L.paperArcs.size());
      case T::Ellipse:  return static_cast<int>(L.paperEllipses.size());
      case T::Polyline: return static_cast<int>(L.paperPolyOffsets.size()) - 1;
      case T::Block:    return static_cast<int>(L.paperBlockRefs.size());
    }
    return 0;
  };
  selp.erase(std::remove_if(selp.begin(), selp.end(),
                            [&](const PaperEntityRef& r) { return r.index < 0 || r.index >= liveCount(r.type); }),
             selp.end());
  if (selp.empty())
    return;

  auto attrsOf = [&](const PaperEntityRef& r) -> EntityAttributes* {
    const size_t i = static_cast<size_t>(r.index);
    switch (r.type) {
      case T::Line:     return i < L.paperLineAttrs.size() ? &L.paperLineAttrs[i] : nullptr;
      case T::Text:     return i < L.paperTextAttrs.size() ? &L.paperTextAttrs[i] : nullptr;
      case T::Circle:   return i < L.paperCircleAttrs.size() ? &L.paperCircleAttrs[i] : nullptr;
      case T::Arc:      return i < L.paperArcAttrs.size() ? &L.paperArcAttrs[i] : nullptr;
      case T::Ellipse:  return i < L.paperEllAttrs.size() ? &L.paperEllAttrs[i] : nullptr;
      case T::Polyline: return i < L.paperPolyAttrs.size() ? &L.paperPolyAttrs[i] : nullptr;
      case T::Block:    return i < L.paperBlockRefAttrs.size() ? &L.paperBlockRefAttrs[i] : nullptr;
    }
    return nullptr;
  };

  int n[7] = {0, 0, 0, 0, 0, 0, 0};
  for (const auto& r : selp)
    ++n[static_cast<int>(r.type)];
  ImGui::Text("Selected: %d paper object(s)", static_cast<int>(selp.size()));
  const int kinds = (n[0] > 0) + (n[1] > 0) + (n[2] > 0) + (n[3] > 0) + (n[4] > 0) + (n[5] > 0) + (n[6] > 0);
  static const char* kPaperTypeName[7] = {"Line", "Text", "Circle", "Arc", "Ellipse", "Polyline", "Block"};
  if (kinds > 1)
    ImGui::TextDisabled("(Mixed: L %d, T %d, C %d, A %d, E %d, P %d)", n[0], n[1], n[2], n[3], n[4], n[5]);
  else if (selp.size() == 1)
    ImGui::TextDisabled("%s", kPaperTypeName[static_cast<int>(selp.front().type)]);
  else
    ImGui::TextDisabled("%d %ss", static_cast<int>(selp.size()), kPaperTypeName[static_cast<int>(selp.front().type)]);

  // Merged layer/color across the selection ("" = mixed).
  auto merged = [&](std::string EntityAttributes::*field) -> std::string {
    std::string v;
    bool first = true;
    for (const auto& r : selp) {
      const EntityAttributes* a = attrsOf(r);
      if (!a)
        continue;
      if (first) { v = a->*field; first = false; }
      else if (v != a->*field) return std::string();
    }
    return v;
  };

  // --- General: Layer + Color, applied to every selected paper entity ---
  if (PropSectionHeader("General") && ImGui::BeginTable("props_paper_gen", 2, kPropTableFlags)) {
    ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthStretch, 0.38f);
    ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch, 0.62f);

    ImGui::TableNextRow();
    ImGui::TableNextColumn(); ImGui::TextUnformatted("Layer");
    ImGui::TableNextColumn();
    {
      const std::string ml = merged(&EntityAttributes::layer);
      const char* prev = ml.empty() ? "(mixed)" : ml.c_str();
      std::vector<std::string> layerOpts;
      CollectAllDrawingLayers(cmd, &layerOpts);
      ImGui::SetNextItemWidth(-1);
      if (ImGui::BeginCombo("##paperlayer", prev)) {
        for (const auto& opt : layerOpts) {
          if (ImGui::Selectable(opt.c_str(), opt == ml)) {
            for (const auto& r : selp)
              if (EntityAttributes* a = attrsOf(r)) a->layer = opt;
            BumpCadGpuCache(cmd);
          }
        }
        ImGui::EndCombo();
      }
    }

    ImGui::TableNextRow();
    ImGui::TableNextColumn(); ImGui::TextUnformatted("Color");
    ImGui::TableNextColumn();
    {
      static const char* kColorOpts[] = {"ByLayer", "Red", "Yellow", "Green", "Cyan", "Blue", "Magenta", "White"};
      const std::string mc = merged(&EntityAttributes::color);
      const char* prev = mc.empty() ? "(mixed)" : mc.c_str();
      ImGui::SetNextItemWidth(-1);
      if (ImGui::BeginCombo("##papercolor", prev)) {
        for (const char* opt : kColorOpts) {
          if (ImGui::Selectable(opt, mc == opt)) {
            for (const auto& r : selp)
              if (EntityAttributes* a = attrsOf(r)) a->color = opt;
            BumpCadGpuCache(cmd);
          }
        }
        ImGui::EndCombo();
      }
    }
    ImGui::EndTable();
  }

  // --- Geometry / Text: only for a single selected object ---
  if (selp.size() != 1)
    return;
  const PaperEntityRef r = selp.front();
  const size_t i = static_cast<size_t>(r.index);
  auto bump = [&]() { if (ImGui::IsItemDeactivatedAfterEdit()) BumpCadGpuCache(cmd); };
  auto geomRow = [&](const char* label, float* v) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn(); ImGui::TextUnformatted(label);
    ImGui::TableNextColumn(); ImGui::SetNextItemWidth(-1);
    ImGui::InputFloat((std::string("##pg_") + label).c_str(), v, 0.f, 0.f, "%.4f");
    bump();
  };

  if (r.type == T::Line && i * 6 + 5 < L.paperLines.size()) {
    if (PropSectionHeader("Geometry") && ImGui::BeginTable("props_paper_line", 2, kPropTableFlags)) {
      ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthStretch, 0.38f);
      ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch, 0.62f);
      geomRow("Start X", &L.paperLines[i * 6]);
      geomRow("Start Y", &L.paperLines[i * 6 + 1]);
      geomRow("End X", &L.paperLines[i * 6 + 3]);
      geomRow("End Y", &L.paperLines[i * 6 + 4]);
      ImGui::EndTable();
    }
  } else if (r.type == T::Circle && i * 3 + 2 < L.paperCircles.size()) {
    if (PropSectionHeader("Geometry") && ImGui::BeginTable("props_paper_circ", 2, kPropTableFlags)) {
      ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthStretch, 0.38f);
      ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch, 0.62f);
      geomRow("Center X", &L.paperCircles[i * 3]);
      geomRow("Center Y", &L.paperCircles[i * 3 + 1]);
      geomRow("Radius", &L.paperCircles[i * 3 + 2]);
      ImGui::EndTable();
    }
  } else if (r.type == T::Arc && i < L.paperArcs.size()) {
    CadArc& a = L.paperArcs[i];
    if (PropSectionHeader("Geometry") && ImGui::BeginTable("props_paper_arc", 2, kPropTableFlags)) {
      ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthStretch, 0.38f);
      ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch, 0.62f);
      geomRow("Center X", &a.cx);
      geomRow("Center Y", &a.cy);
      geomRow("Radius", &a.r);
      ImGui::EndTable();
    }
  } else if (r.type == T::Ellipse && i < L.paperEllipses.size()) {
    CadEllipse& e = L.paperEllipses[i];
    if (PropSectionHeader("Geometry") && ImGui::BeginTable("props_paper_ell", 2, kPropTableFlags)) {
      ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthStretch, 0.38f);
      ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch, 0.62f);
      geomRow("Center X", &e.cx);
      geomRow("Center Y", &e.cy);
      geomRow("Ratio", &e.ratio);
      ImGui::EndTable();
    }
  } else if (r.type == T::Text && i < L.paperTexts.size()) {
    CadAnnotation& a = L.paperTexts[i];
    if (PropSectionHeader("Geometry") && ImGui::BeginTable("props_paper_txt", 2, kPropTableFlags)) {
      ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthStretch, 0.38f);
      ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch, 0.62f);
      geomRow("Insertion X", &a.insX);
      geomRow("Insertion Y", &a.insY);
      geomRow("Height (in)", &a.plottedHeightInches);
      ImGui::EndTable();
    }
    // Text content: seed a buffer when the selected text index changes, edit writes back on change.
    static int gPaperTextBufIdx = -1;
    static char gPaperTextBuf[1024];
    if (gPaperTextBufIdx != r.index) {
      gPaperTextBufIdx = r.index;
      ImStrncpy(gPaperTextBuf, a.text.c_str(), IM_ARRAYSIZE(gPaperTextBuf));
      gPaperTextBuf[IM_ARRAYSIZE(gPaperTextBuf) - 1] = '\0';
    }
    if (PropSectionHeader("Text")) {
      ImGui::TextUnformatted("Contents");
      ImGui::SetNextItemWidth(-1);
      if (ImGui::InputTextMultiline("##papertext", gPaperTextBuf, IM_ARRAYSIZE(gPaperTextBuf),
                                    ImVec2(-1, ImGui::GetTextLineHeight() * 3.f))) {
        a.text = gPaperTextBuf;
        BumpCadGpuCache(cmd);
      }
    }
  } else if (r.type == T::Polyline) {
    if (PropSectionHeader("Geometry") && ImGui::BeginTable("props_paper_poly", 2, kPropTableFlags)) {
      ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthStretch, 0.38f);
      ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch, 0.62f);
      const int v0 = L.paperPolyOffsets[i];
      const int v1 = L.paperPolyOffsets[i + 1];
      PropRow("Vertices", std::to_string(v1 - v0));
      PropRow("Closed", (i < L.paperPolyClosed.size() && L.paperPolyClosed[i]) ? "Yes" : "No");
      ImGui::EndTable();
    }
  }
}

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
  // A raised plate, like the ribbon. Drawn here rather than before End() because
  // this function has several early returns and a 1px line at the very top edge
  // sits above where any content can start (WindowPadding keeps it clear).
  {
    const ImVec2 wp = ImGui::GetWindowPos();
    const ImVec2 ws = ImGui::GetWindowSize();
    PlateTopHilite(ImGui::GetWindowDrawList(), wp, ImVec2(wp.x + ws.x, wp.y + ws.y));
  }

  // REQ-039: in a paper layout (not floating model space), the active selection is native paper-space
  // geometry, which lives in per-layout stores rather than cmd.selection. Show its dedicated panel.
  if (cmd.activeSpaceIndex >= 0 && !InFloatingModelSpace(cmd) && !cmd.selectedPaperEntities.empty()) {
    gPropsSelFingerprint = ~0ull;
    DrawPaperEntityProps(cmd);
    ImGui::End();
    return;
  }

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
  int nTable = 0;
  int nBlock = 0;
  int nPdf  = 0;
  int nSurf = 0;
  int firstSurfIx = -1;
  for (const auto& e : sel) {
    if      (e.type == SelectedEntity::Type::LineSeg)    ++nLine;
    else if (e.type == SelectedEntity::Type::Circle)     ++nCirc;
    else if (e.type == SelectedEntity::Type::Annotation) ++nAnn;
    else if (e.type == SelectedEntity::Type::Table) ++nTable;
    else if (e.type == SelectedEntity::Type::BlockRef) ++nBlock;
    else if (e.type == SelectedEntity::Type::PdfUnderlay)++nPdf;
    else if (e.type == SelectedEntity::Type::Surface) {
      ++nSurf;
      if (firstSurfIx < 0)
        firstSurfIx = e.index;
    }
  }

  ImGui::Text("Selected: %d object(s)", static_cast<int>(sel.size()));
  const int typeKinds = (nLine > 0 ? 1 : 0) + (nCirc > 0 ? 1 : 0) + (nAnn > 0 ? 1 : 0) + (nTable > 0 ? 1 : 0) +
                        (nPdf > 0 ? 1 : 0);
  if (typeKinds > 1)
    ImGui::TextDisabled("(Mixed: Line %d, Circle %d, Ann %d, Table %d, PDF %d)", nLine, nCirc, nAnn, nTable,
                        nPdf);
  else if (nLine > 1)
    ImGui::TextDisabled("%d lines", nLine);
  else if (nCirc > 1)
    ImGui::TextDisabled("%d circles", nCirc);
  else if (nAnn > 1)
    ImGui::TextDisabled("%d annotations", nAnn);
  else if (nTable > 1)
    ImGui::TextDisabled("%d tables", nTable);
  else if (nPdf > 1)
    ImGui::TextDisabled("%d PDF underlays", nPdf);
  else if (nLine == 1)
    ImGui::TextDisabled("Line");
  else if (nCirc == 1)
    ImGui::TextDisabled("Circle");
  else if (nPdf == 1)
    ImGui::TextDisabled("PDF Underlay");
  else if (nSurf > 1)
    ImGui::TextDisabled("%d surfaces", nSurf);
  else if (nSurf == 1)
    ImGui::TextDisabled("TIN Surface");
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
                        : k == CadAnnotation::Kind::Table    ? "TABLE"
                        : k == CadAnnotation::Kind::DimAligned ? "DIMALIGNED"
                        : k == CadAnnotation::Kind::DimLinear  ? "DIMLINEAR"
                                                              : "Annotation";
      ImGui::TextDisabled("%s", lab);
    } else
      ImGui::TextDisabled("Annotation");
  } else if (nTable == 1)
    ImGui::TextDisabled("TABLE");

  ImGui::Separator();

  DrawEditableGeneralSection(cmd, sel);

  // TIN surface (REQ-068 / ADR-036 (b)). Read-only by design: everything below is DERIVED from the
  // definition, so an editable field here would be a second source of truth that the next rebuild
  // overwrites. The definition itself is edited in the Surfaces panel, which the note points at.
  if (nSurf == 1 && firstSurfIx >= 0 && static_cast<size_t>(firstSurfIx) < cmd.cadSurfaces.size()) {
    const CadSurface& s = cmd.cadSurfaces[static_cast<size_t>(firstSurfIx)];
    if (PropSectionHeader("Surface")) {
      if (ImGui::BeginTable("props_surface", 2, kPropTableFlags)) {
        ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthStretch, 0.38f);
        ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch, 0.62f);
        const auto row = [](const char* k, const std::string& v) {
          ImGui::TableNextRow();
          PropValueCellBg();
          ImGui::TableNextColumn(); ImGui::TextUnformatted(k);
          ImGui::TableNextColumn(); ImGui::TextUnformatted(v.c_str());
        };
        row("Name", s.name);
        row("Points", std::to_string(s.vertexCount()));
        row("Triangles", std::to_string(s.triangleCount()));
        if (s.tin && s.tin->vertsXyz.size() >= 3) {
          float lo = s.tin->vertsXyz[2], hi = lo;
          for (size_t i = 2; i < s.tin->vertsXyz.size(); i += 3) {
            lo = std::min(lo, s.tin->vertsXyz[i]);
            hi = std::max(hi, s.tin->vertsXyz[i]);
          }
          const int p = cmd.displayLinearPrecision;
          row("Elevation range", FormatLinear(lo, p) + " to " + FormatLinear(hi, p));
        } else {
          row("Elevation range", "\xe2\x80\x94");
        }
        row("Definition", std::to_string(s.sourcePointGroups.size()) + " group(s), " +
                              std::to_string(s.sourcePointFiles.size()) + " file(s), " +
                              std::to_string(s.breaklines.size()) + " breakline(s), " +
                              std::to_string(s.boundaries.size()) + " boundary(ies)");
        // REQ-086: a surface showing an older triangulation than its definition describes must SAY
        // so. Reporting the counts above while staying silent about that is precisely the quiet
        // "looks current" state the requirement exists to prevent.
        if (s.lastBuildIncomplete)
          row("Status", "out of date \xe2\x80\x94 " + s.lastBuildMessage);
        ImGui::EndTable();
      }
      ImGui::TextDisabled("Read-only. Edit the definition in the Surfaces panel.");
    }
  }

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
  } else if (nLine == 0 && nCirc == 0 && nAnn == 0 && nTable > 0) {
    int tblIdx = -1;
    for (const auto& e : sel) {
      if (e.type == SelectedEntity::Type::Table) {
        tblIdx = e.index;
        break;
      }
    }
    if (nTable == 1 && tblIdx >= 0)
      DrawSingleTableGeometryEditable(cmd, tblIdx);
  } else if (nLine == 0 && nCirc == 0 && nAnn == 0 && nTable == 0 && nBlock > 0) {
    int brIdx = -1;
    for (const auto& e : sel) {
      if (e.type == SelectedEntity::Type::BlockRef) {
        brIdx = e.index;
        break;
      }
    }
    if (nBlock == 1 && brIdx >= 0 && static_cast<size_t>(brIdx) < cmd.cadBlockRefs.size()) {
      CadBlockRef& r = cmd.cadBlockRefs[static_cast<size_t>(brIdx)];
      if (PropSectionHeader("Block reference")) {
        ImGui::TextUnformatted(r.defName.c_str());
        ImGui::DragFloat("X##blk", &r.xf.x, 0.01f);
        ImGui::DragFloat("Y##blk", &r.xf.y, 0.01f);
        ImGui::DragFloat("Z##blk", &r.xf.z, 0.01f);
        ImGui::DragFloat("Scale X##blk", &r.xf.sx, 0.01f);
        ImGui::DragFloat("Scale Y##blk", &r.xf.sy, 0.01f);
        ImGui::DragFloat("Scale Z##blk", &r.xf.sz, 0.01f);
        float deg = r.xf.rotZ * 57.2957795f;
        if (ImGui::DragFloat("Rotation##blk", &deg, 0.1f))
          r.xf.rotZ = deg * 0.01745329252f;
        BumpCadGpuCache(cmd);
      }
    }
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
  } else if (nPdf > 0 && nLine == 0 && nCirc == 0 && nAnn == 0 && nTable == 0) {
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

// AutoCAD's default polar increments, offered in the POLAR status-bar popup (issue #154).
constexpr double kPolarIncrementChoices[] = {90.0, 45.0, 30.0, 22.5, 18.0, 15.0, 10.0, 5.0};

void PushModeToggleButtonColors(bool on, int themeIdx) {
  (void)themeIdx;
  if (!on)
    return;
  ImGui::PushStyleColor(ImGuiCol_Button,        ImGui::ColorConvertU32ToFloat4(g_chrome.ribbonTabOn));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::ColorConvertU32ToFloat4(g_chrome.ribbonTabOnHovered));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImGui::ColorConvertU32ToFloat4(g_chrome.ribbonTabOnActive));
  ImGui::PushStyleColor(ImGuiCol_Text,          ImGui::ColorConvertU32ToFloat4(g_chrome.ribbonTabOnText));
}

void PopModeToggleButtonColors(bool on) {
  if (on)
    ImGui::PopStyleColor(4);
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
  // REQ-307 (GitHub #106): paper-space MOVE/COPY/DELETE's selection step never sets cmd.active (it
  // stays pick-first, K::None, throughout), so it cannot be reached by any of the Kind-based branches
  // below — checked first, ahead of all of them.
  if (PaperIsObjectSelectionStep(cmd))
    return kSelectObjectsPrompt;
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
      return "LINE distance ± / click ray / X,Y / [A] clears:";
    return "Next: click; X, Y; @dx,dy; [A]zimuth, [2P];";
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
      return "POLYLINE distance ± / ray click / [A] clears / [CLOSE]:";
    if (cmd.orthoMode)
      return "POLYLINE next — ortho / X,Y / [A] / [AP] / [CLOSE]:";
    return "POLYLINE next — X,Y / [A] / [AP] / [CLOSE]:";
  }
  if (cmd.active == AppCommandState::Kind::Rect) {
    return cmd.rectPhase == AppCommandState::RectPhase::WaitFirstCorner
               ? "RECT first corner — click or X,Y:"
               : "RECT opposite corner — click / X,Y / @dx,dy:";
  }
  if (cmd.active == AppCommandState::Kind::TrimState)
    return "TRIMSTATE — 0 = draw a line to trim, 1 = pick cutting edges:";
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
      return "TEXT rotation ° CW from N (0 = up):";
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
      return cmd.active == AppCommandState::Kind::DimLinear ? "DIMLINEAR line (cursor/[H]/[V]) or X,Y:"
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
  // The prompted solid primitives (REQ-313 as amended). REQ-304 requires every Kind to carry a live
  // prompt; this one is COMPUTED rather than a literal, because it echoes the dimensions already set
  // back to the user, and there are seven primitives with four different parameter sets between them.
  //
  // The buffer is static because this function returns `const char*` and every other branch returns
  // a string literal. Safe here and only here: the hint is built and consumed on the UI thread
  // within one frame, by this call and the cursor-text call that shares it — the same string, which
  // is REQ-304's actual requirement (the command line and the cursor must not disagree).
  if (cmd.active == AppCommandState::Kind::Solid) {
    static std::string solidHint;
    solidHint = CadSolidPromptText(cmd);
    return solidHint.c_str();
  }
  if (cmd.active == AppCommandState::Kind::Extrude) {
    static std::string extrudeHint;
    extrudeHint = CadExtrudePromptText(cmd);
    return extrudeHint.c_str();
  }
  if (cmd.active == AppCommandState::Kind::Revolve) {
    static std::string revolveHint;
    revolveHint = CadRevolvePromptText(cmd);
    return revolveHint.c_str();
  }
  if (cmd.active == AppCommandState::Kind::Loft) {
    static std::string loftHint;
    loftHint = CadLoftPromptText(cmd);
    return loftHint.c_str();
  }
  if (cmd.active == AppCommandState::Kind::Sweep) {
    static std::string sweepHint;
    sweepHint = CadSweepPromptText(cmd);
    return sweepHint.c_str();
  }
  if (cmd.active == AppCommandState::Kind::Slice) {
    static std::string sliceHint;
    sliceHint = CadSlicePromptText(cmd);
    return sliceHint.c_str();
  }
  if (cmd.active == AppCommandState::Kind::Boolean) {
    static std::string boolHint;
    boolHint = CadBooleanPromptText(cmd);
    return boolHint.c_str();
  }
  // REQ-317 POLYSOLID, computed for the same reason: the hint echoes the height, width and
  // justification in force, so a literal would go stale the moment one of them changed.
  if (cmd.active == AppCommandState::Kind::Polysolid) {
    static std::string polysolidHint;
    polysolidHint = CadPolysolidPromptText(cmd);
    return polysolidHint.c_str();
  }
  if (cmd.active == AppCommandState::Kind::Circle) {
    using CP = AppCommandState::CirclePhase;
    switch (cmd.circlePhase) {
    case CP::WaitCenterOrMode:
      return "Center or type [3P]:";
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
      return kSelectObjectsPrompt;  // REQ-121
    if (cmd.modifyPhase == MP::NeedBase)
      return "Base point X,Y:";
    return "Destination @dx,dy or X,Y:";
  }
  if (cmd.active == AppCommandState::Kind::Array) {
    using AP = AppCommandState::ArrayPhase;
    switch (cmd.arrayPhase) {
    case AP::PickSelection:
      return kSelectObjectsPrompt;  // REQ-121
    case AP::WaitType:
      return "ARRAY — array type: [R]ectangular / [P]olar:";
    case AP::Rect_WaitColumns:
      return "ARRAY Rectangular — number of columns:";
    case AP::Rect_WaitColumnSpacing:
      return "ARRAY Rectangular — column spacing (type a distance, or click):";
    case AP::Rect_WaitRows:
      return "ARRAY Rectangular — number of rows:";
    case AP::Rect_WaitRowSpacing:
      return "ARRAY Rectangular — row spacing (type a distance, or click):";
    case AP::Polar_WaitCenter:
      return "ARRAY Polar — specify center point:";
    case AP::Polar_WaitItemCount:
      return "ARRAY Polar — number of items (total, including the original):";
    case AP::Polar_WaitAngle:
      return "ARRAY Polar — angle to fill in degrees (type, or click):";
    case AP::Polar_WaitRotateAnswer:
      return "ARRAY Polar — rotate items? [Y]es / [N]o:";
    }
    return "ARRAY:";
  }
  if (cmd.active == AppCommandState::Kind::Scale) {
    using MP = AppCommandState::ModifyPhase;
    using SP = AppCommandState::ScalePhase;
    if (cmd.modifyPhase == MP::PickSelection)
      return kSelectObjectsPrompt;  // REQ-121
    if (cmd.modifyPhase == MP::NeedBase)
      return "SCALE — base point X,Y:";
    if (cmd.modifyPhase == MP::NeedDestination) {
      switch (cmd.scalePhase) {
      case SP::FactorPick:
        return "SCALE — scale factor, second point from base, or [R]eference:";
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
      return kSelectObjectsPrompt;  // REQ-121
    case RP::NeedBase:
      return "Base point X,Y:";
    case RP::NeedAngleOrReference:
      return "° CW from north / DMS / [R]eference / [C]opy:";
    case RP::Ref_WaitP1:
    case RP::Ref_WaitP2:
      return "Reference point X,Y ([C] toggles copy):";
    case RP::AfterReference_WaitAngleOrP:
      return "Bearing ° from north / DMS / [P] / [C]opy:";
    case RP::AnglePoints_WaitP1:
    case RP::AnglePoints_WaitP2:
      return "Angle point X,Y ([C] toggles copy):";
    }
  }
  if (cmd.active == AppCommandState::Kind::Mirror) {
    using MirP = AppCommandState::MirrorPhase;
    switch (cmd.mirrorPhase) {
    case MirP::PickSelection:
      return kSelectObjectsPrompt;  // REQ-121
    case MirP::NeedP1:
      return "MIRROR — first point of mirror line X,Y:";
    case MirP::NeedP2:
      return "MIRROR — second point of mirror line X,Y:";
    case MirP::NeedEraseAnswer:
      return "Erase source objects? [Yes/No] <N>:";
    }
  }
  if (cmd.active == AppCommandState::Kind::Lengthen) {
    using LP = AppCommandState::LengthenPhase;
    switch (cmd.lengthenPhase) {
    case LP::WaitSelectOrMode:
      return "LENGTHEN — select object, or [DElta/Percent/Total/DYnamic]:";
    case LP::WaitDeltaValue:
      return "LENGTHEN DElta — length to add (+) or subtract (-):";
    case LP::WaitPercentValue:
      return "LENGTHEN Percent — new length as % of current:";
    case LP::WaitTotalValue:
      return "LENGTHEN Total — new total length:";
    case LP::WaitDynamicTarget:
      return "LENGTHEN DYnamic — drag to the new length, or type it:";
    }
  }
  if (cmd.active == AppCommandState::Kind::Extend) {
    using EP = AppCommandState::ExtendPhase;
    switch (cmd.extendPhase) {
    case EP::SelectBoundaries:
      return "EXTEND — pick boundary edges, Enter when done:";
    case EP::SelectTargets:
      return "EXTEND — click objects to extend (near the end to stretch), Enter when done:";
    }
  }
  if (cmd.active == AppCommandState::Kind::Break) {
    using BP = AppCommandState::BreakPhase;
    switch (cmd.breakPhase) {
    case BP::SelectFirstPoint:
      return "BREAK — select object (the pick is break point 1):";
    case BP::SelectSecondPoint:
      return "BREAK — specify second break point:";
    }
  }
  if (cmd.active == AppCommandState::Kind::Stretch) {
    using MP = AppCommandState::ModifyPhase;
    if (cmd.modifyPhase == MP::PickSelection)
      return "STRETCH — crossing/window box (right-to-left = crossing) or cancel:";
    if (cmd.modifyPhase == MP::NeedBase)
      return "Base point X,Y:";
    return "Destination @dx,dy or X,Y:";
  }
  if (cmd.active == AppCommandState::Kind::Delete)
    return kSelectObjectsPrompt;  // REQ-121
  if (cmd.active == AppCommandState::Kind::Join)
    return kSelectObjectsPrompt;  // REQ-121
  if (cmd.active == AppCommandState::Kind::Trim) {
    using TP = AppCommandState::TrimPhase;
    if (cmd.trimPhase == TP::SelectCuttingEdges)
      return "TRIM — cutting edges, Enter (or [L] = draw on segment, two clicks):";
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

// Content-driven width for the dynamic-cursor input field (REQ-306): sized to fit
// the widest of the strings actually shown (current field text plus, when given,
// a placeholder/hint that must also fit), rather than a fixed footprint. `minPx`
// keeps the field usable for its first keystroke; `maxPx` bounds a pathological
// paste from taking over the viewport.
static float DynamicCursorFieldWidth(const char* text, const char* alsoFits, float minPx, float maxPx) {
  float w = ImGui::CalcTextSize(text ? text : "").x;
  if (alsoFits) w = std::max(w, ImGui::CalcTextSize(alsoFits).x);
  const float chrome = ImGui::GetStyle().FramePadding.x * 2.f + ImGui::GetFontSize();
  return std::clamp(w + chrome, minPx, maxPx);
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
  case K::Rect: return true;  // both corners are point prompts (REQ-024/REQ-053)
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
  // REQ-074. Missing here as well as from the viewport click dispatch, so SURFELEV got neither
  // typed-point entry nor a usable pick — the same pre-existing TASK-055 gap, in the second of the
  // two lists a point-picking command has to appear in.
  case K::SurfaceElevGrade: return true;
  case K::WaterDrop: return true;
  case K::Catchment: return true;
  case K::SwapTinEdge: return true;
  case K::AddTinPoint: return true;
  case K::DelTinPoint: return true;
  case K::MoveTinPoint: return true;
  case K::DelTinLine: return true;
  case K::QuickProfile: return true;
  // REQ-154. The second of the two lists a point-picking command has to appear in — UCS was missing
  // from both, so it had neither dynamic input nor a working click. Same phases that
  // ViewportClickRouteFor routes: everything that takes a coordinate, and nothing that wants a
  // keyword or a number.
  case K::Ucs: {
    using UPh = AppCommandState::UcsPhase;
    return cmd.ucsPhase == UPh::WaitOriginOrOption || cmd.ucsPhase == UPh::WaitXAxisPoint ||
           cmd.ucsPhase == UPh::WaitXyPoint || cmd.ucsPhase == UPh::WaitRotationAngleP1 ||
           cmd.ucsPhase == UPh::WaitRotationAngleP2 || cmd.ucsPhase == UPh::WaitZAxisOrigin ||
           cmd.ucsPhase == UPh::WaitZAxisPoint;
  }
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
  case K::Mirror: {
    using MirP = AppCommandState::MirrorPhase;
    return cmd.mirrorPhase == MirP::NeedP1 || cmd.mirrorPhase == MirP::NeedP2;
    // NeedEraseAnswer is a Yes/No text prompt, not a point (HandleMirrorText).
  }
  case K::Stretch: {
    // REQ-103 step 5. Base and destination are both real points (typed or picked), so STRETCH
    // gets the same dynamic-input prompt MOVE/COPY do — it was omitted here, the second of the
    // two lists a point-picking command has to appear in (TASK-099 F2).
    using MP = AppCommandState::ModifyPhase;
    return cmd.modifyPhase == MP::NeedBase || cmd.modifyPhase == MP::NeedDestination;
  }
  case K::InsertBlock: {
    using IPh = AppCommandState::InsertBlockPhase;
    return cmd.insertBlockPhase == IPh::WaitInsertPoint || cmd.insertBlockPhase == IPh::WaitScale;
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

// REQ-154 / REQ-024. The two UCS axis prompts show a POLAR pair — distance and angle — rather than
// REQ-024's single x,y field, because what those prompts ask for is a DIRECTION. An x,y readout
// answers "where is my cursor"; the question on screen is "what angle is my axis", and the user
// should not have to do the subtraction in their head.
//
// This is a stated exception, not a reversal: every other point prompt keeps the single field
// REQ-024's 2026-06-19 revision settled on, and the polar pair assembles `@distance<angle` — real
// syntax the command line accepts — so the two forms describe the same thing.
//
// Returns false, and leaves the outputs alone, for every prompt that is not one of those two.
static bool CadUcsPolarPromptBase(const AppCommandState& cmd, ray3d::Vec3* baseWorld) {
  if (cmd.active != AppCommandState::Kind::Ucs || !baseWorld)
    return false;
  using UPh = AppCommandState::UcsPhase;
  switch (cmd.ucsPhase) {
  case UPh::WaitXAxisPoint:
  case UPh::WaitXyPoint:
    // Both measure from the ORIGIN, not from each other — one reference for both boxes, so the
    // second prompt does not silently re-base the angle the first one showed.
    *baseWorld = cmd.ucsPendingOrigin;
    return true;
  case UPh::WaitRotationAngleP2:
    *baseWorld = cmd.ucsAngleBasePoint;
    return true;
  default:
    return false;
  }
}

// AutoCAD-style "Specify … :" label for the dynamic-input point prompt (REQ-024).
// Only meaningful when CommandExpectsPointEntry(cmd) is true. Multi-point chains
// (LINE, POLYLINE) count the point being specified: first, second, third, …

static std::string CadPointPromptLabel(const AppCommandState& cmd) {
  using K = AppCommandState::Kind;
  switch (cmd.active) {
  case K::Line:
    // After the first point, show the same "Next: …" help as the command line
    // (clickable shortcuts there; literal brackets here). REQ-024.
    return cmd.linePhase == AppCommandState::LinePhase::NeedFirstPoint
               ? std::string("Specify first point:")
               : std::string("Next: click; X, Y; @dx,dy; [A]zimuth, [2P];");
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
  case K::Rect:
    return cmd.rectPhase == AppCommandState::RectPhase::WaitFirstCorner ? "Specify first corner point:"
                                                                       : "Specify other corner point:";
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
  // REQ-154. Wording follows AutoCAD's own UCS prompts, including the `<accept>` on the two axis
  // steps — both take a bare Enter to accept what has been picked so far, and the label is where a
  // user learns that.
  case K::Ucs:
    switch (cmd.ucsPhase) {
    case AppCommandState::UcsPhase::WaitOriginOrOption:  return "Specify origin of UCS:";
    case AppCommandState::UcsPhase::WaitXAxisPoint:      return "Specify point on X-axis or <accept>:";
    case AppCommandState::UcsPhase::WaitXyPoint:         return "Specify point on the XY plane or <accept>:";
    case AppCommandState::UcsPhase::WaitRotationAngleP1: return "Specify first point of the angle:";
    case AppCommandState::UcsPhase::WaitRotationAngleP2: return "Specify second point of the angle:";
    case AppCommandState::UcsPhase::WaitZAxisOrigin:     return "Specify new origin point:";
    case AppCommandState::UcsPhase::WaitZAxisPoint:      return "Specify point on positive portion of Z axis:";
    default:                                             return "Specify point:";
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
  case K::Mirror:
    return cmd.mirrorPhase == AppCommandState::MirrorPhase::NeedP1 ? "Specify first point of mirror line:"
                                                                   : "Specify second point of mirror line:";
  case K::Stretch:
    return cmd.modifyPhase == AppCommandState::ModifyPhase::NeedBase ? "Specify base point:"
                                                                     : "Specify second point:";
  case K::InsertBlock: {
    using IPh = AppCommandState::InsertBlockPhase;
    if (cmd.insertBlockPhase == IPh::WaitInsertPoint)
      return "Specify insertion point:";
    if (cmd.insertBlockPhase == IPh::WaitScale)
      return "Specify scale point:";
    if (cmd.insertBlockPhase == IPh::WaitRotation)
      return "Specify rotation angle <0d0'0\">:";
    return "Specify point:";
  }
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
  ImGui::PushStyleColor(ImGuiCol_WindowBg, g_chrome.statusBarFace);
  ImGui::Begin("##CadStatusBarStrip", nullptr, wf);
  ImGui::PopStyleColor();

  ImGui::Separator();
  const float statusBtnH = ImGui::GetFrameHeight();
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
  ImGui::PushStyleColor(ImGuiCol_ChildBg, g_chrome.statusStripFace);
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
    // The UCS field used to be the literal word "World". It now reports the actual work plane, so
    // a raised elevation is visible — otherwise geometry silently lands somewhere the user did not
    // expect and nothing on screen says why (REQ-058 / REQ-201).
    if (CadUcsIsWorld(cmd)) {
      ImGui::Text("X %s  Y %s  Z %s  |  UCS: World", FormatLinear(cursorX, p).c_str(),
                  FormatLinear(cursorY, p).c_str(), FormatLinear(cursorZ, p).c_str());
    } else {
      // Under a UCS the readout reports UCS coordinates (REQ-154), because those are the numbers the
      // user would type back in to return here. Reporting world coordinates while entry is read in
      // the UCS is how a value gets copied off the screen and re-entered somewhere else entirely.
      // The label says which frame it is, and the world position is kept alongside so the two are
      // never confused.
      const ray3d::Vec3 inUcs =
          ucs::WorldToUcs(cmd.activeUcs, {cursorX, cursorY, static_cast<double>(cursorZ)});
      ImGui::Text("X %s  Y %s  Z %s  |  UCS: current (world %s, %s)", FormatLinear(inUcs.x, p).c_str(),
                  FormatLinear(inUcs.y, p).c_str(), FormatLinear(inUcs.z, p).c_str(),
                  FormatLinear(cursorX, p).c_str(), FormatLinear(cursorY, p).c_str());
    }
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
#ifdef GOSURVEY_DEVELOPER_SHELL
                         bw("DEV") +
#endif
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
        ImGui::Checkbox("Intersection", &cmd.objectSnapIntersection);
        ImGui::Checkbox("Apparent intersection", &cmd.objectSnapApparentIntersection);
        ImGui::Checkbox("Surface elevation", &cmd.objectSnapSurface);
        ImGui::Checkbox("Solid face / edge", &cmd.objectSnapSolid);
        ImGui::EndPopup();
      }
      ImGui::SameLine(0, sp);
    }
    if (ortho_mode_enabled) {
      const bool on = *ortho_mode_enabled;
      PushModeToggleButtonColors(on, cmd.displayColorThemeIdx);
      if (ImGui::Button("ORTHO", ImVec2(0.f, statusBtnH))) {
        *ortho_mode_enabled = !*ortho_mode_enabled;
        if (*ortho_mode_enabled)
          cmd.polarMode = false;  // ORTHO and POLAR are mutually exclusive (issue #154)
      }
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
      const bool on = cmd.polarMode;
      PushModeToggleButtonColors(on, cmd.displayColorThemeIdx);
      if (ImGui::Button("POLAR", ImVec2(0.f, statusBtnH))) {
        cmd.polarMode = !cmd.polarMode;
        if (cmd.polarMode && ortho_mode_enabled)
          *ortho_mode_enabled = false;  // mutually exclusive (issue #154)
      }
      PopModeToggleButtonColors(on);
      ItemHelpTooltip("Polar tracking — snap to increment angles in the current UCS (F10); "
                      "right-click to set the angle");
      // DSETTINGS-style configuration: increment angle + additional one-off angles (issue #154 AC-2).
      if (ImGui::BeginPopupContextItem("polar_angles", ImGuiPopupFlags_MouseButtonRight)) {
        ImGui::TextDisabled("Increment angle");
        ImGui::Separator();
        for (double choice : kPolarIncrementChoices) {
          char lbl[16];
          std::snprintf(lbl, sizeof(lbl), "%g\xC2\xB0", choice);
          if (ImGui::RadioButton(lbl, std::fabs(cmd.polarIncrementDeg - choice) < 1e-6))
            cmd.polarIncrementDeg = choice;
        }
        ImGui::Separator();
        ImGui::TextDisabled("Additional angles");
        for (size_t i = 0; i < cmd.polarExtraAnglesDeg.size();) {
          ImGui::PushID(static_cast<int>(i));
          float v = static_cast<float>(cmd.polarExtraAnglesDeg[i]);
          ImGui::SetNextItemWidth(80.f);
          if (ImGui::InputFloat("##ang", &v, 0.f, 0.f, "%.2f"))
            cmd.polarExtraAnglesDeg[i] = v;
          ImGui::SameLine();
          const bool del = ImGui::SmallButton("x");
          ImGui::PopID();
          if (del)
            cmd.polarExtraAnglesDeg.erase(cmd.polarExtraAnglesDeg.begin() + static_cast<std::ptrdiff_t>(i));
          else
            ++i;
        }
        if (ImGui::SmallButton("Add angle"))
          cmd.polarExtraAnglesDeg.push_back(0.0);
        ImGui::EndPopup();
      }
      ImGui::SameLine(0, sp);
    }
#ifdef GOSURVEY_DEVELOPER_SHELL
    {
      const bool on = cmd.devShellVisible;
      PushModeToggleButtonColors(on, cmd.displayColorThemeIdx);
      if (ImGui::Button("DEV", ImVec2(0.f, statusBtnH)))
        cmd.devShellVisible = !cmd.devShellVisible;
      PopModeToggleButtonColors(on);
      ItemHelpTooltip("Developer Shell — Debug only. Off by default. Activity, command, and Test Engine logs "
                      "are always written next to the executable.");
      ImGui::SameLine(0, sp);
    }
#endif
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

// REQ-040/REQ-119: lay out one command prompt — plain text plus clickable variant links —
// and return the height it occupies. `cmdbar::ParsePromptSegments` decides what is a link;
// this function only places the pieces, so the rule stays testable without a UI harness.
//
// Clicking a link calls ProcessCommandLineSubmit with the variant's shortcut — the identical
// entry point Enter uses on typed text. That is what makes mouse and keyboard the same path
// rather than two implementations that can drift (GitHub #81).
//
// `draw == false` measures without emitting any item, which is how DrawCommandLinePanel
// reserves footer height: the reservation and the drawn content run the SAME layout, so they
// cannot disagree about where lines break. They must not — a hint that reserves one line and
// draws two shoves the links out from under the mouse (the REQ-040 note above the footer).
//
// `wrapW <= 0` disables wrapping, which is what the floating bar wants: it is one line by
// design (REQ-040), so its prompt must never reflow.
static float LayoutCommandHint(const char* hint, AppCommandState& cmd, std::vector<std::string>& log,
                               const ImVec4& dimCol, float wrapW, bool draw) {
  const std::vector<cmdbar::PromptSegment> segs = cmdbar::ParsePromptSegments(hint);
  if (segs.empty())
    return 0.f;

  const bool wrap = wrapW > 0.f;
  auto submit = [&](const std::string& shortcut) {
    std::string tok = shortcut;
    for (char& c : tok) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    char tmp[32];
    std::snprintf(tmp, sizeof(tmp), "%s", tok.c_str());
    ProcessCommandLineSubmit(tmp, static_cast<int>(sizeof(tmp)), cmd, log);
  };

  int lines = 1;
  float lineW = 0.f;
  bool firstOnLine = true;  // true => emit with no SameLine, so ImGui starts a fresh line

  auto place = [&](const std::string& text, bool isLink, const std::string& shortcut) {
    const float w = ImGui::CalcTextSize(text.c_str()).x;
    if (wrap && !firstOnLine && lineW + w > wrapW) {
      ++lines;
      lineW = 0.f;
      firstOnLine = true;
    }
    if (draw) {
      if (!firstOnLine)
        ImGui::SameLine(0.f, 0.f);
      if (isLink) {
        if (ImGui::TextLink(text.c_str()))
          submit(shortcut);
      } else {
        ImGui::PushStyleColor(ImGuiCol_Text, dimCol);
        ImGui::TextUnformatted(text.c_str());
        ImGui::PopStyleColor();
      }
    }
    lineW += w;
    firstOnLine = false;
  };

  for (const cmdbar::PromptSegment& s : segs) {
    if (s.isLink) {
      place(s.text, true, s.shortcut);  // a link never splits — it is one click target
      continue;
    }
    // Plain text is emitted whole whenever it fits, so spacing renders exactly as written.
    // Splitting into words is only done when the run must wrap, which is the one case where
    // exact spacing cannot survive anyway — and is what keeps a long docked prompt on-screen.
    const float w = ImGui::CalcTextSize(s.text.c_str()).x;
    if (!wrap || lineW + w <= wrapW) {
      place(s.text, false, std::string());
      continue;
    }
    for (std::size_t i = 0; i < s.text.size();) {
      std::size_t e = i;
      while (e < s.text.size() && s.text[e] != ' ')
        ++e;
      while (e < s.text.size() && s.text[e] == ' ')
        ++e;  // carry the following spaces with the word
      std::string word = s.text.substr(i, e - i);
      if (firstOnLine) {
        const std::size_t nb = word.find_first_not_of(' ');
        word = (nb == std::string::npos) ? std::string() : word.substr(nb);  // no leading space
      }
      if (!word.empty())
        place(word, false, std::string());
      i = e;
    }
  }

  const float lineH = ImGui::GetTextLineHeight();
  return static_cast<float>(lines) * lineH +
         static_cast<float>(lines - 1) * ImGui::GetStyle().ItemSpacing.y;
}

void DrawCommandLinePanel(std::vector<std::string>& log, char* cmdBuf, int cmdBufSize, AppCommandState& cmd) {
  const bool isDark = (cmd.displayColorThemeIdx == 0);
  // Console background is slightly distinct from the main workspace in both themes.
  const ImVec4 consoleBg = isDark
      ? Hex(0x2F2F2F)                         // the ground step — recessed vs the panel surface
      : ImVec4(0.235f, 0.235f, 0.235f, 1.f);  // #3C3C3C console panel (recessed vs #464646 band)
  const ImVec4 promptColor = isDark
      ? Hex(0x6CC07A)                         // the palette's success hue, lightened for text: 8.3:1
      : ImVec4(0.180f, 0.720f, 0.400f, 1.f);  // #2EB766 bright green on dark console
  // REQ-040: floating AutoCAD-style command bar (default) vs the legacy docked panel
  // (cmd.cmdLineClassicDock). One function, one shared input + autocomplete; only the
  // surrounding chrome differs by `floating`.
  const bool floating = !cmd.cmdLineClassicDock;

  // Fade timer: reset whenever a log line is appended (drives the recent-history fade).
  if (log.size() != cmd.cmdLogLastSizeForFade) {
    cmd.cmdLogLastSizeForFade = log.size();
    cmd.cmdLogLastChangeTime = ImGui::GetTime();
  }
  {
    // F2 toggles the expanded console; Ctrl+9 hides/restores the bar. ESC is left to
    // command-cancel (handled elsewhere) and never closes the console.
    ImGuiIO& iok = ImGui::GetIO();
    if (floating && ImGui::IsKeyPressed(ImGuiKey_F2, false))
      cmd.cmdConsoleOpen = !cmd.cmdConsoleOpen;
    if (floating && iok.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_9, false))
      cmd.cmdBarVisible = !cmd.cmdBarVisible;
  }
  if (floating && !cmd.cmdBarVisible)
    return;  // bar hidden; Ctrl+9 (or the View menu) restores it.

  const float barRounding = 5.f;
  const ImVec4 barBg = isDark ? Hex(0x343434, cmd.cmdBarOpacity)  // the title-bar step
                              : ImVec4(0.247f, 0.247f, 0.247f, cmd.cmdBarOpacity);
  ImGuiWindowFlags winFlags = 0;
  if (floating) {
    winFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
               ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings |
               ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar |
               ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoFocusOnAppearing;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    // Width is user-resizable (default ~half the viewport); the bar is pinned to the viewport
    // bottom (Y locked) and only slides left/right (X). Anchor is the bottom-LEFT corner.
    const float barW = std::clamp(cmd.cmdBarWidth > 1.f ? cmd.cmdBarWidth : vp->WorkSize.x * 0.5f, 320.f,
                                  std::max(360.f, vp->WorkSize.x - 16.f));
    cmd.cmdBarWidth = barW;
    if (!cmd.cmdBarAnchorValid) {
      cmd.cmdBarAnchorX = vp->WorkPos.x + (vp->WorkSize.x - barW) * 0.5f;
      cmd.cmdBarAnchorValid = true;
    }
    // Clamp X on-screen. Guard the upper bound: when the bar is wider than the viewport
    // (or WorkSize is tiny on the first frame) the max would fall below the min, which is
    // undefined for std::clamp (and asserts in debug).
    const float xMin = vp->WorkPos.x + 4.f;
    const float xMax = vp->WorkPos.x + vp->WorkSize.x - barW - 4.f;
    cmd.cmdBarAnchorX = std::clamp(cmd.cmdBarAnchorX, xMin, std::max(xMin, xMax));
    // Pin the bar's bottom edge just above the status-bar strip (Y locked) — never below it.
    // The gap matters: with the bar's bottom flush on the strip the two read as one
    // welded block, and the bar stops looking like it floats over the drawing.
    constexpr float kCmdBarLift = 10.f;
    const float bottomY = vp->WorkPos.y + vp->WorkSize.y - CadStatusBarStripHeightPx() - kCmdBarLift;
    ImGui::SetNextWindowPos(ImVec2(cmd.cmdBarAnchorX, bottomY), ImGuiCond_Always, ImVec2(0.0f, 1.0f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(barW, 0.f), ImVec2(barW, FLT_MAX));  // fixed width, auto height
    ImGui::SetNextWindowBgAlpha(0.f);  // transparent; the bar background and history chips are painted manually
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.f, 4.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);  // no enclosing box; only the chips + bar show
  } else {
    ImGui::SetNextWindowSize(ImVec2(900, 220), ImGuiCond_FirstUseEver);
  }
  ImGui::PushStyleColor(ImGuiCol_WindowBg, floating ? ImVec4(0, 0, 0, 0) : consoleBg);
  if (!ImGui::Begin(floating ? "##CommandBarFloat" : "Command line", nullptr, winFlags)) {
    ImGui::End();
    ImGui::PopStyleColor();
    if (floating) ImGui::PopStyleVar(2);
    return;
  }
  // The bar's height is content-driven (history chips grow it), so its top edge is only knowable
  // once it has been laid out. Recorded here for the UCS icon, which shares this corner and would
  // otherwise be drawn underneath the bar (the bar is a separate window painted over the viewport).
  cmd.cmdBarTopYPx = floating ? ImGui::GetWindowPos().y : 0.f;
  // Taller frames for the floating bar (a roomier input/icons row than the default).
  if (floating)
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, 7.f));

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
      cmd.active != AppCommandState::Kind::None && cmd.viewportDrawingHovered && !cmd.mtextRichEditorOpen &&
      !cmd.tableCellEditorOpen;

  auto footerNonEmpty = [](const char* s) { return s && s[0] != '\0'; };
  auto wrappedBlockH = [&](const char* s) -> float {
    if (!footerNonEmpty(s))
      return 0.f;
    // A hint carrying variant markup is laid out piece-by-piece, not word-wrapped as one
    // string, so it must be MEASURED the same way (REQ-119) — CalcTextSize would count the
    // brackets ImGui never draws as one run and could disagree about the line count.
    if (std::strchr(s, '['))
      return LayoutCommandHint(s, cmd, log, ImVec4(), wrapW, /*draw=*/false) + st.ItemSpacing.y;
    return ImGui::CalcTextSize(s, nullptr, false, wrapW).y + st.ItemSpacing.y;
  };

  // Exact footer height (separator + input + wrapped hints). Avoids a tall empty band when the dock is tall
  // but only one short hint is visible (old line-budget heuristic summed +2 lines per hint category).
  const float sepBeforeInput = st.ItemSpacing.y + 1.f;
  float footerH = sepBeforeInput;
  // The status line (input box OR the "follows the cursor" hint) always reserves one
  // frame height, so the clickable footer hints below keep a stable position when the
  // cursor crosses between the viewport and this panel — otherwise the taller input
  // box that appears on hover shoves the [A]/[2P] links down out from under the mouse.
  footerH += ImGui::GetFrameHeight();
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
    // Build a contiguous UTF-8 buffer of the log (selectable console + classic log + clipboard share it).
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
  }

  if (!floating) {
    // --- Classic docked scrolling log: Copy-log button + read-only TextUnformatted child. ---
    const float copyBtnW = ImGui::CalcTextSize("Copy log").x + ImGui::GetStyle().FramePadding.x * 2.f + 8.f;
    if (ImGui::Button("Copy log", ImVec2(copyBtnW, 0.f)))
      ImGui::SetClipboardText(cmd.commandLogCacheBytes.data());
    ItemHelpTooltip("Copies the entire command log to the clipboard.");
    ImGui::SameLine();
    ImGui::TextDisabled("(%zu line%s)", log.size(), log.size() == 1 ? "" : "s");

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
    if (log.size() != cmd.commandLogLastSizeForAutoscroll) {
      cmd.commandLogLastSizeForAutoscroll = log.size();
      ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
  } else if (cmd.cmdConsoleOpen) {
    // --- F2 expanded console: selectable + copyable (drag-select then Ctrl+C), scrollable, stays open. ---
    // Width fills the (fixed-width) bar window so the console matches the command line exactly.
    float consoleH = cmd.cmdConsoleHeight > 1.f ? cmd.cmdConsoleHeight : ImGui::GetTextLineHeightWithSpacing() * 15.f;

    // Top-edge height grip: drag up/down to resize the console (the bar stays bottom-pinned).
    ImGui::InvisibleButton("##cb_hgrip", ImVec2(std::max(40.f, ImGui::GetContentRegionAvail().x), 6.f));
    const bool hhov = ImGui::IsItemHovered() || ImGui::IsItemActive();
    if (hhov) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    if (ImGui::IsItemActive()) consoleH -= ImGui::GetIO().MouseDelta.y;  // drag up = taller
    consoleH = std::clamp(consoleH, ImGui::GetTextLineHeightWithSpacing() * 3.f, 1200.f);
    cmd.cmdConsoleHeight = consoleH;
    {
      const ImVec2 gp = ImGui::GetItemRectMin(), gq = ImGui::GetItemRectMax();
      const ImU32 gc = hhov ? IM_COL32(150, 190, 240, 255) : IM_COL32(150, 155, 165, 160);
      const float gy = (gp.y + gq.y) * 0.5f, gmx = (gp.x + gq.x) * 0.5f;
      ImGui::GetWindowDrawList()->AddLine(ImVec2(gmx - 18.f, gy), ImVec2(gmx + 18.f, gy), gc, 1.4f);
    }

    ImGui::PushStyleColor(ImGuiCol_FrameBg, isDark ? Hex(0x282828, cmd.cmdBarOpacity)  // the field step
                                                   : ImVec4(0.235f, 0.235f, 0.235f, cmd.cmdBarOpacity));
    ImGui::InputTextMultiline("##CmdConsole", cmd.commandLogCacheBytes.data(), cmd.commandLogCacheBytes.size(),
                              ImVec2(-FLT_MIN, consoleH), ImGuiInputTextFlags_ReadOnly);
    ImGui::PopStyleColor();
  } else {
    // --- Recent-history chips floating above the bar; fade out after the idle delay. ---
    const float alpha = cmdbar::HistoryAlpha(ImGui::GetTime() - cmd.cmdLogLastChangeTime,
                                             static_cast<double>(cmd.cmdBarFadeDelaySec), 0.8);
    if (alpha > 0.004f && !log.empty()) {
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const size_t startIx = cmdbar::LogTailStart(log.size(), cmd.cmdBarHistoryLines);
      for (size_t i = startIx; i < log.size(); ++i) {
        const std::string& s = log[i];
        const ImVec2 ts = ImGui::CalcTextSize(s.c_str());
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float padx = 6.f, pady = 2.f;
        dl->AddRectFilled(ImVec2(p.x - padx + 4.f, p.y - pady), ImVec2(p.x + ts.x + padx, p.y + ts.y + pady),
                          ImGui::GetColorU32(ImVec4(0.f, 0.f, 0.f, 0.55f * alpha)), 3.f);
        dl->AddText(p, ImGui::GetColorU32(ImVec4(0.86f, 0.88f, 0.92f, alpha)), s.c_str());
        ImGui::Dummy(ImVec2(ts.x + padx, ts.y + pady));
      }
    }
  }

  if (!floating)
    ImGui::Separator();
  ImGui::PushID("GoSurveyCmdPanel");

  ImGuiIO& io = ImGui::GetIO();
  // Type-to-focus: route queued chars now, but defer the focus to just before the
  // InputText (the floating bar has toolbar icons between here and the field, so a
  // SetKeyboardFocusHere here would land on a grip icon instead of the input).
  bool wantFocusInput = false;
  if (!io.WantTextInput && io.InputQueueCharacters.Size > 0 && !cmdInputOnViewport) {
    RouteQueuedCharsToCmdBuf(cmdBuf, cmdBufSize, io);
    wantFocusInput = true;
  }

  const float inputAvailW = ImGui::GetContentRegionAvail().x;

  // --- Floating bar chrome: background panel + toolbar icons (REQ-040). ---
  const float barIconH = ImGui::GetFrameHeight();
  if (floating) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 rmin = ImGui::GetCursorScreenPos();
    // Bar spans the full (fixed) window width: icons + prompt + input + expand.
    dl->AddRectFilled(ImVec2(rmin.x - 2.f, rmin.y - 2.f),
                      ImVec2(rmin.x + inputAvailW + 2.f, rmin.y + barIconH + 2.f),
                      ImGui::GetColorU32(barBg), barRounding);

    auto iconBtn = [&](const char* id, int kind) -> bool {
      const ImVec2 p = ImGui::GetCursorScreenPos();
      const bool clicked = ImGui::InvisibleButton(id, ImVec2(barIconH, barIconH));
      const bool hov = ImGui::IsItemHovered();
      ImDrawList* d = ImGui::GetWindowDrawList();
      if (hov)
        d->AddRectFilled(p, ImVec2(p.x + barIconH, p.y + barIconH), IM_COL32(255, 255, 255, 30), 3.f);
      const ImU32 c = IM_COL32(205, 210, 220, 255);
      const float cx = p.x + barIconH * 0.5f, cy = p.y + barIconH * 0.5f, r = barIconH * 0.22f;
      switch (kind) {
      case 0:  // drag grip (two columns of dots)
        for (int gx = 0; gx < 2; ++gx)
          for (int gy = 0; gy < 3; ++gy)
            d->AddCircleFilled(ImVec2(cx - 2.f + gx * 4.f, cy - 4.f + gy * 4.f), 1.1f, c);
        break;
      case 1:  // close ×
        d->AddLine(ImVec2(cx - r, cy - r), ImVec2(cx + r, cy + r), c, 1.6f);
        d->AddLine(ImVec2(cx - r, cy + r), ImVec2(cx + r, cy - r), c, 1.6f);
        break;
      case 2:  // settings gear
        d->AddCircle(ImVec2(cx, cy), r + 1.f, c, 8, 1.4f);
        d->AddCircleFilled(ImVec2(cx, cy), 1.6f, c);
        break;
      case 3:  // prompt ">" + history caret
        d->AddLine(ImVec2(cx - r - 1.f, cy - r), ImVec2(cx, cy), c, 1.4f);
        d->AddLine(ImVec2(cx, cy), ImVec2(cx - r - 1.f, cy + r), c, 1.4f);
        d->AddTriangleFilled(ImVec2(cx + r - 1.f, cy - 1.f), ImVec2(cx + r + 3.f, cy - 1.f),
                             ImVec2(cx + r + 1.f, cy + 2.f), c);
        break;
      case 4:  // expand ▲
        d->AddTriangleFilled(ImVec2(cx - r, cy + r * 0.6f), ImVec2(cx + r, cy + r * 0.6f),
                             ImVec2(cx, cy - r * 0.8f), c);
        break;
      }
      return clicked;
    };

    iconBtn("##cb_grip", 0);
    if (ImGui::IsItemActive()) cmd.cmdBarAnchorX += io.MouseDelta.x;  // X only — the bar is bottom-locked
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    ItemHelpTooltip("Drag to slide the command line left/right");
    ImGui::SameLine(0, 2);
    if (iconBtn("##cb_close", 1)) cmd.cmdBarVisible = false;
    ItemHelpTooltip("Hide the command line (Ctrl+9 restores)");
    ImGui::SameLine(0, 2);
    if (iconBtn("##cb_cfg", 2)) ImGui::OpenPopup("##cmdBarCfg");
    ItemHelpTooltip("Command line settings");
    ImGui::SameLine(0, 2);
    if (iconBtn("##cb_hist", 3)) ImGui::OpenPopup("##cmdBarHist");
    ItemHelpTooltip("Recent commands");
    ImGui::SameLine(0, 4);
  }

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

  // The floating bar always shows its input + expand chevron (so neither flickers as the
  // cursor crosses onto the drawing); the classic dock swaps in a "follows the cursor"
  // note while the at-crosshair dynamic input is active. Either way only the focused field
  // receives keystrokes, so there is no double routing.
  if (!cmdInputOnViewport || floating) {
    ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue |
                                ImGuiInputTextFlags_CallbackAlways |
                                ImGuiInputTextFlags_CallbackCompletion |  // Tab completes to highlighted
                                ImGuiInputTextFlags_EscapeClearsAll;      // Esc clears the buffer (unfreezes crosshair)
    float inputW;
    const bool activeHint = floating && cmd.active != AppCommandState::Kind::None;
    if (floating) {
      // The active command's hint is the prompt — rendered inline with clickable [option]
      // links (REQ-040), replacing the plain placeholder. Idle shows "Type a command".
      if (activeHint) {
        ImGui::AlignTextToFramePadding();
        // wrapW 0 = never wrap: the floating bar is a single line by design (REQ-040).
        LayoutCommandHint(CommandInputHint(cmd), cmd, log, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled),
                          0.f, /*draw=*/true);
        ImGui::SameLine(0, 6);
      }
      inputW = std::max(80.f, ImGui::GetContentRegionAvail().x - barIconH - 19.f);  // reserve chevron + width grip
    } else {
      ImGui::PushStyleColor(ImGuiCol_Text, promptColor);
      ImGui::TextUnformatted(">");
      ImGui::PopStyleColor();
      const float promptW = ImGui::GetItemRectSize().x + st.ItemSpacing.x * 0.5f;
      ImGui::SameLine(0, st.ItemSpacing.x * 0.5f);
      inputW = std::max(64.f, inputAvailW - sendBtnW - st.ItemSpacing.x - promptW);
    }
    ImGui::SetNextItemWidth(inputW);
    if (wantFocusInput) ImGui::SetKeyboardFocusHere();
    const char* placeholder = floating ? (activeHint ? "" : "Type a command") : CommandInputHint(cmd);
    if (floating) {
      // White input field with near-black text (placeholder a mid-gray so it reads on white).
      ImGui::PushStyleColor(ImGuiCol_FrameBg,        ImVec4(0.97f, 0.97f, 0.97f, 1.f));
      ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(1.00f, 1.00f, 1.00f, 1.f));
      ImGui::PushStyleColor(ImGuiCol_FrameBgActive,  ImVec4(1.00f, 1.00f, 1.00f, 1.f));
      ImGui::PushStyleColor(ImGuiCol_Text,           ImVec4(0.08f, 0.08f, 0.08f, 1.f));
      ImGui::PushStyleColor(ImGuiCol_TextDisabled,   ImVec4(0.45f, 0.45f, 0.45f, 1.f));
    }
    bool exec = ImGui::InputTextWithHint("##CommandLineInput", placeholder, cmdBuf,
                                         static_cast<size_t>(cmdBufSize), flags, CommandLineInputCallback, nullptr);
    if (floating) ImGui::PopStyleColor(5);
    cmdInputMin = ImGui::GetItemRectMin();
    cmdInputMax = ImGui::GetItemRectMax();
    const bool inputActive = ImGui::IsItemActive();
    ImGui::SetItemDefaultFocus();
    if (floating) {
      ImGui::SameLine(0, 4);
      const ImVec2 ep = ImGui::GetCursorScreenPos();
      if (ImGui::InvisibleButton("##cb_expand", ImVec2(barIconH, barIconH))) cmd.cmdConsoleOpen = !cmd.cmdConsoleOpen;
      const bool ehov = ImGui::IsItemHovered();
      ImDrawList* d = ImGui::GetWindowDrawList();
      if (ehov) d->AddRectFilled(ep, ImVec2(ep.x + barIconH, ep.y + barIconH), IM_COL32(255, 255, 255, 30), 3.f);
      const ImU32 ec = IM_COL32(205, 210, 220, 255);
      const float ecx = ep.x + barIconH * 0.5f, ecy = ep.y + barIconH * 0.5f, er = barIconH * 0.22f;
      // ▲ when collapsed (expand), ▼ when the console is open (collapse).
      if (cmd.cmdConsoleOpen)
        d->AddTriangleFilled(ImVec2(ecx - er, ecy - er * 0.6f), ImVec2(ecx + er, ecy - er * 0.6f),
                             ImVec2(ecx, ecy + er * 0.8f), ec);
      else
        d->AddTriangleFilled(ImVec2(ecx - er, ecy + er * 0.6f), ImVec2(ecx + er, ecy + er * 0.6f),
                             ImVec2(ecx, ecy - er * 0.8f), ec);
      ItemHelpTooltip("Expand/collapse the command history (F2)");

      // Right-edge width grip: drag to resize the bar's width.
      ImGui::SameLine(0, 4);
      const ImVec2 gp = ImGui::GetCursorScreenPos();
      ImGui::InvisibleButton("##cb_wgrip", ImVec2(7.f, barIconH));
      const bool whov = ImGui::IsItemHovered() || ImGui::IsItemActive();
      if (whov) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
      if (ImGui::IsItemActive()) cmd.cmdBarWidth = std::max(320.f, cmd.cmdBarWidth + io.MouseDelta.x);
      {
        const ImU32 gc = whov ? IM_COL32(150, 190, 240, 255) : IM_COL32(150, 155, 165, 200);
        const float gcx = gp.x + 3.5f;
        d->AddLine(ImVec2(gcx, gp.y + 3.f), ImVec2(gcx, gp.y + barIconH - 3.f), gc, 1.4f);
      }
      ItemHelpTooltip("Drag to resize the command line width");
    }

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
    // Suggestions (and the Enter-time cmdBuf rewrite below) are for IDLE top-level command entry
    // only. While a command is active and prompting for a typed sub-answer (FILLET's R/T, CHAMFER's
    // D/A/T, LENGTHEN's DE/P/T/DY, a numeric value, ...), the typed text must reach that command's
    // own handler verbatim — a real bug, found from a user report: typing "r" for FILLET's Radius
    // sub-command fuzzy-matched some unrelated top-level command (e.g. "rect"/"redo") and the Enter
    // frame silently substituted THAT name into cmdBuf before submission, so FILLET's own handler
    // never saw "r" at all and refused with "could not be parsed" — reproducible for any short
    // sub-answer that happens to fuzzy-match a registry entry, not specific to FILLET's own code.
    const bool cmdIdle = cmd.active == AppCommandState::Kind::None;
    if (cmdIdle && inputActive && !query.empty() && singleToken && !s_cmdDismissed) {
      cmdSug = FuzzyCommandSuggestions(query, 20);
      s_cmdSugCache = cmdSug;
    } else if (cmdIdle && overCmdSugPopup && !s_cmdDismissed && !s_cmdSugCache.empty()) {
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
      DevShell_OnCommand(cmdBuf);
      ProcessCommandLineSubmit(cmdBuf, cmdBufSize, cmd, log);
    }
  } else {
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Command input follows the cursor on the drawing (viewport).");
  }

  // Floating bar: the prompt lives in the input field's placeholder (CommandInputHint),
  // so the separate footer-hint lines are suppressed — the bar stays a clean single line.
  // Classic dock keeps the wrapped hint lines below the input.
  //
  // REQ-119: a hint carrying variant markup goes through the SHARED renderer — the same one
  // the floating bar uses — so no command needs click handling of its own. LINE used to have
  // a hand-rolled copy of this here, with [A]/[2P] and their tokens spelled out literally;
  // it was deleted because it was exactly the per-command duplication #81 asks us to avoid,
  // and because it could only ever cover the one prompt someone remembered to hardcode.
  auto renderHint = [&](const char* s) {
    if (floating || !s || !s[0]) return;
    const ImVec4 dim = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    if (std::strchr(s, '[')) {
      LayoutCommandHint(s, cmd, log, dim, wrapW, /*draw=*/true);
      return;
    }
    ImGui::PushStyleColor(ImGuiCol_Text, dim);
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

  // --- Command-bar popups (REQ-040): settings (wrench) and recent-command history (>_ ▾). ---
  if (ImGui::BeginPopup("##cmdBarCfg")) {
    ImGui::TextDisabled("Command line");
    ImGui::Separator();
    ImGui::SetNextItemWidth(160.f);
    ImGui::SliderFloat("Fade delay (s)", &cmd.cmdBarFadeDelaySec, 0.5f, 30.f, "%.1f");
    ImGui::SetNextItemWidth(160.f);
    ImGui::SliderFloat("Opacity", &cmd.cmdBarOpacity, 0.3f, 1.f, "%.2f");
    ImGui::SetNextItemWidth(160.f);
    ImGui::SliderInt("History lines", &cmd.cmdBarHistoryLines, 1, 10);
    ImGui::Separator();
    bool classic = cmd.cmdLineClassicDock;
    if (ImGui::Checkbox("Use classic docked panel", &classic))
      cmd.cmdLineClassicDock = classic;
    ImGui::EndPopup();
  }
  if (ImGui::BeginPopup("##cmdBarHist")) {
    if (cmd.cmdEnteredHistory.empty()) {
      ImGui::TextDisabled("(no recent commands)");
    } else {
      // Newest first; clicking re-runs the entry.
      for (size_t k = cmd.cmdEnteredHistory.size(); k-- > 0;) {
        const std::string& h = cmd.cmdEnteredHistory[k];
        ImGui::PushID(static_cast<int>(k));
        if (ImGui::Selectable(h.c_str())) {
          std::snprintf(cmdBuf, static_cast<size_t>(cmdBufSize), "%s", h.c_str());
          ProcessCommandLineSubmit(cmdBuf, cmdBufSize, cmd, log);
        }
        ImGui::PopID();
      }
    }
    ImGui::EndPopup();
  }

  ImGui::PopID();

  // FramePadding was pushed AFTER Begin (inside the window) so it must be popped BEFORE End;
  // WindowPadding + WindowBorderSize were pushed BEFORE Begin so they pop after End.
  if (floating) ImGui::PopStyleVar();  // FramePadding
  ImGui::End();
  ImGui::PopStyleColor();
  if (floating) ImGui::PopStyleVar(2);  // WindowPadding + WindowBorderSize

  // --- nanoCAD-style command autocomplete popup (anchored at the drawing crosshair) ---
  s_cmdSugPopupOpen = false;
  if (cmdShowSug && !cmdSug.empty()) {
    const float rowH  = 40.f;
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
    ImGui::PushStyleColor(ImGuiCol_WindowBg, g_chrome.popupFace);
    ImGui::PushStyleColor(ImGuiCol_Border,  g_chrome.popupBorder);
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

static ImFont* ResolveCadTtf(const std::string& family, bool bold, bool italic, ImFont* fallback, bool* realBold,
                             bool* realItalic) {
  if (family.empty())
    return fallback;
  bool rb = false, ri = false;
  ImFont* tf = FontReg::Resolve(family, bold, italic, &rb, &ri);
  if (realBold)
    *realBold = rb;
  if (realItalic)
    *realItalic = ri;
  return tf ? tf : fallback;
}

static void AddAlignedDimText(ImDrawList* dl, ImFont* font, float fontPx, const ImVec2& pivotSp, float screenAngRad,
                              ImU32 textCol, const char* text) {
  if (!dl || !text || !text[0])
    return;
  if (!font)
    font = ImGui::GetFont();
  const ImVec2 ts = font->CalcTextSizeA(fontPx, FLT_MAX, 0.f, text);
  const int v0 = dl->VtxBuffer.Size;
  dl->AddText(font, fontPx, ImVec2(pivotSp.x - ts.x * 0.5f, pivotSp.y - ts.y * 0.5f), textCol, text);
  const int v1 = dl->VtxBuffer.Size;
  RotateImDrawListVertsXY(dl, v0, v1, pivotSp, std::cos(screenAngRad), std::sin(screenAngRad));
}

/// Dimension label: SHX strokes when the style names a stroke font (unless the string needs °), else TTF.
static void DrawDimLabelText(ImDrawList* dl, const CadAnnotation& a, ImFont* fallback, float fontPx,
                             const ImVec2& pivotSp, float screenAngRad, ImU32 textCol) {
  if (!dl || a.text.empty())
    return;
  const std::string fam = CadDrawFontFamily(a.fontFamily);
  if (cadfont::PreferShxStrokes(fam, a.text)) {
    Shx::Font* sf = Shx::Resolve(fam);
    if (sf && sf->valid()) {
      const float thick = std::max(1.f, fontPx * 0.05f);
      const float w = Shx::MeasureWidthPx(*sf, a.text, fontPx);
      const ImVec2 tl(pivotSp.x - w * 0.5f, pivotSp.y - fontPx * 0.5f);
      const int v0 = dl->VtxBuffer.Size;
      Shx::DrawText(dl, *sf, ImVec2(tl.x, tl.y + fontPx), fontPx, 0.f, textCol, a.text, thick);
      RotateImDrawListVertsXY(dl, v0, dl->VtxBuffer.Size, pivotSp, std::cos(screenAngRad),
                              std::sin(screenAngRad));
      return;
    }
  }
  ImFont* dimFont = ResolveCadTtf(fam, false, false, fallback, nullptr, nullptr);
  AddAlignedDimText(dl, dimFont, fontPx, pivotSp, screenAngRad, textCol, a.text.c_str());
}

static void RotateDrawListVertsAround(ImDrawList* dl, int vtx0, ImVec2 pivot, float rotationRad) {
  if (!dl || rotationRad == 0.f)
    return;
  const float ang = -rotationRad;
  const float ca = std::cos(ang);
  const float sa = std::sin(ang);
  for (int vi = vtx0; vi < dl->VtxBuffer.Size; ++vi) {
    ImVec2& p = dl->VtxBuffer[static_cast<size_t>(vi)].pos;
    const float dx = p.x - pivot.x;
    const float dy = p.y - pivot.y;
    p.x = pivot.x + dx * ca - dy * sa;
    p.y = pivot.y + dx * sa + dy * ca;
  }
}

static void DrawCadSingleLineText(ImDrawList* dl, const CadAnnotation& a, ImFont* fallback, ImVec2 topLeft,
                                  float fontPx, ImU32 col) {
  if (!dl || a.text.empty())
    return;
  const int vtx0 = dl->VtxBuffer.Size;
  const std::string fam = CadDrawFontFamily(a.fontFamily);
  Shx::Font* sf = CadIsShxFontName(fam) ? Shx::Resolve(fam) : nullptr;
  if (sf && sf->valid()) {
    const float thick = std::max(1.f, fontPx * 0.05f);
    const ImVec2 baselinePt(topLeft.x, topLeft.y + fontPx);
    Shx::DrawText(dl, *sf, baselinePt, fontPx, 0.f, col, a.text, thick);
    if (a.underline) {
      const float w = Shx::MeasureWidthPx(*sf, a.text, fontPx);
      const float uy = baselinePt.y + std::max(1.5f, fontPx * 0.12f);
      dl->AddLine(ImVec2(baselinePt.x, uy), ImVec2(baselinePt.x + w, uy), col, thick);
    }
  } else {
    bool realBold = false, realItalic = false;
    ImFont* tf = ResolveCadTtf(fam, a.bold, a.italic, fallback, &realBold, &realItalic);
    const ImVec2 ext = tf->CalcTextSizeA(fontPx, FLT_MAX, 0.f, a.text.c_str());
    if (a.bold && !realBold) {
      dl->AddText(tf, fontPx, ImVec2(topLeft.x + 0.6f, topLeft.y), col, a.text.c_str());
      dl->AddText(tf, fontPx, ImVec2(topLeft.x - 0.6f, topLeft.y), col, a.text.c_str());
    }
    if (a.italic && !realItalic)
      dl->AddText(tf, fontPx, ImVec2(topLeft.x + 0.4f, topLeft.y), col, a.text.c_str());
    dl->AddText(tf, fontPx, topLeft, col, a.text.c_str());
    if (a.underline) {
      const float uy = topLeft.y + ext.y - std::max(1.f, fontPx * 0.07f);
      dl->AddLine(ImVec2(topLeft.x, uy), ImVec2(topLeft.x + ext.x, uy), col, std::max(1.f, fontPx * 0.06f));
    }
  }
  RotateDrawListVertsAround(dl, vtx0, topLeft, a.rotationRad);
}

template <typename WorldToScreen>
static void DrawCadDimStrokesOnDrawList(ImDrawList* dl, const CadAnnotation& a, const CadDimWorldStrokes& strokes,
                                        WorldToScreen wts, float fontPx, ImU32 extCol, ImU32 lineCol, ImU32 arrowCol,
                                        ImU32 textCol, ImFont* font, DimArrowType arrowType) {
  if (!dl)
    return;
  const bool drawFilledArrows = !strokes.arrows.empty();
  for (const CadDimWorldSeg& seg : strokes.segs) {
    if (drawFilledArrows && seg.kind == CadDimWorldSeg::Kind::Arrow)
      continue;
    const ImVec2 s0 = wts(seg.x0, seg.y0);
    const ImVec2 s1 = wts(seg.x1, seg.y1);
    const ImU32 c = (seg.kind == CadDimWorldSeg::Kind::Extension) ? extCol
                    : (seg.kind == CadDimWorldSeg::Kind::Arrow)     ? arrowCol
                                                                   : lineCol;
    dl->AddLine(s0, s1, c, 1.2f);
  }
  for (const CadDimWorldTri& tri : strokes.arrows) {
    const ImVec2 t0 = wts(tri.x0, tri.y0);
    const ImVec2 t1 = wts(tri.x1, tri.y1);
    const ImVec2 t2 = wts(tri.x2, tri.y2);
    if (arrowType == DimArrowType::ClosedBlank || arrowType == DimArrowType::Open)
      dl->AddTriangle(t0, t1, t2, arrowCol, 1.2f);
    else
      dl->AddTriangleFilled(t0, t1, t2, arrowCol);
  }
  if (a.text.empty())
    return;
  const ImVec2 sp = wts(strokes.labelX, strokes.labelY);
  const float dirStep = 0.05f;
  const ImVec2 spDir = wts(strokes.labelX + std::cos(strokes.labelRotRad) * dirStep,
                           strokes.labelY + std::sin(strokes.labelRotRad) * dirStep);
  const float screenAng = std::atan2(spDir.y - sp.y, spDir.x - sp.x);
  DrawDimLabelText(dl, a, font, fontPx, sp, screenAng, textCol);
}

static int HitTestDimGrip(float mouseSx, float mouseSy, ImVec2 imgPos, ImVec2 avail, const Camera& cam,
                          const CadAnnotation& ann, float gripRadiusPx) {
  if (ann.kind != CadAnnotation::Kind::DimAligned && ann.kind != CadAnnotation::Kind::DimLinear)
    return -1;
  float sx1 = 0.f, sy1 = 0.f, sx2 = 0.f, sy2 = 0.f, tx = 0.f, ty = 0.f, nx = 0.f, ny = 0.f, meas = 0.f;
  if (!CadDimAnyGeometry(ann, &sx1, &sy1, &sx2, &sy2, &tx, &ty, &nx, &ny, &meas))
    return -1;
  const float wx[5] = {ann.dimExt1X, ann.dimExt2X, sx1, sx2, ann.insX};
  const float wy[5] = {ann.dimExt1Y, ann.dimExt2Y, sy1, sy2, ann.insY};
  const float r2 = gripRadiusPx * gripRadiusPx;
  // Camera-projected so the grip targets stay under the grips as drawn (REQ-058); in plan view
  // this reduces to the previous linear mapping exactly.
  for (int i = 4; i >= 0; --i) {
    float sx = 0.f, sy = 0.f;
    cam.WorldToScreen(static_cast<double>(wx[i]), static_cast<double>(wy[i]), static_cast<double>(ann.insZ),
                      avail.x, avail.y, &sx, &sy);
    sx += imgPos.x;
    sy += imgPos.y;
    const float dx = mouseSx - sx;
    const float dy = mouseSy - sy;
    if (dx * dx + dy * dy <= r2)
      return i;
  }
  return -1;
}

static int HitTestMtextGrip(float mouseSx, float mouseSy, ImVec2 imgPos, ImVec2 avail, const Camera& cam,
                            const CadAnnotation& ann, float gripRadiusPx) {
  if (!CadAnnotationHasTextBox(ann.kind))
    return -1;
  const float r2 = gripRadiusPx * gripRadiusPx;
  // Camera-projected — see HitTestDimGrip.
  auto toScreen = [&](float wx, float wy, float* sx, float* sy) {
    cam.WorldToScreen(static_cast<double>(wx), static_cast<double>(wy), static_cast<double>(ann.insZ), avail.x,
                      avail.y, sx, sy);
    *sx += imgPos.x;
    *sy += imgPos.y;
  };
  if (ann.surveyPointLabelForId >= 0) {
    float sx = 0.f, sy = 0.f;
    toScreen(0.5f * (ann.boxMinX + ann.boxMaxX), 0.5f * (ann.boxMinY + ann.boxMaxY), &sx, &sy);
    const float dx = mouseSx - sx;
    const float dy = mouseSy - sy;
    if (dx * dx + dy * dy <= r2)
      return 4;
    return -1;
  }
  const float wx[4] = {ann.boxMinX, ann.boxMaxX, ann.boxMaxX, ann.boxMinX};
  const float wy[4] = {ann.boxMinY, ann.boxMinY, ann.boxMaxY, ann.boxMaxY};
  for (int i = 0; i < 4; ++i) {
    float sx = 0.f, sy = 0.f;
    toScreen(wx[i], wy[i], &sx, &sy);
    const float dx = mouseSx - sx;
    const float dy = mouseSy - sy;
    if (dx * dx + dy * dy <= r2)
      return i;
  }
  return -1;
}

// ===================== MTEXT "Text Formatting" panel (REQ-051) =====================
// The AutoCAD/nanoCAD-style MTEXT editor: a floating, draggable two-row toolbar whose position and
// ruler/expanded state persist (UserPrefs, the REQ-040 cmdBar* pattern), over the in-place edit box with
// a column ruler. Controls the stored text model already supports are live — text style, per-selection
// font and colour ([[font:…]]/[[color:…]] run tags), B/I/U, uppercase, symbol, justification
// (mtextAttach), and whole-object height/oblique/entity colour. Every other control is drawn disabled and
// names itself in a tooltip, so the panel never implies a capability the drawing cannot store; each is a
// recorded REQ-051 follow-up. Pure geometry/string decisions live in ui/MtextToolbar.hpp (tested).

static void DrawTextStyleSample(ImDrawList* dl, ImVec2 tl, ImVec2 sz, const TextStyle& s, const char* sample,
                                ImU32 col);  // defined with the STYLE dialog, below

/// Tooltip that still shows on a *disabled* control — how every not-yet-implemented button names itself.
static void MtextTbTip(const char* text) {
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort))
    ImGui::SetTooltip("%s", text);
}

/// Pictographic toolbar glyphs. Drawn from primitives (the command-bar iconBtn precedent) rather than font
/// characters, so the panel does not depend on the loaded font covering ¶, ↔, arrows, and the like.
enum class MtextTbGlyph {
  Annotative, Mask, Undo, Redo, Stack, Ruler, ExpandDown, ExpandUp, Columns, Attach, Paragraph,
  AlignLeft, AlignCenter, AlignRight, AlignJust, AlignDist, LineSpacing, Lists, Field, Oblique,
  Tracking, WidthFactor,
};

static void MtextTbDrawGlyph(ImDrawList* d, ImVec2 c, float box, ImU32 col, MtextTbGlyph g) {
  const float h = box * 0.5f;  // half-extent the glyph is drawn within
  // A horizontal rule at vertical fraction \p yf spanning horizontal fractions [x0f,x1f] of the box.
  auto bar = [&](float yf, float x0f, float x1f) {
    const float y = c.y + h * yf;
    d->AddLine(ImVec2(c.x + h * x0f, y), ImVec2(c.x + h * x1f, y), col, 1.4f);
  };
  // The four-line text block shared by the paragraph-alignment family.
  auto lines4 = [&](float a0, float a1, float b0, float b1) {
    bar(-0.75f, a0, a1);
    bar(-0.25f, b0, b1);
    bar(0.25f, a0, a1);
    bar(0.75f, b0, b1);
  };
  auto arrowUp = [&](float x, float yTop, float yBot) {
    d->AddLine(ImVec2(x, yTop), ImVec2(x, yBot), col, 1.3f);
    d->AddLine(ImVec2(x, yTop), ImVec2(x - 2.5f, yTop + 3.f), col, 1.3f);
    d->AddLine(ImVec2(x, yTop), ImVec2(x + 2.5f, yTop + 3.f), col, 1.3f);
  };
  switch (g) {
  case MtextTbGlyph::Annotative:  // a triangular "annotation scale" marker
    d->AddTriangle(ImVec2(c.x, c.y - h * 0.8f), ImVec2(c.x - h * 0.8f, c.y + h * 0.7f),
                   ImVec2(c.x + h * 0.8f, c.y + h * 0.7f), col, 1.3f);
    break;
  case MtextTbGlyph::Mask:  // background mask: a filled swatch behind a rule
    d->AddRectFilled(ImVec2(c.x - h * 0.8f, c.y - h * 0.2f), ImVec2(c.x + h * 0.8f, c.y + h * 0.8f), col);
    bar(-0.7f, -0.8f, 0.8f);
    break;
  case MtextTbGlyph::Undo:
  case MtextTbGlyph::Redo: {
    const bool fwd = (g == MtextTbGlyph::Redo);
    const float a0 = fwd ? 3.6f : 5.8f, a1 = fwd ? 5.9f : 8.1f;  // half-arc, mirrored
    d->PathArcTo(ImVec2(c.x, c.y + h * 0.25f), h * 0.7f, a0, a1, 12);
    d->PathStroke(col, 0, 1.4f);
    const float tipX = c.x + (fwd ? h * 0.7f : -h * 0.7f);
    d->AddTriangleFilled(ImVec2(tipX, c.y - h * 0.75f), ImVec2(tipX - 3.f, c.y - h * 0.15f),
                         ImVec2(tipX + 3.f, c.y - h * 0.15f), col);
    break;
  }
  case MtextTbGlyph::Stack:  // stacked fraction: two rules split by a divider
    bar(-0.6f, -0.5f, 0.5f);
    bar(0.f, -0.8f, 0.8f);
    bar(0.6f, -0.5f, 0.5f);
    break;
  case MtextTbGlyph::Ruler:  // a ruler strip with ticks
    d->AddRect(ImVec2(c.x - h * 0.9f, c.y - h * 0.45f), ImVec2(c.x + h * 0.9f, c.y + h * 0.45f), col, 0.f, 0, 1.2f);
    for (int i = -1; i <= 1; ++i)
      d->AddLine(ImVec2(c.x + h * 0.45f * static_cast<float>(i), c.y - h * 0.45f),
                 ImVec2(c.x + h * 0.45f * static_cast<float>(i), c.y), col, 1.1f);
    break;
  case MtextTbGlyph::ExpandDown:
    d->AddTriangleFilled(ImVec2(c.x - h * 0.6f, c.y - h * 0.3f), ImVec2(c.x + h * 0.6f, c.y - h * 0.3f),
                         ImVec2(c.x, c.y + h * 0.5f), col);
    break;
  case MtextTbGlyph::ExpandUp:
    d->AddTriangleFilled(ImVec2(c.x - h * 0.6f, c.y + h * 0.3f), ImVec2(c.x + h * 0.6f, c.y + h * 0.3f),
                         ImVec2(c.x, c.y - h * 0.5f), col);
    break;
  case MtextTbGlyph::Columns:  // two text columns
    for (int i = 0; i < 2; ++i) {
      const float x = c.x + (i ? h * 0.15f : -h * 0.85f);
      d->AddRect(ImVec2(x, c.y - h * 0.8f), ImVec2(x + h * 0.7f, c.y + h * 0.8f), col, 0.f, 0, 1.2f);
    }
    break;
  case MtextTbGlyph::Attach:  // justification: a box with its active corner marked
    d->AddRect(ImVec2(c.x - h * 0.85f, c.y - h * 0.85f), ImVec2(c.x + h * 0.85f, c.y + h * 0.85f), col, 0.f, 0,
               1.2f);
    d->AddRectFilled(ImVec2(c.x - h * 0.7f, c.y - h * 0.7f), ImVec2(c.x - h * 0.2f, c.y - h * 0.35f), col);
    break;
  case MtextTbGlyph::Paragraph:  // ¶
    d->AddCircle(ImVec2(c.x - h * 0.1f, c.y - h * 0.3f), h * 0.45f, col, 10, 1.3f);
    d->AddLine(ImVec2(c.x + h * 0.35f, c.y - h * 0.75f), ImVec2(c.x + h * 0.35f, c.y + h * 0.85f), col, 1.3f);
    d->AddLine(ImVec2(c.x - h * 0.1f, c.y + h * 0.15f), ImVec2(c.x - h * 0.1f, c.y + h * 0.85f), col, 1.3f);
    break;
  case MtextTbGlyph::AlignLeft:    lines4(-0.85f, 0.85f, -0.85f, 0.2f); break;
  case MtextTbGlyph::AlignCenter:  lines4(-0.85f, 0.85f, -0.5f, 0.5f); break;
  case MtextTbGlyph::AlignRight:   lines4(-0.85f, 0.85f, -0.2f, 0.85f); break;
  case MtextTbGlyph::AlignJust:    lines4(-0.85f, 0.85f, -0.85f, 0.85f); break;
  case MtextTbGlyph::AlignDist:
    lines4(-0.85f, 0.85f, -0.85f, 0.85f);
    d->AddLine(ImVec2(c.x, c.y - h * 0.95f), ImVec2(c.x, c.y + h * 0.95f), col, 1.f);
    break;
  case MtextTbGlyph::LineSpacing:  // vertical double arrow beside text rules
    arrowUp(c.x - h * 0.6f, c.y - h * 0.85f, c.y + h * 0.85f);
    bar(-0.75f, 0.f, 0.85f);
    bar(0.f, 0.f, 0.85f);
    bar(0.75f, 0.f, 0.85f);
    break;
  case MtextTbGlyph::Lists:  // bulleted rules
    for (int i = -1; i <= 1; ++i) {
      const float yf = static_cast<float>(i) * 0.7f;
      d->AddCircleFilled(ImVec2(c.x - h * 0.7f, c.y + h * yf), 1.4f, col);
      bar(yf, -0.35f, 0.85f);
    }
    break;
  case MtextTbGlyph::Field:  // a field placeholder box
    d->AddRect(ImVec2(c.x - h * 0.85f, c.y - h * 0.5f), ImVec2(c.x + h * 0.85f, c.y + h * 0.5f), col, 0.f, 0,
               1.2f);
    bar(0.f, -0.5f, 0.5f);
    break;
  case MtextTbGlyph::Oblique:  // a slanted stroke
    d->AddLine(ImVec2(c.x - h * 0.45f, c.y + h * 0.8f), ImVec2(c.x + h * 0.6f, c.y - h * 0.8f), col, 1.5f);
    break;
  case MtextTbGlyph::Tracking:  // a↔b letter spacing
  case MtextTbGlyph::WidthFactor: {
    const float y = c.y + (g == MtextTbGlyph::Tracking ? h * 0.55f : 0.f);
    d->AddLine(ImVec2(c.x - h * 0.85f, y), ImVec2(c.x + h * 0.85f, y), col, 1.3f);
    d->AddLine(ImVec2(c.x - h * 0.85f, y), ImVec2(c.x - h * 0.45f, y - 3.f), col, 1.3f);
    d->AddLine(ImVec2(c.x - h * 0.85f, y), ImVec2(c.x - h * 0.45f, y + 3.f), col, 1.3f);
    d->AddLine(ImVec2(c.x + h * 0.85f, y), ImVec2(c.x + h * 0.45f, y - 3.f), col, 1.3f);
    d->AddLine(ImVec2(c.x + h * 0.85f, y), ImVec2(c.x + h * 0.45f, y + 3.f), col, 1.3f);
    if (g == MtextTbGlyph::Tracking)
      bar(-0.6f, -0.85f, 0.85f);
    break;
  }
  }
}

/// Square glyph button sized to the toolbar row. \p on draws it pressed (a toggle that is currently set).
static bool MtextTbIconButton(const char* id, MtextTbGlyph g, bool on = false) {
  const float box = ImGui::GetFrameHeight();
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const bool clicked = ImGui::InvisibleButton(id, ImVec2(box, box));
  const bool hov = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
  const bool held = ImGui::IsItemActive();
  ImDrawList* d = ImGui::GetWindowDrawList();
  if (on || held)
    d->AddRectFilled(p, ImVec2(p.x + box, p.y + box), ImGui::GetColorU32(ImGuiCol_ButtonActive));
  else if (hov)
    d->AddRectFilled(p, ImVec2(p.x + box, p.y + box), ImGui::GetColorU32(ImGuiCol_ButtonHovered));
  MtextTbDrawGlyph(d, ImVec2(p.x + box * 0.5f, p.y + box * 0.5f), box * 0.62f,
                   ImGui::GetColorU32(ImGuiCol_Text), g);
  return clicked;
}

/// Letter button (B, I, U, …) with the toolbar's square footprint, so letters and glyphs line up.
static bool MtextTbLetterButton(const char* id, const char* letter, bool strikeThrough = false,
                                bool overLine = false) {
  const float box = ImGui::GetFrameHeight();
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const bool clicked = ImGui::InvisibleButton(id, ImVec2(box, box));
  ImDrawList* d = ImGui::GetWindowDrawList();
  if (ImGui::IsItemActive())
    d->AddRectFilled(p, ImVec2(p.x + box, p.y + box), ImGui::GetColorU32(ImGuiCol_ButtonActive));
  else if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    d->AddRectFilled(p, ImVec2(p.x + box, p.y + box), ImGui::GetColorU32(ImGuiCol_ButtonHovered));
  const ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
  const ImVec2 ts = ImGui::CalcTextSize(letter);
  const ImVec2 tp(p.x + (box - ts.x) * 0.5f, p.y + (box - ts.y) * 0.5f);
  d->AddText(tp, col, letter);
  if (strikeThrough)
    d->AddLine(ImVec2(tp.x - 1.f, tp.y + ts.y * 0.55f), ImVec2(tp.x + ts.x + 1.f, tp.y + ts.y * 0.55f), col, 1.2f);
  if (overLine)
    d->AddLine(ImVec2(tp.x - 1.f, tp.y + 1.f), ImVec2(tp.x + ts.x + 1.f, tp.y + 1.f), col, 1.2f);
  return clicked;
}

/// Symbols offered by both the toolbar's @ dropdown and the Options menu's Character Set submenu.
static const struct MtextSymbolPick {
  const char* label;
  const char* utf8;
} kMtextSymbolPicks[] = {
    {"\xCF\x80 pi", "\xCF\x80"},                // U+03C0
    {"\xCE\xA3 Sigma", "\xCE\xA3"},             // U+03A3
    {"\xE2\x88\x9E infinity", "\xE2\x88\x9E"},  // U+221E
    {"\xE2\x89\xA4 leq", "\xE2\x89\xA4"},       // U+2264
    {"\xE2\x89\xA5 geq", "\xE2\x89\xA5"},       // U+2265
    {"\xC2\xB1 plus-minus", "\xC2\xB1"},        // U+00B1
    {"\xE2\x88\x9A sqrt", "\xE2\x88\x9A"},      // U+221A
    {"\xE2\x88\xAB integral", "\xE2\x88\xAB"},  // U+222B
    {"\xC3\x97 times", "\xC3\x97"},             // U+00D7
    {"\xC2\xB7 dot", "\xC2\xB7"},               // U+00B7
    {"\xCE\xB1 alpha", "\xCE\xB1"},             // U+03B1
    {"\xCE\xB8 theta", "\xCE\xB8"},             // U+03B8
    {"\xC2\xB0 degrees", "\xC2\xB0"},           // U+00B0
};

/// The column ruler drawn above the in-place box (REQ-051). Visual at this revision: it shows the column's
/// extent and its indent markers; dragging to re-column the text is a recorded follow-up.
static void MtextTbDrawRuler(ImDrawList* d, ImVec2 tl, float w, float h) {
  const ImU32 frame = IM_COL32(96, 96, 96, 255);
  const ImU32 face = IM_COL32(66, 66, 66, 255);
  const ImU32 tick = IM_COL32(200, 205, 212, 255);
  d->AddRectFilled(tl, ImVec2(tl.x + w, tl.y + h), face);
  d->AddRect(tl, ImVec2(tl.x + w, tl.y + h), frame, 0.f, 0, 1.f);
  // Left indent marker (the reference's "L") and the right column-width marker (a diamond).
  const float midY = tl.y + h * 0.5f;
  d->AddLine(ImVec2(tl.x + 5.f, tl.y + 3.f), ImVec2(tl.x + 5.f, tl.y + h - 3.f), tick, 1.4f);
  d->AddLine(ImVec2(tl.x + 5.f, tl.y + h - 3.f), ImVec2(tl.x + 11.f, tl.y + h - 3.f), tick, 1.4f);
  const float dx = tl.x + w - 8.f;
  d->AddQuadFilled(ImVec2(dx, midY - 4.f), ImVec2(dx + 4.f, midY), ImVec2(dx, midY + 4.f),
                   ImVec2(dx - 4.f, midY), tick);
  // Ticks span the column between the two markers; minor every 6px, every 3rd taller.
  const float x0 = tl.x + 16.f;
  const float span = (dx - 6.f) - x0;
  for (const auto& t : mtexttoolbar::RulerTicks(span, 6.f, 3)) {
    const float tickH = t.isMajor ? h * 0.42f : h * 0.24f;
    d->AddLine(ImVec2(x0 + t.offsetPx, midY - tickH), ImVec2(x0 + t.offsetPx, midY + tickH), tick, 1.f);
  }
}

static void DrawTableCellEditorOverlay(AppCommandState& cmd, std::vector<std::string>& log, float worldLeft,
                                       float worldRight, float worldBottom, float worldTop, ImVec2 imgPos,
                                       ImVec2 avail) {
  if (!cmd.tableCellEditorOpen)
    return;
  if (cmd.tableCellEditorIndex < 0 ||
      static_cast<size_t>(cmd.tableCellEditorIndex) >= cmd.cadTables.size()) {
    CancelTableCellEditor(cmd);
    return;
  }
  const CadTable& t = cmd.cadTables[static_cast<size_t>(cmd.tableCellEditorIndex)];
  std::vector<CadTableCellRect> cells;
  CadTableLayoutWorldCells(t, &cells);
  if (cmd.tableCellEditorCell < 0 || static_cast<size_t>(cmd.tableCellEditorCell) >= cells.size()) {
    CancelTableCellEditor(cmd);
    return;
  }
  const CadTableCellRect& cell = cells[static_cast<size_t>(cmd.tableCellEditorCell)];
  const float denx = worldRight - worldLeft;
  const float deny = worldTop - worldBottom;
  if (std::fabs(denx) < 1.e-12f || std::fabs(deny) < 1.e-12f)
    return;
  auto ws = [&](float wx, float wy, ImVec2* o) {
    const float u = (wx - worldLeft) / denx;
    const float v = (worldTop - wy) / deny;
    o->x = imgPos.x + u * avail.x;
    o->y = imgPos.y + v * avail.y;
  };
  ImVec2 p00{}, p01{}, p10{}, p11{};
  ws(cell.x0, cell.y0, &p00);
  ws(cell.x1, cell.y0, &p01);
  ws(cell.x0, cell.y1, &p10);
  ws(cell.x1, cell.y1, &p11);
  const float sx0 = std::min({p00.x, p01.x, p10.x, p11.x});
  const float sx1 = std::max({p00.x, p01.x, p10.x, p11.x});
  const float sy0 = std::min({p00.y, p01.y, p10.y, p11.y});
  const float sy1 = std::max({p00.y, p01.y, p10.y, p11.y});
  const ImVec2 imgMin(imgPos.x, imgPos.y);
  const ImVec2 imgMax(imgPos.x + avail.x, imgPos.y + avail.y);
  const ImGuiStyle& ist0 = ImGui::GetStyle();
  const float boxW = std::clamp(sx1 - sx0 + 8.f, 48.f, imgMax.x - imgMin.x - 4.f);
  const float boxH = std::max(ImGui::GetFrameHeight(), sy1 - sy0);
  const float ex = std::clamp(sx0 - 2.f, imgMin.x + 2.f, imgMax.x - boxW - 2.f);
  const float ey = std::clamp(sy0 - ist0.FramePadding.y, imgMin.y + 2.f, imgMax.y - boxH - 2.f);
  ImGui::SetCursorScreenPos(ImVec2(ex, ey));
  ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.5f);
  ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(86, 156, 214, 255));
  ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(30, 45, 66, 235));
  ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(225, 232, 240, 255));
  if (cmd.tableCellEditorFocusRequest) {
    ImGui::SetKeyboardFocusHere(0);
    cmd.tableCellEditorFocusRequest = false;
  }
  ImGui::SetNextItemWidth(boxW);
  const bool committed =
      ImGui::InputText("##table_cell_inplace", &cmd.tableCellEditorBuf, ImGuiInputTextFlags_EnterReturnsTrue);
  ImGui::PopStyleColor(3);
  ImGui::PopStyleVar();
  if (committed)
    CommitTableCellEditor(cmd, log);
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
    // REQ-039 phase 2: the target may be a model annotation or the active layout's paper text.
    CadAnnotation* ap = MtextRichEditorTargetAnnotation(cmd);
    if (!ap)
      return;
    const CadAnnotation& a = *ap;
    if (a.kind == CadAnnotation::Kind::Mtext && a.boxMaxX > a.boxMinX && a.boxMaxY > a.boxMinY) {
      bx0 = a.boxMinX;
      bx1 = a.boxMaxX;
      by0 = a.boxMinY;
      by1 = a.boxMaxY;
    } else {
      // Single-line TEXT (or a boxless MTEXT): synth a box anchored at the top-left insertion point. Paper
      // coords are inches (height = plotted inches); model coords are world units (× drawing scale).
      const float h = cmd.mtextRichEditorPaper ? std::max(0.01f, a.plottedHeightInches)
                                               : CadAnnotationHeightWorld(a, cmd.modelUnitsPerPlottedInch);
      const float w = std::max(h * 0.6f, h * 0.6f * static_cast<float>(a.text.size()));
      bx0 = a.insX;
      bx1 = a.insX + w;
      by1 = a.insY;
      by0 = a.insY - h;
    }
  }

  const float denx = worldRight - worldLeft + 1e-12f;
  const float deny = worldTop - worldBottom + 1e-12f;
  // Model space projects through the camera so the editor box sits over the text it is editing
  // when the view is orbited (REQ-058); paper space keeps the sheet's own 2D mapping, since a
  // sheet never tilts (ADR-025 (g)). Plan view is identical either way.
  const Camera edCam = CadViewCamera(cmd);
  const bool edPaper = cmd.mtextRichEditorPaper;
  auto ws = [&](float wx, float wy, ImVec2* o) {
    if (!edPaper) {
      float sx = 0.f, sy = 0.f;
      edCam.WorldToScreen(static_cast<double>(wx), static_cast<double>(wy), 0.0, avail.x, avail.y, &sx, &sy);
      o->x = imgPos.x + sx;
      o->y = imgPos.y + sy;
      return;
    }
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
  const float sy1 = std::max({p00.y, p01.y, p10.y, p11.y});

  const ImVec2 imgMin(imgPos.x, imgPos.y);
  const ImVec2 imgMax(imgPos.x + avail.x, imgPos.y + avail.y);

  // Single-line TEXT (REQ-039): edit in place. A bare, blue-bordered input box sits right over the text —
  // no hint line, no surrounding panel, no Save/Cancel buttons. Enter commits; Esc cancels (main.cpp).
  if (cmd.mtextRichEditorPlain && !cmd.mtextRichEditorPlacement) {
    const ImGuiStyle& ist0 = ImGui::GetStyle();
    const float boxW = std::clamp(sx1 - sx0 + 8.f, 48.f, imgMax.x - imgMin.x - 4.f);
    const float boxH = std::max(ImGui::GetFrameHeight(), sy1 - sy0);
    const float ex = std::clamp(sx0 - 2.f, imgMin.x + 2.f, imgMax.x - boxW - 2.f);
    const float ey = std::clamp(sy0 - ist0.FramePadding.y, imgMin.y + 2.f, imgMax.y - boxH - 2.f);
    ImGui::SetCursorScreenPos(ImVec2(ex, ey));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.5f);
    ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(86, 156, 214, 255));  // steel-blue selection border
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(30, 45, 66, 235));   // dark blue-gray fill
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(225, 232, 240, 255));
    if (cmd.mtextRichEditorFocusRequest) {
      ImGui::SetKeyboardFocusHere(0);
      cmd.mtextRichEditorFocusRequest = false;
    }
    ImGui::SetNextItemWidth(boxW);
    const bool committed =
        ImGui::InputText("##plain_text_inplace", &cmd.mtextRichEditorBuf, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
    if (committed)
      CommitMtextRichEditor(cmd, log);
    return;
  }

  // ---- Rich MTEXT: the in-place box (over the text, with its column ruler) + the floating panel ----
  // REQ-051. Single-line TEXT took the bare-box branch above and never reaches here.
  const ImGuiStyle& ist = ImGui::GetStyle();
  ImGuiIO& io = ImGui::GetIO();
  ImDrawList* dl = ImGui::GetWindowDrawList();

  // What the whole-object controls write. Null while *placing* — the MTEXT does not exist yet, so those
  // controls fall back to the defaults CommitMtextRichEditor will stamp onto it, or are disabled.
  CadAnnotation* target = MtextRichEditorTargetAnnotation(cmd);
  EntityAttributes* targetAttr = MtextRichEditorTargetAttrs(cmd);
  const bool placing = cmd.mtextRichEditorPlacement;
  const bool hasSel = cmd.mtextRichEditorSelStart != cmd.mtextRichEditorSelEnd;

  // ---------------------------- in-place edit box + column ruler ----------------------------
  // WYSIWYG box (ADR-023): text wraps at the MTEXT's column and the box grows with it, starting one line
  // tall. RichTextEditDraw does the layout, so the height it reports is authoritative — the estimate here
  // only positions the box before it is drawn.
  const float lh = ImGui::GetTextLineHeight();
  const float rulerH = cmd.mtextPanelRulerVisible ? 15.f : 0.f;
  const float oneLineH = lh + ist.FramePadding.y * 2.f + 2.f;
  const float maxBoxW = std::max(60.f, imgMax.x - imgMin.x - 8.f);
  const float maxBoxH = std::max(oneLineH, imgMax.y - imgMin.y - 8.f - rulerH);
  const float boxW = std::clamp(sx1 - sx0, 60.f, maxBoxW);
  const float boxH = std::min(std::max(oneLineH, cmd.mtextEditLastHeight), std::min(360.f, maxBoxH));
  const float ex = std::clamp(sx0, imgMin.x + 4.f, std::max(imgMin.x + 4.f, imgMax.x - boxW - 4.f));
  const float ey = std::clamp(sy0 - rulerH, imgMin.y + 4.f,
                              std::max(imgMin.y + 4.f, imgMax.y - boxH - rulerH - 4.f));
  if (cmd.mtextPanelRulerVisible) {
    MtextTbDrawRuler(dl, ImVec2(ex, ey), boxW, rulerH);
    // The ruler's right marker sets the MTEXT's column width — the same geometry edit a grip drag makes,
    // so it takes an undo snapshot once per drag rather than one per frame of motion.
    ImGui::SetCursorScreenPos(ImVec2(ex + boxW - 14.f, ey));
    ImGui::InvisibleButton("##mtext_ruler_width", ImVec2(16.f, std::max(6.f, rulerH)));
    if (ImGui::IsItemHovered() || ImGui::IsItemActive())
      ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    ItemHelpTooltip("Drag to set the text column width");
    if (ImGui::IsItemActivated() && target && !cmd.mtextRulerDragActive) {
      PushUndoSnapshot(cmd, "MTEXT width");
      cmd.mtextRulerDragActive = true;
    }
    if (ImGui::IsItemActive() && io.MouseDelta.x != 0.f) {
      const float worldPerPx = (worldRight - worldLeft) / std::max(1.f, avail.x);
      const float dWorld = io.MouseDelta.x * worldPerPx;
      const float minW = 60.f * worldPerPx;  // keep the column at least grabbable-wide on screen
      if (target) {
        target->boxMaxX = std::max(target->boxMinX + minW, target->boxMaxX + dWorld);
      } else if (placing) {
        // Placement drags the box the user is rubber-banding: move whichever corner is on the right.
        float& right = (cmd.mtxtX2 >= cmd.mtxtX1) ? cmd.mtxtX2 : cmd.mtxtX1;
        const float left = (cmd.mtxtX2 >= cmd.mtxtX1) ? cmd.mtxtX1 : cmd.mtxtX2;
        right = std::max(left + minW, right + dWorld);
      }
    }
    if (!ImGui::IsItemActive())
      cmd.mtextRulerDragActive = false;
  }

  ImGui::SetCursorScreenPos(ImVec2(ex, ey + rulerH));
  {
    // True WYSIWYG size: the editor draws at exactly the size the committed MTEXT will draw at, so
    // pressing Enter never resizes the text. This mirrors the render's sizing term for term — the same
    // per-viewport scale selection (REQ-050) and the same clamp. The old [10, 96] px window was its own
    // independent clamp, so any text whose real size fell outside it visibly jumped on commit: large
    // text was shown shrunk, small text shown enlarged.
    // While *placing*, no annotation exists yet, so take the plotted height CommitMtextRichEditor is
    // about to stamp (`defaultPlottedTextHeightInches` — the style stamp copies font/oblique/bold/
    // italic but deliberately not height). Falling back to the UI font size here is what made a newly
    // placed MTEXT jump to a completely different size the moment it was accepted.
    const bool isLabel = target && target->surveyPointLabelForId >= 0;
    const float plottedH = target ? std::max(0.01f, target->plottedHeightInches)
                                  : std::max(0.01f, cmd.defaultPlottedTextHeightInches);
    float editFontPx;
    {
      const float worldPerPxY = (worldTop - worldBottom) / std::max(1.f, avail.y);
      // Paper space pans/zooms in paper inches, so worldPerPxY is already inches-per-pixel there and
      // the plotted height needs no conversion; model space scales it by the units-per-inch the
      // render would use (REQ-050: the current viewport's scale when editing through one).
      float hEdit = plottedH;
      if (!cmd.mtextRichEditorPaper) {
        float mup = cmd.modelUnitsPerPlottedInch;
        if (!isLabel)
          if (const Viewport* mvp = CurrentViewport(cmd))
            mup = std::max(mvp->scaleModelPerPaperIn, 1.e-6f);
        hEdit = plottedH * std::max(mup, 1.e-6f);
      }
      const float minPx = isLabel ? cmd.viewportMtextMinPx : 1.f;
      const float maxPx = isLabel ? cmd.viewportMtextMaxPx : 8192.f;
      editFontPx = std::clamp(hEdit / std::max(worldPerPxY, 1.e-6f), minPx, maxPx);
    }
    const std::string& baseFam = target ? target->fontFamily : std::string();
    float usedH = boxH;
    RichTextEditDraw("##mtext_rte_body", cmd, boxW, editFontPx, IM_COL32(228, 232, 238, 255), baseFam,
                     std::min(360.f, maxBoxH), &usedH);
    cmd.mtextEditLastHeight = usedH;  // next frame positions the box from the height actually used
    // Ctrl+Enter places the text (plain Enter breaks the line). It used to re-normalise the buffer, which
    // was redundant — CommitMtextRichEditor normalises on the way out anyway.
    if (io.KeyCtrl &&
        (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false))) {
      CommitMtextRichEditor(cmd, log);
      return;  // nothing is pushed at this point — the panel's style push happens further down
    }
  }

  // ------------------------------- "Text Formatting" panel -------------------------------
  // Sized from its content; the first time it opens it sits just above the text, then it stays wherever
  // the user drags it (persisted). Anything that does not fit scrolls horizontally rather than clipping.
  const float rowH = ImGui::GetFrameHeight();
  const float titleH = ImGui::GetTextLineHeight() + 6.f;
  // The panel auto-sizes to its content (never clipped, never scrolled). Its size is only known after it
  // is laid out, so last frame's measurement places it and spans its caption; it converges immediately and
  // is re-measured below every frame.
  const float panelW = std::max(240.f, cmd.mtextPanelMeasuredW);
  const float panelH = std::max(titleH + rowH * 2.f, cmd.mtextPanelMeasuredH);
  if (!cmd.mtextPanelAnchorValid) {
    cmd.mtextPanelAnchorX = sx0;
    cmd.mtextPanelAnchorY = ey - panelH - 10.f;
    cmd.mtextPanelAnchorValid = true;
  }
  float px = 0.f, py = 0.f;
  mtexttoolbar::ClampPanelAnchor(cmd.mtextPanelAnchorX, cmd.mtextPanelAnchorY, panelW, panelH, imgMin.x + 2.f,
                                 imgMin.y + 2.f, imgMax.x - 2.f, imgMax.y - 2.f, &px, &py);
  cmd.mtextPanelAnchorX = px;
  cmd.mtextPanelAnchorY = py;

  ImGui::SetCursorScreenPos(ImVec2(px, py));
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
  if (ImGui::BeginChild("##MtextFormattingPanel", ImVec2(0.f, 0.f),
                        ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY |
                            ImGuiChildFlags_AlwaysAutoResize,
                        ImGuiWindowFlags_NoScrollbar)) {
    ImGui::PushID("mtext_fmt");
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(3.f, 3.f));

    // --- caption: drag to move the panel (the anchor is what persists) ---
    {
      const ImVec2 cp = ImGui::GetCursorScreenPos();
      // Span last frame's measured content width. Using the *measured* width (rather than a guess) keeps
      // the caption from being the thing that dictates the panel's width, so the two converge instead of
      // fighting each other.
      const float capW = std::max(200.f, cmd.mtextPanelMeasuredW - ist.WindowPadding.x * 2.f);
      ImGui::InvisibleButton("##caption", ImVec2(capW, titleH));
      if (ImGui::IsItemActive()) {
        cmd.mtextPanelAnchorX += io.MouseDelta.x;
        cmd.mtextPanelAnchorY += io.MouseDelta.y;
      }
      if (ImGui::IsItemHovered() || ImGui::IsItemActive())
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
      ImDrawList* pdl = ImGui::GetWindowDrawList();
      pdl->AddRectFilled(cp, ImVec2(cp.x + capW, cp.y + titleH), IM_COL32(124, 160, 196, 255));
      pdl->AddText(ImVec2(cp.x + 6.f, cp.y + 3.f), IM_COL32(16, 24, 34, 255), "Text Formatting");
      ItemHelpTooltip("Drag to move the Text Formatting panel");
    }

    // ------------------------------- row 1 -------------------------------
    // Text style — assigning re-bakes font/height/oblique from the style (REQ-044 / ADR-020).
    TextStyles::EnsureStandard(cmd.textStyles);
    const std::string curStyle = target ? target->styleName : cmd.activeTextStyleName;
    ImGui::SetNextItemWidth(110.f);
    if (ImGui::BeginCombo("##style", curStyle.empty() ? "(none)" : curStyle.c_str())) {
      for (const TextStyle& s : cmd.textStyles) {
        if (ImGui::Selectable(s.name.c_str(), s.name == curStyle)) {
          if (target)
            TextStyles::Assign(*target, s);
          else
            cmd.activeTextStyleName = s.name;
        }
      }
      ImGui::EndCombo();
    }
    MtextTbTip("Text style");
    ImGui::SameLine();

    // Font — applies to the selected characters via a [[font:…]] run, or to the whole object when nothing
    // is selected (also the only way to go back to the default font, which has no run tag).
    const std::string baseFont = target ? target->fontFamily : std::string();
    ImGui::SetNextItemWidth(150.f);
    if (ImGui::BeginCombo("##font",
                          baseFont.empty() ? TextStyles::kDefaultFontFamily : baseFont.c_str())) {
      for (const char* fn : kTextStyleFonts) {
        const std::string f(fn);
        const char* label = fn;
        // Preview each choice in its own typeface, as the STYLE dialog's picker does.
        const ImVec2 rowTL = ImGui::GetCursorScreenPos();
        const bool picked = ImGui::Selectable(
            label, f == baseFont || (baseFont.empty() && f == TextStyles::kDefaultFontFamily), 0, ImVec2(0.f, rowH));
        if (!f.empty()) {
          TextStyle preview;
          preview.fontFamily = f;
          DrawTextStyleSample(ImGui::GetWindowDrawList(), ImVec2(rowTL.x + 150.f, rowTL.y),
                              ImVec2(120.f, rowH), preview, "AaBb123", ImGui::GetColorU32(ImGuiCol_Text));
        }
        if (picked) {
          const auto tags = mtexttoolbar::FontRunTags(f);
          if (hasSel && !tags.open.empty()) {
            MtextRichWrapSelection(cmd, tags.open.c_str(), tags.close.c_str());
          } else if (target) {
            target->fontFamily = f;
            target->ovFont = true;  // a per-text override: a later style edit must not undo this
          }
        }
      }
      ImGui::EndCombo();
    }
    MtextTbTip("Font — applies to the selected text, or to the whole MTEXT when nothing is selected");
    ImGui::SameLine();

    ImGui::BeginDisabled();
    MtextTbIconButton("##annotative", MtextTbGlyph::Annotative);
    ImGui::EndDisabled();
    MtextTbTip("Annotative (not yet supported)");
    ImGui::SameLine();

    // Height — whole object, in plotted inches. While placing, this sets the height the new MTEXT gets.
    {
      float hgt = target ? target->plottedHeightInches : cmd.defaultPlottedTextHeightInches;
      ImGui::SetNextItemWidth(76.f);
      if (ImGui::InputFloat("##height", &hgt, 0.f, 0.f, "%.4f")) {
        hgt = std::clamp(hgt, 0.0001f, 1000.f);
        if (target) {
          target->plottedHeightInches = hgt;
          target->ovHeight = true;
        } else {
          cmd.defaultPlottedTextHeightInches = hgt;
        }
      }
    }
    MtextTbTip("Text height (plotted inches) — applies to the whole MTEXT");
    ImGui::SameLine();

    if (MtextTbLetterButton("##bold", "B"))
      MtextRichWrapSelection(cmd, "[[b]]", "[[/b]]");
    MtextTbTip("Bold");
    ImGui::SameLine();
    if (MtextTbLetterButton("##italic", "I"))
      MtextRichWrapSelection(cmd, "[[i]]", "[[/i]]");
    MtextTbTip("Italic");
    ImGui::SameLine();
    ImGui::BeginDisabled();
    MtextTbLetterButton("##strike", "A", /*strikeThrough=*/true);
    ImGui::EndDisabled();
    MtextTbTip("Strikethrough (not yet supported)");
    ImGui::SameLine();
    if (MtextTbLetterButton("##under", "U"))
      MtextRichWrapSelection(cmd, "[[u]]", "[[/u]]");
    MtextTbTip("Underline");
    ImGui::SameLine();
    ImGui::BeginDisabled();
    MtextTbLetterButton("##overline", "O", /*strikeThrough=*/false, /*overLine=*/true);
    ImGui::EndDisabled();
    MtextTbTip("Overline (not yet supported)");
    ImGui::SameLine();

    // Per-selection colour ([[color:RRGGBB]]). Disabled with no selection: unlike font there is no
    // sensible whole-object meaning here — that is what the entity-colour combo to the right is for.
    {
      ImGui::BeginDisabled(!hasSel);
      const ImU32 sw = IM_COL32((cmd.mtextPanelRunColor >> 16) & 0xFF, (cmd.mtextPanelRunColor >> 8) & 0xFF,
                                cmd.mtextPanelRunColor & 0xFF, 255);
      const ImVec2 cp = ImGui::GetCursorScreenPos();
      const bool open = ImGui::InvisibleButton("##runcolor", ImVec2(rowH, rowH));
      ImDrawList* pdl = ImGui::GetWindowDrawList();
      pdl->AddRectFilled(ImVec2(cp.x + 2.f, cp.y + 2.f), ImVec2(cp.x + rowH - 2.f, cp.y + rowH - 5.f), sw);
      pdl->AddRect(ImVec2(cp.x + 2.f, cp.y + 2.f), ImVec2(cp.x + rowH - 2.f, cp.y + rowH - 5.f),
                   IM_COL32(30, 30, 30, 255));
      ImGui::EndDisabled();
      if (open)
        ImGui::OpenPopup("##runcolorpick");
      MtextTbTip(hasSel ? "Colour the selected text" : "Colour the selected text — select characters first");
      if (ImGui::BeginPopup("##runcolorpick")) {
        for (const auto& p : kNamedColors) {
          if (std::string(p.storage) == "ByLayer")
            continue;  // a run tag is a literal colour; "ByLayer" has no meaning inside the text
          const unsigned int rgb = (static_cast<unsigned int>(p.r * 255.f) << 16) |
                                   (static_cast<unsigned int>(p.g * 255.f) << 8) |
                                   static_cast<unsigned int>(p.b * 255.f);
          ImGui::ColorButton(p.label, ImVec4(p.r, p.g, p.b, 1.f), ImGuiColorEditFlags_NoTooltip,
                             ImVec2(16.f, 16.f));
          ImGui::SameLine();
          if (ImGui::Selectable(p.label)) {
            cmd.mtextPanelRunColor = rgb;
            const auto tags = mtexttoolbar::ColorRunTags(rgb);
            MtextRichWrapSelection(cmd, tags.open.c_str(), tags.close.c_str());
            ImGui::CloseCurrentPopup();
          }
        }
        ImGui::EndPopup();
      }
    }
    ImGui::SameLine();

    ImGui::BeginDisabled();
    MtextTbIconButton("##mask", MtextTbGlyph::Mask);
    ImGui::EndDisabled();
    MtextTbTip("Background mask (not yet supported)");
    ImGui::SameLine();
    ImGui::BeginDisabled();
    MtextTbIconButton("##undo", MtextTbGlyph::Undo);
    ImGui::EndDisabled();
    MtextTbTip("Undo — use Ctrl+Z inside the text box");
    ImGui::SameLine();
    ImGui::BeginDisabled();
    MtextTbIconButton("##redo", MtextTbGlyph::Redo);
    ImGui::EndDisabled();
    MtextTbTip("Redo — use Ctrl+Y inside the text box");
    ImGui::SameLine();
    ImGui::BeginDisabled();
    MtextTbIconButton("##stack", MtextTbGlyph::Stack);
    ImGui::EndDisabled();
    MtextTbTip("Stacked fractions (not yet supported)");
    ImGui::SameLine();

    // Entity colour — the whole object's colour row, ByLayer included (disabled while placing: the
    // attributes are created by MakeNewEntityAttrs on commit).
    {
      ImGui::BeginDisabled(targetAttr == nullptr);
      const std::string curCol = targetAttr ? targetAttr->color : std::string("ByLayer");
      ImGui::SetNextItemWidth(112.f);
      if (ImGui::BeginCombo("##entcolor", curCol.empty() ? "ByLayer" : curCol.c_str())) {
        for (const auto& p : kNamedColors) {
          ImGui::ColorButton(p.label, ImVec4(p.r, p.g, p.b, 1.f), ImGuiColorEditFlags_NoTooltip,
                             ImVec2(16.f, 16.f));
          ImGui::SameLine();
          if (ImGui::Selectable(p.label, curCol == p.storage) && targetAttr)
            targetAttr->color = p.storage;
        }
        ImGui::EndCombo();
      }
      ImGui::EndDisabled();
      MtextTbTip(targetAttr ? "Object colour" : "Object colour — available once the MTEXT is placed");
    }
    ImGui::SameLine();

    if (MtextTbIconButton("##ruler", MtextTbGlyph::Ruler, cmd.mtextPanelRulerVisible))
      cmd.mtextPanelRulerVisible = !cmd.mtextPanelRulerVisible;
    MtextTbTip("Show/hide the ruler");
    ImGui::SameLine();

    if (ImGui::Button(placing ? "Place" : "OK"))
      CommitMtextRichEditor(cmd, log);
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
      CancelMtextRichEditor(cmd, &log);
    ImGui::SameLine();
    // The chevron opens the Options menu, as in AutoCAD. Items the stored text model can support are
    // live; the rest are disabled and name themselves, matching the toolbar rows above.
    if (MtextTbIconButton("##options", MtextTbGlyph::ExpandDown))
      ImGui::OpenPopup("##mtextOptions");
    MtextTbTip("Options");
    if (ImGui::BeginPopup("##mtextOptions")) {
      size_t selA = static_cast<size_t>(std::max(0, cmd.mtextRichEditorSelStart));
      size_t selB = static_cast<size_t>(std::max(0, cmd.mtextRichEditorSelEnd));
      selA = std::min(selA, cmd.mtextRichEditorBuf.size());
      selB = std::min(selB, cmd.mtextRichEditorBuf.size());
      const bool sel = selB > selA;

      ImGui::BeginDisabled();
      ImGui::MenuItem("Insert Field...", "Ctrl+F");
      ImGui::EndDisabled();
      MtextTbTip("Fields are not supported yet");

      if (ImGui::MenuItem("Import Text...")) {
        char path[1024] = {0};
        if (BrowseOpenFileCsvUtf8(path, sizeof(path))) {  // any text file; the filter offers All (*.*)
          std::ifstream f(path, std::ios::binary);
          if (f) {
            std::stringstream ss;
            ss << f.rdbuf();
            std::string t = ss.str();
            // Insert as literal text: strip CR and collapse "[[" so an imported file cannot inject a tag.
            std::string safe;
            safe.reserve(t.size());
            for (size_t i = 0; i < t.size(); ++i) {
              if (t[i] == '\r')
                continue;
              if (t[i] == '[' && i + 1 < t.size() && t[i + 1] == '[') {
                safe += '[';
                ++i;
                continue;
              }
              safe += t[i];
            }
            const size_t at = std::min(static_cast<size_t>(std::max(0, cmd.mtextRichEditorCursor)),
                                       cmd.mtextRichEditorBuf.size());
            cmd.mtextRichEditorBuf.insert(at, safe);
            log.push_back("MTEXT — imported text from " + std::string(path));
          } else {
            log.push_back("MTEXT — could not read that file.");
          }
        }
      }
      MtextTbTip("Insert the contents of a text file at the caret");

      ImGui::Separator();
      ImGui::BeginDisabled();
      if (ImGui::BeginMenu("Paragraph Alignment")) ImGui::EndMenu();
      ImGui::MenuItem("Paragraph...");
      if (ImGui::BeginMenu("Bullets and Lists")) ImGui::EndMenu();
      if (ImGui::BeginMenu("Columns")) ImGui::EndMenu();
      ImGui::EndDisabled();
      MtextTbTip("Paragraph and column properties are not stored yet");

      ImGui::Separator();
      if (ImGui::MenuItem("Find and Replace...", "Ctrl+R"))
        cmd.mtextFindReplaceOpen = true;

      if (ImGui::BeginMenu("Change Case")) {
        // Rewrites the characters themselves (not a [[caps]] run), so the change is permanent and
        // survives export — which is what AutoCAD's Change Case does.
        const size_t a2 = sel ? selA : 0;
        const size_t b2 = sel ? selB : cmd.mtextRichEditorBuf.size();
        if (ImGui::MenuItem("UPPERCASE"))
          mtextops::UpperRange(cmd.mtextRichEditorBuf, a2, b2);
        if (ImGui::MenuItem("lowercase"))
          mtextops::LowerRange(cmd.mtextRichEditorBuf, a2, b2);
        ImGui::TextDisabled("%s", sel ? "(selected text)" : "(whole MTEXT)");
        ImGui::EndMenu();
      }

      ImGui::MenuItem("All CAPS", nullptr, &cmd.mtextRichEditorTypingAllCaps);
      MtextTbTip("Type new ASCII in ALL CAPS");
      ImGui::MenuItem("Autocorrect cAPS Lock", nullptr, &cmd.mtextEditAutocorrectCapsLock);
      MtextTbTip("Fix a word typed with Caps Lock inverted (hELLO becomes Hello)");

      if (ImGui::BeginMenu("Character Set")) {
        for (const auto& e : kMtextSymbolPicks) {
          if (ImGui::MenuItem(e.label))
            MtextRichInsertAtCaret(cmd, e.utf8);
        }
        ImGui::EndMenu();
      }

      ImGui::Separator();
      ImGui::BeginDisabled();
      ImGui::MenuItem("Combine Paragraphs");
      ImGui::EndDisabled();
      if (ImGui::BeginMenu("Remove Formatting")) {
        if (ImGui::MenuItem("From selected text", nullptr, false, sel))
          mtextops::RemoveFormattingRange(cmd.mtextRichEditorBuf, selA, selB);
        if (ImGui::MenuItem("From the whole MTEXT"))
          cmd.mtextRichEditorBuf = MtextRichFlattenToPlain(cmd.mtextRichEditorBuf);
        ImGui::EndMenu();
      }
      ImGui::BeginDisabled();
      ImGui::MenuItem("Background Mask...");
      ImGui::EndDisabled();
      MtextTbTip("Background masking is not supported yet");

      ImGui::Separator();
      if (ImGui::BeginMenu("Editor Settings")) {
        ImGui::MenuItem("Show Options Row", nullptr, &cmd.mtextPanelRow2Visible);
        ImGui::MenuItem("Show Ruler", nullptr, &cmd.mtextPanelRulerVisible);
        ImGui::EndMenu();
      }
      if (ImGui::MenuItem("Help", "F1"))
        log.push_back("MTEXT — Ctrl+Enter places the text, Esc cancels. Enter breaks the line. "
                      "Select characters to apply a font, colour, or B/I/U to just them.");
      ImGui::EndPopup();
    }

    // Find and Replace, opened from the Options menu.
    if (cmd.mtextFindReplaceOpen) {
      ImGui::OpenPopup("Find and Replace##mtextFR");
      cmd.mtextFindReplaceOpen = false;
      cmd.mtextFindStatus.clear();
    }
    if (ImGui::BeginPopup("Find and Replace##mtextFR")) {
      ImGui::SetNextItemWidth(200.f);
      ImGui::InputText("Find", cmd.mtextFindBuf, sizeof(cmd.mtextFindBuf));
      ImGui::SetNextItemWidth(200.f);
      ImGui::InputText("Replace with", cmd.mtextReplaceBuf, sizeof(cmd.mtextReplaceBuf));
      ImGui::Checkbox("Match case", &cmd.mtextFindMatchCase);
      if (ImGui::Button("Replace All")) {
        const int n = mtextops::FindReplaceAll(cmd.mtextRichEditorBuf, cmd.mtextFindBuf, cmd.mtextReplaceBuf,
                                          cmd.mtextFindMatchCase);
        cmd.mtextFindStatus = (n > 0) ? (std::to_string(n) + " replaced") : "Not found";
      }
      ImGui::SameLine();
      if (ImGui::Button("Close"))
        ImGui::CloseCurrentPopup();
      if (!cmd.mtextFindStatus.empty())
        ImGui::TextDisabled("%s", cmd.mtextFindStatus.c_str());
      ImGui::EndPopup();
    }

    // ------------------------------- row 2 -------------------------------
    if (cmd.mtextPanelRow2Visible) {
      ImGui::BeginDisabled();
      MtextTbIconButton("##columns", MtextTbGlyph::Columns);
      ImGui::EndDisabled();
      MtextTbTip("Columns (not yet supported)");
      ImGui::SameLine();

      // Justification = the MTEXT attachment point (DXF group 71), which the renderer already honours.
      {
        ImGui::BeginDisabled(target == nullptr);
        const int curAttach = target ? target->mtextAttach : 1;
        ImGui::SetNextItemWidth(120.f);
        if (ImGui::BeginCombo("##attach", mtexttoolbar::AttachLabel(curAttach))) {
          for (int k = 1; k <= 9; ++k) {
            if (ImGui::Selectable(mtexttoolbar::AttachLabel(k), k == curAttach) && target)
              target->mtextAttach = k;
          }
          ImGui::EndCombo();
        }
        ImGui::EndDisabled();
        MtextTbTip(target ? "Justification (attachment point)"
                          : "Justification — available once the MTEXT is placed");
      }
      ImGui::SameLine();

      // Paragraph properties are per-paragraph state the annotation does not store — all disabled.
      struct DisabledTb {
        const char* id;
        MtextTbGlyph glyph;
        const char* tip;
      };
      static const DisabledTb kParaTb[] = {
          {"##para", MtextTbGlyph::Paragraph, "Paragraph (not yet supported)"},
          {"##alignL", MtextTbGlyph::AlignLeft, "Align left (not yet supported)"},
          {"##alignC", MtextTbGlyph::AlignCenter, "Align center (not yet supported)"},
          {"##alignR", MtextTbGlyph::AlignRight, "Align right (not yet supported)"},
          {"##alignJ", MtextTbGlyph::AlignJust, "Justify (not yet supported)"},
          {"##alignD", MtextTbGlyph::AlignDist, "Distribute (not yet supported)"},
          {"##spacing", MtextTbGlyph::LineSpacing, "Line spacing (not yet supported)"},
          {"##lists", MtextTbGlyph::Lists, "Bullets and numbering (not yet supported)"},
          {"##field", MtextTbGlyph::Field, "Insert field (not yet supported)"},
      };
      for (const auto& b : kParaTb) {
        ImGui::BeginDisabled();
        MtextTbIconButton(b.id, b.glyph);
        ImGui::EndDisabled();
        MtextTbTip(b.tip);
        ImGui::SameLine();
      }

      if (MtextTbLetterButton("##upper", "A"))
        MtextRichWrapSelection(cmd, "[[caps]]", "[[/caps]]");
      MtextTbTip("UPPERCASE the selected text");
      ImGui::SameLine();
      ImGui::BeginDisabled();
      MtextTbLetterButton("##lower", "a");
      ImGui::EndDisabled();
      MtextTbTip("lowercase (not yet supported)");
      ImGui::SameLine();
      ImGui::BeginDisabled();
      MtextTbLetterButton("##super", "x2");
      ImGui::EndDisabled();
      MtextTbTip("Superscript (not yet supported)");
      ImGui::SameLine();
      ImGui::BeginDisabled();
      MtextTbLetterButton("##sub", "x2");
      ImGui::EndDisabled();
      MtextTbTip("Subscript (not yet supported)");
      ImGui::SameLine();

      // Symbol insertion (the same list the Options menu's Character Set offers).
      ImGui::SetNextItemWidth(90.f);
      if (ImGui::BeginCombo("##symbol", "@")) {
        for (const auto& e : kMtextSymbolPicks) {
          if (ImGui::Selectable(e.label))
            MtextRichInsertAtCaret(cmd, e.utf8);
        }
        ImGui::EndCombo();
      }
      MtextTbTip("Insert a symbol at the caret");
      ImGui::SameLine();

      // Oblique — whole object, in degrees (the annotation stores it; SHX shears faithfully, TTF approximates).
      {
        ImGui::BeginDisabled(target == nullptr);
        MtextTbIconButton("##obliqueIcon", MtextTbGlyph::Oblique);
        ImGui::SameLine();
        float ob = target ? target->obliqueDeg : 0.f;
        ImGui::SetNextItemWidth(78.f);
        if (ImGui::InputFloat("##oblique", &ob, 0.f, 0.f, "%.4f") && target) {
          target->obliqueDeg = std::clamp(ob, -85.f, 85.f);
          target->ovOblique = true;
        }
        ImGui::EndDisabled();
        MtextTbTip(target ? "Oblique angle (degrees) — applies to the whole MTEXT"
                          : "Oblique angle — available once the MTEXT is placed");
      }
      ImGui::SameLine();

      ImGui::BeginDisabled();
      MtextTbIconButton("##trackIcon", MtextTbGlyph::Tracking);
      ImGui::SameLine();
      float tracking = 1.f;
      ImGui::SetNextItemWidth(78.f);
      ImGui::InputFloat("##tracking", &tracking, 0.f, 0.f, "%.4f");
      ImGui::EndDisabled();
      MtextTbTip("Tracking / character spacing (not yet supported)");
      ImGui::SameLine();

      ImGui::BeginDisabled();
      MtextTbIconButton("##widthIcon", MtextTbGlyph::WidthFactor);
      ImGui::SameLine();
      float widthFactor = 1.f;
      ImGui::SetNextItemWidth(78.f);
      ImGui::InputFloat("##width", &widthFactor, 0.f, 0.f, "%.4f");
      ImGui::EndDisabled();
      MtextTbTip("Width factor (not yet supported)");
      ImGui::SameLine();

      ImGui::Checkbox("Abc", &cmd.mtextRichEditorTypingAllCaps);
      MtextTbTip("Type new ASCII in ALL CAPS");
    }

    ImGui::PopStyleVar();
    ImGui::PopID();
    // Feed this frame's auto-sized extent back for next frame's placement and caption span.
    const ImVec2 measured = ImGui::GetWindowSize();
    cmd.mtextPanelMeasuredW = measured.x;
    cmd.mtextPanelMeasuredH = measured.y;
  }
  ImGui::EndChild();
  ImGui::PopStyleColor();
}

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
  case CadSnap::Kind::Intersection:
    return "Intersection";
  case CadSnap::Kind::ApparentIntersection:
    return "Apparent int";
  case CadSnap::Kind::Surface:
    return "Surface";
  case CadSnap::Kind::Edge:
    return "Solid edge";
  case CadSnap::Kind::Face:
    return "Solid face";
  case CadSnap::Kind::Grip:
    return "Grip";
  }
  return "Snap";
}

/// When a single annotation with viewport grips is selected, pull the cursor to the nearest grip inside the OSNAP
/// aperture (competes with geometry snap by closest distance to raw pick).
static void ApplyGripMagnetToGrips(AppCommandState& cmd, double rawX, double rawY, float halfH, float availY,
                                   double* ioX, double* ioY, CadSnap::Hit* out_snap) {
  if (!ioX || !ioY)
    return;
  if (cmd.selection.size() != 1)
    return;
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
  if (cmd.selection[0].type == SelectedEntity::Type::Table) {
    const int ix = cmd.selection[0].index;
    if (ix < 0 || static_cast<size_t>(ix) >= cmd.cadTables.size())
      return;
    const CadTable& t = cmd.cadTables[static_cast<size_t>(ix)];
    for (int c = 0; c < 4; ++c) {
      float gx = 0.f, gy = 0.f;
      CadTableWorldCorner(t, c, &gx, &gy);
      offer(gx, gy);
    }
  } else if (cmd.selection[0].type == SelectedEntity::Type::BlockRef) {
    const int ix = cmd.selection[0].index;
    if (ix < 0 || static_cast<size_t>(ix) >= cmd.cadBlockRefs.size())
      return;
    const CadBlockRef& r = cmd.cadBlockRefs[static_cast<size_t>(ix)];
    offer(r.xf.x, r.xf.y);
    const int di = CadBlockFindDef(cmd.blockDefs, r.defName);
    if (di >= 0) {
      const CadBlockDefinition& def = cmd.blockDefs[static_cast<size_t>(di)];
      const int nG = CadBlockDynGripCount(def);
      for (int g = 1; g < nG; ++g) {
        if (!CadBlockDynGripShownOnInsert(g))
          continue;
        float gx = 0.f, gy = 0.f, gz = 0.f;
        if (CadBlockDynGripWorld(def, r, g, &gx, &gy, &gz))
          offer(gx, gy);
      }
    }
  } else if (cmd.selection[0].type != SelectedEntity::Type::Annotation) {
    return;
  } else {
  const int ix = cmd.selection[0].index;
  if (ix < 0 || static_cast<size_t>(ix) >= cmd.cadAnnotations.size())
    return;
  const CadAnnotation& a = cmd.cadAnnotations[static_cast<size_t>(ix)];
  if (a.kind == CadAnnotation::Kind::DimAligned || a.kind == CadAnnotation::Kind::DimLinear) {
    float sx1 = 0.f, sy1 = 0.f, sx2 = 0.f, sy2 = 0.f, tx = 0.f, ty = 0.f, nx = 0.f, ny = 0.f, ml = 0.f;
    if (CadDimAnyGeometry(a, &sx1, &sy1, &sx2, &sy2, &tx, &ty, &nx, &ny, &ml)) {
      offer(a.dimExt1X, a.dimExt1Y);
      offer(a.dimExt2X, a.dimExt2Y);
      offer(sx1, sy1);
      offer(sx2, sy2);
      offer(a.insX, a.insY);
    }
  } else if (a.kind == CadAnnotation::Kind::Mtext || a.kind == CadAnnotation::Kind::Table) {
    if (a.surveyPointLabelForId >= 0)
      offer(0.5f * (a.boxMinX + a.boxMaxX), 0.5f * (a.boxMinY + a.boxMaxY));
    else {
      offer(a.boxMinX, a.boxMinY);
      offer(a.boxMaxX, a.boxMinY);
      offer(a.boxMaxX, a.boxMaxY);
      offer(a.boxMinX, a.boxMaxY);
    }
  }
  }
  if (bx != *ioX || by != *ioY) {
    *ioX = bx;
    *ioY = by;
    cmd.viewportSnapPickValid = true;
    cmd.viewportSnapPickLocalX = bx;
    cmd.viewportSnapPickLocalY = by;
    if (out_snap) {
      out_snap->valid = true;
      out_snap->kind = CadSnap::Kind::Grip;
      out_snap->x = bx;
      out_snap->y = by;
    }
  }
}

static void DrawCadGripMarker(ImDrawList* dl, const ImVec2& gp, float half, CadBlockDynGripShape shape, float sdx,
                              float sdy, ImU32 fill, ImU32 border) {
  assert(dl != nullptr);
  if (shape == CadBlockDynGripShape::Square) {
    dl->AddRectFilled(ImVec2(gp.x - half, gp.y - half), ImVec2(gp.x + half, gp.y + half), fill);
    dl->AddRect(ImVec2(gp.x - half, gp.y - half), ImVec2(gp.x + half, gp.y + half), border, 0.f, 0, 1.f);
    return;
  }
  float len = std::hypot(sdx, sdy);
  if (len < 1.e-4f) {
    sdx = 1.f;
    sdy = 0.f;
    len = 1.f;
  }
  sdx /= len;
  sdy /= len;
  if (shape == CadBlockDynGripShape::FlipArrow) {
    const float L = half * 2.4f;
    const float W = half * 1.2f;
    const ImVec2 n(-sdy * W, sdx * W);
    const ImVec2 tipA(gp.x + sdx * L, gp.y + sdy * L);
    const ImVec2 tipB(gp.x - sdx * L, gp.y - sdy * L);
    const ImVec2 midA(gp.x + sdx * L * 0.15f, gp.y + sdy * L * 0.15f);
    const ImVec2 midB(gp.x - sdx * L * 0.15f, gp.y - sdy * L * 0.15f);
    dl->AddTriangleFilled(tipA, ImVec2(midA.x + n.x, midA.y + n.y), ImVec2(midA.x - n.x, midA.y - n.y), fill);
    dl->AddTriangle(tipA, ImVec2(midA.x + n.x, midA.y + n.y), ImVec2(midA.x - n.x, midA.y - n.y), border, 1.f);
    dl->AddTriangleFilled(tipB, ImVec2(midB.x + n.x, midB.y + n.y), ImVec2(midB.x - n.x, midB.y - n.y), fill);
    dl->AddTriangle(tipB, ImVec2(midB.x + n.x, midB.y + n.y), ImVec2(midB.x - n.x, midB.y - n.y), border, 1.f);
    return;
  }
  const float L = half * 2.6f;
  const float W = half * (shape == CadBlockDynGripShape::StretchArrow ? 1.55f : 1.35f);
  const ImVec2 tip(gp.x + sdx * L, gp.y + sdy * L);
  const ImVec2 base(gp.x - sdx * L * 0.4f, gp.y - sdy * L * 0.4f);
  const ImVec2 n(-sdy * W, sdx * W);
  dl->AddTriangleFilled(tip, ImVec2(base.x + n.x, base.y + n.y), ImVec2(base.x - n.x, base.y - n.y), fill);
  dl->AddTriangle(tip, ImVec2(base.x + n.x, base.y + n.y), ImVec2(base.x - n.x, base.y - n.y), border, 1.f);
}

// Frame-time diagnostic overlay (issue #166 investigation). Toggled by the PERFHUD command. Shows
// the current / rolling-average / rolling-max of the whole frame and the sections most likely to
// scale with drawing size during a modify command, plus whether the issue-#166 hover-pick gate
// actually skipped this frame. Deliberately a throwaway tool, not a shipped feature.
void DrawPerfHud(const AppCommandState& cmd) {
  if (!cmd.perfHudVisible)
    return;

  constexpr int kWin = 120;  // ~1-2 s of frames
  struct Ring {
    double v[kWin] = {};
    int n = 0, head = 0;
    void push(double x) {
      v[head] = x;
      head = (head + 1) % kWin;
      if (n < kWin)
        ++n;
    }
    double avg() const {
      if (!n)
        return 0.0;
      double s = 0.0;
      for (int i = 0; i < n; ++i)
        s += v[i];
      return s / n;
    }
    double max() const {
      double m = 0.0;
      for (int i = 0; i < n; ++i)
        m = v[i] > m ? v[i] : m;
      return m;
    }
  };
  static Ring frame, vpUi, hover, snap, render, ranHist;

  frame.push(cmd.perfFrameMs);
  vpUi.push(cmd.perfViewportUiMs);
  hover.push(cmd.perfHoverPickMs);
  snap.push(cmd.perfSnapMs);
  render.push(cmd.perfRenderMs);
  ranHist.push(cmd.perfHoverPickRan ? 1.0 : 0.0);
  int hoverRan = 0;
  for (int i = 0; i < ranHist.n; ++i)
    hoverRan += ranHist.v[i] > 0.5 ? 1 : 0;

  // Pin it to the top-centre of the whole window every frame so it can't hide behind a panel or a
  // stale docked position — this is a diagnostic that has to be found instantly.
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowBgAlpha(0.92f);
  ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + 140.f),
                          ImGuiCond_Always, ImVec2(0.5f, 0.f));
  if (ImGui::Begin("Frame profiler (PERFHUD)", nullptr,
                   ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing |
                       ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoCollapse |
                       ImGuiWindowFlags_NoSavedSettings)) {
    const double favg = frame.avg();
    ImGui::Text("FPS  %6.1f      frame  %5.2f / %5.2f / %5.2f ms  (cur/avg/max)",
                favg > 1e-6 ? 1000.0 / favg : 0.0, cmd.perfFrameMs, favg, frame.max());
    ImGui::Separator();
    auto row = [](const char* name, const Ring& r, double cur) {
      ImGui::Text("%-11s %5.2f / %5.2f / %5.2f ms", name, cur, r.avg(), r.max());
    };
    row("viewportUI", vpUi, cmd.perfViewportUiMs);
    row("  hoverPick", hover, cmd.perfHoverPickMs);
    row("  snap", snap, cmd.perfSnapMs);
    row("render", render, cmd.perfRenderMs);
    ImGui::Separator();
    const int win = frame.n ? frame.n : 1;
    ImGui::Text("hoverPick this frame: %s     ran %d / last %d frames",
                cmd.perfHoverPickRan ? "RAN" : "cached", hoverRan, win);
    const char* act = "none";
    switch (cmd.active) {
      case AppCommandState::Kind::Trim: act = "TRIM"; break;
      case AppCommandState::Kind::Extend: act = "EXTEND"; break;
      case AppCommandState::Kind::Break: act = "BREAK"; break;
      case AppCommandState::Kind::Lengthen: act = "LENGTHEN"; break;
      default: break;
    }
    ImGui::Text("command: %-9s   lines %zu  polylines %zu  arcs %zu  circles %zu", act,
                cmd.userLinesFlat.size() / 6, cmd.userPolylineOffsets.size(), cmd.userArcs.size(),
                cmd.userCirclesCxCyZR.size() / 4);
    ImGui::TextDisabled("PERFHUD again to hide. Numbers are wall-clock; vsync caps 'frame'.");
  }
  ImGui::End();
}

void DrawDrawingViewport(unsigned int viewportTextureId, AppCommandState& cmd, std::vector<std::string>& log,
                         char* cmdBuf, int cmdBufSize, double* panX, double* panY, float* zoom, double* outCursorX,
                         double* outCursorY, double* outCursorRawX, double* outCursorRawY, int* outFbW, int* outFbH,
                         CadSnap::Hit* out_snap) {
  // PERFHUD (issue #166 investigation): time the whole viewport-UI pass, every return path.
  struct PerfVpScope {
    AppCommandState& c;
    std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    ~PerfVpScope() {
      c.perfViewportUiMs =
          std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    }
  } perfVpScope{cmd};

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
  // Give the strip more presence: taller tabs, rounded tops, an accent-blue active tab and a
  // separator rule under the whole bar so it reads as a distinct band, not part of the viewport.
  const ImU32 kTabAccent       = HexU32(0x3B6EA5);
  const ImU32 kTabAccentHover  = HexU32(0x4E86C2);
  const ImU32 kTabIdle         = HexU32(0x2C2C2C);
  const ImU32 kTabIdleHover    = HexU32(0x3A3A3A);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.f, 6.f));
  ImGui::PushStyleVar(ImGuiStyleVar_TabRounding, 5.f);
  ImGui::PushStyleVar(ImGuiStyleVar_TabBarBorderSize, 2.f);
  ImGui::PushStyleColor(ImGuiCol_Tab, kTabIdle);
  ImGui::PushStyleColor(ImGuiCol_TabHovered, kTabIdleHover);
  ImGui::PushStyleColor(ImGuiCol_TabActive, kTabAccent);
  ImGui::PushStyleColor(ImGuiCol_TabUnfocused, kTabIdle);
  ImGui::PushStyleColor(ImGuiCol_TabUnfocusedActive, HexU32(0x35597D));
  ImGui::PushStyleColor(ImGuiCol_TabSelectedOverline, kTabAccentHover);
  ImGui::PushStyleColor(ImGuiCol_Text, HexU32(0xE8E8E8));
  const ImVec2 tabBarTL = ImGui::GetCursorScreenPos();
  if (ImGui::BeginTabBar("##DrawingTabs",
                         ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_FittingPolicyScroll)) {
    for (int i = 0; i < static_cast<int>(cmd.drawingTabs.size()); ++i) {
      bool tabOpen = true;
      // REQ-308: drawingTabs[0] is the Start screen — pinned first (Leading) and non-closable
      // (no p_open, so ImGui draws no close button and tabOpen stays true).
      const bool isStart = (i == 0);
      // SetSelected only fires on the one frame after a programmatic switch (e.g. "+").
      // Applying it every frame would override ImGui's own click handling.
      const bool wantSelect = cmd.pendingDrawingTabSwitch && (i == cmd.activeDrawingIdx);
      ImGuiTabItemFlags tflags = wantSelect ? ImGuiTabItemFlags_SetSelected : 0;
      if (isStart)
        tflags |= ImGuiTabItemFlags_Leading | ImGuiTabItemFlags_NoReorder;
      // Append "##<uid>" so each tab has a unique ImGui ID even when two tabs share the same display name.
      const std::string tabLabel = cmd.drawingTabs[i].name + "##dt" + std::to_string(cmd.drawingTabs[i].uid);
      if (ImGui::BeginTabItem(tabLabel.c_str(), isStart ? nullptr : &tabOpen, tflags)) {
        // While a programmatic switch is pending, ignore the selection ImGui reports for any OTHER tab.
        // Tabs are submitted in index order, so the tab that is still selected this frame is reached
        // BEFORE the newly created one — without this guard it overwrote activeDrawingIdx back to itself
        // and consumed the pending flag, and New / Open / "+" left the user on the old tab.
        if (!cmd.pendingDrawingTabSwitch || i == cmd.activeDrawingIdx) {
          cmd.activeDrawingIdx = i;
          cmd.pendingDrawingTabSwitch = false;  // consumed
        }
        ImGui::EndTabItem();
      }
      if (!tabOpen && i >= FirstDrawingTabIndex() && cmd.drawingTabs.size() > 2) {
        const int closeIdx  = i;
        const int tabCount  = static_cast<int>(cmd.drawingTabs.size());

        // Which tab becomes active after this one closes?
        int newActive = cmd.activeDrawingIdx;
        if (closeIdx == cmd.activeDrawingIdx) {
          // Prefer the previous drawing; never land on the Start tab (index 0) while another
          // drawing is still open (REQ-308).
          newActive = (closeIdx - 1 >= FirstDrawingTabIndex()) ? closeIdx - 1 : closeIdx + 1;
          // Load that tab's snapshot into cmd before we erase anything.
          RestoreDocumentFromSnapshot(cmd, newActive);
        }
        // Adjust for the index shift the erase will produce.
        if (newActive > closeIdx) --newActive;
        newActive = std::max(FirstDrawingTabIndex(), std::min(newActive, tabCount - 2));

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
    // Trailing "+" to open a new empty drawing (same path as File ▸ New).
    if (ImGui::TabItemButton("+", ImGuiTabItemFlags_Trailing | ImGuiTabItemFlags_NoTooltip)) {
      NewDrawingInTab(cmd, log);
    }
    ImGui::EndTabBar();
  }
  // Accent rule under the whole strip so it reads as its own band.
  {
    const float y = ImGui::GetCursorScreenPos().y - 1.f;
    const float x0 = tabBarTL.x - ImGui::GetStyle().WindowPadding.x;
    const float x1 = x0 + ImGui::GetWindowWidth();
    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(x0, y), ImVec2(x1, y + 2.f), kTabAccent);
  }
  ImGui::PopStyleColor(7);
  ImGui::PopStyleVar(3);

  // REQ-308: the Start tab owns no drawing. Render the start screen and skip every model/paper
  // viewport interaction below.
  if (cmd.activeDrawingIdx == 0) {
    cmd.viewportDrawingHovered = false;
    cmd.viewportCmdPaletteEngaged = false;
    DrawStartScreen(cmd, log);
    ImGui::End();
    return;
  }

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

  // ViewCube geometry is computed HERE, before any click handling, even though the widget is drawn
  // at the end of this function. Clicks are consumed early, so a widget that only knows its own
  // bounds at draw time cannot defend them — pressing a face or the home button was starting a
  // box-selection drag in the viewport behind it (REQ-059: the widget consumes clicks only within
  // its own bounds, and geometry elsewhere still picks normally).
  constexpr float kViewCubeSize = 148.f;
  constexpr float kViewCubePad = 10.f;
  const float viewCubeX = imgPos.x + avail.x - kViewCubeSize - kViewCubePad;
  const float viewCubeY = imgPos.y + kViewCubePad;
  const ImVec2 vcMouse = ImGui::GetIO().MousePos;
  const bool overViewCube = modelSpace && vcMouse.x >= viewCubeX && vcMouse.x <= viewCubeX + kViewCubeSize &&
                            vcMouse.y >= viewCubeY && vcMouse.y <= viewCubeY + kViewCubeSize;

  // Advance any in-flight ViewCube animation (REQ-059).
  CadTickViewAnimation(cmd, ImGui::GetIO().DeltaTime);

  // Floating model space (REQ-036): Esc does NOT exit — it cancels the active model command so the user
  // keeps editing in the viewport. Exit is via double-click outside the viewport (below), the FLOAT
  // button, or PSPACE.

  // When editing a viewport in place and the zoom lock is OFF, pan/zoom targets the viewport's model
  // framing instead of the sheet (handled below, after paper coords are available).
  const bool routeZoomToViewport = InFloatingModelSpace(cmd) && !cmd.viewportZoomLocked;

  // ZOOM EXTENTS by middle double-click (REQ-120) — AutoCAD's binding for the same gesture.
  //
  // Raised HERE rather than inside the wheel/pan block below, because that block is guarded by
  // `!routeZoomToViewport`: with a floating viewport owning pan/zoom (the default, lock OFF) it is
  // skipped entirely, so the gesture silently did nothing in exactly the place issue #100 is about.
  // REQ-120 claimed floating model space frames the model; it never even fired there. Corrected as
  // part of REQ-123 — the flag is raised in every space and `ProcessPendingViewportZoom` decides
  // what "extents" means for each.
  //
  // Deliberately NOT routed through StartZoomExtentsCommand: that refuses while a command is
  // running, and this gesture is TRANSPARENT, because wanting to reframe mid-command is exactly
  // when a user reaches for it. Setting the flag directly is the same thing DXF import already does
  // to frame a freshly-imported drawing, and it is safe mid-command because the deferred consumer
  // writes only a camera — the active command's phase, picked points and draft geometry are
  // untouched. The typed ZOOMEXTENTS keeps its guard and its refusal message.
  //
  // A double-click is not a drag, so middle-drag pan is unaffected (REQ-045 requires it).
  if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Middle)) {
    cmd.pendingZoomWindow = false;  // a pending ZOOM WINDOW would otherwise win the same frame
    cmd.pendingZoomExtents = true;
  }

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

    // ZOOM EXTENTS by middle double-click (REQ-120) — AutoCAD's binding for the same gesture.
    // Deliberately NOT routed through StartZoomExtentsCommand: that refuses while a command is
    // running, and this gesture is TRANSPARENT, because wanting to reframe mid-command is exactly
    // when a user reaches for it. Setting the flag directly is the same thing DXF import already
    // does to frame a freshly-imported drawing, and it is safe mid-command because the deferred
    // consumer writes only the camera — the active command's phase, picked points and draft
    // geometry are untouched. The typed ZOOMEXTENTS keeps its guard and its refusal message.
    //
    // A double-click is not a drag, so middle-drag pan below is unaffected (REQ-045 requires it).
    // (The gesture itself is raised OUTSIDE this block — see below — because this block is skipped
    // entirely while a floating viewport owns pan/zoom, and the gesture must work there too.)

    // ORBIT (REQ-058): Shift + middle-drag tumbles the camera about the pan point, matching
    // AutoCAD's 3DORBIT binding. Plain middle-drag still pans, so nothing existing changes.
    // Model space only — a paper sheet is 2D (ADR-025 (g)) and must never tilt.
    // The ORBIT command (REQ-084 (c)) adds a LEFT-drag route to the same math, exactly as the PAN
    // command does below for panning — one orbit implementation, two ways in.
    const bool orbitDrag = modelSpace &&
                           ((ImGui::GetIO().KeyShift && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) ||
                            (cmd.active == AppCommandState::Kind::Orbit &&
                             ImGui::IsMouseDragging(ImGuiMouseButton_Left)));
    if (orbitDrag) {
      const ImVec2 d = ImGui::GetIO().MouseDelta;
      // Degrees per pixel: a full window drag sweeps roughly half a turn horizontally, which is
      // the responsiveness AutoCAD's constrained orbit has.
      constexpr float kDegPerPx = 0.4f;
      Camera c = CadViewCamera(cmd);
      // CAMERA-relative orbit (user's stated convention): dragging left swings the camera left, so
      // the model appears to rotate RIGHT; dragging up lifts the camera, so the model rotates DOWN.
      // Increasing azimuth spins the scene counter-clockwise on screen, so a leftward drag
      // (negative d.x) must DECREASE azimuth — hence both coefficients are positive.
      c.Orbit(d.x * kDegPerPx, d.y * kDegPerPx);
      cmd.viewAnimActive = false;  // a hand orbit wins over any ViewCube animation still easing
      cmd.viewportAzimuthDeg = c.azimuthDeg;
      cmd.viewportElevationDeg = c.elevationDeg;
      cmd.viewportRollDeg = 0.f;  // a free orbit is world-referenced; drop any tilted-PLAN roll (#153)
    }

    // PAN command (REQ-045): while pan mode is active, a LEFT-drag pans the view exactly like the
    // built-in middle-drag (which keeps working). Both routes share the same view-pan math.
    const bool panDrag = !orbitDrag &&
                         (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) ||
                          (cmd.active == AppCommandState::Kind::Pan &&
                           ImGui::IsMouseDragging(ImGuiMouseButton_Left)));
    if (panDrag) {
      ImVec2 d = ImGui::GetIO().MouseDelta;
      const double aspectD = static_cast<double>(avail.x) / static_cast<double>(std::max(avail.y, 1.f));
      const double halfH = (1.0 / std::max(static_cast<double>(*zoom), 1.e-9)) * 50.0;
      const double halfW = halfH * aspectD;
      const double wx = (static_cast<double>(d.x) / static_cast<double>(std::max(avail.x, 1.f))) * (2.0 * halfW);
      const double wy = (static_cast<double>(d.y) / static_cast<double>(std::max(avail.y, 1.f))) * (2.0 * halfH);
      if (modelSpace && !CadViewIsPlan(cmd)) {
        // Pan along the CAMERA's right/up axes, not world X/Y (REQ-058). Once the view tilts, the
        // screen axes no longer line up with world axes, so moving the target in world XY makes
        // the model slide diagonally and at the wrong rate — the "not 1 to 1" feel. Moving along
        // the camera basis keeps the point under the cursor under the cursor, which is what pan
        // means. The up axis has a Z component when tilted, which is why the target needs a Z.
        float R[16];
        CadViewCamera(cmd).ViewRotation(R);
        const double rx = R[0], ry = R[4], rz = R[8];  // camera right, in world axes
        const double ux = R[1], uy = R[5], uz = R[9];  // camera up, in world axes
        *panX += -rx * wx + ux * wy;
        *panY += -ry * wx + uy * wy;
        cmd.viewportPanZ += -rz * wx + uz * wy;
      } else {
        *panX -= wx;
        *panY += wy;
      }
    }
  }

  // Publish the viewport size so the command layer can project geometry to screen for box-select
  // under an orbited camera (REQ-058) — it has no other way to learn the aspect.
  cmd.uiViewportWidthPx = avail.x;
  cmd.uiViewportHeightPx = avail.y;

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
        // PAN command (REQ-045): left-drag pans the viewport's model framing like the middle-drag.
        const bool vpPanDrag = ImGui::IsMouseDragging(ImGuiMouseButton_Middle) ||
                               (cmd.active == AppCommandState::Kind::Pan &&
                                ImGui::IsMouseDragging(ImGuiMouseButton_Left));
        if (vpPanDrag) {
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

  // Paper-space object snap (REQ-037, paper-only): snap the cursor to nearby paper geometry. Computed once
  // per frame and reused by paper LINE/TEXT creation and the MOVE/COPY/ROTATE pick points (not entity picking).
  bool paperSnapActive = false;
  float paperSnapXIn = 0.f, paperSnapYIn = 0.f;
  if (!modelSpace && !InFloatingModelSpace(cmd) && cmd.activeSpaceIndex >= 0 &&
      cmd.activeSpaceIndex < static_cast<int>(cmd.paperLayouts.size()) && hovered) {
    float rawX = 0.f, rawY = 0.f;
    screenToPaperIn(&rawX, &rawY);
    const float pxPerIn = avail.x / std::max(1.e-6f, static_cast<float>(worldRight - worldLeft));
    const float snapTolIn = 10.f / std::max(1.e-6f, pxPerIn);
    paperSnapActive = SnapPaperInchPoint(cmd.paperLayouts[static_cast<size_t>(cmd.activeSpaceIndex)], rawX, rawY,
                                         snapTolIn, &paperSnapXIn, &paperSnapYIn);
  }
  // Snapped paper-inch pick: the snap point when one is in range, else the raw cursor.
  auto paperPick = [&](float* x, float* y) {
    if (paperSnapActive) {
      *x = paperSnapXIn;
      *y = paperSnapYIn;
    } else {
      screenToPaperIn(x, y);
    }
  };

  // Paper-space LINE creation (REQ-037): clicks are paper inches; SubmitLineVertex routes the commit to
  // the active layout's paper store (ActivePaperGeometryTarget). Esc finishes via the global handler.
  if (!modelSpace && !InFloatingModelSpace(cmd) && cmd.active == AppCommandState::Kind::Line && hovered &&
      ImGui::IsMouseClicked(ImGuiMouseButton_Left) && mx >= 0 && mx < avail.x && my >= 0 && my < avail.y) {
    float px = 0.f, py = 0.f;
    paperPick(&px, &py);
    // ORTHO (REQ-037): constrain the segment to horizontal/vertical from the anchor — unless snapped to a
    // point (object snap overrides ortho, matching AutoCAD).
    if (!paperSnapActive && cmd.orthoMode && cmd.linePhase == AppCommandState::LinePhase::NeedNextPoint) {
      if (std::fabs(px - cmd.anchorX) >= std::fabs(py - cmd.anchorY))
        py = cmd.anchorY;
      else
        px = cmd.anchorX;
    }
    SubmitLineVertex(cmd, px, py, log);
    consumedPaperClick = true;
  }

  // Paper-space RECT (REQ-037 / REQ-053): both corners are picked in paper inches; CommitRectangle routes
  // the closed polyline to the active layout's paper store. ORTHO is not applied — the shape is already
  // axis-aligned, and constraining a corner would collapse it to a line.
  if (!modelSpace && !InFloatingModelSpace(cmd) && cmd.active == AppCommandState::Kind::Rect && hovered &&
      ImGui::IsMouseClicked(ImGuiMouseButton_Left) && mx >= 0 && mx < avail.x && my >= 0 && my < avail.y) {
    float px = 0.f, py = 0.f;
    paperPick(&px, &py);
    if (cmd.rectPhase == AppCommandState::RectPhase::WaitFirstCorner) {
      cmd.rectX1 = px;
      cmd.rectY1 = py;
      cmd.anchorX = px;
      cmd.anchorY = py;
      cmd.rectPhase = AppCommandState::RectPhase::WaitSecondCorner;
      log.push_back("RECT — pick the opposite corner (or type X,Y / @dx,dy):");
    } else {
      CommitRectangle(cmd, cmd.rectX1, cmd.rectY1, px, py, log);
    }
    consumedPaperClick = true;
  }

  // Paper-space TEXT insertion (REQ-037): the click sets the insertion point in paper inches; height,
  // rotation and the string are then entered on the command line (space-aware commit → paper store).
  if (!modelSpace && !InFloatingModelSpace(cmd) && cmd.active == AppCommandState::Kind::Text &&
      cmd.textPhase == AppCommandState::TextCmdPhase::WaitInsertion && hovered &&
      ImGui::IsMouseClicked(ImGuiMouseButton_Left) && mx >= 0 && mx < avail.x && my >= 0 && my < avail.y) {
    float px = 0.f, py = 0.f;
    paperPick(&px, &py);
    cmd.textInsX = px;
    cmd.textInsY = py;
    cmd.textPhase = AppCommandState::TextCmdPhase::WaitHeight;
    consumedPaperClick = true;
    log.push_back("TEXT — height in paper inches (Enter = sheet default):");
  }

  // Paper-space PASTE (REQ-038, ADR-013): the click places the clipboard into the active layout's paper
  // store at the cursor (paper inches, snapped). The pasted entities become the paper selection.
  if (!modelSpace && !InFloatingModelSpace(cmd) && cmd.active == AppCommandState::Kind::Paste &&
      cmd.modifyPhase == AppCommandState::ModifyPhase::NeedDestination && hovered &&
      ImGui::IsMouseClicked(ImGuiMouseButton_Left) && mx >= 0 && mx < avail.x && my >= 0 && my < avail.y) {
    float px = 0.f, py = 0.f;
    paperPick(&px, &py);
    CommitClipboardPasteAt(cmd, px, py, log);
    consumedPaperClick = true;
  }

  // Paper-space viewport selection + grip edit + MOVE/COPY (REQ-035). Active only while idle of the
  // rectangular-viewport command and in a paper layout.
  if (!modelSpace && !InFloatingModelSpace(cmd) && cmd.active == AppCommandState::Kind::None &&
      !cmd.mtextRichEditorOpen && !cmd.tableCellEditorOpen && cmd.activeSpaceIndex >= 0 &&
      cmd.activeSpaceIndex < static_cast<int>(cmd.paperLayouts.size())) {
    PaperLayout& L = cmd.paperLayouts[static_cast<size_t>(cmd.activeSpaceIndex)];
    float curX = 0.f, curY = 0.f;
    screenToPaperIn(&curX, &curY);
    // Snap MOVE/COPY/ROTATE/MIRROR pick points to paper geometry (REQ-037); entity-selection clicks
    // stay on the raw cursor so picking small objects is not deflected.
    if (paperSnapActive && (cmd.paperMovePhase != 0 || cmd.paperRotatePhase != 0 || cmd.paperMirrorPhase != 0)) {
      curX = paperSnapXIn;
      curY = paperSnapYIn;
    }
    const float pxPerWorld = avail.x / std::max(1.e-6f, static_cast<float>(worldRight - worldLeft));
    const float gripTolIn = 7.f / std::max(1.e-6f, pxPerWorld);
    // Pick tolerance for native paper entities (line/text/…), used by hover, click-select and double-click edit.
    const float entityPickTolIn = std::max(gripTolIn, 6.f / std::max(1.e-6f, pxPerWorld));
    const bool clickL = hovered && !consumedPaperClick && cmd.active != AppCommandState::Kind::Pan &&
                        ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                        mx >= 0 && mx < avail.x && my >= 0 && my < avail.y;

    // Double-click inside a viewport → floating model space (REQ-036). A real double-click is two clicks
    // at the same spot, so it's distinct from a two-corner window box. The first click may have started a
    // window box (interior clicks do) — cancel it and enter the viewport instead.
    bool enteredFloat = false;
    bool openedPaperTextEdit = false;
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && mx >= 0 && mx < avail.x && my >= 0 &&
        my < avail.y && cmd.paperMovePhase == 0 && cmd.paperGripCorner == -2) {
      // REQ-039 phase 2: double-clicking a native paper text opens the in-place editor (takes priority over
      // entering a viewport's floating model space).
      PaperEntityRef dpr;
      if (PickPaperEntityAt(L, curX, curY, entityPickTolIn, &dpr) && dpr.type == PaperEntityRef::Type::Text) {
        cmd.paperSelBoxActive = false;  // discard the box the first click of the double-click started
        ClearViewportSelection(cmd);
        ClearPaperEntitySelection(cmd);
        TogglePaperEntitySelection(cmd, dpr, false);
        OpenPaperTextEditor(cmd, cmd.activeSpaceIndex, dpr.index, &log);
        openedPaperTextEdit = true;
      }
    }
    if (!openedPaperTextEdit && hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && mx >= 0 &&
        mx < avail.x && my >= 0 && my < avail.y && cmd.paperMovePhase == 0 && cmd.paperGripCorner == -2) {
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
      if (cmd.paperMovePhase != 0 || cmd.paperRotatePhase != 0 || cmd.paperMirrorPhase != 0 ||
          cmd.paperLengthenPhase != 0 || cmd.paperExtendPhase != 0 || cmd.paperBreakPhase != 0 ||
          cmd.paperStretchPhase != 0 || cmd.paperFilletPhase != 0 || cmd.paperChamferPhase != 0 ||
          cmd.paperGripCorner != -2 || cmd.paperSelBoxActive ||
          cmd.paperMoveWaitingSelection || cmd.paperDeleteWaitingSelection) {  // REQ-307
        cmd.paperMovePhase = 0;
        cmd.paperRotatePhase = 0;
        cmd.paperMirrorPhase = 0;
        cmd.paperLengthenPhase = 0;
        cmd.paperExtendPhase = 0;
        cmd.paperExtendBoundaries.clear();
        cmd.paperBreakPhase = 0;
        cmd.paperStretchPhase = 0;
        cmd.paperFilletPhase = 0;
        cmd.paperFilletFirstEntity = PaperEntityRef{};
        cmd.paperFilletFirstPolySeg = -1;
        cmd.paperChamferPhase = 0;
        cmd.paperChamferFirstEntity = PaperEntityRef{};
        cmd.paperChamferFirstPolySeg = -1;
        cmd.paperGripCorner = -2;
        cmd.paperSelBoxActive = false;
        cmd.paperMoveWaitingSelection = false;
        cmd.paperDeleteWaitingSelection = false;
      } else if (!cmd.selectedViewports.empty() || !cmd.selectedPaperEntities.empty()) {
        ClearViewportSelection(cmd);
        ClearPaperEntitySelection(cmd);
      }
    }
    // EXTEND: Enter advances boundary collection -> target picking (needs >=1 boundary), or
    // finishes target picking — the same "done picking edges" signal model-space TRIM/EXTEND get
    // from a real Enter, which the rest of this per-frame paper-click block has no other route to.
    if (cmd.paperExtendPhase != 0 && ImGui::IsKeyPressed(ImGuiKey_Enter)) {
      if (cmd.paperExtendPhase == 1) {
        if (cmd.paperExtendBoundaries.empty())
          log.push_back("EXTEND — pick at least one boundary edge before pressing Enter.");
        else {
          cmd.paperExtendPhase = 2;
          log.push_back("EXTEND — click objects to extend. Enter when done.");
        }
      } else {
        cmd.paperExtendPhase = 0;
        cmd.paperExtendBoundaries.clear();
        log.push_back("EXTEND — finished.");
      }
    }
    // REQ-307 (GitHub #106): Enter is what acts on the paper-space selection step, the same shape
    // model-space MOVE/COPY/DELETE use (ProcessCommandLineSubmit's PickSelection branches) — but
    // these never set cmd.active, so that dispatcher never sees them via its Kind-keyed switch; the
    // shared logic lives in CadCommands.cpp (ProcessPaperMoveWaitingSelectionEnter/
    // ProcessPaperDeleteWaitingSelectionEnter) so ProcessCommandLineSubmit's own blank-Enter branch
    // can call it too — this raw check is only for Enter pressed while the mouse/focus never touched
    // the command line, the same reason EXTEND's paper phase above needs one.
    //
    // GetActiveID() == 0 is the guard that keeps the two callers from double-firing on one keypress:
    // when the command-line InputText holds keyboard focus, its own Enter submit already reaches
    // ProcessCommandLineSubmit, whose blank-line branch calls the identical function — this raw poll
    // must stay silent then, or a focused blank Enter would advance/delete twice in one frame.
    if (ImGui::GetActiveID() == 0 && ImGui::IsKeyPressed(ImGuiKey_Enter)) {
      ProcessPaperMoveWaitingSelectionEnter(cmd, log);
      ProcessPaperDeleteWaitingSelectionEnter(cmd, log);
    }

    auto primaryVp = [&]() -> Viewport* {
      if (cmd.selectedViewports.size() == 1 && cmd.selectedViewportIndex >= 0 &&
          cmd.selectedViewportIndex < static_cast<int>(L.viewports.size()))
        return &L.viewports[static_cast<size_t>(cmd.selectedViewportIndex)];
      return nullptr;
    };

    // Hover pre-highlight parity (REQ-039): when idle and not mid-gesture, light up the paper entity under the
    // cursor (mirrors the model viewportHoverEntity). Cleared otherwise so the highlight does not linger.
    cmd.paperHoverValid = false;
    if (hovered && !cmd.paperSelBoxActive && cmd.paperMovePhase == 0 && cmd.paperRotatePhase == 0 &&
        cmd.paperMirrorPhase == 0 && cmd.paperLengthenPhase == 0 && cmd.paperExtendPhase == 0 &&
        cmd.paperBreakPhase == 0 && cmd.paperStretchPhase == 0 && cmd.paperFilletPhase == 0 &&
        cmd.paperChamferPhase == 0 &&
        cmd.paperGripCorner == -2 && mx >= 0 && mx < avail.x && my >= 0 && my < avail.y) {
      PaperEntityRef hr;
      if (PickPaperEntityAt(L, curX, curY, entityPickTolIn, &hr)) {
        cmd.paperHoverValid = true;
        cmd.paperHover = hr;
      }
    }
    auto tryPaperEntityClick = [&]() -> bool {
      PaperEntityRef pr;
      if (!PickPaperEntityAt(L, curX, curY, entityPickTolIn, &pr))
        return false;
      if (!ImGui::GetIO().KeyShift)
        ClearViewportSelection(cmd);
      TogglePaperEntitySelection(cmd, pr, ImGui::GetIO().KeyShift);
      return true;
    };

    // Close an open window-select box at (closeX,closeY) (REQ-039). Shared by click-click (second click) and
    // press-drag-release. Same convention as model geometry: left-to-right = window (fully inside);
    // right-to-left = crossing (any overlap). Selects viewports and every native paper-entity type.
    auto closePaperSelBox = [&](float closeX, float closeY) {
      const bool windowMode = closeX >= cmd.paperSelBoxX0In;
      const float bx0 = std::min(cmd.paperSelBoxX0In, closeX), bx1 = std::max(cmd.paperSelBoxX0In, closeX);
      const float by0 = std::min(cmd.paperSelBoxY0In, closeY), by1 = std::max(cmd.paperSelBoxY0In, closeY);
      cmd.selectedViewports.clear();
      for (int vi = 0; vi < static_cast<int>(L.viewports.size()); ++vi) {
        const Viewport& v = L.viewports[static_cast<size_t>(vi)];
        const float vx0 = v.paperXIn, vy0 = v.paperYIn, vx1 = v.paperXIn + v.paperWIn, vy1 = v.paperYIn + v.paperHIn;
        // A viewport is a hollow rectangle outline, not a filled object. WINDOW (L→R) selects it only when
        // the whole border is inside the box; CROSSING (R→L) selects it only when the box actually touches a
        // border edge — a box drawn entirely inside the viewport interior touches no visible geometry and
        // must NOT select it (issue #4).
        const bool overlap = vx0 <= bx1 && vx1 >= bx0 && vy0 <= by1 && vy1 >= by0;
        const bool boxInsideInterior = bx0 > vx0 && bx1 < vx1 && by0 > vy0 && by1 < vy1;
        const bool sel = windowMode ? (vx0 >= bx0 && vx1 <= bx1 && vy0 >= by0 && vy1 <= by1)
                                    : (overlap && !boxInsideInterior);
        if (sel)
          cmd.selectedViewports.push_back(vi);
      }
      cmd.selectedViewportIndex = cmd.selectedViewports.empty() ? -1 : cmd.selectedViewports.back();
      cmd.selectedViewportLayout = cmd.selectedViewports.empty() ? -1 : cmd.activeSpaceIndex;
      // Native paper geometry in the same box (REQ-039): one shared, unit-tested helper for every type.
      cmd.selectedPaperEntities.clear();
      SelectPaperEntitiesInBox(L, bx0, by0, bx1, by1, windowMode, cmd.selectedPaperEntities, &cmd.blockDefs);
      // REQ-103 STRETCH: remember this box regardless of which command (if any) is active — paper
      // box-select is ambient (this whole lambda runs on any plain click-drag, not gated by
      // cmd.active), and STRETCH is invoked AFTER a selection already exists, the same "pre-select,
      // then invoke" convention paper ROTATE/SCALE use. Invalidated by ClearPaperEntitySelection/
      // TogglePaperEntitySelection so a later plain click-select doesn't leave a stale box behind.
      cmd.paperSelBoxLastValid = true;
      cmd.paperSelBoxLastMnXIn = bx0;
      cmd.paperSelBoxLastMxXIn = bx1;
      cmd.paperSelBoxLastMnYIn = by0;
      cmd.paperSelBoxLastMxYIn = by1;
      cmd.paperSelBoxActive = false;
      BumpCadGpuCache(cmd);
    };

    // REQ-307 (GitHub #106): the paper-space selection step's box MERGES into the accumulating
    // selection rather than replacing it — the paper-space analog of model-space
    // ViewportClickRoute::SelectionAccumulate (D-2026-08-25-l). Same box math as closePaperSelBox
    // above; the only difference is union instead of clear-then-assign.
    auto closePaperSelBoxMerge = [&](float closeX, float closeY) {
      const bool windowMode = closeX >= cmd.paperSelBoxX0In;
      const float bx0 = std::min(cmd.paperSelBoxX0In, closeX), bx1 = std::max(cmd.paperSelBoxX0In, closeX);
      const float by0 = std::min(cmd.paperSelBoxY0In, closeY), by1 = std::max(cmd.paperSelBoxY0In, closeY);
      for (int vi = 0; vi < static_cast<int>(L.viewports.size()); ++vi) {
        const Viewport& v = L.viewports[static_cast<size_t>(vi)];
        const float vx0 = v.paperXIn, vy0 = v.paperYIn, vx1 = v.paperXIn + v.paperWIn, vy1 = v.paperYIn + v.paperHIn;
        const bool overlap = vx0 <= bx1 && vx1 >= bx0 && vy0 <= by1 && vy1 >= by0;
        const bool boxInsideInterior = bx0 > vx0 && bx1 < vx1 && by0 > vy0 && by1 < vy1;
        const bool sel = windowMode ? (vx0 >= bx0 && vx1 <= bx1 && vy0 >= by0 && vy1 <= by1)
                                    : (overlap && !boxInsideInterior);
        if (sel && std::find(cmd.selectedViewports.begin(), cmd.selectedViewports.end(), vi) ==
                       cmd.selectedViewports.end())
          cmd.selectedViewports.push_back(vi);
      }
      cmd.selectedViewportIndex = cmd.selectedViewports.empty() ? -1 : cmd.selectedViewports.back();
      cmd.selectedViewportLayout = cmd.selectedViewports.empty() ? -1 : cmd.activeSpaceIndex;
      std::vector<PaperEntityRef> boxEntities;
      SelectPaperEntitiesInBox(L, bx0, by0, bx1, by1, windowMode, boxEntities, &cmd.blockDefs);
      for (const auto& e : boxEntities) {
        bool present = false;
        for (const auto& s : cmd.selectedPaperEntities)
          if (s.type == e.type && s.index == e.index) { present = true; break; }
        if (!present)
          cmd.selectedPaperEntities.push_back(e);
      }
      cmd.paperSelBoxLastValid = true;
      cmd.paperSelBoxLastMnXIn = bx0;
      cmd.paperSelBoxLastMxXIn = bx1;
      cmd.paperSelBoxLastMnYIn = by0;
      cmd.paperSelBoxLastMxYIn = by1;
      cmd.paperSelBoxActive = false;
      BumpCadGpuCache(cmd);
    };

    if (clickL && (cmd.paperMoveWaitingSelection || cmd.paperDeleteWaitingSelection)) {
      // REQ-307: additive click-or-box accumulation for the paper-space selection step, mirroring
      // model-space SelectionAccumulate (D-2026-08-25-l) — a click toggles one object into the set
      // with no Shift required (SelectViewport/TogglePaperEntitySelection's own `additive=true`
      // already implements exactly this toggle), and a box MERGES rather than replaces.
      if (cmd.paperSelBoxActive) {
        closePaperSelBoxMerge(curX, curY);
      } else {
        PaperEntityRef pr;
        if (PickPaperEntityAt(L, curX, curY, entityPickTolIn, &pr)) {
          TogglePaperEntitySelection(cmd, pr, /*additive=*/true);
        } else {
          // Viewport BORDER hit test, same band/interior rule as the idle fallback below (clicking
          // the interior is the model view, so it does not select).
          const float bt = std::max(gripTolIn, 5.f / std::max(1.e-6f, pxPerWorld));
          int hit = -1;
          for (int vi = static_cast<int>(L.viewports.size()) - 1; vi >= 0; --vi) {
            const Viewport& v = L.viewports[static_cast<size_t>(vi)];
            const float x0 = v.paperXIn, y0 = v.paperYIn, x1 = v.paperXIn + v.paperWIn, y1 = v.paperYIn + v.paperHIn;
            const float btv = std::min(bt, 0.25f * std::min(v.paperWIn, v.paperHIn));
            const bool inOuter = curX >= x0 - btv && curX <= x1 + btv && curY >= y0 - btv && curY <= y1 + btv;
            const bool inInner = curX >= x0 + btv && curX <= x1 - btv && curY >= y0 + btv && curY <= y1 - btv;
            if (inOuter && !inInner) { hit = vi; break; }
          }
          if (hit >= 0) {
            SelectViewport(cmd, hit, /*additive=*/true);
          } else {
            cmd.paperSelBoxActive = true;
            cmd.paperSelBoxX0In = curX;
            cmd.paperSelBoxY0In = curY;
          }
        }
      }
    } else if (clickL && cmd.paperMovePhase == 1) {  // MOVE/COPY: base point
      cmd.paperMoveBaseXIn = curX;
      cmd.paperMoveBaseYIn = curY;
      cmd.paperMovePhase = 2;
      log.push_back("Click the destination point.");
    } else if (clickL && cmd.paperMovePhase == 2) {  // MOVE/COPY: destination
      const float ddx = curX - cmd.paperMoveBaseXIn, ddy = curY - cmd.paperMoveBaseYIn;
      if (!cmd.selectedPaperEntities.empty())
        TranslateSelectedPaperEntities(cmd, ddx, ddy, cmd.paperMoveIsCopy, log);  // REQ-037
      if (!cmd.selectedViewports.empty())
        TranslateSelectedViewports(cmd, ddx, ddy, cmd.paperMoveIsCopy, log);  // REQ-035
      cmd.paperMovePhase = 0;
    } else if (clickL && cmd.paperRotatePhase == 1) {  // ROTATE: base point (REQ-037)
      cmd.paperRotateBaseXIn = curX;
      cmd.paperRotateBaseYIn = curY;
      cmd.paperRotatePhase = 2;
      log.push_back("Click a point to set the rotation angle.");
    } else if (clickL && cmd.paperRotatePhase == 2) {  // ROTATE: angle (REQ-037)
      const float ang = std::atan2(curY - cmd.paperRotateBaseYIn, curX - cmd.paperRotateBaseXIn);
      RotateSelectedPaperEntities(cmd, cmd.paperRotateBaseXIn, cmd.paperRotateBaseYIn, ang, log);
      cmd.paperRotatePhase = 0;
    } else if (clickL && cmd.paperMirrorPhase == 1) {  // MIRROR: first mirror-line point (REQ-103)
      cmd.paperMirrorP1XIn = curX;
      cmd.paperMirrorP1YIn = curY;
      cmd.paperMirrorPhase = 2;
      log.push_back("Click the second point of the mirror line.");
    } else if (clickL && cmd.paperMirrorPhase == 2) {  // MIRROR: second point, commits (REQ-103)
      if (std::hypot(curX - cmd.paperMirrorP1XIn, curY - cmd.paperMirrorP1YIn) < 1e-6f)
        log.push_back("MIRROR — mirror line needs two distinct points; click again.");
      else
        MirrorSelectedPaperEntities(cmd, cmd.paperMirrorP1XIn, cmd.paperMirrorP1YIn, curX, curY, log);
      cmd.paperMirrorPhase = 0;
    } else if (clickL && cmd.paperLengthenPhase == 1) {  // LENGTHEN: pick + apply (REQ-103)
      PaperEntityRef pr;
      if (!PickPaperEntityAt(L, curX, curY, entityPickTolIn, &pr))
        log.push_back("LENGTHEN — nothing under cursor; try again.");
      else
        ApplyLengthenToPaperEntity(cmd, pr, curX, curY, log);
      // Stays in phase 1 — loop back for the next object, same shape as the model-space command.
    } else if (clickL && cmd.paperExtendPhase == 1) {  // EXTEND: collect boundary edges (REQ-103)
      PaperEntityRef pr;
      if (!PickPaperEntityAt(L, curX, curY, entityPickTolIn, &pr))
        log.push_back("EXTEND — no object at pick.");
      else if (pr.type == PaperEntityRef::Type::Text)
        log.push_back("EXTEND — use a line, circle, arc, ellipse, or polyline as a boundary edge.");
      else {
        bool dup = false;
        for (const auto& b : cmd.paperExtendBoundaries)
          if (b.type == pr.type && b.index == pr.index) { dup = true; break; }
        if (dup)
          log.push_back("EXTEND — already a boundary edge.");
        else {
          cmd.paperExtendBoundaries.push_back(pr);
          log.push_back("EXTEND — boundary edge added.");
        }
      }
      // Stays in phase 1 — Enter (handled above) advances to target-picking.
    } else if (clickL && cmd.paperExtendPhase == 2) {  // EXTEND: pick + extend a target (REQ-103)
      PaperEntityRef pr;
      if (!PickPaperEntityAt(L, curX, curY, entityPickTolIn, &pr))
        log.push_back("EXTEND — nothing under cursor; try again.");
      else
        ApplyExtendToPaperEntity(cmd, pr, curX, curY, log);
      // Stays in phase 2 — loop back for the next object, same shape as the model-space command.
    } else if (clickL && cmd.paperBreakPhase == 1) {  // BREAK: select entity + break point 1 (REQ-103)
      PaperEntityRef pr;
      if (!PickPaperEntityAt(L, curX, curY, entityPickTolIn, &pr))
        log.push_back("BREAK — no object at pick.");
      else if (pr.type == PaperEntityRef::Type::Text)
        log.push_back("BREAK — 1 text object ignored: text cannot be broken. Pick a line, circle, "
                      "arc, or polyline.");
      else if (pr.type == PaperEntityRef::Type::Ellipse)
        log.push_back("BREAK — 1 ellipse ignored: every ellipse in this drawing is a full closed "
                      "curve, and GoSurvey has no elliptical-arc entity kind to hold a broken-open "
                      "ellipse. Pick a line, circle, arc, or polyline.");
      else {
        BreakPoint p1{};
        if (!ClosestPointOnPaperEntity(L, pr, curX, curY, &p1))
          log.push_back("BREAK — could not resolve a point on that object; try again.");
        else {
          cmd.paperBreakEntity = pr;
          cmd.paperBreakP1 = p1;
          cmd.paperBreakPhase = 2;
          log.push_back("BREAK — specify second break point:");
        }
      }
    } else if (clickL && cmd.paperBreakPhase == 2) {  // BREAK: second point, applies (REQ-103)
      ApplyBreakToPaperEntity(cmd, cmd.paperBreakEntity, cmd.paperBreakP1, curX, curY, log);
      cmd.paperBreakPhase = 1;
      // Stays in the loop — back to phase 1 for the next object, same shape as model-space BREAK.
    } else if (clickL && cmd.paperFilletPhase == 1) {  // FILLET: pick first curve (REQ-103 step 6a)
      PaperEntityRef pr;
      int polySeg = -1;
      if (!PickPaperEntityAt(L, curX, curY, entityPickTolIn, &pr))
        log.push_back("FILLET — no object at pick.");
      else if (PaperFilletEligibility(L, pr, curX, curY, &polySeg, log)) {
        cmd.paperFilletFirstEntity = pr;
        cmd.paperFilletFirstPolySeg = polySeg;
        cmd.paperFilletFirstPickX = curX;
        cmd.paperFilletFirstPickY = curY;
        cmd.paperFilletPhase = 2;
        log.push_back("FILLET — select second object:");
      }
      // Ineligible picks stay in phase 1 — PaperFilletEligibility already logged why.
    } else if (clickL && cmd.paperFilletPhase == 2) {  // FILLET: pick second curve, applies (REQ-103 step 6a)
      PaperEntityRef pr;
      int polySeg = -1;
      if (!PickPaperEntityAt(L, curX, curY, entityPickTolIn, &pr))
        log.push_back("FILLET — no object at pick.");
      else if (PaperFilletEligibility(L, pr, curX, curY, &polySeg, log)) {
        ApplyFilletToPaperEntities(cmd, cmd.paperFilletFirstEntity, cmd.paperFilletFirstPolySeg,
                                   cmd.paperFilletFirstPickX, cmd.paperFilletFirstPickY, pr, polySeg, curX, curY,
                                   log);
      }
      cmd.paperFilletPhase = 1;
      cmd.paperFilletFirstEntity = PaperEntityRef{};
      cmd.paperFilletFirstPolySeg = -1;
      // Stays in the loop — back to phase 1 for the next corner, same shape as model-space FILLET.
    } else if (clickL && cmd.paperChamferPhase == 1) {  // CHAMFER: pick first curve (REQ-103 step 6b)
      PaperEntityRef pr;
      int polySeg = -1;
      if (!PickPaperEntityAt(L, curX, curY, entityPickTolIn, &pr))
        log.push_back("CHAMFER — no object at pick.");
      else if (PaperChamferEligibility(L, pr, curX, curY, &polySeg, log)) {
        cmd.paperChamferFirstEntity = pr;
        cmd.paperChamferFirstPolySeg = polySeg;
        cmd.paperChamferFirstPickX = curX;
        cmd.paperChamferFirstPickY = curY;
        cmd.paperChamferPhase = 2;
        log.push_back("CHAMFER — select second object:");
      }
    } else if (clickL && cmd.paperChamferPhase == 2) {  // CHAMFER: pick second curve, applies (REQ-103 step 6b)
      PaperEntityRef pr;
      int polySeg = -1;
      if (!PickPaperEntityAt(L, curX, curY, entityPickTolIn, &pr))
        log.push_back("CHAMFER — no object at pick.");
      else if (PaperChamferEligibility(L, pr, curX, curY, &polySeg, log)) {
        ApplyChamferToPaperEntities(cmd, cmd.paperChamferFirstEntity, cmd.paperChamferFirstPolySeg,
                                    cmd.paperChamferFirstPickX, cmd.paperChamferFirstPickY, pr, polySeg, curX,
                                    curY, log);
      }
      cmd.paperChamferPhase = 1;
      cmd.paperChamferFirstEntity = PaperEntityRef{};
      cmd.paperChamferFirstPolySeg = -1;
      // Stays in the loop — back to phase 1 for the next corner, same shape as model-space CHAMFER.
    } else if (clickL && cmd.paperStretchPhase == 1) {  // STRETCH: base point (REQ-103 step 5)
      cmd.paperStretchBaseXIn = curX;
      cmd.paperStretchBaseYIn = curY;
      cmd.paperStretchPhase = 2;
      log.push_back("STRETCH — specify destination point:");
    } else if (clickL && cmd.paperStretchPhase == 2) {  // STRETCH: destination, applies (REQ-103 step 5)
      const float ddx = curX - cmd.paperStretchBaseXIn, ddy = curY - cmd.paperStretchBaseYIn;
      ApplyStretchToPaperSelection(cmd, ddx, ddy, log);
      // Stays active — same selection+box at new position, ready for another base+destination
      // (MOVE's own looping shape for repeated displacement rounds on one selection).
      cmd.paperStretchPhase = 1;
      log.push_back("STRETCH complete — base point (ESC to exit):");
    } else if (clickL && cmd.paperGripCorner != -2) {  // commit an in-progress grip edit
      cmd.paperGripCorner = -2;
      log.push_back("Viewport edited.");
    } else if (clickL && cmd.paperSelBoxActive) {
      // A window-select box is open — this click closes it (priority over grip/body picking). Click-click flow.
      closePaperSelBox(curX, curY);
    } else if (clickL && !enteredFloat && !openedPaperTextEdit) {
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
      } else if (tryPaperEntityClick()) {
        // 2) a native paper entity (line/text) under the cursor → selected (REQ-037).
      } else {
        // 3) a viewport BORDER (topmost wins)? Clicking the interior is the model view, so it does not
        // select — that lets a window box start even when viewports cover the sheet (AutoCAD behavior).
        const float bt = std::max(gripTolIn, 5.f / std::max(1.e-6f, pxPerWorld));
        int hit = -1;
        for (int vi = static_cast<int>(L.viewports.size()) - 1; vi >= 0; --vi) {
          const Viewport& v = L.viewports[static_cast<size_t>(vi)];
          const float x0 = v.paperXIn, y0 = v.paperYIn, x1 = v.paperXIn + v.paperWIn, y1 = v.paperYIn + v.paperHIn;
          // Border hit band, clamped so a small/zoomed-out viewport keeps a non-selecting interior (issue #4):
          // only the visible border ring selects, never the inside of the rect.
          const float btv = std::min(bt, 0.25f * std::min(v.paperWIn, v.paperHIn));
          const bool inOuter = curX >= x0 - btv && curX <= x1 + btv && curY >= y0 - btv && curY <= y1 + btv;
          const bool inInner = curX >= x0 + btv && curX <= x1 - btv && curY >= y0 + btv && curY <= y1 - btv;
          if (inOuter && !inInner) {
            hit = vi;
            break;
          }
        }
        if (hit >= 0) {
          if (!ImGui::GetIO().KeyShift)
            ClearPaperEntitySelection(cmd);  // selecting a viewport clears paper-entity selection (REQ-037)
          SelectViewport(cmd, hit, ImGui::GetIO().KeyShift);
        } else {  // interior or empty: start a window-select box
          if (!ImGui::GetIO().KeyShift)
            ClearPaperEntitySelection(cmd);
          cmd.paperSelBoxActive = true;
          cmd.paperSelBoxX0In = curX;
          cmd.paperSelBoxY0In = curY;
        }
      }
    }

    // Press-drag-release window box (bug #1): if the user pressed and dragged more than a few pixels,
    // releasing closes the open box right away (AutoCAD's drag-select). A near-zero drag is treated as a
    // plain click, leaving the box open for the click-click flow above.
    if (cmd.paperSelBoxActive && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
      const ImVec2 dd = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
      if (std::sqrt(dd.x * dd.x + dd.y * dd.y) > 4.f) {
        // REQ-307: a dragged box during the selection step merges, same as the click-click path above.
        if (cmd.paperMoveWaitingSelection || cmd.paperDeleteWaitingSelection)
          closePaperSelBoxMerge(curX, curY);
        else
          closePaperSelBox(curX, curY);
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
  CadSnap::Hit floatingSnapHit{};  // object-snap result inside the floating viewport, for the overlay glyph
  floatingSnapHit.valid = false;
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
        // World units per screen pixel inside this viewport (viewport scale ÷ paper px-per-inch). Drives the
        // px→world tolerances for snapping and hover so they feel like model space.
        const float pxPerPaperIn = avail.x / std::max(1.e-6f, static_cast<float>(worldRight - worldLeft));
        const float worldPerPx = s / std::max(1.e-6f, pxPerPaperIn);
        // Object snapping inside the floating viewport (REQ-036), mirroring the model-space snap path. The
        // snapped point drives the pick, exactly as the model path feeds CadSnap into SubmitViewportPick.
        double curMX = mLocalX, curMY = mLocalY;
        cmd.viewportSnapPickValid = false;
        const bool midCmd = cmd.active != AppCommandState::Kind::None || cmd.showCreatePointsWindow ||
                            cmd.dimGripMoveActive || cmd.entityGripMoveActive || cmd.mtextGripMoveActive;
        // REQ-121 rule (1), floating model space (REQ-036). The same suppression as the model-space
        // seam below: a selection step is a selection step whichever window it happens inside, and
        // leaving this one snapping would make the rule true in model space and false through a
        // viewport — exactly the per-command-accident shape REQ-121 exists to remove.
        if (cmd.objectSnapEnabled && midCmd && !ViewportIsObjectSelectionStep(cmd)) {
          const float tol = std::max(1.e-6f, cmd.objectSnapAperturePx * worldPerPx);
          CadSnap::SnapExclude exclude{};
          if (cmd.entityGripMoveActive && cmd.entityGripEntityIndex >= 0) {
            exclude.valid = true;
            exclude.type = cmd.entityGripType;
            exclude.index = cmd.entityGripEntityIndex;
          }
          const CadSnap::Hit snap = CadSnap::FindBest(static_cast<float>(mLocalX), static_cast<float>(mLocalY),
                                                      cmd, midCmd, tol, exclude);
          if (snap.valid) {
            cmd.viewportSnapPickValid = true;
            cmd.viewportSnapPickLocalX = snap.x;
            cmd.viewportSnapPickLocalY = snap.y;
            curMX = snap.x;
            curMY = snap.y;
            floatingSnapHit = snap;
          }
        }
        // Hover highlight (REQ-036): entity under the cursor when idle, for the in-viewport highlight below.
        // Also active during VPFREEZE/VPTHAW (REQ-046) so the user sees the object they are about to pick.
        cmd.viewportHoverEntityValid = false;
        const bool vpFreezePick =
            cmd.active == AppCommandState::Kind::VpFreeze || cmd.active == AppCommandState::Kind::VpThaw;
        if (!midCmd || vpFreezePick) {
          SelectedEntity hoverHit{};
          float hoverD2 = 0.f;
          // Match the model-space rule: hover activates once geometry is inside the cursor aperture.
          const float hoverTol = std::max(1.e-6f, std::clamp(cmd.objectSnapAperturePx, 4.f, 64.f) * 0.5f * worldPerPx);
          if (PickClosestCadEntity(cmd, static_cast<float>(mLocalX), static_cast<float>(mLocalY), hoverTol,
                                   &hoverHit, &hoverD2)) {
            cmd.viewportHoverEntityValid = true;
            cmd.viewportHoverEntity = hoverHit;
          }
        }
        if (outCursorX && outCursorY) {
          *outCursorX = curMX;
          *outCursorY = curMY;
        }
        if (outCursorRawX && outCursorRawY) {
          *outCursorRawX = mLocalX;
          *outCursorRawY = mLocalY;
        }
        // Click handling, mirroring model space (REQ-036). The entering double-click must NOT also act here,
        // so single clicks only (IsMouseDoubleClicked guards the box/select from arming on viewport entry):
        //   - active command → command point (snapped);
        //   - a box is open  → close it (window L→R / crossing R→L);
        //   - hovering an object → select it directly (Shift toggles), like the model click-to-select;
        //   - empty space    → arm a selection box.
        if (hovered && cmd.active != AppCommandState::Kind::Pan &&
            cmd.active != AppCommandState::Kind::Orbit &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            !ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
          const float gripTolWorld = 10.f * worldPerPx;  // grip hit radius (~10px) in model units
          if (vpFreezePick) {
            // REQ-046: clicking an object freezes/thaws its LAYER in this floating viewport; stay in the
            // command for more picks (Esc/Enter exits). Empty click (no hover) changes nothing.
            if (cmd.viewportHoverEntityValid) {
              auto layerOf = [&](const SelectedEntity& e) -> std::string {
                using T = SelectedEntity::Type;
                auto at = [](const std::vector<EntityAttributes>& v, int idx) -> std::string {
                  return (idx >= 0 && static_cast<size_t>(idx) < v.size()) ? v[static_cast<size_t>(idx)].layer
                                                                           : std::string();
                };
                switch (e.type) {
                case T::LineSeg:      return at(cmd.userLineAttrs, e.index);
                case T::Polyline:     return at(cmd.userPolylineAttrs, e.index);
                case T::Circle:       return at(cmd.userCircleAttrs, e.index);
                case T::Arc:          return at(cmd.userArcAttrs, e.index);
                case T::Ellipse:      return at(cmd.userEllAttrs, e.index);
                case T::Annotation:   return at(cmd.cadAnnotationAttrs, e.index);
                case T::Table:        return at(cmd.cadTableAttrs, e.index);
                case T::BlockRef:     return at(cmd.cadBlockRefAttrs, e.index);
                case T::FilledRegion: return at(cmd.cadFilledRegionAttrs, e.index);
                default:              return std::string();
                }
              };
              const std::string lyr = layerOf(cmd.viewportHoverEntity);
              if (!lyr.empty()) {
                Viewport& fvp = FL.viewports[static_cast<size_t>(cmd.floatingViewportIndex)];
                if (cmd.active == AppCommandState::Kind::VpFreeze) {
                  FreezeLayerInViewport(fvp, lyr);
                  log.push_back("VPFREEZE — froze layer '" + lyr + "' in this viewport.");
                } else {
                  ThawLayerInViewport(fvp, lyr);
                  log.push_back("VPTHAW — thawed layer '" + lyr + "' in this viewport.");
                }
                BumpCadGpuCache(cmd);
              }
            }
          } else if (cmd.entityGripMoveActive) {
            // Commit the grip drag — geometry was updated live by the (ungated) grip-drag block above.
            ClearEntityGripInteraction(cmd);
            BumpCadGpuCache(cmd);
            log.push_back("Grip edit committed.");
          } else if (cmd.active != AppCommandState::Kind::None) {
            UiSubmitViewportPick(cmd, static_cast<float>(curMX), static_cast<float>(curMY), log);
          } else if (cmd.selBoxWaitingSecond) {
            const bool fenceWindowMode = (mx - cmd.selBoxAnchorScreenX) > 3.f;  // L→R window, R→L crossing
            UiSubmitViewportPick(cmd, static_cast<float>(curMX), static_cast<float>(curMY), log,
                               ImGui::GetIO().KeyShift, fenceWindowMode);
          } else if (TryBeginEntityGripAtLocal(cmd, static_cast<float>(curMX), static_cast<float>(curMY),
                                               gripTolWorld)) {
            // Grabbed a grip of a selected entity → the drag block now moves it; consume this click.
          } else if (cmd.viewportHoverEntityValid) {
            const SelectedEntity e = cmd.viewportHoverEntity;  // the highlighted (blue) object under the cursor
            auto it = std::find_if(cmd.selection.begin(), cmd.selection.end(),
                                   [&](const SelectedEntity& x) { return x.type == e.type && x.index == e.index; });
            if (ImGui::GetIO().KeyShift) {            // Shift+click removes
              if (it != cmd.selection.end())
                cmd.selection.erase(it);
            } else if (it == cmd.selection.end()) {   // plain click adds (additive, like model space)
              cmd.selection.push_back(e);
            }
            EnsureAttrCounts(cmd);
          } else {
            BeginSelectionBoxCorner(cmd, static_cast<float>(curMX), static_cast<float>(curMY), mx, my);
          }
        }
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
      using AK = AppCommandState::Kind;
      const bool blockSnapPickMenu = cmd.mtextRichEditorOpen || cmd.tableCellEditorOpen || cmd.selBoxWaitingSecond ||
                                     cmd.dimGripMoveActive ||
                                     cmd.entityGripMoveActive || cmd.mtextGripMoveActive;
      // REQ-121 rule (1), second seam (GitHub #91 review, D-2026-08-26-d, re-derived post-#103 as
      // D-2026-08-26-e). The snap OVERRIDE menu is a way of forcing a snap, so offering it during an
      // object-selection step offers the user the exact behaviour the rule removes. Picking a
      // candidate arms `objectSnapKindOverrideValid` as a persistent per-kind lock (issue #103); that
      // lock's consumption is already behind `snapViewportActive`'s own selection-step gate, so this
      // menu-level gate is what stops the lock from being armed off a selection-step pixel at all —
      // otherwise it would silently apply to the next NON-selection snap instead, a smaller but real
      // surprise (the override outliving the click that seemed to have no effect).
      const bool allowSnapCycle = cmd.active != AK::None && cmd.objectSnapEnabled &&
                                  !blockSnapPickMenu && !ViewportIsObjectSelectionStep(cmd);
      using DM = AppCommandState::RightClickDefaultMode;
      using EM = AppCommandState::RightClickEditMode;
      using CM = AppCommandState::RightClickCommandMode;
      const bool hasSel = !cmd.selection.empty() || !cmd.selectedSurveyPointIndices.empty();

      // The preference-driven classification, shared by the press path and the time-sensitive
      // release path so the two can never drift apart.
      auto openShortcutMenu = [&]() { ImGui::OpenPopup("##drawing1_vp_ctx"); };
      auto rightClickAsEnter = [&]() {
        if (cmd.active != AK::None)
          ProcessCommandLineSubmit(cmdBuf, cmdBufSize, cmd, log);
        else if (cmd.lastCommand != AK::None)
          RepeatLastCommand(cmd, log);
      };
      auto classifyByPreference = [&]() {
        if (cmd.active != AK::None) {
          switch (cmd.rightClickCommandMode) {
          case CM::Enter:
            ProcessCommandLineSubmit(cmdBuf, cmdBufSize, cmd, log);
            break;
          case CM::ShortcutMenuAlways:
          case CM::ShortcutMenuWhenOptions:
            openShortcutMenu();
            break;
          }
        } else if (hasSel) {
          switch (cmd.rightClickEditMode) {
          case EM::RepeatLastCommand:
            if (cmd.lastCommand != AK::None) RepeatLastCommand(cmd, log);
            else openShortcutMenu();
            break;
          case EM::ShortcutMenu:
            openShortcutMenu();
            break;
          }
        } else {
          switch (cmd.rightClickDefaultMode) {
          case DM::RepeatLastCommand:
            if (cmd.lastCommand != AK::None) RepeatLastCommand(cmd, log);
            else openShortcutMenu();
            break;
          case DM::ShortcutMenu:
            openShortcutMenu();
            break;
          }
        }
      };

      // REQ-084 (b): with time-sensitive right-click ON, the DURATION of the press decides the
      // Default and Command contexts — quick = ENTER, held = shortcut menu. A selection still goes
      // through Edit Mode, which is why the dialog leaves that group enabled. The menu therefore
      // opens on release-or-elapse rather than on press; that is the feature, not a defect.
      const bool timeSensitive = cmd.rightClickTimeSensitive && !blockSnapPickMenu && !hasSel;

      if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        if (ioVpRmb.KeyShift && allowSnapCycle) {
          ImGui::OpenPopup("##gos_snap_pick");
          cmd.rightClickPressPending = false;
        } else if (!blockSnapPickMenu) {
          if (timeSensitive) {
            cmd.rightClickPressPending = true;
            cmd.rightClickPressTimeSec = ImGui::GetTime();
          } else {
            classifyByPreference();
          }
        }
      } else if (cmd.rightClickPressPending) {
        const double heldMs = (ImGui::GetTime() - cmd.rightClickPressTimeSec) * 1000.0;
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
          cmd.rightClickPressPending = false;
          if (!CadRightClickHoldIsMenu(heldMs, cmd.rightClickLongerClickMs))
            rightClickAsEnter();
          else
            openShortcutMenu();  // released after the threshold without the frame that elapses it
        } else if (CadRightClickHoldIsMenu(heldMs, cmd.rightClickLongerClickMs)) {
          cmd.rightClickPressPending = false;
          openShortcutMenu();    // still held: the menu appears the moment the threshold passes
        }
      }
    }
    // A press that ends outside the viewport is abandoned, not resolved — otherwise the next
    // right-click over the drawing would be measured from a press the user made somewhere else.
    if (cmd.rightClickPressPending && !ImGui::IsMouseDown(ImGuiMouseButton_Right))
      cmd.rightClickPressPending = false;
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
  *out_snap = {};

  // Cursor / snap / hover. Runs in model AND paper space (it also drives the paper-space crosshair via
  // outCursorX/Y), but NOT in floating model space — there the floating input block above does this work
  // through the viewport transform, and running it here with the paper view coords would clobber the
  // floating hover/snap/cursor (REQ-036).
  if (!InFloatingModelSpace(cmd)) {
    cmd.viewportSnapPickValid = false;
  }
  // THE model-space input seam (REQ-058). Everything downstream — snap, hover, entity picking,
  // hatch tracing, command submission — consumes rawX/rawY, so orbit-awareness is this one
  // substitution rather than a sweep of every consumer.
  //
  // Plan view and paper space keep the original linear arithmetic **bit-identical** (paper is 2D
  // by definition — ADR-025 (g) — and plan view is REQ-058's parity guarantee). Only an orbited
  // model view takes the ray path.
  //
  // `cursorValid` is false when an orbited ray misses the work plane (an edge-on UCS). That is a
  // real state, not an error to paper over: there is no world point under the cursor, so the block
  // is skipped exactly as if the cursor were outside the viewport. Inventing a coordinate here
  // would drop geometry somewhere the user never pointed (REQ-201).
  bool cursorValid = !InFloatingModelSpace(cmd) && hovered && mx >= 0 && mx < avail.x && my >= 0 && my < avail.y;
  double rawX = 0.0, rawY = 0.0;
  double rawZ = 0.0;
  /// May the surface rollover readout (REQ-089) run this frame? Set from inside the idle-hover block
  /// below, so it is decided by the SAME `blockEntityHover` condition the hover highlight uses rather
  /// than by a second copy of it — the readout must never appear anywhere a hover highlight would not.
  /// It stays false when the cursor is not valid at all, which is what hides the readout the moment
  /// the pointer leaves the viewport.
  bool surfaceReadoutAllowed = false;
  if (cursorValid) {
    const bool orbited = modelSpace && !CadViewIsPlan(cmd);
    if (orbited) {
      const Camera curCam = CadViewCamera(cmd);
      const ray3d::Ray curRay = curCam.ScreenRay(mx, my, avail.x, avail.y);
      ray3d::Vec3 hit;
      if (ray3d::RayPlaneIntersect(curRay, CadActiveWorkPlane(cmd), &hit)) {
        rawX = hit.x;
        rawY = hit.y;
        rawZ = hit.z;
      } else {
        // The work plane is edge-on — exactly what a FRONT / BACK / LEFT / RIGHT view is against
        // the default world-XY plane. There is no intersection, but refusing a coordinate here
        // blanks the crosshair and makes those views unusable. Fall back to a plane through the
        // work-plane origin that FACES the camera, which always yields a point and is what the
        // user is visually pointing at in an elevation view.
        ray3d::Plane facing;
        facing.point = CadActiveWorkPlane(cmd).point;
        facing.normal = ray3d::Scale(curCam.ForwardWorld(), -1.0);
        if (ray3d::RayPlaneIntersect(curRay, facing, &hit)) {
          rawX = hit.x;
          rawY = hit.y;
          rawZ = hit.z;
        } else {
          cursorValid = false;  // genuinely degenerate (zero-size viewport); no point exists
        }
      }
    } else {
      const float u = mx / std::max(avail.x, 1.f);
      const float v = my / std::max(avail.y, 1.f);
      rawX = worldLeft + static_cast<double>(u) * (worldRight - worldLeft);
      rawY = worldTop - static_cast<double>(v) * (worldTop - worldBottom);
      // Plan view maps the screen straight to XY, so there is no ray to intersect — but the point
      // still lies on the work plane, and a TILTED plane's elevation varies across it (REQ-154).
      // Solve the plane for Z at this XY. For a plane parallel to world XY (every pre-UCS drawing)
      // the two offset terms vanish and this is exactly the origin's Z, as before.
      rawZ = ucs::WorkPlaneZAt(CadActiveWorkPlane(cmd), rawX, rawY);
    }
  }
  if (cursorValid) {
    cmd.uiCursorWorldZ = static_cast<float>(rawZ);
    // The cursor is on the work plane, so this IS the elevation a click here would commit at
    // (REQ-154). Published unconditionally so a value left over from a previous typed point can
    // never survive into a mouse-driven one.
    cmd.resolvedPointZValid = true;
    cmd.resolvedPointZ = static_cast<float>(rawZ);

    // The cursor's world ray, built once and handed to every pick in this block. Null in plan
    // view and paper space so those keep the exact pre-3D XY test (REQ-058 parity).
    const bool pickByRay = modelSpace && !CadViewIsPlan(cmd);
    const ray3d::Ray cursorRay =
        pickByRay ? CadViewCamera(cmd).ScreenRay(mx, my, avail.x, avail.y) : ray3d::Ray{};
    const ray3d::Ray* cursorRayPtr = pickByRay ? &cursorRay : nullptr;

    // What the cursor is worth to a prompted solid command (REQ-313 as amended). Resolved by the
    // COMMAND layer, not here: it is geometry, and sharing it is what lets a transcript drive the
    // same resolution the mouse does. This call site supplies the one thing the command layer cannot
    // reach — the pick RAY, which is the only way a height can be read off the screen.
    CadResolveSolidPick(cmd, ray3d::Vec3{rawX, rawY, rawZ}, cursorRayPtr);
    // EXTRUDE's height, resolved the same way and for the same reason (REQ-314).
    CadResolveExtrudePick(cmd, ray3d::Vec3{rawX, rawY, rawZ}, cursorRayPtr);

    if (outCursorRawX)
      *outCursorRawX = rawX;
    if (outCursorRawY)
      *outCursorRawY = rawY;

    using OP = AppCommandState::OffsetPhase;
    if (cmd.active == AppCommandState::Kind::Offset && cmd.offsetPhase == OP::WaitSelectEntity) {
      SelectedEntity hit{};
      float d2 = 0.f;
      const float offTol = CadOffsetEntityPickTolWorld(cmd);
      if (PickClosestCadEntity(cmd, rawX, rawY, offTol, &hit, &d2, cursorRayPtr)) {
        cmd.offsetHoverHighlightValid = true;
        cmd.offsetHoverEntity = hit;
      } else {
        cmd.offsetHoverHighlightValid = false;
      }
    } else
      cmd.offsetHoverHighlightValid = false;

    // HATCH live preview (REQ-043): each frame, trace the closed region under the cursor so the fill
    // previews while the cursor is inside one and clears when it is not.
    if (cmd.active == AppCommandState::Kind::Hatch) {
      if (hovered && CadHatchTraceAt(cmd, rawX, rawY, &cmd.hatchPreviewLoop) && cmd.hatchPreviewLoop.size() >= 6) {
        cmd.hatchPreviewValid = true;
      } else {
        cmd.hatchPreviewValid = false;
        cmd.hatchPreviewLoop.clear();
      }
    }

    using AK = AppCommandState::Kind;
    const bool blockSurveyHover = cmd.active != AK::None || cmd.dimGripMoveActive || cmd.entityGripMoveActive ||
                                  cmd.mtextGripMoveActive || cmd.mtextRichEditorOpen || cmd.tableCellEditorOpen ||
                                  cmd.selBoxWaitingSecond;
    if (!cmd.surveyPoints.empty() && !blockSurveyHover)
      cmd.viewportHoverSurveyPointIndex =
          PickSurveyPointAtCursor(cmd, rawX, rawY, surveyCrossHalfW, avail.x, avail.y, halfH, mx, my);

    // Idle hover: detect CAD entity under cursor for subtle highlight feedback.
    {
      // TRIM's cutting-edge and target picks are entity picks, so they get the same hover feedback as
      // idle selection — you can see what a click will take before you take it (REQ-056). Every other
      // command still suppresses hover, since their clicks mean coordinates rather than objects.
      using TPh = AppCommandState::TrimPhase;
      using EPh = AppCommandState::ExtendPhase;
      using BPh = AppCommandState::BreakPhase;
      using LPh = AppCommandState::LengthenPhase;
      const bool trimEntityPick = cmd.active == AK::Trim && (cmd.trimPhase == TPh::SelectCuttingEdges ||
                                                             cmd.trimPhase == TPh::SelectTrimTargets);
      const bool extendEntityPick = cmd.active == AK::Extend && (cmd.extendPhase == EPh::SelectBoundaries ||
                                                                  cmd.extendPhase == EPh::SelectTargets);
      // BREAK: only SelectFirstPoint is an entity SEARCH (the pick also selects the object);
      // SelectSecondPoint is a point pick on the entity already chosen, so hover stays suppressed
      // there the same as any other coordinate-entry command.
      const bool breakEntityPick = cmd.active == AK::Break && cmd.breakPhase == BPh::SelectFirstPoint;
      // LENGTHEN: WaitSelectOrMode is an entity search exactly like EXTEND's, so it earns the same
      // hover feedback (REQ-056). WaitDynamicTarget is a coordinate pick on an object already
      // chosen, so hover stays suppressed there — the same split BREAK's two phases make.
      const bool lengthenEntityPick = cmd.active == AK::Lengthen && cmd.lengthenPhase == LPh::WaitSelectOrMode;
      const bool blockEntityHover = (cmd.active != AK::None && !trimEntityPick && !extendEntityPick &&
                                     !breakEntityPick && !lengthenEntityPick) ||
                                    cmd.dimGripMoveActive ||
                                    cmd.entityGripMoveActive || cmd.mtextGripMoveActive || cmd.selBoxWaitingSecond;
      // REQ-089: the rollover readout rides on this exact condition. Model space only — a sheet has
      // no surfaces on it (ADR-025 (g)) — and, unlike the hover highlight, it is also suppressed
      // during a TRIM entity pick: TRIM wants to show what a click will take, and a four-row panel
      // over the cursor would cover the very geometry that pick is choosing between.
      surfaceReadoutAllowed = !blockEntityHover && modelSpace && cmd.active == AK::None;
      // Issue #166: the hover pick below is a full entity scan and runs every rendered frame the
      // cursor is over the viewport — both idle and through the TRIM/EXTEND/BREAK/LENGTHEN entity
      // phases (REQ-056). Its result only changes when the cursor, the view, or the geometry moves,
      // so re-run it only then; while the cursor sweeps, cap the rate to ~30 Hz (a highlight that
      // lags the cursor by a frame is invisible, a stuttering viewport is not). The 0.25 s idle
      // ceiling re-runs it anyway for any input not tracked here (a UCS change, a layer freeze).
      const HoverPickView hoverView{cmd.viewportPanX,       cmd.viewportPanY,
                                    cmd.viewportPanZ,        cmd.viewportZoom,
                                    cmd.viewportAzimuthDeg,  cmd.viewportElevationDeg,
                                    cmd.viewportRollDeg};
      const bool runHoverPick =
          !blockEntityHover &&
          HoverPickGateShouldRun(&cmd.viewportHoverPickGate, mx, my, ImGui::GetTime(), hoverView,
                                 cmd.cadGpuRevision, /*moveTolPx=*/1.f, /*minIntervalSec=*/1.0 / 30.0,
                                 /*maxIdleSec=*/0.25);
      cmd.perfHoverPickRan = runHoverPick;
      const auto perfHoverT0 = std::chrono::steady_clock::now();
      // Sub-object pre-highlight (REQ-318 item 14, D-2026-09-04-b): while Ctrl is held, show what a
      // click WOULD take before it is taken. Precedence is vertex-then-edge-then-face within
      // screen-derived tolerances, so on a corner a few pixels decide between three different
      // answers — without this the only way to find out is to click and read the log.
      //
      // Behind `runHoverPick`, the same gate the entity pick uses, which is the whole reason
      // TASK-199's DEBT-1 could be reversed: this is not a second per-frame walk beside the existing
      // hover, it is the same budget. It also SUPPRESSES the entity hover rather than drawing beside
      // it — two highlights answering one cursor is the defect, not the feature.
      // (The live gizmo drag, which covers a face drag too since slice 4c, is a few lines below.)
      const bool subObjectHovering = modelSpace && !blockEntityHover && ImGui::GetIO().KeyCtrl &&
                                     !cmd.gizmoDragActive;
      if (subObjectHovering) {
        cmd.viewportHoverEntityValid = false;
        if (runHoverPick) {
          const ray3d::Ray hoverRay = CadViewCamera(cmd).ScreenRay(mx, my, avail.x, avail.y);
          solidpick::Tolerance hoverTol;
          // The same budget the click uses — literally the same call — so the pre-highlight cannot
          // name one sub-object while the click takes another.
          hoverTol.vertex = static_cast<double>(CadOffsetEntityPickTolWorld(cmd));
          hoverTol.edge = hoverTol.vertex;
          SelectedSubObject hovered;
          cmd.subObjectHoverValid =
              PickSubObjectAcrossSolids(cmd, hoverRay, hoverTol, &hovered);
          if (cmd.subObjectHoverValid)
            cmd.subObjectHover = hovered;
        }
      } else {
        cmd.subObjectHoverValid = false;
      }
      // The translate gizmo's handle pre-highlight and its live drag (REQ-060 slice 4b; the FACE
      // handle and its ghost since slice 4c, issue #148 acceptance 4).
      //
      // Outside `runHoverPick`, unlike every pick above it, and for a reason: this is at most three
      // ray-to-segment tests against a widget whose position is already known, not a walk of the
      // drawing — and while a drag is armed the ghost has to follow the cursor every frame or the
      // gesture is not direct manipulation at all.
      //
      // The HOVER is suppressed while Ctrl is held so it cannot compete with the sub-object pick,
      // which is the same "two highlights answering one cursor is the defect" rule the block above
      // states. A live DRAG is not: pressing Ctrl mid-drag should not freeze the gesture.
      if (modelSpace && (cmd.gizmoDragActive || !ImGui::GetIO().KeyCtrl)) {
        const ray3d::Ray gizRay = CadViewCamera(cmd).ScreenRay(mx, my, avail.x, avail.y);
        if (cmd.gizmoDragActive) {
          UpdateGizmoDrag(cmd, gizRay);
          BumpCadGpuCache(cmd);
        } else {
          const int wasHot = cmd.gizmoHoverAxis;
          // The grab aperture in world units, from the same pixel-to-world conversion the handle
          // length uses, so the target is the stated number of pixels at every zoom.
          UpdateGizmoHover(cmd, gizRay,
                           static_cast<double>(CadSnap::WorldToleranceFromPixels(
                               avail.y, halfH, kGizmoHandleGrabPx)));
          if (cmd.gizmoHoverAxis != wasHot)
            BumpCadGpuCache(cmd);
        }
      } else if (cmd.gizmoHoverAxis >= 0 && !cmd.gizmoDragActive) {
        cmd.gizmoHoverAxis = -1;
        BumpCadGpuCache(cmd);
      }
      if (blockEntityHover) {
        cmd.viewportHoverEntityValid = false;
        cmd.viewportHoverPickGate.primed = false;
      } else if (subObjectHovering) {
        // Handled above; the entity hover stays off while Ctrl is held.
      } else if (runHoverPick) {
        // Text annotations are picked by bounding box and take priority over geometry, mirroring
        // click-to-select (the annotation pick runs before the entity pick on a click). Hovering text
        // pre-highlights it in model space, matching the paper-space hover (REQ-039). Dims keep their
        // existing no-hover behavior — only TEXT/MTEXT pre-highlight.
        int tblHover = modelSpace ? PickCadTableAt(static_cast<float>(rawX), static_cast<float>(rawY), cmd, halfH,
                                                   avail.y)
                                  : -1;
        int annHover = modelSpace ? PickCadAnnotationAt(static_cast<float>(rawX), static_cast<float>(rawY),
                                                        cmd, halfH, avail.y)
                                  : -1;
        if (annHover >= 0) {
          const CadAnnotation::Kind hk = cmd.cadAnnotations[static_cast<size_t>(annHover)].kind;
          if (hk != CadAnnotation::Kind::Text && hk != CadAnnotation::Kind::Mtext &&
              hk != CadAnnotation::Kind::Table)
            annHover = -1;
        }
        if (tblHover >= 0) {
          cmd.viewportHoverEntityValid = true;
          cmd.viewportHoverEntity.type = SelectedEntity::Type::Table;
          cmd.viewportHoverEntity.index = tblHover;
        } else if (annHover >= 0) {
          cmd.viewportHoverEntityValid = true;
          cmd.viewportHoverEntity.type = SelectedEntity::Type::Annotation;
          cmd.viewportHoverEntity.index = annHover;
        } else {
          SelectedEntity hoverHit{};
          float hoverD2 = 0.f;
          const float hoverTol = CadHoverEntityPickTolWorld(cmd);
          // The SAME ray the click below uses (REQ-058) — what highlights has to be what selects.
          // Without it the hover measured a plan-view XY distance from the work-plane cursor point
          // while the click measured the true distance from the ray, and off plan view those two
          // disagree: the XY distance over-measures along the foreshortened screen direction, so
          // geometry the click would take highlighted on one side of the cursor and not the other.
          if (PickClosestCadEntity(cmd, rawX, rawY, hoverTol, &hoverHit, &hoverD2, cursorRayPtr)) {
            cmd.viewportHoverEntityValid = true;
            cmd.viewportHoverEntity = hoverHit;
          } else {
            // Filled-region hover (REQ-042): lowest priority, only when no linework is under the cursor.
            const int frHover = PickFilledRegionAt(cmd, rawX, rawY);
            if (frHover >= 0) {
              cmd.viewportHoverEntityValid = true;
              cmd.viewportHoverEntity.type = SelectedEntity::Type::FilledRegion;
              cmd.viewportHoverEntity.index = frHover;
            } else {
              cmd.viewportHoverEntityValid = false;
            }
          }
        }
      }
      // else: the gate says skip this frame — keep last frame's cmd.viewportHoverEntity{,Valid},
      // which persist across frames, until the cursor / view / geometry actually change (issue #166).
      cmd.perfHoverPickMs =
          std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - perfHoverT0).count();
    }

    // GitHub #91 review, D-2026-08-26-d, re-derived post-#103 (D-2026-08-26-e). The override menu
    // (Shift+Right-Click "Snap once") is gated against ViewportIsObjectSelectionStep above, at
    // `allowSnapCycle`, so it cannot be opened during a selection step. Its consumption — reading
    // `objectSnapKindOverrideValid` as the `onlyKind` filter below — already lives inside
    // `snapViewportActive`'s own `!ViewportIsObjectSelectionStep(cmd)` gate (a few lines down), the
    // same gate the plain automatic snap uses. There is no separate one-shot consumption site left
    // to gate here post-#103: `pendingOneShotSnapValid`'s one-shot-value mechanism was replaced by a
    // persistent per-kind lock, consumed every frame at the one place FindBest is called, which is
    // already behind the selection-step check. A second gate here would be redundant, not defensive.
    const auto perfSnapT0 = std::chrono::steady_clock::now();
    {
      cmd.viewportSnapPickValid = false;
      const bool midCmd = cmd.active != AppCommandState::Kind::None || cmd.showCreatePointsWindow ||
                          cmd.dimGripMoveActive || cmd.entityGripMoveActive || cmd.mtextGripMoveActive;
      // REQ-121 rule (1). During an object-selection step OSNAP has no effect: no marker is drawn
      // and the cursor does not jump, because there is no coordinate being placed. The pick itself
      // was already hit-tested against the raw cursor (`RawEntityPick`'s own comment says why), so
      // before this the cursor visibly leapt to an endpoint while the pick correctly ignored it —
      // the two halves disagreeing on screen, which is worse than either being wrong alone.
      //
      // Suppressing it HERE, at the one place the snap is computed, is what makes the marker vanish
      // too: everything downstream reads `snap.valid`.
      const bool snapViewportActive =
          cmd.objectSnapEnabled && midCmd && !ViewportIsObjectSelectionStep(cmd);
      CadSnap::Hit snap{};
      if (snapViewportActive) {
        const float tol = CadSnap::WorldToleranceFromPixels(avail.y, halfH, cmd.objectSnapAperturePx);
        CadSnap::SnapExclude exclude{};
        if (cmd.entityGripMoveActive && cmd.entityGripEntityIndex >= 0) {
          exclude.valid = true;
          exclude.type  = cmd.entityGripType;
          exclude.index = cmd.entityGripEntityIndex;
        }
        // issue #103: the Shift+Right-Click "Snap once" override restricts FindBest to just the
        // chosen kind for this hover/pick, ignoring the persistent per-type toggles — that is the
        // whole point of an override menu.
        const CadSnap::Kind overrideKind = static_cast<CadSnap::Kind>(cmd.objectSnapKindOverrideKind);
        const CadSnap::Kind* onlyKind = cmd.objectSnapKindOverrideValid ? &overrideKind : nullptr;
        snap = CadSnap::FindBest(rawX, rawY, cmd, midCmd, tol, exclude, cursorRayPtr, onlyKind);  // 3D when orbited (REQ-058)
        if (snap.valid) {
          cmd.viewportSnapPickValid = true;
          cmd.viewportSnapPickLocalX = snap.x;
          cmd.viewportSnapPickLocalY = snap.y;
          cmd.viewportSnapPickLocalZ = snap.z;  // osnap overrides the work-plane elevation (REQ-058)
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
          // Carry the snapped point's elevation too, so the crosshair — which projects through the
          // camera — is drawn at the point it snapped to rather than on the work plane (REQ-058).
          // Eased by the same alpha as X/Y so the crosshair does not jump in Z ahead of the pull.
          cmd.uiCursorWorldZ = static_cast<float>(rawZ + alpha * (static_cast<double>(snap.z) - rawZ));
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
    cmd.perfSnapMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - perfSnapT0).count();
    if (!cmd.objectSnapKindOverrideValid && outCursorX && outCursorY &&
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

  // Surface rollover readout (REQ-089): advance the dwell, and on the one frame it elapses, ask what
  // is under the cursor — once.
  //
  // **The query is deliberately NOT run per frame.** `BuildSurfaceHoverRows` walks every triangle of
  // every visible surface (`TinElevationAt` is a linear scan), and REQ-100's surface profile is the
  // one profile near the frame budget and is CPU-bound — `PickClosestCadEntity` above already walks
  // the same triangles every frame for the hover highlight. Running this beside it would roughly
  // double that cost for a panel the user is not looking at yet. `HoverDwellTick::elapsed` is true on
  // exactly one frame per rest, which is what holds that line; REQ-089 states it as an acceptance
  // condition rather than a note for the same reason.
  {
    const double nowSec = ImGui::GetTime();
    // REQ-090: a survey point under the cursor takes precedence over a surface, and reuses this same
    // dwell — resting on a point is what `viewportHoverSurveyPointIndex` already reports every frame
    // (computed above, §2 of TASK-089), so `tick.settled` (level, not `elapsed`'s one-shot edge) is
    // enough to show it; there is no query here to defer.
    //
    // **Precedence is decided only at draw time, never at query time.** `viewportHoverSurveyPointIndex`
    // is re-picked every frame off the live cursor, using its own aperture — a different radius than
    // this dwell's `kSurfaceRolloverMoveTolPx` move tolerance. A cursor can sit just inside the point's
    // pick aperture on the exact frame `elapsed` fires, then drift a sub-tolerance jitter that leaves
    // the aperture without ever moving far enough to reset the dwell (`tick.moved` stays false). If
    // that drift had skipped `BuildSurfaceHoverRows` at the elapsed frame, nothing would be left to
    // fall back to for the remainder of the rest — `elapsed` cannot come again without a real move. So
    // the query always runs on `elapsed`, unconditionally; only which readout is drawn reads `onPoint`,
    // decided fresh every single frame from the current pick.
    // REQ-318 item 14 takes precedence over both of the readouts below while Ctrl is held: Ctrl is
    // the user saying they are asking about solids, and three panels competing for one cursor is
    // worse than any of them. Its own dwell, so releasing Ctrl and resting on a surface still costs
    // a fresh rest rather than firing off a timer that ran while this panel was showing.
    if (cmd.subObjectHoverValid) {
      const HoverDwellTick subTick = UpdateHoverDwell(&cmd.subObjectHoverDwell, mx, my, nowSec,
                                                      kSurfaceRolloverMoveTolPx, kSurfaceRolloverDwellSec);
      if (subTick.settled)
        DrawSubObjectRolloverReadout(cmd);
      // The surface timer is re-based rather than left running, for the reason the else-branch
      // below gives: coming back to it should cost a dwell, not fire instantly.
      cmd.surfaceHoverRows.clear();
      ResetHoverDwell(&cmd.surfaceHoverDwell, mx, my, nowSec);
    } else if (surfaceReadoutAllowed) {
      ResetHoverDwell(&cmd.subObjectHoverDwell, mx, my, nowSec);
      const HoverDwellTick tick = UpdateHoverDwell(&cmd.surfaceHoverDwell, mx, my, nowSec,
                                                   kSurfaceRolloverMoveTolPx, kSurfaceRolloverDwellSec);
      if (tick.moved)
        cmd.surfaceHoverRows.clear();  // the latched text is about a place the cursor has left
      else if (tick.elapsed)
        BuildSurfaceHoverRows(cmd, rawX, rawY, &cmd.surfaceHoverRows);
      const bool onPoint = cmd.viewportHoverSurveyPointIndex >= 0;
      if (onPoint && tick.settled)
        DrawSurveyPointRolloverReadout(cmd, cmd.viewportHoverSurveyPointIndex);
      else
        DrawSurfaceRolloverReadout(cmd);
    } else {
      // Suppressed — a command started, a gesture began, or the pointer left the viewport. Re-base
      // the timer rather than merely leaving it, so coming back costs a fresh dwell instead of
      // firing instantly on a timer that kept running while the readout was hidden.
      cmd.surfaceHoverRows.clear();
      ResetHoverDwell(&cmd.surfaceHoverDwell, mx, my, nowSec);
      ResetHoverDwell(&cmd.subObjectHoverDwell, mx, my, nowSec);
    }
  }

  // MTEXT box grips: first click arms; snapped cursor updates box live; second LMB commits (like dim / entity grips).
  if (cmd.mtextGripMoveActive && cmd.mtextGripAnnotationIndex >= 0 && outCursorX && outCursorY && hovered &&
      mx >= 0.f && mx < avail.x && my >= 0.f && my < avail.y) {
    const float curWx = cmd.viewportSnapPickValid ? cmd.viewportSnapPickLocalX : *outCursorX;
    const float curWy = cmd.viewportSnapPickValid ? cmd.viewportSnapPickLocalY : *outCursorY;
    const size_t gi = static_cast<size_t>(cmd.mtextGripAnnotationIndex);
    if (gi < cmd.cadAnnotations.size()) {
      CadAnnotation& ann = cmd.cadAnnotations[gi];
      if (ann.kind == CadAnnotation::Kind::Mtext || ann.kind == CadAnnotation::Kind::Table) {
        if (ann.surveyPointLabelForId >= 0 && cmd.mtextGripCorner == 4) {
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
    const float curWx = cmd.viewportSnapPickValid ? cmd.viewportSnapPickLocalX : *outCursorX;
    const float curWy = cmd.viewportSnapPickValid ? cmd.viewportSnapPickLocalY : *outCursorY;
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
    const float curWxRaw = cmd.viewportSnapPickValid
        ? cmd.viewportSnapPickLocalX
        : (outCursorRawX ? static_cast<float>(*outCursorRawX) : static_cast<float>(*outCursorX));
    const float curWyRaw = cmd.viewportSnapPickValid
        ? cmd.viewportSnapPickLocalY
        : (outCursorRawY ? static_cast<float>(*outCursorRawY) : static_cast<float>(*outCursorY));

    // ORTHO constrains the dragged point to the H/V line through the grip's start (REQ-047). An object snap
    // still beats ORTHO, matching the draw commands, so the constraint is skipped on a snapped cursor.
    float curWx = curWxRaw;
    float curWy = curWyRaw;
    if (!cmd.viewportSnapPickValid)
      ApplyOrthoConstrainFromAnchor(cmd, cmd.entityGripAnchorX, cmd.entityGripAnchorY, &curWx, &curWy, cmd.orthoMode);

    cmd.entityGripLiveDistance =
        std::hypot(curWx - cmd.entityGripAnchorX, curWy - cmd.entityGripAnchorY);
    ApplyEntityGripPoint(cmd, curWx, curWy);
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
      if (idx < 0 || static_cast<size_t>(idx) * 4 + 3 >= cmd.userCirclesCxCyZR.size())
        break;
      const size_t k = static_cast<size_t>(idx) * 4;
      cmd.userCirclesCxCyZR[k] = cmd.entityGripOrigCx;
      cmd.userCirclesCxCyZR[k + 1] = cmd.entityGripOrigCy;
      cmd.userCirclesCxCyZR[k + 3] = cmd.entityGripOrigR;
      break;
    }
    case SelectedEntity::Type::Polyline: {
      if (cmd.entityGripOrigPolyBulgeVi >= 0) {  // REQ-316 / ADR-047: arc-segment bulge grip
        if (static_cast<size_t>(cmd.entityGripOrigPolyBulgeVi) < cmd.userPolylineVertsBulge.size())
          cmd.userPolylineVertsBulge[static_cast<size_t>(cmd.entityGripOrigPolyBulgeVi)] = cmd.entityGripOrigPolyBulge;
      } else if (cmd.entityGripOrigPolylineXIdx >= 0 &&
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
    case SelectedEntity::Type::BlockRef: {
      if (idx >= 0 && static_cast<size_t>(idx) < cmd.cadBlockRefs.size())
        CadBlockRestoreDynGripOrig(cmd, &cmd.cadBlockRefs[static_cast<size_t>(idx)]);
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

  // A right-click abandons an armed gizmo drag, exactly as it abandons the two grip drags on either
  // side of this (REQ-060, issue #148 slice 4b). Nothing has moved yet, so there is nothing to undo.
  if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && mx >= 0 && mx < avail.x && my >= 0 &&
      my < avail.y && cmd.gizmoDragActive) {
    CancelGizmoDrag(cmd);
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
  if (modelSpace && hovered && !overCmdSugPopup && !overViewCube &&
      cmd.active != AppCommandState::Kind::Pan && cmd.active != AppCommandState::Kind::Orbit &&
      ImGui::IsMouseClicked(ImGuiMouseButton_Left) && mx >= 0 &&
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
        const int spi = SurveyPointIndexForId(cmd, gripAnn.surveyPointLabelForId);
        if (spi >= 0) {
          const SurveyPoint& sp = cmd.surveyPoints[static_cast<size_t>(spi)];
          // Store what RepositionSurveyLabelMtextForPoint reads back: the LEFT edge in X, the
          // vertical CENTRE in Y. Mixing the two up would make a dragged label jump on the next
          // rebuild, by exactly half its own box.
          gripAnn.surveyLabelUserOffsetEast  = gripAnn.boxMinX - sp.easting;
          gripAnn.surveyLabelUserOffsetNorth = 0.5f * (gripAnn.boxMinY + gripAnn.boxMaxY) - sp.northing;
          gripAnn.surveyLabelHasUserOffset   = true;
        }
      }
      ClearMtextGripInteraction(cmd);
      BumpCadGpuCache(cmd);
    } else {
    using K = AppCommandState::Kind;
    // ModifyPhase/RotatePhase aliases used to live here for the inline click whitelist; the phase
    // tests moved into ViewportClickRouteFor with it (TASK-099).

    const bool haveSnapPick = outCursorX && outCursorY && cmd.viewportSnapPickValid;
    const float commitX = haveSnapPick ? cmd.viewportSnapPickLocalX : *outCursorX;
    const float commitY = haveSnapPick ? cmd.viewportSnapPickLocalY : *outCursorY;

    // Mouse -> world for CLICK handling. This is a second, independent conversion from the hover
    // seam above and must branch the same way, or hover highlights an entity that the click then
    // fails to select (REQ-058). Plan view and paper space keep the exact previous arithmetic.
    const float uPick = mx / std::max(avail.x, 1.f);
    const float vPick = my / std::max(avail.y, 1.f);
    double rawPickX = worldLeft + static_cast<double>(uPick) * (worldRight - worldLeft);
    double rawPickY = worldTop - static_cast<double>(vPick) * (worldTop - worldBottom);
    const bool pickOrbited = modelSpace && !CadViewIsPlan(cmd);
    const Camera pickCam = CadViewCamera(cmd);
    const ray3d::Ray pickRay = pickOrbited ? pickCam.ScreenRay(mx, my, avail.x, avail.y) : ray3d::Ray{};
    const ray3d::Ray* pickRayPtr = pickOrbited ? &pickRay : nullptr;
    if (pickOrbited) {
      ray3d::Vec3 pickHit;
      // Same edge-on fallback as the hover seam above — the two must resolve the cursor
      // identically or clicking lands somewhere other than where the crosshair sat.
      ray3d::Plane pickPlane = CadActiveWorkPlane(cmd);
      if (!ray3d::RayPlaneIntersect(pickRay, pickPlane, &pickHit)) {
        pickPlane.normal = ray3d::Scale(pickCam.ForwardWorld(), -1.0);
        ray3d::RayPlaneIntersect(pickRay, pickPlane, &pickHit);
      }
      rawPickX = pickHit.x;
      rawPickY = pickHit.y;
    }
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
              : PickSurveyPointAtCursor(cmd, rawPickX, rawPickY, surveyCrossHalfW, avail.x, avail.y, halfH, mx, my);
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
    } else {
    // TASK-099. One authority decides what a click means here: ViewportClickRouteFor
    // (viewport/ViewportPickPolicy.hpp). This was an if/else-if whitelist on cmd.active, and a
    // command left out of it silently discarded every viewport click and appeared to hang on its
    // first prompt — it happened to RECT, then to FEATURELINE (TASK-082 BUG-1), then to all five
    // of REQ-103's MIRROR/LENGTHEN/EXTEND/BREAK/STRETCH at once. The policy function is an
    // exhaustive switch with no default, so the compiler objects when a Kind is added without a
    // routing decision, and ViewportPickPolicyTests plus the headless CLICK verb can both reach
    // it — neither of which was possible while the decision lived inline here.
    switch (ViewportClickRouteFor(cmd)) {
    case ViewportClickRoute::RawEntityPick:
      // Entity-pick commands (OFFSET, REQ-069's designators, REQ-103's LENGTHEN/EXTEND/BREAK):
      // the raw, unsnapped cursor position is what PickClosestCadEntity hit-tests against, not an
      // OSNAP-adjusted commit point.
      UiSubmitViewportPick(cmd, rawPickX, rawPickY, log);
      break;
    case ViewportClickRoute::PdfAttachInsertPoint:
      SubmitPdfAttachInsertPoint(cmd, commitX, commitY, log);
      break;
    case ViewportClickRoute::InsertBlockPick:
      SubmitInsertBlockPick(cmd, commitX, commitY, log);
      break;
    case ViewportClickRoute::HatchPick: {
      // HATCH (REQ-043): trace the region under the click and fill it; the command stays active on a miss
      // so the user can click another spot (REQ-201 — nothing placed when no closed boundary is found).
      std::vector<float> loop;
      if (CadHatchTraceAt(cmd, rawPickX, rawPickY, &loop) && CadHatchCommitLoop(cmd, loop, log)) {
        cmd.active = K::None;
        cmd.hatchPreviewValid = false;
        cmd.hatchPreviewLoop.clear();
      } else {
        log.push_back("HATCH — no closed boundary found there; click inside a closed area (Esc to cancel).");
      }
      break;
    }
    case ViewportClickRoute::SnappedPointPick:
      // Point-picking commands (draw commands, and every modify command past its selection
      // phase): the click is a coordinate, handed straight to the command state machine at the
      // OSNAP-adjusted commit point.
      UiSubmitViewportPick(cmd, commitX, commitY, log);
      break;
    case ViewportClickRoute::SelectionBox:
      // Fence: arm the first corner, or close the box on the second click.
      if (!cmd.selBoxWaitingSecond)
        BeginSelectionBoxCorner(cmd, wxPick, wyPick, mx, my);
      else
        UiSubmitViewportPick(cmd, wxPick, wyPick, log, keyShift, fenceWindowMode);
      break;
    case ViewportClickRoute::SelectionAccumulate: {
      // MOVE/COPY/SCALE/ROTATE/MIRROR/ALIGN/ARRAY's "select objects" step (this fix). A click on an
      // entity/annotation/fill/survey point toggles it into the selection additively (plain click
      // adds if absent, Shift removes if present) — the same accumulation ComputeSelectionFromRect
      // already does for a box, just one entity at a time, so mixing clicks and boxes in one
      // invocation merges into a single growing selection. A click on empty space arms or closes
      // the fence exactly like SelectionBox above; unlike SelectionBox, finishing a fence here does
      // NOT end the phase — SubmitViewportPickImpl's PickSelection branches no longer auto-advance,
      // and ProcessCommandLineSubmit's matching PickSelection branches are what advance on Enter.
      //
      // Deliberately narrower than IdleSelection: no grips, no double-click text edit, no
      // exclusive/clear-on-pick semantics for annotations or survey points — those are idle-only
      // behaviors this accumulating multi-type selection has no use for.
      if (cmd.selBoxWaitingSecond) {
        UiSubmitViewportPick(cmd, wxPick, wyPick, log, keyShift, fenceWindowMode);
        break;
      }
      bool handled = false;

      const int tblIx = PickCadTableAt(rawPickX, rawPickY, cmd, halfH, avail.y);
      if (tblIx >= 0) {
        SelectedEntity se{};
        se.type = SelectedEntity::Type::Table;
        se.index = tblIx;
        auto it = std::find_if(cmd.selection.begin(), cmd.selection.end(), [&](const SelectedEntity& x) {
          return x.type == SelectedEntity::Type::Table && x.index == tblIx;
        });
        if (keyShift) {
          if (it != cmd.selection.end())
            cmd.selection.erase(it);
        } else if (it == cmd.selection.end()) {
          cmd.selection.push_back(se);
        }
        EnsureAttrCounts(cmd);
        handled = true;
      }

      const int annIx = PickCadAnnotationAt(rawPickX, rawPickY, cmd, halfH, avail.y);
      if (annIx >= 0) {
        SelectedEntity se{};
        se.type = SelectedEntity::Type::Annotation;
        se.index = annIx;
        auto it = std::find_if(cmd.selection.begin(), cmd.selection.end(), [&](const SelectedEntity& x) {
          return x.type == SelectedEntity::Type::Annotation && x.index == annIx;
        });
        if (keyShift) {
          if (it != cmd.selection.end())
            cmd.selection.erase(it);
        } else if (it == cmd.selection.end()) {
          cmd.selection.push_back(se);
        }
        EnsureAttrCounts(cmd);
        handled = true;
      }

      if (!handled) {
        SelectedEntity clickHit{};
        float clickD2 = 0.f;
        const float clickTol = CadOffsetEntityPickTolWorld(cmd);
        if (PickClosestCadEntity(cmd, rawPickX, rawPickY, clickTol, &clickHit, &clickD2, pickRayPtr)) {
          auto it = std::find_if(cmd.selection.begin(), cmd.selection.end(), [&](const SelectedEntity& x) {
            return x.type == clickHit.type && x.index == clickHit.index;
          });
          if (keyShift) {
            if (it != cmd.selection.end())
              cmd.selection.erase(it);
          } else if (it == cmd.selection.end()) {
            cmd.selection.push_back(clickHit);
          }
          EnsureAttrCounts(cmd);
          handled = true;
        }
      }

      if (!handled) {
        const int frIx = PickFilledRegionAt(cmd, rawPickX, rawPickY);
        if (frIx >= 0) {
          SelectedEntity fe{};
          fe.type = SelectedEntity::Type::FilledRegion;
          fe.index = frIx;
          auto it = std::find_if(cmd.selection.begin(), cmd.selection.end(), [&](const SelectedEntity& x) {
            return x.type == SelectedEntity::Type::FilledRegion && x.index == frIx;
          });
          if (keyShift) {
            if (it != cmd.selection.end())
              cmd.selection.erase(it);
          } else if (it == cmd.selection.end()) {
            cmd.selection.push_back(fe);
          }
          EnsureAttrCounts(cmd);
          handled = true;
        }
      }

      if (!handled && !cmd.surveyPoints.empty()) {
        const int hitIx =
            PickSurveyPointAtCursor(cmd, rawPickX, rawPickY, surveyCrossHalfW, avail.x, avail.y, halfH, mx, my);
        if (hitIx >= 0) {
          // Additive, same as the box (ComputeSelectionFromRect's inclSurvey merge) — NOT idle's
          // ClearCadSelection-first exclusivity, which would drop this command's CAD picks.
          ApplySurveyPointClickSelection(cmd, hitIx, keyShift, &log);
          handled = true;
        }
      }

      if (!handled)
        BeginSelectionBoxCorner(cmd, wxPick, wyPick, mx, my);
      break;
    }
    case ViewportClickRoute::TrimPick: {
      const float trimTol = CadSnap::WorldToleranceFromPixels(avail.y, halfH, cmd.objectSnapAperturePx);
      using TP = AppCommandState::TrimPhase;
      const bool trimCutLinePt =
          cmd.trimPhase == TP::CuttingLine_WaitP1 || cmd.trimPhase == TP::CuttingLine_WaitP2;
      const float tx = trimCutLinePt ? commitX : rawPickX;
      const float ty = trimCutLinePt ? commitY : rawPickY;
      SubmitTrimViewportPick(cmd, tx, ty, trimTol, log);
      break;
    }
    case ViewportClickRoute::Ignore:
      // PAN/ORBIT (drag-driven), TRIMSTATE/ELEV (text prompts), VPFREEZE/VPTHAW (floating
      // viewports only), PaperRectViewport (paper space only), PDFATTACH outside its
      // insertion-point phase, INSERT while the dialog is open. Each is a decision, not an omission
      // — see ViewportClickRouteFor.
      break;
    case ViewportClickRoute::IdleSelection: {
      bool handled = false;
      // Sub-object selection (REQ-318 increment 2, D-2026-09-04-a). Ctrl+click names the FACE, EDGE
      // or VERTEX of a solid under the cursor instead of the whole object; a plain click keeps every
      // behaviour it had. No mode is entered, deliberately — a persistent sub-object mode is one a
      // user can be left in without noticing, after which every ordinary click means something they
      // did not intend.
      //
      // The two selections are mutually exclusive (REQ-318 item 9), which is what makes #148's
      // "does not interfere with whole-entity selection" structural: nothing that consumes
      // `cmd.selection` ever sees a sub-object, so no consumer needs to know this exists.

      // The translate gizmo gets first refusal on the click (REQ-060 slice 4b; a solid FACE too
      // since slice 4c, issue #148 acceptance 4).
      //
      // BEFORE the ordinary selection pick, and before the plain-click clear below, because a
      // handle sits over the thing it moves: a click that selected through it would make the widget
      // undraggable, and a click that cleared the sub-object selection would take away the very
      // face the handle belongs to. It only ever consumes a click that actually lands on a handle —
      // `SubmitGizmoClick` returns false otherwise — so a click anywhere else in the viewport means
      // exactly what it always meant.
      //
      // Not while Ctrl is held: that is the sub-object pick's gesture (D-2026-09-04-a), and the two
      // must not race for the same click. A live drag is exempt, so a drag begun before Ctrl went
      // down can still be committed.
      if (modelSpace && (cmd.gizmoDragActive || !ImGui::GetIO().KeyCtrl)) {
        const ray3d::Ray gizClickRay = pickCam.ScreenRay(mx, my, avail.x, avail.y);
        if (SubmitGizmoClick(cmd, gizClickRay,
                             static_cast<double>(CadSnap::WorldToleranceFromPixels(
                                 avail.y, halfH, kGizmoHandleGrabPx)),
                             log)) {
          BumpCadGpuCache(cmd);
          handled = true;  // the click was the gizmo's; nothing below may also act on it
        }
      }
      const bool subObjectClick = !handled && modelSpace && ImGui::GetIO().KeyCtrl;
      if (!handled && !subObjectClick && !cmd.subObjectSelection.empty()) {
        cmd.subObjectSelection.clear();
        BumpCadGpuCache(cmd);
      }
      if (subObjectClick) {
        AbortMtextGripInteraction(cmd);
        ClearDimGripInteraction(cmd);
        // Everything the click MEANS is in SubmitSubObjectPick, in the command layer, where a
        // transcript can drive it. What is decided here — and only here — is that Ctrl is what asks
        // for a sub-object, plus the two things the command layer has no access to: the cursor RAY
        // and the pixel-derived tolerance.
        //
        // The ray is built here rather than reusing `pickRayPtr`, which is null in plan view: a
        // solid has faces to pick looking straight down just as much as from an orbit, and 2D
        // Wireframe plan IS the default view.
        const ray3d::Ray subRay = pickCam.ScreenRay(mx, my, avail.x, avail.y);
        solidpick::Tolerance subTol;
        // The same screen-derived aperture the entity pick uses (REQ-318 item 5), so a vertex and a
        // line subtend the same target however far away they are.
        subTol.vertex = static_cast<double>(CadOffsetEntityPickTolWorld(cmd));
        subTol.edge = subTol.vertex;
        SubmitSubObjectPick(cmd, subRay, subTol, keyShift, log);
        BumpCadGpuCache(cmd);
        handled = true;
      }
      if (!handled && cmd.selBoxWaitingSecond) {
        UiSubmitViewportPick(cmd, wxPick, wyPick, log, keyShift, fenceWindowMode);
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
            gripCorner = HitTestMtextGrip(mouse.x, mouse.y, imgPos, avail, CadViewCamera(cmd), can, 10.f);
          } else if (can.kind == CadAnnotation::Kind::DimAligned || can.kind == CadAnnotation::Kind::DimLinear) {
            dimGripHit =
                HitTestDimGrip(mouse.x, mouse.y, imgPos, avail, CadViewCamera(cmd), can, 10.f);
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
        if (ann.surveyPointLabelForId >= 0 && gripCorner == 4) {
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
        // Grips must be hit-tested where they are DRAWN, so this projects through the camera
        // (REQ-058). In plan view it reduces to the previous arithmetic exactly; orbited, the old
        // mapping would place the hit targets somewhere other than the visible grip squares.
        const Camera gripCam = CadViewCamera(cmd);
        auto wtsRel = [&](double wx, double wy, double wz) -> ImVec2 {
          if (modelSpace) {
            float sx = 0.f, sy = 0.f;
            gripCam.WorldToScreen(wx, wy, wz, avail.x, avail.y, &sx, &sy);
            return ImVec2(sx, sy);  // relative to image top-left
          }
          const float u = static_cast<float>((wx - worldLeft) / denx);
          const float v = static_cast<float>((worldTop - wy) / deny);
          return ImVec2(u * avail.x, v * avail.y);
        };

        const float gripHitPx = 10.f;
        const float r2 = gripHitPx * gripHitPx;
        float bestD2 = r2;
        int bestWhich = -1;
        SelectedEntity bestSel{};

        float bestGripX = 0.f, bestGripY = 0.f;
        // \p gz is the grip's own elevation (REQ-058). A grip hit-tested at Z 0 while its entity
        // sits at elevation puts the click target somewhere other than the visible square as soon
        // as the view tilts — the same reason the projection itself was camera-routed.
        auto tryGrip = [&](const SelectedEntity& sel, float gx, float gy, float gz, int which) {
          ImVec2 p = wtsRel(gx, gy, static_cast<double>(gz));
          const float dx = mx - p.x, dy = my - p.y;
          const float d2 = dx * dx + dy * dy;
          if (d2 < bestD2) {
            bestD2 = d2; bestWhich = which; bestSel = sel;
            bestGripX = gx; bestGripY = gy;  // ORTHO / typed-distance anchor for the drag (REQ-047)
          }
        };

        for (const SelectedEntity& sel : cmd.selection) {
          switch (sel.type) {
          case SelectedEntity::Type::LineSeg: {
            const size_t k = static_cast<size_t>(sel.index) * 6;
            if (k + 5 < cmd.userLinesFlat.size()) {
              tryGrip(sel, cmd.userLinesFlat[k],     cmd.userLinesFlat[k + 1], cmd.userLinesFlat[k + 2], 0);
              tryGrip(sel, cmd.userLinesFlat[k + 3], cmd.userLinesFlat[k + 4], cmd.userLinesFlat[k + 5], 1);
            }
            break;
          }
          case SelectedEntity::Type::Circle: {
            const size_t k = static_cast<size_t>(sel.index) * 4;
            if (k + 3 < cmd.userCirclesCxCyZR.size()) {
              const float cx = cmd.userCirclesCxCyZR[k];
              const float cy = cmd.userCirclesCxCyZR[k + 1];
              const float cz = cmd.userCirclesCxCyZR[k + 2];
              const float r  = cmd.userCirclesCxCyZR[k + 3];
              tryGrip(sel, cx,     cy, cz, 0);
              tryGrip(sel, cx + r, cy, cz, 1);
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
                if (xIdx + 2 >= cmd.userPolylineVerts.size()) break;
                tryGrip(sel, cmd.userPolylineVerts[xIdx], cmd.userPolylineVerts[xIdx + 1],
                        cmd.userPolylineVerts[xIdx + 2], vi);
              }
              CadForEachPolylineArcMidGrip(cmd, sel.index, [&](int seg, float mx, float my, float mz) {
                tryGrip(sel, mx, my, mz, kPolyBulgeGripBase + seg);  // REQ-316 / ADR-047
              });
            }
            break;
          }
          case SelectedEntity::Type::Arc: {
            if (sel.index >= 0 && static_cast<size_t>(sel.index) < cmd.userArcs.size()) {
              const CadArc& a = cmd.userArcs[static_cast<size_t>(sel.index)];
              const float endRad = a.startRad + a.sweepRad;
              tryGrip(sel, a.cx, a.cy, a.z, 0);
              tryGrip(sel, a.cx + a.r * std::cos(a.startRad), a.cy + a.r * std::sin(a.startRad), a.z, 1);
              tryGrip(sel, a.cx + a.r * std::cos(endRad),     a.cy + a.r * std::sin(endRad),     a.z, 2);
            }
            break;
          }
          case SelectedEntity::Type::Ellipse: {
            if (sel.index >= 0 && static_cast<size_t>(sel.index) < cmd.userEllipses.size()) {
              const CadEllipse& el = cmd.userEllipses[static_cast<size_t>(sel.index)];
              const float perpX = -el.majVy, perpY = el.majVx;
              tryGrip(sel, el.cx,                    el.cy,                    el.z, 0);
              tryGrip(sel, el.cx + el.majVx,         el.cy + el.majVy,         el.z, 1);
              tryGrip(sel, el.cx + perpX * el.ratio, el.cy + perpY * el.ratio, el.z, 2);
            }
            break;
          }
          case SelectedEntity::Type::BlockRef: {
            if (sel.index >= 0 && static_cast<size_t>(sel.index) < cmd.cadBlockRefs.size()) {
              const CadBlockRef& r = cmd.cadBlockRefs[static_cast<size_t>(sel.index)];
              const int di = CadBlockFindDef(cmd.blockDefs, r.defName);
              if (di >= 0) {
                const CadBlockDefinition& def = cmd.blockDefs[static_cast<size_t>(di)];
                const int nG = CadBlockDynGripCount(def);
                for (int g = 0; g < nG; ++g) {
                  if (!CadBlockDynGripShownOnInsert(g))
                    continue;
                  float gx = 0.f, gy = 0.f, gz = 0.f;
                  if (CadBlockDynGripWorld(def, r, g, &gx, &gy, &gz))
                    tryGrip(sel, gx, gy, gz, g);
                }
              }
            }
            break;
          }
          default:
            break;
          }
        }

        if (bestWhich >= 0) {
          PushUndoSnapshot(cmd, "Grip edit");
          if (bestSel.type == SelectedEntity::Type::BlockRef) {
            if (!CadBlockArmDynGrip(cmd, bestSel.index, bestWhich)) {
              BumpCadGpuCache(cmd);
              AbortMtextGripInteraction(cmd);
              ClearDimGripInteraction(cmd);
              handled = true;
            } else {
              cmd.entityGripMoveActive = true;
              cmd.entityGripType = bestSel.type;
              cmd.entityGripEntityIndex = bestSel.index;
              cmd.entityGripWhich = bestWhich;
              cmd.entityGripAnchorX = bestGripX;
              cmd.entityGripAnchorY = bestGripY;
              cmd.entityGripTypedDistanceValid = false;
              AbortMtextGripInteraction(cmd);
              ClearDimGripInteraction(cmd);
              handled = true;
            }
          } else {
          cmd.entityGripMoveActive = true;
          cmd.entityGripType = bestSel.type;
          cmd.entityGripEntityIndex = bestSel.index;
          cmd.entityGripWhich = bestWhich;
          cmd.entityGripAnchorX = bestGripX;
          cmd.entityGripAnchorY = bestGripY;
          cmd.entityGripTypedDistanceValid = false;

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
            const size_t k = static_cast<size_t>(bestSel.index) * 4;
            cmd.entityGripOrigCx = cmd.userCirclesCxCyZR[k];
            cmd.entityGripOrigCy = cmd.userCirclesCxCyZR[k + 1];
            cmd.entityGripOrigR  = cmd.userCirclesCxCyZR[k + 3];
            break;
          }
          case SelectedEntity::Type::Polyline: {
            const int startV  = cmd.userPolylineOffsets[static_cast<size_t>(bestSel.index)];
            cmd.entityGripOrigPolyBulgeVi = -1;
            if (bestWhich >= kPolyBulgeGripBase) {  // REQ-316 / ADR-047: arc-segment bulge grip
              const int va = startV + (bestWhich - kPolyBulgeGripBase);
              cmd.entityGripOrigPolyBulgeVi = va;
              cmd.entityGripOrigPolyBulge = static_cast<size_t>(va) < cmd.userPolylineVertsBulge.size()
                                                ? cmd.userPolylineVertsBulge[static_cast<size_t>(va)] : 0.f;
              cmd.entityGripOrigPolylineXIdx = -1;
              break;
            }
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
      }

      if (!handled && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        const int tIx = PickCadTableAt(rawPickX, rawPickY, cmd, halfH, avail.y);
        if (tIx >= 0 && static_cast<size_t>(tIx) < cmd.cadTables.size()) {
          const int cell = CadTableHitCell(cmd.cadTables[static_cast<size_t>(tIx)], rawPickX, rawPickY);
          if (cell >= 0) {
            AbortMtextGripInteraction(cmd);
            ClearDimGripInteraction(cmd);
            ClearCadSelection(cmd);
            SelectedEntity se{};
            se.type = SelectedEntity::Type::Table;
            se.index = tIx;
            cmd.selection.push_back(se);
            EnsureAttrCounts(cmd);
            cmd.selectedSurveyPointIndices.clear();
            OpenTableCellEditor(cmd, tIx, cell);
            cmd.selBoxWaitingSecond = false;
            handled = true;
          }
        }
      }

      if (!handled && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        const int dIx = PickCadAnnotationAt(rawPickX, rawPickY, cmd, halfH, avail.y);
        const CadAnnotation::Kind dKind =
            (dIx >= 0 && static_cast<size_t>(dIx) < cmd.cadAnnotations.size())
                ? cmd.cadAnnotations[static_cast<size_t>(dIx)].kind
                : CadAnnotation::Kind::DimAligned;  // any non-text sentinel
        // REQ-039 phase 2: double-click any text (MTEXT or single-line TEXT) opens the in-place editor.
        if (dIx >= 0 &&
            (dKind == CadAnnotation::Kind::Mtext || dKind == CadAnnotation::Kind::Text)) {
          AbortMtextGripInteraction(cmd);
          ClearDimGripInteraction(cmd);
          ClearCadSelection(cmd);
          SelectedEntity se;
          se.type = SelectedEntity::Type::Annotation;
          se.index = dIx;
          cmd.selection.push_back(se);
          EnsureAttrCounts(cmd);
          const CadAnnotation& da = cmd.cadAnnotations[static_cast<size_t>(dIx)];
          if (da.kind == CadAnnotation::Kind::Mtext && da.surveyPointLabelForId >= 0) {
            ApplyLinkedSurveyForAnnotationPick(cmd, dIx, false);
            SyncSurveyPointLinkedMtextSelection(cmd, SurveyPointIndexForId(cmd, da.surveyPointLabelForId));
          } else
            cmd.selectedSurveyPointIndices.clear();
          OpenMtextRichEditorForAnnotation(cmd, dIx, &log);
          cmd.selBoxWaitingSecond = false;
          handled = true;
        }
      }

      if (!handled) {
        const int tblClick = PickCadTableAt(rawPickX, rawPickY, cmd, halfH, avail.y);
        if (tblClick >= 0) {
          AbortMtextGripInteraction(cmd);
          ClearDimGripInteraction(cmd);
          CancelTableCellEditor(cmd);
          SelectedEntity se{};
          se.type = SelectedEntity::Type::Table;
          se.index = tblClick;
          if (keyShift) {
            auto it = std::find_if(cmd.selection.begin(), cmd.selection.end(), [&](const SelectedEntity& x) {
              return x.type == SelectedEntity::Type::Table && x.index == tblClick;
            });
            if (it != cmd.selection.end())
              cmd.selection.erase(it);
            else
              cmd.selection.push_back(se);
          } else {
            ClearCadSelection(cmd);
            cmd.selectedSurveyPointIndices.clear();
            cmd.selection.push_back(se);
          }
          EnsureAttrCounts(cmd);
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
              pickedAnn.kind == CadAnnotation::Kind::Mtext && pickedAnn.surveyPointLabelForId >= 0;
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
              SyncSurveyPointLinkedMtextSelection(cmd,
                                                  SurveyPointIndexForId(cmd, pickedAnn.surveyPointLabelForId));
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
        // Same ray the hover used, so what highlights is what selects (REQ-058).
        if (PickClosestCadEntity(cmd, rawPickX, rawPickY, clickTol, &clickHit, &clickD2, pickRayPtr)) {
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

      // Filled-region (hatch) pick — lowest priority, only after annotation + geometry picks miss, so a fill
      // never steals a click from linework on top of it (REQ-042). Clicking anywhere inside the fill selects it.
      if (!handled) {
        const int frIx = PickFilledRegionAt(cmd, rawPickX, rawPickY);
        if (frIx >= 0) {
          AbortMtextGripInteraction(cmd);
          ClearDimGripInteraction(cmd);
          SelectedEntity fe{};
          fe.type = SelectedEntity::Type::FilledRegion;
          fe.index = frIx;
          auto it = std::find_if(cmd.selection.begin(), cmd.selection.end(), [&](const SelectedEntity& x) {
            return x.type == SelectedEntity::Type::FilledRegion && x.index == frIx;
          });
          if (keyShift) {
            if (it != cmd.selection.end())
              cmd.selection.erase(it);
          } else if (it == cmd.selection.end()) {
            cmd.selection.push_back(fe);
          }
          EnsureAttrCounts(cmd);
          cmd.selBoxWaitingSecond = false;
          handled = true;
        }
      }

      if (!handled) {
        if (!cmd.surveyPoints.empty()) {
          const int hitIx = PickSurveyPointAtCursor(cmd, rawPickX, rawPickY, surveyCrossHalfW, avail.x, avail.y, halfH, mx, my);
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
            UiSubmitViewportPick(cmd, wxPick, wyPick, log, keyShift, fenceWindowMode);
        } else {
          if (!cmd.selBoxWaitingSecond)
            BeginSelectionBoxCorner(cmd, wxPick, wyPick, mx, my);
          else
            UiSubmitViewportPick(cmd, wxPick, wyPick, log, keyShift, fenceWindowMode);
        }
      }
      break;
    }
    }  // switch (ViewportClickRouteFor(cmd))
    }  // else of "create-points window owns the click"
    }
  }

  // --- PDF overlays: insertion bounding-box preview + selection border ---
  {
    // PDF underlays are model-space attachments, so their overlay projects through the camera
    // (REQ-058). They are flat sheets with no elevation of their own, hence Z = 0.
    const Camera pdfCam = CadViewCamera(cmd);
    auto wts = [&](float wx, float wy) -> ImVec2 {
      float sx = 0.f, sy = 0.f;
      pdfCam.WorldToScreen(static_cast<double>(wx), static_cast<double>(wy), 0.0, avail.x, avail.y, &sx, &sy);
      return {imgPos.x + sx, imgPos.y + sy};
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
    // Clipped to the drawing Image rect so the white paper does not bleed into surrounding UI (issue #101).
    const ImVec2 __canvasMin = imgPos;
    const ImVec2 __canvasMax = ImVec2(imgPos.x + avail.x, imgPos.y + avail.y);
    sdl->PushClipRect(__canvasMin, __canvasMax, true);
    sdl->AddRectFilled(ImVec2(a.x + 5.f, a.y + 5.f), ImVec2(b.x + 5.f, b.y + 5.f), IM_COL32(0, 0, 0, 90));  // shadow
    sdl->AddRectFilled(a, b, IM_COL32(244, 244, 244, 255));                                                 // sheet
    sdl->AddRect(a, b, IM_COL32(40, 40, 40, 255), 0.f, 0, 1.5f);                                            // border
    sdl->PopClipRect();

    // Viewports (REQ-027): each shows model space clipped + scaled inside its rect. Drawn via the
    // overlay this increment; the GL-batch pass is tracked tech debt (TASK-002 §7).
    const double oX = cmd.worldDocumentOriginX;
    const double oY = cmd.worldDocumentOriginY;
    for (int vi = 0; vi < static_cast<int>(L.viewports.size()); ++vi) {
      const Viewport& vp = L.viewports[static_cast<size_t>(vi)];
      const ImVec2 r0 = w2s(vp.paperXIn, vp.paperYIn);
      const ImVec2 r1 = w2s(vp.paperXIn + vp.paperWIn, vp.paperYIn + vp.paperHIn);
      const ImVec2 rmin(std::min(r0.x, r1.x), std::min(r0.y, r1.y));
      const ImVec2 rmax(std::max(r0.x, r1.x), std::max(r0.y, r1.y));
      // World (model, in world coords) → screen, through this viewport's camera (REQ-061). In plan
      // view ModelToPaperInThroughCamera is exactly the pre-change ModelToPaperIn; a rotated camera
      // gives the axonometric/perspective projection onto the sheet. m2s keeps the 2-arg call sites
      // working (Z assumed 0 — text, tables, grips); m2sz carries a real elevation for linework.
      auto m2sz = [&](double wx, double wy, double wz) {
        float px = 0.f, py = 0.f;
        ModelToPaperInThroughCamera(vp, wx, wy, wz, &px, &py);
        return w2s(px, py);
      };
      auto m2s = [&](double wx, double wy) { return m2sz(wx, wy, 0.0); };
      const float pxPerWorld = avail.x / static_cast<float>(denx);  // screen px per paper inch (uniform)
      const float pxPerModel = pxPerWorld / vp.safeScale();
      sdl->PushClipRect(rmin, rmax, true);
      // Selection + hover highlight inside the floating viewport (REQ-036): GL draws these in model space,
      // but GL is skipped in paper space, so style the overlay strokes here for the active floating viewport.
      const bool isFloatVp = InFloatingModelSpace(cmd) && cmd.floatingViewportLayout == cmd.activeSpaceIndex &&
                             vi == cmd.floatingViewportIndex;
      // Base color for model geometry inside this viewport: a per-viewport VP Color override (REQ-046)
      // wins; otherwise the entity's TRUE color — its own color, or its layer's color when ByLayer
      // (REQ-048), resolved like model space. \p entityColor is "ByLayer" for entities that have none.
      auto vpBaseCol = [&](const std::string& layer, const std::string& entityColor) -> ImU32 {
        float rgba[4] = {0.1f, 0.1f, 0.12f, 1.f};
        if (const std::string* ov = ViewportLayerColorOverride(vp, layer)) {
          ResolveStoredColorForViewport(*ov, 0.f, 0.1f, 0.1f, 0.12f, rgba);  // REQ-046 override wins
        } else {
          std::string col = entityColor;  // REQ-048: entity color, else layer color
          if (col.empty() || col == "ByLayer") {
            const CadLayerRow* lr = FindDrawingLayerRowCi(cmd, layer);
            col = (lr && !lr->color.empty() && lr->color != "ByLayer") ? lr->color : "White";
          }
          ResolveStoredColorForViewport(col, 0.f, 0.1f, 0.1f, 0.12f, rgba);
        }
        AdaptWhiteBlackToBackground(&rgba[0], &rgba[1], &rgba[2], true);  // REQ-048: legible on the white sheet
        return IM_COL32(static_cast<int>(rgba[0] * 255.f), static_cast<int>(rgba[1] * 255.f),
                        static_cast<int>(rgba[2] * 255.f), 255);
      };
      auto entStyle = [&](SelectedEntity::Type t, int idx, ImU32 baseCol, ImU32& col, float& wid) {
        col = baseCol;
        wid = 1.0f;
        if (!isFloatVp)
          return;
        for (const SelectedEntity& se : cmd.selection)
          if (se.type == t && se.index == idx) {
            col = IM_COL32(59, 130, 246, 255);  // selection accent (blue)
            wid = 2.0f;
            return;
          }
        if (cmd.viewportHoverEntityValid && cmd.viewportHoverEntity.type == t &&
            cmd.viewportHoverEntity.index == idx) {
          col = IM_COL32(130, 180, 240, 255);  // hover (lighter blue)
          wid = 1.6f;
        }
      };
      // Lines (REQ-028: skip frozen layers).
      for (size_t i = 0; i + 5 < cmd.userLinesFlat.size(); i += 6) {
        const size_t lineIdx = i / 6;
        const EntityAttributes& attr = LineAttr(cmd, static_cast<int>(lineIdx));
        if (IsLayerFrozenInViewport(vp, attr.layer))
          continue;
        const ImVec2 s0 = m2sz(cmd.userLinesFlat[i] + oX, cmd.userLinesFlat[i + 1] + oY, cmd.userLinesFlat[i + 2]);
        const ImVec2 s1 =
            m2sz(cmd.userLinesFlat[i + 3] + oX, cmd.userLinesFlat[i + 4] + oY, cmd.userLinesFlat[i + 5]);
        ImU32 lc;
        float lw;
        entStyle(SelectedEntity::Type::LineSeg, static_cast<int>(lineIdx), vpBaseCol(attr.layer, attr.color), lc, lw);
        sdl->AddLine(s0, s1, lc, lw);
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
        ImU32 pc;
        float pw;
        entStyle(SelectedEntity::Type::Polyline, static_cast<int>(pi), vpBaseCol(attr.layer, attr.color), pc, pw);
        ImVec2 prev{};
        bool have = false;
        for (int k = start; k < end; ++k) {
          const ImVec2 s = m2sz(cmd.userPolylineVerts[static_cast<size_t>(k) * 3] + oX,
                                cmd.userPolylineVerts[static_cast<size_t>(k) * 3 + 1] + oY,
                                cmd.userPolylineVerts[static_cast<size_t>(k) * 3 + 2]);
          if (have)
            sdl->AddLine(prev, s, pc, pw);
          prev = s;
          have = true;
        }
      }
      // Circles (REQ-028: skip frozen layers).
      for (size_t i = 0; i + 3 < cmd.userCirclesCxCyZR.size(); i += 4) {
        const size_t circleIdx = i / 4;
        const EntityAttributes& attr = CircleAttr(cmd, static_cast<int>(circleIdx));
        if (IsLayerFrozenInViewport(vp, attr.layer))
          continue;
        const double ccx = cmd.userCirclesCxCyZR[i] + oX, ccy = cmd.userCirclesCxCyZR[i + 1] + oY;
        const double ccz = cmd.userCirclesCxCyZR[i + 2];
        const float cr = cmd.userCirclesCxCyZR[i + 3];
        ImU32 cc2;
        float cw;
        entStyle(SelectedEntity::Type::Circle, static_cast<int>(circleIdx), vpBaseCol(attr.layer, attr.color), cc2, cw);
        if (vp.cameraIsPlan()) {
          const ImVec2 c = m2s(ccx, ccy);
          const float rPx = cr * pxPerModel;
          if (rPx >= 0.5f)
            sdl->AddCircle(c, rPx, cc2, 0, cw);
        } else {  // REQ-061: a rotated camera turns the circle into an ellipse on the sheet — sample it.
          constexpr double kTwoPi = 6.283185307179586;
          ImVec2 prev{};
          for (int k = 0; k <= 48; ++k) {
            const double t = kTwoPi * static_cast<double>(k) / 48.0;
            const ImVec2 s = m2sz(ccx + cr * std::cos(t), ccy + cr * std::sin(t), ccz);
            if (k > 0)
              sdl->AddLine(prev, s, cc2, cw);
            prev = s;
          }
        }
      }
      // Arcs (REQ-028: skip frozen layers, sampled).
      for (size_t arcIdx = 0; arcIdx < cmd.userArcs.size(); ++arcIdx) {
        const CadArc& arc = cmd.userArcs[arcIdx];
        const EntityAttributes& attr = ArcAttr(cmd, static_cast<int>(arcIdx));
        if (IsLayerFrozenInViewport(vp, attr.layer))
          continue;
        const int segs = std::clamp(static_cast<int>(std::fabs(arc.sweepRad) / 0.15f) + 2, 2, 180);
        ImU32 ac;
        float aw;
        entStyle(SelectedEntity::Type::Arc, static_cast<int>(arcIdx), vpBaseCol(attr.layer, attr.color), ac, aw);
        ImVec2 prev{};
        for (int k = 0; k <= segs; ++k) {
          const float t = arc.startRad + arc.sweepRad * (static_cast<float>(k) / static_cast<float>(segs));
          const ImVec2 s = m2sz(static_cast<double>(arc.cx + arc.r * std::cos(t)) + oX,
                                static_cast<double>(arc.cy + arc.r * std::sin(t)) + oY, arc.z);
          if (k > 0)
            sdl->AddLine(prev, s, ac, aw);
          prev = s;
        }
      }
      // Ellipses (REQ-028: skip frozen layers).
      for (size_t ei = 0; ei < cmd.userEllipses.size(); ++ei) {
        const CadEllipse& el = cmd.userEllipses[ei];
        const EntityAttributes& attr = EllipseAttr(cmd, static_cast<int>(ei));
        if (IsLayerFrozenInViewport(vp, attr.layer))
          continue;
        const float ma = std::hypot(el.majVx, el.majVy);
        if (ma < 1e-8f)
          continue;
        const int segs = 64;
        ImU32 ec; float ew;
        entStyle(SelectedEntity::Type::Ellipse, static_cast<int>(ei), vpBaseCol(attr.layer, attr.color), ec, ew);
        const float px = -el.majVy * el.ratio;
        const float py = el.majVx * el.ratio;
        constexpr double kTwoPi = 6.283185307179586;
        ImVec2 prev{};
        for (int k = 0; k <= segs; ++k) {
          const double u = kTwoPi * static_cast<double>(k) / static_cast<double>(segs);
          const double c0 = std::cos(u);
          const double s0 = std::sin(u);
          const double wx = static_cast<double>(el.cx) + static_cast<double>(el.majVx) * c0 + static_cast<double>(px) * s0;
          const double wy = static_cast<double>(el.cy) + static_cast<double>(el.majVy) * c0 + static_cast<double>(py) * s0;
          const ImVec2 s = m2sz(wx + oX, wy + oY, el.z);
          if (k > 0) sdl->AddLine(prev, s, ec, ew);
          prev = s;
        }
      }
      // Survey-point crosses (REQ-028: skip frozen layers).
      const float crossPx = 4.f;
      for (const SurveyPoint& sp : cmd.surveyPoints) {
        if (IsLayerFrozenInViewport(vp, sp.layer))
          continue;
        const ImU32 spCol = vpBaseCol(sp.layer, "ByLayer");  // REQ-046/048: VP override, else layer color
        const ImVec2 c = m2sz(static_cast<double>(sp.easting) + oX, static_cast<double>(sp.northing) + oY,
                              static_cast<double>(sp.elevation));
        sdl->AddLine(ImVec2(c.x - crossPx, c.y), ImVec2(c.x + crossPx, c.y), spCol, 1.0f);
        sdl->AddLine(ImVec2(c.x, c.y - crossPx), ImVec2(c.x, c.y + crossPx), spCol, 1.0f);
      }
      // Surfaces through this viewport (REQ-135): same display batches as model GL, clipped here.
      {
        auto emitSurfLines = [&](const std::vector<float>& verts, ImU32 col, float wid) {
          for (size_t i = 0; i + 5 < verts.size(); i += 6) {
            const ImVec2 s0 = m2sz(static_cast<double>(verts[i]) + oX, static_cast<double>(verts[i + 1]) + oY,
                                   static_cast<double>(verts[i + 2]));
            const ImVec2 s1 = m2sz(static_cast<double>(verts[i + 3]) + oX, static_cast<double>(verts[i + 4]) + oY,
                                   static_cast<double>(verts[i + 5]));
            sdl->AddLine(s0, s1, col, wid);
          }
        };
        auto emitSurfTris = [&](const std::vector<float>& verts, ImU32 col) {
          for (size_t i = 0; i + 8 < verts.size(); i += 9) {
            const ImVec2 a = m2sz(static_cast<double>(verts[i]) + oX, static_cast<double>(verts[i + 1]) + oY,
                                  static_cast<double>(verts[i + 2]));
            const ImVec2 b = m2sz(static_cast<double>(verts[i + 3]) + oX, static_cast<double>(verts[i + 4]) + oY,
                                  static_cast<double>(verts[i + 5]));
            const ImVec2 c = m2sz(static_cast<double>(verts[i + 6]) + oX, static_cast<double>(verts[i + 7]) + oY,
                                  static_cast<double>(verts[i + 8]));
            sdl->AddTriangleFilled(a, b, c, col);
          }
        };
        for (size_t si = 0; si < cmd.cadSurfaces.size(); ++si) {
          if (!SurfaceVisible(cmd, si))
            continue;
          if (si >= cmd.cadSurfaceAttrs.size())
            continue;
          const EntityAttributes& attr = cmd.cadSurfaceAttrs[si];
          if (IsLayerFrozenInViewport(vp, attr.layer))
            continue;
          const std::uint64_t id = attr.id;
          auto it = std::find_if(cmd.surfaceDisplayCache.begin(), cmd.surfaceDisplayCache.end(),
                                 [&](const AppCommandState::SurfaceDisplayCacheEntry& e) { return e.surfaceId == id; });
          if (it == cmd.surfaceDisplayCache.end())
            continue;
          for (size_t bi = 0; bi < it->bandTriangleBuffers.size(); ++bi) {
            const std::vector<float>& buf = it->bandTriangleBuffers[bi];
            if (buf.empty())
              continue;
            const std::string& colorStr =
                bi < it->style.bands.size() ? it->style.bands[bi].color : it->style.triangles.color;
            float rgba[4] = {0.1f, 0.1f, 0.12f, 1.f};
            ResolveSurfaceBandLegendRgba(cmd, attr, colorStr, rgba);
            AdaptWhiteBlackToBackground(&rgba[0], &rgba[1], &rgba[2], true);
            const ImU32 fc = IM_COL32(static_cast<int>(rgba[0] * 255.f), static_cast<int>(rgba[1] * 255.f),
                                      static_cast<int>(rgba[2] * 255.f), 180);
            emitSurfTris(buf, fc);
          }
          const ImU32 sc = vpBaseCol(attr.layer, attr.color);
          emitSurfLines(it->triangleEdges, sc, 1.0f);
          emitSurfLines(it->minorContours, sc, 1.0f);
          emitSurfLines(it->majorContours, sc, 1.5f);
          emitSurfLines(it->borderEdges, sc, 1.5f);
          for (const auto& lb : it->contourLabels) {
            const ImVec2 p = m2s(static_cast<double>(lb.x) + oX, static_cast<double>(lb.y) + oY);
            char t[48];
            std::snprintf(t, sizeof(t), "%.2f", lb.level);
            sdl->AddText(p, sc, t);
          }
          for (const auto& ab : it->arrowLineBuffers)
            emitSurfLines(ab, sc, 1.0f);
        }
      }
      // Model TEXT / MTEXT through this viewport (issue #115): same m2s + clip as linework.
      {
        ImFont* vpFont = ImGui::GetFont();
        for (size_t ai = 0; ai < cmd.cadAnnotations.size(); ++ai) {
          const CadAnnotation& ann = cmd.cadAnnotations[ai];
          if (CadAnnotationIsDimension(ann))
            continue;
          if (ann.kind != CadAnnotation::Kind::Table && ann.text.empty())
            continue;
          const EntityAttributes* aa =
              (ai < cmd.cadAnnotationAttrs.size()) ? &cmd.cadAnnotationAttrs[ai] : nullptr;
          // Object isolation (REQ-084 (d)) has to be gated per-overlay, because annotations are drawn
          // here rather than by the GL pass — the model overlay's own annotation loop does exactly this.
          // Without it an isolated-away annotation stayed visible through every layout viewport, which is
          // the one place a hidden object is least likely to be noticed.
          const ViewportTextOverlayPlan plan =
              PlanViewportTextOverlay(ann, aa && CadEntityIdHidden(&cmd.hiddenEntityIds, aa->id), vp,
                                      cmd.modelUnitsPerPlottedInch);
          if (plan.skipHidden)
            continue;
          const std::string layer = aa ? (aa->layer.empty() ? std::string("0") : aa->layer) : std::string("0");
          if (IsLayerFrozenInViewport(vp, layer))
            continue;
          const ImU32 tcol = vpBaseCol(layer, aa ? aa->color : std::string("ByLayer"));
          if (ann.kind == CadAnnotation::Kind::Text) {
            const ImVec2 sp = m2s(static_cast<double>(ann.insX) + oX, static_cast<double>(ann.insY) + oY);
            const float hWorld = CadAnnotationHeightWorld(ann, plan.modelUnitsPerPlottedInch);
            const float fontPx = std::clamp(hWorld * pxPerModel, 1.f, 8192.f);
            DrawCadSingleLineText(sdl, ann, vpFont, sp, fontPx, tcol);
          } else if (ann.kind == CadAnnotation::Kind::Table && ann.tableCols > 0) {
            std::vector<CadTableCellRect> cells;
            CadTableLayoutCells(ann.boxMinX, ann.boxMinY, ann.boxMaxX, ann.boxMaxY, ann.tableCols, ann.tableCells,
                                &cells);
            const ImVec2 tl = m2s(static_cast<double>(ann.boxMinX) + oX, static_cast<double>(ann.boxMaxY) + oY);
            const ImVec2 brc = m2s(static_cast<double>(ann.boxMaxX) + oX, static_cast<double>(ann.boxMinY) + oY);
            const float rx0 = std::min(tl.x, brc.x);
            const float ry0 = std::min(tl.y, brc.y);
            const float rx1 = std::max(tl.x, brc.x);
            const float ry1 = std::max(tl.y, brc.y);
            sdl->AddRect(ImVec2(rx0, ry0), ImVec2(rx1, ry1), tcol, 0.f, 0, 1.f);
            const int rows = CadTableRowCount(ann.tableCols, ann.tableCells);
            for (int c = 1; c < ann.tableCols; ++c) {
              const float t = static_cast<float>(c) / static_cast<float>(ann.tableCols);
              const float x = rx0 + t * (rx1 - rx0);
              sdl->AddLine(ImVec2(x, ry0), ImVec2(x, ry1), tcol, 1.f);
            }
            for (int r = 1; r < rows; ++r) {
              const float t = static_cast<float>(r) / static_cast<float>(std::max(rows, 1));
              const float y = ry0 + t * (ry1 - ry0);
              sdl->AddLine(ImVec2(rx0, y), ImVec2(rx1, y), tcol, 1.f);
            }
            const float hWorld = CadAnnotationHeightWorld(ann, plan.modelUnitsPerPlottedInch);
            const float fontPx = std::clamp(hWorld * pxPerModel, 1.f, 8192.f);
            for (size_t i = 0; i < cells.size() && i < ann.tableCells.size(); ++i) {
              const ImVec2 p =
                  m2s(static_cast<double>(cells[i].x0) + oX, static_cast<double>(cells[i].y1) + oY);
              sdl->AddText(vpFont, fontPx, ImVec2(p.x + 2.f, p.y + 2.f), tcol, ann.tableCells[i].c_str());
            }
          } else if (ann.kind == CadAnnotation::Kind::Mtext) {
            const ImVec2 tl = m2s(static_cast<double>(ann.boxMinX) + oX, static_cast<double>(ann.boxMaxY) + oY);
            const ImVec2 brc = m2s(static_cast<double>(ann.boxMaxX) + oX, static_cast<double>(ann.boxMinY) + oY);
            // REQ-050: plain MTEXT is sized off the scale of the viewport it is drawn THROUGH, not the
            // drawing's plot scale, so its plotted height stays constant on the sheet whatever that
            // viewport's scale is. Using cmd.modelUnitsPerPlottedInch here made the same object read at
            // different sizes in model space and through a 1:50 vs a 1:100 viewport. The rule (and the
            // survey-label exclusion) lives in PlanViewportTextOverlay / MtextScaleThroughViewport.
            const float hWorld = CadAnnotationHeightWorld(ann, plan.modelUnitsPerPlottedInch);
            const float fontPx = std::clamp(hWorld * pxPerModel, 1.f, 8192.f);
            const int acol = (ann.mtextAttach - 1) % 3;
            const int arow = (ann.mtextAttach - 1) / 3;
            float pw = 8.f, ph = fontPx * 1.22f;
            MtextRichNaturalContentPx(vpFont, fontPx, ann.text, &pw, &ph, plan.fontFamily);
            float drawX = tl.x + 4.f, drawY = tl.y + 4.f;
            if (acol == 1)
              drawX = tl.x + 0.5f * ((brc.x - tl.x) - pw);
            else if (acol == 2)
              drawX = brc.x - pw - 4.f;
            if (arow == 1)
              drawY = tl.y + 0.5f * ((brc.y - tl.y) - ph);
            else if (arow == 2)
              drawY = brc.y - ph - 4.f;
            float wrapPx = std::max(8.f, (brc.x - tl.x) - 8.f);
            if (acol != 0)
              wrapPx = std::max(pw, 8.f);
            Shx::Font* sfm = CadIsShxFontName(plan.fontFamily) ? Shx::Resolve(plan.fontFamily) : nullptr;
            if (sfm && sfm->valid()) {
              const std::string plain = MtextRichFlattenToPlain(ann.text);
              const float lineH = fontPx * 1.4f;
              const float thick = std::max(1.f, fontPx * 0.05f);
              std::string ln;
              float ly = drawY;
              auto flush = [&](const std::string& line) {
                const float w = Shx::MeasureWidthPx(*sfm, line, fontPx);
                float lx = drawX;
                if (acol == 1)
                  lx = tl.x + 0.5f * ((brc.x - tl.x) - w);
                else if (acol == 2)
                  lx = std::max(tl.x + 4.f, brc.x - w - 4.f);
                Shx::DrawText(sdl, *sfm, ImVec2(lx, ly + fontPx), fontPx, 0.f, tcol, line, thick);
                ly += lineH;
              };
              for (char ch : plain) {
                if (ch == '\n') {
                  flush(ln);
                  ln.clear();
                } else
                  ln += ch;
              }
              flush(ln);
            } else {
              MtextRichDrawWrapped(sdl, vpFont, fontPx, ImVec2(drawX, drawY), wrapPx, tcol, ann.text,
                                   plan.fontFamily);
            }
          }
        }
      }
      // First-class TABLE entities (REQ-148) through this viewport.
      {
        ImFont* vpFont = ImGui::GetFont();
        for (size_t ti = 0; ti < cmd.cadTables.size(); ++ti) {
        const CadTable& tbl = cmd.cadTables[ti];
        const EntityAttributes* ta =
            (ti < cmd.cadTableAttrs.size()) ? &cmd.cadTableAttrs[ti] : nullptr;
        if (ta && CadEntityIdHidden(&cmd.hiddenEntityIds, ta->id))
          continue;
        const std::string layer = ta ? (ta->layer.empty() ? std::string("0") : ta->layer) : std::string("0");
        if (IsLayerFrozenInViewport(vp, layer))
          continue;
        const ImU32 tcol = vpBaseCol(layer, ta ? ta->color : std::string("ByLayer"));
        ImVec2 c0{}, c1{}, c2{}, c3{};
        float wx = 0.f, wy = 0.f;
        CadTableWorldCorner(tbl, 0, &wx, &wy);
        c0 = m2s(static_cast<double>(wx) + oX, static_cast<double>(wy) + oY);
        CadTableWorldCorner(tbl, 1, &wx, &wy);
        c1 = m2s(static_cast<double>(wx) + oX, static_cast<double>(wy) + oY);
        CadTableWorldCorner(tbl, 2, &wx, &wy);
        c2 = m2s(static_cast<double>(wx) + oX, static_cast<double>(wy) + oY);
        CadTableWorldCorner(tbl, 3, &wx, &wy);
        c3 = m2s(static_cast<double>(wx) + oX, static_cast<double>(wy) + oY);
        sdl->AddLine(c0, c1, tcol, 1.f);
        sdl->AddLine(c1, c2, tcol, 1.f);
        sdl->AddLine(c2, c3, tcol, 1.f);
        sdl->AddLine(c3, c0, tcol, 1.f);
        const int rows = CadTableRowCount(tbl);
        if (tbl.cols > 0 && rows > 0) {
          const float w = std::max(tbl.width, 1.e-3f);
          const float h = std::max(tbl.height, 1.e-3f);
          for (int c = 1; c < tbl.cols; ++c) {
            const float lx = w * static_cast<float>(c) / static_cast<float>(tbl.cols);
            float ax = 0.f, ay = 0.f, bx = 0.f, by = 0.f;
            CadTableLocalToWorld(tbl, lx, 0.f, &ax, &ay);
            CadTableLocalToWorld(tbl, lx, h, &bx, &by);
            sdl->AddLine(m2s(static_cast<double>(ax) + oX, static_cast<double>(ay) + oY),
                         m2s(static_cast<double>(bx) + oX, static_cast<double>(by) + oY), tcol, 1.f);
          }
          for (int r = 1; r < rows; ++r) {
            const float ly = h * static_cast<float>(r) / static_cast<float>(rows);
            float ax = 0.f, ay = 0.f, bx = 0.f, by = 0.f;
            CadTableLocalToWorld(tbl, 0.f, ly, &ax, &ay);
            CadTableLocalToWorld(tbl, w, ly, &bx, &by);
            sdl->AddLine(m2s(static_cast<double>(ax) + oX, static_cast<double>(ay) + oY),
                         m2s(static_cast<double>(bx) + oX, static_cast<double>(by) + oY), tcol, 1.f);
          }
        }
        std::vector<CadTableCellRect> cells;
        CadTableLayoutWorldCells(tbl, &cells);
        const float hWorld = CadTableHeightWorld(tbl, cmd.modelUnitsPerPlottedInch);
        const float fontPx = std::clamp(hWorld * pxPerModel, 1.f, 8192.f);
        for (size_t i = 0; i < cells.size() && i < tbl.cells.size(); ++i) {
          const ImVec2 p =
              m2s(static_cast<double>(cells[i].x0) + oX, static_cast<double>(cells[i].y1) + oY);
          sdl->AddText(vpFont, fontPx, ImVec2(p.x + 2.f, p.y + 2.f), tcol, tbl.cells[i].c_str());
        }
        }
      }
      // Model dimensions through this viewport (issue #110 / REQ-027): same m2s + clip as linework.
      {
        CadDimStrokeParams dsp;
        dsp.modelUnitsPerPlottedInch = cmd.modelUnitsPerPlottedInch;
        dsp.arrowSizeInches = cmd.activeDimensionStyle.arrowSizeInches;
        dsp.arrowScale = cmd.viewportDimArrowScale;
        dsp.arrowType = cmd.activeDimensionStyle.arrowType;
        ImFont* vpFont = ImGui::GetFont();
        auto dimWts = [&](float lx, float ly) {
          return m2s(static_cast<double>(lx) + oX, static_cast<double>(ly) + oY);
        };
        auto dimVpCol = [&](const std::string& styCol, const std::string& layer, const std::string& entityColor,
                            float defR, float defG, float defB) -> ImU32 {
          if (styCol == "ByLayer" || styCol.empty())
            return vpBaseCol(layer, entityColor);
          float rgba[4] = {defR, defG, defB, 1.f};
          ResolveStoredColorForViewport(styCol, 0.f, defR, defG, defB, rgba);
          AdaptWhiteBlackToBackground(&rgba[0], &rgba[1], &rgba[2], true);
          return IM_COL32(static_cast<int>(rgba[0] * 255.f), static_cast<int>(rgba[1] * 255.f),
                          static_cast<int>(rgba[2] * 255.f), 255);
        };
        for (size_t ai = 0; ai < cmd.cadAnnotations.size(); ++ai) {
          const CadAnnotation& ann = cmd.cadAnnotations[ai];
          if (!CadAnnotationIsDimension(ann))
            continue;
          const EntityAttributes* aa =
              (ai < cmd.cadAnnotationAttrs.size()) ? &cmd.cadAnnotationAttrs[ai] : nullptr;
          // Object isolation (REQ-084 (d)) has to be gated per-overlay, because annotations are drawn
          // here rather than by the GL pass — the model overlay's own annotation loop does exactly this.
          // Without it an isolated-away annotation stayed visible through every layout viewport, which is
          // the one place a hidden object is least likely to be noticed.
          if (aa && CadEntityIdHidden(&cmd.hiddenEntityIds, aa->id))
            continue;
          const std::string layer = aa ? (aa->layer.empty() ? std::string("0") : aa->layer) : std::string("0");
          if (IsLayerFrozenInViewport(vp, layer))
            continue;
          const std::string entCol = aa ? aa->color : std::string("ByLayer");
          CadDimWorldStrokes strokes;
          if (!CadDimBuildWorldStrokes(ann, dsp, &strokes))
            continue;
          const ImU32 lineCol = dimVpCol(cmd.activeDimensionStyle.dimLineColor, layer, entCol, 0.1f, 0.1f, 0.12f);
          const ImU32 extCol = dimVpCol(cmd.activeDimensionStyle.extLineColor, layer, entCol, 0.1f, 0.1f, 0.12f);
          const ImU32 textCol = dimVpCol(cmd.activeDimensionStyle.textColor, layer, entCol, 0.08f, 0.08f, 0.1f);
          const ImU32 arrowCol = dimVpCol(cmd.activeDimensionStyle.arrowColor, layer, entCol, 0.1f, 0.1f, 0.12f);
          const float hWorld = CadAnnotationHeightWorld(ann, cmd.modelUnitsPerPlottedInch);
          const float fontPx = std::clamp(hWorld * pxPerModel, 1.f, 8192.f);
          DrawCadDimStrokesOnDrawList(sdl, ann, strokes, dimWts, fontPx, extCol, lineCol, arrowCol, textCol, vpFont,
                                      dsp.arrowType);
        }
        if (isFloatVp && outCursorX && outCursorY) {
          CadAnnotation draft{};
          bool ok = false;
          if (cmd.active == AppCommandState::Kind::DimLinear &&
              cmd.dimPhase == AppCommandState::DimPhase::WaitDimLinePt)
            ok = CadDimLinearBuildDraft(cmd, *outCursorX, *outCursorY, &draft);
          else if (cmd.active == AppCommandState::Kind::DimAngular &&
                   cmd.dimAngularPhase == AppCommandState::DimAngularPhase::WaitArc)
            ok = CadDimAngularBuildDraft(cmd, *outCursorX, *outCursorY, &draft);
          else if (cmd.active == AppCommandState::Kind::DimAligned &&
                   cmd.dimPhase == AppCommandState::DimPhase::WaitDimLinePt)
            ok = CadDimAlignedBuildDraft(cmd, *outCursorX, *outCursorY, &draft);
          CadDimWorldStrokes dstrokes;
          if (ok && CadDimBuildWorldStrokes(draft, dsp, &dstrokes)) {
            const float hWorld = CadAnnotationHeightWorld(draft, cmd.modelUnitsPerPlottedInch);
            const float fontPx = std::clamp(hWorld * pxPerModel, 1.f, 8192.f);
            const ImU32 preview = IM_COL32(160, 220, 255, 180);
            DrawCadDimStrokesOnDrawList(sdl, draft, dstrokes, dimWts, fontPx, preview, preview, preview, preview,
                                        vpFont, dsp.arrowType);
          }
        }
      }
      // Entity grips (REQ-036): squares at each selected entity's grip points; the grabbed grip is hot.
      if (isFloatVp) {
        const float gh = 4.f;
        auto drawGrip = [&](float lx, float ly, bool hot) {
          const ImVec2 p = m2s(static_cast<double>(lx) + oX, static_cast<double>(ly) + oY);
          sdl->AddRectFilled(ImVec2(p.x - gh, p.y - gh), ImVec2(p.x + gh, p.y + gh),
                             hot ? IM_COL32(245, 120, 60, 255) : IM_COL32(59, 130, 246, 255));
          sdl->AddRect(ImVec2(p.x - gh, p.y - gh), ImVec2(p.x + gh, p.y + gh), IM_COL32(255, 255, 255, 255));
        };
        for (const SelectedEntity& sel : cmd.selection) {
          const bool gActive = cmd.entityGripMoveActive && cmd.entityGripType == sel.type &&
                               cmd.entityGripEntityIndex == sel.index;
          auto hot = [&](int which) { return gActive && cmd.entityGripWhich == which; };
          switch (sel.type) {
          case SelectedEntity::Type::LineSeg: {
            const size_t k = static_cast<size_t>(sel.index) * 6;
            if (k + 5 < cmd.userLinesFlat.size()) {
              drawGrip(cmd.userLinesFlat[k], cmd.userLinesFlat[k + 1], hot(0));
              drawGrip(cmd.userLinesFlat[k + 3], cmd.userLinesFlat[k + 4], hot(1));
            }
            break;
          }
          case SelectedEntity::Type::Circle: {
            const size_t k = static_cast<size_t>(sel.index) * 4;
            if (k + 3 < cmd.userCirclesCxCyZR.size()) {
              drawGrip(cmd.userCirclesCxCyZR[k], cmd.userCirclesCxCyZR[k + 1], hot(0));
              drawGrip(cmd.userCirclesCxCyZR[k] + cmd.userCirclesCxCyZR[k + 3], cmd.userCirclesCxCyZR[k + 1], hot(1));
            }
            break;
          }
          case SelectedEntity::Type::Polyline: {
            const int np = cmd.userPolylineOffsets.size() > 0 ? static_cast<int>(cmd.userPolylineOffsets.size() - 1) : 0;
            if (sel.index >= 0 && sel.index < np) {
              const int startV = cmd.userPolylineOffsets[static_cast<size_t>(sel.index)];
              const int endV = cmd.userPolylineOffsets[static_cast<size_t>(sel.index + 1)];
              for (int vi2 = 0; vi2 < endV - startV; ++vi2) {
                const size_t xIdx = static_cast<size_t>(startV + vi2) * 3;
                if (xIdx + 1 >= cmd.userPolylineVerts.size())
                  break;
                drawGrip(cmd.userPolylineVerts[xIdx], cmd.userPolylineVerts[xIdx + 1], hot(vi2));
              }
              CadForEachPolylineArcMidGrip(cmd, sel.index, [&](int seg, float mx, float my, float) {
                drawGrip(mx, my, hot(kPolyBulgeGripBase + seg));  // REQ-316 / ADR-047
              });
            }
            break;
          }
          case SelectedEntity::Type::Arc: {
            if (sel.index >= 0 && static_cast<size_t>(sel.index) < cmd.userArcs.size()) {
              const CadArc& a = cmd.userArcs[static_cast<size_t>(sel.index)];
              const float endRad = a.startRad + a.sweepRad;
              drawGrip(a.cx, a.cy, hot(0));
              drawGrip(a.cx + a.r * std::cos(a.startRad), a.cy + a.r * std::sin(a.startRad), hot(1));
              drawGrip(a.cx + a.r * std::cos(endRad), a.cy + a.r * std::sin(endRad), hot(2));
            }
            break;
          }
          case SelectedEntity::Type::Ellipse: {
            if (sel.index >= 0 && static_cast<size_t>(sel.index) < cmd.userEllipses.size()) {
              const CadEllipse& el = cmd.userEllipses[static_cast<size_t>(sel.index)];
              const float perpX = -el.majVy, perpY = el.majVx;
              drawGrip(el.cx, el.cy, hot(0));
              drawGrip(el.cx + el.majVx, el.cy + el.majVy, hot(1));
              drawGrip(el.cx + perpX * el.ratio, el.cy + perpY * el.ratio, hot(2));
            }
            break;
          }
          case SelectedEntity::Type::BlockRef: {
            if (sel.index >= 0 && static_cast<size_t>(sel.index) < cmd.cadBlockRefs.size()) {
              const CadBlockRef& r = cmd.cadBlockRefs[static_cast<size_t>(sel.index)];
              const int di = CadBlockFindDef(cmd.blockDefs, r.defName);
              if (di >= 0) {
                const CadBlockDefinition& def = cmd.blockDefs[static_cast<size_t>(di)];
                const int nG = CadBlockDynGripCount(def);
                for (int g = 0; g < nG; ++g) {
                  if (!CadBlockDynGripShownOnInsert(g))
                    continue;
                  float gx = 0.f, gy = 0.f, gz = 0.f;
                  if (CadBlockDynGripWorld(def, r, g, &gx, &gy, &gz))
                    drawGrip(gx, gy, hot(g));
                }
              } else
                drawGrip(r.xf.x, r.xf.y, hot(0));
            }
            break;
          }
          default:
            break;
          }
        }
      }
      sdl->PopClipRect();
      // Viewport border; clipped to sheet and drawing area so greyed viewport outline does not bleed out of bounds (issue #101).
      {
        const ImVec2 __vpCanvasMin = imgPos;
        const ImVec2 __vpCanvasMax = ImVec2(imgPos.x + avail.x, imgPos.y + avail.y);
        const ImVec2 __vpClipMin(std::max(a.x, __vpCanvasMin.x), std::max(a.y, __vpCanvasMin.y));
        const ImVec2 __vpClipMax(std::min(b.x, __vpCanvasMax.x), std::min(b.y, __vpCanvasMax.y));
        const bool __vpClipValid = __vpClipMin.x < __vpClipMax.x && __vpClipMin.y < __vpClipMax.y;
        if (__vpClipValid) sdl->PushClipRect(__vpClipMin, __vpClipMax, true);
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
        if (__vpClipValid) sdl->PopClipRect();
      }
    }
    // Native paper-space geometry (REQ-037): committed sheet lines + text, drawn on top of the viewports
    // (in paper inches via w2s — a title block / annotations sit above viewport content).
    // Clipped to the sheet AND to the viewport canvas so paper geometry cannot bleed outside
    // the sheet or outside the drawing area into surrounding UI (issue #101).
    {
      // Sheet rect (a,b) + viewport canvas (imgPos, avail) intersection
      const ImVec2 __sheetMin = a;
      const ImVec2 __sheetMax = b;
      const ImVec2 __canvasMin = imgPos;
      const ImVec2 __canvasMax = ImVec2(imgPos.x + avail.x, imgPos.y + avail.y);
      const ImVec2 __clipMin(std::max(__sheetMin.x, __canvasMin.x), std::max(__sheetMin.y, __canvasMin.y));
      const ImVec2 __clipMax(std::min(__sheetMax.x, __canvasMax.x), std::min(__sheetMax.y, __canvasMax.y));
      const bool __clipValid = __clipMin.x < __clipMax.x && __clipMin.y < __clipMax.y;
      if (__clipValid) sdl->PushClipRect(__clipMin, __clipMax, true);
      constexpr ImU32 kPaperSelCol = IM_COL32(59, 130, 246, 255);
      constexpr ImU32 kPaperHoverCol = IM_COL32(130, 180, 240, 255);  // hover pre-highlight (lighter blue), REQ-039
      const float pxPerPaperIn = avail.x / std::max(1.e-6f, static_cast<float>(worldRight - worldLeft));
      auto isPaperSel = [&](PaperEntityRef::Type t, int idx) {
        for (const PaperEntityRef& r : cmd.selectedPaperEntities)
          if (r.type == t && r.index == idx)
            return true;
        return false;
      };
      // Hover parity (REQ-039): the idle entity under the cursor draws in the hover color (selection wins).
      auto isPaperHover = [&](PaperEntityRef::Type t, int idx) {
        return cmd.active == AppCommandState::Kind::None && cmd.paperHoverValid && cmd.paperHover.type == t &&
               cmd.paperHover.index == idx;
      };
      // REQ-048: true entity/layer color for a native sheet entity (lines/text/circles/arcs/ellipses/
      // polylines), resolved like model space. Attrs are the parallel *Attrs vectors on the layout.
      auto paperAttrs = [&](PaperEntityRef::Type t, int idx) -> const EntityAttributes* {
        auto at = [](const std::vector<EntityAttributes>& v, int i) -> const EntityAttributes* {
          return (i >= 0 && static_cast<size_t>(i) < v.size()) ? &v[static_cast<size_t>(i)] : nullptr;
        };
        switch (t) {
        case PaperEntityRef::Type::Line:     return at(L.paperLineAttrs, idx);
        case PaperEntityRef::Type::Text:     return at(L.paperTextAttrs, idx);
        case PaperEntityRef::Type::Circle:   return at(L.paperCircleAttrs, idx);
        case PaperEntityRef::Type::Arc:      return at(L.paperArcAttrs, idx);
        case PaperEntityRef::Type::Ellipse:  return at(L.paperEllAttrs, idx);
        case PaperEntityRef::Type::Polyline: return at(L.paperPolyAttrs, idx);
        case PaperEntityRef::Type::Block:    return at(L.paperBlockRefAttrs, idx);
        }
        return nullptr;
      };
      auto paperTrueCol = [&](PaperEntityRef::Type t, int idx) -> ImU32 {
        const EntityAttributes* a = paperAttrs(t, idx);
        float rgba[4] = {0.08f, 0.08f, 0.1f, 1.f};
        if (a) {
          const CadLayerRow* lr = FindDrawingLayerRowCi(cmd, a->layer.empty() ? std::string("0") : a->layer);
          ResolveEntityRgbaForViewport(*a, lr, 0.08f, 0.08f, 0.1f, rgba);
        }
        AdaptWhiteBlackToBackground(&rgba[0], &rgba[1], &rgba[2], true);  // REQ-048: legible on the white sheet
        return IM_COL32(static_cast<int>(rgba[0] * 255.f), static_cast<int>(rgba[1] * 255.f),
                        static_cast<int>(rgba[2] * 255.f), 255);
      };
      auto paperCol = [&](bool sel, PaperEntityRef::Type t, int idx) -> ImU32 {
        return sel ? kPaperSelCol : (isPaperHover(t, idx) ? kPaperHoverCol : paperTrueCol(t, idx));
      };
      auto paperWid = [&](bool sel, PaperEntityRef::Type t, int idx) -> float {
        return sel ? 2.0f : (isPaperHover(t, idx) ? 1.6f : 1.2f);
      };
      // Solid fills (REQ-038 addendum) — drawn first so linework/text sits on top. The model GL pass fills
      // these even-odd over all loops (concave + island holes); the paper overlay has no stencil, so replicate
      // even-odd with a screen-space scanline fill: per row, pair sorted edge crossings and fill the odd spans.
      for (size_t fi = 0; fi < L.paperFilledRegions.size(); ++fi) {
        const CadFilledRegion& fr = L.paperFilledRegions[fi];
        if (fr.loopStart.empty() || fr.vertsXyz.size() < 9)  // < 3 vertices × 3 floats
          continue;
        float rgba[4] = {0.85f, 0.85f, 0.85f, 1.f};
        if (fi < L.paperFilledRegionAttrs.size()) {
          const EntityAttributes& fa = L.paperFilledRegionAttrs[fi];
          const CadLayerRow* lr = FindDrawingLayerRowCi(cmd, fa.layer.empty() ? std::string("0") : fa.layer);
          ResolveEntityRgbaForViewport(fa, lr, 0.85f, 0.85f, 0.85f, rgba);
        }
        const ImU32 fillCol = IM_COL32(static_cast<int>(rgba[0] * 255.f), static_cast<int>(rgba[1] * 255.f),
                                       static_cast<int>(rgba[2] * 255.f), 255);
        // Build screen-space edges (every loop, each closed) and the screen y-extent (clamped to the canvas).
        std::vector<ImVec4> edges;  // (x0,y0,x1,y1)
        float yMin = 1e30f, yMax = -1e30f;
        for (size_t lp = 0; lp < fr.loopStart.size(); ++lp) {
          const int begin = fr.loopStart[lp];
          const int cnt = fr.loopCount(lp);
          if (cnt < 3)
            continue;
          for (int k = 0; k < cnt; ++k) {
            const int a = begin + k, b = begin + (k + 1) % cnt;
            const ImVec2 pa =
                w2s(fr.vertsXyz[static_cast<size_t>(a) * 3], fr.vertsXyz[static_cast<size_t>(a) * 3 + 1]);
            const ImVec2 pb =
                w2s(fr.vertsXyz[static_cast<size_t>(b) * 3], fr.vertsXyz[static_cast<size_t>(b) * 3 + 1]);
            edges.push_back(ImVec4(pa.x, pa.y, pb.x, pb.y));
            yMin = std::min({yMin, pa.y, pb.y});
            yMax = std::max({yMax, pa.y, pb.y});
          }
        }
        if (edges.empty())
          continue;
        yMin = std::max(yMin, imgPos.y);
        yMax = std::min(yMax, imgPos.y + avail.y);
        std::vector<float> xs;
        for (float y = std::floor(yMin) + 0.5f; y < yMax; y += 1.f) {
          xs.clear();
          for (const ImVec4& e : edges) {
            const float y0 = e.y, y1 = e.w;
            if ((y >= y0 && y < y1) || (y >= y1 && y < y0))
              xs.push_back(e.x + (y - y0) * (e.z - e.x) / (y1 - y0));
          }
          if (xs.size() < 2)
            continue;
          std::sort(xs.begin(), xs.end());
          for (size_t s = 0; s + 1 < xs.size(); s += 2)  // even-odd: fill between consecutive crossing pairs
            sdl->AddRectFilled(ImVec2(xs[s], y - 0.5f), ImVec2(xs[s + 1], y + 0.5f), fillCol);
        }
      }
      for (size_t i = 0; i + 5 < L.paperLines.size(); i += 6) {
        const int idx = static_cast<int>(i / 6);
        const bool sel = isPaperSel(PaperEntityRef::Type::Line, idx);
        sdl->AddLine(w2s(L.paperLines[i], L.paperLines[i + 1]), w2s(L.paperLines[i + 3], L.paperLines[i + 4]),
                     paperCol(sel, PaperEntityRef::Type::Line, idx), paperWid(sel, PaperEntityRef::Type::Line, idx));
      }
      for (size_t bi = 0; bi < L.paperBlockRefs.size(); ++bi) {
        const int idx = static_cast<int>(bi);
        const bool sel = isPaperSel(PaperEntityRef::Type::Block, idx);
        EntityAttributes ia{};
        if (bi < L.paperBlockRefAttrs.size())
          ia = L.paperBlockRefAttrs[bi];
        std::vector<CadBlockWorldSeg> segs;
        CadBlockCollectWorldLines(cmd.blockDefs, L.paperBlockRefs[bi], ia, &segs);
        const ImU32 col = paperCol(sel, PaperEntityRef::Type::Block, idx);
        const float th = paperWid(sel, PaperEntityRef::Type::Block, idx);
        for (const CadBlockWorldSeg& s : segs)
          sdl->AddLine(w2s(s.x0, s.y0), w2s(s.x1, s.y1), col, th);
      }
      // Circles / arcs / ellipses / polylines (REQ-038, ADR-013), paper inches → screen via w2s (handles the
      // y-flip), so arcs/ellipses are tessellated through w2s rather than ImGui's screen-space PathArcTo.
      auto strokePaperPath = [&](const std::vector<ImVec2>& pts, bool closed, ImU32 col, float th) {
        for (size_t k = 0; k + 1 < pts.size(); ++k)
          sdl->AddLine(pts[k], pts[k + 1], col, th);
        if (closed && pts.size() >= 2)
          sdl->AddLine(pts.back(), pts.front(), col, th);
      };
      for (size_t ci = 0; ci + 2 < L.paperCircles.size(); ci += 3) {
        const int idx = static_cast<int>(ci / 3);
        const bool sel = isPaperSel(PaperEntityRef::Type::Circle, idx);
        sdl->AddCircle(w2s(L.paperCircles[ci], L.paperCircles[ci + 1]), L.paperCircles[ci + 2] * pxPerPaperIn,
                       paperCol(sel, PaperEntityRef::Type::Circle, idx), 0, paperWid(sel, PaperEntityRef::Type::Circle, idx));
      }
      for (size_t ai = 0; ai < L.paperArcs.size(); ++ai) {
        const int idx = static_cast<int>(ai);
        const bool sel = isPaperSel(PaperEntityRef::Type::Arc, idx);
        const CadArc& a = L.paperArcs[ai];
        constexpr int kSeg = 48;
        std::vector<ImVec2> pts;
        pts.reserve(kSeg + 1);
        for (int s = 0; s <= kSeg; ++s) {
          const float t = a.startRad + a.sweepRad * (static_cast<float>(s) / kSeg);
          pts.push_back(w2s(a.cx + a.r * std::cos(t), a.cy + a.r * std::sin(t)));
        }
        strokePaperPath(pts, false, paperCol(sel, PaperEntityRef::Type::Arc, idx), paperWid(sel, PaperEntityRef::Type::Arc, idx));
      }
      for (size_t ei = 0; ei < L.paperEllipses.size(); ++ei) {
        const int idx = static_cast<int>(ei);
        const bool sel = isPaperSel(PaperEntityRef::Type::Ellipse, idx);
        const CadEllipse& e = L.paperEllipses[ei];
        const float mnx = -e.majVy * e.ratio, mny = e.majVx * e.ratio;  // minor axis = perp(major) × ratio
        constexpr int kSeg = 64;
        std::vector<ImVec2> pts;
        pts.reserve(kSeg);
        for (int s = 0; s < kSeg; ++s) {
          const float t = 6.2831853f * (static_cast<float>(s) / kSeg);
          const float ct = std::cos(t), stt = std::sin(t);
          pts.push_back(w2s(e.cx + e.majVx * ct + mnx * stt, e.cy + e.majVy * ct + mny * stt));
        }
        strokePaperPath(pts, true, paperCol(sel, PaperEntityRef::Type::Ellipse, idx), paperWid(sel, PaperEntityRef::Type::Ellipse, idx));
      }
      const int nPaperPoly = static_cast<int>(L.paperPolyOffsets.size()) - 1;
      for (int pi = 0; pi < nPaperPoly; ++pi) {
        const bool sel = isPaperSel(PaperEntityRef::Type::Polyline, pi);
        const int v0 = L.paperPolyOffsets[static_cast<size_t>(pi)];
        const int v1 = L.paperPolyOffsets[static_cast<size_t>(pi + 1)];
        std::vector<ImVec2> pts;
        pts.reserve(static_cast<size_t>(std::max(0, v1 - v0)));
        for (int vi = v0; vi < v1; ++vi)
          pts.push_back(w2s(L.paperPolyVerts[static_cast<size_t>(vi * 3)], L.paperPolyVerts[static_cast<size_t>(vi * 3 + 1)]));
        const bool closed = static_cast<size_t>(pi) < L.paperPolyClosed.size() && L.paperPolyClosed[static_cast<size_t>(pi)];
        strokePaperPath(pts, closed, paperCol(sel, PaperEntityRef::Type::Polyline, pi), paperWid(sel, PaperEntityRef::Type::Polyline, pi));
      }
      ImFont* paperFont = ImGui::GetFont();
      // Draw a paper text/mtext entity with its real typeface + styling (REQ-038): SHX stroke fonts render
      // from the .shx file, TrueType via FontReg (faux bold/italic), MTEXT via the rich wrapper. Height scales
      // with zoom (plotted inches × px/inch), clamped [1, max] so it tracks the sheet instead of a fixed floor.
      auto drawPaperText = [&](const CadAnnotation& a, bool sel, bool hover, ImU32 baseCol) {
        // Paper-space dims are paper-inch entities (like text) but were falling through to the TEXT path and drawing as glyphs.
        // Render them as true dimensions so they are clipped to the sheet and scale with paper, not model.
        if (CadAnnotationIsDimension(a)) {
          CadDimStrokeParams dsp;
          dsp.modelUnitsPerPlottedInch = 1.f;
          dsp.arrowSizeInches = cmd.activeDimensionStyle.arrowSizeInches;
          dsp.arrowScale = 1.f;
          dsp.arrowType = cmd.activeDimensionStyle.arrowType;
          CadDimWorldStrokes strokes;
          if (!CadDimBuildWorldStrokes(a, dsp, &strokes))
            return;
          const ImU32 lineCol = sel ? kPaperSelCol : (hover ? kPaperHoverCol : baseCol);
          constexpr ImU32 kDimTextCol = IM_COL32(248, 250, 252, 255);
          const float hPx = std::clamp(a.plottedHeightInches * pxPerPaperIn, 1.f, 8192.f);
          const float fontPx = std::clamp(hPx, cmd.viewportDimTextMinPx, cmd.viewportDimTextMaxPx);
          auto paperWts = [&](float wx, float wy) { return w2s(wx, wy); };
          DrawCadDimStrokesOnDrawList(sdl, a, strokes, paperWts, fontPx, lineCol, lineCol, lineCol, kDimTextCol,
                                      paperFont, dsp.arrowType);
          if (sel) {
            ImVec2 sa = w2s(a.boxMinX, a.boxMinY), sb = w2s(a.boxMaxX, a.boxMaxY);
            sdl->AddRect(ImVec2(std::min(sa.x, sb.x), std::min(sa.y, sb.y)),
                         ImVec2(std::max(sa.x, sb.x), std::max(sa.y, sb.y)), kPaperSelCol, 0.f, 0, 1.f);
          }
          return;
        }
        if (a.text.empty())
          return;
        const ImU32 col = sel ? kPaperSelCol : (hover ? kPaperHoverCol : baseCol);  // REQ-048 true color
        const ImVec2 p = w2s(a.insX, a.insY);  // insertion ≈ bottom-left
        if (a.kind == CadAnnotation::Kind::Mtext) {
          // Same reasoning as the model path: sheet MTEXT must keep scaling with zoom, so the ceiling here
          // is only a rasterisation sanity bound, not the survey-label legibility cap.
          const float hPx = std::clamp(a.plottedHeightInches * pxPerPaperIn, 1.f, 8192.f);
          const ImVec2 tl = w2s(a.boxMinX, a.boxMaxY);   // top-left of the MTEXT box
          const ImVec2 brc = w2s(a.boxMaxX, a.boxMinY);  // bottom-right
          // Honor MTEXT attachment (group 71): col 0/1/2 = left/center/right, row 0/1/2 = top/middle/bottom.
          const int acol = (a.mtextAttach - 1) % 3;
          const int arow = (a.mtextAttach - 1) / 3;
          float pw = 8.f, ph = hPx * 1.22f;
          const std::string paperFam = CadDrawFontFamily(a.fontFamily);
          MtextRichNaturalContentPx(paperFont, hPx, a.text, &pw, &ph, paperFam);
          float drawX = tl.x + 4.f, drawY = tl.y + 4.f;
          // Anchor to the box without clamping back inside it — see the model-space path.
          if (acol == 1)      drawX = tl.x + 0.5f * ((brc.x - tl.x) - pw);
          else if (acol == 2) drawX = brc.x - pw - 4.f;
          if (arow == 1)      drawY = tl.y + 0.5f * ((brc.y - tl.y) - ph);
          else if (arow == 2) drawY = brc.y - ph - 4.f;
          float wrapPx = std::max(8.f, (brc.x - tl.x) - 8.f);
          if (acol != 0)
            wrapPx = std::max(pw, 8.f);
          Shx::Font* sfm = CadIsShxFontName(paperFam) ? Shx::Resolve(paperFam) : nullptr;
          // Box wraps, box does not hide (mirrors model space): clip to the viewport, not to the box.
          sdl->PushClipRect(imgPos, ImVec2(imgPos.x + avail.x, imgPos.y + avail.y), true);
          if (sfm && sfm->valid()) {
            // SHX MTEXT: flatten the rich wire, split on hard newlines, stroke each line (mirrors model).
            const std::string plain = MtextRichFlattenToPlain(a.text);
            const bool underline = a.text.find("[[u]]") != std::string::npos;
            const float lineH = hPx * 1.4f;
            const float thick = std::max(1.f, hPx * 0.05f);
            std::vector<std::string> lines;
            {
              std::string ln;
              for (char ch : plain) {
                if (ch == '\n') { lines.push_back(ln); ln.clear(); }
                else ln += ch;
              }
              lines.push_back(ln);
            }
            float ly = drawY;
            for (const std::string& ln : lines) {
              const float w = Shx::MeasureWidthPx(*sfm, ln, hPx);
              float lx = drawX;
              if (acol == 1)      lx = tl.x + 0.5f * ((brc.x - tl.x) - w);
              else if (acol == 2) lx = std::max(tl.x + 4.f, brc.x - w - 4.f);
              const ImVec2 base(lx, ly + hPx);
              Shx::DrawText(sdl, *sfm, base, hPx, 0.f, col, ln, thick);
              if (underline) {
                const float uy = base.y + std::max(1.5f, hPx * 0.12f);
                sdl->AddLine(ImVec2(lx, uy), ImVec2(lx + w, uy), col, thick);
              }
              ly += lineH;
            }
          } else {
            MtextRichDrawWrapped(sdl, paperFont, hPx, ImVec2(drawX, drawY), wrapPx, col, a.text, paperFam);
          }
          sdl->PopClipRect();
          if (sel)
            sdl->AddRect(tl, brc, kPaperSelCol, 0.f, 0, 1.f);
          return;
        }
        // Single-line TEXT: the model treats the insertion point as TOP-left (SHX baseline one cap-height down,
        // ImGui AddText from the top-left). Match it so pasted TEXT lands in its row instead of on the rule.
        const float hPx = std::clamp(a.plottedHeightInches * pxPerPaperIn, 1.f, cmd.viewportTextMaxPx);
        DrawCadSingleLineText(sdl, a, paperFont, p, hPx, col);
        if (sel) {
          float w = hPx * 0.6f, h = hPx;
          const std::string paperTextFam = CadDrawFontFamily(a.fontFamily);
          if (CadIsShxFontName(paperTextFam)) {
            if (Shx::Font* sf = Shx::Resolve(paperTextFam); sf && sf->valid())
              w = std::max(Shx::MeasureWidthPx(*sf, a.text, hPx), w);
          } else {
            ImFont* tf = ResolveCadTtf(paperTextFam, a.bold, a.italic, paperFont, nullptr, nullptr);
            const ImVec2 ext = tf->CalcTextSizeA(hPx, FLT_MAX, 0.f, a.text.c_str());
            w = std::max(ext.x, w);
            h = std::max(ext.y, h);
          }
          sdl->AddRect(p, ImVec2(p.x + w, p.y + h), kPaperSelCol, 0.f, 0, 1.f);
        }
      };
      for (size_t ti = 0; ti < L.paperTexts.size(); ++ti)
        drawPaperText(L.paperTexts[ti], isPaperSel(PaperEntityRef::Type::Text, static_cast<int>(ti)),
                      isPaperHover(PaperEntityRef::Type::Text, static_cast<int>(ti)),
                      paperTrueCol(PaperEntityRef::Type::Text, static_cast<int>(ti)));
      if (__clipValid) sdl->PopClipRect();
    }
    const float curPX = static_cast<float>(worldLeft + (mx / std::max(avail.x, 1.f)) * (worldRight - worldLeft));
    const float curPY = static_cast<float>(worldTop - (my / std::max(avail.y, 1.f)) * (worldTop - worldBottom));
    // Paper-space LINE rubber band (REQ-037): from the committed anchor (paper inches) to the (snapped) cursor.
    float snapCurPX = paperSnapActive ? paperSnapXIn : curPX;
    float snapCurPY = paperSnapActive ? paperSnapYIn : curPY;
    if (!InFloatingModelSpace(cmd) && cmd.active == AppCommandState::Kind::Line &&
        cmd.linePhase == AppCommandState::LinePhase::NeedNextPoint && hovered) {
      // Mirror the ORTHO constraint used at commit so the preview matches what will be drawn.
      if (!paperSnapActive && cmd.orthoMode) {
        if (std::fabs(snapCurPX - cmd.anchorX) >= std::fabs(snapCurPY - cmd.anchorY))
          snapCurPY = cmd.anchorY;
        else
          snapCurPX = cmd.anchorX;
      }
      sdl->AddLine(w2s(cmd.anchorX, cmd.anchorY), w2s(snapCurPX, snapCurPY), IM_COL32(59, 130, 246, 230), 1.5f);
    }
    // Paper-space RECT rubber band (REQ-053): the axis-aligned rectangle spanned by the first corner and
    // the (snapped) cursor.
    if (!InFloatingModelSpace(cmd) && cmd.active == AppCommandState::Kind::Rect &&
        cmd.rectPhase == AppCommandState::RectPhase::WaitSecondCorner && hovered) {
      const ImVec2 ra = w2s(cmd.rectX1, cmd.rectY1);
      const ImVec2 rb = w2s(snapCurPX, snapCurPY);
      sdl->AddRect(ImVec2(std::min(ra.x, rb.x), std::min(ra.y, rb.y)),
                   ImVec2(std::max(ra.x, rb.x), std::max(ra.y, rb.y)), IM_COL32(59, 130, 246, 230), 0.f, 0, 1.5f);
    }
    // Object-snap glyph (REQ-037): green square at the snapped paper point. REQ-307 (GitHub #106):
    // suppressed during the paper-space selection step, the same rule REQ-121 gives model space
    // (rule 1) — the step is picking OBJECTS, and a marker that jumps to nearby geometry is
    // misleading when the click hit-tests the raw cursor instead (see the entity/box pick below,
    // which never reads paperSnapXIn/YIn during this step).
    if (paperSnapActive && hovered && !PaperIsObjectSelectionStep(cmd)) {
      const ImVec2 g = w2s(paperSnapXIn, paperSnapYIn);
      sdl->AddRect(ImVec2(g.x - 5.f, g.y - 5.f), ImVec2(g.x + 5.f, g.y + 5.f), IM_COL32(120, 220, 120, 255), 0.f,
                   0, 1.5f);
    }
    // Paper-space PASTE preview (REQ-038): ghost the clipboard at the (snapped) cursor before the placing click.
    if (!InFloatingModelSpace(cmd) && cmd.active == AppCommandState::Kind::Paste &&
        cmd.modifyPhase == AppCommandState::ModifyPhase::NeedDestination && hovered && !cmd.clipboard.empty()) {
      const float gdx = snapCurPX - cmd.clipboard.basePtX;
      const float gdy = snapCurPY - cmd.clipboard.basePtY;
      const float pxPerPaperIn2 = avail.x / std::max(1.e-6f, static_cast<float>(worldRight - worldLeft));
      const ImU32 ghost = IM_COL32(59, 130, 246, 210);
      const CadClipboard& cb = cmd.clipboard;
      for (size_t i = 0; i + 5 < cb.lines.size(); i += 6)
        sdl->AddLine(w2s(cb.lines[i] + gdx, cb.lines[i + 1] + gdy), w2s(cb.lines[i + 3] + gdx, cb.lines[i + 4] + gdy),
                     ghost, 1.4f);
      // Clipboard circles are cx,cy,z,r; the paste ghost is drawn in plan, so z is ignored here.
      for (size_t i = 0; i + 3 < cb.circlesCxCyZR.size(); i += 4)
        sdl->AddCircle(w2s(cb.circlesCxCyZR[i] + gdx, cb.circlesCxCyZR[i + 1] + gdy),
                       cb.circlesCxCyZR[i + 3] * pxPerPaperIn2, ghost, 0, 1.4f);
      for (const CadArc& a : cb.arcs) {
        constexpr int kSeg = 40;
        ImVec2 prev{};
        for (int s = 0; s <= kSeg; ++s) {
          const float t = a.startRad + a.sweepRad * (static_cast<float>(s) / kSeg);
          const ImVec2 p = w2s(a.cx + a.r * std::cos(t) + gdx, a.cy + a.r * std::sin(t) + gdy);
          if (s > 0)
            sdl->AddLine(prev, p, ghost, 1.4f);
          prev = p;
        }
      }
      for (const CadEllipse& e : cb.ellipses) {
        const float mnx = -e.majVy * e.ratio, mny = e.majVx * e.ratio;
        constexpr int kSeg = 56;
        ImVec2 first{}, prev{};
        for (int s = 0; s < kSeg; ++s) {
          const float t = 6.2831853f * (static_cast<float>(s) / kSeg);
          const ImVec2 p = w2s(e.cx + e.majVx * std::cos(t) + mnx * std::sin(t) + gdx,
                               e.cy + e.majVy * std::cos(t) + mny * std::sin(t) + gdy);
          if (s == 0)
            first = p;
          else
            sdl->AddLine(prev, p, ghost, 1.4f);
          prev = p;
        }
        sdl->AddLine(prev, first, ghost, 1.4f);
      }
      const int nGhostPoly = static_cast<int>(cb.polyOffsets.size()) - 1;
      for (int pi = 0; pi < nGhostPoly; ++pi) {
        const int v0 = cb.polyOffsets[static_cast<size_t>(pi)];
        const int v1 = cb.polyOffsets[static_cast<size_t>(pi + 1)];
        for (int vi = v0; vi + 1 < v1; ++vi)
          sdl->AddLine(w2s(cb.polyVerts[static_cast<size_t>(vi * 3)] + gdx, cb.polyVerts[static_cast<size_t>(vi * 3 + 1)] + gdy),
                       w2s(cb.polyVerts[static_cast<size_t>(vi * 3 + 3)] + gdx, cb.polyVerts[static_cast<size_t>(vi * 3 + 4)] + gdy),
                       ghost, 1.4f);
        if (static_cast<size_t>(pi) < cb.polyClosed.size() && cb.polyClosed[static_cast<size_t>(pi)] && v1 - v0 >= 2)
          sdl->AddLine(w2s(cb.polyVerts[static_cast<size_t>((v1 - 1) * 3)] + gdx, cb.polyVerts[static_cast<size_t>((v1 - 1) * 3 + 1)] + gdy),
                       w2s(cb.polyVerts[static_cast<size_t>(v0 * 3)] + gdx, cb.polyVerts[static_cast<size_t>(v0 * 3 + 1)] + gdy),
                       ghost, 1.4f);
      }
      ImFont* pf = ImGui::GetFont();
      for (const CadAnnotation& a : cb.annotations) {
        if (a.text.empty())
          continue;
        CadAnnotation g = a;
        g.insX += gdx;
        g.insY += gdy;
        const float hPx = std::clamp(g.plottedHeightInches * pxPerPaperIn2, 1.f, cmd.viewportTextMaxPx);
        const ImVec2 p = w2s(g.insX, g.insY);
        if (CadAnnotationIsDimension(g)) {
          DrawDimLabelText(sdl, g, pf, hPx, p, 0.f, ghost);
          continue;
        }
        if (g.kind == CadAnnotation::Kind::Mtext)
          g.text = MtextRichFlattenToPlain(a.text);
        DrawCadSingleLineText(sdl, g, pf, p, hPx, ghost);
      }
    }
    // Paper-entity MOVE/COPY ghost + ROTATE + MIRROR preview (REQ-037/REQ-103): selected geometry
    // transformed by the cursor.
    if (!cmd.selectedPaperEntities.empty() && hovered &&
        (cmd.paperMovePhase == 2 || cmd.paperRotatePhase == 2 || cmd.paperMirrorPhase == 2)) {
      const ImU32 ghostCol = (cmd.paperMovePhase == 2 && cmd.paperMoveIsCopy) ? IM_COL32(120, 220, 120, 220)
                                                                              : IM_COL32(245, 200, 70, 220);
      const bool rotating = cmd.paperRotatePhase == 2;
      const bool mirroring = cmd.paperMirrorPhase == 2;
      const float ang = rotating ? std::atan2(curPY - cmd.paperRotateBaseYIn, curPX - cmd.paperRotateBaseXIn) : 0.f;
      const float ca = std::cos(ang), sa = std::sin(ang);
      const float bX = cmd.paperRotateBaseXIn, bY = cmd.paperRotateBaseYIn;
      const float dX = curPX - cmd.paperMoveBaseXIn, dY = curPY - cmd.paperMoveBaseYIn;
      const float mX0 = cmd.paperMirrorP1XIn, mY0 = cmd.paperMirrorP1YIn;
      const float mdx = curPX - mX0, mdy = curPY - mY0;
      const float mlen2 = mdx * mdx + mdy * mdy;
      auto xf = [&](float x, float y) -> ImVec2 {
        if (mirroring) {
          if (mlen2 < 1e-12f)
            return w2s(x, y);
          const float t = ((x - mX0) * mdx + (y - mY0) * mdy) / mlen2;
          const float px = mX0 + t * mdx, py = mY0 + t * mdy;
          return w2s(2.f * px - x, 2.f * py - y);
        }
        if (rotating)
          return w2s(bX + (x - bX) * ca - (y - bY) * sa, bY + (x - bX) * sa + (y - bY) * ca);
        return w2s(x + dX, y + dY);
      };
      const float pxPerPaperInG = avail.x / std::max(1.e-6f, static_cast<float>(worldRight - worldLeft));
      for (const PaperEntityRef& r : cmd.selectedPaperEntities) {
        switch (r.type) {
        case PaperEntityRef::Type::Line: {
          const size_t i = static_cast<size_t>(r.index) * 6;
          if (i + 5 < L.paperLines.size())
            sdl->AddLine(xf(L.paperLines[i], L.paperLines[i + 1]), xf(L.paperLines[i + 3], L.paperLines[i + 4]),
                         ghostCol, 1.5f);
          break;
        }
        case PaperEntityRef::Type::Circle: {
          const size_t i = static_cast<size_t>(r.index) * 3;
          if (i + 2 < L.paperCircles.size())
            sdl->AddCircle(xf(L.paperCircles[i], L.paperCircles[i + 1]), L.paperCircles[i + 2] * pxPerPaperInG,
                           ghostCol, 0, 1.5f);
          break;
        }
        case PaperEntityRef::Type::Arc: {
          if (r.index >= 0 && static_cast<size_t>(r.index) < L.paperArcs.size()) {
            const CadArc& a = L.paperArcs[static_cast<size_t>(r.index)];
            ImVec2 prev{};
            for (int sgi = 0; sgi <= 40; ++sgi) {
              const float t = a.startRad + a.sweepRad * (static_cast<float>(sgi) / 40.f);
              const ImVec2 p = xf(a.cx + a.r * std::cos(t), a.cy + a.r * std::sin(t));
              if (sgi > 0)
                sdl->AddLine(prev, p, ghostCol, 1.5f);
              prev = p;
            }
          }
          break;
        }
        case PaperEntityRef::Type::Ellipse: {
          if (r.index >= 0 && static_cast<size_t>(r.index) < L.paperEllipses.size()) {
            const CadEllipse& e = L.paperEllipses[static_cast<size_t>(r.index)];
            const float mnx = -e.majVy * e.ratio, mny = e.majVx * e.ratio;
            ImVec2 first{}, prev{};
            for (int sgi = 0; sgi < 56; ++sgi) {
              const float t = 6.2831853f * (static_cast<float>(sgi) / 56.f);
              const ImVec2 p = xf(e.cx + e.majVx * std::cos(t) + mnx * std::sin(t),
                                  e.cy + e.majVy * std::cos(t) + mny * std::sin(t));
              if (sgi == 0) first = p; else sdl->AddLine(prev, p, ghostCol, 1.5f);
              prev = p;
            }
            sdl->AddLine(prev, first, ghostCol, 1.5f);
          }
          break;
        }
        case PaperEntityRef::Type::Polyline: {
          const int pi = r.index;
          if (pi >= 0 && static_cast<size_t>(pi + 1) < L.paperPolyOffsets.size()) {
            const int v0 = L.paperPolyOffsets[static_cast<size_t>(pi)];
            const int v1 = L.paperPolyOffsets[static_cast<size_t>(pi + 1)];
            for (int vi = v0; vi + 1 < v1; ++vi)
              sdl->AddLine(xf(L.paperPolyVerts[static_cast<size_t>(vi * 3)], L.paperPolyVerts[static_cast<size_t>(vi * 3 + 1)]),
                           xf(L.paperPolyVerts[static_cast<size_t>(vi * 3 + 3)], L.paperPolyVerts[static_cast<size_t>(vi * 3 + 4)]),
                           ghostCol, 1.5f);
          }
          break;
        }
        case PaperEntityRef::Type::Text: {
          if (r.index >= 0 && static_cast<size_t>(r.index) < L.paperTexts.size()) {
            const CadAnnotation& a = L.paperTexts[static_cast<size_t>(r.index)];
            sdl->AddCircleFilled(xf(a.insX, a.insY), 3.f, ghostCol);
          }
          break;
        }
        case PaperEntityRef::Type::Block: {
          if (r.index >= 0 && static_cast<size_t>(r.index) < L.paperBlockRefs.size()) {
            EntityAttributes ia{};
            if (static_cast<size_t>(r.index) < L.paperBlockRefAttrs.size())
              ia = L.paperBlockRefAttrs[static_cast<size_t>(r.index)];
            std::vector<CadBlockWorldSeg> segs;
            CadBlockCollectWorldLines(cmd.blockDefs, L.paperBlockRefs[static_cast<size_t>(r.index)], ia, &segs);
            for (const CadBlockWorldSeg& s : segs)
              sdl->AddLine(xf(s.x0, s.y0), xf(s.x1, s.y1), ghostCol, 1.5f);
          }
          break;
        }
        }
      }
      if (rotating)
        sdl->AddLine(w2s(bX, bY), w2s(curPX, curPY), ghostCol, 1.f);
    }
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
      // Snap the in-place preview cursor to the floating snap point so the rubber band + crosshair match
      // what a click commits (REQ-036).
      float drawLX = curLX, drawLY = curLY;
      if (floatingSnapHit.valid) {
        drawLX = floatingSnapHit.x;
        drawLY = floatingSnapHit.y;
      }
      if (lineRubber || plineRubber)
        sdl->AddLine(mlToScreen(cmd.anchorX, cmd.anchorY), mlToScreen(drawLX, drawLY),
                     IM_COL32(90, 220, 120, 230), 1.5f);
      // RECT rubber band through a floating viewport (REQ-036 / REQ-053).
      if (cmd.active == AppCommandState::Kind::Rect &&
          cmd.rectPhase == AppCommandState::RectPhase::WaitSecondCorner) {
        const ImVec2 ra = mlToScreen(cmd.rectX1, cmd.rectY1);
        const ImVec2 rb = mlToScreen(drawLX, drawLY);
        sdl->AddRect(ImVec2(std::min(ra.x, rb.x), std::min(ra.y, rb.y)),
                     ImVec2(std::max(ra.x, rb.x), std::max(ra.y, rb.y)), IM_COL32(90, 220, 120, 230), 0.f, 0, 1.5f);
      }
      // The cursor crosshair itself is the full CAD crosshair drawn later (at the raw mouse in floating mode);
      // no small marker here. Keep the snap glyph below.
      if (floatingSnapHit.valid) {  // object-snap glyph (green square), sized to match the model-space glyph
        const ImVec2 sg = mlToScreen(floatingSnapHit.x, floatingSnapHit.y);
        const float h = std::clamp(cmd.objectSnapGlyphHalfPx, 3.f, 48.f);
        sdl->AddRect(ImVec2(sg.x - h, sg.y - h), ImVec2(sg.x + h, sg.y + h), IM_COL32(120, 220, 120, 255), 0.f,
                     0, 2.0f);
      }
      // Selection box (REQ-036): window (L→R, blue) or crossing (R→L, green), drawn from the armed anchor
      // to the cursor while a box is open in the floating viewport.
      if (cmd.active == AppCommandState::Kind::None && cmd.selBoxWaitingSecond) {
        const ImVec2 a = mlToScreen(static_cast<float>(cmd.selBoxAnchorX), static_cast<float>(cmd.selBoxAnchorY));
        const ImVec2 b = mlToScreen(curLX, curLY);
        const ImVec2 mn(std::min(a.x, b.x), std::min(a.y, b.y));
        const ImVec2 mx2(std::max(a.x, b.x), std::max(a.y, b.y));
        const bool windowMode = (mx - cmd.selBoxAnchorScreenX) > 3.f;
        const ImU32 fill = windowMode ? IM_COL32(59, 130, 246, 40) : IM_COL32(90, 220, 120, 40);
        const ImU32 edge = windowMode ? IM_COL32(59, 130, 246, 200) : IM_COL32(90, 220, 120, 220);
        sdl->AddRectFilled(mn, mx2, fill);
        sdl->AddRect(mn, mx2, edge, 0.f, 0, 1.0f);
      }
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
  std::vector<CadTable> transformTablePreviews;
  if (outCursorX && outCursorY)
    CadTableCollectTransformPreviews(cmd, *outCursorX, *outCursorY, &transformTablePreviews);

  using AK = AppCommandState::Kind;
  using AMP = AppCommandState::MtextPhase;
  using ADP = AppCommandState::DimPhase;
  const bool showMtextCmdDraft =
      cmd.active == AK::Mtext && cmd.mtextPhase == AMP::WaitString && !cmd.mtextRichEditorOpen;
  const bool showDimCmdDraft =
      ((cmd.active == AK::DimAligned || cmd.active == AK::DimLinear) && cmd.dimPhase == ADP::WaitDimLinePt ||
       (cmd.active == AK::DimAngular && cmd.dimAngularPhase == AppCommandState::DimAngularPhase::WaitArc)) &&
      outCursorX && outCursorY;

  // Model annotations live in MODEL coordinates; draw them only when the model is the active canvas (model
  // space or floating model space). In a paper layout the canvas is the sheet (paper inches), so drawing
  // model text here would paint it at local-coord positions on the sheet — the stray "artifacts" bug (REQ-038).
  const bool modelAnnotationsVisible = modelSpace || InFloatingModelSpace(cmd);

  // HATCH preview (REQ-043): translucent fill + bright outline of the candidate region under the cursor.
  if (modelAnnotationsVisible && cmd.active == AK::Hatch && cmd.hatchPreviewValid &&
      cmd.hatchPreviewLoop.size() >= 6) {
    ImDrawList* pdl = ImGui::GetWindowDrawList();
    std::vector<ImVec2> pts;
    pts.reserve(cmd.hatchPreviewLoop.size() / 2);
    const Camera hatchCam = CadViewCamera(cmd);
    // The preview sits on the elevation CadHatchCommitLoop will commit the region at, not on the
    // datum (REQ-058) — a preview that does not consume the commit point is not a preview.
    const double hatchPreviewZ = static_cast<double>(CadCommitElevation(cmd));
    for (size_t i = 0; i + 1 < cmd.hatchPreviewLoop.size(); i += 2) {
      // Camera-projected so the preview outline stays on the geometry it traced when orbited
      // (REQ-058); identical to the previous mapping in plan view.
      float sx = 0.f, sy = 0.f;
      hatchCam.WorldToScreen(static_cast<double>(cmd.hatchPreviewLoop[i]),
                             static_cast<double>(cmd.hatchPreviewLoop[i + 1]), hatchPreviewZ, avail.x, avail.y, &sx,
                             &sy);
      pts.push_back(ImVec2(imgPos.x + sx, imgPos.y + sy));
    }
    const int r = static_cast<int>(cmd.hatchColorRgb[0] * 255.f);
    const int g = static_cast<int>(cmd.hatchColorRgb[1] * 255.f);
    const int b = static_cast<int>(cmd.hatchColorRgb[2] * 255.f);
    pdl->AddConvexPolyFilled(pts.data(), static_cast<int>(pts.size()), IM_COL32(r, g, b, 80));
    pdl->AddPolyline(pts.data(), static_cast<int>(pts.size()), IM_COL32(r, g, b, 235), ImDrawFlags_Closed, 2.0f);
  }

  // Hatch line patterns (REQ-043, ADR-018): draw each non-solid region's clipped pattern lines in its resolved
  // colour. Solid fills render under the linework in the GL pass; line patterns render here in the overlay.
  if (modelAnnotationsVisible && !cmd.cadFilledRegions.empty()) {
    ImDrawList* hdl = ImGui::GetWindowDrawList();
    const Camera hatchCam = CadViewCamera(cmd);
    std::vector<float> segs;
    for (size_t fi = 0; fi < cmd.cadFilledRegions.size(); ++fi) {
      const CadFilledRegion& fr = cmd.cadFilledRegions[fi];
      if (fr.isSolid())
        continue;
      const hatchpat::Def* pdef = hatchpat::Find(HatchLibrary(), fr.patternName);
      if (!pdef)
        continue;
      segs.clear();
      if (hatchpattern::BuildSegments(fr, *pdef, &segs) == 0)
        continue;
      // BuildSegments clips the pattern against the boundary in XY and returns no elevations, so
      // the family is drawn on the region's own plane — its mean vertex Z, which is exact for the
      // planar case every hatch is (REQ-058). Without it the pattern stayed on the datum while its
      // solid-filled counterpart and its boundary moved with the orbit.
      double frZSum = 0.;
      size_t frZCount = 0;
      for (size_t vi = 2; vi < fr.vertsXyz.size(); vi += 3) {
        frZSum += static_cast<double>(fr.vertsXyz[vi]);
        ++frZCount;
      }
      const double frZ = frZCount ? frZSum / static_cast<double>(frZCount) : 0.;
      float rgba[4] = {0.78f, 0.78f, 0.78f, 1.f};
      const EntityAttributes* ap = fi < cmd.cadFilledRegionAttrs.size() ? &cmd.cadFilledRegionAttrs[fi] : nullptr;
      if (ap) {
        const CadLayerRow* lr = FindDrawingLayerRowCi(cmd, ap->layer);
        ResolveEntityRgbaForViewport(*ap, lr, 0.78f, 0.78f, 0.78f, rgba);
      }
      const ImU32 col = IM_COL32(static_cast<int>(rgba[0] * 255.f), static_cast<int>(rgba[1] * 255.f),
                                 static_cast<int>(rgba[2] * 255.f),
                                 static_cast<int>(std::clamp(rgba[3], 0.f, 1.f) * 255.f));
      for (size_t s = 0; s + 3 < segs.size(); s += 4) {
        // Camera-projected (REQ-058): pattern lines must stay inside their region when orbited.
        float px0 = 0.f, py0 = 0.f, px1 = 0.f, py1 = 0.f;
        hatchCam.WorldToScreen(static_cast<double>(segs[s]), static_cast<double>(segs[s + 1]), frZ, avail.x,
                               avail.y, &px0, &py0);
        hatchCam.WorldToScreen(static_cast<double>(segs[s + 2]), static_cast<double>(segs[s + 3]), frZ, avail.x,
                               avail.y, &px1, &py1);
        const float u0 = px0 / std::max(avail.x, 1.f), v0 = py0 / std::max(avail.y, 1.f);
        const float u1 = px1 / std::max(avail.x, 1.f), v1 = py1 / std::max(avail.y, 1.f);
        hdl->AddLine(ImVec2(imgPos.x + u0 * avail.x, imgPos.y + v0 * avail.y),
                     ImVec2(imgPos.x + u1 * avail.x, imgPos.y + v1 * avail.y), col, 1.0f);
      }
    }
  }

  if (modelAnnotationsVisible &&
      (CadNeedsAnnotationOverlay(cmd.cadAnnotations.size(), cmd.cadTables.size(), cmd.cadBlockRefs.size(),
                                 showMtextCmdDraft, showDimCmdDraft) ||
       !transformAnnPreviews.empty() || !transformTablePreviews.empty())) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // Clip model annotations to the drawing viewport so they cannot bleed into surrounding UI (issue #101).
    dl->PushClipRect(imgPos, ImVec2(imgPos.x + avail.x, imgPos.y + avail.y), true);
    // Annotations are drawn by ImGui, not GL, so they do NOT inherit the renderer's MVP: without
    // routing through the camera they would stay in plan positions while an orbit tilts the
    // linework around them (REQ-058). In plan view `Camera::WorldToScreen` reduces exactly to the
    // previous linear mapping — asserted by a parity test — so nothing changes until the user
    // actually orbits. Coordinates here are LOCAL storage space, which is also the camera's
    // target space, so no rebase is involved.
    const Camera annCam = CadViewCamera(cmd);
    auto worldToScreen = [&](float wx, float wy, ImVec2* out, float wz = 0.f) {
      float px = 0.f, py = 0.f;
      annCam.WorldToScreen(static_cast<double>(wx), static_cast<double>(wy), static_cast<double>(wz), avail.x,
                           avail.y, &px, &py);
      out->x = imgPos.x + px;
      out->y = imgPos.y + py;
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
      if (CadAnnotationIsDimension(a) && cmd.activeSpaceIndex >= 0)
        return;
      if (a.kind == CadAnnotation::Kind::Text) {
        ImVec2 sp{};
        worldToScreen(a.insX, a.insY, &sp, a.insZ);
        // CAD TEXT is model-sized: scale with the drawing (no min-px floor), so it stays proportional when
        // zoomed out instead of ballooning at the readability floor. Only cap the upper end.
        const float fontPx =
            std::clamp(hWorld / std::max(worldPerPxY, 1.e-6f), 1.f, cmd.viewportTextMaxPx);
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
        DrawCadSingleLineText(dl, a, font, sp, fontPx, col);
      } else if (a.kind == CadAnnotation::Kind::DimAligned || a.kind == CadAnnotation::Kind::DimLinear) {
        float sx1 = 0.f, sy1 = 0.f, sx2 = 0.f, sy2 = 0.f, tx = 0.f, ty = 0.f, nx = 0.f, ny = 0.f, meas = 0.f;
        if (!CadDimAnyGeometry(a, &sx1, &sy1, &sx2, &sy2, &tx, &ty, &nx, &ny, &meas))
          return;
        // Resolve colors from DimensionStyle, falling back to ByLayer/entity
        auto resolveDimColor = [&](const std::string& styCol, float defR, float defG, float defB) -> ImU32 {
          if (styCol == "ByLayer" || styCol.empty()) {
            float rgba[4];
            if (attrPtr) ResolveEntityColorForViewport(*attrPtr, defR, defG, defB, rgba);
            else { rgba[0]=defR; rgba[1]=defG; rgba[2]=defB; rgba[3]=1.f; }
            return IM_COL32(int(rgba[0]*255), int(rgba[1]*255), int(rgba[2]*255), int(rgba[3]*255));
          } else {
            float rgba[4];
            ResolveStoredColorForViewport(styCol, 0.f, defR, defG, defB, rgba);
            return IM_COL32(int(rgba[0]*255), int(rgba[1]*255), int(rgba[2]*255), int(rgba[3]*255));
          }
        };
        const ImU32 lineCol = resolveDimColor(cmd.activeDimensionStyle.dimLineColor, 225/255.f, 177/255.f, 44/255.f);
        const ImU32 extCol = resolveDimColor(cmd.activeDimensionStyle.extLineColor, 225/255.f, 177/255.f, 44/255.f);
        const ImU32 textCol = resolveDimColor(cmd.activeDimensionStyle.textColor, 248/255.f, 250/255.f, 252/255.f);
        // Dimension geometry is overlay-drawn, so it projects through the camera like everything
        // else (REQ-058); it sits on the dimension's own plane. Identical to the previous mapping
        // in plan view.
        const Camera dimCam = CadViewCamera(cmd);
        const float dimZ = a.insZ;
        auto ws = [&](float wx, float wy, ImVec2* o) {
          float sx = 0.f, sy = 0.f;
          dimCam.WorldToScreen(static_cast<double>(wx), static_cast<double>(wy), static_cast<double>(dimZ),
                               avail.x, avail.y, &sx, &sy);
          o->x = imgPos.x + sx;
          o->y = imgPos.y + sy;
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
        dl->AddLine(A, B, extCol, extPx);
        ws(ex2, ey2, &A);
        ws(sx2 + nx * over, sy2 + ny * over, &B);
        dl->AddLine(A, B, extCol, extPx);
        // Arrow length in world units from DimensionStyle arrowSize (plotted inches) with viewport scale; tiny floor from meas for readability.
        const float styleArrowWorld = cmd.activeDimensionStyle.arrowSizeInches * cmd.modelUnitsPerPlottedInch;
        const float alenW =
            std::max(styleArrowWorld * cmd.viewportDimArrowScale * 0.10f, cmd.viewportDimArrowScale * 0.012f * meas);
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
          const ImU32 arrowCol = resolveDimColor(cmd.activeDimensionStyle.arrowColor, 225/255.f, 177/255.f, 44/255.f);
          dl->AddTriangleFilled(t0, t1, t2, arrowCol);
          ws(tip2x, tip2y, &t0);
          ws(base2x + ox, base2y + oy, &t1);
          ws(base2x - ox, base2y - oy, &t2);
          dl->AddTriangleFilled(t0, t1, t2, arrowCol);
        }
        ImVec2 sp{};
        ws(a.insX, a.insY, &sp);
        ImVec2 spDir{};
        const float dirStep = std::max(1.e-4f, hWorld);
        ws(a.insX + std::cos(a.rotationRad) * dirStep, a.insY + std::sin(a.rotationRad) * dirStep, &spDir);
        const float screenAng = std::atan2(spDir.y - sp.y, spDir.x - sp.x);
        DrawDimLabelText(dl, a, font, fontPx, sp, screenAng, textCol);
      } else if (a.kind == CadAnnotation::Kind::DimAngular) {
        float a1=0.f,a2=0.f,sweep=0.f,theta=0.f,bisx=0.f,bisy=0.f;
        if (!CadDimAngularComputeFrame(a, &a1,&a2,&sweep,&bisx,&bisy,&theta)) return;
        const float R = std::max(a.dimSignedOffset, 1.e-6f);
        const float vx = a.dimAngVertexX, vy = a.dimAngVertexY;
        auto resolveDimColor2 = [&](const std::string& styCol, float defR, float defG, float defB) -> ImU32 {
          if (styCol == "ByLayer" || styCol.empty()) {
            float rgba[4];
            if (attrPtr) ResolveEntityColorForViewport(*attrPtr, defR, defG, defB, rgba);
            else { rgba[0]=defR; rgba[1]=defG; rgba[2]=defB; rgba[3]=1.f; }
            return IM_COL32(int(rgba[0]*255), int(rgba[1]*255), int(rgba[2]*255), int(rgba[3]*255));
          } else {
            float rgba[4];
            ResolveStoredColorForViewport(styCol, 0.f, defR, defG, defB, rgba);
            return IM_COL32(int(rgba[0]*255), int(rgba[1]*255), int(rgba[2]*255), int(rgba[3]*255));
          }
        };
        const ImU32 lineCol2 = resolveDimColor2(cmd.activeDimensionStyle.dimLineColor, 225/255.f, 177/255.f, 44/255.f);
        const ImU32 extCol2 = resolveDimColor2(cmd.activeDimensionStyle.extLineColor, 225/255.f, 177/255.f, 44/255.f);
        const ImU32 textCol2 = resolveDimColor2(cmd.activeDimensionStyle.textColor, 248/255.f, 250/255.f, 252/255.f);
        const ImU32 arrowCol2 = resolveDimColor2(cmd.activeDimensionStyle.arrowColor, 225/255.f, 177/255.f, 44/255.f);
        const Camera dimCam2 = CadViewCamera(cmd);
        const float dimZ2 = a.insZ;
        auto ws2 = [&](float wx, float wy, ImVec2* o) {
          float sx=0.f,sy=0.f;
          dimCam2.WorldToScreen((double)wx,(double)wy,(double)dimZ2, avail.x, avail.y, &sx, &sy);
          o->x = imgPos.x + sx; o->y = imgPos.y + sy;
        };
        const float fontPx2 = std::clamp(hWorld / std::max(worldPerPxY, 1.e-6f), cmd.viewportDimTextMinPx, cmd.viewportDimTextMaxPx);
        const float extPx2 = std::clamp(cmd.viewportDimExtLinePx, 0.25f, 16.f);
        const float dimLnPx2 = std::clamp(cmd.viewportDimDimLinePx, 0.25f, 16.f);
        const float gapWorld = std::max(0.12f * hWorld, 0.015f * R);
        const float overWorld = std::max(0.08f * hWorld, 0.01f * R);
        const float ex1x0 = vx + std::cos(a1) * gapWorld;
        const float ex1y0 = vy + std::sin(a1) * gapWorld;
        const float ex1x1 = vx + std::cos(a1) * (R + overWorld);
        const float ex1y1 = vy + std::sin(a1) * (R + overWorld);
        const float ex2x0 = vx + std::cos(a2) * gapWorld;
        const float ex2y0 = vy + std::sin(a2) * gapWorld;
        const float ex2x1 = vx + std::cos(a2) * (R + overWorld);
        const float ex2y1 = vy + std::sin(a2) * (R + overWorld);
        ImVec2 A2{},B2{};
        ws2(ex1x0, ex1y0, &A2); ws2(ex1x1, ex1y1, &B2); dl->AddLine(A2,B2,extCol2,extPx2);
        ws2(ex2x0, ex2y0, &A2); ws2(ex2x1, ex2y1, &B2); dl->AddLine(A2,B2,extCol2,extPx2);
        const int segs = 32;
        const float styleArrowWorld2 = cmd.activeDimensionStyle.arrowSizeInches * cmd.modelUnitsPerPlottedInch;
        const float arcLen = R * std::fabs(sweep);
        const float arrowWorld = std::max(styleArrowWorld2 * cmd.viewportDimArrowScale * 0.10f, cmd.viewportDimArrowScale * 0.012f * arcLen);
        for (int i=0;i<segs;++i) {
          float aa = a1 + sweep * (float)i/segs;
          float ab = a1 + sweep * (float)(i+1)/segs;
          float mid = a1 + sweep*0.5f;
          float gapAng = (hWorld * 1.8f) / std::max(R, 1.e-6f);
          if (std::fabs(aa - mid) < gapAng*0.5f || std::fabs(ab - mid) < gapAng*0.5f) continue;
          if ((aa < mid && ab > mid) || (aa > mid && ab < mid)) continue;
          ImVec2 p0{},p1{};
          ws2(vx + std::cos(aa)*R, vy + std::sin(aa)*R, &p0);
          ws2(vx + std::cos(ab)*R, vy + std::sin(ab)*R, &p1);
          dl->AddLine(p0,p1,lineCol2,dimLnPx2);
        }
        // Arrows: match DIMLINEAR exactly - same world size, same screen conversion, both point inward toward arc center
        const float arrowLenWorld = arrowWorld; // already max(styleArrowWorld*0.10*scale, 0.012*arcLen*scale)
        const float maxArrowForArc = 0.47f * std::max(0.f, R * std::fabs(sweep) - 0.15f * arrowLenWorld);
        const float arrowLenUse = std::max(1.e-6f, std::min(arrowLenWorld, maxArrowForArc));
        auto drawArrowAng = [&](float ang, bool isStart) {
          // Outward arrows (pointing away from arc center, like AutoCAD outward)
          float baseAng = ang + (isStart ? (sweep>0 ? 0.14f : -0.14f) : (sweep>0 ? -0.14f : 0.14f));
          ImVec2 tip{}, base{};
          ws2(vx + std::cos(ang)*R, vy + std::sin(ang)*R, &tip);
          ws2(vx + std::cos(baseAng)*R, vy + std::sin(baseAng)*R, &base);
          float dx = tip.x - base.x, dy = tip.y - base.y;
          float len = std::hypot(dx,dy);
          if (len < 1.e-3f) return;
          dx/=len; dy/=len;
          float hwWorld = arrowLenUse * 0.48f;
          float oxW = -dy*hwWorld, oyW = dx*hwWorld;
          ImVec2 t1W{base.x + oxW, base.y + oyW}, t2W{base.x - oxW, base.y - oyW};
          ImVec2 t1{}, t2{};
          ws2(t1W.x, t1W.y, &t1); ws2(t2W.x, t2W.y, &t2);
          // Convert tip/base already in screen, but t1/t2 now also in screen via ws2, need to handle correctly
          // Instead compute screen directly like DIMLINEAR does: convert tip and base to screen, then compute hw in screen
          // For matching DIMLINEAR, use world hw then convert
        };
        // Actually match DIMLINEAR exactly: use world alenW/alenUse logic
        auto drawArrowCorrect = [&](float ang, bool isStart) {
          ImVec2 tipS{}, baseS{};
          ws2(vx + std::cos(ang)*R, vy + std::sin(ang)*R, &tipS);
          float baseAng2 = ang + (isStart ? (sweep>0 ? 0.12f : -0.12f) : (sweep>0 ? -0.12f : 0.12f));
          ws2(vx + std::cos(baseAng2)*R, vy + std::sin(baseAng2)*R, &baseS);
          float dxS = tipS.x - baseS.x, dyS = tipS.y - baseS.y;
          float lenS = std::hypot(dxS,dyS);
          if (lenS < 1.e-3f) return;
          dxS/=lenS; dyS/=lenS;
          // hw = alenUse*0.48, expressed in screen px: (alenUse / worldPerPxY) * 0.48
          float hwScreen = (arrowLenUse / std::max(worldPerPxY, 1.e-6f)) * 0.48f;
          float oxS = -dyS*hwScreen, oyS = dxS*hwScreen;
          ImVec2 tt1{baseS.x + oxS, baseS.y + oyS}, tt2{baseS.x - oxS, baseS.y - oyS};
          if (cmd.activeDimensionStyle.arrowType == DimArrowType::Tick) {
            float tickLenS = (arrowLenUse / std::max(worldPerPxY, 1.e-6f)) * 1.1f;
            dl->AddLine(ImVec2(tipS.x - dyS*tickLenS*0.5f, tipS.y + dxS*tickLenS*0.5f), ImVec2(tipS.x + dyS*tickLenS*0.5f, tipS.y - dxS*tickLenS*0.5f), arrowCol2, dimLnPx2);
          } else if (cmd.activeDimensionStyle.arrowType == DimArrowType::Dot) {
            dl->AddCircleFilled(tipS, hwScreen*0.9f, arrowCol2, 12);
          } else if (cmd.activeDimensionStyle.arrowType == DimArrowType::None) {
          } else {
            bool filled = (cmd.activeDimensionStyle.arrowType == DimArrowType::ClosedFilled);
            if (filled) dl->AddTriangleFilled(tipS, tt1, tt2, arrowCol2);
            else dl->AddTriangle(tipS, tt1, tt2, arrowCol2, dimLnPx2);
          }
        };
        drawArrowCorrect(a1, true);
        drawArrowCorrect(a2, false);
        ImVec2 sp2{}; ws2(a.insX, a.insY, &sp2);
        ImVec2 spDir2{}; float dirStep2 = std::max(1.e-4f, hWorld);
        ws2(a.insX + std::cos(a.rotationRad)*dirStep2, a.insY + std::sin(a.rotationRad)*dirStep2, &spDir2);
        float screenAng2 = std::atan2(spDir2.y - sp2.y, spDir2.x - sp2.x);
        DrawDimLabelText(dl, a, font, fontPx2, sp2, screenAng2, textCol2);
      } else {
        if (a.kind == CadAnnotation::Kind::Table && a.tableCols > 0) {
          ImVec2 sa{}, sb{};
          worldToScreen(a.boxMinX, a.boxMinY, &sa, a.insZ);
          worldToScreen(a.boxMaxX, a.boxMaxY, &sb, a.insZ);
          const float rx0 = std::min(sa.x, sb.x);
          const float ry0 = std::min(sa.y, sb.y);
          const float rx1 = std::max(sa.x, sb.x);
          const float ry1 = std::max(sa.y, sb.y);
          std::vector<CadTableCellRect> cells;
          CadTableLayoutCells(a.boxMinX, a.boxMinY, a.boxMaxX, a.boxMaxY, a.tableCols, a.tableCells, &cells);
          const ImU32 gridCol = IM_COL32(200, 200, 210, 255);
          dl->AddRect(ImVec2(rx0, ry0), ImVec2(rx1, ry1), gridCol, 0.f, 0, 1.5f);
          const int rows = CadTableRowCount(a.tableCols, a.tableCells);
          for (int c = 1; c < a.tableCols; ++c) {
            const float t = static_cast<float>(c) / static_cast<float>(a.tableCols);
            const float x = rx0 + t * (rx1 - rx0);
            dl->AddLine(ImVec2(x, ry0), ImVec2(x, ry1), gridCol, 1.f);
          }
          for (int r = 1; r < rows; ++r) {
            const float t = static_cast<float>(r) / static_cast<float>(std::max(rows, 1));
            const float y = ry0 + t * (ry1 - ry0);
            dl->AddLine(ImVec2(rx0, y), ImVec2(rx1, y), gridCol, 1.f);
          }
          const float fontPx = std::clamp(CadAnnotationHeightWorld(a, cmd.modelUnitsPerPlottedInch) /
                                              std::max(worldPerPxY, 1.e-6f),
                                          1.f, 8192.f);
          for (size_t i = 0; i < cells.size() && i < a.tableCells.size(); ++i) {
            ImVec2 tl{}, br{};
            worldToScreen(cells[i].x0, cells[i].y1, &tl, a.insZ);
            worldToScreen(cells[i].x1, cells[i].y0, &br, a.insZ);
            const float cx = std::min(tl.x, br.x) + 3.f;
            const float cy = std::min(tl.y, br.y) + 2.f;
            dl->AddText(font, fontPx, ImVec2(cx, cy), colFallback, a.tableCells[i].c_str());
          }
          return;
        }
        ImVec2 sa{}, sb{};
        worldToScreen(a.boxMinX, a.boxMinY, &sa, a.insZ);
        worldToScreen(a.boxMaxX, a.boxMaxY, &sb, a.insZ);
        const float rx0 = std::min(sa.x, sb.x);
        const float ry0 = std::min(sa.y, sb.y);
        const float rx1 = std::max(sa.x, sb.x);
        const float ry1 = std::max(sa.y, sb.y);
        // Survey-point labels keep the readability floor (stay legible at any zoom); plain MTEXT is
        // model-sized and scales with the drawing (no floor) so it stays proportional when zoomed out.
        const float mtextMinPx = (a.surveyPointLabelForId >= 0) ? cmd.viewportMtextMinPx : 1.f;
        // REQ-050: plain MTEXT is sized off the viewport's scale — the viewport being edited through (floating
        // model space) else the drawing scale — so its plotted height stays constant on the sheet regardless
        // of that viewport's scale. Survey labels keep the global drawing scale (their own layout owns size).
        float mtextMup = cmd.modelUnitsPerPlottedInch;
        if (const Viewport* mvp = CurrentViewport(cmd))
          mtextMup = MtextScaleThroughViewport(a, *mvp, cmd.modelUnitsPerPlottedInch);
        const float hWorldMtext = CadAnnotationHeightWorld(a, mtextMup);
        // The screen-size cap belongs to survey-point labels only: those are sized for legibility, not to
        // scale. Applying it to plain MTEXT made the text stop growing once zoomed past ~128 px while the
        // geometry around it kept scaling — the text is a model-space object and must scale with the view
        // (REQ-050). The remaining ceiling is only a rasterisation sanity bound.
        const float mtextMaxPx = (a.surveyPointLabelForId >= 0) ? cmd.viewportMtextMaxPx : 8192.f;
        const float fontPx =
            std::clamp(hWorldMtext / std::max(worldPerPxY, 1.e-6f), mtextMinPx, mtextMaxPx);
        const std::string mtextFam = CadDrawFontFamily(a.fontFamily);
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
        if (a.surveyPointLabelForId >= 0) {
          float pw = 8.f;
          float ph = fontPx * 1.22f;
          MtextRichNaturalContentPx(font, fontPx, a.text, &pw, &ph, mtextFam);
          drawX = rx0 + 0.5f * ((rx1 - rx0) - pw);
          drawY = ry0 + 0.5f * ((ry1 - ry0) - ph);
          wrapW = std::max(pw, 8.f);

          // Draw leader line from label to point when label is manually offset far enough.
          const int leaderPi =
              a.surveyLabelHasUserOffset ? SurveyPointIndexForId(cmd, a.surveyPointLabelForId) : -1;
          if (leaderPi >= 0) {
            const SurveyPoint& lsp = cmd.surveyPoints[static_cast<size_t>(leaderPi)];
            const float lcx = 0.5f * (a.boxMinX + a.boxMaxX);
            const float lcy = 0.5f * (a.boxMinY + a.boxMaxY);
            const float bwHalf = 0.5f * std::fabs(a.boxMaxX - a.boxMinX);
            const float bhHalf = 0.5f * std::fabs(a.boxMaxY - a.boxMinY);
            const float halfDiag = std::hypot(bwHalf, bhHalf);
            const float distToPoint = std::hypot(lsp.easting - lcx, lsp.northing - lcy);
            if (distToPoint > halfDiag * 1.1f) {
              ImVec2 ptScreen{};
              worldToScreen(lsp.easting, lsp.northing, &ptScreen, lsp.elevation);
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
        } else if (a.mtextAttach != 1) {
          // Honor MTEXT attachment (group 71): col 0/1/2 = left/center/right, row 0/1/2 = top/middle/bottom.
          const int acol = (a.mtextAttach - 1) % 3;
          const int arow = (a.mtextAttach - 1) / 3;
          float pw = 8.f, ph = fontPx * 1.22f;
          MtextRichNaturalContentPx(font, fontPx, a.text, &pw, &ph, mtextFam);
          // Anchor to the box, but never clamp back inside it: content taller or wider than the box
          // must overhang rather than be shoved in (and then clipped away).
          if (acol == 1)      drawX = rx0 + 0.5f * ((rx1 - rx0) - pw);
          else if (acol == 2) drawX = rx1 - pw - 4.f;
          if (arow == 1)      drawY = ry0 + 0.5f * ((ry1 - ry0) - ph);
          else if (arow == 2) drawY = ry1 - ph - 4.f;
          if (acol != 0) wrapW = std::max(pw, 8.f);
        }
        const bool rotateMtext = a.surveyPointLabelForId < 0 && std::fabs(a.rotationRad) > 1.e-5f;
        ImVec2 mtextPivot{rx0, ry0};
        if (rotateMtext) {
          worldToScreen(a.insX, a.insY, &mtextPivot, a.insZ);
          drawX = mtextPivot.x;
          drawY = mtextPivot.y;
          wrapW = 1.e8f;
        }
        dl->PushClipRect(imgPos, ImVec2(imgPos.x + avail.x, imgPos.y + avail.y), true);
        const int mtextVtx0 = dl->VtxBuffer.Size;
        Shx::Font* sfm = CadIsShxFontName(mtextFam) ? Shx::Resolve(mtextFam) : nullptr;
        if (sfm && sfm->valid()) {
          // Render MTEXT as SHX strokes (exact AutoCAD match), line by line, honoring the attachment.
          const std::string plain = MtextRichFlattenToPlain(a.text);
          const bool underline = a.text.find("[[u]]") != std::string::npos;
          const int acol = (a.mtextAttach - 1) % 3;
          const float lineH = fontPx * 1.4f;
          const float thick = std::max(1.f, fontPx * 0.05f);
          std::vector<std::string> lines;
          {
            std::string ln;
            for (char ch : plain) {
              if (ch == '\n') { lines.push_back(ln); ln.clear(); }
              else ln += ch;
            }
            lines.push_back(ln);
          }
          float ly = drawY;
          const float left0 = rotateMtext ? mtextPivot.x : rx0;
          for (const std::string& ln : lines) {
            const float w = Shx::MeasureWidthPx(*sfm, ln, fontPx);
            float lx = left0 + 4.f;
            if (!rotateMtext) {
              if (acol == 1)      lx = rx0 + 0.5f * ((rx1 - rx0) - w);
              else if (acol == 2) lx = std::max(rx0 + 4.f, rx1 - w - 4.f);
            }
            const ImVec2 base(lx, ly + fontPx);
            Shx::DrawText(dl, *sfm, base, fontPx, 0.f, col, ln, thick);
            if (underline) {
              const float uy = base.y + std::max(1.5f, fontPx * 0.12f);
              dl->AddLine(ImVec2(lx, uy), ImVec2(lx + w, uy), col, thick);
            }
            ly += lineH;
          }
        } else {
          MtextRichDrawWrapped(dl, font, fontPx, ImVec2(drawX, drawY), wrapW, col, a.text, mtextFam);
        }
        if (rotateMtext)
          RotateDrawListVertsAround(dl, mtextVtx0, mtextPivot, a.rotationRad);
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
    auto isTableSelected = [&](size_t ix) {
      for (const auto& e : cmd.selection) {
        if (e.type == SelectedEntity::Type::Table && static_cast<size_t>(e.index) == ix)
          return true;
      }
      return false;
    };
    auto drawCadTableVisual = [&](const CadTable& t, const EntityAttributes* attrPtr, ImU32 colFallback) {
      ImU32 col = colFallback;
      if (attrPtr) {
        float rgba[4];
        ResolveEntityColorForViewport(*attrPtr, 230 / 255.f, 232 / 255.f, 238 / 255.f, rgba);
        col = IM_COL32(static_cast<int>(rgba[0] * 255.f), static_cast<int>(rgba[1] * 255.f),
                       static_cast<int>(rgba[2] * 255.f), static_cast<int>(rgba[3] * 255.f));
      }
      ImVec2 c[4];
      for (int i = 0; i < 4; ++i) {
        float wx = 0.f, wy = 0.f;
        CadTableWorldCorner(t, i, &wx, &wy);
        worldToScreen(wx, wy, &c[i], t.insZ);
      }
      dl->AddLine(c[0], c[1], col, 1.5f);
      dl->AddLine(c[1], c[2], col, 1.5f);
      dl->AddLine(c[2], c[3], col, 1.5f);
      dl->AddLine(c[3], c[0], col, 1.5f);
      const int rows = CadTableRowCount(t);
      if (t.cols > 0 && rows > 0) {
        const float w = std::max(t.width, 1.e-3f);
        const float h = std::max(t.height, 1.e-3f);
        for (int ci = 1; ci < t.cols; ++ci) {
          const float lx = w * static_cast<float>(ci) / static_cast<float>(t.cols);
          float ax = 0.f, ay = 0.f, bx = 0.f, by = 0.f;
          CadTableLocalToWorld(t, lx, 0.f, &ax, &ay);
          CadTableLocalToWorld(t, lx, h, &bx, &by);
          ImVec2 sa{}, sb{};
          worldToScreen(ax, ay, &sa, t.insZ);
          worldToScreen(bx, by, &sb, t.insZ);
          dl->AddLine(sa, sb, col, 1.f);
        }
        for (int r = 1; r < rows; ++r) {
          const float ly = h * static_cast<float>(r) / static_cast<float>(rows);
          float ax = 0.f, ay = 0.f, bx = 0.f, by = 0.f;
          CadTableLocalToWorld(t, 0.f, ly, &ax, &ay);
          CadTableLocalToWorld(t, w, ly, &bx, &by);
          ImVec2 sa{}, sb{};
          worldToScreen(ax, ay, &sa, t.insZ);
          worldToScreen(bx, by, &sb, t.insZ);
          dl->AddLine(sa, sb, col, 1.f);
        }
      }
      std::vector<CadTableCellRect> cells;
      CadTableLayoutWorldCells(t, &cells);
      const float fontPx =
          std::clamp(CadTableHeightWorld(t, cmd.modelUnitsPerPlottedInch) / std::max(worldPerPxY, 1.e-6f), 1.f,
                     8192.f);
      for (size_t i = 0; i < cells.size() && i < t.cells.size(); ++i) {
        ImVec2 tl{};
        worldToScreen(cells[i].x0, cells[i].y1, &tl, t.insZ);
        dl->AddText(font, fontPx, ImVec2(tl.x + 3.f, tl.y + 2.f), col, t.cells[i].c_str());
      }
    };

    for (size_t ai = 0; ai < cmd.cadAnnotations.size(); ++ai) {
      const EntityAttributes* ap =
          ai < cmd.cadAnnotationAttrs.size() ? &cmd.cadAnnotationAttrs[ai] : nullptr;
      // Annotations are drawn by this overlay rather than the GL pass, so isolation is gated here
      // (REQ-084 (d)); PickCadAnnotationAt gates the matching pick.
      if (ap && CadEntityIdHidden(&cmd.hiddenEntityIds, ap->id))
        continue;
      drawAnnotationVisual(cmd.cadAnnotations[ai], ap, kAnnCol);
    }
    for (size_t bi = 0; bi < cmd.cadBlockRefs.size(); ++bi) {
      const EntityAttributes* bp =
          bi < cmd.cadBlockRefAttrs.size() ? &cmd.cadBlockRefAttrs[bi] : nullptr;
      if (bp && CadEntityIdHidden(&cmd.hiddenEntityIds, bp->id))
        continue;
      std::vector<CadAnnotation> blockAnns;
      CadBlockCollectWorldAnnotations(cmd.blockDefs, cmd.cadBlockRefs[bi], &blockAnns);
      for (const CadAnnotation& a : blockAnns)
        drawAnnotationVisual(a, bp, kAnnCol);
    }

    for (const CadAnnotation& ap : transformAnnPreviews)
      drawAnnotationVisual(ap, nullptr, kAnnTfPrevCol);

    for (size_t ti = 0; ti < cmd.cadTables.size(); ++ti) {
      const EntityAttributes* tp = ti < cmd.cadTableAttrs.size() ? &cmd.cadTableAttrs[ti] : nullptr;
      if (tp && CadEntityIdHidden(&cmd.hiddenEntityIds, tp->id))
        continue;
      drawCadTableVisual(cmd.cadTables[ti], tp, kAnnCol);
    }
    for (const CadTable& tp : transformTablePreviews)
      drawCadTableVisual(tp, nullptr, kAnnTfPrevCol);

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
      bool ok = false;
      if (cmd.active == AK::DimLinear) ok = CadDimLinearBuildDraft(cmd, *outCursorX, *outCursorY, &d);
      else if (cmd.active == AK::DimAngular) ok = CadDimAngularBuildDraft(cmd, *outCursorX, *outCursorY, &d);
      else ok = CadDimAlignedBuildDraft(cmd, *outCursorX, *outCursorY, &d);
      if (ok) {
        drawAnnotationVisual(d, nullptr, kAnnTfPrevCol);
      }
    }
    // DimAngular early-phase preview - always show ray preview following mouse, even when not in WaitArc
    if (cmd.active == AK::DimAngular && outCursorX && outCursorY) {
      const float vx = cmd.dimAngVx, vy = cmd.dimAngVy;
      ImVec2 vScreen{}, cScreen{};
      worldToScreen(vx, vy, &vScreen, cmd.anchorZ);
      worldToScreen(*outCursorX, *outCursorY, &cScreen, cmd.anchorZ);
      if (cmd.dimAngularPhase == AppCommandState::DimAngularPhase::WaitRay1) {
        dl->AddLine(vScreen, cScreen, kAnnTfPrevCol, 1.2f);
      } else if (cmd.dimAngularPhase == AppCommandState::DimAngularPhase::WaitRay2) {
        ImVec2 r1Screen{};
        worldToScreen(cmd.dimE1x, cmd.dimE1y, &r1Screen, cmd.anchorZ);
        dl->AddLine(vScreen, r1Screen, kAnnTfPrevCol, 1.2f);
        dl->AddLine(vScreen, cScreen, kAnnTfPrevCol, 1.2f);
      }
    }

    const float gripHalf = cmd.gripSizePx;
    for (size_t ai = 0; ai < cmd.cadAnnotations.size(); ++ai) {
      const CadAnnotation& a = cmd.cadAnnotations[ai];
      if ((a.kind != CadAnnotation::Kind::Mtext && a.kind != CadAnnotation::Kind::Table) ||
          !isAnnSelected(ai))
        continue;
      // Survey-linked labels: grips only (no selection rectangle).
      if (a.surveyPointLabelForId < 0) {
        ImVec2 sa{}, sb{};
        worldToScreen(a.boxMinX, a.boxMinY, &sa, a.insZ);
        worldToScreen(a.boxMaxX, a.boxMaxY, &sb, a.insZ);
        const float rx0 = std::min(sa.x, sb.x);
        const float ry0 = std::min(sa.y, sb.y);
        const float rx1 = std::max(sa.x, sb.x);
        const float ry1 = std::max(sa.y, sb.y);
        dl->AddRect(ImVec2(rx0, ry0), ImVec2(rx1, ry1), kAnnSelCol, 0.f, 0, 2.f);
      }
      if (a.surveyPointLabelForId >= 0) {
        const float cx = 0.5f * (a.boxMinX + a.boxMaxX);
        const float cy = 0.5f * (a.boxMinY + a.boxMaxY);
        ImVec2 gp{};
        worldToScreen(cx, cy, &gp, a.insZ);  // the label's own elevation, as its selection rect uses
        dl->AddRectFilled(ImVec2(gp.x - gripHalf, gp.y - gripHalf), ImVec2(gp.x + gripHalf, gp.y + gripHalf),
                          kGripFill);
        dl->AddRect(ImVec2(gp.x - gripHalf, gp.y - gripHalf), ImVec2(gp.x + gripHalf, gp.y + gripHalf), kGripBorder,
                    0.f, 0, 1.f);
      } else {
        const float wx[4] = {a.boxMinX, a.boxMaxX, a.boxMaxX, a.boxMinX};
        const float wy[4] = {a.boxMinY, a.boxMinY, a.boxMaxY, a.boxMaxY};
        for (int c = 0; c < 4; ++c) {
          ImVec2 gp{};
          worldToScreen(wx[c], wy[c], &gp, a.insZ);
          dl->AddRectFilled(ImVec2(gp.x - gripHalf, gp.y - gripHalf), ImVec2(gp.x + gripHalf, gp.y + gripHalf),
                            kGripFill);
          dl->AddRect(ImVec2(gp.x - gripHalf, gp.y - gripHalf), ImVec2(gp.x + gripHalf, gp.y + gripHalf), kGripBorder,
                      0.f, 0, 1.f);
        }
      }
      dl->PopClipRect();
    }

    for (size_t ti = 0; ti < cmd.cadTables.size(); ++ti) {
      if (!isTableSelected(ti))
        continue;
      const CadTable& t = cmd.cadTables[ti];
      ImVec2 c[4];
      for (int i = 0; i < 4; ++i) {
        float wx = 0.f, wy = 0.f;
        CadTableWorldCorner(t, i, &wx, &wy);
        worldToScreen(wx, wy, &c[i], t.insZ);
      }
      dl->AddPolyline(c, 4, kAnnSelCol, ImDrawFlags_Closed, 2.f);
      for (int i = 0; i < 4; ++i) {
        dl->AddRectFilled(ImVec2(c[i].x - gripHalf, c[i].y - gripHalf),
                          ImVec2(c[i].x + gripHalf, c[i].y + gripHalf), kGripFill);
        dl->AddRect(ImVec2(c[i].x - gripHalf, c[i].y - gripHalf), ImVec2(c[i].x + gripHalf, c[i].y + gripHalf),
                    kGripBorder, 0.f, 0, 1.f);
      }
    }

    // Screen rect that hugs a rendered annotation glyph. For single-line TEXT we mirror the renderer's
    // anchor exactly (top-left = worldToScreen(insX,insY), height = the same clamped fontPx, width = the
    // measured glyph width) so the highlight tracks the visible glyph regardless of rough-bounds estimates.
    // For MTEXT the box (boxMin/boxMax) already bounds the wrapped text.
    auto annScreenRect = [&](const CadAnnotation& a, ImVec2* rmin, ImVec2* rmax) {
      if (CadAnnotationHasTextBox(a.kind)) {
        ImVec2 sa{}, sb{};
        worldToScreen(a.boxMinX, a.boxMinY, &sa, a.insZ);
        worldToScreen(a.boxMaxX, a.boxMaxY, &sb, a.insZ);
        rmin->x = std::min(sa.x, sb.x);
        rmin->y = std::min(sa.y, sb.y);
        rmax->x = std::max(sa.x, sb.x);
        rmax->y = std::max(sa.y, sb.y);
        return;
      }
      const float hW = CadAnnotationHeightWorld(a, cmd.modelUnitsPerPlottedInch);
      const float fpx = std::clamp(hW / std::max(worldPerPxY, 1.e-6f), 1.f, cmd.viewportTextMaxPx);
      ImVec2 sp{};
      worldToScreen(a.insX, a.insY, &sp, a.insZ);
      float tw = 0.f;
      const std::string hitFam = CadDrawFontFamily(a.fontFamily);
      Shx::Font* sf = CadIsShxFontName(hitFam) ? Shx::Resolve(hitFam) : nullptr;
      if (sf && sf->valid()) {
        tw = Shx::MeasureWidthPx(*sf, a.text, fpx);
      } else {
        bool rb = false, ri = false;
        ImFont* tf = FontReg::Resolve(hitFam, a.bold, a.italic, &rb, &ri);
        if (!tf) tf = font;
        tw = tf->CalcTextSizeA(fpx, FLT_MAX, 0.f, a.text.c_str()).x;
      }
      rmin->x = sp.x;
      rmin->y = sp.y;
      rmax->x = sp.x + std::max(tw, 4.f);
      rmax->y = sp.y + fpx;
    };

    // Four screen corners that hug a single-line TEXT glyph, rotated about its insertion point exactly as
    // the glyph is drawn (top-left pivot, screen angle −rotationRad) so the box follows rotated text.
    auto annTextCorners = [&](const CadAnnotation& a, float padPx, ImVec2 c[4]) {
      const float hW = CadAnnotationHeightWorld(a, cmd.modelUnitsPerPlottedInch);
      const float fpx = std::clamp(hW / std::max(worldPerPxY, 1.e-6f), 1.f, cmd.viewportTextMaxPx);
      ImVec2 sp{};
      worldToScreen(a.insX, a.insY, &sp, a.insZ);
      float tw = 0.f;
      const std::string hitFam = CadDrawFontFamily(a.fontFamily);
      Shx::Font* sf = CadIsShxFontName(hitFam) ? Shx::Resolve(hitFam) : nullptr;
      if (sf && sf->valid()) {
        tw = Shx::MeasureWidthPx(*sf, a.text, fpx);
      } else {
        bool rb = false, ri = false;
        ImFont* tf = FontReg::Resolve(hitFam, a.bold, a.italic, &rb, &ri);
        if (!tf) tf = font;
        tw = tf->CalcTextSizeA(fpx, FLT_MAX, 0.f, a.text.c_str()).x;
      }
      tw = std::max(tw, 4.f);
      const float ang = -a.rotationRad;
      const float ca = std::cos(ang), sa = std::sin(ang);
      auto rot = [&](float lx, float ly) {
        return ImVec2(sp.x + lx * ca - ly * sa, sp.y + lx * sa + ly * ca);
      };
      c[0] = rot(-padPx, -padPx);
      c[1] = rot(tw + padPx, -padPx);
      c[2] = rot(tw + padPx, fpx + padPx);
      c[3] = rot(-padPx, fpx + padPx);
    };

    // Selected single-line TEXT: a selection box around the glyph so the selection is visible (parity
    // with MTEXT above). Grips/grip-drag for single-line TEXT are deferred to the grip phase.
    for (size_t ai = 0; ai < cmd.cadAnnotations.size(); ++ai) {
      const CadAnnotation& a = cmd.cadAnnotations[ai];
      if (a.kind != CadAnnotation::Kind::Text || !isAnnSelected(ai))
        continue;
      ImVec2 c[4];
      annTextCorners(a, 1.f, c);
      dl->AddPolyline(c, 4, kAnnSelCol, ImDrawFlags_Closed, 2.f);
    }

    // Hover pre-highlight for text annotations (idle, not selected) — mirrors the paper-space hover (REQ-039).
    if (cmd.viewportHoverEntityValid &&
        cmd.viewportHoverEntity.type == SelectedEntity::Type::Annotation) {
      const int hi = cmd.viewportHoverEntity.index;
      if (hi >= 0 && static_cast<size_t>(hi) < cmd.cadAnnotations.size() &&
          !isAnnSelected(static_cast<size_t>(hi))) {
        const CadAnnotation& ha = cmd.cadAnnotations[static_cast<size_t>(hi)];
        if (ha.kind == CadAnnotation::Kind::Text) {
          ImVec2 c[4];
          annTextCorners(ha, 2.f, c);
          dl->AddPolyline(c, 4, IM_COL32(130, 180, 240, 200), ImDrawFlags_Closed, 1.4f);
        } else if (ha.kind == CadAnnotation::Kind::Mtext) {
          ImVec2 rmin{}, rmax{};
          annScreenRect(ha, &rmin, &rmax);
          dl->AddRect(ImVec2(rmin.x - 2.f, rmin.y - 2.f), ImVec2(rmax.x + 2.f, rmax.y + 2.f),
                      IM_COL32(130, 180, 240, 200), 0.f, 0, 1.4f);
        }
      }
    }
    if (cmd.viewportHoverEntityValid && cmd.viewportHoverEntity.type == SelectedEntity::Type::Table) {
      const int hi = cmd.viewportHoverEntity.index;
      if (hi >= 0 && static_cast<size_t>(hi) < cmd.cadTables.size() && !isTableSelected(static_cast<size_t>(hi))) {
        const CadTable& ht = cmd.cadTables[static_cast<size_t>(hi)];
        ImVec2 c[4];
        for (int i = 0; i < 4; ++i) {
          float wx = 0.f, wy = 0.f;
          CadTableWorldCorner(ht, i, &wx, &wy);
          worldToScreen(wx, wy, &c[i], ht.insZ);
        }
        dl->AddPolyline(c, 4, IM_COL32(130, 180, 240, 200), ImDrawFlags_Closed, 1.4f);
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
        worldToScreen(wx[c], wy[c], &gp, a.insZ);  // the dimension's plane (REQ-058)
        dl->AddRectFilled(ImVec2(gp.x - gripHalf, gp.y - gripHalf), ImVec2(gp.x + gripHalf, gp.y + gripHalf),
                          kGripFill);
        dl->AddRect(ImVec2(gp.x - gripHalf, gp.y - gripHalf), ImVec2(gp.x + gripHalf, gp.y + gripHalf), kGripBorder,
                    0.f, 0, 1.f);
      }
    }
  }

  // --- Survey point marker highlights: hover and selection (REQ-058) -----------------------------
  // Both highlight the point's OWN X rather than ringing it: a ring says "something is here" where a
  // coloured X says "this one".
  //
  // A selected survey point previously showed nothing at all. Its linked label DID highlight — the
  // label is a CadAnnotation and so lives in `cmd.selection`, which `BuildSelectionHighlight` walks
  // — but survey points live in `selectedSurveyPointIndices`, which that function never sees. So the
  // label lit up and the point it belongs to did not.
  //
  // Drawn here rather than added to BuildSelectionHighlight because the X is **billboarded**: its
  // shape depends on the camera, and that function takes only the command state. This is the same
  // reason the markers themselves are built with a basis in main.cpp.
  if (!cmd.surveyPoints.empty()) {
    const Camera hlCam = CadViewCamera(cmd);
    const ray3d::Vec3 hr = hlCam.RightWorld();
    const ray3d::Vec3 hu = hlCam.UpWorld();
    MarkerBillboardBasis hlBasis;
    hlBasis.rightX = static_cast<float>(hr.x);
    hlBasis.rightY = static_cast<float>(hr.y);
    hlBasis.rightZ = static_cast<float>(hr.z);
    hlBasis.upX = static_cast<float>(hu.x);
    hlBasis.upY = static_cast<float>(hu.y);
    hlBasis.upZ = static_cast<float>(hu.z);

    const float armHl =
        SurveyPointCrossHalfWorldFromPaper(cmd.surveyPointCrossSpanPlottedInches, cmd.modelUnitsPerPlottedInch);
    ImDrawList* dlMk = ImGui::GetWindowDrawList();
    std::vector<float> mkCross;  // reused across points: 4 vertices, two segments forming the X

    // The highlight geometry comes from AppendSurveyPointCrossVertices — the very function that
    // builds the drawn marker — in the same camera basis, so highlight and marker cannot drift apart
    // in size or angle.
    const auto highlightMarker = [&](const SurveyPoint& sp, ImU32 col, float thick) {
      mkCross.clear();
      AppendSurveyPointCrossVertices(sp.easting, sp.northing, sp.elevation, armHl, &mkCross, hlBasis);
      if (mkCross.size() != 12)
        return;
      ImVec2 q[4];
      for (int v = 0; v < 4; ++v) {
        float sx = 0.f, sy = 0.f;
        hlCam.WorldToScreen(static_cast<double>(mkCross[static_cast<size_t>(v) * 3 + 0]),
                            static_cast<double>(mkCross[static_cast<size_t>(v) * 3 + 1]),
                            static_cast<double>(mkCross[static_cast<size_t>(v) * 3 + 2]), avail.x, avail.y, &sx,
                            &sy);
        q[v] = ImVec2(imgPos.x + sx, imgPos.y + sy);
      }
      dlMk->AddLine(q[0], q[1], col, thick);
      dlMk->AddLine(q[2], q[3], col, thick);
    };

    // Yellow matches the selection highlight every other entity type uses (the renderer's
    // highlightLines colour, 1.00/0.92/0.15); blue matches hover. Reusing those exact colours is
    // what makes a selected point read as "selected" without the user learning a second language.
    constexpr ImU32 kMarkerSelCol = IM_COL32(255, 235, 38, 255);
    constexpr ImU32 kMarkerHovCol = IM_COL32(100, 215, 255, 255);

    const int hovIx = cmd.viewportHoverSurveyPointIndex;
    const bool hovValid = hovIx >= 0 && static_cast<size_t>(hovIx) < cmd.surveyPoints.size();
    const bool hovAlsoSelected =
        hovValid && std::find(cmd.selectedSurveyPointIndices.begin(), cmd.selectedSurveyPointIndices.end(),
                              hovIx) != cmd.selectedSurveyPointIndices.end();
    // Selection takes visual precedence over hover — the same rule BuildHoverHighlight applies to
    // entities, so a selected point does not flicker to hover blue when the cursor crosses it.
    if (hovValid && !hovAlsoSelected)
      highlightMarker(cmd.surveyPoints[static_cast<size_t>(hovIx)], kMarkerHovCol, 2.6f);

    for (int spi : cmd.selectedSurveyPointIndices)
      if (spi >= 0 && static_cast<size_t>(spi) < cmd.surveyPoints.size())
        highlightMarker(cmd.surveyPoints[static_cast<size_t>(spi)], kMarkerSelCol, 2.8f);
  }

  // --- Box selection, screen-aligned (REQ-058) ---------------------------------------------------
  // Drawn here as a screen-space rectangle rather than in GL as world geometry on the XY plane.
  // The old world-space rectangle projected to a parallelogram lying on the datum once the view was
  // orbited, which was not merely ugly: the hit test projects the TWO drag corners and forms an
  // axis-aligned SCREEN rectangle from them, so the drawn box and the region that actually selects
  // were different shapes. Building it from the same two projected corners the hit test uses makes
  // the box show exactly what it will select, in every orientation.
  //
  // Z = 0 for both corners, matching the hit test's own `SP(xa, ya, 0.f, ...)`: the drag happens on
  // the work plane, and using anything else here would re-introduce the same disagreement.
  //
  // Colours are carried over from the removed GL stage unchanged, so a plan-view drag — the default
  // view and the common case — looks exactly as it did before.
  if (modelSpace && cmd.selBoxWaitingSecond) {
    const Camera selCam = CadViewCamera(cmd);
    float ax = 0.f, ay = 0.f, bx = 0.f, by = 0.f;
    // Each corner at its OWN elevation — the anchor's captured work-plane Z, and the live cursor's.
    // Z = 0 for both is invisible in plan view (Z does not move a plan projection) and on the world
    // XY plane at elevation zero, but as soon as the work plane is TILTED by a UCS or raised by
    // ELEV, it draws the box at pixels the mouse never visited. The same two values go to
    // `ComputeSelectionFromRect`, so the box still shows exactly what it will select — which is the
    // property this block was written to guarantee and which Z = 0 was quietly breaking.
    selCam.WorldToScreen(static_cast<double>(cmd.selBoxAnchorX), static_cast<double>(cmd.selBoxAnchorY),
                         static_cast<double>(cmd.selBoxAnchorZ), avail.x, avail.y, &ax, &ay);
    selCam.WorldToScreen(rawX, rawY, static_cast<double>(cmd.uiCursorWorldZ), avail.x, avail.y, &bx, &by);
    const ImVec2 mnSel(imgPos.x + std::min(ax, bx), imgPos.y + std::min(ay, by));
    const ImVec2 mxSel(imgPos.x + std::max(ax, bx), imgPos.y + std::max(ay, by));
    ImDrawList* dlSel = ImGui::GetWindowDrawList();
    dlSel->AddRectFilled(mnSel, mxSel, IM_COL32(64, 140, 255, 56));
    dlSel->AddRect(mnSel, mxSel, IM_COL32(115, 199, 255, 230), 0.f, 0, 1.5f);
  }

  if (modelAnnotationsVisible && !cmd.surveyPoints.empty() && cmd.surveyPointShowIdInViewport) {
    // Survey points are model-only (ADR-009); their IDs use model coordinates, so only label them on the
    // model canvas — never paint them onto a paper sheet at local-coord positions (REQ-038 artifact fix).
    ImDrawList* dlS = ImGui::GetWindowDrawList();
    const float worldPerPxYL = (worldTop - worldBottom) / std::max(avail.y, 1.f);
    const float hWorldL =
        cmd.surveyPointLabelPlottedHeightInches * std::max(cmd.modelUnitsPerPlottedInch, 1.e-6f);
    const float fontPxL =
        std::clamp(hWorldL / std::max(worldPerPxYL, 1.e-6f), cmd.viewportTextMinPx, cmd.viewportTextMaxPx);
    ImFont* fontL = ImGui::GetFont();
    constexpr ImU32 kPtIdCol = IM_COL32(255, 248, 200, 255);
    // Camera-projected (REQ-058): reduces to the previous mapping in plan view, and keeps this
    // overlay on the geometry once the view is orbited.
    const Camera ovCamA = CadViewCamera(cmd);
    auto wts = [&](float wx, float wy, float wz, ImVec2* o) {
      float sx = 0.f, sy = 0.f;
      ovCamA.WorldToScreen(static_cast<double>(wx), static_cast<double>(wy), static_cast<double>(wz), avail.x, avail.y,
                           &sx, &sy);
      o->x = imgPos.x + sx;
      o->y = imgPos.y + sy;
    };
    for (const auto& p : cmd.surveyPoints) {
      ImVec2 sp{};
      wts(p.easting, p.northing, p.elevation, &sp);  // elevation IS the point's Z (REQ-057)
      char idb[32];
      std::snprintf(idb, sizeof(idb), "%d", p.id);
      dlS->AddText(fontL, fontPxL, ImVec2(sp.x + 6.f, sp.y - fontPxL * 0.35f), kPtIdCol, idb);
    }
  }

  // --- CAD ENTITY GRIPS (viewport direct edit) ---
  // Model space only: this maps via the model view window (worldLeft..worldRight). In floating model space
  // the grips are drawn by the per-viewport overlay through the viewport transform; running this there too
  // would place stray grips at the wrong screen positions (REQ-036).
  if (modelSpace && !cmd.selection.empty()) {
    ImDrawList* dlG = ImGui::GetWindowDrawList();
    const float gripHalf = cmd.gripSizePx;
    constexpr ImU32 kGripFillE = IM_COL32(59, 130, 246, 255);
    constexpr ImU32 kGripBorderE = IM_COL32(30, 64, 175, 255);
    // Grip squares must land ON the geometry they belong to, so this projects through the camera
    // (REQ-058). Drawn with the plan mapping they sat off the object once the view was orbited —
    // and the grip HIT test already projects, so the two would also have disagreed.
    const Camera ovCamB = CadViewCamera(cmd);
    auto wts = [&](float wx, float wy, ImVec2* o, float wz = 0.f) {
      float sx = 0.f, sy = 0.f;
      ovCamB.WorldToScreen(static_cast<double>(wx), static_cast<double>(wy), static_cast<double>(wz), avail.x,
                           avail.y, &sx, &sy);
      o->x = imgPos.x + sx;
      o->y = imgPos.y + sy;
    };

    auto drawGrip = [&](float wx, float wy, float wz = 0.f) {
      ImVec2 gp{};
      wts(wx, wy, &gp, wz);
      dlG->AddRectFilled(ImVec2(gp.x - gripHalf, gp.y - gripHalf), ImVec2(gp.x + gripHalf, gp.y + gripHalf),
                          kGripFillE);
      dlG->AddRect(ImVec2(gp.x - gripHalf, gp.y - gripHalf), ImVec2(gp.x + gripHalf, gp.y + gripHalf),
                   kGripBorderE, 0.f, 0, 1.f);
    };

    for (const SelectedEntity& sel : cmd.selection) {
      if (sel.type == SelectedEntity::Type::LineSeg) {
        const size_t k = static_cast<size_t>(sel.index) * 6;
        if (k + 5 < cmd.userLinesFlat.size()) {
          // Each endpoint carries its own Z, so a sloped line's grips sit on its actual ends.
          drawGrip(cmd.userLinesFlat[k], cmd.userLinesFlat[k + 1], cmd.userLinesFlat[k + 2]);
          drawGrip(cmd.userLinesFlat[k + 3], cmd.userLinesFlat[k + 4], cmd.userLinesFlat[k + 5]);
        }
      } else if (sel.type == SelectedEntity::Type::Circle) {
        const size_t k = static_cast<size_t>(sel.index) * 4;
        if (k + 3 < cmd.userCirclesCxCyZR.size()) {
          const float cx = cmd.userCirclesCxCyZR[k];
          const float cy = cmd.userCirclesCxCyZR[k + 1];
          const float cz = cmd.userCirclesCxCyZR[k + 2];
          const float r = cmd.userCirclesCxCyZR[k + 3];
          drawGrip(cx, cy, cz);
          drawGrip(cx + r, cy, cz);
        }
      } else if (sel.type == SelectedEntity::Type::Polyline) {
        const int np = cmd.userPolylineOffsets.size() > 0 ? static_cast<int>(cmd.userPolylineOffsets.size() - 1) : 0;
        if (sel.index >= 0 && sel.index < np) {
          const int startV = cmd.userPolylineOffsets[static_cast<size_t>(sel.index)];
          const int endV = cmd.userPolylineOffsets[static_cast<size_t>(sel.index + 1)];
          for (int vi = 0; vi < endV - startV; ++vi) {
            const size_t xIdx = static_cast<size_t>(startV + vi) * 3;
            if (xIdx + 2 >= cmd.userPolylineVerts.size())
              break;
            // Each vertex carries its own Z, so a polyline up a slope keeps its grips on it.
            drawGrip(cmd.userPolylineVerts[xIdx], cmd.userPolylineVerts[xIdx + 1],
                     cmd.userPolylineVerts[xIdx + 2]);
          }
          // REQ-316 / ADR-047: a midpoint grip on every ARC segment — dragging it changes the bulge.
          CadForEachPolylineArcMidGrip(cmd, sel.index, [&](int, float mx, float my, float mz) {
            drawGrip(mx, my, mz);
          });
        }
      } else if (sel.type == SelectedEntity::Type::Arc) {
        if (sel.index >= 0 && static_cast<size_t>(sel.index) < cmd.userArcs.size()) {
          const CadArc& a = cmd.userArcs[static_cast<size_t>(sel.index)];
          drawGrip(a.cx, a.cy, a.z);
          drawGrip(a.cx + a.r * std::cos(a.startRad), a.cy + a.r * std::sin(a.startRad), a.z);
          const float endRad = a.startRad + a.sweepRad;
          drawGrip(a.cx + a.r * std::cos(endRad), a.cy + a.r * std::sin(endRad), a.z);
        }
      } else if (sel.type == SelectedEntity::Type::Ellipse) {
        if (sel.index >= 0 && static_cast<size_t>(sel.index) < cmd.userEllipses.size()) {
          const CadEllipse& el = cmd.userEllipses[static_cast<size_t>(sel.index)];
          drawGrip(el.cx, el.cy, el.z);
          drawGrip(el.cx + el.majVx, el.cy + el.majVy, el.z);
          const float perpX = -el.majVy;
          const float perpY = el.majVx;
          drawGrip(el.cx + perpX * el.ratio, el.cy + perpY * el.ratio, el.z);
        }
      } else if (sel.type == SelectedEntity::Type::BlockRef) {
        if (sel.index >= 0 && static_cast<size_t>(sel.index) < cmd.cadBlockRefs.size()) {
          const CadBlockRef& r = cmd.cadBlockRefs[static_cast<size_t>(sel.index)];
          const int di = CadBlockFindDef(cmd.blockDefs, r.defName);
          if (di >= 0) {
            const CadBlockDefinition& def = cmd.blockDefs[static_cast<size_t>(di)];
            const int nG = CadBlockDynGripCount(def);
            for (int g = 0; g < nG; ++g) {
              if (!CadBlockDynGripShownOnInsert(g))
                continue;
              float gx = 0.f, gy = 0.f, gz = r.xf.z;
              if (!CadBlockDynGripWorld(def, r, g, &gx, &gy, &gz))
                continue;
              ImVec2 gp{};
              wts(gx, gy, &gp, gz);
              float ldx = 0.f, ldy = 0.f, wdx = 0.f, wdy = 0.f;
              CadBlockDynGripLocalAxis(g, &ldx, &ldy);
              CadBlockXformDelta(r.xf, ldx, ldy, &wdx, &wdy);
              ImVec2 gp2{};
              wts(gx + wdx, gy + wdy, &gp2, gz);
              const CadBlockDynGripShape sh = CadBlockDynGripShapeOf(g);
              const ImU32 fill = (sh == CadBlockDynGripShape::Square) ? kGripFillE : IM_COL32(0, 220, 230, 255);
              const ImU32 bord = (sh == CadBlockDynGripShape::Square) ? kGripBorderE : IM_COL32(0, 140, 160, 255);
              DrawCadGripMarker(dlG, gp, gripHalf, sh, gp2.x - gp.x, gp2.y - gp.y, fill, bord);
            }
          } else
            drawGrip(r.xf.x, r.xf.y, r.xf.z);
        }
      } else if (sel.type == SelectedEntity::Type::Table) {
        if (sel.index >= 0 && static_cast<size_t>(sel.index) < cmd.cadTables.size()) {
          const CadTable& t = cmd.cadTables[static_cast<size_t>(sel.index)];
          for (int i = 0; i < 4; ++i) {
            float gx = 0.f, gy = 0.f;
            CadTableWorldCorner(t, i, &gx, &gy);
            drawGrip(gx, gy, t.insZ);
          }
        }
      }
    }
  }

  using VK = AppCommandState::Kind;
  const bool inImage = hovered && mx >= 0.f && mx < avail.x && my >= 0.f && my < avail.y;

  // REQ-307 (GitHub #106): the paper-space selection step keeps cmd.active == None throughout (it
  // is not a model-space command), so the engagement gate below — keyed on cmd.active — would never
  // let the palette engage for it without this. Extending the gate rather than cmd.active itself
  // avoids touching the huge existing Kind-keyed switches this whole file consults elsewhere.
  const bool paperSelStep = PaperIsObjectSelectionStep(cmd);

  if (cmd.active == VK::None && !paperSelStep) {
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
      (cmd.active != VK::None || paperSelStep) && cmd.active != VK::Pan && cmd.viewportCmdPaletteEngaged &&
      cmdBuf && cmdBufSize > 0 && !cmd.mtextRichEditorOpen && !cmd.tableCellEditorOpen;
  cmd.viewportDrawingHovered = showViewportCmdPalette;

  // Detect the palette's open edge so the two-field coordinate input resets its
  // typed-value locks when it (re)appears (REQ-024).
  static bool s_vpPalShownPrev = false;
  const bool vpPalJustOpened = showViewportCmdPalette && !s_vpPalShownPrev;
  s_vpPalShownPrev = showViewportCmdPalette;

  if (showViewportCmdPalette) {
    ImGuiIO& io = ImGui::GetIO();
    const ImGuiViewport* mainViewport = ImGui::GetMainViewport();

    const bool pointEntry = CommandExpectsPointEntry(cmd);

    // Prompt label (AutoCAD "Specify ... :"). Reset the two-field locks whenever
    // the prompt changes (new point, including after a commit or viewport click)
    // or the palette just reopened for a fresh command.
    const char* curHint = CommandInputHint(cmd);
    static const char* s_lastDynHint = nullptr;
    const bool promptChanged = (curHint != s_lastDynHint) || vpPalJustOpened;
    s_lastDynHint = curHint;
    const std::string promptLabel = pointEntry ? CadPointPromptLabel(cmd) : std::string(curHint);

    // Resolve the field's live text BEFORE layout, so the box sizes to its actual
    // content (REQ-306) instead of a fixed footprint. Point entry always has a
    // definite string (the live/typed coordinate); the non-point field may be
    // empty, so its placeholder hint is also weighed for width.
    static char dynBuf[160] = {0};
    static bool dynLocked = false;
    if (promptChanged) dynLocked = false;
    if (pointEntry) {
      double liveWx = 0.0, liveWy = 0.0;
      if (outCursorX && outCursorY)
        CadCoord::WorldFromLocal(cmd, static_cast<float>(*outCursorX), static_cast<float>(*outCursorY), &liveWx,
                                 &liveWy);
      const int prec = cmd.displayLinearPrecision;
      if (!dynLocked)
        std::snprintf(dynBuf, sizeof(dynBuf), "%s,%s", FormatLinear(liveWx, prec).c_str(),
                      FormatLinear(liveWy, prec).c_str());
    }
    const char* fieldHint = "Type value or Enter";
    const float minFieldPx = 56.f * io.FontGlobalScale;
    const float maxFieldPx = mainViewport->WorkSize.x * 0.48f;
    const float fieldW = pointEntry
        ? DynamicCursorFieldWidth(dynBuf, nullptr, minFieldPx, maxFieldPx)
        : DynamicCursorFieldWidth(cmdBuf, fieldHint, minFieldPx, maxFieldPx);

    const float pad = 14.f;
    const ImVec2 winPad(8.f, 6.f);
    const float estW = std::max(fieldW, ImGui::CalcTextSize(promptLabel.c_str()).x) + winPad.x * 2.f;
    const float estH = 60.f;
    ImVec2 wp(mouse.x + pad, mouse.y + pad);
    wp.x = std::clamp(wp.x, mainViewport->WorkPos.x + 4.f,
                      mainViewport->WorkPos.x + mainViewport->WorkSize.x - estW - 8.f);
    wp.y = std::clamp(wp.y, mainViewport->WorkPos.y + 4.f,
                      mainViewport->WorkPos.y + mainViewport->WorkSize.y - estH - 8.f);

    ImGui::SetNextWindowPos(wp, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.94f);
    ImGuiWindowFlags wf = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize |
                          ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, winPad);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.f);
    ImGui::Begin("##ViewportCommandInput", nullptr, wf);

    // Prompt label: a muted/secondary tone with a little gap below it, so it reads
    // as a label separated from the input box. Point prompts get an AutoCAD-style
    // "Specify … :" label; other prompts keep the full guidance hint.
    const ImVec4 hintCol = (cmd.displayColorThemeIdx == 0)
        ? ImVec4(0.90f, 0.93f, 0.98f, 1.f)
        : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    ImGui::PushStyleColor(ImGuiCol_Text, hintCol);
    ImGui::TextUnformatted(promptLabel.c_str());
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0.f, 2.f));

    ray3d::Vec3 polarBase{};
    const bool polarPrompt = pointEntry && CadUcsPolarPromptBase(cmd, &polarBase);
    if (polarPrompt) {
      // Distance + angle, AutoCAD's UCS form (REQ-154; the stated exception to REQ-024's single
      // field). Both track the cursor until typed; either one's Enter commits the pair, assembled
      // as `@distance<angle` — the same string the command line accepts, so the mouse and the
      // keyboard produce identical input rather than two parallel code paths.
      static char distBuf[48] = {0};
      static char angBuf[48] = {0};
      static bool polarLocked = false;
      if (promptChanged) polarLocked = false;

      double liveWx = 0.0, liveWy = 0.0;
      if (outCursorX && outCursorY)
        CadCoord::WorldFromLocal(cmd, static_cast<float>(*outCursorX), static_cast<float>(*outCursorY), &liveWx,
                                 &liveWy);
      const ray3d::Vec3 cursorWorld{liveWx, liveWy, polarBase.z};
      const ray3d::Vec3 dir = ray3d::Sub(cursorWorld, polarBase);
      const int prec = cmd.displayLinearPrecision;
      if (!polarLocked) {
        std::snprintf(distBuf, sizeof(distBuf), "%s", FormatLinear(ray3d::Length(dir), prec).c_str());
        double angDeg = 0.0;
        // Measured in the ACTIVE frame's XY plane from its +X — the same reference the two-point
        // form uses, so a number read here and a number typed there mean the same rotation.
        if (ucs::AngleInRotationPlaneDeg(cmd.activeUcs, 'Z', dir, &angDeg)) {
          while (angDeg < 0.0) angDeg += 360.0;
          std::snprintf(angBuf, sizeof(angBuf), "%.0f", angDeg);
        } else {
          std::snprintf(angBuf, sizeof(angBuf), "0");
        }
      }

      const ImGuiInputTextFlags pf = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackAlways;
      const float boxW = 74.f * io.FontGlobalScale;
      const ImGuiID idDist = ImGui::GetID("##ucsDist");
      const ImGuiID idAng = ImGui::GetID("##ucsAng");
      const ImGuiID activeIdP = ImGui::GetActiveID();

      // Type-to-start, the same mechanism the single field has. Without it neither box ever takes
      // focus, so the pair was READ-ONLY: it tracked the cursor and swallowed nothing, and typing
      // went to the command bar instead. That is the whole point of the boxes, so this is not a
      // nicety — it is the feature.
      //
      // The first keystroke seeds the DISTANCE box, which is the one you land on. Tab then moves to
      // the angle (ImGui's own next-item behaviour, so no key handling of ours), which is how you
      // reach "I only care about the angle".
      if (activeIdP != idDist && activeIdP != idAng && !io.WantTextInput && io.InputQueueCharacters.Size > 0) {
        distBuf[0] = '\0';
        polarLocked = true;
        RouteQueuedCharsToCmdBuf(distBuf, static_cast<int>(sizeof(distBuf)), io);
        ImGui::SetKeyboardFocusHere();
      }

      ImGui::SetNextItemWidth(boxW);
      const bool distEnter = ImGui::InputText("##ucsDist", distBuf, sizeof(distBuf), pf, CommandLineInputCallback);
      if (ImGui::IsItemActivated() && !polarLocked) { distBuf[0] = '\0'; polarLocked = true; }
      if (ImGui::IsItemEdited()) polarLocked = true;
      ImGui::SameLine(0.f, 6.f);
      // The angle box wears its own "<", so the pair reads as the polar notation it produces rather
      // than as two unrelated numbers.
      ImGui::TextUnformatted("<");
      ImGui::SameLine(0.f, 4.f);
      ImGui::SetNextItemWidth(boxW);
      const bool angEnter = ImGui::InputText("##ucsAng", angBuf, sizeof(angBuf), pf, CommandLineInputCallback);
      if (ImGui::IsItemActivated() && !polarLocked) { angBuf[0] = '\0'; polarLocked = true; }
      if (ImGui::IsItemEdited()) polarLocked = true;

      if (distEnter || angEnter) {
        // An angle ALONE is a complete answer at these prompts, and the commonest one: the X-axis
        // and XY-plane picks define a DIRECTION, so the distance does not affect the resulting frame
        // at all. Tabbing to the angle, clearing it and typing 27 should work without also having to
        // supply a length nobody uses — so a blank or unusable distance falls back to the live one,
        // and to 1.0 if the cursor happens to sit on the origin.
        double useDist = 0.0;
        {
          std::istringstream di{std::string(distBuf)};
          if (!(di >> useDist) || !std::isfinite(useDist) || useDist == 0.0)
            useDist = ray3d::Length(dir);
          if (!std::isfinite(useDist) || useDist == 0.0)
            useDist = 1.0;
        }
        // A blank angle means "the direction I am pointing", so the live value stands in for it.
        std::string angText = StringUtil::trimCopy(std::string(angBuf));
        if (angText.empty()) {
          double liveAng = 0.0;
          if (ucs::AngleInRotationPlaneDeg(cmd.activeUcs, 'Z', dir, &liveAng)) {
            while (liveAng < 0.0) liveAng += 360.0;
          }
          char ab[32];
          std::snprintf(ab, sizeof(ab), "%.4f", liveAng);
          angText = ab;
        }
        char polarBuf[160];
        std::snprintf(polarBuf, sizeof(polarBuf), "@%.6f<%s", useDist, angText.c_str());
        ProcessCommandLineSubmit(polarBuf, static_cast<int>(sizeof(polarBuf)), cmd, log);
      }
    } else if (pointEntry) {
      // Single live coordinate field: tracks the crosshair's world X,Y at the
      // display precision until the user types (which locks it). The field accepts
      // absolute "x,y", relative "@dx,dy", bearings, distances, or any other input
      // ProcessCommandLineSubmit understands. Enter — or a viewport click —
      // commits. REQ-024.
      const ImGuiID idDyn = ImGui::GetID("##dynPt");
      const ImGuiID activeId = ImGui::GetActiveID();

      // Type-to-start: the first keystroke with the field unfocused begins fresh
      // entry immediately — clear the live value, capture the typed char, then lock
      // and focus the field. (Without seeding, ImGui's first key only grabs focus,
      // so it took two presses to start typing.)
      if (activeId != idDyn && !io.WantTextInput && io.InputQueueCharacters.Size > 0) {
        dynBuf[0] = '\0';
        dynLocked = true;
        RouteQueuedCharsToCmdBuf(dynBuf, static_cast<int>(sizeof(dynBuf)), io);
        ImGui::SetKeyboardFocusHere();
      }

      // CallbackAlways + CommandLineInputCallback collapses the select-all ImGui
      // applies when SetKeyboardFocusHere takes the field — otherwise the seeded
      // first character stays highlighted and the next keystroke replaces it.
      const ImGuiInputTextFlags pf = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackAlways;
      ImGui::SetNextItemWidth(fieldW);
      const bool dynEnter = ImGui::InputText("##dynPt", dynBuf, sizeof(dynBuf), pf, CommandLineInputCallback);
      // Clicking into the field clears its live readout for fresh entry.
      if (ImGui::IsItemActivated() && !dynLocked) { dynBuf[0] = '\0'; dynLocked = true; }
      if (ImGui::IsItemEdited()) dynLocked = true;

      if (dynEnter)
        ProcessCommandLineSubmit(dynBuf, static_cast<int>(sizeof(dynBuf)), cmd, log);
    } else {
      // Single field for non-point prompts (bearing/angle/distance/option/command).
      if (!io.WantTextInput && io.InputQueueCharacters.Size > 0) {
        RouteQueuedCharsToCmdBuf(cmdBuf, cmdBufSize, io);
        ImGui::SetKeyboardFocusHere(0);
      }
      const ImGuiInputTextFlags itf = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackAlways;
      ImGui::SetNextItemWidth(fieldW);
      const bool exec =
          ImGui::InputTextWithHint("##vp_cmd_buf", fieldHint, cmdBuf, static_cast<size_t>(cmdBufSize),
                                   itf, CommandLineInputCallback, nullptr);
      if (exec)
        ProcessCommandLineSubmit(cmdBuf, cmdBufSize, cmd, log);
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
  }

  // Grip stretch dynamic input (REQ-024 / REQ-047). A grip drag runs with no active command, so the
  // command palette above never appears for it. This is the same box at the cursor, showing the live
  // stretch distance with its text SELECTED — so typing a distance replaces it, exactly like AutoCAD.
  static char s_gripBuf[96] = {0};
  // The exact string we last forced into the field. Comparing against it is how the callback tells
  // "the user typed" from "we refreshed it" — ImGui applies keystrokes BEFORE CallbackAlways fires,
  // so without this the refresh would overwrite the first character the user typed.
  static char s_gripPushed[96] = {0};
  static bool s_gripLocked = false;
  static bool s_gripShownPrev = false;
  const bool showGripDynInput =
      cmd.entityGripMoveActive && !cmd.mtextRichEditorOpen && !cmd.tableCellEditorOpen &&
      cmd.active == AppCommandState::Kind::None;
  if (!showGripDynInput) {  // drag ended — next one starts with a fresh, unlocked field
    s_gripShownPrev = false;
    s_gripBuf[0] = '\0';
    s_gripPushed[0] = '\0';
    s_gripLocked = false;
  }
  if (showGripDynInput) {
    ImGuiIO& ioGrip = ImGui::GetIO();
    const ImGuiViewport* gripViewport = ImGui::GetMainViewport();
    const char* gripPrompt = cmd.orthoMode ? "Specify stretch distance (ortho):" : "Specify stretch distance:";

    // The live value the field mirrors until the user types over it.
    const std::string liveText =
        FormatLinear(static_cast<double>(cmd.entityGripLiveDistance), cmd.displayLinearPrecision);

    if (!s_gripShownPrev) {  // opening edge: fresh drag, fresh field
      s_gripBuf[0] = '\0';
      s_gripPushed[0] = '\0';
      s_gripLocked = false;
    }
    s_gripShownPrev = true;

    // Content-driven width (REQ-306): the shown value (locked typed text, or the
    // still-live readout) rather than a fixed footprint.
    const char* gripFieldText = s_gripLocked ? s_gripBuf : liveText.c_str();
    const float gripFieldW = DynamicCursorFieldWidth(gripFieldText, nullptr, 56.f * ioGrip.FontGlobalScale,
                                                      gripViewport->WorkSize.x * 0.48f);
    const ImVec2 gripWinPad(8.f, 6.f);
    const float gripPad = 14.f;
    const float gripEstW = std::max(gripFieldW, ImGui::CalcTextSize(gripPrompt).x) + gripWinPad.x * 2.f;
    const float gripEstH = 60.f;
    ImVec2 gp(mouse.x + gripPad, mouse.y + gripPad);
    gp.x = std::clamp(gp.x, gripViewport->WorkPos.x + 4.f,
                      gripViewport->WorkPos.x + gripViewport->WorkSize.x - gripEstW - 8.f);
    gp.y = std::clamp(gp.y, gripViewport->WorkPos.y + 4.f,
                      gripViewport->WorkPos.y + gripViewport->WorkSize.y - gripEstH - 8.f);

    ImGui::SetNextWindowPos(gp, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.94f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, gripWinPad);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.f);
    ImGui::Begin("##ViewportGripInput", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoDocking);

    const ImVec4 gripHintCol = (cmd.displayColorThemeIdx == 0)
        ? ImVec4(0.90f, 0.93f, 0.98f, 1.f)
        : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    ImGui::PushStyleColor(ImGuiCol_Text, gripHintCol);
    ImGui::TextUnformatted(gripPrompt);
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0.f, 2.f));

    struct GripDynState { const char* live; bool* locked; char* pushed; size_t pushedSize; };
    GripDynState gds{liveText.c_str(), &s_gripLocked, s_gripPushed, sizeof(s_gripPushed)};

    const auto gripCb = [](ImGuiInputTextCallbackData* data) -> int {
      auto* s = static_cast<GripDynState*>(data->UserData);
      if (!s || *s->locked)
        return 0;
      if (std::strcmp(data->Buf, s->pushed) != 0) {
        *s->locked = true;  // the buffer changed and we did not change it — the user typed
        return 0;
      }
      if (std::strcmp(data->Buf, s->live) != 0) {
        data->DeleteChars(0, data->BufTextLen);
        data->InsertChars(0, s->live);
        std::snprintf(s->pushed, s->pushedSize, "%s", s->live);
      }
      // Keep the whole value selected so the next keystroke replaces it.
      data->SelectionStart = 0;
      data->SelectionEnd = data->BufTextLen;
      data->CursorPos = data->BufTextLen;
      return 0;
    };

    // Hold focus while the value is still live, so the selection is visible and typing lands here.
    if (!s_gripLocked && ImGui::GetActiveID() != ImGui::GetID("##gripDist"))
      ImGui::SetKeyboardFocusHere();

    ImGui::SetNextItemWidth(gripFieldW);
    const bool gripEnter =
        ImGui::InputText("##gripDist", s_gripBuf, sizeof(s_gripBuf),
                         ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackAlways,
                         +gripCb, &gds);
    if (ImGui::IsItemEdited())
      s_gripLocked = true;

    if (gripEnter) {
      ProcessCommandLineSubmit(s_gripBuf, static_cast<int>(sizeof(s_gripBuf)), cmd, log);
      s_gripBuf[0] = '\0';
      s_gripPushed[0] = '\0';
      s_gripLocked = false;
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
  // PAN command (REQ-045): show a hand instead of the CAD crosshair while pan mode is active.
  // ORBIT (REQ-084 (c)) is a drag mode too: the crosshair would say "pick a point", which is not
  // what a left-drag does while it is active.
  const bool panMode = cmd.active == AppCommandState::Kind::Pan ||
                       cmd.active == AppCommandState::Kind::Orbit;
  if (panMode && liveHover)
    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
  if (!panMode && (liveHover || frozenHair)) {
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
      // The outCursor→screen mapping below uses the model/paper VIEW window (worldLeft..worldRight). In
      // floating model space outCursorX/Y are model-local coords (a different space), so that mapping would
      // throw the crosshair off-screen — keep it at the raw mouse there (the snap glyph shows the snap).
      if (outCursorX && outCursorY && !InFloatingModelSpace(cmd)) {
        // This exists so the crosshair jumps to the SNAPPED point rather than the raw mouse.
        // It must project through the camera: with an orbited view the plan mapping sends the
        // crosshair to a completely different pixel, and as the azimuth sweeps it traces a circle
        // around the target and flips to the far side of the viewport past 90 degrees.
        // In plan view (and paper space) the camera projection reduces exactly to the old
        // arithmetic, so this is a no-op there. Unsnapped, the ray→world→screen round trip returns
        // the mouse position, so the crosshair still sits under the cursor.
        if (modelSpace) {
          float sx = 0.f, sy = 0.f;
          CadViewCamera(cmd).WorldToScreen(static_cast<double>(*outCursorX), static_cast<double>(*outCursorY),
                                           static_cast<double>(cmd.uiCursorWorldZ), avail.x, avail.y, &sx, &sy);
          cx = imgPos.x + sx;
          cy = imgPos.y + sy;
        } else {
          const float denx = worldRight - worldLeft + 1.e-12f;
          const float deny = worldTop - worldBottom + 1.e-12f;
          const float uSnap = (*outCursorX - worldLeft) / denx;
          const float vSnap = (worldTop - *outCursorY) / deny;
          cx = imgPos.x + uSnap * avail.x;
          cy = imgPos.y + vSnap * avail.y;
        }
      }
      // Remember the live crosshair so the popup can anchor here, and so the position
      // is preserved once we freeze on the next typed character.
      s_lastCrosshairScreen = ImVec2(cx, cy);
    }
    // REQ-121 rule (2): during an object-selection step the cursor is a PICKBOX — the centre square
    // alone, with the four crosshair arms suppressed. That is AutoCAD's convention and the visible
    // signal that rules (1) and (3) are in force: crosshair means "place a point", box means "pick
    // a thing".
    //
    // The square is sized from `viewportCrosshairPickHalfPx*`, a setting that already existed, is
    // already persisted in `.gs` and user prefs, and was until now never read by the renderer — the
    // crosshair's centre box tracks the SNAP APERTURE instead. So the pickbox gets the field named
    // for it rather than a new tunable (CLAUDE.md rule 2), and the crosshair keeps the aperture-
    // sized box it has always drawn, unchanged.
    const bool pickboxCursor = ViewportIsObjectSelectionStep(cmd) || PaperIsObjectSelectionStep(cmd);  // REQ-307
    const float ap = std::clamp(cmd.objectSnapAperturePx, 4.f, 64.f);
    const float phx = pickboxCursor ? std::clamp(cmd.viewportCrosshairPickHalfPxX, 2.f, 32.f) : ap * 0.5f;
    const float phy = pickboxCursor ? std::clamp(cmd.viewportCrosshairPickHalfPxY, 2.f, 32.f) : ap * 0.5f;
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
    // The arms are what make it a crosshair; a pickbox is the box on its own (REQ-121 rule 2).
    //
    // REQ-310: with the 3D crosshair on, the two screen-aligned arms are replaced by the active
    // UCS's three axes projected into the view. Model space only — a paper sheet is 2D by
    // definition (ADR-009/013), so there is no frame there for the axes to describe.
    const bool hair3d = cmd.viewportCrosshair3d && modelSpace && !InFloatingModelSpace(cmd);
    bool drew3d = false;
    if (!pickboxCursor && hair3d) {
      // One arm length for all three axes, so the triad reads as a triad rather than as an ellipse.
      // Taken from the vertical setting because that is what CURSORSIZE maps to directly
      // (`viewportCrosshairArmFracY` == the percentage; the X fraction is a 0.6 derivative of it).
      const float arm3d = fry * avail.y;
      const crosshair3d::Triad tri = crosshair3d::Compute(CadViewCamera(cmd), cmd.activeUcs, arm3d);
      // `Degenerate` is the pure-geometry guard (each axis cleared 6 px). The renderer also gaps
      // every arm by the pickbox, and with a large CURSORSIZE/pickbox that gap can reach ~32 px and
      // swallow a near-edge-on axis whole. `DrawableCount` applies that same test, so the fallback
      // decision matches what actually renders — REQ-201: degrade to the 2D crosshair, never to a
      // one-armed one.
      if (!crosshair3d::Degenerate(tri) && crosshair3d::DrawableCount(tri, phx, phy) >= 2) {
        auto col = [](const crosshair3d::AxisRgb& c) { return IM_COL32(c.r, c.g, c.b, 255); };
        // Each axis is a FULL line through the centre, gapped by the pickbox so the square stays
        // readable — the same gap the 2D arms leave.
        auto drawAxis = [&](const crosshair3d::Arm& a, ImU32 c) {
          if (!crosshair3d::ArmDrawable(a, phx, phy))
            return;
          const float len = std::sqrt(a.dx * a.dx + a.dy * a.dy);
          const float ux = a.dx / len, uy = a.dy / len;
          const float gap = crosshair3d::ArmGapPx(a, phx, phy);
          wdl->AddLine(ImVec2(cx + ux * gap, cy + uy * gap), ImVec2(cx + a.dx, cy + a.dy), c, hair);
          wdl->AddLine(ImVec2(cx - ux * gap, cy - uy * gap), ImVec2(cx - a.dx, cy - a.dy), c, hair);
        };
        // Z first so an in-plane X or Y arm draws over it, matching the UCS icon's own ordering —
        // in plan view Z is a dot under the centre and must not sit on top of the pickbox.
        drawAxis(tri.z, col(crosshair3d::kAxisColorZ));
        drawAxis(tri.x, col(crosshair3d::kAxisColorX));
        drawAxis(tri.y, col(crosshair3d::kAxisColorY));
        drew3d = true;
      }
      // A degenerate frame — or one with fewer than two arms the pickbox does not swallow — falls
      // through to the 2D arms below rather than leaving no cursor.
    }
    if (!pickboxCursor && !drew3d) {
      wdl->AddLine(ImVec2(xl, cy), ImVec2(cx - phx, cy), kCad, hair);
      wdl->AddLine(ImVec2(cx + phx, cy), ImVec2(xr, cy), kCad, hair);
      wdl->AddLine(ImVec2(cx, yt), ImVec2(cx, cy - phy), kCad, hair);
      wdl->AddLine(ImVec2(cx, cy + phy), ImVec2(cx, yb), kCad, hair);
    }
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

  // ---- POLAR tracking alignment path + readout (issue #154 AC-3) -------------------------------
  // Only while POLAR is on and a rubber-band draw has an anchor down. The rubber band itself already
  // snaps to the ray (ApplyOrthoConstrainFromAnchor's polar fall-through); this adds AutoCAD's dashed
  // alignment path through the anchor and the "distance < angle" tooltip.
  if (modelSpace && cmd.polarMode && liveHover && outCursorX && outCursorY && !InFloatingModelSpace(cmd)) {
    const bool haveAnchor =
        (cmd.active == AppCommandState::Kind::Line &&
         cmd.linePhase == AppCommandState::LinePhase::NeedNextPoint) ||
        (cmd.active == AppCommandState::Kind::Polyline &&
         cmd.polylinePhase == AppCommandState::PolylinePhase::NeedNextPoint);
    if (haveAnchor) {
      const ucs::Ucs frame = CadActiveUcsStorage(cmd);
      const ray3d::Vec3 anchor{cmd.anchorX, cmd.anchorY, 0.0};
      const ray3d::Vec3 cursor{*outCursorX, *outCursorY, 0.0};
      const std::vector<double>& extra = cmd.polarExtraAnglesDeg;
      const ray3d::Vec3 snapped =
          ucs::SnapToPolarRay(frame, anchor, cursor, cmd.polarIncrementDeg,
                              extra.empty() ? nullptr : extra.data(), static_cast<int>(extra.size()));
      const ray3d::Vec3 d = ray3d::Sub(snapped, anchor);
      if (ray3d::Length(d) > 1e-9) {
        const Camera pcam = CadViewCamera(cmd);
        auto toScreen = [&](const ray3d::Vec3& p) {
          float sx = 0.f, sy = 0.f;
          pcam.WorldToScreen(p.x, p.y, cmd.uiCursorWorldZ, avail.x, avail.y, &sx, &sy);
          return ImVec2(imgPos.x + sx, imgPos.y + sy);
        };
        // Extend the ray well past the cursor both ways so it reads as an infinite alignment path.
        const double reach = 1e6;
        const ray3d::Vec3 dn = ray3d::Normalize(d);
        const ImVec2 a0 = toScreen(ray3d::Sub(anchor, ray3d::Scale(dn, reach)));
        const ImVec2 a1 = toScreen(ray3d::Add(anchor, ray3d::Scale(dn, reach)));
        ImDrawList* pdl = ImGui::GetWindowDrawList();
        pdl->PushClipRect(imgPos, ImVec2(imgPos.x + avail.x, imgPos.y + avail.y), true);
        // Dashed: short segments along the screen-space line.
        const float dx = a1.x - a0.x, dy = a1.y - a0.y;
        const float len = std::sqrt(dx * dx + dy * dy);
        const int seg = std::clamp(static_cast<int>(len / 12.f), 1, 4000);
        constexpr ImU32 kPolarCol = IM_COL32(120, 235, 140, 200);
        for (int i = 0; i < seg; i += 2) {
          const float t0 = static_cast<float>(i) / static_cast<float>(seg);
          const float t1 = static_cast<float>(i + 1) / static_cast<float>(seg);
          pdl->AddLine(ImVec2(a0.x + dx * t0, a0.y + dy * t0), ImVec2(a0.x + dx * t1, a0.y + dy * t1), kPolarCol,
                       1.0f);
        }
        double angDeg = 0.0;
        (void)ucs::AngleInRotationPlaneDeg(frame, 'Z', d, &angDeg);
        if (angDeg < 0.0)
          angDeg += 360.0;
        char rd[80];
        std::snprintf(rd, sizeof(rd), "%s < %.2f\xC2\xB0",
                      FormatLinear(ray3d::Length(d), cmd.displayLinearPrecision).c_str(), angDeg);
        const ImVec2 tp(mouse.x + 16.f, mouse.y + 16.f);
        const ImVec2 ts = ImGui::CalcTextSize(rd);
        pdl->AddRectFilled(ImVec2(tp.x - 3.f, tp.y - 2.f), ImVec2(tp.x + ts.x + 3.f, tp.y + ts.y + 2.f),
                           IM_COL32(20, 28, 22, 220), 3.f);
        pdl->AddText(tp, IM_COL32(190, 245, 200, 255), rd);
        pdl->PopClipRect();
      }
    }
  }

  // ---- REQ-072 analysis legend (TASK-086 §6 (4)) ------------------------------------------------
  DrawSurfaceAnalysisLegend(cmd, imgPos, avail);
  if (cmd.activeSpaceIndex == kModelSpaceIndex) {
    ImDrawList* labelDl = ImGui::GetWindowDrawList();
    const Camera labelCam = CadViewCamera(cmd);
    const double oX = cmd.worldDocumentOriginX;
    const double oY = cmd.worldDocumentOriginY;
    for (size_t si = 0; si < cmd.cadSurfaces.size(); ++si) {
      if (!SurfaceVisible(cmd, si) || si >= cmd.cadSurfaceAttrs.size())
        continue;
      const std::uint64_t id = cmd.cadSurfaceAttrs[si].id;
      auto it = std::find_if(cmd.surfaceDisplayCache.begin(), cmd.surfaceDisplayCache.end(),
                             [&](const AppCommandState::SurfaceDisplayCacheEntry& e) { return e.surfaceId == id; });
      if (it == cmd.surfaceDisplayCache.end())
        continue;
      for (const auto& lb : it->contourLabels) {
        float sx = 0.f, sy = 0.f;
        labelCam.WorldToScreen(static_cast<double>(lb.x) + oX, static_cast<double>(lb.y) + oY,
                               static_cast<double>(lb.z), avail.x, avail.y, &sx, &sy);
        char t[48];
        std::snprintf(t, sizeof(t), "%.2f", lb.level);
        labelDl->AddText(ImVec2(imgPos.x + sx, imgPos.y + sy), IM_COL32(240, 220, 80, 255), t);
      }
    }
  }

  // ---- ViewCube (REQ-059) ----------------------------------------------------------------------
  // Model space only: a paper sheet is 2D (ADR-025 (g)) and has no orientation to show.
  if (modelSpace && avail.x > 40.f && avail.y > 40.f) {
    const ImVec2 mp = ImGui::GetIO().MousePos;
    const viewcube::Result vc =
        viewcube::Draw(ImGui::GetWindowDrawList(), CadViewCamera(cmd), viewCubeX, viewCubeY, kViewCubeSize, mp.x,
                       mp.y, ImGui::IsMouseClicked(ImGuiMouseButton_Left), CadUcsViewAzimuthOffsetDeg(cmd));
    if (vc.changed)
      CadStartViewAnimation(cmd, vc.azimuthDeg, vc.elevationDeg);  // ease, don't jump (REQ-059)
  }

  // ---- UCS dropdown (REQ-154) --------------------------------------------------------------------
  // AutoCAD's little frame selector under the ViewCube. It answers "which frame am I in?" without
  // reading the status bar, and switching back to World — the thing you do most — becomes one click
  // instead of `UCS` then `W`.
  //
  // Drawn as its OWN overlay window rather than by moving this one's cursor. Reaching outside the
  // viewport window's content region with SetCursorScreenPos trips ImGui's "code uses
  // SetCursorPos()/SetCursorScreenPos() to extend window/parent boundaries" assertion, which paints
  // a red banner across the drawing — the same overlay-window shape the cursor's dynamic input above
  // already uses, and for the same reason.
  //
  // Model space only, like the ViewCube and the UCS icon: a paper sheet has no frame.
  if (modelSpace && avail.x > 200.f && avail.y > 200.f) {
    // The label names the frame: WCS, the saved name when the active frame IS one of them, and
    // otherwise "Unnamed" — AutoCAD's own word for a frame that has been built but not saved, and a
    // useful nudge that `UCS N` would keep it.
    const bool isWorld = CadUcsIsWorld(cmd);
    const std::string activeName = CadUcsFrameLabel(cmd);

    const float dropW = std::max(84.f, ImGui::CalcTextSize(activeName.c_str()).x + 40.f);
    ImGui::SetNextWindowPos(ImVec2(viewCubeX + kViewCubeSize - dropW, viewCubeY + kViewCubeSize + 6.f),
                            ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.f);  // the button carries its own fill; the window is just a frame
    const ImGuiWindowFlags dwf = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoFocusOnAppearing;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
    if (ImGui::Begin("##UcsDropdown", nullptr, dwf)) {
      ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.f, 3.f));
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.24f, 0.28f, 0.85f));
      const bool dropClicked = ImGui::Button((activeName + "  \xe2\x96\xbe##ucsdrop").c_str(), ImVec2(dropW, 22.f));
      const ImVec2 btnMax = ImGui::GetItemRectMax();
      if (dropClicked)
        ImGui::OpenPopup("##ucsdropmenu");
      ImGui::PopStyleColor();
      ImGui::PopStyleVar();

      // Right-aligned to the button. The button sits hard against the viewport's right edge (it
      // tracks the ViewCube), so a popup growing rightward runs off the drawing and under whatever
      // panel is docked there - which is exactly what it did before this pin.
      ImGui::SetNextWindowPos(ImVec2(btnMax.x, btnMax.y + 2.f), ImGuiCond_Always, ImVec2(1.f, 0.f));
      if (ImGui::BeginPopup("##ucsdropmenu")) {
        if (ImGui::MenuItem("WCS", nullptr, isWorld) && !isWorld)
          SetActiveUcs(cmd, ucs::Ucs{}, log);
        // Every frame saved in this drawing, so restoring one is a click. It is the only way to
        // restore by name besides the View Manager - the UCS command's Named option only saves.
        if (!cmd.ucsNamed.empty()) {
          ImGui::Separator();
          for (const NamedUcs& n : cmd.ucsNamed) {
            const bool isActive = !isWorld && ucs::FramesMatch(n.frame, cmd.activeUcs);
            if (ImGui::MenuItem(n.name.c_str(), nullptr, isActive) && !isActive)
              SetActiveUcs(cmd, n.frame, log);
          }
        }
        ImGui::Separator();
        // Opens the ordinary command, so the dropdown and the command line share one implementation
        // and cannot drift about what "new UCS" means.
        if (ImGui::MenuItem("New UCS"))
          StartUcsCommand(cmd, log);
        ImGui::EndPopup();
      }
    }
    ImGui::End();
    ImGui::PopStyleVar();
  }

  // ---- UCS icon (REQ-154) ------------------------------------------------------------------------
  // Drawn AT THE UCS ORIGIN, which is AutoCAD's UCSICON Origin behaviour and what a user reading a
  // rotated frame actually wants: the icon then says where the frame IS, not merely which way it
  // points. Model space only, like the ViewCube: a paper sheet has no coordinate frame.
  //
  // It falls back to the bottom-left corner whenever the origin is off-screen or too near an edge
  // to draw the whole triad. That fallback is not a nicety — an icon pinned to an origin you have
  // panned away from is an icon you cannot see, and the frame matters most exactly then. AutoCAD
  // does the same.
  //
  // Purely an indicator either way: no hit region, swallows no clicks.
  if (modelSpace && avail.x > 80.f && avail.y > 80.f) {
    constexpr float kUcsIconArm = 26.f;
    constexpr float kUcsIconInset = 46.f;
    // What the icon draws BELOW its root: an axis label sits 8 px past a tip, and the "W" marker
    // one text line under the origin. A horizontal X arm (which is exactly the World case) puts
    // both at the root's own height.
    constexpr float kUcsIconDescent = 26.f;

    float iconX = imgPos.x + kUcsIconInset;
    float iconY = imgPos.y + avail.y - kUcsIconInset;
    // Stay clear of the floating command bar (REQ-040). It is a separate window painted OVER this
    // viewport and shares the bottom-left corner, so without this the World icon's X arm and its
    // "W" are drawn underneath it — the icon hides exactly the frame it exists to name, and only
    // in World, because any rotation lifts both arms clear.
    if (cmd.cmdBarTopYPx > 0.f)
      iconY = std::min(iconY, cmd.cmdBarTopYPx - kUcsIconDescent - 6.f);
    iconY = std::max(iconY, imgPos.y + kUcsIconArm + 12.f);  // never climb out of the viewport

    // Project the frame's own origin. The stores are local in XY and the UCS is world (see
    // CadActiveUcsStorage), so it is converted down before projecting rather than the camera being
    // asked about a world point it does not use.
    {
      const ucs::Ucs frameStore = CadActiveUcsStorage(cmd);
      float olx = 0.f, oly = 0.f;
      CadCoord::LocalFromWorld(cmd, static_cast<float>(cmd.activeUcs.origin.x),
                               static_cast<float>(cmd.activeUcs.origin.y), &olx, &oly);
      float sx = 0.f, sy = 0.f;
      CadViewCamera(cmd).WorldToScreen(static_cast<double>(olx), static_cast<double>(oly),
                                       cmd.activeUcs.origin.z, avail.x, avail.y, &sx, &sy);
      const float ax = imgPos.x + sx;
      const float ay = imgPos.y + sy;
      // Room for the arms, their labels and the "W" on every side before committing to the origin.
      const float pad = kUcsIconArm + kUcsIconDescent + 8.f;
      const bool fits = std::isfinite(ax) && std::isfinite(ay) && ax > imgPos.x + pad &&
                        ax < imgPos.x + avail.x - pad && ay > imgPos.y + pad &&
                        ay < imgPos.y + avail.y - pad &&
                        (cmd.cmdBarTopYPx <= 0.f || ay < cmd.cmdBarTopYPx - pad);
      if (fits) {
        iconX = ax;
        iconY = ay;
      }
      ucsicon::Draw(ImGui::GetWindowDrawList(), CadViewCamera(cmd), frameStore, iconX, iconY, kUcsIconArm,
                    CadUcsIsWorld(cmd));
    }

    // Live preview of the frame BEING DEFINED (REQ-154), drawn at the origin already picked and
    // oriented by what the cursor currently implies. This is the thing a user is actually deciding
    // at these prompts — the rubber line says which DIRECTION, but only a labelled triad says which
    // way X and Y will end up pointing, and that is the part that is easy to get backwards.
    //
    // Drawn in addition to the icon above, not instead of it: the icon is where the frame IS now,
    // this is where it WOULD be. Seeing both is what makes the change legible.
    //
    // It derives its frame from ucs::AlignedToDirection and ucs::FromThreePoints — the same two
    // functions the commit uses — so the preview cannot show one frame and commit another.
    if (cmd.active == AppCommandState::Kind::Ucs && outCursorX && outCursorY) {
      using UPh = AppCommandState::UcsPhase;
      const bool xPhase = cmd.ucsPhase == UPh::WaitXAxisPoint;
      const bool xyPhase = cmd.ucsPhase == UPh::WaitXyPoint;
      if (xPhase || xyPhase) {
        double cwx = 0.0, cwy = 0.0;
        CadCoord::WorldFromLocal(cmd, static_cast<float>(*outCursorX), static_cast<float>(*outCursorY), &cwx, &cwy);
        const ray3d::Vec3 cursorWorld{cwx, cwy, cmd.ucsPendingOrigin.z};
        ucs::Ucs preview;
        const bool built =
            xPhase ? ucs::AlignedToDirection(cmd.ucsPendingOrigin, ray3d::Sub(cursorWorld, cmd.ucsPendingOrigin),
                                             &preview)
                   : ucs::FromThreePoints(cmd.ucsPendingOrigin, cmd.ucsPendingXAxisPoint, cursorWorld, &preview);
        if (built) {
          // Same local-space conversion the icon above uses: the stores are local in XY and the UCS
          // is world, so the origin comes down before it is projected.
          float plx = 0.f, ply = 0.f;
          CadCoord::LocalFromWorld(cmd, static_cast<float>(cmd.ucsPendingOrigin.x),
                                   static_cast<float>(cmd.ucsPendingOrigin.y), &plx, &ply);
          float psx = 0.f, psy = 0.f;
          CadViewCamera(cmd).WorldToScreen(static_cast<double>(plx), static_cast<double>(ply),
                                           cmd.ucsPendingOrigin.z, avail.x, avail.y, &psx, &psy);
          const float px = imgPos.x + psx;
          const float py = imgPos.y + psy;
          if (std::isfinite(px) && std::isfinite(py) && px > imgPos.x - 200.f &&
              px < imgPos.x + avail.x + 200.f && py > imgPos.y - 200.f && py < imgPos.y + avail.y + 200.f) {
            // The preview frame is expressed in WORLD; the icon draws from storage-local axes, and
            // the two differ only by the document origin, which is a translation — so the axes carry
            // over unchanged and only the origin needed converting.
            ucsicon::Draw(ImGui::GetWindowDrawList(), CadViewCamera(cmd), preview, px, py, kUcsIconArm,
                          /*isWorld=*/false);
          }
        }
      }
    }
  }

  if (ImGui::BeginPopup("##drawing1_vp_ctx")) {
    using AK = AppCommandState::Kind;
    const bool gripActive = cmd.dimGripMoveActive || cmd.entityGripMoveActive ||
                            cmd.mtextGripMoveActive || cmd.mtextRichEditorOpen || cmd.tableCellEditorOpen;
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
      // Edit Mode / Default Mode shortcut menu — the drawing's action menu (REQ-084 (c)).
      if (cmd.lastCommand != AK::None) {
        char repeatLabel[64];
        std::snprintf(repeatLabel, sizeof(repeatLabel), "Repeat %s",
                      AppCommandState::KindName(cmd.lastCommand));
        if (ImGui::MenuItem(repeatLabel))
          RepeatLastCommand(cmd, log);
      }

      // Recent Input — the SAME history the command bar's dropdown shows (REQ-040), newest first,
      // so the two surfaces cannot disagree about what was typed. Re-running is a plain submit
      // through the command line, which is what typing it again would do.
      if (ImGui::BeginMenu("Recent Input", !cmd.cmdEnteredHistory.empty())) {
        // Chosen first, run after the loop. Re-submitting appends to this same history (and, at the
        // 20-entry cap, erases its front), so running inside the loop would leave the rest of the
        // frame walking a vector that had shifted under it.
        std::string chosen;
        for (size_t i = cmd.cmdEnteredHistory.size(); i-- > 0;) {
          ImGui::PushID(static_cast<int>(i));
          if (ImGui::MenuItem(cmd.cmdEnteredHistory[i].c_str()))
            chosen = cmd.cmdEnteredHistory[i];
          ImGui::PopID();
        }
        ImGui::EndMenu();
        if (!chosen.empty()) {
          char buf[256];
          std::snprintf(buf, sizeof(buf), "%s", chosen.c_str());
          ProcessCommandLineSubmit(buf, static_cast<int>(sizeof(buf)), cmd, log);
          ImGui::CloseCurrentPopup();
        }
      }
      ImGui::Separator();

      // Isolate Objects (REQ-084 (d)).
      if (ImGui::BeginMenu("Isolate Objects")) {
        if (ImGui::MenuItem("Isolate Objects", nullptr, false, hasSel)) {
          IsolateSelectedObjects(cmd, log);
          ImGui::CloseCurrentPopup();
        }
        if (ImGui::MenuItem("Hide Objects", nullptr, false, hasSel)) {
          HideSelectedObjects(cmd, log);
          ImGui::CloseCurrentPopup();
        }
        if (ImGui::MenuItem("End Object Isolation", nullptr, false, !cmd.hiddenEntityIds.empty())) {
          EndObjectIsolation(cmd, log);
          ImGui::CloseCurrentPopup();
        }
        ImGui::EndMenu();
      }

      if (ImGui::BeginMenu("Clipboard")) {
        const bool hasClip = !cmd.clipboard.empty();
        if (ImGui::MenuItem("Cut", "Ctrl+X", false, hasSel)) {
          // No CUT command exists; cut IS copy-then-erase, and spelling it out here keeps one
          // owner for each half rather than a third path through the clipboard.
          CopySelectionToClipboard(cmd, log);
          StartDeleteCommand(cmd, log);
          ImGui::CloseCurrentPopup();
        }
        if (ImGui::MenuItem("Copy", "Ctrl+C", false, hasSel)) {
          CopySelectionToClipboard(cmd, log);
          ImGui::CloseCurrentPopup();
        }
        if (ImGui::MenuItem("Paste", "Ctrl+V", false, hasClip)) {
          StartPasteCommand(cmd, log);
          ImGui::CloseCurrentPopup();
        }
        if (ImGui::MenuItem("Paste at Original Coordinates", nullptr, false, hasClip)) {
          StartPasteOrigCommand(cmd, log);
          ImGui::CloseCurrentPopup();
        }
        ImGui::EndMenu();
      }

      if (ImGui::BeginMenu("Basic Modify Tools", hasSel)) {
        if (ImGui::MenuItem("Move"))           { StartMoveCommand(cmd, log);   ImGui::CloseCurrentPopup(); }
        if (ImGui::MenuItem("Copy Selection")) { StartCopyCommand(cmd, log);   ImGui::CloseCurrentPopup(); }
        if (ImGui::MenuItem("Rotate"))         { StartRotateCommand(cmd, log); ImGui::CloseCurrentPopup(); }
        if (ImGui::MenuItem("Scale"))          { StartScaleCommand(cmd, log);  ImGui::CloseCurrentPopup(); }
        if (ImGui::MenuItem("Mirror"))         { StartMirrorCommand(cmd, log); ImGui::CloseCurrentPopup(); }
        if (ImGui::MenuItem("Lengthen"))       { StartLengthenCommand(cmd, log); ImGui::CloseCurrentPopup(); }
        if (ImGui::MenuItem("Erase"))          { StartDeleteCommand(cmd, log); ImGui::CloseCurrentPopup(); }
        ImGui::Separator();
        if (ImGui::MenuItem("Offset"))         { StartOffsetCommand(cmd, log); ImGui::CloseCurrentPopup(); }
        if (ImGui::MenuItem("Trim"))           { StartTrimCommand(cmd, log);   ImGui::CloseCurrentPopup(); }
        if (ImGui::MenuItem("Extend"))         { StartExtendCommand(cmd, log); ImGui::CloseCurrentPopup(); }
        if (ImGui::MenuItem("Break"))          { StartBreakCommand(cmd, log);  ImGui::CloseCurrentPopup(); }
        if (ImGui::MenuItem("Stretch"))        { StartStretchCommand(cmd, log); ImGui::CloseCurrentPopup(); }
        if (ImGui::MenuItem("Fillet"))         { StartFilletCommand(cmd, log); ImGui::CloseCurrentPopup(); }
        if (ImGui::MenuItem("Chamfer"))        { StartChamferCommand(cmd, log); ImGui::CloseCurrentPopup(); }
        if (ImGui::MenuItem("Join"))           { StartJoinCommand(cmd, log);   ImGui::CloseCurrentPopup(); }
        ImGui::EndMenu();
      }
      ImGui::Separator();

      if (ImGui::MenuItem("Pan"))        { StartPanCommand(cmd, log);          ImGui::CloseCurrentPopup(); }
      if (ImGui::MenuItem("Zoom"))       { StartZoomWindowCommand(cmd, log);   ImGui::CloseCurrentPopup(); }
      if (ImGui::MenuItem("Free Orbit")) { StartOrbitCommand(cmd, log);        ImGui::CloseCurrentPopup(); }
      ImGui::Separator();

      if (ImGui::MenuItem("Quick Select...")) {
        StartQuickSelectCommand(cmd, log);
        ImGui::CloseCurrentPopup();
      }
      // No "Find..." here. GoSurvey's find/replace (mtextFindReplaceOpen) lives inside the MTEXT
      // text-formatting panel and searches only the buffer being edited — and the drawing shortcut
      // menu cannot even open while that editor is up. A drawing-wide FIND does not exist, so the
      // item would be a control that does nothing, which is the failure REQ-201 forbids. Recorded
      // in REQ-084 and as TASK-070 DEBT-3; it returns when drawing-wide FIND is a requirement.
      if (ImGui::MenuItem("Options...")) {
        cmd.showSettingsWindow = true;
        ImGui::CloseCurrentPopup();
      }

      // REQ-054's selection items stay exactly where they were — below the general actions, and
      // only when there is a selection to act on.
      if (hasSel) {
        ImGui::Separator();
        if (ImGui::MenuItem("Select similar")) {
          SelectSimilarToCurrentSelection(cmd, &log);
          ImGui::CloseCurrentPopup();
        }
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
    // Issue #103: choosing a kind here arms a LIVE override — every hover/pick until the next
    // viewport commit (ClearPendingOneShotObjectSnap) is restricted to just that kind, exactly like
    // the persistent per-type toggles normally gate FindBest, except this ignores those toggles
    // entirely. That is the whole point of a temporary override: reaching a kind the user does not
    // keep enabled generally. Perpendicular is the one exception offered conditionally — it needs a
    // command-defined reference point to measure a foot from (CommandHasPerpendicularSnapReference),
    // which is a structural precondition, not a preference.
    const auto armOverride = [&](CadSnap::Kind k) {
      cmd.objectSnapKindOverrideValid = true;
      cmd.objectSnapKindOverrideKind = static_cast<int>(k);
      log.push_back(std::string(SnapKindLabelForUi(k)) + " snap locked for the next pick.");
      ImGui::CloseCurrentPopup();
    };

    ImGui::TextUnformatted("Snap once — choose type");
    ImGui::Separator();
    if (ImGui::Selectable("Endpoint"))
      armOverride(CadSnap::Kind::Endpoint);
    if (ImGui::Selectable("Midpoint"))
      armOverride(CadSnap::Kind::Midpoint);
    if (ImGui::Selectable("Center"))
      armOverride(CadSnap::Kind::Center);
    if (CadSnap::CommandHasPerpendicularSnapReference(cmd, true, /*ignoreToggle=*/true) &&
        ImGui::Selectable("Perpendicular"))
      armOverride(CadSnap::Kind::Perpendicular);
    if (ImGui::Selectable("Survey"))
      armOverride(CadSnap::Kind::SurveyCenter);
    if (ImGui::Selectable("Geometric center"))
      armOverride(CadSnap::Kind::GeometricCenter);
    if (ImGui::Selectable("Intersection"))
      armOverride(CadSnap::Kind::Intersection);
    if (ImGui::Selectable("Apparent intersection"))
      armOverride(CadSnap::Kind::ApparentIntersection);
    if (ImGui::Selectable("Surface"))
      armOverride(CadSnap::Kind::Surface);
    if (ImGui::Selectable("Solid edge"))
      armOverride(CadSnap::Kind::Edge);
    if (ImGui::Selectable("Solid face"))
      armOverride(CadSnap::Kind::Face);
    ImGui::EndPopup();
  }

  DrawMtextRichEditorOverlay(cmd, log, static_cast<float>(worldLeft), static_cast<float>(worldRight),
                             static_cast<float>(worldBottom), static_cast<float>(worldTop), imgPos, avail);
  DrawTableCellEditorOverlay(cmd, log, static_cast<float>(worldLeft), static_cast<float>(worldRight),
                             static_cast<float>(worldBottom), static_cast<float>(worldTop), imgPos, avail);

  // REQ-046: the per-viewport layer freeze UI moved from this standalone panel to the Layer Manager's
  // VP Freeze / VP Color columns and the VPFREEZE / VPTHAW commands. (Panel removed.)

  *outFbW = vpFbW;
  *outFbH = vpFbH;
  cmd.viewportLastFbW = vpFbW;
  cmd.viewportLastFbH = vpFbH;
  cmd.viewportPanX = *panX;
  cmd.viewportPanY = *panY;
  cmd.viewportZoom = *zoom;

  // The drawing canvas is the lowest plane in the shell, so it receives what the
  // ribbon above it and the panel beside it cast. Top and left are where those
  // sit in the shipped layout (SetupMainDockLayout), and they match the
  // light-from-top-left convention the classic theme's bevels already use, so
  // the two themes never disagree about where the light is.
  //
  // Cast onto the IMAGE rect, not the window rect. The window rect starts above
  // the dock tab bar and its content is inset by WindowPadding, so a shadow
  // aimed at it lands entirely on the tab strip and the padding band — drawn,
  // but nowhere near the drawing it is supposed to fall across.
  CastShadowInto(ImGui::GetWindowDrawList(), imgPos, ImVec2(imgPos.x + avail.x, imgPos.y + avail.y),
                 /*fromTop=*/true, /*fromLeft=*/true);

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
    case T::Table:      return (size_t)e.index < cmd.cadTableAttrs.size()       ? &cmd.cadTableAttrs[(size_t)e.index]       : nullptr;
    case T::BlockRef:   return (size_t)e.index < cmd.cadBlockRefAttrs.size()    ? &cmd.cadBlockRefAttrs[(size_t)e.index]    : nullptr;
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
    case T::Table:
      if (cmd.qsObjectType != OT::All)
        return false;
      break;
    case T::BlockRef:
      if (cmd.qsObjectType != OT::All)
        return false;
      break;
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
        const size_t k = (size_t)e.index * 4;
        if (k + 3 < cmd.userCirclesCxCyZR.size()) r = cmd.userCirclesCxCyZR[k + 3];
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
      if (e.type == T::Table && (size_t)e.index < cmd.cadTables.size()) {
        std::string joined;
        for (const std::string& c : cmd.cadTables[(size_t)e.index].cells) {
          if (!joined.empty())
            joined += " ";
          joined += c;
        }
        return matchStr(joined);
      }
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
    const int nCirc = (int)(cmd.userCirclesCxCyZR.size() / 4);
    for (int i = 0; i < nCirc; ++i)   addCad({SelectedEntity::Type::Circle, i});
    for (int i = 0; i < (int)cmd.userArcs.size(); ++i)      addCad({SelectedEntity::Type::Arc, i});
    for (int i = 0; i < (int)cmd.userEllipses.size(); ++i)  addCad({SelectedEntity::Type::Ellipse, i});
    const int nPoly = std::max(0, (int)cmd.userPolylineOffsets.size() - 1);
    for (int i = 0; i < nPoly; ++i)   addCad({SelectedEntity::Type::Polyline, i});
    for (int i = 0; i < (int)cmd.cadAnnotations.size(); ++i) addCad({SelectedEntity::Type::Annotation, i});
    for (int i = 0; i < (int)cmd.cadTables.size(); ++i)      addCad({SelectedEntity::Type::Table, i});
    for (int i = 0; i < (int)cmd.cadBlockRefs.size(); ++i)   addCad({SelectedEntity::Type::BlockRef, i});
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
      case CadAnnotation::Kind::Table:      kindStr = "Table";            break;
      case CadAnnotation::Kind::DimAligned: kindStr = "Dim (Aligned)";    break;
      case CadAnnotation::Kind::DimLinear:  kindStr = "Dim (Linear)";     break;
      case CadAnnotation::Kind::DimAngular: kindStr = "Dim (Angular)";    break;
      }
    }
    std::snprintf(buf, bufSize, "%s %d", kindStr, e.index + 1);
    break;
  }
  case T::Table:
    std::snprintf(buf, bufSize, "Table %d", e.index + 1);
    break;
  case T::BlockRef:
    std::snprintf(buf, bufSize, "Block %d", e.index + 1);
    break;
  case T::PdfUnderlay:
    std::snprintf(buf, bufSize, "PDF Underlay %d", e.index + 1);
    break;
  case T::Surface:
    // By NAME, not by ordinal (REQ-068). A surface is the one selectable object the user named
    // themselves, and "Surface 2" in a list beside "Existing Ground" and "Proposed" would be the
    // least useful of the three labels available.
    if (static_cast<size_t>(e.index) < cmd.cadSurfaces.size())
      std::snprintf(buf, bufSize, "Surface \"%s\"", cmd.cadSurfaces[static_cast<size_t>(e.index)].name.c_str());
    else
      std::snprintf(buf, bufSize, "Surface %d", e.index + 1);
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

// Quick-pick fonts shared by the dialog combo: a TrueType family ("Arial") or an SHX file name
// ("romans.shx"). Anything FontReg / Shx can resolve works.
static void DrawTextStyleSample(ImDrawList* dl, ImVec2 tl, ImVec2 sz, const TextStyle& s, const char* sample,
                                ImU32 col) {
  const float pad = 6.f;
  const float maxH = std::max(6.f, sz.y - 2.f * pad);
  const float maxW = std::max(6.f, sz.x - 2.f * pad);
  // Cap the glyph height so a tall preview box doesn't balloon the sample; then shrink to fit the width.
  float capPx = std::min(maxH, 48.f);
  const std::string previewFam = TextStyles::EffectiveFontFamily(s.fontFamily);
  Shx::Font* sf = CadIsShxFontName(previewFam) ? Shx::Resolve(previewFam) : nullptr;
  bool rb = false, ri = false;
  ImFont* tf = FontReg::Resolve(previewFam, s.bold, s.italic, &rb, &ri);
  if (!tf) tf = ImGui::GetFont();
  auto measure = [&](float cp) -> float {
    if (sf && sf->valid()) return Shx::MeasureWidthPx(*sf, sample, cp);
    return tf->CalcTextSizeA(cp, FLT_MAX, 0.f, sample).x;
  };
  const float w = measure(capPx);
  if (w > maxW && w > 0.f)
    capPx *= maxW / w;
  capPx = std::clamp(capPx, 4.f, 240.f);
  const ImVec2 org(tl.x + pad, tl.y + (sz.y - capPx) * 0.5f);  // vertically centered
  if (sf && sf->valid())
    Shx::DrawText(dl, *sf, ImVec2(org.x, org.y + capPx), capPx, 0.f, col, sample, std::max(1.f, capPx * 0.05f));
  else
    dl->AddText(tf, capPx, org, col, sample);
}

void DrawTextStyleManagerWindow(AppCommandState& cmd, std::vector<std::string>* log) {
  std::vector<std::string> discard;
  if (!log)
    log = &discard;
  TextStyles::EnsureStandard(cmd.textStyles);
  if (cmd.activeTextStyleName.empty() || !TextStyles::Find(cmd.textStyles, cmd.activeTextStyleName))
    cmd.activeTextStyleName = TextStyles::kStandardName;
  if (!cmd.showTextStyleManagerWindow)
    return;

  ImGui::SetNextWindowSize(ImVec2(720, 470), ImGuiCond_FirstUseEver);
  bool open = cmd.showTextStyleManagerWindow;
  if (!ImGui::Begin("Text Style", &open)) {
    cmd.showTextStyleManagerWindow = open;
    ImGui::End();
    return;
  }
  cmd.showTextStyleManagerWindow = open;

  static int selIdx = 0;
  if (selIdx < 0 || selIdx >= static_cast<int>(cmd.textStyles.size()))
    selIdx = 0;
  // Default the selection to the current style when the window first appears each session.
  static bool firstShow = true;
  if (firstShow) {
    for (size_t i = 0; i < cmd.textStyles.size(); ++i)
      if (cmd.textStyles[i].name == cmd.activeTextStyleName) selIdx = static_cast<int>(i);
    firstShow = false;
  }

  ImGui::Text("Current text style:  %s", cmd.activeTextStyleName.c_str());
  ImGui::Separator();

  bool styleChanged = false;
  int deleteIdx = -1;
  const float footer = ImGui::GetFrameHeightWithSpacing() + 8.f;

  // ── Left: styles list + live preview ───────────────────────────────────────────────────────────
  ImGui::BeginChild("##tsleft", ImVec2(220.f, -footer), false);
  ImGui::TextUnformatted("Styles:");
  ImGui::BeginChild("##tslist", ImVec2(0, 200.f), true);
  for (size_t i = 0; i < cmd.textStyles.size(); ++i) {
    const bool sel = (static_cast<int>(i) == selIdx);
    if (ImGui::Selectable(cmd.textStyles[i].name.c_str(), sel))
      selIdx = static_cast<int>(i);
  }
  ImGui::EndChild();
  ImGui::SetNextItemWidth(-1);
  ImGui::BeginDisabled(true);  // filter is cosmetic for now
  if (ImGui::BeginCombo("##allstyles", "All styles"))
    ImGui::EndCombo();
  ImGui::EndDisabled();
  ImGui::Spacing();
  {
    const ImVec2 pmn = ImGui::GetCursorScreenPos();
    const ImVec2 psz(ImGui::GetContentRegionAvail().x, std::max(70.f, ImGui::GetContentRegionAvail().y));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pmn, ImVec2(pmn.x + psz.x, pmn.y + psz.y), IM_COL32(250, 250, 250, 255), 3.f);
    dl->AddRect(pmn, ImVec2(pmn.x + psz.x, pmn.y + psz.y), IM_COL32(90, 90, 90, 255), 3.f, 0, 1.f);
    dl->PushClipRect(pmn, ImVec2(pmn.x + psz.x, pmn.y + psz.y), true);
    DrawTextStyleSample(dl, pmn, psz, cmd.textStyles[static_cast<size_t>(selIdx)], "AaBb12",
                        IM_COL32(20, 20, 20, 255));
    dl->PopClipRect();
    ImGui::Dummy(psz);
  }
  ImGui::EndChild();

  ImGui::SameLine();

  // ── Middle: font / size / effects (the AutoCAD groups; unsupported effects shown disabled) ───────
  TextStyle& s = cmd.textStyles[static_cast<size_t>(selIdx)];
  const bool isStandard = (s.name == TextStyles::kStandardName);
  bool inUse = false;
  for (const auto& a : cmd.cadAnnotations)
    if (TextStyles::IsStyleableText(a) && a.styleName == s.name) {
      inUse = true;
      break;
    }

  ImGui::BeginChild("##tsmid", ImVec2(-150.f, -footer), false);
  ImGui::SeparatorText("Font");
  ImGui::TextUnformatted("Font Name:");
  ImGui::SetNextItemWidth(230.f);
  if (ImGui::BeginCombo("##fontname",
                        s.fontFamily.empty() ? TextStyles::kDefaultFontFamily : s.fontFamily.c_str())) {
    for (const char* fn : kTextStyleFonts) {
      const char* label = fn;
      if (ImGui::Selectable(label, s.fontFamily == fn || (s.fontFamily.empty() && std::strcmp(fn, TextStyles::kDefaultFontFamily) == 0))) {
        s.fontFamily = fn;
        styleChanged = true;
      }
    }
    ImGui::EndCombo();
  }
  {
    static bool dummyBigFont = false;
    ImGui::BeginDisabled(true);
    ImGui::Checkbox("Use Big Font", &dummyBigFont);
    ImGui::EndDisabled();
  }
  ImGui::TextUnformatted("Font Style:");
  ImGui::SetNextItemWidth(160.f);
  {
    const char* fsNames[] = {"Regular", "Bold", "Italic", "Bold Italic"};
    const int fsIdx = (s.bold && s.italic) ? 3 : s.italic ? 2 : s.bold ? 1 : 0;
    if (ImGui::BeginCombo("##fstyle", fsNames[fsIdx])) {
      for (int k = 0; k < 4; ++k) {
        if (ImGui::Selectable(fsNames[k], k == fsIdx)) {
          s.bold = (k == 1 || k == 3);
          s.italic = (k == 2 || k == 3);
          styleChanged = true;
        }
      }
      ImGui::EndCombo();
    }
  }

  ImGui::SeparatorText("Size");
  {
    static bool dummyAnno = false;
    ImGui::BeginDisabled(true);
    ImGui::Checkbox("Annotative", &dummyAnno);
    ImGui::EndDisabled();
  }
  ImGui::TextUnformatted("Height");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(120.f);
  ImGui::InputFloat("##h", &s.heightInches, 0.f, 0.f, "%.4f");
  if (ImGui::IsItemDeactivatedAfterEdit()) {
    if (s.heightInches <= 0.f)
      s.heightInches = 0.0625f;
    styleChanged = true;
  }

  ImGui::SeparatorText("Effects");
  {
    static bool dUpside = false, dBackwards = false, dVertical = false;
    static float dWidthFactor = 1.f;
    ImGui::BeginDisabled(true);  // these effects are not modeled yet (visual parity with AutoCAD)
    ImGui::Checkbox("Upside down", &dUpside);
    ImGui::Checkbox("Backwards", &dBackwards);
    ImGui::Checkbox("Vertical", &dVertical);
    ImGui::TextUnformatted("Width Factor");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100.f);
    ImGui::InputFloat("##wf", &dWidthFactor, 0.f, 0.f, "%.4f");
    ImGui::EndDisabled();
  }
  ImGui::TextUnformatted("Oblique Angle");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(100.f);
  ImGui::InputFloat("##ob", &s.obliqueDeg, 0.f, 0.f, "%.3f");
  if (ImGui::IsItemDeactivatedAfterEdit())
    styleChanged = true;
  ImGui::EndChild();

  ImGui::SameLine();

  // ── Right: Set Current / New… / Delete ───────────────────────────────────────────────────────────
  ImGui::BeginGroup();
  if (ImGui::Button("Set Current", ImVec2(132.f, 0.f)))
    SetActiveTextStyle(cmd, s.name);
  if (ImGui::Button("New...", ImVec2(132.f, 0.f)))
    ImGui::OpenPopup("New Text Style");
  ImGui::BeginDisabled(isStandard || inUse);
  if (ImGui::Button("Delete", ImVec2(132.f, 0.f)))
    deleteIdx = selIdx;
  ImGui::EndDisabled();
  if ((isStandard || inUse) && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    ImGui::SetTooltip(isStandard ? "Standard cannot be deleted."
                                 : "Style is in use by existing text — cannot delete.");
  ImGui::EndGroup();

  // New-style modal.
  if (ImGui::BeginPopupModal("New Text Style", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    static char newNameBuf[120] = "Style1";
    ImGui::TextUnformatted("Style name:");
    ImGui::SetNextItemWidth(240.f);
    ImGui::InputText("##newname", newNameBuf, IM_ARRAYSIZE(newNameBuf));
    const std::string nm = TrimUi(std::string(newNameBuf));
    const bool dup = !nm.empty() && TextStyles::Find(cmd.textStyles, nm) != nullptr;
    if (dup)
      ImGui::TextColored(ImVec4(1.f, 0.5f, 0.4f, 1.f), "A style with that name already exists.");
    ImGui::BeginDisabled(nm.empty() || dup);
    if (ImGui::Button("OK", ImVec2(110.f, 0.f))) {
      PushUndoSnapshot(cmd, "Add text style");
      TextStyle ns = s;  // seed from the selected style
      ns.name = nm;
      cmd.textStyles.push_back(ns);
      selIdx = static_cast<int>(cmd.textStyles.size()) - 1;
      log->push_back("Text style added: " + nm);
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(110.f, 0.f)))
      ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }

  // ── Footer: Apply / Cancel / Help ────────────────────────────────────────────────────────────────
  ImGui::Separator();
  const float bw = 90.f;
  ImGui::SetCursorPosX(ImGui::GetWindowWidth() - (bw + 8.f) * 3.f);
  if (ImGui::Button("Apply", ImVec2(bw, 0.f))) {
    // Edits already apply live; Apply just re-confirms (re-bake + cache).
    TextStyles::RebakeAllForStyle(cmd.cadAnnotations, s);
    BumpCadGpuCache(cmd);
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel", ImVec2(bw, 0.f)))
    cmd.showTextStyleManagerWindow = false;
  ImGui::SameLine();
  ImGui::BeginDisabled(true);
  ImGui::Button("Help", ImVec2(bw, 0.f));
  ImGui::EndDisabled();

  // Apply edits to referencing text (live reference) + keep the new-text default height in sync.
  if (styleChanged) {
    TextStyles::RebakeAllForStyle(cmd.cadAnnotations, s);
    if (cmd.activeTextStyleName == s.name)
      SetActiveTextStyle(cmd, s.name);
    BumpCadGpuCache(cmd);
  }
  if (deleteIdx >= 0 && deleteIdx < static_cast<int>(cmd.textStyles.size())) {
    PushUndoSnapshot(cmd, "Delete text style");
    const std::string nm = cmd.textStyles[static_cast<size_t>(deleteIdx)].name;
    cmd.textStyles.erase(cmd.textStyles.begin() + deleteIdx);
    if (cmd.activeTextStyleName == nm)
      SetActiveTextStyle(cmd, TextStyles::kStandardName);
    if (selIdx >= static_cast<int>(cmd.textStyles.size()))
      selIdx = static_cast<int>(cmd.textStyles.size()) - 1;
    log->push_back("Text style deleted: " + nm);
  }

  ImGui::End();
}

void DrawDimStyleWindow(AppCommandState& cmd, std::vector<std::string>* log) {
  std::vector<std::string> discard;
  if (!log) log = &discard;
  if (!cmd.showDimStyleDialog) return;
  ImGui::SetNextWindowSize(ImVec2(640, 560), ImGuiCond_FirstUseEver);
  bool open = cmd.showDimStyleDialog;
  if (!ImGui::Begin("Dimension Style", &open)) {
    cmd.showDimStyleDialog = open;
    ImGui::End();
    return;
  }
  cmd.showDimStyleDialog = open;
  DimensionStyle& d = cmd.dimStyleDraft;
  // ---- Text ----
  ImGui::SeparatorText("Text");
  ImGui::DragFloat("Size (in)", &d.textSizeInches, 0.005f, 0.02f, 1.0f, "%.3f");
  if (d.textSizeInches < 0.01f) d.textSizeInches = 0.01f;
  // Font: simple combo of known fonts
  {
    const char* fonts[] = {"(default)", "Arial", "Times New Roman", "Courier New", "romans.shx", "simplex.shx"};
    std::string cur = d.textFont.empty() ? "(default)" : d.textFont;
    if (ImGui::BeginCombo("Font", cur.c_str())) {
      for (auto f : fonts) {
        std::string val = (std::string(f) == "(default)") ? "" : f;
        bool sel = (d.textFont == val);
        if (ImGui::Selectable(f, sel)) d.textFont = val;
      }
      ImGui::EndCombo();
    }
  }
  {
    const char* cols[] = {"ByLayer", "Red", "Yellow", "Green", "Cyan", "Blue", "Magenta", "White", "#e1b12c"};
    if (ImGui::BeginCombo("Text Color", d.textColor.c_str())) {
      for (auto c : cols) if (ImGui::Selectable(c, d.textColor==c)) d.textColor = c;
      ImGui::EndCombo();
    }
  }
  {
    const char* aligns[] = {"Center", "Above", "Beside"};
    int cur = (int)d.textAlign;
    if (ImGui::BeginCombo("Alignment", aligns[cur])) {
      for (int i=0;i<3;++i) if (ImGui::Selectable(aligns[i], i==cur)) d.textAlign = (DimTextAlign)i;
      ImGui::EndCombo();
    }
  }
  // ---- Dimension Lines ----
  ImGui::SeparatorText("Dimension Lines");
  {
    const char* cols[] = {"ByLayer", "Red", "Yellow", "Green", "Cyan", "Blue", "Magenta", "White"};
    if (ImGui::BeginCombo("Dim Line Color", d.dimLineColor.c_str())) {
      for (auto c : cols) if (ImGui::Selectable(c, d.dimLineColor==c)) d.dimLineColor = c;
      ImGui::EndCombo();
    }
  }
  {
    const char* lts[] = {"Continuous", "Dashed", "Dotted", "Center", "Phantom"};
    if (ImGui::BeginCombo("Dim Line Type", d.dimLineType.c_str())) {
      for (auto lt : lts) if (ImGui::Selectable(lt, d.dimLineType==lt)) d.dimLineType = lt;
      ImGui::EndCombo();
    }
  }
  // ---- Extension Lines ----
  ImGui::SeparatorText("Extension Lines");
  {
    const char* cols[] = {"ByLayer", "Red", "Yellow", "Green", "Cyan", "Blue", "Magenta", "White"};
    if (ImGui::BeginCombo("Ext Line Color", d.extLineColor.c_str())) {
      for (auto c : cols) if (ImGui::Selectable(c, d.extLineColor==c)) d.extLineColor = c;
      ImGui::EndCombo();
    }
  }
  {
    const char* lts[] = {"Continuous", "Dashed", "Dotted", "Center", "Phantom"};
    if (ImGui::BeginCombo("Ext Line Type", d.extLineType.c_str())) {
      for (auto lt : lts) if (ImGui::Selectable(lt, d.extLineType==lt)) d.extLineType = lt;
      ImGui::EndCombo();
    }
  }
  // ---- Arrows ----
  ImGui::SeparatorText("Arrows");
  ImGui::DragFloat("Arrow Size (in)", &d.arrowSizeInches, 0.005f, 0.01f, 1.0f, "%.3f");
  if (d.arrowSizeInches < 0.01f) d.arrowSizeInches = 0.01f;
  {
    const char* types[] = {"Closed Filled", "Closed Blank", "Tick", "Dot", "Open", "None"};
    int cur = (int)d.arrowType;
    if (ImGui::BeginCombo("Arrow Type", types[cur])) {
      for (int i=0;i<6;++i) if (ImGui::Selectable(types[i], i==cur)) d.arrowType = (DimArrowType)i;
      ImGui::EndCombo();
    }
  }
  {
    const char* cols[] = {"ByLayer", "Red", "Yellow", "Green", "Cyan", "Blue", "Magenta", "White"};
    if (ImGui::BeginCombo("Arrow Color", d.arrowColor.c_str())) {
      for (auto c : cols) if (ImGui::Selectable(c, d.arrowColor==c)) d.arrowColor = c;
      ImGui::EndCombo();
    }
  }
  // ---- Units ----
  ImGui::SeparatorText("Units");
  {
    const char* fmts[] = {"Decimal", "Architectural", "Engineering", "Fractional"};
    int cur = (int)d.unitFormat;
    if (ImGui::BeginCombo("Unit Format", fmts[cur])) {
      for (int i=0;i<4;++i) if (ImGui::Selectable(fmts[i], i==cur)) d.unitFormat = (DimUnitFormat)i;
      ImGui::EndCombo();
    }
  }
  {
    const char* precs[] = {"0", "0.0", "0.00", "0.000", "0.0000"};
    int cur = d.unitPrecision;
    if (cur < 0) cur = 0; if (cur > 4) cur = 4;
    if (ImGui::BeginCombo("Precision", precs[cur])) {
      for (int i=0;i<5;++i) if (ImGui::Selectable(precs[i], i==cur)) d.unitPrecision = i;
      ImGui::EndCombo();
    }
  }
  {
    float tmpScale = (float)d.unitScale;
    if (ImGui::DragFloat("Unit Scale", &tmpScale, 0.01f, 0.001f, 1000.f, "%.3f")) d.unitScale = (double)tmpScale;
    if (d.unitScale < 0.001) d.unitScale = 0.001;
  }
  // ---- Preview ----
  ImGui::SeparatorText("Preview");
  {
    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 p1(p0.x + avail.x, p0.y + 40.f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, p1, IM_COL32(35,35,45,255), 4.f);
    dl->AddRect(p0, p1, IM_COL32(90,90,90,255), 4.f, 0, 1.f);
    float midY = (p0.y + p1.y)*0.5f;
    float xL = p0.x + 40.f, xR = p1.x - 40.f;
    dl->AddLine(ImVec2(xL, midY+10), ImVec2(xL, midY-10), IM_COL32(200,200,200,255), 1.f);
    dl->AddLine(ImVec2(xR, midY+10), ImVec2(xR, midY-10), IM_COL32(200,200,200,255), 1.f);
    dl->AddLine(ImVec2(xL, midY), ImVec2(xR, midY), IM_COL32(46,91,174,255), 1.5f);
    // arrows
    float ah = 6.f;
    dl->AddTriangleFilled(ImVec2(xL, midY), ImVec2(xL+ah, midY-ah*0.5f), ImVec2(xL+ah, midY+ah*0.5f), IM_COL32(46,91,174,255));
    dl->AddTriangleFilled(ImVec2(xR, midY), ImVec2(xR-ah, midY-ah*0.5f), ImVec2(xR-ah, midY+ah*0.5f), IM_COL32(46,91,174,255));
    std::string txt = DimensionStyles::FormatLinearDim(12.5, d);
    CadAnnotation previewAnn;
    previewAnn.fontFamily = d.textFont;
    previewAnn.text = txt;
    DrawDimLabelText(dl, previewAnn, ImGui::GetFont(), 14.f, ImVec2((xL + xR) * 0.5f, midY - 10.f), 0.f,
                     IM_COL32(248, 250, 252, 255));
    ImGui::Dummy(ImVec2(avail.x, 44.f));
  }
  ImGui::Separator();
  auto applyDimStyleToAnns = [&](std::vector<CadAnnotation>& anns) {
    for (auto& a : anns) {
      if (a.kind == CadAnnotation::Kind::DimAligned || a.kind == CadAnnotation::Kind::DimLinear) {
        float sx1, sy1, sx2, sy2, tx, ty, nx, ny, ml;
        if (CadDimAnyGeometry(a, &sx1, &sy1, &sx2, &sy2, &tx, &ty, &nx, &ny, &ml))
          a.text = DimensionStyles::FormatLinearDim(static_cast<double>(ml), cmd.activeDimensionStyle);
      } else if (a.kind == CadAnnotation::Kind::DimAngular) {
        float a1 = 0.f, a2 = 0.f, sweep = 0.f, theta = 0.f, bisx = 0.f, bisy = 0.f;
        if (CadDimAngularComputeFrame(a, &a1, &a2, &sweep, &bisx, &bisy, &theta))
          a.text = FormatSweptAngle(static_cast<double>(theta) * (180.0 / 3.14159265358979323846),
                                    CadAngleDisplaySettings(cmd));
      } else
        continue;
      DimensionStyles::BakeTextOntoDimension(a, cmd.activeDimensionStyle);
    }
  };
  auto applyDimStyleEverywhere = [&]() {
    applyDimStyleToAnns(cmd.cadAnnotations);
    for (PaperLayout& L : cmd.paperLayouts)
      applyDimStyleToAnns(L.paperTexts);
  };
  float bw = 90.f;
  if (ImGui::Button("OK", ImVec2(bw, 0))) {
    PushUndoSnapshot(cmd, "DIMSTY");
    cmd.activeDimensionStyle = d;
    applyDimStyleEverywhere();
    BumpCadGpuCache(cmd);
    cmd.showDimStyleDialog = false;
    log->push_back("DIMSTY — style applied.");
  }
  ImGui::SameLine();
  if (ImGui::Button("Apply", ImVec2(bw, 0))) {
    PushUndoSnapshot(cmd, "DIMSTY");
    cmd.activeDimensionStyle = d;
    applyDimStyleEverywhere();
    BumpCadGpuCache(cmd);
    log->push_back("DIMSTY — style applied.");
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel", ImVec2(bw, 0))) {
    cmd.showDimStyleDialog = false;
  }
  ImGui::End();
}

// REQ-106 — the View Manager, the dialog half of "a VIEW command/dialog".
//
// Deliberately small: list, restore, delete, and save-the-current-view. Everything it does to a
// VIEW is something `VIEW` can do from the command line, and it calls the same functions — so the
// dialog cannot drift from the command, which is the failure the DIMSTY/UNITS pair already shows is
// real.
//
// It also owns restore and delete for named coordinate systems, which `UCS Named` deliberately does
// NOT offer (REQ-154). Not an exception to the rule above: those two still go through the shared
// RestoreNamedUcs / DeleteNamedUcs, so there is one implementation — it is only the *prompt* for
// them that the command line no longer has, because choosing among saved frames wants a list.
void DrawViewManagerWindow(AppCommandState& cmd, std::vector<std::string>* log) {
  std::vector<std::string> discard;
  if (!log)
    log = &discard;

  // The New View prompt is its own tiny modal so the ribbon button is one click to a name box,
  // rather than opening the manager and hunting for a button.
  if (cmd.showViewManagerNewPrompt) {
    ImGui::OpenPopup("New View");
    cmd.showViewManagerNewPrompt = false;
    cmd.newViewNameBuf[0] = '\0';
  }
  if (ImGui::BeginPopupModal("New View", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextUnformatted("Save the current camera and coordinate frame as:");
    ImGui::SetNextItemWidth(260.f);
    const bool entered = ImGui::InputText("##newviewname", cmd.newViewNameBuf, sizeof(cmd.newViewNameBuf),
                                          ImGuiInputTextFlags_EnterReturnsTrue);
    const bool named = cmd.newViewNameBuf[0] != '\0';
    ImGui::BeginDisabled(!named);
    const bool okd = ImGui::Button("Save", ImVec2(90.f, 0.f));
    ImGui::EndDisabled();
    if ((entered || okd) && named) {
      ProcessViewCommandLine(cmd, std::string("S ") + cmd.newViewNameBuf, *log);
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(90.f, 0.f)))
      ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }

  if (!cmd.showViewManagerWindow)
    return;
  ImGui::SetNextWindowSize(ImVec2(560.f, 480.f), ImGuiCond_FirstUseEver);
  bool open = cmd.showViewManagerWindow;
  if (!ImGui::Begin("View Manager", &open)) {
    ImGui::End();
    cmd.showViewManagerWindow = open;
    return;
  }
  cmd.showViewManagerWindow = open;

  const NamedView* curView = CurrentNamedView(cmd);
  ImGui::TextUnformatted(curView ? ("Current view: " + curView->name).c_str() : "Current view: Unsaved View");
  ImGui::Separator();

  if (cmd.namedViews.empty()) {
    ImGui::TextDisabled("No saved views in this drawing.");
  } else {
    // Delete is deferred to after the loop: erasing from the vector being iterated is how a manager
    // dialog gets a crash that only reproduces when you delete the entry you are looking at.
    int deleteIdx = -1;
    if (ImGui::BeginTable("##viewsTable", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
      ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableSetupColumn("Orientation", ImGuiTableColumnFlags_WidthFixed, 130.f);
      ImGui::TableSetupColumn("Frame", ImGuiTableColumnFlags_WidthFixed, 70.f);
      ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 150.f);
      ImGui::TableHeadersRow();
      for (size_t i = 0; i < cmd.namedViews.size(); ++i) {
        const NamedView& v = cmd.namedViews[i];
        ImGui::TableNextRow();
        ImGui::PushID(static_cast<int>(i));
        ImGui::TableNextColumn();
        const bool isCur = (curView && curView->name == v.name);
        if (isCur)
          ImGui::TextColored(ImVec4(0.55f, 0.80f, 1.f, 1.f), "%s", v.name.c_str());
        else
          ImGui::TextUnformatted(v.name.c_str());
        ImGui::TableNextColumn();
        ImGui::Text("az %.1f  el %.1f", v.azimuthDeg, v.elevationDeg);
        ImGui::TableNextColumn();
        // Saying WCS vs UCS matters: it is the half of a restore a user does not otherwise see, and
        // the half that changes what their next typed coordinate means.
        ImGui::TextUnformatted(ucs::IsWorld(v.ucs) ? "WCS" : "UCS");
        ImGui::TableNextColumn();
        if (ImGui::SmallButton("Restore"))
          RestoreNamedView(cmd, v, *log);
        ImGui::SameLine();
        if (ImGui::SmallButton("Update"))
          ProcessViewCommandLine(cmd, std::string("S ") + v.name, *log);
        ImGui::SameLine();
        if (ImGui::SmallButton("Delete"))
          deleteIdx = static_cast<int>(i);
        ImGui::PopID();
      }
      ImGui::EndTable();
    }
    if (deleteIdx >= 0)
      ProcessViewCommandLine(cmd, std::string("D ") + cmd.namedViews[static_cast<size_t>(deleteIdx)].name, *log);
  }

  // ---- Named coordinate systems (REQ-154) ------------------------------------------------------
  // Restoring and deleting a saved UCS live HERE and nowhere else. `UCS Named` saves and only saves,
  // because saving is the one of the three that needs no list: you already have the frame in front
  // of you. The other two need to know what exists, and a command prompt cannot show you that - it
  // can only ask you to remember a name. So they moved to the place that already lists things.
  ImGui::Separator();
  ImGui::TextUnformatted("Named coordinate systems");
  if (cmd.ucsNamed.empty()) {
    ImGui::TextDisabled("No saved coordinate systems in this drawing. Save one with UCS N <name>.");
  } else {
    // Same deferred-delete shape as the views table above, and for the same reason: erasing from
    // the vector being iterated is how a manager dialog crashes on the row you are looking at.
    int ucsDeleteIdx = -1;
    if (ImGui::BeginTable("##ucsTable", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
      // The button column is sized to actually HOLD both buttons. A window this dialog has been
      // opened in before keeps its remembered size, not the 560 default, so the layout has to
      // survive the old 520 — and a Delete button clipped off the right edge is a function the user
      // cannot reach at all.
      ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 120.f);
      ImGui::TableSetupColumn("Frame", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 145.f);
      ImGui::TableHeadersRow();
      for (size_t i = 0; i < cmd.ucsNamed.size(); ++i) {
        const NamedUcs& n = cmd.ucsNamed[i];
        ImGui::TableNextRow();
        ImGui::PushID(static_cast<int>(1000 + i));
        ImGui::TableNextColumn();
        const bool isCur = ucs::FramesMatch(n.frame, cmd.activeUcs);
        if (isCur)
          ImGui::TextColored(ImVec4(0.55f, 0.80f, 1.f, 1.f), "%s", n.name.c_str());
        else
          ImGui::TextUnformatted(n.name.c_str());
        ImGui::TableNextColumn();
        // Clipped by the column, so hovering gives the whole thing back rather than making the user
        // widen the window to read six numbers.
        const std::string desc = DescribeUcs(n.frame);
        ImGui::TextUnformatted(desc.c_str());
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("%s", desc.c_str());
        ImGui::TableNextColumn();
        // Restoring the frame you are already in would do nothing but write a log line, so the
        // button says so rather than pretending to act.
        ImGui::BeginDisabled(isCur);
        if (ImGui::SmallButton("Restore"))
          RestoreNamedUcs(cmd, n.name, *log);
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::SmallButton("Delete"))
          ucsDeleteIdx = static_cast<int>(i);
        ImGui::PopID();
      }
      ImGui::EndTable();
    }
    if (ucsDeleteIdx >= 0)
      DeleteNamedUcs(cmd, cmd.ucsNamed[static_cast<size_t>(ucsDeleteIdx)].name, *log);
  }

  ImGui::Separator();
  if (ImGui::Button("New View...", ImVec2(120.f, 0.f)))
    cmd.showViewManagerNewPrompt = true;
  ImGui::SameLine();
  if (ImGui::Button("Close", ImVec2(90.f, 0.f)))
    cmd.showViewManagerWindow = false;
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
  BeginStyledDialog();
  cmd.showLayerManagerWindow = open;

  ImGui::TextWrapped(
      "Layers group objects for display and DXF. New geometry uses the current layer from the ribbon (top right). "
      "Layer 0 cannot be renamed or deleted.");
  ImGui::Separator();

  static char newLayerBuf[160] = "NewLayer";
  ImGui::InputText("New layer name", newLayerBuf, IM_ARRAYSIZE(newLayerBuf));
  ImGui::SameLine();
  if (StyledButton("Add layer", ImVec2(0, 0), /*primary=*/true)) {
    std::string err;
    if (CadAddDrawingLayer(cmd, std::string(newLayerBuf), &err))
      log->push_back("Layer added: " + TrimUi(std::string(newLayerBuf)));
    else
      log->push_back("LAYER — " + err);
  }

  ImGui::Separator();
  // REQ-046: "current viewport" the VP Freeze / VP Color columns act on (nullptr in model space or when
  // no single viewport is current → those columns are disabled).
  Viewport* vpCur = CurrentViewport(cmd);
  std::string pendingDeleteLayer;
  if (ImGui::BeginTable("laymgr", 13, kGridTableFlags, ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 16.f))) {
    ImGui::TableSetupScrollFreeze(1, 1);  // header AND the Name column stay put while scrolling
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultSort, 0.16f);
    ImGui::TableSetupColumn("On", ImGuiTableColumnFlags_WidthFixed, 36.f);
    ImGui::TableSetupColumn("Freeze", ImGuiTableColumnFlags_WidthFixed, 52.f);
    ImGui::TableSetupColumn("Lock", ImGuiTableColumnFlags_WidthFixed, 44.f);
    ImGui::TableSetupColumn("Plot", ImGuiTableColumnFlags_WidthFixed, 40.f);
    ImGui::TableSetupColumn("Current", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort, 64.f);
    ImGui::TableSetupColumn("Color", ImGuiTableColumnFlags_WidthStretch, 0.12f);
    ImGui::TableSetupColumn("Linetype", ImGuiTableColumnFlags_WidthStretch, 0.11f);
    ImGui::TableSetupColumn("Lineweight", ImGuiTableColumnFlags_WidthStretch, 0.10f);
    ImGui::TableSetupColumn("Transparency", ImGuiTableColumnFlags_WidthStretch, 0.10f);
    ImGui::TableSetupColumn("VP Freeze", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort, 64.f);
    ImGui::TableSetupColumn("VP Color", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoSort, 0.12f);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort, 72.f);
    ImGui::TableHeadersRow();

    // Sorted VIEW only — `drawingLayerTable`'s own order is left alone, for the
    // same reason as the Viewpoints grid: other code indexes into it.
    static std::vector<size_t> layOrder;
    layOrder.resize(cmd.drawingLayerTable.size());
    for (size_t k = 0; k < layOrder.size(); ++k)
      layOrder[k] = k;
    if (ImGuiTableSortSpecs* ss = ImGui::TableGetSortSpecs()) {
      if (ss->SpecsCount > 0) {
        const auto& L = cmd.drawingLayerTable;
        std::stable_sort(layOrder.begin(), layOrder.end(), [&](size_t a, size_t b) {
          for (int s = 0; s < ss->SpecsCount; ++s) {
            const ImGuiTableColumnSortSpecs& sp = ss->Specs[s];
            auto cmpBool = [](bool x, bool y) { return x == y ? 0 : (x ? 1 : -1); };
            int c = 0;
            switch (sp.ColumnIndex) {
              case 0: c = L[a].name.compare(L[b].name); break;
              case 1: c = cmpBool(L[a].on, L[b].on); break;
              case 2: c = cmpBool(L[a].frozen, L[b].frozen); break;
              case 3: c = cmpBool(L[a].locked, L[b].locked); break;
              case 4: c = cmpBool(L[a].plottable, L[b].plottable); break;
              case 6: c = L[a].color.compare(L[b].color); break;
              case 7: c = L[a].linetype.compare(L[b].linetype); break;
              case 8: c = (L[a].lineweightMm < L[b].lineweightMm) ? -1
                        : (L[a].lineweightMm > L[b].lineweightMm) ? 1 : 0; break;
              case 9: c = (L[a].transparency < L[b].transparency) ? -1
                        : (L[a].transparency > L[b].transparency) ? 1 : 0; break;
              default: break;
            }
            if (c != 0)
              return sp.SortDirection == ImGuiSortDirection_Ascending ? c < 0 : c > 0;
          }
          return a < b;
        });
      }
    }

    PushGridCellStyle();
    for (size_t k = 0; k < layOrder.size(); ++k) {
      const size_t i = layOrder[k];
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
        ImGui::SetNextItemWidth(-FLT_MIN);
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
      if (GridCheckbox("##on", &row.on))
        BumpCadGpuCache(cmd);
      ImGui::TableNextColumn();
      if (GridCheckbox("##fr", &row.frozen))
        BumpCadGpuCache(cmd);
      ImGui::TableNextColumn();
      if (GridCheckbox("##lk", &row.locked))
        BumpCadGpuCache(cmd);
      ImGui::TableNextColumn();
      if (GridCheckbox("##plot", &row.plottable))  // REQ-029/030: exclude from plots when off
        BumpCadGpuCache(cmd);
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Plottable — when off, this layer's geometry (and viewports on it) is excluded from plots.");
      ImGui::TableNextColumn();
      if (GridRadio("##cur", cmd.currentLayer == row.name)) {
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

      // REQ-046: VP Freeze — freeze/thaw this layer in the current viewport (disabled when none).
      ImGui::TableNextColumn();
      {
        ImGui::BeginDisabled(vpCur == nullptr);
        bool vpFrozen = vpCur && IsLayerFrozenInViewport(*vpCur, row.name);
        if (GridCheckbox("##vpfr", &vpFrozen) && vpCur) {
          if (vpFrozen)
            FreezeLayerInViewport(*vpCur, row.name);
          else
            ThawLayerInViewport(*vpCur, row.name);
          BumpCadGpuCache(cmd);
        }
        ImGui::EndDisabled();
      }

      // REQ-046: VP Color — per-viewport color override for this layer ("(none)" clears it).
      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(-1);
      {
        ImGui::BeginDisabled(vpCur == nullptr);
        const std::string* ov = vpCur ? ViewportLayerColorOverride(*vpCur, row.name) : nullptr;
        char vcprev[120];
        ImStrncpy(vcprev, ov ? ColorStorageToPreviewLabel(*ov).c_str() : "(none)", sizeof(vcprev));
        vcprev[sizeof(vcprev) - 1] = '\0';
        if (ImGui::BeginCombo("##vpcol", vcprev)) {
          if (ImGui::Selectable("(none)", ov == nullptr) && vpCur) {
            ClearViewportLayerColor(*vpCur, row.name);
            BumpCadGpuCache(cmd);
          }
          for (const auto& p : kNamedColors) {
            if (std::string(p.storage) == "ByLayer")
              continue;
            const bool sel = ov && *ov == p.storage;
            if (ImGui::Selectable(p.label, sel) && vpCur) {
              SetViewportLayerColor(*vpCur, row.name, p.storage);
              BumpCadGpuCache(cmd);
            }
            if (sel)
              ImGui::SetItemDefaultFocus();
          }
          ImGui::EndCombo();
        }
        ImGui::EndDisabled();
      }

      ImGui::TableNextColumn();
      if (row.name != "0") {
        // Deferred to after the table: deleting mid-iteration invalidated both
        // the row reference and the sort view built from the old indices.
        if (ImGui::SmallButton("Delete"))
          pendingDeleteLayer = row.name;
      } else {
        ImGui::TextDisabled("—");
      }

      ImGui::PopID();
    }
    PopGridCellStyle();
    ImGui::EndTable();
  }

  if (!pendingDeleteLayer.empty()) {
    std::string err;
    if (!CadDeleteDrawingLayer(cmd, pendingDeleteLayer, &err))
      log->push_back("LAYER — " + err);
  }

  ImGui::Separator();
  ImGui::TextDisabled(
      "On / Freeze / Lock are stored for future visibility and editing rules; all layers still draw. "
      "Color, linetype, lineweight, and transparency apply to entities set to ByLayer / defaults.");

  ImGui::End();
}

// Settings panel implementation lives in CadUiSettings.cpp.

// DELETED: duplicate BoxBegin and DrawSettingsPanel — do not re-add here.
