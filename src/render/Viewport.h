#pragma once
// The 3D drawing area: sky and ground, shaded faces with textures, edges and profiles, axes,
// selection highlights and the drawing tools' previews. Also camera navigation and picking.

#include "geom/Math.h"
#include "render/SceneBuilder.h"
#include "render/Style.h"

#include <QColor>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QOpenGLWidget>
#include <QPointer>
#include <QVariantAnimation>

#include <memory>
#include <optional>
#include <vector>

namespace render {

struct View {
    geom::V3 eye{240.719, -236.069, 102.276};
    geom::V3 target{-285.372, 553.004, -82.0446};
    geom::V3 up{0, 0, 1};
    bool perspective = true;
    double fovDeg = 35;        // height field of view
    double orthoHeight = 133;  // model inches visible top to bottom in parallel projection
};

enum class StandardView { Iso, Top, Bottom, Front, Back, Left, Right };

struct OverlayLine {
    geom::V3 a, b;
    QColor color{0, 0, 0};
    float width = 1.0f;
    bool dashed = false;
};

struct OverlayPoint {
    geom::V3 p;
    QColor color{0, 160, 0};
    float size = 8.0f;
};

// What the active tool wants drawn on top of the model.
struct Overlay {
    std::vector<OverlayLine> lines;
    std::vector<OverlayPoint> points;
    std::optional<QRectF> selectionBox;
    bool crossing = false;  // right-to-left box selection is drawn dashed
    QString tip;
    QPointF tipAt;
};

struct Ray {
    geom::V3 origin, dir;
};

struct Hit {
    Pick pick;
    geom::V3 point;
    geom::V3 normal;  // world normal of a face hit
    bool isEdge = false;
    geom::V3 edgeA, edgeB;
};

// Receives the mouse and keys while a drawing tool is active.
class InputHandler : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    virtual void press(QMouseEvent* e) = 0;
    virtual void move(QMouseEvent* e) = 0;
    virtual void release(QMouseEvent* e) = 0;
    virtual void doubleClick(QMouseEvent* e) { (void)e; }
    virtual bool key(QKeyEvent* e) {
        (void)e;
        return false;
    }
};

class Viewport : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT
public:
    explicit Viewport(QWidget* parent = nullptr);
    ~Viewport() override;

    void setScene(Scene scene, const Style& style);
    void setStyle(const Style& style);
    const Scene& scene() const { return scene_; }
    const Style& style() const { return style_; }
    void setOverlay(Overlay o);
    void setInputHandler(InputHandler* h) { handler_ = h; }

    View view() const { return view_; }
    void setView(const View& v, bool animate = false);
    void zoomExtents(bool animate = true);
    void standardView(StandardView sv);
    void setPerspective(bool on);
    // Picks the pivot for an orbit that starts at a widget position.
    void beginOrbit(QPointF at);
    void endOrbit() { orbitCenter_.reset(); }
    void orbitBy(double dxPixels, double dyPixels);
    void panBy(double dxPixels, double dyPixels);
    void zoomAt(QPointF at, double factor);
    void zoomWheel(QPointF at, double notches);
    void zoomFov(double deltaDeg);

    Ray rayAt(QPointF widgetPos) const;
    std::optional<QPointF> project(const geom::V3& p) const;
    // Distance in model inches that one pixel covers at a point.
    double pixelSize(const geom::V3& at) const;
    std::optional<Hit> pick(QPointF widgetPos, bool edges = true) const;
    // Root entities whose drawing falls inside (or crosses) a widget rectangle.
    std::vector<skp::ObjId> pickBox(const QRectF& box, bool crossing) const;
    // Nearest vertex of the model to a widget position, within `radius` pixels.
    std::optional<geom::V3> snapVertex(QPointF widgetPos, double radius) const;
    geom::V3 eyeDirection() const;

    // Counts drawn frames (used by --screenshot).
    int framesDrawn() const { return frames_; }

signals:
    void viewChanged();
    void framePainted();

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;

private:
    struct GpuBatch {
        std::unique_ptr<QOpenGLBuffer> vbo;
        int count = 0;
        int texture = -1;
        bool transparent = false;
    };

    geom::M4 viewMatrix() const;
    geom::M4 projMatrix() const;
    geom::M4 viewProj() const { return projMatrix() * viewMatrix(); }
    void nearFar(double& n, double& f) const;
    void uploadScene();
    void destroyGpu();
    void drawBackground();
    void drawFaces(bool transparent);
    void drawLines(const std::vector<float>& data, bool depthTest);
    void buildEdgeLines(std::vector<float>& solid, std::vector<float>& back) const;
    void addLine(std::vector<float>& out, const geom::V3& a, const geom::V3& b, const QColor& c, float width,
                 bool dashed) const;
    void drawOverlayText();

    Scene scene_;
    Style style_;
    View view_;
    Overlay overlay_;
    QPointer<InputHandler> handler_;
    bool sceneDirty_ = true;

    std::unique_ptr<QOpenGLShaderProgram> faceProg_, lineProg_, skyProg_;
    std::vector<GpuBatch> gpu_;
    std::vector<std::unique_ptr<QOpenGLTexture>> textures_;
    std::unique_ptr<QOpenGLBuffer> lineVbo_;
    std::unique_ptr<QOpenGLTexture> whiteTex_;
    bool glReady_ = false;

    // Navigation with the middle button.
    enum class Drag { None, Orbit, Pan };
    Drag drag_ = Drag::None;
    QPointF lastPos_;
    std::optional<geom::V3> orbitCenter_;
    std::unique_ptr<QVariantAnimation> anim_;
    View animFrom_, animTo_;
    int frames_ = 0;
};

}  // namespace render
