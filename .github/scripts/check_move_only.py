#!/usr/bin/env python3
"""Prove that a `refactor: move-only` PR contains renames and path rewrites only."""

import argparse
import json
import posixpath
import re
import subprocess
import sys
from collections import Counter
from pathlib import PurePosixPath


BUILD_FILE_NAMES = {"CMakeLists.txt", "CMakePresets.json"}
BUILD_FILE_SUFFIXES = (".cmake", ".vcxproj", ".vcxproj.filters", ".props", ".targets", ".sln")
SOURCE_FILE_SUFFIXES = (".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx")
DEPENDENCY_BASELINE = "docs/architecture/dependency-baseline.json"
FILE_SIZE_BASELINE = ".github/file-size-baseline.json"
INCLUDE_LINE = re.compile(r'^(\s*#\s*include\s*)([<"])([^>"\n]+)([>"])(\s*(?://.*|/\*.*\*/)?\s*)$')
CMAKE_SOURCE_COMMANDS = {"add_executable", "add_library", "target_sources", "source_group"}
CMAKE_SOURCE_VARIABLE = re.compile(r"^\s*([A-Za-z_]\w*)", re.IGNORECASE)
HUNK_HEADER = re.compile(r"^@@ -(\d+)(?:,\d+)? \+(\d+)(?:,\d+)? @@")
PATH_TOKEN_CHARS = "_.-/\\"


def git(root, *args):
    return subprocess.run(
        ["git", "-C", str(root), *args], check=True, capture_output=True
    ).stdout


def git_text(root, ref, path):
    return git(root, "show", f"{ref}:{path}").decode("utf-8", errors="surrogateescape")


def is_build_file(path):
    name = PurePosixPath(path).name
    return name in BUILD_FILE_NAMES or path.endswith(BUILD_FILE_SUFFIXES)


def is_source_file(path):
    return path.endswith(SOURCE_FILE_SUFFIXES)


def classify_changed_files(root, base_ref, head_ref):
    fields = git(
        root, "diff", "--name-status", "-z", "--find-renames=20%", f"{base_ref}...{head_ref}"
    ).decode("utf-8", errors="surrogateescape").split("\0")
    fields = [field for field in fields if field]
    moves = []
    build_edits = []
    include_edits = []
    baseline_edits = []
    file_size_baseline_edits = []
    index = 0
    while index < len(fields):
        status = fields[index]
        index += 1
        if status.startswith("R"):
            if index + 1 >= len(fields):
                raise ValueError("Git returned an incomplete rename record")
            moves.append((fields[index], fields[index + 1]))
            index += 2
        elif status == "M" and is_build_file(fields[index]):
            build_edits.append(fields[index])
            index += 1
        elif status == "M" and is_source_file(fields[index]):
            # An unmoved source file may change only include lines that point at moved files.
            include_edits.append(fields[index])
            index += 1
        elif status == "M" and fields[index] == DEPENDENCY_BASELINE:
            # The dependency baseline may only lose entries in a move-only PR.
            baseline_edits.append(fields[index])
            index += 1
        elif status == "M" and fields[index] == FILE_SIZE_BASELINE:
            # File-size metadata may only transfer existing caps along proven renames.
            file_size_baseline_edits.append(fields[index])
            index += 1
        else:
            raise ValueError(
                f"{status} {fields[index]}: changed files must be renames, build-file path entries, "
                "or source files whose only edits are include paths of moved files, "
                "a dependency baseline with entries removed, or a file-size baseline path rewrite"
            )
    if not moves:
        raise ValueError("no renamed files found")
    return moves, build_edits, include_edits, baseline_edits, file_size_baseline_edits


def changed_hunks(root, base_ref, head_ref, old_path, new_path):
    diff = git(
        root,
        "diff",
        "--no-ext-diff",
        "--no-color",
        "--unified=0",
        "--find-renames=20%",
        f"{base_ref}...{head_ref}",
        "--",
        old_path,
        new_path,
    ).decode("utf-8", errors="surrogateescape")
    hunks = []
    current = None
    for line in diff.splitlines():
        header = HUNK_HEADER.match(line)
        if header:
            if current is not None:
                hunks.append(current)
            current = [[], []]
            old_number = int(header.group(1))
            new_number = int(header.group(2))
        elif current is None:
            continue
        elif line == "\\ No newline at end of file":
            raise ValueError(f"{new_path}: final newline changed")
        elif line.startswith("+"):
            current[1].append((new_number, line[1:]))
            new_number += 1
        elif line.startswith("-"):
            current[0].append((old_number, line[1:]))
            old_number += 1
        elif line.startswith(" "):
            old_number += 1
            new_number += 1
    if current is not None:
        hunks.append(current)
    return hunks


def file_mode(root, ref, path):
    record = git(root, "ls-tree", "-z", ref, "--", path).split(b"\0", 1)[0]
    if not record:
        raise ValueError(f"could not read file metadata for {path} at {ref}")
    metadata = record.split(b"\t", 1)[0].split()
    return metadata[0].decode("ascii")


def verify_rename_metadata(root, base_ref, head_ref, old_path, new_path):
    old_mode = file_mode(root, base_ref, old_path)
    new_mode = file_mode(root, head_ref, new_path)
    if old_mode != new_mode:
        raise ValueError(f"{new_path}: file mode changed from {old_mode} to {new_mode}")

    numstat = git(
        root,
        "diff",
        "--numstat",
        "--find-renames=20%",
        f"{base_ref}...{head_ref}",
        "--",
        old_path,
        new_path,
    )
    if any(line.startswith(b"-\t-\t") for line in numstat.splitlines()):
        raise ValueError(f"{new_path}: binary content changed during the rename")


def normalized_repo_path(path):
    return posixpath.normpath(path.replace("\\", "/"))


def include_resolves_to(include_path, source_path, moved_path):
    include_path = include_path.replace("\\", "/")
    if include_path.startswith("/") or re.match(r"^[A-Za-z]:", include_path):
        return False
    moved_path = normalized_repo_path(moved_path)
    source_directory = posixpath.dirname(source_path)
    candidates = {
        normalized_repo_path(include_path),
        normalized_repo_path(posixpath.join(source_directory, include_path)),
    }
    if moved_path.startswith("src/"):
        candidates.add(normalized_repo_path(posixpath.join("src", include_path)))
        direct_child = moved_path.split("/", 2)[1]
        candidates.add(normalized_repo_path(posixpath.join("src", direct_child, include_path)))
    # Public include tree: src/<system>/include/<path> is included as <path>.
    if "/include/" in moved_path and include_path == moved_path.split("/include/", 1)[1]:
        return True
    return moved_path in candidates


def include_path_maps(old_include, new_include, old_source, new_source, moves):
    matches = [
        (moved_old, moved_new)
        for moved_old, moved_new in moves
        if include_resolves_to(old_include, old_source, moved_old)
        and include_resolves_to(new_include, new_source, moved_new)
    ]
    return len(matches) == 1


def skip_cmake_quoted_or_bracketed(text, index):
    if text[index] == '"':
        escaped = False
        index += 1
        while index < len(text):
            if escaped:
                escaped = False
            elif text[index] == "\\":
                escaped = True
            elif text[index] == '"':
                return index + 1
            index += 1
        return len(text)
    bracket = re.match(r"\[(=*)\[", text[index:])
    if bracket:
        closing = "]" + bracket.group(1) + "]"
        end = text.find(closing, index + bracket.end())
        return len(text) if end < 0 else end + len(closing)
    return None


def cmake_statements(text):
    index = 0
    while index < len(text):
        character = text[index]
        if character.isspace() or character == ";":
            index += 1
            continue
        if character == "#":
            bracket_end = skip_cmake_quoted_or_bracketed(text, index + 1)
            if bracket_end is not None:
                index = bracket_end
            else:
                newline = text.find("\n", index)
                index = len(text) if newline < 0 else newline + 1
            continue
        skipped = skip_cmake_quoted_or_bracketed(text, index)
        if skipped is not None:
            index = skipped
            continue

        command = re.match(r"[A-Za-z_]\w*", text[index:])
        if not command:
            index += 1
            continue
        name = command.group(0).lower()
        cursor = index + command.end()
        while cursor < len(text) and text[cursor].isspace():
            cursor += 1
        if cursor >= len(text) or text[cursor] != "(":
            index += command.end()
            continue

        start = index
        depth = 0
        quote = False
        escaped = False
        arguments_start = cursor + 1
        while cursor < len(text):
            character = text[cursor]
            if quote:
                if escaped:
                    escaped = False
                elif character == "\\":
                    escaped = True
                elif character == '"':
                    quote = False
                cursor += 1
                continue
            if character == "#":
                bracket_end = skip_cmake_quoted_or_bracketed(text, cursor + 1)
                if bracket_end is not None:
                    cursor = bracket_end
                    continue
                newline = text.find("\n", cursor)
                cursor = len(text) if newline < 0 else newline + 1
                continue
            skipped = skip_cmake_quoted_or_bracketed(text, cursor)
            if skipped is not None:
                if character == '"':
                    quote = True
                    cursor += 1
                else:
                    cursor = skipped
                continue
            if character == "(":
                depth += 1
            elif character == ")":
                depth -= 1
                if depth == 0:
                    end = cursor + 1
                    yield name, text[arguments_start:cursor], start, end
                    index = end
                    break
            cursor += 1
        else:
            return


def cmake_source_entry_lines(text):
    lines = set()
    for command, arguments, start, end in cmake_statements(text):
        allowed = command in CMAKE_SOURCE_COMMANDS
        if command == "set":
            variable = CMAKE_SOURCE_VARIABLE.match(arguments)
            allowed = bool(
                variable
                and re.search(r"(SOURCES|FILES|HEADERS|RESOURCES|INCLUDES)$", variable.group(1), re.I)
            )
        elif command == "file":
            allowed = bool(re.search(r"\bGLOB(?:_RECURSE)?\b", arguments, re.I))
        if allowed:
            first_line = text.count("\n", 0, start) + 1
            last_line = text.count("\n", 0, end) + 1
            lines.update(range(first_line, last_line + 1))
    return lines


def cmake_include_directory_lines(text):
    """Return lines inside target_include_directories commands."""
    lines = set()
    for command, _arguments, start, end in cmake_statements(text):
        if command == "target_include_directories":
            first_line = text.count("\n", 0, start) + 1
            last_line = text.count("\n", 0, end) + 1
            lines.update(range(first_line, last_line + 1))
    return lines


def path_is_under_root(path, root):
    """Test a repository-relative path against a normalized CMake source root."""
    root = posixpath.normpath(root)
    if root == ".":
        return not path.startswith(("../", "/"))
    return path.startswith(root.rstrip("/") + "/")


def is_cmake_include_root_rewrite(
    old_line,
    new_line,
    old_line_number,
    new_line_number,
    old_build_file,
    old_text,
    new_text,
    moves,
):
    """Allow a target include root to follow a moved public header tree."""
    if old_line_number not in cmake_include_directory_lines(old_text) or (
        new_line_number not in cmake_include_directory_lines(new_text)
    ):
        return False

    path_token = re.compile(r"(?P<prefix>\$\{CMAKE_CURRENT_SOURCE_DIR\}/)(?P<path>[^\s\"()]+)")
    old_match = path_token.search(old_line)
    new_match = path_token.search(new_line)
    if not old_match or not new_match:
        return False
    if old_line[:old_match.start()] != new_line[:new_match.start()] or (
        old_line[old_match.end():] != new_line[new_match.end():]
    ):
        return False

    cmake_directory = posixpath.dirname(old_build_file) or "."
    old_root = posixpath.normpath(
        posixpath.join(cmake_directory, old_match.group("path").replace("\\", "/"))
    )
    new_root = posixpath.normpath(
        posixpath.join(cmake_directory, new_match.group("path").replace("\\", "/"))
    )
    if old_root == new_root or posixpath.basename(new_root) != "include":
        return False
    return any(
        path_is_under_root(old_path, old_root) and path_is_under_root(new_path, new_root)
        for old_path, new_path in moves
    )


def is_build_entry(path, line_number, line, text):
    if not is_build_file(path):
        return False
    if path.endswith((".cmake", "CMakeLists.txt")):
        return line_number in cmake_source_entry_lines(text)
    if path.endswith((".vcxproj", ".vcxproj.filters", ".props", ".targets")):
        return bool(re.search(r"<(?:ClCompile|ClInclude|None)\b[^>]*\bInclude=", line))
    if PurePosixPath(path).name == "CMakePresets.json":
        return bool(re.search(r'"sourceDir"\s*:', line))
    if path.endswith(".sln"):
        return line.lstrip().startswith("Project(") and ".vcxproj" in line.lower()
    return False


def build_path_variants(old_build_file, new_build_file, moves):
    pairs = []
    old_build_dir = posixpath.dirname(old_build_file) or "."
    new_build_dir = posixpath.dirname(new_build_file) or "."
    for old_path, new_path in moves:
        candidates = (
            (old_path, new_path),
            (old_path.replace("/", "\\"), new_path.replace("/", "\\")),
            ("./" + old_path, "./" + new_path),
            (
                posixpath.relpath(old_path, old_build_dir),
                posixpath.relpath(new_path, new_build_dir),
            ),
        )
        for old_value, new_value in candidates:
            if old_value != new_value and ("/" in old_value or "\\" in old_value):
                pairs.append((old_value, new_value))
    return sorted(set(pairs), key=lambda pair: len(pair[0]), reverse=True)


def replace_path_token(line, old_path, new_path):
    pattern = re.compile(re.escape(old_path))

    def replace_if_full_path(match):
        before = line[match.start() - 1] if match.start() else ""
        after = line[match.end()] if match.end() < len(line) else ""
        if (
            before.isalnum()
            or (before and before in PATH_TOKEN_CHARS)
            or after.isalnum()
            or (after and after in PATH_TOKEN_CHARS)
        ):
            return match.group(0)
        return new_path

    return pattern.sub(replace_if_full_path, line)


def is_build_path_rewrite(
    old_line,
    new_line,
    old_build_file,
    new_build_file,
    old_line_number,
    new_line_number,
    old_text,
    new_text,
    moves,
):
    if not is_build_entry(old_build_file, old_line_number, old_line, old_text) or not is_build_entry(
        new_build_file, new_line_number, new_line, new_text
    ):
        return False
    return any(
        old_path in old_line
        and replace_path_token(old_line, old_path, new_path) == new_line
        for old_path, new_path in build_path_variants(old_build_file, new_build_file, moves)
    )


def validate_pair(
    old_line_number,
    old_line,
    new_line_number,
    new_line,
    old_file,
    new_file,
    old_text,
    new_text,
    moves,
):
    old_include = INCLUDE_LINE.fullmatch(old_line)
    new_include = INCLUDE_LINE.fullmatch(new_line)
    if old_include and new_include:
        # The delimiter may change from "..." to <...> when a moved header joins the public include
        # tree; the directive and any trailing comment must stay exactly the same.
        fixed_parts_match = (old_include.group(1), old_include.group(5)) == (
            new_include.group(1), new_include.group(5)
        )
        if fixed_parts_match and include_path_maps(
            old_include.group(3), new_include.group(3), old_file, new_file, moves
        ):
            return
        raise ValueError(f"{new_file}: include edit does not resolve to the same moved file")
    if is_build_path_rewrite(
        old_line,
        new_line,
        old_file,
        new_file,
        old_line_number,
        new_line_number,
        old_text,
        new_text,
        moves,
    ):
        return
    raise ValueError(f"{new_file}: edit is not an include or moved-file build-entry path rewrite")


def baseline_entry_keys(text, moves=()):
    document = json.loads(text)
    entries = document.get("violations", document) if isinstance(document, dict) else document
    old_to_new = dict(moves)
    normalized = []
    for entry in entries:
        normalized_entry = dict(entry)
        if normalized_entry.get("file") in old_to_new:
            normalized_entry["file"] = old_to_new[normalized_entry["file"]]
        normalized.append(json.dumps(normalized_entry, sort_keys=True))
    return Counter(normalized)


def check_baseline_only_shrinks(root, base_ref, head_ref, path, moves):
    old = baseline_entry_keys(git_text(root, base_ref, path), moves)
    new = baseline_entry_keys(git_text(root, head_ref, path))
    added = new - old
    if added:
        raise ValueError(f"{path}: a move-only PR may remove baseline entries but not add them")


def check_file_size_baseline_moves(root, base_ref, head_ref, path, moves):
    old = json.loads(git_text(root, base_ref, path))
    new = json.loads(git_text(root, head_ref, path))
    if not isinstance(old, dict) or not isinstance(new, dict):
        raise ValueError(f"{path}: expected a path-to-cap JSON object")

    expected = dict(old)
    for old_path, new_path in moves:
        if old_path not in old:
            continue
        if new_path in expected and new_path != old_path:
            raise ValueError(f"{path}: rename would collide with existing baseline path {new_path}")
        expected.pop(old_path)
        expected[new_path] = old[old_path]

    if json.dumps(new, sort_keys=True) != json.dumps(expected, sort_keys=True):
        raise ValueError(
            f"{path}: entries may only be rewritten from proven rename paths, with caps unchanged"
        )


def check_move_only(root, base_ref, head_ref):
    moves, build_edits, include_edits, baseline_edits, file_size_baseline_edits = (
        classify_changed_files(root, base_ref, head_ref)
    )
    details = []
    for old_path, new_path in moves:
        verify_rename_metadata(root, base_ref, head_ref, old_path, new_path)
        old_text = git_text(root, base_ref, old_path) if is_build_file(old_path) else ""
        new_text = git_text(root, head_ref, new_path) if is_build_file(new_path) else ""
        for removed, added in changed_hunks(root, base_ref, head_ref, old_path, new_path):
            if len(removed) != len(added):
                raise ValueError(f"{new_path}: inserted or deleted lines are not allowed")
            for (old_line_number, old_line), (new_line_number, new_line) in zip(removed, added):
                validate_pair(
                    old_line_number,
                    old_line,
                    new_line_number,
                    new_line,
                    old_path,
                    new_path,
                    old_text,
                    new_text,
                    moves,
                )
        details.append(f"{old_path} -> {new_path}")

    for include_path in include_edits:
        for removed, added in changed_hunks(root, base_ref, head_ref, include_path, include_path):
            if len(removed) != len(added):
                raise ValueError(f"{include_path}: inserted or deleted lines are not allowed")
            for (old_line_number, old_line), (new_line_number, new_line) in zip(removed, added):
                validate_pair(
                    old_line_number,
                    old_line,
                    new_line_number,
                    new_line,
                    include_path,
                    include_path,
                    "",
                    "",
                    moves,
                )

    for baseline_path in baseline_edits:
        check_baseline_only_shrinks(root, base_ref, head_ref, baseline_path, moves)

    for baseline_path in file_size_baseline_edits:
        check_file_size_baseline_moves(root, base_ref, head_ref, baseline_path, moves)

    for build_path in build_edits:
        old_text = git_text(root, base_ref, build_path)
        new_text = git_text(root, head_ref, build_path)
        for removed, added in changed_hunks(root, base_ref, head_ref, build_path, build_path):
            if len(removed) != len(added):
                raise ValueError(f"{build_path}: inserted or deleted build entries are not allowed")
            for (old_line_number, old_line), (new_line_number, new_line) in zip(removed, added):
                if not is_build_path_rewrite(
                    old_line,
                    new_line,
                    build_path,
                    build_path,
                    old_line_number,
                    new_line_number,
                    old_text,
                    new_text,
                    moves,
                ) and not is_cmake_include_root_rewrite(
                    old_line,
                    new_line,
                    old_line_number,
                    new_line_number,
                    build_path,
                    old_text,
                    new_text,
                    moves,
                ):
                    raise ValueError(f"{build_path}: edit is not a moved-file build entry path rewrite")
    return details


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base-ref", required=True, help="base commit or ref")
    parser.add_argument("--head-ref", default="HEAD", help="PR head commit or ref (default: HEAD)")
    parser.add_argument("--root", default=".", help="Git checkout root (default: current directory)")
    args = parser.parse_args()
    try:
        moves = check_move_only(args.root, args.base_ref, args.head_ref)
    except (ValueError, subprocess.CalledProcessError) as error:
        detail = (
            error.stderr.decode("utf-8", errors="replace").strip()
            if isinstance(error, subprocess.CalledProcessError)
            else str(error)
        )
        print(f"Move-only proof failed: {detail}", file=sys.stderr)
        return 1
    print("Move-only proof passed:")
    for move in moves:
        print(f"  {move}")
    print("Build, tests, and the architecture dependency ratchet remain required PR checks.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
