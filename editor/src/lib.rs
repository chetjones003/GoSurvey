//! Editor layer (rank 5): Tauri shell and ui-sdk front end.
//!
//! The types here are the webview wire format. Application types are converted
//! at this boundary, so the layers below stay free of serde and camelCase keys.

use std::sync::Mutex;

use gs_application::{
    CommandInputEvent, CommandPrompt, CommandResult, DrawingSnapshot, Engine, EngineState,
    EngineStatus,
};
use serde::{Deserialize, Serialize};

/// Shown to the user when the engine's lock is poisoned by a panic.
pub const ENGINE_UNAVAILABLE: &str = "Drawing engine unavailable. Restart GoSurvey.";

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
#[serde(rename_all = "lowercase")]
pub enum WireEngineState {
    Ready,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct WireEngineStatus {
    pub state: WireEngineState,
    pub version: &'static str,
}

#[derive(Debug, Clone, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct WireDrawingSnapshot {
    pub name: String,
    pub entity_count: usize,
    pub lines: Vec<WireLine>,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct WireCommandResult {
    pub messages: Vec<String>,
    pub prompt: Option<WireCommandPrompt>,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct WireCommandPrompt {
    pub text: String,
    pub accepts_blank: bool,
    pub accepts_point: bool,
}

/// Platform-neutral user input sent from the Editor to the Application command event handler.
#[derive(Debug, Clone, PartialEq, Deserialize, Serialize)]
#[serde(tag = "type", rename_all = "camelCase")]
pub enum WireCommandInput {
    TextSubmitted { text: String },
    PointPicked { point: WirePoint },
    Cancelled,
}

impl From<WireCommandInput> for CommandInputEvent {
    fn from(input: WireCommandInput) -> Self {
        match input {
            WireCommandInput::TextSubmitted { text } => Self::TextSubmitted(text),
            WireCommandInput::PointPicked { point } => Self::PointPicked {
                x: point.x,
                y: point.y,
            },
            WireCommandInput::Cancelled => Self::Cancelled,
        }
    }
}

impl From<CommandPrompt> for WireCommandPrompt {
    fn from(prompt: CommandPrompt) -> Self {
        Self {
            text: prompt.text,
            accepts_blank: prompt.accepts_blank,
            accepts_point: prompt.accepts_point,
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct WirePoint {
    pub x: f64,
    pub y: f64,
}

#[derive(Debug, Clone, Copy, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct WireLine {
    pub start: WirePoint,
    pub end: WirePoint,
}

impl From<EngineStatus> for WireEngineStatus {
    fn from(status: EngineStatus) -> Self {
        let state = match status.state {
            EngineState::Ready => WireEngineState::Ready,
        };
        Self {
            state,
            version: status.version,
        }
    }
}

impl From<DrawingSnapshot> for WireDrawingSnapshot {
    fn from(snapshot: DrawingSnapshot) -> Self {
        Self {
            name: snapshot.name,
            entity_count: snapshot.entity_count,
            lines: snapshot
                .lines
                .into_iter()
                .map(|line| WireLine {
                    start: WirePoint {
                        x: line.start.x,
                        y: line.start.y,
                    },
                    end: WirePoint {
                        x: line.end.x,
                        y: line.end.y,
                    },
                })
                .collect(),
        }
    }
}

impl From<CommandResult> for WireCommandResult {
    fn from(result: CommandResult) -> Self {
        Self {
            messages: result.messages,
            prompt: result.prompt.map(WireCommandPrompt::from),
        }
    }
}

/// Runs `read` against the engine, or reports that the engine is unavailable.
pub fn read_engine<T>(
    engine: &Mutex<Engine>,
    read: impl FnOnce(&Engine) -> T,
) -> Result<T, String> {
    let engine = engine.lock().map_err(|_| ENGINE_UNAVAILABLE.to_owned())?;
    Ok(read(&engine))
}

#[cfg(windows)]
pub mod shell;

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn read_engine_passes_the_engine_through() {
        let engine = Mutex::new(Engine::new());
        let name = read_engine(&engine, |e| e.snapshot().name).unwrap();
        assert_eq!(name, "Untitled");
    }

    #[test]
    fn read_engine_reports_unavailable_after_a_panic() {
        let engine = Mutex::new(Engine::new());
        let _ = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
            let _guard = engine.lock().unwrap();
            panic!("poison the lock");
        }));
        let error = read_engine(&engine, |e| e.status()).unwrap_err();
        assert_eq!(error, ENGINE_UNAVAILABLE);
    }

    #[test]
    fn status_serialises_state_in_lowercase() {
        let wire = WireEngineStatus::from(Engine::new().status());
        let json = serde_json::to_value(wire).unwrap();
        assert_eq!(json["state"], "ready");
        assert_eq!(json["version"], env!("CARGO_PKG_VERSION"));
    }

    #[test]
    fn snapshot_serialises_with_camel_case_keys() {
        let wire = WireDrawingSnapshot::from(Engine::new().snapshot());
        let json = serde_json::to_value(wire).unwrap();
        assert_eq!(json["entityCount"], 0);
        assert_eq!(json["name"], "Untitled");
        assert_eq!(json["lines"], serde_json::json!([]));
    }

    #[test]
    fn command_result_serialises_messages_and_prompt() {
        let wire = WireCommandResult::from(Engine::new().execute_command("LINE"));
        let json = serde_json::to_value(wire).unwrap();
        assert_eq!(json["messages"], serde_json::json!(["> LINE"]));
        assert_eq!(
            json["prompt"]["text"],
            "LINE: First point — click or type X,Y / X Y. Esc cancels."
        );
        assert_eq!(json["prompt"]["acceptsBlank"], false);
        assert_eq!(json["prompt"]["acceptsPoint"], true);
    }

    #[test]
    fn command_input_wire_events_are_tagged_and_platform_neutral() {
        let json = serde_json::to_value(WireCommandInput::PointPicked {
            point: WirePoint { x: 2.0, y: 3.0 },
        })
        .unwrap();
        assert_eq!(
            json,
            serde_json::json!({
                "type": "pointPicked",
                "point": { "x": 2.0, "y": 3.0 }
            })
        );

        let input: WireCommandInput = serde_json::from_value(serde_json::json!({
            "type": "textSubmitted",
            "text": "LINE"
        }))
        .unwrap();
        assert_eq!(
            CommandInputEvent::from(input),
            CommandInputEvent::TextSubmitted("LINE".to_owned())
        );
    }
}
