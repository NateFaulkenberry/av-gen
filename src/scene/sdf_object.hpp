#pragma once

// SDF objects in a scene (ADR-027): a spatial::SdfTree with a transform, a material and a render
// mode. Raymarch: rendering::SdfRenderer sphere-traces the object inside its bounds in the lit
// pass and writes depth. Mesh: the CPU meshes it with surface nets (cached by the tree's hash)
// and the result is drawn as an ordinary entity mesh. Parameters follow the rest/live pattern:
// "sdf/<node>/node/<i>/<field>" (i = pre-order index of enabled and disabled nodes, 1-based),
// plus visible, transform/*, material/*, bounds, resolution.

#include "core/error.hpp"
#include "params/parameter_set.hpp"
#include "scene/scene_types.hpp"
#include "spatial/sdf.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace avgen::scene {

enum class SdfRenderMode : std::uint8_t { Raymarch, Mesh };
[[nodiscard]] const char* sdfRenderModeName(SdfRenderMode mode);
[[nodiscard]] std::optional<SdfRenderMode> sdfRenderModeFromName(std::string_view name);

struct SdfObject {
    std::string name = "sdf";
    bool visible = true;
    spatial::SdfTree tree;
    Transform transform;                 // tree-local -> world (parent node folded in by the composition)
    Material material;
    SdfRenderMode renderMode = SdfRenderMode::Raymarch;
    glm::vec3 boundsMin{-5.0f};          // tree-local box the object lives in (raymarch entry, meshing domain)
    glm::vec3 boundsMax{5.0f};
    int resolution = 48;                 // surface-nets cells per axis (Mesh mode)
    int maxSteps = 128;                  // raymarch
    float epsilon = 0.002f;              // hit threshold (× distance for perspective)
    float stepScale = 0.9f;              // relaxation (displaced trees need < 1)
    float normalEpsilon = 0.002f;
    // Structural outputs
    std::uint64_t structureVersion = 0;
    std::uint64_t builtHash = 0;
    MeshData mesh;                       // Mesh mode result (empty for Raymarch)
    std::uint64_t meshHash = 0;

    [[nodiscard]] Result<void> validate() const;
    // Re-meshes (Mesh mode) or just bumps the version when the tree/bounds/resolution changed.
    bool rebuild(double time = 0.0, const spatial::FieldSet* fields = nullptr);
    [[nodiscard]] std::uint64_t structuralHash() const; // tree, bounds, resolution, render mode
    [[nodiscard]] nlohmann::json toJson() const;
    static Result<SdfObject> fromJson(const nlohmann::json& j);
};

struct SdfParameters {
    std::string prefix;
    std::vector<params::IParameter*> all;
    params::Parameter<bool>* visible = nullptr;
    params::Parameter<glm::vec3>* position = nullptr;
    params::Parameter<glm::vec3>* rotation = nullptr;
    params::Parameter<glm::vec3>* scale = nullptr;
    std::vector<params::Parameter<float>*> nodeAmount;  // node/<i>/amount, pre-order
    std::vector<params::Parameter<float>*> nodeRadius;  // node/<i>/radius
    std::vector<params::Parameter<float>*> nodeSmooth;  // node/<i>/smooth
    params::Parameter<glm::vec3>* baseColor = nullptr;
    params::Parameter<float>* emissive = nullptr;
};
[[nodiscard]] SdfParameters registerSdfParameters(params::ParameterSet& params, const SdfObject& rest,
                                                  const std::string& prefix);
// Copies finals into `live`; structural nodes (kind, children, enabled) come from `rest`.
// Returns true when the structural hash changed (the caller rebuilds).
bool applySdfParameters(const SdfParameters& p, const SdfObject& rest, SdfObject& live);
void unregisterSdfParameters(params::ParameterSet& params, const SdfParameters& p);

} // namespace avgen::scene
