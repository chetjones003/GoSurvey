// ADR-0012 / Issue #1578: Single authoritative command registry backed by per-family modules.

#include "engine/CommandRegistry.hpp"
#include "CadCommands.hpp"
#include <GoSurvey/Core/StringUtil.hpp>

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string_view>

namespace stepengine {

namespace {

int FuzzySubsequenceScore(std::string_view query, std::string_view cand) {
  if (query.empty())
    return 0;
  size_t qi = 0;
  int score = 0;
  bool prevMatch = false;
  for (size_t i = 0; i < cand.size() && qi < query.size(); ++i) {
    const char qc = static_cast<char>(std::tolower(static_cast<unsigned char>(query[qi])));
    const char cc = static_cast<char>(std::tolower(static_cast<unsigned char>(cand[i])));
    if (qc == cc) {
      score += 10 + (prevMatch ? 5 : 0);
      prevMatch = true;
      ++qi;
    } else {
      prevMatch = false;
    }
  }
  if (qi != query.size())
    return -1;
  score += static_cast<int>(50 - cand.size());
  return score;
}

bool MatchesAliasList(const char* aliases, const std::string& lowerToken) {
  if (!aliases || aliases[0] == '\0')
    return false;
  std::istringstream als(aliases);
  std::string a;
  while (std::getline(als, a, ',')) {
    a = StringUtil::trimCopy(a);
    if (!a.empty() && lowerToken == StringUtil::toLowerAsciiCopy(a))
      return true;
  }
  return false;
}

} // namespace

const CommandRegistry& CommandRegistry::Instance() {
  static const CommandRegistry kInstance = [] {
    CommandRegistry reg;
    RegisterAllCommandModules(reg);
    return reg;
  }();
  return kInstance;
}

void CommandRegistry::Add(CommandDef def) {
  defs_.push_back(std::move(def));
}

const CommandDef* CommandRegistry::FindAny(const std::string& lowerToken) const {
  if (lowerToken.empty())
    return nullptr;
  for (const CommandDef& d : defs_) {
    if (lowerToken == StringUtil::toLowerAsciiCopy(d.primary))
      return &d;
  }
  for (const CommandDef& d : defs_) {
    if (MatchesAliasList(d.aliases, lowerToken))
      return &d;
  }
  return nullptr;
}

const CommandDef* CommandRegistry::FindListed(const std::string& lowerToken) const {
  if (lowerToken.empty())
    return nullptr;
  for (const CommandDef& d : defs_) {
    if (!d.listed)
      continue;
    if (lowerToken == StringUtil::toLowerAsciiCopy(d.primary))
      return &d;
  }
  for (const CommandDef& d : defs_) {
    if (!d.listed)
      continue;
    if (MatchesAliasList(d.aliases, lowerToken))
      return &d;
  }
  return nullptr;
}

const CommandDef* CommandRegistry::FindRepeatableByKind(CadCommandKind kind) const {
  if (kind == CadCommandKind::None)
    return nullptr;
  for (const CommandDef& d : defs_) {
    if (d.repeatable && d.kind == kind && d.execute != nullptr)
      return &d;
  }
  return nullptr;
}

// Per-family module registrars.
void RegisterModifyPilotCommands(CommandRegistry& reg);
void RegisterModifyRestCommands(CommandRegistry& reg);
void RegisterDrawCommands(CommandRegistry& reg);
void RegisterSolidCommands(CommandRegistry& reg);
void RegisterPipeSiteCommands(CommandRegistry& reg);
void RegisterSurfaceCommands(CommandRegistry& reg);
void RegisterAlignmentCommands(CommandRegistry& reg);
void RegisterUcsViewCommands(CommandRegistry& reg);
void RegisterGizmoGeoPropCommands(CommandRegistry& reg);
void RegisterBlocksCommands(CommandRegistry& reg);
void RegisterMiscCommands(CommandRegistry& reg);

void RegisterAllCommandModules(CommandRegistry& reg) {
  RegisterModifyPilotCommands(reg);
  RegisterModifyRestCommands(reg);
  RegisterDrawCommands(reg);
  RegisterSolidCommands(reg);
  RegisterPipeSiteCommands(reg);
  RegisterSurfaceCommands(reg);
  RegisterAlignmentCommands(reg);
  RegisterUcsViewCommands(reg);
  RegisterGizmoGeoPropCommands(reg);
  RegisterBlocksCommands(reg);
  RegisterMiscCommands(reg);
}

std::vector<std::string> RegistryFuzzyCommandMatches(const std::string& query, int maxResults) {
  struct Scored {
    std::string name;
    int score;
  };
  std::vector<Scored> ranked;
  const std::string qlow = StringUtil::toLowerAsciiCopy(StringUtil::trimCopy(query));
  if (qlow.empty())
    return {};
  for (const CommandDef& d : CommandRegistry::Instance().All()) {
    if (!d.listed)
      continue;
    const int sc = FuzzySubsequenceScore(qlow, d.primary);
    if (sc >= 0)
      ranked.push_back({d.primary, sc});
  }
  std::sort(ranked.begin(), ranked.end(), [](const Scored& a, const Scored& b) {
    if (a.score != b.score)
      return a.score > b.score;
    return a.name < b.name;
  });
  std::vector<std::string> out;
  for (size_t i = 0; i < ranked.size() && static_cast<int>(out.size()) < maxResults; ++i)
    out.push_back(ranked[i].name);
  return out;
}

std::vector<CommandSuggestion> RegistryFuzzyCommandSuggestions(const std::string& query, int maxResults) {
  struct Scored {
    const CommandDef* entry;
    int score;
    bool exactMatch;
  };
  std::vector<Scored> ranked;
  const std::string qlow = StringUtil::toLowerAsciiCopy(StringUtil::trimCopy(query));
  if (qlow.empty())
    return {};

  const auto isPrefix = [](const std::string& q, const std::string& s) {
    return s.size() >= q.size() && StringUtil::toLowerAsciiCopy(s).compare(0, q.size(), q) == 0;
  };
  for (const CommandDef& d : CommandRegistry::Instance().All()) {
    if (!d.listed)
      continue;
    int best = -1;
    const std::string nameLow = StringUtil::toLowerAsciiCopy(d.primary);
    bool exactMatch = nameLow == qlow;
    if (exactMatch)
      best = 5000000;
    else if (isPrefix(qlow, d.primary))
      best = 3000000 + FuzzySubsequenceScore(qlow, nameLow);
    else
      best = 1000000 + FuzzySubsequenceScore(qlow, nameLow);
    if (d.aliases && d.aliases[0] != '\0') {
      std::istringstream als(d.aliases);
      std::string a;
      while (std::getline(als, a, ',')) {
        a = StringUtil::trimCopy(a);
        if (a.empty())
          continue;
        const std::string aliasLow = StringUtil::toLowerAsciiCopy(a);
        if (aliasLow == qlow) {
          exactMatch = true;
          best = std::max(best, 4000000);
        } else if (isPrefix(qlow, a)) {
          best = std::max(best, 2000000 + FuzzySubsequenceScore(qlow, aliasLow));
        }
      }
    }
    if (best >= 1000000)
      ranked.push_back({&d, best, exactMatch});
  }
  std::sort(ranked.begin(), ranked.end(), [](const Scored& a, const Scored& b) {
    if (a.score != b.score)
      return a.score > b.score;
    return std::string(a.entry->primary) < std::string(b.entry->primary);
  });

  std::vector<CommandSuggestion> out;
  for (size_t i = 0; i < ranked.size() && static_cast<int>(out.size()) < maxResults; ++i) {
    CommandSuggestion s;
    s.name = ranked[i].entry->primary;
    for (char& ch : s.name)
      ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    s.description = ranked[i].entry->description ? ranked[i].entry->description : "";
    s.exactMatch = ranked[i].exactMatch;
    out.push_back(std::move(s));
  }
  return out;
}

} // namespace stepengine

std::vector<std::string> FuzzyCommandMatches(const std::string& query, int maxResults) {
  return stepengine::RegistryFuzzyCommandMatches(query, maxResults);
}

std::vector<CommandSuggestion> FuzzyCommandSuggestions(const std::string& query, int maxResults) {
  return stepengine::RegistryFuzzyCommandSuggestions(query, maxResults);
}

std::string CommandPrimaryForName(const std::string& name) {
  const std::string low = StringUtil::toLowerAsciiCopy(StringUtil::trimCopy(name));
  const stepengine::CommandDef* def = stepengine::CommandRegistry::Instance().FindAny(low);
  return def ? StringUtil::toLowerAsciiCopy(def->primary) : std::string();
}

std::string CommandPrimaryForKind(CadCommandKind kind) {
  if (kind == CadCommandKind::None)
    return {};
  const stepengine::CommandDef* only = nullptr;
  for (const stepengine::CommandDef& def : stepengine::CommandRegistry::Instance().All()) {
    if (def.kind != kind)
      continue;
    if (only)
      return {};
    only = &def;
  }
  return only ? StringUtil::toLowerAsciiCopy(only->primary) : std::string();
}

std::string ResolveCommandLineSubmission(const std::string& typedText) {
  const std::string trimmed = StringUtil::trimCopy(typedText);
  if (trimmed.empty() || trimmed.find_first_of(" \t") != std::string::npos)
    return typedText;
  const auto top = FuzzyCommandSuggestions(trimmed, 1);
  if (top.empty())
    return typedText;
  std::string resolved = top[0].name;
  for (char& ch : resolved)
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  return resolved;
}

std::vector<std::string> RegisteredCommandPrimariesForTest() {
  std::vector<std::string> out;
  const auto& all = stepengine::CommandRegistry::Instance().All();
  out.reserve(all.size());
  for (const stepengine::CommandDef& d : all) {
    if (d.listed)
      out.emplace_back(d.primary);
  }
  return out;
}

bool DispatchByPrimaryForTest(const std::string& primary, AppCommandState& st,
                              std::vector<std::string>& log) {
  const std::string low = StringUtil::toLowerAsciiCopy(StringUtil::trimCopy(primary));
  if (const stepengine::CommandDef* def = stepengine::CommandRegistry::Instance().FindAny(low);
      def && def->execute) {
    def->execute(st, {}, log);
    return true;
  }
  return false;
}
