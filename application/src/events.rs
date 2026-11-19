use gs_domain::{Drawing, Line2};
use gs_foundation::EventHandler;

use crate::command::{CommandInputEvent, CommandManager};

#[derive(Debug, Clone, PartialEq)]
pub enum EngineEvent {
    InputSubmitted(String),
    PromptChanged(Option<CommandPrompt>),
    LineAdded(Line2),
    CommandMessage(String),
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct CommandPrompt {
    pub text: String,
    pub accepts_blank: bool,
    pub accepts_point: bool,
}

impl CommandPrompt {
    pub fn new(text: impl Into<String>, accepts_blank: bool, accepts_point: bool) -> Self {
        Self {
            text: text.into(),
            accepts_blank,
            accepts_point,
        }
    }
}

pub(crate) struct EngineContext {
    pub drawing: Drawing,
    pub messages: Vec<String>,
    pub prompt: Option<CommandPrompt>,
}

pub(crate) struct CommandEventContext {
    pub commands: CommandManager,
    pub output: Vec<EngineEvent>,
}

pub(crate) struct CommandEventHandler;

impl EventHandler<CommandInputEvent, CommandEventContext> for CommandEventHandler {
    fn handle(&mut self, event: &CommandInputEvent, context: &mut CommandEventContext) {
        context.output.extend(context.commands.handle_event(event));
    }
}

pub(crate) struct DrawingEventHandler;

impl EventHandler<EngineEvent, EngineContext> for DrawingEventHandler {
    fn handle(&mut self, event: &EngineEvent, context: &mut EngineContext) {
        if let EngineEvent::LineAdded(line) = event {
            context.drawing.add_line(*line);
        }
    }
}

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
