#pragma once

#include "CadCommands.hpp"
#include <GoSurvey/Debug/DocumentInvariants.hpp>

// Capture by value: subsequent document edits cannot change a diagnostic
// result. Used by both the transcript driver and the deliberately broken
// drawing fixtures.
inline DocumentDiagnosticSnapshot CaptureDocumentDiagnosticSnapshot(const AppCommandState& st) {
  DocumentDiagnosticSnapshot result;
  result.userLinesFlat = st.drawing.userLinesFlat;
  result.userCirclesCxCyZR = st.drawing.userCirclesCxCyZR;
  result.userPolylineVerts = st.drawing.userPolylineVerts;
  result.featureLineVerts = st.drawing.featureLineVerts;
  result.userCircleNormals = st.drawing.userCircleNormals;
  result.userPolylineVertsBulge = st.drawing.userPolylineVertsBulge;
  result.userPolylineVertsNormal = st.drawing.userPolylineVertsNormal;
  result.userPolylineOffsets = st.drawing.userPolylineOffsets;
  result.featureLineOffsets = st.drawing.featureLineOffsets;
  result.nextEntityId = st.nextEntityId;
  result.cadPointsCount = st.drawing.cadPoints.size();
  result.cadMeshesCount = st.drawing.cadMeshes.size();
  result.cadPointCloudsCount = st.drawing.cadPointClouds.size();
  result.cadSurfacesCount = st.drawing.cadSurfaces.size();
  result.cadSolidsCount = st.drawing.cadSolids.size();
  result.cadTablesCount = st.drawing.cadTables.size();
  result.cadBlockRefsCount = st.drawing.cadBlockRefs.size();
  result.cadPositionMarkersCount = st.drawing.cadPositionMarkers.size();
  result.cadMultileadersCount = st.drawing.cadMultileaders.size();
  result.pdfAttachmentsCount = st.drawing.pdfAttachments.size();
  result.cadPipeRunsCount = st.drawing.cadPipeRuns.size();
  result.cadPointAttrsCount = st.drawing.cadPointAttrs.size();
  result.cadSolidAttrsCount = st.drawing.cadSolidAttrs.size();
  result.userPolylineClosedCount = st.drawing.userPolylineClosed.size();
  result.featureLineClosedCount = st.drawing.featureLineClosed.size();
  result.featureLineInfoCount = st.drawing.featureLineInfo.size();
  result.featureLineElevPtCount = st.drawing.featureLineElevPt.size();
  result.userLineAttrs.reserve(st.drawing.userLineAttrs.size());
  for (const auto& value : st.drawing.userLineAttrs) result.userLineAttrs.push_back({value.id});
  result.userCircleAttrs.reserve(st.drawing.userCircleAttrs.size());
  for (const auto& value : st.drawing.userCircleAttrs) result.userCircleAttrs.push_back({value.id});
  result.userArcAttrs.reserve(st.drawing.userArcAttrs.size());
  for (const auto& value : st.drawing.userArcAttrs) result.userArcAttrs.push_back({value.id});
  result.userEllAttrs.reserve(st.drawing.userEllAttrs.size());
  for (const auto& value : st.drawing.userEllAttrs) result.userEllAttrs.push_back({value.id});
  result.cadSplineAttrs.reserve(st.drawing.cadSplineAttrs.size());
  for (const auto& value : st.drawing.cadSplineAttrs) result.cadSplineAttrs.push_back({value.id});
  result.userPolylineAttrs.reserve(st.drawing.userPolylineAttrs.size());
  for (const auto& value : st.drawing.userPolylineAttrs) result.userPolylineAttrs.push_back({value.id});
  result.cadAnnotationAttrs.reserve(st.drawing.cadAnnotationAttrs.size());
  for (const auto& value : st.drawing.cadAnnotationAttrs) result.cadAnnotationAttrs.push_back({value.id});
  result.cadFilledRegionAttrs.reserve(st.drawing.cadFilledRegionAttrs.size());
  for (const auto& value : st.drawing.cadFilledRegionAttrs)
    result.cadFilledRegionAttrs.push_back({value.id});
  result.cadMeshAttrs.reserve(st.drawing.cadMeshAttrs.size());
  for (const auto& value : st.drawing.cadMeshAttrs) result.cadMeshAttrs.push_back({value.id});
  result.cadPointCloudAttrs.reserve(st.drawing.cadPointCloudAttrs.size());
  for (const auto& value : st.drawing.cadPointCloudAttrs) result.cadPointCloudAttrs.push_back({value.id});
  result.cadSurfaceAttrs.reserve(st.drawing.cadSurfaceAttrs.size());
  for (const auto& value : st.drawing.cadSurfaceAttrs) result.cadSurfaceAttrs.push_back({value.id});
  result.cadTableAttrs.reserve(st.drawing.cadTableAttrs.size());
  for (const auto& value : st.drawing.cadTableAttrs) result.cadTableAttrs.push_back({value.id});
  result.cadBlockRefAttrs.reserve(st.drawing.cadBlockRefAttrs.size());
  for (const auto& value : st.drawing.cadBlockRefAttrs) result.cadBlockRefAttrs.push_back({value.id});
  result.featureLineAttrs.reserve(st.drawing.featureLineAttrs.size());
  for (const auto& value : st.drawing.featureLineAttrs) result.featureLineAttrs.push_back({value.id});
  result.userArcs.reserve(st.drawing.userArcs.size());
  for (const auto& value : st.drawing.userArcs)
    result.userArcs.push_back({value.cx, value.cy, value.r, value.startRad, value.sweepRad, value.z,
                               value.nx, value.ny, value.nz});
  result.userEllipses.reserve(st.drawing.userEllipses.size());
  for (const auto& value : st.drawing.userEllipses)
    result.userEllipses.push_back(
        {value.cx, value.cy, value.majVx, value.majVy, value.ratio, value.z});
  result.cadAnnotations.reserve(st.drawing.cadAnnotations.size());
  for (const auto& value : st.drawing.cadAnnotations)
    result.cadAnnotations.push_back({value.insX, value.insY, value.insZ, value.rotationRad,
                                     value.boxMinX, value.boxMinY, value.boxMaxX, value.boxMaxY,
                                     value.surveyPointLabelForId});
  result.surveyPoints.reserve(st.drawing.surveyPoints.size());
  for (const auto& value : st.drawing.surveyPoints)
    result.surveyPoints.push_back({value.id, value.labelMtextAnnId});
  result.cadFilledRegions.reserve(st.drawing.cadFilledRegions.size());
  for (const auto& value : st.drawing.cadFilledRegions)
    result.cadFilledRegions.push_back({value.vertsXyz.size(), value.loopStart});
  result.splineValidationErrors.reserve(st.drawing.cadSplines.size());
  for (const auto& value : st.drawing.cadSplines)
    result.splineValidationErrors.push_back(spline::Validate(value));
  using T = SelectedEntity::Type;
  const auto capacity = [&](T t) -> size_t {
    switch (t) {
      case T::LineSeg:
        return st.drawing.userLinesFlat.size() / 6;
      case T::Circle:
        return st.drawing.userCirclesCxCyZR.size() / 4;
      case T::Annotation:
        return st.drawing.cadAnnotations.size();
      case T::Polyline:
        return (st.drawing.userPolylineOffsets.empty() ? 0 : st.drawing.userPolylineOffsets.size() - 1);
      case T::Arc:
        return st.drawing.userArcs.size();
      case T::Ellipse:
        return st.drawing.userEllipses.size();
      case T::Spline:
        return st.drawing.cadSplines.size();  // REQ-443
      case T::Point:
        return st.drawing.cadPoints.size();  // REQ-456
      case T::PositionMarker:
        return st.drawing.cadPositionMarkers.size();  // -Wswitch with Point
      case T::Multileader:
        return st.drawing.cadMultileaders.size();  // -Wswitch with Point
      case T::PdfUnderlay:
        return st.drawing.pdfAttachments.size();
      case T::FilledRegion:
        return st.drawing.cadFilledRegions.size();
      case T::Mesh:
        return st.drawing.cadMeshes.size();
      case T::PointCloud:
        return st.drawing.cadPointClouds.size();
      // REQ-087. Without this case the function returned 0 for a selected feature
      // line, so the check fired on every valid selection of one — the exact
      // inverse of a missed case, and caught by `-Wswitch` rather than by the
      // MSVC build, which does not warn here.
      case T::FeatureLine:
        return st.drawing.featureLineOffsets.empty() ? 0 : st.drawing.featureLineOffsets.size() - 1;
      case T::Surface:
        return st.drawing.cadSurfaces.size();  // REQ-068 / ADR-036 (b)
      case T::Table:
        return st.drawing.cadTables.size();
      case T::BlockRef:
        return st.drawing.cadBlockRefs.size();  // GitHub issue #124
      case T::Solid:
        return st.drawing.cadSolids.size();  // REQ-313 / ADR-045
      // Pre-existing gap found while adding PointCloud above: MSVC (the shipping
      // compiler) does not warn on this switch missing a case, only clang's
      // -Wswitch does, so PipeRun (issue #486) was silently returning 0 capacity
      // — meaning a selected pipe run always failed this invariant's range check.
      // Fixed alongside PointCloud rather than filed separately since it is the
      // same one-line shape as every case above it.
      case T::PipeRun:
        return st.drawing.cadPipeRuns.size();
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
