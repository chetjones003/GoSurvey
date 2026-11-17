#pragma once

#include "CadCommands.hpp"
#include "util/docinvariants.hpp"

// Capture by value: subsequent document edits cannot change a diagnostic
// result. Used by both the transcript driver and the deliberately broken
// drawing fixtures.
inline DocumentDiagnosticSnapshot CaptureDocumentDiagnosticSnapshot(const AppCommandState& st) {
  DocumentDiagnosticSnapshot result;
  result.userLinesFlat = st.userLinesFlat;
  result.userCirclesCxCyZR = st.userCirclesCxCyZR;
  result.userPolylineVerts = st.userPolylineVerts;
  result.featureLineVerts = st.featureLineVerts;
  result.userCircleNormals = st.userCircleNormals;
  result.userPolylineVertsBulge = st.userPolylineVertsBulge;
  result.userPolylineVertsNormal = st.userPolylineVertsNormal;
  result.userPolylineOffsets = st.userPolylineOffsets;
  result.featureLineOffsets = st.featureLineOffsets;
  result.nextEntityId = st.nextEntityId;
  result.cadPointsCount = st.cadPoints.size();
  result.cadMeshesCount = st.cadMeshes.size();
  result.cadPointCloudsCount = st.cadPointClouds.size();
  result.cadSurfacesCount = st.cadSurfaces.size();
  result.cadSolidsCount = st.cadSolids.size();
  result.cadTablesCount = st.cadTables.size();
  result.cadBlockRefsCount = st.cadBlockRefs.size();
  result.cadPositionMarkersCount = st.cadPositionMarkers.size();
  result.cadMultileadersCount = st.cadMultileaders.size();
  result.pdfAttachmentsCount = st.pdfAttachments.size();
  result.cadPipeRunsCount = st.cadPipeRuns.size();
  result.cadPointAttrsCount = st.cadPointAttrs.size();
  result.cadSolidAttrsCount = st.cadSolidAttrs.size();
  result.userPolylineClosedCount = st.userPolylineClosed.size();
  result.featureLineClosedCount = st.featureLineClosed.size();
  result.featureLineInfoCount = st.featureLineInfo.size();
  result.featureLineElevPtCount = st.featureLineElevPt.size();
  result.userLineAttrs.reserve(st.userLineAttrs.size());
  for (const auto& value : st.userLineAttrs) result.userLineAttrs.push_back({value.id});
  result.userCircleAttrs.reserve(st.userCircleAttrs.size());
  for (const auto& value : st.userCircleAttrs) result.userCircleAttrs.push_back({value.id});
  result.userArcAttrs.reserve(st.userArcAttrs.size());
  for (const auto& value : st.userArcAttrs) result.userArcAttrs.push_back({value.id});
  result.userEllAttrs.reserve(st.userEllAttrs.size());
  for (const auto& value : st.userEllAttrs) result.userEllAttrs.push_back({value.id});
  result.cadSplineAttrs.reserve(st.cadSplineAttrs.size());
  for (const auto& value : st.cadSplineAttrs) result.cadSplineAttrs.push_back({value.id});
  result.userPolylineAttrs.reserve(st.userPolylineAttrs.size());
  for (const auto& value : st.userPolylineAttrs) result.userPolylineAttrs.push_back({value.id});
  result.cadAnnotationAttrs.reserve(st.cadAnnotationAttrs.size());
  for (const auto& value : st.cadAnnotationAttrs) result.cadAnnotationAttrs.push_back({value.id});
  result.cadFilledRegionAttrs.reserve(st.cadFilledRegionAttrs.size());
  for (const auto& value : st.cadFilledRegionAttrs)
    result.cadFilledRegionAttrs.push_back({value.id});
  result.cadMeshAttrs.reserve(st.cadMeshAttrs.size());
  for (const auto& value : st.cadMeshAttrs) result.cadMeshAttrs.push_back({value.id});
  result.cadPointCloudAttrs.reserve(st.cadPointCloudAttrs.size());
  for (const auto& value : st.cadPointCloudAttrs) result.cadPointCloudAttrs.push_back({value.id});
  result.cadSurfaceAttrs.reserve(st.cadSurfaceAttrs.size());
  for (const auto& value : st.cadSurfaceAttrs) result.cadSurfaceAttrs.push_back({value.id});
  result.cadTableAttrs.reserve(st.cadTableAttrs.size());
  for (const auto& value : st.cadTableAttrs) result.cadTableAttrs.push_back({value.id});
  result.cadBlockRefAttrs.reserve(st.cadBlockRefAttrs.size());
  for (const auto& value : st.cadBlockRefAttrs) result.cadBlockRefAttrs.push_back({value.id});
  result.featureLineAttrs.reserve(st.featureLineAttrs.size());
  for (const auto& value : st.featureLineAttrs) result.featureLineAttrs.push_back({value.id});
  result.userArcs.reserve(st.userArcs.size());
  for (const auto& value : st.userArcs)
    result.userArcs.push_back({value.cx, value.cy, value.r, value.startRad, value.sweepRad, value.z,
                               value.nx, value.ny, value.nz});
  result.userEllipses.reserve(st.userEllipses.size());
  for (const auto& value : st.userEllipses)
    result.userEllipses.push_back(
        {value.cx, value.cy, value.majVx, value.majVy, value.ratio, value.z});
  result.cadAnnotations.reserve(st.cadAnnotations.size());
  for (const auto& value : st.cadAnnotations)
    result.cadAnnotations.push_back({value.insX, value.insY, value.insZ, value.rotationRad,
                                     value.boxMinX, value.boxMinY, value.boxMaxX, value.boxMaxY,
                                     value.surveyPointLabelForId});
  result.surveyPoints.reserve(st.surveyPoints.size());
  for (const auto& value : st.surveyPoints)
    result.surveyPoints.push_back({value.id, value.labelMtextAnnId});
  result.cadFilledRegions.reserve(st.cadFilledRegions.size());
  for (const auto& value : st.cadFilledRegions)
    result.cadFilledRegions.push_back({value.vertsXyz.size(), value.loopStart});
  result.splineValidationErrors.reserve(st.cadSplines.size());
  for (const auto& value : st.cadSplines)
    result.splineValidationErrors.push_back(spline::Validate(value));
  using T = SelectedEntity::Type;
  const auto capacity = [&](T t) -> size_t {
    switch (t) {
      case T::LineSeg:
        return st.userLinesFlat.size() / 6;
      case T::Circle:
        return st.userCirclesCxCyZR.size() / 4;
      case T::Annotation:
        return st.cadAnnotations.size();
      case T::Polyline:
        return (st.userPolylineOffsets.empty() ? 0 : st.userPolylineOffsets.size() - 1);
      case T::Arc:
        return st.userArcs.size();
      case T::Ellipse:
        return st.userEllipses.size();
      case T::Spline:
        return st.cadSplines.size();  // REQ-443
      case T::Point:
        return st.cadPoints.size();  // REQ-456
      case T::PositionMarker:
        return st.cadPositionMarkers.size();  // -Wswitch with Point
      case T::Multileader:
        return st.cadMultileaders.size();  // -Wswitch with Point
      case T::PdfUnderlay:
        return st.pdfAttachments.size();
      case T::FilledRegion:
        return st.cadFilledRegions.size();
      case T::Mesh:
        return st.cadMeshes.size();
      case T::PointCloud:
        return st.cadPointClouds.size();
      // REQ-087. Without this case the function returned 0 for a selected feature
      // line, so the check fired on every valid selection of one — the exact
      // inverse of a missed case, and caught by `-Wswitch` rather than by the
      // MSVC build, which does not warn here.
      case T::FeatureLine:
        return st.featureLineOffsets.empty() ? 0 : st.featureLineOffsets.size() - 1;
      case T::Surface:
        return st.cadSurfaces.size();  // REQ-068 / ADR-036 (b)
      case T::Table:
        return st.cadTables.size();
      case T::BlockRef:
        return st.cadBlockRefs.size();  // GitHub issue #124
      case T::Solid:
        return st.cadSolids.size();  // REQ-313 / ADR-045
      // Pre-existing gap found while adding PointCloud above: MSVC (the shipping
      // compiler) does not warn on this switch missing a case, only clang's
      // -Wswitch does, so PipeRun (issue #486) was silently returning 0 capacity
      // — meaning a selected pipe run always failed this invariant's range check.
      // Fixed alongside PointCloud rather than filed separately since it is the
      // same one-line shape as every case above it.
      case T::PipeRun:
        return st.cadPipeRuns.size();
      // REQ-343 amended / ADR-059 (i), issue #479. SectionPlane is a type TAG
      // only — the plane is singular and its "is it selected" state lives in the
      // dedicated `sectionPlaneSelected` bool, never as an entry in
      // `st.selection` (see the enumerator's doc comment in CadCommands.hpp). So
      // this case can never actually be reached by the loop below; 0 is correct
      // either way, but it is stated explicitly rather than left to silently fall
      // through -Wswitch.
      case T::SectionPlane:
        return 0;
    }
    return 0;
  };
  result.selection.reserve(st.selection.size());
  for (const auto& value : st.selection)
    result.selection.push_back({static_cast<int>(value.type), value.index, capacity(value.type)});
  return result;
}
