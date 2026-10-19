#pragma once
#include <QMainWindow>
#include "CadCommands.hpp"
#include "AdjustmentEditor.hpp"
#include <future>
#include <functional>
#include <QStringList>
class QPlainTextEdit;
class QTableWidget;
class QLabel;
class QTimer;
class QAction;
class QListWidget;

class AdjustmentWindow final : public QMainWindow {
public:
  AdjustmentWindow(AppCommandState& state, std::vector<std::string>& log, QWidget* parent);
  ~AdjustmentWindow() override;
  bool RunSmokeChecks(QString& error);
  bool LoadFile(const QString& path, QString& error);
  int ImportFiles(const QStringList& paths, QStringList& failures);
  bool SaveFile(const QString& path, QString& error);
  bool AcceptResult(QString& error);
  int CommitToDrawing();
  bool IsBusy() const { return m_Work.valid(); }
  bool HasUnsavedChanges() const;
  /// REQ-431: check the text now instead of after the typing pause.
  void ValidateNow();
  std::function<void()> drawingChanged;
protected:
  void closeEvent(QCloseEvent* event) override;
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
  void RefreshImportedFiles();
  void RefreshPreview();
  void RefreshFilesAndSummary();
  AppCommandState& m_State;
  std::vector<std::string>& m_Log;
  AdjustmentCodeEdit* m_Editor;
  AdjustmentPlanView* m_Plan;
  QLabel* m_Files;
  QLabel* m_Summary;
  QLabel* m_TextStatus;
  QTimer* m_ValidateTimer;
  TraverseData m_PreviewData;  ///< the text as last read without error, solved for the plan only
  QPlainTextEdit* m_Output;
  QTableWidget* m_Coordinates;
  QTableWidget* m_Residuals;
  QListWidget* m_ImportedFiles;
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
};
