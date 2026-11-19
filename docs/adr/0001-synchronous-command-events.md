# Synchronous command events

The Engine executes one command-line entry at a time, so command handlers publish typed events through a synchronous Foundation event bus and Application event handlers update the drawing and command output in order. This keeps the seam deterministic and easy to test; asynchronous delivery would add scheduling and concurrency before the Rust editor has independent event producers that need it.
