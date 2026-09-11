#!/usr/bin/env python3
"""Generate schematic command help images and embed them on wiki command pages."""

from __future__ import annotations

import re
import sys
from pathlib import Path

from wiki_command_image_lib import CardSpec, render_card, tagline_from_description, title_for_primary

ROOT = Path(__file__).resolve().parents[1]
WIKI = ROOT / "resources" / "wiki"
IMAGES = WIKI / "images" / "commands"
COMMANDS_DIR = WIKI / "Commands"
REGISTRY_CPP = ROOT / "src" / "commands" / "CadCommands.cpp"


def _wf(*steps: str) -> tuple[str, dict]:
    return ("workflow", {"steps": list(steps)})


def _dlg(*steps: str) -> tuple[str, dict]:
    return ("dialog", {"steps": list(steps)})


def _list(title: str, *items: str) -> tuple[str, dict]:
    return ("list_log", {"title": title, "items": list(items)})


# primary -> (diagram kind, optional params)
VISUALS: dict[str, tuple[str, dict]] = {
    "line": ("line", {}),
    "circle": ("circle", {}),
    "polyline": ("polyline", {}),
    "3dpoly": ("polyline", {}),
    "rect": ("rect", {}),
    "arc": ("arc", {}),
    "ellipse": ("ellipse", {}),
    "hatch": ("hatch", {}),
    "featureline": ("polyline", {}),
    "featurelinelist": _list("Feature lines", "FL-ROAD  — 12 vertices", "FL-DITCH — 8 vertices"),
    "box": ("box_solid", {"solid": "box"}),
    "wedge": ("box_solid", {"solid": "box"}),
    "pyramid": ("box_solid", {"solid": "cone"}),
    "cylinder": ("box_solid", {"solid": "cylinder"}),
    "cone": ("box_solid", {"solid": "cone"}),
    "sphere": ("box_solid", {"solid": "sphere"}),
    "torus": ("box_solid", {"solid": "sphere"}),
    "presspull": ("presspull", {}),
    "solidlist": _list("Solids", "BOX — vol 1250 cu", "CYL — vol 890 cu"),
    "section": ("section", {}),
    "solidcheck": _dlg("Checks solids for closed, manifold, oriented topology"),
    "polysolid": ("polysolid", {}),
    "isolines": ("box_solid", {"solid": "cylinder"}),
    "extrude": ("extrude", {}),
    "revolve": ("revolve", {}),
    "slice": ("slice", {}),
    "loft": ("loft", {}),
    "sweep": ("sweep", {}),
    "union": ("boolean", {"mode": "union"}),
    "subtract": ("boolean", {"mode": "subtract"}),
    "intersect": ("boolean", {"mode": "intersect"}),
    "text": ("text", {}),
    "mtext": ("mtext", {}),
    "dimaligned": ("dimension", {"dim": "aligned"}),
    "dimlinear": ("dimension", {"dim": "linear"}),
    "dimangular": ("dimension", {"dim": "angular"}),
    "dimsty": _dlg("Open the dimension style editor", "Set units, arrows, text height"),
    "style": _dlg("Open the text style manager", "Create or edit named text styles"),
    "move": ("move", {}),
    "copy": ("copy", {}),
    "rotate": ("rotate", {}),
    "scale": ("scale", {}),
    "mirror": ("mirror", {}),
    "array": ("array", {}),
    "lengthen": ("extend", {}),
    "extend": ("extend", {}),
    "break": ("break", {}),
    "stretch": ("stretch", {}),
    "fillet": ("fillet", {}),
    "chamfer": ("chamfer", {}),
    "delete": ("delete", {}),
    "join": ("join", {}),
    "trim": ("trim", {}),
    "offset": ("offset", {}),
    "overkill": ("select", {}),
    "align": ("move", {}),
    "paste": ("copy", {}),
    "pasteorig": ("copy", {}),
    "trimstate": _dlg("Set TRIM mode: draw a cutting line or pick edges"),
    "explode": ("block", {"mode": "def"}),
    "select": ("select", {}),
    "quickselect": ("select", {}),
    "selectsimilar": ("select", {}),
    "isolateobjects": ("hide", {"isolate": True}),
    "hideobjects": ("hide", {"isolate": False}),
    "unisolateobjects": ("hide", {"isolate": True}),
    "zoomextents": ("view", {"view": "zoom_extents"}),
    "zoomwindow": ("view", {"view": "zoom_window"}),
    "pan": ("view", {"view": "pan"}),
    "orbit": ("view", {"view": "orbit"}),
    "visualstyle": ("view", {"view": "generic"}),
    "perspective": ("view", {"view": "orbit"}),
    "fov": ("view", {"view": "orbit"}),
    "crosshair3d": ("view", {"view": "generic"}),
    "regen": ("view", {"view": "generic"}),
    "view": _dlg("Save, restore, or delete named views", "Or open the View Manager"),
    "plan": ("view", {"view": "generic"}),
    "ucs": _wf("Define or restore a User Coordinate System", "Pick origin and X direction"),
    "ucsfollow": _dlg("Toggle whether the view follows UCS changes"),
    "gizmo": ("view", {"view": "orbit"}),
    "layer": ("layer", {}),
    "vpfreeze": ("layer", {}),
    "vpthaw": ("layer", {}),
    "createpoints": ("points", {}),
    "viewpoints": _dlg("Open the survey points table", "Edit numbers, coordinates, descriptions"),
    "importpoints": ("file", {"import": True}),
    "exportpoints": ("file", {"import": False}),
    "inverse": ("measure", {"measure": "inverse"}),
    "surfelev": ("surface", {}),
    "id": ("measure", {"measure": "id"}),
    "dist": ("measure", {"measure": "dist"}),
    "designatebreakline": _wf("Pick a line or polyline on the surface", "It becomes a breakline constraint"),
    "designatecontour": _wf("Pick a contour source line", "Surface follows it when rebuilt"),
    "designateboundary": _wf("Pick a closed boundary polyline", "Clip or constrain the surface"),
    "surfacecreate": _wf("Name the new surface", "Choose point groups to include"),
    "surfacecreategrid": _wf("Set grid origin and spacing", "Enter elevations for each grid node"),
    "surfacecreatecorr": _wf("Name the corridor surface", "Pick alignment and profile data"),
    "surfacecreatevolgrid": _wf("Pick base and comparison surfaces", "Build a volume grid surface"),
    "surfswapedge": _wf("Pick a surface", "Click an interior TIN edge to flip"),
    "surfaceaddpoint": _wf("Pick the surface", "Click or type a new definition point"),
    "surfacemovepoint": _wf("Pick the surface and a point", "Move the point to a new location"),
    "surfdelline": _wf("Pick the surface", "Delete an interior TIN edge"),
    "surfacedelpoint": _wf("Pick the surface near a point", "Remove the nearest definition point"),
    "quickprofile": _wf("Pick two plan points", "Profile is sampled along the surface"),
    "volreport": _dlg("Insert MTEXT of the last volume report"),
    "voltable": _dlg("Insert a TABLE of the last volume report"),
    "volcsv": ("file", {"import": False}),
    "volumesurface": _wf("Name the volume surface", "Pick base and comparison TINs"),
    "surfacerename": _wf("Type the old surface name", "Type the new name"),
    "surfacedelete": _wf("Type the surface name to remove"),
    "surfacerebuild": _wf("Rebuild one named surface", "Or rebuild all if no name is given"),
    "surfacelist": _list("Surfaces", "EG  — 842 pts, 1,604 tris", "FG  — 1,203 pts, 2,310 tris"),
    "surfacestats": _list("Surface stats", "Points: 842", "Triangles: 1,604", "Breaklines: 3"),
    "watershed": _wf("Pick a surface", "Delineate watershed basins"),
    "waterdrop": _wf("Pick a surface and a start point", "Trace the steepest descent path"),
    "catchment": _wf("Pick a surface and outlet point", "Delineate the upstream catchment"),
    "undesignate": _wf("Pick surface and definition type", "Remove one linked breakline, boundary, or file"),
    "surfaceaddfile": ("file", {"import": True}),
    "surfaceimportfile": ("file", {"import": True}),
    "flelev": _wf("Pick or name a feature line", "Set, raise, or edit vertex elevations"),
    "flelevedit": _dlg("Open the feature line elevation editor"),
    "extract": _wf("Pick a surface", "Bake displayed contours to polylines"),
    "volumes": _wf("Pick base and comparison surfaces", "Cut / fill / net volume is computed"),
    "voldash": _dlg("Open the live volume dashboard between two surfaces"),
    "surfstyle": _dlg("Edit surface display: contours, triangles, border"),
    "mview": ("view", {"view": "mview"}),
    "mspace": ("view", {"view": "mview"}),
    "pspace": ("view", {"view": "mview"}),
    "plotscale": _dlg("Set model units per plotted inch", "Example: 50 → 1\" = 50'"),
    "block": ("block", {"mode": "def"}),
    "insert": ("block", {"mode": "insert"}),
    "bedit": _wf("Type the block name", "Edit geometry inside the block editor"),
    "bsave": _wf("Save changes to the block being edited"),
    "bclose": _wf("Close the block editor", "Return to the drawing"),
    "bsaveas": _wf("Save the edited block under a new name"),
    "attdef": ("text", {}),
    "attedit": ("text", {}),
    "attsync": _wf("Pick block inserts", "Sync attributes from their definitions"),
    "attext": ("file", {"import": False}),
    "blocklist": _list("Block definitions", "TREE", "MANHOLE", "TITLE"),
    "blockstats": _list("Block stats", "TREE — 14 refs, 8 entities", "MANHOLE — 3 refs"),
    "purge": _wf("Remove unused block definitions from the drawing"),
    "wblock": ("file", {"import": False}),
    "blockimport": ("file", {"import": True}),
    "blocklib": _dlg("Browse the drawing block library with previews"),
    "blocksearch": _dlg("Search block names in the drawing"),
    "blockfav": _dlg("Manage favorite block definitions"),
    "blockrecent": _dlg("Show recently inserted blocks"),
    "makeblock": _wf("Type a new block name", "Create an empty block definition"),
    "mkline": ("line", {}),
    "selline": ("select", {}),
    "selblock": ("select", {}),
    "movesel": ("move", {}),
    "copysel": ("copy", {}),
    "rotatesel": ("rotate", {}),
    "scalesel": ("scale", {}),
    "mirrorsel": ("mirror", {}),
    "copyclip": ("copy", {}),
    "pasteblock": ("block", {"mode": "insert"}),
    "blockmodel": ("view", {"view": "mview"}),
    "blockpaper": ("view", {"view": "mview"}),
    "pdfattach": ("pdf", {}),
    "importmodel": ("file", {"import": True}),
    "units": _dlg("Set linear and angular display units", "Choose precision and format"),
    "elev": _wf("Set the work-plane elevation for new geometry", "Type W to return to world Z=0"),
    "help": _dlg("Print a compact command list to the log"),
    "bench": _dlg("Run a frame-budget benchmark", "For performance testing, not drafting"),
}


def parse_registry() -> list[tuple[str, str, str]]:
    text = REGISTRY_CPP.read_text(encoding="utf-8")
    start = text.find("const CmdEntry kRegistry[]")
    block = text[start : text.find("};", start)]
    return re.findall(r'\{"([^"]+)",\s*"([^"]*)",\s*"([^"]*)"', block)


def image_filename(primary: str) -> str:
    return f"commands/{primary}.png"


def image_markdown(primary: str, title: str) -> str:
    return f"![{title} command overview](wiki-img:{image_filename(primary)})"


def card_for(primary: str, description: str) -> CardSpec:
    kind, params = VISUALS.get(primary, ("generic", {}))
    title = title_for_primary(primary)
    return CardSpec(
        primary=primary,
        title=title,
        tagline=tagline_from_description(description),
        kind=kind,
        params=dict(params),
    )


def render_all() -> dict[str, str]:
    IMAGES.mkdir(parents=True, exist_ok=True)
    registry = parse_registry()
    out: dict[str, str] = {}
    for path in IMAGES.glob("*.png"):
        path.unlink()

    for primary, _aliases, description in registry:
        spec = card_for(primary, description)
        img = render_card(spec)
        dest = IMAGES / f"{primary}.png"
        img.save(dest, "PNG")
        out[primary] = image_filename(primary)
    return out


def embed_images_in_pages() -> None:
    registry = parse_registry()
    for primary, _aliases, description in registry:
        page = COMMANDS_DIR / f"{primary}.md"
        if not page.exists():
            continue
        title = title_for_primary(primary)
        img_line = image_markdown(primary, title)
        text = page.read_text(encoding="utf-8")
        text = re.sub(r"\n!\[[^\]]*\]\(wiki-img:commands/[^\)]+\)\n?", "\n", text)
        lines = text.splitlines()
        insert_at = 1
        for i, ln in enumerate(lines):
            if ln.startswith("# "):
                insert_at = i + 1
                break
        while insert_at < len(lines) and lines[insert_at].strip() == "":
            insert_at += 1
        lines.insert(insert_at, "")
        lines.insert(insert_at + 1, img_line)
        text = "\n".join(lines) + ("\n" if not text.endswith("\n") else "")
        page.write_text(text, encoding="utf-8")


def main() -> int:
    paths = render_all()
    embed_images_in_pages()
    print(f"Generated {len(paths)} command images in resources/wiki/images/commands/")
    return 0


if __name__ == "__main__":
    sys.exit(main())
