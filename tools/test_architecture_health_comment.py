import importlib.util
import unittest
from pathlib import Path

SCRIPT = Path(__file__).with_name("architecture_health_comment.py")
SPEC = importlib.util.spec_from_file_location("architecture_health_comment", SCRIPT)
health_comment = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(health_comment)


def snapshot(commit, violations, largest_path, largest_lines, fan_in):
    return {
        "commit": commit,
        "metrics": {
            "violation_count": violations,
            "largest_file": {"path": largest_path, "lines": largest_lines},
            "god_header_fan_in": {"path": "src/commands/CadCommands.hpp", "fan_in": fan_in},
        },
    }


class ArchitectureHealthCommentTests(unittest.TestCase):
    def test_renders_metric_changes_and_trends(self):
        baseline = snapshot("a" * 40, 12, "src/old.cpp", 2400, 39)
        current = snapshot("b" * 40, 9, "src/new.cpp", 2500, 39)
        trend = [snapshot("c" * 40, 12, "src/old.cpp", 2400, 39)]

        body = health_comment.render_comment(baseline, current, trend, 1216)

        self.assertIn("| Dependency violations | 12 | 9 | -3 (better) |", body)
        self.assertIn("| Largest source file | `src/old.cpp` (2,400 lines) | `src/new.cpp` (2,500 lines) | +100 lines (worse) |", body)
        self.assertIn("| `CadCommands.hpp` include fan-in | 39 | 39 | 0 (same) |", body)
        self.assertEqual(body.count("```mermaid"), 3)
        self.assertIn('x-axis ["cccccc", "PR #1216"]', body)
        self.assertIn("line [12, 9]", body)
        self.assertIn("line [2400, 2500]", body)
        self.assertIn("line [39, 39]", body)

    def test_missing_baseline_is_explained_and_comment_still_has_pr_metrics(self):
        current = snapshot("b" * 40, 0, "src/new.cpp", 10, 2)

        body = health_comment.render_comment(None, current, [], 1216)

        self.assertIn("No beta snapshot was found for the PR merge base", body)
        self.assertIn("| Dependency violations | unavailable | 0 | unavailable |", body)
        self.assertIn('x-axis ["PR #1216"]', body)

    def test_metric_direction_marks_each_lower_metric_as_better(self):
        baseline = snapshot("a" * 40, 1, "src/a.cpp", 100, 5)
        current = snapshot("b" * 40, 2, "src/b.cpp", 100, 4)

        body = health_comment.render_comment(baseline, current, [current], 8)

        self.assertIn("| Dependency violations | 1 | 2 | +1 (worse) |", body)
        self.assertIn("| Largest source file | `src/a.cpp` (100 lines) | `src/b.cpp` (100 lines) | 0 lines (same) |", body)
        self.assertIn("| `CadCommands.hpp` include fan-in | 5 | 4 | -1 (better) |", body)

    def test_beta_build_times_are_trends_only_and_pr_is_not_included(self):
        beta = snapshot("c" * 40, 1, "src/a.cpp", 10, 2)
        beta["build_times"] = {"clean_release_seconds": 120, "domain_header_rebuild_seconds": 8}
        current = snapshot("b" * 40, 0, "src/a.cpp", 10, 2)
        current["build_times"] = {"clean_release_seconds": 999, "domain_header_rebuild_seconds": 999}

        body = health_comment.render_comment(None, current, [beta], 1217)

        self.assertIn("### Beta build-time trend (seconds)", body)
        self.assertIn('x-axis ["cccccc"]', body)
        self.assertIn("line [120]", body)
        self.assertIn("line [8]", body)
        self.assertNotIn("999", body)
        self.assertNotIn("better", body.split("### Beta build-time trend", 1)[1])


if __name__ == "__main__":
    unittest.main()
