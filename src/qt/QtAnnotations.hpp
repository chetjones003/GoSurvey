#pragma once
#include <string>
class QPainter;
struct AppCommandState;
/// Laid-out height (pixels) of MTEXT \p wire at \p pixels text height wrapped at \p wrapWidthPx, using the
/// renderer's own layout (SHX strokes or a Qt text document). Installed as AppCommandState::measureMtextHeight.
float MeasureQtMtextHeight(float pixels, const std::string& wire, const std::string& family, float wrapWidthPx);
void DrawQtModelAnnotations(QPainter& painter, const AppCommandState& state, float width, float height);
/// Ghost annotation previews for MOVE/COPY/SCALE/ROTATE/MIRROR transform steps.
void DrawQtTransformAnnotationPreviews(QPainter& painter, const AppCommandState& state, float cursorLocalX,
                                       float cursorLocalY, float width, float height);
/// Paint the model's survey markers and optional IDs in the current camera and plot scale.
void DrawQtSurveyPoints(QPainter& painter, const AppCommandState& state, float width, float height);
