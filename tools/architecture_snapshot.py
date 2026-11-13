#!/usr/bin/env python3
"""Record architecture-health snapshots keyed by commit (issue #1215).

`build` measures one checkout and writes a snapshot document. `append` stores that document as
`snapshots/<commit>.json` in a metrics checkout. Snapshot files are created exclusively: a commit's
snapshot is never rewritten, and re-appending identical content is a no-op. Snapshots carry no
timestamp, so a rerun of the same commit produces the same bytes.
"""

import argparse
import importlib.util
import json
import os
import re
import sys
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parent
COMMIT = re.compile(r"[0-9a-f]{40}")
GOD_HEADER = "src/commands/CadCommands.hpp"
SIZE_BASELINE = Path(".github/file-size-baseline.json")
VIOLATION_BASELINE = Path("docs/architecture/dependency-baseline.json")


def _load(name):
    spec = importlib.util.spec_from_file_location(name, SCRIPTS / f"{name}.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


INVENTORY = _load("architecture_inventory")
DEPENDENCIES = _load("architecture_dependencies")


class SnapshotConflict(Exception):
    """Raised when a commit already has a different snapshot on record."""


def _check_commit(commit):
    if not COMMIT.fullmatch(commit):
        raise ValueError(f"commit must be a 40-character lowercase SHA: {commit!r}")


def _read_json(path, default):
    return json.loads(path.read_text(encoding="utf-8")) if path.is_file() else default


def _serialize(snapshot):
    return json.dumps(snapshot, indent=2, sort_keys=True) + "\n"


def largest_source_file(root):
    """Return the source file with the most lines; ties go to the lowest path."""
    rows = []
    for path in (root / "src").rglob("*"):
        if path.is_file() and path.suffix.lower() in INVENTORY.SOURCE_SUFFIXES:
            lines = len(path.read_text(encoding="utf-8", errors="replace").splitlines())
            rows.append({"path": path.relative_to(root).as_posix(), "lines": lines})
    if not rows:
        return None
    return min(rows, key=lambda row: (-row["lines"], row["path"]))


def build_snapshot(root, commit):
    _check_commit(commit)
    root = Path(root).resolve()
    inventory = INVENTORY.collect_metrics(root)
    violations = DEPENDENCIES.check(root)["violations"]
    violation_baseline = _read_json(root / VIOLATION_BASELINE, {})
    return {
        "commit": commit,
        "metrics": {
            "largest_file": largest_source_file(root),
            "violation_count": len(violations),
            "god_header_fan_in": {"path": GOD_HEADER, "fan_in": inventory["cad_commands_header_fan_in"]},
            "dependency_cycle_count": len(inventory["dependency_cycles"]),
            # No domain test target exists yet, so the share is defined as zero until systems own tests.
            "domain_tests_without_qt": {"domain_test_count": 0, "built_without_qt_count": 0, "share": 0.0},
        },
        "baselines": {
            "violation_count": len(violation_baseline.get("violations", [])),
            "file_size": _read_json(root / SIZE_BASELINE, {}),
        },
    }


def append_snapshot(history_dir, snapshot):
    commit = snapshot["commit"]
    _check_commit(commit)
    directory = Path(history_dir) / "snapshots"
    directory.mkdir(parents=True, exist_ok=True)
    target = directory / f"{commit}.json"
    text = _serialize(snapshot)
    # Write a complete temp file, then hard-link it into place. The link fails if the target exists,
    # so a crash never leaves a truncated snapshot behind.
    temporary = directory / f".{commit}.{os.getpid()}.tmp"
    temporary.write_text(text, encoding="utf-8", newline="\n")
    try:
        os.link(temporary, target)
        return "created"
    except FileExistsError:
        if target.read_text(encoding="utf-8") == text:
            return "unchanged"
        raise SnapshotConflict(f"{target} already holds a different snapshot; snapshots are append-only")
    finally:
        temporary.unlink(missing_ok=True)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    build = commands.add_parser("build", help="measure a checkout into a snapshot file")
    build.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    build.add_argument("--commit", required=True, help="full 40-character commit SHA being measured")
    build.add_argument("--output", type=Path, required=True)
    append = commands.add_parser("append", help="append a snapshot file to a metrics checkout")
    append.add_argument("--snapshot", type=Path, required=True)
    append.add_argument("--history-dir", type=Path, required=True)
    args = parser.parse_args(argv)

    if args.command == "build":
        snapshot = build_snapshot(args.root, args.commit)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(_serialize(snapshot), encoding="utf-8", newline="\n")
        print(f"Built snapshot for {snapshot['commit']}: {args.output}")
        return 0

    snapshot = json.loads(args.snapshot.read_text(encoding="utf-8"))
    try:
        result = append_snapshot(args.history_dir, snapshot)
    except SnapshotConflict as error:
        print(f"CONFLICT: {error}", file=sys.stderr)
        return 1
    print(f"{result}: {snapshot['commit']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
