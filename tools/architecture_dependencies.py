#!/usr/bin/env python3
"""Check source includes against the candidate system map and a non-growing baseline."""

import argparse
import importlib.util
import json
import re
import subprocess
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
    for entry in mapping.get("fileMappings", []):
        if entry["path"] == normalized and entry.get("status") != "ambiguous":
            return entry["system"]
    for entry in sorted(mapping.get("mappedFolders", []), key=lambda item: len(item["path"]), reverse=True):
        if normalized.startswith(entry["path"]):
            return entry["system"]
    return None


def _read_systems(mapping_root):
    text = (mapping_root / "docs/architecture/system-map.md").read_text(encoding="utf-8")
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


def _resolve_include(root, source, include, mapping, source_renames=None,
                     reference_root=None, reference_source=None):
    candidate_paths = _include_candidate_paths(root, source, include, mapping)
    found = {candidate.resolve() for candidate in candidate_paths if candidate.is_file()}
    if source_renames and reference_root and reference_source:
        reference_candidates = _include_candidate_paths(
            reference_root, reference_source, include, mapping)
        reference_paths = set()
        for candidate in reference_candidates:
            try:
                relative = candidate.resolve().relative_to(reference_root).as_posix()
            except ValueError:
                continue
            if candidate.is_file():
                reference_paths.add(relative)

        mapped_reference = {source_renames.get(path, path) for path in reference_paths}
        renamed_destinations = {source_renames[path] for path in reference_paths
                                if path in source_renames}
        mapped_destinations = {root / path for path in mapped_reference}
        moved_destinations = {root / path for path in renamed_destinations}
        if (len(mapped_destinations) == len(mapped_reference)
                and all(path.is_file() for path in mapped_destinations)):
            expanded = set(found)
            expanded.update(moved_destinations)
            # Preserve the old resolution only when the candidate's entire set is
            # exactly the reference set after proven moves. A newly added header
            # therefore cannot make a previously unique include look ambiguous.
            if expanded == {path.resolve() for path in mapped_destinations}:
                found = expanded
    if len(found) == 1:
        return next(iter(found))
    if len(found) > 1:
        return "ambiguous"
    return None


def _include_candidate_paths(root, source, include, mapping):
    candidate_paths = [source.parent / include, root / include, root / "src" / include]
    include_directories = {root / entry["path"] for entry in mapping.get("mappedFolders", [])}
    include_directories.update(root / Path(entry["path"]).parent for entry in mapping.get("fileMappings", []))
    candidate_paths.extend(directory / include for directory in include_directories)
    return candidate_paths


def _identity(item):
    # Key by edge (file, rule, target system), not include text. A move that renames an include
    # keeps the same forbidden edge, so it must not count as new violations. Each edge is still
    # counted, so a new forbidden include from the same file grows the baseline.
    return (item["file"], item["rule"], item["target"])


def compare_baseline(current, baseline, file_renames=None):
    file_renames = file_renames or {}
    normalized_baseline = []
    for item in baseline:
        normalized = dict(item)
        normalized["file"] = file_renames.get(normalized["file"], normalized["file"])
        normalized_baseline.append(normalized)
    now, old = Counter(map(_identity, current)), Counter(map(_identity, normalized_baseline))
    added = now - old
    removed = old - now
    return {
        "ok": not added and not removed,
        "new_violations": [list(item) for item, count in sorted(added.items()) for _ in range(count)],
        "baseline_entries_to_remove": [list(item) for item, count in sorted(removed.items()) for _ in range(count)],
    }


def _proven_source_renames(reference_root, candidate_root):
    """Map reference paths to candidate paths only for Git-detected source renames."""
    try:
        reference_head = subprocess.check_output(
            ["git", "-C", str(reference_root), "rev-parse", "HEAD"],
            text=True, stderr=subprocess.DEVNULL).strip()
        candidate_head = subprocess.check_output(
            ["git", "-C", str(candidate_root), "rev-parse", "HEAD"],
            text=True, stderr=subprocess.DEVNULL).strip()
        changes = subprocess.check_output(
            ["git", "-C", str(candidate_root), "diff", "--name-status", "-z", "--find-renames=20%",
             reference_head, candidate_head], text=True, encoding="utf-8", errors="surrogateescape",
            stderr=subprocess.DEVNULL)
    except (OSError, subprocess.CalledProcessError):
        return {}

    renames = {}
    fields = changes.split("\0")
    index = 0
    while index < len(fields) - 1:
        status = fields[index]
        index += 1
        if status.startswith(("R", "C")):
            if index + 1 >= len(fields):
                return {}
            old_path, new_path = fields[index:index + 2]
            index += 2
            if (status.startswith("R") and Path(old_path).suffix.lower() in SOURCE_SUFFIXES
                    and Path(new_path).suffix.lower() in SOURCE_SUFFIXES):
                renames[old_path] = new_path
        else:
            index += 1
    return renames


def _changed_source_paths(reference_root, candidate_root):
    """Return candidate source paths edited since the reference, or None on error."""
    try:
        reference_head = subprocess.check_output(
            ["git", "-C", str(reference_root), "rev-parse", "HEAD"],
            text=True, stderr=subprocess.DEVNULL).strip()
        candidate_head = subprocess.check_output(
            ["git", "-C", str(candidate_root), "rev-parse", "HEAD"],
            text=True, stderr=subprocess.DEVNULL).strip()
        changes = subprocess.check_output(
            ["git", "-C", str(candidate_root), "diff", "--name-status", "-z",
             "--find-renames=100%", reference_head, candidate_head],
            text=True, encoding="utf-8", errors="surrogateescape", stderr=subprocess.DEVNULL)
        working_changes = subprocess.check_output(
            ["git", "-C", str(candidate_root), "diff", "--name-only", "-z", "HEAD"],
            text=True, encoding="utf-8", errors="surrogateescape", stderr=subprocess.DEVNULL)
        untracked = subprocess.check_output(
            ["git", "-C", str(candidate_root), "ls-files", "--others", "-z"],
            text=True, encoding="utf-8", errors="surrogateescape", stderr=subprocess.DEVNULL)
    except (OSError, subprocess.CalledProcessError):
        return None

    changed = set()
    fields = changes.split("\0")
    index = 0
    while index < len(fields) - 1:
        status = fields[index]
        index += 1
        if status.startswith("R"):
            if index + 1 >= len(fields):
                return None
            _, new_path = fields[index:index + 2]
            index += 2
            # --find-renames=100% ensures this is an unchanged move.
            continue
        if status.startswith("C"):
            if index + 1 >= len(fields):
                return None
            index += 1
            changed.add(fields[index])
        else:
            changed.add(fields[index])
        index += 1
    changed.update(path for path in working_changes.split("\0") if path)
    changed.update(path for path in untracked.split("\0") if path)
    candidate_sources = {
        path.relative_to(candidate_root).as_posix()
        for path in (candidate_root / "src").rglob("*")
        if path.is_file() and path.suffix.lower() in SOURCE_SUFFIXES
    }
    return changed & candidate_sources


def check(root, mapping_root=None, source_renames=None, changed_sources=None,
          reference_root=None):
    root = Path(root).resolve()
    mapping_root = Path(mapping_root or root).resolve()
    mapping_path = mapping_root / "docs/architecture/system-map.json"
    mapping = json.loads(mapping_path.read_text(encoding="utf-8"))
    systems = _read_systems(mapping_root)
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
                    violations.append({"file": relative, "include": include, "target": target_owner,
                                       "rule": result["rule"], "message": result["message"]})
                includes.append(result)
                continue
            include_renames = source_renames
            reference_source = None
            if changed_sources is not None and relative in changed_sources:
                include_renames = None
            if include_renames and reference_root:
                old_relative = next((old for old, new in include_renames.items()
                                     if new == relative), relative)
                possible_reference_source = reference_root / old_relative
                if possible_reference_source.is_file():
                    reference_source = possible_reference_source
            target_path = _resolve_include(root, source, include, mapping, include_renames,
                                           reference_root, reference_source)
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
                violations.append({"file": relative, "include": include, "target": target_owner,
                                   "rule": result["rule"], "message": result["message"]})
            includes.append(result)
    return {"includes": includes, "violations": violations, "unmapped_files": unmapped}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--baseline", type=Path, default=Path("docs/architecture/dependency-baseline.json"))
    parser.add_argument("--write-baseline", action="store_true", help="write current violations as the baseline")
    parser.add_argument("--report", type=Path, help="write all include classifications and comparison details as JSON")
    parser.add_argument("--reference-root", type=Path,
                        help="scan reference source using the candidate map; reject baseline entries absent there")
    args = parser.parse_args()
    root = args.root.resolve()
    baseline_path = args.baseline if args.baseline.is_absolute() else root / args.baseline
    changed_sources = None
    path_renames = {}
    if args.reference_root:
        reference_root = args.reference_root.resolve()
        changed_sources = _changed_source_paths(reference_root, root)
        path_renames = _proven_source_renames(reference_root, root)
        if changed_sources is None:
            # Without a trustworthy source diff, do not suppress include growth.
            path_renames = {}
    result = check(root, source_renames=path_renames, changed_sources=changed_sources,
                   reference_root=args.reference_root.resolve() if args.reference_root else None)
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
            # A mapping-only change can reveal violations already present in base source. Use
            # the candidate ownership table for both scans so only source changes grow the ratchet.
            reference_violations = check(args.reference_root.resolve(), mapping_root=root)["violations"]
            baseline_growth = compare_baseline(
                baseline, reference_violations, path_renames)["new_violations"]
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
