#include "edit/Tools.h"

#include "skp/Builder.h"
#include "skp/Query.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QRegularExpression>

#include <cmath>
#include <numbers>

namespace edit {

using geom::V3;

namespace {

const QColor kRed(255, 0, 0), kGreen(0, 160, 0), kBlue(0, 0, 255);
constexpr int kCircleSegments = 24;  // CCircleTool::s_numSideDefault (main program 0x10083dbf8)

struct AxisInfo {
    V3 dir;
    QColor color;
    const char* name;
};
const std::array<AxisInfo, 3> kAxes{{{V3{1, 0, 0}, kRed, "On Red Axis"},
                                     {V3{0, 1, 0}, kGreen, "On Green Axis"},
                                     {V3{0, 0, 1}, kBlue, "On Blue Axis"}}};

// Closest point on the line (p, d) to the ray (o, r).
std::optional<V3> closestOnLine(const V3& p, const V3& d, const V3& o, const V3& r) {
    const V3 w = p - o;
    const double a = d.dot(d), b = d.dot(r), c = r.dot(r), dd = d.dot(w), e = r.dot(w);
    const double den = a * c - b * b;
    if (std::abs(den) < 1e-12) return std::nullopt;
    const double s = (b * e - c * dd) / den;
    return p + d * s;
}

QColor axisColor(const V3& d) {
    const V3 n = d.normalized();
    for (const AxisInfo& a : kAxes) {
        if (std::abs(n.dot(a.dir)) > 0.9999) return a.color;
    }
    return QColor(0, 0, 0);
}

}  // namespace

QString toolName(ToolId t) {
    switch (t) {
        case ToolId::Select: return QObject::tr("Select");
        case ToolId::Eraser: return QObject::tr("Eraser");
        case ToolId::Paint: return QObject::tr("Paint Bucket");
        case ToolId::Line: return QObject::tr("Line");
        case ToolId::Rectangle: return QObject::tr("Rectangle");
        case ToolId::Circle: return QObject::tr("Circle");
        case ToolId::PushPull: return QObject::tr("Push/Pull");
        case ToolId::Move: return QObject::tr("Move");
        case ToolId::Tape: return QObject::tr("Tape Measure");
        case ToolId::Orbit: return QObject::tr("Orbit");
        case ToolId::Pan: return QObject::tr("Pan");
        case ToolId::Zoom: return QObject::tr("Zoom");
        case ToolId::Arc: return QObject::tr("Arc");
        case ToolId::Polygon: return QObject::tr("Polygon");
        case ToolId::Rotate: return QObject::tr("Rotate");
        case ToolId::Scale: return QObject::tr("Scale");
        case ToolId::Offset: return QObject::tr("Offset");
        case ToolId::ZoomWindow: return QObject::tr("Zoom Window");
        case ToolId::FieldOfView: return QObject::tr("Field of View");
        case ToolId::Walk: return QObject::tr("Walk");
        case ToolId::LookAround: return QObject::tr("Look Around");
        case ToolId::PositionCamera: return QObject::tr("Position Camera");
    }
    return {};
}

ToolController::ToolController(render::Viewport* view, ui::Document* doc, QObject* parent)
    : render::InputHandler(parent), view_(view), doc_(doc) {}

ui::UnitFormat ToolController::units() const {
    return doc_ ? ui::modelUnits(doc_->model()) : ui::UnitFormat::Architectural;
}

QString ToolController::len(double inches) const {
    return ui::formatLength(inches, units());
}

void ToolController::setTool(ToolId t) {
    cancel();
    tool_ = t;
    if (view_) {
        Qt::CursorShape c = Qt::CrossCursor;
        if (t == ToolId::Select) c = Qt::ArrowCursor;
        if (t == ToolId::Orbit || t == ToolId::Pan) c = Qt::OpenHandCursor;
        if (t == ToolId::Paint) c = Qt::PointingHandCursor;
        view_->setCursor(c);
    }
    showPrompt();
    emit toolChanged(t);
}

void ToolController::cancel() {
    start_.reset();
    chain_.clear();
    plane_.reset();
    ppFace_.reset();
    ppDist_ = 0;
    moving_.clear();
    dragging_ = false;
    boxSelecting_ = false;
    lock_.reset();
    stage_ = 0;
    second_.reset();
    amount_ = 0;
    targets_.clear();
    offsetFace_.reset();
    if (view_) view_->setOverlay({});
    showPrompt();
}

void ToolController::showPrompt() {
    QString s, label;
    switch (tool_) {
        case ToolId::Select:
            s = tr("Click to select. Shift-click to add or remove. Drag for a box: left to right picks what is "
                   "inside, right to left what it crosses.");
            break;
        case ToolId::Eraser: s = tr("Click an edge, group or component to erase it."); break;
        case ToolId::Paint: s = tr("Click a face or group to paint it. Alt-click to pick up a material."); break;
        case ToolId::Line:
            s = start_ ? tr("Click the end point, or type a length. Arrow keys lock an axis. Esc stops.")
                       : tr("Click the start point of the line.");
            label = tr("Length");
            break;
        case ToolId::Rectangle:
            s = start_ ? tr("Click the opposite corner, or type width,depth.") : tr("Click the first corner.");
            label = tr("Dimensions");
            break;
        case ToolId::Circle:
            s = start_ ? tr("Click to set the radius, or type it.") : tr("Click the centre of the circle.");
            label = tr("Radius");
            break;
        case ToolId::PushPull:
            s = ppFace_ ? tr("Move to push or pull, then click, or type a distance.")
                        : tr("Press on a face and drag to push or pull it. Double-click repeats the last distance.");
            label = tr("Distance");
            break;
        case ToolId::Move:
            s = start_ ? tr("Click the point to move to, or type a distance.")
                       : tr("Pick what to move, then click a base point.");
            label = tr("Distance");
            break;
        case ToolId::Tape:
            s = start_ ? tr("Click the second point to read the distance.") : tr("Click the point to measure from.");
            label = tr("Length");
            break;
        case ToolId::Orbit: s = tr("Drag to orbit. Shift-drag to pan."); break;
        case ToolId::Pan: s = tr("Drag to pan."); break;
        case ToolId::Zoom: s = tr("Drag up or down to zoom."); break;
        default: s = extraPrompt(label); break;
    }
    emit status(s);
    emit measurement(label, QString());
}

std::optional<V3> ToolController::onPlane(QPointF pos, const geom::Plane& pl) const {
    if (!view_) return std::nullopt;
    const render::Ray r = view_->rayAt(pos);
    const auto t = pl.ray(r.origin, r.dir);
    if (!t) return std::nullopt;
    return r.origin + r.dir * *t;
}

Inference ToolController::infer(QPointF pos, const std::optional<V3>& from) const {
    Inference r;
    if (!view_) return r;
    const double tol = 10.0;
    const auto hit = view_->pick(pos, true);
    r.hit = hit;
    if (hit && !hit->isEdge) {
        r.faceNormal = hit->normal;
        if (hit->pick.face && hit->pick.top == *hit->pick.face) r.faceId = *hit->pick.face;
    }
    // Axis lock from the arrow keys wins.
    if (lock_ && from) {
        const render::Ray ray = view_->rayAt(pos);
        if (auto q = closestOnLine(*from, *lock_, ray.origin, ray.dir)) {
            r.p = *q;
            r.color = axisColor(*lock_);
            r.label = tr("Locked");
            return r;
        }
    }
    if (auto v = view_->snapVertex(pos, tol)) {
        r.p = *v;
        r.label = tr("Endpoint");
        r.color = QColor(0, 170, 0);
        return r;
    }
    if (hit && hit->isEdge) {
        const V3 mid = (hit->edgeA + hit->edgeB) * 0.5;
        const auto pm = view_->project(mid);
        if (pm && std::hypot(pm->x() - pos.x(), pm->y() - pos.y()) < tol) {
            r.p = mid;
            r.label = tr("Midpoint");
            r.color = QColor(0, 190, 255);
        } else {
            r.p = hit->point;
            r.label = tr("On Edge");
            r.color = QColor(230, 0, 0);
        }
        return r;
    }
    if (const auto po = view_->project(V3{0, 0, 0}); po && std::hypot(po->x() - pos.x(), po->y() - pos.y()) < tol) {
        r.p = V3{0, 0, 0};
        r.label = tr("Origin");
        r.color = QColor(230, 190, 0);
        return r;
    }
    if (from) {
        const render::Ray ray = view_->rayAt(pos);
        double best = 8.0;
        for (const AxisInfo& a : kAxes) {
            const auto q = closestOnLine(*from, a.dir, ray.origin, ray.dir);
            if (!q) continue;
            const auto pq = view_->project(*q);
            if (!pq) continue;
            const double d = std::hypot(pq->x() - pos.x(), pq->y() - pos.y());
            if (d < best && q->dist(*from) > 1e-6) {
                best = d;
                r.p = *q;
                r.label = tr(a.name);
                r.color = a.color;
            }
        }
        if (!r.label.isEmpty()) return r;
    }
    if (hit) {
        r.p = hit->point;
        r.label = tr("On Face");
        r.color = QColor(0, 0, 255);
        return r;
    }
    // Ground plane, or a plane through the view target when looking level.
    if (auto g = onPlane(pos, geom::Plane{{0, 0, 1}, 0}); g && (*g - view_->view().eye).dot(view_->rayAt(pos).dir) > 0) {
        r.p = *g;
        return r;
    }
    if (auto g = onPlane(pos, geom::Plane::fromPointNormal(view_->view().target, view_->eyeDirection()))) r.p = *g;
    return r;
}

void ToolController::drawingFrame(V3& n, V3& u, V3& v) const {
    n = plane_ ? plane_->n.normalized() : V3{0, 0, 1};
    if (std::abs(n.z) > 0.999) {
        u = {1, 0, 0};
        v = n.z > 0 ? V3{0, 1, 0} : V3{0, -1, 0};
        return;
    }
    u = V3{0, 0, 1}.cross(n).normalized();
    v = n.cross(u).normalized();
}

void ToolController::preview() {
    if (!view_) return;
    render::Overlay o;
    if (!cur_.label.isEmpty()) {
        o.points.push_back({cur_.p, cur_.color, 7.0f});
        o.tip = cur_.label;
        o.tipAt = lastPos_;
    }
    switch (tool_) {
        case ToolId::Line:
        case ToolId::Tape:
            if (start_) {
                const QColor c = cur_.color == QColor(0, 0, 0) ? axisColor(cur_.p - *start_) : cur_.color;
                o.lines.push_back({*start_, cur_.p, c, 1.5f, tool_ == ToolId::Tape});
                emit measurement(tr("Length"), len(start_->dist(cur_.p)));
            }
            break;
        case ToolId::Move:
            if (start_) {
                o.lines.push_back({*start_, cur_.p, axisColor(cur_.p - *start_), 1.0f, true});
                const V3 d = cur_.p - *start_;
                for (skp::ObjId id : moving_) {
                    const auto it = view_->scene().topBoxes.find(id);
                    if (it == view_->scene().topBoxes.end() || it->second.empty()) continue;
                    const geom::Box& b = it->second;
                    for (int i = 0; i < 4; ++i) {
                        const V3 lo{(i & 1) ? b.hi.x : b.lo.x, (i & 2) ? b.hi.y : b.lo.y, b.lo.z};
                        const V3 hi{lo.x, lo.y, b.hi.z};
                        o.lines.push_back({lo + d, hi + d, QColor(0, 1, 255), 1.0f, false});
                    }
                }
                emit measurement(tr("Distance"), len(d.length()));
            }
            break;
        case ToolId::Rectangle:
            if (start_ && plane_) {
                V3 n, u, v;
                drawingFrame(n, u, v);
                const V3 p2 = plane_->project(cur_.p);
                const V3 d = p2 - *start_;
                const double a = d.dot(u), b = d.dot(v);
                const std::array<V3, 4> c{*start_, *start_ + u * a, *start_ + u * a + v * b, *start_ + v * b};
                for (std::size_t i = 0; i < 4; ++i) o.lines.push_back({c[i], c[(i + 1) % 4], QColor(0, 0, 0), 1.5f, false});
                emit measurement(tr("Dimensions"), len(std::abs(a)) + QStringLiteral(", ") + len(std::abs(b)));
            }
            break;
        case ToolId::Circle:
            if (start_ && plane_) {
                V3 n, u, v;
                drawingFrame(n, u, v);
                const double r = plane_->project(cur_.p).dist(*start_);
                for (int i = 0; i < kCircleSegments; ++i) {
                    const double a0 = 2 * std::numbers::pi * i / kCircleSegments;
                    const double a1 = 2 * std::numbers::pi * (i + 1) / kCircleSegments;
                    o.lines.push_back({*start_ + (u * std::cos(a0) + v * std::sin(a0)) * r,
                                       *start_ + (u * std::cos(a1) + v * std::sin(a1)) * r, QColor(0, 0, 0), 1.5f,
                                       false});
                }
                o.lines.push_back({*start_, plane_->project(cur_.p), QColor(0, 0, 0), 1.0f, true});
                emit measurement(tr("Radius"), len(r));
            }
            break;
        case ToolId::PushPull:
            if (ppFace_ && doc_) {
                const auto* face = doc_->model().get<skp::Face>(ppFace_);
                if (face != nullptr) {
                    const V3 off = ppNormal_ * ppDist_;
                    const skp::Loop3 outer = skp::faceOuter(doc_->model(), *face);
                    for (std::size_t i = 0; i < outer.size(); ++i) {
                        const V3 a = outer[i], b = outer[(i + 1) % outer.size()];
                        o.lines.push_back({a + off, b + off, QColor(0, 0, 0), 1.5f, false});
                        o.lines.push_back({a, a + off, QColor(0, 0, 0), 1.0f, false});
                    }
                }
                emit measurement(tr("Distance"), len(ppDist_));
            }
            break;
        default: extraPreview(o); break;
    }
    view_->setOverlay(std::move(o));
}

// ------------------------------------------------------------------ mouse

void ToolController::press(QMouseEvent* e) {
    if (!view_ || !doc_) return;
    lastPos_ = pressPos_ = e->position();
    if (e->button() == Qt::RightButton) {
        cancel();
        return;
    }
    if (e->button() != Qt::LeftButton) return;
    dragging_ = true;
    if (isExtra(tool_)) {
        extraPress(e);
        return;
    }
    switch (tool_) {
        case ToolId::Select: boxSelecting_ = false; break;
        case ToolId::Orbit: view_->beginOrbit(e->position()); break;
        case ToolId::Eraser: eraseAt(e->position()); break;
        case ToolId::Paint: paintAt(e->position(), (e->modifiers() & (Qt::AltModifier | Qt::MetaModifier)) != 0); break;
        case ToolId::Line:
            cur_ = infer(e->position(), start_);
            if (!start_) {
                start_ = cur_.p;
                chain_ = {cur_.p};
            } else {
                finishLine(cur_.p);
            }
            break;
        case ToolId::Rectangle:
        case ToolId::Circle:
            cur_ = infer(e->position(), start_);
            if (!start_) {
                start_ = cur_.p;
                if (cur_.faceNormal && cur_.label == tr("On Face")) {
                    plane_ = geom::Plane::fromPointNormal(cur_.p, *cur_.faceNormal);
                } else {
                    plane_ = geom::Plane::fromPointNormal(cur_.p, V3{0, 0, 1});
                }
            } else if (tool_ == ToolId::Rectangle) {
                finishRect(plane_ ? plane_->project(cur_.p) : cur_.p);
            } else {
                finishCircle(plane_ ? plane_->project(cur_.p) : cur_.p);
            }
            break;
        case ToolId::PushPull:
            if (ppFace_) {
                commitPushPull(ppDist_);
            } else {
                const auto h = view_->pick(e->position(), false);
                if (h && h->pick.face && h->pick.top == *h->pick.face) {
                    const auto* face = doc_->model().get<skp::Face>(h->pick.face);
                    if (face != nullptr) {
                        ppFace_ = *h->pick.face;
                        ppNormal_ = geom::Plane::fromSkp(face->plane).n.normalized();
                        ppPoint_ = h->point;
                        ppDist_ = 0;
                    }
                }
            }
            break;
        case ToolId::Move:
            cur_ = infer(e->position(), start_);
            if (!start_) {
                moving_ = doc_->selection();
                if (moving_.empty()) {
                    if (const auto h = view_->pick(e->position(), true)) {
                        moving_ = {h->pick.top};
                        doc_->setSelection(moving_);
                    }
                }
                if (!moving_.empty()) start_ = cur_.p;
            } else {
                commitMove(cur_.p - *start_);
            }
            break;
        case ToolId::Tape:
            cur_ = infer(e->position(), start_);
            if (!start_) {
                start_ = cur_.p;
            } else {
                emit status(tr("Distance: %1").arg(len(start_->dist(cur_.p))));
                emit measurement(tr("Length"), len(start_->dist(cur_.p)));
                start_.reset();
                view_->setOverlay({});
                return;
            }
            break;
        default: break;
    }
    showPrompt();
    preview();
}

void ToolController::move(QMouseEvent* e) {
    if (!view_ || !doc_) return;
    const QPointF d = e->position() - lastPos_;
    lastPos_ = e->position();
    const bool left = (e->buttons() & Qt::LeftButton) != 0;
    delta_ = d;
    if (isExtra(tool_)) {
        extraMove(e);
        return;
    }
    switch (tool_) {
        case ToolId::Orbit:
            if (left) {
                if (e->modifiers() & Qt::ShiftModifier) view_->panBy(d.x(), d.y()); else view_->orbitBy(d.x(), d.y());
            }
            return;
        case ToolId::Pan:
            if (left) view_->panBy(d.x(), d.y());
            return;
        case ToolId::Zoom:
            if (left) view_->zoomAt(pressPos_, std::pow(1.01, d.y()));
            return;
        case ToolId::Select:
            if (left && dragging_) {
                const QPointF delta = e->position() - pressPos_;
                if (std::hypot(delta.x(), delta.y()) > 4) boxSelecting_ = true;
                if (boxSelecting_) {
                    render::Overlay o;
                    o.selectionBox = QRectF(pressPos_, e->position()).normalized();
                    o.crossing = e->position().x() < pressPos_.x();
                    view_->setOverlay(std::move(o));
                }
            }
            return;
        case ToolId::PushPull:
            if (ppFace_) {
                const double k = view_->pixelSize(ppPoint_) * 60.0;
                const auto s0 = view_->project(ppPoint_);
                const auto s1 = view_->project(ppPoint_ + ppNormal_ * k);
                const Inference inf = infer(e->position(), std::nullopt);
                if (inf.label == tr("Endpoint") || inf.label == tr("Midpoint") || inf.label == tr("On Edge")) {
                    ppDist_ = (inf.p - ppPoint_).dot(ppNormal_);
                } else if (s0 && s1) {
                    const QPointF sv = *s1 - *s0;
                    const double l2 = QPointF::dotProduct(sv, sv);
                    if (l2 > 1.0) ppDist_ = QPointF::dotProduct(e->position() - pressPos_, sv) / l2 * k;
                }
                cur_ = Inference{};
                preview();
            } else {
                cur_ = Inference{};
                const auto h = view_->pick(e->position(), false);
                render::Overlay o;
                if (h && h->pick.face && h->pick.top == *h->pick.face) {
                    o.tip = tr("On Face");
                    o.tipAt = e->position();
                }
                view_->setOverlay(std::move(o));
            }
            return;
        case ToolId::Eraser:
        case ToolId::Paint: return;
        default: break;
    }
    cur_ = infer(e->position(), start_);
    preview();
}

void ToolController::release(QMouseEvent* e) {
    if (!view_ || !doc_ || e->button() != Qt::LeftButton) return;
    const QPointF delta = e->position() - pressPos_;
    const bool dragged = std::hypot(delta.x(), delta.y()) > 4;
    dragging_ = false;
    if (tool_ == ToolId::Orbit) view_->endOrbit();
    if (isExtra(tool_)) {
        extraRelease(e);
        return;
    }
    if (tool_ == ToolId::Select) {
        if (boxSelecting_) {
            const QRectF box = QRectF(pressPos_, e->position()).normalized();
            const bool crossing = e->position().x() < pressPos_.x();
            std::set<skp::ObjId> sel;
            if (e->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier)) sel = doc_->selection();
            for (skp::ObjId id : view_->pickBox(box, crossing)) sel.insert(id);
            doc_->setSelection(std::move(sel));
            boxSelecting_ = false;
            view_->setOverlay({});
        } else {
            selectAt(e->position(), e->modifiers());
        }
        return;
    }
    // Press-drag-release also works for these tools.
    if (dragged && tool_ == ToolId::PushPull && ppFace_) commitPushPull(ppDist_);
    if (dragged && (tool_ == ToolId::Rectangle || tool_ == ToolId::Circle || tool_ == ToolId::Line) && start_) {
        cur_ = infer(e->position(), start_);
        if (tool_ == ToolId::Rectangle) finishRect(plane_ ? plane_->project(cur_.p) : cur_.p);
        else if (tool_ == ToolId::Circle) finishCircle(plane_ ? plane_->project(cur_.p) : cur_.p);
        else finishLine(cur_.p);
    }
}

void ToolController::doubleClick(QMouseEvent* e) {
    if (!view_ || !doc_) return;
    if (tool_ == ToolId::PushPull && std::abs(lastPushPull_) > 1e-9) {
        const auto h = view_->pick(e->position(), false);
        if (h && h->pick.face && h->pick.top == *h->pick.face) {
            const auto* face = doc_->model().get<skp::Face>(h->pick.face);
            if (face != nullptr) {
                ppFace_ = *h->pick.face;
                ppNormal_ = geom::Plane::fromSkp(face->plane).n.normalized();
                commitPushPull(lastPushPull_);
            }
        }
        return;
    }
    if (tool_ == ToolId::Select) {
        // A face with its edges.
        const auto h = view_->pick(e->position(), false);
        if (h && h->pick.face && h->pick.top == *h->pick.face) {
            std::set<skp::ObjId> sel{*h->pick.face};
            if (const auto* face = doc_->model().get<skp::Face>(h->pick.face)) {
                for (skp::ObjId l : face->loops) {
                    if (const auto* loop = doc_->model().get<skp::Loop>(skp::Ref{l})) {
                        for (skp::ObjId u : loop->uses) {
                            const auto* eu = doc_->model().get<skp::EdgeUse>(skp::Ref{u});
                            if (eu != nullptr && eu->edge) sel.insert(*eu->edge);
                        }
                    }
                }
            }
            doc_->setSelection(std::move(sel));
        }
    }
}

bool ToolController::key(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape) {
        cancel();
        return true;
    }
    const bool drawing = start_ && (tool_ == ToolId::Line || tool_ == ToolId::Move || tool_ == ToolId::Tape);
    if (drawing) {
        std::optional<V3> want;
        if (e->key() == Qt::Key_Right) want = V3{1, 0, 0};
        if (e->key() == Qt::Key_Left) want = V3{0, 1, 0};
        if (e->key() == Qt::Key_Up || e->key() == Qt::Key_Down) want = V3{0, 0, 1};
        if (want) {
            const bool same = lock_ && lock_->dist(*want) < 1e-9;
            lock_ = same ? std::nullopt : want;
            return true;
        }
    }
    return false;
}

// ------------------------------------------------------------------ actions

void ToolController::selectAt(QPointF pos, Qt::KeyboardModifiers mods) {
    const auto h = view_->pick(pos, true);
    std::set<skp::ObjId> sel = doc_->selection();
    const bool toggle = (mods & Qt::ShiftModifier) != 0;
    const bool add = (mods & (Qt::ControlModifier | Qt::AltModifier)) != 0;
    if (!toggle && !add) sel.clear();
    if (h) {
        const skp::ObjId id = h->pick.top;
        if (toggle && sel.contains(id)) sel.erase(id); else sel.insert(id);
    }
    doc_->setSelection(std::move(sel));
}

void ToolController::eraseAt(QPointF pos) {
    const auto h = view_->pick(pos, true);
    if (!h) return;
    const skp::ObjId id = h->pick.top;
    const bool isFace = h->pick.face && *h->pick.face == id;
    if (isFace) {
        emit status(tr("The eraser removes edges, groups and components. Select a face and press Delete to remove it."));
        return;
    }
    doc_->checkpoint(tr("Erase"));
    skp::erase(doc_->model(), doc_->root(), {id});
    doc_->edited();
}

void ToolController::paintAt(QPointF pos, bool sample) {
    const auto h = view_->pick(pos, false);
    if (!h) return;
    const skp::ObjId id = h->pick.top;
    if (sample) {
        skp::Ref m;
        if (const auto* face = doc_->model().get<skp::Face>(h->pick.face)) m = face->de.material;
        if (!m) {
            if (const auto* inst = doc_->model().get<skp::Instance>(skp::Ref{id})) m = inst->de.material;
        }
        doc_->setPaintMaterial(m);
        return;
    }
    // Painting a selected face paints every selected face.
    std::set<skp::ObjId> targets{id};
    if (doc_->selection().contains(id)) targets = doc_->selection();
    doc_->checkpoint(tr("Paint"));
    for (skp::ObjId t : targets) skp::paint(doc_->model(), t, doc_->paintMaterial());
    doc_->edited();
}

void ToolController::finishLine(const V3& p) {
    if (chain_.empty() || chain_.back().dist(p) < 1e-3) return;
    doc_->checkpoint(tr("Line"));
    skp::addEdge(doc_->model(), doc_->root(), chain_.back(), p);
    bool closed = false;
    if (chain_.size() >= 3 && chain_.front().dist(p) < 1e-3) {
        // The loop closed: make a face if it is flat. Faces on the ground look down, like SketchUp's.
        const bool ground = std::all_of(chain_.begin(), chain_.end(), [](const V3& q) { return std::abs(q.z) < 1e-6; });
        V3 c;
        for (const V3& q : chain_) c += q;
        c = c / static_cast<double>(chain_.size());
        const V3 towards = ground ? V3{0, 0, -1} : view_->view().eye - c;
        skp::addFace(doc_->model(), doc_->root(), chain_, {}, towards);
        closed = true;
    }
    doc_->edited();
    if (closed) {
        chain_.clear();
        start_.reset();
        lock_.reset();
    } else {
        chain_.push_back(p);
        start_ = p;
    }
}

void ToolController::finishRect(const V3& p2) {
    if (!start_ || !plane_) return;
    V3 n, u, v;
    drawingFrame(n, u, v);
    const V3 d = p2 - *start_;
    const double a = d.dot(u), b = d.dot(v);
    if (std::abs(a) < 1e-3 || std::abs(b) < 1e-3) return;
    const skp::Loop3 pts{*start_, *start_ + u * a, *start_ + u * a + v * b, *start_ + v * b};
    doc_->checkpoint(tr("Rectangle"));
    const bool ground = std::abs(n.z) > 0.999 && std::abs(start_->z) < 1e-6;
    std::optional<skp::ObjId> made;
    if (cur_.faceId) made = skp::imprintFace(doc_->model(), doc_->root(), *cur_.faceId, pts);
    if (!made) {
        // Try the face the first corner sat on.
        for (skp::ObjId id : skp::rootComponent(doc_->model())->ents) {
            const auto* face = doc_->model().get<skp::Face>(skp::Ref{id});
            if (face == nullptr) continue;
            const geom::Plane fp = geom::Plane::fromSkp(face->plane);
            if (std::abs(fp.n.dot(n)) > 0.9999 && std::abs(fp.dist(*start_)) < 1e-3) {
                made = skp::imprintFace(doc_->model(), doc_->root(), id, pts);
                if (made) break;
            }
        }
    }
    if (!made) skp::addFace(doc_->model(), doc_->root(), pts, {}, ground ? V3{0, 0, -1} : n);
    doc_->edited();
    start_.reset();
    plane_.reset();
    view_->setOverlay({});
    showPrompt();
}

void ToolController::finishCircle(const V3& p2) {
    if (!start_ || !plane_) return;
    V3 n, u, v;
    drawingFrame(n, u, v);
    const double r = p2.dist(*start_);
    if (r < 1e-3) return;
    // Start the polygon toward the clicked point.
    const V3 dir = (p2 - *start_).normalized();
    const double a0 = std::atan2(dir.dot(v), dir.dot(u));
    skp::Loop3 pts;
    for (int i = 0; i < kCircleSegments; ++i) {
        const double a = a0 + 2 * std::numbers::pi * i / kCircleSegments;
        pts.push_back(*start_ + (u * std::cos(a) + v * std::sin(a)) * r);
    }
    doc_->checkpoint(tr("Circle"));
    const bool ground = std::abs(n.z) > 0.999 && std::abs(start_->z) < 1e-6;
    std::optional<skp::ObjId> made;
    for (skp::ObjId id : skp::rootComponent(doc_->model())->ents) {
        const auto* face = doc_->model().get<skp::Face>(skp::Ref{id});
        if (face == nullptr) continue;
        const geom::Plane fp = geom::Plane::fromSkp(face->plane);
        if (std::abs(fp.n.dot(n)) > 0.9999 && std::abs(fp.dist(*start_)) < 1e-3) {
            made = skp::imprintFace(doc_->model(), doc_->root(), id, pts);
            if (made) break;
        }
    }
    if (!made) skp::addFace(doc_->model(), doc_->root(), pts, {}, ground ? V3{0, 0, -1} : n);
    doc_->edited();
    start_.reset();
    plane_.reset();
    view_->setOverlay({});
    showPrompt();
}

void ToolController::commitPushPull(double d) {
    if (!ppFace_) return;
    const skp::ObjId face = *ppFace_;
    ppFace_.reset();
    view_->setOverlay({});
    if (std::abs(d) < 1e-3) {
        showPrompt();
        return;
    }
    doc_->checkpoint(tr("Push/Pull"));
    skp::pushPull(doc_->model(), doc_->root(), face, d);
    lastPushPull_ = d;
    doc_->edited();
    emit measurement(tr("Distance"), len(d));
    showPrompt();
}

void ToolController::commitMove(const V3& delta) {
    if (moving_.empty()) return;
    if (delta.length() > 1e-6) {
        doc_->checkpoint(tr("Move"));
        skp::translate(doc_->model(), doc_->root(), moving_, delta);
        doc_->edited();
    }
    start_.reset();
    moving_.clear();
    lock_.reset();
    view_->setOverlay({});
    showPrompt();
}

bool ToolController::typed(const QString& text) {
    if (!doc_ || !view_) return false;
    if (isExtra(tool_)) return extraTyped(text);
    const ui::UnitFormat u = units();
    switch (tool_) {
        case ToolId::Line:
        case ToolId::Move: {
            if (!start_) return false;
            const auto l = ui::parseLength(text, u);
            const V3 dir = (cur_.p - *start_).normalized();
            if (!l || dir.length() < 0.5) return false;
            const V3 p = *start_ + dir * *l;
            if (tool_ == ToolId::Line) finishLine(p); else commitMove(p - *start_);
            preview();
            return true;
        }
        case ToolId::Rectangle: {
            if (!start_ || !plane_) return false;
            const QStringList parts = text.split(QRegularExpression(QStringLiteral("[,;]")));
            if (parts.size() != 2) return false;
            const auto a = ui::parseLength(parts[0], u), b = ui::parseLength(parts[1], u);
            if (!a || !b) return false;
            V3 n, uu, v;
            drawingFrame(n, uu, v);
            const V3 d = plane_->project(cur_.p) - *start_;
            const double sa = d.dot(uu) < 0 ? -1 : 1, sb = d.dot(v) < 0 ? -1 : 1;
            finishRect(*start_ + uu * (*a * sa) + v * (*b * sb));
            return true;
        }
        case ToolId::Circle: {
            if (!start_ || !plane_) return false;
            const auto r = ui::parseLength(text, u);
            if (!r || *r <= 0) return false;
            V3 n, uu, v;
            drawingFrame(n, uu, v);
            V3 dir = (plane_->project(cur_.p) - *start_).normalized();
            if (dir.length() < 0.5) dir = uu;
            finishCircle(*start_ + dir * *r);
            return true;
        }
        case ToolId::PushPull: {
            const auto d = ui::parseLength(text, u);
            if (!d) return false;
            if (ppFace_) {
                // Without a mouse movement the typed distance pulls toward the viewer's side of the face.
                double sign = ppDist_ < 0 ? -1.0 : 1.0;
                if (std::abs(ppDist_) < 1e-9) sign = ppNormal_.dot(view_->view().eye - ppPoint_) < 0 ? -1.0 : 1.0;
                commitPushPull(sign * *d);
                return true;
            }
            return false;
        }
        default: return false;
    }
}

}  // namespace edit
