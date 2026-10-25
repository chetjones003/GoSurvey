#include "PdfWindow.hpp"
#include "PdfCanvas.hpp"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDebug>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QImage>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QProgressBar>
#include <QRegion>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTimer>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <functional>
#include <future>
#include <vector>
// Shows the Compare tab of the side panel, wide enough for its list.
void PdfWindow::ShowComparePanel() {
  m_ThumbDock->show();
  const int index = m_SidePages->indexOf(m_ComparePanel);
  m_SidePages->setCurrentIndex(index);
  if (auto* group = findChild<QButtonGroup*>("pdfSideGroup")) if (auto* tab = group->button(index)) tab->setChecked(true);
  resizeDocks({m_ThumbDock}, {460}, Qt::Horizontal);
}

void PdfWindow::BeginCompare() {
  if (!m_Document) { QMessageBox::information(this, "Compare revisions", "Open the base PDF before comparing revisions."); return; }
  const QString path = QFileDialog::getOpenFileName(this, "Choose revision PDF", {}, "PDF (*.pdf)");
  if (path.isEmpty()) return;
  if (QFileInfo(path).absoluteFilePath() == QString::fromStdWString(m_Document->Path().wstring())) {
    QMessageBox::warning(this, "Compare revisions", "Choose a different PDF file for the revision."); return;
  }
  if (m_Compare && m_CompareWork.valid()) { QMessageBox::warning(this, "Compare revisions", "Wait for the current comparison to finish."); return; }
  m_Compare = std::make_unique<CompareSession>();
  m_Compare->source = path;
  m_Compare->basePage = CurrentPage();
  m_Compare->revisionPage = 0;
  {
    const QSignalBlocker baseBlock(m_CompareBasePage);
    const QSignalBlocker revisionBlock(m_CompareRevisionPage);
    m_CompareBasePage->setRange(1, m_Document->PageCount());
    m_CompareBasePage->setValue(m_Compare->basePage + 1);
    m_CompareRevisionPage->setRange(1, 1); m_CompareRevisionPage->setValue(1);
  }
  m_Compare->mode = static_cast<CompareMode>(m_CompareMode->currentData().toInt());
  ShowComparePanel();
  StartCompare(true); // align and list the changes straight away; Find changes re-runs it after a setting changes
}

void PdfWindow::StartCompare(bool findChanges) {
  if (!m_Document || !m_Compare || m_Compare->busy || m_CompareWork.valid()) return;
  m_Compare->cancel = false;
  m_Compare->busy = true;
  m_CompareProgress->show();
  if (!findChanges) m_Compare->haveChanges = false;
  m_CompareBasePage->setEnabled(false); m_CompareRevisionPage->setEnabled(false);
  m_CompareStatus->setText(findChanges ? "Aligning sheets and finding changes…" : "Rendering and aligning the two pages…");
  m_CompareTable->setRowCount(0);
  auto* base = m_Document.get();
  auto* existingRevision = m_Compare->revision.get();
  const auto path = std::filesystem::path(m_Compare->source.toStdWString());
  const auto cancel = &m_Compare->cancel;
  const int bp = m_Compare->basePage, rp = m_Compare->revisionPage;
  const auto requestedTransform = m_Compare->transform;
  const auto diffSettings = m_Compare->settings;
  const bool hadAlignment = m_Compare->aligned;
  m_CompareWork = std::async(std::launch::async, [base, existingRevision, path, cancel, bp, rp, requestedTransform, diffSettings, hadAlignment, findChanges]() noexcept { // NOLINT(bugprone-exception-escape): catches worker failures; clang-tidy attributes potential std::async/STL allocation exceptions to the call operator.
    CompareOutcome out;
    try {
    std::unique_ptr<pdfview::PdfDocument> opened;
    pdfview::PdfDocument* revision = existingRevision;
    if (!revision) {
      auto result = pdfview::PdfDocument::Open(path);
      if (!result.doc) { out.error = result.error; return out; }
      opened = std::move(result.doc); revision = opened.get();
    }
    if (bp < 0 || bp >= base->PageCount() || rp < 0 || rp >= revision->PageCount()) return out;
    const auto bs = base->Sizes()[bp]; const auto rs = revision->Sizes()[rp];
    if (!(bs.wPt > 0 && bs.hPt > 0 && rs.wPt > 0 && rs.hPt > 0) ||
        !std::isfinite(bs.wPt) || !std::isfinite(bs.hPt) || !std::isfinite(rs.wPt) || !std::isfinite(rs.hPt)) {
      out.error = "A selected page has invalid dimensions."; return out;
    }
    constexpr double maxPixels = 4.0 * 1024.0 * 1024.0;
    const double basePixelsPerPt = std::sqrt(maxPixels / (static_cast<double>(bs.wPt) * bs.hPt));
    const double revisionPixelsPerPt = std::sqrt(maxPixels / (static_cast<double>(rs.wPt) * rs.hPt));
    const double pxPerPt = std::min({2.0, 1500.0 / bs.wPt, 8192.0 / bs.hPt, 4096.0 / rs.wPt,
                                     8192.0 / rs.hPt, basePixelsPerPt, revisionPixelsPerPt});
    const int bw = std::max(64, qRound(bs.wPt * pxPerPt));
    const int bh = std::max(64, qRound(bs.hPt * pxPerPt));
    const int rw = std::max(64, qRound(rs.wPt * pxPerPt));
    const int rh = std::max(64, qRound(rs.hPt * pxPerPt));
    if (!base->RenderPage(bp, bw, bh, out.base, [cancel] { return cancel->load(); }) ||
        !revision->RenderPage(rp, rw, rh, out.revision, [cancel] { return cancel->load(); })) return out;
    out.alignment = pdfalign::AutoAlign(out.base, bs.hPt, out.revision, rs.hPt, pxPerPt, [cancel] { return cancel->load(); });
    out.hasAlignment = true;
    out.automaticTransform = out.alignment.xf;
    if (hadAlignment) out.alignment.xf = requestedTransform;
    pdfalign::ResampleAligned(out.revision, rs.hPt, static_cast<float>(pxPerPt), out.alignment.xf,
        bw, bh, static_cast<float>(pxPerPt), bs.hPt, out.aligned);
    std::vector<pdfview::ObjBox> baseBoxes, revisionBoxes;
    const bool hasBaseBoxes = base->TextBoxes(bp, baseBoxes, [cancel] { return cancel->load(); });
    const bool hasRevisionBoxes = revision->TextBoxes(rp, revisionBoxes, [cancel] { return cancel->load(); });
    const auto baseUnits = hasBaseBoxes ? pdfalign::BoxesToPixels(baseBoxes, {}, bs.hPt, pxPerPt, bw, bh) : std::vector<pdfalign::PixRect>{};
    const auto revisionUnits = hasRevisionBoxes ? pdfalign::BoxesToPixels(revisionBoxes, out.alignment.xf, bs.hPt, pxPerPt, bw, bh) : std::vector<pdfalign::PixRect>{};
    const uint8_t blue[3] = {255, 55, 30}, red[3] = {30, 55, 255};
    pdfview::Bitmap baseOnly, revisionOnly;
    pdfalign::MarkOnlyIn(out.base, out.aligned, blue, baseOnly, pxPerPt, baseUnits);
    pdfalign::MarkOnlyIn(out.aligned, out.base, red, revisionOnly, pxPerPt, revisionUnits);
    pdfalign::TintFromMarks(out.base, out.aligned, baseOnly, revisionOnly, out.tint);
    out.marks = baseOnly; // the two sets of marks together, transparent everywhere else
    for (size_t i = 0; i < out.marks.bgra.size() / 4; ++i)
      if (revisionOnly.bgra[i * 4 + 3]) std::copy_n(revisionOnly.bgra.data() + i * 4, 4, out.marks.bgra.data() + i * 4);
    out.revMarks = revisionOnly;
    out.baseView = out.base; out.revisionView = out.aligned;
    for (size_t i = 0; i < out.base.bgra.size() / 4; ++i) {
      if (baseOnly.bgra[i * 4 + 3]) std::copy_n(baseOnly.bgra.data() + i * 4, 3, out.baseView.bgra.data() + i * 4);
      if (revisionOnly.bgra[i * 4 + 3]) std::copy_n(revisionOnly.bgra.data() + i * 4, 3, out.revisionView.bgra.data() + i * 4);
    }
    if (findChanges && !cancel->load()) {
      auto found = pdfdiff::FindChanges(out.base, out.aligned, pxPerPt, bs.hPt, diffSettings, {}, {}, [cancel] { return cancel->load(); });
      if (!found.cancelled) out.changes = std::move(found);
      else return out;
    }
    if (cancel->load()) return out;
    out.ok = true;
    out.openedRevision = std::move(opened);
    return out;
    } catch (...) {
      return CompareOutcome{};
    }
  });
  m_ComparePoll->start();
}

void PdfWindow::PollCompare() {
  if (!m_CompareWork.valid() || m_CompareWork.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
  m_ComparePoll->stop();
  CompareOutcome out;
  try { out = m_CompareWork.get(); }
  catch (const std::exception& error) { qWarning() << "PDF comparison failed:" << error.what(); }
  if (!m_Compare) return;
  m_Compare->busy = false;
  m_CompareProgress->hide();
  m_CompareBasePage->setEnabled(true); m_CompareRevisionPage->setEnabled(true);
  if (m_Compare->cancel) {
    m_CompareStatus->setText("Comparison cancelled. No partial result was kept.");
    if (m_CompareClosePending) { StopCompareDetail(); m_Compare.reset(); m_CompareClosePending = false; m_Page->update(); }
    return;
  }
  if (!out.ok) {
    const QString reason = QString::fromUtf8(out.error.c_str());
    m_CompareStatus->setText(reason.isEmpty() ? "The pages could not be rendered or compared. Check that both PDFs are readable." : "Could not open the revision: " + reason);
    return;
  }
  if (out.openedRevision) {
    StopCompareDetail(); // a worker reads the revision through a raw pointer
    m_Compare->revision = std::move(out.openedRevision);
    const QSignalBlocker block(m_CompareRevisionPage);
    m_CompareRevisionPage->setRange(1, m_Compare->revision->PageCount());
    m_CompareRevisionPage->setValue(std::clamp(m_Compare->revisionPage + 1, 1, m_Compare->revision->PageCount()));
  }
  m_Compare->automatic = out.automaticTransform;
  if (!m_Compare->aligned) m_Compare->transform = out.alignment.xf;
  m_Compare->aligned = true;
  m_Compare->lowConfidence = !out.alignment.matched && !m_Compare->manualAlignment;
  m_Compare->overlay = QImage(out.tint.bgra.data(), out.tint.w, out.tint.h, static_cast<qsizetype>(out.tint.w) * 4, QImage::Format_ARGB32).copy();
  m_Compare->marksOverlay = QImage(out.marks.bgra.data(), out.marks.w, out.marks.h, static_cast<qsizetype>(out.marks.w) * 4, QImage::Format_ARGB32).copy();
  m_Compare->revMarks = QImage(out.revMarks.bgra.data(), out.revMarks.w, out.revMarks.h, static_cast<qsizetype>(out.revMarks.w) * 4, QImage::Format_ARGB32).copy();
  m_Compare->revisionOverlay = QImage(out.aligned.bgra.data(), out.aligned.w, out.aligned.h, static_cast<qsizetype>(out.aligned.w) * 4, QImage::Format_ARGB32).copy();
  m_Compare->baseView = QImage(out.baseView.bgra.data(), out.baseView.w, out.baseView.h, static_cast<qsizetype>(out.baseView.w) * 4, QImage::Format_ARGB32).copy();
  m_Compare->revisionView = QImage(out.revisionView.bgra.data(), out.revisionView.w, out.revisionView.h, static_cast<qsizetype>(out.revisionView.w) * 4, QImage::Format_ARGB32).copy();
  if (!out.changes.regions.empty() || m_CompareStatus->text().startsWith("Aligning")) {
    m_Compare->changes = std::move(out.changes.regions);
    m_Compare->haveChanges = true;
  }
  m_CompareStatus->setText(QString("Revision: %1, page %2. Alignment confidence %3%4")
      .arg(QFileInfo(m_Compare->source).fileName()).arg(m_Compare->revisionPage + 1)
      .arg(out.alignment.confidence * 100.0, 0, 'f', 1)
      .arg(m_Compare->lowConfidence ? ". Check alignment before trusting any change list." : ""));
  if (m_Compare->haveChanges) {
    m_CompareStatus->setText(m_CompareStatus->text() + (m_Compare->changes.empty() ? ". No changes found at this tolerance." :
        QString(". %1 change%2 found.").arg(m_Compare->changes.size()).arg(m_Compare->changes.size() == 1 ? "" : "s")));
    m_CompareTable->setRowCount(static_cast<int>(m_Compare->changes.size()));
    for (int row = 0; row < static_cast<int>(m_Compare->changes.size()); ++row) {
      const auto& region = m_Compare->changes[static_cast<std::size_t>(row)];
      const QString values[] = {QString::fromLatin1(pdfdiff::KindName(region.kind)), QString::number(region.x0, 'f', 1),
          QString::number(region.y0, 'f', 1), QString("%1 × %2 pt").arg(region.Width(), 0, 'f', 1).arg(region.Height(), 0, 'f', 1)};
      for (int column = 0; column < 4; ++column) m_CompareTable->setItem(row, column, new QTableWidgetItem(values[column]));
    }
  }
  ClearCompareDetail();
  RequestCompareDetail();
  m_Page->update();
}

void PdfWindow::UpdateCompareOverlay() {
  if (!m_Compare || !m_Compare->revision) return;
  m_Compare->mode = static_cast<CompareMode>(m_CompareMode->currentData().toInt());
  RequestCompareDetail();
  m_Page->update();
}

void PdfWindow::DrawCompareOverlay(QPainter& painter) {
  if (!m_Compare || m_Compare->overlay.isNull() || ActivePage() != m_Compare->basePage) return;
  if (m_Compare->mode == CompareMode::Base) return; // Base mode is the original page.
  painter.save();
  painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
  if (m_Compare->mode == CompareMode::Opacity) painter.setOpacity(std::clamp(m_Compare->opacity / 100.0, 0.0, 1.0));
  // The comparison images have a fixed resolution; they are stretched over the page at whatever zoom it is shown.
  const QRect pageRect(0, 0, PageWidthPx(), PageHeightPx());
  const bool detailHere = (m_Compare->mode == CompareMode::Tint ? !m_CompareDetailMarks.isNull() : !m_CompareDetailImage.isNull()) &&
                          m_CompareDetailPageWidthPx == PageWidthPx() && m_Layout == Layout::PageByPage;
  if (detailHere) { // the sharp part, with the soft picture only round it
    painter.setClipRegion(QRegion(pageRect).subtracted(QRegion(m_CompareDetailRect)));
  }
  if (m_Compare->mode == CompareMode::Opacity) painter.drawImage(pageRect, m_Compare->revisionOverlay);
  else if (m_Compare->mode == CompareMode::Base) painter.drawImage(pageRect, m_Compare->baseView);
  else if (m_Compare->mode == CompareMode::Revision) painter.drawImage(pageRect, m_Compare->revisionView);
  else painter.drawImage(pageRect, m_Compare->marksOverlay.isNull() ? m_Compare->overlay : m_Compare->marksOverlay); // Tint: the page stays crisp underneath
  if (detailHere) {
    painter.setClipping(false);
    if (m_Compare->mode == CompareMode::Tint) painter.drawImage(m_CompareDetailRect, m_CompareDetailMarks);
    else {
      painter.drawImage(m_CompareDetailRect, m_CompareDetailImage);
      if (m_Compare->mode == CompareMode::Revision && !m_CompareDetailRedMarks.isNull()) { painter.setOpacity(1.0); painter.drawImage(m_CompareDetailRect, m_CompareDetailRedMarks); }
    }
  }
  if (m_Compare->haveChanges && m_CompareShowHighlights->isChecked()) {
    const auto size = BasePageSize();
    const double factor = static_cast<double>(PageWidthPx()) / std::max(1.f, size.wPt); // the regions are in page points
    for (const auto& region : m_Compare->changes) {
      const QColor color = region.kind == pdfdiff::Kind::Added ? QColor(45, 205, 105, 90) :
        region.kind == pdfdiff::Kind::Removed ? QColor(235, 65, 65, 90) : QColor(240, 175, 45, 100);
      painter.setPen(QPen(color.darker(140), 1.5)); painter.setBrush(color);
      painter.drawRect(QRectF(region.x0 * factor, (size.hPt - region.y1) * factor,
          region.Width() * factor, region.Height() * factor));
    }
  }
  painter.restore();
}

namespace {
// The comparison's colours (BGR): blue is on the base only, red on the revision only.
constexpr uint8_t kBaseOnlyBgr[3] = {255, 55, 30};
constexpr uint8_t kRevisionOnlyBgr[3] = {30, 55, 255};
constexpr double kSoftEnoughShare = 0.9;     // the low-resolution picture is used as is once it is this close to the screen width
constexpr double kMaxDetailPixels = 12.0e6;  // the sharp picture of the visible part
constexpr double kMaxSourcePixels = 24.0e6;  // the revision render that picture is made from
constexpr int kPaperLuminance = 245, kInkSpan = 185; // paper is this bright; this much darker is full ink
constexpr int kRevisionMargin = 3;           // extra revision pixels rendered round the visible part, for the resampling

/// The visible part of the base page at screen resolution: a rectangle of device pixels, and how many make one point.
struct DetailView {
  int x0 = 0, y0 = 0, w = 0, h = 0;
  double pxPerPt = 1.0;
  float pageWPt = 0, pageHPt = 0;
  int FullW() const { return std::max(1, static_cast<int>(std::lround(pageWPt * pxPerPt))); }
  int FullH() const { return std::max(1, static_cast<int>(std::lround(pageHPt * pxPerPt))); }
};

int InkCoverage(const uint8_t* bgra) { // white 0, dark 255
  const int luminance = (bgra[0] + bgra[1] * 2 + bgra[2]) / 4;
  return std::clamp((kPaperLuminance - luminance) * 255 / kInkSpan, 0, 255);
}

void PutMark(std::vector<uint8_t>& bgra, std::size_t at, const uint8_t bgr[3], int alpha) {
  bgra[at] = bgr[0]; bgra[at + 1] = bgr[1]; bgra[at + 2] = bgr[2]; bgra[at + 3] = static_cast<uint8_t>(alpha);
}

/// The revision rendered at the screen's zoom and carried through the alignment onto the view's pixels (bilinear). False when
/// cancelled or when the render failed; a view that would need an unreasonably large render is left as blank paper.
bool ResampleRevision(pdfview::PdfDocument& revision, int page, const pdfview::PageSize& size, const pdfalign::Transform& xf,
                      const DetailView& view, const std::function<bool()>& cancel, pdfview::Bitmap& out) {
  const double scale = xf.Scale();
  if (scale < 1e-9) return false;
  const pdfalign::Transform inv = xf.Inverse();
  const double k = view.pxPerPt, kr = k * scale; // kr: revision pixels per revision point, about one to one with the screen
  const int fullW = std::max(1, static_cast<int>(std::lround(size.wPt * kr))), fullH = std::max(1, static_cast<int>(std::lround(size.hPt * kr)));
  double minX = 1e30, maxX = -1e30, minY = 1e30, maxY = -1e30; // the view's corners, carried into the revision
  for (const int cx : {0, view.w}) for (const int cy : {0, view.h}) {
    const auto corner = inv.Apply({(view.x0 + cx) / k, view.pageHPt - (view.y0 + cy) / k});
    minX = std::min(minX, corner.x * kr); maxX = std::max(maxX, corner.x * kr);
    minY = std::min(minY, (size.hPt - corner.y) * kr); maxY = std::max(maxY, (size.hPt - corner.y) * kr);
  }
  const int rx = std::clamp(static_cast<int>(std::floor(minX)) - kRevisionMargin, 0, fullW), ry = std::clamp(static_cast<int>(std::floor(minY)) - kRevisionMargin, 0, fullH);
  const int rw = std::clamp(static_cast<int>(std::ceil(maxX)) + kRevisionMargin, 0, fullW) - rx, rh = std::clamp(static_cast<int>(std::ceil(maxY)) + kRevisionMargin, 0, fullH) - ry;
  out.w = view.w; out.h = view.h;
  out.bgra.assign(static_cast<std::size_t>(view.w) * view.h * 4u, 255);
  if (rw <= 0 || rh <= 0 || static_cast<double>(rw) * rh > kMaxSourcePixels) return true;
  pdfview::Bitmap source;
  if (!revision.RenderRegion(page, {fullW, fullH}, {rx, ry, rw, rh}, source, cancel) || source.w <= 0) return false;
  for (int y = 0; y < view.h; ++y) {
    if (cancel()) return false;
    uint8_t* row = &out.bgra[static_cast<std::size_t>(y) * view.w * 4u];
    for (int x = 0; x < view.w; ++x) {
      const auto r = inv.Apply({(view.x0 + x + 0.5) / k, view.pageHPt - (view.y0 + y + 0.5) / k});
      const double fx = r.x * kr - 0.5 - rx, fy = (size.hPt - r.y) * kr - 0.5 - ry;
      const int x0 = static_cast<int>(std::floor(fx)), y0 = static_cast<int>(std::floor(fy));
      if (x0 < -1 || y0 < -1 || x0 >= source.w || y0 >= source.h) continue;
      const double tx = fx - x0, ty = fy - y0;
      for (int c = 0; c < 3; ++c) {
        const auto at = [&](int px, int py) -> double {
          if (px < 0 || py < 0 || px >= source.w || py >= source.h) return 255.0;
          return source.bgra[(static_cast<std::size_t>(py) * source.w + px) * 4u + static_cast<std::size_t>(c)];
        };
        const double v = (at(x0, y0) * (1 - tx) + at(x0 + 1, y0) * tx) * (1 - ty) + (at(x0, y0 + 1) * (1 - tx) + at(x0 + 1, y0 + 1) * tx) * ty;
        row[x * 4 + c] = static_cast<uint8_t>(std::clamp(v + 0.5, 0.0, 255.0));
      }
    }
  }
  return true;
}

/// The differing marks again, sharp: the low-resolution comparison says WHERE (it also dropped specks and grouped letters), and
/// the ink there is redrawn from the screen-resolution renders, coloured by how dark it is. \p marks holds both colours,
/// \p redMarks only the revision's. False when cancelled or the base render failed.
bool BuildDetailMarks(pdfview::PdfDocument& base, int page, const QImage& lowMarks, const pdfview::Bitmap& aligned, const DetailView& view,
                      const std::function<bool()>& cancel, pdfview::Bitmap& marks, pdfview::Bitmap& redMarks) {
  const int fullW = view.FullW(), fullH = view.FullH();
  pdfview::Bitmap baseSource;
  if (!base.RenderRegion(page, {fullW, fullH}, {view.x0, view.y0, view.w, view.h}, baseSource, cancel) || baseSource.w != view.w ||
      baseSource.h != view.h) return false;
  const int lw = lowMarks.width(), lh = lowMarks.height();
  std::vector<uint8_t> found(static_cast<std::size_t>(lw) * lh, 0), margin(found.size(), 0); // 1: blue, 2: red
  for (int y = 0; y < lh; ++y) {
    const auto* line = reinterpret_cast<const QRgb*>(lowMarks.constScanLine(y));
    for (int x = 0; x < lw; ++x) if (qAlpha(line[x])) found[static_cast<std::size_t>(y) * lw + x] = qBlue(line[x]) > qRed(line[x]) ? 1 : 2;
  }
  for (int y = 0; y < lh; ++y)
    for (int x = 0; x < lw; ++x) { // one low-resolution pixel of margin round every mark
      uint8_t bits = 0;
      for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
        const int nx = x + dx, ny = y + dy;
        if (nx >= 0 && ny >= 0 && nx < lw && ny < lh) bits |= found[static_cast<std::size_t>(ny) * lw + nx];
      }
      margin[static_cast<std::size_t>(y) * lw + x] = bits;
    }
  marks.w = redMarks.w = view.w; marks.h = redMarks.h = view.h;
  marks.bgra.assign(static_cast<std::size_t>(view.w) * view.h * 4u, 0);
  redMarks.bgra.assign(marks.bgra.size(), 0);
  for (int y = 0; y < view.h; ++y) {
    if (cancel()) return false;
    const int ly = std::min(lh - 1, static_cast<int>((view.y0 + y + 0.5) * lh / fullH));
    for (int x = 0; x < view.w; ++x) {
      const uint8_t bits = margin[static_cast<std::size_t>(ly) * lw + std::min(lw - 1, static_cast<int>((view.x0 + x + 0.5) * lw / fullW))];
      if (!bits) continue;
      const std::size_t at = (static_cast<std::size_t>(y) * view.w + x) * 4u;
      const int blue = (bits & 1) ? InkCoverage(&baseSource.bgra[at]) : 0, red = (bits & 2) ? InkCoverage(&aligned.bgra[at]) : 0;
      if (red > 0) PutMark(redMarks.bgra, at, kRevisionOnlyBgr, red);
      if (red > blue) PutMark(marks.bgra, at, kRevisionOnlyBgr, red);
      else if (blue > 0) PutMark(marks.bgra, at, kBaseOnlyBgr, blue);
    }
  }
  return true;
}
} // namespace

void PdfWindow::ClearCompareDetail() {
  m_CompareDetailImage = {};
  m_CompareDetailMarks = {};
  m_CompareDetailRedMarks = {};
}

bool PdfWindow::KeepCompareRevisionWhenMarksFail(CompareDetail& detail, bool cancelled) {
  if (cancelled) { detail = {}; return false; }
  detail.marks = {};
  detail.redMarks = {};
  detail.failed = true;
  return true;
}

// A sharp picture of the visible part of the page, in the base page's frame, for when the page is shown much larger than the
// comparison images were made: the revision (Revision and Opacity views) and the differing marks (all views) are redrawn at the
// zoom on screen.
void PdfWindow::RequestCompareDetail() {
  if (!m_Compare || !m_Compare->revision || m_Compare->busy || !m_Document || !PageReady()) return;
  if (m_Compare->mode == CompareMode::Base || m_Layout != Layout::PageByPage || m_Compare->basePage != CurrentPage()) { ClearCompareDetail(); return; }
  if (m_CompareDetailWork.valid()) { m_CompareDetailCancel = true; m_CompareDetailAgain = true; return; }
  const double ratio = m_Page->devicePixelRatioF();
  const auto baseSize = BasePageSize();
  if (m_Compare->overlay.width() >= PageWidthPx() * ratio * kSoftEnoughShare) { ClearCompareDetail(); return; }
  const QRect visible = m_Page->visibleRegion().boundingRect().intersected(QRect(0, 0, PageWidthPx(), PageHeightPx()));
  if (visible.isEmpty()) return;
  DetailView view;
  view.x0 = static_cast<int>(std::floor(visible.x() * ratio)); view.y0 = static_cast<int>(std::floor(visible.y() * ratio));
  view.w = static_cast<int>(std::ceil(visible.width() * ratio)); view.h = static_cast<int>(std::ceil(visible.height() * ratio));
  view.pxPerPt = PageWidthPx() * ratio / std::max(1.f, baseSize.wPt); // device pixels per base point
  view.pageWPt = baseSize.wPt; view.pageHPt = baseSize.hPt;
  if (static_cast<double>(view.w) * view.h > kMaxDetailPixels) return;
  const int rp = m_Compare->revisionPage;
  if (rp < 0 || rp >= m_Compare->revision->PageCount()) return;
  // The worker owns copies of everything it reads except the two documents; StopCompareDetail waits for it before either goes.
  auto* revision = m_Compare->revision.get();
  auto* baseDocument = m_Document.get();
  const auto revisionSize = revision->Sizes()[static_cast<std::size_t>(rp)];
  const pdfalign::Transform xf = m_Compare->transform;
  const int basePageIndex = m_Compare->basePage, pageWidthPx = PageWidthPx();
  const QImage lowMarks = m_Compare->marksOverlay; // where the comparison found differences, at low resolution
  m_CompareDetailCancel = false;
  m_CompareDetailAgain = false;
  m_CompareDetailWork = std::async(std::launch::async, [this, revision, baseDocument, rp, revisionSize, xf, view, visible, pageWidthPx, ratio, basePageIndex, lowMarks]() noexcept {
    CompareDetail out;
    const auto cancelled = [this] { return m_CompareDetailCancel.load(); };
    try {
      if (!ResampleRevision(*revision, rp, revisionSize, xf, view, cancelled, out.bitmap)) { const bool failed = !cancelled(); out = {}; out.failed = failed; return out; }
      if (!lowMarks.isNull() && !BuildDetailMarks(*baseDocument, basePageIndex, lowMarks, out.bitmap, view, cancelled, out.marks, out.redMarks)) {
        if (!KeepCompareRevisionWhenMarksFail(out, cancelled())) return CompareDetail{};
      }
      out.rect = visible; out.pageWidthPx = pageWidthPx; out.ratio = ratio;
    } catch (...) { out = {}; out.failed = true; }
    return out;
  });
  m_CompareDetailPoll->start();
}

void PdfWindow::PollCompareDetail() {
  if (!m_CompareDetailWork.valid() || m_CompareDetailWork.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
  m_CompareDetailPoll->stop();
  CompareDetail out;
  try { out = m_CompareDetailWork.get(); } catch (const std::exception& error) { qWarning() << "PDF compare: the sharp picture failed:" << error.what(); }
  if (out.failed) qWarning() << "PDF compare: the sharp picture of the visible part could not be drawn; the comparison picture stays.";
  if (m_CompareDetailAgain) { RequestCompareDetail(); return; } // the view moved while it drew
  if (!m_Compare || out.bitmap.w <= 0) return;
  const auto toImage = [&](const pdfview::Bitmap& bitmap) {
    if (bitmap.w <= 0) return QImage();
    QImage made = QImage(bitmap.bgra.data(), bitmap.w, bitmap.h, static_cast<qsizetype>(bitmap.w) * 4, QImage::Format_ARGB32).copy();
    made.setDevicePixelRatio(out.ratio);
    return made;
  };
  m_CompareDetailMarks = toImage(out.marks);
  m_CompareDetailRedMarks = toImage(out.redMarks);
  m_CompareDetailImage = toImage(out.bitmap);
  m_CompareDetailRect = out.rect;
  m_CompareDetailPageWidthPx = out.pageWidthPx;
  m_Page->update();
}

void PdfWindow::StopCompareDetail() {
  m_CompareDetailCancel = true;
  if (m_CompareDetailWork.valid()) {
    try {
      m_CompareDetailWork.wait();
    } catch (const std::exception& error) {
      qWarning() << "PDF compare: waiting for detail rendering during shutdown failed:" << error.what();
    } catch (...) {
      qWarning() << "PDF compare: waiting for detail rendering during shutdown failed with an unknown error.";
    }
    m_CompareDetailWork = {};
  }
  if (m_CompareDetailPoll) m_CompareDetailPoll->stop();
  ClearCompareDetail();
  m_CompareDetailAgain = false;
}

void PdfWindow::ComparePick(QPointF point, Qt::MouseButton button) {
  if (!m_Compare || !m_Compare->revision || m_Compare->busy || button != Qt::LeftButton || !PageReady()) return;
  if (m_Compare->mode != CompareMode::Base && m_Compare->mode != CompareMode::Revision) {
    m_CompareStatus->setText("Choose Base or Revision in the view selector before picking matching points."); return;
  }
  if (ActivePage() != m_Compare->basePage) { m_CompareStatus->setText("Pick the matching points on the base page."); return; }
  const auto size = BasePageSize();
  const double factor = static_cast<double>(PageWidthPx()) / std::max(1.0, static_cast<double>(size.wPt));
  pdfalign::Pt picked{point.x() / factor, size.hPt - point.y() / factor};
  const bool isBase = static_cast<CompareMode>(m_CompareMode->currentData().toInt()) == CompareMode::Base;
  const bool expectedBase = (m_ComparePickCount % 2) == 0;
  if (isBase != expectedBase) {
    m_CompareStatus->setText(expectedBase ? "Switch to Base and pick the next base point." : "Switch to Revision and pick its matching point."); return;
  }
  if (!isBase) picked = m_Compare->transform.Inverse().Apply(picked);
  m_ComparePicks[m_ComparePickCount++] = picked;
  if (m_ComparePickCount < static_cast<int>(m_ComparePickTarget)) {
    m_CompareStatus->setText(isBase ? "Now switch to Revision and pick its matching point." : "Now switch to Base for the second matching pair."); return;
  }
  std::string why;
  pdfalign::Transform transform;
  bool ok = false;
  if (m_ComparePickTarget == ComparePickCount::OnePair) { transform = pdfalign::FromOnePoint(m_ComparePicks[1], m_ComparePicks[0]); ok = true; }
  else ok = pdfalign::FromTwoPoints(m_ComparePicks[1], m_ComparePicks[3], m_ComparePicks[0], m_ComparePicks[2], transform, why);
  m_ComparePicking = false;
  if (!ok) { m_CompareStatus->setText(QString("Could not align the picked points: %1").arg(QString::fromUtf8(why.c_str()))); return; }
  m_Compare->transform = transform; m_Compare->aligned = true; m_Compare->lowConfidence = false;
  m_Compare->manualAlignment = true;
  StartCompare(false);
}

void PdfWindow::SaveCompareChanges() {
  if (!m_Compare || !m_Compare->revision || !m_Compare->haveChanges || m_Compare->changes.empty()) {
    m_CompareStatus->setText("Find changes before saving markups."); return;
  }
  if (m_Compare->lowConfidence) {
    if (QMessageBox::warning(m_ComparePanel, "Low-confidence alignment", "This alignment may be wrong. Save these findings anyway?",
          QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) return;
  }
  const QString path = QFileDialog::getSaveFileName(m_ComparePanel, "Save findings as PDF", {}, "PDF (*.pdf)");
  if (path.isEmpty()) return;
  const auto dest = path.endsWith(".pdf", Qt::CaseInsensitive) ? path : path + ".pdf";
  const auto marks = pdfdiff::RegionsToMarkups(m_Compare->changes, m_Compare->revisionPage, m_Compare->transform.Inverse());
  const auto error = pdfview::SaveAnnotated(m_Compare->revision->Path(), marks, std::filesystem::path(dest.toStdWString()));
  if (!error.empty()) { QMessageBox::warning(m_ComparePanel, "Save findings", QString::fromUtf8(error.c_str())); return; }
  m_CompareStatus->setText("Change markups saved to a new revision copy. The source PDF was not changed.");
}
