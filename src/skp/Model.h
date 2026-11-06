#pragma once
// In-memory form of everything a .skp file holds (MECHANICS.md sections 2-4).
// Geometry and the parts the app edits are typed structs; settings classes the app only reads a few
// values from are kept as ordered records so that saving writes them back unchanged.

#include <QByteArray>
#include <QString>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace skp {

using ObjId = std::size_t;
using Ref = std::optional<ObjId>;

struct Color {
    std::uint8_t r = 0, g = 0, b = 0, a = 255;
    friend bool operator==(const Color&, const Color&) = default;
};

using Vec3d = std::array<double, 3>;
using Xform13 = std::array<double, 13>;  // 3x3 rows, translation, scale
using Guid = std::array<std::uint8_t, 16>;

inline constexpr Xform13 kIdentity13{1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1};

struct Entity {
    Ref attrs;
    std::uint64_t pid = 0;
};

// Drawing-element flag bits as stored (CDrawingElement::Serialize 0x96b84).
enum DrawFlag : std::size_t { kHidden = 0, kCastsShadows = 1, kReceivesShadows = 2 };

struct DrawElem {
    Entity ent;
    Ref material;
    std::array<bool, 6> flags{false, true, true, false, false, false};
    Ref layer;
    std::optional<Color> legacyColor;
};

struct Vertex {
    Entity ent;
    Vec3d pos{};
};

struct Edge {
    DrawElem de;
    Ref v0, v1, curve;
};

struct EdgeUse {
    Entity ent;
    Ref edge;
    bool reversed = false;
    Ref loop;
    std::array<Ref, 2> legacyLinks;
};

struct Loop {
    Entity ent;
    bool b0 = false, b1 = false;
    std::vector<ObjId> uses;
};

struct Face {
    DrawElem de;
    std::array<double, 4> plane{};
    std::vector<ObjId> loops;
    Ref back;
};

struct Curve {
    Entity ent;
    bool b = false;
    std::uint32_t n = 0;
    std::array<Ref, 2> legacy;
};

struct Arc3d {
    Vec3d center{}, normal{}, xaxis{};
    double start = 0, end = 0;
    Vec3d yaxis{};
};

struct ArcCurve {
    Curve curve;
    Arc3d arc;
};

struct Component {
    DrawElem de;
    Ref materials, layers, defs;
    std::vector<ObjId> ents;
    Ref relmap, section, fonts;
    std::optional<Xform13> legacyXform;
};

struct Definition {
    Component comp;
    Guid guid{};
    QString name, description, path;
    std::uint32_t time = 0;
    bool flag = false;
    Vec3d insertPoint{};
    Ref behavior;
    std::uint32_t u = 0;
    Ref thumbnail;
};

enum class InstanceKind { Component, Group, Image };

struct Instance {
    InstanceKind kind = InstanceKind::Component;
    DrawElem de;
    Ref def;
    Xform13 xform = kIdentity13;
    QString name;
    Guid guid{};
};

struct Camera {
    Vec3d eye{0, 0, 0}, target{0, 0, -1}, up{0, 1, 0};
    double d50 = 0, d58 = 0;
    bool perspective = true;
    double fov = 35, height = 100;
    Vec3d p88{};
    double aspect = 0;
    bool fovIsHeight = true, b81 = false;
    QString description;
    double db8 = 0;
    bool bc0 = false;
    double dc8 = 0, dd0 = 0, dd8 = 0;
};

struct Dib {
    std::uint32_t kind = 4;
    QByteArray data;
    std::uint32_t extra = 0;
};

struct Texture {
    Entity ent;
    Ref dib;
    double width = 0, height = 0;
    QString name;
    Color average{};
};

struct Material {
    Entity ent;
    QString name;
    Ref texture;
    bool b60 = false;
    Color color{64, 64, 64, 255};
    QString s68;
    std::uint32_t kind = 0;
    std::uint32_t u84 = 0;
    double transparency = 0;  // 0 = opaque
    bool useAlpha = false;
};

struct MaterialManager {
    Entity ent;
    std::vector<Ref> mats;
    Ref current;
};

struct Layer {
    Entity ent;
    QString name;
    bool hidden = false;
    Ref material;
    std::uint32_t u = 0;
};

struct LayerManager {
    Entity ent;
    std::vector<Ref> layers;
    Ref active;
    QString legacyActiveName;
};

struct DefinitionList {
    std::vector<Ref> defs;
};

struct AttrContainer {
    Entity ent;
    std::vector<ObjId> attrs;
};

struct Attribute {
    Entity ent;
    std::uint32_t u = 0;
};

// Typed values of named attributes (CTypedValue, operator>> 0x1571f2).
struct TypedValue {
    std::uint8_t type = 0;
    std::variant<std::monostate, std::uint8_t, std::uint16_t, std::uint32_t, float, double, bool, Color,
                 QString, std::vector<TypedValue>, Vec3d, Xform13>
        value;
};

struct AttrNamed {
    Entity ent;
    std::uint32_t u = 0;
    QString name;
    std::vector<std::pair<QString, TypedValue>> pairs;
    std::uint32_t extra = 0;
};

struct VersionMap {
    std::vector<std::pair<std::string, std::uint32_t>> entries;
};

// One field of a settings record.
using Field = std::variant<std::uint8_t, std::uint16_t, std::uint32_t, std::uint64_t, float, double, bool,
                           Color, QString, Ref, QByteArray>;

struct Record {
    std::string cls;
    std::vector<Field> fields;
};

using Object = std::variant<std::monostate, VersionMap, Vertex, Edge, EdgeUse, Loop, Face, Curve, ArcCurve,
                            Component, Definition, Instance, Camera, Dib, Texture, Material, MaterialManager,
                            Layer, LayerManager, DefinitionList, AttrContainer, Attribute, AttrNamed, Record>;

std::string classNameOf(const Object& o);
std::optional<Object> makeObjectOfClass(const std::string& cls);

}  // namespace skp
