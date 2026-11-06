#include "render/Style.h"

#include "render/Sun.h"
#include "skp/Query.h"
#include "skp/Records.h"

#include <algorithm>

namespace render {

QColor toQColor(const skp::Color& c) {
    return QColor(c.r, c.g, c.b, c.a);
}

skp::Color fromQColor(const QColor& c) {
    return skp::Color{static_cast<std::uint8_t>(c.red()), static_cast<std::uint8_t>(c.green()),
                      static_cast<std::uint8_t>(c.blue()), static_cast<std::uint8_t>(c.alpha())};
}

namespace {

void getColor(const skp::Record* r, std::size_t i, QColor& out) {
    if (auto c = skp::recColor(r, i)) out = QColor(c->r, c->g, c->b);
}
void getBool(const skp::Record* r, std::size_t i, bool& out) {
    if (auto b = skp::recBool(r, i)) out = *b;
}
void getU32(const skp::Record* r, std::size_t i, std::uint32_t& out) {
    if (auto u = skp::recU32(r, i)) out = *u;
}
void getF64(const skp::Record* r, std::size_t i, double& out) {
    if (auto d = skp::recF64(r, i)) out = *d;
}

template <class T>
void put(skp::Record* r, std::size_t i, T v) {
    if (r != nullptr && i < r->fields.size() && std::holds_alternative<T>(r->fields[i])) r->fields[i] = v;
}
void putColor(skp::Record* r, std::size_t i, const QColor& c) {
    if (r == nullptr || i >= r->fields.size()) return;
    if (const auto* old = std::get_if<skp::Color>(&r->fields[i])) {
        skp::Color n = fromQColor(c);
        n.a = old->a;  // keep the stored alpha byte
        r->fields[i] = n;
    }
}

}  // namespace

geom::V3 Style::sunDirection() const {
    SunInput in;
    in.localSeconds = shadowTime;
    in.timeZoneHours = timeZone;
    in.latitudeDeg = latitude;
    in.longitudeDeg = longitude;
    in.north = north;
    return render::sunDirection(in);
}

Style::Lighting Style::lighting(const geom::V3& towardViewer) const {
    // BaseSketchView::SetLightingAndViewTransformations 0x100162880 with SetupLights 0x1001634a6
    // (scene ambient → GL_LIGHT_MODEL_AMBIENT, ambient/diffuse → GL_LIGHT0, colour material on both).
    // Hidden line: scene 0, ambient 1, diffuse 0 (constants 0x1007523a0..b4).
    Lighting l;
    if (renderMode == kHiddenLine) {
        l.scene = 0.0;
        l.ambient = 1.0;
        l.diffuse = 0.0;
        l.dir = towardViewer.normalized();
        return l;
    }
    l.scene = 0.2;
    if (useSun) {
        // Sun direction, dark and light sliders scaled by 0.01 (constant 0x1007523b8).
        l.dir = sunDirection();
        l.ambient = static_cast<double>(dark) * 0.01;
        l.diffuse = static_cast<double>(light) * 0.01;
    } else {
        // A headlight: light direction (0, 0, 1) in eye space, ambient 0.2, diffuse 0.8.
        l.dir = towardViewer.normalized();
        l.ambient = 0.2;
        l.diffuse = 0.8;
    }
    return l;
}

double Style::brightness(const geom::V3& n, const geom::V3& towardViewer) const {
    const Lighting l = lighting(towardViewer);
    return std::min(1.0, l.scene + l.ambient + l.diffuse * std::max(0.0, n.normalized().dot(l.dir)));
}

Style readStyle(const skp::SkpFile& f) {
    Style s;
    const skp::Record* ro = skp::record(f, f.rendering);
    getU32(ro, skp::ro::kRenderMode, s.renderMode);
    getBool(ro, skp::ro::kJitter, s.jitter);
    std::uint32_t edgeDisplay = 1;
    getU32(ro, skp::ro::kEdgeDisplay, edgeDisplay);
    s.edges = edgeDisplay != 0;
    getColor(ro, skp::ro::kBackground, s.background);
    getColor(ro, skp::ro::kEdgeColor, s.edge);
    getColor(ro, skp::ro::kHighlight, s.highlight);
    getColor(ro, skp::ro::kConstruction, s.construction);
    getBool(ro, skp::ro::kTexture, s.textures);
    getU32(ro, skp::ro::kEdgeColorMode, s.edgeColorMode);
    getBool(ro, skp::ro::kExtensions, s.extensions);
    getU32(ro, skp::ro::kExtensionLen, s.extensionLength);
    getBool(ro, skp::ro::kProfiles, s.profiles);
    getU32(ro, skp::ro::kProfileWidth, s.profileWidth);
    getBool(ro, skp::ro::kDepthCue, s.depthCue);
    getU32(ro, skp::ro::kDepthCue + 1, s.depthCueWidth);
    getBool(ro, skp::ro::kEndpoints, s.endpoints);
    getU32(ro, skp::ro::kEndpoints + 1, s.endpointLength);
    getBool(ro, skp::ro::kHiddenGeometry, s.hiddenGeometry);
    getColor(ro, skp::ro::kFaceFront, s.faceFront);
    getColor(ro, skp::ro::kFaceBack, s.faceBack);
    getColor(ro, skp::ro::kSky, s.sky);
    if (auto h = skp::recColor(ro, skp::ro::kHorizon)) s.horizon = QColor(h->r, h->g, h->b, h->a);
    getColor(ro, skp::ro::kGround, s.ground);
    getBool(ro, skp::ro::kDrawSky, s.drawSky);
    getBool(ro, skp::ro::kDrawGround, s.drawGround);
    getU32(ro, skp::ro::kGroundTransparency, s.groundTransparency);
    getBool(ro, skp::ro::kModelTransparency, s.xray);
    getBool(ro, skp::ro::kAxes, s.axes);
    getBool(ro, skp::ro::kBackEdges, s.backEdges);

    const skp::Record* sh = skp::record(f, f.shadow);
    getU32(sh, skp::sh::kTime, s.shadowTime);
    getF64(sh, skp::sh::kLatitude, s.latitude);
    getF64(sh, skp::sh::kLongitude, s.longitude);
    getF64(sh, skp::sh::kTimeZone, s.timeZone);
    double nx = 0, ny = 1, nz = 0;
    getF64(sh, skp::sh::kNorth, nx);
    getF64(sh, skp::sh::kNorth + 1, ny);
    getF64(sh, skp::sh::kNorth + 2, nz);
    s.north = geom::V3{nx, ny, nz};
    if (s.north.length() < 1e-9) s.north = {0, 1, 0};
    getU32(sh, skp::sh::kLight, s.light);
    getU32(sh, skp::sh::kDark, s.dark);
    getBool(sh, skp::sh::kUseSun, s.useSun);
    return s;
}

void writeStyle(skp::SkpFile& f, const Style& s) {
    skp::Record* ro = skp::record(f, f.rendering);
    put(ro, skp::ro::kRenderMode, s.renderMode);
    put(ro, skp::ro::kJitter, s.jitter);
    put(ro, skp::ro::kEdgeDisplay, static_cast<std::uint32_t>(s.edges ? 1 : 0));
    putColor(ro, skp::ro::kBackground, s.background);
    putColor(ro, skp::ro::kEdgeColor, s.edge);
    putColor(ro, skp::ro::kFaceFront, s.faceFront);
    putColor(ro, skp::ro::kFaceBack, s.faceBack);
    putColor(ro, skp::ro::kSky, s.sky);
    putColor(ro, skp::ro::kGround, s.ground);
    put(ro, skp::ro::kTexture, s.textures);
    put(ro, skp::ro::kExtensions, s.extensions);
    put(ro, skp::ro::kProfiles, s.profiles);
    put(ro, skp::ro::kDepthCue, s.depthCue);
    put(ro, skp::ro::kEndpoints, s.endpoints);
    put(ro, skp::ro::kHiddenGeometry, s.hiddenGeometry);
    put(ro, skp::ro::kDrawSky, s.drawSky);
    put(ro, skp::ro::kDrawGround, s.drawGround);
    put(ro, skp::ro::kModelTransparency, s.xray);
    put(ro, skp::ro::kAxes, s.axes);
    put(ro, skp::ro::kBackEdges, s.backEdges);

    skp::Record* sh = skp::record(f, f.shadow);
    put(sh, skp::sh::kTime, s.shadowTime);
    put(sh, skp::sh::kLight, s.light);
    put(sh, skp::sh::kDark, s.dark);
    put(sh, skp::sh::kUseSun, s.useSun);
}

}  // namespace render
