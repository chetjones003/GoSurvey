#pragma once

#include <QString>

class QWidget;
struct AppCommandState;

/// REQ-517.1: the Create View Frames dialog. Asks for the alignment, the station range, the sheet size, the named
/// scale, the overlap and the placement, then returns the CREATEVIEWFRAMES command those answers make. The dialog
/// changes nothing itself; the command does the work, so the typed and the dialog forms share one code path.
/// Returns an empty string when the dialog is cancelled.
[[nodiscard]] QString RunCreateViewFramesDialog(QWidget* parent, const AppCommandState& state);
