#!/usr/bin/env python3
"""Collect lightweight source-tree architecture metrics without compiling."""

import argparse
import json
import re
from collections import defaultdict
from pathlib import Path

SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx"}
INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.MULTILINE)
QT_INCLUDE = re.compile(r'^\s*#\s*include\s*[<"](?:Q[A-Z][A-Za-z0-9_]*|Qt[A-Za-z0-9_/]*)', re.MULTILINE)
OPENGL = re.compile(r'\b(?:gl[A-Z]\w*\s*\(|GL_[A-Z0-9_]+|OpenGL|QOpenGL\w*)')
OPENGL_INCLUDE = re.compile(
    r'^\s*#\s*include\s*[<"](?:GL/|OpenGL/|QOpenGL\w*|glew[^>"/]*\.h|glad/[^>"]+\.h)',
    re.MULTILINE | re.IGNORECASE,
)
RAW_STRING_START = re.compile(r'(?:u8|u|U|L)?R"(?P<delimiter>[^ ()\\\t\r\n]{0,16})\(')
INCLUDE_PREFIX = re.compile(r'^\s*#\s*include\b')


def _strip_cpp(text, preserve_include_strings):
    output = []
    line_prefix = []
    index = 0

    def append(value, masked=False):
        for character in value:
            if character == "\n":
                output.append("\n")
                line_prefix.clear()
            else:
                output.append(" " if masked else character)
                line_prefix.append(" " if masked else character)

    while index < len(text):
        if text.startswith("//", index):
            end = text.find("\n", index)
            if end == -1:
                append(text[index:], masked=True)
                break
            append(text[index:end], masked=True)
            index = end
            continue

        if text.startswith("/*", index):
            close = text.find("*/", index + 2)
            end = len(text) if close == -1 else close + 2
            append(text[index:end], masked=True)
            index = end
            continue

        raw_start = RAW_STRING_START.match(text, index)
        if raw_start and (index == 0 or not (text[index - 1].isalnum() or text[index - 1] == "_")):
            terminator = ")" + raw_start.group("delimiter") + '"'
            close = text.find(terminator, raw_start.end())
            end = len(text) if close == -1 else close + len(terminator)
            append(text[index:end], masked=True)
            index = end
            continue

        character = text[index]
        is_digit_separator = character == "'" and index > 0 and text[index - 1] in "0123456789abcdefABCDEF"
        if character in "\"'" and not is_digit_separator:
            end = index + 1
            while end < len(text):
                if text[end] == "\\":
                    end += 2
                elif text[end] == character:
                    end += 1
                    break
                elif text[end] == "\n":
                    break
                else:
                    end += 1
            literal = text[index:end]
            preserve = preserve_include_strings and character == '"' and INCLUDE_PREFIX.match("".join(line_prefix))
            append(literal, masked=not preserve)
            index = end
            continue

        append(character)
        index += 1

    return "".join(output)


def _strip_cpp_comments_and_literals(text):
    return _strip_cpp(text, preserve_include_strings=False)


def _strip_cpp_comments(text):
    return _strip_cpp(text, preserve_include_strings=True)


def _cycles(graph):
    visited = set()
    finish_order = []
    for start in sorted(graph):
        if start in visited:
            continue
        visited.add(start)
        traversal = [(start, iter(sorted(graph[start])))]
        while traversal:
            node, neighbors = traversal[-1]
            neighbor = next(neighbors, None)
            if neighbor is None:
                finish_order.append(node)
                traversal.pop()
            elif neighbor not in visited:
                visited.add(neighbor)
                traversal.append((neighbor, iter(sorted(graph[neighbor]))))

    reverse_graph = {node: set() for node in graph}
    for node, neighbors in graph.items():
        for neighbor in neighbors:
            reverse_graph[neighbor].add(node)

    assigned = set()
    found = []
    for start in reversed(finish_order):
        if start in assigned:
            continue
        component = []
        pending = [start]
        assigned.add(start)
        while pending:
            node = pending.pop()
            component.append(node)
            for neighbor in sorted(reverse_graph[node], reverse=True):
                if neighbor not in assigned:
                    assigned.add(neighbor)
                    pending.append(neighbor)
        if len(component) > 1 or start in graph[start]:
            found.append(sorted(component))
    return sorted(found)


def collect_metrics(root):
    root = Path(root).resolve()
    files = sorted(path for path in (root / "src").rglob("*")
                   if path.is_file() and path.suffix.lower() in SOURCE_SUFFIXES)
    texts = {path: path.read_text(encoding="utf-8", errors="replace") for path in files}
    relative = {path: path.relative_to(root).as_posix() for path in files}
    src_root = root / "src"
    include_dirs = [src_root] + sorted(path for path in src_root.iterdir() if path.is_dir())
    line_counts = {path: len(text.splitlines()) for path, text in texts.items()}
    folder_counts = defaultdict(int)
    graph = {relative[path]: set() for path in files}
    for path, text in texts.items():
        uncommented = _strip_cpp_comments(text)
        source_rel = relative[path]
        folder_counts[Path(source_rel).parent.as_posix()] += line_counts[path]
        for included in INCLUDE.findall(uncommented):
            candidates = (path.parent / included, *(directory / included for directory in include_dirs), root / included)
            target = next((candidate.resolve() for candidate in candidates if candidate.is_file()), None)
            if target in relative:
                graph[source_rel].add(relative[target])

    cad_header = "src/commands/CadCommands.hpp"
    largest = sorted(({"path": relative[p], "bytes": p.stat().st_size, "lines": line_counts[p]}
                      for p in files), key=lambda item: (-item["bytes"], item["path"]))[:20]
    qt_outside = sorted(relative[p] for p, text in texts.items()
                        if not relative[p].startswith("src/qt/")
                        and QT_INCLUDE.search(_strip_cpp_comments(text)))
    gl_outside = sorted(relative[p] for p, text in texts.items()
                        if not relative[p].startswith("src/render/")
                        and (OPENGL.search(_strip_cpp_comments_and_literals(text))
                             or OPENGL_INCLUDE.search(_strip_cpp_comments(text))))
    cad_header_fan_in = sum(cad_header in neighbors for neighbors in graph.values())
    return {
        "source_file_count": len(files),
        "largest_files": largest,
        "folder_line_counts": dict(sorted(folder_counts.items())),
        "include_graph": {node: sorted(neighbors) for node, neighbors in sorted(graph.items())},
        "cad_commands_header_fan_in": cad_header_fan_in,
        "dependency_cycles": _cycles(graph),
        "qt_includes_outside_qt": {"count": len(qt_outside), "files": qt_outside},
        "opengl_outside_rendering": {"count": len(gl_outside), "files": gl_outside},
    }


def render_report(metrics):
    lines = ["Architecture inventory", "", f"Source files: {metrics['source_file_count']}",
             f"CadCommands.hpp include fan-in: {metrics['cad_commands_header_fan_in']}", "",
             "Largest files (bytes; lines):"]
    lines.extend(f"  {row['path']}: {row['bytes']}; {row['lines']}" for row in metrics["largest_files"])
    lines.extend(["", "Per-folder line counts:"])
    lines.extend(f"  {folder}: {count}" for folder, count in metrics["folder_line_counts"].items())
    lines.extend(["", f"Dependency cycles: {len(metrics['dependency_cycles'])}"])
    lines.extend(f"  {', '.join(cycle)}" for cycle in metrics["dependency_cycles"])
    edge_count = sum(len(neighbors) for neighbors in metrics["include_graph"].values())
    lines.extend(["", f"Include graph: {len(metrics['include_graph'])} files, {edge_count} edges"])
    lines.extend(f"  {source} -> {', '.join(targets)}"
                 for source, targets in metrics["include_graph"].items() if targets)
    for key, label in (("qt_includes_outside_qt", "Files including Qt outside src/qt"),
                       ("opengl_outside_rendering", "Files using OpenGL outside src/render")):
        finding = metrics[key]
        lines.extend(["", f"{label}: {finding['count']}"])
        lines.extend(f"  {path}" for path in finding["files"])
    return "\n".join(lines) + "\n"


def write_report(root, output_dir):
    output_dir = Path(output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    metrics = collect_metrics(root)
    report_path = output_dir / "architecture-inventory.txt"
    metrics_path = output_dir / "architecture-inventory.json"
    report_path.write_text(render_report(metrics), encoding="utf-8")
    metrics_path.write_text(json.dumps(metrics, indent=2) + "\n", encoding="utf-8")
    return report_path, metrics_path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1], help="repository root")
    parser.add_argument("--output-dir", type=Path, help="report directory (default: <root>/build/artifacts/architecture-inventory)")
    args = parser.parse_args()
    output = args.output_dir or args.root / "build/artifacts/architecture-inventory"
    report, metrics = write_report(args.root, output)
    print(report.read_text(encoding="utf-8"), end="")
    print(f"\nMetrics JSON: {metrics}")


if __name__ == "__main__":
    main()
