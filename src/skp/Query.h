#pragma once
// Read-only views over a loaded model.

#include "skp/Records.h"
#include "skp/SkpFile.h"

#include <QImage>

#include <optional>
#include <vector>

namespace skp {

const Component* rootComponent(const SkpFile& f);
Component* rootComponent(SkpFile& f);
const Component* componentOf(const SkpFile& f, ObjId id);  // CComponent or a definition's component
Component* componentOf(SkpFile& f, ObjId id);

std::vector<ObjId> definitions(const SkpFile& f);
std::vector<ObjId> materials(const SkpFile& f);
std::vector<ObjId> layers(const SkpFile& f);
std::vector<ObjId> scenes(const SkpFile& f);
QString definitionName(const SkpFile& f, Ref def);

Vec3d vertexPos(const SkpFile& f, Ref v);
// Vertex positions of a loop in walking order.
std::vector<Vec3d> loopPoints(const SkpFile& f, ObjId loop);
std::vector<ObjId> loopVertices(const SkpFile& f, ObjId loop);

// Record field access.
const Record* record(const SkpFile& f, Ref r);
Record* record(SkpFile& f, Ref r);
std::optional<Color> recColor(const Record* r, std::size_t i);
std::optional<bool> recBool(const Record* r, std::size_t i);
std::optional<std::uint32_t> recU32(const Record* r, std::size_t i);
std::optional<double> recF64(const Record* r, std::size_t i);
std::optional<QString> recString(const Record* r, std::size_t i);
bool setRecColor(Record* r, std::size_t i, Color c);
bool setRecBool(Record* r, std::size_t i, bool b);

// The face's texture coordinates (attribute type 0x32, GetTextureCoords 0xb4b71).
const Record* faceTextureCoords(const SkpFile& f, const Face& face);

// Material colour with transparency folded into alpha (as the API reports it).
Color materialRgba(const Material& m);
std::optional<QImage> textureImage(const SkpFile& f, const Material& m);

// 13-number transform helpers.
Vec3d xformPoint(const Xform13& t, const Vec3d& p);
Xform13 xformMul(const Xform13& a, const Xform13& b);  // a then... returns a∘b (apply b first)

// Statistics for the checker.
struct Stats {
    std::size_t faces = 0, edges = 0, instances = 0, groups = 0, definitions = 0, materials = 0, layers = 0,
                scenes = 0, objects = 0;
};
Stats stats(const SkpFile& f);

}  // namespace skp
