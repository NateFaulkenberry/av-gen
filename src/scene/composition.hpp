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
#include "scene/material_params.hpp"
#include "scene/sdf_object.hpp"
#include "scene/spline_params.hpp"
#include "scene/particles.hpp"
#include "scene/scene_controller.hpp"

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace avgen::scene {

enum class NodeKind : std::uint8_t { Gltf, Orb, Grid, Particles, Scene, Procedural, Field, Spline, Sdf };
const char* nodeKindName(NodeKind kind);
Result<NodeKind> nodeKindFromName(const std::string& name);

struct CompositionNode {
    std::string name;
    NodeKind kind = NodeKind::Gltf;
    std::filesystem::path asset;   // gltf/glb for Gltf, scene file for Scene (as written; resolved via registry)
    std::string parent;            // name of the parent node ("" = root); world = parent world x local
    Transform transform;           // local transform of the instance
    bool visible = true;
    float emissiveBoost = 1.0f;
    float roughnessScale = 1.0f;
    ParticleSystem particles;      // settings for kind Particles (name is taken from the node)
    ProceduralGeometry procedural; // settings for kind Procedural (ADR-023; name is taken from the node)
    spatial::FieldSpec field;      // settings for kind Field (ADR-025; name is taken from the node; the node
                                   // transform is the field's frame, folded into the FieldSpec at rebuild)
    spatial::Spline spline;        // settings for kind Spline (ADR-026; the node transform is applied to the
                                   // generated control points at rebuild)
    SdfObject sdf;                 // settings for kind Sdf (ADR-027; node transform folded into sdf.transform)

    // Runtime (not serialised)
    std::shared_ptr<const assets::SceneAsset> sceneAsset; // Gltf
    std::unique_ptr<class Composition> child;              // Scene (nested)
    params::Parameter<glm::vec3>* positionParam = nullptr;
    params::Parameter<glm::vec3>* rotationParam = nullptr; // Euler degrees
    params::Parameter<glm::vec3>* scaleParam = nullptr;
    params::Parameter<bool>* visibleParam = nullptr;
    params::Parameter<float>* emissiveParam = nullptr;
    params::Parameter<float>* roughnessParam = nullptr;
    ParticleParameters particleParams;
    ParticleSystem particleRest;
    ProceduralParameters proceduralParams;
    ProceduralGeometry proceduralRest;
    FieldParameters fieldParams;
    spatial::FieldSpec fieldRest;
    SplineParameters splineParams;
    spatial::Spline splineRest;
    SdfParameters sdfParams;
    SdfObject sdfRest;
};

class Composition final : public SceneController {
public:
    Composition(assets::AssetRegistry& registry, std::string name = "composition");
    ~Composition() override;

    // ---- SceneController ----
    [[nodiscard]] std::string name() const override { return name_; }
    void update(const FrameTime& time) override;
    [[nodiscard]] const Scene& scene() const override { return scene_; }
    [[nodiscard]] Scene& scene() override { return scene_; }

    // ---- nodes ----
    // Loads the node's asset (Gltf / Scene kinds) through the registry; names are made unique.
    // Registers the node's parameters immediately when the composition is attached.
    Result<CompositionNode*> addNode(CompositionNode node);
    // Removes a node; its children are re-parented to the removed node's parent.
    bool removeNode(const std::string& name);
    [[nodiscard]] CompositionNode* findNode(const std::string& name);
    [[nodiscard]] const CompositionNode* findNode(const std::string& name) const;
    // Re-parents `name` under `parent` ("" = root). Errors: unknown node, a cycle.
    Result<void> setParent(const std::string& name, const std::string& parent);
    // World transform of a node (parent chain applied, parameters included), without the root
    // scale/rotation. Unknown parents are treated as roots.
    [[nodiscard]] Transform nodeWorldTransform(const CompositionNode& node) const;
    [[nodiscard]] const std::vector<std::unique_ptr<CompositionNode>>& nodes() const { return nodes_; }
    // ---- composition (ADR-038) ----
    // What the frame is about: focal points, depth layers and exclusion regions. Its fields are
    // appended to the scene's field set at every rebuild under reserved "composition.*" names.
    [[nodiscard]] const CompositionData& composition() const { return compositionData_; }
    void setComposition(CompositionData data) {
        compositionData_ = std::move(data);
        dirty_ = true;
    }

    // ---- procedural graph (ADR-028) ----
    // A composition is either graph-driven or flat: installing a graph replaces every node this
    // composition previously installed from a graph (hand-added nodes are left alone). The graph
    // is evaluated on load and whenever `markGraphDirty()` is called; its emitted objects become
    // ordinary nodes with ordinary parameters, and its routes are added to the modulator.
    Result<void> setGraph(graph::Graph graph, params::Modulator* modulator = nullptr);
    [[nodiscard]] const graph::Graph* graph() const { return graph_ ? &*graph_ : nullptr; }
    [[nodiscard]] graph::Graph* graph() { return graph_ ? &*graph_ : nullptr; }
    void markGraphDirty() { graphDirty_ = true; }
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
    [[nodiscard]] const std::filesystem::path& sourcePath() const { return sourcePath_; }

    static void addDefaultRoutes(params::Modulator& modulator);

private:
    void rebuild();          // flattens nodes into scene_ (meshes/textures/entities/particles)
    void ensureBuilt();      // rebuild() when dirty
    void applyParameters();  // node finals -> transforms/materials/particles; camera; environment
    void registerNodeParameters(CompositionNode& node);
    void unregisterNodeParameters(CompositionNode& node);
    void unregisterParameters(); // removes every parameter this composition registered, then detach()
    std::string uniqueName(const std::string& base) const;
    [[nodiscard]] std::string nestedPrefix(const CompositionNode& node) const;
    void rebuildProcedurals();  // (re)generates every procedural object against the flattened scene
    void rebuildSdfs();
    [[nodiscard]] Transform nodeTransform(const CompositionNode& node) const; // params or authored values (local)
    // True when making `parent` the parent of `node` would close a cycle (node and parent by name).
    [[nodiscard]] bool wouldCycle(const std::string& node, const std::string& parent) const;
    [[nodiscard]] static bool nodeVisible(const CompositionNode& node);
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
    // Authored camera/environment settings (used when unattached and as parameter defaults).
    std::optional<float> cameraDistanceSetting_; // empty = fitted to the bounds
    std::optional<float> cameraHeightSetting_;
    float cameraOrbitSpeedSetting_ = 0.12f;
    float cameraFovSetting_ = 50.0f;
    float envIntensitySetting_ = 1.0f;
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
    params::Parameter<float>* fogHeight_ = nullptr;
    params::Parameter<float>* fogHeightFalloff_ = nullptr;
    params::Parameter<float>* volumeScattering_ = nullptr;
    params::Parameter<float>* volumeAbsorption_ = nullptr;
    params::Parameter<float>* volumeAnisotropy_ = nullptr;
    params::Parameter<float>* volumeNoise_ = nullptr;
    params::Parameter<float>* volumeNoiseScale_ = nullptr;
    params::Parameter<float>* volumeNoiseSpeed_ = nullptr;
    params::Parameter<float>* volumeEmission_ = nullptr;
    params::Parameter<int>* volumeSteps_ = nullptr;
    params::Parameter<float>* keyLight_ = nullptr;   // multiplier on the default key light
    bool addedKeyLight_ = false;
    std::uint64_t frameCounter_ = 0;
    params::Parameter<glm::vec3>* fogColor_ = nullptr;
    float fogDensitySetting_ = 0.0f;
    scene::Environment volumeSetting_; // the scene-file values behind the scene/volume* parameters
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
    double currentTime_ = 0.0;
    // Scene-level material programs (ADR-030): "materialPrograms" in the file, parameters
    // "material/<name>/…", referenced by Material::program.
    CompositionData compositionData_;
    std::optional<graph::Graph> graph_;
    bool graphDirty_ = false;
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
        int fieldIndex = -1;                         // index into scene_.fields.fields (Field kind)
        std::size_t firstField = 0;                  // Scene kind: the child's fields copied in
        std::size_t fieldCount = 0;
        int splineIndex = -1;                        // index into scene_.splines.splines (Spline kind)
        std::size_t firstSpline = 0;
        std::size_t splineCount = 0;
        int sdfIndex = -1;                           // index into scene_.sdfs (Sdf kind)
        std::size_t firstSdf = 0;
        std::size_t sdfCount = 0;
        std::size_t firstMaterial = 0;               // Scene kind: the child's material programs copied in
        std::size_t materialCount = 0;
        std::size_t firstProcedural = 0;             // Scene kind: the child's procedurals copied in
        std::size_t proceduralCount = 0;
        std::size_t firstParticle = 0;               // particle range (Particles and Scene kinds)
        std::size_t particleCount = 0;
        std::uint64_t childMeshVersion = 0;          // Scene kind: what was flattened
        std::size_t childEntityCount = 0;
        std::size_t childParticleCount = 0;
    };
    std::vector<NodeRange> ranges_;

    params::ParameterSet* params_ = nullptr;
    params::Modulator* modulator_ = nullptr;
    std::string prefix_;
    params::Parameter<float>* cameraDistance_ = nullptr;
    params::Parameter<float>* cameraHeight_ = nullptr;
    params::Parameter<float>* cameraOrbitSpeed_ = nullptr;
    params::Parameter<float>* cameraFov_ = nullptr;
    params::Parameter<float>* envIntensity_ = nullptr;
    params::Parameter<float>* envRotation_ = nullptr;
    params::Parameter<float>* brightness_ = nullptr;
    params::Parameter<float>* gridIntensity_ = nullptr;
    params::Parameter<float>* rootScale_ = nullptr;
    params::Parameter<float>* rootRotationSpeed_ = nullptr;
    params::Parameter<float>* rootImpulse_ = nullptr;
    float rootAngle_ = 0.0f;
};

} // namespace avgen::scene
