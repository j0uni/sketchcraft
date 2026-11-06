#pragma once
// Display settings of a model: CRenderingOptions and CShadowInfo values the renderer uses
// (MECHANICS.md sections 5 and 6).

#include "geom/Math.h"
#include "skp/SkpFile.h"

#include <QColor>

#include <cstdint>

namespace render {

// CRenderingOptions render mode (field 0x28): CStyleEditorHelper::SetRender* 0x1005c0728..0x1005c07f6 and
// -[SketchUpController renderMonochrome:] 0x100010d2c.
enum RenderMode : std::uint32_t { kWireframe = 0, kHiddenLine = 1, kShaded = 2, kMonochrome = 5 };

struct Style {
    QColor background{153, 189, 144}, sky{104, 195, 251}, ground{210, 208, 185};
    QColor edge{0, 0, 0}, highlight{0, 1, 255}, construction{0, 0, 0};
    QColor faceFront{255, 255, 255}, faceBack{164, 178, 187};
    QColor horizon{0, 0, 0, 0};  // stored horizon colour; all zero = derive it from the sky
    bool drawSky = true, drawGround = false;
    std::uint32_t groundTransparency = 50;

    std::uint32_t renderMode = kShaded;
    bool textures = true;
    bool xray = false;
    bool edges = true;
    bool profiles = true;
    std::uint32_t profileWidth = 2;
    bool depthCue = false;
    std::uint32_t depthCueWidth = 4;
    bool extensions = false;
    std::uint32_t extensionLength = 3;
    bool endpoints = false;
    std::uint32_t endpointLength = 9;
    bool jitter = false;
    bool hiddenGeometry = false;
    std::uint32_t edgeColorMode = 1;
    bool axes = true;
    bool backEdges = false;
    bool guides = true;  // View > Guides (kept per session; its stored field was not located)

    // Shadow settings.
    std::uint32_t shadowTime = 1383917400;  // Nov 8 2013, 13:30 local
    double timeZone = -7, latitude = 40.0183, longitude = -105.242;
    geom::V3 north{0, 1, 0};
    std::uint32_t light = 80, dark = 45;
    bool useSun = false;

    geom::V3 sunDirection() const;
    // The light set up by BaseSketchView::SetLightingAndViewTransformations (main program 0x100162880):
    // brightness = min(1, scene + ambient + diffuse * max(0, n·dir)). `towardViewer` is the camera
    // headlight used when the sun is not used for shading.
    struct Lighting {
        geom::V3 dir;
        double scene = 0.2, ambient = 0.2, diffuse = 0.8;
    };
    Lighting lighting(const geom::V3& towardViewer) const;
    double brightness(const geom::V3& n, const geom::V3& towardViewer) const;
};

Style readStyle(const skp::SkpFile& f);
// Writes the values this struct carries back into the model's records.
void writeStyle(skp::SkpFile& f, const Style& s);

QColor toQColor(const skp::Color& c);
skp::Color fromQColor(const QColor& c);

}  // namespace render
