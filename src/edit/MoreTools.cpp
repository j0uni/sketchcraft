// Arc, Polygon, Rotate, Scale and Offset, and the camera tools Zoom Window, Field of View, Walk,
// Look Around and Position Camera.

#include "edit/Tools.h"

#include "skp/Builder.h"
#include "skp/Query.h"

#include <QMouseEvent>
#include <QRegularExpression>

#include <cmath>
#include <numbers>

namespace edit {

using geom::M4;
using geom::V3;

namespace {

constexpr double kTol = 1e-3;
constexpr double kEyeHeight = 66;            // Position Camera: 5' 6" above the clicked point (estimated)
constexpr double kAngleSnap = 15;            // protractor snaps every 15 degrees (estimated)
constexpr double kAngleSnapWindow = 1.5;     // degrees from a snap angle that still snap (estimated)

double toDeg(double r) { return r * 180.0 / std::numbers::pi; }
double toRad(double d) { return d * std::numbers::pi / 180.0; }

// "12s" sets a side count.
std::optional<int> sideCount(const QString& text) {
    static const QRegularExpression re(QStringLiteral(R"(^\s*(\d+)\s*s\s*$)"));
    const auto m = re.match(text);
    if (!m.hasMatch()) return std::nullopt;
    const int n = m.captured(1).toInt();
    if (n < 3 || n > 999) return std::nullopt;
    return n;
}

std::optional<double> plainNumber(const QString& text) {
    QString t = text.trimmed();
    t.replace(u',', u'.');
    t.remove(QStringLiteral("deg"));
    t.remove(u'°');
    bool ok = false;
    const double v = t.trimmed().toDouble(&ok);
    return ok ? std::optional<double>(v) : std::nullopt;
}

// Arc through a and b bulging by h along w (unit, in the plane with normal n).
std::vector<V3> arcPoints(const V3& a, const V3& b, const V3& n, const V3& w, double h, int segments) {
    const double c = a.dist(b);
    if (c < kTol) return {};
    if (std::abs(h) < kTol) return {a, b};
    const V3 mid = (a + b) * 0.5;
    const double r = (h * h + c * c / 4.0) / (2.0 * std::abs(h));
    const V3 apex = mid + w * h;
    const V3 centre = apex - w * (h > 0 ? r : -r);
    const V3 e1 = (a - centre).normalized();
    const V3 e2 = n.normalized().cross(e1).normalized();
    auto angleOf = [&](const V3& p) {
        const V3 d = p - centre;
        double ang = std::atan2(d.dot(e2), d.dot(e1));
        if (ang < 0) ang += 2 * std::numbers::pi;
        return ang;
    };
    double sweep = angleOf(b);
    if (angleOf(apex) > sweep) sweep -= 2 * std::numbers::pi;
    std::vector<V3> pts;
    for (int i = 0; i <= segments; ++i) {
        const double ang = sweep * i / segments;
        pts.push_back(centre + (e1 * std::cos(ang) + e2 * std::sin(ang)) * r);
    }
    pts.front() = a;
    pts.back() = b;
    return pts;
}

}  // namespace

bool ToolController::isExtra(ToolId t) {
    return static_cast<int>(t) >= static_cast<int>(ToolId::Arc);
}

std::set<skp::ObjId> ToolController::targetsAt(const QPointF& pos) {
    if (!doc_->selection().empty()) return doc_->selection();
    if (const auto h = view_->pick(pos, true)) {
        std::set<skp::ObjId> s{h->pick.top};
        doc_->setSelection(s);
        return s;
    }
    return {};
}

QString ToolController::extraPrompt(QString& label) const {
    switch (tool_) {
        case ToolId::Arc:
            label = stage_ == 2 ? tr("Bulge") : tr("Length");
            return stage_ == 0   ? tr("Click the start point of the arc. Type a number followed by s to set its segments.")
                   : stage_ == 1 ? tr("Click the end point of the arc.")
                                 : tr("Move to set the bulge and click, or type the bulge.");
        case ToolId::Polygon:
            label = tr("Radius");
            return stage_ == 0 ? tr("Click the centre of the polygon. Type a number followed by s to set its sides.")
                               : tr("Click to set the radius, or type it.");
        case ToolId::Rotate:
            label = tr("Angle");
            return stage_ == 0   ? tr("Pick what to rotate (or select it first), then click the centre of rotation.")
                   : stage_ == 1 ? tr("Click the start of the angle.")
                                 : tr("Click the end of the angle, or type an angle in degrees.");
        case ToolId::Scale:
            label = tr("Scale");
            return stage_ == 0 ? tr("Pick what to scale (or select it first), then click a point to drag.")
                               : tr("Move to scale and click, or type a scale factor.");
        case ToolId::Offset:
            label = tr("Distance");
            return stage_ == 0 ? tr("Click a face to offset its outline.") : tr("Move inward and click, or type a distance.");
        case ToolId::ZoomWindow: return tr("Drag a rectangle to zoom to.");
        case ToolId::FieldOfView:
            label = tr("Field of View");
            return tr("Drag up or down to widen or narrow the field of view, or type it in degrees.");
        case ToolId::Walk: return tr("Drag up and down to walk, left and right to turn.");
        case ToolId::LookAround: return tr("Drag to look around from where you stand.");
        case ToolId::PositionCamera: return tr("Click where to stand; the eye is placed 5' 6\" above that point.");
        default: return {};
    }
}

void ToolController::extraPress(QMouseEvent* e) {
    const QPointF pos = e->position();
    switch (tool_) {
        case ToolId::Arc:
        case ToolId::Polygon:
            cur_ = infer(pos, tool_ == ToolId::Arc && stage_ >= 1 ? start_ : std::nullopt);
            if (stage_ == 0) {
                start_ = cur_.p;
                plane_ = cur_.faceNormal && cur_.label == tr("On Face")
                             ? geom::Plane::fromPointNormal(cur_.p, *cur_.faceNormal)
                             : geom::Plane::fromPointNormal(cur_.p, V3{0, 0, 1});
                stage_ = 1;
            } else if (tool_ == ToolId::Polygon) {
                finishPolygon(plane_->project(cur_.p));
            } else if (stage_ == 1) {
                const V3 p = plane_->project(cur_.p);
                if (p.dist(*start_) < kTol) return;
                second_ = p;
                stage_ = 2;
            } else {
                finishArc(amount_);
            }
            break;
        case ToolId::Rotate:
            if (stage_ == 0) {
                targets_ = targetsAt(pos);
                if (targets_.empty()) return;
                cur_ = infer(pos, std::nullopt);
                pivot_ = cur_.p;
                plane_ = cur_.faceNormal && cur_.label == tr("On Face")
                             ? geom::Plane::fromPointNormal(cur_.p, *cur_.faceNormal)
                             : geom::Plane::fromPointNormal(cur_.p, V3{0, 0, 1});
                stage_ = 1;
            } else if (stage_ == 1) {
                cur_ = infer(pos, pivot_);
                const V3 p = plane_->project(cur_.p);
                if (p.dist(pivot_) < kTol) return;
                second_ = p;
                amount_ = 0;
                stage_ = 2;
            } else {
                commitRotate(amount_);
            }
            break;
        case ToolId::Scale:
            if (stage_ == 0) {
                targets_ = targetsAt(pos);
                if (targets_.empty()) return;
                geom::Box box;
                for (skp::ObjId id : targets_) {
                    if (auto it = view_->scene().topBoxes.find(id); it != view_->scene().topBoxes.end()) {
                        box.add(it->second.lo);
                        box.add(it->second.hi);
                    }
                }
                if (box.empty()) return;
                pivot_ = box.center();
                cur_ = infer(pos, std::nullopt);
                second_ = cur_.p;
                amount_ = 1.0;
                stage_ = 1;
            } else {
                commitScale(amount_);
            }
            break;
        case ToolId::Offset:
            if (stage_ == 0) {
                const auto h = view_->pick(pos, false);
                if (!h || !h->pick.face || h->pick.top != *h->pick.face) return;
                offsetFace_ = *h->pick.face;
                start_ = h->point;
                amount_ = 0;
                stage_ = 1;
            } else {
                commitOffset(amount_);
            }
            break;
        case ToolId::PositionCamera: {
            cur_ = infer(pos, std::nullopt);
            render::View v = view_->view();
            V3 f = v.target - v.eye;
            f.z = 0;
            if (f.length() < 1e-9) f = V3{0, 1, 0};
            v.eye = cur_.p + V3{0, 0, kEyeHeight};
            v.target = v.eye + f.normalized() * 100.0;
            v.up = V3{0, 0, 1};
            v.perspective = true;
            view_->setView(v, true);
            setTool(ToolId::LookAround);
            return;
        }
        default: break;
    }
    showPrompt();
    preview();
}

void ToolController::extraMove(QMouseEvent* e) {
    const QPointF pos = e->position();
    const bool left = (e->buttons() & Qt::LeftButton) != 0;
    const double perPixel = std::numbers::pi * 0.5 / std::max(1, view_->height());  // same rate as orbit
    switch (tool_) {
        case ToolId::Arc:
            cur_ = infer(pos, stage_ == 1 ? start_ : std::nullopt);
            if (stage_ == 2 && plane_ && start_ && second_) {
                const V3 p = plane_->project(cur_.p);
                const V3 chord = (*second_ - *start_).normalized();
                const V3 w = plane_->n.cross(chord).normalized();
                amount_ = (p - (*start_ + *second_) * 0.5).dot(w);
            }
            break;
        case ToolId::Rotate:
            cur_ = infer(pos, stage_ >= 1 ? std::optional<V3>(pivot_) : std::nullopt);
            if (stage_ == 2 && plane_ && second_) {
                const V3 n = plane_->n.normalized();
                const V3 a = (*second_ - pivot_).normalized();
                const V3 b = (plane_->project(cur_.p) - pivot_).normalized();
                double ang = std::atan2(n.dot(a.cross(b)), a.dot(b));
                const double deg = toDeg(ang);
                const double snapped = std::round(deg / kAngleSnap) * kAngleSnap;
                if (std::abs(deg - snapped) < kAngleSnapWindow) ang = toRad(snapped);
                amount_ = ang;
            }
            break;
        case ToolId::Scale:
            cur_ = infer(pos, std::nullopt);
            if (stage_ == 1 && second_) {
                const auto sp = view_->project(pivot_), sg = view_->project(*second_);
                if (sp && sg) {
                    const double d0 = std::hypot(sg->x() - sp->x(), sg->y() - sp->y());
                    const double d1 = std::hypot(pos.x() - sp->x(), pos.y() - sp->y());
                    if (d0 > 1) amount_ = std::max(0.01, d1 / d0);
                }
            }
            break;
        case ToolId::Offset:
            if (stage_ == 1 && offsetFace_) {
                const auto* face = doc_->model().get<skp::Face>(offsetFace_);
                if (face == nullptr) break;
                const geom::Plane pl = geom::Plane::fromSkp(face->plane);
                if (auto p = onPlane(pos, pl)) {
                    // Distance from the cursor to the nearest outline edge, inward only.
                    const skp::Loop3 outer = skp::faceOuter(doc_->model(), *face);
                    double best = 1e300;
                    for (std::size_t i = 0; i < outer.size(); ++i) {
                        best = std::min(best, p->dist(geom::closestOnSegment(*p, outer[i], outer[(i + 1) % outer.size()])));
                    }
                    amount_ = best < 1e299 ? best : 0;
                }
            }
            cur_ = Inference{};
            break;
        case ToolId::ZoomWindow:
            if (left) {
                render::Overlay o;
                o.selectionBox = QRectF(pressPos_, pos).normalized();
                view_->setOverlay(std::move(o));
            }
            return;
        case ToolId::FieldOfView:
            if (left) view_->zoomFov(delta_.y() * 0.1);
            emit measurement(tr("Field of View"), QStringLiteral("%1 deg").arg(view_->view().fovDeg, 0, 'f', 2));
            return;
        case ToolId::Walk:
            if (left) {
                render::View v = view_->view();
                V3 f = v.target - v.eye;
                f.z = 0;
                if (f.length() < 1e-9) return;
                f = f.normalized();
                const V3 step = f * (-delta_.y() * view_->pixelSize(v.target) * 2.0);
                v.eye += step;
                v.target += step;
                const M4 turn = M4::rotate(v.eye, V3{0, 0, 1}, -delta_.x() * perPixel);
                v.target = turn.point(v.target);
                view_->setView(v);
            }
            return;
        case ToolId::LookAround:
            if (left) {
                render::View v = view_->view();
                const V3 f = (v.target - v.eye);
                const V3 right = f.cross(V3{0, 0, 1}).normalized();
                M4 turn = M4::rotate(v.eye, V3{0, 0, 1}, -delta_.x() * perPixel);
                if (right.length() > 0.5) turn = M4::rotate(v.eye, right, -delta_.y() * perPixel) * turn;
                v.target = turn.point(v.target);
                v.up = V3{0, 0, 1};
                view_->setView(v);
            }
            return;
        case ToolId::Polygon:
        case ToolId::PositionCamera:
        default: cur_ = infer(pos, tool_ == ToolId::Polygon ? start_ : std::nullopt); break;
    }
    preview();
}

void ToolController::extraRelease(QMouseEvent* e) {
    const QPointF delta = e->position() - pressPos_;
    const bool dragged = std::hypot(delta.x(), delta.y()) > 4;
    if (tool_ == ToolId::ZoomWindow && dragged) {
        const QRectF r = QRectF(pressPos_, e->position()).normalized();
        render::View v = view_->view();
        const V3 f = view_->eyeDirection();
        const double dist = (v.target - v.eye).length();
        const render::Ray ray = view_->rayAt(r.center());
        const auto t = geom::Plane::fromPointNormal(v.target, f).ray(ray.origin, ray.dir);
        const V3 c = t ? ray.origin + ray.dir * *t : v.target;
        const double ratio = std::max(r.width() / std::max(1, view_->width()), r.height() / std::max(1, view_->height()));
        v.target = c;
        v.eye = c - f * (dist * std::max(ratio, 0.01));
        v.orthoHeight *= std::max(ratio, 0.01);
        view_->setOverlay({});
        view_->setView(v, true);
        return;
    }
    if (tool_ == ToolId::Polygon && dragged && stage_ == 1) {
        cur_ = infer(e->position(), start_);
        finishPolygon(plane_->project(cur_.p));
    }
}

void ToolController::extraPreview(render::Overlay& o) {
    const QColor black(0, 0, 0);
    switch (tool_) {
        case ToolId::Arc:
            if (stage_ == 1 && start_ && plane_) {
                o.lines.push_back({*start_, plane_->project(cur_.p), black, 1.5f, false});
                emit measurement(tr("Length"), len(start_->dist(plane_->project(cur_.p))));
            } else if (stage_ == 2 && start_ && second_ && plane_) {
                const V3 w = plane_->n.cross((*second_ - *start_).normalized()).normalized();
                const auto pts = arcPoints(*start_, *second_, plane_->n, w, amount_, arcSegments_);
                for (std::size_t i = 0; i + 1 < pts.size(); ++i) o.lines.push_back({pts[i], pts[i + 1], black, 1.5f, false});
                emit measurement(tr("Bulge"), len(std::abs(amount_)));
            }
            break;
        case ToolId::Polygon:
            if (stage_ == 1 && start_ && plane_) {
                const V3 p = plane_->project(cur_.p);
                const double r = p.dist(*start_);
                const V3 u = (p - *start_).normalized(), v = plane_->n.cross(u).normalized();
                for (int i = 0; i < polygonSides_; ++i) {
                    const double a0 = 2 * std::numbers::pi * i / polygonSides_, a1 = 2 * std::numbers::pi * (i + 1) / polygonSides_;
                    o.lines.push_back({*start_ + (u * std::cos(a0) + v * std::sin(a0)) * r,
                                       *start_ + (u * std::cos(a1) + v * std::sin(a1)) * r, black, 1.5f, false});
                }
                emit measurement(tr("Radius"), len(r));
            }
            break;
        case ToolId::Rotate:
            if (stage_ >= 1 && plane_) {
                // A protractor circle around the centre.
                const double r = view_->pixelSize(pivot_) * 40.0;
                V3 u, v;
                geom::arbitraryAxes(plane_->n, u, v);
                for (int i = 0; i < 36; ++i) {
                    const double a0 = 2 * std::numbers::pi * i / 36, a1 = 2 * std::numbers::pi * (i + 1) / 36;
                    o.lines.push_back({pivot_ + (u * std::cos(a0) + v * std::sin(a0)) * r,
                                       pivot_ + (u * std::cos(a1) + v * std::sin(a1)) * r, QColor(0, 0, 255), 1.0f, false});
                }
                if (second_) {
                    o.lines.push_back({pivot_, *second_, black, 1.0f, true});
                    const V3 end = M4::rotate(pivot_, plane_->n, amount_).point(*second_);
                    o.lines.push_back({pivot_, end, black, 1.0f, true});
                    emit measurement(tr("Angle"), QStringLiteral("%1").arg(toDeg(amount_), 0, 'f', 1));
                } else {
                    o.lines.push_back({pivot_, plane_->project(cur_.p), black, 1.0f, true});
                }
            }
            break;
        case ToolId::Scale:
            if (stage_ == 1) {
                o.lines.push_back({pivot_, cur_.p, black, 1.0f, true});
                o.points.push_back({pivot_, QColor(255, 0, 0), 7.0f});
                emit measurement(tr("Scale"), QStringLiteral("%1").arg(amount_, 0, 'f', 2));
            }
            break;
        case ToolId::Offset:
            if (stage_ == 1 && offsetFace_) {
                if (const auto* face = doc_->model().get<skp::Face>(offsetFace_)) {
                    const V3 n = geom::Plane::fromSkp(face->plane).n.normalized();
                    const skp::Loop3 in = skp::offsetLoop(skp::faceOuter(doc_->model(), *face), n, amount_);
                    for (std::size_t i = 0; i < in.size(); ++i) o.lines.push_back({in[i], in[(i + 1) % in.size()], black, 1.5f, false});
                }
                emit measurement(tr("Distance"), len(amount_));
            }
            break;
        default: break;
    }
}

bool ToolController::extraTyped(const QString& text) {
    const ui::UnitFormat u = units();
    switch (tool_) {
        case ToolId::Arc:
            if (auto n = sideCount(text)) {
                arcSegments_ = *n;
                preview();
                return true;
            }
            if (stage_ == 2) {
                const auto b = ui::parseLength(text, u);
                if (!b) return false;
                finishArc(amount_ < 0 ? -*b : *b);
                return true;
            }
            return false;
        case ToolId::Polygon:
            if (auto n = sideCount(text)) {
                polygonSides_ = *n;
                preview();
                return true;
            }
            if (stage_ == 1 && start_ && plane_) {
                const auto r = ui::parseLength(text, u);
                if (!r || *r <= 0) return false;
                V3 dir = (plane_->project(cur_.p) - *start_).normalized();
                if (dir.length() < 0.5) {
                    V3 a, b;
                    geom::arbitraryAxes(plane_->n, a, b);
                    dir = a;
                }
                finishPolygon(*start_ + dir * *r);
                return true;
            }
            return false;
        case ToolId::Rotate:
            if (stage_ >= 1) {
                const auto d = plainNumber(text);
                if (!d) return false;
                commitRotate(toRad(*d));
                return true;
            }
            return false;
        case ToolId::Scale:
            if (stage_ == 1) {
                const auto d = plainNumber(text);
                if (!d || *d <= 0) return false;
                commitScale(*d);
                return true;
            }
            return false;
        case ToolId::Offset:
            if (stage_ == 1) {
                const auto d = ui::parseLength(text, u);
                if (!d || *d <= 0) return false;
                commitOffset(*d);
                return true;
            }
            return false;
        case ToolId::FieldOfView: {
            const auto d = plainNumber(text);
            if (!d || *d < 1 || *d > 120) return false;
            render::View v = view_->view();
            v.fovDeg = *d;
            view_->setView(v);
            return true;
        }
        default: return false;
    }
}

void ToolController::finishArc(double bulge) {
    if (!start_ || !second_ || !plane_) return;
    const V3 w = plane_->n.cross((*second_ - *start_).normalized()).normalized();
    const auto pts = arcPoints(*start_, *second_, plane_->n, w, bulge, arcSegments_);
    if (pts.size() >= 2) {
        doc_->checkpoint(tr("Arc"));
        for (std::size_t i = 0; i + 1 < pts.size(); ++i) skp::addEdge(doc_->model(), doc_->root(), pts[i], pts[i + 1]);
        doc_->edited();
    }
    cancel();
}

void ToolController::finishPolygon(const V3& p) {
    if (!start_ || !plane_) return;
    const double r = p.dist(*start_);
    if (r < kTol) return;
    const V3 n = plane_->n.normalized();
    const V3 u = (p - *start_).normalized(), v = n.cross(u).normalized();
    skp::Loop3 pts;
    for (int i = 0; i < polygonSides_; ++i) {
        const double a = 2 * std::numbers::pi * i / polygonSides_;
        pts.push_back(*start_ + (u * std::cos(a) + v * std::sin(a)) * r);
    }
    doc_->checkpoint(tr("Polygon"));
    const bool ground = std::abs(n.z) > 0.999 && std::abs(start_->z) < 1e-6;
    std::optional<skp::ObjId> made;
    if (const auto* root = skp::rootComponent(doc_->model())) {
        for (skp::ObjId id : root->ents) {
            const auto* face = doc_->model().get<skp::Face>(skp::Ref{id});
            if (face == nullptr) continue;
            const geom::Plane fp = geom::Plane::fromSkp(face->plane);
            if (std::abs(fp.n.dot(n)) > 0.9999 && std::abs(fp.dist(*start_)) < kTol) {
                made = skp::imprintFace(doc_->model(), doc_->root(), id, pts);
                if (made) break;
            }
        }
    }
    if (!made) skp::addFace(doc_->model(), doc_->root(), pts, {}, ground ? V3{0, 0, -1} : n);
    doc_->edited();
    cancel();
}

void ToolController::commitRotate(double angle) {
    if (targets_.empty() || !plane_ || std::abs(angle) < 1e-9) {
        cancel();
        return;
    }
    doc_->checkpoint(tr("Rotate"));
    skp::transformEntities(doc_->model(), doc_->root(), targets_, M4::rotate(pivot_, plane_->n, angle));
    doc_->edited();
    emit measurement(tr("Angle"), QStringLiteral("%1").arg(toDeg(angle), 0, 'f', 1));
    cancel();
}

void ToolController::commitScale(double factor) {
    if (targets_.empty() || factor <= 0 || std::abs(factor - 1.0) < 1e-9) {
        cancel();
        return;
    }
    M4 s;
    s.at(0, 0) = factor;
    s.at(1, 1) = factor;
    s.at(2, 2) = factor;
    doc_->checkpoint(tr("Scale"));
    skp::transformEntities(doc_->model(), doc_->root(), targets_, M4::translate(pivot_) * s * M4::translate(-pivot_));
    doc_->edited();
    cancel();
}

void ToolController::commitOffset(double distance) {
    if (!offsetFace_ || distance <= kTol) {
        cancel();
        return;
    }
    doc_->checkpoint(tr("Offset"));
    if (!skp::offsetFace(doc_->model(), doc_->root(), *offsetFace_, distance)) {
        emit status(tr("That offset doesn't fit inside the face."));
    }
    doc_->edited();
    cancel();
}

}  // namespace edit
