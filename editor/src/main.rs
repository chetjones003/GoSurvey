#![cfg_attr(all(not(debug_assertions), windows), windows_subsystem = "windows")]

#[cfg(windows)]
fn main() {
    gs_editor::shell::run();
}

#[cfg(not(windows))]
fn main() {
    eprintln!("GoSurvey runs on Windows only.");
}
