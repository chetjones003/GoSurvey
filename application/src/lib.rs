//! Application systems layer (rank 4): command registry and interaction.
//!
//! The Engine is the Application seam used by the Editor. It owns the current drawing, command
//! registry and active session, and wires the input and output event buses. The Editor translates
//! native interactions into [`CommandInputEvent`]s and reads [`CommandResult`]s plus a read-only
//! [`DrawingSnapshot`]; commands never depend on UI or Tauri types.
//!
//! To add a command, implement [`RegisteredCommand`] and, for interactive behavior, [`ActiveCommand`].
//! Register it with [`Engine::register_command`]. Shared parsing and lifecycle behavior live under
//! `command/`; geometric calculations live in `gs-kernel`. The contributor guide is
//! `docs/architecture/commands.md`.

mod command;
mod events;

pub use command::{
    ActiveCommand, CommandInputEvent, CommandManager, CommandOutcome, CommandProgress,
    CommandRegistrationError, RegisteredCommand,
    parser::{CommandInputError, CommandParser, ParsedCommand, PointInput},
};
pub use events::{CommandPrompt, EngineEvent};

use gs_domain::Drawing;
use gs_foundation::EventBus;

use crate::events::{
    CommandEventContext, CommandEventHandler, CommandOutputEventHandler, DrawingEventHandler,
    EngineContext,
};

/// Whether the Engine can accept commands.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum EngineState {
    Ready,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct EngineStatus {
    pub state: EngineState,
    pub version: &'static str,
}

/// Read-only projection of the open Drawing returned across the Editor seam.
#[derive(Debug, Clone, PartialEq)]
pub struct DrawingSnapshot {
    pub name: String,
    pub entity_count: usize,
    pub lines: Vec<LineSnapshot>,
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub struct PointSnapshot {
    pub x: f64,
    pub y: f64,
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub struct LineSnapshot {
    pub start: PointSnapshot,
    pub end: PointSnapshot,
}

/// Messages and current prompt produced while processing one command input event.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct CommandResult {
    pub messages: Vec<String>,
    pub prompt: Option<CommandPrompt>,
}

/// Application facade for command input, event dispatch, and read-only drawing access.
///
/// `dispatch_command_input` sends normalized input through the command-input EventBus, then
/// publishes the resulting `EngineEvent`s through the drawing/output EventBus. Callers should use
/// this seam rather than changing the Drawing or calling a command implementation directly.
pub struct Engine {
    context: EngineContext,
    events: EventBus<EngineEvent, EngineContext>,
    command_events: EventBus<CommandInputEvent, CommandEventContext>,
    command_context: CommandEventContext,
}

impl Engine {
    pub fn new() -> Self {
        let mut events = EventBus::new();
        events.subscribe(DrawingEventHandler);
        events.subscribe(CommandOutputEventHandler);
        let mut command_events = EventBus::new();
        command_events.subscribe(CommandEventHandler);
        Self {
            context: EngineContext {
                drawing: Drawing::new(),
                messages: Vec::new(),
                prompt: None,
            },
            events,
            command_events,
            command_context: CommandEventContext {
                commands: CommandManager::new(),
                output: Vec::new(),
            },
        }
    }

    /// Returns whether this Engine is ready and identifies its build version.
    pub fn status(&self) -> EngineStatus {
        EngineStatus {
            state: EngineState::Ready,
            version: env!("CARGO_PKG_VERSION"),
        }
    }

    /// Copies the visible drawing data into a read-only Editor-facing snapshot.
    pub fn snapshot(&self) -> DrawingSnapshot {
        DrawingSnapshot {
            name: self.context.drawing.name().to_owned(),
            entity_count: self.context.drawing.entity_count(),
            lines: self
                .context
                .drawing
                .lines()
                .iter()
                .map(|line| LineSnapshot {
                    start: PointSnapshot {
                        x: line.start.x,
                        y: line.start.y,
                    },
                    end: PointSnapshot {
                        x: line.end.x,
                        y: line.end.y,
                    },
                })
                .collect(),
        }
    }

    /// Registers a command. Its name and aliases are matched case-insensitively by the manager.
    pub fn register_command(
        &mut self,
        command: impl RegisteredCommand + 'static,
    ) -> Result<(), CommandRegistrationError> {
        self.command_context.commands.register(command)
    }

    /// Convenience helper for text-only callers; dispatches text through the command event bus.
    pub fn execute_command(&mut self, input: &str) -> CommandResult {
        self.dispatch_command_input(CommandInputEvent::TextSubmitted(input.to_owned()))
    }

    /// Dispatches normalized text, drawing-plane point-pick, or cancellation input through the
    /// command event handler, then applies all resulting Engine events before returning.
    ///
    /// Editor adapters should map native input to this enum before calling the Engine. In
    /// particular, point values must already be in drawing-plane coordinates.
    pub fn dispatch_command_input(&mut self, input: CommandInputEvent) -> CommandResult {
        let start = self.context.messages.len();
        self.command_events
            .publish(&input, &mut self.command_context);
        let events = std::mem::take(&mut self.command_context.output);
        for event in &events {
            self.events.publish(event, &mut self.context);
        }
        let messages = self.context.messages.drain(start..).collect();
        CommandResult {
            messages,
            prompt: self.context.prompt.clone(),
        }
    }
}

impl Default for Engine {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn new_engine_reports_ready_with_this_crate_version() {
        let status = Engine::new().status();
        assert_eq!(status.state, EngineState::Ready);
        assert_eq!(status.version, env!("CARGO_PKG_VERSION"));
    }

    #[test]
    fn new_engine_has_an_empty_untitled_drawing() {
        let snapshot = Engine::new().snapshot();
        assert_eq!(
            snapshot,
            DrawingSnapshot {
                name: "Untitled".to_owned(),
                entity_count: 0,
                lines: vec![],
            }
        );
    }

    #[test]
    fn one_line_line_input_draws_a_segment_and_keeps_the_command_active() {
        let mut engine = Engine::new();

        let result = engine.execute_command("LINE, 0,0, 20,10,");

        assert_eq!(result.messages, ["> LINE, 0,0, 20,10,", "Line created."]);
        assert!(
            result
                .prompt
                .as_ref()
                .unwrap()
                .text
                .as_str()
                .starts_with("LINE: Next point")
        );
        assert_eq!(engine.snapshot().entity_count, 1);
        assert_eq!(
            engine.snapshot().lines,
            vec![LineSnapshot {
                start: PointSnapshot { x: 0.0, y: 0.0 },
                end: PointSnapshot { x: 20.0, y: 10.0 },
            }]
        );
    }

    #[test]
    fn line_session_draws_repeated_absolute_and_relative_segments_until_escape() {
        let mut engine = Engine::new();

        let start = engine.execute_command("l");
        assert!(
            start
                .prompt
                .as_ref()
                .unwrap()
                .text
                .as_str()
                .starts_with("LINE: First point")
        );
        assert_eq!(engine.execute_command("0 0").messages, ["> 0 0"]);
        assert!(
            engine
                .execute_command("20,10")
                .messages
                .contains(&"Line created.".to_owned())
        );
        assert!(
            engine
                .execute_command("@-5,2")
                .messages
                .contains(&"Line created.".to_owned())
        );
        let end = engine.dispatch_command_input(CommandInputEvent::Cancelled);

        assert_eq!(end.prompt, None);
        assert_eq!(end.messages, ["> Esc", "Command finished."]);
        assert_eq!(
            engine.snapshot().lines,
            vec![
                LineSnapshot {
                    start: PointSnapshot { x: 0.0, y: 0.0 },
                    end: PointSnapshot { x: 20.0, y: 10.0 },
                },
                LineSnapshot {
                    start: PointSnapshot { x: 20.0, y: 10.0 },
                    end: PointSnapshot { x: 15.0, y: 12.0 },
                },
            ]
        );
    }

    #[test]
    fn invalid_point_is_reported_without_ending_the_line_command() {
        let mut engine = Engine::new();
        engine.execute_command("LINE");
        engine.execute_command("0,0");

        let result = engine.execute_command("not a point");

        assert!(
            result
                .messages
                .contains(&"Enter a point as X,Y or X Y.".to_owned())
        );
        assert!(
            result
                .prompt
                .as_ref()
                .unwrap()
                .text
                .as_str()
                .starts_with("LINE: Next point")
        );
        assert_eq!(engine.snapshot().entity_count, 0);
    }

    #[test]
    fn coordinate_overflow_does_not_create_a_line_or_end_the_session() {
        let mut engine = Engine::new();
        engine.execute_command("LINE");
        engine.execute_command("1e308,0");

        let result = engine.execute_command("@1e308,0");

        assert!(
            result
                .messages
                .contains(&"The point is outside the supported coordinate range.".to_owned())
        );
        assert!(result.prompt.is_some());
        assert_eq!(engine.snapshot().entity_count, 0);

        engine.execute_command("@-1e308,0");
        assert_eq!(engine.snapshot().entity_count, 1);
    }

    #[test]
    fn line_bearing_lock_turns_clockwise_from_north_and_accepts_distance() {
        let mut engine = Engine::new();
        engine.execute_command("LINE");
        engine.execute_command("0,0");
        let bearing = engine.execute_command("A 90");
        assert!(bearing.prompt.as_ref().unwrap().text.contains("distance"));

        engine.execute_command("100");
        engine.dispatch_command_input(CommandInputEvent::Cancelled);

        let line = engine.snapshot().lines[0];
        assert!((line.end.x - 100.0).abs() < 1e-9);
        assert!(line.end.y.abs() < 1e-9);
    }

    #[test]
    fn line_accepts_dms_bearing_and_bearing_plus_turn() {
        let mut engine = Engine::new();
        engine.execute_command("LINE");
        engine.execute_command("0,0");
        engine.execute_command("A 45d30m00s +44.5");
        engine.execute_command("100");
        engine.dispatch_command_input(CommandInputEvent::Cancelled);

        let line = engine.snapshot().lines[0];
        assert!((line.end.x - 100.0).abs() < 1e-9);
        assert!(line.end.y.abs() < 1e-9);
    }

    #[test]
    fn line_two_point_direction_can_be_locked_with_enter() {
        let mut engine = Engine::new();
        engine.execute_command("LINE");
        engine.execute_command("0,0");
        engine.execute_command("2P");
        engine.execute_command("0,0");
        engine.execute_command("1,0");
        let lock = engine.execute_command("");
        assert!(lock.prompt.as_ref().unwrap().text.contains("distance"));

        engine.execute_command("10");
        engine.dispatch_command_input(CommandInputEvent::Cancelled);

        let line = engine.snapshot().lines[0];
        assert!((line.end.x - 10.0).abs() < 1e-9);
        assert!(line.end.y.abs() < 1e-9);
    }

    #[test]
    fn escape_during_a_two_point_pick_cancels_only_the_pick() {
        let mut engine = Engine::new();
        engine.execute_command("LINE");
        engine.execute_command("0,0");
        engine.execute_command("2P");

        let cancelled_pick = engine.dispatch_command_input(CommandInputEvent::Cancelled);

        assert!(
            cancelled_pick
                .messages
                .contains(&"Direction pick cancelled.".to_owned())
        );
        assert!(
            cancelled_pick
                .prompt
                .as_ref()
                .unwrap()
                .text
                .starts_with("LINE: Next point")
        );
        engine.execute_command("10,0");
        engine.dispatch_command_input(CommandInputEvent::Cancelled);
        assert_eq!(engine.snapshot().entity_count, 1);
    }

    #[test]
    fn an_unregistered_command_is_logged_and_does_not_change_the_drawing() {
        let mut engine = Engine::new();

        let result = engine.execute_command("CIRCLE");

        assert_eq!(result.messages, ["> CIRCLE", "Unknown command. Type HELP."]);
        assert_eq!(engine.snapshot().entity_count, 0);
    }

    #[test]
    fn help_is_resolved_through_the_registered_command_manager() {
        let mut engine = Engine::new();

        let result = engine.execute_command("help");

        assert_eq!(
            result.messages,
            [
                "> help",
                "Commands: LINE (L) — draw repeated segments from points and directions; HELP — list registered commands."
            ]
        );
        assert_eq!(result.prompt, None);
    }

    #[test]
    fn duplicate_command_alias_is_refused_without_replacing_line() {
        struct Collision;
        impl RegisteredCommand for Collision {
            fn name(&self) -> &'static str {
                "CIRCLE"
            }
            fn aliases(&self) -> &'static [&'static str] {
                &["l"]
            }
            fn start(&self, _: &[String]) -> CommandProgress {
                CommandProgress::finish([EngineEvent::CommandMessage("wrong".to_owned())])
            }
        }

        let mut engine = Engine::new();

        assert_eq!(
            engine.register_command(Collision),
            Err(CommandRegistrationError::DuplicateName("L".to_owned()))
        );
        assert!(
            engine
                .execute_command("L")
                .prompt
                .unwrap()
                .text
                .as_str()
                .starts_with("LINE: First point")
        );
    }

    #[test]
    fn help_includes_commands_registered_after_engine_creation() {
        struct Circle;
        impl RegisteredCommand for Circle {
            fn name(&self) -> &'static str {
                "CIRCLE"
            }
            fn description(&self) -> &'static str {
                "draw a circle"
            }
            fn start(&self, _: &[String]) -> CommandProgress {
                CommandProgress::finish([EngineEvent::CommandMessage("Circle created.".to_owned())])
            }
        }

        let mut engine = Engine::new();
        engine.register_command(Circle).unwrap();

        let result = engine.execute_command("HELP");

        assert!(result.messages[1].contains("CIRCLE — draw a circle"));
    }

    #[test]
    fn active_command_errors_and_cancellation_use_shared_session_lifecycle() {
        struct Probe;

        impl RegisteredCommand for Probe {
            fn name(&self) -> &'static str {
                "PROBE"
            }

            fn start(&self, _: &[String]) -> CommandProgress {
                CommandProgress::start(ProbeSession, [])
            }
        }

        struct ProbeSession;

        impl ActiveCommand for ProbeSession {
            fn prompt(&self) -> CommandPrompt {
                CommandPrompt::new("PROBE: Enter a value", false, false)
            }

            fn handle_event(
                &mut self,
                event: &CommandInputEvent,
            ) -> Result<CommandOutcome, String> {
                if matches!(event, CommandInputEvent::TextSubmitted(input) if input.eq_ignore_ascii_case("bad"))
                {
                    Err("Invalid probe value.".to_owned())
                } else {
                    Ok(CommandOutcome::finish([EngineEvent::CommandMessage(
                        "Probe finished.".to_owned(),
                    )]))
                }
            }
        }

        let mut engine = Engine::new();
        engine.register_command(Probe).unwrap();

        assert_eq!(
            engine.execute_command("PROBE").prompt.unwrap().text,
            "PROBE: Enter a value"
        );
        let error = engine.execute_command("bad");
        assert_eq!(error.messages, ["> bad", "Invalid probe value."]);
        assert_eq!(error.prompt.unwrap().text, "PROBE: Enter a value");

        let cancelled = engine.dispatch_command_input(CommandInputEvent::Cancelled);
        assert_eq!(cancelled.messages, ["> Esc", "Command cancelled."]);
        assert_eq!(cancelled.prompt, None);
    }

    #[test]
    fn point_picks_are_routed_through_the_command_event_handler() {
        let mut engine = Engine::new();
        engine.execute_command("LINE");

        let first = engine.dispatch_command_input(CommandInputEvent::point_picked(4.0, 5.0));
        assert_eq!(first.messages, ["> 4,5"]);
        let second = engine.dispatch_command_input(CommandInputEvent::point_picked(9.0, 5.0));

        assert_eq!(second.messages, ["> 9,5", "Line created."]);
        assert_eq!(engine.snapshot().entity_count, 1);
        assert_eq!(
            engine.snapshot().lines[0],
            LineSnapshot {
                start: PointSnapshot { x: 4.0, y: 5.0 },
                end: PointSnapshot { x: 9.0, y: 5.0 },
            }
        );
    }

    #[test]
    fn direction_pick_uses_point_events_and_kernel_bearing_math() {
        let mut engine = Engine::new();
        engine.execute_command("LINE");
        engine.dispatch_command_input(CommandInputEvent::point_picked(10.0, 10.0));
        engine.execute_command("2P");
        engine.dispatch_command_input(CommandInputEvent::point_picked(0.0, 0.0));
        engine.dispatch_command_input(CommandInputEvent::point_picked(0.0, 10.0));
        engine.execute_command("");
        engine.execute_command("5");

        assert_eq!(
            engine.snapshot().lines[0],
            LineSnapshot {
                start: PointSnapshot { x: 10.0, y: 10.0 },
                end: PointSnapshot { x: 10.0, y: 15.0 },
            }
        );
    }
}
