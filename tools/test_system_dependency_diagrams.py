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

    def changed(self, source, target, add):
        allowed = {system: set(targets) for system, targets in self.allowed.items()}
        (allowed[source].add if add else allowed[source].discard)(target)
        return allowed

    def test_forbidden_example_that_the_map_allows_is_rejected(self):
        example = diagrams.Forbidden("survey", "document", "wrong", (), ())
        with self.assertRaisesRegex(diagrams.DiagramError, "forbidden example"):
            diagrams.validate(self.allowed, forbidden=[example])

    def test_forbidden_reason_contradicted_by_the_map_is_rejected(self):
        # "Interaction also may not use Network or Pdf" must fail once the table allows Network.
        with self.assertRaisesRegex(diagrams.DiagramError, "allows interaction -> network"):
            diagrams.validate(self.changed("interaction", "network", add=True))
        # "one-way: Interaction may use Commands" must fail once the table drops that edge.
        with self.assertRaisesRegex(diagrams.DiagramError, "has no interaction -> commands"):
            diagrams.validate(self.changed("interaction", "commands", add=False), feature_edges=[])
        # "engine code never includes a backend" covers every engine system, not just the example.
        with self.assertRaisesRegex(diagrams.DiagramError, "allows math -> pqt"):
            diagrams.validate(self.changed("math", "pqt", add=True))

    def test_core_claim_contradicted_by_the_map_is_rejected(self):
        with self.assertRaisesRegex(diagrams.DiagramError, "omits renderer -> core"):
            diagrams.validate(self.changed("renderer", "core", add=False), feature_edges=[])
        with self.assertRaisesRegex(diagrams.DiagramError, "allows editor -> core"):
            diagrams.validate(self.changed("editor", "core", add=True))

    def test_gap_is_dropped_when_the_map_no_longer_has_it(self):
        self.assertIn("Commands may not include Geometry", diagrams.render_markdown(self.allowed))
        fixed = self.changed("commands", "geometry", add=True)
        self.assertNotIn("Commands may not include Geometry", diagrams.render_markdown(fixed))

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
