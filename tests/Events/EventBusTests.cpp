#include <catch2/catch_test_macros.hpp>

#include <GoSurvey/Events/Events.hpp>

using namespace GoSurvey::Events;

TEST_CASE("EventBus delivers typed events to active subscriptions") {
  EventBus bus;
  int observed = 0;
  auto subscription = bus.Subscribe<DocumentChangedEvent>([&](const DocumentChangedEvent& event) {
    observed = static_cast<int>(event.revision);
  });

  bus.Publish(DocumentChangedEvent{DocumentToken{17}, DocumentChangeKind::Geometry, 42});

  REQUIRE(observed == 42);
  subscription.Reset();
  bus.Publish(DocumentChangedEvent{DocumentToken{17}, DocumentChangeKind::Geometry, 43});
  REQUIRE(observed == 42);
}

TEST_CASE("EventSubscription is move-only, idempotent and unsubscribes on destruction") {
  EventBus bus;
  int calls = 0;
  {
    auto original = bus.Subscribe<KeyEvent>([&](const KeyEvent&) { ++calls; });
    auto moved = std::move(original);
    moved.Reset();
    moved.Reset();
    bus.Publish(KeyEvent{13, 0, EventPhase::Begin});
  }
  REQUIRE(calls == 0);

  {
    auto subscription = bus.Subscribe<KeyEvent>([&](const KeyEvent&) { ++calls; });
    bus.Publish(KeyEvent{13, 0, EventPhase::Begin});
  }
  bus.Publish(KeyEvent{13, 0, EventPhase::End});
  REQUIRE(calls == 1);
}

TEST_CASE("EventBus invokes callbacks outside its lock") {
  EventBus bus;
  int calls = 0;
  EventSubscription second;
  auto first = bus.Subscribe<WheelEvent>([&](const WheelEvent&) {
    ++calls;
    second.Reset();
  });
  second = bus.Subscribe<WheelEvent>([&](const WheelEvent&) { ++calls; });

  bus.Publish(WheelEvent{});

  REQUIRE(calls == 2);
  bus.Publish(WheelEvent{});
  REQUIRE(calls == 3);
}

TEST_CASE("input and change events carry toolkit-neutral payloads") {
  const PointerEvent pointer{12.5f, -4.0f, 1, 2, EventPhase::End};
  const TextInputEvent text{"é", TextInputPhase::Commit};
  const CommandStateChangedEvent state{CommandStateChangeKind::Prompt, 9};

  REQUIRE(pointer.x == 12.5f);
  REQUIRE(pointer.phase == EventPhase::End);
  REQUIRE(text.text == "é");
  REQUIRE(text.phase == TextInputPhase::Commit);
  REQUIRE(state.generation == 9);
}
