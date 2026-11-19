//! Tauri window and IPC commands. Only this file knows about the desktop window.

use std::sync::Mutex;

use gs_application::Engine;

use crate::{WireCommandResult, WireDrawingSnapshot, WireEngineStatus, read_engine};

#[tauri::command]
fn engine_status(engine: tauri::State<'_, Mutex<Engine>>) -> Result<WireEngineStatus, String> {
    read_engine(&engine, |e| WireEngineStatus::from(e.status()))
}

#[tauri::command]
fn drawing_snapshot(
    engine: tauri::State<'_, Mutex<Engine>>,
) -> Result<WireDrawingSnapshot, String> {
    read_engine(&engine, |e| WireDrawingSnapshot::from(e.snapshot()))
}

#[tauri::command]
fn execute_command(
    engine: tauri::State<'_, Mutex<Engine>>,
    input: String,
) -> Result<WireCommandResult, String> {
    let mut engine = engine
        .lock()
        .map_err(|_| super::ENGINE_UNAVAILABLE.to_owned())?;
    Ok(WireCommandResult::from(engine.execute_command(&input)))
}

pub fn run() {
    tauri::Builder::default()
        .manage(Mutex::new(Engine::new()))
        .invoke_handler(tauri::generate_handler![
            engine_status,
            drawing_snapshot,
            execute_command
        ])
        .run(tauri::generate_context!())
        .expect("Unable to start GoSurvey");
}
