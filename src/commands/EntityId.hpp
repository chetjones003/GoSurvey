#pragma once

/// Stable entity identity (REQ-076 / ADR-027) — the pure half.
///
/// Deliberately its own translation unit, like `DwgProbe.cpp` and `CadLinetype.cpp`: it depends on
/// nothing but `<cstdint>`, `<vector>` and `EntityAttributes`, so `GoSurveyTests` links it without
/// the GUI stack. The rules this file encodes — never reuse an id, never renumber an assigned one,
/// resolve to *nothing* rather than to an index successor — are exactly the ones REQ-076 exists to
/// guarantee, so they must be testable, and the GUI-facing wrappers in `CadCommands.cpp` are only
/// the plumbing that hands these functions the right arrays.

#include <cstdint>
#include <vector>

#include "CadEntities.hpp"

/// Sentinel for `AppCommandState::entityIdSweepRevision` meaning "never swept — sweep on the next
/// call". It is deliberately outside the range of the 32-bit `cadGpuRevision` it gets compared
/// against, so ordinary editing can never reach it and accidentally certify a drawing as swept.
inline constexpr std::uint64_t kEntityIdSweepNever = ~static_cast<std::uint64_t>(0);

/// Raise \p nextId above every id already present, then assign it to every entity whose id is 0.
///
/// \param arraysInSweepOrder the drawing's attribute arrays, in a **fixed** order. The order is what
///        makes assignment deterministic, which is what lets a legacy `.gs` load with the same ids
///        every time (REQ-076). Null entries are skipped.
/// \param nextId the drawing's counter.
/// \return the counter after assignment — always >= \p nextId, never less.
///
/// The high-water pass matters: an undo frame, a paste, or a file written by a newer session can
/// carry ids at or above the counter, and handing one out again would alias two entities — the exact
/// failure the requirement exists to prevent. Idempotent: a second call assigns nothing.
std::uint64_t AssignMissingEntityIds(const std::vector<std::vector<EntityAttributes>*>& arraysInSweepOrder,
                                     std::uint64_t nextId);

/// Index of the entity carrying \p id within \p attrs, or -1 if it is not there.
///
/// Returns -1 for id 0, which means "unassigned" and is never a valid reference target.
[[nodiscard]] int FindEntityIndexById(const std::vector<EntityAttributes>& attrs, std::uint64_t id);

/// Zero the ids from \p firstIndex onward, so a later sweep issues fresh ones.
///
/// Used on paste: a pasted entity is a *different* entity and must not carry its source's identity.
void ClearEntityIdsFrom(std::vector<EntityAttributes>& attrs, std::size_t firstIndex);
