#!/usr/bin/env python3
"""File-size gate for C/C++ sources (issue #1213).

New source files over LIMIT lines fail. Files already over LIMIT on beta are grandfathered in
BASELINE_PATH with their line counts. A grandfathered file may only shrink: the baseline entry must
be lowered to the new count, or removed once the file is at or under LIMIT. The PR's baseline can
never gain entries or raise a value over the base branch's baseline.
"""

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

LIMIT = 3000
BASELINE_PATH = ".github/file-size-baseline.json"
SOURCE_SUFFIXES = (".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".ipp", ".tpp", ".inl")
EXCLUDED_PREFIXES = ("third_party/",)


def find_violations(current, baseline, base_baseline, renamed_paths=()):
    """Return human-readable violations.

    current: {path: line_count} for every in-scope source file.
    baseline: the PR's grandfather map {path: line_count}.
    base_baseline: the base branch's cap map {path: line_count}. Before the baseline file exists on
    the base, this is derived from the base commit's oversized files (see base_oversized_caps).
    """
    problems = []
    # Transfer an existing cap only when git identifies a rename, so a move does not
    # create a new oversized-file exception or allow the cap to increase.
    transferred_caps = {
        new_path: base_baseline[old_path]
        for old_path, new_path in renamed_paths
        if old_path in base_baseline
        and new_path not in base_baseline
        and old_path not in current
        and baseline.get(new_path) == base_baseline[old_path]
    }
    transferred_old_paths = {
        old_path for old_path, new_path in renamed_paths if new_path in transferred_caps
    }

    for path, value in sorted(baseline.items()):
        if path not in base_baseline and path not in transferred_caps:
            problems.append(f"{path}: baseline entry added; the baseline may only shrink")
        elif path in base_baseline and value > base_baseline[path]:
            problems.append(
                f"{path}: baseline raised from {base_baseline[path]} to {value}; "
                "the baseline may only shrink")

    for path, lines in sorted(current.items()):
        if lines > LIMIT and path not in baseline:
            problems.append(f"{path}: {lines} lines exceeds {LIMIT} and is not grandfathered")
        elif lines > LIMIT and lines > baseline[path]:
            problems.append(f"{path}: grew from {baseline[path]} to {lines} lines (limit {LIMIT})")

    for path, value in sorted(baseline.items()):
        if path in transferred_old_paths:
            continue
        lines = current.get(path)
        if lines is None:
            problems.append(f"{path}: baseline entry for a file that no longer exists; remove it")
        elif lines <= LIMIT:
            problems.append(f"{path}: now {lines} lines, under {LIMIT}; remove its baseline entry")
        elif lines < value:
            problems.append(f"{path}: shrank from {value} to {lines}; lower its baseline entry to {lines}")

    return problems


def read_renamed_paths(root, base_ref):
    """Return source-file renames between base_ref and the checked-out tree."""
    result = subprocess.run(
        ["git", "-C", str(root), "diff", "--name-status", "--find-renames", base_ref, "--"],
        check=True, capture_output=True, text=True,
    )
    renamed = []
    for line in result.stdout.splitlines():
        fields = line.split("\t")
        if len(fields) == 3 and fields[0].startswith("R"):
            renamed.append((fields[1], fields[2]))
    return renamed


def count_lines(text):
    """Count lines with CRLF and bare CR as breaks, plus an unterminated last line."""
    text = text.replace("\r\n", "\n").replace("\r", "\n")
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


def read_base_text(root, base_ref, path):
    """Return the base commit's text for path, or None when the path is absent from that commit.

    `ls-tree` separates absent (empty output) from a failed lookup (non-zero exit), which must not be
    mistaken for absence.
    """
    listing = subprocess.run(
        ["git", "-C", str(root), "ls-tree", "-z", base_ref, "--", path],
        capture_output=True,
    )
    if listing.returncode != 0:
        raise SystemExit(f"File-size gate: could not read {path} at {base_ref}: "
                         f"{listing.stderr.decode('utf-8', errors='replace').strip()}")
    if not listing.stdout:
        return None
    blob = subprocess.run(
        ["git", "-C", str(root), "show", f"{base_ref}:{path}"],
        capture_output=True, check=True,
    )
    return blob.stdout.decode("utf-8", errors="replace")


def read_base_baseline(root, base_ref):
    """Return the base branch baseline, or None when the base commit has no baseline file."""
    commit = subprocess.run(
        ["git", "-C", str(root), "cat-file", "-e", f"{base_ref}^{{commit}}"],
        capture_output=True,
    )
    if commit.returncode != 0:
        raise SystemExit(f"File-size gate: base commit {base_ref} is not available; fetch it first.")
    text = read_base_text(root, base_ref, BASELINE_PATH)
    return None if text is None else load_baseline_text(text)


def base_oversized_caps(root, base_ref, paths):
    """Bootstrap cap: line counts at base_ref for those of paths already over LIMIT there."""
    caps = {}
    for path in paths:
        text = read_base_text(root, base_ref, path)
        lines = 0 if text is None else count_lines(text)
        if lines > LIMIT:
            caps[path] = lines
    return caps


def write_baseline(root, output):
    counts = collect_line_counts(root)
    grandfathered = {path: lines for path, lines in sorted(counts.items()) if lines > LIMIT}
    Path(output).write_text(json.dumps(grandfathered, indent=2) + "\n", encoding="utf-8")
    return grandfathered


def release_baseline_ref(root, base_ref):
    """Use beta's accepted debt baseline for release PRs to master.

    The size baseline is maintained on beta. Comparing a beta-to-master promotion against
    master's older baseline would reject every oversized file that was already accepted on
    beta, even though the promotion does not grow that debt. All other PRs remain checked
    against their actual target commit.
    """
    if (os.environ.get("GITHUB_BASE_REF") != "master" or
            not os.environ.get("GITHUB_HEAD_REF", "").startswith("chore/release-")):
        return base_ref

    result = subprocess.run(
        ["git", "-C", str(root), "fetch", "--no-tags", "--depth=1", "origin",
         "+refs/heads/beta:refs/remotes/origin/beta"],
        capture_output=True, text=True,
    )
    if result.returncode != 0:
        raise SystemExit("File-size gate: could not fetch beta's accepted size baseline: "
                         f"{result.stderr.strip()}")
    print("Note: release PR to master; comparing file-size limits against origin/beta.")
    return "refs/remotes/origin/beta"


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

    if not args.base_ref:
        parser.error("--base-ref is required when checking (omit it only with --write-baseline)")

    if argv is None:  # CI invocation only; tests pass argv and must not read the job's env.
        args.base_ref = release_baseline_ref(args.root, args.base_ref)

    current = collect_line_counts(args.root)
    baseline = read_baseline(args.root)
    base_baseline = read_base_baseline(args.root, args.base_ref)
    if base_baseline is None:
        candidates = {path for path, lines in current.items() if lines > LIMIT} | set(baseline)
        base_baseline = base_oversized_caps(args.root, args.base_ref, sorted(candidates))
        print(f"Note: base {args.base_ref} has no {BASELINE_PATH}; bootstrap caps derived from "
              f"{len(base_baseline)} oversized file(s) at the base commit.")

    renamed_paths = read_renamed_paths(args.root, args.base_ref)
    problems = find_violations(current, baseline, base_baseline, renamed_paths)
    for problem in problems:
        print(f"::error::{problem}")
    if problems:
        print(f"File-size gate failed: {len(problems)} problem(s). Limit is {LIMIT} lines.")
        return 1
    print(f"File-size gate passed: {len(current)} source file(s), {len(baseline)} grandfathered.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
