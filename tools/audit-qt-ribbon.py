"""Audit the native ribbon inventory against the local v0.7.2 release tag."""
import json
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
def release(path):
    return subprocess.check_output(["git", "show", "v0.7.2:" + path], cwd=ROOT).decode("utf-8")

def main():
    source = release("src/ui/CadUi.cpp")
    start = source.index('  if (cmd.activeRibbonTab == kRibbonTabHome) {', source.index('void DrawRibbonBar'))
    end = source.index('  // REQ-302 increment 2 (ADR-038 (a)): decide breakpoint', start)
    body = source[start:end]
    expected = set(re.findall(r'\b(?:rowBtn|iconBtn2Row|iconBtn|largeBtnSpecEx|largeBtnSpec|beParam|RibbonButtonEx)\(\s*"(##[^"\n]+)"', body))
    for call in re.findall(r'\bcol\((.*?)\)\s*[,}]', body, re.S):
        expected.update(re.findall(r'"(##[^"\n]+)"', call))
    expected.update(re.findall(r'\{"(##[^"\n]+)"', release("src/ui/ModelingRibbon.hpp")))
    expected.update({"##RibbonLAY", "##PdfBgBtn", "##PdfVecBtn", "##BeClose"})
    # Issue #811: Civil 3D project-data palettes are removed from the native ribbon (not implemented).
    expected -= {"##PalPanorama", "##PalRefMgr", "##PalCompEd", "##PalWorkFolder", "##ExpProjExplorer"}
    catalog = (ROOT / "src/qt/RibbonCatalog.hpp").read_text(encoding="utf-8")
    rows = []
    for line in catalog.splitlines():
        if not line.startswith('  {"'): continue
        fields = re.findall(r'"(?:\\.|[^"\\])*"', line)
        rows.append([json.loads(field) for field in fields])
    ids = [row[2] for row in rows]
    errors = []
    if len(ids) != len(set(ids)): errors.append("Duplicate ribbon IDs")
    if expected - set(ids): errors.append("Missing release buttons: " + ", ".join(sorted(expected - set(ids))))
    icons = {p.stem.lower() for p in (ROOT / "resources/icons").glob("*.png")}
    for row in rows:
        if row[4].lower() not in icons: errors.append(f"Missing icon: {row[2]} -> {row[4]}")
        if not row[3]: errors.append(f"Missing label: {row[2]}")
    if errors: raise SystemExit("\n".join(errors))
    print(f"PASS: {len(expected)} release button IDs covered; {len(rows)} native entries; all icons exist.")

if __name__ == "__main__": main()
