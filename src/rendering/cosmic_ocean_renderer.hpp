#pragma once

// The Cosmic Ocean's draw (ADR-390).
//
// One fullscreen triangle inside the scene pass, immediately after ADR-230's atmospheric sky layer,
// with the same depth state and the same additive blend. It owns one uniform buffer and one bind
// group and nothing else: there is no render target, no texture, no vertex buffer and no per-object
// data, because every celestial body in the effect is a hash evaluated in the fragment shader.
//
// **Why a second draw rather than a term inside the atmosphere layer.** Appending a forty-six-lane
// block to `FrameUniforms` means declaring it in `common.wgsl`, which every shader includes, and
// teaching the layout-guard test to flatten forty-six names; it also grows the block
// `ShadowRenderer::upload` copies once per shadow view for data no shadow view reads. A separate
// pipeline costs one bind and one `Draw(3)`. It is in the *same render pass*, so on a tile-based
// GPU the second draw's read-modify-write of the HDR and emission targets stays in tile memory --
// which is why a second draw here is cheap and a second *pass* would not be.
//
// It also earns an honest A/B arm. `--disable cosmic` removes the draw and stops the upload, so the
// arm removes the fragment work and the uniform content together rather than measuring the same
// frame through one more branch (ADR-182). That arm is also §39's acceptance test.

#include "core/error.hpp"
#include "world/cosmic_ocean.hpp"

#include <webgpu/webgpu_cpp.h>

#include <memory>

namespace avgen::gpu {
class Context;
class ShaderLibrary;
} // namespace avgen::gpu

namespace avgen::rendering {

struct CosmicOceanStats {
    bool drawn = false;
    std::uint32_t dropped = 0; // effects past the first; the UI reports them rather than hiding them
    // ADR-450. The size the nebulae were actually rasterised at, which is not always the size that
    // was asked for -- a scale of 1.0, or a frame too small to halve, keeps them in the main draw.
    std::uint32_t nebulaWidth = 0;
    std::uint32_t nebulaHeight = 0;
    bool nebulaReduced = false;
};

class CosmicOceanRenderer {
public:
    CosmicOceanRenderer(gpu::Context& context, gpu::ShaderLibrary& shaders);
    ~CosmicOceanRenderer();
    CosmicOceanRenderer(const CosmicOceanRenderer&) = delete;
    CosmicOceanRenderer& operator=(const CosmicOceanRenderer&) = delete;

    // `frameLayout` is SceneRenderer's group 0 layout; `colorFormat` the HDR target's format.
    [[nodiscard]] Result<void> init(wgpu::TextureFormat colorFormat, wgpu::TextureFormat depthFormat,
                                    const wgpu::BindGroupLayout& frameLayout);
    [[nodiscard]] Result<void> reload(); // hot reload; keeps the old pipeline on failure

    // Per frame, before the scene pass is encoded. Uploads the packed block and decides whether the
    // draw happens at all. `block` is `world::packCosmicOcean`'s output; `live` is false when no
    // scene authored an ocean, when it is disabled, or when the renderer's toggle is off -- and on
    // false nothing is uploaded and nothing is drawn.
    // `nebulaScale` is the fraction of (width, height) the two nebulae are rasterised at; 1.0
    // keeps them in the main draw and allocates nothing. Sizing happens here rather than in `draw`
    // because the offscreen pair may have to be (re)created, which cannot happen inside a pass.
    void update(const world::CosmicOceanGpu& block, bool live, std::uint32_t dropped,
                float nebulaScale, std::uint32_t width, std::uint32_t height);

    // Records the reduced-resolution nebula pass. Must be called on the encoder BEFORE the scene
    // pass is begun -- it is its own render pass and a pass cannot be nested in another. A no-op
    // when the ocean is not live or the lever is off.
    void renderNebula(wgpu::CommandEncoder& encoder, const wgpu::BindGroup& frameBindGroup);

    // Records the draw. A no-op when the last `update` said the ocean is not live, so a scene
    // without one pays nothing -- not even a uniform branch.
    void draw(wgpu::RenderPassEncoder& pass);

    [[nodiscard]] bool ready() const;
    [[nodiscard]] const CosmicOceanStats& stats() const { return stats_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    CosmicOceanStats stats_;
};

} // namespace avgen::rendering
