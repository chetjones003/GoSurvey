//! Application events and handlers that apply command effects to Engine state.
//!
//! [`CommandEventHandler`] routes normalized Editor input into the command manager. Commands emit
//! [`EngineEvent`]s; the drawing and output handlers below apply them in bus registration order.

use gs_domain::{Drawing, Line2};
use gs_foundation::EventHandler;

use crate::command::{CommandInputEvent, CommandManager};

/// Typed effects emitted by command processing and consumed by Application handlers.
#[derive(Debug, Clone, PartialEq)]
pub enum EngineEvent {
    /// A user-submitted value to append to command history.
    InputSubmitted(String),
    /// Replaces the current command prompt.
    PromptChanged(Option<CommandPrompt>),
    /// Adds a validated line entity to the open drawing.
    LineAdded(Line2),
    /// Adds a user-facing status or error message to command history.
    CommandMessage(String),
}

/// Describes the input a command session accepts at its current step.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct CommandPrompt {
    /// Text rendered beside the command input.
    pub text: String,
    /// Whether submitting an empty line has meaning at this prompt.
    pub accepts_blank: bool,
    /// Whether the Editor should translate viewport clicks into point-pick events.
    pub accepts_point: bool,
}

impl CommandPrompt {
    /// Creates a prompt and declares the semantic input types enabled for this step.
    pub fn new(text: impl Into<String>, accepts_blank: bool, accepts_point: bool) -> Self {
        Self {
            text: text.into(),
            accepts_blank,
            accepts_point,
        }
    }
}

/// Mutable Application state shared by output event handlers.
pub(crate) struct EngineContext {
    pub drawing: Drawing,
    pub messages: Vec<String>,
    pub prompt: Option<CommandPrompt>,
}

/// Command input handler context, including pending output for the Engine's event bus.
pub(crate) struct CommandEventContext {
    pub commands: CommandManager,
    pub output: Vec<EngineEvent>,
}

/// Routes a normalized Editor input event to the command manager.
pub(crate) struct CommandEventHandler;

impl EventHandler<CommandInputEvent, CommandEventContext> for CommandEventHandler {
    fn handle(&mut self, event: &CommandInputEvent, context: &mut CommandEventContext) {
        context.output.extend(context.commands.handle_event(event));
    }
}

/// Applies added geometry to the open Drawing.
pub(crate) struct DrawingEventHandler;

impl EventHandler<EngineEvent, EngineContext> for DrawingEventHandler {
    fn handle(&mut self, event: &EngineEvent, context: &mut EngineContext) {
        if let EngineEvent::LineAdded(line) = event {
            context.drawing.add_line(*line);
        }
    }
}

/// Collects history, prompt, and command messages for the Editor response.
pub(crate) struct CommandOutputEventHandler;

impl EventHandler<EngineEvent, EngineContext> for CommandOutputEventHandler {
    fn handle(&mut self, event: &EngineEvent, context: &mut EngineContext) {
        match event {
            EngineEvent::InputSubmitted(input) => context.messages.push(format!("> {input}")),
            EngineEvent::PromptChanged(prompt) => context.prompt = prompt.clone(),
            EngineEvent::LineAdded(_) => context.messages.push("Line created.".to_owned()),
            EngineEvent::CommandMessage(message) => context.messages.push(message.clone()),
        }
    }
}
