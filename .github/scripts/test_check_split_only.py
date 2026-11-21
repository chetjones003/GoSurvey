"""Fixture-tree tests for the split-only PR proof (ADR-0012, Track A)."""

import json
import subprocess
import tempfile
import unittest
from pathlib import Path

import check_split_only as proof


BASELINE = ".github/file-size-baseline.json"
OLD_CPP = (
    "void Widget::reset() {}\n"
    "void Widget::run() { --counter; }\n"
)


def git(root, *args):
    subprocess.run(["git", "-C", str(root), *args], check=True, capture_output=True)


class SplitOnlyFixtureTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        git(self.root, "init", "-q")
        git(self.root, "config", "user.email", "split-proof@example.test")
        git(self.root, "config", "user.name", "Split Proof")
        self.write("src/old/Widget.cpp", '#include "Widget.hpp"\n' + OLD_CPP)
        self.write("src/old/Widget.hpp", "#pragma once\nclass Widget {};\n")
        self.write("CMakeLists.txt", "add_library(app src/old/Widget.cpp)\n")
        self.write(BASELINE, json.dumps({"src/old/Widget.cpp": 100, "src/old/Other.cpp": 50}))
        self.commit("base")
        self.base = subprocess.check_output(
            ["git", "-C", str(self.root), "rev-parse", "HEAD"], text=True
        ).strip()

    def tearDown(self):
        self.temp.cleanup()

    def write(self, relative, content):
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8")

    def commit(self, message):
        git(self.root, "add", "-A")
        git(self.root, "commit", "-qm", message)

    def check(self):
        return proof.check_split_only(self.root, self.base, "HEAD")

    def split_widget_into_family(self, new_cpp, extra_new_lines=""):
        """Move the reset and run definitions into src/new/WidgetRun.cpp."""
        self.write("src/old/Widget.cpp", '#include "Widget.hpp"\n')
        self.write("src/new/WidgetRun.cpp", (
            '#include "../old/Widget.hpp"\n'
            + extra_new_lines
            + new_cpp
        ))

    # Happy paths -----------------------------------------------------------

    def test_moving_whole_lines_into_a_new_file_passes(self):
        self.split_widget_into_family(OLD_CPP)
        self.commit("split widget")

        self.assertCountEqual(self.check(), [
            "M src/old/Widget.cpp",
            "A src/new/WidgetRun.cpp",
        ])

    def test_added_include_and_forward_declaration_pass(self):
        self.split_widget_into_family(
            OLD_CPP,
            extra_new_lines="#include <vector>\nclass Helper;\n",
        )
        self.commit("split widget with includes and a forward declaration")

        self.check()

    def test_added_function_prototype_passes(self):
        self.write("src/old/Widget.cpp", '#include "Widget.hpp"\n')
        self.write("src/new/WidgetRun.hpp", "void Widget_run(int counter);\n")
        self.write("src/new/WidgetRun.cpp", OLD_CPP)
        self.commit("move code and declare its entry point in a new header")

        self.check()

    def test_blank_lines_are_ignored(self):
        self.write("src/old/Widget.cpp", (
            '#include "Widget.hpp"\n'
            "void Widget::reset() {}\n"
        ))
        self.write("src/new/WidgetRun.cpp", (
            "void Widget::run() { --counter; }\n\n"
        ))
        self.commit("move run and drop a blank line")

        self.check()

    def test_deleted_file_whose_lines_reappear_in_a_new_file_passes(self):
        git(self.root, "rm", "-q", "src/old/Widget.cpp")
        self.write("src/new/WidgetRun.cpp", '#include "Widget.hpp"\n' + OLD_CPP)
        self.write("CMakeLists.txt", (
            "add_library(app src/old/Widget.cpp)\n"
            "target_sources(app PRIVATE src/new/WidgetRun.cpp)\n"
        ))
        self.commit("delete old file and move its contents")

        self.check()

    def test_cmake_source_entry_addition_passes(self):
        self.split_widget_into_family(OLD_CPP)
        self.write("CMakeLists.txt", (
            "add_library(app src/old/Widget.cpp)\n"
            "target_sources(app PRIVATE src/new/WidgetRun.cpp)\n"
        ))
        self.commit("split and register the new source")

        self.check()

    def test_baseline_cap_decrease_passes(self):
        self.split_widget_into_family(OLD_CPP)
        self.write(BASELINE, json.dumps({"src/old/Widget.cpp": 40, "src/old/Other.cpp": 50}))
        self.commit("lower the cap")

        self.check()

    def test_baseline_entry_removal_passes(self):
        self.split_widget_into_family(OLD_CPP)
        self.write(BASELINE, json.dumps({"src/old/Other.cpp": 50}))
        self.commit("drop a file that is now under the limit")

        self.check()

    # Rejections ------------------------------------------------------------

    def test_changed_moved_line_fails(self):
        self.split_widget_into_family(OLD_CPP.replace("--counter", "++counter"))
        self.commit("move code and change behaviour")

        with self.assertRaisesRegex(ValueError, "do not reappear verbatim"):
            self.check()

    def test_deleted_code_fails(self):
        self.write("src/old/Widget.cpp", '#include "Widget.hpp"\nvoid Widget::reset() {}\n')
        self.commit("delete run without moving it")

        with self.assertRaisesRegex(ValueError, "do not reappear verbatim"):
            self.check()

    def test_added_statement_fails(self):
        self.split_widget_into_family(OLD_CPP + "return_value = compute();\n")
        self.commit("sneak a statement into the split")

        with self.assertRaisesRegex(ValueError, "not an include"):
            self.check()

    def test_added_return_call_is_not_mistaken_for_a_prototype(self):
        self.split_widget_into_family(OLD_CPP + "return compute(counter);\n")
        self.commit("add a return statement")

        with self.assertRaisesRegex(ValueError, "not an include"):
            self.check()

    def test_added_non_source_cmake_line_fails(self):
        self.split_widget_into_family(OLD_CPP)
        self.write("CMakeLists.txt", (
            "add_library(app src/old/Widget.cpp)\n"
            'set(APP_VERSION "2")\n'
        ))
        self.commit("change a CMake variable")

        with self.assertRaisesRegex(ValueError, "CMake source entry"):
            self.check()

    def test_baseline_cap_increase_fails(self):
        self.split_widget_into_family(OLD_CPP)
        self.write(BASELINE, json.dumps({"src/old/Widget.cpp": 500, "src/old/Other.cpp": 50}))
        self.commit("raise a cap")

        with self.assertRaisesRegex(ValueError, "grew"):
            self.check()

    def test_baseline_new_entry_fails(self):
        self.split_widget_into_family(OLD_CPP)
        self.write(BASELINE, json.dumps({
            "src/old/Widget.cpp": 100,
            "src/old/Other.cpp": 50,
            "src/new/WidgetRun.cpp": 10,
        }))
        self.commit("add a baseline entry")

        with self.assertRaisesRegex(ValueError, "may not add"):
            self.check()

    def test_binary_change_fails(self):
        self.split_widget_into_family(OLD_CPP)
        (self.root / "assets").mkdir()
        (self.root / "assets/icon.bin").write_bytes(b"\x00\x01\x02")
        self.commit("add a binary file")

        with self.assertRaisesRegex(ValueError, "binary"):
            self.check()

    def test_pure_addition_with_no_moved_lines_fails(self):
        self.write("src/new/Empty.cpp", "#include <vector>\n")
        self.commit("add an include only")

        with self.assertRaisesRegex(ValueError, "no moved lines"):
            self.check()


if __name__ == "__main__":
    unittest.main()
