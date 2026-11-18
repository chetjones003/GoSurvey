use std::path::Path;

use gs_layer_check::{Crate, LAYERS, load_workspace, parse_metadata, violations};

fn krate(name: &str, deps: &[&str]) -> Crate {
    Crate {
        name: name.to_string(),
        deps: deps.iter().map(|d| d.to_string()).collect(),
    }
}

#[test]
fn downward_dependencies_have_no_violations() {
    let graph = [
        krate("gs-foundation", &[]),
        krate("gs-kernel", &["gs-foundation"]),
        krate("gs-domain", &["gs-kernel", "gs-foundation"]),
        krate("gs-editor", &["gs-domain"]),
    ];

    assert!(violations(&graph).is_empty());
}

#[test]
fn foundation_depending_on_domain_is_a_violation() {
    let graph = [
        krate("gs-foundation", &["gs-domain"]),
        krate("gs-domain", &[]),
    ];

    let found = violations(&graph);

    assert_eq!(found.len(), 1, "expected one violation, got {found:?}");
    assert!(found[0].contains("gs-foundation"));
    assert!(found[0].contains("gs-domain"));
}

#[test]
fn crates_outside_the_layer_table_are_not_checked_for_direction() {
    let graph = [
        krate("gs-layer-check", &["gs-editor"]),
        krate("gs-editor", &[]),
    ];

    assert!(violations(&graph).is_empty());
}

#[test]
fn parse_metadata_reads_package_names_and_declared_dependencies() {
    let json = r#"{
        "packages": [
            {"name": "gs-kernel", "dependencies": [{"name": "gs-foundation", "kind": null}]},
            {"name": "gs-foundation", "dependencies": []}
        ]
    }"#;

    let crates = parse_metadata(json).expect("valid metadata");

    assert_eq!(
        crates,
        vec![
            krate("gs-kernel", &["gs-foundation"]),
            krate("gs-foundation", &[]),
        ]
    );
}

#[test]
fn parse_metadata_rejects_a_package_without_a_name() {
    let json = r#"{"packages": [{"dependencies": []}]}"#;

    assert!(parse_metadata(json).is_err());
}

#[test]
fn real_workspace_is_exactly_the_layer_crates_plus_the_gate_and_has_no_upward_dependencies() {
    let manifest = Path::new(env!("CARGO_MANIFEST_DIR")).join("../Cargo.toml");

    let crates = load_workspace(&manifest).expect("cargo metadata succeeds");

    let mut names: Vec<&str> = crates.iter().map(|c| c.name.as_str()).collect();
    names.sort_unstable();
    let mut expected: Vec<&str> = LAYERS.iter().map(|(name, _)| *name).collect();
    expected.push("gs-layer-check");
    expected.sort_unstable();
    assert_eq!(
        names, expected,
        "every workspace crate must be a layer or the gate"
    );
    assert_eq!(violations(&crates), Vec::<String>::new());
}
