"""Select MSVC /analyze object targets for changed C/C++ files and header consumers.

MSVC reports translation-unit diagnostics, so a changed header selects every analyzed
translation unit that reaches it through its include graph. If the source graph cannot be
classified safely, the selector falls back to every analyzed translation unit.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path


SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".c++"}
HEADER_SUFFIXES = {".h", ".hh", ".hpp", ".hxx", ".inl", ".ipp", ".tpp", ".inc"}
FULL_SCOPE_INPUT_SUFFIXES = {".in", ".ui", ".qrc"}
ANALYSIS_CONFIGURATION_RE = re.compile(
    r"(?i)\b(?:target_compile_options|target_compile_definitions|target_include_directories|"
    r"include_directories|target_sources|add_library|add_executable|add_subdirectory|option|"
    r"find_package|target_link_libraries|target_compile_features|set_property|file\s*\(\s*GLOB|"
    r"set_target_properties|"
    r"set_source_files_properties|target_precompile_headers|gosurvey_target_warnings|"
    r"GOSURVEY_ENABLE_ANALYZE|GOSURVEY_ANALYZE_WERROR|CMAKE_[A-Z0-9_]*FLAGS|"
    r"CMAKE_[A-Z0-9_]*STANDARD|CMAKE_MSVC_RUNTIME_LIBRARY|add_custom_command|"
    r"AUTOMOC|AUTOUIC|AUTORCC|SOURCES|/analyze|/WX|/W[0-4]|/wd\d+|"
    r"/external:|/Z[67]|/Zi|/EH[a-z]+)\b"
)
INCLUDE_RE = re.compile(r"^\s*#\s*include\s+(\S+)")
LITERAL_INCLUDE_RE = re.compile(r'^[<"]([^>"]+)[>"]$')
INCLUDE_DIR_RE = re.compile(r'(?i)(?:[-/]external:I|/I|-I)(?:"([^"]+)"|([^\s]+))')


def canonical(path: Path) -> str:
    return os.path.normcase(str(path.resolve()))


def is_within(path: Path, root: Path) -> bool:
    try:
        path.resolve().relative_to(root.resolve())
        return True
    except ValueError:
        return False


def parse_include_dirs(command: str, directory: Path, root: Path) -> list[Path]:
    result = [directory, root]
    for match in INCLUDE_DIR_RE.finditer(command):
        value = match.group(1) or match.group(2)
        candidate = Path(value)
        if not candidate.is_absolute():
            candidate = directory / candidate
        result.append(candidate)
    # PCH headers are forced into each applicable translation unit by CMake. The project PCH
    # deliberately contains no project headers, but include it in the graph if that changes.
    result.append(root / "cmake" / "gosurvey_pch.hpp")
    unique: list[Path] = []
    seen: set[str] = set()
    for path in result:
        key = canonical(path)
        if key not in seen:
            seen.add(key)
            unique.append(path)
    return unique


def project_includes(name: str, including_file: Path, include_dirs: list[Path], root: Path) -> list[Path]:
    """Return all matching project headers, conservatively accounting for target include order."""
    include_path = Path(name.replace("\\", "/"))
    candidates = [including_file.parent / include_path, *(base / include_path for base in include_dirs)]
    found: dict[str, Path] = {}
    for candidate in candidates:
        if candidate.is_file() and is_within(candidate, root):
            resolved = candidate.resolve()
            found[canonical(resolved)] = resolved
    return list(found.values())


def reverse_include_graph(analyzed: list[dict], root: Path) -> tuple[dict[str, set[str]], bool]:
    """Build a reverse graph once so changed-header lookup stays cheap on large projects."""
    all_include_dirs = [directory for entry in analyzed for directory in entry["include_dirs"]]
    project_dirs = [directory for directory in all_include_dirs if is_within(directory, root)]
    project_dirs.append(root)
    include_dirs: list[Path] = []
    seen_dirs: set[str] = set()
    for directory in project_dirs:
        key = canonical(directory)
        if key not in seen_dirs:
            seen_dirs.add(key)
            include_dirs.append(directory)

    reverse: dict[str, set[str]] = {}
    pending = [entry["source"] for entry in analyzed]
    pending.append(root / "cmake" / "gosurvey_pch.hpp")
    visited: set[str] = set()
    while pending:
        current = pending.pop()
        if not current.is_file():
            continue
        current_key = canonical(current)
        if current_key in visited:
            continue
        visited.add(current_key)
        try:
            lines = current.read_text(encoding="utf-8", errors="replace").splitlines()
        except OSError:
            return {}, False
        for line in lines:
            match = INCLUDE_RE.match(line)
            if not match:
                continue
            literal = LITERAL_INCLUDE_RE.match(match.group(1))
            if not literal:
                # Vendored headers often use macro includes for vendor-specific config. Only
                # application/test code can hide a dependency on a changed GoSurvey header.
                if not is_within(current, root / "third_party"):
                    return {}, False
                continue
            for included in project_includes(literal.group(1), current, include_dirs, root):
                included_key = canonical(included)
                reverse.setdefault(included_key, set()).add(current_key)
                pending.append(included)
    return reverse, True


def read_analyzed_commands(compilation_database: Path, root: Path):
    entries = json.loads(compilation_database.read_text(encoding="utf-8"))
    analyzed = []
    for entry in entries:
        command = entry.get("command", "")
        if not re.search(r"(?i)(?:^|\s)/analyze(?:\s|$)", command):
            continue
        directory = Path(entry["directory"])
        source = Path(entry["file"])
        if not source.is_absolute():
            source = directory / source
        output = entry.get("output")
        if not output:
            raise ValueError(f"MSVC compile entry has no output path: {source}")
        object_path = Path(output)
        if not object_path.is_absolute():
            object_path = directory / object_path
        try:
            target = object_path.resolve().relative_to(compilation_database.parent.resolve()).as_posix()
        except ValueError as error:
            raise ValueError(f"Object output is outside the Ninja build directory: {output}") from error
        analyzed.append(
            {
                "source": source.resolve(),
                "target": target,
                "include_dirs": parse_include_dirs(command, directory, root),
                "command": command,
                "directory": str(directory),
                "output": object_path.resolve(),
            }
        )
    if not analyzed:
        raise ValueError("No /analyze compile commands found; refusing to report an empty analysis.")
    return analyzed


def changed_paths(base_sha: str, root: Path) -> list[Path]:
    result = subprocess.run(
        ["git", "-C", str(root), "diff", "--name-only", "--diff-filter=ACMR", f"{base_sha}...HEAD"],
        check=True,
        text=True,
        capture_output=True,
    )
    return [(root / path).resolve() for path in result.stdout.splitlines() if path]


def cmake_diff_affects_analysis(diff_text: str) -> bool:
    current_path = ""
    for line in diff_text.splitlines():
        if line.startswith("+++ b/"):
            current_path = line[6:]
        elif line.startswith("--- a/"):
            current_path = line[6:]
        elif line[:1] in {"+", "-"} and not line.startswith(("+++", "---")):
            content = line[1:].lstrip()
            if content.startswith("#"):
                continue
            is_cmake_file = (
                current_path == "CMakeLists.txt"
                or current_path.endswith("/CMakeLists.txt")
                or current_path.endswith(".cmake")
            )
            policy_change = "cmake_policy" in content.lower() and "cmp0141" not in content.lower()
            if is_cmake_file and (ANALYSIS_CONFIGURATION_RE.search(content) or policy_change):
                return True
    return False


def cmake_changes_affect_analysis(base_sha: str, root: Path) -> bool:
    result = subprocess.run(
        [
            "git",
            "-C",
            str(root),
            "diff",
            "--unified=0",
            f"{base_sha}...HEAD",
            "--",
            ":(glob)**/CMakeLists.txt",
            ":(glob)**/*.cmake",
        ],
        check=True,
        text=True,
        capture_output=True,
    )
    return cmake_diff_affects_analysis(result.stdout)


def select_targets(
    paths: list[Path], analyzed: list[dict], root: Path, analysis_configuration_changed: bool = False
) -> tuple[list[str], str]:
    commands_by_source = {canonical(entry["source"]): entry for entry in analyzed}
    if analysis_configuration_changed:
        return sorted(entry["target"] for entry in analyzed), (
            "full-project fallback: analyzer-affecting CMake configuration changed"
        )
    changed_sources: set[str] = set()
    changed_headers: set[str] = set()
    for path in paths:
        suffix = path.suffix.lower()
        if suffix in FULL_SCOPE_INPUT_SUFFIXES or path.name.lower() == "cmakepresets.json":
            return sorted(entry["target"] for entry in analyzed), (
                f"full-project fallback: build or source-generation input changed ({path.relative_to(root)})"
            )
        if suffix in SOURCE_SUFFIXES:
            # Vendored sources intentionally have no /analyze commands in this project.
            if not is_within(path, root / "third_party"):
                changed_sources.add(canonical(path))
        elif suffix in HEADER_SUFFIXES:
            changed_headers.add(canonical(path))

    missing_sources = changed_sources.difference(commands_by_source)
    if missing_sources:
        return sorted(entry["target"] for entry in analyzed), (
            f"full-project fallback: {len(missing_sources)} changed source file(s) had no analyzed compile entry"
        )

    selected = {commands_by_source[source]["target"] for source in changed_sources}
    if changed_headers:
        reverse, classifiable = reverse_include_graph(analyzed, root)
        if not classifiable:
            return sorted(item["target"] for item in analyzed), (
                "full-project fallback: an include graph contains an unresolved macro include"
            )
        affected = set(changed_headers)
        pending = list(changed_headers)
        while pending:
            for parent in reverse.get(pending.pop(), set()):
                if parent not in affected:
                    affected.add(parent)
                    pending.append(parent)
        selected.update(
            entry["target"] for entry in analyzed if canonical(entry["source"]) in affected
        )

    if not selected:
        return [], "no analyzed C/C++ translation units are affected by this diff"
    return sorted(selected), (
        f"diff-scoped analysis: {len(changed_sources)} changed source(s), "
        f"{len(changed_headers)} changed header(s), {len(selected)} affected translation unit(s)"
    )


def read_autogen_info(build_dir: Path) -> list[dict]:
    result = []
    for info_path in (build_dir / "CMakeFiles").glob("*_autogen.dir/AutogenInfo.json"):
        data = json.loads(info_path.read_text(encoding="utf-8"))
        ninja_target = info_path.parent.name.removesuffix(".dir")
        target_name = ninja_target.removesuffix("_autogen")
        paths = set()
        for key in ("SOURCES", "HEADERS"):
            for item in data.get(key, []):
                value = item[0] if isinstance(item, list) else item
                paths.add(canonical(Path(value)))
        result.append(
            {
                "target": target_name,
                "ninja_target": ninja_target,
                "info_file": str(info_path.resolve()),
                "paths": paths,
                "moc_source": canonical(Path(data["MOC_COMPILATION_FILE"])),
            }
        )
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base-sha", required=True)
    parser.add_argument("--compile-commands", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--targets-file", type=Path, required=True)
    args = parser.parse_args()

    root = args.repo_root.resolve()
    build_dir = args.build_dir.resolve()
    analyzed = read_analyzed_commands(args.compile_commands.resolve(), root)
    config_changed = cmake_changes_affect_analysis(args.base_sha, root)
    paths = changed_paths(args.base_sha, root)
    targets, reason = select_targets(
        paths, analyzed, root, analysis_configuration_changed=config_changed
    )
    selected_entries = [entry for entry in analyzed if entry["target"] in set(targets)]
    selected_target_names = {
        match.group(1)
        for entry in selected_entries
        if (match := re.match(r"CMakeFiles/(.+)\.dir/", entry["target"]))
    }
    autogen_info = read_autogen_info(build_dir)
    all_scope = reason.startswith("full-project fallback")
    autogen_info_files = []
    for info in autogen_info:
        if all_scope or info["target"] in selected_target_names or any(
            canonical(path) in info["paths"] for path in paths
        ):
            autogen_info_files.append(info["info_file"])
            moc_entry = next(
                (entry for entry in analyzed if canonical(entry["source"]) == info["moc_source"]), None
            )
            if moc_entry and moc_entry not in selected_entries:
                selected_entries.append(moc_entry)
                targets.append(moc_entry["target"])

    args.targets_file.parent.mkdir(parents=True, exist_ok=True)
    selection = {
        "reason": reason,
        "candidate_count": len(analyzed),
        "autogen_info_files": sorted(set(autogen_info_files)),
        "units": [
            {
                "source": str(entry["source"]),
                "directory": entry["directory"],
                "command": entry["command"],
                "output": str(entry["output"]),
                "target": entry["target"],
            }
            for entry in selected_entries
        ],
    }
    args.targets_file.write_text(json.dumps(selection, indent=2) + "\n", encoding="utf-8")
    print(f"MSVC /analyze: {reason}; candidate translation units: {len(analyzed)}")
    print(f"Selected translation units: {len(selected_entries)}; Qt autogen targets: {len(autogen_info_files)}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        print(f"MSVC /analyze target selection failed: {type(error).__name__}: {error}", file=sys.stderr)
        raise SystemExit(1)
