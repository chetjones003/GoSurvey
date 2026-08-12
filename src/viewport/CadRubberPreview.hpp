#pragma once

#include <vector>

struct AppCommandState;

/// One rubber segment as two XYZ triples, in view-relative coords.
///
/// \param z0,z1 elevation of each end (REQ-058). Defaulted to 0 because most draft geometry — box
///        selections, fences, trim lines — is genuinely flat; the LINE/POLYLINE rubber band passes
///        the real elevations so the preview is drawn where the committed geometry will land
///        rather than on the datum.
void PushRubberSegViewRel(std::vector<float>& o, double x0, double y0, double x1, double y1, double anchorX,
                          double anchorY, float z0 = 0.f, float z1 = 0.f);

void AppendWorldRectRubberViewRel(std::vector<float>& o, float xa, float ya, float xb, float yb, double anchorX,
                                  double anchorY, float z = 0.f);

/// Draft-command rubber for the drawing viewport (view-relative coords for GPU precision at high zoom).
void AppendCadDraftRubberLines(const AppCommandState& cmd, double curX, double curY, bool orthoEnabled,
                               double viewAnchorX, double viewAnchorY, float orthoHalfH, int fbHeightPx,
                               std::vector<float>& rubberLines);
