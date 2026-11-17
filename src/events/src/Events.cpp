#include <GoSurvey/Events/Events.hpp>

#include <mutex>
#include <unordered_map>
#include <vector>

namespace GoSurvey::Events {

struct EventBus::State {
  struct Slot {
    std::uint64_t id;
    std::function<void(const void*)> callback;
  };

  std::mutex mutex;
  std::uint64_t nextId = 1;
  std::unordered_map<std::type_index, std::vector<Slot>> handlers;
};

EventBus::EventBus() : state_(std::make_shared<State>()) {}

EventBus::~EventBus() = default;

std::uint64_t EventBus::SubscribeImpl(std::type_index key, std::function<void(const void*)> callback) {
  std::lock_guard lock(state_->mutex);
  const auto id = state_->nextId++;
  state_->handlers[key].push_back({id, std::move(callback)});
  return id;
}

void EventBus::UnsubscribeImpl(const std::shared_ptr<State>& state, std::type_index key, std::uint64_t id) {
  std::lock_guard lock(state->mutex);
  const auto found = state->handlers.find(key);
  if (found == state->handlers.end())
    return;

  auto& slots = found->second;
  for (auto it = slots.begin(); it != slots.end(); ++it) {
    if (it->id == id) {
      slots.erase(it);
      break;
    }
  }
  if (slots.empty())
    state->handlers.erase(found);
}

void EventBus::PublishImpl(std::type_index key, const void* event) const {
  std::vector<std::function<void(const void*)>> callbacks;
  {
    std::lock_guard lock(state_->mutex);
    const auto found = state_->handlers.find(key);
    if (found == state_->handlers.end())
      return;
    callbacks.reserve(found->second.size());
    for (const auto& slot : found->second)
      callbacks.push_back(slot.callback);
  }

  for (const auto& callback : callbacks)
    callback(event);
}

}  // namespace GoSurvey::Events
