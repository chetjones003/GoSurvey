//! Shared syntax parsing for command names, coordinates, bearings, and distances.
//!
//! Parsing validates and constructs typed inputs; coordinate arithmetic and geometry operations
//! belong in `gs-kernel`. Command implementations should reuse this parser instead of splitting or
//! interpreting coordinate text independently.

use gs_domain::{Bearing, Point2};

/// User-facing parse failures for normalized command input.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum CommandInputError {
    /// Input did not contain exactly two coordinate values.
    ExpectedCoordinatePair,
    /// A coordinate was not a finite number.
    InvalidCoordinate,
    /// Relative coordinates were supplied without a previous point.
    RelativePointNeedsAnchor,
    /// Resolving relative coordinates exceeded the supported floating-point range.
    CoordinateOverflow,
    /// Bearing syntax or DMS fields were invalid.
    InvalidBearing,
    /// A scalar distance was not finite or contained coordinate separators.
    InvalidDistance,
}

impl CommandInputError {
    /// Returns a short message suitable for command history and prompt recovery.
    pub fn message(&self) -> &'static str {
        match self {
            Self::ExpectedCoordinatePair => "Enter a point as X,Y or X Y.",
            Self::InvalidCoordinate => "Coordinates must be finite numbers.",
            Self::RelativePointNeedsAnchor => "A relative point needs a previous point.",
            Self::CoordinateOverflow => "The point is outside the supported coordinate range.",
            Self::InvalidBearing => "Enter a bearing in decimal degrees or DMS.",
            Self::InvalidDistance => "Enter a finite distance.",
        }
    }
}

/// Parsed coordinate intent. Relative values stay as deltas until resolved against an anchor.
#[derive(Debug, Clone, PartialEq)]
pub enum PointInput {
    /// An explicit drawing-plane coordinate.
    Absolute(Point2),
    /// An offset from the previous point.
    Relative { dx: f64, dy: f64 },
}

impl PointInput {
    /// Resolves typed absolute or relative coordinates without doing geometry in a command.
    pub fn resolve(&self, previous: Option<Point2>) -> Result<Point2, CommandInputError> {
        match *self {
            Self::Absolute(point) => Ok(point),
            Self::Relative { dx, dy } => previous
                .ok_or(CommandInputError::RelativePointNeedsAnchor)?
                .translated(dx, dy)
                .ok_or(CommandInputError::CoordinateOverflow),
        }
    }
}

/// A command name and its already-tokenized initial arguments.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ParsedCommand {
    pub name: String,
    pub arguments: Vec<String>,
}

/// Stateless parsers shared by registered commands.
pub struct CommandParser;

impl CommandParser {
    /// Splits command input on commas or whitespace and uppercases its command name.
    pub fn parse_command(input: &str) -> Option<ParsedCommand> {
        let input = input.trim();
        let name_end = input
            .char_indices()
            .find_map(|(index, character)| matches!(character, ',' | ' ' | '\t').then_some(index))
            .unwrap_or(input.len());
        let name = input[..name_end].trim();
        if name.is_empty() {
            return None;
        }
        let arguments = input[name_end..]
            .split(|character: char| character == ',' || character.is_whitespace())
            .filter(|part| !part.trim().is_empty())
            .map(|part| part.trim().to_owned())
            .collect();
        Some(ParsedCommand {
            name: name.to_ascii_uppercase(),
            arguments,
        })
    }

    /// Parses `X,Y`, `X Y`, `@dx,dy`, or `@dx dy` without performing coordinate math.
    pub fn parse_point(
        input: &str,
        previous: Option<Point2>,
    ) -> Result<PointInput, CommandInputError> {
        let input = input.trim();
        let (relative, coordinates) = match input.strip_prefix('@') {
            Some(coordinates) => (true, coordinates),
            None => (false, input),
        };
        let parts: Vec<_> = coordinates
            .split(|character: char| character == ',' || character.is_whitespace())
            .filter(|part| !part.trim().is_empty())
            .collect();
        if parts.len() != 2 {
            return Err(CommandInputError::ExpectedCoordinatePair);
        }
        let x = parse_finite(parts[0])?;
        let y = parse_finite(parts[1])?;
        if relative {
            if previous.is_none() {
                return Err(CommandInputError::RelativePointNeedsAnchor);
            }
            Ok(PointInput::Relative { dx: x, dy: y })
        } else {
            Ok(PointInput::Absolute(Point2 { x, y }))
        }
    }

    /// Parses two already-separated coordinate arguments as one point expression.
    pub fn parse_point_fields(
        fields: &[String],
        previous: Option<Point2>,
    ) -> Result<PointInput, CommandInputError> {
        Self::parse_point(&fields.join(","), previous)
    }

    /// Parses decimal or DMS bearings, optionally preceded by `A` and followed by a signed turn.
    pub fn parse_bearing(input: &str) -> Result<Bearing, CommandInputError> {
        let mut parts = input.split_whitespace().peekable();
        if parts
            .peek()
            .is_some_and(|part| part.eq_ignore_ascii_case("A"))
        {
            parts.next();
        }
        let bearing = parse_dms_or_decimal(parts.next().ok_or(CommandInputError::InvalidBearing)?)?;
        match parts.next() {
            Some(turn) if turn.starts_with('+') || turn.starts_with('-') => {
                let turn = parse_finite(turn).map_err(|_| CommandInputError::InvalidBearing)?;
                if parts.next().is_some() {
                    return Err(CommandInputError::InvalidBearing);
                }
                bearing
                    .turned_by(turn)
                    .ok_or(CommandInputError::InvalidBearing)
            }
            Some(_) => Err(CommandInputError::InvalidBearing),
            None => Ok(bearing),
        }
    }

    /// Parses one finite scalar distance with no coordinate or relative-point syntax.
    pub fn parse_distance(input: &str) -> Result<f64, CommandInputError> {
        if input.contains(',') || input.starts_with('@') || input.chars().any(char::is_whitespace) {
            return Err(CommandInputError::InvalidDistance);
        }
        parse_finite(input).map_err(|_| CommandInputError::InvalidDistance)
    }
}

fn parse_dms_or_decimal(input: &str) -> Result<Bearing, CommandInputError> {
    if let Ok(degrees) = input.parse::<f64>() {
        return Bearing::from_degrees(degrees).ok_or(CommandInputError::InvalidBearing);
    }
    let lowercase = input.to_ascii_lowercase();
    let (degrees, remainder) = lowercase
        .split_once('d')
        .ok_or(CommandInputError::InvalidBearing)?;
    let degrees = parse_finite(degrees).map_err(|_| CommandInputError::InvalidBearing)?;
    let remainder = remainder.trim_end_matches('s');
    let (minutes, seconds) = match remainder.split_once('m') {
        Some((minutes, seconds)) => (
            if minutes.is_empty() {
                0.0
            } else {
                parse_finite(minutes).map_err(|_| CommandInputError::InvalidBearing)?
            },
            if seconds.is_empty() {
                0.0
            } else {
                parse_finite(seconds).map_err(|_| CommandInputError::InvalidBearing)?
            },
        ),
        None if remainder.is_empty() => (0.0, 0.0),
        None => return Err(CommandInputError::InvalidBearing),
    };
    Bearing::from_dms(degrees, minutes, seconds).ok_or(CommandInputError::InvalidBearing)
}

fn parse_finite(input: &str) -> Result<f64, CommandInputError> {
    let value = input
        .parse::<f64>()
        .map_err(|_| CommandInputError::InvalidCoordinate)?;
    if value.is_finite() {
        Ok(value)
    } else {
        Err(CommandInputError::InvalidCoordinate)
    }
}
