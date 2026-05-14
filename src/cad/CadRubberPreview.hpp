#pragma once

#include <vector>

struct AppCommandState;

/// One rubber segment as two XYZ triples (Z always 0 for draft lines).
void PushRubberSeg(std::vector<float>& o, float x0, float y0, float x1, float y1);

void AppendWorldRectRubber(std::vector<float>& o, float xa, float ya, float xb, float yb);

/// Draft-command rubber for the drawing viewport (line, polyline, arc, circle, dims, survey inverse, MTEXT box).
void AppendCadDraftRubberLines(const AppCommandState& cmd, float curX, float curY, bool orthoEnabled,
                               std::vector<float>& rubberLines);
