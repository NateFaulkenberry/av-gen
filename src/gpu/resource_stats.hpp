#pragma once

// ADR-1091: what the GPU layer holds, for the live profiler (docs/live-optimizer/02-plan.md, 1f).
//
// Two kinds of accounting, both off the frame:
//   * pipelines and shader modules are COUNTED as they are created (one relaxed atomic increment at creation, which
//     happens at load or, for SDF variants, at first use -- never per frame in a steady scene). The counts are
//     "created since the process started"; a pipeline the engine dropped is still counted. Their *delta* over a
//     measured window is what the profiler reports as compiles during measurement, and a touring frame needs it at 0.
//   * memory is read from Dawn's own accounting (`dawn::native::ComputeEstimatedMemoryUsageInfo` and
//     `DumpMemoryStatistics`), once, when asked. Dawn already tracks every texture and buffer it allocates, so
//     counting the same bytes again at ~150 creation sites in this repository would be a second, weaker copy of a
//     number that exists. The figures are Dawn's estimate of what it allocated, said so wherever they are printed.

#include <webgpu/webgpu_cpp.h>

#include <cstdint>
#include <string>
#include <vector>

namespace avgen::gpu {

struct PipelineCounters {
    std::uint64_t renderPipelines = 0;
    std::uint64_t computePipelines = 0;
    std::uint64_t shaderModules = 0;
    [[nodiscard]] std::uint64_t compiles() const { return renderPipelines + computePipelines + shaderModules; }
};
[[nodiscard]] PipelineCounters pipelineCounters();
void noteShaderModuleCreated();

// Every pipeline in the renderer is created through these, so the counters cannot miss one.
[[nodiscard]] wgpu::RenderPipeline createRenderPipeline(const wgpu::Device& device,
                                                        const wgpu::RenderPipelineDescriptor* desc);
[[nodiscard]] wgpu::ComputePipeline createComputePipeline(const wgpu::Device& device,
                                                          const wgpu::ComputePipelineDescriptor* desc);

struct TextureMemory {
    std::string label;
    std::uint64_t bytes = 0;
    std::string detail;
};
struct MemoryReport {
    bool available = false;
    std::uint64_t totalBytes = 0;
    std::uint64_t textureBytes = 0;
    std::uint64_t depthStencilBytes = 0;
    std::uint64_t bufferBytes = 0;
    std::uint64_t renderTargetBytes = 0; // textures whose usage includes RenderAttachment
    std::uint64_t textures = 0;
    std::uint64_t buffers = 0;
    std::vector<TextureMemory> largest; // the largest textures, largest first
};
[[nodiscard]] MemoryReport memoryReport(const wgpu::Device& device, std::size_t largestCount);

} // namespace avgen::gpu
