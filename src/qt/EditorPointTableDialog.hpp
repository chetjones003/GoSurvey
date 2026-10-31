#pragma once

#include <string>
#include <vector>

class QWidget;
struct AppCommandState;

/// ADDPOINTTABLE dialog (REQ-474): a point table style, survey points chosen by point group or check box, then OK
/// hands the chosen points to the command, which asks for the insertion point in the viewport.
void ShowAddPointTableDialog(QWidget* parent, AppCommandState& state, std::vector<std::string>& log);
