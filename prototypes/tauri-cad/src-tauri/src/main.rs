#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]
use gosurvey_spike::{Engine, Event, Snapshot};
use std::sync::Mutex;
use tauri::Manager;

#[tauri::command]
fn dispatch(engine: tauri::State<'_, Mutex<Engine>>, event: Event) -> Result<Snapshot, String> {
    let mut engine = engine
        .lock()
        .map_err(|_| "Drawing engine unavailable. Restart the spike.")?;
    engine.dispatch(event)
}

fn main() {
    tauri::Builder::default()
        .manage(Mutex::new(Engine::demo()))
        .setup(|app| {
            // Native WebView2 automation captures a separate hidden instance, never the user's drawing.
            if std::env::var_os("GOSURVEY_SPIKE_AUTOMATION").is_some() {
                if let Some(window) = app.get_webview_window("main") {
                    window.hide()?;
                }
            }
            Ok(())
        })
        .invoke_handler(tauri::generate_handler![dispatch])
        .run(tauri::generate_context!())
        .expect("Unable to start GoSurvey spike");
}
