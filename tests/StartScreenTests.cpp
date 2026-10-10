// REQ-308 — the pure parts of the Start-screen feature: the drawing-tab index sentinel invariant,
// the tab-close guard, and the thumbnail-cache naming + eviction rules. The three-column panel
// itself is ImGui and manual-verify (project norm — no ImGui automation).

#include "CadCommands.hpp"
#include "ThumbnailCache.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>

TEST_CASE("a fresh AppCommandState opens on the Start tab, Drawing 1 after it", "[req308]") {
  AppCommandState cmd;
  REQUIRE(cmd.drawingTabs.size() == 2);
  CHECK(cmd.drawingTabs[0].name == "Start");
  CHECK(cmd.drawingTabs[FirstDrawingTabIndex()].name == "Drawing 1");
  CHECK(cmd.activeDrawingIdx == 0);        // Start is active on launch
  CHECK(cmd.prevDrawingIdx == 0);
  CHECK(cmd.documents.size() == cmd.drawingTabs.size());  // index-aligned, incl. the phantom slot
  CHECK(FirstDrawingTabIndex() == 1);
}

TEST_CASE("the tab-close guard never closes the Start tab or the last drawing", "[req308]") {
  // Mirrors CadUi.cpp's guard: closeable iff  i >= FirstDrawingTabIndex() && drawingTabs.size() > 2
  auto closeable = [](int i, int tabCount) {
    return i >= FirstDrawingTabIndex() && tabCount > 2;
  };
  CHECK_FALSE(closeable(0, 3));   // Start, even with drawings open
  CHECK_FALSE(closeable(1, 2));   // the only drawing ([Start, Drawing 1])
  CHECK(closeable(1, 3));         // one of two drawings
  CHECK(closeable(2, 3));
}

TEST_CASE("thumbnail file names are deterministic and path-specific", "[req308]") {
  const std::string a = thumbs::ThumbFileName("C:/jobs/site.dwg");
  CHECK(a == thumbs::ThumbFileName("C:/jobs/site.dwg"));
  CHECK(a != thumbs::ThumbFileName("C:/jobs/other.dwg"));
  CHECK(a.size() == 20);                 // 16 hex + ".bmp"
  CHECK(a.substr(a.size() - 4) == ".bmp");
}

TEST_CASE("EvictThumbnails keeps the newest files and tolerates a missing dir", "[req308]") {
  const auto dir = std::filesystem::temp_directory_path() / "gosurvey-thumb-evict-test";
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);

  // Missing directory: no throw, no-op.
  CHECK_NOTHROW(thumbs::EvictThumbnails(dir, 4));

  std::filesystem::create_directories(dir, ec);
  for (int i = 0; i < 8; ++i) {
    std::ofstream(dir / (std::to_string(i) + ".bmp")) << "x";
    std::filesystem::last_write_time(
        dir / (std::to_string(i) + ".bmp"),
        std::filesystem::file_time_type::clock::now() + std::chrono::seconds(i), ec);
  }
  std::ofstream(dir / "keep.txt") << "not a bmp";

  thumbs::EvictThumbnails(dir, 3);

  int bmps = 0;
  for (auto& e : std::filesystem::directory_iterator(dir))
    if (e.path().extension() == ".bmp")
      ++bmps;
  CHECK(bmps == 3);
  CHECK(std::filesystem::exists(dir / "7.bmp"));   // newest survive
  CHECK(std::filesystem::exists(dir / "6.bmp"));
  CHECK(std::filesystem::exists(dir / "5.bmp"));
  CHECK_FALSE(std::filesystem::exists(dir / "0.bmp"));
  CHECK(std::filesystem::exists(dir / "keep.txt"));  // non-bmp untouched

  std::filesystem::remove_all(dir, ec);
}
