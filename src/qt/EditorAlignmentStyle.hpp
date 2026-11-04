#pragma once

#include <string>
#include <vector>

class QWidget;
struct AppCommandState;

/// The alignment style editor (REQ-491): tabs Information, Design, Markers, Display and Summary, as Civil 3D's style dialog
/// groups them. Every control runs the matching ALIGNMENTSTYLE command, so validation, refusals and the undo step are the
/// same as typed commands. \p initialName selects the style to show (empty: the first).
void ShowAlignmentStyleDialog(QWidget* parent, AppCommandState& state, std::vector<std::string>& log,
                              const std::string& initialName);
