// Can a person reach the travelling band of light that climbs the tree?
//
// The owner looked at the Tree of Life floating island and asked what the "scan line type effect
// going through the tree" was, and why it was not something they could control. The effect is
// `treeEnergyAt` in `common.wgsl`: a band of brightness that leaves the roots and climbs to the
// canopy, `fract(h - t * speed)` with a squared falloff, and it is on in the shipped scene at
// intensity 1.2. It has sixteen registered, modulatable, serialised parameters under
// `nodes/tree-of-life/energy/`, and every existing test about it asserts exactly that -- which is
// why none of them noticed the complaint. Asserting the registration proves the parameters exist
// and says nothing about whether the UI can reach them (ADR-387).
//
// The Tree panel used to name this effect and was deleted. ADR-387's migration table says the
// controls moved to "World > Inspector, on the selected node". That is a claim, and ADR-385 is
// about exactly this: a stated reason that nobody checked. So this file checks it by doing the
// panel's OWN arithmetic -- the same prefix split and the same authoring-layer filter that
// `WorldPanel::drawInspector` runs -- against the real shipped scene.
//
// Both cases carry a control that fails if the check is vacuous (ADR-182).

#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "ui/ui_logic.hpp"
#include "world/effects/effect_params.hpp"
#include "world/effects/effect_registry.hpp"
#include "world/atmospherics.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;
using nlohmann::json;

namespace {

fs::path examplesDir() {
#ifdef AVGEN_SOURCE_DIR
    return fs::path(AVGEN_SOURCE_DIR) / "examples";
#else
    return {};
#endif
}

// `WorldPanel::drawInspector`, reduced to what it decides: which parameters appear under the
// selected object, and which sub-group each one lands in. Kept deliberately as a transcription
// rather than a call into the panel, because the panel needs an ImGui frame -- and kept in one
// place so a change to the panel that this no longer matches shows up as a failure here.
struct InspectorRows {
    std::vector<std::string> plain;                            // leaf
    std::map<std::string, std::vector<std::string>> groups;    // group -> leaves
};

InspectorRows inspectorRows(const params::ParameterSet& params, const std::string& prefix,
                            ui::AuthoringLayer layer) {
    InspectorRows out;
    for (const params::IParameter* param : params.ordered()) {
        if (!param->flags().exposed || !ui::detail::pathStartsWith(param->path(), prefix)) {
            continue;
        }
        if (!ui::layerShowsPath(layer, param->path())) {
            continue;
        }
        const std::string rel = param->path().substr(prefix.size());
        const std::size_t slash = rel.find('/');
        if (slash == std::string::npos) {
            out.plain.push_back(rel);
            continue;
        }
        out.groups[rel.substr(0, slash)].push_back(rel.substr(slash + 1));
    }
    return out;
}

} // namespace

TEST_CASE("the Inspector reaches the tree's energy controls on the shipped scene",
          "[treeisland][ui][energy][reach]") {
    const fs::path dir = examplesDir();
    if (dir.empty()) {
        SKIP("AVGEN_SOURCE_DIR not defined");
    }
    assets::AssetRegistry registry(dir / "treeisland");
    auto loaded = scene::Composition::loadFile(
            dir / "treeisland/tree-of-life-floating-island.scene.json", registry);
    REQUIRE(loaded.has_value());

    params::ParameterSet params;
    params::Modulator modulator;
    (*loaded)->attach(params, modulator);

    const std::string prefix = "nodes/tree-of-life/";
    const InspectorRows rows = inspectorRows(params, prefix, ui::AuthoringLayer::Intermediate);

    // The group has to be there, and it has to be the one an artist would open looking for a
    // travelling pulse of light.
    const auto energy = rows.groups.find("energy");
    REQUIRE(energy != rows.groups.end());

    // The controls that shape the band the owner saw: how bright, how fast it travels, how wide the
    // lit part of the cycle is, and where along the tree it is allowed to show.
    const std::set<std::string> present(energy->second.begin(), energy->second.end());
    for (const char* leaf : {"intensity", "pulseSpeed", "pulseWidth", "propagation", "root",
                             "trunk", "branch", "canopy", "noise", "bloom", "shimmer",
                             "shimmerSpeed", "shimmerScale", "colorNear", "colorFar"}) {
        INFO("nodes/tree-of-life/energy/" << leaf);
        CHECK(present.count(leaf) == 1);
    }

    // Control 1: the layer filter is live, so this test is not passing because nothing filters.
    // At Beginner, `nodes/` is not in the list and the whole Inspector body is empty -- which is
    // worth knowing on its own, because it means the answer to "where do I turn this down" depends
    // on a setting elsewhere in the panel.
    const InspectorRows beginner = inspectorRows(params, prefix, ui::AuthoringLayer::Beginner);
    CHECK(beginner.groups.empty());
    CHECK(beginner.plain.empty());

    // Control 2: the grouping is read off the path and is not handed to every node. A node that
    // declares neither a wind body nor energy gets no energy group, so a pass above means this
    // node's registration put it there rather than the enumeration inventing it.
    const InspectorRows surface =
            inspectorRows(params, "nodes/island-surface/", ui::AuthoringLayer::Intermediate);
    CHECK(surface.groups.count("energy") == 0);
}

TEST_CASE("the shipped project's vortex routes name parameters that exist",
          "[treeisland][vortex][routes][reach]") {
    const fs::path dir = examplesDir();
    if (dir.empty()) {
        SKIP("AVGEN_SOURCE_DIR not defined");
    }
    std::ifstream in(dir / "treeisland/tree-of-life-floating-island.json");
    REQUIRE(in.good());
    const json doc = json::parse(in);

    // The PLACED MEDIUM the project declares, and the prefix its parameters therefore live under.
    //
    // This looked for `"kind" == "vortex"` and was right for as long as the hero's medium was one.
    // ADR-580 replaced it with a tornado, and the check has to follow the *role* rather than the
    // kind or it asserts a fact about which effect the deliverable happened to use. Reading it from
    // the registry -- whatever kind is there, is it routed correctly -- is what makes it survive
    // the next replacement as well as this one.
    std::string mediumName;
    world::EffectKind mediumKind = world::EffectKind::Vortex;
    for (const auto& e : doc.value("atmosphericEffects", json::array())) {
        const std::string key = e.value("kind", std::string{});
        const world::EffectSchema* s = world::effectSchema(key);
        if (s != nullptr && s->resolve.bucket == world::EffectBucket::Medium) {
            mediumName = e.value("name", std::string{});
            mediumKind = s->kind;
        }
    }
    REQUIRE_FALSE(mediumName.empty());
    const std::string vortexName = mediumName;

    const world::EffectSchema* schema = world::effectSchema(mediumKind);
    REQUIRE(schema != nullptr);
    REQUIRE(schema->factory != nullptr);
    params::ParameterSet params;
    std::vector<world::EffectInstance> effects{schema->factory(mediumName)};
    world::registerEffectParameters(params, effects);
    const std::string prefix = world::effectParameterPrefix(vortexName);

    // Every route that is about the vortex must name a parameter that exists. A route whose target
    // does not resolve is dropped with a warning nobody reads: the picture keeps its funnel and the
    // funnel stops answering the music, which is a change of 95% of the frame that fails nothing.
    std::size_t checked = 0;
    for (const auto& r : doc.value("routes", json::array())) {
        const std::string target = r.value("target", std::string{});
        if (target.find("vortex") == std::string::npos &&
            !target.starts_with(prefix)) {
            continue;
        }
        INFO("route target " << target);
        CHECK(target.starts_with(prefix)); // not the pre-ADR-387 `scene/vortex/...`
        CHECK(params.find(target) != nullptr);
        ++checked;
    }
    // The project is the reason this test exists, so it has to still be routing the vortex.
    CHECK(checked == 5);

    // Control: the old path really is dead, so the assertion above could have come out the other
    // way. If `scene/vortex/density` still resolved, re-pointing the routes would have been
    // cosmetic and this test would prove nothing.
    CHECK(params.find("scene/vortex/density") == nullptr);
}
