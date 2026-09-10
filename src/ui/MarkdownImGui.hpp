#pragma once

#include <string_view>

/// REQ-336 / ADR-056 — draw CommonMark into the current ImGui window via vendored md4c.
/// Links open with the system browser (ShellExecute on Windows).
void DrawMarkdownImGui(std::string_view markdown);
