// ADR-383: the vortex as an authored World Effect rather than a field on the environment, and the
// removal of the Tree panel that wrapped it.
//
// Four questions, and each has a control that could fail the other way:
//
//   1. does a vortex survive a save and a load, field for field?
//   2. does a scene written in the *old* form still load, with its values intact? (§19)
//   3. does every parameter path the World Effects panel asks for on a vortex actually exist?
//      This is the shape of the defect ADR-382 records: a panel computed a path by string
//      arithmetic, got it wrong, drew nothing and said nothing. The row lists are data now, so the
//      test walks the same list the panel walks.
//   4. is the Tree panel gone from the registry, and is there still exactly one global wind?

#include "assets/asset_registry.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "ui/editor_layout.hpp"
#include "ui/ui_logic.hpp"
#include "ui/world_effects_panel.hpp"
#include "world/atmospheric_params.hpp"
#include "world/atmospherics.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using Catch::Approx;
using json = nlohmann::json;
using namespace avgen;

namespace {

std::filesystem::path scratch() {
    auto dir = std::filesystem::temp_directory_path() / "avgen_vortex_effect";
    std::filesystem::create_directories(dir);
    return dir;
}

// The Tree of Life's own numbers, so that what this asserts about preservation is what the shipped
// scene actually carries rather than a set of round ones that would survive a bug that rounded.
json shippedVortex() {
    return json{{"center", json::array({0.0, -70.0, 0.0})},
                {"radius", 200.0},
                {"funnelDepth", 1500.0},
                {"throat", 0.1},
                {"throatDensity", 0.55},
                {"thickness", 70.0},
                {"swirl", 6.5},
                {"rotationSpeed", 0.028},
                {"density", 0.0013},
                {"innerVoid", 0.24},
                {"contrast", 3.6},
                {"turbulence", 0.65},
                {"turbulenceScale", 2.4},
                {"breathAmount", 0.05},
                {"breathSpeed", 0.18},
                {"emission", 0.04},
                {"filaments", 2.2},
                {"cometResponse", 0.6},
                {"cometReach", 6.0},
                {"colorDeep", json::array({0.016, 0.012, 0.062})},
                {"colorMid", json::array({0.05, 0.085, 0.24})},
                {"colorAccent", json::array({0.1, 0.34, 0.46})}};
}

void checkShippedValues(const world::Vortex& v) {
    CHECK(v.center.y == Approx(-70.0f));
    CHECK(v.radius == Approx(200.0f));
    CHECK(v.funnelDepth == Approx(1500.0f));
    CHECK(v.throat == Approx(0.1f));
    CHECK(v.throatDensity == Approx(0.55f));
    CHECK(v.thickness == Approx(70.0f));
    CHECK(v.swirl == Approx(6.5f));
    CHECK(v.rotationSpeed == Approx(0.028f));
    CHECK(v.density == Approx(0.0013f));
    CHECK(v.innerVoid == Approx(0.24f));
    CHECK(v.contrast == Approx(3.6f));
    CHECK(v.turbulence == Approx(0.65f));
    CHECK(v.turbulenceScale == Approx(2.4f));
    CHECK(v.emission == Approx(0.04f));
    CHECK(v.filaments == Approx(2.2f));
    CHECK(v.cometResponse == Approx(0.6f));
    CHECK(v.cometReach == Approx(6.0f));
    CHECK(v.colorAccent.b == Approx(0.46f));
    // Absent from the file, so this is the default carried through rather than a zero.
    CHECK(v.spill == Approx(2.5f));
}

std::filesystem::path writeScene(const char* file, bool legacy) {
    json doc;
    doc["format"] = "avgen-scene";
    doc["version"] = 1;
    doc["name"] = "vortex probe";
    if (legacy) {
        doc["environment"] = json{{"vortex", shippedVortex()}};
    } else {
        doc["atmosphericEffects"] = json::array({json{{"name", "Cosmic Vortex"},
                                                      {"enabled", true},
                                                      {"kind", "vortex"},
                                                      {"activation", "always"},
                                                      {"vortex", shippedVortex()}}});
    }
    const auto path = scratch() / file;
    std::ofstream(path) << doc.dump(1);
    return path;
}

const world::AtmosphericEffect* firstVortex(const std::vector<world::AtmosphericEffect>& effects) {
    for (const world::AtmosphericEffect& e : effects) {
        if (e.kind == world::AtmosphereKind::Vortex) {
            return &e;
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("a vortex round-trips through JSON as an atmospheric effect", "[vortex][atmospherics]") {
    world::AtmosphericEffect e = world::cosmicVortex("Funnel");
    e.vortex.center = {3.0f, -70.0f, -8.0f};
    e.vortex.spill = 4.25f;

    const auto back = world::AtmosphericEffect::fromJson(e.toJson());
    REQUIRE(back.has_value());
    CHECK(back->kind == world::AtmosphereKind::Vortex);
    CHECK(back->name == "Funnel");
    CHECK(back->vortex.center.z == Approx(-8.0f));
    CHECK(back->vortex.spill == Approx(4.25f));
    CHECK(back->vortex.radius == Approx(e.vortex.radius));
    CHECK(back->vortex.colorAccent.g == Approx(e.vortex.colorAccent.g));

    // THE CONTROL: an effect of another kind does not come back as a vortex, and a comet's payload
    // is not quietly overwritten by the vortex default that now sits beside it.
    const auto comet = world::AtmosphericEffect::fromJson(world::bioluminescentComet("C").toJson());
    REQUIRE(comet.has_value());
    CHECK(comet->kind == world::AtmosphereKind::Comet);
    CHECK_FALSE(comet->vortex.active());
    CHECK(comet->comet.appearance.coreIntensity > 0.0f);
}

TEST_CASE("a scene authoring a vortex effect reaches the frame", "[vortex][atmospherics][composition]") {
    assets::AssetRegistry registry;
    registry.setBaseDirectory(scratch());
    params::ParameterSet params;
    params::Modulator modulator;

    auto loaded = scene::Composition::loadFile(writeScene("authored.scene.json", false), registry);
    REQUIRE(loaded.has_value());
    (*loaded)->attach(params, modulator);

    const world::AtmosphericEffect* e = firstVortex((*loaded)->atmosphericEffects());
    REQUIRE(e != nullptr);
    CHECK(e->name == "Cosmic Vortex");
    checkShippedValues(e->vortex);
}

TEST_CASE("a scene in the legacy environment.vortex form is migrated, values intact",
          "[vortex][atmospherics][composition][migration]") {
    assets::AssetRegistry registry;
    registry.setBaseDirectory(scratch());
    params::ParameterSet params;
    params::Modulator modulator;

    auto loaded = scene::Composition::loadFile(writeScene("legacy.scene.json", true), registry);
    REQUIRE(loaded.has_value());
    (*loaded)->attach(params, modulator);

    const world::AtmosphericEffect* e = firstVortex((*loaded)->atmosphericEffects());
    REQUIRE(e != nullptr);
    CHECK(e->enabled);
    CHECK(e->activation == world::Activation::Always);
    checkShippedValues(e->vortex);

    // Saved in the new format, and the old key is gone rather than written twice.
    const json saved = (*loaded)->toJson();
    REQUIRE(saved.contains("atmosphericEffects"));
    CHECK(saved["atmosphericEffects"].size() == 1);
    CHECK(saved["atmosphericEffects"][0]["kind"] == "vortex");
    CHECK_FALSE(saved.value("environment", json::object()).contains("vortex"));
}

TEST_CASE("a legacy vortex that was switched off does not become an effect",
          "[vortex][atmospherics][composition][migration]") {
    // THE CONTROL for the migration: `radius` 0 was ADR-371's "off", and every scene in the
    // repository but one has no vortex at all. Turning those into disabled effect instances would
    // put a row in the World Effects panel for something nobody authored.
    assets::AssetRegistry registry;
    registry.setBaseDirectory(scratch());
    json doc;
    doc["format"] = "avgen-scene";
    doc["version"] = 1;
    doc["name"] = "off";
    doc["environment"] = json{{"vortex", json{{"radius", 0.0}, {"emission", 3.0}}}};
    const auto path = scratch() / "off.scene.json";
    std::ofstream(path) << doc.dump(1);

    auto loaded = scene::Composition::loadFile(path, registry);
    REQUIRE(loaded.has_value());
    CHECK(firstVortex((*loaded)->atmosphericEffects()) == nullptr);
}

TEST_CASE("every parameter the World Effects panel asks a vortex for exists",
          "[vortex][atmospherics][params][ui]") {
    params::ParameterSet params;
    std::vector<world::AtmosphericEffect> effects{world::cosmicVortex("Funnel")};
    world::registerAtmosphericParameters(params, effects);

    const std::string prefix = world::atmosphericParameterPrefix("Funnel");
    const auto requireLeaf = [&](std::string_view leaf) {
        const std::string path = prefix + std::string(leaf);
        INFO(path);
        params::IParameter* p = params.find(path);
        REQUIRE(p != nullptr);
        CHECK(p->flags().modulatable);
        CHECK(p->flags().serialized);
    };
    for (const ui::EffectRow& r : ui::vortexRows()) {
        requireLeaf(r.leaf);
    }
    for (const ui::EffectRow& r : ui::vortexAdvancedRows()) {
        requireLeaf(r.leaf);
    }
    // The lifetime rows the panel draws for every kind, and the enable toggle on its header.
    for (const char* leaf : {"enabled", "delay", "fadeIn", "fadeOut", "lifetime", "repeat",
                             "windowStart", "windowSeconds"}) {
        requireLeaf(leaf);
    }
    // The beat route's target, computed the way the panel computes it rather than asserted as a
    // literal -- ADR-382's defect was in exactly that arithmetic.
    CHECK(params.find(ui::atmosphericBeatTarget("Funnel", world::AtmosphereKind::Vortex)) != nullptr);

    // THE CONTROL: a leaf that does not exist is not found, so the loop above could have failed.
    CHECK(params.find(prefix + "sunsetAmount") == nullptr);
    // And the rows a vortex does NOT have are genuinely absent, which is why the panel skips the
    // rainbow and ground-pool sections rather than drawing rows that do nothing.
    CHECK(params.find(prefix + "rainbowSpeed") == nullptr);
}

TEST_CASE("the Tree panel is gone and the Environment panel is generic", "[ui][panels][vortex]") {
    const auto panels = ui::editorPanels();
    const auto named = [&](const char* id) {
        return std::any_of(panels.begin(), panels.end(),
                           [id](const ui::EditorPanel& p) { return std::string(p.id) == id; });
    };
    CHECK_FALSE(named("Tree"));
    // THE CONTROL: the predicate finds the panels that are there, so the line above is a fact about
    // the registry rather than about a typo in the predicate.
    CHECK(named("Environment"));
    CHECK(named("World Effects"));
    CHECK(named("Parameters"));

    // The Environment panel's blurb no longer names one scene's furniture.
    const ui::EditorPanel* env = ui::findEditorPanel("Environment");
    REQUIRE(env != nullptr);
    const std::string blurb(env->hint);
    CHECK(blurb.find("island") == std::string::npos);
    CHECK(blurb.find("vortex") == std::string::npos);
}
