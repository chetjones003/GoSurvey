#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <typeindex>
#include <typeinfo>
#include <utility>

namespace GoSurvey::Events {

enum class EventPhase : std::uint8_t { Begin, Update, End };
enum class TextInputPhase : std::uint8_t { Composition, Commit };
enum class DocumentChangeKind : std::uint8_t { Geometry, Properties, Structure, SavedState };
enum class CommandStateChangeKind : std::uint8_t { ActiveCommand, Prompt, Selection, Mode };

struct PointerEvent {
  float x = 0.0f;
  float y = 0.0f;
  std::uint32_t buttons = 0;
  std::uint32_t modifiers = 0;
  EventPhase phase = EventPhase::Update;
};

struct WheelEvent {
  float x = 0.0f;
  float y = 0.0f;
  float deltaX = 0.0f;
  float deltaY = 0.0f;
  std::uint32_t modifiers = 0;
};

struct KeyEvent {
  std::uint32_t key = 0;
  std::uint32_t modifiers = 0;
  EventPhase phase = EventPhase::Begin;
};

struct TextInputEvent {
  std::string text;
  TextInputPhase phase = TextInputPhase::Commit;
};

struct DocumentToken {
  std::uint64_t value = 0;
  friend bool operator==(DocumentToken a, DocumentToken b) { return a.value == b.value; }
};

struct DocumentChangedEvent {
  DocumentToken document;
  DocumentChangeKind kind = DocumentChangeKind::Geometry;
  std::uint64_t revision = 0;
};

struct CommandStateChangedEvent {
  CommandStateChangeKind kind = CommandStateChangeKind::ActiveCommand;
  std::uint64_t generation = 0;
};

class EventSubscription {
 public:
  EventSubscription() = default;
  explicit EventSubscription(std::function<void()> reset) : reset_(std::move(reset)) {}
  EventSubscription(const EventSubscription&) = delete;
  EventSubscription& operator=(const EventSubscription&) = delete;
  EventSubscription(EventSubscription&& other) noexcept : reset_(std::move(other.reset_)) { other.reset_ = {}; }
  EventSubscription& operator=(EventSubscription&& other) noexcept {
    if (this != &other) {
      Reset();
      reset_ = std::move(other.reset_);
      other.reset_ = {};
    }
    return *this;
  }
  ~EventSubscription() { Reset(); }

  void Reset() {
    if (auto reset = std::exchange(reset_, {}))
      reset();
  }

 private:
  std::function<void()> reset_;
};

/// Instance-owned, toolkit-neutral event bus. Callbacks run outside the internal lock.
class EventBus {
 public:
  EventBus();
  EventBus(const EventBus&) = delete;
  EventBus& operator=(const EventBus&) = delete;
  ~EventBus();

  template <typename Event>
  EventSubscription Subscribe(std::function<void(const Event&)> callback) {
    const auto key = std::type_index(typeid(Event));
    const auto id = SubscribeImpl(key, [callback = std::move(callback)](const void* event) {
      callback(*static_cast<const Event*>(event));
    });
    const auto state = std::weak_ptr<State>(state_);
    return EventSubscription([state, key, id] {
      if (const auto locked = state.lock())
        UnsubscribeImpl(locked, key, id);
    });
  }

  template <typename Event>
  void Publish(const Event& event) const {
    PublishImpl(std::type_index(typeid(Event)), &event);
  }

 private:
  struct State;

  std::uint64_t SubscribeImpl(std::type_index key, std::function<void(const void*)> callback);
  static void UnsubscribeImpl(const std::shared_ptr<State>& state, std::type_index key, std::uint64_t id);
  void PublishImpl(std::type_index key, const void* event) const;

  std::shared_ptr<State> state_;
};

}  // namespace GoSurvey::Events
