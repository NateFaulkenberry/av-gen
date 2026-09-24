#pragma once

// DF: the distortion framework's CPU half (Effect Library Wave 1, roadmap 1.7).
//
// **What DF is.** One shared screen-space refraction system that every distortion type is a
// PRODUCER for. None of them owns a render pass. A producer resolves to one or more PROXIES -- a
// world-space shape carrying a displacement field -- and the renderer (`rendering/
// distortion_renderer.*`, `shaders/distortion.wgsl`) draws every proxy of the frame into one
// offset target, copies the HDR image once, and resolves the whole lot in one scissored pass. So a
// Space Warp, a Shockwave and a Heat Shimmer on one frame cost one copy and one resolve between them,
// and overlapping fields superpose (the offsets add: thin-lens superposition).
//
// **What this file owns.** The GPU-ready record (`DistortionProxy`), the frame block the renderer
// reads (`DistortionFrame`, on `scene::Scene::distortion`), and the one builder
// (`buildDistortionFrame`) that walks the effect list in evaluation order, gates each instance on its
// activation and timing, asks its type for proxies, and writes every instance's status. The
// renderer reads the block and never evaluates an effect.
//
// **Adding a producer** is one file in `kinds/` plus one row in `distortionProducers()` (in
// distortion_frame.cpp): a function that turns a live instance into proxies. The builder handles
// activation, timing, capacity, priority and status for every producer the same way, which is the
// part that must not be re-implemented per type. `test_distortion_frame.cpp` holds the producer
// table against the registry: a type whose bucket is `Distortion` with no producer is a named
// failure, not an instance that silently never draws.
//
// **Authoring is in WORLD units.** A proxy's displacement is metres at a world point, and the shader
// turns it into a screen offset as `proj(p + d) - proj(p)` -- the lesson `shaders/water.wgsl` learned:
// an offset added straight to a UV means something different at 2 m and at 50 m. Nothing in this
// file knows the camera, so a frame block is the same whatever looks at it.

#include "world/effects/effect_instance.hpp"
#include "world/effects/effect_timing.hpp"

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace avgen::world {

// The proxy budget. One storage buffer of this many records; the 65th live proxy is `Dropped` with a
// reason, lowest evaluation priority first (the order `effectOrder_` already sorts by).
inline constexpr std::size_t kMaxDistortionProxies = 64;

// The proxy's rasterised SHAPE: which geometry the offset pass draws to cover the field. Only the
// ellipsoid is drawn today; the others are reserved so a Shockwave disc, a Portal quad and a
// camera-wide field slot in without a layout change (the shader dispatches on this value).
enum class DistortionShape : std::uint8_t {
    Ellipsoid = 0,  // a closed ellipsoid: Space Warp, Bubble, Energy Shield
    Disc = 1,       // reserved: a flat ring in a plane (Shockwave, Ripple)
    Quad = 2,       // reserved: a camera-facing or placed quad (Portal, Reality Tear)
    FullScreen = 3, // reserved: a screen-covering triangle (Radial Distortion, Heat Shimmer)
};

// The displacement FIELD evaluated inside the shape. One per family of look; the proxy's numbered
// lanes mean what the field says they mean. Only the warp field exists today.
enum class DistortionField : std::uint8_t {
    Warp = 0, // radial pull + bow along motion + swirl + curl turbulence (Space Warp)
};

// One proxy, exactly as `shaders/distortion.wgsl` reads it (`DfProxy`, 144 bytes, nine vec4s).
// Plain data; `packDistortionProxy`-style helpers live with each producer.
struct DistortionProxy {
    // xyz = world centre; w = EXCLUSION RADIUS in metres. The lens plane sits this far behind the
    // centre, and nothing nearer than it is bent or sampled (see `shaders/distortion.wgsl`, the
    // self-exclusion rule): 0 for a warp at a point, the owner's bounding radius for an entity, so
    // the owner stays crisp while what is behind it bends.
    glm::vec4 centre{0.0f};
    // xyz = the ellipsoid's three semi-axes as WORLD vectors (direction times length), mutually
    // orthogonal. w lanes: shape, field, and the depth band (m) over which the bend fades in behind
    // the lens plane, which is what keeps a plane cutting through the field free of a hard seam.
    glm::vec4 axis0{0.0f};
    glm::vec4 axis1{0.0f};
    glm::vec4 axis2{0.0f};
    // x radial, y bow, z swirl weights; w = the peak displacement in METRES (strength and the
    // lifecycle envelope folded in).
    glm::vec4 terms{0.0f};
    // xyz = unit direction of motion (world), w = how much of the velocity terms apply (0..1).
    glm::vec4 motion{0.0f};
    // x = falloff exponent, y = edge softness (0..1 of the radius), z = turbulence amount (0..1 of
    // the displacement), w = turbulence frequency (cycles across the radius).
    glm::vec4 shape{0.0f};
    // x = turbulence phase (seconds of animation, speed folded in), y = chroma spread (0..1),
    // z = rim width (0..1 of the radius), w = seed.
    glm::vec4 noise{0.0f};
    // rgb = rim radiance (HDR, envelope folded in); w = inner radius (0..1): the field is zero inside
    // it, so an entity's own silhouette is not where the bend is strongest.
    glm::vec4 rim{0.0f};
};
static_assert(sizeof(DistortionProxy) == 144, "DfProxy in shaders/distortion.wgsl is nine vec4s");

// What the renderer reads, on `scene::Scene::distortion`. `count == 0` is the gate: no targets are
// touched, no copy is made and no pass is encoded, so the frame is byte-identical to one rendered by
// a build without DF.
struct DistortionFrame {
    std::uint32_t count = 0;
    std::uint32_t dropped = 0; // live proxies that did not fit the budget this frame
    std::array<DistortionProxy, kMaxDistortionProxies> proxies{};
};

// ---- producers ---------------------------------------------------------------------------------

// Turns one LIVE instance (activation open, envelope > 0) into proxies. `envelope` is the timing
// envelope already resolved; `out` is where to write; returns how many were written (0 = nothing to
// draw this frame -- a degenerate size, an owner with no view). Must be a pure function of its
// arguments: no state, no clock, so a scrub and a play land the same frame (ADR-091).
using DistortionProduceFn = std::size_t (*)(const EffectInstance& instance, const EffectContext& ctx,
                                            float envelope, std::span<DistortionProxy> out);

struct DistortionProducer {
    EffectKind kind{};
    DistortionProduceFn produce = nullptr;
    std::size_t maxProxies = 1; // the most one instance may write (sizes the scratch in `records`)
};

// Every distortion producer the engine has, one row per type. See the header comment.
[[nodiscard]] std::span<const DistortionProducer> distortionProducers();
[[nodiscard]] const DistortionProducer* distortionProducer(EffectKind kind);

// The activation and timing gate every DF type shares: the envelope of `instance` at `ctx.seconds`,
// or 0 when it is disabled, outside its activation window, or faded out. An entity owner is the
// subject a `HeroFocus` activation fires for; a World owner follows whatever the cut holds.
[[nodiscard]] float distortionEnvelope(const EffectInstance& instance, const EffectContext& ctx);

// ADR-703's `resolve.records` hook for every DF type: how many proxies ONE instance contributes in
// `ctx`, through exactly the code `buildDistortionFrame` runs (envelope, then the producer).
[[nodiscard]] std::size_t distortionRecords(const EffectInstance& instance, const EffectContext& ctx);

// The builder, in the evaluator's convention. Walks `order` (the evaluation order: stage, priority,
// stack position), fills `out`, and writes Disabled / Dormant / Drawn / Dropped for every instance
// whose type is a DF producer -- with a reason sentence for Dropped. Allocates nothing once
// `reasons` strings have capacity.
void buildDistortionFrame(std::span<const EffectInstance> effects, const EffectContext& ctx,
                          DistortionFrame& out, std::span<const std::uint32_t> order,
                          std::span<EffectStatus> status, std::span<std::string> reasons);

// ---- shared geometry helpers for producers ------------------------------------------------------

// An orthonormal basis whose first axis is `forward` (unit). Deterministic for any input, including
// the poles, so a proxy stretched along a velocity never flips its secondary axes frame to frame.
void distortionBasis(glm::vec3 forward, glm::vec3& a0, glm::vec3& a1, glm::vec3& a2);

} // namespace avgen::world
