#pragma once

// ADR-945: which glowing patches of an ecology become lights this frame, and the pool of light each
// one throws on the ground.
//
// A glowing scatter layer is reduced at build time to one soft emitter per occupied cell of a coarse
// grid (ADR-053, `world::aggregateGlow`); a valley has thousands. About two hundred can be real
// lights. Until ADR-945 they were taken nearest-first whatever their power, so the faint glow of the
// trees, ferns and grass beside the camera filled the budget and a wide shot's fungi 100-250 m out
// cast nothing; and each light sat at half its layer's height (0.14 m for a 0.28 m mushroom), where
// a 1/d^2 light makes a hot speck half a metre across rather than a pool.
//
// Both halves are pure functions of the frame -- the camera, the clusters and the parameters' values
// -- with no memory of the last frame, so a seek lands on exactly the lights play would have.

#include "world/ecology.hpp"

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace avgen::scene {

// Where the light for one glowing patch stands and how far it reaches.
struct GlowPool {
    glm::vec3 position{0.0f}; // the light: above the patch's ground point
    float range = 0.0f;       // the light's window radius (the renderer's `range`)
    float radius = 0.0f;      // the pool's radius on the ground the light was placed for
};

// A patch's light, placed so it lights a pool of about `reach` metres' radius on the ground round it.
//
// A point light at height h over flat ground lights it as h / (h^2 + r^2)^1.5: the pool's size IS the
// light's height, and its range only windows the tail. So the light stands at half the pool's radius
// over the patch's ground point (the irradiance at the pool's edge is then 9% of its centre), and its
// window reaches twice the radius, so the edge is soft rather than cut. The pool is never smaller than
// the patch's own spread, nor lower than the glowing organ (`lift`, the height the cluster was raised
// to: half the layer's height), so a tall glowing tree still lights from its crown.
//
// The light's intensity is not changed by where it stands: the same power spread wider. A larger
// reach makes a larger and fainter pool; `scene/ecologyLight` is the brightness.
[[nodiscard]] GlowPool glowPool(const world::GlowCluster& cluster, float lift, float reach);

// One glowing patch that could be a light this frame.
struct EcologyLightCandidate {
    glm::vec3 position{0.0f}; // the light's position (GlowPool::position)
    float range = 0.0f;       // its window radius
    float power = 0.0f;       // what it emits at the artist's settings, before the beat moves it
};

// The camera the choice is made for.
struct EcologyLightView {
    glm::vec3 eye{0.0f};
    std::array<glm::vec4, 6> planes{}; // world-space frustum planes, inward, normalised
    float farthest = 120.0f;           // no pool further than this from the eye (metres)
};

struct EcologyLightChoice {
    std::uint32_t index = 0; // into the candidates
    float score = 0.0f;
    // 0..1: fades a light in as it approaches the budget's cut and the distance limit, so a light
    // crossing either as the camera moves arrives from nothing rather than popping.
    float weight = 1.0f;
};

// A candidate's contribution to the picture: its power over its squared distance from the eye (the
// light a pool puts into the frame is its power spread over a patch of ground whose size on screen
// falls as 1/d^2), zero when its whole reach is outside the frustum or beyond `farthest`. The
// distance is never taken as less than the light's own range, so a light the camera stands inside
// does not score infinitely.
[[nodiscard]] float ecologyLightScore(const EcologyLightCandidate& c, const EcologyLightView& view);

// The `budget` candidates that put the most light into the picture, highest first, ties broken by
// candidate order (which the caller keeps stable). Candidates scoring zero are never chosen.
[[nodiscard]] std::vector<EcologyLightChoice> chooseEcologyLights(std::span<const EcologyLightCandidate> candidates,
                                                                  const EcologyLightView& view, std::size_t budget);

// How strongly a scatter layer glows per unit of its own area, which is what `scene/glow-pools/faintest`
// compares against: the emission intensity times the colour's peak, times what its sparsity leaves lit,
// times the layer's gain. Independent of how much of the layer there is.
[[nodiscard]] float layerGlowStrength(const world::ScatterLayer& layer, float gain);

} // namespace avgen::scene
