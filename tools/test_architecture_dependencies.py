import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).with_name("architecture_dependencies.py")
SPEC = importlib.util.spec_from_file_location("architecture_dependencies", SCRIPT)
checker = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(checker)


class ArchitectureDependencyTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        (self.root / "src/a").mkdir(parents=True)
        (self.root / "src/b").mkdir(parents=True)
        (self.root / "docs/architecture").mkdir(parents=True)
        (self.root / "src/a/A.cpp").write_text('#include "b/Private.hpp"\n', encoding="utf-8")
        (self.root / "src/b/Private.hpp").write_text("#pragma once\n", encoding="utf-8")
        (self.root / "docs/architecture/system-map.json").write_text(json.dumps({
            "mappedFolders": [
                {"path": "src/a/", "system": "A"},
                {"path": "src/b/", "system": "B"},
            ],
            "fileMappings": [],
        }), encoding="utf-8")
        (self.root / "docs/architecture/system-map.md").write_text(
            "| System | Tier | Responsibility | Does not own | Allowed dependencies |\n"
            "|---|---|---|---|---|\n"
            "| A | T2 Domain | A system | Nothing | None |\n"
            "| B | T1 Kernel | B system | Nothing | None |\n", encoding="utf-8")

    def tearDown(self):
        self.temporary.cleanup()

    def test_forbidden_tier_include_fails_and_names_rule(self):
        result = checker.check(self.root)
        self.assertEqual(result["violations"][0]["rule"], "forbidden dependency")
        self.assertIn('src/a/A.cpp includes "b/Private.hpp"', result["violations"][0]["message"])

    def test_include_resolution_uses_newly_mapped_folders(self):
        (self.root / "src/c").mkdir()
        (self.root / "src/c/Header.hpp").write_text("#pragma once\n", encoding="utf-8")
        mapping_path = self.root / "docs/architecture/system-map.json"
        mapping = json.loads(mapping_path.read_text(encoding="utf-8"))
        mapping["mappedFolders"].append({"path": "src/c/", "system": "C"})
        mapping_path.write_text(json.dumps(mapping), encoding="utf-8")
        (self.root / "docs/architecture/system-map.md").write_text(
            "| System | Tier | Responsibility | Does not own | Allowed dependencies |\n"
            "|---|---|---|---|---|\n"
            "| A | T2 Domain | A system | Nothing | None |\n"
            "| B | T1 Kernel | B system | Nothing | None |\n"
            "| C | T1 Kernel | C system | Nothing | None |\n", encoding="utf-8")
        (self.root / "src/a/A.cpp").write_text('#include "Header.hpp"\n', encoding="utf-8")
        result = checker.check(self.root)
        self.assertEqual(result["violations"][0]["rule"], "forbidden dependency")
        self.assertEqual(result["violations"][0]["include"], "Header.hpp")

    def test_allowed_dependency_through_private_header_fails(self):
        (self.root / "docs/architecture/system-map.md").write_text(
            "| System | Tier | Responsibility | Does not own | Allowed dependencies |\n"
            "|---|---|---|---|---|\n"
            "| A | T2 Domain | A system | Nothing | B |\n"
            "| B | T1 Kernel | B system | Nothing | None |\n", encoding="utf-8")
        result = checker.check(self.root)
        self.assertEqual(result["violations"][0]["rule"], "private-header include")

    def test_allowed_system_qualified_public_include_passes(self):
        (self.root / "docs/architecture/system-map.md").write_text(
            "| System | Tier | Responsibility | Does not own | Allowed dependencies |\n"
            "|---|---|---|---|---|\n"
            "| A | T2 Domain | A system | Nothing | B |\n"
            "| B | T1 Kernel | B system | Nothing | None |\n", encoding="utf-8")
        (self.root / "src/a/A.cpp").write_text('#include "GoSurvey/B/Api.hpp"\n', encoding="utf-8")
        result = checker.check(self.root)
        self.assertEqual(result["violations"], [])
        self.assertEqual(result["includes"][0]["classification"], "allowed")

    def test_platform_and_application_systems_are_read_from_the_map(self):
        (self.root / "docs/architecture/system-map.md").write_text(
            "| System | Tier | Responsibility | Does not own | Allowed dependencies |\n"
            "|---|---|---|---|---|\n"
            "| A | T2 Domain | A system | Nothing | Qt |\n"
            "| Qt | Platform backend | Qt system | Nothing | None |\n", encoding="utf-8")
        (self.root / "src/a/A.cpp").write_text('#include "GoSurvey/Qt/Api.hpp"\n', encoding="utf-8")
        result = checker.check(self.root)
        self.assertEqual(result["violations"], [])
        self.assertEqual(result["includes"][0]["target_system"], "Qt")

    def test_includes_in_comments_and_raw_strings_are_ignored(self):
        (self.root / "src/a/A.cpp").write_text(
            '/* #include "b/Private.hpp" */\n'
            'const char* raw = R"tag(\n#include "b/Private.hpp"\n)tag";\n', encoding="utf-8")
        result = checker.check(self.root)
        self.assertEqual(result["includes"], [])

    def test_baseline_must_not_grow_or_shrink_without_update(self):
        baseline = checker.check(self.root)['violations']
        self.assertTrue(checker.compare_baseline(baseline, baseline)["ok"])
        self.assertFalse(checker.compare_baseline([], baseline)["ok"])
        self.assertFalse(checker.compare_baseline(baseline + baseline, baseline)["ok"])

    def test_candidate_cannot_add_a_violation_and_its_baseline_together(self):
        reference = [{"file": "src/a/A.cpp", "include": "b/Private.hpp",
                      "rule": "forbidden dependency", "message": "fixture violation"}]
        candidate_baseline = reference + [{"file": "src/a/A.cpp", "include": "b/Other.hpp",
                                          "rule": "forbidden dependency", "message": "new fixture violation"}]
        self.assertTrue(checker.compare_baseline(candidate_baseline, candidate_baseline)["ok"])
        growth = checker.compare_baseline(candidate_baseline, reference)
        self.assertEqual(growth["new_violations"], [["src/a/A.cpp", "b/Other.hpp", "forbidden dependency"]])

    def test_unmapped_source_files_are_reported(self):
        (self.root / "src/unknown").mkdir()
        (self.root / "src/unknown/Thing.cpp").write_text("\n", encoding="utf-8")
        result = checker.check(self.root)
        self.assertEqual(result["unmapped_files"], ["src/unknown/Thing.cpp"])

    def test_unresolved_quoted_include_is_reported_as_unmapped(self):
        (self.root / "src/a/A.cpp").write_text('#include "MissingLocalHeader.hpp"\n', encoding="utf-8")
        result = checker.check(self.root)
        self.assertEqual(result["includes"][0]["classification"], "unmapped")

    def test_unresolved_include_path_does_not_match_another_file_by_basename(self):
        (self.root / "src/a/A.cpp").write_text('#include "missing/Private.hpp"\n', encoding="utf-8")
        result = checker.check(self.root)
        self.assertEqual(result["includes"][0]["classification"], "unmapped")

    def test_macro_include_is_reported_as_unmapped(self):
        (self.root / "src/a/A.cpp").write_text("#include PROJECT_HEADER\n", encoding="utf-8")
        result = checker.check(self.root)
        self.assertEqual(result["includes"][0]["classification"], "unmapped")


if __name__ == "__main__":
    unittest.main()
