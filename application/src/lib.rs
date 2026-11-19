//! Application systems layer (rank 4): command registry and interaction.

use gs_domain::Drawing;
use serde::Serialize;

/// Whether the engine can accept commands.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
#[serde(rename_all = "lowercase")]
pub enum EngineState {
    Ready,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct EngineStatus {
    pub state: EngineState,
    pub version: &'static str,
}

/// What the front end sees of the open drawing.
#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct DrawingSnapshot {
    pub name: String,
    pub entity_count: usize,
}

/// The command seam. The front end sends commands and reads snapshots.
pub struct Engine {
    drawing: Drawing,
}

impl Engine {
    pub fn new() -> Self {
        Self {
            drawing: Drawing::new(),
        }
    }

    pub fn status(&self) -> EngineStatus {
        EngineStatus {
            state: EngineState::Ready,
            version: env!("CARGO_PKG_VERSION"),
        }
    }

    pub fn snapshot(&self) -> DrawingSnapshot {
        DrawingSnapshot {
            name: self.drawing.name().to_owned(),
            entity_count: self.drawing.entity_count(),
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
    fn new_engine_reports_ready() {
        let status = Engine::new().status();
        assert_eq!(status.state, EngineState::Ready);
        assert_eq!(status.version, "0.1.0");
    }

    #[test]
    fn new_engine_has_an_empty_untitled_drawing() {
        let snapshot = Engine::new().snapshot();
        assert_eq!(
            snapshot,
            DrawingSnapshot {
                name: "Untitled".to_owned(),
                entity_count: 0,
            }
        );
    }

    #[test]
    fn snapshot_serialises_with_camel_case_keys() {
        let json = serde_json::to_value(Engine::new().snapshot()).unwrap();
        assert_eq!(json["entityCount"], 0);
        assert_eq!(json["name"], "Untitled");
    }

    #[test]
    fn status_serialises_state_in_lowercase() {
        let json = serde_json::to_value(Engine::new().status()).unwrap();
        assert_eq!(json["state"], "ready");
    }
}
