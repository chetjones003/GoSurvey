#!/usr/bin/env python3
"""Generate one wiki page per registered command under resources/wiki/Commands/."""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WIKI = ROOT / "resources" / "wiki"
COMMANDS_DIR = WIKI / "Commands"
REGISTRY_CPP = ROOT / "src" / "commands" / "CadCommands.cpp"
COMMAND_REF = WIKI / "Command-Reference.md"

SKIP_SCAN = {
    "Command-Reference",
    "Home",
    "Getting-Started",
    "FAQ",
    "Known-Limitations",
    "Troubleshooting",
    "Workflows",
    "Keyboard-Shortcuts",
}

# Command primary -> Command-Reference subsection title.
CATEGORIES: dict[str, str] = {
    # Draw
    "line": "Draw",
    "circle": "Draw",
    "polyline": "Draw",
    "3dpoly": "Draw",
    "rect": "Draw",
    "arc": "Draw",
    "ellipse": "Draw",
    "hatch": "Draw",
    "featureline": "Draw",
    "featurelinelist": "Draw",
    # Solids and 3D
    "box": "Solids and 3D",
    "wedge": "Solids and 3D",
    "pyramid": "Solids and 3D",
    "cylinder": "Solids and 3D",
    "cone": "Solids and 3D",
    "sphere": "Solids and 3D",
    "torus": "Solids and 3D",
    "presspull": "Solids and 3D",
    "solidlist": "Solids and 3D",
    "section": "Solids and 3D",
    "solidcheck": "Solids and 3D",
    "polysolid": "Solids and 3D",
    "isolines": "Solids and 3D",
    "extrude": "Solids and 3D",
    "revolve": "Solids and 3D",
    "slice": "Solids and 3D",
    "loft": "Solids and 3D",
    "sweep": "Solids and 3D",
    "union": "Solids and 3D",
    "subtract": "Solids and 3D",
    "intersect": "Solids and 3D",
    # Annotation
    "text": "Annotation",
    "mtext": "Annotation",
    "dimaligned": "Annotation",
    "dimlinear": "Annotation",
    "dimangular": "Annotation",
    "dimsty": "Annotation",
    "style": "Annotation",
    # Modify
    "move": "Modify",
    "copy": "Modify",
    "rotate": "Modify",
    "scale": "Modify",
    "mirror": "Modify",
    "array": "Modify",
    "lengthen": "Modify",
    "extend": "Modify",
    "break": "Modify",
    "stretch": "Modify",
    "fillet": "Modify",
    "chamfer": "Modify",
    "delete": "Modify",
    "join": "Modify",
    "trim": "Modify",
    "offset": "Modify",
    "overkill": "Modify",
    "align": "Modify",
    "paste": "Modify",
    "pasteorig": "Modify",
    "trimstate": "Modify",
    "explode": "Modify",
    # Selection and visibility
    "select": "Selection and visibility",
    "quickselect": "Selection and visibility",
    "selectsimilar": "Selection and visibility",
    "isolateobjects": "Selection and visibility",
    "hideobjects": "Selection and visibility",
    "unisolateobjects": "Selection and visibility",
    # View and navigation
    "zoomextents": "View and navigation",
    "zoomwindow": "View and navigation",
    "pan": "View and navigation",
    "orbit": "View and navigation",
    "visualstyle": "View and navigation",
    "perspective": "View and navigation",
    "fov": "View and navigation",
    "crosshair3d": "View and navigation",
    "regen": "View and navigation",
    "view": "View and navigation",
    "plan": "View and navigation",
    "ucs": "View and navigation",
    "ucsfollow": "View and navigation",
    "gizmo": "View and navigation",
    # Layers and properties
    "layer": "Layers and properties",
    "vpfreeze": "Layers and properties",
    "vpthaw": "Layers and properties",
    # Survey and COGO
    "createpoints": "Survey and COGO",
    "viewpoints": "Survey and COGO",
    "importpoints": "Survey and COGO",
    "exportpoints": "Survey and COGO",
    "traverse": "Survey and COGO",
    "inverse": "Survey and COGO",
    "surfelev": "Survey and COGO",
    "id": "Survey and COGO",
    "dist": "Survey and COGO",
    # Surfaces and volumes
    "designatebreakline": "Surfaces and volumes",
    "designatecontour": "Surfaces and volumes",
    "designateboundary": "Surfaces and volumes",
    "surfacecreate": "Surfaces and volumes",
    "surfacecreategrid": "Surfaces and volumes",
    "surfacecreatecorr": "Surfaces and volumes",
    "surfacecreatevolgrid": "Surfaces and volumes",
    "surfswapedge": "Surfaces and volumes",
    "surfaceaddpoint": "Surfaces and volumes",
    "surfacemovepoint": "Surfaces and volumes",
    "surfdelline": "Surfaces and volumes",
    "surfacedelpoint": "Surfaces and volumes",
    "quickprofile": "Surfaces and volumes",
    "volreport": "Surfaces and volumes",
    "voltable": "Surfaces and volumes",
    "volcsv": "Surfaces and volumes",
    "volumesurface": "Surfaces and volumes",
    "surfacerename": "Surfaces and volumes",
    "surfacedelete": "Surfaces and volumes",
    "surfacerebuild": "Surfaces and volumes",
    "surfacelist": "Surfaces and volumes",
    "surfacestats": "Surfaces and volumes",
    "watershed": "Surfaces and volumes",
    "waterdrop": "Surfaces and volumes",
    "catchment": "Surfaces and volumes",
    "undesignate": "Surfaces and volumes",
    "surfaceaddfile": "Surfaces and volumes",
    "surfaceimportfile": "Surfaces and volumes",
    "flelev": "Surfaces and volumes",
    "flelevedit": "Surfaces and volumes",
    "extract": "Surfaces and volumes",
    "volumes": "Surfaces and volumes",
    "voldash": "Surfaces and volumes",
    "surfstyle": "Surfaces and volumes",
    # Paper space and plotting
    "mview": "Paper space and plotting",
    "mspace": "Paper space and plotting",
    "pspace": "Paper space and plotting",
    "plotscale": "Paper space and plotting",
    # Blocks and attributes
    "block": "Blocks and attributes",
    "insert": "Blocks and attributes",
    "bedit": "Blocks and attributes",
    "bsave": "Blocks and attributes",
    "bclose": "Blocks and attributes",
    "bsaveas": "Blocks and attributes",
    "attdef": "Blocks and attributes",
    "attedit": "Blocks and attributes",
    "attsync": "Blocks and attributes",
    "attext": "Blocks and attributes",
    "blocklist": "Blocks and attributes",
    "blockstats": "Blocks and attributes",
    "purge": "Blocks and attributes",
    "wblock": "Blocks and attributes",
    "blockimport": "Blocks and attributes",
    "blocklib": "Blocks and attributes",
    "blocksearch": "Blocks and attributes",
    "blockfav": "Blocks and attributes",
    "blockrecent": "Blocks and attributes",
    "makeblock": "Blocks and attributes",
    "mkline": "Blocks and attributes",
    "selline": "Blocks and attributes",
    "selblock": "Blocks and attributes",
    "movesel": "Blocks and attributes",
    "copysel": "Blocks and attributes",
    "rotatesel": "Blocks and attributes",
    "scalesel": "Blocks and attributes",
    "mirrorsel": "Blocks and attributes",
    "copyclip": "Blocks and attributes",
    "pasteblock": "Blocks and attributes",
    "blockmodel": "Blocks and attributes",
    "blockpaper": "Blocks and attributes",
    # Import and reference geometry
    "pdfattach": "Import and reference geometry",
    "importmodel": "Import and reference geometry",
    # Settings and utilities
    "units": "Settings and utilities",
    "elev": "Settings and utilities",
    "help": "Settings and utilities",
    "options": "Settings and utilities",
    # Diagnostics
    "bench": "Diagnostics",
}

CATEGORY_OVERVIEW: dict[str, str] = {
    "Draw": "Drawing Tools",
    "Solids and 3D": "Drawing Tools",
    "Annotation": "Annotation",
    "Modify": "Modify Tools",
    "Selection and visibility": "Object Selection",
    "View and navigation": "Views and Navigation",
    "Layers and properties": "Layers",
    "Survey and COGO": "Survey Points",
    "Surfaces and volumes": "Point Groups and Surfaces",
    "Paper space and plotting": "Paper Space and Layouts",
    "Blocks and attributes": "Import and Export",
    "Import and reference geometry": "Import and Export",
    "Settings and utilities": "Settings and Options",
    "Diagnostics": "Command Reference",
}

CATEGORY_ORDER = [
    "Draw",
    "Solids and 3D",
    "Annotation",
    "Modify",
    "Selection and visibility",
    "View and navigation",
    "Layers and properties",
    "Survey and COGO",
    "Surfaces and volumes",
    "Paper space and plotting",
    "Blocks and attributes",
    "Settings and utilities",
    "Import and reference geometry",
    "Diagnostics",
]


def parse_registry() -> list[tuple[str, str, str]]:
    text = REGISTRY_CPP.read_text(encoding="utf-8")
    start = text.find("const CmdEntry kRegistry[]")
    if start < 0:
        raise SystemExit("kRegistry not found in CadCommands.cpp")
    block = text[start : text.find("};", start)]
    entries: list[tuple[str, str, str]] = []
    for m in re.finditer(r'\{"([^"]+)",\s*"([^"]*)",\s*"([^"]*)"', block):
        entries.append((m.group(1), m.group(2), m.group(3)))
    if not entries:
        raise SystemExit("No registry entries parsed")
    return entries


def format_aliases(aliases: str) -> str:
    parts = [p.strip() for p in aliases.split(",") if p.strip()]
    if not parts:
        return "—"
    return ", ".join(f"`{p.upper()}`" for p in parts)


def format_command_name(primary: str) -> str:
    return primary.upper()


def wiki_link_commands(primary: str) -> str:
    return f"[[Commands/{primary}]]"


def stem_to_title(stem: str) -> str:
    return stem.replace("-", " ")


def parse_heading_for_command(heading: str, registry: set[str]) -> str | None:
    h = heading[3:].strip() if heading.startswith("## ") else heading.strip()

    tail = re.search(r"[—–-]\s*([A-Z0-9]+)", h)
    if tail:
        candidate = tail.group(1).lower()
        if candidate in registry:
            return candidate

    name = re.split(r"\s*[—–-]\s+", h, maxsplit=1)[0].strip()
    name = re.sub(r"\s*\(`[^`]+`(?:,\s*`[^`]+`)*\)\s*$", "", name)
    name = re.sub(r"\s*\([^)]*\)\s*$", "", name)
    token = name.replace(" ", "").lower()
    if token in registry:
        return token

    first = name.split()[0].lower() if name else ""
    if first in registry:
        return first
    return None


def extract_sections(registry: set[str]) -> dict[str, dict[str, str]]:
    sections: dict[str, dict[str, str]] = {}
    for path in sorted(WIKI.glob("*.md")):
        if path.stem in SKIP_SCAN or path.stem == "Commands":
            continue
        lines = path.read_text(encoding="utf-8").splitlines()
        i = 0
        while i < len(lines):
            line = lines[i]
            if line.startswith("## ") and not line.startswith("### "):
                cmd = parse_heading_for_command(line, registry)
                if cmd and cmd not in sections:
                    body: list[str] = []
                    i += 1
                    while i < len(lines):
                        nxt = lines[i]
                        if nxt.startswith("## ") and not nxt.startswith("### "):
                            break
                        body.append(nxt)
                        i += 1
                    sections[cmd] = {
                        "body": "\n".join(body).strip(),
                        "source_stem": path.stem,
                    }
                    continue
            i += 1
    return sections


def build_command_page(
    primary: str,
    aliases: str,
    description: str,
    section: dict[str, str] | None,
    category: str,
) -> str:
    name = format_command_name(primary)
    alias_col = format_aliases(aliases)
    overview = CATEGORY_OVERVIEW.get(category, "Command Reference")
    lines = [
        f"# {name}",
        "",
        f"**Command:** `{name}`" + (f" — aliases: {alias_col}" if alias_col != "—" else ""),
        f"**Category overview:** [[{overview}]]",
        "",
    ]
    if section and section.get("body"):
        lines.append(section["body"])
        lines.extend(["", "---", ""])
    else:
        lines.extend(
            [
                description.strip() or f"Documentation for `{name}`.",
                "",
                f"See [[{overview}]] for related tools and workflows.",
                "",
                "---",
                "",
            ]
        )
    lines.extend(
        [
            "## Related",
            "",
            f"- [[{overview}]]",
            "- [[Command Reference]]",
        ]
    )
    return "\n".join(lines) + "\n"


def rewrite_command_reference(registry: list[tuple[str, str, str]]) -> None:
    original = COMMAND_REF.read_text(encoding="utf-8")
    tail_start = original.find("## Commands that accept arguments on one line")
    if tail_start < 0:
        raise SystemExit("Command-Reference tail section not found")
    tail = original[tail_start:]

    intro_end = original.find("## By category")
    if intro_end < 0:
        raise SystemExit("Command-Reference intro not found")
    intro = original[:intro_end].rstrip() + "\n\n"

    by_cat: dict[str, list[tuple[str, str, str]]] = {c: [] for c in CATEGORY_ORDER}
    for primary, aliases, description in registry:
        cat = CATEGORIES.get(primary)
        if not cat:
            raise SystemExit(f"No category for command: {primary}")
        by_cat[cat].append((primary, aliases, description))

    out: list[str] = [intro, "## By category", ""]
    for cat in CATEGORY_ORDER:
        rows = sorted(by_cat[cat], key=lambda r: r[0])
        if not rows:
            continue
        out.append(f"### {cat}")
        out.append("")
        if cat == "Diagnostics":
            out.append("| Command | Aliases | Description |")
            out.append("|---|---|---|")
            for primary, aliases, description in rows:
                out.append(
                    f"| `{format_command_name(primary)}` | {format_aliases(aliases)} | "
                    f"{description.replace('|', '\\|')} |"
                )
        else:
            out.append("| Command | Aliases | Description | Details |")
            out.append("|---|---|---|---|")
            for primary, aliases, description in rows:
                details = wiki_link_commands(primary) if primary != "help" else "—"
                out.append(
                    f"| `{format_command_name(primary)}` | {format_aliases(aliases)} | "
                    f"{description.replace('|', '\\|')} | {details} |"
                )
        out.append("")

    out.extend(["---", "", "## Alphabetical index", ""])
    out.append("| Command | Aliases | Page |")
    out.append("|---|---|---|")
    for primary, aliases, description in sorted(registry, key=lambda r: r[0]):
        page = wiki_link_commands(primary)
        out.append(
            f"| `{format_command_name(primary)}` | {format_aliases(aliases)} | {page} |"
        )
    out.extend(["", "---", "", tail.lstrip()])
    COMMAND_REF.write_text("\n".join(out), encoding="utf-8")


def generate_command_images() -> None:
    import subprocess

    script = ROOT / "dev" / "generate-wiki-command-images.py"
    subprocess.run([sys.executable, str(script)], check=True, cwd=ROOT / "dev")


def main() -> int:
    registry_list = parse_registry()
    registry_set = {p for p, _, _ in registry_list}
    missing = [p for p, _, _ in registry_list if p not in CATEGORIES]
    if missing:
        raise SystemExit(f"Missing category mapping for: {', '.join(missing)}")

    sections = extract_sections(registry_set)
    COMMANDS_DIR.mkdir(parents=True, exist_ok=True)

    for path in COMMANDS_DIR.glob("*.md"):
        path.unlink()

    for primary, aliases, description in registry_list:
        page = build_command_page(
            primary,
            aliases,
            description,
            sections.get(primary),
            CATEGORIES[primary],
        )
        (COMMANDS_DIR / f"{primary}.md").write_text(page, encoding="utf-8")

    rewrite_command_reference(registry_list)
    generate_command_images()

    extracted = len(sections)
    print(
        f"Generated {len(registry_list)} command pages in {COMMANDS_DIR.relative_to(ROOT)} "
        f"({extracted} with extracted body text from category pages)."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
