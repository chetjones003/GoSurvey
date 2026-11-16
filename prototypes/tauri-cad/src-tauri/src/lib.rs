//! Throwaway 2D port of GoSurvey command phases, line transforms and ORTHO semantics.
//! Document values remain f64; the frontend never commits geometry on its own.
use serde::{Deserialize, Serialize};

#[derive(Clone, Copy, Debug, Deserialize, Serialize, PartialEq)]
pub struct Point {
    pub x: f64,
    pub y: f64,
}
impl Point {
    fn checked(self) -> Result<Self, String> {
        if self.x.is_finite() && self.y.is_finite() && self.x.abs() <= 1e12 && self.y.abs() <= 1e12
        {
            Ok(self)
        } else {
            Err("Coordinates must be finite and within ±1e12 drawing units.".into())
        }
    }
}
#[derive(Clone, Debug, Serialize, PartialEq)]
pub struct Line {
    pub id: u32,
    pub a: Point,
    pub b: Point,
    pub layer: String,
}
#[derive(Clone, Debug)]
enum Phase {
    Idle,
    Line(Option<Point>),
    Select(bool),
    Base(bool),
    Destination(bool, Point),
}
#[derive(Deserialize)]
#[serde(tag = "type", rename_all = "snake_case")]
pub enum Event {
    Snapshot,
    Text { text: String },
    Start { text: String },
    Point { point: Point },
    Select { id: Option<u32>, additive: bool },
    Cancel,
    Undo,
    Redo,
    Ortho { enabled: bool },
    Layer { layer: String },
}
#[derive(Serialize)]
pub struct Snapshot {
    pub lines: Vec<Line>,
    pub selected: Vec<u32>,
    pub prompt: String,
    pub mode: String,
    pub anchor: Option<Point>,
    pub log: Vec<String>,
    pub ortho: bool,
    pub layer: String,
    pub can_undo: bool,
    pub can_redo: bool,
}
pub struct Engine {
    lines: Vec<Line>,
    selected: Vec<u32>,
    phase: Phase,
    next_id: u32,
    undo: Vec<Vec<Line>>,
    redo: Vec<Vec<Line>>,
    log: Vec<String>,
    ortho: bool,
    layer: String,
}
impl Default for Engine {
    fn default() -> Self {
        Self {
            lines: vec![],
            selected: vec![],
            phase: Phase::Idle,
            next_id: 1,
            undo: vec![],
            redo: vec![],
            log: vec!["GoSurvey Rust engine ready. Type HELP for commands.".into()],
            ortho: false,
            layer: "Boundary".into(),
        }
    }
}
impl Engine {
    pub fn demo() -> Self {
        let mut e = Self::default();
        let boundary = [
            (-76., -48.),
            (62., -48.),
            (92., 14.),
            (55., 72.),
            (-50., 72.),
            (-76., -48.),
        ];
        for pair in boundary.windows(2) {
            e.add_raw(
                Point {
                    x: pair[0].0,
                    y: pair[0].1,
                },
                Point {
                    x: pair[1].0,
                    y: pair[1].1,
                },
                "Boundary",
            );
        }
        for (a, b) in [
            ((-76., 8.), (89., 8.)),
            ((-15., -48.), (-15., 72.)),
            ((26., -48.), (26., 72.)),
        ] {
            e.add_raw(
                Point { x: a.0, y: a.1 },
                Point { x: b.0, y: b.1 },
                "Easements",
            );
        }
        let building = [
            (-56., 24.),
            (-30., 24.),
            (-30., 50.),
            (-56., 50.),
            (-56., 24.),
        ];
        for pair in building.windows(2) {
            e.add_raw(
                Point {
                    x: pair[0].0,
                    y: pair[0].1,
                },
                Point {
                    x: pair[1].0,
                    y: pair[1].1,
                },
                "Structures",
            );
        }
        e
    }
    fn add_raw(&mut self, a: Point, b: Point, layer: &str) {
        self.lines.push(Line {
            id: self.next_id,
            a,
            b,
            layer: layer.into(),
        });
        self.next_id += 1;
    }
    fn checkpoint(&mut self) {
        self.undo.push(self.lines.clone());
        if self.undo.len() > 100 {
            self.undo.remove(0);
        }
        self.redo.clear();
    }
    fn message(&mut self, s: impl Into<String>) {
        self.log.push(s.into());
        if self.log.len() > 120 {
            self.log.remove(0);
        }
    }
    pub fn snapshot(&self) -> Snapshot {
        let (mode, prompt, anchor) = match self.phase {
            Phase::Idle => ("select", "Type a command or select linework", None),
            Phase::Line(None) => ("line", "LINE · Specify first point: X,Y or click", None),
            Phase::Line(Some(p)) => (
                "line",
                "LINE · Next point: X,Y / @dX,dY · Enter to finish",
                Some(p),
            ),
            Phase::Select(copy) => (
                if copy { "copy" } else { "move" },
                "Select objects · Enter to continue",
                None,
            ),
            Phase::Base(copy) => (
                if copy { "copy" } else { "move" },
                "Specify base point: X,Y or click",
                None,
            ),
            Phase::Destination(copy, p) => (
                if copy { "copy" } else { "move" },
                "Specify destination: X,Y / @dX,dY or click",
                Some(p),
            ),
        };
        Snapshot {
            lines: self.lines.clone(),
            selected: self.selected.clone(),
            prompt: prompt.into(),
            mode: mode.into(),
            anchor,
            log: self.log.clone(),
            ortho: self.ortho,
            layer: self.layer.clone(),
            can_undo: !self.undo.is_empty(),
            can_redo: !self.redo.is_empty(),
        }
    }
    pub fn dispatch(&mut self, event: Event) -> Result<Snapshot, String> {
        // All mutations are serialized by the Tauri owner. An error is also returned as a command log entry.
        let result = match event {
            Event::Snapshot => Ok(()),
            Event::Text { text } => self.text(text.trim()),
            Event::Start { text } => {
                self.phase = Phase::Idle;
                self.text(text.trim())
            }
            Event::Point { point } => self.point(point),
            Event::Select { id, additive } => self.select(id, additive),
            Event::Cancel => {
                self.phase = Phase::Idle;
                self.selected.clear();
                self.message("*Cancel*");
                Ok(())
            }
            Event::Undo => {
                self.restore(false);
                Ok(())
            }
            Event::Redo => {
                self.restore(true);
                Ok(())
            }
            Event::Ortho { enabled } => {
                self.ortho = enabled;
                Ok(())
            }
            Event::Layer { layer } => {
                if ["Boundary", "Easements", "Structures", "Draft"].contains(&layer.as_str()) {
                    self.layer = layer;
                    Ok(())
                } else {
                    Err("Unknown layer.".into())
                }
            }
        };
        if let Err(message) = result {
            self.message(format!("Error: {message}"));
        }
        Ok(self.snapshot())
    }
    fn select(&mut self, id: Option<u32>, additive: bool) -> Result<(), String> {
        if !matches!(self.phase, Phase::Idle | Phase::Select(_)) {
            return Err("Finish or cancel the active command before changing selection.".into());
        }
        if let Some(id) = id {
            if !self.lines.iter().any(|l| l.id == id) {
                return Err("Entity no longer exists.".into());
            }
        }
        if !additive {
            self.selected.clear();
        }
        if let Some(id) = id {
            if let Some(i) = self.selected.iter().position(|v| *v == id) {
                self.selected.remove(i);
            } else {
                self.selected.push(id);
            }
        }
        Ok(())
    }
    fn text(&mut self, text: &str) -> Result<(), String> {
        if text.is_empty() {
            match self.phase {
                Phase::Select(copy) => {
                    if self.selected.is_empty() {
                        return Err("Select at least one line.".into());
                    }
                    self.phase = Phase::Base(copy);
                }
                Phase::Line(_) => {
                    self.phase = Phase::Idle;
                    self.message("LINE finished.");
                }
                _ => {}
            }
            return Ok(());
        }
        if text.contains(',')
            && !matches!(
                text.split_whitespace()
                    .next()
                    .unwrap_or_default()
                    .to_uppercase()
                    .as_str(),
                "LINE" | "L" | "MOVE" | "M" | "COPY" | "CO" | "CP"
            )
        {
            return self.point(self.parse_point(text)?);
        }
        let mut tokens = text.split_whitespace();
        let command = tokens.next().unwrap_or_default().to_uppercase();
        if matches!(
            command.as_str(),
            "U" | "UNDO" | "REDO" | "ESC" | "CANCEL" | "HELP" | "?"
        ) {
            match command.as_str() {
                "U"|"UNDO"=>self.restore(false),"REDO"=>self.restore(true),
                "ESC"|"CANCEL"=>{self.phase=Phase::Idle;self.selected.clear();self.message("*Cancel*");},
                _=>self.message("LINE (L) · MOVE (M) · COPY (CO/CP) · DELETE (E/ERASE) · UNDO (U) · REDO · PAN (P) · ZOOM (Z) · X,Y · @dX,dY"),
            }
            return Ok(());
        }
        if !matches!(self.phase, Phase::Idle) {
            return Err("Expected a point. Press Esc to cancel the active command.".into());
        }
        match command.as_str() {
            "L" | "LINE" => {
                self.selected.clear();
                self.phase = Phase::Line(None);
                self.message("LINE · Specify first point.");
            }
            "M" | "MOVE" | "CO" | "CP" | "COPY" => {
                let copy = matches!(command.as_str(), "CO" | "CP" | "COPY");
                self.phase = if self.selected.is_empty() {
                    Phase::Select(copy)
                } else {
                    Phase::Base(copy)
                };
                self.message(format!("{command} · {} selected.", self.selected.len()));
            }
            "E" | "ERASE" | "DELETE" | "DEL" => {
                if self.selected.is_empty() {
                    return Err("Select lines before DELETE.".into());
                }
                self.checkpoint();
                let count = self.selected.len();
                self.lines.retain(|l| !self.selected.contains(&l.id));
                self.selected.clear();
                self.message(format!("Deleted {count} line(s)."));
            }
            _ => return Err(format!("Unknown command: {command}. Type HELP.")),
        }
        for token in tokens {
            let p = self.parse_point(token)?;
            self.point(p)?;
        }
        Ok(())
    }
    fn parse_point(&self, text: &str) -> Result<Point, String> {
        let relative = text.starts_with('@');
        let value = text.trim_start_matches('@');
        let (x, y) = value.split_once(',').ok_or("Use X,Y or @dX,dY.")?;
        let mut p = Point {
            x: x.trim().parse().map_err(|_| "Invalid X coordinate.")?,
            y: y.trim().parse().map_err(|_| "Invalid Y coordinate.")?,
        }
        .checked()?;
        if relative {
            let anchor = match self.phase {
                Phase::Line(Some(p)) | Phase::Destination(_, p) => p,
                _ => return Err("Relative entry needs a base point.".into()),
            };
            p = Point {
                x: anchor.x + p.x,
                y: anchor.y + p.y,
            };
        }
        p.checked()
    }
    fn point(&mut self, p: Point) -> Result<(), String> {
        let mut p = p.checked()?;
        let constrain = |a: Point, p: &mut Point| {
            if (p.x - a.x).abs() >= (p.y - a.y).abs() {
                p.y = a.y;
            } else {
                p.x = a.x;
            }
        };
        match self.phase.clone() {
            Phase::Line(None) => self.phase = Phase::Line(Some(p)),
            Phase::Line(Some(a)) => {
                if self.ortho {
                    constrain(a, &mut p);
                }
                if a == p {
                    return Err("Line endpoints must be different.".into());
                }
                self.checkpoint();
                self.add_raw(a, p, &self.layer.clone());
                self.phase = Phase::Line(Some(p));
                self.message(format!(
                    "Line #{} · {:.3} ft",
                    self.next_id - 1,
                    (p.x - a.x).hypot(p.y - a.y)
                ));
            }
            Phase::Base(copy) => self.phase = Phase::Destination(copy, p),
            Phase::Destination(copy, a) => {
                if self.ortho {
                    constrain(a, &mut p);
                }
                let dx = p.x - a.x;
                let dy = p.y - a.y;
                if dx == 0. && dy == 0. {
                    return Err("Displacement must be nonzero.".into());
                }
                let mut edits: Vec<Line> = self
                    .lines
                    .iter()
                    .filter(|l| self.selected.contains(&l.id))
                    .cloned()
                    .collect();
                for line in &mut edits {
                    line.a = Point {
                        x: line.a.x + dx,
                        y: line.a.y + dy,
                    }
                    .checked()?;
                    line.b = Point {
                        x: line.b.x + dx,
                        y: line.b.y + dy,
                    }
                    .checked()?;
                }
                self.checkpoint();
                for mut line in edits {
                    if copy {
                        line.id = self.next_id;
                        self.next_id += 1;
                        self.lines.push(line);
                    } else if let Some(old) = self.lines.iter_mut().find(|l| l.id == line.id) {
                        *old = line;
                    }
                }
                self.message(format!(
                    "{} {} line(s) · Δ {:.3}, {:.3}",
                    if copy { "Copied" } else { "Moved" },
                    self.selected.len(),
                    dx,
                    dy
                ));
                self.phase = Phase::Idle;
            }
            _ => return Err("Start LINE or select a transform before entering a point.".into()),
        }
        Ok(())
    }
    fn restore(&mut self, redo: bool) {
        let previous = if redo {
            self.redo.pop()
        } else {
            self.undo.pop()
        };
        if let Some(lines) = previous {
            if redo {
                self.undo.push(self.lines.clone());
            } else {
                self.redo.push(self.lines.clone());
            }
            self.lines = lines;
            self.selected.clear();
            self.phase = Phase::Idle;
            self.message(if redo { "Redo." } else { "Undo." });
        } else {
            self.message(if redo {
                "Nothing to redo."
            } else {
                "Nothing to undo."
            });
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    fn text(e: &mut Engine, s: &str) {
        e.dispatch(Event::Text { text: s.into() }).unwrap();
    }
    #[test]
    fn state_plane_line_relative_copy_move_delete_undo() {
        let mut e = Engine::default();
        text(&mut e, "LINE");
        text(&mut e, "2000000.10,500000.03");
        text(&mut e, "@10.25,0.5");
        text(&mut e, "");
        assert!((e.lines[0].b.x - 2000010.35).abs() < 1e-9);
        e.dispatch(Event::Select {
            id: Some(1),
            additive: false,
        })
        .unwrap();
        text(&mut e, "COPY");
        text(&mut e, "0,0");
        text(&mut e, "10,20");
        assert_eq!(e.lines.len(), 2);
        text(&mut e, "MOVE");
        text(&mut e, "0,0");
        text(&mut e, "@-2,3");
        assert!((e.lines[0].a.x - 1999998.10).abs() < 1e-9);
        text(&mut e, "DELETE");
        assert_eq!(e.lines.len(), 1);
        text(&mut e, "UNDO");
        assert_eq!(e.lines.len(), 2);
        text(&mut e, "REDO");
        assert_eq!(e.lines.len(), 1);
    }
    #[test]
    fn invalid_and_degenerate_input_does_not_mutate() {
        let mut e = Engine::default();
        text(&mut e, "L");
        text(&mut e, "1,2");
        text(&mut e, "NaN,3");
        text(&mut e, "1,2");
        text(&mut e, "@inf,0");
        assert!(e.lines.is_empty());
        text(&mut e, "3,4");
        assert_eq!(e.lines.len(), 1);
        text(&mut e, "CANCEL");
        text(&mut e, "DELETE");
        assert_eq!(e.lines.len(), 1);
    }
    #[test]
    fn selection_phase_ortho_and_cancel() {
        let mut e = Engine::demo();
        text(&mut e, "MOVE");
        text(&mut e, "");
        assert!(matches!(e.phase, Phase::Select(false)));
        e.dispatch(Event::Select {
            id: Some(1),
            additive: false,
        })
        .unwrap();
        text(&mut e, "");
        text(&mut e, "0,0");
        e.dispatch(Event::Ortho { enabled: true }).unwrap();
        text(&mut e, "10,3");
        assert_eq!(e.lines[0].a.y, -48.);
        let lines = e.lines.clone();
        text(&mut e, "COPY");
        text(&mut e, "0,0");
        text(&mut e, "CANCEL");
        assert_eq!(e.lines, lines);
    }
}
