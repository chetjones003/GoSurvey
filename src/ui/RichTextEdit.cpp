#include "RichTextEdit.hpp"

#include "CadCommands.hpp"
#include "FontRegistry.hpp"
#include "MtextRichSpans.hpp"
#include "MtextTextOps.hpp"
#include "RichTextLayout.hpp"
#include "ShxDraw.hpp"

#include <imgui_internal.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

// See RichTextEdit.hpp / ADR-023. Layout and caret math are pure and tested in RichTextLayoutTests; this
// file is the font-dependent half: measuring, drawing, and input.

namespace {

constexpr float kLineSpacing = 1.22f;  // matches MtextRichFormat's wrapped-draw line height
constexpr size_t kUndoDepth = 64;

/// True when a font name refers to an SHX stroke font. Local copy of the same predicate in
/// MtextRichFormat.cpp / CadUi.cpp — see the task log on why it is not hoisted into the Shx module.
bool IsShxFontName(const std::string& s) {
  if (s.size() < 4)
    return false;
  std::string ext = s.substr(s.size() - 4);
  for (char& c : ext)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return ext == ".shx";
}

/// The typeface a span draws in: its [[font:…]] override, else the annotation's base family, else the
/// UI font. Mirrors MtextRichFormat's resolution so the editor and the drawn MTEXT agree.
ImFont* ResolveSpanFont(const MtextRichSpan& sp, const std::string& baseFamily, ImFont* fallback,
                        bool* outRealBold, bool* outRealItalic) {
  *outRealBold = false;
  *outRealItalic = false;
  const std::string& fam = !sp.font.empty() ? sp.font : baseFamily;
  if (fam.empty())
    return fallback;
  ImFont* f = FontReg::Resolve(fam, sp.bold, sp.italic, outRealBold, outRealItalic);
  return f ? f : fallback;
}

/// The SHX stroke font a span draws in, or nullptr when it is a TrueType span. The editor must
/// resolve this the same way MtextRichFormat does, or an SHX run would be measured with TrueType
/// metrics here and drawn as strokes in the viewport — putting the caret and the wrap in the wrong
/// place the moment the two disagree.
Shx::Font* ResolveSpanShx(const MtextRichSpan& sp, const std::string& baseFamily) {
  const std::string& fam = !sp.font.empty() ? sp.font : baseFamily;
  if (!IsShxFontName(fam))
    return nullptr;
  Shx::Font* f = Shx::Resolve(fam);
  return (f && f->valid()) ? f : nullptr;
}

/// Width of one cell's glyph, through whichever font that span actually draws with.
float CellWidthPx(Shx::Font* sf, ImFont* f, float fontPx, const std::string& g) {
  if (sf)
    return Shx::MeasureWidthPx(*sf, g, fontPx);
  return f->CalcTextSizeA(fontPx, FLT_MAX, 0.f, g.c_str(), g.c_str() + g.size()).x;
}

ImU32 SpanColor(const MtextRichSpan& sp, ImU32 base) {
  if (!sp.hasColor)
    return base;
  const uint32_t rgb = sp.color;
  return IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, 255);
}

/// The bytes a cell displays — the raw character, upper-cased when its span is a [[caps]] run.
std::string CellGlyph(const std::string& wire, const richtext::Cell& c, bool caps) {
  std::string g = wire.substr(c.rawBegin, c.rawEnd - c.rawBegin);
  if (caps)
    MtextRichApplyCapsAscii(&g);
  return g;
}

void PushUndo(AppCommandState& cmd) {
  cmd.mtextEditUndo.push_back(cmd.mtextRichEditorBuf);
  if (cmd.mtextEditUndo.size() > kUndoDepth)
    cmd.mtextEditUndo.erase(cmd.mtextEditUndo.begin());
  cmd.mtextEditRedo.clear();
}

}  // namespace

bool RichTextEditDraw(const char* strId, AppCommandState& cmd, float boxW, float fontPx, ImU32 baseColor,
                      const std::string& baseFontFamily, float maxH, float* outH) {
  ImGuiWindow* window = ImGui::GetCurrentWindow();
  if (window->SkipItems) {
    if (outH)
      *outH = 0.f;
    return false;
  }
  ImGuiIO& io = ImGui::GetIO();
  ImFont* uiFont = ImGui::GetFont();
  const float lineH = std::max(4.f, fontPx * kLineSpacing);
  const float pad = 3.f;
  const float textW = std::max(8.f, boxW - pad * 2.f);
  bool changed = false;

  // ---------------- parse → cells → measure → wrap ----------------
  std::vector<MtextRichSpan> spans;
  MtextRichBuildSpans(cmd.mtextRichEditorBuf, &spans);
  std::vector<richtext::Cell> cells;
  richtext::BuildCells(cmd.mtextRichEditorBuf, spans, &cells);

  // Per-span font resolution is cached: a span is usually many characters, and Resolve does real work.
  std::vector<ImFont*> spanFont(spans.size(), uiFont);
  std::vector<Shx::Font*> spanShx(spans.size(), nullptr);
  std::vector<bool> spanFauxBold(spans.size(), false);
  for (size_t s = 0; s < spans.size(); ++s) {
    bool rb = false, ri = false;
    spanFont[s] = ResolveSpanFont(spans[s], baseFontFamily, uiFont, &rb, &ri);
    spanShx[s] = ResolveSpanShx(spans[s], baseFontFamily);
    spanFauxBold[s] = spans[s].bold && !rb && spanShx[s] == nullptr;  // SHX has no faux-bold strike
  }
  for (richtext::Cell& c : cells) {
    if (c.isNewline) {
      c.w = 0.f;
      continue;
    }
    const size_t si = static_cast<size_t>(std::max(0, c.spanIndex));
    ImFont* f = si < spanFont.size() ? spanFont[si] : uiFont;
    Shx::Font* sf = si < spanShx.size() ? spanShx[si] : nullptr;
    const bool caps = si < spans.size() && spans[si].caps;
    const std::string g = CellGlyph(cmd.mtextRichEditorBuf, c, caps);
    c.w = CellWidthPx(sf, f, fontPx, g);
    if (si < spanFauxBold.size() && spanFauxBold[si])
      c.w += std::max(0.5f, fontPx * 0.03f);  // faux-bold double-strike offset
  }
  const int lineCount = richtext::WrapCells(&cells, textW);
  const int nCells = static_cast<int>(cells.size());

  const float contentH = static_cast<float>(lineCount) * lineH;
  const float boxH = std::min(std::max(lineH, contentH) + pad * 2.f, std::max(lineH + pad * 2.f, maxH));

  // ---------------- item + focus ----------------
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const ImRect bb(origin, ImVec2(origin.x + boxW, origin.y + boxH));
  const ImGuiID id = window->GetID(strId);
  ImGui::ItemSize(ImVec2(boxW, boxH));
  if (!ImGui::ItemAdd(bb, id)) {
    if (outH)
      *outH = boxH;
    return false;
  }
  const bool hovered = ImGui::ItemHoverable(bb, id, 0);
  if (hovered)
    ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);

  // Focus is our own flag, NOT ImGui's ActiveID. Holding ActiveID across frames makes ImGui's
  // ItemHoverable reject every other item in the application — no buttons, no dragging, not even the
  // window close box. So the editor keeps focus for the whole editing session on its own, and only
  // borrows ActiveID while a drag-select is in progress (below), which is when blocking other items is
  // the correct behavior anyway.
  if (cmd.mtextRichEditorFocusRequest) {
    cmd.mtextEditFocused = true;
    ImGui::FocusWindow(window);
    cmd.mtextEditCaret = nCells;  // opening an existing MTEXT puts the caret at the end
    cmd.mtextEditAnchor = nCells;
    cmd.mtextRichEditorFocusRequest = false;
  }
  if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    cmd.mtextEditFocused = true;

  // Yield the keyboard whenever some other widget is genuinely active (a toolbar combo being dragged,
  // another text field). A momentary button press returns ActiveID to 0 on release, so typing resumes by
  // itself — which is what lets the user click B and keep typing.
  const ImGuiID activeId = ImGui::GetActiveID();
  const bool focused = cmd.mtextEditFocused && (activeId == 0 || activeId == id);
  if (focused) {
    // Claim the keyboard so the command bar's type-to-focus does not steal our characters.
    io.WantTextInput = true;
    io.WantCaptureKeyboard = true;
  }

  auto clampCaret = [&](int v) { return v < 0 ? 0 : (v > nCells ? nCells : v); };
  cmd.mtextEditCaret = clampCaret(cmd.mtextEditCaret);
  cmd.mtextEditAnchor = clampCaret(cmd.mtextEditAnchor);

  // Visual position of a caret index: it sits *before* that cell, so a caret at the first cell of a
  // wrapped line shows at the start of that line.
  auto caretXY = [&](int caret, float* cx, int* cline) {
    if (cells.empty()) {
      *cx = 0.f;
      *cline = 0;
      return;
    }
    if (caret < nCells) {
      *cx = cells[static_cast<size_t>(caret)].x;
      *cline = cells[static_cast<size_t>(caret)].line;
      return;
    }
    const richtext::Cell& last = cells.back();
    if (last.isNewline) {
      *cx = 0.f;
      *cline = last.line + 1;
    } else {
      *cx = last.x + last.w;
      *cline = last.line;
    }
  };

  // ---------------- mouse ----------------
  const ImVec2 local(io.MousePos.x - (origin.x + pad), io.MousePos.y - (origin.y + pad) + cmd.mtextEditScrollY);
  if (focused && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    const int hit = richtext::CaretFromPoint(cells, lineCount, lineH, local.x, local.y);
    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
      int a = 0, b = 0;
      richtext::WordBounds(cells, hit, &a, &b);
      cmd.mtextEditAnchor = a;
      cmd.mtextEditCaret = b;
    } else {
      cmd.mtextEditCaret = hit;
      if (!io.KeyShift)
        cmd.mtextEditAnchor = hit;
      cmd.mtextEditMouseSelecting = true;
      ImGui::SetActiveID(id, window);  // held only while the drag lasts, released below
    }
    cmd.mtextEditBlinkT = ImGui::GetTime();
  }
  if (cmd.mtextEditMouseSelecting) {
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      cmd.mtextEditCaret = richtext::CaretFromPoint(cells, lineCount, lineH, local.x, local.y);
    } else {
      cmd.mtextEditMouseSelecting = false;
      if (ImGui::GetActiveID() == id)
        ImGui::ClearActiveID();  // never outlive the drag, or the rest of the UI goes dead
    }
  }

  // ---------------- keyboard ----------------
  auto selMin = [&]() { return std::min(cmd.mtextEditCaret, cmd.mtextEditAnchor); };
  auto selMax = [&]() { return std::max(cmd.mtextEditCaret, cmd.mtextEditAnchor); };
  auto hasSel = [&]() { return cmd.mtextEditCaret != cmd.mtextEditAnchor; };
  auto selectedText = [&]() -> std::string {
    if (!hasSel())
      return {};
    size_t ra = 0, rb = 0;
    richtext::SelectionRawRange(cells, selMin(), selMax(), &ra, &rb);
    return cmd.mtextRichEditorBuf.substr(ra, rb - ra);
  };
  // Erase the selection from the buffer; returns the visible index the caret lands on.
  auto eraseSelection = [&]() -> int {
    if (!hasSel())
      return cmd.mtextEditCaret;
    size_t ra = 0, rb = 0;
    const int a = selMin();
    richtext::SelectionRawRange(cells, a, selMax(), &ra, &rb);
    cmd.mtextRichEditorBuf.erase(ra, rb - ra);
    return a;
  };

  if (focused) {
    const bool ctrl = io.KeyCtrl;
    const bool shift = io.KeyShift;
    auto moveCaret = [&](int to) {
      cmd.mtextEditCaret = clampCaret(to);
      if (!shift)
        cmd.mtextEditAnchor = cmd.mtextEditCaret;
      cmd.mtextEditBlinkT = ImGui::GetTime();
    };

    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true))
      moveCaret(cmd.mtextEditCaret - 1);
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, true))
      moveCaret(cmd.mtextEditCaret + 1);
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true) || ImGui::IsKeyPressed(ImGuiKey_DownArrow, true)) {
      const bool up = ImGui::IsKeyPressed(ImGuiKey_UpArrow, true);
      float cx = 0.f;
      int cl = 0;
      caretXY(cmd.mtextEditCaret, &cx, &cl);
      const int target = std::clamp(cl + (up ? -1 : 1), 0, lineCount - 1);
      moveCaret(richtext::CaretFromPoint(cells, lineCount, lineH, cx,
                                         (static_cast<float>(target) + 0.5f) * lineH));
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Home, false)) {
      float cx = 0.f;
      int cl = 0;
      caretXY(cmd.mtextEditCaret, &cx, &cl);
      moveCaret(richtext::CaretAtLineStart(cells, cl));
    }
    if (ImGui::IsKeyPressed(ImGuiKey_End, false)) {
      float cx = 0.f;
      int cl = 0;
      caretXY(cmd.mtextEditCaret, &cx, &cl);
      moveCaret(richtext::CaretFromPoint(cells, lineCount, lineH, 1.e9f,
                                         (static_cast<float>(cl) + 0.5f) * lineH));
    }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_A, false)) {
      cmd.mtextEditAnchor = 0;
      cmd.mtextEditCaret = nCells;
    }

    // --- editing ---
    if (ImGui::IsKeyPressed(ImGuiKey_Backspace, true)) {
      if (hasSel()) {
        PushUndo(cmd);
        moveCaret(eraseSelection());
        cmd.mtextEditAnchor = cmd.mtextEditCaret;
        changed = true;
      } else if (cmd.mtextEditCaret > 0) {
        PushUndo(cmd);
        const richtext::Cell& c = cells[static_cast<size_t>(cmd.mtextEditCaret - 1)];
        cmd.mtextRichEditorBuf.erase(c.rawBegin, c.rawEnd - c.rawBegin);
        moveCaret(cmd.mtextEditCaret - 1);
        cmd.mtextEditAnchor = cmd.mtextEditCaret;
        changed = true;
      }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Delete, true)) {
      if (hasSel()) {
        PushUndo(cmd);
        moveCaret(eraseSelection());
        cmd.mtextEditAnchor = cmd.mtextEditCaret;
        changed = true;
      } else if (cmd.mtextEditCaret < nCells) {
        PushUndo(cmd);
        const richtext::Cell& c = cells[static_cast<size_t>(cmd.mtextEditCaret)];
        cmd.mtextRichEditorBuf.erase(c.rawBegin, c.rawEnd - c.rawBegin);
        changed = true;
      }
    }

    // Insert UTF-8 at the caret, replacing any selection. Typed text inherits the styling to its left,
    // which InsertOffset delivers by landing inside the preceding run (ADR-023 (d)).
    auto insertText = [&](const std::string& utf8, int visibleCount) {
      if (utf8.empty())
        return;
      PushUndo(cmd);
      int at = cmd.mtextEditCaret;
      if (hasSel()) {
        at = eraseSelection();
        // The erase invalidated the cells; re-derive the insertion point from the fresh buffer.
        std::vector<MtextRichSpan> s2;
        MtextRichBuildSpans(cmd.mtextRichEditorBuf, &s2);
        std::vector<richtext::Cell> c2;
        richtext::BuildCells(cmd.mtextRichEditorBuf, s2, &c2);
        const size_t off = richtext::InsertOffset(c2, cmd.mtextRichEditorBuf.size(), at);
        cmd.mtextRichEditorBuf.insert(off, utf8);
      } else {
        const size_t off = richtext::InsertOffset(cells, cmd.mtextRichEditorBuf.size(), at);
        cmd.mtextRichEditorBuf.insert(off, utf8);
      }
      cmd.mtextEditCaret = at + visibleCount;
      cmd.mtextEditAnchor = cmd.mtextEditCaret;
      cmd.mtextEditBlinkT = ImGui::GetTime();
      changed = true;
      // A word is "finished" when the character that ends it is a separator; correct it then, so the fix
      // lands once rather than on every keystroke inside the word.
      if (cmd.mtextEditAutocorrectCapsLock && !utf8.empty()) {
        const char last = utf8.back();
        if (last == ' ' || last == '\n' || last == '\t' || last == '.' || last == ',') {
          std::vector<MtextRichSpan> s3;
          MtextRichBuildSpans(cmd.mtextRichEditorBuf, &s3);
          std::vector<richtext::Cell> c3;
          richtext::BuildCells(cmd.mtextRichEditorBuf, s3, &c3);
          const int sepIdx = cmd.mtextEditCaret - 1;  // the separator we just inserted
          if (sepIdx >= 0 && sepIdx < static_cast<int>(c3.size()))
            mtextops::AutocorrectCapsLockWord(cmd.mtextRichEditorBuf, c3[static_cast<size_t>(sepIdx)].rawBegin);
        }
      }
    };

    if (ImGui::IsKeyPressed(ImGuiKey_Enter, true) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, true)) {
      if (!ctrl)  // Ctrl+Enter is the normalize shortcut, handled by the caller
        insertText("\n", 1);
    }

    // --- clipboard ---
    if (ctrl && (ImGui::IsKeyPressed(ImGuiKey_C, false) || ImGui::IsKeyPressed(ImGuiKey_X, false))) {
      const std::string sel = selectedText();
      if (!sel.empty()) {
        // Copy the plain visible text, not the tags: what is pasted elsewhere is what was seen.
        std::vector<MtextRichSpan> ss;
        MtextRichBuildSpans(sel, &ss);
        std::string plain;
        for (const auto& s : ss)
          plain += sel.substr(s.rawBegin, s.rawEnd - s.rawBegin);
        ImGui::SetClipboardText(plain.c_str());
        if (ImGui::IsKeyPressed(ImGuiKey_X, false)) {
          PushUndo(cmd);
          const int at = eraseSelection();
          cmd.mtextEditCaret = at;
          cmd.mtextEditAnchor = at;
          changed = true;
        }
      }
    }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_V, false)) {
      if (const char* clip = ImGui::GetClipboardText()) {
        // Paste as literal text: strip anything that would be read back as a control tag.
        std::string t(clip);
        std::string safe;
        safe.reserve(t.size());
        for (size_t i = 0; i < t.size(); ++i) {
          if (t[i] == '\r')
            continue;
          if (t[i] == '[' && i + 1 < t.size() && t[i + 1] == '[') {
            safe += '[';  // collapse "[[" so pasted text can never inject a tag
            ++i;
            continue;
          }
          safe += t[i];
        }
        int visible = 0;
        for (size_t i = 0; i < safe.size();)
          i += richtext::Utf8CharLen(safe, i), ++visible;
        insertText(safe, visible);
      }
    }

    // --- in-editor undo/redo (the drawing-level undo only fires on commit) ---
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z, false) && !cmd.mtextEditUndo.empty()) {
      cmd.mtextEditRedo.push_back(cmd.mtextRichEditorBuf);
      cmd.mtextRichEditorBuf = cmd.mtextEditUndo.back();
      cmd.mtextEditUndo.pop_back();
      changed = true;
    }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Y, false) && !cmd.mtextEditRedo.empty()) {
      cmd.mtextEditUndo.push_back(cmd.mtextRichEditorBuf);
      cmd.mtextRichEditorBuf = cmd.mtextEditRedo.back();
      cmd.mtextEditRedo.pop_back();
      changed = true;
    }

    // --- typed characters ---
    if (!io.InputQueueCharacters.empty()) {
      std::string typed;
      int visible = 0;
      for (int k = 0; k < io.InputQueueCharacters.Size; ++k) {
        unsigned int ch = static_cast<unsigned int>(io.InputQueueCharacters[k]);
        if (ch < 32 && ch != '\t')
          continue;  // control characters: Enter/Backspace are handled as keys above
        if (cmd.mtextRichEditorTypingAllCaps && ch >= 'a' && ch <= 'z')
          ch = ch - 'a' + 'A';
        char buf[5] = {0};
        ImTextCharToUtf8(buf, ch);
        typed += buf;
        ++visible;
      }
      io.InputQueueCharacters.resize(0);  // consumed: nothing later this frame should see them
      if (!typed.empty())
        insertText(typed, visible);
    }
  }

  // A buffer edit invalidated the layout; the draw below would use stale cells, so re-run it.
  if (changed) {
    MtextRichBuildSpans(cmd.mtextRichEditorBuf, &spans);
    richtext::BuildCells(cmd.mtextRichEditorBuf, spans, &cells);
    spanFont.assign(spans.size(), uiFont);
    spanShx.assign(spans.size(), nullptr);
    spanFauxBold.assign(spans.size(), false);
    for (size_t s = 0; s < spans.size(); ++s) {
      bool rb = false, ri = false;
      spanFont[s] = ResolveSpanFont(spans[s], baseFontFamily, uiFont, &rb, &ri);
      spanShx[s] = ResolveSpanShx(spans[s], baseFontFamily);
      spanFauxBold[s] = spans[s].bold && !rb && spanShx[s] == nullptr;
    }
    for (richtext::Cell& c : cells) {
      if (c.isNewline) {
        c.w = 0.f;
        continue;
      }
      const size_t si = static_cast<size_t>(std::max(0, c.spanIndex));
      ImFont* f = si < spanFont.size() ? spanFont[si] : uiFont;
      Shx::Font* sf = si < spanShx.size() ? spanShx[si] : nullptr;
      const bool caps = si < spans.size() && spans[si].caps;
      const std::string g = CellGlyph(cmd.mtextRichEditorBuf, c, caps);
      c.w = CellWidthPx(sf, f, fontPx, g);
    }
    richtext::WrapCells(&cells, textW);
    cmd.mtextEditCaret = std::clamp(cmd.mtextEditCaret, 0, static_cast<int>(cells.size()));
    cmd.mtextEditAnchor = std::clamp(cmd.mtextEditAnchor, 0, static_cast<int>(cells.size()));
  }

  // ---------------- scroll the caret into view ----------------
  {
    float cx = 0.f;
    int cl = 0;
    caretXY(cmd.mtextEditCaret, &cx, &cl);
    const float viewH = boxH - pad * 2.f;
    const float caretTop = static_cast<float>(cl) * lineH;
    if (caretTop < cmd.mtextEditScrollY)
      cmd.mtextEditScrollY = caretTop;
    else if (caretTop + lineH > cmd.mtextEditScrollY + viewH)
      cmd.mtextEditScrollY = caretTop + lineH - viewH;
    const float maxScroll = std::max(0.f, contentH - viewH);
    cmd.mtextEditScrollY = std::clamp(cmd.mtextEditScrollY, 0.f, maxScroll);
  }

  // ---------------- draw ----------------
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(bb.Min, bb.Max, IM_COL32(58, 58, 58, 240));
  dl->AddRect(bb.Min, bb.Max, focused ? IM_COL32(124, 160, 196, 255) : IM_COL32(120, 124, 130, 255));
  // Vertical extent clips (this is a scrolling region), horizontal does not. The box width decides
  // where lines WRAP; a single word too long to fit cannot be wrapped, and the committed MTEXT lets it
  // overhang — so the editor must too, or the text would appear cut off here and whole after commit.
  // There is no horizontal scroll to reveal it otherwise. The generous X range is intersected with the
  // current clip, so this can never paint outside the window.
  dl->PushClipRect(ImVec2(bb.Min.x + 1.f - 4096.f, bb.Min.y + 1.f),
                   ImVec2(bb.Max.x - 1.f + 4096.f, bb.Max.y - 1.f), true);

  const ImVec2 textOrigin(origin.x + pad, origin.y + pad - cmd.mtextEditScrollY);
  const int a = selMin(), b = selMax();

  // Selection highlight, one rect per line it covers.
  if (a != b) {
    for (int ln = 0; ln < lineCount; ++ln) {
      float x0 = -1.f, x1 = -1.f;
      for (int j = a; j < b && j < nCells; ++j) {
        const richtext::Cell& c = cells[static_cast<size_t>(j)];
        if (c.line != ln)
          continue;
        const float cw = c.isNewline ? fontPx * 0.35f : c.w;  // a selected break shows a small stub
        if (x0 < 0.f)
          x0 = c.x;
        x1 = c.x + cw;
      }
      if (x0 >= 0.f)
        dl->AddRectFilled(ImVec2(textOrigin.x + x0, textOrigin.y + static_cast<float>(ln) * lineH),
                          ImVec2(textOrigin.x + x1, textOrigin.y + static_cast<float>(ln + 1) * lineH),
                          IM_COL32(58, 96, 158, 200));
    }
  }

  // Glyphs, per character in its span's style.
  for (const richtext::Cell& c : cells) {
    if (c.isNewline)
      continue;
    const size_t si = static_cast<size_t>(std::max(0, c.spanIndex));
    if (si >= spans.size())
      continue;
    const MtextRichSpan& sp = spans[si];
    ImFont* f = spanFont[si];
    Shx::Font* sf = spanShx[si];
    const ImU32 col = SpanColor(sp, baseColor);
    const std::string g = CellGlyph(cmd.mtextRichEditorBuf, c, sp.caps);
    const ImVec2 at(textOrigin.x + c.x, textOrigin.y + static_cast<float>(c.line) * lineH);
    if (sf) {
      // Stroke the glyph from the .shx, baseline one cap-height below the cell's top-left — the same
      // anchoring MtextRichFormat uses, so what is typed matches what is drawn after commit.
      Shx::DrawText(dl, *sf, ImVec2(at.x, at.y + fontPx), fontPx, 0.f, col, g,
                    std::max(1.f, fontPx * 0.05f));
    } else {
      dl->AddText(f, fontPx, at, col, g.c_str(), g.c_str() + g.size());
      if (spanFauxBold[si])  // no bold face available: double-strike a hair to the right
        dl->AddText(f, fontPx, ImVec2(at.x + std::max(0.5f, fontPx * 0.03f), at.y), col, g.c_str(),
                    g.c_str() + g.size());
    }
    if (sp.underline) {
      const float uy = at.y + fontPx * 1.02f;
      dl->AddLine(ImVec2(at.x, uy), ImVec2(at.x + c.w, uy), col, std::max(1.f, fontPx * 0.06f));
    }
  }

  // Caret — solid while typing, blinking when idle.
  if (focused) {
    const double dt = ImGui::GetTime() - cmd.mtextEditBlinkT;
    if (dt < 0.4 || std::fmod(dt, 1.0) < 0.6) {
      float cx = 0.f;
      int cl = 0;
      caretXY(cmd.mtextEditCaret, &cx, &cl);
      const float y = textOrigin.y + static_cast<float>(cl) * lineH;
      dl->AddLine(ImVec2(textOrigin.x + cx, y), ImVec2(textOrigin.x + cx, y + lineH),
                  IM_COL32(235, 240, 246, 255), 1.2f);
    }
  }
  dl->PopClipRect();

  // ---------------- publish raw offsets for the formatting toolbar ----------------
  // This is the load-bearing line of ADR-023: B/I/U, the font picker, and the colour swatch all wrap a
  // raw byte range, so they keep working untouched as long as these stay in sync with the visible caret.
  size_t rawA = 0, rawB = 0;
  richtext::SelectionRawRange(cells, a, b, &rawA, &rawB);
  cmd.mtextRichEditorSelStart = static_cast<int>(rawA);
  cmd.mtextRichEditorSelEnd = static_cast<int>(rawB);
  cmd.mtextRichEditorCursor =
      static_cast<int>(richtext::InsertOffset(cells, cmd.mtextRichEditorBuf.size(), cmd.mtextEditCaret));

  if (outH)
    *outH = boxH;
  return changed;
}
