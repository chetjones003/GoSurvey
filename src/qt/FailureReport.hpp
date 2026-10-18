#pragma once
// REQ-424 — the Qt side of crash and startup-failure reports. Building, logging and sending a
// report is shared platform code (CrashReporter, StartupFailure); this file only shows the result
// and runs the Qt build's graphics check at launch.
#include <QString>
#include <functional>
#include <string>

class QDialog;
class QUrl;
class QWidget;

namespace startupFailure { enum class Stage; }

/// Opens a URL in the user's browser; true when the request was handed to the OS.
using OpenUrlFunction = std::function<bool(const QUrl&)>;

/// REQ-424 §2–§3: the report window — the full report read-only, Send This Report (clipboard +
/// GitHub new issue + an outcome message; the window stays open) and Close (the cancel; nothing
/// more is sent). \p openUrl defaults to the browser and is replaceable so tests never open one.
QDialog* CreateFailureReportDialog(const QString& title, const QString& report,
                                   QWidget* parent = nullptr, OpenUrlFunction openUrl = {});

/// Shows the report window modally and returns once it is closed.
void ExecFailureReportDialog(const std::string& titleUtf8, const std::string& reportUtf8);

/// REQ-424 §4: the Qt build's launch-time graphics check — an OpenGL 3.3 core context and the
/// viewport renderer, the same things the ImGui launch fails on. False with the failing stage and
/// a technical detail line when GoSurvey cannot draw on this machine.
bool ProbeStartupGraphics(startupFailure::Stage& stage, std::string& detail);
