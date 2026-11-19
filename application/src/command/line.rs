use gs_domain::{Bearing, GeometryError, Line2, Point2};

use crate::events::{CommandPrompt, EngineEvent};

use super::parser::{CommandParser, PointInput};
use super::{ActiveCommand, CommandInputEvent, CommandOutcome, CommandProgress, RegisteredCommand};

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
        "draw repeated segments from points and directions"
    }

    fn aliases(&self) -> &'static [&'static str] {
        &["L"]
    }

    fn start(&self, arguments: &[String]) -> CommandProgress {
        match arguments.len() {
            0 => CommandProgress::start(LineSession::new(None, 0), []),
            2 => match CommandParser::parse_point_fields(arguments, None) {
                Ok(PointInput::Absolute(point)) => {
                    CommandProgress::start(LineSession::new(Some(point), 0), [])
                }
                Ok(PointInput::Relative { .. }) => CommandProgress::finish([message(
                    "The first LINE point must be absolute X,Y coordinates.",
                )]),
                Err(error) => CommandProgress::finish([message(error.message())]),
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
                        let end = match end.resolve(Some(start)) {
                            Ok(end) => end,
                            Err(error) => {
                                return CommandProgress::finish([message(error.message())]);
                            }
                        };
                        let line = match Line2::try_new(start, end) {
                            Ok(line) => line,
                            Err(error) => return CommandProgress::finish([geometry_error(error)]),
                        };
                        CommandProgress::start(
                            LineSession::new(Some(end), 1),
                            [EngineEvent::LineAdded(line)],
                        )
                    }
                    (Err(error), _) | (_, Err(error)) => {
                        CommandProgress::finish([message(error.message())])
                    }
                    _ => CommandProgress::finish([message(
                        "The first LINE point must be absolute X,Y coordinates.",
                    )]),
                }
            }
            _ => CommandProgress::finish([message(
                "LINE accepts zero, one, or two points after the command.",
            )]),
        }
    }
}

#[derive(Clone, Copy)]
enum LineMode {
    Point,
    Bearing,
    DirectionFirst {
        restore_bearing: Option<Bearing>,
    },
    DirectionSecond {
        first: Point2,
        restore_bearing: Option<Bearing>,
    },
    ConfirmBearing {
        bearing: Bearing,
        restore_bearing: Option<Bearing>,
    },
}

struct LineSession {
    previous: Option<Point2>,
    created_segments: usize,
    bearing: Option<Bearing>,
    mode: LineMode,
}

impl LineSession {
    fn new(previous: Option<Point2>, created_segments: usize) -> Self {
        Self {
            previous,
            created_segments,
            bearing: None,
            mode: LineMode::Point,
        }
    }

    fn point_prompt(&self) -> CommandPrompt {
        let text = match (self.previous, self.bearing) {
            (None, _) => FIRST_POINT_PROMPT,
            (Some(_), Some(_)) => LOCKED_POINT_PROMPT,
            (Some(_), None) => NEXT_POINT_PROMPT,
        };
        CommandPrompt::new(text, false, true)
    }

    fn submit_point(&mut self, input: &str) -> Result<CommandOutcome, String> {
        if input.is_empty() {
            if self.previous.is_some() {
                return Ok(CommandOutcome::finish([message("Command finished.")]));
            }
            return Ok(CommandOutcome::continue_with([]));
        }
        if input.eq_ignore_ascii_case("A") {
            return self.change_bearing_lock();
        }
        if matches!(
            input.to_ascii_uppercase().as_str(),
            "2P" | "AP" | "ANGLEPICK"
        ) {
            return self.begin_direction_pick();
        }
        if input
            .get(..1)
            .is_some_and(|prefix| prefix.eq_ignore_ascii_case("A"))
        {
            self.bearing = Some(CommandParser::parse_bearing(input).map_err(|error| {
                match error {
                    super::parser::CommandInputError::InvalidBearing => {
                        "Enter A followed by a bearing, such as A 45 or A 132d30m."
                    }
                    _ => error.message(),
                }
                .to_owned()
            })?);
            return Ok(CommandOutcome::continue_with([message("Bearing locked.")]));
        }

        if let (Some(bearing), Some(previous)) = (self.bearing, self.previous)
            && let Ok(distance) = CommandParser::parse_distance(input)
        {
            let point = bearing
                .project(previous, distance)
                .ok_or_else(|| "The point is outside the supported coordinate range.".to_owned())?;
            return self.add_point(point);
        }

        let point_input = CommandParser::parse_point(input, self.previous)
            .map_err(|error| error.message().to_owned())?;
        let point = point_input
            .resolve(self.previous)
            .map_err(|error| error.message().to_owned())?;
        self.add_point(point)
    }

    fn submit_bearing(&mut self, input: &str) -> Result<CommandOutcome, String> {
        if input.is_empty() {
            self.mode = LineMode::Point;
            return Ok(CommandOutcome::continue_with([message(
                "Bearing entry cancelled.",
            )]));
        }
        self.bearing =
            Some(CommandParser::parse_bearing(input).map_err(|error| error.message().to_owned())?);
        self.mode = LineMode::Point;
        Ok(CommandOutcome::continue_with([message("Bearing locked.")]))
    }

    fn submit_first_direction_point(
        &mut self,
        input: &str,
        restore_bearing: Option<Bearing>,
    ) -> Result<CommandOutcome, String> {
        let point = CommandParser::parse_point(input, self.previous)
            .and_then(|point| point.resolve(self.previous))
            .map_err(|_| "Enter the first direction point as X,Y or X Y.".to_owned())?;
        self.select_first_direction_point(point, restore_bearing)
    }

    fn select_first_direction_point(
        &mut self,
        point: Point2,
        restore_bearing: Option<Bearing>,
    ) -> Result<CommandOutcome, String> {
        self.mode = LineMode::DirectionSecond {
            first: point,
            restore_bearing,
        };
        Ok(CommandOutcome::continue_with([]))
    }

    fn submit_second_direction_point(
        &mut self,
        input: &str,
        first: Point2,
        restore_bearing: Option<Bearing>,
    ) -> Result<CommandOutcome, String> {
        let second = CommandParser::parse_point(input, self.previous)
            .and_then(|point| point.resolve(self.previous))
            .map_err(|_| "Enter the second direction point as X,Y or X Y.".to_owned())?;
        self.select_second_direction_point(second, first, restore_bearing)
    }

    fn select_second_direction_point(
        &mut self,
        second: Point2,
        first: Point2,
        restore_bearing: Option<Bearing>,
    ) -> Result<CommandOutcome, String> {
        let bearing = Bearing::from_points(first, second)
            .ok_or_else(|| "Direction points must be different and finite.".to_owned())?;
        self.mode = LineMode::ConfirmBearing {
            bearing,
            restore_bearing,
        };
        Ok(CommandOutcome::continue_with([]))
    }

    fn confirm_bearing(&mut self, input: &str, bearing: Bearing) -> Result<CommandOutcome, String> {
        if input.is_empty() {
            self.bearing = Some(bearing);
            self.mode = LineMode::Point;
            return Ok(CommandOutcome::continue_with([message("Bearing locked.")]));
        }
        if !input.starts_with('+') && !input.starts_with('-') {
            return Err("Press Enter to lock, or type a turn such as +90 or -45.".to_owned());
        }
        let turn = input
            .parse::<f64>()
            .ok()
            .filter(|turn| turn.is_finite())
            .ok_or_else(|| "Enter a finite turn such as +90 or -45.".to_owned())?;
        self.bearing =
            Some(bearing.turned_by(turn).ok_or_else(|| {
                "The adjusted bearing is outside the supported range.".to_owned()
            })?);
        self.mode = LineMode::Point;
        Ok(CommandOutcome::continue_with([message(
            "Adjusted bearing locked.",
        )]))
    }

    fn add_point(&mut self, point: Point2) -> Result<CommandOutcome, String> {
        let Some(previous) = self.previous else {
            self.previous = Some(point);
            return Ok(CommandOutcome::continue_with([]));
        };
        let line = Line2::try_new(previous, point).map_err(|error| match error {
            GeometryError::DegenerateLine => "LINE points must be different.".to_owned(),
            GeometryError::NonFinitePoint => {
                "The point is outside the supported coordinate range.".to_owned()
            }
        })?;
        self.previous = Some(point);
        self.created_segments += 1;
        Ok(CommandOutcome::continue_with([EngineEvent::LineAdded(
            line,
        )]))
    }

    fn change_bearing_lock(&mut self) -> Result<CommandOutcome, String> {
        if self.previous.is_none() {
            return Err("Specify the first LINE point before entering a bearing.".to_owned());
        }
        if self.bearing.take().is_some() {
            Ok(CommandOutcome::continue_with([message(
                "Bearing lock cleared.",
            )]))
        } else {
            self.mode = LineMode::Bearing;
            Ok(CommandOutcome::continue_with([]))
        }
    }

    fn begin_direction_pick(&mut self) -> Result<CommandOutcome, String> {
        if self.previous.is_none() {
            return Err("Specify the first LINE point before choosing a direction.".to_owned());
        }
        self.mode = LineMode::DirectionFirst {
            restore_bearing: self.bearing,
        };
        Ok(CommandOutcome::continue_with([]))
    }
}

impl ActiveCommand for LineSession {
    fn prompt(&self) -> CommandPrompt {
        match self.mode {
            LineMode::Point => self.point_prompt(),
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

    fn handle_event(&mut self, event: &CommandInputEvent) -> Result<CommandOutcome, String> {
        match event {
            CommandInputEvent::TextSubmitted(input) => match self.mode {
                LineMode::Point => self.submit_point(input),
                LineMode::Bearing => self.submit_bearing(input),
                LineMode::DirectionFirst { restore_bearing } => {
                    self.submit_first_direction_point(input, restore_bearing)
                }
                LineMode::DirectionSecond {
                    first,
                    restore_bearing,
                } => self.submit_second_direction_point(input, first, restore_bearing),
                LineMode::ConfirmBearing { bearing, .. } => self.confirm_bearing(input, bearing),
            },
            CommandInputEvent::PointPicked(point) => match self.mode {
                LineMode::Point => self.add_point(*point),
                LineMode::DirectionFirst { restore_bearing } => {
                    self.select_first_direction_point(*point, restore_bearing)
                }
                LineMode::DirectionSecond {
                    first,
                    restore_bearing,
                } => self.select_second_direction_point(*point, first, restore_bearing),
                LineMode::Bearing | LineMode::ConfirmBearing { .. } => {
                    Err("A point cannot be picked while entering a bearing.".to_owned())
                }
            },
            CommandInputEvent::Cancelled => Ok(self.cancel()),
        }
    }

    fn cancel(&mut self) -> CommandOutcome {
        match self.mode {
            LineMode::Bearing => {
                self.mode = LineMode::Point;
                CommandOutcome::continue_with([message("Bearing entry cancelled.")])
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
                CommandOutcome::continue_with([message("Direction pick cancelled.")])
            }
            LineMode::Point => CommandOutcome::finish([message(if self.created_segments == 0 {
                "Command cancelled."
            } else {
                "Command finished."
            })]),
        }
    }
}

fn message(text: &str) -> EngineEvent {
    EngineEvent::CommandMessage(text.to_owned())
}

fn geometry_error(error: GeometryError) -> EngineEvent {
    message(match error {
        GeometryError::DegenerateLine => "LINE points must be different.",
        GeometryError::NonFinitePoint => "The point is outside the supported coordinate range.",
    })
}
