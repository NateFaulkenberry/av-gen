// Phase B §49 -- deterministic randomness.
//
// "Procedural idle and secondary motion should use deterministic seeds -- **characterSeed,
// layerSeed**. This permits reproducible renders, reproducible tests, deterministic offline baking,
// debugging. Do not use uncontrolled global randomness."
//
// My own audit marked this stage met, on the grounds that the secondary layer is a pure function
// of the timeline second with no random number generator anywhere near it. That is true, and it is
// only the second sentence. The first names two fields that did not exist: a body's variation was
// **hand-authored**, one `phase` per layer per character typed into the scene file. Five aliens can
// be hand-spread. A hundred cannot, and hand-authored spread is not a seed -- it is the absence of
// one, done by hand.
//
// So the audit was generous, and the thing that caught it was re-reading §49's own text rather
// than my memory of what had been built.

#include "assets/asset_registry.hpp"
#include "core/time.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "scene/pose_layers.hpp"
#include "signals/signal_bus.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace avgen;
using Catch::Approx;

namespace {
namespace fs = std::filesystem;
fs::path labScene() {
    return fs::path(AVGEN_SOURCE_DIR) / "examples" / "labs" / "character" /
           "character-intelligence-lab.scene.json";
}
} // namespace

TEST_CASE("an unseeded layer is byte-for-byte what it was", "[seeds][phaseB]") {
    // The whole reason `seedPhase(0, 0)` short-circuits to an exact zero rather than hashing two
    // zeroes like any other pair. Without it, adding this field would have shifted every secondary
    // layer in the repository, and the first anyone would have known is a render that no longer
    // matched -- a change with no author, which is the worst kind.
    CHECK(scene::seedPhase(0u, 0u) == 0.0f);
    // And it is a genuine special case, not a coincidence: neighbouring pairs are not near zero.
    CHECK(scene::seedPhase(1u, 0u) != 0.0f);
    CHECK(scene::seedPhase(0u, 1u) != 0.0f);
}

TEST_CASE("the same seeds give the same phase, in any order, forever", "[seeds][phaseB]") {
    // "Reproducible renders, reproducible tests, deterministic offline baking." All three reduce
    // to one property: the function has no state and no sequence, so calling it out of order, or
    // interleaved with other calls, or a million calls later, cannot change an answer.
    const float a = scene::seedPhase(12345u, 678u);
    for (std::uint32_t i = 0; i < 1000u; ++i) {
        (void)scene::seedPhase(i, i * 7u); // churn, in case anything is hiding a sequence
    }
    CHECK(scene::seedPhase(12345u, 678u) == a); // bit-for-bit, not Approx
    CHECK(a >= 0.0f);
    CHECK(a < 1.0f);

    // Pinned values. If someone "improves" the mixer, every previously rendered frame of every
    // scene with a seeded layer changes, and this is the test that says so before the render does.
    CHECK(scene::seedFromName("scout") == 3273829637u);
    CHECK(scene::seedFromName("") == 2166136261u);
    CHECK(scene::seedFromName("scout") != scene::seedFromName("diver"));
}

TEST_CASE("a hundred characters spread out without anyone authoring a hundred numbers",
          "[seeds][phaseB]") {
    // This is the half the audit missed, stated as a measurement. Hand-authoring gives five aliens
    // five phases; the question §49 is really asking is what happens at the hundred characters §47
    // profiled.
    std::vector<float> phases;
    phases.reserve(100);
    for (int i = 0; i < 100; ++i) {
        phases.push_back(scene::seedPhase(scene::seedFromName(fmt::format("alien-{:03d}", i)),
                                          scene::seedFromName("life")));
    }
    std::sort(phases.begin(), phases.end());
    float worstGap = 0.0f;
    float closest = 1.0f;
    for (std::size_t i = 1; i < phases.size(); ++i) {
        const float gap = phases[i] - phases[i - 1];
        worstGap = std::max(worstGap, gap);
        closest = std::min(closest, gap);
    }
    WARN(fmt::format("100 seeded phases: closest pair {:.5f} apart, largest empty gap {:.4f}",
                     closest, worstGap));
    // Not in lockstep: no two of a hundred characters breathe together...
    CHECK(closest > 1e-5f);
    // ...and no quarter of the cycle is empty, which is what "spread" has to mean for this to be
    // better than the five hand-typed numbers it replaces. A hash that piled everything into one
    // corner would satisfy the line above and fail the character.
    CHECK(worstGap < 0.25f);

    // Two layers on the *same* body also differ, which is what `layerSeed` is for and what a
    // per-character seed alone would not give.
    const std::uint32_t body = scene::seedFromName("scout");
    CHECK(scene::seedPhase(body, scene::seedFromName("breath")) !=
          scene::seedPhase(body, scene::seedFromName("sway")));
}

TEST_CASE("the shipping driver seeds every layer it drives", "[seeds][phaseB][character]") {
    // §49's mechanism with no caller is ADR-600 again, so the assertion that matters is this one:
    // the product wires it. Delete the two lines in `driveLayers` and this fails.
    if (!fs::exists(labScene())) {
        SKIP("the character lab scene is not present");
    }
    assets::AssetRegistry registry(labScene().parent_path());
    auto loaded = scene::Composition::loadFile(labScene(), registry);
    REQUIRE(loaded.has_value());
    std::unique_ptr<scene::Composition> comp = std::move(*loaded);
    params::ParameterSet params;
    params::Modulator modulator;
    signals::SignalBus bus;
    comp->attach(params, modulator);
    comp->setViewport(1280, 720);
    comp->scene().detailLimits.entityDistanceCull = false;
    FrameTime time;
    for (int i = 0; i < 60; ++i) {
        time.renderTime = static_cast<double>(i) / 60.0;
        time.deltaTime = i == 0 ? 0.0 : 1.0 / 60.0;
        time.frameIndex = static_cast<std::uint64_t>(i);
        params.resetFinals();
        bus.clearEvents();
        comp->updateFields(time, bus, modulator);
        modulator.applyRoutes(bus, params, time.deltaTime);
        comp->updateBehaviour(time, bus);
        comp->update(time);
    }

    int layers = 0;
    int seeded = 0;
    std::set<std::uint32_t> characterSeeds;
    std::set<std::uint32_t> layerSeeds;
    for (const scene::SkinnedRig& r : comp->scene().rigs) {
        for (const scene::PoseLayer& layer : r.layers.layers()) {
            ++layers;
            if (layer.characterSeed != 0u && layer.layerSeed != 0u) {
                ++seeded;
            }
            characterSeeds.insert(layer.characterSeed);
            layerSeeds.insert(layer.layerSeed);
        }
    }
    WARN(fmt::format("{} layer(s), {} seeded, {} distinct character seed(s), {} distinct layer "
                     "seed(s)",
                     layers, seeded, characterSeeds.size(), layerSeeds.size()));
    REQUIRE(layers > 0);     // the scene really has layers, so the next line is not vacuous
    CHECK(seeded == layers); // every one of them, whatever its drive
    // This lab scene carries two layers on one character, so the *character* seeds are correctly
    // identical and it is the LAYER seeds that must differ. Asserting distinct character seeds
    // here would have been a test that fails on a correct engine because the fixture has one
    // character -- which is a fixture that cannot answer the question, not a defect.
    CHECK(layerSeeds.size() == static_cast<std::size_t>(layers));
    CHECK(characterSeeds.size() >= 1);
}
