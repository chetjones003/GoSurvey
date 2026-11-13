import importlib.util
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).with_name("system_dependency_diagrams.py")
SPEC = importlib.util.spec_from_file_location("system_dependency_diagrams", SCRIPT)
diagrams = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(diagrams)


class SystemDependencyDiagramTests(unittest.TestCase):
    def setUp(self):
        self.allowed = diagrams.read_allowed()

    def test_committed_doc_matches_generator(self):
        self.assertEqual(diagrams.main(["--check"]), 0)

    def test_every_allowed_edge_except_core_is_drawn(self):
        for system, targets in self.allowed.items():
            card = diagrams.system_card(system, self.allowed)
            for target in targets - {"core"}:
                self.assertRegex(card, rf"{system}__src -\.?-\.?> {system}__{target}\n")

    def test_feature_edge_off_the_system_map_is_rejected(self):
        with self.assertRaisesRegex(diagrams.DiagramError, "pointcloud.store -> document.ent"):
            diagrams.validate(self.allowed, feature_edges=[("pointcloud.store", "document.ent")])

    def test_unknown_feature_is_rejected(self):
        with self.assertRaisesRegex(diagrams.DiagramError, "unknown feature"):
            diagrams.validate(self.allowed, feature_edges=[("interaction.snap", "document.nope")])

    def test_forbidden_example_that_the_map_allows_is_rejected(self):
        with self.assertRaisesRegex(diagrams.DiagramError, "forbidden example"):
            diagrams.validate(self.allowed, forbidden=[("survey", "document", "wrong")])

    def test_system_missing_from_diagrams_is_reported(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "docs/architecture").mkdir(parents=True)
            (root / "docs/architecture/system-map.md").write_text(
                "| System | Tier | Responsibility | Does not own | Allowed dependencies |\n"
                "|---|---|---|---|---|\n"
                "| Telemetry | T3 Services | x | x | Core |\n", encoding="utf-8")
            with self.assertRaisesRegex(diagrams.DiagramError, "'Telemetry'"):
                diagrams.read_allowed(root)


if __name__ == "__main__":
    unittest.main()
