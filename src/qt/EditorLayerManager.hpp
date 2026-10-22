#pragma once

#include <functional>

class QWidget;
struct AppCommandState;

/// Show the Layer Properties Manager (REQ-451): a resizable window with a sortable, reorderable data grid of the
/// drawing's layers, colour swatches, linetype samples and, with a current viewport, the REQ-046 VP columns.
/// \p refresh repaints the editor after the drawing changes. Returns when the window is closed.
void ShowLayerManager(QWidget* parent, AppCommandState& state, const std::function<void()>& refresh);
