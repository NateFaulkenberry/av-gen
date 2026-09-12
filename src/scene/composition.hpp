#pragma once

// Scene composition (milestone 0.7, ADR-017): a SceneController made of named nodes. A node
// instances a glTF asset from the registry, a built-in generator (orb, grid), a particle system,
// or a nested scene file (another composition). Nodes have a transform, visibility and material
// overrides, all exposed as parameters "nodes/<name>/..."; nested compositions are namespaced
// "nodes/<name>/nodes/<child>/...". The composition flattens into one Scene: each unique asset's
// meshes and textures are stored once, entities are instanced per node.
//
// Scene file (JSON): { "format": "avgen-scene", "version": 1, "name": "...",
//   "camera": {"distance", "height", "orbitSpeed", "fov"}, "environment": {"map": path, "intensity"},
//   "nodes": [ {"name", "kind": "gltf|orb|grid|particles|scene|procedural|field", "asset": path, "parent": "other",
//               "position": [x,y,z], "rotation": [degrees x,y,z], "scale": [x,y,z], "visible": true,
//               "emissiveBoost": 1.0, "roughnessScale": 1.0, "particles": {...settings...}} ] }
// Nodes may be parented to another node of the same composition ("parent"): the world transform
// is parent world x local (rest + parameter offsets). Cycles are errors; an unknown parent is a
// warning and the node behaves as a root. Parameter paths do not change with parenting.

#include "assets/asset_registry.hpp"
#include "core/error.hpp"
#include "graph/graph.hpp"
#include "scene/field_params.hpp"
#include "scene/light_rig.hpp"
#include "scene/material_params.hpp"
#include "scene/sdf_object.hpp"
#include "scene/spline_params.hpp"
#include "scene/floaters.hpp"
#include "scene/particles.hpp"
#include "entity/entity.hpp"
#include "entity/obstacles.hpp"
#include "scene/scene_controller.hpp"
#include "world/city.hpp"
#include "world/ecology.hpp"
#include "world/hero.hpp"
#include "world/terrain.hpp"
#include "world/terrain_query.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace avgen::scene {

// Terrain (ADR-046): the node kind that turns a WorldMap into ground. It is a node rather than a
// property of the scene because a composition may hold more than one world, and because everything
// a node already has -- a transform, visibility, a material, parameters -- is what terrain needs.
// `Group` is an empty transform. It draws nothing and generates nothing; its whole purpose is to
// be something other nodes can be parented to, so an artist's arrangement of twenty rocks is one
// thing that moves as a unit and stays twenty rocks you can still select individually (ADR-092,
// world-authoring-spec §26). Parenting, the world transform, the parameters and the file format
// already did all of that work; a group is the node kind that had been missing to use it.
// Euler angles in degrees <-> quaternion, in the convention every node's "rotation" field and
// every "nodes/<name>/rotation" parameter uses: glm::quat(vec3) builds Rz * Ry * Rx, and the
// recovery uses atan2 rather than glm::eulerAngles because asin loses precision near +-90 degrees.
// Declared here because the editor has to go the other way -- a gizmo produces a rotation and has
// to write the parameter -- and a seventh private copy of this pair would be a seventh chance for
// one of them to disagree about the order.
[[nodiscard]] glm::quat quatFromEulerDegrees(const glm::vec3& degrees);
[[nodiscard]] glm::vec3 eulerDegrees(const glm::quat& q);

// The world-space axis-aligned box a node occupies (ADR-092). Invalid when the node draws nothing:
// a group, or a node whose asset failed to load. "Empty" and "at the origin" are different answers
// and an editor that confuses them draws a selection outline around a point in space.
struct WorldBounds {
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};
    bool valid = false;

    [[nodiscard]] glm::vec3 centre() const { return (min + max) * 0.5f; }
    [[nodiscard]] glm::vec3 size() const { return max - min; }
    [[nodiscard]] float radius() const { return glm::length(size()) * 0.5f; }
    void include(const glm::vec3& p) {
        if (!valid) { min = max = p; valid = true; return; }
        min = glm::min(min, p);
        max = glm::max(max, p);
    }
    void include(const WorldBounds& other) {
        if (!other.valid) { return; }
        include(other.min);
        include(other.max);
    }
};

enum class NodeKind : std::uint8_t { Gltf, Orb, Grid, Particles, Scene, Procedural, Field, Spline, Sdf, Terrain, Group, City };
const char* nodeKindName(NodeKind kind);
Result<NodeKind> nodeKindFromName(const std::string& name);

struct MaterialPartParameters {
    params::Parameter<glm::vec3>* tint = nullptr;
    params::Parameter<float>* emissiveGain = nullptr;
    params::Parameter<float>* roughnessScale = nullptr;
    params::Parameter<float>* opacityScale = nullptr;
    // The colour this part emits. Black means "whatever the material already had", because an
    // emission of zero and an emission that is black are the same picture, so black is free to
    // mean something else. Without it a part can only be scaled: `emissiveGain` multiplies, and a
    // multiplier cannot light a lamp whose glTF emissiveFactor is [0,0,0] -- which is every lamp
    // in every asset exported without emission, i.e. most of them. A part that cannot be given a
    // colour cannot be given a *different* colour from its neighbour either, and driving different
    // parts of one object from different bands is the entire reason parts are addressable.
    params::Parameter<glm::vec3>* emissiveColor = nullptr;

    void apply(Material& material) const;
};

// What a scene file says about a skinned character on a Gltf node (ADR-086). Everything here is
// declarative and optional: a glTF with a skin in it animates on its first clip without any of it.
//
// This is the *authored* half of the seam. The other half is direct: a behaviour reaches
// `composition.scene().rigs[entity.rig].player` and calls play() with the timeline second it
// decided at. The two do not fight -- the node re-applies its `state` only when the request
// changes or a rebuild has just replaced the rig -- so a behaviour may take a character over and
// keep it.
struct NodeAnimation {
    std::string state;        // the state to enter ("" = leave the rig on its default)
    float blend = -1.0f;      // cross-fade seconds; < 0 = the state's own blendIn
    float speed = 1.0f;       // clip seconds per timeline second
    float updateHz = 0.0f;    // pose rate ceiling; 0 = every frame when near the camera
    float cullDistance = 120.0f; // metres beyond which the rig is not posed at all (0 = never cull)
    [[nodiscard]] bool authored() const {
        return !state.empty() || blend >= 0.0f || speed != 1.0f || updateHz != 0.0f ||
               cullDistance != 120.0f;
    }
};

struct CompositionNode {
    std::string name;
    NodeKind kind = NodeKind::Gltf;
    std::filesystem::path asset;   // gltf/glb for Gltf, scene file for Scene (as written; resolved via registry)
    std::string parent;            // name of the parent node ("" = root); world = parent world x local
    Transform transform;           // local transform of the instance
    bool visible = true;
    // Locked out of the pointer, the way a locked layer is in an image editor. It still renders and
    // still belongs to the scene -- it simply stops answering clicks and box selections, so the
    // ground plane a world is built on stops being what you select every time you aim at something
    // standing on it.
    //
    // Not a parameter, and deliberately not undoable: locking is a statement about how you are
    // working, not an edit to the work. It is saved with the scene, because which things you had
    // put out of the way is worth keeping between sessions.
    bool locked = false;
    float emissiveBoost = 1.0f;
    float roughnessScale = 1.0f;
    ParticleSystem particles;      // settings for kind Particles (name is taken from the node)
    ProceduralGeometry procedural; // settings for kind Procedural (ADR-023; name is taken from the node)
    // ADR-044: whether the scene file wrote a `material` block for this node. A mesh source takes
    // the asset's own material unless the scene deliberately overrode it, and "the author wrote
    // nothing" is not the same as "the author wrote the defaults".
    bool proceduralMaterialAuthored = false;
    spatial::FieldSpec field;      // settings for kind Field (ADR-025; name is taken from the node; the node
                                   // transform is the field's frame, folded into the FieldSpec at rebuild)
    spatial::Spline spline;        // settings for kind Spline (ADR-026; the node transform is applied to the
                                   // generated control points at rebuild)
    SdfObject sdf;                 // settings for kind Sdf (ADR-027; node transform folded into sdf.transform)
    world::WorldMap worldMap;      // settings for kind Terrain (ADR-046): the geography
    world::TerrainSettings terrain;// settings for kind Terrain: how it is chopped up and coarsened
    world::WaterFlowSettings waterFlow; // settings for kind Terrain (ADR-099): how fast the water runs
    Material terrainMaterial;      // settings for kind Terrain: shared by every chunk
    world::Ecology ecology;        // settings for kind Terrain (ADR-048): what grows on it
    // Settings for kind City (ADR-100). The node carries the *description*, never the placements:
    // a scatter cloud is a runtime shared_ptr and is not serialised, exactly as a terrain's ecology
    // scatter is not, so a city is re-planned and re-placed on every rebuild from these few numbers.
    // That is what makes it survive a save and a reload.
    world::CitySettings city;
    std::filesystem::path cityLibrary; // the tiling manifest, as written; resolved via the registry
    std::size_t cityCells = 0;         // what the last rebuild planned, for the editor to show

    NodeAnimation animation;       // ADR-086; Gltf nodes whose asset carries a skin

    // Runtime (not serialised)
    std::shared_ptr<const assets::SceneAsset> sceneAsset; // Gltf
    std::vector<RigId> rigs;       // ADR-086: this node's rigs in the flattened scene
    std::string animationApplied;  // the state `animationAppliedAt` refers to
    double animationAppliedAt = 0.0; // the timeline second the request was made (kept across rebuilds)
    bool animationPushed = false;  // cleared by a rebuild: push the same request at the same second
    bool animationRebase = false;  // ADR-089: force the phase origin, even re-entering the same state
    std::unique_ptr<class Composition> child;              // Scene (nested)
    params::Parameter<glm::vec3>* positionParam = nullptr;
    params::Parameter<glm::vec3>* rotationParam = nullptr; // Euler degrees
    params::Parameter<glm::vec3>* scaleParam = nullptr;
    params::Parameter<bool>* visibleParam = nullptr;
    params::Parameter<float>* emissiveParam = nullptr;
    params::Parameter<float>* roughnessParam = nullptr;
    // Scale and tint for the lights this node's asset contributed. Registered for every node and
    // inert on one that brought none, exactly as `emissiveBoost` is on a node with no emission.
    params::Parameter<float>* lightIntensityParam = nullptr;
    params::Parameter<glm::vec3>* lightColorParam = nullptr;
    ParticleParameters particleParams;
    ParticleSystem particleRest;
    ProceduralParameters proceduralParams;
    ProceduralGeometry proceduralRest;
    // ADR-099 §13: when set, this Procedural node's instances are not scattered on the ground --
    // they float on the named terrain node's water and drift with it, recomputed every frame from
    // the timeline clock. Everything else about the node is unchanged: the same imported mesh, the
    // same material, the same GPU culling and LOD.
    std::optional<FloatSpec> floats;
    bool floatWarned = false;      // the "nothing will float" warning is said once, not per frame
    // ADR-044: a multi-material asset is one procedural object per material. `proceduralRest` is
    // part 0 -- the one carrying the most surface area, and the one the node's parameters were
    // registered from; these are the rest copies of the others, identical to it but for their mesh
    // and their material. Built at rebuild, applied alongside it every frame.
    std::vector<ProceduralGeometry> proceduralSubRest;
    std::vector<MaterialPartParameters> materialPartParams;
    // Part index -> the material name the asset gave it, filled at rebuild for an imported mesh
    // source (ADR-044) and empty for everything else. This is what lets a reaction be written
    // against a name an artist can see in the model file rather than against an area-ordered index
    // nobody can predict: see entity::EntityWorld::resolveTarget.
    std::vector<std::string> materialPartNames;
    FieldParameters fieldParams;
    spatial::FieldSpec fieldRest;
    SplineParameters splineParams;
    spatial::Spline splineRest;
    SdfParameters sdfParams;
    SdfObject sdfRest;
    // Terrain: what the last flatten produced, kept so the next one does not produce it again
    // (ADR-092). A terrain's chunk meshes and its ecology scatter are pure functions of the map,
    // the terrain settings and the ecology -- and on a Glowmere world they are ~390 ms of a
    // rebuild, most of it `world::scatter` walking a quarter of a million grid cells. `dirty_` has
    // no granularity: adding one flower re-flattens the world, so without this an artist painting
    // a meadow pays for the whole terrain once per dab.
    //
    // Keyed on the hash of exactly those inputs, so a terrain that *has* changed rebuilds and one
    // that has not does not. Cleared by `setEcology`/`setWorldMap`-shaped edits by virtue of the
    // hash moving; nothing has to remember to invalidate it.
    struct TerrainProducts {
        std::uint64_t hash = 0; // 0 means nothing is cached
        std::vector<std::shared_ptr<spatial::PointCloud>> clouds; // one per ecology layer, in order
        std::vector<world::GlowCluster> glow;
        // Chunk mesh ids are stored **relative to the first mesh this terrain contributed**, because
        // the absolute ids depend on what else the scene flattened before it and that changes
        // whenever a node is added. They are rebased on reuse.
        std::vector<world::TerrainChunk> chunks;
        std::vector<MeshData> meshes; // in the order buildTerrain emitted them
        [[nodiscard]] bool usable(std::uint64_t want) const {
            return hash != 0 && hash == want && !meshes.empty();
        }
    };
    TerrainProducts terrainProducts;
    std::vector<world::TerrainChunk> chunks;  // Terrain: built at rebuild, indexed by entity offset
    // Terrain (ADR-099): the water bodies derived from this node's map, built at rebuild. The
    // surface mesh's flow lanes come from it, and so does every floating thing on it.
    world::WaterBodySet waterBodies;
    // Terrain (ADR-099): this node's slot in Scene::waters, or -1 when it has no water. Set at
    // rebuild; the per-frame parameter pass writes through it.
    int waterSurfaceIndex = -1;
    // Terrain: the emissive scatter layers reduced to soft emitters, built at rebuild. The
    // per-frame pass picks the ones near the camera and makes them lights (ADR-053).
    std::vector<world::GlowCluster> glow;
    params::Parameter<bool>* terrainLodParam = nullptr;   // Terrain: LOD selection on/off (debug)
    params::Parameter<bool>* terrainCullParam = nullptr;  // Terrain: frustum culling on/off (debug)
    params::Parameter<float>* terrainLodDistanceParam = nullptr;
    params::Parameter<float>* terrainViewDistanceParam = nullptr;
    // Terrain (ADR-099): the water's own modulation surface. These are the properties §15 asks a
    // signal to reach, and they are ordinary parameters so they reach it through the ModRoute
    // chain every other reactive property in this engine uses, not a second one.
    params::Parameter<float>* waterGlowParam = nullptr;
    params::Parameter<float>* waterSparkleParam = nullptr;
    params::Parameter<float>* waterRippleParam = nullptr;
    params::Parameter<float>* waterFlowSpeedParam = nullptr;
    params::Parameter<float>* waterSwellParam = nullptr;
    params::Parameter<float>* waterFoamParam = nullptr;
    params::Parameter<glm::vec3>* waterGlowColorParam = nullptr;
};

// A copy of everything *authored* about a node -- exactly the fields the scene file writes -- with
// every piece of runtime state left behind for `addNode` to rebuild: the loaded glTF asset, the
// nested child composition, the parameter pointers, the rest copies, a terrain's built chunks.
//
// This is what duplication and the clipboard copy (ADR-092). It is a named function rather than a
// copy constructor because CompositionNode deliberately is not copyable -- it owns a nested
// Composition -- and because the list of fields here is the list a new authored field has to be
// added to. If a duplicate ever comes back missing something, this is the function that forgot it.
[[nodiscard]] CompositionNode cloneNodeSpec(const CompositionNode& node);

class Composition final : public SceneController {
public:
    Composition(assets::AssetRegistry& registry, std::string name = "composition");
    ~Composition() override;

    // ---- SceneController ----
    [[nodiscard]] std::string name() const override { return name_; }
    void update(const FrameTime& time) override;
    void updateBehaviour(const FrameTime& time, const signals::SignalBus& bus) override;
    void updateFields(const FrameTime& time, signals::SignalBus& bus, params::Modulator& modulator) override;
    [[nodiscard]] const Scene& scene() const override { return scene_; }
    [[nodiscard]] Scene& scene() override { return scene_; }

    // ---- nodes ----
    // Loads the node's asset (Gltf / Scene kinds) through the registry; names are made unique.
    // Registers the node's parameters immediately when the composition is attached.
    Result<CompositionNode*> addNode(CompositionNode node);
    // Removes a node; its children are re-parented to the removed node's parent.
    bool removeNode(const std::string& name);
    // The same removal, handing the node back instead of destroying it (ADR-092). Undo needs the
    // node itself rather than a description of it: a CompositionNode carries a loaded scene asset,
    // a nested child composition, a procedural rest copy and a terrain's built chunks, and a
    // round trip through the scene-file JSON would quietly drop whatever the format does not
    // write. Moving the node out keeps all of it, exactly, for the cost of a pointer.
    //
    // Returns nullptr when no node of that name exists. The returned node's parameter pointers are
    // already cleared -- it is out of the parameter set -- so it is safe to hold across any number
    // of frames and hand back to addNode().
    [[nodiscard]] std::unique_ptr<CompositionNode> detachNode(const std::string& name);
    [[nodiscard]] CompositionNode* findNode(const std::string& name);
    [[nodiscard]] const CompositionNode* findNode(const std::string& name) const;
    // Which node owns scene entity `entityIndex`, or nullptr. This is what a viewport click
    // resolves through: the identifier target records a scene entity index (ADR-035) and a person
    // selects a *node*, so somebody has to hold the mapping. The composition already does -- it
    // flattens nodes into entity ranges -- and exposing the lookup is better than a second table
    // built beside it that would drift the first time a node stopped emitting geometry.
    [[nodiscard]] const CompositionNode* nodeForEntity(std::size_t entityIndex) const;
    // The node that owns a procedural, for resolving a click on anything scattered. A node with a
    // multi-material asset owns a *run* of procedurals -- one per material over the same cloud --
    // so this is a range test, not an equality one. Without that, clicking the second material of a
    // two-material kerb selects nothing while clicking the first works.
    [[nodiscard]] const CompositionNode* nodeForProcedural(std::size_t proceduralIndex) const;

    // ---- skinned characters (ADR-086) ----
    // Asks every rig `nodeName` owns to enter `state` at timeline second `now`, cross-fading over
    // `blend` seconds (< 0 = the state's own) and running at `speed` clip seconds per timeline
    // second. False when there is no such node. The declarative route; a behaviour wanting
    // frame-by-frame control drives scene().rigs[...].player itself.
    //
    // `rebase` (ADR-089) forces the clip's phase origin to `now` even when that state is already
    // the current one. The two callers want opposite things and both are right: a *behaviour* calls
    // this every frame and must not restart the walk it is already walking, while a *sequencer*
    // cues the same walk at 0:12 and again at 1:04 and means two different phases -- and a scrub
    // backwards means the earlier one again. Idempotent either way: a request identical to the one
    // already in force returns immediately, so calling it per frame costs a string compare.
    bool setNodeAnimation(const std::string& nodeName, const std::string& state, double now,
                          float blend = -1.0f, float speed = 1.0f, bool rebase = false);
    // What the last update() spent on posing, and how many rigs it skipped.
    [[nodiscard]] const RigStats& rigStats() const { return rigStats_; }

    // Re-parents `name` under `parent` ("" = root). Errors: unknown node, a cycle.
    Result<void> setParent(const std::string& name, const std::string& parent);
    // World transform of a node (parent chain applied, parameters included), without the root
    // scale/rotation. Unknown parents are treated as roots.
    [[nodiscard]] Transform nodeWorldTransform(const CompositionNode& node) const;
    // What a node actually occupies in the world, measured from the flattened scene rather than
    // from the asset file: the box the editor outlines, sits a gizmo in the middle of, and tests a
    // box-selection against has to be the box that is on screen. A Group has no geometry of its
    // own, so its bounds are the union of its descendants' -- which is what makes a group possible
    // to grab. Rebuilds the scene first when it is dirty, because bounds read from a stale
    // flattening are bounds of the world as it was before the last edit.
    [[nodiscard]] WorldBounds nodeBounds(const std::string& name);
    [[nodiscard]] const std::vector<std::unique_ptr<CompositionNode>>& nodes() const { return nodes_; }
    // ---- composition (ADR-038) ----
    // What the frame is about: focal points, depth layers and exclusion regions. Its fields are
    // appended to the scene's field set at every rebuild under reserved "composition.*" names.
    [[nodiscard]] const CompositionData& composition() const { return compositionData_; }
    void setComposition(CompositionData data) {
        compositionData_ = std::move(data);
        dirty_ = true;
    }

    // ---- heroes (ADR-072, authored in ADR-074) ----
    // What in this scene is worth travelling towards. A peer of CompositionData rather than a part
    // of it: focal points say where the frame should point, and a hero says what the thing there
    // *is* -- how big, how important, what colour it owns and how it answers the music. Until now
    // the only producer of heroes was the world composer, so a hand-authored scene had nothing that
    // a camera director could be pointed at and no route by which a reaction profile could reach
    // Glowmere's elder.
    //
    // Deliberately does not mark the composition dirty and never reaches `scene_`: heroes are a
    // description of what has already been placed by the scene's own nodes, so declaring one must
    // not be able to move, resize or relight anything. A rebuild triggered from here would be a
    // rebuild that only risks changing a frame.
    [[nodiscard]] const std::vector<world::HeroPoint>& heroes() const { return heroes_; }
    // Rejects the whole set rather than dropping the bad member, and names it. A hero silently
    // dropped is a camera director that frames nothing with no explanation of why.
    Result<void> setHeroes(std::vector<world::HeroPoint> heroes);

    // ---- the ground (§3, ADR-090) --------------------------------------------------------------
    //
    // The scene's spatial queries: height, normal, slope, walkability, water, occupancy and the
    // nearest valid point, over this composition's terrain node, its ecology and its heroes. This is
    // the accessor everything that needs to know about the ground should use -- navigation, water
    // placement, the editor's placement preview, a scatter pass -- rather than reaching for the
    // terrain node's `worldMap` and re-deriving the parts.
    //
    // Returned by value and cheap (pointers, a span and a few floats), but it borrows from this
    // composition: it is valid until the terrain node, the ecology or the hero list changes. Take it
    // where you use it rather than holding one across a rebuild.
    //
    // `obstacles` is left null: §5's per-object set belongs to navigation, and a query with no
    // obstacle field says so through `hasObstacles()` rather than pretending the world is empty.
    [[nodiscard]] world::TerrainQuery terrainQuery() const;

    // ---- entities (ADR-088) ------------------------------------------------------------------
    //
    // The `entities` array of a scene file: what in this scene moves on its own and how it answers
    // the music. A peer of `heroes` for the same reason heroes are a peer of the composition data
    // -- the nodes are already placed, and this says what drives them.
    //
    // Setting entities does not mark the composition dirty: an entity moves a node by writing its
    // transform parameters, and nothing it can do requires geometry to be rebuilt.
    [[nodiscard]] const std::vector<entity::EntityDesc>& entities() const { return entityDescs_; }
    [[nodiscard]] const entity::EntityWorld& entityWorld() const { return entityWorld_; }
    [[nodiscard]] entity::EntityWorld& entityWorld() { return entityWorld_; }
    // Rejects the whole set and names the offender rather than dropping one, for the same reason
    // setHeroes does: an entity silently missing is a scene that does nothing with no explanation.
    Result<void> setEntities(std::vector<entity::EntityDesc> entities);

    // ---- trigger volumes and music influence fields (ADR-097) --------------------------------
    //
    // The `fields` array of a scene file, a sibling of `entities` for the same reason: the volumes
    // are places in the world, and what they do is scale the reactions the entities already have.
    [[nodiscard]] const std::vector<entity::FieldDesc>& fields() const { return fieldDescs_; }
    Result<void> setFields(std::vector<entity::FieldDesc> fields);
    // The profile library this scene named, or an empty one. Held so a save can write the name
    // back and an editor can list what an entity may choose from.
    [[nodiscard]] const std::string& entityProfileLibraryPath() const { return profileLibraryPath_; }
    void setEntityProfileLibraryPath(std::string path) { profileLibraryPath_ = std::move(path); }
    // Rebuilds the entity layer's view of this composition -- which node each entity drives, what
    // its material parts are called, where the ground is -- and reinstalls its reaction routes on
    // the modulator. Called from attach() and after a rebuild; safe to call again.
    void installEntities();
    // Everything the entity layer could not resolve. Empty when all of it resolved.
    [[nodiscard]] const std::vector<std::string>& entityProblems() const { return entityWorld_.problems(); }

    // ---- procedural graph (ADR-028) ----
    // A composition is either graph-driven or flat: installing a graph replaces every node this
    // composition previously installed from a graph (hand-added nodes are left alone). The graph
    // is evaluated on load and whenever `markGraphDirty()` is called; its emitted objects become
    // ordinary nodes with ordinary parameters, and its routes are added to the modulator.
    Result<void> setGraph(graph::Graph graph, params::Modulator* modulator = nullptr);
    [[nodiscard]] const graph::Graph* graph() const { return graph_ ? &*graph_ : nullptr; }
    [[nodiscard]] graph::Graph* graph() { return graph_ ? &*graph_ : nullptr; }
    void markGraphDirty() { graphDirty_ = true; }

    // ---- interactive regeneration (docs/application-performance.md) ---------------------------
    //
    // Procedural regeneration is legitimate work -- change the knob, the geometry changes -- but it
    // is done inside the frame, so while a slider is being dragged the editor's frame rate becomes
    // the regeneration rate. Measured on examples/world/glowmere-stylized.json: dragging
    // `procedural/elder-stem/hierarchy/depth` to 4 put every following frame at 145-216 ms, i.e.
    // 5-7 fps, for as long as the drag continued.
    //
    // With a budget set, an object whose last regeneration cost more than `budgetMs` waits for its
    // inputs to stop moving before regenerating again, rather than chasing every frame of a drag.
    // It still regenerates periodically during a long continuous drag, at a rate that scales with
    // what it costs, so the picture keeps up without the editor stopping.
    //
    // Zero -- the default -- means regenerate whenever the inputs change, which is the behaviour
    // offline rendering requires and gets: the deferral reads a wall clock, and a wall clock has no
    // business deciding what a deterministic render contains. Only the live editor sets it.
    void setInteractiveRebuildBudget(double budgetMs) { interactiveRebuildBudgetMs_ = budgetMs; }
    [[nodiscard]] double interactiveRebuildBudget() const { return interactiveRebuildBudgetMs_; }
    // Objects currently holding a regeneration back, for the editor to show. Never a silent state:
    // geometry that is deliberately a few frames behind the slider has to say so.
    [[nodiscard]] std::size_t proceduralsAwaitingRebuild() const;

    // Per-object regeneration cost and deferral state, indexed into the flattened procedural list.
    // Cleared by rebuild(), which is also what rebuilds that list, so the indices cannot go stale.
    // Public because the policy that reads it is a free function -- see advanceRebuildDeferral in
    // composition.cpp, which is pure and is where the behaviour is pinned.
    //
    // Two timers, not one, and they answer different questions. `settledForMs` asks "have the
    // inputs stopped moving?" and restarts whenever they move again -- a drag restarts it every
    // frame, which is what keeps the drag out of the regeneration. `heldForMs` asks "how long has
    // this object been wrong?" and only resets when it is actually regenerated, so a drag that goes
    // on for seconds still gets a refresh. With one timer the second question cannot be asked at
    // all: the first thing a moving target does is reset it.
    struct ProceduralRebuildState {
        double lastMs = 0.0;            // what this object's last regeneration actually cost
        double settledForMs = 0.0;      // since its inputs last moved
        double heldForMs = 0.0;         // since it first wanted to regenerate and was not let
        bool deferring = false;
        std::uint64_t deferredHash = 0; // the hash it is waiting to reach
    };
    void clearGraph();
    [[nodiscard]] const std::vector<std::string>& graphWarnings() const { return graphWarnings_; }
    // Re-evaluates the graph when dirty and installs the result (called by update()).
    Result<void> evaluateGraph(double time);

    // ---- simulated grid fields (scene-level, ADR-032) ----
    // A grid is referenced by name from a `FieldKind::Grid` field; only its settings are
    // serialised (`"grids"` in the scene file), never its cell values.
    Result<void> addGrid(spatial::GridField grid);
    [[nodiscard]] const std::vector<spatial::GridField>& grids() const { return grids_; }
    [[nodiscard]] std::vector<spatial::GridField>& grids() { return grids_; }

    // ---- material programs (scene-level, ADR-030) ----
    Result<void> addMaterialProgram(MaterialProgram program); // registers parameters when attached
    [[nodiscard]] const std::vector<MaterialProgram>& materialPrograms() const { return materialPrograms_; }
    void setCameraSpline(std::string name) { cameraSplineSetting_ = std::move(name); }
    // ---- viewport (ADR-046) ----
    // The window this composition will be drawn into, set by whoever renders it, once per frame
    // before update(). Terrain LOD is a screen-space decision -- how large a chunk's quads are in
    // pixels -- so it needs the viewport as well as the lens, and the chunk frustum cull needs its
    // aspect. The default is the 900-pixel reference height `lodDistance` is authored against and
    // an aspect below the conservative cull floor, so a caller that never sets it picks exactly the
    // levels it always did.
    void setViewport(std::uint32_t width, std::uint32_t height);
    [[nodiscard]] std::uint32_t viewportWidth() const { return viewportWidth_; }
    [[nodiscard]] std::uint32_t viewportHeight() const { return viewportHeight_; }
    [[nodiscard]] std::size_t nodeCount() const { return nodes_.size(); }

    // ---- parameters ----
    // Registers camera/environment/scene parameters and every node's parameters under `prefix`
    // (empty for the root; "nodes/<name>/" for nested children) and adds default routes (root only).
    void attach(params::ParameterSet& params, params::Modulator& modulator, const std::string& prefix = "");
    void detach();   // forgets parameter pointers (before the parameter set is cleared)
    [[nodiscard]] bool attached() const { return params_ != nullptr; }

    // ---- framing ----
    [[nodiscard]] glm::vec3 boundsCenter() const { return center_; }
    [[nodiscard]] float boundsRadius() const { return radius_; }

    // ---- files ----
    [[nodiscard]] nlohmann::json toJson() const;
    static Result<std::unique_ptr<Composition>> fromJson(const nlohmann::json& j, assets::AssetRegistry& registry,
                                                         int depth = 0);
    [[nodiscard]] Result<void> saveFile(const std::filesystem::path& path) const;
    static Result<std::unique_ptr<Composition>> loadFile(const std::filesystem::path& path, assets::AssetRegistry& registry,
                                                         int depth = 0);
    static constexpr int kMaxNestingDepth = 4;
    static constexpr const char* kFormatName = "avgen-scene";
    static constexpr int kFormatVersion = 1;

    // Environment map path (relative or absolute as given); empty = none.
    void setEnvironmentMap(const std::filesystem::path& path);
    [[nodiscard]] const std::filesystem::path& environmentMap() const { return environmentPath_; }
    // ADR-059: the file's `post` block, verbatim. The composition does not interpret it -- post is
    // the Engine's, and these become parameter base values when the scene loads -- but it holds it
    // so a round trip writes back exactly what was read.
    [[nodiscard]] const nlohmann::json& postJson() const { return postJson_; }
    // Light rig (ADR-033): `"lightRig"` in the scene file's environment block, resolved through the
    // asset registry and expanded into `Scene::lights` around the composition's bounds every frame,
    // so `followCamera` rig lights track the camera. An empty path clears the rig and restores the
    // default key light. A rig that fails to load is a warning, not an error.
    Result<void> setLightRig(const std::filesystem::path& path);
    // Installs a rig built in memory. Named apart from setLightRig so `setLightRig({})` keeps
    // meaning "clear the rig" rather than becoming ambiguous. A generated world's art direction describes a rig -- a key
    // that rakes over an ambient that stays out of its way -- and writing that to a temporary file
    // so the path overload could read it back would be a file nobody asked for. `sourcePath` is
    // what the scene will remember it as, and may be empty for a rig that has no file.
    Result<void> installLightRig(LightRig rig, const std::filesystem::path& sourcePath = {});
    [[nodiscard]] const std::filesystem::path& lightRigPath() const { return lightRigPath_; }
    [[nodiscard]] const LightRig* lightRig() const { return lightRig_ ? &*lightRig_ : nullptr; }
    [[nodiscard]] const std::filesystem::path& sourcePath() const { return sourcePath_; }

    static void addDefaultRoutes(params::Modulator& modulator);

private:
    void rebuild();          // flattens nodes into scene_ (meshes/textures/entities/particles)
    void ensureBuilt();      // rebuild() when dirty
    void applyParameters(); // node finals -> transforms/materials/particles; camera; environment
    // Aims the camera so the composition's focal point lands at its requested screen position.
    void applyFraming();
    // Per frame, after the camera is known: picks each terrain chunk's LOD mesh and culls the ones
    // outside the frustum or beyond the view distance. Changes no geometry, only which mesh each
    // chunk entity points at, which is why a camera can fly across a world for free.
    void updateTerrainLod();
    void updateWaterSurfaces(); // ADR-099: the water parameters into Scene::waters, once a frame
    void updateFloaters(double time); // ADR-099 §13: drifting instances, once a frame
    std::vector<Floater> floaterScratch_; // reused by updateFloaters so a drifting layer allocates once
    void updateEcologyLights();
    void registerNodeParameters(CompositionNode& node);
    void unregisterNodeParameters(CompositionNode& node);
    void unregisterParameters(); // removes every parameter this composition registered, then detach()
    std::string uniqueName(const std::string& base) const;
    [[nodiscard]] std::string nestedPrefix(const CompositionNode& node) const;
    void rebuildProcedurals();  // (re)generates every procedural object against the flattened scene
    std::vector<ProceduralRebuildState> proceduralRebuild_;
    std::chrono::steady_clock::time_point lastRebuildPollTime_{};
    void rebuildSdfs();
    [[nodiscard]] Transform nodeTransform(const CompositionNode& node) const; // params or authored values (local)
    // True when making `parent` the parent of `node` would close a cycle (node and parent by name).
    [[nodiscard]] bool wouldCycle(const std::string& node, const std::string& parent) const;
    // Whether a node is drawn: its own `visible`, and every ancestor's. Inherited, because a group
    // is a thing an artist hides -- hiding "the village" and watching the houses stay up is not a
    // subtlety, it is the feature not working. Non-static for that reason; it has to walk parents.
    [[nodiscard]] bool nodeVisible(const CompositionNode& node) const;
    [[nodiscard]] float fitDistance() const;
    // Loads a nested scene file for a Scene node (cycle and depth checks against this chain).
    [[nodiscard]] Result<std::unique_ptr<Composition>> loadChild(const std::filesystem::path& asset) const;
    static Result<std::unique_ptr<Composition>> fromJsonImpl(const nlohmann::json& j,
                                                             assets::AssetRegistry& registry, int depth,
                                                             std::vector<std::filesystem::path> ancestors,
                                                             std::filesystem::path sourcePath);
    static Result<std::unique_ptr<Composition>> loadNested(const std::filesystem::path& path,
                                                           assets::AssetRegistry& registry, int depth,
                                                           std::vector<std::filesystem::path> ancestors);

    assets::AssetRegistry& registry_;
    std::string name_;
    std::filesystem::path sourcePath_;
    std::vector<std::filesystem::path> ancestors_; // enclosing scene files, outermost first
    int depth_ = 0;
    std::filesystem::path environmentPath_;
    std::filesystem::path lightRigPath_;
    std::optional<LightRig> lightRig_;   // the authored rig (parameter defaults)
    LightRigParameters lightRigParams_;
    std::size_t rigLightCount_ = 0;      // lights the rig appended to scene_.lights last frame
    std::size_t ecologyLightCount_ = 0;  // ecology lights appended to scene_.lights last frame
    bool ecologyLightsEnabled_ = true;
    float ecologyLightGain_ = 0.0f;      // 0 disables; scenes opt in (ADR-053)
    float ecologyLightRange_ = 120.0f;   // metres from the camera a glowing patch still lights
    float ecologyGlowCell_ = 9.0f;       // metres per aggregation cell
    // Authored camera/environment settings (used when unattached and as parameter defaults).
    std::optional<float> cameraDistanceSetting_; // empty = fitted to the bounds
    std::optional<float> cameraHeightSetting_;
    float cameraOrbitSpeedSetting_ = 0.12f;
    float cameraFovSetting_ = 50.0f;
    float envIntensitySetting_ = 1.0f;
    // HDRI sky (ADR-049): the scene-file values behind env/rotation and the background controls.
    // `envRotationSetting_` in particular used to have nowhere to come from -- the parameter
    // existed but always started at 0, so a scene file could not aim its sky at all.
    float envRotationSetting_ = 0.0f;      // radians about +Y
    float skyIntensitySetting_ = 1.0f;     // the visible sky only
    float skyBloomSetting_ = 0.0f;         // how much of the sky the bloom mask sees
    bool showSkyboxSetting_ = true;        // draw the environment behind the world at all
    bool lightFromEnvironmentSetting_ = false;
    bool stylizedSetting_ = false;
    params::Parameter<bool>* stylized_ = nullptr;
    // The environment map's brightest direction, in world space at rotation 0, found once when the
    // map is loaded: an 8K scan is 33 M texels and has no business being swept every frame.
    std::optional<glm::vec3> envDominantDirection_;
    // Procedural sky (ADR-036): the scene-file values behind the env/sky/* parameters.
    scene::SkySettings skySetting_;
    Scene scene_;
    std::vector<std::unique_ptr<CompositionNode>> nodes_;
    bool dirty_ = true;
    glm::vec3 center_{0.0f};
    float radius_ = 1.0f;
    float cameraAngle_ = 0.0f;
    // Free camera (camera/mode = 1): explicit position/target parameters instead of the orbit.
    params::Parameter<int>* cameraMode_ = nullptr;
    params::Parameter<float>* fogDensity_ = nullptr;
    // Volumetric atmosphere (ADR-032): scene/volume* next to scene/fog*.
    params::Parameter<float>* volumeDensity_ = nullptr;
    nlohmann::json postJson_;
    params::Parameter<float>* fogHeight_ = nullptr;
    params::Parameter<float>* fogHeightFalloff_ = nullptr;
    // ADR-055: the two wind controls worth touching live. The rest of the field is authored.
    params::Parameter<float>* windSpeed_ = nullptr;
    params::Parameter<float>* windDirection_ = nullptr;
    params::Parameter<float>* volumeScattering_ = nullptr;
    params::Parameter<float>* volumeAbsorption_ = nullptr;
    params::Parameter<float>* volumeAnisotropy_ = nullptr;
    params::Parameter<float>* volumeNoise_ = nullptr;
    params::Parameter<float>* volumeNoiseScale_ = nullptr;
    params::Parameter<float>* volumeNoiseSpeed_ = nullptr;
    params::Parameter<float>* volumeEmission_ = nullptr;
    params::Parameter<glm::vec3>* styledSkyAmbient_ = nullptr;
    params::Parameter<glm::vec3>* styledGroundAmbient_ = nullptr;
    params::Parameter<int>* volumeSteps_ = nullptr;
    params::Parameter<float>* keyLight_ = nullptr;   // multiplier on the default key light
    bool addedKeyLight_ = false;
    std::uint64_t frameCounter_ = 0;
    params::Parameter<glm::vec3>* fogColor_ = nullptr;
    float fogDensitySetting_ = 0.0f;
    scene::Environment volumeSetting_; // the scene-file values behind the scene/volume* parameters
    wind::WindParams windSetting_;     // the scene-file values behind the scene/wind* parameters
    std::string volumeDensityFieldSetting_;
    std::string volumeColorFieldSetting_;
    glm::vec3 fogColorSetting_{0.012f, 0.012f, 0.02f};
    bool fogColorSet_ = false;
    params::Parameter<glm::vec3>* cameraPosition_ = nullptr;
    params::Parameter<glm::vec3>* cameraTarget_ = nullptr;
    int cameraModeSetting_ = 0;
    glm::vec3 cameraPositionSetting_{0.0f, 2.0f, 10.0f};
    glm::vec3 cameraTargetSetting_{0.0f, 1.0f, 0.0f};
    // Camera mode 2 (spline, ADR-026): rides the named scene spline at camera/splineT (0..1 of the
    // length), looks camera/lookAhead units further along, offset in the spline frame.
    std::string cameraSplineSetting_;
    params::Parameter<float>* cameraSplineT_ = nullptr;
    params::Parameter<float>* cameraLookAhead_ = nullptr;
    params::Parameter<glm::vec3>* cameraSplineOffset_ = nullptr;
    // Camera shake (ADR-098): a camera-space offset, in every camera mode. `start` is the second
    // the impulse began -- a parameter and not a timer, which is what keeps a decaying shake a pure
    // function of the playhead. See scene::CameraShake.
    params::Parameter<float>* cameraShakeAmplitude_ = nullptr;
    params::Parameter<float>* cameraShakeFrequency_ = nullptr;
    params::Parameter<float>* cameraShakeDecay_ = nullptr;
    params::Parameter<float>* cameraShakeRotation_ = nullptr;
    params::Parameter<float>* cameraShakeStart_ = nullptr;
    double currentTime_ = 0.0;
    void updateCharacters(const FrameTime& time); // ADR-086
    RigStats rigStats_;   // ADR-086: what the last update() spent posing skinned characters
    // Scene-level material programs (ADR-030): "materialPrograms" in the file, parameters
    // "material/<name>/…", referenced by Material::program.
    CompositionData compositionData_;
    [[nodiscard]] entity::Navigator buildNavigator() const;
    // The luminous patches of ecology, as places worth walking to (ADR-093, §6).
    [[nodiscard]] std::vector<entity::InterestPoint> glowInterestPoints() const;
    [[nodiscard]] std::uint32_t worldSeed() const;
    // Marks the entities of entity-driven nodes that fall outside the camera frustum. This only
    // suppresses drawing; rig evaluation remains timeline-driven so a character cannot freeze at
    // the frustum edge. ADR-086's cullDistance remains the animation distance policy. Deliberately
    // only for nodes an entity drives: every other node's visibility is somebody else's decision
    // and flipping it here would be a rendering change smuggled in as an optimisation.
    void cullEntityNodes();

    // Turns a behaviour's Activity into an animation state on the node it drives (ADR-086/087).
    // Owned by the composition because only the composition knows which node holds which rig.
    class AnimationSink final : public entity::IPoseSink {
    public:
        AnimationSink(Composition& owner, std::string node, const entity::Entity& entity)
            : owner_(owner), node_(std::move(node)), entity_(entity) {}
        void setLocomotion(const entity::LocomotionState& state) override;

    private:
        Composition& owner_;
        std::string node_;
        const entity::Entity& entity_;
    };
    std::vector<std::unique_ptr<AnimationSink>> animationSinks_;

    std::vector<world::HeroPoint> heroes_;   // ADR-074: authored, round-tripped as "heroes"
    std::vector<entity::EntityDesc> entityDescs_; // ADR-088: authored, round-tripped as "entities"
    std::vector<entity::FieldDesc> fieldDescs_;   // ADR-097: authored, round-tripped as "fields"
    std::string profileLibraryPath_;              // ADR-097: "entityProfiles", relative to the scene
    bool fieldRoutesChecked_ = false;             // has the "a route cannot drive a field" scan run
    entity::EntityWorld entityWorld_;
    // Every solid a walker has to go round, built once per rebuild from the scatter clouds and the
    // heroes (ADR-093, §5). Shared rather than owned outright: the navigator every entity reads is
    // a copy, and they all have to be looking at the same set. Null until the first rebuild.
    std::shared_ptr<spatial::ObstacleField> obstacles_;
    // The same set presented through §3's one-method interface (ADR-090), so a caller holding only
    // a `TerrainQuery` gets the per-instance answer too. A stable member rather than a temporary
    // because `terrainQuery()` hands out a pointer to it.
    entity::NavigationObstacles obstacleBridge_;
    // How coarse the navigation graph is, in metres. 0 disables pathfinding, which leaves the
    // straight-line steering that was here before ADR-093 -- correct, and unable to route.
    float navCellSize_ = 4.0f;
    std::optional<graph::Graph> graph_;
    bool graphDirty_ = false;
    double interactiveRebuildBudgetMs_ = 0.0;
    std::vector<std::string> graphNodes_;      // node names installed by the last evaluation
    std::vector<std::string> graphMaterials_;  // material program names installed by it
    std::vector<std::string> graphWarnings_;
    params::Modulator* graphModulator_ = nullptr;
    std::vector<spatial::GridField> grids_;
    std::vector<MaterialProgram> materialPrograms_;
    std::vector<MaterialProgramParameters> materialParams_;
    std::size_t ownMaterialCount_ = 0; // this composition's programs come first in scene_.materialPrograms

    // Flattened bookkeeping: per node, the entity index range in scene_ and the rest transforms.
    struct NodeRange {
        std::size_t firstEntity = 0;
        std::size_t entityCount = 0;
        std::vector<Transform> restTransforms;       // entity transforms inside the asset
        std::vector<float> restEmissive;
        std::vector<float> restRoughness;
        int particleIndex = -1;                      // index into scene_.particles (Particles kind)
        int proceduralIndex = -1;                    // index into scene_.procedurals (Procedural kind)
        std::size_t proceduralSubCount = 0;          // the asset's other materials, immediately after it
        int fieldIndex = -1;                         // index into scene_.fields.fields (Field kind)
        std::size_t firstField = 0;                  // Scene kind: the child's fields copied in
        std::size_t fieldCount = 0;
        int splineIndex = -1;                        // index into scene_.splines.splines (Spline kind)
        std::size_t firstSpline = 0;
        std::size_t splineCount = 0;
        int sdfIndex = -1;                           // index into scene_.sdfs (Sdf kind)
        std::size_t firstSdf = 0;
        std::size_t sdfCount = 0;
        // The lights an asset brought in with it (ADR-034 follow-up). Kept as a range with their
        // rest values so a node's `lightIntensity` and `lightColor` can be applied every frame --
        // `rebuild` repopulates `scene_.lights` wholesale, so a value written straight onto a light
        // was discarded at the next rebuild and lights were the one thing in a scene that could not
        // be animated.
        std::size_t firstLight = 0;
        std::size_t lightCount = 0;
        std::vector<float> restLightIntensity;
        std::vector<glm::vec3> restLightColor;
        std::size_t firstMaterial = 0;               // Scene kind: the child's material programs copied in
        std::size_t materialCount = 0;
        std::size_t firstProcedural = 0;             // Scene kind: the child's procedurals copied in
        std::size_t proceduralCount = 0;
        std::size_t firstParticle = 0;               // particle range (Particles and Scene kinds)
        std::size_t particleCount = 0;
        std::size_t firstRig = 0;                    // ADR-086: this node's rigs in scene_.rigs
        std::size_t rigCount = 0;
        std::uint64_t childMeshVersion = 0;          // Scene kind: what was flattened
        std::size_t childEntityCount = 0;
        std::size_t childParticleCount = 0;
    };
    std::vector<NodeRange> ranges_;
    std::uint32_t viewportWidth_ = 1440;   // see setViewport(); the defaults reproduce the
    std::uint32_t viewportHeight_ = 900;   // reference lens terrain LOD was authored against

    params::ParameterSet* params_ = nullptr;
    params::Modulator* modulator_ = nullptr;
    std::string prefix_;
    params::Parameter<float>* cameraDistance_ = nullptr;
    params::Parameter<float>* cameraHeight_ = nullptr;
    params::Parameter<float>* cameraOrbitSpeed_ = nullptr;
    params::Parameter<float>* cameraFov_ = nullptr;
    params::Parameter<float>* envIntensity_ = nullptr;
    params::Parameter<float>* envRotation_ = nullptr;
    // env/sky/* (ADR-036)
    params::Parameter<bool>* skyEnabled_ = nullptr;
    params::Parameter<bool>* skyBackground_ = nullptr;
    params::Parameter<glm::vec3>* skyZenith_ = nullptr;
    params::Parameter<glm::vec3>* skyHorizon_ = nullptr;
    params::Parameter<glm::vec3>* skyGround_ = nullptr;
    params::Parameter<glm::vec3>* skySunColor_ = nullptr;
    params::Parameter<float>* skyHaze_ = nullptr;
    params::Parameter<float>* skySunIntensity_ = nullptr;
    params::Parameter<float>* skySunSize_ = nullptr;
    params::Parameter<float>* skySunGlow_ = nullptr;
    params::Parameter<float>* skyIntensity_ = nullptr;
    params::Parameter<float>* brightness_ = nullptr;
    params::Parameter<float>* gridIntensity_ = nullptr;
    params::Parameter<float>* rootScale_ = nullptr;
    params::Parameter<float>* rootRotationSpeed_ = nullptr;
    params::Parameter<float>* rootImpulse_ = nullptr;
    float rootAngle_ = 0.0f;
};


// The interactive-rebuild policy, as a pure function: state in, decision out, no clock read inside.
// True means regenerate this object now. Defined in composition.cpp beside its only caller and
// pinned in tests/unit/test_composition.cpp, because the behaviour that matters -- a drag never
// reaching a regeneration, and a released slider always reaching one -- is a property of this
// arithmetic and not of any scene.
[[nodiscard]] bool advanceRebuildDeferral(Composition::ProceduralRebuildState& state, std::uint64_t wanted,
                                          std::uint64_t built, double elapsedMs);

} // namespace avgen::scene
