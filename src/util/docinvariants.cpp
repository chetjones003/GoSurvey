#include "docinvariants.hpp"

#include "CadCommands.hpp"

#include <cmath>
#include <cstdio>
#include <unordered_map>

namespace {

void Add(std::vector<InvariantViolation>* out, const char* name, std::string detail,
         int entityIndex = -1) {
  out->push_back(InvariantViolation{name, std::move(detail), entityIndex});
}

/// `size` floats must be a whole number of `stride`-float vertices.
void CheckStride(std::vector<InvariantViolation>* out, const char* store, size_t size, size_t stride) {
  if (stride == 0 || size % stride == 0)
    return;
  Add(out, docinv::kFlatStrides,
      std::string(store) + ".size()=" + std::to_string(size) + " is not a multiple of " +
          std::to_string(stride));
}

/// Every float in a flat store must be finite. Reports the FIRST offending index only: a NaN
/// usually arrives in a whole run, and one report per float would bury the finding in its own noise.
void CheckFinite(std::vector<InvariantViolation>* out, const char* store,
                 const std::vector<float>& v) {
  for (size_t i = 0; i < v.size(); ++i) {
    if (std::isfinite(v[i]))
      continue;
    char buf[64];
    std::snprintf(buf, sizeof buf, "%g", static_cast<double>(v[i]));
    Add(out, docinv::kFiniteCoords,
        std::string(store) + "[" + std::to_string(i) + "] = " + buf, static_cast<int>(i));
    return;
  }
}

void CheckFiniteScalar(std::vector<InvariantViolation>* out, const char* what, float value,
                       int entityIndex) {
  if (std::isfinite(value))
    return;
  char buf[64];
  std::snprintf(buf, sizeof buf, "%g", static_cast<double>(value));
  Add(out, docinv::kFiniteCoords, std::string(what) + " = " + buf, entityIndex);
}

/// A geometry array and its parallel attribute array must describe the same number of entities.
void CheckAttrCount(std::vector<InvariantViolation>* out, const char* what, size_t entities,
                    size_t attrs) {
  if (entities == attrs)
    return;
  Add(out, docinv::kAttrCounts,
      std::string(what) + ": " + std::to_string(entities) + " entities but " +
          std::to_string(attrs) + " attribute rows");
}

/// Number of polylines. `userPolylineOffsets` is CSR, not one-entry-per-entity: polyline i spans
/// vertices [offsets[i], offsets[i+1]), so N polylines need N+1 offsets and an empty drawing has 0.
///
/// This is written down here because the obvious reading — one offset per polyline — is wrong, and
/// getting it wrong makes every one of these checks fire on every valid drawing. That is not a
/// hypothetical: the first version of this file made exactly that mistake and reported
/// "2 entities but 1 attribute rows" against a perfectly good single polyline.
size_t PolylineCount(const AppCommandState& st) {
  return st.userPolylineOffsets.empty() ? 0 : st.userPolylineOffsets.size() - 1;
}

/// Collect one entity's id into the uniqueness map, reporting a collision against whatever held it.
void NoteId(std::vector<InvariantViolation>* out, std::unordered_map<std::uint64_t, std::string>* seen,
            std::uint64_t id, const char* store, size_t index, std::uint64_t nextEntityId) {
  if (id == 0)
    return;  // 0 means "unassigned" — a legitimate transient state before the next EnsureEntityIds sweep.

  const std::string where = std::string(store) + "[" + std::to_string(index) + "]";
  const auto it = seen->find(id);
  if (it != seen->end()) {
    Add(out, docinv::kEntityIds,
        "id " + std::to_string(id) + " is used by both " + it->second + " and " + where,
        static_cast<int>(index));
  } else {
    (*seen)[id] = where;
  }

  if (id >= nextEntityId) {
    Add(out, docinv::kEntityIds,
        where + " has id " + std::to_string(id) + " but nextEntityId is " +
            std::to_string(nextEntityId) + " — the next entity created would reuse it",
        static_cast<int>(index));
  }
}

}  // namespace

void CheckDocumentInvariants(const AppCommandState& st, std::vector<InvariantViolation>* out) {
  if (!out)
    return;

  // --- Flat store strides (architecture §11.8) -----------------------------------------------
  CheckStride(out, "userLinesFlat", st.userLinesFlat.size(), 6);       // two XYZ endpoints
  CheckStride(out, "userCirclesCxCyZR", st.userCirclesCxCyZR.size(), 4);
  CheckStride(out, "userCircleNormals", st.userCircleNormals.size(), 3);   // REQ-312
  CheckStride(out, "userPolylineVerts", st.userPolylineVerts.size(), 3);
  CheckStride(out, "featureLineVerts", st.featureLineVerts.size(), 3);   // REQ-087, §11.8
  for (size_t i = 0; i < st.cadFilledRegions.size(); ++i) {
    const std::string label = "cadFilledRegions[" + std::to_string(i) + "].vertsXyz";
    CheckStride(out, label.c_str(), st.cadFilledRegions[i].vertsXyz.size(), 3);
  }

  // --- Finite coordinates ---------------------------------------------------------------------
  CheckFinite(out, "userLinesFlat", st.userLinesFlat);
  CheckFinite(out, "userCirclesCxCyZR", st.userCirclesCxCyZR);
  CheckFinite(out, "userCircleNormals", st.userCircleNormals);
  CheckFinite(out, "userPolylineVerts", st.userPolylineVerts);
  CheckFinite(out, "featureLineVerts", st.featureLineVerts);
  for (size_t i = 0; i < st.userArcs.size(); ++i) {
    const CadArc& a = st.userArcs[i];
    const std::string p = "userArcs[" + std::to_string(i) + "].";
    CheckFiniteScalar(out, (p + "cx").c_str(), a.cx, static_cast<int>(i));
    CheckFiniteScalar(out, (p + "cy").c_str(), a.cy, static_cast<int>(i));
    CheckFiniteScalar(out, (p + "r").c_str(), a.r, static_cast<int>(i));
    CheckFiniteScalar(out, (p + "startRad").c_str(), a.startRad, static_cast<int>(i));
    CheckFiniteScalar(out, (p + "sweepRad").c_str(), a.sweepRad, static_cast<int>(i));
    CheckFiniteScalar(out, (p + "z").c_str(), a.z, static_cast<int>(i));
    CheckFiniteScalar(out, (p + "nx").c_str(), a.nx, static_cast<int>(i));
    CheckFiniteScalar(out, (p + "ny").c_str(), a.ny, static_cast<int>(i));
    CheckFiniteScalar(out, (p + "nz").c_str(), a.nz, static_cast<int>(i));
  }
  for (size_t i = 0; i < st.userEllipses.size(); ++i) {
    const CadEllipse& e = st.userEllipses[i];
    const std::string p = "userEllipses[" + std::to_string(i) + "].";
    CheckFiniteScalar(out, (p + "cx").c_str(), e.cx, static_cast<int>(i));
    CheckFiniteScalar(out, (p + "cy").c_str(), e.cy, static_cast<int>(i));
    CheckFiniteScalar(out, (p + "majVx").c_str(), e.majVx, static_cast<int>(i));
    CheckFiniteScalar(out, (p + "majVy").c_str(), e.majVy, static_cast<int>(i));
    CheckFiniteScalar(out, (p + "ratio").c_str(), e.ratio, static_cast<int>(i));
    CheckFiniteScalar(out, (p + "z").c_str(), e.z, static_cast<int>(i));
  }
  for (size_t i = 0; i < st.cadAnnotations.size(); ++i) {
    const CadAnnotation& a = st.cadAnnotations[i];
    const std::string p = "cadAnnotations[" + std::to_string(i) + "].";
    CheckFiniteScalar(out, (p + "insX").c_str(), a.insX, static_cast<int>(i));
    CheckFiniteScalar(out, (p + "insY").c_str(), a.insY, static_cast<int>(i));
    CheckFiniteScalar(out, (p + "insZ").c_str(), a.insZ, static_cast<int>(i));
    CheckFiniteScalar(out, (p + "rotationRad").c_str(), a.rotationRad, static_cast<int>(i));
    CheckFiniteScalar(out, (p + "boxMinX").c_str(), a.boxMinX, static_cast<int>(i));
    CheckFiniteScalar(out, (p + "boxMinY").c_str(), a.boxMinY, static_cast<int>(i));
    CheckFiniteScalar(out, (p + "boxMaxX").c_str(), a.boxMaxX, static_cast<int>(i));
    CheckFiniteScalar(out, (p + "boxMaxY").c_str(), a.boxMaxY, static_cast<int>(i));
  }

  // --- Parallel attribute arrays ----------------------------------------------------------------
  CheckAttrCount(out, "lines", st.userLinesFlat.size() / 6, st.userLineAttrs.size());
  CheckAttrCount(out, "circles", st.userCirclesCxCyZR.size() / 4, st.userCircleAttrs.size());
  CheckAttrCount(out, "circle normals", st.userCirclesCxCyZR.size() / 4,
                 st.userCircleNormals.size() / 3);   // REQ-312 side-car (D-2026-08-31-f)
  CheckAttrCount(out, "arcs", st.userArcs.size(), st.userArcAttrs.size());
  CheckAttrCount(out, "ellipses", st.userEllipses.size(), st.userEllAttrs.size());
  CheckAttrCount(out, "annotations", st.cadAnnotations.size(), st.cadAnnotationAttrs.size());
  CheckAttrCount(out, "filled regions", st.cadFilledRegions.size(), st.cadFilledRegionAttrs.size());
  CheckAttrCount(out, "meshes", st.cadMeshes.size(), st.cadMeshAttrs.size());
  CheckAttrCount(out, "surfaces", st.cadSurfaces.size(), st.cadSurfaceAttrs.size());
  CheckAttrCount(out, "solids", st.cadSolids.size(), st.cadSolidAttrs.size());  // REQ-313
  CheckAttrCount(out, "tables", st.cadTables.size(), st.cadTableAttrs.size());
  CheckAttrCount(out, "block refs", st.cadBlockRefs.size(), st.cadBlockRefAttrs.size());
  CheckAttrCount(out, "polylines", PolylineCount(st), st.userPolylineAttrs.size());
  CheckAttrCount(out, "polyline closed-flags", PolylineCount(st), st.userPolylineClosed.size());

  // --- Polyline offsets ------------------------------------------------------------------------
  {
    const int vertexCount = static_cast<int>(st.userPolylineVerts.size() / 3);
    int prev = 0;
    for (size_t i = 0; i < st.userPolylineOffsets.size(); ++i) {
      const int off = st.userPolylineOffsets[i];
      if (off < 0 || off > vertexCount) {
        Add(out, docinv::kPolylineOffsets,
            "userPolylineOffsets[" + std::to_string(i) + "] = " + std::to_string(off) +
                " is outside [0, " + std::to_string(vertexCount) + "]",
            static_cast<int>(i));
      } else if (off < prev) {
        Add(out, docinv::kPolylineOffsets,
            "userPolylineOffsets[" + std::to_string(i) + "] = " + std::to_string(off) +
                " goes backwards from " + std::to_string(prev),
            static_cast<int>(i));
      }
      prev = off;
    }
    // CSR endpoints: the array starts at 0 and ends at the vertex count. Either being wrong makes
    // the last polyline read the wrong span, which is silent — the geometry is simply drawn short
    // or long — so it is checked rather than assumed.
    if (!st.userPolylineOffsets.empty()) {
      // A one-entry table is not a valid CSR array: it describes zero polylines while claiming a
      // table exists, and "zero polylines" is spelled EMPTY. This is the .gs reader's own rule
      // (GsIo: "expected empty or at least two entries"), lifted here so the corruption is caught at
      // the moment it is created rather than at the load that refuses the file. Issue #60 slipped
      // through the three checks around it — {0} with zero vertices starts at 0, never goes
      // backwards, and ends at the vertex count — which is why the reader's rule has to be its own.
      if (st.userPolylineOffsets.size() == 1) {
        Add(out, docinv::kPolylineOffsets,
            "userPolylineOffsets holds 1 entry; 0 polylines is spelled as an EMPTY table and the "
            ".gs reader rejects a single entry");
      }
      if (st.userPolylineOffsets.front() != 0) {
        Add(out, docinv::kPolylineOffsets,
            "userPolylineOffsets[0] = " + std::to_string(st.userPolylineOffsets.front()) +
                ", expected 0");
      }
      if (st.userPolylineOffsets.back() != vertexCount) {
        Add(out, docinv::kPolylineOffsets,
            "userPolylineOffsets.back() = " + std::to_string(st.userPolylineOffsets.back()) +
                " but userPolylineVerts holds " + std::to_string(vertexCount) + " vertices");
      }
    }
  }

  // --- Feature lines (REQ-087) -------------------------------------------------------------------
  // Same CSR rules as polylines, plus one that is unique to this store: the elevation-point flag
  // array is per VERTEX, and a short one is SILENT — the missing tail reads as "all PIs", so
  // elevation points disappear while the geometry still looks exactly right (ADR-035 (a)).
  {
    const size_t featureLineCount = st.featureLineOffsets.empty() ? 0 : st.featureLineOffsets.size() - 1;
    const int vertexCount = static_cast<int>(st.featureLineVerts.size() / 3);

    CheckAttrCount(out, "feature lines", featureLineCount, st.featureLineAttrs.size());
    CheckAttrCount(out, "feature-line closed-flags", featureLineCount, st.featureLineClosed.size());
    CheckAttrCount(out, "feature-line info rows", featureLineCount, st.featureLineInfo.size());

    if (st.featureLineElevPt.size() != static_cast<size_t>(vertexCount)) {
      Add(out, docinv::kFeatureLineOffsets,
          "featureLineElevPt holds " + std::to_string(st.featureLineElevPt.size()) +
              " flags but featureLineVerts holds " + std::to_string(vertexCount) +
              " vertices; the flag array is per vertex");
    }

    int prev = 0;
    for (size_t i = 0; i < st.featureLineOffsets.size(); ++i) {
      const int off = st.featureLineOffsets[i];
      if (off < 0 || off > vertexCount) {
        Add(out, docinv::kFeatureLineOffsets,
            "featureLineOffsets[" + std::to_string(i) + "] = " + std::to_string(off) +
                " is outside [0, " + std::to_string(vertexCount) + "]",
            static_cast<int>(i));
      } else if (off < prev) {
        Add(out, docinv::kFeatureLineOffsets,
            "featureLineOffsets[" + std::to_string(i) + "] = " + std::to_string(off) +
                " goes backwards from " + std::to_string(prev),
            static_cast<int>(i));
      }
      prev = off;
    }
    if (!st.featureLineOffsets.empty()) {
      if (st.featureLineOffsets.size() == 1) {
        Add(out, docinv::kFeatureLineOffsets,
            "featureLineOffsets holds 1 entry; 0 feature lines is spelled as an EMPTY table");
      }
      if (st.featureLineOffsets.front() != 0) {
        Add(out, docinv::kFeatureLineOffsets,
            "featureLineOffsets[0] = " + std::to_string(st.featureLineOffsets.front()) + ", expected 0");
      }
      if (st.featureLineOffsets.back() != vertexCount) {
        Add(out, docinv::kFeatureLineOffsets,
            "featureLineOffsets.back() = " + std::to_string(st.featureLineOffsets.back()) +
                " but featureLineVerts holds " + std::to_string(vertexCount) + " vertices");
      }
    }
  }

  // --- Filled-region loop starts ----------------------------------------------------------------
  for (size_t r = 0; r < st.cadFilledRegions.size(); ++r) {
    const CadFilledRegion& fr = st.cadFilledRegions[r];
    const int vertexCount = static_cast<int>(fr.vertsXyz.size() / 3);
    int prev = -1;
    for (size_t k = 0; k < fr.loopStart.size(); ++k) {
      const int s = fr.loopStart[k];
      if (s < 0 || s > vertexCount) {
        Add(out, docinv::kRegionLoops,
            "cadFilledRegions[" + std::to_string(r) + "].loopStart[" + std::to_string(k) + "] = " +
                std::to_string(s) + " is outside [0, " + std::to_string(vertexCount) + "]",
            static_cast<int>(r));
      } else if (s < prev) {
        Add(out, docinv::kRegionLoops,
            "cadFilledRegions[" + std::to_string(r) + "].loopStart[" + std::to_string(k) +
                "] goes backwards",
            static_cast<int>(r));
      }
      prev = s;
    }
    if (!fr.loopStart.empty() && fr.loopStart[0] != 0) {
      Add(out, docinv::kRegionLoops,
          "cadFilledRegions[" + std::to_string(r) + "].loopStart[0] = " +
              std::to_string(fr.loopStart[0]) + ", expected 0",
          static_cast<int>(r));
    }
  }

  // --- Entity identity (REQ-076) -----------------------------------------------------------------
  {
    std::unordered_map<std::uint64_t, std::string> seen;
    const auto sweep = [&](const char* store, const std::vector<EntityAttributes>& attrs) {
      for (size_t i = 0; i < attrs.size(); ++i)
        NoteId(out, &seen, attrs[i].id, store, i, st.nextEntityId);
    };
    sweep("userLineAttrs", st.userLineAttrs);
    sweep("userCircleAttrs", st.userCircleAttrs);
    sweep("userArcAttrs", st.userArcAttrs);
    sweep("userEllAttrs", st.userEllAttrs);
    sweep("userPolylineAttrs", st.userPolylineAttrs);
    sweep("cadAnnotationAttrs", st.cadAnnotationAttrs);
    sweep("cadFilledRegionAttrs", st.cadFilledRegionAttrs);
    sweep("cadMeshAttrs", st.cadMeshAttrs);
    sweep("cadSurfaceAttrs", st.cadSurfaceAttrs);
    sweep("cadTableAttrs", st.cadTableAttrs);
    sweep("cadBlockRefAttrs", st.cadBlockRefAttrs);  // GitHub issue #124
    sweep("featureLineAttrs", st.featureLineAttrs);  // REQ-087; a surface references these by id
  }

  // --- Selection (architecture §11.9) --------------------------------------------------------------
  {
    using T = SelectedEntity::Type;
    const auto capacity = [&](T t) -> size_t {
      switch (t) {
      case T::LineSeg:      return st.userLinesFlat.size() / 6;
      case T::Circle:       return st.userCirclesCxCyZR.size() / 4;
      case T::Annotation:   return st.cadAnnotations.size();
      case T::Polyline:     return PolylineCount(st);
      case T::Arc:          return st.userArcs.size();
      case T::Ellipse:      return st.userEllipses.size();
      case T::PdfUnderlay:  return st.pdfAttachments.size();
      case T::FilledRegion: return st.cadFilledRegions.size();
      case T::Mesh:         return st.cadMeshes.size();
      // REQ-087. Without this case the function returned 0 for a selected feature line, so the
      // check fired on every valid selection of one — the exact inverse of a missed case, and
      // caught by `-Wswitch` rather than by the MSVC build, which does not warn here.
      case T::FeatureLine:  return st.featureLineOffsets.empty() ? 0 : st.featureLineOffsets.size() - 1;
      case T::Surface:      return st.cadSurfaces.size();  // REQ-068 / ADR-036 (b)
      case T::Table:        return st.cadTables.size();
      case T::BlockRef:     return st.cadBlockRefs.size();  // GitHub issue #124
      case T::Solid:        return st.cadSolids.size();      // REQ-313 / ADR-045
      }
      return 0;
    };
    for (size_t i = 0; i < st.selection.size(); ++i) {
      const SelectedEntity& s = st.selection[i];
      const size_t cap = capacity(s.type);
      if (s.index < 0 || static_cast<size_t>(s.index) >= cap) {
        Add(out, docinv::kSelectionInRange,
            "selection[" + std::to_string(i) + "] type=" +
                std::to_string(static_cast<int>(s.type)) + " index=" + std::to_string(s.index) +
                " but only " + std::to_string(cap) + " of that type exist",
            s.index);
      }
    }
  }

  // --- Survey label links (REQ-076: a reference is an id, and a stale id resolves to nothing) -----
  {
    std::unordered_map<std::uint64_t, size_t> annById;
    for (size_t i = 0; i < st.cadAnnotationAttrs.size(); ++i) {
      if (st.cadAnnotationAttrs[i].id != 0)
        annById[st.cadAnnotationAttrs[i].id] = i;
    }
    for (size_t i = 0; i < st.surveyPoints.size(); ++i) {
      const std::uint64_t labelId = st.surveyPoints[i].labelMtextAnnId;
      if (labelId == 0)
        continue;  // no label — legitimate
      const auto it = annById.find(labelId);
      if (it == annById.end())
        continue;  // the label was erased; the id resolves to nothing, which is the REQ-076 promise

      // It resolves — so the annotation must agree that it belongs to this point.
      const CadAnnotation& a = st.cadAnnotations[it->second];
      if (a.surveyPointLabelForId != st.surveyPoints[i].id) {
        Add(out, docinv::kSurveyLabelLinks,
            "surveyPoints[" + std::to_string(i) + "] (id " +
                std::to_string(st.surveyPoints[i].id) + ") points at annotation id " +
                std::to_string(labelId) + ", which claims to label point id " +
                std::to_string(a.surveyPointLabelForId),
            static_cast<int>(i));
      }
    }
  }
}

std::string FormatInvariantViolations(const std::vector<InvariantViolation>& v) {
  std::string s;
  for (size_t i = 0; i < v.size(); ++i) {
    if (i)
      s += "; ";
    s += v[i].name;
    s += ": ";
    s += v[i].detail;
  }
  return s;
}
