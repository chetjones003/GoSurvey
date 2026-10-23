"""Summarize fresh CI build traces without hiding compiler-cache bypasses."""

import argparse
import json
import os
import re
import subprocess
import xml.etree.ElementTree as ET
from pathlib import Path


def cache_summary(data: dict) -> dict:
    stats = data["stats"]
    requests = stats.get("compile_requests", 0)
    hits = sum(stats.get("cache_hits", {}).get("counts", {}).values())
    return {
        "requests": requests,
        "hits": hits,
        "misses": sum(stats.get("cache_misses", {}).get("counts", {}).values()),
        "uncacheable": stats.get("requests_not_cacheable", 0),
        "uncacheable_reasons": stats.get("not_cached", {}),
        "hit_fraction_all_requests": hits / requests if requests else 0,
    }


def ninja_summary(path: Path) -> dict:
    commands = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith("#"):
            continue
        start, end, _, output, command_hash = line.split("\t")
        start, end = int(start), int(end)
        # One command can emit several output rows (for example an executable and its PDB).
        commands.setdefault((start, end, command_hash), {
            "start_ms": start, "end_ms": end, "output": output,
            "seconds": (end - start) / 1000,
        })
    events = []
    for command in commands.values():
        if command["end_ms"] > command["start_ms"]:
            events.extend([(command["start_ms"], 1), (command["end_ms"], -1)])
    running = peak = 0
    for _, change in sorted(events):
        running += change
        peak = max(peak, running)
    return {
        "commands": len(commands),
        "peak_running_commands": peak,
        "wall_seconds": max((c["end_ms"] for c in commands.values()), default=0) / 1000,
        "longest_commands": sorted(commands.values(), key=lambda c: c["seconds"], reverse=True)[:20],
    }


def test_timings(build_dir: Path, output_dir: Path) -> tuple[list[dict], list[str]]:
    cases = []
    incomplete = []
    reports = list(output_dir.glob("**/*.xml"))
    reports.extend((build_dir.parent.parent / "artifacts" / "qt-editor").glob("**/qt-tests.xml"))
    reports.extend((build_dir.parent.parent / "artifacts" / "catch").glob("**/results.xml"))
    for report in reports:
        try:
            tree = ET.parse(report)
        except ET.ParseError as error:
            # A timed-out test can leave an unfinished report; preserve the other evidence.
            incomplete.append(f"{report}: {error}")
            continue
        for case in tree.iter("testcase"):
            cases.append({"name": case.get("name", ""), "seconds": float(case.get("time", 0)),
                          "report": str(report)})
        if tree.getroot().tag == "Catch2TestRun":
            for case in tree.getroot().findall("TestCase"):
                result = case.find("OverallResult")
                if result is not None:
                    cases.append({"name": case.get("name", ""),
                                  "seconds": float(result.get("durationInSeconds", 0)),
                                  "report": str(report)})
    log = build_dir / "Testing" / "Temporary" / "LastTest.log"
    if log.exists():
        for line in log.read_text(encoding="utf-8", errors="replace").splitlines():
            match = re.match(r"^\s*([0-9.]+) s: (.+)$", line)
            if match:
                cases.append({"name": match[2], "seconds": float(match[1]), "report": "Catch2"})
    return sorted(cases, key=lambda case: case["seconds"], reverse=True)[:30], incomplete


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--sccache", action="store_true")
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    report = {}
    summary = ["## CI performance evidence"]
    ninja_log = args.build_dir / ".ninja_log"
    if ninja_log.exists():
        report["ninja"] = ninja_summary(ninja_log)
        trace = report["ninja"]
        summary.append(f"Ninja: {trace['wall_seconds']:.1f}s; "
                       f"peak {trace['peak_running_commands']} simultaneous commands.")
        summary.extend(["", "### Longest build commands", "| Seconds | Output |", "|---:|---|"])
        for command in trace["longest_commands"]:
            summary.append(f"| {command['seconds']:.2f} | {command['output'].replace('|', '/')} |")
    if args.sccache:
        result = subprocess.run([os.environ.get("SCCACHE_PATH", "sccache"),
                                 "--show-stats", "--stats-format=json"],
                                text=True, capture_output=True, check=True)
        raw = json.loads(result.stdout)
        (args.output_dir / "sccache.json").write_text(json.dumps(raw, indent=2), encoding="utf-8")
        report["cache"] = cache_summary(raw)
        cache = report["cache"]
        summary.extend(["", "### Compiler cache", f"**{cache['hits']}/{cache['requests']} requests "
                        f"reused ({cache['hit_fraction_all_requests']:.1%} of ALL requests)**; "
                        f"{cache['misses']} misses; {cache['uncacheable']} uncacheable.",
                        f"Bypass reasons: `{json.dumps(cache['uncacheable_reasons'])}`"])
    report["slowest_tests"], report["incomplete_test_reports"] = test_timings(args.build_dir, args.output_dir)
    if report["incomplete_test_reports"]:
        summary.extend(["", "### Incomplete test reports (a test may have aborted)",
                        *report["incomplete_test_reports"]])
    if report["slowest_tests"]:
        summary.extend(["", "### Slowest tests (registrations and individual cases)",
                        "| Seconds | Test |", "|---:|---|"])
        for case in report["slowest_tests"]:
            summary.append(f"| {case['seconds']:.2f} | {case['name'].replace('|', '/')} |")
    text = "\n".join(summary) + "\n"
    (args.output_dir / "performance.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    (args.output_dir / "performance.md").write_text(text, encoding="utf-8")
    print(text)
    if os.environ.get("GITHUB_STEP_SUMMARY"):
        with open(os.environ["GITHUB_STEP_SUMMARY"], "a", encoding="utf-8") as stream:
            stream.write(text)


if __name__ == "__main__":
    main()
