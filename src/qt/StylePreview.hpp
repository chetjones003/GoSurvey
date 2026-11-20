#pragma once

#include <QPixmap>
#include <QSize>
#include <QString>

struct DimensionStyle;
struct MultileaderStyle;
struct TextStyle;

// Style-manager previews drawn by the same code as the ribbon gallery thumbnails (EditorRibbon.cpp).
QPixmap DimStylePreviewPixmap(const DimensionStyle& style, QSize size, qreal dpr);
QPixmap MleaderStylePreviewPixmap(const MultileaderStyle& style, const QString& label, QSize size, qreal dpr);
QPixmap TextStylePreviewPixmap(const TextStyle& style, QSize size, qreal dpr);

// Stylesheet that makes a QComboBox open a compact, scrolling drop-down in the current theme.
QString PopupComboStyleSheet();
