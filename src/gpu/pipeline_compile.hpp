#pragma once

// ADR-1562: pipelines compile on Dawn's workers, and the thread that waits for them stays responsive.
//
// A pipeline's creation is where Metal compiles its shaders. Cold, that is seconds per pipeline (minutes, before
// ADR-1561), and the editor creates about 130 of them at startup. Two things follow:
//   * Every pipeline, including the ones created through the synchronous `gpu::createRenderPipeline` and
//     `gpu::createComputePipeline`, is compiled with Dawn's async creation, and the waiting thread calls the wait hook
//     about every 30 ms. The editor's hook pumps the window and draws a "Compiling shaders" screen, so a cold launch
//     is a progress screen, not a blank window the system marks as not responding. What the caller gets back is the
//     same pipeline it always got; a creation that fails is retried synchronously, so its error object and its error
//     scope are what they were.
//   * The heavy pipelines are *started* (`startRenderPipeline`, `startComputePipeline`): their compiles run in
//     parallel while the rest of initialisation goes on, and `finishPipelines` waits for every one started. A
//     renderer that starts a pipeline must not use it before `finishPipelines` returns.
//
// The waiting thread does nothing else: offline renders and tests block exactly where they blocked before, and the
// pipelines they get are identical.

#include "core/error.hpp"

#include <webgpu/webgpu_cpp.h>

#include <cstdint>
#include <functional>
#include <string>

namespace avgen::gpu {

struct PipelineCompileProgress {
    std::uint64_t started = 0; // pipeline creations begun since the process started
    std::uint64_t landed = 0;  // of those, finished (successfully or not)
    std::string waitingFor;    // the label of a pipeline being waited on, when known
};
[[nodiscard]] PipelineCompileProgress pipelineCompileProgress();

// Called, on the thread that installed it only, about every 30 ms while that thread waits for a pipeline. Not called
// re-entrantly. An empty function removes it.
void setPipelineWaitHook(std::function<void(const PipelineCompileProgress&)> hook);

// While one is alive (on the creating thread), start*Pipeline begins compiles on Dawn's workers; with none, it creates
// synchronously, so a renderer initialised on its own (a test, a tool) behaves exactly as before. The destructor waits
// for anything still in flight, so an early return cannot leave a callback pointing at a dead member.
class PipelineBatch {
public:
    explicit PipelineBatch(const wgpu::Device& device);
    ~PipelineBatch();
    PipelineBatch(const PipelineBatch&) = delete;
    PipelineBatch& operator=(const PipelineBatch&) = delete;
    // Waits for every started pipeline; fails, naming them, if any failed.
    [[nodiscard]] Result<void> finish();

private:
    wgpu::Device device_;
};

// Begin a compile; `*out` is set when it lands (null if it failed: finishPipelines reports it). `out` must stay valid
// until finishPipelines returns.
void startRenderPipeline(const wgpu::Device& device, const wgpu::RenderPipelineDescriptor* desc,
                         wgpu::RenderPipeline* out);
void startComputePipeline(const wgpu::Device& device, const wgpu::ComputePipelineDescriptor* desc,
                          wgpu::ComputePipeline* out);
[[nodiscard]] std::size_t pipelinesInFlight();

// The synchronous creations' implementation (resource_stats.cpp's wrappers count, then call these).
[[nodiscard]] wgpu::RenderPipeline compileRenderPipeline(const wgpu::Device& device,
                                                         const wgpu::RenderPipelineDescriptor* desc);
[[nodiscard]] wgpu::ComputePipeline compileComputePipeline(const wgpu::Device& device,
                                                           const wgpu::ComputePipelineDescriptor* desc);

// For AVGEN_PIPELINE_TIMING (ADR-1560).
void notePipelineTime(const char* kind, const std::string& label, double ms);

} // namespace avgen::gpu
