//! Extensible command registry, input routing, parsing entry points, and interactive sessions.
//!
//! Add command implementations in this module tree, implement [`RegisteredCommand`], and register
//! them through [`CommandManager`] or [`crate::Engine::register_command`]. Interactive commands
//! return an [`ActiveCommand`] session. The shared lifecycle owns cancellation routing, recoverable
//! error messages, and prompt updates; commands own only domain-specific state transitions.
//! See `docs/architecture/commands.md` for the contributor recipe and event flow.

mod line;
pub mod parser;

use std::collections::{HashMap, HashSet};
use std::sync::Arc;

use crate::events::{CommandPrompt, EngineEvent};
use gs_domain::Point2;

use self::line::LineCommand;
use self::parser::CommandParser;

/// A named command registered with the [`CommandManager`].
///
/// `start` receives whitespace/comma-separated arguments after the command name. Return
/// `CommandProgress::finish` for a one-shot command or `CommandProgress::start` for an interactive
/// command session.
pub trait RegisteredCommand: Send + Sync {
    /// Canonical command name. Names are normalized and matched case-insensitively.
    fn name(&self) -> &'static str;

    /// Short description shown by the built-in HELP command.
    fn description(&self) -> &'static str {
        ""
    }

    /// Additional names that invoke the same implementation.
    fn aliases(&self) -> &'static [&'static str] {
        &[]
    }

    /// Parses arguments after the command name and returns a one-shot result or active session.
    fn start(&self, arguments: &[String]) -> CommandProgress;
}

/// State machine for a command that requests additional user input.
///
/// Implement [`prompt`](Self::prompt) and [`handle_event`](Self::handle_event). Return
/// `CommandOutcome::Continue` to keep the session active or `Finish` to end it. Returning
/// `Err(message)` displays a message and retains the session; validate before mutating state so a
/// retry starts from the same state. The shared lifecycle sends only normalized text and drawing
/// points, and invokes [`cancel`](Self::cancel) for cancellation.
pub trait ActiveCommand: Send {
    /// Describes the current input expected from the user.
    fn prompt(&self) -> CommandPrompt;

    /// Handles one non-cancel input. Return an error to report it and keep the session active.
    fn handle_event(&mut self, event: &CommandInputEvent) -> Result<CommandOutcome, String>;

    /// Overrides shared cancellation handling when the command has a nested input mode.
    fn cancel(&mut self) -> CommandOutcome {
        CommandOutcome::finish([EngineEvent::CommandMessage("Command cancelled.".to_owned())])
    }
}

/// Shared command lifecycle: normalize input, route cancellation, report recoverable errors,
/// and refresh or clear the prompt after every submission.
fn submit_active(mut active: Box<dyn ActiveCommand>, event: &CommandInputEvent) -> CommandProgress {
    let outcome = match event {
        CommandInputEvent::Cancelled => active.cancel(),
        CommandInputEvent::TextSubmitted(text) => handle_active_event(
            active.as_mut(),
            &CommandInputEvent::TextSubmitted(text.trim().to_owned()),
        ),
        CommandInputEvent::PointPicked(_) => handle_active_event(active.as_mut(), event),
    };
    match outcome {
        CommandOutcome::Continue(events) => CommandProgress::continue_with(active, events),
        CommandOutcome::Finish(events) => CommandProgress::finish(events),
    }
}

fn handle_active_event(
    active: &mut dyn ActiveCommand,
    event: &CommandInputEvent,
) -> CommandOutcome {
    active.handle_event(event).unwrap_or_else(|message| {
        CommandOutcome::continue_with([EngineEvent::CommandMessage(message)])
    })
}

/// Normalized user intent routed by the Application event handler.
///
/// The Editor translates platform input into these values. Commands receive submitted text or
/// drawing-plane points, never keyboard codes, DOM events, or screen coordinates.
#[derive(Debug, Clone, PartialEq)]
pub enum CommandInputEvent {
    /// Text accepted from the command line by Enter. An empty string represents meaningful Enter.
    TextSubmitted(String),
    /// A point already converted to drawing-plane coordinates by the Editor adapter.
    PointPicked(Point2),
    /// User requested cancellation, normally by pressing Esc.
    Cancelled,
}

impl CommandInputEvent {
    /// Builds a typed point-pick event from drawing-plane coordinates.
    pub fn point_picked(x: f64, y: f64) -> Self {
        Self::PointPicked(Point2 { x, y })
    }
}

/// The shared result of handling one command input.
#[must_use]
pub enum CommandOutcome {
    /// Keep the active session and refresh its prompt after applying these events.
    Continue(Vec<EngineEvent>),
    /// End the active session and clear its prompt after applying these events.
    Finish(Vec<EngineEvent>),
}

impl CommandOutcome {
    /// Keeps the current session active and publishes these output events.
    pub fn continue_with(events: impl IntoIterator<Item = EngineEvent>) -> Self {
        Self::Continue(events.into_iter().collect())
    }

    /// Ends the current session and publishes these output events.
    pub fn finish(events: impl IntoIterator<Item = EngineEvent>) -> Self {
        Self::Finish(events.into_iter().collect())
    }
}

#[must_use]
pub struct CommandProgress {
    events: Vec<EngineEvent>,
    active: Option<Box<dyn ActiveCommand>>,
}

impl CommandProgress {
    /// Reports an argument error and clears the active prompt.
    pub fn error(message: impl Into<String>) -> Self {
        Self::finish([EngineEvent::CommandMessage(message.into())])
    }

    /// Completes command execution and clears the current prompt.
    pub fn finish(events: impl IntoIterator<Item = EngineEvent>) -> Self {
        let mut events = events.into_iter().collect::<Vec<_>>();
        events.push(EngineEvent::PromptChanged(None));
        Self {
            events,
            active: None,
        }
    }

    fn continue_with(
        active: Box<dyn ActiveCommand>,
        events: impl IntoIterator<Item = EngineEvent>,
    ) -> Self {
        let mut events = events.into_iter().collect::<Vec<_>>();
        events.push(EngineEvent::PromptChanged(Some(active.prompt())));
        Self {
            events,
            active: Some(active),
        }
    }

    /// Starts a command session and publishes its initial prompt plus any startup events.
    pub fn start(
        active: impl ActiveCommand + 'static,
        events: impl IntoIterator<Item = EngineEvent>,
    ) -> Self {
        Self::continue_with(Box::new(active), events)
    }
}

/// Error returned when a registered command name or alias conflicts with the registry.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum CommandRegistrationError {
    /// The canonical name or an alias contains no non-whitespace characters.
    EmptyName,
    /// A canonical name or alias is already reserved by a command or the built-in HELP command.
    DuplicateName(String),
}

/// Stores command definitions and the one active interactive session.
///
/// Registration is atomic: if any name or alias is invalid, none of that command's names are
/// installed. `HELP` is reserved. Input events enter through the Application command event handler;
/// this manager resolves command names and forwards normalized input to the active session.
pub struct CommandManager {
    commands: HashMap<String, Arc<dyn RegisteredCommand>>,
    active: Option<Box<dyn ActiveCommand>>,
}

impl CommandManager {
    /// Creates a manager with the built-in LINE command registered.
    pub fn new() -> Self {
        let mut manager = Self {
            commands: HashMap::new(),
            active: None,
        };
        manager
            .register(LineCommand)
            .expect("the built-in LINE command names are unique");
        manager
    }

    /// Registers a command and all its aliases, rejecting conflicts without partial registration.
    pub fn register(
        &mut self,
        command: impl RegisteredCommand + 'static,
    ) -> Result<(), CommandRegistrationError> {
        let mut names = HashSet::new();
        for name in std::iter::once(command.name()).chain(command.aliases().iter().copied()) {
            let normalized = normalize_name(name);
            if normalized.is_empty() {
                return Err(CommandRegistrationError::EmptyName);
            }
            if normalized == "HELP" {
                return Err(CommandRegistrationError::DuplicateName(normalized));
            }
            if !names.insert(normalized.clone()) || self.commands.contains_key(&normalized) {
                return Err(CommandRegistrationError::DuplicateName(normalized));
            }
        }
        let command = Arc::new(command) as Arc<dyn RegisteredCommand>;
        for name in std::iter::once(command.name()).chain(command.aliases().iter().copied()) {
            self.commands
                .insert(normalize_name(name), Arc::clone(&command));
        }
        Ok(())
    }

    /// Applies one normalized input event and returns output events for the Engine event bus.
    pub(super) fn handle_event(&mut self, event: &CommandInputEvent) -> Vec<EngineEvent> {
        let log_entry = match event {
            CommandInputEvent::TextSubmitted(text) if !text.trim().is_empty() => {
                Some(text.trim().to_owned())
            }
            CommandInputEvent::PointPicked(point) if self.active.is_some() => {
                Some(format!("{},{}", point.x, point.y))
            }
            CommandInputEvent::Cancelled if self.active.is_some() => Some("Esc".to_owned()),
            _ => None,
        };
        let mut events = log_entry
            .map(|input| vec![EngineEvent::InputSubmitted(input)])
            .unwrap_or_default();
        if let Some(active) = self.active.take() {
            let progress = submit_active(active, event);
            self.active = progress.active;
            events.extend(progress.events);
            return events;
        }

        let CommandInputEvent::TextSubmitted(input) = event else {
            return events;
        };
        let input = input.trim();
        if input.is_empty() {
            return events;
        }
        let Some(command_input) = CommandParser::parse_command(input) else {
            events.push(EngineEvent::CommandMessage(
                "Enter a command name.".to_owned(),
            ));
            return events;
        };
        if command_input.name == "HELP" {
            events.push(EngineEvent::CommandMessage(self.help_message()));
            return events;
        }
        let Some(command) = self
            .commands
            .get(&normalize_name(&command_input.name))
            .cloned()
        else {
            events.push(EngineEvent::CommandMessage(
                "Unknown command. Type HELP.".to_owned(),
            ));
            return events;
        };
        let progress = command.start(&command_input.arguments);
        self.active = progress.active;
        events.extend(progress.events);
        events
    }

    fn help_message(&self) -> String {
        let mut seen = HashSet::new();
        let mut entries = self
            .commands
            .values()
            .filter(|command| seen.insert(command.name()))
            .map(|command| {
                let aliases = command.aliases();
                let name = if aliases.is_empty() {
                    command.name().to_owned()
                } else {
                    format!("{} ({})", command.name(), aliases.join(", "))
                };
                format!("{name} — {}", command.description())
            })
            .collect::<Vec<_>>();
        entries.sort();
        entries.push("HELP — list registered commands".to_owned());
        format!("Commands: {}.", entries.join("; "))
    }
}

impl Default for CommandManager {
    fn default() -> Self {
        Self::new()
    }
}

fn normalize_name(name: &str) -> String {
    name.trim().to_ascii_uppercase()
}
