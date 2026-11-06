#pragma once
// Making and changing models: a new empty model and the edits the drawing tools perform.
// Edits work on one component (normally the model's root) and keep SketchUp's topology:
// vertices shared by edges, edges used by loops of edge uses, loops making up faces.

#include "geom/Math.h"
#include "skp/SkpFile.h"

#include <QString>

#include <optional>
#include <set>
#include <vector>

namespace skp {

// A model with SketchUp 2017's default settings and one layer, Layer0.
SkpFile newModel();

Ref layer0(const SkpFile& f);

using Loop3 = std::vector<geom::V3>;

// Adds an edge (or returns the one already joining the two points).
std::optional<ObjId> addEdge(SkpFile& f, ObjId comp, const geom::V3& a, const geom::V3& b);
// Adds a planar face from its outer loop and optional holes. `towards` picks which side faces front.
std::optional<ObjId> addFace(SkpFile& f, ObjId comp, const Loop3& outer, const std::vector<Loop3>& holes = {},
                             std::optional<geom::V3> towards = std::nullopt);
// Draws a loop onto an existing face: the face gets it as a hole and a new face fills it, so the two
// can be pushed and pulled apart. Returns nothing when the loop is not inside the face.
std::optional<ObjId> imprintFace(SkpFile& f, ObjId comp, ObjId host, const Loop3& loop);
// Push/pull a face along its normal by `distance` (model inches, either sign).
bool pushPull(SkpFile& f, ObjId comp, ObjId face, double distance);
void reverseFace(SkpFile& f, ObjId face);
// Erases root entities: an edge also takes the faces bounded by it.
void erase(SkpFile& f, ObjId comp, const std::set<ObjId>& ents);
// Moves geometry (by its vertices) and instances (by their transforms).
void translate(SkpFile& f, ObjId comp, const std::set<ObjId>& ents, const geom::V3& delta);
// Paints a face's front or an instance.
void paint(SkpFile& f, ObjId ent, Ref material);

ObjId addMaterial(SkpFile& f, const QString& name, Color color);
ObjId addLayer(SkpFile& f, const QString& name);
// Groups root entities into a new group; returns the group instance.
std::optional<ObjId> makeGroup(SkpFile& f, const std::set<ObjId>& ents);
// The same as a named component (a reusable definition) instead of a group.
std::optional<ObjId> makeComponent(SkpFile& f, const std::set<ObjId>& ents, const QString& name);
// Applies a transform to root entities: geometry by its vertices, instances by their transforms.
void transformEntities(SkpFile& f, ObjId comp, const std::set<ObjId>& ents, const geom::M4& m);
// Draws the face's outline moved inward by `distance` onto the face (the Offset tool).
std::optional<ObjId> offsetFace(SkpFile& f, ObjId comp, ObjId face, double distance);
// Inward offset of a planar loop wound counter-clockwise about n (empty when it collapses).
Loop3 offsetLoop(const Loop3& loop, const geom::V3& n, double distance);
// Places a component instance in the root.
std::optional<ObjId> addInstance(SkpFile& f, ObjId definition, const geom::M4& xform);
// Adds a scene that remembers a camera.
ObjId addScene(SkpFile& f, const QString& name, const Camera& cam);
// Camera of a scene, if it stores one.
std::optional<Camera> sceneCamera(const SkpFile& f, ObjId page);
QString sceneName(const SkpFile& f, ObjId page);

// Outer loop points of a face (in its component's coordinates).
Loop3 faceOuter(const SkpFile& f, const Face& face);
std::vector<Loop3> faceHoles(const SkpFile& f, const Face& face);
double faceArea(const SkpFile& f, const Face& face);

}  // namespace skp
