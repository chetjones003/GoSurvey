#include "QtText.hpp"
#include "MtextRichSpans.hpp"
#include <QFont>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QFileInfo>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
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
                    const std::string& family, const QColor& color) {
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
    format.setForeground(span.hasColor ? QColor::fromRgb(span.color) : color);
    if (!span.font.empty()) format.setFontFamilies({QString::fromUtf8(span.font.c_str())});
    std::string text = wire.substr(span.rawBegin, span.rawEnd - span.rawBegin);
    if (span.caps) MtextRichApplyCapsAscii(&text);
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
