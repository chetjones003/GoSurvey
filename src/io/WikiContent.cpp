#include "WikiContent.hpp"

#include "AppPaths.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <regex>
#include <sstream>

namespace {

namespace fs = std::filesystem;

std::string ReadFileUtf8(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in)
    return {};
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

std::string Trim(std::string s) {
  auto notSpace = [](unsigned char c) { return !std::isspace(c); };
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
  s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
  return s;
}

std::string SlugFromFilename(const fs::path& file) {
  return file.stem().string();
}

std::string TitleFromMarkdown(const std::string& md, const std::string& slugFallback) {
  std::istringstream in(md);
  std::string        line;
  while (std::getline(in, line)) {
    line = Trim(line);
    if (line.size() >= 2 && line[0] == '#' && line[1] == ' ') {
      line.erase(0, 2);
      return Trim(line);
    }
  }
  return slugFallback;
}

std::string SlugFromWikiTitle(std::string_view title) {
  std::string out;
  out.reserve(title.size());
  for (size_t i = 0; i < title.size(); ++i) {
    const char c = title[i];
    if (c == ' ')
      out.push_back('-');
    else
      out.push_back(c);
  }
  return out;
}

std::string ReplaceAllRegex(const std::string& input, const std::regex& re,
                            const std::function<std::string(const std::smatch&)>& repl) {
  std::string out;
  out.reserve(input.size());
  std::sregex_iterator it(input.begin(), input.end(), re);
  const std::sregex_iterator end;
  size_t last = 0;
  for (; it != end; ++it) {
    const std::smatch& m = *it;
    out.append(input, last, static_cast<size_t>(m.position()) - last);
    out.append(repl(m));
    last = static_cast<size_t>(m.position() + m.length());
  }
  out.append(input, last, std::string::npos);
  return out;
}

std::string PrepareWikiMarkdown(const std::string& raw) {
  static const std::regex wikiLinkRe(R"(\[\[([^\]]+)\]\])");
  static const std::regex wikiImgRe(
      R"(https://raw\.githubusercontent\.com/wiki/chetjones003/GoSurvey/images/([A-Za-z0-9._-]+))");

  std::string out = ReplaceAllRegex(raw, wikiLinkRe, [](const std::smatch& m) {
    const std::string title = Trim(m[1].str());
    const std::string slug  = SlugFromWikiTitle(title);
    return "[" + title + "](wiki:" + slug + ")";
  });

  out = ReplaceAllRegex(out, wikiImgRe, [](const std::smatch& m) {
    return std::string("wiki-img:") + m[1].str();
  });

  return out;
}

void ParseSidebar(const std::string& sidebar, std::vector<WikiNavEntry>& nav) {
  std::istringstream in(sidebar);
  std::string        line;
  while (std::getline(in, line)) {
    line = Trim(line);
    if (line.empty())
      continue;

    static const std::regex itemRe(R"(^-\s*\[\[([^\]]+)\]\])");
    static const std::regex boldLinkRe(R"(^\*\*\[\[([^\]]+)\]\]\*\*)");
    static const std::regex headerRe(R"(^#{1,3}\s*(.+)$)");
    static const std::regex boldRe(R"(^\*\*([^*]+)\*\*)");

    std::smatch m;
    if (std::regex_match(line, m, itemRe)) {
      const std::string title = Trim(m[1].str());
      nav.push_back({title, SlugFromWikiTitle(title), false});
      continue;
    }
    if (std::regex_match(line, m, boldLinkRe)) {
      const std::string title = Trim(m[1].str());
      nav.push_back({title, SlugFromWikiTitle(title), false});
      continue;
    }
    if (std::regex_match(line, m, headerRe)) {
      nav.push_back({Trim(m[1].str()), {}, true});
      continue;
    }
    if (std::regex_match(line, m, boldRe)) {
      nav.push_back({Trim(m[1].str()), {}, true});
    }
  }
}

}  // namespace

std::string WikiTitleToSlug(std::string_view titleOrSlug) {
  if (titleOrSlug.find(' ') != std::string_view::npos)
    return SlugFromWikiTitle(titleOrSlug);
  return std::string(titleOrSlug);
}

WikiBundle LoadWikiBundle() {
  WikiBundle bundle;
  const fs::path root =
      ResolveBundledAssetPath(fs::path("resources") / "wiki");
  if (root.empty() || !fs::is_directory(root)) {
    bundle.fallbackMessage =
        "User manual folder is missing (resources/wiki).\n\n"
        "The manual is shipped with GoSurvey; reinstall if this folder is absent.";
    return bundle;
  }

  bundle.rootPath = root.string();

  const fs::path sidebarPath = root / "_Sidebar.md";
  if (fs::is_regular_file(sidebarPath))
    ParseSidebar(ReadFileUtf8(sidebarPath), bundle.nav);

  for (const fs::directory_entry& entry : fs::recursive_directory_iterator(root)) {
    if (!entry.is_regular_file())
      continue;
    const fs::path file = entry.path();
    if (file.extension() != ".md")
      continue;
    const std::string filename = file.filename().string();
    if (filename.empty() || filename[0] == '_')
      continue;

    const std::string raw = ReadFileUtf8(file);
    if (raw.empty())
      continue;

    std::string slug = fs::relative(file, root).generic_string();
    if (slug.size() >= 3 && slug.compare(slug.size() - 3, 3, ".md") == 0)
      slug.resize(slug.size() - 3);

    WikiPage page;
    page.slug     = slug;
    page.title    = TitleFromMarkdown(raw, SlugFromFilename(file));
    page.markdown = PrepareWikiMarkdown(raw);
    bundle.pagesBySlug.emplace(page.slug, std::move(page));
  }

  if (bundle.nav.empty()) {
    for (const auto& [slug, page] : bundle.pagesBySlug)
      bundle.nav.push_back({page.title, slug, false});
  }

  if (bundle.pagesBySlug.empty()) {
    bundle.fallbackMessage =
        "No user-manual pages were found in resources/wiki.";
    return bundle;
  }

  bundle.ok = true;
  return bundle;
}
