// CP-D r2 (owner: "when the tile moves back up the feet clip through the tile"): a body standing on a tiles ground
// stands on the tile the GPU DRAWS -- its height, frame by frame through landings and their springs, against the tiles
// kernel's own record for the cell under it.

#include "assets/asset_registry.hpp"
#include "core/log.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/shader_library.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "rendering/procedural_renderer.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/composition.hpp"
#include "scene/generator.hpp"
#include "signals/signal_bus.hpp"
#include "support/temp_dir.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>

using namespace avgen;

TEST_CASE("Hopping bodies stand on the tile the kernel draws, through every landing", "[gpu][geography][feet]") {
    static bool logInit = false;
    if (!logInit) {
        log::init(log::Level::Warn);
        logInit = true;
    }
    auto ctx = gpu::Context::create(gpu::ContextDesc{});
    if (!ctx) {
        SKIP("no GPU adapter available: " << ctx.error().message);
    }
    gpu::ShaderLibrary shaders(**ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(**ctx, shaders);
    REQUIRE(renderer.init().has_value());

    const auto json = nlohmann::json::parse(R"({
        "format": "avgen-scene", "version": 1, "name": "feet",
        "camera": {"position": [0, 30, 40], "target": [0, 0, 0]},
        "environment": {"tileDynamics": {
            "waves": [ {"amplitude": 0.4, "wavelength": 36, "speed": 5, "onLand": 1} ],
            "spring": {"depth": 1.1, "frequency": 1.4, "damping": 0.16, "rippleSpeed": 16, "rippleRadius": 14}
        }},
        "nodes": [
            {"name": "ground", "kind": "procedural", "position": [0, 0, 0],
             "procedural": {"source": {"kind": "box", "size": [3.66, 1.2, 3.66]},
                "distribution": {"kind": "generator", "generator": {"name": "tiles", "version": 1,
                    "cellSize": 4.0, "viewDistance": 80.0, "tiles": {"seed": 3, "landThreshold": 0.2}}},
                "slabs": true}},
            {"name": "a", "kind": "orb", "position": [6, 0, 6]},
            {"name": "b", "kind": "orb", "position": [-10, 0, 3]},
            {"name": "c", "kind": "orb", "position": [2, 0, -12]}
        ],
        "entities": [
            {"name": "a", "node": "a", "seed": 5, "behaviors": [{"kind": "hop", "hopChance": 0.6, "pauseMax": 1.2, "strength": 1.2}]},
            {"name": "b", "node": "b", "seed": 9, "behaviors": [{"kind": "hop", "hopChance": 0.6, "pauseMax": 1.2, "strength": 1.2}]},
            {"name": "c", "node": "c", "seed": 13, "behaviors": [{"kind": "hop", "hopChance": 0.6, "pauseMax": 1.2, "strength": 1.2}]}
        ]})");
    assets::AssetRegistry registry{testsupport::processTempDir()};
    auto c = scene::Composition::fromJson(json, registry);
    REQUIRE(c.has_value());
    scene::Composition& comp = **c;
    params::ParameterSet params;
    params::Modulator modulator;
    signals::SignalBus bus;
    comp.attach(params, modulator);

    float worst = 0.0f;
    float worstAt = 0.0f;
    int standing = 0;
    int landings = 0;
    for (int f = 0; f <= 30 * 12; ++f) {
        FrameTime t{};
        t.renderTime = f / 30.0;
        t.deltaTime = f == 0 ? 0.0 : 1.0 / 30.0;
        t.frameIndex = static_cast<std::uint64_t>(f);
        params.resetFinals();
        comp.updateFields(t, bus, modulator);
        modulator.applyRoutes(bus, params, t.deltaTime);
        comp.updateBehaviour(t, bus);
        comp.update(t);
        if (f < 30) {
            continue;
        }
        const scene::Scene& s = comp.scene();
        REQUIRE(renderer.renderToImage(s, t, 64, 36).has_value());
        const scene::ProceduralGeometry* g = nullptr;
        for (const auto& p : s.procedurals) {
            if (p.distribution.generator.isTiles()) g = &p;
        }
        REQUIRE(g != nullptr);
        const scene::GeneratorWindow w = renderer.procedurals().generatorWindow(g->name);
        auto records = renderer.procedurals().readInstanceRecords(g->name);
        REQUIRE(records.has_value());
        landings = static_cast<int>(s.environment.tileDynamics.springs.size());
        for (const auto& e : comp.entityWorld().entities()) {
            const entity::EntityState& st = e->state();
            if (st.airborne || st.activity == entity::Activity::Walk || st.activity == entity::Activity::Run) {
                continue; // standing and landing bodies sit on their tile's top
            }
            const glm::vec3 p = st.position();
            const std::int32_t ix = static_cast<std::int32_t>(std::floor(p.x / g->distribution.generator.cellSize));
            const std::int32_t iz = static_cast<std::int32_t>(std::floor(p.z / g->distribution.generator.cellSize));
            const std::int32_t lx = ix - w.originX;
            const std::int32_t lz = iz - w.originZ;
            if (lx < 0 || lz < 0 || lx >= w.countX || lz >= w.countZ) continue;
            const auto& r = (*records)[static_cast<std::size_t>(lz) * static_cast<std::size_t>(w.countX) +
                                       static_cast<std::size_t>(lx)];
            const float top = r.scale.w; // the tile's top, generator metres (ADR-1970)
            ++standing;
            if (std::abs(p.y - top) > worst) {
                worst = std::abs(p.y - top);
                worstAt = static_cast<float>(t.renderTime);
            }
        }
    }
    CAPTURE(worst, worstAt, standing, landings);
    CHECK(standing > 100);
    CHECK(landings > 0);
    CHECK(worst < 1e-3f); // the body stands on the drawn top, in every frame of every landing
}
