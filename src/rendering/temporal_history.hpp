#pragma once

// The reusable temporal media system (ADR-400, brief §8): a bounded ring of previous frames that
// temporal effects read.
//
// **It is a cache, not an accumulator.** Nothing here integrates. A channel's ring holds the last
// K frames of a signal the renderer already produces, and K is declared by whichever effects are
// live. That is what makes the whole family scrub-safe: a cache can be rebuilt by re-rendering the
// frames that filled it, where an accumulator cannot be rebuilt at all. See ADR-400 for why this
// is the central constraint rather than an implementation detail.
//
// **Memory is the design problem, not ALU** (§8: "do not blindly retain expensive full-resolution
// buffers"). Three rules hold the budget:
//
//   1. **Depth is per-channel, not global.** "32 frames of history" never means 32 frames of every
//      channel. Reprojection needs one frame of depth; an advection chain composes into a single
//      accumulator rather than K stored motion fields. Only colour genuinely wants depth.
//   2. **Reduced resolution and a cheaper format.** Colour is RG11B10Ufloat at `resolutionScale`,
//      not RGBA16Float at full size: 4 bytes a texel instead of 8, over a quarter of the texels at
//      the 0.5 default. Eight times less, on a signal about to be blurred, smeared or advected.
//   3. **One texture array per channel**, not K textures -- one allocation, one binding, per-layer
//      render views. The shadow atlas (`shadow_renderer.cpp`) is the precedent.
//
//  Naively, 32 frames of all channels at 1080p is 1.86 GB. Under these rules the Realtime default
//  is about 25 MB against a scene-target baseline of about 91 MB. `bytes()` reports the real
//  figure rather than the estimate, so the UI can show what a setting costs.
//
// **What is deliberately NOT here: per-object history.** §8 asks for "selective / object-specific
// history", and the cheap reading of that is a storage partition per object. It is not needed: the
// ADR-035 identifier target already names the object at every pixel, so §12's object-level
// datamosh selects at *read* time by comparing ids and moshing only the chosen ones. Storing
// per-object history would spend memory doing what an existing target does for free.

#include "core/error.hpp"

#include <webgpu/webgpu_cpp.h>

#include <array>
#include <cstdint>
#include <memory>

namespace avgen::gpu {
class Context;
class FrameTimeline;
class ShaderLibrary;
} // namespace avgen::gpu

namespace avgen::rendering {

// The signals a ring can hold. Colour is the only one an effect can rely on being present; the
// rest are opt-in and cost nothing when their depth is zero.
//
// Normal is absent on purpose. Nothing in the temporal or digital-image half reads it, and §8's
// list is a menu rather than a requirement -- a channel with no reader is memory spent to satisfy
// a spec sentence (ADR-385: a stated reason is not evidence).
enum class TemporalChannel : std::uint8_t {
    Colour = 0,    // RG11B10Ufloat  the scene's HDR radiance before the post chain
    Motion,        // RG16Float      the ADR-035 velocity target, in UV units
    Count,
};

constexpr std::uint32_t kTemporalChannelCount = static_cast<std::uint32_t>(TemporalChannel::Count);

// The hard ceiling the brief names (§8: 1-32 frames). It is also the ceiling on a seek warm-up,
// because a warm-up re-renders exactly this many frames.
constexpr std::uint32_t kMaxTemporalFrames = 32;

[[nodiscard]] constexpr const char* temporalChannelName(TemporalChannel c) {
    switch (c) {
    case TemporalChannel::Colour: return "colour";
    case TemporalChannel::Motion: return "motion";
    case TemporalChannel::Count: break;
    }
    return "unknown";
}

// `rg11b10Renderable` is `gpu::Context::capabilities().rg11b10Renderable`. Drawing into RG11B10Ufloat is
// an OPTIONAL WebGPU feature, not a given -- sampling it always works, rendering to it does not,
// and Dawn rejects the texture at creation rather than at draw. Found by a validation error on the
// first GPU run, which is the honest way to find it; assuming the format was renderable because it
// is samplable would have been a memory claim the hardware never agreed to.
//
// The fallback costs twice the bytes and changes nothing anybody can see, so it is a cost that
// degrades rather than a feature that disappears.
[[nodiscard]] constexpr wgpu::TextureFormat temporalChannelFormat(TemporalChannel c, bool rg11b10Renderable) {
    switch (c) {
    // Not RGBA16Float where the adapter allows it: half the bytes, no alpha (nothing here has one),
    // and 11-bit mantissas are ample for a signal that exists to be blurred. The single largest
    // memory saving in the design.
    case TemporalChannel::Colour:
        return rg11b10Renderable ? wgpu::TextureFormat::RG11B10Ufloat : wgpu::TextureFormat::RGBA16Float;
    // Matches SceneRenderer::kVelocityFormat exactly, so a capture is a copy rather than a
    // conversion. Measured adequate: the half-float ulp in UV units at 1920 px is 0.117 px at
    // 150 px of motion, so an eight-frame advection chain drifts under a pixel (ADR-400 §45).
    case TemporalChannel::Motion: return wgpu::TextureFormat::RG16Float;
    case TemporalChannel::Count: break;
    }
    return wgpu::TextureFormat::Undefined;
}

// How many frames of each channel to keep. Zero disables a channel outright -- no allocation, no
// capture pass attachment, no cost.
struct TemporalHistoryConfig {
    std::array<std::uint32_t, kTemporalChannelCount> frames{};
    // Fraction of the scene resolution the ring is stored at. Driven by the quality tier beside
    // `aoResolutionScale` / `volumeResolutionScale`, which are the existing precedents.
    float resolutionScale = 0.5f;

    [[nodiscard]] bool anyEnabled() const;
    // The longest history any channel asks for: what a seek warm-up has to re-render.
    [[nodiscard]] std::uint32_t maxFrames() const;
    [[nodiscard]] bool operator==(const TemporalHistoryConfig&) const = default;
};

// What the artist is told (ADR-400). `framesValid < framesNeeded` means the picture on screen is
// not yet the picture a render would produce.
//
// This is the disclosure the whole design turns on, so it is a value the renderer reports rather
// than a flag a panel infers. A guarantee that is not visible is a guarantee nobody can rely on
// (ADR-091).
struct TemporalHistoryState {
    std::uint32_t framesValid = 0;
    std::uint32_t framesNeeded = 0;
    // True when the history cannot fill by waiting -- the effect asks for more frames than the
    // ring was built for. Waiting will not fix it, so the badge must say something different.
    bool stalled = false;

    [[nodiscard]] bool complete() const { return framesValid >= framesNeeded; }
    [[nodiscard]] bool settling() const { return !complete() && !stalled; }
};

class TemporalHistory {
public:
    TemporalHistory(gpu::Context& context, gpu::ShaderLibrary& shaders);
    ~TemporalHistory();
    TemporalHistory(const TemporalHistory&) = delete;
    TemporalHistory& operator=(const TemporalHistory&) = delete;

    [[nodiscard]] Result<void> init();
    [[nodiscard]] Result<void> reload(); // hot reload of temporal.wgsl; keeps old pipelines on failure

    // Allocates or reuses the rings for a scene resolution and a requested depth per channel.
    // Reallocates only when the resolved size, format set or depth actually changes, so calling it
    // every frame is free.
    [[nodiscard]] Result<void> configure(std::uint32_t sceneWidth, std::uint32_t sceneHeight,
                                         const TemporalHistoryConfig& config);

    // Frame bookkeeping. This is the half that determinism lives or dies on, and it is copied
    // deliberately from `AoRenderer::update` rather than reinvented -- see the .cpp for the two
    // cases and why they need opposite treatment.
    void beginFrame(std::uint64_t frameIndex);

    // Encodes the capture: one pass that downsamples the frame's signals into the write layer.
    // Views may be null for a channel whose depth is zero. Does nothing when disabled.
    void encodeCapture(wgpu::CommandEncoder& encoder, const wgpu::TextureView& sceneHdr,
                       const wgpu::TextureView& velocity);

    // Drops the history. Call on any discontinuity: a seek, a cut, a resize, a scene swap.
    // `SceneRenderer::resetTemporalHistory()` is the one hook that does this for every temporal
    // consumer at once; do not add a second.
    void reset();

    [[nodiscard]] const TemporalHistoryState& state() const { return state_; }
    // Reports what the live effects need, so `state()` can say whether they have it.
    void setFramesNeeded(std::uint32_t frames);

    [[nodiscard]] bool active() const;
    // The 2D-array view an effect samples. Always valid once init() succeeded: a disabled channel
    // returns a 1x1x1 placeholder, so a bind group is never left with a null.
    [[nodiscard]] const wgpu::TextureView& arrayView(TemporalChannel channel) const;
    [[nodiscard]] std::uint32_t frames(TemporalChannel channel) const;
    // The layer the NEXT capture will write. An effect reading `tapsBack` frames into the past
    // wants layer `(writeLayer + frames - tapsBack) % frames`; the shader does this itself from
    // the two values, so there is one implementation of the mapping rather than two.
    [[nodiscard]] std::uint32_t writeLayer(TemporalChannel channel) const;
    [[nodiscard]] std::uint32_t width() const;
    [[nodiscard]] std::uint32_t height() const;
    // Bytes actually allocated across every ring. The UI shows this, so it is measured rather than
    // estimated from the config.
    [[nodiscard]] std::uint64_t bytes() const;

    void setTimeline(gpu::FrameTimeline* timeline);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    TemporalHistoryState state_;
};

} // namespace avgen::rendering
