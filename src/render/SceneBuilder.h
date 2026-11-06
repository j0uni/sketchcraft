#pragma once
// Flattens a model into triangles and edges ready for drawing and picking.

#include "geom/Math.h"
#include "render/Style.h"
#include "skp/SkpFile.h"

#include <QImage>

#include <map>
#include <set>
#include <vector>

namespace render {

// Vertex layout shared with the viewport's shader: position, normal, uv, rgba, selected.
inline constexpr int kVertexFloats = 13;

struct Batch {
    int texture = -1;  // index into Scene::textures
    bool transparent = false;
    std::vector<float> data;  // kVertexFloats per vertex, three vertices per triangle
    std::size_t vertexCount() const { return data.size() / kVertexFloats; }
};

// What a click on the scene lands on.
struct Pick {
    skp::ObjId top = 0;  // entity in the model's root (face, edge or instance)
    skp::Ref face;       // the face hit (in its own component)
    skp::Ref edge;
    geom::M4 xform;      // local -> world of the hit entity
};

struct SceneEdge {
    geom::V3 a, b;
    geom::V3 n1, n2;  // normals of up to two faces using the edge
    int faces = 0;
    bool selected = false;
    Pick pick;
};

struct PickTri {
    geom::V3 a, b, c;
    Pick pick;
};

struct Scene {
    std::vector<Batch> batches;
    std::vector<QImage> textures;
    std::vector<SceneEdge> edges;
    std::vector<PickTri> tris;
    std::map<skp::ObjId, geom::Box> topBoxes;
    std::set<skp::ObjId> selectedInstances;
    geom::Box bounds;
    bool hasFaceMe = false;
    std::size_t faceCount = 0;
    // Guides (construction lines and points), drawn dashed in the construction colour.
    struct Guide {
        geom::V3 p, dir;
        double t0 = 0, t1 = 0;  // ±1e30 = endless (CLine3d start/end, read by operator>> 0x157eb0)
    };
    std::vector<Guide> guides;
    std::vector<geom::V3> guidePoints;
};

struct BuildOptions {
    Style style;
    geom::V3 eye{0, -1000, 500};
    std::set<skp::ObjId> selection;  // root-level entity ids
};

Scene buildScene(const skp::SkpFile& f, const BuildOptions& o);

// Texture position of a model point on a face (MECHANICS.md section 4). `ownCoords` uses the face's
// stored texture coordinates; otherwise the plain projection.
std::array<double, 2> textureUV(const skp::SkpFile& f, const skp::Face& face, const skp::Texture& tex,
                                const geom::V3& p, bool front, bool ownCoords);

bool isFaceMe(const skp::SkpFile& f, const skp::Definition& d);
bool layerHidden(const skp::SkpFile& f, skp::Ref layer);

}  // namespace render
