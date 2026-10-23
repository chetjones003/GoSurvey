#pragma once
class QPainter;
struct AppCommandState;
void DrawQtModelAnnotations(QPainter& painter, const AppCommandState& state, float width, float height);
/// Paint the model's survey markers and optional IDs in the current camera and plot scale.
void DrawQtSurveyPoints(QPainter& painter, const AppCommandState& state, float width, float height);
