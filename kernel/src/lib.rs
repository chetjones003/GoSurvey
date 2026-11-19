//! Kernel layer (rank 1): 2D geometry, curves, NURBS, B-rep, meshes.

/// A point in the drawing plane.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Point2 {
    pub x: f64,
    pub y: f64,
}

/// A straight segment between two drawing-plane points.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Line2 {
    pub start: Point2,
    pub end: Point2,
}
