//! Foundation layer (rank 0): core services, math, events, toolkit-neutral UI interfaces, debugging, crash reporting.

/// Receives events published through an [`EventBus`].
pub trait EventHandler<Event, Context>: Send {
    fn handle(&mut self, event: &Event, context: &mut Context);
}

/// Dispatches each event to registered handlers in registration order.
///
/// Handlers run synchronously and share a mutable context. A handler may react
/// to events it owns and ignore all others.
pub struct EventBus<Event, Context> {
    handlers: Vec<Box<dyn EventHandler<Event, Context>>>,
}

impl<Event, Context> EventBus<Event, Context> {
    pub fn new() -> Self {
        Self {
            handlers: Vec::new(),
        }
    }

    pub fn subscribe(&mut self, handler: impl EventHandler<Event, Context> + 'static) {
        self.handlers.push(Box::new(handler));
    }

    pub fn publish(&mut self, event: &Event, context: &mut Context) {
        for handler in &mut self.handlers {
            handler.handle(event, context);
        }
    }
}

impl<Event, Context> Default for EventBus<Event, Context> {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[derive(Debug)]
    enum TestEvent {
        Add(i32),
    }

    struct Adder(i32);

    impl EventHandler<TestEvent, i32> for Adder {
        fn handle(&mut self, event: &TestEvent, total: &mut i32) {
            let TestEvent::Add(value) = event;
            *total += value + self.0;
        }
    }

    #[test]
    fn publishes_to_each_handler_in_registration_order() {
        let mut bus = EventBus::new();
        bus.subscribe(Adder(1));
        bus.subscribe(Adder(10));
        let mut total = 0;

        bus.publish(&TestEvent::Add(2), &mut total);

        assert_eq!(total, 15);
    }
}
