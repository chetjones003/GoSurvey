#pragma once

#include <string>
#include <vector>

class QWidget;
struct AppCommandState;

/// The Alignment Layout table (REQ-486.1): one row per tangent/curve with its stations, length, radius, delta, bearing,
/// direction and constraint state. Editing a cell runs the same ALIGNMENTEDIT command the command line does, so the solver,
/// the refusals and the undo step are identical; a refused edit leaves the table showing the real values and says why.
/// \p initialName selects the alignment to show (empty: the first).
void ShowAlignmentLayoutDialog(QWidget* parent, AppCommandState& state, std::vector<std::string>& log,
                               const std::string& initialName);
