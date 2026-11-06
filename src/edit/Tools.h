#pragma once
// The drawing and navigation tools, driven by the viewport's mouse and the Measurements box.

#include "geom/Math.h"
#include "render/Viewport.h"
#include "ui/Document.h"
#include "ui/Units.h"

#include <QPointer>
#include <QString>

#include <optional>
#include <vector>

namespace edit {

enum class ToolId {
    Select, Eraser, Paint, Line, Rectangle, Circle, PushPull, Move, Tape, Orbit, Pan, Zoom,
    Arc, Polygon, Rotate, Scale, Offset, ZoomWindow, FieldOfView, Walk, LookAround, PositionCamera
};

// A point the cursor snapped to, and why.
struct Inference {
    geom::V3 p;
    QString label;  // "Endpoint", "Midpoint", "On Edge", "On Face", "Origin", "On Red Axis"...
    QColor color{0, 0, 0};
    std::optional<render::Hit> hit;
    std::optional<geom::V3> faceNormal;
    std::optional<skp::ObjId> faceId;  // a root face under the cursor
};

class ToolController : public render::InputHandler {
    Q_OBJECT
public:
    ToolController(render::Viewport* view, ui::Document* doc, QObject* parent);

    ToolId tool() const { return tool_; }
    void setTool(ToolId t);
    // A value typed into the Measurements box; returns false when it can't be used now.
    bool typed(const QString& text);
    void cancel();

    void press(QMouseEvent* e) override;
    void move(QMouseEvent* e) override;
    void release(QMouseEvent* e) override;
    void doubleClick(QMouseEvent* e) override;
    bool key(QKeyEvent* e) override;

signals:
    void status(const QString& text);
    void measurement(const QString& label, const QString& value);
    void toolChanged(edit::ToolId t);

private:
    Inference infer(QPointF pos, const std::optional<geom::V3>& from) const;
    std::optional<geom::V3> onPlane(QPointF pos, const geom::Plane& pl) const;
    void showPrompt();
    void preview();
    ui::UnitFormat units() const;
    QString len(double inches) const;

    void finishLine(const geom::V3& p);
    void finishRect(const geom::V3& p);
    void finishCircle(const geom::V3& p);
    void commitPushPull(double d);
    void commitMove(const geom::V3& delta);
    void selectAt(QPointF pos, Qt::KeyboardModifiers mods);
    void eraseAt(QPointF pos);
    void paintAt(QPointF pos, bool sample);
    // Plane and in-plane axes for rectangles and circles.
    void drawingFrame(geom::V3& n, geom::V3& u, geom::V3& v) const;

    QPointer<render::Viewport> view_;
    QPointer<ui::Document> doc_;
    ToolId tool_ = ToolId::Select;

    // Shared state of a tool in progress.
    std::optional<geom::V3> start_;       // first point (line, rectangle, circle centre, move base, tape)
    std::vector<geom::V3> chain_;         // points of the line being drawn
    std::optional<geom::Plane> plane_;    // drawing plane chosen at the first click
    Inference cur_;
    QPointF pressPos_, lastPos_, delta_;
    bool dragging_ = false;
    bool boxSelecting_ = false;
    // Push/pull.
    std::optional<skp::ObjId> ppFace_;
    geom::V3 ppNormal_, ppPoint_;
    double ppDist_ = 0, lastPushPull_ = 0;
    // Move.
    std::set<skp::ObjId> moving_;
    // Arrow keys lock drawing to an axis (right = red, left = green, up/down = blue).
    std::optional<geom::V3> lock_;

    // Tools in MoreTools.cpp (arc, polygon, rotate, scale, offset and the camera tools).
    static bool isExtra(ToolId t);
    void extraPress(QMouseEvent* e);
    void extraMove(QMouseEvent* e);
    void extraRelease(QMouseEvent* e);
    void extraPreview(render::Overlay& o);
    bool extraTyped(const QString& text);
    QString extraPrompt(QString& label) const;
    void finishArc(double bulge);
    void finishPolygon(const geom::V3& p);
    void commitRotate(double angle);
    void commitScale(double factor);
    void commitOffset(double distance);
    std::set<skp::ObjId> targetsAt(const QPointF& pos);
    int stage_ = 0;
    std::optional<geom::V3> second_;    // arc end, rotate reference, scale grip
    double amount_ = 0;                 // arc bulge, rotate angle, scale factor, offset distance
    int arcSegments_ = 12;              // CArc3PointTool::s_numSideDefault 0x100841eb8
    int polygonSides_ = 6;              // CPolyTool::s_numSideDefault 0x10083e210
    std::set<skp::ObjId> targets_;
    geom::V3 pivot_;
    std::optional<skp::ObjId> offsetFace_;
};

QString toolName(ToolId t);

}  // namespace edit
