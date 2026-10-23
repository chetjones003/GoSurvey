#pragma once
// REQ-059: paints the ViewCube that viewcube::BuildScene lays out. All geometry and hit-testing live in
// viewport/ViewCube.hpp; this file only turns a Scene into pixels.
#include "ViewCube.hpp"

class QPainter;

void PaintViewCube(QPainter& painter, const viewcube::Scene& scene, const viewcube::Hit& hover);
