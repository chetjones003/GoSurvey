//! Layer gate for the GoSurvey Rust workspace.

use std::path::Path;

/// Layer crates, bottom (rank 0) to top.
pub const LAYERS: &[(&str, u8)] = &[
    ("gs-foundation", 0),
    ("gs-kernel", 1),
    ("gs-domain", 2),
    ("gs-services", 3),
    ("gs-application", 4),
    ("gs-editor", 5),
];

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Crate {
    pub name: String,
    pub deps: Vec<String>,
}

fn rank_of(name: &str) -> Option<u8> {
    LAYERS
        .iter()
        .find(|(layer, _)| *layer == name)
        .map(|(_, rank)| *rank)
}

/// Every dependency between two layer crates must point to a strictly lower rank.
/// Crates outside the layer table are not checked for direction.
pub fn violations(crates: &[Crate]) -> Vec<String> {
    let mut found = Vec::new();
    for krate in crates {
        let Some(rank) = rank_of(&krate.name) else {
            continue;
        };
        for dep in &krate.deps {
            let Some(dep_rank) = rank_of(dep) else {
                continue;
            };
            if dep_rank >= rank {
                found.push(format!(
                    "{} (layer {rank}) depends on {dep} (layer {dep_rank}); dependencies must point downward",
                    krate.name
                ));
            }
        }
    }
    found
}

pub fn parse_metadata(json: &str) -> Result<Vec<Crate>, String> {
    let value: serde_json::Value = serde_json::from_str(json).map_err(|e| e.to_string())?;
    let packages = value["packages"]
        .as_array()
        .ok_or_else(|| "cargo metadata has no packages array".to_string())?;
    packages
        .iter()
        .map(|package| {
            let name = package["name"]
                .as_str()
                .ok_or_else(|| "cargo metadata package has no name".to_string())?;
            let deps = package["dependencies"]
                .as_array()
                .map(|deps| {
                    deps.iter()
                        .filter_map(|d| d["name"].as_str().map(str::to_string))
                        .collect()
                })
                .unwrap_or_default();
            Ok(Crate {
                name: name.to_string(),
                deps,
            })
        })
        .collect()
}

pub fn load_workspace(manifest: &Path) -> Result<Vec<Crate>, String> {
    let output = std::process::Command::new("cargo")
        .args([
            "metadata",
            "--no-deps",
            "--format-version",
            "1",
            "--manifest-path",
        ])
        .arg(manifest)
        .output()
        .map_err(|e| e.to_string())?;
    if !output.status.success() {
        return Err(String::from_utf8_lossy(&output.stderr).into_owned());
    }
    parse_metadata(&String::from_utf8_lossy(&output.stdout))
}
