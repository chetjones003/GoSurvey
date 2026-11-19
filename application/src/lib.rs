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

struct ParsedLine {
    start: (f64, f64),
    end: (f64, f64),
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
            lines: self
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

    /// Executes a typed command and returns the message to add to command history.
    pub fn execute_command(&mut self, input: &str) -> String {
        match parse_line_command(input) {
            Ok(line) => {
                self.drawing.add_line(line.start, line.end);
                "Line created.".to_owned()
            }
            Err(message) => message,
        }
    }
}

fn parse_line_command(input: &str) -> Result<ParsedLine, String> {
    let fields: Vec<_> = input.split(',').map(str::trim).collect();
    if fields
        .first()
        .is_none_or(|name| !name.eq_ignore_ascii_case("LINE"))
    {
        return Err("Unknown command. Try LINE, x1,y1, x2,y2,".to_owned());
    }
    let coordinates = fields[1..].iter().filter(|field| !field.is_empty());
    let mut values = Vec::new();
    for field in coordinates {
        let value = field
            .parse::<f64>()
            .map_err(|_| "LINE expects four numeric coordinates: x1,y1, x2,y2,".to_owned())?;
        if !value.is_finite() {
            return Err("LINE coordinates must be finite numbers.".to_owned());
        }
        values.push(value);
    }
    if values.len() != 4 {
        return Err("LINE expects four numeric coordinates: x1,y1, x2,y2,".to_owned());
    }
    Ok(ParsedLine {
        start: (values[0], values[1]),
        end: (values[2], values[3]),
    })
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
    fn line_command_adds_a_line_to_the_drawing() {
        let mut engine = Engine::new();

        let result = engine.execute_command("LINE, 0,0, 20,10,");

        assert_eq!(result, "Line created.");
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
    fn invalid_line_command_does_not_change_the_drawing() {
        let mut engine = Engine::new();

        let result = engine.execute_command("LINE, 0, nope, 20,10,");

        assert_eq!(
            result,
            "LINE expects four numeric coordinates: x1,y1, x2,y2,"
        );
        assert_eq!(engine.snapshot().entity_count, 0);
    }
}
