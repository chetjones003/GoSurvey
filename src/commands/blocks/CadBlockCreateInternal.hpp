#include "CadBlocks.hpp"
#include "CadCommands.hpp"
#include <string>
#include <vector>

namespace {

EntityAttributes NewBlockAttr(const AppCommandState& st) {
  EntityAttributes a;
  a.layer = st.drawing.currentLayer.empty() ? std::string("0") : st.drawing.currentLayer;
  a.color = st.drawing.currentColor.empty() ? std::string("ByLayer") : st.drawing.currentColor;  // REQ-356
  a.linetype = "ByLayer";
  a.lineweightMm = -1.f;
  a.transparency = -1.f;
  return a;
}

void NoteRecent(AppCommandState& st, const std::string& name) {
  st.blockRecent.erase(std::remove_if(st.blockRecent.begin(), st.blockRecent.end(),
                                      [&](const std::string& n) { return CadBlockEqCi(n, name); }),
                       st.blockRecent.end());
  st.blockRecent.insert(st.blockRecent.begin(), name);
  if (st.blockRecent.size() > 24)
    st.blockRecent.resize(24);
}

void CaptureSelectionInto(const AppCommandState& st, CadBlockContent* c, float bx, float by, float bz) {
  assert(c != nullptr);
  for (const SelectedEntity& e : st.selection) {
    if (e.type == SelectedEntity::Type::LineSeg) {
      const size_t li = static_cast<size_t>(e.index);
      if (li >= st.drawing.lineCount())
        continue;
      CadLineRecord ln = st.drawing.line(li);
      ln.x0 -= bx;
      ln.y0 -= by;
      ln.z0 -= bz;
      ln.x1 -= bx;
      ln.y1 -= by;
      ln.z1 -= bz;
      c->pushLine(ln);
      EntityAttributes a{};
      if (static_cast<size_t>(e.index) < st.drawing.userLineAttrs.size())
        a = st.drawing.userLineAttrs[static_cast<size_t>(e.index)];
      c->userLineAttrs.push_back(a);
      c->lineVis.push_back("");
    } else if (e.type == SelectedEntity::Type::Circle) {
      const size_t ci = static_cast<size_t>(e.index);
      if (ci >= st.drawing.circleCount())
        continue;
      CadCircleRecord cr = st.drawing.circle(ci);
      cr.cx -= bx;
      cr.cy -= by;
      cr.cz -= bz;
      c->pushCircle(cr);
      EntityAttributes a{};
      if (static_cast<size_t>(e.index) < st.drawing.userCircleAttrs.size())
        a = st.drawing.userCircleAttrs[static_cast<size_t>(e.index)];
      c->userCircleAttrs.push_back(a);
      float bnx = 0.f, bny = 0.f, bnz = 1.f;
      CircleNormalAt(st.drawing.userCircleNormals, static_cast<size_t>(e.index), &bnx, &bny, &bnz);
      PushCircleNormal(c->userCircleNormals, bnx, bny, bnz);
      c->circleVis.push_back("");
    } else if (e.type == SelectedEntity::Type::Annotation) {
      if (e.index < 0 || static_cast<size_t>(e.index) >= st.drawing.cadAnnotations.size())
        continue;
      CadAnnotation t = st.drawing.cadAnnotations[static_cast<size_t>(e.index)];
      t.insX -= bx;
      t.insY -= by;
      t.insZ -= bz;
      c->cadAnnotations.push_back(std::move(t));
      EntityAttributes a{};
      if (static_cast<size_t>(e.index) < st.drawing.cadAnnotationAttrs.size())
        a = st.drawing.cadAnnotationAttrs[static_cast<size_t>(e.index)];
      c->cadAnnotationAttrs.push_back(a);
    } else if (e.type == SelectedEntity::Type::Arc) {
      if (e.index < 0 || static_cast<size_t>(e.index) >= st.drawing.userArcs.size())
        continue;
      CadArc a = st.drawing.userArcs[static_cast<size_t>(e.index)];
      a.cx -= bx;
      a.cy -= by;
      c->userArcs.push_back(a);
      EntityAttributes at{};
      if (static_cast<size_t>(e.index) < st.drawing.userArcAttrs.size())
        at = st.drawing.userArcAttrs[static_cast<size_t>(e.index)];
      c->userArcAttrs.push_back(at);
    } else if (e.type == SelectedEntity::Type::BlockRef) {
      if (e.index < 0 || static_cast<size_t>(e.index) >= st.drawing.cadBlockRefs.size())
        continue;
      CadBlockNested n;
      n.defName = st.drawing.cadBlockRefs[static_cast<size_t>(e.index)].defName;
      n.xf = st.drawing.cadBlockRefs[static_cast<size_t>(e.index)].xf;
      n.xf.x -= bx;
      n.xf.y -= by;
      n.xf.z -= bz;
      c->nested.push_back(std::move(n));
    } else if (e.type == SelectedEntity::Type::Mesh) {
      if (e.index < 0 || static_cast<size_t>(e.index) >= st.drawing.cadMeshes.size())
        continue;
      c->cadMeshes.push_back(st.drawing.cadMeshes[static_cast<size_t>(e.index)]);
      EntityAttributes a{};
      if (static_cast<size_t>(e.index) < st.drawing.cadMeshAttrs.size())
        a = st.drawing.cadMeshAttrs[static_cast<size_t>(e.index)];
      c->cadMeshAttrs.push_back(a);
    } else if (e.type == SelectedEntity::Type::Solid) {
      if (e.index < 0 || static_cast<size_t>(e.index) >= st.drawing.cadSolids.size())
        continue;
      const CadSolidPtr& sp = st.drawing.cadSolids[static_cast<size_t>(e.index)];
      if (!sp)
        continue;
      brep::Solid local =
          brep::Translate(*sp, brep::Vec3{-static_cast<double>(bx), -static_cast<double>(by),
                                           -static_cast<double>(bz)});
      c->cadSolids.push_back(std::make_shared<const brep::Solid>(std::move(local)));
      EntityAttributes a{};
      if (static_cast<size_t>(e.index) < st.drawing.cadSolidAttrs.size())
        a = st.drawing.cadSolidAttrs[static_cast<size_t>(e.index)];
      c->cadSolidAttrs.push_back(a);
    }
  }
}

void EraseSelectedSources(AppCommandState& st) {
  std::vector<int> lines;
  std::vector<int> circles;
  std::vector<int> anns;
  std::vector<int> arcs;
  std::vector<int> meshes;
  std::vector<int> refs;
  // Solids were missing here while `CaptureSelectionInto` has copied them into the definition since
  // REQ-320 — so BLOCK's "Convert to block" and "Delete" left the original solid in the drawing
  // alongside a block reference holding a copy of it, silently duplicating the geometry (found by
  // the `block-create-basepoint-pick` GUI test, 2026-09-23).
  std::vector<int> solids;
  for (const SelectedEntity& e : st.selection) {
    if (e.type == SelectedEntity::Type::LineSeg)
      lines.push_back(e.index);
    else if (e.type == SelectedEntity::Type::Circle)
      circles.push_back(e.index);
    else if (e.type == SelectedEntity::Type::Annotation)
      anns.push_back(e.index);
    else if (e.type == SelectedEntity::Type::Arc)
      arcs.push_back(e.index);
    else if (e.type == SelectedEntity::Type::Mesh)
      meshes.push_back(e.index);
    else if (e.type == SelectedEntity::Type::BlockRef)
      refs.push_back(e.index);
    else if (e.type == SelectedEntity::Type::Solid)
      solids.push_back(e.index);
  }
  auto dropLines = [&](std::vector<int>& idx) {
    std::sort(idx.begin(), idx.end());
    idx.erase(std::unique(idx.begin(), idx.end()), idx.end());
    for (int i = static_cast<int>(idx.size()) - 1; i >= 0; --i) {
      const int k = idx[static_cast<size_t>(i)];
      if (k < 0)
        continue;
      if (static_cast<size_t>(k) < st.drawing.lineCount())
        st.drawing.eraseLine(static_cast<size_t>(k));
      if (static_cast<size_t>(k) < st.drawing.userLineAttrs.size())
        st.drawing.userLineAttrs.erase(st.drawing.userLineAttrs.begin() + k);
    }
  };
  dropLines(lines);
  auto dedup = [](std::vector<int>& v) {
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
  };
  dedup(circles);
  for (int i = static_cast<int>(circles.size()) - 1; i >= 0; --i) {
    const int k = circles[static_cast<size_t>(i)];
    if (static_cast<size_t>(k) < st.drawing.circleCount())
      st.drawing.eraseCircle(static_cast<size_t>(k));
    if (static_cast<size_t>(k) < st.drawing.userCircleAttrs.size())
      st.drawing.userCircleAttrs.erase(st.drawing.userCircleAttrs.begin() + k);
    EraseCircleNormal(st.drawing.userCircleNormals, static_cast<size_t>(k));
  }
  dedup(anns);
  for (int i = static_cast<int>(anns.size()) - 1; i >= 0; --i) {
    const int k = anns[static_cast<size_t>(i)];
    if (k >= 0 && static_cast<size_t>(k) < st.drawing.cadAnnotations.size())
      st.drawing.cadAnnotations.erase(st.drawing.cadAnnotations.begin() + k);
    if (k >= 0 && static_cast<size_t>(k) < st.drawing.cadAnnotationAttrs.size())
      st.drawing.cadAnnotationAttrs.erase(st.drawing.cadAnnotationAttrs.begin() + k);
  }
  dedup(arcs);
  for (int i = static_cast<int>(arcs.size()) - 1; i >= 0; --i) {
    const int k = arcs[static_cast<size_t>(i)];
    if (k >= 0 && static_cast<size_t>(k) < st.drawing.userArcs.size())
      st.drawing.userArcs.erase(st.drawing.userArcs.begin() + k);
    if (k >= 0 && static_cast<size_t>(k) < st.drawing.userArcAttrs.size())
      st.drawing.userArcAttrs.erase(st.drawing.userArcAttrs.begin() + k);
  }
  std::sort(meshes.begin(), meshes.end());
  for (int i = static_cast<int>(meshes.size()) - 1; i >= 0; --i) {
    const int k = meshes[static_cast<size_t>(i)];
    if (k >= 0 && static_cast<size_t>(k) < st.drawing.cadMeshes.size())
      st.drawing.cadMeshes.erase(st.drawing.cadMeshes.begin() + k);
    if (k >= 0 && static_cast<size_t>(k) < st.drawing.cadMeshAttrs.size())
      st.drawing.cadMeshAttrs.erase(st.drawing.cadMeshAttrs.begin() + k);
  }
  std::sort(refs.begin(), refs.end());
  for (int i = static_cast<int>(refs.size()) - 1; i >= 0; --i) {
    const int k = refs[static_cast<size_t>(i)];
    if (k >= 0 && static_cast<size_t>(k) < st.drawing.cadBlockRefs.size())
      st.drawing.cadBlockRefs.erase(st.drawing.cadBlockRefs.begin() + k);
    if (k >= 0 && static_cast<size_t>(k) < st.drawing.cadBlockRefAttrs.size())
      st.drawing.cadBlockRefAttrs.erase(st.drawing.cadBlockRefAttrs.begin() + k);
  }
  std::sort(solids.begin(), solids.end());
  for (int i = static_cast<int>(solids.size()) - 1; i >= 0; --i) {
    const int k = solids[static_cast<size_t>(i)];
    if (k >= 0 && static_cast<size_t>(k) < st.drawing.cadSolids.size())
      st.drawing.cadSolids.erase(st.drawing.cadSolids.begin() + k);
    if (k >= 0 && static_cast<size_t>(k) < st.drawing.cadSolidAttrs.size())
      st.drawing.cadSolidAttrs.erase(st.drawing.cadSolidAttrs.begin() + k);
  }
  st.selection.clear();
}

} // namespace
