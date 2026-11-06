#include "render/Viewport.h"

#include <QMatrix4x4>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace render {

using geom::M4;
using geom::V3;

namespace {

constexpr int kLineFloats = 15;  // p0(3) p1(3) side(2) rgba(4) width, dashed, extension
constexpr double kDeg = std::numbers::pi / 180.0;
constexpr double kOrbitFactor = 0.5;    // CCameraTool::DoOrbit2 0x10012165c (constant 0x10073b2f8)
constexpr double kWheelDivisor = 10.0;  // CSketchView::DoMouseWheel 0x10014df6a (constant 0x10073b698)
constexpr double kMinZoomDepth = 1.0;   // inches; the original uses its near-plane distance (estimated)

const char* const kFaceVs = R"(#version 120
attribute vec3 a_pos;
attribute vec3 a_nrm;
attribute vec2 a_uv;
attribute vec4 a_col;
attribute float a_sel;
uniform mat4 u_mvp;
varying vec3 v_nrm;
varying vec2 v_uv;
varying vec4 v_col;
varying float v_sel;
void main() {
    gl_Position = u_mvp * vec4(a_pos, 1.0);
    v_nrm = a_nrm;
    v_uv = a_uv;
    v_col = a_col;
    v_sel = a_sel;
}
)";

const char* const kFaceFs = R"(#version 120
uniform sampler2D u_tex;
uniform float u_useTex;
uniform vec3 u_lightDir;
uniform float u_scene;
uniform float u_ambient;
uniform float u_diffuse;
uniform vec4 u_hl;
varying vec3 v_nrm;
varying vec2 v_uv;
varying vec4 v_col;
varying float v_sel;
void main() {
    vec4 base = v_col;
    if (u_useTex > 0.5) {
        vec4 t = texture2D(u_tex, v_uv);
        base = vec4(t.rgb, t.a * v_col.a);
    }
    // Fixed-function light of the original: scene ambient + light ambient + diffuse * n.l.
    float l = max(dot(normalize(v_nrm), u_lightDir), 0.0);
    float shade = min(u_scene + u_ambient + u_diffuse * l, 1.0);
    vec3 c = base.rgb * shade;
    if (v_sel > 0.5) {
        vec2 g = mod(floor(gl_FragCoord.xy), 3.0);
        if (g.x < 1.0 && g.y < 1.0) c = u_hl.rgb;
    }
    gl_FragColor = vec4(c, base.a);
}
)";

const char* const kLineVs = R"(#version 120
attribute vec3 a_p0;
attribute vec3 a_p1;
attribute vec2 a_side;
attribute vec4 a_col;
attribute vec3 a_wde;
uniform mat4 u_mvp;
uniform vec2 u_vp;
varying vec4 v_col;
varying vec2 v_start;
varying float v_dash;
void main() {
    vec4 c0 = u_mvp * vec4(a_p0, 1.0);
    vec4 c1 = u_mvp * vec4(a_p1, 1.0);
    const float e = 1e-5;
    if (c0.w < e && c1.w < e) {
        gl_Position = vec4(0.0, 0.0, -2.0, 1.0);
        v_col = a_col; v_start = vec2(0.0); v_dash = 0.0;
        return;
    }
    if (c0.w < e) c0 = mix(c0, c1, (e - c0.w) / (c1.w - c0.w));
    if (c1.w < e) c1 = mix(c1, c0, (e - c1.w) / (c0.w - c1.w));
    vec2 s0 = c0.xy / c0.w * 0.5 * u_vp;
    vec2 s1 = c1.xy / c1.w * 0.5 * u_vp;
    vec2 d = s1 - s0;
    float len = length(d);
    vec2 dir = len > 1e-6 ? d / len : vec2(1.0, 0.0);
    vec2 nrm = vec2(-dir.y, dir.x);
    bool far = a_side.x > 0.5;
    vec4 c = far ? c1 : c0;
    vec2 s = (far ? s1 + dir * a_wde.z : s0 - dir * a_wde.z) + nrm * a_side.y * a_wde.x * 0.5;
    gl_Position = vec4(s / (0.5 * u_vp) * c.w, c.z, c.w);
    // Dashes are measured on screen from the (clipped) start point, in window pixels.
    v_start = s0 - dir * a_wde.z + 0.5 * u_vp;
    v_dash = a_wde.y;
    v_col = a_col;
}
)";

const char* const kLineFs = R"(#version 120
varying vec4 v_col;
varying vec2 v_start;
varying float v_dash;
void main() {
    if (v_dash > 0.5 && mod(length(gl_FragCoord.xy - v_start), 8.0) > 4.0) discard;
    gl_FragColor = v_col;
}
)";

const char* const kSkyVs = R"(#version 120
attribute vec2 a_ndc;
varying vec2 v_ndc;
void main() {
    v_ndc = a_ndc;
    gl_Position = vec4(a_ndc, 0.0, 1.0);
}
)";

const char* const kSkyFs = R"(#version 120
uniform vec3 u_zc;    // world up (0,0,1) in camera coordinates
uniform vec2 u_tan;   // half view size at depth 1 (perspective) or in model units (parallel)
uniform float u_persp;
uniform vec3 u_sky;
uniform vec3 u_horizon;
uniform vec3 u_bg;
uniform vec3 u_ground;
uniform float u_drawSky;
uniform float u_drawGround;
uniform float u_groundAlpha;
varying vec2 v_ndc;
void main() {
    // Height above the horizon of this pixel's view ray (perspective) or screen point (parallel).
    float zz = u_persp > 0.5 ? u_zc.z : 0.0;
    float up = u_zc.x * v_ndc.x * u_tan.x + u_zc.y * v_ndc.y * u_tan.y - zz;
    vec3 c = u_bg;
    if (up >= 0.0) {
        if (u_drawSky > 0.5) {
            // GroundSkyHelper::DrawHorizon: one quad shaded from the horizon colour at the horizon line
            // to the sky colour at the top of the view (linear on screen).
            float t = 1.0;
            if (abs(u_zc.y) > 1e-6) {
                float h = (zz - u_zc.x * v_ndc.x * u_tan.x) / (u_zc.y * u_tan.y);
                if (h < 1.0) t = clamp((v_ndc.y - h) / (1.0 - h), 0.0, 1.0);
            }
            c = mix(u_horizon, u_sky, t);
        }
    } else if (u_drawGround > 0.5) {
        c = mix(u_ground, u_bg, u_groundAlpha);
    }
    gl_FragColor = vec4(c, 1.0);
}
)";

QMatrix4x4 toQt(const M4& m) {
    QMatrix4x4 q;
    float* d = q.data();
    for (std::size_t i = 0; i < 16; ++i) d[i] = static_cast<float>(m.m[i]);
    return q;
}

QVector3D q3(const V3& v) {
    return QVector3D(static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z));
}

QVector3D rgb(const QColor& c) {
    return QVector3D(static_cast<float>(c.redF()), static_cast<float>(c.greenF()), static_cast<float>(c.blueF()));
}

std::optional<double> rayTriangle(const Ray& r, const V3& a, const V3& b, const V3& c) {
    const V3 e1 = b - a, e2 = c - a;
    const V3 p = r.dir.cross(e2);
    const double det = e1.dot(p);
    if (std::abs(det) < 1e-14) return std::nullopt;
    const double inv = 1.0 / det;
    const V3 t = r.origin - a;
    const double u = t.dot(p) * inv;
    if (u < 0 || u > 1) return std::nullopt;
    const V3 q = t.cross(e1);
    const double v = r.dir.dot(q) * inv;
    if (v < 0 || u + v > 1) return std::nullopt;
    const double dist = e2.dot(q) * inv;
    if (dist <= 0) return std::nullopt;
    return dist;
}

double segmentPixelDistance(QPointF p, QPointF a, QPointF b, double& t) {
    const QPointF ab = b - a;
    const double l2 = QPointF::dotProduct(ab, ab);
    t = l2 > 1e-12 ? std::clamp(QPointF::dotProduct(p - a, ab) / l2, 0.0, 1.0) : 0.0;
    const QPointF c = a + ab * t;
    return std::hypot(p.x() - c.x(), p.y() - c.y());
}

}  // namespace

Viewport::Viewport(QWidget* parent) : QOpenGLWidget(parent) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(320, 240);
    anim_ = std::make_unique<QVariantAnimation>();
    anim_->setDuration(450);
    anim_->setStartValue(0.0);
    anim_->setEndValue(1.0);
    anim_->setEasingCurve(QEasingCurve::InOutCubic);
    connect(anim_.get(), &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        const double t = v.toDouble();
        View w = animTo_;
        w.eye = animFrom_.eye + (animTo_.eye - animFrom_.eye) * t;
        w.target = animFrom_.target + (animTo_.target - animFrom_.target) * t;
        w.up = (animFrom_.up + (animTo_.up - animFrom_.up) * t).normalized();
        if (w.up.length() < 0.5) w.up = animTo_.up;
        w.fovDeg = animFrom_.fovDeg + (animTo_.fovDeg - animFrom_.fovDeg) * t;
        w.orthoHeight = animFrom_.orthoHeight + (animTo_.orthoHeight - animFrom_.orthoHeight) * t;
        view_ = w;
        update();
        emit viewChanged();
    });
}

Viewport::~Viewport() {
    anim_->stop();
    if (glReady_) {
        makeCurrent();
        destroyGpu();
        faceProg_.reset();
        lineProg_.reset();
        skyProg_.reset();
        lineVbo_.reset();
        whiteTex_.reset();
        doneCurrent();
    }
}

void Viewport::destroyGpu() {
    gpu_.clear();
    textures_.clear();
}

void Viewport::setScene(Scene scene, const Style& style) {
    scene_ = std::move(scene);
    style_ = style;
    sceneDirty_ = true;
    update();
}

void Viewport::setStyle(const Style& style) {
    style_ = style;
    update();
}

void Viewport::setOverlay(Overlay o) {
    overlay_ = std::move(o);
    update();
}

void Viewport::setView(const View& v, bool animate) {
    if (animate && isVisible()) {
        anim_->stop();
        animFrom_ = view_;
        animTo_ = v;
        anim_->start();
        return;
    }
    anim_->stop();
    view_ = v;
    update();
    emit viewChanged();
}

V3 Viewport::eyeDirection() const {
    return (view_.target - view_.eye).normalized();
}

M4 Viewport::viewMatrix() const {
    const V3 f = eyeDirection();
    V3 up = view_.up;
    if (std::abs(f.dot(up.normalized())) > 0.999) up = std::abs(f.z) > 0.9 ? V3{0, 1, 0} : V3{0, 0, 1};
    const V3 s = f.cross(up).normalized();
    const V3 u = s.cross(f);
    M4 m;
    m.at(0, 0) = s.x;
    m.at(0, 1) = s.y;
    m.at(0, 2) = s.z;
    m.at(1, 0) = u.x;
    m.at(1, 1) = u.y;
    m.at(1, 2) = u.z;
    m.at(2, 0) = -f.x;
    m.at(2, 1) = -f.y;
    m.at(2, 2) = -f.z;
    m.at(0, 3) = -s.dot(view_.eye);
    m.at(1, 3) = -u.dot(view_.eye);
    m.at(2, 3) = f.dot(view_.eye);
    return m;
}

void Viewport::nearFar(double& n, double& f) const {
    const V3 fwd = eyeDirection();
    const double dist = (view_.target - view_.eye).length();
    double r = scene_.bounds.empty() ? 500.0 : scene_.bounds.radius();
    const V3 c = scene_.bounds.empty() ? view_.target : scene_.bounds.center();
    const double d = (c - view_.eye).dot(fwd);
    // Room for the axes, drawn out to three times the view distance.
    const double axes = 3.0 * (dist + r) + view_.eye.length();
    f = std::max({d + r * 1.5, axes, 100.0});
    n = std::max({d - r * 1.5, f * 2e-5, 0.05});
    if (!view_.perspective) n = -f;
}

M4 Viewport::projMatrix() const {
    const double w = std::max(1, width()), h = std::max(1, height());
    const double aspect = w / h;
    double n = 1, f = 1000;
    nearFar(n, f);
    M4 p;
    if (view_.perspective) {
        const double t = 1.0 / std::tan(view_.fovDeg * kDeg / 2.0);
        p.at(0, 0) = t / aspect;
        p.at(1, 1) = t;
        p.at(2, 2) = -(f + n) / (f - n);
        p.at(2, 3) = -2.0 * f * n / (f - n);
        p.at(3, 2) = -1.0;
        p.at(3, 3) = 0.0;
    } else {
        const double hh = view_.orthoHeight / 2.0, hw = hh * aspect;
        p.at(0, 0) = 1.0 / hw;
        p.at(1, 1) = 1.0 / hh;
        p.at(2, 2) = -2.0 / (f - n);
        p.at(2, 3) = -(f + n) / (f - n);
    }
    return p;
}

Ray Viewport::rayAt(QPointF pos) const {
    const double w = std::max(1, width()), h = std::max(1, height());
    const double nx = 2.0 * pos.x() / w - 1.0, ny = 1.0 - 2.0 * pos.y() / h;
    const auto inv = viewProj().inverse();
    if (!inv) return {view_.eye, eyeDirection()};
    // A general 4x4 inverse is needed for the projection; do it through Qt.
    const QMatrix4x4 ivp = toQt(viewProj()).inverted();
    const QVector4D a = ivp * QVector4D(static_cast<float>(nx), static_cast<float>(ny), -1.0f, 1.0f);
    const QVector4D b = ivp * QVector4D(static_cast<float>(nx), static_cast<float>(ny), 1.0f, 1.0f);
    const V3 pa{a.x() / a.w(), a.y() / a.w(), a.z() / a.w()};
    const V3 pb{b.x() / b.w(), b.y() / b.w(), b.z() / b.w()};
    if (view_.perspective) return {view_.eye, (pb - view_.eye).normalized()};
    return {pa, (pb - pa).normalized()};
}

std::optional<QPointF> Viewport::project(const V3& p) const {
    const M4 vp = viewProj();
    const double x = vp.at(0, 0) * p.x + vp.at(0, 1) * p.y + vp.at(0, 2) * p.z + vp.at(0, 3);
    const double y = vp.at(1, 0) * p.x + vp.at(1, 1) * p.y + vp.at(1, 2) * p.z + vp.at(1, 3);
    const double w = vp.at(3, 0) * p.x + vp.at(3, 1) * p.y + vp.at(3, 2) * p.z + vp.at(3, 3);
    if (w <= 1e-9) return std::nullopt;
    return QPointF((x / w + 1.0) * 0.5 * width(), (1.0 - y / w) * 0.5 * height());
}

double Viewport::pixelSize(const V3& at) const {
    const double h = std::max(1, height());
    if (!view_.perspective) return view_.orthoHeight / h;
    const double d = std::max(1e-3, (at - view_.eye).dot(eyeDirection()));
    return 2.0 * d * std::tan(view_.fovDeg * kDeg / 2.0) / h;
}

std::optional<Hit> Viewport::pick(QPointF pos, bool edges) const {
    const Ray r = rayAt(pos);
    std::optional<Hit> best;
    double bestT = 1e300;
    for (const PickTri& t : scene_.tris) {
        if (auto d = rayTriangle(r, t.a, t.b, t.c); d && *d < bestT) {
            bestT = *d;
            Hit h;
            h.pick = t.pick;
            h.point = r.origin + r.dir * *d;
            h.normal = (t.b - t.a).cross(t.c - t.a).normalized();
            best = h;
        }
    }
    if (edges) {
        double bestPx = 5.0;
        for (const SceneEdge& e : scene_.edges) {
            const auto pa = project(e.a), pb = project(e.b);
            if (!pa || !pb) continue;
            double t = 0;
            const double px = segmentPixelDistance(pos, *pa, *pb, t);
            if (px > bestPx) continue;
            const V3 p = e.a + (e.b - e.a) * t;
            const double along = (p - r.origin).dot(r.dir);
            if (along > bestT + pixelSize(p) * 4) continue;  // hidden behind a face
            bestPx = px;
            Hit h;
            h.pick = e.pick;
            h.point = p;
            h.isEdge = true;
            h.edgeA = e.a;
            h.edgeB = e.b;
            if (best && !best->isEdge) h.normal = best->normal;
            best = h;
        }
    }
    return best;
}

std::vector<skp::ObjId> Viewport::pickBox(const QRectF& box, bool crossing) const {
    std::vector<skp::ObjId> out;
    for (const auto& [id, b] : scene_.topBoxes) {
        if (b.empty()) continue;
        QRectF r;
        bool any = false;
        for (int i = 0; i < 8; ++i) {
            const V3 c{(i & 1) ? b.hi.x : b.lo.x, (i & 2) ? b.hi.y : b.lo.y, (i & 4) ? b.hi.z : b.lo.z};
            const auto p = project(c);
            if (!p) continue;
            const QRectF pr(*p, QSizeF(0.01, 0.01));
            r = any ? r.united(pr) : pr;
            any = true;
        }
        if (!any) continue;
        if (crossing ? box.intersects(r) : box.contains(r)) out.push_back(id);
    }
    return out;
}

std::optional<V3> Viewport::snapVertex(QPointF pos, double radius) const {
    std::optional<V3> best;
    double bestPx = radius;
    double bestDepth = 1e300;
    const V3 fwd = eyeDirection();
    for (const SceneEdge& e : scene_.edges) {
        for (const V3* p : {&e.a, &e.b}) {
            const auto s = project(*p);
            if (!s) continue;
            const double px = std::hypot(s->x() - pos.x(), s->y() - pos.y());
            const double depth = (*p - view_.eye).dot(fwd);
            if (px < bestPx - 0.5 || (px <= bestPx + 0.5 && depth < bestDepth)) {
                bestPx = px;
                bestDepth = depth;
                best = *p;
            }
        }
    }
    return best;
}

// ------------------------------------------------------------------ navigation

void Viewport::beginOrbit(QPointF at) {
    // CViewCenterFinder::GetOrbitCenter (0x100165248): the model point under the cursor, else under the
    // view centre, gives a depth; the pivot is that far straight ahead of the eye. With nothing to pick,
    // the camera target.
    const V3 f = eyeDirection();
    std::optional<double> depth;
    for (QPointF p : {at, QPointF(width() / 2.0, height() / 2.0)}) {
        if (auto h = pick(p, false)) {
            const double d = (h->point - view_.eye).dot(f);
            if (d > 0) {
                depth = d;
                break;
            }
        }
    }
    if (!depth && !scene_.bounds.empty()) {
        const double d = (scene_.bounds.center() - view_.eye).dot(f);
        if (d > 0) depth = d;
    }
    orbitCenter_ = depth ? view_.eye + f * *depth : view_.target;
}

namespace {
// GraphicUtils::ComputeUp (0x1002acc80): the world vertical made perpendicular to the view, flipped to
// agree with the previous up; looking straight up or down, the previous up made perpendicular instead.
V3 computeUp(const V3& eye, const V3& target, const V3& hint) {
    const V3 f = (target - eye).normalized();
    const V3 side = V3{0, 0, 1}.cross(f);
    V3 up;
    if (side.length() > 1e-6) {
        up = f.cross(side).normalized();
        if (up.dot(hint) < 0) up = -up;
        return up;
    }
    const V3 s2 = hint.cross(f);
    if (s2.length() > 1e-6) return f.cross(s2).normalized();
    return f.cross(V3{0, 1, 0}.cross(f)).normalized();
}
}  // namespace

void Viewport::orbitBy(double dx, double dy) {
    anim_->stop();
    // CCameraTool::DoOrbit2 (0x10012165c), per mouse move:
    //   back = normalize(eye - target), right = up × back, camUp = back × right   (ComputeParameters 0x10031e1ea)
    //   s = |back × Z|, h = dx·s, v = -dy (screen y grows downward)
    //   axis = v·right − h·camUp, angle = sqrt(v² + h²) / view height · π · 0.5
    // then eye and target turn about the pivot and the camera is levelled again (ComputeUp).
    const V3 back = (view_.eye - view_.target).normalized();
    const V3 right = view_.up.cross(back).normalized();
    const V3 camUp = back.cross(right).normalized();
    const double s = back.cross(V3{0, 0, 1}).length();
    const double h = dx * s;
    const double v = -dy;
    const V3 axis = right * v - camUp * h;
    const double move = std::sqrt(v * v + h * h);
    if (axis.length() < 1e-12 || move < 1e-12) return;
    const double angle = move / std::max(1, height()) * std::numbers::pi * kOrbitFactor;
    const V3 pivot = orbitCenter_.value_or(view_.target);
    const M4 r = M4::rotate(pivot, axis.normalized(), angle);
    view_.eye = r.point(view_.eye);
    view_.target = r.point(view_.target);
    view_.up = computeUp(view_.eye, view_.target, camUp);
    update();
    emit viewChanged();
}

void Viewport::panBy(double dx, double dy) {
    anim_->stop();
    const V3 f = eyeDirection();
    const V3 right = f.cross(view_.up).normalized();
    const V3 up = right.cross(f);
    const double s = pixelSize(view_.target);
    const V3 d = right * (-dx * s) + up * (dy * s);
    view_.eye += d;
    view_.target += d;
    update();
    emit viewChanged();
}

void Viewport::zoomAt(QPointF at, double factor) {
    anim_->stop();
    V3 p;
    if (auto h = pick(at, false)) {
        p = h->point;
    } else {
        const Ray r = rayAt(at);
        const geom::Plane pl = geom::Plane::fromPointNormal(view_.target, eyeDirection());
        const auto t = pl.ray(r.origin, r.dir);
        p = t ? r.origin + r.dir * *t : view_.target;
    }
    if (view_.perspective) {
        const double dist = (p - view_.eye).length();
        if (factor < 1 && dist < 0.5) return;
        view_.eye = p + (view_.eye - p) * factor;
        view_.target = p + (view_.target - p) * factor;
    } else {
        const V3 f = eyeDirection();
        auto lateral = [&](const V3& q) { return q - f * (q - p).dot(f); };
        const V3 le = lateral(view_.eye);
        const V3 shift = (p + (le - p) * factor) - le;
        view_.eye += shift;
        view_.target += shift;
        view_.orthoHeight = std::max(1e-3, view_.orthoHeight * factor);
    }
    update();
    emit viewChanged();
}

void Viewport::zoomWheel(QPointF at, double notches) {
    // CSketchView::DoMouseWheel (0x10014df6a) turns a wheel step into amount = step / 10;
    // CSketchView::ZoomTowardsPoint (0x10014c032) / CViewDefinition::ZoomTowardsPoint (0x10031fd78)
    // move eye and target along the ray under the cursor by depth-of-picked-point * amount
    // (perspective), or scale the parallel view height by (1 - amount).
    const double amount = notches / kWheelDivisor;
    if (!view_.perspective) {
        zoomAt(at, std::max(0.05, 1.0 - amount));
        return;
    }
    anim_->stop();
    const Ray r = rayAt(at);
    V3 p;
    if (auto h = pick(at, false)) {
        p = h->point;
    } else {
        const geom::Plane pl = geom::Plane::fromPointNormal(view_.target, eyeDirection());
        const auto t = pl.ray(r.origin, r.dir);
        p = t ? r.origin + r.dir * *t : view_.target;
    }
    const double depth = std::max(kMinZoomDepth, (p - view_.eye).dot(eyeDirection()));
    const V3 shift = r.dir * (depth * amount);
    view_.eye += shift;
    view_.target += shift;
    update();
    emit viewChanged();
}

void Viewport::zoomFov(double delta) {
    view_.fovDeg = std::clamp(view_.fovDeg + delta, 1.0, 120.0);
    update();
    emit viewChanged();
}

void Viewport::setPerspective(bool on) {
    if (view_.perspective == on) return;
    const double dist = (view_.target - view_.eye).length();
    if (!on) {
        // CCamera::SetPerspective 0x63144: parallel height = 2 tan(fov / 2) * distance.
        view_.orthoHeight = 2.0 * std::tan(view_.fovDeg * kDeg / 2.0) * dist;
    }
    view_.perspective = on;
    update();
    emit viewChanged();
}

void Viewport::zoomExtents(bool animate) {
    if (scene_.bounds.empty()) return;
    View v = view_;
    const V3 c = scene_.bounds.center();
    const double r = std::max(1.0, scene_.bounds.radius());
    const V3 f = eyeDirection();
    const double aspect = std::max(1, width()) / static_cast<double>(std::max(1, height()));
    const double halfFov = view_.fovDeg * kDeg / 2.0;
    const double fit = std::min(halfFov, std::atan(std::tan(halfFov) * aspect));
    const double dist = r / std::sin(fit) * 1.02;
    v.target = c;
    v.eye = c - f * dist;
    v.orthoHeight = 2.0 * r * 1.05 / std::min(1.0, aspect);
    setView(v, animate);
}

void Viewport::standardView(StandardView sv) {
    View v = view_;
    V3 dir{1, -1, 1};
    V3 up{0, 0, 1};
    switch (sv) {
        case StandardView::Iso: dir = {1, -1, 1}; break;
        case StandardView::Top: dir = {0, 0, 1}; up = {0, 1, 0}; break;
        case StandardView::Bottom: dir = {0, 0, -1}; up = {0, 1, 0}; break;
        case StandardView::Front: dir = {0, -1, 0}; break;
        case StandardView::Back: dir = {0, 1, 0}; break;
        case StandardView::Left: dir = {-1, 0, 0}; break;
        case StandardView::Right: dir = {1, 0, 0}; break;
    }
    const V3 c = scene_.bounds.empty() ? V3{0, 0, 0} : scene_.bounds.center();
    const double dist = std::max(100.0, (view_.eye - view_.target).length());
    v.target = c;
    v.eye = c + dir.normalized() * dist;
    const V3 f = (v.target - v.eye).normalized();
    const V3 right = f.cross(up).normalized();
    v.up = right.cross(f).normalized();
    animFrom_ = view_;
    view_ = v;
    // Fit the model in the new direction, then animate there.
    const View fitted = [&] {
        const View keep = view_;
        zoomExtents(false);
        const View out = view_;
        view_ = keep;
        return out;
    }();
    view_ = animFrom_;
    setView(scene_.bounds.empty() ? v : fitted, true);
}

// ------------------------------------------------------------------ input

void Viewport::mousePressEvent(QMouseEvent* e) {
    setFocus();
    if (e->button() == Qt::MiddleButton) {
        drag_ = (e->modifiers() & Qt::ShiftModifier) ? Drag::Pan : Drag::Orbit;
        lastPos_ = e->position();
        if (drag_ == Drag::Orbit) beginOrbit(e->position());
        return;
    }
    if (handler_) handler_->press(e);
}

void Viewport::mouseMoveEvent(QMouseEvent* e) {
    if (drag_ != Drag::None) {
        const QPointF d = e->position() - lastPos_;
        lastPos_ = e->position();
        if (drag_ == Drag::Orbit) orbitBy(d.x(), d.y()); else panBy(d.x(), d.y());
        return;
    }
    if (handler_) handler_->move(e);
}

void Viewport::mouseReleaseEvent(QMouseEvent* e) {
    if (drag_ != Drag::None && e->button() == Qt::MiddleButton) {
        drag_ = Drag::None;
        endOrbit();
        return;
    }
    if (handler_) handler_->release(e);
}

void Viewport::mouseDoubleClickEvent(QMouseEvent* e) {
    if (e->button() == Qt::MiddleButton) {
        // Double-clicking the wheel centres the view on that point.
        if (auto h = pick(e->position(), false)) {
            View v = view_;
            const V3 d = h->point - v.target;
            v.target += d;
            v.eye += d;
            setView(v, true);
        }
        return;
    }
    if (handler_) handler_->doubleClick(e);
}

void Viewport::wheelEvent(QWheelEvent* e) {
    const double steps = e->angleDelta().y() / 120.0;
    if (std::abs(steps) < 1e-6) return;
    zoomWheel(e->position(), steps);
}

void Viewport::keyPressEvent(QKeyEvent* e) {
    if (handler_ && handler_->key(e)) return;
    QOpenGLWidget::keyPressEvent(e);
}

// ------------------------------------------------------------------ drawing

void Viewport::initializeGL() {
    initializeOpenGLFunctions();
    auto make = [this](const char* vs, const char* fs, const char* what) {
        auto p = std::make_unique<QOpenGLShaderProgram>();
        bool ok = p->addShaderFromSourceCode(QOpenGLShader::Vertex, vs);
        ok = ok && p->addShaderFromSourceCode(QOpenGLShader::Fragment, fs);
        ok = ok && p->link();
        if (!ok) qWarning("shader %s: %s", what, qPrintable(p->log()));
        (void)this;
        return p;
    };
    faceProg_ = make(kFaceVs, kFaceFs, "faces");
    lineProg_ = make(kLineVs, kLineFs, "lines");
    skyProg_ = make(kSkyVs, kSkyFs, "sky");
    lineVbo_ = std::make_unique<QOpenGLBuffer>(QOpenGLBuffer::VertexBuffer);
    lineVbo_->create();
    lineVbo_->setUsagePattern(QOpenGLBuffer::StreamDraw);
    QImage white(1, 1, QImage::Format_RGBA8888);
    white.fill(Qt::white);
    whiteTex_ = std::make_unique<QOpenGLTexture>(white);
    glReady_ = true;
    sceneDirty_ = true;
}

void Viewport::resizeGL(int, int) {}

void Viewport::uploadScene() {
    destroyGpu();
    for (const QImage& img : scene_.textures) {
        auto t = std::make_unique<QOpenGLTexture>(img.convertToFormat(QImage::Format_RGBA8888).flipped(Qt::Vertical));
        t->setMinificationFilter(QOpenGLTexture::LinearMipMapLinear);
        t->setMagnificationFilter(QOpenGLTexture::Linear);
        t->setWrapMode(QOpenGLTexture::Repeat);
        textures_.push_back(std::move(t));
    }
    for (const Batch& b : scene_.batches) {
        if (b.data.empty()) continue;
        GpuBatch g;
        g.vbo = std::make_unique<QOpenGLBuffer>(QOpenGLBuffer::VertexBuffer);
        g.vbo->create();
        g.vbo->bind();
        g.vbo->allocate(b.data.data(), static_cast<int>(b.data.size() * sizeof(float)));
        g.vbo->release();
        g.count = static_cast<int>(b.vertexCount());
        g.texture = b.texture;
        g.transparent = b.transparent;
        gpu_.push_back(std::move(g));
    }
    sceneDirty_ = false;
}

void Viewport::drawBackground() {
    if (!skyProg_ || !skyProg_->isLinked()) return;
    const std::array<float, 8> quad{-1, -1, 1, -1, -1, 1, 1, 1};
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    skyProg_->bind();
    skyProg_->setUniformValue("u_zc", q3(viewMatrix().vector(V3{0, 0, 1})));
    const double aspect = std::max(1, width()) / static_cast<double>(std::max(1, height()));
    const double ty = view_.perspective ? std::tan(view_.fovDeg * kDeg / 2.0) : view_.orthoHeight / 2.0;
    skyProg_->setUniformValue("u_tan", QVector2D(static_cast<float>(ty * aspect), static_cast<float>(ty)));
    skyProg_->setUniformValue("u_persp", view_.perspective ? 1.0f : 0.0f);
    skyProg_->setUniformValue("u_sky", rgb(style_.sky));
    // CRenderingOptions::GetHorizonColorForDisplay (0x1002db06e): with no stored horizon colour it is
    // CColor::Blend(sky, white, 0.25) = 0.25 sky + 0.75 white (libCommonUtils 0x5f62).
    QColor horizon = style_.horizon;
    if (!horizon.isValid() || horizon.alpha() == 0) {
        constexpr float kSkyShare = 0.25f;
        horizon = QColor::fromRgbF(style_.sky.redF() * kSkyShare + (1 - kSkyShare),
                                   style_.sky.greenF() * kSkyShare + (1 - kSkyShare),
                                   style_.sky.blueF() * kSkyShare + (1 - kSkyShare));
    }
    skyProg_->setUniformValue("u_horizon", rgb(horizon));
    skyProg_->setUniformValue("u_bg", rgb(style_.background));
    skyProg_->setUniformValue("u_ground", rgb(style_.ground));
    skyProg_->setUniformValue("u_drawSky", style_.drawSky ? 1.0f : 0.0f);
    skyProg_->setUniformValue("u_drawGround", style_.drawGround ? 1.0f : 0.0f);
    skyProg_->setUniformValue("u_groundAlpha", static_cast<float>(style_.groundTransparency) / 100.0f);
    const int loc = skyProg_->attributeLocation("a_ndc");
    lineVbo_->bind();
    lineVbo_->allocate(quad.data(), static_cast<int>(quad.size() * sizeof(float)));
    skyProg_->enableAttributeArray(loc);
    skyProg_->setAttributeBuffer(loc, GL_FLOAT, 0, 2, 2 * sizeof(float));
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    skyProg_->disableAttributeArray(loc);
    lineVbo_->release();
    skyProg_->release();
    glDepthMask(GL_TRUE);
}

void Viewport::drawFaces(bool transparent) {
    if (!faceProg_ || !faceProg_->isLinked()) return;
    faceProg_->bind();
    faceProg_->setUniformValue("u_mvp", toQt(viewProj()));
    const Style::Lighting light = style_.lighting(-eyeDirection());
    faceProg_->setUniformValue("u_lightDir", q3(light.dir));
    faceProg_->setUniformValue("u_scene", static_cast<float>(light.scene));
    faceProg_->setUniformValue("u_ambient", static_cast<float>(light.ambient));
    faceProg_->setUniformValue("u_diffuse", static_cast<float>(light.diffuse));
    faceProg_->setUniformValue("u_hl", QVector4D(rgb(style_.highlight), 1.0f));
    faceProg_->setUniformValue("u_tex", 0);
    const int aPos = faceProg_->attributeLocation("a_pos");
    const int aNrm = faceProg_->attributeLocation("a_nrm");
    const int aUv = faceProg_->attributeLocation("a_uv");
    const int aCol = faceProg_->attributeLocation("a_col");
    const int aSel = faceProg_->attributeLocation("a_sel");
    const int stride = kVertexFloats * static_cast<int>(sizeof(float));
    for (GpuBatch& g : gpu_) {
        if (g.transparent != transparent || g.count == 0) continue;
        const bool tex = g.texture >= 0 && static_cast<std::size_t>(g.texture) < textures_.size();
        faceProg_->setUniformValue("u_useTex", tex ? 1.0f : 0.0f);
        if (tex) textures_[static_cast<std::size_t>(g.texture)]->bind(0); else if (whiteTex_) whiteTex_->bind(0);
        g.vbo->bind();
        faceProg_->enableAttributeArray(aPos);
        faceProg_->enableAttributeArray(aNrm);
        faceProg_->enableAttributeArray(aUv);
        faceProg_->enableAttributeArray(aCol);
        faceProg_->enableAttributeArray(aSel);
        faceProg_->setAttributeBuffer(aPos, GL_FLOAT, 0, 3, stride);
        faceProg_->setAttributeBuffer(aNrm, GL_FLOAT, 3 * sizeof(float), 3, stride);
        faceProg_->setAttributeBuffer(aUv, GL_FLOAT, 6 * sizeof(float), 2, stride);
        faceProg_->setAttributeBuffer(aCol, GL_FLOAT, 8 * sizeof(float), 4, stride);
        faceProg_->setAttributeBuffer(aSel, GL_FLOAT, 12 * sizeof(float), 1, stride);
        glDrawArrays(GL_TRIANGLES, 0, g.count);
        faceProg_->disableAttributeArray(aPos);
        faceProg_->disableAttributeArray(aNrm);
        faceProg_->disableAttributeArray(aUv);
        faceProg_->disableAttributeArray(aCol);
        faceProg_->disableAttributeArray(aSel);
        g.vbo->release();
        if (tex) textures_[static_cast<std::size_t>(g.texture)]->release(0);
    }
    faceProg_->release();
}

void Viewport::addLine(std::vector<float>& out, const V3& aIn, const V3& bIn, const QColor& c, float width,
                       bool dashed) const {
    V3 a = aIn, b = bIn;
    if (dashed && view_.perspective) {
        // Keep dashed lines in front of the near plane so their on-screen dash spacing stays exact.
        double n = 1, f = 1000;
        nearFar(n, f);
        const V3 fwd = eyeDirection();
        const double da = (a - view_.eye).dot(fwd), db = (b - view_.eye).dot(fwd);
        if (da < n && db < n) return;
        if (da < n) a = a + (b - a) * ((n - da) / (db - da));
        if (db < n) b = b + (a - b) * ((n - db) / (da - db));
    }
    const float dpr = static_cast<float>(devicePixelRatioF());
    const float ext = style_.extensions ? static_cast<float>(style_.extensionLength) * dpr : 0.0f;
    const std::array<std::array<float, 2>, 6> corners{{{0, -1}, {1, -1}, {1, 1}, {0, -1}, {1, 1}, {0, 1}}};
    for (const auto& s : corners) {
        const std::array<float, kLineFloats> v{
            static_cast<float>(a.x), static_cast<float>(a.y), static_cast<float>(a.z), static_cast<float>(b.x),
            static_cast<float>(b.y), static_cast<float>(b.z), s[0], s[1], static_cast<float>(c.redF()),
            static_cast<float>(c.greenF()), static_cast<float>(c.blueF()), static_cast<float>(c.alphaF()),
            std::max(1.0f, width * dpr), dashed ? 1.0f : 0.0f, ext};
        out.insert(out.end(), v.begin(), v.end());
    }
}

void Viewport::buildEdgeLines(std::vector<float>& solid, std::vector<float>& back) const {
    const V3 fwd = eyeDirection();
    for (const SceneEdge& e : scene_.edges) {
        const V3 mid = (e.a + e.b) * 0.5;
        const V3 look = view_.perspective ? mid - view_.eye : fwd;
        bool profile = false;
        if (style_.profiles && style_.renderMode != kWireframe) {
            if (e.faces == 1) {
                profile = true;
            } else if (e.faces == 2) {
                const double s1 = e.n1.dot(look), s2 = e.n2.dot(look);
                profile = (s1 < 0) != (s2 < 0);
            }
        }
        if (!style_.edges && !profile && !e.selected) continue;
        const QColor col = e.selected ? style_.highlight : style_.edge;
        const float w = profile ? static_cast<float>(std::max<std::uint32_t>(1, style_.profileWidth)) : 1.0f;
        addLine(solid, e.a, e.b, col, e.selected ? std::max(w, 2.0f) : w, false);
        if (style_.backEdges) addLine(back, e.a, e.b, col, 1.0f, true);
    }
    // Selected groups and components get their bounding box.
    for (skp::ObjId id : scene_.selectedInstances) {
        const auto it = scene_.topBoxes.find(id);
        if (it == scene_.topBoxes.end() || it->second.empty()) continue;
        const geom::Box& b = it->second;
        const std::array<V3, 8> c{V3{b.lo.x, b.lo.y, b.lo.z}, V3{b.hi.x, b.lo.y, b.lo.z}, V3{b.hi.x, b.hi.y, b.lo.z},
                                  V3{b.lo.x, b.hi.y, b.lo.z}, V3{b.lo.x, b.lo.y, b.hi.z}, V3{b.hi.x, b.lo.y, b.hi.z},
                                  V3{b.hi.x, b.hi.y, b.hi.z}, V3{b.lo.x, b.hi.y, b.hi.z}};
        const std::array<std::array<int, 2>, 12> ed{
            {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}}};
        for (const auto& p : ed) {
            addLine(solid, c[static_cast<std::size_t>(p[0])], c[static_cast<std::size_t>(p[1])], style_.highlight,
                    1.0f, false);
        }
    }
}

void Viewport::drawLines(const std::vector<float>& data, bool depthTest) {
    if (data.empty() || !lineProg_ || !lineProg_->isLinked()) return;
    if (depthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    lineProg_->bind();
    lineProg_->setUniformValue("u_mvp", toQt(viewProj()));
    const float dpr = static_cast<float>(devicePixelRatioF());
    lineProg_->setUniformValue("u_vp", QVector2D(static_cast<float>(width()) * dpr, static_cast<float>(height()) * dpr));
    const int aP0 = lineProg_->attributeLocation("a_p0");
    const int aP1 = lineProg_->attributeLocation("a_p1");
    const int aSide = lineProg_->attributeLocation("a_side");
    const int aCol = lineProg_->attributeLocation("a_col");
    const int aWde = lineProg_->attributeLocation("a_wde");
    const int stride = kLineFloats * static_cast<int>(sizeof(float));
    lineVbo_->bind();
    lineVbo_->allocate(data.data(), static_cast<int>(data.size() * sizeof(float)));
    for (int a : {aP0, aP1, aSide, aCol, aWde}) lineProg_->enableAttributeArray(a);
    lineProg_->setAttributeBuffer(aP0, GL_FLOAT, 0, 3, stride);
    lineProg_->setAttributeBuffer(aP1, GL_FLOAT, 3 * sizeof(float), 3, stride);
    lineProg_->setAttributeBuffer(aSide, GL_FLOAT, 6 * sizeof(float), 2, stride);
    lineProg_->setAttributeBuffer(aCol, GL_FLOAT, 8 * sizeof(float), 4, stride);
    lineProg_->setAttributeBuffer(aWde, GL_FLOAT, 12 * sizeof(float), 3, stride);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<int>(data.size() / kLineFloats));
    for (int a : {aP0, aP1, aSide, aCol, aWde}) lineProg_->disableAttributeArray(a);
    lineVbo_->release();
    lineProg_->release();
    glEnable(GL_DEPTH_TEST);
}

void Viewport::paintGL() {
    if (sceneDirty_) uploadScene();
    const float dpr = static_cast<float>(devicePixelRatioF());
    glViewport(0, 0, static_cast<int>(width() * dpr), static_cast<int>(height() * dpr));
    glClearColor(static_cast<float>(style_.background.redF()), static_cast<float>(style_.background.greenF()),
                 static_cast<float>(style_.background.blueF()), 1.0f);
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    drawBackground();

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.0f, 2.0f);
    glDisable(GL_BLEND);
    drawFaces(false);

    std::vector<float> solid, back;
    buildEdgeLines(solid, back);
    if (style_.guides) {
        const double far = 3.0 * ((view_.target - view_.eye).length() +
                                  (scene_.bounds.empty() ? 500.0 : scene_.bounds.radius()));
        for (const Scene::Guide& g : scene_.guides) {
            // Endless guides reach well past the view; the line shader clips them at the eye.
            const double t0 = std::max(g.t0, -far - (g.p - view_.eye).length());
            const double t1 = std::min(g.t1, far + (g.p - view_.eye).length());
            if (t1 <= t0) continue;
            addLine(solid, g.p + g.dir * t0, g.p + g.dir * t1, style_.construction, 1.0f, true);
        }
        for (const V3& p : scene_.guidePoints) {
            const double s = pixelSize(p) * 4.0;
            addLine(solid, p - V3{s, 0, 0}, p + V3{s, 0, 0}, style_.construction, 1.0f, false);
            addLine(solid, p - V3{0, s, 0}, p + V3{0, s, 0}, style_.construction, 1.0f, false);
            addLine(solid, p - V3{0, 0, s}, p + V3{0, 0, s}, style_.construction, 1.0f, false);
        }
    }
    if (style_.axes) {
        const double len = 3.0 * ((view_.target - view_.eye).length() +
                                  (scene_.bounds.empty() ? 500.0 : scene_.bounds.radius()));
        const std::array<std::pair<V3, QColor>, 3> axes{
            {{V3{1, 0, 0}, QColor(255, 0, 0)}, {V3{0, 1, 0}, QColor(0, 160, 0)}, {V3{0, 0, 1}, QColor(0, 0, 255)}}};
        for (const auto& [d, c] : axes) {
            addLine(solid, V3{0, 0, 0}, d * len, c, 1.0f, false);
            addLine(solid, V3{0, 0, 0}, d * -len, c, 1.0f, true);
        }
    }
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if (!back.empty()) drawLines(back, false);
    drawLines(solid, true);

    // Transparent faces last, without writing depth.
    glDepthMask(GL_FALSE);
    drawFaces(true);
    glDepthMask(GL_TRUE);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDisable(GL_CULL_FACE);

    // Tool previews on top.
    std::vector<float> over;
    for (const OverlayLine& l : overlay_.lines) addLine(over, l.a, l.b, l.color, l.width, l.dashed);
    for (const OverlayPoint& p : overlay_.points) {
        const double s = pixelSize(p.p) * p.size * 0.5;
        const V3 f = eyeDirection();
        const V3 right = f.cross(view_.up).normalized();
        addLine(over, p.p - right * s, p.p + right * s, p.color, p.size, false);
    }
    drawLines(over, false);
    glDisable(GL_BLEND);
    drawOverlayText();
    ++frames_;
    emit framePainted();
}

void Viewport::drawOverlayText() {
    if (!overlay_.selectionBox && overlay_.tip.isEmpty()) return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    if (overlay_.selectionBox) {
        QPen pen(QColor(0, 0, 0));
        pen.setWidthF(1.0);
        if (overlay_.crossing) pen.setStyle(Qt::DashLine);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawRect(*overlay_.selectionBox);
    }
    if (!overlay_.tip.isEmpty()) {
        QFont f = font();
        f.setPointSizeF(f.pointSizeF() * 0.95);
        p.setFont(f);
        const QRectF br = p.fontMetrics().boundingRect(overlay_.tip).adjusted(-4, -2, 4, 2);
        const QRectF box = br.translated(overlay_.tipAt + QPointF(14, 24) - br.topLeft());
        p.setPen(QColor(80, 80, 80));
        p.setBrush(QColor(255, 255, 225));
        p.drawRect(box);
        p.setPen(QColor(0, 0, 0));
        p.drawText(box, Qt::AlignCenter, overlay_.tip);
    }
}

}  // namespace render
