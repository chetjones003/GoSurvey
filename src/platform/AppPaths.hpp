#pragma once

#include <filesystem>

// Where the application's files live. Pure <filesystem> and OS environment queries — no GL, no
// window.
//
// These paths are shared by the Qt application and headless tools without pulling in graphics code.
//
// Deliberately NOT solved with a second headless implementation the way the WinFileDialogs and
// PdfAttach seams are (ADR-031 (b′)): those functions genuinely differ without a desktop, whereas
// these are identical in both builds. A seam here would duplicate real logic and let the two copies
// drift. Same reasoning, and the same shape, as ADR-022 moving ShxFont down to a shared layer.
//

/// Directory containing the executable, or empty if unknown.
std::filesystem::path AppExecutableDirectory();

/// Per-user application data directory (e.g. %APPDATA%\GoSurvey on Windows).
/// Returns an empty path if it cannot be determined.
std::filesystem::path UserDataDirectory();

/// Tries `<exe>/relativePath` then `<cwd>/relativePath`. \p relativePath may include subdirs
/// (e.g. `icons/logo.png`). Returns an empty path when neither exists.
std::filesystem::path ResolveBundledAssetPath(const std::filesystem::path& relativePath);

/// Bundled app logo for splash, title bar, and window icon: `icons/bitmap.png`, then `bitmap.png`
/// beside the exe or cwd.
std::filesystem::path ResolveAppLogoPngPath();

/// Default startup workspace template: `resources/default-template.gst` beside the executable (or cwd).
[[nodiscard]] std::filesystem::path ResolveDefaultWorkspaceTemplateGstPath();
