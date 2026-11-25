#include "WikiHelp.hpp"

#include "CadCommands.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace {

std::unordered_map<std::string, std::string> g_commandPageSlug;
/// Command (lowercase) -> first non-command page with an all-caps heading naming it, e.g. "## REVCLOUD".
std::unordered_map<std::string, std::string> g_commandHeadingSlug;
std::string                                  g_uiHoverTooltip;
bool                                         g_uiHoverValid = false;

std::string ToLowerAscii(std::string s) {
  for (char& c : s)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

std::string CommandPageSlug(const std::string& primaryLower) {
  return std::string("Commands/") + primaryLower;
}

std::string ParseCommandBarPrimaryFromTooltip(std::string_view tooltip) {
  const size_t tag = tooltip.find("Command bar:");
  if (tag == std::string_view::npos)
    return {};

  std::string_view rest = tooltip.substr(tag + 12);
  while (!rest.empty() && std::isspace(static_cast<unsigned char>(rest.front())))
    rest.remove_prefix(1);

  size_t end = rest.size();
  for (size_t i = 0; i < rest.size(); ++i) {
    const char c = rest[i];
    if (c == '\n' || c == '\r' || c == ',')
      end = i;
    if (c == ' ' && i > 0) {
      const std::string_view tail = rest.substr(i + 1);
      if (tail.rfind("or ", 0) == 0)
        end = i;
    }
    if (end != rest.size())
      break;
  }

  std::string token(rest.substr(0, end));
  while (!token.empty() && std::isspace(static_cast<unsigned char>(token.back())))
    token.pop_back();
  return ToLowerAscii(token);
}

WikiHelpTarget TargetFromPrimary(const std::string& primaryLower) {
  WikiHelpTarget out;
  out.commandPrimary = primaryLower;
  if (primaryLower.empty()) {
    out.pageSlug = "Home";
    return out;
  }

  if (const auto it = g_commandPageSlug.find(primaryLower); it != g_commandPageSlug.end())
    out.pageSlug = it->second;
  else if (const auto heading = g_commandHeadingSlug.find(primaryLower); heading != g_commandHeadingSlug.end())
    out.pageSlug = heading->second;
  else if (g_commandPageSlug.empty() && g_commandHeadingSlug.empty())
    out.pageSlug = CommandPageSlug(primaryLower);  // no index built: keep the conventional page name
  else
    out.pageSlug = "Command-Reference";  // every command is listed there
  return out;
}

WikiHelpTarget TargetFromUiTooltip(std::string_view tooltip) {
  if (const std::string cmd = ParseCommandBarPrimaryFromTooltip(tooltip); !cmd.empty())
    return TargetFromPrimary(cmd);

  static const struct {
    const char* needle;
    const char* page;
  } kUiTopics[] = {
      {"Object snap", "Object-Snaps"},
      {"OSNAP (", "Object-Snaps"},
      {"ORTHO (", "Drafting-Aids"},
      {"POLAR (", "Drafting-Aids"},
      {"GRID (", "Drafting-Aids"},
      {"3D Object snap", "Object-Snaps"},
      {"Ortho mode", "Drafting-Aids"},
      {"Polar tracking", "Drafting-Aids"},
      {"Drawing grid", "Drafting-Aids"},
      {"Toolspace", "User-Interface"},
      {"application settings", "Settings-and-Options"},
      {"Layer Manager", "Layers"},
      {"Properties", "Properties"},
      {"ViewCube", "Views-and-Navigation"},
      {"Command line", "Command-Line"},
      {"Viewport scale", "Paper-Space-and-Layouts"},
      {"Multi Selection", "Object-Selection"},
      {"Layouts & spaces", "Paper-Space-and-Layouts"},
      {"Traverse", "Traverse-Editor"},
      {"Point Groups", "Point-Groups-and-Surfaces"},
      {"Surfaces", "Point-Groups-and-Surfaces"},
  };

  for (const auto& row : kUiTopics) {
    if (tooltip.find(row.needle) != std::string_view::npos) {
      WikiHelpTarget out;
      out.pageSlug = row.page;
      return out;
    }
  }

  return {};
}

}  // namespace

void WikiHelpBuildIndex(const WikiBundle& bundle) {
  g_commandPageSlug.clear();
  g_commandHeadingSlug.clear();
  if (!bundle.ok)
    return;

  std::vector<std::string> topicSlugs;
  for (const auto& [slug, page] : bundle.pagesBySlug) {
    (void)page;
    if (slug.rfind("Commands/", 0) != 0) {
      topicSlugs.push_back(slug);
      continue;
    }
    const std::string primary = slug.substr(std::string("Commands/").size());
    if (!primary.empty())
      g_commandPageSlug.emplace(primary, slug);
  }

  // Commands without their own page are documented under a heading on a topic page. Sorted so the
  // choice does not depend on hash order.
  std::sort(topicSlugs.begin(), topicSlugs.end());
  for (const std::string& slug : topicSlugs) {
    std::istringstream lines(bundle.pagesBySlug.at(slug).markdown);
    std::string line;
    while (std::getline(lines, line)) {
      size_t i = 0;
      while (i < line.size() && line[i] == '#')
        ++i;
      if (i == 0)
        continue;
      while (i < line.size() && (line[i] == ' ' || line[i] == '`'))
        ++i;
      size_t end = i;
      bool hasLetter = false, allCaps = true;
      while (end < line.size() && std::isalnum(static_cast<unsigned char>(line[end]))) {
        const unsigned char c = static_cast<unsigned char>(line[end]);
        if (std::isalpha(c)) {
          hasLetter = true;
          allCaps = allCaps && std::isupper(c);
        }
        ++end;
      }
      if (!hasLetter || !allCaps || end - i < 2)
        continue;
      g_commandHeadingSlug.emplace(ToLowerAscii(line.substr(i, end - i)), slug);
    }
  }
}

WikiHelpTarget WikiHelpLookupCommand(const std::string_view primaryLower) {
  return TargetFromPrimary(std::string(primaryLower));
}

void WikiHelpBeginFrame() {
  g_uiHoverValid = false;
  g_uiHoverTooltip.clear();
}

void WikiHelpNotifyUiHover(const std::string_view tooltipText) {
  if (tooltipText.empty())
    return;
  g_uiHoverTooltip = std::string(tooltipText);
  g_uiHoverValid   = true;
}

bool WikiHeadingMatchesCommand(const std::string_view headingLine,
                               const std::string_view commandPrimaryLower) {
  if (commandPrimaryLower.empty())
    return false;

  std::string heading;
  heading.reserve(headingLine.size());
  for (char c : headingLine) {
    if (c == '`')
      continue;
    heading.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
  }

  std::string cmd;
  cmd.reserve(commandPrimaryLower.size());
  for (char c : commandPrimaryLower)
    cmd.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));

  if (heading.size() < cmd.size())
    return false;
  if (heading.compare(0, cmd.size(), cmd) != 0)
    return false;

  if (heading.size() == cmd.size())
    return true;

  const char next = heading[cmd.size()];
  return next == ' ' || next == '(' || next == '/' || next == '-';
}

WikiHelpTarget WikiHelpTargetFromUiTooltip(std::string_view tooltipText) {
  return TargetFromUiTooltip(tooltipText);
}

bool WikiHelpConsumeUiHover(std::string* outTooltip) {
  if (!g_uiHoverValid)
    return false;
  if (outTooltip != nullptr)
    *outTooltip = g_uiHoverTooltip;
  return true;
}
