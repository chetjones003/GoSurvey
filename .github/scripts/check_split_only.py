#!/usr/bin/env python3
"""Prove that a `refactor: split-only` PR moves code between files and changes nothing else.

ADR-0012, Track A. Every removed line must reappear verbatim in an added line of a different file,
so a move within one file (a reorder) is rejected. The only added lines that are not moved lines
must be includes, forward declarations, function declarations, or CMake source entries. The
file-size baseline may only shrink. Blank lines are not compared, because they cannot change
behaviour.
"""

import argparse
import json
import re
import subprocess
import sys
from collections import Counter

import check_move_only as move_proof


FILE_SIZE_BASELINE = move_proof.FILE_SIZE_BASELINE
CMAKE_FILE_NAMES = ("CMakeLists.txt",)
CMAKE_FILE_SUFFIXES = (".cmake",)
CMAKE_SOURCE_COMMANDS = {"target_sources", "add_library", "add_executable"}
SOURCE_LIST_VARIABLE = re.compile(r"(SOURCES|FILES|HEADERS|RESOURCES|INCLUDES)$", re.IGNORECASE)
CMAKE_OPENER = re.compile(
    r"^\s*(?:set|add_library|add_executable|target_sources)\(\s*[\w.-]+"
    r"(?:\s+(?:PRIVATE|PUBLIC|INTERFACE))?\s*$"
)
SOURCE_PATH_TOKEN = re.compile(r"\S+\.(?:c|cc|cpp|cxx|h|hh|hpp|hxx)\b")
INCLUDE_DIRECTIVE = re.compile(r"^\s*#\s*include\b")
NAMESPACE_OPEN = re.compile(r"^\s*(?:inline\s+)?namespace\s*(?:[\w:]+\s*)?\{\s*(?://.*)?$")
NAMESPACE_CLOSE = re.compile(r"^\s*\}\s*//\s*namespace\b.*$")
FORWARD_DECLARATION = re.compile(
    r"^\s*(?:template\s*<[^>]*>\s*)?"
    r"(?:(?:class|struct|union)\s+\w+|enum\s+(?:class\s+|struct\s+)?\w+(?:\s*:\s*[\w:\s]+)?)"
    r"\s*;\s*$"
)
FUNCTION_DECLARATION = re.compile(
    r"^\s*(?:(?:static|inline|extern|constexpr)\s+)*[A-Za-z_][\w:<>,\s*&]*?\s[*&]*[A-Za-z_~][\w:]*"
    r"\s*\([^;{}()]*\)\s*(?:const\s*)?;\s*$"
)
# A statement such as `return f(x);` looks like a declaration, so these leading words exclude it.
STATEMENT_KEYWORDS = {
    "return", "throw", "co_return", "co_yield", "else", "case", "goto", "delete", "new", "using",
    "typedef", "do",
}


def changed_files(root, base_ref, head_ref):
    fields = move_proof.git(
        root, "diff", "--name-status", "-z", "--no-renames", f"{base_ref}...{head_ref}"
    ).decode("utf-8", errors="surrogateescape").split("\0")
    fields = [field for field in fields if field]
    if len(fields) % 2:
        raise ValueError("Git returned an incomplete change record")
    changes = list(zip(fields[0::2], fields[1::2]))
    for status, path in changes:
        if status not in {"A", "M", "D"}:
            raise ValueError(f"{status} {path}: split-only changes may only add, modify or delete files")
    return changes


def reject_binary_changes(root, base_ref, head_ref):
    numstat = move_proof.git(root, "diff", "--numstat", "--no-renames", f"{base_ref}...{head_ref}")
    for line in numstat.splitlines():
        if line.startswith(b"-\t-\t"):
            path = line.split(b"\t", 2)[2].decode("utf-8", errors="surrogateescape")
            raise ValueError(f"{path}: binary content changed")


def is_function_declaration(text):
    first_word = re.match(r"\s*(\w+)", text)
    if not first_word or first_word.group(1) in STATEMENT_KEYWORDS:
        return False
    return bool(FUNCTION_DECLARATION.match(text))


def is_cmake_file(path):
    name = path.rsplit("/", 1)[-1]
    return name in CMAKE_FILE_NAMES or path.endswith(CMAKE_FILE_SUFFIXES)


def is_cmake_source_entry(text_of_file, line_number, text):
    """True when the line is a source path, or the bare opener, of a source-list statement.

    The opener is the first line of a multi-line call, such as `set(APP_SOURCES` or
    `add_library(app`. It is accepted only when it has no keyword after the target or variable.
    """
    is_source_path = bool(SOURCE_PATH_TOKEN.search(text))
    is_opener = bool(CMAKE_OPENER.match(text))
    is_closer = text.strip() == ")"
    if not (is_source_path or is_opener or is_closer):
        return False
    for name, arguments, start, end in move_proof.cmake_statements(text_of_file):
        if name == "set":
            variable = move_proof.CMAKE_SOURCE_VARIABLE.match(arguments)
            if not (variable and SOURCE_LIST_VARIABLE.search(variable.group(1))):
                continue
        elif name not in CMAKE_SOURCE_COMMANDS:
            continue
        first_line = text_of_file.count("\n", 0, start) + 1
        last_line = text_of_file.count("\n", 0, end) + 1
        if not first_line <= line_number <= last_line:
            continue
        if is_source_path or line_number == first_line or (is_closer and line_number == last_line):
            return True
    return False


def is_allowed_addition(root, head_ref, path, line_number, text, head_texts):
    """Return True when an added line that is not a moved line is allowed in this PR."""
    if is_cmake_file(path):
        if path not in head_texts:
            head_texts[path] = move_proof.git_text(root, head_ref, path)
        return is_cmake_source_entry(head_texts[path], line_number, text)
    if move_proof.is_build_file(path):
        return False
    return bool(
        INCLUDE_DIRECTIVE.match(text)
        or FORWARD_DECLARATION.match(text)
        or is_function_declaration(text)
        or NAMESPACE_OPEN.match(text)
        or NAMESPACE_CLOSE.match(text)
    )


def check_moves_leave_their_file(removed_by_path, added_by_path, added_total):
    """A removed line must reappear in a different file, not be reordered within its own file."""
    for path, removed in removed_by_path.items():
        for text, count in removed.items():
            elsewhere = added_total[text] - added_by_path[path][text]
            if count > elsewhere:
                raise ValueError(
                    f"{path}: removed line is not moved to another file (reorder within a file "
                    f"is not allowed): {text!r}"
                )


def check_file_size_baseline(root, base_ref, head_ref):
    old = json.loads(move_proof.git_text(root, base_ref, FILE_SIZE_BASELINE))
    new = json.loads(move_proof.git_text(root, head_ref, FILE_SIZE_BASELINE))
    if not isinstance(old, dict) or not isinstance(new, dict):
        raise ValueError(f"{FILE_SIZE_BASELINE}: expected a path-to-cap JSON object")
    for path, cap in new.items():
        if not isinstance(cap, int) or isinstance(cap, bool):
            raise ValueError(f"{FILE_SIZE_BASELINE}: cap for {path} is not an integer: {cap!r}")
        if path not in old:
            raise ValueError(f"{FILE_SIZE_BASELINE}: a split-only PR may not add an entry for {path}")
        if cap > old[path]:
            raise ValueError(
                f"{FILE_SIZE_BASELINE}: cap for {path} grew from {old[path]} to {cap}"
            )


def check_split_only(root, base_ref, head_ref):
    changes = changed_files(root, base_ref, head_ref)
    reject_binary_changes(root, base_ref, head_ref)

    removed_by_path = {}
    added = []  # (path, new line number, text)
    details = []
    baseline_status = None
    for status, path in changes:
        if path == FILE_SIZE_BASELINE:
            # Deleting the baseline is allowed: it only removes entries, which is shrink-only.
            baseline_status = status
            continue
        removed_by_path[path] = Counter()
        for hunk_removed, hunk_added in move_proof.changed_hunks(root, base_ref, head_ref, path, path):
            removed_by_path[path].update(text for _, text in hunk_removed if text.strip())
            added.extend(
                (path, number, text) for number, text in hunk_added if text.strip()
            )
        details.append(f"{status} {path}")

    removed_total = Counter()
    for removed in removed_by_path.values():
        removed_total.update(removed)
    if not removed_total:
        raise ValueError("no moved lines found")

    added_by_path = {path: Counter() for path in removed_by_path}
    for path, _number, text in added:
        added_by_path[path][text] += 1
    added_total = Counter(text for _, _, text in added)

    missing = removed_total - added_total
    if missing:
        sample = next(iter(missing))
        raise ValueError(
            f"{sum(missing.values())} removed line(s) do not reappear verbatim, "
            f"for example: {sample!r}"
        )
    check_moves_leave_their_file(removed_by_path, added_by_path, added_total)

    # Each removed line matches one added line with the same text. Added lines beyond those
    # matches are new code, and each must be an allowed kind.
    budget = removed_total.copy()
    head_texts = {}
    for path, number, text in added:
        if budget[text] > 0:
            budget[text] -= 1
            continue
        if not is_allowed_addition(root, head_ref, path, number, text, head_texts):
            raise ValueError(
                f"{path}:{number}: added line is not an include, forward declaration, "
                f"CMake source entry or moved line: {text!r}"
            )

    if baseline_status == "M":
        check_file_size_baseline(root, base_ref, head_ref)
    elif baseline_status == "A":
        raise ValueError(f"{FILE_SIZE_BASELINE}: a split-only PR may not add the file-size baseline")

    return details


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base-ref", required=True, help="base commit or ref")
    parser.add_argument("--head-ref", default="HEAD", help="PR head commit or ref (default: HEAD)")
    parser.add_argument("--root", default=".", help="Git checkout root (default: current directory)")
    args = parser.parse_args()
    try:
        details = check_split_only(args.root, args.base_ref, args.head_ref)
    except (ValueError, subprocess.CalledProcessError, json.JSONDecodeError) as error:
        detail = (
            error.stderr.decode("utf-8", errors="replace").strip()
            if isinstance(error, subprocess.CalledProcessError)
            else str(error)
        )
        print(f"Split-only proof failed: {detail}", file=sys.stderr)
        return 1
    print("Split-only proof passed:")
    for detail in details:
        print(f"  {detail}")
    print("Build, tests, and the architecture dependency ratchet remain required PR checks.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
