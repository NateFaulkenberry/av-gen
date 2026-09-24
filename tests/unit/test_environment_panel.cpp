// ADR-375, revised by ADR-387: the Environment panel, and the two ways a bespoke panel lies.
//
// A hand-written panel asks for parameters by string. If a path is wrong -- a typo, or a rename
// somewhere else -- the row simply does not draw, and the section degrades to an empty box that
// looks like "this scene has no wind" rather than "this panel is broken". Nothing in the build
// catches that, because a missing parameter is a runtime null and not a compile error.
//
// So: every path these panels reference must resolve in a scene that has the feature. That is this
// file's job, and it is the findability half of ADR-350 -- which only ever asserted that a
// parameter was *registered*, never that anything pointed at it.
//
// ADR-387 removed the Tree panel and cut the Environment panel back to what is generic, so the
// sections this file used to cover moved:
//
//   * the vortex's rows  -> tests/unit/test_vortex_effect.cpp, against the World Effects panel
//   * a node's wind      -> the World panel's Inspector, whose arithmetic is asserted below
//   * the falling leaves -> the same Inspector, ditto
//
// The Inspector's arithmetic is asserted here rather than taken on trust, because it is the same
// kind of string arithmetic the Tree panel got wrong: it splits a parameter path on the first '/'
// after the selected object's prefix and uses the front half as a group heading.

#include "assets/asset_registry.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "scene/particles.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

using namespace avgen;

namespace {

bool registered(const params::ParameterSet& s, const std::string& path) {
    return s.find(path) != nullptr;
}

} // namespace

TEST_CASE("Every path the Environment panel asks for exists", "[ui][panels][parameters]") {
    assets::AssetRegistry registry;
    params::ParameterSet params;
    params::Modulator modulator;
    scene::Composition comp(registry, "composition");
    comp.attach(params, modulator);

    for (const char* path : {"scene/volumeDensity", "scene/fogHeight", "scene/fogHeightFalloff",
                             // ADR-568 (§7): drawn beside the falloff, so they are asked for here.
                             "scene/fogUpperDensity", "scene/fogHeightCurve",
                             // ADR-570 (§20/§22): the self-shadow march's two controls.
                             "scene/volumeShadowSteps", "scene/volumeShadowStrength",
                             // ADR-573 (§27): two controls that shipped without a way to reach them.
                             "scene/volumeLocalLights", "scene/volumeMaxDistance",
                             // ADR-574: ADR-058's coupling, reachable at last.
                             "scene/fogHeightAmount",
                             "env/intensity", "env/sky/intensity"}) {
        INFO(path);
        CHECK(registered(params, path));
    }
    // THE CONTROL. Without it the loop above passes against a set that answers yes to anything.
    CHECK_FALSE(registered(params, "scene/nonesuch"));
    // ADR-387: and the vortex is no longer a field on the environment, so nothing registers these.
    // A scene that still carries them in its file is migrated on load -- see test_vortex_effect.cpp.
    CHECK_FALSE(registered(params, "scene/vortex/radius"));
    CHECK_FALSE(registered(params, "scene/vortex/emission"));
    // ADR-705: one law, one density. The surface pass's own exp-squared density is gone, not
    // aliased (ADR-441), so a route or a look still naming it resolves to nothing.
    CHECK_FALSE(registered(params, "scene/fogDensity"));
}

TEST_CASE("Every path the Environment panel's wind section asks for exists",
          "[ui][panels][parameters][wind]") {
    assets::AssetRegistry registry;
    params::ParameterSet params;
    params::Modulator modulator;
    scene::Composition comp(registry, "composition");
    comp.attach(params, modulator);

    for (const char* path : {"scene/wind/enabled", "scene/windSpeed", "scene/windDirection",
                             "scene/wind/gustAmount", "scene/wind/gustScale", "scene/wind/gustSpeed",
                             "scene/wind/gustSharpness", "scene/wind/turbulence",
                             "scene/wind/turbulenceScale", "scene/wind/turbulenceSpeed",
                             "scene/wind/regionScale", "scene/wind/regionAmount",
                             "scene/wind/regionDrift", "scene/wind/flutterScale"}) {
        INFO(path);
        CHECK(registered(params, path));
    }

    // ADR-387 §18: exactly one global wind. A second one registered anywhere -- on a node, on an
    // effect -- would make "how windy is it" have two answers, which is the thing the consolidation
    // is against. Anything ending in `windSpeed` other than the one is a failure.
    int globals = 0;
    for (const params::IParameter* p : params.ordered()) {
        const std::string& path = p->path();
        const std::string tail = "windSpeed";
        if (path.size() >= tail.size() && path.compare(path.size() - tail.size(), tail.size(), tail) == 0) {
            INFO(path);
            CHECK(path == "scene/windSpeed");
            ++globals;
        }
    }
    CHECK(globals == 1);
}

TEST_CASE("A node's wind response is reachable through the Inspector's own arithmetic",
          "[ui][panels][parameters][wind][inspector]") {
    // ADR-387. The per-body half of the wind moved out of a bespoke panel and into the World
    // panel's Inspector, which groups an object's parameters by the first path segment after its
    // prefix. That is string arithmetic of exactly the kind ADR-382 records getting wrong, so it is
    // done here the way the Inspector does it and checked against what registration produces.
    assets::AssetRegistry registry;
    params::ParameterSet params;
    params::Modulator modulator;
    scene::Composition comp(registry, "composition");
    comp.attach(params, modulator);

    scene::CompositionNode body;
    body.name = "tree";
    body.kind = scene::NodeKind::Group;
    body.windAuthored = true;
    REQUIRE(comp.addNode(std::move(body)).has_value());

    const std::string prefix = "nodes/tree/"; // WorldSelection::parameterPrefix() for a node
    std::vector<std::string> windGroup;
    bool sawPlainRow = false;
    for (const params::IParameter* p : params.ordered()) {
        const std::string& path = p->path();
        if (path.size() <= prefix.size() || path.compare(0, prefix.size(), prefix) != 0) {
            continue;
        }
        const std::string rel = path.substr(prefix.size());
        const std::size_t slash = rel.find('/');
        if (slash == std::string::npos) {
            sawPlainRow = true;
            continue;
        }
        if (rel.substr(0, slash) == "wind") {
            windGroup.push_back(rel.substr(slash + 1));
        }
    }
    for (const char* leaf : {"strength", "trunk", "branch", "foliage", "flutter", "lag"}) {
        INFO(leaf);
        CHECK(std::find(windGroup.begin(), windGroup.end(), leaf) != windGroup.end());
    }
    // THE CONTROL, twice over: the grouping does not swallow the node's ordinary rows, and it does
    // not invent members.
    CHECK(sawPlainRow);
    CHECK(std::find(windGroup.begin(), windGroup.end(), "nonesuch") == windGroup.end());
}

TEST_CASE("A particle system's controls are reachable under its own prefix",
          "[ui][panels][parameters][leaf][inspector]") {
    assets::AssetRegistry registry;
    params::ParameterSet params;
    params::Modulator modulator;
    scene::Composition comp(registry, "composition");
    comp.attach(params, modulator);

    scene::CompositionNode leaves;
    leaves.name = "falling-leaves";
    leaves.kind = scene::NodeKind::Particles;
    leaves.particles.name = "falling-leaves";
    leaves.particles.shape2d = scene::ParticleShape::Leaf;
    REQUIRE(comp.addNode(std::move(leaves)).has_value());

    // ADR-387: found by selecting it -- `WorldSelection::Kind::Particles` is `particles/<name>/`
    // -- rather than by sniffing paths for the substring "leaf", which is what the Tree panel did
    // and which made the control's existence depend on what somebody named their node.
    const std::string base = "particles/falling-leaves/";
    for (const char* leaf : {"enabled", "spawnRate", "lifetime", "size", "gravity", "drag",
                             "windInfluence", "turbulence", "turbulenceScale", "tumbleRate",
                             "leafAspect", "twoSided", "softness", "colorStart", "colorEnd",
                             "emissive"}) {
        INFO(base + leaf);
        CHECK(registered(params, base + leaf));
    }
    CHECK_FALSE(registered(params, base + "nonesuch"));
}

TEST_CASE("A wind body can be created and removed from the application", "[ui][panels][wind][parameters]") {
    assets::AssetRegistry registry;
    params::ParameterSet params;
    params::Modulator modulator;
    scene::Composition comp(registry, "composition");
    comp.attach(params, modulator);

    scene::CompositionNode group;
    group.name = "oak";
    group.kind = scene::NodeKind::Group;
    REQUIRE(comp.addNode(std::move(group)).has_value());

    // Before: no body, no controls. This is the state a new scene is in, and the reason the button
    // exists -- ADR-360 left the wind tunable but not creatable.
    CHECK_FALSE(registered(params, "nodes/oak/wind/strength"));

    REQUIRE(comp.setNodeWindBody("oak", true));
    for (const char* leaf : {"strength", "trunk", "branch", "foliage", "flutter", "lag"}) {
        INFO(leaf);
        CHECK(registered(params, std::string("nodes/oak/wind/") + leaf));
    }
    // A body created by hand starts at a strength that does something. ADR-360 shipped one at 0 and
    // the owner reported the scene as unchanged; pressing a button that does nothing is worse.
    const params::IParameter* strength = params.find("nodes/oak/wind/strength");
    REQUIRE(strength != nullptr);
    CHECK(strength->baseComponent(0) > 0.0f);

    // Idempotent, and reversible without leaking parameters -- a stale `nodes/oak/wind/strength`
    // left behind is exactly the orphan ADR-264 spent an afternoon on.
    CHECK_FALSE(comp.setNodeWindBody("oak", true));
    REQUIRE(comp.setNodeWindBody("oak", false));
    for (const char* leaf : {"strength", "trunk", "branch", "foliage", "flutter", "lag"}) {
        INFO(leaf);
        CHECK_FALSE(registered(params, std::string("nodes/oak/wind/") + leaf));
    }
    // ...and the node's ordinary parameters survived the round trip, which is the control: an
    // unregister that took too much would show up here and nowhere else.
    CHECK(registered(params, "nodes/oak/position"));
    CHECK(registered(params, "nodes/oak/visible"));

    CHECK_FALSE(comp.setNodeWindBody("nonesuch", true));
}

// The owner opened the Tree panel and found "Tree energy" and "Canopy shimmer" empty on a scene
// whose tree declares both a wind body and an energy block.
//
// Nothing was missing. `windBodies()` returned the node's *base* -- "nodes/tree-of-life/" -- because
// it already stripped the "/wind/strength" tail it matched on. The energy section then removed five
// more characters on the assumption that it still ended in "wind/", which turned
// "nodes/tree-of-life/" into "nodes/tree-of-" and asked for "nodes/tree-of-energy/intensity". No
// scene has that, so the section drew nothing and said nothing.
//
// **Why the tests before it did not catch it.** They assert the paths the *registration* produces.
// The panel did not use those; it computed its own from a prefix, and the arithmetic in between was
// never exercised.
//
// ADR-387 deleted that panel, and the energy controls moved to the World panel's Inspector, which
// derives its groups from the paths rather than from a wind prefix -- so the off-by-five cannot be
// written again in that shape. The case is kept and pointed at the new arithmetic, because the
// *blind spot* is what it guards and the blind spot survives a rewrite: a panel asks for parameters
// by string, and a wrong one neither fails to compile nor throws.
TEST_CASE("A node's energy controls resolve through the Inspector's own arithmetic",
          "[ui][panels][parameters][energy][inspector]") {
    assets::AssetRegistry registry;
    params::ParameterSet params;
    params::Modulator modulator;
    scene::Composition comp(registry, "composition");
    comp.attach(params, modulator);

    scene::CompositionNode body;
    body.name = "tree-of-life"; // the real name: long enough that the old off-by-five truncated it
    body.kind = scene::NodeKind::Group;
    body.windAuthored = true;   // energy registers on either flag
    REQUIRE(comp.addNode(std::move(body)).has_value());

    // The Inspector's arithmetic, done here exactly as `WorldPanel::drawInspector` does it: the
    // selection's prefix, then the first path segment after it as a group heading.
    const std::string prefix = "nodes/tree-of-life/"; // WorldSelection::parameterPrefix(), Kind::Node
    std::vector<std::string> energyGroup;
    for (const params::IParameter* p : params.ordered()) {
        const std::string& path = p->path();
        if (path.size() <= prefix.size() || path.compare(0, prefix.size(), prefix) != 0) {
            continue;
        }
        const std::string rel = path.substr(prefix.size());
        const std::size_t slash = rel.find('/');
        if (slash != std::string::npos && rel.substr(0, slash) == "energy") {
            energyGroup.push_back(rel.substr(slash + 1));
        }
    }
    for (const char* leaf : {"intensity", "pulseSpeed", "pulseWidth", "propagation", "root",
                             "trunk", "branch", "canopy", "noise", "bloom", "shimmer",
                             "shimmerSpeed", "shimmerScale", "colorNear", "colorFar"}) {
        INFO(leaf);
        CHECK(std::find(energyGroup.begin(), energyGroup.end(), leaf) != energyGroup.end());
    }

    // The control, and the whole point of the case: the arithmetic that shipped produced a path
    // that resolves to nothing. It is asserted directly so that reintroducing it anywhere fails
    // here rather than going quietly blank in a panel.
    const std::string broken = prefix.substr(0, prefix.size() - 5) + "energy/";
    CHECK(broken == "nodes/tree-of-energy/");
    CHECK(registered(params, prefix + "energy/intensity"));
    CHECK_FALSE(registered(params, broken + "intensity"));
    CHECK(std::find(energyGroup.begin(), energyGroup.end(), "nonesuch") == energyGroup.end());
}
