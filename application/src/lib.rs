//! Application systems layer (rank 4): command registry and interaction.
//!
//! Wire formats for the front end live in the Editor, not here.

use gs_domain::Drawing;

/// Whether the engine can accept commands.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum EngineState {
    Ready,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct EngineStatus {
    pub state: EngineState,
    pub version: &'static str,
}

/// What the front end sees of the open drawing.
#[derive(Debug, Clone, PartialEq, Eq)]
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
            }
        );
    }
}
