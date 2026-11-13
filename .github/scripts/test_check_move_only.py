"""Fixture-tree tests for the move-only PR proof."""

import subprocess
import tempfile
import unittest
from pathlib import Path

import check_move_only as proof


def git(root, *args):
    subprocess.run(["git", "-C", str(root), *args], check=True, capture_output=True)


class MoveOnlyFixtureTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        git(self.root, "init", "-q")
        git(self.root, "config", "user.email", "move-proof@example.test")
        git(self.root, "config", "user.name", "Move Proof")
        self.write("src/old/Widget.hpp", '#pragma once\nclass Widget {};\n#include "Other.hpp"\n')
        self.write("src/old/Other.hpp", "#pragma once\n")
        self.write("src/old/Widget.cpp", (
            '#include "Widget.hpp"\n'
            "void Widget::reset() {}\n"
            "void Widget::run() { --counter; }\n"
        ))
        self.write("CMakeLists.txt", (
            "add_library(app src/old/Widget.cpp)\n"
            'set(APP_DISPLAY_NAME "Widget.cpp viewer")\n'
        ))
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

    def write_bytes(self, relative, content):
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content)

    def move(self, old, new):
        destination = self.root / new
        destination.parent.mkdir(parents=True, exist_ok=True)
        (self.root / old).replace(destination)

    def commit(self, message):
        git(self.root, "add", "-A")
        git(self.root, "commit", "-qm", message)

    def check(self):
        return proof.check_move_only(self.root, self.base, "HEAD")

    def test_directory_moves_and_include_and_build_paths_pass(self):
        self.move("src/old/Widget.hpp", "src/new/Widget.hpp")
        self.move("src/old/Widget.cpp", "src/new/Widget.cpp")
        self.write("src/new/Widget.cpp", (
            '#include "../new/Widget.hpp"\n'
            "void Widget::reset() {}\n"
            "void Widget::run() { --counter; }\n"
        ))
        self.write("CMakeLists.txt", (
            "add_library(app src/new/Widget.cpp)\n"
            'set(APP_DISPLAY_NAME "Widget.cpp viewer")\n'
        ))
        self.commit("move widget")

        self.assertEqual(self.check(), [
            "src/old/Widget.cpp -> src/new/Widget.cpp",
            "src/old/Widget.hpp -> src/new/Widget.hpp",
        ])

    def test_real_source_edit_fails(self):
        self.move("src/old/Widget.cpp", "src/new/Widget.cpp")
        self.write("src/new/Widget.cpp", (
            '#include "Widget.hpp"\n'
            "void Widget::reset() {}\n"
            "void Widget::run() { changed(); }\n"
        ))
        self.move("src/old/Widget.hpp", "src/new/Widget.hpp")
        self.commit("move and edit widget")

        with self.assertRaises(ValueError):
            self.check()

    def test_added_file_fails_even_when_other_files_are_renamed(self):
        self.move("src/old/Widget.hpp", "src/new/Widget.hpp")
        self.move("src/old/Widget.cpp", "src/new/Widget.cpp")
        self.write("src/new/Widget.cpp", (
            '#include "../new/Widget.hpp"\n'
            "void Widget::reset() {}\n"
            "void Widget::run() { --counter; }\n"
        ))
        self.write("src/new/extra.cpp", "void extra() {}\n")
        self.commit("add file during move")

        with self.assertRaisesRegex(ValueError, "changed files must be renames"):
            self.check()

    def test_build_file_path_rewrite_is_allowed(self):
        self.move("src/old/Widget.hpp", "src/new/Widget.hpp")
        self.move("src/old/Widget.cpp", "src/new/Widget.cpp")
        self.write("src/new/Widget.cpp", (
            '#include "../new/Widget.hpp"\n'
            "void Widget::reset() {}\n"
            "void Widget::run() { --counter; }\n"
        ))
        self.write("CMakeLists.txt", (
            "add_library(app src/new/Widget.cpp)\n"
            'set(APP_DISPLAY_NAME "Widget.cpp viewer")\n'
        ))
        self.commit("move build entry")

        self.assertEqual(len(self.check()), 2)

    def test_multiline_cmake_source_entry_path_rewrite_is_allowed(self):
        self.write("CMakeLists.txt", (
            "add_library(\n"
            "  app\n"
            "  src/old/Widget.cpp\n"
            ")\n"
            'set(APP_DISPLAY_NAME "Widget.cpp viewer")\n'
        ))
        self.commit("format source list")
        self.base = subprocess.check_output(
            ["git", "-C", str(self.root), "rev-parse", "HEAD"], text=True
        ).strip()
        self.move("src/old/Widget.cpp", "src/new/Widget.cpp")
        self.write("CMakeLists.txt", (
            "add_library(\n"
            "  app\n"
            "  src/new/Widget.cpp\n"
            ")\n"
            'set(APP_DISPLAY_NAME "Widget.cpp viewer")\n'
        ))
        self.commit("move source in multiline list")

        self.assertEqual(self.check(), ["src/old/Widget.cpp -> src/new/Widget.cpp"])

    def test_binary_content_change_fails(self):
        self.move("src/old/Widget.hpp", "src/new/Widget.hpp")
        self.move("src/old/Widget.cpp", "src/new/Widget.cpp")
        self.write_bytes("src/new/Widget.hpp", b"#pragma once\nclass Widget {};\n\0changed")
        self.write("src/new/Widget.cpp", (
            '#include "../new/Widget.hpp"\n'
            "void Widget::reset() {}\n"
            "void Widget::run() { --counter; }\n"
        ))
        self.write("CMakeLists.txt", (
            "add_library(app src/new/Widget.cpp)\n"
            'set(APP_DISPLAY_NAME "Widget.cpp viewer")\n'
        ))
        self.commit("change bytes during move")

        with self.assertRaisesRegex(ValueError, "binary content changed"):
            self.check()

    def test_unrelated_include_path_fails(self):
        self.move("src/old/Widget.hpp", "src/new/Widget.hpp")
        self.move("src/old/Widget.cpp", "src/new/Widget.cpp")
        self.write("src/new/Widget.cpp", (
            '#include "../unrelated/Widget.hpp"\n'
            "void Widget::reset() {}\n"
            "void Widget::run() { --counter; }\n"
        ))
        self.write("CMakeLists.txt", (
            "add_library(app src/new/Widget.cpp)\n"
            'set(APP_DISPLAY_NAME "Widget.cpp viewer")\n'
        ))
        self.commit("redirect include during move")

        with self.assertRaises(ValueError):
            self.check()

    def test_unrelated_build_string_change_fails(self):
        self.move("src/old/Widget.cpp", "src/new/Gadget.cpp")
        self.write("CMakeLists.txt", (
            "add_library(app src/new/Gadget.cpp)\n"
            "set(APP_DISPLAY_NAME \"Gadget.cpp viewer\")\n"
        ))
        self.commit("rename source and display name")

        with self.assertRaisesRegex(ValueError, "not a moved-file build entry"):
            self.check()

    def test_moved_path_in_multiline_message_is_not_a_build_entry(self):
        self.write("CMakeLists.txt", (
            "add_library(app src/old/Widget.cpp)\n"
            'set(APP_DISPLAY_NAME "Widget.cpp viewer")\n'
            "message(STATUS\n"
            '  "src/old/Widget.cpp"\n'
            ")\n"
        ))
        self.commit("add path to build message")
        self.base = subprocess.check_output(
            ["git", "-C", str(self.root), "rev-parse", "HEAD"], text=True
        ).strip()
        self.move("src/old/Widget.cpp", "src/new/Widget.cpp")
        self.write("CMakeLists.txt", (
            "add_library(app src/new/Widget.cpp)\n"
            'set(APP_DISPLAY_NAME "Widget.cpp viewer")\n'
            "message(STATUS\n"
            '  "src/new/Widget.cpp"\n'
            ")\n"
        ))
        self.commit("rewrite message with moved path")

        with self.assertRaises(ValueError):
            self.check()

    def test_increment_operator_edits_are_not_ignored(self):
        self.move("src/old/Widget.hpp", "src/new/Widget.hpp")
        self.move("src/old/Widget.cpp", "src/new/Widget.cpp")
        self.write("src/new/Widget.cpp", (
            '#include "../new/Widget.hpp"\n'
            "void Widget::reset() {}\n"
            "void Widget::run() { ++counter; }\n"
        ))
        self.write("CMakeLists.txt", (
            "add_library(app src/new/Widget.cpp)\n"
            'set(APP_DISPLAY_NAME "Widget.cpp viewer")\n'
        ))
        self.commit("change behavior during move")

        with self.assertRaises(ValueError):
            self.check()

    def test_final_newline_change_fails(self):
        self.write("src/old/Other.hpp", "#pragma once\n")
        self.write("src/old/Widget.hpp", '#pragma once\nclass Widget {};\n#include "Other.hpp"\n')
        self.move("src/old/Widget.hpp", "src/new/Widget.hpp")
        self.move("src/old/Other.hpp", "src/new/Other.hpp")
        self.move("src/old/Widget.cpp", "src/new/Widget.cpp")
        self.write("src/new/Widget.hpp", '#pragma once\nclass Widget {};\n#include "../new/Other.hpp"')
        self.write("src/new/Widget.cpp", (
            '#include "../new/Widget.hpp"\n'
            "void Widget::reset() {}\n"
            "void Widget::run() { --counter; }\n"
        ))
        self.write("CMakeLists.txt", (
            "add_library(app src/new/Widget.cpp)\n"
            'set(APP_DISPLAY_NAME "Widget.cpp viewer")\n'
        ))
        self.commit("change newline during move")

        with self.assertRaisesRegex(ValueError, "final newline changed"):
            self.check()

    def test_executable_mode_change_fails(self):
        self.move("src/old/Widget.cpp", "src/new/Widget.cpp")
        self.write("CMakeLists.txt", (
            "add_library(app src/new/Widget.cpp)\n"
            'set(APP_DISPLAY_NAME "Widget.cpp viewer")\n'
        ))
        git(self.root, "add", "-A")
        git(self.root, "update-index", "--chmod=+x", "src/new/Widget.cpp")
        git(self.root, "commit", "-qm", "change mode during move")

        with self.assertRaisesRegex(ValueError, "file mode changed"):
            self.check()


class WorkflowLabelTests(unittest.TestCase):
    def test_workflows_use_exact_label_membership(self):
        repo = Path(__file__).resolve().parents[2]
        expression = "contains(github.event.pull_request.labels.*.name, 'refactor: move-only')"
        pr_agent = (repo / ".github/workflows/pr-agent.yml").read_text(encoding="utf-8")
        pr_checks = (repo / ".github/workflows/pr-checks.yml").read_text(encoding="utf-8")

        self.assertIn(expression, pr_agent)
        self.assertEqual(pr_checks.count(expression), 3)
        self.assertNotIn("contains(toJSON(github.event.pull_request.labels.*.name)", pr_agent + pr_checks)


class IncludeMappingTests(unittest.TestCase):
    def test_include_paths_must_resolve_to_the_corresponding_moved_file(self):
        moves = [
            ("src/old/Widget.hpp", "src/new/Widget.hpp"),
            ("src/old/Widget.cpp", "src/new/Widget.cpp"),
        ]

        self.assertTrue(proof.include_path_maps(
            "Widget.hpp", "../new/Widget.hpp", "src/old/Widget.cpp", "src/new/Widget.cpp", moves
        ))
        self.assertFalse(proof.include_path_maps(
            "Widget.hpp", "../unrelated/Widget.hpp", "src/old/Widget.cpp", "src/new/Widget.cpp", moves
        ))
        self.assertFalse(proof.include_path_maps(
            "vendor/src/old/Widget.hpp",
            "vendor/src/new/Widget.hpp",
            "src/old/Widget.cpp",
            "src/new/Widget.cpp",
            moves,
        ))


if __name__ == "__main__":
    unittest.main()
