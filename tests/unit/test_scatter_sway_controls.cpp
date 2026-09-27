// ADR-938: how a scatter layer sways in the wind is a set of parameters, named for what the viewer sees.
//
// A layer's wind response (ADR-055: `ScatterLayer::motion` -- how much wind it catches, its tip travel,
// its stiffness and weight, ...) was scene data only. gv3-look found it on Glowmere Valley 3: the fan
// plants, big broad-leaved plants in the foreground of four shots, kept the source's stiffness and
// swung 0.9 cm in a gust, and the only way to loosen them was to edit the scene file.
//
// This checks the three things that make a control real (the owner's rule: anything visible must be
// findable and adjustable, under a name for what it does):
//   * reach -- each control where an artist looks, doing the panels' own arithmetic on the registered
//     set (the Parameters panel's `parameterSubGroup`, the Inspector's `inspectorRowLabel` under the
//     terrain a click on a plant selects), as test_emission_lanes.cpp's reach test does;
//   * it reaches the plant -- a slider, a key and a route move every part of its layer and nothing else,
//     through the species model the renderer evaluates (the GPU half is test_scatter_sway_gpu.cpp);
//   * it survives -- the base goes back into the layer, so a save keeps it, and none of the nine is
//     structural, so moving one replants nothing.

#include "assets/asset_registry.hpp"
#include "core/time.hpp"
#include "core/wind.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "scene/scatter_anchors.hpp"
#include "signals/signal_bus.hpp"
#include "support/gltf_fixture.hpp"
#include "support/temp_dir.hpp"
#include "ui/ui_logic.hpp"
#include "world/ecology.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace avgen;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

std::filesystem::path writeJson(const std::string& name, const std::string& text) {
    const auto path = testsupport::processTempDir() / ("avgen_sway_" + name + ".json");
    std::ofstream out(path);
    out << text;
    return path;
}

// A valley with ferns that sway (an authored `motion`, the GV3 fan plants' first numbers) and stones
// that do not (no `motion`), in a breeze.
struct Valley {
    std::filesystem::path glb = testsupport::writeTriangleGlb("sway_tri");
    std::filesystem::path path;
    assets::AssetRegistry registry{testsupport::processTempDir()};
    std::unique_ptr<scene::Composition> comp;
    params::ParameterSet params;
    params::Modulator modulator;

    Valley() {
        std::string text = R"({
          "format": "avgen-scene", "version": 1, "name": "sway",
          "wind": { "enabled": true, "direction": 0.62, "speed": 0.74, "gustAmount": 0.52, "gustScale": 38.0,
                    "gustSpeed": 4.0, "turbulence": 0.22 },
          "nodes": [
            { "name": "valley", "kind": "terrain", "world": { "name": "small", "size": [60, 60], "features": [] },
              "terrain": { "chunkSize": 30.0, "resolution": 8, "lodLevels": 1, "viewDistance": 200.0 },
              "scatter": [
                { "name": "ferns", "asset": "@GLB@", "densities": { "meadow": 0.02 }, "height": 1.2,
                  "motion": { "stiffness": 2.4, "mass": 1.2, "damping": 0.9, "windSensitivity": 0.65,
                              "bendLimit": 0.16, "tipAmplitude": 0.08, "gustResponse": 1.2 } },
                { "name": "stones", "asset": "@GLB@", "densities": { "meadow": 0.01 } } ] }
          ]
        })";
        for (std::size_t at = text.find("@GLB@"); at != std::string::npos; at = text.find("@GLB@")) {
            text.replace(at, 5, glb.filename().string());
        }
        path = writeJson("valley", text);
        auto loaded = scene::Composition::loadFile(path.filename(), registry);
        if (!loaded) {
            FAIL(loaded.error().message);
        }
        comp = std::move(*loaded);
        comp->attach(params, modulator);
        comp->update(FrameTime{});
    }
    ~Valley() {
        std::filesystem::remove(path);
        std::filesystem::remove(glb);
    }

    // Every drawable the named layer grew (its parts share one placement set).
    std::vector<const scene::ProceduralGeometry*> parts(const std::string& layer) const {
        std::vector<const scene::ProceduralGeometry*> out;
        const std::string prefix = scene::scatterObjectName("valley", layer);
        for (const scene::ProceduralGeometry& g : comp->scene().procedurals) {
            if (g.name.rfind(prefix, 0) == 0) {
                out.push_back(&g);
            }
        }
        return out;
    }
    // One frame as the engine runs it: the finals start from the bases, then the composition applies.
    void settle() {
        params.resetFinals();
        comp->update(FrameTime{});
    }
    params::Parameter<float>* sway(const std::string& layer, const std::string& leaf) {
        return params.findAs<float>("nodes/valley/scatter/" + layer + "/sway/" + leaf);
    }
};

struct Reach {
    const char* leaf;
    const char* label;
    bool routable;
};

// What an artist reads, in both panels.
const std::vector<Reach> kReach{
    {"windSensitivity", "catches the wind (0 = stands still)", true},
    {"tipAmplitude", "sway at the tip (x its height)", true},
    {"gustResponse", "takes the gusts (x)", true},
    {"stiffness", "stiffness (bends less, rings faster)", false},
    {"mass", "weight at the tip (rings slower)", false},
    {"damping", "settles (low keeps ringing, 1 stops at once)", true},
    {"bendLimit", "bends at most (x its height)", true},
    {"bendCurve", "bends along (1 = the whole stem, 3 = the tip)", true},
    {"amplitudeVariance", "difference between plants (+- of its sway)", true},
};

} // namespace

TEST_CASE("UI reach: every scatter layer's sway is a set of controls named for what the viewer sees",
          "[ecology][wind][ui][adr938]") {
    Valley v;
    REQUIRE(scene::scatterSwayControls().size() == kReach.size());
    for (const char* layer : {"ferns", "stones"}) {
        for (const Reach& r : kReach) {
            const std::string path = std::string("nodes/valley/scatter/") + layer + "/sway/" + r.leaf;
            INFO(path);
            params::IParameter* p = v.params.find(path);
            REQUIRE(p != nullptr);
            CHECK(p->flags().exposed);
            CHECK(p->flags().serialized);
            // The two that set how fast the plant rings refuse routes (phase = t x rate); a key or a
            // slider still moves them.
            CHECK(p->flags().modulatable == r.routable);
            // The Parameters panel: the `nodes` group, "<terrain>/scatter/<layer>/sway", the label.
            CHECK(p->group() == "nodes");
            CHECK(p->label() == r.label);
            CHECK(ui::parameterSubGroup(p->path(), p->group()) == std::string("valley/scatter/") + layer + "/sway");
            CHECK(ui::layerShowsPath(ui::AuthoringLayer::Intermediate, p->path()));
            // The World panel Inspector: a click on a fern selects the terrain that grew it, whose
            // section lists this under "scatter" as "<layer>/sway/<label>".
            const ui::InspectorPlace place = ui::inspectorPlace(p->path(), "nodes/valley/");
            CHECK(place.heading == "scatter");
            CHECK(ui::inspectorRowLabel(p->path(), place.cut, p->label()) ==
                  std::string(layer) + "/sway/" + r.label);
        }
    }
    const scene::Scene& s = v.comp->scene();
    for (std::size_t i = 0; i < s.procedurals.size(); ++i) {
        if (s.procedurals[i].name.rfind(scene::scatterObjectName("valley", "ferns"), 0) == 0) {
            const scene::CompositionNode* owner = v.comp->nodeForProcedural(i);
            REQUIRE(owner != nullptr);
            CHECK(owner->name == "valley");
        }
    }
    // The defaults are what the scene says: the ferns' authored response, and for stones -- which
    // author none -- the species defaults, under which nothing moves until "catches the wind" is raised.
    CHECK(v.sway("ferns", "stiffness")->base() == 2.4f);
    CHECK(v.sway("ferns", "tipAmplitude")->base() == 0.08f);
    CHECK(v.sway("ferns", "bendCurve")->base() == wind::VegetationMotion{}.bendCurve);
    CHECK(v.sway("stones", "windSensitivity")->base() == 0.0f);
}

TEST_CASE("a sway control moves every part of its layer through the species model, and nothing else",
          "[ecology][wind][composition][adr938]") {
    Valley v;
    const auto ferns = v.parts("ferns");
    const auto stones = v.parts("stones");
    REQUIRE_FALSE(ferns.empty());
    REQUIRE_FALSE(stones.empty());
    const wind::WindParams air = v.comp->scene().environment.wind;
    REQUIRE(air.active());

    // As authored, before anything moved: the control arm for every check below.
    const wind::MotionResponse authored = wind::motionResponse(air, ferns.front()->motion);
    REQUIRE(authored.steadyGain > 0.0f);
    for (const auto* g : ferns) {
        CHECK(g->motion.tipAmplitude == 0.08f);
        CHECK(g->motion.stiffness == 2.4f);
    }

    // A slider: the tip's travel 0.08 -> 0.20 (gv3-look's retune) scales every gain by 2.5, on
    // every part of the ferns.
    v.sway("ferns", "tipAmplitude")->setBase(0.20f);
    v.settle();
    for (const auto* g : ferns) {
        CHECK(g->motion.tipAmplitude == 0.20f);
        const wind::MotionResponse r = wind::motionResponse(air, g->motion);
        CHECK_THAT(r.steadyGain, WithinRel(authored.steadyGain * 2.5f, 1e-5f));
        CHECK_THAT(r.gustGain, WithinRel(authored.gustGain * 2.5f, 1e-5f));
    }
    // Stiffer and heavier: the ring moves (sqrt(k/m)) and the steady lean falls as 1/k.
    v.sway("ferns", "stiffness")->setBase(4.8f);
    v.sway("ferns", "mass")->setBase(0.6f);
    v.settle();
    const wind::MotionResponse stiff = wind::motionResponse(air, ferns.front()->motion);
    CHECK_THAT(stiff.flutterOmega, WithinRel(std::sqrt(4.8f / 0.6f), 1e-5f));
    CHECK(stiff.steadyGain < authored.steadyGain * 2.5f);
    // ...and the stones, which nothing touched, are exactly as they were.
    for (const auto* g : stones) {
        CHECK(g->motion.tipAmplitude == wind::VegetationMotion{}.tipAmplitude);
        CHECK(g->motion.windSensitivity == 0.0f);
        CHECK_FALSE(g->motion.active());
    }
    // A still layer starts to sway when it is told to catch the wind: the stones' parts go active.
    v.sway("stones", "windSensitivity")->setBase(0.5f);
    v.settle();
    for (const auto* g : stones) {
        CHECK(g->motion.active());
    }
}

TEST_CASE("a route reaches a sway control the same frame, and one to the flutter's rate is refused",
          "[ecology][wind][composition][modulation][adr938]") {
    Valley v;
    signals::SignalBus bus;
    const signals::SignalId breath = bus.declare("test.breath");
    v.modulator.clearRoutes();
    params::ModRoute tip;
    tip.source = "test.breath";
    tip.target = "nodes/valley/scatter/ferns/sway/tipAmplitude";
    tip.amount = 0.1f;
    v.modulator.addRoute(tip);
    REQUIRE(v.modulator.bind(bus, v.params).has_value());
    const auto frame = [&](float value) {
        bus.set(breath, value);
        v.params.resetFinals();
        v.modulator.applyRoutes(bus, v.params, 1.0 / 60.0);
        v.comp->update(FrameTime{});
        return v.parts("ferns").front()->motion.tipAmplitude;
    };
    CHECK(frame(0.0f) == 0.08f); // the control: a route at rest moves nothing
    CHECK_THAT(frame(1.0f), WithinAbs(0.18f, 1e-6f));
    CHECK(frame(0.0f) == 0.08f);

    // The stiffness sets the ring's rate: a route to it is refused at bind, by name, and moves nothing.
    params::ModRoute rate;
    rate.source = "test.breath";
    rate.target = "nodes/valley/scatter/ferns/sway/stiffness";
    rate.amount = 2.0f;
    v.modulator.addRoute(rate);
    const auto bound = v.modulator.bind(bus, v.params);
    REQUIRE_FALSE(bound.has_value());
    CHECK(bound.error().message.find("not modulatable") != std::string::npos);
    frame(1.0f);
    CHECK(v.parts("ferns").front()->motion.stiffness == 2.4f);
}

TEST_CASE("a sway control survives a save and replants nothing", "[ecology][wind][composition][adr938]") {
    Valley v;
    // The nine are per frame: none of them may move a layer's structural hash, which seeds every
    // instance's variation and keys the terrain's cache.
    world::ScatterLayer layer;
    layer.name = "ferns";
    const std::uint64_t hash = layer.structuralHash();
    for (const scene::ScatterSwayControl& c : scene::scatterSwayControls()) {
        INFO(c.leaf);
        world::ScatterLayer moved = layer;
        moved.motion.*c.member = moved.motion.*c.member * 1.7f + 0.3f;
        CHECK(moved.structuralHash() == hash);
    }

    // The base goes back into the authored layer, so the scene the composition writes keeps it.
    const std::size_t before = v.parts("ferns").size();
    v.sway("ferns", "tipAmplitude")->setBase(0.20f);
    v.sway("ferns", "gustResponse")->setBase(1.6f);
    v.settle();
    CHECK(v.parts("ferns").size() == before);
    const nlohmann::json doc = v.comp->toJson();
    const nlohmann::json* ferns = nullptr;
    for (const nlohmann::json& node : doc.at("nodes")) {
        if (node.value("name", "") == "valley") {
            for (const nlohmann::json& l : node.at("scatter")) {
                if (l.value("name", "") == "ferns") {
                    ferns = &l;
                }
            }
        }
    }
    REQUIRE(ferns != nullptr);
    CHECK(ferns->at("motion").at("tipAmplitude").get<float>() == 0.20f);
    CHECK(ferns->at("motion").at("gustResponse").get<float>() == 1.6f);
    CHECK(ferns->at("motion").at("stiffness").get<float>() == 2.4f); // untouched, as authored
}
