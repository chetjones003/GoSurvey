#!/usr/bin/env python3
"""Prove that a `refactor: split-only` PR moves code between files and changes nothing else.

ADR-0012, Track A. The PR may move whole lines into other files. Every removed line must reappear
verbatim in an added line. The only added lines that are not moved lines must be includes, forward
declarations or CMake source entries. The file-size baseline may only shrink. Blank lines are not
compared, because they cannot change behaviour.
"""

import argparse
import json
import re
import subprocess
import sys
from collections import Counter

import check_move_only as move_proof


FILE_SIZE_BASELINE = move_proof.FILE_SIZE_BASELINE
INCLUDE_DIRECTIVE = re.compile(r"^\s*#\s*include\b")
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


def is_allowed_addition(root, head_ref, path, line_number, text, head_texts):
    """Return True when an added line that is not a moved line is allowed in this PR."""
    if move_proof.is_build_file(path):
        if path not in head_texts:
            head_texts[path] = move_proof.git_text(root, head_ref, path)
        return move_proof.is_build_entry(path, line_number, text, head_texts[path])
    return bool(
        INCLUDE_DIRECTIVE.match(text)
        or FORWARD_DECLARATION.match(text)
        or is_function_declaration(text)
    )


def check_file_size_baseline(root, base_ref, head_ref):
    old = json.loads(move_proof.git_text(root, base_ref, FILE_SIZE_BASELINE))
    new = json.loads(move_proof.git_text(root, head_ref, FILE_SIZE_BASELINE))
    if not isinstance(old, dict) or not isinstance(new, dict):
        raise ValueError(f"{FILE_SIZE_BASELINE}: expected a path-to-cap JSON object")
    for path, cap in new.items():
        if path not in old:
            raise ValueError(f"{FILE_SIZE_BASELINE}: a split-only PR may not add an entry for {path}")
        if cap > old[path]:
            raise ValueError(
                f"{FILE_SIZE_BASELINE}: cap for {path} grew from {old[path]} to {cap}"
            )


def check_split_only(root, base_ref, head_ref):
    changes = changed_files(root, base_ref, head_ref)
    reject_binary_changes(root, base_ref, head_ref)

    removed = Counter()
    added = []  # (path, new line number, text)
    details = []
    baseline_edited = False
    for status, path in changes:
        if path == FILE_SIZE_BASELINE:
            if status != "M":
                raise ValueError(f"{path}: the file-size baseline may only be modified")
            baseline_edited = True
            continue
        hunks = move_proof.changed_hunks(root, base_ref, head_ref, path, path)
        for hunk_removed, hunk_added in hunks:
            removed.update(text for _, text in hunk_removed if text.strip())
            added.extend(
                (path, number, text) for number, text in hunk_added if text.strip()
            )
        details.append(f"{status} {path}")

    if not removed:
        raise ValueError("no moved lines found")

    added_counts = Counter(text for _, _, text in added)
    missing = removed - added_counts
    if missing:
        sample = next(iter(missing))
        raise ValueError(
            f"{sum(missing.values())} removed line(s) do not reappear verbatim, "
            f"for example: {sample!r}"
        )

    # Each removed line matches one added line with the same text. Added lines beyond those
    # matches are new code, and each must be an allowed kind.
    budget = removed.copy()
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

    if baseline_edited:
        check_file_size_baseline(root, base_ref, head_ref)

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
