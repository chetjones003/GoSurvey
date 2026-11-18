#include <GoSurvey/Renderer/AlignmentDisplayCachePublic.hpp>
#include <GoSurvey/Renderer/CadSolidDisplayPublic.hpp>
#include <GoSurvey/Renderer/CameraPublic.hpp>
#include <GoSurvey/Renderer/GizmoOverlayPublic.hpp>
#include <GoSurvey/Renderer/SectionClipPublic.hpp>
#include <GoSurvey/Renderer/ViewportProjectionPublic.hpp>

#include <cmath>
#include <cstdint>
#include <vector>

struct ViewportValue {
  double modelCenterX = 10.0;
  double modelCenterY = 20.0;
  float paperXIn = 1.0f;
  float paperYIn = 2.0f;
  float paperWIn = 10.0f;
  float paperHIn = 8.0f;
  float scaleModelPerPaperIn = 2.0f;
  float camAzimuthDeg = 0.0f;
  float camElevationDeg = 90.0f;
  float camRollDeg = 0.0f;
  bool camPerspective = false;
  float camFovDeg = 45.0f;
  bool cameraIsPlan() const { return !camPerspective && camAzimuthDeg == 0.0f && camElevationDeg == 90.0f && camRollDeg == 0.0f; }
  float safeScale() const { return scaleModelPerPaperIn > 1.0e-6f ? scaleModelPerPaperIn : 1.0e-6f; }
};

// The standalone consumer links only GoSurvey::Renderer; its CMake target defines its include and link closure.

int main() {
  Camera camera = Camera::Plan(12.0, -4.0, 25.0f);
  if (!camera.isPlanView() || camera.targetX != 12.0 || camera.targetY != -4.0) return 1;

  const ViewportValue viewport;
  float paperX = 0.0f, paperY = 0.0f;
  ModelToPaperInThroughCamera(viewport, 12.0, 22.0, 0.0, &paperX, &paperY);
  if (paperX != 7.0f || paperY != 7.0f) return 2;

  SectionClipPlane plane;
  plane.active = true;
  plane.nx = 0.0;
  plane.ny = 0.0;
  plane.nz = 1.0;
  plane.c = 3.0;
  float shaderPlane[4]{};
  SectionClipToShaderVec4(plane, 100.0, 200.0, shaderPlane);
  if (shaderPlane[0] != 0.0f || shaderPlane[1] != 0.0f || shaderPlane[2] != -1.0f || shaderPlane[3] != 3.0f)
    return 3;

  alignment::Alignment alignment;
  alignment.id = 9;
  if (!alignment.AppendTangent({0.0, 0.0}, 0.0, 10.0)) return 4;
  alignment::AlignmentDisplayCache cache;
  const auto& samples = cache.Samples(alignment, 0.01);
  if (samples.size() != 1 || samples.front().size() < 2 || cache.Rebuilds() != 1) return 5;
  cache.Retain(std::vector<std::uint64_t>{});
  CadSolidDisplayGeometry solids;
  CadGizmoOverlay gizmo;
  if (!solids.empty() || !gizmo.empty()) return 6;
  return 0;
}
