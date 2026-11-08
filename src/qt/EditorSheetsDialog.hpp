#pragma once

#include <QString>

class QWidget;
struct AppCommandState;

/// REQ-518.1: the Create Sheets dialog. Asks for the alignment with a view frame group, the paper layout to use as the
/// template and the frames to use (ALL by default), then returns the CREATESHEETS command those answers make. The dialog
/// changes nothing itself; the command does the work, so the typed and the dialog forms share one code path.
/// Returns an empty string when the dialog is cancelled or cannot run.
[[nodiscard]] QString RunCreateSheetsDialog(QWidget* parent, const AppCommandState& state);
