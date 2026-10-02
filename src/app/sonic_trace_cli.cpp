#include "app/sonic_trace_cli.hpp"

#include "app/engine.hpp"
#include "core/log.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace avgen::app {

int runSonicTraceCommand(const std::filesystem::path& project, const std::filesystem::path& out, double fps) {
    if (project.empty()) {
        log::error("--sonic-trace needs a project: pass --project <file.json>");
        return 2;
    }
    Engine engine(EngineMode::Offline);
    engine.setLiveControl(false);
    if (auto r = engine.loadProject(project); !r) {
        log::error("--sonic-trace: {}", r.error().message);
        return 3;
    }
    const sonic::SonicSetup* setup = engine.sonicSetup();
    if (setup == nullptr) {
        log::error("--sonic-trace: '{}' has no `sonic` block", project.string());
        return 3;
    }
    const double rate = fps > 0.0 ? fps : std::max(1.0, engine.renderSettings().fps);
    const double duration = std::max(engine.durationSeconds(), setup->notes.endSeconds());
    signals::SignalBus& bus = engine.signals();

    std::ofstream file(out);
    if (!file) {
        log::error("--sonic-trace: cannot write '{}'", out.string());
        return 4;
    }
    const auto frames = static_cast<std::uint64_t>(std::ceil(duration * rate));
    std::vector<signals::SignalId> columns;
    bool header = false;
    // Means of the medium tier over the frames with sound, for the summary.
    std::array<double, sonic::kDimensionCount> sums{};
    std::uint64_t voiced = 0;
    double updateMs = 0.0;
    for (std::uint64_t f = 0; f <= frames; ++f) {
        FrameTime t;
        t.renderTime = static_cast<double>(f) / rate;
        t.deltaTime = f == 0 ? 0.0 : 1.0 / rate;
        t.frameIndex = f;
        const auto started = std::chrono::steady_clock::now();
        engine.update(t);
        updateMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        if (!header) {
            // Declared by now: the frame signals at construction, the interpret sources at load.
            file << "time";
            for (std::size_t i = 0; i < bus.size(); ++i) {
                const std::string& name = bus.info(static_cast<signals::SignalId>(i)).name;
                if (name.starts_with("sonic.") || name.starts_with("notes.") || name.starts_with("timbre.") ||
                    name.starts_with("visual.") || name.starts_with("response.")) {
                    columns.push_back(static_cast<signals::SignalId>(i));
                    file << ',' << name;
                }
            }
            file << '\n';
            header = true;
        }
        file << fmt::format("{:.4f}", t.renderTime);
        for (const signals::SignalId id : columns) {
            file << fmt::format(",{:.4f}", bus.value(id));
        }
        file << '\n';
        const sonic::SonicRuntime& rt = engine.sonicRuntime();
        if (rt.timbre().loudnessDb >= setup->character.gateDb) {
            ++voiced;
            for (std::size_t i = 0; i < sonic::kDimensionCount; ++i) {
                sums[i] += rt.medium()[i];
            }
        }
    }
    if (!file) {
        log::error("--sonic-trace: could not finish '{}'", out.string());
        return 4;
    }

    // The subsystem's own per-frame cost, measured apart from the engine: the same walk and publish, again.
    double sonicMs = 0.0;
    if (const auto* track = engine.track(); track != nullptr && !track->empty()) {
        signals::SignalBus scratch;
        sonic::SonicRuntime runtime;
        runtime.declare(scratch);
        const auto started = std::chrono::steady_clock::now();
        for (std::uint64_t f = 0; f <= frames; ++f) {
            const double s = static_cast<double>(f) / rate;
            runtime.advance(*setup, *track, s);
            runtime.publish(setup, scratch, s);
            scratch.clearEvents();
        }
        sonicMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    }

    std::printf("sonic trace: %llu frames at %g fps, %zu columns -> %s\n",
                static_cast<unsigned long long>(frames + 1), rate, columns.size(), out.string().c_str());
    std::printf("timbre pass: %zu analysis frames in %.1f ms at load (%.1f us per frame)\n", setup->timbre.size(),
                setup->timbreMillis,
                setup->timbre.empty() ? 0.0 : setup->timbreMillis * 1000.0 / static_cast<double>(setup->timbre.size()));
    std::printf("per render frame: sonic %.2f us (engine update %.2f ms)\n",
                sonicMs * 1000.0 / static_cast<double>(frames + 1), updateMs / static_cast<double>(frames + 1));
    std::printf("character, mean of the medium tier over %llu voiced frames:\n", static_cast<unsigned long long>(voiced));
    for (std::size_t i = 0; i < sonic::kDimensionCount; ++i) {
        const double mean = voiced > 0 ? sums[i] / static_cast<double>(voiced) : 0.0;
        std::string bar(static_cast<std::size_t>(std::lround(mean * 20.0)), '#');
        std::printf("  %-14s %.3f %s\n", sonic::dimensionName(static_cast<sonic::Dimension>(i)), mean, bar.c_str());
    }
    return 0;
}

} // namespace avgen::app
