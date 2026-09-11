#include "WhatsNewContent.hpp"

#include "AppPaths.hpp"
#include "Version.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

WhatsNewContent LoadWhatsNewContent() {
  WhatsNewContent out;
  const std::filesystem::path path =
      ResolveBundledAssetPath(std::filesystem::path("resources") / "whats-new.md");
  if (path.empty()) {
    out.fallbackMessage =
        std::string("Release notes file is missing (resources/whats-new.md).\n\n") +
        "GoSurvey " + GOSURVEY_VERSION_FULL;
    return out;
  }

  std::ifstream in(path, std::ios::binary);
  if (!in) {
    out.fallbackMessage =
        std::string("Could not read release notes at:\n") + path.string() + "\n\n" +
        "GoSurvey " + GOSURVEY_VERSION_FULL;
    return out;
  }

  std::ostringstream ss;
  ss << in.rdbuf();
  if (!in && !in.eof()) {
    out.fallbackMessage =
        std::string("Failed while reading release notes at:\n") + path.string() + "\n\n" +
        "GoSurvey " + GOSURVEY_VERSION_FULL;
    return out;
  }

  out.ok = true;
  out.markdown = ss.str();
  return out;
}
