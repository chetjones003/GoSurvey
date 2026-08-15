// Stable entity identity (REQ-076 / ADR-027).
//
// The requirement exists because entities live in flat arrays that **compact on erase**, so an array
// index silently comes to mean a different entity after any earlier delete. Every test below is a
// restatement of one of REQ-076's acceptance conditions, and the load-bearing one is
// "resolves to nothing, not to its index successor" — that is the bug the whole feature prevents,
// and it would pass trivially against an index-based build only if you never erased anything.
//
// `CadCommands.cpp` cannot be linked by this target (it pulls in ImGui and the GUI stack), which is
// exactly why the rules live in the dependency-free `EntityId.cpp` and the state-facing wrappers
// there are only plumbing.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "commands/EntityId.hpp"

using json = nlohmann::json;

namespace {

/// A drawing's worth of attribute arrays, in the sweep order `CadCommands.cpp` uses.
struct FakeDrawing {
  std::vector<EntityAttributes> lines;
  std::vector<EntityAttributes> circles;
  std::vector<EntityAttributes> annotations;
  std::uint64_t nextId = 1;

  std::vector<std::vector<EntityAttributes>*> arrays() { return {&lines, &circles, &annotations}; }

  void sweep() { nextId = AssignMissingEntityIds(arrays(), nextId); }
};

std::vector<EntityAttributes> NAttrs(size_t n) { return std::vector<EntityAttributes>(n); }

/// Ids in an array, for order-sensitive comparisons.
std::vector<std::uint64_t> IdsOf(const std::vector<EntityAttributes>& v) {
  std::vector<std::uint64_t> out;
  out.reserve(v.size());
  for (const EntityAttributes& a : v)
    out.push_back(a.id);
  return out;
}

} // namespace

TEST_CASE("Sweep assigns an id to every entity and never hands out 0", "[entityid]") {
  FakeDrawing d;
  d.lines = NAttrs(3);
  d.circles = NAttrs(2);
  d.sweep();

  REQUIRE(IdsOf(d.lines) == std::vector<std::uint64_t>{1, 2, 3});
  REQUIRE(IdsOf(d.circles) == std::vector<std::uint64_t>{4, 5});
  REQUIRE(d.nextId == 6);
}

TEST_CASE("Sweep is idempotent — an assigned id is never renumbered", "[entityid]") {
  FakeDrawing d;
  d.lines = NAttrs(2);
  d.sweep();
  const std::vector<std::uint64_t> first = IdsOf(d.lines);

  d.sweep();
  d.sweep();
  REQUIRE(IdsOf(d.lines) == first);
  REQUIRE(d.nextId == 3);  // no id burned by the extra sweeps
}

TEST_CASE("A new entity beside assigned ones only fills the gap", "[entityid]") {
  FakeDrawing d;
  d.lines = NAttrs(2);
  d.sweep();
  d.lines.emplace_back();  // freshly created: id 0
  d.sweep();

  REQUIRE(IdsOf(d.lines) == std::vector<std::uint64_t>{1, 2, 3});
}

TEST_CASE("An erased entity's id resolves to nothing — NOT to its index successor", "[entityid]") {
  // The regression the requirement is built on. Before REQ-076 the reference was the index, so
  // after this erase a stored "1" designated what is now `attrs[1]` — a different entity, silently.
  FakeDrawing d;
  d.lines = NAttrs(3);
  d.sweep();
  const std::uint64_t idOfSecond = d.lines[1].id;
  const std::uint64_t idOfThird = d.lines[2].id;

  d.lines.erase(d.lines.begin() + 1);  // the array compacts: the third entity is now at index 1

  REQUIRE(FindEntityIndexById(d.lines, idOfSecond) == -1);
  REQUIRE(FindEntityIndexById(d.lines, idOfThird) == 1);
  // ...and specifically, the erased id did not come to mean the entity now occupying index 1.
  REQUIRE(d.lines[1].id != idOfSecond);
}

TEST_CASE("Ids are not reused after an erase", "[entityid]") {
  FakeDrawing d;
  d.lines = NAttrs(3);
  d.sweep();
  const std::uint64_t erasedId = d.lines[2].id;

  d.lines.pop_back();
  d.lines.emplace_back();  // a new entity created where the erased one was
  d.sweep();

  REQUIRE(d.lines[2].id != erasedId);
  REQUIRE(d.lines[2].id > erasedId);
}

TEST_CASE("The sweep never issues an id that is already present", "[entityid]") {
  // Guards the high-water pass. A restored undo frame, a paste, or a file from a newer session can
  // carry ids at or above a stale counter; issuing one again would alias two entities.
  FakeDrawing d;
  d.nextId = 1;                 // counter says "fresh drawing"...
  d.lines = NAttrs(2);
  d.lines[0].id = 500;          // ...but the content says otherwise
  d.lines[1].id = 0;
  d.sweep();

  REQUIRE(d.lines[0].id == 500);   // untouched
  REQUIRE(d.lines[1].id == 501);   // above the high-water mark, not 1
  REQUIRE(d.lines[1].id != d.lines[0].id);
}

TEST_CASE("Id 0 means unassigned and never resolves", "[entityid]") {
  std::vector<EntityAttributes> attrs = NAttrs(2);  // both id 0
  REQUIRE(FindEntityIndexById(attrs, 0) == -1);

  // Even after a sweep has given everyone a real id, a lookup of 0 still finds nothing.
  FakeDrawing d;
  d.lines = std::move(attrs);
  d.sweep();
  REQUIRE(FindEntityIndexById(d.lines, 0) == -1);
}

TEST_CASE("A pasted copy receives a new id, distinct from its source", "[entityid]") {
  FakeDrawing d;
  d.lines = NAttrs(2);
  d.sweep();
  const std::vector<std::uint64_t> sourceIds = IdsOf(d.lines);

  // Paste appends copies of the source attributes — ids and all — then clears them.
  const size_t firstPasted = d.lines.size();
  d.lines.push_back(d.lines[0]);
  d.lines.push_back(d.lines[1]);
  REQUIRE(d.lines[firstPasted].id == sourceIds[0]);  // the hazard, before the clear
  ClearEntityIdsFrom(d.lines, firstPasted);
  d.sweep();

  REQUIRE(d.lines[firstPasted].id != sourceIds[0]);
  REQUIRE(d.lines[firstPasted + 1].id != sourceIds[1]);
  // Each source id still resolves to its own entity, not to a copy.
  REQUIRE(FindEntityIndexById(d.lines, sourceIds[0]) == 0);
  REQUIRE(FindEntityIndexById(d.lines, sourceIds[1]) == 1);
}

TEST_CASE("A legacy drawing loads with the same ids every time", "[entityid][gs]") {
  // A pre-REQ-076 `.gs` has no ids at all, so they are assigned on load. Determinism comes from the
  // fixed array order — without it, the same file would produce different ids on different loads and
  // no stored reference could survive a save/load.
  auto loadLegacy = [] {
    FakeDrawing d;  // every entity id 0, counter at 1, exactly as a legacy file deserializes
    d.lines = NAttrs(4);
    d.circles = NAttrs(2);
    d.annotations = NAttrs(3);
    d.sweep();
    return d;
  };

  FakeDrawing first = loadLegacy();
  FakeDrawing second = loadLegacy();

  REQUIRE(IdsOf(first.lines) == IdsOf(second.lines));
  REQUIRE(IdsOf(first.circles) == IdsOf(second.circles));
  REQUIRE(IdsOf(first.annotations) == IdsOf(second.annotations));
  REQUIRE(first.nextId == second.nextId);
  // Sweep order is lines → circles → annotations, so the ids partition in that order.
  REQUIRE(IdsOf(first.lines) == std::vector<std::uint64_t>{1, 2, 3, 4});
  REQUIRE(IdsOf(first.circles) == std::vector<std::uint64_t>{5, 6});
  REQUIRE(IdsOf(first.annotations) == std::vector<std::uint64_t>{7, 8, 9});
}

TEST_CASE("An id survives .gs serialization exactly", "[entityid][gs]") {
  // GsIo.cpp cannot be linked here, so this exercises the serializer it writes ids with. A uint64
  // id is the one field where a float-ish or lossy round trip would be catastrophic rather than
  // merely imprecise: an id that changes value is an id that points at the wrong entity.
  const std::vector<std::uint64_t> ids = {1u,
                                          2u,
                                          1000000u,
                                          4294967295u,          // 2^32 - 1: past 32-bit range
                                          4294967296u,          // 2^32
                                          9007199254740993ull,  // 2^53 + 1: not representable as double
                                          18446744073709551615ull};  // uint64 max
  json o;
  o["ids"] = ids;
  const json back = json::parse(o.dump());
  REQUIRE(back["ids"].get<std::vector<std::uint64_t>>() == ids);
}

TEST_CASE("A legacy entity object with no id field loads as unassigned", "[entityid][gs]") {
  // Mirrors EntityAttributesFromJson's default. It must be 0 (→ swept) and never something that
  // could collide with a real id.
  const json legacy = json::parse(R"({"layer":"0","color":"ByLayer"})");
  REQUIRE(legacy.value("id", static_cast<std::uint64_t>(0)) == 0u);

  const json current = json::parse(R"({"id":42,"layer":"0"})");
  REQUIRE(current.value("id", static_cast<std::uint64_t>(0)) == 42u);
}
