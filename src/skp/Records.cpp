// Settings and annotation classes kept as ordered records (MECHANICS.md section 8).
// When an older file lacks a field, a default is recorded so every version yields the same field list,
// and saving always writes the 2017 layout.

#include "skp/Archive.h"
#include "skp/Records.h"

namespace skp {

namespace {

using std::uint32_t;

void pointRef(RecIO& r) {  // CPointRef operator>> 0xf0faa
    r.u32();
    const uint32_t ver = r.u32();
    r.f64n(3);
    r.r();
    if (ver > 0) {
        r.r();
    } else {
        r.def(Ref{});
    }
    const uint32_t n = r.u32();
    for (uint32_t i = 0; i < n; ++i) r.r();
    if (ver >= 4) {
        const uint32_t m = r.u32();
        for (uint32_t i = 0; i < m; ++i) r.r();
    } else {
        r.def(uint32_t{0});
    }
}

void faceTextureCoords(RecIO& r) {  // 0xb5914
    const uint32_t v = r.ver("CFaceTextureCoords");
    r.entity();
    r.u32();     // CAttribute value
    r.f64n(9);   // front 3x3
    if (v != 0) {
        r.f64n(3);  // front projection
    } else {
        for (int i = 0; i < 3; ++i) r.def(0.0);
    }
    if (v >= 2) {
        r.f64n(12);  // back 3x3 + back projection
    } else {
        for (double x : {1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0}) r.def(x);
    }
    if (v >= 3) {
        for (int side = 0; side < 2; ++side) {
            const uint32_t n = r.u32();
            for (uint32_t i = 0; i < n; ++i) r.f64n(4);
        }
    } else {
        r.def(uint32_t{0});
        r.def(uint32_t{0});
    }
    if (v >= 4) {
        r.u32();
        r.u32();
    } else {
        r.def(uint32_t{1});
        r.def(uint32_t{0});
    }
}

void componentBehavior(RecIO& r) {  // 0x70730
    const uint32_t v = r.ver("CComponentBehavior");
    r.entity();
    if (v >= 3) r.b(); else r.def(true);
    r.b();
    if (v >= 2) r.u32(); else r.def(uint32_t{0});
    if (v >= 4) r.u8(); else r.def(std::uint8_t{0});
    if (v >= 5) r.u32(); else r.def(uint32_t{0});
}

void pageBase(RecIO& r) {  // CSketchUpPage::Serialize 0x11c8c8
    if (!r.reading() || r.ver("CEntity") != 0) {
        r.entity();
    } else {
        r.def(Ref{});
        r.def(std::uint64_t{0});
    }
    r.s();
    r.s();
}

void renderingOptions(RecIO& r) {  // CRenderingOptions::Serialize 0xf6582 (version 36 layout)
    const uint32_t v = r.ver("CRenderingOptions");
    auto& ar = r.ar();
    r.entity();
    r.u32();  // 0x28 render mode
    if (r.reading() && v >= 0x11 && v < 0x16) {
        const uint32_t x = ar.legacyU32();
        r.def(x == 1);
        r.def(x == 2);
    } else if (v >= 0x16) {
        r.b();
        r.b();
    } else {
        r.b();
        r.def(false);
    }
    r.b();  // 0x4b
    if (v > 8) {
        r.u32();  // 0x30
    } else if (r.reading()) {
        r.def(static_cast<uint32_t>(ar.legacyBool() ? 1 : 0));
    }
    r.c();  // 0x70 background
    r.c();  // 0x74 edges
    r.c();  // 0x78 highlight
    if (v >= 11) r.c(); else r.def(Color{0, 0, 0, 255});  // 0x80 construction
    if (v >= 6) {
        r.b();
        r.b();
        r.b();
        r.b();
        r.u32();
    } else {
        const bool present[4] = {v >= 1, v >= 4, v >= 5, v == 2};
        for (bool p : present) {
            if (p) r.b(); else r.def(false);
        }
        r.def(uint32_t{0});
    }
    if (v >= 7) {
        r.b();
        r.u32();
        r.b();
        r.u32();
    } else {
        r.def(false);
        r.def(uint32_t{0});
        r.def(false);
        r.def(uint32_t{0});
    }
    if (v >= 0x1a) {
        r.b();
        r.u32();
        r.b();
        r.u32();
        if (v > 0x1b) r.b(); else r.def(false);
    } else {
        r.def(false);
        r.def(uint32_t{0});
        r.def(false);
        r.def(uint32_t{0});
        r.def(false);
    }
    if (v >= 7) r.b(); else r.def(false);            // 0x4a hidden geometry
    if (v >= 8) r.u32(); else r.def(uint32_t{0});    // 0x44 face colour mode
    if (v >= 10) {
        r.c();  // 0x88 front
        r.c();  // 0x84 back
    } else {
        r.def(Color{255, 255, 255, 255});
        r.def(Color{164, 178, 187, 255});
    }
    if (v >= 12) {
        r.f64();
        r.f64();
        r.b();
        r.b();
    } else {
        r.def(0.0);
        r.def(0.0);
        r.def(false);
        r.def(false);
    }
    if (v >= 0x1d) {
        r.b();
        r.c();
        r.b();
        r.f64();
        r.f64();
        if (v >= 0x1e) r.u32(); else r.def(uint32_t{0});
        if (v >= 0x1f) {
            r.u32();
            r.b();
            r.b();
            r.b();
            r.b();
        } else {
            r.def(uint32_t{0});
            for (int i = 0; i < 4; ++i) r.def(false);
        }
    } else {
        r.def(false);
        r.def(Color{0, 0, 0, 255});
        r.def(false);
        r.def(0.0);
        r.def(0.0);
        r.def(uint32_t{0});
        r.def(uint32_t{0});
        for (int i = 0; i < 4; ++i) r.def(false);
    }
    // sky, horizon, ground, draw sky, draw ground, underground
    if (v > 13) {
        r.c();
        if (v >= 0x18) r.c(); else r.def(Color{0, 0, 0, 0});
        r.c();
        r.b();
        r.b();
        r.b();
    } else if (v == 13 && r.reading()) {
        Color c1{};
        ar.color(c1);
        r.def(c1);
        r.def(Color{0, 0, 0, 0});
        Color c2{};
        ar.color(c2);
        r.def(c2);
        for (int i = 0; i < 3; ++i) {
            const uint32_t x = ar.legacyU32();
            r.def(x != 0xFFFFFFFFu && x != 0);
        }
    } else {
        r.def(Color{203, 217, 236, 255});
        r.def(Color{0, 0, 0, 0});
        r.def(Color{210, 208, 185, 255});
        for (int i = 0; i < 3; ++i) r.def(false);
    }
    if (v >= 13) r.u32(); else r.def(uint32_t{0});  // 0xbc ground transparency
    if (v >= 15) {
        r.c();
        r.c();
        r.c();
        r.u32();
        r.u32();
    } else {
        for (int i = 0; i < 3; ++i) r.def(Color{0, 0, 0, 255});
        r.def(uint32_t{1});
        r.def(uint32_t{0});
    }
    if (v == 0x12 && r.reading()) {
        r.def(static_cast<uint32_t>(ar.legacyBool() ? 1 : 0));
    } else if (v == 16 && r.reading()) {
        ar.legacyBool();
        r.def(uint32_t{0});
    } else if (v >= 0x13) {
        r.u32();
    } else {
        r.def(uint32_t{0});
    }
    if (v >= 0x14) {
        r.b();
        r.f64();
    } else {
        r.def(true);
        r.def(0.0);
    }
    if (v >= 0x15) r.b(); else r.def(true);
    if (r.reading() && (v == 0x17 || v == 0x18)) {
        ar.legacyBool();
    }
    const bool tail = !(v == 0x17 || v == 0x18);
    if (tail && v >= 0x1b) r.c(); else r.def(Color{0, 0, 0, 255});  // 0x7c locked
    if (tail && v >= 0x20) {
        r.b();  // 0x8c watermarks
        if (v == 0x21) ar.legacyBool();
    } else {
        r.def(false);
    }
    if (tail && v >= 0x23) r.f64(); else r.def(0.5);
    if (tail && v >= 0x24) {
        r.b();
        r.b();
        r.f64();
        r.b();
        r.f64();
    } else {
        r.def(false);
        r.def(false);
        r.def(0.0);
        r.def(false);
        r.def(0.0);
    }
}

void text(RecIO& r) {  // CText::Serialize 0x12acf6
    const uint32_t v = r.ver("CText");
    r.drawElem();
    if (v < 6) {
        throw SkpError("text notes from SketchUp 5 or older are not supported");
    }
    r.r();  // font
    r.f64();
    r.f64();
    pointRef(r);
    r.f64n(3);
    r.f64n(3);
    r.u32();
    r.u32();
    r.b();
    r.b();
    r.u32();
    if (v >= 2) r.b(); else r.def(false);
    r.s();
    if (v >= 8) r.b(); else r.def(false);
    if (v >= 9) r.u32(); else r.def(uint32_t{0});
}

void keyValues(RecIO& r) {
    for (int guard = 0; guard < 1000000; ++guard) {
        bool end = false;
        if (!r.reading()) {
            const Field* f = r.peek();
            const QString* s = f != nullptr ? std::get_if<QString>(f) : nullptr;
            end = s != nullptr && s->isEmpty();
        }
        const QString k = r.s();
        if (r.reading()) end = k.isEmpty();
        if (end) return;
        typedValueFields(r);
    }
}

void variant(RecIO& r) {  // AtlastArchiveHelper::SerializeVariant 0x5af80
    r.u32();
    switch (r.u32()) {
        case 1: r.b(); break;
        case 2: r.u8(); break;
        case 3: r.u16(); break;
        case 4:
        case 5:
        case 14: r.u32(); break;
        case 6: r.f32(); break;
        case 7: r.f64(); break;
        case 9:
        case 10:
        case 11: r.f64n(3); break;
        case 12: r.f64n(13); break;
        case 15: r.u64(); break;
        default: break;
    }
}

void nprEdge(RecIO& r) {  // AtlastArchiveHelper::SerializeNPREdge 0x5ab04
    auto& ar = r.ar();
    if (r.reading()) {
        uint32_t ver = 0;
        ar.u32(ver);
        r.def(uint32_t{2});
        if (ver > 1) {
            r.f64();
            r.f64();
        } else {
            r.def(static_cast<double>(ar.legacyU32()));
            r.def(static_cast<double>(ar.legacyU32()));
        }
    } else {
        r.u32();
        r.f64();
        r.f64();
    }
    r.u32();
    r.b();
    r.u32();
    r.u32();
    const uint32_t n = r.u32();
    for (uint32_t i = 0; i < n; ++i) r.u32();
    const uint32_t m = r.u32();
    for (uint32_t i = 0; i < m; ++i) r.r();
}

void style(RecIO& r) {  // AtlastArchiveHelper::SerializeStyle 0x5a470
    auto& ar = r.ar();
    uint32_t sv = 3;
    if (r.reading()) {
        ar.u32(sv);
        r.def(uint32_t{3});
    } else {
        r.u32();
    }
    r.s();
    r.s();
    if (sv == 1 && r.reading() && ar.legacyBool()) {
        Object dib{Dib{}};
        serializeObject(ar, dib);
    }
    const uint32_t n = r.u32();
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t id = r.u32();
        if (id == 0x3f5) {
            nprEdge(r);
        } else if (id == 0x1389) {
            if (sv >= 3) {
                const uint32_t m = r.u32();
                for (uint32_t k = 0; k < m; ++k) r.r();
            } else {
                r.def(uint32_t{0});
            }
        } else {
            variant(r);
        }
    }
}

void shadowInfo(RecIO& r) {  // CShadowInfo::Serialize 0x10c546
    const uint32_t v = r.ver("CShadowInfo");
    r.entity();
    r.u32();
    r.b();
    r.s();
    r.s();
    if (v != 0) r.f64n(3); else for (int i = 0; i < 3; ++i) r.def(0.0);
    if (v >= 2) {
        r.f64n(3);
        r.b();
    } else {
        r.def(0.0);
        r.def(1.0);
        r.def(0.0);
        r.def(false);
    }
    if (v == 3) r.ar().legacyBool();
    if (v >= 5) {
        r.b();
        r.b();
        r.b();
        r.u32();
        r.u32();
    } else {
        for (int i = 0; i < 3; ++i) r.def(true);
        r.def(uint32_t{80});
        r.def(uint32_t{20});
    }
    if (v >= 6) r.b(); else r.def(false);
}

void sketchCS(RecIO& r) {
    r.drawElem();
    r.f64n(12);
}

void viewPage(RecIO& r) {  // CViewPage::Serialize 0x1428c6
    const uint32_t v = r.ver("CViewPage");
    pageBase(r);
    const uint32_t flags = r.u32();
    if ((flags & 1) != 0) r.r();  // camera
    if ((flags & 2) != 0) {
        renderingOptions(r);
        if (v >= 10) r.r(); else r.def(Ref{});
    }
    if ((flags & 4) != 0) {
        shadowInfo(r);
        if (v >= 2) r.b(); else r.def(false);
    }
    if ((flags & 8) != 0) {
        sketchCS(r);
        r.b();
    }
    if ((flags & 16) != 0) {
        const uint32_t n = r.u32();
        for (uint32_t i = 0; i < n; ++i) r.r();
    }
    if ((flags & 32) != 0) {
        if (v != 0) {
            const uint32_t n = r.u32();
            for (uint32_t i = 0; i < n; ++i) r.r();
        } else {
            r.def(uint32_t{0});
        }
    }
    if ((flags & 64) != 0) {
        if (v == 4 && r.reading()) {
            Ref one;
            r.ar().ref(one);
            r.def(uint32_t{1});
            r.def(one);
        } else if (v >= 5) {
            const uint32_t n = r.u32();
            for (uint32_t i = 0; i < n; ++i) r.r();
        } else {
            r.def(uint32_t{0});
        }
    }
    if (v >= 3) r.b(); else r.def(false);
    if (v >= 6 && v <= 10) r.ar().legacyBool();
    if (v >= 8) r.f64(); else r.def(-1.0);
    if (v >= 9) r.f64(); else r.def(-1.0);
    if (v >= 10) r.r(); else r.def(Ref{});
    if (v >= 12) {
        r.b();
        if (r.b()) {
            if (r.b()) r.blob();
        }
    } else {
        r.def(false);
        r.def(false);
    }
}

void dimensionBase(RecIO& r) {
    r.drawElem();
    r.s();
    if (r.ver("CDimension") > 0) {
        r.r();
        r.b();
        r.u32();
    } else {
        r.def(Ref{});
        r.def(false);
        r.def(uint32_t{0});
    }
}

void textStyle(RecIO& r) {
    const uint32_t v = r.ver("CTextStyle");
    auto& ar = r.ar();
    r.entity();
    if (v != 3) {
        r.r();
    } else if (r.reading()) {
        QString s;
        ar.string(s);
        ar.legacyU32();
        ar.legacyBool();
        ar.legacyBool();
        ar.string(s);
        r.def(Ref{});
    }
    r.u32();
    r.u32();
    r.b();
    r.u32();
    r.b();
    if (v > 1) r.c(); else r.def(Color{0, 0, 0, 255});
    if (v > 4) {
        r.c();
        r.r();
    } else {
        r.def(Color{0, 0, 0, 255});
        r.def(Ref{});
    }
}

void dimensionStyle(RecIO& r) {
    const uint32_t v = r.ver("CDimensionStyle");
    r.entity();
    r.u32();
    if (v > 0) r.r(); else r.def(Ref{});
    r.b();
    r.b();
    r.u32();
    if (v >= 3) r.u32(); else r.def(uint32_t{0});
    r.u32();
    r.u32();
    if (v >= 3) r.u32(); else r.def(uint32_t{0});
    r.b();
    r.c();
    r.b();
    r.b();
    r.f64();
    r.b();
    r.f64();
    if (v >= 2) {
        r.c();
        r.c();
    } else {
        r.def(Color{0, 0, 0, 255});
        r.def(Color{0, 0, 0, 255});
    }
    if (v >= 4) r.u32(); else r.def(uint32_t{0});
}

}  // namespace

void typedValueFields(RecIO& r) {
    const std::uint8_t t = r.u8();
    switch (t) {
        case 2: r.u8(); break;
        case 3: r.u16(); break;
        case 4:
        case 9: r.u32(); break;
        case 5: r.f32(); break;
        case 6:
        case 12:
        case 13:
        case 14:
        case 15:
        case 16: r.f64(); break;
        case 7: r.b(); break;
        case 8: r.c(); break;
        case 10: r.s(); break;
        case 11: {
            const uint32_t n = r.u32();
            for (uint32_t i = 0; i < n; ++i) typedValueFields(r);
            break;
        }
        case 17:
        case 18:
        case 19: r.f64n(3); break;
        case 20: r.f64n(13); break;
        default: break;
    }
}

void serializeRecord(Archive& ar, Record& rec) {
    RecIO r(ar, rec);
    const std::string& c = rec.cls;
    const uint32_t v = ar.ver(c);
    if (c == "CFaceTextureCoords") {
        faceTextureCoords(r);
    } else if (c == "CComponentBehavior") {
        componentBehavior(r);
    } else if (c == "CConstructionGeometry") {
        r.drawElem();
    } else if (c == "CConstructionLine") {
        r.drawElem();
        if (v == 0 && r.reading()) {
            r.f64n(6);
            r.def(0.0);
            r.def(0.0);
            r.def(uint32_t{0});
        } else {
            r.f64n(8);
            r.u32();
        }
    } else if (c == "CConstructionPoint") {
        r.drawElem();
        r.f64n(6);
        r.b();
    } else if (c == "CSketchCS") {
        sketchCS(r);
    } else if (c == "CSectionPlane") {
        r.drawElem();
        r.f64n(4);
    } else if (c == "CSketchUpPage") {
        pageBase(r);
    } else if (c == "CRelationship") {
        r.entity();
        r.r();
        r.r();
    } else if (c == "CRelationshipMap") {
        const uint32_t n = r.u32();
        for (uint32_t i = 0; i < n; ++i) r.r();
    } else if (c == "CPageList") {
        r.entity();
        const uint32_t n = r.u32();
        for (uint32_t i = 0; i < n; ++i) r.r();
        r.r();
    } else if (c == "CThumbnail") {
        r.entity();
        if (v == 0 && r.reading()) {
            throw SkpError("old thumbnail layout is not supported");
        }
        r.r();
        r.r();
    } else if (c == "CRenderingOptions") {
        renderingOptions(r);
    } else if (c == "CText") {
        text(r);
    } else if (c == "COptionsManager") {
        r.u32();
        const uint32_t n = r.u32();
        for (uint32_t i = 0; i < n; ++i) {
            r.s();
            keyValues(r);
        }
    } else if (c == "CShadowInfo") {
        shadowInfo(r);
    } else if (c == "CSkpStyleManager") {
        r.entity();
        const uint32_t n = r.u32();
        if (n > 0) {
            for (uint32_t i = 0; i < n; ++i) r.r();
            r.r();
            r.r();
            if (v >= 2) r.b(); else r.def(false);
        }
    } else if (c == "CSkpStyle") {
        r.entity();
        r.bytes(16);
        r.s();
        style(r);
    } else if (c == "CViewPage") {
        viewPage(r);
    } else if (c == "CFontManager") {
        r.entity();
        const uint32_t n = r.u32();
        for (uint32_t i = 0; i < n; ++i) r.r();
    } else if (c == "CSkFont") {
        r.entity();
        r.s();
        r.b();
        r.b();
        r.u32();
        if (v != 0) {
            r.b();
            r.f64();
        } else {
            r.def(false);
            r.def(0.0);
        }
    } else if (c == "CWatermark") {
        r.entity();
        r.b();
        r.s();
        r.u32();
        r.u32();
        for (int i = 0; i < 5; ++i) r.b();
        r.f64();
        r.f64();
        r.s();
        r.r();
    } else if (c == "CWatermarkManager") {
        r.entity();
        if (v <= 1 && r.reading()) {
            const uint32_t n = ar.legacyU32();
            for (uint32_t i = 0; i < n && i < 100000; ++i) ar.legacyRef();
        }
        const uint32_t n = r.u32();
        for (uint32_t i = 0; i < n; ++i) r.r();
    } else if (c == "CTextStyle") {
        textStyle(r);
    } else if (c == "CDimensionStyle") {
        dimensionStyle(r);
    } else if (c == "CDimension") {
        dimensionBase(r);
    } else if (c == "CDimensionLinear") {
        if (v < 2) throw SkpError("old linear dimensions are not supported");
        dimensionBase(r);
        pointRef(r);
        pointRef(r);
        r.f64n(3);
        if (v > 3) {
            r.f64n(3);
        } else {
            ar.legacyF64();
            r.def(1.0);
            r.def(0.0);
            r.def(0.0);
        }
        r.u32();
        r.f64();
        if (v >= 3) r.f64(); else r.def(0.0);
        if (v >= 6) r.u32(); else r.def(uint32_t{0});
    } else if (c == "CDimensionRadial") {
        dimensionBase(r);
        const Ref edge = r.r();
        r.f64();
        r.f64();
        r.b();
        if (!edge) {
            r.f64n(11);
            if (!r.reading() || v > 1) {
                r.f64n(3);
            } else {
                for (int i = 0; i < 3; ++i) r.def(0.0);
            }
        }
    } else if (c == "CPolyline3d") {
        r.drawElem();
        const uint32_t n = r.u32();
        r.f64n(3 * static_cast<std::size_t>(n));
    } else if (c == "CSchemaFile") {
        r.s();
        r.s();
        r.blob();
        for (int k = 0; k < 2; ++k) {
            const uint32_t n = r.u32();
            for (uint32_t i = 0; i < n; ++i) r.r();
        }
        if (v != 0) {
            if (r.b()) r.r();
        } else {
            r.def(false);
        }
    } else if (c == "CSchemaZipFile") {
        r.s();
        r.s();
        r.blob();
    } else if (c == "CSchemaFilterFile") {
        r.s();
        r.blob();
    } else {
        throw SkpError("class " + c + " is not supported yet");
    }
}

}  // namespace skp
