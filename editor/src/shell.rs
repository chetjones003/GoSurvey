//! Tauri window and IPC commands. Only this file knows about the desktop window.

use std::sync::Mutex;

use gs_application::{DrawingSnapshot, Engine, EngineStatus};

use crate::read_engine;

#[tauri::command]
fn engine_status(engine: tauri::State<'_, Mutex<Engine>>) -> Result<EngineStatus, String> {
    read_engine(&engine, |e| e.status())
}

#[tauri::command]
fn drawing_snapshot(engine: tauri::State<'_, Mutex<Engine>>) -> Result<DrawingSnapshot, String> {
    read_engine(&engine, |e| e.snapshot())
}

pub fn run() {
    tauri::Builder::default()
        .manage(Mutex::new(Engine::new()))
        .invoke_handler(tauri::generate_handler![engine_status, drawing_snapshot])
        .run(tauri::generate_context!())
        .expect("Unable to start GoSurvey");
}
