#include "QtText.hpp"
#include "MtextRichSpans.hpp"
#include <QFont>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <algorithm>
#include <cmath>

void PopulateQtText(QTextDocument& document, float pixels, const std::string& wire,
                    const std::string& family, const QColor& color) {
  document.clear();
  document.setDocumentMargin(0);
  QFont font(family.empty() ? "Segoe UI" : QString::fromUtf8(family.c_str()));
  font.setPixelSize(std::clamp(qRound(std::isfinite(pixels) ? pixels : 12), 1, 4096));
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
