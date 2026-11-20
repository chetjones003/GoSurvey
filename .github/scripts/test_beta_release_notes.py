import argparse
import json
import tempfile
import unittest
import urllib.error
from pathlib import Path
from unittest.mock import patch

import beta_release_notes as notes


class ValidateModelChangesTests(unittest.TestCase):
    def setUp(self):
        self.items = [{"title": "Add point CSV export", "description": "Survey points can be exported."}]

    def test_accepts_one_plain_text_result_for_each_source_index(self):
        result = notes.validate_model_changes(self.items, {"changes": [
            {"source_index": 0, "user_facing": True,
             "release_note": "Export survey points to a CSV file.",
             "whats_new": "Save survey points as CSV."}
        ]})
        self.assertEqual(result[0]["source_index"], 0)

    def test_rejects_missing_duplicate_and_unknown_source_indices(self):
        record = {"source_index": 0, "user_facing": False, "release_note": "", "whats_new": ""}
        with self.assertRaisesRegex(ValueError, "exactly one"):
            notes.validate_model_changes(self.items, {"changes": []})
        with self.assertRaisesRegex(ValueError, "duplicated"):
            notes.validate_model_changes(self.items * 2, {"changes": [record, record]})
        unknown = {**record, "source_index": 1}
        with self.assertRaisesRegex(ValueError, "unknown"):
            notes.validate_model_changes(self.items, {"changes": [unknown]})

    def test_rejects_markup_links_and_newlines(self):
        for value in ("<img src=x>", "See https://example.com", "first\nsecond", "# Heading"):
            record = {"source_index": 0, "user_facing": True,
                      "release_note": value, "whats_new": "Short summary."}
            with self.subTest(value=value), self.assertRaises(ValueError):
                notes.validate_model_changes(self.items, {"changes": [record]})

    def test_non_user_facing_record_must_not_include_notes(self):
        record = {"source_index": 0, "user_facing": False,
                  "release_note": "A note", "whats_new": ""}
        with self.assertRaisesRegex(ValueError, "must have empty"):
            notes.validate_model_changes(self.items, {"changes": [record]})


class RequestOpenAITests(unittest.TestCase):
    @patch("beta_release_notes.urllib.request.urlopen")
    def test_uses_luna_responses_strict_schema_and_only_title_description(self, urlopen):
        items = [{"title": "Fix drawing save", "description": "Saving keeps the layer color."}]
        output = {"changes": [{"source_index": 0, "user_facing": True,
                               "release_note": "Layer colors now persist when you save.",
                               "whats_new": "Layer colors persist on save."}]}

        class FakeResponse:
            def __enter__(self):
                return self

            def __exit__(self, *_args):
                return False

            def read(self):
                return json.dumps({"status": "completed", "output": [{"type": "message", "content": [
                    {"type": "output_text", "text": json.dumps(output)}
                ]}]}).encode()

        urlopen.return_value = FakeResponse()
        result = notes.request_openai(items, "test-secret")
        self.assertEqual(len(result), 1)
        request = urlopen.call_args.args[0]
        payload = json.loads(request.data.decode())
        self.assertEqual(request.full_url, "https://api.openai.com/v1/responses")
        self.assertEqual(payload["model"], "gpt-6-luna")
        self.assertEqual(payload["reasoning"], {"effort": "low"})
        self.assertFalse(payload["store"])
        self.assertEqual(payload["text"]["format"]["type"], "json_schema")
        self.assertTrue(payload["text"]["format"]["strict"])
        model_input = json.loads(payload["input"])
        self.assertEqual(model_input, [{"source_index": 0, **items[0]}])
        self.assertNotIn("number", model_input[0])
        self.assertNotIn("pull_request", model_input[0])
        self.assertEqual(request.headers["Authorization"], "Bearer test-secret")

    @patch("beta_release_notes.urllib.request.urlopen")
    def test_api_authentication_failure_stops_generation_without_logging_body(self, urlopen):
        urlopen.side_effect = urllib.error.HTTPError(
            "https://api.openai.com/v1/responses", 401, "Unauthorized", {}, None
        )
        with self.assertRaisesRegex(RuntimeError, "HTTP 401"):
            notes.request_openai([{"title": "A", "description": "B"}], "test-secret")


class FormattingTests(unittest.TestCase):
    def test_inserts_beta_summary_and_preserves_authored_markdown(self):
        authored = "# GoSurvey 0.7.8\n\nIntro.\n\n## Key Features\n\n- Keep me\n\n![Map](whats-new:maps.png)\n"
        items = [{"title": "Add export", "description": "Export points."}]
        changes = [{"source_index": 0, "user_facing": True,
                    "release_note": "Export survey points as CSV.",
                    "whats_new": "Save survey points as CSV."}]
        release_text, full_markdown = notes.format_outputs("0.7.9", authored, changes)
        self.assertIn("## 0.7.9", release_text)
        self.assertIn("## New in beta 0.7.9", full_markdown)
        self.assertIn("## Key Features\n\n- Keep me", full_markdown)
        self.assertIn("![Map](whats-new:maps.png)", full_markdown)
        self.assertEqual(full_markdown.count("## New in beta"), 1)

    def test_empty_pr_range_has_deterministic_content(self):
        release_text, full_markdown = notes.format_outputs("0.7.9", "# GoSurvey\n\n## Key Features\n", [])
        self.assertIn("No user-facing changes.", release_text)
        self.assertIn("No user-facing changes in this beta.", full_markdown)

    def test_only_non_user_facing_prs_have_deterministic_content(self):
        release_text, full_markdown = notes.format_outputs(
            "0.7.9", "# GoSurvey\n\n## Key Features\n",
            [{"source_index": 0, "user_facing": False, "release_note": "", "whats_new": ""}],
        )
        self.assertIn("No user-facing changes.", release_text)
        self.assertIn("No user-facing changes in this beta.", full_markdown)

    def test_missing_insertion_point_fails_closed(self):
        with self.assertRaisesRegex(ValueError, "insertion point"):
            notes.format_outputs("0.7.9", "# GoSurvey\n", [])


class PullRequestSelectionTests(unittest.TestCase):
    @patch("beta_release_notes.github_get")
    @patch("beta_release_notes._git")
    def test_selects_only_merged_beta_prs_represented_after_baseline(self, git, github_get):
        commit_a, commit_b, old_commit = "a" * 40, "b" * 40, "c" * 40
        git.side_effect = [
            "\n".join([commit_a, commit_b, old_commit]),  # source ancestors
            old_commit,  # baseline ancestors
            "\n".join([old_commit, commit_a, commit_b]),  # first-parent ordering
        ]
        github_get.return_value = [
            {"merged_at": "now", "merge_commit_sha": commit_b, "number": 22,
             "title": "Second", "body": "Description 2"},
            {"merged_at": "now", "merge_commit_sha": old_commit, "number": 10,
             "title": "Already stable", "body": "Old body"},
            {"merged_at": None, "merge_commit_sha": commit_a, "number": 11,
             "title": "Open PR", "body": "Open body"},
            {"merged_at": "now", "merge_commit_sha": commit_a, "number": 21,
             "title": "First", "body": "Description 1"},
        ]
        selected = notes.collect_merged_beta_prs("owner/repo", "source", "baseline", "token")
        self.assertEqual(selected, [
            {"title": "First", "description": "Description 1"},
            {"title": "Second", "description": "Description 2"},
        ])
        github_get.assert_called_once()


class SameCommitReuseTests(unittest.TestCase):
    def test_reuses_manifest_outputs_without_openai_call(self):
        authored = "# GoSurvey\n\n## Key Features\n"
        saved_markdown = "# GoSurvey\n\n## New in beta 0.7.9\n\nSaved.\n\n## Key Features\n"
        saved_notes = "## 0.7.9\n\nSaved release note.\n"
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            whats_new = root / "whats-new.md"
            whats_new.write_text(authored, encoding="utf-8")
            args = argparse.Namespace(
                output_dir=str(root / "out"), whats_new=str(whats_new), repository="owner/repo",
                source_commit="a" * 40, baseline_commit="b" * 40, version="0.7.9",
                github_token="", openai_key="", smoke_test=False,
            )
            previous = {"sourceCommit": args.source_commit, "version": args.version, "notes": saved_notes,
                        "whatsNewMarkdown": saved_markdown, "notesModel": "gpt-6-luna",
                        "notesBaselineCommit": args.baseline_commit,
                        "notesSourcePullRequestCount": 3}
            with patch("beta_release_notes.get_existing_manifest", return_value=previous), \
                 patch("beta_release_notes.request_openai", side_effect=AssertionError("must not call AI")):
                notes.run(args)
            output = json.loads((root / "out" / "beta-release-content.json").read_text(encoding="utf-8"))
            self.assertTrue(output["reused"])
            self.assertEqual(output["notes"], saved_notes)
            self.assertEqual(whats_new.read_text(encoding="utf-8"), saved_markdown)


if __name__ == "__main__":
    unittest.main()
