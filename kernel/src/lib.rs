//! Kernel layer (rank 1): 2D geometry, curves, NURBS, B-rep, meshes.

/// A point in the drawing plane.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Point2 {
    pub x: f64,
    pub y: f64,
}

impl Point2 {
    /// Returns the point offset by a vector, or `None` if the result overflows.
    pub fn translated(self, dx: f64, dy: f64) -> Option<Self> {
        let point = Self {
            x: self.x + dx,
            y: self.y + dy,
        };
        (point.x.is_finite() && point.y.is_finite()).then_some(point)
    }
}

/// A direction measured clockwise from north in the drawing plane.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Bearing(f64);

impl Bearing {
    /// Creates a normalized bearing from degrees clockwise from north.
    pub fn from_degrees(degrees: f64) -> Option<Self> {
        degrees.is_finite().then(|| Self(degrees.rem_euclid(360.0)))
    }

    /// Creates a bearing from signed degrees, minutes and seconds.
    pub fn from_dms(degrees: f64, minutes: f64, seconds: f64) -> Option<Self> {
        if !degrees.is_finite()
            || !minutes.is_finite()
            || !seconds.is_finite()
            || !(0.0..60.0).contains(&minutes)
            || !(0.0..60.0).contains(&seconds)
        {
            return None;
        }
        let sign = if degrees.is_sign_negative() {
            -1.0
        } else {
            1.0
        };
        Self::from_degrees(sign * (degrees.abs() + minutes / 60.0 + seconds / 3600.0))
    }

    /// Calculates the bearing from `start` to `end`.
    pub fn from_points(start: Point2, end: Point2) -> Option<Self> {
        let dx = end.x - start.x;
        let dy = end.y - start.y;
        (dx.is_finite() && dy.is_finite() && (dx != 0.0 || dy != 0.0))
            .then(|| Self(dx.atan2(dy).to_degrees().rem_euclid(360.0)))
    }

    pub fn degrees(self) -> f64 {
        self.0
    }

    /// Rotates clockwise by `degrees` and normalizes the result.
    pub fn turned_by(self, degrees: f64) -> Option<Self> {
        Self::from_degrees(self.0 + degrees)
    }

    /// Projects a point from `origin` along this bearing by `distance`.
    pub fn project(self, origin: Point2, distance: f64) -> Option<Point2> {
        if !distance.is_finite() {
            return None;
        }
        let radians = self.0.to_radians();
        origin.translated(distance * radians.sin(), distance * radians.cos())
    }
}

/// A straight segment between two drawing-plane points.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Line2 {
    pub start: Point2,
    pub end: Point2,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum GeometryError {
    /// One or more coordinates are NaN or infinite.
    NonFinitePoint,
    /// The segment endpoints are equal.
    DegenerateLine,
}

impl Line2 {
    /// Creates a finite, non-zero line segment.
    pub fn try_new(start: Point2, end: Point2) -> Result<Self, GeometryError> {
        if ![start.x, start.y, end.x, end.y]
            .into_iter()
            .all(f64::is_finite)
        {
            return Err(GeometryError::NonFinitePoint);
        }
        if start == end {
            return Err(GeometryError::DegenerateLine);
        }
        Ok(Self { start, end })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn bearing_uses_clockwise_from_north_and_projects_points() {
        let origin = Point2 { x: 10.0, y: 20.0 };
        let east = Bearing::from_degrees(90.0).unwrap().project(origin, 5.0);
        let south = Bearing::from_degrees(180.0).unwrap().project(origin, 5.0);

        assert_eq!(east, Some(Point2 { x: 15.0, y: 20.0 }));
        assert_eq!(south, Some(Point2 { x: 10.0, y: 15.0 }));
        assert_eq!(
            Bearing::from_dms(-12.0, 30.0, 0.0).unwrap().degrees(),
            347.5
        );
    }

    #[test]
    fn bearing_from_points_handles_quadrants_and_rejects_invalid_vectors() {
        let origin = Point2 { x: 0.0, y: 0.0 };

        assert_eq!(
            Bearing::from_points(origin, Point2 { x: 1.0, y: 0.0 })
                .unwrap()
                .degrees(),
            90.0
        );
        assert_eq!(
            Bearing::from_points(origin, Point2 { x: 0.0, y: -1.0 })
                .unwrap()
                .degrees(),
            180.0
        );
        assert!(Bearing::from_points(origin, origin).is_none());
        assert!(Bearing::from_degrees(f64::INFINITY).is_none());
    }

    #[test]
    fn coordinate_math_and_line_validation_report_overflow_and_degeneracy() {
        let max = Point2 {
            x: f64::MAX,
            y: 0.0,
        };
        assert!(max.translated(f64::MAX, 0.0).is_none());
        assert_eq!(Line2::try_new(max, max), Err(GeometryError::DegenerateLine));
    }
}
