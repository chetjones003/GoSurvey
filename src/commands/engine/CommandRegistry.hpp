#pragma once
// ADR-0012 / Issue #1578: Single command registry built from command modules.
// Replaces kRegistry[], DispatchByPrimary(), idle command-name chains, and RepeatLastCommand().

#include "engine/CommandSession.hpp"

#include <string>
#include <vector>

struct AppCommandState;
struct CommandSuggestion;

namespace stepengine {

/// One command definition contributed by a command module.
struct CommandDef {
  const char* primary = "";      ///< Lowercase canonical command name (e.g. "move").
  const char* aliases = "";      ///< Comma-separated aliases (e.g. "m").
  const char* description = "";  ///< Human-readable description for autocomplete and HELP.
  CadCommandKind kind = CadCommandKind::None;
  bool repeatable = false;       ///< True if RepeatLastCommand can re-invoke this command.
  /// True if listed in autocomplete, HELP, and RegisteredCommandPrimariesForTest().
  /// False for internal/inline-only commands not advertised in the public registry.
  bool listed = true;
  /// Runs the command with optional trailing arguments (`args` is trimmed; empty for bare invocation).
  void (*execute)(AppCommandState& st, const std::string& args, std::vector<std::string>& log) = nullptr;
};

/// The single authoritative command registry, populated once from every command family module.
class CommandRegistry {
 public:
  static const CommandRegistry& Instance();

  void Add(CommandDef def);

  [[nodiscard]] const std::vector<CommandDef>& All() const { return defs_; }
  /// Finds an entry (listed or unlisted) matching `lowerToken` by primary name or alias.
  [[nodiscard]] const CommandDef* FindAny(const std::string& lowerToken) const;
  /// Finds a listed entry matching `lowerToken` by primary name or alias.
  [[nodiscard]] const CommandDef* FindListed(const std::string& lowerToken) const;
  /// Finds the repeatable entry whose `kind` matches `kind`.
  [[nodiscard]] const CommandDef* FindRepeatableByKind(CadCommandKind kind) const;

 private:
  std::vector<CommandDef> defs_;
};

/// Populates `reg` from every command module family. Implemented in CommandRegistry.cpp.
void RegisterAllCommandModules(CommandRegistry& reg);

} // namespace stepengine
