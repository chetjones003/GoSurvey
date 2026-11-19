use gs_domain::{Line2, Point2};

use crate::events::{CommandPrompt, EngineEvent};

use super::parser::{CommandParser, PointInput};
use super::{ActiveCommand, CommandProgress, RegisteredCommand};

const FIRST_POINT_PROMPT: &str = "LINE: First point — click or type X,Y / X Y. Esc cancels.";
const NEXT_POINT_PROMPT: &str =
    "LINE: Next point — click; X,Y; @dx,dy; A bearing; 2P direction. Esc ends.";
const LOCKED_POINT_PROMPT: &str =
    "LINE (bearing lock): distance, click or point; A clears the lock. Esc ends.";

pub(super) struct LineCommand;

impl RegisteredCommand for LineCommand {
    fn name(&self) -> &'static str {
        "LINE"
    }

    fn description(&self) -> &'static str {
        "draw repeated segments from typed points"
    }

    fn aliases(&self) -> &'static [&'static str] {
        &["L"]
    }

    fn start(&self, arguments: &[String]) -> CommandProgress {
        match arguments.len() {
            0 => LineSession::first_point().keep(Vec::new()),
            2 => match CommandParser::parse_point_fields(arguments, None) {
                Ok(PointInput::Absolute(point)) => {
                    LineSession::next_point(point, 0).keep(Vec::new())
                }
                Ok(PointInput::Relative { .. }) => {
                    CommandProgress::finish([EngineEvent::CommandMessage(
                        "The first LINE point must be absolute X,Y coordinates.".to_owned(),
                    )])
                }
                Err(error) => CommandProgress::finish([EngineEvent::CommandMessage(
                    error.message().to_owned(),
                )]),
            },
            4 => {
                let start = CommandParser::parse_point_fields(&arguments[..2], None);
                let end = match &start {
                    Ok(PointInput::Absolute(point)) => {
                        CommandParser::parse_point_fields(&arguments[2..], Some(*point))
                    }
                    _ => CommandParser::parse_point_fields(&arguments[2..], None),
                };
                match (start, end) {
                    (Ok(PointInput::Absolute(start)), Ok(end)) => {
                        let end = match resolve_point(end, Some(start)) {
                            Ok(end) => end,
                            Err(error) => {
                                return CommandProgress::finish([EngineEvent::CommandMessage(
                                    error.message().to_owned(),
                                )]);
                            }
                        };
                        if start == end {
                            return CommandProgress::finish([EngineEvent::CommandMessage(
                                "LINE points must be different.".to_owned(),
                            )]);
                        }
                        LineSession::next_point(end, 1)
                            .keep(vec![EngineEvent::LineAdded(Line2 { start, end })])
                    }
                    (Err(error), _) | (_, Err(error)) => {
                        CommandProgress::finish([EngineEvent::CommandMessage(
                            error.message().to_owned(),
                        )])
                    }
                    _ => CommandProgress::finish([EngineEvent::CommandMessage(
                        "The first LINE point must be absolute X,Y coordinates.".to_owned(),
                    )]),
                }
            }
            _ => CommandProgress::finish([EngineEvent::CommandMessage(
                "LINE accepts zero, one, or two points after the command.".to_owned(),
            )]),
        }
    }
}

#[derive(Clone, Copy)]
enum LineMode {
    Point,
    Bearing,
    DirectionFirst {
        restore_bearing: Option<f64>,
    },
    DirectionSecond {
        first: Point2,
        restore_bearing: Option<f64>,
    },
    ConfirmBearing {
        bearing: f64,
        restore_bearing: Option<f64>,
    },
}

struct LineSession {
    previous: Option<Point2>,
    created_segments: usize,
    bearing: Option<f64>,
    mode: LineMode,
}

impl LineSession {
    fn first_point() -> Self {
        Self {
            previous: None,
            created_segments: 0,
            bearing: None,
            mode: LineMode::Point,
        }
    }

    fn next_point(previous: Point2, created_segments: usize) -> Self {
        Self {
            previous: Some(previous),
            created_segments,
            bearing: None,
            mode: LineMode::Point,
        }
    }

    fn keep(self, mut events: Vec<EngineEvent>) -> CommandProgress {
        events.push(EngineEvent::PromptChanged(Some(self.prompt())));
        CommandProgress::continue_with(self, events)
    }

    fn prompt(&self) -> CommandPrompt {
        match self.mode {
            LineMode::Point if self.previous.is_none() => {
                CommandPrompt::new(FIRST_POINT_PROMPT, false, true)
            }
            LineMode::Point if self.bearing.is_some() => {
                CommandPrompt::new(LOCKED_POINT_PROMPT, false, true)
            }
            LineMode::Point => CommandPrompt::new(NEXT_POINT_PROMPT, false, true),
            LineMode::Bearing => CommandPrompt::new(
                "LINE: Type a bearing in degrees clockwise from north. Enter cancels bearing entry.",
                true,
                false,
            ),
            LineMode::DirectionFirst { .. } => CommandPrompt::new(
                "LINE 2P: Pick or type the first direction point. Esc cancels direction pick.",
                false,
                true,
            ),
            LineMode::DirectionSecond { .. } => CommandPrompt::new(
                "LINE 2P: Pick or type the second direction point. Esc cancels direction pick.",
                false,
                true,
            ),
            LineMode::ConfirmBearing { .. } => CommandPrompt::new(
                "LINE 2P: Enter locks the bearing; type +90 or -45 to adjust and lock.",
                true,
                false,
            ),
        }
    }

    fn submit(mut self, input: &str) -> CommandProgress {
        let input = input.trim();
        if input.eq_ignore_ascii_case("ESC") || input.eq_ignore_ascii_case("CANCEL") {
            return self.cancel_input_mode();
        }

        match self.mode {
            LineMode::Bearing => return self.submit_bearing(input),
            LineMode::DirectionFirst { restore_bearing } => {
                return self.submit_first_direction_point(input, restore_bearing);
            }
            LineMode::DirectionSecond {
                first,
                restore_bearing,
            } => {
                return self.submit_second_direction_point(input, first, restore_bearing);
            }
            LineMode::ConfirmBearing {
                bearing,
                restore_bearing,
            } => {
                return self.confirm_bearing(input, bearing, restore_bearing);
            }
            LineMode::Point => {}
        }

        if input.is_empty() {
            return self.keep(Vec::new());
        }
        if input.eq_ignore_ascii_case("A") {
            return self.change_bearing_lock();
        }
        if input.eq_ignore_ascii_case("2P")
            || input.eq_ignore_ascii_case("AP")
            || input.eq_ignore_ascii_case("ANGLEPICK")
        {
            return self.begin_direction_pick();
        }
        if input
            .get(..1)
            .is_some_and(|prefix| prefix.eq_ignore_ascii_case("A"))
        {
            if let Some(bearing) = parse_bearing_entry(input) {
                self.bearing = Some(bearing);
                return self.keep(vec![EngineEvent::CommandMessage(
                    "Bearing locked.".to_owned(),
                )]);
            }
            return self.keep(vec![EngineEvent::CommandMessage(
                "Enter A followed by a bearing, such as A 45 or A 132d30m.".to_owned(),
            )]);
        }

        if let (Some(bearing), Some(previous)) = (self.bearing, self.previous)
            && let Some(distance) = parse_distance(input)
        {
            let radians = bearing.to_radians();
            let point = Point2 {
                x: previous.x + distance * radians.sin(),
                y: previous.y + distance * radians.cos(),
            };
            if !point_is_finite(point) {
                return self.keep(vec![EngineEvent::CommandMessage(
                    "The point is outside the supported coordinate range.".to_owned(),
                )]);
            }
            return self.add_segment(point);
        }

        match CommandParser::parse_point(input, self.previous) {
            Ok(point) => match resolve_point(point, self.previous) {
                Ok(point) => self.add_segment(point),
                Err(error) => self.keep(vec![EngineEvent::CommandMessage(
                    error.message().to_owned(),
                )]),
            },
            Err(error) => self.keep(vec![EngineEvent::CommandMessage(
                error.message().to_owned(),
            )]),
        }
    }

    fn submit_bearing(mut self, input: &str) -> CommandProgress {
        if input.is_empty() {
            self.mode = LineMode::Point;
            return self.keep(vec![EngineEvent::CommandMessage(
                "Bearing entry cancelled.".to_owned(),
            )]);
        }
        match parse_bearing(input) {
            Some(bearing) => {
                self.bearing = Some(bearing);
                self.mode = LineMode::Point;
                self.keep(vec![EngineEvent::CommandMessage(
                    "Bearing locked.".to_owned(),
                )])
            }
            None => self.keep(vec![EngineEvent::CommandMessage(
                "Enter a bearing in decimal degrees or DMS.".to_owned(),
            )]),
        }
    }

    fn change_bearing_lock(mut self) -> CommandProgress {
        if self.previous.is_none() {
            return self.keep(vec![EngineEvent::CommandMessage(
                "Specify the first LINE point before entering a bearing.".to_owned(),
            )]);
        }
        if self.bearing.take().is_some() {
            self.keep(vec![EngineEvent::CommandMessage(
                "Bearing lock cleared.".to_owned(),
            )])
        } else {
            self.mode = LineMode::Bearing;
            self.keep(Vec::new())
        }
    }

    fn begin_direction_pick(mut self) -> CommandProgress {
        if self.previous.is_none() {
            return self.keep(vec![EngineEvent::CommandMessage(
                "Specify the first LINE point before choosing a direction.".to_owned(),
            )]);
        }
        self.mode = LineMode::DirectionFirst {
            restore_bearing: self.bearing,
        };
        self.keep(Vec::new())
    }

    fn submit_first_direction_point(
        mut self,
        input: &str,
        restore_bearing: Option<f64>,
    ) -> CommandProgress {
        match CommandParser::parse_point(input, self.previous) {
            Ok(point) => match resolve_point(point, self.previous) {
                Ok(first) => {
                    self.mode = LineMode::DirectionSecond {
                        first,
                        restore_bearing,
                    };
                    self.keep(Vec::new())
                }
                Err(error) => self.keep(vec![EngineEvent::CommandMessage(
                    error.message().to_owned(),
                )]),
            },
            Err(_) => self.keep(vec![EngineEvent::CommandMessage(
                "Enter the first direction point as X,Y or X Y.".to_owned(),
            )]),
        }
    }

    fn submit_second_direction_point(
        mut self,
        input: &str,
        first: Point2,
        restore_bearing: Option<f64>,
    ) -> CommandProgress {
        let second = match CommandParser::parse_point(input, self.previous) {
            Ok(point) => match resolve_point(point, self.previous) {
                Ok(second) => second,
                Err(error) => {
                    return self.keep(vec![EngineEvent::CommandMessage(
                        error.message().to_owned(),
                    )]);
                }
            },
            Err(_) => {
                return self.keep(vec![EngineEvent::CommandMessage(
                    "Enter the second direction point as X,Y or X Y.".to_owned(),
                )]);
            }
        };
        if first == second {
            return self.keep(vec![EngineEvent::CommandMessage(
                "Direction points must be different.".to_owned(),
            )]);
        }
        let dx = second.x - first.x;
        let dy = second.y - first.y;
        if !dx.is_finite() || !dy.is_finite() {
            return self.keep(vec![EngineEvent::CommandMessage(
                "Direction points are too far apart to calculate a bearing.".to_owned(),
            )]);
        }
        let bearing = dx.atan2(dy).to_degrees().rem_euclid(360.0);
        self.mode = LineMode::ConfirmBearing {
            bearing,
            restore_bearing,
        };
        self.keep(Vec::new())
    }

    fn confirm_bearing(
        mut self,
        input: &str,
        bearing: f64,
        restore_bearing: Option<f64>,
    ) -> CommandProgress {
        if input.is_empty() {
            self.bearing = Some(bearing);
            self.mode = LineMode::Point;
            return self.keep(vec![EngineEvent::CommandMessage(
                "Bearing locked.".to_owned(),
            )]);
        }
        match input.parse::<f64>() {
            Ok(turn) if turn.is_finite() && (input.starts_with('+') || input.starts_with('-')) => {
                let adjusted = bearing + turn;
                if !adjusted.is_finite() {
                    return self.keep(vec![EngineEvent::CommandMessage(
                        "The adjusted bearing is outside the supported range.".to_owned(),
                    )]);
                }
                self.bearing = Some(adjusted);
                self.mode = LineMode::Point;
                self.keep(vec![EngineEvent::CommandMessage(
                    "Adjusted bearing locked.".to_owned(),
                )])
            }
            _ => {
                self.mode = LineMode::ConfirmBearing {
                    bearing,
                    restore_bearing,
                };
                self.keep(vec![EngineEvent::CommandMessage(
                    "Press Enter to lock, or type a turn such as +90 or -45.".to_owned(),
                )])
            }
        }
    }

    fn add_segment(mut self, point: Point2) -> CommandProgress {
        let Some(previous) = self.previous else {
            self.previous = Some(point);
            return self.keep(Vec::new());
        };
        if previous == point {
            return self.keep(vec![EngineEvent::CommandMessage(
                "LINE points must be different.".to_owned(),
            )]);
        }
        self.previous = Some(point);
        self.created_segments += 1;
        self.keep(vec![EngineEvent::LineAdded(Line2 {
            start: previous,
            end: point,
        })])
    }

    fn cancel_input_mode(mut self) -> CommandProgress {
        match self.mode {
            LineMode::Bearing => {
                self.mode = LineMode::Point;
                self.keep(vec![EngineEvent::CommandMessage(
                    "Bearing entry cancelled.".to_owned(),
                )])
            }
            LineMode::DirectionFirst { restore_bearing }
            | LineMode::DirectionSecond {
                restore_bearing, ..
            }
            | LineMode::ConfirmBearing {
                restore_bearing, ..
            } => {
                self.bearing = restore_bearing;
                self.mode = LineMode::Point;
                self.keep(vec![EngineEvent::CommandMessage(
                    "Direction pick cancelled.".to_owned(),
                )])
            }
            LineMode::Point => {
                let message = if self.created_segments == 0 {
                    "Command cancelled."
                } else {
                    "Command finished."
                };
                CommandProgress::finish([
                    EngineEvent::PromptChanged(None),
                    EngineEvent::CommandMessage(message.to_owned()),
                ])
            }
        }
    }
}

impl ActiveCommand for LineSession {
    fn submit(self: Box<Self>, input: &str) -> CommandProgress {
        LineSession::submit(*self, input)
    }
}

fn resolve_point(
    input: PointInput,
    previous: Option<Point2>,
) -> Result<Point2, super::parser::PointParseError> {
    use super::parser::PointParseError;

    let point = match input {
        PointInput::Absolute(point) => point,
        PointInput::Relative { dx, dy } => {
            let previous = previous.ok_or(PointParseError::RelativePointNeedsAnchor)?;
            Point2 {
                x: previous.x + dx,
                y: previous.y + dy,
            }
        }
    };
    if point_is_finite(point) {
        Ok(point)
    } else {
        Err(PointParseError::CoordinateOverflow)
    }
}

fn point_is_finite(point: Point2) -> bool {
    point.x.is_finite() && point.y.is_finite()
}

fn parse_distance(input: &str) -> Option<f64> {
    if input.contains(',') || input.chars().any(char::is_whitespace) || input.starts_with('@') {
        return None;
    }
    let distance = input.parse::<f64>().ok()?;
    distance.is_finite().then_some(distance)
}

fn parse_bearing_entry(input: &str) -> Option<f64> {
    let mut parts = input.split_whitespace();
    if !parts.next()?.eq_ignore_ascii_case("A") {
        return None;
    }
    let bearing = parse_bearing(parts.next()?)?;
    let turn = match parts.next() {
        Some(turn) if turn.starts_with('+') || turn.starts_with('-') => {
            let turn = turn.parse::<f64>().ok()?;
            if !turn.is_finite() || parts.next().is_some() {
                return None;
            }
            turn
        }
        Some(_) => return None,
        None => 0.0,
    };
    let bearing = bearing + turn;
    bearing.is_finite().then_some(bearing)
}

fn parse_bearing(input: &str) -> Option<f64> {
    let input = input.trim().to_ascii_lowercase();
    if let Ok(degrees) = input.parse::<f64>() {
        return degrees.is_finite().then_some(degrees);
    }
    let (degrees, remainder) = input.split_once('d')?;
    let degrees = degrees.parse::<f64>().ok()?;
    if !degrees.is_finite() {
        return None;
    }
    let sign = if degrees.is_sign_negative() {
        -1.0
    } else {
        1.0
    };
    let remainder = remainder.trim_end_matches('s');
    let (minutes, seconds) = match remainder.split_once('m') {
        Some((minutes, seconds)) => {
            let minutes = if minutes.is_empty() {
                0.0
            } else {
                minutes.parse::<f64>().ok()?
            };
            let seconds = if seconds.is_empty() {
                0.0
            } else {
                seconds.parse::<f64>().ok()?
            };
            (minutes, seconds)
        }
        None if remainder.is_empty() => (0.0, 0.0),
        None => return None,
    };
    if !minutes.is_finite()
        || !seconds.is_finite()
        || minutes.abs() >= 60.0
        || seconds.abs() >= 60.0
    {
        return None;
    }
    let bearing = sign * (degrees.abs() + minutes / 60.0 + seconds / 3600.0);
    bearing.is_finite().then_some(bearing)
}
