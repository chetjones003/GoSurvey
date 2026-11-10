#include "EditorWindow.hpp"
#include "EditorViewport.hpp"

#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QMetaObject>
#include <QPointer>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <deque>
#include <memory>
#include <thread>
#include <utility>

namespace {
struct OptimizationWorker {
  std::atomic<bool> cancel{false};
  std::atomic<bool> done{false};
  std::atomic<int> publishedEvaluations{0};
  GradingOptimizationResult result;
};

struct OptimizationPlayback {
  std::vector<float> sourceLines;
  std::deque<GradingOptimizationEvaluation> pending;
  std::uint64_t surfaceId = 0;
  bool previewStarted = false;
  bool frameInFlight = false;
  bool previewInterrupted = false;
  int presentedEvaluations = 0;
  std::uint64_t presentedSequence = 0;
  GradingOptimizationEvaluation inFlightEvaluation;
};

struct OptimizationPreview {
  bool ready = false;
  std::uint64_t groupId = 0;
  std::uint64_t groundId = 0;
  std::uint32_t revision = 0;
  double offset = 0.0;
  SurfaceVolumeResult volumes;
};

class OptimizationSession final : public QObject {
public:
  explicit OptimizationSession(EditorViewport* viewport, QObject* parent)
      : QObject(parent), m_Viewport(viewport) {}
  ~OptimizationSession() override {
    CancelAndJoin();
    if (m_Viewport) {
      m_Viewport->gradingPreviewFrameDrawn = {};
      m_Viewport->ClearGradingOptimizationPreview();
    }
  }

  void CancelAndJoin() {
    ++runGeneration;
    if (worker) worker->cancel.store(true, std::memory_order_relaxed);
    if (workerThread.joinable()) workerThread.join();
  }

  OptimizationPreview preview;
  OptimizationPlayback playback;
  std::uint64_t viewportInvalidationSerial = 0;
  std::uint64_t runGeneration = 0;
  std::unique_ptr<OptimizationWorker> worker;
  std::thread workerThread;

private:
  QPointer<EditorViewport> m_Viewport;
};

std::vector<float> GradingPreviewLines(const CadSurface& design) {
  std::vector<float> lines;
  if (!design.tin || design.tin->indices.size() < 3 || design.tin->indices.size() % 3 != 0)
    return lines;
  lines.reserve(design.tin->indices.size() * 6);
  for (std::size_t i = 0; i + 2 < design.tin->indices.size(); i += 3) {
    const std::uint32_t tri[3] = {design.tin->indices[i], design.tin->indices[i + 1],
                                  design.tin->indices[i + 2]};
    for (int edge = 0; edge < 3; ++edge) {
      const std::size_t a = static_cast<std::size_t>(tri[edge]) * 3;
      const std::size_t b = static_cast<std::size_t>(tri[(edge + 1) % 3]) * 3;
      if (a + 2 >= design.tin->vertsXyz.size() || b + 2 >= design.tin->vertsXyz.size()) {
        lines.clear();
        return lines;
      }
      for (std::size_t axis = 0; axis < 3; ++axis) {
        lines.push_back(static_cast<float>(design.tin->vertsXyz[a + axis]));
      }
      for (std::size_t axis = 0; axis < 3; ++axis) {
        lines.push_back(static_cast<float>(design.tin->vertsXyz[b + axis]));
      }
    }
  }
  return lines;
}

void PresentNextOptimizationEvaluation(OptimizationPlayback* playback,
                                       EditorViewport* viewport) {
  if (!viewport || playback->frameInFlight || playback->pending.empty())
    return;
  if (!viewport->GradingOptimizationPreviewCanPaint())
    return;
  const GradingOptimizationEvaluation evaluation = playback->pending.front();
  playback->pending.pop_front();
  playback->presentedSequence = static_cast<std::uint64_t>(evaluation.sequence);
  playback->inFlightEvaluation = evaluation;
  if (!playback->previewStarted) {
    if (!viewport->BeginGradingOptimizationPreview(playback->surfaceId,
                                                    std::move(playback->sourceLines),
                                                    playback->presentedSequence, evaluation.elevationOffset)) {
      playback->previewInterrupted = true;
      return;
    }
    playback->previewStarted = true;
  } else {
    if (!viewport->UpdateGradingOptimizationPreview(playback->presentedSequence, evaluation.elevationOffset)) {
      playback->previewInterrupted = true;
      return;
    }
  }
  playback->frameInFlight = true;
}

void FillOptimizationSurfaces(EditorWindow& editor, QComboBox* combo, bool groupsOnly) {
  combo->clear();
  const AppCommandState& state = editor.State();
  for (size_t i = 0; i < state.cadSurfaces.size() && i < state.cadSurfaceAttrs.size(); ++i) {
    const CadSurface& surface = state.cadSurfaces[i];
    if (!surface.tin || surface.lastBuildIncomplete ||
        surface.builtAtRevision != state.cadGpuRevision ||
        surface.isVolumeSurface() ||
        (groupsOnly ? (!surface.gradingGroup || surface.gradingMembers.empty()) : surface.gradingGroup))
      continue;
    combo->addItem(QString::fromUtf8(surface.name.c_str()),
                   QVariant::fromValue<qulonglong>(state.cadSurfaceAttrs[i].id));
  }
}
} // namespace

void EditorWindow::ShowGradingOptimization() {
  if (auto* existing = findChild<QDialog*>("gradingOptimization")) {
    existing->setAttribute(Qt::WA_DeleteOnClose, false);
    delete existing;
  }

  auto* dialog = new QDialog(this);
  dialog->setObjectName("gradingOptimization");
  dialog->setWindowTitle("Grading Optimization");
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setModal(false);
  dialog->setProperty("documentGeneration", QVariant::fromValue<qulonglong>(m_DocumentGeneration));
  dialog->setMinimumWidth(470);

  auto* layout = new QVBoxLayout(dialog);
  layout->setAlignment(Qt::AlignTop);
  layout->setSpacing(10);
  auto* heading = new QLabel("Adjust grading elevations", dialog);
  heading->setObjectName("gradingOptimizationHeading");
  QFont headingFont = heading->font();
  headingFont.setBold(true);
  heading->setFont(headingFont);
  auto* guidance = new QLabel(
      "Keep the group's footprint fixed while shifting its elevations together. Preview the cut/fill result before applying it.",
      dialog);
  guidance->setObjectName("gradingOptimizationGuidance");
  guidance->setWordWrap(true);
  layout->addWidget(heading);
  layout->addWidget(guidance);

  auto* form = new QFormLayout;
  auto* group = new QComboBox(dialog);
  group->setObjectName("gradingOptimizationGroup");
  FillOptimizationSurfaces(*this, group, true);
  form->addRow("Grading group", group);
  auto* ground = new QComboBox(dialog);
  ground->setObjectName("gradingOptimizationGround");
  FillOptimizationSurfaces(*this, ground, false);
  form->addRow("Ground surface", ground);
  auto* mode = new QComboBox(dialog);
  mode->setObjectName("gradingOptimizationMode");
  mode->addItem("Balance cut and fill", 0);
  mode->addItem("Target net volume", 1);
  form->addRow("Goal", mode);
  auto* target = new QDoubleSpinBox(dialog);
  target->setObjectName("gradingOptimizationTarget");
  target->setRange(-1.0e12, 1.0e12);
  target->setDecimals(4);
  target->setSuffix(" yd³");
  target->setValue(0.0);
  form->addRow("Target net", target);
  layout->addLayout(form);

  auto* status = new QLabel(dialog);
  status->setObjectName("gradingOptimizationStatus");
  status->setWordWrap(true);
  if (group->count() == 0)
    status->setText("No current built grading groups are available. Create or rebuild a group first.");
  else if (ground->count() == 0)
    status->setText("No current built ground surfaces are available. Build a ground TIN first.");
  else
    status->setText("Ready to preview.");
  layout->addWidget(status);

  auto* buttons = new QHBoxLayout;
  auto* previewButton = new QPushButton("Preview", dialog);
  previewButton->setObjectName("gradingOptimizationPreview");
  auto* applyButton = new QPushButton("Apply", dialog);
  applyButton->setObjectName("gradingOptimizationApply");
  applyButton->setEnabled(false);
  auto* cancelButton = new QPushButton("Cancel", dialog);
  cancelButton->setObjectName("gradingOptimizationCancel");
  buttons->addStretch(1);
  buttons->addWidget(previewButton);
  buttons->addWidget(applyButton);
  buttons->addWidget(cancelButton);
  layout->addLayout(buttons);

  EditorViewport* viewport = m_Viewport;
  auto* session = new OptimizationSession(viewport, dialog);
  OptimizationPreview* preview = &session->preview;
  OptimizationPlayback* playback = &session->playback;
  auto* poll = new QTimer(dialog);
  poll->setInterval(25);
  auto* staleWatch = new QTimer(dialog);
  staleWatch->setInterval(100);
  const auto invalidate = [session, preview, playback, viewport, applyButton, staleWatch, status]() {
    session->CancelAndJoin();
    preview->ready = false;
    staleWatch->stop();
    playback->pending.clear();
    playback->previewStarted = false;
    playback->frameInFlight = false;
    playback->previewInterrupted = false;
    viewport->gradingPreviewFrameDrawn = {};
    viewport->ClearGradingOptimizationPreview();
    applyButton->setEnabled(false);
    status->setText("Settings changed. Preview again to see the result for these settings.");
  };
  QObject::connect(group, qOverload<int>(&QComboBox::currentIndexChanged), dialog,
                   [invalidate](int) { invalidate(); });
  QObject::connect(ground, qOverload<int>(&QComboBox::currentIndexChanged), dialog,
                   [invalidate](int) { invalidate(); });
  QObject::connect(mode, qOverload<int>(&QComboBox::currentIndexChanged), dialog,
                   [invalidate, target](int index) {
                     target->setEnabled(index == 1);
                     invalidate();
                   });
  QObject::connect(target, qOverload<double>(&QDoubleSpinBox::valueChanged), dialog,
                   [invalidate](double) { invalidate(); });

  QObject::connect(previewButton, &QPushButton::clicked, dialog, [this, dialog, group, ground, mode, target,
                                                                  status, applyButton, preview, session, poll, staleWatch,
                                                                  previewButton, cancelButton, playback, viewport]() {
    session->CancelAndJoin();
    preview->ready = false;
    playback->pending.clear();
    playback->previewStarted = false;
    playback->frameInFlight = false;
    playback->previewInterrupted = false;
    playback->presentedEvaluations = 0;
    playback->presentedSequence = 0;
    playback->surfaceId = 0;
    viewport->gradingPreviewFrameDrawn = {};
    viewport->ClearGradingOptimizationPreview();
    applyButton->setEnabled(false);
    staleWatch->stop();
    if (dialog->property("documentGeneration").toULongLong() != m_DocumentGeneration) {
      group->setEnabled(false);
      ground->setEnabled(false);
      status->setText("The active drawing changed. Close this tool and open it again for the current drawing.");
      return;
    }
    if (group->currentIndex() < 0 || ground->currentIndex() < 0) {
      status->setText("Choose a grading group and a built ground surface.");
      return;
    }
    if (!viewport->GradingOptimizationPreviewCanPaint()) {
      status->setText("Switch to model space to preview grading optimization.");
      return;
    }
    const auto groupId = group->currentData().toULongLong();
    const auto groundId = ground->currentData().toULongLong();
    const int gi = FindSurfaceIndexById(m_State, groupId);
    const int bi = FindSurfaceIndexById(m_State, groundId);
    if (gi < 0 || bi < 0 || m_State.cadSurfaces[static_cast<size_t>(gi)].builtAtRevision != m_State.cadGpuRevision ||
        m_State.cadSurfaces[static_cast<size_t>(bi)].builtAtRevision != m_State.cadGpuRevision) {
      status->setText("A selected surface is no longer current. Reopen this tool and preview again.");
      return;
    }
    const CadSurface& design = m_State.cadSurfaces[static_cast<size_t>(gi)];
    const CadSurface& base = m_State.cadSurfaces[static_cast<size_t>(bi)];
    if (!design.gradingGroup || base.gradingGroup || !design.tin || !base.tin ||
        design.lastBuildIncomplete || base.lastBuildIncomplete) {
      status->setText("Choose a current grading group and a current ground TIN.");
      return;
    }
    const double targetFt3 = mode->currentData().toInt() == 0 ? 0.0 : target->value() * 27.0;
    const auto baseVerts = base.tin->vertsXyz;
    const auto baseIndices = base.tin->indices;
    const auto designVerts = design.tin->vertsXyz;
    const auto designIndices = design.tin->indices;
    playback->sourceLines = GradingPreviewLines(design);
    if (playback->sourceLines.empty()) {
      status->setText("The grading group has no drawable triangles for a live preview.");
      return;
    }
    playback->surfaceId = groupId;
    const auto revision = m_State.cadGpuRevision;
    const auto generation = m_DocumentGeneration;
    session->viewportInvalidationSerial = viewport->GradingPreviewInvalidationSerial();
    session->worker = std::make_unique<OptimizationWorker>();
    OptimizationWorker* currentWorker = session->worker.get();
    const std::uint64_t runGeneration = ++session->runGeneration;
    viewport->gradingPreviewFrameDrawn = [session, playback, viewport, status](std::uint64_t sequence) {
      if (!session || !playback->frameInFlight || sequence != playback->presentedSequence)
        return;
      playback->frameInFlight = false;
      ++playback->presentedEvaluations;
      const GradingOptimizationEvaluation& evaluation = playback->inFlightEvaluation;
      status->setText(QString("Showing solver evaluation %1\nOffset: %2 ft   Net: %3 yd³")
                          .arg(evaluation.sequence)
                          .arg(evaluation.elevationOffset, 0, 'g', 9)
                          .arg(evaluation.volumes.netFt3 / 27.0, 0, 'f', 4));
      PresentNextOptimizationEvaluation(playback, viewport);
    };
    session->workerThread = std::thread([this, currentWorker, baseVerts, baseIndices, designVerts, designIndices, targetFt3,
                 session, playback, viewport, status, runGeneration, generation, revision]() {
      currentWorker->result = OptimizeGradingElevationOffset(baseVerts, baseIndices, designVerts,
                                                              designIndices, targetFt3,
                                                              &currentWorker->cancel,
          [this, currentWorker, session, playback, viewport, status, runGeneration, generation, revision](
              const GradingOptimizationEvaluation& evaluation) {
            QMetaObject::invokeMethod(session,
                [this, session, playback, viewport, status, evaluation, runGeneration, generation, revision]() {
                  if (session->runGeneration != runGeneration || !session->worker ||
                      session->worker->cancel.load(std::memory_order_relaxed))
                    return;
                  if (generation != m_DocumentGeneration || revision != m_State.cadGpuRevision) {
                    playback->previewInterrupted = true;
                    return;
                  }
                  playback->pending.push_back(evaluation);
                  PresentNextOptimizationEvaluation(playback, viewport);
                }, Qt::QueuedConnection);
            currentWorker->publishedEvaluations.store(evaluation.sequence, std::memory_order_release);
          });
      currentWorker->done.store(true, std::memory_order_release);
    });
    group->setEnabled(false);
    ground->setEnabled(false);
    mode->setEnabled(false);
    target->setEnabled(false);
    previewButton->setEnabled(false);
    cancelButton->setText("Cancel preview");
    status->setText("Calculating preview…");
    poll->stop();
    QObject::disconnect(poll, &QTimer::timeout, nullptr, nullptr);
    QObject::connect(poll, &QTimer::timeout, dialog,
                     [this, dialog, group, ground, mode, target, status, applyButton, preview, playback, session, viewport,
                      poll, staleWatch, previewButton, cancelButton, generation, revision,
                      groupId,
                      groundId]() {
      const bool workerCancelled = session->worker &&
          session->worker->cancel.load(std::memory_order_relaxed);
      const bool drawingChanged = generation != m_DocumentGeneration || revision != m_State.cadGpuRevision;
      if (!workerCancelled && (playback->previewInterrupted || !viewport->GradingOptimizationPreviewCanPaint() ||
          session->viewportInvalidationSerial != viewport->GradingPreviewInvalidationSerial() ||
          (playback->previewStarted && !viewport->GradingOptimizationPreviewActive()) || drawingChanged)) {
        session->CancelAndJoin();
        poll->stop();
        group->setEnabled(true);
        ground->setEnabled(true);
        mode->setEnabled(true);
        target->setEnabled(mode->currentData().toInt() == 1);
        previewButton->setEnabled(true);
        cancelButton->setText("Cancel");
        viewport->gradingPreviewFrameDrawn = {};
        playback->pending.clear();
        playback->frameInFlight = false;
        viewport->ClearGradingOptimizationPreview();
        status->setText(drawingChanged
            ? "The drawing changed during preview. The temporary preview was cleared; preview again to continue."
            : "Preview interrupted because the viewport changed. Preview again to continue.");
        preview->ready = false;
        applyButton->setEnabled(false);
        return;
      }
      if (!session->worker || !session->worker->done.load(std::memory_order_acquire)) return;
      const int published = session->worker->publishedEvaluations.load(std::memory_order_acquire);
      if (!session->worker->cancel.load(std::memory_order_relaxed) &&
          (playback->frameInFlight || !playback->pending.empty() || playback->presentedEvaluations < published))
        return;
      poll->stop();
      group->setEnabled(true);
      ground->setEnabled(true);
      mode->setEnabled(true);
      target->setEnabled(mode->currentData().toInt() == 1);
      previewButton->setEnabled(true);
      cancelButton->setText("Cancel");
      viewport->gradingPreviewFrameDrawn = {};
      if (session->worker->cancel.load(std::memory_order_relaxed)) {
        viewport->ClearGradingOptimizationPreview();
        status->setText("Preview cancelled. You can preview again or close this tool.");
        preview->ready = false;
        applyButton->setEnabled(false);
        return;
      }
      const GradingOptimizationResult& result = session->worker->result;
    if (!result.succeeded) {
      viewport->ClearGradingOptimizationPreview();
      status->setText(QString::fromUtf8(result.message.c_str()));
      preview->ready = false;
      applyButton->setEnabled(false);
      return;
    }
    preview->ready = true;
    preview->groupId = groupId;
    preview->groundId = groundId;
    preview->revision = revision;
    preview->offset = result.elevationOffset;
    preview->volumes = result.volumes;
    status->setText(QString("Offset: %1 ft\nCut: %2 yd³   Fill: %3 yd³   Net: %4 yd³\nTarget error: %5 yd³   Iterations: %6")
                        .arg(result.elevationOffset, 0, 'g', 10)
                        .arg(result.volumes.cutFt3 / 27.0, 0, 'f', 4)
                        .arg(result.volumes.fillFt3 / 27.0, 0, 'f', 4)
                        .arg(result.volumes.netFt3 / 27.0, 0, 'f', 4)
                        .arg(result.targetErrorFt3 / 27.0, 0, 'f', 4)
                        .arg(result.iterations));
    const bool readOnly = ActiveDrawingReadOnly();
    applyButton->setEnabled(!readOnly);
    staleWatch->start();
    if (readOnly)
      status->setText(status->text() + "\nThis project is read-only; Apply is unavailable.");
    });
    poll->start();
    });

  QObject::connect(staleWatch, &QTimer::timeout, dialog,
                   [this, dialog, status, preview, applyButton, viewport, session, staleWatch]() {
    if (!preview->ready) {
      staleWatch->stop();
      return;
    }
    const bool stale = dialog->property("documentGeneration").toULongLong() != m_DocumentGeneration ||
                       preview->revision != m_State.cadGpuRevision ||
                       session->viewportInvalidationSerial != viewport->GradingPreviewInvalidationSerial() ||
                       !viewport->GradingOptimizationPreviewActive() ||
                       !viewport->GradingOptimizationPreviewCanPaint();
    if (!stale) return;
    preview->ready = false;
    applyButton->setEnabled(false);
    viewport->gradingPreviewFrameDrawn = {};
    viewport->ClearGradingOptimizationPreview();
    status->setText("The drawing or viewport changed after preview. Preview again before applying.");
    staleWatch->stop();
  });
  QObject::connect(applyButton, &QPushButton::clicked, dialog,
                   [this, dialog, status, preview, applyButton, viewport, session, staleWatch]() {
    if (!preview->ready) return;
    if (dialog->property("documentGeneration").toULongLong() != m_DocumentGeneration ||
        preview->revision != m_State.cadGpuRevision ||
        session->viewportInvalidationSerial != viewport->GradingPreviewInvalidationSerial()) {
      viewport->gradingPreviewFrameDrawn = {};
      viewport->ClearGradingOptimizationPreview();
      preview->ready = false;
      applyButton->setEnabled(false);
      staleWatch->stop();
      status->setText("The drawing or viewport changed after preview. Preview again before applying.");
      return;
    }
    if (!ApplyGradingGroupElevationOffset(m_State, preview->groupId, preview->groundId,
                                          preview->revision, preview->offset, preview->volumes, m_Log)) {
      viewport->ClearGradingOptimizationPreview();
      preview->ready = false;
      applyButton->setEnabled(false);
      status->setText(QString::fromUtf8(m_Log.empty() ? "Optimization could not be applied." : m_Log.back().c_str()));
      return;
    }
    viewport->gradingPreviewFrameDrawn = {};
    viewport->ClearGradingOptimizationPreview();
    session->preview.ready = false;
    staleWatch->stop();
    Refresh();
    dialog->close();
  });
  QObject::connect(cancelButton, &QPushButton::clicked, dialog, [dialog, session, playback, viewport, poll, staleWatch]() {
    if (session->worker && poll->isActive()) {
      session->worker->cancel.store(true, std::memory_order_relaxed);
      playback->pending.clear();
      playback->frameInFlight = false;
      viewport->gradingPreviewFrameDrawn = {};
      viewport->ClearGradingOptimizationPreview();
      session->preview.ready = false;
      staleWatch->stop();
      return;
    }
    dialog->reject();
  });
  QObject::connect(dialog, &QDialog::rejected, dialog, [session, playback, viewport, staleWatch]() {
    session->CancelAndJoin();
    staleWatch->stop();
    session->preview.ready = false;
    playback->pending.clear();
    viewport->gradingPreviewFrameDrawn = {};
    viewport->ClearGradingOptimizationPreview();
  });
  mode->setCurrentIndex(0);
  target->setEnabled(false);
  dialog->show();
  dialog->raise();
  dialog->activateWindow();
}
