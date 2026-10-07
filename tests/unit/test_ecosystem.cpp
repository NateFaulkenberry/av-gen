// ADR-1200: the ecosystem block (scene/ecosystem.hpp) -- the first Environment's scene side.
//
// What the CPU can settle: the block's JSON (round trip, refusal of what it does not know), its templates, its
// parameters (registered from the authored values, reaching the scene's live copy every frame), and that a scene
// file carries it through the Composition. The pixels are tests/rendering/test_ecosystem_gpu.cpp.

#include "assets/asset_registry.hpp"
#include "core/time.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "scene/ecosystem.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <string>

using namespace avgen;
using Catch::Matchers::WithinAbs;
namespace fs = std::filesystem;

namespace {

nlohmann::json layerJson(const std::string& templatePath) {
    return nlohmann::json{{"name", "polyps"},
                          {"hosts", {"mats"}},
                          {"template", templatePath},
                          {"color", {0.04, 0.38, 1.0}},
                          {"excitedColor", {1.0, 0.1, 0.6}},
                          {"intensity", 0.4},
                          {"excitedIntensity", 9.0},
                          {"responseField", "prop"},
                          {"responseThreshold", 0.05},
                          {"lagField", "lag"},
                          {"travel", 0.5},
                          {"wakeField", "wake"},
                          {"wakeGain", 2.0},
                          {"breath", 0.3},
                          {"pulseRate", 2.0},
                          {"sparsity", 0.25},
                          {"maxDistance", 250.0},
                          {"nearFade", 3.0}};
}

fs::path writeTemplate(const std::string& name, const nlohmann::json& points) {
    const fs::path dir = fs::temp_directory_path() / "avgen-ecosystem-test";
    fs::create_directories(dir);
    const fs::path file = dir / name;
    std::ofstream(file) << nlohmann::json{{"points", points}}.dump();
    return file;
}

} // namespace

TEST_CASE("an ecosystem block round-trips through JSON, and refuses what it does not know", "[ecosystem][json]") {
    nlohmann::json block{{"spriteRadius", 2.0}, {"maxSprites", 1024}, {"layers", {layerJson("t.emit.json")}}};
    auto eco = scene::Ecosystem::fromJson(block);
    REQUIRE(eco);
    REQUIRE(eco->layers.size() == 1);
    const scene::EmitterLayer& l = eco->layers[0];
    CHECK(l.name == "polyps");
    CHECK(l.hosts == std::vector<std::string>{"mats"});
    CHECK(l.excitedColor.x == 1.0f);
    CHECK(l.travel == 0.5f);
    CHECK(l.wakeField == "wake");
    CHECK(l.wakeGain == 2.0f);
    CHECK(l.nearFade == 3.0f);
    CHECK(eco->spriteRadius == 2.0f);

    auto again = scene::Ecosystem::fromJson(eco->toJson());
    REQUIRE(again);
    CHECK(again->toJson() == eco->toJson());

    SECTION("an unknown key is refused (a typo would otherwise be a silent no-op)") {
        nlohmann::json bad = block;
        bad["layers"][0]["intensty"] = 1.0;
        auto r = scene::Ecosystem::fromJson(bad);
        REQUIRE_FALSE(r);
        CHECK(r.error().message.find("intensty") != std::string::npos);
    }
    SECTION("a layer needs hosts and a template") {
        nlohmann::json bad = block;
        bad["layers"][0].erase("hosts");
        CHECK_FALSE(scene::Ecosystem::fromJson(bad));
        bad = block;
        bad["layers"][0].erase("template");
        CHECK_FALSE(scene::Ecosystem::fromJson(bad));
    }
    SECTION("ranges are enforced") {
        nlohmann::json bad = block;
        bad["layers"][0]["travel"] = 1.5;
        CHECK_FALSE(scene::Ecosystem::fromJson(bad));
        bad = block;
        bad["layers"][0]["size"] = 0.0;
        CHECK_FALSE(scene::Ecosystem::fromJson(bad));
        bad = block;
        bad["spriteRadius"] = 0.1;
        CHECK_FALSE(scene::Ecosystem::fromJson(bad));
    }
    SECTION("two layers of one name are refused (a name is half a parameter path)") {
        nlohmann::json bad = block;
        bad["layers"].push_back(layerJson("t.emit.json"));
        CHECK_FALSE(scene::Ecosystem::fromJson(bad));
    }
    SECTION("a single host may be written as a string") {
        nlohmann::json one = block;
        one["layers"][0]["hosts"] = "mats";
        auto r = scene::Ecosystem::fromJson(one);
        REQUIRE(r);
        CHECK(r->layers[0].hosts == std::vector<std::string>{"mats"});
    }
}

TEST_CASE("an emitter template is a list of [x, y, z, v, u, radius]", "[ecosystem][json]") {
    auto ok = scene::emitterTemplateFromJson(nlohmann::json{{"points", {{0.0, 1.0, 2.0, 0.5, 0.25, 0.02}}}});
    REQUIRE(ok);
    REQUIRE(ok->size() == 1);
    CHECK((*ok)[0].position.z == 2.0f);
    CHECK((*ok)[0].v == 0.5f);
    CHECK((*ok)[0].radius == 0.02f);
    CHECK_FALSE(scene::emitterTemplateFromJson(nlohmann::json{{"points", {{0.0, 1.0, 2.0}}}}));
    CHECK_FALSE(scene::emitterTemplateFromJson(nlohmann::json{{"points", {{0.0, 1.0, 2.0, 0.5, 0.25, -1.0}}}}));
    CHECK_FALSE(scene::emitterTemplateFromJson(nlohmann::json::array()));

    const fs::path file = writeTemplate("one.emit.json", {{0.0, 0.0, 0.0, 0.0, 0.0, 0.05}});
    nlohmann::json block{{"layers", {layerJson(file.string())}}};
    auto eco = scene::Ecosystem::fromJson(block);
    REQUIRE(eco);
    CHECK_FALSE(eco->active()); // nothing loaded yet
    REQUIRE(eco->loadTemplates(fs::temp_directory_path()));
    REQUIRE(eco->layers[0].points);
    CHECK(eco->layers[0].points->size() == 1);
    CHECK(eco->layers[0].pointsHash != 0);
    CHECK(eco->active());
    eco->enabled = false;
    CHECK_FALSE(eco->active());

    nlohmann::json missing{{"layers", {layerJson("no-such-file.emit.json")}}};
    auto m = scene::Ecosystem::fromJson(missing);
    REQUIRE(m);
    CHECK_FALSE(m->loadTemplates(fs::temp_directory_path()));
}

TEST_CASE("a scene file's ecosystem reaches the scene, with its layers as live parameters",
          "[ecosystem][composition][params]") {
    const fs::path file = writeTemplate("two.emit.json", {{0.0, 0.0, 0.0, 0.0, 0.0, 0.05},
                                                          {0.0, 1.0, 0.0, 1.0, 0.5, 0.05}});
    nlohmann::json doc{{"format", "avgen-scene"}, {"version", 1}, {"name", "eco"},
                       {"nodes", {{{"name", "mats"}, {"kind", "procedural"},
                                   {"procedural", {{"source", {{"kind", "box"}}},
                                                   {"distribution", {{"kind", "points"},
                                                                     {"points", {{0, 0, -10, 0, 0, 0, 1, 1, 1, 1},
                                                                                 {2, 0, -10, 0, 0, 0, 1, 1, 1, 1}}}}}}}}}},
                       {"ecosystem", {{"layers", {layerJson(file.string())}}}}};
    assets::AssetRegistry registry;
    auto comp = scene::Composition::fromJson(doc, registry);
    REQUIRE(comp);
    REQUIRE((*comp)->ecosystem().layers.size() == 1);
    CHECK((*comp)->toJson().contains("ecosystem"));

    params::ParameterSet params;
    params::Modulator modulator;
    (*comp)->attach(params, modulator);
    auto* intensity = params.findAs<float>("ecosystem/polyps/intensity");
    REQUIRE(intensity != nullptr);
    CHECK_THAT(intensity->value(), WithinAbs(0.4, 1e-6)); // the authored value: registering changes nothing
    REQUIRE(params.find("ecosystem/polyps/excitedColor") != nullptr);
    REQUIRE(params.find("ecosystem/polyps/enabled") != nullptr);
    REQUIRE(params.find("ecosystem/polyps/wakeGain") != nullptr);

    FrameTime t{};
    t.renderTime = 1.0;
    (*comp)->update(t);
    const scene::Scene& s = (*comp)->scene();
    REQUIRE(s.ecosystem.layers.size() == 1);
    CHECK(s.ecosystem.active());
    CHECK_THAT(s.ecosystem.layers[0].intensity, WithinAbs(0.4, 1e-6));
    REQUIRE(s.ecosystem.layers[0].points);
    CHECK(s.ecosystem.layers[0].points->size() == 2);

    // A route, MIDI or the editor moves the parameter; the next frame's scene carries it.
    intensity->setBaseComponent(0, 3.0f);
    params.resetFinals();
    (*comp)->update(t);
    CHECK_THAT((*comp)->scene().ecosystem.layers[0].intensity, WithinAbs(3.0, 1e-6));
    auto* on = params.findAs<bool>("ecosystem/polyps/enabled");
    REQUIRE(on != nullptr);
    on->setBaseComponent(0, 0.0f);
    params.resetFinals();
    (*comp)->update(t);
    CHECK_FALSE((*comp)->scene().ecosystem.active());
}
