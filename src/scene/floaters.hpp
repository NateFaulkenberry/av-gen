#pragma once

// Things that float and drift (ADR-091 §13). Lily pads, leaves, petals, blossoms, glowing
// organisms, debris -- anything that sits on a water surface, is carried by the current, and is not
// a particle. Particles are the wrong tool for these: they are billboards with a lifetime, and a
// lily pad is a mesh with an orientation that has to be there the whole shot.
//
// It is not a new instancing system either. A floating layer is an ordinary
// `scene::ProceduralGeometry` -- an imported mesh, a material, GPU culling, LOD, the wind
// deformer -- and all this adds is where its instances are *this second*. `evaluateFloaters` is a
// pure function of the water bodies, the spec and the timeline clock:
//
//     placement(i, t) = body.pointAt(wrap(s_i + v_i * t)) + lateral_i, yaw_i + spin_i * t, ...
//
// There is no integration and no state, which is the whole reason it is written this way: a render
// that starts at t = 40 s has to put every leaf exactly where a render that started at 0 would have
// put it by then, and an accumulating drift cannot promise that. The same property makes a
// floating layer free to scrub, seek and render out of order.
//
// Nothing here knows about the GPU, an entity or a frame. The Composition turns the results into
// InstanceRecords once a frame; see Composition::updateFloaters.

#include "core/error.hpp"
#include "world/water.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace avgen::scene {

// One species of floating thing, as an artist authors it.
struct FloatSpec {
    bool enabled = true;
    // The terrain node whose water this floats on. Required: a floating layer with no water is the
    // kind of thing that silently places nothing, and this engine has done that seven times.
    std::string water;
    // The named WaterBody inside it, or empty for every body the world has -- which is what a
    // layer of leaves wants, since a world's ponds should have leaves on them too.
    std::string body;

    int count = 60;
    std::uint32_t seed = 91;
    float sizeMin = 0.7f;
    float sizeMax = 1.4f;

    // Drift. `driftScale` multiplies the local flow speed the body reports, and `driftSpread` is
    // how much that varies per instance -- the single most important number here, because a set of
    // leaves all travelling at exactly the water's speed reads as a conveyor belt and one where
    // they differ by a quarter reads as a river. The flow already varies across the channel
    // (world::WaterBody::shearProfile), so a leaf near the bank falls behind one in midstream for
    // free; this is the variation on top of that.
    float driftScale = 1.0f;
    float driftSpread = 0.35f;

    // Where across the channel they sit: 0 = all on the centreline, 1 = out to the nominal bank.
    // `margin` then pulls them back off the bank by that fraction of a half-width, which is what
    // keeps a pad from hanging over dry ground where the channel narrows.
    float lateral = 0.8f;
    float margin = 0.22f;
    // How much they clump. 0 spreads them evenly along the course; 1 gathers them into `clusters`
    // knots with bare water between. Real still water collects leaves in eddies and against the
    // inside of bends, and an even spread is the tell that a scatter was uniform.
    float clustering = 0.55f;
    int clusters = 8;

    float spin = 0.05f;        // radians per second of yaw, sign and rate varying per instance
    float bob = 0.04f;         // metres of vertical rise and fall
    float bobRate = 0.32f;     // cycles per second of the bob
    float tilt = 0.10f;        // radians of static lean off level, per instance
    float sink = 0.01f;        // metres below the surface the origin sits

    [[nodiscard]] Result<void> validate() const;
    [[nodiscard]] std::uint64_t structuralHash() const; // everything except the clock
    [[nodiscard]] nlohmann::json toJson() const;
    static Result<FloatSpec> fromJson(const nlohmann::json& j);
};

// One floating thing at one instant.
struct Floater {
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    float scale = 1.0f;
    float random = 0.0f;   // 0..1, stable per instance: colour and emissive variation
    float speed = 0.0f;    // metres per second it is travelling at, for anything that wants it
};

// Places `spec.count` floaters on `bodies` at timeline second `time`. Pure and deterministic: the
// same arguments always give the same vector, and `time` is the only one that moves.
void evaluateFloaters(const world::WaterBodySet& bodies, const FloatSpec& spec, float time,
                      std::vector<Floater>& out);

} // namespace avgen::scene
