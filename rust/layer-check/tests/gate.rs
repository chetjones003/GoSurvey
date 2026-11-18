use std::path::Path;

use gs_layer_check::{load_workspace, parse_metadata, violations, Crate};

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
fn real_workspace_has_one_crate_per_layer_and_no_upward_dependencies() {
    let manifest = Path::new(env!("CARGO_MANIFEST_DIR")).join("../Cargo.toml");

    let crates = load_workspace(&manifest).expect("cargo metadata succeeds");

    for layer in [
        "gs-foundation",
        "gs-kernel",
        "gs-domain",
        "gs-services",
        "gs-application",
        "gs-editor",
    ] {
        assert_eq!(
            crates.iter().filter(|c| c.name == layer).count(),
            1,
            "expected exactly one crate named {layer}"
        );
    }
    assert_eq!(violations(&crates), Vec::<String>::new());
}
