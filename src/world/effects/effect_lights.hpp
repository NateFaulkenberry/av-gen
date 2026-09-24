#pragma once

// LIGHTMOD's transient pool (Effect Library Wave 1, package 1.5): real lights an effect asks for.
//
// A Glow's spill is the first client: a point light at the owner's centre in the glow's colour, so a
// glowing saucer also lights the ground under it. Later clients (Aura spill, a Shockwave flash, a
// Portal spill) request through the same pool.
//
// **Why a reserved budget and not "whatever is left".** The clustered path takes 256 lights in total
// (`rendering::kMaxSceneLights`). Ecology glows (ADR-053) already take everything up to
// `kMaxEcologyLights` that authored lights leave, so a second allocator asking for "the rest" would
// fight it frame by frame and win or lose depending on where the camera stood. Instead the pool owns
// a fixed `kEffectLightBudget` slots and the ecology cap is lowered by the same number
// (`scene/composition.cpp`). A Glow's spill is then lit whatever the valley is growing, and the
// ecology gives up exactly this many of its nearest-camera glows -- a trade stated in one constant.
//
// **Unshadowed.** Pool lights cast no shadow. A shadowed pool light would take one of the eight
// shadow views (`shadow_math.hpp`) and is a later quality-flagged addition, not this package.
//
// **Honest drops.** Requests are ranked by the requesting type's priority, then by the light's
// projected intensity at the camera, then by stack order (so the ranking is a pure function of the
// frame). The first `kEffectLightBudget` are kept; every loser is reported by the builder that asked
// -- `Partial` when the effect still draws without its light, with a reason naming the budget.
//
// Plain data on `scene::Scene` (inside `world::EntityFxFrame::lights`): the renderer appends these
// after the authored and ecology lights, before the froxel build, and never evaluates an effect.

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace avgen::world {

enum class EffectStatus : std::uint8_t;

// The pool's size. Subtracted from the ecology light cap in `scene/composition.cpp`.
inline constexpr std::size_t kEffectLightBudget = 16;

// One pool light, already resolved: a point light in world space. Unshadowed by construction.
struct EffectLight {
    glm::vec3 position{0.0f};
    float intensity = 0.0f;       // candela, like an authored point light
    glm::vec3 color{1.0f};        // linear RGB
    float range = 1.0f;           // metres; the light's influence ends here (clustered culling)
    float volumetric = 0.0f;      // 0..1: how much it lights the fog (`PunctualLight::volumetricStrength`)
};

// What the renderer reads.
struct EffectLightFrame {
    std::array<EffectLight, kEffectLightBudget> lights{};
    std::uint32_t count = 0;
    std::uint32_t dropped = 0;    // requests that lost their light this frame
};

// One request, gathered by a builder while it walks its instances.
struct EffectLightRequest {
    std::uint32_t instance = 0;   // index into the effect list (for the status and reason)
    int priority = 0;             // the requesting type's schema priority: lower wins
    std::uint32_t order = 0;      // stack order position: the final, deterministic tie-break
    EffectLight light;
};

// Ranks `requests` in place and writes the winners into `out` (which it clears first). Every loser
// gets `Partial` in `status` -- the effect still draws, its light did not fit -- and a reason in
// `reasons`, unless its status is already worse than Drawn. `cameraPosition` is what "projected
// intensity" is projected to. Allocation-free: it sorts the caller's span.
void resolveEffectLights(std::span<EffectLightRequest> requests, const glm::vec3& cameraPosition,
                         EffectLightFrame& out, std::span<EffectStatus> status,
                         std::span<std::string> reasons);

// How bright a request looks from the camera: intensity over the squared distance, floored at the
// light's own range so a light the camera stands inside does not rank as infinitely bright.
[[nodiscard]] float projectedIntensity(const EffectLight& light, const glm::vec3& cameraPosition);

} // namespace avgen::world
