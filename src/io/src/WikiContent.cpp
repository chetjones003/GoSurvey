#include "WikiContent.hpp"

#include "AppPaths.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <regex>
#include <sstream>
#include <string_view>

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

bool StartsWith(const std::string& s, std::string_view prefix) {
  return s.compare(0, prefix.size(), prefix) == 0;
}

/// Command pages open with loose metadata lines ("**Command:** `LINE` — aliases: `L`",
/// "**Category overview:** [[Drawing Tools]]", "**Ribbon:** Draw → Line **Command:** …") that run
/// together into one paragraph, often beside the overview image. Rewrite them as a two-column
/// table directly under the title, with the image after it. The table's header row is left empty;
/// the help window drops it and styles the table as the page's info card.
std::string MetadataCard(const std::string& md) {
  std::vector<std::string> lines;
  {
    std::istringstream in(md);
    std::string line;
    while (std::getline(in, line)) {
      if (!line.empty() && line.back() == '\r')
        line.pop_back();
      lines.push_back(line);
    }
  }
  size_t title = 0;
  while (title < lines.size() && !StartsWith(lines[title], "# "))
    ++title;
  if (title == lines.size())
    return md;

  static constexpr std::string_view kCommand = "**Command:**", kCategory = "**Category overview:**",
                                    kRibbon = "**Ribbon:**", kAliases = " — aliases: ";
  std::string command, aliases, ribbon, category;
  std::vector<std::string> images;
  size_t end = title + 1;
  for (; end < lines.size(); ++end) {
    const std::string line = Trim(lines[end]);
    if (line.empty())
      continue;
    if (StartsWith(line, "![")) {
      images.push_back(line);
    } else if (StartsWith(line, kCommand)) {
      command = Trim(line.substr(kCommand.size()));
      if (const size_t at = command.find(kAliases); at != std::string::npos) {
        aliases = Trim(command.substr(at + kAliases.size()));
        command = Trim(command.substr(0, at));
      }
    } else if (StartsWith(line, kCategory)) {
      category = Trim(line.substr(kCategory.size()));
    } else if (StartsWith(line, kRibbon)) {
      ribbon = line.substr(kRibbon.size());
      if (const size_t at = ribbon.find(kCommand); at != std::string::npos)
        ribbon.resize(at);  // "Draw → Line **Command:** `LINE`" repeats the command row
      ribbon = Trim(ribbon);
    } else {
      break;
    }
  }
  if (command.empty() && category.empty() && ribbon.empty())
    return md;

  std::string out;
  for (size_t i = 0; i <= title; ++i)
    out += lines[i] + "\n";
  out += "\n|  |  |\n|---|---|\n";
  if (!command.empty())
    out += "| Command | " + command + " |\n";
  if (!aliases.empty())
    out += "| Aliases | " + aliases + " |\n";
  if (!ribbon.empty())
    out += "| Ribbon | " + ribbon + " |\n";
  if (!category.empty())
    out += "| Overview | " + category + " |\n";
  out += "\n";
  // The overview image follows the one-line description, so the page says what the command does
  // before showing it. With no plain paragraph first, the image goes straight after the card.
  size_t afterIntro = end;
  if (end < lines.size()) {
    const std::string first = Trim(lines[end]);
    const bool paragraph = !first.empty() && first[0] != '#' && first[0] != '|' && first[0] != '>' &&
                           !StartsWith(first, "```") && !StartsWith(first, "- ") && !StartsWith(first, "---");
    if (paragraph)
      while (afterIntro < lines.size() && !Trim(lines[afterIntro]).empty())
        ++afterIntro;
  }
  for (size_t i = end; i < afterIntro; ++i)
    out += lines[i] + "\n";
  if (!images.empty())
    out += "\n";
  for (const std::string& image : images)
    out += image + "\n\n";
  for (size_t i = afterIntro; i < lines.size(); ++i)
    out += lines[i] + "\n";
  return out;
}

std::string PrepareWikiMarkdown(const std::string& raw) {
  static const std::regex wikiLinkRe(R"(\[\[([^\]]+)\]\])");
  static const std::regex wikiImgRe(
      R"(https://raw\.githubusercontent\.com/wiki/chetjones003/GoSurvey/images/([A-Za-z0-9._-]+))");

  // [[Page]], [[Label|Page]]; a bare [[Commands/line]] reads as "LINE", not its path.
  std::string out = ReplaceAllRegex(MetadataCard(raw), wikiLinkRe, [](const std::smatch& m) {
    std::string inner = Trim(m[1].str());
    std::string label = inner;
    if (const size_t bar = inner.find('|'); bar != std::string::npos) {
      label = Trim(inner.substr(0, bar));
      inner = Trim(inner.substr(bar + 1));
    } else if (StartsWith(inner, "Commands/")) {
      label = inner.substr(std::string_view("Commands/").size());
      for (char& c : label)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return "[" + label + "](wiki:" + SlugFromWikiTitle(inner) + ")";
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
