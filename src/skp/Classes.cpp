// Typed classes (MECHANICS.md sections 3-4). Field order and version conditions come from each class's
// Serialize function in the original library (addresses in the sheet).

#include "skp/Archive.h"

#include <algorithm>
#include <cmath>

namespace skp {

namespace {

const char* const kRecordClasses[] = {
    "CFaceTextureCoords", "CComponentBehavior", "CConstructionGeometry", "CConstructionLine",
    "CConstructionPoint", "CText", "CDimension", "CDimensionLinear", "CDimensionRadial", "CDimensionStyle",
    "CTextStyle", "CPageList", "CSketchUpPage", "CViewPage", "CShadowInfo", "CRenderingOptions",
    "COptionsManager", "CSkpStyleManager", "CSkpStyle", "CFontManager", "CSkFont", "CSketchCS",
    "CSectionPlane", "CThumbnail", "CWatermark", "CWatermarkManager", "CPolyline3d", "CRelationship",
    "CRelationshipMap", "CSchemaFile", "CSchemaZipFile", "CSchemaFilterFile", "CBackgroundImage",
};

template <class... Ts>
struct Overloaded : Ts... {
    using Ts::operator()...;
};
template <class... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

}  // namespace

std::string classNameOf(const Object& o) {
    return std::visit(Overloaded{
                          [](const std::monostate&) { return std::string("<empty>"); },
                          [](const VersionMap&) { return std::string("CVersionMap"); },
                          [](const Vertex&) { return std::string("CVertex"); },
                          [](const Edge&) { return std::string("CEdge"); },
                          [](const EdgeUse&) { return std::string("CEdgeUse"); },
                          [](const Loop&) { return std::string("CLoop"); },
                          [](const Face&) { return std::string("CFace"); },
                          [](const Curve&) { return std::string("CCurve"); },
                          [](const ArcCurve&) { return std::string("CArcCurve"); },
                          [](const Component&) { return std::string("CComponent"); },
                          [](const Definition&) { return std::string("CComponentDefinition"); },
                          [](const Instance& i) {
                              switch (i.kind) {
                                  case InstanceKind::Group: return std::string("CGroup");
                                  case InstanceKind::Image: return std::string("CImage");
                                  case InstanceKind::Component: break;
                              }
                              return std::string("CComponentInstance");
                          },
                          [](const Camera&) { return std::string("CCamera"); },
                          [](const Dib&) { return std::string("CDib"); },
                          [](const Texture&) { return std::string("CTexture"); },
                          [](const Material&) { return std::string("CMaterial"); },
                          [](const MaterialManager&) { return std::string("CMaterialManager"); },
                          [](const Layer&) { return std::string("CLayer"); },
                          [](const LayerManager&) { return std::string("CLayerManager"); },
                          [](const DefinitionList&) { return std::string("CDefinitionList"); },
                          [](const AttrContainer&) { return std::string("CAttributeContainer"); },
                          [](const Attribute&) { return std::string("CAttribute"); },
                          [](const AttrNamed&) { return std::string("CAttributeNamed"); },
                          [](const Record& r) { return r.cls; },
                      },
                      o);
}

std::optional<Object> makeObjectOfClass(const std::string& c) {
    if (c == "CVersionMap") return Object{VersionMap{}};
    if (c == "CVertex") return Object{Vertex{}};
    if (c == "CEdge") return Object{Edge{}};
    if (c == "CEdgeUse") return Object{EdgeUse{}};
    if (c == "CLoop") return Object{Loop{}};
    if (c == "CFace") return Object{Face{}};
    if (c == "CCurve") return Object{Curve{}};
    if (c == "CArcCurve") return Object{ArcCurve{}};
    if (c == "CComponent") return Object{Component{}};
    if (c == "CComponentDefinition") return Object{Definition{}};
    if (c == "CComponentInstance") return Object{Instance{}};
    if (c == "CGroup") return Object{Instance{InstanceKind::Group, {}, {}, kIdentity13, {}, {}}};
    if (c == "CImage") return Object{Instance{InstanceKind::Image, {}, {}, kIdentity13, {}, {}}};
    if (c == "CCamera") return Object{Camera{}};
    if (c == "CDib") return Object{Dib{}};
    if (c == "CTexture") return Object{Texture{}};
    if (c == "CMaterial") return Object{Material{}};
    if (c == "CMaterialManager") return Object{MaterialManager{}};
    if (c == "CLayer") return Object{Layer{}};
    if (c == "CLayerManager") return Object{LayerManager{}};
    if (c == "CDefinitionList") return Object{DefinitionList{}};
    if (c == "CAttributeContainer") return Object{AttrContainer{}};
    if (c == "CAttribute") return Object{Attribute{}};
    if (c == "CAttributeNamed") return Object{AttrNamed{}};
    for (const char* rc : kRecordClasses) {
        if (c == rc) {
            return Object{Record{c, {}}};
        }
    }
    return std::nullopt;
}

// ---------------------------------------------------------------- base classes

void ioEntity(Archive& ar, Entity& e) {  // CEntity::Serialize 0x9fcfe
    const std::uint32_t v = ar.ver("CEntity");
    if (v == 1) {
        ar.legacyU32();
    }
    if (v > 2) {
        ar.ref(e.attrs);
    }
    if (v == 4) {
        ar.u64(e.pid);
    } else if (v >= 5) {
        ar.byteArrayNumber(e.pid);
    }
}

void ioDrawElem(Archive& ar, DrawElem& d) {  // CDrawingElement::Serialize 0x96b84
    ioEntity(ar, d.ent);
    const std::uint32_t v = ar.ver("CDrawingElement");
    if (v > 4 || v == 2) {
        ar.ref(d.material);
    } else if (ar.reading()) {
        Color c{};
        ar.color(c);
        if (!(c == Color{0, 0, 0, 0})) {
            d.legacyColor = c;
        }
    }
    if (v != 0) {
        ar.boolean(d.flags[0]);
    }
    if (v >= 6) {
        ar.boolean(d.flags[1]);
        ar.boolean(d.flags[2]);
    }
    if (v >= 7) ar.boolean(d.flags[3]);
    if (v >= 8) ar.boolean(d.flags[4]);
    if (v >= 9) ar.boolean(d.flags[5]);
    if (v == 2 && ar.legacyBool()) {
        ar.legacyRef();
    }
    if (v >= 4) {
        ar.ref(d.layer);
    }
}

namespace {

void ioVersionMap(Archive& ar, VersionMap& m) {  // CVersionMap::Serialize 0x138f86
    if (ar.reading()) {
        for (int guard = 0; guard < 10000; ++guard) {
            QString s;
            ar.string(s);
            std::uint32_t v = 0;
            ar.u32(v);
            if (s == QLatin1String("End-Of-Version-Map")) {
                return;
            }
            std::string name = s.toStdString();
            if (name == "CESketchPage") name = "CSketchUpPage";
            if (name == "CESketchDoc") name = "CSketchUpModel";
            if (name == "CAnnotation") name = "CText";
            m.entries.emplace_back(name, v);
        }
        throw SkpError("version map does not end");
    }
    for (auto& [k, v] : m.entries) {
        QString s = QString::fromStdString(k);
        ar.string(s);
        ar.u32(v);
    }
    QString end = QStringLiteral("End-Of-Version-Map");
    std::uint32_t zero = 0;
    ar.string(end);
    ar.u32(zero);
}

void ioEdge(Archive& ar, Edge& e) {  // CEdge::Serialize 0x98c88
    ioDrawElem(ar, e.de);
    ar.ref(e.v0);
    ar.ref(e.v1);
    const std::uint32_t v = ar.ver("CEdge");
    if (v == 1 || v == 2) {
        ar.ref(e.curve);
    }
}

void ioEdgeUse(Archive& ar, EdgeUse& u) {  // CEdgeUse::Serialize 0x9eb8c
    ioEntity(ar, u.ent);
    ar.ref(u.edge);
    ar.boolean(u.reversed);
    ar.ref(u.loop);
    if (ar.ver("CEdgeUse") == 0) {
        ar.ref(u.legacyLinks[0]);
        ar.ref(u.legacyLinks[1]);
    }
}

void ioLoop(Archive& ar, Loop& l) {  // CLoop::Serialize 0xcf708
    ioEntity(ar, l.ent);
    ar.boolean(l.b0);
    ar.boolean(l.b1);
    if (!ar.reading()) {
        ar.refList(l.uses);
        return;
    }
    Ref first;
    ar.ref(first);
    if (!first) {
        return;
    }
    l.uses.push_back(*first);
    if (ar.ver("CLoop") != 0) {
        ar.refList(l.uses);
        return;
    }
    // Old files chain each use to the next instead of listing them.
    ObjId cur = *first;
    for (std::size_t guard = 0; guard < 1000000; ++guard) {
        const auto* eu = std::get_if<EdgeUse>(&ar.arena().at(cur));
        if (eu == nullptr || !eu->legacyLinks[1]) {
            break;
        }
        const ObjId n = *eu->legacyLinks[1];
        if (n == *first || std::find(l.uses.begin(), l.uses.end(), n) != l.uses.end()) {
            break;
        }
        l.uses.push_back(n);
        cur = n;
    }
}

void ioFace(Archive& ar, Face& f) {  // CFace::Serialize 0xa3d40
    ioDrawElem(ar, f.de);
    for (double& x : f.plane) {
        ar.f64(x);
    }
    auto n = static_cast<std::uint32_t>(f.loops.size());
    ar.u32(n);
    if (ar.reading()) {
        if (n >= 100000) {
            throw SkpError("too many loops in a face");
        }
        for (std::uint32_t i = 0; i < n; ++i) {
            Ref x;
            ar.ref(x);
            if (!x) {
                throw SkpError("face has an empty loop");
            }
            f.loops.push_back(*x);
        }
    } else {
        for (ObjId id : f.loops) {
            Ref x = id;
            ar.ref(x);
        }
    }
    const std::uint32_t v = ar.ver("CFace");
    if (v == 1) {
        if (ar.legacyU32() != 0) {
            ar.ref(f.de.material);
        }
        f.back = f.de.material;
    } else if (v < 3) {
        if (ar.reading()) {
            f.back = f.de.material;
        }
    } else {
        ar.ref(f.back);
    }
}

void ioCurve(Archive& ar, Curve& c) {  // CCurve::Serialize 0x813c6
    ioEntity(ar, c.ent);
    switch (ar.ver("CCurve")) {
        case 0:
            ar.ref(c.legacy[0]);
            ar.ref(c.legacy[1]);
            break;
        case 1: break;
        case 2: ar.boolean(c.b); break;
        case 3:
            ar.ref(c.legacy[0]);
            ar.ref(c.legacy[1]);
            ar.boolean(c.b);
            ar.u32(c.n);
            break;
        default:
            ar.boolean(c.b);
            ar.u32(c.n);
            break;
    }
}

void ioArcCurve(Archive& ar, ArcCurve& a) {  // CArcCurve::Serialize 0x57d56
    ioCurve(ar, a.curve);
    ar.point(a.arc.center);
    ar.point(a.arc.normal);
    ar.point(a.arc.xaxis);
    ar.f64(a.arc.start);
    ar.f64(a.arc.end);
    if (!ar.reading() || ar.ver("CArcCurve") != 0) {
        ar.point(a.arc.yaxis);
    } else {
        const Vec3d& n = a.arc.normal;
        const Vec3d& x = a.arc.xaxis;
        a.arc.yaxis = {n[1] * x[2] - n[2] * x[1], n[2] * x[0] - n[0] * x[2], n[0] * x[1] - n[1] * x[0]};
    }
}

Object makeRecord(const char* cls) {
    return Object{Record{cls, {}}};
}

void ioEntityList(Archive& ar, std::vector<ObjId>& ents) {
    auto n = static_cast<std::uint32_t>(ents.size());
    ar.u32(n);
    if (ar.reading()) {
        for (std::uint32_t i = 0; i < n; ++i) {
            Ref x;
            ar.ref(x);
            if (x) {
                ents.push_back(*x);
            }
        }
    } else {
        for (ObjId id : ents) {
            Ref x = id;
            ar.ref(x);
        }
    }
}

void ioComponent(Archive& ar, Component& c) {  // CComponent::Serialize 0x68e42
    ioDrawElem(ar, c.de);
    const std::uint32_t v = ar.ver("CComponent");
    if (v >= 9) {
        ar.inlineObj(c.materials, [] { return Object{MaterialManager{}}; });
        ar.inlineObj(c.layers, [] { return Object{LayerManager{}}; });
        ar.inlineObj(c.defs, [] { return Object{DefinitionList{}}; });
        ioEntityList(ar, c.ents);
        ar.inlineObj(c.relmap, [] { return makeRecord("CRelationshipMap"); });
        ar.ref(c.section);
        if (v == 10) {
            ar.inlineObj(c.fonts, [] { return makeRecord("CFontManager"); });
        }
        return;
    }
    if (!ar.reading()) {
        throw SkpError("cannot write an old component layout");
    }
    ioEntityList(ar, c.ents);
    if (v <= 6) {
        Xform13 t{};
        ar.xform(t);
        c.legacyXform = t;
    }
    if (v != 0) {
        ar.inlineObj(c.relmap, [] { return makeRecord("CRelationshipMap"); });
    }
    if (v >= 2) {
        ar.inlineObj(c.defs, [] { return Object{DefinitionList{}}; });
    }
    if (v >= 3 && v <= 5) {
        if (ar.legacyU32() != 0) {
            ar.ref(c.de.material);
        }
    }
    if (v >= 4) {
        ar.inlineObj(c.materials, [] { return Object{MaterialManager{}}; });
    }
    if (v >= 5) {
        ar.inlineObj(c.layers, [] { return Object{LayerManager{}}; });
    }
    if (v >= 8) {
        ar.ref(c.section);
    }
}

void ioDefinition(Archive& ar, Definition& d) {  // CComponentDefinition::Serialize 0x730f6
    ioComponent(ar, d.comp);
    ar.guid(d.guid);
    const std::uint32_t v = ar.ver("CComponentDefinition");
    if (v >= 3) {
        ar.string(d.name);
        ar.string(d.description);
    }
    if (v > 3) {
        ar.string(d.path);
    } else if (ar.reading()) {
        const std::uint32_t n = ar.legacyU32();
        for (std::uint32_t i = 0; i < n && i < 100000; ++i) {
            ar.string(d.path);
        }
    }
    ar.u32(d.time);
    if (v >= 9) ar.boolean(d.flag);
    if (v >= 5) ar.point(d.insertPoint);
    if (v <= 5 && ar.reading()) {
        const std::uint32_t n = ar.legacyU32();
        for (std::uint32_t i = 0; i < n && i < 10000000; ++i) {
            ar.legacyRef();
        }
    }
    if (v >= 2) {
        ar.inlineObj(d.behavior, [] { return makeRecord("CComponentBehavior"); });
    }
    if (v >= 8) ar.u32(d.u);
    if (v >= 10) ar.ref(d.thumbnail);
}

void ioInstance(Archive& ar, Instance& i) {  // CComponentInstance::Serialize 0x7727a
    ioDrawElem(ar, i.de);
    ar.ref(i.def);
    ar.xform(i.xform);
    const std::uint32_t v = ar.ver("CComponentInstance");
    if (v <= 1) {
        ar.legacyU32();
    }
    if ((v == 1 || v == 2) && ar.legacyU32() != 0) {
        ar.ref(i.de.material);
    }
    if (v >= 4) ar.string(i.name);
    if (v >= 5) ar.guid(i.guid);
}

void ioDib(Archive& ar, Dib& d) {  // CDib::Serialize 0x8a102
    const std::uint32_t v = ar.ver("CDib");
    if (ar.reading() && v == 0) {
        return;
    }
    if (ar.reading() && v == 1) {
        d.kind = 4;
    } else {
        ar.u32(d.kind);
    }
    ar.blobU32(d.data);
    if (v >= 3 && d.kind == 1) {
        ar.u32(d.extra);
    }
}

void ioTexture(Archive& ar, Texture& t) {  // CTexture::Serialize 0x12f422
    const std::uint32_t v = ar.ver("CTexture");
    if (v >= 5) {
        ioEntity(ar, t.ent);
    }
    if (v >= 6 || v < 2) {
        ar.ref(t.dib);
    } else {
        bool has = t.dib.has_value();
        ar.boolean(has);
        if (has) {
            ar.inlineObj(t.dib, [] { return Object{Dib{}}; });
        }
    }
    ar.f64(t.width);
    ar.f64(t.height);
    ar.string(t.name);
    if (v != 0) {
        ar.color(t.average);
    }
    if (v == 3) {
        ar.legacyU32();
        ar.legacyU32();
    }
}

void ioMaterial(Archive& ar, Material& m) {  // CMaterial::Serialize 0xd2f2a
    ioEntity(ar, m.ent);
    const std::uint32_t v = ar.ver("CMaterial");
    ar.string(m.name);
    if (v <= 2 && ar.legacyBool()) {
        ar.legacyU32();
    }
    bool hasTex = m.texture.has_value();
    ar.boolean(hasTex);
    if (hasTex) {
        if (v > 3) {
            ar.inlineObj(m.texture, [] { return Object{Texture{}}; });
        } else {
            ar.ref(m.texture);
        }
    }
    if (ar.reading()) {
        m.kind = 1;
    }
    if (v >= 2 && v <= 4) {
        ar.legacyU32();
    } else if (v >= 6) {
        ar.boolean(m.b60);
        if (v >= 7) ar.color(m.color);
        if (v >= 8) ar.string(m.s68);
        if (v >= 9) ar.u32(m.kind);
        if (v >= 10) ar.u32(m.u84);
        if (v >= 11) ar.f64(m.transparency);
        if (v >= 12) {
            ar.boolean(m.useAlpha);
        } else if (ar.reading()) {
            m.useAlpha = std::abs(m.transparency) > 1e-6;
        }
    }
    if (ar.reading()) {
        if (!m.texture) {
            m.kind = 0;
        }
        if (m.color == Color{0, 0, 0, 0}) {
            m.color = Color{64, 64, 64, 255};
        }
    }
}

void ioMaterialManager(Archive& ar, MaterialManager& m) {  // CMaterialManager::Serialize 0xd5250
    const std::uint32_t v = ar.ver("CMaterialManager");
    if (v != 0) {
        ioEntity(ar, m.ent);
    }
    auto n = static_cast<std::uint32_t>(m.mats.size());
    ar.u32(n);
    if (ar.reading()) {
        if (n > 10000000) throw SkpError("too many materials");
        m.mats.assign(n, std::nullopt);
    }
    for (Ref& x : m.mats) {
        ar.ref(x);
    }
    if (v >= 2) ar.ref(m.current);
    if (v == 3) ar.legacyRef();
}

void ioLayer(Archive& ar, Layer& l) {  // CLayer::Serialize 0xc97d2
    ioEntity(ar, l.ent);
    const std::uint32_t v = ar.ver("CLayer");
    ar.string(l.name);
    ar.boolean(l.hidden);  // CLayer +0x40, IsHidden 0xc9506
    if (ar.reading() && v == 0) {
        Color c{};
        ar.color(c);
        Ref ignored;
        ar.ref(ignored);
        Material mat;
        mat.name = l.name;
        mat.color = c;
        mat.color.a = 255;
        l.material = ar.alloc(Object{mat});
    } else {
        ar.inlineObj(l.material, [] { return Object{Material{}}; });
    }
    if (v >= 2) ar.u32(l.u);
    if (ar.reading() && l.name == QLatin1String("Default")) {
        l.name = QStringLiteral("Layer0");
    }
}

void ioLayerManager(Archive& ar, LayerManager& l) {  // CLayerManager::Serialize 0xcadba
    const std::uint32_t v = ar.ver("CLayerManager");
    if (v != 0) {
        ioEntity(ar, l.ent);
    }
    auto n = static_cast<std::uint32_t>(l.layers.size());
    ar.u32(n);
    if (ar.reading()) {
        if (n > 1000000) throw SkpError("too many layers");
        l.layers.assign(n, std::nullopt);
    }
    for (Ref& x : l.layers) {
        ar.ref(x);
    }
    if (v == 3 || (v > 3 && n > 0)) {
        ar.ref(l.active);
    } else if (v <= 2 && ar.reading()) {
        ar.string(l.legacyActiveName);
    }
}

void ioDefinitionList(Archive& ar, DefinitionList& d) {  // CDefinitionList::Serialize 0x85ab8
    auto n = static_cast<std::uint32_t>(d.defs.size());
    ar.u32(n);
    if (ar.reading()) {
        if (n > 10000000) throw SkpError("too many definitions");
        d.defs.assign(n, std::nullopt);
    }
    for (Ref& x : d.defs) {
        ar.ref(x);
    }
}

void ioAttrNamed(Archive& ar, AttrNamed& a) {  // CAttributeNamed::Serialize 0x5d538
    ioEntity(ar, a.ent);
    ar.u32(a.u);
    ar.string(a.name);
    if (ar.reading()) {
        for (int guard = 0; guard < 1000000; ++guard) {
            QString k;
            ar.string(k);
            if (k.isEmpty()) {
                break;
            }
            TypedValue tv;
            ioTypedValue(ar, tv);
            a.pairs.emplace_back(k, std::move(tv));
        }
    } else {
        for (auto& [k, tv] : a.pairs) {
            ar.string(k);
            ioTypedValue(ar, tv);
        }
        QString empty;
        ar.string(empty);
    }
    if (ar.ver("CAttributeNamed") > 0) {
        ar.u32(a.extra);
    }
}

}  // namespace

void ioTypedValue(Archive& ar, TypedValue& tv) {  // CTypedValue operator>> 0x1571f2
    ar.u8(tv.type);
    const std::uint8_t t = tv.type;
    if (ar.reading()) {
        switch (t) {
            case 2: tv.value = std::uint8_t{0}; break;
            case 3: tv.value = std::uint16_t{0}; break;
            case 4:
            case 9: tv.value = std::uint32_t{0}; break;
            case 5: tv.value = 0.0f; break;
            case 6:
            case 12:
            case 13:
            case 14:
            case 15:
            case 16: tv.value = 0.0; break;
            case 7: tv.value = false; break;
            case 8: tv.value = Color{}; break;
            case 10: tv.value = QString(); break;
            case 11: tv.value = std::vector<TypedValue>{}; break;
            case 17:
            case 18:
            case 19: tv.value = Vec3d{}; break;
            case 20: tv.value = Xform13{}; break;
            default: tv.value = std::monostate{}; break;
        }
    }
    std::visit(Overloaded{
                   [](std::monostate&) {},
                   [&](std::uint8_t& x) { ar.u8(x); },
                   [&](std::uint16_t& x) { ar.u16(x); },
                   [&](std::uint32_t& x) { ar.u32(x); },
                   [&](float& x) { ar.f32(x); },
                   [&](double& x) { ar.f64(x); },
                   [&](bool& x) { ar.boolean(x); },
                   [&](Color& x) { ar.color(x); },
                   [&](QString& x) { ar.string(x); },
                   [&](std::vector<TypedValue>& items) {
                       auto n = static_cast<std::uint32_t>(items.size());
                       ar.u32(n);
                       if (ar.reading()) {
                           if (n > 10000000) throw SkpError("attribute array too long");
                           items.resize(n);
                       }
                       for (TypedValue& x : items) {
                           ioTypedValue(ar, x);
                       }
                   },
                   [&](Vec3d& p) { ar.point(p); },
                   [&](Xform13& x) { ar.xform(x); },
               },
               tv.value);
}

void ioCamera(Archive& ar, Camera& c) {  // CCamera::Serialize 0x63d14
    const std::uint32_t v = ar.ver("CCamera");
    ar.point(c.eye);
    ar.point(c.target);
    ar.point(c.up);
    ar.f64(c.d50);
    ar.f64(c.d58);
    ar.boolean(c.perspective);
    ar.f64(c.fov);
    ar.f64(c.height);
    ar.point(c.p88);
    ar.f64(c.aspect);
    ar.boolean(c.fovIsHeight);
    if (v > 1) ar.boolean(c.b81);
    if (v >= 3) ar.string(c.description);
    if (v >= 4) ar.f64(c.db8);
    if (v >= 5) {
        ar.boolean(c.bc0);
        ar.f64(c.dc8);
        ar.f64(c.dd0);
        ar.f64(c.dd8);
    }
}

void serializeObject(Archive& ar, Object& o) {
    std::visit(Overloaded{
                   [](std::monostate&) { throw SkpError("empty object"); },
                   [&](VersionMap& m) { ioVersionMap(ar, m); },
                   [&](Vertex& v) {  // CVertex::Serialize 0x13a550
                       ioEntity(ar, v.ent);
                       ar.point(v.pos);
                   },
                   [&](Edge& e) { ioEdge(ar, e); },
                   [&](EdgeUse& u) { ioEdgeUse(ar, u); },
                   [&](Loop& l) { ioLoop(ar, l); },
                   [&](Face& f) { ioFace(ar, f); },
                   [&](Curve& c) { ioCurve(ar, c); },
                   [&](ArcCurve& a) { ioArcCurve(ar, a); },
                   [&](Component& c) { ioComponent(ar, c); },
                   [&](Definition& d) { ioDefinition(ar, d); },
                   [&](Instance& i) { ioInstance(ar, i); },
                   [&](Camera& c) { ioCamera(ar, c); },
                   [&](Dib& d) { ioDib(ar, d); },
                   [&](Texture& t) { ioTexture(ar, t); },
                   [&](Material& m) { ioMaterial(ar, m); },
                   [&](MaterialManager& m) { ioMaterialManager(ar, m); },
                   [&](Layer& l) { ioLayer(ar, l); },
                   [&](LayerManager& l) { ioLayerManager(ar, l); },
                   [&](DefinitionList& d) { ioDefinitionList(ar, d); },
                   [&](AttrContainer& a) {  // CAttributeContainer::Serialize 0x5c304
                       ioEntity(ar, a.ent);
                       ar.refList(a.attrs);
                   },
                   [&](Attribute& a) {  // CAttribute::Serialize 0x5bab0
                       ioEntity(ar, a.ent);
                       ar.u32(a.u);
                   },
                   [&](AttrNamed& a) { ioAttrNamed(ar, a); },
                   [&](Record& r) { serializeRecord(ar, r); },
               },
               o);
}

}  // namespace skp
