// avgen_cast_trace -- where every character is, at every moment of a film: the call sheet a
// cinematographer needs before placing a camera on somebody who is not following a script.
//
//   avgen_cast_trace (--project p.json | --scene s.scene.json)
//                    [--seconds N] [--fps F] [--hz H] [--nodes a,b,...] [--out file.json]
//
//   --project P   load a project through `Engine::loadProject`, the path the application opens
//   --scene S     load a bare composition through `Engine::loadComposition`
//   --seconds N   simulated seconds (default 60)
//   --fps F       the simulation's step, which must be the render's frame rate (default 60)
//   --hz H        samples written per second (default 10)
//   --nodes LIST  composition nodes to trace besides the entities, by name (a hero, a camera rig)
//   --out FILE    write the JSON there (default stdout)
//
// Autonomous characters decide where they go, so the only way to know where an alien will be at
// 2:30 is to run the film to 2:30. This runs the real `app::Engine` in Offline mode -- the same
// Director performances, staging, parameter overrides and music events the render sees -- and
// writes each entity's position, facing, speed, activity and visibility at a fixed sample rate.
//
// Two conditions make the trace describe the render rather than some other film:
//
//   * **The step is the render's.** An entity integrates at 1/fps (`EntityWorld::seekExact` steps
//     the same grid), so a 30 fps trace of a film rendered at 60 is a different simulation, and for
//     a character that decides, a different path. Trace at the fps you render at.
//   * **Entity distance culling is off,** as it is in an offline render (`DetailLimits::
//     offlineDefault`). The live viewport culls far bodies; a trace that culled would describe the
//     viewport.
//
// It traces; it does not judge. Exit status: 0 when the file was written, 1 on a load failure or
// bad arguments.

#include "app/engine.hpp"
#include "core/log.hpp"
#include "core/time.hpp"
#include "entity/entity.hpp"
#include "entity/locomotion.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "scene/detail_limits.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

struct Options {
    fs::path project;
    fs::path scene;
    double seconds = 60.0;
    double fps = 60.0;
    double hz = 10.0;
    std::vector<std::string> nodes;
    fs::path out;
};

void usage() {
    std::fprintf(stderr,
                 "usage: avgen_cast_trace (--project p.json | --scene s.scene.json)\n"
                 "                        [--seconds N] [--fps F] [--hz H] [--nodes a,b,...] [--out file.json]\n");
}

std::vector<std::string> splitList(const char* text) {
    std::vector<std::string> out;
    std::stringstream stream(text);
    std::string item;
    while (std::getline(stream, item, ',')) {
        if (!item.empty()) {
            out.push_back(item);
        }
    }
    return out;
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
        } else if (std::strcmp(a, "--hz") == 0 && hasValue) {
            o.hz = std::atof(argv[++i]);
        } else if (std::strcmp(a, "--nodes") == 0 && hasValue) {
            o.nodes = splitList(argv[++i]);
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
    if (!(o.seconds > 0.0) || !(o.fps > 0.0) || !(o.hz > 0.0) || o.hz > o.fps) {
        std::fprintf(stderr, "--seconds, --fps and --hz must be positive, and --hz no more than --fps\n");
        return false;
    }
    return true;
}

// Three decimals: a millimetre is finer than any framing decision, and the file stays readable.
double rounded(double v) { return std::round(v * 1000.0) / 1000.0; }

nlohmann::json vec3(const glm::vec3& v) { return {rounded(v.x), rounded(v.y), rounded(v.z)}; }

struct Track {
    nlohmann::json t = nlohmann::json::array();
    nlohmann::json position = nlohmann::json::array();
    nlohmann::json yaw = nlohmann::json::array();
    nlohmann::json speed = nlohmann::json::array();
    nlohmann::json activity = nlohmann::json::array();
    nlohmann::json visible = nlohmann::json::array();
};

} // namespace

int main(int argc, char** argv) {
    Options o;
    if (!parse(argc, argv, o)) {
        usage();
        return 1;
    }
    // The JSON may go to stdout, so the engine's chatter goes to stderr and only warnings reach it.
    log::init(log::Level::Warn);

    app::Engine engine(app::EngineMode::Offline);
    const auto loaded = o.project.empty() ? engine.loadComposition(o.scene) : engine.loadProject(o.project);
    if (!loaded) {
        std::fprintf(stderr, "load failed: %s\n", loaded.error().message.c_str());
        return 1;
    }
    scene::Composition* composition = engine.composition();
    if (composition == nullptr) {
        std::fprintf(stderr, "the loaded document is not a composition; there is no cast to trace\n");
        return 1;
    }
    // Through the engine, not the scene: `Engine::update` writes its own limits over the scene's at
    // the top of every frame (ADR-186), which is why `avgen_character_quality` checks it took.
    scene::DetailLimits limits = engine.detailLimits();
    limits.entityDistanceCull = false;
    engine.setDetailLimits(limits);

    for (const std::string& name : o.nodes) {
        if (composition->findNode(name) == nullptr) {
            std::fprintf(stderr, "no composition node named '%s'\n", name.c_str());
            return 1;
        }
    }

    // The application's frame convention: the first frame is t = 0 with a zero delta, every later
    // one a full step (`tests/support/project_round_trip.hpp` `stepFrames`).
    const double dt = 1.0 / o.fps;
    const auto frames = static_cast<std::uint64_t>(std::llround(o.seconds * o.fps)) + 1;
    const auto every = std::max<std::uint64_t>(1, static_cast<std::uint64_t>(std::llround(o.fps / o.hz)));
    std::map<std::string, Track> entities;
    std::map<std::string, Track> nodes;
    params::ParameterSet& params = engine.params();

    for (std::uint64_t i = 0; i < frames; ++i) {
        FrameTime time;
        time.renderTime = static_cast<double>(i) * dt;
        time.deltaTime = i == 0 ? 0.0 : dt;
        time.frameIndex = i;
        engine.update(time);
        if (i == 0 && composition->scene().detailLimits.entityDistanceCull) {
            std::fprintf(stderr, "entity distance cull is still in force after the first update; "
                                 "the trace would describe the viewport, not the render\n");
            return 1;
        }
        if (i % every != 0) {
            continue;
        }
        for (const auto& owned : composition->entityWorld().entities()) {
            const entity::Entity& e = *owned;
            const entity::LocomotionState& loco = e.locomotion();
            Track& track = entities[e.name()];
            track.t.push_back(rounded(time.renderTime));
            track.position.push_back(vec3(loco.position));
            track.yaw.push_back(rounded(loco.yaw));
            track.speed.push_back(rounded(loco.speed));
            track.activity.push_back(entity::activityName(loco.activity));
            const params::IParameter* visible = params.find("nodes/" + e.desc().driven() + "/visible");
            track.visible.push_back(visible == nullptr || visible->finalComponent(0) > 0.5f);
        }
        for (const std::string& name : o.nodes) {
            const scene::CompositionNode* node = composition->findNode(name);
            const scene::Transform world = composition->nodeWorldTransform(*node);
            Track& track = nodes[name];
            track.t.push_back(rounded(time.renderTime));
            track.position.push_back(vec3(world.position));
            const params::IParameter* visible = params.find("nodes/" + name + "/visible");
            track.visible.push_back(visible == nullptr || visible->finalComponent(0) > 0.5f);
        }
    }

    nlohmann::json doc;
    doc["source"] = o.project.empty() ? o.scene.string() : o.project.string();
    doc["fps"] = o.fps;
    doc["hz"] = o.fps / static_cast<double>(every);
    doc["seconds"] = o.seconds;
    for (auto& [name, track] : entities) {
        doc["entities"][name] = {{"t", std::move(track.t)},           {"position", std::move(track.position)},
                                 {"yaw", std::move(track.yaw)},       {"speed", std::move(track.speed)},
                                 {"activity", std::move(track.activity)}, {"visible", std::move(track.visible)}};
    }
    for (auto& [name, track] : nodes) {
        doc["nodes"][name] = {{"t", std::move(track.t)},
                              {"position", std::move(track.position)},
                              {"visible", std::move(track.visible)}};
    }

    const std::string text = doc.dump();
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
