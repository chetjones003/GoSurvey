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
CPP_COMMENTS_AND_LITERALS = re.compile(
    r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|(?<![0-9A-Fa-f])\'(?:\\.|[^\'\\\n])*\'',
    re.DOTALL,
)
CPP_COMMENTS_AND_STRINGS = re.compile(
    r'"(?:\\.|[^"\\])*"|(?<![0-9A-Fa-f])\'(?:\\.|[^\'\\\n])*\'|//[^\n]*|/\*.*?\*/',
    re.DOTALL,
)


def _strip_cpp_comments_and_literals(text):
    return CPP_COMMENTS_AND_LITERALS.sub(lambda match: "\n" * match.group(0).count("\n"), text)


def _strip_cpp_comments(text):
    def preserve_literals_remove_comments(match):
        value = match.group(0)
        if value.startswith(("//", "/*")):
            return "\n" * value.count("\n")
        return value

    return CPP_COMMENTS_AND_STRINGS.sub(preserve_literals_remove_comments, text)


def _cycles(graph):
    index = 0
    stack = []
    on_stack = set()
    indexes = {}
    low = {}
    found = []

    def visit(node):
        nonlocal index
        indexes[node] = low[node] = index
        index += 1
        stack.append(node)
        on_stack.add(node)
        for neighbor in graph[node]:
            if neighbor not in indexes:
                visit(neighbor)
                low[node] = min(low[node], low[neighbor])
            elif neighbor in on_stack:
                low[node] = min(low[node], indexes[neighbor])
        if low[node] == indexes[node]:
            component = []
            while True:
                member = stack.pop()
                on_stack.remove(member)
                component.append(member)
                if member == node:
                    break
            if len(component) > 1 or node in graph[node]:
                found.append(sorted(component))

    for node in graph:
        if node not in indexes:
            visit(node)
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
    include_counts = defaultdict(int)

    for path, text in texts.items():
        uncommented = _strip_cpp_comments(text)
        source_rel = relative[path]
        folder_counts[Path(source_rel).parent.as_posix()] += line_counts[path]
        for included in INCLUDE.findall(uncommented):
            candidates = (path.parent / included, *(directory / included for directory in include_dirs), root / included)
            target = next((candidate.resolve() for candidate in candidates if candidate.is_file()), None)
            if target in relative:
                graph[source_rel].add(relative[target])
                include_counts[relative[target]] += 1

    cad_header = "src/commands/CadCommands.hpp"
    largest = sorted(({"path": relative[p], "bytes": p.stat().st_size, "lines": line_counts[p]}
                      for p in files), key=lambda item: (-item["bytes"], item["path"]))[:20]
    qt_outside = sorted(relative[p] for p, text in texts.items()
                        if "/qt/" not in relative[p] and QT_INCLUDE.search(_strip_cpp_comments(text)))
    gl_outside = sorted(relative[p] for p, text in texts.items()
                        if "/render/" not in relative[p]
                        and (OPENGL.search(_strip_cpp_comments_and_literals(text))
                             or OPENGL_INCLUDE.search(_strip_cpp_comments(text))))
    return {
        "source_file_count": len(files),
        "largest_files": largest,
        "folder_line_counts": dict(sorted(folder_counts.items())),
        "include_graph": {node: sorted(neighbors) for node, neighbors in sorted(graph.items())},
        "cad_commands_header_fan_in": include_counts[cad_header],
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
