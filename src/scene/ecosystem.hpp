#pragma once

// ADR-1200: the ecosystem, the first ENVIRONMENT (docs/prototypes/bioluminescent/03-architecture.md).
//
// An ecosystem is the part of a scene the general renderer cannot draw well: the micro-scale light of a living
// world. It is a set of EMITTER LAYERS. Each layer attaches a template of emitter points (photophores, polyps,
// beads) to every instance of one or more ordinary procedural nodes (its HOSTS: the organisms' bodies), and
// rendering::EcosystemRenderer draws those points by accumulating their light in compute instead of rasterising
// geometry for them. The bodies stay conventional procedurals, so an ecosystem scene is a HYBRID by construction:
// ordinary nodes, characters and cameras share its depth and its frame.
//
// Why points (measured, 03-architecture.md §3.1): the Rift's polyps and photophores, drawn as 20-triangle beads,
// cost about 22 ms of a 1080p frame in the lit pass and the depth prepass, and drawing them unlit saved nothing.
// The cost is the rasterisation of sub-pixel geometry, which no material setting can fix.
//
// What an emitter does (per point, per frame, on the GPU):
//   rest      = intensity x (1 + breath x slow per-point cycle) x (1 - sparsity gate) x (1 + flicker x twinkle)
//               x (1 + wakeGain x wakeField(p))       an awakened region keeps glowing between events
//               + spontaneous pulses (pulseRate per minute, decaying over pulseDecay seconds)
//   response  = responseGain x max(0, field(p) - responseThreshold)            field: responseField at the point
//               with `travel` > 0, a point far along its organism (v -> 1) hears `lagField` instead, so light climbs
//               a stalk or runs out along an arm behind the front
//   colour    = mix(color, excitedColor, saturate(response))                    fluorescence: the wave shifts hue
//   radiance  = colour x (rest + excitedIntensity x response)
// Every input is a function of time and the point's identity, so play, scrub and offline agree.
//
// The scene file's block:
//   "ecosystem": { "spriteRadius": 1.5, "maxSprites": 65536,
//     "layers": [ { "name": "polyps", "hosts": ["matCushion0"], "template": "meshes/mat_polyps.emit.json",
//                   "color": [0.04, 0.38, 1.0], "intensity": 0.4, "excitedIntensity": 9.0, "size": 1.0,
//                   "responseField": "prop", "responseGain": 1.0, "responseThreshold": 0.05,
//                   "lagField": "", "travel": 0.0, "breath": 0.3, "breathRate": 0.07,
//                   "flicker": 0.0, "flickerRate": 5.0, "pulseRate": 0.5, "pulseDecay": 1.2,
//                   "sparsity": 0.2, "maxDistance": 300.0 } ] }
// An emitter template file is { "points": [[x, y, z, v, u, radius], ...] } in the host's source space: v runs
// along the organism (0 root .. 1 tip), u is the point's own hash in [0, 1), radius in metres.

#include "core/error.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace avgen::params {
class ParameterSet;
class IParameter;
} // namespace avgen::params

namespace avgen::scene {

struct EmitterPoint {
    glm::vec3 position{0.0f}; // host source space
    float v = 0.0f;           // along the organism, 0 root .. 1 tip
    float u = 0.0f;           // the point's own hash
    float radius = 0.02f;     // metres
};
static_assert(sizeof(EmitterPoint) == 24);

// Reads an emitter template file ({"points": [[x, y, z, v, u, r], ...]}).
[[nodiscard]] Result<std::vector<EmitterPoint>> loadEmitterTemplate(const std::filesystem::path& file);
[[nodiscard]] Result<std::vector<EmitterPoint>> emitterTemplateFromJson(const nlohmann::json& j);

constexpr std::size_t kMaxEcosystemLayers = 16;
constexpr std::size_t kMaxTemplatePoints = 65536;

struct EmitterLayer {
    std::string name;
    bool enabled = true;
    std::vector<std::string> hosts;  // procedural node names whose instances carry the template
    std::string templatePath;        // as written; resolved by the loader
    // Runtime: the loaded template (never serialised; shared so a per-frame copy of the Scene is a pointer).
    std::shared_ptr<const std::vector<EmitterPoint>> points;
    std::uint64_t pointsHash = 0;

    glm::vec3 color{0.04f, 0.38f, 1.0f};
    glm::vec3 excitedColor{0.04f, 0.38f, 1.0f};
    float intensity = 0.5f;          // rest radiance, multiples of scene white
    float excitedIntensity = 8.0f;   // added at full response
    float size = 1.0f;               // multiplies every point's radius
    std::string responseField;       // scalar field the layer answers (empty: rest only)
    float responseGain = 1.0f;
    float responseThreshold = 0.0f;
    std::string lagField;            // what a point far along its organism hears (empty: none)
    float travel = 0.0f;             // 0..1: how far along v the lag field takes over
    std::string wakeField;           // scalar field that raises the rest glow (an awakened region stays alive)
    float wakeGain = 0.0f;           // rest x (1 + wakeGain x wakeField(p))
    float breath = 0.0f;             // slow autonomous cycle depth, 0..1
    float breathRate = 0.08f;        // Hz
    float flicker = 0.0f;            // fast per-point twinkle depth, 0..1
    float flickerRate = 5.0f;        // Hz
    float pulseRate = 0.0f;          // spontaneous flashes per point per minute
    float pulseDecay = 1.0f;         // seconds a spontaneous flash takes to fade
    float sparsity = 0.0f;           // fraction of points dark at rest (they still answer the field)
    float maxDistance = 300.0f;      // metres; fades out over the last quarter

    [[nodiscard]] Result<void> validate() const;
};

struct Ecosystem {
    bool enabled = true;
    float spriteRadius = 1.5f;       // pixels: a point larger than this is drawn as a soft sprite, smaller is splatted
    std::uint32_t maxSprites = 65536;
    std::vector<EmitterLayer> layers;

    [[nodiscard]] bool active() const;
    [[nodiscard]] Result<void> validate() const;
    [[nodiscard]] nlohmann::json toJson() const;
    // Parses the block; templates are NOT loaded (the composition resolves the paths, then calls loadTemplates).
    static Result<Ecosystem> fromJson(const nlohmann::json& j);
    // Loads every layer's template, resolving a relative path against `base`.
    [[nodiscard]] Result<void> loadTemplates(const std::filesystem::path& base);
};

// ---- parameters (ecosystem/<layer>/<leaf>) -------------------------------------------------------------------------

struct EcosystemParameters {
    struct Layer {
        std::string prefix; // "ecosystem/<layer>/"
        std::vector<params::IParameter*> all;
    };
    std::vector<Layer> layers;
};

// Registers each layer's behaviour under `prefix` ("ecosystem/"). Defaults are the authored values.
[[nodiscard]] EcosystemParameters registerEcosystemParameters(params::ParameterSet& params, const Ecosystem& rest,
                                                              const std::string& prefix);
// Copies the finals into `live` (layout -- hosts, templates, fields -- comes from `rest`).
void applyEcosystemParameters(const EcosystemParameters& p, const Ecosystem& rest, Ecosystem& live);

} // namespace avgen::scene
