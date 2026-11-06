#pragma once

#include <QString>
#include <QWidget>

#include <functional>
#include <string>
#include <utility>
#include <vector>

class QCloseEvent;
class QLabel;
class QToolButton;
struct AppCommandState;

/// The commands a Create Alignment dialog submits (REQ-485): `create` makes the alignment `name`; `layout` (layout form,
/// only when a starting point was given) then starts Tangent-Tangent from it. Otherwise the layout toolbar opens idle,
/// as Civil 3D's does. All empty when the dialog was cancelled.
struct CreateAlignmentCommands {
  QString name;
  QString create;
  QString layout;
};

/// REQ-485.1 / .2: Civil 3D's Create Alignment - Layout and Create Alignment from Objects dialogs. Name, Type,
/// Description and Starting station; a General tab with the alignment style, layer and label set (plus the optional
/// starting point and direction for a layout, or the conversion options for objects); a Design Criteria tab with the
/// starting design speed and the check set. Nothing is changed here: the dialog only builds the typed commands.
[[nodiscard]] CreateAlignmentCommands RunCreateAlignmentDialog(QWidget* parent, const AppCommandState& state,
                                                               bool fromObjects, std::function<void()> help = {});

/// REQ-485.1 Curve and Spiral Settings: clothoid spiral in and out lengths and the default radius. Returns the
/// ALIGNMENTCURVESETTINGS command, or an empty string when cancelled.
[[nodiscard]] QString RunCurveSpiralSettingsDialog(QWidget* parent, const AppCommandState& state,
                                                   std::function<void()> help = {});

/// REQ-485.1 Alignment Layout Tools, the small tool window Civil 3D shows while an alignment is laid out:
/// Tangent-Tangent (No curves / With curves) and Curve and Spiral Settings, Delete Sub-entity, Alignment Grid View,
/// Undo and Redo, and a status row with the current command and the spiral type. Every button submits a command.
class AlignmentLayoutTools final : public QWidget {
 public:
  AlignmentLayoutTools(AppCommandState& state, std::vector<std::string>& log, std::function<void(const QString&)> submit,
                       QWidget* parent);
  /// Follows `alignmentLayoutToolsName`: retitles, updates the status row, and hides when the alignment is gone.
  void Sync();
  /// Opens the user manual for the Curve and Spiral Settings dialog's Help button.
  void SetHelp(std::function<void()> help) { help_ = std::move(help); }

 protected:
  void closeEvent(QCloseEvent* event) override;

 private:
  void StartLayout(bool curves);
  void EditSettings();
  void DeleteSubEntity();
  void Run(const QString& command);
  [[nodiscard]] QString Name() const;

  AppCommandState& state_;
  std::vector<std::string>& log_;
  std::function<void(const QString&)> submit_;
  std::function<void()> help_;
  QToolButton* tangents_ = nullptr;
  QToolButton* redo_ = nullptr;
  QLabel* status_ = nullptr;
  bool used_ = false;  ///< A layout tool ran: the status row names it, as Civil 3D's does after the tool ends.
};
