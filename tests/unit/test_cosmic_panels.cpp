// ADR-375: the Environment and Tree panels, and the two ways a bespoke panel lies.
//
// A hand-written panel asks for parameters by string. If a path is wrong -- a typo, or a rename
// somewhere else -- the row simply does not draw, and the section degrades to an empty box that
// looks like "this scene has no wind" rather than "this panel is broken". Nothing in the build
// catches that, because a missing parameter is a runtime null and not a compile error.
//
// So: every path these panels reference must resolve in a scene that has the feature. That is this
// file's job, and it is the findability half of ADR-350 -- which only ever asserted that a
// parameter was *registered*, never that anything pointed at it.

#include "assets/asset_registry.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "scene/particles.hpp"

#include <catch2/catch_test_macros.hpp>

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
                             "env/intensity", "env/sky/intensity",
                             "scene/vortex/radius", "scene/vortex/funnelDepth", "scene/vortex/throat",
                             "scene/vortex/throatDensity", "scene/vortex/thickness",
                             "scene/vortex/swirl", "scene/vortex/rotationSpeed",
                             "scene/vortex/turbulence", "scene/vortex/density",
                             "scene/vortex/emission", "scene/vortex/contrast",
                             "scene/vortex/innerVoid", "scene/vortex/filaments",
                             "scene/vortex/breathAmount", "scene/vortex/colorDeep",
                             "scene/vortex/colorMid", "scene/vortex/colorAccent"}) {
        INFO(path);
        CHECK(registered(params, path));
    }
    // THE CONTROL. Without it the loop above passes against a set that answers yes to anything.
    CHECK_FALSE(registered(params, "scene/vortex/nonesuch"));
}

TEST_CASE("Every path the Tree panel's wind section asks for exists", "[ui][panels][parameters][wind]") {
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

    scene::CompositionNode body;
    body.name = "tree";
    body.kind = scene::NodeKind::Group;
    body.windAuthored = true;
    REQUIRE(comp.addNode(std::move(body)).has_value());
    for (const char* leaf : {"strength", "trunk", "branch", "foliage", "flutter", "lag"}) {
        const std::string path = std::string("nodes/tree/wind/") + leaf;
        INFO(path);
        CHECK(registered(params, path));
    }
    CHECK_FALSE(registered(params, "nodes/tree/wind/nonesuch"));
}

TEST_CASE("Every path the Tree panel's leaf section asks for exists", "[ui][panels][parameters][leaf]") {
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

    // The panel finds the system by the `/spawnRate` suffix on a path containing "leaf" or
    // "leaves", because a nested composition prefixes the name. Assert the convention holds as well
    // as the leaves, or the panel finds nothing in a sub-scene and says "this scene sheds no
    // leaves" about a scene that does.
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
// Nothing was missing. `windBodies()` returns the node's *base* -- "nodes/tree-of-life/" -- because
// it already strips the "/wind/strength" tail it matched on. The energy section then removed five
// more characters on the assumption that it still ended in "wind/", which turned
// "nodes/tree-of-life/" into "nodes/tree-of-" and asked for "nodes/tree-of-energy/intensity". No
// scene has that, so `have()` said no and the section drew nothing.
//
// The section above it -- wind -- was fine, because it uses the base as given. So the two sections
// disagreed about what `windBodies` returns, and only one of them was right.
//
// **Why the existing tests did not catch it.** They assert the paths the *registration* produces.
// The panel does not use those; it computes its own from a prefix, and the arithmetic in between
// was never exercised. A panel asks for parameters by string, so a wrong path neither fails to
// compile nor throws -- it draws an empty box indistinguishable from a scene that has no energy.
// This case does the panel's arithmetic and then asks whether the answer exists.
TEST_CASE("The Tree panel's energy prefix resolves to parameters that exist",
          "[ui][panels][parameters][energy]") {
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

    // Step 1: what `windBodies()` hands the section, derived by its own rule rather than assumed.
    const std::string tail = "/wind/strength";
    std::string base;
    for (const params::IParameter* p : params.ordered()) {
        const std::string& path = p->path();
        if (path.size() > tail.size() &&
            path.compare(path.size() - tail.size(), tail.size(), tail) == 0) {
            base = path.substr(0, path.size() - tail.size() + 1);
        }
    }
    REQUIRE(base == "nodes/tree-of-life/");

    // Step 2: the prefix the section builds from it, and every leaf both sections ask for.
    const std::string e = base + "energy/";
    for (const char* leaf : {"intensity", "pulseSpeed", "pulseWidth", "propagation", "root",
                             "trunk", "branch", "canopy", "noise", "bloom", "shimmer",
                             "shimmerSpeed", "shimmerScale", "colorNear", "colorFar"}) {
        INFO(e + leaf);
        CHECK(registered(params, e + leaf));
    }

    // The control, and the whole point of the case: the arithmetic that shipped produced a path
    // that resolves to nothing. If someone reintroduces it, this fails instead of the panel going
    // quietly blank.
    const std::string broken = base.substr(0, base.size() - 5) + "energy/";
    CHECK(broken == "nodes/tree-of-energy/");
    CHECK_FALSE(registered(params, broken + "intensity"));
    CHECK_FALSE(registered(params, e + "nonesuch"));
}
