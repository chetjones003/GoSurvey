"""Generate the system-level and feature-level dependency diagrams for the target architecture.

Source of truth for system edges: the "Allowed dependencies" column of
docs/architecture/system-map.md (copied into SYSTEM_DEPS below). Feature edges are a proposal;
every feature edge is checked to lie on an allowed system edge.

Rewrites docs/architecture/system-dependencies.md (one Mermaid diagram per system, for both
levels) and writes a three-page US Letter print layout to
build/artifacts/diagrams/system-dependencies.html. Print it to PDF from a browser.

Usage, from the repository root: python tools/system_dependency_diagrams.py
"""
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
MD = ROOT / "docs/architecture/system-dependencies.md"
MAP = ROOT / "docs/architecture/system-map.md"
HTML = ROOT / "build/artifacts/diagrams/system-dependencies.html"

# ---------------------------------------------------------------- systems -------------------
# id: (display name, tier, features [(feature id, label)])
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
# Allowed dependencies, verbatim from the system-map table.
SYSTEM_DEPS = {
    "core": "", "math": "core", "events": "core math", "ui": "core events", "debug": "core",
    "geometry": "math core",
    "document": "geometry math core events",
    "survey": "geometry math core document",
    "terrain": "geometry math core document survey",
    "pointcloud": "math core",
    "pdf": "geometry math core document",
    "renderer": "geometry math core document survey terrain pointcloud pdf",
    "io": "geometry math core document survey terrain pointcloud pdf",
    "network": "core events",
    "commands": "renderer io network document survey terrain pointcloud pdf ui events core",
    "interaction": "commands renderer document survey terrain pointcloud geometry math events ui core",
    "pqt": "core events ui", "pgl": "core math renderer", "pwin": "core ui network",
    "phead": "core events ui renderer",
    "editor": "commands interaction renderer io network pqt pgl pwin",
}
PLATFORM = ["pqt", "pgl", "pwin", "phead"]
IMPLEMENTS = {("pqt", "ui"), ("pqt", "events"), ("pgl", "renderer"), ("pwin", "ui"),
              ("pwin", "network"), ("pwin", "core"), ("phead", "events"), ("phead", "ui"),
              ("phead", "renderer")}
FORBIDDEN = [  # (src, dst, why); drawn on the system diagram
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
# Colour per source system: arrows leaving a system use the colour of its box border.
COLOR = {
    "editor": "#f8fafc", "interaction": "#e879f9", "commands": "#60a5fa", "renderer": "#22d3ee",
    "io": "#38bdf8", "network": "#2dd4bf", "document": "#86efac", "survey": "#facc15",
    "terrain": "#4ade80", "pointcloud": "#a3e635", "pdf": "#34d399", "geometry": "#fbbf24",
    "core": "#c4b5fd", "math": "#a78bfa", "events": "#c084fc", "ui": "#d8b4fe",
    "debug": "#ddd6fe", "pqt": "#f472b6", "pgl": "#fb923c", "pwin": "#94a3b8",
    "phead": "#d1d5db",
}
FILL = {"APP": "#1f2937", "T4": "#312e81", "T3": "#164e63", "T2": "#14532d", "T1": "#713f12",
        "T0": "#4c1d95", "PLATFORM": "#3f3f46"}

# ---------------------------------------------------------------- feature edges -------------
# (source feature, target feature or whole system). Platform -> interface edges are dotted.
F = [
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
    # Platform adapters
    ("pqt.ui", "ui"), ("pqt.input", "events.input"), ("pgl.dev", "renderer.dev"),
    ("pgl.dev", "math.mat"),
    ("pwin.paths", "core.paths"), ("pwin.http", "network.xport"), ("pwin.crash", "core.log"),
    ("pwin.dlg", "ui.dlg"),
    ("phead.dev", "renderer.dev"), ("phead.dlg", "ui.dlg"), ("phead.input", "events.input"),
]


def sys_of(ref):
    return ref.split(".")[0]


def fid(ref):
    return ref.replace(".", "_")


# The copy above must match the system-map table exactly.
_BY_NAME = {v[0]: k for k, v in SYSTEMS.items()}
_table = {}
for row in MAP.read_text(encoding="utf-8").splitlines():
    cells = [c.strip() for c in row.strip().strip("|").split("|")]
    if len(cells) == 5 and cells[0] in _BY_NAME:
        deps = [] if cells[4] == "None" else [_BY_NAME[d.strip()] for d in cells[4].split(",")]
        _table[_BY_NAME[cells[0]]] = set(deps)
assert _table.keys() == SYSTEM_DEPS.keys(), "system list differs from system-map.md"
for _s, _deps in _table.items():
    assert _deps == set(SYSTEM_DEPS[_s].split()), f"{_s}: dependencies differ from system-map.md"

# Every feature edge must follow an allowed system edge (or stay inside one system).
for a, b in F:
    sa, sb = sys_of(a), sys_of(b)
    if sa != sb:
        assert sb in SYSTEM_DEPS[sa].split(), f"feature edge {a} -> {b} breaks the system map"
    for ref in (a, b):
        if "." in ref:
            s, f = ref.split(".")
            assert f in [x for x, _ in SYSTEMS[s][2]], f"unknown feature {ref}"
for s, d, _ in FORBIDDEN:
    assert d not in SYSTEM_DEPS[s].split(), f"forbidden edge {s} -> {d} is actually allowed"


def is_dotted(sa, sb):
    return sa in PLATFORM and (sa, sb) in IMPLEMENTS


# ---------------------------------------------------------------- per-system cards ----------
# One small diagram per system: every arrow starts at one named box and ends at one named box.
ORDER = ["editor", "interaction", "commands", "renderer", "io", "network", "document", "survey",
         "terrain", "pointcloud", "pdf", "geometry", "math", "events", "ui", "debug", "core",
         "pqt", "pgl", "pwin", "phead"]
RED = "#ef4444"
NL = "\n"


def box(nid, label, tier, stroke, bold=False, fill=None):
    text = f"<b>{label}</b>" if bold else label
    return (f'  {nid}["{text}"]',
            f"  style {nid} fill:{fill or FILL[tier]},stroke:{stroke},"
            f"stroke-width:{3 if bold else 2}px,color:#f8fafc")


def link_styles(colors):
    return [f"  linkStyle {i} stroke:{c},stroke-width:2.5px" + (",stroke-dasharray:8 5" if c == RED else "")
            for i, c in enumerate(colors)]


def system_card(s):
    name, tier, _ = SYSTEMS[s]
    p = s + "__"  # unique node ids: many diagrams share one HTML page
    nodes, styles, edges, colors = [], [], [], []
    n, st = box(p + "src", name, tier, COLOR[s], bold=True)
    nodes.append(n); styles.append(st)
    deps = [d for d in SYSTEM_DEPS[s].split() if d != "core"]
    for d in deps:
        n, st = box(p + d, SYSTEMS[d][0], TIER_OF[d], COLOR[d])
        nodes.append(n); styles.append(st)
        edges.append(f"  {p}src {'-.->' if is_dotted(s, d) else '-->'} {p}{d}")
        colors.append(COLOR[s])
    for a, d, _ in FORBIDDEN:
        if a != s:
            continue
        n, st = box(f"{p}x_{d}", "✗ " + SYSTEMS[d][0], TIER_OF[d], RED, fill="#3f1d1d")
        nodes.append(n); styles.append(st)
        edges.append(f"  {p}src -.-x {p}x_{d}")
        colors.append(RED)
    if not edges:
        n, st = box(p + "none", "nothing" if s == "core" else "Core only", "T0", COLOR["core"])
        nodes.append(n); styles.append(st)
        edges.append(f"  {p}src --> {p}none")
        colors.append(COLOR[s])
    return "flowchart LR" + NL + NL.join(nodes + edges + styles + link_styles(colors)) + NL


def feature_card(s):
    name, tier, feats = SYSTEMS[s]
    p = s + "__"
    lines = [f'  subgraph {p}SRC["{name}"]', "    direction TB"]
    styles = [f"  style {p}SRC fill:#27272a,stroke:{COLOR[s]},stroke-width:3px,color:#f8fafc"]
    for f, label in feats:
        lines.append(f'    {p}{f}["{label}"]')
        styles.append(f"  style {p}{f} fill:{FILL[tier]},stroke:{COLOR[s]},stroke-width:2px,color:#f8fafc")
    lines.append("  end")
    nodes, edges, colors, seen = [], [], [], set()
    for a, b in F:
        if sys_of(a) != s:
            continue
        sb = sys_of(b)
        if sb == s:
            tid = p + b.split(".")[1]
        else:
            tid = p + "t_" + fid(b)
            if tid not in seen:
                seen.add(tid)
                feat = dict(SYSTEMS[sb][2])[b.split(".")[1]] if "." in b else "all"
                n, st = box(tid, f"{SYSTEMS[sb][0]} · {feat}", TIER_OF[sb], COLOR[sb])
                nodes.append(n); styles.append(st)
        edges.append(f"  {p}{a.split('.')[1]} {'-.->' if is_dotted(s, sb) else '-->'} {tid}")
        colors.append(COLOR[s])
    return "flowchart LR" + NL + NL.join(lines + nodes + edges + styles + link_styles(colors)) + NL


TIER_OF = {s: v[1] for s, v in SYSTEMS.items()}
SYS_CARDS = {s: system_card(s) for s in ORDER}
FEAT_CARDS = {s: feature_card(s) for s in ORDER}

# ---------------------------------------------------------------- markdown ------------------
INTRO = """# GoSurvey system dependencies (detailed)

Two views of the **target architecture**. The [system map](system-map.md) table stays
authoritative; if a diagram here disagrees with it, fix the diagram.

Each system has its own small diagram, so every arrow starts at one named box and ends at one
named box. Nothing has to be traced across the page.

- **Arrow** `A --> B`: A may include B (system view) or uses B (feature view).
- **Arrow colour** is the colour of the system the arrow starts from.
- **Box colour** is the tier of the box: Application, Services, Domain, Kernel, Foundation or
  Platform.
- **Dotted arrow**: a platform backend implements that interface.
- **Red ✗ box**: a forbidden dependency. These are examples; anything not drawn is forbidden too.
- **Core** is open to every system except GoSurveyEditor, so it is not drawn on each system.

`python tools/system_dependency_diagrams.py` generates this file and a three-page US Letter print
layout in `build/artifacts/diagrams/`. Edit the generator, not this file.

## 1. System dependencies

Every arrow is one entry in the "Allowed dependencies" column of the system map.
"""
FEAT_INTRO = """## 2. Feature dependencies (proposed)

Each arrow runs from a feature to the feature it uses; "System · all" means the whole system.
This level is a **proposal** derived from the target architecture: the system map and ADRs define
only system-level edges. Every feature arrow lies on an allowed system edge from section 1, and
the generator checks this.
"""
OUTRO = """## Forbidden examples (red ✗)

| From | ✗ To | Why |
|---|---|---|
{rows}

## Gaps the table leaves open

These follow from the current table and may be unintended. Resolve them in `system-map.md`, not here.

- **Debug has no consumers.** No system lists Debug as an allowed dependency, so nothing may call
  the profiler, tracer or invariant checks.
- **Commands may not include Geometry or Math.** Interaction, Renderer and IO may; Commands may
  not, even though command families build geometry.
- **Interaction may not include Pdf**, though it may include every other Domain system.
- **GoSurveyEditor may not include Headless, Events, UI or Core directly**; it reaches them only
  through the systems it lists.
"""
md = [INTRO]
md += [f"### {SYSTEMS[s][0]}{NL}{NL}```mermaid{NL}{SYS_CARDS[s]}```{NL}" for s in ORDER]
md.append(FEAT_INTRO)
md += [f"### {SYSTEMS[s][0]} features{NL}{NL}```mermaid{NL}{FEAT_CARDS[s]}```{NL}" for s in ORDER]
md.append(OUTRO.format(rows=NL.join(
    f"| {SYSTEMS[a][0]} | {SYSTEMS[b][0]} | {why[0].upper() + why[1:]}. |" for a, b, why in FORBIDDEN)))
MD.write_text(NL.join(md), encoding="utf-8")

# ---------------------------------------------------------------- print HTML ----------------


def cards_html(cards):
    return "".join(f'<div class="card"><pre class="mermaid">{NL}{c}</pre></div>' for c in cards.values())


page_sys = f"""<section class="sheet">
<h1>GoSurvey · system dependencies</h1>
<div class="sub">Target architecture · one diagram per system · each arrow is one allowed entry in docs/architecture/system-map.md.
Box colour = tier · arrow colour = source system · dotted = implements · <span class="red">✗</span> = forbidden example ·
Core is open to every system except GoSurveyEditor.</div>
<div class="grid sys">{cards_html(SYS_CARDS)}</div></section>"""
FEAT_SPLIT = ORDER.index("document")
page_feat = "".join(f"""<section class="sheet">
<h1>GoSurvey · feature dependencies (proposed) · {part}</h1>
<div class="sub">Target architecture · each arrow runs from a feature to the feature it uses · "System · all" = the whole system ·
box colour = tier · arrow colour = source system · dotted = implements · every arrow lies on an allowed system edge from page 1.</div>
<div class="grid feat">{cards_html({k: FEAT_CARDS[k] for k in keys})}</div></section>""" for part, keys in [
    ("Application and Services", ORDER[:FEAT_SPLIT]), ("Domain, Kernel, Foundation and Platform", ORDER[FEAT_SPLIT:])])
pages = [page_sys, page_feat]

HTML.parent.mkdir(parents=True, exist_ok=True)
HTML.write_text(f"""<!doctype html><html><head><meta charset="utf-8"><title>GoSurvey dependencies</title>
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
.grid {{ flex: 1 1 auto; min-height: 0; column-gap: 5pt; column-fill: balance; }}
.grid {{ columns: 3; }}
.card {{ break-inside: avoid; margin: 0 0 5pt; border: 1px solid #3f3f46; border-radius: 4pt; padding: 3pt; }}
pre.mermaid {{ margin: 0; display: flex; justify-content: center; }}
pre.mermaid svg {{ max-width: none !important; height: auto; flex: none; }}
</style></head><body>
{''.join(pages)}
<script type="module">
import mermaid from "https://cdn.jsdelivr.net/npm/mermaid@11/dist/mermaid.esm.min.mjs";
mermaid.initialize({{ startOnLoad: false, theme: "dark", flowchart: {{ nodeSpacing: 12, rankSpacing: 40, padding: 6 }} }});
const pres = [...document.querySelectorAll("pre.mermaid")];
for (let i = 0; i < pres.length; i++) {{
  const {{ svg }} = await mermaid.render("card" + i, pres[i].textContent);
  pres[i].innerHTML = svg;
}}
// Fit each sheet: one common scale for every card, trying 2-5 columns, keep the largest scale.
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
  grid.dataset.fit = best.cols + " cols @ " + best.k.toFixed(2);
}}
document.title = [...document.querySelectorAll(".grid")].map(g => g.dataset.fit + " " + g.scrollHeight + "/" + g.clientHeight + "/" + g.scrollWidth + "/" + g.clientWidth).join(" | ");
</script></body></html>
""", encoding="utf-8")
print("feature edges", len(F))
