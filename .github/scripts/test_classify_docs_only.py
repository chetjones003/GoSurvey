"""Offline regression tests for the docs-only PR classifier."""

import importlib.util
import os
import unittest
from pathlib import Path
from unittest.mock import patch


SCRIPT = Path(__file__).with_name("classify_docs_only.py")
os.environ.setdefault("GH_TOKEN", "test-token")
SPEC = importlib.util.spec_from_file_location("classify_docs_only", SCRIPT)
classifier = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(classifier)


class ClassifyDocsOnlyTests(unittest.TestCase):
    def setUp(self):
        self.environment = patch.dict(
            os.environ,
            {
                "PR_LABELS": '["docs-only"]',
                "REPOSITORY": "owner/repo",
                "PR_NUMBER": "42",
            },
        )
        self.environment.start()
        self.addCleanup(self.environment.stop)

    def classify(self, changed_files, files):
        with patch.object(
            classifier,
            "github_get",
            side_effect=[{"changed_files": changed_files}, files],
        ):
            return classifier.classify_docs_only()

    def test_documentation_files_are_eligible(self):
        files = [
            {"filename": "AGENTS.md"},
            {"filename": "spec/requirements.md"},
        ]

        self.assertTrue(self.classify(2, files))

    def test_missing_label_runs_checks_without_listing_files(self):
        with patch.dict(os.environ, {"PR_LABELS": "[]"}):
            with patch.object(classifier, "github_get") as get:
                self.assertFalse(classifier.classify_docs_only())

        get.assert_not_called()

    def test_code_file_is_not_documentation(self):
        self.assertFalse(self.classify(1, [{"filename": "src/main.cpp"}]))

    def test_renamed_code_to_docs_is_not_documentation(self):
        files = [
            {
                "filename": "docs/source.md",
                "previous_filename": "src/source.cpp",
            }
        ]

        self.assertFalse(self.classify(1, files))

    def test_renamed_docs_to_code_is_not_documentation(self):
        files = [
            {
                "filename": "src/source.cpp",
                "previous_filename": "docs/source.md",
            }
        ]

        self.assertFalse(self.classify(1, files))

    def test_plain_text_is_allowed_only_in_documentation_roots(self):
        self.assertTrue(self.classify(1, [{"filename": "docs/notes.txt"}]))
        self.assertFalse(self.classify(1, [{"filename": "CMakeLists.txt"}]))

    def test_incomplete_file_listing_runs_checks(self):
        self.assertFalse(self.classify(2, [{"filename": "README.md"}]))

    def test_api_file_limit_runs_checks(self):
        with patch.object(classifier, "github_get", return_value={"changed_files": 3001}) as get:
            self.assertFalse(classifier.classify_docs_only())

        get.assert_called_once()

    def test_api_failure_propagates_for_fail_closed_workflow(self):
        with patch.object(classifier, "github_get", side_effect=OSError("API unavailable")):
            with self.assertRaisesRegex(OSError, "API unavailable"):
                classifier.classify_docs_only()


if __name__ == "__main__":
    unittest.main()
