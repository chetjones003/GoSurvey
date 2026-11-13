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
        self.write("src/old/Widget.hpp", "#pragma once\nclass Widget {};\n")
        self.write("src/old/Widget.cpp", '#include "Widget.hpp"\nvoid Widget::run() {}\n')
        self.write("CMakeLists.txt", "add_library(app src/old/Widget.cpp)\n")
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
        self.write("src/new/Widget.cpp", '#include "../new/Widget.hpp"\nvoid Widget::run() {}\n')
        self.write("CMakeLists.txt", "add_library(app src/new/Widget.cpp)\n")
        self.commit("move widget")

        self.assertEqual(self.check(), [
            "src/old/Widget.cpp -> src/new/Widget.cpp",
            "src/old/Widget.hpp -> src/new/Widget.hpp",
        ])

    def test_real_source_edit_fails(self):
        self.move("src/old/Widget.cpp", "src/new/Widget.cpp")
        self.write("src/new/Widget.cpp", '#include "Widget.hpp"\nvoid Widget::run() { changed(); }\n')
        self.move("src/old/Widget.hpp", "src/new/Widget.hpp")
        self.commit("move and edit widget")

        with self.assertRaisesRegex(ValueError, "not an include or build-file path rewrite"):
            self.check()

    def test_added_file_fails_even_when_other_files_are_renamed(self):
        self.move("src/old/Widget.hpp", "src/new/Widget.hpp")
        self.move("src/old/Widget.cpp", "src/new/Widget.cpp")
        self.write("src/new/Widget.cpp", '#include "../new/Widget.hpp"\nvoid Widget::run() {}\n')
        self.write("src/new/extra.cpp", "void extra() {}\n")
        self.commit("add file during move")

        with self.assertRaisesRegex(ValueError, "changed files must be renames"):
            self.check()

    def test_build_file_path_rewrite_is_allowed(self):
        self.move("src/old/Widget.hpp", "src/new/Widget.hpp")
        self.move("src/old/Widget.cpp", "src/new/Widget.cpp")
        self.write("src/new/Widget.cpp", '#include "../new/Widget.hpp"\nvoid Widget::run() {}\n')
        self.write("CMakeLists.txt", "add_library(app src/new/Widget.cpp)\n")
        self.commit("move build entry")

        self.assertEqual(len(self.check()), 2)


if __name__ == "__main__":
    unittest.main()
