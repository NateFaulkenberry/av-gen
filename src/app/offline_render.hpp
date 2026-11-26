#pragma once

// The offline render job, built the one way every caller builds it (ADR-2159, Engine API stage S6), and the GPU half
// an `api::EngineHost` composes in -- since stage S10 (ADR-2165) Studio's host too.
//
// `makeOfflineRenderEngine` is the CPU half every render starts with: an Offline engine, live control off, the
// project file loaded, `requireProjectScene`.
//
// `GpuRenderBackend` is the host's `api::RenderBackend`. It creates the `gpu::Context` and the
// `ShaderLibrary(defaultSearchDirs(exe))` once -- on the first job that starts, or when Studio asks for them at
// start-up (its live renderer, compositor and outputs are built from them) -- and makes each job a RenderJob over a
// fresh Offline engine:
//   - `create`: an API request (render.capture, render.start);
//   - `createLocal`: the operator's full RenderSettings (the Render panel, the render queue, headless --render and
//     --queue), with the debug overlays and a frame cap per slice, and a hook that hands the RenderJob to the caller
//     when it starts (the Render panel's progress and frame preview read it).
// It is the only GPU code an API host has.

#include "api/jobs.hpp"
#include "app/render_job.hpp"
#include "app/render_settings.hpp"
#include "core/error.hpp"
#include "rendering/debug_visualizer.hpp"

#include <filesystem>
#include <functional>
#include <memory>

namespace avgen::gpu {
class Context;
class ShaderLibrary;
} // namespace avgen::gpu

namespace avgen::app {

class Engine;

// The four steps: an Offline engine, `setLiveControl(false)`, `loadProject(projectFile)`, `requireProjectScene()`;
// the project's warnings are logged.
[[nodiscard]] Result<std::unique_ptr<Engine>> makeOfflineRenderEngine(const std::filesystem::path& projectFile);

// The device's options for a host's lazily created context, with Dawn's persistent pipeline cache where the
// application keeps it (AVGEN_PIPELINE_CACHE=off|<dir> as for the application).
// `shaderSearchDirs` empty: `ShaderLibrary::defaultSearchDirs(executable)`.
struct GpuBackendOptions {
    std::filesystem::path executable;
    std::vector<std::filesystem::path> shaderSearchDirs;
    bool pipelineCache = true;
    // ADR-2165: Studio's device presents to its window: the window's CAMetalLayer, asked for when the device is
    // created (empty, or null: a headless device). And the device's label (Studio's has none, as before).
    std::function<void*()> surfaceLayer;
    std::string label = "avgen-engine-host";
};

// One of the operator's jobs, over its full settings (ADR-2165).
struct LocalRenderJob {
    std::filesystem::path project;    // the file the job loads (a scratch copy, or the user's project for --render)
    RenderSettings settings;
    std::filesystem::path outputBase; // a relative output resolves here; empty: the project file's folder
    rendering::DebugViewOptions debug{};
    int maxFramesPerStep = 1;         // the Render panel's 4 per UI frame; --render's 1
    // Called on the host thread when the RenderJob has started (pipelines compiled, warmed up).
    std::function<void(std::shared_ptr<RenderJob>)> onStarted;
    // Called once the project has loaded, before anything else: the scratch copy has done its job.
    std::function<void()> onLoaded;
};

class GpuRenderBackend final : public api::RenderBackend {
public:
    explicit GpuRenderBackend(GpuBackendOptions options);
    ~GpuRenderBackend() override;

    [[nodiscard]] Result<void> ensureDevice() override;
    [[nodiscard]] bool deviceCreated() const override { return context_ != nullptr; }
    [[nodiscard]] Result<std::unique_ptr<api::GpuRenderJob>> create(const api::RenderRequest& request) override;
    [[nodiscard]] Result<std::unique_ptr<api::GpuRenderJob>> createLocal(LocalRenderJob job);

    // The device and the shaders; only after `ensureDevice` succeeded.
    [[nodiscard]] gpu::Context& context() { return *context_; }
    [[nodiscard]] gpu::ShaderLibrary& shaders() { return *shaders_; }

private:
    GpuBackendOptions options_;
    std::unique_ptr<gpu::Context> context_;
    std::unique_ptr<gpu::ShaderLibrary> shaders_;
};

[[nodiscard]] std::unique_ptr<api::RenderBackend> makeGpuRenderBackend(GpuBackendOptions options);
[[nodiscard]] api::RenderBackendFactory gpuRenderBackendFactory(GpuBackendOptions options);

} // namespace avgen::app
