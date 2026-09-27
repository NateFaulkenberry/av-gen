// avgen_song_cut -- the Director's Song Mode cut of a project, as JSON, on the CPU (ADR-923).
//
//   avgen_song_cut --project P.json [--song-plan PLAN.json] [--director k=v,...] [--out CUT.json]
//                  [--save-project OUT.json] [--save-scene OUT.scene.json]
//
//   --project P       load a project through `Engine::loadProject`, the path the application opens:
//                     its scene, its heroes, its cameras, its audio -- analysed, so the cut lands on
//                     the track's beat grid (ADR-896, ADR-921)
//   --song-plan PLAN  cut this plan instead of the project's own sections (its `sections`, and its
//                     `events` for the peaks, ADR-922)
//   --director K=V,.. the Auto-director panel's settings over the project's own, as `avgen
//                     --director` takes them; `mode` is Song whatever is asked
//   --out FILE        write the cut report there (default: stdout)
//   --save-project F  write the project with the cut installed (the framing, the aim follow, the
//                     shot spans); --save-scene the scene (the camera shot track lives there)
//
// The same cut `avgen --project P --director mode=song --song-plan PLAN --cut-report FILE` makes,
// without a window, a GPU or the GPU lock: Song Mode is a pure function of the plan, the heroes, the
// cameras and the grid, and the engine that holds them runs in Offline mode on the CPU. What a
// generator iterating on a plan wants: seconds, not a render.
//
// Exit status: 0 when the report was written; 1 on bad arguments, a load failure or a cut the
// director refused (the reason is printed).

#include "app/camera_director.hpp"
#include "app/engine.hpp"
#include "app/song_director.hpp"
#include "app/song_plan.hpp"
#include "core/log.hpp"
#include "scene/composition.hpp"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

void usage() {
    std::fprintf(stderr,
                 "usage: avgen_song_cut --project P.json [--song-plan PLAN.json] [--director k=v,...]\n"
                 "                      [--out CUT.json] [--save-project OUT.json] [--save-scene OUT.scene.json]\n");
}

} // namespace

int main(int argc, char** argv) {
    fs::path project;
    fs::path songPlan;
    std::string director;
    fs::path out;
    fs::path saveProject;
    fs::path saveScene;
    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        const auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : nullptr; };
        const char* v = nullptr;
        if (std::strcmp(a, "--project") == 0 && (v = next())) {
            project = v;
        } else if (std::strcmp(a, "--song-plan") == 0 && (v = next())) {
            songPlan = v;
        } else if (std::strcmp(a, "--director") == 0 && (v = next())) {
            director = v;
        } else if (std::strcmp(a, "--out") == 0 && (v = next())) {
            out = v;
        } else if (std::strcmp(a, "--save-project") == 0 && (v = next())) {
            saveProject = v;
        } else if (std::strcmp(a, "--save-scene") == 0 && (v = next())) {
            saveScene = v;
        } else {
            usage();
            return 1;
        }
    }
    if (project.empty()) {
        usage();
        return 1;
    }

    app::Engine engine(app::EngineMode::Offline);
    if (auto loaded = engine.loadProject(project); !loaded) {
        std::fprintf(stderr, "load failed: %s\n", loaded.error().message.c_str());
        return 1;
    }
    if (engine.composition() == nullptr) {
        std::fprintf(stderr, "%s has no scene to direct\n", project.string().c_str());
        return 1;
    }

    // The project's own settings, the command line's over them, and Song Mode whatever was asked:
    // this tool reports a Song Mode cut and nothing else.
    app::AutoDirectorSettings settings = engine.autoDirector();
    if (!director.empty()) {
        if (auto ok = app::applyDirectorArgs(settings, director); !ok) {
            std::fprintf(stderr, "%s\n", ok.error().message.c_str());
            return 1;
        }
    }
    settings.mode = app::DirectorMode::Song;
    if (auto ok = settings.validate(); !ok) {
        std::fprintf(stderr, "%s\n", ok.error().message.c_str());
        return 1;
    }

    std::optional<app::SongPlan> plan;
    if (!songPlan.empty()) {
        std::ifstream in(songPlan);
        if (!in) {
            std::fprintf(stderr, "--song-plan: cannot open %s\n", songPlan.string().c_str());
            return 1;
        }
        nlohmann::json doc;
        try {
            in >> doc;
        } catch (const std::exception& e) {
            std::fprintf(stderr, "--song-plan %s: %s\n", songPlan.string().c_str(), e.what());
            return 1;
        }
        auto parsed = app::songPlanFromJson(doc);
        if (!parsed) {
            std::fprintf(stderr, "--song-plan %s: %s\n", songPlan.string().c_str(), parsed.error().message.c_str());
            return 1;
        }
        plan = std::move(*parsed);
        engine.songPlan() = *plan;
    }

    const std::vector<world::HeroPoint> heroes = engine.composition()->heroes();
    app::SongDirection cut;
    auto installed = app::directEngine(engine, heroes, settings, plan ? &*plan : nullptr, &cut);
    if (!installed) {
        std::fprintf(stderr, "the director refused: %s\n", installed.error().message.c_str());
        return 1;
    }

    const std::string text = cut.report().dump(2) + "\n";
    if (out.empty()) {
        std::cout << text;
    } else {
        std::ofstream file(out);
        if (!file) {
            std::fprintf(stderr, "cannot write %s\n", out.string().c_str());
            return 1;
        }
        file << text;
    }
    // The settings the cut was made with, so a saved project re-cuts the same way.
    engine.autoDirector() = settings;
    if (!saveProject.empty()) {
        if (auto r = engine.saveProject(saveProject); !r) {
            std::fprintf(stderr, "save project: %s\n", r.error().message.c_str());
            return 1;
        }
    }
    if (!saveScene.empty()) {
        if (auto r = engine.saveComposition(saveScene); !r) {
            std::fprintf(stderr, "save scene: %s\n", r.error().message.c_str());
            return 1;
        }
    }
    std::fprintf(stderr, "song cut: %zu section(s), %zu shot(s), %zu cut(s) on a downbeat of %zu\n",
                 cut.sections.size(), cut.decisions.size(),
                 cut.report()["stats"]["cutsOnDownbeat"].get<std::size_t>(),
                 cut.decisions.empty() ? std::size_t{0} : cut.decisions.size() - 1);
    return 0;
}
