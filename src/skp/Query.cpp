#include "skp/Query.h"

#include <algorithm>

namespace skp {

const Component* rootComponent(const SkpFile& f) {
    return f.get<Component>(f.root);
}

Component* rootComponent(SkpFile& f) {
    return f.get<Component>(f.root);
}

const Component* componentOf(const SkpFile& f, ObjId id) {
    if (id >= f.objs.size()) return nullptr;
    if (const auto* c = std::get_if<Component>(&f.objs[id])) return c;
    if (const auto* d = std::get_if<Definition>(&f.objs[id])) return &d->comp;
    return nullptr;
}

Component* componentOf(SkpFile& f, ObjId id) {
    if (id >= f.objs.size()) return nullptr;
    if (auto* c = std::get_if<Component>(&f.objs[id])) return c;
    if (auto* d = std::get_if<Definition>(&f.objs[id])) return &d->comp;
    return nullptr;
}

namespace {
std::vector<ObjId> flat(const std::vector<Ref>& v) {
    std::vector<ObjId> out;
    for (const Ref& r : v) {
        if (r) out.push_back(*r);
    }
    return out;
}
}  // namespace

std::vector<ObjId> definitions(const SkpFile& f) {
    const Component* c = rootComponent(f);
    const auto* dl = c ? f.get<DefinitionList>(c->defs) : nullptr;
    return dl ? flat(dl->defs) : std::vector<ObjId>{};
}

std::vector<ObjId> materials(const SkpFile& f) {
    const Component* c = rootComponent(f);
    const auto* mm = c ? f.get<MaterialManager>(c->materials) : nullptr;
    return mm ? flat(mm->mats) : std::vector<ObjId>{};
}

std::vector<ObjId> layers(const SkpFile& f) {
    const Component* c = rootComponent(f);
    const auto* lm = c ? f.get<LayerManager>(c->layers) : nullptr;
    return lm ? flat(lm->layers) : std::vector<ObjId>{};
}

std::vector<ObjId> scenes(const SkpFile& f) {
    std::vector<ObjId> out;
    const Record* pl = record(f, f.pages);
    if (pl == nullptr || pl->fields.size() < 3) return out;
    const auto n = recU32(pl, 2).value_or(0);
    for (std::size_t i = 0; i < n && 3 + i < pl->fields.size(); ++i) {
        if (const auto* r = std::get_if<Ref>(&pl->fields[3 + i]); r != nullptr && *r) {
            out.push_back(**r);
        }
    }
    return out;
}

QString definitionName(const SkpFile& f, Ref def) {
    const auto* d = f.get<Definition>(def);
    return d ? d->name : QString();
}

Vec3d vertexPos(const SkpFile& f, Ref v) {
    const auto* vx = f.get<Vertex>(v);
    return vx ? vx->pos : Vec3d{0, 0, 0};
}

std::vector<ObjId> loopVertices(const SkpFile& f, ObjId loop) {
    std::vector<ObjId> out;
    const auto* l = f.get<Loop>(loop);
    if (l == nullptr) return out;
    for (ObjId u : l->uses) {
        const auto* eu = f.get<EdgeUse>(u);
        const auto* e = eu ? f.get<Edge>(eu->edge) : nullptr;
        if (e == nullptr) continue;
        const Ref v = eu->reversed ? e->v1 : e->v0;
        if (v) out.push_back(*v);
    }
    return out;
}

std::vector<Vec3d> loopPoints(const SkpFile& f, ObjId loop) {
    std::vector<Vec3d> out;
    for (ObjId v : loopVertices(f, loop)) {
        out.push_back(vertexPos(f, v));
    }
    return out;
}

const Record* record(const SkpFile& f, Ref r) {
    return f.get<Record>(r);
}

Record* record(SkpFile& f, Ref r) {
    return f.get<Record>(r);
}

namespace {
template <class T>
std::optional<T> recAt(const Record* r, std::size_t i) {
    if (r == nullptr || i >= r->fields.size()) return std::nullopt;
    if (const T* p = std::get_if<T>(&r->fields[i])) return *p;
    return std::nullopt;
}
}  // namespace

std::optional<Color> recColor(const Record* r, std::size_t i) { return recAt<Color>(r, i); }
std::optional<bool> recBool(const Record* r, std::size_t i) { return recAt<bool>(r, i); }
std::optional<std::uint32_t> recU32(const Record* r, std::size_t i) { return recAt<std::uint32_t>(r, i); }
std::optional<double> recF64(const Record* r, std::size_t i) { return recAt<double>(r, i); }
std::optional<QString> recString(const Record* r, std::size_t i) { return recAt<QString>(r, i); }

bool setRecColor(Record* r, std::size_t i, Color c) {
    if (r == nullptr || i >= r->fields.size() || !std::holds_alternative<Color>(r->fields[i])) return false;
    r->fields[i] = c;
    return true;
}

bool setRecBool(Record* r, std::size_t i, bool b) {
    if (r == nullptr || i >= r->fields.size() || !std::holds_alternative<bool>(r->fields[i])) return false;
    r->fields[i] = b;
    return true;
}

const Record* faceTextureCoords(const SkpFile& f, const Face& face) {
    const auto* ac = f.get<AttrContainer>(face.de.ent.attrs);
    if (ac == nullptr) return nullptr;
    for (ObjId a : ac->attrs) {
        const Record* r = f.get<Record>(a);
        if (r != nullptr && r->cls == "CFaceTextureCoords") return r;
    }
    return nullptr;
}

Color materialRgba(const Material& m) {
    Color c = m.color;
    c.a = m.useAlpha ? static_cast<std::uint8_t>(std::clamp(1.0 - m.transparency, 0.0, 1.0) * 255.0 + 0.5) : 255;
    return c;
}

std::optional<QImage> textureImage(const SkpFile& f, const Material& m) {
    const auto* t = f.get<Texture>(m.texture);
    const auto* d = t ? f.get<Dib>(t->dib) : nullptr;
    if (d == nullptr || d->data.isEmpty()) return std::nullopt;
    QImage img;
    if (!img.loadFromData(d->data)) return std::nullopt;
    return img;
}

Vec3d xformPoint(const Xform13& t, const Vec3d& p) {
    const double w = std::abs(t[12]) > 1e-12 ? t[12] : 1.0;
    return {(t[0] * p[0] + t[1] * p[1] + t[2] * p[2] + t[9]) / w,
            (t[3] * p[0] + t[4] * p[1] + t[5] * p[2] + t[10]) / w,
            (t[6] * p[0] + t[7] * p[1] + t[8] * p[2] + t[11]) / w};
}

Xform13 xformMul(const Xform13& a, const Xform13& b) {
    // Result maps p -> a(b(p)). Scale terms are folded into the matrix (scale 1).
    const double wa = std::abs(a[12]) > 1e-12 ? a[12] : 1.0;
    const double wb = std::abs(b[12]) > 1e-12 ? b[12] : 1.0;
    Xform13 r{};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            double s = 0;
            for (int k = 0; k < 3; ++k) s += a[i * 3 + k] * b[k * 3 + j];
            r[static_cast<std::size_t>(i * 3 + j)] = s / (wa * wb);
        }
        double t = 0;
        for (int k = 0; k < 3; ++k) t += a[i * 3 + k] * b[static_cast<std::size_t>(9 + k)];
        r[static_cast<std::size_t>(9 + i)] = (t / wb + a[static_cast<std::size_t>(9 + i)]) / wa;
    }
    r[12] = 1.0;
    return r;
}

Stats stats(const SkpFile& f) {
    Stats s;
    s.objects = f.objs.size();
    for (const Object& o : f.objs) {
        if (std::holds_alternative<Face>(o)) ++s.faces;
        if (std::holds_alternative<Edge>(o)) ++s.edges;
        if (const auto* i = std::get_if<Instance>(&o)) {
            if (i->kind == InstanceKind::Group) ++s.groups; else ++s.instances;
        }
    }
    s.definitions = definitions(f).size();
    s.materials = materials(f).size();
    s.layers = layers(f).size();
    s.scenes = scenes(f).size();
    return s;
}

}  // namespace skp
