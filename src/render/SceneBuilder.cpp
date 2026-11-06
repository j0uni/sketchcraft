#include "render/SceneBuilder.h"

#include "geom/Triangulate.h"
#include "skp/Query.h"
#include "skp/Records.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace render {

using geom::M4;
using geom::V3;
using skp::ObjId;
using skp::Ref;

namespace {

using Mat3 = std::array<double, 9>;  // row-major

std::optional<Mat3> inverse3(const Mat3& m) {
    const double d = m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) +
                     m[2] * (m[3] * m[7] - m[4] * m[6]);
    if (std::abs(d) < 1e-300) return std::nullopt;
    return Mat3{(m[4] * m[8] - m[5] * m[7]) / d, (m[2] * m[7] - m[1] * m[8]) / d, (m[1] * m[5] - m[2] * m[4]) / d,
                (m[5] * m[6] - m[3] * m[8]) / d, (m[0] * m[8] - m[2] * m[6]) / d, (m[2] * m[3] - m[0] * m[5]) / d,
                (m[3] * m[7] - m[4] * m[6]) / d, (m[1] * m[6] - m[0] * m[7]) / d, (m[0] * m[4] - m[1] * m[3]) / d};
}

double fieldF64(const skp::Record* r, std::size_t i, double fallback) {
    return skp::recF64(r, i).value_or(fallback);
}

// Front and back flag words follow the two push-pin lists (CFaceTextureCoords::Serialize 0xb5914).
std::uint32_t coordFlags(const skp::Record* r, bool front) {
    std::size_t i = 27;
    for (int side = 0; side < 2; ++side) {
        const std::uint32_t n = skp::recU32(r, i).value_or(0);
        i += 1 + 4 * static_cast<std::size_t>(n);
    }
    return skp::recU32(r, front ? i : i + 1).value_or(front ? 1u : 0u);
}

struct LocalFace {
    ObjId id = 0;
    const skp::Face* face = nullptr;
    V3 normal;
    std::vector<V3> tri;   // three per triangle, wound counter-clockwise about the normal
    std::vector<V3> triN;  // smoothed normal per triangle corner
};

struct LocalEdge {
    ObjId id = 0;
    V3 a, b, n1, n2;
    int faces = 0;
    bool hidden = false, soft = false;
    Ref layer;
};

struct LocalComp {
    std::vector<LocalFace> faces;
    std::vector<LocalEdge> edges;
    std::vector<ObjId> instances;
    std::vector<ObjId> guides;
};

// Union-find over face indices for smoothing groups.
struct Groups {
    std::vector<std::size_t> parent;
    explicit Groups(std::size_t n) : parent(n) {
        for (std::size_t i = 0; i < n; ++i) parent[i] = i;
    }
    std::size_t find(std::size_t x) {
        while (parent[x] != x) {
            parent[x] = parent[parent[x]];
            x = parent[x];
        }
        return x;
    }
    void join(std::size_t a, std::size_t b) { parent[find(a)] = find(b); }
};

class SceneMaker {
public:
    SceneMaker(const skp::SkpFile& f, const BuildOptions& o, Scene& s) : f_(f), o_(o), s_(s) {}

    void run() {
        const skp::Component* root = skp::rootComponent(f_);
        if (root == nullptr || !f_.root) return;
        drawComp(*f_.root, M4::identity(), Ref{}, std::nullopt, false, 0);
    }

private:
    const LocalComp& local(ObjId compId) {
        if (auto it = cache_.find(compId); it != cache_.end()) return it->second;
        LocalComp lc;
        const skp::Component* comp = skp::componentOf(f_, compId);
        if (comp != nullptr) buildLocal(*comp, lc);
        return cache_.emplace(compId, std::move(lc)).first->second;
    }

    void buildLocal(const skp::Component& comp, LocalComp& lc) {
        // Edges first, so faces can record which edges they use.
        std::map<ObjId, std::size_t> edgeIndex;
        for (ObjId id : comp.ents) {
            if (id >= f_.objs.size()) continue;
            if (const auto* e = std::get_if<skp::Edge>(&f_.objs[id])) {
                LocalEdge le;
                le.id = id;
                le.a = V3::from(skp::vertexPos(f_, e->v0));
                le.b = V3::from(skp::vertexPos(f_, e->v1));
                le.hidden = e->de.flags[skp::kHidden];
                le.soft = e->de.flags[3];  // CEdge::IsSoft 0x98608: drawing-element flag bit 3
                le.layer = e->de.layer;
                edgeIndex[id] = lc.edges.size();
                lc.edges.push_back(le);
            } else if (const auto* rec = std::get_if<skp::Record>(&f_.objs[id]);
                       rec != nullptr && (rec->cls == "CConstructionLine" || rec->cls == "CConstructionPoint")) {
                lc.guides.push_back(id);
            } else if (std::holds_alternative<skp::Instance>(f_.objs[id])) {
                lc.instances.push_back(id);
            }
        }
        struct FaceInfo {
            std::vector<ObjId> verts;     // corner vertex per triangle corner
            std::vector<ObjId> edges;
        };
        std::vector<FaceInfo> infos;
        for (ObjId id : comp.ents) {
            if (id >= f_.objs.size()) continue;
            const auto* face = std::get_if<skp::Face>(&f_.objs[id]);
            if (face == nullptr || face->loops.empty()) continue;
            LocalFace lf;
            lf.id = id;
            lf.face = face;
            const geom::Plane pl = geom::Plane::fromSkp(face->plane);
            lf.normal = pl.n.normalized();
            V3 xa, ya;
            geom::arbitraryAxes(lf.normal, xa, ya);
            std::vector<V3> pts;
            std::vector<ObjId> vids;
            std::vector<geom::P2> outer;
            std::vector<std::vector<geom::P2>> holes;
            FaceInfo info;
            for (std::size_t li = 0; li < face->loops.size(); ++li) {
                const std::vector<ObjId> lv = skp::loopVertices(f_, face->loops[li]);
                std::vector<geom::P2> ring;
                for (ObjId v : lv) {
                    const V3 p = V3::from(skp::vertexPos(f_, v));
                    pts.push_back(p);
                    vids.push_back(v);
                    ring.push_back({p.dot(xa), p.dot(ya)});
                }
                if (li == 0) outer = std::move(ring); else holes.push_back(std::move(ring));
                if (const auto* loop = f_.get<skp::Loop>(face->loops[li])) {
                    for (ObjId u : loop->uses) {
                        if (const auto* eu = f_.get<skp::EdgeUse>(u); eu != nullptr && eu->edge) {
                            info.edges.push_back(*eu->edge);
                        }
                    }
                }
            }
            for (const geom::Tri& t : geom::triangulate(outer, holes)) {
                for (std::size_t k : t) {
                    if (k >= pts.size()) continue;
                    lf.tri.push_back(pts[k]);
                    info.verts.push_back(vids[k]);
                }
            }
            if (lf.tri.size() % 3 != 0) lf.tri.resize(lf.tri.size() / 3 * 3);
            info.verts.resize(lf.tri.size());
            lc.faces.push_back(std::move(lf));
            infos.push_back(std::move(info));
        }
        // Edge to face links, and smoothing groups across smooth edges (CEdge::IsSmooth 0x9b6e0, bit 4).
        Groups groups(lc.faces.size());
        std::map<ObjId, std::vector<std::size_t>> edgeFaces;
        for (std::size_t fi = 0; fi < lc.faces.size(); ++fi) {
            for (ObjId e : infos[fi].edges) {
                auto& v = edgeFaces[e];
                if (std::find(v.begin(), v.end(), fi) == v.end()) v.push_back(fi);
            }
        }
        for (const auto& [eid, fs] : edgeFaces) {
            if (auto it = edgeIndex.find(eid); it != edgeIndex.end()) {
                LocalEdge& le = lc.edges[it->second];
                le.faces = static_cast<int>(fs.size());
                if (!fs.empty()) le.n1 = lc.faces[fs[0]].normal;
                if (fs.size() > 1) le.n2 = lc.faces[fs[1]].normal;
            }
            const auto* e = f_.get<skp::Edge>(eid);
            if (e != nullptr && e->de.flags[4]) {
                for (std::size_t k = 1; k < fs.size(); ++k) groups.join(fs[0], fs[k]);
            }
        }
        std::map<std::pair<std::size_t, ObjId>, V3> sums;
        for (std::size_t fi = 0; fi < lc.faces.size(); ++fi) {
            const std::size_t g = groups.find(fi);
            std::set<ObjId> seen;
            for (ObjId v : infos[fi].verts) {
                if (seen.insert(v).second) sums[{g, v}] += lc.faces[fi].normal;
            }
        }
        for (std::size_t fi = 0; fi < lc.faces.size(); ++fi) {
            const std::size_t g = groups.find(fi);
            LocalFace& lf = lc.faces[fi];
            lf.triN.reserve(lf.tri.size());
            for (ObjId v : infos[fi].verts) {
                const V3 n = sums[{g, v}].normalized();
                lf.triN.push_back(n.length() > 0.5 ? n : lf.normal);
            }
        }
    }

    int textureFor(Ref mat) {
        if (!mat) return -1;
        if (auto it = texIndex_.find(*mat); it != texIndex_.end()) return it->second;
        int idx = -1;
        if (const auto* m = f_.get<skp::Material>(mat); m != nullptr && m->texture) {
            if (auto img = skp::textureImage(f_, *m)) {
                idx = static_cast<int>(s_.textures.size());
                s_.textures.push_back(std::move(*img));
            }
        }
        texIndex_[*mat] = idx;
        return idx;
    }

    Batch& batch(int tex, bool transparent) {
        const auto key = std::make_pair(tex, transparent);
        if (auto it = batchIndex_.find(key); it != batchIndex_.end()) return s_.batches[it->second];
        batchIndex_[key] = s_.batches.size();
        Batch b;
        b.texture = tex;
        b.transparent = transparent;
        s_.batches.push_back(std::move(b));
        return s_.batches.back();
    }

    static void put(Batch& b, const V3& p, const V3& n, const std::array<double, 2>& uv, const QColor& c,
                    bool sel) {
        const std::array<float, kVertexFloats> v{
            static_cast<float>(p.x),    static_cast<float>(p.y),     static_cast<float>(p.z),
            static_cast<float>(n.x),    static_cast<float>(n.y),     static_cast<float>(n.z),
            static_cast<float>(uv[0]),  static_cast<float>(uv[1]),   static_cast<float>(c.redF()),
            static_cast<float>(c.greenF()), static_cast<float>(c.blueF()), static_cast<float>(c.alphaF()),
            sel ? 1.0f : 0.0f};
        b.data.insert(b.data.end(), v.begin(), v.end());
    }

    bool hiddenEntity(const skp::DrawElem& de) const {
        if (o_.style.hiddenGeometry) return false;
        return de.flags[skp::kHidden] || layerHidden(f_, de.layer);
    }

    // Colour of one side of a face; also tells whether the material carries a texture.
    QColor sideColor(Ref mat, bool front, int& tex) {
        tex = -1;
        const Style& st = o_.style;
        if (st.renderMode == kHiddenLine) return st.background;
        const QColor plain = front ? st.faceFront : st.faceBack;
        if (st.renderMode == kMonochrome) return plain;
        const auto* m = f_.get<skp::Material>(mat);
        if (m == nullptr) return plain;
        if (st.textures) tex = textureFor(mat);
        return toQColor(skp::materialRgba(*m));
    }

    void emitFace(const LocalFace& lf, const M4& xf, bool mirrored, Ref inherited, const Pick& pick, bool sel,
                  geom::Box& topBox) {
        const skp::Face& face = *lf.face;
        const Ref frontMat = face.de.material ? face.de.material : inherited;
        const Ref backMat = face.back ? face.back : inherited;
        ++s_.faceCount;
        for (int side = 0; side < 2; ++side) {
            const bool front = side == 0;
            const Ref mat = front ? frontMat : backMat;
            const bool own = front ? static_cast<bool>(face.de.material) : static_cast<bool>(face.back);
            int tex = -1;
            QColor col = sideColor(mat, front, tex);
            const skp::Texture* t = nullptr;
            if (tex >= 0) {
                const auto* m = f_.get<skp::Material>(mat);
                t = m ? f_.get<skp::Texture>(m->texture) : nullptr;
                if (t == nullptr) tex = -1;
            }
            bool transparent = col.alpha() < 255;
            if (o_.style.xray) {
                col.setAlphaF(std::min(col.alphaF(), 0.5f));
                transparent = true;
            }
            Batch& b = batch(tex, transparent);
            for (std::size_t i = 0; i + 2 < lf.tri.size(); i += 3) {
                std::array<std::size_t, 3> order{i, i + 1, i + 2};
                if (front == mirrored) std::swap(order[1], order[2]);
                for (std::size_t k : order) {
                    const V3 wp = xf.point(lf.tri[k]);
                    V3 wn = xf.normal(lf.triN[k]);
                    if (!front) wn = -wn;
                    std::array<double, 2> uv{0, 0};
                    if (t != nullptr) uv = textureUV(f_, face, *t, lf.tri[k], front, own);
                    put(b, wp, wn, uv, col, sel);
                }
            }
        }
        for (std::size_t i = 0; i + 2 < lf.tri.size(); i += 3) {
            PickTri pt{xf.point(lf.tri[i]), xf.point(lf.tri[i + 1]), xf.point(lf.tri[i + 2]), pick};
            for (const V3* p : {&pt.a, &pt.b, &pt.c}) {
                s_.bounds.add(*p);
                topBox.add(*p);
            }
            s_.tris.push_back(pt);
        }
    }

    // Draws one component (root or definition) with a transform. `top` is the root entity that owns it.
    void drawComp(ObjId compId, const M4& xf, Ref inherited, std::optional<ObjId> top, bool sel, int depth) {
        if (depth > 32) return;
        const LocalComp& lc = local(compId);
        const bool mirrored = xf.det3() < 0;
        const bool wire = o_.style.renderMode == kWireframe;
        for (const LocalFace& lf : lc.faces) {
            if (hiddenEntity(lf.face->de)) continue;
            const ObjId t = top.value_or(lf.id);
            const bool s = sel || (!top && o_.selection.contains(lf.id));
            Pick pick{t, lf.id, std::nullopt, xf};
            geom::Box& box = s_.topBoxes[t];
            if (wire) {
                for (const V3& p : lf.tri) {
                    s_.bounds.add(xf.point(p));
                    box.add(xf.point(p));
                }
                continue;
            }
            emitFace(lf, xf, mirrored, inherited, pick, s, box);
        }
        for (const LocalEdge& le : lc.edges) {
            const auto* e = f_.get<skp::Edge>(le.id);
            if (e == nullptr || hiddenEntity(e->de)) continue;
            if (le.soft && !o_.style.hiddenGeometry) continue;
            SceneEdge se;
            se.a = xf.point(le.a);
            se.b = xf.point(le.b);
            se.n1 = le.faces > 0 ? xf.normal(le.n1) : V3{};
            se.n2 = le.faces > 1 ? xf.normal(le.n2) : V3{};
            se.faces = le.faces;
            const ObjId t = top.value_or(le.id);
            se.selected = sel || (!top && o_.selection.contains(le.id));
            se.pick = Pick{t, std::nullopt, le.id, xf};
            s_.bounds.add(se.a);
            s_.bounds.add(se.b);
            geom::Box& box = s_.topBoxes[t];
            box.add(se.a);
            box.add(se.b);
            s_.edges.push_back(se);
        }
        // Guides: drawing element (fields 0-9: hidden = 3, layer = 9), then CLine3d point, direction,
        // start, end (CConstructionLine::Serialize 0x7bfe0) or a point (CConstructionPoint).
        for (ObjId gid : lc.guides) {
            const skp::Record* r = f_.get<skp::Record>(gid);
            if (r == nullptr || r->fields.size() < 13) continue;
            const bool hidden = skp::recBool(r, 3).value_or(false);
            const Ref layer = std::holds_alternative<Ref>(r->fields[9]) ? std::get<Ref>(r->fields[9]) : Ref{};
            if (!o_.style.hiddenGeometry && (hidden || layerHidden(f_, layer))) continue;
            auto num = [&](std::size_t i) { return skp::recF64(r, i).value_or(0.0); };
            const V3 p = xf.point(V3{num(10), num(11), num(12)});
            if (r->cls == "CConstructionPoint") {
                s_.guidePoints.push_back(p);
                continue;
            }
            if (r->fields.size() < 18) continue;
            const V3 d = xf.vector(V3{num(13), num(14), num(15)});
            const double len = d.length();
            if (len < 1e-12) continue;
            s_.guides.push_back({p, d / len, num(16) * len, num(17) * len});
        }
        for (ObjId iid : lc.instances) {
            const auto* inst = f_.get<skp::Instance>(iid);
            if (inst == nullptr || !inst->def || hiddenEntity(inst->de)) continue;
            const auto* def = f_.get<skp::Definition>(inst->def);
            if (def == nullptr) continue;
            M4 ixf = xf * M4::fromSkp(inst->xform);
            if (isFaceMe(f_, *def)) {
                s_.hasFaceMe = true;
                ixf = faceCamera(ixf);
            }
            const Ref mat = inst->de.material ? inst->de.material : inherited;
            const bool s = sel || (!top && o_.selection.contains(iid));
            if (!top && s) s_.selectedInstances.insert(iid);
            drawComp(*inst->def, ixf, mat, top ? top : std::optional<ObjId>(iid), s, depth + 1);
        }
    }

    // Always-face-camera components turn about the vertical through their origin so that their
    // front (local -Y) looks at the eye (estimated; CComponentBehavior +0x38).
    M4 faceCamera(const M4& xf) const {
        const V3 origin = xf.point(V3{0, 0, 0});
        V3 facing = xf.vector(V3{0, -1, 0});
        facing.z = 0;
        V3 want = o_.eye - origin;
        want.z = 0;
        if (facing.length() < 1e-9 || want.length() < 1e-9) return xf;
        facing = facing.normalized();
        want = want.normalized();
        const double ang = std::atan2(facing.cross(want).z, facing.dot(want));
        return M4::rotate(origin, V3{0, 0, 1}, ang) * xf;
    }

    const skp::SkpFile& f_;
    const BuildOptions& o_;
    Scene& s_;
    std::map<ObjId, LocalComp> cache_;
    std::map<ObjId, int> texIndex_;
    std::map<std::pair<int, bool>, std::size_t> batchIndex_;
};

}  // namespace

bool layerHidden(const skp::SkpFile& f, skp::Ref layer) {
    const auto* l = f.get<skp::Layer>(layer);
    return l != nullptr && l->hidden;
}

bool isFaceMe(const skp::SkpFile& f, const skp::Definition& d) {
    // CComponentBehavior record field 5: bit 0 always face camera, bit 1 shadows face sun (0x70730).
    const skp::Record* r = skp::record(f, d.behavior);
    if (r == nullptr || r->fields.size() <= 5) return false;
    if (const auto* b = std::get_if<std::uint8_t>(&r->fields[5])) return (*b & 1) != 0;
    return false;
}

std::array<double, 2> textureUV(const skp::SkpFile& f, const skp::Face& face, const skp::Texture& tex,
                                const V3& p, bool front, bool ownCoords) {
    const geom::Plane pl = geom::Plane::fromSkp(face.plane);
    const skp::Record* tc = ownCoords ? skp::faceTextureCoords(f, face) : nullptr;
    const std::uint32_t flags = tc ? coordFlags(tc, front) : 0;
    // GetAAMatrix 0xb960f: face frame at the plane point, or at the origin along the projection vector.
    V3 origin = pl.origin();
    V3 n = pl.n;
    if (tc != nullptr && (flags & 2) != 0) {
        const std::size_t k = front ? skp::tc::kFrontProj : skp::tc::kBackProj;
        const V3 proj{fieldF64(tc, k, 0), fieldF64(tc, k + 1, 0), fieldF64(tc, k + 2, 0)};
        if (std::abs(proj.length() - 1.0) < 1e-6) {
            origin = V3{0, 0, 0};
            n = proj;
        }
    }
    V3 xa, ya;
    geom::arbitraryAxes(n, xa, ya);
    const V3 d = p - origin;
    // GetUVQMatrix 0xb97bc: (flags & 1 ? stored matrix, inverted on load : identity) * Scale(1/w, 1/h).
    const double w = tex.width > 1e-9 ? tex.width : 1.0;
    const double h = tex.height > 1e-9 ? tex.height : 1.0;
    const std::array<double, 3> s{d.dot(xa) / w, d.dot(ya) / h, 1.0};
    std::array<double, 3> uvq = s;
    if (tc != nullptr && (flags & 1) != 0) {
        const std::size_t k = front ? skp::tc::kFront : skp::tc::kBack;
        Mat3 m{};
        for (std::size_t i = 0; i < 9; ++i) m[i] = fieldF64(tc, k + i, (i % 4 == 0) ? 1.0 : 0.0);
        if (auto inv = inverse3(m)) {
            for (std::size_t r = 0; r < 3; ++r) {
                uvq[r] = (*inv)[r * 3] * s[0] + (*inv)[r * 3 + 1] * s[1] + (*inv)[r * 3 + 2] * s[2];
            }
        }
    }
    const double q = std::abs(uvq[2]) > 1e-12 ? uvq[2] : 1.0;
    return {uvq[0] / q, uvq[1] / q};
}

Scene buildScene(const skp::SkpFile& f, const BuildOptions& o) {
    Scene s;
    SceneMaker(f, o, s).run();
    return s;
}

}  // namespace render
