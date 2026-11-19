mod line;
pub mod parser;

use std::collections::{HashMap, HashSet};
use std::sync::Arc;

use crate::events::EngineEvent;

use self::line::LineCommand;
use self::parser::CommandParser;

pub trait RegisteredCommand: Send + Sync {
    fn name(&self) -> &'static str;
    fn description(&self) -> &'static str {
        ""
    }
    fn aliases(&self) -> &'static [&'static str] {
        &[]
    }
    fn start(&self, arguments: &[String]) -> CommandProgress;
}

pub trait ActiveCommand: Send {
    fn submit(self: Box<Self>, input: &str) -> CommandProgress;
}

pub struct CommandProgress {
    pub events: Vec<EngineEvent>,
    pub active: Option<Box<dyn ActiveCommand>>,
}

impl CommandProgress {
    pub fn finish(events: impl IntoIterator<Item = EngineEvent>) -> Self {
        Self {
            events: events.into_iter().collect(),
            active: None,
        }
    }

    pub fn continue_with(
        active: impl ActiveCommand + 'static,
        events: impl IntoIterator<Item = EngineEvent>,
    ) -> Self {
        Self {
            events: events.into_iter().collect(),
            active: Some(Box::new(active)),
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum CommandRegistrationError {
    EmptyName,
    DuplicateName(String),
}

pub struct CommandManager {
    commands: HashMap<String, Arc<dyn RegisteredCommand>>,
    active: Option<Box<dyn ActiveCommand>>,
}

impl CommandManager {
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

    pub fn submit(&mut self, input: &str) -> Vec<EngineEvent> {
        let input = input.trim();
        if input.is_empty() && self.active.is_none() {
            return Vec::new();
        }
        let mut events = if input.is_empty() {
            Vec::new()
        } else {
            vec![EngineEvent::InputSubmitted(input.to_owned())]
        };

        if let Some(active) = self.active.take() {
            let progress = active.submit(input);
            self.active = progress.active;
            events.extend(progress.events);
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
