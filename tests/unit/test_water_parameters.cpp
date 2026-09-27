// A terrain node's water parameters leave with the node (ADR-350's ten, found by the GV3 revision's
// water audit, 2026-09-26).
//
// ADR-350 made ten more `WaterSettings` fields parameters -- clarity, maxOpacity, fresnel, reflection,
// roughness, refraction, rippleScale, shallowDepth, shallowColor, deepColor -- and added them to the
// registrar and to the per-frame copy, but not to the two hand-kept lists that undo a registration:
// `unregisterNodeParameters`' suffix table and `detach`'s pointer sweep. So:
//   * removing a terrain node left all ten registered, and a node added later under the same name
//     was handed the dead node's parameters -- `ParameterSet::add` returns an existing path as it is,
//     with whatever value a route or an edit had left in it;
//   * detaching left ten cached pointers into the set the composition had just left, which the next
//     `update` read. That is the heap-use-after-free `test_composition.cpp` describes for the day/night
//     group, one subsystem over.
//
// Both tests sweep every path under `nodes/<terrain>/water/` rather than naming ten, because a list of
// ten is the shape of the defect: the next field somebody makes routable is the one a list forgets.

#include "assets/asset_registry.hpp"
#include "core/time.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <memory>
#include <string>
#include <vector>

using namespace avgen;

namespace {

// A small world (the shipped generator at 160 m has a river in it) so a build takes milliseconds.
nlohmann::json terrainScene(float clarity) {
    return nlohmann::json::parse(R"({
      "format": "avgen-scene", "version": 1, "name": "water-params",
      "camera": { "mode": 1, "position": [0, 30, 60], "target": [0, 0, -60], "fov": 50.0 },
      "nodes": [
        { "name": "sea", "kind": "terrain",
          "world": { "name": "small", "size": [160, 160] },
          "terrain": { "chunkSize": 40.0, "resolution": 8, "lodLevels": 2,
                       "lodDistance": 50.0, "viewDistance": 400.0,
                       "water": { "enabled": true, "clarity": )" +
                                 std::to_string(clarity) + R"( } } }
      ]
    })");
}

std::vector<std::string> waterPaths(const params::ParameterSet& params, const std::string& node) {
    const std::string prefix = "nodes/" + node + "/water/";
    std::vector<std::string> out;
    for (const params::IParameter* p : params.ordered()) {
        if (p->path().starts_with(prefix)) {
            out.push_back(p->path());
        }
    }
    return out;
}

} // namespace

TEST_CASE("removing a terrain node unregisters every water parameter it registered",
          "[unit][water][parameters]") {
    assets::AssetRegistry registry;
    auto loaded = scene::Composition::fromJson(terrainScene(1.25f), registry);
    REQUIRE(loaded.has_value());
    scene::Composition& comp = **loaded;
    params::ParameterSet params;
    params::Modulator modulator;
    comp.attach(params, modulator);

    // The premise: the node really did register water parameters, ADR-350's among them. Without it
    // the sweep below would pass on a node that registered none.
    const std::vector<std::string> before = waterPaths(params, "sea");
    INFO(before.size() << " water parameters registered");
    REQUIRE(before.size() >= 17);
    REQUIRE(params.find("nodes/sea/water/clarity") != nullptr);

    // Move one of ADR-350's, as a route or an edit would, so a survivor would be visible by value too.
    auto* clarity = params.findAs<float>("nodes/sea/water/clarity");
    REQUIRE(clarity != nullptr);
    clarity->setBase(7.0f);

    REQUIRE(comp.removeNode("sea"));
    std::string leaked;
    for (const std::string& path : waterPaths(params, "sea")) {
        leaked += path + " ";
    }
    INFO("survived removeNode: " << leaked);
    CHECK(leaked.empty());

    // And the consequence that made it a defect rather than a leak: a new node under the same name
    // gets its own authored values, not the dead node's.
    auto replacement = scene::Composition::fromJson(terrainScene(2.0f), registry);
    REQUIRE(replacement.has_value());
    std::unique_ptr<scene::CompositionNode> authored = (*replacement)->detachNode("sea");
    REQUIRE(authored != nullptr);
    REQUIRE(authored->terrain.water.clarity == 2.0f);
    REQUIRE(comp.addNode(std::move(*authored)).has_value());
    auto* fresh = params.findAs<float>("nodes/sea/water/clarity");
    REQUIRE(fresh != nullptr);
    CHECK(fresh->base() == 2.0f);
}

TEST_CASE("a detached composition's water follows its authored values, not the set it left",
          "[unit][water][parameters]") {
    // The shape of test_composition.cpp's "a detached composition follows its authored values": the
    // parameters stay ALIVE and move after the detach, so a pointer that survived `detach` produces a
    // wrong value, which a CHECK can see without a sanitizer.
    assets::AssetRegistry registry;
    auto loaded = scene::Composition::fromJson(terrainScene(1.25f), registry);
    REQUIRE(loaded.has_value());
    scene::Composition& comp = **loaded;
    params::ParameterSet params;
    params::Modulator modulator;
    comp.attach(params, modulator);
    comp.update(FrameTime{});
    REQUIRE(comp.scene().waters.size() == 1);
    const scene::WaterSettings authored = comp.scene().waters[0].settings;

    // Every parameter under the node's water, each moved to the top of its slider -- somewhere the
    // scene never authored -- and back to its default, which is the authored value it registered with.
    std::vector<params::IParameter*> water;
    for (const std::string& path : waterPaths(params, "sea")) {
        water.push_back(params.find(path));
    }
    INFO(water.size() << " water parameters");
    REQUIRE(water.size() >= 17);
    const auto moveAll = [&](bool away) {
        for (params::IParameter* p : water) {
            for (std::size_t i = 0; i < p->componentCount(); ++i) {
                p->setBaseComponent(i, away ? p->softMax(i) : p->defaultComponent(i));
            }
        }
        params.resetFinals();
        comp.update(FrameTime{});
    };

    // The premise (ADR-182): attached, the water follows the set -- clarity among them.
    moveAll(true);
    REQUIRE(comp.scene().waters[0].settings.clarity != authored.clarity);
    moveAll(false);

    comp.detach();
    REQUIRE_FALSE(comp.attached());
    moveAll(true);
    const scene::WaterSettings after = comp.scene().waters[0].settings;
    CHECK(after.clarity == authored.clarity);
    CHECK(after.maxOpacity == authored.maxOpacity);
    CHECK(after.fresnel == authored.fresnel);
    CHECK(after.reflection == authored.reflection);
    CHECK(after.roughness == authored.roughness);
    CHECK(after.refraction == authored.refraction);
    CHECK(after.rippleScale == authored.rippleScale);
    CHECK(after.shallow == authored.shallow);
    CHECK(after.shallowColor == authored.shallowColor);
    CHECK(after.deepColor == authored.deepColor);
    CHECK(after.glow == authored.glow);
    CHECK(after.ripple == authored.ripple);
}
