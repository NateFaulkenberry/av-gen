#pragma once

// The sky's star field as an effect (Effect Library Wave 2, catalog-particles.md "Stars").
//
// **What it replaces.** The background pass has always drawn a fixed field on the stylised analytic
// sky: one hashed cell in 83 holds a star, every star the same blue-white, the same brightness, no
// twinkle (shaders/skybox.wgsl). A Stars instance takes those constants over -- density, a power-law
// magnitude spread, a colour-temperature spread, scintillation that is stronger toward the horizon,
// an optional galactic band -- and draws on ANY sky, map or analytic, because adding one is the
// author asking for stars. With no live instance the pass draws exactly what it drew before (the
// `on` flag is the gate; the frame is byte-identical).
//
// **One per sky.** A sky has one star field. The first live instance in evaluation order draws; any
// other live one is `Dropped` with a reason naming the one that drew, rather than being blended into
// a field nobody authored.
//
// **Deterministic.** Everything is a hash of the star's cell plus the transport second. Twinkle
// frequencies are snapped to whole cycles per `kTwinklePeriod`, so the second the shader is handed
// can wrap at that period without a seam and stays precise in f32 on a long timeline.

#include "world/effects/effect_instance.hpp"
#include "world/effects/effect_timing.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace avgen::world {

inline constexpr double kTwinklePeriod = 256.0; // seconds; see the header

// Plain, GPU-ready: the renderer packs it into three frame vectors and evaluates nothing.
struct StarField {
    bool on = false;            // false: the background pass's own fixed field, exactly as before
    float density = 0.015f;     // fraction of sky cells that hold a star
    float brightness = 2.0f;    // radiance of a magnitude-0 star
    float magnitudeSlope = 2.5f; // 1 = every star equally bright; higher = few bright, many faint
    float colorSpread = 0.5f;   // 0 = all blue-white, 1 = blue-white to orange
    float twinkle = 0.25f;      // depth of scintillation at the horizon (0..1)
    float twinkleRate = 3.0f;   // radians per second, per star within +-30%
    float horizonFade = 0.35f;  // elevation (sine) by which stars reach full brightness
    float band = 0.0f;          // strength of the galactic band (0 = none)
    float bandTilt = 0.6f;      // radians: the band's great circle tilted from the horizon
    float daylight = 1.0f;      // how far a bright sky behind a star hides it (0 = never)
    float envelope = 1.0f;      // the instance's lifecycle envelope, folded into brightness
    float seconds = 0.0f;       // the transport second, wrapped at kTwinklePeriod
};

// The Sky-stage builder. Walks `order`, writes every Stars instance's status and reason.
void buildStarField(std::span<const EffectInstance> effects, const EffectContext& ctx, StarField& out,
                    std::span<const std::uint32_t> order, std::span<EffectStatus> status,
                    std::span<std::string> reasons);

// The conformance probe's `resolve.records`: 1 when one live instance would draw.
[[nodiscard]] std::size_t starFieldRecords(const EffectInstance& instance, const EffectContext& ctx);

} // namespace avgen::world
