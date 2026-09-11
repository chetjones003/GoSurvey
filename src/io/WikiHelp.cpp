#include "WikiHelp.hpp"

#include "CadCommands.hpp"

#include <cctype>
#include <string>
#include <string_view>
#include <unordered_map>

namespace {

std::unordered_map<std::string, std::string> g_commandPageSlug;
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

  const auto it = g_commandPageSlug.find(primaryLower);
  if (it != g_commandPageSlug.end())
    out.pageSlug = it->second;
  else
    out.pageSlug = CommandPageSlug(primaryLower);
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
  if (!bundle.ok)
    return;

  for (const auto& [slug, page] : bundle.pagesBySlug) {
    (void)page;
    if (slug.rfind("Commands/", 0) != 0)
      continue;
    const std::string primary = slug.substr(std::string("Commands/").size());
    if (!primary.empty())
      g_commandPageSlug.emplace(primary, slug);
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
