#!/usr/bin/env python3
"""File-size gate for C/C++ sources (issue #1213).

New source files over LIMIT lines fail. Files already over LIMIT on beta are grandfathered in
BASELINE_PATH with their line counts. A grandfathered file may only shrink: the baseline entry must
be lowered to the new count, or removed once the file is at or under LIMIT. The PR's baseline can
never gain entries or raise a value over the base branch's baseline.
"""

import argparse
import json
import subprocess
import sys
from pathlib import Path

LIMIT = 3000
BASELINE_PATH = ".github/file-size-baseline.json"
SOURCE_SUFFIXES = (".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx")
EXCLUDED_PREFIXES = ("third_party/",)


def find_violations(current, baseline, base_baseline):
    """Return human-readable violations.

    current: {path: line_count} for every in-scope source file.
    baseline: the PR's grandfather map {path: line_count}.
    base_baseline: the base branch's map, or None when the base has no baseline yet (bootstrap).
    """
    problems = []

    if base_baseline is not None:
        for path, value in sorted(baseline.items()):
            if path not in base_baseline:
                problems.append(f"{path}: baseline entry added; the baseline may only shrink")
            elif value > base_baseline[path]:
                problems.append(
                    f"{path}: baseline raised from {base_baseline[path]} to {value}; "
                    "the baseline may only shrink")

    for path, lines in sorted(current.items()):
        if lines > LIMIT and path not in baseline:
            problems.append(f"{path}: {lines} lines exceeds {LIMIT} and is not grandfathered")
        elif lines > LIMIT and lines > baseline[path]:
            problems.append(f"{path}: grew from {baseline[path]} to {lines} lines (limit {LIMIT})")

    for path, value in sorted(baseline.items()):
        lines = current.get(path)
        if lines is None:
            problems.append(f"{path}: baseline entry for a file that no longer exists; remove it")
        elif lines <= LIMIT:
            problems.append(f"{path}: now {lines} lines, under {LIMIT}; remove its baseline entry")
        elif lines < value:
            problems.append(f"{path}: shrank from {value} to {lines}; lower its baseline entry to {lines}")

    return problems


def count_lines(text):
    """Count newline-delimited lines, like `wc -l` plus an unterminated last line."""
    return text.count("\n") + (1 if text and not text.endswith("\n") else 0)


def tracked_source_files(root):
    output = subprocess.run(
        ["git", "-C", str(root), "ls-files", "-z"],
        check=True, capture_output=True,
    ).stdout.decode("utf-8", errors="replace")
    return [
        path for path in output.split("\0")
        if path.lower().endswith(SOURCE_SUFFIXES) and not path.startswith(EXCLUDED_PREFIXES)
    ]


def collect_line_counts(root):
    root = Path(root)
    counts = {}
    for relative in tracked_source_files(root):
        text = (root / relative).read_bytes().decode("utf-8", errors="replace")
        counts[relative] = count_lines(text)
    return counts


def load_baseline_text(text):
    return {path: int(value) for path, value in json.loads(text).items()}


def read_baseline(root):
    path = Path(root) / BASELINE_PATH
    return load_baseline_text(path.read_text(encoding="utf-8"))


def read_base_baseline(root, base_ref):
    """Return the base branch baseline, or None when the base commit exists but has no baseline file.

    Fails closed: an unknown base commit is an error, not a missing baseline, so the cap cannot be
    silently dropped.
    """
    commit = subprocess.run(
        ["git", "-C", str(root), "cat-file", "-e", f"{base_ref}^{{commit}}"],
        capture_output=True,
    )
    if commit.returncode != 0:
        raise SystemExit(f"File-size gate: base commit {base_ref} is not available; fetch it first.")
    result = subprocess.run(
        ["git", "-C", str(root), "show", f"{base_ref}:{BASELINE_PATH}"],
        capture_output=True,
    )
    if result.returncode != 0:
        return None
    return load_baseline_text(result.stdout.decode("utf-8"))


def write_baseline(root, output):
    counts = collect_line_counts(root)
    grandfathered = {path: lines for path, lines in sorted(counts.items()) if lines > LIMIT}
    Path(output).write_text(json.dumps(grandfathered, indent=2) + "\n", encoding="utf-8")
    return grandfathered


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2], help="repository root")
    parser.add_argument("--base-ref", help="base commit whose baseline caps the PR's baseline")
    parser.add_argument("--write-baseline", type=Path, metavar="PATH",
                        help="write the grandfather baseline for the checked-out tree and exit")
    args = parser.parse_args(argv)

    if args.write_baseline:
        grandfathered = write_baseline(args.root, args.write_baseline)
        print(f"Wrote {len(grandfathered)} grandfathered file(s) to {args.write_baseline}")
        return 0

    current = collect_line_counts(args.root)
    baseline = read_baseline(args.root)
    base_baseline = read_base_baseline(args.root, args.base_ref) if args.base_ref else None
    if args.base_ref and base_baseline is None:
        print(f"Note: base {args.base_ref} has no {BASELINE_PATH}; bootstrap run, base cap not applied.")

    problems = find_violations(current, baseline, base_baseline)
    for problem in problems:
        print(f"::error::{problem}")
    if problems:
        print(f"File-size gate failed: {len(problems)} problem(s). Limit is {LIMIT} lines.")
        return 1
    print(f"File-size gate passed: {len(current)} source file(s), {len(baseline)} grandfathered.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
