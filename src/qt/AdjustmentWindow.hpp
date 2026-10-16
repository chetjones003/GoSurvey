#pragma once
#include <QMainWindow>
#include "CadCommands.hpp"
class QPlainTextEdit;
class QTableWidget;
class QLabel;

class AdjustmentWindow final : public QMainWindow {
public:
  AdjustmentWindow(AppCommandState& state, std::vector<std::string>& log, QWidget* parent);
  bool RunSmokeChecks(QString& error);
private:
  void Import();
  void Calculate();
  void SaveText();
  AppCommandState& m_State;
  std::vector<std::string>& m_Log;
  QPlainTextEdit* m_Editor;
  QPlainTextEdit* m_Output;
  QTableWidget* m_Coordinates;
  QTableWidget* m_Residuals;
};
