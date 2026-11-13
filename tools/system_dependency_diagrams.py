"""Generate the per-system and feature dependency diagrams for the target architecture.

System edges come from the "Allowed dependencies" column of docs/architecture/system-map.md,
which stays authoritative. Feature edges below are a proposal; each one must lie on an allowed
system edge, and the run fails if one does not.

Writes docs/architecture/system-dependencies.md (one Mermaid diagram per system, for both
levels) and a three-page US Letter print layout at
build/artifacts/diagrams/system-dependencies.html; print that page to PDF from a browser.

Usage, from the repository root:
    python tools/system_dependency_diagrams.py           # regenerate
    python tools/system_dependency_diagrams.py --check   # fail if the doc is out of date
"""
import argparse
import importlib.util
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
DOC = "docs/architecture/system-dependencies.md"
PRINT_PAGE = "build/artifacts/diagrams/system-dependencies.html"
MERMAID_URL = "https://cdn.jsdelivr.net/npm/mermaid@11.17.2/dist/mermaid.esm.min.mjs"

_spec = importlib.util.spec_from_file_location(
    "architecture_dependencies", pathlib.Path(__file__).with_name("architecture_dependencies.py"))
_architecture = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_architecture)


class DiagramError(Exception):
    """The diagram data disagrees with the system map."""


# ---------------------------------------------------------------- systems and features ------
# id: (system-map name, tier, [(feature id, label)])
SYSTEMS = {
    "editor": ("GoSurveyEditor", "APP", [("start", "startup · composition"), ("win", "windows"),
               ("dlg", "dialogs"), ("ribbon", "ribbon"), ("panels", "panels"), ("keys", "hotkeys")]),
    "interaction": ("Interaction", "T4", [("snap", "snap"), ("grips", "grips"),
                    ("prev", "previews"), ("pick", "picking")]),
    "commands": ("Commands", "T4", [("reg", "registry"), ("fam", "command families")]),
    "renderer": ("Renderer", "T3", [("cam", "camera"), ("lists", "draw lists"),
                 ("cache", "caches"), ("dev", "device interfaces")]),
    "io": ("IO", "T3", [("dxf", "DXF"), ("dwg", "DWG"), ("gltf", "glTF"), ("stl", "STL"),
           ("sat", "SAT"), ("pts", "point files"), ("starnet", "StarNet")]),
    "network": ("Network", "T3", [("tiles", "map tiles"), ("upd", "updates"),
                ("tel", "telemetry"), ("xport", "transport interface")]),
    "document": ("Document", "T2", [("ent", "entities"), ("lay", "layers"), ("blk", "blocks"),
                 ("tbl", "tables"), ("txt", "text"), ("undo", "undo"), ("sel", "selection")]),
    "survey": ("Survey", "T2", [("pts", "points"), ("trav", "traverse"),
               ("align", "alignment"), ("crs", "coordinate systems")]),
    "terrain": ("Terrain", "T2", [("tin", "TIN"), ("cont", "contours"), ("vol", "volumes"),
                ("day", "daylight")]),
    "pointcloud": ("PointCloud", "T2", [("store", "storage"), ("oct", "octree"),
                   ("cache", "cache")]),
    "pdf": ("Pdf", "T2", [("ann", "annotate"), ("meas", "measure"), ("cmp", "compare"),
            ("scale", "scale")]),
    "geometry": ("Geometry", "T1", [("g2d", "geom2d"), ("curves", "curves"), ("nurbs", "NURBS"),
                 ("brep", "B-rep"), ("mesh", "mesh")]),
    "core": ("Core", "T0", [("log", "logging"), ("set", "settings"), ("units", "units"),
             ("fmt", "formatting"), ("paths", "paths interface")]),
    "math": ("Math", "T0", [("vec", "vectors"), ("mat", "matrices"), ("rays", "rays"),
             ("ucs", "UCS")]),
    "events": ("Events", "T0", [("input", "input"), ("docchg", "document changes")]),
    "ui": ("UI", "T0", [("win", "window"), ("dlg", "dialogs"), ("prog", "progress"),
           ("clip", "clipboard"), ("timer", "timer")]),
    "debug": ("Debug", "T0", [("prof", "profiling"), ("trace", "traces"),
              ("inv", "invariants")]),
    "pqt": ("Qt", "PLATFORM", [("ui", "UI adapter"), ("input", "input translation")]),
    "pgl": ("OpenGL", "PLATFORM", [("dev", "render device")]),
    "pwin": ("Windows", "PLATFORM", [("paths", "paths"), ("http", "HTTP"), ("crash", "crash"),
             ("dlg", "native dialogs")]),
    "phead": ("Headless", "PLATFORM", [("dev", "null device"), ("dlg", "scripted dialogs"),
              ("input", "scripted input")]),
}
TIER_OF = {system: entry[1] for system, entry in SYSTEMS.items()}
ORDER = ["editor", "interaction", "commands", "renderer", "io", "network", "document", "survey",
         "terrain", "pointcloud", "pdf", "geometry", "math", "events", "ui", "debug", "core",
         "pqt", "pgl", "pwin", "phead"]
FEATURE_PAGE_SPLIT = ORDER.index("document")

# Platform -> engine edges where the backend implements the interface (drawn dotted).
SYSTEM_IMPLEMENTS = {("pqt", "ui"), ("pqt", "events"), ("pgl", "renderer"), ("pwin", "ui"),
                     ("pwin", "network"), ("phead", "events"), ("phead", "ui"),
                     ("phead", "renderer")}
FEATURE_IMPLEMENTS = {("pqt.ui", "ui"), ("pqt.input", "events.input"),
                      ("pgl.dev", "renderer.dev"), ("pwin.paths", "core.paths"),
                      ("pwin.http", "network.xport"), ("pwin.dlg", "ui.dlg"),
                      ("phead.dev", "renderer.dev"), ("phead.dlg", "ui.dlg"),
                      ("phead.input", "events.input")}

# (source feature, target feature or whole system). A proposal; see the module docstring.
FEATURE_EDGES = [
    # GoSurveyEditor: composes the engine; reaches the Domain only through Commands/Interaction.
    ("editor.start", "commands.reg"), ("editor.start", "pgl.dev"), ("editor.start", "pqt.ui"),
    ("editor.start", "pwin.crash"), ("editor.start", "network.upd"),
    ("editor.win", "renderer.cam"), ("editor.win", "interaction.pick"),
    ("editor.dlg", "pqt.ui"), ("editor.dlg", "io"), ("editor.ribbon", "commands.reg"),
    ("editor.keys", "commands.reg"), ("editor.panels", "commands.fam"),
    # Interaction
    ("interaction.snap", "document.ent"), ("interaction.snap", "survey.pts"),
    ("interaction.snap", "geometry.curves"), ("interaction.snap", "math.ucs"),
    ("interaction.grips", "document.ent"), ("interaction.grips", "commands.fam"),
    ("interaction.prev", "renderer.lists"), ("interaction.prev", "commands.fam"),
    ("interaction.pick", "renderer.cam"), ("interaction.pick", "math.rays"),
    ("interaction.pick", "document.sel"), ("interaction.pick", "pointcloud.oct"),
    ("interaction.pick", "events.input"), ("interaction.pick", "terrain.tin"),
    # Commands
    ("commands.reg", "ui.dlg"), ("commands.reg", "events.input"),
    ("commands.fam", "document.undo"), ("commands.fam", "document.ent"),
    ("commands.fam", "survey.trav"), ("commands.fam", "terrain.vol"),
    ("commands.fam", "pdf.meas"), ("commands.fam", "pointcloud.store"),
    ("commands.fam", "io"), ("commands.fam", "network.tiles"), ("commands.fam", "ui.prog"),
    ("commands.fam", "renderer.cam"),
    # Renderer
    ("renderer.cam", "math.mat"), ("renderer.cam", "math.ucs"),
    ("renderer.lists", "document.ent"), ("renderer.lists", "document.lay"),
    ("renderer.lists", "survey.pts"), ("renderer.lists", "terrain.cont"),
    ("renderer.lists", "pointcloud.oct"), ("renderer.lists", "pdf.ann"),
    ("renderer.lists", "geometry.mesh"), ("renderer.cache", "renderer.lists"),
    ("renderer.cache", "document.undo"), ("renderer.lists", "renderer.dev"),
    # IO
    ("io.dxf", "document"), ("io.dxf", "geometry.nurbs"), ("io.dwg", "document"),
    ("io.dwg", "geometry.nurbs"), ("io.gltf", "geometry.mesh"), ("io.stl", "geometry.mesh"),
    ("io.sat", "geometry.brep"), ("io.pts", "survey.pts"), ("io.pts", "pointcloud.store"),
    ("io.starnet", "survey.trav"),
    # Network
    ("network.tiles", "network.xport"), ("network.upd", "network.xport"),
    ("network.tel", "network.xport"), ("network.upd", "core.set"), ("network.tel", "core.log"),
    # Domain
    ("document.ent", "geometry"), ("document.blk", "document.ent"),
    ("document.lay", "document.ent"), ("document.txt", "core.fmt"),
    ("document.tbl", "document.txt"), ("document.undo", "events.docchg"),
    ("document.sel", "document.ent"),
    ("survey.pts", "document.ent"), ("survey.pts", "math.vec"), ("survey.trav", "survey.pts"),
    ("survey.trav", "core.units"), ("survey.align", "geometry.curves"),
    ("survey.crs", "math.mat"),
    ("terrain.tin", "survey.pts"), ("terrain.tin", "geometry.mesh"),
    ("terrain.cont", "terrain.tin"), ("terrain.cont", "geometry.curves"),
    ("terrain.cont", "document.ent"), ("terrain.vol", "terrain.tin"),
    ("terrain.day", "survey.align"), ("terrain.day", "terrain.tin"),
    ("pointcloud.oct", "pointcloud.store"), ("pointcloud.store", "math.vec"),
    ("pointcloud.cache", "core.set"),
    ("pdf.ann", "document.ent"), ("pdf.meas", "geometry.g2d"), ("pdf.cmp", "geometry.g2d"),
    ("pdf.scale", "math.mat"),
    # Kernel
    ("geometry.nurbs", "geometry.curves"), ("geometry.brep", "geometry.nurbs"),
    ("geometry.brep", "geometry.mesh"), ("geometry.curves", "math.vec"),
    ("geometry.mesh", "math.vec"), ("geometry.g2d", "math.vec"),
    # Foundation
    ("math.ucs", "math.mat"), ("math.rays", "math.vec"), ("math.mat", "math.vec"),
    ("events.input", "math.vec"), ("ui.win", "events.input"), ("ui.timer", "events.input"),
    ("debug.trace", "core.log"), ("debug.inv", "core.log"), ("debug.prof", "core.log"),
    ("core.fmt", "core.units"), ("core.set", "core.paths"),
    # Platform adapters (dotted where listed in FEATURE_IMPLEMENTS)
    ("pqt.ui", "ui"), ("pqt.input", "events.input"), ("pgl.dev", "renderer.dev"),
    ("pgl.dev", "math.mat"),
    ("pwin.paths", "core.paths"), ("pwin.http", "network.xport"), ("pwin.crash", "core.log"),
    ("pwin.dlg", "ui.dlg"),
    ("phead.dev", "renderer.dev"), ("phead.dlg", "ui.dlg"), ("phead.input", "events.input"),
]

# Forbidden examples shown in red on the system view: (source, target, why).
FORBIDDEN = [
    ("editor", "document", "reaches Domain only through Commands / Interaction"),
    ("editor", "phead", "Headless is for tests and batch runs"),
    ("commands", "interaction", "one-way: Interaction may use Commands"),
    ("interaction", "io", "Interaction also may not use Network or Pdf"),
    ("network", "survey", "Network may not use any Domain system"),
    ("renderer", "io", "no edges between Services"),
    ("document", "survey", "no reverse peer edge"),
    ("survey", "terrain", "no reverse peer edge"),
    ("pointcloud", "document", "PointCloud has no Domain peers"),
    ("geometry", "document", "Kernel never reaches up into Domain"),
    ("document", "pqt", "engine code never includes a backend"),
    ("renderer", "pgl", "engine code never includes a backend"),
]

# Arrows leaving a system use that system's colour; box fill shows the tier.
COLOR = {
    "editor": "#f8fafc", "interaction": "#e879f9", "commands": "#60a5fa", "renderer": "#22d3ee",
    "io": "#38bdf8", "network": "#2dd4bf", "document": "#86efac", "survey": "#facc15",
    "terrain": "#4ade80", "pointcloud": "#a3e635", "pdf": "#34d399", "geometry": "#fbbf24",
    "core": "#c4b5fd", "math": "#a78bfa", "events": "#c084fc", "ui": "#d8b4fe",
    "debug": "#ddd6fe", "pqt": "#f472b6", "pgl": "#fb923c", "pwin": "#94a3b8",
    "phead": "#d1d5db",
}
TIER_FILL = {"APP": "#1f2937", "T4": "#312e81", "T3": "#164e63", "T2": "#14532d",
             "T1": "#713f12", "T0": "#4c1d95", "PLATFORM": "#3f3f46"}
FORBIDDEN_COLOR = "#ef4444"
NEWLINE = "\n"


def system_of(ref):
    return ref.split(".")[0]


def label_of(ref):
    system = system_of(ref)
    if "." not in ref:
        return f"{SYSTEMS[system][0]} · all"
    return f"{SYSTEMS[system][0]} · {dict(SYSTEMS[system][2])[ref.split('.')[1]]}"


# ---------------------------------------------------------------- validation ----------------
def read_allowed(root=ROOT):
    """Return {system id: set of allowed system ids} from the system-map table."""
    by_name = {entry[0]: system for system, entry in SYSTEMS.items()}
    allowed = {}
    for name, row in _architecture._read_systems(root).items():
        if name not in by_name:
            raise DiagramError(f"system-map.md lists {name!r}, which the diagrams do not know")
        unknown = row["dependencies"] - by_name.keys()
        if unknown:
            raise DiagramError(f"{name} depends on unknown systems {sorted(unknown)}")
        allowed[by_name[name]] = {by_name[dependency] for dependency in row["dependencies"]}
    missing = SYSTEMS.keys() - allowed.keys()
    if missing:
        raise DiagramError(f"system-map.md has no row for {sorted(SYSTEMS[s][0] for s in missing)}")
    return allowed


def validate(allowed, feature_edges=None, forbidden=None):
    """Raise DiagramError unless every drawn edge agrees with the system map."""
    feature_edges = FEATURE_EDGES if feature_edges is None else feature_edges
    forbidden = FORBIDDEN if forbidden is None else forbidden
    for ref in {ref for edge in feature_edges for ref in edge}:
        system = system_of(ref)
        if system not in SYSTEMS or ("." in ref and ref.split(".")[1] not in dict(SYSTEMS[system][2])):
            raise DiagramError(f"unknown feature {ref!r}")
    for source, target in feature_edges:
        if "." not in source:
            raise DiagramError(f"feature edge must start at a feature, not {source!r}")
        if system_of(source) != system_of(target) and system_of(target) not in allowed[system_of(source)]:
            raise DiagramError(f"feature edge {source} -> {target} is not allowed by the system map")
    for source, target, _ in forbidden:
        if target in allowed[source]:
            raise DiagramError(f"forbidden example {source} -> {target} is allowed by the system map")
    for source, target in SYSTEM_IMPLEMENTS | {(system_of(a), system_of(b)) for a, b in FEATURE_IMPLEMENTS}:
        if target not in allowed[source]:
            raise DiagramError(f"implements edge {source} -> {target} is not allowed by the system map")


# ---------------------------------------------------------------- per-system diagrams -------
def node(node_id, label, tier, stroke, bold=False, fill=None):
    text = f"<b>{label}</b>" if bold else label
    return (f'  {node_id}["{text}"]',
            f"  style {node_id} fill:{fill or TIER_FILL[tier]},stroke:{stroke},"
            f"stroke-width:{3 if bold else 2}px,color:#f8fafc")


def link_styles(colors):
    return [f"  linkStyle {index} stroke:{color},stroke-width:2.5px"
            + (",stroke-dasharray:8 5" if color == FORBIDDEN_COLOR else "")
            for index, color in enumerate(colors)]


def system_card(system, allowed):
    name, tier, _ = SYSTEMS[system]
    prefix = system + "__"  # unique ids: every card shares one HTML page
    nodes, styles, edges, colors = [], [], [], []
    line, style = node(prefix + "src", name, tier, COLOR[system], bold=True)
    nodes.append(line); styles.append(style)
    targets = [s for s in ORDER if s in allowed[system] and s != "core"]
    for target in targets:
        line, style = node(prefix + target, SYSTEMS[target][0], TIER_OF[target], COLOR[target])
        nodes.append(line); styles.append(style)
        arrow = "-.->" if (system, target) in SYSTEM_IMPLEMENTS else "-->"
        edges.append(f"  {prefix}src {arrow} {prefix}{target}")
        colors.append(COLOR[system])
    for source, target, _ in FORBIDDEN:
        if source != system:
            continue
        line, style = node(f"{prefix}x_{target}", "✗ " + SYSTEMS[target][0], TIER_OF[target],
                           FORBIDDEN_COLOR, fill="#3f1d1d")
        nodes.append(line); styles.append(style)
        edges.append(f"  {prefix}src -.-x {prefix}x_{target}")
        colors.append(FORBIDDEN_COLOR)
    if not targets:  # a note, not a dependency box
        note = "depends on nothing" if not allowed[system] else "may include only Core"
        nodes.append(f'  {prefix}note["<i>{note}</i>"]')
        styles.append(f"  style {prefix}note fill:none,stroke:none,color:#a1a1aa")
        edges.insert(0, f"  {prefix}src ~~~ {prefix}note")
        colors.insert(0, "transparent")
    return "flowchart LR" + NEWLINE + NEWLINE.join(nodes + edges + styles + link_styles(colors)) + NEWLINE


def feature_card(system):
    name, tier, features = SYSTEMS[system]
    prefix = system + "__"
    lines = [f'  subgraph {prefix}SRC["{name}"]', "    direction TB"]
    styles = [f"  style {prefix}SRC fill:#27272a,stroke:{COLOR[system]},stroke-width:3px,color:#f8fafc"]
    for feature, label in features:
        lines.append(f'    {prefix}{feature}["{label}"]')
        styles.append(f"  style {prefix}{feature} fill:{TIER_FILL[tier]},stroke:{COLOR[system]},"
                      "stroke-width:2px,color:#f8fafc")
    lines.append("  end")
    nodes, edges, colors, seen = [], [], [], set()
    for source, target in FEATURE_EDGES:
        if system_of(source) != system:
            continue
        if system_of(target) == system:
            target_id = prefix + target.split(".")[1]
        else:
            target_id = prefix + "t_" + target.replace(".", "_")
            if target_id not in seen:
                seen.add(target_id)
                line, style = node(target_id, label_of(target), TIER_OF[system_of(target)],
                                   COLOR[system_of(target)])
                nodes.append(line); styles.append(style)
        arrow = "-.->" if (source, target) in FEATURE_IMPLEMENTS else "-->"
        edges.append(f"  {prefix}{source.split('.')[1]} {arrow} {target_id}")
        colors.append(COLOR[system])
    return "flowchart LR" + NEWLINE + NEWLINE.join(lines + nodes + edges + styles + link_styles(colors)) + NEWLINE


# ---------------------------------------------------------------- outputs -------------------
LEGEND = """- **Arrow** `A --> B`: A may include B (system view) or uses B (feature view).
- **Arrow colour** is the colour of the system the arrow starts from.
- **Box colour** is the tier of the box: Application, Services, Domain, Kernel, Foundation or
  Platform.
- **Dotted arrow**: a platform backend implements that interface.
- **Red ✗ box**: a forbidden dependency. These are examples. Apart from Core, anything not drawn
  is forbidden too.
- **Core** is open to every system except GoSurveyEditor, so the system view does not draw it.
"""


def render_markdown(allowed):
    rows = NEWLINE.join(f"| {SYSTEMS[a][0]} | {SYSTEMS[b][0]} | {why[0].upper() + why[1:]}. |"
                        for a, b, why in FORBIDDEN)
    parts = [f"""# GoSurvey system dependencies (detailed)

Two views of the **target architecture**. The [system map](system-map.md) table stays
authoritative; if a diagram here disagrees with it, fix the diagram.

Each system has its own small diagram, so every arrow starts at one named box and ends at one
named box. Nothing has to be traced across the page.

{LEGEND}
`python tools/system_dependency_diagrams.py` generates this file and a three-page US Letter print
layout in `build/artifacts/diagrams/`. Edit the generator, not this file; `--check` fails when
this file is out of date.

## 1. System dependencies

Every arrow is one entry in the "Allowed dependencies" column of the system map.
"""]
    parts += [f"### {SYSTEMS[s][0]}{NEWLINE}{NEWLINE}```mermaid{NEWLINE}{system_card(s, allowed)}```{NEWLINE}"
              for s in ORDER]
    parts.append("""## 2. Feature dependencies (proposed)

Each arrow runs from a feature to the feature it uses; "System · all" means the whole system.
This level is a **proposal** derived from the target architecture: the system map and ADRs define
only system-level edges, and some features here (such as Network's transport interface or Core's
paths interface) are proposed names. Every feature arrow lies on an allowed system edge from
section 1, and the generator checks this.
""")
    parts += [f"### {SYSTEMS[s][0]} features{NEWLINE}{NEWLINE}```mermaid{NEWLINE}{feature_card(s)}```{NEWLINE}"
              for s in ORDER]
    parts.append(f"""## Forbidden examples (red ✗)

| From | ✗ To | Why |
|---|---|---|
{rows}

## Gaps the table leaves open

These follow from the system-map table as of this diagram and may be unintended. Resolve them in
`system-map.md`, then update this list.

- **Debug has no consumers.** No system lists Debug as an allowed dependency, so nothing may call
  the profiler, tracer or invariant checks.
- **Commands may not include Geometry or Math.** Interaction, Renderer and IO may; Commands may
  not, even though command families build geometry.
- **Interaction may not include Pdf**, though it may include every other Domain system.
- **GoSurveyEditor may not include Headless, Events, UI or Core directly**; it reaches them only
  through the systems it lists.
""")
    return NEWLINE.join(parts)


def render_print_page(allowed):
    def cards(diagrams):
        return "".join(f'<div class="card"><pre class="mermaid">{NEWLINE}{d}</pre></div>' for d in diagrams)

    system_sheet = f"""<section class="sheet">
<h1>GoSurvey · system dependencies</h1>
<div class="sub">Target architecture · one diagram per system · each arrow is one allowed entry in docs/architecture/system-map.md.
Box colour = tier · arrow colour = source system · dotted = implements · <span class="red">✗</span> = forbidden example ·
Core is open to every system except GoSurveyEditor.</div>
<div class="grid">{cards(system_card(s, allowed) for s in ORDER)}</div></section>"""
    feature_sheets = "".join(f"""<section class="sheet">
<h1>GoSurvey · feature dependencies (proposed) · {title}</h1>
<div class="sub">Target architecture · each arrow runs from a feature to the feature it uses · "System · all" = the whole system ·
box colour = tier · arrow colour = source system · dotted = implements · every arrow lies on an allowed system edge from page 1.</div>
<div class="grid">{cards(feature_card(s) for s in systems)}</div></section>""" for title, systems in [
        ("Application and Services", ORDER[:FEATURE_PAGE_SPLIT]),
        ("Domain, Kernel, Foundation and Platform", ORDER[FEATURE_PAGE_SPLIT:])])
    return f"""<!doctype html><html><head><meta charset="utf-8"><title>GoSurvey dependencies</title>
<style>
@page {{ size: letter portrait; margin: 0.3in; }}
* {{ box-sizing: border-box; }}
html, body {{ margin: 0; background: #18181b; color: #e4e4e7; font-family: "Trebuchet MS", system-ui, sans-serif;
  -webkit-print-color-adjust: exact; print-color-adjust: exact; }}
.sheet {{ width: 7.9in; height: 10.35in; margin: 0 auto; padding: 0.04in 0.06in; display: flex;
  flex-direction: column; overflow: hidden; break-after: page; }}
.sheet:last-child {{ break-after: auto; }}
h1 {{ font-size: 13pt; margin: 0 0 2pt; }}
.sub {{ font-size: 7.3pt; color: #a1a1aa; margin-bottom: 5pt; line-height: 1.3; }}
.red {{ color: #f87171; font-weight: bold; }}
.grid {{ flex: 1 1 auto; min-height: 0; columns: 3; column-gap: 5pt; column-fill: balance; }}
.card {{ break-inside: avoid; margin: 0 0 5pt; border: 1px solid #3f3f46; border-radius: 4pt; padding: 3pt; }}
pre.mermaid {{ margin: 0; display: flex; justify-content: center; }}
pre.mermaid svg {{ max-width: none !important; height: auto; flex: none; }}
</style></head><body>
{system_sheet}{feature_sheets}
<script type="module">
import mermaid from "{MERMAID_URL}";
mermaid.initialize({{ startOnLoad: false, theme: "dark", flowchart: {{ nodeSpacing: 12, rankSpacing: 40, padding: 6 }} }});
// Render one at a time: batch rendering mixes diagrams up when many share a page.
const pres = [...document.querySelectorAll("pre.mermaid")];
for (let i = 0; i < pres.length; i++) {{
  const {{ svg }} = await mermaid.render("card" + i, pres[i].textContent);
  pres[i].innerHTML = svg;
}}
// Fit each sheet: one common scale for every card, 2-5 columns, keep the largest scale.
for (const grid of document.querySelectorAll(".grid")) {{
  const svgs = [...grid.querySelectorAll("svg")];
  const natural = svgs.map(s => s.viewBox.baseVal.width);
  const fits = () => grid.scrollHeight <= grid.clientHeight + 1 && grid.scrollWidth <= grid.clientWidth + 1
    && svgs.every(s => s.getBoundingClientRect().width <= s.parentElement.parentElement.clientWidth - 6);
  const apply = (cols, k) => {{ grid.style.columns = cols; svgs.forEach((s, i) => {{ s.style.width = (natural[i] * k) + "px"; }}); }};
  let best = null;
  for (const cols of [2, 3, 4, 5]) {{
    let lo = 0.1, hi = 1.6;
    for (let n = 0; n < 18; n++) {{ const mid = (lo + hi) / 2; apply(cols, mid); if (fits()) lo = mid; else hi = mid; }}
    if (!best || lo > best.k) best = {{ cols, k: lo }};
  }}
  apply(best.cols, best.k);
}}
</script></body></html>
"""


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", type=pathlib.Path, default=ROOT, help="repository root")
    parser.add_argument("--check", action="store_true", help="fail if the doc is out of date")
    args = parser.parse_args(argv)
    try:
        allowed = read_allowed(args.root)
        validate(allowed)
    except DiagramError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    markdown = render_markdown(allowed)
    doc = args.root / DOC
    if args.check:
        current = doc.read_text(encoding="utf-8").replace("\r\n", "\n") if doc.exists() else ""
        if current != markdown:
            print(f"error: {DOC} is out of date; run python tools/system_dependency_diagrams.py",
                  file=sys.stderr)
            return 1
        return 0
    doc.write_text(markdown, encoding="utf-8", newline="\n")
    page = args.root / PRINT_PAGE
    page.parent.mkdir(parents=True, exist_ok=True)
    page.write_text(render_print_page(allowed), encoding="utf-8")
    print(f"wrote {DOC} and {PRINT_PAGE}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
