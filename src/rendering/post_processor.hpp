#pragma once

// Built-in image formation chain (ADR-016, ADR-039) over the transient pool. The order is fixed
// and documented in docs/image-formation.md:
//
//   scene HDR -> [metering of the pre-exposure image] -> [exposure] -> [defocus] -> [motion blur]
//             -> [lens distortion + chromatic aberration]
//             -> [bloom: prefilter, downsample chain, energy-conserving upsample chain]
//             -> [halation pyramid + anamorphic streaks: the "wide" tier]
//             -> composite (bloom + wide tier + colour grade) -> [fxaa] -> [sharpen]
//             -> HDR result for tone mapping.
//
// The defocus pass is one circle-of-confusion gather driven either by distance from the focus
// plane (depth of field, ADR-037) or by distance from a band across the frame (the tilt-shift,
// ADR-079), or by both at once, whichever circle is larger.
//
// Passes run only when their settings are active; with everything off and a unit exposure the
// input is returned unchanged. Selective post (bloom weighted by emission, sharpening masked by
// object identifier) uses the ADR-035 auxiliary targets when the caller supplies them in
// PostFrameInputs, and silently falls back to the luminance-only behaviour when it does not.

#include "core/error.hpp"
#include "gpu/transient_pool.hpp"
#include "scene/camera.hpp"
#include "scene/composition_data.hpp"
#include "scene/post_settings.hpp"

#include <glm/glm.hpp>
#include <webgpu/webgpu_cpp.h>

#include <cstdint>
#include <vector>

namespace avgen::gpu {
class Context;
class FrameTimeline;
class ShaderLibrary;
} // namespace avgen::gpu

namespace avgen::rendering {

struct PostFrameInputs {
    wgpu::TextureView sceneHdr;
    wgpu::TextureView depth;
    // ADR-035 auxiliary targets, optional. Null (the normal case today) disables the selective
    // paths without changing the image: `emission` weights bloom, `identifier` masks sharpening.
    wgpu::TextureView emission;
    wgpu::TextureView identifier;
    // ADR-040: the RG16F per-pixel screen motion. Null skips motion blur entirely - there is no
    // longer a camera-only fallback, because it disagreed with everything that moves on its own.
    wgpu::TextureView velocity;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    glm::mat4 prevViewProj{1.0f};
    glm::mat4 invViewProj{1.0f};
    glm::vec3 cameraPos{0.0f};
    std::uint64_t frameIndex = 0;
    const scene::PostSettings* settings = nullptr;
    // ADR-038: the scene's depth layers grade contrast and saturation by distance (atmospheric
    // perspective). Null, or a scene with no layers, leaves the grade uniform across the frame.
    const scene::CompositionData* composition = nullptr;
};

struct PostStats {
    std::uint32_t passes = 0;
    double postMs = -1.0;            // GPU time of the whole chain (the "post/" prefix on the timeline)
    std::uint32_t bloomLevels = 0;
    std::uint32_t halationLevels = 0;
    float exposureScale = 1.0f;      // the linear scale applied before bloom
    float exposureEv100 = 0.0f;      // the EV in force (scene-referred; see scene/camera.hpp)
    float meteredLuminance = -1.0f;  // the previous frame's centre-weighted luminance (-1 = none)
};

class PostProcessor {
public:
    PostProcessor(gpu::Context& context, gpu::ShaderLibrary& shaders);
    [[nodiscard]] Result<void> init();
    [[nodiscard]] Result<void> reload();

    // Encodes the chain and returns the view to tone-map (a pool texture, or the input itself).
    wgpu::TextureView run(wgpu::CommandEncoder& encoder, const PostFrameInputs& inputs, gpu::TransientPool& pool);
    // The texture behind the view run() returned this frame; null when the input passed through.
    [[nodiscard]] const wgpu::Texture& outputTexture() const { return output_; }
    [[nodiscard]] const PostStats& stats() const { return stats_; }

    // Auto-exposure state (ADR-037). It is part of render state: reset it when a render job seeks
    // or a scene is swapped so an offline render reproduces a live one exactly.
    [[nodiscard]] const scene::ExposureState& exposureState() const { return exposureState_; }
    void resetExposure();

    static constexpr wgpu::TextureFormat kHdrFormat = wgpu::TextureFormat::RGBA16Float;
    // Matches SceneRenderer's velocity target (ADR-035); the motion-blur tiles use it too.
    static constexpr wgpu::TextureFormat kVelocityFormat = wgpu::TextureFormat::RG16Float;

    // The shared frame timeline every post pass marks itself on (gpu/frame_timeline.hpp).
    void setTimeline(gpu::FrameTimeline* timeline) { timeline_ = timeline; }

private:
    static constexpr std::uint32_t kMaxDepthLayers = 6; // shaders/post.wgsl PostUniforms

    struct Uniforms {
        glm::vec2 texelSize;
        glm::vec2 outputSize;
        glm::vec4 params0;
        glm::vec4 params1;
        glm::vec4 params2;
        glm::vec4 params3;
        glm::vec4 params4;
        glm::vec4 lift;
        glm::vec4 gamma;
        glm::vec4 gain;
        glm::vec4 tintA;
        glm::vec4 tintB;
        glm::vec4 cameraPos;
        glm::mat4 prevViewProj;
        glm::mat4 invViewProj;
        // ADR-038 depth layers, as (start, end, contrast, saturation). Unused slots are zero and
        // the count rides in params2.y, so a scene with no layers writes an identity grade.
        glm::vec4 depthLayers[kMaxDepthLayers];
    };
    static_assert(sizeof(Uniforms) == 16 + 16 * 11 + 128 + 16 * kMaxDepthLayers);
    static constexpr std::uint32_t kSlotStride = 512; // dynamic-offset alignment safe
    static constexpr std::uint32_t kMaxSlots = 96;
    static constexpr std::uint64_t kMeterReadbackBytes = 256; // one row, alignment-safe

    struct PassTextures {
        wgpu::TextureView source;
        wgpu::TextureView second;
        wgpu::TextureView third;
        wgpu::TextureView depth;
        wgpu::TextureView emission;
        wgpu::TextureView identifier;
    };

    Result<void> createPipelines(const wgpu::ShaderModule& module);
    Result<wgpu::RenderPipeline> makePipeline(const wgpu::ShaderModule& module, const char* entry,
                                             wgpu::TextureFormat format = kHdrFormat);
    void runPass(wgpu::CommandEncoder& encoder, const wgpu::RenderPipeline& pipeline, const wgpu::TextureView& target,
                 const PassTextures& textures, const Uniforms& uniforms);
    // Convenience for the many passes that only bind `source` (and optionally `second`/`depth`).
    void runPass(wgpu::CommandEncoder& encoder, const wgpu::RenderPipeline& pipeline, const wgpu::TextureView& target,
                 const wgpu::TextureView& source, const wgpu::TextureView& second, const wgpu::TextureView& depth,
                 const Uniforms& uniforms);
    // Reads back the 1x1 metering result the previous frame produced (blocking, deterministic).
    // Returns false when nothing has been metered yet.
    bool takeMeasurement(float& luminance);
    // Encodes the metering reduction of the pre-exposure image and the readback copy.
    void encodeMetering(wgpu::CommandEncoder& encoder, const PostFrameInputs& in, gpu::TransientPool& pool,
                        const Uniforms& base);
    // Downsample/upsample pyramid over an already-prefiltered base; returns its finest level.
    wgpu::TextureView buildPyramid(wgpu::CommandEncoder& encoder, gpu::TransientPool& pool, const Uniforms& base,
                                   std::vector<gpu::TransientTexture>& down, float spread, float blend);

    gpu::Context& context_;
    gpu::ShaderLibrary& shaders_;
    gpu::FrameTimeline* timeline_ = nullptr;
    const char* stage_ = "post"; // which stage the pass being encoded belongs to
    bool initialised_ = false;
    wgpu::BindGroupLayout layout_;
    wgpu::PipelineLayout pipelineLayout_;
    wgpu::RenderPipeline exposure_;
    wgpu::RenderPipeline meterPrefilter_;
    wgpu::RenderPipeline meterReduce_;
    wgpu::RenderPipeline prefilter_;
    wgpu::RenderPipeline halationPrefilter_;
    wgpu::RenderPipeline downsample_;
    wgpu::RenderPipeline upsample_;
    wgpu::RenderPipeline wide_;
    wgpu::RenderPipeline lens_;
    wgpu::RenderPipeline composite_;
    wgpu::RenderPipeline fxaa_;
    wgpu::RenderPipeline sharpen_;
    wgpu::RenderPipeline dof_;
    wgpu::RenderPipeline motionBlur_;
    wgpu::RenderPipeline velocityTileMax_;
    wgpu::RenderPipeline velocityNeighbourMax_;
    wgpu::Sampler sampler_;
    wgpu::Buffer uniforms_;
    std::uint32_t slot_ = 0;
    wgpu::Texture black_;
    wgpu::TextureView blackView_;
    wgpu::Texture depthPlaceholder_;
    wgpu::TextureView depthPlaceholderView_;
    wgpu::Texture idPlaceholder_;
    wgpu::TextureView idPlaceholderView_;
    wgpu::Buffer meterReadback_;
    bool meterPending_ = false;   // a copy into meterReadback_ is in flight
    bool haveMeasurement_ = false;
    float measuredLuminance_ = 0.0f;
    scene::ExposureState exposureState_;
    PostStats stats_;
    wgpu::Texture output_;
};

} // namespace avgen::rendering
