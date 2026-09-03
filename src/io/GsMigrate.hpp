#pragma once

#include <nlohmann/json.hpp>

#include <cstddef>
#include <string>
#include <vector>

/// REQ-079 / ADR-030 — carrying an older `.gs` document forward to the current format version.
///
/// Pure: no window, no GL, no command layer, no filesystem. Steps operate on the parsed JSON
/// document tree *before* the typed loader sees it, which is what keeps a migration correct
/// forever — it is frozen against the shape it was written for, and cannot rot when the structs
/// it once matched are changed (ADR-030 (b)).

/// The `.gs` format version this build writes, and the target every migration chain climbs to.
///
/// Declared here rather than inside GsIo.cpp so it is reachable without dragging in the command
/// layer. The updater needs it: the release manifest carries the format version of the build
/// being offered, and comparing that against this one is what lets the update dialog tell a user
/// whether drawings saved by the new version will still open in the one they have (REQ-078).
constexpr int kGsFormatVersion = 4;

/// One single-version step. `fromVersion` is the version it upgrades FROM; it always produces
/// `fromVersion + 1` (ADR-030 (c)) — a step that jumps two versions is two steps.
struct GsMigrationStep {
  int         fromVersion;
  const char* description;   ///< shown in the load log, e.g. "circles gain a Z coordinate"
  /// Transforms \p doc in place. Returns false, with a reason in \p err, if the document cannot
  /// be carried forward — which is a genuinely breaking change and must be reported, never
  /// papered over with a best guess (REQ-201).
  bool (*apply)(nlohmann::json& doc, std::string& err);
};

/// The migrations this build knows about, in ascending `fromVersion` order.
/// Empty today: `.gs` is still at version 1 and every change so far has been additive, handled by
/// the tolerant-key pattern that ADR-030 (f) deliberately keeps.
const GsMigrationStep* GsMigrationTable(size_t& countOut);

/// Applies \p steps to bring \p doc from \p fileVersion up to \p targetVersion.
///
/// Separate from `MigrateGsDocument` so the composition logic — the part most likely to be wrong,
/// and the part that must keep working as versions accumulate — is reachable from tests with
/// synthetic step tables instead of only through whichever migrations happen to exist today
/// (ADR-030 (e)).
///
/// Returns false on: a file newer than \p targetVersion, a missing step in the chain, or a step
/// that reports failure. \p log receives one line per applied step.
bool ApplyGsMigrations(nlohmann::json&        doc,
                       int                    fileVersion,
                       int                    targetVersion,
                       const GsMigrationStep* steps,
                       size_t                 stepCount,
                       std::vector<std::string>& log,
                       std::string&              err);

/// Production entry point: applies `GsMigrationTable()` to reach \p targetVersion.
bool MigrateGsDocument(nlohmann::json&           doc,
                       int                       fileVersion,
                       int                       targetVersion,
                       std::vector<std::string>& log,
                       std::string&              err);
