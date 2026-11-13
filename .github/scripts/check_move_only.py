#!/usr/bin/env python3
"""Prove that a `refactor: move-only` PR contains renames and path rewrites only."""

import argparse
import re
import subprocess
import sys
from pathlib import PurePosixPath


BUILD_FILE_NAMES = {"CMakeLists.txt", "CMakePresets.json", "Makefile", "meson.build"}
BUILD_FILE_SUFFIXES = (".cmake", ".vcxproj", ".vcxproj.filters", ".props", ".targets", ".sln")
INCLUDE_LINE = re.compile(r'^(\s*#\s*include\s*)([<"])(.*)([>"])(\s*(?://.*|/\*.*\*/)?\s*)$')


def git(root, *args):
    return subprocess.run(
        ["git", "-C", str(root), *args], check=True, capture_output=True
    ).stdout


def classify_changed_files(root, base_ref, head_ref):
    fields = git(root, "diff", "--name-status", "-z", "--find-renames=20%",
                 f"{base_ref}...{head_ref}").decode("utf-8", errors="surrogateescape").split("\0")
    fields = [field for field in fields if field]
    result = []
    build_edits = []
    index = 0
    while index < len(fields):
        status = fields[index]
        index += 1
        if status.startswith("R"):
            if index + 1 >= len(fields):
                raise ValueError("Git returned an incomplete rename record")
            result.append((fields[index], fields[index + 1]))
            index += 2
        elif status == "M" and is_build_file(fields[index]):
            build_edits.append(fields[index])
            index += 1
        else:
            raise ValueError(f"{status} {fields[index]}: changed files must be renames or build-file path entries")
    if not result:
        raise ValueError("no renamed files found")
    return result, build_edits


def diff_changes(root, base_ref, head_ref, old_path, new_path):
    diff = git(root, "diff", "--no-ext-diff", "--no-color", "--unified=0", "--find-renames=20%",
               f"{base_ref}...{head_ref}", "--", old_path, new_path).decode(
                   "utf-8", errors="surrogateescape")
    hunks = []
    current = None
    for line in diff.splitlines():
        if line.startswith("@@"):
            if current is not None:
                hunks.append(current)
            current = [[], []]
        elif current is not None and line.startswith("+") and not line.startswith("+++"):
            current[1].append(line[1:])
        elif current is not None and line.startswith("-") and not line.startswith("---"):
            current[0].append(line[1:])
    if current is not None:
        hunks.append(current)
    return hunks


def is_build_file(path):
    name = PurePosixPath(path).name
    return name in BUILD_FILE_NAMES or path.endswith(BUILD_FILE_SUFFIXES)


def rename_path_variants(moves):
    pairs = []
    for old, new in moves:
        for old_value, new_value in ((old, new), (old.replace("/", "\\"), new.replace("/", "\\")),
                                     (PurePosixPath(old).name, PurePosixPath(new).name)):
            if old_value != new_value:
                pairs.append((old_value, new_value))
    return sorted(set(pairs), key=lambda pair: len(pair[0]), reverse=True)


def is_path_rewrite(old_line, new_line, moves):
    for old_path, new_path in rename_path_variants(moves):
        if old_path in old_line and old_line.replace(old_path, new_path) == new_line:
            return True
    return False


def validate_pair(old_line, new_line, source_path, moves):
    old_include = INCLUDE_LINE.fullmatch(old_line)
    new_include = INCLUDE_LINE.fullmatch(new_line)
    if old_include and new_include:
        if (old_include.group(1), old_include.group(2), old_include.group(4), old_include.group(5)) == (
                new_include.group(1), new_include.group(2), new_include.group(4), new_include.group(5)):
            if is_path_rewrite(old_include.group(3), new_include.group(3), moves):
                return
            for old_path, new_path in moves:
                if (PurePosixPath(old_path).name == PurePosixPath(new_path).name
                        == PurePosixPath(old_include.group(3)).name
                        == PurePosixPath(new_include.group(3)).name
                        and old_include.group(3) != new_include.group(3)):
                    return
    if is_build_file(source_path) and is_path_rewrite(old_line, new_line, moves):
        return
    raise ValueError(f"{source_path}: edit is not an include or build-file path rewrite: {old_line!r} -> {new_line!r}")


def check_move_only(root, base_ref, head_ref):
    moves, build_edits = classify_changed_files(root, base_ref, head_ref)
    details = []
    for old_path, new_path in moves:
        for removed, added in diff_changes(root, base_ref, head_ref, old_path, new_path):
            if len(removed) != len(added):
                raise ValueError(f"{new_path}: inserted or deleted lines are not allowed")
            for old_line, new_line in zip(removed, added):
                validate_pair(old_line, new_line, new_path, moves)
        details.append(f"{old_path} -> {new_path}")
    for build_path in build_edits:
        for removed, added in diff_changes(root, base_ref, head_ref, build_path, build_path):
            if len(removed) != len(added):
                raise ValueError(f"{build_path}: inserted or deleted build entries are not allowed")
            for old_line, new_line in zip(removed, added):
                if not is_path_rewrite(old_line, new_line, moves):
                    raise ValueError(f"{build_path}: edit is not a moved-path entry: {old_line!r} -> {new_line!r}")
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
        detail = error.stderr.decode("utf-8", errors="replace").strip() if isinstance(error, subprocess.CalledProcessError) else str(error)
        print(f"Move-only proof failed: {detail}", file=sys.stderr)
        return 1
    print("Move-only proof passed:")
    for move in moves:
        print(f"  {move}")
    print("Build, tests, and the architecture dependency ratchet remain required PR checks.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
