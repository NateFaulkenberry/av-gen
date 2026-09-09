#pragma once

// The colour targets of the scene pass (ADR-035), shared by every renderer that draws into it:
// entities, procedural instances, raymarched SDFs, the grid, the skybox and particles. A pipeline
// used inside that pass must declare all five in this order, or WebGPU rejects it.
//
//   0 HDR radiance          RGBA16Float
//   1 normal + roughness    RGBA16Float  rg = octahedral normal, b = roughness, a = flags
//   2 velocity              RG16Float    screen motion in UV units
//   3 emission              RGBA16Float  rgb = emitted radiance, a = bloom weight
//   4 identifiers           R32Uint      low 16 bits object id, high 16 bits material id

#include <webgpu/webgpu_cpp.h>

#include <array>
#include <cstdint>

namespace avgen::rendering {

constexpr std::uint32_t kSceneTargetCount = 5;

[[nodiscard]] inline std::array<wgpu::TextureFormat, kSceneTargetCount> sceneTargetFormats(
    wgpu::TextureFormat hdrFormat) {
    return {hdrFormat, wgpu::TextureFormat::RGBA16Float, wgpu::TextureFormat::RG16Float,
            wgpu::TextureFormat::RGBA16Float, wgpu::TextureFormat::R32Uint};
}

// Fills the colour-target array for a scene-pass pipeline. `blend` (may be null) applies to the
// colour target only; `auxMask` is the write mask of the four auxiliary targets, so a blended or
// additive pipeline can leave the surface targets to the opaque geometry behind it.
inline void fillSceneTargets(std::array<wgpu::ColorTargetState, kSceneTargetCount>& targets,
                             wgpu::TextureFormat hdrFormat, const wgpu::BlendState* blend,
                             wgpu::ColorWriteMask auxMask = wgpu::ColorWriteMask::All,
                             wgpu::ColorWriteMask colorMask = wgpu::ColorWriteMask::All) {
    const auto formats = sceneTargetFormats(hdrFormat);
    for (std::uint32_t i = 0; i < kSceneTargetCount; ++i) {
        targets[i] = wgpu::ColorTargetState{};
        targets[i].format = formats[i];
        targets[i].writeMask = i == 0 ? colorMask : auxMask;
    }
    targets[0].blend = blend;
}

} // namespace avgen::rendering
