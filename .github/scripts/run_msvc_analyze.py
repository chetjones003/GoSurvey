"""Run only the MSVC /analyze compiler commands selected for a PR diff."""

from __future__ import annotations

import argparse
import concurrent.futures
import json
import re
import subprocess
import sys
from pathlib import Path


def prepare_autogen(build_dir: Path, info_files: list[str]) -> None:
    for info_file in info_files:
        result = subprocess.run(
            ["cmake", "-E", "cmake_autogen", info_file, "Release"],
            cwd=build_dir,
            text=True,
            capture_output=True,
        )
        if result.stdout:
            print(result.stdout, end="")
        if result.stderr:
            print(result.stderr, end="", file=sys.stderr)
        if result.returncode:
            raise RuntimeError(f"Qt autogen for {info_file} failed with exit {result.returncode}")


def run_unit(unit: dict, compiler_launcher: str) -> tuple[dict, int, str, str]:
    command = unit["command"]
    command = re.sub(r"(?i)(?<!\S)/showIncludes(?=\s|$)", "", command)
    if compiler_launcher and not re.search(r"(?i)(?:^|[\\/ ])sccache(?:\.exe)?(?:\s|$)", command):
        command = f"{compiler_launcher} {command}"
    output = Path(unit["output"])
    output.parent.mkdir(parents=True, exist_ok=True)
    result = subprocess.run(
        command,
        cwd=unit["directory"],
        shell=True,
        text=True,
        capture_output=True,
        errors="replace",
    )
    return unit, result.returncode, result.stdout, result.stderr


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--selection", required=True, type=Path)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--compiler-launcher", default="sccache")
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()

    selection = json.loads(args.selection.read_text(encoding="utf-8"))
    units = selection["units"]
    print(f"MSVC /analyze scope: {selection['reason']}")
    if not units:
        print("No affected translation units; blocking analysis check passes with an empty diff scope.")
        return 0

    build_dir = args.build_dir.resolve()
    if selection["reason"].startswith("full-project fallback"):
        print("Dependency scope is uncertain; running the full MSVC /analyze build as a safe fallback.")
        result = subprocess.run(["cmake", "--build", str(build_dir)])
        return result.returncode

    prepare_autogen(build_dir, selection.get("autogen_info_files", []))
    print(f"Running {len(units)} MSVC /analyze compile command(s) using up to {args.jobs} workers.")
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as executor:
        futures = [executor.submit(run_unit, unit, args.compiler_launcher) for unit in units]
        results = [future.result() for future in futures]

    failed = False
    diagnostic = re.compile(r"(?i)(?:warning|error)\s+C\d{4}")
    for unit, returncode, stdout, stderr in results:
        source = unit["source"]
        print(f"MSVC /analyze: {source} (exit {returncode})")
        output = stdout + stderr
        if returncode:
            print(output, end="" if output.endswith("\n") else "\n")
            failed = True
        else:
            for line in output.splitlines():
                if diagnostic.search(line):
                    print(line)
    if failed:
        print("::error::MSVC /analyze failed for one or more diff-affected translation units.")
        return 1
    print("MSVC /analyze passed for every diff-affected translation unit.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        print(f"MSVC /analyze execution failed: {type(error).__name__}: {error}", file=sys.stderr)
        raise SystemExit(1)
