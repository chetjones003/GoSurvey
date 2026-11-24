#include "QtText.hpp"
#include "GoSurvey/Document/MtextRichSpans.hpp"
#include <QFont>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QFileInfo>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextBlock>
#include <QTextFragment>
#include <algorithm>
#include <cmath>

namespace {
QString NormalizeFontName(QString name) {
  QString normalized;
  normalized.reserve(name.size());
  for (const QChar ch : name)
    if (ch.isLetterOrNumber())
      normalized.append(ch.toCaseFolded());
  return normalized;
}
}

QString QtFontFamilyForCadFont(const std::string& fontName) {
  const QString input = QString::fromUtf8(fontName.c_str()).trimmed();
  if (input.isEmpty() || input.endsWith(QStringLiteral(".shx"), Qt::CaseInsensitive))
    return input;
  const QFileInfo file(input);
  const QString baseName = file.completeBaseName();
  if (baseName.isEmpty())
    return input;
  const QString wanted = NormalizeFontName(baseName);
  if (wanted == QStringLiteral("arial"))
    return QStringLiteral("Arial");
  QFontDatabase database;
  for (const QString& family : database.families())
    if (NormalizeFontName(family) == wanted)
      return family;
  const QString suffix = file.suffix();
  if (suffix.compare(QStringLiteral("ttf"), Qt::CaseInsensitive) == 0 ||
      suffix.compare(QStringLiteral("otf"), Qt::CaseInsensitive) == 0 ||
      suffix.compare(QStringLiteral("ttc"), Qt::CaseInsensitive) == 0)
    return baseName;
  return input;
}

void PopulateQtText(QTextDocument& document, float pixels, const std::string& wire,
                    const std::string& family, const QColor& color, bool inheritColor) {
  document.clear();
  document.setDocumentMargin(0);
  QFont font(family.empty() ? QStringLiteral("Segoe UI") : QtFontFamilyForCadFont(family));
  const int targetCapHeight = std::clamp(qRound(std::isfinite(pixels) ? pixels : 12), 1, 4096);
  font.setPixelSize(targetCapHeight);
  const qreal capHeight = QFontMetricsF(font).capHeight();
  if (capHeight > 0.0)
    font.setPixelSize(std::clamp(qRound(targetCapHeight * targetCapHeight / capHeight), 1, 4096));
  document.setDefaultFont(font);
  QTextCursor cursor(&document);
  std::vector<MtextRichSpan> spans;
  MtextRichBuildSpans(wire, &spans);
  for (const auto& span : spans) {
    QTextCharFormat format;
    format.setFontWeight(span.bold ? QFont::Bold : QFont::Normal);
    format.setFontItalic(span.italic); format.setFontUnderline(span.underline);
    if (span.hasColor || !inheritColor) format.setForeground(span.hasColor ? QColor::fromRgb(span.color) : color);
    if (!span.font.empty()) format.setFontFamilies({QString::fromUtf8(span.font.c_str())});
    // Caps is a display attribute: keep the typed casing so an edited document round-trips it.
    if (span.caps) format.setFontCapitalization(QFont::AllUppercase);
    const std::string text = wire.substr(span.rawBegin, span.rawEnd - span.rawBegin);
    cursor.insertText(QString::fromUtf8(text.c_str()), format);
  }
}

void MeasureQtText(float pixels, const std::string& wire, const std::string& family, float* width, float* height) {
  QTextDocument document;
  PopulateQtText(document, pixels, wire, family);
  document.adjustSize();
  if (width) *width = static_cast<float>(document.idealWidth());
  if (height) *height = static_cast<float>(document.size().height());
}

std::string QtTextToMtextWire(const QTextDocument& document) {
  std::string wire;
  for (auto block = document.begin(); block.isValid(); block = block.next()) {
    if (block != document.begin()) wire += '\n';
    for (auto it = block.begin(); !it.atEnd(); ++it) {
      const auto fragment = it.fragment();
      if (!fragment.isValid()) continue;
      const auto format = fragment.charFormat();
      std::string open, close;
      const auto wrap = [&](const std::string& a, const std::string& b) { open += a; close = b + close; };
      if (format.fontWeight() >= QFont::Bold) wrap("[[b]]", "[[/b]]");
      if (format.fontItalic()) wrap("[[i]]", "[[/i]]");
      if (format.fontUnderline()) wrap("[[u]]", "[[/u]]");
      if (format.fontCapitalization() == QFont::AllUppercase) wrap("[[caps]]", "[[/caps]]");
      if (format.hasProperty(QTextFormat::FontFamilies)) {
        const auto families = format.fontFamilies().toStringList();
        if (!families.isEmpty()) wrap("[[font:" + families.front().toUtf8().toStdString() + "]]", "[[/font]]");
      }
      if (format.foreground().style() != Qt::NoBrush) {
        const auto rgb = format.foreground().color().name(QColor::HexRgb).mid(1).toUpper();
        wrap("[[color:" + rgb.toStdString() + "]]", "[[/color]]");
      }
      wire += open + fragment.text().toUtf8().toStdString() + close;
    }
  }
  return wire;
}
