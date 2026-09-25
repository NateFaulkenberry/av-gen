#include "app/director_stills.hpp"

#include "app/directing_context.hpp"
#include "app/engine.hpp"
#include "app/render_source.hpp"
#include "core/log.hpp"
#include "core/time.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/composition.hpp"

#include <chrono>
#include <system_error>
#include <unistd.h>

namespace avgen::app {
namespace {

double since(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

} // namespace

std::vector<std::pair<std::string, const seq::Shot*>> proposedShots(const directing::Compilation& c) {
    std::vector<std::pair<std::string, const seq::Shot*>> out;
    for (const directing::PlanShot& ps : c.plan.shots) {
        if (c.validation.isBlocked(ps.key)) {
            continue;
        }
        if (const seq::Shot* shot = c.staged.sequence.shotNamed(ps.name); shot != nullptr) {
            out.emplace_back(ps.key, shot);
        }
    }
    return out;
}

Result<ShotStillsReport> renderShotStills(gpu::Context& context, gpu::ShaderLibrary& shaders, Engine& live,
                                          const directing::Compilation& compilation, std::uint32_t width,
                                          std::uint32_t height, const std::filesystem::path& scratchDir) {
    ShotStillsReport report;
    const auto shots = proposedShots(compilation);
    if (shots.empty()) {
        return report;
    }
    static std::uint64_t serial = 0;
    const RenderSource source = renderSourceFor(live.projectPath(), scratchDir, "director_stills", ++serial,
                                                static_cast<long long>(::getpid()));
    auto start = std::chrono::steady_clock::now();
    if (auto r = live.writeProjectCopy(source.scratch); !r) {
        return fail("stills: cannot write the scratch copy: {}", r.error().message);
    }
    Engine scratch(EngineMode::Offline);
    auto loaded = scratch.loadProject(source.scratch);
    std::error_code ec;
    std::filesystem::remove(source.scratch, ec);
    if (!loaded) {
        return fail("stills: the scratch copy does not load: {}", loaded.error().message);
    }
    if (auto r = installCompilation(scratch, compilation); !r) {
        return fail("stills: the proposal does not install on the scratch copy: {}", r.error().message);
    }
    scratch.setViewport(width, height);
    report.loadMs = since(start);

    start = std::chrono::steady_clock::now();
    rendering::SceneRenderer renderer(context, shaders);
    if (auto r = renderer.init(); !r) {
        return fail("stills: renderer: {}", r.error().message);
    }
    std::uint64_t frame = 0;
    for (const auto& [item, shot] : shots) {
        const double mid = shot->startSeconds + (shot->durationSeconds * 0.5);
        scratch.seekSeconds(mid);
        const FrameTime t{mid, 1.0 / 60.0, ++frame};
        scratch.update(t);
        auto image = renderer.renderToImage(scratch.composition()->scene(), t, width, height);
        if (!image) {
            return fail("stills: rendering '{}': {}", shot->name, image.error().message);
        }
        report.stills.push_back(ShotStill{item, shot->name, mid, std::move(*image)});
    }
    report.renderMs = since(start);
    log::info("director stills: {} shot(s) at {}x{}: scratch session {:.0f} ms, seeks and frames {:.0f} ms",
              report.stills.size(), width, height, report.loadMs, report.renderMs);
    return report;
}

} // namespace avgen::app
