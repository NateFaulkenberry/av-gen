// Phase B §46 -- that a layer arrives instead of appearing.
//
// The vertical slice found the defect: a layer switched on at full weight in one frame moved what
// it drives 0.803 m between two frames, thirty-five times the distance the body covered in the
// same frame. It found it in a harness, though, and a mechanism that only the harness sets is
// ADR-600 rebuilt -- a knob with no caller refuses nothing.
//
// So this file proves the two halves the slice cannot: that the seam publishes the schedule from
// **both** of its publishers (ADR-554, and this is the struct that rule was discovered on), and
// that `driveLayers` consumes it on the shipping path rather than the field merely existing.

#include "assets/asset_registry.hpp"
#include "core/time.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "scene/pose_layers.hpp"
#include "signals/signal_bus.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <memory>

using namespace avgen;
using Catch::Approx;

namespace {
namespace fs = std::filesystem;

fs::path labScene() {
    return fs::path(AVGEN_SOURCE_DIR) / "examples" / "labs" / "character" /
           "character-intelligence-lab.scene.json";
}
} // namespace

TEST_CASE("a blended weight is a pure function of how long ago the target changed",
          "[layers][blend][phaseB]") {
    scene::PoseLayer layer;
    layer.weight = 1.0f;
    layer.weightBefore = 0.0f;
    layer.blendSeconds = 0.4f;

    layer.blendElapsed = 0.0f;
    CHECK(layer.effectiveWeight() == Approx(0.0f));
    layer.blendElapsed = 0.2f;
    CHECK(layer.effectiveWeight() == Approx(0.5f)); // smoothstep is symmetric about its midpoint
    layer.blendElapsed = 0.4f;
    CHECK(layer.effectiveWeight() == Approx(1.0f));
    layer.blendElapsed = 40.0f;
    CHECK(layer.effectiveWeight() == Approx(1.0f)); // and stays there, however long ago it was

    // Monotone, and with zero slope at both ends -- which is the whole reason it is a smoothstep
    // and not a lerp. A lerp arrives with the velocity it had all the way, so the thing it drives
    // stops dead at the target, and a stop is a discontinuity in the derivative that reads as a
    // tick even when no position jumped.
    float previous = -1.0f;
    for (int i = 0; i <= 40; ++i) {
        layer.blendElapsed = static_cast<float>(i) * 0.01f;
        const float w = layer.effectiveWeight();
        CHECK(w >= previous);
        previous = w;
    }
    layer.blendElapsed = 0.01f;
    const float firstStep = layer.effectiveWeight();
    layer.blendElapsed = 0.21f;
    const float middleStep = layer.effectiveWeight() - 0.5f;
    CHECK(firstStep < middleStep); // it eases in rather than starting at full rate

    // Fading out is the same function run the other way, which matters because a layer that eases
    // in and snaps out is still a layer that snaps.
    layer.weight = 0.0f;
    layer.weightBefore = 1.0f;
    layer.blendElapsed = 0.2f;
    CHECK(layer.effectiveWeight() == Approx(0.5f));

    // **`blendSeconds` at zero is the old behaviour exactly**, to the bit, which is what makes this
    // safe to add to a struct every layer in the repository already uses.
    layer.blendSeconds = 0.0f;
    layer.weight = 0.7f;
    layer.blendElapsed = 0.0f;
    CHECK(layer.effectiveWeight() == 0.7f);
}

TEST_CASE("the shipping look drive sets the schedule, not just the weight",
          "[layers][blend][phaseB][character]") {
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
    for (int i = 0; i < 120; ++i) {
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

    // The lab's watcher carries a `look`-driven aim layer. After two seconds of ticking, the
    // shipping driver has to have written a blend time onto it -- not the test, not a fixture.
    // Delete the two lines in `driveLayers` and this fails; it cannot pass by the field existing.
    int lookLayers = 0;
    int scheduled = 0;
    for (const scene::SkinnedRig& r : comp->scene().rigs) {
        for (const scene::PoseLayer& layer : r.layers.layers()) {
            if (layer.drive != scene::PoseLayerDrive::Look) {
                continue;
            }
            ++lookLayers;
            if (layer.blendSeconds > 0.0f) {
                ++scheduled;
            }
        }
    }
    INFO("look-driven layers found: " << lookLayers);
    CHECK(lookLayers > 0);        // the scene really has one, so the next line is not vacuous
    CHECK(scheduled == lookLayers);
}
