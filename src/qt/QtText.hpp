#pragma once
#include <QColor>
#include <QString>
#include <string>
class QTextDocument;
QString QtFontFamilyForCadFont(const std::string& fontName);
// The requested pixel height is CAD's nominal character/cap height, not the font's em size.
void PopulateQtText(QTextDocument& document, float pixels, const std::string& wire,
                    const std::string& family, const QColor& color = Qt::white);
void MeasureQtText(float pixels, const std::string& wire, const std::string& family,
                   float* width, float* height);
