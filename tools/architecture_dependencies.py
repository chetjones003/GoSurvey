#!/usr/bin/env python3
"""Check source includes against the draft system map and a non-growing baseline."""

import argparse
import importlib.util
import json
import re
import sys
from collections import Counter
from pathlib import Path

SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx"}
INCLUDE = re.compile(
    r'^\s*#\s*include\b\s*(?:"(?P<quoted>[^"\n]+)"|<(?P<angled>[^>\n]+)>|(?P<macro>\S+))',
    re.MULTILINE)
SYSTEM_ROW = re.compile(r"^\|[^\n]+\|$", re.MULTILINE)
INVENTORY_SPEC = importlib.util.spec_from_file_location(
    "architecture_inventory", Path(__file__).with_name("architecture_inventory.py"))
INVENTORY = importlib.util.module_from_spec(INVENTORY_SPEC)
INVENTORY_SPEC.loader.exec_module(INVENTORY)


def _system_for(path, mapping):
    normalized = path.as_posix()
    for entry in sorted(mapping.get("mappedFolders", []), key=lambda item: len(item["path"]), reverse=True):
        if normalized.startswith(entry["path"]):
            return entry["system"]
    for entry in mapping.get("fileMappings", []):
        if entry["path"] == normalized:
            return entry["system"] if entry.get("status") != "ambiguous" else None
    return None


def _read_systems(root):
    text = (root / "docs/architecture/system-map.md").read_text(encoding="utf-8")
    systems = {}
    for row in SYSTEM_ROW.findall(text):
        columns = [column.strip() for column in row.strip().strip("|").split("|")]
        if len(columns) < 5:
            continue
        name, tier, dependencies = columns[0], columns[1], columns[4]
        if name == "System" or name.startswith("-"):
            continue
        systems[name] = {"tier": tier, "dependencies": {item.strip() for item in dependencies.split(",") if item.strip() != "None"}}
    return systems


def _resolve_include(root, source, include, mapping):
    candidate_paths = [source.parent / include, root / include, root / "src" / include]
    include_directories = {root / entry["path"] for entry in mapping.get("mappedFolders", [])}
    include_directories.update(root / Path(entry["path"]).parent for entry in mapping.get("fileMappings", []))
    candidate_paths.extend(directory / include for directory in include_directories)
    found = {candidate.resolve() for candidate in candidate_paths if candidate.is_file()}
    if len(found) == 1:
        return next(iter(found))
    if len(found) > 1:
        return "ambiguous"
    return None


def _identity(item):
    return (item["file"], item["include"], item["rule"])


def compare_baseline(current, baseline):
    now, old = Counter(map(_identity, current)), Counter(map(_identity, baseline))
    added = now - old
    removed = old - now
    return {
        "ok": not added and not removed,
        "new_violations": [list(item) for item, count in sorted(added.items()) for _ in range(count)],
        "baseline_entries_to_remove": [list(item) for item, count in sorted(removed.items()) for _ in range(count)],
    }


def check(root):
    root = Path(root).resolve()
    mapping_path = root / "docs/architecture/system-map.json"
    mapping = json.loads(mapping_path.read_text(encoding="utf-8"))
    systems = _read_systems(root)
    files = sorted(path for path in (root / "src").rglob("*") if path.is_file() and path.suffix.lower() in SOURCE_SUFFIXES)
    unmapped = []
    violations = []
    includes = []
    for source in files:
        relative = source.relative_to(root).as_posix()
        owner = _system_for(Path(relative), mapping)
        if owner is None:
            unmapped.append(relative)
        clean = INVENTORY._strip_cpp_comments(source.read_text(encoding="utf-8", errors="replace"))
        for include_match in INCLUDE.finditer(clean):
            include = include_match.group("quoted") or include_match.group("angled") or include_match.group("macro")
            delimiter = '"' if include_match.group("quoted") else '<' if include_match.group("angled") else None
            if delimiter is None:
                includes.append({"file": relative, "include": include, "classification": "unmapped"})
                continue
            public_match = re.match(r"GoSurvey/([^/]+)/", include.replace("\\", "/"))
            if public_match:
                target_owner = public_match.group(1)
                result = {"file": relative, "include": include, "classification": "allowed", "target_system": target_owner}
                if owner is None or target_owner not in systems:
                    result["classification"] = "unmapped"
                elif target_owner != owner and target_owner not in systems.get(owner, {}).get("dependencies", set()):
                    result.update(classification="violation", rule="forbidden dependency",
                                  message=f'{relative} includes "{include}": {owner} may not depend on {target_owner} (forbidden dependency)')
                    violations.append({"file": relative, "include": include, "rule": result["rule"], "message": result["message"]})
                includes.append(result)
                continue
            target_path = _resolve_include(root, source, include, mapping)
            result = {"file": relative, "include": include, "classification": "allowed"}
            if target_path is None:
                if delimiter == '"' or include.startswith("src/"):
                    result["classification"] = "unmapped"
                includes.append(result)
                continue
            if target_path == "ambiguous":
                result["classification"] = "unmapped"
                includes.append(result)
                continue
            try:
                target_relative = target_path.relative_to(root).as_posix()
            except ValueError:
                includes.append(result)
                continue
            target_owner = _system_for(Path(target_relative), mapping)
            if target_owner is None:
                result["classification"] = "unmapped"
                includes.append(result)
                continue
            if owner is None:
                result["classification"] = "unmapped"
                includes.append(result)
                continue
            if target_owner == owner:
                includes.append(result)
                continue
            allowed = target_owner in systems.get(owner, {}).get("dependencies", set())
            if not allowed:
                result.update(classification="violation", rule="forbidden dependency",
                              message=f'{relative} includes "{include}": {owner} may not depend on {target_owner} (forbidden dependency)')
            elif not include.replace("\\", "/").startswith(f"GoSurvey/{target_owner}/"):
                result.update(classification="violation", rule="private-header include",
                              message=f'{relative} includes "{include}": cross-system include must use GoSurvey/{target_owner}/ public headers (private-header include)')
            if result["classification"] == "violation":
                violations.append({"file": relative, "include": include, "rule": result["rule"], "message": result["message"]})
            includes.append(result)
    return {"includes": includes, "violations": violations, "unmapped_files": unmapped}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--baseline", type=Path, default=Path("docs/architecture/dependency-baseline.json"))
    parser.add_argument("--write-baseline", action="store_true", help="write current violations as the baseline")
    parser.add_argument("--report", type=Path, help="write all include classifications and comparison details as JSON")
    parser.add_argument("--reference-root", type=Path,
                        help="reject baseline entries absent from a fresh scan of this reference tree")
    args = parser.parse_args()
    root = args.root.resolve()
    baseline_path = args.baseline if args.baseline.is_absolute() else root / args.baseline
    result = check(root)
    if args.write_baseline:
        baseline_path.parent.mkdir(parents=True, exist_ok=True)
        baseline_document = {"generatedFrom": "beta", "violations": result["violations"]}
        baseline_path.write_text(json.dumps(baseline_document, indent=2) + "\n", encoding="utf-8")
        print(f"Wrote {len(result['violations'])} violations to {baseline_path}")
    else:
        baseline_document = json.loads(baseline_path.read_text(encoding="utf-8"))
        baseline = baseline_document.get("violations", baseline_document) if isinstance(baseline_document, dict) else baseline_document
        comparison = compare_baseline(result["violations"], baseline)
        baseline_growth = None
        if args.reference_root:
            reference_violations = check(args.reference_root.resolve())["violations"]
            baseline_growth = compare_baseline(baseline, reference_violations)["new_violations"]
            comparison["ok"] = comparison["ok"] and not baseline_growth
            comparison["baseline_growth"] = baseline_growth
        result["baseline"] = comparison
        violation_messages = {_identity(item): item["message"] for item in result["violations"]}
        for item in comparison["new_violations"]:
            print(f"VIOLATION: {violation_messages[tuple(item)]}")
        for path in result["unmapped_files"]:
            print(f"UNMAPPED: {path}: no system mapping")
        for item in comparison["baseline_entries_to_remove"]:
            print(f"BASELINE SHRINK REQUIRED: {item[0]} includes {item[1]} ({item[2]})")
        for item in baseline_growth or []:
            message = violation_messages.get(tuple(item), f"{item[0]} includes {item[1]} ({item[2]})")
            print(f"BASELINE GROWTH: {message}")
        unmapped_includes = sum(item["classification"] == "unmapped" for item in result["includes"])
        print(f"Checked {len(result['includes'])} includes: {len(result['violations'])} violations, "
              f"{len(result['unmapped_files'])} unmapped files, {unmapped_includes} unmapped includes")
        if args.report:
            report_path = args.report if args.report.is_absolute() else root / args.report
            report_path.parent.mkdir(parents=True, exist_ok=True)
            report_path.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        if not comparison["ok"]:
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
