// ADR-925: the reactive catalogue, generated from the engine's own data. Every family the table
// names has a case here on the glade fixture (tests/support/reactivity_fixture.hpp), with the
// neighbour it must not offer: a layer that emits nothing, a character's glow, a dead target.

#include "app/directing_context.hpp"
#include "app/engine.hpp"
#include "directing/reactive_catalog.hpp"
#include "support/gltf_fixture.hpp"
#include "support/reactivity_fixture.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <string>

using namespace avgen;
using namespace avgen::directing;
using Catch::Matchers::WithinAbs;

namespace {

struct Glade {
    app::Engine engine{app::EngineMode::Offline};
    explicit Glade(const testsupport::GladeOptions& options = {}) {
        const auto glb = testsupport::writeTriangleGlb("reactive_catalog");
        REQUIRE(testsupport::loadGlade(engine, glb, glb, options));
    }
    [[nodiscard]] ReactiveCatalog catalog() { return app::sceneFactsFor(engine).capabilities.reactive(); }
};

const ReactiveTarget& target(const ReactiveCatalog& c, const std::string& path) {
    const ReactiveTarget* t = c.find(path);
    INFO(path);
    REQUIRE(t != nullptr);
    return *t;
}

bool excluded(const ReactiveCatalog& c, const std::string& path, std::string_view why) {
    return std::any_of(c.excluded.begin(), c.excluded.end(), [&](const std::string& e) {
        return e.starts_with(path + ":") && e.find(why) != std::string::npos;
    });
}

} // namespace

TEST_CASE("The reactive catalogue lists every kind of target the glade has, each with what moving it does",
          "[directing][reactivity][catalog][adr925]") {
    Glade glade;
    const ReactiveCatalog c = glade.catalog();
    INFO(c.toJson().dump(1).substr(0, 6000));

    // A hero's program-lit parts, and its plain one.
    const ReactiveTarget& gills = target(c, "nodes/elder-gills/emissiveBoost");
    CHECK(gills.group == ReactiveGroup::HeroEmission);
    CHECK(gills.hero == "elder-cap");
    CHECK(gills.kind == ReactiveKind::Luminance);
    CHECK(gills.level == ReactiveLevel::Meso);
    CHECK(gills.programLit);
    CHECK(target(c, "nodes/elder-cap/emissiveBoost").programLit);
    CHECK_FALSE(target(c, "nodes/elder-stem/emissiveBoost").programLit);
    CHECK(target(c, "nodes/elder-stem/emissiveBoost").hero == "elder-cap");
    CHECK(target(c, "nodes/lantern-gills/emissiveBoost").hero == "lantern-cap");

    // The glowing scatter layers, their hue, their wave; the faint one ranks far below the fungi.
    const ReactiveTarget& fungi = target(c, "nodes/meadow/scatter/fungi/emissionGain");
    CHECK(fungi.group == ReactiveGroup::ScatterGlow);
    CHECK(fungi.level == ReactiveLevel::Micro);
    CHECK_THAT(fungi.size, WithinAbs(0.3, 1e-6));
    CHECK(target(c, "nodes/meadow/scatter/lamps/emissionGain").level == ReactiveLevel::Meso); // 1.5 m: a lamp
    CHECK(target(c, "nodes/meadow/scatter/grass/emissionGain").emission < 0.25f * fungi.emission); // below salience
    const ReactiveTarget& hue = target(c, "nodes/meadow/scatter/fungi/hueOffset");
    CHECK(hue.group == ReactiveGroup::ScatterHue);
    CHECK(hue.kind == ReactiveKind::Hue);
    CHECK(hue.level == ReactiveLevel::Macro);
    CHECK_THAT(hue.safeMin, WithinAbs(-0.08, 1e-6)); // turns: past +0.08 the valley's teal clips
    CHECK_THAT(hue.safeMax, WithinAbs(0.08, 1e-6));
    const ReactiveTarget& wave = target(c, "nodes/meadow/scatter/fungi/emissiveFieldAmount");
    CHECK(wave.group == ReactiveGroup::ScatterWave);
    CHECK(wave.field == "ripple");
    CHECK(wave.fieldTriggered);
    const ReactiveTarget& nodeWave = target(c, "procedural/moss-gills/emissiveFieldAmount");
    CHECK(nodeWave.group == ReactiveGroup::NodeWave);
    CHECK(nodeWave.hero == "moss-cap");

    // A material program: every surface drawn with it moves together, and the catalogue says which.
    const ReactiveTarget& tissue = target(c, "material/tissue/emissionIntensity");
    CHECK(tissue.group == ReactiveGroup::MaterialEmission);
    CHECK(std::find(tissue.sharedBy.begin(), tissue.sharedBy.end(), "elder-gills") != tissue.sharedBy.end());
    CHECK(std::find(tissue.sharedBy.begin(), tissue.sharedBy.end(), "meadow/fungi") != tissue.sharedBy.end());

    // Particles: the free swarm and the elder's spores (its hero's, through the parent).
    const ReactiveTarget& spores = target(c, "particles/spores/emissive");
    CHECK(spores.group == ReactiveGroup::Particles);
    CHECK(spores.level == ReactiveLevel::Micro);
    CHECK(spores.hero.empty());
    CHECK(target(c, "particles/elder-spores/emissive").hero == "elder-cap");
    CHECK(target(c, "particles/spores/spawnRate").kind == ReactiveKind::Density);

    // The world: the ecology light, the air, the wind, the water, a world effect, the lights.
    CHECK(target(c, "scene/ecologyLight").group == ReactiveGroup::EcologyLight);
    CHECK(target(c, "scene/volumeDensity").kind == ReactiveKind::Density);
    CHECK(target(c, "scene/windSpeed").kind == ReactiveKind::Motion);
    CHECK(target(c, "scene/wind/gustAmount").group == ReactiveGroup::Wind);
    CHECK(target(c, "nodes/meadow/water/sparkle").group == ReactiveGroup::Water);
    CHECK(target(c, "nodes/meadow/water/ripple").kind == ReactiveKind::Motion);
    const ReactiveTarget& aurora = target(c, "fx/aurora/intensity");
    CHECK(aurora.group == ReactiveGroup::Effect);
    CHECK(aurora.level == ReactiveLevel::Macro);
    const ReactiveTarget& practical = target(c, "lightrig/Glade/elder-practical/intensity");
    CHECK(practical.group == ReactiveGroup::Light);
    CHECK_FALSE(practical.global);
    INFO("the practical light stands at " << (practical.position ? practical.position->x : -999.0f) << ", "
                                          << (practical.position ? practical.position->z : -999.0f));
    CHECK(practical.hero == "elder-cap"); // a light has no node: it answers for the hero it stands beside
    CHECK(target(c, "lightrig/Glade/moon/intensity").global);
    CHECK(target(c, "lightrig/Glade/keyIntensity").global);

    // Every entry rests inside its own safe range, and says it in words a viewer would use.
    for (const ReactiveTarget& t : c.targets) {
        INFO(t.path << " base " << t.base << " safe " << t.safeMin << ".." << t.safeMax << " label '" << t.label << "'");
        CHECK(t.safeMin <= t.neutral + 1e-5f);
        CHECK(t.neutral <= t.safeMax + 1e-5f);
        CHECK(t.safeMin < t.safeMax);
        CHECK(t.label.find(' ') != std::string::npos);
        CHECK((t.label.find('/') == std::string::npos || t.group == ReactiveGroup::MaterialEmission));
    }
    // Every group but the water's tears (off in the glade: its amount is 0) and node emission has an entry here.
    for (const ReactiveGroup g : allReactiveGroups()) {
        if (g == ReactiveGroup::WaterTears || g == ReactiveGroup::NodeEmission) {
            continue;
        }
        INFO(reactiveGroupName(g));
        CHECK_FALSE(c.inGroup(g).empty());
    }
}

TEST_CASE("The catalogue never offers what cannot reach the picture, nor a character's glow as a hero's",
          "[directing][reactivity][catalog][adr925]") {
    Glade glade;
    const ReactiveCatalog c = glade.catalog();

    // A layer that emits nothing: its three lanes are dead, and the liveness registry says why.
    for (const char* lane : {"emissionGain", "hueOffset", "emissiveFieldAmount"}) {
        const std::string path = std::string("nodes/meadow/scatter/stones/") + lane;
        INFO(path);
        CHECK(c.find(path) == nullptr);
        CHECK(excluded(c, path, "emits nothing"));
    }
    // A light-wave depth on a layer that names no field.
    CHECK(c.find("nodes/meadow/scatter/shelf/emissiveFieldAmount") == nullptr);
    CHECK(excluded(c, "nodes/meadow/scatter/shelf/emissiveFieldAmount", "names no emissiveField"));
    // The terrain's own boost moves every layer at once: its layers' lanes are the handle.
    CHECK(c.find("nodes/meadow/emissiveBoost") == nullptr);

    // The character: its body emits, but it is no hero's part, its hero point is not a hero, and
    // anything a plan might do to it is marked the simulation's.
    const ReactiveTarget* walker = c.find("nodes/walker/emissiveBoost");
    REQUIRE(walker != nullptr);
    CHECK(walker->group == ReactiveGroup::NodeEmission);
    CHECK(walker->hero.empty());
    CHECK(walker->scripted);
    CHECK(c.hero("walker") == nullptr);
    CHECK(c.hero("elder-cap") != nullptr);

    // An authored route is recorded on its target, so a planner can leave it to its author.
    params::ModRoute authored;
    authored.source = "audio.treble";
    authored.target = "particles/spores/emissive";
    glade.engine.modulator().addRoute(authored);
    glade.engine.rebind();
    const ReactiveCatalog after = glade.catalog();
    CHECK(target(after, "particles/spores/emissive").drivenBy == std::vector<std::string>{"audio.treble"});
}

TEST_CASE("The water's tears are catalogued where the water stream registers them, and not while they are off",
          "[directing][reactivity][catalog][adr925]") {
    Glade glade;
    // ADR-916's controls, registered by the pond's terrain: off by default (amount 0, where the tear code
    // is compiled out), so none of the routable three is offered, each with that reason.
    const std::string base = "nodes/meadow/water/tears/";
    REQUIRE(glade.engine.params().find(base + "amount") != nullptr);
    ReactiveCatalog c = glade.catalog();
    for (const char* leaf : {"amount", "shear", "coverage"}) {
        INFO(leaf);
        CHECK(c.find(base + leaf) == nullptr);
        CHECK(excluded(c, base + leaf, "compiled out"));
    }
    // On: all three in the water-tears group, as motion, labelled for what a viewer sees.
    glade.engine.params().findAs<float>(base + "amount")->setBase(0.4f);
    c = glade.catalog();
    const ReactiveTarget& amount = target(c, base + "amount");
    CHECK(amount.group == ReactiveGroup::WaterTears);
    CHECK(amount.kind == ReactiveKind::Motion);
    CHECK_THAT(amount.base, WithinAbs(0.4, 1e-6));
    CHECK(amount.label == "water tears");
    CHECK(target(c, base + "shear").label == "tear shear");
    CHECK(target(c, base + "coverage").label == "tear coverage");
    // The seven that refuse routes are never offered: a route to one is refused at bind.
    for (const char* leaf : {"cell", "spacing", "stretch", "followWind", "direction", "drift", "wind"}) {
        INFO(leaf);
        CHECK(c.find(base + leaf) == nullptr);
    }
}
