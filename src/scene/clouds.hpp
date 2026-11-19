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
// ADR-2100 (Cloud Environment v2) extends the block without a second renderer:
//   * an unbounded analytic cloud SEA (`sea`): a height band whose density is a coverage map times a 3D shape noise, so
//     an ocean of cloud runs to the horizon with no edge and opens into breaks where the coverage is low;
//   * GLOW emitters (`glows`): coloured light sources (a settlement's neon) that light the cloud from inside and below,
//     occluded by the cloud between;
//   * motion and routing: a drift (wind, metres per second), a swell, and registered parameters
//     (`clouds/<leaf>`, `clouds/glow/<i>/...`) so palette moods and audio reach the clouds through ordinary routes;
//   * a cheaper march: reduced resolution with jittered steps, temporal accumulation and a depth-aware upsample, lobes
//     binned on a grid so a sample reads only the lobes near it, and steps that grow with distance.

#include "core/error.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace avgen::params {
class IParameter;
class ParameterSet;
} // namespace avgen::params

namespace avgen::scene {

struct CloudLobe {
    glm::vec3 centre{0.0f};
    float radius = 100.0f;
    glm::vec3 scale{1.0f};
    float density = 1.0f; // 0..1
    float soft = 0.35f;   // edge softness, a fraction of the radius
};

// ADR-2100: the analytic sea of cloud. Unbounded in x and z; between `base` and `top` in y.
struct CloudSea {
    bool enabled = false;
    float base = -300.0f;          // metres: the band's floor
    float top = 0.0f;              // metres: the band's ceiling (the swell raises it)
    float coverage = 0.6f;         // 0..1: the fraction of the sea that is cloud (low = breaks open)
    float coverageScale = 0.0003f; // cycles per metre of the coverage map (the size of the breaks)
    float density = 1.0f;          // 0..4: a multiplier on the sea's density
    // ADR-2101: a sea with relief. relief > 0 switches the band model for a TOP SURFACE: a height field (one swell of
    // `reliefScale` metres plus three octaves of billow noise, rounded tops and creased troughs) of +-`relief` metres
    // about `top`, bumped in 3D by `billow` metres of the shape noise (cauliflower), with the cloud solid below it down
    // to `base`. The key's light is then shadowed by the height field itself (crests lit, troughs dark).
    float relief = 0.0f;           // metres; 0 = the ADR-2100 band
    float reliefScale = 2000.0f;   // metres: the wavelength of the largest swell
    float billow = 0.0f;           // metres of 3D bumps on the surface
    float farFade = 0.0f;          // 0..1: the share of maxDistance over which the far sea fades out (0 = none)
    struct Break {
        glm::vec2 centre{0.0f};    // x, z
        float radius = 200.0f;     // metres, across the narrow axis
        float elongation = 1.0f;   // the long axis is radius * elongation, along x before the yaw
        float yaw = 0.0f;          // radians about +y
    };
    std::vector<Break> breaks;     // at most 2: holes through the sea, all the way down
};

// ADR-2100: a glow emitter: light from inside or below the cloud (a city's neon), falling off over `radius`.
struct CloudGlow {
    std::string name;              // optional; the parameters are keyed on the index
    glm::vec3 position{0.0f};
    glm::vec3 color{1.0f, 0.3f, 0.8f};
    float radius = 300.0f;         // metres at which the light has fallen to half; it is gone by 4x
    float intensity = 1.0f;
};

inline constexpr std::size_t kMaxCloudGlows = 16;

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

    // ADR-2100
    CloudSea sea;
    std::vector<CloudGlow> glows;
    float glowGain = 1.0f;            // a multiplier over every glow (the route target for a global pulse)
    float glowOcclusion = 1.0f;       // how strongly cloud between a glow and a sample hides it (0 = not at all)
    glm::vec3 drift{0.0f};            // metres per second the cloud's noise moves (wind)
    float density = 1.0f;             // a multiplier over all density (sea and lobes)
    float swell = 0.0f;               // 0..1: raises the sea's top and fattens the lobes (a bass target)
    float detailScale = 0.02f;        // cycles per metre of the edge-detail noise
    float detailAmount = 0.35f;       // how much of a thin edge the detail noise erodes (0..1)
    float resolution = 0.5f;          // the march's resolution, a fraction of the frame's (0.25..1)
    float temporal = 0.85f;           // the weight of the accumulated history (0 = none; up to 0.97)
    float minStep = 4.0f;             // metres: the shortest step
    float stepGrowth = 0.03f;         // a step is at least this fraction of its distance from the eye
    float maxDistance = 40000.0f;     // metres the march reaches

    [[nodiscard]] bool active() const { return enabled && (!lobes.empty() || sea.enabled); }
    [[nodiscard]] Result<void> validate() const;
    // `base` is the scene file's directory, against which `source` is resolved.
    [[nodiscard]] static Result<Clouds> fromJson(const nlohmann::json& j, const std::filesystem::path& base);
    [[nodiscard]] nlohmann::json toJson() const;
};

// Reads a lobes file's masses (`{"masses": {name: [ {c, r, scale, density, soft}, ...]}}`), or a bare array of lobes.
[[nodiscard]] Result<std::vector<CloudLobe>> readCloudLobes(const nlohmann::json& j,
                                                            const std::vector<std::string>& masses);

inline constexpr std::size_t kMaxCloudLobes = 4096;

// ADR-2100: the block's live knobs (`clouds/<leaf>`, `clouds/glow/<i>/intensity|color`). Each default is the block's
// own value, so registering them changes no picture.
struct CloudParameters {
    std::vector<params::IParameter*> all;
    std::size_t floats = 0;  // the leading float parameters (in kCloudFloats order)
    std::size_t vectors = 0; // then the vec3s (in kCloudVectors order)
                             // then two per glow: intensity, colour
};
[[nodiscard]] CloudParameters registerCloudParameters(params::ParameterSet& params, const Clouds& rest,
                                                      const std::string& prefix);
void applyCloudParameters(const CloudParameters& p, const Clouds& rest, Clouds& live);

} // namespace avgen::scene
