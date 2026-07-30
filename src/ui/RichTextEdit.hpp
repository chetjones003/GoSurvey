#pragma once

#include <imgui.h>

#include <string>

struct AppCommandState;

// WYSIWYG MTEXT edit widget (ADR-023). Replaces ImGui's InputTextMultiline, which cannot word-wrap and
// would show the raw [[b]]…[[/b]] wire tags. This one wraps at the MTEXT's column, grows with the text,
// renders bold/italic/underline/caps/per-run font and colour as formatting, and never shows a tag.
//
// The caret and selection anchor live in \c AppCommandState as **visible character indices**; the widget
// publishes the equivalent **raw byte offsets** into \c mtextRichEditorSelStart / \c mtextRichEditorSelEnd
// every frame, which is what keeps the existing formatting toolbar (B/I/U, font, colour) working unchanged.

/// Draw and drive the editor at the current ImGui cursor position.
/// \param boxW    column width in px — text wraps here (the ruler drag sets it)
/// \param fontPx  glyph size in px (the MTEXT's own on-screen size, floored to stay legible)
/// \param maxH    height cap; taller content scrolls to keep the caret visible
/// \param outH    receives the height actually used
/// \returns true if the buffer changed this frame.
bool RichTextEditDraw(const char* strId, AppCommandState& cmd, float boxW, float fontPx, ImU32 baseColor,
                      const std::string& baseFontFamily, float maxH, float* outH);
