// A character's pose after a project switch is the pose it has when its project is opened fresh -- in the ENGINE.
//
// Written while chasing ADR-1394: the alien project rendered after the Character Animation Lab drew differently from a
// fresh one, and the first question was whether the engine or the renderer carried it. This says the engine does not
// (the rigs' model-space joints and the entities' positions are identical); the renderer did, through per-object state
// keyed by name (tests/rendering/test_project_switch_gpu.cpp). Kept because it is the cheap half of that question.

#include "app/engine.hpp"
#include "core/time.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <sstream>

using namespace avgen;
namespace fs = std::filesystem;

namespace {
void play(app::Engine& e, double start, int frames) {
    FixedStepClock clock(30.0, start);
    e.setViewport(320, 180);
    for (int f = 0; f < frames; ++f) {
        e.update(e.tick(clock));
    }
}
std::string poseDifferences(const scene::Scene& a, const scene::Scene& b) {
    std::ostringstream out;
    if (a.entities.size() != b.entities.size()) {
        out << "entities " << a.entities.size() << " vs " << b.entities.size();
        return out.str();
    }
    for (std::size_t i = 0; i < a.entities.size(); ++i) {
        const auto& x = a.entities[i];
        const auto& y = b.entities[i];
        if (x.transform.position != y.transform.position) {
            out << x.name << " position; ";
        }
        if (x.rig < a.rigs.size() && y.rig < b.rigs.size()) {
            const auto& p = a.rigs[x.rig].scratchModel;
            const auto& q = b.rigs[y.rig].scratchModel;
            if (p.size() != q.size()) {
                out << x.name << " joint count; ";
                continue;
            }
            for (std::size_t j = 0; j < p.size(); ++j) {
                if (p[j] != q[j]) {
                    out << x.name << " joint " << j << "; ";
                    break;
                }
            }
        }
    }
    return out.str();
}
} // namespace

TEST_CASE("A character's pose does not depend on the project opened before it", "[integration][switch]") {
    const fs::path root = fs::path(AVGEN_SOURCE_DIR);
    if (!fs::exists(root / "assets/aliens/alien-scout.glb")) {
        SKIP("the private character assets are not present");
    }
    const fs::path probe = root / "examples/characters/alien.json";
    for (const char* before : {"examples/lab/character-animation-lab.json",
                               "examples/labs/character/character-intelligence-lab.scene.json"}) {
        INFO(before);
        app::Engine fresh(app::EngineMode::Offline);
        fresh.setLiveControl(false);
        REQUIRE(fresh.loadFile(probe).has_value());
        play(fresh, 3.0, 6);

        app::Engine switched(app::EngineMode::Offline);
        switched.setLiveControl(false);
        REQUIRE(switched.loadFile(root / before).has_value());
        play(switched, 1.0, 4);
        REQUIRE(switched.loadFile(probe).has_value());
        play(switched, 3.0, 6);
        const std::string diff = poseDifferences(fresh.scene(), switched.scene());
        INFO(diff);
        CHECK(diff.empty());
    }
}
