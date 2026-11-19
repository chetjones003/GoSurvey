# Adding a command

Commands belong in `application/src/command/`. A command has two parts:

- A `RegisteredCommand` describes its name, aliases and help text, then parses its initial
  arguments in `start`.
- An `ActiveCommand` stores state while it asks for more input. Use this for interactive commands;
  one-shot commands can return `CommandProgress::finish` directly.

Register a command once when constructing the Engine (or through
`Engine::register_command`). Names and aliases are matched case-insensitively. `HELP` is reserved
and lists registered commands automatically.

The Editor owns native keyboard and pointer handling. It sends the Application `CommandInputEvent`
variants `TextSubmitted`, `PointPicked`, and `Cancelled`; an Application `CommandEventHandler`
routes them through the `CommandManager` and active session. Commands never receive DOM events,
key codes, or pointer coordinates in screen space.

## Session lifecycle

```rust,ignore
impl RegisteredCommand for CircleCommand {
    fn name(&self) -> &'static str { "CIRCLE" }
    fn description(&self) -> &'static str { "draw a circle" }

    fn start(&self, arguments: &[String]) -> CommandProgress {
        match parse_circle_arguments(arguments) {
            Ok(session) => CommandProgress::start(session, []),
            Err(message) => CommandProgress::error(message),
        }
    }
}

impl ActiveCommand for CircleSession {
    fn prompt(&self) -> CommandPrompt {
        CommandPrompt::new("CIRCLE: Specify center", false, true)
    }

    fn handle_event(&mut self, event: &CommandInputEvent) -> Result<CommandOutcome, String> {
        let CommandInputEvent::TextSubmitted(input) = event else {
            return Err("This prompt expects text input.".to_owned());
        };
        let center = CommandParser::parse_point(input, None)
            .map_err(|error| error.message().to_owned())?;
        // Update session state and return Continue or Finish with typed events.
        Ok(CommandOutcome::continue_with([]))
    }
}
```

`CommandProgress::start` stores the session and publishes its prompt. For each later event, the
shared lifecycle trims submitted text, routes `Cancelled`, converts returned errors into a message,
and refreshes the prompt. `Err(message)` keeps the session active, so parsing and validation errors
are recoverable by default. Return `CommandOutcome::finish(events)` to end; the prompt is cleared
automatically. Override `ActiveCommand::cancel` only when cancellation should end a nested mode and
keep the outer command active, as LINE does for a direction pick. Validate before changing session
state when returning an error, so recovery retries from the same state.

`ActiveCommand::handle_event` receives normalized `CommandInputEvent`s. Handle
`TextSubmitted(text)` by parsing text with `CommandParser`; handle `PointPicked { x, y }` as a
drawing-plane point selected by the Editor. The shared lifecycle handles `Cancelled` and invokes
the command's cancellation policy.

Use `CommandParser` for input syntax: `parse_command`, `parse_point`, `parse_point_fields`,
`parse_bearing`, and `parse_distance`. `PointInput::resolve` applies absolute or relative input to
an optional prior point and reports overflow. Return parser errors as their existing user-facing
message (`error.message()`) or provide a more specific command message.

## Math and events

Do not calculate geometry in a command. Use `gs-domain`'s re-exported `gs-kernel` types and
operations: `Point2::translated`, `Bearing::from_points`, `Bearing::project`, `Bearing::turned_by`,
and `Line2::try_new`. Add reusable calculations to `gs-kernel`; commands should parse intent,
manage interaction state, and request those calculations.

Commands return `EngineEvent`s in `CommandOutcome`/`CommandProgress`. Use existing typed events when
possible. Add a new event and an Application `EventHandler` when a new kind of application-side
effect is needed. Keep UI or Tauri types out of command code.
