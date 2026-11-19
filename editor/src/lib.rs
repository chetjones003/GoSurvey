//! Editor layer (rank 5): Tauri shell and ui-sdk front end.

use std::sync::Mutex;

use gs_application::Engine;

/// Shown to the user when the engine's lock is poisoned by a panic.
pub const ENGINE_UNAVAILABLE: &str = "Drawing engine unavailable. Restart GoSurvey.";

/// Runs `read` against the engine, or reports that the engine is unavailable.
pub fn read_engine<T>(
    engine: &Mutex<Engine>,
    read: impl FnOnce(&Engine) -> T,
) -> Result<T, String> {
    let engine = engine.lock().map_err(|_| ENGINE_UNAVAILABLE.to_owned())?;
    Ok(read(&engine))
}

#[cfg(windows)]
pub mod shell;

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn read_engine_passes_the_engine_through() {
        let engine = Mutex::new(Engine::new());
        let name = read_engine(&engine, |e| e.snapshot().name).unwrap();
        assert_eq!(name, "Untitled");
    }

    #[test]
    fn read_engine_reports_unavailable_after_a_panic() {
        let engine = Mutex::new(Engine::new());
        let _ = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
            let _guard = engine.lock().unwrap();
            panic!("poison the lock");
        }));
        let error = read_engine(&engine, |e| e.status()).unwrap_err();
        assert_eq!(error, ENGINE_UNAVAILABLE);
    }
}
