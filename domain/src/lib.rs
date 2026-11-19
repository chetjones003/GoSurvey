//! Domain layer (rank 2): Drawing, Project, Survey, Terrain, PointCloud, PDF.

use gs_kernel::Line2;

/// An open drawing. Holds model data only, never GPU-shaped buffers.
#[derive(Debug, Clone, PartialEq)]
pub struct Drawing {
    name: String,
    lines: Vec<Line2>,
}

impl Drawing {
    /// A new drawing with no entities.
    pub fn new() -> Self {
        Self {
            name: "Untitled".to_owned(),
            lines: Vec::new(),
        }
    }

    pub fn name(&self) -> &str {
        &self.name
    }

    pub fn entity_count(&self) -> usize {
        self.lines.len()
    }

    pub fn lines(&self) -> &[Line2] {
        &self.lines
    }

    pub fn add_line(&mut self, start: (f64, f64), end: (f64, f64)) {
        self.lines.push(Line2 {
            start: gs_kernel::Point2 {
                x: start.0,
                y: start.1,
            },
            end: gs_kernel::Point2 { x: end.0, y: end.1 },
        });
    }
}

impl Default for Drawing {
    fn default() -> Self {
        Self::new()
    }
}
