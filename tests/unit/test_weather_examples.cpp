// The eight shipped weather effects, checked for the failures that are silent.
//
// The point of this file is not that the scenes "work". It is that the three ways a scene in this
// repository fails WITHOUT SAYING SO are each checked by name:
//
//   1. A scene that does not parse. The application falls back to a default scene and the render
//      completes with a sequence hash -- which is how a colonnade authored with a `spacing` key
//      the Linear distribution does not read produced a perfectly good picture of an entirely
//      different scene during this work.
//   2. A particle system that names a field the scene does not define. `emitMaskField` resolving
//      to nothing leaves the mask OFF, which is the right behaviour and is indistinguishable by
//      eye from a mask that is currently letting everything through. This is ADR-375's and
//      ADR-392's defect, and the whole reason §77 says findable is not the same as reachable.
//   3. A particle system the renderer would refuse. `validateParticleSystem` is called by the
//      parser, so a scene that loads has already passed it -- but a scene that ships should not
//      depend on that being true tomorrow.

#include "assets/asset_registry.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "scene/particles.hpp"

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

fs::path weatherDir() {
    return fs::path(AVGEN_SOURCE_DIR) / "examples" / "weather";
}

const std::vector<std::string>& weatherScenes() {
    static const std::vector<std::string> kScenes = {
        "rain.scene.json",       "snow.scene.json",    "ashfall.scene.json", "weather-front.scene.json",
        "dust-motes.scene.json", "fireflies.scene.json", "pollen.scene.json", "season.scene.json",
    };
    return kScenes;
}

} // namespace

TEST_CASE("every shipped weather scene loads, and none of them is empty", "[weather][examples]") {
    // The premise: the directory is where this thinks it is. Without it a typo in the path turns
    // this whole file into eight cases that iterate over nothing and pass (ADR-182).
    REQUIRE(fs::is_directory(weatherDir()));
    REQUIRE(fs::is_regular_file(weatherDir() / "_stage.scene.json"));

    for (const std::string& name : weatherScenes()) {
        INFO("scene " << name);
        assets::AssetRegistry registry(weatherDir());
        auto loaded = scene::Composition::loadFile(weatherDir() / name, registry);
        REQUIRE(loaded.has_value());

        params::ParameterSet params;
        params::Modulator modulator;
        (*loaded)->attach(params, modulator);
        (*loaded)->update(FrameTime{});
        const scene::Scene& s = (*loaded)->scene();

        // Every one of these is a weather effect, so every one of them has particles. A scene that
        // parsed into no particle systems at all is the failure this case exists for.
        INFO("particle systems: " << s.particles.size());
        CHECK(s.particles.size() >= 1);
        for (const scene::ParticleSystem& ps : s.particles) {
            INFO("system " << ps.name);
            CHECK(ps.capacity > 0);
            CHECK(scene::validateParticleSystem(ps).has_value());
        }
    }
}

TEST_CASE("no weather system names a field its scene does not define", "[weather][examples][fields]") {
    // A subscription that names nothing must be a stated problem, not a still picture -- the
    // principle `world/world_effects/field_bus.hpp` is built on, applied to the one place ADR-520
    // added a name-by-string.
    std::string dangling;
    std::size_t masksChecked = 0;
    std::size_t forcesChecked = 0;

    for (const std::string& name : weatherScenes()) {
        assets::AssetRegistry registry(weatherDir());
        auto loaded = scene::Composition::loadFile(weatherDir() / name, registry);
        REQUIRE(loaded.has_value());
        params::ParameterSet params;
        params::Modulator modulator;
        (*loaded)->attach(params, modulator);
        (*loaded)->update(FrameTime{});
        const scene::Scene& s = (*loaded)->scene();

        for (const scene::ParticleSystem& ps : s.particles) {
            if (!ps.emitMaskField.empty()) {
                ++masksChecked;
                if (s.fields.indexOf(ps.emitMaskField) < 0) {
                    dangling += name + ":" + ps.name + " emitMaskField='" + ps.emitMaskField + "' ";
                }
            }
            for (const scene::FieldForce& f : ps.fieldForces) {
                if (!f.enabled || f.field.empty()) {
                    continue;
                }
                ++forcesChecked;
                if (s.fields.indexOf(f.field) < 0) {
                    dangling += name + ":" + ps.name + " fieldForce='" + f.field + "' ";
                }
            }
        }
    }

    // The premise, and it is the whole case: at least one scene really does name a field, so this
    // is not eight scenes with nothing to check reporting that nothing is wrong. `weather-front`
    // names two.
    INFO("emitMaskField subscriptions checked: " << masksChecked
         << ", fieldForce subscriptions checked: " << forcesChecked);
    REQUIRE(masksChecked >= 2);

    INFO("subscriptions naming a field the scene does not define: " << dangling);
    CHECK(dangling.empty());
}

TEST_CASE("the season macro's targets all name registered parameters", "[weather][examples][season]") {
    // §49 asks for one continuous parameter rather than four presets, and the whole of that
    // promise is 32 macro targets naming parameter paths by string. A target whose path resolves
    // to nothing is a season that moves everything except the one thing an artist is looking at,
    // and nothing anywhere reports it.
    const fs::path project = weatherDir() / "season.json";
    REQUIRE(fs::is_regular_file(project));
    std::ifstream in(project);
    REQUIRE(in.good());
    nlohmann::json pj;
    in >> pj;
    REQUIRE(pj.contains("worldMacros"));
    REQUIRE(pj["worldMacros"].is_array());
    REQUIRE(pj["worldMacros"].size() == 1);

    assets::AssetRegistry registry(weatherDir());
    auto loaded = scene::Composition::loadFile(weatherDir() / "season.scene.json", registry);
    REQUIRE(loaded.has_value());
    params::ParameterSet params;
    params::Modulator modulator;
    (*loaded)->attach(params, modulator);
    (*loaded)->update(FrameTime{});

    const nlohmann::json& targets = pj["worldMacros"][0]["targets"];
    REQUIRE(targets.is_array());
    // The premise: there are targets. A macro with none would satisfy every check below.
    REQUIRE(targets.size() >= 20);

    std::string missing;
    for (const nlohmann::json& t : targets) {
        REQUIRE(t.contains("path"));
        const std::string path = t["path"].get<std::string>();
        if (params.find(path) == nullptr) {
            missing += path + " ";
        }
    }
    INFO("season macro targets naming no parameter: " << missing);
    CHECK(missing.empty());
}
