// Phase B §50 -- procedural animation, made debuggable.
//
// "Add useful debug overlays. Potential toggles: Show Skeleton, Show IK Chains, Show IK Targets,
// Show Contacts, Show Ground Probes, Show Velocity, Show Desired Velocity, Show Facing, Show Look
// Target, Show Reach Target, Show Body Compensation, Show Motion Phase, Show Layer Weights. **This
// should make procedural animation debuggable.**"
//
// What makes this stage load-bearing rather than cosmetic: every defect this phase found was
// invisible until something measured it. A foot sliding 0.662 m. A layer arriving at full weight
// in one frame and moving a hand 0.803 m. An arm whose length changed by 21.4% as it walked. Each
// needed a purpose-built probe, and **a probe can only be written once somebody suspects the thing
// it measures.** An overlay is the half that works the other way round: it puts the quantity on
// the screen continuously, so the next one is seen before it is suspected. §46's six unauthored
// discontinuities took an 870-frame fixture and a control arm to find; a foot chain flashing from
// green to amber once per stride is the same finding at a glance.
//
// The rule these tests enforce is the one that separates a diagnostic from a picture: **each
// toggle must draw nothing when it is off, and the drawing must differ when the thing being
// diagnosed differs.** A chain drawn the same colour whatever the solver said is a picture of a
// leg, and this phase has already been caught by one of those.

#include "assets/gltf_loader.hpp"
#include "gpu/context.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/debug_draw.hpp"
#include "rendering/debug_visualizer.hpp"
#include "scene/animation.hpp"
#include "scene/pose_layers.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>

#include <filesystem>
#include <string>
#include <vector>

using namespace avgen;
using Catch::Approx;

namespace {
namespace fs = std::filesystem;

fs::path alienPath() {
    return fs::path(AVGEN_SOURCE_DIR) / "assets" / "aliens" / "alien-scout.glb";
}

// A scene with one posed alien, its layer stack run, and its model-space joints left where the
// renderer would leave them. `reachTarget` is the knob the control arms turn: near the hand it
// solves, far from it the solver clamps, and the overlay has to say which.
struct Fixture {
    scene::Scene scene;
    bool ok = false;

    explicit Fixture(const glm::vec3& reachTarget, bool withGround = true) {
        assets::GltfLoadOptions options;
        options.loadImages = false;
        if (!fs::exists(alienPath()) || !assets::loadGltf(alienPath(), scene, options).has_value() ||
            scene.rigs.empty()) {
            return;
        }
        scene::SkinnedRig& rig = scene.rigs.front();

        std::vector<scene::PoseLayer> layers;
        for (const char* side : {"l", "r"}) {
            scene::PoseLayer foot;
            foot.name = fmt::format("foot.{}", side);
            foot.kind = scene::PoseLayerKind::Foot;
            foot.chainRoot = fmt::format("thigh_twist.{}", side);
            foot.chainMid = fmt::format("leg_stretch.{}", side);
            foot.chainTip = fmt::format("foot.{}", side);
            foot.weight = 1.0f;
            foot.hasGround = withGround;
            foot.groundPoint = glm::vec3(0.0f);
            foot.groundNormal = glm::vec3(0.0f, 1.0f, 0.0f);
            foot.bodyVelocity = glm::vec3(0.0f, 0.0f, 1.2f);
            foot.bodyAcceleration = glm::vec3(0.4f, 0.0f, 0.9f);
            layers.push_back(foot);
        }
        scene::PoseLayer reach;
        reach.name = "reach";
        reach.kind = scene::PoseLayerKind::Reach;
        reach.chainRoot = "shoulder.l";
        reach.chainMid = "forearm_stretch.l";
        reach.chainTip = "hand.l";
        reach.weight = 1.0f;
        reach.hasTarget = true;
        reach.target = reachTarget;
        reach.bodyVelocity = glm::vec3(0.0f, 0.0f, 1.2f);
        reach.bodyAcceleration = glm::vec3(0.4f, 0.0f, 0.9f);
        layers.push_back(reach);

        if (!rig.layers.bind(std::move(layers), rig.skeleton, rig.clips).empty()) {
            return;
        }
        scene::Pose pose;
        scene::setRestPose(rig.skeleton, pose);
        rig.layers.apply(rig.skeleton, rig.clips, 0.0, pose);
        // What the renderer leaves behind, and what every overlay here reads.
        scene::poseToModel(rig.skeleton, pose, rig.scratchModel);

        scene::Entity entity;
        entity.name = "scout";
        entity.visible = true;
        entity.rig = 0;
        // Required, and not incidental: the debug builder skips any entity whose `mesh` is out of
        // range, so a fixture without one draws nothing and every arm below would have passed its
        // "off" control and failed its "on" one -- testing.md family C, in the fixture.
        entity.mesh = 0;
        entity.transform.position = glm::vec3(3.0f, 0.0f, -2.0f); // NOT the origin, deliberately
        scene.entities.push_back(entity);
        ok = true;
    }
};

std::size_t drawnWith(rendering::DebugDraw& draw, const scene::Scene& scene,
                      const rendering::DebugViewOptions& options) {
    draw.clear();
    rendering::buildDebugGeometry(draw, scene, options, 0.0);
    return draw.lineVertexCount() + draw.pointVertexCount();
}

} // namespace

TEST_CASE("each motion overlay draws only when it is asked to", "[debug][motion][phaseB]") {
    auto ctx = gpu::Context::create(gpu::ContextDesc{});
    if (!ctx) {
        SKIP("no GPU adapter available");
    }
    Fixture fixture(glm::vec3(0.30f, 1.10f, 0.35f));
    if (!fixture.ok) {
        SKIP("the Glowmere alien is not present");
    }
    gpu::ShaderLibrary shaders(**ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::DebugDraw draw(**ctx, shaders);

    rendering::DebugViewOptions options;
    // The control every one of the arms below is measured against: nothing on, nothing drawn.
    CHECK(drawnWith(draw, fixture.scene, options) == 0);

    struct Arm {
        const char* name;
        bool rendering::DebugViewOptions::*flag;
    };
    const Arm arms[] = {
        {"motionChains", &rendering::DebugViewOptions::motionChains},
        {"motionTargets", &rendering::DebugViewOptions::motionTargets},
        {"motionContacts", &rendering::DebugViewOptions::motionContacts},
        {"motionVectors", &rendering::DebugViewOptions::motionVectors},
    };
    for (const Arm& arm : arms) {
        rendering::DebugViewOptions one;
        one.*(arm.flag) = true;
        const std::size_t drawn = drawnWith(draw, fixture.scene, one);
        INFO(arm.name);
        CHECK(drawn > 0); // it draws something when on...
        rendering::DebugViewOptions off;
        CHECK(drawnWith(draw, fixture.scene, off) == 0); // ...and nothing when off
        WARN(fmt::format("{}: {} vertices", arm.name, drawn));
    }
}

TEST_CASE("an IK chain overlay says what the solver said", "[debug][motion][phaseB]") {
    // **The arm that can fail, and the reason this file is not decoration.** A chain drawn the same
    // whatever the solve returned is a picture of a leg. The target is moved from inside the arm's
    // reach to three metres away, which is `Clamped` rather than `Solved`, and the overlay's colour
    // has to change. Nothing else about the scene differs.
    auto ctx = gpu::Context::create(gpu::ContextDesc{});
    if (!ctx) {
        SKIP("no GPU adapter available");
    }
    Fixture reachable(glm::vec3(0.30f, 1.10f, 0.35f));
    Fixture unreachable(glm::vec3(3.00f, 1.10f, 0.35f));
    if (!reachable.ok || !unreachable.ok) {
        SKIP("the Glowmere alien is not present");
    }
    gpu::ShaderLibrary shaders(**ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::DebugDraw draw(**ctx, shaders);

    rendering::DebugViewOptions options;
    options.motionChains = true;

    const auto colours = [&](const scene::Scene& scene) {
        draw.clear();
        rendering::buildDebugGeometry(draw, scene, options, 0.0);
        std::vector<glm::vec4> out;
        for (const rendering::DebugVertex& v : draw.lineVertices()) {
            out.push_back(v.color);
        }
        return out;
    };

    const std::vector<glm::vec4> solved = colours(reachable.scene);
    const std::vector<glm::vec4> clamped = colours(unreachable.scene);
    REQUIRE(solved.size() == clamped.size()); // the same chains were drawn either way
    REQUIRE_FALSE(solved.empty());

    int differing = 0;
    for (std::size_t i = 0; i < solved.size(); ++i) {
        if (glm::length(glm::vec3(solved[i]) - glm::vec3(clamped[i])) > 1e-3f) {
            ++differing;
        }
    }
    WARN(fmt::format("{} of {} chain vertices changed colour when the target moved out of reach",
                     differing, solved.size()));
    CHECK(differing > 0);
    // And not *everything* changed: the two foot chains are solving the same thing in both scenes
    // and must be drawn identically. A diagnostic that recolours the whole rig when one limb
    // clamps is as uninformative as one that recolours none of it.
    CHECK(differing < static_cast<int>(solved.size()));
}

TEST_CASE("the contact overlay is absent when there is no ground to be on",
          "[debug][motion][phaseB]") {
    // `hasGround` false is not the same as a ground plane at zero, and an overlay that cannot tell
    // those apart is ADR-551's mistake drawn in green.
    auto ctx = gpu::Context::create(gpu::ContextDesc{});
    if (!ctx) {
        SKIP("no GPU adapter available");
    }
    Fixture grounded(glm::vec3(0.30f, 1.10f, 0.35f), true);
    Fixture airborne(glm::vec3(0.30f, 1.10f, 0.35f), false);
    if (!grounded.ok || !airborne.ok) {
        SKIP("the Glowmere alien is not present");
    }
    gpu::ShaderLibrary shaders(**ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::DebugDraw draw(**ctx, shaders);
    rendering::DebugViewOptions options;
    options.motionContacts = true;
    CHECK(drawnWith(draw, grounded.scene, options) > 0);
    CHECK(drawnWith(draw, airborne.scene, options) == 0);
}
