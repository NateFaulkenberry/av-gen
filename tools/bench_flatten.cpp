// TEMPORARY DIAGNOSTIC (docs/investigations/ui-responsiveness.md, phase 1).
//
// Measures the cost of `Composition::rebuild()` -- "the single most expensive thing the editor does
// on the main thread" -- on a real world, and specifically the cost of the flatten that
// `Composition::setHeroes` forces when a user stars one object.
//
// Three arms, so the number means something:
//   cold   the first rebuild, with no TerrainProducts cache
//   warm   a rebuild with the terrain cache hot and NOTHING changed
//   star   a rebuild caused by setHeroes with the SAME hero list -- the user's star toggle
//
// CPU only -- no GPU, no window, no renderer. Reported as a MINIMUM over repeats (ADR-170).
//
// DELETE THIS FILE, and its two lines in tools/CMakeLists.txt, once the question is answered.

#include "assets/asset_registry.hpp"
#include "core/log.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

using namespace avgen;
namespace fs = std::filesystem;

namespace {
double msSince(std::chrono::steady_clock::time_point t) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
}
} // namespace

int main(int argc, char** argv) {
    const fs::path scenePath =
        argc > 1 ? fs::path(argv[1]) : fs::path("examples/world/glowmere-valley-2.scene.json");
    const int repeats = argc > 2 ? std::atoi(argv[2]) : 5;

    log::setLevel(log::Level::Info);
    assets::AssetRegistry registry{scenePath.parent_path()};
    auto loaded = scene::Composition::loadFile(scenePath, registry);
    if (!loaded) {
        std::fprintf(stderr, "could not load '%s': %s\n", scenePath.string().c_str(),
                     loaded.error().message.c_str());
        return 1;
    }
    scene::Composition& comp = **loaded;
    params::ParameterSet params;
    params::Modulator modulator;
    comp.attach(params, modulator);
    if (comp.nodes().empty()) {
        std::fprintf(stderr, "scene has no nodes\n");
        return 1;
    }
    // `ensureBuilt()` is private; `nodeCorners` is the public call that forces it. Naming a real
    // node matters -- it returns early on an unknown name and would flatten nothing.
    const std::string anyNode = comp.nodes().front()->name;
    const auto forceBuild = [&] { (void)comp.nodeCorners(anyNode); };

    // Arm 1: cold. Guarded, because a rebuild that did not happen times as ~0 and would read as a
    // fast flatten rather than as no flatten at all (ADR-182).
    const std::uint64_t beforeCold = comp.flattenCount();
    const auto coldStart = std::chrono::steady_clock::now();
    forceBuild();
    const double coldMs = msSince(coldStart);
    const std::uint64_t afterCold = comp.flattenCount();

    std::printf("scene: %s\n", scenePath.string().c_str());
    std::printf("  nodes %zu, heroes %zu\n", comp.nodes().size(), comp.heroes().size());
    std::printf("\ncold arm: %.1f ms  [flattens %llu -> %llu, so it rebuilt %s]\n", coldMs,
                static_cast<unsigned long long>(beforeCold),
                static_cast<unsigned long long>(afterCold),
                afterCold > beforeCold ? "YES" : "NO -- this arm measured nothing");

    // Arm 2: warm. Nothing changed; this is the floor a cached rebuild cannot go below.
    double warmBest = 1e30;
    for (int r = 0; r < repeats; ++r) {
        comp.setComposition(comp.composition()); // dirties without changing anything
        const std::uint64_t before = comp.flattenCount();
        const auto start = std::chrono::steady_clock::now();
        forceBuild();
        const double ms = msSince(start);
        if (comp.flattenCount() == before) {
            std::fprintf(stderr, "warm arm %d did not rebuild; the probe is vacuous\n", r);
            return 1;
        }
        warmBest = std::min(warmBest, ms);
    }
    std::printf("warm flatten (terrain cache hot, no change), min of %d: %.1f ms\n", repeats, warmBest);

    // Arm 3: the star. setHeroes with the same list -- exactly what the UI's star toggle does.
    auto heroes = comp.heroes();
    double starBest = 1e30;
    for (int r = 0; r < repeats; ++r) {
        const std::uint64_t before = comp.flattenCount();
        const auto start = std::chrono::steady_clock::now();
        if (auto ok = comp.setHeroes(heroes); !ok) {
            std::fprintf(stderr, "setHeroes failed: %s\n", ok.error().message.c_str());
            return 1;
        }
        forceBuild();
        const double ms = msSince(start);
        if (comp.flattenCount() == before) {
            std::fprintf(stderr, "star arm %d did not rebuild; the probe is vacuous\n", r);
            return 1;
        }
        starBest = std::min(starBest, ms);
    }
    std::printf("STAR a hero (setHeroes + flatten),  min of %d: %.1f ms\n", repeats, starBest);
    std::printf("\ntotal flattens performed: %llu\n",
                static_cast<unsigned long long>(comp.flattenCount()));
    std::printf("\nA frame that does this also runs the rest of Engine::update and then builds the\n"
                "UI, on the same thread, before anything is presented.\n");
    return 0;
}
