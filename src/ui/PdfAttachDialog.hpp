#pragma once

#include "CadCommands.hpp"

#include <string>
#include <vector>

/// Draw the PDFATTACH configuration dialog and drive the phase transitions.
/// Returns true while the dialog or a subsequent viewport-pick phase is active.
bool DrawPdfAttachDialog(AppCommandState& cmd, std::vector<std::string>& log);
