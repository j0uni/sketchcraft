#pragma once
// Toolbar icons drawn in code (simple original glyphs in the spirit of a modelling app's toolbar).

#include <QIcon>
#include <QString>

namespace ui {

// Ids: select, eraser, line, rectangle, circle, pushpull, move, tape, paint, orbit, pan, zoom, zoomext,
// iso, top, front, right, back, left, xray, backedges, wireframe, hiddenline, shaded, textured,
// monochrome, new, open, save, undo, redo, perspective, scene.
QIcon icon(const QString& id);

}  // namespace ui
