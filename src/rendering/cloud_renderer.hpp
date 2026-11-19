#pragma once

// ADR-1289: the cloud Environment. ADR-2100 (Cloud Environment v2): three passes over the lit frame.
//   1. `clouds`           -- the march, at `Clouds::resolution` of the frame, into a premultiplied colour target and a
//                            depth target (the cloud's transmittance-weighted distance, and the scene depth it was
//                            cut against). Steps are jittered per pixel and per frame.
//   2. `clouds.resolve`   -- temporal accumulation: last frame's result reprojected by the cloud's own depth, clamped
//                            to this frame's neighbourhood, blended by `Clouds::temporal`. Skipped when that is 0.
//   3. `clouds.composite` -- a depth-aware upsample to the frame's resolution, composited premultiplied into HDR.
// The lobes are binned on an xz grid once when they change, so a sample reads only the lobes near it; the shape noise
// is a tiling 64^3 texture built once per process.

#include "rendering/environment_renderer.hpp"

#include <glm/glm.hpp>
#include <webgpu/webgpu_cpp.h>

#include <array>
#include <cstdint>
#include <vector>

namespace avgen::gpu {
class Context;
}

namespace avgen::scene {
struct CloudLobe;
}

namespace avgen::rendering {

// ADR-2100: the lobe bins, built on the CPU (public for the unit test).
struct CloudLobeBins {
    glm::vec2 origin{0.0f};  // xz of the grid's corner
    float cellSize = 1.0f;   // metres
    std::uint32_t nx = 0;
    std::uint32_t nz = 0;
    std::vector<std::uint32_t> words; // nx*nz (offset, count) pairs, then the lobe indices
    std::uint32_t truncated = 0;      // cells that held more lobes than the renderer reads (the extra are dropped)
};
inline constexpr std::uint32_t kMaxLobesPerCell = 64;
[[nodiscard]] CloudLobeBins binCloudLobes(const std::vector<scene::CloudLobe>& lobes);

// ADR-2100: the tiling shape-noise volume, kCloudNoiseSize^3 RGBA8 (R Perlin-Worley, G Worley fbm at 4/8/16 cells,
// B at 8/16/32, A Perlin fbm). Deterministic; built once per process.
inline constexpr std::uint32_t kCloudNoiseSize = 64;
[[nodiscard]] const std::vector<std::uint8_t>& cloudNoiseVolume();

class CloudRenderer final : public EnvironmentRenderer {
public:
    [[nodiscard]] std::string_view name() const override { return "clouds"; }
    [[nodiscard]] bool wants(const scene::Scene& scene) const override;
    void encode(const EnvironmentFrame& frame) override;
    [[nodiscard]] Result<void> reload(gpu::ShaderLibrary& shaders) override;

    [[nodiscard]] Result<void> init(gpu::Context& context, gpu::ShaderLibrary& shaders, wgpu::TextureFormat hdrFormat);
    [[nodiscard]] bool ready() const { return static_cast<bool>(march_) && static_cast<bool>(composite_); }

private:
    [[nodiscard]] Result<void> createPipelines(gpu::ShaderLibrary& shaders);
    void ensureTargets(std::uint32_t w, std::uint32_t h);

    gpu::Context* context_ = nullptr;
    wgpu::TextureFormat hdrFormat_ = wgpu::TextureFormat::RGBA16Float;
    wgpu::BindGroupLayout marchLayout_;
    wgpu::BindGroupLayout resolveLayout_;
    wgpu::BindGroupLayout compositeLayout_;
    wgpu::RenderPipeline march_;
    wgpu::RenderPipeline resolve_;
    wgpu::RenderPipeline composite_;
    wgpu::Buffer uniforms_;
    wgpu::Buffer lobes_;
    wgpu::Buffer bins_;
    wgpu::Texture noise_;
    wgpu::TextureView noiseView_;
    wgpu::Sampler repeatSampler_;
    wgpu::Sampler clampSampler_;
    std::uint64_t lobesKey_ = 0;
    std::uint32_t lobeCount_ = 0;
    CloudLobeBins bins_cpu_;
    glm::vec3 boxMin_{0.0f};
    glm::vec3 boxMax_{0.0f};

    // the reduced-resolution targets: this frame's march (colour, depth) and the history ping-pong
    std::uint32_t marchW_ = 0;
    std::uint32_t marchH_ = 0;
    wgpu::Texture marchColor_;
    wgpu::TextureView marchColorView_;
    wgpu::Texture marchDepth_;
    wgpu::TextureView marchDepthView_;
    std::array<wgpu::Texture, 2> history_{};
    std::array<wgpu::TextureView, 2> historyView_{};
    int historyIndex_ = 0;     // the one written last frame
    bool historyValid_ = false;
    glm::mat4 lastViewProj_{1.0f};
    double lastTime_ = -1e30;
};

} // namespace avgen::rendering
