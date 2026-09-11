"""Render AutoCAD-style command help cards (grey panel, red geometry, green pick points)."""

from __future__ import annotations

import math
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable

from PIL import Image, ImageDraw, ImageFont

W = 580
H = 400
PAD = 18
DIAG_TOP = 118

BG = (235, 235, 235)
TITLE = (40, 40, 40)
BODY = (90, 90, 90)
RED = (220, 40, 40)
GREEN = (40, 180, 60)
GREY_SHAPE = (150, 150, 150)
PINK = (245, 205, 205)
PINK_DARK = (210, 165, 165)
BLACK = (30, 30, 30)
WHITE = (255, 255, 255)
DIAG_BG = (228, 228, 228)

COS30 = math.cos(math.radians(30))
SIN30 = math.sin(math.radians(30))


@dataclass
class CardSpec:
    primary: str
    title: str
    tagline: str
    kind: str = "generic"
    params: dict = field(default_factory=dict)


def _font(size: int, bold: bool = False) -> ImageFont.FreeTypeFont | ImageFont.ImageFont:
    bundled = Path(__file__).resolve().parents[1] / "resources" / "fonts"
    if bold:
        candidates = [
            bundled / "IBMPlexSans-Bold.ttf",
            bundled / "IBMPlexSans-SemiBold.ttf",
            bundled / "IBMPlexSansCondensed-SemiBold.ttf",
        ]
    else:
        candidates = [
            bundled / "IBMPlexSans-Regular.ttf",
        ]
    for p in candidates:
        if p.exists():
            return ImageFont.truetype(str(p), size)
    windir = Path("C:/Windows/Fonts")
    for name in ("segoeui.ttf", "arial.ttf"):
        p = windir / name
        if p.exists():
            return ImageFont.truetype(str(p), size)
    return ImageFont.load_default()


def _wrap(text: str, draw: ImageDraw.ImageDraw, font: ImageFont.ImageFont, max_w: int) -> list[str]:
    words = text.split()
    if not words:
        return []
    lines: list[str] = []
    cur = words[0]
    for w in words[1:]:
        trial = cur + " " + w
        if draw.textlength(trial, font=font) <= max_w:
            cur = trial
        else:
            lines.append(cur)
            cur = w
    lines.append(cur)
    return lines[:3]


def _pick(draw: ImageDraw.ImageDraw, x: float, y: float, n: int, f: ImageFont.ImageFont) -> None:
    s = 8
    draw.line([(x - s, y - s), (x + s, y + s)], fill=GREEN, width=2)
    draw.line([(x - s, y + s), (x + s, y - s)], fill=GREEN, width=2)
    draw.text((x + 11, y - 17), str(n), fill=GREEN, font=f)


def _diagram_bg(draw: ImageDraw.ImageDraw) -> None:
    draw.rectangle([PAD, DIAG_TOP, W - PAD, H - PAD], fill=DIAG_BG, outline=(210, 210, 210), width=1)


def _caption(draw: ImageDraw.ImageDraw, x: float, y: float, text: str) -> None:
    draw.text((x, y), text, fill=BODY, font=_font(11))


def _dim_vertical(
    draw: ImageDraw.ImageDraw, x: float, y0: float, y1: float, pick_n: int, pf: ImageFont.ImageFont
) -> None:
    ya, yb = (y0, y1) if y0 < y1 else (y1, y0)
    draw.line([(x, ya), (x, yb)], fill=BLACK, width=1)
    draw.line([(x - 6, ya), (x + 6, ya)], fill=BLACK, width=1)
    draw.line([(x - 6, yb), (x + 6, yb)], fill=BLACK, width=1)
    _arrow(draw, x, yb - 2, x, ya + 2, color=BLACK, width=1)
    if pick_n:
        _pick(draw, x + 18, ya + 8, pick_n, pf)


def _line(draw: ImageDraw.ImageDraw, pts: list[tuple[float, float]], color=RED, width=2) -> None:
    if len(pts) >= 2:
        draw.line(pts, fill=color, width=width)


def _poly(draw: ImageDraw.ImageDraw, pts: list[tuple[float, float]], color=RED, width=2) -> None:
    _line(draw, pts + [pts[0]] if len(pts) >= 3 else pts, color, width)


def _circle_outline(
    draw: ImageDraw.ImageDraw, cx: float, cy: float, r: float, color=RED, width=2
) -> None:
    draw.ellipse([cx - r, cy - r, cx + r, cy + r], outline=color, width=width)


def _arrow(
    draw: ImageDraw.ImageDraw,
    x0: float,
    y0: float,
    x1: float,
    y1: float,
    color=BLACK,
    width=1,
) -> None:
    draw.line([(x0, y0), (x1, y1)], fill=color, width=width)
    ang = math.atan2(y1 - y0, x1 - x0)
    ah = 8
    a1 = ang + math.radians(150)
    a2 = ang - math.radians(150)
    draw.line([(x1, y1), (x1 + ah * math.cos(a1), y1 + ah * math.sin(a1))], fill=color, width=width)
    draw.line([(x1, y1), (x1 + ah * math.cos(a2), y1 + ah * math.sin(a2))], fill=color, width=width)


def _iso(x: float, y: float, z: float, ox: float, oy: float, sc: float = 22.0) -> tuple[float, float]:
    sx = ox + (x - y) * COS30 * sc
    sy = oy + (x + y) * SIN30 * sc - z * sc
    return sx, sy


def _iso_box(
    draw: ImageDraw.ImageDraw,
    ox: float,
    oy: float,
    lx: float,
    ly: float,
    lz: float,
    sc: float,
    fill_top=PINK,
    fill_side=PINK_DARK,
    outline=RED,
) -> None:
    p = [
        _iso(0, 0, 0, ox, oy, sc),
        _iso(lx, 0, 0, ox, oy, sc),
        _iso(lx, ly, 0, ox, oy, sc),
        _iso(0, ly, 0, ox, oy, sc),
        _iso(0, 0, lz, ox, oy, sc),
        _iso(lx, 0, lz, ox, oy, sc),
        _iso(lx, ly, lz, ox, oy, sc),
        _iso(0, ly, lz, ox, oy, sc),
    ]
    top = [p[4], p[5], p[6], p[7]]
    front = [p[0], p[1], p[5], p[4]]
    right = [p[1], p[2], p[6], p[5]]
    draw.polygon(top, fill=fill_top, outline=outline)
    draw.polygon(front, fill=fill_side, outline=outline)
    draw.polygon(right, fill=fill_side, outline=outline)


def _header(draw: ImageDraw.ImageDraw, spec: CardSpec) -> None:
    tf = _font(22, bold=True)
    bf = _font(13)
    draw.text((PAD, 14), spec.title, fill=TITLE, font=tf)
    for i, ln in enumerate(_wrap(spec.tagline, draw, bf, W - 2 * PAD)):
        draw.text((PAD, 44 + i * 16), ln, fill=BODY, font=bf)


def _diag_line(draw: ImageDraw.ImageDraw, pf: ImageFont.ImageFont) -> None:
    _diagram_bg(draw)
    pts = [(120, 260), (460, 170)]
    _line(draw, pts)
    for i, (x, y) in enumerate(pts, 1):
        _pick(draw, x, y, i, pf)


def _diag_polyline(draw: ImageDraw.ImageDraw, pf: ImageFont.ImageFont) -> None:
    pts = [(70, 290), (170, 170), (300, 230), (420, 150), (500, 260), (360, 310)]
    _poly(draw, pts)
    for i, (x, y) in enumerate(pts, 1):
        _pick(draw, x, y, i, pf)


def _diag_circle(draw: ImageDraw.ImageDraw, pf: ImageFont.ImageFont) -> None:
    _diagram_bg(draw)
    cx, cy, r = 300, 230, 95
    _circle_outline(draw, cx, cy, r)
    _pick(draw, cx, cy, 1, pf)
    _pick(draw, cx + r, cy, 2, pf)
    _caption(draw, 220, 305, "center, then radius")


def _diag_arc(draw: ImageDraw.ImageDraw, pf: ImageFont.ImageFont) -> None:
    pts = [(120, 280), (300, 120), (480, 260)]
    _line(draw, pts[:2])
    _line(draw, pts[1:])
    draw.arc([140, 140, 460, 320], start=200, end=340, fill=RED, width=2)
    for i, (x, y) in enumerate(pts, 1):
        _pick(draw, x, y, i, pf)


def _diag_ellipse(draw: ImageDraw.ImageDraw, pf: ImageFont.ImageFont) -> None:
    draw.ellipse([170, 160, 430, 300], outline=RED, width=2)
    cx, cy = 300, 230
    _pick(draw, cx, cy, 1, pf)
    _pick(draw, 430, 230, 2, pf)
    _pick(draw, 300, 160, 3, pf)


def _diag_rect(draw: ImageDraw.ImageDraw, pf: ImageFont.ImageFont) -> None:
    pts = [(160, 280), (420, 140)]
    _poly(draw, [(160, 280), (420, 280), (420, 140), (160, 140)])
    _pick(draw, pts[0][0], pts[0][1], 1, pf)
    _pick(draw, pts[1][0], pts[1][1], 2, pf)


def _diag_hatch(draw: ImageDraw.ImageDraw, pf: ImageFont.ImageFont) -> None:
    _poly(draw, [(140, 270), (440, 270), (440, 150), (140, 150)])
    for x in range(150, 430, 14):
        draw.line([(x, 160), (x + 80, 260)], fill=RED, width=1)
    _pick(draw, 290, 210, 1, pf)


def _diag_offset(draw: ImageDraw.ImageDraw, pf: ImageFont.ImageFont) -> None:
    outer = [(120, 260), (460, 260), (460, 150), (120, 150)]
    inner = [(150, 235), (430, 235), (430, 175), (150, 175)]
    _poly(draw, outer, GREY_SHAPE)
    _poly(draw, inner)
    _pick(draw, 290, 205, 1, pf)
    _pick(draw, 340, 120, 2, pf)


def _diag_move_copy(draw: ImageDraw.ImageDraw, pf: ImageFont.ImageFont, copy: bool = False) -> None:
    _diagram_bg(draw)
    src = [(120, 250), (220, 250), (220, 170), (120, 170)]
    dst = [(x + 180, y - 40) for x, y in src]
    _poly(draw, src, GREY_SHAPE)
    _poly(draw, dst)
    _arrow(draw, 230, 210, 300, 175)
    _pick(draw, 170, 210, 1, pf)
    _pick(draw, 350, 170, 2, pf)
    if copy:
        _poly(draw, src, RED, 1)
    _caption(draw, 150, 305, "base point" if not copy else "base point — original kept")


def _diag_rotate(draw: ImageDraw.ImageDraw, pf: ImageFont.ImageFont) -> None:
    _poly(draw, [(380, 250), (480, 250), (480, 170), (380, 170)], GREY_SHAPE)
    _poly(draw, [(160, 220), (250, 180), (230, 120), (140, 160)])
    _pick(draw, 200, 175, 1, pf)
    _pick(draw, 200, 260, 2, pf)
    draw.arc([150, 120, 260, 280], start=300, end=20, fill=BLACK, width=1)


def _diag_scale(draw: ImageDraw.ImageDraw, pf: ImageFont.ImageFont) -> None:
    _poly(draw, [(140, 250), (220, 250), (220, 190), (140, 190)], GREY_SHAPE)
    _poly(draw, [(300, 270), (460, 270), (460, 150), (300, 150)])
    _pick(draw, 180, 220, 1, pf)
    _pick(draw, 380, 210, 2, pf)


def _diag_mirror(draw: ImageDraw.ImageDraw, pf: ImageFont.ImageFont) -> None:
    _line(draw, [(290, 120), (290, 300)], BLACK, 1)
    _poly(draw, [(140, 220), (240, 220), (240, 160), (140, 160)])
    _poly(draw, [(340, 220), (440, 220), (440, 160), (340, 160)])
    _pick(draw, 190, 190, 1, pf)
    _pick(draw, 290, 260, 2, pf)


def _diag_trim(draw: ImageDraw.ImageDraw, pf: ImageFont.ImageFont) -> None:
    _line(draw, [(80, 200), (500, 200)], GREY_SHAPE, 2)
    _line(draw, [(300, 120), (300, 300)], GREY_SHAPE, 2)
    _line(draw, [(120, 260), (480, 140)], RED, 2)
    _pick(draw, 300, 200, 1, pf)
    _pick(draw, 400, 175, 2, pf)


def _diag_extend(draw: ImageDraw.ImageDraw, pf: ImageFont.ImageFont) -> None:
    _line(draw, [(100, 240), (500, 240)], GREY_SHAPE, 2)
    _line(draw, [(280, 260), (360, 180)], RED, 2)
    _pick(draw, 360, 180, 1, pf)


def _corner_fillet(
    draw: ImageDraw.ImageDraw, cx: float, cy: float, r: float, pf: ImageFont.ImageFont
) -> None:
    """Inside corner at (cx,cy): horizontal from left, vertical up, tangent arc."""
    _line(draw, [(90, cy), (cx - r, cy)], RED, 3)
    _line(draw, [(cx, cy - r), (cx, 95)], RED, 3)
    draw.arc([cx - 2 * r, cy - 2 * r, cx, cy], start=0, end=90, fill=RED, width=3)
    _pick(draw, 170, cy, 1, pf)
    _pick(draw, cx, 150, 2, pf)


def _diag_fillet(draw: ImageDraw.ImageDraw, pf: ImageFont.ImageFont) -> None:
    _diagram_bg(draw)
    _corner_fillet(draw, 340, 265, 55, pf)
    _caption(draw, 110, 310, "Pick two lines — fillet arc replaces the sharp corner")


def _diag_chamfer(draw: ImageDraw.ImageDraw, pf: ImageFont.ImageFont) -> None:
    _diagram_bg(draw)
    cx, cy, r = 340, 265, 55
    _line(draw, [(90, cy), (cx - r, cy)], RED, 3)
    _line(draw, [(cx, cy - r), (cx, 95)], RED, 3)
    _line(draw, [(cx - r, cy), (cx, cy - r)], RED, 3)
    _pick(draw, 170, cy, 1, pf)
    _pick(draw, cx, 150, 2, pf)
    _caption(draw, 110, 310, "Pick two lines — straight bevel connects them")


def _diag_delete(draw: ImageDraw.ImageDraw, pf: ImageFont.ImageFont) -> None:
    _poly(draw, [(180, 250), (280, 250), (280, 170), (180, 170)])
    _poly(draw, [(320, 230), (420, 230), (420, 150), (320, 150)])
    _pick(draw, 230, 210, 1, pf)


def _diag_join(draw: ImageDraw.ImageDraw, pf: ImageFont.ImageFont) -> None:
    _line(draw, [(120, 220), (260, 220)], RED, 2)
    _line(draw, [(260, 220), (440, 160)], RED, 2)
    _pick(draw, 200, 220, 1, pf)
    _pick(draw, 380, 175, 2, pf)


def _diag_break(draw: ImageDraw.ImageDraw, pf: ImageFont.ImageFont) -> None:
    _line(draw, [(100, 220), (230, 220)], RED, 2)
    _line(draw, [(270, 220), (480, 220)], RED, 2)
    _pick(draw, 250, 220, 1, pf)
    _pick(draw, 290, 220, 2, pf)


def _diag_stretch(draw: ImageDraw.ImageDraw, pf: ImageFont.ImageFont) -> None:
    _poly(draw, [(140, 260), (260, 260), (260, 160), (140, 160)], GREY_SHAPE)
    _poly(draw, [(300, 260), (420, 260), (420, 160), (300, 160)])
    draw.rectangle([130, 150, 270, 270], outline=BLACK, width=1)
    _arrow(draw, 270, 210, 300, 210)
    _pick(draw, 200, 210, 1, pf)


def _diag_array_rect(draw: ImageDraw.ImageDraw, pf: ImageFont.ImageFont) -> None:
    for dx in (0, 70, 140):
        for dy in (0, 55):
            _poly(draw, [(160 + dx, 180 + dy), (210 + dx, 180 + dy), (210 + dx, 140 + dy), (160 + dx, 140 + dy)], RED, 1)
    _pick(draw, 185, 160, 1, pf)


def _diag_extrude(draw: ImageDraw.ImageDraw, pf: ImageFont.ImageFont) -> None:
    _diagram_bg(draw)
    # 1 — closed 2D profile (plan)
    draw.rectangle([88, 262, 228, 292], outline=GREY_SHAPE, width=2)
    _pick(draw, 158, 277, 1, pf)
    _caption(draw, 108, 300, "closed shape")
    # 2 — extrusion height
    _dim_vertical(draw, 248, 292, 148, 2, pf)
    _caption(draw, 258, 300, "height")
    # Result — same footprint extruded upward (isometric prism)
    _iso_box(draw, 318, 292, 2.1, 0.55, 1.05, 34)
    _caption(draw, 350, 300, "solid")


def _diag_revolve(draw: ImageDraw.ImageDraw, pf: ImageFont.ImageFont) -> None:
    _diagram_bg(draw)
    ax = 300
    # Profile to revolve (grey)
    _poly(draw, [(130, 285), (175, 285), (175, 175), (130, 175)], GREY_SHAPE, 2)
    _pick(draw, 152, 230, 1, pf)
    _caption(draw, 118, 300, "profile")
    # Axis of revolution
    _line(draw, [(ax, 130), (ax, 300)], BLACK, 2)
    _caption(draw, ax + 8, 305, "axis")
    _pick(draw, ax, 285, 2, pf)
    # Swept solid (revolved bowl shape)
    draw.arc([ax - 95, 165, ax + 95, 290], start=270, end=90, fill=RED, width=3)
    _line(draw, [(ax, 165), (ax, 290)], RED, 2)
    _caption(draw, 360, 300, "solid")


def _diag_sweep(draw: ImageDraw.ImageDraw, pf: ImageFont.ImageFont) -> None:
    _diagram_bg(draw)
    path = [(110, 290), (210, 210), (360, 235), (470, 175)]
    # Path (dashed grey)
    for i in range(len(path) - 1):
        x0, y0 = path[i]
        x1, y1 = path[i + 1]
        steps = 8
        for s in range(steps):
            if s % 2 == 0:
                t0, t1 = s / steps, (s + 1) / steps
                draw.line(
                    [(x0 + (x1 - x0) * t0, y0 + (y1 - y0) * t0), (x0 + (x1 - x0) * t1, y0 + (y1 - y0) * t1)],
                    fill=GREY_SHAPE,
                    width=2,
                )
    _caption(draw, 250, 305, "path")
    # Closed profile at start
    _circle_outline(draw, path[0][0], path[0][1], 22, RED, 2)
    _pick(draw, path[0][0], path[0][1], 1, pf)
    _caption(draw, 70, 305, "profile")
    # Swept solid along path (red tube)
    for i in range(len(path) - 1):
        draw.line([path[i], path[i + 1]], fill=RED, width=18)
    _pick(draw, path[2][0], path[2][1], 2, pf)
    _caption(draw, 390, 305, "solid")


def _diag_loft(draw: ImageDraw.ImageDraw, pf: ImageFont.ImageFont) -> None:
    _diagram_bg(draw)
    profiles = [
        [(120, 280), (170, 280), (170, 230), (120, 230)],
        [(250, 265), (320, 265), (320, 205), (250, 205)],
        [(390, 250), (470, 250), (470, 180), (390, 180)],
    ]
    for idx, pts in enumerate(profiles):
        _poly(draw, pts, GREY_SHAPE if idx == 0 else RED, 2)
        cx = sum(p[0] for p in pts) / 4
        cy = sum(p[1] for p in pts) / 4
        _pick(draw, cx, cy, idx + 1, pf)
    # Loft rails connecting corresponding corners
    for corner in range(4):
        rail = [profiles[i][corner] for i in range(3)]
        _line(draw, rail, RED, 1)
    _caption(draw, 200, 305, "pick cross-sections in order — surface follows between them")


def _diag_presspull(draw: ImageDraw.ImageDraw, pf: ImageFont.ImageFont) -> None:
    _diagram_bg(draw)
    _iso_box(draw, 160, 285, 1.5, 1.0, 0.55, 26)
    # Highlighted top face
    p = [_iso(0, 0, 0.55, 160, 285, 26), _iso(1.5, 0, 0.55, 160, 285, 26),
         _iso(1.5, 1.0, 0.55, 160, 285, 26), _iso(0, 1.0, 0.55, 160, 285, 26)]
    draw.polygon(p, outline=RED, fill=PINK)
    _pick(draw, 230, 210, 1, pf)
    _caption(draw, 120, 305, "face")
    _dim_vertical(draw, 310, 240, 130, 2, pf)
    _iso_box(draw, 160, 285, 1.5, 1.0, 1.05, 26)
    _caption(draw, 350, 305, "pulled solid")


def _diag_slice(draw: ImageDraw.ImageDraw, pf: ImageFont.ImageFont) -> None:
    _diagram_bg(draw)
    _iso_box(draw, 250, 275, 1.4, 1.0, 0.75, 24)
    # Cutting plane
    p0 = _iso(-0.3, -0.3, 0.0, 250, 275, 24)
    p1 = _iso(1.7, -0.3, 0.0, 250, 275, 24)
    p2 = _iso(1.7, 1.3, 0.95, 250, 275, 24)
    p3 = _iso(-0.3, 1.3, 0.95, 250, 275, 24)
    draw.polygon([p0, p1, p2, p3], outline=BLACK, fill=(200, 220, 240))
    _pick(draw, 180, 230, 1, pf)
    _pick(draw, 400, 200, 2, pf)
    _pick(draw, 320, 260, 3, pf)
    _caption(draw, 130, 305, "solid")
    _caption(draw, 380, 305, "3 points define cut plane")


def _diag_section(draw: ImageDraw.ImageDraw, pf: ImageFont.ImageFont) -> None:
    _diagram_bg(draw)
    _iso_box(draw, 220, 280, 1.3, 1.0, 0.7, 22)
    # Section profile (bold red closed polyline on cut)
    sec = [_iso(0.2, 0.2, 0.35, 220, 280, 22), _iso(1.1, 0.2, 0.35, 220, 280, 22),
           _iso(1.1, 0.8, 0.35, 220, 280, 22), _iso(0.2, 0.8, 0.35, 220, 280, 22)]
    _poly(draw, sec, RED, 3)
    _pick(draw, 160, 240, 1, pf)
    _caption(draw, 150, 305, "select solids — cross-section polyline is created")


def _diag_polysolid(draw: ImageDraw.ImageDraw, pf: ImageFont.ImageFont) -> None:
    _diagram_bg(draw)
    path = [(120, 290), (220, 230), (340, 260), (440, 200)]
    for i in range(len(path) - 1):
        draw.line([path[i], path[i + 1]], fill=RED, width=22)
    for i, (x, y) in enumerate(path[:3], 1):
        _pick(draw, x, y, i, pf)
    _caption(draw, 170, 305, "click path points — wall extrudes along the path")


def _diag_box_solid(draw: ImageDraw.ImageDraw, pf: ImageFont.ImageFont, kind: str) -> None:
    _diagram_bg(draw)
    _iso_box(draw, 260, 265, 1.6, 1.0, 0.7, 22)
    _pick(draw, 200, 285, 1, pf)
    if kind == "cylinder":
        draw.ellipse([240, 250, 360, 290], outline=RED, width=2)
        _line(draw, [(240, 270), (240, 190)], RED, 2)
        _line(draw, [(360, 270), (360, 190)], RED, 2)
        draw.arc([240, 170, 360, 210], start=0, end=180, fill=RED, width=2)
    elif kind == "sphere":
        _circle_outline(draw, 300, 220, 70)
    elif kind == "cone":
        draw.polygon([(300, 150), (230, 280), (370, 280)], outline=RED, fill=PINK)
    _pick(draw, 300, 280, 2, pf)


def _diag_boolean(draw: ImageDraw.ImageDraw, pf: ImageFont.ImageFont, mode: str) -> None:
    _diagram_bg(draw)
    _iso_box(draw, 175, 265, 1.1, 1.0, 0.85, 28)
    _iso_box(draw, 285, 245, 1.1, 1.0, 0.85, 28)
    _pick(draw, 230, 280, 1, pf)
    _pick(draw, 360, 260, 2, pf)
    if mode == "union":
        _iso_box(draw, 400, 265, 1.35, 1.05, 0.85, 28)
        _caption(draw, 405, 305, "combined")
    elif mode == "subtract":
        _caption(draw, 400, 255, "first minus second")
        draw.line([(400, 220), (480, 300)], fill=RED, width=3)
    else:
        _caption(draw, 400, 255, "shared volume kept")


def _diag_text(draw: ImageDraw.ImageDraw, pf: ImageFont.ImageFont, multi: bool = False) -> None:
    _pick(draw, 160, 230, 1, pf)
    if multi:
        draw.rectangle([200, 160, 440, 260], outline=RED, width=2)
        draw.text((210, 175), "Multiline", fill=BODY, font=pf)
        draw.text((210, 195), "text…", fill=BODY, font=pf)
    else:
        draw.text((200, 220), "Sample text", fill=RED, font=_font(16))


def _diag_dimension(draw: ImageDraw.ImageDraw, pf: ImageFont.ImageFont, kind: str) -> None:
    _line(draw, [(120, 260), (460, 260)], RED, 2)
    _line(draw, [(120, 250), (120, 270)], RED, 1)
    _line(draw, [(460, 250), (460, 270)], RED, 1)
    if kind == "aligned":
        _line(draw, [(140, 240), (440, 180)], RED, 2)
    elif kind == "angular":
        _line(draw, [(300, 260), (420, 180)], RED, 2)
        draw.arc([260, 180, 380, 260], start=0, end=45, fill=RED, width=1)
    _pick(draw, 120, 260, 1, pf)
    _pick(draw, 460, 260, 2, pf)


def _diag_block(draw: ImageDraw.ImageDraw, pf: ImageFont.ImageFont, mode: str) -> None:
    _poly(draw, [(240, 250), (360, 250), (360, 160), (240, 160)])
    draw.line([(240, 160), (360, 250)], fill=RED, width=1)
    if mode == "insert":
        _pick(draw, 300, 205, 1, pf)
        _pick(draw, 400, 240, 2, pf)
    else:
        _pick(draw, 240, 250, 1, pf)


def _diag_surface(draw: ImageDraw.ImageDraw, pf: ImageFont.ImageFont) -> None:
    _diagram_bg(draw)
    # Contour lines (grey) suggesting terrain
    for y in range(175, 285, 22):
        draw.arc([100, y - 40, 480, y + 80], start=200, end=340, fill=GREY_SHAPE, width=1)
    # TIN triangles
    pts = [(130, 275), (270, 175), (410, 230), (500, 195)]
    _poly(draw, pts, RED, 1)
    _line(draw, [pts[0], pts[2]], RED, 1)
    _line(draw, [pts[1], pts[3]], RED, 1)
    _pick(draw, 320, 220, 1, pf)
    _caption(draw, 200, 305, "TIN surface — pick a point to query elevation")


def _diag_points(draw: ImageDraw.ImageDraw, pf: ImageFont.ImageFont) -> None:
    coords = [(160, 240), (240, 180), (320, 260), (400, 200), (460, 250)]
    for x, y in coords:
        _pick(draw, x, y, 0, pf)
    _pick(draw, 160, 240, 1, pf)


def _diag_measure(draw: ImageDraw.ImageDraw, pf: ImageFont.ImageFont, kind: str) -> None:
    _pick(draw, 180, 240, 1, pf)
    _pick(draw, 420, 180, 2, pf)
    _line(draw, [(180, 240), (420, 180)], RED, 2)
    if kind == "inverse":
        draw.text((260, 195), "Δ", fill=BLACK, font=_font(16))
    elif kind == "dist":
        draw.text((260, 195), "3D", fill=BLACK, font=_font(14))


def _diag_view(draw: ImageDraw.ImageDraw, pf: ImageFont.ImageFont, kind: str) -> None:
    draw.rectangle([140, 160, 440, 290], outline=BLACK, width=2)
    if kind == "zoom_extents":
        _poly(draw, [(200, 250), (320, 250), (320, 190), (200, 190)], RED, 1)
        draw.rectangle([130, 150, 450, 300], outline=RED, width=1)
    elif kind == "zoom_window":
        draw.rectangle([220, 200, 360, 260], outline=RED, width=2)
        _pick(draw, 220, 200, 1, pf)
        _pick(draw, 360, 260, 2, pf)
    elif kind == "pan":
        _arrow(draw, 260, 225, 340, 225)
    elif kind == "orbit":
        draw.ellipse([220, 180, 360, 300], outline=RED, width=2)
        _arrow(draw, 360, 240, 400, 210)
    elif kind == "mview":
        draw.line([(290, 160), (290, 290)], fill=RED, width=1)
        draw.line([(140, 225), (440, 225)], fill=RED, width=1)
    else:
        _poly(draw, [(200, 250), (300, 250), (300, 190), (200, 190)], RED, 1)


def _diag_layer(draw: ImageDraw.ImageDraw, pf: ImageFont.ImageFont) -> None:
    for i, c in enumerate((RED, GREY_SHAPE, BLACK)):
        y = 175 + i * 38
        draw.rectangle([160, y, 420, y + 28], outline=c, width=2)
        draw.text((430, y + 4), f"L{i}", fill=BODY, font=pf)


def _diag_log_list(draw: ImageDraw.ImageDraw, pf: ImageFont.ImageFont, items: list[str], title: str) -> None:
    _diagram_bg(draw)
    draw.rectangle([110, 168, 470, 292], fill=(248, 248, 248), outline=BLACK, width=1)
    draw.rectangle([110, 168, 470, 192], fill=(210, 225, 240))
    draw.text((120, 173), title, fill=TITLE, font=_font(12, bold=True))
    y = 200
    for item in items[:5]:
        draw.text((125, y), item, fill=TITLE, font=pf)
        y += 18


def _diag_workflow(draw: ImageDraw.ImageDraw, pf: ImageFont.ImageFont, steps: list[str]) -> None:
    _diagram_bg(draw)
    y = DIAG_TOP + 24
    for i, step in enumerate(steps[:4], 1):
        draw.ellipse([PAD + 8, y, PAD + 28, y + 20], fill=GREEN, outline=GREEN)
        draw.text((PAD + 13, y + 2), str(i), fill=WHITE, font=pf)
        for j, ln in enumerate(_wrap(step, draw, pf, W - PAD * 2 - 40)):
            draw.text((PAD + 38, y + j * 14), ln, fill=TITLE, font=pf)
        y += 34 + 14 * max(0, len(_wrap(step, draw, pf, W - PAD * 2 - 40)) - 1)


def _diag_dialog(draw: ImageDraw.ImageDraw, pf: ImageFont.ImageFont, steps: list[str] | None = None) -> None:
    if steps:
        _diag_workflow(draw, pf, steps)
        return
    _diagram_bg(draw)
    draw.rectangle([150, 175, 430, 285], outline=BLACK, width=2, fill=(245, 245, 245))
    draw.rectangle([150, 175, 430, 205], fill=(210, 225, 240))
    draw.text((165, 182), "Panel / dialog", fill=TITLE, font=pf)
    draw.line([(170, 220), (410, 220)], fill=GREY_SHAPE, width=1)
    draw.line([(170, 245), (350, 245)], fill=GREY_SHAPE, width=1)


def _diag_file(draw: ImageDraw.ImageDraw, pf: ImageFont.ImageFont, imp: bool) -> None:
    draw.rectangle([180, 180, 260, 260], outline=BLACK, width=2)
    draw.text((200, 210), ".csv", fill=BODY, font=pf)
    if imp:
        _arrow(draw, 280, 220, 360, 220)
        draw.rectangle([360, 170, 460, 270], outline=RED, width=2)
    else:
        _arrow(draw, 360, 220, 280, 220)
        draw.rectangle([360, 170, 460, 270], outline=RED, width=2)


def _diag_pdf(draw: ImageDraw.ImageDraw, pf: ImageFont.ImageFont) -> None:
    draw.rectangle([160, 170, 420, 280], outline=GREY_SHAPE, width=2)
    draw.text((200, 210), "PDF page", fill=BODY, font=pf)
    _pick(draw, 290, 225, 1, pf)


def _diag_select(draw: ImageDraw.ImageDraw, pf: ImageFont.ImageFont) -> None:
    draw.rectangle([170, 180, 410, 270], outline=RED, width=1)
    _poly(draw, [(220, 250), (300, 250), (300, 200), (220, 200)], RED, 2)
    _pick(draw, 195, 175, 1, pf)
    _pick(draw, 415, 275, 2, pf)


def _diag_hide(draw: ImageDraw.ImageDraw, pf: ImageFont.ImageFont, isolate: bool) -> None:
    if isolate:
        _poly(draw, [(260, 230), (340, 230), (340, 170), (260, 170)], RED, 2)
        draw.text((170, 210), "…", fill=GREY_SHAPE, font=_font(24))
    else:
        _poly(draw, [(260, 230), (340, 230), (340, 170), (260, 170)], GREY_SHAPE, 1)
    _pick(draw, 300, 200, 1, pf)


def _diag_generic(draw: ImageDraw.ImageDraw, pf: ImageFont.ImageFont) -> None:
    _poly(draw, [(180, 260), (400, 260), (400, 170), (180, 170)])
    _pick(draw, 290, 215, 1, pf)


DIAGRAMS: dict[str, Callable[[ImageDraw.ImageDraw, ImageFont.ImageFont, CardSpec], None]] = {}


def _register(name: str):
    def deco(fn):
        DIAGRAMS[name] = fn
        return fn

    return deco


@_register("line")
def d_line(d, f, s):
    _diag_line(d, f)


@_register("polyline")
def d_polyline(d, f, s):
    _diag_polyline(d, f)


@_register("circle")
def d_circle(d, f, s):
    _diag_circle(d, f)


@_register("arc")
def d_arc(d, f, s):
    _diag_arc(d, f)


@_register("ellipse")
def d_ellipse(d, f, s):
    _diag_ellipse(d, f)


@_register("rect")
def d_rect(d, f, s):
    _diag_rect(d, f)


@_register("hatch")
def d_hatch(d, f, s):
    _diag_hatch(d, f)


@_register("offset")
def d_offset(d, f, s):
    _diag_offset(d, f)


@_register("move")
def d_move(d, f, s):
    _diag_move_copy(d, f, False)


@_register("copy")
def d_copy(d, f, s):
    _diag_move_copy(d, f, True)


@_register("rotate")
def d_rotate(d, f, s):
    _diag_rotate(d, f)


@_register("scale")
def d_scale(d, f, s):
    _diag_scale(d, f)


@_register("mirror")
def d_mirror(d, f, s):
    _diag_mirror(d, f)


@_register("trim")
def d_trim(d, f, s):
    _diag_trim(d, f)


@_register("extend")
def d_extend(d, f, s):
    _diag_extend(d, f)


@_register("fillet")
def d_fillet(d, f, s):
    _diag_fillet(d, f)


@_register("chamfer")
def d_chamfer(d, f, s):
    _diag_chamfer(d, f)


@_register("delete")
def d_delete(d, f, s):
    _diag_delete(d, f)


@_register("join")
def d_join(d, f, s):
    _diag_join(d, f)


@_register("break")
def d_break(d, f, s):
    _diag_break(d, f)


@_register("stretch")
def d_stretch(d, f, s):
    _diag_stretch(d, f)


@_register("array")
def d_array(d, f, s):
    _diag_array_rect(d, f)


@_register("extrude")
def d_extrude(d, f, s):
    _diag_extrude(d, f)


@_register("revolve")
def d_revolve(d, f, s):
    _diag_revolve(d, f)


@_register("sweep")
def d_sweep(d, f, s):
    _diag_sweep(d, f)


@_register("loft")
def d_loft(d, f, s):
    _diag_loft(d, f)


@_register("presspull")
def d_presspull(d, f, s):
    _diag_presspull(d, f)


@_register("slice")
def d_slice(d, f, s):
    _diag_slice(d, f)


@_register("section")
def d_section(d, f, s):
    _diag_section(d, f)


@_register("polysolid")
def d_polysolid(d, f, s):
    _diag_polysolid(d, f)


@_register("workflow")
def d_workflow(d, f, s):
    _diag_workflow(d, f, s.params.get("steps", ["Run the command", "Follow the prompts"]))


@_register("list_log")
def d_list_log(d, f, s):
    _diag_log_list(
        d,
        f,
        s.params.get("items", ["Item 1", "Item 2", "Item 3"]),
        s.params.get("title", "Command log"),
    )


@_register("box_solid")
def d_box_solid(d, f, s):
    _diag_box_solid(d, f, s.params.get("solid", "box"))


@_register("boolean")
def d_boolean(d, f, s):
    _diag_boolean(d, f, s.params.get("mode", "union"))


@_register("text")
def d_text(d, f, s):
    _diag_text(d, f, False)


@_register("mtext")
def d_mtext(d, f, s):
    _diag_text(d, f, True)


@_register("dimension")
def d_dimension(d, f, s):
    _diag_dimension(d, f, s.params.get("dim", "linear"))


@_register("block")
def d_block(d, f, s):
    _diag_block(d, f, s.params.get("mode", "def"))


@_register("surface")
def d_surface(d, f, s):
    _diag_surface(d, f)


@_register("points")
def d_points(d, f, s):
    _diag_points(d, f)


@_register("measure")
def d_measure(d, f, s):
    _diag_measure(d, f, s.params.get("measure", "id"))


@_register("view")
def d_view(d, f, s):
    _diag_view(d, f, s.params.get("view", "generic"))


@_register("layer")
def d_layer(d, f, s):
    _diag_layer(d, f)


@_register("dialog")
def d_dialog(d, f, s):
    _diag_dialog(d, f, s.params.get("steps"))


@_register("file")
def d_file(d, f, s):
    _diag_file(d, f, s.params.get("import", True))


@_register("pdf")
def d_pdf(d, f, s):
    _diag_pdf(d, f)


@_register("select")
def d_select(d, f, s):
    _diag_select(d, f)


@_register("hide")
def d_hide(d, f, s):
    _diag_hide(d, f, s.params.get("isolate", False))


@_register("generic")
def d_generic(d, f, s):
    _diag_generic(d, f)


def render_card(spec: CardSpec) -> Image.Image:
    img = Image.new("RGB", (W, H), BG)
    draw = ImageDraw.Draw(img)
    _header(draw, spec)
    pf = _font(12)
    fn = DIAGRAMS.get(spec.kind, d_generic)
    fn(draw, pf, spec)
    return img


def tagline_from_description(description: str) -> str:
    text = description.strip()
    if not text:
        return "GoSurvey command."
    for sep in (". ", "; ", " — ", " - "):
        if sep in text:
            text = text.split(sep)[0]
            break
    if len(text) > 90:
        text = text[:87] + "…"
    return text[0].upper() + text[1:] if text else "GoSurvey command."


def title_for_primary(primary: str) -> str:
    return primary.upper()
