#pragma once

#include <vector>

struct AppCommandState;

/// One rubber segment as two XYZ triples (Z always 0 for draft lines), in view-relative coords.
void PushRubberSegViewRel(std::vector<float>& o, double x0, double y0, double x1, double y1, double anchorX,
                          double anchorY);

void AppendWorldRectRubberViewRel(std::vector<float>& o, float xa, float ya, float xb, float yb, double anchorX,
                                  double anchorY);

/// Draft-command rubber for the drawing viewport (view-relative coords for GPU precision at high zoom).
void AppendCadDraftRubberLines(const AppCommandState& cmd, double curX, double curY, bool orthoEnabled,
                               double viewAnchorX, double viewAnchorY, float orthoHalfH, int fbHeightPx,
                               std::vector<float>& rubberLines);
