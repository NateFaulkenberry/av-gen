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

#include "rendering/cloud_march_quality.hpp"
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
    // nx*nz (offset, packed) pairs, then the lobe indices. ADR-2101: packed = count (bits 0..7) | the column's lowest
    // lobe bottom (bits 8..19) | its highest lobe top (bits 20..31), both quantised over [yLo, yHi] (kBinCountMask etc.)
    std::vector<std::uint32_t> words;
    float yLo = 0.0f;                 // the lobes' vertical extent, as the renderer's box has it
    float yHi = 0.0f;
    std::uint32_t truncated = 0;      // cells that held more lobes than the renderer reads (the extra are dropped)
    std::uint32_t halosDropped = 0;   // ADR-2123: mist halo entries a full cell had no room for (bodies always first)
};
inline constexpr std::uint32_t kMaxLobesPerCell = 128; // ADR-2123 (was 64): room for the mist halos
inline constexpr std::uint32_t kMaxBinCellsPerSide = 128; // ADR-2105 (was 64)
inline constexpr std::uint32_t kMaxBinCellsPerSideSplit = 512; // ADR-2114: the finest grid an overfull cell splits to
inline constexpr std::uint32_t kBinCountMask = 0xFFu;
// ADR-2112: `bobReach` (per formation, metres) widens a riding lobe's footprint and height range by how far its
// formation can move, so the bins built once still hold it wherever it bobs.
// ADR-2123: `grow` (>= 1) is the lobes' mist halo as a multiple of their radius; a cell lists the lobes whose bodies
// cover it first (split per ADR-2114 until they fit) and then, in the room left, the lobes only their halo reaches.
[[nodiscard]] CloudLobeBins binCloudLobes(const std::vector<scene::CloudLobe>& lobes,
                                          const std::vector<glm::vec3>& bobReach = {}, float grow = 1.0f);

// ADR-2111: the march's per-frame jitters from FrameTime::frameNonce (x = the step offset, y = the light taps'),
// each a low-discrepancy sequence per FRAME at 24, 30 and 60 fps (public for the unit test).
[[nodiscard]] glm::vec2 cloudFrameJitter(std::uint32_t frameNonce);

// ADR-2131: the march in bands of rows, each its own submission, so no command buffer runs past the macOS GPU watchdog
// (~5 s: past it the buffer is aborted and the process's later submissions are ignored, and the frames come back
// black with no error). A band's cost is counted as march pixels * steps * (1 + light taps); 2^30 of them took ~0.6 s
// in Neon Dreams' heaviest measured view (Apple M-series, 192 steps, 8 taps, 3 erosion octaves), so a band has about
// 8x headroom. A live march (half resolution, ~96 steps) is one band, as before.
inline constexpr std::uint64_t kCloudMarchUnitsPerSubmit = 1ull << 30;
[[nodiscard]] std::uint32_t cloudMarchBands(std::uint32_t marchWidth, std::uint32_t marchHeight, int steps,
                                            int lightSteps, std::uint64_t unitsPerSubmit = kCloudMarchUnitsPerSubmit);

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
    // ADR-2131: the bands the last frame's march was split into (1 = one pass, as before), and a test's budget
    [[nodiscard]] std::uint32_t lastMarchBands() const { return lastMarchBands_; }
    void setMarchUnitsPerSubmit(std::uint64_t units) { unitsPerSubmit_ = units; }

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
    CloudLobeBins farBins_;          // ADR-2104: the far lobe set's grid (its words follow the hero grid's)
    std::uint32_t farWordBase_ = 0;
    bool farOn_ = false;
    float heroYLo_ = 0.0f;
    float heroYHi_ = 0.0f;
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
    std::uint32_t lastMarchBands_ = 0;
    std::uint64_t unitsPerSubmit_ = kCloudMarchUnitsPerSubmit;
    int historyIndex_ = 0;     // the one written last frame
    bool historyValid_ = false;
    glm::mat4 lastViewProj_{1.0f};
    double lastTime_ = -1e30;
};

} // namespace avgen::rendering
