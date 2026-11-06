#include "skp/Builder.h"

#include "skp/Query.h"

#include <QUuid>

#include <algorithm>
#include <cmath>
#include <map>

namespace skp {

using geom::V3;

namespace {

constexpr double kMergeTolerance = 1e-3;  // inches; SketchUp's own tolerance is 0.001"

// Default CRenderingOptions values of a 2017 model (version 36 field order, as read from
// SketchUp's "Simple" template). R = null reference, numbers keep the field's stored type.
struct Def {
    char kind;  // R, U, B, C, F
    double n = 0;
    std::array<std::uint8_t, 4> c{0, 0, 0, 0};
};
const std::vector<Def>& renderingDefaults() {
    static const std::vector<Def> d{
        {'R'}, {'U', 0}, {'U', 2}, {'B', 0}, {'B', 1}, {'B', 0}, {'U', 1}, {'C', 0, {153, 189, 144, 255}},
        {'C', 0, {0, 0, 0, 255}}, {'C', 0, {0, 1, 255, 255}}, {'C', 0, {0, 0, 0, 255}}, {'B', 0}, {'B', 0}, {'B', 0},
        {'B', 1}, {'U', 1}, {'B', 0}, {'U', 3}, {'B', 1}, {'U', 2}, {'B', 0}, {'U', 4}, {'B', 0}, {'U', 9}, {'B', 0},
        {'B', 0}, {'U', 0}, {'C', 0, {255, 255, 255, 255}}, {'C', 0, {164, 178, 187, 255}}, {'F', 0.25}, {'F', 0.5},
        {'B', 0}, {'B', 0}, {'B', 0}, {'C', 0, {0, 0, 0, 0}}, {'B', 1}, {'F', -1}, {'F', -1}, {'U', 1}, {'U', 0},
        {'B', 1}, {'B', 1}, {'B', 1}, {'B', 0}, {'C', 0, {104, 195, 251, 255}}, {'C', 0, {0, 0, 0, 0}},
        {'C', 0, {210, 208, 185, 255}}, {'B', 1}, {'B', 0}, {'B', 1}, {'U', 50}, {'C', 0, {255, 135, 0, 255}},
        {'C', 0, {112, 105, 97, 255}}, {'C', 0, {0, 0, 0, 255}}, {'U', 3}, {'U', 2}, {'U', 0}, {'B', 1},
        {'F', 0.349066}, {'B', 1}, {'C', 0, {255, 0, 0, 255}}, {'B', 1}, {'F', 0.65}, {'B', 0}, {'B', 1}, {'F', 1},
        {'B', 1}, {'F', 0.8}};
    return d;
}

void applyDefault(Field& field, const Def& d) {
    std::visit(
        [&](auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, bool>) {
                if (d.kind == 'B') v = d.n != 0;
            } else if constexpr (std::is_same_v<T, Color>) {
                if (d.kind == 'C') v = Color{d.c[0], d.c[1], d.c[2], d.c[3]};
            } else if constexpr (std::is_same_v<T, double> || std::is_same_v<T, float>) {
                if (d.kind == 'F') v = static_cast<T>(d.n);
            } else if constexpr (std::is_integral_v<T>) {
                if (d.kind == 'U') v = static_cast<T>(d.n);
            }
        },
        field);
}

template <class T>
void setField(Record* r, std::size_t i, T v) {
    if (r == nullptr || i >= r->fields.size()) return;
    std::visit(
        [&](auto& cur) {
            using C = std::decay_t<decltype(cur)>;
            if constexpr (std::is_same_v<C, T>) {
                cur = v;
            } else if constexpr (std::is_arithmetic_v<C> && std::is_arithmetic_v<T> && !std::is_same_v<C, bool>) {
                cur = static_cast<C>(v);
            }
        },
        r->fields[i]);
}

Entity newEntity(SkpFile& f) {
    Entity e;
    e.pid = f.takePid();
    return e;
}

DrawElem newDrawElem(SkpFile& f) {
    DrawElem d;
    d.ent = newEntity(f);
    d.layer = layer0(f);
    return d;
}

V3 pos(const SkpFile& f, Ref v) {
    return V3::from(vertexPos(f, v));
}

// Vertices used by the edges of a component.
std::vector<ObjId> componentVertices(const SkpFile& f, const Component& c) {
    std::vector<ObjId> out;
    for (ObjId id : c.ents) {
        if (const auto* e = f.get<Edge>(id)) {
            for (Ref v : {e->v0, e->v1}) {
                if (v && std::find(out.begin(), out.end(), *v) == out.end()) out.push_back(*v);
            }
        }
    }
    return out;
}

ObjId findOrAddVertex(SkpFile& f, ObjId comp, const V3& p) {
    if (const Component* c = componentOf(f, comp)) {
        for (ObjId v : componentVertices(f, *c)) {
            if (pos(f, v).dist(p) < kMergeTolerance) return v;
        }
    }
    Vertex v;
    v.ent = newEntity(f);
    v.pos = p.arr();
    return f.add(Object{v});
}

std::optional<ObjId> edgeBetween(const SkpFile& f, const Component& c, ObjId a, ObjId b) {
    for (ObjId id : c.ents) {
        if (const auto* e = f.get<Edge>(id)) {
            if ((e->v0 == a && e->v1 == b) || (e->v0 == b && e->v1 == a)) return id;
        }
    }
    return std::nullopt;
}

std::vector<ObjId> facesUsingEdge(const SkpFile& f, const Component& c, ObjId edge) {
    std::vector<ObjId> out;
    for (ObjId id : c.ents) {
        const auto* face = f.get<Face>(id);
        if (face == nullptr) continue;
        bool uses = false;
        for (ObjId l : face->loops) {
            const auto* loop = f.get<Loop>(l);
            if (loop == nullptr) continue;
            for (ObjId u : loop->uses) {
                const auto* eu = f.get<EdgeUse>(u);
                if (eu != nullptr && eu->edge == edge) uses = true;
            }
        }
        if (uses) out.push_back(id);
    }
    return out;
}

std::vector<ObjId> faceEdges(const SkpFile& f, const Face& face) {
    std::vector<ObjId> out;
    for (ObjId l : face.loops) {
        if (const auto* loop = f.get<Loop>(l)) {
            for (ObjId u : loop->uses) {
                if (const auto* eu = f.get<EdgeUse>(u); eu != nullptr && eu->edge) out.push_back(*eu->edge);
            }
        }
    }
    return out;
}

Loop3 loopPts(const SkpFile& f, ObjId loop) {
    Loop3 out;
    for (const Vec3d& p : loopPoints(f, loop)) out.push_back(V3::from(p));
    return out;
}

bool collinearRemoved(Loop3& pts) {
    // Drops repeated and collinear corners; returns false when fewer than 3 remain.
    Loop3 out;
    for (std::size_t i = 0; i < pts.size(); ++i) {
        const V3& p = pts[i];
        if (!out.empty() && out.back().dist(p) < kMergeTolerance) continue;
        out.push_back(p);
    }
    while (out.size() > 1 && out.front().dist(out.back()) < kMergeTolerance) out.pop_back();
    bool changed = true;
    while (changed && out.size() >= 3) {
        changed = false;
        for (std::size_t i = 0; i < out.size(); ++i) {
            const V3& a = out[(i + out.size() - 1) % out.size()];
            const V3& b = out[i];
            const V3& c = out[(i + 1) % out.size()];
            if ((b - a).cross(c - b).length() < 1e-9 * std::max(1.0, (c - a).length())) {
                out.erase(out.begin() + static_cast<std::ptrdiff_t>(i));
                changed = true;
                break;
            }
        }
    }
    pts = std::move(out);
    return pts.size() >= 3;
}

ObjId makeLoop(SkpFile& f, ObjId comp, const Loop3& pts, bool outer) {
    Loop loop;
    loop.ent = newEntity(f);
    loop.b0 = outer;  // the shipped files mark the first (outer) loop with true
    const ObjId loopId = f.add(Object{loop});
    std::vector<ObjId> uses;
    for (std::size_t i = 0; i < pts.size(); ++i) {
        const ObjId a = findOrAddVertex(f, comp, pts[i]);
        const ObjId b = findOrAddVertex(f, comp, pts[(i + 1) % pts.size()]);
        Ref edgeId;
        if (const Component* c = componentOf(f, comp)) edgeId = edgeBetween(f, *c, a, b);
        if (!edgeId) {
            Edge e;
            e.de = newDrawElem(f);
            e.v0 = a;
            e.v1 = b;
            edgeId = f.add(Object{e});
            if (Component* c = componentOf(f, comp)) c->ents.push_back(*edgeId);
        }
        const auto* e = f.get<Edge>(edgeId);
        EdgeUse eu;
        eu.ent = newEntity(f);
        eu.edge = edgeId;
        eu.reversed = e != nullptr && e->v0 != a;
        eu.loop = loopId;
        uses.push_back(f.add(Object{eu}));
    }
    if (auto* l = f.get<Loop>(loopId)) l->uses = std::move(uses);
    return loopId;
}

void refreshPlane(SkpFile& f, ObjId faceId) {
    auto* face = f.get<Face>(faceId);
    if (face == nullptr || face->loops.empty()) return;
    const Loop3 pts = loopPts(f, face->loops.front());
    const V3 n = geom::newell(pts);
    if (n.length() < 1e-12 || pts.empty()) return;
    face->plane = geom::Plane::fromPointNormal(pts.front(), n).toSkp();
}

}  // namespace

Ref layer0(const SkpFile& f) {
    const auto ls = layers(f);
    if (ls.empty()) return std::nullopt;
    return ls.front();
}

SkpFile newModel() {
    SkpFile f;
    f.header = QStringLiteral("SketchUp Model");
    f.appVersion = QStringLiteral("{17.0.10349}");
    for (const auto& [k, v] : versions2017()) f.versionMap.emplace_back(k, v);
    const QByteArray uuid = QUuid::createUuid().toRfc4122();
    for (std::size_t i = 0; i < f.guid.size() && i < static_cast<std::size_t>(uuid.size()); ++i) {
        f.guid[i] = static_cast<std::uint8_t>(uuid[static_cast<qsizetype>(i)]);
    }
    Camera cam;  // the "Simple" template's opening view
    cam.eye = {240.719, -236.069, 102.276};
    cam.target = {-285.372, 553.004, -82.0446};
    cam.up = {-0.105834, 0.158739, 0.981632};
    cam.fov = 35;
    cam.height = 133;
    f.camera = f.add(Object{cam});

    // One pass of the writer creates every part in SketchUp's order with zero values.
    (void)writeSkp(f, true);

    if (Record* ro = record(f, f.rendering)) {
        const auto& d = renderingDefaults();
        for (std::size_t i = 0; i < ro->fields.size() && i < d.size(); ++i) applyDefault(ro->fields[i], d[i]);
    }
    if (Record* sh = record(f, f.shadow)) {
        // The template's shadow settings: Boulder (CO), Nov 8 13:30, light 80, dark 45.
        setField(sh, 2, std::uint32_t{1383917400});
        if (sh->fields.size() > 5) {
            sh->fields[4] = QStringLiteral("Boulder (CO)");
            sh->fields[5] = QStringLiteral("USA");
        }
        setField(sh, 6, -105.242);
        setField(sh, 7, 40.0183);
        setField(sh, 8, -7.0);
        setField(sh, 10, 1.0);
        setField(sh, 13, true);
        setField(sh, 14, true);
        setField(sh, 16, std::uint32_t{80});
        setField(sh, 17, std::uint32_t{45});
    }
    if (Record* axes = record(f, f.axes)) {
        // CSketchCS: drawing element, then origin and the three axis vectors.
        const std::size_t base = axes->fields.size() >= 12 ? axes->fields.size() - 12 : 0;
        const std::array<double, 12> v{0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1};
        for (std::size_t i = 0; i < 12; ++i) setField(axes, base + i, v[i]);
        for (std::size_t i = 4; i < 6 && i < axes->fields.size(); ++i) setField(axes, i, true);
    }
    if (Record* beh = record(f, f.behavior)) (void)beh;

    // Layer0, red-ish like SketchUp's own.
    Component* root = rootComponent(f);
    if (root != nullptr) {
        if (auto* lm = f.get<LayerManager>(root->layers)) {
            Layer l;
            l.ent = newEntity(f);
            l.name = QStringLiteral("Layer0");
            Material lmat;
            lmat.name = l.name;
            lmat.color = Color{255, 84, 84, 255};
            l.material = f.add(Object{lmat});
            const ObjId lid = f.add(Object{l});
            if (auto* lm2 = f.get<LayerManager>(rootComponent(f)->layers)) {
                lm2->layers.push_back(lid);
                lm2->active = lid;
            }
            (void)lm;
        }
    }
    return f;
}

std::optional<ObjId> addEdge(SkpFile& f, ObjId comp, const V3& a, const V3& b) {
    if (a.dist(b) < kMergeTolerance) return std::nullopt;
    const ObjId va = findOrAddVertex(f, comp, a);
    const ObjId vb = findOrAddVertex(f, comp, b);
    if (const Component* c = componentOf(f, comp)) {
        if (auto e = edgeBetween(f, *c, va, vb)) return e;
    }
    Edge e;
    e.de = newDrawElem(f);
    e.v0 = va;
    e.v1 = vb;
    const ObjId id = f.add(Object{e});
    if (Component* c = componentOf(f, comp)) c->ents.push_back(id);
    return id;
}

std::optional<ObjId> addFace(SkpFile& f, ObjId comp, const Loop3& outerIn, const std::vector<Loop3>& holesIn,
                             std::optional<V3> towards) {
    Loop3 outer = outerIn;
    if (!collinearRemoved(outer)) return std::nullopt;
    V3 n = geom::newell(outer);
    if (n.length() < 1e-12) return std::nullopt;
    n = n.normalized();
    const geom::Plane pl = geom::Plane::fromPointNormal(outer.front(), n);
    for (const V3& p : outer) {
        if (std::abs(pl.dist(p)) > 1e-3) return std::nullopt;  // not planar
    }
    if (towards && n.dot(*towards) < 0) {
        std::reverse(outer.begin(), outer.end());
        n = -n;
    }
    std::vector<Loop3> holes;
    for (Loop3 h : holesIn) {
        if (!collinearRemoved(h)) continue;
        if (geom::newell(h).dot(n) > 0) std::reverse(h.begin(), h.end());  // inner loops wind the other way
        holes.push_back(std::move(h));
    }
    Face face;
    face.de = newDrawElem(f);
    face.plane = geom::Plane::fromPointNormal(outer.front(), n).toSkp();
    const ObjId faceId = f.add(Object{face});
    std::vector<ObjId> loops{makeLoop(f, comp, outer, true)};
    for (const Loop3& h : holes) loops.push_back(makeLoop(f, comp, h, false));
    if (auto* fc = f.get<Face>(faceId)) fc->loops = std::move(loops);
    if (Component* c = componentOf(f, comp)) c->ents.push_back(faceId);
    return faceId;
}

std::optional<ObjId> imprintFace(SkpFile& f, ObjId comp, ObjId host, const Loop3& loopIn) {
    const auto* face = f.get<Face>(Ref{host});
    if (face == nullptr) return std::nullopt;
    Loop3 loop = loopIn;
    if (!collinearRemoved(loop)) return std::nullopt;
    const geom::Plane pl = geom::Plane::fromSkp(face->plane);
    V3 xa, ya;
    geom::arbitraryAxes(pl.n, xa, ya);
    auto flat = [&](const Loop3& l) {
        std::vector<std::array<double, 2>> out;
        for (const V3& p : l) out.push_back({p.dot(xa), p.dot(ya)});
        return out;
    };
    auto inside = [](const std::array<double, 2>& p, const std::vector<std::array<double, 2>>& poly) {
        bool in = false;
        for (std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
            const auto& a = poly[i];
            const auto& b = poly[j];
            if (((a[1] > p[1]) != (b[1] > p[1])) && (p[0] < (b[0] - a[0]) * (p[1] - a[1]) / (b[1] - a[1]) + a[0])) {
                in = !in;
            }
        }
        return in;
    };
    for (const V3& p : loop) {
        if (std::abs(pl.dist(p)) > 1e-3) return std::nullopt;
    }
    const auto outer = flat(faceOuter(f, *face));
    const auto pts = flat(loop);
    // Every corner strictly inside the outer loop (a little in from its edges) and outside the holes.
    for (const auto& p : pts) {
        if (!inside(p, outer)) return std::nullopt;
        for (const Loop3& h : faceHoles(f, *face)) {
            if (inside(p, flat(h))) return std::nullopt;
        }
    }
    const Ref frontMat = face->de.material, backMat = face->back;
    Loop3 hole = loop;
    if (geom::newell(hole).dot(pl.n) > 0) std::reverse(hole.begin(), hole.end());
    const ObjId holeLoop = makeLoop(f, comp, hole, false);
    if (auto* hf = f.get<Face>(Ref{host})) hf->loops.push_back(holeLoop);
    auto inner = addFace(f, comp, loop, {}, pl.n);
    if (inner) {
        if (auto* nf = f.get<Face>(inner)) {
            nf->de.material = frontMat;
            nf->back = backMat;
        }
    }
    return inner;
}

void reverseFace(SkpFile& f, ObjId faceId) {
    auto* face = f.get<Face>(faceId);
    if (face == nullptr) return;
    for (ObjId l : face->loops) {
        auto* loop = f.get<Loop>(l);
        if (loop == nullptr) continue;
        std::reverse(loop->uses.begin(), loop->uses.end());
        for (ObjId u : loop->uses) {
            if (auto* eu = f.get<EdgeUse>(u)) eu->reversed = !eu->reversed;
        }
    }
    for (double& x : face->plane) x = -x;
    std::swap(face->de.material, face->back);
}

Loop3 faceOuter(const SkpFile& f, const Face& face) {
    return face.loops.empty() ? Loop3{} : loopPts(f, face.loops.front());
}

std::vector<Loop3> faceHoles(const SkpFile& f, const Face& face) {
    std::vector<Loop3> out;
    for (std::size_t i = 1; i < face.loops.size(); ++i) out.push_back(loopPts(f, face.loops[i]));
    return out;
}

double faceArea(const SkpFile& f, const Face& face) {
    double a = geom::newell(faceOuter(f, face)).length() * 0.5;
    for (const Loop3& h : faceHoles(f, face)) a -= geom::newell(h).length() * 0.5;
    return std::max(0.0, a);
}

bool pushPull(SkpFile& f, ObjId comp, ObjId faceId, double distance) {
    const auto* face = f.get<Face>(faceId);
    const Component* c = componentOf(f, comp);
    if (face == nullptr || c == nullptr || std::abs(distance) < kMergeTolerance) return false;
    const V3 n = geom::Plane::fromSkp(face->plane).n.normalized();
    const V3 offset = n * distance;

    // Faces next to this one across its edges.
    std::set<ObjId> neighbours;
    for (ObjId e : faceEdges(f, *face)) {
        for (ObjId other : facesUsingEdge(f, *c, e)) {
            if (other != faceId) neighbours.insert(other);
        }
    }
    const bool allSquare = !neighbours.empty() && std::all_of(neighbours.begin(), neighbours.end(), [&](ObjId o) {
        const auto* of = f.get<Face>(o);
        return of != nullptr && std::abs(geom::Plane::fromSkp(of->plane).n.normalized().dot(n)) < 1e-6;
    });
    if (allSquare) {
        // Side faces contain the push direction, so moving this face's corners keeps them flat.
        std::set<ObjId> verts;
        for (ObjId l : face->loops) {
            for (ObjId v : loopVertices(f, l)) verts.insert(v);
        }
        for (ObjId v : verts) {
            if (auto* vx = f.get<Vertex>(v)) vx->pos = (V3::from(vx->pos) + offset).arr();
        }
        refreshPlane(f, faceId);
        return true;
    }

    const Loop3 outer = faceOuter(f, *face);
    const std::vector<Loop3> holes = faceHoles(f, *face);
    const Ref frontMat = face->de.material;
    const double s = distance > 0 ? 1.0 : -1.0;
    // Walls: one quad per loop edge; for a loop wound counter-clockwise about n, edge × n points out.
    auto walls = [&](const Loop3& loop) {
        for (std::size_t i = 0; i < loop.size(); ++i) {
            const V3 a = loop[i], b = loop[(i + 1) % loop.size()];
            const V3 out = (b - a).cross(n);
            Loop3 quad{a, b, b + offset, a + offset};
            if (auto w = addFace(f, comp, quad, {}, out)) {
                if (auto* wf = f.get<Face>(*w)) wf->de.material = frontMat;
            }
        }
    };
    walls(outer);
    for (const Loop3& h : holes) walls(h);
    Loop3 top;
    for (const V3& p : outer) top.push_back(p + offset);
    std::vector<Loop3> topHoles;
    for (const Loop3& h : holes) {
        Loop3 t;
        for (const V3& p : h) t.push_back(p + offset);
        topHoles.push_back(std::move(t));
    }
    if (auto t = addFace(f, comp, top, topHoles, n * s)) {
        if (auto* tf = f.get<Face>(*t)) tf->de.material = frontMat;
    }
    if (!neighbours.empty()) {
        erase(f, comp, {faceId});
    } else if (distance > 0) {
        reverseFace(f, faceId);  // its front now points into the new solid
    }
    return true;
}

void erase(SkpFile& f, ObjId comp, const std::set<ObjId>& ents) {
    Component* c = componentOf(f, comp);
    if (c == nullptr) return;
    std::set<ObjId> gone = ents;
    for (ObjId id : ents) {
        if (f.get<Edge>(Ref{id}) != nullptr) {
            for (ObjId face : facesUsingEdge(f, *c, id)) gone.insert(face);
        }
    }
    std::erase_if(c->ents, [&](ObjId id) { return gone.contains(id); });
}

void translate(SkpFile& f, ObjId comp, const std::set<ObjId>& ents, const V3& delta) {
    Component* c = componentOf(f, comp);
    if (c == nullptr) return;
    std::set<ObjId> verts;
    for (ObjId id : ents) {
        if (auto* inst = f.get<Instance>(Ref{id})) {
            inst->xform[9] += delta.x * inst->xform[12];
            inst->xform[10] += delta.y * inst->xform[12];
            inst->xform[11] += delta.z * inst->xform[12];
        } else if (const auto* e = f.get<Edge>(Ref{id})) {
            if (e->v0) verts.insert(*e->v0);
            if (e->v1) verts.insert(*e->v1);
        } else if (const auto* face = f.get<Face>(Ref{id})) {
            for (ObjId l : face->loops) {
                for (ObjId v : loopVertices(f, l)) verts.insert(v);
            }
        }
    }
    for (ObjId v : verts) {
        if (auto* vx = f.get<Vertex>(v)) vx->pos = (V3::from(vx->pos) + delta).arr();
    }
    if (verts.empty()) return;
    for (ObjId id : c->ents) {
        if (f.get<Face>(Ref{id}) != nullptr) refreshPlane(f, id);
    }
}

void paint(SkpFile& f, ObjId ent, Ref material) {
    if (auto* face = f.get<Face>(Ref{ent})) {
        face->de.material = material;
    } else if (auto* inst = f.get<Instance>(Ref{ent})) {
        inst->de.material = material;
    }
}

ObjId addMaterial(SkpFile& f, const QString& name, Color color) {
    Material m;
    m.ent = newEntity(f);
    m.name = name;
    m.color = color;
    const ObjId id = f.add(Object{m});
    if (Component* root = rootComponent(f)) {
        if (auto* mm = f.get<MaterialManager>(root->materials)) mm->mats.push_back(id);
    }
    return id;
}

ObjId addLayer(SkpFile& f, const QString& name) {
    Layer l;
    l.ent = newEntity(f);
    l.name = name;
    Material lm;
    lm.name = name;
    // SketchUp gives each new layer its own colour; a fixed palette stands in for its choice.
    static const std::array<Color, 6> palette{Color{84, 160, 255, 255}, Color{255, 190, 84, 255},
                                              Color{120, 220, 120, 255}, Color{200, 120, 255, 255},
                                              Color{255, 120, 180, 255}, Color{90, 210, 210, 255}};
    lm.color = palette[layers(f).size() % palette.size()];
    l.material = f.add(Object{lm});
    const ObjId id = f.add(Object{l});
    if (Component* root = rootComponent(f)) {
        if (auto* lmg = f.get<LayerManager>(root->layers)) lmg->layers.push_back(id);
    }
    return id;
}

namespace {
std::optional<ObjId> wrapEntities(SkpFile& f, const std::set<ObjId>& ents, InstanceKind kind, const QString& name) {
    Component* root = rootComponent(f);
    if (root == nullptr || ents.empty() || !f.root) return std::nullopt;
    // Faces bring their edges along.
    std::set<ObjId> take = ents;
    for (ObjId id : ents) {
        if (const auto* face = f.get<Face>(Ref{id})) {
            for (ObjId e : faceEdges(f, *face)) take.insert(e);
        }
    }
    Definition d;
    d.comp.de = newDrawElem(f);
    d.name = name;
    const QByteArray uuid = QUuid::createUuid().toRfc4122();
    for (std::size_t i = 0; i < d.guid.size() && i < static_cast<std::size_t>(uuid.size()); ++i) {
        d.guid[i] = static_cast<std::uint8_t>(uuid[static_cast<qsizetype>(i)]);
    }
    for (ObjId id : root->ents) {
        if (take.contains(id)) d.comp.ents.push_back(id);
    }
    // Definitions get their behaviour record from a template definition when the model has one.
    for (ObjId other : definitions(f)) {
        if (const auto* od = f.get<Definition>(other); od != nullptr && od->behavior) {
            if (const Record* r = record(f, od->behavior)) {
                Record copy = *r;
                if (copy.fields.size() > 5) copy.fields[5] = std::uint8_t{0};
                if (copy.fields.size() > 1) copy.fields[1] = f.takePid();
                d.behavior = f.add(Object{copy});
            }
            break;
        }
    }
    if (!d.behavior) {
        Record b{"CComponentBehavior", {Ref{}, f.takePid(), false, false, std::uint32_t{0}, std::uint8_t{0},
                                        std::uint32_t{0}}};
        d.behavior = f.add(Object{b});
    }
    const ObjId defId = f.add(Object{d});
    root = rootComponent(f);
    if (root == nullptr) return std::nullopt;
    if (auto* dl = f.get<DefinitionList>(root->defs)) dl->defs.push_back(defId);
    std::erase_if(root->ents, [&](ObjId id) { return take.contains(id); });
    Instance inst;
    inst.kind = kind;
    inst.de = newDrawElem(f);
    inst.def = defId;
    const ObjId instId = f.add(Object{inst});
    if (Component* r2 = rootComponent(f)) r2->ents.push_back(instId);
    return instId;
}
}  // namespace

std::optional<ObjId> makeGroup(SkpFile& f, const std::set<ObjId>& ents) {
    return wrapEntities(f, ents, InstanceKind::Group, QStringLiteral("Group#%1").arg(definitions(f).size() + 1));
}

std::optional<ObjId> makeComponent(SkpFile& f, const std::set<ObjId>& ents, const QString& name) {
    return wrapEntities(f, ents, InstanceKind::Component, name);
}

void transformEntities(SkpFile& f, ObjId comp, const std::set<ObjId>& ents, const geom::M4& m) {
    Component* c = componentOf(f, comp);
    if (c == nullptr) return;
    std::set<ObjId> verts;
    for (ObjId id : ents) {
        if (auto* inst = f.get<Instance>(Ref{id})) {
            inst->xform = (m * geom::M4::fromSkp(inst->xform)).toSkp();
        } else if (const auto* e = f.get<Edge>(Ref{id})) {
            if (e->v0) verts.insert(*e->v0);
            if (e->v1) verts.insert(*e->v1);
        } else if (const auto* face = f.get<Face>(Ref{id})) {
            for (ObjId l : face->loops) {
                for (ObjId v : loopVertices(f, l)) verts.insert(v);
            }
        }
    }
    for (ObjId v : verts) {
        if (auto* vx = f.get<Vertex>(v)) vx->pos = m.point(V3::from(vx->pos)).arr();
    }
    if (verts.empty()) return;
    for (ObjId id : c->ents) {
        if (f.get<Face>(Ref{id}) != nullptr) refreshPlane(f, id);
    }
}

Loop3 offsetLoop(const Loop3& loop, const V3& n, double distance) {
    // Each edge moves inward (n × edge for a counter-clockwise loop) and neighbours meet again.
    const std::size_t k = loop.size();
    if (k < 3) return {};
    Loop3 out;
    for (std::size_t i = 0; i < k; ++i) {
        const V3 a = loop[(i + k - 1) % k], b = loop[i], c = loop[(i + 1) % k];
        const V3 e1 = (b - a).normalized(), e2 = (c - b).normalized();
        const V3 in1 = n.cross(e1).normalized(), in2 = n.cross(e2).normalized();
        const V3 bis = in1 + in2;
        const double cosHalf = bis.normalized().dot(in1);
        if (bis.length() < 1e-9 || std::abs(cosHalf) < 1e-6) return {};
        out.push_back(b + bis.normalized() * (distance / cosHalf));
    }
    // A collapsed offset flips the winding.
    if (geom::newell(out).dot(n) <= 0) return {};
    return out;
}

std::optional<ObjId> offsetFace(SkpFile& f, ObjId comp, ObjId faceId, double distance) {
    const auto* face = f.get<Face>(Ref{faceId});
    if (face == nullptr || distance <= 0) return std::nullopt;
    const V3 n = geom::Plane::fromSkp(face->plane).n.normalized();
    const Loop3 inner = offsetLoop(faceOuter(f, *face), n, distance);
    if (inner.empty()) return std::nullopt;
    return imprintFace(f, comp, faceId, inner);
}

std::optional<ObjId> addInstance(SkpFile& f, ObjId definition, const geom::M4& xform) {
    if (f.get<Definition>(Ref{definition}) == nullptr) return std::nullopt;
    Instance inst;
    inst.kind = InstanceKind::Component;
    inst.de = newDrawElem(f);
    inst.def = definition;
    inst.xform = xform.toSkp();
    const ObjId id = f.add(Object{inst});
    if (Component* root = rootComponent(f)) root->ents.push_back(id);
    return id;
}

ObjId addScene(SkpFile& f, const QString& name, const Camera& cam) {
    // CViewPage (version 12) storing only its camera (flags = 1), then its trailing fields.
    const ObjId camId = f.add(Object{cam});
    Record page{"CViewPage",
                {Ref{}, f.takePid(), name, QString(), std::uint32_t{1}, Ref{camId}, false, -1.0, -1.0, Ref{}, false,
                 false}};
    const ObjId pageId = f.add(Object{page});
    if (Record* pl = record(f, f.pages); pl != nullptr && pl->fields.size() >= 4) {
        const std::uint32_t n = recU32(pl, 2).value_or(0);
        const std::size_t at = std::min<std::size_t>(3 + n, pl->fields.size());
        pl->fields.insert(pl->fields.begin() + static_cast<std::ptrdiff_t>(at), Ref{pageId});
        pl->fields[2] = n + 1;
    }
    return pageId;
}

std::optional<Camera> sceneCamera(const SkpFile& f, ObjId page) {
    const Record* r = record(f, Ref{page});
    if (r == nullptr || r->cls != "CViewPage") return std::nullopt;
    const std::uint32_t flags = recU32(r, 4).value_or(0);
    if ((flags & 1) == 0 || r->fields.size() < 6) return std::nullopt;
    const auto* ref = std::get_if<Ref>(&r->fields[5]);
    const auto* cam = ref ? f.get<Camera>(*ref) : nullptr;
    if (cam == nullptr) return std::nullopt;
    return *cam;
}

QString sceneName(const SkpFile& f, ObjId page) {
    return recString(record(f, Ref{page}), 2).value_or(QString());
}

}  // namespace skp
