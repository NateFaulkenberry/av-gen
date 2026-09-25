// avgen_character_quality -- ADR-826: simulate a scene or a project headless and report, per
// entity, how its motion and behaviour would read on screen. Individual metrics, never a score.
//
//   avgen_character_quality (--project p.json | --scene s.scene.json)
//                           [--seconds N] [--fps F] [--out file.json]
//
//   --project P   load a project through `Engine::loadProject`, the path the application opens
//   --scene S     load a bare composition through `Engine::loadComposition`
//   --seconds N   simulated seconds (default 60)
//   --fps F       frames per simulated second (default 60)
//   --out FILE    write the JSON there (default stdout)
//
// It runs the real `app::Engine` in Offline mode rather than an `EntityWorld` on its own, because the
// cast's motion is decided in more places than the entity tier: the Director's performances, the
// staging, the project's parameter overrides and the scene's own sequencing all write into the
// world before the entities step. A quality meter over half the pipeline measures half the film.
//
// Entity distance culling is switched off: it is a function of the camera, and a character that
// stops being simulated because the camera looked away would read as "stuck" here for reasons
// that have nothing to do with how it moves.
//
// Exit status: 0 when the file was written, 1 on a load failure or bad arguments. It reports; it
// does not judge -- thresholds on these numbers belong to whoever is reviewing the film.

#include "app/engine.hpp"
#include "core/log.hpp"
#include "core/time.hpp"
#include "entity/character_quality.hpp"
#include "entity/entity.hpp"
#include "scene/composition.hpp"
#include "scene/detail_limits.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

struct Options {
    fs::path project;
    fs::path scene;
    double seconds = 60.0;
    double fps = 60.0;
    fs::path out;
};

void usage() {
    std::fprintf(stderr,
                 "usage: avgen_character_quality (--project p.json | --scene s.scene.json)\n"
                 "                               [--seconds N] [--fps F] [--out file.json]\n");
}

bool parse(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        const bool hasValue = i + 1 < argc;
        if (std::strcmp(a, "--project") == 0 && hasValue) {
            o.project = argv[++i];
        } else if (std::strcmp(a, "--scene") == 0 && hasValue) {
            o.scene = argv[++i];
        } else if (std::strcmp(a, "--seconds") == 0 && hasValue) {
            o.seconds = std::atof(argv[++i]);
        } else if (std::strcmp(a, "--fps") == 0 && hasValue) {
            o.fps = std::atof(argv[++i]);
        } else if (std::strcmp(a, "--out") == 0 && hasValue) {
            o.out = argv[++i];
        } else {
            std::fprintf(stderr, "unknown or incomplete argument: %s\n", a);
            return false;
        }
    }
    if (o.project.empty() == o.scene.empty()) {
        std::fprintf(stderr, "exactly one of --project and --scene is required\n");
        return false;
    }
    if (!(o.seconds > 0.0) || !(o.fps > 0.0)) {
        std::fprintf(stderr, "--seconds and --fps must be positive\n");
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    Options o;
    if (!parse(argc, argv, o)) {
        usage();
        return 1;
    }

    // The engine's default logger writes to stdout, which is where the JSON goes when `--out` is not
    // given: a report interleaved with load chatter is not JSON. Logs go to stderr, warnings only --
    // the entity tier's own foot-slip warnings are worth seeing beside the numbers.
    log::init(log::Level::Warn);

    app::Engine engine(app::EngineMode::Offline);
    const auto loaded = o.project.empty() ? engine.loadComposition(o.scene) : engine.loadProject(o.project);
    if (!loaded) {
        std::fprintf(stderr, "load failed: %s\n", loaded.error().message.c_str());
        return 1;
    }
    scene::Composition* composition = engine.composition();
    if (composition == nullptr) {
        std::fprintf(stderr, "the loaded document is not a composition; there is no cast to measure\n");
        return 1;
    }
    // Through the ENGINE, not the scene. `Engine::update` writes its own `detailLimits_` over the
    // controller's scene at the top of every frame (ADR-186), so setting the flag on
    // `composition->scene()` -- the obvious line, and the first one this tool had -- is undone before
    // the first entity steps: the aliens then ran at their authored `coarseInterval` of 0.1 s beyond
    // 120 m, and every tenth-of-a-second catch-up step read as a 21 m/s hitch and every held frame
    // between as "stuck". The check after the first update below makes that failure loud.
    scene::DetailLimits limits = engine.detailLimits();
    limits.entityDistanceCull = false;
    engine.setDetailLimits(limits);

    // The application's frame convention (tests/support/project_round_trip.hpp `stepFrames`): the
    // first frame is at t = 0 with a zero delta, and every later one is a full step.
    const double dt = 1.0 / o.fps;
    const auto frames = static_cast<std::uint64_t>(std::llround(o.seconds * o.fps));
    entity::CharacterQualityRecorder recorder;
    double simulatingMs = 0.0;
    for (std::uint64_t i = 0; i < frames; ++i) {
        FrameTime time;
        time.renderTime = static_cast<double>(i) * dt;
        time.deltaTime = i == 0 ? 0.0 : dt;
        time.frameIndex = i;
        const auto start = std::chrono::steady_clock::now();
        engine.update(time);
        simulatingMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        if (i == 0 && composition->scene().detailLimits.entityDistanceCull) {
            std::fprintf(stderr, "entity distance cull is still in force after the first update; "
                                 "the numbers would measure the LOD, not the cast\n");
            return 1;
        }
        recorder.record(composition->entityWorld(), time.renderTime, time.deltaTime);
    }

    entity::CharacterQualityReport report = recorder.report();
    // Only the engine's update is timed, not the recorder: the question is what the simulation
    // costs, and the meter's own cost is not part of the film.
    report.wallClockMsPerFrame = frames > 0 ? simulatingMs / static_cast<double>(frames) : 0.0;
    nlohmann::json doc = entity::toJson(report);
    doc["source"] = o.project.empty() ? o.scene.string() : o.project.string();
    doc["scene"]["fps"] = o.fps;

    const std::string text = doc.dump(2);
    if (o.out.empty()) {
        std::fwrite(text.data(), 1, text.size(), stdout);
        std::fputc('\n', stdout);
    } else {
        std::ofstream file(o.out);
        file << text << '\n';
        if (!file) {
            std::fprintf(stderr, "could not write %s\n", o.out.string().c_str());
            return 1;
        }
    }
    return 0;
}
