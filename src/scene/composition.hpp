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
//   "nodes": [ {"name", "kind": "gltf|orb|grid|particles|scene", "asset": path,
//               "position": [x,y,z], "rotation": [degrees x,y,z], "scale": [x,y,z], "visible": true,
//               "emissiveBoost": 1.0, "roughnessScale": 1.0, "particles": {...settings...}} ] }

#include "assets/asset_registry.hpp"
#include "core/error.hpp"
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

enum class NodeKind : std::uint8_t { Gltf, Orb, Grid, Particles, Scene };
const char* nodeKindName(NodeKind kind);
Result<NodeKind> nodeKindFromName(const std::string& name);

struct CompositionNode {
    std::string name;
    NodeKind kind = NodeKind::Gltf;
    std::filesystem::path asset;   // gltf/glb for Gltf, scene file for Scene (as written; resolved via registry)
    Transform transform;           // local transform of the instance
    bool visible = true;
    float emissiveBoost = 1.0f;
    float roughnessScale = 1.0f;
    ParticleSystem particles;      // settings for kind Particles (name is taken from the node)

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
    bool removeNode(const std::string& name);
    [[nodiscard]] CompositionNode* findNode(const std::string& name);
    [[nodiscard]] const std::vector<std::unique_ptr<CompositionNode>>& nodes() const { return nodes_; }
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
    [[nodiscard]] Transform nodeTransform(const CompositionNode& node) const; // params or authored values
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

    // Flattened bookkeeping: per node, the entity index range in scene_ and the rest transforms.
    struct NodeRange {
        std::size_t firstEntity = 0;
        std::size_t entityCount = 0;
        std::vector<Transform> restTransforms;       // entity transforms inside the asset
        std::vector<float> restEmissive;
        std::vector<float> restRoughness;
        int particleIndex = -1;                      // index into scene_.particles (Particles kind)
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
