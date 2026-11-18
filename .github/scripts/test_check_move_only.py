"""Fixture-tree tests for the move-only PR proof."""

import json
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

    def test_unmoved_source_include_rewrite_to_public_moved_header_passes(self):
        self.move("src/old/Widget.hpp", "src/new/include/GoSurvey/New/Widget.hpp")
        self.write("src/old/Widget.cpp", (
            '#include "../new/include/GoSurvey/New/Widget.hpp"\n'
            "void Widget::reset() {}\n"
            "void Widget::run() { --counter; }\n"
        ))
        self.commit("move widget header to public tree")
        self.write("src/old/Widget.cpp", (
            '#include <GoSurvey/New/Widget.hpp>\n'
            "void Widget::reset() {}\n"
            "void Widget::run() { --counter; }\n"
        ))
        self.commit("rewrite include of moved header to public path")

        self.assertEqual(self.check(), [
            "src/old/Widget.hpp -> src/new/include/GoSurvey/New/Widget.hpp",
        ])

    def test_unmoved_source_include_rewrite_to_unrelated_header_fails(self):
        self.move("src/old/Widget.hpp", "src/new/Widget.hpp")
        self.write("src/old/Widget.cpp", (
            '#include "Other.hpp"\n'
            "void Widget::reset() {}\n"
            "void Widget::run() { --counter; }\n"
        ))
        self.commit("point include at an unrelated header")

        with self.assertRaises(ValueError):
            self.check()

    def test_unmoved_source_non_include_edit_fails(self):
        self.move("src/old/Widget.hpp", "src/new/Widget.hpp")
        self.write("src/old/Other.hpp", "#pragma once\n// edited\n")
        self.commit("edit unmoved source body")

        with self.assertRaises(ValueError):
            self.check()

    def test_baseline_may_lose_entries_in_a_move_only_pr(self):
        self.write("docs/architecture/dependency-baseline.json", json.dumps({"violations": [
            {"file": "src/a.cpp", "rule": "forbidden dependency", "target": "Math"},
            {"file": "src/b.cpp", "rule": "private-header include", "target": "Core"},
        ]}))
        self.commit("baseline before move")
        self.base = subprocess.check_output(
            ["git", "-C", str(self.root), "rev-parse", "HEAD"], text=True).strip()
        self.move("src/old/Other.hpp", "src/new/Other.hpp")
        self.write("docs/architecture/dependency-baseline.json", json.dumps({"violations": [
            {"file": "src/a.cpp", "rule": "forbidden dependency", "target": "Math"},
        ]}))
        self.commit("move and shrink baseline")

        self.assertEqual(self.check(), ["src/old/Other.hpp -> src/new/Other.hpp"])

    def test_baseline_may_not_gain_entries_in_a_move_only_pr(self):
        self.write("docs/architecture/dependency-baseline.json", json.dumps({"violations": [
            {"file": "src/a.cpp", "rule": "forbidden dependency", "target": "Math"},
        ]}))
        self.commit("baseline before move")
        self.base = subprocess.check_output(
            ["git", "-C", str(self.root), "rev-parse", "HEAD"], text=True).strip()
        self.move("src/old/Other.hpp", "src/new/Other.hpp")
        self.write("docs/architecture/dependency-baseline.json", json.dumps({"violations": [
            {"file": "src/a.cpp", "rule": "forbidden dependency", "target": "Math"},
            {"file": "src/c.cpp", "rule": "forbidden dependency", "target": "Core"},
        ]}))
        self.commit("move and grow baseline")

        with self.assertRaises(ValueError):
            self.check()

    def test_baseline_entry_file_path_may_follow_a_proven_source_rename(self):
        self.write("docs/architecture/dependency-baseline.json", json.dumps({"violations": [
            {"file": "src/old/Widget.cpp", "rule": "forbidden dependency", "target": "Math",
             "message": "src/old/Widget.cpp includes private Math header"},
        ]}))
        self.commit("baseline before source rename")
        self.base = subprocess.check_output(
            ["git", "-C", str(self.root), "rev-parse", "HEAD"], text=True).strip()
        self.move("src/old/Widget.cpp", "src/new/Widget.cpp")
        self.write("docs/architecture/dependency-baseline.json", json.dumps({"violations": [
            {"file": "src/new/Widget.cpp", "rule": "forbidden dependency", "target": "Math",
             "message": "src/old/Widget.cpp includes private Math header"},
        ]}))
        self.commit("rename source and baseline path")

        self.assertEqual(self.check(), ["src/old/Widget.cpp -> src/new/Widget.cpp"])

    def test_baseline_rename_path_does_not_allow_new_target_or_multiplicity(self):
        self.write("docs/architecture/dependency-baseline.json", json.dumps({"violations": [
            {"file": "src/old/Widget.cpp", "rule": "forbidden dependency", "target": "Math",
             "message": "original"},
        ]}))
        self.commit("baseline before source rename")
        self.base = subprocess.check_output(
            ["git", "-C", str(self.root), "rev-parse", "HEAD"], text=True).strip()
        self.move("src/old/Widget.cpp", "src/new/Widget.cpp")
        self.write("docs/architecture/dependency-baseline.json", json.dumps({"violations": [
            {"file": "src/new/Widget.cpp", "rule": "forbidden dependency", "target": "Renderer",
             "message": "original"},
            {"file": "src/new/Widget.cpp", "rule": "forbidden dependency", "target": "Math",
             "message": "original"},
        ]}))
        self.commit("rename source with changed baseline edge")

        with self.assertRaises(ValueError):
            self.check()

    def test_renamed_baseline_allows_no_additions_targets_multiplicity_or_unrelated_files(self):
        old = [{"file": "src/old/Widget.cpp", "rule": "forbidden dependency", "target": "Math",
                "message": "original"}]
        rewritten = dict(old[0], file="src/new/Widget.cpp")
        cases = [
            [rewritten, dict(rewritten, target="Renderer")],
            [rewritten, dict(rewritten)],
            [rewritten, dict(rewritten, file="src/other.cpp")],
        ]
        baseline = proof.baseline_entry_keys(json.dumps({"violations": old}),
                                             [("src/old/Widget.cpp", "src/new/Widget.cpp")])
        for entries in cases:
            with self.subTest(entries=entries):
                actual = proof.baseline_entry_keys(json.dumps({"violations": entries}),
                                                   [("src/old/Widget.cpp", "src/new/Widget.cpp")])
                self.assertTrue(actual - baseline)

    def prepare_file_size_baseline_move(self):
        self.write(".github/file-size-baseline.json", json.dumps({
            "src/old/Widget.cpp": 3200,
            "src/old/Other.hpp": 3100,
        }))
        self.commit("file-size baseline before move")
        self.base = subprocess.check_output(
            ["git", "-C", str(self.root), "rev-parse", "HEAD"], text=True
        ).strip()
        self.move("src/old/Widget.cpp", "src/new/Widget.cpp")
        self.write("src/new/Widget.cpp", (
            '#include "Widget.hpp"\n'
            "void Widget::reset() {}\n"
            "void Widget::run() { --counter; }\n"
        ))
        self.write("CMakeLists.txt", (
            "add_library(app src/new/Widget.cpp)\n"
            'set(APP_DISPLAY_NAME "Widget.cpp viewer")\n'
        ))

    def test_file_size_baseline_path_rewrite_for_proven_rename_passes(self):
        self.prepare_file_size_baseline_move()
        self.write(".github/file-size-baseline.json", json.dumps({
            "src/new/Widget.cpp": 3200,
            "src/old/Other.hpp": 3100,
        }))
        self.commit("transfer file-size baseline path")

        self.assertEqual(self.check(), ["src/old/Widget.cpp -> src/new/Widget.cpp"])

    def test_file_size_baseline_cap_increase_fails(self):
        self.prepare_file_size_baseline_move()
        self.write(".github/file-size-baseline.json", json.dumps({
            "src/new/Widget.cpp": 3201,
            "src/old/Other.hpp": 3100,
        }))
        self.commit("raise transferred file-size cap")

        with self.assertRaisesRegex(ValueError, "proven rename paths"):
            self.check()

    def test_file_size_baseline_new_entry_fails(self):
        self.prepare_file_size_baseline_move()
        self.write(".github/file-size-baseline.json", json.dumps({
            "src/new/Widget.cpp": 3200,
            "src/old/Other.hpp": 3100,
            "src/new/Extra.cpp": 3001,
        }))
        self.commit("add unrelated file-size baseline entry")

        with self.assertRaisesRegex(ValueError, "proven rename paths"):
            self.check()

    def test_file_size_baseline_unrelated_remap_fails(self):
        self.prepare_file_size_baseline_move()
        self.write(".github/file-size-baseline.json", json.dumps({
            "src/new/Widget.cpp": 3200,
            "src/unrelated/Other.hpp": 3100,
        }))
        self.commit("remap unrelated file-size baseline entry")

        with self.assertRaisesRegex(ValueError, "proven rename paths"):
            self.check()

    def test_file_size_baseline_rename_collision_fails(self):
        self.write(".github/file-size-baseline.json", json.dumps({
            "src/old/Widget.cpp": 3200,
            "src/new/Widget.cpp": 3150,
        }))
        self.commit("file-size baseline with destination already present")
        self.base = subprocess.check_output(
            ["git", "-C", str(self.root), "rev-parse", "HEAD"], text=True
        ).strip()
        self.move("src/old/Widget.cpp", "src/new/Widget.cpp")
        self.write("src/new/Widget.cpp", (
            '#include "Widget.hpp"\n'
            "void Widget::reset() {}\n"
            "void Widget::run() { --counter; }\n"
        ))
        self.write("CMakeLists.txt", (
            "add_library(app src/new/Widget.cpp)\n"
            'set(APP_DISPLAY_NAME "Widget.cpp viewer")\n'
        ))
        self.write(".github/file-size-baseline.json", json.dumps({
            "src/new/Widget.cpp": 3200,
        }))
        self.commit("collide file-size baseline rename")

        with self.assertRaisesRegex(ValueError, "collide"):
            self.check()

    def test_file_size_baseline_entry_drop_fails(self):
        self.prepare_file_size_baseline_move()
        self.write(".github/file-size-baseline.json", json.dumps({
            "src/new/Widget.cpp": 3200,
        }))
        self.commit("drop unrelated file-size baseline entry")

        with self.assertRaisesRegex(ValueError, "proven rename paths"):
            self.check()

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

    def test_cmake_public_include_root_follows_moved_header_tree(self):
        self.write("CMakeLists.txt", (
            "add_library(app src/old/Widget.cpp)\n"
            "target_include_directories(app PRIVATE\n"
            '  "${CMAKE_CURRENT_SOURCE_DIR}/src/old")\n'
        ))
        self.commit("configure old public header root")
        self.base = subprocess.check_output(
            ["git", "-C", str(self.root), "rev-parse", "HEAD"], text=True
        ).strip()
        self.move("src/old/Widget.hpp", "src/new/include/GoSurvey/New/Widget.hpp")
        self.write("CMakeLists.txt", (
            "add_library(app src/old/Widget.cpp)\n"
            "target_include_directories(app PRIVATE\n"
            '  "${CMAKE_CURRENT_SOURCE_DIR}/src/new/include")\n'
        ))
        self.commit("move public header and its include root")

        self.assertEqual(self.check(), [
            "src/old/Widget.hpp -> src/new/include/GoSurvey/New/Widget.hpp",
        ])

    def test_cmake_repository_root_include_can_follow_moved_public_header(self):
        self.write("CMakeLists.txt", (
            "add_library(app src/old/Widget.cpp)\n"
            "target_include_directories(app PRIVATE\n"
            '  "${CMAKE_CURRENT_SOURCE_DIR}/.")\n'
        ))
        self.commit("configure repository include root")
        self.base = subprocess.check_output(
            ["git", "-C", str(self.root), "rev-parse", "HEAD"], text=True
        ).strip()
        self.move("src/old/Widget.hpp", "src/new/include/GoSurvey/New/Widget.hpp")
        self.write("CMakeLists.txt", (
            "add_library(app src/old/Widget.cpp)\n"
            "target_include_directories(app PRIVATE\n"
            '  "${CMAKE_CURRENT_SOURCE_DIR}/src/new/include")\n'
        ))
        self.commit("move public header under new include root")

        self.assertEqual(self.check(), [
            "src/old/Widget.hpp -> src/new/include/GoSurvey/New/Widget.hpp",
        ])

    def test_cmake_include_root_cannot_move_to_unrelated_tree(self):
        self.write("CMakeLists.txt", (
            "add_library(app src/old/Widget.cpp)\n"
            "target_include_directories(app PRIVATE\n"
            '  "${CMAKE_CURRENT_SOURCE_DIR}/src/old")\n'
        ))
        self.commit("configure old public header root")
        self.base = subprocess.check_output(
            ["git", "-C", str(self.root), "rev-parse", "HEAD"], text=True
        ).strip()
        self.move("src/old/Widget.hpp", "src/new/include/GoSurvey/New/Widget.hpp")
        self.write("CMakeLists.txt", (
            "add_library(app src/old/Widget.cpp)\n"
            "target_include_directories(app PRIVATE\n"
            '  "${CMAKE_CURRENT_SOURCE_DIR}/src/unrelated/include")\n'
        ))
        self.commit("point public include root at unrelated tree")

        with self.assertRaisesRegex(ValueError, "moved-file build entry path rewrite"):
            self.check()

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

    def test_source_variable_move_is_allowed_but_display_variable_is_not(self):
        for variable, allowed in [("APP_SOURCES", True), ("APP_DISPLAY_NAME", False)]:
            with self.subTest(variable=variable):
                old = f"set ({variable}\n  src/old/Widget.cpp)\n"
                new = f"set ({variable}\n  src/new/Widget.cpp)\n"
                self.assertEqual(proof.is_build_path_rewrite(
                    old.splitlines()[1], new.splitlines()[1],
                    "CMakeLists.txt", "CMakeLists.txt", 2, 2, old, new,
                    [("src/old/Widget.cpp", "src/new/Widget.cpp")]), allowed)

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
