#pragma once

// ADR-1289: sculptural clouds from authored lobes -- the third Environment (ADR-1200 seam).
//
// A cloud mass is a list of soft ellipsoid lobes (centre, radius, per-axis scale, density, edge softness), authored in
// Blender against the shot's composition and exported as JSON (`*.lobes.json`, 03-hero-assets.md). The renderer
// (rendering::CloudRenderer) ray-marches the union of the lobes, eroded by noise, against the frame's linear depth, and
// shades it with a directional key (the moon), a two-colour ambient (sky above, ground below) and a warm horizon light
// that only reaches the lower, horizon-facing parts. It composites over the lit frame before the medium, so the
// distance haze and the bloom treat it like any other pixel.
//
// The block is static: no simulation, no parameters beyond the authored ones -- a still-image cloud, deliberately.

#include "core/error.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace avgen::scene {

struct CloudLobe {
    glm::vec3 centre{0.0f};
    float radius = 100.0f;
    glm::vec3 scale{1.0f};
    float density = 1.0f; // 0..1
    float soft = 0.35f;   // edge softness, a fraction of the radius
};

struct Clouds {
    bool enabled = true;
    std::string source;               // the lobes file as authored (relative to the scene file), or empty when inline
    std::vector<std::string> masses;  // which masses of the file to use; empty = all
    std::vector<CloudLobe> lobes;     // resolved
    float extinction = 0.006f;        // per metre at density 1
    int steps = 96;                   // view march steps (8..512)
    int lightSteps = 6;               // steps toward the key (0..32)
    float lightDistance = 400.0f;     // metres the shadow march reaches
    glm::vec3 lightDirection{0.0f, 0.3f, 1.0f}; // toward the key (normalised on load)
    glm::vec3 lightColor{0.55f, 0.65f, 1.0f};
    float lightIntensity = 1.0f;
    float anisotropy = 0.55f;         // Henyey-Greenstein g of the forward lobe (silver lining toward the key)
    glm::vec3 ambientTop{0.03f, 0.045f, 0.16f};
    glm::vec3 ambientBottom{0.012f, 0.012f, 0.05f};
    glm::vec3 warmColor{0.0f};        // the horizon's light on the cloud undersides and horizon-facing flanks
    glm::vec3 warmDirection{0.0f, 0.0f, 1.0f}; // toward the warm glow (normalised on load)
    float warmHeight = 1500.0f;       // metres above which the warm light no longer reaches
    float noiseScale = 0.004f;        // cycles per metre of the erosion noise
    float noiseAmount = 0.45f;        // how much of the density the noise can remove (0..1)
    glm::vec3 hazeColor{0.02f, 0.03f, 0.15f};
    float hazeDistance = 20000.0f;    // metres over which the cloud fades into the haze colour
    float powder = 0.6f;              // darkening of thin, unlit edges seen away from the key (0..1)
    std::uint32_t seed = 1;

    [[nodiscard]] bool active() const { return enabled && !lobes.empty(); }
    [[nodiscard]] Result<void> validate() const;
    // `base` is the scene file's directory, against which `source` is resolved.
    [[nodiscard]] static Result<Clouds> fromJson(const nlohmann::json& j, const std::filesystem::path& base);
    [[nodiscard]] nlohmann::json toJson() const;
};

// Reads a lobes file's masses (`{"masses": {name: [ {c, r, scale, density, soft}, ...]}}`), or a bare array of lobes.
[[nodiscard]] Result<std::vector<CloudLobe>> readCloudLobes(const nlohmann::json& j,
                                                            const std::vector<std::string>& masses);

inline constexpr std::size_t kMaxCloudLobes = 4096;

} // namespace avgen::scene
