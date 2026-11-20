#pragma once

#include <QString>

class QWidget;
struct AppCommandState;

/// REQ-519.1: the Sample Lines dialog. Asks for the alignment, the start and end stations, the interval, the left and
/// right swath widths and the surface (None by default), then returns the CREATESAMPLELINES command those answers make.
/// The dialog changes nothing itself; the command does the work, so the typed and the dialog forms share one code path.
/// Returns an empty string when the dialog is cancelled or cannot run.
[[nodiscard]] QString RunCreateSampleLinesDialog(QWidget* parent, const AppCommandState& state);
