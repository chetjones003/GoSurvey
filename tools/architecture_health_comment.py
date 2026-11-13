#!/usr/bin/env python3
"""Publish a pull request's architecture metrics against its beta merge base."""

import importlib.util
import json
import os
import re
import subprocess
import sys
import tempfile
import urllib.error
import urllib.request
from pathlib import Path, PurePosixPath

MARKER = "<!-- architecture-health -->"
METRICS_REF = "refs/remotes/origin/metrics"
BETA_REF = "refs/remotes/origin/beta"
SNAPSHOT_PREFIX = "snapshots/"
SNAPSHOT_LIMIT = 20
SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx"}
ANALYSIS_FILES = {
    ".github/file-size-baseline.json",
    "docs/architecture/dependency-baseline.json",
    "docs/architecture/system-map.md",
    "docs/architecture/system-map.json",
}


def _metric_values(snapshot):
    metrics = snapshot["metrics"]
    largest = metrics.get("largest_file")
    fan_in = metrics.get("god_header_fan_in") or {}
    return {
        "violations": int(metrics["violation_count"]),
        "largest_lines": int(largest["lines"]) if largest else 0,
        "largest_path": largest["path"] if largest else "(no source files)",
        "fan_in": int(fan_in.get("fan_in", 0)),
    }


def _direction(delta):
    return "better" if delta < 0 else "worse" if delta > 0 else "same"


def _change(delta, unit=""):
    sign = "+" if delta > 0 else ""
    return f"{sign}{delta:,}{unit} ({_direction(delta)})"


def _chart(title, unit, values):
    labels = [f'"{label}"' for label, _ in values]
    points = [value for _, value in values]
    maximum = max(points, default=0)
    maximum = max(1, maximum)
    return "\n".join((
        "```mermaid",
        "xychart-beta",
        f'    title "{title}"',
        f'    x-axis [{", ".join(labels)}]',
        f'    y-axis "{unit}" 0 --> {maximum}',
        f'    line [{", ".join(str(value) for value in points)}]',
        "```",
    ))


def render_comment(baseline, current, trend, pr_number):
    """Render the stable user-facing comment from snapshot-shaped dictionaries."""
    current_values = _metric_values(current)
    baseline_values = _metric_values(baseline) if baseline else None
    rows = []
    for key, title, unit in (
        ("violations", "Dependency violations", ""),
        ("largest_lines", "Largest source file", " lines"),
        ("fan_in", "`CadCommands.hpp` include fan-in", ""),
    ):
        if baseline_values is None:
            baseline_text = "unavailable"
            current_text = str(current_values[key])
            if key == "largest_lines":
                current_text = f"`{current_values['largest_path']}` ({current_values[key]:,} lines)"
            change_text = "unavailable"
        else:
            baseline_text = f"{baseline_values[key]:,}"
            current_text = f"{current_values[key]:,}"
            if key == "largest_lines":
                baseline_text = f"`{baseline_values['largest_path']}` ({baseline_text} lines)"
                current_text = f"`{current_values['largest_path']}` ({current_text} lines)"
            change_text = _change(current_values[key] - baseline_values[key], unit)
        rows.append(f"| {title} | {baseline_text} | {current_text} | {change_text} |")

    lines = [MARKER, "## Architecture health", ""]
    if baseline is None:
        lines.extend((
            "No beta snapshot was found for the PR merge base, so baseline comparisons are unavailable. "
            "The PR metrics and beta trend are still shown below.",
            "",
        ))
    else:
        lines.extend((
            f"Baseline: beta merge base [`{baseline['commit'][:12]}`].",
            "",
        ))
    lines.extend((
        "| Metric | Baseline | This PR | Change |",
        "| --- | ---: | ---: | ---: |",
        *rows,
        "",
        "Lower counts are better. Trend includes the latest available beta snapshots and this PR.",
        "",
    ))

    chart_values = []
    for snapshot in trend[-SNAPSHOT_LIMIT:]:
        values = _metric_values(snapshot)
        chart_values.append((snapshot["commit"][:6], values))
    chart_values.append((f"PR #{pr_number}", current_values))
    lines.extend((
        _chart("Dependency violations", "Count", [(label, v["violations"]) for label, v in chart_values]),
        "",
        _chart("Largest source file", "Lines", [(label, v["largest_lines"]) for label, v in chart_values]),
        "",
        _chart("CadCommands.hpp include fan-in", "Headers", [(label, v["fan_in"]) for label, v in chart_values]),
    ))
    return "\n".join(lines) + "\n"


def _git(args, *, check=True, text=True):
    result = subprocess.run(["git", *args], check=False, capture_output=True, text=text)
    if check and result.returncode:
        stderr = result.stderr.decode(errors="replace") if isinstance(result.stderr, bytes) else result.stderr
        raise RuntimeError(f"git {' '.join(args)} failed: {stderr.strip()}")
    return result


def _read_snapshot(revision, commit):
    path = f"{SNAPSHOT_PREFIX}{commit}.json"
    result = _git(["show", f"{revision}:{path}"], check=False)
    if result.returncode:
        return None
    return json.loads(result.stdout)


def _analysis_paths(commit):
    result = _git(["ls-tree", "-r", "-z", commit, "--", "src", ".github/file-size-baseline.json",
                   "docs/architecture/dependency-baseline.json", "docs/architecture/system-map.md",
                   "docs/architecture/system-map.json"], text=False)
    selected = []
    for entry in result.stdout.split(b"\0"):
        if not entry:
            continue
        metadata, raw_path = entry.split(b"\t", 1)
        mode, kind, object_id = metadata.decode("ascii").split()
        path = PurePosixPath(raw_path.decode("utf-8", errors="surrogateescape"))
        path_text = path.as_posix()
        if kind != "blob" or mode not in {"100644", "100755"}:
            continue  # In particular, never follow a PR-controlled symlink.
        if path_text.startswith("src/") and path.suffix.lower() in SOURCE_SUFFIXES:
            selected.append((path, object_id))
        elif path_text in ANALYSIS_FILES:
            selected.append((path, object_id))
    return selected


def _extract_analysis_tree(commit, destination):
    destination.mkdir(parents=True, exist_ok=True)
    entries = _analysis_paths(commit)
    objects = subprocess.run(["git", "cat-file", "--batch"], input="".join(
        f"{object_id}\n" for _, object_id in entries).encode("ascii"), capture_output=True, check=False)
    if objects.returncode:
        raise RuntimeError(f"git cat-file --batch failed: {objects.stderr.decode(errors='replace').strip()}")
    cursor = 0
    for path, object_id in entries:
        if path.is_absolute() or ".." in path.parts:
            raise RuntimeError(f"unsafe path in PR tree: {path}")
        header_end = objects.stdout.find(b"\n", cursor)
        if header_end < 0:
            raise RuntimeError(f"incomplete git blob response for {path}")
        header = objects.stdout[cursor:header_end].decode("ascii").split()
        if len(header) != 3 or header[0] != object_id or header[1] != "blob":
            raise RuntimeError(f"invalid git blob response for {path}")
        size = int(header[2])
        blob_start = header_end + 1
        blob_end = blob_start + size
        if blob_end >= len(objects.stdout) or objects.stdout[blob_end:blob_end + 1] != b"\n":
            raise RuntimeError(f"truncated git blob response for {path}")
        target = destination.joinpath(*path.parts)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(objects.stdout[blob_start:blob_end])
        cursor = blob_end + 1


def _load_snapshot_builder():
    path = Path(__file__).with_name("architecture_snapshot.py")
    spec = importlib.util.spec_from_file_location("architecture_snapshot_for_comment", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _github_request(method, url, token, payload=None):
    body = json.dumps(payload).encode("utf-8") if payload is not None else None
    request = urllib.request.Request(url, data=body, method=method, headers={
        "Accept": "application/vnd.github+json",
        "Authorization": f"Bearer {token}",
        "X-GitHub-Api-Version": "2022-11-28",
        "Content-Type": "application/json",
        "User-Agent": "GoSurvey-architecture-health",
    })
    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            return response.read(), response.headers.get("Link", "")
    except urllib.error.HTTPError as error:
        detail = error.read().decode("utf-8", errors="replace")
        raise RuntimeError(f"GitHub API returned HTTP {error.code}: {detail}") from error


def _comment_urls(api_url, repository, pr_number, token):
    url = f"{api_url.rstrip('/')}/repos/{repository}/issues/{pr_number}/comments?per_page=100"
    comments = []
    while url:
        payload, link_header = _github_request("GET", url, token)
        comments.extend(json.loads(payload))
        next_link = re.search(r'<([^>]+)>;\s*rel="next"', link_header)
        url = next_link.group(1) if next_link else ""
    return comments


def publish_comment(repository, pr_number, body, token, api_url):
    base = f"{api_url.rstrip('/')}/repos/{repository}/issues/{pr_number}/comments"
    comment_endpoint = f"{api_url.rstrip('/')}/repos/{repository}/issues/comments"
    matches = [comment for comment in _comment_urls(api_url, repository, pr_number, token)
               if comment.get("user", {}).get("login") == "github-actions[bot]"
               and MARKER in comment.get("body", "")]
    if matches:
        _github_request("PATCH", f"{comment_endpoint}/{matches[0]['id']}", token, {"body": body})
        # Keep exactly one bot comment even if a previous interrupted run created duplicates.
        for duplicate in matches[1:]:
            _github_request("DELETE", f"{comment_endpoint}/{duplicate['id']}", token)
    else:
        _github_request("POST", base, token, {"body": body})


def run():
    repository = os.environ["GITHUB_REPOSITORY"]
    pr_number = int(os.environ["PR_NUMBER"])
    head_sha = os.environ["PR_HEAD_SHA"]
    token = os.environ["GITHUB_TOKEN"]
    api_url = os.environ.get("GITHUB_API_URL", "https://api.github.com")
    if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", repository):
        raise ValueError("GITHUB_REPOSITORY must be owner/repository")
    if pr_number <= 0 or not re.fullmatch(r"[0-9a-f]{40}", head_sha):
        raise ValueError("PR number and full lowercase PR head SHA are required")

    _git(["fetch", "--no-tags", "origin", "+refs/heads/beta:" + BETA_REF])
    _git(["fetch", "--no-tags", "origin", f"+refs/pull/{pr_number}/head:refs/remotes/pull/{pr_number}/head"])
    _git(["fetch", "--no-tags", "origin", "+refs/heads/metrics:" + METRICS_REF], check=False)
    _git(["cat-file", "-e", f"{head_sha}^{{commit}}"])
    beta_head = _git(["rev-parse", BETA_REF]).stdout.strip()
    merge_base = _git(["merge-base", head_sha, beta_head]).stdout.strip()
    baseline = _read_snapshot(METRICS_REF, merge_base) if _git(["show-ref", "--verify", "--quiet", METRICS_REF], check=False).returncode == 0 else None

    trend = []
    if _git(["show-ref", "--verify", "--quiet", METRICS_REF], check=False).returncode == 0:
        recorded = _git(["ls-tree", "-r", "--name-only", METRICS_REF, "--", "snapshots"]).stdout.splitlines()
        recorded_commits = {Path(path).stem for path in recorded if path.endswith(".json")}
        beta_commits = _git(["rev-list", "--first-parent", BETA_REF]).stdout.splitlines()
        for commit in (sha for sha in reversed(beta_commits) if sha in recorded_commits):
            snapshot = _read_snapshot(METRICS_REF, commit)
            if snapshot is not None:
                trend.append(snapshot)
        trend = trend[-SNAPSHOT_LIMIT:]

    with tempfile.TemporaryDirectory(prefix="gosurvey-pr-architecture-") as temporary:
        source_root = Path(temporary) / "pr-source"
        _extract_analysis_tree(head_sha, source_root)
        current = _load_snapshot_builder().build_snapshot(source_root, head_sha)
    body = render_comment(baseline, current, trend, pr_number)
    publish_comment(repository, pr_number, body, token, api_url)
    print(f"Updated architecture-health comment on PR #{pr_number}.")


if __name__ == "__main__":
    try:
        run()
    except (KeyError, ValueError, RuntimeError, json.JSONDecodeError) as error:
        print(f"architecture-health comment failed: {error}", file=sys.stderr)
        sys.exit(1)
