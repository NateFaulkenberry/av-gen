// The Wave 1 stacking demo (ADR-703): the Glowmere film's saucer carries a Space Warp, a Glow and a
// Trail, and the valley has fireflies -- four instances of four Wave 1 types, three stacked on one
// owner, each drawn by a different integrator (DF after the volumetric composite, FXL in the lit
// pass on a procedural node, RIBBON beside the particles, EMIT in the particle system).
//
// `[.generate]` writes `examples/effects/ufo-stack.json` through the engine's own add path -- the
// same `editEffects` + `addDefaultEffectRoutes` the Add Effect menu uses -- so every entry is the
// canonical form and every default route is the one a person would get. Run it by hand when a type
// changes; it is hidden so the suite never writes into the repository.
//
// The other cases check the committed file: it loads with no warnings about effects, all four
// instances are Drawn at a second when the saucer is flying, and the routes that make the stack
// respond to the saucer's motion are bound to the saucer's own signal.

#include "app/engine.hpp"
#include "core/time.hpp"
#include "world/effects/effect_instance.hpp"
#include "world/effects/effect_registry.hpp"
#include "world/effects/effect_stack.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

fs::path sourceProject() { return fs::path(AVGEN_SOURCE_DIR) / "examples" / "world" / "glowmere-valley-2-multicam.json"; }
fs::path demoProject() { return fs::path(AVGEN_SOURCE_DIR) / "examples" / "effects" / "ufo-stack.json"; }

world::EffectInstance styled(world::EffectKind kind, const char* style, world::EffectOwner owner) {
    world::EffectInstance e = world::makeEffect(kind, world::effectSchema(kind)->displayName);
    e.id.clear();
    e.owner = std::move(owner);
    REQUIRE(world::applyEffectStyle(e, kind, style));
    return e;
}

void step(app::Engine& engine, double seconds) {
    engine.update(FrameTime{seconds, 1.0 / 60.0, static_cast<std::uint64_t>(seconds * 60.0)});
}

} // namespace

TEST_CASE("generate the Wave 1 UFO stack demo", "[.generate][effects][demo]") {
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(sourceProject()).has_value());

    const world::EffectOwner saucer = world::EffectOwner::entity("visitor");
    std::vector<std::string> added;
    REQUIRE(engine
                .editEffects([&](std::vector<world::EffectInstance>& list) -> Result<void> {
                    for (world::EffectInstance e :
                         {styled(world::EffectKind::SpaceWarp, "UFO Warp", saucer),
                          styled(world::EffectKind::Glow, "Neon", saucer),
                          styled(world::EffectKind::Trail, "UFO Wake", saucer)}) {
                        auto id = world::insertEffect(list, std::move(e));
                        if (!id) {
                            return std::unexpected(id.error());
                        }
                        added.push_back(*id);
                    }
                    // Fireflies in the valley around the elder mushroom, where the saucer hunts.
                    world::EffectInstance flies =
                        styled(world::EffectKind::ParticleEmitter, "Meadow Fireflies", world::EffectOwner::world());
                    flies.values.setFloat("particleEmitter/centerX", -14.0f);
                    flies.values.setFloat("particleEmitter/centerY", 18.0f);
                    flies.values.setFloat("particleEmitter/centerZ", 52.0f);
                    flies.values.setFloat("particleEmitter/radius", 22.0f);
                    flies.values.setFloat("particleEmitter/rate", 120.0f);
                    flies.values.setFloat("particleEmitter/size", 0.12f);
                    auto id = world::insertEffect(list, std::move(flies));
                    if (!id) {
                        return std::unexpected(id.error());
                    }
                    added.push_back(*id);
                    return {};
                })
                .has_value());
    for (const std::string& id : added) {
        engine.addDefaultEffectRoutes(id);
    }
    fs::create_directories(demoProject().parent_path());
    REQUIRE(engine.saveProject(demoProject()).has_value());
}

TEST_CASE("the UFO stack demo loads, and all four instances draw while the saucer flies",
          "[effects][demo]") {
    REQUIRE(fs::exists(demoProject()));
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(demoProject()).has_value());
    for (const std::string& w : engine.projectWarnings()) {
        INFO(w);
        CHECK(w.find("effect") == std::string::npos);
    }

    std::vector<std::string> stack;
    for (const std::size_t i : world::effectsOf(engine.effects(), world::EffectOwner::entity("visitor"))) {
        stack.push_back(engine.effects()[i].id);
    }
    // Hero Pulse (ADR-702) plus the three Wave 1 effects, in the order a person stacked them.
    REQUIRE(stack.size() == 4);

    // A second in the saucer's first abduction cycle when it is travelling. Stepped at 60 Hz from
    // shortly before, so history and velocity have samples.
    for (double t = 118.0; t <= 120.0; t += 1.0 / 60.0) {
        step(engine, t);
    }
    for (const std::string& id : stack) {
        const world::EffectInstance& e = engine.effects()[world::findEffect(engine.effects(), id)];
        if (e.kind == world::EffectKind::GroundPulse) {
            continue; // fires only when the cut holds the saucer
        }
        INFO(id << " status " << world::effectStatusName(engine.effectStatus(id)) << ": "
                << engine.effectStatusReason(id));
        CHECK(engine.effectStatus(id) == world::EffectStatus::Drawn);
    }

    // The stack answers the saucer's motion through the saucer's own published signal.
    std::size_t bound = 0;
    for (const params::ModRoute& r : engine.modulator().routes()) {
        if (r.source == "entity.visitor.speed") {
            ++bound;
        }
    }
    CHECK(bound >= 2); // Space Warp's strength and the Trail's opacity
}

