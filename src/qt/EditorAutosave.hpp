#pragma once

#include "Autosave.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

class QTimer;
class QWidget;

/// Runtime autosave state owned by EditorWindow (composition-root level, not persisted in
/// AppCommandState).
struct EditorAutosaveState {
  std::filesystem::path rootDir;
  std::string           sessionId;
  std::uint32_t         pid = 0;
  std::int64_t          startedAtUnix = 0;
  QTimer*               timer = nullptr;
  QTimer*               retryTimer = nullptr;
  bool                  started = false;
  /// Last cadGpuRevision successfully written to disk per DrawingTab::uid in this session.
  std::unordered_map<std::uint32_t, std::uint32_t> lastAutosavedRevision;
  /// Old crashed-session recovery entries that have been opened into a tab but not yet superseded
  /// by a new save or current-session autosave. Keyed by the recovered tab's DrawingTab::uid.
  std::unordered_map<std::uint32_t, autosave::RecoveryEntry> pendingRecoveredSources;
};

struct AutosaveRecoveryChoice {
  enum class Action { Later, Recover, DiscardAll } action = Action::Later;
  std::vector<int> selectedRows;
};

/// Shows the Drawing Recovery modal dialog for \p entries and returns the user's choice.
AutosaveRecoveryChoice RunAutosaveRecoveryDialog(QWidget* parent,
                                                 const std::filesystem::path& autosaveRoot,
                                                 const std::vector<autosave::RecoveryEntry>& entries,
                                                 bool userInitiated);

/// Formats a recovered tab display name with a single " (Recovered)" suffix.
std::string MakeRecoveredTabName(const std::string& rawName);
