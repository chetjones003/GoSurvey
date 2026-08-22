// Right-click customization and object isolation (REQ-084).
//
// Two rules carry the feature's correctness, and both are pure:
//
//   1. `CadIsolationHiddenSet` decides WHAT ISOLATEOBJECTS hides. Get the difference backwards, or
//      lose the sortedness, and the command hides the selection instead of the rest of the drawing
//      — or hides nothing at all, because every gate reaches it through a `binary_search` that
//      silently returns garbage on an unsorted range.
//   2. `CadRightClickHoldIsMenu` decides whether a press was an ENTER or a shortcut menu. The
//      boundary case is the one worth pinning: exactly at the threshold must belong to one verdict,
//      not to neither.
//
// `CadCommands.cpp` — which owns the commands themselves — pulls in ImGui and the GUI stack and
// cannot be linked by this target, which is why both rules live inline in the header. The same
// constraint shapes `EntityIdTests.cpp`; see its note.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

#include "commands/CadCommands.hpp"

namespace {
using Ids = std::vector<std::uint64_t>;
}

// --- CadEntityIdHidden -----------------------------------------------------------------------

TEST_CASE("Nothing is hidden when the isolation set is empty", "[req084][isolate]") {
  const Ids none;
  CHECK_FALSE(CadEntityIdHidden(&none, 1));
  CHECK_FALSE(CadEntityIdHidden(&none, 999));
  // The renderer passes nullptr whenever there is no extended geometry at all.
  CHECK_FALSE(CadEntityIdHidden(nullptr, 7));
}

TEST_CASE("Membership is by id, and id 0 is never hidden", "[req084][isolate]") {
  const Ids hidden{2, 4, 6, 8};
  CHECK(CadEntityIdHidden(&hidden, 2));
  CHECK(CadEntityIdHidden(&hidden, 8));
  CHECK_FALSE(CadEntityIdHidden(&hidden, 5));
  CHECK_FALSE(CadEntityIdHidden(&hidden, 9));
  // 0 means "id not yet assigned by the sweep" (REQ-076). An unassigned entity is brand new, and
  // treating it as a member of any set would make it vanish the moment it was drawn.
  CHECK_FALSE(CadEntityIdHidden(&hidden, 0));
}

// --- CadIsolationHiddenSet -------------------------------------------------------------------

TEST_CASE("Isolate hides everything except the selection", "[req084][isolate]") {
  const Ids all{1, 2, 3, 4, 5};
  const Ids keep{2, 4};
  const Ids hide = CadIsolationHiddenSet(all, keep);
  CHECK(hide == Ids{1, 3, 5});
  // The kept objects must still be pickable — that is the whole point of isolating them.
  CHECK_FALSE(CadEntityIdHidden(&hide, 2));
  CHECK_FALSE(CadEntityIdHidden(&hide, 4));
  CHECK(CadEntityIdHidden(&hide, 3));
}

TEST_CASE("The hidden set comes back sorted from unsorted input", "[req084][isolate]") {
  // Every gate reaches this set through binary_search, so an unsorted result is not merely untidy —
  // it makes membership answers arbitrary. Attribute arrays are in creation order, not id order,
  // once anything has been erased and re-added.
  const Ids all{9, 1, 7, 3, 5};
  const Ids keep{7};
  const Ids hide = CadIsolationHiddenSet(all, keep);
  REQUIRE(hide == Ids{1, 3, 5, 9});
  for (std::uint64_t id : hide)
    CHECK(CadEntityIdHidden(&hide, id));
}

TEST_CASE("Duplicate ids collapse rather than repeating", "[req084][isolate]") {
  const Ids all{4, 4, 1, 1, 1, 2};
  const Ids keep{1, 1};
  CHECK(CadIsolationHiddenSet(all, keep) == Ids{2, 4});
}

TEST_CASE("Isolating the whole drawing hides nothing", "[req084][isolate]") {
  const Ids all{1, 2, 3};
  CHECK(CadIsolationHiddenSet(all, all).empty());
}

TEST_CASE("A kept id that is not in the drawing hides nothing extra", "[req084][isolate]") {
  // The selection can name an entity that has since been erased; that must not push some other
  // object into the hidden set by miscounting the difference.
  const Ids all{1, 2, 3};
  const Ids keep{2, 42};
  CHECK(CadIsolationHiddenSet(all, keep) == Ids{1, 3});
}

TEST_CASE("Isolating in an empty drawing hides nothing", "[req084][isolate]") {
  CHECK(CadIsolationHiddenSet(Ids{}, Ids{1, 2}).empty());
}

// --- CadRightClickHoldIsMenu -----------------------------------------------------------------

TEST_CASE("A quick right-click is an ENTER, a held one is the shortcut menu", "[req084][rightclick]") {
  constexpr int kMs = 250;  // the shipped default
  CHECK_FALSE(CadRightClickHoldIsMenu(0.0, kMs));
  CHECK_FALSE(CadRightClickHoldIsMenu(60.0, kMs));
  CHECK_FALSE(CadRightClickHoldIsMenu(249.9, kMs));
  CHECK(CadRightClickHoldIsMenu(250.0, kMs));   // the boundary belongs to the menu, not to neither
  CHECK(CadRightClickHoldIsMenu(1200.0, kMs));
}

TEST_CASE("The threshold is the configured duration, not a constant", "[req084][rightclick]") {
  // A user who sets a long duration must not still get the menu at 250 ms.
  CHECK_FALSE(CadRightClickHoldIsMenu(300.0, AppCommandState::kRightClickMaxMs));
  CHECK(CadRightClickHoldIsMenu(300.0, AppCommandState::kRightClickMinMs));
}

TEST_CASE("Time-sensitive right-click ships off, at AutoCAD's default duration", "[req084][rightclick]") {
  // REQ-084 (a): an existing profile must not acquire the behaviour on upgrade. UserPrefs leaves
  // both fields at their compiled defaults when the keys are absent, so this default IS the
  // upgrade behaviour.
  const AppCommandState fresh;
  CHECK_FALSE(fresh.rightClickTimeSensitive);
  CHECK(fresh.rightClickLongerClickMs == 250);
  CHECK(fresh.rightClickLongerClickMs >= AppCommandState::kRightClickMinMs);
  CHECK(fresh.rightClickLongerClickMs <= AppCommandState::kRightClickMaxMs);
  // And nothing is isolated in a fresh drawing (REQ-084 (d): isolation is session state).
  CHECK(fresh.hiddenEntityIds.empty());
}
