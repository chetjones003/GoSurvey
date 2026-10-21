"""Tests for diff-scoped MSVC analysis target selection."""

import importlib.util
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).with_name("select_msvc_analyze_targets.py")
SPEC = importlib.util.spec_from_file_location("select_msvc_analyze_targets", SCRIPT)
selector = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(selector)


class SelectMsvcAnalyzeTargetsTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / "cmake").mkdir()
        (self.root / "cmake" / "gosurvey_pch.hpp").write_text("#include <vector>\n")
        (self.root / "include").mkdir()
        (self.root / "src").mkdir()

        self.common = self.root / "include" / "Common.hpp"
        self.common.write_text("#pragma once\n")
        self.nested = self.root / "include" / "Nested.hpp"
        self.nested.write_text('#include "Common.hpp"\n')
        self.first = self.root / "src" / "first.cpp"
        self.first.write_text('#include "Nested.hpp"\n')
        self.second = self.root / "src" / "second.cpp"
        self.second.write_text('#include "Other.hpp"\n')
        (self.root / "include" / "Other.hpp").write_text("#pragma once\n")
        self.analyzed = [
            self.entry(self.first, "CMakeFiles/App.dir/src/first.cpp.obj"),
            self.entry(self.second, "CMakeFiles/App.dir/src/second.cpp.obj"),
        ]

    def entry(self, source, target):
        return {
            "source": source,
            "target": target,
            "include_dirs": [self.root / "include", self.root],
        }

    def test_changed_source_selects_only_its_translation_unit(self):
        targets, reason = selector.select_targets([self.first], self.analyzed, self.root)
        self.assertEqual(targets, ["CMakeFiles/App.dir/src/first.cpp.obj"])
        self.assertIn("diff-scoped", reason)

    def test_changed_transitive_header_selects_all_consumers_only(self):
        targets, _ = selector.select_targets([self.common], self.analyzed, self.root)
        self.assertEqual(targets, ["CMakeFiles/App.dir/src/first.cpp.obj"])

    def test_unrelated_header_selects_no_translation_units(self):
        unrelated = self.root / "include" / "Unused.hpp"
        unrelated.write_text("#pragma once\n")
        targets, _ = selector.select_targets([unrelated], self.analyzed, self.root)
        self.assertEqual(targets, [])

    def test_macro_include_falls_back_to_all_analyzed_translation_units(self):
        self.second.write_text("#include PROJECT_HEADER\n")
        targets, reason = selector.select_targets([self.common], self.analyzed, self.root)
        self.assertEqual(len(targets), 2)
        self.assertIn("full-project fallback", reason)

    def test_unanalyzed_changed_source_falls_back_to_complete_scope(self):
        unlisted = self.root / "src" / "unlisted.cpp"
        unlisted.write_text("int main() {}\n")
        targets, reason = selector.select_targets([unlisted], self.analyzed, self.root)
        self.assertEqual(len(targets), 2)
        self.assertIn("no analyzed compile entry", reason)

    def test_source_generation_change_falls_back_to_complete_scope(self):
        ui_file = self.root / "Dialog.ui"
        ui_file.write_text("<ui/>\n")
        targets, reason = selector.select_targets([ui_file], self.analyzed, self.root)
        self.assertEqual(len(targets), 2)
        self.assertIn("build or source-generation input", reason)

    def test_analyzer_affecting_cmake_change_falls_back_to_complete_scope(self):
        targets, reason = selector.select_targets(
            [], self.analyzed, self.root, analysis_configuration_changed=True
        )
        self.assertEqual(len(targets), 2)
        self.assertIn("analyzer-affecting CMake configuration", reason)

    def test_cmake_test_registration_change_keeps_scope_diff_based(self):
        diff = """diff --git a/cmake/GoSurveyQtTests.cmake b/cmake/GoSurveyQtTests.cmake
--- a/cmake/GoSurveyQtTests.cmake
+++ b/cmake/GoSurveyQtTests.cmake
@@ -1 +1 @@
-add_test(NAME old COMMAND test)
+add_test(NAME new COMMAND test)
"""
        self.assertFalse(selector.cmake_diff_affects_analysis(diff))

    def test_cmake_compile_flag_change_requests_full_scope(self):
        diff = """diff --git a/CMakeLists.txt b/CMakeLists.txt
--- a/CMakeLists.txt
+++ b/CMakeLists.txt
@@ -10 +10 @@
-target_compile_options(GoSurvey PRIVATE /W4)
+target_compile_options(GoSurvey PRIVATE /W4 /analyze)
"""
        self.assertTrue(selector.cmake_diff_affects_analysis(diff))

    def test_cmake_link_dependency_change_requests_full_scope(self):
        diff = """diff --git a/CMakeLists.txt b/CMakeLists.txt
--- a/CMakeLists.txt
+++ b/CMakeLists.txt
@@ -10 +10 @@
-target_link_libraries(App PRIVATE Qt6::Core)
+target_link_libraries(App PRIVATE Qt6::Core Qt6::Gui)
"""
        self.assertTrue(selector.cmake_diff_affects_analysis(diff))

    def test_comment_about_compiler_flags_does_not_force_full_scope(self):
        diff = """diff --git a/CMakeLists.txt b/CMakeLists.txt
--- a/CMakeLists.txt
+++ b/CMakeLists.txt
@@ -10 +10 @@
-# sccache used /Zi before this configure step.
+# sccache now needs /Z7 from this configure step.
"""
        self.assertFalse(selector.cmake_diff_affects_analysis(diff))


if __name__ == "__main__":
    unittest.main()
