import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).with_name("architecture_snapshot.py")
SPEC = importlib.util.spec_from_file_location("architecture_snapshot", SCRIPT)
snapshot_tool = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(snapshot_tool)

COMMIT = "a" * 40
OTHER_COMMIT = "b" * 40


class ArchitectureSnapshotTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name) / "checkout"
        (self.root / "src/a").mkdir(parents=True)
        (self.root / "src/b").mkdir(parents=True)
        (self.root / "docs/architecture").mkdir(parents=True)
        (self.root / ".github").mkdir(parents=True)
        # A includes B's private header: one forbidden-dependency violation.
        (self.root / "src/a/A.cpp").write_text('#include "b/Private.hpp"\n', encoding="utf-8")
        (self.root / "src/b/Private.hpp").write_text("#pragma once\n", encoding="utf-8")
        (self.root / "docs/architecture/system-map.json").write_text(json.dumps({
            "mappedFolders": [{"path": "src/a/", "system": "A"}, {"path": "src/b/", "system": "B"}],
            "fileMappings": [],
        }), encoding="utf-8")
        (self.root / "docs/architecture/system-map.md").write_text(
            "| System | Tier | Responsibility | Does not own | Allowed dependencies |\n"
            "|---|---|---|---|---|\n"
            "| A | T2 Domain | A system | Nothing | None |\n"
            "| B | T1 Kernel | B system | Nothing | None |\n", encoding="utf-8")
        (self.root / "docs/architecture/dependency-baseline.json").write_text(json.dumps({
            "generatedFrom": "beta",
            "violations": [{"file": "src/a/A.cpp", "include": "b/Private.hpp", "rule": "forbidden dependency",
                            "message": "old"}],
        }), encoding="utf-8")
        (self.root / ".github/file-size-baseline.json").write_text(
            json.dumps({"src/a/A.cpp": 4000}), encoding="utf-8")
        self.history = Path(self.temporary.name) / "metrics"

    def tearDown(self):
        self.temporary.cleanup()

    def test_build_reports_violations_largest_file_and_baselines(self):
        (self.root / "src/a/Big.cpp").write_text("line\n" * 5, encoding="utf-8")
        snap = snapshot_tool.build_snapshot(self.root, COMMIT)
        self.assertEqual(snap["commit"], COMMIT)
        self.assertEqual(snap["metrics"]["violation_count"], 1)
        self.assertEqual(snap["metrics"]["largest_file"], {"path": "src/a/Big.cpp", "lines": 5})
        self.assertEqual(snap["metrics"]["god_header_fan_in"], {"path": "src/commands/CadCommands.hpp", "fan_in": 0})
        self.assertEqual(snap["metrics"]["domain_tests_without_qt"]["share"], 0.0)
        self.assertEqual(snap["baselines"]["violation_count"], 1)
        self.assertEqual(snap["baselines"]["violations"][0]["include"], "b/Private.hpp")
        self.assertEqual(snap["baselines"]["file_size"], {"src/a/A.cpp": 4000})

    def test_largest_file_tie_breaks_by_path(self):
        (self.root / "src/a/A.cpp").write_text("x\n" * 3, encoding="utf-8")
        (self.root / "src/b/Zed.cpp").write_text("x\n" * 3, encoding="utf-8")
        (self.root / "src/a/Alpha.cpp").write_text("x\n" * 3, encoding="utf-8")
        largest = snapshot_tool.build_snapshot(self.root, COMMIT)["metrics"]["largest_file"]
        self.assertEqual(largest, {"path": "src/a/A.cpp", "lines": 3})

    def test_missing_baselines_are_empty_not_errors(self):
        (self.root / ".github/file-size-baseline.json").unlink()
        (self.root / "docs/architecture/dependency-baseline.json").unlink()
        snap = snapshot_tool.build_snapshot(self.root, COMMIT)
        self.assertEqual(snap["baselines"], {"violation_count": 0, "violations": [], "file_size": {}})

    def test_rejects_refs_and_paths_that_are_not_full_shas(self):
        for bad in ("HEAD", "beta", "abc123", "../../escape", "A" * 40):
            with self.subTest(commit=bad), self.assertRaises(ValueError):
                snapshot_tool.build_snapshot(self.root, bad)

    def test_append_creates_then_treats_identical_rerun_as_unchanged(self):
        snap = snapshot_tool.build_snapshot(self.root, COMMIT)
        self.assertEqual(snapshot_tool.append_snapshot(self.history, snap), "created")
        self.assertEqual(snapshot_tool.append_snapshot(self.history, snap), "unchanged")
        stored = json.loads((self.history / "snapshots" / f"{COMMIT}.json").read_text(encoding="utf-8"))
        self.assertEqual(stored, snap)

    def test_append_never_rewrites_a_different_snapshot_for_the_same_commit(self):
        snap = snapshot_tool.build_snapshot(self.root, COMMIT)
        snapshot_tool.append_snapshot(self.history, snap)
        target = self.history / "snapshots" / f"{COMMIT}.json"
        before = target.read_bytes()
        changed = dict(snap, metrics=dict(snap["metrics"], violation_count=99))
        with self.assertRaises(snapshot_tool.SnapshotConflict):
            snapshot_tool.append_snapshot(self.history, changed)
        self.assertEqual(target.read_bytes(), before)

    def test_append_leaves_no_temporary_file_behind(self):
        snap = snapshot_tool.build_snapshot(self.root, COMMIT)
        snapshot_tool.append_snapshot(self.history, snap)
        snapshot_tool.append_snapshot(self.history, snap)
        names = [path.name for path in (self.history / "snapshots").iterdir()]
        self.assertEqual(names, [f"{COMMIT}.json"])

    def test_append_keeps_one_file_per_commit(self):
        snapshot_tool.append_snapshot(self.history, snapshot_tool.build_snapshot(self.root, COMMIT))
        snapshot_tool.append_snapshot(self.history, snapshot_tool.build_snapshot(self.root, OTHER_COMMIT))
        names = sorted(path.name for path in (self.history / "snapshots").iterdir())
        self.assertEqual(names, [f"{COMMIT}.json", f"{OTHER_COMMIT}.json"])

    def test_cli_build_then_append_round_trip(self):
        output = Path(self.temporary.name) / "in" / f"{COMMIT}.json"
        self.assertEqual(snapshot_tool.main(["build", "--root", str(self.root), "--commit", COMMIT,
                                             "--output", str(output)]), 0)
        self.assertEqual(snapshot_tool.main(["append", "--snapshot", str(output),
                                             "--history-dir", str(self.history)]), 0)
        self.assertEqual(snapshot_tool.main(["append", "--snapshot", str(output),
                                             "--history-dir", str(self.history)]), 0)
        conflicting = json.loads(output.read_text(encoding="utf-8"))
        conflicting["metrics"]["violation_count"] = 42
        output.write_text(json.dumps(conflicting), encoding="utf-8")
        self.assertEqual(snapshot_tool.main(["append", "--snapshot", str(output),
                                             "--history-dir", str(self.history)]), 1)


if __name__ == "__main__":
    unittest.main()
