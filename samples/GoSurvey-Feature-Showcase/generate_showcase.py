"""Create original GoSurvey showcase DWGs from small, authored DXF/CSV sources.

Run from the repository root after building: python samples/GoSurvey-Feature-Showcase/generate_showcase.py
The source DXFs and transcripts are temporary; only the GoSurvey DWGs are delivered.
"""
from __future__ import annotations

import math
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PROJECT = ROOT / "samples" / "GoSurvey-Feature-Showcase"
OUT = PROJECT / "Drawings"
DRIVER = ROOT / "build" / ".cmake" / "release" / "tools" / "gosurvey_headless" / "gosurvey_headless.exe"


class Dxf:
    def __init__(self):
        self.entities: list[str] = []
        self.layers: dict[str, int] = {"0": 7}

    def entity(self, kind: str, *pairs):
        data = [(0, kind), (8, "0")]
        data.extend(pairs)
        self.entities.extend(f"{code}\n{value}\n" for code, value in data)

    def line(self, a, b):
        self.entity("LINE", (10, a[0]), (20, a[1]), (30, a[2]), (11, b[0]), (21, b[1]), (31, b[2]))

    def circle(self, p, r):
        self.entity("CIRCLE", (10, p[0]), (20, p[1]), (30, p[2]), (40, r))

    def arc(self, p, r, start, end):
        self.entity("ARC", (10, p[0]), (20, p[1]), (30, p[2]), (40, r), (50, start), (51, end))

    def poly(self, pts, closed=False, bulges=None):
        elevations = [p[2] if len(p) > 2 else 0 for p in pts]
        elevation = elevations[0] if elevations else 0
        if any(z != elevation for z in elevations):
            if bulges and any(bulges):
                raise ValueError("DXF 3D polylines cannot contain bulged segments")
            self.entity("POLYLINE", (66, 1), (10, 0), (20, 0), (30, 0), (70, 8 | (1 if closed else 0)))
            for p, z in zip(pts, elevations):
                self.entity("VERTEX", (10, p[0]), (20, p[1]), (30, z), (70, 32))
            self.entity("SEQEND")
            return
        data = [(90, len(pts)), (70, 1 if closed else 0), (38, elevation)]
        for i, p in enumerate(pts):
            data += [(10, p[0]), (20, p[1])]
            if bulges and bulges[i]:
                data.append((42, bulges[i]))
        self.entity("LWPOLYLINE", *data)

    def text(self, p, s, h=2.5):
        self.entity("TEXT", (10, p[0]), (20, p[1]), (30, p[2]), (40, h), (1, s))

    def face(self, pts):
        q = list(pts)
        if len(q) == 3:
            q.append(q[-1])
        data = []
        for i, p in enumerate(q[:4]):
            data += [(10+i, p[0]), (20+i, p[1]), (30+i, p[2])]
        self.entity("3DFACE", *data)

    def write(self, path: Path):
        header = "0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\nAC1015\n0\nENDSEC\n0\nSECTION\n2\nTABLES\n0\nTABLE\n2\nLAYER\n70\n" + str(len(self.layers)) + "\n"
        for name, color in self.layers.items():
            header += f"0\nLAYER\n2\n{name}\n70\n0\n62\n{color}\n6\nCONTINUOUS\n"
        header += "0\nENDTAB\n0\nENDSEC\n0\nSECTION\n2\nENTITIES\n"
        # DXF R2000 text payloads are authored as ASCII here; replace typographic punctuation
        # in drawing labels with a plain dash instead of rejecting the whole source file.
        payload = (header + "".join(self.entities) + "0\nENDSEC\n0\nEOF\n").replace("—", "-")
        path.write_text(payload, encoding="ascii", errors="replace")


def rect(d, x, y, w, h, z=0):
    d.poly([(x,y,z),(x+w,y,z),(x+w,y+h,z),(x,y+h,z)], True)


def boxwire(d, x, y, z, w, h, depth):
    for dz in (0, depth):
        rect(d, x, y, w, h, z + dz)
    for px, py in ((x,y), (x+w,y), (x+w,y+h), (x,y+h)):
        d.line((px, py, z), (px, py, z + depth))


def cylinder(d, x, y, z, radius, height, sides=16):
    bottom = [(x + radius*math.cos(2*math.pi*i/sides), y + radius*math.sin(2*math.pi*i/sides), z) for i in range(sides)]
    top = [(px, py, z + height) for px, py, _ in bottom]
    d.poly(bottom, True); d.poly(top, True)
    for i in range(0, sides, 4): d.line(bottom[i], top[i])


def write_sources(tmp: Path):
    names = ["01-Geometry-and-Civil-Objects", "09-Mountainous-Surface", "10-Grading", "11-Alignments", "12-Primitive-3D-Models", "13-Complex-Piping-System", "14-Additional-Features"]
    drawings = [Dxf() for _ in names]

    # 1–8: hand-drafted geometry, vertices, arcs and field elevations.
    d = drawings[0]
    d.text((0,112,0), "01  GEOMETRY AND SURVEY OBJECTS", 4)
    for a,b in [((0,0,0),(40,0,0)),((40,0,0),(40,30,0)),((40,30,0),(0,30,0)),((0,30,0),(0,0,0))]:
        d.line(a,b)
    for p in [(0,0,0),(40,0,0),(40,30,0),(0,30,0)]: d.circle(p,1.8)
    rect(d,55,0,35,30)
    rect(d,105,0,35,30)
    d.text((0,-8,0),"1 LINES + ENDPOINT CIRCLES     2 CLOSED POLYLINE     3 RECTANGLE",2)
    d.arc((20,55,0),15,20,155); d.arc((70,55,0),15,210,345)
    d.poly([(105,40,0),(135,40,0),(150,55,0),(135,70,0),(105,70,0)],False,[0,0.4142,0,0,-0.4142])
    d.poly([(0,85,0),(15,85,3),(30,85,7),(45,85,4),(60,85,1)])
    d.poly([(0,95,0),(15,100,5),(30,105,8),(45,100,6)])
    d.poly([(60,85,0),(75,85,2),(90,90,5),(105,95,9)])
    d.text((0,75,0),"4 ARCS     5 BULGED POLYLINE     6 3D POLYLINES     7 FEATURE-LINE CANDIDATES",2)
    for i in range(16):
        x=(i%8)*8+3; y=(i//8)*10+120
        d.circle((x,y,100+i*.23),.8); d.text((x+1,y,100+i*.23),f"{i+1}",1.2)
    d.text((0,142,0),"8 SURVEY CONTROL POINTS (3D MARKERS; CSV DATA IN PROJECT)",2)

    # 9: original 25 by 20 TIN built from 500 measured-style topo observations.
    surf=drawings[1]; surf.text((0,222,0),"09  RIDGE AND BASIN — 500 POINT TERRAIN",5)
    # Survey point symbols and generated labels remain in the DWG for inspection but start hidden,
    # so they do not bury the actual surface. The TIN itself stays on its own visible layer.
    surf.layers["V-NODE"] = -7
    surf.layers["V-NODE-TEXT"] = -7
    def elev(x,y):
        return 34*math.exp(-((x-88)**2/1300+(y-108)**2/2200))+21*math.exp(-((x-175)**2/900+(y-65)**2/1200))+7*math.sin(x/19)*math.cos(y/22)+2*math.sin((x+y)/11)
    # 10-ft index contours sampled from the authored height field.
    for level in [5,10,15,20,25,30,35,40,45]:
        for row in range(0,20,2):
            pts=[]
            for col in range(25):
                x=col*10.; y=row*10.; z=elev(x,y)
                if z>=level: pts.append((x,y,level))
            if len(pts)>1: surf.poly(pts)
    csv=tmp/"mountain-points.csv"
    csv.write_text("Point,Northing,Easting,Elevation,Description\n"+"".join(f"{r*25+c+1},{r*10:.3f},{c*10:.3f},{elev(c*10,r*10):.3f},TOPO\n" for r in range(20) for c in range(25)),encoding="ascii")

    # 10: an authored cut/fill pad with slope benches and labelled toe/crest lines.
    g=drawings[2]; g.text((0,100,0),"10  PAD GRADING STUDY — DATUM 100",4)
    rect(g,0,0,50,35,100); rect(g,10,8,30,19,112)
    for off in [0,4,8,12]:
        g.poly([(-off,-off,100), (50+off,-off,100), (50+off,35+off,100),( -off,35+off,100)],True)
    g.poly([(0,0,100),(50,0,100),(50,35,100),(0,35,100)],True)
    for x in [0,10,20,30,40,50]: g.line((x,0,100),(x,35,112 if 10<x<40 else 100))
    g.text((2,-7,0),"TOE / DAYLIGHT",2); g.text((12,10,113),"PROPOSED PAD 112.00",2)
    g.text((0,55,0),"Four 4-unit benches shown from toe to pad. Feature line and grading creation exercise area.",2)

    # 11: centerline/stations, parallel offsets and profile markers.
    a=drawings[3]; a.text((0,115,0),"11  NORTH RIDGE ACCESS ALIGNMENT",4)
    center=[(0,0,0),(35,10,0),(70,25,0),(105,22,0),(140,48,0),(175,55,0),(210,78,0)]
    a.poly(center); a.poly([(x-3,y-6,z) for x,y,z in center]); a.poly([(x+3,y+6,z) for x,y,z in center])
    for i,(x,y,z) in enumerate(center):
        a.circle((x,y,0),1.4); a.text((x+1,y+2,0),f"{i*100:03d}+00",1.7)
        a.line((x,y,0),(x,y,12+i*1.5))
        a.circle((x,125+i*1.5,0),1)
    a.poly([(0,125,0),(35,130,0),(70,137,0),(105,140,0),(140,151,0),(175,155,0),(210,165,0)])
    a.text((0,105,0),"CENTERLINE / OFFSETS / STATION TICKS / PROFILE",2)

    # 12: seven native solids are created through GoSurvey commands in the transcript below.
    m=drawings[4]; m.text((0,112,0),"12  PRIMITIVE MODEL GALLERY",4)
    for x,label in [(0,"BOX"),(28,"WEDGE"),(56,"PYRAMID"),(92,"CYLINDER"),(116,"CONE"),(136,"SPHERE"),(165,"TORUS")]: m.text((x, -8, 0),label,2)

    # 13: multi-branch plant model with elevated headers, vertical risers and equipment.
    p=drawings[5]; p.text((0,125,0),"13  PROCESS PIPING — HEADER / PUMP / BRANCH NETWORK",4)
    # main 3D headers and branches
    for z in [4,22]:
        p.poly([(0,20,z),(45,20,z),(50,25,z),(90,25,z),(96,20,z),(150,20,z),(156,26,z),(210,26,z)])
    for x in [20,55,90,125,160,195]:
        p.poly([(x,20,4),(x,20,12),(x,20,22)])
        p.circle((x,20,4),3); p.circle((x,20,22),2.5)
    for y in [45,70,95]:
        p.poly([(35,y,4),(35,y,12),(45,y+5,17),(75,y+5,17),(84,y,22)])
        p.poly([(125,y,4),(125,y,12),(135,y+5,17),(165,y+5,17),(174,y,22)])
    # equipment skids, exchanger, tank, pump/motor symbols
    boxwire(p,45,46,0,34,20,14); boxwire(p,135,46,0,34,20,14)
    cylinder(p,103,78,0,13,34); cylinder(p,103,78,34,14,2)
    p.circle((24,103,8),8); p.circle((24,103,8),5); p.line((24,103,0),(24,103,18))
    for x in range(0,211,10): p.line((x,0,0),(x,112,0))
    for y in range(0,113,10): p.line((0,y,0),(210,y,0))
    p.text((0,-7,0),"PIPE RACK GRID 10 x 10  |  TWIN HEADERS  |  6 RISERS  |  6 PROCESS BRANCHES  |  VESSEL + PUMPS",2)

    # 14: original utility/site plan containing structures, dimensions, drainage and callouts.
    x=drawings[6]; x.text((0,145,0),"14  SITE UTILITIES AND DETAILS",4)
    rect(x,0,0,100,65); rect(x,15,12,30,20); rect(x,58,12,27,20); rect(x,15,40,70,12)
    x.poly([(5,5,0),(95,5,0),(95,60,0),(5,60,0)],True)
    for i in range(11): x.circle((i*9+5,5,0),1)
    x.poly([(0,75,0),(20,75,0),(24,80,0),(48,80,0),(55,88,0),(75,88,0),(85,100,0),(100,100,0)])
    x.poly([(0,85,0),(24,85,0),(28,92,0),(55,92,0),(60,101,0),(80,101,0)])
    for px,py in [(24,80),(55,88),(85,100),(28,92),(60,101)]:
        x.circle((px,py,0),2); x.line((px-3,py,0),(px+3,py,0)); x.line((px,py-3,0),(px,py+3,0))
    x.text((15,34,0),"BUILDING A",2.5); x.text((59,34,0),"PARKING",2.5)
    x.text((0,110,0),"STORM / SANITARY ROUTES WITH STRUCTURES AND SITE CONTROL",2.5)

    for name,drawing in zip(names,drawings): drawing.write(tmp/(name+".dxf"))
    return names, csv


def main():
    if not DRIVER.exists():
        raise SystemExit(f"Build first; headless GoSurvey driver is missing: {DRIVER}")
    OUT.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="gosurvey-showcase-") as td:
        tmp=Path(td)
        names,csv=write_sources(tmp)
        for i,name in enumerate(names):
            transcript=tmp/(name+".txt")
            lines=[f"IMPORT DXF {tmp/(name+'.dxf')}" ]
            if i==1:
                lines += [
                    "CMD SURFACECREATE Mountain Terrain",
                    f"CMD SURFACEADDFILE Mountain Terrain, {csv}, PNEZD, HEADER",
                    "CMD SURFACEIMPORTFILE Mountain Terrain, 1",
                    "CMD SURFACEREBUILD Mountain Terrain",
                    "CMD SURFSTYLE SHOW Standard, triangles",
                ]
            if i==4:
                lines += [
                    "CMD BOX 0,0,0 18 18 14",
                    "CMD WEDGE 28,0,0 18 18 14",
                    "CMD PYRAMID 56,0,0 4 9 0 17",
                    "CMD CYLINDER 92,9,0 8 18",
                    "CMD CONE 116,9,0 8 0 16",
                    "CMD SPHERE 145,9,9 9",
                    "CMD TORUS 174,9,9 5 3",
                ]
            if i==5:
                # Two native 4-inch pipe runs create swept solids with bends and branches.
                for route in [
                    [(0,20,4),(45,20,4),(50,25,4),(96,25,4),(102,20,4),(210,20,4)],
                    [(0,20,22),(45,20,22),(50,25,22),(96,25,22),(102,20,22),(210,20,22)],
                    [(35,45,4),(35,45,14),(45,55,17),(75,55,17),(84,45,22)],
                    [(125,45,4),(125,45,14),(135,55,17),(165,55,17),(174,45,22)],
                ]:
                    lines += ["CMD PIPERUN", "CMD 4in CS150", "CMD"]
                    lines += [f"CMD {x},{y},{z}" for x,y,z in route]
                    lines.append("CMD END")
            lines.append(f"SAVEAS {OUT/(name+'.dwg')}")
            transcript.write_text("\n".join(lines)+"\n",encoding="utf-8")
            proc=subprocess.run([str(DRIVER),"run",str(transcript),"--print-log","--check-at-end"],cwd=ROOT,text=True,capture_output=True)
            print(proc.stdout)
            if proc.returncode:
                print(proc.stderr)
                raise SystemExit(proc.returncode)
            print(proc.stderr,end="")


if __name__ == "__main__":
    main()
