use gs_domain::Point2;

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum PointParseError {
    ExpectedCoordinatePair,
    InvalidCoordinate,
    RelativePointNeedsAnchor,
    CoordinateOverflow,
}

impl PointParseError {
    pub fn message(&self) -> &'static str {
        match self {
            Self::ExpectedCoordinatePair => "Enter a point as X,Y or X Y.",
            Self::InvalidCoordinate => "Coordinates must be finite numbers.",
            Self::RelativePointNeedsAnchor => "A relative point needs a previous point.",
            Self::CoordinateOverflow => "The point is outside the supported coordinate range.",
        }
    }
}

#[derive(Debug, Clone, PartialEq)]
pub enum PointInput {
    Absolute(Point2),
    Relative { dx: f64, dy: f64 },
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ParsedCommand {
    pub name: String,
    pub arguments: Vec<String>,
}

pub struct CommandParser;

impl CommandParser {
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

    pub fn parse_point(
        input: &str,
        previous: Option<Point2>,
    ) -> Result<PointInput, PointParseError> {
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
            return Err(PointParseError::ExpectedCoordinatePair);
        }
        let x = parse_finite(parts[0])?;
        let y = parse_finite(parts[1])?;
        if relative {
            if previous.is_none() {
                return Err(PointParseError::RelativePointNeedsAnchor);
            }
            Ok(PointInput::Relative { dx: x, dy: y })
        } else {
            Ok(PointInput::Absolute(Point2 { x, y }))
        }
    }

    pub fn parse_point_fields(
        fields: &[String],
        previous: Option<Point2>,
    ) -> Result<PointInput, PointParseError> {
        Self::parse_point(&fields.join(","), previous)
    }
}

fn parse_finite(input: &str) -> Result<f64, PointParseError> {
    let value = input
        .parse::<f64>()
        .map_err(|_| PointParseError::InvalidCoordinate)?;
    if value.is_finite() {
        Ok(value)
    } else {
        Err(PointParseError::InvalidCoordinate)
    }
}
