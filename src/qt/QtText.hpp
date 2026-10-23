#pragma once
#include <QColor>
#include <string>
class QTextDocument;
void PopulateQtText(QTextDocument& document, float pixels, const std::string& wire,
                    const std::string& family, const QColor& color = Qt::white);
void MeasureQtText(float pixels, const std::string& wire, const std::string& family,
                   float* width, float* height);
