// Phase B §46 -- the first true vertical slice.
//
// Every stage before this one proved a mechanism in isolation, which is the right way to prove a
// mechanism and a poor way to find out whether they compose. §46 asks for one continuous run:
//
//     idle -> desired velocity -> accelerate into walk -> change direction -> curved trajectory
//     -> uneven terrain -> feet adapt -> pelvis compensates -> look toward target -> slow ->
//     stop naturally -> reach toward target -> return to idle
//
// driven by **MotionRequests**, not by a hand-authored sequence. "Should look like a continuous
// character motion system, not a collection of disconnected demos."
//
// The word doing the work there is *continuous*, and it is measurable: across the whole run, and
// especially across the frames where the script changes its mind, no joint may move further in one
// frame than a body moving at this speed could account for. That is the assertion.
//
// ADR-182 decides the evidence, because "nothing jumped" is satisfiable by a stack that does
// nothing at all. Three things have to hold together:
//
//   1. **Continuity.** Per-frame joint displacement stays under a bound, at every frame, including
//      the eight transition frames where the script switches phase.
//   2. **The run actually happened.** Every scripted phase is observed to have occurred -- the
//      speed crossed its thresholds, the facing turned through its angle, the ground height moved,
//      the look and reach layers resolved `Applied`. A stack that froze the rig passes (1) and
//      fails all of (2).
//   3. **The controller is what makes it continuous**, not the clip and not luck. A control arm
//      runs the identical script with the controller bypassed -- the scripted velocity fed
//      straight through -- and has to breach the bound (3).
//
// (3) is the arm that can fail, and it is the reason this file is not a demo.

#include "assets/gltf_loader.hpp"
#include "entity/motion_controller.hpp"
#include "scene/animation.hpp"
#include "scene/motion_context.hpp"
#include "scene/pose_layers.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
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

// The script, as phases of a single continuous timeline. Each entry is what the *behaviour* tier
// would be asking for in Phase D; here it is written out, which §55-57 explicitly permits ("for
// now, use scripted MotionRequests to validate Phase B").
struct Beat {
    const char* name;
    float seconds;
    glm::vec3 desiredVelocity;
    glm::vec3 desiredFacing;
    bool look;
    bool reach;
    float groundHeight; // metres, the terrain under the character during this beat
};

const Beat kScript[] = {
    {"idle",        1.0f, {0.0f, 0.0f, 0.0f},  {0.0f, 0.0f, 1.0f}, false, false, 0.00f},
    {"accelerate",  1.5f, {0.0f, 0.0f, 1.4f},  {0.0f, 0.0f, 1.0f}, false, false, 0.00f},
    {"walk",        1.5f, {0.0f, 0.0f, 1.4f},  {0.0f, 0.0f, 1.0f}, false, false, 0.00f},
    {"turn",        2.0f, {1.4f, 0.0f, 0.0f},  {1.0f, 0.0f, 0.0f}, false, false, 0.00f},
    {"curve",       1.5f, {1.0f, 0.0f, -1.0f}, {1.0f, 0.0f, -1.0f}, false, false, 0.12f},
    {"slope",       1.5f, {1.0f, 0.0f, -1.0f}, {1.0f, 0.0f, -1.0f}, true,  false, 0.35f},
    {"slow",        1.5f, {0.3f, 0.0f, -0.3f}, {1.0f, 0.0f, -1.0f}, true,  false, 0.35f},
    {"stop",        1.0f, {0.0f, 0.0f, 0.0f},  {1.0f, 0.0f, -1.0f}, true,  true,  0.35f},
    {"reach",       1.5f, {0.0f, 0.0f, 0.0f},  {1.0f, 0.0f, -1.0f}, true,  true,  0.35f},
    {"return",      1.5f, {0.0f, 0.0f, 0.0f},  {0.0f, 0.0f, 1.0f}, false, false, 0.35f},
};

// A flat ground plane at whatever height the current beat says, so the "uneven terrain" leg of the
// slice is a real change in what the ground query answers rather than a flag.
class StepGround final : public scene::IGroundQuery {
public:
    float height = 0.0f;
    [[nodiscard]] scene::GroundSample sampleAt(const glm::vec3& worldPoint) const override {
        scene::GroundSample out;
        out.point = glm::vec3(worldPoint.x, height, worldPoint.z);
        out.normal = glm::vec3(0.0f, 1.0f, 0.0f);
        out.distance = worldPoint.y - height;
        out.category = scene::GroundCategory::Authored;
        out.valid = true;
        return out;
    }
};

struct SliceResult {
    float worstStep = 0.0f;       // worst per-frame joint displacement, metres
    std::string worstWhere;       // which beat it happened in
    float worstTransition = 0.0f; // worst step on a frame where the beat changed
    double worstTime = 0.0;
    bool worstWasClipWrap = false;
    int worstJoint = -1;
    float worstWrapStep = 0.0f;   // worst step on a frame where the walk clip looped
    float topSpeed = 0.0f;
    float facingSwept = 0.0f;     // total radians the facing turned through
    float groundLow = 1e9f;
    float groundHigh = -1e9f;
    int lookApplied = 0;
    int reachApplied = 0;
    int footApplied = 0;
    int frames = 0;
};

// One run of the whole script. `useController` is the arm switch: true runs MotionRequest through
// `stepMotion` as the product does, false feeds the scripted velocity straight to the layers.
SliceResult runSlice(const scene::Skeleton& sk, const std::vector<scene::AnimationClip>& clips,
                     scene::PoseLayerStack& stack, bool useController) {
    SliceResult out;
    entity::MotionState state;
    entity::MotionLimits limits;
    StepGround ground;

    const scene::AnimationClip* walk = nullptr;
    for (const scene::AnimationClip& c : clips) {
        if (c.name.find("Walking") != std::string::npos && c.length() > 0.0f) {
            walk = &c;
            break;
        }
    }
    REQUIRE(walk != nullptr);

    scene::Pose pose;
    std::vector<glm::mat4> model;
    std::vector<glm::vec3> previous;
    const float dt = 1.0f / 60.0f;
    double now = 0.0;
    glm::vec3 previousFacing{0.0f, 0.0f, 1.0f};
    float previousClipTime = 0.0f;
    bool lookOn = false;
    bool reachOn = false;
    float lookBefore = 0.0f;
    float reachBefore = 0.0f;
    double lookSince = 0.0;
    double reachSince = 0.0;
    int beatIndex = 0;
    float previousGround = kScript[0].groundHeight;

    for (const Beat& beat : kScript) {
        const int frames = static_cast<int>(beat.seconds * 60.0f);
        for (int f = 0; f < frames; ++f) {
            const bool transition = (f == 0 && beatIndex > 0);
            // The terrain ramps across the beat instead of stepping between beats. The first
            // version of this script stepped it, and the slice's largest remaining discontinuity
            // was 0.2305 m on `toes_01.r` at exactly the frame the number changed -- the ground
            // teleporting 0.23 m under a standing foot. That is a defect in the script, not in the
            // stack, and the distinction matters: a continuity bound is only meaningful over
            // inputs a world could actually present.
            const float ramp = static_cast<float>(f) / static_cast<float>(std::max(frames, 1));
            const float groundNow = previousGround + (beat.groundHeight - previousGround) * ramp;

            entity::MotionRequest request;
            request.desiredVelocity = beat.desiredVelocity;
            request.desiredFacing = glm::normalize(beat.desiredFacing);
            entity::MotionSolution solution;
            if (useController) {
                solution = entity::stepMotion(request, state, limits, dt, state);
            } else {
                // The control: no limiting, no smoothing. The scripted velocity *is* the answer,
                // which is exactly what a system without a motion controller would do.
                solution.velocity = request.desiredVelocity;
                solution.facing = request.desiredFacing;
                solution.speed = glm::length(glm::vec3(solution.velocity.x, 0.0f, solution.velocity.z));
                solution.acceleration = (solution.velocity - state.velocity) / dt;
                state.velocity = solution.velocity;
                state.facing = solution.facing;
                state.started = true;
            }

            ground.height = groundNow;
            out.topSpeed = std::max(out.topSpeed, solution.speed);
            out.groundLow = std::min(out.groundLow, groundNow);
            out.groundHigh = std::max(out.groundHigh, groundNow);
            out.facingSwept += std::acos(std::clamp(glm::dot(previousFacing, solution.facing), -1.0f, 1.0f));
            previousFacing = solution.facing;

            // MotionContext is the seam the layers read (§4). Built here the way `driveLayers`
            // builds it from `LocomotionState`, because this test stands one tier below the scene.
            scene::MotionContext ctx;
            ctx.dt = dt;
            ctx.time = now;
            ctx.velocity = solution.velocity;
            ctx.facing = solution.facing;
            ctx.groundSpeed = solution.speed;
            ctx.turnRate = solution.turnRate;
            ctx.acceleration = solution.acceleration;
            ctx.desiredVelocity = request.desiredVelocity;
            ctx.desiredFacing = request.desiredFacing;
            ctx.ground = &ground;
            ctx.groundPoint = glm::vec3(0.0f, groundNow, 0.0f);
            ctx.hasGroundPlane = true;
            ctx.mode = solution.speed > 0.1f ? scene::LocomotionMode::Walk : scene::LocomotionMode::Idle;
            ctx.hasLookTarget = beat.look;
            ctx.lookTarget = glm::vec3(1.2f, 1.4f, 1.5f);

            // The blend schedule, stated rather than accumulated (§46). `lookSince`/`reachSince`
            // are the sample second at which the script last changed its mind about that layer --
            // a property of the script, which is why this is reconstructable at frame N and a
            // remembered ramp would not be.
            if (beat.look != lookOn) {
                lookBefore = lookOn ? 1.0f : 0.0f;
                lookOn = beat.look;
                lookSince = now;
            }
            if (beat.reach != reachOn) {
                reachBefore = reachOn ? 1.0f : 0.0f;
                reachOn = beat.reach;
                reachSince = now;
            }

            // Per-frame layer intent, written the way the composition writes it.
            for (scene::PoseLayer& layer : stack.layers()) {
                switch (layer.kind) {
                case scene::PoseLayerKind::Stride:
                    // **Not** `speed > threshold ? speed/authored : 1.0`. That is the shape
                    // `Gait::footSlip` has, and the slice caught it: at the walk->stand transition
                    // the ratio steps from 0.3 to 1.0 in one frame and the stride layer triples a
                    // foot's excursion between two frames -- 0.301 m on `leg_twist.l`, which was
                    // the largest discontinuity left in the run after the reach blend was fixed.
                    //
                    // The ratio and the weight say different things, and the threshold conflated
                    // them. "How far out of step is this clip" is only meaningful while there is a
                    // stride; "should this layer do anything" is the weight. So the ratio stays
                    // continuous all the way to zero speed and the *weight* falls off instead --
                    // and a weight of zero scales nothing, which is what ratio 1.0 was trying to
                    // say by jumping.
                    //
                    // The weight is the ratio, clamped, and that is one coefficient rather than
                    // two: a stride layer's authority *is* how much stride there is. The first
                    // attempt used a separate 0.4 m/s window and left 0.1495 m on `leg_stretch.r`
                    // four frames into the acceleration, because at 6 m/s^2 the weight finished
                    // its sweep while the ratio was still at 0.28 -- the layer arrived at full
                    // authority before it had anything to say. Tying them together spreads the
                    // whole transition over the whole acceleration, which is where it belongs.
                    layer.strideRatio = solution.speed / 1.4f;
                    layer.weight = std::clamp(layer.strideRatio, 0.0f, 1.0f);
                    layer.bodySpeed = solution.speed;
                    break;
                case scene::PoseLayerKind::Lean:
                    layer.weight = 1.0f;
                    layer.bodyAcceleration = solution.acceleration;
                    layer.bodyTurnRate = solution.turnRate;
                    layer.bodySpeed = solution.speed;
                    break;
                case scene::PoseLayerKind::Foot:
                    layer.weight = 1.0f;
                    layer.hasGround = true;
                    layer.groundPoint = glm::vec3(0.0f, groundNow, 0.0f);
                    layer.groundNormal = glm::vec3(0.0f, 1.0f, 0.0f);
                    break;
                case scene::PoseLayerKind::Aim:
                    layer.weight = beat.look ? 1.0f : 0.0f;
                    layer.weightBefore = lookBefore;
                    layer.blendElapsed = static_cast<float>(now - lookSince);
                    layer.blendSeconds = 0.25f;
                    // The target stays live through the fade-out, or the layer would blend a
                    // weight down while the thing it is aiming at vanishes -- `NoTarget` at
                    // weight 0.4 is still a snap.
                    layer.hasTarget = layer.effectiveWeight() > 0.0f;
                    layer.target = ctx.lookTarget;
                    break;
                case scene::PoseLayerKind::Reach:
                    layer.weight = beat.reach ? 1.0f : 0.0f;
                    layer.weightBefore = reachBefore;
                    layer.blendElapsed = static_cast<float>(now - reachSince);
                    // A reach is not a glance. The hand covers 0.803 m getting to this target; at
                    // the look layer's 0.25 s that is 3.2 m/s and the fingertips were still moving
                    // 0.0983 m per frame at the peak of the smoothstep. 0.45 s puts the hand at a
                    // brisk-but-human 1.8 m/s average. The number is set by how far the hand has
                    // to go, not by the bound it has to pass.
                    layer.blendSeconds = 0.45f;
                    layer.hasTarget = layer.effectiveWeight() > 0.0f;
                    layer.target = glm::vec3(0.30f, 1.10f, 0.35f);
                    break;
                default:
                    break;
                }
            }

            const float clipTime = std::fmod(static_cast<float>(now), walk->length());
            const bool wrapped = clipTime < previousClipTime;
            previousClipTime = clipTime;
            scene::setRestPose(sk, pose);
            scene::sampleClip(*walk, walk->start + clipTime, pose);
            stack.apply(sk, clips, now, pose);
            scene::poseToModel(sk, pose, model);

            std::vector<glm::vec3> positions(model.size());
            for (std::size_t j = 0; j < model.size(); ++j) {
                positions[j] = glm::vec3(model[j][3]);
            }
            if (!previous.empty()) {
                float step = 0.0f;
                int stepJoint = -1;
                for (std::size_t j = 0; j < positions.size(); ++j) {
                    const float d = glm::length(positions[j] - previous[j]);
                    if (d > step) {
                        step = d;
                        stepJoint = static_cast<int>(j);
                    }
                }
                if (step > out.worstStep) {
                    out.worstStep = step;
                    out.worstWhere = beat.name;
                    out.worstTime = now;
                    out.worstWasClipWrap = wrapped;
                    out.worstJoint = stepJoint;
                }
                if (wrapped) {
                    out.worstWrapStep = std::max(out.worstWrapStep, step);
                }
                if (transition) {
                    out.worstTransition = std::max(out.worstTransition, step);
                }
            }
            previous = positions;

            const std::vector<scene::LayerResolution>& results = stack.results();
            for (std::size_t i = 0; i < stack.layers().size() && i < results.size(); ++i) {
                if (results[i] != scene::LayerResolution::Applied &&
                    results[i] != scene::LayerResolution::Clamped) {
                    continue;
                }
                switch (stack.layers()[i].kind) {
                case scene::PoseLayerKind::Aim: ++out.lookApplied; break;
                case scene::PoseLayerKind::Reach: ++out.reachApplied; break;
                case scene::PoseLayerKind::Foot: ++out.footApplied; break;
                default: break;
                }
            }

            now += static_cast<double>(dt);
            ++out.frames;
        }
        previousGround = beat.groundHeight;
        ++beatIndex;
    }
    return out;
}

std::vector<scene::PoseLayer> sliceLayers() {
    std::vector<scene::PoseLayer> layers;
    // Configured the way `glowmere-valley-2-multicam.scene.json` configures the shipping aliens,
    // because a slice built on layer settings the product does not use proves the slice, not the
    // product.
    for (const char* side : {"l", "r"}) {
        scene::PoseLayer stride;
        stride.name = fmt::format("stride.{}", side);
        stride.kind = scene::PoseLayerKind::Stride;
        stride.strideJoint = fmt::format("foot.{}", side);
        stride.strideOrigin = "root.x";
        layers.push_back(stride);
    }

    scene::PoseLayer lean;
    lean.name = "lean";
    lean.kind = scene::PoseLayerKind::Lean;
    lean.mask.joints = {"spine_02.x", "spine_03.x"};
    layers.push_back(lean);

    for (const char* side : {"l", "r"}) {
        scene::PoseLayer foot;
        foot.name = fmt::format("foot.{}", side);
        foot.kind = scene::PoseLayerKind::Foot;
        foot.chainRoot = fmt::format("thigh_twist.{}", side);
        foot.chainMid = fmt::format("leg_stretch.{}", side);
        foot.chainTip = fmt::format("foot.{}", side);
        layers.push_back(foot);
    }

    scene::PoseLayer look;
    look.name = "look";
    look.kind = scene::PoseLayerKind::Aim;
    look.pivot = "head.x";
    look.mask.joints = {"head.x", "Eye_L", "Eye_R"};
    look.maxYawDegrees = 75.0f;
    look.maxPitchDegrees = 30.0f;
    layers.push_back(look);

    scene::PoseLayer reach;
    reach.name = "reach";
    reach.kind = scene::PoseLayerKind::Reach;
    reach.chainRoot = "shoulder.l";
    reach.chainMid = "forearm_stretch.l";
    reach.chainTip = "hand.l";
    layers.push_back(reach);
    return layers;
}

} // namespace

TEST_CASE("the whole stack runs one continuous script without a joint jumping",
          "[slice][phaseB][aliens]") {
    if (!fs::exists(alienPath())) {
        SKIP("the Glowmere alien is not present");
    }
    scene::Scene sc;
    assets::GltfLoadOptions options;
    options.loadImages = false;
    REQUIRE(assets::loadGltf(alienPath(), sc, options).has_value());
    REQUIRE_FALSE(sc.rigs.empty());
    const scene::SkinnedRig& rig = sc.rigs.front();

    scene::PoseLayerStack stack;
    const std::vector<std::string> missing = stack.bind(sliceLayers(), rig.skeleton, rig.clips);
    for (const std::string& m : missing) {
        WARN(fmt::format("unresolved joint name: {}", m));
    }
    CHECK(missing.empty());

    const SliceResult driven = runSlice(rig.skeleton, rig.clips, stack, true);

    WARN(fmt::format("{} frames over {:.1f}s; top speed {:.2f} m/s; facing swept {:.0f} deg; "
                     "ground {:.2f}..{:.2f}m",
                     driven.frames, driven.frames / 60.0f, driven.topSpeed,
                     driven.facingSwept * 57.2957795f, driven.groundLow, driven.groundHigh));
    WARN(fmt::format("worst per-frame joint step {:.4f}m (in '{}'); worst on a beat change {:.4f}m",
                     driven.worstStep, driven.worstWhere, driven.worstTransition));
    WARN(fmt::format("worst step at t={:.3f}s on joint '{}', on a clip wrap: {}; worst on any "
                     "wrap {:.4f}m",
                     driven.worstTime,
                     driven.worstJoint >= 0
                         ? rig.skeleton.joints[static_cast<std::size_t>(driven.worstJoint)].name
                         : "?",
                     driven.worstWasClipWrap ? "YES" : "no", driven.worstWrapStep));
    WARN(fmt::format("look resolved on {} frames, reach on {}, feet on {}", driven.lookApplied,
                     driven.reachApplied, driven.footApplied));

    // (2) THE RUN ACTUALLY HAPPENED. Without these, (1) is satisfied by a frozen rig.
    CHECK(driven.frames == 870);
    CHECK(driven.topSpeed > 1.2f);                    // it got up to walking speed
    CHECK(driven.facingSwept > 2.0f);                 // and turned through more than 115 degrees
    CHECK(driven.groundHigh - driven.groundLow > 0.3f); // the terrain really moved under it
    CHECK(driven.lookApplied > 200);                  // the look layer resolved while looking
    CHECK(driven.reachApplied > 100);                 // and the reach layer while reaching
    CHECK(driven.footApplied > 1500);                 // both feet, most frames

    // (1) CONTINUITY. A joint on a body walking at 1.4 m/s covers 0.023 m per frame at 60 Hz; the
    // extremities of a stride swing several times that. The bound is set at 0.08 m -- generous for
    // ordinary motion and far below the 0.3 m a velocity discontinuity produces -- and asserted
    // separately on the beat changes, which are the frames a disconnected demo would fail on.
    CHECK(driven.worstStep < 0.08f);
    CHECK(driven.worstTransition < 0.08f);
}

TEST_CASE("without the motion controller the same script is not continuous",
          "[slice][phaseB][aliens]") {
    // (3) THE ARM THAT CAN FAIL. Same script, same layers, same clip, same ground -- the only
    // difference is that the scripted velocity is handed to the layers unfiltered instead of
    // through `stepMotion`. If this passes the continuity bound, then the bound is loose or the
    // clip was carrying the motion all along, and the first test proves nothing about the
    // controller.
    if (!fs::exists(alienPath())) {
        SKIP("the Glowmere alien is not present");
    }
    scene::Scene sc;
    assets::GltfLoadOptions options;
    options.loadImages = false;
    REQUIRE(assets::loadGltf(alienPath(), sc, options).has_value());
    const scene::SkinnedRig& rig = sc.rigs.front();

    scene::PoseLayerStack stack;
    const std::vector<std::string> missing = stack.bind(sliceLayers(), rig.skeleton, rig.clips);
    CHECK(missing.empty());

    const SliceResult raw = runSlice(rig.skeleton, rig.clips, stack, false);
    WARN(fmt::format("uncontrolled: worst step {:.4f}m (in '{}'), worst on a beat change {:.4f}m",
                     raw.worstStep, raw.worstWhere, raw.worstTransition));
    CHECK(raw.worstTransition > 0.08f);
}

TEST_CASE("the slice is reproducible", "[slice][phaseB][aliens]") {
    // §49 in advance, and free here: the same script run twice has to give the same numbers to the
    // bit, or nothing measured above is a baseline. This is the cheapest possible determinism
    // probe and it covers every layer the slice touches.
    if (!fs::exists(alienPath())) {
        SKIP("the Glowmere alien is not present");
    }
    scene::Scene sc;
    assets::GltfLoadOptions options;
    options.loadImages = false;
    REQUIRE(assets::loadGltf(alienPath(), sc, options).has_value());
    const scene::SkinnedRig& rig = sc.rigs.front();

    scene::PoseLayerStack a;
    scene::PoseLayerStack b;
    CHECK(a.bind(sliceLayers(), rig.skeleton, rig.clips).empty());
    CHECK(b.bind(sliceLayers(), rig.skeleton, rig.clips).empty());
    const SliceResult first = runSlice(rig.skeleton, rig.clips, a, true);
    const SliceResult second = runSlice(rig.skeleton, rig.clips, b, true);
    CHECK(first.worstStep == second.worstStep);
    CHECK(first.topSpeed == second.topSpeed);
    CHECK(first.facingSwept == second.facingSwept);
    CHECK(first.lookApplied == second.lookApplied);
    CHECK(first.reachApplied == second.reachApplied);
}
