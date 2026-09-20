#pragma once

// The temporal effect passes (ADR-410, brief §9-§17) over the ring in `temporal_history.hpp`.
//
// Runs between the scene pass and the post chain: it captures the clean scene radiance into the
// ring and then returns an image for post to grade. Capturing *before* post and *before* its own
// output is the determinism decision, not a convenience -- see the header of `shaders/temporal.wgsl`.
//
// **What an effect may not do.** Read its own previous output. That is an IIR filter, K is
// infinite, and no warm-up can rebuild it. Every effect here reads the ring, which holds clean
// frames, so every effect is an FIR filter a warm-up can reproduce exactly.

#include "core/error.hpp"
#include "gpu/transient_pool.hpp"
#include "rendering/temporal_history.hpp"
#include "scene/temporal_settings.hpp"

#include <cstdint>
#include <memory>
#include <webgpu/webgpu_cpp.h>

namespace avgen::gpu {
class Context;
class FrameTimeline;
class ShaderLibrary;
} // namespace avgen::gpu

namespace avgen::rendering {

struct TemporalStats {
    std::uint32_t passes = 0;
    std::uint32_t framesValid = 0;
    std::uint32_t framesNeeded = 0;
    bool settling = false;
    bool stalled = false;
    std::uint64_t historyBytes = 0;
    std::uint32_t historyWidth = 0;
    std::uint32_t historyHeight = 0;
    double temporalMs = -1.0; // GPU time of the capture + effect passes (-1 = unavailable)
};

struct TemporalFrameInputs {
    wgpu::TextureView sceneHdr;
    wgpu::TextureView velocity;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint64_t frameIndex = 0;
    const scene::TemporalSettings* settings = nullptr;
    float resolutionScale = 0.5f; // from the quality tier
    wgpu::TextureFormat hdrFormat = wgpu::TextureFormat::RGBA16Float;
};

class TemporalEffects {
public:
    TemporalEffects(gpu::Context& context, gpu::ShaderLibrary& shaders);
    ~TemporalEffects();
    TemporalEffects(const TemporalEffects&) = delete;
    TemporalEffects& operator=(const TemporalEffects&) = delete;

    [[nodiscard]] Result<void> init();
    [[nodiscard]] Result<void> reload();

    // Encodes the capture and whatever effects are live. Returns the view the post chain should
    // treat as the scene image -- `in.sceneHdr` unchanged when nothing ran, so a scene with no
    // temporal effect is byte-identical to one built without this stage (ADR-368: a feature that
    // must not change the image proves it differentially).
    wgpu::TextureView run(wgpu::CommandEncoder& encoder, const TemporalFrameInputs& in, gpu::TransientPool& pool);

    // Renders the §53 history-state debug view over `target`. Safe to call when the ring is empty;
    // it then draws the empty ring, which is the answer to "why is nothing happening".
    void encodeDebugView(wgpu::CommandEncoder& encoder, const wgpu::TextureView& target, std::uint32_t width,
                         std::uint32_t height, wgpu::TextureFormat format);

    void reset(); // a discontinuity: forwards to the ring
    [[nodiscard]] const TemporalStats& stats() const { return stats_; }
    [[nodiscard]] const TemporalHistory& history() const { return *history_; }
    void setTimeline(gpu::FrameTimeline* timeline);
    void collectTimings();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::unique_ptr<TemporalHistory> history_;
    TemporalStats stats_;
};

} // namespace avgen::rendering
