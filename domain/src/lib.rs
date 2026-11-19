//! Domain layer (rank 2): Drawing, Project, Survey, Terrain, PointCloud, PDF.

pub use gs_kernel::{Line2, Point2};

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

    pub fn add_line(&mut self, line: Line2) {
        self.lines.push(line);
    }
}

impl Default for Drawing {
    fn default() -> Self {
        Self::new()
    }
}
