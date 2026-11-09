#pragma once

#include <QString>

class QWidget;
struct AppCommandState;

/// REQ-519.3: the Create Section Sheets dialog. Asks for the alignment with sample lines, the lines to draw (ALL by
/// default), the sections per sheet and the axis elevation, then returns the CREATESECTIONSHEETS command those answers
/// make. The dialog changes nothing itself; the command does the work, so the typed and the dialog forms share one code
/// path. Returns an empty string when the dialog is cancelled or cannot run.
[[nodiscard]] QString RunCreateSectionSheetsDialog(QWidget* parent, const AppCommandState& state);
