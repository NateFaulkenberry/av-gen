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
#include "scene/day_night.hpp"
#include "scene/field_params.hpp"
#include "scene/ground_query.hpp"
#include "entity/clip_motion_provider.hpp"
#include "entity/match_motion_provider.hpp"
#include "scene/motion_library.hpp"
#include "entity/motion_chain.hpp"
#include "scene/motion_context.hpp"
#include "scene/camera_rig.hpp"
#include "scene/light_rig.hpp"
#include "scene/material_params.hpp"
#include "scene/rebuild_deferral.hpp"
#include "stage/staging.hpp"
#include "scene/sdf_object.hpp"
#include "scene/spline_params.hpp"
#include "scene/floaters.hpp"
#include "scene/particles.hpp"
#include "scene/pose_layers.hpp"
#include "scene/root_motion.hpp"
#include "entity/entity.hpp"
#include "entity/obstacles.hpp"
#include "scene/scene_controller.hpp"
#include "world/city.hpp"
#include "world/ecology.hpp"
#include "world/effects/effect_instance.hpp"
#include "world/hero.hpp"
#include "world/terrain.hpp"
#include "world/terrain_query.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <chrono>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace avgen::params {
class Timeline;
}

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
// ADR-278: the keys `Composition::fromJson` reads, in each of the objects a scene file is made of.
// Public so a test can hold the list against every scene file that ships -- a key list that has
// drifted from the parser warns about a correct file, and the first spurious warning is the one
// that gets the whole check switched off.
[[nodiscard]] std::span<const std::string_view> sceneFileKeys();
[[nodiscard]] std::span<const std::string_view> sceneEnvironmentKeys();
[[nodiscard]] std::span<const std::string_view> sceneSkyKeys();
[[nodiscard]] std::span<const std::string_view> sceneLightKeys();

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
// The whole of a node's animation request, including its distance policy.
//
// All four distance knobs are authored here, which they were not: `nearDistance` and `farHz` were
// compiled into `SkinnedRig` and only `updateHz` and `cullDistance` were ever copied from the scene.
// That made the authored rate a number with almost no effect -- Glowmere asks for 30 Hz and got 20
// past fifteen metres, because `farHz` is a ceiling the author could not see, let alone raise.
//
// They are here rather than on the rig because a rig is an *asset's* skeleton and a distance policy
// is a *scene's* decision about one character in one world. Two nodes on the same character file
// must be able to have different policies, for the same reason ADR-086 gives for copying rigs per
// node instance rather than per asset.
struct NodeAnimation {
    std::string state;        // the state to enter ("" = leave the rig on its default)
    float blend = -1.0f;      // cross-fade seconds; < 0 = the state's own blendIn
    float speed = 1.0f;       // clip seconds per timeline second
    // ---- the distance ladder, all four rungs authorable ----------------------------------------
    float updateHz = 0.0f;       // pose rate ceiling; 0 = every frame when near the camera
    float nearDistance = 15.0f;  // nearer than this: posed every frame (subject to updateHz)
    float farHz = 20.0f;         // between nearDistance and cullDistance: this rate
    float cullDistance = 120.0f; // metres beyond which the rig is not posed at all (0 = never cull)
    // ---- the layer stack (ADR-300) -------------------------------------------------------------
    // What goes on top of `state`. Authored here rather than compiled in, because a joint mask is
    // per-asset: the three rig families this repository loads call the head `head.x`, `Head01` and
    // `Head`, and a hardcoded list would be a silent no-op on two of the three. A layer with a
    // `drive` of `look` or `reaction` is how `entity::LocomotionState`'s two published-and-ignored
    // fields reach a pose; without one authored on the node they go on reaching nothing, which is
    // the honest behaviour -- there is no joint name this engine may assume.
    std::vector<PoseLayer> layers;
    // ---- reachable contact solving (ADR-544) ---------------------------------------------------
    // Whether, and how far, this character's body may move when a foot layer is asked for ground
    // its leg cannot reach. Authored per node for the same reason the layers are: how much a body
    // may drop its hips is a fact about the character and the staging, not about the engine.
    // Disabled by default -- it moves a joint nothing else moves.
    BodyCompensationSpec bodyCompensation;
    // ---- contacts, phase and inertialization (ADR-546, ADR-547) ---------------------------------
    // The joints this character's clips are analysed for ground contact on, in the order that makes
    // the first one the phase reference. Empty means no analysis, and phase matching then falls
    // back to frame zero -- the behaviour every scene had before this existed.
    std::vector<std::string> contacts;
    // Enter every state at the outgoing state's phase rather than at its clip's frame zero.
    bool matchPhase = false;
    // Seconds for an inertialized transition's offset to halve. 0 keeps the cross-fade.
    float inertialize = 0.0f;
    // ---- root motion (ADR-337) -----------------------------------------------------------------
    // Which of this character's clips hand their root displacement to the simulation instead of
    // drawing it. Per clip and per node, because it is an art decision twice over: whether a clip
    // means to travel is a property of the take, and whether *this* body should be moved by it is
    // a property of the scene. Glowmere's aliens and the lab's `watcher` load the same 26 clips
    // and want different answers.
    //
    // Empty is the default and 163 of the 168 clips this project loads will keep it.
    std::vector<RootMotionSpec> rootMotion;
    [[nodiscard]] bool authored() const {
        return !state.empty() || blend >= 0.0f || speed != 1.0f || updateHz != 0.0f ||
               nearDistance != 15.0f || farHz != 20.0f || cullDistance != 120.0f ||
               !layers.empty() || !rootMotion.empty();
    }
};

// What a scene file says about runtime LOD on a Gltf node (ADR-351). Serialised as a `lod` block.
//
// The ladder lives here rather than on the asset because it is a property of how the node is *used*
// -- the same tree is a hero in one shot and scenery in another -- and because the chain is built
// per node instance at flatten time, cached by asset path and settings, so two nodes on the same
// asset asking for the same ladder build it once.
struct NodeLod {
    bool enabled = false;
    // Fractions of the source triangle count, descending, LOD0 first. Empty means
    // `assets::foliageLodSettings`'s five rungs, which is the calibration §3 of the asset-LOD brief
    // asks for. These are *targets*: every level reports what it actually achieved and the ones
    // that fall short say so.
    std::vector<float> ratios;
    // Remove whole disconnected shells on geometry the simplifier cannot touch. On by default,
    // because the assets that need runtime LOD most are the ones made of instanced foliage and a
    // chain that silently returns the source at every rung is not a chain. Turn it off to see the
    // simplifier's own answer.
    bool thinning = true;
    // A dead zone around the selector's thresholds, as a fraction of them (rendering/
    // representation.hpp). 0 is frame-independent and is the default everywhere in this engine.
    float hysteresis = 0.0f;
    // The quality floor, in pixels of projected deviation: a rung whose error projects to more than
    // this is refused however cheap it would be. Negative takes `RepresentationPolicy`'s calibrated
    // default, which is 8 px and is where the Tree of Life's rungs were measured to stop being
    // distinguishable from the source. Raise it to trade the hero shot's detail for its frame time;
    // this is the one number that decides that trade and it is authorable for that reason.
    float maxScreenError = -1.0f;
};

// ADR-360: a wind body. Authored on a GROUP node, and stamped onto every mesh the group contains,
// because the deformation is continuous in world space and two meshes that touch stay joined only
// if they are given the same origin and extent. Putting it on the group rather than on each mesh is
// what makes that impossible to get wrong from a scene file.
//
// The origin, height and radius are NOT authored: they are measured from the group's own combined
// world bounds at bake, so moving or rescaling the tree keeps the wind attached to it. What is
// authored is how much each tier moves.
struct WindBodySettings {
    float strength = 0.0f;  // overall amplitude; 0 is off, and off is the default
    float trunk = 0.25f;    // the low, near-axis part
    float branch = 1.0f;    // the mid, mid-radius part
    float foliage = 1.0f;   // the high, far-out part
    float flutter = 1.0f;   // the fast rattle at the tips
    float lag = 0.25f;      // seconds the body's whole-mass lean trails the field
};

// ADR-376: the tree's emissive life, authored on the same GROUP node as the wind body and stamped
// onto the same meshes, for the same reason: one body, one frame.
struct TreeEnergySettings {
    float intensity = 0.0f;
    float pulseSpeed = 0.18f, pulseWidth = 0.28f, propagation = 0.16f;
    float root = 1.0f, trunk = 1.0f, branch = 0.85f, canopy = 0.6f;
    float noiseAmount = 0.35f, noiseScale = 0.08f, noiseSpeed = 2.0f, bloom = 1.0f;
    float shimmer = 0.0f, shimmerSpeed = 0.05f, shimmerScale = 0.035f, shimmerVariation = 0.5f;
    glm::vec3 colorNear{0.20f, 0.85f, 0.55f};
    glm::vec3 colorFar{0.35f, 0.75f, 1.00f};
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
    // ADR-385. A whole-node opacity multiplier, for the same reason `emissiveBoost` and
    // `roughnessScale` are here: an effect wants to drive one object's material without the scene
    // restating the asset's own numbers. 1 is a genuine no-op and is the default, so a node nobody
    // fades is byte-identical to one built before this existed.
    //
    // It also does the thing neither of its neighbours has to. `pbr_shade.wgsl` reads
    // `let alpha = select(1.0, baseColor.a, alphaMode > 1.5)`: an OPAQUE material's alpha is
    // discarded outright, and every farm GLB in the repository is authored OPAQUE. So an opacity
    // under 1 also *promotes* the entity's `alphaMode` to `Blend` for the frames it is under 1, and
    // puts it back when it is not. Driving the number alone rendered a perfectly solid cow.
    float opacityScale = 1.0f;
    // ADR-360: the wind body this node's meshes belong to, when it is a Group that declares one.
    // `windAuthored` distinguishes "the author wrote nothing" from "the author wrote the defaults",
    // which is what keeps a scene written before this key existed byte-identical on a re-save.
    WindBodySettings wind;
    bool windAuthored = false;
    // ADR-376. Authored alongside the wind body; `energyAuthored` keeps "wrote nothing" apart from
    // "wrote the defaults", which is what keeps a re-save byte-identical.
    TreeEnergySettings energy;
    bool energyAuthored = false;
    params::Parameter<float>* energyIntensityParam = nullptr;
    params::Parameter<float>* energyPulseSpeedParam = nullptr;
    params::Parameter<float>* energyPulseWidthParam = nullptr;
    params::Parameter<float>* energyPropagationParam = nullptr;
    params::Parameter<float>* energyRootParam = nullptr;
    params::Parameter<float>* energyTrunkParam = nullptr;
    params::Parameter<float>* energyBranchParam = nullptr;
    params::Parameter<float>* energyCanopyParam = nullptr;
    params::Parameter<float>* energyNoiseParam = nullptr;
    params::Parameter<float>* energyBloomParam = nullptr;
    params::Parameter<float>* shimmerParam = nullptr;
    params::Parameter<float>* shimmerSpeedParam = nullptr;
    params::Parameter<float>* shimmerScaleParam = nullptr;
    params::Parameter<glm::vec3>* energyColorNearParam = nullptr;
    params::Parameter<glm::vec3>* energyColorFarParam = nullptr;
    // ADR-370: for a Particles node, the node whose canopy it sheds from. Empty means the authored
    // emitter box stands. `canopyFrom` is where the crown starts, as a fraction of the body's
    // height, so leaves do not fall out of the trunk.
    std::string canopySource;
    float canopyFrom = 0.45f;
    // ADR-380: this system's attractor is the cosmic vortex, taken from the environment at bake
    // rather than copied into the file. `vortexReach` scales the vortex's radius into the
    // attractor's radius of influence.
    bool vortexAttractor = false;
    float vortexReach = 3.0f;
    params::Parameter<float>* windStrengthParam = nullptr;
    params::Parameter<float>* windTrunkParam = nullptr;
    params::Parameter<float>* windBranchParam = nullptr;
    params::Parameter<float>* windFoliageParam = nullptr;
    params::Parameter<float>* windFlutterParam = nullptr;
    params::Parameter<float>* windLagParam = nullptr;
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
    // The node's authored `material` block. Named for the kind that has always read it; an Orb
    // reads it too, and every other kind takes its surface from somewhere the node cannot override
    // (see the warning in `fromJson`).
    Material terrainMaterial;      // settings for kind Terrain: shared by every chunk
    bool materialAuthored = false; // the scene wrote a `material` block, rather than this being the default
    world::Ecology ecology;        // settings for kind Terrain (ADR-048): what grows on it
    // Settings for kind City (ADR-100). The node carries the *description*, never the placements:
    // a scatter cloud is a runtime shared_ptr and is not serialised, exactly as a terrain's ecology
    // scatter is not, so a city is re-planned and re-placed on every rebuild from these few numbers.
    // That is what makes it survive a save and a reload.
    world::CitySettings city;
    std::filesystem::path cityLibrary; // the tiling manifest, as written; resolved via the registry
    std::size_t cityCells = 0;         // what the last rebuild planned, for the editor to show

    NodeAnimation animation;       // ADR-086; Gltf nodes whose asset carries a skin
    // ADR-351: runtime LOD for this node's imported meshes. Off unless the scene file asks, which
    // is the whole of the opt-in: a behaviour that changes under every existing scene is not a fix,
    // and nothing in this repository draws differently until a node writes `"lod": {...}`.
    NodeLod lod;

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
    params::Parameter<float>* opacityParam = nullptr;
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
    // Said once per node, not once per rebuild: a terrain flattens every frame and a warning on
    // every frame is a warning nobody reads.
    bool terrainGroundWarned = false;
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
    // ADR-350. ADR-099 chose six water properties as "the ones worth moving". The water-world
    // spec's §17 and §23 ask for a different nine, and none of them were reachable: a scene could
    // not change how clear its water was, what colour it went with depth, or how much sky it
    // reflected, without editing JSON. Added rather than replacing the six.
    params::Parameter<float>* waterClarityParam = nullptr;
    params::Parameter<float>* waterMaxOpacityParam = nullptr;
    params::Parameter<float>* waterFresnelParam = nullptr;
    params::Parameter<float>* waterReflectionParam = nullptr;
    params::Parameter<float>* waterRoughnessParam = nullptr;
    params::Parameter<float>* waterRefractionParam = nullptr;
    params::Parameter<float>* waterRippleScaleParam = nullptr;
    params::Parameter<float>* waterShallowDepthParam = nullptr;
    params::Parameter<glm::vec3>* waterShallowColorParam = nullptr;
    params::Parameter<glm::vec3>* waterDeepColorParam = nullptr;
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

// One shot of a directed sequence, remembered so its aim can follow the hero it was cut for
// (ADR-158).
//
// Directing is a bake and stays one: the cuts, the camera's path and its lens are keyframes, which
// is what makes a directed camera scrubbable, renderable offline and identical every time. What a
// bake cannot express is a subject that *moves* after the bake -- Glowmere's wanderer walks out of
// its own close-up -- and re-cutting for that would replace the whole film every time somebody's
// hero took a step.
//
// So only the aim follows, inside the shot the director already chose. `heroAtCut` is where the
// hero stood when the keys were written, so the offset is zero at the moment of the cut and grows
// only as far as the hero actually walks: a shot of something standing still is bit-identical to
// what it was before this existed.
// Where a followed node has been, so a chase camera can stand where its subject *was*.
//
// One per node any rig follows with a lag. Sampled once a frame after the parameters are applied,
// which is the only moment at which "where the subject is" is a settled fact -- the same moment
// `syncHeroesToNodes` reads, and for the same reason.
//
// A ring rather than a growing list: the longest lag any rig asks for, plus a margin, is all that
// can ever be read. A composition that plays for an hour holds a couple of hundred samples.
struct FollowTrail {
    struct Sample {
        double seconds = 0.0;
        glm::vec3 position{0.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    };
    std::string node;
    std::vector<Sample> samples; // ordered by time, oldest first
};

struct AimFollow {
    double startSeconds = 0.0;
    double endSeconds = 0.0;
    std::string hero;            // names a hero in `Composition::heroes()`
    glm::vec3 heroAtCut{0.0f};   // where that hero stood when the shot was cut

    friend bool operator==(const AimFollow&, const AimFollow&) = default;
};


// A 64-bit digest of a scene's texture table -- name, dimensions, format and every pixel.
//
// Exposed because it is the whole of the decision `Composition::rebuild` makes about
// `Scene::textureVersion`, and a decision that cannot be tested on its own is a decision tested by
// whatever happens to call it. tests/unit/test_composition.cpp changes one texel and requires the
// answer to move, which is the arm that fails if this ever hashes nothing (ADR-182).
[[nodiscard]] std::uint64_t textureTableDigest(const std::vector<TextureData>& textures);

class Composition final : public SceneController, public stage::IVisualPlacement {
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
    // The eight world-space corners of every mesh the node draws, un-boxed. What a "does it fit
    // inside a cylinder" question needs: the axis-aligned box of a rotated body is larger than the
    // body by up to its own diagonal, which on a 3.6x farm animal is a metre of beam.
    [[nodiscard]] std::vector<glm::vec3> nodeCorners(const std::string& name);

    // ---- what a node contributes to the picture (`stage::IVisualPlacement`) ----
    //
    // "Where is this node's contribution to the picture centred, and where is the node itself?"
    // Both in world, both read out of the *flattened* scene -- so both carry the parent chain, the
    // parameter finals, the entity offsets, and whatever the asset does inside its own node.
    //
    // The centre, per node kind, and each is the honest answer for that kind rather than a rule
    // bent to fit:
    //
    //   Particles   the emitter's world point. A tractor beam authored 2.05 m under a tilted saucer
    //               has an axis that is not the saucer's origin, and the axis is the thing anything
    //               lining up with the beam must line up with.
    //   meshes      the centre of the box those meshes occupy. A farm GLB is not centred on its own
    //               origin; that offset is an asset property, it rotates with the body, and it is
    //               what a viewer is looking at.
    //   else        the node's own origin. A procedural draws through a point cloud rather than
    //               through scene entities, and its origin is the best answer available without
    //               walking a cloud every frame -- which would cost more than the whole director
    //               and would be the wrong number anyway for a scatter layer.
    //
    // `const`, and deliberately **does not rebuild**: the director runs before the entity pass that
    // writes the finals the next flattening reads, so this is last frame's answer by construction.
    // Rebuilding here to hide that would be a second flattening per frame *and* would still be a
    // different frame's answer from the one the renderer used. False while the scene is dirty --
    // frame zero, and after a structural edit -- so the caller falls back rather than reading a
    // stale box as if it were current.
    [[nodiscard]] bool visualPlacement(std::string_view node,
                                       stage::VisualPlacement& out) const override;

    // ---- a scrub that replays the director (ADR-671) ------------------------------------------
    //
    // What `Engine::seekSeconds` calls. The director is reset (ADR-209) and then **replayed** step
    // for step with the entities -- staging first, the entity step, the node offsets written back
    // -- so the craft, the animals it lifts and every character that perceives them land where a
    // play from zero puts them (ADR-360). The owner's ruling of 2026-09-21 replaces ADR-209's "a
    // scenario picks up again on the next frame": a scrub into an abduction shows it mid-cycle.
    //
    // The director asks where nodes are *drawn* (`Anchor::Drawn`), which a play answers from the
    // previous frame's flattening. The replay does not flatten; it answers from `ReplayPlacement`,
    // the same arithmetic over the same parameter finals, captured after each replayed step -- one
    // step old, as the play's is.
    //
    // ADR-700: and it does it from the nearest simulation checkpoint rather than from a reset, so
    // it is exact at any time rather than inside ninety seconds. The composition's half of a
    // checkpoint is the director (whole), the bases it wrote, and `ReplayPlacement`.
    void seekWithDirector(double seconds, params::ParameterSet& params, entity::SeekBudget budget,
                          double step = 1.0 / 60.0);
    [[nodiscard]] const std::vector<std::unique_ptr<CompositionNode>>& nodes() const { return nodes_; }
    // ---- composition (ADR-038) ----
    // What the frame is about: focal points, depth layers and exclusion regions. Its fields are
    // appended to the scene's field set at every rebuild under reserved "composition.*" names.
    [[nodiscard]] const CompositionData& composition() const { return compositionData_; }
    void setComposition(CompositionData data) {
        compositionData_ = std::move(data);
        dirty_ = true;
    }

    // How coarse the navigation graph is, in metres (ADR-193). The default of 4 m is a judgement
    // about Glowmere-sized worlds; a tighter world wants a finer grid and a vast one cannot afford
    // it, and until now neither could say so -- the field existed with no setter and no scene key,
    // so its documented "0 disables pathfinding" escape hatch was unreachable.
    //
    // Marks the composition dirty, because the grid is baked during a rebuild and a cell size that
    // takes effect at some unrelated later flatten is worse than one that cannot be set.
    void setNavCellSize(float metres) {
        const float clamped = metres <= 0.0f ? 0.0f : std::clamp(metres, 0.5f, 64.0f);
        if (clamped == navCellSize_) {
            return;
        }
        navCellSize_ = clamped;
        dirty_ = true;
    }
    [[nodiscard]] float navCellSize() const { return navCellSize_; }

    // How wide the body the navigation graph is built for is, in metres (ADR-199).
    //
    // The grid already inflates every solid by a body radius when it decides which cells are
    // blocked -- it just used the *world's* default of 0.45 m, a person. Glowmere Valley 2's
    // inhabitants are six metres tall with a 2.4 m radius, so every path was planned through gaps
    // they do not fit in, and the per-frame penetration resolve then fought the walk: measured,
    // `sage` spent one unbroken stretch of 62 seconds playing a walk cycle and going nowhere, and
    // `rook` and `ember` were under a centimetre a frame for more than half of theirs.
    //
    // Scene-level rather than per-character, because there is one grid per world -- see ADR-195 and
    // ADR-196, which both record the same limitation. Set it to the widest body that has to route.
    void setNavBodyRadius(float metres) {
        const float clamped = std::clamp(metres, 0.0f, 40.0f);
        if (clamped == navBodyRadius_) {
            return;
        }
        navBodyRadius_ = clamped;
        dirty_ = true;
    }
    [[nodiscard]] float navBodyRadius() const { return navBodyRadius_; }

    // How deep a walker will wade, in metres. 0 -- the default -- is a walkable set that stops at
    // the waterline, which is what every scene written before this key existed was authored
    // against; see `world::WalkRules::wadeDepth` for why the default is not a plausible ankle
    // depth. Above 0, shallow water joins the walkable set, the navigation graph prices it, and
    // `explore` slows down in it.
    //
    // Scene-wide rather than per-character, and deliberately: the graph is baked once and every
    // walker shares it, so a character that waded deeper than the grid was built for would plan
    // against a walkable set it does not agree with.
    //
    // Marks the composition dirty for the same reason the cell size does -- the grid is baked
    // during a rebuild, and a wade band that takes effect at some unrelated later flatten is worse
    // than one that cannot be set.
    void setNavWadeDepth(float metres) {
        const float clamped = std::clamp(metres, 0.0f, 8.0f);
        if (clamped == navWadeDepth_) {
            return;
        }
        navWadeDepth_ = clamped;
        dirty_ = true;
    }
    [[nodiscard]] float navWadeDepth() const { return navWadeDepth_; }

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
    // The same heroes with their **authored** positions rather than their live ones, which is what
    // every serialiser wants and what `heroes()` cannot give it.
    //
    // `heroes()` returns positions that follow their node's final, so a hero riding a moving body
    // moves with it -- required by the director and by the editor, and wrong to write to a file.
    // Saving `heroes()` is what walked `ember` 68 m per save until 2026-09-22. Both the scene
    // serialiser and `Engine::projectDocument` go through here instead.
    [[nodiscard]] std::vector<world::HeroPoint> authoredHeroes() const;
    // Rejects the whole set rather than dropping the bad member, and names it. A hero silently
    // dropped is a camera director that frames nothing with no explanation of why.
    Result<void> setHeroes(std::vector<world::HeroPoint> heroes);

    // ---- effects (ADR-702) -----------------------------------------------------------------------
    //
    // The scene's ONE effect list: every effect instance on every owner -- the World, an entity, a
    // camera, a light -- round-tripped as `"effects"`. Before ADR-702 there were two lists here,
    // `worldEffects` (ADR-207's surface waves) and `atmosphericEffects` (ADR-230's sky and medium
    // effects), with two setters and two serialisers; "the World's effects" and "rook's effects" are
    // now two filtered views of this list (`world::effectsOf`), stored grouped by owner in stack
    // order (see `world/effects/effect_stack.hpp`).
    //
    // Nothing here *runs* them -- resolving a source to a world position needs the camera, the shot
    // schedule and the transport clock, all of which the Engine has and a Composition does not. And
    // nothing here registers their parameters: the Engine does, into a container it releases
    // wholesale, so there are no effect pointers in this class to go stale.
    [[nodiscard]] const std::vector<world::EffectInstance>& effects() const { return effects_; }
    // Rejects the whole set on a duplicate id, an unsupported owner, a broken stack order or an
    // invalid effect, and names it -- an effect silently dropped is one that never fires with
    // nothing saying why.
    Result<void> setEffects(std::vector<world::EffectInstance> effects);

    // ---- authored lights (ADR-278) ---------------------------------------------------------------
    //
    // A light a scene file wrote down, round-tripped as a top-level `"lights"` array. Before this
    // there were four ways a light could come into existence -- a rig, a glTF asset's own
    // KHR_lights_punctual, the procedural ecology, and the one default key -- and none of them was
    // a scene file, so a scene could describe every other thing in the world and not the light
    // on it.
    //
    // **Precedence**, written down here because four sources with no stated order is how the defect
    // went unnoticed. `rebuild` adds these after a node's asset lights and before the default key,
    // and `defaultKeyLight()` is added only when there is no rig and no light at all -- so
    // authoring one light replaces the default and authoring none keeps it. A rig is *not*
    // replaced: rig lights are appended every frame alongside these, the same way a glTF lamp and
    // a rig already coexist, because a rig lights the subject and an authored light lights the
    // world, and a scene that wants only its own simply has no rig.
    //
    // `node` is the answer to the question a `NodeKind::Light` would have answered. An authored
    // light naming a node is expressed in that node's local frame and rides its world transform
    // every frame -- so a lamp on a moving vehicle moves, and hiding the node puts the light out --
    // without a thirteenth case in a `NodeKind` switch that `nodeKindName`, the editor, the brush,
    // the context menu and the graph would each have to decide what "a node that is not geometry"
    // means for. An empty `node` is a world light, positioned where the file says.
    // `id` is the light's identity; `light.name` is only what it is called.
    //
    // They were one string, and rename is what separated them. A parameter path, a modulation
    // route's target and a timeline track's target are all `lights/<...>/<field>`, so while that
    // `<...>` was the display name, renaming a light silently orphaned every route and track bound
    // to it -- the "Hero Mushroom Pulse" defect, 511 occurrences across 22 files, one format over.
    // The two answers were to re-point every binding on every rename, or to stop keying on a
    // mutable string. This is the second: with an id there is nothing to re-point, because nothing
    // moved.
    //
    // **An id is derived from the sanitised name when a scene file does not give one.** That is
    // what keeps the 81 shipped projects keyed on `lights/celestial-key/intensity` resolving -- the
    // id happens to equal the old name, so every existing binding still finds its light, and the
    // first rename moves the name while the id stays put. `toJson` writes `id` only when it differs
    // from `sanitise(name)`, so a scene nobody has renamed stays byte-identical and the key appears
    // exactly when it has become load-bearing.
    struct AuthoredLight {
        PunctualLight light;  // world space, or the node's local space when `node` is set
        std::string node;     // the composition node this light rides, or empty
        std::string id;       // stable parameter-path identity; empty means "derive from the name"
    };
    // The id a light answers to: its own, or the one derived from its name. A free function because
    // the loader, the editor and the tests all have to agree on the derivation, and because the
    // rule must be checkable without a composition.
    [[nodiscard]] static std::string authoredLightId(const AuthoredLight& light);
    // An id not already in `taken`, derived from `name` and suffixed when it collides. What the
    // editor mints for a new or a duplicated light.
    [[nodiscard]] static std::string uniqueAuthoredLightId(std::string_view name,
                                                           std::span<const std::string> taken);
    [[nodiscard]] const std::vector<AuthoredLight>& authoredLights() const { return authoredLights_; }
    // The authored lights as *structure*: what `setAuthoredLights` was given, untouched by the
    // per-frame parameter writeback.
    //
    // `authoredLights()` is deliberately not that. `applyParameters` writes each light's live base
    // values back into it so that "Save Scene As..." records what the user set rather than what the
    // file said (ADR-225) -- which is right for a scene and wrong for the project's record, because
    // a project already answers every one of those numbers in its `parameters` block (ADR-271).
    // Comparing the parameter-baked list against the scene made an untouched project write a whole
    // copy of its lighting: measured on `tree-of-life-floating-island.json`, three lights differing
    // only in the fields that project already overrides. So the project owes the **set** -- which
    // lights exist, of what type, riding which node -- exactly as ADR-330 framed it for nodes.
    [[nodiscard]] const std::vector<AuthoredLight>& authoredLightsRest() const { return authoredLightsRest_; }
    // `authoredLightsRest()` in the serialisation a scene file would have given it, for
    // `authoredLightsAgainst`. A method rather than a free function because the converter it needs
    // is file-local.
    [[nodiscard]] nlohmann::json authoredLightsRestJson() const;

    // The live knobs of one authored light (ADR-358). Registered under
    // "lights/<name>/", which is the path `GltfScene` already gave an *imported* light's
    // intensity, so the two lighting sources answer to the same prefix.
    //
    // Azimuth and elevation describe **where the light comes from**, in world space, which is the
    // way a person describes a sun: elevation above the horizon, azimuth clockwise from +Z through
    // +X. Deliberately NOT the camera-relative frame `LightRig` uses -- an authored light is a
    // property of the world, and a scene whose sun swings round as the camera orbits is the exact
    // failure the brief's §15 names. Only an aimed light (directional or spot) gets them.
    //
    // `angularSize` is the apparent DIAMETER of the source in degrees and it drives
    // `PunctualLight::softness`, one for one. That is a calibration and not a physical solid
    // angle: the renderer's penumbra is a PCSS filter width in shadow-map texels, not a cone, and
    // shaders/shadows.wgsl clamps its blocker search at `softness * 6` texels capped at 24 -- so
    // the picture stops changing somewhere above four degrees. Read it as "the sun is about a half
    // and a soft celestial source is two to four", and see the ADR for what it is not.
    struct AuthoredLightParams {
        params::Parameter<bool>* enabled = nullptr;
        params::Parameter<float>* intensity = nullptr;
        params::Parameter<glm::vec3>* color = nullptr;
        params::Parameter<float>* azimuth = nullptr;   // degrees, world, source side; aimed lights only
        params::Parameter<float>* elevation = nullptr; // degrees above the horizon; aimed lights only
        params::Parameter<float>* angularSize = nullptr;
        params::Parameter<float>* shadowStrength = nullptr;
        // The rest of what a person manipulates. `position` is the one that unblocked the editor:
        // every transform here is a parameter write, and that is the whole of what makes a gizmo
        // drag undoable, keyable and modulatable -- so until this existed no gizmo could move a
        // light at all, whatever the viewport drew.
        //
        // Absolute, seeded from the scene, never multipliers over it. ADR-271 is the cautionary
        // tale: a control displaying metres while writing a ratio put `0.0538` in a project file
        // for a beam somebody had set to 0.42 m, and no one reading that file could tell.
        params::Parameter<glm::vec3>* position = nullptr;   // metres, world or the node's local frame
        params::Parameter<float>* range = nullptr;          // metres; 0 = infinite
        params::Parameter<float>* innerCone = nullptr;      // DEGREES, like the file; spot only
        params::Parameter<float>* outerCone = nullptr;      // degrees; spot only
        params::Parameter<float>* temperature = nullptr;    // Kelvin
        params::Parameter<float>* tint = nullptr;
        params::Parameter<float>* width = nullptr;          // area emitters
        params::Parameter<float>* height = nullptr;
        params::Parameter<float>* radius = nullptr;
        params::Parameter<bool>* castsShadow = nullptr;
        params::Parameter<bool>* contactShadow = nullptr;
        params::Parameter<float>* shadowBias = nullptr;
        params::Parameter<float>* volumetric = nullptr;
    };
    // Where a light's source sits, as the two angles above, given the direction it travels.
    // Free functions rather than methods because the inverse pair has to be checkable without a
    // composition, and because the editor and the tests both want them.
    static void lightAngles(const glm::vec3& travelDirection, float& azimuthDegrees, float& elevationDegrees);
    [[nodiscard]] static glm::vec3 lightDirectionFromAngles(float azimuthDegrees, float elevationDegrees);
    // Rejects the whole set on a duplicate or empty name, and names it, for the reason
    // `setEffects` does: a name is half of an identity, and a light nobody can name is a light
    // nobody can find in a frame that has thirty of them.
    Result<void> setAuthoredLights(std::vector<AuthoredLight> lights);

    // Bumped by every accepted `setHeroes`. A counter rather than a comparison of the lists,
    // because what reads it is asking "is the shot I cut still the shot these heroes describe" --
    // a question about *when*, not about which fields differ -- and because comparing two vectors
    // of heroes every frame to answer "no" is work nobody needs done.
    [[nodiscard]] std::uint64_t heroRevision() const { return heroRevision_; }
    // Bumped when a hero's *geometry* settles somewhere new -- it followed its object, or somebody
    // moved its aim or its stand-off -- as opposed to the set of heroes changing.
    //
    // Two counters because the two want different answers from the camera director. Changing the
    // cast is a decision and re-cuts the film at once, wherever the playhead is. A hero moving is
    // usually the *world* moving: Glowmere's wanderer walks, so following it bumped this several
    // times a minute, and each bump re-cut the whole film under a running playhead -- which is how
    // the director ended up apparently stuck on one hero during playback.
    [[nodiscard]] std::uint64_t heroPlacementRevision() const { return heroPlacementRevision_; }
    // Replaces one hero, validated on its own. For editing what a hero *is* -- its importance, where
    // on the object the camera looks, how far it stands off -- without the whole-set semantics of
    // `setHeroes`: the change lands immediately and the revision waits for the settle, so dragging a
    // slider does not re-cut the film sixty times a second.
    Result<void> editHero(const std::string& name, const world::HeroPoint& value);
    // How long a hero's object has to stop moving before the declaration is considered settled and
    // the revision moves. Exposed so a test does not have to sleep.
    void setHeroSettleSeconds(double seconds) { heroSettleSeconds_ = std::max(0.0, seconds); }

    // The shots a directed sequence baked, so the camera's aim can follow their heroes (ADR-158).
    //
    // Set by `app::installSequence` and cleared when the camera is handed back. Entries whose hero
    // is not in `heroes()`, or whose shot is not the one the playhead is in, do nothing -- so a
    // stale table is inert rather than wrong, which matters because the table outlives the heroes
    // it names whenever somebody unstars one.
    //
    // Round-trips with the project, next to the timeline tracks it accompanies, because an offline
    // render reloads the project before drawing it: a table that only lived in memory would make a
    // rendered file differ from the window that asked for it, which is the one thing a deterministic
    // engine may not do.
    void setAimFollow(std::vector<AimFollow> shots);
    [[nodiscard]] const std::vector<AimFollow>& aimFollow() const { return aimFollow_; }

    // Drops the aim-follow smoother's running state. What a seek needs, so that the seeked second
    // is a function of the second rather than of how the playhead got there. (This is all that is
    // left of `clearAimHoldState`: the hold it also cleared was retired with ADR-217.)
    void clearAimFollowState() {
        aimFollowSmoothed_ = glm::vec3(0.0f);
        aimFollowPrimed_ = false;
    }

    // How hard the aim-follow delta is filtered, in milliseconds. ADR-158 adds the hero's movement
    // since the cut straight onto the camera target, which is right for travel and wrong for
    // anything that oscillates: a hovering saucer with a sine on its height hands the camera that
    // sine, one frame at a time, and the shot rocks with it.
    //
    // A low pass separates the two by *rate* rather than by amount, which is the only thing that
    // distinguishes them: a body crossing two hundred metres moves far and slowly, a hover bob
    // moves a little and quickly. Travel passes through with a small constant lag; the bob is
    // attenuated by roughly the ratio of its period to this constant.
    //
    // 0 restores the unfiltered behaviour exactly, so a project that does not ask is unchanged.
    void setAimFollowSmoothingMs(float ms) { aimFollowSmoothingMs_ = ms; }
    [[nodiscard]] float aimFollowSmoothingMs() const { return aimFollowSmoothingMs_; }
    // ---- multiple cameras (ADR-245) ------------------------------------------------------------
    //
    // The camera collection, the authored shot track and the resolved active camera. See
    // `scene/camera_rig.hpp` for what each of those three is and why they are three things.
    //
    // A composition that has never been touched holds exactly one camera -- the main one, which is
    // the `camera/*` block this file has always had -- and no shots, so `resolveActiveCamera`
    // answers "the main camera" at every instant and the frame is evaluated by the code that was
    // here before. That is the backward-compatibility story in one sentence.
    [[nodiscard]] const CameraDirection& cameraDirection() const { return cameraDirection_; }
    // Replaces the collection. Refuses an invalid one whole (a shot naming a camera that is not
    // there, a duplicate slug) and leaves the old one in place, then re-registers the per-camera
    // parameters. Call `Engine::refreshCameraParameters` rather than this from the application, so
    // the timeline re-binds onto the channels that now exist.
    [[nodiscard]] Result<void> setCameraDirection(CameraDirection direction);
    // Which camera is on screen this frame and why. **The one published answer**: nothing else in
    // the engine decides this, and a consumer that wants the pose reads `Scene::camera` as it
    // always has. Updated once per frame inside `applyParameters`.
    [[nodiscard]] const ActiveCameraState& activeCamera() const { return activeCamera_; }
    // ---- what the viewport is looking through (ADR-391) -----------------------------------------
    //
    // **Navigating the view is no longer the same act as modifying the film's camera.**
    //
    // The viewport used to *be* `camera/*`: a drag wrote `camera/position` and `camera/target`, so
    // flying around to look at something was an edit to the deliverable. On a directed project that
    // meant a drag could only be honoured by discarding the director's bake (ADR-386's lock), and
    // "look through that camera" could not be built at all, because there was no pose in this
    // engine that the film did not own.
    //
    // There is one now. The viewport has a mode, and the mode says where the frame comes from:
    //
    //   Film    -- the director's answer, `camera/*`, the active rig. Exactly as before, and **the
    //              only value any render path ever sees**: this is default-constructed state that
    //              nothing but the editor writes, and it is not serialised, so an offline render,
    //              a RenderJob and a sequence render cannot inherit an editor's navigation.
    //   Editor  -- a pose the editor owns (`editorCamera()`), which drags, the wheel, frame-selected
    //              and "go to camera" write. Never reaches `camera/*` and never reaches a file.
    //   Through -- pinned to one authored rig whatever the director is doing.
    //
    // **Applied to the frame, never inside the resolver.** This is the same rule free-roam obeyed
    // and the reason it is stated twice: `resolveActiveCamera` stays a pure function of (cameras,
    // shots, events, time), `activeCamera()` keeps answering with the camera that owns the *film*,
    // and all that changes is the pose written into `scene_.camera` for display. What free-roam got
    // wrong was only its destination -- it redirected to the MAIN camera, and the main camera *is*
    // `camera/*`, so navigating still wrote the film. It is superseded by `Editor` rather than kept
    // beside it: two overlapping concepts for "the viewport is not showing the film" is how the next
    // person gets this wrong.
    //
    // One consequence worth naming: because the editor pose is not `camera/*`, it does not care
    // about `camera/mode`. A drag in orbit mode used to write parameters the main camera does not
    // read and look exactly like a dead input; the editor viewpoint moves.
    void setViewportView(ViewportView view);
    [[nodiscard]] const ViewportView& viewportView() const { return viewportView_; }
    // Where the editor's own viewpoint is. Read by the editor to move it, and by nothing else.
    //
    // Seeded from the film the first frame `Editor` is in effect and never before, so switching to
    // the editor viewpoint puts you exactly where you were looking rather than teleporting you --
    // and so a composition that has never had an editor viewpoint has no pose to disagree about.
    [[nodiscard]] const CameraPose& editorCamera() const { return editorCamera_; }
    [[nodiscard]] bool editorCameraSeeded() const { return editorCameraSeeded_; }
    void setEditorCamera(const CameraPose& pose) {
        editorCamera_ = pose;
        editorCameraSeeded_ = true;
    }
    // Whether the timeline drives any of a camera's own channels -- which is the whole of the
    // difference between a "static" camera and an "animated" one. There is no mode for it because
    // there is no state for it: a camera is animated exactly when somebody keyed it.
    [[nodiscard]] bool cameraIsAnimated(CameraId id, const params::Timeline& timeline) const;
    // Drops the event observation the director accumulated. Called on a seek, next to
    // `clearAimFollowState`, and for the same reason: a scenario's run is live state, so the seeked
    // second must not inherit an event span observed before the jump.
    void clearCameraEventState() { cameraEvents_.clear(); }
    // The event spans the director can currently see. Exposed for tests and an overlay.
    [[nodiscard]] std::span<const CameraEventSpan> cameraEventSpans() const { return cameraEvents_; }

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

    // ---- environmental contact queries (ADR-551, Phase B §17) -----------------------------------
    // What a procedural layer asks about the world, behind an abstraction that hides where the
    // answer came from. Defaults to this composition's own terrain; a caller may install another
    // source -- a collision system, a baked field, a test's plane -- without the layers knowing.
    [[nodiscard]] const IGroundQuery& groundQuery() const;
    void setGroundQuery(const IGroundQuery* query) { groundQuery_ = query; }

    // ---- entities (ADR-088) ------------------------------------------------------------------
    //
    // The `entities` array of a scene file: what in this scene moves on its own and how it answers
    // the music. A peer of `heroes` for the same reason heroes are a peer of the composition data
    // -- the nodes are already placed, and this says what drives them.
    //
    // Setting entities does not mark the composition dirty: an entity moves a node by writing its
    // transform parameters, and nothing it can do requires geometry to be rebuilt.
    [[nodiscard]] const std::vector<entity::EntityDesc>& entities() const { return entityDescs_; }
    // Phase D §26: how far each named world event carries. Takes effect at the next rebuild.
    void setEventProfiles(std::vector<entity::EntityWorld::EventProfile> profiles) {
        eventProfiles_ = std::move(profiles);
        entityWorld_.setEventProfiles(eventProfiles_);
    }
    [[nodiscard]] const std::vector<entity::EntityWorld::EventProfile>& eventProfiles() const {
        return eventProfiles_;
    }
    [[nodiscard]] const entity::EntityWorld& entityWorld() const { return entityWorld_; }
    [[nodiscard]] entity::EntityWorld& entityWorld() { return entityWorld_; }
    // Phase C §4/§67: how many motion databases the scene holds. One per (skeleton, feature config),
    // however many bodies match on it; a crowd that built one each would say so here.
    [[nodiscard]] std::size_t motionDatabaseCount() const { return matchAssets_.size(); }
    // Rejects the whole set and names the offender rather than dropping one, for the same reason
    // setHeroes does: an entity silently missing is a scene that does nothing with no explanation.
    Result<void> setEntities(std::vector<entity::EntityDesc> entities);

    // ---- the director (ADR-209) ---------------------------------------------------------------
    //
    // The `staging` object of a scene file: the actors a shot moves as one thing, and the scenarios
    // that decide what they do. A peer of `entities` for the same reason entities are a peer of
    // heroes -- the entities already exist and already know how to act; this says who decides.
    //
    // It is ticked from `updateBehaviour`, immediately before the entity pass, so an override the
    // director issues this frame is executed this frame.
    [[nodiscard]] const stage::StagingDesc& staging() const { return stagingDesc_; }
    [[nodiscard]] const stage::Staging& director() const { return staging_; }
    [[nodiscard]] stage::Staging& director() { return staging_; }
    Result<void> setStaging(stage::StagingDesc staging);

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
    // How many times this composition has re-flattened -- the all-or-nothing rebuild that
    // `dirty_` gates. Counted so an interaction can be judged by what it *caused* rather than by
    // how long the machine happened to take: "one flatten per click" and "one per frame of the
    // drag" are different defects and a millisecond figure on a contended machine tells them apart
    // badly.
    [[nodiscard]] std::uint64_t flattenCount() const { return flattens_; }
    // Restores the pre-fix behaviour: generate inside the parameter pass *as well as* in
    // rebuildProcedurals(). It exists for the same reason AVGEN_SCATTER_WORKERS does -- so the
    // comparison that justified removing it stays runnable rather than becoming a claim about two
    // binaries that no longer both exist -- and so `--ui-ab` can interleave the two arms inside one
    // process, which is the only kind of frame-time comparison this project accepts. Never set by
    // anything but that A/B: offline and ordinary live editing take the fixed path.
    void setLegacyProceduralGeneration(bool on) { legacyProceduralGeneration_ = on; }
    [[nodiscard]] bool legacyProceduralGeneration() const { return legacyProceduralGeneration_; }

    // Per-object regeneration cost and deferral state, indexed into the flattened procedural list.
    // Cleared by rebuild(), which is also what rebuilds that list, so the indices cannot go stale.
    // The policy that reads it is a pure free function in scene/rebuild_deferral.hpp, shared now
    // with the renderer's environment rebuild; this name is kept because a great deal of code and
    // several tests already say it.
    using ProceduralRebuildState = RebuildDeferral;
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
    // The same load with a project's node edits (ADR-330) spliced into the document first. The
    // edits reach the root scene only: a nested scene file is another document with its own
    // authorship, and a project that could reach into one would be editing a file it never opened.
    static Result<std::unique_ptr<Composition>> loadFile(const std::filesystem::path& path,
                                                         assets::AssetRegistry& registry,
                                                         const nlohmann::json& nodeEdits, int depth = 0);
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
    // Changes only how the rig's path is WRITTEN, not which rig is loaded. `setLightRig`
    // reloads the file and re-registers the rig's parameters, so using it to move a path --
    // which is what saving into another folder needs -- would tear down and rebuild every
    // `lightrig/...` parameter and lose whatever the author had tuned them to. This exists
    // because the light rig was the one asset in the environment block that `saveComposition`
    // did not rebase: a scene saved to another folder kept a path relative to the folder it
    // came from, and the rig was missing the next time it opened.
    void rebaseLightRigPath(const std::filesystem::path& path) {
        lightRigPath_ = path;
        dirty_ = true;
    }
    [[nodiscard]] const LightRig* lightRig() const { return lightRig_ ? &*lightRig_ : nullptr; }
    [[nodiscard]] const std::filesystem::path& sourcePath() const { return sourcePath_; }

    static void addDefaultRoutes(params::Modulator& modulator);

private:
    void rebuild();          // flattens nodes into scene_ (meshes/textures/entities/particles)
    void ensureBuilt();      // rebuild() when dirty
    void applyParameters();
    void applyDayNight();
    void resolveEnvironmentMap();  // ADR-346; runs alone, without a rebuild
    // Nudges the camera's aim onto the hero the active directed shot was cut for (ADR-158).
    // After `syncHeroesToNodes`, not inside `applyParameters`, so it reads where the hero is
    // *this* frame rather than where it was last one.
    void applyDirectedAim();
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
    void registerAuthoredLightParameters(params::ParameterSet& params);
    void unregisterAuthoredLightParameters();
    // ADR-370: the world bounds of a node's whole subtree, from the baked meshes. Shared by the
    // wind body and the canopy emitter so the two cannot disagree about where the tree is.
    [[nodiscard]] bool subtreeWorldBounds(std::size_t node, glm::vec3& lo, glm::vec3& hi,
                                          std::vector<std::size_t>* members) const;
    // ADR-370: point a particle node's emitter box at the measured canopy of another node.
    void applyCanopyEmitters();
    // ADR-360: stamp each declared wind body's measured origin/extent onto every mesh it holds.
    void applyWindBodies();
    // ...and the entity spans it stamped, so the per-frame parameter pass can move `strength` and
    // the three influences without re-measuring anything. The bounds are geometry and only change
    // when the scene is rebuilt; the amounts are artist controls and change every frame they are
    // dragged. Keeping them apart is what keeps a live slider off the bounds path -- the mistake
    // ADR-355 cost 350 ms a frame for.
    struct WindBodySpan {
        std::size_t node = 0;
        std::vector<std::pair<std::size_t, std::size_t>> entities; // [first, count)
    };
    std::vector<WindBodySpan> windBodies_;
    void refreshWindBodyAmounts();
public:
    bool setNodeWindBody(const std::string& name, bool present);

    // ADR-421: the deformer stack of a procedural node, replaced wholesale.
    //
    // The stack's SHAPE -- how many slots, what kind each is, which space it acts in -- is
    // structural: `registerProceduralParameters` writes one set of `deform/<n>/*` paths per slot and
    // labels them by kind, and `applyProceduralParameters` takes `kind` and `space` from the rest
    // copy on every frame. So changing any of those means unregister, mutate, re-register, exactly
    // as `setNodeWindBody` above does for a wind body, and for the same reason: the parameters ARE
    // the stack's interface and they have to be rebuilt to describe it.
    //
    // It does NOT rebuild geometry, and that is a property worth stating because it is what makes
    // an editor for this affordable. `ProceduralGeometry::structuralHash` does not include the
    // deformers -- deliberately, since a deformer is a vertex-stage transform of geometry that has
    // already been generated -- so adding a Twist to one of Glowmere Valley 2's forty-two
    // procedurals costs a re-registration and a flatten, not a regeneration.
    //
    // Both `procedural` and `proceduralRest` are written: the first is what `toJson` saves, the
    // second is what registration and the per-frame apply read. Writing one and not the other is
    // the reader-without-a-writer this repository keeps finding.
    Result<void> setNodeDeformers(const std::string& name, std::vector<Deformer> stack);
private:
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
    // The same, from the parameter **bases**: where the file puts the node rather than where this
    // instant's modulation has it. What an entity's anchor must be -- see the note on the
    // definition, and ADR-264.
    [[nodiscard]] Transform nodeBaseTransform(const CompositionNode& node) const;
    [[nodiscard]] Transform nodeWorldBaseTransform(const CompositionNode& node) const;
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
    // `nodeEdits` is null for every nested load and for every plain `loadFile`; see the public
    // overload above for why it stops at the root.
    static Result<std::unique_ptr<Composition>> loadNested(const std::filesystem::path& path,
                                                           assets::AssetRegistry& registry, int depth,
                                                           std::vector<std::filesystem::path> ancestors,
                                                           const nlohmann::json& nodeEdits = nlohmann::json());

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
    bool proceduralSkyBackgroundSetting_ = false; // ADR-345; analytic sky behind an HDRI
    // ADR-346: an environment-map change resolves on its own rather than re-flattening the world.
    bool environmentDirty_ = false;
    struct EnvironmentTexture {
        std::string path;
        TextureId texture = kInvalidTexture;
        glm::vec3 dominant{0.0f, 1.0f, 0.0f};
    };
    // Cleared by `rebuild()`, which clears the scene and with it every id in here. Without the
    // cache, `addTexture` would append on every swap and a long-running cycle would leak one
    // texture per crossing.
    std::vector<EnvironmentTexture> environmentTextureCache_;
    bool lightFromEnvironmentSetting_ = false;
    bool stylizedSetting_ = false;
    params::Parameter<bool>* stylized_ = nullptr;
    // The environment map's brightest direction, in world space at rotation 0, found once when the
    // map is loaded: an 8K scan is 33 M texels and has no business being swept every frame.
    std::optional<glm::vec3> envDominantDirection_;
    // Procedural sky (ADR-036): the scene-file values behind the env/sky/* parameters.
    scene::SkySettings skySetting_;
    // ADR-343: the day/night cycle. Inert unless the scene enables it; when it is on it
    // becomes the authority for the fields it owns and writes them after every other
    // parameter, so "one environment changing state" is true by ordering rather than by care.
    scene::DayNightSettings dayNight_;
    scene::DayNightState dayNightState_;  // this frame's resolved environment
    // ADR-350. Every one of these was a struct field the application read and never kept: no
    // parameter, no UI, no modulation, no keyframing -- and no writer either, so a scene that
    // loaded a `dayNight` block and was saved lost it. ADR-225 twice over.
    struct DayNightParams {
        params::Parameter<bool>* enabled = nullptr;
        params::Parameter<bool>* paused = nullptr;
        params::Parameter<float>* dayPhase = nullptr;      // the scrub target
        params::Parameter<float>* cycleSeconds = nullptr;
        params::Parameter<float>* phaseOffset = nullptr;
        params::Parameter<float>* sunPeakElevation = nullptr;
        params::Parameter<float>* sunAzimuthAtDawn = nullptr;
        params::Parameter<float>* sunAzimuthSweep = nullptr;
        params::Parameter<float>* sunIntensityScale = nullptr;
        params::Parameter<float>* moonIntensityScale = nullptr;
        params::Parameter<float>* starBrightnessScale = nullptr;
        params::Parameter<float>* hdriIntensityScale = nullptr;
        params::Parameter<float>* glowInfluence = nullptr;
        params::Parameter<float>* fogHorizonBlend = nullptr;
        // No colour parameters. The owner asked for the curves to stay scene-authored, and a wall
        // of stops is a control surface nobody reaches for while looking at the scene. The sun
        // angles below are in DEGREES for the same reason: a raw radian sweep is an inscrutable
        // knob, and a slider from 0 to 90 is not.
    };
    DayNightParams dayNightParams_;
    params::Parameter<bool>* showSkybox_ = nullptr;                 // ADR-350
    params::Parameter<bool>* proceduralSkyBackground_ = nullptr;    // ADR-350
    params::Parameter<bool>* lightFromEnvironment_ = nullptr;       // ADR-350
    params::Parameter<float>* skyBloom_ = nullptr;                  // ADR-350
    bool dayNightNightMap_ = false;       // which map is currently bound
    Scene scene_;
    std::vector<std::unique_ptr<CompositionNode>> nodes_;
    bool dirty_ = true;
    std::uint64_t flattens_ = 0;
    // The texture table as the last rebuild left it, as a digest. What makes a flatten's bump of
    // `Scene::textureVersion` conditional on a texture having actually changed rather than on a
    // flatten having happened (ADR-273).
    std::uint64_t textureDigest_ = 0;
    bool legacyProceduralGeneration_ = false;
    glm::vec3 center_{0.0f};
    float radius_ = 1.0f;
    float cameraAngle_ = 0.0f;
    // Free camera (camera/mode = 1): explicit position/target parameters instead of the orbit.
    params::Parameter<int>* cameraMode_ = nullptr;
    // Volumetric atmosphere (ADR-032): scene/volume* next to scene/fog*.
    params::Parameter<float>* volumeDensity_ = nullptr;
    nlohmann::json postJson_;
    params::Parameter<float>* fogHeight_ = nullptr;
    params::Parameter<float>* fogHeightFalloff_ = nullptr;
    // ADR-568 (§7): the layer's shape, beside the height and the falloff it shapes.
    params::Parameter<float>* fogUpperDensity_ = nullptr;
    params::Parameter<float>* fogHeightCurve_ = nullptr;
    params::Parameter<float>* horizonDensity_ = nullptr; // ADR-705 (§7)
    // ADR-055/ADR-360: the whole field, live. Two of these existed; the other twelve were authored
    // only, and `enabled` -- the gate every other one hangs off -- was reachable from neither the
    // UI nor a save, so `scene/windSpeed` could be dragged to its maximum and do nothing. The two
    // original paths keep their spelling (`scene/windSpeed`, `scene/windDirection`): renaming them
    // to `scene/wind/*` would orphan the value in every project that already has one.
    params::Parameter<bool>* windEnabled_ = nullptr;
    params::Parameter<float>* windSpeed_ = nullptr;
    params::Parameter<float>* windDirection_ = nullptr;
    params::Parameter<float>* windGustAmount_ = nullptr;
    params::Parameter<float>* windGustScale_ = nullptr;
    params::Parameter<float>* windGustSpeed_ = nullptr;
    params::Parameter<float>* windGustSharpness_ = nullptr;
    params::Parameter<float>* windTurbulence_ = nullptr;
    params::Parameter<float>* windTurbulenceScale_ = nullptr;
    params::Parameter<float>* windTurbulenceSpeed_ = nullptr;
    params::Parameter<float>* windRegionScale_ = nullptr;
    params::Parameter<float>* windRegionAmount_ = nullptr;
    params::Parameter<float>* windRegionDrift_ = nullptr;
    params::Parameter<float>* windFlutterScale_ = nullptr;
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
    // ADR-570 (§20/§22): the self-shadow march, beside the march steps it is a march of.
    params::Parameter<int>* volumeShadowSteps_ = nullptr;
    params::Parameter<float>* volumeShadowStrength_ = nullptr;
    // ADR-573 (§27): two volumetric controls that existed, shipped, and could only be
    // reached by hand-editing a scene file. Parameters now, so they are drawable,
    // automatable and modulatable like every other control on this pass.
    params::Parameter<float>* volumeLocalLights_ = nullptr;
    params::Parameter<float>* volumeMaxDistance_ = nullptr;
    // ADR-574: ADR-058's surface/volume coupling. Unreachable until the reason recorded
    // for its omission was tested and turned out to be false about its own code.
    params::Parameter<float>* fogHeightAmount_ = nullptr;
    params::Parameter<float>* volumeJitter_ = nullptr; // ADR-461
    // How far the directional shadow cascades reach; 0 = ADR-112's automatic range. See
    // `Environment::shadowRange` for why a scene is allowed an opinion about this one.
    params::Parameter<float>* shadowRange_ = nullptr;
    params::Parameter<float>* keyLight_ = nullptr;   // multiplier on the default key light
    bool addedKeyLight_ = false;
    mutable std::uint64_t frameCounter_ = 0;
    params::Parameter<glm::vec3>* fogColor_ = nullptr;
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

    // ---- multiple cameras (ADR-245) -------------------------------------------------------------
    //
    // The collection, the parameter handles for the authored cameras, and this frame's answer.
    //
    // `cameraChannels_` is parallel to the *authored* cameras (everything but the main one, whose
    // channels are the `camera*_` handles above). Rebuilt by `registerParameters`, so it is empty
    // and harmless in an unattached composition.
    CameraDirection cameraDirection_;
    struct CameraChannels {
        CameraId id = kNoCamera;
        params::Parameter<glm::vec3>* position = nullptr;
        params::Parameter<glm::vec3>* target = nullptr;
        params::Parameter<float>* fov = nullptr;
        params::Parameter<float>* focalLength = nullptr;
        params::Parameter<float>* splineT = nullptr;
        params::Parameter<float>* lookAhead = nullptr;
        params::Parameter<glm::vec3>* splineOffset = nullptr;
    };
    std::vector<CameraChannels> cameraChannels_;
    ActiveCameraState activeCamera_;
    // ADR-391. Editor state, deliberately not serialised: see `setViewportView`.
    ViewportView viewportView_;
    CameraPose editorCamera_;
    bool editorCameraSeeded_ = false;
    // What the director has seen happen, as spans. A scenario's run is live state (ADR-210: it is
    // started by `autoStart` or by a signal edge, not by a second on the timeline), so the only
    // honest span for one is "it began when this composition first saw it begin". An entry whose
    // `endSeconds <= startSeconds` is still running.
    //
    // This is the one part of camera direction that is not a pure function of the playhead, and it
    // is the same compromise ADR-217's hold already makes for the same reason. A seek clears it.
    std::vector<CameraEventSpan> cameraEvents_;
    // Evaluates one authored camera's channels into a pose. Free function shape kept private
    // because it needs the composition's splines.
    [[nodiscard]] CameraPose evaluateAuthoredCamera(const CameraRig& rig,
                                                    const CameraChannels* channels) const;
    // Evaluates the main camera exactly as this file always has: orbit, free or spline, from the
    // `camera/*` parameters. Extracted from `applyParameters` without a change of behaviour.
    [[nodiscard]] CameraPose evaluateMainCamera() const;
    // ADR-391: the frame the *viewport* is showing, applied over the film's camera once the film's
    // camera is final. A no-op in `Film` mode, which is every render.
    void applyViewportView(float mainFovDegrees);
    // Folds this frame's staging state into `cameraEvents_`. Called from `updateBehaviour`, after
    // the staging tick, so a scenario that began this frame is visible to this frame's cut.
    void observeCameraEvents(double seconds);
    // Registers `cameras/<slug>/*` for every authored camera. Called from `attach` and again when
    // the collection changes; `ParameterSet::add` returns the existing parameter for a path that is
    // already there, so a camera that did not move keeps the very pointer its tracks are bound to.
    void registerCameraChannels(params::ParameterSet& params, float reach);
    double currentTime_ = 0.0;
    void updateCharacters(const FrameTime& time); // ADR-086
    RigStats rigStats_;   // ADR-086: what the last update() spent posing skinned characters
    // ADR-551. `terrainGround_` adapts this composition's own terrain to `IGroundQuery`;
    // `groundQuery_` is what the layers actually ask, and is null until something installs one.
    class TerrainGroundQuery final : public IGroundQuery {
    public:
        explicit TerrainGroundQuery(const Composition& owner) : owner_(owner) {}
        [[nodiscard]] GroundSample sampleAt(const glm::vec3& worldPoint) const override;

    private:
        const Composition& owner_;
    };
    TerrainGroundQuery terrainGround_{*this};
    const IGroundQuery* groundQuery_ = nullptr;
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

    // Turns a behaviour's Activity into an animation state on the node it drives (ADR-086/087),
    // and answers where that node's joints are (ADR-274).
    //
    // Both halves of the seam in one object because both need the same one fact and nothing else:
    // which node this entity drives. Only the composition knows which node holds which rig, so the
    // pose sink lives here; the skeleton query needs the identical lookup, and a second class
    // beside this one would be a second copy of it kept in step by hand.
    class AnimationSink final : public entity::IPoseSink,
                               public entity::ISkeletonQuery,
                               public entity::IRootMotionSource {
    public:
        AnimationSink(Composition& owner, std::string node, const entity::Entity& entity)
            : owner_(owner), node_(std::move(node)), entity_(entity) {}
        void setLocomotion(const entity::LocomotionState& state) override;
        // ADR-300. The second half of `setLocomotion`: the fields the first half does not read.
        // Separate because it does a different thing -- it does not push an animation *state*, it
        // writes this frame's intent onto the node's layer stack, in the entity's own frame.
        void driveLayers(const entity::LocomotionState& state);
        // Phase B §4. The context the procedural layers read, rebuilt from the seam each frame and
        // kept so a layer, a test or a debug view can ask for it without re-deriving the node's
        // world transform. Valid only after `driveLayers` has run for this frame.
        [[nodiscard]] const MotionContext& motion() const { return motion_; }
        [[nodiscard]] const std::string& nodeName() const { return node_; }
        [[nodiscard]] const std::string& entityName() const { return entity_.desc().name; }
        // The joint's transform in the entity's own frame -- the rig's model space. False when
        // this node carries no rig, no rig of its carries the joint, or the rig has not been posed.
        [[nodiscard]] bool jointTransform(std::string_view joint, scene::Transform& out) const override;
        // ADR-337. The only method on this object that answers a question about the *simulation*
        // rather than about the drawing, and it does not write it: it reports, and the entity
        // decides. Entity-local, in the asset's own units, exactly like `jointTransform`.
        [[nodiscard]] entity::RootMotionSample rootMotion(double now) const override;

    private:
        Composition& owner_;
        std::string node_;
        const entity::Entity& entity_;
        // The model-space matrices the last query derived, and the rig and palette version they
        // came from. A socket query is `poseToModel` over every joint -- 89 of them on an alien --
        // and a body may carry several sockets, so the second one in a frame is a lookup. Keyed on
        // `SkinnedRig::paletteVersion`, which the rig bumps on every re-pose and on nothing else,
        // so a held or rate-limited rig answers from the cache and a re-posed one never does.
        mutable std::vector<glm::mat4> model_;
        mutable RigId modelRig_ = kInvalidRig;
        mutable std::uint64_t modelVersion_ = 0;
        mutable bool modelValid_ = false;
        MotionContext motion_;

        // Phase B. The provider chain for THIS body, and the clip provider at the bottom of it.
        // Owned here because the entries index this rig's clips; the memory that distinguishes one
        // frame from the next is owned by the `Entity` (ADR-541) and replayed by a seek.
        //
        // Built lazily on the first frame the rig exists: a rig is not loaded when the sink is
        // constructed, so building it at bind time would build it against nothing.
        entity::ClipMotionProvider clipProvider_;
        // ADR-623. In front of the clip provider when the body opted in, and only then. The asset
        // is shared with every body on the same skeleton and config (ADR-650); holding it here
        // keeps it alive for as long as this provider points into it.
        entity::MatchMotionProvider matchProvider_;
        std::shared_ptr<const MotionAsset> matchAsset_;
        entity::MotionChain chain_;
        bool chainBuilt_ = false;
        RigId chainRig_ = kInvalidRig;
        void buildChain(const SkinnedRig& rig);

    public:
        // Build this body's chain now if its rig exists and it has not been built. Called before
        // the entities advance, so the first frame a body is simulated already has its providers:
        // a chain built lazily at the first *pose* left frame 0 unsimulated, and a seek, which
        // replays with the chain present, then disagreed with a play (ADR-360).
        void prepareChain();
        [[nodiscard]] const entity::MatchMotionProvider* matcher() const {
            return matchAsset_ != nullptr ? &matchProvider_ : nullptr;
        }

    public:
        // Whether this body's base pose came from the chain on the last frame, and what the chain
        // said. Reported so that "is the opt-in actually doing anything" is answerable from
        // outside -- which is the question a seam called by nothing cannot answer.
        [[nodiscard]] const entity::MotionChain& chain() const { return chain_; }
        [[nodiscard]] bool posedByProvider() const { return posedByProvider_; }
        [[nodiscard]] const entity::MotionChainResult& chainResult() const { return chainResult_; }

    private:
        bool posedByProvider_ = false;
        entity::MotionChainResult chainResult_;
    };
    std::vector<std::unique_ptr<AnimationSink>> animationSinks_;
    // ADR-623/ADR-650: one motion database per (skeleton, feature config), shared by every body
    // that matches on it. Built on first use from the rig's own clips.
    std::map<std::string, std::shared_ptr<const MotionAsset>> matchAssets_;
    [[nodiscard]] std::shared_ptr<const MotionAsset> matchAssetFor(const SkinnedRig& rig,
                                                                   const entity::MotionMatchingDesc& m,
                                                                   const std::string& who);

public:
    // The motion context most recently built for `node`, or null when that node drives no entity
    // or has not been updated yet. Phase B §50 wants this for a debug view; a test wants it to
    // check that the seam arrived, which is the only way to catch a field published into a
    // context nobody reads.
    [[nodiscard]] const MotionContext* motionContext(std::string_view node) const;

    // Phase B §50/§64. What the motion seam did for `node` on the frame just drawn.
    //
    // Exists because "did the provider actually drive this body" has to be answerable from
    // outside the sink. A seam whose only evidence is its own internal state is a seam that can
    // stop working silently, which is the failure this whole stage was closing.
    struct MotionDebug {
        bool found = false;            // this node drives an entity at all
        bool optedIn = false;          // the scene asked for procedural motion
        bool posedByProvider = false;  // ...and it actually happened, this frame
        entity::MotionStatus status = entity::MotionStatus::NoContent;
        int provider = -1;             // which one answered, as a chain index
        std::uint32_t fellThrough = 0; // how many declined before it
        std::uint32_t generation = 0;  // the memory's, so "it never ran" is visible
        float localTime = 0.0f;
        // How many frames the RIG actually consumed an external pose on. Counted by the consumer,
        // which is the only party whose answer cannot be a claim -- see `externalPoseFrames`.
        std::uint64_t externalPoseFrames = 0;

        // **Phase B §50.** Everything above answers "did the provider seam work". These answer
        // "what is the animation actually doing", which is the question §50 exists for and which
        // nothing could ask before: the layer state lived inside `PoseLayerStack` and the body
        // state inside `MotionContext`, and neither was reachable from outside the sink.
        //
        // Gathered here rather than read out of ImGui so it can be **tested without a GPU and
        // without a panel** -- the numbers are the part that can be wrong, and a panel that
        // renders wrong numbers correctly is not debuggable, it is convincing.
        struct LayerRow {
            std::string name;
            PoseLayerKind kind = PoseLayerKind::Aim;
            float requestedWeight = 0.0f; // what the driver asked for this frame
            float realizedWeight = 0.0f;  // what the blend actually applied (§46)
            LayerResolution resolution = LayerResolution::Inactive;
            IkStatus ik = IkStatus::Solved;
            bool hasTarget = false;
            bool hasGround = false;
        };
        std::vector<LayerRow> layers;
        LocomotionMode mode = LocomotionMode::Idle;
        MotionPhase motionPhase = MotionPhase::Idle;
        float groundSpeed = 0.0f;
        float turnRate = 0.0f;
        bool hasGroundPlane = false;
        bool hasLookTarget = false;
        // The pelvis correction and whether it was enough -- "it ran" and "it worked" are
        // different answers and a panel that conflates them hides the interesting case.
        glm::vec3 bodyCompensation{0.0f};
        std::uint32_t unreachableAfterCompensation = 0;
    };
    [[nodiscard]] MotionDebug motionDebug(std::string_view node) const;

private:

    std::vector<world::EffectInstance> effects_; // ADR-702: authored, round-tripped as "effects"
    // ADR-278: authored, round-tripped as the top-level "lights".
    std::vector<AuthoredLight> authoredLights_;
    std::vector<AuthoredLight> authoredLightsRest_; // structure only; see authoredLightsRest()
    std::vector<AuthoredLightParams> authoredLightParams_; // parallel to `authoredLights_`
    // Where `rebuild` put them in `scene_.lights`, and the node index each one rides (or npos).
    // Resolved once at rebuild rather than by name every frame: the name is the author's handle on
    // the light and the index is the engine's, and looking a string up 60 times a second to move a
    // lamp is the kind of cost that is invisible until a world has three hundred of them.
    static constexpr std::size_t kNoNode = static_cast<std::size_t>(-1);
    std::size_t authoredLightFirst_ = 0;
    std::vector<std::size_t> authoredLightNodeIndex_;
    std::vector<world::HeroPoint> heroes_;   // ADR-074: authored, round-tripped as "heroes"
    // The subset of `heroes_` a *walker's* clearance field may treat as solid: the ones no entity
    // drives (ADR-349). Storage rather than a filter at the point of use, because
    // `ClearanceField::heroes` is a span and the field outlives the call that built it. Rebuilt by
    // `buildNavigator`, which is the only reader and is called after both lists are known.
    mutable std::vector<world::HeroPoint> walkerHeroes_;
    std::uint64_t heroRevision_ = 1;
    std::uint64_t heroPlacementRevision_ = 1;
    // Where each hero's node stood when the hero was last in step with it, so a move can be applied
    // to the hero as a *delta*. A delta rather than a re-measurement, because a hero's position is
    // allowed to be somewhere other than the middle of its object -- Glowmere's elder sits below
    // its crown on purpose -- and re-measuring would quietly throw that authorship away.
    //
    // Parallel to `heroes_`. An empty optional means "no node of that name", which is a legitimate
    // state: a hero may name an assembly of several nodes rather than one object.
    // Exactly the paths `attach` added to the set, taken as the difference between the set before
    // and after it ran, and removed verbatim by `unregisterParameters`.
    //
    // It used to be a hand-written list of twenty-seven path strings while `attach` registered
    // them one at a time, and it had fallen behind by **sixty-four**: all of `camera/shake/*`,
    // `camera/mode`, `camera/position`, `camera/target`, the whole `env/dayNight/*` group and the
    // `scene/volume*` family survived removing a nested scene, leaving ghost rows in the
    // Parameters panel for a scene that no longer existed and writing them back on the next save.
    // Found on 2026-09-23 by widening the nested-removal test from three named paths to a sweep.
    //
    // A diff rather than a list because the list is the thing that goes stale: this cannot fall
    // behind a registrar it is computed from. It is also why the ninety-one `params.add` calls in
    // `attach` needed no edit.
    std::vector<std::string> registeredPaths_;
    std::vector<std::optional<glm::vec3>> heroAnchors_;
    // The authoring half of the same idea, and the reason there are two of these.
    //
    // `heroAnchors_` tracks each node's **final** transform, and `HeroPoint::position` follows it.
    // That is required: a hero riding a flying object has to move with it or the director cannot
    // aim at it (`test_camera_aim`), and an editor drag has to carry the hero along
    // (`test_world_editor`). But the same field is what `toJson` writes, so following the final
    // meant every save recorded wherever the character had wandered to -- the P0 of 2026-09-22,
    // 68 m per save for `ember`, compounding without limit.
    //
    // So the live position keeps following the final, and these two carry the authored value that
    // is actually saved: `heroBaseAnchors_` tracks each node's **base**, and `heroBasePositions_`
    // moves only when that does. An author dragging a node writes the base and moves both; a
    // character walking writes only the final and moves only the live position.
    //
    // `heroBaseAnchors_` is adopted **lazily** -- on first sight in `syncHeroesToNodes` rather than
    // eagerly here -- because a node's base legitimately changes during a load, when the project's
    // `nodes/<name>/position` is applied over the scene's. Seeding eagerly made that look like a
    // drag and moved the hero by the difference, which was the last 68.000 m of the same defect.
    std::vector<std::optional<glm::vec3>> heroBaseAnchors_;
    std::vector<glm::vec3> heroBasePositions_;
    std::vector<AimFollow> aimFollow_;  // ADR-158; empty unless a director cut this camera
    // One trail per node some rig chases. Empty -- and costing nothing -- until a rig asks for a
    // lag, which is what keeps every existing camera bit-identical.
    std::vector<FollowTrail> followTrails_;
    // Set once when a chase asks for a time the trail does not reach, so the limit is reported
    // rather than silently producing an un-lagged camera that looks like a working one.
    mutable bool followTrailShortReported_ = false;
    // ADR-245: the filtered aim-follow delta, and whether it has a value yet. Reset by
    // `clearAimFollowState` (a seek) and whenever the active shot changes -- at a cut the delta is
    // zero by definition, and inheriting the previous shot's would start the new one off-centre.
    glm::vec3 aimFollowSmoothed_{0.0f};
    bool aimFollowPrimed_ = false;
    const AimFollow* aimFollowLast_ = nullptr;
    float aimFollowSmoothingMs_ = 0.0f;
    double aimFollowPrevTime_ = 0.0;

    // A hero moved and the world has not settled yet. Moving an object is a *drag* -- sixty
    // positions a second -- and each one that reached `heroRevision_` would re-cut the directed
    // shot, which is a fold of the whole track. The revision moves once, when the motion stops.
    bool heroMotionPending_ = false;
    double heroSettleAt_ = 0.0;
    double heroSettleSeconds_ = 0.25;
    void syncHeroesToNodes();
    // Appends this frame's sample to every trail some rig chases, and drops what no rig can reach.
    void recordFollowTrails();
    // Where `node` was at `seconds`, interpolated. `fallback` when there is no trail, or when the
    // time asked for is outside the one there is -- which is the head of a render and the first
    // `lag` seconds after a seek.
    [[nodiscard]] FollowTrail::Sample followTrailAt(const std::string& node, double seconds,
                                                    const FollowTrail::Sample& fallback) const;
    void markHeroesMoved();
    void settleHeroes();
    std::vector<entity::EntityDesc> entityDescs_; // ADR-088: authored, round-tripped as "entities"
    std::vector<entity::EntityWorld::EventProfile> eventProfiles_; // Phase D §26: "worldEvents"
    // Phase D §26: the director's beats this update, raised as world events. Shared by
    // `updateBehaviour` and the replay in `seekWithDirector`.
    void raiseDirectorBeats(double time);
    // ADR-700: the director replay's inputs that are not parameter bases, for the checkpoint key.
    [[nodiscard]] std::uint64_t replayInputKey() const;
    // ADR-671: `visualPlacement` for a replay, one step old, from the finals.
    class ReplayPlacement final : public stage::IVisualPlacement {
    public:
        explicit ReplayPlacement(const Composition& comp) : comp_(comp) {}
        void invalidate() { valid_.assign(valid_.size(), 0u); }
        void capture();
        [[nodiscard]] bool visualPlacement(std::string_view node,
                                           stage::VisualPlacement& out) const override;
        // ADR-700: what a checkpoint keeps of it -- the last step's "flattening", which the next
        // step's director reads.
        [[nodiscard]] const std::vector<stage::VisualPlacement>& placed() const { return placed_; }
        [[nodiscard]] const std::vector<std::uint8_t>& valid() const { return valid_; }
        void set(std::vector<stage::VisualPlacement> placed, std::vector<std::uint8_t> valid) {
            placed_ = std::move(placed);
            valid_ = std::move(valid);
        }

    private:
        const Composition& comp_;
        std::vector<stage::VisualPlacement> placed_;
        std::vector<std::uint8_t> valid_;
    };
    friend class ReplayPlacement;
    // ADR-700: the replay's last placement, for the first frame after a seek. That frame's director
    // asks where nodes are drawn, and a play answers from the previous frame's flattening -- but
    // after a seek the last flattening is from before the jump, a different second entirely. So
    // until the next `update` flattens, `visualPlacement` answers from what the replay captured
    // after its last step, which is the flattening the play would have had. Found by checking the
    // frame after a 30 s scrub: `bull-18`, mid-abduction, 43 m from the play's.
    std::vector<stage::VisualPlacement> seekPlaced_;
    std::vector<std::uint8_t> seekPlacedValid_;
    bool seekPlacementLive_ = false;
    stage::StagingDesc stagingDesc_;              // ADR-209: authored, round-tripped as "staging"
    stage::Staging staging_;
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
    // 0 keeps the navigator's own default (0.45 m, a person), which is what every scene written
    // before ADR-199 gets.
    float navBodyRadius_ = 0.0f;
    float navWadeDepth_ = 0.0f;
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

    // ADR-351: LOD chains built for imported assets, keyed by the asset path and the ladder asked
    // for. A cache with a lifetime longer than a flatten, which this file is otherwise careful not
    // to have -- justified because building the Tree of Life's chains takes 3.4 seconds of pure
    // arithmetic and a flatten runs on every parameter edit. What makes it safe is that the value
    // is a pure function of its key: `buildLodChain` is deterministic and reads nothing but the
    // mesh, and the mesh comes from the registry, which is itself keyed on the path and bumps a
    // version when a file is reloaded -- so the version is in the key too.
    // ADR-351: this asset's LOD chains, built once per (path, version, ladder) and shared.
    [[nodiscard]] std::shared_ptr<const std::vector<MeshLodChain>>
    lodChainsFor(const assets::SceneAsset& asset, const NodeLod& lod, const std::string& owner);

    struct LodCacheKey {
        std::string asset;
        std::uint64_t assetVersion = 0;
        std::string ladder; // the settings, rendered; see `lodLadderKey`
        [[nodiscard]] bool operator<(const LodCacheKey& other) const {
            return std::tie(asset, assetVersion, ladder) <
                   std::tie(other.asset, other.assetVersion, other.ladder);
        }
    };
    std::map<LodCacheKey, std::shared_ptr<const std::vector<MeshLodChain>>> lodCache_;

    // Flattened bookkeeping: per node, the entity index range in scene_ and the rest transforms.
    struct NodeRange {
        std::size_t firstEntity = 0;
        std::size_t entityCount = 0;
        // The node's world transform *as the last flattening used it* -- root fold included, so it
        // is the transform every entity, emitter and light in this range was placed by.
        //
        // Recorded rather than recomputed, and the difference is not bookkeeping. `applyParameters`
        // reads the parameter **finals**, and finals are rebuilt from bases at the top of every
        // frame and then written by routes, reactions and `EntityWorld::applyOffsets` -- so between
        // the reset and the entity pass, `nodeWorldTransform` answers *the position the file was
        // authored with*, not the one anything is drawn at. The director runs in exactly that
        // window. Asking it there cost an afternoon: a wandering cow's `Drawn` placement came out
        // 5.3 m wrong -- the distance it had walked from its authored spot -- while every animal
        // that never left its authored spot looked perfect.
        Transform world;
        bool worldValid = false;
        std::vector<Transform> restTransforms;       // entity transforms inside the asset
        std::vector<float> restEmissive;
        std::vector<float> restRoughness;
        // The opacity the asset was built with, captured the first time a node's `opacity`
        // parameter is read rather than pushed alongside `restRoughness` at every one of the six
        // sites that build a range. Lazy because a vector that is silently shorter than
        // `entityCount` is an out-of-bounds write, and six push_backs that must stay in step with
        // each other is the shape of defect this file has the most of.
        std::vector<float> restOpacity;
        std::vector<std::uint8_t> restAlphaMode;
        int particleIndex = -1;                      // index into scene_.particles (Particles kind)
        int proceduralIndex = -1;                    // index into scene_.procedurals (Procedural kind)
        std::size_t proceduralSubCount = 0;          // the asset's other materials, immediately after it
        // A terrain's ecology scatter: one procedural per layer, plus each layer's material subs,
        // emitted in one run. Recorded because otherwise nothing maps them back to the node that
        // owns them -- `nodeForProcedural` resolved 29 of Glowmere Valley 2's 42 procedurals and
        // returned nothing for the other 13, which are every fern, bush, boulder and scattered
        // fungus in the world. A click on one selected nothing at all.
        std::size_t ecologyFirst = 0;
        std::size_t ecologyCount = 0;
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

// ---- a project's record of the nodes its session added to, and removed from, its scene ----------
//
// ADR-330, and it is ADR-207 / ADR-230 / ADR-276's family a fourth time. A project whose scene came
// from a file saves that scene **by reference** -- `assets.scene.path` plus a hash of bytes already
// on disk -- so an object deleted in the world editor lived in the window the person was looking at
// and in no document any render reads. Measured on the owner's own project: 80 nodes, 79 after a
// delete, **80 again after a save and a reload**.
//
// The record is a *difference* against the scene file rather than a copy of the live list, and the
// difference is taken **by name**:
//
//  * By name, because a node's numbers are already the project's `parameters` block -- ADR-271's
//    boundary -- and a second copy of a moved node's position would be a second answer to one
//    question. What the project owes is the *set*: which objects there are.
//  * A difference rather than a copy, because a copy makes the scene file dead for the project that
//    holds it: the next correction anybody makes to a shared scene would never reach it again. It
//    is also what makes "added, then deleted" write nothing at all, rather than eighty nodes that
//    happen to match.
//
// `removed` is what makes it work at all, and it is the half that has no natural home: a removal is
// a negative fact, so there is no node left to carry it and the only thing that can record it is
// the absence, measured against the file.

// The record, or null when the session's node set is the scene file's. Null rather than an empty
// object, so a project nobody edited is byte-stable through a save -- the control that makes the
// positive arms mean something (ADR-182).
//
// `liveNodes` is `Composition::toJson()["nodes"]`: the same serialisation a scene file gets, so an
// added node is written exactly as an authored one -- and graph-installed nodes are already left
// out of it, because the graph re-emits them on load and recording them would double them.
[[nodiscard]] nlohmann::json nodeEditsAgainst(const nlohmann::json& liveNodes, const nlohmann::json& sceneDoc);

// Null when the session's authored lights are the scene file's, so a project nobody edited stays
// byte-stable through a save (ADR-182's control). Otherwise the whole live list, which is the shape
// `effects` and `heroes` use -- most of a light's fields are not parameters, so there is no
// by-name difference to record the way `nodeEditsAgainst` can. Pure, so it is testable without an
// engine; the converters it needs stay file-local.
// Parses a `"lights"` array -- a scene file's or a project's -- into the list `setAuthoredLights`
// takes. The whole block is refused on a bad member rather than the member skipped, for the reason
// the heroes loader gives: a light that quietly failed to load looks exactly like a light nobody
// authored.
[[nodiscard]] Result<std::vector<Composition::AuthoredLight>> authoredLightsFromJson(
    const nlohmann::json& array, std::string_view where);

[[nodiscard]] nlohmann::json authoredLightsAgainst(const nlohmann::json& liveLights,
                                                   const nlohmann::json& sceneDoc);

// Splices the record into a scene document before it is parsed. A null or empty record is a no-op.
//
// Before the parse, rather than onto the composition afterwards, for one reason worth its ten
// lines: the composition the engine ends up with is then *exactly* the one the parser would build
// from a scene file with those edits made. One code path for bringing a node into being, with its
// parent, its nested scene and its asset resolved against the scene's own folder. A second
// construction path for "the nodes that came from the project" would be a second set of rules to
// keep in step with a five-hundred-line parser.
//
// A removal reproduces `Composition::detachNode` rather than merely dropping the entry: a child of
// a removed node keeps its local transform under the grandparent. That is the editor's own
// semantics, and a reload that orphaned the child instead would move it.
void applyNodeEdits(nlohmann::json& sceneDoc, const nlohmann::json& edits);

} // namespace avgen::scene
