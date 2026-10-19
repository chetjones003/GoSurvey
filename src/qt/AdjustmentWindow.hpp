#pragma once
#include <QMainWindow>
#include "CadCommands.hpp"
#include <future>
#include <functional>
class QPlainTextEdit;
class QTableWidget;
class QLabel;
class QTimer;
class QAction;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QEvent;

class AdjustmentWindow final : public QMainWindow {
public:
  AdjustmentWindow(AppCommandState& state, std::vector<std::string>& log, QWidget* parent);
  ~AdjustmentWindow() override;
  bool RunSmokeChecks(QString& error);
  bool LoadFile(const QString& path, QString& error);
  bool SaveFile(const QString& path, QString& error);
  bool AcceptResult(QString& error);
  int CommitToDrawing();
  bool IsBusy() const { return m_Work.valid(); }
  bool HasUnsavedChanges() const;
  std::function<void()> drawingChanged;
protected:
  void closeEvent(QCloseEvent* event) override;
  void changeEvent(QEvent* event) override;
private:
  void Import();
  void Calculate();
  void SaveText();
  bool Save(bool saveAs = false);
  bool ConfirmDiscard();
  bool ParseEditor(QString& error);
  void PollCalculation();
  void PresentResult();
  void ExportReport();
  void EditWeights();
  void SetBusy(bool busy);
  void BuildOptionsBar(QWidget* parent);
  void InvalidateResult();
  void SyncOptionsFromText();
  void WriteScaleDirective();
  void ApplyOptionsToData();
  void RefreshOptionsBar();
  QString ApplyResolvedScale();
  QString UnitSuffix() const;
  AppCommandState& m_State;
  std::vector<std::string>& m_Log;
  QPlainTextEdit* m_Editor;
  QPlainTextEdit* m_Output;
  QTableWidget* m_Coordinates;
  QTableWidget* m_Residuals;
  TraverseData m_Data;
  LsaWeights m_Weights;
  QString m_Path;
  QString m_SourceText;
  QString m_SavedText;
  bool m_Imported = false;
  std::uint32_t m_ResultDrawingUid = 0;
  std::future<std::pair<TraverseData, LsaResult>> m_Work;
  QTimer* m_Poll;
  QAction* m_RunAction;
  QAction* m_AcceptAction;
  QAction* m_CommitAction;
  QWidget* m_OptionsBar = nullptr;
  QLabel* m_UnitsLabel = nullptr;
  QCheckBox* m_CurvRefr = nullptr;
  QDoubleSpinBox* m_RefractionK = nullptr;
  QLabel* m_RefractionWarning = nullptr;
  QCheckBox* m_ScaleCheck = nullptr;
  QComboBox* m_ScaleKind = nullptr;
  QLabel* m_ScaleReadout = nullptr;
  QLabel* m_ResidualNote = nullptr;
  bool m_SyncingOptions = false;
  QString m_ScaleNote;
};
