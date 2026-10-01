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

// ADR-1002: the ray-marched surface's own cheap lighting terms, evaluated from the field itself at the
// hit (Raymarch mode only; per-frame uniforms, not structural). Every term is off at 0.
struct SdfLook {
    float aoStrength = 0.0f;       // 5-tap SDF ambient occlusion along the normal; 0 = off, 1 = full
    float aoDistance = 1.0f;       // how far (tree-local units) the occlusion looks
    float edgeIntensity = 0.0f;    // emissive edges from the field's curvature (a 4-tap Laplacian); 0 = off
    float edgeWidth = 0.05f;       // the Laplacian's tap offset: wider picks up broader creases
    glm::vec3 edgeColor{0.25f, 0.8f, 1.0f};
    // ADR-1047: the line look. `edgePixels` > 0 sets the edge's width in screen pixels at every distance
    // (the tap offset follows the hit's distance), so lines stay a few pixels wide in a small room and do
    // not shrink into moire far away; 0 keeps `edgeWidth` in tree-local units. The mask is
    // smoothstep(edgeThreshold, edgeThreshold + edgeSoftness, 1 - cos angle): a small softness draws a
    // hard, thin line, a large one a soft glow across the crease.
    float edgePixels = 0.0f;
    float edgeThreshold = 0.02f;
    float edgeSoftness = 0.28f;
    // ADR-1052: a rim (fresnel) emission: rimColor * rimIntensity * (1 - |n . v|)^rimPower, added as light
    // where the surface turns away from the eye, so a smooth silhouette (a rounded head, a sphere) glows
    // against the dark where the crease-only edge term draws nothing. Off at intensity 0 (byte-identical).
    float rimIntensity = 0.0f;
    glm::vec3 rimColor{1.0f};
    float rimPower = 3.0f;
    // ADR-1054: screen static. A surface with `static` > 0 shows animated snow: its colour and emission are
    // multiplied by a hash per cell of `staticCell` metres (object-local) that re-rolls `staticRate` times a
    // second, with a rolling bar of strength `staticRoll` drifting down it. A pure function of time.
    float staticCell = 0.012f;
    float staticRate = 24.0f;
    float staticRoll = 0.35f;
    float shadowStrength = 0.0f;   // an SDF soft shadow towards `shadowDirection`; 0 = off (a second march)
    float shadowSoftness = 8.0f;   // Quilez's k: larger = harder
    glm::vec3 shadowDirection{0.3f, 1.0f, 0.2f}; // world space, towards the light
    int shadowSteps = 32;
};

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
    float maxDistance = 0.0f;            // ADR-1002: march length cap in tree-local units (0 = the bounds only)
    // ADR-1002: whether a Raymarch object is marched into the depth prepass and into the shadow maps.
    // Each is a full second march over the object's screen (or shadow-map) rect. Off: the lit pass
    // still writes depth, so meshes and particles compose correctly; what is lost is the object's
    // depth in the passes that read the prepass before the lit pass (GTAO, the screen-space shadow
    // mask, contact shadows) and, for castShadows, its shadow in the shadow maps.
    bool depthPrepass = true;
    // ADR-1003: march a WGSL compilation of the tree instead of interpreting the packed program. A
    // structural change (a kind, a child, `enabled`) compiles a new pipeline (a hitch of tens of ms);
    // parameter changes, a morph's amount and counts included, do not.
    bool compile = false;
    bool castShadows = true;
    SdfLook look;                        // ADR-1002
    // ADR-1044: surfaces. Empty = one surface, the material as it always was. Otherwise a compiled tree's
    // node `material` ids pick one per hit, and its `color` multiplies the material's base colour and its
    // `emission` the material's emission (colour x intensity) -- so author the material white with emission
    // colour white and intensity 1, and give each surface its real albedo and emitted radiance.
    // Parameters: surface/<k>/color, surface/<k>/emission. Compiled objects only (the interpreter shades
    // every hit as surface 0).
    struct Surface {
        glm::vec3 color{1.0f};
        glm::vec3 emission{0.0f};
        glm::vec3 edge{1.0f}; // ADR-1047: multiplies the object's edge colour on this surface (0 = no lines)
        float rim = 1.0f;     // ADR-1052: multiplies the object's rim on this surface (0 = no rim)
        float staticAmount = 0.0f; // ADR-1054: 0 = a plain surface, 1 = full static (TV snow)
    };
    std::vector<Surface> surfaces;
    // ADR-903: the owning node's `emissiveBoost`, applied by the lit shader after the material
    // program. Runtime only (the Composition writes it every frame); 1 is the surface as authored.
    float emissionGain = 1.0f;
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
// ADR-1002 parameters, beside the rest: march/{maxSteps, epsilon, stepScale, maxDistance} and
// look/{ao/strength, ao/distance, edge/intensity, edge/width, edge/color, shadow/strength,
// shadow/softness, shadow/direction, shadow/steps}.
[[nodiscard]] SdfParameters registerSdfParameters(params::ParameterSet& params, const SdfObject& rest,
                                                  const std::string& prefix);
// Copies finals into `live`; structural nodes (kind, children, enabled) come from `rest`.
// Returns true when the structural hash changed (the caller rebuilds).
bool applySdfParameters(const SdfParameters& p, const SdfObject& rest, SdfObject& live);
void unregisterSdfParameters(params::ParameterSet& params, const SdfParameters& p);

} // namespace avgen::scene
