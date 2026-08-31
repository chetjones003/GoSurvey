#pragma once

// REQ-308 / D-2026-08-30-c — the recent-drawings thumbnail cache: naming + bounded eviction.
// Header-only and pure (only <filesystem>/<string>/<cstdint>) so both the app and GoSurveyTests
// use the exact same rules with no GL or window. The actual pixel capture is
// ViewportRenderer::CaptureThumbnailBmp — this file never touches gl* (invariant §11.6).

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace thumbs {

/// Deterministic cache file name for a drawing path: FNV-1a (64-bit) of the path text, lower-hex,
/// plus ".bmp". Same drawing path always maps to the same file, so a re-save overwrites its own
/// thumbnail rather than leaking a new one.
inline std::string ThumbFileName(const std::string& drawingPath) {
  std::uint64_t h = 1469598103934665603ull;
  for (unsigned char c : drawingPath) {
    h ^= c;
    h *= 1099511628211ull;
  }
  char buf[17];
  for (int i = 15; i >= 0; --i) {
    buf[i] = "0123456789abcdef"[h & 0xf];
    h >>= 4;
  }
  buf[16] = '\0';
  return std::string(buf) + ".bmp";
}

/// Keep at most \p maxFiles *.bmp files in \p dir, deleting the least-recently-written first.
/// Silent and best-effort: a missing directory or an unreadable entry is skipped, never thrown.
inline void EvictThumbnails(const std::filesystem::path& dir, std::size_t maxFiles) {
  std::error_code ec;
  if (!std::filesystem::is_directory(dir, ec))
    return;

  std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> files;
  for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
    if (!it->is_regular_file(ec))
      continue;
    if (it->path().extension() != ".bmp")
      continue;
    std::error_code tec;
    const auto t = std::filesystem::last_write_time(it->path(), tec);
    if (tec)
      continue;
    files.emplace_back(t, it->path());
  }
  if (files.size() <= maxFiles)
    return;

  std::sort(files.begin(), files.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });  // oldest first
  const std::size_t toRemove = files.size() - maxFiles;
  for (std::size_t i = 0; i < toRemove; ++i) {
    std::error_code rec;
    std::filesystem::remove(files[i].second, rec);
  }
}

}  // namespace thumbs
