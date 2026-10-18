#pragma once

#include <string>
#include <vector>

class QWidget;
struct AppCommandState;

/// Show the native editor for the drawing-owned point groups (REQ-067).
void ShowPointGroupManager(QWidget* parent, AppCommandState& state, std::vector<std::string>& log);
