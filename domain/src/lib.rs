//! Domain layer (rank 2): Drawing, Project, Survey, Terrain, PointCloud, PDF.

/// An open drawing. Holds model data only, never GPU-shaped buffers.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Drawing {
    name: String,
}

impl Drawing {
    /// A new drawing with no entities.
    pub fn new() -> Self {
        Self {
            name: "Untitled".to_owned(),
        }
    }

    pub fn name(&self) -> &str {
        &self.name
    }

    pub fn entity_count(&self) -> usize {
        0
    }
}

impl Default for Drawing {
    fn default() -> Self {
        Self::new()
    }
}
