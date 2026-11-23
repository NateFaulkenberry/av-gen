#pragma once

// ADR-2124: a Shockwave that travels THROUGH the world (Neon Dreams, owner msg 195: "the wave should pulsate through
// the entire scene / geometry / characters").
//
// A Shockwave whose `travel` is "Through the world" resolves each live front to one `PulseFront` here as well as to
// its DF proxy (a world shell, `DistortionShape::Shell`). The renderer copies the fronts into the frame block
// (`FrameUniforms::pulses`), and four consumers read them, each per vertex or per sample from the front's centre and
// radius alone -- no bounds test, no per-object list:
//   * the vertex stages (`vs_entity`, `vs_skinned`, `procVertex`): the front PUSHES what it passes -- out from the
//     centre and up, then a smaller recoil behind it (`pulseOffset`, shaders/common.wgsl);
//   * the surface shading (`shadeSurface`): a band of emitted light on every surface it crosses, and already-emissive
//     surfaces (windows, strips) flare (`pulseFlash`);
//   * the cloud march: the cloud brightens and thickens in the band (`pulseCloud`);
//   * DF: the air bends where the front meets the scene and along its silhouette (fs_offset_ext, shape 6).
// Like every TRIGGER consumer a front is a pure function of `age = t - t0` (ADR-091): a seek draws the fronts a play
// draws. Nothing here keeps state.

#include "world/effects/effect_instance.hpp"
#include "world/effects/effect_timing.hpp"

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace avgen::world {

// The most fronts the frame carries (all instances together). A kick every beat at 171 BPM with a 2.5 s life is
// seven in the air; twelve leaves room for two sources.
inline constexpr std::size_t kMaxPulseFronts = 12;

// One front as the shaders read it (four vec4s; shaders/common.wgsl documents the lanes as `pulses[4i..4i+3]`).
struct PulseFront {
    glm::vec4 centreRadius{0.0f}; // xyz = the centre (world), w = the radius now (m)
    glm::vec4 motion{0.0f};       // x = the radius a frame earlier (1/30 s), y = the band's half-width (m),
                                  // z = the push now (m, decay and envelope folded in), w = the push a frame earlier
    glm::vec4 flash{0.0f};        // rgb = the surface band's radiance (HDR, decay and envelope folded in), w = the
                                  // shift cap (px at 1080p, ADR-2126): a pushed point never moves further on screen
    glm::vec4 shape{0.0f};        // x = the share of the push that is UP (0 radial .. 1 vertical), y = the cloud
                                  // ripple (0..), z = how much brighter an emissive surface flares (x its own), w = calm
                                  // near the lens (m, ADR-2126): no push within it, full from twice it (0 off)
};
static_assert(sizeof(PulseFront) == 64);

struct PulseFrame {
    std::uint32_t count = 0;
    // ADR-2128: the live instances' largest `sourceClear` (m): every consumer weighs a point by
    // smoothstep(clear / 2, clear, its distance from the front's centre); 0 off. One value for the frame (the
    // block's pulseInfo.y): scenes send their fronts from one source.
    float sourceClear = 0.0f;
    std::array<PulseFront, kMaxPulseFronts> fronts{};
};

// The seconds a frame earlier the `motion` lanes are taken at (the velocity target's previous position). A constant
// so the record stays a pure function of the second; 1/30 is the film's frame.
inline constexpr double kPulsePrevSeconds = 1.0 / 30.0;

// Every enabled Shockwave in the "Through the world" mode, in evaluation order, while its envelope is open. Unused
// slots are zeroed, so the block is a pure function of the live fronts.
void buildPulseFrame(std::span<const EffectInstance> effects, const EffectContext& ctx, PulseFrame& out,
                     std::span<const std::uint32_t> order);

// The Shockwave's own resolution (kinds/shockwave_effect.cpp): the live fronts of one instance, newest first.
std::size_t shockwavePulseFronts(const EffectInstance& instance, const EffectContext& ctx, float envelope,
                                 std::span<PulseFront> out);

// ADR-2128: an instance's source clearance (m; 0 off, and 0 for a ring-mode Shockwave).
float shockwaveSourceClear(const EffectInstance& instance);

} // namespace avgen::world
