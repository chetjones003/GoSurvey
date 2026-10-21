import tempfile
import unittest
from pathlib import Path

from report_ci_performance import cache_summary, ninja_summary, test_timings


class PerformanceEvidenceTests(unittest.TestCase):
    def test_cache_reuse_includes_requests_rejected_for_pch(self):
        summary = cache_summary({"stats": {
            "compile_requests": 430,
            "requests_not_cacheable": 313,
            "cache_hits": {"counts": {"C/C++": 114}},
            "cache_misses": {"counts": {"C/C++": 3}},
            "not_cached": {"/Fp": 311, "/Yc": 2},
        }})
        self.assertEqual(summary["hits"], 114)
        self.assertAlmostEqual(summary["hit_fraction_all_requests"], 114 / 430)
        self.assertEqual(summary["uncacheable"], 313)

    def test_empty_cache_statistics_do_not_divide_by_zero(self):
        self.assertEqual(cache_summary({"stats": {}})["hit_fraction_all_requests"], 0)

    def test_aborted_test_report_does_not_hide_other_timing_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            reports = root / "reports"
            reports.mkdir()
            (reports / "finished.xml").write_text(
                '<testsuite><testcase name="finished" time="1.25"/></testsuite>', encoding="utf-8")
            (reports / "aborted.xml").write_text('<testsuite>', encoding="utf-8")
            cases, incomplete = test_timings(root / "build/.cmake/release", reports)
        self.assertEqual(cases[0]["seconds"], 1.25)
        self.assertEqual(len(incomplete), 1)
        self.assertIn("aborted.xml", incomplete[0])

    def test_ninja_counts_parallel_commands_once_for_multiple_outputs(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / ".ninja_log"
            path.write_text("# ninja log v7\n0\t1000\t1\ta.obj\taa\n"
                            "0\t1000\t1\ta.pdb\taa\n500\t2000\t1\tb.obj\tbb\n"
                            "2000\t2200\t1\tapp.exe\tcc\n", encoding="utf-8")
            summary = ninja_summary(path)
        self.assertEqual(summary["commands"], 3)
        self.assertEqual(summary["peak_running_commands"], 2)
        self.assertAlmostEqual(summary["wall_seconds"], 2.2)
        self.assertEqual(summary["longest_commands"][0]["output"], "b.obj")


if __name__ == "__main__":
    unittest.main()
