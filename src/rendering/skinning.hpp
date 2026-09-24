#pragma once

// GPU skinning (ADR-086). Owns everything the skinned draw path needs that the static one does
// not: the joint-matrix buffer, its bind group, and the four pipeline variants built from
// shaders/pbr_skinned.wgsl. It lives in its own file so the scene renderer gains a pipeline choice
// and one extra dynamic offset at three draw sites rather than a subsystem.
//
// Bind groups. A skinned pipeline uses the scene's frame (0), material (2) and IBL (3) groups
// unchanged; only group 1 differs, adding the palette at binding 1 beside the ObjectUniforms that
// were always at binding 0. Both are bound with dynamic offsets, so one bind group serves every
// skinned draw in the frame.
//
// Buffer layout. One 256-byte-aligned slice per rig, holding the current palette followed by the
// palette the rig was drawn with last frame; the shader reads the joint count from
// ObjectUniforms::ids.w. The slice size is the largest rig in the scene, not kMaxPaletteJoints, so
// a 49-joint character costs 6 KB and not 32.

#include "core/error.hpp"
#include "scene/scene.hpp"

#include <webgpu/webgpu_cpp.h>

#include <cstdint>
#include <vector>

namespace avgen::gpu {
class Context;
class ShaderLibrary;
} // namespace avgen::gpu

namespace avgen::rendering {

struct SkinningStats {
    std::uint32_t rigs = 0;        // rigs with a palette on the GPU this frame
    std::uint32_t joints = 0;      // joint matrices uploaded this frame (0 when nothing moved)
    std::uint32_t uploadBytes = 0; // bytes written to the joint buffer this frame
    std::uint32_t draws = 0;       // skinned draws recorded this frame, across every pass
};

class SkinningRenderer {
public:
    // Where one rig's matrices live. `offset` is the dynamic offset for group 1 binding 1.
    struct Slice {
        std::uint32_t offset = 0;
        std::uint32_t jointCount = 0;
        [[nodiscard]] bool valid() const { return jointCount > 0; }
    };

    SkinningRenderer(gpu::Context& context, gpu::ShaderLibrary& shaders);

    // `objectUniforms` is the scene renderer's own object buffer: the skinned path binds exactly
    // the same ObjectUniforms, from the same slots, so a skinned entity and a static one describe
    // themselves identically.
    // ADR-703: `entityFx` is the scene renderer's per-entity effect records (FXL), bound at binding
    // 2 exactly as the static entity layout binds them, because the skinned pipeline runs the same
    // `fs_main` and that fragment stage reads them.
    [[nodiscard]] Result<void> init(const wgpu::BindGroupLayout& frameLayout,
                                    const wgpu::BindGroupLayout& materialLayout,
                                    const wgpu::BindGroupLayout& iblLayout, const wgpu::Buffer& objectUniforms,
                                    std::uint64_t objectSize, const wgpu::Buffer& entityFx,
                                    std::uint64_t entityFxSize, wgpu::TextureFormat colorFormat,
                                    wgpu::TextureFormat depthFormat);
    // ADR-128: the scene renderer's object buffer grows with the frame, and when it is replaced
    // this group's binding 0 still names the old one. Called by whoever replaced it, so the skinned
    // path cannot be left reading a buffer nobody is writing any more. ADR-703: the same for the
    // effect-record buffer at binding 2.
    void setObjectBuffer(const wgpu::Buffer& objectUniforms, std::uint64_t objectSize, const wgpu::Buffer& entityFx,
                         std::uint64_t entityFxSize);

    // Recompiles pbr_skinned.wgsl and rebuilds the pipelines; the previous ones are kept on failure.
    [[nodiscard]] Result<void> reload();

    // Sizes the joint buffer for this scene's rigs and uploads the palettes that changed. Cheap and
    // idempotent: a frame in which no rig re-posed writes nothing.
    void update(const scene::Scene& scene);
    // Holds the palettes the GPU already has (renderer forensics 4.5, "freeze animation"). Distinct
    // from disabling animation, which draws the *bind* pose: this holds whatever pose was current
    // when the arm was set, so a character stops moving in place rather than snapping to a T-pose.
    // Implemented as "do not upload" rather than as a copy of the scene's state, because the palette
    // on the GPU is already the thing being frozen and a second copy is a second thing to keep true.
    void setFrozen(bool frozen) { frozen_ = frozen; }
    [[nodiscard]] bool frozen() const { return frozen_; }

    // The slice for `rig`, or a zero slice when the rig has no palette on the GPU.
    [[nodiscard]] Slice slice(scene::RigId rig) const;
    [[nodiscard]] bool ready() const { return ready_; }

    [[nodiscard]] const wgpu::RenderPipeline& litPipeline(bool blend, bool doubleSided) const;
    [[nodiscard]] const wgpu::RenderPipeline& depthPipeline() const { return depthOnly_; }
    [[nodiscard]] const wgpu::BindGroup& objectBindGroup() const { return objectGroup_; }

    [[nodiscard]] const SkinningStats& stats() const { return stats_; }
    void resetFrameStats() { stats_.draws = 0; }
    void countDraw() { ++stats_.draws; }

    // The vertex buffer layout of a skinned mesh's influence stream (slot 1), exposed so the mesh
    // upload and the pipelines cannot disagree about it.
    static constexpr std::uint32_t kInfluenceSlot = 1;

private:
    Result<void> createPipelines(const wgpu::ShaderModule& module);
    void ensureBuffer(std::uint32_t sliceBytes, std::uint32_t rigCount);
    void rebuildObjectGroup();

    gpu::Context& context_;
    gpu::ShaderLibrary& shaders_;
    wgpu::BindGroupLayout objectLayout_;
    wgpu::PipelineLayout pipelineLayout_;
    wgpu::ShaderModule module_;
    wgpu::RenderPipeline opaqueCull_;
    wgpu::RenderPipeline opaqueNoCull_;
    wgpu::RenderPipeline blend_;
    wgpu::RenderPipeline depthOnly_;
    wgpu::Buffer objectUniforms_;
    std::uint64_t objectSize_ = 0;
    wgpu::Buffer entityFx_;
    std::uint64_t entityFxSize_ = 0;
    wgpu::Buffer jointBuffer_;
    wgpu::BindGroup objectGroup_;
    std::uint32_t sliceBytes_ = 0;
    std::uint32_t sliceCount_ = 0;
    bool frozen_ = false;
    wgpu::TextureFormat colorFormat_ = wgpu::TextureFormat::RGBA16Float;
    wgpu::TextureFormat depthFormat_ = wgpu::TextureFormat::Depth24Plus;
    std::vector<Slice> slices_;
    std::vector<std::uint64_t> uploadedVersions_;
    std::vector<std::uint8_t> staging_;
    const scene::Scene* scene_ = nullptr;
    SkinningStats stats_;
    bool ready_ = false;
};

} // namespace avgen::rendering
