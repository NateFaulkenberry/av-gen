#pragma once

// The offline render job, built the one way every caller builds it (ADR-2159, Engine API stage S6), and the GPU half
// an `api::EngineHost` composes in.
//
// `makeOfflineRenderJob` is `Application::makeRenderJob` moved out, unchanged: an Offline engine, live control off,
// the project file loaded, `requireProjectScene`, then the RenderJob over it and `start()`. The Render panel, the
// render queue and headless `--render` call it through `Application`; the API host calls its first half
// (`makeOfflineRenderEngine`), because it starts the job later, under the machine's GPU lock.
//
// `makeGpuRenderBackend` is the host's `api::RenderBackend`: it creates the `gpu::Context` and the
// `ShaderLibrary(defaultSearchDirs(exe))` on the first job that starts -- never before -- and makes each job a
// RenderJob over a fresh Offline engine loaded from the request's snapshot. It is the only GPU code an API host has.

#include "api/jobs.hpp"
#include "app/render_job.hpp"
#include "app/render_settings.hpp"
#include "core/error.hpp"

#include <filesystem>
#include <memory>

namespace avgen::gpu {
class Context;
class ShaderLibrary;
} // namespace avgen::gpu

namespace avgen::rendering {
struct DebugViewOptions;
} // namespace avgen::rendering

namespace avgen::app {

class Engine;

// The four steps: an Offline engine, `setLiveControl(false)`, `loadProject(projectFile)`, `requireProjectScene()`;
// the project's warnings are logged.
[[nodiscard]] Result<std::unique_ptr<Engine>> makeOfflineRenderEngine(const std::filesystem::path& projectFile);

// The engine above, a RenderJob over it (output relative to `outputBase`, or the project file's folder when empty),
// the debug overlays, and `start()`.
[[nodiscard]] Result<std::unique_ptr<RenderJob>> makeOfflineRenderJob(gpu::Context& context,
                                                                      gpu::ShaderLibrary& shaders,
                                                                      const std::filesystem::path& projectFile,
                                                                      RenderSettings settings,
                                                                      const std::filesystem::path& outputBase,
                                                                      const rendering::DebugViewOptions& debug);

// The device's options for a host's lazily created context: headless, with Dawn's persistent pipeline cache where
// the application keeps it (AVGEN_PIPELINE_CACHE=off|<dir> as for the application).
// `shaderSearchDirs` empty: `ShaderLibrary::defaultSearchDirs(executable)`.
struct GpuBackendOptions {
    std::filesystem::path executable;
    std::vector<std::filesystem::path> shaderSearchDirs;
    bool pipelineCache = true;
};
[[nodiscard]] std::unique_ptr<api::RenderBackend> makeGpuRenderBackend(GpuBackendOptions options);
[[nodiscard]] api::RenderBackendFactory gpuRenderBackendFactory(GpuBackendOptions options);

} // namespace avgen::app
