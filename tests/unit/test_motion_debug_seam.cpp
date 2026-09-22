// Phase B §50 -- the numbers behind the overlays.
//
// The overlays themselves are proved in `tests/rendering/test_motion_debug.cpp`, where the
// geometry can be read back. This file proves the other half, and it is the half that can be
// wrong: **a panel that renders wrong numbers correctly is not debuggable, it is convincing.**
//
// `Composition::motionDebug` already answered "did the provider seam work". It could not answer
// "what is the animation actually doing", because the layer state lived inside `PoseLayerStack`
// and the body state inside `MotionContext`, and neither was reachable from outside the sink. That
// is now reported, and reported **as the frame left it** rather than re-derived -- a diagnostic
// that recomputes what it is diagnosing agrees with itself (ADR-182).

#include "assets/asset_registry.hpp"
#include "core/time.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "signals/signal_bus.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>

#include <filesystem>
#include <memory>
#include <string>

using namespace avgen;
using Catch::Approx;

namespace {
namespace fs = std::filesystem;
fs::path labScene() {
    return fs::path(AVGEN_SOURCE_DIR) / "examples" / "labs" / "character" /
           "character-intelligence-lab.scene.json";
}
} // namespace

TEST_CASE("the motion debug seam reports the layer state the frame ran with",
          "[motiondebug][phaseB][character]") {
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
    for (int i = 0; i < 180; ++i) {
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

    // Find a node the scene really drives, rather than asserting about a name that may have moved.
    std::string driven;
    for (const scene::SkinnedRig& r : comp->scene().rigs) {
        const std::size_t slash = r.name.find('/');
        if (slash == std::string::npos) {
            continue;
        }
        const scene::Composition::MotionDebug probe = comp->motionDebug(r.name.substr(0, slash));
        if (probe.found && !probe.layers.empty()) {
            driven = r.name.substr(0, slash);
            break;
        }
    }
    REQUIRE_FALSE(driven.empty()); // the fixture drives something with layers on it

    const scene::Composition::MotionDebug debug = comp->motionDebug(driven);
    WARN(fmt::format("node '{}': mode {}, phase {}, speed {:.3f} m/s, ground {}, look {}", driven,
                     scene::locomotionModeName(debug.mode),
                     scene::motionPhaseName(debug.motionPhase), debug.groundSpeed,
                     debug.hasGroundPlane ? "yes" : "no", debug.hasLookTarget ? "yes" : "no"));
    for (const scene::Composition::MotionDebug::LayerRow& row : debug.layers) {
        WARN(fmt::format("  {:<12} {:<10} requested {:.2f} realized {:.2f}  {}  ik={}", row.name,
                         scene::poseLayerKindName(row.kind), row.requestedWeight,
                         row.realizedWeight, scene::layerResolutionName(row.resolution),
                         scene::ikStatusName(row.ik)));
    }

    CHECK(debug.found);
    REQUIRE_FALSE(debug.layers.empty());

    // **Realized weight is reported separately from requested, and that is the point.** A layer at
    // requested 1.0 and realized 0.02 is two frames into a blend; a panel showing the request
    // would say it is fully on, which is §46's defect drawn as though it were not happening. After
    // three seconds every blend has finished, so here the two agree -- and the assertion that they
    // are reported as two fields, not one, is `realizedWeight` existing and tracking.
    int resolved = 0;
    for (const scene::Composition::MotionDebug::LayerRow& row : debug.layers) {
        CHECK(row.realizedWeight >= 0.0f);
        CHECK(row.realizedWeight <= 1.0f);
        if (row.requestedWeight > 0.0f) {
            CHECK(row.realizedWeight > 0.0f); // three seconds in, nothing is still blending
        } else {
            CHECK(row.realizedWeight == 0.0f);
        }
        if (row.resolution == scene::LayerResolution::Applied ||
            row.resolution == scene::LayerResolution::Clamped) {
            ++resolved;
        }
    }
    // Not a vacuous read: at least one layer actually did something this frame, so the resolutions
    // above are a report and not a row of defaults.
    CHECK(resolved > 0);

    // And the seam distinguishes a node it drives from one it does not, which is what stops every
    // assertion here passing on an empty struct.
    const scene::Composition::MotionDebug missing = comp->motionDebug("no-such-node");
    CHECK_FALSE(missing.found);
    CHECK(missing.layers.empty());
}
