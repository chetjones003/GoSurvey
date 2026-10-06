#pragma once

#include "CadCommands.hpp"

#include <string>
#include <vector>

/// Draw the PDFATTACH configuration dialog and drive the phase transitions.
/// Returns true while the dialog or a subsequent viewport-pick phase is active.
bool DrawPdfAttachDialog(AppCommandState& cmd, std::vector<std::string>& log);

/// Starts the background rasterize of `cmd.pdfAttachFilePath` and moves the dialog to its Building
/// phase. Called by the dialog's Attach button, and by the project Copy / Link prompt (REQ-379) once
/// the file to attach is decided.
void StartPdfAttachBuild(AppCommandState& cmd, std::vector<std::string>& log);
