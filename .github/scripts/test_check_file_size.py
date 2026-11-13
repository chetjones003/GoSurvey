"""Tests for the CI file-size gate (issue #1213)."""

import json
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import check_file_size as gate


class CountLinesTests(unittest.TestCase):
    def test_counts_newline_terminated_lines(self):
        self.assertEqual(gate.count_lines("a\nb\n"), 2)

    def test_counts_unterminated_last_line(self):
        self.assertEqual(gate.count_lines("a\nb"), 2)

    def test_empty_text_has_no_lines(self):
        self.assertEqual(gate.count_lines(""), 0)

    def test_form_feed_is_not_a_line_break(self):
        self.assertEqual(gate.count_lines("a\x0cb\n"), 1)

    def test_crlf_counts_one_line_per_break(self):
        self.assertEqual(gate.count_lines("a\r\nb\r\n"), 2)

    def test_cr_only_line_endings_count_each_break(self):
        self.assertEqual(gate.count_lines("a\rb\rc"), 3)


class FindViolationsTests(unittest.TestCase):
    def violations(self, current, baseline, base_baseline):
        return gate.find_violations(current, baseline, base_baseline)

    def test_under_limit_file_not_grandfathered_passes(self):
        self.assertEqual(self.violations({"src/a.cpp": 2999}, {}, {}), [])

    def test_exactly_at_limit_not_grandfathered_passes(self):
        self.assertEqual(self.violations({"src/a.cpp": 3000}, {}, {}), [])

    def test_new_file_one_line_over_limit_fails(self):
        result = self.violations({"src/a.cpp": 3001}, {}, {})
        self.assertEqual(len(result), 1)
        self.assertIn("src/a.cpp", result[0])

    def test_grandfathered_file_unchanged_passes(self):
        current = {"src/big.cpp": 3500}
        self.assertEqual(self.violations(current, {"src/big.cpp": 3500}, {"src/big.cpp": 3500}), [])

    def test_grandfathered_file_that_grows_fails(self):
        current = {"src/big.cpp": 3501}
        result = self.violations(current, {"src/big.cpp": 3500}, {"src/big.cpp": 3500})
        self.assertEqual(len(result), 1)
        self.assertIn("src/big.cpp", result[0])

    def test_grandfathered_file_that_shrinks_requires_lower_baseline(self):
        current = {"src/big.cpp": 3400}
        result = self.violations(current, {"src/big.cpp": 3500}, {"src/big.cpp": 3500})
        self.assertEqual(len(result), 1)
        self.assertIn("3400", result[0])

    def test_shrunk_baseline_matching_current_passes(self):
        current = {"src/big.cpp": 3400}
        self.assertEqual(self.violations(current, {"src/big.cpp": 3400}, {"src/big.cpp": 3500}), [])

    def test_grandfathered_file_shrunk_under_limit_requires_entry_removal(self):
        current = {"src/big.cpp": 2900}
        result = self.violations(current, {"src/big.cpp": 3500}, {"src/big.cpp": 3500})
        self.assertEqual(len(result), 1)
        self.assertIn("src/big.cpp", result[0])

    def test_baseline_entry_for_deleted_file_fails(self):
        result = self.violations({}, {"src/gone.cpp": 3500}, {"src/gone.cpp": 3500})
        self.assertEqual(len(result), 1)
        self.assertIn("src/gone.cpp", result[0])

    def test_baseline_entry_added_by_pr_fails(self):
        current = {"src/big.cpp": 3500}
        result = self.violations(current, {"src/big.cpp": 3500}, {})
        self.assertEqual(len(result), 1)
        self.assertIn("src/big.cpp", result[0])

    def test_baseline_value_raised_by_pr_fails(self):
        current = {"src/big.cpp": 3600}
        result = self.violations(current, {"src/big.cpp": 3600}, {"src/big.cpp": 3500})
        self.assertEqual(len(result), 1)
        self.assertIn("raised", result[0])


def _write(root, relative, lines):
    path = root / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("".join("x\n" for _ in range(lines)), encoding="utf-8")


def _git(root, *args):
    subprocess.run(["git", "-C", str(root), *args], check=True, capture_output=True)


class BaseReadTests(unittest.TestCase):
    def test_ls_tree_error_is_not_treated_as_absent(self):
        failed = subprocess.CompletedProcess(args=[], returncode=128, stdout=b"", stderr=b"fatal")
        with mock.patch.object(gate.subprocess, "run", return_value=failed):
            with self.assertRaises(SystemExit):
                gate.read_base_text(Path("."), "abc123", gate.BASELINE_PATH)


class FixtureTreeTests(unittest.TestCase):
    """Run the CLI against real temporary git repositories: base commit, then a PR commit."""

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = Path(self._tmp.name)
        _git(self.root, "init", "-q")
        _write(self.root, "src/big.cpp", 3500)
        _write(self.root, "src/small.cpp", 10)
        self._write_baseline({"src/big.cpp": 3500})
        self._commit("base")
        self.base = self._head()

    def tearDown(self):
        self._tmp.cleanup()

    def _head(self):
        result = subprocess.run(["git", "-C", str(self.root), "rev-parse", "HEAD"],
                                check=True, capture_output=True, text=True)
        return result.stdout.strip()

    def _write_baseline(self, data):
        path = self.root / gate.BASELINE_PATH
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(data), encoding="utf-8")

    def _commit(self, message):
        _git(self.root, "add", "-A")
        _git(self.root, "-c", "user.name=t", "-c", "user.email=t@example.invalid",
             "commit", "-q", "--allow-empty", "-m", message)

    def _gate(self):
        return gate.main(["--root", str(self.root), "--base-ref", self.base])

    def test_unchanged_tree_passes(self):
        self._commit("noop")
        self.assertEqual(self._gate(), 0)

    def test_new_file_over_limit_fails(self):
        _write(self.root, "src/new.cpp", 3001)
        self._commit("pr")
        self.assertEqual(self._gate(), 1)

    def test_new_file_at_limit_passes(self):
        _write(self.root, "src/new.cpp", 3000)
        self._commit("pr")
        self.assertEqual(self._gate(), 0)

    def test_grandfathered_file_grow_fails(self):
        _write(self.root, "src/big.cpp", 3501)
        self._commit("pr")
        self.assertEqual(self._gate(), 1)

    def test_grandfathered_file_shrink_with_baseline_lowered_passes(self):
        _write(self.root, "src/big.cpp", 3400)
        self._write_baseline({"src/big.cpp": 3400})
        self._commit("pr")
        self.assertEqual(self._gate(), 0)

    def test_grandfathered_file_shrink_without_baseline_update_fails(self):
        _write(self.root, "src/big.cpp", 3400)
        self._commit("pr")
        self.assertEqual(self._gate(), 1)

    def test_pr_cannot_raise_baseline_to_hide_growth(self):
        _write(self.root, "src/big.cpp", 3600)
        self._write_baseline({"src/big.cpp": 3600})
        self._commit("pr")
        self.assertEqual(self._gate(), 1)

    def test_pr_cannot_add_baseline_entry_for_new_file(self):
        _write(self.root, "src/new.cpp", 3001)
        self._write_baseline({"src/big.cpp": 3500, "src/new.cpp": 3001})
        self._commit("pr")
        self.assertEqual(self._gate(), 1)

    def _bootstrap_base(self):
        """Make the base commit have no baseline file, as beta does before this gate merges."""
        _git(self.root, "rm", "-q", "--cached", gate.BASELINE_PATH)
        (self.root / gate.BASELINE_PATH).unlink()
        self._commit("remove baseline on base")
        self.base = self._head()

    def test_bootstrap_base_without_baseline_file_passes(self):
        self._bootstrap_base()
        self._write_baseline({"src/big.cpp": 3500})
        self._commit("pr adds baseline")
        self.assertEqual(self._gate(), 0)

    def test_bootstrap_grandfathered_file_growth_fails(self):
        self._bootstrap_base()
        _write(self.root, "src/big.cpp", 3501)
        self._write_baseline({"src/big.cpp": 3501})
        self._commit("pr grows big file")
        self.assertEqual(self._gate(), 1)

    def test_bootstrap_new_oversized_file_fails(self):
        self._bootstrap_base()
        _write(self.root, "src/new.cpp", 3001)
        self._write_baseline({"src/big.cpp": 3500, "src/new.cpp": 3001})
        self._commit("pr adds new big file")
        self.assertEqual(self._gate(), 1)

    def test_new_file_under_limit_passes(self):
        _write(self.root, "src/new.cpp", 2999)
        self._commit("pr")
        self.assertEqual(self._gate(), 0)

    def test_grandfathered_file_shrunk_under_limit_with_entry_removed_passes(self):
        _write(self.root, "src/big.cpp", 2900)
        self._write_baseline({})
        self._commit("pr")
        self.assertEqual(self._gate(), 0)

    def test_grandfathered_file_shrunk_under_limit_with_entry_kept_fails(self):
        _write(self.root, "src/big.cpp", 2900)
        self._commit("pr")
        self.assertEqual(self._gate(), 1)

    def test_new_ipp_implementation_file_over_limit_fails(self):
        _write(self.root, "src/new.ipp", 3001)
        self._commit("pr")
        self.assertEqual(self._gate(), 1)

    def test_unknown_base_ref_fails_closed(self):
        with self.assertRaises(SystemExit):
            gate.main(["--root", str(self.root), "--base-ref", "0" * 40])

    def test_write_baseline_lists_only_files_over_limit(self):
        _write(self.root, "src/at_limit.cpp", 3000)
        self._commit("pr")
        output = self.root / "generated.json"
        gate.main(["--root", str(self.root), "--write-baseline", str(output)])
        self.assertEqual(json.loads(output.read_text(encoding="utf-8")), {"src/big.cpp": 3500})


if __name__ == "__main__":
    unittest.main()
