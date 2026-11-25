// The in-app user manual window (F1, Help → User Manual): page lookup, navigation, search, and the
// styling that turns the bundled wiki Markdown into readable pages.
#include "EditorWindow.hpp"
#include "QtAccent.hpp"
#include "WikiHelp.hpp"
#include "engine/CommandRegistry.hpp"

#include <QDesktopServices>
#include <QDialog>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStyle>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextDocumentFragment>
#include <QTextFrame>
#include <QTextTable>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <vector>

namespace {

/// Registry primary (lowercase) for the first token of a command line or ribbon route, so aliases
/// such as "L" find the LINE page. Unlisted commands (TOOLSPACE, BPARAM, UNDO...) count too.
/// Empty for "@" UI routes and unknown words.
std::string HelpCommandPrimary(const QString& text) {
  const QString token = text.trimmed().section(QRegularExpression("[\\s,]+"), 0, 0);
  if (token.isEmpty() || token.startsWith('@')) return {};
  const auto* def = stepengine::CommandRegistry::Instance().FindAny(token.toLower().toUtf8().toStdString());
  return def ? QString::fromUtf8(def->primary).toLower().toStdString() : std::string();
}

/// Registry primary for a running command. KindName is tried first; a kind whose name is not a
/// command (SOLID, ZOOM...) resolves only when exactly one command runs as that kind.
std::string HelpCommandPrimaryForKind(AppCommandState::Kind kind) {
  if (kind == AppCommandState::Kind::None) return {};
  if (std::string primary = HelpCommandPrimary(QString::fromUtf8(AppCommandState::KindName(kind))); !primary.empty())
    return primary;
  const stepengine::CommandDef* only = nullptr;
  for (const auto& def : stepengine::CommandRegistry::Instance().All()) {
    if (def.kind != kind) continue;
    if (only) return {};
    only = &def;
  }
  return only ? QString::fromUtf8(only->primary).toLower().toStdString() : std::string();
}

/// Colours for the manual, matched to the editor theme.
struct ManualTheme {
  QColor page, sidebar, text, muted, accent, rule, codeBg, codeText, tableHead, tableStripe, card, callout, calloutEdge;
};

ManualTheme ManualThemeFor(bool dark) {
  const QtAccent& a = QtAccentFor(dark);
  if (dark)
    return {QColor("#1b1f22"), QColor("#22272b"), QColor("#e2e6e9"), QColor("#93a0a8"), QColor(a.link),
            QColor("#353d43"), QColor("#262c31"), QColor("#f1c08c"), QColor("#2b3237"), QColor("#20252a"),
            QColor("#232a2f"), QColor("#33291c"), QColor(a.strong)};
  return {QColor("#ffffff"), QColor("#f3f4f6"), QColor("#1f2328"), QColor("#5d6770"), QColor(a.link),
          QColor("#d8dde2"), QColor("#f2f4f6"), QColor("#7a3f05"), QColor("#eef1f4"), QColor("#f8f9fa"),
          QColor("#f5f7f9"), QColor("#fdf3e6"), QColor(a.strong)};
}

constexpr int kReadableWidth = 860;  // px of text column; wider lines are hard to follow
constexpr int kMaxImageWidth = 480;

bool IsCodeBlock(const QTextBlock& block) {
  const QTextBlockFormat f = block.blockFormat();
  return f.hasProperty(QTextFormat::BlockCodeFence) || f.nonBreakableLines();
}

bool IsQuoteBlock(const QTextBlock& block) {
  return block.blockFormat().intProperty(QTextFormat::BlockQuoteLevel) > 0;
}

/// Moves each run of consecutive blocks that \p match accepts into a one-cell table, so the run
/// gets real padding and a background (block backgrounds alone have no inner padding). Runs are
/// collected first and rebuilt back to front, so earlier positions stay valid.
template <typename Match>
void BoxBlockRuns(QTextDocument* doc, Match match, const QTextTableFormat& box, const QTextBlockFormat& inner) {
  std::vector<std::pair<int, int>> runs;  // [start, end) document positions
  for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
    if (!match(b) || QTextCursor(b).currentTable()) continue;
    const int start = b.position();
    QTextBlock last = b;
    while (last.next().isValid() && match(last.next())) last = last.next();
    runs.emplace_back(start, last.position() + last.length() - 1);
    b = last;
  }
  for (auto it = runs.rbegin(); it != runs.rend(); ++it) {
    QTextCursor c(doc);
    c.setPosition(it->first);
    c.setPosition(it->second, QTextCursor::KeepAnchor);
    const QTextDocumentFragment content = c.selection();
    c.removeSelectedText();
    c.insertTable(1, 1, box);
    c.insertFragment(content);
    // Inside the box the quote indent and outer spacing are the box's job.
    QTextTable* table = c.currentTable();
    // Frame backgrounds are not painted for tables; the fill goes on the cell.
    QTextTableCell cell = table->cellAt(0, 0);
    QTextTableCellFormat fill = cell.format().toTableCellFormat();
    fill.setBackground(box.background());
    cell.setFormat(fill);
    QTextCursor all = table->cellAt(0, 0).firstCursorPosition();
    all.setPosition(table->cellAt(0, 0).lastCursorPosition().position(), QTextCursor::KeepAnchor);
    all.mergeBlockFormat(inner);
  }
}

/// Restyles a page rendered by QTextDocument::setMarkdown: type scale and spacing, tables, code,
/// callouts, inline code, links and image size. The Markdown importer gives every element plain
/// default formatting, which is what made pages hard to scan.
void StyleManualDocument(QTextDocument* doc, const ManualTheme& t) {
  // GitHub tables escape a pipe inside code as "\|"; Qt keeps the backslash.
  for (QTextCursor c = doc->find("\\|"); !c.isNull(); c = doc->find("\\|", c)) c.insertText("|");

  // Blocks: headings, body rhythm.
  for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
    QTextCursor c(b);
    QTextBlockFormat bf = b.blockFormat();
    const int level = bf.headingLevel();
    if (level > 0) {
      static constexpr qreal kSize[] = {0, 22, 15.5, 12.5, 11, 10.5, 10.5};
      static constexpr qreal kTop[] = {0, 2, 26, 18, 14, 12, 12};
      bf.setTopMargin(kTop[std::min(level, 6)]);
      bf.setBottomMargin(level == 1 ? 14 : 6);
      bf.setLineHeight(115, QTextBlockFormat::ProportionalHeight);
      c.setBlockFormat(bf);
      QTextCharFormat cf;
      cf.setFontPointSize(kSize[std::min(level, 6)]);
      cf.setFontWeight(level <= 2 ? QFont::DemiBold : QFont::Bold);
      cf.setForeground(level == 4 ? t.muted : t.text);
      c.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
      c.mergeCharFormat(cf);
      continue;
    }
    if (IsCodeBlock(b)) continue;
    if (bf.hasProperty(QTextFormat::BlockTrailingHorizontalRulerWidth)) {
      // "---" before Related: the rule draws in a hard, unthemeable colour; spacing separates it.
      bf.clearProperty(QTextFormat::BlockTrailingHorizontalRulerWidth);
      bf.setTopMargin(6);
      c.setBlockFormat(bf);
      continue;
    }
    // Proportional line height would scale an image line too, leaving a gap half the image tall.
    if (!b.text().contains(QChar::ObjectReplacementCharacter))
      bf.setLineHeight(140, QTextBlockFormat::ProportionalHeight);
    bf.setBottomMargin(b.textList() ? 3 : 10);
    c.setBlockFormat(bf);
  }

  // Inline runs: code spans, links, images. Collected first: restyling merges fragments.
  struct Run { int position, length; QTextCharFormat format; bool codeBlock; };
  std::vector<Run> runs;
  for (QTextBlock b = doc->begin(); b.isValid(); b = b.next())
    for (auto it = b.begin(); !it.atEnd(); ++it)
      if (it.fragment().isValid())
        runs.push_back({it.fragment().position(), it.fragment().length(), it.fragment().charFormat(), IsCodeBlock(b)});
  for (const Run& run : runs) {
    {
      const bool codeBlock = run.codeBlock;
      QTextCharFormat cf = run.format;
      QTextCursor c(doc);
      c.setPosition(run.position);
      c.setPosition(run.position + run.length, QTextCursor::KeepAnchor);
      if (cf.isImageFormat()) {
        QTextImageFormat image = cf.toImageFormat();
        const QImage source(QUrl(image.name()).toLocalFile());
        if (!source.isNull() && source.width() > kMaxImageWidth) {
          image.setWidth(kMaxImageWidth);
          image.setHeight(source.height() * kMaxImageWidth / static_cast<qreal>(source.width()));
          c.setCharFormat(image);
        }
        continue;
      }
      if (cf.isAnchor()) {
        cf.setForeground(t.accent);
        cf.setFontUnderline(false);
        c.setCharFormat(cf);
      } else if (cf.fontFixedPitch() || codeBlock) {
        cf.setFontFamilies({"Cascadia Mono", "Consolas", "Courier New"});
        cf.setFontPointSize(9.5);
        cf.setForeground(t.codeText);
        if (!codeBlock) cf.setBackground(t.codeBg);
        c.setCharFormat(cf);
      }
    }
  }

  // Tables: the leading metadata table is the page's info card; others get borders and stripes.
  bool first = true;
  for (QTextFrame* frame : doc->rootFrame()->childFrames()) {
    auto* table = qobject_cast<QTextTable*>(frame);
    if (!table) continue;
    QTextTableFormat tf = table->format();
    tf.setCellSpacing(0);
    tf.setBorderCollapse(true);
    tf.setBorder(1);
    tf.setBorderBrush(t.rule);
    tf.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
    tf.setCellPadding(7);
    tf.setTopMargin(4);
    tf.setBottomMargin(14);
    const bool card = first && table->rows() > 1 && table->cellAt(0, 0).firstCursorPosition().block().text().isEmpty() &&
                      table->cellAt(0, 1).firstCursorPosition().block().text().isEmpty();
    first = false;
    if (card) {
      table->removeRows(0, 1);
      tf.setBackground(t.card);
      tf.setWidth(QTextLength(QTextLength::PercentageLength, 100));
      tf.setColumnWidthConstraints({QTextLength(QTextLength::FixedLength, 110), QTextLength(QTextLength::VariableLength, 0)});
      table->setFormat(tf);
      for (int r = 0; r < table->rows(); ++r)
        for (int col = 0; col < table->columns(); ++col) {
          QTextTableCell cell = table->cellAt(r, col);
          QTextTableCellFormat format = cell.format().toTableCellFormat();
          format.setBackground(t.card);
          format.setTopPadding(r == 0 ? 10 : 3);
          format.setBottomPadding(r == table->rows() - 1 ? 10 : 3);
          format.setLeftPadding(col == 0 ? 14 : 6);
          cell.setFormat(format);
          QTextCursor c = cell.firstCursorPosition();
          c.setPosition(cell.lastCursorPosition().position(), QTextCursor::KeepAnchor);
          QTextBlockFormat compact;
          compact.setLineHeight(125, QTextBlockFormat::ProportionalHeight);
          compact.setBottomMargin(0);
          c.mergeBlockFormat(compact);
          if (col == 0) {
            QTextCharFormat key;
            key.setForeground(t.muted);
            key.setFontPointSize(9.5);
            c.mergeCharFormat(key);
          }
        }
      continue;
    }
    tf.setWidth(QTextLength(QTextLength::PercentageLength, 100));
    tf.setHeaderRowCount(1);
    table->setFormat(tf);
    for (int r = 0; r < table->rows(); ++r)
      for (int col = 0; col < table->columns(); ++col) {
        QTextTableCell cell = table->cellAt(r, col);
        QTextTableCellFormat format = cell.format().toTableCellFormat();
        if (r == 0) format.setBackground(t.tableHead);
        else if (r % 2 == 0) format.setBackground(t.tableStripe);
        cell.setFormat(format);
        QTextCursor c = cell.firstCursorPosition();
        c.setPosition(cell.lastCursorPosition().position(), QTextCursor::KeepAnchor);
        QTextBlockFormat bf;
        bf.setLineHeight(130, QTextBlockFormat::ProportionalHeight);
        bf.setBottomMargin(0);
        c.mergeBlockFormat(bf);
        if (r == 0) {
          QTextCharFormat head;
          head.setFontWeight(QFont::DemiBold);
          c.mergeCharFormat(head);
        }
      }
  }

  // Code blocks and "> " callouts become padded boxes.
  QTextTableFormat box;
  box.setBorder(0);
  box.setCellSpacing(0);
  box.setCellPadding(10);
  box.setTopMargin(2);
  box.setBottomMargin(14);
  box.setWidth(QTextLength(QTextLength::PercentageLength, 100));
  QTextBlockFormat inner;
  inner.setLeftMargin(0);
  inner.setIndent(0);
  inner.setBottomMargin(0);
  inner.setProperty(QTextFormat::BlockQuoteLevel, 0);
  box.setBackground(t.codeBg);
  QTextBlockFormat codeInner = inner;
  codeInner.setLineHeight(135, QTextBlockFormat::ProportionalHeight);
  BoxBlockRuns(doc, IsCodeBlock, box, codeInner);
  box.setBackground(t.callout);
  box.setBorder(1);
  box.setBorderBrush(t.calloutEdge);
  box.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
  QTextBlockFormat quoteInner = inner;
  quoteInner.setLineHeight(150, QTextBlockFormat::ProportionalHeight);
  BoxBlockRuns(doc, IsQuoteBlock, box, quoteInner);

  // Boxing and the dropped rules leave empty paragraphs between elements; collapse them.
  for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
    if (!b.text().isEmpty() || b.textList() || QTextCursor(b).currentTable()) continue;
    QTextBlockFormat bf = b.blockFormat();
    bf.setLineHeight(1, QTextBlockFormat::FixedHeight);
    bf.setTopMargin(0);
    bf.setBottomMargin(0);
    QTextCursor(b).setBlockFormat(bf);
  }
}

/// Keeps the text column at a readable width, centred in a wide window.
class ReadableColumn : public QObject {
 public:
  explicit ReadableColumn(QTextBrowser* browser) : QObject(browser), m_Browser(browser) {
    browser->viewport()->installEventFilter(this);
  }
  void Apply() {
    QTextFrameFormat f = m_Browser->document()->rootFrame()->frameFormat();
    const int side = std::max(32, (m_Browser->viewport()->width() - kReadableWidth) / 2);
    f.setLeftMargin(side);
    f.setRightMargin(side);
    f.setTopMargin(26);
    f.setBottomMargin(40);
    m_Browser->document()->rootFrame()->setFrameFormat(f);
  }

 protected:
  bool eventFilter(QObject*, QEvent* event) override {
    if (event->type() == QEvent::Resize) Apply();
    return false;
  }

 private:
  QTextBrowser* m_Browser;
};

}  // namespace

void EditorWindow::ShowContextualHelp(const QString& topicCommand, const QString& uiTooltip) {
  if (!m_WikiLoaded) {
    m_WikiBundle = LoadWikiBundle();
    WikiHelpBuildIndex(m_WikiBundle);
    m_WikiLoaded = true;
  }
  // Topic order: the explicit command, then the hovered control's "Command bar:" name, its ribbon
  // route or UI topic, then (when none of those names a page) the running or last command, then Home.
  WikiHelpTarget uiTopic;
  if (!uiTooltip.isEmpty()) uiTopic = WikiHelpTargetFromUiTooltip(uiTooltip.toUtf8().toStdString());
  std::string primary = HelpCommandPrimary(topicCommand);
  if (primary.empty()) primary = HelpCommandPrimary(QString::fromStdString(uiTopic.commandPrimary));
  const bool uiPage = primary.empty() && !uiTopic.pageSlug.empty() && uiTopic.commandPrimary.empty();
  if (primary.empty() && !uiPage) primary = HelpCommandPrimaryForKind(m_State.active);
  if (primary.empty() && !uiPage) primary = HelpCommandPrimaryForKind(m_State.lastCommand);
  WikiHelpTarget target;
  if (!primary.empty()) target = WikiHelpLookupCommand(primary);
  else if (uiPage) target = uiTopic;
  if (target.pageSlug.empty()) target = WikiHelpTarget{"Home", {}};

  const bool dark = palette().color(QPalette::Window).lightness() < 128;
  const ManualTheme theme = ManualThemeFor(dark);
  const QtAccent& accent = QtAccentFor(dark);

  QDialog dialog(this); dialog.setObjectName("contextualHelpDialog");
  dialog.setWindowTitle("GoSurvey User Manual"); dialog.resize(1200, 800);
  dialog.setStyleSheet(QString(
      "QDialog#contextualHelpDialog { background:%1; }"
      "QWidget#manualTopBar { background:%2; border-bottom:1px solid %3; }"
      "QLabel#manualCrumbs { color:%4; font-size:13px; padding-left:6px; }"
      "QToolButton { color:%5; background:transparent; border:1px solid transparent; border-radius:5px;"
      " padding:3px 9px; font-size:15px; }"
      "QToolButton:hover { background:%6; } QToolButton:disabled { color:%3; }"
      "QLineEdit#manualSearch { background:%1; color:%5; border:1px solid %3; border-radius:6px;"
      " padding:6px 9px; font-size:13px; margin:10px 10px 6px 10px; }"
      "QLineEdit#manualSearch:focus { border-color:%7; }"
      "QListWidget#manualPages { background:%2; color:%5; border:none; outline:0; font-size:13px; padding:0 6px 10px 6px; }"
      "QListWidget#manualPages::item { padding:5px 10px; border-radius:5px; }"
      "QListWidget#manualPages::item:hover { background:%6; }"
      "QListWidget#manualPages::item:selected { background:%8; color:%5; }"
      "QWidget#manualSide { background:%2; border-right:1px solid %3; }"
      "QTextBrowser#manualContent { background:%1; color:%5; border:none; }"
      "QSplitter::handle { background:%3; width:1px; }")
      .arg(theme.page.name(), theme.sidebar.name(), theme.rule.name(), theme.muted.name(), theme.text.name(),
           dark ? "#2c3338" : "#e6e9ec", accent.strong, accent.tint));
  auto* layout = new QVBoxLayout(&dialog); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(0);

  // Top bar: back / forward / home, then where you are.
  auto* topBar = new QWidget(&dialog); topBar->setObjectName("manualTopBar");
  auto* topLayout = new QHBoxLayout(topBar); topLayout->setContentsMargins(10, 6, 10, 6); topLayout->setSpacing(2);
  auto* back = new QToolButton(topBar); back->setObjectName("manualBack"); back->setText("←"); back->setToolTip("Back (Alt+Left)");
  auto* forward = new QToolButton(topBar); forward->setObjectName("manualForward"); forward->setText("→"); forward->setToolTip("Forward (Alt+Right)");
  auto* home = new QToolButton(topBar); home->setObjectName("manualHome"); home->setText("⌂"); home->setToolTip("Manual home");
  back->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Left)); forward->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Right));
  auto* crumbs = new QLabel(topBar); crumbs->setObjectName("manualCrumbs");
  topLayout->addWidget(back); topLayout->addWidget(forward); topLayout->addWidget(home); topLayout->addWidget(crumbs, 1);
  layout->addWidget(topBar);

  auto* splitter = new QSplitter(&dialog); splitter->setObjectName("manualSplitter"); splitter->setHandleWidth(1);
  layout->addWidget(splitter, 1);
  auto* side = new QWidget(splitter); side->setObjectName("manualSide");
  auto* sideLayout = new QVBoxLayout(side); sideLayout->setContentsMargins(0, 0, 0, 0); sideLayout->setSpacing(0);
  auto* search = new QLineEdit(side); search->setObjectName("manualSearch");
  search->setPlaceholderText("Search the manual"); search->setClearButtonEnabled(true);
  auto* pages = new QListWidget(side); pages->setObjectName("manualPages");
  sideLayout->addWidget(search); sideLayout->addWidget(pages, 1);
  auto* browser = new QTextBrowser(splitter); browser->setObjectName("manualContent");
  browser->setOpenLinks(false); browser->setOpenExternalLinks(false);
  browser->document()->setDocumentMargin(0);
  QFont body("Segoe UI"); body.setPointSizeF(10.5);
  browser->document()->setDefaultFont(body);
  auto* column = new ReadableColumn(browser);
  splitter->addWidget(side); splitter->addWidget(browser); splitter->setStretchFactor(1, 1);
  splitter->setSizes({270, 930});

  // Section label for the breadcrumb: the sidebar group a page sits under, "Commands" for the
  // per-command pages.
  const auto sectionOf = [this](const QString& slug) {
    if (slug.startsWith("Commands/")) return QStringLiteral("Commands");
    QString section;
    for (const auto& entry : m_WikiBundle.nav) {
      if (entry.isSection) section = QString::fromUtf8(entry.label.c_str());
      else if (QString::fromUtf8(entry.slug.c_str()) == slug) return section;
    }
    return QString();
  };
  // Shows a page and scrolls to `focus`: for a command, the heading that documents it, else its first
  // whole-word mention in capitals; for a search, the first case-insensitive match.
  auto showPage = [this, browser, crumbs, column, theme, sectionOf](const QString& slug, const QString& focus = {}, bool command = false) {
    const auto found = m_WikiBundle.pagesBySlug.find(slug.toUtf8().toStdString());
    if (found == m_WikiBundle.pagesBySlug.end()) return false;
    const QString pageTitle = QString::fromUtf8(found->second.title.c_str());
    const QString section = sectionOf(slug);
    crumbs->setText(section.isEmpty() || section == "GoSurvey Manual" ? pageTitle : section + "  ›  " + pageTitle);
    QString markdown = QString::fromUtf8(found->second.markdown.c_str());
    QRegularExpression image("wiki-img:([A-Za-z0-9._/-]+)");
    auto match = image.globalMatch(markdown);
    QString rewritten; qsizetype last = 0;
    while (match.hasNext()) {
      const auto item = match.next();
      const auto path = std::filesystem::path(m_WikiBundle.rootPath) / "images" / item.captured(1).toStdWString();
      rewritten += markdown.mid(last, item.capturedStart() - last);
      rewritten += QUrl::fromLocalFile(QString::fromStdWString(path.wstring())).toString();
      last = item.capturedEnd();
    }
    rewritten += markdown.mid(last);
    browser->setMarkdown(rewritten);
    StyleManualDocument(browser->document(), theme);
    column->Apply();
    browser->moveCursor(QTextCursor::Start);
    if (focus.isEmpty()) return true;
    QTextDocument* doc = browser->document();
    QTextCursor at;
    if (command) {
      const std::string primaryLower = focus.toLower().toStdString();
      for (QTextBlock block = doc->begin(); block.isValid() && at.isNull(); block = block.next())
        if (block.blockFormat().headingLevel() > 0 && WikiHeadingMatchesCommand(block.text().toStdString(), primaryLower))
          at = QTextCursor(block);
      if (at.isNull()) at = doc->find(focus.toUpper(), 0, QTextDocument::FindCaseSensitively | QTextDocument::FindWholeWords);
    } else {
      at = doc->find(focus);
    }
    if (at.isNull()) return true;
    // Jump to the end first so ensureCursorVisible brings the match to the top, not the bottom.
    browser->moveCursor(QTextCursor::End); browser->setTextCursor(at); browser->ensureCursorVisible();
    return true;
  };

  // History for back / forward. Each entry is a page slug.
  struct History { std::vector<QString> pages; int at = -1; };
  auto history = std::make_shared<History>();
  const auto syncButtons = [back, forward, history] {
    back->setEnabled(history->at > 0);
    forward->setEnabled(history->at + 1 < static_cast<int>(history->pages.size()));
  };
  const auto selectInList = [pages](const QString& slug) {
    const QSignalBlocker block(pages);
    pages->clearSelection(); pages->setCurrentRow(-1);
    for (int i = 0; i < pages->count(); ++i)
      if (pages->item(i)->data(Qt::UserRole).toString() == slug) { pages->setCurrentRow(i); break; }
  };
  // Opens a page and records it; `focus`/`command` as for showPage.
  const auto navigate = [showPage, history, syncButtons, selectInList](const QString& slug, const QString& focus = {}, bool command = false) {
    if (!showPage(slug, focus, command)) return;
    if (history->at < 0 || history->pages[static_cast<size_t>(history->at)] != slug) {
      history->pages.resize(static_cast<size_t>(history->at + 1));
      history->pages.push_back(slug);
      history->at = static_cast<int>(history->pages.size()) - 1;
    }
    selectInList(slug);
    syncButtons();
  };
  const auto step = [showPage, history, syncButtons, selectInList](int delta) {
    const int next = history->at + delta;
    if (next < 0 || next >= static_cast<int>(history->pages.size())) return;
    history->at = next;
    showPage(history->pages[static_cast<size_t>(next)]);
    selectInList(history->pages[static_cast<size_t>(next)]);
    syncButtons();
  };
  connect(back, &QToolButton::clicked, &dialog, [step] { step(-1); });
  connect(forward, &QToolButton::clicked, &dialog, [step] { step(1); });
  connect(home, &QToolButton::clicked, &dialog, [navigate] { navigate("Home"); });

  const auto fillNav = [this, pages, theme] {
    const QSignalBlocker block(pages);
    pages->clear();
    for (const auto& entry : m_WikiBundle.nav) {
      if (entry.isSection) {
        if (entry.label == "GoSurvey Manual") continue;  // the window title already says it
        auto* item = new QListWidgetItem(QString::fromUtf8(entry.label.c_str()).toUpper(), pages);
        item->setFlags(Qt::NoItemFlags);
        QFont font = item->font(); font.setPointSizeF(8); font.setBold(true); font.setLetterSpacing(QFont::AbsoluteSpacing, 0.8);
        item->setFont(font); item->setForeground(theme.muted);
        item->setSizeHint(QSize(0, 34)); item->setTextAlignment(Qt::AlignLeft | Qt::AlignBottom);
      } else {
        auto* item = new QListWidgetItem(QString::fromUtf8(entry.label.c_str()), pages);
        item->setData(Qt::UserRole, QString::fromUtf8(entry.slug.c_str()));
      }
    }
  };
  // Search covers every page, including the per-command pages the sidebar does not list. Every word
  // must appear somewhere on the page; title hits rank first, then pages that mention the words most.
  const auto fillResults = [this, pages, theme](const QString& query) {
    const QStringList words = query.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    struct Hit { long long score; QString title; QString slug; };
    std::vector<Hit> hits;
    for (const auto& [slug, page] : m_WikiBundle.pagesBySlug) {
      const QString pageTitle = QString::fromUtf8(page.title.c_str());
      const QString body = QString::fromUtf8(page.markdown.c_str());
      long long score = 0; bool all = true;
      for (const QString& word : words) {
        const bool inTitle = pageTitle.contains(word, Qt::CaseInsensitive);
        const qsizetype mentions = body.count(word, Qt::CaseInsensitive);
        if (!inTitle && mentions == 0) { all = false; break; }
        score += (inTitle ? 1000 : 0) + std::min<qsizetype>(mentions, 50);
      }
      if (all) hits.push_back({score, pageTitle, QString::fromUtf8(slug.c_str())});
    }
    std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) {
      return a.score != b.score ? a.score > b.score : a.title.compare(b.title, Qt::CaseInsensitive) < 0;
    });
    const QSignalBlocker block(pages);
    pages->clear();
    auto* heading = new QListWidgetItem(hits.empty() ? QStringLiteral("NO MATCHES")
                                                     : QString("%1 RESULT%2").arg(hits.size()).arg(hits.size() == 1 ? "" : "S"), pages);
    heading->setFlags(Qt::NoItemFlags);
    QFont font = heading->font(); font.setPointSizeF(8); font.setBold(true); font.setLetterSpacing(QFont::AbsoluteSpacing, 0.8);
    heading->setFont(font); heading->setForeground(theme.muted); heading->setSizeHint(QSize(0, 30));
    for (const Hit& hit : hits) {
      const bool commandPage = hit.slug.startsWith("Commands/");
      auto* item = new QListWidgetItem(commandPage ? hit.title + "  ·  command" : hit.title, pages);
      item->setData(Qt::UserRole, hit.slug); item->setToolTip(hit.slug);
    }
  };
  fillNav();
  connect(pages, &QListWidget::currentItemChanged, &dialog, [navigate, search](QListWidgetItem* current) {
    if (!current) return;
    const QString slug = current->data(Qt::UserRole).toString();
    if (slug.isEmpty()) return;
    navigate(slug, search->text().trimmed().section(QRegularExpression("\\s+"), 0, 0));
  });
  connect(search, &QLineEdit::textChanged, &dialog, [fillNav, fillResults](const QString& text) {
    if (text.trimmed().isEmpty()) fillNav(); else fillResults(text.trimmed());
  });
  connect(search, &QLineEdit::returnPressed, &dialog, [pages] {
    for (int i = 0; i < pages->count(); ++i) if (pages->item(i)->flags() & Qt::ItemIsSelectable) {
      pages->setCurrentRow(i); pages->setFocus(); return;
    }
  });
  connect(browser, &QTextBrowser::anchorClicked, &dialog, [navigate](const QUrl& url) {
    if (url.scheme() != "wiki") { QDesktopServices::openUrl(url); return; }
    navigate(url.path().isEmpty() ? url.host() : url.path());
  });

  // A command's own page opens at the top; a shared topic page scrolls to the command.
  const QString initial = QString::fromUtf8(target.pageSlug.c_str());
  const QString commandName = QString::fromStdString(target.commandPrimary);
  const bool ownPage = initial.compare("Commands/" + commandName, Qt::CaseInsensitive) == 0;
  navigate(initial, ownPage ? QString() : commandName, true);
  if (!m_WikiBundle.ok) browser->setPlainText(QString::fromUtf8(m_WikiBundle.fallbackMessage.c_str()));
  syncButtons();
  search->setFocus();
  dialog.exec();
}
