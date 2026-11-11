// The Throw Lab (CPD-R7): a crest throwing an alien, measured frame by frame on one time axis. A hidden [measure] case
// over a project `tools/throw_lab.py` writes (a flat tile grid, one scripted wave, the art's alien, no music), so the
// timing of the crouch, the launch, the flight and the landing against the wave can be refined in minutes.

#include "app/engine.hpp"
#include "core/time.hpp"
#include "entity/entity.hpp"
#include "scene/composition.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

using namespace avgen;

TEST_CASE("the throw lab: the tile, the feet, the animation and the launch on one time axis (measure)",
          "[.][tiles][hop][measure][throwlab]") {
    // AVGEN_THROWLAB_PROJECT: the lab project; AVGEN_THROWLAB_CSV: where to write; AVGEN_THROWLAB_TO (s, default 12);
    // AVGEN_THROWLAB_FPS (default 60); AVGEN_THROWLAB_BODY (the entity's node, default alien00). One row per frame:
    //   t; tile: the drawn top of the tile under the body (the ground the renderer draws, springs included);
    //   wave: the same without the landings (the crest alone); body: the simulated body's y; root: the drawn root's y
    //   (after ADR-1991's lift); feet: the lowest posed low vertex's y (where the drawn feet are); state, clip time,
    //   the state fading out and the blend weight; activity; airborne; launch (1 on the first airborne frame).
    const char* projectPath = std::getenv("AVGEN_THROWLAB_PROJECT");
    const char* csvPath = std::getenv("AVGEN_THROWLAB_CSV");
    if (projectPath == nullptr || csvPath == nullptr) SKIP("set AVGEN_THROWLAB_PROJECT and AVGEN_THROWLAB_CSV");
    const auto env = [](const char* k, double d) { return std::getenv(k) != nullptr ? std::atof(std::getenv(k)) : d; };
    const double fps = env("AVGEN_THROWLAB_FPS", 60.0);
    const double t1 = env("AVGEN_THROWLAB_TO", 12.0);
    const std::string who = std::getenv("AVGEN_THROWLAB_BODY") != nullptr ? std::getenv("AVGEN_THROWLAB_BODY") : "alien00";
    app::Engine engine{app::EngineMode::Offline};
    REQUIRE(engine.loadProject(projectPath).has_value());
    std::ofstream out(csvPath);
    out << "t,tile,wave,body,root,feet,state,clipTime,fading,blend,activity,airborne,launch\n";
    bool was = false;
    for (long long k = 0; k <= static_cast<long long>(t1 * fps); ++k) {
        const double t = static_cast<double>(k) / fps;
        engine.update(FrameTime{t, k == 0 ? 0.0 : 1.0 / fps, static_cast<std::uint64_t>(k)});
        scene::Composition* comp = engine.composition();
        REQUIRE(comp != nullptr);
        const entity::Entity* body = nullptr;
        for (const auto& e : comp->entityWorld().entities()) {
            if (e->name() == who) body = e.get();
        }
        REQUIRE(body != nullptr);
        const world::GroundSource* g = comp->entityWorld().ground();
        REQUIRE(g != nullptr);
        const entity::EntityState& st = body->state();
        const glm::vec3 p = st.position();
        const scene::CompositionNode* node = comp->findNode(who);
        REQUIRE(node != nullptr);
        float root = std::nanf("");
        float feet = std::nanf("");
        std::string state = "-";
        std::string fading = "-";
        float clipTime = std::nanf("");
        float blend = 1.0f;
        if (!node->rigs.empty() && node->rigs.front() < comp->scene().rigs.size()) {
            const scene::RigId rig = node->rigs.front();
            const scene::SkinnedRig& r = comp->scene().rigs[rig];
            state = std::string(r.player.currentState());
            fading = std::string(r.player.fadingState());
            clipTime = r.player.stateTime(r.clips, t);
            blend = r.player.blendWeight(t);
            feet = comp->lowestFootY(rig);
            for (const scene::Entity& e : comp->scene().entities) {
                if (e.rig == rig) {
                    root = e.transform.position.y;
                    break;
                }
            }
        }
        const bool launch = st.airborne && !was;
        was = st.airborne;
        out << t << "," << g->at(p.x, p.z, t).height << "," << g->landscape(p.x, p.z, t).height << "," << p.y << ","
            << root << "," << feet << "," << (state.empty() ? "-" : state) << "," << clipTime << ","
            << (fading.empty() ? "-" : fading) << "," << blend << "," << entity::activityName(st.activity) << ","
            << (st.airborne ? 1 : 0) << "," << (launch ? 1 : 0) << "\n";
    }
}
