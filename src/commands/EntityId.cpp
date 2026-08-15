#include "EntityId.hpp"

std::uint64_t AssignMissingEntityIds(const std::vector<std::vector<EntityAttributes>*>& arraysInSweepOrder,
                                     std::uint64_t nextId) {
  if (nextId == 0)
    nextId = 1;  // 0 is the "unassigned" sentinel and must never be handed out as an identity.

  for (const std::vector<EntityAttributes>* arr : arraysInSweepOrder) {
    if (!arr)
      continue;
    for (const EntityAttributes& att : *arr)
      if (att.id >= nextId)
        nextId = att.id + 1;
  }

  for (std::vector<EntityAttributes>* arr : arraysInSweepOrder) {
    if (!arr)
      continue;
    for (EntityAttributes& att : *arr)
      if (att.id == 0)
        att.id = nextId++;
  }
  return nextId;
}

int FindEntityIndexById(const std::vector<EntityAttributes>& attrs, std::uint64_t id) {
  if (id == 0)
    return -1;
  for (std::size_t i = 0; i < attrs.size(); ++i)
    if (attrs[i].id == id)
      return static_cast<int>(i);
  return -1;
}

void ClearEntityIdsFrom(std::vector<EntityAttributes>& attrs, std::size_t firstIndex) {
  for (std::size_t i = firstIndex; i < attrs.size(); ++i)
    attrs[i].id = 0;
}
