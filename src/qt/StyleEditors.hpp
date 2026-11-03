#pragma once

#include <QString>
#include <QStringList>

#include "CadEntities.hpp"
#include "DimensionStyle.hpp"
#include "MultileaderStyle.hpp"
#include "cadtablestyle.hpp"

class QWidget;

// The Modify / New style windows for the four annotation style kinds. Each edits a copy the caller passes in and
// returns true when the user pressed OK (the caller then stores it and takes the undo snapshot). The windows follow
// the application theme; their layout mirrors the Civil 3D / AutoCAD dialogs the product is compared against.

bool EditTextStyleDialog(QWidget* parent, TextStyle* style, bool create);
bool EditDimensionStyleDialog(QWidget* parent, DimensionStyle* style, bool create, const QStringList& textStyleNames);
bool EditMultileaderStyleDialog(QWidget* parent, MultileaderStyle* style, bool create, const QStringList& textStyleNames);
bool EditTableStyleDialog(QWidget* parent, CadTableStyle* style, bool create, const QStringList& textStyleNames);
