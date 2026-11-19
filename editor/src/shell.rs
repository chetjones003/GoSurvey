//! Tauri window and IPC commands. Only this file knows about the desktop window.

use std::sync::Mutex;

use gs_application::Engine;

use crate::{WireDrawingSnapshot, WireEngineStatus, read_engine};

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

pub fn run() {
    tauri::Builder::default()
        .manage(Mutex::new(Engine::new()))
        .invoke_handler(tauri::generate_handler![engine_status, drawing_snapshot])
        .run(tauri::generate_context!())
        .expect("Unable to start GoSurvey");
}
